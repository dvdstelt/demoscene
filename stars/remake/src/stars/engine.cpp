// BARTi's engine, the drawing (engine.h): 0x789c4 (one object: set-up 0x78a35, transform 0x79b16, vertex normals
// 0x79a17, face list 0x78b08, per-vertex parameters 0x4d4b4 / 0x4d4e7 / 0x4da28, the draw rows of the mode table
// 0x784d1) and 0x79483 (scene lists: camera, object culling), with the fillers of the modes the parts use: 0x4e710
// (modes 2, 3 and type-1 faces of 20), 0x51593 (mode 15), 0x4f47c (mode 20), 0x52e96 / 0x54548 (mode 21 into the
// 256x256 buffer), and the present 0x78905 (rows 1..197).
//
// Drawing goes into an MSAA buffer of our own, composited premultiplied over the part's frame inside the view's clip
// rectangle, so anti-aliased edges blend with the background. Faces are ordered by a depth buffer, or as the
// original orders them (Order::Sorted: the 0x78b08 depth key and the quicksort 0x7898d; Order::File: file order;
// scene objects by their centres). The fillers' integer screen vertices, affine 8.8 interpolation, nearest texels,
// isqrt rings and 16 / 32 shade levels become: float projection, perspective-correct interpolation of the rotated
// normal and the texture coordinate, the env coordinate per pixel from the interpolated normal (direction
// renormalised, length interpolated), the light curve at the continuous radius, the level and the texels' colours
// interpolated through the palette, and a box filter over the pixel (up to 3 x 3 samples) where the texture is
// minified.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "stars/engine.h"

