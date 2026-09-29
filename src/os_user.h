#pragma once

#include "os_config.h"

// Описание пользователя
struct OsUser {
    uint32_t uid;
    char     name[OS_USERNAME_MAX];
    char     passHash[OS_PASSWORD_HASH + 1];  // hex-строка SHA-256
    uint8_t  privileges;
    bool     enabled;
};

// Инициализация подсистемы пользователей
bool osUserInit();

// Аутентификация: возвращает uid или OS_UID_NOBODY при ошибке
uint32_t osUserAuth(const char* name, const char* password);

// Поиск по uid / имени
const OsUser* osUserFindByUid(uint32_t uid);
const OsUser* osUserFindByName(const char* name);

// Смена пароля (старый пароль проверяется)
bool osUserChangePassword(uint32_t uid, const char* oldPass, const char* newPass);

// Создание / удаление пользователя (только root)
bool osUserCreate(const char* name, const char* password, uint8_t privileges);
bool osUserDelete(const char* name);

// Установка прав
bool osUserSetPrivileges(uint32_t uid, uint8_t privs);
bool osUserSetEnabled(uint32_t uid, bool enabled);

// Проверка прав у текущего процесса
bool osUserHasPriv(uint8_t privBit);

// Список пользователей
void osUserList(void (*cb)(const OsUser&, void*), void* user);

// Утилита: hex-строка SHA-256 от пароля
void osUserHashPassword(const char* password, char* outHex);

// Вывести таблицу прав (для справки root)
void osUserPrintPrivileges();