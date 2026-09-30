#include "os_user.h"
#include "os_kernel.h"
#include <string.h>

// ---------- Статическая таблица пользователей ----------
static OsUser g_users[OS_MAX_USERS];
static SemaphoreHandle_t g_userMutex = nullptr;

// ============================================================
//  FNV-1a 64-bit хэш (замена SHA-256)
// ============================================================
//  Не криптостойкий, но:
//   - детерминированный
//   - быстрый (десятки тактов)
//   - даёт 64-битный результат = 16 hex-символов
//   - два раунда с разной солью = 32 hex-символа (как SHA-256)
//
//  Для учебной ОС с 2..8 пользователями — приемлемо.
//  Для продакшена — верните SHA-256 (mbedtls_sha256_starts без _ret)
//  или PBKDF2-HMAC.
// ============================================================

static uint64_t fnv1a64(const char* data)
{
    // FNV-1a: hash = (hash XOR byte) * prime
    // offset basis: 14695981039346656037 (2^64 / golden ratio)
    // prime:        1099511628211
    uint64_t hash = 14695981039346656037ULL;
    while (*data) {
        hash ^= (uint8_t)(*data++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void osUserHashPassword(const char* password, char* outHex)
{
    if (!outHex) return;
    if (!password) password = "";

    // ---------- Раунд 1 ----------
    char buf1[OS_PASSWORD_MAX + 24];
    snprintf(buf1, sizeof(buf1), "MPM$v1$%s$ESP32", password);
    uint64_t h1 = fnv1a64(buf1);

    // ---------- Раунд 2 (зависит от h1 — эффект лавины) ----------
    char buf2[OS_PASSWORD_MAX + 48];
    snprintf(buf2, sizeof(buf2), "SALT2$%s$%016llx",
             password, (unsigned long long)h1);
    uint64_t h2 = fnv1a64(buf2);

    // ---------- 32 hex-символа: h1 (16) + h2 (16) ----------
    snprintf(outHex, OS_PASSWORD_HASH + 1,
             "%016llx%016llx",
             (unsigned long long)h1,
             (unsigned long long)h2);
}

// ---------- Внутренняя проверка пароля ----------
static bool checkPassword(const OsUser* u, const char* password)
{
    if (!u || !u->enabled) return false;
    char hash[OS_PASSWORD_HASH + 1];
    osUserHashPassword(password, hash);
    return strcmp(hash, u->passHash) == 0;
}

// ---------- Инициализация ----------
bool osUserInit()
{
    g_userMutex = xSemaphoreCreateMutex();
    if (!g_userMutex) return false;

    memset(g_users, 0, sizeof(g_users));

    // root: полный доступ, пароль по умолчанию "root"
    g_users[0].uid        = OS_UID_ROOT;
    strncpy(g_users[0].name, "root", OS_USERNAME_MAX - 1);
    osUserHashPassword("root", g_users[0].passHash);
    g_users[0].privileges = OS_PRIV_ALL;
    g_users[0].enabled    = true;

    // guest: только чтение, пароль пустой
    g_users[1].uid        = OS_UID_GUEST;
    strncpy(g_users[1].name, "guest", OS_USERNAME_MAX - 1);
    osUserHashPassword("guest", g_users[1].passHash);
    g_users[1].privileges = OS_PRIV_READ_FS;
    g_users[1].enabled    = true;

    OS_LOG("Users initialized");
    return true;
}

// ---------- Аутентификация ----------
uint32_t osUserAuth(const char* name, const char* password)
{
    if (!name || !password) return OS_UID_NOBODY;
    uint32_t uid = OS_UID_NOBODY;

    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return OS_UID_NOBODY;

    for (auto& u : g_users) {
        if (u.enabled && strcmp(u.name, name) == 0) {
            if (checkPassword(&u, password)) uid = u.uid;
            break;
        }
    }

    xSemaphoreGive(g_userMutex);
    return uid;
}

// ---------- Поиск ----------
const OsUser* osUserFindByUid(uint32_t uid)
{
    const OsUser* found = nullptr;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return nullptr;
    for (auto& u : g_users) if (u.uid == uid && u.enabled) { found = &u; break; }
    xSemaphoreGive(g_userMutex);
    return found;
}

const OsUser* osUserFindByName(const char* name)
{
    const OsUser* found = nullptr;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return nullptr;
    for (auto& u : g_users)
        if (strcmp(u.name, name) == 0 && u.enabled) { found = &u; break; }
    xSemaphoreGive(g_userMutex);
    return found;
}

// ---------- Смена пароля ----------
bool osUserChangePassword(uint32_t uid, const char* oldPass, const char* newPass)
{
    if (!newPass || strlen(newPass) >= OS_PASSWORD_MAX) return false;

    bool ok = false;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return false;

    for (auto& u : g_users) {
        if (u.uid == uid && u.enabled) {
            if (!checkPassword(&u, oldPass)) break;
            osUserHashPassword(newPass, u.passHash);
            ok = true;
            break;
        }
    }

    xSemaphoreGive(g_userMutex);
    return ok;
}

// ---------- Создание / удаление ----------
bool osUserCreate(const char* name, const char* password, uint8_t privileges)
{
    if (!name || !password) return false;
    if (strlen(name) >= OS_USERNAME_MAX) return false;

    bool ok = false;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return false;

    // Проверка на дубликат
    for (auto& u : g_users)
        if (u.enabled && strcmp(u.name, name) == 0) {
            xSemaphoreGive(g_userMutex);
            return false;
        }

    // Поиск свободного слота
    for (auto& u : g_users) {
        if (!u.enabled) {
            u.uid = OS_UID_ROOT + 100 + (uint32_t)(&u - g_users);
            strncpy(u.name, name, OS_USERNAME_MAX - 1);
            u.name[OS_USERNAME_MAX - 1] = '\0';
            osUserHashPassword(password, u.passHash);
            u.privileges = privileges;
            u.enabled    = true;
            ok = true;
            break;
        }
    }

    xSemaphoreGive(g_userMutex);
    return ok;
}

bool osUserDelete(const char* name)
{
    bool ok = false;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return false;
    for (auto& u : g_users) {
        if (u.enabled && strcmp(u.name, name) == 0 && u.uid != OS_UID_ROOT) {
            memset(&u, 0, sizeof(u));
            ok = true;
            break;
        }
    }
    xSemaphoreGive(g_userMutex);
    return ok;
}

// ---------- Права ----------
bool osUserSetPrivileges(uint32_t uid, uint8_t privs)
{
    bool ok = false;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return false;
    for (auto& u : g_users) {
        if (u.uid == uid && u.enabled) { u.privileges = privs; ok = true; break; }
    }
    xSemaphoreGive(g_userMutex);
    return ok;
}

bool osUserSetEnabled(uint32_t uid, bool enabled)
{
    bool ok = false;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return false;
    for (auto& u : g_users) {
        if (u.uid == uid) {
            if (uid == OS_UID_ROOT && !enabled) break;  // root отключить нельзя
            u.enabled = enabled;
            ok = true;
            break;
        }
    }
    xSemaphoreGive(g_userMutex);
    return ok;
}

// ---------- Проверка прав текущего процесса ----------
bool osUserHasPriv(uint8_t privBit)
{
    ProcessDescriptor* cur = osProcessCurrent();
    if (!cur) return false;

    const OsUser* u = osUserFindByUid(cur->userNumber);
    if (!u) return false;

    return (u->privileges & privBit) != 0;
}

// ---------- Список ----------
void osUserList(void (*cb)(const OsUser&, void*), void* user)
{
    if (!cb) return;
    if (xSemaphoreTake(g_userMutex, portMAX_DELAY) != pdTRUE) return;
    for (auto& u : g_users) if (u.enabled) cb(u, user);
    xSemaphoreGive(g_userMutex);
}

// ---------- Таблица прав ----------
void osUserPrintPrivileges()
{
    osPrintf("  BIT    HEX    CONSTANT              DESCRIPTION\r\n");
    osPrintf("  -----  -----  --------------------  ------------------------------\r\n");
    osPrintf("    0    0x01   OS_PRIV_READ_FS       read files (ls, cat)\r\n");
    osPrintf("    1    0x02   OS_PRIV_WRITE_FS      write/delete files\r\n");
    osPrintf("    2    0x04   OS_PRIV_KILL_PROC     kill/abort processes\r\n");
    osPrintf("    3    0x08   OS_PRIV_ATTACH        attach/detach consoles\r\n");
    osPrintf("    4    0x10   OS_PRIV_USER_MGMT     useradd/userdel/users/su\r\n");
    osPrintf("    5    0x20   OS_PRIV_REBOOT        reboot system\r\n");
    osPrintf("  -----  -----  --------------------  ------------------------------\r\n");
    osPrintf("  All    0xFF   OS_PRIV_ALL           full access (root)\r\n");
    osPrintf("\r\n");
    osPrintf("  Common combinations:\r\n");
    osPrintf("    0x00  - no rights (nobody)\r\n");
    osPrintf("    0x01  - read-only (guest)\r\n");
    osPrintf("    0x03  - read + write files\r\n");
    osPrintf("    0x0F  - files + kill + attach (normal user)\r\n");
    osPrintf("    0x1F  - everything except reboot\r\n");
    osPrintf("    0xFF  - full access (root)\r\n");
}