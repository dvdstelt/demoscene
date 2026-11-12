// p6: "SHADOW volume", "material MORPH", "METAL shading" (dope/re/notes-p6p7.md sections 1..3, 5..7). The cat inside
// two turning blue rings flies over p5's swirl (p6 sets no video mode and starts on it); the rings' shadows lie on
// the cat's fur, the fur flattens while the colours morph from orange fur to blue-grey metal and back, then a metal
// cat; the group drifts out to the right and the DAC fades to black.
//
// Ports: the script 3951 (set-up, the 16 steps), the per-retrace routine 1cc0 (label slide, palette morph [3259],
// bump strength [176e], motion of the group 1620 and the cat 165a) with the handler's DAC upload 3895 and the ramps
// 38ca / 3871, the bump offset table 3913, the frame loop 2de6 with the shadow mask written into the light map 0451
// (2e05..2e66), the label and frame copy 2d5c (background restored from the swirl 0c97:0000), the exit fade 3d22.
// The 3D engine (src/dope/engine.h) draws the objects: the cat (mesh 1cc2, F 3500, +2a 0101h: the light map 0451
// with the fur 23a0) and the rings (mesh 202d, +2a 0200h: the cloud map 0449, env only), env coordinates negated.
// The remake follows the no-mouse path (the capture had no mouse driver: the selector adders stay 0).
//
// Evaluated exactly instead of the original's shortcuts:
//   - motion, angles, label, bump strength and morph are per-retrace sums evaluated at the continuous retrace count
//     (exact at whole retraces, linear between);
//   - the light map 0451 (the env map's right half: an index ramp 80 + shade, shade = min(63, 64 - r / 2) around
//     texel (92, 92) with 64 integer levels) is computed from its formula with a continuous shade at 4 x 4 samples
//     per texel and coloured through the ramps interpolated between their entries;
//   - the 32 shadow masks (64 x 64 bits, one per 1/128 turn, switched every 4 retraces) become smooth signed
//     distance fields (their staircase and stray pixels smoothed away), interpolated between neighbouring masks by
//     the fractional mask index, so the ring shadows move continuously; the edge is anti-aliased;
//   - the palette morph (ramps of 1/256 weights, both products floored) is a continuous mix of pal1 and pal2 in
//     display space with w = m / 4096; the bump strength scales continuous offsets (engine: bumpNibbles);
//   - the exit fade (v * L) >> 8 is a colour multiply, interpolated between its steps (one per retrace).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "demo.h"
#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"

using namespace dope;

