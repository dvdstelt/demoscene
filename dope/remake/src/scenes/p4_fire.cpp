// p4's FIRE clouds (dope/re/notes-p4.md section 4): the feedback buffer 1aab, stepped once per odd retrace, that
// keeps an exponential moving average of the cloud texture 1ec1 (brightened by 1b36) seen through the polar swirl
// map 07d9, which scrolls the texture outward one row per step and turns it with the sine phase; its 15 fire levels
// (the composite 1b91 / 1c6b with the 4 x 4 dither) and the fire half of the palette mix 1999.
//
// Evaluated exactly instead of the original's shortcuts:
//   - the 160 x 200 buffer of 2-pixel-wide values is a buffer of the output's size: the recursion has no spatial
//     coupling, so every output pixel runs it at its own position;
//   - the swirl map (32000 stored words, generated before release) is its polar formula (notes-p4 4.2): v = a + b r,
//     u = 256 / (2 pi) atan2(Y, -X) with X, Y the distance from the screen centre in fire pixels and Y weighted by k.
//     a, b and k are fitted at start-up to the map in the image (the stored map is within +-1 of the formula except
//     next to the axes, where the generator wrote glitches that the remake drops);
//   - the texture read at 256 (v - m) + u + s (nearest texel, the column carrying into the next row) is a filtered,
//     mipmapped read at column u + S and row v - m, wrapping in both directions; S = SINE / 256 is continuous (the
//     table becomes sin), so floor(S) and its 1/2-column bias are gone;
//   - 1b36's floor(7 c / 4) clipped at 127 is min(1.75 c, 127) per texel, filtered after the clip;
//   - BLEND[F][C] = min((24 C + 232 F + 63) >> 8, 112) is the float recursion F' = min((24 C + 232 F + 63) / 256 -
//     0.5, 112), >= 0 (notes-p4 4.1: the closest float form of the integer one, whose dead zone it drops);
//   - the dither of 2F / 16 into 15 levels (thresholds 0, 1, 4, 7 of 16, edx = 01040007h) is its mean, the level
//     L = F / 8 + (3 - 7.5) / 16, with the 15 fire colours (0010:15AE) interpolated between levels;
//   - the 1/256 palette weights are the continuous fire weight a: the fire adds a * fire(L) (see p4.cpp).
// The state between steps (the original holds F for two retraces) is blended between two consecutive steps.
#include <algorithm>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>

#include "exe.h"
#include "scenes/p4.h"

namespace {

const char* kStepFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D prev;     // F of the previous step, same size as this target
uniform sampler2D cloud;    // min(1.75 c, 127), 256 x 256, repeat, mipmapped
uniform int first;          // 1: the previous state is the cleared buffer
uniform float row;          // texture rows scrolled so far (m mod 256)
uniform float col;          // the sine turn S = SINE / 256 in texture columns
uniform vec3 map;           // the swirl map: v = map.x + map.y * r, Y weight map.z
void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);            // VGA position of the pixel centre
    vec2 q = vec2((160.0 - p.x) * 0.5, map.z * (p.y - 100.0));     // (-X, Y) in fire pixels (2 screen pixels wide)
    float v = min(map.x + map.y * length(q), 255.0);               // the map's radius (row), at most 255
    float u = 128.0 / PI * atan(q.y, q.x);                         // the map's angle (column): left 0, bottom 64
    vec2 tc = vec2(u + col, v - row) / 256.0;
    // the footprint of this pixel in the texture, without the jump of the angle at the right (u = +-128)
    vec2 qx = dFdx(q), qy = dFdy(q);
    float r2 = max(dot(q, q), 1e-4);
    vec2 gx = vec2((q.x * qx.y - q.y * qx.x) / r2 * 128.0 / PI, dFdx(v)) / 256.0;
    vec2 gy = vec2((q.x * qy.y - q.y * qy.x) / r2 * 128.0 / PI, dFdy(v)) / 256.0;
    float c = textureGrad(cloud, tc, gx, gy).r;
    float f = first != 0 ? 0.0 : texelFetch(prev, ivec2(gl_FragCoord.xy), 0).r;
    o = vec4(clamp((24.0 * c + 232.0 * f + 63.0) / 256.0 - 0.5, 0.0, 112.0), 0.0, 0.0, 1.0);
}
)";

const char* kDrawFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D f0;       // F after step n
uniform sampler2D f1;       // F after step n + 1
uniform sampler2D pal;      // the 15 fire colours
uniform float frac;
uniform float a;            // the fire weight A / 4096
void main() {
    ivec2 ij = ivec2(gl_FragCoord.xy);
    float f = mix(texelFetch(f0, ij, 0).r, texelFetch(f1, ij, 0).r, frac);
    float level = clamp(f / 8.0 + (3.0 - 7.5) / 16.0, 0.0, 14.0);  // the mean of the dithered level
    o = vec4(dpPalLerp(pal, level) * a, 1.0);
}
)";

