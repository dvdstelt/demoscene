// The vector engine's renderer, 0x1f5d1, on the GPU: transform and projection (0x18ea1 / 0x19022 / 0x19118), the
// face list with back-face culling (0x19f78), and the triangle filler 0x1e07c with its span loops 0x1e77c / 0x1f07c:
// texel + highlight (map 0x220000 built by 0x6592) through the colour tables 0x1a479 / 0x1b079 / 0x1bc79 (0x1a336,
// flash 0x1a229). See jizz/re/notes-engine.md sections 5 and 6, notes-core.md section 6.
//
// Instead of the original's radix-sorted painter's algorithm (0x1966e, 0x19240): a depth buffer with MSAA, into a
// buffer of our own that is composited onto the part's frame (premultiplied, so anti-aliased edges blend with the
// background the part drew first). Texture coordinates are interpolated perspective-correctly and read filtered;
// the highlight is evaluated per pixel from the interpolated normal: (255 - 128 sin a)^4 / 2^24 with a the angle
// between the normal and the view axis (0x6592's table, continuous); the colour tables become
// x = clamp((texel + highlight) S/256 + E/255, 0, 1), x = mix(x, 1 - x, F/256) per channel (no 5:6:5).
// Faces crossing the near limit are clipped at z = 16 instead of dropped. Insets are clipped with clip distances.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "jizz/engine.h"

#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif

namespace jizz {

const char* kEngineGLSL = R"(
// Nix's engine pixel (0x1e77c): texel + highlight through the colour tables 0x1a336 (esf = E/255, S/256, F/256).
// The highlight map 0x6592 is centred on the view axis: (255 - r)^4 / 2^24 at r = 128 sin(angle to the axis).
vec3 engineColour(vec3 texel, vec3 n, vec3 right, vec3 up, vec3 esf) {
    n = normalize(n);
    float a = dot(n, right), b = dot(n, up);
    float r = min(128.0 * sqrt(a * a + b * b), 128.0);
    float e = pow(255.0 - r, 4.0) * (1.0 / (16777216.0 * 255.0));
    vec3 x = clamp((texel + e) * esf.y + esf.x, 0.0, 1.0);
    return mix(x, 1.0 - x, esf.z);
}
)";

namespace {

const char* kVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aCode;
uniform mat4 uView, uProj;
uniform vec4 uClip;   // viewport in NDC: left, right, bottom, top
out vec3 vNrm;
out vec2 vUV;
noperspective out vec3 vNrmA;   // Debug::original only: screen-linear like the original's spans
noperspective out vec2 vUVA;
flat out int vCode;
out float gl_ClipDistance[4];
void main() {
    vec4 c = uProj * (uView * vec4(aPos, 1.0));
    gl_Position = c;
    gl_ClipDistance[0] = c.x - uClip.x * c.w;
    gl_ClipDistance[1] = uClip.y * c.w - c.x;
    gl_ClipDistance[2] = c.y - uClip.z * c.w;
    gl_ClipDistance[3] = uClip.w * c.w - c.y;
    vNrm = vNrmA = aNrm;
    vUV = vUVA = aUV;
    vCode = int(aCode + 0.5);
}
)";

const char* kFS = R"(
in vec3 vNrm;
in vec2 vUV;
noperspective in vec3 vNrmA;
noperspective in vec2 vUVA;
flat in int vCode;
out vec4 o;
uniform sampler2D uTex0, uTex1, uTex2, uTex3;
uniform vec3 uRight, uUp, uESF;
uniform int uOriginal;

// Debug::original: the original's shortcuts, for checking the formulas against its pages: screen-linear (u, v)
// and highlight coordinates, nearest texels, the 0x6592 table with its centre at (127, 128), the integer colour
// tables of 0x1a336 and the 5:6:5 truncation (expanded like tools/compare.py).
vec3 originalPixel(int unit) {
    ivec2 ti = ivec2(floor(vUVA * 256.0)) & 255;
    vec3 t;
    if (unit == 0) t = texelFetch(uTex0, ti, 0).rgb;
    else if (unit == 1) t = texelFetch(uTex1, ti, 0).rgb;
    else if (unit == 2) t = texelFetch(uTex2, ti, 0).rgb;
    else t = texelFetch(uTex3, ti, 0).rgb;
    ivec2 ei = ivec2(floor(128.0 + 128.0 * vec2(dot(vNrmA, uRight), dot(vNrmA, uUp)))) & 255;
    float d = length(vec2(ei) - vec2(127.0, 128.0));
    float e = floor(pow(255.0 - d, 4.0) / 16777216.0 + 0.5);
    float E = floor(uESF.x * 255.0 + 0.5), S = floor(uESF.y * 256.0 + 0.5), F = floor(uESF.z * 256.0 + 0.5);
    vec3 v = clamp(floor(((floor(t * 255.0 + 0.5) + e) * S + E * 256.0) / 256.0), 0.0, 255.0);
    v = floor(((255.0 - F) * v + v + (255.0 - v) * F) / 256.0);
    vec3 q = floor(v / vec3(8.0, 4.0, 8.0));
    return floor(q * 255.0 / vec3(31.0, 63.0, 31.0)) / 255.0;
}

