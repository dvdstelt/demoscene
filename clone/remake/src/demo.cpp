// Demo framework (demo.h): blend modes, the target pool, the clocks and the sync events (the table 0x420499 and the
// player's timing as main 0x4001ab sees it), picture drawing (the blit 0x404002), the placeholder of a part that is
// not built, and the frame: the cue at t.
#include "demo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "exe.h"

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

// ---------------------------------------------------------------- clocks and events
// The player (notes-music sections 1 and 8): one tick per timer interrupt, every tickClocks PIT clocks; xmpGetTimer is
// (k - 2) * tickClocks at interrupt k. Row p (order * 64 + row: the song plays its orders in sequence, no jumps, no
// speed or tempo changes) is read at interrupt 1 + speed * p, so xmpGetPos returns it from timer
// (speed * p - 1) * tickClocks, and it is heard one tick later (notes-core 5.3: p * 66300 - 22100).
double Ctx::positionTime(int pos) const { return (double(speed) * pos - 1.0) * tickClocks / kPitHz; }
double Ctx::rowTime(int pos) const { return double(speed) * pos * tickClocks / kPitHz; }
int Ctx::position(double t) const {
    int p = int(std::floor((timer(t) / tickClocks + 1.0) / speed));
    return std::max(0, p);
}

double Ctx::frames(double t) const { return (t - origin) * (cue ? cue->fps : 60.0); }

double Ctx::eventTime(int event, int n) const {
    for (const SyncEntry& e : sync)
        if (e.event == event && n-- == 0) return e.time;
    return std::numeric_limits<double>::infinity();
}

int Ctx::eventCount(int event, double t) const {
    int n = 0;
    for (const SyncEntry& e : sync)
        if (e.event == event && e.time <= t) n++;
    return n;
}

double Ctx::lastEvent(int event, double t) const {
    double at = -std::numeric_limits<double>::infinity();
    for (const SyncEntry& e : sync)
        if (e.event == event && e.time <= t) at = e.time;
    return at;
}

// ---------------------------------------------------------------- ctx
TargetPool::Lease Ctx::temp(bool depth, float scale, GLenum fmt) {
    return pool.get(std::max(1, int(std::lround(w * scale))), std::max(1, int(std::lround(h * scale))), depth, fmt);
}

TargetPool::Lease Ctx::tempSized(int tw, int th, bool depth, GLenum fmt) { return pool.get(tw, th, depth, fmt); }

bool Ctx::hasPart(const std::string& name) const { return scenes && scenes->count(name) != 0; }

namespace {
std::string posText(int pos) {
    char b[16];
    std::snprintf(b, sizeof b, "%02X:%02X", pos / 64, pos % 64);
    return b;
}

// The captions and glyphs the parts draw (notes-gfx 7.2: black box 0x4043be, text, glyph; opaque here), so that the
// placeholders show where they go and the timeline can be checked against the capture before the parts exist.
struct Caption {
    int part;
    float bx, by, bw, bh;      // black box
    const char* text;
    float tx, ty;
    const char* glyph;
    float gx, gy;
};
constexpr Caption kCaptions[] = {
    {2, 10, 130, 100, 40, "meet_us", 20, 151, "glyph1", 80, 111},
    {5, 180, 30, 130, 40, "get_them_clones", 215, 53, "glyph2", 140, 0},
    {3, 162, 135, 148, 40, "connect_the_dots", 212, 160, "glyph3", 212, 87},
    {4, 10, 45, 170, 40, "tunnels_meet_more_tunnels", 20, 70, "glyph4", 40, 0},
    {11, 120, 72, 160, 48, "title_clone_meets_clone", 141, 104, nullptr, 0, 0},  // the box: tiles 3..6 of rows 2..3
};

// A part that is not built: a dim colour derived from its name, the name, the part number and routine, the music
// position, the part's clock (t70 units) and the capture's tick; and the part's caption and glyph.
void placeholder(Ctx& c, const std::string& name, double t) {
    unsigned hsh = 2166136261u;
    for (char ch : name) hsh = (hsh ^ uint8_t(ch)) * 16777619u;
    c.clear(Vec3(0.12f + 0.25f * ((hsh >> 0) & 255) / 255.0f, 0.12f + 0.25f * ((hsh >> 8) & 255) / 255.0f,
                 0.12f + 0.25f * ((hsh >> 16) & 255) / 255.0f));
    for (const Caption& k : kCaptions) {
        if (k.part != c.part) continue;
        if (k.part == 11 && t >= c.eventTime(1101)) break;  // the title fades out from 0b:00 (not modelled here)
        setBlend(Blend::None);
        c.rect(k.bx, k.by, k.bw, k.bh, Vec3(0, 0, 0));
        c.overlay(k.text, k.tx, k.ty);
        if (k.glyph) c.overlay(k.glyph, k.gx, k.gy);
    }
    setBlend(Blend::None);
    float y = c.part == 4 || c.part == 5 ? 120 : 16;  // below the captions at the top
    char b[128];
    std::snprintf(b, sizeof b, "%s  PART %d", name.c_str(), c.part);
    c.text(16, y, 3, b, Vec3(1, 1, 1));
    std::snprintf(b, sizeof b, "%s  CLOCK %.1f  %.2f S  0X%X", posText(c.position(t)).c_str(), c.clock(t),
                  t - c.start, c.cue ? c.cue->routine : 0u);
    c.text(16, y + 28, 1.5f, b, Vec3(1, 1, 0.6f));
    std::snprintf(b, sizeof b, "MUSIC %.3f  T70 %.1f  TICK %d", t, c.t70(t), c.captureTick(t));
    c.text(16, y + 42, 1.5f, b, Vec3(0.8f, 0.8f, 0.8f));
}
}  // namespace

