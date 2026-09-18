#include "display.h"

#include "auth.h"
#include "config.h"
#include "feeds.h"
#include "spotify.h"
#include "logger.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <time.h>
#include <vector>

#define FONT_INFO 1
#define FONT_LABEL 2
#define FONT_BODY 4
#define FONT_TITLE 6
#define FONT_HUGE 7

TFT_eSPI tft = TFT_eSPI();
DisplayState displayState;
int scrollPos = 240;

namespace {

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

// --- Spotify player face 1: album art + telemetry -----------------------------
// Palette is fixed, not activeTheme(): these colours come from the mockups in design/.
// DESIGN.md supplies the neutrals and the mockup adds Spotify green as the brand accent.
// DESIGN.md's surface is #111318, a very dark blue-grey rather than black. On a monitor
// that reads as black; on a backlit IPS panel it lands at 6% brightness on top of the
// backlight's own leakage and looks washed grey-blue. True black gives the LCD the
// most contrast it can physically manage, so the page uses it in place of the token.
constexpr uint16_t kSpSurface   = rgb565(0x00, 0x00, 0x00);  // #111318 -> true black
constexpr uint16_t kSpBar       = rgb565(0x0c, 0x0e, 0x13);  // #0c0e13 container-lowest
constexpr uint16_t kSpPanel     = rgb565(0x1d, 0x20, 0x24);  // #1d2024 container
constexpr uint16_t kSpGreen     = rgb565(0x1d, 0xb9, 0x54);  // #1db954 Spotify green
constexpr uint16_t kSpAmber     = rgb565(0xff, 0xb9, 0x5f);  // #ffb95f secondary
constexpr uint16_t kSpText      = rgb565(0xe1, 0xe2, 0xe9);  // #e1e2e9 on-surface
constexpr uint16_t kSpMuted     = rgb565(0x87, 0x92, 0x9a);  // #87929a outline
constexpr uint16_t kSpMint      = rgb565(0x56, 0xe5, 0xa9);  // #56e5a9 tertiary
constexpr uint16_t kSpCyan      = rgb565(0x38, 0xbd, 0xf8);  // #38bdf8 primary

constexpr int kArtBox = 80;   // the mockup's art slot
constexpr int kArtX = 8;
constexpr int kArtY = 28;
constexpr int kColX = 96;     // right-hand text column
constexpr int kColW = 136;
constexpr int kBarX = 8;
constexpr int kBarW = 224;
constexpr int kBarY = 130;

constexpr int kPageMargin = 4;
constexpr int kPageWidth = DISPLAY_WIDTH - (kPageMargin * 2);
constexpr int kHeaderTitleY = 10;
constexpr int kHeaderSubtitleY = 12;
constexpr int kHeaderRuleY = 32;
constexpr int kContentTopY = 40;
constexpr int kClockDynamicRegionX = 12;
constexpr int kClockDynamicRegionY = 40;
constexpr int kClockDynamicRegionWidth = DISPLAY_WIDTH - (kClockDynamicRegionX * 2);
constexpr int kClockDynamicRegionHeight = 66;
constexpr int kClockTimeY = 40;
constexpr int kClockMetaY = 102;
// Seconds sit in a smaller face to the right of HH:MM at a FIXED x, so the big digits
// keep their size and neither part shifts as the value changes (font 7 is monospace,
// but 12-hour time still swaps between 1 and 2 hour digits).
constexpr int kClockSecondsX = 182;
constexpr int kClockSecondsGap = 8;
constexpr int kClockCardY = 28;
constexpr int kClockCardHeight = 104;
constexpr int kClockFooterY = 140;
constexpr int kClockFooterHeight = 92;
// Bold face: full-bleed, no panels.
constexpr int kBoldRuleY = 27;
constexpr int kBoldTimeY = 34;
constexpr int kBoldMetaY = 88;
// Matrix face: a clock strip over a grid of instrument tiles.
constexpr int kMatrixZoneHeight = 116;
constexpr int kMatrixTimeY = 26;
constexpr int kMatrixTimeRight = 150;
constexpr int kMatrixSideX = 164;
constexpr int kMatrixSecondsY = 58;
constexpr int kMatrixSecondsBarY = 84;
constexpr int kMatrixTapeY = 98;
// Radial face: two perimeter gauges around centred digits.
constexpr int kRadialCenter = 120;
constexpr int kRadialOuterRadius = 116;
constexpr int kRadialInnerRadius = 108;
constexpr int kRadialTimeY = 102;
constexpr int kRadialSideX = 198;
constexpr int kRadialDateY = 158;
constexpr uint8_t kPageTransitionFadeDownSteps = 2;
constexpr uint8_t kPageTransitionFadeUpSteps = 3;
constexpr uint16_t kPageTransitionFadeStepDelayMs = 12;
constexpr uint8_t kPageTransitionDimPercent = 82;
constexpr uint8_t kPageTransitionMinimumBrightness = 18;
constexpr size_t kTemporaryMessageBufferSize = 96;

struct ThemePalette {
    uint16_t background;
    uint16_t surface;
    uint16_t surfaceAlt;
    uint16_t accent;
    uint16_t accentSoft;
    uint16_t text;
    uint16_t muted;
    uint16_t positive;
    uint16_t negative;
    uint16_t warning;
};

struct Rgb888 {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct ThemeSelection {
    uint8_t theme;
    bool customThemeEnabled;
    const char *background;
    const char *surface;
    const char *accent;
    const char *text;
};

const ThemePalette kThemes[DASHBOARD_THEME_COUNT] = {
    {
        rgb565(10, 14, 26),     // background - deep navy
        rgb565(21, 27, 46),     // surface
        rgb565(30, 38, 64),     // surfaceAlt
        rgb565(111, 211, 199),  // accent - teal, matches the default seconds colour
        rgb565(35, 75, 82),     // accentSoft
        rgb565(237, 241, 250),  // text
        rgb565(147, 160, 188),  // muted - brighter than upstream so small text reads
        rgb565(123, 214, 167),
        rgb565(255, 180, 171),
        rgb565(255, 210, 128),
    },
    {
        rgb565(33, 16, 11),
        rgb565(61, 34, 26),
        rgb565(74, 43, 32),
        rgb565(255, 183, 77),
        rgb565(118, 75, 35),
        rgb565(255, 239, 232),
        rgb565(224, 189, 173),
        rgb565(151, 221, 168),
        rgb565(255, 172, 162),
        rgb565(255, 214, 120),
    },
    {
        rgb565(9, 18, 14),
        rgb565(19, 36, 28),
        rgb565(26, 47, 37),
        rgb565(165, 230, 174),
        rgb565(47, 83, 60),
        rgb565(232, 247, 236),
        rgb565(160, 188, 170),
        rgb565(137, 230, 164),
        rgb565(255, 169, 169),
        rgb565(233, 241, 129),
    },
};

int appliedBrightness = -1;

enum DisplayRenderMode : uint8_t {
    DISPLAY_MODE_DASHBOARD = 0,
    DISPLAY_MODE_TEMP_MESSAGE,
    DISPLAY_MODE_AP,
    DISPLAY_MODE_IMAGE
};

struct RenderCache {
    bool valid;
    uint8_t mode;
    uint8_t page;
    uint8_t theme;
    uint32_t staticHash;
    uint32_t dynamicHash;
    char imagePath[DISPLAY_PATH_BUFFER_SIZE];
};

RenderCache renderCache = {false, DISPLAY_MODE_DASHBOARD, 0, 0, 0, 0, {0}};

struct ClockMetaCache {
    bool valid;
    char metaLine[40];
};

ClockMetaCache clockMetaCache = {false, {0}};

struct TemporaryMessageState {
    bool active;
    unsigned long expiresAtMs;
    char message[kTemporaryMessageBufferSize];
};

TemporaryMessageState temporaryMessage = {false, 0, {0}};

constexpr uint32_t kFnvOffset = 2166136261UL;
constexpr uint32_t kFnvPrime = 16777619UL;

void hashBytes(uint32_t &hash, const uint8_t *data, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        hash ^= data[index];
        hash *= kFnvPrime;
    }
}

template <typename TValue>
void hashValue(uint32_t &hash, const TValue &value) {
    hashBytes(hash, reinterpret_cast<const uint8_t*>(&value), sizeof(TValue));
}

void hashCString(uint32_t &hash, const char *value) {
    if (value == nullptr) {
        const uint8_t zero = 0;
        hashBytes(hash, &zero, sizeof(zero));
        return;
    }

    hashBytes(hash, reinterpret_cast<const uint8_t*>(value), strlen(value));
    const uint8_t terminator = 0;
    hashBytes(hash, &terminator, sizeof(terminator));
}

void invalidateRenderCache() {
    renderCache.valid = false;
    renderCache.imagePath[0] = '\0';
    clockMetaCache.valid = false;
    clockMetaCache.metaLine[0] = '\0';
}

void clearTemporaryMessage() {
    temporaryMessage.active = false;
    temporaryMessage.expiresAtMs = 0;
    temporaryMessage.message[0] = '\0';
}

bool temporaryMessageExpired() {
    return temporaryMessage.active &&
           static_cast<long>(millis() - temporaryMessage.expiresAtMs) >= 0;
}

bool temporaryMessageVisible() {
    return temporaryMessage.active &&
           temporaryMessage.message[0] != '\0' &&
           !temporaryMessageExpired();
}

Rgb888 rgb565ToRgb888(uint16_t color) {
    Rgb888 rgb;
    rgb.red = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
    rgb.green = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
    rgb.blue = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
    return rgb;
}

uint16_t rgb888ToRgb565(const Rgb888 &rgb) {
    return rgb565(rgb.red, rgb.green, rgb.blue);
}

bool parseHexColor888(const char *value, Rgb888 &rgb) {
    if (value == nullptr) {
        return false;
    }

    const char *source = value[0] == '#' ? value + 1 : value;
    if (strlen(source) != 6) {
        return false;
    }

    char *end = nullptr;
    unsigned long raw = strtoul(source, &end, 16);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    rgb.red = static_cast<uint8_t>((raw >> 16) & 0xFF);
    rgb.green = static_cast<uint8_t>((raw >> 8) & 0xFF);
    rgb.blue = static_cast<uint8_t>(raw & 0xFF);
    return true;
}

Rgb888 blendRgb(const Rgb888 &base, const Rgb888 &mix, uint8_t mixWeightPercent) {
    uint16_t baseWeight = 100U - constrain(mixWeightPercent, 0, 100);
    uint16_t mixWeight = constrain(mixWeightPercent, 0, 100);

    Rgb888 result;
    result.red = static_cast<uint8_t>((base.red * baseWeight + mix.red * mixWeight) / 100U);
    result.green = static_cast<uint8_t>((base.green * baseWeight + mix.green * mixWeight) / 100U);
    result.blue = static_cast<uint8_t>((base.blue * baseWeight + mix.blue * mixWeight) / 100U);
    return result;
}

ThemeSelection effectiveThemeSelection() {
    if (dashboardNightModeActive() && dashboardConfig.nightThemeEnabled) {
        return {
            dashboardConfig.nightTheme,
            dashboardConfig.nightCustomThemeEnabled,
            dashboardConfig.nightCustomBackground,
            dashboardConfig.nightCustomSurface,
            dashboardConfig.nightCustomAccent,
            dashboardConfig.nightCustomText
        };
    }

    return {
        dashboardConfig.theme,
        dashboardConfig.customThemeEnabled,
        dashboardConfig.customBackground,
        dashboardConfig.customSurface,
        dashboardConfig.customAccent,
        dashboardConfig.customText
    };
}

uint8_t activeThemeIdValue() {
    ThemeSelection selection = effectiveThemeSelection();
    return constrain(selection.theme, 0, DASHBOARD_THEME_COUNT - 1);
}

ThemePalette buildCustomTheme(const ThemePalette &base,
                              const char *backgroundColor,
                              const char *surfaceColor,
                              const char *accentColor,
                              const char *textColor) {
    Rgb888 background = rgb565ToRgb888(base.background);
    Rgb888 surface = rgb565ToRgb888(base.surface);
    Rgb888 accent = rgb565ToRgb888(base.accent);
    Rgb888 text = rgb565ToRgb888(base.text);

    parseHexColor888(backgroundColor, background);
    parseHexColor888(surfaceColor, surface);
    parseHexColor888(accentColor, accent);
    parseHexColor888(textColor, text);

    ThemePalette theme = {};
    theme.background = rgb888ToRgb565(background);
    theme.surface = rgb888ToRgb565(surface);
    theme.surfaceAlt = rgb888ToRgb565(blendRgb(surface, background, 22));
    theme.accent = rgb888ToRgb565(accent);
    theme.accentSoft = rgb888ToRgb565(blendRgb(surface, accent, 28));
    theme.text = rgb888ToRgb565(text);
    theme.muted = rgb888ToRgb565(blendRgb(text, background, 52));
    theme.positive = base.positive;
    theme.negative = base.negative;
    theme.warning = base.warning;
    return theme;
}

void hashThemeConfig(uint32_t &hash) {
    ThemeSelection selection = effectiveThemeSelection();
    hashValue(hash, selection.theme);
    hashValue(hash, selection.customThemeEnabled);
    if (!selection.customThemeEnabled) {
        return;
    }

    hashCString(hash, selection.background);
    hashCString(hash, selection.surface);
    hashCString(hash, selection.accent);
    hashCString(hash, selection.text);
}

bool tftOutput(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap) {
    if (y >= tft.height()) {
        return false;
    }

    tft.pushImage(x, y, width, height, bitmap);
    return true;
}

const ThemePalette& activeTheme() {
    static ThemePalette customTheme;
    ThemeSelection selection = effectiveThemeSelection();
    uint8_t themeIndex = constrain(selection.theme, 0, DASHBOARD_THEME_COUNT - 1);
    if (!selection.customThemeEnabled) {
        return kThemes[themeIndex];
    }

    customTheme = buildCustomTheme(kThemes[themeIndex],
                                   selection.background,
                                   selection.surface,
                                   selection.accent,
                                   selection.text);
    return customTheme;
}

String safeCString(const char *value) {
    return (value != nullptr) ? String(value) : String("");
}

bool hasTimeSync() {
    return dashboardCurrentEpoch() != 0;
}

String trimTrailingZeros(float value, uint8_t precision = 2) {
    String result(value, precision);
    while (result.endsWith("0")) {
        result.remove(result.length() - 1);
    }
    if (result.endsWith(".")) {
        result.remove(result.length() - 1);
    }
    return result;
}

String groupThousands(const String &value) {
    int decimalIndex = value.indexOf('.');
    if (decimalIndex < 0) {
        decimalIndex = value.length();
    }

    int startIndex = value.startsWith("-") ? 1 : 0;
    if (decimalIndex - startIndex <= 3) {
        return value;
    }

    String grouped;
    grouped.reserve(value.length() + ((decimalIndex - startIndex - 1) / 3));
    if (startIndex == 1) {
        grouped += '-';
    }

    for (int index = startIndex; index < static_cast<int>(value.length()); ++index) {
        grouped += value.charAt(index);

        if (index < decimalIndex - 1) {
            int remainingDigits = decimalIndex - index - 1;
            if (remainingDigits > 0 && (remainingDigits % 3) == 0) {
                grouped += ',';
            }
        }
    }

    return grouped;
}

String formatTimeForTm(const tm &timeInfo, bool showSeconds, bool use24Hour, bool *isPm = nullptr) {
    char buffer[16];
    const char *format = nullptr;

    if (use24Hour) {
        format = showSeconds ? "%H:%M:%S" : "%H:%M";
    } else {
        format = showSeconds ? "%I:%M:%S" : "%I:%M";
        if (isPm != nullptr) {
            *isPm = timeInfo.tm_hour >= 12;
        }
    }

    strftime(buffer, sizeof(buffer), format, &timeInfo);
    return String(buffer);
}

String formatLocalTime(bool showSeconds, bool use24Hour, bool *isPm = nullptr) {
    if (!hasTimeSync()) {
        if (isPm != nullptr) {
            *isPm = false;
        }
        return showSeconds ? "--:--:--" : "--:--";
    }

    time_t now = time(nullptr);
    tm localTimeInfo;
    localtime_r(&now, &localTimeInfo);
    return formatTimeForTm(localTimeInfo, showSeconds, use24Hour, isPm);
}

String formatLocalDate() {
    if (!hasTimeSync()) {
        return "Waiting for time";
    }

    char buffer[32];
    time_t now = time(nullptr);
    tm localTimeInfo;
    localtime_r(&now, &localTimeInfo);
    strftime(buffer, sizeof(buffer), "%a %d %b", &localTimeInfo);  // weekday, no year
    return String(buffer);
}

String formatWorldTime(long offsetSeconds) {
    if (!hasTimeSync()) {
        return "--:--";
    }

    time_t now = time(nullptr) + offsetSeconds;
    tm utcTimeInfo;
    gmtime_r(&now, &utcTimeInfo);
    return formatTimeForTm(utcTimeInfo, false, dashboardConfig.use24Hour);
}

String formatDuration(int32_t totalSeconds) {
    int32_t boundedSeconds = (totalSeconds > 0) ? totalSeconds : 0;
    int32_t hours = boundedSeconds / 3600;
    int32_t minutes = (boundedSeconds % 3600) / 60;
    int32_t seconds = boundedSeconds % 60;

    char buffer[16];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%ld:%02ld", static_cast<long>(hours), static_cast<long>(minutes));
    } else {
        snprintf(buffer, sizeof(buffer), "%02ld:%02ld", static_cast<long>(minutes), static_cast<long>(seconds));
    }
    return String(buffer);
}

String formatRelativeCountdown(int32_t totalSeconds) {
    int32_t boundedSeconds = (totalSeconds > 0) ? totalSeconds : 0;
    if (boundedSeconds <= 0) {
        return "Now";
    }

    int32_t hours = boundedSeconds / 3600;
    int32_t minutes = (boundedSeconds % 3600) / 60;

    if (hours > 0) {
        return "in " + String(hours) + "h " + String(minutes) + "m";
    }

    return "in " + String((minutes > 1) ? minutes : 1) + "m";
}

