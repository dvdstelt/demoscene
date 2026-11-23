// The credits (5:63..11:63, inside/re/notes-credits.md): a tunnel of 11 rings built every frame along a wavy flight
// path (credits/tuubi.sro reshaped, textured with credits/tunnel.raw through credits/tunnel.shd), the camera flying
// through it and rolling, 12 additive smoke puffs (credits/sauhu.raw through credits/smoke.shd), from 9:48 a spiked
// ball (credits/piikkis.sro, environment mapped) that closes in, before 9:48 the water mask over the whole frame
// (credits/wtrmask.raw through credits/wtrmask.int, a signed additive level map), and from 7:00 to 9:32 the five name
// pictures (credits/kredu*.raw through credits/tunnel.int, additive level maps) at the bottom left. Ports the init
// 0x406d10 (the flight path 0x40684c, the smoke 0x406c70), the tick 0x405f30, the frame 0x406794 (the camera and the
// ball 0x4061fc, the tunnel 0x406470, the seam 0x4063fc) and the script's events 0x405f1c (the next name) and
// 0x405f10 (9:48: the ball, the mask off). The flashes and the fade are the framework's.
//
// The state the tick routine steps (the distance flown, the roll, the smoothed camera and ball positions, the ball's
// lead and spin) is replayed per refresh from the part's start (the path is a table: the recurrences have no closed
// form) and blended between ticks by the fraction of the refresh; nothing in the part depends on the frame rate (the
// tunnel, the smoke and the ball's position are computed per drawn frame from the tick state). The path is the
// original's, from rand() replayed from the program's start (seed 1, the intro's 260 calls first).
//
// Evaluated exactly instead of the original's shortcuts (notes section 8): the distance flown, the ring index and
// the camera, ball and vertex positions are continuous (floats instead of truncated ints), the ring v values k x 409.6
// instead of (k x 4096) / 10, the seam's u 4096 (one texture width) instead of 4101, the distance fade without
// truncation, the spin a continuous rotation (S^n about S's axis by n times its angle); the engine (inside/engine.h)
// draws the tunnel perspective-correct with filtered texels and a continuous light level, anti-aliased, and the smoke
// as filtered squares; the mask and the names are filtered level maps through the fits of their blend tables. Kept:
// the transposed look-at (the view along the look-at matrix's third column, not towards the target) and the target's
// x mixed 0.8 / 0.2 with the camera's, the camera-facing 12-gons, the distance fade beyond 7 rings and the minimum
// light 112, the smoke's 1.2x vertical offset (the engine's billboards), gap 9's backwards texture at its closing quad,
// the ball's trailing from the part's start, SANTTU never shown.
//
// The mask's table darkens (a negative offset) and some rows of both tables scale a channel by more than 1, which a
// dual-source blend into the 16-bit normalised frame cannot write: the mask and the names are drawn here (kLevelFS),
// not with the engine's bitmaps.
//
// The redraws: the names' star and letters may be redrawn (art/credits_<name>.png: grey 255 x level / 31, the face
// area filled with the star or black). With the art on, the redraw is the level map and the original's face pixels
// (the scanned photos: always the original's, filtered) are drawn over it: the pixels inside our measured face shapes
// (tools/align_art.py FACES) that are not at the star's level.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"
#include "stb_image.h"

