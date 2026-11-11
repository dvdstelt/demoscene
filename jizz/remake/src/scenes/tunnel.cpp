// The tunnel part 0x169e7 (04:00..06:00, jizz/re/notes-parts3d-a.md section 4). Per frame:
//   - event 400: Nix's engine scene 2 (script 0x15945: a torus of map5 with a swelling tube) full screen, the camera
//     (path 2, roll wobble on) running inside the tube;
//   - event 410: the 2D swirl 0x16cd3 (map3 through the polar table 0x16f4f, centred at (180, 100)) with the torus
//     in a 170x95 inset at (90, 77) and the white frame 0x1151c;
//   - from event 500: the swirl centred at (235, 70); at 515 / 525 / 535 (-> 510) the jittered rectangles 0x18228 /
//     0x183a9 of the key frames at 0x18325 / 0x18351 / 0x1837d through the glow overlay (blur, shift 1); at 520 the
//     torus in a 160x100 inset at the key frames' inset position, with the white frame.
// The swirl is evaluated per pixel (atan2 / hypot, filtered reads); the glow, the rectangles' jitter (random
// generator 0x141f6, continuing where the ribbon part left it) and the trail across the 520 gaps are stepped at
// the capture's 35 fps.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

// 0x16cd3: the screen through the polar table at a window of it; colour = map3[v][u], u = angle (512 units per
// turn) + du, v = 1.3 * radius + dv (core jzTexPolar, without the carry of the angle byte into the radius).
const char* kSwirlFS = R"(
out vec4 o;
uniform vec2 uSize;     // target size (pixels)
uniform vec2 uCentre;   // VGA coordinates of the table's centre
uniform vec2 uOff;      // (du, dv) in texels
uniform sampler2D uTex;
void main() {
    vec2 P = vec2(gl_FragCoord.x, uSize.y - gl_FragCoord.y) * vec2(320.0, 200.0) / uSize;
    o = vec4(jzTexPolar(uTex, P - uCentre, uOff).rgb, 1.0);
}
)";

class TunnelScene : public Scene {
public:
    void init(Ctx& c) override {
        const JizzExe& exe = *c.exe;
        jizz::prepare(c);
        tx_ = &c.shared<Textures>();
        slots_ = tx_->slotIds(true);  // map5 copied into slot 1 by the init (0x169e7)
        scene_.build(exe, 2);
        c.shared<Engine>();
        swirl_.build(kFullscreenVS, std::string(kGLSL) + kSwirlFS, "tunnel swirl");
        glow_.prepare(c);

        t400_ = rt::syncTime(c, 400);
        timer0_ = c.timer(t400_);
        const double t500 = rt::syncTime(c, 500, t400_);
        tKey_[0] = rt::syncTime(c, 515, t500);
        tKey_[1] = rt::syncTime(c, 525, t500);
        tKey_[2] = rt::syncTime(c, 535, t500);
        for (int i = 0; i < 3; i++) tInset_[i] = rt::syncTime(c, 520, tKey_[i]);

        // swirl: windows 0x16b2b (events >= 500) / 0x16bdb (410) into the 600-dword rows of the table, the table's
        // origin (0x16ff2 / 0x16ff6), the offset multipliers 0x16b0c / 0x16b19
        auto centre = [&](uint32_t insn, float* xy) {
            uint32_t w = movImm(exe, insn) / 4;  // dwords
            xy[0] = 0.5f - exe.f32(0x16ff2) - float(w % 600);
            xy[1] = 0.5f - exe.f32(0x16ff6) - float(w / 600);
        };
        centre(0x16b2b, centreLate_);
        centre(0x16bdb, centre410_);
        mulU_ = movImm(exe, 0x16b0c);
        mulV_ = movImm(exe, 0x16b19);

        // insets: 410 (0x16bea..0x16c0d), 520 (size 0x16a82 / 0x16a8c, position from the key frames)
        inset410_ = Inset{float(movImm(exe, 0x16bea)), float(movImm(exe, 0x16bf1)), float(storeImm(exe, 0x16c03)),
                          float(storeImm(exe, 0x16c0d))};
        const uint32_t cfg[3] = {movImm(exe, 0x16aa4), movImm(exe, 0x16ab5), movImm(exe, 0x16ac6)};  // mov esi, cfg
        for (int i = 0; i < 3; i++) {
            quads_[i] = rt::keyQuads(exe, cfg[i]);
            inset520_[i] = Inset{float(exe.s32(cfg[i] + 36)), float(exe.s32(cfg[i] + 40)),
                                 float(storeImm(exe, 0x16a82)), float(storeImm(exe, 0x16a8c))};
        }

        // the generator where 0x17003's rectangles left it in the capture
        rngStart_ = Rng::afterTextures(exe).skip(Rng::kRibbonCalls);
        // frames of event 510 only: the glow and the jitter stand still while 520 shows the inset
        clock_.segs = {{tKey_[0], rt::kRate2}, {tInset_[0], 0}, {tKey_[1], rt::kRate2},
                       {tInset_[1], 0},        {tKey_[2], rt::kRate2}, {tInset_[2], 0}};
        src_.params = [](int) {
            GlowStep s;  // 0x16c7d: blur; 0x16c92: shift 1
            s.blur = true;
            s.shift = 1;
            s.trailBias = rt::truncation(s.shift);
            return s;
        };
        src_.draw = [this](Ctx& cc, int k) { drawStep(cc, k); };
    }

