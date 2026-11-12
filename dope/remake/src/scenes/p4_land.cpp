// p4's FRACTAL scape (dope/re/notes-p4.md section 5): the voxel landscape of 1ee8 (the sky: background colour 2)
// and 1efc (55 depth rows far to near, each projected per column from the height map 2eec, filled down to the
// previous row's screen row with the colour map 3eec), coloured by the landscape palette 0010:20D2.
//
// The original's camera (notes-p4 5): a depth row at d texels (z = 32 d) shows the map at u = [2111] / 16 +
// (x - 127.5) d / 64 and row v = P / 8 + d at screen row Y = 72.5 + (1024 - (43690 / 8192) H) / d (base 48h +
// 8000h / z, scale AAAAh / z, the +0.5: a row fills the screen rows up to the farther row's y + 1). A screen pixel
// shows the nearest depth at which the terrain rises above its ray, in the colour of the map there, and the sky
// where the terrain ends (the farthest row, [2117] = 6Eh: DIST 80, d = 40 + the sub-step).
//
// Evaluated exactly instead of the original's shortcuts:
//   - the 55 depth rows (spaced 0.5 to 2 texels) and their sub-step sawtooth (15 - (P & 15)) / 32 are a continuous
//     ray-cast: per output column, 1024 depth samples from the nearest depth any screen row can reach to the far
//     end (fixed at its mean, sub-step 8 / 32), with heights read bilinearly (the original: linear along u, nearest
//     row); per pixel, the first depth where the terrain rises above the ray (a binary search over the running
//     minimum of the projected rows), refined between the two samples around it;
//   - the map row floor(P / 8) + floor(z / 32) (the terrain jumps a texel every 4 retraces) is v = P / 8 + d,
//     continuous;
//   - the nearest-texel colour of each row's span is the colour map converted to the palette's greys and filtered
//     at the hit;
//   - the hard edges (sky / terrain, near ridge / far terrain) are anti-aliased with four rays per pixel, spread
//     over its height, that share the column's samples;
//   - the never-drawn column 319 is drawn; the change of P inside a frame (an IRQ during 1efc) does not exist.
#include <algorithm>
#include <cmath>
#include <string>

#include "exe.h"
#include "scenes/p4.h"

namespace {

constexpr int kDepthSamples = 1024;
// 1efc: base = 48h + 8000h / z and scale = AAAAh / z (z = 32 d), the row y = base - ((HM << 8) * scale >> 16):
constexpr float kHorizon = 0x48 + 0.5f;                       // + 0.5: a row fills the screen rows up to y + 1
constexpr float kBase = 0x8000 / 32.0f;                       // 1024 / d
constexpr float kScale = 0xaaaa * 256.0f / 65536.0f / 32.0f;  // (43690 / 8192) H / d
// 1f69..1fa8: U = ([2111] << 12) - (step << 7) in 8.16 fixed point, step = z << 5 (d / 64 texels), and U += step
// before each column: column 127 (centre x = 127.5) reads u = [2111] / 16
constexpr float kCentreX = 127 + 0.5f;

// The projection of 1efc in texels and VGA rows (see the top of the file).
const char* kCommon = R"(
uniform sampler2D height;   // the height map 0..118, repeat, linear
uniform vec2 cam;           // the camera: u (texels), v = P / 8 (texels, mod 256)
uniform vec4 proj;          // horizon row (72.5), base numerator (1024), height scale (43690 / 8192), x of u = cam.x
uniform vec2 depth;         // the first depth sample, the spacing of the samples
uniform float cols;         // columns (the output's width)
float colX(float c) { return (c + 0.5) * 320.0 / cols; }                    // VGA x of an output column's centre
vec2 mapAt(float x, float d) { return vec2(cam.x + (x - proj.w) * d / 64.0, cam.y + d); }
// heights are samples at the texel corners along u (1efc interpolates HM[u], HM[u + 1]) and rows along v
float rowAt(float x, float d) {
    vec2 m = mapAt(x, d);
    float h = textureLod(height, vec2(m.x + 0.5, m.y) / 256.0, 0.0).r;
    return proj.x + (proj.y - proj.z * h) / d;
}
)";

// Pass 1: the screen row of the terrain at every depth sample of every column (column = x, sample = y).
const char* kRowsFS = R"(
out vec4 o;
void main() {
    vec2 ij = floor(gl_FragCoord.xy);
    o = vec4(rowAt(colX(ij.x), depth.x + ij.y * depth.y), 0.0, 0.0, 1.0);
}
)";

// Pass 2 (Hillis-Steele, 10 times): the running minimum of the rows from the nearest sample outwards.
const char* kScanFS = R"(
out vec4 o;
uniform sampler2D src;
uniform int off;
void main() {
    ivec2 ij = ivec2(gl_FragCoord.xy);
    float m = texelFetch(src, ij, 0).r;
    if (ij.y >= off) m = min(m, texelFetch(src, ij - ivec2(0, off), 0).r);
    o = vec4(m, 0.0, 0.0, 1.0);
}
)";

