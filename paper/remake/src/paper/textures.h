// Procedural textures made by the original's init (paper/re/notes-init.md §5.4, §5.5), exact and at high resolution:
//   0x10d3  256x256 grey "cloud" / kaleidoscope texture [0x1eddd] from the static 64x64 4-bit image at 0x16ca3
//           (title and "the end" warp, greetings height field, the light map, the teal/flock canyon texture)
//   0x55e2  320x200 paper grain [0x1edf5] (shade-table rows 28..31): the paper of the sketch, scene and logo page,
//           and the back of the peeled page (0x2ca2)
//
// GPU (ctx.shared<CoreTextures>()):
//   cloud   R16 cloudSize^2 (default 1024), repeat, mipmapped, values = original byte / 255. Built with the original's
//           construction in continuous form: the 64x64 nibble image interpolated (Catmull-Rom) instead of pixel-doubled,
//           mirrored 4 ways, a toroidal Gaussian of the same variance as the 6 four-neighbour passes (sigma = sqrt(3)
//           texels of the 256 texture), shifted by the same (-6,-6) texels. It tiles; area-downsampled to 256 it
//           matches the exact texture (see coretest). Address it like the original: texture column c, row r in
//           256-texel units -> sample at (c/256, r/256) (texel centres of the 256 grid at +0.5, as GL does).
//   cloud256 R8 256x256, the exact original bytes (nearest, repeat), for references and exact derived tables.
//   grain   R32F 320x200, repeat, linear: the paper grain *before* its truncation to 4 levels, i.e. the shade row
//           e = 30 - 7/16 + k (n[i] - n[i+321]) / 8 of the same random noise n (k = 1.06 restores the variance the
//           truncation added: same mean 29.56 and variance 0.746 as the original's 28..31, correlation 0.94 with
//           it). One texel per original pixel; sample it bicubic (paperGrain() in kTexturesGLSL) so the realisation
//           stays the original's at its own scale.
//
// GLSL (kTexturesGLSL, needs kCommonGLSL's texBicubic, which every Shader gets):
//   float paperGrain(sampler2D grain, vec2 vga)       // shade row e at a VGA position (bicubic, ~27.6..31.4)
//   float paperGrainFine(sampler2D grain, vec2 vga, float amount)  // + optional sub-pixel detail (amount ~0.3:
//                                                      // smooth noise at 3 cells per original pixel)
//   vec3  pageBack(sampler2D grain, vec2 vga)          // back of the peeled page: the raw grain used as colour
//                                                      // indices of the default grey ramp (grey e-1 of 63)
//   vec3  paperFront(sampler2D grain, vec2 vga)        // white paper as the 3D parts show it: SHADE[e][0x3e]
#pragma once
#include <cstdint>
#include <vector>

#include "render.h"

class PaperExe;
struct Ctx;

namespace paper {

// Exact 0x10d3: 256x256 bytes (values 0..224), row-major [row][column].
std::vector<uint8_t> cloudTexture256(const PaperExe& exe);
// High-resolution n x n version (n a multiple of 256), values = byte/255 in 0..65535.
std::vector<uint16_t> cloudTextureHi(const PaperExe& exe, int n);

// Exact 0x55e2: 320x200 bytes (28..31), from the random generator at RngPos::kPaperGrain.
std::vector<uint8_t> paperGrainBytes(const PaperExe& exe);
// The continuous shade row e (see above), 320x200 floats.
std::vector<float> paperGrainSmooth(const PaperExe& exe);

struct CoreTextures {
    static constexpr int kCloudSize = 1024;
    Texture cloud, cloud256, grain;
    explicit CoreTextures(Ctx& c);
};

extern const char* kTexturesGLSL;

}  // namespace paper
