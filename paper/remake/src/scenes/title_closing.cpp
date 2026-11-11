// The closing logo (ticks 11377-12235): the "psychic link" box logo zooming out over the glowing "1996", which keeps
// blurring and sinks to black (paper/re/notes-title.md §3). Ports 0x13d6 (part loop), 0x136c (per-tick zoom
// recurrence), 0x129b (frame: dim, add, one 0xf90 pass), 0x11c0 (zoom), the init half 0xfb2 (both images), 0x13af /
// 0x13bb (the "1996" blur calls) and the palette 0x14dc at angle 0x2d0, shift 12. The fade from white is the core
// post pass.
//
// High resolution:
//  - the logo (0x11b0 text at (187, 6) in a 640-wide buffer, two inverted rectangles, one 0xf47) is drawn every
//    frame from the core font's SDF glyphs and analytic rectangles at the zoomed size, then blurred by the Gaussian
//    of that 0xf47 call (sigma 1 logo px) and dimmed like 0x129b;
//  - the "1996" image is the original's own 320x200 integer buffer, simulated exactly (0xedc glyphs, 150 + 1 per
//    8 ticks calls of 0xf47 with their per-pass truncation) and upscaled bicubic: the truncation is what erodes the
//    glow to the thin bright streaks and to black (a Gaussian of the same variance would stay a bright blob at
//    twice the brightness), and after 150 calls there is no sharper detail to recover;
//  - the final 0xf90 pass of the composite (tmp = min(255, "1996" + dimmed logo), VGA = 0xf90(tmp)) is applied to
//    each term: exactly (integers) to the "1996" buffer, as extra Gaussian variance (1/2 VGA px^2) to the logo and
//    the edges of its 40-row band; only the clamp at 255 moves before it (a sub-pixel difference at saturated
//    edges). So a frame is: the logo layer at a reduced density (it is blurred by >= 0.87 VGA px), then one
//    full-resolution pass (bicubic "1996", bicubic logo, palette).
// Motion: Z_n of the exact integer recurrence, interpolated geometrically between ticks. The logo stays centred on
// logo pixel (320, 20) at every size; 0x11c0's truncated first row (the band mode below s = 0x500 starts at row
// floor(100 - 20 s / 256), up to one row higher) is dropped.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "paper/font.h"
#include "paper/gpu.h"
#include "paper/palette.h"

namespace {

using namespace paper;

constexpr int kStart = 11377;               // first tick of the part
constexpr uint32_t kZ0 = 0x1368, kK0 = 0x1364;  // static dwords of 0x136c
constexpr uint32_t kLogoText = 0x10c6;      // "psychic link"
constexpr int kHueAngle = 0x2d0, kHueShift = 12;
constexpr int kInitBlurCalls = 150;         // [0x1edad]: init + pencil part + logo page wait loop, always 150

// ---------------------------------------------------------------- 0x136c: Z_n, n ticks after the part start

std::vector<int64_t> zoomSequence(const PaperExe& exe, int n) {
    int64_t Z = exe.s32(kZ0), K = exe.s32(kK0);
    std::vector<int64_t> z{Z};
    for (int i = 0; i < n; i++) {
        Z = (Z * K) >> 16;
        if (Z < 0x2000) K = (((K - 0xffce) * 240) >> 8) + 0xffce;  // arithmetic shift: K settles at 0xffbf
        z.push_back(Z);
    }
    return z;
}

// ---------------------------------------------------------------- "1996": 0xfb2 + 0xf47, exact integers

class Glow1996 {
public:
    static constexpr int W = 320, H = 200, N = W * H;

    explicit Glow1996(const FontData& f) : buf_(size_t(N + 2 * W), 0), tmp_(size_t(N + 4 * W), 0) {
        std::vector<uint8_t> b(size_t(N), 0);
        int x = 2;
        // 0xedc with al = 0x7b, 0x7c, 0x7c, 0x7d (immediates at 0xfd9..0xfee, "1996") from (2, 9)
        for (char ch : std::string("{||}")) x = f.drawBytesBig(b.data(), W, H, W, x, 9, ch);
        for (int p = 0; p < N; p++) buf_[size_t(W + p)] = b[size_t(p)];
        for (int k = 0; k < kInitBlurCalls; k++) blurCall();
        states_.push_back(snapshot());
    }

