// The SurRender engine's pixels on the GPU (engine.h): the six rasterizers 0x412db0, 0x414e5c, 0x412264, 0x41669c,
// 0x416dc8, 0x4172e0, the billboards' 0x415dbc / 0x415dc8 and the 2D bitmaps 0x415df0 (types 0x415fe8, 0x416174,
// 0x4164ec, 0x416324), drawn from the triangle stream the CPU pipeline builds (engine.cpp) in the painter's order,
// with MSAA, into ctx.out.
//
// The original's pixel is an index: table lookups of the nearest texel at the level floor((shade + 1) / 16). Here
// each fragment looks up the four texels around its sample point (each through the tables at the two table rows
// around the continuous level) and blends the colours; Fidelity::filter = false keeps one texel and one row. The
// rasterizers that read the background index (a table indexed by [source][background]) cannot be evaluated in a
// shader; each table row is fitted per colour channel as out = b * background + s over the palette's 256 colours
// (least squares), and the fragment writes s and b for dual-source blending (out = s + b * dst). The fits are also
// what the filtered taps and levels blend.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "demo.h"
#include "inside/engine.h"
#include "inside/engine_impl.h"
#include "inside/indexed.h"

#ifndef GL_SRC1_COLOR
#define GL_SRC1_COLOR 0x88F9
#endif
#ifndef GL_SHORT
#define GL_SHORT 0x1402
#endif

namespace inside::sr {

using namespace detail;

namespace {

const char* kSceneVS = R"(
layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aUV;
layout(location = 2) in vec2 aLev;
layout(location = 3) in ivec4 aA;
layout(location = 4) in ivec4 aB;
INTERP centroid out vec4 vUV;
INTERP centroid out vec2 vLev;
flat out ivec4 vA;
flat out ivec4 vB;
void main() {
    gl_Position = aPos;
    vUV = aUV;
    vLev = aLev;
    vA = aA;
    vB = aB;
}
)";

// Shared by the scene and the bitmap shaders: palette, tables, fits, taps, table rows.
const char* kCommonFS = R"(
uniform sampler2D pal;      // 256 x 1 colours
uniform sampler2D fits;     // 256 x 2n: row 2t = b, row 2t + 1 = s of table t, column = the table's row
uniform int filt;           // 0: the nearest texel and floor(level) (the original), 1: four taps, continuous level
layout(location = 0, index = 0) out vec4 oS;
layout(location = 0, index = 1) out vec4 oB;

vec3 P(uint i) { return texelFetch(pal, ivec2(int(i), 0), 0).rgb; }
vec3 FB(int t, int row) { return texelFetch(fits, ivec2(row, 2 * t), 0).rgb; }
vec3 FS(int t, int row) { return texelFetch(fits, ivec2(row, 2 * t + 1), 0).rgb; }

// The table rows around a level (the rasterizers' row is floor(level); the remake blends the rows whose centres
// are around it).
void rowsOf(float l, int rows, out int r0, out int r1, out float f) {
    if (filt == 0) {
        r0 = r1 = clamp(int(floor(l)), 0, rows - 1);
        f = 0.0;
        return;
    }
    float x = clamp(l - 0.5, 0.0, float(rows - 1));
    r0 = int(floor(x));
    r1 = min(r0 + 1, rows - 1);
    f = x - float(r0);
}

// The texels around p (texel n covers [n, n + 1)) and their weights, or the one under p.
int taps(vec2 p, out ivec2 c[4], out float w[4]) {
    if (filt == 0) {
        c[0] = ivec2(floor(p));
        w[0] = 1.0;
        c[1] = c[2] = c[3] = c[0];
        w[1] = w[2] = w[3] = 0.0;
        return 1;
    }
    vec2 q = p - 0.5;
    vec2 i = floor(q);
    vec2 f = q - i;
    ivec2 b = ivec2(i);
    c[0] = b;
    c[1] = b + ivec2(1, 0);
    c[2] = b + ivec2(0, 1);
    c[3] = b + ivec2(1, 1);
    w[0] = (1.0 - f.x) * (1.0 - f.y);
    w[1] = f.x * (1.0 - f.y);
    w[2] = (1.0 - f.x) * f.y;
    w[3] = f.x * f.y;
    return 4;
}
)";

