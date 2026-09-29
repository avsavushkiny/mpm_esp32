#include "os_net.h"
#include "os_kernel.h"     // для osMapPriority()
#include <WiFi.h>
#include <WiFiServer.h>

static WiFiServer*  g_telnetServer = nullptr;
static TaskHandle_t g_telnetTask   = nullptr;

bool osNetInit(const char* ssid, const char* pass)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
        vTaskDelay(pdMS_TO_TICKS(100));

    if (WiFi.status() == WL_CONNECTED) {
        OS_LOG("WiFi connected: %s", WiFi.localIP().toString().c_str());
        return true;
    }
    OS_LOG("WiFi connect failed");
    return false;
}

bool   osNetConnected() { return WiFi.status() == WL_CONNECTED; }
String osNetIp()        { return WiFi.localIP().toString(); }

extern void osTmpHandleTelnetClient(WiFiClient& client);

static void telnetAcceptTask(void* arg)
{
    WiFiServer* srv = static_cast<WiFiServer*>(arg);
    for (;;) {
        WiFiClient client = srv->available();
        if (client) osTmpHandleTelnetClient(client);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool osNetStartTelnet(uint16_t port)
{
    if (g_telnetServer) return true;
    g_telnetServer = new WiFiServer(port);
    g_telnetServer->begin();
    g_telnetServer->setNoDelay(true);

    xTaskCreatePinnedToCore(
        telnetAcceptTask, "telnet_acc", 4096, g_telnetServer,
        osMapPriority(OS_PRIO_NORMAL), &g_telnetTask, OS_CORE_NET);

    OS_LOG("Telnet on port %u", port);
    return true;
}

void osNetStopTelnet()
{
    if (g_telnetServer) { g_telnetServer->end(); delete g_telnetServer; g_telnetServer = nullptr; }
    if (g_telnetTask)   { vTaskDelete(g_telnetTask); g_telnetTask = nullptr; }
}