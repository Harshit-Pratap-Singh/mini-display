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
- **TLS costs ~10 KB of DRAM plus the receive buffer in IRAM** (measured 2026-09-18, HARDWARE.md → "TLS cost for Spotify"), not the ~20 KB once assumed. Use `setInsecure()`, `setBufferSizes(4096, 512)` — **512 fails with `BR_ERR_TOO_LARGE` because Spotify offers no MFLN** — never hold two TLS connections at once, and log both heaps on every poll.
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
Notes: TFT_eSPI defaults ST7789 to SPI mode 3 (override with `-D TFT_SPI_MODE=SPI_MODE0` only if a real CS pin turns up). iodn already builds with `PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED`. **Measured 2026-09-18: the BearSSL receive buffer IS served from the IRAM second heap** — DRAM holds only the ~10 KB session context and is flat whatever `setBufferSizes()` asks for. Budget the two heaps separately; `ESP.getFreeHeap()` reports DRAM only, use `HeapSelectIram` for the other. (An earlier note here claimed IRAM could not hold these buffers; that was wrong.)

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
- **Modes = iodn page chips** (`enabledPages[]` in `/dashboard-config.json`, edited with live preview on the dashboard). After the strip the chips are Clock, Photos, later Spotify (M6). There is no Weather chip: every clock face draws the weather, and with one forecast day fetched a separate page had nothing extra to show (removed in milestone 4). One ticked chip = fixed mode; several + "Auto rotate" = timed cycle (`rotationIntervalSec`); several with rotate off = manual.
- **"Show now" replaces the button:** one protected route `POST /page?id=N` (jump) / `POST /page` (next) calling a single writer `displaySetPage()`, plus a "Show now" select in the dashboard. Button code (`src/button.*`, GPIO4, long-press backlight toggle) is deleted; backlight off = brightness 0 or night dimming.
- **Defaults** flip to rotation off + Clock only, so a fresh device is a still clock.
- **Spotify auto (M6):** ticking the Spotify chip *is* "Auto". The page counts as available while playing or within `spotifyIdleMinutes` of the last playback (0 = never expire); play-start jumps to it; iodn's existing content-eviction then falls back to the first other ticked chip. Known limit: the idle target is the lowest-index ticked chip — untick Clock if Photos should be the idle mode.

### Spotify
**Player face decisions — agreed 2026-09-18, do not re-litigate:**
- ✅ **Three selectable faces, all built 2026-09-19** (`spotifyFace`, Display → Spotify face), same pattern as the clock faces: `0` Art — album art + real telemetry, `1` Text — no art and no JPEG decode, with the animated equaliser, `2` Radial — 360° ring with a needle head. Designs and tokens are in `design/`. Face 1 is the only one that decodes art, so face 1 is the one to suspect if memory misbehaves.
- **Poll `/v1/me/player`, not `/v1/me/player/currently-playing`.** Same one call and rate limit, and it is the only source of device name, volume, and shuffle/repeat, which all three faces use.
- **Fixed palette on the Spotify faces, taken from the mockups** (user instruction 2026-09-18, superseding an earlier note here that said DESIGN.md colours only). DESIGN.md supplies the neutrals - surface `#111318`, text `#e1e2e9` - and the mockups add **Spotify green `#1db954`** as the brand accent, with amber `#ffb95f`, cyan `#38bdf8` and mint `#56e5a9` as secondaries. Per face, counted from the mockup HTML in `design/`: face 1 is green-dominant, face 2 uses mint + cyan + green + amber together, face 3 is green (`#1ed760`/`#1db954`) with amber. These pages ignore `activeTheme()`.
- **The EQ visualiser is on face 2, by user instruction 2026-09-19** (this reverses an earlier note here that forbade it — do not reverse it back). The device has no audio and the Web API sends no spectrum data, so the bar heights are an animation, not a measurement. It is modelled rather than pure noise (left columns swing higher and decay slower like bass, right ones flicker; instant attack, one-block decay) and it settles flat when paused, which is the one thing it reports truthfully. It runs from `loop()` at ~12 fps because the 500 ms page tick would render it as a slideshow. **Face 1 keeps the real telemetry** (WiFi signal, both heaps as sampled during the poll, poll latency), so the honest numbers are still one face away.
- **Not available from the Web API, so absent from every face:** bitrate, codec, queue position, liked state. **No transport controls** either: this board has no buttons and we deliberately took read-only scopes, so painted prev/play/next would do nothing.
- ✅ `tools/spotify_auth.py` **written 2026-09-18** (stdlib only, `--selftest` needs no network or credentials). **OAuth happens on the PC, not on the device.** Spotify accepts plain-HTTP redirect URIs only for loopback IP literals (`http://127.0.0.1:PORT/...`); `localhost` is rejected and a LAN address would need HTTPS, which the ESP8266 cannot sensibly serve. `tools/spotify_auth.py` runs a tiny local server on `http://127.0.0.1:8888/callback`, completes the Authorization Code flow with scopes `user-read-currently-playing user-read-playback-state`, and prints the refresh token. The user pastes client ID, client secret and refresh token into the device web UI.
- The device refreshes its own access token (`POST https://accounts.spotify.com/api/token`) and polls `GET https://api.spotify.com/v1/me/player`. **Not every 5 s** — a poll blocks `loop()` for ~1.4 s (ECDHE on an 80 MHz core; session resumption already took it from 2.6 s and nothing removes the rest), so 5 s would freeze the display 28% of the time. The interval adapts instead: **30 s idle / 15 s playing / 5 s in the last 20 s of a track**, since the progress bar interpolates locally and only *changes* need the network. Back off on HTTP 429 and honour `Retry-After`. `/spotify.json` exposes `nextPollInMs` to observe the cadence.
- ✅ **Album art done 2026-09-18 (stage 3), re-downloaded only on track change.** Takes the **largest image ≤300 px** (TJpg scale 2 → 150 px), *not* the 64×64 this line used to prefer: measured on hardware, the TLS handshake dominates and is paid whatever the size, so 64×64 saves only the ~1.2 s transfer and buys a blurry thumbnail. Budget **~2.7 s of blocked `loop()` per track change**. Streamed to `/art.jpg` on LittleFS 512 B at a time — never buffered in RAM — verified against `Content-Length`, short reads deleted, 3 strikes then stop retrying. Decode with `TJpgDec.drawFsJpg()`. Numbers in HARDWARE.md → "Milestone 6 stage 2–3".
- Layout: art, title (scroll if long), artist, thin progress bar, small paused indicator.
- Policy (verified Sep 2026): Development Mode apps require the app owner to hold an active Spotify Premium subscription and are capped at 5 users; add the listening account under the app's User Management. `currently-playing` is not among the endpoints removed in the Feb 2026 changes — re-check the migration guide if calls start failing.

