#include "os_tmp.h"
#include "os_cli.h"
#include "os_kernel.h"
#include "os_user.h"
#include "driver/uart.h"
#include "os_console.h"

// ---------- Serial TMP ----------
static void serialWrite(const char* s, void*)
{
    if (!s) return;
    size_t len = strlen(s);
    if (len) uart_write_bytes(UART_NUM_0, s, len);
}

static int serialRead(char* buf, size_t max, void*)
{
    size_t n = 0;
    uint32_t start = millis();
    while (n < max - 1) {
        if (uart_read_bytes(UART_NUM_0, (uint8_t*)&buf[n], 1, 0) == 1) {
            char c = buf[n];
            if (c == '\r' || c == '\n') {
                if (n == 0) continue;
                break;
            }
            if (c == 127 || c == 8) {
                if (n > 0) { n--; uart_write_bytes(UART_NUM_0, "\b \b", 3); }
                continue;
            }
            uart_write_bytes(UART_NUM_0, &c, 1);
            n++;
            start = millis();
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
            if (millis() - start > 100) break;
        }
    }
    buf[n] = '\0';
    return (int)n;
}

static void serialTmpTask(void*)
{
    // 1) Создаём очередь для исходящих сообщений
    QueueHandle_t outq = xQueueCreate(8, OS_CONSOLE_MSG_MAX);

    // 2) Регистрируем консоль
    uint16_t consoleId = osConsoleRegister(outq, nullptr);
    // consoleId будет 1 для первой консоли

    // 3) Устанавливаем в свой дескриптор
    ProcessDescriptor* self = osProcessCurrent();
    if (self) {
        self->consoleId  = consoleId;
        self->userNumber = OS_UID_ROOT;
    }

    CliContext ctx;
    ctx.consoleId = consoleId;
    ctx.write     = serialWrite;
    ctx.read      = serialRead;
    ctx.user      = nullptr;

    serialWrite("\r\n" OS_VERSION "\r\n", nullptr);
    serialWrite("Logged in as root (serial console).\r\n", nullptr);
    serialWrite("Type 'help' for commands.\r\n", nullptr);
    osCliPrompt(&ctx);

    char line[OS_CLI_LINE_MAX];
    char outmsg[OS_CONSOLE_MSG_MAX];

    char notice[OS_NOTICE_MAX_LEN];
    uint32_t noticeEpoch = 0;

    for (;;) {
        // ---- Проверяем очередь исходящих сообщений ----
        while (xQueueReceive(outq, outmsg, 0) == pdTRUE) {
            serialWrite("\r\n[out] ", nullptr);
            serialWrite(outmsg, nullptr);
            serialWrite("\r\n", nullptr);
            osCliPrompt(&ctx);   // восстановить приглашение
        }

        // Проверяем новые уведомления
        if (osNotifyPoll(&noticeEpoch, notice, sizeof(notice))) {
            serialWrite("\r\n\r\n*** SYSTEM NOTICE ***\r\n", nullptr);
            serialWrite(notice, nullptr);
            serialWrite("\r\n\r\n", nullptr);
            osCliPrompt(&ctx);   // восстановить приглашение после прерывания
        }

        // ---- Читаем ввод ----
        int n = serialRead(line, sizeof(line), nullptr);
        if (n > 0) {
            serialWrite("\r\n", nullptr);
            osCliExecute(&ctx, line);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool osTmpInitSerial()
{
    ProcessDescriptor* p = osProcessCreate(
        "tmp-serial", serialTmpTask, 12288, OS_PRIO_NORMAL, OS_CORE_APP);
    if (p) {
        p->consoleId  = 1;
        p->userNumber = OS_UID_ROOT;   // root по умолчанию
    }
    return p != nullptr;
}



// ---------- Telnet TMP ----------
// Контекст одной telnet-сессии
struct TelnetCtx {
    WiFiClient client;
    uint16_t   consoleId;   // номер консоли (2, 3, 4, ...)
};

// ---------- Запись в сокет ----------
static void telnetWrite(const char* s, void* user)
{
    TelnetCtx* t = static_cast<TelnetCtx*>(user);
    if (t && t->client.connected() && s) {
        t->client.print(s);
    }
}

// ---------- Чтение строки с эхом (для CLI) ----------
static int telnetRead(char* buf, size_t max, void* user)
{
    TelnetCtx* t = static_cast<TelnetCtx*>(user);
    if (!t || !t->client.connected()) return 0;

    size_t n = 0;
    uint32_t lastByteTime = millis();

    while (n < max - 1) {
        if (!t->client.available()) {
            // Если строка уже начата и пауза > 50 мс — считаем ввод завершённым
            if (n > 0 && (millis() - lastByteTime) > 50) break;
            // Если ничего не пришло и ждём > 500 мс — выходим с пустой строкой
            if (n == 0 && (millis() - lastByteTime) > 500) break;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int c = t->client.read();
        if (c < 0) break;

        // --- IAC (Telnet control) ---
        if (c == 0xFF) {
            if (!t->client.available()) break;
            int cmd = t->client.read();
            if (cmd == 0xFA) {
                // Subnegotiation — читаем до IAC SE
                while (t->client.available()) {
                    int x = t->client.read();
                    if (x == 0xFF) {
                        if (!t->client.available()) break;
                        int y = t->client.read();
                        if (y == 0xF0) break;
                    }
                }
            } else if (cmd >= 0xFB && cmd <= 0xFE) {
                if (t->client.available()) t->client.read();
            }
            continue;
        }

        // --- Backspace ---
        if (c == 127 || c == 8) {
            if (n > 0) {
                n--;
                t->client.print("\b \b");
            }
            continue;
        }

        // --- CR / LF — конец строки ---
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;
            break;
        }

        // --- Обычный символ ---
        buf[n++] = (char)c;
        t->client.print((char)c);   // эхо
        lastByteTime = millis();
    }

    buf[n] = '\0';
    return (int)n;
}

// ---------- Чтение строки без эха (для пароля) ----------
// Возвращает: длину строки, -1 при разрыве, -2 при таймауте
static int telnetReadNoEcho(TelnetCtx* t, char* buf, size_t max, uint32_t timeoutMs)
{
    size_t n = 0;
    uint32_t start = millis();

    while (n < max - 1) {
        if (!t->client.connected()) return -1;
        if (millis() - start > timeoutMs) return -2;

        if (!t->client.available()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int c = t->client.read();
        if (c < 0) break;

        // IAC
        if (c == 0xFF) {
            if (t->client.available()) {
                int cmd = t->client.read();
                if (cmd >= 0xFB && cmd <= 0xFE && t->client.available())
                    t->client.read();
            }
            continue;
        }

        if (c == 127 || c == 8) { if (n > 0) n--; continue; }
        if (c == '\r' || c == '\n') { if (n == 0) continue; break; }

        buf[n++] = (char)c;
        start = millis();   // сбрасываем таймаут на каждый принятый байт
    }

    buf[n] = '\0';
    return (int)n;
}

static void telnetTmpTask(void* arg)
{
    TelnetCtx* t = static_cast<TelnetCtx*>(arg);
    if (!t) {
        vTaskDelete(nullptr);
        return;
    }

    const OsUser* u = nullptr;
    uint32_t noticeEpoch = 0;

    // ---------- 1. Создаём очередь исходящих сообщений ----------
    QueueHandle_t outq = xQueueCreate(8, OS_CONSOLE_MSG_MAX);
    if (!outq) {
        telnetWrite("\r\nServer error: no queue.\r\n", t);
        t->client.stop();
        delete t;
        vTaskDelete(nullptr);
        return;
    }

    // ---------- 2. Регистрируем консоль ----------
    uint16_t consoleId = osConsoleRegister(outq, nullptr);
    if (consoleId == 0) {
        telnetWrite("\r\nServer error: no console slot.\r\n", t);
        vQueueDelete(outq);
        t->client.stop();
        delete t;
        vTaskDelete(nullptr);
        return;
    }
    t->consoleId = consoleId;

    // ---------- 3. Приветствие ----------
    telnetWrite("\r\n" OS_VERSION "\r\n", t);

    // ---------- 4. Аутентификация (3 попытки) ----------
    char user[OS_USERNAME_MAX] = {0};
    char pass[OS_PASSWORD_MAX] = {0};
    uint32_t uid = OS_UID_NOBODY;

    for (int attempt = 0; attempt < 3; attempt++) {
        telnetWrite("login: ", t);
        int n = telnetReadNoEcho(t, user, sizeof(user), 60000);  // 60 сек
        if (n == -1) { telnetWrite("\r\nDisconnected.\r\n", t); goto cleanup; }
        if (n == -2) { telnetWrite("\r\nTimeout.\r\n", t);      goto cleanup; }
        if (n == 0)  { telnetWrite("\r\n", t); continue; }

        telnetWrite("Password: ", t);
        n = telnetReadNoEcho(t, pass, sizeof(pass), 60000);
        if (n == -1) { telnetWrite("\r\nDisconnected.\r\n", t); goto cleanup; }
        if (n == -2) { telnetWrite("\r\nTimeout.\r\n", t);      goto cleanup; }
        telnetWrite("\r\n", t);

        uid = osUserAuth(user, pass);
        if (uid != OS_UID_NOBODY) break;
        telnetWrite("Login incorrect.\r\n", t);
    }

    if (uid == OS_UID_NOBODY) {
        telnetWrite("Too many failed attempts. Goodbye.\r\n", t);
        goto cleanup;
    }

    // ---------- 5. Устанавливаем uid и consoleId в дескриптор ----------
    {
        ProcessDescriptor* self = osProcessCurrent();
        if (self) {
            self->userNumber = uid;
            self->consoleId  = consoleId;
        }
    }

    // ---------- 6. Приветствие после логина ----------
    // const OsUser* u = osUserFindByUid(uid);
    telnetWrite("Welcome, ", t);
    telnetWrite(u ? u->name : "user", t);
    telnetWrite(".\r\nType 'help' for commands.\r\n", t);

    // ---------- 7. Основной цикл ----------
    CliContext ctx;
    ctx.consoleId = consoleId;
    ctx.write     = telnetWrite;
    ctx.read      = telnetRead;
    ctx.user      = t;

    osCliPrompt(&ctx);

    char line[OS_CLI_LINE_MAX];
    char outmsg[OS_CONSOLE_MSG_MAX];
    // uint32_t noticeEpoch = 0;

    while (t->client.connected()) {
        // --- 7.1. Проверяем очередь исходящих сообщений ---
        while (xQueueReceive(outq, outmsg, 0) == pdTRUE) {
            telnetWrite("\r\n[out] ", t);
            telnetWrite(outmsg, t);
            telnetWrite("\r\n", t);
            osCliPrompt(&ctx);
        }

        // --- 7.2. Проверяем системные уведомления ---
        // (если реализован osNotifyPoll)
        // if (osNotifyPoll(&noticeEpoch, outmsg, sizeof(outmsg))) {
        //     telnetWrite("\r\n\r\n*** SYSTEM NOTICE ***\r\n", t);
        //     telnetWrite(outmsg, t);
        //     telnetWrite("\r\n\r\n", t);
        //     osCliPrompt(&ctx);
        // }

        // --- 7.3. Читаем ввод пользователя ---
        int n = telnetRead(line, sizeof(line), t);
        if (n > 0) {
            telnetWrite("\r\n", t);
            osCliExecute(&ctx, line);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

cleanup:
    // ---------- 8. Освобождаем консоль ----------
    // Помечаем консоль как свободную
    {
        OsConsole* c = osConsoleFind(consoleId);
        if (c) {
            // Отменяем регистрацию (нужна функция osConsoleUnregister,
            // либо делаем это здесь вручную — см. ниже)
        }
    }

    // Освобождаем консоль
    osConsoleUnregister(consoleId);

    // Удаляем очередь
    if (outq) vQueueDelete(outq);

    // Прощаемся и закрываем сокет
    telnetWrite("\r\nGoodbye.\r\n", t);
    t->client.stop();

    // Освобождаем контекст
    delete t;

    // Завершаем задачу
    vTaskDelete(nullptr);
}

// Счётчик консолей для telnet-сессий
static uint16_t g_nextConsole = 2;

void osTmpHandleTelnetClient(WiFiClient& client)
{
    TelnetCtx* t = new TelnetCtx{client, g_nextConsole++};
    if (g_nextConsole > 100) g_nextConsole = 2;

    char name[OS_MAX_NAME_LEN];
    snprintf(name, sizeof(name), "tmp-tel%u", t->consoleId);

    ProcessDescriptor* p = osProcessCreate(
        name,
        telnetTmpTask,
        16384,                  // стек 16 КБ (для telnet-сессий нужен запас)
        OS_PRIO_NORMAL,
        OS_CORE_NET,
        t
    );

    if (!p) {
        t->client.print("Server busy.\r\n");
        t->client.stop();
        delete t;
        return;
    }

    // consoleId установится внутри telnetTmpTask через osConsoleRegister
}