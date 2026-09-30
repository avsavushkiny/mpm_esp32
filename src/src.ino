#include <Arduino.h>
#include "os_config.h"
#include "os_kernel.h"
#include "os_ipc.h"
#include "os_time.h"
#include "os_fs.h"
#include "os_net.h"
#include "os_cli.h"
#include "os_tmp.h"
#include "os_user.h"

// ---------- Демо-задачи ----------
static void heartbeatTask(void*)
{
    uint32_t n = 0;
    for (;;) {
        n++;
        OS_LOG("heartbeat #%lu (heap=%lu)",
               (unsigned long)n, (unsigned long)ESP.getFreeHeap());
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void flagWaiterTask(void*)
{
    ProcessDescriptor* self = osProcessCurrent();
    for (;;) {
        OS_LOG("waiter: waiting...");
        if (osFlagWait(self, OS_FLAG_USER1, 5000))
            OS_LOG("waiter: got flag");
        else
            OS_LOG("waiter: timeout");
    }
}

static void producerTask(void*)
{
    OsQueue* q = osQueueFind("demo");
    if (!q) { vTaskDelete(nullptr); return; }
    uint32_t n = 0;
    for (;;) {
        OsMessage m{};
        ProcessDescriptor* me = osProcessCurrent();
        m.senderPid = me ? me->pid : 0;
        m.targetPid = 0;
        m.type      = 1;
        m.len       = snprintf((char*)m.payload, OS_MSG_PAYLOAD_MAX, "msg #%lu", (unsigned long)n++);
        osMessageSend(q, m, 100);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void consumerTask(void*)
{
    OsQueue* q = osQueueFind("demo");
    if (!q) { vTaskDelete(nullptr); return; }
    for (;;) {
        OsMessage m;
        if (osMessageReceive(q, m, portMAX_DELAY)) {
            OS_LOG("consumer: from=%u: %s", m.senderPid, (const char*)m.payload);
        }
    }
}

// ---------- Setup ----------
void setup()
{
    Serial.begin(115200);
    delay(300);

    osPrintf("\r\n\r\n=== %s ===\r\n", OS_VERSION);
    osPrintf("Build: %s\r\n\r\n", OS_BUILD_DATE);

    if (!osKernelInit()) { osPrintf("kernel init failed\r\n"); return; }
    if (!osTimeInit())   { osPrintf("time init failed\r\n");   return; }
    if (!osIpcInit())    { osPrintf("ipc init failed\r\n");    return; }
    if (!osUserInit())   { osPrintf("user init failed\r\n");   return; }

    osFsInit();
    // osNetInit("RT-GPON-6089", "u7PxRkFQ");
    osNetInit("Allowed-IoT", "Mup80673");
    !!osNetStartTelnet(23);

    osCliInit();
    osTmpInitSerial();

    osProcessCreate("heartbeat", heartbeatTask, 3072, OS_PRIO_LOW,    OS_CORE_NET);
    osProcessCreate("waiter",    flagWaiterTask, 3072, OS_PRIO_NORMAL, OS_CORE_APP);

    osQueueCreate("demo", 8, sizeof(OsMessage));
    // osProcessCreate("producer",  producerTask, 3072, OS_PRIO_NORMAL, OS_CORE_APP);
    // osProcessCreate("consumer",  consumerTask, 3072, OS_PRIO_NORMAL, OS_CORE_APP);

    osPrintf("System ready. Tasks: %u\r\n", (unsigned)uxTaskGetNumberOfTasks());
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}