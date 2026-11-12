// torso: the stone torso, the red ring and the pink mirror, 6c:01-7c:2f (stars/re/notes-parts3d-b.md section 5).
// A stone torso (file 46, light-mapped with the stone frieze file 43 as bump map) rises turning in front of a dark
// green landscape (file 47); a red ring (part of file 46, env map file 29 through the red ramp) rises from below and
// slides up and down it, its height marked on the torso as a blue band of the light map; later a twisted pink mirror
// (file 45) flies in and reflects the torso and the ring, which are rendered into its env map (file 44) every frame.
// Ports 0x16d86 (set-up: objects, maps, palette 42, light map 0x7873a DL 2 + 0x20, F 300, shift 1), 0x16f7c (the
// loop), 0x17084 / 0x17253 (the phases per drawn frame: fades, rise, ring height, wobble), 0x17513 (the PIT
// callback: spin, mirror flight, torso drift), 0x17342 (the ring's vertices), 0x17459 / 0x1748b (the band), 0x173a1
// (the mirrored instance drawn into the env map in mode 21), and the draws in modes 2 and 20.
//
// Evaluated exactly instead of the original's shortcuts: every motion as a continuous function of the fractional
// tick (the rise, ring height and wobble are sampled once per drawn frame in the original, the spin and the flights
// stepped per tick: here both are evaluated at t, with exact sin / cos instead of the 1023-amplitude table); the fades
// as colour multiplies instead of 6-bit integer products; the reflection rendered at 1024 x 1024 with the same
// mapping (map texel = pixel of the 320x200 projection, x < 256, y < 255) and read filtered; the band's edges
// anti-aliased; the ring's env index (b >> 1) + 0x60 as 0x60 + 31 b / 63, so that the filtered values 0..63 of file
// 29 stay on the red ramp 0x60..0x7f (b / 2 would reach 0x7f.5, half into the unused colour 0x80). Kept: the
// frame-rate dependent end of the rise in the capture (the last frame before tick 160 had tick 158, so the torso
// stays 22.9 units low and its spin starts from angle 43), the ring's jump when the wobble starts (its x / z offset
// switches from 0 to the circle), the torso's jump 100 units back when the mirror starts, the torso drawn over the
// mirror whatever their depth.
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"
#include "stars/engine.h"

using namespace stars;

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr int kRingFirst = 177;      // vertices 177..241 and faces 329..458 of file 46: the ring (0x16f46)
constexpr int kRingFace = 329;
// 0x17084's phases (PIT ticks since the part started)
constexpr double kRise = 64, kRiseLast = 158, kSpin = 160, kSlide = 304, kWobble = 496, kMirror1 = 736,
                 kMirror2 = 1600, kEnd = 2447;

double sinT(double i) { return 1023 * std::sin(kTwoPi * i / 4096); }   // COS[(i - 0x400) & 0xfff], continuous
double cosT(double i) { return 1023 * std::cos(kTwoPi * i / 4096); }   // COS[i & 0xfff]

struct State {
    Pose torso, mirror;   // positions in 1/1024 units (direct draws), angles
    double ringx = 0, ringy = 0, ringz = 0;
    bool mirrorOn = false;
    double fade = 1;
};

struct Torso : Scene {
    Object3D torso, mirror;
    std::vector<EngineVertex> ringRef;
    LightRamp ramp;
    BumpMap bump;
    EnvTexture ringEnv;
    Palette p42;
    PaletteTexture pal;
    EngineTarget reflection;
    Pose torso0, mirror0;   // the object records 0x16cc8 and 0x16ca8 as the image holds them

    static Pose record(const StarsExe& x, uint32_t a) {
        return {double(x.s32(a)), double(x.s32(a + 4)), double(x.s32(a + 8)),
                double(x.s16(a + 0xc)), double(x.s16(a + 0xe)), double(x.s16(a + 0x10))};
    }

    void init(Ctx& c) override {
        const StarsExe& x = *c.exe;
        torso.load(x, 46);
        mirror.load(x, 45);
        for (size_t i = kRingFace; i < torso.faces.size(); i++) torso.faces[i].type = 1;   // 0x16f46: the ring
        torso.touch();
        ringRef.assign(torso.verts.begin() + kRingFirst, torso.verts.end());   // the reference copy 0x16d08
        ramp.load(x, 0x16c20);
        bump.create(x.file(43), -4, 8, true);           // ((b - 4) << 3), read in pairs by modes 20 / 21
        ringEnv.makeLevel(x.fileSpan(29).first(65536)); // (b >> 1) + 0x60: the red ramp
        p42 = vgaPalette(x.fileSpan(42));
        pal.set(p42);
        reflection.create(1024);
        torso0 = record(x, 0x16cc8);
        mirror0 = record(x, 0x16ca8);
    }

