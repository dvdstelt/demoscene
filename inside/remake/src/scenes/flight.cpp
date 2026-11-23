// The flight over the landscape (19:63..23:63; inside/re/notes-flight.md): three small environment-mapped ships fly
// over a fogged, textured landscape under a flat sky with the sun and a lens flare, followed by a chase camera that
// jumps away once when it comes too close to a ship. Ports the init 0x402948 (0x401450 the grid and the height field,
// 0x40136c its light, 0x4018d0 the ships, 0x4023d4 / 0x402294 / 0x402330 the sun and the flares), the tick routine
// 0x4021f8 (0x401dbc the ships, 0x4020f8 the camera) and the frame routine 0x4028e4 (0x40276c the objects, 0x402638
// the camera jump, 0x4024d0 the flares' visibility, 0x401ab8 the grid around the camera, 0x401c64 its fog, the clear
// to the sky index 0x41127c and the SurRender render 0x40c6ac of the scene: the landscape through rasterizer
// 0x412264 with map.shd and map.fog, the ships through 0x412db0 with the environment stage 0x41acc4, the sun and the
// flares through the sprite drawer 0x415878). The fade in over 60 refreshes, the fade out and the flashes are the
// framework's.
//
// Motion. The tick routine is replayed from the part's start for the whole part at once (about 1400 ticks): plain
// doubles with the notes' integer rule for the yaw arguments n1..n4 (notes 5.4: the original's x87 extended
// precision makes T * 0.12 truncate one lower where it is a whole number) and the original's float32 roundings, T =
// the part tick counter P at each call (PORT.md; the remake's clock gives 4n - 2 at the n-th call where the capture
// had 4n, so the paths drift apart by up to some 40 units), and the camera jump 0x402638 checked once after every
// tick (only the first frame after a tick can see a ship that close) with Watcom's rand() from the state the capture
// had at the flight's start. A frame between two ticks blends the positions and the ships' yaw by the fraction of
// the refresh and rebuilds the camera matrix (look at ship 0, roll, pitch: 0x414cec) from the blended look vector;
// in the refresh after a jump the camera keeps its old matrix at the new position (the original shows mostly sky
// there) and turns back with the next tick.
//
// Evaluated exactly instead of the original's shortcuts (notes section 8):
// - the landscape is the original's lattice (the 19 x 19 vertices around the camera's cell with their heights and
//   light values, every cell split along the same diagonal, so the surface is the same flat triangles), drawn
//   perspective-correct with a depth buffer instead of affine in the painter's order. Per pixel: the four texels of
//   map.raw around the point (u, v continuous in world x, z instead of utab's truncated values), each through the two
//   map.shd rows around the Gouraud light level, blended in colour; then the fog towards the sky colour 0xdd that
//   map.fog tabulates, evaluated in colour at the level of the true distance of the surface point instead of a level
//   per vertex interpolated across the triangle;
// - the ships: the mesh without the truncation to whole units, u, v of the environment stage 0x41acc4 (camera-space
//   normal and position) without its truncations, river.raw filtered in colour, depth-tested against the landscape
//   (the painter's order of the original sorts whole faces by their mean depth);
// - the sun and the flares: quads at the float positions and sizes of 0x415878 (keeping its vertical offset with the
//   horizontal focal length: 1.2 times too far from the centre line), filtered; the flares add half their colour
//   (what flare.shd tabulates), clamped; the sun behind everything and the flares on top, where their sort keys put
//   them;
// - anti-aliased edges (MSAA).
// Kept: the grid's extent (its fogged far vertices cover the sun with the sky colour behind distant mountains), the
// light from the x slope only, the camera and the ships flying through mountains (the near plane at 64 units cuts
// them open), the flares' visibility from the projected sun without an occlusion test, the camera jump.
//
// INSIDE_ENGINE (inside/engine.h, sr::fidelity()) also switches this part: "original" draws affine, with the nearest
// texels and table rows, map.fog by the fog level per vertex as 0x401c64 computes it, utab's coordinates and integer
// positions, for pixel checks against the capture at --shotsize 320x200.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"