const char* kSceneFS = R"(
INTERP centroid in vec4 vUV;   // u, v (texels), eu, ev (texels of the second texture; billboards: w, h)
INTERP centroid in vec2 vLev;  // table rows of the two shade channels
flat in ivec4 vA;              // raster, texture, second texture, table 1
flat in ivec4 vB;              // table 2, rows of table 1, rows of table 2, log2 size (w | h << 4)
uniform usampler2DArray texs;
uniform usampler2DArray tabs;

uint TB(int t, int row, uint col) { return texelFetch(tabs, ivec3(int(col), row, t), 0).r; }
uint TX(int layer, ivec2 c) { return texelFetch(texs, ivec3(c, layer), 0).r; }

void main() {
    int raster = vA.x, tex = vA.y, tex2 = vA.z, t1 = vA.w, t2 = vB.x;
    ivec2 mask = ivec2((1 << (vB.w & 15)) - 1, (1 << (vB.w >> 4)) - 1);
    vec3 S = vec3(0.0), B = vec3(0.0);
    int a0, a1, b0, b1;
    float af, bf;
    rowsOf(vLev.x, vB.y, a0, a1, af);
    rowsOf(vLev.y, vB.z, b0, b1, bf);
    if (raster == 4) {                       // 0x416dc8: table1[level][background]
        S = mix(FS(t1, a0), FS(t1, a1), af);
        B = mix(FB(t1, a0), FB(t1, a1), af);
    } else {
        ivec2 c[4];
        float w[4];
        int n = taps(vUV.xy, c, w);
        bool sprite = raster >= 6;
        ivec2 lim = max(ivec2(vUV.zw + 0.5) - 1, ivec2(0));
        for (int k = 0; k < n; k++) {
            ivec2 tc = sprite ? clamp(c[k], ivec2(0), lim) : (c[k] & mask);
            uint i = TX(tex, tc);
            vec3 s, b = vec3(0.0);
            if (raster == 0 || raster == 6) {                // 0x412db0, 0x415dbc: tex[u, v]
                s = P(i);
            } else if (raster == 1) {                        // 0x414e5c: table1[level][tex[u, v]]
                s = mix(P(TB(t1, a0, i)), P(TB(t1, a1, i)), af);
            } else if (raster == 2) {                        // 0x412264: table2[level2][table1[level][tex[u, v]]]
                uint j0 = TB(t1, a0, i), j1 = TB(t1, a1, i);
                vec3 c0 = mix(P(TB(t2, b0, j0)), P(TB(t2, b1, j0)), bf);
                vec3 c1 = mix(P(TB(t2, b0, j1)), P(TB(t2, b1, j1)), bf);
                s = mix(c0, c1, af);
            } else if (raster == 3 || raster == 7) {         // 0x41669c, 0x415dc8: table1[tex[u, v]][background]
                s = FS(t1, int(i));
                b = FB(t1, int(i));
            } else {                                         // 0x4172e0: table1[tex2[eu, ev]][tex[u, v]]
                ivec2 e[4];
                float ew[4];
                int m = taps(vUV.zw, e, ew);
                s = vec3(0.0);
                for (int q = 0; q < m; q++) s += ew[q] * P(TB(t1, int(TX(tex2, e[q] & 255)), i));
            }
            S += w[k] * s;
            B += w[k] * b;
        }
    }
    oS = vec4(S, 1.0);
    oB = vec4(B, 1.0);
}
)";

const char* kBitmapVS = R"(
uniform vec4 rect;      // the quad, VGA coordinates
uniform vec2 origin;    // the bitmap's top-left corner
uniform vec2 frame;     // 320, 200
out vec2 vP;            // texel coordinates
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vec2 p = rect.xy + c * rect.zw;
    vP = p - origin;
    gl_Position = vec4(2.0 * p.x / frame.x - 1.0, 1.0 - 2.0 * p.y / frame.y, 0.0, 1.0);
}
)";

