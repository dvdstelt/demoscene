// The pencil line 0x18b7 / 0x1612 and the shade-table darkening 0x144b / 0x1943 at any resolution (see pencil.h).
#include "pencil.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "common.h"
#include "palette.h"

namespace paper {

// ---------------------------------------------------------------- palette ramps
ShadePalette ShadePalette::fromExe(const PaperExe& exe) { return ShadePalette(DefaultPalette(exe).rgb); }

void ShadePalette::setEntries6(int first, const uint8_t* rgb6, int n) {
    for (int i = 0; i < n && first + i < 256; i++)
        rgb[size_t(first + i)] = Vec3((rgb6[3 * i] & 63) / 63.0f, (rgb6[3 * i + 1] & 63) / 63.0f, (rgb6[3 * i + 2] & 63) / 63.0f);
}

Vec3 ShadePalette::at(double index, int rampIndex) const {
    const int r = rampIndex >= 0 ? rampIndex : int(std::clamp(index, 0.0, 255.0));
    const int first = r < 64 ? 0 : (r & ~31), last = r < 64 ? 63 : first + 31;
    index = std::clamp(index, double(first), double(last));
    const int i0 = int(std::floor(index)), i1 = std::min(i0 + 1, last);
    const float f = float(index - i0);
    return rgb[size_t(i0)] * (1 - f) + rgb[size_t(i1)] * f;
}

// ---------------------------------------------------------------- init tables (0x5b7d)
PencilTables::PencilTables(const PaperExe& exe) {
    Rng r = Rng::at(exe, RngPos::kGreyTable);
    for (int i = 0; i < 1024; i++) {
        const uint32_t v = r.next() & 31;
        dark[i] = uint8_t(v < 5 ? 30 + (v >> 2) : 26 + (v >> 3));
    }
    for (int i = 0; i < 1024; i++) {
        int32_t s = 0;
        for (int k = 0; k < 120; k++) s += int32_t(r.next() & 15) - 7;
        jitter[i] = int8_t((s >> 6) - 1);
    }
}

// ---------------------------------------------------------------- 0x18b7 -> passes
namespace {
// 0x1612 x-major, going up (after ordering left to right): the row is not floor(y) but y_int - floor(f + n|s|),
// f = fraction of y at the first drawn column, i.e. ceil(y - 2f): rounded up (the 2-pixel run one pixel lower than a
// downward line's) when the line starts on screen (f = 0), shifted back up by 2f when its start was clipped. Returns
// that offset of the run's centre (mean over the line) relative to a downward line: 1 - 2f.
float upwardBias(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    const int dx = x1 - x0;
    if (dx <= 0 || x0 >= 320) return 1.0f;
    const int32_t s = recipTab(std::min(dx, 2048)) * (y1 - y0);  // 16.16, negative
    int32_t y = int32_t(uint32_t(y0) << 16);
    int x = x0;
    if (x < 0) { y = int32_t(uint32_t(y) - uint32_t(uint64_t(int64_t(s) * x))); x = 0; }
    for (int n = std::min(x1, 320) - x; n > 0 && y >= (200 << 16); n--) y += s;  // rows below the screen skipped
    return 1.0f - 2.0f * float(y & 0xffff) / 65536.0f;
}
}  // namespace

void addPencilPasses(const PencilTables& t, const PencilLine& line, std::vector<PencilPass>& out, const float* plane,
                     const float* zinv) {
    if (line.t1 <= line.t0 || line.strength <= 0) return;
    // the original's integer endpoints: hashes of the jitter and of the darkness offset
    const int32_t X0 = int32_t(std::floor(line.a.x)), Y0 = int32_t(std::floor(line.a.y));
    const int32_t X1 = int32_t(std::floor(line.b.x)), Y1 = int32_t(std::floor(line.b.y));
    const uint32_t h0 = (uint32_t(X0) * uint32_t(Y0) + uint32_t(Y0)) & 0x1ff;
    const uint32_t h1 = (uint32_t(X1) * uint32_t(Y1) + uint32_t(Y1)) & 0x1ff;
    const float off = float((uint32_t(X0) + uint32_t(Y0) + uint32_t(X1) + uint32_t(Y1)) & 0xff);
    for (int k = 0; k < 3; k++) {
        const int jy0 = t.jitter[h0 + 2 * k], jx0 = t.jitter[h0 + 2 * k + 1];
        const int jy1 = t.jitter[h1 + 2 * k], jx1 = t.jitter[h1 + 2 * k + 1];
        const int idx = (X1 + jx1) - (X0 + jx0), idy = (Y1 + jy1) - (Y0 + jy0);
        if (idx == 0 && idy == 0) continue;  // 0x1612 draws nothing
        const bool xm = std::abs(idx) > std::abs(idy);
        // pass endpoints (float), in (major, minor) coordinates
        float a[2] = {line.a.x + jx0, line.a.y + jy0}, b[2] = {line.b.x + jx1, line.b.y + jy1};
        const int M = xm ? 0 : 1, m = 1 - M;
        // partial range on the major axis (fractions measured from a)
        const float ta = a[M] + line.t0 * (b[M] - a[M]), tb = a[M] + line.t1 * (b[M] - a[M]);
        const bool swap = xm ? idx < 0 : idy < 0;  // 0x1612 draws from the top (y-major) / left (x-major) end
        float za = zinv ? zinv[0] : 0, zb = zinv ? zinv[1] : 0;
        if (swap) { std::swap(a, b); std::swap(za, zb); }
        const float len = b[M] - a[M];
        const float slope = len > 1e-6f ? (b[m] - a[m]) / len : 0.0f;
        const float bias = (xm && idy < 0) ? upwardBias(X0 + jx0, Y0 + jy0, X1 + jx1, Y1 + jy1) : 0.0f;
        // the darkness sequence starts at the first step drawn on screen (clipped rows/columns use no bytes)
        const float minLim = xm ? 200.0f : 320.0f;
        float origin = std::max(a[M], 0.0f);
        const float mo = a[m] + slope * (origin - a[M]) + bias;
        if (mo < 0 && slope > 0) origin = a[M] + (0 - a[m] - bias) / slope;
        else if (mo >= minLim && slope < 0) origin = a[M] + (minLim - a[m] - bias) / slope;
        // the band covers the steps' pixel rows/columns: from half a step before the start to half a step before
        // the end (the end pixel is not drawn)
        const float lo = std::max(a[M], std::min(ta, tb)) - 0.5f, hi = std::min(b[M], std::max(ta, tb)) - 0.5f;
        if (hi <= lo) continue;
        PencilPass p;
        p.seg[0] = a[0]; p.seg[1] = a[1]; p.seg[2] = b[0]; p.seg[3] = b[1];
        p.prm[0] = xm ? 1.0f : 0.0f; p.prm[1] = bias; p.prm[2] = lo; p.prm[3] = hi;
        p.tex[0] = off; p.tex[1] = origin - 0.5f; p.tex[2] = line.strength;
        p.tex[3] = float((h0 * 7 + h1 * 13 + uint32_t(k) * 31 + uint32_t(off)) & 255);
        for (int q = 0; q < 4; q++) p.plane[q] = plane ? plane[q] : 0.0f;
        p.zinv[0] = za; p.zinv[1] = zb;
        out.push_back(p);
    }
}

// ---------------------------------------------------------------- GLSL
const char* kRampDarkenGLSL = R"(
uniform float uGrey[64];   // grey ramp (palette 0..63), one value per entry
uniform vec3 uRampK[5];    // colour ramps at 96, 128, 160, 192, 224: the full colour (knee) entry
uniform vec3 uRampW[5];    // and the white end (entry 31); entry 0 is black
uniform float uRampKi[5];  // knee position in the ramp (15 or 16)
float greyAt(float e) {
    e = clamp(e, 0.0, 63.0);
    int i = int(floor(e)); int j = min(i + 1, 63);
    return mix(uGrey[i], uGrey[j], e - float(i));
}
float greyInv(float g) {           // largest entry position with that grey (the table is monotone)
    int lo = 0, hi = 63;
    for (int it = 0; it < 7; it++) { int mid = (lo + hi + 1) / 2; if (uGrey[mid] <= g) lo = mid; else hi = mid - 1; }
    if (lo >= 63) return 63.0;
    float g0 = uGrey[lo], g1 = uGrey[lo + 1];
    return float(lo) + (g1 > g0 ? clamp((g - g0) / (g1 - g0), 0.0, 1.0) : 0.0);
}
vec3 rampAt(int r, float p) {
    float ki = uRampKi[r];
    return p <= ki ? uRampK[r] * (p / ki) : mix(uRampK[r], uRampW[r], (p - ki) / (31.0 - ki));
}
// SHADE[32 - steps][c] for a display colour: the colour moved `steps` entries down its palette ramp (2 entries
// per step on the grey ramp), clamped at black. The ramp is the one nearest to the colour; the ramp's change is
// added to the colour (so colours slightly off the ramp keep their offset).
vec3 rampDarken(vec3 c, float steps) {
    float g = (c.r + c.g + c.b) / 3.0;
    vec3 dg = c - vec3(g);
    float best = dot(dg, dg) - 1e-6;
    float e = greyInv(g);
    vec3 res = c + vec3(greyAt(e - 2.0 * steps) - greyAt(e));
    for (int r = 0; r < 5; r++) {
        vec3 K = uRampK[r], KW = uRampW[r] - uRampK[r];
        float ki = uRampKi[r];
        float t = clamp(dot(c, K) / max(dot(K, K), 1e-8), 0.0, 1.0);
        vec3 q = c - K * t;
        float d2 = dot(q, q);
        if (d2 < best) { best = d2; float p = t * ki; res = c + rampAt(r, max(p - steps, 0.0)) - rampAt(r, p); }
        t = clamp(dot(c - K, KW) / max(dot(KW, KW), 1e-8), 0.0, 1.0);
        q = c - (K + KW * t);
        d2 = dot(q, q);
        if (d2 < best) { best = d2; float p = ki + t * (31.0 - ki); res = c + rampAt(r, max(p - steps, 0.0)) - rampAt(r, p); }
    }
    return clamp(res, 0.0, 1.0);
}
)";

