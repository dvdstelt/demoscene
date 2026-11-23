// The 3D engine's GL part (engine.h): the textures (TEXTURE.RAW's 16 tiles through the palette, and through every
// level of GOURAUD.PAL), the scene shader (the face types 1, 6, 8 of 0x21ec24 with the lighting of 0x21e72c /
// 0x21e7c8 as a continuous level), the painter's-order draw into a multisampled target, PART4's box lines (0x25f26e,
// 0x21d22c), the AA pass 0x2045fa and FIN's blend 0x2046c7 in colour.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "demo.h"
#include "exe.h"
#include "reve/engine.h"

namespace reve3d {

namespace {
// GL entry points / enums this file needs beyond gl.h's
constexpr GLenum kTex2DArray = GL_TEXTURE_2D_ARRAY;

// Addresses of the names the inits pass to the loaders (notes-parts3d.md)
constexpr uint32_t kTextureName = 0x205ceb;   // "GFX\TEXTURE.RAW" (0x204841: the textures, [0x21b140])
constexpr uint32_t kGouraudName = 0x205d18;   // "GFX\GOURAUD.PAL" (0x204963: the shade table, 0x21643c)
constexpr uint32_t kAntialiaName = 0x205d28;  // "GFX\ANTIALIA.PAL" (0x20494e: the AA pass's table, 0x20643c)
constexpr uint32_t kAniExt = 0x204918;        // 0x204916: mov dword [edi], ".ANI" over the WLD name's extension
constexpr int kTile = 64;                     // 0x21b148: the texel mask 0x3f3f
constexpr int kLevels = 16;                   // GOURAUD.PAL: 16 levels of 256

struct GpuVertex {
    float pos[3];
    float nrm[3];
    float uv[2];     // texels (u / 256, v / 256 of the unsigned words)
    float info[3];   // tile, type, colour
};
static_assert(sizeof(GpuVertex) == 11 * 4);

const char* kSceneVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aInfo;
uniform mat3 uRot;      // object -> camera space
uniform vec3 uTrans;
uniform vec3 uLight;    // the lighting point, object space
uniform vec2 uProj;     // focal / 160, focal / 100
uniform float uNear;    // 128
uniform vec2 uDist;     // flat, vertex: |d| per level (512, 1024)
out vec2 vUV;
out float vLev;
flat out vec3 vInfo;
void main() {
    vec3 c = uRot * aPos + uTrans;
    // x right, y up, z forward; clipped at z = uNear (clip z = z - 2 near, w = z), no far plane
    gl_Position = vec4(c.x * uProj.x, c.y * uProj.y, c.z - 2.0 * uNear, c.z);
    vUV = aUV / 64.0;
    // 0x21e7c8 / 0x21e72c: level = 15 - (16 cos - |d| / dist), clamped; flat faces are lit at the object's origin
    bool flatFace = aInfo.y < 1.5;
    vec3 d = uLight - (flatFace ? vec3(0.0) : aPos);
    float dl = length(d);
    float cs = dl > 0.0 ? dot(d, aNrm) / dl : 0.0;
    vLev = clamp(15.0 - 16.0 * cs + dl / (flatFace ? uDist.x : uDist.y), 0.0, 15.0);
    vInfo = aInfo;
}
)";

const char* kSceneFS = R"(
in vec2 vUV;
in float vLev;
flat in vec3 vInfo;
uniform sampler2DArray uPlain;   // 16 tiles: TEXTURE.RAW through the palette
uniform sampler2DArray uShade;   // layer level * 16 + tile: GOURAUD.PAL's colours of the tile
uniform sampler2D uPal;          // 256 x 1
uniform sampler2DArray uOrig;    // the original's tiles through the palette (plain[0])
uniform sampler2DArray uDetail;  // their 4x upscale
uniform int uHasDetail;
uniform vec3 uGain[17];          // per level, and 16: the plain tiles
out vec4 o;
void main() {
    float tile = floor(vInfo.x + 0.5);
    int type = int(vInfo.y + 0.5);
    float l = clamp(vLev, 0.0, 15.0);
    float l0 = min(floor(l), 14.0);
    float f = l - l0;
    vec3 c;
    if (type == 8) {
        c = mix(texture(uShade, vec3(vUV, l0 * 16.0 + tile)).rgb, texture(uShade, vec3(vUV, (l0 + 1.0) * 16.0 + tile)).rgb, f);
    } else if (type == 6 || type == 7) {
        c = texture(uPlain, vec3(vUV, tile)).rgb;
    }
    if (uHasDetail != 0 && type >= 6 && type <= 8) {
        vec3 g = type == 8 ? mix(uGain[int(l0)], uGain[int(l0) + 1], f) : uGain[16];
        c += g * (texture(uDetail, vec3(vUV, tile)).rgb - texture(uOrig, vec3(vUV, tile)).rgb);
    } else if (type < 6 || type > 8) {
        int i = int(vInfo.z + 0.5) + int(l0);
        c = mix(texelFetch(uPal, ivec2(i & 255, 0), 0).rgb, texelFetch(uPal, ivec2((i + 1) & 255, 0), 0).rgb, f);
    }
    o = vec4(c, 1.0);
}
)";

