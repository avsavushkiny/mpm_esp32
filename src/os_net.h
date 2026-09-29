#pragma once
#include "os_config.h"

bool   osNetInit(const char* ssid, const char* pass);
bool   osNetConnected();
String osNetIp();

bool   osNetStartTelnet(uint16_t port);
void   osNetStopTelnet();