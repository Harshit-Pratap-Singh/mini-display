# HARDWARE.md — verified facts (2026-09-16)

Single source of truth for pins, display settings, flash layout and the base-repo decision. Anything marked UNVERIFIED is still the CLAUDE.md hypothesis.

## Serial / USB
- USB-serial IC: **CH340** (USB VID 0x1A86, PID 0x7523), macOS 26 native driver. Port `/dev/cu.usbserial-10` (suffix may change with the USB port used).
- Auto-reset into the bootloader works (DTR/RTS). No GPIO0 jumper needed for esptool.
- **Baud: use 115200.** 921600 and 460800 both fail on this CH340 under macOS (data corruption right after the baud switch). Full 4 MB read ≈ 6.5 min.
- If the board does not enumerate at all (`ioreg -p IOUSB` empty), it is the cable/port, not a driver: re-plug or use a USB-A→C cable via a hub.
- Board variant differs from iodn's photo (purple ESP-12F board with a 6-pin UART header): ours is the blue bare-ESP8266EX `V2.6` PCB with on-board USB-serial, so the UART pads are not needed.

## MCU
- **ESP8266EX**, 26 MHz crystal, 160 MHz capable. MAC `8c:4f:00:e1:cf:2e`, chip ID `0x00e1cf2e`.

## Flash
- **4 MB** (4,194,304 bytes). JEDEC manufacturer `0x5E` (Zbit), device `0x4016` (32 Mbit).
- Stock image header: flash mode DIO, 40 MHz, size 4MB (`esptool image-info`).
- Used-block map from the backup (64 KB blocks): sketch ≈ `0x000000–0x110000`, FS data ≈ `0x150000–0x340000`, SDK system params / RF cal in the last 64 KB, everything else erased (0xFF).

## Backup
- File: `backup/stock_backup.bin`, **4,194,304 bytes == flash size**, read 2026-09-16 23:27 at 115200 baud.
- SHA-256: `74780e0a6a83c6de2e7f997042d4645de7f5db107f30636cf81a87b8c41feb1a`
- Content check: full-image `verify-flash` reported a digest mismatch one minute after the read (the stock firmware had rebooted and run in between). Per-region `verify-flash`: sketch `0x000000–0x110000` **matched**, FS data `0x150000–0x340000` **matched**, last 64 KB (`0x3F0000+`, SDK system params / RF cal) differs — expected, the SDK rewrites it on every boot and regenerates it after a restore. **Backup is a valid restore image.**
- Restore: `esptool --port /dev/cu.usbserial-10 -b 115200 write-flash 0 backup/stock_backup.bin`
- Vendor-proprietary. Git-ignored. Never commit or redistribute.

## Stock firmware — strings findings only (not reverse-engineered)
- Platform: ESP8266 Arduino core (LittleFS via `lfs.c`, `spiffs_api.h`, `ESP8266HTTPClient`, BearSSL), NONOS SDK "compiled @ Jul 3 2019" → Arduino core 2.5–2.7 era. Arduino-HomeKit-ESP8266 library present (`Pair-Verify`, "Firmware ONLY supports ESP8266!!!").
- Product name: **"Smart Weather Clock"**. Version strings: `HG V2.4.5`, `HG V3.1.1`, `SD V2.5.7`, `SDD V1.4.3`. No "GeekMagic" or "SmallTV" string → generic/clone firmware for this board family.
- Features seen: weather.com.cn (CN) and OpenWeatherMap weather, Coindesk BTC price (EUR/USD), worldtimeapi + `ntp6.aliyun.com` time, WiFi setup page with SSID scan, GIF/JPG upload ("Only converted GIF can be uploaded", Google-Drive conversion tool + YouTube tutorial links), LCD rotation, °F/mile switch, LED brightness. Web routes: `/index.html /indexkey.html /indexwifi.html /wifi /update /upload /updateCity /updateLEDBrightness /updateLedLabel`. Code lineage: RandomNerdTutorials.
- FS files: `BTC.jpg brid.jpg clock.jpg file1–5.jpg gif.jpg humidity.jpg hutao.jpg spaceman.jpg temperature.jpg totoro.jpg res.json`.

## Pins & display — VERIFIED 2026-09-16 (iodn firmware setup screen rendered correctly, colours right)
| Signal | GPIO | Source |
|---|---|---|
| SPI SCLK | 14 | CLAUDE.md hypothesis = iodn build flags |
| SPI MOSI | 13 | same |
| TFT DC | 0 | same (boot-mode pin) |
| TFT RST | 2 | same |
| TFT CS | none (GND) | same → SPI mode 3 |
| Backlight PWM | 5 | same |
| Button | — | **no user button on this hardware** (user-confirmed 2026-09-16); iodn reads GPIO4 with pull-up (`PIN_BUTTON`, `src/config.h`) — deleted 2026-09-17 in commit A; GPIO4 is now unused |

