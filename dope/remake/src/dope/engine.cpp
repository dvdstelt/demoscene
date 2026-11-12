// The 3D engine's renderer (engine.h): the frame build of p2 354a / 3585 / 3310 and its copies in p3, p5, p6, p7 on
// the GPU. The vertex shader is the vertex loop (1c60 / 1b60: matrix 1d86 applied to the vertex plus pivot, the
// projection with the per-object f and the 0.75 vertical scale, the env coordinates from the rotated normal, the
// texture words from the mesh or the object-space normal); culling is 3377's screen winding; the fragment shader is
// both fillers (2881 / 2660 texture + env: texel code -> bump offset -> env texel; 3030 / 2ea0 env only) with the
// colours computed in float from filtered maps. Faces are ordered by a depth buffer, or as the radix sort 3585
// orders them (Order::Painter: by the sum of the vertices' local depth times f, far first). Drawing goes into an
// MSAA buffer of our own, composited premultiplied over the part's frame inside the view's clip rectangle, so the
// anti-aliased edges blend with the background.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "dope/engine.h"

namespace dope {

namespace {

// Near and far planes of the depth buffer (mesh units in front of the eye): the nearest vertex in the demo is
// about 900 units away (p5's torus), the farthest object about 64000 (p3's start).
constexpr double kNear = 32.0, kFar = 400000.0;

const char* kVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aTex;    // the mesh's map coordinates: (high word, low word)
uniform mat3 uR;                      // Rz(-c) Rx(-a) Ry(b)
uniform vec3 uPiv, uPivAfter;         // pivot (mesh units), p5's pivot / 32768 after the rotation
uniform vec3 uT;                      // (object + camera position) / 128
uniform vec4 uProj;                   // clip x = x X + y d, clip y = z Y + w d
uniform vec3 uDepth;                  // z_ndc = x + y / D at the depth D = d + z V.z
uniform int uTexSrc;                  // 0 mesh, 1 object-space normal, 2 the normal with the 15-bit wrap
out vec3 vN;
out vec2 vT;
noperspective out vec3 vNA;           // Debug::affine: screen-linear as the fillers interpolate
noperspective out vec2 vTA;
void main() {
    vec3 V = uR * (aPos + uPiv) + uPivAfter;
    float d = uT.z - V.z;
    float D = d + uDepth.z * V.z;
    gl_Position = vec4(uProj.x * (V.x + uT.x) + uProj.y * d, uProj.z * (V.y + uT.y) + uProj.w * d,
                       (uDepth.x + uDepth.y / D) * d, d);
    vN = uR * aNrm;
    if (uTexSrc == 0) {
        vT = aTex;
    } else {
        vec2 n = aNrm.yx;
        if (uTexSrc == 2) n = mod(n + 16384.0, 32768.0) - 16384.0;   // sext15
        vT = n;
    }
    vNA = vN;
    vTA = vT;
}
)";

const char* kFS = R"(
in vec3 vN;
in vec2 vT;
noperspective in vec3 vNA;
noperspective in vec2 vTA;
out vec4 o;
uniform int uAffine, uMaxBox;
uniform sampler2D uEnv, uBump, uPal;
uniform vec4 uEnvWin;      // the 128 x 128 window: u0, v0, u1 - u0, v1 - v0
uniform vec4 uEnvClampR;   // where reads are clamped (texture coordinates): x0, y0, x1, y1
uniform int uEnvLevel;     // 0: colour map; 1: level map through the palette ramp
uniform vec2 uLevel;       // index = x + y * value
uniform float uEnvSign;    // p6: -1
uniform int uHasBump;
uniform vec3 uTexMap;      // bump texels: column = hi * x, row = lo * y + z
uniform vec2 uBumpSize;    // the bump texture in texels
uniform float uBumpScale;
uniform vec3 uMul, uAdd;

// e = (column, row) in texels of the 128 map; g = its derivatives
vec3 envColour(vec2 e, vec2 gx, vec2 gy) {
    vec2 k = uEnvWin.zw / 128.0;
    vec2 t = clamp(uEnvWin.xy + e * k, uEnvClampR.xy, uEnvClampR.zw);
    vec4 c = textureGrad(uEnv, t, gx * k, gy * k);
    if (uEnvLevel != 0) return dpPalLerp(uPal, uLevel.x + uLevel.y * c.r);
    return c.rgb;
}

void main() {
    // 1c60: env column / row = 64 + (R n).xy / 512 (8.8 words of the rotated normal), interpolated
    vec3 nv = uAffine != 0 ? vNA : vN;
    vec2 tv = uAffine != 0 ? vTA : vT;
    vec2 e = 64.0 + uEnvSign * nv.xy / 512.0;
    vec2 dex = dFdx(e), dey = dFdy(e);
    vec3 c;
    if (uHasBump == 0) {
        c = envColour(e, dex, dey);
    } else {
        // 2660: texel code at the texture words -> displacement of the env lookup (decoded, filtered)
        vec2 tc = vec2(tv.x * uTexMap.x, tv.y * uTexMap.y + uTexMap.z) / uBumpSize;
        vec2 dtx = dFdx(tc), dty = dFdy(tc);
        vec2 b0 = textureGrad(uBump, tc, dtx, dty).rg * uBumpScale;
        vec2 gx = dex + dFdx(b0), gy = dey + dFdy(b0);
        // a box filter over the pixel where the bump map is minified
        float fp = max(length(dtx * uBumpSize), length(dty * uBumpSize));
        int k = int(clamp(ceil(fp), 1.0, float(uMaxBox)));
        float fk = float(k);
        c = vec3(0.0);
        for (int sy = 0; sy < k; sy++)
            for (int sx = 0; sx < k; sx++) {
                vec2 s = (vec2(sx, sy) + 0.5) / fk - 0.5;
                vec2 b = textureGrad(uBump, tc + dtx * s.x + dty * s.y, dtx / fk, dty / fk).rg * uBumpScale;
                c += envColour(e + dex * s.x + dey * s.y + b, gx / fk, gy / fk);
            }
        c /= fk * fk;
    }
    o = vec4(clamp(c * uMul + uAdd, 0.0, 1.0), 1.0);
}
)";