namespace stars {

namespace {

constexpr int kFloats = 9;  // per vertex: position 3, normal 3, uv 2, face type 1

const char* kVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aType;
uniform mat4 uMV;      // object -> the engine's camera space (x right, y down, z towards the eye)
uniform mat3 uN;       // object normal -> env space (object angles + env angles, no camera)
uniform vec4 uProj;    // clip x = p.x X + p.y w, clip y = p.z Y + p.w w, w = F - Z
uniform vec3 uDepth;   // F, clip z = d.y w + d.z
out vec3 vN;
out float vLen;               // the vertex normal's length (1 = 255 in the file; 0 for the few zero normals)
out vec2 vUV;
noperspective out vec3 vNA;   // Debug::affineEnv: screen-linear, not renormalised (the fillers' interpolation)
flat out int vType;
void main() {
    vec4 v = uMV * vec4(aPos, 1.0);
    float w = uDepth.x - v.z;
    gl_Position = vec4(uProj.x * v.x + uProj.y * w, uProj.z * v.y + uProj.w * w, uDepth.y * w + uDepth.z, w);
    vN = vNA = uN * aNrm;
    vLen = length(aNrm);
    vUV = aUV;
    vType = int(aType + 0.5);
}
)";

const char* kFS = R"(
in vec3 vN;
in float vLen;
in vec2 vUV;
noperspective in vec3 vNA;
flat in int vType;
out vec4 o;
uniform int uAffineEnv;
uniform int uMode;          // 0: env (modes 2, 3); 1: texture + bump light (15); 2: type 1 env, else bump light (20, 21)
uniform float uEnvR;        // env radius in texels: 127.5 (mode 2, n >> 11) or 63.75 (n >> 12)
uniform sampler2D uPal, uEnvRGB, uEnvLevel, uRamp, uTexA, uTexB, uBumpA, uBumpB;
uniform int uEnvKind;       // 0: true colour, 1: level map through the palette
uniform int uEnvFlip;
uniform vec2 uEnvIdx;       // level map: index = x + y * value
uniform vec3 uLevel;        // level = clamp(x * ramp(r), y, z)
uniform vec2 uIndex;        // index = x + y * level + texel
uniform int uHasTex;
uniform float uTexMix;
uniform int uHasBump;
uniform float uBumpMix;
uniform vec2 uBumpUV;       // texture texels -> bump map coordinates
uniform vec3 uBand;         // light-map rows x <= v < y add z to the index
uniform vec3 uMul, uAdd;
uniform int uSharp;         // texels as squares with anti-aliased edges instead of bilinear

// Moves a texture position p (texels) so that bilinear weights become a step over one output pixel (fw texels).
vec2 sharpen(vec2 p, vec2 fw) {
    vec2 q = p - 0.5, i = floor(q), f = q - i;
    return i + clamp((f - 0.5) / max(fw, vec2(1e-4)) + 0.5, 0.0, 1.0) + 0.5;
}

vec3 palAt(float i) { return texelFetch(uPal, ivec2(clamp(int(i), 0, 255), 0), 0).rgb; }
vec3 palLerp(float i) {
    float c = clamp(i, 0.0, 255.0), a = floor(c);
    return mix(palAt(a), palAt(a + 1.0), c - a);
}
// The colour of index base + N L + m for a fractional level L (and texel m): between the palette's entries.
vec3 palIndex(float base, float N, float L, float m) {
    float l0 = floor(L), fl = L - l0, m0 = floor(m), fm = m - m0;
    float i = base + N * l0 + m0;
    vec3 a = palAt(i), b = palAt(i + N);
    if (fm > 0.0) {
        a = mix(a, palAt(i + 1.0), fm);
        b = mix(b, palAt(i + N + 1.0), fm);
    }
    return mix(a, b, fl);
}
// 0x7873a's curve read at 127 - r, between its entries
float rampAt(float r) {
    float x = clamp(127.0 - r, 0.0, 127.0), a = floor(x);
    int k = int(a);
    return mix(texelFetch(uRamp, ivec2(k, 0), 0).r, texelFetch(uRamp, ivec2(min(k + 1, 127), 0), 0).r, x - a);
}

vec3 envColour(vec2 e, vec2 dex, vec2 dey) {
    vec2 t = e / 256.0, gx = dex / 256.0, gy = dey / 256.0;
    if (uEnvFlip != 0) { t.y = 1.0 - t.y; gx.y = -gx.y; gy.y = -gy.y; }
    if (uEnvKind == 0) return textureGrad(uEnvRGB, t, gx, gy).rgb;
    return palLerp(uEnvIdx.x + uEnvIdx.y * 255.0 * textureGrad(uEnvLevel, t, gx, gy).r);
}

vec2 bumpAt(vec2 p) {
    if (uHasBump == 0) return vec2(0.0);
    vec2 q = p * uBumpUV;
    vec2 b = textureLod(uBumpA, q, 0.0).rg;
    if (uBumpMix > 0.0) b = mix(b, textureLod(uBumpB, q, 0.0).rg, uBumpMix);
    return b;
}

float texel(ivec2 i) {
    i &= ivec2(255);
    float a = texelFetch(uTexA, i, 0).r;
    if (uTexMix > 0.0) a = mix(a, texelFetch(uTexB, i, 0).r, uTexMix);
    return a * 255.0;
}

// The colours of the four texels around p at level L (each as the original computes it), filtered.
vec3 shadeLevel(float base, float L, vec2 p) {
    if (uHasTex == 0) return palIndex(base, uIndex.y, L, 0.0);
    vec2 q = p - 0.5, f = fract(q);
    ivec2 i = ivec2(floor(q));
    vec3 c00 = palIndex(base, uIndex.y, L, texel(i));
    vec3 c10 = palIndex(base, uIndex.y, L, texel(i + ivec2(1, 0)));
    vec3 c01 = palIndex(base, uIndex.y, L, texel(i + ivec2(0, 1)));
    vec3 c11 = palIndex(base, uIndex.y, L, texel(i + ivec2(1, 1)));
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// modes 15 / 20 / 21: the light map at the env coordinate moved by the bump offsets, + the texture
vec3 lightColour(vec2 e, vec2 p, float bandW) {
    vec2 d = e + bumpAt(p);
    float L = clamp(uLevel.x * rampAt(length(d - 128.0)), uLevel.y, uLevel.z);
    vec3 c = shadeLevel(uIndex.x, L, p);
    if (uBand.z != 0.0) {
        float k = clamp((d.y - uBand.x) / bandW + 0.5, 0.0, 1.0) - clamp((d.y - uBand.y) / bandW + 0.5, 0.0, 1.0);
        if (k > 0.0) c = mix(c, shadeLevel(uIndex.x + uBand.z, L, p), k);
    }
    return c;
}

void main() {
    // the env coordinate from the interpolated normal: its direction renormalised, its length interpolated from
    // the vertices' (the original interpolates the per-vertex coordinates: a zero normal is the map's centre, a
    // morphed normal between keys is shorter)
    float l = length(vN);
    vec2 nxy = l > 1e-6 ? vN.xy * (vLen / l) : vec2(0.0);
    if (uAffineEnv != 0) nxy = vNA.xy;
    vec2 e = 128.0 + uEnvR * nxy;
    vec2 dex = dFdx(e), dey = dFdy(e);
    vec3 c;
    if (uMode == 0 || (uMode == 2 && vType == 1)) {
        c = envColour(e, dex, dey);
    } else {
        vec2 p = vUV, dpx = dFdx(p), dpy = dFdy(p);
        float dy = e.y + bumpAt(p).y;
        float bandW = max(abs(dFdx(dy)) + abs(dFdy(dy)), 1e-3);
        // a box filter over the pixel where the texture (and its bump offsets) are minified
        float fp = max(length(dpx), length(dpy));
        int k = uHasTex != 0 || uHasBump != 0 ? int(clamp(ceil(fp), 1.0, 3.0)) : 1;
        vec2 fw = (abs(dpx) + abs(dpy)) / float(k);
        c = vec3(0.0);
        for (int sy = 0; sy < k; sy++)
            for (int sx = 0; sx < k; sx++) {
                vec2 s = (vec2(sx, sy) + 0.5) / float(k) - 0.5;
                vec2 ps = p + dpx * s.x + dpy * s.y;
                if (uSharp != 0) ps = sharpen(ps, fw);
                c += lightColour(e + dex * s.x + dey * s.y, ps, bandW / float(k));
            }
        c /= float(k * k);
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

// Units of the samplers.
enum Unit { kPal, kEnvRGB, kEnvLevel, kRamp, kTexA, kTexB, kBumpA, kBumpB, kUnits };
const char* kUnitNames[kUnits] = {"uPal", "uEnvRGB", "uEnvLevel", "uRamp", "uTexA", "uTexB", "uBumpA", "uBumpB"};

void setupVao(GLuint& vao, GLuint& vbo) {
    gl::GenVertexArrays(1, &vao);
    gl::BindVertexArray(vao);
    gl::GenBuffers(1, &vbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei st = kFloats * 4;
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)12);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)24);
    gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)32);
    for (GLuint i = 0; i < 4; i++) gl::EnableVertexAttribArray(i);
}

