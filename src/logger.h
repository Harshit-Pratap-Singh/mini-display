#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#define LOG_BUFFER_SIZE 16
#define LOG_LINE_LENGTH 96

void loggerInit();
void logPrint(const String &msg);
void logPrintf(const char* format, ...);
String logGetAll();

// A crash's cause and call stack, kept in RTC memory across the restart it causes.
void crashRecordOnBoot();  // once, early in setup()
String crashRecordText();  // "" unless this boot followed a crash

#endif
