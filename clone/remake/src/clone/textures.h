// The generated textures (clone/re/notes-gfx.md sections 2..4; main 0x400240..0x4008b4 and 0x4033a7), evaluated
// exactly instead of through the original's 8-bit log levels:
//   map A [0xe35108]: light violet-blue with dark blue spots every 32 texels    (part 7, texture mapper)
//   map B [0xe3510c]: a blue / violet lattice of round spots every 16 texels    (parts 8 and 10)
//   map C [0xe35110]: a fine blue lattice every 8 texels                         (part 9)
//   table 1 (0xe6542b): pale pink-grey, 8-texel spots     -> the grid texture [0x4207c8] of part 2
//   table 2 (0xe35128): pinker, 32-texel spots            -> the grid texture from event 5 on: parts 5 and 4
// All five repeat every 256 texels in u and v. The values are LINEAR: V = 1 + x on the original's 0..255 scale
// (stored here as V / 255), which is what the fillers' exp tables decode the stored levels to before they multiply
// by the shade (cmc::gain23 for the grid filler, cmc::gain90 for the texture mapper; notes-gfx 2.4). What the log
// form adds, and the remake keeps: the maps' dark values are raised to 31.31 / 255 (enc90 clamps the level at 0).
// What it drops: the truncation of the cell brightness and of the level (the original is on average 0.6 levels of
// 4.3 % (tables) to 1.0 level of 1.1 % (maps) darker), the geometric-mean filtering between texels, the top byte of
// the grid texels and the one-texel seam where the grid texture wraps in u.
//
// Two styles (notes-gfx section 4):
//   Smooth (default): the formula with the continuous distance to the cell centre, at Textures::kSize texels per
//     period: round, smooth spots (map C about 5 % darker on average than the original's stepped rings);
//   Texel: the original's 256 x 256 texels with the integer radius floor(r) (stepped rings, squarer spots), evaluated
//     in float, filtered linearly: the look closest to the capture.
//
// Two forms:
//   Linear (default): V / 255 per channel;
//   Log: the stored level L / 255 per channel, without the truncation: L = 128 + ln(V / 128) * 1000 / 11 for the maps
//     (0x4167b7), L = 128 + 23 ln(V) for the tables (0x4034e7). The fillers filter the levels and decode afterwards
//     (a geometric mean between texels, up to 3.4 % darker halfway than filtering V; notes-engine 10 asks for it in
//     the texel style). Decode after the filter with the shade s (table units, 128 = gain 1):
//       maps:   colour = min(1, 128 exp(0.011 (L + s - 256)) / 255)        (notes-engine 10, GLSL cmDecodeMap)
//       tables: colour = min(1, exp((L + s - 256) / 23) / 255)             (notes-grid 3, GLSL cmDecodeTable)
//
// Coordinates: the original samples its texture at (u, v) in texels, bilinearly between texel floor(u) and
// floor(u) + 1, so a texel's value is at integer coordinates. In GL (both styles) sample at
//   s = (u + 0.5) / 256, t = (v + 0.5) / 256   (u = column, v = row of the original's 256 x 256 array).
// The grid texture is addressed with its row first in the grid filler (notes-grid 2: texel (row u, column v)); the
// grid scope's (u, v) are then (column, row) = (v, u) here.
//
// Use: cmc::Textures& tx = c.shared<cmc::Textures>();  tx.get(cmc::Tex::MapB).bind(0);
//      tx.grid(c, t) = table 1 before event 5 (20:00), table 2 from it (nothing restores table 1).
#pragma once
#include "demo.h"

namespace cmc {

enum class Tex { MapA, MapB, MapC, Table1, Table2 };
enum class TexStyle { Smooth, Texel };
enum class TexForm { Linear, Log };

// The linear value V = 1 + x (0..255 scale, per channel R, G, B) of a texture at original texel coordinates (u, v)
// (any real numbers; period 256). Texel style: floor of the distance at integer (u, v), as the original.
Vec3 texValue(Tex which, double u, double v, TexStyle style = TexStyle::Smooth);
// The same as stored levels L (0..255, not truncated; see "Two forms").
Vec3 texLevel(Tex which, double u, double v, TexStyle style = TexStyle::Smooth);

class Textures {
public:
    static constexpr int kSize = 1024;  // texels per period of the smooth style (4 per original texel)
    explicit Textures(Ctx& c);
    ~Textures();
    // RGBA16F, V / 255 (or L / 255) in RGB, alpha 1; GL_REPEAT; the smooth style mipmapped (trilinear, anisotropic),
    // the texel style bilinear. Built on first use (the smooth style takes a few tens of ms).
    const Texture& get(Tex which, TexStyle style = TexStyle::Smooth, TexForm form = TexForm::Linear);
    // The grid filler's texture [0x4207c8] at music time t: table 1, from event 5 on table 2.
    const Texture& grid(const Ctx& c, double t, TexStyle style = TexStyle::Smooth, TexForm form = TexForm::Linear);

private:
    Texture tex_[2][2][5];  // [style][form][which]
};

// GLSL (prepend to a fragment shader):
//   vec3 cmTexValue(int which, vec2 uv)   the formulas: which = 0..4 as Tex, uv in original texels, smooth style,
//                                         V / 255; a procedural texture of unlimited sharpness that does not filter,
//                                         so prefer the textures where the texture is minified (far ends of tunnels)
//   vec3 cmDecodeMap(vec3 L, float s)     a map's level (sampled from the Log form, 0..1) with shade s -> colour
//   vec3 cmDecodeTable(vec3 L, float s)   the same for the grid tables
extern const char* kTexGLSL;

}  // namespace cmc
