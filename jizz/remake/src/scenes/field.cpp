// The field lines: part 0x17c8a with its table 0x17b2f. map15 read through the signed sums, over six moving poles
// (three +, three -), of their angles (the field lines of three positive and three negative charges) and of a
// three-lobed distance; one white page while the table is built, white timer flashes (0x17979 / 0x17999) at the
// start and at 10:00. jizz/re/notes-parts2d 6.
//
// Per pixel, at the output resolution, for the pixel position P (VGA coordinates) and the poles Q_k:
//   d_k = P - Q_k,  th_k = atan2(d_k),  rho_k = |d_k|,  s_k = +1, -1, +1, -1, +1, -1
//   row = (sum s_k th_k * 20860.8 - 0x5678) / 256       (the angle words: 65536 per half turn)
//   col = (sum s_k 45.6 rho_k / (0.9 + 0.5 sin 3 th_k) - 0x1234) / 256
// read with a filtered, wrapping lookup whose footprint comes from the analytic derivatives (the angle sum is
// continuous modulo 256 everywhere, so there is no seam). Dropped: the 600x340 table sampled at whole pixels, the
// whole-pixel pole positions, the truncated texel indices and the 32 steps of the fades.
//
// The pole speed tau = 3 t S / 2^24 (t in PIT clocks since the start) uses S, which drops by 3 per drawn frame
// from 1021 to 512: this is stepped on the capture's frame clock (35 fps while the start flash is applied, 70 fps
// after it) and blended between frames.
#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "jizz/core.h"
#include "jizz/textures.h"

namespace {
using namespace jizz;

constexpr int kPos = 0x0d00;                // 0d:00
constexpr double kPit = 1193182.0;
constexpr uint32_t kStartRate = 0x2d9686;   // 0x17cd6: flash at the loop start (1.233 s)
constexpr uint32_t kRate10 = 0x249f00;      // 0x17cfb: flash at event 0x1000 (10:00, 1.534 s)
// Measured in the capture (full.flips, jizz/re/models/parts2d/out/field_check.txt): the white page shows while the
// table is built; the loop's start timer (109397012) is read 109983 clocks (0.0922 s) after the music reaches 0d:00,
// and the second loop frame reads the timer 30337 clocks after it (then 35 fps while the start flash lasts, 70 fps
// after it).
constexpr double kSetup = 109983.0 / kPit;
constexpr double kFrame1 = 30337.0 / kPit;
constexpr int kPoleColumn[6] = {2, 10, 13, 19, 12, 15};  // immediates in 0x17d38..0x17f60 (pole k's column offset)

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform vec2 size;
uniform vec2 Q[6];      // pole positions (VGA coordinates)
uniform vec4 k;         // (angle scale 20860.8 / 256, radius scale 45.6 / 256, 0x5678 / 256, 0x1234 / 256)
uniform vec2 lobe;      // (0.5, 0.9): g = lobe.x sin 3th + lobe.y
void main() {
    vec2 P = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    vec2 dPx = dFdx(P), dPy = dFdy(P);
    float su = 0.0, sv = 0.0;
    vec2 gu = vec2(0.0), gv = vec2(0.0);   // gradients of col and row (per VGA unit)
    for (int i = 0; i < 6; i++) {
        float s = (i & 1) == 0 ? 1.0 : -1.0;
        vec2 d = P - Q[i];
        float r2 = max(dot(d, d), 1e-12), rho = sqrt(r2);
        float sn = d.y / rho, cs = d.x / rho;
        float s3 = sn * (3.0 - 4.0 * sn * sn), c3 = cs * (4.0 * cs * cs - 3.0);   // sin 3th, cos 3th
        float g = lobe.y + lobe.x * s3;
        vec2 gth = vec2(-d.y, d.x) / r2;
        sv += s * atan(d.y, d.x);
        gv += s * gth;
        su += s * rho / g;
        gu += s * (d / rho * g - rho * 3.0 * lobe.x * c3 * gth) / (g * g);
    }
    vec2 t = vec2(su * k.y - k.w, sv * k.x - k.z);   // (col, row) in texels
    vec2 gU = gu * k.y, gV = gv * k.x;
    vec2 dx = vec2(dot(gU, dPx), dot(gV, dPx)), dy = vec2(dot(gU, dPy), dot(gV, dPy));
    o = vec4(jzTex256Grad(tex, t, dx, dy).rgb, 1.0);
}
)";

