#include "spotify.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <umm_malloc/umm_heap_select.h>

#include "logger.h"
#include "spotify_url.h"

SpotifyConfig spotifyConfig;
SpotifyRuntime spotifyRuntime;

namespace {

constexpr char kConfigTempPath[] = "/spotify-config.tmp";

void copyString(char *destination, size_t size, const char *source) {
    if (destination == nullptr || size == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
}

constexpr char kApiHost[] = "api.spotify.com";
constexpr char kAuthHost[] = "accounts.spotify.com";

// Measured on this board 2026-09-18 (HARDWARE.md, "TLS cost for Spotify"): one connection
// costs ~10.2 KB of DRAM whatever buffer is asked for, because the receive buffer comes
// from the IRAM second heap. rx512 FAILS with BR_ERR_TOO_LARGE - Spotify offers no MFLN -
// so 4096 is the safe floor, and it costs nothing extra.
constexpr uint16_t kTlsRxBuffer = 4096;
constexpr uint16_t kTlsTxBuffer = 512;

// A poll blocks loop() for ~1.4 s even with session resumption - the ECDHE key exchange
// dominates on an 80 MHz core and no caching removes it. Polling every 5 s would freeze
// the display for a quarter of every cycle, so the interval adapts instead: slow while a
// track plays out (the progress bar interpolates locally and needs no help), fast only in
// the window where the track is about to change and a poll would actually learn something.
constexpr unsigned long kPollWhilePlayingMs = 15000;
constexpr unsigned long kPollNearTrackEndMs = 5000;
constexpr unsigned long kTrackEndWindowMs = 20000;
constexpr unsigned long kPollWhileIdleMs = 30000;   // nothing playing: stop hammering
constexpr unsigned long kPollAfterErrorMs = 60000;
constexpr unsigned long kPollAfterTransientMs = 15000;
// Access tokens last 3600 s (measured). Refresh early so a poll never races expiry.
constexpr unsigned long kTokenLifetimeMs = 3300000UL;

// Album art. copyPreferredArtUrl() asks for the <=300 px image, measured at ~40 KB; the
// cap only exists so a surprise 640 px URL cannot fill the filesystem. Three failures on
// one track and we stop retrying: a URL that 404s will not start working, and each retry
// costs another 1.4 s handshake.
constexpr uint32_t kArtMaxBytes = 80000;
constexpr unsigned long kArtStallMs = 8000;   // no bytes for this long = give up
constexpr unsigned long kArtRetryMs = 30000;
constexpr uint8_t kArtMaxFailures = 3;
// An art download and the next poll otherwise land on consecutive passes of loop(),
// microseconds apart. That is not two TLS connections at once, but it gives lwIP and
// BearSSL no time to release the first 10 KB before the second is asked for, and the
// symptom is a mid-parse allocation failure reported as "parse failed".
constexpr unsigned long kPostArtSettleMs = 1500;

// The full /v1/me/player payload is 2.6 KB+ and we render a dozen fields of it, so the
// parse is always filtered. Held for the life of the program rather than rebuilt per
// poll: the rebuild churned the heap at exactly the wrong moment.
JsonDocument pollFilter;

void buildPollFilter() {
    pollFilter.clear();
    pollFilter["is_playing"] = true;
    pollFilter["progress_ms"] = true;
    pollFilter["shuffle_state"] = true;
    pollFilter["repeat_state"] = true;
    pollFilter["device"]["name"] = true;
    pollFilter["device"]["volume_percent"] = true;
    pollFilter["item"]["name"] = true;
    pollFilter["item"]["duration_ms"] = true;
    pollFilter["item"]["id"] = true;
    pollFilter["item"]["artists"][0]["name"] = true;
    pollFilter["item"]["album"]["images"][0]["url"] = true;
    pollFilter["item"]["album"]["images"][0]["width"] = true;
    pollFilter.shrinkToFit();
}

char accessToken[SPOTIFY_ACCESS_TOKEN_LENGTH];
unsigned long nextPollMs = 0;
unsigned long nextArtMs = 0;
uint8_t artFailures = 0;

// A full TLS handshake blocks loop() for ~2.6 s (measured on this board), which at a 5 s
// poll would stall the display and web server for half of every cycle. A cached session
// lets BearSSL do an abbreviated handshake on reconnect. One per host, because a session
// is only valid for the server that issued it.
BearSSL::Session apiSession;
BearSSL::Session authSession;
BearSSL::Session artSession;

void freeHeaps(uint32_t &dram, uint32_t &iram) {
    { HeapSelectDram scope; dram = ESP.getFreeHeap(); }
    { HeapSelectIram scope; iram = ESP.getFreeHeap(); }
}

void setError(const char *message) {
    strncpy(spotifyRuntime.lastError, message, sizeof(spotifyRuntime.lastError) - 1);
    spotifyRuntime.lastError[sizeof(spotifyRuntime.lastError) - 1] = '\0';
}

// Reads the status line and consumes headers, returning the HTTP status. Sets
// retryAfterSeconds from a Retry-After header, which Spotify sends with 429.
// HTTP header names are case-insensitive and Spotify's CDN does not spell them the same
// way the API does, so compare without case rather than with startsWith().
bool headerIs(const String &line, const char *name) {
    size_t length = strlen(name);
    if (line.length() <= length || line[length] != ':') {
        return false;
    }
    return strncasecmp(line.c_str(), name, length) == 0;
}

int readHttpResponseHead(WiFiClient &client, int *retryAfterSeconds,
                         long *contentLength = nullptr) {
    String statusLine = client.readStringUntil('\n');
    int firstSpace = statusLine.indexOf(' ');
    if (firstSpace < 0) {
        return -1;
    }
    int status = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();

    // connected() goes false as soon as the server closes, while the response is still
    // buffered locally - drain available() too or the read races the close.
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() == 0) {
            // Timed out or the socket closed mid-headers. The blank line that really ends
            // the headers is "\r\n", which arrives here as "\r", so an empty string is
            // never a legitimate end. Breaking here would hand the body parser a pile of
            // headers and report it as a parse failure.
            return -2;
        }
        line.trim();
        if (line.length() == 0) {
            break;  // blank line: headers done, body follows
        }
        if (retryAfterSeconds != nullptr && headerIs(line, "Retry-After")) {
            *retryAfterSeconds = line.substring(12).toInt();
        }
        if (contentLength != nullptr && headerIs(line, "Content-Length")) {
            *contentLength = line.substring(15).toInt();
        }
    }
    return status;
}

