// The table-driven "bitmap effects" (vividexp/re/notes-bitmap.md): part I's three "1x1 Efekti" sections (a 256 x 256
// texture read through two polar per-pixel tables, kaleidoscopic stars and swirls, in 1, 2 or 4 x 4 tiles, with a
// photo of a woman over them), part I's 2 x 2 effect (three planes summed into a glow ramp: a dotted floor lit by a
// swirl) and part II's four sections (two swirling planes blended half and half).
// Ports InitBITMAPEFEK 0x400ae4 (with MakePrec 0x4007fc), RunBITMAPEFEK 0x401270 (with ReadPrm 0x401024 and the step
// 0x40c22c), ClearEFEKPARAMS 0x401588 / 0x40c284 and the drawers 0x40cab5, 0x40cb33 (one plane, photo), 0x40cbe7
// (two planes through DTABLE) and 0x40c3ad / 0x40c455 / 0x40c543 / 0x40c626 / 0x40c74f / 0x40c86d (2 x 2, 2..4
// planes, with and without the photo).
//
// Every plane is a 256 x 256 texture read at (column u(x, y) + Ox, row v(x, y) + Oy): u and v are per-pixel tables,
// (Ox, Oy) the plane's offset, which moves by the .prm speeds once per refresh while a Run line runs and carries over
// from run to run and section to section (only ClearEFEKPARAMS zeroes it). The segment's Init line names the .ini
// (planes, tables, pictures), its Run line the .prm; the files are read from VIVID.EXE, the offsets replayed from the
// script's lines, so any refresh can be drawn in any order.
//
// What is evaluated exactly instead of the original's shortcuts:
//   - the tables that follow a formula (all .1x1 tables but tab14: notes 6.1, 6.2) are evaluated per output pixel at
//     the continuous position, in float, without the truncation to bytes: v = ka a + kr r + wa sin(na a) +
//     wr sin(nr r) about the table's centre, with its tiling and mirroring (the seams stay where they are, at pixel
//     boundaries, the mirrored tiles keep their 1-pixel asymmetry). Which formula a table is is found by comparing
//     the table read from VIVID.EXE with the candidates (the source holds the formulas, not the file names);
//   - the tables without an exact formula (tab14.1x1, the .2x2 tables, which carry noise) are read from the file and
//     interpolated with a cubic B-spline through their entries (one entry per pixel or per 2 x 2 block), wrap-aware
//     (the entries are mod 256), plus 0.5 for the generator's truncation: smooth, without blocks, and within a
//     fraction of a texel of the table;
//   - the offsets move continuously with the fractional refresh instead of in whole texels (the drawers use only the
//     offsets' high bytes), and the carry from the column into the row of the 16-bit address add is dropped;
//   - the textures are filtered in colour: each texel's index is looked up in the palette first (the DAC's for part I,
//     each texture's own palette for part II); where a texel covers more than an output pixel the colours are read
//     with Catmull-Rom (smooth, without the texel steps, keeping the texels' contrast), where it covers less they
//     are read trilinearly and anisotropically with the analytic gradient of the coordinates (so the angle maps'
//     branch cut and the tile seams do not blur the mip level, and the polar centres, where the original aliases,
//     average out);
//   - part II's DTABLE, a dithered 256-colour approximation of the 50 % blend of the two textures' colours (notes
//     7.3), is drawn as that blend;
//   - the 2 x 2 effect's index sum is the sum of the planes' filtered index values, looked up in the ramp palette
//     between neighbouring entries, per output pixel (no 2 x 2 blocks);
//   - the photo (original pixels: a photo of a real person, not redrawn) is filtered bilinearly in colour through the
//     palette from its opaque pixels only; its edge is the 0.5 contour of the B-spline-smoothed mask, anti-aliased
//     over one output pixel, instead of the pixel stairs.
// Kept: the table formulas, centres and tilings, the speeds with RunBITMAPEFEK's y quirk (a negative x speed borrows
// 1 from the y speed, notes 4.2), the offsets carried across runs and sections (part II starts where the 2 x 2
// ended), the refresh part II loses after each Init (4.3), the photo's place and mask, the textures' texels.
// Dropped: the tearing (the drawers write to VGA memory while it is displayed), the previous frame under the first
// frames of a section.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "demo.h"
#include "exe.h"