Display VERIFIED: ST7789, 240×240, no offsets, SPI mode 3 (TFT_eSPI default for ST7789; CS tied low), 40 MHz, `tft.invertDisplay(true)` in code, **default RGB order — the BGR hypothesis was wrong, do not add TFT_RGB_ORDER=TFT_BGR**.

## Base repo decision
- **Fork `iodn/geekmagic-tv-esp8266`** @ `d703c12` ("Initial firmware repository", firmware version 2.2.2 per `.bumpversion.toml`).
- License: **MIT**, `Copyright (c) 2026 B. van Weerd` → reuse permitted. `LICENSE` kept at repo root; credit in README (milestone 7).
- Layout: project root is the fork (`git remote upstream`, branch `main`).
- Its build-flag pin map matches the hypothesis exactly; author tested on the "GeekMagic SmallTV Ultra ESP8266 variant".
- Build for `esp12e` (2026-09-16): **SUCCESS**. `platformio.ini` addition: `[env:esp12e] extends = env:nodemcuv2, board = esp12e, board_build.ldscript = eagle.flash.4m2m.ld` (both envs defaulted to `4m1m` = 1 MB LittleFS; pinned to 4m2m for 2 MB per CLAUDE.md).

| Build metric (esp12e, 4m2m) | Value |
|---|---|
| Sketch flash | 892,567 / 1,044,464 B (85.5%) |
| firmware.bin | 896,720 B |
| Static RAM | 54,208 / 81,920 B (66.2%) |
| Cold build / relink | 146 s / 22 s |

- Toolchain: PlatformIO 6.2.0, espressif8266 platform, Arduino core 3.1.2 (`framework-arduinoespressif8266` 3.30102.0). Resolved libs: TFT_eSPI 2.5.43, tzapu/WiFiManager 2.0.17, ArduinoJson 7.4.3, TJpg_Decoder 1.1.0. Already uses `PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED`.
- Budget notes: ~150 KB sketch headroom for our additions (AnimatedGIF, Spotify client). BearSSL TLS is already linked by `feeds.cpp` (`setInsecure`/`setFingerprint`/`setBufferSizes`), so Spotify HTTPS costs little extra flash; heap is the constraint.
- **Flashed 2026-09-16** after `erase-flash`: `pio run -e esp12e -t upload` at 115200, 896,720 B written (546,750 compressed) in 53 s, hash verified.

## First boot of iodn firmware (esp12e, 2026-09-16) — serial log facts
- Boots normally; "Display init complete"; button initialised on GPIO4 (INPUT_PULLUP); LittleFS mounts and `/image/` is "cleared" — but `clearImageDirectory()` (`src/main.cpp:568-582`) passes the bare `dir.fileName()` to `LittleFS.remove()`, which targets the root, so uploaded images most likely survive reboots (static analysis 2026-09-17; confirm on hardware: expect `Failed to delete: <name>` in the boot log).
- No saved WiFi → failsafe AP `SmartClock-Setup`, 8-digit password regenerated every boot (read it from the display or serial), IP 192.168.4.1. Admin password for the dashboard is a generated 10-digit number, also shown on display/serial.
- Boot-failure counter: opening the serial port resets the board (DTR/RTS auto-reset). Two resets before the counter clears put it into "recovery mode" once (`BOOT_RECOVERY_THRESHOLD 2`, `src/settings.cpp`); the counter clears only **30 s after `setup()` completes** (`kBootSuccessConfirmMs`, `src/main.cpp`) and any reset type counts, so an upload auto-reset followed by opening the monitor within ~40 s yields one recovery boot. Recovery disables NTP/mDNS/OTA/feeds but not the web UI or `/update`. Five resets within 10 s of power-on trigger a factory reset (`POWER_CYCLE_THRESHOLD 5`). Wait >40 s after `Ready!` before opening the port.

| Free heap (`ESP.getFreeHeap()`, DRAM only; IRAM second heap not counted) | Bytes |
|---|---|
| after display init | 24,504 |
| after FS/auth/dashboard/feeds init | 24,288 |
| after WiFi setup (softAP mode) | 21,520 |
| after web server init | 17,296 |

