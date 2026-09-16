# CLAUDE.md — ESP8266 mini display: Spotify · photo frame · clock + weather

## What this project is
Custom firmware for a small ESP8266 desk display (a GeekMagic "SmallTV Ultra"–style board with a bare ESP8266EX, or a clone) with a 1.54" 240×240 IPS screen. We replace the vendor firmware with our own, which has three modes:

1. **Spotify Now Playing** — album art, track title, artist, progress bar; shows automatically when music is playing.
2. **Photo frame** — slideshow of user-uploaded 240×240 JPEGs and small animated GIFs.
3. **Clock + weather** — time/date via NTP, current conditions and today's range via Open-Meteo.

Mode switching via the physical button; settings and uploads via a web UI served by the device. Runs standalone: no PC or Home Assistant needed at runtime.

We are **not** reverse-engineering the stock firmware. We back it up (so the device can always be restored) and replace it.

## Starting point — reuse existing open-source work, do not rewrite it
**Do not start from a blank `main.cpp`.** The default decision is to **fork `iodn/geekmagic-tv-esp8266` and build on it.** Milestone 2 below evaluates it on the real hardware; only fall back to from-scratch if that evaluation fails.

| Project | How we use it |
|---|---|
| https://github.com/iodn/geekmagic-tv-esp8266 | **Code base.** Custom ESP8266 firmware for this exact board family: TFT_eSPI display setup with working pins/flags, LittleFS config, WiFi setup, web dashboard, OTA, Open-Meteo weather, image display. Our clock + weather mode largely exists here already. We add Spotify mode, GIF playback, the photo slideshow and our button/auto-mode logic. Check its license first and keep attribution. |
| https://github.com/witnessmenow/spotify-api-arduino | **Spotify client.** Token refresh + currently-playing parsing (already uses ArduinoJson filters). Start from its ESP8266 examples. Hand-roll a client only if it won't fit in RAM/flash next to TFT_eSPI + AnimatedGIF. |
| https://github.com/ViToni/esphome-geekmagic-smalltv and the Home Assistant thread in References | **Hardware notes only.** Cross-check pins and display settings; nothing to import. |
| `bodmer/TFT_eSPI`, `bodmer/TJpg_Decoder`, `bitbank2/AnimatedGIF`, `bblanchon/ArduinoJson` | **Always.** Never write a display driver, JPEG decoder or GIF decoder ourselves. |

Fall back to building from scratch (Architecture below) only if the iodn firmware fails to build for our board, cannot be made to drive our display, does not fit in flash with our additions, or its license does not permit reuse. Record that decision and the reason in `HARDWARE.md`.

## Hardware — confirmed from photos (2026-09-16)
| Item | Finding |
|---|---|
| Board | Unbranded blue PCB, silkscreen `2024/01/02 V2.6`, USB-C, PCB trace antenna |
| MCU | **ESP8266EX**, QFN-32, chip date code 302024. Xtensa LX106, 80/160 MHz, ~80 KB RAM (≈40–50 KB free heap after WiFi) |
| Flash | External SPI NOR, SOIC-8 next to the MCU. **4 MB confirmed** (JEDEC 0x5E/0x4016, `flash-id` 2026-09-16) |
| USB–serial | **CH340 confirmed** (VID 0x1A86 PID 0x7523), macOS native driver, `/dev/cu.usbserial-*`. Auto-reset works. Use 115200 baud only (921600/460800 corrupt). |
| Display | ~1.54" 240×240 IPS, 12-pin FPC. Panel label `BL-AS4035P-02 241229`, FPC label `WA548C0491-102`. Controller almost certainly ST7789(V) |
| Button | **None usable** (user-confirmed 2026-09-16); the SMD switch seen in photos is not a user button |
| Power | AMS1117-3.3 LDO |
| Misc | `Q2` footprint unpopulated; two unpopulated through-holes next to it (possibly UART or GPIO0/GND pads — check) |

