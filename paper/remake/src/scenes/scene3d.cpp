// The coloured 3D scene ("scene3d", ticks 3186-5480) and the first page peel ("peel1", 5480-5611)
// (paper/re/notes-scene3d.md; data and per-tick state in scene3d_data.h).
//
// Ports:
//   0x5eec  scene render: sky gradient 0x1bae (0x80 -> 0x9f over 350 rows above the horizon), ground 0x6d, the
//           pencil horizon line 0x18b7, the object groups ({0,1} {2..8} {9,10}, from [0x482b] >= 0x600
//           {0,1} {5,6,7} {2} {9,10} {3,4} {8}) through the 3D core (0x3fb3 / 0x43df), mode 0 or 2
//   0x603f  frame: scene + paper composite 0x1943, or (transition) the pencil pass: shadow 0x42e4, composite, the
//           red pencil (mode 1) on top
//   0x72c4  colour transition (ticks 3187-3441): mode-0 and mode-2 renders mixed by the noise mask
//           N[x] + N[0x207 - row] - 18 >= threshold (byte [0x7324], 255 -> 0 by 0x7244), callback 0x6199
//   0x726d / 0x3c3e / 0x3dc0  the banner cloth from the noise, every frame (callback 0x620b)
//   0x73a6 / 0x7418  the "Statix + Vic (acme)" drawing on the banner, one point per tick from tick 4211
//   0x5623 / 0x2b55 / 0x2b6c  the peel of the live scene over the logo page (core Peel, page from the "logo" part)
//
// High resolution (BRIEF fidelity rules):
//   - camera on the spline at fractional ticks (angles not floored); the noise as the continuous 5-sine sum at
//     fractional phases; the transition mask evaluated per pixel from it with an anti-aliased edge, so the blobs
//     grow smoothly in the original's places; the banner cloth evaluated on a 4x2 finer grid (smooth flutter, finer
//     facets, outlines on the borders and fold silhouettes as before); the drawing grows continuously along the
//     segment between its per-tick points, drawn with the core's pencil strokes.
//   - kept: the z <= 400 face cull, the banner's asymmetric sag (truncated 0x2ad0 step), the drawing's
//     missing sag (0x7418 projects warp(u, v) without it: up to ~8 px above the cloth in the middle, what the
//     original shows), no depth test for the drawing, the frozen screen while the logo page is built (5480-5482),
//     the banner's set-up shape during the transition and its jump to the noise shape at 3442.
//   - timing follows the reference capture (paper/re/full.frames), measured against it: in the 3D scene the frame of
//     tick t shows the camera and noise after the IRQ of t - 2 (camera H((240 (t - 3188)) >> 7): the integer camera
//     matches the capture's horizon row at 98 of 105 sampled ticks, against 73 for t - 3187) and the drawing after
//     the IRQ of t - 1 (it is drawn at the end of the frame); the transition frames took ~2 ticks: the mask uses the
//     noise of t - 3.5 and the threshold of t - 2.5, the camera t - 3.5 (eased to t - 2 over its last 42 ticks, where
//     the picture is all colour, so the motion stays continuous), the pencil t - 2.5.
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "demo.h"
#include "paper/core2d.h"
#include "paper/obj3d.h"
#include "paper/pencil.h"
#include "paper/render3d.h"
#include "scenes/scene3d_data.h"

using namespace paper;

