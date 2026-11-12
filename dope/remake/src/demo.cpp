// Demo framework (demo.h): blend modes, the target pool, the clocks and the capture's frame grid, picture drawing,
// the placeholder of a part that is not built, the part sequence (buildTimeline, timeline.cpp) and the frame: the
// cue at t, its set-up screen and held last frame, black elsewhere.
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

// ---------------------------------------------------------------- clocks
double CaptureClock::toCapture(double rec) const {
    if (sync.empty() || rec <= sync.front().first) return rec - (sync.empty() ? 0.0 : sync.front().first - sync.front().second);
    for (size_t i = 1; i < sync.size(); i++)
        if (rec <= sync[i].first) {
            const auto& a = sync[i - 1];
            const auto& b = sync[i];
            double f = b.first > a.first ? (rec - a.first) / (b.first - a.first) : 1.0;
            return a.second + f * (b.second - a.second);
        }
    return rec - (sync.back().first - sync.back().second);
}

int Ctx::position(double t) const { return music ? music->positionAt(t) : 0; }
double Ctx::positionTime(int pos) const { return music ? music->timeOfPosition(pos) : 0.0; }
double Ctx::tickTime(int k) const { return music ? music->timeOfTick(k) : 0.0; }

// ---------------------------------------------------------------- ctx
TargetPool::Lease Ctx::temp(bool depth, float scale, GLenum fmt) {
    return pool.get(std::max(1, int(std::lround(w * scale))), std::max(1, int(std::lround(h * scale))), depth, fmt);
}

TargetPool::Lease Ctx::tempSized(int tw, int th, bool depth, GLenum fmt) { return pool.get(tw, th, depth, fmt); }

bool Ctx::hasPart(const std::string& name) const { return scenes && scenes->count(name) != 0; }

namespace {
std::string posText(int pos) {
    char b[16];
    std::snprintf(b, sizeof b, "%02X:%02X", (pos >> 8) & 0xff, pos & 0xff);
    return b;
}

// A part that is not built: a dim colour derived from its name, the name, the music position, the retraces since
// its first per-retrace call, and the capture frame the remake corresponds to.
void placeholder(Ctx& c, const std::string& name, double t) {
    unsigned hsh = 2166136261u;
    for (char ch : name) hsh = (hsh ^ uint8_t(ch)) * 16777619u;
    c.clear(Vec3(0.12f + 0.25f * ((hsh >> 0) & 255) / 255.0f, 0.12f + 0.25f * ((hsh >> 8) & 255) / 255.0f,
                 0.12f + 0.25f * ((hsh >> 16) & 255) / 255.0f));
    setBlend(Blend::None);
    c.text(16, 16, 4, name, Vec3(1, 1, 1));
    char b[96];
    double r = c.retraces(t);
    std::snprintf(b, sizeof b, "%s  %.2f S  R %.0f%s", posText(c.position(t)).c_str(), t - c.start, std::floor(r),
                  r < 0 ? " SET-UP" : "");
    c.text(16, 52, 1.5f, b, Vec3(1, 1, 0.6f));
    std::snprintf(b, sizeof b, "MUSIC %.3f  FRAME %d", t, c.captureFrame(t));
    c.text(16, 68, 1.5f, b, Vec3(0.8f, 0.8f, 0.8f));
    // the progress through the part: a bar from load to end
    double f = c.end > c.load ? std::clamp((t - c.load) / (c.end - c.load), 0.0, 1.0) : 0.0;
    c.rect(16, 180, 288, 4, Vec3(0.3f, 0.3f, 0.3f));
    c.rect(16, 180, float(288 * f), 4, Vec3(1, 1, 1));
}
}  // namespace

