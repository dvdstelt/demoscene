// The ribbon part 0x17003 (01:00..04:00, jizz/re/notes-parts3d-a.md section 3). Per frame, in the original's order:
//   - the ceiling and floor planes 0x17553 (map8 clouds, from 02:00 map1 / map1a turning: the mirror plane) as a
//     per-pixel perspective mapping with filtered reads, and the black band 0x1770b over the horizon;
//   - Nix's engine scene 0 (script 0x157a4: the twisted ribbon torus, and from 02:00 the map10 sphere backdrop) on
//     camera path 0 (0x15bef) at events 100 / 120 full screen and from 03:10 in the insets of 0x17003 with the white
//     frame 0x1151c; the colour tables E -200 / S 0x1c8 until the 03:10 flash 0x1a229, then E 0 / S 0x100 with the
//     inversion decaying 9 per frame (0x638b);
//   - the 8-bit glow overlay (core Glow, 0x1739c..0x17408): the rings and spokes of 0x180e5 / 0x18009 at events
//     120 / 128 and 210 / 215 / 220 (decaying at 130, 225, 230), the jittered rectangles 0x18228 / 0x183a9 at 300;
//   - the glass-letter sprites 0x174e5 (weirdtxt) at events 210 and 220 / 225;
//   - the timer fades 0x17979 / 0x17999 from white at 02:00 (after one white page 0x17737) and 03:00.
// Per-frame state (ring table, glow, rectangle jitter, inversion) is stepped at the capture's frame rates.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "jizz/core.h"
#include "jizz/engine.h"
#include "jizz/textures.h"
#include "ribbons_tunnel.h"