const char* kLineVS = R"(
layout(location = 0) in vec2 aPos;   // clip space
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* kLineFS = R"(
uniform vec3 uColour;
out vec4 o;
void main() { o = vec4(uColour, 1.0); }
)";

// 0x2045fa: d = T[T[up][down]][T[centre][left]] (odd columns with the pairs swapped; T is symmetric): the average of
// the four in colour
const char* kAaFS = R"(
in vec2 uv;
uniform sampler2D uSrc;
uniform vec2 uPx;   // one original pixel in uv: 1 / 320, 1 / 200
out vec4 o;
void main() {
    vec3 s = texture(uSrc, uv).rgb + texture(uSrc, uv - vec2(uPx.x, 0.0)).rgb
           + texture(uSrc, uv + vec2(0.0, uPx.y)).rgb + texture(uSrc, uv - vec2(0.0, uPx.y)).rgb;
    o = vec4(0.25 * s, 1.0);
}
)";

// One original pixel's box average along one axis (the AA pass's taps are whole pixels of the original): uStep = one
// output pixel along the axis (uv), uWidth = the box width in output pixels.
const char* kBoxFS = R"(
in vec2 uv;
uniform sampler2D uSrc;
uniform vec2 uStep;
uniform float uWidth;
out vec4 o;
void main() {
    int n = max(1, int(ceil(uWidth)));
    vec3 s = vec3(0.0);
    for (int i = 0; i < n; i++) s += texture(uSrc, uv + uStep * ((float(i) + 0.5) * uWidth / float(n) - 0.5 * uWidth)).rgb;
    o = vec4(s / float(n), 1.0);
}
)";

const char* kWeightedFS = R"(
in vec2 uv;
uniform sampler2D uSrc;
uniform float uWeight;
out vec4 o;
void main() { o = vec4(texture(uSrc, uv).rgb * uWeight, 1.0); }
)";

