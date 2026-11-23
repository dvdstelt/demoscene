// The palette fades (fade.h): the script's palette routines replayed as an affine colour transform per refresh.
#include "inside/fade.h"

#include <algorithm>
#include <cmath>

#include "music.h"

namespace inside {

const char* kFadeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D frame;
uniform vec3 mulc;
uniform vec3 addc;
void main() { vec4 c = texture(frame, uv); o = vec4(clamp(c.rgb * mulc + addc, 0.0, 1.0), 1.0); }
)";

namespace {
// The palette routines (NOTES.md "Palette"): 0x400498 white flash (start min(c + 63, 63), 15 refreshes), 0x400454
// inverse flash (start 63 - c, 20), 0x400438 fade to black (40). The script calls them directly, and some part
// routines call them (notes of the parts): the twirl's start 0x40a678 and event 0x40a098 (white), the flower's show
// 0x409358 (white when its counter k = (calls + 1) % 9 is even, inverse when odd) and hide 0x4093f0 (white), the
// credits' next name 0x405f1c (white), the temple's start 0x40461c (white). When two run before the same refresh, the
// second replaces the first (each sets the start anew).
constexpr uint32_t kFlash = 0x400498, kInverse = 0x400454, kBlack = 0x400438;
constexpr uint32_t kWhiteFlashers[] = {0x40a678, 0x40a098, 0x4093f0, 0x405f1c, 0x40461c};
constexpr uint32_t kFlowerShow = 0x409358;
// Fades a part starts itself with 0x4004ac(level, n) (notes-core.md section 6): the intro's init fades in from black
// over 120 refreshes (its first step at the first record of the intro, the script's entry 0x4038c8); the flight's start
// 0x402c28 fades in over 60 (from the black the script's fade-out left).
struct PartFade { uint32_t routine; float level; int steps; };
constexpr PartFade kPartFades[] = {{0x4038c8, 1.0f, 120}, {0x402c28, 1.0f, 60}};
}  // namespace

Fade::Fade(Ctx& c) {
    int flowerShows = 0;
    for (const ScriptEntry& e : c.script()) {   // in table order: entries of one refresh run in this order
        if (!e.routine || !std::isfinite(e.refresh)) continue;
        bool white = e.routine == kFlash;
        for (uint32_t w : kWhiteFlashers) white |= e.routine == w;
        if (e.routine == kFlowerShow) white = ((flowerShows++ + 1) % 9) % 2 == 0;
        if (white) ops_.push_back({FadeOp::Flash, e.refresh, 1.0f, 15});
        else if (e.routine == kInverse || e.routine == kFlowerShow) ops_.push_back({FadeOp::Inverse, e.refresh, 1.0f, 20});
        else if (e.routine == kBlack) ops_.push_back({FadeOp::Black, e.refresh, 0.0f, 40});
        for (const PartFade& f : kPartFades)
            if (e.routine == f.routine) ops_.push_back({FadeOp::Fade, e.refresh, f.level, f.steps});
    }
    std::stable_sort(ops_.begin(), ops_.end(), [](const FadeOp& x, const FadeOp& y) { return x.refresh < y.refresh; });
    // step the state once per refresh, like 0x400540 in the timer callback
    first_ = ops_.empty() ? 0 : int(std::floor(ops_.front().refresh)) - 1;
    int last = int(std::ceil(c.music ? c.music->endRefresh : 0)) + 2;
    double cur_a = 0, cur_b = 0, fa = 0, fb = 0, da = 0, db = 0, level = 0;   // the DAC starts black
    int left = 0;
    size_t next = 0;
    for (int r = first_; r <= last; r++) {
        // the routines that ran before this refresh's step
        while (next < ops_.size() && ops_[next].refresh <= r) {
            const FadeOp& op = ops_[next++];
            double ta = level, tb = 0;     // target: base x level
            int n = op.steps;
            switch (op.kind) {
                case FadeOp::Flash: cur_a = 0; cur_b = 1; level = 1; ta = 1; break;
                case FadeOp::Inverse: cur_a = -1; cur_b = 1; level = 1; ta = 1; break;
                case FadeOp::Black: level = 0; ta = 0; break;
                case FadeOp::Fade: level = op.level; ta = op.level; break;
                case FadeOp::SetBase: ta = level; n = std::max(1, left); break;
            }
            n = std::max(1, n);
            fa = cur_a;
            fb = cur_b;
            da = (ta - cur_a) / n;
            db = (tb - cur_b) / n;
            left = n;
        }
        if (left > 0) {
            fa += da;
            fb += db;
            cur_a = fa;
            cur_b = fb;
            left--;
        }
        states_.push_back({cur_a, cur_b});
    }
}

FadeState Fade::at(double r) const {
    if (states_.empty()) return {};
    double x = r - first_;
    int i = int(std::floor(x));
    double f = x - i;
    auto get = [&](int k) { return states_[size_t(std::clamp(k, 0, int(states_.size()) - 1))]; };
    Step s0 = get(i), s1 = get(i + 1);
    float a = float(s0.a + (s1.a - s0.a) * f), b = float(s0.b + (s1.b - s0.b) * f);
    return {Vec3(a, a, a), Vec3(b, b, b)};
}

}  // namespace inside