namespace {

namespace sr = inside::sr;

// ------------------------------------------------------------------------------------------------- INSIDE.EXE
// File names (the strings the init 0x402948 passes to 0x400614).
constexpr uint32_t kMapName = 0x4207cc, kFogName = 0x4207dc, kShdName = 0x4207ec, kRiverName = 0x4207fc,
                   kShipName = 0x420810, kPalName = 0x420820;
// The init's file loads (mov eax, name; call 0x400614; mov [slot], eax), and 0x4023d4, which makes the sun (0x402294)
// and the flares (0x402330: eax = [slot] the bitmap, edx = width, ebx = height, push k).
constexpr uint32_t kLoadsBegin = 0x40297b, kLoadsEnd = 0x402a43, kLoadFile = 0x400614;
constexpr uint32_t kSpritesBegin = 0x4023d4, kSpritesEnd = 0x4024cc, kMakeFlare = 0x402330;
// 0x402294: mov eax, [slot] (the sun's bitmap); mov dword [ebx + 0xe4 / 0xe8 / 0xec / 0xf0], width, height, k, size.
constexpr uint32_t kSunSlot = 0x4022cd, kSunW = 0x4022dc, kSunH = 0x4022e6, kSunK = 0x4022fa, kSunSize = 0x402304;
constexpr uint32_t kFlareSize = 0x40237b;   // 0x402330: mov dword [ebx + 0xf0], size
constexpr uint32_t kFov = 0x402ab1;         // the init: mov dword [scene + 0x3c], fov
constexpr uint32_t kSeed = 0x40295d;        // the init: mov eax, seed; ...; call srand
constexpr uint32_t kShipScale = 0x401988;   // 0x4018d0: mov ebx, the load scale of ship.sro (16.16)
constexpr uint32_t kSunDx = 0x4028b6;       // 0x40276c: sub eax, dx (sun x = control x - dx)
constexpr uint32_t kSunY = 0x4028d4;        // 0x40276c: mov dword [sun + 0x14], y
constexpr uint32_t kFlareMargin = 0x40257c; // 0x4024d0: sub eax, margin (the window widened for the flares)

// 0x401450: the height of column i, row j is the sum of eleven terms amp * f(arg * pi/2 * freq / 128) with f = sin
// (0x4012e0) or cos (0x401308), summed in this order; arg is i or j plus an offset (mov r32, imm32 in the routine)
// or one of the products. Each term: the instruction with the offset, the push of the float freq, the double amp.
enum class Arg { I, J, IJ03, JI1, IJ, IJ1 };   // i + off, j + off, (int)(k i j), j (i + 1), i j, i (j + 1)
struct Term {
    bool cosine;
    Arg arg;
    uint32_t offset, freq, amp;
};
constexpr Term kTerms[] = {
    {false, Arg::I, 0x40157c, 0x40159e, 0x42059f}, {false, Arg::J, 0x40151e, 0x4015ae, 0x4205a7},
    {true, Arg::J, 0x401517, 0x4015c6, 0x42059f},  {true, Arg::I, 0x40158f, 0x4015e2, 0x4205a7},
    {false, Arg::IJ03, 0, 0x401608, 0x4205b7},     {true, Arg::I, 0x40156b, 0x40162d, 0x4205bf},
    {true, Arg::J, 0x401512, 0x401647, 0x4205c7},  {false, Arg::I, 0x401566, 0x401663, 0x4205cf},
    {false, Arg::JI1, 0, 0x40167f, 0x4205d7},      {true, Arg::IJ, 0, 0x401699, 0x4205cf},
    {false, Arg::IJ1, 0, 0x4016b5, 0x4205df},
};
constexpr uint32_t kIJFactor = 0x4205af;                       // the double k of (int)(k i j)
constexpr uint32_t kMaxHeight = 0x4205e7;                      // float: heights above it become 0
constexpr uint32_t kSinPi = 0x420540, kSinInv = 0x420548;      // 0x4012e0: double pi/2, float 1/128
constexpr uint32_t kCosPi = 0x42054c, kCosInv = 0x420554;      // 0x401308
// 0x40136c: float 1/512, double angle offset, doubles 64, 16, float minimum
constexpr uint32_t kSlope = 0x42055f, kLightAngle = 0x420573, kLight64 = 0x420563, kLight16 = 0x42056b,
                   kLightMin = 0x42057b;
constexpr uint32_t kUtab4096 = 0x42058f, kUtabStep = 0x420597;   // 0x401450: utab[k] = (int)(k * 4096 * (1 / 18))
// 0x401c64: float 0.8 x double 3328 (the fog's start), the doubles per unit, 63, 16
constexpr uint32_t kFogStartF = 0x4227e8, kFogStartD = 0x42061a, kFogPerUnit = 0x420622, kFog63 = 0x42062a,
                   kFog16 = 0x420632;

// The tick routine's constants (doubles unless noted; notes 5.1..5.3).
constexpr uint32_t kSpeed = 0x4227ec, kSpeedShip[3] = {0x4206ee, 0x4206f6, 0x4206fe}, kSpeedCam = 0x4206f6;
constexpr uint32_t kT = 0x42063a, kN1i = 0x420642, kN1m = 0x42064a, kF1i = 0x420652, kF1c = 0x42065a,
                   kN2c = 0x42066e, kN3c = 0x420686, kN4c = 0x420696, kN4i = 0x42069e;
constexpr uint32_t kN3i = 0x401efc;                            // imul eax, edx, imm8 (n3's per-ship offset)
constexpr uint32_t kHalfPiA = 0x420662, kInvA = 0x42066a;      // the first term's own pi/2 and float 1/128
constexpr uint32_t kF2 = 0x401ed6, kF3 = 0x401f22, kF4 = 0x401f65;   // push float (cos / sin frequencies)
constexpr uint32_t kR2 = 0x420676, kR1 = 0x42067e, kR3 = 0x42068e, kR4 = 0x4206a6, kRMin = 0x4206ae;
constexpr uint32_t kYDecay = 0x4206b6, kFollow = 0x4206be, kHi = 0x4206c6, kHMin = 0x4206ce, kHMax = 0x4206d6;
constexpr uint32_t kCamDecay = 0x4206de, kCamY = 0x4206e6;
constexpr uint32_t kRoll = 0x402194, kPitch = 0x4021a8;        // two pushes each (high dword first)
constexpr uint32_t kJumpDist = 0x420729;                       // 0x402638

// The capture's state of Watcom's rand() at the flight's start (notes-flight 6.2: left by the flower's start; the
// flight calls rand() only for the jump until then).
constexpr uint32_t kRandAtStart = 0x95a8f93f;

// ------------------------------------------------------------------------------------------------- code reading
uint32_t immAfter(const InsideExe& x, uint32_t a, uint8_t op, int at) {
    if (x.u8(a) != op) throw std::runtime_error("flight: unexpected instruction at " + std::to_string(a));
    return x.u32(a + uint32_t(at));
}
uint32_t movRegImm(const InsideExe& x, uint32_t a) {   // mov r32, imm32
    if ((x.u8(a) & 0xf8) != 0xb8) throw std::runtime_error("flight: no mov r32, imm32 at " + std::to_string(a));
    return x.u32(a + 1);
}
uint32_t pushImm(const InsideExe& x, uint32_t a) {
    if (x.u8(a) == 0x68) return x.u32(a + 1);
    if (x.u8(a) == 0x6a) return uint32_t(int32_t(x.s8(a + 1)));
    throw std::runtime_error("flight: no push at " + std::to_string(a));
}
int32_t movMemImm(const InsideExe& x, uint32_t a) {    // mov dword [r + disp8 / disp32], imm32
    if (x.u8(a) != 0xc7) throw std::runtime_error("flight: no mov [r + d], imm32 at " + std::to_string(a));
    int mod = x.u8(a + 1) >> 6;
    return x.s32(a + (mod == 1 ? 3 : 6));
}
float asFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
double pushedDouble(const InsideExe& x, uint32_t a) {   // push high; push low
    uint32_t hi = pushImm(x, a), lo = pushImm(x, a + (x.u8(a) == 0x68 ? 5 : 2));
    uint64_t bits = uint64_t(hi) << 32 | lo;
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}
// A double that is a whole number of hundredths (for the integer form of n1..n4).
int64_t hundredths(double v, double scale = 100) {
    double s = v * scale;
    int64_t r = std::llround(s);
    if (std::abs(s - double(r)) > 1e-6) throw std::runtime_error("flight: unexpected yaw constant");
    return r;
}

// The init's file loads: slot -> the name's address.
std::map<uint32_t, uint32_t> traceLoads(const InsideExe& x) {
    std::map<uint32_t, uint32_t> files;
    uint32_t eax = 0, file = 0;
    for (uint32_t a = kLoadsBegin; a < kLoadsEnd;) {
        uint8_t op = x.u8(a);
        if (op == 0xb8) { eax = x.u32(a + 1); a += 5; continue; }                      // mov eax, name
        if (op == 0xba) { a += 5; continue; }                                           // mov edx, imm32
        if (op == 0xe8) { file = (a + 5 + x.u32(a + 1) == kLoadFile) ? eax : 0; a += 5; continue; }
        if (op == 0xa3) { if (file) files[x.u32(a + 1)] = file; a += 5; continue; }   // mov [slot], eax
        throw std::runtime_error("flight: unexpected instruction in the loads at " + std::to_string(a));
    }
    return files;
}

struct FlareDef {
    uint32_t slot = 0;   // where the init put the bitmap
    int w = 0, h = 0;
    float k = 0;         // scales the position: 0 the screen centre, 1 the sun
};

// 0x4023d4: the eight calls of 0x402330 with their registers and pushed k.
std::vector<FlareDef> traceFlares(const InsideExe& x) {
    std::vector<FlareDef> out;
    uint32_t slot = 0, ebx = 0, edx = 0, push = 0;
    for (uint32_t a = kSpritesBegin; a < kSpritesEnd;) {
        uint8_t op = x.u8(a), m = x.u8(a + 1);
        if (op == 0x52 || op == 0x53) { a += 1; continue; }                // push edx / ebx
        if (op == 0x31 && m == 0xd2) { edx = 0; a += 2; continue; }        // xor edx, edx
        if (op == 0x89 && m == 0xda) { edx = ebx; a += 2; continue; }      // mov edx, ebx
        if (op == 0x89 && m == 0x15) { a += 6; continue; }                 // mov [abs32], edx
        if (op == 0xa3) { a += 5; continue; }                              // mov [abs32], eax
        if (op == 0xa1) { slot = x.u32(a + 1); a += 5; continue; }         // mov eax, [slot]
        if (op == 0xba) { edx = x.u32(a + 1); a += 5; continue; }
        if (op == 0xbb) { ebx = x.u32(a + 1); a += 5; continue; }
        if (op == 0x6a || op == 0x68) { push = pushImm(x, a); a += op == 0x68 ? 5 : 2; continue; }
        if (op == 0xe8) {
            if (a + 5 + x.u32(a + 1) == kMakeFlare) out.push_back({slot, int(edx), int(ebx), asFloat(push)});
            a += 5;
            continue;
        }
        throw std::runtime_error("flight: unexpected instruction in 0x4023d4 at " + std::to_string(a));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------- maths
struct V3d {
    double x = 0, y = 0, z = 0;
    V3d operator+(const V3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3d operator-(const V3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3d operator*(double s) const { return {x * s, y * s, z * s}; }
};
V3d lerp(const V3d& a, const V3d& b, double f) { return a + (b - a) * f; }
V3d truncated(const V3d& v) { return {std::trunc(v.x), std::trunc(v.y), std::trunc(v.z)}; }
double length(const V3d& v) { return std::sqrt((v.y * v.y + v.x * v.x) + v.z * v.z); }   // 0x4136fc

// 3 x 3 row-major, acting on column vectors.
struct M3d {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    V3d operator*(const V3d& v) const {   // 0x4144ec
        return {(v.x * m[0][0] + v.y * m[0][1]) + v.z * m[0][2], (v.x * m[1][0] + v.y * m[1][1]) + v.z * m[1][2],
                (v.x * m[2][0] + v.y * m[2][1]) + v.z * m[2][2]};
    }
    M3d operator*(const M3d& b) const {   // 0x414070
        M3d o;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                o.m[i][j] = (m[i][1] * b.m[1][j] + m[i][0] * b.m[0][j]) + m[i][2] * b.m[2][j];
        return o;
    }
    M3d transposed() const {
        M3d t;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) t.m[r][c] = m[c][r];
        return t;
    }
};
// M = M R, R a rotation in the plane of columns i, j (0x414270 x: 1, 2, +1; 0x414344 y: 0, 2, -1; 0x414418 z: 0, 1, +1)
M3d rotated(const M3d& a, double angle, int i, int j, double sign) {
    double c = std::cos(angle), s = std::sin(angle);
    M3d o = a;
    for (int r = 0; r < 3; r++) {
        o.m[r][i] = a.m[r][i] * c + sign * (a.m[r][j] * s);
        o.m[r][j] = a.m[r][j] * c - sign * (a.m[r][i] * s);
    }
    return o;
}
M3d rotX(const M3d& a, double t) { return rotated(a, t, 1, 2, 1); }
M3d rotY(const M3d& a, double t) { return rotated(a, t, 0, 2, -1); }
M3d rotZ(const M3d& a, double t) { return rotated(a, t, 0, 1, 1); }
// 0x414cec: rows a, u, f with f = d / |d|, a = (-f.z, 0, f.x) / |..|, u = f x a / |..|
M3d look(const V3d& d) {
    double inv = 1.0 / length(d);
    V3d f = d * inv;
    V3d a{-f.z, 0.0, f.x};
    a = a * (1.0 / length(a));
    V3d u{f.y * a.z - f.z * a.y, f.z * a.x - f.x * a.z, f.x * a.y - f.y * a.x};
    u = u * (1.0 / length(u));
    M3d m;
    const V3d rows[3] = {a, u, f};
    for (int r = 0; r < 3; r++) {
        m.m[r][0] = rows[r].x;
        m.m[r][1] = rows[r].y;
        m.m[r][2] = rows[r].z;
    }
    return m;
}

// Watcom's rand() 0x4134cd.
struct Rand {
    uint32_t seed = 1;
    int operator()() {
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }
};

int pmod(int a, int m) { return ((a % m) + m) % m; }
int64_t tdiv(int64_t a, int64_t b) { return a / b; }   // C division (toward zero)

// ------------------------------------------------------------------------------------------------- height field
constexpr int kField = 128, kHalf = 64;

// 0x401450 (heights, smoothing, mirroring) and 0x40136c (light), in the main line's double precision.
void buildField(const InsideExe& x, std::vector<int32_t>& H, std::vector<int32_t>& L) {
    H.assign(size_t(kField) * kField, 0);
    L.assign(size_t(kField) * kField, 0);
    auto at = [](std::vector<int32_t>& v, int j, int i) -> int32_t& { return v[size_t(j) * kField + size_t(i)]; };
    const double sinPi = x.f64(kSinPi), cosPi = x.f64(kCosPi), ij = x.f64(kIJFactor);
    const float sinInv = x.f32(kSinInv), cosInv = x.f32(kCosInv), maxH = x.f32(kMaxHeight);
    struct T {
        bool cosine;
        Arg arg;
        int off;
        float freq;
        double amp;
    };
    std::vector<T> terms;
    for (const Term& t : kTerms)
        terms.push_back({t.cosine, t.arg, t.offset ? int32_t(movRegImm(x, t.offset)) : 0, asFloat(pushImm(x, t.freq)),
                         x.f64(t.amp)});
    for (int j = 0; j < kHalf; j++)
        for (int i = 0; i < kHalf; i++) {
            double acc = 0;
            for (size_t k = 0; k < terms.size(); k++) {
                const T& t = terms[k];
                int n = 0;
                switch (t.arg) {
                    case Arg::I: n = i + t.off; break;
                    case Arg::J: n = j + t.off; break;
                    case Arg::IJ03: n = int(double(i * j) * ij); break;
                    case Arg::JI1: n = j * (i + 1); break;
                    case Arg::IJ: n = i * j; break;
                    case Arg::IJ1: n = i * (j + 1); break;
                }
                double a = t.cosine ? std::cos(double(n) * cosPi * double(t.freq) * double(cosInv))
                                    : std::sin(double(n) * sinPi * double(t.freq) * double(sinInv));
                double term = a * t.amp;
                acc = k == 0 ? term : term + acc;
            }
            float h = float(acc);
            if (h > maxH) h = 0;
            at(H, j, i) = int32_t(h);
        }
    for (int j = 0; j < kHalf - 1; j++)   // smoothing (int division toward zero; reads only unchanged entries)
        for (int i = 0; i < kHalf - 1; i++)
            at(H, j, i) = (at(H, j, i) + at(H, j, i + 1) + 2 * at(H, j + 1, i + 1)) / 4;
    for (int j = 0; j < kHalf; j++)       // mirror in x, then in z: the field tiles
        for (int i = 0; i < kHalf; i++) at(H, j, kHalf + i) = at(H, j, kHalf - 1 - i);
    for (int k = 0; k < kHalf; k++)
        for (int i = 0; i < kField; i++) at(H, kHalf + k, i) = at(H, kHalf - 1 - k, i);
    // the light: the Lambert term of the slope towards x + 1
    const double slope = double(x.f32(kSlope)), ang = x.f64(kLightAngle), l64 = x.f64(kLight64),
                 l16 = x.f64(kLight16);
    const float lmin = x.f32(kLightMin);
    for (int z = 0; z < kField; z++)
        for (int c = 0; c < kField; c++) {
            double d = double(at(H, z, c) - at(H, z, (c + 1) & (kField - 1))) * slope;
            double a = std::atan2(d, 1.0);
            double v = std::cos(a + ang) * l64 * l16;
            float f = v < double(lmin) ? lmin : float(v);
            at(L, z, c) = int32_t(f);
        }
}

// ------------------------------------------------------------------------------------------------- motion
struct MotionConsts {
    double speedShip[3], speedCam;                  // float32 per refresh (stored as floats by 0x4021f8)
    int64_t t100, n1i100, n1m10, n2c100, n3c100, n3i, n4c100, n4i100;
    double f1i, f1c, halfPiA, halfPiS, halfPiC;
    float invA, invS, invC, f2, f3, f4;
    double r1, r2, r3, r4, rmin, yDecay, follow, hi, hc, hMin, hMax, camDecay, camY, roll, pitch, jumpDist;
};

MotionConsts readMotion(const InsideExe& x) {
    MotionConsts k{};
    double S = x.f64(kSpeed);
    for (int i = 0; i < 3; i++) k.speedShip[i] = double(float(S * x.f64(kSpeedShip[i])));
    k.speedCam = double(float(S * x.f64(kSpeedCam)));
    k.t100 = hundredths(x.f64(kT));
    k.n1i100 = hundredths(x.f64(kN1i));
    k.n1m10 = hundredths(x.f64(kN1m), 10);
    k.n2c100 = hundredths(x.f64(kN2c));
    k.n3c100 = hundredths(x.f64(kN3c));
    if (x.u8(kN3i) != 0x6b) throw std::runtime_error("flight: no imul at 0x401efc");
    k.n3i = x.s8(kN3i + 2);
    k.n4c100 = hundredths(x.f64(kN4c));
    k.n4i100 = hundredths(x.f64(kN4i));
    k.f1i = x.f64(kF1i);
    k.f1c = x.f64(kF1c);
    k.halfPiA = x.f64(kHalfPiA);
    k.invA = x.f32(kInvA);
    k.halfPiS = x.f64(kSinPi);
    k.invS = x.f32(kSinInv);
    k.halfPiC = x.f64(kCosPi);
    k.invC = x.f32(kCosInv);
    k.f2 = asFloat(pushImm(x, kF2));
    k.f3 = asFloat(pushImm(x, kF3));
    k.f4 = asFloat(pushImm(x, kF4));
    k.r1 = x.f64(kR1);
    k.r2 = x.f64(kR2);
    k.r3 = x.f64(kR3);
    k.r4 = x.f64(kR4);
    k.rmin = x.f64(kRMin);
    k.yDecay = x.f64(kYDecay);
    k.follow = x.f64(kFollow);
    k.hi = x.f64(kHi);
    k.hc = x.f64(kF1c);   // h = i * 0.3 + the same 1.7
    k.hMin = x.f64(kHMin);
    k.hMax = x.f64(kHMax);
    k.camDecay = x.f64(kCamDecay);
    k.camY = x.f64(kCamY);
    k.roll = pushedDouble(x, kRoll);
    k.pitch = pushedDouble(x, kPitch);
    k.jumpDist = x.f64(kJumpDist);
    return k;
}

// The state after a tick (or the init's for tick 0).
struct TickState {
    V3d ship[3];
    M3d shipM[3];
    double yaw[3] = {0, 0, 0};   // the rotation this tick applied to each ship
    V3d camPre;                  // the camera after the tick, before a jump
    V3d cam;                     // after the jump (if any)
    V3d lookDir;                 // control point - camera, from which the tick built the matrix
    M3d camM;
    bool jumped = false;
};

class Motion {
public:
    Motion(const MotionConsts& k, uint32_t seed) : k_(k) {
        Rand r;   // the init 0x402948: srand(seed), 0x4018d0 (5 calls), the camera and the control point (4)
        r.seed = seed;
        int x0 = r() % 0x3c00, z0 = r() % 0x3c00;
        for (int i = 0; i < 3; i++) s_.ship[i] = {double(x0 + r() % 0x1400), double(0xa00 * i), double(z0 - 0xa00 * i)};
        int a = r() % 0x800, b = r() % 0xa00;   // z's rand() first
        s_.cam = s_.camPre = s_.ship[0] + V3d{double(b), 0, double(a)};
        a = r() % 0x800;
        b = r() % 0xa00;
        ctl_ = s_.ship[0] + V3d{double(b), 0, double(a)};
        s_.lookDir = ctl_ - s_.cam;
        rand_.seed = kRandAtStart;
    }
    const TickState& state() const { return s_; }
    V3d initialControl() const { return ctl_; }

    // 0x4021f8 with the part tick counter T, then the camera jump 0x402638 of the frame after it.
    void tick(int64_t T) {
        for (int i = 0; i < 3; i++) ship(i, k_.speedShip[i], T);
        ctl_ = s_.ship[0];
        camera(k_.speedCam);
        s_.camPre = s_.cam;
        s_.jumped = jump();
    }

private:
    // n1..n4 of 0x401dbc as integers (notes 5.4): 100 x the value; the x87 value is just below it when T > 0.
    static int64_t below(int64_t v100, int64_t T) {
        if (T > 0 && v100 > 0) return (v100 - 1) / 100;
        return v100 >= 0 ? v100 / 100 : -((-v100) / 100);
    }

    void ship(int i, double s, int64_t T) {   // 0x401dbc
        TickState& st = s_;
        V3d& p = st.ship[i];
        M3d& m = st.shipM[i];
        p = p + V3d{m.m[0][2] * s, m.m[1][2] * s, m.m[2][2] * s};   // forward along the local z
        int64_t n1 = (k_.t100 * T + k_.n1i100 * i) * k_.n1m10 / 1000;
        int64_t n2 = below(k_.t100 * T + k_.n2c100 - 100 * 17 * i, T);   // 17 i: shl 4 + add
        int64_t n3 = below(k_.t100 * T + k_.n3c100 + 100 * k_.n3i * i, T);
        int64_t n4 = below(k_.t100 * T + k_.n4c100 - k_.n4i100 * i, T);
        float f1 = float(double(i) * k_.f1i + k_.f1c);
        float a = float(std::sin(double(n1) * k_.halfPiA * double(f1) * double(k_.invA)));
        auto c = [&](int64_t n, float f) { return std::cos(double(n) * k_.halfPiC * double(f) * double(k_.invC)); };
        auto sn = [&](int64_t n, float f) { return std::sin(double(n) * k_.halfPiS * double(f) * double(k_.invS)); };
        double r = c(n2, k_.f2) * k_.r2 + double(a) * k_.r1;
        r = c(n3, k_.f3) * k_.r3 + r;
        r = r - sn(n4, k_.f4) * k_.r4;
        st.yaw[i] = 0;
        if (std::abs(r) > k_.rmin) {   // yaw only: the ships stay level
            m = rotY(m, r);
            st.yaw[i] = r;
        }
        p.y *= k_.yDecay;
        if (i > 0) {   // follow ship 0, both tests in turn
            const V3d& p0 = st.ship[0];
            double step = s * k_.follow;
            if (p.x > p0.x) p.x -= step;
            if (p.x < p0.x) p.x += step;
            if (p.z > p0.z) p.z -= step;
            if (p.z < p0.z) p.z += step;
        }
        double h = double(i) * k_.hi + k_.hc;
        if (h * k_.hMin < p.y) p.y = -(h * k_.hMax);
    }

    void camera(double s) {   // 0x4020f8
        V3d& c = s_.cam;
        const M3d& m = s_.camM;
        c = c + V3d{m.m[0][2] * s, m.m[1][2] * s, m.m[2][2] * s};
        c.y *= k_.camDecay;
        if (c.y > k_.camY) c.y = k_.camY;
        s_.lookDir = ctl_ - c;
        s_.camM = rotX(rotZ(look(s_.lookDir), k_.roll), k_.pitch);
    }

    bool jump() {   // 0x402638: away from a ship closer than jumpDist, by 5..14 times the offset (2..6 in y)
        bool hit = false;
        for (int i = 0; i < 3; i++) {
            V3d d = s_.ship[i] - s_.cam;
            if (std::sqrt((d.x * d.x + d.y * d.y) + d.z * d.z) < k_.jumpDist) {
                double n1 = std::trunc(double(rand_() % 10 + 5) * d.x);
                double n2 = std::trunc(double(rand_() % 5 + 2) * d.y);
                double n3 = std::trunc(double(rand_() % 10 + 5) * d.z);
                s_.cam = s_.cam - V3d{n1, n2, n3};
                hit = true;
            }
        }
        return hit;
    }

    MotionConsts k_;
    TickState s_;
    V3d ctl_;
    Rand rand_;
};

// ------------------------------------------------------------------------------------------------- shaders
// Camera space: x right, y down, z forward (units). VGA x = cx + fx x / z, y = cy + fy y / z.
const char* kProjGLSL = R"(
uniform vec4 proj;      // fx, fy, cx, cy
uniform vec2 depthAB;   // clip z = A z + B
vec4 clipOf(vec3 p) {
    return vec4(2.0 * (proj.z * p.z + proj.x * p.x) / 320.0 - p.z, p.z - 2.0 * (proj.w * p.z + proj.y * p.y) / 200.0,
                depthAB.x * p.z + depthAB.y, p.z);
}
)";

const char* kLandVS = R"(
layout(location = 0) in vec3 aPos;   // camera space
layout(location = 1) in vec2 aUV;    // map.raw texels
layout(location = 2) in vec2 aLev;   // light level (table row coordinate), fog level of the vertex
INTERP centroid out vec3 vPos;
INTERP centroid out vec2 vUV;
INTERP centroid out vec2 vLev;
void main() {
    gl_Position = clipOf(aPos);
    vPos = aPos;
    vUV = aUV;
    vLev = aLev;
}
)";

const char* kPalGLSL = R"(
uniform sampler2D pal;
uniform int filt;       // 0: the nearest texel and table row (the original), 1: four taps, continuous levels
vec3 P(uint i) { return texelFetch(pal, ivec2(int(i), 0), 0).rgb; }
)";

const char* kLandFS = R"(
INTERP centroid in vec3 vPos;
INTERP centroid in vec2 vUV;
INTERP centroid in vec2 vLev;
out vec4 o;
uniform usampler2D tex;    // map.raw
uniform usampler2D shd;    // map.shd: 256 x rows
uniform usampler2D fogt;   // map.fog: 256 x rows
uniform int vertexFog;     // 1: the fog level of the vertices (vLev.y), 0: per pixel from the distance
uniform vec2 fog;          // the distance where the fog starts, levels per unit
uniform vec3 sky;          // palette entry 0xdd
uint T(ivec2 t) { return texelFetch(tex, t & 511, 0).r; }
uint S(int row, uint c) { return texelFetch(shd, ivec2(int(c), row), 0).r; }
uint F(int row, uint c) { return texelFetch(fogt, ivec2(int(c), row), 0).r; }
void main() {
    int rows = textureSize(shd, 0).y;
    float level = vertexFog != 0 ? vLev.y : max(0.0, (length(vPos) - fog.x) * fog.y);
    if (filt == 0) {   // fog[level2][shd[level][tex[u, v]]]
        uint i = S(clamp(int(floor(vLev.x)), 0, rows - 1), T(ivec2(floor(vUV))));
        int fr = clamp(int(floor(level)), 0, textureSize(fogt, 0).y - 1);
        if (fr > 0) i = F(fr, i);
        o = vec4(P(i), 1.0);
        return;
    }
    float x = clamp(vLev.x - 0.5, 0.0, float(rows - 1));
    int r0 = int(floor(x));
    int r1 = min(r0 + 1, rows - 1);
    float lf = x - float(r0);
    vec2 q = vUV - 0.5;            // texel n covers [n, n + 1)
    vec2 b = floor(q);
    vec2 f = q - b;
    ivec2 t = ivec2(b);
    vec3 c[4];
    for (int k = 0; k < 4; k++) {
        uint i = T(t + ivec2(k & 1, k >> 1));
        c[k] = mix(P(S(r0, i)), P(S(r1, i)), lf);
    }
    vec3 col = mix(mix(c[0], c[1], f.x), mix(c[2], c[3], f.x), f.y);
    o = vec4(mix(col, sky, clamp(level / 63.0, 0.0, 1.0)), 1.0);
}
)";