namespace {

namespace sr = inside::sr;

// ------------------------------------------------------------------------------------------------ addresses
// File names (the strings the init 0x406d10 passes to 0x400614).
constexpr uint32_t kPalName = 0x4210e8, kTexName = 0x4210fc, kShdName = 0x421110, kSmokeShdName = 0x421124,
                   kSmokeName = 0x421138, kNameIntName = 0x42114c, kBallTexName = 0x421160,
                   kMaskName = 0x4211ec, kMaskIntName = 0x421200, kTunnelSroName = 0x421214,
                   kBallSroName = 0x421228;
// 0x410618(scene, W = edx, H = ebx, x0 = ecx (0), y0, x1, y1 pushed); fov and near (mov [scene + d], imm32)
constexpr uint32_t kViewY1 = 0x406f37, kViewX1 = 0x406f3c, kViewH = 0x406f43, kViewY0 = 0x406f48,
                   kViewW = 0x406f4a, kFov = 0x406f6c, kNear = 0x406f7c;
// 0x410bb0 scale (mov ebx), 0x410850 rasterizer (mov edx), the flags (or [obj + 0xc4 / 0xc5], imm)
constexpr uint32_t kTunnelScale = 0x406fba, kTunnelRaster = 0x406fda, kTunnelFlags = 0x407066;
constexpr uint32_t kBallScale = 0x406ffe, kBallRaster = 0x40701e, kBallFlags = 0x407032;
// the materials' u / v masks (texture sizes - 1)
constexpr uint32_t kTunnelUMask = 0x4070ce, kTunnelVMask = 0x4070da, kBallUMask = 0x407129, kBallVMask = 0x407135;
// 0x406c70: the smoke sprites (mov [ebx + d], imm32): draw routine, width, height, position scale, size, depth bias
constexpr uint32_t kSmokeDraw = 0x406c91, kSmokeW = 0x406cb5, kSmokeH = 0x406cbf, kSmokeScale = 0x406cc9,
                   kSmokeSize = 0x406cd3, kSmokeBias = 0x406cdd;
constexpr uint32_t kSmokeBlended = 0x415dc8;   // the billboards' blended draw routine
// The name bitmaps 0x415e50 (width = ebx, height = ecx) by name index ([0x4263c0 + 4 i]); the file names.
struct NameSpec {
    uint32_t file, w, h;
    const char* art;   // our name of the redraw (art/<name>.png)
};
constexpr NameSpec kNames[] = {
    {0x421174, 0x407197, 0x40718b, "credits_destop"},        // kredu1
    {0x4211c4, 0x40721c, 0x407217, "credits_wili"},          // kredu7
    {0x421188, 0x4071bc, 0x4071b7, "credits_jouni"},         // kredu2
    {0x4211d8, 0x40723c, 0x407237, "credits_juliet"},        // kredu8
    {0x42119c, 0x4071dc, 0x4071d7, "credits_yolk_legend"},   // kredu3
};
constexpr uint32_t kMaskW = 0x40725c, kMaskH = 0x407257, kMaskType = 0x407277, kNameType = 0x40728d;
// 0x406794: the names drawn (cmp [name], imm8), positions (push y, push x as floats)
constexpr uint32_t kNameCount = 0x406813, kNameY = 0x40682a, kNameX = 0x406836, kMaskY = 0x406805, kMaskX = 0x40680c;
// 0x405f30 (tick): doubles and the spin (six pushes for 0x4145f4: c hi, c lo, b hi, b lo, a hi, a lo)
constexpr uint32_t kRoll = 0x420f38, kSpeed = 0x420f40, kCamNew = 0x420f48, kCamOld = 0x420f50,
                   kLeadStep = 0x420f58, kBallNew = 0x420f60, kBallOld = 0x420f68;
constexpr uint32_t kSpinPush[6] = {0x40604f, 0x406060, 0x40606d, 0x40607e, 0x40608f, 0x4060a0};
constexpr uint32_t kBallLead = 0x40610c;   // add eax, imm8 (rings ahead of the camera's point, plus trunc(s))
constexpr uint32_t kRoll0 = 0x42284c, kLead0 = 0x422850;   // floats (data): the roll and the ball's lead at the start
// 0x4061fc (camera): the target's lead (add edi, imm8) and its x mix (doubles)
constexpr uint32_t kTargetLead = 0x4062c9, kTargetCam = 0x420f70, kTargetPath = 0x420f78;
// 0x406470 (tunnel): u = trunc(j x 0x100 x 16.0 x (1 / 12)), the fade distance (imul, D, imm8), the minimum light
// (cmp edx, imm8), the smoke's offset (mov edi, imm32); 0x406d10: v = (k x 0x1000) / 10 (add ebx, imm32; mov esi)
constexpr uint32_t kUStep = 0x4066bc, kU16 = 0x420f80, kUTwelfth = 0x420f88, kFadeRings = 0x406677,
                   kMinLight = 0x4066a1, kSmokeOffset = 0x4066f9, kVStep = 0x4072d1, kVDiv = 0x4072bc;
constexpr uint32_t kSeamU = 0x406428;   // 0x4063fc: u = 0x1000 + 0x1000 / D (mov eax, imm32)
// 0x40684c (path): the counters' start values (mov r32, imm32), the moduli of rand(), the doubles (below)
constexpr uint32_t kPathA = 0x406866, kPathB = 0x40686b, kPathC = 0x406870, kPathD = 0x40688d, kPathH = 0x406892,
                   kPathE = 0x4068a7, kPathG = 0x4068b0, kRand1 = 0x406a23, kRand2 = 0x406a8a, kMaxLight = 0x4210d4;
// script routines
constexpr uint32_t kNameEvent = 0x405f1c, kBallEvent = 0x405f10;

// Loop counts of the routines (code constants): path entries, vertices per ring, rings, smoke puffs.
constexpr int kEntries = 1024, kRingVerts = 12, kRings = 11, kSmokes = 12;
// rand() calls before the path (the intro's init: 130 drops at (rand() % 320, rand() % 200)); notes 3.1.
constexpr int kRandBefore = 260;

// The face shapes of the name pictures in original pixels (our measurements, tools/align_art.py FACES): ellipses
// (centre x, y, radius x, y), boxes (x0, y0, x1, y1); a face pixel is inside one and not at the star's level.
struct FaceShapes {
    std::vector<std::array<float, 4>> ellipses, boxes;
};
const FaceShapes kFaces[] = {
    {{{50, 35, 36, 30}}, {{78, 4, 93, 38}}},
    {{{77, 37, 25, 28}}, {}},
    {{{50, 38, 23, 27}}, {}},
    {{{51, 36, 27, 25}}, {}},
    {{{49, 38, 34, 28}, {266, 38, 28, 30}}, {}},
};
constexpr int kStarLevel = 10;

// ------------------------------------------------------------------------------------------------ the exe's code
uint32_t pushImm(const InsideExe& x, uint32_t a) {
    if (x.u8(a) == 0x68) return x.u32(a + 1);
    if (x.u8(a) == 0x6a) return uint32_t(int32_t(x.s8(a + 1)));
    throw std::runtime_error("credits: no push at " + std::to_string(a));
}
uint32_t movRegImm(const InsideExe& x, uint32_t a) {   // mov r32, imm32
    if ((x.u8(a) & 0xf8) != 0xb8) throw std::runtime_error("credits: no mov r32, imm32 at " + std::to_string(a));
    return x.u32(a + 1);
}
int32_t movMemImm(const InsideExe& x, uint32_t a) {    // mov dword [r + disp8 / disp32 (/ esp + disp8)], imm32
    if (x.u8(a) != 0xc7) throw std::runtime_error("credits: no mov [r + d], imm32 at " + std::to_string(a));
    int mod = x.u8(a + 1) >> 6, rm = x.u8(a + 1) & 7;
    uint32_t o = 2 + (rm == 4 ? 1 : 0) + (mod == 1 ? 1 : mod == 2 ? 4 : 0);
    return x.s32(a + o);
}
int32_t imm8At(const InsideExe& x, uint32_t a, uint32_t offset) { return x.s8(a + offset); }
float asFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
double pushedDouble(const InsideExe& x, uint32_t hi, uint32_t lo) {
    uint64_t bits = uint64_t(pushImm(x, hi)) << 32 | pushImm(x, lo);
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}
// Six pushes: c, b, a of 0x4145f4 as doubles (high dword first).
sr::V3 pushedEuler(const InsideExe& x, const uint32_t (&at)[6]) {
    return {pushedDouble(x, at[4], at[5]), pushedDouble(x, at[2], at[3]), pushedDouble(x, at[0], at[1])};
}

// Watcom's rand() 0x4134cd.
struct Rand {
    uint32_t seed = 1;
    int operator()() {
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }
};

// ------------------------------------------------------------------------------------------------ the flight path
struct PathEntry {
    int32_t p[3];
    int32_t light[kRingVerts];
    float radius[kRingVerts];
};

// 0x40684c, as the original computes it (x87 at 53-bit precision: doubles; float stores): ring centres D apart along
// (P, Q, D) normalised, 12 lights and radius factors per entry from rand().
std::vector<PathEntry> buildPath(const InsideExe& x, int D) {
    auto K = [&](uint32_t a) { return x.f64(a); };
    Rand rng;
    for (int k = 0; k < kRandBefore; k++) rng();
    const int a0 = int32_t(movRegImm(x, kPathA)), b0 = int32_t(movRegImm(x, kPathB)),
              c0 = int32_t(movRegImm(x, kPathC)), d0 = int32_t(movRegImm(x, kPathD)),
              e0 = int32_t(movRegImm(x, kPathE)), g0 = int32_t(movRegImm(x, kPathG)),
              h0 = int32_t(movRegImm(x, kPathH));
    const int mod1 = int32_t(movRegImm(x, kRand1)), mod2 = movMemImm(x, kRand2);
    const float maxLight = x.f32(kMaxLight);
    std::vector<PathEntry> path(kEntries);
    float X = 0, Y = 0, Z = 0;
    for (int i = 0; i < kEntries; i++) {
        const double di = i, a = a0 + i, b = b0 + i, c = c0 + i, d = d0 + i, e = e0 + i, g = g0 + i, h = h0 + i;
        // P = |5530 cos(g / 7.1)| + |1500 sin(i / 10.3)| - 1650 cos(c / 14.4) + 2757 cos((i + 20.4) / 16.5)
        //     + |3130 sin(b / 14.3)|
        // Q = |1250 sin(a / 6.3)| - |4240 cos(g / 10.1)| + 2363 cos(e / 28.4) - 2653 cos(i / 14.5)
        //     - sin(d / 21.3) 1100 x 1.2   (in the code's order of operations)
        double A = std::fabs(std::cos(g * K(0x420fa4)) * K(0x420fac));
        double B = std::fabs(std::sin(a * K(0x420ff4)) * K(0x420ffc));
        double C = std::fabs(std::cos(g * K(0x421004)) * K(0x42100c));
        double E = std::fabs(std::sin(di * K(0x420fb4)) * K(0x420fbc));
        double Q = (B - C) + std::cos(e * K(0x421014)) * K(0x42101c);
        double P = (A + E) - std::cos(c * K(0x420fc4)) * K(0x420fcc);
        Q = Q - std::cos(di * K(0x421024)) * K(0x42102c);
        P = P + std::cos((di + K(0x4210c4)) * K(0x420fd4)) * K(0x420fdc);
        Q = Q - std::sin(d * K(0x421034)) * K(0x42103c) * K(0x421044);
        P = P + std::fabs(std::sin(b * K(0x420fe4)) * K(0x420fec));
        double v[3] = {P, Q, double(D)};                                          // 0x413510
        double k = double(D) / std::sqrt((v[1] * v[1] + v[0] * v[0]) + v[2] * v[2]);   // 0x413750: length D
        X = float(double(X) + v[0] * k);
        Y = float(double(Y) + v[1] * k);
        Z = float(double(Z) + v[2] * k);
        PathEntry& pe = path[size_t(i)];
        const int r1 = rng() % mod1;
        const float base = float(r1 * K(0x42104c) + K(0x4210cc));                // 0.65 + r1 / 1200
        for (int j = 0; j < kRingVerts; j++) {
            const double dj = j, k1 = i + j, k2 = i - j;
            double rr = std::fabs(std::sin(dj * K(0x421054)) * K(0x42105c)) + double(base);
            const int r2 = rng() % mod2;
            pe.radius[j] = float(r2 * K(0x421064) + rr);                           // + r2 / 1000
            // t = |s3 / 3 + sin(k1 / 2.23)| + 0.4 sin((i + 0.8 j) / 3.05), s3 = 0.5 cos(k2 / 17.1) + 0.5 sin(i / 10.3)
            //     + 0.8 sin(h / 13.1) + 0.2 cos(h / 41.3); light = clamp(t x 64 x 16, 0, 1008)
            double s3 = std::cos(k2 * K(0x42106c)) * K(0x421074) + std::sin(di * K(0x420fb4)) * K(0x421074);
            s3 = s3 + std::sin(h * K(0x42107c)) * K(0x421084);
            s3 = s3 + std::cos(h * K(0x42108c)) * K(0x421094);
            double t = std::fabs(s3 * K(0x42109c) + std::sin(k1 * K(0x4210a4)));
            t = t + std::sin((di + dj * K(0x421084)) * K(0x4210ac)) * K(0x42105c);
            float L = float(t * K(0x4210b4) * K(0x4210bc));
            if (0 > L) L = 0;
            if (L > maxLight) L = maxLight;
            pe.light[j] = int32_t(L);
        }
        pe.p[0] = int32_t(X);
        pe.p[1] = int32_t(Y);
        pe.p[2] = int32_t(Z);
    }
    return path;
}

// ------------------------------------------------------------------------------------------------ shaders
// The mask and the names: level maps through the fits of their blend tables (out = b x background + s per channel,
// Renderer::fit), filtered (the four texels around the point, each through its row; a redraw's continuous level
// through the two rows around it, the original's face texels over it). The frame is 16-bit normalised, and a blend
// cannot write a negative offset (the mask darkens) or a factor above 1 (both tables have rows with one): the shader
// reads the background from a copy of the area it covers and writes the clamped result.
const char* kLevelVS = R"(
uniform vec4 rect;   // x, y, w, h in VGA coordinates
uniform vec2 origin; // the bitmap's top-left corner
out vec2 vP;         // the bitmap's pixel coordinates
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vec2 p = rect.xy + c * rect.zw;
    vP = p - origin;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";

const char* kLevelFS = R"(
in vec2 vP;
uniform usampler2D lev;    // the original's levels 0..31; bit 7: a face pixel (drawn over the redraw)
uniform sampler2D art;     // the redraw: grey = level / 31
uniform sampler2D fits;    // 32 x 2n: row 2t the factor b, row 2t + 1 the offset s of table t's rows 0..31
uniform int tab;           // the table
uniform int useArt;        // 1: the redraw's level outside the faces
uniform int edge;          // taps outside the bitmap: 0 level 0 (transparent), 1 the nearest texel
uniform int filt;          // 0: the texel under the point (the original)
uniform sampler2D bg;      // a copy of the frame where the bitmap is drawn
uniform vec2 size;         // the bitmap's size in pixels
out vec4 o;

vec3 rowB(int l) { return texelFetch(fits, ivec2(l, 2 * tab), 0).rgb; }
vec3 rowS(int l) { return texelFetch(fits, ivec2(l, 2 * tab + 1), 0).rgb; }

void main() {
    ivec2 sz = ivec2(size + 0.5);
    vec3 sA = vec3(0.0), bA = vec3(1.0);
    float g = texture(art, vP / size).r;   // outside any branch: the mipmap level needs the neighbours' coordinates
    if (useArt != 0) {   // the redraw's continuous level: the two table rows around it blended
        bool inside = all(greaterThanEqual(vP, vec2(0.0))) && all(lessThan(vP, size));
        float L = inside ? clamp(g * 31.0, 0.0, 31.0) : 0.0;
        int r0 = min(int(L), 30);
        float lf = L - float(r0);
        sA = mix(rowS(r0), rowS(r0 + 1), lf);
        bA = mix(rowB(r0), rowB(r0 + 1), lf);
    }
    // the four texels around the point (texel n covers [n, n + 1)), or the one under it
    vec2 q = filt != 0 ? vP - 0.5 : floor(vP);
    ivec2 i0 = ivec2(floor(q));
    vec2 f = q - vec2(i0);
    vec3 S = vec3(0.0), B = vec3(0.0);
    for (int k = 0; k < 4; k++) {
        ivec2 t = i0 + ivec2(k & 1, k >> 1);
        float w = ((k & 1) != 0 ? f.x : 1.0 - f.x) * ((k >> 1) != 0 ? f.y : 1.0 - f.y);
        if (w <= 0.0) continue;
        bool out_ = any(lessThan(t, ivec2(0))) || any(greaterThanEqual(t, sz));
        uint v = (!out_ || edge != 0) ? texelFetch(lev, clamp(t, ivec2(0), sz - 1), 0).r : 0u;
        if (useArt != 0 && (v & 128u) == 0u) {
            S += w * sA;
            B += w * bA;
        } else {
            int l = int(v & 31u);
            S += w * rowS(l);
            B += w * rowB(l);
        }
    }
    vec3 c = texelFetch(bg, ivec2(gl_FragCoord.xy), 0).rgb;
    o = vec4(clamp(B * c + S, 0.0, 1.0), 1.0);
}
)";

