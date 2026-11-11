// The JIZZ core library, part 2: the thick lines 0x1122f / 0x1145c as anti-aliased strokes, the jittered quads
// 0x183a9, and the glow chain shared by TBL (0x11986), 0x17003 and 0x169e7: line buffer >> 3, the 2x2 box 0x11eb2
// (as a Gaussian of the same variance), the trail 0x12111 and the fade towards a colour 0x11396 (notes-core 8.4-8.6).
#include <algorithm>
#include <cmath>

#include "jizz/core.h"
#include "jizz/coregl.h"

namespace jizz {

// =============================================================================================== lines
bool onScreen(const Segment& s) {  // 0x1145c: unsigned compares of the integer end points against 320 / 200
    auto in = [](float v, float n) { return v >= -0.5f && v < n - 0.5f; };
    return in(s.x0, 320) && in(s.x1, 320) && in(s.y0, 200) && in(s.y1, 200);
}

static inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void jitterQuads(Rng& rng, const Quad* q, int first, int last, uint32_t mask, std::vector<Segment>& out) {
    for (int n = first; n <= last; n++)  // 0x183a9
        for (int k = 0; k < 4; k++) {
            uint32_t r = rng.next();
            int j = (k + 1) & 3;
            Segment s;
            s.x0 = q[n].x[k] + float(r & mask); r = ror(r, 4);
            s.y0 = q[n].y[k] + float(r & mask); r = ror(r, 4);
            s.x1 = q[n].x[j] + float(r & mask); r = ror(r, 4);
            s.y1 = q[n].y[j] + float(r & mask);
            if (onScreen(s) && (s.x0 != s.x1 || s.y0 != s.y1)) out.push_back(s);  // dx + dy == 0: nothing
        }
}

// The stroke is the parallelogram the DDA of 0x1145c fills: along the major axis from half a pixel before the
// first end pixel to half a pixel after the last, 4 pixels thick along the minor axis starting at the end pixels'
// rows (x-major: rows y..y+3) or columns (y-major: x..x+3). Coverage is analytic (box filter per axis of the
// parallelogram), in target pixels.
static const char* kLineVS = R"(
layout(location = 0) in vec4 seg;   // x0, y0, x1, y1: pixel indices of the original page
uniform vec2 size;                  // target size in pixels
out vec2 q;                         // position in the parallelogram: 0..1 along the line, 0..1 across
flat out vec2 L;                    // its extents in target pixels (between the end edges, across the line)
void main() {
    vec2 k = size / vec2(320.0, 200.0);
    vec2 c0 = seg.xy + 0.5, d = seg.zw - seg.xy;
    bool xm = abs(d.y) <= abs(d.x);                       // 0x1145c: dy <= dx draws 4 rows per column
    vec2 ext = d * (0.5 / max(xm ? abs(d.x) : abs(d.y), 1e-6));
    vec2 a = (d + 2.0 * ext) * k;
    vec2 b = (xm ? vec2(0.0, 4.0) : vec2(4.0, 0.0)) * k;
    vec2 o = (c0 - ext - (xm ? vec2(0.0, 0.5) : vec2(0.5, 0.0))) * k;
    float area = max(abs(a.x * b.y - a.y * b.x), 1e-6);
    L = vec2(area / length(b), area / max(length(a), 1e-6));
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    q = mix(-1.0 / L, 1.0 + 1.0 / L, corner);             // one pixel of margin for the anti-aliasing
    vec2 P = o + q.x * a + q.y * b;
    gl_Position = vec4(P.x / size.x * 2.0 - 1.0, 1.0 - P.y / size.y * 2.0, 0.0, 1.0);
}
)";
static const char* kLineFS = R"(
in vec2 q;
flat in vec2 L;
out vec4 o;
uniform float value;
float slab(float d, float len) { return clamp(clamp(d + 0.5, 0.0, 1.0) + clamp(len - d + 0.5, 0.0, 1.0) - 1.0, 0.0, 1.0); }
void main() {
    float cov = slab(q.x * L.x, L.x) * slab(q.y * L.y, L.y);
    if (cov <= 0.0) discard;
    o = vec4(value, value, value, cov);
}
)";