namespace {
// Instanced quad per pass: the band around the line, 2 VGA px across the minor axis, plus a margin of 1.5 target
// pixels for the antialiasing.
const char* kStrokeVS = R"(
layout(location = 0) in vec4 aSeg;
layout(location = 1) in vec4 aPrm;
layout(location = 2) in vec4 aTex;
layout(location = 3) in vec4 aPlane;
layout(location = 4) in vec2 aZinv;
uniform vec2 uPx;   // one target pixel in VGA units
out vec2 vVga;
flat out vec4 fSeg; flat out vec4 fPrm; flat out vec4 fTex; flat out vec4 fPlane; flat out vec2 fZinv;
void main() {
    bool xm = aPrm.x > 0.5;
    vec2 A = xm ? aSeg.xy : aSeg.yx, B = xm ? aSeg.zw : aSeg.wz;
    vec2 px = max((xm ? uPx : uPx.yx) * 1.5, vec2(0.3));   // margin: antialiasing and the soft edges
    float s = (B.y - A.y) / max(B.x - A.x, 1e-4);
    float M = (gl_VertexID & 1) == 0 ? aPrm.z - px.x : aPrm.w + px.x;
    float m = A.y + s * (M - A.x) + aPrm.y + ((gl_VertexID >> 1) == 0 ? -1.0 - px.y - abs(s) * px.x : 1.0 + px.y + abs(s) * px.x);
    vVga = xm ? vec2(M, m) : vec2(m, M);
    fSeg = aSeg; fPrm = aPrm; fTex = aTex; fPlane = aPlane; fZinv = aZinv;
    gl_Position = vec4(vVga.x / 160.0 - 1.0, 1.0 - vVga.y / 100.0, 0.0, 1.0);
}
)";