const char* kCompositeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uTex;
void main() { o = texelFetch(uTex, ivec2(gl_FragCoord.xy), 0); }
)";

enum Unit { kEnv, kBump, kPal, kUnits };

// Pixel rectangle (GL, rows from the bottom) of a VGA rectangle in a w x h target covering the view.
void pixelRect(const View& v, int w, int h, float x, float y, float rw, float rh, int r[4]) {
    float x0 = (x - v.x) / v.w * float(w), x1 = (x + rw - v.x) / v.w * float(w);
    float y0 = float(h) - (y + rh - v.y) / v.h * float(h), y1 = float(h) - (y - v.y) / v.h * float(h);
    r[0] = std::clamp(int(std::lround(x0)), 0, w);
    r[1] = std::clamp(int(std::lround(y0)), 0, h);
    r[2] = std::clamp(int(std::lround(x1)), 0, w) - r[0];
    r[3] = std::clamp(int(std::lround(y1)), 0, h) - r[1];
}

}  // namespace

// A draw waiting for the painter's sort.
struct Engine::Pending {
    Object o;
    Materials m;
};

struct Engine::Buffers {
    int w = 0, h = 0, samples = 0;
    GLuint fbo = 0, colour = 0, depth = 0;
    Target resolved;
    ~Buffers() {
        if (fbo) gl::DeleteFramebuffers(1, &fbo);
        if (colour) gl::DeleteRenderbuffers(1, &colour);
        if (depth) gl::DeleteRenderbuffers(1, &depth);
        resolved.destroy();
    }
};

