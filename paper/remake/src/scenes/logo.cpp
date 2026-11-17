// "present the Paper intro": the logo page and its ink flood fill (paper/re/notes-logo.md), scene "logo".
//
// Ports
//   0x5623  page set-up at tick 5480: white page, "present the\x01intro" (0x5929) through 0x11b0 / 0xe8a as a blurred
//           drop shadow (pen (20,4), 4 blur calls 0xf47, x0.5) plus a sharp copy (pen (16,1)) and one more blur, mapped
//           63 - T/4 onto page rows 10..49 and 158..197; the 224x83 4-bit "Paper" sketch 0x1bf03 (unpacker 0x58ea,
//           inverted) at (48, 50); the paper grain composite 0x1949 (LUT 0x144b rows 28..31 over the grain 0x55e2)
//   0x572f  the part (ticks 5611..6017) with its tick callback 0x561c and push 0x58b5: a 4-connected flood fill over
//           the 224x92 ink image 0x196c3, one ring per tick, from the 8 seeds of table 0x585f, seed k pushed when the
//           music counter reaches 664 + 4k; ink pixels are drawn in palette colours 0x41..0x4f without grain.
//
// render(tick) is valid for every tick from 5480 on: before the first seed (5681) it is the page as 0x5623 built it
// (the scene peel shows it as the new page), from 5962 on it is the finished page (the teal peel's old page, 6017).
//
// Timing: R(p) is the tick whose IRQ is followed by the frame that first shows ink pixel p (seed k at the first tick
// whose music counter is 664 + 4k, then one 4-connected ring per tick). The capture shows that frame one record later
// (full.frames record t is the frame drawn before IRQ t), so remake tick t matches capture record t + 1 exactly.
//
// High resolution:
//   - text, by default: the high-resolution redraw art/text.png (paper/art.h; paper/remake/art/README.md), a coverage
//     mask of the sharp copy over the 320x80 text buffer. The original blurs its 1-bit sharp copy once (sigma 1 px),
//     which at 320x200 is its anti-aliasing; here the page pass draws the letters from the mask, anti-aliased at the
//     output resolution (the mask's 0.5 contour is the edge). The shadow is built as the original builds it, from the
//     same mask: drawn at +4,+3 at half strength into a UNORM buffer and blurred as 5 blur calls (sigma sqrt(5) px);
//     T = min(1, shadow + letters). Cached per output size.
//   - text with --original-art (or without the file): the core font's anti-aliased outlines drawn into a UNORM
//     intensity buffer covering the original's 320x80 text buffer (saturating adds as the byte adds), the 0xf47 blurs
//     as Gaussians of the same variance. The buffer is cached per output size.
//   - sketch: by default the high-resolution redraw art/sketch.png (paper/art.h; paper/remake/art/README.md), a grey
//     shade map grey = 255 v / 63 over the sketch's 224x83 frame, sampled trilinearly and shaded exactly as the
//     original's page values; with --original-art (or without the file) Catmull-Rom on the 4-bit darkness values,
//     clamped to the range of the nearest 2x2 texels (no ringing). 0x572f writes every ink pixel over the sketch,
//     but the ink pass lets the page show through the ink's light greys (partial coverage, below), so with the
//     redraw the page pass hides the sketch where the fill has reached an ink pixel of any colour (fill channel G).
//   - ink, by default: the high-resolution redraw art/ink.png of the finished ink (colour with alpha over the ink
//     image's 224x92 frame), revealed by the original's fill timing: R(p) on the original map is extended to a field
//     of kRevealScale samples per original pixel (revealField: the Gaussian-weighted mean of the ticks of the nearest
//     original ink pixel's region), and the ink shows where R < tick + 0.5 (the original's pixel set {R <= n} at whole
//     ticks n), with an anti-aliased front that moves on continuously between ticks. The page pass hides the sketch
//     within 1 original pixel of the arrived ink (inkDistance). The next two items (the ink image's outline and
//     colours) are used with --original-art (or without the file); the fill simulation serves both.
//   - ink outline: the fill is simulated exactly (the original's list algorithm) to get every ink pixel's arrival tick
//     R(p). Each frame a small pass writes s_p = ramp(tau - R(p) + 1) x w_p per ink texel (fill fraction 0 before, 1
//     from its own tick on, times the outline weight w_p, see InkTexels); the ink pass Catmull-Rom interpolates the s_p
//     and thresholds at 0.5 with an anti-aliased edge. At an integer tick n this is the smooth outline of the
//     original's pixel set {R <= n}; between ticks the front moves on continuously along the original's fronts
//     (ramp() makes a straight front advance linearly, one original pixel per tick). Walls never fill, so neighbouring
//     letters do not leak into each other.
//   - ink colours: body and greys by weighted Catmull-Rom (smooth shading); the red edges 0x4c..0x4f as a class of
//     their own where their interpolated indicator wins (crisp red strokes instead of 8-pixel smears).
//   - grain: the core's continuous grain (bicubic), index = max(0, v + 2e - 64) through the default grey ramp.
// No fade in this part (level 0x8000); the core post pass handles the level.
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "paper/art.h"
#include "paper/blur.h"
#include "paper/font.h"
#include "paper/palette.h"
#include "paper/textures.h"