- **RAM is tighter than CLAUDE.md's 40–50 KB estimate.** A BearSSL session (~20 KB) does not fit in this headroom as-is; milestone 6 will need trimming (fonts, page HTML, feeds we don't use) and/or the IRAM second heap. STA-mode numbers below.

## STA mode heap — measured 2026-09-17 (same esp12e build, saved home-WiFi credentials, DHCP IP 192.168.1.14)
Serial boot log, `ESP.getFreeHeap()` (DRAM only):

| Free heap, STA mode | Bytes |
|---|---|
| after display init | 24,504 |
| after FS/auth/dashboard/feeds init | 24,288 |
| after WiFi connected (STA, 1 attempt) | 23,144 |
| after web server init | 18,920 |
| after initial display render / time services | 18,920 |
| steady state after the ~60 s boot-stability window | 18,416 |

- STA saves only ~1.6 KB over softAP (18,920 vs 17,296 after web server init).
- Consequences visible in the log: mDNS **and ArduinoOTA** are deferred indefinitely (`Deferring mDNS, free heap too low: 18416`; gate `kOptionalServiceMinFreeHeapBytes = 28000`, `src/main.cpp:41`), and the clock page's sprite is skipped (`Clock sprite skipped, free heap too low: 18392`; gate `kClockSpriteMinFreeHeapBytes = 30000`, `src/display.cpp:44`). The firmware is already running in its own low-memory fallbacks on this board; only web `/update` OTA is testable until the gate is lowered after the strip.
- `GET /` (dashboard shell, no auth) is 139,092 B — the single biggest flash item after the libraries; strip-list sections and/or gzip are the lever.
- Boot-log capture without `pio device monitor`: `tools/bootlog.py [port] [seconds]` (pyserial, esptool-style hard reset = DTR off + RTS pulse, prints to stdout). Run it with Homebrew's esptool venv Python, which has pyserial: `/opt/homebrew/Cellar/esptool/5.4.0/libexec/bin/python tools/bootlog.py /dev/cu.usbserial-10 60`. One reset is safe once the counter has cleared (`Boot stability confirmed`, ~30 s after setup).
- Visual check (user, 2026-09-16): setup screen readable, colours correct, backlight on → SCLK 14 / MOSI 13 / DC 0 / RST 2 / no CS / BL 5 verified. **No user button exists**; mode switching is web-driven since commit A (CLAUDE.md Mode logic: page chips + `POST /page`).

## Milestone 3 — commit A verified on hardware 2026-09-17
Firmware: our build with the button removed and `POST /page` added. Flashed over USB at 115200 (897,984 B, hash verified). The web `/update` OTA path was **not** exercised — still untested on this board.

Baseline checks on the pre-commit-A build (STA, 192.168.1.14):

| Check | Result |
|---|---|
| WiFi from saved credentials | connects on attempt 1, DHCP 192.168.1.14 |
| NTP | **working, but the stock single-server config was broken on this network** — see the NTP section below |
| Timezone | was `gmtOffset: 0` (UTC) out of the box → **set to 19800 (IST)**; `configTime(gmtOffset, 0, "pool.ntp.org")`, fixed offset, no DST |
| Settings persistence across reboot | **all survived**: brightness + gmtOffset (EEPROM), rotation interval + page toggles (`/dashboard-config.json`), widget data (`/dashboard-data.json`), WiFi creds (SDK store) |
| Settings persistence across a sketch flash | EEPROM and LittleFS both survive `write-flash` of the sketch region |
| LittleFS | 2,072,576 B total, 2,023,424 B free empty → the `eagle.flash.4m2m.ld` 2 MB layout is live |
| Image upload | `POST /image/upload` (multipart, field `file`) works; a 14,819 B JPEG consumed 32,768 B of FS |
| `/image/*` wiped at boot? | **No — images survive reboots.** Confirmed on hardware: free space was identical before and after a reboot with an image present. `clearImageDirectory()` (`src/main.cpp`) passes the bare `dir.fileName()` to `LittleFS.remove()`, which targets the root. Fix before milestone 5. |
| `POST /delete` | takes `{"file":"/image/x.jpg"}` (not `path`) |

`POST /page` contract, verified with Clock-only config:

| Request | Response |
|---|---|
| `?id=0` (Clock, ticked, has content) | 200 |
| `?id=1` (Weather ticked but feed disabled → no content) | 409 |
| `?id=7` (Quote, unticked) | 409 |
| `?id=9` (out of range) | 400 |
| `?id=abc`, `?id=` | 400 |
| no `id` (advance) | 200 |

Auto rotate with Clock + Status ticked and Weather ticked-but-empty: cycles Clock ↔ Status every 3 s and **skips Weather**, i.e. the renderer's content-availability gate and the `/page` gate agree. `/app.json` reports the live page.

