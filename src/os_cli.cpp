#include "os_cli.h"
#include "os_kernel.h"
#include "os_time.h"
#include "os_fs.h"
#include "os_net.h"
#include "os_user.h"
#include "os_console.h"
#include "os_ota.h"
#include <stdarg.h>

static void cmdHelp(CliContext*, int, char**);
static void cmdPs(CliContext*, int, char**);
static void cmdKill(CliContext*, int, char**);
static void cmdAttach(CliContext*, int, char**);
static void cmdDetach(CliContext*, int, char**);
static void cmdAbort(CliContext*, int, char**);
static void cmdShow(CliContext*, int, char**);
static void cmdUser(CliContext*, int, char**);
static void cmdStat(CliContext*, int, char**);
static void cmdLs(CliContext*, int, char**);
static void cmdCat(CliContext*, int, char**);
static void cmdEcho(CliContext*, int, char**);
static void cmdWho(CliContext*, int, char**);
static void cmdLogin(CliContext*, int, char**);
static void cmdLogout(CliContext*, int, char**);
static void cmdPasswd(CliContext*, int, char**);
static void cmdSu(CliContext*, int, char**);
static void cmdUsers(CliContext*, int, char**);
static void cmdUseradd(CliContext*, int, char**);
static void cmdUserdel(CliContext*, int, char**);
static void cmdPrivs(CliContext*, int, char**);
static void cmdReboot(CliContext*, int, char**);
static void cmdSend(CliContext*, int, char**);
static void cmdOtaUrl(CliContext*, int, char**);
static void cmdOtaCancel(CliContext* ctx, int, char**);

static const ShellCommand g_commands[] = {
    {"help",     cmdHelp,     "Show this help"},
    {"ps",       cmdPs,       "List processes"},
    {"kill",     cmdKill,     "kill <pid>"},
    {"attach",   cmdAttach,   "attach <pid>"},
    {"detach",   cmdDetach,   "detach <pid>"},
    {"abort",    cmdAbort,    "abort <pid>"},
    {"show",     cmdShow,     "System status"},
    {"user",     cmdUser,     "user <n>     - switch user (deprecated)"},
    {"stat",     cmdStat,     "Statistics"},
    {"ls",       cmdLs,       "List files"},
    {"cat",      cmdCat,      "cat <file>"},
    {"echo",     cmdEcho,     "echo <text>"},
    {"who",      cmdWho,      "who          - list active sessions"},
    {"login",    cmdLogin,    "login <name> - re-authenticate"},
    {"logout",   cmdLogout,   "logout       - drop privileges"},
    {"passwd",   cmdPasswd,   "passwd       - change password"},
    {"su",       cmdSu,       "su <name>    - switch user"},
    {"users",    cmdUsers,    "users        - list users"},
    {"useradd",  cmdUseradd,  "useradd <name> <pass> <privs>"},
    {"userdel",  cmdUserdel,  "userdel <name>"},
    {"send",     cmdSend,     "send <pid|console> <text> - send message to task or console"},
    {"privs",    cmdPrivs,    "privs        - show privilege table (root only)"},
    {"otaurl",   cmdOtaUrl,   "otaurl <url> - flash firmware from URL"},
    {"otacancel",cmdOtaCancel, "otacancel         - cancel running OTA"},
    
    {"reboot",   cmdReboot,   "reboot [now] - restart the system"},
};

const ShellCommand* osCliCommands(size_t& count)
{
    count = sizeof(g_commands) / sizeof(g_commands[0]);
    return g_commands;
}

// ---------- Хелперы ----------
static void cliWrite(CliContext* ctx, const char* s)
{
    if (ctx && ctx->write && s) ctx->write(s, ctx->user);
}

static void cliPrintf(CliContext* ctx, const char* fmt, ...)
{
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cliWrite(ctx, buf);
}

bool osCliInit() { OS_LOG("CLI init"); return true; }

void osCliPrompt(CliContext* ctx) { cliWrite(ctx, "\r\nmpm> "); }