### Photo frame
- Photos are pre-processed on the PC (`prepare_images.py`) to 240×240 **baseline** JPEG (TJpg_Decoder cannot decode progressive JPEG), quality ~80, typically 20–40 KB each. Upload via web UI to `/photos/`.
- ~~GIFs~~ **not supported** (decided 2026-09-17): `AnimatedGIF`'s decoder is 24,172 B against ~18 KB of free heap. Uploads are restricted to `.jpg`/`.jpeg`. See HARDWARE.md → "Milestone 5 stage B" for the measurement and the no-heap fallback if it is ever wanted.
- Settings: slideshow interval (`photoIntervalSec`) and shuffle (`photoShuffle`).

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
5. **Photo frame.** ✅ **Stage A done 2026-09-17** — `tools/prepare_images.py` (240×240 baseline JPEG, centre-cropped, EXIF-aware, quality stepped to fit; `--selftest` needs no hardware) and a `Photos` page chip that slideshows `/image/*.jpg` with a configurable interval and shuffle. Verified on the board: no heap cost, no leak over ~30 decodes. See HARDWARE.md → "Milestone 5 stage A". **Stage B closed without GIF support 2026-09-17:** `AnimatedGIF` is 24,172 B against 18,208 B of free heap — measured, not estimated — so it fits neither the heap nor static RAM. Shrinking its LZW dictionary would only play specially re-encoded GIFs and would still foreclose milestone 6. Heap is reserved for Spotify instead. Full numbers and the fallback that would cost no heap are in HARDWARE.md → "Milestone 5 stage B".
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

## Status — resume here (updated 2026-09-19, handover)
**Done:** milestones 1–6. Everything is committed; the tree is clean at `aa8b578`. **HARDWARE.md is the source of truth** for measured facts — pins, both heaps, TLS costs, the serial-reset trap, the font constraints. Read its last three sections before touching Spotify or a face.

**Device state right now:** on home WiFi at **192.168.1.14** (`smartclock-e1cf2e.local`), running `aa8b578` — all four clock faces, photo slideshow, and **four Spotify faces** (`spotifyFace` 0 Art · 1 Text · 2 Radial · 3 Turntable, picker under Display). Spotify credentials and WiFi are on the device in LittleFS; copies in `secrets/` (git-ignored). Dashboard login `admin` + the 10-digit password, which is in **`secrets/device_password`** (mode 600, git-ignored) so `tools/ota_update.sh` can use it.

