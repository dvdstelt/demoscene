// The JIZZ core library, part 1: the random generator 0x141f6 (with the seeds of 0x11dfe and 0x12e72), the precalc
// functions 0x6592 (spot table) and 0x16f4f (polar table) as continuous maths, the engine colour tables 0x1a336 /
// 0x1a229 and the inversion decay of 0x638b, the fades 0x1131b / 0x179f6 / 0x17979 / 0x17999, the inset frame
// 0x1151c and the GLSL helpers. Part 2 (lines, jittered quads, glow) is glow.cpp. See core.h.
#include "jizz/core.h"

#include <algorithm>
#include <cmath>

#include "exe.h"
#include "jizz/coregl.h"

namespace jizz {

// =============================================================================================== random generator
static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

uint32_t Rng::next() {  // 0x141f6
    uint32_t eax = a + 0x0e372471u, ebx = b + 0x00bc5ea9u, ecx = c + 0x00c8ade7u, edx;
    eax ^= ebx;
    int64_t p = int64_t(int32_t(eax)) * int64_t(int32_t(ecx));  // imul ecx: signed 32x32 -> 64
    eax = uint32_t(uint64_t(p));
    edx = uint32_t(uint64_t(p) >> 32);
    edx = rol(edx, 13) + eax;
    ecx = rol(ecx, 14);
    edx -= ebx;
    eax ^= edx;
    ebx += ecx;
    ecx ^= edx;
    eax -= ebx;
    ebx = rol(ebx, 9);
    uint64_t q = uint64_t(eax) * uint64_t(ecx);  // mul ecx: unsigned
    eax = uint32_t(q);
    edx = uint32_t(q >> 32);
    ebx ^= edx;
    eax += 0x01e42ac3u;
    ebx += 0x005edf04u;
    ecx += 0x011a2ec1u;
    a = ebx;  // the state words rotate
    b = ecx;
    c = eax;
    return eax;
}

Rng& Rng::skip(uint64_t calls) {
    while (calls--) next();
    return *this;
}

Rng Rng::afterPrecalc() { return Rng().skip(kPrecalcCalls); }

Rng Rng::textureSeed(const JizzExe& exe) {
    // 0x12e73 / 0x12e7d / 0x12e87: mov dword [0x141ea / 0x141ee / 0x141f2], imm32 (the immediate at +6)
    return Rng(exe.u32(0x12e73 + 6), exe.u32(0x12e7d + 6), exe.u32(0x12e87 + 6));
}

Rng Rng::afterTextures(const JizzExe& exe) { return textureSeed(exe).skip(kTextureCalls); }

std::array<uint32_t, 80> precalcRandoms() {
    std::array<uint32_t, 80> r{};
    Rng g;
    for (uint32_t& v : r) v = g.next();
    return r;
}

// =============================================================================================== precalc functions
float spot(float r) {  // 0x6592: (255 - r)^4 / 2^24
    float v = std::max(255.0f - r, 0.0f);
    v *= v;
    return v * v * (1.0f / 16777216.0f);
}

float spotMap(float i, float j) {
    i -= 256.0f * std::floor(i / 256.0f);
    j -= 256.0f * std::floor(j / 256.0f);
    return spot(std::hypot(i - 127.0f, j - 128.0f));  // centre at column 127, row 128
}

void polar(float x, float y, float& u, float& v) {  // 0x16f4f
    u = std::atan2(y, x) * float(256.0 / 3.14159265358979323846);
    v = 1.3f * std::sqrt(x * x + y * y);
}

const char* kGLSL = R"(
// ---- jizz core (src/jizz/core.h)
float jzSpot(float r) { float v = max(255.0 - r, 0.0); v *= v; return v * v * (1.0 / 16777216.0); }
float jzSpotMap(vec2 t) { t = mod(t, 256.0); return jzSpot(length(t - vec2(127.0, 128.0))); }
vec2 jzPolar(vec2 p) { return vec2(atan(p.y, p.x) * (256.0 / PI), 1.3 * length(p)); }
mat2 jzPolarJacobian(vec2 p) {
    float r2 = max(dot(p, p), 1e-8), r = sqrt(r2);
    const float k = 256.0 / PI;
    return mat2(-p.y * k / r2, 1.3 * p.x / r,    // d(u, v) / dx
                 p.x * k / r2, 1.3 * p.y / r);   // d(u, v) / dy
}
vec4 jzTex256(sampler2D m, vec2 t) { return texture(m, (t + 0.5) * (1.0 / 256.0)); }
vec4 jzTex256Grad(sampler2D m, vec2 t, vec2 dtdx, vec2 dtdy) {
    return textureGrad(m, (t + 0.5) * (1.0 / 256.0), dtdx * (1.0 / 256.0), dtdy * (1.0 / 256.0));
}
vec4 jzTexPolar(sampler2D m, vec2 p, vec2 off) {
    mat2 J = jzPolarJacobian(p);
    return jzTex256Grad(m, jzPolar(p) + off, J * dFdx(p), J * dFdy(p));
}
vec3 jzColourTable(vec3 i, vec3 esf) {
    vec3 x = clamp(i * (esf.y / 256.0) + esf.x / 255.0, 0.0, 1.0);
    return mix(x, 1.0 - x, esf.z / 256.0);
}
vec3 jzFade(vec3 c, vec3 C, float v) { return mix(c, C, v / 32.0); }
float jzGlowValue(sampler2D a, sampler2D b, float f, vec2 uv) { return mix(texture(a, uv).r, texture(b, uv).r, f); }
)";

