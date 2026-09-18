#ifndef SPOTIFY_URL_H
#define SPOTIFY_URL_H

// Split out of spotify.cpp so it can be compiled and exercised on a PC: it writes
// network-supplied bytes into a fixed buffer, which is worth a runnable check.
// See tools/test_spotify_url.cpp.

#include <stddef.h>
#include <string.h>

// Splits "https://i.scdn.co/image/ab67..." into a host buffer and a pointer to the path.
// No allocation: this runs next to a live TLS connection where heap is scarce.
// Returns false - writing nothing - for anything that is not https://host/path, or for a
// host that would not fit, so the caller never sees a half-filled buffer.
inline bool splitArtUrl(const char *url, char *host, size_t hostSize, const char **path) {
    constexpr size_t kSchemeLength = 8;  // "https://"
    if (url == nullptr || host == nullptr || hostSize == 0 || path == nullptr) {
        return false;
    }
    if (strncmp(url, "https://", kSchemeLength) != 0) {
        return false;
    }
    const char *hostStart = url + kSchemeLength;
    const char *slash = strchr(hostStart, '/');
    if (slash == nullptr) {
        return false;
    }
    size_t hostLength = static_cast<size_t>(slash - hostStart);
    if (hostLength == 0 || hostLength >= hostSize) {
        return false;
    }
    memcpy(host, hostStart, hostLength);
    host[hostLength] = '\0';
    *path = slash;
    return true;
}

#endif