// Pixel rectangle of a VGA rectangle in a target of w x h pixels covering the view (GL rows from the bottom).
void pixelRect(const View& v, int w, int h, float x, float y, float rw, float rh, int r[4]) {
    float x0 = (x - v.x) / v.w * float(w), x1 = (x + rw - v.x) / v.w * float(w);
    float y0 = float(h) - (y + rh - v.y) / v.h * float(h), y1 = float(h) - (y - v.y) / v.h * float(h);
    r[0] = std::clamp(int(std::lround(x0)), 0, w);
    r[1] = std::clamp(int(std::lround(y0)), 0, h);
    r[2] = std::clamp(int(std::lround(x1)), 0, w) - r[0];
    r[3] = std::clamp(int(std::lround(y1)), 0, h) - r[1];
}

}  // namespace

// =============================================================================================== object upload
void Object3D::upload() const {
    // 3 vertices per face; the texture coordinates of each face are unwrapped the short way round relative to its
    // first corner (the fillers take every 8-bit difference as -128..127)
    data_.clear();
    data_.reserve(faces.size() * 3 * kFloats);
    for (const EngineFace& f : faces) {
        const EngineVertex* c[3] = {&verts[f.a], &verts[f.b], &verts[f.c]};
        for (int k = 0; k < 3; k++) {
            const EngineVertex& v = *c[k];
            int du = ((int(v.u) - int(c[0]->u) + 128) & 255) - 128, dv = ((int(v.v) - int(c[0]->v) + 128) & 255) - 128;
            const float d[kFloats] = {v.x,  v.y,  v.z,  v.nx / 255.0f, v.ny / 255.0f, v.nz / 255.0f,
                                      float(c[0]->u + du), float(c[0]->v + dv), float(f.type)};
            data_.insert(data_.end(), d, d + kFloats);
        }
    }
    if (!vao_) setupVao(vao_, vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(data_.size() * 4), data_.data(), GL_DYNAMIC_DRAW);
    gl::BindVertexArray(0);
    count_ = int(faces.size() * 3);
    dirty_ = false;
}