const char* kBitmapFS = R"(
in vec2 vP;
uniform usampler2D bmp;
uniform int type;       // 1 copy, 2 table[src][dst], 3 copy where src != key, 4 table where src != key
uniform int table;
uniform int key;
// Fidelity::exact with nearest texels (the check at 320 x 200): the background's palette index from a copy of the
// frame (the palette colour nearest to it), and the table looked up as the original does.
uniform int exactBg;
uniform sampler2D bg;
uniform usampler2DArray tabs;
uint bgIndex() {
    vec3 c = texelFetch(bg, ivec2(gl_FragCoord.xy), 0).rgb;
    uint best = 0u;
    float bd = 1e9;
    for (int i = 0; i < 256; i++) {
        vec3 e = P(uint(i)) - c;
        float d = dot(e, e);
        if (d < bd) { bd = d; best = uint(i); }
    }
    return best;
}
void main() {
    ivec2 sz = textureSize(bmp, 0);
    if (exactBg != 0) {
        ivec2 t = ivec2(floor(vP));
        uint i = texelFetch(bmp, clamp(t, ivec2(0), sz - 1), 0).r;
        if (type == 4 && int(i) == key) discard;
        oS = vec4(P(texelFetch(tabs, ivec3(int(bgIndex()), int(i), table), 0).r), 1.0);
        oB = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    ivec2 c[4];
    float w[4];
    int n = taps(vP, c, w);
    vec3 S = vec3(0.0), B = vec3(0.0);
    for (int k = 0; k < n; k++) {
        bool out_ = any(lessThan(c[k], ivec2(0))) || any(greaterThanEqual(c[k], sz));
        vec3 s = vec3(0.0), b = vec3(1.0);   // outside or the key: the background stays
        if (!(out_ && type >= 3)) {
            uint i = texelFetch(bmp, clamp(c[k], ivec2(0), sz - 1), 0).r;
            if (type >= 3 && int(i) == key) {
            } else if (type == 1 || type == 3) {
                s = P(i);
                b = vec3(0.0);
            } else {
                s = FS(table, int(i));
                b = FB(table, int(i));
            }
        }
        S += w[k] * s;
        B += w[k] * b;
    }
    oS = vec4(S, 1.0);
    oB = vec4(B, 1.0);
}
)";

const char* kCopyFS = R"(
uniform sampler2D src;
out vec4 o;
void main() { o = texelFetch(src, ivec2(gl_FragCoord.xy), 0); }
)";

std::string interp(bool perspective) {
    return perspective ? "#define INTERP smooth\n" : "#define INTERP noperspective\n";
}

}  // namespace

// GL objects shared by every part's renderer (Ctx::shared).
struct Gpu {
    explicit Gpu(Ctx&) {}
    ~Gpu() {
        if (msFbo) gl::DeleteFramebuffers(1, &msFbo);
        if (msColor) gl::DeleteRenderbuffers(1, &msColor);
        if (vbo) gl::DeleteBuffers(1, &vbo);
        if (vao) gl::DeleteVertexArrays(1, &vao);
    }
    Shader scene[2];               // [perspective]
    bool built[2] = {false, false};
    Shader bitmap, copy;
    bool bitmapBuilt = false, copyBuilt = false;
    GLuint vao = 0, vbo = 0;
    GLuint msFbo = 0, msColor = 0;
    int msW = 0, msH = 0, msSamples = 0;
    int maxSamples = -1;

