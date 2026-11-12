// The STARS core library (core.h): the random generator 0x4af10, the sine tables, the palette fader 0x17e96 /
// 0x4a80b and palette textures.
#include "stars/core.h"

#include <algorithm>
#include <vector>

#include "exe.h"

namespace stars {

Random::Random(const StarsExe& exe) : s(exe.u32(0x4af35)) {}

int32_t SineTable::at(const StarsExe& exe, int i) const {
    int k = ((i % entries) + entries) % entries;
    uint32_t a = addr + uint32_t(k * bytes);
    return bytes == 2 ? exe.s16(a) : exe.s32(a);
}

double Fade::level(double t) const {
    double k = std::clamp((t - t0) * rate, 0.0, double(count));
    return from + step * k;
}

double Fade::levelStepped(double t) const {
    double k = std::clamp(std::floor((t - t0) * rate), 0.0, double(count));
    return from + step * k;
}

void PaletteTexture::set(const Palette& p) {
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
vec3 stPal(sampler2D pal, float i) { return texelFetch(pal, ivec2(int(floor(i + 0.5)) & 255, 0), 0).rgb; }
vec3 stPalLerp(sampler2D pal, float i) {
    float c = clamp(i, 0.0, 255.0);
    int a = int(floor(c));
    return mix(texelFetch(pal, ivec2(a, 0), 0).rgb, texelFetch(pal, ivec2(min(a + 1, 255), 0), 0).rgb, c - float(a));
}
float stSinA(float i) { return 65528.0 * sin(6.283185307179586 * i / 4096.0); }
)";

}  // namespace stars