    // 0x129b's 0xf90 pass of the buffer after kInitBlurCalls + calls calls of 0xf47 (what the screen shows
    // where the logo is not)
    const std::vector<uint8_t>& state(int calls) {
        calls = std::max(0, calls);
        while (int(states_.size()) <= calls) {
            blurCall();
            states_.push_back(snapshot());
        }
        return states_[size_t(calls)];
    }

private:
    // 0xf47 on 200 rows, pitch 320: (N + S + W + E) >> 2 into a temp with 640 zero bytes above and below, and back.
    // x neighbours run on linearly into the previous / next row; rows -1 and 200 of the buffer are zero.
    void blurCall() {
        int32_t* s = buf_.data() + W;
        int32_t* d = tmp_.data() + 2 * W;
        for (int p = 0; p < N; p++) d[p] = (s[p - W] + s[p + W] + s[p - 1] + s[p + 1]) >> 2;
        for (int p = 0; p < N; p++) s[p] = (d[p - W] + d[p + W] + d[p - 1] + d[p + 1]) >> 2;
    }
    // one 0xf90 pass (zero rows above and below), as 0x129b does from its temp buffer to VGA
    std::vector<uint8_t> snapshot() const {
        const int32_t* s = buf_.data() + W;
        std::vector<uint8_t> v(static_cast<size_t>(N));
        for (int p = 0; p < N; p++) v[size_t(p)] = uint8_t(std::min(255, (s[p - W] + s[p + W] + s[p - 1] + s[p + 1]) >> 2));
        return v;
    }

    std::vector<int32_t> buf_, tmp_;
    std::vector<std::vector<uint8_t>> states_;
};

// ---------------------------------------------------------------- shaders

const char* kVgaVS = R"(
out vec2 vga;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vga = vec2(p.x * 320.0, (1.0 - p.y) * 200.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Logo layer: glyph coverage XOR the frame (outer rectangle minus inner one), as 0x109a's NOTs leave it:
// "psychic" white inside the inner rectangle, "link" black on the white frame. One texel of the logo target.
const char* kXorFS = R"(
out vec4 o;
uniform sampler2D glyphs;
uniform vec4 area;     // VGA x0, y0 and VGA px per texel (x, y) of the logo target
uniform float mag;     // screen (VGA) px per logo px
uniform vec4 outer, inner;   // x0, y0, x1, y1 in logo px
float overlap(float c, float f, float a, float b) { return clamp((min(b, c + 0.5 * f) - max(a, c - 0.5 * f)) / f, 0.0, 1.0); }
float box(vec2 p, vec2 f, vec4 r) { return overlap(p.x, f.x, r.x, r.z) * overlap(p.y, f.y, r.y, r.w); }
void main() {
    // targets are stored bottom-up: texel row 0 is the bottom of the covered VGA area
    vec2 vga = area.xy + vec2(gl_FragCoord.x, float(textureSize(glyphs, 0).y) - gl_FragCoord.y) * area.zw;
    vec2 p = vec2(320.0, 20.0) + (vga - vec2(160.0, 100.0)) / mag;   // logo px (0x11c0's mapping)
    vec2 f = area.zw / mag;
    float ring = box(p, f, outer) - box(p, f, inner);
    float g = min(1.0, texelFetch(glyphs, ivec2(gl_FragCoord.xy), 0).r);
    o = vec4(g + ring - 2.0 * g * ring, 0.0, 0.0, 1.0);
}
)";

// Separable Gaussian, taps paired into bilinear fetches (weights and offsets from gaussTaps)
const char* kGaussFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D src;
uniform vec2 dir;       // one texel in uv
uniform int pairs;
uniform float w0;
uniform float wt[8];
uniform float off[8];
void main() {
    float s = texture(src, uv).r * w0;
    for (int i = 0; i < pairs; i++) {
        vec2 d = dir * off[i];
        s += (texture(src, uv + d).r + texture(src, uv - d).r) * wt[i];
    }
    o = vec4(s, 0.0, 0.0, 1.0);
}
)";

// 0x129b: VGA = 0xf90(min(255, "1996" + T[logo])), T[v] = v * dh >> 8, the logo only on its 40 rows (0x11c0's
// band); the 0xf90 pass is already in both inputs (see the top of the file). Then the palette.
const char* kFrameFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D glowA, glowB, logo;
uniform float glowMix;
uniform vec4 area;     // VGA x0, y0, w, h covered by the logo target
uniform float mag, dim;
uniform float bandSigma;   // logo rows: 0xf90's sigma and the pixel footprint
uniform vec3 base;
float cdf(float x) {   // standard normal CDF (Abramowitz-Stegun 7.1.26 erf, error < 1.5e-7)
    float z = abs(x) * 0.70710678, t = 1.0 / (1.0 + 0.3275911 * z);
    float e = 1.0 - t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429)))) * exp(-z * z);
    return 0.5 + 0.5 * sign(x) * e;
}
void main() {
    vec2 gu = vga / vec2(320.0, 200.0);
    float glow = mix(texBicubic(glowA, gu).r, texBicubic(glowB, gu).r, glowMix);
    vec2 lu = (vga - area.xy) / area.zw;
    float l = max(0.0, texBicubic(logo, vec2(lu.x, 1.0 - lu.y)).r);
    float v = 20.0 + (vga.y - 100.0) / mag;
    float band = cdf(v / bandSigma) - cdf((v - 40.0) / bandSigma);
    float idx = min(255.0, max(0.0, glow) * 255.0 + l * 255.0 * dim * band);
    o = vec4(hueRamp(base, idx), 1.0);
}
)";