void configureTlsClient(BearSSL::WiFiClientSecure &client, BearSSL::Session &session) {
    client.setInsecure();  // no cert store on a 4 MB board; see CLAUDE.md
    client.setBufferSizes(kTlsRxBuffer, kTlsTxBuffer);
    client.setSession(&session);
    client.setTimeout(10000);
}

// Swaps the stored refresh token for a fresh access token. Spotify does NOT return a new
// refresh token (verified 2026-09-18), so nothing is written back to LittleFS here.
bool refreshAccessToken() {
    BearSSL::WiFiClientSecure client;
    configureTlsClient(client, authSession);
    if (!client.connect(kAuthHost, 443)) {
        uint32_t dram = 0;
        uint32_t iram = 0;
        freeHeaps(dram, iram);
        char detail[sizeof(spotifyRuntime.lastError)];
        snprintf(detail, sizeof(detail), "Auth d%u i%u e%d", dram, iram,
                 client.getLastSSLError());
        setError(detail);
        logPrintf("Spotify auth connect failed: ssl %d, DRAM %u, IRAM %u",
                  client.getLastSSLError(), dram, iram);
        return false;
    }

    char body[SPOTIFY_REFRESH_TOKEN_LENGTH + 48];
    int bodyLength = snprintf(body, sizeof(body),
                              "grant_type=refresh_token&refresh_token=%s",
                              spotifyConfig.refreshToken);
    char credentials[SPOTIFY_CLIENT_ID_LENGTH + SPOTIFY_CLIENT_SECRET_LENGTH + 2];
    snprintf(credentials, sizeof(credentials), "%s:%s",
             spotifyConfig.clientId, spotifyConfig.clientSecret);
    String basic = base64::encode(String(credentials));  // the one String we cannot avoid

    client.print(F("POST /api/token HTTP/1.1\r\nHost: "));
    client.print(kAuthHost);
    client.print(F("\r\nAuthorization: Basic "));
    client.print(basic);
    client.print(F("\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: "));
    client.print(bodyLength);
    client.print(F("\r\nConnection: close\r\n\r\n"));
    client.print(body);

    int status = readHttpResponseHead(client, nullptr);
    if (status != 200) {
        client.stop();
        setError(status == 400 ? "Auth rejected - re-run spotify_auth.py" : "Auth HTTP error");
        logPrintf("Spotify token refresh failed: HTTP %d", status);
        return false;
    }

    JsonDocument filter;
    filter["access_token"] = true;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    client.stop();
    if (error || doc["access_token"].isNull()) {
        setError("Auth parse failed");
        return false;
    }

    strncpy(accessToken, doc["access_token"], sizeof(accessToken) - 1);
    accessToken[sizeof(accessToken) - 1] = '\0';
    spotifyRuntime.tokenExpiresAtMs = millis() + kTokenLifetimeMs;
    setError("");
    logPrint(F("Spotify access token refreshed"));
    return true;
}