    // 0x17084 (per frame) and 0x17513 (per tick), as functions of the tick n
    State stateAt(double n) const {
        State s;
        Pose& tp = s.torso;
        tp = torso0;
        tp.x = 0;
        tp.y = 0x113000;
        tp.z = -0x23f000;
        // the rise: half a turn and 1100 units up over 96 ticks, updated per frame (the capture's last at tick 158)
        if (n >= kRise) {
            double r = (std::min(n, kRiseLast) - kRise) / 96;
            tp.b = 0x800 - 0x800 * r;
            tp.y = 0x113000 - 0x113000 * r;
        }
        if (n >= kSpin) tp.b -= 12 * (n - kSpin);        // 0x17513: spin from the first frame with t >= 160
        // the ring: rises from y = 200 (x 16 units), then slides up and down
        s.ringy = n < kSpin ? 200 : n < kSlide ? 200 - 200 * (n - kSpin) / 144 : sinT((n - kSlide) * 16) / 32;
        if (n >= kWobble) {                               // 0x17253: the torso tilts, the ring circles about it
            double k = n - kWobble;
            tp.a = sinT(k * 64) / 4;
            s.ringx = sinT(k * 128) / 8;
            s.ringz = cosT(k * 128) / 8;
        }
        // the mirror: flies in (phase 1), then drifts right while the torso drifts left (phase 2)
        s.mirrorOn = n >= kMirror1;
        double m1 = std::clamp(n - kMirror1, 0.0, kMirror2 - kMirror1), m2 = std::max(0.0, n - kMirror2);
        Pose& mp = s.mirror;
        mp = mirror0;
        mp.a += m1 + m2;
        mp.b -= 7 * m1 + 8 * m2;
        mp.c += 3 * m1 + 4 * m2;
        mp.x = std::min(mirror0.x + 0xda2 * m1, double(-0x4b000)) + 0x2ba * m2;
        mp.z = std::min(mirror0.z + 0x1e4d * m1, double(-0x271000));
        tp.x = std::min(0x307 * m1, double(0x64000)) - 0x5c1 * m2;
        if (s.mirrorOn) tp.z = -0x258000;                 // 0x173a1 moves the torso back
        // fades: in P42 t >> 6, out P42 f >> 7 (f = 2447 - t < 127)
        s.fade = std::clamp(std::min(n / 64, (kEnd - n) / 128), 0.0, 1.0);
        return s;
    }

    static Pose camSpace(const Pose& p) { return {p.x / 1024, p.y / 1024, p.z / 1024, p.a, p.b, p.c}; }

    void render(Ctx& c, double t) override {
        double n = std::clamp(c.ticks(t), 0.0, kEnd);
        State s = stateAt(n);
        // 0x17342: the ring's vertices = the reference + (ringx, ringy << 4, ringz)
        for (size_t i = 0; i < ringRef.size(); i++) {
            EngineVertex& v = torso.verts[kRingFirst + i];
            v.x = float(ringRef[i].x + s.ringx);
            v.y = float(ringRef[i].y + s.ringy * 16);
            v.z = float(ringRef[i].z + s.ringz);
        }
        torso.touch();
        Vec3 mul{float(s.fade), float(s.fade), float(s.fade)};
        const Picture& bg = c.picture(47, {.palette = &p42, .indexAdd = 0xe6});   // file 47 at 0xe6..0xed
        c.drawPicture(bg, {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = mul});

        Camera cam;
        cam.F = 300;     // [0x57ab6], set by 0x16d86
        cam.shift = 1;
        // modes 20 / 21: the torso through the light map (0x20 + L >> 1) moved by the bump pairs, the ring (type 1)
        // through file 29 and the red ramp
        Shading sh;
        sh.mode = 20;
        sh.palette = &pal;
        sh.ramp = &ramp;
        sh.levelScale = 0.5f;
        sh.levelMin = 0;
        sh.levelMax = 31;
        sh.base = 0x20;
        sh.N = 1;
        sh.bump[0] = &bump;
        sh.env = ringEnv.level(0x60, 31.0f / 63.0f);
        if (s.ringy > -100 && s.ringy < 100) {   // 0x17459: light-map rows 0x88 + ringy .. + 15 add 0x20 (blue ramp)
            sh.bandV0 = float(0x88 + s.ringy);
            sh.bandV1 = sh.bandV0 + 16;
            sh.bandAdd = 0x20;
        }
        Engine& eng = c.shared<Engine>();
        Pose tp = camSpace(s.torso);
        if (s.mirrorOn) {
            // 0x173a1: file 44, then the torso and ring mirrored about the y axis in mode 21 into the 256x256 map
            reflection.fill(c, c.picture(44, {.palette = &p42}));
            Shading s21 = sh;
            s21.mode = 21;
            Pose inst{s.torso.x / 2 / 1024, tp.y, tp.z, tp.a, 0x800 - tp.b, tp.c};
            eng.begin(c, reflection, cam);
            eng.draw(torso, inst, s21);
            eng.end();
        }
        sh.mul = mul;
        eng.begin(c, cam);
        if (s.mirrorOn) {
            Shading s2;   // mode 2: env = the reflection map, the doubled range (n >> 11), wrapping
            s2.mode = 2;
            s2.env = reflection.env();
            s2.mul = mul;
            eng.draw(mirror, camSpace(s.mirror), s2);
            eng.layer();  // the torso and ring are drawn over the mirror
        }
        eng.draw(torso, tp, sh);
        eng.end();
    }
};
REGISTER_SCENE(Torso, "torso");

}  // namespace
