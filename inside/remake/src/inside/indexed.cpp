// Indexed pictures and palettes on the GPU (indexed.h).
#include "inside/indexed.h"

#include <vector>

#include "exe.h"
#include "pictures.h"

namespace inside {

Texture uploadIndices(std::span<const uint8_t> px, int w, int h) {
    Texture t;
    t.createRaw(w, h, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, px.data(), false, true, false);
    return t;
}

Texture uploadPalette(const std::array<Vec3, 256>& pal) {
    std::vector<float> d(256 * 4);
    for (int i = 0; i < 256; i++) {
        d[size_t(i) * 4 + 0] = pal[size_t(i)].x;
        d[size_t(i) * 4 + 1] = pal[size_t(i)].y;
        d[size_t(i) * 4 + 2] = pal[size_t(i)].z;
        d[size_t(i) * 4 + 3] = 1.0f;
    }
    Texture t;
    t.createRaw(256, 1, GL_RGBA32F, GL_RGBA, GL_FLOAT, d.data(), false, false, false);
    return t;
}

std::array<Vec3, 256> palette(const InsideDat& dat, const std::string& file, bool black0) {
    std::array<Vec3, 256> p = datPalette(dat, file);
    if (black0) p[0] = Vec3(0, 0, 0);
    return p;
}

const char* kIndexedGLSL = R"(
vec3 palColor(sampler2D pal, uint i) { return texelFetch(pal, ivec2(int(i), 0), 0).rgb; }
uint idxAt(usampler2D tex, ivec2 t) {
    ivec2 s = textureSize(tex, 0);
    return texelFetch(tex, ivec2(((t.x % s.x) + s.x) % s.x, ((t.y % s.y) + s.y) % s.y), 0).r;
}
)";

}  // namespace inside