void Ctx::renderPart(const std::string& name, double tm, Target& t) {
    Target* saved = out;
    std::string savedScene = scene;
    int savedPart = part;
    double savedLoad = load, savedStart = start, savedEnd = end;
    if (name != scene && cues)
        for (const Cue& q : *cues)
            if (q.scene == name) {
                scene = q.scene;
                part = q.part;
                load = q.load;
                start = q.start;
                end = q.end;
                break;
            }
    out = &t;
    t.bind();
    auto it = scenes ? scenes->find(name) : decltype(scenes->end()){};
    if (scenes && it != scenes->end()) {
        depth++;
        it->second->render(*this, tm);
        depth--;
    } else {
        placeholder(*this, name, tm);
    }
    scene = savedScene;
    part = savedPart;
    load = savedLoad;
    start = savedStart;
    end = savedEnd;
    out = saved;
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
// Pictures: premultiplied alpha; colour = clamp(c * mul + add, 0, 1) in straight colour, then the opacity.
static const char* kPictureFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform int sharp;
uniform vec3 mulc;
uniform vec3 addc;
uniform float alpha;
void main() {
    vec4 c = sharp != 0 ? texSharp(tex, uv) : texture(tex, uv);
    vec3 rgb = clamp(c.rgb * mulc + addc * c.a, vec3(0.0), vec3(c.a));
    o = vec4(rgb, c.a) * alpha;
}
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

void Ctx::drawPicture(const Picture& p, const PictureDraw& d) {
    if (!p.tex.id || p.srcW <= 0 || p.srcH <= 0 || d.w <= 0 || d.h <= 0) return;
    float sx = d.sx, sw = d.sw < 0 ? float(p.srcW) - d.sx : d.sw, sh = d.sh < 0 ? float(p.srcH) - d.sy : d.sh;
    float x0 = d.x, x1 = d.x + d.w;
    // horizontal clipping (the labels slide in from a screen edge): cut the source with the destination
    float c0 = std::max(x0, d.clipX0), c1 = std::min(x1, d.clipX1);
    if (c1 <= c0) return;
    float k = sw / d.w;
    sx += (c0 - x0) * k;
    sw = (c1 - c0) * k;
    setBlend(Blend::Premul);
    pictureShader.use();
    p.tex.bind(0);
    pictureShader.set("tex", 0);
    pictureShader.set("sharp", p.redraw ? 0 : 1);
    pictureShader.set("rect", c0, d.y, c1 - c0, d.h);
    pictureShader.set("uvr", sx / p.srcW, d.sy / p.srcH, (sx + sw) / p.srcW, (d.sy + sh) / p.srcH);
    pictureShader.set("mulc", d.mul);
    pictureShader.set("addc", d.add);
    pictureShader.set("alpha", d.alpha);
    drawQuad();
}

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
        {'"', {0x00, 0x07, 0x00, 0x07, 0x00}}, {',', {0x00, 0x50, 0x30, 0x00, 0x00}},
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
const Cue* Demo::cueAt(double t) const {
    for (const Cue& c : cues_)
        if (t >= c.load && t < std::max(c.end, c.hold)) return &c;
    return nullptr;
}

bool Demo::init(const DopeExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err) {
    ctx.exe = &exe;
    ctx.music = &music;
    ctx.scenes = &scenes_;
    ctx.cues = &cues_;
    ctx.pictures = &pictures_;
    ctx.clock.zero = kCaptureFirstTick - music.timeOfTick(0);
    if (!ctx.imageShader.build(kImageVS, kImageFS, "image") || !ctx.flatShader.build(kImageVS, kFlatFS, "flat") ||
        !ctx.pictureShader.build(kImageVS, kPictureFS, "picture")) {
        if (err) *err = "shader build failed";
        return false;
    }
    pictures_.init(&exe, o.artDir, o.art);
    cues_ = buildTimeline(ctx);
    resize(w, h);
    // every registered part is created and initialised up front (the original unpacks and sets up each part when
    // CDPI loads it; the remake does it before the music starts)
    for (auto& [name, factory] : sceneRegistry()) {
        uint64_t t0 = SDL_GetTicksNS();
        for (const Cue& q : cues_)
            if (q.scene == name) {
                ctx.scene = q.scene;
                ctx.part = q.part;
                ctx.load = q.load;
                ctx.start = q.start;
                ctx.end = q.end;
            }
        scenes_[name].reset(factory());
        scenes_[name]->init(ctx);
        SDL_Log("part %-4s init %.0f ms", name.c_str(), (SDL_GetTicksNS() - t0) * 1e-6);
    }
    for (const Cue& c : cues_)
        if (!scenes_.count(c.scene))
            SDL_Log("part %-4s is not built: placeholder for %.2f-%.2f s", c.scene.c_str(), c.load, c.end);
    return true;
}

void Demo::resize(int w, int h) {
    ctx.w = w;
    ctx.h = h;
    frame_.create(w, h, true, GL_RGBA16F);
    ctx.pool.clear();
}

void Demo::release() {
    scenes_.clear();
    ctx.scenes = nullptr;
    ctx.shared_.clear();
    ctx.pool.clear();
    pictures_.release();
    frame_.destroy();
}

double Demo::length() const { return cues_.empty() ? 0.0 : std::max(cues_.back().end, cues_.back().hold); }

const Target& Demo::render(double t) {
    ctx.out = &frame_;
    frame_.bind();
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    ctx.clear();
    if (const Cue* c = cueAt(t)) {
        ctx.scene = c->scene;
        ctx.part = c->part;
        ctx.load = c->load;
        ctx.start = c->start;
        ctx.end = c->end;
        // between the part's end and the next part the last frame stays on screen (CDPI loads the next part)
        double tp = t < c->end ? t : std::nextafter(c->end, c->load);
        ctx.renderPart(c->scene, tp, frame_);
    }
    ctx.out = &frame_;
    frame_.bind();
    return frame_;
}
