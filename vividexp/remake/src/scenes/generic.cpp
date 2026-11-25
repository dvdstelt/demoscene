// The generic 3D scenes of part II (vividexp/re/notes-generic.md): "LANDSCAPE" (gen/gen.ini: a bump-environment-
// mapped terrain turning under the camera along a spline) and the second scene (gen2/gen.ini: ten blobs with Phong
// texture bump, floating environment bump and environment bump shading along a spline over a sunset photo).
// Ports GenericInit 0x408cec (the per-object maps 0x4077d4, the bump gradient table 0x4074e4, the Phong map
// 0x413a60) and GenericEffect 0x409d74 with the engine it calls: the spline 0x413870 / 0x413b90, the rotations
// 0x413d98 / 0x413e6a / 0x413f37, the environment coordinates 0x414160, the projection 0x414000, the back-face test
// 0x4140e0, the near test in 0x409d74, the object sort 0x415480 and the drawers 0x40b060 (type 9), 0x40b218
// (type 10), 0x40b3dc (type 11) with their rasterizers 0x414f13 / 0x418313. The files are read by generic_files.h.
//
// Everything comes from VIVID.EXE at run time: the ini named by the segment's GenericInit line, the objects, splines,
// maps, light table and background it names (key names read by address), the bump table's constants (0x820734 and
// the 100.0 pushed at 0x407f03), the projection's immediates (0x409fdd..0x409ff1) and the near limit (0x40a0f9).
//
// What is evaluated exactly instead of the original's shortcuts:
//   - the spline in doubles (a natural cubic spline, continuous in time) instead of 16.16 fixed point truncated to
//     integers; angles through sin / cos instead of the 1,025-entry table; the rotation as a float matrix;
//   - the projection with float vertex positions (and the focal lengths 0xd8f0 * 7000 / 2^20, 0xb4c6 * 7000 / 2^20
//     without truncation); perspective-correct interpolation and anti-aliased edges (multisampling) instead of
//     affine 8.8 spans with one gradient per triangle; a depth buffer per object instead of the face sort (the same
//     visible face to a few pixels per frame, notes 6); the objects still drawn one over the other, farthest first;
//   - the environment / Phong coordinates per pixel from the interpolated, normalised normal:
//     128 + (256 / pi) asin(n) texels, instead of the asin table indexed by n >> 6 and interpolated linearly;
//   - the bump offsets: the table of forward differences of the height picture (row and column 255 zero, as the
//     init leaves them) is read bilinearly and turned into atan(dh / 100) * 256 / 3.14 environment texels per pixel,
//     instead of the truncated offsets of the nearest bump texel;
//   - the maps (environment, texture) read bilinearly in colour: each tap's index through the palette, then blended;
//     the Phong map evaluated continuously from its formula (0x413a60: amb + diff c + spec c^(exp + 1),
//     c = cos(pi / 2 r / 128)), and the light table blended between the shade rows on either side;
//   - the sunset photo drawn from its indices through the palette with a bicubic filter in colour (or its redraw).
// Kept: the near rule (a face with a vertex at depth <= 280 is dropped, not clipped: LANDSCAPE's low pass shows the
// hills behind), back-face culling, the per-vertex mapping coordinates without unwrapping (the "zip" bands of the
// terrain), the transposed environment read of type 10, the wrap of every map at 256 texels.
// Dropped: page timing (the black refreshes before the first page, the frame rate and the display lag of 2 to 6
// refreshes), the second scene's frame with a black background, objects reading the clock one refresh apart.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "demo.h"
#include "exe.h"
#include "scenes/generic_files.h"

