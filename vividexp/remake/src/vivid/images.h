// The demo's pictures and palettes as the original decodes them, and their upload as indexed textures.
//   - `.pcx` (0x401884, notes-core.md): width = xmax + 1, height = ymax + 1 (header bytes 8..11); the RLE stream from
//     byte 128 is decoded for width x height bytes (bytes per line is not read); the palette follows the 0x0c marker,
//     8-bit components shifted right by 2;
//   - `.raw` (Image Alchemy "mhwanh"): a 32-byte header (width and height big-endian at 8 and 10), a 768-byte
//     palette, the pixels;
//   - `.pal`: 768 bytes of 6-bit components.
// The GPU side follows the other remakes: indices as GL_R8UI (texelFetch, no filtering), palettes as 256 x 1
// RGBA32F of display-space colours (6-bit v -> v / 63); the shaders look each index up and filter the colours, never
// the indices (kIndexedGLSL).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "render.h"

class VividExe;

namespace vivid {

using Palette = std::array<Vec3, 256>;   // display-space colours, 6-bit v -> v / 63

struct Indexed {
    int w = 0, h = 0;
    std::vector<uint8_t> px;     // w * h indices, row 0 first
    std::array<uint8_t, 768> pal{};   // the file's own palette as 6-bit components (PCX: >> 2; RAW: >> 2)
    uint8_t at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
};

Indexed decodePcx(std::span<const uint8_t> file);
Indexed decodeRaw(std::span<const uint8_t> file);
// A picture by archive name: .pcx or .raw. Throws std::out_of_range if VIVID.EXE has no such file.
Indexed loadPicture(const VividExe& exe, const std::string& name);
// A .pal file (6-bit components) as 768 bytes.
std::array<uint8_t, 768> loadPal(const VividExe& exe, const std::string& name);
Palette toColours(const std::array<uint8_t, 768>& p6);

Texture uploadIndices(std::span<const uint8_t> px, int w, int h);
Texture uploadIndices(const Indexed& p);
// 256 x 1 RGBA32F; update() rewrites it in place.
Texture uploadPalette(const Palette& pal);
void updatePalette(Texture& t, const Palette& pal);

// GLSL helpers: vec3 palColor(sampler2D pal, uint index); uint idxAt(usampler2D tex, ivec2 texel) (wrapping);
// uint idxClamp(usampler2D tex, ivec2 texel) (clamped to the edges).
extern const char* kIndexedGLSL;

}  // namespace vivid
