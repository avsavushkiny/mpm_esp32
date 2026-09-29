#include "os_cli.h"
#include "os_kernel.h"
#include "os_time.h"
#include "os_fs.h"
#include "os_net.h"
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

static const ShellCommand g_commands[] = {
    {"help",   cmdHelp,   "Show this help"},
    {"ps",     cmdPs,     "List processes"},
    {"kill",   cmdKill,   "kill <pid>"},
    {"attach", cmdAttach, "attach <pid>"},
    {"detach", cmdDetach, "detach <pid>"},
    {"abort",  cmdAbort,  "abort <pid>"},
    {"show",   cmdShow,   "System status"},
    {"user",   cmdUser,   "user <n>"},
    {"stat",   cmdStat,   "Statistics"},
    {"ls",     cmdLs,     "List files"},
    {"cat",    cmdCat,    "cat <file>"},
    {"echo",   cmdEcho,   "echo <text>"},
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
    uint16_t pid = (uint16_t)atoi(argv[1]);
    cliPrintf(ctx, osProcessKill(pid) ? "killed %u\r\n" : "no such process\r\n", pid);
}

static void cmdAttach(CliContext* ctx, int argc, char** argv)
{
    if (argc < 2) { cliWrite(ctx, "usage: attach <pid>\r\n"); return; }
    uint16_t pid = (uint16_t)atoi(argv[1]);
    cliPrintf(ctx, osProcessAttach(pid, ctx->consoleId)
                   ? "attached %u\r\n" : "no such process\r\n", pid);
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
    OsTime t = osTimeGet();
    cliPrintf(ctx, "%s\r\n", OS_VERSION);
    cliPrintf(ctx, "Build:      %s\r\n", OS_BUILD_DATE);
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