const char* kShipVS = R"(
layout(location = 0) in vec3 aPos;   // camera space
layout(location = 1) in vec2 aUV;    // river.raw texels
INTERP centroid out vec2 vUV;
void main() {
    gl_Position = clipOf(aPos);
    vUV = aUV;
}
)";

const char* kShipFS = R"(
INTERP centroid in vec2 vUV;
out vec4 o;
uniform usampler2D tex;    // river.raw
uint T(ivec2 t) { return texelFetch(tex, t & 255, 0).r; }
void main() {
    if (filt == 0) {
        o = vec4(P(T(ivec2(floor(vUV)))), 1.0);
        return;
    }
    vec2 q = vUV - 0.5;
    vec2 b = floor(q);
    vec2 f = q - b;
    ivec2 t = ivec2(b);
    vec3 c00 = P(T(t)), c10 = P(T(t + ivec2(1, 0))), c01 = P(T(t + ivec2(0, 1))), c11 = P(T(t + ivec2(1, 1)));
    o = vec4(mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y), 1.0);
}
)";

// A bitmap on a screen-aligned rectangle (VGA coordinates); texel coordinates at the corners.
const char* kSpriteVS = R"(
uniform vec4 rect;   // x0, y0, x1, y1
uniform vec4 uvs;    // u0, v0, u1, v1
out vec2 vT;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vec2 p = mix(rect.xy, rect.zw, c);
    vT = mix(uvs.xy, uvs.zw, c);
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";

