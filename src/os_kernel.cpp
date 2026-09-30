#include "os_kernel.h"
#include <stdarg.h>
#include <string.h>
#include "os_user.h"
#include "driver/uart.h"

// ---------- Статическая таблица ----------
static ProcessDescriptor g_procTable[OS_MAX_PROCESSES];
static SemaphoreHandle_t g_procMutex  = nullptr;
static uint16_t          g_nextPid    = 100;
static SemaphoreHandle_t g_printMutex = nullptr;
static void osNoticeInit();

// ---------- Безопасный вывод через UART0 ----------
void osPrintf(const char* fmt, ...)
{
    char buf[192];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        // Просто пишем в UART. ESP32 UART имеет свой аппаратный буфер.
        // Если переполнение — байты потеряются, но система не зависнет.
        uart_write_bytes(UART_NUM_0, buf, n > (int)sizeof(buf) - 1 ? sizeof(buf) - 1 : n);
    }
}

// ---------- Инициализация ----------
bool osKernelInit()
{
    g_procMutex  = xSemaphoreCreateMutex();
    // g_printMutex = xSemaphoreCreateMutex();
    if (!g_procMutex /*|| !g_printMutex*/) return false;

    osNoticeInit();

    for (auto& p : g_procTable) {
        p.pid   = 0;
        p.state = ProcState::FREE;
        p.flags = nullptr;
        p.task  = nullptr;
        p.name[0] = '\0';
    }

    OS_LOG("Kernel initialized");
    return true;
}

// ---------- Маппинг приоритетов 0..255 -> 1..8 ----------
UBaseType_t osMapPriority(uint8_t mpmPrio)
{
    UBaseType_t p = 1 + (mpmPrio >> 5);
    if (p > 8) p = 8;
    if (p < 1) p = 1;
    return p;
}

// ---------- Выделение слота ----------
static ProcessDescriptor* allocSlot()
{
    ProcessDescriptor* slot = nullptr;
    if (xSemaphoreTake(g_procMutex, portMAX_DELAY) != pdTRUE) return nullptr;

    for (auto& p : g_procTable) {
        if (p.pid == 0) { slot = &p; break; }
    }
    if (slot) slot->pid = g_nextPid++;

    xSemaphoreGive(g_procMutex);
    return slot;
}

// ---------- Обёртка задачи ----------
struct TaskStartCtx {
    ProcessDescriptor* proc;
    TaskFunction_t     userEntry;
    void*              userArg;
};

static void taskTrampoline(void* arg)
{
    TaskStartCtx* ctx = static_cast<TaskStartCtx*>(arg);
    ProcessDescriptor* proc  = ctx->proc;
    TaskFunction_t     entry = ctx->userEntry;
    void*              userArg = ctx->userArg;
    delete ctx;

    proc->startTime = millis();
    proc->state     = ProcState::RUNNING;

    OS_LOG("Process %u '%s' started on core %d",
           proc->pid, proc->name, (int)proc->core);

    entry(userArg);   // <-- если функция вернётся — завершаем процесс

    OS_LOG("Process %u '%s' finished", proc->pid, proc->name);

    // Аккуратно освобождаем слот
    if (xSemaphoreTake(g_procMutex, portMAX_DELAY) == pdTRUE) {
        if (proc->flags) { vEventGroupDelete(proc->flags); proc->flags = nullptr; }
        proc->pid   = 0;
        proc->task  = nullptr;
        proc->state = ProcState::FREE;
        xSemaphoreGive(g_procMutex);
    }

    vTaskDelete(nullptr);
}

// ---------- Создание ----------
ProcessDescriptor* osProcessCreate(
    const char*    name,
    TaskFunction_t entry,
    uint32_t       stackBytes,
    uint8_t        mpmPriority,
    BaseType_t     core,
    void*          arg)
{
    ProcessDescriptor* proc = allocSlot();
    if (!proc) { OS_LOG("No free slot"); return nullptr; }

    strncpy(proc->name, name, OS_MAX_NAME_LEN - 1);
    proc->name[OS_MAX_NAME_LEN - 1] = '\0';
    proc->core        = core;
    proc->mpmPriority = mpmPriority;
    proc->state       = ProcState::READY;
    proc->userNumber  = 1;
    proc->consoleId   = 0;
    proc->cpuTimeUs   = 0;
    proc->wakeTime    = 0;
    proc->flags       = xEventGroupCreate();
    if (!proc->flags) { proc->pid = 0; return nullptr; }

    TaskStartCtx* ctx = new TaskStartCtx{proc, entry, arg};
    if (!ctx) {
        vEventGroupDelete(proc->flags);
        proc->pid = 0;
        return nullptr;
    }

    // IDF: стек в байтах
    BaseType_t rc = xTaskCreatePinnedToCore(
        taskTrampoline,
        proc->name,
        stackBytes / 4,
        ctx,
        osMapPriority(mpmPriority),
        &proc->task,
        core
    );

    if (rc != pdPASS) {
        vEventGroupDelete(proc->flags);
        delete ctx;
        proc->pid = 0;
        OS_LOG("xTaskCreate failed for '%s'", name);
        return nullptr;
    }

    OS_LOG("Created %u '%s' prio=%u core=%d",
           proc->pid, proc->name, mpmPriority, (int)core);
    return proc;
}

// ---------- Поиск ----------
ProcessDescriptor* osProcessFind(uint16_t pid)
{
    ProcessDescriptor* found = nullptr;
    if (xSemaphoreTake(g_procMutex, portMAX_DELAY) != pdTRUE) return nullptr;
    for (auto& p : g_procTable) if (p.pid == pid) { found = &p; break; }
    xSemaphoreGive(g_procMutex);
    return found;
}

