#pragma once

#include "os_config.h"

struct ProcessDescriptor {
    uint16_t            pid;
    char                name[OS_MAX_NAME_LEN];
    TaskHandle_t        task;
    BaseType_t          core;
    uint8_t             mpmPriority;
    ProcState           state;
    uint32_t            userNumber;
    uint16_t            consoleId;
    EventGroupHandle_t  flags;
    uint32_t            startTime;
    uint32_t            cpuTimeUs;
    uint32_t            wakeTime;
    QueueHandle_t inq;
};

bool osKernelInit();

ProcessDescriptor* osProcessCreate(
    const char*    name,
    TaskFunction_t entry,
    uint32_t       stackBytes,
    uint8_t        mpmPriority,
    BaseType_t     core,
    void*          arg = nullptr
);

bool osProcessKill(uint16_t pid);
ProcessDescriptor* osProcessFind(uint16_t pid);
ProcessDescriptor* osProcessCurrent();
bool osProcessAttach(uint16_t pid, uint16_t consoleId);
bool osProcessDetach(uint16_t pid);
void osProcessSetState(ProcessDescriptor* proc, ProcState s);

// Callback вызывается БЕЗ внутренних мьютексов, с копией дескриптора
void osProcessList(void (*cb)(const ProcessDescriptor&, void*), void* user);

bool osFlagWait(ProcessDescriptor* proc, EventBits_t bits, uint32_t timeoutMs);
void osFlagSet(ProcessDescriptor* proc, EventBits_t bits);
void osFlagSetFromISR(ProcessDescriptor* proc, EventBits_t bits);

UBaseType_t osMapPriority(uint8_t mpmPrio);

// Уведомления пользователей
void osNotifyAll(const char* message);        // разослать всем консолям
void osNotifyAllAndWait(const char* message, uint32_t waitMs);  // и подождать
void osNotifyAll(const char* message);
void osNotifyAllAndWait(const char* message, uint32_t waitMs);
bool osNotifyPoll(uint32_t* localEpoch, char* out, size_t maxLen);

// Отправить сообщение задаче по PID
// Возвращает true при успехе
bool osProcessSendMessage(uint16_t pid, const char* msg);