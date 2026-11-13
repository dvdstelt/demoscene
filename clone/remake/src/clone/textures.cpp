// The generated textures (textures.h): main's texture loop 0x400240..0x4008b4 (blocks 8, 32, 16 and A, notes-gfx 2.3)
// as the linear formula of notes-gfx section 4, at high resolution (smooth style) or on the original's texels with
// the integer radius (texel style); 0x4033a7 (the grid texture) is a repacking of the tables and needs no code here.
#include "clone/textures.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace cmc {
namespace {
constexpr double kPi = 3.14159265358979323846;
// enc90 clamps the stored level at 0, which decodes to 128 * e^(-128 * 0.011): the maps' darkest value
const double kMapFloor = 128.0 * std::exp(-128.0 * 0.011);

bool isMap(Tex w) { return w == Tex::MapA || w == Tex::MapB || w == Tex::MapC; }

// Distance to the centre of the n-texel cell that contains (u, v); the centre is texel n / 2. The original takes the
// floor of the distance at integer texels.
double cellRadius(double u, double v, int n, bool floorR) {
    double du = u - n * std::floor(u / n) - 0.5 * n, dv = v - n * std::floor(v / n) - 0.5 * n;
    double r = std::sqrt(du * du + dv * dv);
    return floorR ? std::floor(r) : r;
}

// Cell brightness: block 8 (0..96), block 16 (0..112), block 32 (0..112); map A uses 168 - c32 (56..168).
double cellBrightness(Tex w, double u, double v, bool floorR) {
    switch (w) {
        case Tex::MapC:
        case Tex::Table1: return (std::cos(kPi * cellRadius(u, v, 8, floorR) * 0.125) + 1.0) * 48.0;
        case Tex::MapB: return (std::cos(kPi * cellRadius(u, v, 16, floorR) * 0.0625) + 1.0) * 56.0;
        case Tex::Table2: return (std::cos(kPi * cellRadius(u, v, 32, floorR) * 0.03125) + 1.0) * 56.0;
        case Tex::MapA: return 168.0 - (std::cos(kPi * cellRadius(u, v, 32, floorR) * 0.03125) + 1.0) * 56.0;
    }
    return 0.0;
}

// The channels from the cell brightness c and the colour drift s = su + sv (one period over the texture in u and v).
Vec3 channels(Tex w, double c, double s) {
    auto lo = [&](double a0) { return a0 + 0.05 * s; };
    double hi = 0.7 + 0.15 * s;
    double r = 0, g = 0, b = 0;
    switch (w) {
        case Tex::MapA:
        case Tex::MapB: r = c * hi; g = c * lo(0.7); b = 1.5 * c; break;    // B = (c * 5) * 0.3
        case Tex::MapC: r = c * lo(0.7); g = c * hi; b = 1.5 * c; break;
        case Tex::Table1: r = 0.7 * c; g = c * lo(0.5); b = c * hi; break;
        case Tex::Table2: r = 0.74 * c; g = c * lo(0.42); b = c * hi; break;  // R = c * 0.37 * 2
    }
    double v[3] = {1.0 + r, 1.0 + g, 1.0 + b};
    if (isMap(w))
        for (double& x : v) x = std::max(x, kMapFloor);
    return Vec3(float(v[0]), float(v[1]), float(v[2]));
}

Vec3 toLevel(Tex w, Vec3 v) {
    auto one = [&](float x) {
        return float(isMap(w) ? 128.0 + std::log(x / 128.0) * (1000.0 / 11.0) : 128.0 + 23.0 * std::log(double(x)));
    };
    return Vec3(one(v.x), one(v.y), one(v.z));
}

void fill(Tex w, TexStyle style, TexForm form, int n, std::vector<float>& px) {
    px.assign(size_t(n) * n * 4, 1.0f);
    bool texel = style == TexStyle::Texel;
    auto rows = [&](int y0, int y1) {
        for (int y = y0; y < y1; y++) {
            double v = texel ? y : (y + 0.5) * 256.0 / n - 0.5;
            for (int x = 0; x < n; x++) {
                double u = texel ? x : (x + 0.5) * 256.0 / n - 0.5;
                Vec3 c = form == TexForm::Log ? texLevel(w, u, v, style) : texValue(w, u, v, style);
                float* p = &px[(size_t(y) * n + x) * 4];
                p[0] = c.x / 255.0f;
                p[1] = c.y / 255.0f;
                p[2] = c.z / 255.0f;
            }
        }
    };
    int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    if (n < 512) threads = 1;
    std::vector<std::thread> pool;
    for (int k = 0; k < threads; k++) pool.emplace_back(rows, n * k / threads, n * (k + 1) / threads);
    for (auto& th : pool) th.join();
}
}  // namespace