### FIRST THING NEXT SESSION: rebuild Spotify face 4 (Turntable) properly
The user looked at it on the panel and asked for it to be **rebuilt properly** — no specifics were given before the session ended. **Ask for a photo of the current face 4 and what is wrong with it before writing code.** Spec and the hardware-forced adaptations are in `design/spotify-face-4-turntable.md`; the mockup's key numbers are there (centre 120,120; label r=72; rim r=74; grooves r=78–96 then a clear band; spin mark at r=103; progress ring r=113–116; tonearm pivot 204,30 sweeping 8°→26.5°). The current implementation is `renderSpotifyFaceTurntable()` + `drawFace4Dynamic()` + `tickTurntableSpin()` in `src/display.cpp`. Likely suspects, unverified: the row-by-row corner mask that makes the art circular (black notches or spill past the gold rim), the tonearm geometry (lifted from an SVG simulator, never measured on the panel), whether one spin mark reads as rotation, and the arm's erase-and-restore leaving streaks. Also **not yet judged by eye:** face 2's equaliser in motion and face 3's bottom row (`82% / PLAYING / SHUF+R` in 148 px).

### Then: milestone 7 (polish) — untouched
Night dimming, **scrolling text for long titles** (faces 0 and 1 clip with `..`; 2 and 3 wrap), reconnect/watchdog, README with photos, attribution to iodn + Bodmer + bitbank2 + bblanchon + witnessmenow, and a "restore stock firmware" section (`esptool --port /dev/cu.usbserial-10 -b 115200 write-flash 0 backup/stock_backup.bin`).

### Facts that will bite if forgotten
- **Flash over the air, not USB.** `pio run -e esp12e && tools/ota_update.sh`. A USB upload or a serial-monitor open **resets the board**; two resets inside 30 s trip the boot counter into **recovery mode, which disables `spotifyLoop()`** — a working Spotify page then looks broken. **Five quick cycles factory-reset and wipe WiFi + Spotify credentials.** Only fall back to USB (`/dev/cu.usbserial-10`, 115200) if OTA is unreachable. Check the boot banner for `RECOVERY MODE` before debugging anything Spotify.
- **DRAM is ~2 KB free during a Spotify poll** (measured; IRAM ~13 KB is fine). TLS takes ~10 KB of ~16 KB. Every Spotify failure so far traced to *our* allocation habits, not Spotify: `String` concatenation before a parse, a JSON filter rebuilt per poll, a header timeout misreported as a parse error. **Fixed buffers, print pieces, never `String +` on the poll path.** If `Parse: NoMemory` returns, the answer is a fixed-size arena allocator for the parse — deliberately not built yet. Faces 0 and 3 decode a JPEG; faces 1 and 2 never touch the decoder and are the fallbacks if memory misbehaves.
- **`ESP.getFreeHeap()` is DRAM only.** The BearSSL receive buffer comes from the IRAM second heap. Face 0's `DRAM/IRAM` cell and the poll log show both, sampled *during* the poll — idle heap says nothing.
- **Fonts 6 and 7 are digits-only.** `FONT_BODY` (26 px) is the largest that can draw a title; wrap (`drawWrappedText`) rather than shrink.
- **`drawString` paints an opaque box**; overlaps erase neighbours. **Anything drawn but not hashed never repaints.** Repainting a whole ring at 2 Hz flickers — redraw only the changed wedge.
- **A circle inscribed in a 240×240 square has nothing in the corners**: at y=30 only x 72..168 is inside r=102. Compute the usable width per row before placing anything on a radial face.
- Two `.venv`s exist; `.venv-1` has Pillow. `node` is present for `node --check` on `src/webui.h` scripts — extract each `<script>` block separately (two pages each declare `uiThemeStorageKey`).

**Commands (macOS, this board):**
```
pio run -e esp12e                              # build (tools/prebuild.py regenerates src/webui_gz.h)
tools/ota_update.sh                            # flash over WiFi — the default; needs secrets/device_password
pio run -e esp12e -t upload --upload-port /dev/cu.usbserial-10   # USB fallback — resets the board, 115200 only
c++ -std=c++17 -o /tmp/t tools/test_spotify_url.cpp && /tmp/t   # PC selftest for the art URL parser
esptool --port /dev/cu.usbserial-10 -b 115200 write-flash 0 backup/stock_backup.bin   # restore stock
```

## Kickoff prompt for the next session
> Read CLAUDE.md (Status section first) and the last three sections of HARDWARE.md. The board is at 192.168.1.14 running `aa8b578` with four Spotify faces. First task: rebuild Spotify face 4 (Turntable) properly — ask me for a photo and what's wrong before changing code. Flash with `tools/ota_update.sh`, never USB unless OTA is down. Stop and show me before every flash. I'm new to embedded — explain hardware constraints from first principles.