namespace scene3d {
namespace {

// Display latency of the capture (see the top): the state shown at tick t is the one after the IRQ of t - lag.
constexpr double kCamLag = 2.0;         // 3D scene: camera, noise, groups
constexpr double kCamLagTrans = 3.5;    // transition: camera
constexpr double kHwLag = 1.0;          // the drawing's reveal
constexpr double kMaskPhaseLag = 3.5;   // transition: noise phases of the mask
constexpr double kMaskThrLag = 2.5;     // transition: threshold
constexpr double kPencilLag = 2.5;      // transition: the pencil and its shadow
constexpr double kCamEase = 42.0;       // ticks before 3442 over which the camera's lag goes from 3.5 to 2

constexpr int kBannerSubU = 4, kBannerSubV = 2;  // finer cloth grid: 16x10 cells -> 64x20

// ---------------------------------------------------------------- shaders
// 0x1bae sky (index 0x80 at row h - 350 to 0x9f at the horizon, continuous) and ground 0x6d; above the gradient
// the 0x3a background (never on screen in practice).
const char* kBackgroundFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uPal;
uniform float uH;   // horizon: first ground row (VGA, continuous)
void main() {
    float y = (1.0 - uv.y) * 200.0;
    float dy = max(fwidth(y), 1e-4);
    float r0 = uH - 350.0;
    float idx = 128.0 + (y - 0.5 - r0) * 31.0 / 350.0 - 0.5;
    vec3 sky = mix(palLookup(uPal, 58.0), palLookup(uPal, clamp(idx, 128.0, 159.0)), clamp((y - r0) / dy + 0.5, 0.0, 1.0));
    vec3 ground = palLookup(uPal, 109.0);
    o = vec4(mix(sky, ground, clamp((y - uH) / dy + 0.5, 0.0, 1.0)), 1.0);
}
)";

// 0x72c4 mask: colour where N[x] + N[0x207 - row] - 18 >= thr (mod 256), the table value N = floor(Nc): the continuous
// boundary is at Nc(x) + Nc(row) - 18.5 = thr. Mixes uA (sketch) and uB (colour) by the anti-aliased mask.
const char* kMixFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uA;
uniform sampler2D uB;
uniform vec3 uW[5];   // per wave: freq / 32768, phase / 32768, amp / 4
uniform float uThr;
float Nc(float i) {
    float x = 2048.0 - i, s = 0.0;
    for (int k = 0; k < 5; k++) s += uW[k].z * sin(6.28318530718 * fract(uW[k].x * x + uW[k].y));
    return clamp(128.0 + s, 0.0, 250.0) * 0.5;
}
void main() {
    vec2 vga = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float v = Nc(vga.x - 0.5) + Nc(519.0 - (vga.y - 0.5)) - 18.5;
    if (v < 0.0) v += 256.0;   // the 8-bit wrap (deepest minima first; never reached with the original's noise)
    float d = v - uThr;
    float m = clamp(d / max(fwidth(d), 1e-4) + 0.5, 0.0, 1.0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    o = mix(texelFetch(uA, p, 0), texelFetch(uB, p, 0), m);
}
)";

// 0x42e4 / 0x1943: in the pencil's shadow the paper row e becomes e | 0x20, a shade-table row of
// c = (e >> 1) + (e >> 3) (17 / 18 for e = 28..31): darkening 32 - c instead of 32 - e. Continuous, mean-matched:
// c = 0.625 e - 0.94, i.e. 0.8775 + 0.375 e ramp steps more than the paper (added where the mask covers).
const char* kShadowAddFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uMask;
uniform sampler2D uGrain;
void main() {
    float m = texelFetch(uMask, ivec2(gl_FragCoord.xy), 0).r;
    if (m <= 0.0) discard;
    vec2 vga = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    o = vec4(m * (0.8775 + 0.375 * paperGrain(uGrain, vga)), 0.0, 0.0, 0.0);
}
)";

const char* kShadowVS = R"(
layout(location = 0) in vec2 aPos;   // VGA
void main() { gl_Position = vec4(aPos.x / 160.0 - 1.0, 1.0 - aPos.y / 100.0, 0.0, 1.0); }
)";
const char* kShadowFS = R"(
out vec4 o;
void main() { o = vec4(1.0); }
)";