const char* kSpriteFS = R"(
in vec2 vT;
out vec4 o;
uniform usampler2D tex;
uniform int add;     // 0: opaque (0x415dbc); 1: adds half the colour, index 0 adds nothing (flare.shd, 0x415dc8)
vec3 tap(ivec2 t) {
    ivec2 s = textureSize(tex, 0);
    if (add == 0) return P(texelFetch(tex, clamp(t, ivec2(0), s - 1), 0).r);
    if (any(lessThan(t, ivec2(0))) || any(greaterThanEqual(t, s))) return vec3(0.0);
    uint i = texelFetch(tex, t, 0).r;
    return i == 0u ? vec3(0.0) : 0.5 * P(i);
}
void main() {
    vec3 c;
    if (filt == 0) {
        c = tap(ivec2(floor(vT)));
    } else {
        vec2 q = vT - 0.5;
        vec2 b = floor(q);
        vec2 f = q - b;
        ivec2 t = ivec2(b);
        c = mix(mix(tap(t), tap(t + ivec2(1, 0)), f.x), mix(tap(t + ivec2(0, 1)), tap(t + ivec2(1, 1)), f.x), f.y);
    }
    o = vec4(c, 1.0);
}
)";

std::string interp(bool perspective) {
    return perspective ? "#define INTERP smooth\n" : "#define INTERP noperspective\n";
}