namespace {

// ---------------------------------------------------------------- addresses
// key names (GenericInit 0x408cec and the map set-up 0x4077d4): "Common<Key>" and "%s<Key>"
constexpr uint32_t kKeyUseSplineLooping = 0x820b4a, kKeyNumberOfObjects = 0x820b5b, kKeyObjectName = 0x820bba;
constexpr uint32_t kKeyUseBackgroundPic = 0x820ec3, kKeyBackgroundPic = 0x820ed4, kKeyBackgroundColor = 0x820f42;
struct ObjKey { uint32_t common, fmt; };
constexpr ObjKey kShadingType{0x820bf0, 0x820c1a}, kSplineSpeed{0x820c53, 0x820c65}, kSplinePath{0x820c8b, 0x820cf6};
constexpr ObjKey kSplineStartPos{0x820d70, 0x820db7}, kObject{0x820e02, 0x820e6d};
constexpr ObjKey kTextureMap{0x82079e, 0x8207af}, kEnvironmentMap{0x8207c9, 0x8207de}, kBumpMap{0x8207fc, 0x82080a};
constexpr ObjKey kTextureLite{0x820821, 0x820833};
constexpr ObjKey kPhongAmb{0x8209dc, 0x8209eb}, kPhongDiff{0x820a03, 0x820a13}, kPhongSpec{0x820a2c, 0x820a3c};
constexpr ObjKey kPhongExp{0x820a55, 0x820a64}, kPhongShades{0x820a7c, 0x820a8e};
constexpr uint32_t kSplLength = 0x821aa0;    // "@length" (0x4133f8)
// the bump gradient table 0x4074e4: atan(dh * (1 / 100.0)) * 81.528... (256 / 3.14)
constexpr uint32_t kBumpScale = 0x820734;
constexpr uint32_t kBumpDivHi = 0x407f04, kBumpDivLo = 0x407f0c;   // push 0x40590000, push 0: the double 100.0
// GenericEffect's call of the projection 0x414000 and its near test
constexpr uint32_t kCamZ = 0x409fde, kScaleY = 0x409fe3, kCentreY = 0x409fe8, kScaleX = 0x409fed, kCentreX = 0x409ff2;
constexpr uint32_t kNearZ = 0x40a0fb;        // cmp ecx, -6720: hidden if z + Z <= -6720

constexpr int kTypeEnvBump = 9, kTypeFloatBump = 10, kTypePhongBump = 11;
constexpr int kSamples = 4;                  // multisampling
constexpr int kMaxSubsamples = 4;            // shader samples per axis where a pixel covers several bump texels

// ---------------------------------------------------------------- shaders
const char* kObjectVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
uniform mat3 rot;        // the object's rotation Rz(C) Rx(A) Ry(B)
uniform vec3 trans;      // its position (X, Y, Z)
uniform vec4 proj;       // fx / 160, fy / 100, (cx - 160) / 160, (100 - cy) / 100
uniform vec3 depth;      // camera z, depth mapping a, b
out vec3 vN;
out vec2 vUv;
void main() {
    vec3 p = rot * aPos + trans;
    float d = p.z - depth.x;                       // 7000 + z: the distance from the eye along +z
    gl_Position = vec4(proj.x * p.x + proj.z * d, proj.y * p.y + proj.w * d, depth.y * d + depth.z, d);
    vN = rot * aNrm;
    vUv = aUv;
}
)";

const char* kObjectFS = R"(
in vec3 vN;
in vec2 vUv;
out vec4 o;
uniform int type;              // 9 environment bump, 10 floating environment bump, 11 Phong texture bump
uniform usampler2D envTex;     // 256 x 256 environment map indices
uniform sampler2D slopeTex;    // 256 x 256 forward differences of the bump heights (x, y), linear, repeat
uniform usampler2D texTex;     // type 11: 256 x 256 texture indices
uniform usampler2D litTex;     // type 11: light table, row = shade, column = texel index
uniform sampler2D pal;         // the DAC
uniform vec2 bumpK;            // 1 / 100, 256 / 3.14
uniform vec4 phong;            // amb, diff, spec (fractions of 65536), shades - 1
uniform int phongPow;          // the power of c in the specular term
uniform int maxSamples;        // per axis, where a pixel covers several bump texels