Engine::Engine() {
    if (!shader_.build(kVS, std::string(kGLSL) + kFS, "engine")) SDL_Log("engine shader failed");
    if (!composite_.build(kFullscreenVS, kCompositeFS, "engine composite")) SDL_Log("engine composite failed");
    const uint8_t zero[4] = {0, 0, 0, 0}, white[4] = {255, 255, 255, 255};
    zero_.createRaw(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, zero, false, true, false);
    white_.createRaw(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, white, false, true, false);
    shader_.use();
    shader_.set("uEnv", int(kEnv));
    shader_.set("uBump", int(kBump));
    shader_.set("uPal", int(kPal));
    pending_ = new std::vector<Pending>;
}

Engine::~Engine() {
    for (Buffers* b : buffers_) delete b;
    delete pending_;
    if (sortIbo_) gl::DeleteBuffers(1, &sortIbo_);
    zero_.destroy();
    white_.destroy();
}

Engine::Buffers& Engine::buffers(int w, int h) {
    GLint maxS = 1;
    gl::GetIntegerv(GL_MAX_SAMPLES, &maxS);
    int s = std::max(1, std::min(samples, int(maxS)));
    for (Buffers* b : buffers_)
        if (b->w == w && b->h == h && b->samples == s) return *b;
    if (buffers_.size() >= 3) {   // one set per size in use; a resize drops the oldest
        delete buffers_.front();
        buffers_.erase(buffers_.begin());
    }
    Buffers* b = new Buffers;
    b->w = w, b->h = h, b->samples = s;
    GLsizei ns = s > 1 ? s : 0;
    gl::GenRenderbuffers(1, &b->colour);
    gl::BindRenderbuffer(GL_RENDERBUFFER, b->colour);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, ns, GL_RGBA16F, w, h);
    gl::GenRenderbuffers(1, &b->depth);
    gl::BindRenderbuffer(GL_RENDERBUFFER, b->depth);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, ns, GL_DEPTH24_STENCIL8, w, h);
    gl::GenFramebuffers(1, &b->fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, b->fbo);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, b->colour);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, b->depth);
    if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_Log("engine: %dx%d framebuffer with %d samples incomplete", w, h, s);
    b->resolved.create(w, h, false, GL_RGBA16F);
    buffers_.push_back(b);
    return *b;
}

void Engine::begin(Target& out, const EnginePart& part, const Pose& camera, const View& view, Order order) {
    out_ = &out;
    part_ = part;
    cam_ = camera;
    view_ = view;
    frameOrder_ = order;
    pending_->clear();
    cur_ = &buffers(out.w, out.h);
    gl::BindFramebuffer(GL_FRAMEBUFFER, cur_->fbo);
    gl::Viewport(0, 0, out.w, out.h);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_TRUE);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::ClearColor(0, 0, 0, 0);
    gl::ClearDepth(1.0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (frameOrder_ == Order::Depth) gl::Enable(GL_DEPTH_TEST);
    else gl::Disable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LESS);
    gl::Enable(GL_CULL_FACE);
    shader_.use();
    shader_.set("uEnvSign", part.envNegate ? -1.0f : 1.0f);
    int src = part.texSource == EnginePart::TexSource::Mesh ? 0 : part.wrap15 ? 2 : 1;
    shader_.set("uTexSrc", src);
    shader_.set("uAffine", debug.affine ? 1 : 0);
    shader_.set("uMaxBox", debug.singleSample ? 1 : 3);
}