uint8_t to8(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

GLuint makeArray(int layers, const std::vector<uint8_t>& rgba, int size = kTile) {
    GLuint id = 0;
    gl::GenTextures(1, &id);
    gl::BindTexture(kTex2DArray, id);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage3D(kTex2DArray, 0, GL_RGBA8, size, size, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    gl::GenerateMipmap(kTex2DArray);
    gl::TexParameteri(kTex2DArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl::TexParameteri(kTex2DArray, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(kTex2DArray, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl::TexParameteri(kTex2DArray, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl::TexParameterf(kTex2DArray, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    gl::GetError();   // anisotropy is optional
    gl::BindTexture(kTex2DArray, 0);
    return id;
}

// The least-squares gain per channel of y[j] over pal[j], weighted by use[j] (how much of a change in a texel's colour
// a table row passes on; docs/redrawing-pictures.md section 8)
Vec3 colourGain(const std::array<Vec3, 256>& pal, const std::array<Vec3, 256>& y, const std::array<double, 256>& use) {
    double total = 0;
    for (double v : use) total += v;
    const double eps = 1e-3 * std::max(total, 1.0) / 256;
    auto ch = [](const Vec3& v, int k) { return double(k == 0 ? v.x : k == 1 ? v.y : v.z); };
    float g[3];
    for (int k = 0; k < 3; k++) {
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t j = 0; j < 256; j++) {
            double w = use[j] + eps, x = ch(pal[j], k), v = ch(y[j], k);
            n += w, sx += w * x, sy += w * v, sxx += w * x * x, sxy += w * x * v;
        }
        double var = sxx - sx * sx / n;
        g[k] = var > 1e-9 * n ? float(std::clamp((sxy - sx * sy / n) / var, 0.0, 2.0)) : 1.0f;
    }
    return Vec3(g[0], g[1], g[2]);
}

// The tile a face's texture byte selects: origin (16 (t & 15), 16 (t >> 4)) in TEXTURE.RAW, which in every file is
// one of the 16 aligned 64 x 64 tiles
int tileOf(uint8_t t) { return ((t >> 6) << 2) | ((t & 15) >> 2); }
}  // namespace

// ---------------------------------------------------------------------------------------------- shared resources
Gpu::Gpu(Ctx& c) {
    k.read(*c.exe);
    RawImage tex = rawImage(*c.exe, c.exe->cstr(kTextureName));   // 256 x 256 indices and the palette
    std::span<const uint8_t> gour = c.exe->file(c.exe->cstr(kGouraudName));
    std::span<const uint8_t> T = c.exe->file(c.exe->cstr(kAntialiaName));
    for (int set = 0; set < 2; set++) {
        // the colour of each index: the palette's, or after the AA pass over an area of it
        for (int i = 0; i < 256; i++) {
            int j = i;
            if (set == 1 && T.size() >= 65536) {
                int t = T[size_t(i) * 256 + i];
                j = T[size_t(t) * 256 + t];
            }
            pal[set][i] = tex.color(j);
        }
        auto put = [&](std::vector<uint8_t>& out, size_t at, int index) {
            const Vec3& v = pal[set][index & 255];
            out[at] = to8(v.x);
            out[at + 1] = to8(v.y);
            out[at + 2] = to8(v.z);
            out[at + 3] = 255;
        };
        const size_t layer = size_t(kTile) * kTile * 4;
        std::vector<uint8_t> plainPx(16 * layer), shadePx(size_t(kLevels) * 16 * layer);
        for (int t = 0; t < 16; t++)
            for (int y = 0; y < kTile; y++)
                for (int x = 0; x < kTile; x++) {
                    int texel = tex.px[size_t((t >> 2) * kTile + y) * tex.w + (t & 3) * kTile + x];
                    size_t at = (size_t(y) * kTile + x) * 4;
                    put(plainPx, t * layer + at, texel);
                    for (int l = 0; l < kLevels; l++) {
                        size_t g = size_t(l) * 256 + texel;
                        put(shadePx, size_t(l * 16 + t) * layer + at, g < gour.size() ? gour[g] : 0);
                    }
                }
        plain[set] = makeArray(16, plainPx);
        shade[set] = makeArray(kLevels * 16, shadePx);
        std::vector<uint8_t> p(256 * 4);
        for (int i = 0; i < 256; i++) put(p, size_t(i) * 4, i);
        palette[set].createRaw(256, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, p.data(), false, false, false);
    }
    // the detail: the upscale's tiles, and the gains (weighted by how often the textures use each index)
    std::array<double, 256> use{};
    for (uint8_t i : tex.px) use[i] += 1.0;
    for (int set = 0; set < 2; set++) {
        std::array<Vec3, 256> y;
        for (int l = 0; l < kLevels; l++) {
            for (int j = 0; j < 256; j++) {
                size_t gi = size_t(l) * 256 + size_t(j);
                y[size_t(j)] = pal[set][gi < gour.size() ? gour[gi] : 0];
            }
            gain[set][size_t(l)] = colourGain(pal[0], y, use);
        }
        gain[set][16] = colourGain(pal[0], pal[set], use);
    }
    const Image* up = c.pictures ? c.pictures->detail(detailName(c.exe->cstr(kTextureName))) : nullptr;
    if (up && up->w == 4 * tex.w && up->h == 4 * tex.h && tex.w == 4 * kTile) {
        const int n = 4 * kTile;   // a tile of the upscale
        std::vector<uint8_t> px(size_t(16) * n * n * 4);
        for (int t = 0; t < 16; t++)
            for (int y2 = 0; y2 < n; y2++)
                std::memcpy(&px[(size_t(t) * n + y2) * n * 4],
                            &up->rgba[(size_t((t >> 2) * n + y2) * up->w + size_t((t & 3) * n)) * 4], size_t(n) * 4);
        detail = makeArray(16, px, n);
    }
    scene.build(kSceneVS, kSceneFS, "reve3d scene");
    line.build(kLineVS, kLineFS, "reve3d line");
    aa.build(kFullscreenVS, kAaFS, "reve3d aa");
    box.build(kFullscreenVS, kBoxFS, "reve3d box");
    weighted.build(kFullscreenVS, kWeightedFS, "reve3d weighted");
}

Gpu::~Gpu() {
    if (detail) gl::DeleteTextures(1, &detail);
    for (int i = 0; i < 2; i++) {
        if (plain[i]) gl::DeleteTextures(1, &plain[i]);
        if (shade[i]) gl::DeleteTextures(1, &shade[i]);
        palette[i].destroy();
    }
    if (msFbo) gl::DeleteFramebuffers(1, &msFbo);
    if (msColor) gl::DeleteRenderbuffers(1, &msColor);
}

bool Gpu::bindMs(int w, int h) {
    if (maxSamples < 0) {
        GLint m = 0;
        gl::GetIntegerv(GL_MAX_SAMPLES, &m);
        maxSamples = m;
    }
    int want = std::min(4, maxSamples);
    if (want <= 1) return false;
    if (!msFbo || msW != w || msH != h || samples != want) {
        if (!msFbo) {
            gl::GenFramebuffers(1, &msFbo);
            gl::GenRenderbuffers(1, &msColor);
        }
        gl::BindRenderbuffer(GL_RENDERBUFFER, msColor);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_RGBA8, w, h);   // as kTargetFormat
        gl::BindFramebuffer(GL_FRAMEBUFFER, msFbo);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("reve3d: %d-sample target %dx%d incomplete, drawing without MSAA", want, w, h);
            maxSamples = 0;
            return false;
        }
        msW = w;
        msH = h;
        samples = want;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, msFbo);
    gl::Viewport(0, 0, w, h);
    return true;
}

// ---------------------------------------------------------------------------------------------- a world
void Scene3D::init(Ctx& c, uint32_t wldName, bool aaColours, bool part4) {
    part4_ = part4;
    set_ = aaColours ? 1 : 0;
    c.shared<Gpu>();
    std::string name = c.exe->cstr(wldName);
    std::string err;
    if (!world_.load(c.exe->file(name), &err)) SDL_Log("%s: %s", name.c_str(), err.c_str());
    // the inits replace the extension in place: the dword at 0x204918 (".ANI") over the name's '.'
    std::string ani = name.substr(0, name.find('.')) + std::string(reinterpret_cast<const char*>(c.exe->at(kAniExt, 4)), 4);
    if (!anim_.load(c.exe->file(ani), world_, &err)) SDL_Log("%s: %s", ani.c_str(), err.c_str());
    upload();
}

void Scene3D::upload() {
    std::vector<GpuVertex> v;
    first_.clear();
    for (const Object& o : world_.objects) {
        first_.push_back(uint32_t(v.size()));
        for (const Face& f : o.faces) {
            const bool flat = f.type < 2;
            for (int k = 0; k < 3; k++) {
                const Vertex& p = o.verts[f.v[k]];
                const int16_t* n = flat ? f.n : p.n;
                v.push_back({{float(p.x), float(p.y), float(p.z)},
                             {n[0] / 32767.0f, n[1] / 32767.0f, n[2] / 32767.0f},
                             {p.u / 256.0f, p.v / 256.0f},
                             {float(tileOf(f.tex)), float(f.type), float(f.colour)}});
            }
        }
    }
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::GenBuffers(1, &ibo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(GpuVertex)), v.data(), GL_STATIC_DRAW);
    const GLsizei st = sizeof(GpuVertex);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, pos));
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, nrm));
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, uv));
    gl::VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, info));
    for (GLuint i = 0; i < 4; i++) gl::EnableVertexAttribArray(i);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    gl::BindVertexArray(0);
    gl::GenVertexArrays(1, &lineVao_);
    gl::GenBuffers(1, &lineVbo_);
    gl::BindVertexArray(lineVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    gl::EnableVertexAttribArray(0);
    gl::BindVertexArray(0);
}

