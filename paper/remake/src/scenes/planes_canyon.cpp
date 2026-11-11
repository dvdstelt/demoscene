// The canyon of the paper-plane parts (0x68a6 texture, 0x2f24 ray grid, 0x3095 block mapper, per pixel) and the
// matrix interpolation they share. See planes_canyon.h.
#include "scenes/planes_canyon.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "exe.h"
#include "paper/common.h"
#include "paper/palette.h"
#include "paper/textures.h"

using namespace paper;

namespace planes {

std::array<Vec3, 32> tealRamp(const PaperExe& exe) {
    std::array<Vec3, 32> r;
    const uint8_t* p = exe.at(0x681a, 96);
    for (int i = 0; i < 32; i++) r[size_t(i)] = Vec3(p[3 * i] / 63.0f, p[3 * i + 1] / 63.0f, p[3 * i + 2] / 63.0f);
    return r;
}

std::array<Vec3, 32> blueRamp(const PaperExe& exe) {
    DefaultPalette pal(exe);
    std::array<Vec3, 32> r;
    for (int i = 0; i < 32; i++) r[size_t(i)] = pal.rgb[size_t(128 + i)];
    return r;
}

double tealPlaneDistance(const PaperExe& exe) { return exe.f32(0x23e4); }

std::vector<uint8_t> canyonGrainBits(const PaperExe& exe) {
    // 0x68a6: one random dword per 4 texels (little-endian bytes), bit 1 of each byte
    Rng rng = Rng::at(exe, RngPos::kCanyonTex);
    std::vector<uint8_t> g(65536);
    for (size_t k = 0; k < 16384; k++) {
        const uint32_t r = rng.next();
        for (size_t b = 0; b < 4; b++) g[4 * k + b] = uint8_t((r >> (8 * b + 1)) & 1);
    }
    return g;
}

// Catmull-Rom upsampling of a toroidal 256x256 field by `scale` (texel centres as GL samples them: output texel i
// is at (i + 0.5) / scale - 0.5 in source texels).
static std::vector<float> upsampleCatmullRom(const std::vector<float>& src, int scale) {
    const int n = 256, N = n * scale;
    struct Tap { int k0; float w[4]; };
    std::vector<Tap> taps(static_cast<size_t>(N));
    for (int i = 0; i < N; i++) {
        const double x = (i + 0.5) / scale - 0.5, k = std::floor(x), f = x - k;
        Tap& t = taps[size_t(i)];
        t.k0 = int(k) - 1;
        t.w[0] = float(f * (-0.5 + f * (1.0 - 0.5 * f)));
        t.w[1] = float(1.0 + f * f * (-2.5 + 1.5 * f));
        t.w[2] = float(f * (0.5 + f * (2.0 - 1.5 * f)));
        t.w[3] = float(f * f * (-0.5 + 0.5 * f));
    }
    auto at = [&](int k) { return size_t(((k % n) + n) % n); };
    std::vector<float> rows(size_t(n) * size_t(N)), out(size_t(N) * size_t(N));
    for (int y = 0; y < n; y++)
        for (int i = 0; i < N; i++) {
            const Tap& t = taps[size_t(i)];
            float v = 0;
            for (int j = 0; j < 4; j++) v += t.w[j] * src[size_t(y) * size_t(n) + at(t.k0 + j)];
            rows[size_t(y) * size_t(N) + size_t(i)] = v;
        }
    for (int i = 0; i < N; i++) {
        const Tap& t = taps[size_t(i)];
        for (int x = 0; x < N; x++) {
            float v = 0;
            for (int j = 0; j < 4; j++) v += t.w[j] * rows[at(t.k0 + j) * size_t(N) + size_t(x)];
            out[size_t(i) * size_t(N) + size_t(x)] = v;
        }
    }
    return out;
}

static const char* kCanyonFS = R"(
in vec2 uv;
out vec4 frag;
uniform vec2 uSize;               // target size in pixels
uniform vec3 uR0, uR1, uR2;       // camera rows (world): right, down, forward
uniform vec3 uPos;                // camera position
uniform float uH;                 // planes z = +-H
uniform vec3 uRamp[32];           // palette entries 128..159
uniform sampler2D uCloud;         // smooth high-resolution 0x10d3 (byte / 255), repeat
uniform sampler2D uGrain;         // the random +2 bits (0 / 1), Catmull-Rom upsampled, repeat
uniform sampler2D uLevel;         // exact texel levels / 255 (nearest), PLANES_EXACT
uniform int uExact;
uniform vec2 uGrainCode;          // grain = texel * range - bias
uniform vec3 uK;                  // 0x23d8 / 0x23dc / 0x23e0: -20, -12.51, 28

// integral of floor() from 0 to x
float floorInt(float x) { float f = floor(x); return f * (x - f) + 0.5 * f * (f - 1.0); }
// floor() box-filtered over [x - w/2, x + w/2]
float stairBox(float x, float w) {
    if (w < 1e-3) return floor(x);
    return (floorInt(x + 0.5 * w) - floorInt(x - 0.5 * w)) / w;
}
vec3 ramp(float idx) {
    idx = clamp(idx, 0.0, 31.0);
    int k = int(floor(idx));
    return mix(uRamp[k], uRamp[min(k + 1, 31)], idx - float(k));
}

void main() {
    // VGA position of this pixel; the original pixel (px, py) shows the ray of its corner (px, py)
    vec2 vga = vec2(gl_FragCoord.x / uSize.x * 320.0, 200.0 - gl_FragCoord.y / uSize.y * 200.0) - 0.5;
    vec3 d = (vga.x / 8.0 + uK.x) * uR0 + (vga.y / 8.0 + uK.y) * uR1 + uK.z * uR2;
    float dz = abs(d.z) < 1e-9 ? 1e-9 : d.z;
    float t = (uH - uPos.z) / dz;
    if (t < 0.0) t = (-uH - uPos.z) / dz;
    t = min(t, 1e7);
    vec2 tc = uK.y * (uPos.xy + t * d.xy) / 256.0;   // (column, row) in texels of the 256 texture
    vec2 st = tc / 256.0;
    if (uExact == 1) {
        float s = clamp(8.0 * (1024.0 - floor(t + 0.5)), 2048.0, 8192.0);
        float dark = 32.0 - floor(s / 256.0);
        ivec2 tx = ivec2(mod(floor(tc), 256.0));
        float L = floor(texelFetch(uLevel, tx, 0).r * 255.0 + 0.5);
        frag = vec4(uRamp[int(max(0.0, L - dark))], 1.0);
        return;
    }
    float x = texture(uCloud, st).r * (255.0 / 32.0);
    float q = clamp(stairBox(x, length(vec2(dFdx(x), dFdy(x)))), 0.0, 7.0);
    float g = texture(uGrain, st).r * uGrainCode.x - uGrainCode.y;
    float L = 11.0 + q + 2.0 * g;
    float fog = min(t / 32.0 + 0.5, 24.0);
    frag = vec4(ramp(max(0.0, L - fog)), 1.0);
}
)";