// the bump offset (environment texels) at bump coordinate p (texel n covers [n, n + 1))
vec2 bumpOffset(vec2 p) {
    vec2 f = texture(slopeTex, p / 256.0).xy;
    return atan(f * bumpK.x) * bumpK.y;
}

vec3 envTap(ivec2 t) { return palColor(pal, texelFetch(envTex, t & 255, 0).r); }
// the environment map at (column, row) q, wrapping, filtered in colour
vec3 envAt(vec2 q) {
    vec2 p = q - 0.5;
    vec2 f = floor(p);
    vec2 w = p - f;
    ivec2 i = ivec2(f);
    return mix(mix(envTap(i), envTap(i + ivec2(1, 0)), w.x), mix(envTap(i + ivec2(0, 1)), envTap(i + ivec2(1, 1)), w.x),
               w.y);
}

// the Phong map 0x413a60 at (column, row) q: its centre at 128, radius 128
float shadeAt(vec2 q) {
    vec2 d = mod(q, 256.0) - 128.0;               // the map wraps at 256 texels
    float r = length(d);
    float v = phong.x;
    if (r <= 128.0) {
        float c = clamp(cos(1.5707963 * r / 128.0), 0.0, 1.0);
        v += c * phong.y + (phongPow == 0 ? 1.0 : pow(c, float(phongPow))) * phong.z;
    }
    return v * phong.w;
}

// texel t of the texture lit by shade rows r0 / r1 blended by fs
vec3 litTap(ivec2 t, int r0, int r1, float fs) {
    int tex = int(texelFetch(texTex, t & 255, 0).r);
    return mix(palColor(pal, texelFetch(litTex, ivec2(tex, r0), 0).r), palColor(pal, texelFetch(litTex, ivec2(tex, r1), 0).r),
               fs);
}

// 0x414160: the environment / Phong map coordinates E[nx], E[ny] of a normal, in texels
vec2 envCoord(vec3 nv) { return 128.0 + (256.0 / PI) * asin(clamp(normalize(nv).xy, -1.0, 1.0)); }

// the colour at mapping coordinate uv with the (unnormalised) normal nv: the drawers' inner loops
vec3 colourAt(vec2 uv, vec3 nv) {
    vec2 e = envCoord(nv);
    if (type == 9) return envAt(e + bumpOffset(uv));
    if (type == 10) {
        vec2 b = bumpOffset(e);
        return envAt(vec2(e.y + b.x, e.x + b.y));                     // row E(nx) + dy, column E(ny) + dx
    }
    float s = clamp(shadeAt(e + bumpOffset(uv)), 0.0, phong.w);
    float s0 = floor(s);
    int r0 = int(s0), r1 = min(r0 + 1, int(phong.w));
    float fs = s - s0;
    vec2 p = uv - 0.5;
    vec2 f = floor(p);
    vec2 w = p - f;
    ivec2 i = ivec2(f);
    return mix(mix(litTap(i, r0, r1, fs), litTap(i + ivec2(1, 0), r0, r1, fs), w.x),
               mix(litTap(i + ivec2(0, 1), r0, r1, fs), litTap(i + ivec2(1, 1), r0, r1, fs), w.x), w.y);
}

void main() {
    // where one pixel covers more than a texel of the bump map (far terrain, the squeezed "zip" faces, small
    // objects), average a grid of samples over the pixel instead of aliasing
    vec2 uvx = dFdx(vUv), uvy = dFdy(vUv);
    vec3 nx = dFdx(vN), ny = dFdy(vN);
    vec2 e = envCoord(vN);
    vec2 fp = type == 10 ? vec2(length(dFdx(e)), length(dFdy(e))) : vec2(length(uvx), length(uvy));
    ivec2 k = clamp(ivec2(ceil(fp - 0.25)), ivec2(1), ivec2(maxSamples));
    vec3 col = vec3(0.0);
    for (int j = 0; j < k.y; ++j)
        for (int i = 0; i < k.x; ++i) {
            vec2 d = (vec2(i, j) + 0.5) / vec2(k) - 0.5;
            col += colourAt(vUv + uvx * d.x + uvy * d.y, vN + nx * d.x + ny * d.y);
        }
    o = vec4(col / float(k.x * k.y), 1.0);
}
)";