void copyFirstArtist(JsonArrayConst artists) {
    spotifyRuntime.artistName[0] = '\0';
    for (JsonObjectConst artist : artists) {
        const char *name = artist["name"];
        if (name == nullptr) {
            continue;
        }
        if (spotifyRuntime.artistName[0] != '\0') {
            strncat(spotifyRuntime.artistName, ", ",
                    sizeof(spotifyRuntime.artistName) - strlen(spotifyRuntime.artistName) - 1);
        }
        strncat(spotifyRuntime.artistName, name,
                sizeof(spotifyRuntime.artistName) - strlen(spotifyRuntime.artistName) - 1);
    }
}

// Picks the 300x300 image: TJpg renders it at scale 2 = 150 px, for ~40 KB. The 640 is
// 144 KB (seconds of blocking download) and the 64 is tiny on a 240 px panel.
void copyPreferredArtUrl(JsonArrayConst images) {
    const char *best = nullptr;
    int bestWidth = 0;
    for (JsonObjectConst image : images) {
        int width = image["width"] | 0;
        const char *url = image["url"];
        if (url == nullptr) {
            continue;
        }
        // Prefer the largest image that is still <= 300 px wide.
        if (width <= 300 && width > bestWidth) {
            bestWidth = width;
            best = url;
        }
    }
    if (best != nullptr) {
        strncpy(spotifyRuntime.artUrl, best, sizeof(spotifyRuntime.artUrl) - 1);
        spotifyRuntime.artUrl[sizeof(spotifyRuntime.artUrl) - 1] = '\0';
    }
}

void markNothingPlaying() {
    spotifyRuntime.playing = false;
    // hasTrack is deliberately left alone: the page stays available for idleMinutes so a
    // brief pause does not yank the screen away mid-song.
}