// =============================================================================================== EngineTarget
EngineTarget::~EngineTarget() { destroy(); }

void EngineTarget::create(int size) {
    destroy();
    n_ = size;
    gl::GenTextures(1, &tex_);
    gl::BindTexture(GL_TEXTURE_2D, tex_);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, n_, n_, 0, GL_RGBA, GL_FLOAT, nullptr);
    gl::GenerateMipmap(GL_TEXTURE_2D);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    gl::GenFramebuffers(1, &fbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_Log("engine: %dx%d map target incomplete", n_, n_);
}

void EngineTarget::destroy() {
    if (fbo_) gl::DeleteFramebuffers(1, &fbo_);
    if (tex_) gl::DeleteTextures(1, &tex_);
    fbo_ = tex_ = 0;
    n_ = 0;
}

void EngineTarget::bind() const {
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, n_, n_);
}

void EngineTarget::fill(Ctx& c, const Picture& p, Vec3 mul) {
    bind();
    gl::Disable(GL_SCISSOR_TEST);
    setBlend(Blend::None);
    c.drawTexture(p.tex.id, 0, 0, 320, 200, 1.0f, 0, 0, 1, 1, mul);  // the whole target
}

// =============================================================================================== Engine
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
    if (!shader_.build(kVS, kFS, "engine")) SDL_Log("engine shader failed");
    if (!composite_.build(kFullscreenVS, kCompositeFS, "engine composite")) SDL_Log("engine composite shader failed");
    const uint8_t zero[4] = {0, 0, 0, 0}, white[4] = {255, 255, 255, 255};
    zero_.createRaw(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, zero, false, true, false);
    white_.createRaw(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, white, false, true, false);
    setupVao(dropVao_, dropVbo_);
    gl::BindVertexArray(0);
    shader_.use();
    for (int u = 0; u < kUnits; u++) shader_.set(kUnitNames[u], u);
}

Engine::~Engine() {
    for (Buffers* b : buffers_) delete b;
    if (dropVao_) gl::DeleteVertexArrays(1, &dropVao_);
    if (dropVbo_) gl::DeleteBuffers(1, &dropVbo_);
    zero_.destroy();
    white_.destroy();
}