// ------------------------------------------------------------------------------------------------- the part
constexpr int kGrid = 19;    // vertices per side of the landscape grid (0x401ab8)
constexpr int kCell = 512;   // units per cell (the grid's load scale)
// The environment stage 0x41acc4: U = (2 n) . row of the object-to-camera matrix, with vertex normals of length
// 0.8 x 1024 (0x410a70), centred on half the texture (2048 of 4096); the vertex term is XS x / 16384, YS y / 16384.
constexpr double kEnvNormal = 2 * 0.8 * 1024, kEnvCentre = 2048;

struct LandVertex {
    float pos[3], uv[2], lev[2];
};
struct ShipVertex {
    float pos[3], uv[2];
};

struct Sprite {
    Texture tex;
    int w = 0, h = 0;
    double k = 0, size = 0;
};

// What the frame routine renders at a refresh.
struct View {
    V3d cam, ctl, ship[3];
    M3d camM, shipM[3];
};

class FlightScene : public Scene {
public:
    ~FlightScene() override { release(); }

    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        const sr::Fidelity& fid = sr::fidelity();
        buildField(x, H_, L_);
        mk_ = readMotion(x);
        seed_ = movRegImm(x, kSeed);
        fov_ = movMemImm(x, kFov);
        sunDx_ = immAfter(x, kSunDx, 0x2d, 1);   // sub eax, imm32
        sunY_ = movMemImm(x, kSunY);
        if (x.u8(kFlareMargin) != 0x83) throw std::runtime_error("flight: no sub eax, imm8 at 0x40257c");
        margin_ = x.s8(kFlareMargin + 2);
        utab4096_ = x.f64(kUtab4096);
        utabStep_ = x.f64(kUtabStep);
        fogStart_ = std::trunc(double(x.f32(kFogStartF)) * x.f64(kFogStartD));
        fogPerUnit_ = x.f64(kFogPerUnit) * x.f64(kFog63);
        fog16_ = x.f64(kFog16);
        sr::Scene defaults;   // 0x410878: the near plane (camera z x 128) and the sprites' far limit
        near_ = defaults.near;
        far_ = defaults.far;