// ------------------------------------------------------------------------------------------------ the part
// The state the tick routine 0x405f30 steps (after k ticks).
struct Tick {
    sr::V3 C, S;          // the smoothed camera and ball positions [0x426390], [0x426320]
    float theta = 0;      // the roll [0x42284c]
    float s = 0;          // the ball's lead [0x422850]
    sr::M3 spin;          // the ball's orientation [0x4262d8]
};

// A level map drawn through a blend table (0x415df0 with a bitmap of type 2, or 4 with key 0).
struct LevelMap {
    sr::Bitmap bmp;        // the original's (the engine's bitmap: INSIDE_ENGINE=original draws it as the original)
    int w = 0, h = 0;
    int fit = 0;           // its table's fits (kNameFit, kMaskFit)
    bool key = false;      // level 0 is transparent
    Texture levels;        // the original's levels; bit 7: a face pixel (names)
    Texture art;           // the redraw's level map (id 0: none)
};
constexpr int kNameFit = 0, kMaskFit = 1, kLevels = 32;

class CreditsScene : public Scene {
public:
    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        eng_.init(c);
        readConstants(x);
        pal_ = eng_.palette(inside::palette(d, x.cstr(kPalName)));

        // 0x406d10: the scene, the camera, the tunnel and the ball, the materials
        scene_.width = int(movRegImm(x, kViewW));
        scene_.height = int(movRegImm(x, kViewH));
        scene_.x0 = 0;   // ecx = 0 (xor ecx, ecx)
        scene_.y0 = int(pushImm(x, kViewY0));
        scene_.x1 = int(pushImm(x, kViewX1));
        scene_.y1 = int(pushImm(x, kViewY1));
        scene_.fov = movMemImm(x, kFov);
        scene_.near = movMemImm(x, kNear);
        scene_.add(camera_);
        scene_.camera = &camera_;