void drawLines(Ctx& c, const Segment* segs, size_t n, float value) {
    if (!c.out || n == 0) return;
    CoreGL& g = c.shared<CoreGL>();
    std::vector<float> data;
    data.reserve(n * 4);
    for (size_t i = 0; i < n; i++) {
        const Segment& s = segs[i];
        if (s.x0 == s.x1 && s.y0 == s.y1) continue;  // 0x1145c: a single point draws nothing
        data.insert(data.end(), {s.x0, s.y0, s.x1, s.y1});
    }
    if (data.empty()) return;
    c.out->bind();
    gl::Disable(GL_DEPTH_TEST);
    setBlend(Blend::Alpha);
    g.lines.use();
    g.lines.set("size", float(c.out->w), float(c.out->h));
    g.lines.set("value", value);
    gl::BindVertexArray(g.lineVao);
    gl::BindBuffer(GL_ARRAY_BUFFER, g.lineVbo);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.size() * sizeof(float)), data.data(), GL_STREAM_DRAW);
    gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(data.size() / 4));
    gl::BindVertexArray(0);
    setBlend(Blend::None);
}

// =============================================================================================== glow passes
static const char* kScaleFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D src;
uniform float k;
void main() { o = vec4(texelFetch(src, ivec2(gl_FragCoord.xy), 0).r * k, 0.0, 0.0, 1.0); }
)";
// One axis of the Gaussian: taps at whole pixels lo .. lo + n - 1 along dir, weight exp(-(i - offset)^2 / 2 sigma^2)
// evaluated incrementally (w(i + 1) = w(i) r(i), r(i + 1) = r(i) c) and normalised by their sum; two neighbouring
// taps are read together as one linear-filtered read.
static const char* kGaussFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D src;
uniform vec2 dir;      // one pixel along the axis in texture coordinates
uniform int lo, n;
uniform vec3 w0;       // w(lo), r(lo), c
uniform float k;
void main() {
    vec2 c = gl_FragCoord.xy / vec2(textureSize(src, 0));
    float acc = 0.0, ws = 0.0, w = w0.x, r = w0.y;
    for (int i = 0; i < n; i += 2) {
        float wa = w, wb = w * r;
        float s = wa + wb;
        acc += s * texture(src, c + dir * (float(lo + i) + wb / s)).r;
        ws += s;
        w = wb * r * w0.z;
        r *= w0.z * w0.z;
    }
    o = vec4(acc / ws * k, 0.0, 0.0, 1.0);
}
)";
static const char* kTrailFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D T, B;
uniform float a, bias, kB;
void main() {   // 0x12111: d += (s - d) >> n
    ivec2 p = ivec2(gl_FragCoord.xy);
    float t = texelFetch(T, p, 0).r, b = texelFetch(B, p, 0).r * kB;
    o = vec4(max(t + (b - t) * a - bias, 0.0), 0.0, 0.0, 1.0);
}
)";
static const char* kGlowFadeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D A, B;
uniform float f;
uniform vec3 C;
void main() {   // 0x11396: the page towards C by v / 32
    float v = mix(texture(A, uv).r, texture(B, uv).r, f);
    o = vec4(C, clamp(v / 32.0, 0.0, 1.0));
}
)";

void CoreGL::buildGlow() {
    lines.build(kLineVS, kLineFS, "jizz lines");
    scale.build(kFullscreenVS, kScaleFS, "jizz glow scale");
    gauss.build(kFullscreenVS, kGaussFS, "jizz glow gauss");
    trail.build(kFullscreenVS, kTrailFS, "jizz glow trail");
    glowFade.build(kFullscreenVS, kGlowFadeFS, "jizz glow fade");
    gl::GenVertexArrays(1, &lineVao);
    gl::BindVertexArray(lineVao);
    gl::GenBuffers(1, &lineVbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, lineVbo);
    gl::BufferData(GL_ARRAY_BUFFER, 1024 * 16, nullptr, GL_STREAM_DRAW);
    gl::VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    gl::VertexAttribDivisor(0, 1);
    gl::EnableVertexAttribArray(0);
    gl::BindVertexArray(0);
}

CoreGL::~CoreGL() {
    if (lineVbo) gl::DeleteBuffers(1, &lineVbo);
    if (lineVao) gl::DeleteVertexArrays(1, &lineVao);
}