namespace {

// The script's command names, as the interpreter 0x40b760 compares them.
constexpr uint32_t kCmdInit = 0x8210fe;       // "InitBITMAPEFEK"
constexpr uint32_t kCmdRun = 0x82112d;        // "RunBITMAPEFEK"
constexpr uint32_t kCmdClear = 0x82113b;      // "ClearEFEKPARAMS"

constexpr int kTexSize = 256;                 // the planes: 256 x 256 indices (PICRGB + 0x10000 k)
constexpr uint32_t kPhotoSub = 0x3f3f3f40;    // 0x400f79: every dword of TAUSTA minus this (indices 0..63 -> 192..255)

// ---------------------------------------------------------------- the .ini and .prm files

// InitBITMAPEFEK's sscanf (format at 0x82025c): TYPE PLANES BPIC x, then BPICNAME DTABNAME x x P1..P4 T1..T7; "0" is
// no file. T8 is never read.
struct Ini {
    int type = 0, planes = 0, bpic = 0;
    std::string bpicName, dtabName;
    std::array<std::string, 4> pics;
    std::array<std::string, 8> tabs;
};

Ini parseIni(std::span<const uint8_t> f) {
    std::istringstream in(std::string(f.begin(), f.end()));
    std::vector<std::string> w;
    std::string s;
    while (w.size() < 19 && in >> s) w.push_back(s);
    w.resize(19);
    Ini ini;
    ini.type = std::atoi(w[0].c_str());
    ini.planes = std::atoi(w[1].c_str());
    ini.bpic = std::atoi(w[2].c_str());
    ini.bpicName = w[4];
    ini.dtabName = w[5];
    for (int k = 0; k < 4; ++k) ini.pics[size_t(k)] = w[size_t(8 + k)];
    for (int k = 0; k < 7; ++k) ini.tabs[size_t(k)] = w[size_t(12 + k)];
    return ini;
}

bool named(const std::string& n) { return !n.empty() && n != "0"; }

// ReadPrm 0x401024: eight words (short)trunc(atof(s) * 256) (__CHP 0x41130c truncates), 1/256 texel per refresh.
std::array<int, 8> readPrm(std::span<const uint8_t> f) {
    std::istringstream in(std::string(f.begin(), f.end()));
    std::array<int, 8> sp{};
    std::string s;
    for (int i = 0; i < 8 && in >> s; ++i) sp[size_t(i)] = int(int16_t(int(std::trunc(std::atof(s.c_str()) * 256.0))));
    return sp;
}

// The speeds the step 0x40c22c adds per refresh (Ox1 Oy1 .. Ox4 Oy4), as RunBITMAPEFEK packs them: argument k is
// (S[2k+1] << 16) + S[2k] with S[2k] sign-extended, so a negative x speed takes 1 from the y speed; plane 4 is
// stepped with plane 3's argument.
std::array<double, 8> stepSpeeds(const std::array<int, 8>& sp) {
    std::array<double, 8> v{};
    for (int k = 0; k < 4; ++k) {
        int j = std::min(k, 2);
        int sx = sp[size_t(2 * j)], sy = sp[size_t(2 * j + 1)];
        v[size_t(2 * k)] = sx;
        v[size_t(2 * k + 1)] = sy - (sx < 0 ? 1 : 0);
    }
    return v;
}

// ---------------------------------------------------------------- the table formulas (notes-bitmap.md 6)

// A texture coordinate as a function of the original pixel (x, y): the table generator's
//   v = ka a + kr r + wa sin(na a) + wr sin(nr r),   r, a = distance and angle (radians, y down) about (cx, cy),
// evaluated at (nx xl, ny yl) where (xl, yl) is the position in its tile (nx x ny tiles of 320 / nx x 200 / ny
// pixels; with `mirror` the second tile column and row are mirrored: xl -> tile width - 1 - xl), after mirroring the
// whole table (flip bit 0: x -> 319 - x, bit 1: y -> 199 - y).
struct Formula {
    float cx = 160, cy = 100;
    int nx = 1, ny = 1;
    bool mirror = false;
    int flip = 0;
    float ka = 0, kr = 0, wa = 0, na = 0, wr = 0, nr = 0;