    void render(Ctx& c, double t) override {
        const int ev = c.event(t);
        const double clocks = c.timer(t) - timer0_;
        c.clear(kBlack);
        if (ev > 499) {
            drawSwirl(c, clocks, centreLate_);
            int key = t >= tKey_[2] ? 2 : t >= tKey_[1] ? 1 : t >= tKey_[0] ? 0 : -1;
            if (ev == 515 || ev == 525 || ev == 535) {  // -> 510: the rectangles
                glow_.advance(c, clock_.steps(t), 0, src_);
                glow_.fadeTowards(c, kWhite);
            }
            if (ev == 520 && key >= 0) drawTorus(c, clocks, inset520_[key]);
        } else if (ev != 400) {  // 410
            drawSwirl(c, clocks, centre410_);
            drawTorus(c, clocks, inset410_);
        } else {
            drawTorus(c, clocks, Inset{});
        }
    }

private:
    void drawSwirl(Ctx& c, double clocks, const float* centre) {
        // 0x16b0c..0x16b20: du = dl, dv = dh of the 32x32 -> 64 products with the part's timer
        double du = std::fmod(clocks * double(mulU_) / 4294967296.0, 256.0);
        double dv = std::fmod(clocks * double(mulV_) / 1099511627776.0, 256.0);
        c.out->bind();
        setBlend(Blend::None);
        swirl_.use();
        swirl_.set("uSize", float(c.out->w), float(c.out->h));
        swirl_.set("uCentre", centre[0], centre[1]);
        swirl_.set("uOff", float(du), float(dv));
        tx_->bind(Tex::Map3, 0);
        swirl_.set("uTex", 0);
        drawFullscreen();
    }

    void drawTorus(Ctx& c, double clocks, const Inset& in) {
        EngineCamera cam = scene_.camera(clocks, true);  // [0x15c3f] = 1
        EngineShading sh;                                // plain colour tables (E 0, S 0x100, F 0)
        sh.tex = slots_;
        c.shared<Engine>().render(c, scene_, cam, EngineViewport::rect(in.x, in.y, in.w, in.h), sh);
        insetFrame(c, in);  // 0x1151c (nothing at offset 0)
    }

    // 0x16b41: the rectangles of the key frames of this phase, chosen by the row: a = (row & 15) * 2
    int keyAt(double t) const { return t >= tKey_[2] ? 2 : t >= tKey_[1] ? 1 : 0; }
    void select(const Ctx& c, double t, int& first, int& last) const {
        int a = (c.position(t) & 15) * 2;
        if (a >= 6) {
            first = std::min(a, 12) - 6;
            last = 6;
        } else {
            first = 0;
            last = a;
        }
    }

    void drawStep(Ctx& c, int n) {
        const double tk = clock_.timeOf(n) + 1e-6;
        int first, last;
        select(c, tk, first, last);
        rngTrack_.start(rngStart_, 0);
        Rng r = rngTrack_.at(n, [&](int j) {
            int f, l;
            select(c, clock_.timeOf(j) + 1e-6, f, l);
            return 4 * (l - f + 1);
        });
        segs_.clear();
        jitterQuads(r, quads_[keyAt(tk)].data(), first, last, 7, segs_);  // 0x183a9(ebp = 7)
        drawLines(c, segs_);
    }

    const Textures* tx_ = nullptr;
    std::array<GLuint, 18> slots_{};
    EngineScene scene_;
    Shader swirl_;
    Glow glow_{Glow::Overlay};
    Glow::Source src_;
    rt::StepClock clock_;

    double t400_ = 0, timer0_ = 0, tKey_[3] = {}, tInset_[3] = {};
    float centreLate_[2] = {}, centre410_[2] = {};
    uint32_t mulU_ = 0, mulV_ = 0;
    Inset inset410_, inset520_[3];
    std::vector<Quad> quads_[3];
    Rng rngStart_;
    RngTrack rngTrack_;
    std::vector<Segment> segs_;
};

}  // namespace

REGISTER_SCENE(TunnelScene, "tunnel");