class FieldScene : public Scene {
public:
    void init(Ctx& c) override {
        c.shared<Textures>();
        jizz::prepare(c);
        const JizzExe& e = *c.exe;
        angScale_ = e.f32(0x17c1a) * e.f32(0x17c2e);  // 10430.4 * 2: 65536 per half turn
        lobeA_ = e.f32(0x17c22);                       // 0.5
        lobeB_ = e.f32(0x17c26) + e.f32(0x17c22);      // 0.4 + 0.5
        radScale_ = e.f32(0x17c2a);                    // 45.6
        ampX_ = float(e.u32(0x17c4e));                 // 124
        ampY_ = float(e.u32(0x17c52));                 // 67
        for (int i = 0; i < 12; i++) freq_[i] = e.f32(0x17c5a + 4 * i);  // fx1, fy1, fx2, fy2, ...
        pos_ = c.positionTime(kPos);
        start_ = pos_ + kSetup;
        // the first frame at or after the end of the start flash ends the 35 fps phase
        const double flashEnd = 1023.0 * 4294967296.0 / kStartRate / kPit;
        switch_ = int(std::ceil((flashEnd - kFrame1) * 35.0)) + 1;
        sh_.build(kFullscreenVS, std::string(kGLSL) + kFS, "field");
    }

    // Loop frames since the start (fractional): frame 0 at 0, frame 1 at kFrame1, then 35 fps up to frame switch_,
    // 70 fps after it.
    double frames(double tp) const {
        if (tp < kFrame1) return std::max(0.0, tp / kFrame1);
        const double tSwitch = kFrame1 + (switch_ - 1) / 35.0;
        if (tp < tSwitch) return 1.0 + (tp - kFrame1) * 35.0;
        return switch_ + (tp - tSwitch) * 70.0;
    }

    void render(Ctx& c, double t) override {
        if (t < start_) {  // 0x17c8f: one white page while 0x17b2f builds the table
            c.clear(kWhite);
            return;
        }
        const Textures& tx = c.shared<Textures>();
        const double tp = c.timer(t) - c.timer(start_);  // time() in PIT clocks
        // S: 0x400 - 3 per frame, at least 0x200 (blended between frames: piecewise linear)
        const double S = std::max(512.0, 1024.0 - 3.0 * (frames((t - start_)) + 1.0));
        const double tau = 3.0 * (tp / 16.0) * S / 1048576.0;
        float Q[12];
        for (int i = 0; i < 6; i++) {
            const double X = (std::sin(tau * freq_[2 * i]) + 1.0) * ampX_;
            const double Y = (std::sin(tau * freq_[2 * i + 1]) + 1.0) * ampY_;
            Q[2 * i] = float(300.0 - X - kPoleColumn[i]);  // table column X + c + x, x0 = -299.5
            Q[2 * i + 1] = float(170.0 - Y);                // table row Y + y, y0 = -169.5
        }
        c.out->bind();
        setBlend(Blend::None);
        sh_.use();
        tx.bind(Tex::Map15, 0);
        sh_.set("tex", 0);
        sh_.set("size", float(c.out->w), float(c.out->h));
        gl::Uniform2fv(sh_.loc("Q"), 6, Q);
        sh_.set("k", angScale_ / 256.0f, radScale_ / 256.0f, float(0x5678) / 256.0f, float(0x1234) / 256.0f);
        sh_.set("lobe", lobeA_, lobeB_);
        drawFullscreen();
        double since = 0;
        if (c.event(t, &since) == 0x1000) timerFade(c, t, since, kRate10, kWhite);  // the part sets 0x1001
        else timerFade(c, t, start_, kStartRate, kWhite);
    }

private:
    Shader sh_;
    double pos_ = 0, start_ = 0;
    int switch_ = 44;
    float angScale_ = 0, radScale_ = 0, lobeA_ = 0, lobeB_ = 0, ampX_ = 0, ampY_ = 0;
    float freq_[12] = {};
};

REGISTER_SCENE(FieldScene, "field");
}  // namespace
