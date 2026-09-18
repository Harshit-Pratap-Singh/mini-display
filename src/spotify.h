#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <Arduino.h>

// Credentials live in their own LittleFS file rather than the 512-byte EEPROM Settings
// struct: the refresh token alone is ~131 chars and the access token 256, which would
// swamp it. Same pattern as /feeds-config.json.
#define SPOTIFY_CONFIG_PATH "/spotify-config.json"
#define SPOTIFY_CONFIG_VERSION 1

// Measured against the real API on 2026-09-18 (see HARDWARE.md):
//   refresh response 378 B, access token 256 chars, expires_in 3600 s,
//   currently-playing 2,655 B for a track, and HTTP 204 with an EMPTY body when idle.
#define SPOTIFY_ACCESS_TOKEN_LENGTH 300
#define SPOTIFY_REFRESH_TOKEN_LENGTH 200
#define SPOTIFY_CLIENT_ID_LENGTH 40
#define SPOTIFY_CLIENT_SECRET_LENGTH 40
#define SPOTIFY_TEXT_LENGTH 64
#define SPOTIFY_ART_PATH "/art.jpg"

struct SpotifyConfig {
    uint16_t version;
    bool enabled;
    char clientId[SPOTIFY_CLIENT_ID_LENGTH];
    char clientSecret[SPOTIFY_CLIENT_SECRET_LENGTH];
    char refreshToken[SPOTIFY_REFRESH_TOKEN_LENGTH];
    // How long the page stays available after playback stops, so a brief pause does not
    // yank the screen away. 0 = never expire.
    uint16_t idleMinutes;
    bool showArt;
};

struct SpotifyRuntime {
    bool hasCredentials;
    bool playing;
    bool hasTrack;
    char trackName[SPOTIFY_TEXT_LENGTH];
    char artistName[SPOTIFY_TEXT_LENGTH];
    uint32_t progressMs;
    uint32_t durationMs;
    // millis() when progressMs was sampled, so the bar can advance between polls without
    // asking Spotify more often than the rate limit allows.
    unsigned long progressSampledMs;
    // From /v1/me/player, which /currently-playing does not carry.
    char deviceName[28];
    int volumePercent;    // -1 when unknown
    bool shuffle;
    char repeatMode[8];   // "off", "track", "context"
    // Round-trip time of the last successful poll, in ms - one of the real numbers the
    // telemetry strip shows in place of a fake audio visualiser.
    uint16_t lastPollLatencyMs;
    char artUrl[80];      // i.scdn.co URLs measured at 64 chars
    char artTrackId[24];  // which track the file at SPOTIFY_ART_PATH belongs to
    bool artReady;
    unsigned long lastPollMs;
    unsigned long lastPlayingMs;
    unsigned long tokenExpiresAtMs;
    char lastError[64];
};

extern SpotifyConfig spotifyConfig;
extern SpotifyRuntime spotifyRuntime;

void spotifyInit();
void spotifyLoop();
bool spotifyLoadConfig();
bool spotifySaveConfig();
bool spotifyApplyConfigJson(const String &json, String *error = nullptr);
void spotifyBuildConfigJson(String &json);
void spotifyBuildStatusJson(String &json);

// True while a track is playing, or within idleMinutes of the last one - this is what
// makes the Spotify page "available" to the rotation.
bool spotifyPageAvailable();
// Seconds of progress right now, interpolated from the last poll.
uint32_t spotifyProgressMs();

#endif