namespace {

using namespace jizz;
using rt::movImm;
using rt::storeImm;

// Glow phases of the part, as separate step ranges of one Glow (the buffers are empty again between them).
constexpr int kPhaseBits = 20;

// ------------------------------------------------------------------------------------------------ rings 0x180e5
// A record of the table [0x18005]: a segment from (a1, r1) to (a2, r2) in polar coordinates (angle units 2 pi /
// 16384, radius in 1/0.78 pixels), stepped per call of 0x18009 by da (16-bit wrap) and dr (8-bit wrap).
struct Ring {
    uint16_t a1, a2, da;
    uint8_t r1, r2, dr;
};

// 0x180e5: four rings (0x18181: n segments of 0x4000 / n units) and three fans of spokes (0x181bc), with the counts,
// radii, steps and angles read from the immediates of its call blocks.
std::vector<Ring> ringTable(const JizzExe& exe) {
    std::vector<Ring> v;
    const uint16_t da = exe.u16(0x181ad);  // 0x181a9: mov word [edi + 0x18], imm16
    for (uint32_t b = 0x180eb; b <= 0x1811e; b += 0x11) {  // mov ebp, n; mov eax, r; mov bl, dr; call 0x18181
        uint32_t n = movImm(exe, b), r = movImm(exe, b + 5);
        uint8_t dr = exe.u8(b + 11);
        uint16_t step = uint16_t(0x4000 / n), a = 0;
        for (uint32_t i = 0; i < n; i++, a = uint16_t(a + step))
            v.push_back({a, uint16_t(a + step), da, uint8_t(r), uint8_t(r), dr});
    }
    for (uint32_t b = 0x1812f; b <= 0x18165; b += 0x1b) {  // ... mov ecx, a0; mov edx, da << 16 | step; call 0x181bc
        uint32_t n = movImm(exe, b), r = movImm(exe, b + 5), a = movImm(exe, b + 12), d = movImm(exe, b + 17);
        uint8_t dr = exe.u8(b + 11);
        for (uint32_t i = 0; i < n; i++, a += d)
            v.push_back({uint16_t(a), uint16_t(a), uint16_t(d >> 16), uint8_t(r), uint8_t(r >> 16), dr});
    }
    return v;
}

struct RingProjection {
    double cx = 160, cy = 100, k = 0.78, unit = 0;  // 0x17ff9, 0x17ffd, 0x18001, 0x11596
    void point(double a, double r, float& x, float& y) const {
        x = float(std::cos(a * unit) * r * k + cx);
        y = float(std::sin(a * unit) * r * k * k + cy);
    }
};

// The segments 0x18009 draws after its `calls`-th call: ellipse arcs (the original's chords, subdivided so the
// rings are smooth at any resolution) and radial spokes. A record is dropped when one of its end points is off the
// page, as 0x1145c does.
void ringSegments(const std::vector<Ring>& tab, int calls, const RingProjection& p, std::vector<Segment>& out) {
    for (const Ring& g : tab) {
        uint16_t a1 = uint16_t(g.a1 + uint32_t(calls) * g.da), a2 = uint16_t(g.a2 + uint32_t(calls) * g.da);
        int r1 = (g.r1 + calls * g.dr) & 255, r2 = (g.r2 + calls * g.dr) & 255;
        Segment s;
        p.point(int16_t(a1), r1, s.x0, s.y0);
        p.point(int16_t(a2), r2, s.x1, s.y1);
        if (!onScreen(s)) continue;
        int span = int16_t(uint16_t(a2 - a1));
        if (span == 0) {  // a spoke
            out.push_back(s);
            continue;
        }
        int sub = std::max(1, (std::abs(span) + 63) / 64);
        float x = s.x0, y = s.y0;
        for (int j = 1; j <= sub; j++) {
            Segment q{x, y, 0, 0};
            p.point(int16_t(a1) + double(span) * j / sub, r1 + double(r2 - r1) * j / sub, q.x1, q.y1);
            out.push_back(q);
            x = q.x1;
            y = q.y1;
        }
    }
}

// ------------------------------------------------------------------------------------------------ shaders
// 0x17553 + 0x1770b per pixel. Row y of the ceiling (y < 95) is at depth d = 100 - y, the floor's (y > 106) at
// d = y - 99; t = H / d, half width A = 2600 t, forward B = 2500 (t - 1); the row centre (B sin a + X0, -B cos a + Y0)
// and the point at column x: centre + A (x / 160 - 1) (cos a, sin a), in 1/256 texel. The original samples at the
// pixel's left edge and truncates to a texel: here at continuous positions, filtered.
const char* kPlaneFS = R"(
out vec4 o;
uniform vec2 uSize;     // target size (pixels)
uniform vec3 uP;        // H, X0, Y0 (1/256 texel)
uniform vec2 uSC;       // sin, cos of the angle
uniform vec2 uK;        // forward 2500 [0x17531], half width 2600 [0x17543]
uniform vec2 uBand;     // black rows [95, 107)
uniform sampler2D uCeil, uFloor;
void main() {
    vec2 P = vec2(gl_FragCoord.x, uSize.y - gl_FragCoord.y) * vec2(320.0, 200.0) / uSize;
    bool top = P.y < 100.0;
    float d = max(top ? 100.5 - P.y : P.y - 99.5, 0.5);
    float t = uP.x / d;
    float A = t * uK.y, B = (t - 1.0) * uK.x;
    float xf = (P.x - 0.5) / 160.0 - 1.0;
    vec2 w = vec2(B * uSC.x + uP.y, -B * uSC.y + uP.z) + A * xf * vec2(uSC.y, uSC.x);
    vec2 T = w * (1.0 / 256.0) - 0.5;     // texel i shown where the 8.16 coordinate is in [i, i + 1)
    vec3 cc = jzTex256(uCeil, T).rgb, cf = jzTex256(uFloor, T).rgb;
    float hy = 100.0 / uSize.y;           // half an output pixel in VGA rows
    float band = clamp(min(P.y + hy, uBand.y) - max(P.y - hy, uBand.x), 0.0, 2.0 * hy) / (2.0 * hy);
    o = vec4((top ? cc : cf) * (1.0 - band), 1.0);
}
)";

// 0x174e5: a 16-bit picture copied to the page except where the 5:6:5 word is 0 (here: below one step of every
// channel, anti-aliased on the filtered colour).
const char* kSpriteVS = R"(
uniform vec4 rect;   // VGA x, y, w, h
uniform vec4 uvr;    // texel coordinates (256 grid) of the corners
out vec2 tc;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    tc = mix(uvr.xy, uvr.zw, c);
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
const char* kSpriteFS = R"(
in vec2 tc;
out vec4 o;
uniform sampler2D tex;
void main() {
    vec3 c = texture(tex, tc / 256.0).rgb;
    float m = max(max(c.r * (255.0 / 8.0), c.g * (255.0 / 4.0)), c.b * (255.0 / 8.0));   // >= 1: word != 0
    float a = clamp((m - 1.0) / max(fwidth(m), 1e-4) + 0.5, 0.0, 1.0);
    o = vec4(c, a);
}
)";