    Shader& sceneShader(bool perspective) {
        int k = perspective ? 1 : 0;
        if (!built[k]) {
            built[k] = true;
            scene[k].build(interp(perspective) + kSceneVS, interp(perspective) + kCommonFS + kSceneFS,
                           perspective ? "engine (perspective)" : "engine (affine)");
        }
        return scene[k];
    }
    Shader& bitmapShader() {
        if (!bitmapBuilt) {
            bitmapBuilt = true;
            bitmap.build(kBitmapVS, std::string(kCommonFS) + kBitmapFS, "engine bitmap");
        }
        return bitmap;
    }
    Shader& copyShader() {
        if (!copyBuilt) {
            copyBuilt = true;
            copy.build(kFullscreenVS, kCopyFS, "engine copy");
        }
        return copy;
    }
    void vertexArray() {
        if (vao) return;
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vbo);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        const GLsizei st = sizeof(GpuVertex);
        gl::VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, pos));
        gl::VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, uv));
        gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(GpuVertex, lev));
        gl::VertexAttribIPointer(3, 4, GL_SHORT, st, (void*)offsetof(GpuVertex, a));
        gl::VertexAttribIPointer(4, 4, GL_SHORT, st, (void*)offsetof(GpuVertex, b));
        for (GLuint i = 0; i < 5; i++) gl::EnableVertexAttribArray(i);
        gl::BindVertexArray(0);
    }
    // The multisampled target, the size of ctx.out; returns the samples used (0: none).
    int msTarget(int w, int h, int samples) {
        if (maxSamples < 0) {
            GLint m = 0;
            gl::GetIntegerv(GL_MAX_SAMPLES, &m);
            maxSamples = m;
        }
        samples = std::min(samples, maxSamples);
        if (samples <= 1) return 0;
        if (msFbo && msW == w && msH == h && msSamples == samples) return samples;
        if (!msFbo) {
            gl::GenFramebuffers(1, &msFbo);
            gl::GenRenderbuffers(1, &msColor);
        }
        gl::BindRenderbuffer(GL_RENDERBUFFER, msColor);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16, w, h);   // clamped, as ctx.out
        gl::BindFramebuffer(GL_FRAMEBUFFER, msFbo);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("engine: %d-sample target %dx%d incomplete, drawing without MSAA", samples, w, h);
            maxSamples = 0;
            return 0;
        }
        msW = w;
        msH = h;
        msSamples = samples;
        return samples;
    }
};

struct Renderer::Impl {
    Ctx* ctx = nullptr;
    std::vector<std::vector<uint8_t>> texData;
    std::vector<TexInfo> texInfo;
    std::vector<std::vector<uint8_t>> tabData;
    std::vector<int> tabRows;
    std::vector<std::array<Vec3, 256>> pals;
    std::vector<Texture> palTex;
    std::vector<Material> mats;
    std::vector<Texture> bitmaps;
    struct FitSet {
        size_t tables = 0;
        std::vector<std::vector<RowFit>> rows;
        Texture tex;
    };
    std::vector<FitSet> fits;   // per palette
    GLuint texArray = 0, tabArray = 0;
    size_t texLayers = 0, tabLayers = 0;
    std::vector<GpuVertex> verts;

    ~Impl() {
        if (texArray) gl::DeleteTextures(1, &texArray);
        if (tabArray) gl::DeleteTextures(1, &tabArray);
        for (Texture& t : palTex) t.destroy();
        for (Texture& t : bitmaps) t.destroy();
        for (FitSet& f : fits) f.tex.destroy();
    }

