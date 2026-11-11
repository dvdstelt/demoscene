#include "demo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

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

// ---------------------------------------------------------------- ctx
TargetPool::Lease Ctx::temp(bool depth, float scale, GLenum fmt) {
    return pool.get(std::max(1, int(std::lround(w * scale))), std::max(1, int(std::lround(h * scale))), depth, fmt);
}

TargetPool::Lease Ctx::tempSized(int tw, int th, bool depth, GLenum fmt) { return pool.get(tw, th, depth, fmt); }

bool Ctx::hasPart(const std::string& name) const { return scenes && scenes->count(name) != 0; }

int Ctx::counter(int t) const { return music ? music->counter(t) : -1; }

void Ctx::renderPart(const std::string& name, double tick, Target& t) {
    Target* saved = out;
    out = &t;
    t.bind();
    auto it = scenes ? scenes->find(name) : decltype(scenes->end()){};
    if (scenes && it != scenes->end()) {
        depth++;
        it->second->render(*this, tick);
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
const char* partNameAt(double tick) {
    for (const Cue& c : timeline())
        if (tick >= c.start && tick < c.end) return c.scene.c_str();
    return "";
}

bool Demo::init(const PaperExe& exe, Music& music, int w, int h, std::string* err) {
    ctx.exe = &exe;
    ctx.music = &music;
    ctx.scenes = &scenes_;
    if (!ctx.imageShader.build(kImageVS, kImageFS, "image") || !ctx.flatShader.build(kImageVS, kFlatFS, "flat")) {
        if (err) *err = "shader build failed";
        return false;
    }
    resize(w, h);
    // every registered part is created and initialised up front (the original's precalc)
    for (auto& [name, factory] : sceneRegistry()) {
        uint64_t t0 = SDL_GetTicksNS();
        scenes_[name].reset(factory());
        scenes_[name]->init(ctx);
        SDL_Log("part %-10s init %.0f ms", name.c_str(), (SDL_GetTicksNS() - t0) * 1e-6);
    }
    for (const Cue& c : timeline())
        if (!scenes_.count(c.scene) && c.scene != "precalc")
            SDL_Log("part %s is not built: placeholder for ticks %d-%d", c.scene.c_str(), c.start, c.end);
    return true;
}

void Demo::resize(int w, int h) {
    ctx.w = w;
    ctx.h = h;
    frame_.create(w, h, true, GL_RGBA16F);
    ctx.pool.clear();
}

int Demo::length() const { return timeline().back().end; }

const Target& Demo::render(double tick) {
    ctx.out = &frame_;
    frame_.bind();
    setBlend(Blend::None);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    ctx.clear();
    std::string name = partNameAt(tick);
    if (!name.empty() && name != "precalc") ctx.renderPart(name, tick, frame_);
    ctx.out = &frame_;
    frame_.bind();
    if (postPass()) postPass()(ctx, frame_, tick);
    ctx.out = &frame_;
    return frame_;
}