bool fetchNowPlaying() {
    unsigned long startedMs = millis();
    BearSSL::WiFiClientSecure client;
    configureTlsClient(client, apiSession);
    if (!client.connect(kApiHost, 443)) {
        uint32_t dram = 0;
        uint32_t iram = 0;
        freeHeaps(dram, iram);
        char detail[sizeof(spotifyRuntime.lastError)];
        snprintf(detail, sizeof(detail), "Connect d%u i%u e%d", dram, iram,
                 client.getLastSSLError());
        setError(detail);
        logPrintf("Spotify API connect failed: ssl %d, DRAM %u, IRAM %u",
                  client.getLastSSLError(), dram, iram);
        return false;
    }

    client.print(F("GET /v1/me/player HTTP/1.1\r\nHost: "));
    client.print(kApiHost);
    client.print(F("\r\nAuthorization: Bearer "));
    client.print(accessToken);
    client.print(F("\r\nConnection: close\r\n\r\n"));

    int retryAfter = 0;
    long bodyLength = -1;
    int status = readHttpResponseHead(client, &retryAfter, &bodyLength);

    if (status == 204) {
        // Nothing playing. There is NO body - parsing here would hang until timeout.
        client.stop();
        markNothingPlaying();
        setError("");
        spotifyRuntime.lastPollLatencyMs = static_cast<uint16_t>(millis() - startedMs);
        return true;
    }
    if (status == 401) {
        client.stop();
        spotifyRuntime.tokenExpiresAtMs = 0;  // force a refresh on the next pass
        setError("Token expired");
        return false;
    }
    if (status == 429) {
        client.stop();
        nextPollMs = millis() + (retryAfter > 0 ? (retryAfter * 1000UL) : 30000UL);
        setError("Rate limited");
        logPrintf("Spotify rate limited, backing off %ds", retryAfter > 0 ? retryAfter : 30);
        return false;
    }
    if (status != 200) {
        client.stop();
        char detail[sizeof(spotifyRuntime.lastError)];
        if (status == -2) {
            strcpy(detail, "Headers timed out");
        } else if (status == -1) {
            // Connected, then nothing readable came back. Not an HTTP error at all -
            // the socket died before the status line, so calling it one misleads.
            strcpy(detail, "No response");
        } else {
            snprintf(detail, sizeof(detail), "HTTP %d", status);
        }
        setError(detail);
        uint32_t dram = 0;
        uint32_t iram = 0;
        freeHeaps(dram, iram);
        logPrintf("Spotify /me/player HTTP %d (DRAM %u, IRAM %u)", status, dram, iram);
        // 5xx and dropped sockets are transient; a minute of stale screen for a blip is
        // too long. Client errors are our fault and will not fix themselves, so those
        // keep the full backoff.
        bool transient = (status < 0) || (status >= 500);
        nextPollMs = millis() + (transient ? kPollAfterTransientMs : kPollAfterErrorMs);
        return false;
    }

    JsonDocument doc;
    uint32_t heapBeforeParse = 0;
    uint32_t iramBeforeParse = 0;
    freeHeaps(heapBeforeParse, iramBeforeParse);
    spotifyRuntime.lastPollDramBytes = heapBeforeParse;
    spotifyRuntime.lastPollIramBytes = iramBeforeParse;
    DeserializationError error = deserializeJson(doc, client,
                                                DeserializationOption::Filter(pollFilter));
    client.stop();
    if (error) {
        char detail[sizeof(spotifyRuntime.lastError)];
        snprintf(detail, sizeof(detail), "Parse: %s", error.c_str());
        setError(detail);
        logPrintf("Spotify parse failed: %s (DRAM %u, IRAM %u at parse, body %ld B)",
                  error.c_str(), heapBeforeParse, iramBeforeParse, bodyLength);
        return false;
    }

    JsonObjectConst item = doc["item"];
    if (item.isNull()) {
        markNothingPlaying();
        setError("");
        return true;
    }

    spotifyRuntime.playing = doc["is_playing"] | false;
    spotifyRuntime.progressMs = doc["progress_ms"] | 0;
    spotifyRuntime.progressSampledMs = millis();
    spotifyRuntime.durationMs = item["duration_ms"] | 0;
    spotifyRuntime.shuffle = doc["shuffle_state"] | false;
    copyString(spotifyRuntime.repeatMode, sizeof(spotifyRuntime.repeatMode), doc["repeat_state"] | "off");
    copyString(spotifyRuntime.deviceName, sizeof(spotifyRuntime.deviceName), doc["device"]["name"] | "");
    spotifyRuntime.volumePercent = doc["device"]["volume_percent"] | -1;
    copyString(spotifyRuntime.trackName, sizeof(spotifyRuntime.trackName), item["name"] | "");
    copyFirstArtist(item["artists"].as<JsonArrayConst>());

    const char *trackId = item["id"] | "";
    if (strcmp(trackId, spotifyRuntime.artTrackId) != 0) {
        // New track: the cached art file no longer matches.
        spotifyRuntime.artReady = false;
        copyString(spotifyRuntime.artTrackId, sizeof(spotifyRuntime.artTrackId), trackId);
        spotifyRuntime.artUrl[0] = '\0';
        copyPreferredArtUrl(item["album"]["images"].as<JsonArrayConst>());
        artFailures = 0;
        nextArtMs = millis();
    }

    spotifyRuntime.hasTrack = spotifyRuntime.trackName[0] != '\0';
    if (spotifyRuntime.playing) {
        spotifyRuntime.lastPlayingMs = millis();
    }
    spotifyRuntime.lastPollLatencyMs = static_cast<uint16_t>(millis() - startedMs);
    setError("");
    {
        uint32_t dram = 0;
        uint32_t iram = 0;
        freeHeaps(dram, iram);
        logPrintf("Spotify poll OK %ums, DRAM %u, IRAM %u",
                  spotifyRuntime.lastPollLatencyMs, dram, iram);
    }
    return true;
}

