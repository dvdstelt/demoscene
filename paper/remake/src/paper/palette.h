// Palette system of the original as true-colour functions (paper/re/notes-init.md §5.6, §6):
//   0x169a3  default palette (6-bit RGB, static in the exe)        -> DefaultPalette, palette texture
//   0x15c0   DAC upload with the fade level [0x1edb5]               -> applyLevel (the post pass in fade.h uses it)
//   0x14dc   hue palette generator (angle [0x14dd], shift [0x1505]) -> huePalette6 (exact), hueBase + hueRamp (analytic)
//   0x144b   shade table [0x1eded] and multiply table (+0x10000)   -> shadeIndex, mulIndex (continuous)
// Colours are display-space floats 0..1 (6-bit value v -> v/63). Palette indices are floats: the original's
// 32-entry ramps are quantisations of linear ramps, so a fractional index is interpolated inside its ramp.
//
// GLSL: kPaletteGLSL has the same functions for shaders (prepend it to your fragment shader source):
//   vec3  palLookup(sampler2D pal, float index)   // 256x1 palette texture (PaletteTex), interpolated inside the ramp
//   vec3  hueRamp(vec3 base, float index)         // analytic 0x14dc ramp, index 0..255 (fractional)
//   float shadeIndex(float row, float c)          // 0x144b SHADE[row][c] without truncation
//   float mulIndex(float row, float c)            // 0x144b MUL[row][c] = c*row/32 (saturating at 255)
//   vec3  applyLevel(vec3 c, float level)         // 0x15c0 on a display colour, level = [0x1edb5] (0x8000 = 1:1)
#pragma once
#include <array>
#include <cstdint>

#include "render.h"

class PaperExe;
struct Ctx;

namespace paper {

// ---- 0x169a3 default palette
struct DefaultPalette {
    std::array<uint8_t, 768> raw6{};  // as in the exe (6-bit RGB)
    std::array<Vec3, 256> rgb{};      // raw6 / 63
    explicit DefaultPalette(const PaperExe& exe);
    // Colour at a fractional index, linear inside its ramp (greys 0..63; 32-entry ramps from 64 on).
    Vec3 at(double index) const;
};

// The ramp a palette index belongs to: [first, last] = [0, 63] for the grey ramp, else the 32-entry block.
void rampOf(double index, int& first, int& last);

// ---- 0x15c0 fade level on a colour. level = [0x1edb5] (0 black, 0x8000 unchanged, 0xff00 white), may be fractional.
// Continuous form of   L = level >> 8;  L <= 128: c*L/128;   else: b = min(L,255)-128, towards white with b/127.
// (The integer formula's own truncations, its 127/128 factor and the 1-level dip right above 128 are dropped.)
Vec3 applyLevel(Vec3 c, double level);
float levelFactor(double level);   // L <= 128: the multiplier L/128, else 1
float levelWhite(double level);    // L > 128: the mix towards white (b/127), else 0
// The original's exact integer upload of one 6-bit value (for checks against the capture's DAC).
int applyLevel6(int p, int level);

// ---- 0x14dc hue palette
// Exact 256-entry 6-bit palette as the original builds it (angle = [0x14dd], shift = [0x1505]: 11 title/greetings/
// "the end", 12 closing logo).
std::array<uint8_t, 768> huePalette6(int angle, int shift);
// Its base colour (entry 128, the "colour" of the black -> colour -> white ramp) for a real angle (1/1024 turns):
// (R,G,B)/252 with R = 128 - s, G = 128 + s/2 + c*222/256, B = 128 + s/2 - c*222/256, s = 65536 sin / 2^shift,
// c = 65536 cos / 2^shift.
Vec3 hueBase(double angle, int shift);
// The analytic ramp the 256 entries quantise: index 0 black, 128 = base, white from 254 on:
//   i <= 128: base*i/128;   i = 128+k: min(1, base*(128-k)/128 + k/126).
Vec3 hueRamp(Vec3 base, double index);

// ---- 0x144b shade / multiply tables as continuous index functions
// SHADE[row][c]: row <= 32 darkens by 32-row ramp steps (grey ramp 0..63: 2 entries per step), rows 33..64 are the
// shadow rows (k = 0.625*(row-32)); never below the ramp's first entry. row 32 = identity.
double shadeIndex(double row, double c);
// MUL[row][c] = min(255, c*row/32).
double mulIndex(double row, double c);

// ---- GPU side
extern const char* kPaletteGLSL;

// 256x1 RGBA16F textures (nearest filtering; palLookup interpolates inside the ramp itself).
struct PaletteTex {
    Texture def;   // the default palette 0x169a3
    explicit PaletteTex(Ctx& c);
    // Upload any 256-colour palette into a 256x1 texture (created on first use).
    static void upload(Texture& t, const std::array<Vec3, 256>& rgb);
};

// A 256-entry ramp texture of the analytic hue palette (entries sampled at integer indices, linear filtering:
// sample at u = (index + 0.5) / 256). update() is cheap (256 texels) and can run every frame.
struct HueRampTex {
    Texture tex;
    void update(double angle, int shift);
};

}  // namespace paper
