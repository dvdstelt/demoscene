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
//   - text: the core font's anti-aliased outlines drawn into a UNORM intensity buffer covering the original's 320x80
//     text buffer (saturating adds as the byte adds), the 0xf47 blurs as Gaussians of the same variance. The buffer is
//     cached per output size.
//   - sketch: Catmull-Rom on the 4-bit darkness values, clamped to the range of the nearest 2x2 texels (no ringing).
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
)";

const char* kPageFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D textBuf;   // T of the 320x80 text buffer (0..1 = byte/255), stored bottom-up
uniform sampler2D grain;     // core paper grain (shade row e)
uniform sampler2D sketch;    // darkness n, padded by PAD
uniform vec2 textTexel;      // text-buffer pixels per texel

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

void main() {
    vec2 vga = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    // page value v (63 = white paper .. 0 = black): the text (rows 10..49, 158..197) and the sketch (overwrites rows
    // 50..132, where there is no text; min() only meets their faint tails at row 50)
    float v = 63.0 - 63.75 * textValue(vga);
    vec2 q = vga - LOGO;
    if (q.x > -2.0 && q.x < 226.0 && q.y > -2.0 && q.y < 85.0) v = min(v, 63.0 - 4.0 * sketchDark(q));
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

// Per frame, per ink texel: fill fraction x outline weight. The fraction rises from 0 to 1 during the tick before
// the pixel's arrival tick, along ramp[] (see frontRamp).
const char* kFillFS = R"(
out vec4 o;
uniform sampler2D shape;     // RG: arrival tick - TICK_BASE, outline weight (padded by PAD)
uniform float tau;           // tick - TICK_BASE
uniform float ramp[33];
void main() {
    vec2 a = texelFetch(shape, ivec2(gl_FragCoord.xy), 0).rg;
    float x = clamp(tau - a.x + 1.0, 0.0, 1.0) * 32.0;
    int i = min(int(x), 31);
    o = vec4(mix(ramp[i], ramp[i + 1], x - float(i)) * a.y);
}
)";

const char* kInkFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D fill;      // fill fraction x outline weight (this frame)
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

struct LogoScene : Scene {
    Shader page, fillShader, inkShader;
    Texture sketchTex, shapeTex, toneTex, redTex;
    Target fill;  // fill fraction x outline weight per ink texel, rebuilt for every frame's tick
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

        std::vector<uint8_t> ink = unpackNibbles(exe, kInkImage, kInkH);
        InkTexels it = inkTexels(DefaultPalette(exe), ink, simulateFill(c, ink));
        upload(shapeTex, 2, it.shape, kInkH, false);
        upload(toneTex, 4, it.tone, kInkH, true);
        upload(redTex, 4, it.red, kInkH, true);

        ramp = frontRamp();
        page.build(kFullscreenVS, std::string(kTexturesGLSL) + kLogoGLSL + kPageFS, "logo page");
        fillShader.build(kFullscreenVS, kFillFS, "logo fill");
        inkShader.build(kInkVS, std::string(kLogoGLSL) + kInkFS, "logo ink");
        fill.create(kImgW + 2 * kPad, kInkH + 2 * kPad, false, GL_R16F);
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

    const Target& textFor(Ctx& c, int w, int h) {
        int th = std::max(1, int(std::lround(h * 0.4)));
        for (TextCache& e : cache)
            if (e.w == w && e.h == th) return e.t;
        TextCache& e = cache[size_t(nextCache)];
        nextCache = (nextCache + 1) % int(cache.size());
        e.t.create(w, th, false, GL_R16);
        e.w = w;
        e.h = th;
        buildText(c, e.t);
        return e.t;
    }

    void drawPage(Ctx& c, const Target& tb) {
        page.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, tb.tex);
        c.shared<CoreTextures>().grain.bind(1);
        sketchTex.bind(2);
        page.set("textBuf", 0);
        page.set("grain", 1);
        page.set("sketch", 2);
        page.set("textTexel", 320.0f / tb.w, 80.0f / tb.h);
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

    void render(Ctx& c, double tick) override {
        Target& out = *c.out;
        const Target& tb = textFor(c, out.w, out.h);
        setBlend(Blend::None);
        updateFill(tick);
        out.bind();
        drawPage(c, tb);
        setBlend(Blend::Premul);
        drawInk();
        setBlend(Blend::None);
    }
};

REGISTER_SCENE(LogoScene, "logo");

}  // namespace