// a picture's indices through the palette, one fragment per original pixel (rendered into a 320 x 200 target,
// GL row j = picture row 199 - j)
const char* kPictureFS = R"(
in vec2 uv;
out vec4 o;
uniform usampler2D pic;
uniform sampler2D pal;
void main() {
    ivec2 s = textureSize(pic, 0);
    ivec2 t = ivec2(gl_FragCoord.xy);
    o = vec4(palColor(pal, idxClamp(pic, ivec2(t.x, s.y - 1 - t.y))), 1.0);
}
)";

// the background over the whole screen: the picture's colours bicubically, or the redraw (row 0 at its top)
const char* kBackgroundFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D rgb;
uniform int redraw;
void main() {
    vec3 c = redraw != 0 ? texture(rgb, vec2(uv.x, 1.0 - uv.y)).rgb : texBicubic(rgb, uv).rgb;
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// ---------------------------------------------------------------- the scene's data

// The maps an object reads (0x4077d4), shared between objects with the same files.
struct Maps {
    Texture env, slope, tex, lit;
};

struct Object {
    int type = 0;
    std::vector<generic::Spline> path;      // X, Y, Z, A, B, C
    int keys = 0;
    double speed = 0;                       // 16.16 keys per refresh: 0x10000 / SplineSpeed (integer division)
    double start = 0, max = 0;              // 16.16 keys
    const generic::Mesh3d* mesh = nullptr;
    const Maps* maps = nullptr;
    float amb = 0, diff = 0, spec = 0;      // Phong map, fractions of 65536
    int shades = 0, power = 1;
    GLuint vao = 0, vbo = 0, ibo = 0;
};

struct Setup {
    bool looping = false;
    bool usePic = false;
    std::string pic;                        // BackgroundPic
    int bgColour = 0;
    std::vector<Object> objs;
    std::map<std::string, std::unique_ptr<generic::Mesh3d>> meshes;
    std::map<std::string, std::unique_ptr<Maps>> maps;
    ~Setup() {
        for (Object& o : objs) {
            if (o.vao) gl::DeleteVertexArrays(1, &o.vao);
            if (o.vbo) gl::DeleteBuffers(1, &o.vbo);
            if (o.ibo) gl::DeleteBuffers(1, &o.ibo);
        }
        for (auto& [k, m] : maps) { m->env.destroy(); m->slope.destroy(); m->tex.destroy(); m->lit.destroy(); }
    }
};

// The first 65,536 pixels of a picture as a 256 x 256 map (0x4077d4).
std::vector<uint8_t> mapPixels(const vivid::Indexed& p) {
    if (p.px.size() < 65536) throw std::runtime_error("map smaller than 256 x 256");
    return {p.px.begin(), p.px.begin() + 65536};
}

// The bump gradient table 0x4074e4 before its atan: forward differences of the heights in x and y, row and column
// 255 left zero.
Texture slopeTexture(const std::vector<uint8_t>& h) {
    std::vector<float> d(65536 * 2, 0.0f);
    for (int y = 0; y < 255; ++y)
        for (int x = 0; x < 255; ++x) {
            const int p = y * 256 + x;
            d[size_t(p) * 2] = float(int(h[size_t(p + 1)]) - int(h[size_t(p)]));
            d[size_t(p) * 2 + 1] = float(int(h[size_t(p + 256)]) - int(h[size_t(p)]));
        }
    Texture t;
    t.createRaw(256, 256, GL_RG16F, GL_RG, GL_FLOAT, d.data(), true, true, false);
    return t;
}

void uploadMesh(Object& o) {
    gl::GenVertexArrays(1, &o.vao);
    gl::GenBuffers(1, &o.vbo);
    gl::GenBuffers(1, &o.ibo);
    gl::BindVertexArray(o.vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, o.vbo);
    const auto& v = o.mesh->verts;
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(v[0])), v.data(), GL_STATIC_DRAW);
    const GLsizei st = sizeof(generic::Mesh3d::Vertex);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)12);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)24);
    for (GLuint i = 0; i < 3; ++i) gl::EnableVertexAttribArray(i);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, o.ibo);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(o.mesh->faces.size() * 4), nullptr, GL_STREAM_DRAW);
    gl::BindVertexArray(0);
}