// ------------------------------------------------------------------------------------------------ the part
class RibbonsScene : public Scene {
public:
    void init(Ctx& c) override {
        const JizzExe& exe = *c.exe;
        jizz::prepare(c);
        tx_ = &c.shared<Textures>();
        slots_ = tx_->slotIds(false);
        scene_.build(exe, 0);
        c.shared<Engine>();
        plane_.build(kFullscreenVS, std::string(kGLSL) + kPlaneFS, "ribbons planes");
        sprite_.build(kSpriteVS, kSpriteFS, "ribbons sprite");
        glow_.prepare(c);

        // sync entries (events are the part's code constants)
        t100_ = rt::syncTime(c, 100);
        t120_ = rt::syncTime(c, 120, t100_);
        t128_ = rt::syncTime(c, 128, t100_);
        t130_ = rt::syncTime(c, 130, t100_);
        t201_ = rt::syncTime(c, 201, t100_);
        t210_ = rt::syncTime(c, 210, t100_);
        t225_ = rt::syncTime(c, 225, t100_);
        t301_ = rt::syncTime(c, 301, t100_);
        t311_ = rt::syncTime(c, 311, t100_);
        t315_ = rt::syncTime(c, 315, t100_);
        t320_ = rt::syncTime(c, 320, t100_);
        timer0_ = c.timer(t100_);  // [0x6542] = -xmpGetTimer()

        // planes 0x17167..0x171d8 / 0x17553
        hStart_ = int32_t(exe.u32(0x17199));  // add eax, 3660
        hMin_ = int32_t(exe.u32(0x1719e));    // cmp eax, 600
        kFwd_ = float(exe.s32(0x17531));
        kHalf_ = float(exe.s32(0x17543));
        unit_ = exe.f64(0x11596);

        // engine colour tables: 0x1703f / 0x17044 (E, S), the flash 0x171f4 / 0x171fb (F, decay per frame)
        tableE_ = float(int32_t(movImm(exe, 0x1703f)));
        tableS_ = float(storeImm(exe, 0x17044));
        flashF_ = exe.u8(0x171fa);
        flashDec_ = exe.u8(0x17201);

        // insets: event 201 (0x170c4..), 315 (0x17222..), 320 (0x17265..)
        auto inset = [&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
            return Inset{float(movImm(exe, x)), float(movImm(exe, y)), float(storeImm(exe, w)), float(storeImm(exe, h))};
        };
        inset201_ = inset(0x170c4, 0x170cb, 0x170dd, 0x170e7);
        inset315_ = inset(0x17222, 0x17229, 0x1723b, 0x17245);
        inset320_ = inset(0x17265, 0x1726c, 0x1727e, 0x17288);

        // fades: 0x170fb (02:00), 0x1711b (03:00)
        fade201_ = movImm(exe, 0x170fb);
        fade301_ = movImm(exe, 0x1711b);
        white_ = 1.0 / rt::kRate2;  // the white page 0x17737 is one frame; the fade starts after its flip

        // sprites 0x174e5 at events 210 (0x17419..) and 220 / 225 (0x1744b..): source offset, x, y, cl = w, ch = h
        auto sprite = [&](uint32_t src, uint32_t x, uint32_t y, uint32_t wh) {
            uint32_t s = wh;
            return Sprite{float(movImm(exe, x)), float(movImm(exe, y)), float(s & 0xff), float((s >> 8) & 0xff),
                          float(src / 512)};  // 256 words per row
        };
        sprite1_ = sprite(0, 0x1741f, 0x17424, movImm(exe, 0x17429));
        sprite2_ = sprite(exe.u32(0x17453), 0x17457, 0x1745c, movImm(exe, 0x17461));

        // rings and rectangles
        rings_ = ringTable(exe);
        ringProj_ = {exe.f32(0x17ff9), exe.f32(0x17ffd), exe.f32(0x18001), unit_};
        quads_ = rt::keyQuads(exe, 0x181f4);  // precalc 0x18228 on the configuration at 0x181f4
        rngStart_ = Rng::afterTextures(exe);

        // per-frame clocks of the three glow phases (rates of the capture: event 120 and 300 while the timer fade
        // runs at 23.3 fps, the rest at 35 fps)
        double fadeLen = 1023.0 * 4294967296.0 / double(fade301_) / rt::kPitHz;
        clock_[0].segs = {{t120_, rt::kRate3}, {t128_, rt::kRate2}};
        clock_[1].segs = {{t210_, rt::kRate2}};
        clock_[2].segs = {{t301_, rt::kRate3}, {t301_ + fadeLen, rt::kRate2}};
        phaseEnd_[0] = t201_;
        phaseEnd_[1] = t301_;
        phaseEnd_[2] = t311_;
        phaseStart_[0] = t120_;
        phaseStart_[1] = t210_;
        phaseStart_[2] = t301_;

        src_.params = [](int k) {
            GlowStep s;
            bool rects = (k >> kPhaseBits) == 2;  // 0x17361 / 0x17368: blur and shift 1; rings 0x172fb: 0 and 2
            s.blur = rects;
            s.shift = rects ? 1 : 2;
            s.trailBias = rt::truncation(s.shift);
            return s;
        };
        src_.draw = [this](Ctx& cc, int k) { drawStep(cc, k); };
    }