namespace {

using namespace paper;

// ---- the original's data and code constants
constexpr uint32_t kSketchImage = 0x1bf03;  // 224x83, 4 bit, high nibble first: darkness n (0 = paper)
constexpr uint32_t kInkImage = 0x196c3;     // 224x92, 4 bit: 0 = no ink, n = colour 0x40 + n
constexpr uint32_t kSeedTable = 0x585f;     // 9 dwords: map offsets (stride 320), 0-terminated
constexpr uint32_t kText = 0x5929;          // "present the\x01intro"
constexpr int kImgW = 224, kSketchH = 83, kInkH = 92;
constexpr int kLogoX = 48, kLogoY = 50;     // page + 0x3eb0
constexpr int kStride = 320;
constexpr int kPartTick = 5611;             // 0x572f starts (peel progress reached 0x2bc)
constexpr int kEndCounter = 0x2c0;          // [0x1ea8f]: the part ends at music counter 704
constexpr int kPad = 2;                     // zero texels around the images (bicubic footprint)
constexpr float kTickBase = 5600.0f;        // arrival ticks are stored relative to this (float precision)
constexpr float kNever = 1.0e6f;            // arrival of wall pixels
constexpr int kRevealScale = 4;             // reveal-field samples per original pixel and axis (ink redraw)
constexpr double kRevealSigma = 0.6;        // its Gaussian, original pixels
constexpr float kHideMax = 4.0f;            // distances to the ink redraw are stored up to this (original pixels)

// 0x58ea: 4-bit image, 112 bytes per row, high nibble = left pixel.
std::vector<uint8_t> unpackNibbles(const PaperExe& exe, uint32_t addr, int rows) {
    std::vector<uint8_t> img(size_t(kImgW) * rows);
    for (int i = 0; i < rows * kImgW / 2; i++) {
        uint8_t b = exe.u8(addr + i);
        img[2 * i] = b >> 4;
        img[2 * i + 1] = b & 15;
    }
    return img;
}

// 0x572f's catch-up loop with 0x58b5 and the seed schedule, exactly as the original runs it (one layer per tick;
// the music counter decides when a seed is pushed). Returns the tick of the frame that first shows each ink pixel
// (map row-major, 224 x 92), INT_MAX for pixels without ink.
std::vector<int> simulateFill(const Ctx& c, const std::vector<uint8_t>& ink) {
    const PaperExe& exe = *c.exe;
    std::vector<uint8_t> page(64000, 0);  // [0x1edc5] as the fill map / state, stride 320
    for (int y = 0; y < kInkH; y++)
        for (int x = 0; x < kImgW; x++) page[size_t(y) * kStride + x] = ink[size_t(y) * kImgW + x];
    std::vector<int32_t> list;  // [0x1edfd], count [0x1edb1]
    auto push = [&](int32_t p) {
        if (p <= 0 || page[size_t(p)] >= 0x40) return;
        page[size_t(p)] += 0x40;
        list.push_back(p);
    };
    std::vector<int> arrival(size_t(kImgW) * kInkH, INT_MAX);
    int seedIndex = 0, lastQuarter = 0, delay = 2;  // bytes 0x21455 / 0x21456 / 0x21457 (dword 0x20000)
    for (int t = kPartTick + 1; t < kPartTick + 1000; t++) {
        int m = c.counter(t);
        if (m >= kEndCounter) break;
        if ((m & 3) == 0 && ((m >> 2) & 255) != lastQuarter) {  // once per 4 counter steps
            lastQuarter = (m >> 2) & 255;
            uint32_t seed = exe.u32(kSeedTable + 4 * seedIndex);
            if (--delay < 0 && seed != 0) {
                seedIndex++;
                push(int32_t(seed));
            }
        }
        size_t n = list.size();  // entries pushed during this layer are handled by the next one
        for (size_t j = 0; j < n; j++) {
            int32_t p = list[j];
            if (page[size_t(p)] <= 0x40) continue;
            arrival[size_t(p / kStride) * kImgW + p % kStride] = t;
            page[size_t(p)] = 0x40;
            push(p - 1);
            push(p + 1);
            push(p + kStride);
            push(p - kStride);
        }
    }
    return arrival;
}

// The ink redraw's reveal field: the arrival tick R - kTickBase at kRevealScale x kRevealScale samples per original
// pixel over the 224x92 ink frame (row-major, row 0 at the top), kNever only if there is no ink at all. A sample takes
// the region (4-connected ink, one per seed) of its nearest original ink pixel and the Gaussian-weighted mean of that
// region's arrival ticks around it. Inside the ink this follows the original's fronts smoothly (along a straight
// front R(x) = x - 0.5 for the pixel [n, n + 1) with R = n, so R < tick + 0.5 is exactly the original's pixel set at
// whole ticks); outside the original's pixels (the redraw's edges, the ink image's holes) the nearby ticks carry on,
// and two regions never mix.
std::vector<float> revealField(const std::vector<int>& arrival) {
    constexpr int W = kImgW, H = kInkH, S = kRevealScale, win = 2;
    auto inked = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < W && y < H && arrival[size_t(y) * W + x] != INT_MAX;
    };
    // regions: 4-connected components of the ink pixels
    std::vector<int> region(size_t(W) * H, -1);
    std::vector<int> queue;
    int regions = 0;
    for (int i = 0; i < W * H; i++) {
        if (!inked(i % W, i / W) || region[size_t(i)] >= 0) continue;
        region[size_t(i)] = regions;
        queue.assign(1, i);
        for (size_t j = 0; j < queue.size(); j++) {
            int x = queue[j] % W, y = queue[j] / W;
            const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
            for (const auto& n : nb)
                if (inked(n[0], n[1]) && region[size_t(n[1]) * W + n[0]] < 0) {
                    region[size_t(n[1]) * W + n[0]] = regions;
                    queue.push_back(n[1] * W + n[0]);
                }
        }
        regions++;
    }
    // the nearest ink pixel of every pixel (multi-source BFS, 8 neighbours), for samples far from the ink
    std::vector<int> nearest(size_t(W) * H, -1);
    queue.clear();
    for (int i = 0; i < W * H; i++)
        if (region[size_t(i)] >= 0) {
            nearest[size_t(i)] = i;
            queue.push_back(i);
        }
    for (size_t j = 0; j < queue.size(); j++) {
        int x = queue[j] % W, y = queue[j] / W;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H || nearest[size_t(ny) * W + nx] >= 0) continue;
                nearest[size_t(ny) * W + nx] = nearest[size_t(queue[j])];
                queue.push_back(ny * W + nx);
            }
    }
    // per sub-pixel position f of a sample and pixel offset d: the squared distance to that pixel's centre along one
    // axis and its Gaussian factor (the weights are separable)
    double d2[S][2 * win + 1], g[S][2 * win + 1];
    for (int f = 0; f < S; f++)
        for (int d = -win; d <= win; d++) {
            double e = d + 0.5 - (f + 0.5) / S;
            d2[f][d + win] = e * e;
            g[f][d + win] = std::exp(-0.5 * e * e / (kRevealSigma * kRevealSigma));
        }
    std::vector<float> out(size_t(W) * S * H * S, kNever);
    for (int sy = 0; sy < H * S; sy++)
        for (int sx = 0; sx < W * S; sx++) {
            const int cx = sx / S, cy = sy / S, fx = sx % S, fy = sy % S;  // pixel and position in it
            int best = -1;
            double bestD = 1e30;
            for (int dy = -win; dy <= win; dy++)
                for (int dx = -win; dx <= win; dx++) {
                    int x = cx + dx, y = cy + dy;
                    if (!inked(x, y)) continue;
                    double d = d2[fx][dx + win] + d2[fy][dy + win];
                    if (d < bestD) bestD = d, best = y * W + x;
                }
            if (best < 0) best = nearest[size_t(cy) * W + cx];
            if (best < 0) continue;
            const int k = region[size_t(best)];
            double sw = 0, sr = 0;
            for (int dy = -win; dy <= win; dy++)
                for (int dx = -win; dx <= win; dx++) {
                    int x = cx + dx, y = cy + dy;
                    if (!inked(x, y) || region[size_t(y) * W + x] != k) continue;
                    double w = g[fx][dx + win] * g[fy][dy + win];
                    sw += w;
                    sr += w * arrival[size_t(y) * W + x];
                }
            double r = sw > 1e-9 ? sr / sw : double(arrival[size_t(best)]);
            out[size_t(sy) * W * S + sx] = float(r - kTickBase);
        }
    return out;
}

