#include "demo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "timeline.h"

std::map<std::string, SceneFactory>& sceneRegistry() {
    static std::map<std::string, SceneFactory> r;
    return r;
}

void setBlend(Blend b) {
    switch (b) {
        case Blend::None: gl::Disable(GL_BLEND); break;
        case Blend::Alpha:
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Add:
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case Blend::Screen:
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        case Blend::Premul:
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Multiply:
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
    }
}

// ---------------------------------------------------------------- resources
namespace {
struct RawInfo { int w, h; bool pal; };
RawInfo rawInfo(const std::string& name, size_t size) {
    if (name == "final.raw") return {320, 350, false};
    if (name == "fontout.raw") return {1024, 64, false};
    if (name == "fontblur.raw") return {560, 96, false};
    if (name == "credits.raw") return {512, 32, false};
    if (name == "303b.raw") return {256, 38, false};
    if (name == "mustard.raw") return {256, 48, false};
    if (name == "smalfont.raw") return {288, 32, false};
    switch (size) {
        case 64768: return {320, 200, true};
        case 66304: return {256, 256, true};
        case 131840: return {256, 512, true};
        case 192768: return {320, 600, true};
        case 393984: return {256, 1536, true};
        case 65536: return {256, 256, false};
    }
    throw std::runtime_error("unknown raw size for " + name);
}

// Felzenszwalb squared-distance transform (1D) used for SDF fonts.
void edt1d(const float* f, int n, float* d, int* v, float* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; q++) {
        float s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
        while (s <= z[k]) {
            k--;
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        d[q] = (q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

std::vector<float> edt2d(const std::vector<uint8_t>& inside, int w, int h, bool target) {
    const float INF = 1e20f;
    std::vector<float> g(size_t(w) * h);
    for (size_t i = 0; i < g.size(); i++) g[i] = (inside[i] != 0) == target ? 0.0f : INF;
    int n = std::max(w, h);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) f[y] = g[size_t(y) * w + x];
        edt1d(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; y++) g[size_t(y) * w + x] = d[y];
    }
    for (int y = 0; y < h; y++) {
        edt1d(&g[size_t(y) * w], w, d.data(), v.data(), z.data());
        for (int x = 0; x < w; x++) g[size_t(y) * w + x] = std::sqrt(d[x]);
    }
    return g;
}

// Builds an upscaled signed-distance image from an 8-bit coverage bitmap.
Image makeSdf(const uint8_t* src, int sw, int sh, int stride, int scale, float spread) {
    int W = sw * scale, H = sh * scale;
    std::vector<uint8_t> inside(size_t(W) * H);
    auto at = [&](int x, int y) {
        x = std::clamp(x, 0, sw - 1);
        y = std::clamp(y, 0, sh - 1);
        return float(src[y * stride + x]);
    };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float fx = (x + 0.5f) / scale - 0.5f, fy = (y + 0.5f) / scale - 0.5f;
            int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
            float ax = fx - x0, ay = fy - y0;
            float v = (at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax) * (1 - ay) + (at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax) * ay;
            inside[size_t(y) * W + x] = v >= 128.0f;
        }
    auto dOut = edt2d(inside, W, H, true);   // distance to nearest inside pixel
    auto dIn = edt2d(inside, W, H, false);   // distance to nearest outside pixel
    Image im;
    im.w = W;
    im.h = H;
    im.rgba.resize(size_t(W) * H * 4);
    for (size_t i = 0; i < size_t(W) * H; i++) {
        float sd = (dOut[i] - dIn[i]) / (spread * scale);  // >0 outside
        uint8_t v = uint8_t(std::clamp(0.5f - 0.5f * sd, 0.0f, 1.0f) * 255.0f + 0.5f);
        im.rgba[i * 4 + 0] = im.rgba[i * 4 + 1] = im.rgba[i * 4 + 2] = im.rgba[i * 4 + 3] = v;
    }
    return im;
}
}  // namespace

Texture& Ctx::tex(const std::string& key) {
    auto it = textures.find(key);
    if (it != textures.end()) return it->second;
    // key "name" or "name#k" (k-th 256x256 / 320x200 tile of a stacked image)
    std::string name = key, file;
    int tile = -1;
    size_t hash = key.find('#');
    if (hash != std::string::npos) { name = key.substr(0, hash); tile = std::stoi(key.substr(hash + 1)); }
    file = name + ".raw";
    const Bytes& raw = pak.get(file);
    RawInfo ri = rawInfo(file, raw.size());
    int tileH = ri.w == 320 ? 200 : 256;
    int y0 = tile >= 0 ? tile * tileH : 0;
    int rows = tile >= 0 ? tileH : ri.h;
    Image im = ri.pal ? imagePaletted(raw, ri.w, ri.h, y0, rows) : imageGray(raw, ri.w, ri.h, y0, rows);
    Texture& t = textures[key];
    t.create(im, true, true);
    return t;
}

