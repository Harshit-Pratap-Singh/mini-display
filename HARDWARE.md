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
| Button | — | **no user button on this hardware** (user-confirmed 2026-09-16); iodn reads GPIO4 with pull-up, harmless but unused |

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
- Boots normally; "Display init complete"; button initialised on GPIO4 (INPUT_PULLUP); LittleFS mounts and `/image/` is cleared.
- No saved WiFi → failsafe AP `SmartClock-Setup`, 8-digit password regenerated every boot (read it from the display or serial), IP 192.168.4.1. Admin password for the dashboard is a generated 10-digit number, also shown on display/serial.
- Boot-failure counter: opening the serial port resets the board (DTR/RTS auto-reset). Two resets during early boot put it into "recovery mode" once; the counter clears after any complete boot. Harmless, but don't spam the port during boot.

| Free heap (`ESP.getFreeHeap()`, DRAM only; IRAM second heap not counted) | Bytes |
|---|---|
| after display init | 24,504 |
| after FS/auth/dashboard/feeds init | 24,288 |
| after WiFi setup (softAP mode) | 21,520 |
| after web server init | 17,296 |

- **RAM is tighter than CLAUDE.md's 40–50 KB estimate.** A BearSSL session (~20 KB) does not fit in this headroom as-is; milestone 6 will need trimming (fonts, page HTML, feeds we don't use) and/or the IRAM second heap. Measure in STA mode too (softAP costs extra).
- Visual check (user, 2026-09-16): setup screen readable, colours correct, backlight on → SCLK 14 / MOSI 13 / DC 0 / RST 2 / no CS / BL 5 verified. **No user button exists**, so CLAUDE.md’s button-driven mode switching needs a replacement (web UI, auto mode, timed cycle).