// GenericInit 0x408cec for the ini `iniName`.
std::unique_ptr<Setup> loadSetup(Ctx& c, const std::string& iniName) {
    const VividExe& exe = *c.exe;
    using generic::keyName;
    auto s = std::make_unique<Setup>();
    const generic::Ini ini(exe.file(iniName));
    s->looping = ini.getInt(keyName(exe, kKeyUseSplineLooping), 0) != 0;
    const int n = ini.getInt(keyName(exe, kKeyNumberOfObjects), 0);
    if (n <= 0) throw std::runtime_error("no objects");
    s->usePic = ini.getInt(keyName(exe, kKeyUseBackgroundPic), 0) != 0;
    if (s->usePic) {
        auto pic = ini.get(keyName(exe, kKeyBackgroundPic));
        if (!pic) throw std::runtime_error("missing " + keyName(exe, kKeyBackgroundPic));
        s->pic = *pic;
        c.pictures->indices(s->pic);   // decoded and uploaded now
    }
    s->bgColour = ini.getInt(keyName(exe, kKeyBackgroundColor), 0) & 255;

    const std::string lengthKey = exe.cstr(kSplLength);
    std::map<std::string, generic::SplineFile> splines;
    for (int i = 0; i < n; ++i) {
        auto name = ini.get(keyName(exe, kKeyObjectName, std::to_string(i)));
        if (!name) throw std::runtime_error("missing " + keyName(exe, kKeyObjectName, std::to_string(i)));
        auto key = [&](ObjKey k) { return generic::requireKey(exe, ini, k.common, k.fmt, *name); };
        Object o;
        o.type = std::atoi(key(kShadingType).c_str());
        if (o.type != kTypeEnvBump && o.type != kTypeFloatBump && o.type != kTypePhongBump)
            throw std::runtime_error("shading type " + std::to_string(o.type) + " is not ported");
        const int sp = std::atoi(key(kSplineSpeed).c_str());
        o.speed = sp ? double(0x10000 / sp) : 0.0;
        const std::string path = key(kSplinePath);
        if (!splines.count(path)) splines[path] = generic::readSpl(exe.file(path), lengthKey);
        const generic::SplineFile& sf = splines[path];
        if (sf.cols.size() < 6) throw std::runtime_error(path + ": fewer than 6 columns");
        for (int col = 0; col < 6; ++col) o.path.emplace_back(sf.cols[size_t(col)]);
        o.keys = sf.rows;
        o.max = double(o.keys - 1) * 65536.0;
        o.start = std::atoi(key(kSplineStartPos).c_str());
        if (o.start < 0 || o.start >= o.max) throw std::runtime_error(*name + ": start position out of range");

        const std::string m3d = key(kObject);
        auto& mesh = s->meshes[m3d];
        if (!mesh) mesh = std::make_unique<generic::Mesh3d>(generic::readM3d(exe.file(m3d)));
        o.mesh = mesh.get();

        // the maps (0x4077d4): types 9, 10 env + bump; 11 texture + bump, Phong map, light table
        const std::string bump = key(kBumpMap);
        const std::string env = o.type == kTypePhongBump ? std::string() : key(kEnvironmentMap);
        const std::string tex = o.type == kTypePhongBump ? key(kTextureMap) : std::string();
        const std::string lit = o.type == kTypePhongBump ? key(kTextureLite) : std::string();
        auto& maps = s->maps[env + "|" + bump + "|" + tex + "|" + lit];
        if (!maps) {
            maps = std::make_unique<Maps>();
            maps->slope = slopeTexture(mapPixels(c.pictures->indexed(bump)));
            if (!env.empty()) maps->env = vivid::uploadIndices(mapPixels(c.pictures->indexed(env)), 256, 256);
            if (!tex.empty()) {
                maps->tex = vivid::uploadIndices(mapPixels(c.pictures->indexed(tex)), 256, 256);
                std::span<const uint8_t> l = exe.file(lit);   // the first 64 KB over a zeroed block
                std::vector<uint8_t> table(65536, 0);
                std::copy_n(l.begin(), std::min<size_t>(l.size(), table.size()), table.begin());
                maps->lit = vivid::uploadIndices(table, 256, 256);
            }
        }
        o.maps = maps.get();
        if (o.type == kTypePhongBump) {
            o.amb = float(std::atoi(key(kPhongAmb).c_str())) / 65536.0f;
            o.diff = float(std::atoi(key(kPhongDiff).c_str())) / 65536.0f;
            o.spec = float(std::atoi(key(kPhongSpec).c_str())) / 65536.0f;
            const int e = std::atoi(key(kPhongExp).c_str());
            o.power = e == 0 ? 0 : (e == 1 ? 1 : e + 1);   // 0x413a60: 1, c, or c^(exp + 1)
            o.shades = std::clamp(std::atoi(key(kPhongShades).c_str()), 1, 256);
        }
        uploadMesh(o);
        s->objs.push_back(std::move(o));
    }
    return s;
}

