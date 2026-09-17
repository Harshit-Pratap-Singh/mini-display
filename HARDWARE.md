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
| NTP | **working** — device epoch matched the PC to the second (probed over HTTP: `POST /dashboard/data {"focus":{"remainingSeconds":N}}` makes the device stamp `dashboardCurrentEpoch()`, then read `data.focus.updatedAtEpoch` from `/dashboard.json`; it returns 0 when unsynced) |
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

## Space budget — what to cut when flash or heap runs out (measured 2026-09-16)
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