| Free heap, STA, after commit A | Bytes | Δ vs before |
|---|---|---|
| after display init | 24,656 | +152 |
| after FS/auth/dashboard/feeds init | 24,440 | +152 |
| after WiFi connected | 23,296 | +152 |
| after web server init | 19,072 | +152 |
| steady state | 18,568 | +152 |

Build: flash 893,839 B (85.6%), static RAM 54,044 B (66.0%). The `Button initialized on GPIO4` line is gone from the boot log; GPIO4 is now unused. mDNS/ArduinoOTA are still deferred (19 KB < the 28,000 B gate) — lower it in commit B.

## NTP — one server was not enough (fixed 2026-09-17)
Symptom: the clock showed `Waiting for time` for ~15 minutes after a power cycle (`hasTimeSync()` = `dashboardCurrentEpoch() != 0`, i.e. `time(nullptr)` below the year-2000 floor).

Cause: upstream called `configTime(gmtOffset, 0, "pool.ntp.org")` with that **single** server. When the first SNTP request goes unanswered, lwIP doubles its retry delay each time (3, 6, 12, 24, 48, 96, then 180 s repeating), so recovery takes ~12-15 min. Measured on this board: epoch still 0 at 12 min, synced at ~15 min, twice.

Network evidence (same LAN, from the Mac): `sntp pool.ntp.org` **timed out**, while `time.google.com` and `time.cloudflare.com` answered in <20 ms. The individual pool IPs did answer when queried directly, so the pool is flaky here rather than blocked — one lost packet at boot is enough to start the backoff spiral.

Fix (commit after `238991f`):
- `NTP_SERVER` → `NTP_SERVERS` in `src/config.h` = `"time.google.com", "time.cloudflare.com", "pool.ntp.org"`, passed to all three `configTime()` call sites (`src/main.cpp`, `src/webserver.cpp` ×2). lwIP rotates servers, so one silent server no longer stalls the clock.
- `retryTimeSyncIfNeeded()` in `src/main.cpp` restarts SNTP once a minute while the clock is still unset, which resets the backoff. It returns immediately once `dashboardCurrentEpoch() != 0`, so there is no traffic after sync.

Result: 3 of 3 cold boots synced in **20-25 s**; the retry never had to fire. Cost +248 B flash, +44 B static RAM.

**Timezone:** the device shipped with `gmtOffset: 0` (UTC), so even a synced clock showed the wrong time. Set to **19800 (IST)** via the dashboard; it persists. `configTime()` here takes a fixed offset with DST 0, not a POSIX TZ string.

**Probing the device clock over HTTP** (no serial needed): `POST /dashboard/data {"focus":{"remainingSeconds":1500}}` makes the device stamp `dashboardCurrentEpoch()`, then read `data.focus.updatedAtEpoch` from `/dashboard.json`. It is 0 when unsynced.

## Optional services + image persistence (2026-09-17, commit `2c76796`)

**mDNS and ArduinoOTA now actually run.** Upstream gated them on ≥28,000 B free heap (`kOptionalServiceMinFreeHeapBytes`, `src/main.cpp`), which this board never reached, so they had never started once. Lowered to **12,000 B**, still clear of the HTTPS feeds' own 15,000 B floor (`kHttpsMinFreeHeapBytes`, `src/feeds.cpp`).

| Boot stage, STA | Free heap |
|---|---|
| after web server init | 22,008 |
| after time services init | 22,008 |
| after mDNS init | 21,320 (mDNS costs ~688 B) |
| after OTA init | 20,488 (ArduinoOTA costs ~832 B) |
| steady state | 20,344 |

Both services together cost ~1,520 B, and steady-state heap (20,344 B) is still ~1.8 KB **above** the pre-trim baseline of 18,568 B.

Verified on hardware:
- `smartclock-e1cf2e.local` resolves and pings (192.168.1.14) — first time on this board.
- **ArduinoOTA works**: `python ~/.platformio/packages/framework-arduinoespressif8266/tools/espota.py -i smartclock-e1cf2e.local -p 8266 --auth=<admin password> -f .pio/build/esp12e/firmware.bin -r` → `Authenticating...OK`, 100%, device rebooted and came back. Auth is the dashboard admin password (`ArduinoOTA.setPasswordHash(authOtaPasswordHash())` = its MD5).
- Web `/update` still not exercised from here (a local tooling permission blocks the upload); the route and page serve fine.

