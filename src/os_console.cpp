#include "os_console.h"
#include "os_kernel.h"
#include <stdarg.h>
#include <string.h>

// Таблица консолей
static OsConsole g_consoles[OS_MAX_CONSOLE];
static SemaphoreHandle_t g_consoleMutex = nullptr;

// ---------- Инициализация ----------
bool osConsoleInit()
{
    g_consoleMutex = xSemaphoreCreateMutex();
    if (!g_consoleMutex) return false;

    for (auto& c : g_consoles) {
        c.consoleId = 0;
        c.outq      = nullptr;
        c.inq       = nullptr;
        c.used      = false;
    }

    OS_LOG("Console subsystem initialized");
    return true;
}

// ---------- Регистрация ----------
uint16_t osConsoleRegister(QueueHandle_t outq, QueueHandle_t inq)
{
    if (!outq) return 0;

    uint16_t id = 0;
    if (xSemaphoreTake(g_consoleMutex, portMAX_DELAY) != pdTRUE) return 0;

    // Ищем свободный слот
    for (auto& c : g_consoles) {
        if (!c.used) {
            c.outq = outq;
            c.inq  = inq;
            c.used = true;
            // consoleId = индекс + 1 (1-based, 0 = none)
            c.consoleId = (uint16_t)((&c - g_consoles) + 1);
            id = c.consoleId;
            break;
        }
    }

    xSemaphoreGive(g_consoleMutex);
    return id;
}

// ---------- Поиск ----------
OsConsole* osConsoleFind(uint16_t consoleId)
{
    if (consoleId == 0) return nullptr;
    OsConsole* found = nullptr;

    if (xSemaphoreTake(g_consoleMutex, portMAX_DELAY) != pdTRUE) return nullptr;

    for (auto& c : g_consoles) {
        if (c.used && c.consoleId == consoleId) { found = &c; break; }
    }

    xSemaphoreGive(g_consoleMutex);
    return found;
}

// ---------- Отправка ----------
bool osConsoleWrite(uint16_t consoleId, const char* msg)
{
    if (!msg || consoleId == 0) return false;

    OsConsole* c = osConsoleFind(consoleId);
    if (!c || !c->outq) return false;

    // Копируем в буфер, потому что очередь хранит копию
    char buf[OS_CONSOLE_MSG_MAX];
    strncpy(buf, msg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Кладём в очередь. Таймаут 100 мс — если переполнено, теряем.
    return xQueueSend(c->outq, buf, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool osConsolePrintf(uint16_t consoleId, const char* fmt, ...)
{
    char buf[OS_CONSOLE_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return osConsoleWrite(consoleId, buf);
}

bool osConsoleWriteCurrent(const char* msg)
{
    ProcessDescriptor* self = osProcessCurrent();
    if (!self) return false;
    return osConsoleWrite(self->consoleId, msg);
}

bool osConsolePrintfCurrent(const char* fmt, ...)
{
    ProcessDescriptor* self = osProcessCurrent();
    if (!self) return false;

    char buf[OS_CONSOLE_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return osConsoleWrite(self->consoleId, buf);
}

bool osConsoleUnregister(uint16_t consoleId)
{
    if (consoleId == 0) return false;
    bool ok = false;

    if (xSemaphoreTake(g_consoleMutex, portMAX_DELAY) != pdTRUE) return false;
    for (auto& c : g_consoles) {
        if (c.used && c.consoleId == consoleId) {
            c.used      = false;
            c.consoleId = 0;
            c.outq      = nullptr;
            c.inq       = nullptr;
            ok = true;
            break;
        }
    }
    xSemaphoreGive(g_consoleMutex);
    return ok;
}