void main() {
    int unit = vCode & 3;
    if (uOriginal != 0) {
        o = vec4(originalPixel(unit), 1.0);
        return;
    }
    vec3 n = normalize(vNrm);
    vec2 uv = vCode >= 4 ? 0.5 * n.xy : vUV;   // 0x79a8: (u, v) = 128 n.xy texels
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    vec3 t;
    if (unit == 0) t = textureGrad(uTex0, uv, dx, dy).rgb;
    else if (unit == 1) t = textureGrad(uTex1, uv, dx, dy).rgb;
    else if (unit == 2) t = textureGrad(uTex2, uv, dx, dy).rgb;
    else t = textureGrad(uTex3, uv, dx, dy).rgb;
    o = vec4(engineColour(t, n, uRight, uUp, uESF), 1.0);
}
)";

const char* kCompositeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uTex;
void main() { o = texelFetch(uTex, ivec2(gl_FragCoord.xy), 0); }
)";

void setupVao(GLuint& vao, GLuint& vbo) {
    gl::GenVertexArrays(1, &vao);
    gl::BindVertexArray(vao);
    gl::GenBuffers(1, &vbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei st = sizeof(EngineGpuVertex);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)12);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)24);
    gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)32);
    for (GLuint i = 0; i < 4; i++) gl::EnableVertexAttribArray(i);
}

// world-space depth along the view axis (the original's z')
float depth(const EngineCamera& c, const float* p) {
    return c.fwd.x * (p[0] - c.eye.x) + c.fwd.y * (p[1] - c.eye.y) + c.fwd.z * (p[2] - c.eye.z);
}

}  // namespace

// ---------------------------------------------------------------- EngineScene

void EngineScene::build(const JizzExe& exe, int scene, int detail) {
    EngineCameraPath path;
    path.load(exe, scene);
    build(buildEngineGeometry(exe, scene, detail), path);
}

void EngineScene::build(const EngineGeometry& g, const EngineCameraPath& path) {
    geo_ = g;
    path_ = path;
    slots_.clear();
    verts_.clear();
    ranges_.clear();
    auto unit = [&](int slot) {
        for (size_t i = 0; i < slots_.size(); i++)
            if (slots_[i] == slot) return int(i);
        if (slots_.size() == 4) {
            SDL_Log("engine scene %d: more than 4 texture slots, slot %d drawn with slot %d", path.scene, slot, slots_[0]);
            return 0;
        }
        slots_.push_back(slot);
        return int(slots_.size() - 1);
    };
    for (const EngineObject& o : geo_.objects) {
        uint32_t first = uint32_t(verts_.size());
        for (uint32_t k = 0; k < o.faceCount; k++) {
            const EngineFace& f = geo_.faces[o.firstFace + k];
            if (f.type < 1 || f.type > 3) continue;  // draw table 0x1926e: only 1..3 fill
            float code = float(unit(f.tex));
            const uint32_t c[3] = {f.a, f.b, f.c};
            for (int i = 0; i < 3; i++) {
                const EngineVertex& v = geo_.vertices[c[i]];
                float u = f.ownUV ? f.uv[2 * i] : v.u, w = f.ownUV ? f.uv[2 * i + 1] : v.v;
                verts_.push_back({{v.p[0], v.p[1], v.p[2]}, {v.n[0], v.n[1], v.n[2]}, {u / 256.0f, w / 256.0f}, code});
            }
        }
        ranges_.push_back({first, uint32_t(verts_.size()) - first});
    }
    if (!vao_) setupVao(vao_, vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(EngineGpuVertex)), verts_.data(), GL_STATIC_DRAW);
    gl::BindVertexArray(0);
}

void EngineScene::setVisible(int object, bool on) {
    if (object < 0 || object >= int(geo_.objects.size())) return;
    uint32_t& f = geo_.objects[size_t(object)].flags;
    f = on ? (f & ~0x43u) : (f | 0x43u);
}

bool EngineScene::visible(int object) const {
    return object >= 0 && object < int(geo_.objects.size()) && !(geo_.objects[size_t(object)].flags & 0x40);
}

// ---------------------------------------------------------------- EngineMesh