Canyon::Canyon(Ctx& c) {
    exact_ = std::getenv("PLANES_EXACT") != nullptr;
    for (int i = 0; i < 3; i++) k_[i] = c.exe->f32(0x23d8 + 4 * uint32_t(i));
    shader_.build(kFullscreenVS, kCanyonFS, "planes canyon");
    std::vector<uint8_t> bits = canyonGrainBits(*c.exe);
    std::vector<float> g(65536);
    std::vector<uint8_t> lv(65536);
    const std::vector<uint8_t> T = cloudTexture256(*c.exe);
    for (size_t i = 0; i < 65536; i++) {
        g[i] = float(bits[i]);
        lv[i] = uint8_t(11 + ((T[i] >> 5) & 7) + 2 * bits[i]);
    }
    // Catmull-Rom of 0/1 data stays within -0.281..1.281 (2D separable): stored as (g + 0.3) / 1.6 in 8 bits
    const std::vector<float> gHi = upsampleCatmullRom(g, kGrainScale);
    std::vector<uint8_t> g8(gHi.size());
    for (size_t i = 0; i < gHi.size(); i++)
        g8[i] = uint8_t(std::lround(std::clamp((gHi[i] + kGrainBias) / kGrainRange, 0.0f, 1.0f) * 255.0f));
    grain_.createRaw(256 * kGrainScale, 256 * kGrainScale, GL_R8, GL_RED, GL_UNSIGNED_BYTE, g8.data(), true, true, true);
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    gl::GetError();  // anisotropy is optional
    level256_.createRaw(256, 256, GL_R8, GL_RED, GL_UNSIGNED_BYTE, lv.data(), false, true, false);
    c.shared<CoreTextures>();
}

