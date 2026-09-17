# CLAUDE.md — ESP8266 mini display: Spotify · photo frame · clock + weather

## What this project is
Custom firmware for a small ESP8266 desk display (a GeekMagic "SmallTV Ultra"–style board with a bare ESP8266EX, or a clone) with a 1.54" 240×240 IPS screen. We replace the vendor firmware with our own, which has three modes:

1. **Spotify Now Playing** — album art, track title, artist, progress bar; shows automatically when music is playing.
2. **Photo frame** — slideshow of user-uploaded 240×240 JPEGs and small animated GIFs.
3. **Clock + weather** — time/date via NTP, current conditions and today's range via Open-Meteo.

Mode switching via the web UI (page chips + "Show now" + Auto rotate — this board has no button); settings and uploads via a web UI served by the device. Runs standalone: no PC or Home Assistant needed at runtime.

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
| MCU | **ESP8266EX**, QFN-32, chip date code 302024. Xtensa LX106, 80/160 MHz, ~80 KB RAM. **Measured free DRAM heap with the iodn build: 17.3 KB softAP / 18.9 KB STA after web server init, 18.4 KB steady** (HARDWARE.md) |
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
- **Secrets never enter git.** They live only on the device: WiFi creds in the SDK's persisted store (`WiFi.persistent(true)`), device settings in iodn's EEPROM-emulated `Settings` struct (`src/settings.cpp`), API keys/tokens in LittleFS JSON (`/feeds-config.json` today; Spotify client ID/secret/refresh token go in a LittleFS JSON file the same way). There is no `/config.json`. Any local copies go in `secrets/` (git-ignored). `stock_backup.bin` is vendor-proprietary: keep it in `backup/` (git-ignored), never commit or redistribute.

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
upload_speed = 115200                          ; this board's CH340 corrupts at 460800+
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
Notes: TFT_eSPI defaults ST7789 to SPI mode 3 (override with `-D TFT_SPI_MODE=SPI_MODE0` only if a real CS pin turns up). iodn already builds with `PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED`; the IRAM second heap cannot hold BearSSL buffers, so budget with DRAM (`ESP.getFreeHeap()`) numbers only.