// ---------------------------------------------------------------- helpers
// The banner on a finer grid with the 0x3a8c face pattern (v, v+1, v+W+1), (v+W+1, v+W, v), grey 0x30, add 0 (keeps
// its shade in the sketch mode), the four borders flagged as outlines, no angle creases (as 0x3eeb finds none).
Object3D makeFineBanner(int su, int sv) {
    const int W = 16 * su + 1, H = 10 * sv + 1, cw = W - 1, ch = H - 1;
    Object3D o;
    o.name = "banner";
    o.ivert.assign(size_t(W * H), IVec3());
    o.vert.assign(size_t(W * H), Vec3d());
    o.face.resize(size_t(2 * cw * ch));
    auto A = [&](int r, int c) { return uint16_t(2 * (r * cw + c)); };
    for (int r = 0; r < ch; r++)
        for (int c = 0; c < cw; c++) {
            const uint16_t v = uint16_t(r * W + c), a = A(r, c), b = uint16_t(a + 1);
            Face3D& fa = o.face[a];
            Face3D& fb = o.face[b];
            fa.v[0] = v; fa.v[1] = uint16_t(v + 1); fa.v[2] = uint16_t(v + W + 1);
            fb.v[0] = uint16_t(v + W + 1); fb.v[1] = uint16_t(v + W); fb.v[2] = v;
            fa.nb[0] = r > 0 ? uint16_t(A(r - 1, c) + 1) : a;
            fa.nb[1] = c < cw - 1 ? uint16_t(A(r, c + 1) + 1) : a;
            fa.nb[2] = b;
            fb.nb[0] = r < ch - 1 ? A(r + 1, c) : b;
            fb.nb[1] = c > 0 ? A(r, c - 1) : b;
            fb.nb[2] = a;
            fa.outline = uint8_t((r == 0 ? 1 : 0) | (c == cw - 1 ? 2 : 0));
            fb.outline = uint8_t((r == ch - 1 ? 1 : 0) | (c == 0 ? 2 : 0));
            fa.base = fb.base = 0x30;
            fa.add = fb.add = 0;
        }
    o.updateNormals();
    return o;
}

// Multisampled coverage mask of a set of VGA triangles (the pencil's shadow: a union, so no seams), resolved into a
// single-sample R8 target.
class MaskRaster {
public:
    void draw(Ctx& c, Target& out, const std::vector<float>& tris) {
        ensure(out.w, out.h);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl::Viewport(0, 0, w_, h_);
        gl::ClearColor(0, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        if (!tris.empty()) {
            setBlend(Blend::None);
            gl::Disable(GL_DEPTH_TEST);
            shader_.use();
            gl::BindVertexArray(vao_);
            gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(tris.size() * sizeof(float)), tris.data(), GL_STREAM_DRAW);
            gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(tris.size() / 2));
            gl::BindVertexArray(0);
        }
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, out.fbo);
        gl::BlitFramebuffer(0, 0, w_, h_, 0, 0, out.w, out.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (c.out) c.out->bind();
    }
    void init() {
        shader_.build(kShadowVS, kShadowFS, "scene3d-shadow");
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        gl::EnableVertexAttribArray(0);
        gl::BindVertexArray(0);
    }

private:
    void ensure(int w, int h) {
        if (fbo_ && w == w_ && h == h_) return;
        if (fbo_) {
            gl::DeleteFramebuffers(1, &fbo_);
            gl::DeleteRenderbuffers(1, &rb_);
        }
        w_ = w;
        h_ = h;
        GLint maxSamples = 4;
        gl::GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        gl::GenFramebuffers(1, &fbo_);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl::GenRenderbuffers(1, &rb_);
        gl::BindRenderbuffer(GL_RENDERBUFFER, rb_);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, std::clamp(int(maxSamples), 1, 8), GL_R8, w, h);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_);
    }
    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0, fbo_ = 0, rb_ = 0;
    int w_ = 0, h_ = 0;
};