// The distance (original pixels, up to kHideMax) from every reveal-field sample to the ink redraw (its alpha >= 0.5,
// read bilinearly), row-major like revealField: the page pass hides the sketch near the ink.
std::vector<float> inkDistance(const ArtImage& art) {
    const int S = kRevealScale, W = kImgW * S, H = kInkH * S, R = int(kHideMax) * S;
    auto alpha = [&](int x, int y) {
        x = std::clamp(x, 0, art.w - 1);
        y = std::clamp(y, 0, art.h - 1);
        return float(art.px[(size_t(y) * art.w + x) * 4 + 3]);
    };
    std::vector<uint8_t> in(size_t(W) * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = (x + 0.5f) / W * art.w - 0.5f, v = (y + 0.5f) / H * art.h - 0.5f;
            int i = int(std::floor(u)), j = int(std::floor(v));
            float fu = u - i, fv = v - j;
            float a = (alpha(i, j) * (1 - fu) + alpha(i + 1, j) * fu) * (1 - fv) +
                      (alpha(i, j + 1) * (1 - fu) + alpha(i + 1, j + 1) * fu) * fv;
            in[size_t(y) * W + x] = a >= 127.5f;
        }
    // the horizontal distance (samples) to the nearest ink sample in the row, then the nearest over the rows within R
    std::vector<float> hd(size_t(W) * H, 1e9f), out(size_t(W) * H, kHideMax);
    for (int y = 0; y < H; y++) {
        float* row = &hd[size_t(y) * W];
        for (int x = 0, last = -1000000; x < W; x++) {
            if (in[size_t(y) * W + x]) last = x;
            row[x] = float(x - last);
        }
        for (int x = W - 1, last = 1000000; x >= 0; x--) {
            if (in[size_t(y) * W + x]) last = x;
            row[x] = std::min(row[x], float(last - x));
        }
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float best = 1e18f;
            for (int dy = -R; dy <= R; dy++) {
                if (y + dy < 0 || y + dy >= H) continue;
                float h = hd[size_t(y + dy) * W + x];
                best = std::min(best, h * h + float(dy * dy));
            }
            out[size_t(y) * W + x] = std::min(kHideMax, std::sqrt(best) / S);
        }
    return out;
}

