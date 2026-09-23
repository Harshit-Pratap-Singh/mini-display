#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>
#include "display.h" // Include for display functions

void webserverInit();
void webserverHandle();
unsigned long webserverLastActivityMs();  // millis() of the last request served or waiting
void webserverProcessPendingActions();
bool webserverHasPendingNetworkAction();
void webserverApplyEffectiveBrightness(bool syncPreference = false);
void handleRoot();

extern int currentBrightness;
extern int currentTheme;
extern char currentImage[DISPLAY_PATH_BUFFER_SIZE];
extern String apPassword; // Random AP password generated at runtime
extern bool wifiFailsafeMode; // WiFi failsafe mode flag

#endif