// Pass 3: the landscape per pixel.
const char* kDrawFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D rows;     // the running minimum of the terrain's screen rows per column and depth sample
uniform sampler2D colour;   // the colour map through the landscape palette, repeat, linear
uniform int samples;        // depth samples per column
uniform vec3 sky;           // landscape colour 2 (1ee8)
uniform float mul;          // the background weight
uniform float pixH;         // one output pixel's height in VGA rows
const int kRays = 4;
void main() {
    int col = int(gl_FragCoord.x);
    float x = colX(float(col));
    float y = (1.0 - uv.y) * 200.0;
    float top = texelFetch(rows, ivec2(col, samples - 1), 0).r;   // the highest row the terrain reaches here
    vec3 acc = vec3(0.0);
    int lo = 0;
    for (int s = kRays - 1; s >= 0; s--) {                          // bottom ray first: its hit is the nearest
        float ys = y + pixH * ((float(s) + 0.5) / float(kRays) - 0.5);
        if (ys <= top) { acc += sky * float(s + 1); break; }        // this ray and the ones above see the sky
        int hi = samples - 1;                                       // the first sample above the ray
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (texelFetch(rows, ivec2(col, mid), 0).r < ys) hi = mid; else lo = mid + 1;
        }
        float d = depth.x + float(lo) * depth.y;
        if (lo > 0) {                                               // between the samples lo - 1 and lo
            float y0 = rowAt(x, d - depth.y), y1 = texelFetch(rows, ivec2(col, lo), 0).r;
            d -= depth.y * (1.0 - clamp((y0 - ys) / max(y0 - y1, 1e-6), 0.0, 1.0));
        }
        acc += textureLod(colour, mapAt(x, d) / 256.0, 0.0).rgb;   // colour texel i covers [i, i + 1)
    }
    o = vec4(acc / float(kRays) * mul, 1.0);
}
)";

}  // namespace

void P4Land::init(Ctx& c) {
    const DopeImage& p4 = c.exe->part(4);
    Indexed hm = readIndexed(*c.exe, *findPicture("p4-landscape-height"));
    std::vector<float> h(hm.px.size());
    for (size_t i = 0; i < h.size(); i++) {
        h[i] = float(hm.px[i]);
        hmax_ = std::max(hmax_, h[i]);
    }
    height_.createRaw(hm.w, hm.h, GL_R16F, GL_RED, GL_FLOAT, h.data(), true, true, false);
    // the colour map's indices through the landscape palette (the catalog's palette of the map: 0010:20D2)
    Indexed cm = readIndexed(*c.exe, *findPicture("p4-landscape-colour"));
    std::vector<float> rgb(cm.px.size() * 4);
    for (size_t i = 0; i < cm.px.size(); i++) {
        Vec3 v = cm.pal[cm.px[i]];
        rgb[i * 4 + 0] = v.x;
        rgb[i * 4 + 1] = v.y;
        rgb[i * 4 + 2] = v.z;
        rgb[i * 4 + 3] = 1.0f;
    }
    colour_.createRaw(cm.w, cm.h, GL_RGBA16F, GL_RGBA, GL_FLOAT, rgb.data(), true, true, false);
    // 1ee8 fills the background with colour 2 (eax = 02020202h) of the landscape palette
    sky_ = vgaPalette(p4.csSpan(0x20d2, 16 * 3))[2];
    // the camera's u: [2111] / 16 texels (1f6c: [2111] << 12 in 8.16 fixed point)
    ucam_ = float(p4.cs16(0x2111)) / 16.0f;
    // the far end: the first depth row, [2117] = 6Eh (1f0a): DIST at 0010:2058 + 6Eh, z = 16 DIST + 15 - (P & 15)
    // with P even: d = z / 32 between DIST / 2 + 1/32 and DIST / 2 + 15/32, 8/32 on average
    dfar_ = (16.0f * float(p4.cs16(0x2058 + 0x6e)) + 8.0f) / 32.0f;
    // the nearest depth a ray of the screen (down to its bottom edge, row 200) can meet the highest terrain at
    dnear_ = (kBase - kScale * hmax_) / (200.0f - kHorizon) * 0.99f;
    colShader_.build(kFullscreenVS, std::string(kCommon) + kRowsFS, "p4 land rows");
    scanShader_.build(kFullscreenVS, kScanFS, "p4 land scan");
    drawShader_.build(kFullscreenVS, std::string(kCommon) + kDrawFS, "p4 land");
}

void P4Land::draw(Ctx& c, double vcam, float mul) {
    int w = c.out->w;
    if (w != w_) {
        for (Target& t : scan_) t.create(w, kDepthSamples, false, GL_R32F);
        w_ = w;
    }
    float step = (dfar_ - dnear_) / float(kDepthSamples - 1);
    float cv = float(std::fmod(vcam, 256.0));
    auto common = [&](Shader& s) {
        s.use();
        height_.bind(0);
        s.set("height", 0);
        s.set("cam", ucam_, cv);
        s.set("proj", kHorizon, kBase, kScale, kCentreX);
        s.set("depth", dnear_, step);
        s.set("cols", float(w));
    };
    setBlend(Blend::None);
    // pass 1: rows per depth sample
    scan_[0].bind();
    common(colShader_);
    drawFullscreen();
    // pass 2: running minimum outwards
    int src = 0;
    scanShader_.use();
    scanShader_.set("src", 0);
    for (int off = 1; off < kDepthSamples; off *= 2) {
        scan_[1 - src].bind();
        scan_[src].bindTex(0);
        scanShader_.set("off", off);
        drawFullscreen();
        src = 1 - src;
    }
    // pass 3: the pixels
    c.out->bind();
    common(drawShader_);
    scan_[src].bindTex(1);
    colour_.bind(2);
    drawShader_.set("rows", 1);
    drawShader_.set("colour", 2);
    drawShader_.set("samples", kDepthSamples);
    drawShader_.set("sky", sky_);
    drawShader_.set("mul", mul);
    drawShader_.set("pixH", 200.0f / float(c.out->h));
    drawFullscreen();
}