const char* kStrokeFS = R"(
in vec2 vVga;
flat in vec4 fSeg; flat in vec4 fPrm; flat in vec4 fTex; flat in vec4 fPlane; flat in vec2 fZinv;
uniform sampler2D uDark;   // 1024 x 1: darkening of the original's byte i in ramp steps (32 - byte)
uniform float uGrain;      // amplitude of the fine grain, ramp steps
#ifdef MRT
layout(location = 0) out vec4 oColour;
layout(location = 1) out vec4 oDark;
uniform vec2 uDepthAB;     // ndc z = A + B / z
#else
out vec4 oDark;
#endif
float dk(float i) { return texelFetch(uDark, ivec2(int(clamp(i, 0.0, 1023.0)), 0), 0).r; }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    bool xm = fPrm.x > 0.5;
    vec2 A = xm ? fSeg.xy : fSeg.yx, B = xm ? fSeg.zw : fSeg.wz, P = xm ? vVga : vVga.yx;
    float s = (B.y - A.y) / max(B.x - A.x, 1e-4);
    float across = P.y - (A.y + s * (P.x - A.x) + fPrm.y);
    float fa = max(fwidth(across), 1e-4), fm = max(fwidth(P.x), 1e-4);
#ifdef MRT
    // depth: the drawing face's plane at this pixel (silhouette lines extend it past the edge), never behind the
    // edge itself, minus the depth slope of one pixel (MSAA samples) and a small relative bias
    float ze = 1.0 / max(mix(fZinv.x, fZinv.y, clamp((P.x - A.x) / max(B.x - A.x, 1e-4), 0.0, 1.0)), 1e-9);
    vec3 ray = vec3((vVga.x - 160.0) / 224.0, (vVga.y - 100.0) / 224.0, 1.0);
    float nr = dot(fPlane.xyz, ray);
    float zp = abs(nr) > 1e-12 ? fPlane.w / nr : ze;
    zp = zp > 0.0 ? clamp(zp, 0.5 * ze, ze) : ze;
    float zs = fwidth(zp);
    float z = max(zp * 0.9999 - zs, 1.0);