// Streams the album art JPEG straight to LittleFS - never into RAM, there is no room for
// 40 KB. Called on its own pass of spotifyLoop() so it never overlaps the track poll.
bool downloadArt() {
    char host[48];
    const char *path = nullptr;
    if (!splitArtUrl(spotifyRuntime.artUrl, host, sizeof(host), &path)) {
        setError("Bad art URL");
        return false;
    }

    unsigned long startedMs = millis();
    BearSSL::WiFiClientSecure client;
    configureTlsClient(client, artSession);
    if (!client.connect(host, 443)) {
        uint32_t dram = 0;
        uint32_t iram = 0;
        freeHeaps(dram, iram);
        setError("Art connect failed");
        logPrintf("Spotify art connect failed: ssl %d, DRAM %u, IRAM %u",
                  client.getLastSSLError(), dram, iram);
        return false;
    }

    client.print(F("GET "));
    client.print(path);
    client.print(F(" HTTP/1.1\r\nHost: "));
    client.print(host);
    client.print(F("\r\nConnection: close\r\n\r\n"));

    long contentLength = -1;
    int status = readHttpResponseHead(client, nullptr, &contentLength);
    if (status != 200) {
        client.stop();
        setError("Art HTTP error");
        logPrintf("Spotify art HTTP %d", status);
        return false;
    }
    if (contentLength > static_cast<long>(kArtMaxBytes)) {
        client.stop();
        setError("Art too large");
        logPrintf("Spotify art %ld B exceeds the %u B cap", contentLength, kArtMaxBytes);
        return false;
    }

    File file = LittleFS.open(SPOTIFY_ART_PATH, "w");
    if (!file) {
        client.stop();
        setError("Art open failed");
        return false;
    }

    // 512 B a time: large enough that the loop is not read-bound, small enough to sit on
    // the stack beside a live TLS connection.
    uint8_t buffer[512];
    uint32_t written = 0;
    bool overflow = false;
    bool writeFailed = false;
    unsigned long stallDeadline = millis() + kArtStallMs;

    while (contentLength < 0 || written < static_cast<uint32_t>(contentLength)) {
        int chunk = client.read(buffer, sizeof(buffer));
        if (chunk > 0) {
            if (written + static_cast<uint32_t>(chunk) > kArtMaxBytes) {
                overflow = true;
                break;
            }
            if (file.write(buffer, static_cast<size_t>(chunk)) != static_cast<size_t>(chunk)) {
                writeFailed = true;  // filesystem full
                break;
            }
            written += static_cast<uint32_t>(chunk);
            stallDeadline = millis() + kArtStallMs;  // progress resets the clock
            continue;
        }
        if (!client.connected() && client.available() == 0) {
            break;  // server closed; contentLength check below decides if that was clean
        }
        if (static_cast<long>(millis() - stallDeadline) >= 0) {
            break;
        }
        delay(1);  // yields to the WiFi stack rather than spinning
    }

    file.close();
    client.stop();

    // A truncated JPEG decodes to garbage, so treat a short read as failure and delete
    // the file rather than leaving half an image for the renderer to find.
    const char *failure = nullptr;
    if (writeFailed) {
        failure = "Art write failed";
    } else if (overflow) {
        failure = "Art too large";
    } else if (written == 0) {
        failure = "Art empty";
    } else if (contentLength > 0 && written != static_cast<uint32_t>(contentLength)) {
        failure = "Art truncated";
    }
    if (failure != nullptr) {
        LittleFS.remove(SPOTIFY_ART_PATH);
        setError(failure);
        logPrintf("Spotify art failed: %s (%u of %ld B)", failure, written, contentLength);
        return false;
    }

    spotifyRuntime.artReady = true;
    setError("");
    logPrintf("Spotify art %u B in %lu ms", written, millis() - startedMs);
    return true;
}

