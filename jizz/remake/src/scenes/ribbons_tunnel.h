// Helpers shared by the ribbon part 0x17003 (ribbons.cpp) and the tunnel part 0x169e7 (tunnel.cpp): reading code
// immediates and sync entries, the piecewise frame clock of their per-frame state, and the rectangle key frames of
// 0x18228 (jizz/re/notes-parts3d-a.md sections 3.9 and 4.5). Header only, so either part builds on its own.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "jizz/core.h"

namespace rt {

// The capture's frame rates: the parts flip during the 70 Hz retrace, every 2nd (35 fps) or 3rd (23.3 fps) one
// depending on the work of the frame (jizz/re/full.flips).
constexpr double kRate2 = 70.0 / 2.0;
constexpr double kRate3 = 70.0 / 3.0;
constexpr double kPitHz = 1193182.0;

// 0x12111 truncates d + (s - d) / 2^n to an integer every frame: on average it loses (2^n - 1) / 2^(n + 1) (0.25 at
// n = 1, 0.375 at n = 2), which also ends the faint tail of the trail after a few frames. The float glow subtracts
// that mean (GlowStep::trailBias).
inline float truncation(int shift) { return shift > 0 ? float((1 << shift) - 1) / float(2 << shift) : 0.0f; }

// The immediate of a "mov reg, imm32" (5 bytes: opcode, imm32) at `insn`.
inline uint32_t movImm(const JizzExe& exe, uint32_t insn) { return exe.u32(insn + 1); }
// The immediate of a "mov dword [abs32], imm32" (10 bytes: c7 05, address, imm32) at `insn`.
inline uint32_t storeImm(const JizzExe& exe, uint32_t insn) { return exe.u32(insn + 6); }

// Music time of the first sync-table entry with this event at or after music time `from` (the events are code
// constants of the parts; the positions come from the table at 0x6417).
inline double syncTime(const Ctx& c, int event, double from = 0.0) {
    for (const Ctx::SyncEntry& e : c.sync)
        if (e.event == event && e.time >= from) return e.time;
    return std::numeric_limits<double>::infinity();
}

// Frames drawn since a phase start as a continuous count: piecewise constant rates (0 = the state is frozen, e.g.
// while another event runs). steps(t) = 0 at the first segment's start.
struct StepClock {
    struct Seg {
        double t0, rate;
    };
    std::vector<Seg> segs;  // ascending t0; the last one runs on

    double steps(double t) const {
        double s = 0;
        for (size_t i = 0; i < segs.size(); i++) {
            double a = segs[i].t0, b = i + 1 < segs.size() ? segs[i + 1].t0 : std::numeric_limits<double>::infinity();
            if (t <= a) break;
            s += (std::min(t, b) - a) * segs[i].rate;
        }
        return s;
    }
    // The time at which steps() reaches k (k >= 0): when frame k was drawn.
    double timeOf(double k) const {
        double s = 0;
        for (size_t i = 0; i < segs.size(); i++) {
            double a = segs[i].t0, b = i + 1 < segs.size() ? segs[i + 1].t0 : std::numeric_limits<double>::infinity();
            double n = segs[i].rate * (b - a);
            if (segs[i].rate > 0 && k <= s + n) return a + (k - s) / segs[i].rate;
            s += n;
        }
        return segs.empty() ? 0.0 : segs.back().t0;
    }
};

// 0x18228 on a key-frame configuration at `cfg` (9 dwords: xA, xB, yA, yB, RA, RB, angleA, angleB, n; the angle
// offset 2048.0 at 0x18224 and the vertical squash 0.625 at 0x18218): n + 1 squares of half-diagonal R turned by
// (angle + 2048) units of 2 pi / 16384, squashed vertically, from the A key (quad 0) to the B key (quad n).
// Evaluated in float instead of 0x182e1's truncating integer lerp and the rounded corners.
inline std::vector<jizz::Quad> keyQuads(const JizzExe& exe, uint32_t cfg) {
    const int n = exe.s32(cfg + 32);
    const double squash = exe.f32(0x18218), offset = exe.f32(0x18224), unit = exe.f64(0x11596);
    std::vector<jizz::Quad> q(size_t(std::max(n, 0) + 1));
    auto key = [&](int k, int field) {
        double A = exe.s32(cfg + 8 * uint32_t(field)), B = exe.s32(cfg + 8 * uint32_t(field) + 4);
        return A + (B - A) * k / n;
    };
    for (int k = 0; k <= n; k++) {
        double X = key(k, 0), Y = key(k, 1), R = key(k, 2), th = (key(k, 3) + offset) * unit;
        double c = R * std::cos(th), s = R * std::sin(th);
        jizz::Quad& o = q[size_t(k)];
        const double xs[4] = {X + c, X - s, X - c, X + s};
        const double ys[4] = {Y + squash * s, Y + squash * c, Y - squash * s, Y - squash * c};
        for (int i = 0; i < 4; i++) {
            o.x[i] = float(xs[i]);
            o.y[i] = float(ys[i]);
        }
    }
    return q;
}

}  // namespace rt