        int tunTex = eng_.texture(d.file(x.cstr(kTexName)), movMemImm(x, kTunnelUMask) + 1,
                                  movMemImm(x, kTunnelVMask) + 1);
        int tunMat = eng_.material({.texture = tunTex, .table1 = eng_.table(d.file(x.cstr(kShdName)))});
        tunnel_.loadSro(d.file(x.cstr(kTunnelSroName)), int32_t(movRegImm(x, kTunnelScale)), tunMat);
        tunnel_.setRaster(sr::rasterAt(movRegImm(x, kTunnelRaster)));
        scene_.add(tunnel_);
        int ballTex = eng_.texture(d.file(x.cstr(kBallTexName)), movMemImm(x, kBallUMask) + 1,
                                   movMemImm(x, kBallVMask) + 1);
        int ballMat = eng_.material({.texture = ballTex});
        ball_.loadSro(d.file(x.cstr(kBallSroName)), int32_t(movRegImm(x, kBallScale)), ballMat);
        ball_.setRaster(sr::rasterAt(movRegImm(x, kBallRaster)));
        ball_.flags |= x.u16(kBallFlags + 7);   // or word [ball + 0xc4], imm16: hidden, environment-mapped
        scene_.add(ball_);
        tunnel_.flags |= uint32_t(x.u8(kTunnelFlags + 6)) << 8;   // or byte [tunnel + 0xc5], imm8: no culling
        buildTunnel();

