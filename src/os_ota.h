#pragma once

#include "os_config.h"

// Скачать .bin по HTTP и прошить.
// При успехе — вызывает ESP.restart(), сюда не возвращается.
// При ошибке — возвращает false, пишет текст в outErr.
bool osOtaUpdateFromUrl(const char* url, char* outErr, size_t errMax);