void Scene3D::render(Ctx& c, double n, Target& t, bool msaa) {
    Gpu& g = c.shared<Gpu>();
    const State s = anim_.at(n);
    const Frame f = setupFrame(world_, s, g.k, part4_);
    // one index buffer: the drawn objects back to front, each object's faces back to front
    idx_.clear();
    std::vector<std::pair<size_t, size_t>> ranges;
    for (int i : f.drawOrder) {
        size_t at = idx_.size();
        for (uint32_t j : f.objects[size_t(i)].order)
            for (uint32_t k = 0; k < 3; k++) idx_.push_back(first_[size_t(i)] + 3 * j + k);
        ranges.push_back({at, idx_.size() - at});
    }
    const bool ms = msaa && g.bindMs(t.w, t.h);
    if (!ms) t.bind();
    const Vec3 bg = g.pal[set_][0];
    gl::ClearColor(bg.x, bg.y, bg.z, 1.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    // front faces run counter-clockwise on the y-down screen (0x21eb00); the y flip to GL's window space keeps them
    // counter-clockwise there: GL's front faces
    gl::Enable(GL_CULL_FACE);
    gl::CullFace(GL_BACK);
    if (!idx_.empty()) {
        Shader& sh = g.scene;
        sh.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(kTex2DArray, g.plain[set_]);
        gl::ActiveTexture(GL_TEXTURE1);
        gl::BindTexture(kTex2DArray, g.shade[set_]);
        g.palette[set_].bind(2);
        gl::ActiveTexture(GL_TEXTURE3);
        gl::BindTexture(kTex2DArray, g.plain[0]);
        gl::ActiveTexture(GL_TEXTURE0 + 4);
        gl::BindTexture(kTex2DArray, g.detail);
        sh.set("uPlain", 0);
        sh.set("uShade", 1);
        sh.set("uPal", 2);
        sh.set("uOrig", 3);
        sh.set("uDetail", 4);
        sh.set("uHasDetail", g.detail ? 1 : 0);
        std::vector<float> gains;
        for (const Vec3& v : g.gain[set_]) gains.insert(gains.end(), {v.x, v.y, v.z});
        gl::Uniform3fv(sh.loc("uGain"), 17, gains.data());
        sh.set("uProj", float(g.k.focal / g.k.cx), float(g.k.focal / g.k.cy));
        sh.set("uNear", float(g.k.nearZ));
        sh.set("uDist", float(g.k.flatDist), float(g.k.vertexDist));
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx_.size() * 4), idx_.data(), GL_STREAM_DRAW);
        for (size_t k = 0; k < f.drawOrder.size(); k++) {
            const ObjectView& v = f.objects[size_t(f.drawOrder[k])];
            float m[9];
            for (int r = 0; r < 3; r++)
                for (int col = 0; col < 3; col++) m[col * 3 + r] = float(v.rot.m[r][col]);   // column major
            gl::UniformMatrix3fv(sh.loc("uRot"), 1, GL_FALSE, m);
            sh.set("uTrans", float(v.trans[0]), float(v.trans[1]), float(v.trans[2]));
            sh.set("uLight", float(v.light[0]), float(v.light[1]), float(v.light[2]));
            gl::DrawElements(GL_TRIANGLES, GLsizei(ranges[k].second), GL_UNSIGNED_INT,
                             (void*)(ranges[k].first * sizeof(uint32_t)));
        }
        gl::BindVertexArray(0);
        for (GLenum u : {GL_TEXTURE0 + 4, GL_TEXTURE3, GL_TEXTURE1}) {
            gl::ActiveTexture(u);
            gl::BindTexture(kTex2DArray, 0);
        }
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(kTex2DArray, 0);
    }
    gl::Disable(GL_CULL_FACE);
    if (part4_) drawBoxes(c, f);
    if (ms) {   // resolve into t
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, g.msFbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, t.fbo);
        gl::BlitFramebuffer(0, 0, t.w, t.h, 0, 0, t.w, t.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    t.bind();
}