String formatUtcOffset(long offsetSeconds) {
    long absoluteSeconds = labs(offsetSeconds);
    long hours = absoluteSeconds / 3600;
    long minutes = (absoluteSeconds % 3600) / 60;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "UTC%c%02ld:%02ld",
             offsetSeconds >= 0 ? '+' : '-',
             hours,
             minutes);
    return String(buffer);
}

String formatClockMetaLine(bool isPm) {
    String metaLine = formatLocalDate();
    if (!dashboardConfig.use24Hour && hasTimeSync()) {
        metaLine += isPm ? "  PM" : "  AM";
    }
    return metaLine;
}

void updateClockMetaCache(const String &metaLine) {
    strncpy(clockMetaCache.metaLine, metaLine.c_str(), sizeof(clockMetaCache.metaLine) - 1);
    clockMetaCache.metaLine[sizeof(clockMetaCache.metaLine) - 1] = '\0';
    clockMetaCache.valid = true;
}

template <typename TCanvas>
int chooseCanvasFittingFont(TCanvas &canvas,
                            const String &text,
                            int maxWidth,
                            const int *fontCandidates,
                            size_t fontCandidateCount) {
    if (fontCandidates == nullptr || fontCandidateCount == 0) {
        return FONT_INFO;
    }

    for (size_t index = 0; index < fontCandidateCount; ++index) {
        int font = fontCandidates[index];
        if (canvas.textWidth(text, font) <= maxWidth) {
            return font;
        }
    }

    return fontCandidates[fontCandidateCount - 1];
}

// Hour / minute / second each get their own colour, set as hex in the dashboard.
struct ClockColors {
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
};

uint16_t colorFromHex(const char *hex, uint16_t fallback) {
    Rgb888 rgb;
    if (!parseHexColor888(hex, rgb)) {
        return fallback;
    }
    return rgb565(rgb.red, rgb.green, rgb.blue);
}

ClockColors activeClockColors() {
    const ThemePalette &theme = activeTheme();
    ClockColors colors;
    colors.hour = colorFromHex(dashboardConfig.clockHourColor, theme.text);
    colors.minute = colorFromHex(dashboardConfig.clockMinuteColor, theme.text);
    colors.second = colorFromHex(dashboardConfig.clockSecondColor, theme.accent);
    return colors;
}

// Draws `text` so the draw overwrites its own previous pixels: the padded fill and the
// glyphs go down in one call, so nothing blinks on the once-a-second redraws. Padding is
// applied to the right of a left datum, both sides of a centre datum and to the left of a
// right datum, so pick the datum that faces the digits that can change width.
void drawPaddedText(const String &text,
                    int x,
                    int y,
                    uint8_t datum,
                    int font,
                    int padWidth,
                    uint16_t color,
                    uint16_t background) {
    tft.setTextDatum(datum);
    tft.setTextFont(font);
    tft.setTextColor(color, background);
    tft.setTextPadding(padWidth);
    tft.drawString(text, x, y, font);
    tft.setTextPadding(0);
}

struct ClockParts {
    String hours;
    String minutes;
    String seconds;  // empty when the clock is not showing seconds
};

// Splits "HH:MM" or "HH:MM:SS" as formatLocalTime() produced it.
ClockParts splitClockText(const String &timeText) {
    ClockParts parts;
    String big = timeText;
    int firstColon = timeText.indexOf(':');
    int lastColon = timeText.lastIndexOf(':');
    if (firstColon > 0 && lastColon > firstColon) {
        big = timeText.substring(0, lastColon);
        parts.seconds = timeText.substring(lastColon + 1);
    }
    parts.hours = firstColon > 0 ? big.substring(0, firstColon) : big;
    parts.minutes = firstColon > 0 ? big.substring(firstColon + 1) : String();
    return parts;
}

// Width of the widest HH:MM the big font can produce, so a face can centre the block
// once and keep it still while the hours swap between one and two digits.
int bigTimeWidth() {
    return (tft.textWidth("88", FONT_HUGE) * 2) + tft.textWidth(":", FONT_HUGE);
}

// The colon blinks one display tick lit, one tick dark. The phase is latched once per
// update rather than read from millis() at each use: dashboardDynamicHash() and the draw
// inside drawBigTime() happen a whole render apart, and a flip in between would cache a
// parity the panel never showed, leaving the colon half a beat out until the next change.
bool clockColonLit = true;

void latchClockColonPhase() {
    clockColonLit = ((millis() / DISPLAY_UPDATE_INTERVAL) & 1UL) == 0UL;
}

bool clockColonVisible() {
    return clockColonLit;
}

// Draws HH:MM in the configured hour and minute colours. `rightEdge` pins the right of the
// minutes, and the hours are right-aligned against the colon with a two-digit-wide pad, so
// a 12-hour clock dropping its leading digit erases the old one and shifts nothing.
void drawBigTime(const ClockParts &parts, int rightEdge, int y, uint16_t background) {
    const ThemePalette &theme = activeTheme();
    ClockColors colors = activeClockColors();

    bool hasMinutes = parts.minutes.length() > 0;
    int minutesWidth = hasMinutes ? tft.textWidth(parts.minutes, FONT_HUGE) : 0;
    int colonWidth = hasMinutes ? tft.textWidth(":", FONT_HUGE) : 0;
    int minutesLeft = rightEdge - minutesWidth;
    int colonLeft = minutesLeft - colonWidth;

    drawPaddedText(parts.hours, colonLeft, y, TR_DATUM, FONT_HUGE,
                   tft.textWidth("88", FONT_HUGE) + 6, colors.hour, background);
    if (hasMinutes) {
        if (clockColonVisible()) {
            drawPaddedText(":", colonLeft, y, TL_DATUM, FONT_HUGE, 0, theme.muted, background);
        } else {
            // Blank the colon by its own footprint. Redrawing it in the background colour
            // would not work: TFT_eSPI skips the glyph background whenever fg == bg.
            tft.setTextFont(FONT_HUGE);
            tft.fillRect(colonLeft, y, colonWidth, tft.fontHeight(), background);
        }
        drawPaddedText(parts.minutes, minutesLeft, y, TL_DATUM, FONT_HUGE, 0, colors.minute, background);
    }
}

// Square-cornered panel: the instrument-panel look the matrix and bold faces use, as
// opposed to drawRoundedPanel() which the card faces use.
void drawTile(int x, int y, int width, int height, uint16_t fillColor, uint16_t borderColor) {
    tft.fillRect(x, y, width, height, fillColor);
    tft.drawRect(x, y, width, height, borderColor);
}

// Chunky segmented level bar. `segments` blocks, the first `percent` of them lit.
void drawSegmentBar(int x, int y, int width, int height, int percent, int segments,
                    uint16_t onColor, uint16_t offColor) {
    if (segments < 1) {
        return;
    }
    int gap = 2;
    int segmentWidth = (width - (gap * (segments - 1))) / segments;
    if (segmentWidth < 1) {
        return;
    }
    int bounded = constrain(percent, 0, 100);
    int lit = (bounded * segments + 99) / 100;  // any non-zero value lights at least one
    for (int index = 0; index < segments; ++index) {
        tft.fillRect(x + (index * (segmentWidth + gap)), y, segmentWidth, height,
                     index < lit ? onColor : offColor);
    }
}

// Solid progress bar, used for the seconds sweep on the matrix face.
void drawLevelBar(int x, int y, int width, int height, int percent,
                  uint16_t onColor, uint16_t offColor) {
    int filled = (constrain(percent, 0, 100) * width) / 100;
    tft.fillRect(x, y, width, height, offColor);
    if (filled > 0) {
        tft.fillRect(x, y, filled, height, onColor);
    }
}

// A small triangle marker: up for a high/sunrise, down for a low/sunset.
void drawTriangleMarker(int centerX, int centerY, int size, bool pointsUp, uint16_t color) {
    if (pointsUp) {
        tft.fillTriangle(centerX, centerY - size, centerX - size, centerY + size,
                         centerX + size, centerY + size, color);
    } else {
        tft.fillTriangle(centerX, centerY + size, centerX - size, centerY - size,
                         centerX + size, centerY - size, color);
    }
}

String headerSubtitle() {
    if (displayState.apMode && displayState.ipInfo[0] != '\0') {
        return safeCString(displayState.ipInfo);
    }

    if (!dashboardConfig.showIp) {
        return "";
    }

    if (displayState.ipInfo[0] != '\0') {
        return safeCString(displayState.ipInfo);
    }

    return "";
}

std::vector<String> wrapText(const String &text, int font, int maxWidth) {
    std::vector<String> lines;

    if (text.isEmpty()) {
        lines.push_back("");
        return lines;
    }

    tft.setTextFont(font);
    String currentLine;
    String currentWord;
    currentLine.reserve(text.length() + 4);
    currentWord.reserve(text.length() + 4);

    auto flushWord = [&](bool forceLineBreak) {
        if (currentWord.isEmpty()) {
            return;
        }

        String candidate = currentLine;
        if (!candidate.isEmpty()) {
            candidate += " ";
        }
        candidate += currentWord;

        if (forceLineBreak || tft.textWidth(candidate) > maxWidth) {
            if (!currentLine.isEmpty()) {
                lines.push_back(currentLine);
                currentLine = currentWord;
            } else {
                lines.push_back(currentWord);
                currentLine = "";
            }
        } else {
            currentLine = candidate;
        }

        currentWord = "";
    };

    for (size_t index = 0; index < text.length(); ++index) {
        char character = text.charAt(index);

        if (character == '\n') {
            flushWord(false);
            lines.push_back(currentLine);
            currentLine = "";
        } else if (character == ' ') {
            flushWord(false);
        } else {
            currentWord += character;
        }
    }

    flushWord(false);
    if (!currentLine.isEmpty()) {
        lines.push_back(currentLine);
    }

    if (lines.empty()) {
        lines.push_back("");
    }

    return lines;
}

void drawRoundedPanel(int x, int y, int width, int height, uint16_t fillColor, uint16_t borderColor) {
    tft.fillRoundRect(x, y, width, height, 18, fillColor);
    tft.drawRoundRect(x, y, width, height, 18, borderColor);
}

void drawBadge(int x, int y, int width, int height, const String &label, uint16_t fillColor, uint16_t textColor) {
    tft.fillRoundRect(x, y, width, height, height / 2, fillColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(FONT_LABEL);
    tft.setTextColor(textColor, fillColor);
    tft.drawString(label, x + (width / 2), y + (height / 2), FONT_LABEL);
}

void drawWrappedCenteredText(const String &text,
                             int centerX,
                             int startY,
                             int maxWidth,
                             int font,
                             uint16_t textColor,
                             uint16_t backgroundColor,
                             int lineSpacing = 4) {
    std::vector<String> lines = wrapText(text, font, maxWidth);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(font);
    tft.setTextColor(textColor, backgroundColor);
    int lineHeight = tft.fontHeight() + lineSpacing;

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        tft.drawString(lines[lineIndex], centerX, startY + (lineIndex * lineHeight), font);
    }
}

int chooseFittingFont(const String &text, int maxWidth, const int *fontCandidates, size_t fontCandidateCount) {
    if (fontCandidates == nullptr || fontCandidateCount == 0) {
        return FONT_INFO;
    }

    for (size_t index = 0; index < fontCandidateCount; ++index) {
        int font = fontCandidates[index];
        if (tft.textWidth(text, font) <= maxWidth) {
            return font;
        }
    }

    return fontCandidates[fontCandidateCount - 1];
}

void drawAdaptiveBadge(int x,
                       int y,
                       int width,
                       int height,
                       const String &label,
                       const int *fontCandidates,
                       size_t fontCandidateCount,
                       uint16_t fillColor,
                       uint16_t textColor) {
    tft.fillRoundRect(x, y, width, height, height / 2, fillColor);
    tft.setTextDatum(MC_DATUM);

    int font = chooseFittingFont(label, max(0, width - 12), fontCandidates, fontCandidateCount);
    tft.setTextFont(font);
    tft.setTextColor(textColor, fillColor);
    tft.drawString(label, x + (width / 2), y + (height / 2), font);
}

// Clips `text` to `maxWidth`, ending it with ".." so the cut is visible. The smallest
// font candidate is often still too wide for a long city name, and drawString() neither
// clips nor wraps - it just paints over whatever is next to it.
String fitTextToWidth(const String &text, int font, int maxWidth) {
    if (maxWidth <= 0 || tft.textWidth(text, font) <= maxWidth) {
        return text;
    }
    String trimmed = text;
    while (trimmed.length() > 1 && tft.textWidth(trimmed + "..", font) > maxWidth) {
        trimmed.remove(trimmed.length() - 1);
    }
    // textWidth() counts bytes but drawString() decodes UTF-8, so a cut inside a multi-byte
    // sequence leaves a lead byte that swallows the first marker dot as a continuation byte.
    while (trimmed.length() > 0 &&
           (static_cast<uint8_t>(trimmed[trimmed.length() - 1]) & 0xC0) == 0x80) {
        trimmed.remove(trimmed.length() - 1);
    }
    if (trimmed.length() > 0 && (static_cast<uint8_t>(trimmed[trimmed.length() - 1]) & 0x80) != 0) {
        trimmed.remove(trimmed.length() - 1);
    }
    // Room for one character and the marker, or nothing sensible fits at all.
    return tft.textWidth(trimmed + "..", font) <= maxWidth ? trimmed + ".." : String();
}

void drawAdaptiveText(const String &text,
                      int x,
                      int y,
                      int maxWidth,
                      uint8_t datum,
                      const int *fontCandidates,
                      size_t fontCandidateCount,
                      uint16_t textColor,
                      uint16_t backgroundColor) {
    int font = chooseFittingFont(text, maxWidth, fontCandidates, fontCandidateCount);
    tft.setTextDatum(datum);
    tft.setTextFont(font);
    tft.setTextColor(textColor, backgroundColor);
    tft.drawString(fitTextToWidth(text, font, maxWidth), x, y, font);
}

void drawDividerLine(int x, int y, int width, uint16_t color) {
    tft.drawFastHLine(x, y, width, color);
}

// The built-in fonts stop at ASCII 127, so there is no degree glyph to print.
// Draw it: a small ring, sized to the font it sits next to.
void drawDegreeRing(int x, int y, int radius, uint16_t color, uint16_t background) {
    tft.fillCircle(x, y, radius, color);
    tft.fillCircle(x, y, radius - (radius > 3 ? 2 : 1), background);
}

// --- Weather icons -------------------------------------------------------------
// Drawn with primitives instead of PROGMEM bitmaps: a few hundred bytes of code
// instead of a bitmap per condition, and they follow the theme colours for free.
enum WeatherGlyph : uint8_t {
    WEATHER_GLYPH_SUN = 0,
    WEATHER_GLYPH_MOON,
    WEATHER_GLYPH_PARTLY,
    WEATHER_GLYPH_PARTLY_NIGHT,
    WEATHER_GLYPH_SUNRISE,
    WEATHER_GLYPH_CLOUD,
    WEATHER_GLYPH_FOG,
    WEATHER_GLYPH_RAIN,
    WEATHER_GLYPH_SNOW,
    WEATHER_GLYPH_STORM,
    WEATHER_GLYPH_UNKNOWN
};

// WMO 4677 code groups, as Open-Meteo reports them.
WeatherGlyph weatherGlyphForCode(int code, bool isNight) {
    if (code < 0) {
        return WEATHER_GLYPH_UNKNOWN;
    }
    if (code == 0) {
        return isNight ? WEATHER_GLYPH_MOON : WEATHER_GLYPH_SUN;
    }
    if (code == 1 || code == 2) {
        return isNight ? WEATHER_GLYPH_PARTLY_NIGHT : WEATHER_GLYPH_PARTLY;
    }
    if (code == 3) {
        return WEATHER_GLYPH_CLOUD;
    }
    if (code == 45 || code == 48) {
        return WEATHER_GLYPH_FOG;
    }
    if (code >= 95) {
        return WEATHER_GLYPH_STORM;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return WEATHER_GLYPH_SNOW;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return WEATHER_GLYPH_RAIN;
    }
    return WEATHER_GLYPH_CLOUD;
}

// A puffy cloud centred on (cx, cy), `width` wide.
void drawCloudShape(int cx, int cy, int width, uint16_t fill) {
    int r = width / 4;
    tft.fillCircle(cx - r, cy, r, fill);
    tft.fillCircle(cx + r, cy, r * 3 / 4, fill);
    tft.fillCircle(cx, cy - r * 2 / 3, r, fill);
    tft.fillRoundRect(cx - width / 2, cy - r / 2, width, r + r / 2, r / 2, fill);
}

// Crescent: a filled disc with a second disc punched out of it in the background colour.
void drawMoonShape(int cx, int cy, int radius, uint16_t fill, uint16_t background) {
    tft.fillCircle(cx, cy, radius, fill);
    tft.fillCircle(cx + radius / 2, cy - radius / 3, radius, background);
}

// Sun sitting on the horizon with rays above it: used around sunrise and sunset.
void drawSunriseShape(int cx, int cy, int radius, uint16_t fill, uint16_t accent, uint16_t background) {
    tft.fillCircle(cx, cy, radius, fill);
    // Cut the disc off at the horizon with whatever is actually behind this icon - the
    // faces sit on different panels, so this cannot assume one surface colour.
    tft.fillRect(cx - radius - 6, cy + 1, (radius + 6) * 2, radius + 2, background);
    tft.drawFastHLine(cx - radius - 8, cy + 1, (radius + 8) * 2, accent);
    for (int i = -1; i <= 1; ++i) {
        int x = cx + (i * (radius + 5));
        tft.drawLine(x, cy - radius - 4, x, cy - radius - 9, accent);
    }
}

void drawSunShape(int cx, int cy, int radius, uint16_t fill) {
    tft.fillCircle(cx, cy, radius, fill);
    for (int i = 0; i < 8; ++i) {
        float angle = static_cast<float>(i) * (PI / 4.0f);
        int inner = radius + 3;
        int outer = radius + 7;
        tft.drawLine(cx + static_cast<int>(cosf(angle) * inner),
                     cy + static_cast<int>(sinf(angle) * inner),
                     cx + static_cast<int>(cosf(angle) * outer),
                     cy + static_cast<int>(sinf(angle) * outer),
                     fill);
    }
}