int Ctx::videoFrames(const std::string& name) {
    auto it = videos.find(name);
    if (it == videos.end()) {
        auto v = std::make_unique<Video>();
        if (!v->load(dataPath(name + ".LZW"), name == "PARANOID")) throw std::runtime_error("video " + name);
        it = videos.emplace(name, std::move(v)).first;
    }
    return it->second->frames;
}

Texture& Ctx::video(const std::string& name, float frame) {
    int n = videoFrames(name);
    const Video& v = *videos[name];
    int f = std::clamp(int(std::floor(frame)), 0, n - 1);
    Texture& t = videoTex[name];
    if (!t.id) {
        t.createEmpty(v.w, v.h, GL_RGBA8, false);
        videoLoaded[name] = -1;
    }
    if (videoLoaded[name] != f) {
        t.update(v.frame(f));
        videoLoaded[name] = f;
    }
    return t;
}

const Mesh& Ctx::meshData(const std::string& name) {
    auto it = meshesData.find(name);
    if (it != meshesData.end()) return it->second;
    const Bytes& raw = pak.get(name + ".dat");
    Mesh m;
    if (name == "spikey") m = meshSpikey(raw);
    else if (name == "icosa") m = meshIcosa(raw);
    else m = meshFace(raw);
    return meshesData[name] = std::move(m);
}

GpuMesh& Ctx::mesh(const std::string& name) {
    auto it = meshes.find(name);
    if (it != meshes.end()) return it->second;
    Mesh m = meshData(name);
    // keep the original proportions but bring everything to unit radius
    float r = 0;
    for (size_t i = 0; i < m.pos.size(); i += 3) r = std::max(r, std::sqrt(m.pos[i] * m.pos[i] + m.pos[i + 1] * m.pos[i + 1] + m.pos[i + 2] * m.pos[i + 2]));
    for (auto& p : m.pos) p /= r;
    GpuMesh& g = meshes[name];
    g.create(m);
    return g;
}

Font& Ctx::font(const std::string& name) {
    auto it = fonts.find(name);
    if (it != fonts.end()) return it->second;
    Font& f = fonts[name];
    const int scale = 4;
    if (name == "lyrics" || name == "outline") {
        bool lyr = name == "lyrics";
        const Bytes& raw = pak.get(lyr ? "fontblur.raw" : "fontout.raw");
        int w = lyr ? 560 : 1024;
        int y0 = lyr ? 26 : 4, rows = lyr ? 40 : 56;
        Image sdf = makeSdf(raw.data() + y0 * w, w, rows, w, scale, lyr ? 3.0f : 2.0f);
        f.tex.create(sdf, false, true);
        f.cellH = rows;
        // measure glyph extents from empty columns (over the crisp band)
        std::vector<std::pair<int, int>> runs;
        bool in = false;
        int s = 0;
        for (int x = 0; x <= w; x++) {
            int c = 0;
            if (x < w) for (int y = 0; y < rows; y++) c = std::max<int>(c, raw[(y0 + y) * w + x]);
            if (c > 40 && !in) { s = x; in = true; }
            else if (c <= 40 && in) { runs.emplace_back(s, x); in = false; }
        }
        char base = lyr ? 'A' : 'a';
        for (size_t i = 0; i < runs.size() && i < 26; i++) {
            Font::Glyph g;
            g.u0 = float(runs[i].first - 1) / w;
            g.u1 = float(runs[i].second + 1) / w;
            g.v0 = 0;
            g.v1 = 1;
            g.w = runs[i].second - runs[i].first + 2;
            f.glyphs[char(base + i)] = g;
            f.glyphs[char(lyr ? std::tolower(base + int(i)) : std::toupper(base + int(i)))] = g;
        }
        f.spaceW = lyr ? 12 : 16;
    } else if (name == "credits") {
        // credits.raw holds six whole words, addressed as "0".."5" from left to right
        const Bytes& raw = pak.get("credits.raw");
        Image sdf = makeSdf(raw.data(), 512, 32, 512, scale, 2.5f);
        f.tex.create(sdf, false, true);
        f.cellH = 32;
        struct W { int x0, x1; };
        // word boundaries measured from the image (see tools/)
        static const W words[] = {{0, 74}, {74, 167}, {168, 266}, {266, 356}, {356, 400}, {400, 508}};
        int ci = 0;
        for (const W& wd : words) {
            Font::Glyph g{float(wd.x0) / 512, 0, float(wd.x1) / 512, 1, wd.x1 - wd.x0};
            f.glyphs[char('0' + ci++)] = g;  // addressed as "0".."5"
        }
    }
    return f;
}