**Uploaded images now persist across reboots, on purpose.** `clearImageDirectory()` and the "clear stale image path" block are deleted from `src/main.cpp`. The function had never worked anyway (it passed a bare filename to `LittleFS.remove()`), and the photo frame in milestone 5 needs images to survive. Verified: uploaded a JPEG, rebooted, `POST /image/show` displayed it and `/app.json` reported the path; free space was identical before and after the reboot.

Note: there is **no GET route for `/image/<name>`** — files are referenced by path and shown on the device, not served over HTTP. A direct fetch returns 404 by design.

## Space budget — measured 2026-09-17 with a link map, and what we took back

Method: `PLATFORMIO_BUILD_FLAGS="-Wl,-Map=/tmp/fw.map" pio run -e esp12e`, then sum the loadable sections (`.text`, `.rodata`, `.irom.text`, `.irom0.text`, `.data`) per object. **The earlier estimates in this file were wrong in two places** — the dashboard HTML is far bigger than assumed, and SD support costs 22 KB, not 6.

Biggest items in the 894 KB sketch before trimming:

| Component | Bytes | Note |
|---|---|---|
| `index_html` (dashboard page) | 140,849 | one PROGMEM blob, 13.5% of the whole sketch slot |
| BearSSL | 113,318 | keep — Spotify needs TLS even after the HTTPS feeds go |
| lwIP | 91,588 | keep |
| Arduino core | 87,747 | keep |
| TFT_eSPI | 63,860 | keep |
| `feeds.cpp` | 51,290 | weather + markets + Home Assistant |
| WiFiManager | 54,761 | removed |
| `ota_html` | 17,834 | OTA upload page |
| mDNS | 28,110 | still linked, still never starts (heap gate) |
| `dashboard.cpp` | 29,488 | |
| ESP8266SdFat | 21,664 | removed |
| `display.cpp` | 19,724 | |

### Trim applied (commit after `6841011`) — 198,160 B of flash, no features lost
| Change | Saved |
|---|---|
| gzip `index_html` (140,848 → 23,591 B) and `ota_html` (17,833 → 4,953 B), served with `Content-Encoding: gzip` | ~130 KB |
| drop SD/SdFat, pulled in by TJpg_Decoder's bundled `User_Config.h` (`TJPGD_LOAD_SD_LIBRARY`) on a board with no SD slot | ~22 KB |
| remove WiFiManager (tzapu): it only ran when saved credentials failed, duplicating the failsafe AP + dashboard scan/join we already have | ~55 KB |

| Build | Before | After |
|---|---|---|
| Flash | 894,087 B (85.6%) | **695,927 B (66.6%)** |
| Static RAM | 54,088 B (66.0%) | **51,900 B (63.4%)** |
| Free sketch space | 150,377 B | **348,537 B** |

**Free heap also rose by 2,840 B at every stage** (measured on hardware after flashing, STA mode) — more than the 2-5 KB guess, because dropping WiFiManager and SdFat removes their static allocations too:

| Free heap, STA | Before | After |
|---|---|---|
| after display init | 24,656 | **27,496** |
| after FS/auth/dashboard/feeds init | 24,440 | **27,280** |
| after WiFi connected | 23,296 | **26,136** |
| after web server init | 19,072 | **21,912** |
| steady state | 18,568 | **21,408** |

Verified on hardware after flashing: `GET /` returns 23,591 B on the wire with `Content-Encoding: gzip` and `Content-Length: 23591`, decompressing to exactly 140,848 B of valid HTML; `GET /update` returns 4,953 B → 17,833 B; JSON routes are unchanged and uncompressed; `POST /page` still works; settings survived the flash. mDNS/ArduinoOTA still deferred (21,408 < the 28,000 B gate).

`tools/prebuild.py` is a PlatformIO `pre:` hook doing the gzip and the TJpg patch on every build; verified it also works on a clean checkout (dependencies install before it runs). `src/webui.h` stays the editable source and is no longer compiled; `src/webui_gz.h` is generated and git-ignored. Gzip round-trip verified byte-for-byte against the originals.

Two consequences of this trim, both accepted:
- **The dashboard and OTA pages are now always gzipped**, with no `Accept-Encoding` check (the uncompressed copy is deliberately not on the device, and `ESP8266WebServer` only collects request headers you register). Browsers are unaffected. Plain `curl http://<ip>/` now returns gzip bytes — use **`curl --compressed`**. The JSON API routes are unaffected.
- **WiFiManager's captive-portal popup is gone** from the one path that used it: saved credentials that stop working. There is no `DNSServer` anywhere in this firmware (before or after), so the "no credentials saved" path never had that popup either. On either path you browse to `192.168.4.1` manually.

