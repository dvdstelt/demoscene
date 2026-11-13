// The 3D engine's drawing (engine.h): 0x4169f8 (the buckets far to near, the shade per vertex: 37.2 (1 - n_z)^3 from
// the normals or the faces' values, times the light [0x427f70]), the frustum clipper 0x41674b / 0x4166dc / 0x416669 /
// 0x416472 / 0x416297 and Craft's texture mapper 0x415127 (projection 9.1, edges 0x414f2f / 0x4150f7, spans with the
// unrolled loop 0x4159f1.., the bilinear filter 9.4 and the shade table 0x426b8c made by 0x416806), with the window
// of 0x414f04.
//
// Per draw the faces are tested on the CPU as 0x416929 does (back faces, the far cut) and the drawn ones go to the GPU
// as a stream of corners (camera-space position, u, v, shade). The vertex shader projects them with the half-pixel
// shift of the original's sample point; the fragment shader is the closed form of notes-engine 10: the map's log
// levels filtered bilinearly at the perspective-correct u, v, plus the shade interpolated on the screen, decoded with
// the exponential and clamped at white. Faces are ordered by a depth buffer (or in the original's order without
// it), drawn with MSAA into a buffer of our own and composited with their coverage over the part's frame inside the
// window (columns 0..320, rows 20..180 or 0..200).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "clone/engine.h"
#include "exe.h"

namespace cmc {

namespace {

constexpr int kFloats = 6;          // per corner: x, y, z (camera space), u, v (map widths), shade (table units)
constexpr double kNear = 1.0;       // the GPU's near plane (the original has none)
constexpr double kFar = 100000.0;   // beyond every drawn face (the far cut keeps mean z < 6826.7)

const char* kVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aShade;
uniform vec2 uX;      // clip x = uX.x x + uX.y z
uniform vec2 uY;      // clip y = uY.x y + uY.y z
uniform vec2 uZ;      // clip z = uZ.x z + uZ.y, clip w = z
out vec2 vUV;                   // perspective-correct (the original divides every 8 pixels)
noperspective out float vShade; // affine on the screen, as the mapper steps it
void main() {
    gl_Position = vec4(uX.x * aPos.x + uX.y * aPos.z, uY.x * aPos.y + uY.y * aPos.z, uZ.x * aPos.z + uZ.y, aPos.z);
    vUV = aUV;
    vShade = aShade;
}
)";

// The map holds log levels L / 255 (Textures' Log form, or the original's bytes): filtered as levels, decoded after
// the filter with the shade, as the mapper does (notes-engine 9.4, 9.5, 10). Where the map is minified the original's
// point samples average to the mean of the decoded values V = 128 exp(0.011 (L - 128)), not to the decoded mean of L
// (darker: the lattices' dark cells pull it down): the log-filtered sample of the base level is scaled by the ratio of
// the mipmapped linear form (the mean of V over the pixel) to its own base-level sample. Magnified, the ratio is 1.
const char* kFS = R"(
in vec2 vUV;
noperspective in float vShade;
out vec4 o;
uniform sampler2D uMap;     // log levels L / 255
uniform sampler2D uLin;     // the same map's linear values V / 255, mipmapped (uHasLin)
uniform int uHasLin;
uniform float uLight;
void main() {
    // u, v in map widths; texel centres at integer texel coordinates: + half a texel of the 256-texel map
    vec2 uv = vUV + 0.5 / 256.0;
    float s = vShade * uLight;
    if (uHasLin == 0) {
        o = vec4(cmDecodeMap(texture(uMap, uv).rgb, s), 1.0);
        return;
    }
    vec3 L = textureLod(uMap, uv, 0.0).rgb * 255.0;
    vec3 V = 128.0 * exp(0.011 * (L - 128.0)) * texture(uLin, uv).rgb / max(textureLod(uLin, uv, 0.0).rgb, vec3(1e-4));
    o = vec4(min(vec3(1.0), V * exp(0.011 * (s - 128.0)) / 255.0), 1.0);
}
)";

const char* kCompositeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uTex;
void main() { o = texelFetch(uTex, ivec2(gl_FragCoord.xy), 0); }
)";

