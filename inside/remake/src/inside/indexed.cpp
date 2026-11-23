// Indexed pictures and palettes on the GPU (indexed.h).
#include "inside/indexed.h"

#include <algorithm>

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

Vec3 colourGain(const std::array<Vec3, 256>& pal, const std::array<Vec3, 256>& y, const std::array<double, 256>& use) {
    double total = 0;
    for (double v : use) total += v;
    double eps = 1e-3 * std::max(total, 1.0) / 256;
    auto ch = [](const Vec3& v, int k) { return double(k == 0 ? v.x : k == 1 ? v.y : v.z); };
    float g[3];
    for (int k = 0; k < 3; k++) {
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t j = 0; j < 256; j++) {
            double w = use[j] + eps, x = ch(pal[j], k), v = ch(y[j], k);
            n += w; sx += w * x; sy += w * v; sxx += w * x * x; sxy += w * x * v;
        }
        double var = sxx - sx * sx / n;
        g[k] = var > 1e-9 * n ? float(std::clamp((sxy - sx * sy / n) / var, 0.0, 2.0)) : 1.0f;
    }
    return Vec3(g[0], g[1], g[2]);
}

}  // namespace inside