// ---------- Разбор ----------
#define MAX_ARGS 8
static int tokenize(char* line, char** argv, int maxArgs)
{
    int argc = 0;
    char* p = line;
    while (*p && argc < maxArgs) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

// ---------- Выполнение ----------
void osCliExecute(CliContext* ctx, const char* line)
{
    if (!line || !*line) { osCliPrompt(ctx); return; }

    char buf[OS_CLI_LINE_MAX];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* argv[MAX_ARGS];
    int argc = tokenize(buf, argv, MAX_ARGS);
    if (argc == 0) { osCliPrompt(ctx); return; }

    size_t n;
    const ShellCommand* cmds = osCliCommands(n);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(cmds[i].name, argv[0]) == 0) {
            cmds[i].handler(ctx, argc, argv);
            osCliPrompt(ctx);
            return;
        }
    }
    cliPrintf(ctx, "Unknown command: %s\r\n", argv[0]);
    osCliPrompt(ctx);
}

// ---------- Команды ----------
static void cmdHelp(CliContext* ctx, int, char**)
{
    size_t n;
    const ShellCommand* cmds = osCliCommands(n);
    cliWrite(ctx, "Commands:\r\n");
    for (size_t i = 0; i < n; i++)
        cliPrintf(ctx, "  %-8s - %s\r\n", cmds[i].name, cmds[i].help);
}

static void psCallback(const ProcessDescriptor& p, void* user)
{
    CliContext* ctx = static_cast<CliContext*>(user);
    const char* s = "?";
    switch (p.state) {
        case ProcState::RUNNING:    s = "RUN"; break;
        case ProcState::READY:      s = "RDY"; break;
        case ProcState::BLOCKED:    s = "BLK"; break;
        case ProcState::POLLED:     s = "POL"; break;
        case ProcState::TERMINATED: s = "TRM"; break;
        default: break;
    }
    cliPrintf(ctx, "  %4u  %-16s %s  core=%d prio=%3u  cons=%u\r\n",
              p.pid, p.name, s, (int)p.core, p.mpmPriority, p.consoleId);
}

static void cmdPs(CliContext* ctx, int, char**)
{
    cliWrite(ctx, "  PID   NAME             STATE  CORE  PRIO   CONSOLE\r\n");
    osProcessList(psCallback, ctx);
}

static void cmdKill(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) { cliWrite(ctx, "usage: kill <pid>\r\n"); return; }
    if (!osUserHasPriv(OS_PRIV_KILL_PROC)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    uint16_t pid = (uint16_t)atoi(argv[1]);
    cliPrintf(ctx, osProcessKill(pid) ? "killed %u\r\n" : "no such process\r\n", pid);
}

// static void cmdAttach(CliContext* ctx, int argc, char** argv)
// {
//     if (argc < 2) { cliWrite(ctx, "usage: attach <pid>\r\n"); return; }
//     if (!osUserHasPriv(OS_PRIV_ATTACH)) {
//         cliWrite(ctx, "Permission denied.\r\n");
//         return;
//     }
//     uint16_t pid = (uint16_t)atoi(argv[1]);
//     cliPrintf(ctx, osProcessAttach(pid, ctx->consoleId)
//                    ? "attached %u\r\n" : "no such process\r\n", pid);
// }

static void cmdAttach(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) {
        cliWrite(ctx, "usage: attach <pid> [console]\r\n");
        return;
    }
    if (!osUserHasPriv(OS_PRIV_ATTACH)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }

    uint16_t pid = (uint16_t)atoi(argv[1]);

    // Если console не указан — текущая консоль
    uint16_t consoleId = ctx->consoleId;
    if (argc >= 3) {
        consoleId = (uint16_t)atoi(argv[2]);
    }

    if (osProcessAttach(pid, consoleId)) {
        cliPrintf(ctx, "attached %u to console %u\r\n", pid, consoleId);
    } else {
        cliWrite(ctx, "no such process\r\n");
    }
}