## Pin map — VERIFIED 2026-09-16 with the iodn firmware (details in HARDWARE.md)
Several independent community sources document the GeekMagic SmallTV ESP8266 family with this wiring (the iodn firmware uses the same):

| Signal | GPIO | Notes |
|---|---|---|
| SPI SCLK | 14 | ESP8266 hardware SPI, fixed |
| SPI MOSI | 13 | fixed |
| TFT DC | 0 | also the boot-mode pin: hold to GND at power-up to force flash mode |
| TFT RST | 2 | |
| TFT CS | none | tied to GND on these boards → display needs SPI mode 3 |
| Backlight | 5 | PWM-capable |
| Button | — | **No user button on this hardware** (verified 2026-09-16). Mode switching must be web UI / auto mode / timed cycle. |

Display settings VERIFIED 2026-09-16: ST7789, 240×240, no offsets, **inverted colors (`invertDisplay(true)`), default RGB order (not BGR), SPI mode 3**, 40 MHz.

Verification: flashing the iodn firmware (milestone 2) is the test. Black screen or garbage → multimeter continuity from the 12 FPC pads to the ESP8266 pins / nearby passives; candidate GPIOs are 0, 2, 4, 5, 15, 16. Wrong pins cannot damage anything here, so hypothesis-first is fine.

## Hard constraints — read before writing code
- **RAM is the limiting factor.** A 240×240×16-bit framebuffer is 115 KB — impossible. Draw straight to the panel: decode JPEG/GIF in blocks/lines and `pushImage()` them. No full-screen buffers, ever.
- **TLS is expensive.** A BearSSL connection costs ~20 KB+ of heap and a 1–2 s handshake. Use `setInsecure()` (or one pinned root CA), shrink buffers with `setBufferSizes()`, never hold two TLS connections at once, and log `ESP.getFreeHeap()` on every poll.
- **Spotify JSON is big.** Always parse with an ArduinoJson *filter* that keeps only the fields we render.
- **Avoid heap fragmentation:** fixed static buffers instead of `String` concatenation in loops.
- **Flash budget:** sketch ≤ ~1 MB; the rest is LittleFS for photos/GIFs/config. Keep OTA working.
- **Never write to flash before `backup/stock_backup.bin` exists** and its size equals the `flash_id` size.
- **Secrets never enter git.** WiFi creds, Spotify client ID/secret and refresh token live in LittleFS `/config.json` on the device; any local copies go in `secrets/` (git-ignored). `stock_backup.bin` is vendor-proprietary: keep it in `backup/` (git-ignored), never commit or redistribute.

## Toolchain
- PlatformIO Core (CLI), Arduino framework, `platform = espressif8266`
- `esptool` for identification/backup/restore (v5 spells commands with hyphens: `chip-id`, `flash-id`, `read-flash`, `write-flash`)
- Python 3 + Pillow for `tools/`; `gifsicle` for GIF shrinking
- Libraries: `bodmer/TFT_eSPI`, `bodmer/TJpg_Decoder`, `bitbank2/AnimatedGIF`, `bblanchon/ArduinoJson`, `witnessmenow/spotify-api-arduino`, core `ESP8266WiFi`, `ESP8266WebServer`, `ESP8266HTTPUpdateServer`, `LittleFS`, `ArduinoOTA`