bool artDownloadDue() {
    if (!spotifyConfig.showArt || spotifyRuntime.artReady) {
        return false;
    }
    if (spotifyRuntime.artUrl[0] == '\0' || artFailures >= kArtMaxFailures) {
        return false;
    }
    return static_cast<long>(millis() - nextArtMs) >= 0;
}

void setConfigDefaults() {
    memset(&spotifyConfig, 0, sizeof(spotifyConfig));
    spotifyConfig.version = SPOTIFY_CONFIG_VERSION;
    spotifyConfig.enabled = false;
    spotifyConfig.idleMinutes = 5;  // a short pause should not yank the page away
    spotifyConfig.showArt = true;
}

void normalizeConfig() {
    spotifyConfig.version = SPOTIFY_CONFIG_VERSION;
    spotifyConfig.idleMinutes = constrain(spotifyConfig.idleMinutes, 0, 1440);
    spotifyConfig.clientId[sizeof(spotifyConfig.clientId) - 1] = '\0';
    spotifyConfig.clientSecret[sizeof(spotifyConfig.clientSecret) - 1] = '\0';
    spotifyConfig.refreshToken[sizeof(spotifyConfig.refreshToken) - 1] = '\0';
}

bool credentialsPresent() {
    return spotifyConfig.clientId[0] != '\0' &&
           spotifyConfig.clientSecret[0] != '\0' &&
           spotifyConfig.refreshToken[0] != '\0';
}

void applyConfigObject(JsonObjectConst root) {
    if (root.isNull()) {
        return;
    }
    if (!root["enabled"].isNull()) {
        spotifyConfig.enabled = root["enabled"].as<bool>();
    }
    if (!root["clientId"].isNull()) {
        copyString(spotifyConfig.clientId, sizeof(spotifyConfig.clientId), root["clientId"]);
    }
    // An empty secret or refresh token means "leave what is stored alone": the dashboard
    // never sends them back, so saving any other setting must not wipe them.
    const char *secret = root["clientSecret"];
    if (secret != nullptr && secret[0] != '\0') {
        copyString(spotifyConfig.clientSecret, sizeof(spotifyConfig.clientSecret), secret);
    }
    const char *refresh = root["refreshToken"];
    if (refresh != nullptr && refresh[0] != '\0') {
        copyString(spotifyConfig.refreshToken, sizeof(spotifyConfig.refreshToken), refresh);
    }
    if (!root["idleMinutes"].isNull()) {
        spotifyConfig.idleMinutes = static_cast<uint16_t>(
            constrain(root["idleMinutes"].as<int>(), 0, 1440));
    }
    if (!root["showArt"].isNull()) {
        spotifyConfig.showArt = root["showArt"].as<bool>();
    }
    normalizeConfig();
}

}  // namespace