        // 0x40684c: the path (D = the ring spacing of the loaded tunnel)
        path_ = buildPath(x, D_);

        // 0x406c70: the smoke sprites, each put at the head of the scene's list
        sr::Sprite sp;
        sp.texture = eng_.texture(d.file(x.cstr(kSmokeName)), movMemImm(x, kSmokeW), movMemImm(x, kSmokeH));
        sp.table = uint32_t(movMemImm(x, kSmokeDraw)) == kSmokeBlended ? eng_.table(d.file(x.cstr(kSmokeShdName))) : -1;
        sp.w = movMemImm(x, kSmokeW);
        sp.h = movMemImm(x, kSmokeH);
        sp.scale = asFloat(uint32_t(movMemImm(x, kSmokeScale)));
        sp.size = asFloat(uint32_t(movMemImm(x, kSmokeSize)));
        sp.bias = asFloat(uint32_t(movMemImm(x, kSmokeBias)));
        for (sr::Object& s : smoke_) {
            s.makeBillboard(sp);
            scene_.add(s);
        }

        // the mask and the names (0x415e50) and their tables
        const int maskTable = eng_.table(d.file(x.cstr(kMaskIntName)));
        nameTable_ = eng_.table(d.file(x.cstr(kNameIntName)));
        loadLevels(mask_, d.file(x.cstr(kMaskName)), int(movRegImm(x, kMaskW)), int(movRegImm(x, kMaskH)),
                   movMemImm(x, kMaskType), maskTable, nullptr);
        for (size_t i = 0; i < names_.size(); i++) {
            const NameSpec& s = kNames[i];
            loadLevels(names_[i], d.file(x.cstr(s.file)), int(movRegImm(x, s.w)), int(movRegImm(x, s.h)),
                       int(movRegImm(x, kNameType)), nameTable_, &kFaces[i]);
            if (c.art()) loadArt(names_[i], c.pictures->redrawPath(s.art));
        }
        buildFits(maskTable);
        levelShader_.build(kLevelVS, kLevelFS, "credits level maps");
    }

    void render(Ctx& c, double r) override {
        stepTicks(c);
        const bool ex = sr::fidelity().exact;
        // the tick state shown at refresh r: n ticks (n = r - start), blended between ticks n and n + 1
        double n = std::clamp(c.refreshes(r), 0.0, double(ticks_.size() - 2));
        if (ex) n = std::floor(n);
        const int i = int(n);
        const double fr = n - i;
        const Tick &t0 = ticks_[size_t(i)], &t1 = ticks_[size_t(i) + 1];
        auto mixV = [&](const sr::V3& a, const sr::V3& b) { return a + (b - a) * fr; };
        const double pos = ex ? double(float(speed_ * i)) : speed_ * n;   // [0x426434]
        const double theta = ex ? double(t0.theta) : t0.theta + (double(t1.theta) - t0.theta) * fr;
        const sr::V3 C = ex ? t0.C : mixV(t0.C, t1.C), S = ex ? t0.S : mixV(t0.S, t1.S);
        const sr::M3 spin = ex ? t0.spin : sr::axisAngle(spinAxis_, spinAngle_ * n);

        const bool event = c.entryCount(kBallEvent, r) > 0;
        const int name = c.entryCount(kNameEvent, r) - 1;

        sr::M3 look;
        sr::V3 cam = frameCamera(pos, C, ex, look);                 // 0x4061fc
        frameTunnel(pos, cam, look, ex);                              // 0x406470, 0x4063fc
        camera_.pos = cam;
        camera_.rot = look * sr::euler(0, 0, theta);                  // 0x4145f4(0, 0, theta), 0x414070
        ball_.rot = spin;
        ball_.pos = ex ? trunc3(S) : S;
        if (event) ball_.flags &= ~sr::kHidden;                       // 0x406794
        else ball_.flags |= sr::kHidden;

        eng_.render(c, scene_, pal_, 0);                              // 0x41127c(scene, 0), 0x40c6ac
        if (!event) drawLevels(c, mask_, maskX_, maskY_);             // 0x415df0(mask, 0, 0): before 9:48
        if (name >= 0 && name < nameCount_ && size_t(name) < names_.size())
            drawLevels(c, names_[size_t(name)], nameX_, nameY_);      // 0x415df0(names[name], 0, 117)
    }

