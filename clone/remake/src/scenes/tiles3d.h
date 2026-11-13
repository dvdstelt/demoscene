// What the 3D parts 7 (bumps.cpp), 8 (blob.cpp), 9 (cubes.cpp) and 10 (slabs.cpp) share (clone/re/notes-parts3d.md):
//   TileBand    the background of all four: the loop over an 8 x 8 grid of 40 x 24 rectangles from row 4 that every
//               part routine (0x416fce, 0x417929, 0x4185e5, 0x41958e) runs with the colours of the table 0x428164
//               (cmc::tileColour) and fills with 0x4043be (2.4);
//   Cubes       the cube builder 0x417c48 (part 9) / 0x418b86 (part 10) with its tables 0x428020 (faces) and 0x4280e0
//               (corners) (5.5);
//   blobField   the metaball field of 0x418094 / 0x418fd2: -135 + 270 times Wyvill's falloff of three blobs (5.4);
//   cubeMask    the neighbour test of the voxel loops 0x418445 / 0x4193b6, with the original's x + 2 read (5.5);
//   warmUp      one engine draw in init() so that the first frame of a part does not pay for buffers and shaders.
//
// Evaluated exactly instead of the original's shortcuts: the tile colours without the truncation to bytes (the clamp
// at 255 stays), each pixel coloured by the area of the tiles it covers (anti-aliased edges at any resolution); the
// cube shades as floats instead of 16.16 integers.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "clone/core.h"
#include "clone/engine.h"
#include "demo.h"
#include "exe.h"

namespace p3d {

// =============================================================================================== the tile band
// 0x4043be(target + (y0 * 320 + x0) * 4, colour, 40, 24) for row = -1..6 (y0 = 24 row + 28: rows 4..195), col = 0..7
// (x0 = 40 col), with the dword index idx = start + 3 (8 (row + 1) + col) into 0x428164. Rows 0..3 and 196..199 stay
// black (the frame is cleared; parts 8 and 9 do not write them, the capture shows them black: notes-parts3d 2.5).
inline const char* kTileVS = R"(
uniform vec4 rect;   // x, y, w, h in VGA coordinates
out vec2 vga;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vga = rect.xy + c * rect.zw;
    gl_Position = vec4(vga.x / 160.0 - 1.0, 1.0 - vga.y / 100.0, 0.0, 1.0);
}
)";

// Each pixel: the area-weighted sum of the tiles under its footprint (black outside the band).
inline const char* kTileFS = R"(
in vec2 vga;
out vec4 o;
uniform vec3 tiles[64];   // row -1..6 (from the top), col 0..7; display-space colours
vec3 tile(int col, int row) {
    return col < 0 || col > 7 || row < 0 || row > 7 ? vec3(0.0) : tiles[row * 8 + col];
}
void main() {
    vec2 hw = 0.5 * fwidth(vga), size = vec2(40.0, 24.0), top = vec2(0.0, 4.0);
    vec2 lo = (vga - hw - top) / size, hi = (vga + hw - top) / size;
    ivec2 a = ivec2(floor(lo)), b = min(ivec2(floor(hi)), a + 3);
    vec3 sum = vec3(0.0);
    for (int j = a.y; j <= b.y; j++)
        for (int i = a.x; i <= b.x; i++) {
            vec2 w = max(vec2(0.0), min(hi, vec2(i + 1, j + 1)) - max(lo, vec2(i, j)));
            sum += tile(i, j) * (w.x * w.y);
        }
    o = vec4(sum / max((hi.x - lo.x) * (hi.y - lo.y), 1e-12), 1.0);
}
)";

class TileBand {
public:
    explicit TileBand(Ctx&) { sh_.build(kTileVS, kTileFS, "tiles3d"); }