// ---------------------------------------------------------------- the part's world
class World {
public:
    explicit World(Ctx& c)
        : lib_(c.shared<Objects3D>()),
          pal_(ShadePalette::fromExe(*c.exe)),
          path_(*c.exe),
          transWaves_(Waves::init(*c.exe, RngPos::kTransitionNoise)),
          sceneWaves_(Waves::init(*c.exe, RngPos::kSceneNoise)),
          hw_(handwritingPoints(*c.exe)) {
        for (int s = 0; s < Objects3D::kSlots; s++) obj_[s] = lib_.scene[s];
        // the finished sketch: per-face reveal masks and the stroke still in progress
        int yaw, pitch;
        IVec3 pos;
        path_.exactKey0(yaw, pitch, pos);
        reveal_ = std::make_unique<SketchReveal>(*c.exe, lib_, yaw, pitch, pos);
        for (int s = 0; s <= Objects3D::Flower; s++) {
            if (s == Objects3D::Banner) continue;
            for (size_t f = 0; f < obj_[s].face.size(); f++) obj_[s].face[f].sketch = reveal_->mask[s][f];
        }
        banner_ = makeFineBanner(kBannerSubU, kBannerSubV);
        bannerSetup_ = banner_;
        setBannerShape(bannerSetup_, nullptr);
        bg_.build(kFullscreenVS, std::string(kPaletteGLSL) + kBackgroundFS, "scene3d-background");
        mix_.build(kFullscreenVS, kMixFS, "scene3d-mix");
        shadowAdd_.build(kFullscreenVS, std::string(kTexturesGLSL) + kShadowAddFS, "scene3d-shadow-add");
        shadowMask_.init();
        c.shared<Renderer3D>();
        c.shared<CoreTextures>();
        c.shared<PaletteTex>();
    }

    // The coloured scene (0x620b loop and the live page of the peel) at tick t.
    void renderScene(Ctx& c, Target& frame, double t) {
        const double ts = std::max(t - kCamLag, double(kSceneFirst));
        const CameraPath::Pose pose = cameraAt(t);
        const Waves w = sceneWaves_.advanced(ts - kSceneFirst, 2);
        setBannerShape(banner_, &w);
        const View3D view = makeView(pose, Mode3D::Colour);
        PencilInk ink(c, frame);
        drawColour(c, frame, ink, view, pose, lateGroups(t), &banner_);
        paper(c, ink);
        drawHandwriting(ink, view.cam, w, t - kHwLag - 4210.0);
        ink.resolve(c, frame, &pal_);
    }

