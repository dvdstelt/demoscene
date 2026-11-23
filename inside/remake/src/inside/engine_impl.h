// Internal interface between the engine's CPU pipeline (engine.cpp) and its OpenGL renderer (engine_gl.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include "inside/engine.h"

namespace inside::sr::detail {

// What the shader draws per face (Raster values 0..5, then the billboards).
enum GpuRaster : int16_t { kSpriteOpaque = 6, kSpriteTable = 7 };

// One vertex of the frame's triangle stream (three per triangle, in drawing order).
struct GpuVertex {
    float pos[4];      // clip space (x, y from the 320 x 200 frame, z = 0, w = camera z)
    float uv[4];       // u, v in texels of the texture (with the rasterizers' +1 bias), eu, ev in texels of the second
                       // texture; billboards: u, v in texels, then the bitmap's width and height
    float lev[2];      // table rows of channel 1 and 2 (shade / 16, with the +1 bias)
    int16_t a[4];      // raster, texture layer, second texture layer, table 1 layer
    int16_t b[4];      // table 2 layer, rows of table 1, rows of table 2, log2 of the texture size (w | h << 4)
};
static_assert(sizeof(GpuVertex) == 56, "vertex layout");

struct TexInfo {
    int w = 0, h = 0;      // texels
    int wlog = 0, hlog = 0;
};

// What the pipeline needs from the renderer's resources.
struct Resources {
    const std::vector<Material>* materials = nullptr;
    const std::vector<TexInfo>* textures = nullptr;
    const std::vector<int>* tableRows = nullptr;
};

// The CPU part of 0x40c6ac for a scene: world transforms, the object pipelines, the sort, the near clipper, and the
// triangle stream for the GPU. `blended` tells whether any item reads the background through a table.
void buildFrame(Scene& s, const Resources& res, const Fidelity& f, std::vector<GpuVertex>& out, bool& blended);

}  // namespace inside::sr::detail