namespace {

// Image addresses (notes-p6p7 2.1, 3.1, 3.3, 5).
constexpr uint16_t kGroupRecord = 0x1620, kCatRecord = 0x165a, kRingsRecord = 0x1694;
constexpr uint16_t kFurSeg = 0x23a0, kFlatSeg = 0x2398;   // texture table cs:0218 = {2398, 23a0, 0, 0}
constexpr uint16_t kPal1Seg = 0x0419;                     // 3951: copied to cs:325d (the env map's RIX palette)
constexpr uint16_t kPal2 = 0x3565;                        // cs: the metal palette (static)
constexpr uint16_t kMaskSeg = 0x368a;                     // 32 masks of 64 x 64 bytes, bit 0 = lit
constexpr uint16_t kLabelPos = 0x1768;                    // [1768] the label position (0 in the image)

// 2de6's loop ends after the frame copied after call 3990 (state 3989); 3d22's first DAC step is on screen in
// the part's record 3991 (PORT.md "Time"), L = 256 - 7 j; step 36 is black (the cue ends there).
constexpr double kLastState = 3989;
constexpr int kFadeRetrace = 3991, kFadeSteps = 36, kFadeStep = -7;   // 3d22 sub bx, 7

// The light map: shade = min(63, 64 - r / 2), r = the distance from texel (91.75, 91.75) of the 128 x 128 half
// (fitted to the stored ramp: equal within one level everywhere, 83 % equal); in continuous texel coordinates
// (texel i covers [i, i + 1]) the centre is at 92.25. Colour 80 + shade lit, 144 + shade in shadow (2e3f..2e5e).
constexpr float kLightCentre = 92.25f;
constexpr int kLightRes = 512;      // samples per side of the 128 x 128 texel map
constexpr float kMaskEdge = 0.25f;  // half width of the shadow edge in mask pixels (2 env texels per mask pixel)
constexpr int kMasks = 32, kMaskSize = 64;

const char* kLightFS = R"(
out vec4 o;
uniform sampler2D uMask;    // 32 signed distance fields of 64 x 64 (mask pixels, > 0 lit), stacked vertically
uniform sampler2D uPal;     // the morphed palette
uniform vec2 uMasks;        // the two masks mixed
uniform float uMix;         // weight of the second
uniform float uTexel;       // env texels per sample
uniform float uCentre, uEdge;
float field(float k, vec2 m) {
    vec2 p = clamp(m, vec2(0.5), vec2(63.5));
    return texture(uMask, vec2(p.x / 64.0, (k * 64.0 + p.y) / 2048.0)).r;
}
void main() {
    vec2 e = gl_FragCoord.xy * uTexel;                   // (column, row) in the 128 x 128 map
    float shade = clamp(64.0 - 0.5 * length(e - vec2(uCentre)), 0.0, 63.0);
    vec2 m = e * 0.5;                                    // 2e05: mask[row / 2][col / 2]
    float d = mix(field(uMasks.x, m), field(uMasks.y, m), uMix);
    float lit = smoothstep(-uEdge, uEdge, d);
    o = vec4(mix(dpPalLerp(uPal, 144.0 + shade), dpPalLerp(uPal, 80.0 + shade), lit), 1.0);
}
)";

// Squared Euclidean distance transform in one dimension (lower envelope of parabolas): d[q] = min_p (q - p)^2 + f[p].
void distance1d(const std::vector<double>& f, std::vector<double>& d, int n) {
    std::vector<int> v(size_t(n) + 1);
    std::vector<double> z(size_t(n) + 2);
    int k = 0;
    v[0] = 0, z[0] = -1e30, z[1] = 1e30;
    auto cut = [&](int q, int p) { return ((f[size_t(q)] + q * q) - (f[size_t(p)] + p * p)) / (2.0 * (q - p)); };
    for (int q = 1; q < n; q++) {
        double s = cut(q, v[size_t(k)]);
        while (s <= z[size_t(k)]) s = cut(q, v[size_t(--k)]);
        k++;
        v[size_t(k)] = q, z[size_t(k)] = s, z[size_t(k) + 1] = 1e30;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[size_t(k) + 1] < q) k++;
        double t = q - v[size_t(k)];
        d[size_t(q)] = t * t + f[size_t(v[size_t(k)])];
    }
}

// Distance (in pixels) from every pixel centre to the nearest pixel with `set` = true, n x n.
std::vector<double> distanceTo(const std::vector<bool>& set, int n) {
    constexpr double kFar = 1e8;
    const size_t sz = size_t(n);
    std::vector<double> g(sz * sz), f(sz), d(sz);
    for (int x = 0; x < n; x++) {
        for (int y = 0; y < n; y++) f[size_t(y)] = set[size_t(y * n + x)] ? 0 : kFar;
        distance1d(f, d, n);
        for (int y = 0; y < n; y++) g[size_t(y * n + x)] = d[size_t(y)];
    }
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) f[size_t(x)] = g[size_t(y * n + x)];
        distance1d(f, d, n);
        for (int x = 0; x < n; x++) g[size_t(y * n + x)] = std::sqrt(d[size_t(x)]);
    }
    return g;
}

