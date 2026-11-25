// Demo framework (demo.h): blend modes, the target pool, drawing helpers, the placeholder of a scene that is not
// built, and the frame: the palette of refresh r, then the scene of the segment at r.
#include "demo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "exe.h"
#include "music.h"

std::map<std::string, SceneFactory>& sceneRegistry() {
    static std::map<std::string, SceneFactory> r;
    return r;
}

void setBlend(Blend b) {
    gl::BlendEquation(GL_FUNC_ADD);
    switch (b) {
        case Blend::None: gl::Disable(GL_BLEND); return;
        case Blend::Alpha:
            gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Add: gl::BlendFunc(GL_SRC_ALPHA, GL_ONE); break;
        case Blend::Screen: gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
        case Blend::Premul: gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
        case Blend::Multiply: gl::BlendFunc(GL_DST_COLOR, GL_ZERO); break;
        case Blend::Max: gl::BlendFunc(GL_ONE, GL_ONE); gl::BlendEquation(GL_MAX); break;
        case Blend::Min: gl::BlendFunc(GL_ONE, GL_ONE); gl::BlendEquation(GL_MIN); break;
    }
    gl::Enable(GL_BLEND);
}

// ---------------------------------------------------------------- target pool
TargetPool::Lease TargetPool::get(int w, int h, bool depth, GLenum fmt) {
    for (auto& e : entries_)
        if (!e->busy && e->t.w == w && e->t.h == h && e->depth == depth && e->fmt == fmt) {
            e->busy = true;
            return Lease(&e->t, &e->busy);
        }
    auto e = std::make_unique<Entry>();
    e->t.create(w, h, depth, fmt);
    e->depth = depth;
    e->fmt = fmt;
    e->busy = true;
    entries_.push_back(std::move(e));
    return Lease(&entries_.back()->t, &entries_.back()->busy);
}

void TargetPool::clear() {
    for (auto& e : entries_) e->t.destroy();
    entries_.clear();
}

// ---------------------------------------------------------------- ctx
TargetPool::Lease Ctx::temp(bool depth, float scale, GLenum fmt) {
    return pool.get(std::max(1, int(std::lround(w * scale))), std::max(1, int(std::lround(h * scale))), depth, fmt);
}

TargetPool::Lease Ctx::tempSized(int tw, int th, bool depth, GLenum fmt) { return pool.get(tw, th, depth, fmt); }

bool Ctx::hasScene(const std::string& name) const { return scenes && scenes->count(name) != 0; }

namespace {
// A scene that is not built: a dim colour derived from its name, the name, the script line, the music position, the
// refreshes since the segment started and the capture's record.
void placeholder(Ctx& c, const std::string& name, double r) {
    unsigned hsh = 2166136261u;
    for (char ch : name) hsh = (hsh ^ uint8_t(ch)) * 16777619u;
    c.clear(Vec3(0.12f + 0.25f * ((hsh >> 0) & 255) / 255.0f, 0.12f + 0.25f * ((hsh >> 8) & 255) / 255.0f,
                 0.12f + 0.25f * ((hsh >> 16) & 255) / 255.0f));
    setBlend(Blend::None);
    char b[160];
    c.text(16, 16, 3, name, Vec3(1, 1, 1));
    std::pair<int, int> pos = c.seg && c.music ? c.music->song[c.seg->part - 1].positionAt(r) : std::pair<int, int>{0, 0};
    std::snprintf(b, sizeof b, "%d:%02d  REFRESH %.1f  SINCE %.1f", pos.first, pos.second, r, c.since(r));
    c.text(16, 44, 1.5f, b, Vec3(1, 1, 0.6f));
    if (c.seg && c.seg->run >= 0) {
        std::string t = c.timeline->line(c.seg->run).line.text;
        size_t a = t.find_first_not_of(' ');
        c.text(16, 58, 1.2f, t.substr(a == std::string::npos ? 0 : a, 50), Vec3(0.8f, 0.8f, 0.8f));
    }
    std::snprintf(b, sizeof b, "RECORD %d", c.captureRecord(r));
    c.text(16, 72, 1.5f, b, Vec3(0.8f, 0.8f, 0.8f));
}
}  // namespace

void Ctx::renderScene(const std::string& name, const vivid::Segment* s, double r, Target& t) {
    Target* saved = out;
    const vivid::Segment* savedSeg = seg;
    out = &t;
    seg = s;
    t.bind();
    auto it = scenes ? scenes->find(name) : decltype(scenes->end()){};
    if (scenes && it != scenes->end()) {
        depth++;
        it->second->render(*this, r);
        depth--;
    } else {
        placeholder(*this, name, r);
    }
    out = saved;
    seg = savedSeg;
    if (out) out->bind();
}

Mat4 Ctx::vgaProj() const { return Mat4::ortho(0, 320, 200, 0, -1, 1); }

