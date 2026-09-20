# Spotify face 4 - Minimalist Turntable

Source: user mockup 2026-09-19 (Stitch "ESP8266 BENCH / Minimalist Turntable").
The mockup is an SVG simulator. The first two sections are its numbers; the third is
what the firmware draws instead and why. Rebuilt 2026-09-19: every number that matters
is a constant in `src/turntable.h`, and `tools/test_turntable.cpp` renders the face on
the PC and asserts the geometry (`c++ -std=c++17 -o /tmp/tt tools/test_turntable.cpp &&
/tmp/tt /tmp` writes four PPM frames: lead-in, mid, run-out, idle).

## The mockup (240x240, centre 120,120)
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
- Title   16 px Inter 800      -> FONT_LABEL (16 px)
- Artist  8.5 px mono 700      -> FONT_INFO
- Elapsed 14 px mono 800       -> FONT_LABEL

## What the firmware draws instead, and why (rebuild 2026-09-19)
The first build copied the mockup's layers one primitive at a time and broke on three
hardware facts. The rebuild is organised around them.

1. **There is no alpha.** The mockup's glance panels are 85% black with the record
   showing through. On the panel a slab is opaque, so the first build's two panels
   covered the top 18 px and bottom 8 px of the album art and cut four gaps into the
   progress ring, one of which was re-cut every tick. Now there is **one pill**, x58
   y26 w124 h32, above the label (first r=60, then r=37 once the label turned, point
   4). The pill's corners are at r=112.6, inside the ring's inner edge (113), so the
   ring is never covered. The times move to the corners a circle in a square leaves free:
   at y=4 the first 64 px are outside r=117, at y=222 the first and last 44 px are.
   Title and artist are clipped with `..` to 116 px; scrolling is milestone 7.

2. **An erase has to be pixel-exact.** Erasing the arm in black then redrawing the
   grooves with `drawCircle` and the ring with `drawArc` leaves whatever those
   primitives round differently: notches in the ring, a nicked rim, streaks. Now one
   function, `tt::staticColor(x, y, sweep)`, defines every static pixel. The full
   render pushes the screen row by row from it (57,600 calls, integer only: the ring's
   angle test is a cross product against the sweep's end vector scaled by 1024). The
   arm, the spin marks and the ring's moving edge erase themselves by asking the same
   function, and where they would cross something they must not disturb they skip the
   pixel instead: marks skip the pill (`inPill`) and the arm (`armCovers`), the ring
   painter skips the arm. The album art is the one exception: nothing dynamic ever
   reaches the label, and the label is repainted whole from RAM (point 4), so no
   square corner ever shows and nothing needs masking.

3. **A tonearm is a rotation about its pivot.** The first build's arm was an ellipse
   formula lifted from the simulator; its stylus slid sideways along the label near
   12 o'clock and never tracked inward. Now the pivot is (218,24) in the corner, the
   arm is 140 px, and the stylus is the intersection of the circle of that radius
   around the pivot with the groove being played, r=108 at the start to r=46 at the
   end (two circles, no trig). That sweeps the arm about 27 degrees per track, and its
   closest approach to the spindle is r=44.8, clear of the rim (37..40). With nothing
   loaded it parks at r=112, in the gap between the vinyl and the ring. The arm always
   crosses the ring near 2 o'clock; the ring painter leaves its pixels alone and the
   arm's own erase restores them with the current sweep.

4. **The label turns, so the art lives in RAM (added 2026-09-19).** Turning an image
   means reading any source pixel for any destination pixel every frame, which needs the
   art uncompressed in memory. The 120 px label was 22.6 KB: never. Free DRAM is ~7 KB
   during a poll; the IRAM second heap has 18.9 KB, TLS wants ~5 KB of it and BearSSL
   silently falls back to DRAM when IRAM runs short. So the label is **r=37 (74 px)**:
   4,281 pixels, two per 32-bit word, **8,564 B** carved from IRAM once and kept
   (`tt::LabelStore`; IRAM tolerates 32-bit accesses only, hence the words and the
   volatile loads). TJpg's scale-4 output of a 300 px cover is 75 px, so the label shows
   the *whole* cover downscaled instead of a crop of its middle, and the decoder's blocks
   go into the store instead of the panel (`tftOutput()` while `turntableStoreArt`).
   `tt::paintLabelFrame()` repaints the label turned to the platter's angle on every spin
   tick: 16.16 fixed point, two adds per pixel, ~4,300 pixels, one `pushImage` per row,
   with the spindle hub painted in place so the label turns around it. The pixels stay in
   panel byte order all the way (TJpg pre-swaps), so the rows are pushed with swapping
   off and the hub and fallback colours are `swap16`ed to match. A 74 px label on a
   220 px disc is about the proportion of a real LP.

### Bands, from the spindle out (r, [inner, outer))
label (turning art) <37 · rim 37..39 amber · dark line 39..40 · vinyl · grooves 1 px
every 3 px, 44..98 · lane 99..108 (marks ride 100..107) · bevel 108..110 · black gap
110..113 · ring 113..117 (dim #2a2d33, played #56e5a9) · black. Spindle hub r<8,
painted with the label.

### Motion
- Spin: the label (album art) and two grey radial dashes 180 degrees apart turn
  together, 4 degrees per ~80 ms tick from `loop()` (about 7 s per revolution). The
  dashes hide behind the pill and under the arm. Everything stops when paused, which is
  the one thing the animation reports truthfully. Without art the label is a flat
  colour, which turning cannot show, so only the dashes carry the motion.
- Arm: moves once the stylus radius has changed by a whole pixel, r=108 to r=46, 62
  moves a track, each about 900 `drawPixel` calls. The pivot cap is redrawn after each
  move because the erase paints black under it.
- Ring: only the wedge between the old and new sweep is repainted, in either direction.

### Still not reproducible
- Rotating anisotropic sheen: no framebuffer, no gradient fill. The turning label and
  the dashes carry the motion instead.
- Art bigger than 74 px that turns: RAM, see point 4. Face 0 keeps the big static art.
- "24-BIT", "SPEED/PITCH", album year: not in the Spotify Web API. "33 1/3 RPM" is
  shown as the platter's nominal speed while playing, like the equaliser on face 2 it
  labels the animation, not a measurement.
- Curved text on circular paths: TFT_eSPI has no textPath.