// `size` is the box the glyph is drawn inside, centred on (cx, cy).
void drawWeatherIcon(int cx, int cy, int size, int code, uint16_t primary, uint16_t accent,
                     uint16_t background, bool isNight, bool nearSunEvent) {
    WeatherGlyph glyph = nearSunEvent && (code <= 3) ? WEATHER_GLYPH_SUNRISE
                                                     : weatherGlyphForCode(code, isNight);
    int cloudWidth = size * 4 / 5;

    switch (glyph) {
        case WEATHER_GLYPH_SUN:
            drawSunShape(cx, cy, size / 4, accent);
            break;
        case WEATHER_GLYPH_MOON:
            drawMoonShape(cx, cy, size / 4, accent, background);
            break;
        case WEATHER_GLYPH_SUNRISE:
            drawSunriseShape(cx, cy, size / 5, accent, primary, background);
            break;
        case WEATHER_GLYPH_PARTLY_NIGHT:
            drawMoonShape(cx + size / 5, cy - size / 5, size / 6, accent, background);
            drawCloudShape(cx - size / 10, cy + size / 8, cloudWidth, primary);
            break;
        case WEATHER_GLYPH_PARTLY:
            drawSunShape(cx + size / 5, cy - size / 5, size / 6, accent);
            drawCloudShape(cx - size / 10, cy + size / 8, cloudWidth, primary);
            break;
        case WEATHER_GLYPH_CLOUD:
            drawCloudShape(cx, cy, cloudWidth, primary);
            break;
        case WEATHER_GLYPH_FOG:
            drawCloudShape(cx, cy - size / 6, cloudWidth, primary);
            for (int i = 0; i < 3; ++i) {
                int y = cy + size / 5 + (i * 5);
                int inset = (i % 2) * 6;
                tft.drawFastHLine(cx - cloudWidth / 2 + inset, y, cloudWidth - (inset * 2), accent);
            }
            break;
        case WEATHER_GLYPH_RAIN:
            drawCloudShape(cx, cy - size / 6, cloudWidth, primary);
            for (int i = -1; i <= 1; ++i) {
                int x = cx + (i * size / 5);
                int y = cy + size / 5;
                tft.drawLine(x + 2, y, x - 2, y + 8, accent);
            }
            break;
        case WEATHER_GLYPH_SNOW:
            drawCloudShape(cx, cy - size / 6, cloudWidth, primary);
            for (int i = -1; i <= 1; ++i) {
                int x = cx + (i * size / 5);
                int y = cy + size / 4;
                tft.drawFastHLine(x - 3, y, 7, accent);
                tft.drawFastVLine(x, y - 3, 7, accent);
            }
            break;
        case WEATHER_GLYPH_STORM:
            drawCloudShape(cx, cy - size / 6, cloudWidth, primary);
            {
                int x = cx;
                int y = cy + size / 8;
                tft.fillTriangle(x + 4, y, x - 5, y + 11, x + 1, y + 11, accent);
                tft.fillTriangle(x - 1, y + 9, x + 5, y + 9, x - 3, y + 20, accent);
            }
            break;
        case WEATHER_GLYPH_UNKNOWN:
        default:
            tft.drawCircle(cx, cy, size / 4, primary);
            tft.drawCircle(cx, cy, size / 4 - 1, primary);
            break;
    }
}

void drawScreenChrome(const String &title) {
    const ThemePalette &theme = activeTheme();
    const int chromeX = 10;
    const int chromeY = 10;
    const int chromeWidth = 220;
    const int chromeHeight = 26;

    tft.fillScreen(theme.background);
    tft.fillRoundRect(chromeX, chromeY, chromeWidth, chromeHeight, 12, theme.surfaceAlt);
    tft.fillCircle(chromeX + 14, chromeY + (chromeHeight / 2), 4, theme.accent);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_LABEL);
    tft.setTextColor(theme.text, theme.surfaceAlt);
    tft.drawString(title, chromeX + 26, chromeY + 5, FONT_LABEL);

    String subtitle = headerSubtitle();
    if (!subtitle.isEmpty()) {
        int chipWidth = min(94, tft.textWidth(subtitle, FONT_INFO) + 16);
        int chipHeight = 18;
        int chipX = chromeX + chromeWidth - chipWidth - 6;
        int chipY = chromeY + 4;

        tft.fillRoundRect(chipX, chipY, chipWidth, chipHeight, chipHeight / 2, theme.background);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.background);
        tft.drawString(subtitle, chipX + (chipWidth / 2), chipY + (chipHeight / 2), FONT_INFO);
    }
}

void drawPlaceholder(const String &title, const String &message) {
    const ThemePalette &theme = activeTheme();
    drawScreenChrome(title);

    drawRoundedPanel(10, 42, 220, 178, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TC_DATUM);
    tft.fillRoundRect(98, 68, 44, 44, 22, theme.accentSoft);
    tft.setTextColor(theme.accent, theme.accentSoft);
    tft.setTextFont(FONT_TITLE);
    tft.drawString("?", 120, 78, FONT_TITLE);

    tft.setTextColor(theme.text, theme.surface);
    tft.setTextFont(FONT_BODY);
    tft.drawString("Nothing here yet", 120, 128, FONT_BODY);
    drawWrappedCenteredText(message, 120, 154, 184, FONT_INFO, theme.muted, theme.surface, 2);
}

const WeatherData* effectiveWeatherData() {
    return feedsWeatherSource() == WEATHER_FEED_OPEN_METEO ? feedsWeatherData() : nullptr;
}

bool weatherWaitingForSync() {
    return feedsWeatherSource() == WEATHER_FEED_OPEN_METEO &&
           feedsWeatherConfigured() &&
           !feedsHasWeatherData();
}

char weatherUnitSymbol() {
    return feedsWeatherUsesFahrenheit() ? 'F' : 'C';
}

// Distance from the top of a built-in font's box down to its ink baseline, taken from the
// TFT_eSPI font headers. Hardcoded rather than read from the library's own fontdata table:
// that table is defined in the header, so referencing it from this file instantiates a
// second copy and drags the glyph tables in with it (measured at +12.4 KB of flash).
int fontBaseline(int font) {
    switch (font) {
        case FONT_LABEL: return 13;  // Font16,    16 px tall
        case FONT_BODY:  return 19;  // Font32rle, 26 px tall
        case FONT_TITLE: return 36;  // Font64rle, 48 px tall
        case FONT_HUGE:  return 47;  // Font7srle, 48 px tall
        default:         return 7;   // GLCD,       8 px tall
    }
}

int temperatureClusterWidth(int value, int numberFont, int unitFont, int ringRadius) {
    return tft.textWidth(String(value), numberFont) + (ringRadius * 2) + 4 +
           tft.textWidth(String(weatherUnitSymbol()), unitFont);
}

// Draws "25" then a degree ring then "C", left-anchored at leftX. The ring is drawn rather
// than printed because the built-in fonts stop at ASCII 127, and the letter follows the
// feed's unit setting so a Fahrenheit device reads "77F".
void drawTemperatureCluster(int value, int leftX, int topY, int numberFont, int unitFont,
                            int ringRadius, uint16_t numberColor, uint16_t unitColor,
                            uint16_t background) {
    String number = String(value);
    String unit = String(weatherUnitSymbol());
    drawPaddedText(number, leftX, topY, TL_DATUM, numberFont, 0, numberColor, background);
    int ringX = leftX + tft.textWidth(number, numberFont) + ringRadius + 2;
    drawDegreeRing(ringX, topY + ringRadius + 2, ringRadius, unitColor, background);
    // Sit the unit letter on the number's own baseline. A TL_DATUM draw positions text by
    // the top of its box, and the built-in fonts leave different amounts of descender room
    // below the baseline (font 2: 16 tall / 13 baseline, font 4: 26/19, font 6: 48/36,
    // font 7: 48/47), so matching box tops puts the letter visibly off the line.
    int unitTop = topY + fontBaseline(numberFont) - fontBaseline(unitFont);
    drawPaddedText(unit, ringX + ringRadius + 2, unitTop, TL_DATUM, unitFont, 0,
                   unitColor, background);
}

const MarketData* effectiveMarketData(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return nullptr;
    }

    uint8_t source = feedsMarketSource(index);
    return (source == MARKET_FEED_FINNHUB || source == MARKET_FEED_COINGECKO)
               ? feedsMarketData(index)
               : nullptr;
}

String marketCurrencyPrefix(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return "$";
    }

    String currency = safeCString(feedConfig.markets[index].currency);
    currency.trim();
    currency.toLowerCase();
    if (currency.length() == 0) {
        currency = "usd";
    }

    if (currency == "usd") {
        return "$";
    }
    if (currency == "cad") {
        return "CA$";
    }
    if (currency == "aud") {
        return "AU$";
    }
    if (currency == "nzd") {
        return "NZ$";
    }
    if (currency == "sgd") {
        return "SG$";
    }
    if (currency == "hkd") {
        return "HK$";
    }

    currency.toUpperCase();
    return currency + " ";
}

String formatMarketPriceValue(float price) {
    float absolutePrice = price >= 0.0f ? price : -price;
    uint8_t precision = 2;

    if (absolutePrice >= 1000.0f) {
        precision = 0;
    } else if (absolutePrice >= 100.0f) {
        precision = 1;
    } else if (absolutePrice >= 1.0f) {
        precision = 2;
    } else if (absolutePrice >= 0.1f) {
        precision = 3;
    } else {
        precision = 4;
    }

    return groupThousands(trimTrailingZeros(price, precision));
}

String formatMarketPrice(uint8_t index, float price) {
    return marketCurrencyPrefix(index) + formatMarketPriceValue(price);
}

String marketChangeWindowLabel(uint8_t index) {
    return feedsMarketSource(index) == MARKET_FEED_COINGECKO ? "24h" : "1d";
}

String formatMarketChangeSummary(uint8_t index, float changePercent) {
    float absoluteChange = changePercent >= 0.0f ? changePercent : -changePercent;
    uint8_t precision = absoluteChange >= 10.0f ? 1 : 2;
    return marketChangeWindowLabel(index) +
           " " +
           (changePercent >= 0.0f ? "+" : "") +
           trimTrailingZeros(changePercent, precision) +
           "%";
}

bool marketWaitingForSync(uint8_t index) {
    uint8_t source = feedsMarketSource(index);
    return (source == MARKET_FEED_FINNHUB || source == MARKET_FEED_COINGECKO) &&
           feedsMarketConfigured(index) &&
           !feedsHasMarketData(index);
}

bool anyMarketsWaitingForSync() {
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (marketWaitingForSync(index)) {
            return true;
        }
    }

    return false;
}

String homeAssistantSlotLabel(uint8_t index) {
    if (index >= HOME_ASSISTANT_SLOT_COUNT) {
        return "Entity";
    }

    const HomeAssistantSlotConfig &slot = feedConfig.homeAssistant.slots[index];
    if (slot.label[0] != '\0') {
        return safeCString(slot.label);
    }

    const HomeAssistantSlotData *runtimeSlot = feedsHomeAssistantSlotData(index);
    if (runtimeSlot != nullptr && runtimeSlot->label[0] != '\0') {
        return runtimeSlot->label;
    }

    String entityId = safeCString(slot.entityId);
    int separatorIndex = entityId.lastIndexOf('.');
    if (separatorIndex >= 0 && separatorIndex < static_cast<int>(entityId.length()) - 1) {
        entityId = entityId.substring(separatorIndex + 1);
    }
    entityId.replace("_", " ");
    if (entityId.length() == 0) {
        return "Entity";
    }

    bool capitalizeNext = true;
    for (size_t charIndex = 0; charIndex < entityId.length(); ++charIndex) {
        char character = entityId.charAt(charIndex);
        if (capitalizeNext && character >= 'a' && character <= 'z') {
            entityId.setCharAt(charIndex, static_cast<char>(toupper(character)));
            capitalizeNext = false;
        } else if (character == ' ') {
            capitalizeNext = true;
        } else {
            capitalizeNext = false;
        }
    }

    return entityId;
}

String homeAssistantSlotUnit(uint8_t index) {
    if (index >= HOME_ASSISTANT_SLOT_COUNT) {
        return "";
    }

    const HomeAssistantSlotConfig &slot = feedConfig.homeAssistant.slots[index];
    if (slot.unit[0] != '\0') {
        return safeCString(slot.unit);
    }

    const HomeAssistantSlotData *runtimeSlot = feedsHomeAssistantSlotData(index);
    return runtimeSlot != nullptr ? safeCString(runtimeSlot->unit) : String("");
}

bool homeAssistantSlotConfigured(uint8_t index) {
    return index < HOME_ASSISTANT_SLOT_COUNT &&
           feedConfig.homeAssistant.slots[index].enabled &&
           feedConfig.homeAssistant.slots[index].entityId[0] != '\0';
}

bool homeAssistantWaitingForSync() {
    return feedsHomeAssistantConfigured() && !feedsHasHomeAssistantData();
}

// --- photo slideshow --------------------------------------------------------------
// Scanning LittleFS is far too slow to do on every hash, and uploads are rare, so the
// count is cached with a short TTL and invalidated outright when a file is added or
// removed. The path for the current index is resolved only when the index moves.
uint16_t cachedPhotoCount = 0;
unsigned long photoCountCheckedMs = 0;
bool photoCountValid = false;
uint16_t photoIndex = 0;
unsigned long lastPhotoAdvanceMs = 0;
char currentPhotoPath[DISPLAY_PATH_BUFFER_SIZE] = {0};
constexpr unsigned long kPhotoCountTtlMs = 5000;

// IMAGE_DIR carries a trailing slash for building file paths; openDir() wants it without.
String photoDirPath() {
    String dir = IMAGE_DIR;
    while (dir.length() > 1 && dir.endsWith("/")) {
        dir.remove(dir.length() - 1);
    }
    return dir;
}

bool isPhotoFile(const String &name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

uint16_t photoCount() {
    if (photoCountValid && (millis() - photoCountCheckedMs) < kPhotoCountTtlMs) {
        return cachedPhotoCount;
    }
    uint16_t count = 0;
    Dir dir = LittleFS.openDir(photoDirPath());
    while (dir.next()) {
        if (dir.isFile() && isPhotoFile(dir.fileName())) {
            ++count;
        }
    }
    cachedPhotoCount = count;
    photoCountCheckedMs = millis();
    photoCountValid = true;
    return count;
}

// Resolves the index-th photo into currentPhotoPath. Cheap enough at one call per
// slideshow step; never called from the hash path.
bool resolvePhotoPath(uint16_t index) {
    uint16_t seen = 0;
    Dir dir = LittleFS.openDir(photoDirPath());
    while (dir.next()) {
        if (!dir.isFile() || !isPhotoFile(dir.fileName())) {
            continue;
        }
        if (seen == index) {
            String full = String(IMAGE_DIR) + dir.fileName();
            strncpy(currentPhotoPath, full.c_str(), sizeof(currentPhotoPath) - 1);
            currentPhotoPath[sizeof(currentPhotoPath) - 1] = '\0';
            return true;
        }
        ++seen;
    }
    currentPhotoPath[0] = '\0';
    return false;
}

void selectPhoto(uint16_t index) {
    photoIndex = index;
    resolvePhotoPath(photoIndex);
}

bool hasPhotoContent() {
    return photoCount() > 0;
}

// Runs from displayUpdate(), so it ticks at the display rate rather than needing its own
// timer in the main loop. Only the photos page advances, and only when no single image is
// pinned over the top of it.
void maybeAdvancePhoto() {
    uint16_t count = photoCount();

    // Keep the index and path consistent with the filesystem on EVERY tick, whatever page
    // is up. renderDashboardPageCached() hashes currentPhotoPath before it renders and
    // stores that hash after, so resolving the path inside the renderer would cache a
    // value the screen never showed and cost a second JPEG decode on the next tick.
    if (count == 0) {
        photoIndex = 0;
        currentPhotoPath[0] = '\0';
        return;
    }
    if (photoIndex >= count) {
        photoIndex = 0;
        currentPhotoPath[0] = '\0';
    }
    if (currentPhotoPath[0] == '\0') {
        selectPhoto(photoIndex);
        lastPhotoAdvanceMs = millis();
    }

    // Only the photos page steps, and not while a single image is pinned over it.
    if (displayState.currentPage != DASHBOARD_PAGE_PHOTOS || displayState.showImage ||
        displayState.apMode || count < 2) {
        return;
    }
    unsigned long intervalMs = static_cast<unsigned long>(dashboardConfig.photoIntervalSec) * 1000UL;
    if (millis() - lastPhotoAdvanceMs < intervalMs) {
        return;
    }
    lastPhotoAdvanceMs = millis();

    uint16_t next = photoIndex;
    if (dashboardConfig.photoShuffle) {
        // count >= 2 here, so this always terminates.
        while (next == photoIndex) {
            next = static_cast<uint16_t>(random(count));
        }
    } else {
        next = (photoIndex + 1) % count;
    }
    selectPhoto(next);
}

bool hasWeatherContent() {
    const WeatherData *weather = effectiveWeatherData();
    return weather != nullptr &&
           (weather->location[0] != '\0' ||
            weather->condition[0] != '\0' ||
            feedsHasWeatherData());
}

bool hasFocusContent() {
    return dashboardData.focus.running ||
           dashboardData.focus.breakMode ||
           dashboardData.focus.label[0] != '\0' ||
           dashboardData.focus.remainingSeconds != static_cast<uint32_t>(dashboardData.focus.durationMinutes) * 60UL;
}

const char* activeClockMessage() {
    if (displayState.line2[0] == '\0') {
        return nullptr;
    }

    if (strcmp(displayState.line2, "Dashboard preview ready") == 0) {
        return nullptr;
    }

    return displayState.line2;
}

bool hasMarketContent() {
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        const MarketData *market = effectiveMarketData(index);
        if (market != nullptr &&
            market->symbol[0] != '\0' &&
            market->enabled) {
            return true;
        }
    }

    return false;
}

bool hasHomeAssistantContent() {
    if (!feedsHomeAssistantConfigured()) {
        return false;
    }

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (homeAssistantSlotConfigured(index)) {
            return true;
        }
    }

    return false;
}

