// Fade level model of [0x1edb5] (IRQ0 0x1e45 step towards [0x1edbd] by [0x1edb9]) and the global post pass that applies
// it to each finished frame like the DAC upload 0x15c0 (paper/re/notes-init.md §6). See fade.h for the event table.
#include "paper/fade.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "paper/palette.h"

namespace paper {
namespace {

struct FadeEvent {
    int tick;        // the main line sets these before the IRQ of this tick
    int target, step;
    int setLevel;    // -1: unchanged
};

constexpr FadeEvent kEvents[] = {
    {100, 0x8000, 150, -1},    {1046, 0xff00, 1000, -1}, {1080, 0x8000, 400, -1}, {7631, 0, 300, -1},
    {7744, 0x8000, 400, -1},   {8707, 0, 300, -1},       {8818, 0x8000, 300, 300}, {10857, 0, 300, -1},
    {10968, 0x8000, 400, -1},  {11344, 0xff00, 1000, -1}, {11377, 0x8000, 512, -1},
};
constexpr int kTicks = 12236;  // 0..12235 (the intro exits in tick 12235)

struct FadeTable {
    std::vector<int> level, target, step;
    FadeTable() {
        level.resize(kTicks);
        target.resize(kTicks);
        step.resize(kTicks);
        int lv = 0, tg = 0, st = 0;
        size_t e = 0;
        for (int t = 0; t < kTicks; t++) {
            for (; e < std::size(kEvents) && kEvents[e].tick == t; e++) {
                tg = kEvents[e].target;
                st = kEvents[e].step;
                if (kEvents[e].setLevel >= 0) lv = kEvents[e].setLevel;
            }
            if (lv < tg) lv = std::min(lv + st, tg);       // 0x1e45: signed compares, clamped at the target
            else if (lv > tg) lv = std::max(lv - st, tg);
            level[t] = lv;
            target[t] = tg;
            step[t] = st;
        }
    }
};

const FadeTable& table() {
    static const FadeTable t;
    return t;
}

int clampTick(int t) { return std::clamp(t, 0, kTicks - 1); }

}  // namespace

int fadeLevelAt(int tick) { return tick < 0 ? 0 : table().level[clampTick(tick)]; }
int fadeTarget(int tick) { return tick < 0 ? 0 : table().target[clampTick(tick)]; }
int fadeStep(int tick) { return tick < 0 ? 0 : table().step[clampTick(tick)]; }

double fadeLevel(double tick) {
    double f = std::floor(tick);
    int t = int(f);
    double a = fadeLevelAt(t), b = fadeLevelAt(t + 1);
    return a + (b - a) * (tick - f);
}

// ---------------------------------------------------------------- post pass
namespace {

// The level is a per-colour function, so applying it to the final RGB frame equals the DAC upload: a multiply below
// 0x8000, a mix towards white above (both done with fixed-function blending over the frame, no copy needed).
void fadePost(Ctx& c, Target& frame, double tick) {
    double level = fadeLevel(tick);
    float f = levelFactor(level), w = levelWhite(level);
    if (f >= 1.0f && w <= 0.0f) return;
    frame.bind();
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_STENCIL_TEST);
    gl::Disable(GL_SCISSOR_TEST);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    if (f < 1.0f) {
        setBlend(Blend::Multiply);  // dst * f
        c.rect(0, 0, 320, 200, Vec3(f, f, f));
    }
    if (w > 0.0f) {
        setBlend(Blend::Alpha);     // dst * (1 - w) + w
        c.rect(0, 0, 320, 200, Vec3(1, 1, 1), w);
    }
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    setBlend(Blend::None);
}

[[maybe_unused]] const bool s_installed = [] {
    postPass() = &fadePost;
    return true;
}();

}  // namespace
}  // namespace paper
