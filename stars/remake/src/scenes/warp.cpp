// warp: the three women (files 11, 12, 13) as a wrapping 256x256 texture seen through a patch whose four edges are
// the four quarters of a closed wavy curve, five phases from 27:01 to 33:23 (stars/re/notes-parts2d-a.md section 2).
// Ports 0x975b (the part: five parameter blocks, the oscillator and the picture cycling), 0x982c (a block's loop),
// 0x9932 (the PIT callback: phases, fade, block length), 0x98b6 / 0x98de / 0x98e9 (the oscillator), 0x9989 + 0x9a02
// (the edge curves), 0x9a62 (the patch fill), 0x9d95 (the textures: the middle 256 columns, rows stretched 200 ->
// 256).
//
// Evaluated exactly instead of the original's shortcuts: the curves at the pixel's continuous position along each
// edge (instead of integer angles and integer radii), the patch formula per output pixel in floats (instead of the
// packed 8.8 accumulators, which are off by up to 3.6 texels), the picture read with a filter at (32 + u,
// v * 200 / 256) with u, v wrapping at 256 (instead of the nearest texel of a nearest-resampled copy), the fade as a
// colour multiply evaluated at t. Dropped: the tearing (the patch is drawn straight into the VGA over 1.5 frames) and
// the race between the PIT and 0x9989 (in 62 of 915 frames the curves mix two phase values). Kept: the x / 200 step
// across a row (the left/right correction extrapolated 1.6 times at the right edge) and the shift byte used mod 32.
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"

using namespace stars;

namespace {

const char* kWarpFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;   // picture 11, 12 or 13 (320 x 200 grid), premultiplied
uniform int sharp;       // original pixels: sharp squares
uniform float ph1, ph2;  // curve rotation, wobble phase (angle units, 4096 per turn)
uniform float lobes;     // 2^(shift & 31)
uniform float amp, base; // radius = base + amp sin(...) (texels)
uniform vec2 off;        // (u0, v0) / 256 (texels)
uniform vec2 TL, TR, BL, BR;  // corner averages (texels)
uniform float level;     // fade level / 256

const float kStepX = 3355443.0 / 1048576.0;  // 0x333333 / 2^20: angle units per column
const float kStepY = 5368709.0 / 1048576.0;  // 0x51eb85 / 2^20: per row
float sinA(float a) { return 65528.0 / 65536.0 * sin(6.283185307179586 * a / 4096.0); }
float cosA(float a) { return 65528.0 / 65536.0 * cos(6.283185307179586 * a / 4096.0); }
// 0x9a02: the point of the curve at angle a
vec2 curve(float a) {
    float r = amp * sinA(a * lobes + ph2) + base;
    return vec2(160.0, 100.0) + r * vec2(cosA(a), sinA(a));
}

vec4 sharpGrad(sampler2D t, vec2 c, vec2 gx, vec2 gy) {
    vec2 sz = vec2(textureSize(t, 0));
    vec2 p = c * sz;
    vec2 seam = floor(p + 0.5);
    vec2 d = max(abs(gx * sz) + abs(gy * sz), vec2(1e-5));
    p = seam + clamp((p - seam) / d, -0.5, 0.5);
    return textureGrad(t, p / sz, gx, gy);
}

void main() {
    // the original's pixel index, continuous: pixel (x, y) has its centre at VGA (x + 0.5, y + 0.5)
    float x = uv.x * 320.0 - 0.5, y = (1.0 - uv.y) * 200.0 - 0.5;
    vec2 T = curve(ph1 + kStepX * x);
    vec2 B = curve(ph1 + 3072.0 - kStepX * x);
    vec2 R = curve(ph1 + 1024.0 + kStepY * y);
    vec2 L = curve(ph1 - kStepY * y);
    vec2 Dl = L - (TL + y / 200.0 * (BL - TL));
    vec2 Dr = R - (TR + y / 200.0 * (BR - TR));
    vec2 P = T + (y + 1.0) / 200.0 * (B - T) + Dl + x / 200.0 * (Dr - Dl) + off;   // texel (u, v)
    // texel (u, v) = picture point (32 + u, v * 200 / 256), wrapping at 256
    vec2 w = mod(P, 256.0);
    vec2 c = vec2((32.0 + w.x) / 320.0, w.y / 256.0);
    vec2 gx = dFdx(P) / vec2(320.0, 256.0), gy = dFdy(P) / vec2(320.0, 256.0);
    vec4 col = sharp != 0 ? sharpGrad(tex, c, gx, gy) : textureGrad(tex, c, gx, gy);
    o = vec4(col.rgb * level, 1.0);
}
)";

