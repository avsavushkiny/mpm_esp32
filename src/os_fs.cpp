#include "os_fs.h"

bool osFsInit()
{
    if (!SPIFFS.begin(true)) { OS_LOG("SPIFFS mount failed"); return false; }
    OS_LOG("SPIFFS mounted");
    return true;
}

bool osFsExists(const char* path) { return SPIFFS.exists(path); }

size_t osFsRead(const char* path, String& out)
{
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return 0;
    out = f.readString();
    size_t n = f.size();
    f.close();
    return n;
}

bool osFsWrite(const char* path, const String& data)
{
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) return false;
    size_t w = f.print(data);
    f.close();
    return w == data.length();
}

bool osFsRemove(const char* path) { return SPIFFS.remove(path); }

void osFsList(void (*cb)(const char* name, size_t size, void* user), void* user)
{
    File root = SPIFFS.open("/");
    if (!root) return;
    File f = root.openNextFile();
    while (f) {
        if (cb) cb(f.name(), f.size(), user);
        f = root.openNextFile();
    }
}