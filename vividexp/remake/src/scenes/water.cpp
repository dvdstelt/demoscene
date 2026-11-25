// The water scene, part II's opening (vividexp/re/notes-water.md): three orange hearts and a larger blue
// "transparent" heart tumble along spline paths over a picture of a star cluster, environment-mapped through a bump
// map; the orange hearts behind the blue one are painted into its environment map, so that it looks like glass.
// Ports WaterInit 0x405bd0 and WaterEffect 0x4064b0 with the object drawer 0x40580c and the engine routines they call:
// the splines 0x413870 / 0x413b90 (water_engine.h), the rotations 0x413c93 / 0x413d98 / 0x413e6a / 0x413f37, the
// environment coordinates 0x414160, the projection 0x414000, the back-face test 0x4140e0, the object order 0x415480,
// the bump-environment rasterizer 0x414f13 (the hearts on screen) and the affine rasterizer 0x41448a (the hearts in
// the blue heart's map), and the bump table of WaterInit (0x405fcf).
//
// Evaluated exactly instead of the original's shortcuts:
// - positions and angles from the natural cubic splines in doubles, continuously in time (count = refreshes since
//   the effect's first frame, fractional), extrapolated past the last knot as the original's search does; rotations
//   from float sin / cos instead of a 2048-step table and three successive truncating 16-bit products;
// - the projection with the original's focal lengths (371, 309 at the camera distance 7000) in float, perspective-
//   correct triangles with 4x MSAA edges instead of integer vertices and affine spans; inside each heart a depth
//   buffer instead of the painter's sort of the face centres (the heart is concave); the hearts still cover each
//   other in the original's order (farthest spline z first, each over the ones before);
// - the environment coordinates per pixel from the interpolated normal, u = 128 + (256 / pi) asin(nx),
//   v = 128 + (256 / pi) asin(ny), instead of the 513-entry asin table at the vertices interpolated linearly;
// - the bump offsets per pixel from the bump map's height differences filtered bilinearly (mipmapped when the map is
//   minified): offset = (256 / 3.14) atan(0.01 dh) with the two constants read from VIVID.EXE, instead of the
//   nearest texel of a truncated byte table; the zero slope of the map's last row and column is kept;
// - the environment maps and the background filtered in colour (each index through the palette, then filtered)
//   instead of nearest indices;
// - the hearts behind the blue heart drawn into its map at a resolution that follows the output's (256 x 256 up to
//   2048 x 2048 from 2560 wide) with the same projection (1/16 scale, the camera 10000 behind), anti-aliased.
// Kept: the hearts in the blue heart's map only while they are more than 500 units behind it (decided per refresh
// with the original's integer z, as it decides it per drawn frame), and the stale map while none is: the map of the
// last refresh that had one (notes 8.3). Dropped: the black second record and the frame pacing (70 or 35 frames per
// second): the scene draws every refresh at count = refreshes since the start - 1.
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "demo.h"
#include "exe.h"
#include "scenes/water_engine.h"

#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif

namespace {

// ---------------------------------------------------------------- what WaterInit reads, by address in VIVID.EXE
// The ini keys (strings at 0x8205cc..0x820687) and the object keys' pointer table 0x821e84.
constexpr uint32_t kKeyUseBackgroundPic = 0x8205cc;
constexpr uint32_t kKeyBackgroundPic = 0x8205dd;
constexpr uint32_t kKeyBackgroundColor = 0x8205eb;
constexpr uint32_t kKeyEnvironmentMap = 0x8205fb;
constexpr uint32_t kKeyBumpMap = 0x82060a;
constexpr uint32_t kKeySolidSplinePath = 0x820647;
constexpr uint32_t kKeyTransparentSplinePath = 0x820657;
constexpr uint32_t kKeySplineSpeed = 0x82066d;
constexpr uint32_t kKeyObjectSpacing = 0x820679;
constexpr uint32_t kKeyUseSplineLooping = 0x820687;
constexpr uint32_t kObjectKeys = 0x821e84;   // TransparentObj, SolidObj0, SolidObj1, SolidObj2
// The bump table's constants (doubles): 0.01 and 256 / 3.14.
constexpr uint32_t kBumpIn = 0x820698;
constexpr uint32_t kBumpOut = 0x8206a0;

// ---------------------------------------------------------------- code constants of WaterInit / WaterEffect
constexpr int kDefaultSplineSpeed = 70;    // ini default
constexpr double kBehind = 500;            // 0x406735: a solid heart is behind when its z > the blue heart's z + 500
constexpr double kMapCamera = 10000;       // its env-map camera: camz = -(z - zt + 10000), kx = camz / 16, ky = -kx
constexpr double kMapScale = 16;
constexpr double kCamZ = -7000;            // 0x414000's arguments for the screen: camera z, scales 0xd8f0, 0xb4c6
constexpr double kScaleX = 0xd8f0, kScaleY = 0xb4c6;
constexpr double kScreenW = 320, kScreenH = 200;   // centre (160, 100)
constexpr double kMapSize = 256;           // the env maps: 256 x 256, centre (128, 128)

constexpr int kObjects = 4;                // 0 the transparent heart, 1..3 the solid hearts
constexpr int kSamples = 4;                // MSAA

// ---------------------------------------------------------------- shaders
const char* kHeartVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aBump;
uniform mat3 rot;       // the object's rotation Rz(c) Rx(a) Ry(b)
uniform vec3 pos;       // its position
uniform vec3 proj;      // clip x = proj.x (x + px), clip y = proj.y (y + py), clip w = z + pz - proj.z
uniform vec2 depthAB;   // ndc depth = A + B / w
uniform vec2 slice;     // the object's depth range in ndc: later objects lie in front of earlier ones
centroid out vec3 vN;
centroid out vec2 vB;
void main() {
    vec3 p = rot * aPos + pos;
    float w = p.z - proj.z;
    float zn = mix(slice.x, slice.y, 0.5 + 0.5 * (depthAB.x + depthAB.y / w));
    gl_Position = vec4(proj.x * p.x, proj.y * p.y, zn * w, w);
    vN = rot * aNrm;
    vB = aBump;
}
)";

// The pixel's colour is a texel of the environment map (0x414f13): at the environment coordinates of the normal,
// shifted by the bump map's slope offsets (in env texels). Texel n covers [n, n + 1), so the sample point is e / 256.
const char* kHeartFS = R"(
centroid in vec3 vN;
centroid in vec2 vB;
uniform sampler2D env;      // the environment map in colour (repeat)
uniform sampler2D slopes;   // the bump map's height differences to the next texel in u and v (repeat)
uniform int bump;
uniform vec2 bumpK;         // offset = bumpK.y atan(bumpK.x slope)
out vec4 o;
void main() {
    vec3 n = normalize(vN);
    vec2 e = 128.0 + (256.0 / PI) * asin(clamp(n.xy, -1.0, 1.0));
    if (bump != 0) e += bumpK.y * atan(bumpK.x * texture(slopes, vB / 256.0).rg);
    o = vec4(texture(env, e / 256.0).rgb, 1.0);
}
)";

// Indices to colours through the palette, one texel per pixel (offset: where the target's texel (0, 0) is read).
const char* kConvertFS = R"(
uniform usampler2D idx;
uniform sampler2D pal;
uniform ivec2 offset;
out vec4 o;
void main() { o = vec4(palColor(pal, texelFetch(idx, ivec2(gl_FragCoord.xy) + offset, 0).r), 1.0); }
)";

// ---------------------------------------------------------------- GL helpers
// A texture with a framebuffer to draw into it.
struct TexTarget {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    bool mip = false;
    void create(int w_, int h_, GLenum ifmt, bool repeat, bool mipmap) {
        destroy();
        w = w_;
        h = h_;
        mip = mipmap;
        gl::GenTextures(1, &tex);
        gl::BindTexture(GL_TEXTURE_2D, tex);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(ifmt), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (mip) gl::GenerateMipmap(GL_TEXTURE_2D);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            SDL_Log("water: framebuffer %dx%d incomplete", w, h);
    }
    void bind() const {
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, w, h);
    }
    void updateMips() const {
        if (!mip) return;
        gl::ActiveTexture(GL_TEXTURE0 + 7);   // a unit no pass reads from
        gl::BindTexture(GL_TEXTURE_2D, tex);
        gl::GenerateMipmap(GL_TEXTURE_2D);
    }
    void destroy() {
        if (fbo) gl::DeleteFramebuffers(1, &fbo);
        if (tex) gl::DeleteTextures(1, &tex);
        fbo = tex = 0;
        w = h = 0;
    }
};