**Flash is no longer the constraint; heap is.** Free heap is still ~18.5 KB and one BearSSL session wants ~16 KB of receive buffer (`kHttpsDefaultRecvBufferBytes`, `src/feeds.cpp`). Stripping the remaining display pages would free only ~2-5 KB of RAM, so milestone 6 depends on MFLN working with `api.spotify.com` or on smaller BearSSL buffers, not on more deletions.

### Still available if ever needed (not taken — no feature loss was required)
| Candidate | Flash | What you lose |
|---|---|---|
| Markets page + Finnhub/CoinGecko feeds | ~28 KB | stock/crypto tickers |
| Home Assistant page + feed | ~32 KB | HA entity cards; also the worst TLS heap case (up to 4 sessions per cycle) |
| Focus / World / Event / Quote / Status pages | ~27 KB total | pomodoro timer, 2 world clocks, countdown, quote, free-text page |
| mDNS | ~28 KB | `smartclock-<id>.local` names |

## Space budget — original cut list (superseded by the table above) — what to cut when flash or heap runs out (measured 2026-09-16)
Object sizes from `xtensa-lx106-elf-size` on the esp12e build (pre-link; real savings are a bit lower, confirm with `pio run`). User decision: strip any upstream feature that is not one of our three modes.

| Module / lib | Flash (text+rodata) | Static RAM | Verdict |
|---|---|---|---|
| WiFiManager (tzapu) | 72.9 KB | ~0 | Biggest lever. iodn already has its own failsafe AP + `/wifi` scan/join routes; replace WiFiManager with those + DNSServer if still short after the cuts below. |
| TFT_eSPI | 65.9 KB | ~0 | Keep. Drop unused fonts (`LOAD_FONT7` 7-segment, maybe `LOAD_FONT6`) for a few KB. |
| feeds.cpp | 58.3 KB | 3.6 KB | Keep Open-Meteo only. Drop CoinGecko/Finnhub markets, Home Assistant, quote. Also frees runtime heap (HTTP clients + JSON docs). |
| webserver.cpp | 49.1 KB | 0.8 KB | Drop routes for removed feeds/pages and the GeekMagic-compat endpoints. |
| dashboard.cpp | 34.4 KB | 1.3 KB | Drop dashboard sections for markets / HA / focus / world clocks / countdown / quote. |
| display.cpp | 25.7 KB | 1.0 KB | Drop renderers for PAGE_MARKETS, PAGE_HOME, PAGE_FOCUS, PAGE_WORLD, PAGE_EVENT, PAGE_QUOTE. Keep clock, weather, status. |
| auth.cpp | 12.3 KB | ~0 | Keep (trust boundary). |
| ArduinoOTA + HTTPClient | 15.7 KB | 0.2 KB | Keep (OTA required). |
| SD / SDFS / SdFat | 5.8 KB | ~0 | Not used by us; pulled in transitively — find the includer and drop. |
| logger.cpp | 0.5 KB | 1.5 KB | Shrink ring buffer if RAM-bound. |

Order of operations: (1) `-D BEARSSL_SSL_BASIC` build flag (one line, trims cipher suites); (2) delete markets/HA/quote/focus/world/event feature slices end-to-end (feed + page + route + dashboard section); (3) fonts; (4) WiFiManager replacement only if still needed. Measure and log heap after each step.

## Milestone 3 — commit C: clock faces (2026-09-17, verified on hardware)

Four selectable clock layouts, chosen with `clockFace` in `/dashboard-config.json` (Display → Clock in the dashboard). All four are drawn with TFT_eSPI primitives — no bitmaps, no sprite, no heap.

| `clockFace` | Name | Layout |
|---|---|---|
| 0 | Cards | rounded time card over a rounded weather card (the default) |
| 1 | Bold | full-bleed: badge strip, 48 px digits, 48 px temperature, rain + high/low chips |
| 2 | Matrix | clock strip over bordered instrument tiles, seconds sweep bar, NTP/link tape, precip/humidity/pressure wells |
| 3 | Radial | rain gauge arcing over the top, temperature-range gauge along the bottom with a position marker |

### Cost — measured with a link map
| Item | Bytes |
|---|---|
| all four static layouts | 4,068 |
| all four per-second updaters | 1,046 |
| shared helpers (bars, tiles, markers, time splitting, temperature cluster) | 906 |
| `TFT_eSPI::drawArc`, pulled in only by the radial face | 1,374 |
| **total added, including the web UI picker and humidity/pressure** | **~8.9 KB** |