    static void uploadArray(GLuint& id, int w, int h, const std::vector<std::vector<uint8_t>>& layers,
                            const std::vector<std::pair<int, int>>& sizes) {
        std::vector<uint8_t> all(size_t(w) * h * std::max<size_t>(1, layers.size()), 0);
        for (size_t l = 0; l < layers.size(); l++)
            for (int y = 0; y < sizes[l].second; y++)
                std::memcpy(&all[(l * size_t(h) + size_t(y)) * size_t(w)], &layers[l][size_t(y) * sizes[l].first],
                            size_t(sizes[l].first));
        if (!id) gl::GenTextures(1, &id);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, id);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8UI, w, h, GLsizei(std::max<size_t>(1, layers.size())), 0,
                       GL_RED_INTEGER, GL_UNSIGNED_BYTE, all.data());
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    void upload() {
        if (texLayers != texData.size() || !texArray) {
            int w = 256, h = 256;   // at least the second texture's 256 x 256
            std::vector<std::pair<int, int>> sz;
            for (const TexInfo& t : texInfo) {
                w = std::max(w, t.w);
                h = std::max(h, t.h);
                sz.push_back({t.w, t.h});
            }
            uploadArray(texArray, w, h, texData, sz);
            texLayers = texData.size();
        }
        if (tabLayers != tabData.size() || !tabArray) {
            std::vector<std::pair<int, int>> sz;
            for (int r : tabRows) sz.push_back({256, r});
            uploadArray(tabArray, 256, 256, tabData, sz);
            tabLayers = tabData.size();
        }
    }

    FitSet& fitSet(int palette) {
        FitSet& f = fits[size_t(palette)];
        if (f.tables == tabData.size() && f.tex.id) return f;
        f.rows.resize(tabData.size());
        std::vector<float> px(size_t(256) * 4 * 2 * std::max<size_t>(1, tabData.size()), 0.0f);
        for (size_t t = 0; t < tabData.size(); t++) {
            if (f.rows[t].empty()) f.rows[t] = fitRows(tabData[t], tabRows[t], pals[size_t(palette)]);
            for (int r = 0; r < 256; r++) {
                // rows past the table's end: the identity
                RowFit rf = r < int(f.rows[t].size()) ? f.rows[t][size_t(r)] : RowFit{Vec3(1, 1, 1), Vec3(0, 0, 0)};
                float* b = &px[((2 * t) * 256 + size_t(r)) * 4];
                float* s = &px[((2 * t + 1) * 256 + size_t(r)) * 4];
                b[0] = rf.b.x; b[1] = rf.b.y; b[2] = rf.b.z; b[3] = 1;
                s[0] = rf.s.x; s[1] = rf.s.y; s[2] = rf.s.z; s[3] = 1;
            }
        }
        f.tex.createRaw(256, int(2 * std::max<size_t>(1, tabData.size())), GL_RGBA32F, GL_RGBA, GL_FLOAT, px.data(),
                        false, false, false);
        f.tables = tabData.size();
        return f;
    }

    // Least squares per row and channel: table[r][j] ~ clamp(b * pal[j] + s, 0, 1) over the 256 background colours
    // j; entries where the table saturates at white or black and the line is already past it are left out of the
    // refit (the frame clamps when it is shown). tools/blend_fit.py does the same and reports the errors.
    static void fitLine(const double* x, const double* y, double& b, double& s) {
        bool use[256];
        std::fill(use, use + 256, true);
        b = s = 0;
        for (int it = 0; it < 4; it++) {
            double n = 0, sx = 0, sy = 0;
            for (int j = 0; j < 256; j++)
                if (use[j]) { n++; sx += x[j]; sy += y[j]; }
            if (n < 2) break;
            double xm = sx / n, ym = sy / n, var = 0, cov = 0;
            for (int j = 0; j < 256; j++)
                if (use[j]) { var += (x[j] - xm) * (x[j] - xm); cov += (x[j] - xm) * (y[j] - ym); }
            b = var > 1e-12 ? cov / var : 0.0;
            s = ym - b * xm;
            bool same = true;
            for (int j = 0; j < 256; j++) {
                double p = b * x[j] + s;
                bool u = !((y[j] >= 1 - 1e-6 && p >= y[j]) || (y[j] <= 1e-6 && p <= y[j]));
                same = same && u == use[j];
                use[j] = u;
            }
            if (same) break;
        }
    }

    static std::vector<RowFit> fitRows(const std::vector<uint8_t>& t, int rows, const std::array<Vec3, 256>& pal) {
        std::vector<RowFit> out(static_cast<size_t>(rows));
        auto ch = [](const Vec3& v, int k) { return double(k == 0 ? v.x : k == 1 ? v.y : v.z); };
        double x[256], y[256];
        for (int r = 0; r < rows; r++) {
            double b[3], s[3], sq = 0, mx = 0;
            for (int k = 0; k < 3; k++) {
                for (int j = 0; j < 256; j++) {
                    x[j] = ch(pal[size_t(j)], k);
                    y[j] = ch(pal[t[size_t(r) * 256 + size_t(j)]], k);
                }
                fitLine(x, y, b[k], s[k]);
                for (int j = 0; j < 256; j++) {
                    double e = std::clamp(b[k] * x[j] + s[k], 0.0, 1.0) - y[j];
                    sq += e * e;
                    mx = std::max(mx, std::abs(e));
                }
            }
            out[size_t(r)] = {Vec3(float(b[0]), float(b[1]), float(b[2])), Vec3(float(s[0]), float(s[1]), float(s[2])),
                              float(std::sqrt(sq / 768) * 63), float(mx * 63)};
        }
        return out;
    }

    void bindCommon(Shader& sh, int palette, bool needFits) {
        gl::ActiveTexture(GL_TEXTURE2);
        gl::BindTexture(GL_TEXTURE_2D, palTex[size_t(palette)].id);
        gl::ActiveTexture(GL_TEXTURE3);
        gl::BindTexture(GL_TEXTURE_2D, needFits ? fitSet(palette).tex.id : palTex[size_t(palette)].id);
        sh.set("pal", 2);
        sh.set("fits", 3);
        sh.set("filt", fidelity().filter ? 1 : 0);
    }
};