    // The finished sketch (3186) and the colour transition (3187-3441), with the pencil flying off.
    void renderTransition(Ctx& c, Target& frame, double t) {
        const CameraPath::Pose pose = cameraAt(t);
        // the mask: noise phases and threshold (0x7244 per tick from 3187: phase += speed, thr 255 -> 0)
        const double tp = std::max(t - kMaskPhaseLag, double(kPartFirst));
        const Waves w = transWaves_.advanced(tp - kPartFirst, 0);
        const double thr = t < kTransitionFirst ? 1e9 : 3441.0 - std::max(t - kMaskThrLag, double(kPartFirst));
        double vmin, vmax;
        maskRange(w, vmin, vmax);
        const bool allSketch = vmax < thr - 1.0, allColour = vmin > thr + 1.0;

        PencilInk ink(c, frame);
        if (allSketch) {
            drawSketch(c, frame, ink, pose);
        } else if (allColour) {
            drawColour(c, frame, ink, makeView(pose, Mode3D::Colour), pose, false, &bannerSetup_);
        } else {
            auto colT = c.temp(), skT = c.temp();
            PencilInk inkC(c, *colT), inkS(c, *skT);
            drawColour(c, *colT, inkC, makeView(pose, Mode3D::Colour), pose, false, &bannerSetup_);
            drawSketch(c, *skT, inkS, pose);
            mixLayers(c, frame, skT->tex, colT->tex, w, thr);
            mixLayers(c, ink.target(), inkS.target().tex, inkC.target().tex, w, thr);
        }
        paper(c, ink);
        // the pencil pass (0x603f): its shadow on the paper, the composite, the pencil on top without paper (its
        // faces cover the paper's darkness in the ink)
        Object3D& pencil = obj_[Objects3D::Pencil];
        placePencil(pencil, t - kPencilLag);
        std::vector<float> shadow;
        if (pencilShadow(pencil, shadow)) addShadow(c, frame, ink, shadow);
        if (pencilVisible(pencil)) drawPencil(c, frame, pencil, ink);
        ink.resolve(c, frame, &pal_);
        if (c.out) c.out->bind();
    }

private:
    // Camera of tick t: after the IRQ of n the camera is H((240 (n - 3186)) >> 7) (0x6199 / 0x620b evaluate 0x74e8
    // before adding 240 to the accumulator), here at continuous n = t - lag: p = 1.875 (n - 3186).
    CameraPath::Pose cameraAt(double t) const { return path_.at(1.875 * (t - camLag(t) - kPartFirst)); }
    static double camLag(double t) {
        if (t >= kSceneFirst) return kCamLag;
        const double t0 = kSceneFirst - kCamEase;
        if (t <= t0) return kCamLagTrans;
        return kCamLagTrans + (kCamLag - kCamLagTrans) * (t - t0) / kCamEase;
    }
    // [0x482b] >= 0x600 for the frame of tick t (p after the IRQ of n = t - 2 is (240 (n - 3185)) >> 7): from 4007 on.
    static bool lateGroups(double t) {
        const int n = int(std::floor(t - kCamLag));
        return n >= kPartFirst && ((240 * (n - 3185)) >> 7) >= 0x600;
    }

    View3D makeView(const CameraPath::Pose& pose, Mode3D mode) const {
        View3D v;
        v.cam = Camera3D::yawPitch(pose.yaw, pose.pitch, pose.pos);
        v.light = lib_.light;
        v.mode = mode;
        v.palette = &pal_;
        return v;
    }

    // 0x3c3e on the fine grid (the noise terms are separable: one set per column, one per row).
    void setBannerShape(Object3D& o, const Waves* w) {
        const int W = 16 * kBannerSubU + 1, H = 10 * kBannerSubV + 1;
        std::vector<Vec3d> v(size_t(W * H));
        std::vector<double> ci(size_t(3 * W)), rj(size_t(3 * H));
        for (int c = 0; c < W; c++)
            for (int k = 0; k < 3; k++) ci[size_t(3 * c + k)] = w ? w->N(3.0 * (2048.0 * c / (W - 1)) / 32.0 + 256 * k) : 0.0;
        for (int r = 0; r < H; r++)
            for (int k = 0; k < 3; k++) rj[size_t(3 * r + k)] = w ? w->N((950.0 * r / (H - 1)) / 16.0 + 128 + 256 * k) : 0.0;
        for (int r = 0; r < H; r++)
            for (int c = 0; c < W; c++) {
                const double u = 2048.0 * c / (W - 1), vv = 950.0 * r / (H - 1), s = u * 30.0 / 32768.0;
                const double* a = &ci[size_t(3 * c)];
                const double* b = &rj[size_t(3 * r)];
                v[size_t(r * W + c)] = Vec3d(0x7bc + u + (a[0] - 64 + b[0] - 64) * vv / 2048.0,
                                             -0x123 + vv + (a[1] - 64 + b[1] - 64) * vv / 2048.0 + 90.0 * s * (2.0 - s),
                                             -3 + u / 512.0 + (a[2] - 80 + b[2] - 80) * vv / 256.0);
            }
        o.setVertices(v);
    }

    std::vector<const Object3D*> list(std::initializer_list<int> ids, const Object3D* banner) const {
        std::vector<const Object3D*> l;
        for (int i : ids) l.push_back(i == Objects3D::Banner ? banner : &obj_[i]);
        return l;
    }

