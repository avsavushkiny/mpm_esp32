#include "os_time.h"
#include "esp_timer.h"

static OsTime g_time = {0, 0};
static esp_timer_handle_t g_tickTimer = nullptr;
static SemaphoreHandle_t  g_timeMutex = nullptr;

static void tickCallback(void* arg)
{
    g_time.ticks++;
    if (g_time.ticks % 1000 == 0) g_time.seconds++;
}

bool osTimeInit()
{
    g_timeMutex = xSemaphoreCreateMutex();
    if (!g_timeMutex) return false;

    esp_timer_create_args_t args = {
        .callback = &tickCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "os_tick"
    };
    if (esp_timer_create(&args, &g_tickTimer) != ESP_OK) return false;
    if (esp_timer_start_periodic(g_tickTimer, 1000) != ESP_OK) return false;

    OS_LOG("Time init");
    return true;
}

OsTime osTimeGet()
{
    OsTime t;
    if (xSemaphoreTake(g_timeMutex, portMAX_DELAY) == pdTRUE) {
        t = g_time;
        xSemaphoreGive(g_timeMutex);
    } else {
        t = g_time;
    }
    return t;
}

uint32_t osTimeMillis() { return millis(); }