Renderer::Renderer() : d_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

void Renderer::init(Ctx& c) {
    d_->ctx = &c;
    // the shaders now rather than at the first frame
    Gpu& g = c.shared<Gpu>();
    g.sceneShader(fidelity().perspective);
    g.bitmapShader();
    g.copyShader();
}

int Renderer::texture(std::span<const uint8_t> px, int w, int h, int pitch) {
    if (pitch <= 0) pitch = w;
    std::vector<uint8_t> d(size_t(w) * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t o = size_t(y) * pitch + size_t(x);
            d[size_t(y) * w + size_t(x)] = o < px.size() ? px[o] : 0;
        }
    TexInfo t;
    t.w = w;
    t.h = h;
    while ((1 << t.wlog) < w) t.wlog++;
    while ((1 << t.hlog) < h) t.hlog++;
    d_->texData.push_back(std::move(d));
    d_->texInfo.push_back(t);
    return int(d_->texData.size()) - 1;
}

int Renderer::table(std::span<const uint8_t> t) {
    int rows = int(std::min<size_t>(256, t.size() / 256));
    std::vector<uint8_t> d(size_t(rows) * 256);
    std::memcpy(d.data(), t.data(), d.size());
    d_->tabData.push_back(std::move(d));
    d_->tabRows.push_back(std::max(rows, 1));
    if (rows == 0) d_->tabData.back().assign(256, 0);
    return int(d_->tabData.size()) - 1;
}

int Renderer::palette(const std::array<Vec3, 256>& colours) {
    d_->pals.push_back(colours);
    d_->palTex.push_back(uploadPalette(colours));
    d_->fits.emplace_back();
    return int(d_->pals.size()) - 1;
}

int Renderer::material(const Material& m) {
    d_->mats.push_back(m);
    return int(d_->mats.size()) - 1;
}

Material& Renderer::materialAt(int handle) { return d_->mats.at(size_t(handle)); }

Bitmap Renderer::bitmap(std::span<const uint8_t> px, int pitch, int w, int h) {
    std::vector<uint8_t> d(size_t(w) * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t o = size_t(y) * pitch + size_t(x);
            d[size_t(y) * w + size_t(x)] = o < px.size() ? px[o] : 0;
        }
    Bitmap b;
    b.w = w;
    b.h = h;
    b.id = int(d_->bitmaps.size());
    d_->bitmaps.push_back(uploadIndices(d, w, h));
    gl::BindTexture(GL_TEXTURE_2D, d_->bitmaps.back().id);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return b;
}

const std::vector<RowFit>& Renderer::fit(int table, int palette) {
    return d_->fitSet(palette).rows.at(size_t(table));
}