Build after the work: **704,827 B flash (67.5%)**, 52,752 B static RAM (64.4%), 339,637 B free. Heap cost is zero — every face draws straight to the panel.

### Font facts that constrain every layout
Taken from the TFT_eSPI font headers; these decide what can be drawn where.

| Constant | Font | Height | Baseline | Notes |
|---|---|---|---|---|
| `FONT_INFO` | 1 GLCD | 8 | 7 | 6 px per char, full ASCII |
| `FONT_LABEL` | 2 Font16 | 16 | 13 | full ASCII, `C`/`F` are 8 px |
| `FONT_BODY` | 4 Font32rle | 26 | 19 | digit 14 |
| `FONT_TITLE` | 6 Font64rle | 48 | 36 | digit 27 — **only `[space] 0-9 : . - a p m`** |
| `FONT_HUGE` | 7 Font7srle | 48 | 47 | digit 32, `"88:88"` is 140 px — **only `[space] 0-9 : . -`** |

- **No font has a degree glyph**, so `°` is drawn as a ring (`drawDegreeRing`) and the unit letter comes from `weatherUnitSymbol()`.
- Baselines differ per font, so two sizes share a visual baseline only when `topY_a + baseline_a == topY_b + baseline_b`. `drawTemperatureCluster()` does this; matching box tops instead puts the unit letter visibly off the line.
- Do **not** read baselines from TFT_eSPI's own `fontdata` table: it is defined in the header, so referencing it from `display.cpp` instantiates a second copy and drags glyph data in with it — **measured at +12.4 KB of flash**. `fontBaseline()` hardcodes the five values above instead.
- `drawString` paints an **opaque box** whenever `textcolor != textbgcolor`, so a later draw erases an earlier one wherever the boxes intersect. Draw order matters as much as position.
- `setTextPadding` fills to the **right** of a left datum, **both sides** of a centre datum, and to the **left** of a right datum — pick the datum facing the side whose width can change.

### Repaint rules learned here
`dashboardStaticHash()` changing forces a full redraw; `dashboardDynamicHash()` changing runs only `updateClockDynamicArea()`. Anything drawn in the static half that is **not** in the static hash goes stale until something unrelated forces a redraw. Three bugs of exactly this shape were fixed:
- `hashWeatherData()` did not hash `humidity`, `pressure`, `sunriseMinutes`, `sunsetMinutes` — a poll moving only those left the tiles showing old values.
- The sun/moon icon follows the **wall clock** crossing sunrise/sunset, not the weather data, so nothing changed at dusk. `hashClockTimeDerivedState()` now hashes `hasTimeSync()` plus the day/night and near-sun-event state.
- The matrix face's `NTP OK` / `NO TIME` tape is static and `hasTimeSync()` was unhashed, so a device that booted before SNTP replied showed a red fault label next to a correct clock, forever.

### Blinking colon
`DISPLAY_UPDATE_INTERVAL` dropped from 1000 ms to **500 ms** so the colon can blink on a half-second phase. Every page is hash-gated, so the extra ticks cost two hash walks, not redraws. Two traps:
- The phase is **latched once per tick** (`latchClockColonPhase()`), not read from `millis()` at each use — the hash and the draw happen a whole render apart, and a flip between them caches a parity the panel never showed.
- `lastDisplayUpdate` snaps to the interval grid rather than to `millis()` after the render; measuring from "now" folds the render time into the period, so the tick drifts slower than the blink and periodically skips a half-beat.
- Blank the colon with a `fillRect` of its own footprint, not by redrawing it in the background colour: TFT_eSPI skips the glyph background when `fg == bg`.

### Weather feed additions
`current=` now also requests `relative_humidity_2m,surface_pressure` (both filtered, both parsed to `-1` when absent — the `memset` before parsing would otherwise leave them reading as 0 %). `fillWeatherDataJson()` now emits `weatherCode`, `humidity`, `pressure`, `sunriseMinutes`, `sunsetMinutes`, so every parsed value is inspectable over HTTP. Bengaluru sample: humidity 74 %, pressure 911 hPa (surface, not sea-level adjusted — the city is ~920 m up).

## Milestone 4 — weather, closed by deletion (2026-09-17)

The separate weather page is **gone** (`DASHBOARD_PAGE_WEATHER`, `renderWeatherPage()`, its chip, and `drawMetricColumn`/`drawDividerColumn`/`formatWeatherTemperature` which were its only users). Every clock face already draws the weather, and `forecast_days=1` means there was no extra data a dedicated page could show. Pages serialize **by name** (`pages["clock"]` etc.), so removing an enum value does not corrupt a stored `/dashboard-config.json` — the dropped key is simply ignored on load. Verified on hardware after flashing: the config survived, `POST /page?id=9` now returns 400 (the count fell from 9 to 8) and `?id=1` returns 409 rather than rendering a ghost page.