    // visible(row, col) for row -1..6, col 0..7; factor(idx) the part's F for the tile at dword index idx. The colour is
    // min(255, value * 0.0125 * 255 * F) per channel, without the truncation.
    template <class Visible, class Factor>
    void draw(Ctx& c, int start, Visible&& visible, Factor&& factor) {
        float col[64 * 3];
        for (int row = -1; row <= 6; row++)
            for (int cl = 0; cl <= 7; cl++) {
                int idx = start + 3 * (8 * (row + 1) + cl);
                Vec3 v(0, 0, 0);
                if (visible(row, cl)) v = cmc::tileColour(*c.exe, idx) * float(factor(idx));
                float* p = &col[3 * (8 * (row + 1) + cl)];
                p[0] = std::clamp(v.x, 0.0f, 1.0f);
                p[1] = std::clamp(v.y, 0.0f, 1.0f);
                p[2] = std::clamp(v.z, 0.0f, 1.0f);
            }
        setBlend(Blend::None);
        sh_.use();
        sh_.set("rect", 0.0f, 0.0f, 320.0f, 200.0f);
        gl::Uniform3fv(sh_.loc("tiles"), 64, col);
        drawQuad();
    }

private:
    Shader sh_;
};

// Draws a mesh once with the engine into a scratch target of the frame's size (in init()), so that the engine's
// multisampled buffers, its shaders' first use and the map's upload do not fall on the part's first frame (some
// 40 ms at 2560 x 1920).
inline void warmUp(Ctx& c, const cmc::EngineMesh& m) {
    if (m.faces.empty() || c.w <= 0 || c.h <= 0) return;
    TargetPool::Lease tmp = c.temp();
    cmc::Engine& eng = c.shared<cmc::Engine>();
    eng.begin(*tmp, cmc::Window::band());
    eng.draw(m, 1.0f);
    eng.end();
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The red value (percent) of the tile at dword index idx: part 8's pulse speed (notes-parts3d 4.1).
inline double tileRed(const CloneExe& exe, int idx) {
    return idx >= 0 && idx < 3 * cmc::kTileEntries ? double(exe.s32(0x428164 + 4 * uint32_t(idx))) : 0.0;
}

// =============================================================================================== metaballs
// Wyvill's soft-object falloff (doubles in the original): ((-4/9 a + 17/9) a - 22/9) a + 1 = (1 - a)^2 (1 - 4a / 9).
inline double wyvill(double a) { return ((-4.0 / 9 * a + 17.0 / 9) * a - 22.0 / 9) * a + 1; }

// 0x418094 / 0x418fd2 at one grid point: -135, plus 270 W(d / R^2) for every blob within reach. The original tests the
// third blob's reach with R1 and its falloff with R2 (all three radii are 35); the first blob's term replaces the -135.
inline float blobField(double X, double Y, double Z, const double (&b)[3][3], const int (&R)[3]) {
    float d[3];
    for (int k = 0; k < 3; k++)
        d[k] = float((X - b[k][0]) * (X - b[k][0]) + (Y - b[k][1]) * (Y - b[k][1]) + (Z - b[k][2]) * (Z - b[k][2]));
    double r0 = double(R[0]) * R[0], r1 = double(R[1]) * R[1], r2 = double(R[2]) * R[2];
    double v = -135;
    if (r0 > d[0]) v = 270 * wyvill(d[0] / r0) - 135;
    if (r1 > d[1]) v += 270 * wyvill(d[1] / r1);
    if (r1 > d[2]) v += 270 * wyvill(d[2] / r2);
    return float(v);
}

// The voxel loops' face mask of a cell: a cube of half-size field / 6 if that is above 0.5; at field / 6 >= 15 a full
// cube (half-size 15, touching its neighbours) whose faces towards full neighbours (field >= 90) are left out.
// `g` is the flat field array, k the cell, sz / sy the strides of the outer two axes (the inner axis has stride 1).
// The +x test reads k + 2, not k + 1 (sic): it keeps the +x face when the next cell is full but the one after is not
// (inside the solid, hidden by the depth buffer) and drops it when the cell after next is full and the next one is
// not (a missing face for a frame). Returns the half-size (0: no cube) and the mask.
struct Voxel {
    float size = 0;
    int mask = 0;
};
inline Voxel cubeMask(const float* g, int k, int sz, int sy) {
    Voxel v;
    float size = float(g[k] * (1 / 6.0f));
    if (!(size > 0.5f)) return v;
    int mask = 0x3f;
    if (size >= 15) {
        size = 15;
        if (g[k - sz] >= 90) mask &= ~0x01;
        if (g[k + sz] >= 90) mask &= ~0x02;
        if (g[k - sy] >= 90) mask &= ~0x04;
        if (g[k + sy] >= 90) mask &= ~0x08;
        if (g[k - 1] >= 90) mask &= ~0x20;
        if (g[k + 2] >= 90) mask &= ~0x10;   // sic: x + 2
    }
    v.size = size;
    v.mask = mask;
    return v;
}

// =============================================================================================== cubes
// 0x417c48 / 0x418b86: cube(x, y, z, mask, s) appends 8 corners (x -+ s, y -+ s, z -+ s) to the object-space vertex
// list and the faces of the table 0x428020 ({a, b, c, mask bit} x 12: two triangles per side) whose bit is set, with
// per-corner shades 192 + 31 (sin(0.035 X + C0) + sin(0.035 Y + C1) + sin(0.035 Z + C2)) at the actual corners (the
// 16.16 value 2031616 (...) + 12582912 in table units) and a planar uv k (X + Z, Y - Z) at the corners of the full
// cell (the table 0x4280e0: +-15), so a shrunk cube shows its cell's part of the map squeezed.
class Cubes {
public:
    void read(const CloneExe& exe) {
        for (int j = 0; j < 12; j++)
            for (int k = 0; k < 4; k++) tab_[j][k] = exe.s32(0x428020 + 16 * uint32_t(j) + 4 * uint32_t(k));
        for (int i = 0; i < 8; i++)
            for (int k = 0; k < 3; k++) corner_[i][k] = exe.f32(0x4280e0 + 12 * uint32_t(i) + 4 * uint32_t(k));
    }
    void clear(cmc::EngineMesh& m) {
        src.clear();
        m.faces.clear();
    }
    // C = the colour phases C0, C1, C2; uvk = 0.003 (part 9) or 0.009 (part 10).
    void cube(cmc::EngineMesh& m, float x, float y, float z, int mask, float s, double uvk, const double (&C)[3]) {
        uint32_t n = uint32_t(src.size());
        float xs = x - s, xp = x + s, ys = y - s, yp = y + s, zs = z - s, zp = z + s;
        const Vec3 v[8] = {{xs, ys, zs}, {xp, ys, zs}, {xp, yp, zs}, {xs, yp, zs},
                           {xs, ys, zp}, {xp, ys, zp}, {xp, yp, zp}, {xs, yp, zp}};
        double shade[8];
        for (int i = 0; i < 8; i++) {
            src.push_back(v[i]);
            shade[i] = 192.0 + 31.0 * (std::sin(0.035 * v[i].x + C[0]) + std::sin(0.035 * v[i].y + C[1]) +
                                       std::sin(0.035 * v[i].z + C[2]));
        }
        for (int j = 0; j < 12; j++) {
            if (!(mask & tab_[j][3])) continue;
            cmc::EngineFace f;
            for (int k = 0; k < 3; k++) {
                int i = tab_[j][k] & 7;
                f.v[k] = n + uint32_t(i);
                f.shade[k] = float(shade[i]);
                double qx = x + corner_[i][0], qy = y + corner_[i][1], qz = z + corner_[i][2];
                f.uv[k][0] = float(uvk * qx + uvk * qz);
                f.uv[k][1] = float(uvk * qy - uvk * qz);
            }
            m.faces.push_back(f);
        }
    }
    // The object-space vertices (0x417c48 writes 8 per cube into the source array; 0x4050e9 transforms them).
    std::vector<Vec3> src;

private:
    int tab_[12][4] = {};
    float corner_[8][3] = {};
};

}  // namespace p3d