Engine::Buffers& Engine::buffers(int w, int h) {
    GLint maxS = 1;
    gl::GetIntegerv(GL_MAX_SAMPLES, &maxS);
    int s = std::max(1, std::min(samples, int(maxS)));
    for (Buffers* b : buffers_)
        if (b->w == w && b->h == h && b->samples == s) return *b;
    // one set per size in use (the screen and a map); a resize drops the old screen size
    if (buffers_.size() >= 3) {
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

void Engine::begin(Target& out, const View& view, const Camera& cam) {
    target_ = nullptr;
    restore_ = nullptr;
    beginOn(out.fbo, out.w, out.h, view, cam);
}

void Engine::begin(Ctx& c, EngineTarget& t, const Camera& cam) {
    beginOn(t.fbo_, t.n_, t.n_, View::map256(), cam);
    target_ = &t;
    restore_ = c.out;
}

void Engine::beginOn(GLuint fbo, int w, int h, const View& view, const Camera& cam) {
    outFbo_ = fbo;
    outW_ = w;
    outH_ = h;
    view_ = view;
    cam_ = cam;
    camRot_ = engineRotation(cam.pose.a, cam.pose.b, cam.pose.c);
    cur_ = &buffers(w, h);
    gl::BindFramebuffer(GL_FRAMEBUFFER, cur_->fbo);
    gl::Viewport(0, 0, w, h);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_TRUE);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::ClearColor(0, 0, 0, 0);
    gl::ClearDepth(1.0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LESS);
    // 0x78b08 keeps the faces with a positive cross product on the y-down screen: clockwise as seen, which GL
    // (front = counter-clockwise in window coordinates, y up) calls back faces
    gl::Enable(GL_CULL_FACE);
    gl::CullFace(GL_FRONT);

    // projection 0x79b16 for the view: x_vga = 160 + f X / w, y_vga = 100 + f Y / w, w = F - Z
    double f = cam.focal();
    double px = 2.0 * f / view.w, qx = 2.0 * (160.0 - view.x) / view.w - 1.0;
    double py = -2.0 * f / view.h, qy = 1.0 - 2.0 * (100.0 - view.y) / view.h;
    // near plane at z = -F (0x78b08 drops faces with depth key >= -F: here clipped per pixel), far beyond the
    // original's -87891
    double zn = 2.0 * cam.F, zf = cam.F + 90000.0;
    shader_.use();
    shader_.set("uAffineEnv", debug.affineEnv ? 1 : 0);
    shader_.set("uProj", float(px), float(qx), float(py), float(qy));
    shader_.set("uDepth", float(cam.F), float((zf + zn) / (zf - zn)), float(-2.0 * zf * zn / (zf - zn)));
}

void Engine::layer() {
    if (!cur_) return;
    gl::Clear(GL_DEPTH_BUFFER_BIT);
}

void Engine::setShading(const Shading& sh) {
    auto bind = [](int unit, GLuint tex, GLuint fallback) {
        gl::ActiveTexture(GL_TEXTURE0 + GLenum(unit));
        gl::BindTexture(GL_TEXTURE_2D, tex ? tex : fallback);
    };
    int mode = sh.mode == 15 ? 1 : (sh.mode == 20 || sh.mode == 21) ? 2 : 0;
    bind(kPal, sh.palette ? sh.palette->tex.id : 0, white_.id);
    bind(kEnvRGB, sh.env.rgb, zero_.id);
    bind(kEnvLevel, sh.env.level, zero_.id);
    bind(kRamp, sh.ramp ? sh.ramp->tex.id : 0, zero_.id);
    bind(kTexA, sh.tex[0] ? sh.tex[0]->tex.id : 0, zero_.id);
    bind(kTexB, sh.tex[1] ? sh.tex[1]->tex.id : (sh.tex[0] ? sh.tex[0]->tex.id : 0), zero_.id);
    bind(kBumpA, sh.bump[0] ? sh.bump[0]->tex.id : 0, zero_.id);
    bind(kBumpB, sh.bump[1] ? sh.bump[1]->tex.id : (sh.bump[0] ? sh.bump[0]->tex.id : 0), zero_.id);
    gl::ActiveTexture(GL_TEXTURE0);
    shader_.set("uMode", mode);
    shader_.set("uEnvR", sh.mode == 2 ? 127.5f : 63.75f);  // 255 * 1024 >> 11 / >> 12
    shader_.set("uEnvKind", sh.env.rgb ? 0 : 1);
    shader_.set("uEnvFlip", sh.env.flipV ? 1 : 0);
    shader_.set("uEnvIdx", sh.env.base, sh.env.scale);
    shader_.set("uLevel", sh.levelScale, sh.levelMin, sh.levelMax);
    shader_.set("uIndex", sh.base, sh.N);
    shader_.set("uHasTex", mode == 1 && sh.tex[0] ? 1 : 0);
    shader_.set("uTexMix", sh.tex[1] ? sh.texMix : 0.0f);
    shader_.set("uHasBump", sh.bump[0] ? 1 : 0);
    shader_.set("uBumpMix", sh.bump[1] ? sh.bumpMix : 0.0f);
    shader_.set("uBumpUV", sh.bump[0] && sh.bump[0]->pairs ? 1.0f / 128.0f : 1.0f / 256.0f, 1.0f / 256.0f);
    shader_.set("uBand", sh.bandV0, sh.bandV1, sh.bandAdd);
    shader_.set("uSharp", sh.sharpTexels ? 1 : 0);
    shader_.set("uMul", sh.mul);
    shader_.set("uAdd", sh.add);
}

void Engine::drawWith(const Object3D& o, const Mat3d& rot, const Vec3& tr, const Pose& pose, const Shading& sh) {
    if (!cur_ || o.faces.empty()) return;
    if (o.dirty_) o.upload();
    // 0x78a35: vertices by M (row vectors) + translation; normals by the object's angles + the env angles
    float mv[16] = {};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) mv[i * 4 + j] = float(rot.m[i][j]);  // column i of the GL matrix = row i of M
    mv[12] = tr.x;
    mv[13] = tr.y;
    mv[14] = tr.z;
    mv[15] = 1.0f;
    Mat3d nm = engineRotation(pose.a + cam_.envA, pose.b + cam_.envB, pose.c + cam_.envC);
    float n3[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) n3[i * 3 + j] = float(nm.m[i][j]);
    shader_.use();
    gl::UniformMatrix4fv(shader_.loc("uMV"), 1, GL_FALSE, mv);
    gl::UniformMatrix3fv(shader_.loc("uN"), 1, GL_FALSE, n3);
    setShading(sh);

    if (sh.order == Order::Depth && !debug.dropNearFaces) {
        gl::BindVertexArray(o.vao_);
        gl::DrawArrays(GL_TRIANGLES, 0, o.count_);
        gl::BindVertexArray(0);
        return;
    }
    drawFaces(o, rot, tr, sh.order);
}

