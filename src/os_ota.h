#pragma once

#include "os_config.h"

// Запустить OTA в фоне (асинхронно)
bool osOtaStartFromUrl(const char* url, uint16_t consoleId);

// Синхронная версия (используется из otaTask)
bool osOtaUpdateFromUrl(const char* url, char* outErr, size_t errMax);

// Отменить текущий OTA
void osOtaCancel();

// Проверить, идёт ли OTA
bool osOtaIsRunning();