void spotifyInit() {
    setConfigDefaults();
    memset(&spotifyRuntime, 0, sizeof(spotifyRuntime));
    if (!spotifyLoadConfig()) {
        spotifySaveConfig();
    }
    buildPollFilter();  // once, while the heap is still clean
    spotifyRuntime.hasCredentials = credentialsPresent();
    logPrintf("Spotify: %s", spotifyConfig.enabled
                                 ? (spotifyRuntime.hasCredentials ? "enabled" : "enabled but no credentials")
                                 : "disabled");
}

bool spotifyLoadConfig() {
    File file = LittleFS.open(SPOTIFY_CONFIG_PATH, "r");
    if (!file) {
        return false;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        logPrintf("Spotify config parse failed: %s", error.c_str());
        return false;
    }
    applyConfigObject(doc.as<JsonObjectConst>());
    spotifyRuntime.hasCredentials = credentialsPresent();
    return true;
}

bool spotifySaveConfig() {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["version"] = spotifyConfig.version;
    root["enabled"] = spotifyConfig.enabled;
    root["clientId"] = spotifyConfig.clientId;
    root["clientSecret"] = spotifyConfig.clientSecret;
    root["refreshToken"] = spotifyConfig.refreshToken;
    root["idleMinutes"] = spotifyConfig.idleMinutes;
    root["showArt"] = spotifyConfig.showArt;

    // Write to a temp file and rename: a power cut mid-write would otherwise leave a
    // truncated file and lose credentials that took a browser round trip to obtain.
    LittleFS.remove(kConfigTempPath);
    File file = LittleFS.open(kConfigTempPath, "w");
    if (!file) {
        return false;
    }
    bool written = serializeJson(doc, file) > 0;
    file.flush();
    file.close();
    if (!written) {
        LittleFS.remove(kConfigTempPath);
        return false;
    }
    LittleFS.remove(SPOTIFY_CONFIG_PATH);
    return LittleFS.rename(kConfigTempPath, SPOTIFY_CONFIG_PATH);
}

bool spotifyApplyConfigJson(const String &json, String *error) {
    JsonDocument doc;
    DeserializationError parseError = deserializeJson(doc, json);
    if (parseError) {
        if (error != nullptr) {
            *error = String("Invalid JSON: ") + parseError.c_str();
        }
        return false;
    }
    applyConfigObject(doc.as<JsonObjectConst>());
    spotifyRuntime.hasCredentials = credentialsPresent();
    if (!spotifySaveConfig()) {
        if (error != nullptr) {
            *error = "Failed to save Spotify config";
        }
        return false;
    }
    return true;
}

void spotifyBuildConfigJson(String &json) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["enabled"] = spotifyConfig.enabled;
    root["clientId"] = spotifyConfig.clientId;
    // The secret and refresh token are never sent back to the browser. The UI shows
    // whether they are set, which is all it needs to render a sensible state.
    root["hasClientSecret"] = spotifyConfig.clientSecret[0] != '\0';
    root["hasRefreshToken"] = spotifyConfig.refreshToken[0] != '\0';
    root["idleMinutes"] = spotifyConfig.idleMinutes;
    root["showArt"] = spotifyConfig.showArt;
    serializeJson(doc, json);
}