// A multisampled colour + depth framebuffer, resolved into a TexTarget of the same format.
struct MsaaTarget {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
    GLenum fmt = 0;
    void ensure(int w_, int h_, GLenum fmt_, int samples) {
        if (fbo && w == w_ && h == h_ && fmt == fmt_) return;
        destroy();
        w = w_;
        h = h_;
        fmt = fmt_;
        gl::GenRenderbuffers(1, &color);
        gl::BindRenderbuffer(GL_RENDERBUFFER, color);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, w, h);
        gl::GenRenderbuffers(1, &depth);
        gl::BindRenderbuffer(GL_RENDERBUFFER, depth);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            SDL_Log("water: multisampled framebuffer %dx%d incomplete", w, h);
    }
    void bind() const {
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, w, h);
    }
    void resolve(const TexTarget& dst) const {
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);
        gl::BlitFramebuffer(0, 0, w, h, 0, 0, dst.w, dst.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void destroy() {
        if (fbo) gl::DeleteFramebuffers(1, &fbo);
        if (color) gl::DeleteRenderbuffers(1, &color);
        if (depth) gl::DeleteRenderbuffers(1, &depth);
        fbo = color = depth = 0;
        w = h = 0;
    }
};

// A heart's triangles for the GPU: one vertex per face corner (the .m3d numbers normals separately from vertices).
struct HeartMesh {
    GLuint vao = 0, vbo = 0;
    int count = 0;
    void create(const vivid3d::M3d& m) {
        std::vector<float> v;
        for (const auto& g : m.groups)
            for (const auto& f : g.faces)
                for (int k = 0; k < 3; ++k) {
                    const auto& p = m.verts[f.v[size_t(k)]];
                    const auto& n = m.normals[f.n[size_t(k)]];
                    // bump coordinates in texels: 0..65535 is one turn of the 256-texel map (0x413330)
                    float a[8] = {float(p.x), float(p.y), float(p.z), float(n[0]), float(n[1]), float(n[2]),
                                  float(p.u) / 256.0f, float(p.v) / 256.0f};
                    v.insert(v.end(), a, a + 8);
                }
        count = int(v.size() / 8);
        gl::GenVertexArrays(1, &vao);
        gl::BindVertexArray(vao);
        gl::GenBuffers(1, &vbo);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
        gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
        gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::EnableVertexAttribArray(2);
        gl::BindVertexArray(0);
    }
    void draw() const {
        gl::BindVertexArray(vao);
        gl::DrawArrays(GL_TRIANGLES, 0, count);
    }
    void destroy() {
        if (vbo) gl::DeleteBuffers(1, &vbo);
        if (vao) gl::DeleteVertexArrays(1, &vao);
        vao = vbo = 0;
    }
};

// An object's six spline values: position x, y, z (y up, z away from the camera) and the angles a (about X),
// b (about Y), c (about Z) in 65536ths of a turn.
struct Pose {
    double x = 0, y = 0, z = 0, a = 0, b = 0, c = 0;
};

// The rotation of 0x413c93..0x413f37, v' = Rz(c) Rx(a) Ry(b) v with the engine's signs, column-major for GL.
std::array<float, 9> rotation(const Pose& p) {
    const double k = 6.283185307179586 / 65536.0;
    const double sa = std::sin(p.a * k), ca = std::cos(p.a * k), sb = std::sin(p.b * k), cb = std::cos(p.b * k),
                 sc = std::sin(p.c * k), cc = std::cos(p.c * k);
    const double ry[3][3] = {{cb, 0, -sb}, {0, 1, 0}, {sb, 0, cb}};    // x' = x cb - z sb, z' = z cb + x sb
    const double rx[3][3] = {{1, 0, 0}, {0, ca, -sa}, {0, sa, ca}};    // y' = y ca - z sa, z' = z ca + y sa
    const double rz[3][3] = {{cc, sc, 0}, {-sc, cc, 0}, {0, 0, 1}};    // x' = x cc + y sc, y' = y cc - x sc
    double t[3][3], m[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t[i][j] = rx[i][0] * ry[0][j] + rx[i][1] * ry[1][j] + rx[i][2] * ry[2][j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i][j] = rz[i][0] * t[0][j] + rz[i][1] * t[1][j] + rz[i][2] * t[2][j];
    std::array<float, 9> out{};
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row) out[size_t(col * 3 + row)] = float(m[row][col]);
    return out;
}

}  // namespace

