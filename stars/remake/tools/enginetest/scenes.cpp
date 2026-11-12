// Test scenes for the engine checker (enginetest.cpp): minimal ports of the motion of the spike ball (0x6534,
// callback 0x6905, path 0x6888, phases 0x6791), the textured wall (0xa0ca, callback 0xa4ac, motion 0xa656, palette
// 0xa3c9 / 0xa530), the bee (0x11e7f, callback 0x12136, camera 0x121b4, wings 0x122d7, palette 0x11fa1), the chrome
// star (0x164dc, callback 0x16646, before its first pulse) and the torso (0x16f7c, callback 0x17513, phases 0x17084,
// reflection 0x173a1, ring 0x17342, band 0x17459; from tick 160 on) as the notes give them (notes-parts3d-a sections
// 3 and 4, notes-parts3d-b sections 3 to 5), drawn with stars::Engine. They are registered under the parts' scene
// names only inside the checker, so that its shots line up with the capture through the real timeline; they leave
// out what is not the engine's (the bee's text, the star's carousel and pulses, first-frame quirks, the spike
// ball's path stall, the display's one-frame lag) and are not the parts.
#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"
#include "stars/engine.h"

using namespace stars;

bool g_sharpTexels = false;   // enginetest --sharp

namespace {

constexpr double kTwoPi = 6.283185307179586;

// One key of a table of int16 records (x, y, z, a, b, c) at addr + 12 k.
Pose key6(const StarsExe& exe, uint32_t addr) {
    return {double(exe.s16(addr)),     double(exe.s16(addr + 2)), double(exe.s16(addr + 4)),
            double(exe.s16(addr + 6)), double(exe.s16(addr + 8)), double(exe.s16(addr + 10))};
}

Pose lerp(const Pose& p, const Pose& q, double f) {
    return {p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, p.z + (q.z - p.z) * f,
            p.a + (q.a - p.a) * f, p.b + (q.b - p.b) * f, p.c + (q.c - p.c) * f};
}

// ------------------------------------------------------------------------------------------------- spike ball
struct SpikeBallTest : Scene {
    Object3D ball;
    LightRamp ramp;
    IndexMap mat;
    BumpMap bump;
    Palette pal15;
    PaletteTexture pal;
    Pose start;

    void init(Ctx& c) override {
        ball.load(*c.exe, 18);
        ramp.load(*c.exe, 0x6310);
        mat.create(c.exe->file(16));
        bump.create(c.exe->file(17), -8, 2);        // 0x6733: ((b - 8) << 1)
        pal15 = vgaPalette(c.exe->fileSpan(15));
        pal.set(pal15);
        start = Pose{0, 0, 0, double(c.exe->s16(0x6498 + 0xc)), double(c.exe->s16(0x6498 + 0xe)),
                     double(c.exe->s16(0x6498 + 0x10))};
    }

    void render(Ctx& c, double t) override {
        double n = c.ticks(t);
        // 0x6791: fade in over 64 ticks, the DAC stays at 62 / 64, fade out from n = 2094
        double level = n < 64 ? std::floor(n) : n < 2094 ? 62 : std::clamp(63 - std::floor((n - 2094) / 4), 0.0, 63.0);
        float f = float(level / 64.0);
        c.drawPicture(c.picture(19, {.palette = &pal15}),
                      {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = Vec3(f, f, f)});
        // 0x6905: angles per tick, faster from n = 1114; 0x6888: the path from n = 624 (without the stall)
        double fast = std::max(0.0, n - 1114);
        double a = 4 * std::max(0.0, n - 624);
        double ang = a * kTwoPi / 4096;
        Pose p{70 * std::sin(ang), 180 * std::cos(2 * ang), 380 * std::cos(ang) - 810,
               start.a + 2 * n, start.b + 3 * n + 7 * fast, start.c + 4 * n};
        Camera cam;
        cam.F = 80;
        cam.shift = 1;
        cam.envA = 0x21 * n + 0x14 * fast;
        Shading sh;
        sh.mode = 15;
        sh.palette = &pal;
        sh.ramp = &ramp;
        sh.levelMin = 1;  // the env map's 0 -> 8
        sh.levelMax = 30; // 16 * (63 >> 2) = 8 * 30
        sh.tex[0] = &mat;
        sh.bump[0] = &bump;
        sh.sharpTexels = g_sharpTexels;
        sh.mul = Vec3(f, f, f);
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam);
        eng.drawScene({{&ball, p}}, sh);
        eng.end();
    }
};
REGISTER_SCENE(SpikeBallTest, "spikeball");