    double at(int x, int y) const {
        double qx = (flip & 1) ? 319 - x : x, qy = (flip & 2) ? 199 - y : y;
        const double tw = 320.0 / nx, th = 200.0 / ny;
        int kx = std::clamp(int(std::floor((qx + 0.5) / tw)), 0, nx - 1);
        int ky = std::clamp(int(std::floor((qy + 0.5) / th)), 0, ny - 1);
        double lx = qx - kx * tw, ly = qy - ky * th;
        if (mirror && kx == 1) lx = tw - 1 - lx;
        if (mirror && ky == 1) ly = th - 1 - ly;
        double dx = nx * lx - cx, dy = ny * ly - cy;
        // the generator's r and a were float variables (notes 6: only matters for byte-exact regeneration)
        double r = float(std::sqrt(dx * dx + dy * dy)), a = float(std::atan2(dy, dx));
        return ka * a + kr * r + wa * std::sin(na * a) + wr * std::sin(nr * r);
    }
};

// The candidates: the base maps (notes 6.1, our fits of the tables) in every tiling and mirroring the tables use.
std::vector<Formula> candidates() {
    const double pi = 3.14159265358979323846;
    const float turn = float(128.0 / pi);   // angle maps: 256 texels per turn
    std::vector<Formula> bases = {
        {160, 100, 1, 1, false, 0, 0, 1, 0, 0, 0, 0},                       // distance
        {160, 100, 1, 1, false, 0, turn, 0, 0, 0, 41, float(pi / 128)},     // angle twisted by distance
        {160, 100, 1, 1, false, 0, 0, 1, 4, 40, 0, 0},                      // distance, 40 small waves
        {160, 100, 1, 1, false, 0, 0, 1, 52, 10, 0, 0},                     // distance, 10 large waves
        {220, 130, 1, 1, false, 0, turn, 0, 0, 0, -15, float(pi / 12.8)},   // angle, fast radial ripple
    };
    struct Tiling { int nx, ny; bool mirror; };
    const Tiling tilings[] = {{1, 1, false}, {1, 2, false}, {1, 2, true}, {2, 2, false}, {2, 2, true}, {4, 4, false}};
    std::vector<Formula> out;
    for (const Formula& b : bases)
        for (const Tiling& t : tilings)
            for (int flip = 0; flip < 4; ++flip) {
                Formula f = b;
                f.nx = t.nx;
                f.ny = t.ny;
                f.mirror = t.mirror;
                f.flip = flip;
                out.push_back(f);
            }
    return out;
}

// The formula that regenerates a 320 x 200 table (the generator stored (int)v & 255): every compared byte within
// +-1 and at most 2 % off by one (rounding at the truncation steps). False if none does.
bool matchFormula(std::span<const uint8_t> t, Formula* out) {
    static const std::vector<Formula> cand = candidates();
    constexpr int kStride = 3;   // every third pixel
    constexpr int kSamples = (64000 + kStride - 1) / kStride;
    int best = kSamples / 50 + 1;
    bool found = false;
    for (const Formula& f : cand) {
        int off = 0;
        bool ok = true;
        for (int i = 0; i < 64000 && ok; i += kStride) {
            double v = f.at(i % 320, i / 320);
            int b = int(int64_t(std::trunc(v)) & 255);
            int e = ((b - int(t[size_t(i)]) + 128) & 255) - 128;
            if (e != 0) {
                ok = std::abs(e) <= 1 && ++off < best;
            }
        }
        if (ok) {
            best = off;
            *out = f;
            found = true;
        }
    }
    return found;
}

// ---------------------------------------------------------------- shaders

// Palette pass: a plane's indices through a palette, into a mipmapped colour texture.
const char* kColourFS = R"(
out vec4 o;
uniform usampler2D idx;
uniform sampler2D pal;
void main() { o = vec4(palColor(pal, texelFetch(idx, ivec2(gl_FragCoord.xy), 0).r), 1.0); }
)";

const char* kBitmapFS = R"(
in vec2 uv;
out vec4 o;
uniform int mode;          // 0: plane 1 in colour; 1: planes 1 and 2 half and half; 2: the planes' sum into the palette
uniform int planes;
uniform vec2 pix;          // one output pixel in original pixels
uniform vec4 off01, off23; // the planes' offsets in texels: Ox1 Oy1 Ox2 Oy2, Ox3 Oy3 Ox4 Oy4
uniform sampler2D plane0, plane1, plane2, plane3;   // colour (modes 0, 1) or index values (mode 2), 256 x 256
// the eight coordinates (2k: column of plane k, 2k + 1: its row): a formula (src < 0) or table slot src
uniform int src[8];
uniform vec4 geo[8];       // centre x, y; tiles across, down
uniform vec4 til[8];       // mirror, flip bits
uniform vec4 fnA[8];       // ka, kr, wa, na
uniform vec4 fnB[8];       // wr, nr
uniform int tables;        // table slots in use
uniform usampler2D tab0, tab1, tab2, tab3;
uniform vec4 tabBlock;     // original pixels per entry of slots 0..3 (1: .1x1, 2: .2x2)
uniform sampler2D pal;     // the DAC
uniform usampler2D photo;  // TAUSTA: 320 x 200, 0xc0 = the effect shows
uniform int photoOn;

