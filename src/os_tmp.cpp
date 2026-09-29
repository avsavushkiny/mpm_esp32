#include "os_tmp.h"
#include "os_cli.h"
#include "os_kernel.h"
#include "os_user.h"
#include "driver/uart.h"

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
    CliContext ctx;
    ctx.consoleId = 1;
    ctx.write     = serialWrite;
    ctx.read      = serialRead;
    ctx.user      = nullptr;

    // Serial = доверенный канал, сразу root
    ProcessDescriptor* self = osProcessCurrent();
    if (self) self->userNumber = OS_UID_ROOT;

    serialWrite("\r\n" OS_VERSION "\r\n", nullptr);
    serialWrite("Logged in as root (serial console).\r\n", nullptr);
    serialWrite("Type 'help' for commands.\r\n", nullptr);
    osCliPrompt(&ctx);

    char line[OS_CLI_LINE_MAX];
    for (;;) {
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
struct TelnetCtx {
    WiFiClient client;
    uint16_t   consoleId;
};

static void telnetWrite(const char* s, void* user)
{
    TelnetCtx* t = static_cast<TelnetCtx*>(user);
    if (t && t->client.connected() && s) t->client.print(s);
}

static int telnetRead(char* buf, size_t max, void* user)
{
    TelnetCtx* t = static_cast<TelnetCtx*>(user);
    if (!t || !t->client.connected()) return 0;

    size_t n = 0;
    uint32_t lastByteTime = millis();

    while (n < max - 1) {
        if (!t->client.available()) {
            // Строка начата и пауза > 50 мс — считаем ввод завершённым
            if (n > 0 && (millis() - lastByteTime) > 50) break;
            // Ничего не пришло и ждём > 500 мс — выходим с пустой строкой
            if (n == 0 && (millis() - lastByteTime) > 500) break;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int c = t->client.read();
        if (c < 0) break;

        // --- IAC (Telnet control byte) ---
        if (c == 0xFF) {
            if (!t->client.available()) break;
            int cmd = t->client.read();

            if (cmd == 0xFA) {
                // Subnegotiation: читаем до IAC SE (0xFF 0xF0)
                while (t->client.available()) {
                    int x = t->client.read();
                    if (x == 0xFF) {
                        if (!t->client.available()) break;
                        int y = t->client.read();
                        if (y == 0xF0) break;
                    }
                }
            } else if (cmd >= 0xFB && cmd <= 0xFE) {
                // WILL / WONT / DO / DONT — за ними идёт байт опции
                if (t->client.available()) t->client.read();
            }
            // Остальные IAC-команды (IP, DM, NOP, ...) — просто пропускаем
            continue;
        }

        // --- Backspace ---
        if (c == 127 || c == 8) {
            if (n > 0) { n--; t->client.print("\b \b"); }
            continue;
        }

        // --- CR / LF — конец строки ---
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;
            break;
        }

        // --- Обычный символ ---
        buf[n++] = (char)c;
        t->client.print((char)c);  // эхо
        lastByteTime = millis();
    }

    buf[n] = '\0';
    return (int)n;
}

// Хелпер: прочитать строку с telnet-клиента, без эха (для пароля)
static int telnetReadNoEcho(TelnetCtx* t, char* buf, size_t max)
{
    size_t n = 0;
    while (n < max - 1) {
        // Если клиент отключился — выходим
        if (!t->client.connected()) break;

        if (!t->client.available()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;                 // <-- ЖДЁМ бесконечно, пока не придёт байт
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

        // Backspace (без эха)
        if (c == 127 || c == 8) {
            if (n > 0) n--;
            continue;
        }

        // CR / LF — конец строки
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;
            break;
        }

        buf[n++] = (char)c;
        // НЕТ t->client.print — это no-echo версия
    }

    buf[n] = '\0';
    return (int)n;
}

static void telnetTmpTask(void* arg)
{
    TelnetCtx* t = static_cast<TelnetCtx*>(arg);

    CliContext ctx;
    ctx.consoleId = t->consoleId;
    ctx.write     = telnetWrite;
    ctx.read      = telnetRead;
    ctx.user      = t;

    telnetWrite("\r\n" OS_VERSION "\r\n", t);

    // ---- Аутентификация ----
    char user[OS_USERNAME_MAX];
    char pass[OS_PASSWORD_MAX];
    uint32_t uid = OS_UID_NOBODY;

    for (int attempt = 0; attempt < 3; attempt++) {
        telnetWrite("login: ", t);
        if (telnetReadNoEcho(t, user, sizeof(user)) <= 0) {
            telnetWrite("\r\n", t);
            continue;
        }
        telnetWrite("\r\nPassword: ", t);
        if (telnetReadNoEcho(t, pass, sizeof(pass)) <= 0) {
            telnetWrite("\r\n", t);
            continue;
        }
        telnetWrite("\r\n", t);

        uid = osUserAuth(user, pass);
        if (uid != OS_UID_NOBODY) break;
        telnetWrite("Login incorrect.\r\n", t);
    }

    if (uid == OS_UID_NOBODY) {
        telnetWrite("Too many failed attempts. Goodbye.\r\n", t);
        t->client.stop();
        delete t;
        vTaskDelete(nullptr);
        return;
    }

    // ---- Устанавливаем uid процессу ----
    ProcessDescriptor* self = osProcessCurrent();
    if (self) self->userNumber = uid;

    telnetWrite("Welcome, ", t);
    telnetWrite(user, t);
    telnetWrite(".\r\nType 'help' for commands.\r\n", t);
    osCliPrompt(&ctx);

    char line[OS_CLI_LINE_MAX];
    while (t->client.connected()) {
        int n = telnetRead(line, sizeof(line), t);
        if (n > 0) {
            telnetWrite("\r\n", t);
            osCliExecute(&ctx, line);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    telnetWrite("\r\nGoodbye.\r\n", t);
    t->client.stop();
    delete t;
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
        name, telnetTmpTask, 16384, OS_PRIO_NORMAL, OS_CORE_NET, t);

    if (!p) {
        t->client.print("Server busy.\r\n");
        t->client.stop();
        delete t;
        return;
    }
    p->consoleId = t->consoleId;
    // userNumber будет установлен в telnetTmpTask после аутентификации
}