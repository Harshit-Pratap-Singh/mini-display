# Screenshot shot list

Photos still needed for the README. Each row says exactly how to put the device into that state, so the
run is mechanical. Everything is set from the dashboard at `http://<device-ip>/`, logged in as `admin`.

Save each shot as `assets/screens/<filename>` and the README markdown at the bottom will work as-is.

## Panel photos

Shoot straight on, screen filling the frame, room lights low so the backlight is not blown out. The
panel is 240×240, so a phone macro at 15–20 cm is plenty. Turn brightness down to about 60% first
(Display → Brightness), otherwise the camera will clip the whites.

| Filename | What | How to get it |
|---|---|---|
| `clock-cards.jpg` | Clock face 0, Cards | Display → tick **Clock** chip → Clock face = *Cards* |
| `clock-bold.jpg` | Clock face 1, Bold | Display → Clock face = *Bold* |
| `clock-matrix.jpg` | Clock face 2, Matrix | Display → Clock face = *Matrix*. The only face showing humidity and pressure |
| `clock-radial.jpg` | Clock face 3, Radial | Display → Clock face = *Radial* |
| `spotify-art.jpg` | Spotify face 0, Art | Display → Spotify face = *Art*, album art on, with music playing |
| `spotify-text.jpg` | Spotify face 1, Text | Display → Spotify face = *Text*, while playing so the equaliser is moving |
| `spotify-radial.jpg` | Spotify face 2, Radial | Display → Spotify face = *Radial*, mid-track so the ring is part filled |
| `spotify-turntable.jpg` | Spotify face 3, Turntable | Display → Spotify face = *Turntable*, mid-track so the arm is off the lead-in |
| `photos-page.jpg` | Photo frame | Upload a JPEG in System → tick **Photos** chip → *Show now* |
| `setup-ap.jpg` | First-run setup screen | Only on a device with no WiFi saved. Skip unless you have a spare board |

Optional but nice:

| Filename | What | How to get it |
|---|---|---|
| `clock-night-icon.jpg` | Night weather icon | Any clock face after local sunset. The sun becomes a crescent moon |
| `weather-stale.jpg` | Stale weather marker | Any clock face after the weather feed has failed twice. Pull the router, wait |

## Browser screenshots

Full window, light or dark to taste, but keep them consistent. Crop out bookmarks and tabs.

| Filename | What |
|---|---|
| `dash-display.jpg` | Dashboard → Display tab, showing the page chips and face pickers |
| `dash-spotify.jpg` | Dashboard → Feeds tab, Spotify panel (blank the client ID and secret first) |
| `dash-system.jpg` | Dashboard → System tab, showing upload and OTA |

> **Before screenshotting the Spotify panel, clear the client ID and secret fields.** They are
> credentials and this repository is public. The refresh token especially must never appear in an image.

## Markdown to paste into the README

Once the files are in place, this block goes under a `## Screens` heading:

```markdown
## Screens

### Clock faces

| Cards | Bold |
|---|---|
| ![Cards](assets/screens/clock-cards.jpg) | ![Bold](assets/screens/clock-bold.jpg) |

| Matrix | Radial |
|---|---|
| ![Matrix](assets/screens/clock-matrix.jpg) | ![Radial](assets/screens/clock-radial.jpg) |

### Spotify faces

| Art | Text |
|---|---|
| ![Art](assets/screens/spotify-art.jpg) | ![Text](assets/screens/spotify-text.jpg) |

| Radial | Turntable |
|---|---|
| ![Radial](assets/screens/spotify-radial.jpg) | ![Turntable](assets/screens/spotify-turntable.jpg) |

### Photo frame and dashboard

| Photos page | Dashboard |
|---|---|
| ![Photos](assets/screens/photos-page.jpg) | ![Dashboard](assets/screens/dash-display.jpg) |
```

## A note on the old asset

`assets/device-display.jpg` is upstream's 3D marketing render, not a photo of this hardware, and it
shows the Weather page this fork deleted. Replace it with a real photo or drop it.