static const char* kImageVS = R"(
uniform vec4 rect;   // x, y, w, h in VGA coordinates
uniform vec4 uvr;    // u0, v0, u1, v1
out vec2 uv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    uv = mix(uvr.xy, uvr.zw, c);
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
static const char* kImageFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform vec4 tint;   // rgb multiplier, alpha
void main() { vec4 c = texture(tex, uv); o = vec4(c.rgb * tint.rgb, c.a * tint.a); }
)";
static const char* kFlatFS = R"(
in vec2 uv;
out vec4 o;
uniform vec4 tint;
void main() { o = tint; }
)";
void Ctx::drawTexture(GLuint tex, float x, float y, float rw, float rh, float alpha, float u0, float v0, float u1,
                      float v1, Vec3 tint) {
    imageShader.use();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    imageShader.set("tex", 0);
    imageShader.set("rect", x, y, rw, rh);
    imageShader.set("uvr", u0, v0, u1, v1);
    imageShader.set("tint", tint.x, tint.y, tint.z, alpha);
    drawQuad();
}

void Ctx::drawTarget(const Target& t, float alpha) { drawTexture(t.tex, 0, 0, 320, 200, alpha, 0, 1, 1, 0); }

void Ctx::rect(float x, float y, float rw, float rh, Vec3 c, float alpha) {
    flatShader.use();
    flatShader.set("rect", x, y, rw, rh);
    flatShader.set("uvr", 0.0f, 0.0f, 1.0f, 1.0f);
    flatShader.set("tint", c.x, c.y, c.z, alpha);
    drawQuad();
}

void Ctx::clear(Vec3 c) {
    gl::ClearColor(c.x, c.y, c.z, 1);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

// A 5x7 font: 5 column bytes per character, bit 0 = top row.
static const uint8_t* glyph(char ch) {
    static const struct { char c; uint8_t col[5]; } kFont[] = {
        {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}}, {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
        {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}}, {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
        {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}}, {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
        {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}}, {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
        {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}}, {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}}, {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
        {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}}, {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
        {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}}, {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
        {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}}, {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
        {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
        {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}}, {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
        {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}}, {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
        {'X', {0x63, 0x14, 0x08, 0x14, 0x63}}, {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}}, {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
        {':', {0x00, 0x36, 0x36, 0x00, 0x00}}, {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
        {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}}, {'(', {0x00, 0x1C, 0x22, 0x41, 0x00}}, {')', {0x00, 0x41, 0x22, 0x1C, 0x00}},
        {'/', {0x20, 0x10, 0x08, 0x04, 0x02}}, {'=', {0x14, 0x14, 0x14, 0x14, 0x14}}, {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    };
    if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    for (const auto& g : kFont)
        if (g.c == ch) return g.col;
    return nullptr;
}

void Ctx::text(float x, float y, float size, const std::string& s, Vec3 color) {
    for (size_t i = 0; i < s.size(); i++) {
        const uint8_t* g = glyph(s[i]);
        if (!g) continue;
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (g[cx] >> cy & 1) rect(x + (float(i) * 6 + cx) * size, y + cy * size, size, size, color);
    }
}

// ---------------------------------------------------------------- demo
bool Demo::init(const VividExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err) {
    opt_ = o;
    ctx.exe = &exe;
    ctx.music = &music;
    ctx.scenes = &scenes_;
    ctx.pictures = &pictures_;
    if (!timeline_.build(exe, music, err)) return false;
    ctx.timeline = &timeline_;
    if (!ctx.imageShader.build(kImageVS, kImageFS, "image") || !ctx.flatShader.build(kImageVS, kFlatFS, "flat")) {
        if (err) *err = "shader build failed";
        return false;
    }
    ctx.paletteTex_ = vivid::uploadPalette(timeline_.palette(0));
    pictures_.init(&exe, o.artDir, o.art);
    resize(w, h);
    // every registered scene is created and initialised up front (the original's precalc)
    for (auto& [name, factory] : sceneRegistry()) {
        uint64_t t0 = SDL_GetTicksNS();
        scenes_[name].reset(factory());
        scenes_[name]->init(ctx);
        SDL_Log("scene %-9s init %.0f ms", name.c_str(), (SDL_GetTicksNS() - t0) * 1e-6);
    }
    for (const vivid::Segment& s : timeline_.segments())
        if (!scenes_.count(s.scene))
            SDL_Log("scene %-9s is not built: placeholder for refreshes %.0f-%.0f", s.scene.c_str(), s.start, s.end);
    return true;
}

void Demo::resize(int w, int h) {
    ctx.w = w;
    ctx.h = h;
    // 16-bit normalised: every draw clamps to 0..1, as the original's pixels are always palette colours
    frame_.create(w, h, true, GL_RGBA16);
    ctx.pool.clear();
}

void Demo::release() {
    scenes_.clear();
    ctx.scenes = nullptr;
    ctx.shared_.clear();
    ctx.pool.clear();
    pictures_.release();
    ctx.paletteTex_.destroy();
    frame_.destroy();
}

const Target& Demo::render(double r) {
    vivid::updatePalette(ctx.paletteTex_, timeline_.palette(r));
    ctx.out = &frame_;
    frame_.bind();
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    ctx.clear();
    if (const vivid::Segment* s = timeline_.segmentAt(r)) ctx.renderScene(s->scene, s, r, frame_);
    ctx.out = &frame_;
    return frame_;
}