// A mask (bit 0 of 64 x 64 bytes) as a smoothed signed distance field in mask pixels, positive where lit: the
// distance to the other class's pixel centres minus half a pixel, then a Gaussian of 0.8 pixels that rounds the
// staircase and removes single stray pixels.
std::vector<float> maskField(std::span<const uint8_t> bits) {
    constexpr int n = kMaskSize;
    std::vector<bool> lit(size_t(n * n)), dark(size_t(n * n));
    for (int i = 0; i < n * n; i++) lit[size_t(i)] = bits[size_t(i)] & 1, dark[size_t(i)] = !lit[size_t(i)];
    std::vector<double> toDark = distanceTo(dark, n), toLit = distanceTo(lit, n);
    std::vector<double> sd(size_t(n * n));
    for (int i = 0; i < n * n; i++)
        sd[size_t(i)] = std::clamp(lit[size_t(i)] ? toDark[size_t(i)] - 0.5 : 0.5 - toLit[size_t(i)], -16.0, 16.0);
    constexpr int R = 3;
    double w[2 * R + 1], sum = 0;
    for (int i = -R; i <= R; i++) sum += w[i + R] = std::exp(-0.5 * i * i / (0.8 * 0.8));
    for (double& x : w) x /= sum;
    auto blur = [&](const std::vector<double>& src, int dx, int dy) {
        std::vector<double> out(src.size());
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                double a = 0;
                for (int i = -R; i <= R; i++) {
                    int xx = std::clamp(x + i * dx, 0, n - 1), yy = std::clamp(y + i * dy, 0, n - 1);
                    a += w[i + R] * src[size_t(yy * n + xx)];
                }
                out[size_t(y * n + x)] = a;
            }
        return out;
    };
    std::vector<double> b = blur(blur(sd, 1, 0), 0, 1);
    return std::vector<float>(b.begin(), b.end());
}

// The group record's motion (1cc0: value += velocity per retrace), angles unwrapped.
struct Motion {
    Integrator x, y, z, a, b, c;
    Pose at(double r) const { return Pose{x.at(r), y.at(r), z.at(r), a.at(r), b.at(r), c.at(r)}; }
    Motion& vel(int k, double vx, double vy, double vz) {
        x.velocity(k, vx), y.velocity(k, vy), z.velocity(k, vz);
        return *this;
    }
    Motion& avel(int k, double va, double vb, double vc) {
        a.velocity(k, va), b.velocity(k, vb), c.velocity(k, vc);
        return *this;
    }
};

// Bump strength k ([176e], step [1770]) and palette morph m ([3259], step [325b]) after n calls of 1cc0.
struct BumpMorph {
    double k = 0x100, m = 0;   // 3a18: k = 100h; m starts at 0
};
BumpMorph bumpMorphAfter(int n) {
    // the script's steps (k, dk, dm), set before the call that starts the step at k
    struct Set { int at, dk, dm; };
    static constexpr Set kSets[] = {
        {2310, -4, 16},    // 3bec / 3bf3: step 11, fur -> metal
        {2730, 6, -24},    // 3c66 / 3c6d: step 12, back to fur
        {3010, -6, 24},    // 3c98 / 3c9f: step 13, metal again
    };
    int k = 0x100, m = 0, dk = 0, dm = 0;
    size_t s = 0;
    for (int i = 0; i < n; i++) {
        if (s < std::size(kSets) && kSets[s].at == i) dk = kSets[s].dk, dm = kSets[s].dm, s++;
        m += dm;
        if (m < 0 || m >= 0x1000) dm = 0;
        k += dk;
        if (k < 0 || k >= 0x100) k -= dk, dk = 0;
    }
    return {double(k), double(m)};
}

