// The "sketch" part (ticks 1079-3186): white paper, the red pencil comes in, rises and sketches the flower, the house,
// the trees and the path as grey pencil lines (the edges of the 3D scene that scene3d then shows in colour).
//
// Ports (paper/re/notes-sketch.md):
//   0x603f  frame: the scene in sketch mode (0x5eec, mode 2) on the paper, then the pencil (mode 1) with its shadow
//   0x5eec  scene in mode 2: every face paper-grey 0x3a (hidden lines removed by the painter's groups {0,1} {2..8}
//           {9,10}), the edges of the completed strokes (0x6133 masks) and the stroke in progress
//   0x42e4  pencil shadow: the pencil projected from the light (-1300, 100, -2500) onto the paper z = 0, one darkness
//           level (paper rows 60..63 instead of 28..31 through the shade table 0x144b)
//   0x1943  paper composite: the grain darkens everything drawn by 0x5eec (lines and shadow included), not the pencil
//   0x61ee / 0x6267 / 0x5e7f / 0x6133: in sketch.h (stroke plan, pencil motion)
//
// Remake: the 3D core draws the scene (Renderer3D, MSAA, pencil lines from PencilInk) and the pencil (mode 1); the
// shadow is rasterised from the exact projected pencil (4x MSAA) and added to the darkness layer; the paper is the
// core's grain. The stroke in progress grows continuously under the tip (see sketch.h). The pencil pose is evaluated
// at fractional ticks (position, height and angles interpolated / continuous instead of 1/1024-turn steps).
#include "scenes/sketch.h"

#include <cstdlib>
#include <vector>

#include <SDL3/SDL.h>

#include "paper/pencil.h"
#include "paper/render3d.h"
#include "paper/textures.h"

using namespace paper;

namespace {

constexpr double kPaperDist = 2000.0;  // the pencil camera: identity at (0, 0, -2000)

// shadow triangles (VGA) -> coverage
const char* kShadowVS = R"(
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos.x / 160.0 - 1.0, 1.0 - aPos.y / 100.0, 0.0, 1.0); }
)";
const char* kShadowFS = R"(
out vec4 o;
void main() { o = vec4(1.0); }
)";
// ink += coverage * (darkness in the shadow - darkness of the lit paper), both functions of the grain row e
const char* kShadowInkFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uMask;
uniform sampler2D uGrain;
uniform vec2 uK;
void main() {
    float m = texelFetch(uMask, ivec2(gl_FragCoord.xy), 0).r;
    if (m <= 0.0) discard;
    float e = texBicubic(uGrain, vec2(uv.x, 1.0 - uv.y)).r;
    o = vec4(m * (uK.x * e + uK.y), 0.0, 0.0, 0.0);
}
)";

// Paper darkness (shade-table steps) for the grain row e: lit 32 - e (the core's continuous grain, as core3dtest);
// in the shadow (rows e + 32: k = ((e) >> 1) + ((e) >> 3) = 17, 17, 18, 18 for e = 28..31) 15, 15, 14, 14, i.e. on
// average 14.47 with a weak grain: 14.47 - 0.4 (e - 29.56). The shadow adds the difference: 0.6 e - 5.77.
constexpr float kLitAdd = 32.0625f;
constexpr float kShadowMul = 0.6f, kShadowAdd = -5.77f;

class SketchScene : public Scene {
public:
    void init(Ctx& c) override {
        plan_ = &sketch::sketchPlan(c);
        lib_ = &c.shared<Objects3D>();
        for (int s = 0; s < Objects3D::kSlots; s++) obj_[s] = lib_->scene[s];
        pal_ = ShadePalette::fromExe(*c.exe);
        c.shared<Renderer3D>();
        c.shared<CoreTextures>();
        shadowSh_.build(kShadowVS, kShadowFS, "sketch-shadow");
        shadowInkSh_.build(kFullscreenVS, kShadowInkFS, "sketch-shadow-ink");
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        gl::EnableVertexAttribArray(0);
        gl::BindVertexArray(0);
        if (const char* d = std::getenv("SKETCH_DUMP")) plan_->dump(d);
    }

    void render(Ctx& c, double tick) override {
        Target& frame = *c.out;
        const sketch::Reveal r = plan_->at(tick);
        c.clear(pal_.rgb[0x3a]);
        PencilInk ink(c, frame);
        Renderer3D& r3 = c.shared<Renderer3D>();
        if (r.done > 0 || r.partial >= 0) drawScene(c, frame, r, ink, r3);
        ink.addTexture(c.shared<CoreTextures>().grain.id, -1.0f, kLitAdd, true);
        posePencil(r);
        drawShadow(c, frame, ink);
        View3D v;
        v.cam = Camera3D{Mat3d(), Vec3d(0, 0, -kPaperDist)};
        v.light = lib_->light;
        v.mode = Mode3D::Pencil;
        v.palette = &pal_;
        r3.begin(c, frame, v);
        r3.group({&obj_[Objects3D::Pencil]});
        r3.end(&ink);  // the pencil covers the paper's and the lines' darkness: it is drawn on top, untextured
        ink.resolve(c, frame, &pal_);
    }

private:
    // 0x5eec in mode 2 with the reveal masks and the stroke in progress
    void drawScene(Ctx& c, Target& frame, const sketch::Reveal& r, PencilInk& ink, Renderer3D& r3) {
        plan_->applyMasks(obj_, r.done);
        std::vector<FaceLine> lines;
        FaceLine fl;
        if (plan_->partialLine(r, obj_, fl)) lines.push_back(fl);
        View3D v;
        v.cam = plan_->camera();
        v.light = lib_->light;
        v.mode = Mode3D::Sketch;
        v.palette = &pal_;
        r3.begin(c, frame, v);
        r3.group({&obj_[0], &obj_[1]}, lines);
        // The original sorts slots 2..8 together by depth bucket, which hides the tip of tree 5 behind the house
        // wall; a depth buffer would show it at the right edge. Painting the trees first reproduces that order
        // (no tree is in front of the house from this camera).
        r3.group({&obj_[5], &obj_[6], &obj_[7]}, lines);
        r3.group({&obj_[2], &obj_[3], &obj_[4], &obj_[8]}, lines);
        r3.group({&obj_[9], &obj_[10]}, lines);
        r3.end(&ink);
    }