// The formula of coordinate i at p (original pixel units, pixel x's centre at x): value and gradient.
vec3 formulaAt(int i, vec2 p) {
    vec4 g = geo[i], t = til[i], fa = fnA[i];
    vec2 fb = fnB[i].xy;
    int flip = int(t.y);
    vec2 q = p, s = vec2(1.0);
    if ((flip & 1) != 0) { q.x = 319.0 - q.x; s.x = -1.0; }
    if ((flip & 2) != 0) { q.y = 199.0 - q.y; s.y = -1.0; }
    vec2 n = g.zw, tw = vec2(320.0, 200.0) / n;
    vec2 k = clamp(floor((q + 0.5) / tw), vec2(0.0), n - 1.0);
    vec2 l = q - k * tw;
    if (t.x > 0.5) {
        if (k.x == 1.0) { l.x = tw.x - 1.0 - l.x; s.x = -s.x; }
        if (k.y == 1.0) { l.y = tw.y - 1.0 - l.y; s.y = -s.y; }
    }
    vec2 d = n * l - g.xy;
    s *= n;
    float r = length(d);
    if (r < 1e-4) { d = vec2(1e-4, 0.0); r = 1e-4; }
    float a = atan(d.y, d.x);
    vec2 gr = d / r, ga = vec2(-d.y, d.x) / (r * r);
    float v = fa.x * a + fa.y * r + fa.z * sin(fa.w * a) + fb.x * sin(fb.y * r);
    vec2 gv = (fa.x + fa.z * fa.w * cos(fa.w * a)) * ga + (fa.y + fb.x * fb.y * cos(fb.y * r)) * gr;
    return vec3(v, gv * s);
}

vec4 bspline(float f) {
    float g = 1.0 - f, f2 = f * f, f3 = f2 * f;
    return vec4(g * g * g, 3.0 * f3 - 6.0 * f2 + 4.0, -3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0, f3) / 6.0;
}
vec4 bsplineD(float f) {
    float g = 1.0 - f;
    return vec4(-3.0 * g * g, 9.0 * f * f - 12.0 * f, -9.0 * f * f + 6.0 * f + 3.0, 3.0 * f * f) / 6.0;
}

// A table at p: cubic B-spline through its entries (entry i's centre at pixel block * i + (block - 1) / 2), the
// entries taken mod 256 relative to the nearest one, + 0.5 for the generator's truncation. Value and gradient.
vec3 tableAt(usampler2D t, float block, vec2 p) {
    ivec2 sz = textureSize(t, 0);
    vec2 q = (p + 0.5) / block - 0.5;
    vec2 fl = floor(q), f = q - fl;
    ivec2 b = ivec2(fl);
    float ref = float(texelFetch(t, clamp(ivec2(floor(q + 0.5)), ivec2(0), sz - 1), 0).r);
    vec4 wx = bspline(f.x), wy = bspline(f.y), dx = bsplineD(f.x), dy = bsplineD(f.y);
    vec3 acc = vec3(0.0);
    for (int j = 0; j < 4; ++j) {
        vec4 row;
        for (int i = 0; i < 4; ++i) {
            float d = float(texelFetch(t, clamp(b + ivec2(i - 1, j - 1), ivec2(0), sz - 1), 0).r) - ref;
            row[i] = d - 256.0 * floor(d / 256.0 + 0.5);
        }
        acc += vec3(wy[j] * dot(wx, row), wy[j] * dot(dx, row), dy[j] * dot(wx, row));
    }
    return vec3(ref + acc.x + 0.5, acc.yz / block);
}

vec3 coordAt(int i, vec2 p, vec3 tv[4]) { return src[i] < 0 ? formulaAt(i, p) : tv[src[i]]; }

// Catmull-Rom through the texels around texel coordinate c (texel n covers [n, n + 1)), from nine bilinear reads of
// the base level: sharp where the texture is magnified, without the texel steps.
vec4 catmullRom(sampler2D tex, vec2 c) {
    vec2 t1 = floor(c - 0.5) + 0.5, f = c - t1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 a = (t1 - 1.0) / 256.0, m = (t1 + w2 / w12) / 256.0, b = (t1 + 2.0) / 256.0;
    return (textureLod(tex, vec2(a.x, a.y), 0.0) * w0.x + textureLod(tex, vec2(m.x, a.y), 0.0) * w12.x +
            textureLod(tex, vec2(b.x, a.y), 0.0) * w3.x) * w0.y +
           (textureLod(tex, vec2(a.x, m.y), 0.0) * w0.x + textureLod(tex, vec2(m.x, m.y), 0.0) * w12.x +
            textureLod(tex, vec2(b.x, m.y), 0.0) * w3.x) * w12.y +
           (textureLod(tex, vec2(a.x, b.y), 0.0) * w0.x + textureLod(tex, vec2(m.x, b.y), 0.0) * w12.x +
            textureLod(tex, vec2(b.x, b.y), 0.0) * w3.x) * w3.y;
}

