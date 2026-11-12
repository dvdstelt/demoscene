// The DOPE core library (core.h): the sine tables, per-retrace integrals, the label slide, the weight-table and
// palette-scale routines 0b00 / 2550 and their float forms, fades, palette textures.
#include "dope/core.h"

#include <algorithm>

#include "exe.h"

namespace dope {

int16_t sineEntry(const DopeImage& img, uint16_t quarter, int i) {
    i = ((i % 2560) + 2560) % 2560;
    if (i >= 2048) i -= 2048;                     // the cosine overrun repeats 0..511
    bool neg = i >= 1024;
    if (neg) i -= 1024;
    int q = i < 512 ? i : 1023 - i;               // the mirror: [1023 - i] = [i]
    int v = img.css16(uint16_t(quarter + 2 * q));
    return int16_t(neg ? -v : v);
}

double Integrator::at(double r) const {
    if (r <= 0) {
        // before the first call: the value set at k = 0 counts
        double v = v0;
        for (const Set& s : sets)
            if (s.k <= 0 && s.hasValue) v = s.value;
        return v;
    }
    double v = v0, vel = vel0;
    int n = int(std::floor(r));
    double frac = r - n;
    int done = 0;  // calls applied
    size_t e = 0;
    auto apply = [&](int k) {
        while (e < sets.size() && sets[e].k <= k) {
            if (sets[e].hasValue) v = sets[e].value;
            if (sets[e].hasVelocity) vel = sets[e].velocity;
            e++;
        }
    };
    while (done < n) {
        apply(done);
        // calls done + 1 .. up to the next event or n run with this velocity
        int next = e < sets.size() ? std::min(n, sets[e].k) : n;
        if (next <= done) next = done + 1;
        v += vel * (next - done);
        done = next;
    }
    apply(n);
    return v + vel * frac;
}

int LabelSlide::at(int n) const {
    int a = a0, v = 0;
    size_t e = 0;
    for (int k = 0; k < n; k++) {  // call k + 1
        while (e < speeds.size() && speeds[e].first <= k) v = speeds[e++].second;
        a += v;
        if (a < 0 || a >= 75) v = 0;
    }
    return a;
}

double LabelSlide::position(double r) const {
    int n = int(std::floor(r));
    double f = r - n;
    int a = at(std::max(0, n));
    if (r < 0) return a;
    int b = at(n + 1);
    return a + f * (b - a);
}

std::array<uint8_t, 64> weightTable(int dx) {
    if (int16_t(dx) < 0) dx = 0;
    dx = (dx & 0xffff) >> 4;
    if (dx >= 0x100) dx = 0x100;
    std::array<uint8_t, 64> t{};
    unsigned ax = 0;
    for (int i = 0; i < 64; i++) {
        t[size_t(i)] = uint8_t(ax >> 8);
        if (ax <= 0x3f00) ax += unsigned(dx);
    }
    return t;
}

Vec3 mixColour(Vec3 a, Vec3 b, double w) {
    float f = float(w);
    return a * (1.0f - f) + b * f;
}

Palette mixPalettes(const Palette& a, const Palette& b, double w) {
    Palette p;
    for (size_t i = 0; i < 256; i++) p[i] = mixColour(a[i], b[i], w);
    return p;
}

std::array<uint8_t, 64> scaleTable(int start, int step) {
    step = std::clamp(step, 0, 0x100);
    std::array<uint8_t, 64> t{};
    unsigned ax = unsigned(start) & 0xffff;
    for (int i = 0; i < 64; i++) {
        t[size_t(i)] = uint8_t(ax >> 8);
        if (ax <= 0x3f00) ax = (ax + unsigned(step)) & 0xffff;
    }
    return t;
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
vec3 dpPal(sampler2D pal, float i) { return texelFetch(pal, ivec2(int(floor(i + 0.5)) & 255, 0), 0).rgb; }
vec3 dpPalLerp(sampler2D pal, float i) {
    float c = clamp(i, 0.0, 255.0);
    int a = int(floor(c));
    return mix(texelFetch(pal, ivec2(a, 0), 0).rgb, texelFetch(pal, ivec2(min(a + 1, 255), 0), 0).rgb, c - float(a));
}
)";

}  // namespace dope
