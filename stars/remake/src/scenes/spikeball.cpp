// spikeball: a ball with long spikes (file 18) turning in front of a static purple swirl (file 19), shaded through
// a generated env map displaced by a bump map (file 17) and coloured by a material map (file 16) through the 8 ramps
// of palette 15 (mode 15); fade in over 64 ticks, from tick 624 the ball moves on a closed path, from tick 1114 it
// turns faster, fade out from tick 2094; the part ends at order 0x27 (stars/re/notes-parts3d-a.md section 4). Ports
// 0x6534 (the frame loop), 0x65ef (set-up: the env map 0x7873a from the ramp 0x6310, the height bytes (b - 8) << 1),
// the PIT callback 0x6905 (object and env angles, path step), 0x6888 (the path), 0x6791 (phases) and 0x6861 (the
// palette fade); the drawing is stars::Engine's (0x78905, 0x79483 in mode 15).
//
// Evaluated exactly instead of the original's shortcuts: the path and the angles are continuous in the PIT ticks with
// exact sin / cos (the original's 1023-amplitude table puts the ball 0.1 % nearer the centre of its path); the light
// is the ramp at the continuous radius (the engine), the fade colour * level / 64 with a continuous level. The DAC
// keeps the last fade-in level the loop sent (62 at the capture's 32.5 fps) until tick 1604 and 63 from there: here
// 63 / 64 throughout.
//
// Kept: the path stalls. The path counter [0x678c] is a byte stepped once per drawn frame: every 256th frame it is 0
// and the callback skips the path step for the ticks of that frame. Frame-rate dependent; replayed as the capture
// shows it (2, 3, 2 ticks from the ticks below, measured from full.frames: the path angle lags 4 (n - 624) by 8, 20,
// 28 units after them). Dropped: the faster rotation starting one tick late (the phase is set per drawn frame).
#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"
#include "stars/engine.h"

using namespace stars;

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Path stalls in the capture: the callback tick after which the path does not move, and for how many ticks.
constexpr double kStalls[][2] = {{1181, 2}, {1755, 3}, {2293, 2}};

class SpikeBall : public Scene {
public:
    ~SpikeBall() override {
        ramp_.tex.destroy();
        mat_.tex.destroy();
        bump_.tex.destroy();
        pal_.tex.destroy();
    }

    void init(Ctx& c) override {
        const StarsExe& x = *c.exe;
        ball_.load(x, 18);
        ramp_.load(x, 0x6310);                    // 0x7873a's curve: 57 zeros, then 1..63
        mat_.create(x.file(16));                  // material per texel: palette offset 0..8
        bump_.create(x.file(17), -8, 2);          // 0x65ef: ((b - 8) << 1) env texels
        pal15_ = vgaPalette(x.fileSpan(15));
        pal_.set(pal15_);
        // the object descriptor 0x6498: its initial angles
        a0_ = x.s16(0x6498 + 0xc);
        b0_ = x.s16(0x6498 + 0xe);
        c0_ = x.s16(0x6498 + 0x10);
    }

    void render(Ctx& c, double t) override {
        double n = std::max(0.0, c.ticks(t));
        float f = float(level(n) / 64);
        Vec3 mul(f, f, f);
        // 0x78905: the frame refilled from file 19 (rows 1..197)
        c.drawPicture(c.picture(19, {.palette = &pal15_}), {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = mul});
        // 0x6905: 2, 3, 4 units per tick about x, y, z, env angle x 0x21 per tick; from phase 4 (n = 0x45a) y + 7 and
        // env x + 0x14 more
        double fast = std::max(0.0, n - 0x45a);
        Pose p = path(pathAngle(n));
        p.a = a0_ + 2 * n;
        p.b = b0_ + 3 * n + 7 * fast;
        p.c = c0_ + 4 * n;
        Camera cam;                // at the origin with angles 0
        cam.envA = 0x21 * n + 0x14 * fast;
        cam.F = 80;                // [0x57ab6]
        cam.shift = 1;             // [0x5594c]
        Shading sh;
        sh.mode = 15;
        sh.palette = &pal_;
        sh.ramp = &ramp_;
        sh.levelMin = 1;           // the env map's 0 -> 8 (level 1)
        sh.levelMax = 30;          // 16 * (63 >> 2) = 8 * 30
        sh.tex[0] = &mat_;
        sh.bump[0] = &bump_;
        sh.mul = mul;
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam);
        eng.drawScene({{&ball_, p}}, sh);
        eng.end();
    }

private:
    // 0x6791 / 0x6861: the palette level (of 64): fade in n for n < 64, then 63; from n = 0x82e down by one per 4 ticks.
    static double level(double n) {
        if (n < 0x40) return std::min(n, 63.0);
        return std::clamp(63 - std::max(0.0, n - 0x82e) / 4, 0.0, 63.0);
    }

    // The path angle a (1/4096 turn): 4 per tick from phase 3 (n = 0x270), minus the stalled ticks.
    static double pathAngle(double n) {
        double ticks = std::max(0.0, n - 0x270);
        for (const auto& s : kStalls) ticks -= std::clamp(n - s[0], 0.0, s[1]);
        return 4 * ticks;
    }

    // 0x6888: x = 70 sin a, y = 180 cos 2a, z = 380 cos a - 810.
    static Pose path(double a) {
        double r = a * kTwoPi / 4096;
        Pose p;
        p.x = 0x46 * std::sin(r);
        p.y = 0xb4 * std::cos(2 * r);
        p.z = 0x17c * std::cos(r) - 0x32a;
        return p;
    }

    Object3D ball_;
    LightRamp ramp_;
    IndexMap mat_;
    BumpMap bump_;
    Palette pal15_;
    PaletteTexture pal_;
    double a0_ = 0, b0_ = 0, c0_ = 0;
};

}  // namespace

REGISTER_SCENE(SpikeBall, "spikeball");
