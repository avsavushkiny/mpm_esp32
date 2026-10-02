#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

// ---------- Лимиты ----------
#define OS_MAX_PROCESSES       16
#define OS_MAX_NAME_LEN        16
#define OS_MAX_MSG_QUEUES      8
#define OS_MAX_TMP_SESSIONS    4
#define OS_CLI_LINE_MAX        128
#define OS_MSG_PAYLOAD_MAX     64

#define OS_MAX_CONSOLE        4
#define OS_CONSOLE_MSG_MAX    128

// ---------- Приоритеты (MP/M-стиль: 0..255) ----------
#define OS_PRIO_IDLE           0
#define OS_PRIO_LOW            64
#define OS_PRIO_NORMAL         128
#define OS_PRIO_HIGH           192
#define OS_PRIO_CRITICAL       224

// ---------- Ядра ----------
#define OS_CORE_NET            0
#define OS_CORE_APP            1

// ---------- OTA ----------
#define OS_OTA_URL_MAX        256
#define OS_OTA_PROGRESS_MS    500

// ---------- Состояния процесса ----------
enum class ProcState : uint8_t {
    FREE = 0,
    RUNNING,
    READY,
    BLOCKED,
    POLLED,
    TERMINATED
};

// ---------- Системные флаги ----------
#define OS_FLAG_INPUT_READY    (1 << 0)
#define OS_FLAG_OUTPUT_READY   (1 << 1)
#define OS_FLAG_TIMER_TICK     (1 << 2)
#define OS_FLAG_NET_RX         (1 << 3)
#define OS_FLAG_USER1          (1 << 8)
#define OS_FLAG_USER2          (1 << 9)

// ---------- Версия ----------
#define OS_VERSION             "MP/M-32 v1.5"
#define OS_BUILD_DATE          __DATE__ " " __TIME__

// ---------- Отладка ----------
//#define OS_DEBUG
#ifdef OS_DEBUG
  #define OS_LOG(fmt, ...) osPrintf("[OS] " fmt "\n", ##__VA_ARGS__)
#else
  #define OS_LOG(fmt, ...) do {} while (0)
#endif

// ---------- Права пользователя (битовая маска) ----------
#define OS_PRIV_NONE        0x00   // ничего нельзя
#define OS_PRIV_READ_FS     0x01   // читать файлы
#define OS_PRIV_WRITE_FS    0x02   // писать файлы
#define OS_PRIV_KILL_PROC   0x04   // kill/abort других процессов
#define OS_PRIV_ATTACH      0x08   // attach/detach
#define OS_PRIV_USER_MGMT   0x10   // user/passwd/su
#define OS_PRIV_REBOOT      0x20   // перезагрузка
#define OS_PRIV_ALL         0xFF   // всё

// ---------- Лимиты пользователей ----------
#define OS_MAX_USERS        8
#define OS_USERNAME_MAX     16
#define OS_PASSWORD_MAX     32
#define OS_PASSWORD_HASH    32      // размер hex-хэша SHA-256

// ---------- Стандартные UID ----------
#define OS_UID_ROOT         0
#define OS_UID_GUEST        1
#define OS_UID_NOBODY       255     // никому не принадлежит

// ---------- Системные уведомления ----------
#define OS_NOTICE_MAX_LEN    192
#define OS_NOTICE_FLAG_SET   0x01

// Единая точка безопасного вывода (реализована в os_kernel.cpp)
void osPrintf(const char* fmt, ...);