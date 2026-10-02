#include "os_ota.h"
#include "os_kernel.h"
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