#endif
    // coverage of the band (box filter of one target pixel, at least a soft pencil edge of 0.35 / 0.5 VGA px,
    // centred on the band's edges so the total darkness stays the original's)
    float ea = max(fa, 0.35), em = max(fm, 0.5);
    float cov = clamp(0.5 + (1.0 - abs(across)) / ea, 0.0, 1.0) *
                clamp(0.5 + (P.x - fPrm.z) / em, 0.0, 1.0) * clamp(0.5 + (fPrm.w - P.x) / em, 0.0, 1.0);
    if (cov <= 0.0) discard;
    // the original's darkness bytes: step k (1 VGA px) from the sequence origin, d[2k] on the first half of the
    // band (left / top pixel), d[2k+1] on the second; cells with soft edges
    float k = P.x - fTex.y;
    float last = max(ceil(B.x - 0.5 - fTex.y) - 1.0, 0.0);
    float kk = clamp(k - 0.5, 0.0, last);
    float i0 = floor(kk), i1 = min(i0 + 1.0, last), f = smoothstep(0.2, 0.8, kk - i0);
    float o = fTex.x;
    float d0 = mix(dk(o + 2.0 * i0), dk(o + 2.0 * i1), f);
    float d1 = mix(dk(o + 2.0 * i0 + 1.0), dk(o + 2.0 * i1 + 1.0), f);
    float d = mix(d0, d1, smoothstep(-0.3, 0.3, across));
    // finer grain at a third of a VGA pixel, fixed to the stroke
    d += uGrain * 2.0 * (vnoise(vec2(k, across) * 3.0 + vec2(fTex.w * 1.37, fTex.w * 0.71)) - 0.5);
    float D = cov * fTex.z * max(d, 0.0);
#ifdef MRT
    gl_FragDepth = 0.5 + 0.5 * (uDepthAB.x + uDepthAB.y / z);
    oColour = vec4(0.0);
#endif
    oDark = vec4(D, 0.0, 0.0, 0.0);
}
)";

const char* kResolveFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uSrc;
uniform sampler2D uInk;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(uSrc, p, 0);
    float d = texelFetch(uInk, p, 0).r;
    o = vec4(d > 0.0 ? rampDarken(c.rgb, d) : c.rgb, c.a);
}
)";

const char* kAddFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uTex;
uniform vec3 uMulAdd;   // mul, add, bicubic
void main() {
    vec2 t = vec2(uv.x, 1.0 - uv.y);   // first image row at the top of the frame
    float v = 0.0;
    if (uMulAdd.x != 0.0) v = uMulAdd.z > 0.5 ? texBicubic(uTex, t).r : texture(uTex, t).r;
    o = vec4(v * uMulAdd.x + uMulAdd.y, 0.0, 0.0, 0.0);
}
)";

GLuint g_dummyTex = 0;
}  // namespace

// ---------------------------------------------------------------- PencilGpu
PencilGpu::PencilGpu(Ctx& c) : tables(*c.exe), defaultPalette(ShadePalette::fromExe(*c.exe)) {
    float d[1024];
    for (int i = 0; i < 1024; i++) d[i] = float(32 - tables.dark[i]);
    darkTex.createRaw(1024, 1, GL_R32F, GL_RED, GL_FLOAT, d, false, false, false);
    strokeInk.build(kStrokeVS, kStrokeFS, "pencil-stroke");
    strokeMrt.build(kStrokeVS, std::string("#define MRT 1\n") + kStrokeFS, "pencil-stroke-3d");
    resolveShader.build(kFullscreenVS, std::string(kRampDarkenGLSL) + kResolveFS, "pencil-resolve");
    addShader.build(kFullscreenVS, kAddFS, "pencil-add");
    gl::GenVertexArrays(1, &vao);
    gl::GenBuffers(1, &vbo);
    gl::BindVertexArray(vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei stride = sizeof(PencilPass);
    const int sizes[5] = {4, 4, 4, 4, 2};
    const size_t offs[5] = {offsetof(PencilPass, seg), offsetof(PencilPass, prm), offsetof(PencilPass, tex),
                            offsetof(PencilPass, plane), offsetof(PencilPass, zinv)};
    for (GLuint i = 0; i < 5; i++) {
        gl::VertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride, (void*)offs[i]);
        gl::VertexAttribDivisor(i, 1);
        gl::EnableVertexAttribArray(i);
    }
    gl::BindVertexArray(0);
    if (!g_dummyTex) {
        const uint8_t z[4] = {0, 0, 0, 0};
        Texture t;
        t.createRaw(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, z);
        g_dummyTex = t.id;
    }
}

