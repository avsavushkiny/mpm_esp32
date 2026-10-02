#include "os_ota.h"
#include "os_kernel.h"
#include "os_console.h"
#include <Update.h>
#include <WiFi.h>
#include <HTTPClient.h>

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

    osPrintf("[OTA] fetching %s\r\n", url);

    HTTPClient http;
    if (!http.begin(url)) {
        if (outErr) snprintf(outErr, errMax, "bad url");
        return false;
    }
    http.setTimeout(15000);

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

    osPrintf("[OTA] size: %d bytes\r\n", len);

    if (!Update.begin(len)) {
        if (outErr) snprintf(outErr, errMax, "begin: %s", Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int written = 0;
    uint32_t lastProgress = 0;

    while (http.connected() && written < len) {
        size_t avail = stream->available();
        if (!avail) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int n = stream->readBytes(buf, min((size_t)sizeof(buf), avail));
        if (n <= 0) break;

        if (Update.write(buf, n) != (size_t)n) {
            if (outErr) snprintf(outErr, errMax, "write failed");
            Update.end(false);
            http.end();
            return false;
        }
        written += n;

        uint32_t now = millis();
        if (now - lastProgress > OS_OTA_PROGRESS_MS) {
            lastProgress = now;
            int pct = (int)((written * 100ULL) / len);
            osPrintf("[OTA] %d%% (%d / %d)\r\n", pct, written, len);
        }
    }

    http.end();

    if (written != len) {
        if (outErr) snprintf(outErr, errMax, "incomplete: %d / %d", written, len);
        Update.end(false);
        return false;
    }

    if (!Update.end(true)) {
        if (outErr) snprintf(outErr, errMax, "finalize: %s", Update.errorString());
        return false;
    }

    osPrintf("[OTA] success, rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
    return true;   // сюда не дойдём
}

// ---------- Контекст для задачи ----------
struct OtaCtx {
    char      url[OS_OTA_URL_MAX];
    uint16_t  consoleId;
};

// ---------- Задача OTA ----------
static void otaTask(void* arg)
{
    OtaCtx* ctx = static_cast<OtaCtx*>(arg);
    if (!ctx) { vTaskDelete(nullptr); return; }

    char err[64] = {0};
    bool ok = osOtaUpdateFromUrl(ctx->url, err, sizeof(err));

    if (!ok) {
        osPrintf("[OTA] failed: %s\r\n", err);
        if (ctx->consoleId != 0) {
            osConsolePrintf(ctx->consoleId,
                            "\r\n[OTA] failed: %s\r\n", err);
        }
    }
    // При успехе osOtaUpdateFromUrl вызовет ESP.restart() — сюда не дойдём

    delete ctx;
    vTaskDelete(nullptr);
}

// ---------- Публичный API для CLI ----------
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
        32768,             // 32 КБ — с запасом для HTTPS+TLS
        ctx,
        2,                 // приоритет
        nullptr,           // handle не нужен
        OS_CORE_NET        // CORE 0, рядом с WiFi
    );

    if (rc != pdPASS) {
        delete ctx;
        return false;
    }
    return true;
}