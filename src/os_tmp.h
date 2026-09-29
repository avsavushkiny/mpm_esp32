#pragma once
#include "os_config.h"
#include <WiFiClient.h>

bool osTmpInitSerial();
void osTmpHandleTelnetClient(WiFiClient& client);