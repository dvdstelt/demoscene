// The "jizz" logo: part 0x1212a. One white page while the part sets up, then the "jizz!" picture (logo2 [0x658e],
// texture build 14: clouds times the RLE picture at 0x27d68) with the one-pixel dark outline that 0x1216f..0x121b9
// marks around it, over a star-shaped polar warp of map5 (0x16dc8 with the star table 0x123df) darkened by a
// wobbling level (0x179f6 with black fade tables); the events 810 / 820 / 830 change the star and start a 0.88 s
// brightening pulse. jizz/re/notes-parts2d 4.
//
// Per pixel, at the output resolution (X, Y from the screen centre in original pixels, col = atan2(Y, X) * 256/pi):
//   row = 1.3 |(X, Y)| + B sin(2 pi (A col + C) / 256),   warp = map5(col, row) * (31 - lv) / 32
// with C = 5t / 8192 (negated when S = -1) and lv = 5 sin(2 pi t / 2^20) + 6 cos(2 pi t / 838860.8) + 13 minus the
// pulse, all from the timer and continuous (the original rounds the star offsets per angle byte and lv to integers,
// and reads the nearest texel).
//
// The picture, with the redraw (art/logo.png, jizz/art.h): the script's clouds (Tex::LogoClouds) times the redraw's
// shape at the picture's solid level, as the script multiplies them (logo2 = clouds x picture / 256); the shape is
// the redraw's coverage at 1/2, anti-aliased at the output resolution. Its opaque area (the shape plus the outline,
// i.e. the shape dilated by one original pixel) comes from a distance field of the redraw at its own resolution.
// With --original-art (or without the file): the filtered high-resolution logo2 texture; its opaque area (the
// non-zero pixels plus the outline) is an anti-aliased shape from a distance field built at 4x the original
// resolution.
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "jizz/art.h"
#include "jizz/core.h"
#include "jizz/textures.h"

namespace {
using namespace jizz;

constexpr int kPos = 0x0800;  // 08:00
constexpr double kPit = 1193182.0;
// Measured in the capture (full.flips, snapshot t4050): the white page shows while the part sets up (outline,
// engine set-up, map5 conversion); the loop's start timer (71411263) is read 61514 clocks (0.0516 s) after the
// music reaches 08:00.
constexpr double kSetup = 61514.0 / kPit;
constexpr int kDistScale = 4;  // distance field resolution: 4x the original

const char* kWarpFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform vec2 size;
uniform vec3 star;     // A (lobes per half turn), B (depth in texels), C (phase in col units)
uniform float k256;    // 2 pi / 256
uniform float dark;    // (31 - lv) / 32
void main() {
    vec2 vga = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    vec2 p = vga - vec2(160.0, 100.0);
    vec2 pr = jzPolar(p);                                  // (col, 1.3 rho)
    float a = (star.x * pr.x + star.z) * k256;
    vec2 t = vec2(pr.x, pr.y + star.y * sin(a));
    mat2 J = jzPolarJacobian(p);
    vec2 dx = J * dFdx(p), dy = J * dFdy(p);
    float kr = star.y * cos(a) * k256 * star.x;            // d row / d col
    dx.y += kr * dx.x;
    dy.y += kr * dy.x;
    o = vec4(jzTex256Grad(tex, t, dx, dy).rgb * dark, 1.0);
}
)";

const char* kPicFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D pic;   // logo2 (320x200 picture, x wrapping at 256)
uniform sampler2D dist;  // distance to the picture's non-zero area in original pixels, over 320x200
uniform vec2 size;
void main() {
    vec2 vga = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    vec2 q = vga - vec2(0.0, 20.0);                      // 0x1239b: picture rows 30..159 -> page rows 50..179
    if (q.y < 30.0 || q.y > 160.0) discard;
    float d = texture(dist, q / vec2(320.0, 200.0)).r;
    float w = 320.0 / size.x;                            // one output pixel in original pixels
    float cov = clamp((1.0 - d) / w + 0.5, 0.0, 1.0);    // outline: one original pixel around the non-zero area
    if (cov <= 0.0) discard;
    o = vec4(texture(pic, q / 256.0).rgb, cov);
}
)";