// Catmull-Rom weight of a sample at distance t.
double catmullRom(double t) {
    t = std::fabs(t);
    if (t < 1) return 1.5 * t * t * t - 2.5 * t * t + 1;
    if (t < 2) return -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2;
    return 0;
}

// The fill fraction a ring pixel needs, as a function of the fraction u of its tick, so that the 0.5 contour of the
// Catmull-Rom interpolation moves linearly: a straight front (pixels ..., 1, 1, sigma, 0, 0, ...) has its contour
// at 0.5 + u pixels past the last full pixel centre. Solved by bisection; u = 0 -> 0, u = 1 -> 1.
std::array<float, 33> frontRamp() {
    auto contour = [](double sigma) {  // contour position past the last full pixel (centre 0), ring pixel at 1
        auto value = [&](double x) {
            double v = 0;
            for (int k = -4; k <= 1; k++) v += catmullRom(x - k) * (k <= 0 ? 1.0 : sigma);
            return v;
        };
        double lo = 0, hi = 2;  // value(lo) > 0.5 >= value(hi)
        for (int i = 0; i < 60; i++) {
            double mid = 0.5 * (lo + hi);
            (value(mid) > 0.5 ? lo : hi) = mid;
        }
        return lo;
    };
    std::array<float, 33> r{};
    for (int i = 0; i <= 32; i++) {
        double want = 0.5 + i / 32.0, lo = 0, hi = 1;
        for (int k = 0; k < 60; k++) {
            double mid = 0.5 * (lo + hi);
            (contour(mid) < want ? lo : hi) = mid;
        }
        r[size_t(i)] = float(0.5 * (lo + hi));
    }
    r[0] = 0;
    r[32] = 1;
    return r;
}

// Pads a w x h float image with kPad zero texels on every side.
std::vector<float> padded(const std::vector<float>& img, int w, int h, int channels, float fill = 0.0f) {
    int pw = w + 2 * kPad, ph = h + 2 * kPad;
    std::vector<float> out(size_t(pw) * ph * channels, fill);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int ch = 0; ch < channels; ch++)
                out[(size_t(y + kPad) * pw + x + kPad) * channels + ch] = img[(size_t(y) * w + x) * channels + ch];
    return out;
}

// The ink image as GPU data, per pixel (unpadded, row-major):
//   shape  RG    arrival tick - kTickBase (kNever without ink), outline weight
//   tone   RGBA  colour x weight, weight: the body 0x4b and the greys 0x41..0x4a (shading and anti-aliasing)
//   red    RGBA  colour, 1: the red edges 0x4c..0x4f
// Outline weight: 1, except for a grey its ink share: how much body ink, blended over white paper, gives that grey.
// The greys at the letters' edges are the art's anti-aliasing against white, so the outline then lies where the art's
// edge is instead of around its light rim pixels.
struct InkTexels {
    std::vector<float> shape, tone, red;
};

InkTexels inkTexels(const DefaultPalette& pal, const std::vector<uint8_t>& ink, const std::vector<int>& arrival) {
    const size_t n = ink.size();
    const Vec3 body = pal.rgb[0x4b];
    InkTexels t;
    t.shape.assign(n * 2, 0.0f);
    t.tone.assign(n * 4, 0.0f);
    t.red.assign(n * 4, 0.0f);
    for (size_t i = 0; i < n; i++) {
        t.shape[2 * i] = arrival[i] == INT_MAX ? kNever : float(arrival[i]) - kTickBase;
        if (!ink[i]) continue;
        Vec3 c = pal.rgb[0x40 + ink[i]];
        bool grey = c.x == c.y && c.y == c.z;
        float w = grey ? std::clamp((1.0f - c.x) / (1.0f - body.x), 0.0f, 1.0f) : 1.0f;
        t.shape[2 * i + 1] = w;
        std::vector<float>& dst = (grey || ink[i] == 0x0b) ? t.tone : t.red;
        float k = (grey || ink[i] == 0x0b) ? w : 1.0f;
        dst[4 * i] = c.x * k;
        dst[4 * i + 1] = c.y * k;
        dst[4 * i + 2] = c.z * k;
        dst[4 * i + 3] = k;
    }
    return t;
}

