#pragma once

#include "os_config.h"

// Запустить OTA в отдельной задаче (асинхронно).
// Возвращает true, если задача создана.
bool osOtaStartFromUrl(const char* url, uint16_t consoleId);

// Синхронная версия (используется из ota_task).
bool osOtaUpdateFromUrl(const char* url, char* outErr, size_t errMax);