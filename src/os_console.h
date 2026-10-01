#pragma once

#include "os_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Максимальная длина сообщения в очереди
#define OS_CONSOLE_MSG_MAX    128

// Одна консоль
struct OsConsole {
    uint16_t        consoleId;
    QueueHandle_t   outq;         // исходящие сообщения (задача → терминал)
    QueueHandle_t   inq;          // входящие сообщения (терминал → задача)
    bool            used;
};

// Инициализация подсистемы консолей
bool osConsoleInit();

// Зарегистрировать консоль, получить consoleId
// Возвращает 0 при ошибке
uint16_t osConsoleRegister(QueueHandle_t outq, QueueHandle_t inq);

// Найти консоль по id
OsConsole* osConsoleFind(uint16_t consoleId);

// Отправить сообщение в консоль (задача → терминал)
// Возвращает true при успехе
bool osConsoleWrite(uint16_t consoleId, const char* msg);

// Отправить форматированное сообщение (удобно)
bool osConsolePrintf(uint16_t consoleId, const char* fmt, ...);

// Отправить в консоль, из которой вызвана задача
// (использует osProcessCurrent()->consoleId)
bool osConsoleWriteCurrent(const char* msg);
bool osConsolePrintfCurrent(const char* fmt, ...);
bool osConsoleUnregister(uint16_t consoleId);