// ------------------------------------------------------------------------------------------------- textured wall
struct WallTest : Scene {
    Object3D wall;
    Morph morph;
    LightRamp ramp;
    IndexMap texA, texB;
    BumpMap bumpA, bumpB;
    Palette p20, p22;
    PaletteTexture pal;
    Pose keys[6];

    // 0xa57d: the top-left 64 x 128 of a map tiled 4 x 2 into 256 x 256
    static std::vector<uint8_t> tiled(const std::vector<uint8_t>& m) {
        std::vector<uint8_t> o(65536);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) o[size_t(y) * 256 + size_t(x)] = m[size_t(y & 127) * 256 + size_t(x & 63)];
        return o;
    }

    void init(Ctx& c) override {
        wall.load(*c.exe, 24);
        morph.load(*c.exe, 0x9fa0, 0x9e80, 33);
        ramp.load(*c.exe, 0x9df0);
        std::vector<uint8_t> a = tiled(c.exe->file(23)), b = tiled(c.exe->file(21));
        texA.create(a);
        texB.create(b);
        bumpA.create(a, 0, 1);   // 0xa0ca: the bump selector is the texture itself
        bumpB.create(b, 0, 1);
        p20 = vgaPalette(c.exe->fileSpan(20));
        p22 = vgaPalette(c.exe->fileSpan(22));
        for (int k = 0; k < 6; k++) keys[k] = key6(*c.exe, 0xa082 + 12 * uint32_t(k));
    }

    void render(Ctx& c, double t) override {
        double n = c.ticks(t);
        // 0xa3c9 / 0xa530: fade in, the stone palette, the crossfade triangle p from t1 = 1080, fade out
        double p = n < 1080 ? 63 : std::abs(63 - std::fmod(n - 1080, 126.0));
        double fade = n < 64 ? std::floor(n) / 64 : n < 2094 ? 63.0 / 64 : std::max(0.0, 63 - std::floor(n - 2094)) / 64;
        Palette cur = p20;
        if (n >= 1079 && n < 2094)
            for (int i = 0; i < 256; i++) cur[size_t(i)] = p22[size_t(i)] + (p20[size_t(i)] - p22[size_t(i)]) * float(p / 64);
        pal.set(cur);
        float f = float(fade);
        c.drawPicture(c.picture(25, {.palette = &p20}), {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = Vec3(f, f, f)});
        // 0xa656: six keys of 338 ticks, linear
        double kf = n / 338.0;
        int k = int(std::floor(kf)) % 6;
        Pose pose = lerp(keys[k], keys[(k + 1) % 6], kf - std::floor(kf));
        wall.setShape(morph.at(n));
        Camera cam;
        cam.F = 100;
        cam.shift = 3;
        Shading sh;
        sh.mode = 15;
        sh.palette = &pal;
        sh.ramp = &ramp;
        sh.tex[0] = &texA;   // F[0] = map 23 ... F[7] = map 21
        sh.tex[1] = &texB;
        sh.texMix = float(n < 1079 ? 1.0 : p / 63.0);
        sh.bump[0] = &bumpA;
        sh.bump[1] = &bumpB;
        sh.bumpMix = sh.texMix;
        sh.sharpTexels = g_sharpTexels;
        sh.mul = Vec3(f, f, f);
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam);
        eng.drawScene({{&wall, pose}}, sh);
        eng.end();
    }
};
REGISTER_SCENE(WallTest, "wall");

// ------------------------------------------------------------------------------------------------- bee
struct BeeTest : Scene {
    World world;
    EnvTexture env;
    Palette pal26;
    PaletteTexture pal;
    Pose camKeys[9];
    Pose poses[4][7];
    int animated[7];
    std::vector<SceneObject> list;

    void init(Ctx& c) override {
        const StarsExe& x = *c.exe;
        world.load(x, 28);
        env.makeLevel(x.fileSpan(29).first(65536));
        std::vector<uint8_t> p = x.file(26);
        for (int i = 0; i < 3; i++) p[size_t(i)] = x.file(31)[size_t(i)];   // colour 0 = file 31's colour 0
        pal26 = vgaPalette(p);
        pal.set(pal26);
        for (int k = 0; k < 9; k++) camKeys[k] = key6(x, 0x11d12 + 12 * uint32_t(k));
        for (int q = 0; q < 4; q++)
            for (int i = 0; i < 7; i++) poses[q][i] = key6(x, 0x11bbe + uint32_t(x.s32(0x11b8e + 4 * uint32_t(q))) + 12 * uint32_t(i));
        for (int i = 0; i < 7; i++) animated[i] = x.s32(0x11ba2 + 4 * uint32_t(i));
    }