    // 0x603f: position from the pen (tip on the paper point under the pen), angles a = 2 a_cnt + 10,
    // b = a + 90 - (x - 160) / 2 with x the pen's original pixel column
    void posePencil(const sketch::Reveal& r) {
        Object3D& p = obj_[Objects3D::Pencil];
        const double k = kPaperDist / Renderer3D::kFocal;
        p.pos = Vec3d((r.pen.x - 160.0) * k, (r.pen.y - 100.0) * k, r.z);
        const double a = 2.0 * r.acnt + 10.0;
        const double b = a + 90.0 - (double(r.pen.x) - 0.5 - 160.0) / 2.0;
        p.rot = rotXY(a, b);
    }

    // 0x42e4: every pencil face projected from the light onto z = 0, seen by the pencil camera; their union is the
    // shadow. Rasterised with 4x MSAA, then added to the darkness layer.
    void drawShadow(Ctx& c, Target& frame, PencilInk& ink) {
        const Object3D& p = obj_[Objects3D::Pencil];
        const Vec3d L = lib_->light;
        std::vector<Vec2f> sv(p.vert.size());
        for (size_t i = 0; i < p.vert.size(); i++) {
            const Vec3d w = p.rot * p.vert[i] + p.pos;
            const double t = -L.z / (w.z - L.z);
            const Vec3d s = L + (w - L) * t;
            sv[i] = Vec2f{float(160.0 + Renderer3D::kFocal * s.x / kPaperDist),
                          float(100.0 + Renderer3D::kFocal * s.y / kPaperDist)};
        }
        tri_.clear();
        for (const Face3D& f : p.face)
            for (int k = 0; k < 3; k++) {
                tri_.push_back(sv[f.v[k]].x);
                tri_.push_back(sv[f.v[k]].y);
            }
        if (!ensureMask(frame.w, frame.h)) return;
        gl::BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
        gl::Viewport(0, 0, frame.w, frame.h);
        gl::ClearColor(0, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_CULL_FACE);
        setBlend(Blend::None);
        shadowSh_.use();
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(tri_.size() * sizeof(float)), tri_.data(), GL_STREAM_DRAW);
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(tri_.size() / 2));
        gl::BindVertexArray(0);
        auto mask = c.tempSized(frame.w, frame.h, false, GL_R8);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, maskFbo_);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, mask->fbo);
        gl::BlitFramebuffer(0, 0, frame.w, frame.h, 0, 0, mask->w, mask->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        Target& it = ink.target();
        it.bind();
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE);
        gl::Enable(GL_BLEND);
        shadowInkSh_.use();
        shadowInkSh_.set("uMask", 0);
        shadowInkSh_.set("uGrain", 1);
        shadowInkSh_.set("uK", kShadowMul, kShadowAdd);
        mask->bindTex(0);
        c.shared<CoreTextures>().grain.bind(1);
        drawFullscreen();
        setBlend(Blend::None);
        gl::ActiveTexture(GL_TEXTURE0);
        frame.bind();
    }

    bool ensureMask(int w, int h) {
        if (maskFbo_ && maskW_ == w && maskH_ == h) return true;
        if (maskFbo_) {
            gl::DeleteFramebuffers(1, &maskFbo_);
            gl::DeleteRenderbuffers(1, &maskRb_);
            maskFbo_ = maskRb_ = 0;
        }
        GLint maxSamples = 4;
        gl::GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        gl::GenFramebuffers(1, &maskFbo_);
        gl::BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
        gl::GenRenderbuffers(1, &maskRb_);
        gl::BindRenderbuffer(GL_RENDERBUFFER, maskRb_);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, std::clamp(int(maxSamples), 1, 4), GL_R8, w, h);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, maskRb_);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("sketch: cannot create the %dx%d shadow mask", w, h);
            gl::DeleteFramebuffers(1, &maskFbo_);
            gl::DeleteRenderbuffers(1, &maskRb_);
            maskFbo_ = maskRb_ = 0;
            return false;
        }
        maskW_ = w;
        maskH_ = h;
        return true;
    }

    const sketch::SketchPlan* plan_ = nullptr;
    const Objects3D* lib_ = nullptr;
    Object3D obj_[Objects3D::kSlots];
    ShadePalette pal_;
    Shader shadowSh_, shadowInkSh_;
    GLuint vao_ = 0, vbo_ = 0;
    GLuint maskFbo_ = 0, maskRb_ = 0;
    int maskW_ = 0, maskH_ = 0;
    std::vector<float> tri_;
};

}  // namespace

REGISTER_SCENE(SketchScene, "sketch");
