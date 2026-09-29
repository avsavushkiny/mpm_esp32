#pragma once
#include "os_config.h"

struct OsTime {
    uint32_t ticks;
    uint32_t seconds;
};

bool     osTimeInit();
OsTime   osTimeGet();
uint32_t osTimeMillis();