static void cmdDetach(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) { cliWrite(ctx, "usage: detach <pid>\r\n"); return; }
    uint16_t pid = (uint16_t)atoi(argv[1]);
    cliPrintf(ctx, osProcessDetach(pid) ? "detached %u\r\n" : "no such process\r\n", pid);
}

static void cmdAbort(CliContext* ctx, int argc, char** argv) { cmdKill(ctx, argc, argv); }

static void cmdShow(CliContext* ctx, int, char**)
{
    ProcessDescriptor* cur = osProcessCurrent();
    const OsUser* u = cur ? osUserFindByUid(cur->userNumber) : nullptr;

    OsTime t = osTimeGet();
    cliPrintf(ctx, "%s\r\n", OS_VERSION);
    cliPrintf(ctx, "Build:      %s\r\n", OS_BUILD_DATE);
    cliPrintf(ctx, "User:       %s (uid=%lu)\r\n",
              u ? u->name : "?", (unsigned long)(cur ? cur->userNumber : 0));
    cliPrintf(ctx, "Uptime:     %lu ms (%lu s)\r\n",
              (unsigned long)t.ticks, (unsigned long)t.seconds);
    cliPrintf(ctx, "Free heap:  %lu bytes\r\n", (unsigned long)ESP.getFreeHeap());
    cliPrintf(ctx, "WiFi:       %s, IP=%s\r\n",
              osNetConnected() ? "connected" : "offline", osNetIp().c_str());
    cliPrintf(ctx, "Console:    %u\r\n", ctx->consoleId);
}

static void cmdUser(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) { cliWrite(ctx, "usage: user <n>\r\n"); return; }
    int n = atoi(argv[1]);
    if (n < 0 || n > 15) { cliWrite(ctx, "user 0..15\r\n"); return; }
    ProcessDescriptor* cur = osProcessCurrent();
    if (cur) cur->userNumber = (uint32_t)n;
    cliPrintf(ctx, "switched to user %d\r\n", n);
}

static void cmdStat(CliContext* ctx, int, char**)
{
    cliWrite(ctx, "System statistics:\r\n");
    cliPrintf(ctx, "  Free heap:       %lu bytes\r\n", (unsigned long)ESP.getFreeHeap());
    cliPrintf(ctx, "  Min free heap:   %lu bytes\r\n", (unsigned long)ESP.getMinFreeHeap());
    cliPrintf(ctx, "  Heap size:       %lu bytes\r\n", (unsigned long)ESP.getHeapSize());
    // cliPrintf(ctx, "  WiFi RSSI:       %d dBm\r\n", WiFi.RSSI());
    cliPrintf(ctx, "  FreeRTOS tasks:  %u\r\n", (unsigned)uxTaskGetNumberOfTasks());
}

static void fsListCb(const char* name, size_t size, void* user)
{
    CliContext* ctx = static_cast<CliContext*>(user);
    cliPrintf(ctx, "  %8u  %s\r\n", (unsigned)size, name);
}

static void cmdLs(CliContext* ctx, int, char**)
{
    cliWrite(ctx, "  SIZE      NAME\r\n");
    osFsList(fsListCb, ctx);
}

static void cmdCat(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) { cliWrite(ctx, "usage: cat <file>\r\n"); return; }
    String data;
    size_t n = osFsRead(argv[1], data);
    if (n == 0) { cliWrite(ctx, "file not found or empty\r\n"); return; }
    cliWrite(ctx, data.c_str());
    cliWrite(ctx, "\r\n");
}

static void cmdEcho(CliContext* ctx, int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        cliWrite(ctx, argv[i]);
        if (i < argc - 1) cliWrite(ctx, " ");
    }
    cliWrite(ctx, "\r\n");
}

// ---------- who ----------
static void whoCallback(const ProcessDescriptor& p, void* user)
{
    CliContext* ctx = static_cast<CliContext*>(user);
    if (p.consoleId == 0) return;

    const OsUser* u = osUserFindByUid(p.userNumber);
    const char* uname = u ? u->name : "?";

    cliPrintf(ctx, "  CONSOLE %u  USER %-12s  PID %u  %s\r\n",
              p.consoleId, uname, p.pid, p.name);
}