// Plane k's texture at p: (column, row) + offset. Magnified (under one texel per output pixel): Catmull-Rom;
// minified (two or more): trilinear and anisotropic with the coordinates' gradient; blended in between.
vec4 planeAt(sampler2D tex, int k, vec2 off, vec2 p, vec3 tv[4]) {
    vec3 u = coordAt(2 * k, p, tv), v = coordAt(2 * k + 1, p, tv);
    vec2 c = mod(vec2(u.x, v.x) + off, 256.0);
    vec2 gx = vec2(u.y, v.y) * pix.x, gy = vec2(u.z, v.z) * pix.y;   // texels per output pixel
    float m = clamp(max(length(gx), length(gy)) - 1.0, 0.0, 1.0);
    vec4 sharp = m < 1.0 ? max(catmullRom(tex, c), 0.0) : vec4(0.0);
    vec4 soft = m > 0.0 ? textureGrad(tex, c / 256.0, gx / 256.0, gy / 256.0) : vec4(0.0);
    return mix(sharp, soft, m);
}

uint photoAt(ivec2 t) { return texelFetch(photo, clamp(t, ivec2(0), ivec2(319, 199)), 0).r; }

// The photo over the effect at p. Its edge is where the cubic B-spline of its mask (1 at the pixels that are not
// 0xc0) crosses 0.5, anti-aliased over one output pixel: a smooth contour instead of the pixel stairs (the masks
// have no isolated pixels, so nothing is lost). Its colour is bilinear in colour between its opaque pixels.
vec3 overPhoto(vec3 col, vec2 p) {
    vec2 fl = floor(p), f = p - fl;
    ivec2 b = ivec2(fl);
    vec4 wx = bspline(f.x), wy = bspline(f.y), dx = bsplineD(f.x), dy = bsplineD(f.y);
    vec3 mask = vec3(0.0);          // value, d/dx, d/dy
    vec4 near = vec4(0.0);          // the 2 x 2 taps around p: bilinear, opaque only
    vec4 far = vec4(0.0);           // all 4 x 4: B-spline, opaque only (where the 2 x 2 has none)
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            uint t = photoAt(b + ivec2(i - 1, j - 1));
            if (t == 0xc0u) continue;
            mask += vec3(wx[i] * wy[j], dx[i] * wy[j], wx[i] * dy[j]);
            vec3 c = palColor(pal, t);
            far += vec4(c, 1.0) * (wx[i] * wy[j]);
            if (i >= 1 && i <= 2 && j >= 1 && j <= 2) {
                float w = (i == 1 ? 1.0 - f.x : f.x) * (j == 1 ? 1.0 - f.y : f.y);
                near += vec4(c, 1.0) * w;
            }
        }
    if (mask.x <= 0.0) return col;
    vec3 pc = near.w > 0.0 ? near.rgb / near.w : far.rgb / max(far.w, 1e-6);
    float fw = max(length(mask.yz * pix), 1e-4);
    return mix(col, pc, clamp((mask.x - 0.5) / fw + 0.5, 0.0, 1.0));
}

void main() {
    vec2 P = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // VGA coordinates
    vec2 p = P - 0.5;                                     // pixel x's centre at x
    vec3 tv[4];
    tv[0] = tv[1] = tv[2] = tv[3] = vec3(0.0);
    if (tables > 0) tv[0] = tableAt(tab0, tabBlock.x, p);
    if (tables > 1) tv[1] = tableAt(tab1, tabBlock.y, p);
    if (tables > 2) tv[2] = tableAt(tab2, tabBlock.z, p);
    if (tables > 3) tv[3] = tableAt(tab3, tabBlock.w, p);

    vec3 col;
    if (mode == 0) {
        col = planeAt(plane0, 0, off01.xy, p, tv).rgb;
    } else if (mode == 1) {
        col = 0.5 * (planeAt(plane0, 0, off01.xy, p, tv).rgb + planeAt(plane1, 1, off01.zw, p, tv).rgb);
    } else {
        float s = planeAt(plane0, 0, off01.xy, p, tv).r;
        if (planes > 1) s += planeAt(plane1, 1, off01.zw, p, tv).r;
        if (planes > 2) s += planeAt(plane2, 2, off23.xy, p, tv).r;
        if (planes > 3) s += planeAt(plane3, 3, off23.zw, p, tv).r;
        float fs = floor(s);
        uint i0 = uint(int(fs) & 255);
        col = mix(palColor(pal, i0), palColor(pal, (i0 + 1u) & 255u), s - fs);
    }

    if (photoOn != 0) col = overPhoto(col, p);
    o = vec4(col, 1.0);
}
)";