void Ctx::renderPart(const std::string& name, double tm, Target& t) {
    Target* saved = out;
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
    if (!p.tex.id || p.srcW <= 0 || p.srcH <= 0) return;
    float sw = d.sw < 0 ? float(p.srcW) : d.sw, sh = d.sh < 0 ? float(p.srcH) : d.sh;
    setBlend(Blend::Premul);
    pictureShader.use();
    p.tex.bind(0);
    pictureShader.set("tex", 0);
    pictureShader.set("sharp", p.redraw ? 0 : 1);
    pictureShader.set("rect", d.x, d.y, d.w, d.h);
    pictureShader.set("uvr", d.sx / p.srcW, d.sy / p.srcH, (d.sx + sw) / p.srcW, (d.sy + sh) / p.srcH);
    pictureShader.set("mulc", d.mul);
    pictureShader.set("addc", d.add);
    pictureShader.set("alpha", d.alpha);
    drawQuad();
}

void Ctx::overlay(const Picture& p, float x, float y, double level) {
    drawPicture(p, {.x = x, .y = y, .w = float(p.srcW), .h = float(p.srcH), .alpha = overlayOpacity(level)});
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
        if (t >= c.start && t < c.end) return &c;
    return nullptr;
}

namespace {
// The player's timing from the module the image holds (notes-music sections 3, 5): 'MXM\0' at 0x406862, speed and
// BPM at +0x18 / +0x19; xmpPlay's timer period 0x2d86bf / BPM PIT clocks per tick.
bool readTiming(const CloneExe& exe, Ctx& c, std::string* err) {
    constexpr uint32_t kModule = 0x406862;
    if (exe.u32(kModule) != 0x004d584d || exe.u8(kModule + 0x18) == 0 || exe.u8(kModule + 0x19) == 0) {
        if (err) *err = "unexpected MXM module header";
        return false;
    }
    c.speed = exe.u8(kModule + 0x18);
    c.tickClocks = double(0x2d86bf / exe.u8(kModule + 0x19));
    return true;
}

// The sync table 0x420499: {s32 pos; s32 event} up to the entry with event -1 (98 entries).
void readSync(const CloneExe& exe, Ctx& c) {
    c.sync.clear();
    for (uint32_t a = 0x420499; a + 8 <= exe.end(); a += 8) {
        int pos = exe.s32(a), ev = exe.s32(a + 4);
        c.sync.push_back({pos, ev, c.positionTime(pos)});
        if (ev == -1) break;
    }
}
}  // namespace

bool Demo::init(const CloneExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err) {
    ctx.exe = &exe;
    ctx.music = &music;
    ctx.scenes = &scenes_;
    ctx.pictures = &pictures_;
    if (!readTiming(exe, ctx, err)) return false;
    readSync(exe, ctx);
    if (!ctx.imageShader.build(kImageVS, kImageFS, "image") || !ctx.flatShader.build(kImageVS, kFlatFS, "flat") ||
        !ctx.pictureShader.build(kImageVS, kPictureFS, "picture")) {
        if (err) *err = "shader build failed";
        return false;
    }
    pictures_.init(&exe, o.artDir, o.art);
    cues_ = buildTimeline(ctx);
    length_ = introLength(ctx);
    resize(w, h);
    // every registered part is created and initialised up front (the original's precalc)
    for (auto& [name, factory] : sceneRegistry()) {
        uint64_t t0 = SDL_GetTicksNS();
        scenes_[name].reset(factory());
        scenes_[name]->init(ctx);
        SDL_Log("part %-8s init %.0f ms", name.c_str(), (SDL_GetTicksNS() - t0) * 1e-6);
    }
    for (const Cue& c : cues_)
        if (!scenes_.count(c.scene))
            SDL_Log("part %-8s is not built: placeholder for %.2f-%.2f s", c.scene.c_str(), c.start, c.end);
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
        ctx.cue = c;
        ctx.start = c->start;
        ctx.end = c->end;
        ctx.origin = c->origin;
        ctx.clock0 = c->clock0;
        ctx.part = c->part;
        ctx.renderPart(c->scene, t, frame_);
    }
    ctx.out = &frame_;
    frame_.bind();
    return frame_;
}