// 0x25f26e / 0x25f139 / 0x21d22c: the 12 edges of every object's (rotated) box when its corner 7 is in front, in
// colour 0x80, after the objects. The original clips the lines to the screen and draws edges to corners behind the
// camera to positions left from earlier frames; here the edges are clipped at the near plane. One original pixel
// wide, anti-aliased by the multisampling.
void Scene3D::drawBoxes(Ctx& c, const Frame& f) {
    Gpu& g = c.shared<Gpu>();
    static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {1, 3}, {0, 2}, {4, 5}, {6, 7},
                                          {5, 7}, {4, 6}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    const double W = c.w, H = c.h, zn = g.k.nearZ;
    const double halfW = 0.5 * W / (2 * g.k.cx);   // half an original pixel, in output pixels
    std::vector<float> q;
    for (const ObjectView& v : f.objects) {
        if (!v.box) continue;
        for (const auto& e : kEdges) {
            V3 a = v.boxCam[size_t(e[0])], b = v.boxCam[size_t(e[1])];
            if (a[2] <= zn && b[2] <= zn) continue;
            if (a[2] < zn || b[2] < zn) {
                V3& p = a[2] < zn ? a : b;
                const V3& o = a[2] < zn ? b : a;
                double t = (zn - p[2]) / (o[2] - p[2]);
                for (int i = 0; i < 3; i++) p[i] += t * (o[i] - p[i]);
            }
            // output pixels (y down)
            auto px = [&](const V3& p) {
                double sx = g.k.cx + g.k.focal * p[0] / p[2], sy = g.k.cy - g.k.focal * p[1] / p[2];
                return std::array<double, 2>{sx * W / (2 * g.k.cx), sy * H / (2 * g.k.cy)};
            };
            auto p0 = px(a), p1 = px(b);
            double dx = p1[0] - p0[0], dy = p1[1] - p0[1], l = std::hypot(dx, dy);
            if (l < 1e-6) continue;
            double nx = -dy / l * halfW, ny = dx / l * halfW;
            auto clip = [&](double x, double y) {
                q.push_back(float(x / W * 2 - 1));
                q.push_back(float(1 - y / H * 2));
            };
            clip(p0[0] + nx, p0[1] + ny), clip(p0[0] - nx, p0[1] - ny), clip(p1[0] + nx, p1[1] + ny);
            clip(p1[0] + nx, p1[1] + ny), clip(p0[0] - nx, p0[1] - ny), clip(p1[0] - nx, p1[1] - ny);
        }
    }
    if (q.empty()) return;
    g.line.use();
    g.line.set("uColour", g.pal[set_][size_t(g.k.boxColour)]);
    gl::BindVertexArray(lineVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(q.size() * sizeof(float)), q.data(), GL_STREAM_DRAW);
    gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(q.size() / 2));
    gl::BindVertexArray(0);
}

