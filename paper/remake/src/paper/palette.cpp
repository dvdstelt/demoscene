// Palette system: default palette 0x169a3, fade upload 0x15c0, hue generator 0x14dc, shade/multiply tables 0x144b
// (paper/re/notes-init.md §5.6, §6), in float. See palette.h.
#include "paper/palette.h"

#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "paper/common.h"

namespace paper {

static constexpr uint32_t kDefaultPalette = 0x169a3;

DefaultPalette::DefaultPalette(const PaperExe& exe) {
    for (int i = 0; i < 768; i++) raw6[i] = exe.u8(kDefaultPalette + i) & 63;
    for (int i = 0; i < 256; i++) rgb[i] = Vec3(raw6[3 * i] / 63.0f, raw6[3 * i + 1] / 63.0f, raw6[3 * i + 2] / 63.0f);
}

void rampOf(double index, int& first, int& last) {
    if (index < 64) { first = 0; last = 63; return; }
    first = std::min(224, int(index) & ~31);
    last = first + 31;
}

Vec3 DefaultPalette::at(double index) const {
    index = std::clamp(index, 0.0, 255.0);
    int first, last;
    rampOf(index, first, last);
    int i0 = int(std::floor(index));
    int i1 = std::min(i0 + 1, last);
    float f = float(index - i0);
    return rgb[i0] * (1 - f) + rgb[i1] * f;
}

// ---------------------------------------------------------------- 0x15c0
float levelFactor(double level) {
    double L = level / 256.0;
    return L <= 128 ? float(std::max(0.0, L) / 128.0) : 1.0f;
}

float levelWhite(double level) {
    double L = level / 256.0;
    return L <= 128 ? 0.0f : float((std::min(L, 255.0) - 128.0) / 127.0);
}

Vec3 applyLevel(Vec3 c, double level) {
    float f = levelFactor(level), w = levelWhite(level);
    c = c * f;
    return c * (1 - w) + Vec3(w, w, w);
}

int applyLevel6(int p, int level) {
    int L = level >> 8;
    if (L <= 128) return (p * L) >> 7;
    int b = std::min(L, 255) - 128;
    return ((((p * (127 - b)) >> 6) & 255) + b) >> 1;
}

// ---------------------------------------------------------------- 0x14dc
std::array<uint8_t, 768> huePalette6(int angle, int shift) {
    int32_t s = sinTab(angle & 1023) >> shift;
    int32_t c = sinTab((angle + 256) & 1023) >> shift;
    int R = (128 - s) & 255;
    int G = (128 + (s >> 1)) & 255, B = G;
    int d = (c * 0xde) >> 8;
    G = (G + d) & 255;
    B = (B - d) & 255;
    const int C[3] = {R, G, B};
    std::array<uint8_t, 768> p{};
    for (int ch = 0; ch < 128; ch++)
        for (int k = 0; k < 3; k++) p[3 * ch + k] = uint8_t((C[k] * ch) >> 9);
    for (int ch = 128; ch >= 1; ch--) {
        int w = (128 - ch) >> 1;
        for (int k = 0; k < 3; k++) p[3 * (256 - ch) + k] = uint8_t(((C[k] * ch) >> 9) + w);
    }
    return p;
}

Vec3 hueBase(double angle, int shift) {
    double amp = 65536.0 / double(1 << shift);
    double s = amp * sinTurn(angle), c = amp * cosTurn(angle);
    double R = 128 - s, G = 128 + s / 2 + c * 222.0 / 256.0, B = 128 + s / 2 - c * 222.0 / 256.0;
    return Vec3(float(R / 252.0), float(G / 252.0), float(B / 252.0));
}

Vec3 hueRamp(Vec3 base, double index) {
    double i = std::clamp(index, 0.0, 255.0);
    if (i <= 128) return base * float(i / 128.0);
    double k = i - 128;
    auto ch = [&](float b) { return float(std::min(1.0, b * (128 - k) / 128.0 + k / 126.0)); };
    return Vec3(ch(base.x), ch(base.y), ch(base.z));
}

// ---------------------------------------------------------------- 0x144b
double shadeIndex(double row, double c) {
    double k = row <= 32 ? row : (row - 32) * 0.625;  // ((ch-32)>>1) + ((ch-32)>>3)
    if (c < 64) return std::max(0.0, c + 2 * k - 64);
    double base = std::floor(c / 32) * 32;
    return base + std::max(0.0, (c - base) + k - 32);
}

double mulIndex(double row, double c) { return std::min(255.0, c * row / 32.0); }

// ---------------------------------------------------------------- GLSL
const char* kPaletteGLSL = R"(
// ---- paper palette functions (src/paper/palette.h)
vec3 palLookup(sampler2D pal, float index) {
    index = clamp(index, 0.0, 255.0);
    float first = index < 64.0 ? 0.0 : min(224.0, floor(index / 32.0) * 32.0);
    float last = index < 64.0 ? 63.0 : first + 31.0;
    float i0 = floor(index);
    float i1 = min(i0 + 1.0, last);
    return mix(texelFetch(pal, ivec2(int(i0), 0), 0).rgb, texelFetch(pal, ivec2(int(i1), 0), 0).rgb, index - i0);
}
vec3 hueRamp(vec3 base, float index) {
    float i = clamp(index, 0.0, 255.0);
    if (i <= 128.0) return base * (i / 128.0);
    float k = i - 128.0;
    return min(vec3(1.0), base * ((128.0 - k) / 128.0) + vec3(k / 126.0));
}
float shadeIndex(float row, float c) {
    float k = row <= 32.0 ? row : (row - 32.0) * 0.625;
    if (c < 64.0) return max(0.0, c + 2.0 * k - 64.0);
    float base = floor(c / 32.0) * 32.0;
    return base + max(0.0, c - base + k - 32.0);
}
float mulIndex(float row, float c) { return min(255.0, c * row / 32.0); }
vec3 applyLevel(vec3 c, float level) {
    float L = level / 256.0;
    if (L <= 128.0) return c * (max(L, 0.0) / 128.0);
    return mix(c, vec3(1.0), (min(L, 255.0) - 128.0) / 127.0);
}
)";

// ---------------------------------------------------------------- textures
void PaletteTex::upload(Texture& t, const std::array<Vec3, 256>& rgb) {
    float px[256 * 4];
    for (int i = 0; i < 256; i++) {
        px[4 * i] = rgb[i].x;
        px[4 * i + 1] = rgb[i].y;
        px[4 * i + 2] = rgb[i].z;
        px[4 * i + 3] = 1.0f;
    }
    if (!t.id) t.createRaw(256, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, px, false, false, false);
    else {
        gl::BindTexture(GL_TEXTURE_2D, t.id);
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_FLOAT, px);
    }
}

PaletteTex::PaletteTex(Ctx& c) { upload(def, DefaultPalette(*c.exe).rgb); }

void HueRampTex::update(double angle, int shift) {
    Vec3 base = hueBase(angle, shift);
    float px[256 * 4];
    for (int i = 0; i < 256; i++) {
        Vec3 v = hueRamp(base, i);
        px[4 * i] = v.x;
        px[4 * i + 1] = v.y;
        px[4 * i + 2] = v.z;
        px[4 * i + 3] = 1.0f;
    }
    if (!tex.id) tex.createRaw(256, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, px, true, false, false);
    else {
        gl::BindTexture(GL_TEXTURE_2D, tex.id);
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_FLOAT, px);
    }
}

}  // namespace paper