ProcessDescriptor* osProcessCurrent()
{
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    ProcessDescriptor* found = nullptr;
    if (xSemaphoreTake(g_procMutex, portMAX_DELAY) != pdTRUE) return nullptr;
    for (auto& p : g_procTable) if (p.task == me) { found = &p; break; }
    xSemaphoreGive(g_procMutex);
    return found;
}

// ---------- Завершение (с защитой от гонки) ----------
bool osProcessKill(uint16_t pid)
{
    if (!osUserHasPriv(OS_PRIV_KILL_PROC)) {
        OS_LOG("kill denied: no privilege");
        return false;
    }

    // Получаем handle текущей задачи БЕЗ мьютекса
    TaskHandle_t me = xTaskGetCurrentTaskHandle();

    if (xSemaphoreTake(g_procMutex, portMAX_DELAY) != pdTRUE) return false;

    ProcessDescriptor* proc = nullptr;
    for (auto& p : g_procTable) if (p.pid == pid) { proc = &p; break; }

    if (!proc || !proc->task || proc->state == ProcState::TERMINATED) {
        xSemaphoreGive(g_procMutex);
        return false;
    }

    // Запрет самоубийства — сравниваем handle, а не вызываем osProcessCurrent
    if (proc->task == me) {
        xSemaphoreGive(g_procMutex);
        return false;
    }

    TaskHandle_t t = proc->task;
    proc->state = ProcState::TERMINATED;
    proc->task  = nullptr;
    proc->pid   = 0;
    if (proc->flags) { vEventGroupDelete(proc->flags); proc->flags = nullptr; }

    xSemaphoreGive(g_procMutex);

    vTaskDelete(t);
    return true;
}

// ---------- ATTACH / DETACH ----------
bool osProcessAttach(uint16_t pid, uint16_t consoleId)
{
    ProcessDescriptor* proc = osProcessFind(pid);
    if (!proc) return false;
    proc->consoleId = consoleId;
    return true;
}

bool osProcessDetach(uint16_t pid)
{
    ProcessDescriptor* proc = osProcessFind(pid);
    if (!proc) return false;
    proc->consoleId = 0;
    return true;
}

void osProcessSetState(ProcessDescriptor* proc, ProcState s)
{
    if (proc) proc->state = s;
}

// ---------- Список: снимок под мьютексом, callback — без ----------
void osProcessList(void (*cb)(const ProcessDescriptor&, void*), void* user)
{
    if (!cb) return;

    static ProcessDescriptor snapshot[OS_MAX_PROCESSES]; // non-static?
    int count = 0;

    if (xSemaphoreTake(g_procMutex, portMAX_DELAY) == pdTRUE) {
        for (auto& p : g_procTable) {
            if (p.pid != 0 && count < OS_MAX_PROCESSES) {
                snapshot[count++] = p;
            }
        }
        xSemaphoreGive(g_procMutex);
    }

    for (int i = 0; i < count; i++) cb(snapshot[i], user);
}

// ---------- Флаги ----------
bool osFlagWait(ProcessDescriptor* proc, EventBits_t bits, uint32_t timeoutMs)
{
    if (!proc || !proc->flags) return false;

    ProcState prev;
    noInterrupts();
    prev = proc->state;
    proc->state = ProcState::BLOCKED;
    interrupts();

    EventBits_t got = xEventGroupWaitBits(
        proc->flags, bits, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeoutMs));

    noInterrupts();
    proc->state = prev;
    interrupts();

    return (got & bits) != 0;
}

void osFlagSet(ProcessDescriptor* proc, EventBits_t bits)
{
    if (proc && proc->flags) xEventGroupSetBits(proc->flags, bits);
}

void osFlagSetFromISR(ProcessDescriptor* proc, EventBits_t bits)
{
    if (!proc || !proc->flags) return;
    BaseType_t hpw = pdFALSE;
    xEventGroupSetBitsFromISR(proc->flags, bits, &hpw);
    portYIELD_FROM_ISR(hpw);
}

// ---------- Системные уведомления (с эпохами) ----------
static SemaphoreHandle_t g_noticeMutex = nullptr;
static volatile uint32_t g_noticeEpoch = 0;    // растёт при каждом уведомлении
static char              g_noticeText[OS_NOTICE_MAX_LEN] = {0};

static void osNoticeInit()
{
    if (!g_noticeMutex) g_noticeMutex = xSemaphoreCreateMutex();
}

void osNotifyAll(const char* message)
{
    if (!message) return;
    if (!g_noticeMutex) osNoticeInit();

    if (xSemaphoreTake(g_noticeMutex, portMAX_DELAY) == pdTRUE) {
        strncpy(g_noticeText, message, OS_NOTICE_MAX_LEN - 1);
        g_noticeText[OS_NOTICE_MAX_LEN - 1] = '\0';
        g_noticeEpoch++;                  // <-- новая эпоха
        xSemaphoreGive(g_noticeMutex);
    }
}

void osNotifyAllAndWait(const char* message, uint32_t waitMs)
{
    osNotifyAll(message);
    vTaskDelay(pdMS_TO_TICKS(waitMs));
}

// Возвращает true, если для этой задачи есть новое уведомление.
// localEpoch — указатель на локальную переменную задачи.
bool osNotifyPoll(uint32_t* localEpoch, char* out, size_t maxLen)
{
    if (!localEpoch || !out) return false;

    uint32_t current = g_noticeEpoch;
    if (current == *localEpoch) return false;   // ничего нового

    if (g_noticeMutex && xSemaphoreTake(g_noticeMutex, portMAX_DELAY) == pdTRUE) {
        strncpy(out, g_noticeText, maxLen - 1);
        out[maxLen - 1] = '\0';
        *localEpoch = current;
        xSemaphoreGive(g_noticeMutex);
        return true;
    }
    return false;
}