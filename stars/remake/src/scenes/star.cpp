// star: the chrome star, 58:21-65:0a (stars/re/notes-parts3d-b.md section 4). An env-mapped star (file 51, three
// morph shapes, env map file 49 through palette 0x15e60) fades in, rises from below, spins, darkens on an
// accelerating beat and flies into the camera while the screen fades out with a bounce; behind it a ring of 16 small
// spiky objects (file 50) turns along the bottom. Ports 0x164dc (set-up and loop: morph with the ease weights
// 0x15860, F 100 left by 0xa14d, shift 1, mode 3), 0x16646 (the PIT callback: angles, rise, beats and pulses, the
// flight and the bounce fade, stepped tick by tick as the original), 0x168a3 (the palette levels towards black or
// white), 0x16acc (the carousel frame (t >> 1) & 63), and for the carousel the precalc 0x16945 / 0x16ba1 (placement:
// a ring of radius 256 at y 100 around z -190, F 20, shift 3) and 0x16b1e (rows 78.. kept, colours mapped through
// the table 0x15a60) (stars/re/notes-parts3d-a.md section 5).
//
// Evaluated exactly instead of the original's shortcuts: the callback's state is stepped per tick exactly and the
// position, angles and palette levels are interpolated between ticks; the shape is morphed at the fractional tick
// with the ease weights interpolated between entries; the palette levels are a per-channel colour map (c l / 2^s
// towards black, c + (1 - c) (l - 2^s) / 2^s towards white) applied in float to the true-colour env maps. The
// carousel is rendered live at the continuous frame (t / 2) instead of 64 pre-rendered 320x200 frames played at 35
// frames a second; its colours are the env map's indices mapped through 0x15a60 (as the RLE encoder does) and then
// the star's palette, so it pulses and fades with the star. Kept: object 0 of the ring turned by the frame number
// minus one instead of tangentially (a register left over in 0x16ba1), the rows above 78 cut off, the small jump
// down when the rise starts (tick 159). Dropped: the race between the callback and 0x168a3 (6 frames of the capture
// mix two ticks' palettes).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"
#include "stars/engine.h"

using namespace stars;

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr int kEndTick = 1806;   // 0x16646 sets phase 2 at tick 1805 (black); the loop returns at its next test

// The callback's state (0x16360 object record, 0x164bb.. levels and beat variables), as integers.
struct StarState {
    int t = 0;
    int64_t x = 0, y = 0, z = 0;    // position in 1/1024 units
    int64_t a = 0, b = 0, c = 0;     // angles, not wrapped (only their value mod 4096 is used)
    int lv[3] = {0, 0, 0};
    int shift = 6;
    int bspeed = 0, interval = 0, next = 0, flash = 0, dec = 0;
    int phase = 0, vz = 0, fi = 0;
};

struct Tables {
    std::array<int, 64> t1{}, t2{};    // 0x15960, 0x159e0: round(32 / 22 cos(2 pi n / 64))
    std::array<int, 256> t3{};         // 0x15660: the bounce fade
    StarState initial;
};

// 0x16646: one PIT tick
void tick(StarState& s, const Tables& tb) {
    s.t++;
    int t = s.t;
    s.a += 10;
    s.b += s.bspeed;
    s.c += 6;
    if (t < 128) {                                   // fade in from black
        s.lv[0] = s.lv[1] = s.lv[2] = t;
        s.shift = 7;
    } else if (t <= 158) {
    } else if (t < 358) {
        s.y = 0x173180 - int64_t(t - 158) * 7500;     // rises to y = 27500 (1/1024 units)
    } else if (s.flash) {                            // pulse: darker, spinning faster
        s.flash--;
        s.shift = 6;
        s.a += 18;
        s.lv[0] = s.lv[1] = (tb.t2[size_t(s.flash)] + 42) & 255;
        s.lv[2] = ((tb.t1[size_t(s.flash)] >> 1) + 48) & 255;
    } else if (s.phase != 1 && t == s.next) {        // beat: the intervals shrink, b spins faster
        s.flash = 63;
        s.interval -= s.dec;
        s.dec += 6;
        s.bspeed += 2;
        if (s.interval < 80) {
            s.interval = 80;
            s.phase = 1;
            s.fi = 0;
        }
        s.next += s.interval;
    }
    if (s.phase == 1) {                              // flight into the camera, then the bounce fade
        s.vz += 185;
        s.z += s.vz;
        if (s.vz > 9200) {
            s.shift = 6;
            s.fi++;
            s.lv[0] = s.lv[1] = s.lv[2] = tb.t3[size_t(s.fi & 255)];
            if (s.fi >= 255) {
                s.fi = 255;
                s.phase = 2;
                s.lv[0] = s.lv[1] = s.lv[2] = 0;
            }
        }
    }
}

// 0x168a3 as a colour map per channel: out = c * mul + add
void levelMap(const StarState& s, Vec3* mul, Vec3* add) {
    float k = float(1 << s.shift);
    float m[3], d[3];
    for (int ch = 0; ch < 3; ch++) {
        float l = float(s.lv[ch]);
        if (l < k) {
            m[ch] = l / k;     // c l >> shift
            d[ch] = 0;
        } else {
            float f = (l - k) / k;   // c + (63 - c) (l - k) >> shift
            m[ch] = 1 - f;
            d[ch] = f;
        }
    }
    *mul = Vec3(m[0], m[1], m[2]);
    *add = Vec3(d[0], d[1], d[2]);
}