void setAniso(GLuint tex) {
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 16.0f);
    gl::GetError();   // anisotropy is optional
}

// ---------------------------------------------------------------- one Init's effect

// What InitBITMAPEFEK builds, for the GPU.
struct Effect {
    Ini ini;
    int mode = -1;                       // the drawer: 0 one plane, 1 two planes (DTABLE), 2 the 2 x 2 sum; -1 none
    int planes = 0;
    std::array<Texture, 4> idx;          // the planes' indices (modes 0, 1)
    std::array<Texture, 4> pal;          // ... and the palette each is coloured with in mode 1 (its own)
    std::array<Texture, 4> values;       // the planes' indices as values, mipmapped (mode 2)
    // coordinates: 2k column, 2k + 1 row of plane k, as the shader's uniforms
    std::array<int, 8> src{-1, -1, -1, -1, -1, -1, -1, -1};   // -1 formula, else table slot
    std::array<float, 32> geo{}, til{}, fnA{}, fnB{};         // the formulas (kBitmapFS)
    std::array<Texture, 4> tables;       // table slots (R8UI)
    std::array<float, 4> block{1, 1, 1, 1};
    int tableCount = 0;
    Texture photo;                       // TAUSTA after the subtraction (R8UI), when the drawer shows it
    bool photoOn = false;
};

std::unique_ptr<Effect> loadEffect(const VividExe& exe, const std::string& iniName) {
    auto e = std::make_unique<Effect>();
    Ini& ini = e->ini;
    ini = parseIni(exe.file(iniName));
    // the drawer (RunBITMAPEFEK's dispatch, notes 5.1)
    if (ini.type == 1 && ini.planes == 1) e->mode = 0;
    else if (ini.type == 1 && ini.planes == 2) e->mode = 1;
    else if (ini.type == 2 && ini.planes >= 2 && ini.planes <= 4) e->mode = 2;
    e->planes = e->mode == 0 ? 1 : e->mode == 1 ? 2 : e->mode == 2 ? ini.planes : 0;
    if (e->mode < 0) return e;

    // the planes (Init's palette, P1's or DTABLE's with the photo's colours at 192..255, is the timeline's)
    vivid::Indexed p1 = vivid::decodeRaw(exe.file(ini.pics[0]));
    for (int k = 0; k < e->planes; ++k) {
        // a fourth 2 x 2 plane reads plane 3's texture (PICRGB + 0x20000 in 0x40c74f / 0x40c86d)
        const std::string& name = ini.pics[size_t(k == 3 ? 2 : k)];
        vivid::Indexed t = k == 0 ? p1 : vivid::decodeRaw(exe.file(name));
        if (t.px.size() < size_t(kTexSize * kTexSize)) throw std::runtime_error("short texture " + name);
        std::span<const uint8_t> px(t.px.data(), size_t(kTexSize * kTexSize));
        if (e->mode == 2) {
            std::vector<float> v(px.begin(), px.end());
            e->values[size_t(k)].createRaw(kTexSize, kTexSize, GL_R32F, GL_RED, GL_FLOAT, v.data(), true, true, true);
            setAniso(e->values[size_t(k)].id);
        } else {
            e->idx[size_t(k)] = vivid::uploadIndices(px, kTexSize, kTexSize);
            e->pal[size_t(k)] = vivid::uploadPalette(vivid::toColours(t.pal));
        }
    }
    if (ini.bpic == 1 && named(ini.bpicName)) {
        vivid::Indexed b = vivid::decodeRaw(exe.file(ini.bpicName));
        if (b.px.size() < 64000) throw std::runtime_error("short picture " + ini.bpicName);
        // 0x400f79: each little-endian dword minus 0x3f3f3f40 (borrows included)
        std::vector<uint8_t> t(b.px.begin(), b.px.begin() + 64000);
        for (size_t i = 0; i + 4 <= t.size(); i += 4) {
            uint32_t d = uint32_t(t[i]) | uint32_t(t[i + 1]) << 8 | uint32_t(t[i + 2]) << 16 | uint32_t(t[i + 3]) << 24;
            d -= kPhotoSub;
            for (int j = 0; j < 4; ++j) t[i + size_t(j)] = uint8_t(d >> (8 * j));
        }
        e->photo = vivid::uploadIndices(t, 320, 200);
        e->photoOn = e->mode != 1;   // 0x40cbe7 (two 1 x 1 planes) has no background
    }

    // the coordinates: plane k reads T(2k+1) (column) and T(2k+2) (row) through MakePrec
    const int entries = ini.type == 1 ? 64000 : 16000;
    std::map<std::string, int> slots;
    for (int i = 0; i < 2 * e->planes; ++i) {
        const std::string& name = ini.tabs[size_t(i)];
        std::span<const uint8_t> t = exe.file(name);
        if (t.size() < size_t(entries)) throw std::runtime_error("short table " + name);
        Formula f;
        if (ini.type == 1 && matchFormula(t, &f)) {
            const float u[16] = {f.cx, f.cy, float(f.nx), float(f.ny), f.mirror ? 1.0f : 0.0f, float(f.flip), 0, 0,
                                 f.ka, f.kr, f.wa, f.na, f.wr, f.nr, 0, 0};
            for (size_t j = 0; j < 4; ++j) {
                e->geo[size_t(4 * i) + j] = u[j];
                e->til[size_t(4 * i) + j] = u[4 + j];
                e->fnA[size_t(4 * i) + j] = u[8 + j];
                e->fnB[size_t(4 * i) + j] = u[12 + j];
            }
            continue;
        }
        auto it = slots.find(name);
        if (it == slots.end()) {
            if (e->tableCount >= 4) throw std::runtime_error("too many tables in " + iniName);
            int s = e->tableCount++;
            const int w = ini.type == 1 ? 320 : 160, h = ini.type == 1 ? 200 : 100;
            e->tables[size_t(s)] = vivid::uploadIndices(t.subspan(0, size_t(entries)), w, h);
            e->block[size_t(s)] = ini.type == 1 ? 1.0f : 2.0f;
            it = slots.emplace(name, s).first;
        }
        e->src[size_t(i)] = it->second;
    }
    return e;
}