// One object's state at t refreshes since GenericEffect started (0x409d74).
struct State {
    double X, Y, Z, A, B, C;
};

State stateAt(const Object& o, const Setup& s, double t) {
    // pos = start + speed t (16.16 keys); a whole lap restarts the object at its start (t0 = counter) when looping
    if (o.speed > 0 && s.looping) t = std::fmod(t, o.max / o.speed);
    const double pos = std::fmod(o.start + o.speed * t, o.max);
    const double k = pos / 65536.0;
    return {o.path[0](k), o.path[1](k), o.path[2](k), o.path[3](k), o.path[4](k), o.path[5](k)};
}

// ---------------------------------------------------------------- the multisampled target
struct MsTarget {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0, samples = 0;
    Target resolved;
    bool ensure(int tw, int th) {
        if (fbo && w == tw && h == th) return samples > 0;
        if (!fbo) {
            gl::GenFramebuffers(1, &fbo);
            gl::GenRenderbuffers(1, &color);
            gl::GenRenderbuffers(1, &depth);
        }
        GLint maxS = 0;
        gl::GetIntegerv(GL_MAX_SAMPLES, &maxS);
        samples = std::max(1, std::min(kSamples, int(maxS)));
        gl::BindRenderbuffer(GL_RENDERBUFFER, color);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16, tw, th);
        gl::BindRenderbuffer(GL_RENDERBUFFER, depth);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, tw, th);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        resolved.create(tw, th, false, GL_RGBA16);
        w = tw;
        h = th;
        if (!ok) {
            SDL_Log("generic: %d-sample target %dx%d incomplete", samples, tw, th);
            samples = 0;
        }
        return samples > 0;
    }
    void release() {
        if (fbo) gl::DeleteFramebuffers(1, &fbo);
        if (color) gl::DeleteRenderbuffers(1, &color);
        if (depth) gl::DeleteRenderbuffers(1, &depth);
        fbo = color = depth = 0;
        resolved.destroy();
    }
};

class GenericScene : public Scene {
public:
    ~GenericScene() override { ms_.release(); }