### Stale-weather marker
`feedsWeatherAgeSeconds()` / `feedsWeatherStaleAfterSeconds()` in `src/feeds.cpp`; `weatherIsStale()` / `weatherConditionLine()` in `src/display.cpp`. Past two missed refreshes (floor: 1 hour) the condition line becomes `1h ago  Drizzle` in the theme's warning colour. Two design constraints that are easy to get wrong:

- **The age must lead the string.** `drawAdaptiveText` clips from the end, so an appended age is the first thing cut. With Matrix's 96 px slot, `Partly cloudy  45m ago` rendered as `Partly cloudy ..` — the age never appeared on three of the four faces.
- **Whole hours, never minutes.** The condition line is hashed into `hashClockTimeDerivedState()`, so it drives a full `fillScreen` redraw. Minute granularity meant ~60 full redraws an hour, most of them producing identical pixels once clipped. Hour granularity ties each redraw to a visible change, and the 1-hour threshold floor keeps `0h ago` from ever rendering.

Flash after milestone 4: **704,343 B (67.4%)** — smaller than milestone 3's 704,827 B, because deleting the page paid for the new marker and 484 B over.

## Milestone 5 stage A — JPEG photo frame (2026-09-17, verified on hardware)

`tools/prepare_images.py` (host, needs Pillow) turns any image into what the device can
actually read, and `DASHBOARD_PAGE_PHOTOS` turns `/image/*.jpg` into a slideshow that takes
part in page rotation like any other chip. Config: `photoIntervalSec` (default 15, clamped
3–3600) and `photoShuffle`.

### Measured on the board
| | |
|---|---|
| Free heap, clock page | 18,208 B |
| Free heap, photos page after ~30 decodes over 90 s | 18,184 B |
| Free heap, back on the clock page | 18,200 B |
| Flash | 706,295 B (67.6%), +2.0 KB for the slideshow |
| Static RAM | 52,876 B (64.5%), +152 B |
| Test photos | 240×240 baseline JPEG, 6.3–16.9 KB each from 1200×800 and 1600×900 sources |

**No heap cost and no leak.** The ±72 B wobble is ordinary `String` churn. Decode still goes
straight to the panel in blocks, so there is no image buffer — the same reason a framebuffer
was never an option. JPEG decode time was not instrumented; the slideshow holds its interval
and the web server stays responsive during a decode, which is all this milestone needs. fps
matters for GIF and is stage B.

### Things that bit, or would have
- **TJpg_Decoder reads baseline JPEG only.** A progressive JPEG under a `.jpg` name passes
  the upload's extension check and then fails to decode. `displayRenderImage()` now returns
  `bool` so the slideshow steps past a file it cannot read instead of parking on its error
  card; with one photo the message stays, which is honest. The message names the real cause
  ("Not a baseline JPEG") rather than a bare `JRESULT`.
- **The renderer must not resolve the photo path.** `renderDashboardPageCached()` samples
  `dashboardStaticHash()` *before* rendering and stores it *after*, so a path resolved inside
  `renderPhotosPage()` cached a hash the screen never showed and cost a second full decode on
  the next tick. `maybeAdvancePhoto()` owns index and path validity and runs that part on
  every tick whatever page is up; the renderer only draws.
- **Never scan LittleFS from the hash path.** `photoCount()` caches with a 5 s TTL, and
  upload and delete call `displayInvalidatePhotoCache()` so a new photo appears at once.
- `Dir::fileName()` returns a **bare** name, so paths are `String(IMAGE_DIR) + fileName()`.
  `openDir()` wants the directory **without** a trailing slash; `IMAGE_DIR` has one for
  building file paths, hence `photoDirPath()`. `dir.next()` also returns directories, so
  entries are filtered with `dir.isFile()`.
- `randomSeed(micros())` in `displayInit()`, or shuffle replays the same order every boot.
- `/app.json` gained `photo` (the slideshow's current file) and `heap`. Without the first
  there is no way to tell from HTTP which photo is up — `img` tracks the *pinned* image, a
  separate mechanism that still overrides every page.

### Host tooling
`pip3 install Pillow` for stills; `brew install gifsicle` for animated GIFs (stage B). Neither
is installed on this Mac — the verification above used a throwaway venv. `--selftest` asserts
240×240, baseline, RGB, within budget, and alpha flattened to black, with no hardware.