// GLSL shared by the passes: clamped texel fetch and exact Catmull-Rom (9 bilinear taps: the two positive middle
// weights of each axis share one tap; the 4-tap trick of texBicubic needs weights of one sign).
const char* kLogoGLSL = R"(
const float PAD = 2.0;
const vec2 LOGO = vec2(48.0, 50.0);
vec4 fetch(sampler2D t, ivec2 p) { return texelFetch(t, clamp(p, ivec2(0), textureSize(t, 0) - 1), 0); }
// t: texture pixel coordinates (texel centres at +0.5); the texture must filter linearly
vec4 catmullRom(sampler2D tex, vec2 t) {
    vec2 sz = vec2(textureSize(tex, 0));
    vec2 p = t - 0.5, i = floor(p), f = p - i;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 c0 = (i - 0.5) / sz, c12 = (i + 0.5 + w2 / w12) / sz, c3 = (i + 2.5) / sz;
    vec4 r0 = textureLod(tex, vec2(c0.x, c0.y), 0.0) * w0.x + textureLod(tex, vec2(c12.x, c0.y), 0.0) * w12.x +
              textureLod(tex, vec2(c3.x, c0.y), 0.0) * w3.x;
    vec4 r1 = textureLod(tex, vec2(c0.x, c12.y), 0.0) * w0.x + textureLod(tex, vec2(c12.x, c12.y), 0.0) * w12.x +
              textureLod(tex, vec2(c3.x, c12.y), 0.0) * w3.x;
    vec4 r2 = textureLod(tex, vec2(c0.x, c3.y), 0.0) * w0.x + textureLod(tex, vec2(c12.x, c3.y), 0.0) * w12.x +
              textureLod(tex, vec2(c3.x, c3.y), 0.0) * w3.x;
    return r0 * w0.y + r1 * w12.y + r2 * w3.y;
}
// The ink redraw's reveal at ink-frame position q (original pixels): 1 where the fill has arrived, R < tau + 0.5,
// with an anti-aliased front (needs uniform control flow: derivatives)
float inkReveal(sampler2D reveal, vec2 q, float tau) {
    float m = tau + 0.5 - texture(reveal, q / vec2(224.0, 92.0)).r;
    return clamp(m / max(fwidth(m), 1e-4) + 0.5, 0.0, 1.0);
}
)";

const char* kPageFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D textBuf;   // T of the 320x80 text buffer (0..1 = byte/255), stored bottom-up
uniform sampler2D grain;     // core paper grain (shade row e)
uniform sampler2D sketch;    // darkness n, padded by PAD
uniform sampler2D sketchArt; // the redraw: page value v / 63 over the sketch's 224x83 frame, row 0 at the top
uniform bool redraw;         // true: sketchArt, false: sketch
uniform sampler2D fill;      // this frame's fill fractions (G: every ink pixel), padded by PAD
uniform vec2 textTexel;      // text-buffer pixels per texel
uniform bool textArt;        // true: the letters from textMask (textBuf then holds only the shadow)
uniform sampler2D textMask;  // the text redraw: coverage over the 320x80 text buffer, row 0 at the top
uniform bool inkArt;         // true: the ink is the redraw, drawn by the ink pass
uniform sampler2D reveal;    // its reveal field: R arrival tick - TICK_BASE, G distance to the ink (original px)
uniform float tau;           // tick - TICK_BASE

// 0x5623 text mapping: buffer rows 0..39 -> page rows 10..49, rows 40..79 -> 158..197, x + 4
float textValue(vec2 p) {
    float bx = p.x - 4.0, by;
    if (bx < 0.0) return 0.0;
    if (p.y >= 10.0 && p.y < 50.0) by = clamp(p.y - 10.0, 0.5 * textTexel.y, 40.0 - 0.5 * textTexel.y);
    else if (p.y >= 158.0 && p.y < 198.0) by = clamp(p.y - 118.0, 40.0 + 0.5 * textTexel.y, 80.0 - 0.5 * textTexel.y);
    else return 0.0;
    return texture(textBuf, vec2(bx / 320.0, 1.0 - by / 80.0)).r;
}

// Sketch darkness (0..15) at sketch pixel coordinates q: Catmull-Rom, kept inside the range of the 2x2 nearest
// texels (no overshoot halos).
float sketchDark(vec2 q) {
    vec2 t = q + PAD;
    float n = catmullRom(sketch, t).r;
    ivec2 i = ivec2(floor(t - 0.5));
    float a = fetch(sketch, i).r, b = fetch(sketch, i + ivec2(1, 0)).r;
    float c = fetch(sketch, i + ivec2(0, 1)).r, d = fetch(sketch, i + ivec2(1, 1)).r;
    return clamp(n, min(min(a, b), min(c, d)), max(max(a, b), max(c, d)));
}

// The redraw's letters at text-buffer position b, anti-aliased at the output resolution: the mask's 0.5 contour is
// the edge (Catmull-Rom while the mask is magnified, its mipmaps when it is minified). Uniform control flow.
float textCoverage(vec2 b) {
    vec2 m = b / vec2(320.0, 80.0), t = m * vec2(textureSize(textMask, 0));
    float k = max(length(dFdx(t)), length(dFdy(t)));  // mask texels per output pixel
    float c = mix(catmullRom(textMask, t).r, texture(textMask, m).r, clamp(k - 1.0, 0.0, 1.0));
    return clamp((c - 0.5) / max(fwidth(c), 1e-4) + 0.5, 0.0, 1.0);
}