struct Star : Scene {
    Object3D star, spiky;
    Morph morph;
    EnvTexture env, carouselEnv;
    Tables tables;
    float spikyC[16] = {};

    void init(Ctx& c) override {
        const StarsExe& x = *c.exe;
        star.load(x, 51);
        morph.load(x, 0x16380, 0x15860, 3);    // shift 7, 128 ticks per key, keys 0 1 2 0, ease weights
        spiky.load(x, 50);
        Palette pal = vgaPalette(x.bytes(0x15e60, 768));
        std::span<const uint8_t> e49 = x.fileSpan(49);
        env.makeRgb(e49, pal);
        // 0x16b1e: the carousel's pixels went through the table 0x15a60 (darker colours of the same palette)
        std::vector<uint8_t> dim(65536, 0);
        for (size_t i = 0; i < dim.size() && i < e49.size(); i++) dim[i] = x.u8(0x15a60 + e49[i]);
        carouselEnv.makeRgb(dim, pal);
        for (size_t n = 0; n < 64; n++) {
            tables.t1[n] = x.s16(0x15960 + 2 * uint32_t(n));
            tables.t2[n] = x.s16(0x159e0 + 2 * uint32_t(n));
        }
        for (size_t n = 0; n < 256; n++) tables.t3[n] = x.s16(0x15660 + 2 * uint32_t(n));
        StarState& s = tables.initial;   // the record 0x16360 and the beat variables as the image holds them
        s.x = x.s32(0x16360);
        s.y = x.s32(0x16364);
        s.z = x.s32(0x16368);
        s.a = x.u16(0x1636c);
        s.b = x.u16(0x1636e);
        s.c = x.u16(0x16370);
        s.bspeed = x.s16(0x164c1);
        s.interval = x.s32(0x164c3);
        s.next = x.s32(0x164c7);
        s.flash = x.s32(0x164cb);
        s.dec = x.s32(0x164cf);
        s.phase = x.u8(0x164d3);
        s.vz = x.s32(0x164d4);
        s.fi = x.s32(0x164d8);
        for (int i = 0; i < 16; i++) spikyC[i] = float(x.s16(0x16160 + 32 * uint32_t(i) + 0x10));
    }

    // The state after `whole` ticks (replayed from the start: at most 1806 cheap steps).
    StarState stateAt(int whole) const {
        StarState s = tables.initial;
        while (s.t < whole) tick(s, tables);
        return s;
    }

    // 0x16acc / 0x16ba1: the ring of 16 objects of file 50 at the continuous carousel frame f (0..64)
    void drawCarousel(Ctx& c, Engine& eng, double f, const Vec3& mul, const Vec3& add) {
        Camera cam;
        cam.F = 0x14;     // 0x16945: [0x57ab6] = 0x14, [0x5594c] = 3
        cam.shift = 3;
        Shading sh;
        sh.mode = 3;
        sh.env = carouselEnv.rgb();
        sh.mul = mul;
        sh.add = add;
        SceneObject list[16];
        double place = 16 * std::fmod(f, 32.0);   // ang = 16 (f & 31) + 256 i
        for (int i = 0; i < 16; i++) {
            double ang = place + 256 * i, r = ang * kTwoPi / 4096;
            double ay = i ? ang - 0x500 : f - 1;   // object 0: the previous frame number (EBX left over)
            Pose p{1023.0 / 4 * std::cos(r), 100, 1023.0 / 4 * std::sin(r) - 0xbe, 0x400, ay, spikyC[i]};
            list[i] = {&spiky, p};
        }
        // the RLE keeps rows 78..199 of the frame (0x6180), the screen copy rows 1..197
        View v = View::screen();
        v.clipY = 78;
        v.clipH = 198 - 78;
        eng.begin(*c.out, v, cam);
        eng.drawScene(std::span<const SceneObject>(list, 16), sh);
        eng.end();
    }

    void render(Ctx& c, double t) override {
        double n = std::clamp(c.ticks(t), 0.0, double(kEndTick));
        int whole = int(std::floor(n));
        float fr = float(n - whole);
        StarState s0 = stateAt(whole), s1 = s0;
        tick(s1, tables);
        auto mix = [&](double a, double b) { return a + (b - a) * double(fr); };
        Vec3 m0, a0, m1, a1;
        levelMap(s0, &m0, &a0);
        levelMap(s1, &m1, &a1);
        Vec3 mul = m0 + (m1 - m0) * fr, add = a0 + (a1 - a0) * fr;

        Engine& eng = c.shared<Engine>();
        // 0x78948 cleared the frame to black; the carousel frame (t >> 1) & 63 first, the star over it
        drawCarousel(c, eng, std::fmod(n / 2, 64.0), mul, add);

        star.setShape(morph.at(n));   // 0x78ffe runs before the draw here: no lag
        Camera cam;
        cam.F = 100;      // left by the wall's set-up 0xa14d
        cam.shift = 1;
        Shading sh;
        sh.mode = 3;
        sh.env = env.rgb();
        sh.mul = mul;
        sh.add = add;
        Pose pose{double(s0.x), mix(double(s0.y), double(s1.y)), mix(double(s0.z), double(s1.z)),
                  mix(double(s0.a), double(s1.a)), mix(double(s0.b), double(s1.b)), mix(double(s0.c), double(s1.c))};
        pose.x /= 1024;   // direct draw: camera space, the record's x.10
        pose.y /= 1024;
        pose.z /= 1024;
        eng.begin(c, cam);
        eng.draw(star, pose, sh);
        eng.end();
    }
};
REGISTER_SCENE(Star, "star");

}  // namespace
