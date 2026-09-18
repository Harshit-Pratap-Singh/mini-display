# Spotify face 4 - Minimalist Turntable

Source: user mockup 2026-09-19 (Stitch "ESP8266 BENCH / Minimalist Turntable").
The mockup is an SVG simulator; these are the numbers the firmware needs.

## Geometry (240x240, centre 120,120)
- Background            #050505 pure black
- Vinyl body            r=112, concentric grooves r=75..112
- Groove strokes        0.3-0.6 px in the mockup -> 1 px here, alternating shades
- Album art             r=72 circular (d=144), clipped to a circle
- Gold rim              r=74, stroke #ffb95f
- Label dashed ring     r=72, #ffd899
- Spindle stack         r=8 #ee9800, r=5.5 #ffddb8, r=3 #2a1700, r=1.2 #ffffff
- Perimeter progress    r=114, 2.5 px, #30c88f, starts at 12 o'clock
- Tonearm pivot         (204,30), sweeps 8.0deg (lead-in) to 26.5deg (runout)
- Top glance panel      x18 y20 w204 h46, rgba(5,7,11,0.85)
- Bottom glance panel   x24 y186 w192 h34
- Time bar              x32 y214 w176 h2

## Typography in the mockup
- Title   16 px Inter 800      -> FONT_BODY (26 px) is our largest; wrap instead
- Artist  8.5 px mono 700      -> FONT_INFO
- Elapsed 14 px mono 800       -> FONT_LABEL

## What cannot be reproduced, and why
- Rotating anisotropic sheen: a rotated circle is the same circle, and there is no
  framebuffer or gradient fill. A rotating radial tick shows the spin instead.
- "24-BIT", "33 1/3 RPM", "SPEED/PITCH", album year: not in the Spotify Web API.
- Curved text on circular paths: TFT_eSPI has no textPath.
- The tonearm stops at the album edge (r=76) rather than crossing the art, so a move
  only has to repaint the groove annulus, not re-decode the JPEG.
