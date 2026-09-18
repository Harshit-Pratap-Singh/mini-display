// Runnable check for the album-art URL parser. Compiles on the PC, no hardware:
//     c++ -std=c++17 -Wall -Wextra -o /tmp/t tools/test_spotify_url.cpp && /tmp/t
// Add -fsanitize=address to prove the buffer bound rather than infer it, where the
// sandbox allows ASan its shadow mapping.
#include "../src/spotify_url.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    char host[48];
    const char *path = nullptr;

    // The real shape, as measured from the API on 2026-09-18.
    assert(splitArtUrl("https://i.scdn.co/image/ab67616d00001e02ff9ca10b55ce82ae553c8228",
                       host, sizeof(host), &path));
    assert(strcmp(host, "i.scdn.co") == 0);
    assert(strcmp(path, "/image/ab67616d00001e02ff9ca10b55ce82ae553c8228") == 0);

    // A bare root path is still a path.
    assert(splitArtUrl("https://h/", host, sizeof(host), &path));
    assert(strcmp(host, "h") == 0 && strcmp(path, "/") == 0);

    // Query strings belong to the path, not the host.
    assert(splitArtUrl("https://h.co/a?b=c", host, sizeof(host), &path));
    assert(strcmp(host, "h.co") == 0 && strcmp(path, "/a?b=c") == 0);

    // Rejections. Each must leave the buffer untouched rather than half-filled.
    const char *bad[] = {
        "",                       // empty
        "http",                   // shorter than the scheme
        "http://i.scdn.co/x",     // plain HTTP: we only ever speak TLS here
        "https://i.scdn.co",      // no path
        "https:///x",             // empty host
        "https://",               // nothing at all after the scheme
    };
    for (const char *url : bad) {
        memset(host, '#', sizeof(host));
        const char *untouched = nullptr;
        assert(!splitArtUrl(url, host, sizeof(host), &untouched));
        assert(untouched == nullptr);
        assert(host[0] == '#');
    }
    assert(!splitArtUrl(nullptr, host, sizeof(host), &path));

    // The one that matters: a host longer than the buffer must be refused, not truncated
    // and never memcpy'd past the end. ASan turns a regression here into a crash.
    char small[10];
    memset(small, '#', sizeof(small));
    assert(!splitArtUrl("https://averylonghostname.example.com/x", small, sizeof(small), &path));
    assert(small[0] == '#');
    // The exact boundary the guard is written on, both sides of it. sizeof(small) is 10,
    // so a 9-character host is the longest that still leaves room for the NUL.
    assert(splitArtUrl("https://123456789/x", small, sizeof(small), &path));
    assert(strcmp(small, "123456789") == 0);
    memset(small, '#', sizeof(small));
    assert(!splitArtUrl("https://1234567890/x", small, sizeof(small), &path));  // one over
    assert(small[0] == '#');

    printf("selftest OK: host/path split, scheme and empty-host rejected, buffer bound held\n");
    return 0;
}