static void cmdWho(CliContext* ctx, int, char**)
{
    cliWrite(ctx, "  CONSOLE  USER          PID   NAME\r\n");
    osProcessList(whoCallback, ctx);
}

// ---------- login ----------
static void cmdLogin(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) { cliWrite(ctx, "usage: login <name>\r\n"); return; }

    cliWrite(ctx, "Password: ");
    char pass[OS_PASSWORD_MAX];
    int n = ctx->read(pass, sizeof(pass), ctx->user);
    if (n <= 0) { cliWrite(ctx, "\r\nCancelled.\r\n"); return; }

    uint32_t uid = osUserAuth(argv[1], pass);
    if (uid == OS_UID_NOBODY) {
        cliWrite(ctx, "\r\nLogin failed.\r\n");
        return;
    }

    ProcessDescriptor* cur = osProcessCurrent();
    if (cur) {
        cur->userNumber = uid;
        cliPrintf(ctx, "\r\nLogged in as %s (uid=%lu)\r\n",
                  argv[1], (unsigned long)uid);
    }
}

// ---------- logout ----------
static void cmdLogout(CliContext* ctx, int, char**)
{
    ProcessDescriptor* cur = osProcessCurrent();
    if (!cur) return;
    cur->userNumber = OS_UID_GUEST;
    cliWrite(ctx, "Logged out. Now guest.\r\n");
}

// ---------- passwd ----------
static void cmdPasswd(CliContext* ctx, int, char**)
{
    ProcessDescriptor* cur = osProcessCurrent();
    if (!cur) return;

    const OsUser* u = osUserFindByUid(cur->userNumber);
    if (!u) { cliWrite(ctx, "No user.\r\n"); return; }

    cliWrite(ctx, "Old password: ");
    char oldPass[OS_PASSWORD_MAX];
    if (ctx->read(oldPass, sizeof(oldPass), ctx->user) <= 0) return;

    cliWrite(ctx, "\r\nNew password: ");
    char newPass[OS_PASSWORD_MAX];
    if (ctx->read(newPass, sizeof(newPass), ctx->user) <= 0) return;

    cliWrite(ctx, "\r\nRepeat: ");
    char repPass[OS_PASSWORD_MAX];
    if (ctx->read(repPass, sizeof(repPass), ctx->user) <= 0) return;

    if (strcmp(newPass, repPass) != 0) {
        cliWrite(ctx, "\r\nPasswords don't match.\r\n");
        return;
    }

    if (osUserChangePassword(cur->userNumber, oldPass, newPass))
        cliWrite(ctx, "\r\nPassword changed.\r\n");
    else
        cliWrite(ctx, "\r\nFailed.\r\n");
}

