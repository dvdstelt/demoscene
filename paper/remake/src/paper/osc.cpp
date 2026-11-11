// Oscillator sets 0x2756 / 0x26d0, the warp control points of 0x2dc8 and the teal wave noise 0x67b9
// (paper/re/notes-init.md §8.8-8.9, notes-title.md §2.4, notes-water.md §3, notes-planes.md §4.2). See osc.h.
#include "paper/osc.h"

#include <cmath>

#include "paper/common.h"

namespace paper {

static constexpr double kTwoPi = 6.283185307179586476925;

OscSet OscSet::create(Rng& rng, int scale, int firstTick, int perTick) {
    OscSet s;
    s.firstTick = firstTick;
    s.perTick = perTick;
    for (Osc& o : s.init) {
        o.phase = int32_t(rng.next());
        o.amp = int32_t(rng.next() & 0x3ff) + 0x100;
        o.speed = ((int32_t(rng.next() & 0xff) - 0x80) * scale) >> 8;
    }
    return s;
}

OscSet OscSet::createAt(const PaperExe& exe, uint64_t rngCalls, int scale, int firstTick, int perTick) {
    Rng r = Rng::at(exe, rngCalls);
    return create(r, scale, firstTick, perTick);
}

OscSet OscSet::title(const PaperExe& exe) { return createAt(exe, RngPos::kTitleOsc, 300, 106); }
OscSet OscSet::theEnd(const PaperExe& exe) { return createAt(exe, RngPos::kEndOsc, 300, 10970); }
OscSet OscSet::greetings(const PaperExe& exe) { return createAt(exe, RngPos::kWaterOsc, 200, 7744); }
OscSet OscSet::teal(const PaperExe& exe) { return createAt(exe, RngPos::kTealOsc, 300, 6018, 2); }

int64_t OscSet::advancesAt(int tick) const {
    int64_t n = int64_t(tick) - firstTick + 1;
    return n > 0 ? n * perTick : 0;
}

double OscSet::advances(double tick) const {
    double n = tick - firstTick + 1;
    return n > 0 ? n * perTick : 0.0;
}

int32_t OscSet::phaseAt(int k, int tick) const {
    return int32_t(uint32_t(init[k].phase) + uint32_t(init[k].speed) * uint32_t(advancesAt(tick)));
}

double OscSet::phase(int k, double tick) const {
    double n = advances(tick);
    double whole = std::floor(n);
    uint32_t p = uint32_t(init[k].phase) + uint32_t(init[k].speed) * uint32_t(int64_t(whole));
    return double(p) + double(init[k].speed) * (n - whole);  // may leave [0, 2^32) by < |speed|: callers reduce
}

// ---------------------------------------------------------------- 0x2dc8 control points
WarpField warpFieldExact(const OscSet& o, int tick) {
    WarpField f{};
    int k = 0;
    for (int R = 0; R < 3; R++)
        for (int K = 0; K < 3; K++) {
            int32_t U = 0, V = 0;
            for (int cl = 4; cl >= 1; cl--) {
                for (int32_t* acc : {&U, &V}) {
                    int32_t ph = o.phaseAt(k, tick);
                    int idx = (int32_t(uint32_t(ph) << cl) >> 7) & 1023;
                    *acc += ((sinTab(idx) * o.init[k].amp) >> 16) >> cl;
                    k++;
                }
            }
            f.x[R][K] = U;
            f.y[R][K] = V;
        }
    return f;
}

WarpField warpField(const OscSet& o, double tick) {
    WarpField f{};
    int k = 0;
    for (int R = 0; R < 3; R++)
        for (int K = 0; K < 3; K++) {
            double U = 0, V = 0;
            for (int cl = 4; cl >= 1; cl--) {
                double period = double(1 << (17 - cl));  // phase units per turn of this octave
                for (double* acc : {&U, &V}) {
                    double ph = std::fmod(o.phase(k, tick), period);
                    *acc += o.init[k].amp * std::sin(kTwoPi * ph / period) / double(1 << cl);
                    k++;
                }
            }
            f.x[R][K] = U;
            f.y[R][K] = V;
        }
    return f;
}

// ---------------------------------------------------------------- 0x67b9
int noise4Exact(const OscSet& o, int first, int tick) {
    int32_t sum = 0;
    int k = first;
    for (int cl = 4; cl >= 1; cl--, k += 2) {
        int32_t a = (int32_t(uint32_t(o.phaseAt(k, tick)) << (cl + 1)) >> 2) + 0x8000000;
        int i = (a >> 8) & 1023, fr = a & 255;
        int32_t s = (sinTab((i + 1) & 1023) * fr + sinTab(i) * (fr ^ 255)) >> 8;
        sum += (s * o.init[k].amp) >> cl;
    }
    return sum >> 15;
}

double noise4(const OscSet& o, int first, double tick) {
    double sum = 0;
    int k = first;
    for (int cl = 4; cl >= 1; cl--, k += 2) {
        double period = double(1 << (19 - cl));  // phase * 2^(cl-1) / 2^18 turns
        double ph = std::fmod(o.phase(k, tick), period);
        // the table interpolation weights sum to 255/256 (kept so that whole ticks agree with the exact version)
        sum += 2.0 * (255.0 / 256.0) * o.init[k].amp * std::sin(kTwoPi * ph / period) / double(1 << cl);
    }
    return sum;
}

}  // namespace paper
