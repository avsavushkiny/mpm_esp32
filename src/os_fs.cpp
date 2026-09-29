#include "os_fs.h"
#include "os_user.h"

bool osFsInit()
{
    if (!SPIFFS.begin(true)) { OS_LOG("SPIFFS mount failed"); return false; }
    OS_LOG("SPIFFS mounted");
    return true;
}

bool osFsExists(const char* path) { return SPIFFS.exists(path); }

size_t osFsRead(const char* path, String& out)
{
    if (!osUserHasPriv(OS_PRIV_READ_FS)) {
        OS_LOG("read denied: no privilege");
        return 0;
    }
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return 0;
    out = f.readString();
    size_t n = f.size();
    f.close();
    return n;
}

bool osFsWrite(const char* path, const String& data)
{
    if (!osUserHasPriv(OS_PRIV_WRITE_FS)) {
        OS_LOG("write denied: no privilege");
        return false;
    }
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) return false;
    size_t w = f.print(data);
    f.close();
    return w == data.length();
}

bool osFsRemove(const char* path)
{
    if (!osUserHasPriv(OS_PRIV_WRITE_FS)) return false;
    return SPIFFS.remove(path);
}

void osFsList(void (*cb)(const char* name, size_t size, void* user), void* user)
{
    if (!osUserHasPriv(OS_PRIV_READ_FS)) return;
    File root = SPIFFS.open("/");
    if (!root) return;
    File f = root.openNextFile();
    while (f) {
        if (cb) cb(f.name(), f.size(), user);
        f = root.openNextFile();
    }
}