class WaterScene : public Scene {
public:
    ~WaterScene() override { release(); }

    void init(Ctx& c) override {
        try {
            load(c);
            ok_ = true;
        } catch (const std::exception& e) {
            SDL_Log("water: %s", e.what());
            ok_ = false;
        }
    }

    void render(Ctx& c, double r) override {
        if (!ok_) return;
        // count = the water's refresh counter since its start (notes 9.3): the frame drawn with count n is on screen
        // from the effect's refresh n + 1 on
        double count = c.since(r) - 1.0;
        if (looping_) count = std::fmod(std::max(count, 0.0), lapCount_);
        else count = std::min(count, lapCount_);   // the effect would end when the leading heart passed the path's end

        Pose pose[kObjects];
        for (int k = 0; k < kObjects; ++k) pose[k] = poseAt(k, count);

        const Target& out = *c.out;
        setBlend(Blend::None);
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_CULL_FACE);
        convertPictures(c);
        drawMap(c, count);
        drawScreen(c, out, pose, r);

        out.bind();
        setBlend(Blend::None);
        c.drawTexture(resolved_.tex, 0, 0, 320, 200, 1.0f, 0, 1, 1, 0);
    }

private:
    // ---------------------------------------------------------------- init: WaterInit 0x405bd0
    void load(Ctx& c) {
        const VividExe& exe = *c.exe;
        const vivid::Segment* seg = nullptr;
        for (const vivid::Segment& s : c.timeline->segments())
            if (s.scene == "water" && s.init >= 0) { seg = &s; break; }
        if (!seg) throw std::runtime_error("no WaterInit line in the scripts");
        // the Init line: "WaterInit 1 water/water.ini" (1: the files come from the archives)
        const std::string iniName = c.timeline->line(seg->init).line.arg(1);
        vivid3d::Ini ini(exe.file(iniName));
        auto key = [&](uint32_t a) { return exe.cstr(a); };
        auto need = [&](uint32_t a) {
            std::optional<std::string> v = ini.str(key(a));
            if (!v) throw std::runtime_error(iniName + ": no " + key(a));
            return *v;
        };

        // background: the picture, or a palette index
        useBack_ = ini.integer(key(kKeyUseBackgroundPic), 0) != 0;
        if (useBack_) {
            backName_ = need(kKeyBackgroundPic);
            backIdx_ = vivid::uploadIndices(c.pictures->indexed(backName_));
            back_.create(backIdx_.w, backIdx_.h, GL_RGBA16F, false, false);
        } else {
            backColour_ = ini.integer(key(kKeyBackgroundColor), 0) & 255;
        }

        // the environment maps: the left half of the picture for the solid hearts, the right half for the blue one
        vivid::Indexed env = vivid::loadPicture(exe, need(kKeyEnvironmentMap));
        if (env.w < 512 || env.h < 256) throw std::runtime_error("environment map smaller than 512 x 256");
        envIdx_ = vivid::uploadIndices(env);
        for (TexTarget& t : envCol_) t.create(256, 256, GL_RGBA16F, true, true);

        // the bump map's slopes (0x405fcf): height differences to the next texel in u and in v; the last column and
        // row stay 0. Kept as differences: the shader filters them and applies the atan per pixel.
        vivid::Indexed b = vivid::loadPicture(exe, need(kKeyBumpMap));
        if (b.w < 256 || b.h < 256) throw std::runtime_error("bump map smaller than 256 x 256");
        std::vector<float> slope(256 * 256 * 2, 0.0f);
        for (int y = 0; y < 255; ++y)
            for (int x = 0; x < 255; ++x) {
                const size_t p = size_t(y * 256 + x) * 2;
                slope[p] = float(int(b.at(x + 1, y)) - int(b.at(x, y)));
                slope[p + 1] = float(int(b.at(x, y + 1)) - int(b.at(x, y)));
            }
        slopes_.createRaw(256, 256, GL_RG32F, GL_RG, GL_FLOAT, slope.data(), true, true, true);
        gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        gl::GetError();   // anisotropy is optional
        bumpIn_ = float(exe.f64(kBumpIn));
        bumpOut_ = float(exe.f64(kBumpOut));

        // the objects: TransparentObj, SolidObj0..2 (the same file loaded once)
        for (int k = 0; k < kObjects; ++k) {
            const std::string name = need(exe.u32(kObjectKeys + 4 * uint32_t(k)));
            int found = -1;
            for (size_t i = 0; i < meshNames_.size(); ++i)
                if (meshNames_[i] == name) found = int(i);
            if (found < 0) {
                vivid3d::M3d m = vivid3d::parseM3d(exe.file(name));
                if (m.normals.empty() || !(m.flags & 2)) throw std::runtime_error(name + ": no normals per corner");
                meshes_.emplace_back();
                meshes_.back().create(m);
                meshNames_.push_back(name);
                found = int(meshes_.size()) - 1;
            }
            meshOf_[k] = found;
        }

        // the paths: six natural cubic splines each, the knots one SplineSpeed apart (0x413520 / 0x4137f8 / 0x413870)
        const uint32_t pathKeys[2] = {kKeyTransparentSplinePath, kKeySolidSplinePath};
        size_t rows = 0;
        for (int p = 0; p < 2; ++p) {
            vivid3d::Spl spl = vivid3d::parseSpl(exe.file(need(pathKeys[p])));
            for (size_t ch = 0; ch < 6; ++ch) paths_[p][ch] = vivid3d::NaturalSpline(spl.column(ch));
            rows = spl.rows.size();   // [0x833dc0]: both files are read, the last one (the transparent) counts
        }
        const int speed = ini.integer(key(kKeySplineSpeed), kDefaultSplineSpeed);
        step_ = speed ? double(65536 / speed) : 0.0;   // 16.16 knots per refresh, an integer division
        const double spacing = ini.integer(key(kKeyObjectSpacing), 0);
        offset_[0] = 0;   // the transparent heart
        for (int k = 1; k < kObjects; ++k) offset_[k] = (k - 1) * spacing;
        looping_ = ini.integer(key(kKeyUseSplineLooping), 0) != 0;
        // the first count at which an object's t reaches the end (rows << 16): there the effect ends (or loops)
        const double maxOffset = *std::max_element(offset_, offset_ + kObjects);
        lapCount_ = step_ > 0 ? (double(rows) * 65536.0 - maxOffset) / step_ : 1e9;

        // which whole counts have a heart behind the blue one (for the stale map)
        const int n = int(std::ceil(seg->end - seg->start)) + 2;
        for (int i = 0; i < n; ++i) behindAt_.push_back(uint8_t(behindMask(i)));

        if (!heart_.build(kHeartVS, kHeartFS, "water heart") ||
            !convert_.build(kFullscreenVS, std::string(vivid::kIndexedGLSL) + kConvertFS, "water convert"))
            throw std::runtime_error("shader build failed");
        GLint maxSamples = 1;
        gl::GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        samples_ = std::clamp(int(maxSamples), 1, kSamples);
    }

    void release() {
        for (HeartMesh& m : meshes_) m.destroy();
        meshes_.clear();
        for (TexTarget& t : envCol_) t.destroy();
        back_.destroy();
        mapTex_.destroy();
        resolved_.destroy();
        mapMsaa_.destroy();
        screenMsaa_.destroy();
        envIdx_.destroy();
        backIdx_.destroy();
        slopes_.destroy();
    }

    // ---------------------------------------------------------------- motion (5., 9.1)
    double splineS(int k, double count) const { return (count * step_ + offset_[k]) / 65536.0; }

    Pose poseAt(int k, double count) const {
        const auto& p = paths_[k == 0 ? 0 : 1];
        const double s = splineS(k, count);
        return {p[0](s), p[1](s), p[2](s), p[3](s), p[4](s), p[5](s)};
    }

    double zAt(int k, double count) const { return paths_[k == 0 ? 0 : 1][2](splineS(k, count)); }

    // The solid hearts behind the blue one (bit k) in the frame drawn at the whole count n: z > zt + 500 (8.1),
    // with the original's integer z (the spline value >> 8, floor). A step: decided once per refresh.
    unsigned behindMask(long n) const {
        if (n >= 0 && size_t(n) < behindAt_.size()) return behindAt_[size_t(n)];
        const double zt = std::floor(zAt(0, double(n)));
        unsigned m = 0;
        for (int k = 1; k < kObjects; ++k)
            if (std::floor(zAt(k, double(n))) > zt + kBehind) m |= 1u << k;
        return m;
    }

    // What the blue heart's map shows at `count` (8.3): the hearts behind it in this refresh's frame, moving with
    // the count; in a refresh without one, the map of the last frame that had one (the original resets the map only
    // when it draws a heart into it). time < 0: none yet, the clean map.
    struct MapState {
        double time = -1;
        unsigned mask = 0;
    };
    MapState mapState(double count) const {
        const long now = long(std::floor(count));
        if (unsigned m = behindMask(now)) return {count, m};
        for (long n = now - 1; n >= 0; --n)
            if (unsigned m = behindMask(n)) return {double(n), m};
        return {};
    }

    // ---------------------------------------------------------------- drawing
    // The environment-map halves and the background in colour, through the palette the DAC holds now.
    void convertPictures(Ctx& c) {
        convert_.use();
        envIdx_.bind(0);
        c.paletteTex().bind(1);
        convert_.set("idx", 0);
        convert_.set("pal", 1);
        for (int i = 0; i < 2; ++i) {
            envCol_[i].bind();
            gl::Uniform2i(convert_.loc("offset"), 256 * i, 0);
            drawFullscreen();
            envCol_[i].updateMips();
        }
        if (useBack_) {
            backIdx_.bind(0);
            back_.bind();
            gl::Uniform2i(convert_.loc("offset"), 0, 0);
            drawFullscreen();
        }
    }

    // One heart with the heart shader. proj / pos / slice as in kHeartVS.
    void drawHeart(int k, const Pose& p, float px, float py, float pz, float ax, float ay, float camz, float lo,
                   float hi, GLuint env, bool bump) {
        const std::array<float, 9> rot = rotation(p);
        // depth: ndc = A + B / w with near 100, far 1e6 (the objects stay 3000 or more in front of the camera)
        const float n = 100.0f, f = 1.0e6f;
        heart_.use();
        gl::UniformMatrix3fv(heart_.loc("rot"), 1, GL_FALSE, rot.data());
        heart_.set("pos", px, py, pz);
        heart_.set("proj", ax, ay, camz);
        heart_.set("depthAB", (f + n) / (f - n), -2.0f * f * n / (f - n));
        heart_.set("slice", lo, hi);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, env);
        slopes_.bind(1);
        heart_.set("env", 0);
        heart_.set("slopes", 1);
        heart_.set("bump", bump ? 1 : 0);
        heart_.set("bumpK", bumpIn_, bumpOut_);
        meshes_[size_t(meshOf_[k])].draw();
    }

    // The blue heart's environment map (8.1): its clean picture, and the solid hearts behind it drawn in at 1/16
    // scale with the left (orange) map by their own environment coordinates, in object order, each over the ones
    // before. A camera 10000 + (z - zt) behind each heart's centre; the heart's own z is left out.
    void drawMap(Ctx& c, double count) {
        const int size = std::clamp(1 << int(std::ceil(std::log2(std::max(1.0, c.out->w / 2.0)))), 256, 2048);
        if (mapTex_.w != size) mapTex_.create(size, size, GL_RGB10_A2, true, true);
        mapMsaa_.ensure(size, size, GL_RGB10_A2, samples_);
        mapMsaa_.bind();
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        setBlend(Blend::None);
        // map row v is texture row v: drawn bottom-up in GL
        c.drawTexture(envCol_[1].tex, 0, 0, 320, 200, 1.0f, 0, 1, 1, 0);

        const MapState ms = mapState(count);
        if (ms.time >= 0) {
            const double tm = ms.time;
            const unsigned mask = ms.mask;
            const double zt = zAt(0, tm);
            gl::Enable(GL_DEPTH_TEST);
            gl::DepthFunc(GL_LESS);
            gl::Enable(GL_CULL_FACE);
            gl::CullFace(GL_FRONT);   // the map's rows run up in GL: the kept (counter-clockwise) faces turn clockwise
            for (int k = 1; k < kObjects; ++k) {
                if (!(mask >> k & 1)) continue;
                const Pose p = poseAt(k, tm);
                const double d = p.z - zt + kMapCamera;
                // u = 128 + (x + px) (d / 16) / (d + z), v = 128 - (y + py) (d / 16) / (d + z)
                const float a = float(2.0 * d / kMapScale / kMapSize);
                const float lo = 1.0f - 2.0f * float(k) / kObjects, hi = 1.0f - 2.0f * float(k - 1) / kObjects;
                drawHeart(k, p, float(p.x), float(p.y), 0.0f, a, -a, float(-d), lo, hi, envCol_[0].tex, false);
            }
            gl::Disable(GL_CULL_FACE);
            gl::Disable(GL_DEPTH_TEST);
        }
        mapMsaa_.resolve(mapTex_);
        mapTex_.updateMips();
    }

    // The frame: the background, then the four hearts farthest spline z first (0x415480, stable), each over the
    // ones before, with the bump map and their environment map (the blue heart's with the hearts drawn in).
    void drawScreen(Ctx& c, const Target& out, const Pose* pose, double r) {
        if (resolved_.w != out.w || resolved_.h != out.h) resolved_.create(out.w, out.h, GL_RGB10_A2, false, false);
        screenMsaa_.ensure(out.w, out.h, GL_RGB10_A2, samples_);
        screenMsaa_.bind();
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        setBlend(Blend::None);
        if (useBack_) {
            const Texture* redraw = c.pictures->redraw(backName_);
            c.drawTexture(redraw ? redraw->id : back_.tex, 0, 0, 320, 200);
        } else {
            c.clear(c.palette(r)[size_t(backColour_)]);
        }

        int order[kObjects];
        for (int k = 0; k < kObjects; ++k) order[k] = k;
        std::stable_sort(order, order + kObjects, [&](int a, int b) { return pose[a].z > pose[b].z; });

        // screen: X = 160 + (x + px) kx / (camz - z - pz), Y = 100 + (y + py) ky / (camz - z - pz) (0x414000) with
        // kx = (0xd8f0 camz) >> 20 = -371, ky = -((0xb4c6 camz) >> 20) = 309 (arithmetic shifts: floor)
        const double kx = std::floor(kScaleX * kCamZ / 1048576.0), ky = -std::floor(kScaleY * kCamZ / 1048576.0);
        const float ax = float(-2.0 * kx / kScreenW), ay = float(2.0 * ky / kScreenH);
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_BACK);
        for (int j = 0; j < kObjects; ++j) {
            const int k = order[j];
            const Pose& p = pose[k];
            const float lo = 1.0f - 2.0f * float(j + 1) / kObjects, hi = 1.0f - 2.0f * float(j) / kObjects;
            drawHeart(k, p, float(p.x), float(p.y), float(p.z), ax, ay, float(kCamZ), lo, hi,
                      k == 0 ? mapTex_.tex : envCol_[0].tex, true);
        }
        gl::Disable(GL_CULL_FACE);
        gl::Disable(GL_DEPTH_TEST);
        screenMsaa_.resolve(resolved_);
    }

    bool ok_ = false;
    // the ini
    bool useBack_ = false, looping_ = false;
    std::string backName_;
    int backColour_ = 0;
    double step_ = 512, offset_[kObjects] = {}, lapCount_ = 1e9;
    float bumpIn_ = 0.01f, bumpOut_ = 81.5f;
    // objects and paths
    std::vector<HeartMesh> meshes_;
    std::vector<std::string> meshNames_;
    int meshOf_[kObjects] = {};
    vivid3d::NaturalSpline paths_[2][6];   // [0] transparent, [1] solid: x, y, z, a, b, c
    std::vector<uint8_t> behindAt_;   // behindMask of the whole counts of the segment
    // GL
    Shader heart_, convert_;
    Texture envIdx_, backIdx_, slopes_;
    TexTarget envCol_[2], back_, mapTex_, resolved_;
    MsaaTarget mapMsaa_, screenMsaa_;
    int samples_ = 1;
};

REGISTER_SCENE(WaterScene, "water");