// Gaussian of sigma texels as a centre weight and up to 8 bilinear pairs (offset, weight) per side.
struct GaussTaps {
    int pairs = 0;
    float w0 = 1, wt[8] = {}, off[8] = {};
};
GaussTaps gaussTaps(double sigma) {
    GaussTaps g;
    const int R = std::min(16, std::max(1, int(std::ceil(3.0 * sigma))));
    auto w = [&](int i) { return std::exp(-0.5 * i * i / (sigma * sigma)); };
    double sum = 1;
    for (int i = 1; i <= R; i++) sum += 2 * w(i);
    g.w0 = float(1 / sum);
    for (int i = 1; i <= R && g.pairs < 8; i += 2) {
        double a = w(i), b = i + 1 <= R ? w(i + 1) : 0.0;
        g.wt[g.pairs] = float((a + b) / sum);
        g.off[g.pairs] = float((i * a + (i + 1) * b) / (a + b));
        g.pairs++;
    }
    return g;
}

// ---------------------------------------------------------------- the part

class ClosingPart : public Scene {
public:
    void init(Ctx& c) override {
        const PaperExe& exe = *c.exe;
        const FontData& f = c.shared<Font>().data();
        zs_ = zoomSequence(exe, 1000);
        glow_ = std::make_unique<Glow1996>(f);
        logoGlyphs_ = layoutText(f, exe.cstr(kLogoText), 187, 6, 640);
        xor_.build(kFullscreenVS, kXorFS, "closing logo xor");
        gauss_.build(kFullscreenVS, kGaussFS, "closing gauss");
        frame_.build(kVgaVS, std::string(kPaletteGLSL) + kFrameFS, "closing frame");
        for (GlowTex& g : tex_) upload(g, 0);
    }

    void render(Ctx& c, double tick) override {
        Target& out = *c.out;
        const double tau = std::max(0.0, tick - kStart);
        const int n = int(std::floor(tau));
        const double fr = tau - n;

        // Z between ticks: geometric interpolation of the exact sequence (it is a zoom by a factor per tick)
        const size_t i0 = std::min(size_t(n), zs_.size() - 2);
        const double Z = double(zs_[i0]) * std::pow(double(zs_[i0 + 1]) / double(zs_[i0]), fr);
        const double s = std::max(128.0, Z / 16.0);  // 0x129b: s = Z >> 4, at least 0x80
        const double mag = s / 256.0;                // screen px per logo px
        const double dim = std::clamp((s - 128.0) * 2.0, 0.0, 255.0) / 256.0;  // dh / 256

        // "1996": one more 0xf47 every 8 ticks (0x13d6 counts ticks from 2 at entry; the capture's pictures show
        // floor(n / 8) calls), mixed into the next state over the tick before a call
        const int b0 = n / 8, b1 = (n + 1) / 8;
        const GlowTex *ga = nullptr, *gb = nullptr;
        glowPair(b0, b1, ga, gb);

        GlState saved;
        gl::Disable(GL_BLEND);
        TargetPool::Lease logo;
        if (dim > 0) logo = drawLogo(c, mag, out);  // invisible from s <= 0x80 on (n >= 475)

        out.bind();
        frame_.use();
        ga->t.bind(0);
        gb->t.bind(1);
        if (logo.t) logo->bindTex(2);
        else ga->t.bind(2);
        frame_.set("glowA", 0);
        frame_.set("glowB", 1);
        frame_.set("logo", 2);
        frame_.set("glowMix", b1 != b0 ? float(fr) : 0.0f);
        frame_.set("area", -kMargin, -kMargin, 320.0f + 2 * kMargin, 200.0f + 2 * kMargin);
        frame_.set("mag", float(mag));
        frame_.set("dim", logo.t ? float(dim) : 0.0f);
        const double pixRow = 200.0 / out.h;  // VGA px per output row
        frame_.set("bandSigma", float(std::sqrt(0.5 + pixRow * pixRow / 12.0) / mag));
        frame_.set("base", hueBase(kHueAngle, kHueShift));
        drawFullscreen();
    }

private:
    static constexpr float kMargin = 32.0f;  // VGA px around the screen in the logo target (3 sigma at 10x zoom)

    struct GlowTex { Texture t; int calls = -1; };

