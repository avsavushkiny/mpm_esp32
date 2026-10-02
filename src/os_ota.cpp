#include "os_ota.h"
#include "os_kernel.h"
#include "os_console.h"
#include "driver/uart.h"
#include <stdarg.h>
#include <string.h>
#include <Update.h>
#include <WiFi.h>
#include <HTTPClient.h>

//  Глобальное состояние OTA-сессии
static uint16_t g_otaConsoleId = 0;   // консоль, откуда вызван otaurl (0 = нет)

//  Хелпер: пишет и в Serial (UART0), и в консоль-источник
static void otaLog(const char* fmt, ...)
{
    char buf[192];

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;

    if (g_otaConsoleId != 0) {
        // Пишем через консоль-источник.
        // Serial (консоль 1) уже сам отправит в UART через TMP-очередь.
        osConsoleWrite(g_otaConsoleId, buf);
    } else {
        // Консоль неизвестна (вызов из setup) — пишем напрямую в UART.
        uart_write_bytes(UART_NUM_0, buf, n);
    }
}

//  Синхронное обновление по URL (вызывается из otaTask)
bool osOtaUpdateFromUrl(const char* url, char* outErr, size_t errMax)
{
    if (!url || !*url) {
        if (outErr) snprintf(outErr, errMax, "empty url");
        return false;
    }
    if (!WiFi.isConnected()) {
        if (outErr) snprintf(outErr, errMax, "no wifi");
        return false;
    }

    otaLog("[OTA] fetching %s\r\n", url);

    HTTPClient http;
    if (!http.begin(url)) {
        if (outErr) snprintf(outErr, errMax, "bad url");
        return false;
    }

    // Следовать редиректам (GitHub отдаёт 302 на raw.githubusercontent.com)
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.setUserAgent("ESP32-OTA/1.0");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        if (outErr) snprintf(outErr, errMax, "http %d", code);
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0) {
        if (outErr) snprintf(outErr, errMax, "unknown size");
        http.end();
        return false;
    }

    otaLog("[OTA] size: %d bytes\r\n", len);

    // Проверим, что .bin влезает в OTA-раздел
    uint32_t freeSpace = ESP.getFreeSketchSpace();
    if ((uint32_t)len > freeSpace) {
        if (outErr) snprintf(outErr, errMax,
                             "too large: %d > %lu", len, (unsigned long)freeSpace);
        http.end();
        return false;
    }

    if (!Update.begin(len)) {
        if (outErr) snprintf(outErr, errMax, "begin: %s", Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t  buf[1024];
    int      written = 0;
    uint32_t lastProgress = 0;
    uint32_t lastDataTime = millis();

    while (written < len) {
        // Проверка обрыва соединения
        if (!http.connected() && !stream->available()) {
            if (outErr) snprintf(outErr, errMax,
                                 "disconnected at %d / %d", written, len);
            Update.end(false);
            http.end();
            return false;
        }

        size_t avail = stream->available();
        if (!avail) {
            // Ничего не приходит — ждём, но не бесконечно
            if (millis() - lastDataTime > 15000) {
                if (outErr) snprintf(outErr, errMax,
                                     "timeout at %d / %d", written, len);
                Update.end(false);
                http.end();
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int n = stream->readBytes(buf, min((size_t)sizeof(buf), avail));
        if (n <= 0) break;

        if (Update.write(buf, n) != (size_t)n) {
            if (outErr) snprintf(outErr, errMax,
                                 "write failed at %d", written);
            Update.end(false);
            http.end();
            return false;
        }
        written      += n;
        lastDataTime  = millis();

        // Прогресс раз в OS_OTA_PROGRESS_MS
        uint32_t now = millis();
        if (now - lastProgress > OS_OTA_PROGRESS_MS) {
            lastProgress = now;
            int pct = (int)((written * 100ULL) / len);
            otaLog("[OTA] %d%% (%d / %d)\r\n", pct, written, len);
        }
    }

    http.end();

    if (written != len) {
        if (outErr) snprintf(outErr, errMax,
                             "incomplete: %d / %d", written, len);
        Update.end(false);
        return false;
    }

    if (!Update.end(true)) {
        if (outErr) snprintf(outErr, errMax,
                             "finalize: %s", Update.errorString());
        return false;
    }

    otaLog("[OTA] success, rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
    return true;   // сюда не дойдём
}

//  Контекст для задачи
struct OtaCtx {
    char     url[OS_OTA_URL_MAX];
    uint16_t consoleId;
};

//  Задача OTA — работает в фоне, CLI свободен
static void otaTask(void* arg)
{
    OtaCtx* ctx = static_cast<OtaCtx*>(arg);
    if (!ctx) {
        vTaskDelete(nullptr);
        return;
    }

    // Запоминаем консоль-источник для otaLog()
    g_otaConsoleId = ctx->consoleId;

    char err[64] = {0};
    bool ok = osOtaUpdateFromUrl(ctx->url, err, sizeof(err));

    if (!ok) {
        otaLog("[OTA] failed: %s\r\n", err);
    }
    // При успехе osOtaUpdateFromUrl вызовет ESP.restart() — сюда не дойдём

    g_otaConsoleId = 0;

    delete ctx;
    vTaskDelete(nullptr);
}

//  Публичный API: запустить OTA в фоне
bool osOtaStartFromUrl(const char* url, uint16_t consoleId)
{
    if (!url || !*url) return false;

    OtaCtx* ctx = new OtaCtx{};
    if (!ctx) return false;

    strncpy(ctx->url, url, sizeof(ctx->url) - 1);
    ctx->url[sizeof(ctx->url) - 1] = '\0';
    ctx->consoleId = consoleId;

    BaseType_t rc = xTaskCreatePinnedToCore(
        otaTask,
        "ota_task",
        32768,          // 32 КБ — с запасом для HTTPS + TLS
        ctx,
        2,              // приоритет
        nullptr,        // handle не нужен
        OS_CORE_NET     // CORE 0, рядом с WiFi
    );

    if (rc != pdPASS) {
        delete ctx;
        return false;
    }

    return true;
}