bool hasWorldClockContent() {
    for (uint8_t index = 0; index < DASHBOARD_WORLD_CLOCK_COUNT; ++index) {
        if (dashboardData.worldClocks[index].enabled && dashboardData.worldClocks[index].label[0] != '\0') {
            return true;
        }
    }

    return false;
}

bool hasEventContent() {
    return dashboardData.event.title[0] != '\0' || dashboardData.event.subtitle[0] != '\0' || dashboardData.event.remainingSeconds > 0;
}

bool hasQuoteContent() {
    return dashboardData.quote.text[0] != '\0';
}

bool hasStatusContent() {
    return dashboardData.status.line1[0] != '\0' || dashboardData.status.line2[0] != '\0';
}

bool dashboardPageHasRenderableContent(uint8_t pageId) {
    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            return true;
        case DASHBOARD_PAGE_PHOTOS:
            return hasPhotoContent();
        case DASHBOARD_PAGE_SPOTIFY:
            return spotifyPageAvailable();
        case DASHBOARD_PAGE_MARKETS:
            return hasMarketContent() || anyMarketsWaitingForSync();
        case DASHBOARD_PAGE_HOME:
            return hasHomeAssistantContent() || homeAssistantWaitingForSync();
        case DASHBOARD_PAGE_FOCUS:
            return hasFocusContent();
        case DASHBOARD_PAGE_WORLD:
            return hasWorldClockContent();
        case DASHBOARD_PAGE_EVENT:
            return hasEventContent();
        case DASHBOARD_PAGE_QUOTE:
            return hasQuoteContent();
        case DASHBOARD_PAGE_STATUS:
            return hasStatusContent();
        default:
            return false;
    }
}

bool dashboardPageAvailable(uint8_t pageId) {
    return dashboardPageEnabled(pageId) && dashboardPageHasRenderableContent(pageId);
}

uint8_t firstAvailableDashboardPage() {
    for (uint8_t pageId = 0; pageId < DASHBOARD_PAGE_COUNT; ++pageId) {
        if (dashboardPageAvailable(pageId)) {
            return pageId;
        }
    }

    return dashboardFirstEnabledPage();
}

uint8_t nextAvailableDashboardPage(uint8_t currentPage) {
    for (uint8_t offset = 1; offset <= DASHBOARD_PAGE_COUNT; ++offset) {
        uint8_t pageId = (currentPage + offset) % DASHBOARD_PAGE_COUNT;
        if (dashboardPageAvailable(pageId)) {
            return pageId;
        }
    }

    if (dashboardPageEnabled(currentPage)) {
        return currentPage;
    }

    return dashboardFirstEnabledPage();
}

// Weather older than two refreshes is still shown - a reading from an hour ago beats a
// blank panel - but it stops claiming to be current. The condition line carries the age,
// because that is the field a stale reading misleads with most.
bool weatherIsStale() {
    long age = feedsWeatherAgeSeconds();
    return age >= 0 && age > feedsWeatherStaleAfterSeconds();
}

// The age goes at the FRONT. drawAdaptiveText clips from the end, so an appended age is
// the first thing cut on the narrow faces - which is precisely the part worth keeping.
// Whole hours, never minutes: the line is hashed into the static half, so minute
// granularity would force a full-screen redraw sixty times an hour to repaint pixels
// that usually clip to the same thing anyway.
String weatherConditionLine(const WeatherData *weather) {
    String condition = (weather != nullptr && weather->condition[0] != '\0')
                           ? String(weather->condition)
                           : String("Ready");
    if (!weatherIsStale()) {
        return condition;
    }
    long ageHours = feedsWeatherAgeSeconds() / 3600;
    String age = (ageHours >= 48) ? (String(ageHours / 24) + "d ago") : (String(ageHours) + "h ago");
    return age + "  " + condition;
}

// True when the local clock is outside sunrise..sunset, so icons switch to moon variants.
bool weatherIsNight(const WeatherData *weather, int nowMinutes) {
    if (weather == nullptr || nowMinutes < 0 ||
        weather->sunriseMinutes < 0 || weather->sunsetMinutes < 0) {
        return false;
    }
    return nowMinutes < weather->sunriseMinutes || nowMinutes >= weather->sunsetMinutes;
}

int localMinutesOfDay() {
    if (!hasTimeSync()) {
        return -1;
    }
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    return (local.tm_hour * 60) + local.tm_min;
}

// Day/night state every face needs before it can pick an icon.
struct WeatherContext {
    bool isNight;
    bool nearSunEvent;
    int nextSunEventMinutes;  // -1 when sunrise/sunset are unknown
};

WeatherContext weatherContextFor(const WeatherData *weather) {
    WeatherContext context = {false, false, -1};
    if (weather == nullptr) {
        return context;
    }
    int nowMinutes = localMinutesOfDay();
    context.isNight = weatherIsNight(weather, nowMinutes);
    context.nearSunEvent = nowMinutes >= 0 && weather->sunriseMinutes >= 0 &&
                           (abs(nowMinutes - weather->sunriseMinutes) <= 30 ||
                            (weather->sunsetMinutes >= 0 &&
                             abs(nowMinutes - weather->sunsetMinutes) <= 30));
    context.nextSunEventMinutes = context.isNight ? weather->sunriseMinutes : weather->sunsetMinutes;
    return context;
}

void hashWeatherData(uint32_t &hash) {
    hashValue(hash, feedsWeatherSource());
    hashValue(hash, feedsWeatherUsesFahrenheit());
    hashValue(hash, weatherWaitingForSync());

    const WeatherData *weather = effectiveWeatherData();
    bool hasData = weather != nullptr;
    hashValue(hash, hasData);
    if (!hasData) {
        return;
    }

    hashCString(hash, weather->location);
    hashCString(hash, weather->condition);
    hashValue(hash, weather->temperature);
    hashValue(hash, weather->high);
    hashValue(hash, weather->low);
    hashValue(hash, weather->rainChance);
    hashValue(hash, weather->weatherCode);
    hashValue(hash, weather->humidity);
    hashValue(hash, weather->pressure);
    hashValue(hash, weather->sunriseMinutes);
    hashValue(hash, weather->sunsetMinutes);
}

// The clock page draws two things in its static half that follow the wall clock rather
// than config or weather: the matrix face's NTP tape, and the sun/moon icon that flips
// when the current time crosses sunrise or sunset. Without these the static half never
// repaints them - the tape stays red all day and the icon never becomes a moon.
void hashClockTimeDerivedState(uint32_t &hash) {
    hashValue(hash, hasTimeSync());
    // Hash the rendered age text, not the raw age: it only changes when the pixels would.
    hashCString(hash, weatherConditionLine(effectiveWeatherData()).c_str());
    WeatherContext context = weatherContextFor(effectiveWeatherData());
    hashValue(hash, context.isNight);
    hashValue(hash, context.nearSunEvent);
    hashValue(hash, context.nextSunEventMinutes);
}

void hashMarketData(uint32_t &hash) {
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        hashValue(hash, feedsMarketSource(index));
        hashValue(hash, marketWaitingForSync(index));
        hashCString(hash, feedConfig.markets[index].currency);

        const MarketData *market = effectiveMarketData(index);
        bool hasData = market != nullptr;
        hashValue(hash, hasData);
        if (!hasData) {
            continue;
        }

        hashValue(hash, market->enabled);
        hashCString(hash, market->symbol);
        hashCString(hash, market->label);
        hashValue(hash, market->price);
        hashValue(hash, market->change);
        hashValue(hash, market->changePercent);
    }
}

void hashHomeAssistantData(uint32_t &hash) {
    hashValue(hash, feedsHomeAssistantConfigured());
    hashValue(hash, feedsHasHomeAssistantData());
    hashValue(hash, homeAssistantWaitingForSync());
    hashCString(hash, feedConfig.homeAssistant.baseUrl);
    hashValue(hash, feedConfig.homeAssistant.refreshMinutes);

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        const HomeAssistantSlotConfig &slotConfig = feedConfig.homeAssistant.slots[index];
        hashValue(hash, slotConfig.enabled);
        hashCString(hash, slotConfig.entityId);
        hashCString(hash, slotConfig.label);
        hashCString(hash, slotConfig.unit);

        const HomeAssistantSlotData *slot = feedsHomeAssistantSlotData(index);
        bool hasData = slot != nullptr;
        hashValue(hash, hasData);
        if (!hasData) {
            continue;
        }

        hashValue(hash, slot->numeric);
        hashCString(hash, slot->label);
        hashCString(hash, slot->state);
        hashCString(hash, slot->unit);
    }
}

void hashWorldClockData(uint32_t &hash) {
    for (uint8_t index = 0; index < DASHBOARD_WORLD_CLOCK_COUNT; ++index) {
        const WorldClockData &clock = dashboardData.worldClocks[index];
        hashValue(hash, clock.enabled);
        hashCString(hash, clock.label);
        hashValue(hash, clock.offsetSeconds);
    }
}

void hashCommonPageState(uint32_t &hash, uint8_t pageId) {
    hashValue(hash, pageId);
    hashThemeConfig(hash);
    hashValue(hash, dashboardConfig.showIp);
    hashCString(hash, displayState.ipInfo);
}

uint32_t dashboardStaticHash(uint8_t pageId) {
    uint32_t hash = kFnvOffset;
    hashCommonPageState(hash, pageId);

    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            hashValue(hash, dashboardConfig.use24Hour);
            hashValue(hash, dashboardConfig.showSeconds);
            // A different face or a new colour is a whole new layout, so force a full redraw.
            hashValue(hash, dashboardConfig.clockFace);
            hashCString(hash, dashboardConfig.clockHourColor);
            hashCString(hash, dashboardConfig.clockMinuteColor);
            hashCString(hash, dashboardConfig.clockSecondColor);
            hashClockTimeDerivedState(hash);
            hashValue(hash, hasWeatherContent());
            hashValue(hash, weatherWaitingForSync());
            hashValue(hash, activeClockMessage() != nullptr);
            if (hasWeatherContent()) {
                hashWeatherData(hash);
            } else if (activeClockMessage() != nullptr) {
                hashCString(hash, activeClockMessage());
            }
            break;
        case DASHBOARD_PAGE_PHOTOS:
            hashCString(hash, currentPhotoPath);
            break;
        case DASHBOARD_PAGE_SPOTIFY:
            hashCString(hash, spotifyRuntime.trackName);
            hashCString(hash, spotifyRuntime.artistName);
            hashValue(hash, spotifyRuntime.playing);
            hashValue(hash, spotifyRuntime.artReady);
            // Everything else the static half paints. Anything drawn but unhashed stays
            // on screen after it stops being true - the bug that left "NO TIME" on the
            // Matrix face after NTP synced.
            hashValue(hash, dashboardConfig.spotifyFace);  // switching face = full redraw
            hashCString(hash, spotifyRuntime.deviceName);
            hashCString(hash, spotifyRuntime.repeatMode);
            hashValue(hash, spotifyRuntime.shuffle);
            hashCString(hash, spotifyRuntime.lastError);
            break;
        case DASHBOARD_PAGE_MARKETS:
            hashMarketData(hash);
            break;
        case DASHBOARD_PAGE_HOME:
            hashHomeAssistantData(hash);
            break;
        case DASHBOARD_PAGE_FOCUS:
            hashCString(hash, dashboardData.focus.label);
            hashValue(hash, dashboardData.focus.running);
            hashValue(hash, dashboardData.focus.breakMode);
            hashValue(hash, dashboardData.focus.durationMinutes);
            break;
        case DASHBOARD_PAGE_WORLD:
            hashValue(hash, dashboardConfig.use24Hour);
            hashWorldClockData(hash);
            break;
        case DASHBOARD_PAGE_EVENT:
            hashCString(hash, dashboardData.event.title);
            hashCString(hash, dashboardData.event.subtitle);
            break;
        case DASHBOARD_PAGE_QUOTE:
            hashCString(hash, dashboardData.quote.text);
            hashCString(hash, dashboardData.quote.author);
            break;
        case DASHBOARD_PAGE_STATUS:
            hashCString(hash, dashboardData.status.line1);
            hashCString(hash, dashboardData.status.line2);
            break;
        default:
            break;
    }

    return hash;
}

uint32_t dashboardDynamicHash(uint8_t pageId) {
    uint32_t hash = kFnvOffset;
    hashValue(hash, pageId);

    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK: {
            bool isPm = false;
            String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
            String metaLine = formatClockMetaLine(isPm);
            hashCString(hash, timeText.c_str());
            hashCString(hash, metaLine.c_str());
            hashValue(hash, isPm);
            hashValue(hash, clockColonVisible());  // drives the 1 Hz colon blink
            break;
        }
        case DASHBOARD_PAGE_SPOTIFY: {
            // Hash what is rendered, not raw milliseconds: the clock text has one-second
            // resolution and the bar one-pixel, so hashing millis() would repaint at every
            // 500 ms tick for no visible change.
            uint32_t progress = spotifyProgressMs();
            hashValue(hash, progress / 1000UL);
            int filled = spotifyRuntime.durationMs > 0
                             ? static_cast<int>((static_cast<uint64_t>(progress) * kBarW) /
                                                spotifyRuntime.durationMs)
                             : 0;
            hashValue(hash, filled);
            hashValue(hash, spotifyRuntime.playing);
            hashValue(hash, spotifyRuntime.volumePercent);
            // Telemetry at the resolution it is drawn - 5 dB buckets and whole KB - or the
            // strip repaints on every stray RSSI wobble.
            hashValue(hash, static_cast<int>(WiFi.RSSI()) / 5);
            hashValue(hash, static_cast<int>(spotifyRuntime.lastPollDramBytes / 1024U));
            hashValue(hash, static_cast<int>(spotifyRuntime.lastPollIramBytes / 1024U));
            hashValue(hash, spotifyRuntime.lastPollLatencyMs);
            break;
        }
        case DASHBOARD_PAGE_FOCUS: {
            int32_t remainingSeconds = dashboardFocusRemainingSeconds();
            hashValue(hash, remainingSeconds);
            break;
        }
        case DASHBOARD_PAGE_WORLD: {
            for (uint8_t index = 0; index < DASHBOARD_WORLD_CLOCK_COUNT; ++index) {
                const WorldClockData &clock = dashboardData.worldClocks[index];
                if (!clock.enabled || clock.label[0] == '\0') {
                    continue;
                }

                String timeText = formatWorldTime(clock.offsetSeconds);
                hashCString(hash, timeText.c_str());
            }
            break;
        }
        case DASHBOARD_PAGE_EVENT: {
            String countdown = formatRelativeCountdown(dashboardEventRemainingSeconds());
            hashCString(hash, countdown.c_str());
            break;
        }
        default:
            break;
    }

    return hash;
}

uint32_t apScreenHash() {
    uint32_t hash = kFnvOffset;
    hashThemeConfig(hash);
    hashCString(hash, displayState.ipInfo);
    hashCString(hash, displayState.apSSID);
    hashCString(hash, displayState.apPassword);
    hashValue(hash, authCanRevealPassword());
    hashCString(hash, authProvisionedPassword());
    return hash;
}

uint32_t temporaryMessageHash() {
    uint32_t hash = kFnvOffset;
    hashThemeConfig(hash);
    hashValue(hash, displayState.apMode);
    hashValue(hash, dashboardConfig.showIp);
    hashCString(hash, displayState.ipInfo);
    hashCString(hash, temporaryMessage.message);
    return hash;
}

bool pageUsesDynamicRefresh(uint8_t pageId) {
    return pageId == DASHBOARD_PAGE_SPOTIFY ||
           pageId == DASHBOARD_PAGE_CLOCK ||
           pageId == DASHBOARD_PAGE_FOCUS ||
           pageId == DASHBOARD_PAGE_WORLD ||
           pageId == DASHBOARD_PAGE_EVENT;
}

// --- per-face dynamic halves -------------------------------------------------
// Each one redraws only what changes on the second tick. Every draw is padded, so
// nothing is cleared first and nothing blinks.

void updateCardsDynamicArea() {
    const ThemePalette &theme = activeTheme();
    bool isPm = false;
    String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
    ClockParts parts = splitClockText(timeText);

    // With seconds beside it the block is pinned left of them; without, it is centred.
    int rightEdge = parts.seconds.length() > 0 ? kClockSecondsX - kClockSecondsGap
                                               : (DISPLAY_WIDTH + bigTimeWidth()) / 2;
    drawBigTime(parts, rightEdge, kClockTimeY, theme.surface);

    if (parts.seconds.length() > 0) {
        tft.setTextFont(FONT_HUGE);
        int bigHeight = tft.fontHeight();
        tft.setTextFont(FONT_BODY);
        int smallHeight = tft.fontHeight();
        drawPaddedText(parts.seconds, kClockSecondsX, kClockTimeY + bigHeight - smallHeight,
                       TL_DATUM, FONT_BODY,
                       kClockDynamicRegionX + kClockDynamicRegionWidth - kClockSecondsX,
                       activeClockColors().second, theme.surface);
    }

    String metaLine = formatClockMetaLine(isPm);
    if (!clockMetaCache.valid || strcmp(clockMetaCache.metaLine, metaLine.c_str()) != 0) {
        drawPaddedText(metaLine, DISPLAY_WIDTH / 2, kClockMetaY, TC_DATUM, FONT_BODY,
                       kClockDynamicRegionWidth, theme.muted, theme.surface);
        updateClockMetaCache(metaLine);
    }
}

void updateBoldDynamicArea() {
    const ThemePalette &theme = activeTheme();
    bool isPm = false;
    String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
    ClockParts parts = splitClockText(timeText);

    drawBigTime(parts, (DISPLAY_WIDTH + bigTimeWidth()) / 2, kBoldTimeY, theme.background);

    // Meta row under the digits: date on the left, seconds on the right.
    if (parts.seconds.length() > 0) {
        drawPaddedText(":" + parts.seconds, 230, kBoldMetaY, TR_DATUM, FONT_LABEL, 58,
                       activeClockColors().second, theme.background);
    }

    String metaLine = formatClockMetaLine(isPm);
    if (!clockMetaCache.valid || strcmp(clockMetaCache.metaLine, metaLine.c_str()) != 0) {
        drawPaddedText(metaLine, 10, kBoldMetaY, TL_DATUM, FONT_LABEL, 150,
                       theme.muted, theme.background);
        updateClockMetaCache(metaLine);
    }
}