    void init(Ctx& c) override {
        const std::string idx = vivid::kIndexedGLSL;
        if (!objShader_.build(kObjectVS, idx + kObjectFS, "generic") ||
            !picShader_.build(kFullscreenVS, idx + kPictureFS, "generic picture") ||
            !bgShader_.build(kFullscreenVS, kBackgroundFS, "generic background"))
            return;
        const VividExe& exe = *c.exe;
        // the bump table's constants and the projection's immediates
        uint64_t bits = uint64_t(exe.u32(kBumpDivHi)) << 32 | uint32_t(int32_t(exe.s8(kBumpDivLo)));
        double div;
        std::memcpy(&div, &bits, 8);
        bumpK_[0] = float(1.0 / div);
        bumpK_[1] = float(exe.f64(kBumpScale));
        camZ_ = exe.s32(kCamZ);
        cx_ = exe.s32(kCentreX);
        cy_ = exe.s32(kCentreY);
        fx_ = -double(exe.s32(kScaleX)) * camZ_ / 1048576.0;    // kx = (0xd8f0 * camz) >> 20 = -371, unrounded
        fy_ = -double(exe.s32(kScaleY)) * camZ_ / 1048576.0;    // ky = -((0xb4c6 * camz) >> 20) = 309
        nearZ_ = exe.s32(kNearZ);
        for (const vivid::Segment& s : c.timeline->segments()) {
            if (s.scene != "generic" || s.init < 0 || setups_.count(s.init)) continue;
            const vivid::ScriptLine& l = c.timeline->line(s.init).line;
            try {
                setups_[s.init] = loadSetup(c, l.arg(1));
            } catch (const std::exception& e) {
                SDL_Log("generic: script line %d (%s): %s", l.line, l.arg(1).c_str(), e.what());
            }
        }
        ok_ = true;
    }

    void render(Ctx& c, double r) override {
        auto it = c.seg ? setups_.find(c.seg->init) : setups_.end();
        if (!ok_ || it == setups_.end()) return;
        const Setup& s = *it->second;
        const double t = std::max(0.0, c.since(r));
        const bool ms = ms_.ensure(c.out->w, c.out->h);
        auto bindTarget = [&] {   // the multisampled target, or ctx.out if there is none
            if (!ms) return c.out->bind();
            gl::BindFramebuffer(GL_FRAMEBUFFER, ms_.fbo);
            gl::Viewport(0, 0, c.out->w, c.out->h);
        };
        drawBackground(c, s, r, bindTarget);

        // object states, then the objects by Z, farthest first (0x415480: stable)
        std::vector<State> st;
        for (const Object& o : s.objs) st.push_back(stateAt(o, s, t));
        std::vector<int> order(s.objs.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return st[size_t(a)].Z > st[size_t(b)].Z; });

        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
        gl::DepthMask(GL_TRUE);
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_BACK);
        setBlend(Blend::None);
        objShader_.use();
        objShader_.set("envTex", 0);
        objShader_.set("slopeTex", 1);
        objShader_.set("texTex", 2);
        objShader_.set("litTex", 3);
        objShader_.set("pal", 4);
        c.paletteTex().bind(4);
        objShader_.set("bumpK", bumpK_[0], bumpK_[1]);
        objShader_.set("maxSamples", kMaxSubsamples);
        // clip = (fx / 160 x + (cx - 160) / 160 d, fy / 100 y + (100 - cy) / 100 d, a d + b, d), d = z - camz
        objShader_.set("proj", float(fx_ / 160.0), float(fy_ / 100.0), float((cx_ - 160.0) / 160.0),
                       float((100.0 - cy_) / 100.0));
        const double zn = 0.8 * (nearZ_ - camZ_), zf = 1e6;   // closer than every face the near rule keeps
        objShader_.set("depth", float(camZ_), float((zf + zn) / (zf - zn)), float(-2.0 * zf * zn / (zf - zn)));
        for (int i : order) drawObject(s.objs[size_t(i)], st[size_t(i)]);
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_CULL_FACE);
        gl::BindVertexArray(0);

        if (ms) {   // resolve, then copy into ctx.out (whatever its format)
            gl::BindFramebuffer(GL_READ_FRAMEBUFFER, ms_.fbo);
            gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, ms_.resolved.fbo);
            gl::BlitFramebuffer(0, 0, ms_.w, ms_.h, 0, 0, ms_.w, ms_.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            c.out->bind();
            setBlend(Blend::None);
            c.drawTarget(ms_.resolved);
        }
    }

