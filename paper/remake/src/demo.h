// Demo framework: render context, part registry, timeline in IRQ0 ticks.
//
// Time: everything is in ticks of the original's 70.0018 Hz timer interrupt, counted from program start (tick 0),
// exactly as in paper/re/PARTS.md and paper/re/full.frames. The clock is fractional: a part is asked for the frame at
// e.g. tick 2000.37 and evaluates continuous motion there. State that the original advances once per tick is stepped
// per whole tick (see TickStepper) so it matches the original exactly.
//
// Screen: the original draws a 320x200 VGA screen shown at 4:3. The remake renders a 4:3 frame of any size into
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

class PaperExe;
class Music;
struct Ctx;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe, build textures and meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at global tick `tick` into ctx.out (already bound, viewport set, not cleared).
    // Must be a pure function of `tick` as far as the viewer can tell: the player seeks, and shots/videos render
    // arbitrary ticks in any order. Per-tick state is stepped with TickStepper (resimulated after a backwards seek).
    virtual void render(Ctx& c, double tick) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
// In a scene's .cpp: REGISTER_SCENE(TitleScene, "title");
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul, Max, Min };
void setBlend(Blend b);

// Deterministic per-tick simulation that follows a fractional clock.
//   stepper.advance(tick, reset, step): makes sure step(n) has run for every whole tick n with first <= n <= floor(tick),
//   in order; after a backwards seek it calls reset() and replays from `first`. Returns floor(tick) (clamped).
struct TickStepper {
    int first = 0;              // first tick whose step runs (the original's first IRQ of the part)
    int done = INT_MIN;         // last tick stepped
    template <class Reset, class Step>
    int advance(double tick, Reset&& reset, Step&& step) {
        int target = int(tick >= 0 ? tick : tick - 1);  // floor
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
    int w = 0, h = 0;                // frame size (4:3)
    const PaperExe* exe = nullptr;   // the original's code32 data
    Music* music = nullptr;          // counter(t) / rowCountdown(t) of the original player
    Target* out = nullptr;           // the target the current part draws into (bound when render() is called)
    // The redrawn pictures (paper/art.h): the art folder, empty with --original-art or when none was found.
    std::string artDir;
    bool art() const { return !artDir.empty(); }

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another part at `tick` into `t` (a placeholder colour if that part is not built). Restores ctx.out
    // and rebinds it afterwards.
    void renderPart(const std::string& name, double tick, Target& t);
    bool hasPart(const std::string& name) const;
    // Music counter [0x1ea8b] after the IRQ of tick t (-1 before the music starts).
    int counter(int t) const;

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
    int depth = 0;  // nesting of renderPart
};

// Global pass over each finished frame, after the part has drawn it: the original's palette fade level [0x1edb5]
// (applied by the DAC upload to every colour on screen). Installed by the core library (src/paper).
using PostPass = void (*)(Ctx& c, Target& frame, double tick);
PostPass& postPass();

struct Cue {
    int start, end;  // ticks [start, end)
    std::string scene;
};
const std::vector<Cue>& timeline();  // timeline.cpp
const char* partNameAt(double tick);

class Demo {
public:
    bool init(const PaperExe& exe, Music& music, int w, int h, std::string* err);
    void resize(int w, int h);
    // Render the frame at `tick` into the internal target and return it.
    const Target& render(double tick);
    int length() const;  // tick at which the intro exits
    Ctx ctx;

private:
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    Target frame_;
    Shader placeholder_;
};