// ---------------------------------------------------------------- the scene

// The offsets of one Run line: o(r) = o0 + v * clamp(r - t0, 0, end - t0) (1/256 texels).
struct Run {
    double t0 = 0, end = 0;
    std::array<double, 8> o0{}, v{};
    std::array<double, 8> at(double r) const {
        double n = std::clamp(r - t0, 0.0, std::max(0.0, end - t0));
        std::array<double, 8> o{};
        for (size_t i = 0; i < 8; ++i) o[i] = o0[i] + v[i] * n;
        return o;
    }
};

class BitmapScene : public Scene {
public:
    void init(Ctx& c) override {
        const std::string fs = std::string(vivid::kIndexedGLSL) + kBitmapFS;
        if (!shader_.build(kFullscreenVS, fs, "bitmap") ||
            !colour_.build(kFullscreenVS, std::string(vivid::kIndexedGLSL) + kColourFS, "bitmap colour"))
            return;
        for (int k = 0; k < 4; ++k) {
            Target& t = planeRgb_[size_t(k)];
            t.create(kTexSize, kTexSize, false, GL_RGBA16F);
            gl::BindTexture(GL_TEXTURE_2D, t.tex);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            gl::GenerateMipmap(GL_TEXTURE_2D);
            setAniso(t.tex);
        }
        const vivid::Timeline& tl = *c.timeline;
        // one effect per .ini (the Init lines of the segments this scene draws)
        std::map<std::string, Effect*> byName;
        for (const vivid::Segment& s : tl.segments()) {
            if (s.scene != "bitmap" || s.init < 0 || effects_.count(s.init)) continue;
            const vivid::ScriptLine& l = tl.line(s.init).line;
            const std::string name = l.arg(0);
            auto it = byName.find(name);
            if (it != byName.end()) {
                effects_[s.init] = it->second;
                continue;
            }
            try {
                owned_.push_back(loadEffect(*c.exe, name));
                byName[name] = effects_[s.init] = owned_.back().get();
            } catch (const std::exception& ex) {
                SDL_Log("bitmap: script line %d: %s", l.line, ex.what());
            }
        }
        replayOffsets(c);
        ok_ = true;
    }

    void render(Ctx& c, double r) override {
        const vivid::Segment* s = c.seg;
        auto it = s ? effects_.find(s->init) : effects_.end();
        auto run = s ? runs_.find(s->run) : runs_.end();
        if (!ok_ || it == effects_.end() || run == runs_.end() || it->second->mode < 0) return;
        const Effect& e = *it->second;
        const Texture& dac = c.paletteTex();   // the DAC: Init's palette and the script's fades
        if (e.mode != 2) colourPlanes(c, e, dac);
        draw(c, e, run->second.at(r), dac);
    }

private:
    // The planes in colour: each texel's index through the DAC (part I) or the texture's own palette (part II, whose
    // DTABLE blends the two textures' colours), mipmapped.
    void colourPlanes(Ctx& c, const Effect& e, const Texture& dac) {
        colour_.use();
        colour_.set("idx", 0);
        colour_.set("pal", 1);
        setBlend(Blend::None);
        for (int k = 0; k < e.planes; ++k) {
            const Target& t = planeRgb_[size_t(k)];
            t.bind();
            e.idx[size_t(k)].bind(0);
            (e.mode == 1 ? e.pal[size_t(k)] : dac).bind(1);
            drawFullscreen();
            gl::BindTexture(GL_TEXTURE_2D, t.tex);
            gl::GenerateMipmap(GL_TEXTURE_2D);
        }
        c.out->bind();
    }

