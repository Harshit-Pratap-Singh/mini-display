// Runnable check for Spotify face 4 (src/turntable.h). Compiles on the PC, no hardware:
//     c++ -std=c++17 -Wall -Wextra -o /tmp/tt tools/test_turntable.cpp && /tmp/tt [outdir]
// Asserts the geometry that cannot be eyeballed on a 1.54" panel, then writes four PPM
// frames (lead-in, mid-track, run-out, idle) into outdir so the face can be looked at
// before it is flashed. Text is drawn as grey boxes: the fonts live in TFT_eSPI. The
// album art is a synthetic cover, turned to a different angle in each frame.
#include "../src/turntable.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace tt;

struct Canvas {
    uint16_t px[kSize * kSize];
    void plot(int x, int y, uint16_t c) {
        assert(x >= 0 && y >= 0 && x < kSize && y < kSize);
        px[y * kSize + x] = c;
    }
    bool operator==(const Canvas &o) const { return memcmp(px, o.px, sizeof(px)) == 0; }
};

static Canvas renderStatic(const Sweep &sweep) {
    Canvas c;
    for (int y = 0; y < kSize; ++y) fillRow(y, sweep, c.px + y * kSize);
    return c;
}

static float stylusRadiusFor(float fraction) {
    return kStylusLeadIn - fraction * (kStylusLeadIn - kStylusRunOut);
}

static void fillCircle(Canvas &c, int cx, int cy, int r, uint16_t color) {
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r) c.plot(cx + x, cy + y, color);
}

static void fillRect(Canvas &c, int x, int y, int w, int h, uint16_t color) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) c.plot(x + i, y + j, color);
}

// Feeds the store the way TJpg does: 16x16 blocks over a 75 px decode square (a 300 px
// cover at scale 4), pixel values from `cover`, already in panel byte order.
template <class Cover>
static void decodeInto(LabelStore &store, Cover &&cover) {
    const int size = 75;
    const int origin = kCenter - size / 2;
    for (int by = origin; by < origin + size; by += 16) {
        for (int bx = origin; bx < origin + size; bx += 16) {
            int w = origin + size - bx < 16 ? origin + size - bx : 16;
            int h = origin + size - by < 16 ? origin + size - by : 16;
            uint16_t block[16 * 16];
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i) block[j * w + i] = cover(bx + i, by + j);
            storeLabelBlock(store, bx, by, w, h, block);
        }
    }
    store.valid = true;
}

// Paints the label into the canvas, un-swapping the panel-order rows back to native.
static void paintLabel(Canvas &c, const LabelStore &store, int phaseDeg) {
    paintLabelFrame(store, phaseDeg, [&](int x, int y, int width, const uint16_t *row) {
        for (int i = 0; i < width; ++i) c.plot(x + i, y, swap16(row[i]));
    });
}

// What display.cpp paints with TFT primitives after the rows, approximated.
static void paintChrome(Canvas &c, Point stylus, bool hasTrack) {
    fillRect(c, kPillX, kPillY, kPillW, kPillH, kBlack);
    fillRect(c, kCenter - 50, kPillY + 3, 100, 16, rgb(0x60, 0x60, 0x66));   // title
    fillRect(c, kCenter - 30, kPillY + 21, 60, 8, rgb(0x30, 0x60, 0x50));    // artist
    fillRect(c, kStateX, kStateY, hasTrack ? 60 : 24, kStateH, rgb(0x60, 0x50, 0x30));
    fillRect(c, kElapsedX, kTimeY, 36, kTimeH, rgb(0x60, 0x60, 0x66));
    fillRect(c, kDurationRight - 36, kTimeY, 36, kTimeH, rgb(0x50, 0x50, 0x55));
    paintArm(c, stylus, false, makeSweep(0));
    fillCircle(c, kPivotX, kPivotY, kPivotBaseR, rgb(0x1d, 0x20, 0x24));
    fillCircle(c, kPivotX, kPivotY, 3, kAmber);
}