// The object's matrix, translation, projection and culling (348b, 1d86).
void Engine::setObject(const Object& o) {
    Pose p = o.pose + cam_;
    std::array<double, 9> R = engineRotation(p.a, p.b, p.c);
    float r3[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r3[j * 3 + i] = float(R[size_t(i * 3 + j)]);   // column-major
    gl::UniformMatrix3fv(shader_.loc("uR"), 1, GL_FALSE, r3);
    shader_.set("uPiv", float(o.pivot[0]), float(o.pivot[1]), float(o.pivot[2]));
    if (part_.pivotAfter)
        shader_.set("uPivAfter", float(o.pivot[0] / 32768.0), float(o.pivot[1] / 32768.0), float(o.pivot[2] / 32768.0));
    else
        shader_.set("uPivAfter", 0.0f, 0.0f, 0.0f);
    shader_.set("uT", float(p.x / 128.0), float(p.y / 128.0), float(p.z / 128.0));
    // VGA x = 160 - f/8 X / d, y = 99 - 3f/32 Y / d, mapped onto the view's rectangle
    const View& v = view_;
    double fx = o.f / 8.0, fy = 3.0 * o.f / 32.0;
    shader_.set("uProj", float(-2.0 * fx / v.w), float(2.0 * (160.0 - v.x) / v.w - 1.0), float(2.0 * fy / v.h),
                float(1.0 - 2.0 * (99.0 - v.y) / v.h));
    double a = (kFar + kNear) / (kFar - kNear), b = -2.0 * kFar * kNear / (kFar - kNear);
    shader_.set("uDepth", float(a), float(b), float(1.0 - o.depthScale));
    // 3377: cross product >= 0 on the y-down screen (clockwise as seen) is drawn for +2c = 0; GL's front faces are
    // counter-clockwise, so those are its back faces
    gl::CullFace(o.cullFlip ? GL_BACK : GL_FRONT);
}

// The filler a face's selectors pick (3310: texseg[B & 3], envseg[C & 3]).
void Engine::setMaterial(const Materials& m, int envSel, int texSel) {
    const EnvMap& e = m.env[envSel & 3];
    const BumpMap* b = m.tex[texSel & 3];
    gl::ActiveTexture(GL_TEXTURE0 + kEnv);
    gl::BindTexture(GL_TEXTURE_2D, e.tex ? e.tex : zero_.id);
    gl::ActiveTexture(GL_TEXTURE0 + kBump);
    gl::BindTexture(GL_TEXTURE_2D, b && b->tex.id ? b->tex.id : zero_.id);
    gl::ActiveTexture(GL_TEXTURE0 + kPal);
    gl::BindTexture(GL_TEXTURE_2D, e.levels && e.levels->tex.id ? e.levels->tex.id : white_.id);
    gl::ActiveTexture(GL_TEXTURE0);
    shader_.set("uEnvWin", e.u0, e.v0, e.u1 - e.u0, e.v1 - e.v0);
    // clamp half a texel inside the window (or the texture), so that filtering does not read past it
    float hx = 0.5f / float(std::max(e.texW, 1)), hy = 0.5f / float(std::max(e.texH, 1));
    float x0 = e.clamp ? std::min(e.u0, e.u1) : 0.0f, x1 = e.clamp ? std::max(e.u0, e.u1) : 1.0f;
    float y0 = e.clamp ? std::min(e.v0, e.v1) : 0.0f, y1 = e.clamp ? std::max(e.v0, e.v1) : 1.0f;
    shader_.set("uEnvClampR", x0 + hx, y0 + hy, x1 - hx, y1 - hy);
    shader_.set("uEnvLevel", e.levels ? 1 : 0);
    shader_.set("uLevel", e.levelBase, e.levelScale);
    shader_.set("uHasBump", b && b->tex.id ? 1 : 0);
    if (b) {
        shader_.set("uTexMap", b->colScale, b->rowScale, b->rowOffset);
        shader_.set("uBumpSize", 128.0f, float(b->rows));
    }
    shader_.set("uBumpScale", m.bumpScale);
    shader_.set("uMul", m.mul);
    shader_.set("uAdd", m.add);
}

void Engine::draw(const Object& o, const Materials& m) {
    if (!cur_ || !o.mesh || o.mesh->faces.empty()) return;   // 348b: nf = 0 skips the object
    if (!o.mesh->vao_) o.mesh->upload();
    if (frameOrder_ == Order::Painter) {
        pending_->push_back({o, m});
        return;
    }
    shader_.use();
    setObject(o);
    gl::BindVertexArray(o.mesh->vao_);
    for (const EngineMesh::Group& g : o.mesh->groups_) {
        setMaterial(m, (g.attr & 0xff) + o.envAdd + m.envAdd, (g.attr >> 8) + o.texAdd + m.texAdd);
        gl::DrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, (void*)(size_t(g.first) * 4));
    }
    gl::BindVertexArray(0);
}