private:
    // The page's background (GenericEffect 0x409d74, refilled by the retrace routine 0x4073fc): the picture or the
    // colour BackgroundColor.
    template <class Bind> void drawBackground(Ctx& c, const Setup& s, double r, Bind&& bindTarget) {
        gl::Disable(GL_DEPTH_TEST);
        gl::DepthMask(GL_TRUE);
        setBlend(Blend::None);
        if (!s.usePic) {
            bindTarget();
            Vec3 col = c.palette(r)[size_t(s.bgColour)];
            gl::ClearColor(col.x, col.y, col.z, 1.0f);
            gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            return;
        }
        const Texture* redraw = c.pictures->redraw(s.pic);
        TargetPool::Lease rgb;
        if (!redraw) {   // the indices through the DAC at the original's resolution
            const Texture& idx = c.pictures->indices(s.pic);
            rgb = c.tempSized(320, 200, false, GL_RGBA16F);
            rgb->bind();
            picShader_.use();
            idx.bind(0);
            c.paletteTex().bind(1);
            picShader_.set("pic", 0);
            picShader_.set("pal", 1);
            drawFullscreen();
        }
        bindTarget();
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        bgShader_.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, redraw ? redraw->id : rgb->tex);
        bgShader_.set("rgb", 0);
        bgShader_.set("redraw", redraw ? 1 : 0);
        drawFullscreen();
    }

    // One object (0x409d74's loop body and its drawer): rotation, the near rule, then the shaded triangles.
    void drawObject(const Object& o, const State& st) {
        const generic::Rot rot = generic::rotation(st.A, st.B, st.C);
        // the near rule: a face with a vertex at z + Z <= -6720 (depth <= 280) is hidden, not clipped
        const auto& v = o.mesh->verts;
        nearOk_.resize(v.size());
        for (size_t i = 0; i < v.size(); ++i) nearOk_[i] = rot.row2(v[i].x, v[i].y, v[i].z) + st.Z > nearZ_;
        idx_.clear();
        const auto& f = o.mesh->faces;
        for (size_t k = 0; k + 2 < f.size(); k += 3)
            if (nearOk_[f[k]] && nearOk_[f[k + 1]] && nearOk_[f[k + 2]]) idx_.insert(idx_.end(), {f[k], f[k + 1], f[k + 2]});
        if (idx_.empty()) return;

        gl::Clear(GL_DEPTH_BUFFER_BIT);   // each object is drawn over the ones before
        float m[9];
        for (int i = 0; i < 9; ++i) m[i] = float(rot.m[i]);
        gl::UniformMatrix3fv(objShader_.loc("rot"), 1, GL_FALSE, m);
        objShader_.set("trans", float(st.X), float(st.Y), float(st.Z));
        objShader_.set("type", o.type);
        objShader_.set("phong", o.amb, o.diff, o.spec, float(o.shades - 1));
        objShader_.set("phongPow", o.power);
        if (o.maps->env.id) o.maps->env.bind(0);
        o.maps->slope.bind(1);
        if (o.maps->tex.id) o.maps->tex.bind(2);
        if (o.maps->lit.id) o.maps->lit.bind(3);
        gl::BindVertexArray(o.vao);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, o.ibo);
        gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(idx_.size() * 4), idx_.data());
        gl::DrawElements(GL_TRIANGLES, GLsizei(idx_.size()), GL_UNSIGNED_INT, nullptr);
    }

    Shader objShader_, picShader_, bgShader_;
    bool ok_ = false;
    float bumpK_[2] = {0.01f, 81.5f};
    double camZ_ = -7000, cx_ = 160, cy_ = 100, fx_ = 371, fy_ = 309, nearZ_ = -6720;
    std::map<int, std::unique_ptr<Setup>> setups_;   // by the index of the GenericInit line
    MsTarget ms_;
    std::vector<char> nearOk_;
    std::vector<uint32_t> idx_;
};

}  // namespace

REGISTER_SCENE(GenericScene, "generic");