        // pictures, tables, palette
        pal_ = inside::palette(d, x.cstr(kPalName));
        palTex_ = inside::uploadPalette(pal_);
        mapTex_ = inside::uploadIndices(d.file(x.cstr(kMapName)), 512, 512);
        auto table = [&](uint32_t name, Texture& t) {
            auto f = d.file(x.cstr(name));
            t = inside::uploadIndices(f, 256, int(f.size() / 256));
        };
        table(kShdName, shdTex_);
        table(kFogName, fogTex_);
        riverTex_ = inside::uploadIndices(d.file(x.cstr(kRiverName)), 256, 256);
        std::map<uint32_t, uint32_t> files = traceLoads(x);
        auto bitmap = [&](uint32_t slot, int w, int h, double k, double size) {
            Sprite s;
            if (!files.count(slot)) throw std::runtime_error("flight: no file in slot " + std::to_string(slot));
            s.tex = inside::uploadIndices(d.file(x.cstr(files[slot])), w, h);
            s.w = w;
            s.h = h;
            s.k = k;
            s.size = size;
            return s;
        };
        auto memFloat = [&](uint32_t a) { return double(asFloat(uint32_t(movMemImm(x, a)))); };
        sun_ = bitmap(immAfter(x, kSunSlot, 0xa1, 1), movMemImm(x, kSunW), movMemImm(x, kSunH), memFloat(kSunK),
                      memFloat(kSunSize));
        double flareSize = memFloat(kFlareSize);
        for (const FlareDef& f : traceFlares(x)) flares_.push_back(bitmap(f.slot, f.w, f.h, double(f.k), flareSize));

        loadShip(d.file(x.cstr(kShipName)), int32_t(movRegImm(x, kShipScale)), fid.exact);

        // GL
        std::string in = interp(fid.perspective);
        landSh_.build(in + kProjGLSL + kLandVS, in + kPalGLSL + kLandFS, "flight landscape");
        shipSh_.build(in + kProjGLSL + kShipVS, in + kPalGLSL + kShipFS, "flight ships");
        spriteSh_.build(kSpriteVS, std::string(kPalGLSL) + kSpriteFS, "flight sprites");
        gl::GenVertexArrays(1, &landVao_);
        gl::GenBuffers(1, &landVbo_);
        gl::GenBuffers(1, &landIbo_);
        gl::BindVertexArray(landVao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, landVbo_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(LandVertex) * kGrid * kGrid), nullptr, GL_STREAM_DRAW);
        const GLsizei ls = sizeof(LandVertex);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ls, (void*)offsetof(LandVertex, pos));
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ls, (void*)offsetof(LandVertex, uv));
        gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, ls, (void*)offsetof(LandVertex, lev));
        for (GLuint i = 0; i < 3; i++) gl::EnableVertexAttribArray(i);
        // 0x410f34: cell (r, c) as (r, c)(r, c + 1)(r + 1, c) and (r + 1, c)(r, c + 1)(r + 1, c + 1)
        std::vector<uint16_t> idx;
        for (int r = 0; r < kGrid - 1; r++)
            for (int c2 = 0; c2 < kGrid - 1; c2++) {
                auto v = [](int rr, int cc) { return uint16_t(kGrid * rr + cc); };
                for (uint16_t i : {v(r, c2), v(r, c2 + 1), v(r + 1, c2), v(r + 1, c2), v(r, c2 + 1), v(r + 1, c2 + 1)})
                    idx.push_back(i);
            }
        landCount_ = GLsizei(idx.size());
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, landIbo_);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(uint16_t)), idx.data(), GL_STATIC_DRAW);
        gl::GenVertexArrays(1, &shipVao_);
        gl::GenBuffers(1, &shipVbo_);
        gl::BindVertexArray(shipVao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, shipVbo_);
        const GLsizei ss = sizeof(ShipVertex);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ss, (void*)offsetof(ShipVertex, pos));
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ss, (void*)offsetof(ShipVertex, uv));
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::BindVertexArray(0);
    }

    void render(Ctx& c, double r) override {
        replay(c);
        const sr::Fidelity& fid = sr::fidelity();
        View v = viewAt(c.refreshes(r), fid.exact);
        Target& out = *c.out;
        const int w = out.w, h = out.h;
        bindTarget(w, h, fid.samples);
        gl::Viewport(0, 0, w, h);
        setBlend(Blend::None);
        gl::Disable(GL_CULL_FACE);
        gl::Disable(GL_SCISSOR_TEST);
        gl::DepthMask(GL_TRUE);
        const Vec3& sky = pal_[0xdd];   // 0x41127c(scene, 0xdd)
        gl::ClearColor(sky.x, sky.y, sky.z, 1);
        gl::ClearDepth(1.0);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const M3d Ct = v.camM.transposed();
        // the sun (key: its depth, behind everything) and the flares (key 0, in front of everything), 0x4024d0
        V3d sunW = sunAt(v.ctl, fid.exact);
        V3d sunC = Ct * (sunW - v.cam);
        if (fid.exact) sunC = truncated(sunC * 128) * (1.0 / 128);
        gl::Disable(GL_DEPTH_TEST);
        drawSprite(sun_, sunC, false, fid);
        // the landscape and the ships, depth-tested
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
        drawLand(v, Ct, fid);
        drawShips(v, Ct, fid);
        gl::Disable(GL_DEPTH_TEST);

        resolve(out);
        if (flaresVisible(sunW, v, Ct)) {   // added to the resolved frame (ctx.out clamps, as the palette does)
            out.bind();
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFunc(GL_ONE, GL_ONE);
            for (const Sprite& s : flares_) drawSprite(s, sunC, true, fid);
            setBlend(Blend::None);
        }
        out.bind();
        gl::ActiveTexture(GL_TEXTURE0);
    }

