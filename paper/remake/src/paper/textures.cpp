// Cloud texture 0x10d3 and paper grain 0x55e2, exact and at high resolution (paper/re/notes-init.md §5.4, §5.5).
// See textures.h.
#include "paper/textures.h"

#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "paper/common.h"

namespace paper {

static constexpr uint32_t kCloudSource = 0x16ca3;  // 64 rows x 32 bytes, 4-bit pixels, high nibble = left pixel

// ---------------------------------------------------------------- 0x10d3 exact
namespace {

// One four-neighbour pass of 0xf90 on the torus: (N + S + W + E) >> 2.
void crossPassWrap(const std::vector<uint8_t>& s, std::vector<uint8_t>& d) {
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int n = s[((y - 1) & 255) * 256 + x], so = s[((y + 1) & 255) * 256 + x];
            int w = s[y * 256 + ((x - 1) & 255)], e = s[y * 256 + ((x + 1) & 255)];
            d[y * 256 + x] = uint8_t((n + so + w + e) >> 2);
        }
}

int nibble(const PaperExe& exe, int row, int col) {
    uint8_t b = exe.u8(kCloudSource + row * 32 + col / 2);
    return (col & 1) ? (b & 15) : (b >> 4);
}

}  // namespace

std::vector<uint8_t> cloudTexture256(const PaperExe& exe) {
    std::vector<uint8_t> t(65536), a(65536), b(65536);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            uint8_t v = uint8_t(nibble(exe, y / 2, x / 2) * 16);  // each nibble 2x2 texels
            t[y * 256 + x] = v;
            t[y * 256 + 255 - x] = v;                             // mirrored horizontally
        }
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 256; x++) t[(128 + y) * 256 + x] = t[(127 - y) * 256 + 255 - x];  // lower half: 180 deg
    for (int it = 0; it < 3; it++) {                              // 3 x 0xf47 on the wrapped copy, read back at (2,2)
        crossPassWrap(t, a);
        crossPassWrap(a, b);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) t[y * 256 + x] = b[((y + 2) & 255) * 256 + ((x + 2) & 255)];
    }
    return t;
}

// ---------------------------------------------------------------- 0x10d3 high resolution
namespace {

// Catmull-Rom weights for the fraction f.
void catmullRom(double f, double w[4]) {
    w[0] = f * (-0.5 + f * (1.0 - 0.5 * f));
    w[1] = 1.0 + f * f * (-2.5 + 1.5 * f);
    w[2] = f * (0.5 + f * (2.0 - 1.5 * f));
    w[3] = f * f * (-0.5 + 0.5 * f);
}

// Nibble index of the 4-way mirrored, 256-periodic texture: even extension about -0.5 and 63.5 (period 128).
int reflectNibble(int i) {
    i &= 127;
    return i < 64 ? i : 127 - i;
}

// Toroidal 1D Gaussian along rows (stride 1) or columns (stride n) of an n x n image.
void gaussWrap(std::vector<float>& img, int n, double sigma, bool vertical) {
    int r = int(std::ceil(4 * sigma));
    std::vector<float> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; i++) sum += k[i + r] = float(std::exp(-0.5 * i * i / (sigma * sigma)));
    for (float& v : k) v = float(v / sum);
    std::vector<float> line(n + 2 * r), out(n);  // line[i + r] = pixel i (wrapped at both ends)
    for (int a = 0; a < n; a++) {
        for (int i = -r; i < n + r; i++) {
            int w = ((i % n) + n) % n;
            line[i + r] = vertical ? img[size_t(w) * n + a] : img[size_t(a) * n + w];
        }
        for (int i = 0; i < n; i++) {
            float s = 0;
            const float* p = &line[i];
            for (int j = 0; j <= 2 * r; j++) s += k[j] * p[j];
            out[i] = s;
        }
        for (int i = 0; i < n; i++) (vertical ? img[size_t(i) * n + a] : img[size_t(a) * n + i]) = out[i];
    }
}

}  // namespace

std::vector<uint16_t> cloudTextureHi(const PaperExe& exe, int n) {
    const int s = std::max(1, n / 256);
    // 1. the mirrored quadrant, Catmull-Rom interpolated at the high-res texel centres (separable)
    std::vector<int> idx(size_t(n) * 4);
    std::vector<double> wt(size_t(n) * 4);
    for (int c = 0; c < n; c++) {
        double X = std::fmod((c + 0.5) / s, 256.0);
        if (X > 128) X = 256 - X;            // the 4-way mirror (about 0 and 128 in 256-texel units)
        double u = (X - 1.0) / 2.0;          // nibble centres at 256-texel coordinates 2i + 1
        int i0 = int(std::floor(u));
        catmullRom(u - i0, &wt[size_t(c) * 4]);
        for (int k = 0; k < 4; k++) idx[size_t(c) * 4 + k] = reflectNibble(i0 - 1 + k);
    }
    std::vector<float> rows(size_t(64) * n), img(size_t(n) * n);
    for (int j = 0; j < 64; j++)
        for (int c = 0; c < n; c++) {
            double v = 0;
            for (int k = 0; k < 4; k++) v += wt[size_t(c) * 4 + k] * nibble(exe, j, idx[size_t(c) * 4 + k]) * 16.0;
            rows[size_t(j) * n + c] = float(v);
        }
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) {
            double v = 0;
            for (int k = 0; k < 4; k++) v += wt[size_t(r) * 4 + k] * rows[size_t(idx[size_t(r) * 4 + k]) * n + c];
            img[size_t(r) * n + c] = float(v);
        }
    // 2. the blur: 6 four-neighbour passes = variance 3 texels^2 per axis at 256, toroidal
    double sigma = std::sqrt(3.0) * s;
    gaussWrap(img, n, sigma, false);
    gaussWrap(img, n, sigma, true);
    // 3. the shift by (-6, -6) texels of the 256 texture
    std::vector<uint16_t> out(size_t(n) * n);
    int sh = 6 * s;
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) {
            float v = img[size_t((r + sh) % n) * n + (c + sh) % n] / 255.0f;
            out[size_t(r) * n + c] = uint16_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f));
        }
    return out;
}