void Renderer::render(Ctx& c, Scene& s, int palette, int clearIndex) {
    Impl& d = *d_;
    const Fidelity& f = fidelity();
    Resources res{&d.mats, &d.texInfo, &d.tabRows};
    bool blended = false;
    buildFrame(s, res, f, d.verts, blended);
    d.upload();

    Gpu& g = c.shared<Gpu>();
    Target& out = *c.out;
    const int w = out.w, h = out.h;
    // the viewport in output pixels (GL: from the bottom)
    int vx0 = int(std::lround(double(std::max(s.x0, 0)) * w / s.width));
    int vx1 = int(std::lround(double(std::min(s.x1, s.width)) * w / s.width));
    int vy0 = int(std::lround(double(std::max(s.y0, 0)) * h / s.height));
    int vy1 = int(std::lround(double(std::min(s.y1, s.height)) * h / s.height));
    bool full = vx0 == 0 && vy0 == 0 && vx1 == w && vy1 == h;

    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    int samples = g.msTarget(w, h, f.samples);
    if (samples) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, g.msFbo);
        gl::Viewport(0, 0, w, h);
        if (clearIndex < 0 || !full) {   // start from what ctx.out holds
            Shader& cp = g.copyShader();
            cp.use();
            out.bindTex(0);
            cp.set("src", 0);
            drawFullscreen();
        }
    } else {
        out.bind();
    }
    gl::Enable(GL_SCISSOR_TEST);
    gl::Scissor(vx0, h - vy1, vx1 - vx0, vy1 - vy0);
    if (clearIndex >= 0) {   // 0x41127c: the viewport to one palette index
        const Vec3& col = d.pals[size_t(palette)][size_t(clearIndex & 255)];
        gl::ClearColor(col.x, col.y, col.z, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
    }
    if (!d.verts.empty()) {
        g.vertexArray();
        gl::BindVertexArray(g.vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, g.vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(d.verts.size() * sizeof(GpuVertex)), d.verts.data(),
                       GL_STREAM_DRAW);
        Shader& sh = g.sceneShader(f.perspective);
        sh.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, d.texArray);
        gl::ActiveTexture(GL_TEXTURE1);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, d.tabArray);
        sh.set("texs", 0);
        sh.set("tabs", 1);
        d.bindCommon(sh, palette, blended);
        gl::Enable(GL_BLEND);
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFuncSeparate(GL_ONE, GL_SRC1_COLOR, GL_ONE, GL_ZERO);   // out = s + b * background
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(d.verts.size()));
        gl::BindVertexArray(0);
        gl::ActiveTexture(GL_TEXTURE1);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, 0);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    gl::Disable(GL_SCISSOR_TEST);
    setBlend(Blend::None);
    if (samples) {   // resolve into ctx.out
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, g.msFbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, out.fbo);
        gl::BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    out.bind();
}

void Renderer::drawBitmap(Ctx& c, const Bitmap& b, float x, float y, int palette) {
    Impl& d = *d_;
    if (b.id < 0 || b.w <= 0 || b.h <= 0) return;
    const Fidelity& f = fidelity();
    if (f.exact) {   // 0x415df0 truncates the position
        x = std::trunc(x);
        y = std::trunc(y);
    }
    Gpu& g = c.shared<Gpu>();
    Shader& sh = g.bitmapShader();
    bool table = b.type == 2 || b.type == 4;
    // the original's mode: the background's indices from a copy of the frame (see kBitmapFS)
    bool exactBg = table && f.exact && !f.filter;
    TargetPool::Lease bgCopy;
    if (exactBg) {
        d.upload();
        bgCopy = c.temp();
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, c.out->fbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, bgCopy->fbo);
        gl::BlitFramebuffer(0, 0, c.out->w, c.out->h, 0, 0, bgCopy->w, bgCopy->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    c.out->bind();
    sh.use();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, d.bitmaps[size_t(b.id)].id);
    sh.set("bmp", 0);
    d.bindCommon(sh, palette, table && !exactBg);
    sh.set("exactBg", exactBg ? 1 : 0);
    if (exactBg) {
        gl::ActiveTexture(GL_TEXTURE0 + 4);
        gl::BindTexture(GL_TEXTURE_2D, bgCopy->tex);
        gl::ActiveTexture(GL_TEXTURE0 + 5);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, d.tabArray);
    }
    sh.set("bg", 4);
    sh.set("tabs", 5);
    sh.set("type", b.type);
    sh.set("table", std::max(b.table, 0));
    sh.set("key", b.key);
    // with filtered taps the transparent types fade out over half a texel past their edge
    float m = (f.filter && b.type >= 3) ? 1.0f : 0.0f;
    sh.set("rect", x - m, y - m, float(b.w) + 2 * m, float(b.h) + 2 * m);
    sh.set("origin", x, y);
    sh.set("frame", 320.0f, 200.0f);
    gl::Enable(GL_BLEND);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFuncSeparate(GL_ONE, GL_SRC1_COLOR, GL_ONE, GL_ZERO);
    drawQuad();
    setBlend(Blend::None);
    gl::ActiveTexture(GL_TEXTURE0 + 5);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    gl::ActiveTexture(GL_TEXTURE0);
}

}  // namespace inside::sr