void main() {
    vec2 vga = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    // page value v (63 = white paper .. 0 = black): the text (rows 10..49, 158..197) and the sketch (overwrites rows
    // 50..132, where there is no text; min() only meets their faint tails at row 50)
    float T = textValue(vga);
    if (textArt) {
        // the sharp letters of each half: "present the" (buffer rows 1..32) on page rows 10..46, "intro" (40..71) on
        // 158..197 (the mask also holds the other line, which the page does not show there)
        bool top = vga.y < 100.0;
        vec2 b = vec2(vga.x - 4.0, vga.y - (top ? 10.0 : 118.0));
        float c = textCoverage(b);
        bool line = b.x >= 0.0 && (top ? vga.y >= 10.0 && vga.y < 47.0 : vga.y >= 158.0 && vga.y < 198.0);
        T = min(1.0, T + (line ? c : 0.0));
    }
    float v = 63.0 - 63.75 * T;
    vec2 q = vga - LOGO;
    // Where the ink redraw has arrived it hides the sketch, within 1 original pixel of the ink: in the original the
    // ink pixels cover every pencil pixel except 50 (the "stx" signature and single pixels at the ink's edge), and
    // every stroke of the sketch redraw except the signature lies within 1 px of the ink redraw (reveal channel G:
    // the distance to it). (Derivatives: uniform control flow.)
    float hide = 0.0;
    if (inkArt) {
        float near = 1.0 - smoothstep(0.75, 1.25, texture(reveal, q / vec2(224.0, 92.0)).g);
        hide = near * inkReveal(reveal, q, tau);
    }
    if (redraw) {
        // The ink covers the sketch wherever the ink image has ink, its light grey rims and highlights included
        // (0x572f writes every ink pixel over the page). The ink pass treats the light greys as partial coverage
        // and lets the page show through them, which shows the redraw's crisp strokes there: hide the strokes where
        // the ink has arrived.
        // (derivatives and the mipmapped read outside the frame test: they need uniform control flow)
        float m = catmullRom(fill, q + PAD).g - 0.5;
        float inked = clamp(m / max(length(vec2(dFdx(m), dFdy(m))), 1e-6) + 0.5, 0.0, 1.0);
        if (inkArt) inked = max(inked, hide);
        float art = 63.0 * texture(sketchArt, q / vec2(224.0, 83.0)).r;
        if (q.x >= 0.0 && q.x < 224.0 && q.y >= 0.0 && q.y < 83.0) v = min(v, mix(art, 63.0, inked));
    } else if (q.x > -2.0 && q.x < 226.0 && q.y > -2.0 && q.y < 85.0) {
        float sk = 63.0 - 4.0 * sketchDark(q);
        v = min(v, inkArt ? mix(sk, 63.0, hide) : sk);
    }
    // 0x1949: LUT[grain][v] = max(0, v + 2e - 64) through the grey ramp
    float e = paperGrain(grain, vga);
    o = vec4(vec3(greyRamp(max(0.0, v + 2.0 * e - 64.0))), 1.0);
}
)";

// The ink pass: a quad over the ink image (VGA rectangle), premultiplied output.
const char* kInkVS = R"(
uniform vec4 rect;   // VGA x, y, w, h
out vec2 vga;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vga = rect.xy + c * rect.zw;
    gl_Position = vec4(vga.x / 160.0 - 1.0, 1.0 - vga.y / 100.0, 0.0, 1.0);
}
)";

// Per frame, per ink texel: R = fill fraction x outline weight (the ink's outline), G = fill fraction of every ink
// pixel, the light greys included (where the ink covers the sketch). The fraction rises from 0 to 1 during the tick
// before the pixel's arrival tick, along ramp[] (see frontRamp).
const char* kFillFS = R"(
out vec4 o;
uniform sampler2D shape;     // RG: arrival tick - TICK_BASE, outline weight (padded by PAD; > 0 on every ink pixel)
uniform float tau;           // tick - TICK_BASE
uniform float ramp[33];
void main() {
    vec2 a = texelFetch(shape, ivec2(gl_FragCoord.xy), 0).rg;
    float x = clamp(tau - a.x + 1.0, 0.0, 1.0) * 32.0;
    int i = min(int(x), 31);
    float s = mix(ramp[i], ramp[i + 1], x - float(i));
    o = vec4(s * a.y, a.y > 0.0 ? s : 0.0, 0.0, 0.0);
}
)";

const char* kInkFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D fill;      // R: fill fraction x outline weight (this frame)
uniform sampler2D tone;      // body and grey colours x weight, weight
uniform sampler2D red;       // red colours, indicator

// anti-aliased step of a field at 0, over at least `soft` field units
float aaStep(float m, float soft) {
    return clamp(m / max(length(vec2(dFdx(m), dFdy(m))), soft) + 0.5, 0.0, 1.0);
}

// Weighted colour c (rgb x w, w), normalised and kept inside the range of the nearest 2x2 coloured texels.
vec3 toneColour(vec4 c, vec2 t) {
    vec3 r = c.rgb / max(c.a, 0.05);
    ivec2 i = ivec2(floor(t - 0.5));
    vec3 lo = vec3(1.0), hi = vec3(0.0);
    for (int k = 0; k < 4; k++) {
        vec4 n = fetch(tone, i + ivec2(k & 1, k >> 1));
        if (n.a > 0.0) { lo = min(lo, n.rgb / n.a); hi = max(hi, n.rgb / n.a); }
    }
    return hi.r >= lo.r ? clamp(r, lo, hi) : clamp(r, 0.0, 1.0);
}