    void render(Ctx& c, double t) override {
        const int ev = c.event(t);
        if (t >= t201_ && t < t201_ + white_) {  // 0x17737: one white page, flipped
            c.clear(kWhite);
            return;
        }
        const double clocks = c.timer(t) - timer0_;
        const bool late = t >= t201_;  // map1 / map1a, sphere backdrop, roll wobble, inset frame

        drawPlanes(c, clocks, late);

        if (t < t128_ || t >= t311_) drawObjects(c, t, clocks, late);  // event <= 120 or 310

        for (int ph = 0; ph < 3; ph++)
            if (t >= phaseStart_[ph] && t < phaseEnd_[ph]) {
                const int base = ph << kPhaseBits;
                glow_.advance(c, base + clock_[ph].steps(t), base, src_);
                glow_.fadeTowards(c, kWhite);  // 0x11396 with the tables of 0x1131b (white)
            }

        if (ev == 210) drawSprite(c, sprite1_);
        if (ev == 220 || ev == 225) drawSprite(c, sprite2_);

        if (t >= t301_) timerFade(c, t, t301_, fade301_);  // 0x17999
        else if (t >= t201_) timerFade(c, t, t201_ + white_, fade201_);
    }

private:
    struct Sprite {
        float x, y, w, h, row;
    };

    void drawPlanes(Ctx& c, double clocks, bool late) {
        double H, X0 = 0, Y0, a = 0;
        if (!late) {  // map8: drifting forwards, the planes coming closer (H falls to 600)
            H = std::max<double>(hMin_, hStart_ - 3.0 * clocks / 8192.0);
            Y0 = std::fmod(clocks / 256.0, 65536.0);
        } else {      // map1 / map1a: turning, drifting and racing forwards
            H = hMin_;
            double e = clocks / 16.0;
            Y0 = 65536.0 - std::fmod(e, 65536.0);
            X0 = std::fmod(e / 8.0, 65536.0);
            a = e / 128.0;
        }
        c.out->bind();
        setBlend(Blend::None);
        plane_.use();
        plane_.set("uSize", float(c.out->w), float(c.out->h));
        plane_.set("uP", float(H), float(X0), float(Y0));
        plane_.set("uSC", float(std::sin(a * unit_)), float(std::cos(a * unit_)));
        plane_.set("uK", kFwd_, kHalf_);
        plane_.set("uBand", 95.0f, 107.0f);  // 0x1770b: 0x1e00 bytes from row 95
        tx_->bind(late ? Tex::Map1 : Tex::Map8, 0);
        tx_->bind(late ? Tex::Map1a : Tex::Map8, 1);
        plane_.set("uCeil", 0);
        plane_.set("uFloor", 1);
        drawFullscreen();
    }