void updateMatrixDynamicArea() {
    const ThemePalette &theme = activeTheme();
    bool isPm = false;
    String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
    ClockParts parts = splitClockText(timeText);

    drawBigTime(parts, kMatrixTimeRight, kMatrixTimeY, theme.surface);

    drawPaddedText(dashboardConfig.use24Hour ? "24H" : (isPm ? "PM" : "AM"),
                   kMatrixSideX, 30, TL_DATUM, FONT_LABEL, 44, theme.warning, theme.surface);

    if (parts.seconds.length() > 0) {
        drawPaddedText(parts.seconds, kMatrixSideX + 26, kMatrixSecondsY, TL_DATUM, FONT_LABEL,
                       36, activeClockColors().second, theme.surface);
        // Sweep bar: the minute filling up, so the strip reads as an instrument.
        drawLevelBar(kMatrixSideX, kMatrixSecondsBarY, 62, 3,
                     (parts.seconds.toInt() * 100) / 59,
                     activeClockColors().second, theme.surfaceAlt);
    }

    String metaLine = formatClockMetaLine(isPm);
    if (!clockMetaCache.valid || strcmp(clockMetaCache.metaLine, metaLine.c_str()) != 0) {
        drawPaddedText(metaLine, DISPLAY_WIDTH / 2, kMatrixTapeY, TC_DATUM, FONT_INFO, 118,
                       theme.muted, theme.surface);
        updateClockMetaCache(metaLine);
    }
}

void updateRadialDynamicArea() {
    const ThemePalette &theme = activeTheme();
    bool isPm = false;
    String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
    ClockParts parts = splitClockText(timeText);

    drawBigTime(parts, (DISPLAY_WIDTH + bigTimeWidth()) / 2, kRadialTimeY, theme.background);

    // The flanks between the two arcs are free, so the seconds live out there.
    if (parts.seconds.length() > 0) {
        drawPaddedText(parts.seconds, kRadialSideX, kRadialTimeY + 10, TL_DATUM, FONT_LABEL, 34,
                       activeClockColors().second, theme.background);
    }
    drawPaddedText(dashboardConfig.use24Hour ? "" : (isPm ? "PM" : "AM"),
                   kRadialSideX, kRadialTimeY + 32, TL_DATUM, FONT_INFO, 22,
                   theme.muted, theme.background);

    String dateLine = formatLocalDate();
    if (!clockMetaCache.valid || strcmp(clockMetaCache.metaLine, dateLine.c_str()) != 0) {
        drawPaddedText(dateLine, DISPLAY_WIDTH / 2, kRadialDateY, TC_DATUM, FONT_LABEL, 150,
                       theme.muted, theme.background);
        updateClockMetaCache(dateLine);
    }
}

void updateClockDynamicArea() {
    switch (dashboardConfig.clockFace) {
        case DASHBOARD_CLOCK_FACE_BOLD:
            updateBoldDynamicArea();
            break;
        case DASHBOARD_CLOCK_FACE_MATRIX:
            updateMatrixDynamicArea();
            break;
        case DASHBOARD_CLOCK_FACE_RADIAL:
            updateRadialDynamicArea();
            break;
        default:
            updateCardsDynamicArea();
            break;
    }
}

String formatTrackTime(uint32_t ms) {
    uint32_t totalSeconds = ms / 1000UL;
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%lu:%02lu",
             static_cast<unsigned long>(totalSeconds / 60UL),
             static_cast<unsigned long>(totalSeconds % 60UL));
    return String(buffer);
}

// Draws /art.jpg into the 80x80 slot. The scale is chosen from the file's own header
// rather than assumed: copyPreferredArtUrl() asks for <=300 px, but a podcast or a sparse
// album can return 64, and a hardcoded scale 4 would render that as a 16 px smudge.
void drawAlbumArt() {
    tft.fillRect(kArtX, kArtY, kArtBox, kArtBox, kSpPanel);
    if (!spotifyRuntime.artReady || !LittleFS.exists(SPOTIFY_ART_PATH)) {
        // No art yet: a green disc reads as "album" without shipping a bitmap.
        tft.fillCircle(kArtX + kArtBox / 2, kArtY + kArtBox / 2, 22, kSpSurface);
        tft.drawCircle(kArtX + kArtBox / 2, kArtY + kArtBox / 2, 22, kSpGreen);
        tft.fillCircle(kArtX + kArtBox / 2, kArtY + kArtBox / 2, 5, kSpGreen);
        return;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    if (TJpgDec.getFsJpgSize(&width, &height, SPOTIFY_ART_PATH, LittleFS) != JDR_OK || width == 0) {
        return;
    }
    uint8_t scale = 1;
    while (scale < 8 && (width / scale) > kArtBox) {
        scale *= 2;
    }
    TJpgDec.setJpgScale(scale);
    File artFile = LittleFS.open(SPOTIFY_ART_PATH, "r");
    if (artFile) {
        int drawn = width / scale;
        int offset = (kArtBox - drawn) / 2;  // centre whatever size we ended up with
        TJpgDec.drawFsJpg(kArtX + offset, kArtY + offset, artFile);
        artFile.close();
    }
    TJpgDec.setJpgScale(1);  // the photo slideshow renders full-screen and expects 1
}

// The mockup puts a spectrum analyser here. This device has no audio - it reports what is
// playing somewhere else - so bars would be invented data. These three numbers are real.
void drawSpotifyTelemetry(bool valuesOnly) {
    const int cellY = 152;
    const int cellH = 44;
    const int cellW = kBarW / 3;
    if (!valuesOnly) {
        drawRoundedPanel(kBarX, cellY, kBarW, cellH, kSpPanel, kSpPanel);
        const char *labels[3] = {"WIFI", "DRAM/IRAM", "POLL"};
        for (int i = 0; i < 3; ++i) {
            drawPaddedText(labels[i], kBarX + cellW * i + cellW / 2, cellY + 6, TC_DATUM,
                           FONT_INFO, 0, kSpMuted, kSpPanel);
        }
    }

    char value[3][14];
    snprintf(value[0], sizeof(value[0]), "%ddB", WiFi.RSSI());
    // DRAM/IRAM as they were during the last poll, not now: idle heap says nothing about
    // whether a poll fits, and the BearSSL receive buffer comes out of IRAM.
    snprintf(value[1], sizeof(value[1]), "%u/%uk",
             static_cast<unsigned>(spotifyRuntime.lastPollDramBytes / 1024U),
             static_cast<unsigned>(spotifyRuntime.lastPollIramBytes / 1024U));
    snprintf(value[2], sizeof(value[2]), "%ums", spotifyRuntime.lastPollLatencyMs);
    for (int i = 0; i < 3; ++i) {
        // FONT_INFO for the heap pair: two numbers and a slash do not fit the cell at
        // FONT_LABEL, and silently clipping the IRAM figure would defeat the point.
        int font = (i == 1) ? FONT_INFO : FONT_LABEL;
        int baselineShift = (i == 1) ? 4 : 0;
        drawPaddedText(value[i], kBarX + cellW * i + cellW / 2, cellY + 20 + baselineShift,
                       TC_DATUM, font, cellW - 8, kSpText, kSpPanel);
    }
}

void drawSpotifyProgress() {
    uint32_t progress = spotifyProgressMs();
    drawPaddedText(formatTrackTime(progress), kBarX, kBarY - 14, TL_DATUM, FONT_INFO, 48,
                   kSpGreen, kSpSurface);
    drawPaddedText(spotifyRuntime.durationMs > 0 ? formatTrackTime(spotifyRuntime.durationMs) : "--:--",
                   kBarX + kBarW, kBarY - 14, TR_DATUM, FONT_INFO, 48, kSpMuted, kSpSurface);

    String state = spotifyRuntime.playing ? "PLAYING" : "PAUSED";
    if (spotifyRuntime.volumePercent >= 0) {
        state += "  " + String(spotifyRuntime.volumePercent) + "%";
    }
    drawPaddedText(state, kBarX + kBarW / 2, kBarY - 14, TC_DATUM, FONT_INFO, 110,
                   spotifyRuntime.playing ? kSpText : kSpAmber, kSpSurface);

    tft.fillRoundRect(kBarX, kBarY, kBarW, 6, 3, kSpPanel);
    if (spotifyRuntime.durationMs > 0) {
        int filled = static_cast<int>((static_cast<uint64_t>(progress) * kBarW) /
                                      spotifyRuntime.durationMs);
        filled = constrain(filled, 0, kBarW);
        if (filled > 0) {
            tft.fillRoundRect(kBarX, kBarY, filled, 6, 3, kSpGreen);
        }
    }
}

// Defined further down with the rest of the Spotify faces; the dispatch lives up here
// beside the other per-page update functions.
void drawFace2Dynamic();
void renderSpotifyFaceText();
void renderSpotifyFaceArt();

void updateSpotifyDynamicArea() {
    if (dashboardConfig.spotifyFace == DASHBOARD_SPOTIFY_FACE_TEXT) {
        drawFace2Dynamic();
        return;
    }
    drawSpotifyProgress();
    drawSpotifyTelemetry(true);  // labels are static; only the numbers move
}

void renderSpotifyPage() {
    switch (dashboardConfig.spotifyFace) {
        case DASHBOARD_SPOTIFY_FACE_TEXT:
            renderSpotifyFaceText();
            break;
        default:
            renderSpotifyFaceArt();
            break;
    }
}

void updateFocusDynamicArea() {
    const ThemePalette &theme = activeTheme();
    int32_t remainingSeconds = dashboardFocusRemainingSeconds();

    tft.fillRect(20, 92, 200, 112, theme.surface);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_HUGE);
    tft.setTextColor(theme.text, theme.surface);
    tft.drawString(formatDuration(remainingSeconds), 120, 106, FONT_HUGE);

    int progressWidth = 184;
    int progressHeight = 8;
    int progressX = 28;
    int progressY = 178;
    tft.fillRoundRect(progressX, progressY, progressWidth, progressHeight, 4, theme.surfaceAlt);
    if (dashboardData.focus.durationMinutes > 0) {
        uint32_t totalSeconds = static_cast<uint32_t>(dashboardData.focus.durationMinutes) * 60UL;
        uint32_t boundedRemaining = remainingSeconds > 0 ? static_cast<uint32_t>(remainingSeconds) : 0U;
        uint32_t elapsedSeconds = (boundedRemaining < totalSeconds) ? (totalSeconds - boundedRemaining) : totalSeconds;
        uint32_t safeTotal = totalSeconds > 0 ? totalSeconds : 1U;
        int filledWidth = map(elapsedSeconds, 0, safeTotal, 0, progressWidth);
        filledWidth = constrain(filledWidth, 0, progressWidth);
        if (filledWidth > 0) {
            tft.fillRoundRect(progressX, progressY, filledWidth, progressHeight, 4, theme.accent);
        }
    }

    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString("Duration " + String(dashboardData.focus.durationMinutes) + " min", 120, 196, FONT_INFO);
}

void updateWorldDynamicArea() {
    const ThemePalette &theme = activeTheme();
    int y = 70;
    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        const WorldClockData &clock = dashboardData.worldClocks[clockIndex];
        if (!clock.enabled || clock.label[0] == '\0') {
            continue;
        }

        tft.fillRect(20, y, 118, 26, theme.surface);
        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(FONT_TITLE);
        tft.setTextColor(theme.text, theme.surface);
        tft.drawString(formatWorldTime(clock.offsetSeconds), 20, y, FONT_TITLE);
        y += 72;
    }
}

void updateEventDynamicArea() {
    const ThemePalette &theme = activeTheme();
    tft.fillRect(20, 96, 200, 52, theme.surface);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_HUGE);
    tft.setTextColor(theme.accent, theme.surface);
    tft.drawString(formatRelativeCountdown(dashboardEventRemainingSeconds()), 120, 100, FONT_HUGE);
}

// Slim header for the card face: an accent dot and the IP, no "Clock" title bar - you can
// see that it is a clock - which buys the time roughly 14 px of height.
void drawClockHeader() {
    const ThemePalette &theme = activeTheme();
    tft.fillScreen(theme.background);
    tft.fillCircle(16, 17, 4, theme.accent);

    String subtitle = headerSubtitle();
    if (!subtitle.isEmpty()) {
        tft.setTextDatum(TR_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.background);
        tft.drawString(subtitle, DISPLAY_WIDTH - 12, 11, FONT_INFO);
    }
}

// Whatever a face should show in place of the weather block when there is none yet.
// Returns false when there is nothing to say at all.
bool drawClockFallbackMessage(int centerX, int topY, int maxWidth, int font,
                              uint16_t background) {
    const ThemePalette &theme = activeTheme();
    if (weatherWaitingForSync()) {
        drawWrappedCenteredText("Syncing weather...", centerX, topY, maxWidth, font,
                                theme.muted, background, 4);
        return true;
    }
    const char *message = activeClockMessage();
    if (message != nullptr) {
        drawWrappedCenteredText(message, centerX, topY, maxWidth, font, theme.text, background, 4);
        return true;
    }
    return false;
}

// --- face: cards -------------------------------------------------------------
// A rounded time card over a rounded weather card.
void renderCardsFace() {
    const ThemePalette &theme = activeTheme();
    const int conditionFonts[] = {FONT_BODY, FONT_LABEL};
    const int labelFonts[] = {FONT_LABEL, FONT_INFO};

    drawClockHeader();
    drawRoundedPanel(8, kClockCardY, 224, kClockCardHeight, theme.surface, theme.surfaceAlt);

    if (!hasWeatherContent() && !weatherWaitingForSync() && activeClockMessage() == nullptr) {
        return;
    }

    drawRoundedPanel(8, kClockFooterY, 224, kClockFooterHeight, theme.surfaceAlt, theme.surfaceAlt);

    const int iconCenterX = 44;
    const int iconCenterY = kClockFooterY + 40;
    const int textLeft = 82;

    if (!hasWeatherContent()) {
        if (weatherWaitingForSync()) {
            drawWeatherIcon(iconCenterX, iconCenterY, 48, -1, theme.muted, theme.muted,
                            theme.surfaceAlt, false, false);
            drawWrappedCenteredText("Syncing weather...", 150, kClockFooterY + 34, 132,
                                    FONT_LABEL, theme.muted, theme.surfaceAlt, 2);
        } else {
            drawWrappedCenteredText(activeClockMessage(), 120, kClockFooterY + 30, 192,
                                    FONT_LABEL, theme.text, theme.surfaceAlt, 2);
        }
        return;
    }

    const WeatherData *weather = effectiveWeatherData();
    WeatherContext context = weatherContextFor(weather);
    drawWeatherIcon(iconCenterX, iconCenterY, 48, weather->weatherCode,
                    theme.text, theme.accent, theme.surfaceAlt, context.isNight, context.nearSunEvent);

    // Next sun event under the icon: a small up/down marker plus the time.
    if (context.nextSunEventMinutes >= 0) {
        int markerX = 24;
        int markerY = kClockFooterY + 76;
        drawTriangleMarker(markerX, markerY - 1, 4, context.isNight,
                           context.isNight ? theme.accent : theme.muted);
        char sunText[12];
        snprintf(sunText, sizeof(sunText), "%d:%02d",
                 context.nextSunEventMinutes / 60, context.nextSunEventMinutes % 60);
        drawPaddedText(sunText, markerX + 8, markerY - 4, TL_DATUM, FONT_INFO, 0,
                       theme.muted, theme.surfaceAlt);
    }

    // Pin the NUMBER's right edge, not the cluster's, so the ring and the unit letter land
    // in the same place whatever the temperature is.
    const int tempNumberRight = 202;
    const int tempTop = kClockFooterY + 6;
    int tempNumberWidth = tft.textWidth(String(weather->temperature), FONT_TITLE);
    drawTemperatureCluster(weather->temperature, tempNumberRight - tempNumberWidth, tempTop,
                           FONT_TITLE, FONT_LABEL, 5, theme.text, theme.accent, theme.surfaceAlt);

    // Only the top text row shares height with the digits, so it is the only one bounded by
    // where they start; the other two now sit below the digits' baseline and get full width.
    String location = weather->location[0] != '\0' ? weather->location : "Weather";
    drawAdaptiveText(location, textLeft, kClockFooterY + 8,
                     max(24, (tempNumberRight - tempNumberWidth) - textLeft - 6), TL_DATUM,
                     labelFonts, sizeof(labelFonts) / sizeof(labelFonts[0]),
                     theme.muted, theme.surfaceAlt);

    drawAdaptiveText(weatherConditionLine(weather), textLeft, kClockFooterY + 46, 140, TL_DATUM,
                     conditionFonts, sizeof(conditionFonts) / sizeof(conditionFonts[0]),
                     weatherIsStale() ? theme.warning : theme.text, theme.surfaceAlt);

    char summary[40];
    snprintf(summary, sizeof(summary), "H %d  L %d  Rain %d%%",
             weather->high, weather->low, weather->rainChance);
    drawAdaptiveText(summary, textLeft, kClockFooterY + 74, 140, TL_DATUM,
                     labelFonts, sizeof(labelFonts) / sizeof(labelFonts[0]),
                     theme.muted, theme.surfaceAlt);
}

