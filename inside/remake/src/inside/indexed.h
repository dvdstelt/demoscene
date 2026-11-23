// 8-bit pictures and palettes from INSIDE.DAT on the GPU. The original draws palette indices; the remake looks the
// indices up in the palette in the shader, tap by tap, and filters the resulting colours (never the indices).
//   - indices: GL_R8UI, read with texelFetch from a usampler2D (exact indices, no filtering);
//   - palettes: 256 x 1 RGBA32F, colour = v / 63 per 6-bit value (entries above 63 clamped, as 0x400364 does).
// kIndexedGLSL has the shader helpers.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "render.h"

class InsideDat;

namespace inside {

Texture uploadIndices(std::span<const uint8_t> px, int w, int h);  // w * h bytes, row 0 first
Texture uploadPalette(const std::array<Vec3, 256>& pal);
// The palette file as colours, with entry 0 forced to black if `black0` (several parts write 0 into entry 0).
std::array<Vec3, 256> palette(const InsideDat& dat, const std::string& file, bool black0 = false);

// GLSL: vec3 palColor(sampler2D pal, uint index) and uint idxAt(usampler2D tex, ivec2 texel) (wrapping).
extern const char* kIndexedGLSL;

// How much of a change in a texel's colour a table passes on, per channel: the weighted least-squares gain of
// y[j] ~ g * pal[j] + c over the 256 indices (weights use[j] + 1e-3 of their mean, so mostly the colours a picture
// uses), clamped to 0..2; 1 where those colours do not vary in a channel. y is what the table makes of index j (the
// palette colour of a shade row's entry, a fitted blend's offset). The upscales' detail is added through it
// (pictures.h, inside/engine_gl.cpp).
Vec3 colourGain(const std::array<Vec3, 256>& pal, const std::array<Vec3, 256>& y, const std::array<double, 256>& use);

}  // namespace inside