Vec3 texValue(Tex w, double u, double v, TexStyle style) {
    bool texel = style == TexStyle::Texel;
    if (texel) { u = std::floor(u); v = std::floor(v); }
    double s = std::sin(kPi * u / 128.0) + std::sin(kPi * v / 128.0);  // su + sv: sin(x / 256 * 2 * pi)
    return channels(w, cellBrightness(w, u, v, texel), s);
}

Vec3 texLevel(Tex w, double u, double v, TexStyle style) { return toLevel(w, texValue(w, u, v, style)); }

Textures::Textures(Ctx&) {}

Textures::~Textures() {
    for (auto& style : tex_)
        for (auto& form : style)
            for (Texture& t : form) t.destroy();
}

const Texture& Textures::get(Tex w, TexStyle style, TexForm form) {
    bool texel = style == TexStyle::Texel;
    Texture& t = tex_[texel ? 1 : 0][form == TexForm::Log ? 1 : 0][int(w)];
    if (t.id) return t;
    int n = texel ? 256 : kSize;
    std::vector<float> px;
    fill(w, style, form, n, px);
    t.createRaw(n, n, GL_RGBA16F, GL_RGBA, GL_FLOAT, px.data(), true, true, !texel);
    if (!texel) {
        gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        gl::GetError();  // anisotropy is optional
    }
    return t;
}

const Texture& Textures::grid(const Ctx& c, double t, TexStyle style, TexForm form) {
    return get(t < c.eventTime(5) ? Tex::Table1 : Tex::Table2, style, form);
}

const char* kTexGLSL = R"(
float cmCellR(vec2 uv, float n) { vec2 d = mod(uv, n) - 0.5 * n; return length(d); }
vec3 cmTexValue(int which, vec2 uv) {
    const float pi = 3.14159265359;
    float s = sin(pi * uv.x / 128.0) + sin(pi * uv.y / 128.0);
    float hi = 0.7 + 0.15 * s;
    vec3 x;
    if (which == 0) {        // map A
        float c = 168.0 - (cos(pi * cmCellR(uv, 32.0) * 0.03125) + 1.0) * 56.0;
        x = vec3(c * hi, c * (0.7 + 0.05 * s), 1.5 * c);
    } else if (which == 1) { // map B
        float c = (cos(pi * cmCellR(uv, 16.0) * 0.0625) + 1.0) * 56.0;
        x = vec3(c * hi, c * (0.7 + 0.05 * s), 1.5 * c);
    } else if (which == 2) { // map C
        float c = (cos(pi * cmCellR(uv, 8.0) * 0.125) + 1.0) * 48.0;
        x = vec3(c * (0.7 + 0.05 * s), c * hi, 1.5 * c);
    } else if (which == 3) { // table 1
        float c = (cos(pi * cmCellR(uv, 8.0) * 0.125) + 1.0) * 48.0;
        x = vec3(0.7 * c, c * (0.5 + 0.05 * s), c * hi);
    } else {                 // table 2
        float c = (cos(pi * cmCellR(uv, 32.0) * 0.03125) + 1.0) * 56.0;
        x = vec3(0.74 * c, c * (0.42 + 0.05 * s), c * hi);
    }
    vec3 v = 1.0 + x;
    if (which <= 2) v = max(v, vec3(128.0 * exp(-128.0 * 0.011)));
    return v / 255.0;
}
vec3 cmDecodeMap(vec3 L, float s) { return min(vec3(1.0), 128.0 * exp(0.011 * (L * 255.0 + s - 256.0)) / 255.0); }
vec3 cmDecodeTable(vec3 L, float s) { return min(vec3(1.0), exp((L * 255.0 + s - 256.0) / 23.0) / 255.0); }
)";

}  // namespace cmc
