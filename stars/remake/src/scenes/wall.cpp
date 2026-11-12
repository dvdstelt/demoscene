// wall: the textured surface, 45:02-54:01 (stars/re/notes-parts3d-b.md section 3). A 23 x 23 grid (file 24) turns
// and sways in front of the blue-violet clouds (file 25); its vertices morph between a flat wall and eight face
// reliefs; it is textured with the stone frieze (map 21) or the Celtic knots (map 23) and lit by a light spot fixed
// to the view, embossed by the texture itself. Ports 0xa14d (set-up: object, maps, palettes, light map 0x7873a with
// DL 4, F 100, shift 3), 0xa0ca (the loop), 0xa4ac (the PIT callback: tick, crossfade triangle, morph step),
// 0xa656 (six motion keys of 338 ticks), 0xa3c9 (the phases: fade in, crossfade, fade out), 0xa530 / 0xa5c5 /
// 0xa57d (palette crossfade P22 -> P20 and the eight texture frames of the top-left 64 x 128, tiled 4 x 2).
// The engine (src/stars/engine.h) draws the grid in mode 15: index = 8 L + texel, the light lookup moved by the
// texel and its right neighbour.
//
// Evaluated exactly instead of the original's shortcuts: the motion keys and the shape keys at the fractional tick
// (one time for both: the original's shape is one frame behind the motion because the loop morphs after drawing);
// the fades and the palette crossfade as continuous colour blends instead of 6-bit integer products; the texture
// crossfade as a continuous blend of the two grey-level maps through the original's eight frames (frame k = p >> 3
// has weight k / 8, frame 7 weight 1; here the weight runs linearly between the frames' centres), looked up in the
// colour table afterwards. Kept: the frame-rate dependent values of the capture (the fade-in ends at 63/64 and the
// palette stays there until the crossfade; the triangle starts at tick 1080, the first frame with t >= 1079), the
// palette and texture following p during the fade out with the stone palette.
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"
#include "stars/engine.h"

using namespace stars;

namespace {

constexpr int kMotionKeys = 6, kMotionTicks = 338;  // 0xa4ac / 0xa656: key every 338 ticks, six keys
constexpr int kShapeKeys = 33;                      // 0xa4ac: morph_step(33 keys); 63 ticks per key (record 0x9fa0)
constexpr double kCrossStart = 1079;                // 0xa3c9: t >= 0x437 starts the crossfade
constexpr double kTriangleStart = 1080;             // the first frame with t >= 1079 in the capture sets dir = -1
constexpr double kFadeOut = 2094, kEnd = 2157;      // 0xa3c9: fade out 63 -> 0 from 0x82e, ends at b = 0

// 0xa082: key k of the motion table (x, y, z, a, b, c as int16)
Pose motionKey(const StarsExe& exe, int k) {
    uint32_t a = 0xa082 + 12 * uint32_t(k);
    return {double(exe.s16(a)),     double(exe.s16(a + 2)), double(exe.s16(a + 4)),
            double(exe.s16(a + 6)), double(exe.s16(a + 8)), double(exe.s16(a + 10))};
}

// 0xa57d: the top-left 64 x 128 of a map tiled 4 x 2 into 256 x 256 (0xa5c5 reads only that corner)
std::vector<uint8_t> tiled(const std::vector<uint8_t>& m) {
    std::vector<uint8_t> o(65536);
    for (size_t y = 0; y < 256; y++)
        for (size_t x = 0; x < 256; x++) o[y * 256 + x] = m[(y & 127) * 256 + (x & 63)];
    return o;
}

// [0xa076]: 63 until the triangle starts, then 63 -> 0 -> 63 every 126 ticks
double crossLevel(double n) {
    if (n <= kTriangleStart) return 63;
    return std::abs(63 - std::fmod(n - kTriangleStart, 126.0));
}

// 0xa530: texture frame F[p >> 3] (F[k] = A + (B - A) k / 8 for k < 7, F[7] = B) as a continuous weight of B: linear
// between the frames' centres p = 8k + 4
double textureWeight(double p) {
    double k = p / 8 - 0.5;
    if (k <= 6) return std::max(k, 0.0) / 8;
    return 0.75 + 0.25 * std::min(k - 6, 1.0);
}

struct Wall : Scene {
    Object3D grid;
    Morph morph;
    LightRamp ramp;
    IndexMap texKnots, texFrieze;      // maps 23 (F[0]) and 21 (F[7])
    BumpMap bumpKnots, bumpFrieze;     // the same maps as bump tables (the bump selector is the texture)
    Palette p20, p22;
    PaletteTexture pal;
    Pose keys[kMotionKeys];