// 3377 + 3585 + 3310: every face of the frame keyed by the sum of its vertices' local depth V.z times f (the
// translation not included), sorted ascending (far first), stable in object and face order, drawn in that order.
void Engine::drawPainter() {
    struct Key { double key; uint32_t draw, face; };
    std::vector<Key> keys;
    std::vector<ProjectedVertex> pv;
    for (size_t i = 0; i < pending_->size(); i++) {
        const Pending& p = (*pending_)[i];
        projectObject(part_, cam_, p.o, pv);
        for (size_t j = 0; j < p.o.mesh->faces.size(); j++) {
            const EngineMesh::Face& f = p.o.mesh->faces[j];
            keys.push_back({(pv[f.a].vz + pv[f.b].vz + pv[f.c].vz) * p.o.f, uint32_t(i), uint32_t(j)});
        }
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.key < b.key; });
    // runs of faces of one draw with one material
    struct Run { uint32_t draw; uint16_t attr; int first, count; };
    std::vector<Run> runs;
    std::vector<uint32_t> idx;
    idx.reserve(keys.size() * 3);
    for (const Key& k : keys) {
        const EngineMesh::Face& f = (*pending_)[k.draw].o.mesh->faces[k.face];
        if (runs.empty() || runs.back().draw != k.draw || runs.back().attr != f.attr)
            runs.push_back({k.draw, f.attr, int(idx.size()), 0});
        idx.push_back(f.a);
        idx.push_back(f.b);
        idx.push_back(f.c);
        runs.back().count += 3;
    }
    if (idx.empty()) return;
    if (!sortIbo_) gl::GenBuffers(1, &sortIbo_);
    shader_.use();
    for (const Run& r : runs) {
        const Pending& p = (*pending_)[r.draw];
        setObject(p.o);
        setMaterial(p.m, (r.attr & 0xff) + p.o.envAdd + p.m.envAdd, (r.attr >> 8) + p.o.texAdd + p.m.texAdd);
        gl::BindVertexArray(p.o.mesh->vao_);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, sortIbo_);
        if (&r == &runs.front())
            gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STREAM_DRAW);
        gl::DrawElements(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, (void*)(size_t(r.first) * 4));
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.o.mesh->ibo_);   // the VAO's own index buffer again
        gl::BindVertexArray(0);
    }
}

void Engine::end() {
    if (!cur_) return;
    if (frameOrder_ == Order::Painter) drawPainter();
    pending_->clear();
    Buffers& b = *cur_;
    cur_ = nullptr;
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    // resolve the samples, then blend the premultiplied result over the target inside the clip rectangle
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, b.fbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, b.resolved.fbo);
    gl::BlitFramebuffer(0, 0, b.w, b.h, 0, 0, b.w, b.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    out_->bind();
    int r[4];
    pixelRect(view_, out_->w, out_->h, view_.clipX, view_.clipY, view_.clipW, view_.clipH, r);
    gl::Scissor(r[0], r[1], r[2], r[3]);
    gl::Enable(GL_SCISSOR_TEST);
    setBlend(Blend::Premul);
    composite_.use();
    b.resolved.bindTex(0);
    composite_.set("uTex", 0);
    drawFullscreen();
    gl::Disable(GL_SCISSOR_TEST);
    out_ = nullptr;
}

}  // namespace dope