void PencilGpu::drawPasses(const std::vector<PencilPass>& passes, const Shader& sh, int w, int h) {
    if (passes.empty()) return;
    sh.use();
    sh.set("uPx", 320.0f / float(w), 200.0f / float(h));
    sh.set("uGrain", 1.2f);
    sh.set("uDark", 0);
    darkTex.bind(0);
    gl::BindVertexArray(vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(passes.size() * sizeof(PencilPass)), passes.data(), GL_STREAM_DRAW);
    gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(passes.size()));
    gl::BindVertexArray(0);
}

void PencilGpu::setRampUniforms(const Shader& sh, const ShadePalette& pal) {
    float grey[64];
    for (int i = 0; i < 64; i++) grey[i] = (pal.rgb[size_t(i)].x + pal.rgb[size_t(i)].y + pal.rgb[size_t(i)].z) / 3.0f;
    for (int i = 1; i < 64; i++) grey[i] = std::max(grey[i], grey[i - 1]);  // keep it monotone for the inverse
    float K[15], W[15], ki[5];
    for (int r = 0; r < 5; r++) {
        const int base = 96 + 32 * r;
        int best = 15;
        float bs = -1;
        for (int e = 8; e < 24; e++) {
            const Vec3& c = pal.rgb[size_t(base + e)];
            const float sat = std::max({c.x, c.y, c.z}) - std::min({c.x, c.y, c.z});
            if (sat > bs + 1e-6f) { bs = sat; best = e; }
        }
        const Vec3& k = pal.rgb[size_t(base + best)];
        const Vec3& w = pal.rgb[size_t(base + 31)];
        K[3 * r] = k.x; K[3 * r + 1] = k.y; K[3 * r + 2] = k.z;
        W[3 * r] = w.x; W[3 * r + 1] = w.y; W[3 * r + 2] = w.z;
        ki[r] = float(best);
    }
    sh.use();
    gl::Uniform1fv(sh.loc("uGrey"), 64, grey);
    gl::Uniform3fv(sh.loc("uRampK"), 5, K);
    gl::Uniform3fv(sh.loc("uRampW"), 5, W);
    gl::Uniform1fv(sh.loc("uRampKi"), 5, ki);
}

// ---------------------------------------------------------------- PencilInk
PencilInk::PencilInk(Ctx& c, const Target& frame)
    : c_(c), gpu_(c.shared<PencilGpu>()), t_(c.tempSized(frame.w, frame.h, false, GL_R16F)) {
    t_->bind();
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    if (c_.out) c_.out->bind();
}

void PencilInk::flush() {
    if (pending_.empty()) return;
    t_->bind();
    gl::Disable(GL_DEPTH_TEST);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE);
    gl::Enable(GL_BLEND);
    gpu_.drawPasses(pending_, gpu_.strokeInk, t_->w, t_->h);
    pending_.clear();
    setBlend(Blend::None);
    if (c_.out) c_.out->bind();
}

void PencilInk::addTexture(GLuint tex, float mul, float add, bool bicubic) {
    flush();
    t_->bind();
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE);
    gl::Enable(GL_BLEND);
    gpu_.addShader.use();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, tex ? tex : g_dummyTex);
    gpu_.addShader.set("uTex", 0);
    gpu_.addShader.set("uMulAdd", tex ? mul : 0.0f, add, bicubic ? 1.0f : 0.0f);
    drawFullscreen();
    setBlend(Blend::None);
    if (c_.out) c_.out->bind();
}

void PencilInk::resolve(Ctx& c, Target& frame, const ShadePalette* pal) {
    flush();
    auto src = c.tempSized(frame.w, frame.h, false, GL_RGBA16F);
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, frame.fbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, src->fbo);
    gl::BlitFramebuffer(0, 0, frame.w, frame.h, 0, 0, src->w, src->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    frame.bind();
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    const Shader& sh = gpu_.resolveShader;
    gpu_.setRampUniforms(sh, pal ? *pal : gpu_.defaultPalette);
    sh.set("uSrc", 0);
    sh.set("uInk", 1);
    src->bindTex(0);
    t_->bindTex(1);
    drawFullscreen();
    gl::ActiveTexture(GL_TEXTURE0);
    if (c.out) c.out->bind();
}

}  // namespace paper
