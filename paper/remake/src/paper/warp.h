// Warp grid 0x2dc8 (+ 0x2db3 light, + the 0x3095 texture mapper) evaluated per pixel on the GPU
// (paper/re/notes-init.md §8.7, §8.9; notes-title.md §2.4; notes-water.md §5a/5b).
//
// The original builds 41x26 vertices from 3x3 oscillator control points (osc.h warpField) with the interpolating
// parabola 0x2ad0 (rows: n = 40, step 12/256; columns: n = 25, step 20/256), stores (u = Y << shiftRow, v = X << shiftCol,
// l) per vertex in 8.8, and 0x3095 interpolates them bilinearly over 8x8 blocks and reads
// TABLE[l >> 8][TEX[u >> 8][v >> 8]]. Here the same biquadratic field is evaluated at every pixel's own position:
//   pixel (x, y) of the original (centre at VGA (x + 0.5, y + 0.5)) sits at the parabola parameters
//   ux = 3x/512 (0 .. 1.875 at x = 320) and uy = 5y/512 (0 .. 1.953): the truncated steps of 0x2ad0 are kept, they
//   shape the field. Lagrange basis through u = 0, 1, 2: L0 = (u-1)(u-2)/2, L1 = u(2-u), L2 = u(u-1)/2.
//   texture column = X * 2^shiftCol / 256, row = Y * 2^shiftRow / 256 (256-texel units, wrapping),
//   value = texel * (l / 256) / 32 for the multiply table (title l = 0xa00 -> 10/32, greetings 0x1400 -> 20/32).
// Shifts ([0x2efc] row, [0x2f02] column): title 5/7, "the end" 5/5, greetings 6/6.
//
// GLSL (kWarpGLSL; set its uniforms with setWarpUniforms):
//   vec2 warpXY(vec2 vga)      // the field (X, Y) in the original's units
//   vec2 warpTexel(vec2 vga)   // (column, row) in 256-texel units; sample the cloud texture at warpTexel / 256.0
#pragma once
#include <cstdint>
#include <vector>

#include "paper/osc.h"
#include "render.h"

struct Ctx;

namespace paper {

struct WarpShift {
    int row = 5, col = 7;  // [0x2efc], [0x2f02]
    static WarpShift title() { return {5, 7}; }
    static WarpShift theEnd() { return {5, 5}; }
    static WarpShift greetings() { return {6, 6}; }
};

// 0x2ad0: n+1 samples of the parabola through p0, p1, p2 (step (65536/n) >> 7, so it falls short of p2).
std::vector<int32_t> quadratic2ad0(int32_t p0, int32_t p1, int32_t p2, int n);

// The original's 41x26 grid (u = row, v = column, 8.8, as stored at [0x1edd9]) from exact integer control points.
struct WarpVertex { int32_t u, v; };
std::vector<WarpVertex> warpGridExact(const WarpField& exactField, WarpShift s);

// CPU evaluation of the continuous field at a VGA position: (column, row) in 256-texel units.
void warpTexelAt(const WarpField& f, WarpShift s, double vgaX, double vgaY, double& col, double& row);

extern const char* kWarpGLSL;
void setWarpUniforms(const Shader& sh, const WarpField& f, WarpShift s);

class Warp {
public:
    explicit Warp(Ctx& c);  // use ctx.shared<Warp>()
    // Into the bound target, over the whole frame: rgb = texel(tex at warpTexel) * mul, a = 1. tex = e.g.
    // CoreTextures::cloud (values = original byte / 255), mul = l/8192 (title 10/32). The result is the original's
    // background byte / 255 (an intensity, to be mapped through a palette ramp later).
    void drawTextured(Ctx& c, const WarpField& f, WarpShift s, GLuint tex, float mul);
    // Into the bound target (use an RGBA32F one): (column, row, l, 1) per pixel, column/row in 256-texel units
    // (not wrapped), l = the shade row (e.g. 10 for the title).
    void drawField(Ctx& c, const WarpField& f, WarpShift s, float l);

private:
    Shader textured_, field_;
};

}  // namespace paper
