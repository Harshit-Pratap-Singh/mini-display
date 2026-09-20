// Spotify face 4, the turntable: geometry and the static picture. No Arduino in here so
// tools/test_turntable.cpp can compile, render and check it on the PC.
//
// The one rule of this face: everything that moves (tonearm, spin marks, the edge of the
// progress ring) erases itself by asking staticColor() what was underneath. So the static
// picture is painted by staticColor() alone - never by TFT_eSPI primitives - and the two
// can never disagree by a pixel. Where a moving thing would cross something it must not
// disturb (a spin mark passing under the arm or behind the title pill) it skips those
// pixels rather than painting and restoring them. The label is the exception: it is the
// album art, kept in RAM and repainted whole, turned to the platter's angle, every tick.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

namespace tt {

constexpr int kSize = 240;
constexpr int kCenter = 120;

// Radii from the spindle outwards. A band is [inner, outer).
constexpr int kLabelR = 37;       // album art: 74 px; TJpg scale 4 of a 300 px cover is 75
constexpr int kRimR = 39;         // gold rim 37..39
constexpr int kRimEdgeR = 40;     // dark line 39..40 between the rim and the vinyl
constexpr int kGrooveFirst = 44;  // one dark 1 px groove every 3 px, 44..98
constexpr int kGrooveLast = 98;
constexpr int kLaneR = 99;        // groove-free lane 99..108 where the spin marks ride
constexpr int kEdgeR = 108;       // lighter bevel 108..110
constexpr int kDiscR = 110;       // vinyl ends; black gap 110..113 sets the ring apart
constexpr int kRingInner = 113;   // progress ring 113..117
constexpr int kRingOuter = 117;

// Spindle hub, painted with the label so the label turns around it and it never moves.
constexpr int kHubR = 8;
constexpr int kHubPaleR = 5;
constexpr int kHubHoleR = 3;

// Tonearm: a straight arm of kArmLength pivoting in the top-right corner. The stylus sits
// where a circle of that length around the pivot meets a circle around the spindle whose
// radius is the groove being played, so it really tracks inward across the record.
constexpr int kPivotX = 218;
constexpr int kPivotY = 24;
constexpr float kArmLength = 140.0f;
constexpr int kStylusLeadIn = 108;   // first groove
constexpr int kStylusRunOut = 46;    // last groove, just outside the rim
constexpr int kStylusParked = 112;   // lifted off the vinyl into the gap: nothing loaded
constexpr int kHeadshellLength = 16;
constexpr int kCounterweightLength = 18;  // 9 of it shows past the pivot cap
constexpr int kPivotBaseR = 9;

// Spin marks: two radial dashes 180 degrees apart on the lane.
constexpr int kMarkInner = 100;
constexpr int kMarkOuter = 107;

// Title pill: the one black slab over the record, above the label. Its corners are at
// r=112.6, inside the ring. Nothing dynamic paints inside this rectangle.
constexpr int kPillX = 58;
constexpr int kPillY = 26;
constexpr int kPillW = 124;
constexpr int kPillH = 32;

// Readouts in the corners, outside the ring, where a circle in a square leaves room.
constexpr int kStateX = 4;      // "33 1/3 RPM" / "PAUSED" / "IDLE", FONT_INFO, top left
constexpr int kStateY = 4;
constexpr int kStateW = 64;
constexpr int kStateH = 8;
constexpr int kTimeY = 222;     // elapsed bottom left, duration bottom right, FONT_LABEL
constexpr int kTimeW = 44;
constexpr int kTimeH = 16;
constexpr int kElapsedX = 4;
constexpr int kDurationRight = kSize - 4;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kBlack = 0;
constexpr uint16_t kVinyl = rgb(0x1b, 0x1c, 0x20);
constexpr uint16_t kGroove = rgb(0x0b, 0x0c, 0x0e);
constexpr uint16_t kBevel = rgb(0x34, 0x37, 0x3d);
constexpr uint16_t kRingDim = rgb(0x2a, 0x2d, 0x33);
constexpr uint16_t kMint = rgb(0x56, 0xe5, 0xa9);           // DESIGN tertiary: progress
constexpr uint16_t kAmber = rgb(0xff, 0xb9, 0x5f);          // DESIGN secondary: rim, headshell
constexpr uint16_t kText = rgb(0xe1, 0xe2, 0xe9);           // arm tube
constexpr uint16_t kMuted = rgb(0x87, 0x92, 0x9a);          // counterweight, spin marks
constexpr uint16_t kGold = rgb(0xee, 0x98, 0x00);           // spindle hub, from the mockup
constexpr uint16_t kGoldPale = rgb(0xff, 0xdd, 0xb8);
constexpr uint16_t kLabelFallback = rgb(0x18, 0x23, 0x32);  // the label when there is no art

constexpr float kDegToRad = 0.017453292f;

// The panel wants the high byte first and TJpg delivers its pixels that way; anything we
// mix into those rows has to match.
constexpr uint16_t swap16(uint16_t c) {
    return static_cast<uint16_t>((c << 8) | (c >> 8));
}

inline int radius2(int x, int y) {
    int dx = x - kCenter;
    int dy = y - kCenter;
    return dx * dx + dy * dy;
}

// The played sweep in degrees clockwise from 12 o'clock, with its end direction scaled to
// integers so the per-pixel test is two multiplies and no trig.
struct Sweep {
    int deg;
    long ux;
    long uy;
};

inline Sweep makeSweep(int deg) {
    float a = static_cast<float>(deg) * kDegToRad;
    Sweep s;
    s.deg = deg;
    s.ux = lroundf(sinf(a) * 1024.0f);
    s.uy = lroundf(-cosf(a) * 1024.0f);
    return s;
}

// Is (dx,dy) fewer than s.deg degrees clockwise from 12 o'clock? Screen y grows downward,
// so a clockwise turn is a positive cross product: cross((0,-1),(1,0)) = 1.
inline bool inSweep(const Sweep &s, int dx, int dy) {
    if (s.deg >= 360) return true;
    if (s.deg <= 0) return false;
    if (dx == 0) return dy < 0 || s.deg > 180;   // exactly 12 or exactly 6 o'clock
    long cross = dx * s.uy - dy * s.ux;          // > 0: still before the sweep's end
    return s.deg <= 180 ? (dx > 0 && cross > 0) : (dx > 0 || cross > 0);
}

// Every static pixel of the face except the album art itself.
inline uint16_t staticColor(int x, int y, const Sweep &sweep) {
    int dx = x - kCenter;
    int dy = y - kCenter;
    int r2 = dx * dx + dy * dy;
    if (r2 >= kRingOuter * kRingOuter) return kBlack;
    if (r2 >= kRingInner * kRingInner) return inSweep(sweep, dx, dy) ? kMint : kRingDim;
    if (r2 >= kDiscR * kDiscR) return kBlack;
    if (r2 >= kEdgeR * kEdgeR) return kBevel;
    if (r2 >= kLaneR * kLaneR) return kVinyl;
    if (r2 >= kGrooveFirst * kGrooveFirst) {
        for (int g = kGrooveFirst; g <= kGrooveLast; g += 3) {
            if (r2 < g * g) break;
            if (r2 < (g + 1) * (g + 1)) return kGroove;
        }
        return kVinyl;
    }
    if (r2 >= kRimEdgeR * kRimEdgeR) return kVinyl;
    if (r2 >= kRimR * kRimR) return kGroove;
    if (r2 >= kLabelR * kLabelR) return kAmber;
    return kLabelFallback;
}

inline void fillRow(int y, const Sweep &sweep, uint16_t *row) {
    for (int x = 0; x < kSize; ++x) {
        row[x] = staticColor(x, y, sweep);
    }
}

// --- The label: album art that turns with the platter ----------------------------------
// Turning an image means reading any source pixel for any destination pixel, so the art
// has to sit in RAM, uncompressed, once per track: two panel-order rgb565 pixels per
// 32-bit word, kLabelPixels of them (about 8.6 KB). Only the IRAM second heap can spare
// that - DRAM has ~7 KB left during a Spotify poll - and IRAM tolerates 32-bit accesses
// only, hence the words and the volatile loads and stores.

constexpr int kLabelRows = 2 * kLabelR;  // rows y = -kLabelR .. kLabelR-1

// Widest |x| on row y with x*x + y*y < kLabelR*kLabelR, or -1 for an empty row: the very
// same pixel set staticColor() calls the label, so the frame covers exactly the art.
constexpr int labelHalfWidth(int y) {
    int h = -1;
    while ((h + 1) * (h + 1) + y * y < kLabelR * kLabelR) ++h;
    return h;
}

constexpr int labelPixelCount() {
    int n = 0;
    for (int y = -kLabelR; y < kLabelR; ++y) {
        int h = labelHalfWidth(y);
        if (h >= 0) n += 2 * h + 1;
    }
    return n;
}
constexpr int kLabelPixels = labelPixelCount();
constexpr int kLabelWords = (kLabelPixels + 1) / 2;

struct LabelGeometry {
    int8_t half[kLabelRows];
    uint16_t offset[kLabelRows];  // index of the row's first pixel in the store
};

inline const LabelGeometry &labelGeometry() {
    static LabelGeometry g;
    static bool ready = false;
    if (!ready) {
        int n = 0;
        for (int y = -kLabelR; y < kLabelR; ++y) {
            int h = labelHalfWidth(y);
            g.half[y + kLabelR] = static_cast<int8_t>(h);
            g.offset[y + kLabelR] = static_cast<uint16_t>(n);
            if (h >= 0) n += 2 * h + 1;
        }
        ready = true;
    }
    return g;
}

struct LabelStore {
    volatile uint32_t *words;  // kLabelWords of them, or nullptr when the heap had none
    bool valid;                // a complete decode has landed since the last track change
};

inline uint32_t labelIndex(const LabelGeometry &g, int rx, int ry) {
    return static_cast<uint32_t>(g.offset[ry + kLabelR] + rx + g.half[ry + kLabelR]);
}

inline void fillLabelStore(LabelStore &store, uint16_t panelColor) {
    uint32_t pair = (static_cast<uint32_t>(panelColor) << 16) | panelColor;
    for (int i = 0; i < kLabelWords; ++i) store.words[i] = pair;
}

// Copies the pixels of one decoded block that land inside the label into the store, in
// the byte order they arrive.
inline void storeLabelBlock(LabelStore &store, int x, int y, int w, int h,
                            const uint16_t *bitmap) {
    const LabelGeometry &g = labelGeometry();
    for (int j = 0; j < h; ++j) {
        int ry = y + j - kCenter;
        if (ry < -kLabelR || ry >= kLabelR) continue;
        int half = g.half[ry + kLabelR];
        if (half < 0) continue;
        for (int i = 0; i < w; ++i) {
            int rx = x + i - kCenter;
            if (rx < -half || rx > half) continue;
            uint32_t idx = labelIndex(g, rx, ry);
            uint32_t word = store.words[idx >> 1];
            uint32_t px = bitmap[j * w + i];
            word = (idx & 1) ? ((word & 0x0000FFFFu) | (px << 16)) : ((word & 0xFFFF0000u) | px);
            store.words[idx >> 1] = word;
        }
    }
}

// Paints the label turned `phaseDeg` clockwise, one row at a time through
// pushRow(x, y, width, pixels): the spindle hub, and for every art pixel the source pixel
// found by turning back. Fixed point 16.16 with two adds per pixel and no trig in the
// loop, about 4,300 pixels a frame. The rows are in panel byte order: push them with byte
// swapping off. Without valid art it paints the flat fallback, which turning cannot show.
template <class F>
inline void paintLabelFrame(const LabelStore &store, int phaseDeg, F &&pushRow) {
    const LabelGeometry &g = labelGeometry();
    float a = static_cast<float>(phaseDeg) * kDegToRad;
    int32_t c = lroundf(cosf(a) * 65536.0f);
    int32_t s = lroundf(sinf(a) * 65536.0f);
    bool art = store.valid && store.words != nullptr;
    uint16_t row[kLabelRows];
    for (int dy = -kLabelR; dy < kLabelR; ++dy) {
        int half = g.half[dy + kLabelR];
        if (half < 0) continue;
        int32_t sx = -half * c + dy * s;  // the row's first pixel, turned back
        int32_t sy = half * s + dy * c;
        for (int dx = -half; dx <= half; ++dx) {
            int rr = dx * dx + dy * dy;
            uint16_t px;
            if (rr < kHubHoleR * kHubHoleR) {
                px = swap16(kBlack);
            } else if (rr < kHubPaleR * kHubPaleR) {
                px = swap16(kGoldPale);
            } else if (rr < kHubR * kHubR) {
                px = swap16(kGold);
            } else if (!art) {
                px = swap16(kLabelFallback);
            } else {
                // Rounding can land a pixel at the rim a hair outside the circle; the
                // nearest edge pixel stands in. Row -kLabelR is empty, so clamp past it.
                int isy = (sy + 32768) >> 16;
                if (isy < -(kLabelR - 1)) isy = -(kLabelR - 1);
                if (isy > kLabelR - 1) isy = kLabelR - 1;
                int h = g.half[isy + kLabelR];
                int isx = (sx + 32768) >> 16;
                if (isx < -h) isx = -h;
                if (isx > h) isx = h;
                uint32_t idx = labelIndex(g, isx, isy);
                uint32_t word = store.words[idx >> 1];
                px = (idx & 1) ? static_cast<uint16_t>(word >> 16)
                               : static_cast<uint16_t>(word & 0xFFFF);
            }
            row[dx + half] = px;
            sx += c;
            sy -= s;
        }
        pushRow(kCenter - half, kCenter + dy, 2 * half + 1, row);
    }
}

// --- The tonearm ---------------------------------------------------------------------

struct Point {
    float x;
    float y;
};

// Where the stylus sits when it plays the groove at `grooveRadius`: the intersection of
// the circle of kArmLength around the pivot with that groove, on the pivot's side of the
// record. Two circles, no trig.
inline Point stylusAt(float grooveRadius) {
    float px = static_cast<float>(kPivotX - kCenter);
    float py = static_cast<float>(kPivotY - kCenter);
    float d = sqrtf(px * px + py * py);
    float ex = px / d;
    float ey = py / d;
    float a = (grooveRadius * grooveRadius - kArmLength * kArmLength + d * d) / (2.0f * d);
    float h2 = grooveRadius * grooveRadius - a * a;
    float h = h2 > 0.0f ? sqrtf(h2) : 0.0f;
    float x1 = a * ex - h * ey;
    float y1 = a * ey + h * ex;
    float x2 = a * ex + h * ey;
    float y2 = a * ey - h * ex;
    Point s = x1 > x2 ? Point{x1, y1} : Point{x2, y2};
    return Point{s.x + kCenter, s.y + kCenter};
}

template <class F>
inline void walkLine(int x0, int y0, int x1, int y1, F &&plot) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// A thick stroke: a Bresenham centre line with an axis-aligned span through each of its
// pixels, horizontal for steep lines and vertical for flat ones. Parallel offset lines
// would bead and leave gaps on diagonals; spans never do.
template <class F>
inline void walkThickLine(int x0, int y0, int x1, int y1, int halfWidth, F &&plot) {
    bool steep = abs(y1 - y0) >= abs(x1 - x0);
    walkLine(x0, y0, x1, y1, [&](int x, int y) {
        for (int k = -halfWidth; k <= halfWidth; ++k) {
            if (steep) {
                plot(x + k, y);
            } else {
                plot(x, y + k);
            }
        }
    });
}

// The arm as strokes: counterweight behind the pivot, a 3 px tube, a 5 px headshell.
// Later strokes win where they overlap, so drawing is deterministic and erasing is the
// same walk with the static colour.
template <class F>
inline void forEachArmPixel(Point stylus, F &&fn) {
    float ax = stylus.x - kPivotX;
    float ay = stylus.y - kPivotY;
    float len = sqrtf(ax * ax + ay * ay);
    float ux = ax / len;
    float uy = ay / len;
    auto stroke = [&](float fromT, float toT, int halfWidth, uint16_t color) {
        walkThickLine(lroundf(kPivotX + ux * fromT), lroundf(kPivotY + uy * fromT),
                      lroundf(kPivotX + ux * toT), lroundf(kPivotY + uy * toT), halfWidth,
                      [&](int x, int y) { fn(x, y, color); });
    };
    stroke(-static_cast<float>(kCounterweightLength), 0.0f, 2, kMuted);
    stroke(0.0f, len - kHeadshellLength, 1, kText);
    stroke(len - kHeadshellLength, len, 2, kAmber);
}

// The arm's footprint with a little slack, so the spin marks and the ring can pass
// beneath it without touching its pixels. Must contain every pixel forEachArmPixel()
// paints between the pivot and the stylus; the test checks that.
inline bool armCovers(Point stylus, int x, int y) {
    float ax = stylus.x - kPivotX;
    float ay = stylus.y - kPivotY;
    float len2 = ax * ax + ay * ay;
    float t = ((x - kPivotX) * ax + (y - kPivotY) * ay) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = kPivotX + ax * t - x;
    float cy = kPivotY + ay * t - y;
    float len = sqrtf(len2);
    float half = t * len >= len - kHeadshellLength - 1.5f ? 3.2f : 2.2f;
    return cx * cx + cy * cy <= half * half;
}

inline bool inPill(int x, int y) {
    return x >= kPillX && x < kPillX + kPillW && y >= kPillY && y < kPillY + kPillH;
}

// Two radial dashes, 180 degrees apart. `phaseDeg` is clockwise from 12 o'clock.
template <class F>
inline void forEachMarkPixel(int phaseDeg, F &&fn) {
    for (int half = 0; half < 2; ++half) {
        float a = static_cast<float>(phaseDeg + half * 180) * kDegToRad;
        float sx = sinf(a);
        float sy = -cosf(a);
        walkThickLine(lroundf(kCenter + sx * kMarkInner), lroundf(kCenter + sy * kMarkInner),
                      lroundf(kCenter + sx * kMarkOuter), lroundf(kCenter + sy * kMarkOuter), 1,
                      fn);
    }
}

// Canvas is anything with plot(x, y, rgb565): the TFT on the device, an array on the PC.

template <class Canvas>
inline void paintArm(Canvas &c, Point stylus, bool erase, const Sweep &sweep) {
    forEachArmPixel(stylus, [&](int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
        c.plot(x, y, erase ? staticColor(x, y, sweep) : color);
    });
}

template <class Canvas>
inline void paintMarks(Canvas &c, int phaseDeg, bool erase, const Sweep &sweep, Point stylus) {
    forEachMarkPixel(phaseDeg, [&](int x, int y) {
        if (inPill(x, y) || armCovers(stylus, x, y)) return;
        c.plot(x, y, erase ? staticColor(x, y, sweep) : kMuted);
    });
}

// Repaints the ring between two angles (clockwise from 12 o'clock) from the static
// picture. Called with the old and the new sweep once the progress has moved, in either
// direction. Scans only the sector's bounding box, whose extremes are at its end angles
// or at a cardinal point.
template <class Canvas>
inline void paintRing(Canvas &c, int fromDeg, int toDeg, const Sweep &sweep, Point stylus) {
    if (toDeg <= fromDeg) return;
    Sweep from = makeSweep(fromDeg);
    Sweep to = makeSweep(toDeg);
    int x0 = kSize, y0 = kSize, x1 = -1, y1 = -1;
    auto include = [&](int deg) {
        float a = static_cast<float>(deg) * kDegToRad;
        for (int r = kRingInner; r <= kRingOuter; r += kRingOuter - kRingInner) {
            int px = lroundf(kCenter + sinf(a) * r);
            int py = lroundf(kCenter - cosf(a) * r);
            if (px < x0) x0 = px;
            if (px > x1) x1 = px;
            if (py < y0) y0 = py;
            if (py > y1) y1 = py;
        }
    };
    include(fromDeg);
    include(toDeg);
    for (int card = 90; card < 360; card += 90) {
        if (card > fromDeg && card < toDeg) include(card);
    }
    x0 = x0 - 1 < 0 ? 0 : x0 - 1;
    y0 = y0 - 1 < 0 ? 0 : y0 - 1;
    x1 = x1 + 1 >= kSize ? kSize - 1 : x1 + 1;
    y1 = y1 + 1 >= kSize ? kSize - 1 : y1 + 1;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int r2 = radius2(x, y);
            if (r2 < kRingInner * kRingInner || r2 >= kRingOuter * kRingOuter) continue;
            int dx = x - kCenter;
            int dy = y - kCenter;
            if (!inSweep(to, dx, dy) || inSweep(from, dx, dy)) continue;
            if (armCovers(stylus, x, y)) continue;
            c.plot(x, y, staticColor(x, y, sweep));
        }
    }
}

}  // namespace tt