// ---------------------------------------------------------------- 0x55e2 paper grain
namespace {

// 0x3ed1 calls of rand() & 0x0f0f0f0f, stored little endian: 64324 bytes of 4-bit noise.
std::vector<uint8_t> grainNoise(const PaperExe& exe) {
    Rng r = Rng::at(exe, RngPos::kPaperGrain);
    std::vector<uint8_t> n(0x3ed1 * 4);
    for (size_t k = 0; k < 0x3ed1; k++) {
        uint32_t v = r.next() & 0x0f0f0f0fu;
        for (int b = 0; b < 4; b++) n[4 * k + b] = uint8_t(v >> (8 * b));
    }
    return n;
}

}  // namespace

std::vector<uint8_t> paperGrainBytes(const PaperExe& exe) {
    std::vector<uint8_t> n = grainNoise(exe), g(64000);
    for (int i = 0; i < 64000; i++) g[i] = uint8_t(30 + (int8_t(uint8_t(n[i] - n[i + 321])) >> 3));
    return g;
}

std::vector<float> paperGrainSmooth(const PaperExe& exe) {
    std::vector<uint8_t> n = grainNoise(exe);
    std::vector<float> g(64000);
    // The truncation to 4 levels adds about 1/12 of variance (uniform residual) to the 42.5/64 of the difference;
    // the deviations are scaled so that the grain keeps the original's contrast.
    const double k = std::sqrt(1.0 + (1.0 / 12.0) / (42.5 / 64.0));
    for (int i = 0; i < 64000; i++) g[i] = float(30.0 - 7.0 / 16.0 + k * (int(n[i]) - int(n[i + 321])) / 8.0);
    return g;
}

// ---------------------------------------------------------------- GPU
CoreTextures::CoreTextures(Ctx& c) {
    const PaperExe& exe = *c.exe;
    std::vector<uint16_t> hi = cloudTextureHi(exe, kCloudSize);
    cloud.createRaw(kCloudSize, kCloudSize, GL_R16, GL_RED, GL_UNSIGNED_SHORT, hi.data(), true, true, true);
    std::vector<uint8_t> lo = cloudTexture256(exe);
    cloud256.createRaw(256, 256, GL_R8, GL_RED, GL_UNSIGNED_BYTE, lo.data(), false, true, false);
    std::vector<float> g = paperGrainSmooth(exe);
    grain.createRaw(320, 200, GL_R32F, GL_RED, GL_FLOAT, g.data(), true, true, false);
}

const char* kTexturesGLSL = R"(
// ---- paper textures (src/paper/textures.h)
float paperGrain(sampler2D grain, vec2 vga) { return texBicubic(grain, vga / vec2(320.0, 200.0)).r; }
// Adds a little detail below the original pixel (smooth value noise at 3 cells per original pixel, about a third of
// the grain's own deviation), so that very large output sizes do not show the grain as soft blobs only.
float paperGrainFine(sampler2D grain, vec2 vga, float amount) {
    vec2 p = vga * 3.0;
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i), b = hash12(i + vec2(1, 0)), c = hash12(i + vec2(0, 1)), d = hash12(i + vec2(1, 1));
    float n = mix(mix(a, b, f.x), mix(c, d, f.x), f.y) - 0.5;
    return paperGrain(grain, vga) + amount * n;
}
// grey ramp 0..63 of the default palette: entry i = grey i-1 up to 52, i from 53 (6-bit), linear in between
float greyRamp(float i) {
    i = clamp(i, 0.0, 63.0);
    float g = i <= 1.0 ? 0.0 : (i <= 52.0 ? i - 1.0 : (i < 53.0 ? mix(51.0, 53.0, i - 52.0) : i));
    return g / 63.0;
}
vec3 pageBack(sampler2D grain, vec2 vga) { return vec3(greyRamp(paperGrain(grain, vga))); }
vec3 paperFront(sampler2D grain, vec2 vga) { return vec3(greyRamp(max(0.0, 62.0 + 2.0 * paperGrain(grain, vga) - 64.0))); }
)";

}  // namespace paper