// A parameter block (0x9488 + 0x2c k: 11 dwords, copied to 0x9458 by 0x982c).
struct Block {
    double u0, v0, base, d1, d2, amp, ph1, ph2, du, dv;  // u0, v0, du, dv in 8.8
    int shift;
};

// One run_block of 0x975b.
struct Phase {
    Block b;
    int count;            // the PIT callback counts down from here; the block lasts count + 1 ticks
    double t0 = 0, t1 = 0;
    int osc = -1;         // the oscillator drives amp: 0 (phase 2), 1 (phase 5), -1 off
    bool cycling = false; // pictures 11 -> 12 -> 13 every 65.5 drawn frames
};

class Warp : public Scene {
public:
    ~Warp() override {
        for (Texture& t : texels_) t.destroy();
        if (shader_.id()) gl::DeleteProgram(shader_.id());
    }

    void init(Ctx& c) override {
        shader_.build(kFullscreenVS, kWarpFS, "warp");
        // the order 0x975b runs them in, the block lengths and the oscillator settings are code (immediates)
        const uint32_t addr[5] = {0x9488, 0x94b4, 0x9564, 0x95e8, 0x9538};
        const int count[5] = {0x104, 0x186, 0x186, 0x186, 0x186};
        for (int k = 0; k < 5; k++) {
            uint32_t a = addr[k];
            Block& b = phases_[size_t(k)].b;
            b.u0 = c.exe->s32(a);
            b.v0 = c.exe->s32(a + 4);
            b.base = c.exe->s32(a + 8);
            b.d1 = c.exe->s32(a + 12);
            b.d2 = c.exe->s32(a + 16);
            b.shift = c.exe->u8(a + 20) & 31;  // patched into `shl ebp, imm8`: the CPU uses it mod 32
            b.amp = c.exe->s32(a + 24);
            b.ph1 = c.exe->s32(a + 28);
            b.ph2 = c.exe->s32(a + 32);
            b.du = c.exe->s32(a + 36);
            b.dv = c.exe->s32(a + 40);
            phases_[size_t(k)].count = count[k];
            phases_[size_t(k)].osc = k == 1 ? 0 : k == 4 ? 1 : -1;
            phases_[size_t(k)].cycling = k >= 2;
        }
        osc0_ = c.exe->u32(0x9618);
        for (int n = 11; n <= 13; n++) {
            if (c.picture(n).redraw) continue;
            // original pixels (with --original-art, or a picture without a redraw): a mipmapped copy, the patch
            // minifies them
            const auto& f = c.exe->file(n);
            Palette pal = vgaPalette(std::span<const uint8_t>(f.data(), 0x300));
            Image im;
            im.w = 320;
            im.h = 200;
            im.rgba.resize(320 * 200 * 4);
            for (size_t i = 0; i < 320 * 200; i++) {
                const Vec3& col = pal[f[0x300 + i]];
                im.rgba[i * 4 + 0] = uint8_t(clamp01(col.x) * 255.0f + 0.5f);
                im.rgba[i * 4 + 1] = uint8_t(clamp01(col.y) * 255.0f + 0.5f);
                im.rgba[i * 4 + 2] = uint8_t(clamp01(col.z) * 255.0f + 0.5f);
                im.rgba[i * 4 + 3] = 255;
            }
            texels_[n - 11].create(im, false, true);
        }
    }