class CatScene : public Scene {
public:
    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(6);
        const EnginePart part = EnginePart::of(6);
        rCat_ = ObjectRecord::read(img, kCatRecord);
        rRings_ = ObjectRecord::read(img, kRingsRecord);
        cat_.load(img, rCat_.seg);
        rings_.load(img, rRings_.seg);
        fur_.create(img, kFurSeg, bumpNibbles(), part);
        flat_.create(img, kFlatSeg, bumpNibbles(), part);
        pal1_ = vgaPalette(img.span(kPal1Seg, 0x0000, 768));
        pal2_ = vgaPalette(img.csSpan(kPal2, 768));
        label_ = LabelSlide{img.css16(kLabelPos), {{140, 1}, {1680, -1}, {1890, 1}, {3290, -1}, {3430, 1}, {3710, -1}}};
        swirl_ = &c.picture("p6-swirl");
        labels_[0] = &c.picture("p6-label-shadow");   // [176c] = 3399 (image)
        labels_[1] = &c.picture("p6-label-morph");    // 3b9f: 3494
        labels_[2] = &c.picture("p6-label-metal");    // 3ce3: 358f
        clouds_ = EnvMap::picture(c.picture("p6-envmap"));
        // the script 3951 (notes-p6p7 1): step k starts after the sum of the previous steps' N
        group_.x = {-3072000, 8000}, group_.y = {0, 0}, group_.z = {4864000, -8600};   // 39ec..3a00, 3a50..3a64
        group_.a = {0, -4}, group_.b = {0, 128}, group_.c = {0, 0};                     // 3a26..3a34
        group_.vel(280, 5000, 0, 0);                                     // 3a81 step 3
        group_.vel(420, 0, 0, 6000);                                     // 3aa5 step 4
        group_.vel(700, 0, 0, 0);                                        // 3ac9 step 5
        group_.avel(910, -14, 128, 0).vel(910, 0, 0, -5000);             // 3aed / 3b02 step 6
        group_.avel(1400, 14, 128, 0).vel(1400, 0, 0, 0);                // 3b26 / 3b3b step 7
        group_.avel(1680, -4, 128, 0).vel(1680, 0, 0, 6000);             // 3b66 / 3b7b step 8
        group_.avel(2100, 16, 128, 0).vel(2100, 0, 0, -3000);            // 3bb3 / 3bc8 step 10
        group_.vel(2310, 0, 0, -4000).avel(2310, -8, 128, 0);            // 3bfa / 3c18 step 11
        group_.vel(2730, 0, 0, 400).avel(2730, 0, 128, 0);               // 3c33 / 3c51 step 12
        group_.vel(3010, 0, 0, 0);                                       // 3c7a step 13
        group_.avel(3290, -4, 128, 0);                                   // 3cac step 14
        group_.avel(3430, 8, 128, 0);                                    // 3cce step 15
        group_.vel(3710, 6000, 0, -2600);                                // 3cfe step 16
        // the 32 shadow masks as smoothed distance fields
        std::vector<float> fields;
        fields.reserve(size_t(kMasks * kMaskSize * kMaskSize));
        for (int k = 0; k < kMasks; k++) {
            std::vector<float> f = maskField(img.span(uint16_t(kMaskSeg + k * 0x100), 0, kMaskSize * kMaskSize));
            fields.insert(fields.end(), f.begin(), f.end());
        }
        masks_.createRaw(kMaskSize, kMasks * kMaskSize, GL_R32F, GL_RED, GL_FLOAT, fields.data(), true, false, false);
        light_.create(kLightRes, kLightRes, false, GL_RGBA16F);
        gl::BindTexture(GL_TEXTURE_2D, light_.tex);
        gl::GenerateMipmap(GL_TEXTURE_2D);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        lightShader_.build(kFullscreenVS, std::string(kGLSL) + kLightFS, "p6 light map");
    }

    void render(Ctx& c, double t) override {
        double r = c.retraces(t);
        double rs = std::clamp(r, 0.0, kLastState);   // the 3D state (held after the last frame)
        Pose g = group_.at(rs);
        // 1cc0: bump strength and morph, linear between whole retraces
        int n = int(std::floor(rs));
        double f = rs - n;
        BumpMorph s0 = bumpMorphAfter(n), s1 = bumpMorphAfter(n + 1);
        double k = s0.k + (s1.k - s0.k) * f, m = s0.m + (s1.m - s0.m) * f;
        // 3895: DAC 16..207 = RA[pal1] + RB[pal2] with RA, RB from 1000h - m and m
        pal_.set(mixPalettes(pal1_, pal2_, weight(m)));
        buildLight(c, g);
        // 3d22: (v * L) >> 8 from record 3991, one step per retrace
        Fade fade{c.retraceTime(kFadeRetrace), kRetraceHz, 256, kFadeStep, kFadeSteps};
        Vec3 mul = fadeMul(fade.level(t));
        mat_.env[0] = mat_.env[2] = clouds_;     // env table cs:0210 = {0449, 0451, 0449, 0451}
        mat_.env[1] = mat_.env[3] = lightMap();
        mat_.tex[0] = &flat_, mat_.tex[1] = &fur_;
        mat_.bumpScale = float(k / 256.0);       // 3913: offsets (i - 8) k / 256
        mat_.mul = mul;
        // the label slides with the position after call n + 1; [176c] changes while it is out
        double lpos = label_.position(rs + 1);
        int j = n + 1;
        const Picture* lbl = labels_[j <= 1890 ? 0 : j <= 3430 ? 1 : 2];
        drawFrame(c, {swirl_, lbl, lpos, mul}, [&] {
            Object oc = Object::from(rCat_, &cat_), orr = Object::from(rRings_, &rings_);
            oc.pose.b += -180.0 * rs;          // 3a3b..3a49: the cat's own angular velocity (0, -180, 0)
            oc.envAdd = oc.texAdd = 1;         // 3a0a: [1684] = 0101h
            orr.envAdd = 0, orr.texAdd = 2;    // 3a11: [16be] = 0200h
            Engine& eng = c.shared<Engine>();
            eng.begin(c, EnginePart::of(6), g);
            eng.draw(oc, mat_);
            eng.draw(orr, mat_);
            eng.end();
        });
    }