    void init(Ctx& c) override {
        const StarsExe& x = *c.exe;
        grid.load(x, 24);
        morph.load(x, 0x9fa0, 0x9e80, kShapeKeys);   // shift 6, 63 ticks per key, linear weights 0..127
        ramp.load(x, 0x9df0);
        std::vector<uint8_t> a = tiled(x.file(23)), b = tiled(x.file(21));
        texKnots.create(a);
        texFrieze.create(b);
        bumpKnots.create(a, 0, 1);    // light[(ev + tex[u + 1]) & 255][(eu + tex[u]) & 255]
        bumpFrieze.create(b, 0, 1);
        p20 = vgaPalette(x.fileSpan(20));
        p22 = vgaPalette(x.fileSpan(22));
        pal.set(p20);
        for (int k = 0; k < kMotionKeys; k++) keys[k] = motionKey(x, k);
    }

    // 0xa656: linear between the keys, by tick
    Pose motion(double n) const {
        double kf = n / kMotionTicks, f = kf - std::floor(kf);
        int k = int(std::floor(kf)) % kMotionKeys;
        const Pose &p = keys[k], &q = keys[(k + 1) % kMotionKeys];
        return {p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, p.z + (q.z - p.z) * f,
                p.a + (q.a - p.a) * f, p.b + (q.b - p.b) * f, p.c + (q.c - p.c) * f};
    }

    void render(Ctx& c, double t) override {
        double n = std::clamp(c.ticks(t), 0.0, kEnd);
        double p = crossLevel(n);
        // 0xa3c9: the palette uploaded per phase (block 0, the clouds' colours, is the same in P20 and P22)
        double fade = 1;
        Palette cur = p20;
        if (n < kCrossStart) {
            fade = std::min(n, 63.0) / 64;                      // P20 t >> 6, then held at 63/64
        } else if (n < kFadeOut) {
            for (size_t i = 0; i < 256; i++) cur[i] = p22[i] + (p20[i] - p22[i]) * float(p / 64);
        } else {
            fade = std::max(0.0, 63 - (n - kFadeOut)) / 64;     // P20 b >> 6
        }
        pal.set(cur);
        Vec3 mul{float(fade), float(fade), float(fade)};
        c.drawPicture(c.picture(25, {.palette = &p20}), {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = mul});

        grid.setShape(morph.at(n));
        Camera cam;
        cam.F = 100;      // [0x57ab6], set by 0xa14d
        cam.shift = 3;    // [0x5594c]: focal length 128
        Shading sh;
        sh.mode = 15;
        sh.palette = &pal;
        sh.ramp = &ramp;          // 0x7873a DL 4: level = L >> 1 (0..31), index = 8 level + texel
        sh.levelScale = 0.5f;
        sh.levelMin = 0;
        sh.levelMax = 31;
        sh.base = 0;
        sh.N = 8;
        // the frieze (F[7]) until the crossfade, then F[p >> 3]
        float w = float(n < kCrossStart ? 1.0 : textureWeight(p));
        sh.tex[0] = &texKnots;
        sh.tex[1] = &texFrieze;
        sh.texMix = w;
        sh.bump[0] = &bumpKnots;
        sh.bump[1] = &bumpFrieze;
        sh.bumpMix = w;
        sh.mul = mul;
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam);
        eng.drawScene({{&grid, motion(n)}}, sh);   // 0x79483 with the one object; camera at 0
        eng.end();
    }
};
REGISTER_SCENE(Wall, "wall");

}  // namespace
