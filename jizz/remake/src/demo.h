// Demo framework: render context, part registry, timeline.
//
// Time: everything is in seconds of music time, from the moment the original's xmpPlay starts the music (the end
// of the precalc, tick 434 of jizz/re/full.frames). The clock is fractional and continuous. The original animates
// almost everything from xmpGetTimer (PIT clocks since the music started, interpolated): ctx.timer(t) gives that
// value. State the original advances once per drawn frame is stepped at a fixed rate with TickStepper.
//
// Screen: the original draws a 320x200 hicolor screen shown at 4:3. The remake renders a 4:3 frame of any size into
// ctx.out. "VGA coordinates" below mean x 0..320 (left to right) and y 0..200 (top to bottom) over the whole frame,
// so pixel centres of the original are at (x + 0.5, y + 0.5).
#pragma once
#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include "render.h"

class JizzExe;
class Music;
struct Ctx;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe, build textures and meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at music time `t` (seconds) into ctx.out (already bound, viewport set, not cleared).
    // Must be a pure function of `t` as far as the viewer can tell: the player seeks, and shots/videos render
    // arbitrary times in any order. Stepped state uses TickStepper (resimulated after a backwards seek).
    virtual void render(Ctx& c, double t) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
// In a scene's .cpp: REGISTER_SCENE(TblScene, "tbl");
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul, Max, Min };
void setBlend(Blend b);

// Deterministic stepped simulation that follows a fractional clock, in steps: pass `steps` = (t - t0) * rate.
//   stepper.advance(steps, reset, step): makes sure step(n) has run for every whole n with first <= n <= floor(steps),
//   in order; after a backwards seek it calls reset() and replays from `first`. Returns floor(steps).
struct TickStepper {
    int first = 0;         // first step that runs
    int done = INT_MIN;    // last step run
    template <class Reset, class Step>
    int advance(double steps, Reset&& reset, Step&& step) {
        int target = int(steps >= 0 ? steps : steps - 1);  // floor
        if (done == INT_MIN || target < done) { reset(); done = first - 1; }
        while (done < target) step(++done);
        return target;
    }
};

// Scratch render targets borrowed from a pool (safe across nested part rendering).
class TargetPool {
public:
    struct Lease {
        Target* t = nullptr;
        bool* busy = nullptr;
        Lease() = default;
        Lease(Target* t_, bool* b) : t(t_), busy(b) {}
        Lease(Lease&& o) noexcept : t(o.t), busy(o.busy) { o.t = nullptr; o.busy = nullptr; }
        Lease& operator=(Lease&& o) noexcept { release(); t = o.t; busy = o.busy; o.t = nullptr; o.busy = nullptr; return *this; }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() { release(); }
        void release() { if (busy) *busy = false; t = nullptr; busy = nullptr; }
        Target* operator->() const { return t; }
        Target& operator*() const { return *t; }
    };
    Lease get(int w, int h, bool depth, GLenum fmt);
    void clear();

private:
    struct Entry { Target t; bool depth; GLenum fmt; bool busy; };
    std::vector<std::unique_ptr<Entry>> entries_;
};

struct Ctx {
    int w = 0, h = 0;               // frame size (4:3)
    const JizzExe* exe = nullptr;   // the original's program image
    Music* music = nullptr;         // positions and the timer of the original player
    Target* out = nullptr;          // the target the current part draws into (bound when render() is called)

    // ---- the original's clock and music sync
    double timer(double t) const;          // xmpGetTimer at music time t (PIT clocks, 1,193,182 per second)
    int position(double t) const;          // xmpGetPos: order << 8 | row
    double positionTime(int pos) const;    // music time at which the position reaches pos
    // The sync event [0x6413] the frame loop 0x638b sets from the table at 0x6417 (order:row -> event) for music
    // time t, and the music time at which that event was set. Parts that rewrite the event do so themselves.
    int event(double t, double* since = nullptr) const;

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another part at `t` into `target` (a placeholder colour if that part is not built). Restores ctx.out
    // and rebinds it afterwards.
    void renderPart(const std::string& name, double t, Target& target);
    bool hasPart(const std::string& name) const;

    // ---- 2D helpers. Coordinates are VGA coordinates (see top of file); the current blend mode is used.
    Mat4 vgaProj() const;  // VGA coordinates -> clip space (y down), for your own geometry
    // Textured quad: texture region (u0,v0)-(u1,v1) (v = 0 is the first row of the image as uploaded)
    // mapped onto the VGA rectangle (x, y, w, h).
    void drawTexture(GLuint tex, float x, float y, float w, float h, float alpha = 1.0f, float u0 = 0, float v0 = 0,
                     float u1 = 1, float v1 = 1, Vec3 tint = Vec3(1, 1, 1));
    // Copy a render target over the whole frame (targets are stored bottom-up as usual in GL).
    void drawTarget(const Target& t, float alpha = 1.0f);
    void rect(float x, float y, float w, float h, Vec3 color, float alpha = 1.0f);
    void clear(Vec3 color = Vec3(0, 0, 0));

    // One shared instance of T per context, created on first use as T(ctx) (for the core library: tables,
    // textures and shaders that several parts use). Created with the GL context current.
    template <class T> T& shared() {
        auto& p = shared_[std::type_index(typeid(T))];
        if (!p) p = std::make_shared<T>(*this);
        return *static_cast<T*>(p.get());
    }

    // Owned by Demo.
    Shader imageShader, flatShader;
    std::map<std::type_index, std::shared_ptr<void>> shared_;
    TargetPool pool;
    std::map<std::string, std::unique_ptr<Scene>>* scenes = nullptr;
    struct SyncEntry { int pos, event; double time; };
    std::vector<SyncEntry> sync;  // the sync table with the music time of each entry (Demo::init)
    int depth = 0;                // nesting of renderPart
};

// Global pass over each finished frame, after the part has drawn it (installed by the core library, src/jizz).
using PostPass = void (*)(Ctx& c, Target& frame, double t);
PostPass& postPass();

// The running order: each part runs from the music position `from` to `to` (order << 8 | row), see
// jizz/re/PARTS.md. Demo::init turns the positions into music times.
struct CueDef {
    int from, to;
    const char* scene;
};
const std::vector<CueDef>& timeline();  // timeline.cpp

struct Cue {
    double start, end;  // music time [start, end)
    std::string scene;
};

class Demo {
public:
    bool init(const JizzExe& exe, Music& music, int w, int h, std::string* err);
    void resize(int w, int h);
    // Render the frame at music time t into the internal target and return it.
    const Target& render(double t);
    double length() const;  // music time at which the intro exits
    const char* partAt(double t) const;
    const std::vector<Cue>& cues() const { return cues_; }
    // Frees every GL object of the parts and the shared core objects. Call it while the GL context still exists.
    void release();
    Ctx ctx;

private:
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    std::vector<Cue> cues_;
    Target frame_;
};