const char* kPicArtFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D clouds;  // the script's clouds (256x256 texels, wrapping): logo2 = clouds x picture / 256
uniform sampler2D art;     // the redraw: coverage over the 256x200 picture, x wrapping at 256 (tile320)
uniform sampler2D dist;    // distance to the redraw's shape (coverage >= 1/2) in original pixels, over 256x200
uniform vec2 size;
uniform float level;       // the picture's solid level / 256
void main() {
    vec2 vga = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    vec2 q = vga - vec2(0.0, 20.0);                      // 0x1239b: picture rows 30..159 -> page rows 50..179
    vec2 a = q / vec2(256.0, 200.0);
    float m = texture(art, a).r;
    float inner = clamp((m - 0.5) / max(fwidth(m), 1e-3) + 0.5, 0.0, 1.0);  // the shape, one output pixel of edge
    if (q.y < 30.0 || q.y > 160.0) discard;
    float d = texture(dist, a).r;
    float w = 320.0 / size.x;                            // one output pixel in original pixels
    float cov = clamp((1.0 - d) / w + 0.5, 0.0, 1.0);    // outline: one original pixel around the shape
    if (cov <= 0.0) discard;
    // the multiply's floor as its mean (texgen_hires.cpp); dark in the outline ring
    vec3 c = max(texture(clouds, q / 256.0).rgb * level - 0.5 / 255.0, 0.0);
    o = vec4(c * inner, cov);
}
)";