    // 0x5eec groups through the core renderer (open pass: begin() done by the caller, end() after).
    void groups(Renderer3D& r, bool late, const Object3D* banner, const std::vector<FaceLine>& lines = {}) {
        if (!late) {
            r.group(list({0, 1}, banner), lines);
            // trees before the house, as the original's bucket order has them (see sketch.cpp)
            r.group(list({5, 6, 7}, banner), lines);
            r.group(list({2, 3, 4, 8}, banner), lines);
            r.group(list({9, 10}, banner), lines);
        } else {
            r.group(list({0, 1}, banner));
            r.group(list({5, 6, 7}, banner));
            r.group(list({2}, banner));
            r.group(list({9, 10}, banner));
            r.group(list({3, 4}, banner));
            r.group(list({8}, banner));
        }
    }

    // Mode 0: sky, ground, horizon line, the coloured objects with outlines.
    void drawColour(Ctx& c, Target& t, PencilInk& ink, const View3D& view, const CameraPath::Pose& pose, bool late,
                    const Object3D* banner) {
        const double cp = cosTurn(pose.pitch);
        const double h = 100.0 - 224.0 * sinTurn(pose.pitch) / (std::abs(cp) > 1e-9 ? cp : 1.0);
        t.bind();
        setBlend(Blend::None);
        bg_.use();
        bg_.set("uH", float(h));
        bg_.set("uPal", 0);
        c.shared<PaletteTex>().def.bind(0);
        drawFullscreen();
        ink.line(PencilLine(Vec2f{0.5f, float(h + 0.5)}, Vec2f{320.5f, float(h + 0.5)}));
        Renderer3D& r = c.shared<Renderer3D>();
        r.begin(c, t, view);
        groups(r, late, banner);
        r.end(&ink);
    }

    // Mode 2: paper 0x3a, the faces as paper, the 274 completed strokes where they are silhouettes / creases from the
    // current camera, the stroke in progress from the pen to its start; the banner keeps its grey shade.
    void drawSketch(Ctx& c, Target& t, PencilInk& ink, const CameraPath::Pose& pose) {
        t.bind();
        setBlend(Blend::None);
        gl::ClearColor(pal_.rgb[0x3a].x, pal_.rgb[0x3a].y, pal_.rgb[0x3a].z, 1.0f);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        std::vector<FaceLine> lines;
        if (reveal_->curSlot >= 0)
            lines.push_back({&obj_[reveal_->curSlot], reveal_->curFace,
                             PencilLine::pixels(reveal_->penX, reveal_->penY, reveal_->startX, reveal_->startY)});
        Renderer3D& r = c.shared<Renderer3D>();
        r.begin(c, t, makeView(pose, Mode3D::Sketch));
        groups(r, false, &bannerSetup_, lines);
        r.end(&ink);
    }

    // 0x1943: the paper grain, darkening 32 - e ramp steps (e the continuous shade row of the grain).
    void paper(Ctx& c, PencilInk& ink) { ink.addTexture(c.shared<CoreTextures>().grain.id, -1.0f, 32.0625f, true); }

    // Range of the mask value over the screen (separable: column term + row term).
    static void maskRange(const Waves& w, double& vmin, double& vmax) {
        double cmin = 1e9, cmax = -1e9, rmin = 1e9, rmax = -1e9;
        for (int k = 0; k <= 640; k++) {
            const double n = w.Nc(k * 0.5 - 0.5);
            cmin = std::min(cmin, n);
            cmax = std::max(cmax, n);
        }
        for (int k = 0; k <= 400; k++) {
            const double n = w.Nc(519.0 - (k * 0.5 - 0.5));
            rmin = std::min(rmin, n);
            rmax = std::max(rmax, n);
        }
        vmin = cmin + rmin - 18.5;
        vmax = cmax + rmax - 18.5;
        if (vmin < 0) vmin = -1e9;  // the wrap: treat as mixed
    }