namespace {
void passScale(CoreGL& g, const Target& src, Target& dst, float k) {
    dst.bind();
    g.scale.use();
    src.bindTex(0);
    g.scale.set("src", 0);
    g.scale.set("k", k);
    drawFullscreen();
}

// 0x11eb2: (s[k] + s[k+1] + s[k+320] + s[k+321]) / 4 = a 2-tap box per axis (variance 1/4 pixel^2, centred half a
// pixel right / down) -> a Gaussian of sigma 0.5 original pixels centred at +0.5 pixel, at the target resolution.
void passGauss(CoreGL& g, Ctx& c, const Target& src, Target& dst, float k) {
    auto tmp = c.tempSized(dst.w, dst.h, false, GL_R16F);
    g.gauss.use();
    g.gauss.set("src", 0);
    auto axis = [&](const Target& s, Target& d, float px, int dx, int dy, float kk) {
        // discrete weights of N(offset, sigma^2) at whole pixels lo..hi
        float sigma = 0.5f * px, offset = 0.5f * px, q = -0.5f / (sigma * sigma);
        int lo = int(std::floor(offset - 3.0f * sigma)), hi = int(std::ceil(offset + 3.0f * sigma));
        if ((hi - lo) % 2 == 0) hi++;  // an even number of taps (the last weight is tiny)
        float x = lo - offset;
        d.bind();
        s.bindTex(0);
        g.gauss.set("dir", float(dx) / s.w, float(dy) / s.h);
        g.gauss.set("lo", lo);
        g.gauss.set("n", hi - lo + 1);
        g.gauss.set("w0", std::exp(q * x * x), std::exp(q * (2 * x + 1)), std::exp(2 * q));
        g.gauss.set("k", kk);
        drawFullscreen();
    };
    axis(src, *tmp, dst.w / 320.0f, 1, 0, k);    // right
    axis(*tmp, dst, dst.h / 200.0f, 0, -1, 1.0f);  // down (GL rows count upwards)
}

void passTrail(CoreGL& g, const Target& T, const Target& B, Target& dst, const GlowStep& p, float kB) {
    dst.bind();
    g.trail.use();
    T.bindTex(0);
    B.bindTex(1);
    g.trail.set("T", 0);
    g.trail.set("B", 1);
    g.trail.set("a", p.shift <= 0 ? 1.0f : std::ldexp(1.0f, -p.shift));
    g.trail.set("bias", p.shift <= 0 ? 0.0f : p.trailBias);
    g.trail.set("kB", kB);
    drawFullscreen();
}
}  // namespace

// =============================================================================================== glow
Glow::~Glow() {
    for (Slot& s : slot_) {
        s.lines.destroy();
        s.trail.destroy();
    }
}

void Glow::fill(Target& t, float v) {
    t.bind();
    gl::ClearColor(v, 0, 0, 0);
    gl::Clear(GL_COLOR_BUFFER_BIT);
}

void Glow::ensure(Ctx& c) {
    int w = std::max(1, int(std::lround(c.w * scale_))), h = std::max(1, int(std::lround(c.h * scale_)));
    if (w == w_ && h == h_ && slot_[0].lines.fbo) return;
    for (Slot& s : slot_) {
        s.lines.create(w, h, false, GL_R16F);
        s.trail.create(w, h, false, GL_R16F);
    }
    w_ = w;
    h_ = h;
    have_ = kNone;
}

// How many steps before `want` matter: the weight of the state before the window in the state after `want`,
// per buffer (B, T), is a product of 2x2 matrices of the steps' decays; stop below 1/1000.
int Glow::history(int want, int first, const Source& src) const {
    double q00 = 1, q01 = 0, q10 = 0, q11 = 1;
    for (int k = want, m = 1; k >= first; k--, m++) {
        GlowStep p = src.params ? src.params(k) : GlowStep{};
        double a = p.shift <= 0 ? 1.0 : std::ldexp(1.0, -p.shift);
        double m00, m01, m10, m11;  // (B, T) after the step from (B, T) before it
        if (order_ == Overlay) { m00 = 1.0 / 8; m01 = 0; m10 = a / 8; m11 = 1 - a; }
        else { m00 = 0; m01 = 1 - a; m10 = 0; m11 = 1 - a; }
        double n00 = q00 * m00 + q01 * m10, n01 = q00 * m01 + q01 * m11;
        double n10 = q10 * m00 + q11 * m10, n11 = q10 * m01 + q11 * m11;
        q00 = n00; q01 = n01; q10 = n10; q11 = n11;
        if (std::max(q00 + q01, q10 + q11) < 1e-3 || m >= 400) return m;
    }
    return want - first + 1;
}

