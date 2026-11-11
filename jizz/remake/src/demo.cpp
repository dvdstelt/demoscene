// Demo framework: blend modes, the target pool, the context helpers, the sync events (0x638b's table at 0x6417)
// and the part sequence.
#include "demo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "exe.h"
#include "music.h"

std::map<std::string, SceneFactory>& sceneRegistry() {
    static std::map<std::string, SceneFactory> r;
    return r;
}

PostPass& postPass() {
    static PostPass p = nullptr;
    return p;
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

// ---------------------------------------------------------------- clock and sync
double Ctx::timer(double t) const { return music ? music->timer(t) : t * Music::kPitHz; }
int Ctx::position(double t) const { return music ? music->position(t) : 0; }
double Ctx::positionTime(int pos) const { return music ? music->positionTime(pos) : 0.0; }

int Ctx::event(double t, double* since) const {
    // 0x638b: the event of the last entry whose position has been reached (0 before the first one)
    int ev = 0;
    double at = 0;
    for (const SyncEntry& e : sync) {
        if (e.time > t) break;
        ev = e.event;
        at = e.time;
    }
    if (since) *since = at;
    return ev;
}

// ---------------------------------------------------------------- ctx
TargetPool::Lease Ctx::temp(bool depth, float scale, GLenum fmt) {
    return pool.get(std::max(1, int(std::lround(w * scale))), std::max(1, int(std::lround(h * scale))), depth, fmt);
}

TargetPool::Lease Ctx::tempSized(int tw, int th, bool depth, GLenum fmt) { return pool.get(tw, th, depth, fmt); }

bool Ctx::hasPart(const std::string& name) const { return scenes && scenes->count(name) != 0; }

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
        // part not built: a dim colour derived from its name
        unsigned hsh = 2166136261u;
        for (char ch : name) hsh = (hsh ^ uint8_t(ch)) * 16777619u;
        clear(Vec3(0.15f + 0.25f * ((hsh >> 0) & 255) / 255.0f, 0.15f + 0.25f * ((hsh >> 8) & 255) / 255.0f,
                   0.15f + 0.25f * ((hsh >> 16) & 255) / 255.0f));
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

// ---------------------------------------------------------------- demo
const char* Demo::partAt(double t) const {
    for (const Cue& c : cues_)
        if (t >= c.start && t < c.end) return c.scene.c_str();
    return "";
}

bool Demo::init(const JizzExe& exe, Music& music, int w, int h, std::string* err) {
    ctx.exe = &exe;
    ctx.music = &music;
    ctx.scenes = &scenes_;
    if (!ctx.imageShader.build(kImageVS, kImageFS, "image") || !ctx.flatShader.build(kImageVS, kFlatFS, "flat")) {
        if (err) *err = "shader build failed";
        return false;
    }
    // The sync table: [0x651b] points at its first entry; 4-byte entries (word position, signed word event)
    // up to the event -1 that ends the intro.
    ctx.sync.clear();
    for (uint32_t a = exe.u32(0x651b);; a += 4) {
        int pos = exe.u16(a), ev = exe.s16(a + 2);
        ctx.sync.push_back({pos, ev, music.positionTime(pos)});
        if (ev == -1 || ctx.sync.size() > 1000) break;
    }
    cues_.clear();
    for (const CueDef& c : timeline())
        cues_.push_back({music.positionTime(c.from), c.to < 0 ? music.length() : music.positionTime(c.to), c.scene});
    resize(w, h);
    // every registered part is created and initialised up front (the original's precalc)
    for (auto& [name, factory] : sceneRegistry()) {
        uint64_t t0 = SDL_GetTicksNS();
        scenes_[name].reset(factory());
        scenes_[name]->init(ctx);
        SDL_Log("part %-10s init %.0f ms", name.c_str(), (SDL_GetTicksNS() - t0) * 1e-6);
    }
    for (const Cue& c : cues_)
        if (!scenes_.count(c.scene)) SDL_Log("part %s is not built: placeholder for %.2f-%.2f s", c.scene.c_str(), c.start, c.end);
    return true;
}

void Demo::resize(int w, int h) {
    ctx.w = w;
    ctx.h = h;
    frame_.create(w, h, true, GL_RGBA16F);
    ctx.pool.clear();
}

double Demo::length() const { return cues_.empty() ? 0.0 : cues_.back().end; }

const Target& Demo::render(double t) {
    ctx.out = &frame_;
    frame_.bind();
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    ctx.clear();
    std::string name = partAt(t);
    if (!name.empty()) ctx.renderPart(name, t, frame_);
    ctx.out = &frame_;
    frame_.bind();
    if (postPass()) postPass()(ctx, frame_, t);
    ctx.out = &frame_;
    return frame_;
}