    void mixLayers(Ctx& c, Target& dst, GLuint a, GLuint b, const Waves& w, double thr) {
        dst.bind();
        setBlend(Blend::None);
        mix_.use();
        float wv[15];
        for (int k = 0; k < 5; k++) {
            wv[3 * k] = float(w.freq[k] / 32768.0);
            wv[3 * k + 1] = float(w.phase[k] / 32768.0);
            wv[3 * k + 2] = float(w.amp[k] * 0.25);
        }
        gl::Uniform3fv(mix_.loc("uW"), 5, wv);
        mix_.set("uThr", float(thr));
        mix_.set("uA", 0);
        mix_.set("uB", 1);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, a);
        gl::ActiveTexture(GL_TEXTURE0 + 1);
        gl::BindTexture(GL_TEXTURE_2D, b);
        drawFullscreen();
        gl::ActiveTexture(GL_TEXTURE0);
        if (c.out) c.out->bind();
    }

    // 0x6199 / 0x603f: pen at the end of the sketch, z 0 -> -1421 (-7 per tick), a_cnt 0 -> 123 (+1 per tick),
    // a = 2 a_cnt + 10, b = a + 90 - ((x - 160) >> 1), position ((x - 160) 8.93, (y - 100) 8.93, z) for the camera at
    // (0, 0, -2000).
    void placePencil(Object3D& p, double tick) const {
        const double k = std::max(tick - kPartFirst, 0.0);
        const double z = std::max(-1421.0, -7.0 * k), acnt = std::min(123.0, k);
        const double a = 2.0 * acnt + 10.0, b = a + 90.0 - ((reveal_->penX - 160) >> 1);
        p.pos = Vec3d((reveal_->penX - 160) * (0x8edb6 / 65536.0), (reveal_->penY - 100) * (0x8edb6 / 65536.0), z);
        p.rot = rotXY(a, b);
    }
    // 0x42e4: every pencil face projected from the light (-1300, 100, -2500) onto the paper z = 0, then through the
    // pencil camera; no culling. Returns whether any of it is on screen.
    bool pencilShadow(const Object3D& p, std::vector<float>& tris) const {
        const Vec3d L = lib_.light;
        std::vector<Vec2f> s(p.vert.size());
        for (size_t i = 0; i < p.vert.size(); i++) {
            const Vec3d w = p.rot * p.vert[i] + p.pos;
            const double t = -L.z / std::max(w.z - L.z, 1e-3);
            s[i] = Vec2f{float(160.0 + 224.0 * (L.x + t * (w.x - L.x)) / 2000.0),
                         float(100.0 + 224.0 * (L.y + t * (w.y - L.y)) / 2000.0)};
        }
        bool on = false;
        for (const Face3D& f : p.face)
            for (int k = 0; k < 3; k++) {
                const Vec2f& q = s[f.v[k]];
                tris.push_back(q.x);
                tris.push_back(q.y);
                on = on || (q.x > -50 && q.x < 370 && q.y > -50 && q.y < 250);
            }
        return on;
    }
    bool pencilVisible(const Object3D& p) const {
        for (const Vec3d& v : p.vert) {
            const Vec3d w = p.rot * v + p.pos;
            const double z = w.z + 2000.0;
            if (z <= Renderer3D::kNear) continue;
            const double x = 160 + 224 * w.x / z, y = 100 + 224 * w.y / z;
            if (x > -100 && x < 420 && y > -100 && y < 300) return true;
        }
        return false;
    }
    void addShadow(Ctx& c, Target& frame, PencilInk& ink, const std::vector<float>& tris) {
        auto mask = c.tempSized(frame.w, frame.h, false, GL_R8);
        shadowMask_.draw(c, *mask, tris);
        Target& it = ink.target();
        it.bind();
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE);
        gl::Enable(GL_BLEND);
        shadowAdd_.use();
        shadowAdd_.set("uMask", 0);
        shadowAdd_.set("uGrain", 1);
        mask->bindTex(0);
        c.shared<CoreTextures>().grain.bind(1);
        drawFullscreen();
        setBlend(Blend::None);
        gl::ActiveTexture(GL_TEXTURE0);
        if (c.out) c.out->bind();
    }
    void drawPencil(Ctx& c, Target& frame, const Object3D& p, PencilInk& ink) {
        View3D v;
        v.cam = Camera3D{Mat3d(), Vec3d(0, 0, -2000)};
        v.light = lib_.light;
        v.mode = Mode3D::Pencil;
        v.palette = &pal_;
        Renderer3D& r = c.shared<Renderer3D>();
        r.begin(c, frame, v);
        r.group({&p});
        r.end(&ink);
    }

    // 0x73a6: the revealed points (count = entries after the IRQ, fractional: the next segment grows), projected with
    // the cloth's warp but without its sag (0x7418; a point at z <= 400 keeps the previous position), one pencil line
    // per segment, pen-ups skipped; after the paper, no depth test.
    void drawHandwriting(PencilInk& ink, const Camera3D& cam, const Waves& w, double count) {
        if (count <= 1.0 || hw_.empty()) return;
        const double full = std::floor(count), frac = count - full;
        Vec2f P{0, 0};
        auto proj = [&](const HwPoint& q) {
            Vec2f s;
            if (cam.project(bannerPoint(&w, q.u, q.v, false), s)) P = s;
        };
        proj(hw_[0]);
        for (size_t q = 1;;) {
            if (q >= hw_.size() || double(q) >= count) return;
            const Vec2f prev = P;
            if (hw_[q].u == 0) {
                if (++q >= hw_.size() || hw_[q].u == 0) return;
                proj(hw_[q++]);
                continue;
            }
            const float reveal = double(q) < full ? 1.0f : float(frac);
            proj(hw_[q++]);
            ink.line(PencilLine(prev, P, 0.0f, reveal));
        }
    }

    Objects3D& lib_;
    ShadePalette pal_;
    CameraPath path_;
    Waves transWaves_, sceneWaves_;
    std::vector<HwPoint> hw_;
    std::unique_ptr<SketchReveal> reveal_;
    Object3D obj_[Objects3D::kSlots];
    Object3D banner_, bannerSetup_;
    Shader bg_, mix_, shadowAdd_;
    MaskRaster shadowMask_;
};