void Ctx::drawImage(const Texture& t, float x, float y, float w_, float h_, float alpha, float u0, float v0, float u1,
                    float v1, float angle, Vec3 tint, bool bicubic, bool grayAlpha) {
    imageShader.use();
    t.bind(0);
    imageShader.set("tex", 0);
    imageShader.set("rect", x, y, w_, h_);
    imageShader.set("uvRect", u0, v0, u1, v1);
    imageShader.set("angle", angle);
    imageShader.set("aspect", aspect);
    imageShader.set("tint", tint.x, tint.y, tint.z, alpha);
    imageShader.set("mode", bicubic ? 1 : (grayAlpha ? 2 : 0));
    drawQuad();
}

void Ctx::drawTarget(const Target& t, float alpha) {
    imageShader.use();
    t.bindTex(0);
    imageShader.set("tex", 0);
    imageShader.set("rect", 0.0f, 0.0f, 1.0f, 1.0f);
    imageShader.set("uvRect", 0.0f, 1.0f, 1.0f, 0.0f);
    imageShader.set("angle", 0.0f);
    imageShader.set("aspect", aspect);
    imageShader.set("tint", 1.0f, 1.0f, 1.0f, alpha);
    imageShader.set("mode", 0);
    drawQuad();
}

float Ctx::textWidth(const std::string& fname, const std::string& s, float height, float tracking, float stretchX) {
    Font& f = font(fname);
    float px = height / f.cellH;  // frame-height units per font pixel
    float wsum = 0;
    for (char ch : s) {
        auto it = f.glyphs.find(ch);
        wsum += (it == f.glyphs.end() ? f.spaceW : it->second.w) * px * stretchX + tracking * height;
    }
    return wsum / aspect;  // in frame-width units
}

void Ctx::drawText(const std::string& fname, const std::string& s, float cx, float cy, float height, float alpha, Vec3 color,
                   float tracking, float stretchX) {
    Font& f = font(fname);
    float total = textWidth(fname, s, height, tracking, stretchX);
    float x = cx - total * 0.5f;
    float px = height / f.cellH;
    imageShader.use();
    f.tex.bind(0);
    imageShader.set("tex", 0);
    imageShader.set("angle", 0.0f);
    imageShader.set("aspect", aspect);
    imageShader.set("tint", color.x, color.y, color.z, alpha);
    imageShader.set("mode", 3);
    for (char ch : s) {
        auto it = f.glyphs.find(ch);
        float gw = ((it == f.glyphs.end() ? f.spaceW : it->second.w) * px * stretchX) / aspect;
        if (it != f.glyphs.end()) {
            const Font::Glyph& g = it->second;
            imageShader.set("rect", x, cy - height * 0.5f, gw, height);
            imageShader.set("uvRect", g.u0, g.v0, g.u1, g.v1);
            drawQuad();
        }
        x += gw + tracking * height / aspect;
    }
}

void Ctx::flash(float amount, Vec3 color) {
    if (amount <= 0.001f) return;
    setBlend(Blend::Alpha);
    flashShader.use();
    flashShader.set("color", color.x, color.y, color.z, std::min(1.0f, amount));
    drawFullscreen();
}

void Ctx::rect(float x, float y, float w_, float h_, Vec3 color, float alpha) {
    setBlend(Blend::Alpha);
    drawImage(white(), x, y, w_, h_, alpha, 0, 0, 1, 1, 0, color);
}

Texture& Ctx::white() {
    auto it = textures.find("#white");
    if (it != textures.end()) return it->second;
    Image im;
    im.w = im.h = 4;
    im.rgba.assign(64, 255);
    Texture& t = textures["#white"];
    t.create(im, false, false);
    return t;
}