**If we fork iodn (the default), keep its `platformio.ini` and add to it.** The file below is the reference for what our additions need, and the full starting point only in the from-scratch fallback (pins are the hypothesis above; fix after milestone 2):
```ini
[env:minitv]
platform = espressif8266
board = esp12e                                 ; generic ESP8266, 4 MB
framework = arduino
board_build.filesystem = littlefs
board_build.ldscript = eagle.flash.4m2m.ld     ; 1 MB sketch + OTA space, 2 MB LittleFS (change if flash_id ≠ 4 MB)
monitor_speed = 115200
upload_speed = 921600
build_flags =
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY
  -D BEARSSL_SSL_BASIC
  ; TFT_eSPI configured via flags instead of User_Setup.h
  -D USER_SETUP_LOADED=1
  -D ST7789_DRIVER=1
  -D TFT_WIDTH=240 -D TFT_HEIGHT=240
  -D TFT_CS=-1 -D TFT_DC=0 -D TFT_RST=2
  -D TFT_BL=5 -D TFT_BACKLIGHT_ON=HIGH
  ; RGB order: leave default — BGR hypothesis disproven on hardware 2026-09-16
  -D TFT_INVERSION_ON=1
  -D SPI_FREQUENCY=40000000
  -D LOAD_GLCD=1 -D LOAD_FONT2=1 -D LOAD_FONT4=1 -D LOAD_FONT6=1 -D LOAD_FONT7=1 -D LOAD_GFXFF=1 -D SMOOTH_FONT=1
lib_deps =
  bodmer/TFT_eSPI
  bodmer/TJpg_Decoder
  bitbank2/AnimatedGIF
  bblanchon/ArduinoJson
  https://github.com/witnessmenow/spotify-api-arduino.git
```
Notes: TFT_eSPI defaults ST7789 to SPI mode 3 (override with `-D TFT_SPI_MODE=SPI_MODE0` only if a real CS pin turns up). Also evaluate the espressif8266 MMU build options (`PIO_FRAMEWORK_ARDUINO_MMU_*`) — the shared second-heap variant can buy ~16 KB of heap.

## Architecture
When forking iodn, **keep its structure** and map the modules below onto it — this tree describes what we add or own, not a mandate to restructure. In the from-scratch fallback it is the layout.
```
src/
  main.cpp           setup/loop, mode state machine, button (short/long press), reconnect + watchdog
  config.{h,cpp}     /config.json load/save (ArduinoJson)
  net.{h,cpp}        WiFi connect, first-run captive setup portal, NTP (configTime + POSIX TZ)
  web.{h,cpp}        ESP8266WebServer: status, settings, Spotify creds, file upload/list/delete, OTA
  display.{h,cpp}    TFT init, drawing helpers, brightness (PWM GPIO5), night dimming
  mode_spotify.cpp   poll currently-playing, art download → LittleFS → TJpg decode, layout
  mode_photos.cpp    slideshow of /photos/*.jpg and /gifs/*.gif (AnimatedGIF line callback → pushImage)
  mode_clock.cpp     clock/date + Open-Meteo conditions, WMO weather code → PROGMEM icon
tools/
  spotify_auth.py    one-time OAuth on the PC → prints refresh token
  prepare_images.py  crop/resize any image to 240×240 baseline JPEG; wraps gifsicle for GIFs
backup/              stock_backup.bin (git-ignored)
HARDWARE.md          verified pins, display settings, flash size, strings findings, base-repo decision
```

### Mode logic
- Button short press cycles Spotify → Photos → Clock. Long press toggles backlight / steps brightness.
- "Auto" setting (default on): when Spotify reports playback, show Spotify; after N minutes of nothing playing, fall back to the user's chosen idle mode (Photos or Clock).

### Spotify
- **OAuth happens on the PC, not on the device.** Spotify accepts plain-HTTP redirect URIs only for loopback IP literals (`http://127.0.0.1:PORT/...`); `localhost` is rejected and a LAN address would need HTTPS, which the ESP8266 cannot sensibly serve. `tools/spotify_auth.py` runs a tiny local server on `http://127.0.0.1:8888/callback`, completes the Authorization Code flow with scopes `user-read-currently-playing user-read-playback-state`, and prints the refresh token. The user pastes client ID, client secret and refresh token into the device web UI.
- The device refreshes its own access token (`POST https://accounts.spotify.com/api/token`) and polls `GET https://api.spotify.com/v1/me/player/currently-playing` every ~5 s while active; back off on HTTP 429 and honor `Retry-After`.
- Re-download album art only on track change. Prefer the 64×64 image URL from the response (300×300 also works with TJpg scale 2 → 150×150). Stream the download to `/art.jpg` on LittleFS, then decode with `TJpgDec.drawFsJpg()` — never buffer the file in RAM.
- Layout: art, title (scroll if long), artist, thin progress bar, small paused indicator.
- Policy (verified Sep 2026): Development Mode apps require the app owner to hold an active Spotify Premium subscription and are capped at 5 users; add the listening account under the app's User Management. `currently-playing` is not among the endpoints removed in the Feb 2026 changes — re-check the migration guide if calls start failing.