    void upload(GlowTex& g, int calls) {
        const std::vector<uint8_t>& v = glow_->state(calls);
        if (!g.t.id) g.t.createRaw(Glow1996::W, Glow1996::H, GL_R8, GL_RED, GL_UNSIGNED_BYTE, v.data(), true, false, false);
        else {
            gl::BindTexture(GL_TEXTURE_2D, g.t.id);
            gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Glow1996::W, Glow1996::H, GL_RED, GL_UNSIGNED_BYTE, v.data());
        }
        g.calls = calls;
    }
    GlowTex* find(int calls) {
        for (GlowTex& g : tex_)
            if (g.calls == calls) return &g;
        return nullptr;
    }
    // textures of the states after b0 and b1 calls (two slots, re-uploaded when the count moves)
    void glowPair(int b0, int b1, const GlowTex*& a, const GlowTex*& b) {
        GlowTex* x = find(b0);
        GlowTex* y = find(b1);
        if (!x) {
            x = y == &tex_[0] ? &tex_[1] : &tex_[0];
            upload(*x, b0);
        }
        if (b1 == b0) y = x;
        else if (!y) {
            y = x == &tex_[0] ? &tex_[1] : &tex_[0];
            upload(*y, b1);
        }
        a = x;
        b = y;
    }

    // The zoomed logo (0x11b0 glyphs XOR 0x109a's frame), blurred by 0xf47 (sigma 1 logo px) and 0x129b's 0xf90
    // (1/2 VGA px^2), in a target that covers the screen plus kMargin. Its density is the output's, halved while
    // the blur is >= 3 texels: the layer is always soft, and bicubic upsampling of it is exact enough.
    TargetPool::Lease drawLogo(Ctx& c, double mag, const Target& out) {
        const double sigmaVga = std::sqrt(mag * mag + 0.5);
        double perVga = out.w / 320.0;
        while (perVga * sigmaVga >= 3.0 && perVga > 1.0 / 64) perVga *= 0.5;
        const int tw = std::max(8, int(std::lround((320 + 2 * kMargin) * perVga)));
        const int th = std::max(8, int(std::lround((200 + 2 * kMargin) * perVga)));
        const float px = (320 + 2 * kMargin) / tw, py = (200 + 2 * kMargin) / th;  // VGA px per texel

        auto a = c.tempSized(tw, th, false, GL_R16F), b = c.tempSized(tw, th, false, GL_R16F);
        a->bind();
        c.clear();
        // logo px -> clip of this target: VGA = (160, 100) + (logo - (320, 20)) * mag
        const float m = float(mag);
        const float uL = 320.0f + (-kMargin - 160.0f) / m, uR = 320.0f + (320.0f + kMargin - 160.0f) / m;
        const float vT = 20.0f + (-kMargin - 100.0f) / m, vB = 20.0f + (200.0f + kMargin - 100.0f) / m;
        c.shared<Font>().draw(logoGlyphs_, Mat4::ortho(uL, uR, vB, vT, -1, 1), GlyphOp::Add, 1.0f);

        b->bind();
        xor_.use();
        a->bindTex(0);
        xor_.set("glyphs", 0);
        xor_.set("area", -kMargin, -kMargin, px, py);
        xor_.set("mag", m);
        xor_.set("outer", 176.0f, 1.0f, 176.0f + 288.0f, 1.0f + 38.0f);  // 0x109a(0xb0, 1, 0x120, 0x26)
        xor_.set("inner", 180.0f, 4.0f, 180.0f + 196.0f, 4.0f + 32.0f);  // 0x109a(0xb4, 4, 0xc4, 0x20)
        drawFullscreen();

        gauss(*b, *a, sigmaVga / px, 1.0f / tw, 0.0f);
        gauss(*a, *b, sigmaVga / py, 0.0f, 1.0f / th);
        return b;
    }

    void gauss(const Target& src, Target& dst, double sigma, float dx, float dy) {
        GaussTaps g = gaussTaps(sigma);
        dst.bind();
        gauss_.use();
        src.bindTex(0);
        gauss_.set("src", 0);
        gauss_.set("dir", dx, dy);
        gauss_.set("pairs", g.pairs);
        gauss_.set("w0", g.w0);
        gl::Uniform1fv(gauss_.loc("wt"), 8, g.wt);
        gl::Uniform1fv(gauss_.loc("off"), 8, g.off);
        drawFullscreen();
    }

    std::vector<int64_t> zs_;
    std::unique_ptr<Glow1996> glow_;
    std::vector<GlyphPos> logoGlyphs_;
    GlowTex tex_[2];
    Shader xor_, gauss_, frame_;
};

REGISTER_SCENE(ClosingPart, "closing");

}  // namespace