// ---------- su ----------
static void cmdSu(CliContext* ctx, int argc, char** argv)
{
    if (!osUserHasPriv(OS_PRIV_USER_MGMT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    if (argc < 2) { cliWrite(ctx, "usage: su <name>\r\n"); return; }

    const OsUser* u = osUserFindByName(argv[1]);
    if (!u) { cliWrite(ctx, "No such user.\r\n"); return; }

    ProcessDescriptor* cur = osProcessCurrent();
    if (cur) {
        cur->userNumber = u->uid;
        cliPrintf(ctx, "Now %s\r\n", u->name);
    }
}

// ---------- users ----------
static void usersCallback(const OsUser& u, void* user)
{
    CliContext* ctx = static_cast<CliContext*>(user);
    cliPrintf(ctx, "  uid=%-4lu  %-12s  priv=0x%02X  %s\r\n",
              (unsigned long)u.uid, u.name, u.privileges,
              u.enabled ? "enabled" : "disabled");
}

static void cmdUsers(CliContext* ctx, int, char**)
{
    if (!osUserHasPriv(OS_PRIV_USER_MGMT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    cliWrite(ctx, "  UID    NAME          PRIVS      STATUS\r\n");
    osUserList(usersCallback, ctx);
}

// ---------- useradd ----------
static void cmdUseradd(CliContext* ctx, int argc, char** argv)
{
    if (!osUserHasPriv(OS_PRIV_USER_MGMT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    if (argc < 4) {
        cliWrite(ctx, "usage: useradd <name> <password> <privs_hex>\r\n");
        return;
    }
    uint8_t privs = (uint8_t)strtol(argv[3], nullptr, 16);
    if (osUserCreate(argv[1], argv[2], privs))
        cliPrintf(ctx, "User %s created.\r\n", argv[1]);
    else
        cliWrite(ctx, "Failed (duplicate or no slot).\r\n");
}

// ---------- userdel ----------
static void cmdUserdel(CliContext* ctx, int argc, char** argv)
{
    if (!osUserHasPriv(OS_PRIV_USER_MGMT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    if (argc < 2) { cliWrite(ctx, "usage: userdel <name>\r\n"); return; }
    if (osUserDelete(argv[1]))
        cliPrintf(ctx, "User %s deleted.\r\n", argv[1]);
    else
        cliWrite(ctx, "Failed (root cannot be deleted).\r\n");
}

// ---------- privs ----------
// Вариант с выводом через CliContext (работает и в Serial, и в telnet)
static void cmdPrivs(CliContext* ctx, int, char**)
{
    if (!osUserHasPriv(OS_PRIV_USER_MGMT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }

    // --- Текущий пользователь ---
    ProcessDescriptor* cur = osProcessCurrent();
    const OsUser* me = cur ? osUserFindByUid(cur->userNumber) : nullptr;
    if (me) {
        cliPrintf(ctx, "Current user: %s (uid=%lu, privs=0x%02X)\r\n\r\n",
                  me->name, (unsigned long)me->uid, me->privileges);
    }
    // --- Расшифровка прав текущего пользователя ---
    // if (me)
    // {
    //     cliPrintf(ctx, "Your privileges (0x%02X):\r\n", me->privileges);
    //     struct
    //     {
    //         uint8_t bit;
    //         const char *name;
    //     } map[] = {
    //         {OS_PRIV_READ_FS, "READ_FS"},
    //         {OS_PRIV_WRITE_FS, "WRITE_FS"},
    //         {OS_PRIV_KILL_PROC, "KILL_PROC"},
    //         {OS_PRIV_ATTACH, "ATTACH"},
    //         {OS_PRIV_USER_MGMT, "USER_MGMT"},
    //         {OS_PRIV_REBOOT, "REBOOT"},
    //     };
    //     for (auto &m : map)
    //     {
    //         cliPrintf(ctx, "  [%c] %s\r\n",
    //                   (me->privileges & m.bit) ? 'X' : ' ',
    //                   m.name);
    //     }
    //     cliWrite(ctx, "\r\n");
    // }

    cliWrite(ctx, "  BIT    HEX    CONSTANT              DESCRIPTION\r\n");
    cliWrite(ctx, "  -----  -----  --------------------  ------------------------------\r\n");
    cliWrite(ctx, "    0    0x01   OS_PRIV_READ_FS       read files (ls, cat)\r\n");
    cliWrite(ctx, "    1    0x02   OS_PRIV_WRITE_FS      write/delete files\r\n");
    cliWrite(ctx, "    2    0x04   OS_PRIV_KILL_PROC     kill/abort processes\r\n");
    cliWrite(ctx, "    3    0x08   OS_PRIV_ATTACH        attach/detach consoles\r\n");
    cliWrite(ctx, "    4    0x10   OS_PRIV_USER_MGMT     useradd/userdel/users/su\r\n");
    cliWrite(ctx, "    5    0x20   OS_PRIV_REBOOT        reboot system\r\n");
    cliWrite(ctx, "  -----  -----  --------------------  ------------------------------\r\n");
    cliWrite(ctx, "  All    0xFF   OS_PRIV_ALL           full access (root)\r\n");
    cliWrite(ctx, "\r\n");
    cliWrite(ctx, "  Common combinations:\r\n");
    cliWrite(ctx, "    0x00  - no rights (nobody)\r\n");
    cliWrite(ctx, "    0x01  - read-only (guest)\r\n");
    cliWrite(ctx, "    0x03  - read + write files\r\n");
    cliWrite(ctx, "    0x0F  - files + kill + attach (normal user)\r\n");
    cliWrite(ctx, "    0x1F  - everything except reboot\r\n");
    cliWrite(ctx, "    0xFF  - full access (root)\r\n");
}

// ---------- reboot ----------
static void cmdReboot(CliContext* ctx, int argc, char** argv)
{
    if (!osUserHasPriv(OS_PRIV_REBOOT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }

    // Опционально: `reboot now` — пропустить задержку
    bool immediate = (argc >= 2 && strcmp(argv[1], "now") == 0);

    if (immediate) {
        cliWrite(ctx, "System is going down for reboot NOW.\r\n");
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP.restart();
        return;
    }

    // Отложенная перезагрузка с уведомлением всех консолей
    cliWrite(ctx, "Broadcasting reboot notice to all sessions...\r\n");
    cliWrite(ctx, "System will reboot in 5 seconds.\r\n");

    osNotifyAllAndWait(
        "System is going down for reboot in 60 seconds.\r\n"
        "Save your work. Reconnect after the reboot.",
        60000    // ждём 60 секунд, чтобы все успели увидеть
    );

    ESP.restart();
}

static void cmdSend(CliContext* ctx, int argc, char** argv)
{
    if (argc < 3) {
        cliWrite(ctx, "usage: send <pid|console> <text>\r\n");
        cliWrite(ctx, "       pid >= 100, console 1..4\r\n");
        return;
    }

    int target = atoi(argv[1]);
    if (target <= 0) {
        cliWrite(ctx, "invalid target\r\n");
        return;
    }

    // Собираем остальные аргументы в одну строку
    char buf[OS_CONSOLE_MSG_MAX];
    int off = 0;
    for (int i = 2; i < argc && off < (int)sizeof(buf) - 2; i++) {
        int w = snprintf(buf + off, sizeof(buf) - off, "%s%s",
                         argv[i], (i < argc - 1) ? " " : "");
        if (w < 0) break;
        off += w;
    }

    // Решаем: это PID или consoleId?
    // PID начинаются с 100, consoleId 1..OS_MAX_CONSOLE
    if (target < 100) {
        // --- Отправка в консоль (очередь TMP) ---
        uint16_t consoleId = (uint16_t)target;

        if (osConsoleWrite(consoleId, buf)) {
            cliPrintf(ctx, "sent to console %u\r\n", consoleId);
        } else {
            cliPrintf(ctx, "console %u not found or queue full\r\n", consoleId);
        }
    } else {
        // --- Отправка задаче (по PID) ---
        uint16_t pid = (uint16_t)target;

        if (osProcessSendMessage(pid, buf)) {
            cliPrintf(ctx, "sent to pid %u\r\n", pid);
        } else {
            cliPrintf(ctx, "pid %u not found or queue full\r\n", pid);
        }
    }
}

static void cmdOtaUrl(CliContext* ctx, int argc, char** argv)
{
    if (!osUserHasPriv(OS_PRIV_REBOOT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    if (argc < 2) {
        cliWrite(ctx, "usage: otaurl <url>\r\n");
        return;
    }

    if (osOtaStartFromUrl(argv[1], ctx->consoleId)) {
        cliPrintf(ctx, "OTA started in background (url=%s)\r\n", argv[1]);
        cliWrite(ctx, "watch progress in this console...\r\n");
    } else {
        cliWrite(ctx, "failed to start OTA task\r\n");
    }
}

static void cmdOtaCancel(CliContext* ctx, int, char**)
{
    if (!osUserHasPriv(OS_PRIV_REBOOT)) {
        cliWrite(ctx, "Permission denied.\r\n");
        return;
    }
    if (!osOtaIsRunning()) {
        cliWrite(ctx, "OTA not running\r\n");
        return;
    }
    osOtaCancel();
    cliWrite(ctx, "OTA cancel requested\r\n");
}