private:
    // 2e05..2e66: the light map with the shadow mask of the group angles, coloured through the morphed palette.
    void buildLight(Ctx& c, const Pose& g) {
        // ax = -b, or b when (int16)(a - 4000h) >= 0 (never in p6: a stays in d6fch..0); mask = (ax & 3fffh) >> 9
        double a = std::fmod(std::fmod(g.a, 65536.0) + 65536.0, 65536.0);
        double ax = (a >= 0x4000 && a < 0xc000) ? g.b : -g.b;
        double idx = std::fmod(std::fmod(ax, 16384.0) + 16384.0, 16384.0) / 512.0;   // 0..32, continuous
        // the original shows mask floor(idx) while idx runs through [k, k + 1): mask k is centred at k + 0.5
        double u = idx - 0.5, k0 = std::floor(u);
        int m0 = (int(k0) % kMasks + kMasks) % kMasks, m1 = (m0 + 1) % kMasks;
        setBlend(Blend::None);
        light_.bind();
        lightShader_.use();
        masks_.bind(0);
        pal_.bind(1);
        lightShader_.set("uMask", 0);
        lightShader_.set("uPal", 1);
        lightShader_.set("uMasks", float(m0), float(m1));
        lightShader_.set("uMix", float(u - k0));
        lightShader_.set("uTexel", 128.0f / float(kLightRes));
        lightShader_.set("uCentre", kLightCentre);
        lightShader_.set("uEdge", kMaskEdge);
        drawFullscreen();
        gl::BindTexture(GL_TEXTURE_2D, light_.tex);
        gl::GenerateMipmap(GL_TEXTURE_2D);
        c.out->bind();
    }

    EnvMap lightMap() const {
        EnvMap e;
        e.tex = light_.tex;
        e.texW = e.texH = kLightRes;
        return e;
    }

    EngineMesh cat_, rings_;
    ObjectRecord rCat_, rRings_;
    BumpMap fur_, flat_;
    Materials mat_;
    Motion group_;
    LabelSlide label_;
    Palette pal1_{}, pal2_{};
    PaletteTexture pal_;
    Texture masks_;
    Target light_;
    Shader lightShader_;
    EnvMap clouds_;
    const Picture* swirl_ = nullptr;
    const Picture* labels_[3] = {nullptr, nullptr, nullptr};
};

}  // namespace

REGISTER_SCENE(CatScene, "p6");