## Architecture
When forking iodn, **keep its structure** and map the modules below onto it — this tree describes what we add or own, not a mandate to restructure. In the from-scratch fallback it is the layout.
```
src/
  main.cpp           setup/loop, page rotation timer, reconnect + watchdog (no button on this board)
  settings.{h,cpp}   (iodn) EEPROM-emulated Settings struct, 512 B; per-feature LittleFS JSON: /dashboard-config.json, /feeds-config.json, /auth.json
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

### Mode logic — agreed 2026-09-17 ("pages-as-modes")
This board has no user button, so modes reuse iodn's existing dashboard page machinery instead of a new mode concept:
- **Modes = iodn page chips** (`enabledPages[]` in `/dashboard-config.json`, edited with live preview on the dashboard). After the strip the chips are Clock, later Photos (M5) and Spotify (M6). There is no Weather chip: every clock face draws the weather, and with one forecast day fetched a separate page had nothing extra to show (removed in milestone 4). One ticked chip = fixed mode; several + "Auto rotate" = timed cycle (`rotationIntervalSec`); several with rotate off = manual.
- **"Show now" replaces the button:** one protected route `POST /page?id=N` (jump) / `POST /page` (next) calling a single writer `displaySetPage()`, plus a "Show now" select in the dashboard. Button code (`src/button.*`, GPIO4, long-press backlight toggle) is deleted; backlight off = brightness 0 or night dimming.
- **Defaults** flip to rotation off + Clock only, so a fresh device is a still clock.
- **Spotify auto (M6):** ticking the Spotify chip *is* "Auto". The page counts as available while playing or within `spotifyIdleMinutes` of the last playback (0 = never expire); play-start jumps to it; iodn's existing content-eviction then falls back to the first other ticked chip. Known limit: the idle target is the lowest-index ticked chip — untick Clock if Photos should be the idle mode.

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
- Time: iodn calls `configTime(gmtOffset, 0, "pool.ntp.org")` — fixed offset in seconds, no DST (IST preset = 19800). Fine for India; a POSIX TZ string would need a `Settings` struct version bump, so defer unless DST is needed.
- Open-Meteo forecast API over plain HTTP, no key: current temperature, weather code, today's min/max, humidity, wind. Refresh every 10–15 min; keep the last good data on failure and show its age. (iodn today fetches only `temperature_2m`, `weather_code` — converted to text and discarded — daily max/min and `precipitation_probability_max`, synchronously with a 12 s timeout that blocks `loop()`; humidity, wind, the raw WMO code and data age are milestone 4 additions to the URL, filter and `WeatherData`.)
- Location: lat/lon in settings, with an optional one-time city lookup via Open-Meteo geocoding in the web UI.
- Icons: drawn with TFT_eSPI primitives keyed by WMO code, not PROGMEM bitmaps — a few hundred bytes of code instead of one bitmap per condition, and they follow the theme colours for free. Day/night variants switch on the real sunrise/sunset times.
- **Clock faces (added 2026-09-17):** four layouts selectable with `clockFace` (Display → Clock): `0` Cards, `1` Bold, `2` Matrix, `3` Radial. Hour/minute/second colours are configurable hex. The colon blinks at 1 Hz. See HARDWARE.md → "Milestone 3 — commit C" for the cost table, the font constraints (fonts 6 and 7 are digits-only, no degree glyph) and the repaint rules — read that before touching a face.
- Open-Meteo `current=` also fetches `relative_humidity_2m,surface_pressure` for the Matrix face. Wind is still not fetched; add it to the URL, the filter and `WeatherData` if a layout needs it.

### Web UI
- No WiFi configured → iodn's failsafe AP `SmartClock-Setup` (8-digit password on screen) at http://192.168.4.1; no captive DNS, `/scan` + `/connect` sit behind the dashboard login. Keep as is.
- Pages: status (heap, RSSI, mode, uptime), settings (mode, auto/idle mode, brightness, night dimming hours, TZ, lat/lon, slideshow interval), Spotify (credentials + "test" button), files (multipart upload, list, delete, free space), OTA update.
- Serve HTML from PROGMEM.

## Milestones — in order, test on hardware at every step
1. ✅ DONE 2026-09-16 — **Identify & back up.** Find the serial port. `esptool chip_id` → expect ESP8266EX. `esptool flash_id` → note size. `esptool -b 921600 read_flash 0 ALL backup/stock_backup.bin`; verify file size == flash size. `strings -n 6 backup/stock_backup.bin | grep -iE 'geek|smalltv|http|ssid|ver'` to identify vendor/features. Write `HARDWARE.md`. If esptool can't sync: hold GPIO0 (DC pad / through-hole) to GND while plugging in. If no serial port appears at all: USB-TTL adapter on the UART pads.
2. ✅ DONE 2026-09-16 — **Base firmware bring-up.** Clone `iodn/geekmagic-tv-esp8266`, read its README and license, build it for `esp12e`, flash it (only after the backup is verified), and confirm the display, button and backlight work. That doubles as pin verification: garbage or black screen → multimeter, fix its pin flags, record the result. If it works, our repo becomes a fork of it. If it cannot be made to work, fall back to a from-scratch color-bar test with the hypothesis pins and continue from-scratch. Update the pin table above, `HARDWARE.md` and `platformio.ini`.
3. ✅ DONE 2026-09-17 — **Network + clock mode.** WiFi setup, config storage, NTP and OTA verified on the board; the heap gate was lowered from 28,000 to 12,000 B so mDNS and ArduinoOTA start for the first time; button mode cycling replaced by the button-less scheme in "Mode logic"; the clock page restyled into four selectable faces. Only web `/update` is still unverified (a local tooling permission blocked the upload, not the device — `espota` wireless flashing is verified end to end).
4. ✅ DONE 2026-09-17 — **Weather.** Icons landed with the clock faces in milestone 3 (drawn primitives, day/night aware). The standalone weather page was **deleted** rather than restyled — the Matrix face already shows strictly more, and we only fetch one forecast day. Offline behaviour added: the last good reading is kept and the condition line leads with its age (`1h ago  Drizzle`, amber) once two refreshes have been missed.
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

**Device state right now (2026-09-17):** running the unmodified iodn esp12e build, joined to home WiFi (STA, DHCP **192.168.1.14**). Dashboard at http://192.168.1.14, login `admin` + the 10-digit password generated at first boot (not stored anywhere on the PC — every JSON route and `/update` return 401 without it). If credentials are lost it falls back to the failsafe AP `SmartClock-Setup` (8-digit password on screen, regenerates every boot) at http://192.168.4.1.

**Open decisions — resolve at the start of milestone 3:**
- ~~No physical button on this board~~ **Resolved 2026-09-17:** pages-as-modes, see "Mode logic" (`POST /page` + Show now; button code deleted in commit A).
- **Heap is ~17 KB free after web server init in AP mode and 18.9 KB in STA (18.4 KB steady)**, not the 40–50 KB assumed. iodn already runs its low-memory fallbacks here (mDNS/ArduinoOTA deferred at the 28,000 B gate, clock sprite skipped at 30,000 B). Before adding Spotify/GIF, follow the cut list in HARDWARE.md → "Space budget". Stripping unused upstream features (markets, Home Assistant, quote, focus, world clocks, countdown, WiFiManager) is approved.

**Commands (macOS, this board):**
```
pio run -e esp12e                                                    # build
pio run -e esp12e -t upload --upload-port /dev/cu.usbserial-10       # flash — 115200 only, 460800+ corrupts on this CH340
pio device monitor -p /dev/cu.usbserial-10 -b 115200                 # opening the port resets the board (auto-reset)
esptool --port /dev/cu.usbserial-10 -b 115200 write-flash 0 backup/stock_backup.bin   # restore stock firmware
```
Installed: Homebrew esptool 5.4.0, platformio 6.2.0. Not yet installed (needed from milestone 5): `brew install gifsicle`, Pillow.

**Milestone 3 progress (2026-09-17):** WiFi set up ✅ (STA 192.168.1.14). STA heap recorded in HARDWARE.md ✅. Button-less design agreed and **commit A shipped ✅** (button → `POST /page` + Show now, Clock-only/rotation-off defaults) — flashed over USB and verified on hardware, see HARDWARE.md “Milestone 3 — commit A”. Also verified: WiFi, **NTP** (device clock matches to the second), **settings persistence** across reboot and across a flash, image upload, LittleFS 2 MB layout. Timezone set to IST (19800); it was UTC out of the box.

**Still to do in milestone 3:**
- **Web OTA (`/update`) is still unverified** — the upload was blocked by a local tooling permission, not by the device. Drag `firmware.bin` onto http://192.168.1.14/update in a browser to confirm. **ArduinoOTA is verified**, so wireless flashing already works: `pio run -e esp12e -t upload --upload-port smartclock-e1cf2e.local --upload-protocol espota --upload-flags --auth=<admin password>`.
- ~~**Commit B:** the approved strip~~ **Done differently and better:** 198 KB of flash reclaimed with **no features removed** — gzip the embedded web pages (~130 KB), drop SD/SdFat (~22 KB), drop WiFiManager (~55 KB). Sketch is now 66.6% full with 348 KB free. See HARDWARE.md “Space budget” for the measured table and the per-feature list still available if ever needed.
- ✅ **Done:** heap gate lowered to 12,000 — **mDNS and ArduinoOTA now start for the first time** (`smartclock-e1cf2e.local` resolves; `espota` upload verified end to end). `clearImageDirectory()` deleted, so uploaded images persist across reboots as milestone 5 needs.
- **Heap, not flash, is now the constraint for Spotify** — but the trim helped there too: free heap rose from 18,568 to **21,408 B** steady (STA), against ~16 KB for one BearSSL receive buffer. Milestone 6 is now plausible without deleting any display page; confirm MFLN behaviour with `api.spotify.com` when we get there.
- ✅ **Commit C done:** the clock page is now four selectable faces (Cards / Bold / Matrix / Radial) with configurable hour/minute/second colours and a 1 Hz blinking colon, plus humidity and pressure added to the weather feed. Build **704,827 B (67.5%)**, 332 KB free, no heap cost. Details, font constraints and the repaint rules are in HARDWARE.md → "Milestone 3 — commit C: clock faces"; read it before editing a face.
- **Next:** milestone 4 (weather page against our layout, offline behaviour), then 5 (photo frame) and 6 (Spotify).

## Kickoff prompt for the next session
> Read CLAUDE.md and HARDWARE.md. Resume at milestone 3 per the Status section: the board is on /dev/cu.usbserial-10 and on home WiFi at 192.168.1.14 running the unmodified iodn esp12e build. Continue from the milestone 3 progress list. Stop and show me before every flash.
