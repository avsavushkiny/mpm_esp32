#include "os_ipc.h"

static OsQueue g_queues[OS_MAX_MSG_QUEUES];
static SemaphoreHandle_t g_ipcMutex = nullptr;

bool osIpcInit()
{
    g_ipcMutex = xSemaphoreCreateMutex();
    if (!g_ipcMutex) return false;
    for (auto& q : g_queues) { q.used = false; q.handle = nullptr; q.name[0] = '\0'; }
    return true;
}

OsQueue* osQueueCreate(const char* name, size_t depth, size_t itemSize)
{
    if (xSemaphoreTake(g_ipcMutex, portMAX_DELAY) != pdTRUE) return nullptr;

    for (auto& q : g_queues)
        if (q.used && strcmp(q.name, name) == 0) { xSemaphoreGive(g_ipcMutex); return nullptr; }

    OsQueue* slot = nullptr;
    for (auto& q : g_queues) if (!q.used) { slot = &q; break; }

    if (!slot) { xSemaphoreGive(g_ipcMutex); return nullptr; }

    slot->handle = xQueueCreate(depth, itemSize);
    if (!slot->handle) { xSemaphoreGive(g_ipcMutex); return nullptr; }

    strncpy(slot->name, name, OS_MAX_NAME_LEN - 1);
    slot->name[OS_MAX_NAME_LEN - 1] = '\0';
    slot->used = true;

    xSemaphoreGive(g_ipcMutex);
    OS_LOG("Queue '%s' created", name);
    return slot;
}

OsQueue* osQueueFind(const char* name)
{
    OsQueue* found = nullptr;
    if (xSemaphoreTake(g_ipcMutex, portMAX_DELAY) != pdTRUE) return nullptr;
    for (auto& q : g_queues)
        if (q.used && strcmp(q.name, name) == 0) { found = &q; break; }
    xSemaphoreGive(g_ipcMutex);
    return found;
}

bool osMessageSend(OsQueue* q, const OsMessage& msg, uint32_t timeoutMs)
{
    if (!q || !q->handle) return false;
    return xQueueSend(q->handle, &msg, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool osMessageReceive(OsQueue* q, OsMessage& msg, uint32_t timeoutMs)
{
    if (!q || !q->handle) return false;
    return xQueueReceive(q->handle, &msg, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}