    void drawObjects(Ctx& c, double t, double clocks, bool late) {
        Inset in;  // 320x200 at (0, 0) until event 201
        if (late) in = t >= t320_ ? inset320_ : t >= t315_ ? inset315_ : inset201_;
        scene_.setVisible(0, late);  // the sphere backdrop: flags 0x43 until event 201
        EngineCamera cam = scene_.camera(clocks, late);
        EngineShading sh;
        sh.tex = slots_;
        if (t < t311_) {
            sh.E = tableE_;
            sh.S = tableS_;
            sh.F = 0;
        } else {  // 0x1a229 from the flash on: E 0 ([0x6387]), S 0x100, F decaying per frame
            sh.E = 0;
            sh.S = 256;
            sh.F = inversionDecay(t, t311_, rt::kRate2, flashF_, flashDec_);
        }
        c.shared<Engine>().render(c, scene_, cam, EngineViewport::rect(in.x, in.y, in.w, in.h), sh);
        insetFrame(c, in);  // 0x1151c (nothing at offset 0)
    }

    // One drawn frame of a glow phase: the lines it draws into the raw buffer.
    void drawStep(Ctx& c, int k) {
        const int ph = k >> kPhaseBits, n = k & ((1 << kPhaseBits) - 1);
        const double tk = clock_[ph].timeOf(n) + 1e-6;  // when frame n was drawn
        segs_.clear();
        if (ph == 0 && tk < t130_) ringSegments(rings_, n + 1, ringProj_, segs_);  // events 120, 128
        if (ph == 1 && tk < t225_) ringSegments(rings_, n + 1, ringProj_, segs_);  // events 210 / 215 / 220
        if (ph == 2) {                                                             // event 300
            int first, last;
            select(c, tk, first, last);
            rngTrack_.start(rngStart_, 0);
            Rng r = rngTrack_.at(n, [&](int j) {
                int f, l;
                select(c, clock_[2].timeOf(j) + 1e-6, f, l);
                return 4 * (l - f + 1);
            });
            jitterQuads(r, quads_.data(), first, last, 7, segs_);  // 0x183a9(ebp = 7)
        }
        drawLines(c, segs_);
    }

    // 0x1731a: the rectangles shown in the frame at music time t (by the row of the music position)
    void select(const Ctx& c, double t, int& first, int& last) const {
        int row = c.position(t) & 0xff;
        if (row >= 7) {
            first = std::min(row, 14) - 7;
            last = 7;
        } else {
            first = 0;
            last = row;
        }
    }

    void drawSprite(Ctx& c, const Sprite& s) {
        c.out->bind();
        setBlend(Blend::Alpha);
        sprite_.use();
        tx_->bind(Tex::Weirdtxt, 0);
        sprite_.set("tex", 0);
        sprite_.set("rect", s.x, s.y, s.w, s.h);
        sprite_.set("uvr", 0.0f, s.row, s.w, s.row + s.h);
        drawQuad();
        setBlend(Blend::None);
    }

    const Textures* tx_ = nullptr;
    std::array<GLuint, 18> slots_{};
    EngineScene scene_;
    Shader plane_, sprite_;
    Glow glow_{Glow::Overlay};
    Glow::Source src_;
    rt::StepClock clock_[3];
    double phaseStart_[3] = {}, phaseEnd_[3] = {};

    double t100_ = 0, t120_ = 0, t128_ = 0, t130_ = 0, t201_ = 0, t210_ = 0, t225_ = 0, t301_ = 0, t311_ = 0,
           t315_ = 0, t320_ = 0, timer0_ = 0, white_ = 0;
    int32_t hStart_ = 3660, hMin_ = 600;
    float kFwd_ = 2500, kHalf_ = 2600;
    double unit_ = 0;
    float tableE_ = 0, tableS_ = 256;
    int flashF_ = 255, flashDec_ = 9;
    Inset inset201_, inset315_, inset320_;
    uint32_t fade201_ = 0, fade301_ = 0;
    Sprite sprite1_{}, sprite2_{};

    std::vector<Ring> rings_;
    RingProjection ringProj_;
    std::vector<Quad> quads_;
    Rng rngStart_;
    RngTrack rngTrack_;
    std::vector<Segment> segs_;
};

}  // namespace

REGISTER_SCENE(RibbonsScene, "ribbons");
