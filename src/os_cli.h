#pragma once
#include "os_config.h"

struct CliContext {
    uint16_t  consoleId;
    void    (*write)(const char* s, void* user);
    int     (*read)(char* buf, size_t max, void* user);
    void*     user;
};

bool osCliInit();
void osCliExecute(CliContext* ctx, const char* line);
void osCliPrompt(CliContext* ctx);

typedef void (*CmdHandler)(CliContext* ctx, int argc, char** argv);

struct ShellCommand {
    const char* name;
    CmdHandler  handler;
    const char* help;
};

const ShellCommand* osCliCommands(size_t& count);