// =============================================================================================== colour tables
Vec3 ColourTable::apply(Vec3 texel, float shade) const {
    auto ch = [&](float i) {
        float x = std::clamp((i + shade) * (S / 256.0f) + E / 255.0f, 0.0f, 1.0f);
        return x + (1.0f - 2.0f * x) * (F / 256.0f);  // mix(x, 1 - x, F / 256)
    };
    return {ch(texel.x), ch(texel.y), ch(texel.z)};
}

void ColourTable::set(const Shader& s, const char* uniform) const { s.set(uniform, E, S, F); }

float inversionDecay(double t, double t0, double rate, int f0, int dec, bool blend) {
    if (t < t0) return 0.0f;
    double s = (t - t0) * rate;
    double k = std::floor(s);
    auto F = [&](double n) { return float(std::max(0.0, f0 - n * dec)); };  // 0x638b: F -= dec, 0 when F < dec
    return blend ? F(k) + (F(k + 1) - F(k)) * float(s - k) : F(k);
}

// =============================================================================================== fades
double timerFadeLevel(const Ctx& c, double t, double t0, uint32_t rate) {
    if (t < t0) return 0.0;
    double lv = 1023.0 - (c.timer(t) - c.timer(t0)) * double(rate) / 4294967296.0;  // 0x17999
    return lv > 0.0 ? lv : 0.0;
}

void timerFade(Ctx& c, double t, double t0, uint32_t rate, Vec3 C) {
    double lv = timerFadeLevel(c, t, t0, rate);
    if (lv > 0.0) pageFade(c, float(lv / 1024.0), C);
}

void pageFade(Ctx& c, float amount, Vec3 C) {
    if (amount <= 0.0f) return;
    CoreGL& g = c.shared<CoreGL>();
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_SCISSOR_TEST);
    setBlend(Blend::Alpha);
    g.fill.use();
    g.fill.set("col", C.x, C.y, C.z, std::min(amount, 1.0f));
    drawFullscreen();
    setBlend(Blend::None);
}

// =============================================================================================== insets
Mat4 Inset::clip() const {
    float sx = w / 320.0f, sy = h / 200.0f;
    float tx = (x + 0.5f * w) / 160.0f - 1.0f, ty = 1.0f - (y + 0.5f * h) / 100.0f;
    return Mat4::translate(tx, ty, 0.0f) * Mat4::scale(sx, sy, 1.0f);
}

void Inset::scissor(const Ctx& c) const {
    if (full()) { noScissor(); return; }
    const Target* t = c.out;
    int W = t ? t->w : c.w, H = t ? t->h : c.h;
    float kx = W / 320.0f, ky = H / 200.0f;
    int x0 = int(std::floor(x * kx)), x1 = int(std::ceil((x + w) * kx));
    int y0 = int(std::floor(y * ky)), y1 = int(std::ceil((y + h) * ky));
    gl::Enable(GL_SCISSOR_TEST);
    gl::Scissor(x0, H - y1, x1 - x0, y1 - y0);  // GL rows count from the bottom
}

void Inset::noScissor() { gl::Disable(GL_SCISSOR_TEST); }

void aaRect(Ctx& c, float x, float y, float w, float h, Vec3 col, float alpha) {
    if (w <= 0 || h <= 0 || !c.out) return;
    CoreGL& g = c.shared<CoreGL>();
    float kx = c.out->w / 320.0f, ky = c.out->h / 200.0f;
    gl::Disable(GL_DEPTH_TEST);
    setBlend(Blend::Alpha);
    g.rect.use();
    g.rect.set("size", float(c.out->w), float(c.out->h));
    g.rect.set("r", x * kx, y * ky, (x + w) * kx, (y + h) * ky);
    g.rect.set("col", col.x, col.y, col.z, alpha);
    drawQuad();
    setBlend(Blend::None);
}

void insetFrame(Ctx& c, const Inset& in, Vec3 col) {  // 0x1151c
    if (in.x == 0 && in.y == 0) return;               // [0x15c3b] == 0: no frame
    aaRect(c, in.x - 1, in.y - 1, in.w + 2, 1, col);  // top
    aaRect(c, in.x - 1, in.y + in.h, in.w + 2, 1, col);  // bottom
    aaRect(c, in.x - 1, in.y, 1, in.h, col);          // left
    aaRect(c, in.x + in.w, in.y, 1, in.h, col);       // right
}

// =============================================================================================== shared GL objects
static const char* kRectVS = R"(
uniform vec4 r;      // x0, y0, x1, y1 in target pixels, y down
uniform vec2 size;   // target size
out vec2 p;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    p = mix(r.xy - 1.0, r.zw + 1.0, c);
    gl_Position = vec4(p.x / size.x * 2.0 - 1.0, 1.0 - p.y / size.y * 2.0, 0.0, 1.0);
}
)";
static const char* kRectFS = R"(
in vec2 p;
out vec4 o;
uniform vec4 r;
uniform vec4 col;
void main() {   // exact box-filter coverage of the pixel
    vec2 a = max(p - 0.5, r.xy), b = min(p + 0.5, r.zw);
    float cov = max(b.x - a.x, 0.0) * max(b.y - a.y, 0.0);
    o = vec4(col.rgb, col.a * cov);
}
)";
static const char* kFillFS = R"(
in vec2 uv;
out vec4 o;
uniform vec4 col;
void main() { o = col; }
)";

void prepare(Ctx& c) { c.shared<CoreGL>(); }

CoreGL::CoreGL(Ctx&) {
    rect.build(kRectVS, kRectFS, "jizz rect");
    fill.build(kFullscreenVS, kFillFS, "jizz fill");
    buildGlow();
}

}  // namespace jizz