void Ctx::subViewport(float x, float y, float w_, float h_, bool clear, Vec3 cc) {
    int px = int(x * out->w + 0.5f), pw = int(w_ * out->w + 0.5f);
    int ph = int(h_ * out->h + 0.5f), py = out->h - int(y * out->h + 0.5f) - ph;
    gl::Viewport(px, py, pw, ph);
    gl::Enable(GL_SCISSOR_TEST);
    gl::Scissor(px, py, pw, ph);
    if (clear) {
        gl::ClearColor(cc.x, cc.y, cc.z, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

void Ctx::resetViewport() {
    gl::Disable(GL_SCISSOR_TEST);
    gl::Viewport(0, 0, out->w, out->h);
}

// ---------------------------------------------------------------- demo
static const char* kImageVS = R"(
uniform vec4 rect;      // x, y, w, h in frame units (y down)
uniform vec4 uvRect;    // u0, v0, u1, v1
uniform float angle;
uniform float aspect;
out vec2 uv;
void main() {
    vec2 c = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    uv = mix(uvRect.xy, uvRect.zw, c);
    vec2 p = rect.xy + c * rect.zw;          // frame units
    vec2 ctr = rect.xy + 0.5 * rect.zw;
    vec2 d = (p - ctr) * vec2(aspect, 1.0);
    d = mat2(cos(angle), sin(angle), -sin(angle), cos(angle)) * d;
    p = ctr + d / vec2(aspect, 1.0);
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

static const char* kImageFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform vec4 tint;
uniform int mode;   // 0 rgba, 1 bicubic rgba, 2 gray as alpha, 3 sdf text
void main() {
    if (mode == 3) {
        float d = texture(tex, uv).r;
        float w = max(fwidth(d) * 0.75, 1e-4);
        float a = smoothstep(0.5 - w, 0.5 + w, d);
        o = vec4(tint.rgb, tint.a * a);
        return;
    }
    vec4 c = mode == 1 ? texBicubic(tex, uv) : texture(tex, uv);
    if (mode == 2) { o = vec4(tint.rgb * c.r, tint.a * c.r); return; }
    o = vec4(c.rgb * tint.rgb, c.a * tint.a);
}
)";

static const char* kFlashFS = R"(
out vec4 o;
uniform vec4 color;
void main() { o = color; }
)";

bool Demo::init(int w, int h, std::string* err) {
    try {
        if (!ctx.pak.load(dataPath("MUSTARD.PAK"))) throw std::runtime_error("MUSTARD.PAK not found or invalid");
        std::string exeErr;
        if (!ctx.exe.load(dataPath("303.EXE"), &exeErr)) throw std::runtime_error(exeErr);
        if (!ctx.imageShader.build(kImageVS, kImageFS, "image")) throw std::runtime_error("image shader");
        if (!ctx.flashShader.build(kFullscreenVS, kFlashFS, "flash")) throw std::runtime_error("flash shader");
        resize(w, h);
        for (const auto& c : timeline()) {
            Cue cue{c.start, c.end, c.scene};
            auto it = scenes_.find(c.scene);
            if (it == scenes_.end()) {
                auto f = sceneRegistry().find(c.scene);
                if (f == sceneRegistry().end()) throw std::runtime_error("unknown scene " + c.scene);
                std::unique_ptr<Scene> s(f->second());
                s->init(ctx);
                it = scenes_.emplace(c.scene, std::move(s)).first;
            }
            cue.inst = it->second.get();
            cues_.push_back(cue);
        }
        // warm up lazily created resources so playback does not stall on first use
        for (const char* v : {"ALEX", "HARL", "PARANOID", "VS"}) ctx.videoFrames(v);
        for (const char* f : {"lyrics", "outline", "credits"}) ctx.font(f);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

void Demo::resize(int w, int h) {
    ctx.w = w;
    ctx.h = h;
    ctx.aspect = float(w) / float(h);
    frame_.create(w, h, true);
    for (auto& s : ctx.scratch) s.create(w, h, true);
    for (auto& s : ctx.half) s.create(w / 2, h / 2, false);
}

float Demo::length() const { return cues_.empty() ? 0.0f : cues_.back().end; }

const Target& Demo::render(float T, float dt) {
    ctx.T = T;
    ctx.dt = dt;
    ctx.out = &frame_;
    frame_.bind();
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (Cue& c : cues_) {
        if (T >= c.start && T < c.end) {
            ctx.out = &frame_;
            frame_.bind();
            setBlend(Blend::None);
            gl::Disable(GL_DEPTH_TEST);
            gl::Disable(GL_CULL_FACE);
            c.inst->render(ctx, T - c.start, c.end - c.start);
        }
    }
    frame_.bind();
    gl::Disable(GL_DEPTH_TEST);
    drawOverlays(ctx, T);
    setBlend(Blend::None);
    return frame_;
}