// Pixel rectangle of a VGA rectangle in a target of w x h pixels covering the view (GL rows from the bottom).
void pixelRect(const View& v, int w, int h, float x0, float y0, float x1, float y1, int r[4]) {
    x0 = std::max(x0, v.x), y0 = std::max(y0, v.y), x1 = std::min(x1, v.x + v.w), y1 = std::min(y1, v.y + v.h);
    float px0 = (x0 - v.x) / v.w * float(w), px1 = (x1 - v.x) / v.w * float(w);
    float py0 = float(h) - (y1 - v.y) / v.h * float(h), py1 = float(h) - (y0 - v.y) / v.h * float(h);
    r[0] = std::clamp(int(std::lround(px0)), 0, w);
    r[1] = std::clamp(int(std::lround(py0)), 0, h);
    r[2] = std::max(0, std::clamp(int(std::lround(px1)), 0, w) - r[0]);
    r[3] = std::max(0, std::clamp(int(std::lround(py1)), 0, h) - r[1]);
}

}  // namespace

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

Engine::Engine(Ctx& c) : ctx_(&c) {
    if (c.exe) shadeK_ = c.exe->f64(0x41a4c8) / 65536.0;   // 2437939.2 = 37.2 * 65536
    if (!shader_.build(kVS, std::string(kTexGLSL) + kFS, "engine")) SDL_Log("engine shader failed");
    if (!composite_.build(kFullscreenVS, kCompositeFS, "engine composite")) SDL_Log("engine composite shader failed");
    shader_.use();
    shader_.set("uMap", 0);
    shader_.set("uLin", 1);
    gl::GenVertexArrays(1, &vao_);
    gl::BindVertexArray(vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei st = kFloats * 4;
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)12);
    gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, st, (void*)20);
    for (GLuint i = 0; i < 3; i++) gl::EnableVertexAttribArray(i);
    gl::BindVertexArray(0);
}

Engine::~Engine() {
    for (Buffers* b : buffers_) delete b;
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (vbo_) gl::DeleteBuffers(1, &vbo_);
}

float Engine::normalShade(float nz) const {
    double c = 1.0 - double(nz);
    return float(c * c * c * shadeK_);
}

bool Engine::toScreen(const Vec3& p, float* x, float* y) const {
    if (!(p.z > 0)) return false;
    *x = float(159.5 - 256.0 * win_.a * p.x / p.z);
    *y = float(99.5 - 256.0 * win_.b * p.y / p.z);
    return true;
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
    b->w = w;
    b->h = h;
    b->samples = s;
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

void Engine::begin(Target& out, const Window& w, const View& view) {
    out_ = &out;
    win_ = w;
    view_ = view;
    cur_ = &buffers(out.w, out.h);
    gl::BindFramebuffer(GL_FRAMEBUFFER, cur_->fbo);
    gl::Viewport(0, 0, out.w, out.h);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_CULL_FACE);   // 0x416929's test runs on the CPU
    gl::DepthMask(GL_TRUE);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::ClearColor(0, 0, 0, 0);
    gl::ClearDepth(1.0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::DepthFunc(GL_LESS);

    // column = 159.5 + kx x / z, row = 99.5 + ky y / z (VGA coordinates), mapped to the view's NDC
    double kx = -256.0 * w.a, ky = -256.0 * w.b;
    double ax = 2.0 * kx / view.w, bx = 2.0 * (159.5 - view.x) / view.w - 1.0;
    double ay = -2.0 * ky / view.h, by = 1.0 - 2.0 * (99.5 - view.y) / view.h;
    double az = (kFar + kNear) / (kFar - kNear), bz = -2.0 * kFar * kNear / (kFar - kNear);
    shader_.use();
    shader_.set("uX", float(ax), float(bx));
    shader_.set("uY", float(ay), float(by));
    shader_.set("uZ", float(az), float(bz));
}

void Engine::prepare(Tex map) {
    Textures& tx = ctx_->shared<Textures>();
    tx.get(map, style, TexForm::Log);
    tx.get(map, style, TexForm::Linear);
}

void Engine::upload(const std::vector<float>& data) {
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    size_t bytes = data.size() * sizeof(float);
    if (bytes > vboSize_) {
        vboSize_ = std::max(bytes, vboSize_ * 2);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(vboSize_), nullptr, GL_STREAM_DRAW);
    }
    gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), data.data());
}

