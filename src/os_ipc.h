#pragma once
#include "os_config.h"

struct OsMessage {
    uint16_t senderPid;
    uint16_t targetPid;
    uint32_t type;
    uint32_t len;
    uint8_t  payload[OS_MSG_PAYLOAD_MAX];
};

struct OsQueue {
    QueueHandle_t handle;
    char          name[OS_MAX_NAME_LEN];
    bool          used;
};

bool     osIpcInit();
OsQueue* osQueueCreate(const char* name, size_t depth, size_t itemSize);
OsQueue* osQueueFind(const char* name);
bool     osMessageSend(OsQueue* q, const OsMessage& msg, uint32_t timeoutMs);
bool     osMessageReceive(OsQueue* q, OsMessage& msg, uint32_t timeoutMs);