// The polar formula of the swirl map 07d9 (notes-p4 4.2), fitted to the stored map: fire pixel p = y * 160 + x
// reads word 31999 - p = v * 256 + u. For each Y weight k on a grid, v + 1/2 (v is a floored radius) is fitted
// linearly to r = |(x - 79.5, k (y - 99.5))|; the best k wins. Returns (a, b, k).
Vec3 fitSwirlMap(const DopeImage& p4) {
    constexpr int kW = 160, kH = 200;
    std::vector<float> V(kW * kH);
    for (int p = 0; p < kW * kH; p++) V[size_t(p)] = float(p4.u16(0x07d9, uint16_t(2 * (kW * kH - 1 - p))) >> 8);
    Vec3 best(0, 0, 0);
    double bestErr = 1e30;
    std::vector<double> r(V.size());
    for (int ki = 0; ki <= 80; ki++) {
        double k = 0.6 + ki * 0.0025;
        double sr = 0, sv = 0, srr = 0, srv = 0, n = 0;
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++) {
                size_t i = size_t(y * kW + x);
                double X = x - 79.5, Y = k * (y - 99.5);
                r[i] = std::sqrt(X * X + Y * Y);
                double v = V[i] + 0.5;
                sr += r[i]; sv += v; srr += r[i] * r[i]; srv += r[i] * v; n++;
            }
        double b = (n * srv - sr * sv) / (n * srr - sr * sr), a = (sv - b * sr) / n, err = 0;
        for (size_t i = 0; i < V.size(); i++) {
            double e = a + b * r[i] - (V[i] + 0.5);
            err += e * e;
        }
        if (err < bestErr) { bestErr = err; best = Vec3(float(a), float(b), float(k)); }
    }
    return best;
}

}  // namespace

void P4Fire::init(Ctx& c) {
    const DopeImage& p4 = c.exe->part(4);
    // 1b36: the cloud texture brightened 1.75 times and clipped at 127
    Indexed cl = readIndexed(*c.exe, *findPicture("p4-cloud"));
    std::vector<float> px(cl.px.size());
    for (size_t i = 0; i < px.size(); i++) px[i] = std::min(1.75f * float(cl.px[i]), 127.0f);
    cloud_.createRaw(cl.w, cl.h, GL_R16F, GL_RED, GL_FLOAT, px.data(), true, true, true);
    // the 15 fire colours 0010:15AE (DAC 16 L + c of 1999)
    firePal_.set(vgaPalette(p4.csSpan(0x15ae, 15 * 3)));
    stepShader_.build(kFullscreenVS, kStepFS, "p4 fire step");
    drawShader_.build(kFullscreenVS, std::string(dope::kGLSL) + kDrawFS, "p4 fire");
    map_ = fitSwirlMap(p4);
    SDL_Log("p4: swirl map v = %.3f + %.4f r, Y weight %.4f", map_.x, map_.y, map_.z);
}

void P4Fire::step(int k, const Step& s) {
    Target& dst = buf(k);
    dst.bind();
    stepShader_.use();
    buf(k - 1).bindTex(0);
    cloud_.bind(1);
    stepShader_.set("prev", 0);
    stepShader_.set("cloud", 1);
    stepShader_.set("first", k - 1 == base_ ? 1 : 0);
    stepShader_.set("row", float(s.m & 255));
    // 1aab: SINE[(phase & 0FFEh) / 2] / 256 columns (the sine table as sin)
    stepShader_.set("col", float(dope::sine2048((s.phase & 0xffe) >> 1) / 256.0));
    stepShader_.set("map", map_);
    drawFullscreen();
}

void P4Fire::advance(Ctx& c, int n, const std::vector<Step>& steps) {
    int total = int(steps.size());
    n = std::clamp(n, 1, total);
    int target = std::min(n + 1, total);
    int w = c.out->w, h = c.out->h;
    if (w != w_ || h != h_) {
        for (Target& t : buf_) t.create(w, h, false, GL_R32F);
        w_ = w;
        h_ = h;
        have_ = -1;
    }
    bool held = have_ >= 0 && n >= have_ - valid_ + 1 && target <= have_;
    if (held) return;
    setBlend(Blend::None);
    if (have_ >= 0 && target > have_ && target - have_ <= kRebuild) {
        // playing forwards: the next steps from the last state
        for (int k = have_ + 1; k <= target; k++) step(k, steps[size_t(k - 1)]);
        valid_ = std::min(3, valid_ + (target - have_));
    } else {
        // any other jump: from a cleared buffer kRebuild steps back (exact from the part start)
        base_ = std::max(0, target - kRebuild);
        for (int k = base_ + 1; k <= target; k++) step(k, steps[size_t(k - 1)]);
        valid_ = std::min(3, target - base_);
    }
    have_ = target;
    c.out->bind();
}

void P4Fire::draw(int n, double frac, double a) {
    int total = std::max(1, have_);
    n = std::clamp(n, 1, total);
    setBlend(Blend::Add);
    drawShader_.use();
    buf(n).bindTex(0);
    buf(std::min(n + 1, have_)).bindTex(1);
    firePal_.bind(2);
    drawShader_.set("f0", 0);
    drawShader_.set("f1", 1);
    drawShader_.set("pal", 2);
    drawShader_.set("frac", n + 1 <= have_ ? float(frac) : 0.0f);
    drawShader_.set("a", float(a));
    drawFullscreen();
}