// 0x7898d: quicksort on the signed 16-bit low words (keys index << 16 | depth), as the original orders equal keys.
static void quicksort(std::vector<uint32_t>& a, int lo, int hi) {
    while (hi > lo) {
        int p = lo;
        uint32_t pivot = a[size_t(lo)];
        for (int i = lo + 1; i <= hi; i++)
            if (int16_t(pivot & 0xffff) > int16_t(a[size_t(i)] & 0xffff)) {
                a[size_t(p)] = a[size_t(i)];
                p++;
                a[size_t(i)] = a[size_t(p)];
                a[size_t(p)] = pivot;
            }
        quicksort(a, lo, p);
        lo = p + 1;
    }
}

// 0x78b08 on the CPU: the faces in the original's order (sorted by depth key, or the file's), optionally without the
// faces the original drops near the eye; drawn without the depth test (later faces cover earlier ones).
void Engine::drawFaces(const Object3D& o, const Mat3d& rot, const Vec3& tr, Order order) {
    std::vector<float> z(o.verts.size());
    for (size_t i = 0; i < o.verts.size(); i++) {
        const EngineVertex& v = o.verts[i];
        z[i] = float(v.x * rot.m[0][2] + v.y * rot.m[1][2] + v.z * rot.m[2][2] + tr.z);
    }
    std::vector<uint32_t> keys;
    keys.reserve(o.faces.size());
    for (size_t f = 0; f < o.faces.size(); f++) {
        const EngineFace& fc = o.faces[f];
        double key = (double(z[fc.a]) + z[fc.b]) / 4.0 + z[fc.c] / 2.0;   // weights 1/4, 1/4, 1/2
        if (debug.dropNearFaces && (key >= -cam_.F || key <= -90000000.0 / 1024.0)) continue;
        keys.push_back(uint32_t(f) << 16 | (uint32_t(int32_t(std::floor(key))) & 0xffff));
    }
    if (keys.empty()) return;
    if (order == Order::Sorted && keys.size() > 1) quicksort(keys, 0, int(keys.size()) - 1);
    std::vector<float> data;
    data.reserve(keys.size() * 3 * kFloats);
    for (uint32_t k : keys) {
        auto first = o.data_.begin() + long(size_t(k >> 16) * 3 * kFloats);
        data.insert(data.end(), first, first + 3 * kFloats);
    }
    if (order != Order::Depth) gl::Disable(GL_DEPTH_TEST);
    gl::BindVertexArray(dropVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, dropVbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.size() * 4), data.data(), GL_STREAM_DRAW);
    gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(data.size() / kFloats));
    gl::BindVertexArray(0);
    gl::Enable(GL_DEPTH_TEST);
}

void Engine::draw(const Object3D& o, const Pose& pose, const Shading& sh) {
    // 0x789c4 with [0x582cd] = 0: M = the object's matrix, translation = its (camera-space) position
    drawWith(o, engineRotation(pose.a, pose.b, pose.c), Vec3(float(pose.x), float(pose.y), float(pose.z)), pose, sh);
}

Vec3 Engine::toCamera(const Vec3& world) const {
    Vec3 d(float(world.x - cam_.pose.x), float(world.y - cam_.pose.y), float(world.z - cam_.pose.z));
    return camRot_.apply(d);
}

