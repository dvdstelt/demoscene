// The clone meets clone core library (core.h): the random generator 0x420491, ramps and the overlay fade levels,
// the log encoders 0x4034e7 / 0x4167b7, the tile colours 0x428164, palette textures.
#include "clone/core.h"

#include <algorithm>
#include <vector>

#include "exe.h"

namespace cmc {

Random::Random(const CloneExe& exe) : s(exe.u32(0x420491)) {}

void Random::skip(uint64_t n) {
    // s -> a s + c applied n times = (a^n) s + c (a^(n-1) + ... + 1): square-and-multiply on the affine maps mod 2^32
    uint32_t mul = 1, add = 0, a = 0x41c64e6du, c = 0x3039u;
    while (n) {
        if (n & 1) { mul *= a; add = add * a + c; }
        c = c * (a + 1);
        a = a * a;
        n >>= 1;
    }
    s = s * mul + add;
}

double Ramp::at(const Ctx& c, double t) const {
    return std::clamp(v0 + rate * (c.t70(t) - c.t70(t0)), lo, hi);
}

// Initial values from the image: [0x422514] 63.0 (title), [0x42250c] 63.0 (credits), [0x428464] 1.0 (part 11).
double titleLevel(const Ctx& c, double t) {
    return Ramp{c.eventTime(106), c.exe->f32(0x422514), -0.45, 0.0, 1e9}.at(c, std::max(t, c.eventTime(106)));
}

double creditsLevel(const Ctx& c, double t) {
    return Ramp{c.eventTime(107), c.exe->f32(0x42250c), -0.75, 0.0, 1e9}.at(c, std::max(t, c.eventTime(107)));
}

double tilesTitleLevel(const Ctx& c, double t) {
    double f = Ramp{c.eventTime(1101), c.exe->f32(0x428464), -0.0065, 0.0, 1e9}.at(c, std::max(t, c.eventTime(1101)));
    return (1.0 - f) * 63.0;
}

int enc23(float x) {
    long double v = std::log(1.0L + x) * 23.0L + 128.0L;  // 0x41a14c 23.0, 0x41a154 128.0
    if (!(v > -2147483648.0L) || v >= 2147483648.0L) return 0;  // the x87's invalid result 0x80000000 -> 0
    return std::clamp(int(v), 0, 255);                           // fistp with truncation, then the clamp
}

int enc90(float x) {
    long double v = std::log((1.0L + x) * 0.0078125L) * 90.9090909090909L + 128.0L;  // 0x41a490, 0x41a498, 0x41a4a0
    if (!(v > -2147483648.0L) || v >= 2147483648.0L) return 0;
    return std::clamp(int(v), 0, 255);
}

Vec3 tileColour(const CloneExe& exe, int idx) {
    if (idx < 0 || idx + 2 >= 3 * kTileEntries) return Vec3(0, 0, 0);
    auto ch = [&](int k) { return float(exe.s32(0x428164 + 4 * uint32_t(idx + k))) * 0.0125f; };
    return Vec3(ch(0), ch(1), ch(2));
}

void PaletteTexture::set(const Colours& p) {
    std::vector<float> px(256 * 4);
    for (int i = 0; i < 256; i++) {
        px[size_t(i) * 4 + 0] = p[size_t(i)].x;
        px[size_t(i) * 4 + 1] = p[size_t(i)].y;
        px[size_t(i) * 4 + 2] = p[size_t(i)].z;
        px[size_t(i) * 4 + 3] = 1.0f;
    }
    if (!tex.id) {
        tex.createRaw(256, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, px.data(), false, false, false);
    } else {
        gl::BindTexture(GL_TEXTURE_2D, tex.id);
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_FLOAT, px.data());
    }
}

const char* kGLSL = R"(
vec3 cmPal(sampler2D pal, float i) { return texelFetch(pal, ivec2(int(floor(i + 0.5)) & 255, 0), 0).rgb; }
vec3 cmPalLerp(sampler2D pal, float i) {
    float c = clamp(i, 0.0, 255.0);
    int a = int(floor(c));
    return mix(texelFetch(pal, ivec2(a, 0), 0).rgb, texelFetch(pal, ivec2(min(a + 1, 255), 0), 0).rgb, c - float(a));
}
float cmGain23(float k) { return exp((k - 128.0) / 23.0); }
float cmGain90(float k) { return exp((k - 128.0) * 0.011); }
)";

}  // namespace cmc