    // The frame: the planes through their coordinates at offsets o (1/256 texels), combined as the drawer does.
    void draw(Ctx& c, const Effect& e, const std::array<double, 8>& o, const Texture& dac) {
        float off[8];
        for (size_t i = 0; i < 8; ++i) off[i] = float(std::fmod(std::fmod(o[i] / 256.0, 256.0) + 256.0, 256.0));
        shader_.use();
        for (int k = 0; k < 4; ++k) {   // units 0..3: the planes
            if (e.mode == 2) e.values[size_t(k)].bind(k);
            else planeRgb_[size_t(k)].bindTex(k);
            shader_.set(("plane" + std::to_string(k)).c_str(), k);
        }
        for (int k = 0; k < 4; ++k) {   // units 4..7: the tables
            e.tables[size_t(k)].bind(4 + k);
            shader_.set(("tab" + std::to_string(k)).c_str(), 4 + k);
        }
        dac.bind(8);
        e.photo.bind(9);
        shader_.set("pal", 8);
        shader_.set("photo", 9);
        shader_.set("photoOn", e.photoOn ? 1 : 0);
        shader_.set("mode", e.mode);
        shader_.set("planes", e.planes);
        shader_.set("pix", 320.0f / float(c.out->w), 200.0f / float(c.out->h));
        shader_.set("off01", off[0], off[1], off[2], off[3]);
        shader_.set("off23", off[4], off[5], off[6], off[7]);
        shader_.set("tables", e.tableCount);
        shader_.set("tabBlock", e.block[0], e.block[1], e.block[2], e.block[3]);
        gl::Uniform1iv(shader_.loc("src"), 8, e.src.data());
        gl::Uniform4fv(shader_.loc("geo"), 8, e.geo.data());
        gl::Uniform4fv(shader_.loc("til"), 8, e.til.data());
        gl::Uniform4fv(shader_.loc("fnA"), 8, e.fnA.data());
        gl::Uniform4fv(shader_.loc("fnB"), 8, e.fnB.data());
        setBlend(Blend::None);
        drawFullscreen();
    }

    // The offsets of every RunBITMAPEFEK line, replayed over the script (notes 4.3): each run starts with what the
    // previous one left (only ClearEFEKPARAMS zeroes them); a run after an Init that builds two 1 x 1 planes starts
    // one refresh late (part II's Inits take past the next refresh).
    void replayOffsets(const Ctx& c) {
        const std::string cmdInit = c.exe->cstr(kCmdInit), cmdRun = c.exe->cstr(kCmdRun);
        const std::string cmdClear = c.exe->cstr(kCmdClear);
        const vivid::Timeline& tl = *c.timeline;
        std::array<double, 8> o{};
        bool late = false;
        for (int i = 0; i < int(tl.lines().size()); ++i) {
            const vivid::TimedLine& l = tl.line(i);
            const std::string& cmd = l.line.command();
            if (cmd == cmdClear) {
                o.fill(0);
            } else if (cmd == cmdInit) {
                auto it = effects_.find(i);
                late = it != effects_.end() && it->second->ini.type == 1 && it->second->ini.planes >= 2;
            } else if (cmd == cmdRun) {
                Run run;
                run.t0 = l.start + (late ? 1.0 : 0.0);
                run.end = std::max(run.t0, l.end);
                run.o0 = o;
                try {
                    run.v = stepSpeeds(readPrm(c.exe->file(l.line.arg(0))));
                } catch (const std::exception& ex) {
                    SDL_Log("bitmap: script line %d: %s", l.line.line, ex.what());
                }
                o = run.at(run.end);
                for (double& x : o) x = std::fmod(x, 65536.0);
                runs_[i] = run;
                late = false;
            }
        }
    }

    bool ok_ = false;
    Shader shader_, colour_;
    std::array<Target, 4> planeRgb_;
    std::vector<std::unique_ptr<Effect>> owned_;
    std::map<int, Effect*> effects_;   // by Init line
    std::map<int, Run> runs_;          // by Run line
};

}  // namespace

REGISTER_SCENE(BitmapScene, "bitmap");