private:
    // ---- motion
    // The tick states for the cue: state n is the one after n ticks (the n-th at record start + n - 1, T = P there).
    void replay(Ctx& c) {
        int64_t T1 = int64_t(std::llround(c.partTicks(Ctx::callback(c.start))));
        double span = std::isfinite(c.end) ? std::clamp(c.end - c.start, 0.0, 20000.0) : 2000.0;
        int count = int(std::ceil(span)) + 2;
        if (!states_.empty() && start_ == c.start && T1_ == T1 && int(states_.size()) >= count) return;
        start_ = c.start;
        T1_ = T1;
        Motion m(mk_, seed_);
        states_.clear();
        states_.push_back(m.state());
        ctl0_ = m.initialControl();
        for (int n = 1; n < count; n++) {
            m.tick(T1 + 4 * int64_t(n - 1));
            states_.push_back(m.state());
        }
    }

    View viewAt(double x, bool exact) const {
        x = std::clamp(x, 0.0, double(states_.size() - 2));
        int n = int(std::floor(x));
        double f = exact ? 0.0 : x - n;
        const TickState& a = states_[size_t(n)];
        const TickState& b = states_[size_t(n) + 1];
        View v;
        for (int i = 0; i < 3; i++) {
            v.ship[i] = lerp(a.ship[i], b.ship[i], f);
            v.shipM[i] = rotY(a.shipM[i], b.yaw[i] * f);
        }
        v.ctl = n == 0 ? lerp(ctl0_, b.ship[0], f) : v.ship[0];
        v.cam = lerp(a.cam, b.camPre, f);
        // the matrix of the last tick; between ticks rebuilt from the blended look vector, except before the first
        // tick (the init's identity) and in the refresh after a jump (the old matrix at the new position)
        if (n == 0 || a.jumped || f == 0) {
            v.camM = a.camM;
        } else {
            v.camM = rotX(rotZ(look(lerp(a.lookDir, b.lookDir, f)), mk_.roll), mk_.pitch);
        }
        if (exact) {   // 0x40276c: the objects get the positions as ints
            v.cam = truncated(v.cam);
            v.ctl = truncated(v.ctl);
            for (V3d& p : v.ship) p = truncated(p);
        }
        return v;
    }

    // 0x40276c: the sun at (ctl.x - dx, y, ctl.z)
    V3d sunAt(const V3d& ctl, bool exact) const {
        V3d c = exact ? truncated(ctl) : ctl;
        return {c.x - double(sunDx_), double(sunY_), c.z};
    }

    // 0x4024d0 with 0x411e80: the sun projected in 3D, inside the window widened by the margin
    bool flaresVisible(const V3d& sunW, const View& v, const M3d& Ct) const {
        V3d p = Ct * (sunW - v.cam);
        if (near_ / 256 > p.z) return false;
        double sx = 160 + xs() * p.x / (128 * p.z), sy = 100 + ys() * p.y / (128 * p.z);
        return !(sx < -margin_ || sx >= 320 + margin_ || sy < -margin_ || sy >= 200 + margin_);
    }

    // the projection scales of 0x40c4e4 (XS, YS: 128 x fov W / 320, 128 x fov H / 240 for the 320 x 200 window)
    double xs() const { return 128.0 * fov_ * 320 / 320; }
    double ys() const { return sr::fidelity().exact ? std::trunc(128.0 * fov_ * 200 / 240) : 128.0 * fov_ * 200 / 240; }

    // ---- the ship (0x4018d0: ship.sro through 0x410bb0 with the scale; normals 0x410918, 0x410a70)
    void loadShip(std::span<const uint8_t> sro, int32_t scale, bool exact) {
        auto u32 = [&](size_t o) {
            if (o + 4 > sro.size()) throw std::runtime_error("flight: ship.sro too short");
            uint32_t v;
            std::memcpy(&v, sro.data() + o, 4);
            return v;
        };
        auto s16 = [&](size_t o) {
            if (o + 2 > sro.size()) throw std::runtime_error("flight: ship.sro too short");
            int16_t v;
            std::memcpy(&v, sro.data() + o, 2);
            return v;
        };
        size_t vo = u32(4), nv = u32(8), nf = u32(0x10), fo = vo + 12 * nv;
        shipP_.resize(nv);
        for (size_t i = 0; i < nv; i++) {
            double p[3];
            for (int k = 0; k < 3; k++) {
                p[k] = double(s16(vo + 12 * i + 2 * size_t(k))) * scale / 65536.0;
                if (exact) p[k] = std::trunc(p[k]);
            }
            shipP_[i] = {p[0], p[1], p[2]};
        }
        shipF_.resize(nf);
        shipFN_.resize(nf);
        shipVN_.assign(nv, V3d{});
        for (size_t f = 0; f < nf; f++) {
            for (int k = 0; k < 3; k++) {
                int v = s16(fo + 8 * f + 2 + 2 * size_t(k));
                if (v < 0 || size_t(v) >= nv) throw std::runtime_error("flight: bad face in ship.sro");
                shipF_[f][size_t(k)] = v;
            }
            V3d a = shipP_[size_t(shipF_[f][0])], b = shipP_[size_t(shipF_[f][1])], cc = shipP_[size_t(shipF_[f][2])];
            V3d e1 = b - a, e2 = cc - a;
            V3d n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            double l = length(n);
            n = l > 1e-12 ? n * (1.0 / l) : V3d{};
            shipFN_[f] = n;
            for (int k = 0; k < 3; k++) {
                V3d& vn = shipVN_[size_t(shipF_[f][size_t(k)])];
                vn = vn + n;
            }
        }
        for (V3d& n : shipVN_) {
            double l = length(n);
            n = l > 1e-12 ? n * (1.0 / l) : V3d{};
        }
    }

    // ---- GL
    void bindTarget(int w, int h, int samples) {
        if (maxSamples_ < 0) {
            GLint m = 0;
            gl::GetIntegerv(GL_MAX_SAMPLES, &m);
            maxSamples_ = m;
        }
        samples = std::min(samples, maxSamples_);
        if (samples <= 1) samples = 0;
        if (!fbo_ || fboW_ != w || fboH_ != h || fboSamples_ != samples) {
            if (!fbo_) {
                gl::GenFramebuffers(1, &fbo_);
                gl::GenRenderbuffers(1, &fboColor_);
                gl::GenRenderbuffers(1, &fboDepth_);
            }
            auto storage = [&](GLuint rb, GLenum fmt) {
                gl::BindRenderbuffer(GL_RENDERBUFFER, rb);
                if (samples) gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, w, h);
                else gl::RenderbufferStorage(GL_RENDERBUFFER, fmt, w, h);
            };
            storage(fboColor_, GL_RGBA16);   // as ctx.out (a multisample resolve needs the same format)
            storage(fboDepth_, GL_DEPTH24_STENCIL8);
            gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
            gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fboColor_);
            gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fboDepth_);
            if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("flight: render target incomplete");
            fboW_ = w;
            fboH_ = h;
            fboSamples_ = samples;
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }

    void resolve(const Target& dst) {
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);
        gl::BlitFramebuffer(0, 0, fboW_, fboH_, 0, 0, dst.w, dst.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::BindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    }

    void setProjection(const Shader& s) const {
        // VGA = the engine's screen moved by the rasterizers' sampling point (pixel c, r shows (c + 1, r + 15/16))
        s.set("proj", float(xs() / 128), float(ys() / 128), 160.0f - 0.5f, 100.0f - 0.4375f);
        double n = near_ / 128, f = 1 << 20;   // the near plane of 0x41c0e0; nothing is farther than the far value
        s.set("depthAB", float((f + n) / (f - n)), float(-2 * f * n / (f - n)));
    }

    void bindPalette(const Shader& s, const sr::Fidelity& fid) const {
        palTex_.bind(3);
        s.set("pal", 3);
        s.set("filt", fid.filter ? 1 : 0);
    }

    // 0x401ab8 (the grid around the camera's cell) and 0x401c64 (its fog), drawn by 0x412264 / 0x414e5c
    void drawLand(const View& v, const M3d& Ct, const sr::Fidelity& fid) {
        V3d camI = truncated(v.cam);   // the camera object's position (ints)
        int cx = int(camI.x) / kCell, cz = int(camI.z) / kCell;
        int hx = pmod(cx, kField), hz = pmod(cz, kField), ux = pmod(cx, 18), uz = pmod(cz, 18);
        // 0x401c64: the camera in the grid object's space, as floats
        float fx = float(int(camI.x) % kCell), fy = float(camI.y), fz = float(int(camI.z) % kCell);
        LandVertex lv[kGrid * kGrid];
        for (int r = 0; r < kGrid; r++)
            for (int c = 0; c < kGrid; c++) {
                size_t e = size_t((hz + r) & (kField - 1)) * kField + size_t((hx + c) & (kField - 1));
                double y = H_[e];
                V3d world{double(kCell * (cx + c - 9)), y, double(kCell * (cz + r - 9))};
                V3d p = Ct * (world - v.cam);
                LandVertex& o = lv[kGrid * r + c];
                o.pos[0] = float(p.x);
                o.pos[1] = float(p.y);
                o.pos[2] = float(p.z);
                // u, v: 4096 = the texture (512 texels), repeating every 18 cells; the rasterizer's +1
                double u = fid.exact ? std::trunc((ux + c) * utab4096_ * utabStep_) : (ux + c) * utab4096_ * utabStep_;
                double vv = fid.exact ? std::trunc((uz + r) * utab4096_ * utabStep_) : (uz + r) * utab4096_ * utabStep_;
                o.uv[0] = float((u + 1) / 8);
                o.uv[1] = float((vv + 1) / 8);
                o.lev[0] = float((L_[e] + 1) / 16.0);
                double fog = 0;
                if (fid.exact) {
                    double dx = double(fx) - (c - 9) * kCell, dy = double(fy) - y, dz = double(fz) - (r - 9) * kCell;
                    double ed = std::sqrt((dx * dx + dy * dy) + dz * dz) - fogStart_;
                    int fl = ed >= 0 ? int16_t(int32_t(ed * fogPerUnit_ * fog16_)) : 0;
                    fog = std::clamp(fl, 0, 0x3f0);
                }
                o.lev[1] = float((fog + 1) / 16);
            }
        landSh_.use();
        setProjection(landSh_);
        bindPalette(landSh_, fid);
        mapTex_.bind(0);
        shdTex_.bind(1);
        fogTex_.bind(2);
        landSh_.set("tex", 0);
        landSh_.set("shd", 1);
        landSh_.set("fogt", 2);
        landSh_.set("vertexFog", fid.exact ? 1 : 0);
        landSh_.set("fog", float(fogStart_), float(fogPerUnit_));
        landSh_.set("sky", pal_[0xdd]);
        gl::BindVertexArray(landVao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, landVbo_);
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(sizeof(lv)), lv);
        gl::DrawElements(GL_TRIANGLES, landCount_, GL_UNSIGNED_SHORT, nullptr);
        gl::BindVertexArray(0);
    }

    // The ships: back faces culled in object space (0x41a458), u, v of 0x41acc4, river.raw by 0x412db0.
    void drawShips(const View& v, const M3d& Ct, const sr::Fidelity& fid) {
        shipVerts_.clear();
        std::vector<ShipVertex> pv(shipP_.size());
        for (int s = 0; s < 3; s++) {
            const M3d& R = v.shipM[s];
            const M3d M = Ct * R;   // object to camera
            V3d eye = R.transposed() * (v.cam - v.ship[s]);
            V3d T = Ct * (v.ship[s] - v.cam);
            for (size_t i = 0; i < shipP_.size(); i++) {
                V3d p = M * shipP_[i] + T;
                V3d n = M * shipVN_[i];
                double U = kEnvNormal * n.x, V = kEnvNormal * n.y, u, vv;
                if (fid.exact) {
                    double px = double(tdiv(int64_t(std::trunc(8 * xs() * p.x)), 2048));
                    double py = double(tdiv(int64_t(std::trunc(8 * ys() * p.y)), 2048));
                    u = double(int16_t(-(int64_t(std::trunc(U + kEnvCentre)) & 0xfff) - tdiv(int64_t(px), 64)));
                    vv = double(int16_t(tdiv(int64_t(py), 64) + (int64_t(std::trunc(V + kEnvCentre)) & 0xfff)));
                } else {
                    u = -(U + kEnvCentre) - xs() * p.x / 16384;
                    vv = (V + kEnvCentre) + ys() * p.y / 16384;
                }
                ShipVertex& o = pv[i];
                o.pos[0] = float(p.x);
                o.pos[1] = float(p.y);
                o.pos[2] = float(p.z);
                o.uv[0] = float((u + 1) / 16);   // 16 units per texel of the 256 x 256 map
                o.uv[1] = float((vv + 1) / 16);
            }
            for (size_t f = 0; f < shipF_.size(); f++) {
                const V3d& p0 = shipP_[size_t(shipF_[f][0])];
                const V3d& n = shipFN_[f];
                if (((eye.y - p0.y) * n.y + (eye.x - p0.x) * n.x) + (eye.z - p0.z) * n.z < 0) continue;
                for (int k = 0; k < 3; k++) shipVerts_.push_back(pv[size_t(shipF_[f][size_t(k)])]);
            }
        }
        if (shipVerts_.empty()) return;
        shipSh_.use();
        setProjection(shipSh_);
        bindPalette(shipSh_, fid);
        riverTex_.bind(0);
        shipSh_.set("tex", 0);
        gl::BindVertexArray(shipVao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, shipVbo_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(shipVerts_.size() * sizeof(ShipVertex)), shipVerts_.data(),
                       GL_STREAM_DRAW);
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(shipVerts_.size()));
        gl::BindVertexArray(0);
    }

    // 0x415878: the bitmap centred at (cx + XS k x / 1024 / z, cy + XS k y / 1024 / z) (both with XS: the vertical
    // offset 1.2 times too large), w x size x XS / z by h x size x YS / z, in 1/16 pixels; pixel c samples at the
    // engine's c + 15/16. Flares extend by a texel to fade out their edges.
    void drawSprite(const Sprite& s, const V3d& p, bool add, const sr::Fidelity& fid) {
        double z = p.z;
        if (std::trunc(near_ / 128) > z || far_ < z) return;
        double X = s.k * (128 * p.x) * (xs() / 1024) / z, Y = s.k * (128 * p.y) * (xs() / 1024) / z;
        double W = s.w * s.size * xs() / z, Hh = s.h * s.size * ys() / z;
        if (!(W > 0) || !(Hh > 0)) return;
        double x0 = 160 * 16 + X - W * 0.5, y0 = 100 * 16 + Y - Hh * 0.5;
        if (fid.exact) {
            x0 = std::trunc(x0);
            y0 = std::trunc(y0);
            W = std::trunc(W);
            Hh = std::trunc(Hh);
        }
        double qx0 = (x0 - 7) / 16, qx1 = (x0 + W - 7) / 16, qy0 = (y0 - 7) / 16, qy1 = (y0 + Hh - 7) / 16;
        double u0 = 0, v0 = 0, u1 = s.w, v1 = s.h;
        if (add && fid.filter) {   // one more texel around (transparent)
            double px = (qx1 - qx0) / s.w, py = (qy1 - qy0) / s.h;
            qx0 -= px;
            qx1 += px;
            qy0 -= py;
            qy1 += py;
            u0 = v0 = -1;
            u1 += 1;
            v1 += 1;
        }
        spriteSh_.use();
        bindPalette(spriteSh_, fid);
        s.tex.bind(0);
        spriteSh_.set("tex", 0);
        spriteSh_.set("add", add ? 1 : 0);
        spriteSh_.set("rect", float(qx0), float(qy0), float(qx1), float(qy1));
        spriteSh_.set("uvs", float(u0), float(v0), float(u1), float(v1));
        drawQuad();
    }

    void release() {
        if (landVao_) gl::DeleteVertexArrays(1, &landVao_);
        if (shipVao_) gl::DeleteVertexArrays(1, &shipVao_);
        GLuint bufs[3] = {landVbo_, landIbo_, shipVbo_};
        for (GLuint b : bufs)
            if (b) gl::DeleteBuffers(1, &b);
        if (fbo_) gl::DeleteFramebuffers(1, &fbo_);
        if (fboColor_) gl::DeleteRenderbuffers(1, &fboColor_);
        if (fboDepth_) gl::DeleteRenderbuffers(1, &fboDepth_);
        for (Texture* t : {&palTex_, &mapTex_, &shdTex_, &fogTex_, &riverTex_, &sun_.tex}) t->destroy();
        for (Sprite& s : flares_) s.tex.destroy();
        landVao_ = shipVao_ = landVbo_ = landIbo_ = shipVbo_ = fbo_ = fboColor_ = fboDepth_ = 0;
    }

    // data
    std::vector<int32_t> H_, L_;   // 128 x 128: heights (y down) and light x 16
    MotionConsts mk_{};
    uint32_t seed_ = 0;
    double fov_ = 0, near_ = 0, far_ = 0, utab4096_ = 0, utabStep_ = 0, fogStart_ = 0, fogPerUnit_ = 0, fog16_ = 0;
    int32_t sunDx_ = 0, sunY_ = 0;
    int margin_ = 0;
    std::array<Vec3, 256> pal_{};
    std::vector<V3d> shipP_, shipFN_, shipVN_;
    std::vector<std::array<int, 3>> shipF_;
    Sprite sun_;
    std::vector<Sprite> flares_;
    // motion
    std::vector<TickState> states_;
    V3d ctl0_;
    double start_ = 0;
    int64_t T1_ = 0;
    // GL
    Texture palTex_, mapTex_, shdTex_, fogTex_, riverTex_;
    Shader landSh_, shipSh_, spriteSh_;
    GLuint landVao_ = 0, landVbo_ = 0, landIbo_ = 0, shipVao_ = 0, shipVbo_ = 0;
    GLsizei landCount_ = 0;
    std::vector<ShipVertex> shipVerts_;
    GLuint fbo_ = 0, fboColor_ = 0, fboDepth_ = 0;
    int fboW_ = 0, fboH_ = 0, fboSamples_ = -1, maxSamples_ = -1;
};

}  // namespace

REGISTER_SCENE(FlightScene, "flight");