void Engine::draw(const EngineMesh& m, float light) {
    stats = Stats{};
    stats.faces = int(m.faces.size());
    if (!cur_ || m.faces.empty()) return;
    // the shades per corner: 0x4169f8 reads n_z of the corner's normal (0x416b61, every frame) or the face's values
    const std::vector<Vec3>* nrm = nullptr;
    if (m.shade == Shade::Normals) {
        if (m.normals.size() == m.pos.size()) {
            nrm = &m.normals;
        } else {
            vertexNormals(m.pos, m.faces, normals_);
            nrm = &normals_;
        }
    }
    // 0x416929: back faces and the far cut; the drawn faces with their bucket
    sorted_.clear();
    const size_t nv = m.pos.size();
    for (size_t k = 0; k < m.faces.size(); k++) {
        const EngineFace& f = m.faces[k];
        if (f.v[0] >= nv || f.v[1] >= nv || f.v[2] >= nv) continue;
        FaceSort s = sortFace(m.pos[f.v[0]], m.pos[f.v[1]], m.pos[f.v[2]]);
        if (s.hidden) stats.hidden++;
        else if (s.cut) stats.cut++;
        if (s.drawn()) sorted_.push_back({s.key, uint32_t(k)});
    }
    stats.drawn = int(sorted_.size());
    if (sorted_.empty()) return;
    // 0x4169f8's order: buckets 4095 down to 0, within a bucket the face inserted last first
    if (order == Order::Painter)
        std::sort(sorted_.begin(), sorted_.end(), [](const Sorted& a, const Sorted& b) {
            return a.key != b.key ? a.key > b.key : a.face > b.face;
        });
    data_.clear();
    data_.reserve(sorted_.size() * 3 * kFloats);
    for (const Sorted& s : sorted_) {
        const EngineFace& f = m.faces[s.face];
        for (int i = 0; i < 3; i++) {
            const Vec3& p = m.pos[f.v[i]];
            float sh = nrm ? normalShade((*nrm)[f.v[i]].z) : f.shade[i];
            const float d[kFloats] = {p.x, p.y, p.z, f.uv[i][0], f.uv[i][1], sh};
            data_.insert(data_.end(), d, d + kFloats);
        }
    }
    upload(data_);
    const Texture* logMap = m.texture;
    const Texture* linMap = nullptr;
    if (!logMap) {
        Textures& tx = ctx_->shared<Textures>();
        logMap = &tx.get(m.map, style, TexForm::Log);
        if (!debug.logMips) {
            linMap = &tx.get(m.map, style, TexForm::Linear);
            linMap->bind(1);
        }
    }
    shader_.use();
    shader_.set("uLight", light);
    shader_.set("uHasLin", linMap ? 1 : 0);
    logMap->bind(0);
    if (order == Order::Painter) {
        gl::Disable(GL_DEPTH_TEST);
    } else {
        gl::Enable(GL_DEPTH_TEST);
    }
    gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(data_.size() / kFloats));
    gl::BindVertexArray(0);
}

void Engine::end() {
    if (!cur_) return;
    Buffers& b = *cur_;
    cur_ = nullptr;
    gl::Disable(GL_DEPTH_TEST);
    // resolve the samples, then blend the premultiplied result over the target inside the window
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, b.fbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, b.resolved.fbo);
    gl::BlitFramebuffer(0, 0, b.w, b.h, 0, 0, b.w, b.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    out_->bind();
    int r[4];
    pixelRect(view_, out_->w, out_->h, win_.x0(), win_.y0(), win_.x1(), win_.y1(), r);
    gl::Scissor(r[0], r[1], r[2], r[3]);
    gl::Enable(GL_SCISSOR_TEST);
    setBlend(Blend::Premul);
    composite_.use();
    b.resolved.bindTex(0);
    composite_.set("uTex", 0);
    drawFullscreen();
    setBlend(Blend::None);
    gl::Disable(GL_SCISSOR_TEST);
    out_ = nullptr;
}

}  // namespace cmc