private:
    // ---------------------------------------------------------------- init
    void readConstants(const InsideExe& x) {
        roll_ = x.f64(kRoll);
        speed_ = x.f64(kSpeed);
        camNew_ = x.f64(kCamNew);
        camOld_ = x.f64(kCamOld);
        leadStep_ = x.f64(kLeadStep);
        ballNew_ = x.f64(kBallNew);
        ballOld_ = x.f64(kBallOld);
        sr::V3 e = pushedEuler(x, kSpinPush);
        spinStep_ = sr::euler(e.x, e.y, e.z);
        sr::toAxisAngle(spinStep_, spinAxis_, spinAngle_);
        ballLead_ = imm8At(x, kBallLead, 2);
        roll0_ = x.f32(kRoll0);
        lead0_ = x.f32(kLead0);
        targetLead_ = imm8At(x, kTargetLead, 2);
        targetCam_ = x.f64(kTargetCam);
        targetPath_ = x.f64(kTargetPath);
        uStep_ = x.s32(kUStep + 2);
        u16_ = x.f64(kU16);
        uTwelfth_ = x.f64(kUTwelfth);
        fadeRings_ = imm8At(x, kFadeRings, 6);
        minLight_ = imm8At(x, kMinLight, 2);
        smokeOffset_ = int32_t(movRegImm(x, kSmokeOffset));
        vStep_ = x.s32(kVStep + 2);
        vDiv_ = int32_t(movRegImm(x, kVDiv));
        seamU_ = int32_t(movRegImm(x, kSeamU));
        nameCount_ = imm8At(x, kNameCount, 6);
        nameX_ = asFloat(pushImm(x, kNameX));
        nameY_ = asFloat(pushImm(x, kNameY));
        maskX_ = asFloat(pushImm(x, kMaskX));
        maskY_ = asFloat(pushImm(x, kMaskY));
    }

    // 0x406d10: the ring of the loaded tunnel (its first 12 vertices at z = 0 after the move), the spacing D, the seam
    // vertices S(k) (copies of ring k's first vertex) in gaps 0..8 of the closing quads' faces.
    void buildTunnel() {
        tunnel_.translate(sr::V3(0, 0, -tunnel_.verts[0].p.z));       // 0x41172c
        D_ = int(tunnel_.verts[13].p.z - tunnel_.verts[0].p.z);       // vertex 13 (+0x350) - vertex 0
        for (int j = 0; j < kRingVerts; j++) ring_[size_t(j)] = {tunnel_.verts[size_t(j)].p.x, tunnel_.verts[size_t(j)].p.y};
        const int seam0 = kRings * kRingVerts;
        tunnel_.verts.resize(size_t(seam0 + kRings));
        for (int k = 0; k < kRings - 2; k++) {   // faces 24 k + 22 and 24 k + 23, k = 0..8
            sr::Face& f22 = tunnel_.faces[size_t(2 * kRingVerts * k + 2 * kRingVerts - 2)];
            sr::Face& f23 = tunnel_.faces[size_t(2 * kRingVerts * k + 2 * kRingVerts - 1)];
            f22.a = seam0 + k;
            f22.b = seam0 + k + 1;
            f23.b = seam0 + k + 1;
        }
    }

    // A bitmap of levels 0..31 (0x415e50: pitch = width) and its table; for the names the face pixels get bit 7.
    void loadLevels(LevelMap& m, std::span<const uint8_t> px, int w, int h, int type, int table,
                    const FaceShapes* faces) {
        m.w = w;
        m.h = h;
        m.bmp = eng_.bitmap(px, w, w, h);
        m.bmp.type = type;
        m.bmp.table = table;
        m.bmp.key = 0;
        m.key = type >= 3;
        m.fit = faces ? kNameFit : kMaskFit;
        std::vector<uint8_t> lv(size_t(w) * h, 0);
        for (int y = 0; y < h; y++)
            for (int xx = 0; xx < w; xx++) {
                size_t o = size_t(y) * w + size_t(xx);
                uint8_t v = uint8_t((o < px.size() ? px[o] : 0) & (kLevels - 1));
                if (faces && v != kStarLevel) {
                    float cx = xx + 0.5f, cy = y + 0.5f;
                    bool in = false;
                    for (const auto& e : faces->ellipses) {
                        float dx = (cx - e[0]) / e[2], dy = (cy - e[1]) / e[3];
                        in = in || dx * dx + dy * dy <= 1.0f;
                    }
                    for (const auto& b : faces->boxes) in = in || (cx >= b[0] && cx < b[2] && cy >= b[1] && cy < b[3]);
                    if (in) v |= 128;
                }
                lv[o] = v;
            }
        m.levels = inside::uploadIndices(lv, w, h);
    }

    // A redraw of a name's star and letters (grey 255 x level / 31, at its own resolution over the bitmap's area).
    static void loadArt(LevelMap& m, const std::string& path) {
        if (path.empty()) return;
        int w = 0, h = 0, ch = 0;
        unsigned char* g = stbi_load(path.c_str(), &w, &h, &ch, 1);
        if (!g) {
            SDL_Log("credits: cannot read %s: %s", path.c_str(), stbi_failure_reason());
            return;
        }
        m.art.createRaw(w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, g, true, false, true);
        stbi_image_free(g);
    }

    // The fits of the tables' rows 0..31 (the names' level 0 is the transparent key: no change).
    void buildFits(int maskTable) {
        std::vector<float> px(size_t(kLevels) * 4 * 4, 0.0f);
        const int tables[2] = {nameTable_, maskTable};
        for (int t = 0; t < 2; t++) {
            const std::vector<sr::RowFit>& f = eng_.fit(tables[t], pal_);
            for (int l = 0; l < kLevels; l++) {
                sr::RowFit rf{Vec3(1, 1, 1), Vec3(0, 0, 0)};
                if (!(t == kNameFit && l == 0) && size_t(l) < f.size()) rf = f[size_t(l)];
                float* b = &px[(size_t(2 * t) * kLevels + size_t(l)) * 4];
                float* s = &px[(size_t(2 * t + 1) * kLevels + size_t(l)) * 4];
                b[0] = rf.b.x; b[1] = rf.b.y; b[2] = rf.b.z; b[3] = 1;
                s[0] = rf.s.x; s[1] = rf.s.y; s[2] = rf.s.z; s[3] = 0;
            }
        }
        fits_.createRaw(kLevels, 4, GL_RGBA32F, GL_RGBA, GL_FLOAT, px.data(), false, false, false);
    }

    // ---------------------------------------------------------------- per refresh
    // 0x405f30 for every refresh of the part, from its start (the 9:48 event lowers the lead from the tick after it).
    void stepTicks(const Ctx& c) {
        const double ev = c.entryRefresh(kBallEvent);
        const int count = std::max(2, int(std::ceil(c.end - c.start)) + 2);
        if (!ticks_.empty() && c.start == tickStart_ && ev == tickEvent_ && int(ticks_.size()) == count + 1) return;
        tickStart_ = c.start;
        tickEvent_ = ev;
        ticks_.assign(size_t(count) + 1, Tick{});
        Tick t;
        t.theta = roll0_;
        t.s = lead0_;
        ticks_[0] = t;
        float pos = 0;
        for (int k = 1; k <= count; k++) {
            pos = float(double(pos) + speed_);
            int n = wrap(int(double(pos) / D_));
            float f = float(double(int(pos) % D_) / D_);
            t.theta = float(double(t.theta) + roll_);
            t.C = t.C * camOld_ + lerpPath(n, wrap(n + 1), f) * camNew_;   // (n + 1 is not wrapped; n < 150 here)
            t.spin = t.spin * spinStep_;
            if (ev <= c.start + k - 1) t.s = float(double(t.s) + leadStep_);
            int m = wrap(n + int(t.s) + ballLead_);
            sr::V3 L = lerpPath(m, wrap(m + 1), f);
            L.x = double(float(L.x));
            t.S = t.S * ballOld_ + L * ballNew_;
            ticks_[size_t(k)] = t;
        }
    }

    // 0x4061fc: the camera at the smoothed position, looking (transposed) at a point 3 rings ahead on the path with
    // its x mixed with the camera's.
    sr::V3 frameCamera(double pos, const sr::V3& C, bool ex, sr::M3& look) const {
        int nn;
        double f;
        if (ex) {
            nn = wrap(int(pos / D_));
            f = double(int(pos) % D_) / D_;
        } else {
            double q = pos / D_;
            nn = int(std::floor(q));
            f = q - nn;
            nn = wrap(nn);
        }
        sr::V3 cam = ex ? trunc3(C) : C;
        int m = wrap(nn + targetLead_);
        sr::V3 T = lerpPath(m, wrap(m + 1), f);
        sr::V3 target(cam.x * targetCam_ + T.x * targetPath_, T.y, T.z);
        if (ex) target = trunc3(target);
        look = sr::look(target - cam);   // 0x41077c
        return cam;
    }

    // 0x406470 + 0x4063fc: ring k at path entry q + k, its 12 vertices at the path point plus the template ring
    // scaled by the entry's radii and turned by the look-at matrix, relative to the camera; the light faded beyond
    // 7 rings; the smoke beside the ring centres.
    void frameTunnel(double pos, const sr::V3& cam, const sr::M3& look, bool ex) {
        const int q = ex ? int(pos) / D_ : int(std::floor(pos / D_));
        const int r10 = q % vDiv_;
        const double fade = double(fadeRings_) * D_;
        tunnel_.pos = cam;
        for (int k = 0; k < kRings; k++) {
            const PathEntry& pe = path_[size_t(wrap(q + k))];
            const double v = ex ? double(((r10 + k) * vStep_) / vDiv_) : double(r10 + k) * vStep_ / vDiv_;
            for (int j = 0; j < kRingVerts; j++) {
                sr::Vertex& vt = tunnel_.verts[size_t(k * kRingVerts + j)];
                const double rad = pe.radius[j];
                sr::V3 w = look * sr::V3(ring_[size_t(j)][0] * rad, ring_[size_t(j)][1] * rad, 0);   // 0x4144ec
                sr::V3 p(pe.p[0] + w.x - cam.x, pe.p[1] + w.y - cam.y, pe.p[2] + w.z - cam.z);
                double L = pe.light[j];
                if (ex) {
                    p = trunc3(p);
                    int over = int(std::trunc(std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) - fade));
                    if (over > 0) L -= over;
                    L = std::max<double>(L, minLight_);
                    vt.u = std::trunc(double(j * uStep_) * u16_ * uTwelfth_);
                } else {
                    L -= std::max(0.0, p.len() - fade);
                    L = std::max<double>(L, minLight_);
                    vt.u = double(j * uStep_) * u16_ * uTwelfth_;
                }
                vt.p = p;
                vt.v = v;
                vt.shade = L;
            }
        }
        // 0x4063fc: S(k) = ring k's first vertex with u = one texture width (the original: 0x1000 + 0x1000 / D)
        const int seam0 = kRings * kRingVerts;
        for (int k = 0; k < kRings; k++) {
            sr::Vertex& s = tunnel_.verts[size_t(seam0 + k)];
            s = tunnel_.verts[size_t(k * kRingVerts)];
            s.u = ex ? double(seamU_ + seamU_ / D_) : double(seamU_);
        }
        for (int k = 0; k < kSmokes; k++) {
            const PathEntry& pe = path_[size_t(wrap(q + k))];
            smoke_[size_t(k)].pos = sr::V3(pe.p[0] + smokeOffset_, pe.p[1] + smokeOffset_, pe.p[2]);
        }
    }

    // ---------------------------------------------------------------- drawing
    // 0x415df0 with a level map: out = background x b + s through the fits (kLevelFS), the background from a copy of
    // the area the bitmap covers.
    void drawLevels(Ctx& c, const LevelMap& m, float x, float y) {
        const sr::Fidelity& f = sr::fidelity();
        if (f.exact) {   // 0x415df0 truncates the position
            x = std::trunc(x);
            y = std::trunc(y);
        }
        if (f.exact && !f.filter) {   // the original's table lookups (the engine's pixel check)
            eng_.drawBitmap(c, m.bmp, x, y, pal_);
            return;
        }
        // with filtered taps a transparent bitmap fades out over half a texel past its edge
        const float mg = f.filter && m.key ? 1.0f : 0.0f;
        const float rx = x - mg, ry = y - mg, rw = float(m.w) + 2 * mg, rh = float(m.h) + 2 * mg;
        Target& out = *c.out;
        const int W = out.w, H = out.h;
        const int px0 = std::clamp(int(std::floor(rx * W / 320.0f)) - 1, 0, W);
        const int px1 = std::clamp(int(std::ceil((rx + rw) * W / 320.0f)) + 1, 0, W);
        const int py0 = std::clamp(H - int(std::ceil((ry + rh) * H / 200.0f)) - 1, 0, H);   // GL rows from the bottom
        const int py1 = std::clamp(H - int(std::floor(ry * H / 200.0f)) + 1, 0, H);
        if (px1 <= px0 || py1 <= py0) return;
        TargetPool::Lease bg = c.tempSized(W, H, false, GL_RGBA16);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, out.fbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, bg->fbo);
        gl::BlitFramebuffer(px0, py0, px1, py1, px0, py0, px1, py1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        out.bind();
        setBlend(Blend::None);
        levelShader_.use();
        m.levels.bind(0);
        (m.art.id ? m.art : fits_).bind(1);
        fits_.bind(2);
        bg->bindTex(3);
        levelShader_.set("lev", 0);
        levelShader_.set("art", 1);
        levelShader_.set("fits", 2);
        levelShader_.set("bg", 3);
        levelShader_.set("tab", m.fit);
        levelShader_.set("useArt", m.art.id ? 1 : 0);
        levelShader_.set("edge", m.key ? 0 : 1);
        levelShader_.set("filt", f.filter ? 1 : 0);
        levelShader_.set("size", float(m.w), float(m.h));
        levelShader_.set("rect", rx, ry, rw, rh);
        levelShader_.set("origin", x, y);
        drawQuad();
        gl::ActiveTexture(GL_TEXTURE0);
    }

    // ---------------------------------------------------------------- helpers
    static int wrap(int n) { return ((n % kEntries) + kEntries) % kEntries; }
    static sr::V3 trunc3(const sr::V3& v) { return {std::trunc(v.x), std::trunc(v.y), std::trunc(v.z)}; }
    // a (1 - f) + b f per axis
    sr::V3 lerpPath(int a, int b, double f) const {
        const PathEntry &p = path_[size_t(a)], &q = path_[size_t(b)];
        return {p.p[0] * (1 - f) + q.p[0] * f, p.p[1] * (1 - f) + q.p[1] * f, p.p[2] * (1 - f) + q.p[2] * f};
    }

    sr::Renderer eng_;
    sr::Scene scene_;
    sr::Object camera_, tunnel_, ball_;
    std::array<sr::Object, kSmokes> smoke_;
    int pal_ = 0, nameTable_ = -1;
    LevelMap mask_;
    std::array<LevelMap, std::size(kNames)> names_;
    Texture fits_;
    Shader levelShader_;

    std::vector<PathEntry> path_;
    std::array<std::array<double, 2>, kRingVerts> ring_{};
    int D_ = 1;
    std::vector<Tick> ticks_;
    double tickStart_ = 0, tickEvent_ = 0;

    // the routines' constants (readConstants)
    double roll_ = 0, speed_ = 0, camNew_ = 0, camOld_ = 0, leadStep_ = 0, ballNew_ = 0, ballOld_ = 0;
    double targetCam_ = 0, targetPath_ = 0, u16_ = 0, uTwelfth_ = 0;
    sr::M3 spinStep_;
    sr::V3 spinAxis_;
    double spinAngle_ = 0;
    float roll0_ = 0, lead0_ = 0;
    int ballLead_ = 0, targetLead_ = 0, uStep_ = 0, fadeRings_ = 0, minLight_ = 0, smokeOffset_ = 0;
    int vStep_ = 0, vDiv_ = 1, seamU_ = 0, nameCount_ = 0;
    float nameX_ = 0, nameY_ = 0, maskX_ = 0, maskY_ = 0;
};

}  // namespace

REGISTER_SCENE(CreditsScene, "credits");
