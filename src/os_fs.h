#pragma once
#include "os_config.h"
#include <FS.h>
#include <SPIFFS.h>

bool   osFsInit();
bool   osFsExists(const char* path);
size_t osFsRead(const char* path, String& out);
bool   osFsWrite(const char* path, const String& data);
bool   osFsRemove(const char* path);
void   osFsList(void (*cb)(const char* name, size_t size, void* user), void* user);