// ---------------------------------------------------------------- the two parts
class Scene3D : public Scene {
public:
    void init(Ctx& c) override { c.shared<World>(); }
    void render(Ctx& c, double tick) override {
        World& w = c.shared<World>();
        if (tick < kSceneFirst) w.renderTransition(c, *c.out, tick);
        else w.renderScene(c, *c.out, std::min(tick, double(kPeelPartFirst)));
    }
};

class Peel1 : public Scene {
public:
    void init(Ctx& c) override {
        c.shared<World>();
        c.shared<Peel>();
    }
    void render(Ctx& c, double tick) override {
        World& w = c.shared<World>();
        Target& out = *c.out;
        if (tick < kPeelFirst) {  // 0x5623 builds the page: the screen keeps the last scene frame
            w.renderScene(c, out, kPeelPartFirst);
            return;
        }
        auto scene = c.temp(), page = c.temp();
        w.renderScene(c, *scene, tick);
        c.renderPart("logo", std::max(tick, double(kPeelPartFirst)), *page);
        out.bind();
        c.shared<Peel>().draw(c, scene->tex, page->tex, peelProgress(tick, kPeelSceneFirstTick));
    }
};

}  // namespace

REGISTER_SCENE(Scene3D, "scene3d");
REGISTER_SCENE(Peel1, "peel1");

}  // namespace scene3d