void spotifyBuildStatusJson(String &json) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["enabled"] = spotifyConfig.enabled;
    root["hasCredentials"] = spotifyRuntime.hasCredentials;
    root["playing"] = spotifyRuntime.playing;
    root["hasTrack"] = spotifyRuntime.hasTrack;
    root["track"] = spotifyRuntime.trackName;
    root["artist"] = spotifyRuntime.artistName;
    root["progressMs"] = spotifyProgressMs();
    root["durationMs"] = spotifyRuntime.durationMs;
    root["artReady"] = spotifyRuntime.artReady;
    root["pollMs"] = spotifyRuntime.lastPollLatencyMs;
    // Milliseconds until the next poll, so the adaptive interval is observable without
    // a serial cable: 30000 idle, 15000 mid-track, 5000 in the last 20 s of a track.
    long untilPoll = static_cast<long>(nextPollMs - millis());
    root["nextPollInMs"] = untilPoll > 0 ? untilPoll : 0;
    root["lastError"] = spotifyRuntime.lastError;
    root["available"] = spotifyPageAvailable();
    serializeJson(doc, json);
}

uint32_t spotifyProgressMs() {
    if (!spotifyRuntime.hasTrack) {
        return 0;
    }
    uint32_t progress = spotifyRuntime.progressMs;
    if (spotifyRuntime.playing) {
        // Interpolate between polls so the bar moves smoothly without asking Spotify
        // more often than the rate limit allows.
        progress += (millis() - spotifyRuntime.progressSampledMs);
    }
    return (spotifyRuntime.durationMs > 0 && progress > spotifyRuntime.durationMs)
               ? spotifyRuntime.durationMs
               : progress;
}

bool spotifyPageAvailable() {
    if (!spotifyConfig.enabled || !spotifyRuntime.hasCredentials || !spotifyRuntime.hasTrack) {
        return false;
    }
    if (spotifyRuntime.playing) {
        return true;
    }
    if (spotifyConfig.idleMinutes == 0) {
        return true;  // never expire
    }
    unsigned long idleMs = static_cast<unsigned long>(spotifyConfig.idleMinutes) * 60000UL;
    return (millis() - spotifyRuntime.lastPlayingMs) < idleMs;
}

// How long to wait before the next poll. Costed against the ~1.4 s the poll blocks for.
unsigned long nextPollDelayMs() {
    if (!spotifyRuntime.playing) {
        return kPollWhileIdleMs;
    }
    uint32_t progress = spotifyProgressMs();
    if (spotifyRuntime.durationMs > progress &&
        (spotifyRuntime.durationMs - progress) <= kTrackEndWindowMs) {
        return kPollNearTrackEndMs;  // a change is imminent; worth the stall
    }
    return kPollWhilePlayingMs;
}

void spotifyLoop() {
    if (!spotifyConfig.enabled || !spotifyRuntime.hasCredentials) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    // Art downloads on its own pass. CLAUDE.md forbids two TLS connections at once, and
    // back to back they would block loop() for ~3 s; one per pass keeps that serialised.
    if (artDownloadDue()) {
        if (!downloadArt()) {
            artFailures++;
            nextArtMs = millis() + kArtRetryMs;
        }
        unsigned long settleUntil = millis() + kPostArtSettleMs;
        if (static_cast<long>(settleUntil - nextPollMs) > 0) {
            nextPollMs = settleUntil;  // never bring the poll forward, only push it back
        }
        return;
    }

    if (static_cast<long>(millis() - nextPollMs) < 0) {
        return;
    }

    // One TLS connection at a time, never two - see CLAUDE.md. Refresh first if due; the
    // poll then happens on the next pass rather than opening a second connection here.
    if (accessToken[0] == '\0' || static_cast<long>(millis() - spotifyRuntime.tokenExpiresAtMs) >= 0) {
        if (!refreshAccessToken()) {
            nextPollMs = millis() + kPollAfterErrorMs;
        }
        return;
    }

    bool ok = fetchNowPlaying();
    if (!ok) {
        // 429 and the HTTP-status path set their own backoff. Only cover the failures
        // that did not - connect and parse errors - and give those the full minute.
        if (static_cast<long>(millis() - nextPollMs) >= 0) {
            nextPollMs = millis() + kPollAfterErrorMs;
        }
        return;
    }
    nextPollMs = millis() + nextPollDelayMs();
}