    void render(Ctx& c, double t) override {
        double n = c.ticks(t);
        // 0x11fa1: fade from white over 16 ticks, fade out from n = 910
        float mul = 1, add = 0;
        if (n < 16) {
            mul = float(std::min(4 * std::floor(n), 63.0) / 64);
            add = 1 - mul;
        } else if (n >= 0x38e) {
            mul = float(std::clamp(63 + std::floor((0x3ae - n) / 2), 0.0, 63.0) / 64);
        }
        c.drawPicture(c.picture(27, {.palette = &pal26}),
                      {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = Vec3(mul, mul, mul), .add = Vec3(add, add, add)});
        // 0x121b4: camera keys every 140 ticks, the bob of 64 ticks and the env angles
        double kf = n / 140.0;
        int k = int(std::floor(kf)) % 9;
        Camera cam;
        cam.pose = lerp(camKeys[k], camKeys[(k + 1) % 9], kf - std::floor(kf));
        double s = 1023 * std::sin(kTwoPi * n / 64);
        cam.envA = 100 - s / 8;
        cam.envB = 400 + s / 16 + (cam.pose.b - 0x122);
        cam.pose.y += s / 64;
        cam.F = 30;
        cam.shift = 1;
        // 0x122d7: the wings, antennae and mouth part through 4 poses of 8 ticks
        double pf = n / 8.0;
        int q = int(std::floor(pf)) & 3;
        list.clear();
        for (size_t i = 0; i < world.objects.size(); i++) list.push_back({&world.objects[i], world.poses[i]});
        for (int i = 0; i < 7; i++)
            if (animated[i] >= 0 && animated[i] < int(list.size()))
                list[size_t(animated[i])].pose = lerp(poses[q][i], poses[(q + 1) & 3][i], pf - std::floor(pf));
        Shading sh;
        sh.mode = 3;
        sh.order = Order::File;     // [0x582ac] = 1: faces in file order, the 68 objects by their centres
        sh.palette = &pal;
        sh.env = env.level(0, 1);   // file 29: indices 0..63 of the gold ramp
        sh.mul = Vec3(mul, mul, mul);
        sh.add = Vec3(add, add, add);
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam);
        eng.drawScene(list, sh);
        eng.end();
    }
};
REGISTER_SCENE(BeeTest, "bee");

// ------------------------------------------------------------------------------------------------- chrome star
struct StarTest : Scene {
    Object3D star;
    Morph morph;
    EnvTexture env;

    void init(Ctx& c) override {
        star.load(*c.exe, 51);
        morph.load(*c.exe, 0x16380, 0x15860, 3);    // ease weights, 3 keys of 128 ticks
        env.makeRgb(c.exe->fileSpan(49), vgaPalette(c.exe->bytes(0x15e60, 768)));
    }

    void render(Ctx& c, double t) override {
        double n = c.ticks(t);
        // 0x16646 up to the first beat (n < 560): fade in, rise, spin; levels stay at 127 / 128 after the fade
        double y = n <= 158 ? 1500000 : n < 358 ? 1520000 - (n - 158) * 7500 : 1520000 - 199 * 7500;
        double level = std::min(std::floor(n), 127.0) / 128;
        float f = float(level);
        star.setShape(morph.at(n));
        Camera cam;
        cam.F = 100;   // left by the wall's set-up (0xa14d)
        cam.shift = 1;
        Shading sh;
        sh.mode = 3;
        sh.order = Order::Sorted;
        sh.env = env.rgb();
        sh.mul = Vec3(f, f, f);
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam);
        eng.draw(star, Pose{0, y / 1024, -3750000.0 / 1024, 0xfc00 + 10 * n, 9 * n, 6 * n}, sh);
        eng.end();
    }
};
REGISTER_SCENE(StarTest, "star");

// ------------------------------------------------------------------------------------------------- torso
struct TorsoTest : Scene {
    Object3D torso, mirror;
    std::vector<EngineVertex> ringRef;
    LightRamp ramp;
    BumpMap bump;
    EnvTexture ringEnv;
    Palette p42;
    PaletteTexture pal;
    EngineTarget reflection;