### Photo frame
- Photos are pre-processed on the PC (`prepare_images.py`) to 240×240 **baseline** JPEG (TJpg_Decoder cannot decode progressive JPEG), quality ~80, typically 20–40 KB each. Upload via web UI to `/photos/`.
- GIFs: ≤240×240, reduced palette, aim for <300 KB (`gifsicle --resize-fit 240x240 --colors 64 -O3`). Play with `AnimatedGIF` using a line callback that writes to the TFT. Expect roughly 10–20 fps on ESP8266; measure and state the limits in the web UI help text.
- Settings: slideshow interval, shuffle, GIF loops before advancing.

### Clock + weather
- `configTime()` with a POSIX TZ string from settings (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`).
- Open-Meteo forecast API over plain HTTP, no key: current temperature, weather code, today's min/max, humidity, wind. Refresh every 10–15 min; keep the last good data on failure and show its age.
- Location: lat/lon in settings, with an optional one-time city lookup via Open-Meteo geocoding in the web UI.
- Icons: small RGB565 or 1-bit bitmaps in PROGMEM keyed by WMO code.

### Web UI
- No WiFi configured → open an AP `MiniTV-XXXX` with a captive setup page (minimal, to save flash/RAM).
- Pages: status (heap, RSSI, mode, uptime), settings (mode, auto/idle mode, brightness, night dimming hours, TZ, lat/lon, slideshow interval), Spotify (credentials + "test" button), files (multipart upload, list, delete, free space), OTA update.
- Serve HTML from PROGMEM.

## Milestones — in order, test on hardware at every step
1. ✅ DONE 2026-09-16 — **Identify & back up.** Find the serial port. `esptool chip_id` → expect ESP8266EX. `esptool flash_id` → note size. `esptool -b 921600 read_flash 0 ALL backup/stock_backup.bin`; verify file size == flash size. `strings -n 6 backup/stock_backup.bin | grep -iE 'geek|smalltv|http|ssid|ver'` to identify vendor/features. Write `HARDWARE.md`. If esptool can't sync: hold GPIO0 (DC pad / through-hole) to GND while plugging in. If no serial port appears at all: USB-TTL adapter on the UART pads.
2. ✅ DONE 2026-09-16 — **Base firmware bring-up.** Clone `iodn/geekmagic-tv-esp8266`, read its README and license, build it for `esp12e`, flash it (only after the backup is verified), and confirm the display, button and backlight work. That doubles as pin verification: garbage or black screen → multimeter, fix its pin flags, record the result. If it works, our repo becomes a fork of it. If it cannot be made to work, fall back to a from-scratch color-bar test with the hypothesis pins and continue from-scratch. Update the pin table above, `HARDWARE.md` and `platformio.ini`.
3. **Network + clock mode.** Verify the base firmware's WiFi setup, config storage, NTP and OTA on our board; restyle its clock page to our layout; add button mode cycling with placeholders for the other modes.
4. **Weather.** Verify its Open-Meteo page against our layout; add icons and graceful offline behaviour where missing.
5. **Photo frame.** `prepare_images.py`, extend its upload page, JPEG slideshow, then GIF playback; record fps and heap in `HARDWARE.md`.
6. **Spotify.** `spotify_auth.py`, credentials page, token refresh, polling with 429 backoff, art via LittleFS + TJpg, layout, auto-mode switching.
7. **Polish.** Night dimming, scrolling text, reconnect/watchdog, web OTA, README with photos, attribution to the projects we built on, and a "restore stock firmware" section (`esptool write_flash 0 backup/stock_backup.bin`).

## Working style
- Small, hardware-verified commits per milestone; include heap/fps numbers in commit messages where relevant.
- When something doesn't fit in RAM/flash, trim features (fonts, TLS ciphers, page HTML) rather than adding buffers. A helper server (PC/Pi renders images for the ESP) is the last-resort fallback: note it, don't build it unless milestone 6 fails.
- `HARDWARE.md` is the single source of truth for pins and display settings once verified; keep the hypothesis table here in sync.
- Stop and show results before every flash operation (still the rule; the user approves each flash).

## References
- Base firmware (see Starting point): https://github.com/iodn/geekmagic-tv-esp8266
- Spotify client for ESP8266/ESP32: https://github.com/witnessmenow/spotify-api-arduino
- ESPHome notes + hardware variants of the SmallTV family: https://github.com/ViToni/esphome-geekmagic-smalltv
- Home Assistant community thread with working display settings for the ESP8266 SmallTV: https://community.home-assistant.io/t/installing-esphome-on-geekmagic-smart-weather-clock-smalltv-pro/618029
- Spotify Feb 2026 Development Mode changes: https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide
- Open-Meteo API: https://open-meteo.com/en/docs
- esptool docs: https://docs.espressif.com/projects/esptool/
- TFT_eSPI, TJpg_Decoder (Bodmer) and AnimatedGIF (bitbank2) on GitHub

## Status — resume here (updated 2026-09-17)
**Done:** milestones 1 and 2 (commit `bffd2b6` and the docs commit after it). Stock firmware backed up and verified (`backup/stock_backup.bin`, git-ignored, sha256 in HARDWARE.md). Repo is a fork of iodn @ `d703c12`; `[env:esp12e]` builds and runs on the board; pins and display settings verified. **HARDWARE.md is the source of truth** for hardware facts, heap numbers and the space-budget cut list.

**Device state right now:** running the iodn esp12e build. No WiFi credentials saved → failsafe AP `SmartClock-Setup` (8-digit password shown on screen, regenerates every boot), dashboard at http://192.168.4.1, login `admin` with the 10-digit password shown on screen/serial.

**Open decisions — resolve at the start of milestone 3:**
- **No physical button on this board.** The button-driven mode switching in "Mode logic" needs a replacement: web-UI mode setting + Spotify auto mode + optional timed cycle. Update "Mode logic" once decided.
- **Heap is ~17 KB free after web server init (AP mode)**, not the 40–50 KB assumed. Before adding Spotify/GIF, follow the cut list in HARDWARE.md → "Space budget". Stripping unused upstream features (markets, Home Assistant, quote, focus, world clocks, countdown, WiFiManager) is approved.

**Commands (macOS, this board):**
```
pio run -e esp12e                                                    # build
pio run -e esp12e -t upload --upload-port /dev/cu.usbserial-10       # flash — 115200 only, 460800+ corrupts on this CH340
pio device monitor -p /dev/cu.usbserial-10 -b 115200                 # opening the port resets the board (auto-reset)
esptool --port /dev/cu.usbserial-10 -b 115200 write-flash 0 backup/stock_backup.bin   # restore stock firmware
```
Installed: Homebrew esptool 5.4.0, platformio 6.2.0. Not yet installed (needed from milestone 5): `brew install gifsicle`, Pillow.

**Next — milestone 3:** join `SmartClock-Setup`, set home WiFi in the dashboard, then verify NTP, settings persistence across reboot, web OTA and ArduinoOTA on this board. Then restyle the clock page to our layout and implement button-less mode switching. Record STA-mode free heap in HARDWARE.md.

## Kickoff prompt for the next session
> Read CLAUDE.md and HARDWARE.md. Resume at milestone 3 per the Status section: the board is on /dev/cu.usbserial-10 running the iodn esp12e build in setup-AP mode. First agree the button-less mode-switching approach with me, then verify WiFi setup, NTP, settings persistence and OTA, recording heap numbers in HARDWARE.md. Stop and show me before every flash.