bool Engine::toScreen(const Vec3& p, float* x, float* y) const {
    double w = cam_.F - p.z;
    if (w <= 0) return false;
    double f = cam_.focal();
    *x = float(160.0 + f * p.x / w);
    *y = float(100.0 + f * p.y / w);
    return true;
}

bool Engine::visible(const Object3D& o, const Pose& pose) const {
    // 0x79483: camera-space centre, projected bounding square on the 320x200 screen, sphere beyond z = -F,
    // centre nearer than z = -8000
    Vec3 c = toCamera(Vec3(float(pose.x), float(pose.y), float(pose.z)));
    double r = o.radius, w = cam_.F - c.z, sx, sy, sr;
    if (w <= 0) {
        sx = c.x;
        sy = c.y;
        sr = r;
    } else {
        double f = cam_.focal();
        sx = 160.0 + f * c.x / w;
        sy = 100.0 + f * c.y / w;
        sr = f * r / w;
    }
    return sx + sr > 0 && sx - sr < 320 && sy + sr > 0 && sy - sr < 200 && c.z - r < -cam_.F && c.z > -8000.0;
}

void Engine::drawInScene(const Object3D& o, const Pose& pose, const Shading& sh) {
    if (!debug.noObjectCull && !visible(o, pose)) return;
    // 0x79483 / 0x78a35: M = object x camera, translation = (position - camera) x camera
    Mat3d obj = engineRotation(pose.a, pose.b, pose.c);
    Vec3 tr = toCamera(Vec3(float(pose.x), float(pose.y), float(pose.z)));
    drawWith(o, obj * camRot_, tr, pose, sh);
}

void Engine::drawScene(std::initializer_list<SceneObject> list, const Shading& sh) {
    drawScene(std::span<const SceneObject>(list.begin(), list.size()), sh);
}

void Engine::drawScene(std::span<const SceneObject> list, const Shading& sh) {
    if (sh.order == Order::Depth) {
        for (const SceneObject& s : list)
            if (s.object) drawInScene(*s.object, s.pose, sh);
        return;
    }
    // 0x79483: the visible objects ordered by their centres' depth (farthest first), quicksort 0x7898d
    std::vector<uint32_t> keys;
    for (size_t i = 0; i < list.size(); i++) {
        const SceneObject& s = list[i];
        if (!s.object || (!debug.noObjectCull && !visible(*s.object, s.pose))) continue;
        float z = toCamera(Vec3(float(s.pose.x), float(s.pose.y), float(s.pose.z))).z;
        keys.push_back(uint32_t(i) << 16 | (uint32_t(int32_t(std::floor(z))) & 0xffff));
    }
    if (keys.size() > 1) quicksort(keys, 0, int(keys.size()) - 1);
    Engine::Debug saved = debug;
    debug.noObjectCull = true;  // tested above
    for (uint32_t k : keys) drawInScene(*list[k >> 16].object, list[k >> 16].pose, sh);
    debug = saved;
}

void Engine::end() {
    if (!cur_) return;
    Buffers& b = *cur_;
    cur_ = nullptr;
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    // resolve the samples, then blend the premultiplied result over the target inside the clip rectangle
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, b.fbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, b.resolved.fbo);
    gl::BlitFramebuffer(0, 0, b.w, b.h, 0, 0, b.w, b.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, outFbo_);
    gl::Viewport(0, 0, outW_, outH_);
    int r[4];
    pixelRect(view_, outW_, outH_, view_.clipX, view_.clipY, view_.clipW, view_.clipH, r);
    gl::Scissor(r[0], r[1], r[2], r[3]);
    gl::Enable(GL_SCISSOR_TEST);
    setBlend(Blend::Premul);
    composite_.use();
    b.resolved.bindTex(0);
    composite_.set("uTex", 0);
    drawFullscreen();
    setBlend(Blend::None);
    gl::Disable(GL_SCISSOR_TEST);
    if (target_) {
        gl::BindTexture(GL_TEXTURE_2D, target_->tex_);
        gl::GenerateMipmap(GL_TEXTURE_2D);
        target_ = nullptr;
    }
    if (restore_) {
        restore_->bind();
        restore_ = nullptr;
    }
}

}  // namespace stars