    void render(Ctx& c, double t) override {
        if (start_ != c.start) schedule(c);
        const Phase* ph = nullptr;
        for (const Phase& p : phases_)
            if (t >= p.t0 && t < p.t1) ph = &p;
        if (!ph) return;  // between blocks: the palette is black
        Block b = ph->b;
        // PIT ticks since the block's copy (0x9932 steps the phases, the fade and the count)
        double m = c.ticks(t, ph->t0);
        b.ph1 += b.d1 * m;
        b.ph2 += b.d2 * m;
        b.u0 += b.du * m;
        b.v0 += b.dv * m;
        double level = 8.0 * std::clamp(m, 0.0, 32.0) - 8.0 * std::clamp(m - (ph->count - 32), 0.0, 32.0);
        // per drawn frame (35.04 fps in the capture: one frame per two retraces): the oscillator 0x98e9 runs before
        // the curves of frame j = 1, 2, ..., which is on screen from about 2 j - 1.5 retraces after the block start
        // (the first retrace comes half a retrace after it on average); blended through the middle of the frames
        double r = c.retraces(t, ph->t0);
        if (ph->osc >= 0) {
            double j = (r + 0.5) / 2.0 + (ph->osc == 1 ? osc2Frames_ : 0);
            double lo = ph->osc == 0 ? 0x00 : 0x40, hi = ph->osc == 0 ? 0x40 : 0x100;
            double half = (hi - lo) / 2, centre = lo + half;
            b.amp = centre + half * kSinA(osc0_ + 0x64 * j) / 65536.0;
        }
        int pic = 0;
        if (ph->cycling) {  // frame j (0, 1, ...) of the block shows picture floor(1000 j / 65536) mod 3
            int j = int(std::floor(std::max(0.0, r - 0.5) / 2.0));
            pic = (1000 * j / 65536) % 3;
        }
        const Picture& p = c.picture(11 + pic);

        // the corners: averages of the curves' end points (0x9a62)
        auto curve = [&](double a) {
            double rad = b.amp * kSinA(a * std::ldexp(1.0, b.shift) + b.ph2) / 65536.0 + b.base;
            return std::array<double, 2>{160.0 + rad * kSinA(a + 1024) / 65536.0, 100.0 + rad * kSinA(a) / 65536.0};
        };
        const double sx = 0x333333 / 1048576.0, sy = 0x51eb85 / 1048576.0;
        auto avg = [](std::array<double, 2> p, std::array<double, 2> q) {
            return std::array<double, 2>{(p[0] + q[0]) / 2, (p[1] + q[1]) / 2};
        };
        auto TL = avg(curve(b.ph1), curve(b.ph1));                                    // top[0], left[0]
        auto TR = avg(curve(b.ph1 + sx * 319), curve(b.ph1 + 1024));                  // top[319], right[0]
        auto BL = avg(curve(b.ph1 + 3072), curve(b.ph1 - sy * 199));                  // bottom[0], left[199]
        auto BR = avg(curve(b.ph1 + 3072 - sx * 319), curve(b.ph1 + 1024 + sy * 199)); // bottom[319], right[199]

        setBlend(Blend::None);
        shader_.use();
        (p.redraw ? p.tex : texels_[size_t(pic)]).bind(0);
        shader_.set("tex", 0);
        shader_.set("sharp", p.redraw ? 0 : 1);
        shader_.set("ph1", float(std::fmod(b.ph1, 4096.0)));
        shader_.set("ph2", float(std::fmod(b.ph2, 4096.0)));
        shader_.set("lobes", float(std::ldexp(1.0, b.shift)));
        shader_.set("amp", float(b.amp));
        shader_.set("base", float(b.base));
        shader_.set("off", float(b.u0 / 256.0), float(b.v0 / 256.0));
        shader_.set("TL", float(TL[0]), float(TL[1]));
        shader_.set("TR", float(TR[0]), float(TR[1]));
        shader_.set("BL", float(BL[0]), float(BL[1]));
        shader_.set("BR", float(BR[0]), float(BR[1]));
        shader_.set("level", float(level / 256.0));
        drawFullscreen();
    }

private:
    // 0x975b: block k starts when block k - 1 has ended (count + 1 ticks) and the position has passed.
    void schedule(const Ctx& c) {
        start_ = c.start;
        const int wait[5] = {-1, 0x2800, 0x2b00, 0x2e00, 0x3100};
        double t = c.start;
        for (int k = 0; k < 5; k++) {
            Phase& p = phases_[size_t(k)];
            if (wait[k] >= 0) t = std::max(t, c.afterPos(wait[k]));
            p.t0 = t;
            p.t1 = t + (p.count + 1) / kPitHz;
            t = p.t1;
        }
        // the oscillator keeps its phase: phase 5 continues after the drawn frames of phase 2 (196 in the capture)
        osc2Frames_ = std::ceil((phases_[1].t1 - phases_[1].t0) * kRetraceHz / 2.0);
    }

    Shader shader_;
    std::array<Texture, 3> texels_;  // pictures 11..13 without a redraw: the original pixels, mipmapped
    std::array<Phase, 5> phases_;
    double osc0_ = 0, osc2Frames_ = 0;
    double start_ = -1;
};

}  // namespace

REGISTER_SCENE(Warp, "warp");