void EngineMesh::upload(const float* pos, const float* nrm, const float* uv, size_t n, const uint32_t* idx, size_t ni) {
    verts_.resize(n);
    float code = uvFromNormal ? 4.0f : 0.0f;
    for (size_t i = 0; i < n; i++) {
        EngineGpuVertex& v = verts_[i];
        const float* q = nrm + 3 * i;
        float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        l = l > 0 ? 1.0f / l : 0.0f;
        for (int k = 0; k < 3; k++) {
            v.p[k] = pos[3 * i + size_t(k)];
            v.n[k] = q[k] * l;
        }
        v.uv[0] = uv ? uv[2 * i] / 256.0f : 0.0f;
        v.uv[1] = uv ? uv[2 * i + 1] / 256.0f : 0.0f;
        v.unit = code;
    }
    idx_.assign(idx, idx + ni);
    if (!vao_) {
        setupVao(vao_, vbo_);
        gl::GenBuffers(1, &ibo_);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    }
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(EngineGpuVertex)), verts_.data(), GL_STREAM_DRAW);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx_.size() * 4), idx_.data(), GL_STREAM_DRAW);
    gl::BindVertexArray(0);
}

// ---------------------------------------------------------------- Engine

Engine::Engine() {
    if (!shader_.build(kVS, std::string(kEngineGLSL) + kFS, "engine")) SDL_Log("engine shader failed");
    if (!composite_.build(kFullscreenVS, kCompositeFS, "engine composite")) SDL_Log("engine composite shader failed");
    const uint8_t grey[4] = {128, 128, 128, 255};
    dummy_.createRaw(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, grey, true, true, false);
    setupVao(painterVao_, painterVbo_);
    gl::BindVertexArray(0);
}

void Engine::ensureTargets(int w, int h) {
    GLint maxS = 1;
    gl::GetIntegerv(GL_MAX_SAMPLES, &maxS);
    int s = std::max(1, std::min(samples, int(maxS)));
    if (w == msW_ && h == msH_ && s == msSamples_ && msFbo_) return;
    if (msFbo_) gl::DeleteFramebuffers(1, &msFbo_);
    if (msColour_) gl::DeleteRenderbuffers(1, &msColour_);
    if (msDepth_) gl::DeleteRenderbuffers(1, &msDepth_);
    msW_ = w;
    msH_ = h;
    msSamples_ = s;
    GLsizei ns = s > 1 ? s : 0;
    gl::GenRenderbuffers(1, &msColour_);
    gl::BindRenderbuffer(GL_RENDERBUFFER, msColour_);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, ns, GL_RGBA16F, w, h);
    gl::GenRenderbuffers(1, &msDepth_);
    gl::BindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, ns, GL_DEPTH24_STENCIL8, w, h);
    gl::GenFramebuffers(1, &msFbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColour_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_Log("engine: %dx%d framebuffer with %d samples incomplete", w, h, s);
    resolved_.create(w, h, false, GL_RGBA16F);
}

void Engine::begin(Target& out, const EngineCamera& cam, const EngineViewport& vp, const EngineShading& sh) {
    out_ = &out;
    ensureTargets(out.w, out.h);
    gl::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    gl::Viewport(0, 0, out.w, out.h);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_TRUE);
    gl::ClearColor(0, 0, 0, 0);
    gl::ClearDepth(1.0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (debug.painter) gl::Disable(GL_DEPTH_TEST);
    else {
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
    }
    for (int i = 0; i < 4; i++) gl::Enable(GL_CLIP_DISTANCE0 + GLenum(i));
    cam_ = cam;
    vp_ = vp;
    sh_ = sh;
    shader_.use();
    shader_.set("uView", cam.view());
    shader_.set("uProj", cam.projection(vp));
    shader_.set("uClip", vp.x / 160.0f - 1.0f, (vp.x + vp.w) / 160.0f - 1.0f, 1.0f - (vp.y + vp.h) / 100.0f,
                1.0f - vp.y / 100.0f);
    shader_.set("uRight", cam.right);
    shader_.set("uUp", cam.up);
    shader_.set("uESF", sh.E / 255.0f, sh.S / 256.0f, sh.F / 256.0f);
    shader_.set("uOriginal", debug.original ? 1 : 0);
    shader_.set("uTex0", 0);
    shader_.set("uTex1", 1);
    shader_.set("uTex2", 2);
    shader_.set("uTex3", 3);
    frameSlots_.clear();
    painter_.clear();
}

void Engine::bindUnits(const std::vector<int>& slots) {
    for (int u = 0; u < 4; u++) {
        int slot = slots.empty() ? 0 : slots[size_t(u) < slots.size() ? size_t(u) : 0];
        GLuint t = slot > 0 && slot < int(sh_.tex.size()) ? sh_.tex[size_t(slot)] : 0;
        gl::ActiveTexture(GL_TEXTURE0 + GLenum(u));
        gl::BindTexture(GL_TEXTURE_2D, t ? t : dummy_.id);
    }
    gl::ActiveTexture(GL_TEXTURE0);
}

