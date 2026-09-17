#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "dashboard.h"

// Define buffer sizes for DisplayState char arrays
#define DISPLAY_LINE_BUFFER_SIZE 64
#define DISPLAY_IP_BUFFER_SIZE 24
#define DISPLAY_PATH_BUFFER_SIZE 64
#define DISPLAY_SSID_BUFFER_SIZE 36
#define DISPLAY_PASS_BUFFER_SIZE 16

struct DisplayState {
    char line2[DISPLAY_LINE_BUFFER_SIZE];       // Used for custom messages
    char ipInfo[DISPLAY_IP_BUFFER_SIZE];      // IP address or network info to show at top
    bool showImage;     // True if an image is currently displayed
    char imagePath[DISPLAY_PATH_BUFFER_SIZE];   // Path to the image file
    bool apMode;        // True when showing AP mode credentials screen
    char apSSID[DISPLAY_SSID_BUFFER_SIZE];      // AP mode SSID to display
    char apPassword[DISPLAY_PASS_BUFFER_SIZE];  // AP mode password to display
    uint8_t currentPage;                       // Active dashboard page
    unsigned long lastPageChangeMs;            // millis() of the last page change; drives the rotation timer
};

void displayInit();
void displaySetBrightness(int brightness);
void displayApplyBrightness(int brightness);
void displayUpdate();
void displayRenderClock();
void displayRenderAPMode();
bool displayRenderImage(const char *path);  // false if missing or not decodable
// Drop the cached photo count/path after an upload or delete.
void displayInvalidatePhotoCache();
// Path of the photo the slideshow is currently showing, or "" when there is none.
const char* displayCurrentPhotoPath();
void displayShowMessage(const String &msg);
void displayShowTemporaryMessage(const String &msg, uint32_t durationMs);
void displayShowAPScreen(const char* ssid, const char* password, const char* ip);
void displayBlankScreen();
// Preferred way to change currentPage (render fallbacks and config-save handlers still assign it directly).
// Refuses (false) in AP mode, while a temporary message is up, or if the page is not enabled/has no content.
bool displaySetPage(uint8_t page, bool smoothTransition = false);
bool displayCycleNextPage(bool smoothTransition = false);

extern DisplayState displayState;
extern TFT_eSPI tft;
extern int scrollPos;

#endif