void main() {
    vec2 t = vga - LOGO + PAD;
    float a = aaStep(catmullRom(fill, t).r - 0.5, 1e-6);
    vec4 c = catmullRom(tone, t), r = catmullRom(red, t);
    // the red edges win where their interpolated indicator exceeds the other ink's: red strokes with a boundary
    // softened over about a third of an original pixel, so the pixel steps of the 1-pixel lines do not show
    vec3 col = mix(toneColour(c, t), clamp(r.rgb / max(r.a, 0.05), 0.0, 1.0), aaStep(r.a - c.a, 0.3));
    o = vec4(col * a, a);
}
)";

// The text redraw's shadow (0x5623's copy at (20,4) = the sharp copy's position + (4,3), at half strength) into the
// text buffer target, which then gets the 5 blur calls.
const char* kShadowFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D mask;  // coverage over the 320x80 text buffer, row 0 at the top
void main() {
    vec2 b = vec2(uv.x * 320.0, (1.0 - uv.y) * 80.0) - vec2(4.0, 3.0);
    float inside = (b.x >= 0.0 && b.y >= 0.0 && b.x < 320.0 && b.y < 80.0) ? 1.0 : 0.0;
    o = vec4(0.5 * inside * texture(mask, b / vec2(320.0, 80.0)).r, 0.0, 0.0, 1.0);
}
)";

// The ink redraw over the ink frame, revealed by the fill (premultiplied output).
const char* kInkArtFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D inkPic;    // premultiplied RGBA over the 224x92 ink frame, row 0 at the top
uniform sampler2D reveal;    // arrival tick - TICK_BASE
uniform float tau;           // tick - TICK_BASE
void main() {
    vec2 q = vga - LOGO;
    o = texture(inkPic, q / vec2(224.0, 92.0)) * inkReveal(reveal, q, tau);
}
)";

struct LogoScene : Scene {
    Shader page, fillShader, inkShader, shadowShader, inkArtShader;
    Texture sketchTex, sketchArt, shapeTex, toneTex, redTex, textMask, inkPic, revealTex;
    bool redraw = false;   // sketchArt holds the redraw (art/sketch.png)
    bool textArt = false;  // textMask holds the text redraw (art/text.png)
    bool inkArt = false;   // inkPic holds the ink redraw (art/ink.png), revealTex its reveal field
    Target fill;  // per ink texel: fill fraction x outline weight, fill fraction; rebuilt for every frame's tick
    std::array<float, 33> ramp{};
    std::string text;
    // the blurred text buffer, per output size
    struct TextCache { Target t; int w = 0, h = 0; };
    std::array<TextCache, 2> cache;
    int nextCache = 0;

    static void upload(Texture& t, int channels, const std::vector<float>& img, int h, bool linear) {
        static const GLenum ifmt[] = {0, GL_R16F, GL_RG32F, 0, GL_RGBA16F}, fmt[] = {0, GL_RED, GL_RG, 0, GL_RGBA};
        std::vector<float> p = padded(img, kImgW, h, channels);
        t.createRaw(kImgW + 2 * kPad, h + 2 * kPad, ifmt[channels], fmt[channels], GL_FLOAT, p.data(), linear, false);
    }

    void init(Ctx& c) override {
        const PaperExe& exe = *c.exe;
        c.shared<Font>();
        c.shared<CoreTextures>();
        text = exe.cstr(kText);

        std::vector<uint8_t> sk = unpackNibbles(exe, kSketchImage, kSketchH);
        upload(sketchTex, 1, std::vector<float>(sk.begin(), sk.end()), kSketchH, true);
        ArtImage art;
        redraw = loadArtGrey(c, "sketch", &art);
        if (redraw)  // grey = 255 v / 63 over the 224x83 frame; mipmapped for small frames, clamped at the edges
            sketchArt.createRaw(art.w, art.h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, art.px.data(), true, false, true);
        textArt = loadArtGrey(c, "text", &art);
        if (textArt)  // coverage over the 320x80 text buffer
            textMask.createRaw(art.w, art.h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, art.px.data(), true, false, true);

        std::vector<uint8_t> ink = unpackNibbles(exe, kInkImage, kInkH);
        std::vector<int> arrival = simulateFill(c, ink);
        InkTexels it = inkTexels(DefaultPalette(exe), ink, arrival);
        upload(shapeTex, 2, it.shape, kInkH, false);
        upload(toneTex, 4, it.tone, kInkH, true);
        upload(redTex, 4, it.red, kInkH, true);
        inkArt = loadArtRgba(c, "ink", &art);
        if (inkArt) {  // colour with alpha over the 224x92 ink frame, premultiplied for filtering
            for (size_t i = 0; i < art.px.size(); i += 4)
                for (size_t k = 0; k < 3; k++) art.px[i + k] = uint8_t((art.px[i + k] * art.px[i + 3] + 127) / 255);
            inkPic.createRaw(art.w, art.h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, art.px.data(), true, false, true);
            std::vector<float> r = revealField(arrival), d = inkDistance(art), rg(r.size() * 2);
            for (size_t i = 0; i < r.size(); i++) {
                rg[2 * i] = r[i];
                rg[2 * i + 1] = d[i];
            }
            revealTex.createRaw(kImgW * kRevealScale, kInkH * kRevealScale, GL_RG32F, GL_RG, GL_FLOAT, rg.data(), true,
                                false);
        }

        ramp = frontRamp();
        page.build(kFullscreenVS, std::string(kTexturesGLSL) + kLogoGLSL + kPageFS, "logo page");
        fillShader.build(kFullscreenVS, kFillFS, "logo fill");
        inkShader.build(kInkVS, std::string(kLogoGLSL) + kInkFS, "logo ink");
        if (textArt) shadowShader.build(kFullscreenVS, kShadowFS, "logo text shadow");
        if (inkArt) inkArtShader.build(kInkVS, std::string(kLogoGLSL) + kInkArtFS, "logo ink redraw");
        fill.create(kImgW + 2 * kPad, kInkH + 2 * kPad, false, GL_RG16F);
    }

