// Random oscillators 0x2756 (create) / 0x26d0 (per-tick phase advance), notes-init.md §8.8, and the two things built
// from them: the 3x3 control points of the warp grid 0x2dc8 (title, "the end", greetings; see warp.h) and the teal
// part's wave noise 0x67b9 (notes-planes.md §4.2).
//
// A set is created from the random generator at a known call count (common.h RngPos), so the realisation equals the
// capture's. Its state is a closed form of the tick: phase_k(n) = phase0_k + speed_k * perTick * n, where n = number
// of ticks advanced after the IRQ of tick t = t - firstTick + 1 (0 before). Exact integers at whole ticks; at
// fractional ticks the phase is interpolated linearly (that is the continuous motion the per-tick steps sample).
//
//   OscSet osc = OscSet::title(*ctx.exe);           // 0x2756(300) at tick 106, first advance in the IRQ of tick 106
//   WarpField f = warpField(osc, tick);             // continuous control points at a fractional tick
#pragma once
#include <array>
#include <cstdint>

class PaperExe;

namespace paper {

class Rng;

struct Osc {
    int32_t phase;  // +0  (only phase bits 3..16 are used by 0x2dc8)
    int32_t amp;    // +4  (rand & 0x3ff) + 0x100 = 256..1279 (called "freq" in notes-init)
    int32_t speed;  // +8  ((rand & 0xff) - 0x80) * scale >> 8
};

struct OscSet {
    std::array<Osc, 72> init{};  // as 0x2756 leaves them, before the first advance
    int firstTick = 0;           // tick whose IRQ makes the first advance
    int perTick = 1;             // 0x26d0 calls per tick (2 in the teal part)

    // 0x2756 with ebx = scale: 72 x {phase = rand, amp = (rand & 0x3ff) + 0x100, speed = ((rand & 0xff) - 0x80) * scale >> 8}
    static OscSet create(Rng& rng, int scale, int firstTick, int perTick = 1);
    static OscSet createAt(const PaperExe& exe, uint64_t rngCalls, int scale, int firstTick, int perTick = 1);

    // The sets of the intro, as in the capture (call counts and first ticks checked against the snapshots).
    static OscSet title(const PaperExe& exe);     // scale 300, first advance tick 106
    static OscSet theEnd(const PaperExe& exe);    // scale 300, first advance tick 10970 (flock at 34 fps, see RngPos)
    static OscSet greetings(const PaperExe& exe); // scale 200, first advance tick 7744
    static OscSet teal(const PaperExe& exe);      // scale 300, two advances per tick from tick 6018

    // Number of 0x26d0 advances after the IRQ of `tick` (exact at whole ticks, linear in between).
    int64_t advancesAt(int tick) const;
    double advances(double tick) const;
    // Exact 32-bit phase of oscillator k after the IRQ of tick t.
    int32_t phaseAt(int k, int tick) const;
    // Continuous phase at a fractional tick, reduced mod 2^32 into [0, 2^32) (fraction kept).
    double phase(int k, double tick) const;
};

// ---- 0x2dc8 control points: 3x3 points (row R, column K), each (x, y); oscillators 24R + 8K + 2m (x) and +1 (y),
// octave m = 0..3 (cl = 4 - m):  x = sum_m ((sin[(phase >> (3+m)) & 1023] * amp) >> 16) >> (4-m)
struct WarpField {
    double x[3][3], y[3][3];  // [row R][column K]
};
// Exact integer control points after the IRQ of `tick` (what the original computes for a frame drawn then).
WarpField warpFieldExact(const OscSet& o, int tick);
// Continuous: x = sum_m amp/2^(4-m) * sin(2 pi phase / 2^(13+m)) with the continuous phase.
WarpField warpField(const OscSet& o, double tick);

// ---- 0x67b9 wave noise of the teal part: uses oscillators w[0], w[2], w[4], w[6] (cl = 4, 3, 2, 1).
int noise4Exact(const OscSet& o, int first, int tick);
// Continuous: sum over cl=4..1 of amp/2^(cl-1)... in the units of the exact version (range about +-2398).
double noise4(const OscSet& o, int first, double tick);

}  // namespace paper