// --- face: bold --------------------------------------------------------------
// No panels behind the time: a badge strip, the biggest digits the fonts allow, a
// temperature the same size, and two chips along the bottom.
void renderBoldFace() {
    const ThemePalette &theme = activeTheme();
    tft.fillScreen(theme.background);

    bool haveWeather = hasWeatherContent();
    const WeatherData *weather = haveWeather ? effectiveWeatherData() : nullptr;

    String badge = (haveWeather && weather->location[0] != '\0') ? String(weather->location)
                                                                 : String("CLOCK");
    badge.toUpperCase();
    int badgeWidth = min(92, tft.textWidth(badge, FONT_INFO) + 14);
    tft.fillRoundRect(8, 5, badgeWidth, 18, 4, theme.accent);
    const int badgeFonts[] = {FONT_INFO};
    drawAdaptiveText(badge, 8 + (badgeWidth / 2), 14, badgeWidth - 8, MC_DATUM,
                     badgeFonts, 1, theme.background, theme.accent);

    String subtitle = headerSubtitle();
    int subtitleWidth = 0;
    if (!subtitle.isEmpty()) {
        subtitleWidth = tft.textWidth(subtitle, FONT_INFO) + 10;
        drawPaddedText(subtitle, 232, 10, TR_DATUM, FONT_INFO, 0, theme.muted, theme.background);
    }

    if (haveWeather && weather->condition[0] != '\0') {
        String condition = weatherConditionLine(weather);
        condition.toUpperCase();
        int conditionLeft = 8 + badgeWidth + 8;
        int conditionWidth = (DISPLAY_WIDTH - 8 - subtitleWidth) - conditionLeft;
        if (conditionWidth > 20) {
            const int conditionFonts[] = {FONT_LABEL, FONT_INFO};
            drawAdaptiveText(condition, conditionLeft, 6, conditionWidth, TL_DATUM,
                             conditionFonts, sizeof(conditionFonts) / sizeof(conditionFonts[0]),
                             weatherIsStale() ? theme.warning : theme.accent, theme.background);
        }
    }

    tft.fillRect(8, kBoldRuleY, 224, 2, theme.accent);
    tft.drawFastHLine(8, 110, 224, theme.surfaceAlt);

    if (!haveWeather) {
        drawClockFallbackMessage(DISPLAY_WIDTH / 2, 150, 200, FONT_LABEL, theme.background);
        return;
    }

    WeatherContext context = weatherContextFor(weather);
    drawWeatherIcon(46, 142, 52, weather->weatherCode, theme.text, theme.accent,
                    theme.background, context.isNight, context.nearSunEvent);

    int tempWidth = temperatureClusterWidth(weather->temperature, FONT_HUGE, FONT_BODY, 6);
    drawTemperatureCluster(weather->temperature, 228 - tempWidth, 118, FONT_HUGE, FONT_BODY, 6,
                           theme.text, theme.accent, theme.background);

    // Left chip: rain chance with a segmented gauge.
    tft.fillRoundRect(8, 176, 110, 52, 6, theme.surface);
    drawPaddedText("RAIN", 16, 182, TL_DATUM, FONT_INFO, 0, theme.muted, theme.surface);
    drawPaddedText(String(weather->rainChance) + "%", 16, 192, TL_DATUM, FONT_BODY, 0,
                   theme.accent, theme.surface);
    drawSegmentBar(16, 220, 94, 4, weather->rainChance, 4, theme.accent, theme.surfaceAlt);

    // Right chip: today's high over today's low.
    tft.fillRoundRect(122, 176, 110, 52, 6, theme.surface);
    const int chipRows[2] = {181, 205};
    const int chipValues[2] = {weather->high, weather->low};
    const uint16_t chipColors[2] = {theme.warning, theme.accent};
    for (int row = 0; row < 2; ++row) {
        String value = String(chipValues[row]);
        drawTriangleMarker(134, chipRows[row] + 8, 5, row == 0, chipColors[row]);
        drawPaddedText(value, 146, chipRows[row], TL_DATUM, FONT_LABEL, 0, theme.text, theme.surface);
        drawDegreeRing(146 + tft.textWidth(value, FONT_LABEL) + 5, chipRows[row] + 4, 3,
                       theme.muted, theme.surface);
    }
}

// --- face: matrix ------------------------------------------------------------
// Instrument panel: a clock strip over bordered tiles, every value in its own well.
void renderMatrixFace() {
    const ThemePalette &theme = activeTheme();
    tft.fillScreen(theme.background);

    tft.fillRect(0, 0, DISPLAY_WIDTH, kMatrixZoneHeight - 2, theme.surface);
    tft.fillRect(0, kMatrixZoneHeight - 2, DISPLAY_WIDTH, 2, theme.accent);

    bool haveWeather = hasWeatherContent();
    const WeatherData *weather = haveWeather ? effectiveWeatherData() : nullptr;

    tft.fillRect(8, 6, 4, 4, theme.accent);
    String location = (haveWeather && weather->location[0] != '\0') ? String(weather->location)
                                                                    : String("CLOCK");
    location.toUpperCase();
    drawPaddedText(location, 18, 4, TL_DATUM, FONT_INFO, 0, theme.accent, theme.surface);

    String subtitle = headerSubtitle();
    if (!subtitle.isEmpty()) {
        drawPaddedText(subtitle, 232, 4, TR_DATUM, FONT_INFO, 0, theme.muted, theme.surface);
    }

    tft.drawFastVLine(kMatrixSideX - 10, 28, 52, theme.surfaceAlt);
    drawPaddedText("SEC", kMatrixSideX, kMatrixSecondsY + 4, TL_DATUM, FONT_INFO, 0,
                   theme.muted, theme.surface);

    bool synced = hasTimeSync();
    drawPaddedText(synced ? "NTP OK" : "NO TIME", 8, kMatrixTapeY, TL_DATUM, FONT_INFO, 0,
                   synced ? theme.positive : theme.negative, theme.surface);
    drawPaddedText(displayState.apMode ? "AP" : "STA", 232, kMatrixTapeY, TR_DATUM, FONT_INFO, 0,
                   theme.muted, theme.surface);

    if (!haveWeather) {
        drawClockFallbackMessage(DISPLAY_WIDTH / 2, 160, 200, FONT_LABEL, theme.background);
        return;
    }

    WeatherContext context = weatherContextFor(weather);

    // Row A: the icon in its own well, then temperature with the day's range beside it.
    drawTile(6, 120, 58, 64, theme.surfaceAlt, theme.accentSoft);
    drawWeatherIcon(35, 144, 40, weather->weatherCode, theme.text, theme.accent,
                    theme.surfaceAlt, context.isNight, context.nearSunEvent);
    char codeText[12];
    snprintf(codeText, sizeof(codeText), "WMO %d", weather->weatherCode);
    drawPaddedText(codeText, 35, 172, TC_DATUM, FONT_INFO, 0, theme.accent, theme.surfaceAlt);

    drawTile(70, 120, 164, 64, theme.surfaceAlt, theme.accentSoft);
    // Step the number down a size rather than let it reach the range divider: "-19" and
    // "100" are both wider than the 48 px face has room for here.
    const int tempClusterRight = 176;
    int tempNumberFont =
        (78 + temperatureClusterWidth(weather->temperature, FONT_TITLE, FONT_LABEL, 5)) > tempClusterRight
            ? FONT_BODY
            : FONT_TITLE;
    drawTemperatureCluster(weather->temperature, 78, 122, tempNumberFont, FONT_LABEL, 5,
                           theme.text, theme.accent, theme.surfaceAlt);

    const int conditionFonts[] = {FONT_INFO};
    drawAdaptiveText(weatherConditionLine(weather), 78, 172, 96, TL_DATUM, conditionFonts, 1,
                     weatherIsStale() ? theme.negative : theme.warning, theme.surfaceAlt);

    // Range column. Label and value share a centre line (ML/MR datums) so the 8 px label
    // does not ride high against the 16 px number, and each value gets its own degree ring.
    // The value is adaptive so a three-digit reading steps down instead of hitting the label.
    tft.drawFastVLine(180, 126, 52, theme.accentSoft);
    const int rangeValueFonts[] = {FONT_LABEL, FONT_INFO};
    const int rangeRowY[2] = {134, 164};
    const int rangeValues[2] = {weather->high, weather->low};
    const char *rangeLabels[2] = {"HI", "LO"};
    const uint16_t rangeColors[2] = {theme.negative, theme.accent};
    for (int row = 0; row < 2; ++row) {
        drawPaddedText(rangeLabels[row], 186, rangeRowY[row], ML_DATUM, FONT_INFO, 0,
                       rangeColors[row], theme.surfaceAlt);
        drawAdaptiveText(String(rangeValues[row]), 222, rangeRowY[row], 22, MR_DATUM,
                         rangeValueFonts, sizeof(rangeValueFonts) / sizeof(rangeValueFonts[0]),
                         theme.text, theme.surfaceAlt);
        drawDegreeRing(228, rangeRowY[row] - 4, 3, theme.muted, theme.surfaceAlt);
    }

    // Row B: three sensor wells.
    const int tileX[3] = {6, 84, 162};
    char precipValue[12];
    char humidityValue[12];
    char pressureValue[12];
    snprintf(precipValue, sizeof(precipValue), "%d%%", weather->rainChance);
    if (weather->humidity >= 0) {
        snprintf(humidityValue, sizeof(humidityValue), "%d%%", weather->humidity);
    } else {
        strcpy(humidityValue, "--");
    }
    if (weather->pressure > 0) {
        snprintf(pressureValue, sizeof(pressureValue), "%d", weather->pressure);
    } else {
        strcpy(pressureValue, "--");
    }
    const char *tileLabels[3] = {"PRECIP", "HUMID", "BARO"};
    const char *tileValues[3] = {precipValue, humidityValue, pressureValue};
    const uint16_t tileColors[3] = {theme.accent, theme.warning, theme.positive};

    for (int index = 0; index < 3; ++index) {
        drawTile(tileX[index], 190, 72, 42, theme.surfaceAlt, theme.accentSoft);
        drawPaddedText(tileLabels[index], tileX[index] + 5, 194, TL_DATUM, FONT_INFO, 0,
                       theme.muted, theme.surfaceAlt);
        drawPaddedText(tileValues[index], tileX[index] + 5, 204, TL_DATUM, FONT_LABEL, 0,
                       tileColors[index], theme.surfaceAlt);
    }
    drawSegmentBar(tileX[0] + 5, 224, 62, 4, weather->rainChance, 4, theme.accent, theme.background);
    if (weather->humidity >= 0) {
        drawLevelBar(tileX[1] + 5, 224, 62, 4, weather->humidity, theme.warning, theme.background);
    }
    drawPaddedText("hPa", tileX[2] + 5, 223, TL_DATUM, FONT_INFO, 0, theme.muted, theme.surfaceAlt);
}

// --- face: radial ------------------------------------------------------------
// Point on a gauge circle. Angle 0 is 6 o'clock and grows towards 9, 12 then 3 o'clock,
// which is how TFT_eSPI's drawArc() measures it.
void radialPoint(int angleDegrees, int radius, int &x, int &y) {
    float radians = static_cast<float>(angleDegrees) * DEG_TO_RAD;
    x = kRadialCenter - static_cast<int>(lroundf(sinf(radians) * radius));
    y = kRadialCenter + static_cast<int>(lroundf(cosf(radians) * radius));
}

void renderRadialFace() {
    const ThemePalette &theme = activeTheme();
    tft.fillScreen(theme.background);

    bool haveWeather = hasWeatherContent();
    const WeatherData *weather = haveWeather ? effectiveWeatherData() : nullptr;

    // Two perimeter gauges: rain over the top, today's temperature range along the
    // bottom. Tracks go down first so an empty gauge still reads as a gauge. The
    // flanks either side stay clear, which is where the seconds sit.
    tft.drawArc(kRadialCenter, kRadialCenter, kRadialOuterRadius, kRadialInnerRadius,
                120, 240, theme.surfaceAlt, theme.background, false);
    tft.drawArc(kRadialCenter, kRadialCenter, kRadialOuterRadius, kRadialInnerRadius,
                300, 60, theme.surfaceAlt, theme.background, false);

    if (!haveWeather) {
        drawClockFallbackMessage(kRadialCenter, 62, 150, FONT_INFO, theme.background);
        return;
    }

    int rain = constrain(weather->rainChance, 0, 100);
    if (rain > 0) {
        tft.drawArc(kRadialCenter, kRadialCenter, kRadialOuterRadius, kRadialInnerRadius,
                    120, 120 + ((rain * 120) / 100), theme.accent, theme.background, false);
    }
    drawPaddedText(String(rain) + "%", kRadialCenter, 22, TC_DATUM, FONT_INFO, 0,
                   theme.accent, theme.background);

    // Bottom gauge: the low sits at the left end, the high at the right end, and the
    // marker shows where the current temperature falls between them.
    int span = weather->high - weather->low;
    int position = span > 0
                       ? constrain(((weather->temperature - weather->low) * 100) / span, 0, 100)
                       : 50;
    int markerAngle = (60 - ((position * 120) / 100) + 360) % 360;
    if (markerAngle != 60) {
        tft.drawArc(kRadialCenter, kRadialCenter, kRadialOuterRadius, kRadialInnerRadius,
                    markerAngle, 60, theme.warning, theme.background, false);
    }
    int markerX = 0;
    int markerY = 0;
    radialPoint(markerAngle, (kRadialOuterRadius + kRadialInnerRadius) / 2, markerX, markerY);
    tft.fillCircle(markerX, markerY, 5, theme.text);

    WeatherContext context = weatherContextFor(weather);

    String pill = weatherConditionLine(weather);
    if (weather->location[0] != '\0') {
        pill = String(weather->location) + "  " + pill;
    }
    tft.fillRoundRect(54, 40, 132, 18, 9, theme.surface);
    const int pillFonts[] = {FONT_INFO};
    drawAdaptiveText(pill, kRadialCenter, 49, 122, MC_DATUM, pillFonts, 1,
                     weatherIsStale() ? theme.warning : theme.text, theme.surface);

    // Icon sits at 78, not 82: the storm glyph's bolt runs 20 px below centre and would
    // otherwise poke into the big-time band at y=102 and lose its tip on every blink.
    drawWeatherIcon(92, 78, 30, weather->weatherCode, theme.text, theme.accent,
                    theme.background, context.isNight, context.nearSunEvent);

    drawTemperatureCluster(weather->temperature, 122, 68, FONT_BODY, FONT_LABEL, 4,
                           theme.text, theme.accent, theme.background);

    String lowText = "LO " + String(weather->low);
    drawPaddedText(lowText, 56, 180, TL_DATUM, FONT_INFO, 0, theme.accent, theme.background);
    drawDegreeRing(56 + tft.textWidth(lowText, FONT_INFO) + 4, 181, 2, theme.accent, theme.background);
    drawPaddedText("HI " + String(weather->high), 176, 180, TR_DATUM, FONT_INFO, 0,
                   theme.warning, theme.background);
    drawDegreeRing(181, 181, 2, theme.warning, theme.background);
}

void renderClockPage() {
    clockMetaCache.valid = false;
    switch (dashboardConfig.clockFace) {
        case DASHBOARD_CLOCK_FACE_BOLD:
            renderBoldFace();
            break;
        case DASHBOARD_CLOCK_FACE_MATRIX:
            renderMatrixFace();
            break;
        case DASHBOARD_CLOCK_FACE_RADIAL:
            renderRadialFace();
            break;
        default:
            renderCardsFace();
            break;
    }
    updateClockDynamicArea();
}

// A photo frame gets the whole panel: no chrome, no header, nothing over the image.
void renderPhotosPage() {
    const ThemePalette &theme = activeTheme();
    // maybeAdvancePhoto() runs first in displayUpdate() and owns index/path validity, so
    // an empty path here means there is genuinely nothing to show.
    if (currentPhotoPath[0] == '\0') {
        drawPlaceholder("Photos", "Upload a 240x240 JPEG.");
        return;
    }

    tft.fillScreen(theme.background);
    if (displayRenderImage(currentPhotoPath)) {
        return;
    }

    // Undecodable. Step past it so one bad file cannot own the slideshow; if it is the
    // only photo there is nothing to step to, and leaving the message up is honest.
    uint16_t count = photoCount();
    if (count > 1) {
        selectPhoto((photoIndex + 1) % count);
        lastPhotoAdvanceMs = millis();
    }
}

// Placeholder until stage 4: proves the page joins rotation and shows live state.

// --- Spotify player face 2: text-first, no album art ------------------------
// design/spotify-face-2-low-ram.html. The mockup's conceit is "zero JPEG decode, pure
// vector primitives", and that is real here rather than decorative: this face never
// touches TJpg or /art.jpg, so it costs nothing on a board with 2 KB of DRAM free
// during a poll.

// The mockup puts a seven-channel spectrum analyser across the top. The device has no
// audio, so those bars would be animating invented numbers. Same visual language, real
// quantities: signal, both heaps as they were during the poll, and round-trip time.
void drawSpotifyMeters(bool valuesOnly) {
    const int panelY = 22;
    const int panelH = 68;
    const int count = 4;
    const int slotW = kBarW / count;

    if (!valuesOnly) {
        drawRoundedPanel(kBarX, panelY, kBarW, panelH, kSpPanel, kSpPanel);
    }

    const char *labels[count] = {"WIFI", "DRAM", "IRAM", "POLL"};
    const uint16_t colors[count] = {kSpCyan, kSpMint, kSpGreen, kSpAmber};

    // Each reading mapped to 0-100 against a ceiling that means something on this board:
    // -30 dBm is excellent and -90 unusable; 8 KB of DRAM free during a poll would be
    // luxurious; IRAM starts around 18 KB; a poll under 2.5 s is the best we ever see.
    int rssi = WiFi.RSSI();
    int percent[count];
    percent[0] = constrain((rssi + 90) * 100 / 60, 0, 100);
    percent[1] = constrain(static_cast<int>(spotifyRuntime.lastPollDramBytes / 82), 0, 100);
    percent[2] = constrain(static_cast<int>(spotifyRuntime.lastPollIramBytes / 184), 0, 100);
    percent[3] = constrain(100 - (spotifyRuntime.lastPollLatencyMs / 25), 0, 100);

    char value[count][12];
    snprintf(value[0], sizeof(value[0]), "%d", rssi);
    snprintf(value[1], sizeof(value[1]), "%uk",
             static_cast<unsigned>(spotifyRuntime.lastPollDramBytes / 1024U));
    snprintf(value[2], sizeof(value[2]), "%uk",
             static_cast<unsigned>(spotifyRuntime.lastPollIramBytes / 1024U));
    snprintf(value[3], sizeof(value[3]), "%u", spotifyRuntime.lastPollLatencyMs);

    const int barTop = panelY + 18;
    const int barHeight = 30;
    for (int i = 0; i < count; ++i) {
        int centreX = kBarX + slotW * i + slotW / 2;
        if (!valuesOnly) {
            drawPaddedText(labels[i], centreX, panelY + 5, TC_DATUM, FONT_INFO, 0,
                           kSpMuted, kSpPanel);
        }
        // Vertical bar, filled from the bottom like a level meter.
        int barW = 18;
        int filled = (barHeight * percent[i]) / 100;
        tft.fillRect(centreX - barW / 2, barTop, barW, barHeight - filled, kSpSurface);
        tft.fillRect(centreX - barW / 2, barTop + barHeight - filled, barW, filled, colors[i]);
        drawPaddedText(value[i], centreX, barTop + barHeight + 4, TC_DATUM, FONT_INFO,
                       slotW - 6, kSpText, kSpPanel);
    }
}