void Glow::resetSlot(Ctx& c, Slot& s, bool phaseStart, const Source& src) {
    fill(s.lines, 0.0f);
    fill(s.trail, 0.0f);
    if (phaseStart && src.reset) {
        Target* out = c.out;
        src.reset(c, s.lines, s.trail);
        c.out = out;
    }
}

void Glow::doStep(Ctx& c, int k, const Source& src) {
    CoreGL& g = c.shared<CoreGL>();
    GlowStep p = src.params ? src.params(k) : GlowStep{};
    Slot& S = slot_[cur_];
    Slot& D = slot_[1 - cur_];
    auto X = c.tempSized(w_, h_, false, GL_R16F);
    if (order_ == Overlay) passScale(g, S.lines, *X, 1.0f);  // the lines go on top of last frame's buffer
    else fill(*X, 0.0f);                                      // TBL draws a whole picture
    Target* out = c.out;
    c.out = &*X;
    X->bind();
    setBlend(Blend::None);
    if (src.draw) src.draw(c, k);
    c.out = out;
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_SCISSOR_TEST);
    if (order_ == Overlay) {
        if (p.blur) passGauss(g, c, *X, D.lines, 1.0f / 8);  // >> 3, then 0x11eb2 in place
        else passScale(g, *X, D.lines, 1.0f / 8);
        if (p.shift <= 0) passScale(g, D.lines, D.trail, 1.0f);
        else passTrail(g, S.trail, D.lines, D.trail, p, 1.0f);
    } else {
        passTrail(g, S.trail, *X, D.trail, p, 1.0f / 8);    // >> 3 and 0x12111
        if (p.blur) passGauss(g, c, D.trail, D.lines, 1.0f);  // 0x11eb2 from the trail into the buffer
        else passScale(g, D.trail, D.lines, 1.0f);
    }
    cur_ = 1 - cur_;
    have_ = k;
}

void Glow::advance(Ctx& c, double steps, int first, const Source& src) {
    Target* out = c.out;
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_DEPTH_TEST);
    ensure(c);
    double fl = std::floor(steps);
    int n = int(fl);
    double frac = steps - fl;
    if (n < first - 1) { n = first - 1; frac = 0; }
    int want = n + 1;  // shown: S(n) and S(n + 1)
    bool rebuild = have_ == kNone || first != first_ || want < have_;
    int hist = 0;
    if (!rebuild && want > have_) {
        hist = history(want, first, src);
        rebuild = want - have_ > hist;
    }
    if (rebuild) {
        if (!hist) hist = history(want, first, src);
        int k0 = std::max(first, want - hist + 1);
        resetSlot(c, slot_[cur_], k0 == first, src);
        first_ = first;
        have_ = k0 - 1;
    }
    while (have_ < want) doStep(c, have_ + 1, src);
    frac_ = frac;
    c.out = out;
    if (out) out->bind();
    setBlend(Blend::None);
}

void Glow::bind(int unit) const {
    result(slot_[1 - cur_]).bindTex(unit);
    result(slot_[cur_]).bindTex(unit + 1);
}

void Glow::fadeTowards(Ctx& c, Vec3 C) const {
    if (have_ == kNone || !c.out) return;
    CoreGL& g = c.shared<CoreGL>();
    c.out->bind();
    gl::Disable(GL_DEPTH_TEST);
    setBlend(Blend::Alpha);
    g.glowFade.use();
    bind(0);
    g.glowFade.set("A", 0);
    g.glowFade.set("B", 1);
    g.glowFade.set("f", fraction());
    g.glowFade.set("C", C);
    drawFullscreen();
    setBlend(Blend::None);
}

}  // namespace jizz