void Engine::setDrawState(bool cull) {
    // 0x19f78 keeps the faces that are clockwise on the screen (positive area with y down); GL calls those back
    // faces (front = counter-clockwise), so its front faces are culled.
    if (cull) {
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_FRONT);
    } else {
        gl::Disable(GL_CULL_FACE);
    }
}

int Engine::unitFor(int slot) {
    for (size_t i = 0; i < frameSlots_.size(); i++)
        if (frameSlots_[i] == slot) return int(i);
    if (frameSlots_.size() == 4) return 0;
    frameSlots_.push_back(slot);
    return int(frameSlots_.size() - 1);
}

void Engine::addPainterFaces(const std::vector<EngineGpuVertex>& verts, const uint32_t* idx, uint32_t first,
                             uint32_t count, const std::vector<int>& slots) {
    for (uint32_t f = 0; f + 3 <= count; f += 3) {
        PainterFace pf;
        float z = 0;
        bool nearDrop = false;
        for (int i = 0; i < 3; i++) {
            uint32_t k = idx ? idx[first + f + uint32_t(i)] : first + f + uint32_t(i);
            pf.v[i] = verts[k];
            float d = depth(cam_, pf.v[i].p);
            nearDrop |= d < 16.5f;  // 0x18ea1: fist(z') <= 16 marks the vertex, 0x19f78 drops its faces
            z += d;
            int code = int(pf.v[i].unit + 0.5f);
            int slot = slots.empty() ? 0 : slots[size_t(code & 3) < slots.size() ? size_t(code & 3) : 0];
            pf.v[i].unit = float(unitFor(slot) | (code & 4));
        }
        if (debug.dropNear && nearDrop) continue;
        pf.key = int(std::nearbyint(z)) & 0xffff;  // 0x19f78 key, 0x1966e sorts its low 16 bits
        painter_.push_back(pf);
    }
}

void Engine::draw(const EngineScene& s) {
    if (!out_ || !s.vao_) return;
    if (debug.painter || debug.dropNear) {  // faces collected on the CPU, drawn at end()
        for (size_t o = 0; o < s.ranges_.size(); o++)
            if (s.visible(int(o))) addPainterFaces(s.verts_, nullptr, s.ranges_[o].first, s.ranges_[o].second, s.slots_);
        return;
    }
    bindUnits(s.slots_);
    setDrawState(true);
    gl::BindVertexArray(s.vao_);
    for (size_t o = 0; o < s.ranges_.size(); o++)
        if (s.visible(int(o)) && s.ranges_[o].second)
            gl::DrawArrays(GL_TRIANGLES, GLint(s.ranges_[o].first), GLsizei(s.ranges_[o].second));
    gl::BindVertexArray(0);
}

void Engine::draw(const EngineMesh& m) {
    if (!out_ || !m.vao_ || m.idx_.empty()) return;
    std::vector<int> slots{m.texSlot};
    if (debug.painter || debug.dropNear) {
        addPainterFaces(m.verts_, m.idx_.data(), 0, uint32_t(m.idx_.size()), slots);
        return;
    }
    bindUnits(slots);
    setDrawState(m.cull);
    gl::BindVertexArray(m.vao_);
    gl::DrawElements(GL_TRIANGLES, GLsizei(m.idx_.size()), GL_UNSIGNED_INT, nullptr);
    gl::BindVertexArray(0);
}

void Engine::end() {
    if (!out_) return;
    Target& out = *out_;
    out_ = nullptr;
    if (!painter_.empty()) {
        // 0x1966e + 0x19240: far to near; equal keys keep the list order
        if (debug.painter)
            std::stable_sort(painter_.begin(), painter_.end(),
                             [](const PainterFace& a, const PainterFace& b) { return a.key > b.key; });
        std::vector<EngineGpuVertex> v;
        v.reserve(painter_.size() * 3);
        for (const PainterFace& f : painter_) v.insert(v.end(), f.v, f.v + 3);
        bindUnits(frameSlots_);
        setDrawState(true);
        gl::BindVertexArray(painterVao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, painterVbo_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(EngineGpuVertex)), v.data(), GL_STREAM_DRAW);
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(v.size()));
        gl::BindVertexArray(0);
    }
    for (int i = 0; i < 4; i++) gl::Disable(GL_CLIP_DISTANCE0 + GLenum(i));
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);

    // resolve the samples, then blend the premultiplied result over the part's frame
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved_.fbo);
    gl::BlitFramebuffer(0, 0, out.w, out.h, 0, 0, out.w, out.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    out.bind();
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl::Enable(GL_BLEND);
    composite_.use();
    resolved_.bindTex(0);
    composite_.set("uTex", 0);
    drawFullscreen();
    gl::Disable(GL_BLEND);
}

}  // namespace jizz