// Sixteen chunks rather than a smooth bar, straight from the mockup. Drawn segment by
// segment so a repaint only touches the chunks that changed state.
void drawSegmentedProgress() {
    const int segments = 16;
    const int gap = 3;
    const int segmentW = (kBarW - gap * (segments - 1)) / segments;
    const int y = 168;
    uint32_t progress = spotifyProgressMs();
    int lit = 0;
    if (spotifyRuntime.durationMs > 0) {
        lit = static_cast<int>((static_cast<uint64_t>(progress) * segments) /
                               spotifyRuntime.durationMs);
        lit = constrain(lit, 0, segments);
    }
    for (int i = 0; i < segments; ++i) {
        tft.fillRect(kBarX + i * (segmentW + gap), y, segmentW, 8,
                     i < lit ? kSpGreen : kSpPanel);
    }
}

void drawFace2Dynamic() {
    drawSpotifyMeters(true);
    drawSegmentedProgress();

    uint32_t progress = spotifyProgressMs();
    drawPaddedText(String("ELAPSED ") + formatTrackTime(progress), kBarX, 182, TL_DATUM,
                   FONT_INFO, 96, kSpCyan, kSpSurface);
    String remain = "REMAIN --:--";
    if (spotifyRuntime.durationMs > progress) {
        remain = String("REMAIN -") + formatTrackTime(spotifyRuntime.durationMs - progress);
    }
    drawPaddedText(remain, kBarX + kBarW, 182, TR_DATUM, FONT_INFO, 96, kSpMint, kSpSurface);
}

void renderSpotifyFaceText() {
    static const int infoOnly[] = {FONT_INFO};
    tft.fillScreen(kSpSurface);

    tft.fillRect(0, 0, 240, 18, kSpBar);
    tft.fillCircle(11, 9, 4, kSpGreen);
    drawPaddedText("SPOTIFY", 20, 5, TL_DATUM, FONT_INFO, 0, kSpGreen, kSpBar);
    drawPaddedText(spotifyRuntime.playing ? "PLAYING" : "PAUSED", 232, 5, TR_DATUM,
                   FONT_INFO, 0, spotifyRuntime.playing ? kSpMint : kSpAmber, kSpBar);

    drawSpotifyMeters(false);

    // The mockup shows "TRACK // 04" and "FLAC - 24-BIT" here. Queue position, codec and
    // bit depth are all absent from the Web API, so the row carries what is real.
    String modes = String("SHUF:") + (spotifyRuntime.shuffle ? "ON" : "OFF");
    if (spotifyRuntime.repeatMode[0] != '\0') {
        modes += "  REP:" + String(spotifyRuntime.repeatMode);
    }
    modes.toUpperCase();
    drawAdaptiveText(modes, kBarX, 96, 130, TL_DATUM, infoOnly, 1, kSpMuted, kSpSurface);
    if (spotifyRuntime.volumePercent >= 0) {
        drawPaddedText(String("VOL ") + spotifyRuntime.volumePercent + "%", kBarX + kBarW, 96,
                       TR_DATUM, FONT_INFO, 70, kSpAmber, kSpSurface);
    }

    const int titleFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    drawAdaptiveText(spotifyRuntime.trackName[0] != '\0' ? spotifyRuntime.trackName : "Nothing playing",
                     kBarX, 110, kBarW, TL_DATUM, titleFonts,
                     sizeof(titleFonts) / sizeof(titleFonts[0]), kSpText, kSpSurface);

    const int artistFonts[] = {FONT_LABEL, FONT_INFO};
    drawAdaptiveText(spotifyRuntime.artistName, kBarX, 142, kBarW, TL_DATUM, artistFonts,
                     sizeof(artistFonts) / sizeof(artistFonts[0]), kSpAmber, kSpSurface);

    drawSegmentedProgress();
    drawFace2Dynamic();

    drawPaddedText(spotifyRuntime.lastError[0] != '\0' ? spotifyRuntime.lastError
                                                       : spotifyRuntime.deviceName,
                   120, 206, TC_DATUM, FONT_INFO, 230,
                   spotifyRuntime.lastError[0] != '\0' ? kSpAmber : kSpMuted, kSpSurface);
}

void renderSpotifyFaceArt() {
    static const int infoOnly[] = {FONT_INFO};
    tft.fillScreen(kSpSurface);

    // Top bar: brand mark left, the device actually playing on the right. The mockup shows
    // a bitrate here; the Web API does not report one, so the device name takes the slot.
    tft.fillRect(0, 0, 240, 20, kSpBar);
    tft.fillCircle(12, 10, 4, kSpGreen);
    drawPaddedText("SPOTIFY", 22, 6, TL_DATUM, FONT_INFO, 0, kSpGreen, kSpBar);
    if (spotifyRuntime.deviceName[0] != '\0') {
        drawAdaptiveText(spotifyRuntime.deviceName, 232, 6, 150, TR_DATUM,
                         infoOnly, 1, kSpAmber, kSpBar);
    }

    drawAlbumArt();

    drawPaddedText(spotifyRuntime.playing ? "NOW PLAYING" : "PAUSED", kColX, kArtY + 2, TL_DATUM,
                   FONT_INFO, 0, kSpGreen, kSpSurface);

    const int titleFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    drawAdaptiveText(spotifyRuntime.trackName[0] != '\0' ? spotifyRuntime.trackName : "Nothing playing",
                     kColX, kArtY + 16, kColW, TL_DATUM, titleFonts,
                     sizeof(titleFonts) / sizeof(titleFonts[0]), kSpText, kSpSurface);

    const int artistFonts[] = {FONT_LABEL, FONT_INFO};
    drawAdaptiveText(spotifyRuntime.artistName, kColX, kArtY + 46, kColW, TL_DATUM, artistFonts,
                     sizeof(artistFonts) / sizeof(artistFonts[0]), kSpGreen, kSpSurface);

    // Shuffle and repeat are the only playback modes the read-only scopes expose.
    String modes = String("SHUF:") + (spotifyRuntime.shuffle ? "ON" : "OFF");
    if (spotifyRuntime.repeatMode[0] != '\0') {
        modes += "  REP:" + String(spotifyRuntime.repeatMode);
    }
    modes.toUpperCase();  // the API returns "off"/"track"/"context" in lower case
    drawAdaptiveText(modes, kColX, kArtY + 66, kColW, TL_DATUM, infoOnly, 1,
                     kSpMuted, kSpSurface);

    drawSpotifyProgress();
    drawSpotifyTelemetry(false);

    // No transport row: this board has no buttons and the token is read-only, so painted
    // prev/play/next would be decoration that lies about what the device can do.
    drawPaddedText(spotifyRuntime.lastError[0] != '\0' ? spotifyRuntime.lastError : "SPOTIFY CONNECT",
                   120, 208, TC_DATUM, FONT_INFO, 220,
                   spotifyRuntime.lastError[0] != '\0' ? kSpAmber : kSpMuted, kSpSurface);
}

void renderMarketsPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasMarketContent()) {
        if (anyMarketsWaitingForSync()) {
            drawPlaceholder("Markets", "Syncing live data.");
            return;
        }
        drawPlaceholder("Markets", "Add ticker data.");
        return;
    }

    drawScreenChrome("Markets");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    int y = 50;
    bool firstRow = true;
    const int standardPriceFonts[] = {FONT_TITLE, FONT_BODY, FONT_LABEL};
    const int compactPriceFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int symbolFonts[] = {FONT_BODY, FONT_LABEL};
    const int labelFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int deltaFonts[] = {FONT_INFO};
    for (uint8_t marketIndex = 0; marketIndex < DASHBOARD_MARKET_COUNT; ++marketIndex) {
        const MarketData *market = effectiveMarketData(marketIndex);
        if (market == nullptr ||
            market->symbol[0] == '\0' ||
            !market->enabled) {
            continue;
        }

        if (!firstRow) {
            drawDividerLine(20, y - 8, 192, theme.surfaceAlt);
        }

        tft.setTextDatum(TL_DATUM);
        String marketLabel = market->label[0] != '\0' ? market->label : "Live feed";
        drawAdaptiveText(market->symbol,
                         20,
                         y,
                         72,
                         TL_DATUM,
                         symbolFonts,
                         sizeof(symbolFonts) / sizeof(symbolFonts[0]),
                         theme.accent,
                         theme.surface);
        drawAdaptiveText(marketLabel,
                         20,
                         y + 20,
                         104,
                         TL_DATUM,
                         labelFonts,
                         sizeof(labelFonts) / sizeof(labelFonts[0]),
                         theme.text,
                         theme.surface);

        tft.setTextDatum(TR_DATUM);
        String priceText = formatMarketPrice(marketIndex, market->price);
        const int *priceFonts = standardPriceFonts;
        size_t priceFontCount = sizeof(standardPriceFonts) / sizeof(standardPriceFonts[0]);
        if (priceText.length() >= 8 || tft.textWidth(priceText, FONT_TITLE) > 126) {
            priceFonts = compactPriceFonts;
            priceFontCount = sizeof(compactPriceFonts) / sizeof(compactPriceFonts[0]);
        }

        int priceFont = chooseFittingFont(priceText, 134, priceFonts, priceFontCount);
        tft.setTextFont(priceFont);
        tft.setTextColor(theme.text, theme.surface);
        tft.drawString(priceText, 220, y + 2, priceFont);

        int deltaY = y + 2 + tft.fontHeight() + 4;

        uint16_t deltaColor = market->changePercent >= 0.0f ? theme.positive : theme.negative;
        drawAdaptiveText(formatMarketChangeSummary(marketIndex, market->changePercent),
                         220,
                         deltaY,
                         134,
                         TR_DATUM,
                         deltaFonts,
                         sizeof(deltaFonts) / sizeof(deltaFonts[0]),
                         deltaColor,
                         theme.surface);

        y += 58;
        firstRow = false;
    }
}

void drawHomeAssistantCard(int x,
                           int y,
                           int width,
                           int height,
                           uint8_t slotIndex,
                           uint16_t fillColor,
                           uint16_t borderColor) {
    const ThemePalette &theme = activeTheme();
    drawRoundedPanel(x, y, width, height, fillColor, borderColor);

    tft.fillRoundRect(x + 10, y + 10, 26, 4, 2, theme.accentSoft);

    const int labelFonts[] = {FONT_LABEL, FONT_INFO};
    const int valueFonts[] = {FONT_TITLE, FONT_BODY, FONT_LABEL, FONT_INFO};
    const int unitFonts[] = {FONT_INFO};
    const HomeAssistantSlotData *slot = feedsHomeAssistantSlotData(slotIndex);
    int labelY = y + 18;
    int stateY = height >= 120 ? y + 74 : (height >= 96 ? y + 50 : y + 40);
    int unitY = height >= 120 ? y + 106 : (height >= 96 ? y + 76 : y + 61);

    String label = homeAssistantSlotLabel(slotIndex);
    drawAdaptiveText(label,
                     x + 12,
                     labelY,
                     width - 24,
                     TL_DATUM,
                     labelFonts,
                     sizeof(labelFonts) / sizeof(labelFonts[0]),
                     theme.muted,
                     fillColor);

    String unit = homeAssistantSlotUnit(slotIndex);
    if (slot != nullptr && slot->hasData && unit.length() > 0) {
        drawAdaptiveBadge(x + width - 52,
                          y + 10,
                          40,
                          18,
                          unit,
                          unitFonts,
                          sizeof(unitFonts) / sizeof(unitFonts[0]),
                          theme.accentSoft,
                          theme.accent);
    }

    String stateText = "Waiting";
    uint16_t stateColor = theme.text;
    if (slot != nullptr && slot->hasData) {
        stateText = safeCString(slot->state);
        stateColor = slot->numeric ? theme.text : theme.accent;
    } else if (!feedsHomeAssistantConfigured()) {
        stateText = "Add";
        stateColor = theme.muted;
    } else if (!homeAssistantSlotConfigured(slotIndex)) {
        stateText = "Off";
        stateColor = theme.muted;
    } else if (!homeAssistantWaitingForSync()) {
        stateText = "No data";
        stateColor = theme.muted;
    }

    drawAdaptiveText(stateText,
                     x + (width / 2),
                     stateY,
                     width - 20,
                     TC_DATUM,
                     valueFonts,
                     sizeof(valueFonts) / sizeof(valueFonts[0]),
                     stateColor,
                     fillColor);

    if (slot != nullptr &&
        slot->hasData &&
        unit.length() > 0 &&
        (!slot->numeric || tft.textWidth(stateText, FONT_TITLE) > width - 30)) {
        drawAdaptiveText(unit,
                         x + (width / 2),
                         unitY,
                         width - 22,
                         TC_DATUM,
                         unitFonts,
                         sizeof(unitFonts) / sizeof(unitFonts[0]),
                         theme.muted,
                         fillColor);
    }
}

void renderHomePage() {
    const ThemePalette &theme = activeTheme();
    if (!hasHomeAssistantContent()) {
        if (homeAssistantWaitingForSync()) {
            drawPlaceholder("Home", "Syncing Home Assistant.");
            return;
        }
        drawPlaceholder("Home", "Add Home Assistant entities.");
        return;
    }

    drawScreenChrome("Home");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    uint8_t slotIndexes[HOME_ASSISTANT_SLOT_COUNT] = {0};
    uint8_t slotCount = 0;
    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (homeAssistantSlotConfigured(index)) {
            slotIndexes[slotCount++] = index;
        }
    }

    if (slotCount == 0) {
        drawWrappedCenteredText("Add Home Assistant entities.", 120, 114, 188, FONT_LABEL, theme.muted, theme.surface, 2);
        return;
    }

    if (slotCount == 1) {
        drawHomeAssistantCard(16, 48, 208, 160, slotIndexes[0], theme.surfaceAlt, theme.surfaceAlt);
        return;
    }

    if (slotCount == 2) {
        drawHomeAssistantCard(16, 48, 208, 70, slotIndexes[0], theme.surfaceAlt, theme.surfaceAlt);
        drawHomeAssistantCard(16, 134, 208, 70, slotIndexes[1], theme.surfaceAlt, theme.surfaceAlt);
        return;
    }

    const int cardWidth = 100;
    const int cardHeight = 80;
    const int leftColumnX = 16;
    const int rightColumnX = 124;
    const int topRowY = 48;
    const int bottomRowY = 136;

    for (uint8_t displayIndex = 0; displayIndex < slotCount; ++displayIndex) {
        int x = (displayIndex % 2 == 0) ? leftColumnX : rightColumnX;
        int y = (displayIndex < 2) ? topRowY : bottomRowY;
        drawHomeAssistantCard(x,
                              y,
                              cardWidth,
                              cardHeight,
                              slotIndexes[displayIndex],
                              theme.surfaceAlt,
                              theme.surfaceAlt);
    }
}

void renderFocusPage() {
    if (!hasFocusContent()) {
        drawPlaceholder("Focus", "Set a timer when you need it.");
        return;
    }

    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Focus");

    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);
    drawBadge(18, 48, 64, 20, dashboardData.focus.breakMode ? "Break" : "Focus", theme.accentSoft, theme.accent);

    if (dashboardData.focus.running) {
        drawBadge(166, 48, 54, 20, "Active", theme.positive, theme.background);
    } else {
        drawBadge(170, 48, 50, 20, "Ready", theme.surfaceAlt, theme.text);
    }

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString(dashboardData.focus.label[0] != '\0' ? dashboardData.focus.label : "Session",
                   120, 76, FONT_INFO);

    updateFocusDynamicArea();
}

void renderWorldPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasWorldClockContent()) {
        drawPlaceholder("World", "Add world clocks.");
        return;
    }

    drawScreenChrome("World");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    int y = 52;
    bool firstRow = true;
    const int offsetFonts[] = {FONT_INFO};
    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        const WorldClockData &clock = dashboardData.worldClocks[clockIndex];
        if (!clock.enabled || clock.label[0] == '\0') {
            continue;
        }

        if (!firstRow) {
            drawDividerLine(20, y - 10, 184, theme.surfaceAlt);
        }

        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.surface);
        tft.drawString(clock.label, 20, y, FONT_INFO);

        drawAdaptiveText(formatUtcOffset(clock.offsetSeconds),
                         220,
                         y,
                         88,
                         TR_DATUM,
                         offsetFonts,
                         sizeof(offsetFonts) / sizeof(offsetFonts[0]),
                         theme.accent,
                         theme.surface);
        y += 72;
        firstRow = false;
    }

    updateWorldDynamicArea();
}

void renderEventPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasEventContent()) {
        drawPlaceholder("Event", "Add a countdown.");
        return;
    }

    drawScreenChrome("Event");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    drawWrappedCenteredText(dashboardData.event.title[0] != '\0' ? dashboardData.event.title : "Upcoming",
                            120, 56, 186, FONT_BODY, theme.text, theme.surface, 2);
    tft.fillRoundRect(104, 82, 32, 4, 2, theme.accentSoft);

    updateEventDynamicArea();

    if (dashboardData.event.subtitle[0] != '\0') {
        drawWrappedCenteredText(dashboardData.event.subtitle,
                                120,
                                178,
                                184,
                                FONT_INFO,
                                theme.muted,
                                theme.surface,
                                2);
    }
}