    void init(Ctx& c) override {
        torso.load(*c.exe, 46);
        mirror.load(*c.exe, 45);
        for (size_t i = 329; i < torso.faces.size(); i++) torso.faces[i].type = 1;   // 0x16f46: the ring
        torso.touch();
        ringRef.assign(torso.verts.begin() + 177, torso.verts.end());
        ramp.load(*c.exe, 0x16c20);
        bump.create(c.exe->file(43), -4, 8, true);    // ((b - 4) << 3), read in pairs (modes 20 / 21)
        ringEnv.makeLevel(c.exe->fileSpan(29).first(65536));
        p42 = vgaPalette(c.exe->fileSpan(42));
        pal.set(p42);
        reflection.create(1024);
    }

    void render(Ctx& c, double t) override {
        double n = std::max(160.0, c.ticks(t));
        auto sinT = [](double a) { return 1023 * std::sin(kTwoPi * a / 4096); };
        auto cosT = [](double a) { return 1023 * std::cos(kTwoPi * a / 4096); };
        // 0x17084 / 0x17253: the torso's tilt, the ring circling, sliding up and down
        double k = n - 496;
        bool wobble = n >= 496;
        double ringx = wobble ? sinT(k * 128) / 8 : 0, ringz = wobble ? cosT(k * 128) / 8 : 0;
        double ringy = n < 304 ? 200 - 200 * (n - 160) / 144 : sinT((n - 304) * 16) / 32;
        Pose tp{0, 23467.0 / 1024, -0x23f000 / 1024.0, wobble ? sinT(k * 64) / 4 : 0, 43 - 12 * (n - 160), 0};
        // 0x17513: the mirror flies in from n = 736 (mirror phase 1 only: n < 1600)
        double m = std::clamp(n - 736, 0.0, 864.0);
        bool mirrorOn = n >= 736;
        Pose mp{std::min(-2000 + m * 0xda2 / 1024.0, -300.0), 0, std::min(-6000 + m * 0x1e4d / 1024.0, -2500.0),
                m, -7 * m, 3 * m};
        if (mirrorOn) {
            tp.x = std::min(m * 0x307, double(0x64000)) / 1024;
            tp.z = -0x258000 / 1024.0;
        }
        // 0x17342: the ring's vertices
        for (size_t i = 0; i < ringRef.size(); i++) {
            EngineVertex& v = torso.verts[177 + i];
            v.x = float(ringRef[i].x + ringx);
            v.y = float(ringRef[i].y + ringy * 16);
            v.z = float(ringRef[i].z + ringz);
        }
        torso.touch();
        c.drawPicture(c.picture(47, {.palette = &p42, .indexAdd = 0xe6}), {.y = 1, .h = 197, .sy = 1, .sh = 197});
        Camera cam;
        cam.F = 300;
        cam.shift = 1;
        Shading sh;   // modes 20 / 21: stone light map + bump; the ring (type 1) env-mapped with file 29 (red ramp)
        sh.mode = 20;
        sh.palette = &pal;
        sh.ramp = &ramp;
        sh.base = 0x20;
        sh.N = 1;
        sh.bump[0] = &bump;
        sh.sharpTexels = g_sharpTexels;
        sh.env = ringEnv.level(0x60, 0.5f);
        if (ringy > -100 && ringy < 100) {   // 0x17459: the band of the light map follows the ring
            sh.bandV0 = float(0x88 + ringy);
            sh.bandV1 = sh.bandV0 + 16;
            sh.bandAdd = 0x20;
        }
        Engine& eng = c.shared<Engine>();
        if (mirrorOn) {
            // 0x173a1: file 44 + the mirrored torso and ring in mode 21, then the mirror (mode 2) with that map
            reflection.fill(c, c.picture(44, {.palette = &p42}));
            Shading s21 = sh;
            s21.mode = 21;
            eng.begin(c, reflection, cam);
            eng.draw(torso, Pose{std::floor(tp.x * 1024 / 2) / 1024, tp.y, tp.z, tp.a, 0x800 - tp.b, tp.c}, s21);
            eng.end();
        }
        eng.begin(c, cam);
        if (mirrorOn) {
            Shading s2;
            s2.mode = 2;
            s2.env = reflection.env();
            eng.draw(mirror, mp, s2);
            eng.layer();   // the torso is drawn over the mirror
        }
        eng.draw(torso, tp, sh);
        eng.end();
    }
};
REGISTER_SCENE(TorsoTest, "torso");

}  // namespace