void Canyon::draw(Ctx& c, const CanyonCamera& cam, const std::array<Vec3, 32>& ramp) {
    CoreTextures& tx = c.shared<CoreTextures>();
    GLint vp[4] = {0, 0, 1, 1};
    gl::GetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
    setBlend(Blend::None);
    shader_.use();
    tx.cloud.bind(0);
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);  // the core's texture: restored below
    gl::GetError();
    grain_.bind(1);
    level256_.bind(2);
    shader_.set("uCloud", 0);
    shader_.set("uGrain", 1);
    shader_.set("uLevel", 2);
    shader_.set("uExact", exact_ ? 1 : 0);
    shader_.set("uGrainCode", kGrainRange, kGrainBias);
    shader_.set("uSize", float(vp[2]), float(vp[3]));
    const Mat3d& m = cam.rot;
    shader_.set("uR0", float(m(0, 0)), float(m(0, 1)), float(m(0, 2)));
    shader_.set("uR1", float(m(1, 0)), float(m(1, 1)), float(m(1, 2)));
    shader_.set("uR2", float(m(2, 0)), float(m(2, 1)), float(m(2, 2)));
    shader_.set("uPos", float(cam.pos.x), float(cam.pos.y), float(cam.pos.z));
    shader_.set("uH", float(cam.H));
    shader_.set("uK", k_[0], k_[1], k_[2]);
    float rp[96];
    for (int i = 0; i < 32; i++) {
        rp[3 * i] = ramp[size_t(i)].x;
        rp[3 * i + 1] = ramp[size_t(i)].y;
        rp[3 * i + 2] = ramp[size_t(i)].z;
    }
    gl::Uniform3fv(shader_.loc("uRamp"), 32, rp);
    drawFullscreen();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, tx.cloud.id);
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 1.0f);
    gl::GetError();
}

// ---------------------------------------------------------------- matrix interpolation
namespace {

Mat3d inverse(const Mat3d& m) {
    const double a = m(0, 0), b = m(0, 1), c = m(0, 2), d = m(1, 0), e = m(1, 1), f = m(1, 2), g = m(2, 0),
                 h = m(2, 1), i = m(2, 2);
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    Mat3d r;
    r(0, 0) = A / det; r(0, 1) = -(b * i - c * h) / det; r(0, 2) = (b * f - c * e) / det;
    r(1, 0) = B / det; r(1, 1) = (a * i - c * g) / det;  r(1, 2) = -(a * f - c * d) / det;
    r(2, 0) = C / det; r(2, 1) = -(a * h - b * g) / det; r(2, 2) = (a * e - b * d) / det;
    return r;
}

// rotation part of the polar decomposition (Newton iteration; the inputs are rotations to a few per cent)
Mat3d polarRotation(const Mat3d& m) {
    Mat3d q = m;
    for (int k = 0; k < 8; k++) q = lerp(q, inverse(q).transposed(), 0.5);
    return q;
}

struct Quat { double w, x, y, z; };

Quat toQuat(const Mat3d& m) {
    Quat q;
    const double tr = m(0, 0) + m(1, 1) + m(2, 2);
    if (tr > 0) {
        const double s = std::sqrt(tr + 1.0) * 2;
        q = {0.25 * s, (m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s};
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(0, 0) - m(1, 1) - m(2, 2)) * 2;
        q = {(m(2, 1) - m(1, 2)) / s, 0.25 * s, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s};
    } else if (m(1, 1) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(1, 1) - m(0, 0) - m(2, 2)) * 2;
        q = {(m(0, 2) - m(2, 0)) / s, (m(0, 1) + m(1, 0)) / s, 0.25 * s, (m(1, 2) + m(2, 1)) / s};
    } else {
        const double s = std::sqrt(1.0 + m(2, 2) - m(0, 0) - m(1, 1)) * 2;
        q = {(m(1, 0) - m(0, 1)) / s, (m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, 0.25 * s};
    }
    return q;
}

Mat3d fromQuat(const Quat& q) {
    const double w = q.w, x = q.x, y = q.y, z = q.z;
    Mat3d m;
    m(0, 0) = 1 - 2 * (y * y + z * z); m(0, 1) = 2 * (x * y - w * z);     m(0, 2) = 2 * (x * z + w * y);
    m(1, 0) = 2 * (x * y + w * z);     m(1, 1) = 1 - 2 * (x * x + z * z); m(1, 2) = 2 * (y * z - w * x);
    m(2, 0) = 2 * (x * z - w * y);     m(2, 1) = 2 * (y * z + w * x);     m(2, 2) = 1 - 2 * (x * x + y * y);
    return m;
}

Quat slerp(Quat a, Quat b, double t) {
    double d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) { b = {-b.w, -b.x, -b.y, -b.z}; d = -d; }
    double ka = 1 - t, kb = t;
    if (d < 0.9999) {
        const double th = std::acos(std::min(1.0, d)), s = std::sin(th);
        ka = std::sin((1 - t) * th) / s;
        kb = std::sin(t * th) / s;
    }
    Quat r{ka * a.w + kb * b.w, ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z};
    const double n = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
    return {r.w / n, r.x / n, r.y / n, r.z / n};
}

}  // namespace

Mat3d interpMatrix(const Mat3d& a, const Mat3d& b, double t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    const Mat3d qa = polarRotation(a), qb = polarRotation(b);
    const Mat3d sa = qa.transposed() * a, sb = qb.transposed() * b;
    return fromQuat(slerp(toQuat(qa), toQuat(qb), t)) * lerp(sa, sb, t);
}

}  // namespace planes