void renderQuotePage() {
    const ThemePalette &theme = activeTheme();
    if (!hasQuoteContent()) {
        drawPlaceholder("Quote", "Add a quote.");
        return;
    }

    drawScreenChrome("Quote");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    tft.setTextDatum(TC_DATUM);
    tft.fillRoundRect(100, 52, 40, 40, 20, theme.accentSoft);
    tft.setTextFont(FONT_TITLE);
    tft.setTextColor(theme.accent, theme.accentSoft);
    tft.drawString("\"", 120, 60, FONT_TITLE);

    drawWrappedCenteredText(safeCString(dashboardData.quote.text),
                            120, 104, 186, FONT_LABEL, theme.text, theme.surface, 4);

    if (dashboardData.quote.author[0] != '\0') {
        int chipWidth = min(160, tft.textWidth(dashboardData.quote.author, FONT_INFO) + 20);
        int chipX = 120 - (chipWidth / 2);
        tft.fillRoundRect(chipX, 186, chipWidth, 20, 10, theme.accentSoft);
        drawAdaptiveText(safeCString(dashboardData.quote.author),
                         120,
                         191,
                         chipWidth - 14,
                         MC_DATUM,
                         nullptr,
                         0,
                         theme.accent,
                         theme.accentSoft);
    }
}

void renderStatusPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasStatusContent()) {
        drawPlaceholder("Status", "Add status lines.");
        return;
    }

    drawScreenChrome("Status");

    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_TITLE);
    tft.setTextColor(theme.text, theme.surface);
    drawWrappedCenteredText(dashboardData.status.line1[0] != '\0' ? dashboardData.status.line1 : "Status line 1",
                            120, 74, 184, FONT_BODY, theme.text, theme.surface, 2);

    drawDividerLine(28, 132, 184, theme.surfaceAlt);
    drawWrappedCenteredText(dashboardData.status.line2[0] != '\0' ? dashboardData.status.line2 : "Status line 2",
                            120, 150, 184, FONT_LABEL, theme.muted, theme.surface, 2);
}

void renderDashboardPage() {
    if (!dashboardPageAvailable(displayState.currentPage)) {
        displayState.currentPage = firstAvailableDashboardPage();
    }

    switch (displayState.currentPage) {
        case DASHBOARD_PAGE_CLOCK:
            renderClockPage();
            break;
        case DASHBOARD_PAGE_PHOTOS:
            renderPhotosPage();
            break;
        case DASHBOARD_PAGE_SPOTIFY:
            renderSpotifyPage();
            break;
        case DASHBOARD_PAGE_MARKETS:
            renderMarketsPage();
            break;
        case DASHBOARD_PAGE_HOME:
            renderHomePage();
            break;
        case DASHBOARD_PAGE_FOCUS:
            renderFocusPage();
            break;
        case DASHBOARD_PAGE_WORLD:
            renderWorldPage();
            break;
        case DASHBOARD_PAGE_EVENT:
            renderEventPage();
            break;
        case DASHBOARD_PAGE_QUOTE:
            renderQuotePage();
            break;
        case DASHBOARD_PAGE_STATUS:
            renderStatusPage();
            break;
        default:
            renderClockPage();
            break;
    }
}

void updateDashboardDynamicArea(uint8_t pageId) {
    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            updateClockDynamicArea();
            break;
        case DASHBOARD_PAGE_SPOTIFY:
            updateSpotifyDynamicArea();
            break;
        case DASHBOARD_PAGE_FOCUS:
            updateFocusDynamicArea();
            break;
        case DASHBOARD_PAGE_WORLD:
            updateWorldDynamicArea();
            break;
        case DASHBOARD_PAGE_EVENT:
            updateEventDynamicArea();
            break;
        default:
            break;
    }
}

void renderDashboardPageCached() {
    if (!dashboardPageAvailable(displayState.currentPage)) {
        displayState.currentPage = firstAvailableDashboardPage();
    }

    uint8_t pageId = displayState.currentPage;
    uint8_t themeId = activeThemeIdValue();
    uint32_t staticHash = dashboardStaticHash(pageId);
    uint32_t dynamicHash = dashboardDynamicHash(pageId);

    bool requiresFullRender = !renderCache.valid ||
                              renderCache.mode != DISPLAY_MODE_DASHBOARD ||
                              renderCache.page != pageId ||
                              renderCache.theme != themeId ||
                              renderCache.staticHash != staticHash;

    if (requiresFullRender) {
        renderDashboardPage();
        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_DASHBOARD;
        renderCache.page = pageId;
        renderCache.theme = themeId;
        renderCache.staticHash = staticHash;
        renderCache.dynamicHash = dynamicHash;
        renderCache.imagePath[0] = '\0';
        return;
    }

    if (pageUsesDynamicRefresh(pageId) && renderCache.dynamicHash != dynamicHash) {
        updateDashboardDynamicArea(pageId);
        renderCache.dynamicHash = dynamicHash;
    }
}

}  // namespace

void setBacklightLevel(int brightness, bool shouldLog = true) {
    int boundedBrightness = constrain(brightness, 0, 100);
    if (appliedBrightness == boundedBrightness) {
        return;
    }

    int pwmValue = 0;
    if (boundedBrightness == 0) {
        pwmValue = 1023;
    } else if (boundedBrightness == 100) {
        pwmValue = 0;
    } else {
        pwmValue = map(boundedBrightness, 0, 100, 1023, 0);
    }

    analogWrite(PIN_BACKLIGHT, pwmValue);
    appliedBrightness = boundedBrightness;
    if (shouldLog) {
        logPrintf("Brightness: %d%%, PWM: %d", boundedBrightness, pwmValue);
    }
}

void animateBacklightRamp(int fromBrightness,
                          int toBrightness,
                          uint8_t steps,
                          uint16_t stepDelayMs) {
    if (steps == 0) {
        setBacklightLevel(toBrightness, false);
        return;
    }

    int start = constrain(fromBrightness, 0, 100);
    int target = constrain(toBrightness, 0, 100);
    if (start == target) {
        return;
    }

    for (uint8_t step = 1; step <= steps; ++step) {
        int interpolated = start + ((target - start) * static_cast<int>(step)) / static_cast<int>(steps);
        setBacklightLevel(interpolated, false);
        if (stepDelayMs > 0) {
            delay(stepDelayMs);
            yield();
        }
    }
}

int beginPageTransition(int targetBrightness) {
    int startBrightness = constrain(targetBrightness, 0, 100);
    if (startBrightness < kPageTransitionMinimumBrightness) {
        return startBrightness;
    }

    int dimBrightness = max(static_cast<int>(kPageTransitionMinimumBrightness),
                            (startBrightness * static_cast<int>(kPageTransitionDimPercent)) / 100);
    if (dimBrightness >= startBrightness) {
        return startBrightness;
    }

    animateBacklightRamp(startBrightness,
                         dimBrightness,
                         kPageTransitionFadeDownSteps,
                         kPageTransitionFadeStepDelayMs);
    return dimBrightness;
}

void endPageTransition(int transitionBrightness, int targetBrightness) {
    int startBrightness = constrain(transitionBrightness, 0, 100);
    int endBrightness = constrain(targetBrightness, 0, 100);
    if (startBrightness == endBrightness) {
        return;
    }

    delay(8);
    yield();
    animateBacklightRamp(startBrightness,
                         endBrightness,
                         kPageTransitionFadeUpSteps,
                         kPageTransitionFadeStepDelayMs);
}

void displayInit() {
    logPrint(F("Display init..."));

    tft.init();
    tft.setRotation(0);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);

    // Without a seed the shuffle plays the same order after every power cycle.
    randomSeed(micros());
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tftOutput);

    displayState.line2[0] = '\0';
    displayState.ipInfo[0] = '\0';
    displayState.showImage = false;
    displayState.imagePath[0] = '\0';
    displayState.apMode = false;
    displayState.apSSID[0] = '\0';
    displayState.apPassword[0] = '\0';
    displayState.currentPage = DASHBOARD_PAGE_CLOCK;
    clearTemporaryMessage();

    pinMode(PIN_BACKLIGHT, OUTPUT);
    analogWriteFreq(1000);
    analogWriteRange(1023);
    displaySetBrightness(100);

    logPrint(F("Display init complete"));
}

void displaySetBrightness(int brightness) {
    setBacklightLevel(brightness);
}

void displayApplyBrightness(int brightness) {
    setBacklightLevel(brightness);
}

void displayUpdate() {
    latchClockColonPhase();
    maybeAdvancePhoto();

    if (temporaryMessageVisible()) {
        uint32_t currentHash = temporaryMessageHash();
        uint8_t themeId = activeThemeIdValue();
        if (renderCache.valid &&
            renderCache.mode == DISPLAY_MODE_TEMP_MESSAGE &&
            renderCache.theme == themeId &&
            renderCache.staticHash == currentHash) {
            return;
        }

        const ThemePalette &theme = activeTheme();
        drawScreenChrome("SmartClock");
        drawRoundedPanel(12, 48, DISPLAY_WIDTH - 24, 176, theme.surface, theme.surfaceAlt);
        drawWrappedCenteredText(temporaryMessage.message, 120, 92, 192, FONT_BODY, theme.text, theme.surface, 4);

        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_TEMP_MESSAGE;
        renderCache.page = 0;
        renderCache.theme = themeId;
        renderCache.staticHash = currentHash;
        renderCache.dynamicHash = 0;
        renderCache.imagePath[0] = '\0';
        return;
    }

    if (temporaryMessageExpired()) {
        clearTemporaryMessage();
        invalidateRenderCache();
    }

    if (displayState.showImage && displayState.imagePath[0] != '\0') {
        if (renderCache.valid &&
            renderCache.mode == DISPLAY_MODE_IMAGE &&
            strcmp(renderCache.imagePath, displayState.imagePath) == 0) {
            return;
        }

        displayRenderImage(displayState.imagePath);
        if (LittleFS.exists(displayState.imagePath)) {
            renderCache.valid = true;
            renderCache.mode = DISPLAY_MODE_IMAGE;
            renderCache.page = 0;
            renderCache.theme = activeThemeIdValue();
            renderCache.staticHash = 0;
            renderCache.dynamicHash = 0;
            strncpy(renderCache.imagePath, displayState.imagePath, sizeof(renderCache.imagePath) - 1);
            renderCache.imagePath[sizeof(renderCache.imagePath) - 1] = '\0';
        }
        return;
    }

    if (displayState.apMode) {
        uint32_t currentHash = apScreenHash();
        uint8_t themeId = activeThemeIdValue();
        if (renderCache.valid &&
            renderCache.mode == DISPLAY_MODE_AP &&
            renderCache.theme == themeId &&
            renderCache.staticHash == currentHash) {
            return;
        }

        displayRenderAPMode();
        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_AP;
        renderCache.page = 0;
        renderCache.theme = themeId;
        renderCache.staticHash = currentHash;
        renderCache.dynamicHash = 0;
        renderCache.imagePath[0] = '\0';
        return;
    }

    renderDashboardPageCached();
}

void displayRenderClock() {
    renderClockPage();
}

void displayRenderAPMode() {
    const ThemePalette &theme = activeTheme();
    bool canRevealAdminPassword = authCanRevealPassword();
    const char *adminPassword = authProvisionedPassword();
    String setupIp = displayState.ipInfo[0] != '\0' ? safeCString(displayState.ipInfo) : String("192.168.4.1");
    const int kWideFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int kIpFonts[] = {FONT_INFO};
    const int kCompactFonts[] = {FONT_LABEL, FONT_INFO};

    tft.fillScreen(theme.background);

    tft.fillRoundRect(10, 10, 220, 24, 12, theme.surfaceAlt);
    tft.fillCircle(24, 22, 4, theme.accent);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.text, theme.surfaceAlt);
    tft.drawString("Setup mode", 36, 16, FONT_INFO);

    drawRoundedPanel(10, 42, 220, 60, theme.accentSoft, theme.accentSoft);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.accent, theme.accentSoft);
    tft.drawString("SETUP URL", 120, 52, FONT_INFO);
    drawAdaptiveText(setupIp,
                     120,
                     68,
                     192,
                     TC_DATUM,
                     kIpFonts,
                     sizeof(kIpFonts) / sizeof(kIpFonts[0]),
                     theme.text,
                     theme.accentSoft);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.accentSoft);
    tft.drawString("Open this in browser", 120, 86, FONT_INFO);

    drawRoundedPanel(10, 108, 134, 50, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString("WI-FI", 20, 118, FONT_INFO);
    drawAdaptiveText(safeCString(displayState.apSSID),
                     77,
                     136,
                     106,
                     TC_DATUM,
                     kCompactFonts,
                     sizeof(kCompactFonts) / sizeof(kCompactFonts[0]),
                     theme.text,
                     theme.surface);

    drawRoundedPanel(150, 108, 80, 50, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString("PASS", 190, 118, FONT_INFO);
    drawAdaptiveText(safeCString(displayState.apPassword),
                     190,
                     136,
                     58,
                     TC_DATUM,
                     kWideFonts,
                     sizeof(kWideFonts) / sizeof(kWideFonts[0]),
                     theme.text,
                     theme.surface);

    drawRoundedPanel(10, 168, 220, 62, theme.surfaceAlt, theme.surfaceAlt);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(canRevealAdminPassword ? theme.accent : theme.muted, theme.surfaceAlt);
    tft.drawString("ADMIN LOGIN", 20, 178, FONT_INFO);

    if (canRevealAdminPassword && adminPassword != nullptr && adminPassword[0] != '\0') {
        drawBadge(20, 193, 48, 18, "admin", theme.accentSoft, theme.accent);
        drawAdaptiveText(safeCString(adminPassword),
                         220,
                         191,
                         138,
                         TR_DATUM,
                         kWideFonts,
                         sizeof(kWideFonts) / sizeof(kWideFonts[0]),
                         theme.text,
                         theme.surfaceAlt);

        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.surfaceAlt);
        tft.drawString("Use this password on the dashboard.", 20, 212, FONT_INFO);
        return;
    }

    drawWrappedCenteredText("Use your saved admin password after Wi-Fi setup.",
                            120,
                            194,
                            192,
                            FONT_INFO,
                            theme.muted,
                            theme.surfaceAlt,
                            2);
}

void displayBlankScreen() {
    clearTemporaryMessage();
    tft.fillScreen(TFT_BLACK);
    invalidateRenderCache();
    logPrint(F("Display blanked to black."));
}

// Called by the upload and delete handlers: the photo count is cached, and waiting out
// its TTL would make a fresh upload look like it did nothing.
// The slideshow picks its own file, so /app.json is the only way to see which one is up.
const char* displayCurrentPhotoPath() {
    return currentPhotoPath;
}

void displayInvalidatePhotoCache() {
    photoCountValid = false;
    currentPhotoPath[0] = '\0';
}

// Returns false when the file is missing or TJpg cannot decode it, so the slideshow can
// step past a bad photo rather than parking on its error card.
bool displayRenderImage(const char *path) {
    if (!LittleFS.exists(path)) {
        displayShowMessage(F("Image not found"));
        return false;
    }

    File jpgFile = LittleFS.open(path, "r");
    if (!jpgFile) {
        displayShowMessage(String(F("Failed to open\n")) + path);
        return false;
    }

    tft.startWrite();
    JRESULT result = TJpgDec.drawFsJpg(0, 0, jpgFile);
    tft.endWrite();
    jpgFile.close();

    if (result != JDR_OK) {
        // Nearly always a progressive JPEG wearing a .jpg name: TJpg_Decoder reads
        // baseline only. tools/prepare_images.py produces the right thing.
        displayShowMessage(String(F("Not a baseline JPEG\nerror ")) + String(result));
        return false;
    }
    return true;
}

void displayShowMessage(const String &msg) {
    clearTemporaryMessage();
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("SmartClock");
    drawRoundedPanel(18, 54, 204, 144, theme.surface, theme.surfaceAlt);
    drawWrappedCenteredText(msg, 120, 96, 170, FONT_BODY, theme.text, theme.surface, 4);
    invalidateRenderCache();
}

void displayShowTemporaryMessage(const String &msg, uint32_t durationMs) {
    strncpy(temporaryMessage.message, msg.c_str(), sizeof(temporaryMessage.message) - 1);
    temporaryMessage.message[sizeof(temporaryMessage.message) - 1] = '\0';
    temporaryMessage.active = temporaryMessage.message[0] != '\0';
    temporaryMessage.expiresAtMs = millis() + (durationMs > 0 ? durationMs : 1000UL);
    invalidateRenderCache();
    displayUpdate();
}

void displayShowAPScreen(const char *ssid, const char *password, const char *ip) {
    displayState.apMode = true;
    displayState.showImage = false;
    strncpy(displayState.apSSID, ssid, sizeof(displayState.apSSID) - 1);
    displayState.apSSID[sizeof(displayState.apSSID) - 1] = '\0';
    strncpy(displayState.apPassword, password, sizeof(displayState.apPassword) - 1);
    displayState.apPassword[sizeof(displayState.apPassword) - 1] = '\0';
    strncpy(displayState.ipInfo, ip, sizeof(displayState.ipInfo) - 1);
    displayState.ipInfo[sizeof(displayState.ipInfo) - 1] = '\0';
    invalidateRenderCache();
    displayUpdate();
}

bool displaySetPage(uint8_t page, bool smoothTransition) {
    if (temporaryMessageVisible()) {
        logPrint(F("Page change ignored while temporary message is active"));
        return false;
    }

    if (displayState.apMode) {
        logPrint(F("Page change ignored in AP mode"));
        return false;
    }

    // Same gate as the renderer: an enabled page with nothing to draw would be evicted within 1 s anyway.
    if (page >= DASHBOARD_PAGE_COUNT || !dashboardPageAvailable(page)) {
        return false;
    }

    bool leavingImage = displayState.showImage;
    displayState.showImage = false;
    displayState.lastPageChangeMs = millis();
    if (!leavingImage && page == displayState.currentPage) {
        return true;
    }

    displayState.currentPage = page;

    // No fade when coming back from an image: the old behaviour redrew immediately.
    bool fade = smoothTransition && !leavingImage;
    int targetBrightness = appliedBrightness;
    int transitionBrightness = fade ? beginPageTransition(targetBrightness) : targetBrightness;

    invalidateRenderCache();
    displayUpdate();
    if (fade) {
        endPageTransition(transitionBrightness, targetBrightness);
    }
    return true;
}

bool displayCycleNextPage(bool smoothTransition) {
    displayState.lastPageChangeMs = millis();  // restart the rotation timer even if the change is refused
    uint8_t next = (displayState.showImage || !dashboardPageAvailable(displayState.currentPage))
                       ? firstAvailableDashboardPage()
                       : nextAvailableDashboardPage(displayState.currentPage);
    return displaySetPage(next, smoothTransition);
}