static void writePpm(const Canvas &c, const std::string &path) {
    FILE *f = fopen(path.c_str(), "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", kSize, kSize);
    for (int i = 0; i < kSize * kSize; ++i) {
        uint16_t p = c.px[i];
        unsigned char rgb8[3] = {static_cast<unsigned char>(((p >> 11) & 0x1f) * 255 / 31),
                                 static_cast<unsigned char>(((p >> 5) & 0x3f) * 255 / 63),
                                 static_cast<unsigned char>((p & 0x1f) * 255 / 31)};
        fwrite(rgb8, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    // --- the arm ---------------------------------------------------------------------
    // Across the whole track, and parked, the stylus lands on the groove it was asked for,
    // stays on screen, and no pixel of the arm touches the label, its rim, or the pill.
    for (float r = kStylusRunOut; r <= kStylusParked; r += 0.25f) {
        Point s = stylusAt(r);
        float got = hypotf(s.x - kCenter, s.y - kCenter);
        assert(fabsf(got - r) < 0.05f);
        assert(s.x > kCenter);  // on the pivot's side of the record
        forEachArmPixel(s, [&](int x, int y, uint16_t color) {
            assert(x >= 0 && y >= 0 && x < kSize && y < kSize);
            assert(radius2(x, y) >= (kRimEdgeR + 1) * (kRimEdgeR + 1));
            assert(!inPill(x, y));
            // the footprint contains the tube and the headshell (the counterweight sits
            // behind the pivot, where nothing dynamic ever goes)
            if (color != kMuted) assert(armCovers(s, x, y));
        });
    }
    // The pivot cap and the counterweight stay clear of the ring.
    for (int y = -kPivotBaseR - kCounterweightLength; y <= kPivotBaseR; ++y)
        for (int x = -kPivotBaseR - 3; x <= kPivotBaseR + 3; ++x)
            assert(radius2(kPivotX + x, kPivotY + y) >= (kRingOuter + 1) * (kRingOuter + 1));

    // --- layout ----------------------------------------------------------------------
    // The pill sits inside the ring and above the label; the corner readouts sit outside
    // the ring.
    for (int y = kPillY; y < kPillY + kPillH; ++y) {
        assert(radius2(kPillX, y) < kRingInner * kRingInner);
        assert(radius2(kPillX + kPillW - 1, y) < kRingInner * kRingInner);
    }
    assert(kCenter - kLabelR > kPillY + kPillH);
    for (int y = kStateY; y < kStateY + kStateH; ++y)
        for (int x = kStateX; x < kStateX + kStateW; ++x)
            assert(radius2(x, y) >= kRingOuter * kRingOuter);
    for (int y = kTimeY; y < kTimeY + kTimeH; ++y) {
        for (int x = kElapsedX; x < kElapsedX + kTimeW; ++x)
            assert(radius2(x, y) >= kRingOuter * kRingOuter);
        for (int x = kDurationRight - kTimeW; x < kDurationRight; ++x)
            assert(radius2(x, y) >= kRingOuter * kRingOuter);
    }

    // --- exactness: draw then erase leaves the static picture untouched ----------------
    const int sweeps[] = {0, 1, 37, 180, 181, 270, 359, 360};
    for (int sweepDeg : sweeps) {
        Sweep sweep = makeSweep(sweepDeg);
        Canvas base = renderStatic(sweep);
        for (float f = 0.0f; f <= 1.0f; f += 0.125f) {
            Point s = stylusAt(stylusRadiusFor(f));
            Canvas c = base;
            paintArm(c, s, false, sweep);
            assert(!(c == base));
            paintArm(c, s, true, sweep);
            assert(c == base);
            for (int phase = 0; phase < 360; phase += 4) {
                paintMarks(c, phase, false, sweep, s);
                paintMarks(c, phase, true, sweep, s);
            }
            assert(c == base);
            // marks never paint inside the pill or over the arm
            paintArm(c, s, false, sweep);
            Canvas withArm = c;
            for (int phase = 0; phase < 360; phase += 4) {
                paintMarks(c, phase, false, sweep, s);
                for (int y = 0; y < kSize; ++y)
                    for (int x = 0; x < kSize; ++x)
                        if (inPill(x, y) || armCovers(s, x, y))
                            assert(c.px[y * kSize + x] == withArm.px[y * kSize + x]);
                paintMarks(c, phase, true, sweep, s);
            }
            assert(c == withArm);
        }
    }

    // --- the ring's incremental edge equals a fresh render ----------------------------
    // Advance and retreat, across the top and across each cardinal. The arm always
    // crosses the ring somewhere (a straight arm from a pivot outside it to a stylus
    // inside it must), so the comparison leaves out the arm's footprint, which the
    // ring painter is required to skip.
    Point parked = stylusAt(kStylusParked);
    const int steps[][2] = {{0, 1}, {1, 2}, {89, 91}, {179, 181}, {269, 271}, {355, 360},
                            {0, 360}, {120, 240}, {40, 41}};
    for (auto &step : steps) {
        for (int direction = 0; direction < 2; ++direction) {
            int fromSweep = direction == 0 ? step[0] : step[1];
            int toSweep = direction == 0 ? step[1] : step[0];
            Canvas c = renderStatic(makeSweep(fromSweep));
            Sweep now = makeSweep(toSweep);
            int lo = fromSweep < toSweep ? fromSweep : toSweep;
            int hi = fromSweep < toSweep ? toSweep : fromSweep;
            paintRing(c, lo, hi, now, parked);
            Canvas want = renderStatic(now);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                    if (!armCovers(parked, x, y))
                        assert(c.px[y * kSize + x] == want.px[y * kSize + x]);
        }
    }
    // With the arm on the record, the ring edge leaves the arm's pixels alone.
    {
        Point s = stylusAt(stylusRadiusFor(0.0f));
        Canvas c = renderStatic(makeSweep(0));
        paintArm(c, s, false, makeSweep(0));
        Canvas before = c;
        paintRing(c, 0, 360, makeSweep(360), s);
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
                if (armCovers(s, x, y)) assert(c.px[y * kSize + x] == before.px[y * kSize + x]);
    }

    // --- the label store and the turning frame ------------------------------------------
    static uint32_t words[kLabelWords];
    LabelStore store{words, false};
    {
        // The store holds exactly the pixels staticColor() calls the label.
        int inside = 0;
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x) inside += radius2(x, y) < kLabelR * kLabelR;
        assert(inside == kLabelPixels);

        // A cover whose every pixel says where it came from.
        auto coordinates = [](int px, int py) {
            return static_cast<uint16_t>(((px - kCenter + 64) << 8) | (py - kCenter + 64));
        };
        fillLabelStore(store, 0x0101);
        decodeInto(store, coordinates);

        const uint16_t sentinel = 0xEEEE;
        auto isArt = [](int x, int y) {
            return radius2(x, y) < kLabelR * kLabelR && radius2(x, y) >= kHubR * kHubR;
        };
        // The frame paints every label pixel and nothing else; a frame at 0 degrees is
        // the cover itself, a quarter turn is exact, and any angle only ever fetches from
        // inside the circle and from the same distance to the spindle.
        Canvas c;
        for (int phase : {0, 90, 7, 45, 133, 271}) {
            memset(c.px, 0xEE, sizeof(c.px));
            paintLabelFrame(store, phase, [&](int x, int y, int width, const uint16_t *row) {
                for (int i = 0; i < width; ++i) c.plot(x + i, y, row[i]);
            });
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    uint16_t got = c.px[y * kSize + x];
                    int dx = x - kCenter;
                    int dy = y - kCenter;
                    if (radius2(x, y) >= kLabelR * kLabelR) {
                        assert(got == sentinel);
                        continue;
                    }
                    assert(got != sentinel);
                    if (!isArt(x, y)) {
                        assert(got == swap16(kGold) || got == swap16(kGoldPale) ||
                               got == swap16(kBlack));
                        continue;
                    }
                    int sx = (got >> 8) - 64;
                    int sy = (got & 0xFF) - 64;
                    assert(sx * sx + sy * sy < kLabelR * kLabelR);
                    if (phase == 0) assert(sx == dx && sy == dy);
                    if (phase == 90) assert(sx == dy && sy == -dx);
                    assert(fabsf(hypotf(sx, sy) - hypotf(dx, dy)) < 1.0f);
                }
            }
        }
        // Without art the frame is the flat fallback label around the hub.
        store.valid = false;
        memset(c.px, 0xEE, sizeof(c.px));
        paintLabelFrame(store, 33, [&](int x, int y, int width, const uint16_t *row) {
            for (int i = 0; i < width; ++i) c.plot(x + i, y, row[i]);
        });
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
                if (isArt(x, y)) assert(c.px[y * kSize + x] == swap16(kLabelFallback));
    }

    // --- frames to look at --------------------------------------------------------------
    // A synthetic cover: four colour quadrants and a white stripe, so the turn shows.
    auto cover = [](int px, int py) {
        int rx = px - kCenter;
        int ry = py - kCenter;
        uint16_t c = rx < 0 ? (ry < 0 ? rgb(0xc0, 0x40, 0x30) : rgb(0x20, 0x60, 0xb0))
                            : (ry < 0 ? rgb(0xe0, 0xb0, 0x30) : rgb(0x30, 0x90, 0x50));
        if (abs(rx - ry) < 4) c = rgb(0xf0, 0xf0, 0xf0);
        return swap16(c);
    };
    fillLabelStore(store, swap16(kLabelFallback));
    decodeInto(store, cover);

    std::string outdir = argc > 1 ? argv[1] : ".";
    struct Frame {
        const char *name;
        float fraction;
        bool hasTrack;
        int phase;
    } frames[] = {{"lead-in", 0.0f, true, 0}, {"mid", 0.35f, true, 120},
                  {"run-out", 1.0f, true, 240}, {"idle", 0.0f, false, 0}};
    for (const Frame &frame : frames) {
        int sweepDeg = frame.hasTrack ? static_cast<int>(frame.fraction * 360.0f) : 0;
        Sweep sweep = makeSweep(sweepDeg);
        Canvas c = renderStatic(sweep);
        Point s = stylusAt(frame.hasTrack ? stylusRadiusFor(frame.fraction) : kStylusParked);
        store.valid = frame.hasTrack;
        paintLabel(c, store, frame.phase);
        paintChrome(c, s, frame.hasTrack);
        if (frame.hasTrack) paintMarks(c, frame.phase, false, sweep, s);
        writePpm(c, outdir + "/turntable-" + frame.name + ".ppm");
    }
    printf("turntable: all checks passed, %d label pixels in %d words, frames in %s\n",
           kLabelPixels, kLabelWords, outdir.c_str());
    return 0;
}