void aaPass(Ctx& c, const Target& src) {
    Gpu& g = c.shared<Gpu>();
    Target* out = c.out;
    setBlend(Blend::None);
    // the taps are pixels of the original: box-average the source over one original pixel (separable), then the
    // four taps one original pixel apart (at 320 x 200 output: exactly the original's kernel)
    auto h = c.temp(false, 1.0f, kTargetFormat), v = c.temp(false, 1.0f, kTargetFormat);
    const float bw = float(src.w / (2 * g.k.cx)), bh = float(src.h / (2 * g.k.cy));
    g.box.use();
    g.box.set("uSrc", 0);
    h->bind();
    src.bindTex(0);
    g.box.set("uStep", 1.0f / float(src.w), 0.0f);
    g.box.set("uWidth", bw);
    drawFullscreen();
    v->bind();
    h->bindTex(0);
    g.box.set("uStep", 0.0f, 1.0f / float(src.h));
    g.box.set("uWidth", bh);
    drawFullscreen();
    out->bind();
    g.aa.use();
    v->bindTex(0);
    g.aa.set("uSrc", 0);
    g.aa.set("uPx", float(1.0 / (2 * g.k.cx)), float(1.0 / (2 * g.k.cy)));
    drawFullscreen();
}

void addWeighted(Ctx& c, const Target& src, float weight) {
    Gpu& g = c.shared<Gpu>();
    setBlend(Blend::Add);
    g.weighted.use();
    src.bindTex(0);
    g.weighted.set("uSrc", 0);
    g.weighted.set("uWeight", weight);
    drawFullscreen();
    setBlend(Blend::None);
}

}  // namespace reve3d
