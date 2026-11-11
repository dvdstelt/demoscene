// Small exact helpers shared by everything that ports the original's integer code:
// the random generator 0xaa2, the sine table 0x22479 and the reciprocal table 0x1ee19 (paper/re/notes-init.md §4).
#pragma once
#include <cmath>
#include <cstdint>

#include "exe.h"

namespace paper {

// 0xaa2: additive lagged Fibonacci, x[n] = x[n-34] + x[n-64] mod 2^32. State: 64 dwords at code32 0x9a0, byte
// indices i = [0xaa0] (0x1d), j = [0xaa1] (0x3f), read from the exe. A fresh Rng is the state at program start;
// main makes kMainWarmup calls, then init 0x5b7d makes kInitCalls (notes-init.md §1).
// Note: notes-init's "140057 init calls" is the total from program start *including* main's 72 (checked against the
// generator state in snapshot t120 and later ones: after init the count is 140057 = 72 + 139985, indices i = 54,
// j = 24). The positions of every random generator of the intro are in RngPos below.
class Rng {
public:
    static constexpr uint64_t kMainWarmup = 72;
    static constexpr uint64_t kInitCalls = 139985;  // 1024 grey + 122880 jitter + 16081 paper grain

    // The generator after `calls` calls from program start (see RngPos).
    static Rng at(const PaperExe& exe, uint64_t calls) { Rng r(exe); r.skip(calls); return r; }

    explicit Rng(const PaperExe& exe) {
        for (int k = 0; k < 64; k++) t_[k] = exe.u32(0x9a0 + 4 * k);
        i_ = exe.u8(0xaa0) & 63;
        j_ = exe.u8(0xaa1) & 63;
    }
    uint32_t next() {
        uint32_t v = t_[i_] + t_[j_];
        t_[j_] = v;
        i_ = (i_ + 1) & 63;
        j_ = (j_ + 1) & 63;
        calls_++;
        return v;
    }
    void skip(uint64_t n) { while (n--) next(); }
    uint64_t calls() const { return calls_; }  // calls made on this object since program start
    // 0x377f: random in -mask..mask (r & 0x10000 selects the sign; 0 twice as likely)
    int32_t signedMasked(uint32_t mask) {
        uint32_t r = next();
        int32_t v = int32_t(r & mask);
        return (r & 0x10000) ? -v : v;
    }

private:
    uint32_t t_[64];
    int i_ = 0, j_ = 0;
    uint64_t calls_ = 0;
};

// Number of 0xaa2 calls made from program start up to the start of each random generator of the intro, in the
// reference capture (paper/re/full.frames). Settled by matching the generator state (0x9a0 table + 0xaa0/0xaa1) in
// every snapshot against a replay from the exe's initial state, and the oscillator tables in notes-title/-water/
// -planes. Use Rng::at(exe, RngPos::kX). All are machine-independent except the flock and what follows it.
struct RngPos {
    static constexpr uint64_t kGreyTable = 72;           // init: pencil darkness bytes [0x1ee09] (1024 calls)
    static constexpr uint64_t kJitterTable = 1096;       // init: endpoint jitter [0x1ee0d] (1024 x 120 calls)
    static constexpr uint64_t kPaperGrain = 123976;      // init 0x55e2 (0x3ed1 = 16081 calls)
    static constexpr uint64_t kAfterInit = 140057;       // nothing else until the title
    static constexpr uint64_t kTitleJitter = 140057;     // 0x2936 -> 0x29e7 letter jitter at setup (192 calls)
    static constexpr uint64_t kTitleOsc = 140249;        // 0x2756(300) at tick 106 (216 calls)
    static constexpr uint64_t kTitleRejitter = 140465;   // then 0x29e7 (192 calls) on every music-counter change,
                                                         // counters 0..116 (117 times): 162929 at the title's end
    static constexpr uint64_t kTransitionNoise = 162929; // scene3d 0x7207 at tick 3186 (20 calls)
    static constexpr uint64_t kSceneNoise = 162949;      // scene3d 0x7207 at tick 3442 (20 calls)
    static constexpr uint64_t kTealOsc = 162969;         // teal 0x2756(300) at tick 6018 (216 calls)
    static constexpr uint64_t kCanyonTex = 163185;       // teal canyon texture, 16384 calls (4 texels per call)
    static constexpr uint64_t kWaterOsc = 179569;        // greetings 0x2756(200) at tick 7741 (216 calls)
    static constexpr uint64_t kFlock = 179785;           // flock 0x33d0 + 0x342f per drawn frame (frame-rate dependent:
                                                         // 105547 calls in the capture, flock at 34 fps)
    static constexpr uint64_t kEndJitter = 285332;       // "the end" 0x29e7 at setup (capture value, 192 calls)
    static constexpr uint64_t kEndOsc = 285524;          // "the end" 0x2756(300) at tick 10970 (216 calls)
    static constexpr uint64_t kEndRejitter = 285740;     // then 192 calls per music-counter change: the first callback
                                                         // (tick 10970) re-rolls at once ([0x22459] still holds the
                                                         // title's counter), 17 re-rolls by tick 11099, none after 11377
};

// 0x22479: sin table, 1024 steps per turn, 16.16 (built with x87 accumulation: two entries differ from rounding).
inline int32_t sinTab(int k) {
    k &= 1023;
    if (k == 661) return -51912;
    if (k == 905) return -43712;
    return int32_t(std::lround(65536.0 * std::sin(2.0 * 3.14159265358979323846 * k / 1024.0)));
}
inline int32_t cosTab(int k) { return sinTab(k + 256); }
// The continuous functions the table samples (angle in 1/1024 turn), for evaluating at fractional angles.
inline double sinTurn(double a) { return std::sin(2.0 * 3.14159265358979323846 * a / 1024.0); }
inline double cosTurn(double a) { return std::cos(2.0 * 3.14159265358979323846 * a / 1024.0); }

// 0x1ee19: [0x1ee15 + 4n] = floor(65536 / n), n = 1..2048.
inline int32_t recipTab(int n) { return n > 0 ? 65536 / n : 0; }

}  // namespace paper