// Squared Euclidean distance transform of one line (Felzenszwalb & Huttenlocher), f = 0 on the set, large off it.
// Samples one unit apart; for a spacing s, pass f / s^2 and multiply the result by s^2.
void edt1d(const double* f, int n, double* d, int* v, double* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e30;
    z[1] = 1e30;
    auto meet = [&](int q, int p) { return ((f[q] + double(q) * q) - (f[p] + double(p) * p)) / (2.0 * (q - p)); };
    for (int q = 1; q < n; q++) {
        double s = meet(q, v[k]);
        while (s <= z[k]) s = meet(q, v[--k]);  // z[0] = -inf stops it
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e30;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        d[q] = double(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

class LogoScene : public Scene {
public:
    void init(Ctx& c) override {
        const Textures& tx = c.shared<Textures>();
        jizz::prepare(c);
        const JizzExe& e = *c.exe;
        A0_ = e.s32(0x1238e);  // 7
        B0_ = e.s32(0x12392);  // 25
        S0_ = e.s32(0x12396);  // 0
        k256_ = e.f32(0x141e2);
        twoPi16k_ = e.f64(0x11596);
        pos_ = c.positionTime(kPos);
        start_ = pos_ + kSetup;
        warp_.build(kFullscreenVS, std::string(kGLSL) + kWarpFS, "logo warp");
        ArtImage im;
        if (loadArtGrey(c, "logo", &im)) {
            uploadArt(im, art_);
            buildArtDistance(im);
            const uint8_t* pb = tx.planar(Tex::PictureB);
            level_ = float(*std::max_element(pb, pb + 65536)) / 256.0f;  // 255: the picture's solid pixels
            pic_.build(kFullscreenVS, kPicArtFS, "logo picture (redraw)");
        } else {
            pic_.build(kFullscreenVS, kPicFS, "logo picture");
            buildDistance(tx);
        }
    }

    void render(Ctx& c, double t) override {
        if (t < start_) {  // 0x17737: one white page during the set-up
            c.clear(kWhite);
            return;
        }
        const Textures& tx = c.shared<Textures>();
        const double tp = c.timer(t) - c.timer(start_);  // time() in PIT clocks
        // 0x12267: the events 810 / 820 / 830 set A, B, S and start the pulse (the part then sets 800)
        double since = 0;
        const int ev = c.event(t, &since);
        int A = A0_, B = B0_, S = S0_;
        bool pulse = false;
        if (ev != 800) {
            pulse = true;
            if (ev == 810) { A = 4; B = 0x23; S = -1; }
            else if (ev == 820) { A = 7; B = 0x17; S = 0; }
            else { A = 5; B = 0x26; S = -1; }
        }
        // C = (5t >> 13) ^ S (S = -1: -C - 1, the star turns the other way)
        double C = std::fmod(5.0 * tp / 8192.0, 256.0);
        if (S) C = -C - 1.0;
        // 0x122dc: lv = round(5 sin + 6 cos) + 13, minus the pulse (31 -> 0 over 1023 * 1024 clocks)
        const double a = tp / 64.0;
        double lv = 5.0 * std::sin(std::fmod(a, 16384.0) * twoPi16k_) +
                    6.0 * std::cos(std::fmod(a * 5.0 / 4.0, 16384.0) * twoPi16k_) + 13.0;
        if (pulse) {
            const double k = std::max(0.0, 1023.0 - (tp - (c.timer(since) - c.timer(start_))) / 1024.0);
            lv = std::max(0.0, lv - k / 32.0);
        }

        c.out->bind();
        setBlend(Blend::None);
        warp_.use();
        tx.bind(Tex::Map5, 0);
        warp_.set("tex", 0);
        warp_.set("size", float(c.out->w), float(c.out->h));
        warp_.set("star", float(A), float(B), float(C));
        warp_.set("k256", k256_);
        warp_.set("dark", float((31.0 - lv) / 32.0));  // 0x179f6 towards black: c (31 - lv) / 32
        drawFullscreen();

        setBlend(Blend::Alpha);  // 0x1239b: the picture and its outline over the warp
        pic_.use();
        if (art_.id) {
            tx.bind(Tex::LogoClouds, 0);
            art_.bind(1);
            dist_.bind(2);
            pic_.set("clouds", 0);
            pic_.set("art", 1);
            pic_.set("dist", 2);
            pic_.set("level", level_);
        } else {
            tx.bind(Tex::Logo2, 0);
            dist_.bind(1);
            pic_.set("pic", 0);
            pic_.set("dist", 1);
        }
        pic_.set("size", float(c.out->w), float(c.out->h));
        drawFullscreen();
        setBlend(Blend::None);
    }

private:
    // The opaque area of 0x1239b: the non-zero pixels of logo2 (rows 30..159) and the outline 0x1216f marks around
    // them. As a continuous shape: the non-zero area (the 0/1 mask smoothed by a Gaussian of 1/2 pixel, so that pixel
    // staircases become straight edges, interpolated bilinearly, threshold 1/2) dilated by one original pixel;
    // stored as the distance to the non-zero area, in original pixels.
    void buildDistance(const Textures& tx) {
        const int W = 320, H = 200, S = kDistScale, w = W * S, h = H * S;
        std::vector<double> mask(size_t(W) * H, 0.0), tmp(size_t(W) * H, 0.0);
        for (int y = 30; y <= 159; y++)
            for (int x = 0; x < W; x++) mask[size_t(y) * W + x] = tx.rgb565(Tex::Logo2, x, y) != 0 ? 1.0 : 0.0;
        double g[5], gs = 0;
        for (int i = 0; i < 5; i++) gs += g[i] = std::exp(-0.5 * (i - 2) * (i - 2) / 0.25);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                double a = 0;
                for (int i = 0; i < 5; i++) a += g[i] * mask[size_t(y) * W + std::clamp(x + i - 2, 0, W - 1)];
                tmp[size_t(y) * W + x] = a / gs;
            }
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                double a = 0;
                for (int i = 0; i < 5; i++) a += g[i] * tmp[size_t(std::clamp(y + i - 2, 0, H - 1)) * W + x];
                mask[size_t(y) * W + x] = a / gs;
            }
        auto nz = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= W || y >= H) return 0.0;
            return mask[size_t(y) * W + x];
        };
        std::vector<double> f(size_t(w) * h), d(std::max(w, h)), col(std::max(w, h)), z(std::max(w, h) + 1);
        std::vector<int> v(std::max(w, h));
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                const double fx = (i + 0.5) / S - 0.5, fy = (j + 0.5) / S - 0.5;
                const int x = int(std::floor(fx)), y = int(std::floor(fy));
                const double ax = fx - x, ay = fy - y;
                const double m = (nz(x, y) * (1 - ax) + nz(x + 1, y) * ax) * (1 - ay) +
                                 (nz(x, y + 1) * (1 - ax) + nz(x + 1, y + 1) * ax) * ay;
                f[size_t(j) * w + i] = m >= 0.5 ? 0.0 : 1e12;
            }
        for (int i = 0; i < w; i++) {  // columns
            for (int j = 0; j < h; j++) col[j] = f[size_t(j) * w + i];
            edt1d(col.data(), h, d.data(), v.data(), z.data());
            for (int j = 0; j < h; j++) f[size_t(j) * w + i] = d[j];
        }
        std::vector<float> out(size_t(w) * h);
        for (int j = 0; j < h; j++) {  // rows
            edt1d(&f[size_t(j) * w], w, d.data(), v.data(), z.data());
            for (int i = 0; i < w; i++)  // the boundary lies half a sample outside the last inside sample
                out[size_t(j) * w + i] = float(std::min(4.0, std::max(0.0, std::sqrt(d[i]) - 0.5) / S));
        }
        dist_.createRaw(w, h, GL_R16F, GL_RED, GL_FLOAT, out.data(), true, false, false);
    }

    // The same for the redraw: the distance to its shape (coverage >= 1/2) in original pixels, at the redraw's own
    // resolution over the 256x200 picture (one sample is 256 / w original pixels across and 200 / h down), wrapping
    // in x like the picture. Only the outline needs it, so it is clamped at 4 pixels.
    void buildArtDistance(const ArtImage& im) {
        const int w = im.w, h = im.h;
        const double sx = 256.0 / w, sy = 200.0 / h;  // sample spacing in original pixels
        std::vector<double> f(size_t(w) * h), d(std::max(w, h)), col(std::max(w, h)), z(std::max(w, h) + 1);
        std::vector<int> v(std::max(w, h));
        for (size_t i = 0; i < f.size(); i++) f[i] = im.px[i] >= 128 ? 0.0 : 1e12;
        for (int i = 0; i < w; i++) {  // columns, spacing sy
            for (int j = 0; j < h; j++) col[j] = f[size_t(j) * w + i] / (sy * sy);
            edt1d(col.data(), h, d.data(), v.data(), z.data());
            for (int j = 0; j < h; j++) f[size_t(j) * w + i] = d[j] * sy * sy;
        }
        std::vector<float> out(size_t(w) * h);
        const double half = 0.25 * (sx + sy);  // the boundary lies about half a sample outside the last inside one
        for (int j = 0; j < h; j++) {  // rows, spacing sx
            for (int i = 0; i < w; i++) col[i] = f[size_t(j) * w + i] / (sx * sx);
            edt1d(col.data(), w, d.data(), v.data(), z.data());
            for (int i = 0; i < w; i++)
                out[size_t(j) * w + i] = float(std::min(4.0, std::max(0.0, std::sqrt(d[i] * sx * sx) - half)));
        }
        dist_.createRaw(w, h, GL_R16F, GL_RED, GL_FLOAT, out.data(), true, true, false);
    }

    Shader warp_, pic_;
    Texture dist_, art_;  // art_: the redraw (id 0 with --original-art)
    double pos_ = 0, start_ = 0, twoPi16k_ = 0;
    int A0_ = 7, B0_ = 25, S0_ = 0;
    float k256_ = 0, level_ = 1;
};

REGISTER_SCENE(LogoScene, "logo");
}  // namespace