    // 0x5623's text buffer T: shadow at (20,4) blurred 4 times and halved, the sharp copy at (16,1) added (saturating),
    // one more blur. The halving is applied to the shadow before its blurs (they are linear).
    void buildText(Ctx& c, Target& t) {
        Font& font = c.shared<Font>();
        const Mat4 proj = Font::pixelProj(320, 80);
        t.bind();
        gl::ClearColor(0, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        font.draw(layoutText(font.data(), text, 20, 4), proj, GlyphOp::Add, 0.5f);
        blur(c, t, 8, Edge::Zero, 320, 80, GL_R16);
        t.bind();
        font.draw(layoutText(font.data(), text, 16, 1), proj, GlyphOp::Add, 1.0f);
        blur(c, t, 2, Edge::Zero, 320, 80, GL_R16);
    }

    // With the text redraw the buffer holds only the shadow: the mask at (+4,+3) at half strength, blurred 5 times
    // (10 passes, as the original's shadow copy); the page pass adds the sharp letters from the mask.
    void buildTextShadow(Ctx& c, Target& t) {
        t.bind();
        setBlend(Blend::None);  // the page may be rendered inside another part's passes (the peels)
        shadowShader.use();
        textMask.bind(0);
        shadowShader.set("mask", 0);
        drawFullscreen();
        blur(c, t, 10, Edge::Zero, 320, 80, GL_R16);
    }

    const Target& textFor(Ctx& c, int w, int h) {
        int th = std::max(1, int(std::lround(h * 0.4)));
        for (TextCache& e : cache)
            if (e.w == w && e.h == th) return e.t;
        TextCache& e = cache[size_t(nextCache)];
        nextCache = (nextCache + 1) % int(cache.size());
        e.t.create(w, th, false, GL_R16);
        e.w = w;
        e.h = th;
        if (textArt)
            buildTextShadow(c, e.t);
        else
            buildText(c, e.t);
        return e.t;
    }

    void drawPage(Ctx& c, const Target& tb, double tick) {
        page.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, tb.tex);
        c.shared<CoreTextures>().grain.bind(1);
        sketchTex.bind(2);
        (redraw ? sketchArt : sketchTex).bind(3);
        fill.bindTex(4);
        page.set("textBuf", 0);
        page.set("grain", 1);
        page.set("sketch", 2);
        page.set("sketchArt", 3);
        page.set("fill", 4);
        page.set("redraw", redraw ? 1 : 0);
        page.set("textTexel", 320.0f / tb.w, 80.0f / tb.h);
        page.set("textArt", textArt ? 1 : 0);
        page.set("inkArt", inkArt ? 1 : 0);
        if (textArt) {
            textMask.bind(5);
            page.set("textMask", 5);
        }
        if (inkArt) {
            revealTex.bind(6);
            page.set("reveal", 6);
            page.set("tau", float(tick - kTickBase));
        }
        drawFullscreen();
    }

    // The fill fractions at this tick into `fill` (leaves another target bound).
    void updateFill(double tick) {
        fill.bind();
        fillShader.use();
        shapeTex.bind(0);
        fillShader.set("shape", 0);
        fillShader.set("tau", float(tick - kTickBase));
        gl::Uniform1fv(fillShader.loc("ramp"), 33, ramp.data());
        drawFullscreen();
    }

    void drawInk() {
        inkShader.use();
        fill.bindTex(0);
        toneTex.bind(1);
        redTex.bind(2);
        inkShader.set("fill", 0);
        inkShader.set("tone", 1);
        inkShader.set("red", 2);
        inkShader.set("rect", float(kLogoX - kPad), float(kLogoY - kPad), float(kImgW + 2 * kPad),
                      float(kInkH + 2 * kPad));
        drawQuad();
    }

    void drawInkArt(double tick) {
        inkArtShader.use();
        inkPic.bind(0);
        revealTex.bind(1);
        inkArtShader.set("inkPic", 0);
        inkArtShader.set("reveal", 1);
        inkArtShader.set("tau", float(tick - kTickBase));
        inkArtShader.set("rect", float(kLogoX), float(kLogoY), float(kImgW), float(kInkH));
        drawQuad();
    }

    void render(Ctx& c, double tick) override {
        Target& out = *c.out;
        const Target& tb = textFor(c, out.w, out.h);
        setBlend(Blend::None);
        updateFill(tick);
        out.bind();
        drawPage(c, tb, tick);
        setBlend(Blend::Premul);
        if (inkArt)
            drawInkArt(tick);
        else
            drawInk();
        setBlend(Blend::None);
    }
};

REGISTER_SCENE(LogoScene, "logo");

}  // namespace
