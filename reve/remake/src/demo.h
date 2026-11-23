// Demo framework: render context, part registry, clocks, the script, cues (reve/re/PORT.md is the porters' guide).
//
// Time: everything is in screen refreshes of the original, counted as in reve/re/full.frames: refresh k is record k
// of the capture (taken at the start of the vertical retrace), program time (k + 412/449) / 70.0863 s. The original
// runs on the refresh: its timer interrupt is locked to the vertical retrace (reve/re/NOTES.md "The clock") and calls
// the current script entry's retrace routine once per refresh; the main loop draws at most one frame per refresh
// (the frame wait 0x2045d0). The remake's clock is fractional: a part is asked for the frame at, say, refresh 2000.37
// and evaluates continuous motion there; state the original advances once per refresh is stepped per whole refresh
// with TickStepper (replayed after a backwards seek) and blended by the fraction.
//
// Playback, --start, --shots, --video and --bench use seconds of music time t (t = 0 when the player starts its GUS
// timers): refresh = music.refreshOf(t) (music.h).
//
// Screen: the original draws a 320x200 8-bit screen through a 256-colour palette, shown at 4:3. The remake renders a
// 4:3 frame of any size into ctx.out in true colour: a pixel's colour is what the palette entry of its index shows, as
// a display-space float (a 6-bit value v is v / 63). "VGA coordinates" below mean x 0..320 (left to right) and y
// 0..200 (top to bottom) over the whole frame, so the pixel (x, y) of the original covers [x, x+1] x [y, y+1].
#pragma once
#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include "pictures.h"
#include "render.h"

class ReveExe;
struct Soundtrack;
struct Ctx;
struct Cue;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from REVE.EXE, build textures, meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at refresh `r` (fractional) into ctx.out (already bound, viewport set, cleared to black).
    // ctx.cue is the cue being drawn. Must be a pure function of `r` as far as the viewer can tell: the player seeks,
    // and shots / videos render arbitrary refreshes in any order. Stepped state uses TickStepper.
    virtual void render(Ctx& c, double r) = 0;
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

constexpr double kPitHz = 1193182.0;
constexpr double kRefreshHz = 25175000.0 / (800.0 * 449.0);  // 70.0863 refreshes per second

// Whole steps and the fraction of a clock: n = floor(x).
struct Steps {
    int n;
    double frac;
};
inline Steps stepsOf(double x) {
    double f = std::floor(x);
    return {int(f), x - f};
}

// Deterministic stepped simulation that follows a fractional clock.
//   stepper.advance(x, reset, step): makes sure step(n) has run for every whole n with first <= n <= floor(x), in
//   order; after a backwards seek it calls reset() and replays from `first`. Returns floor(x).
// Typical use: state stepped once per refresh from the part's first retrace, blended by the fraction:
//   int n = stepper.advance(c.refreshes(r), [&] { s = State(); }, [&](int k) { s.step(k); });
struct TickStepper {
    int first = 0;
    int done = INT_MIN;
    template <class Reset, class Step>
    int advance(double x, Reset&& reset, Step&& step) {
        int target = int(std::floor(x));
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

// How ctx.drawPicture places a picture (C++20: c.drawPicture(pic, {.x = 20, .y = 151, .w = 42, .h = 9})).
struct PictureDraw {
    float x = 0, y = 0, w = 320, h = 200;     // destination rectangle in VGA coordinates
    float sx = 0, sy = 0, sw = -1, sh = -1;   // source rectangle in the original's pixel grid (-1: all of it)
    Vec3 mul = Vec3(1, 1, 1);                 // colour = clamp(colour * mul + add, 0, 1)
    Vec3 add = Vec3(0, 0, 0);
    float alpha = 1.0f;                       // overall opacity
};

// One entry of the script table 0x205ed0 (reve/re/NOTES.md "The script table", PARTS.md).
struct ScriptEntry {
    int index = 0;
    uint32_t init = 0, done = 0;   // routines main calls before and after the entry
    uint32_t retrace = 0;          // the routine the timer interrupt calls once per refresh during the entry
    uint32_t frame = 0;            // the routine main calls once per drawn frame
    int endPos = 0;                // music position order << 8 | row at which the entry ends
    double start = 0, end = 0;     // refreshes: the entry's first retrace call, the next entry's (the cue's)
};

struct Ctx {
    int w = 0, h = 0;                      // frame size (4:3)
    const ReveExe* exe = nullptr;          // the original's program image and files
    const Soundtrack* music = nullptr;     // the rendered soundtrack and the player's positions (music.h)
    Target* out = nullptr;                 // the target the current part draws into (bound when render() is called)

    // ---- the current cue (struct Cue below, built by timeline.cpp), set before render()
    const Cue* cue = nullptr;
    double start = 0, end = 0;             // refreshes: the cue's first retrace, the next cue's

    // ---- the original's clocks (reve/re/NOTES.md "The clock")
    // Retrace routine calls whose result record r shows: the cue's retrace routine runs at the retraces of records
    // start, start + 1, ...
    double refreshes(double r) const { return r - start; }
    int position(double r) const;          // music position order << 8 | row as the timer interrupt sees it

    // ---- the script
    const std::vector<ScriptEntry>& script() const { return script_; }

    // The record of reve/re/full.frames nearest to refresh r.
    int captureRecord(double r) const { return int(std::lround(r)); }

    // ---- pictures (pictures.h): the original's pixels through their palette, or the redraw (art/<name>.png).
    bool art() const { return pictures && pictures->useArt(); }
    const Picture& picture(const std::string& name) { return pictures->get(name); }
    void drawPicture(const Picture& p, const PictureDraw& d);

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another part at refresh r into `target` (a placeholder if that part is not built). Restores ctx.out.
    void renderPart(const std::string& name, double r, Target& target);
    bool hasPart(const std::string& name) const;

    // ---- 2D helpers. Coordinates are VGA coordinates (see top of file); the current blend mode is used.
    Mat4 vgaProj() const;  // VGA coordinates -> clip space (y down), for your own geometry
    void drawTexture(GLuint tex, float x, float y, float w, float h, float alpha = 1.0f, float u0 = 0, float v0 = 0,
                     float u1 = 1, float v1 = 1, Vec3 tint = Vec3(1, 1, 1));
    void drawTarget(const Target& t, float alpha = 1.0f);
    void rect(float x, float y, float w, float h, Vec3 color, float alpha = 1.0f);
    void clear(Vec3 color = Vec3(0, 0, 0));
    // Text in a 5x7 pixel font (for placeholders and debugging): `size` = VGA units per font pixel.
    void text(float x, float y, float size, const std::string& s, Vec3 color);

    // One shared instance of T per context, created on first use as T(ctx) (for the core library: textures, tables
    // and shaders that several parts use). Created with the GL context current.
    template <class T> T& shared() {
        auto& p = shared_[std::type_index(typeid(T))];
        if (!p) p = std::make_shared<T>(*this);
        return *static_cast<T*>(p.get());
    }

    // Owned by Demo.
    Shader imageShader, flatShader, pictureShader;
    PictureStore* pictures = nullptr;
    std::map<std::type_index, std::shared_ptr<void>> shared_;
    TargetPool pool;
    std::map<std::string, std::unique_ptr<Scene>>* scenes = nullptr;
    std::vector<ScriptEntry> script_;
    int depth = 0;
};

// The running order (timeline.cpp): one cue per script entry, from its first retrace to the next entry's.
struct Cue {
    std::string scene;        // the scene name (src/scenes)
    int entry = 0;            // index into the script
    double start = 0;         // refresh S of the entry's first retrace call: record r shows the state after r - S calls
    double end = 0;           // the next entry's start (the end of the demo for the last)
    double hold = 0;          // the original draws no frame of this entry after this refresh (the next entry's init
                              // runs): Demo::render asks the part for min(r, hold), so the screen holds that frame
    double black = 0;         // ... unless the next init clears the screen: black from this refresh on
};
std::vector<Cue> buildTimeline(Ctx& c);
// The refresh at which the demo ends.
double demoEnd(const Ctx& c);

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
    };
    bool init(const ReveExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err);
    void resize(int w, int h);
    // Render the frame at refresh r into the internal target and return it.
    const Target& render(double r);
    double length() const { return length_; }  // refresh at which the demo ends
    const Cue* cueAt(double r) const;
    const char* partAt(double r) const { const Cue* c = cueAt(r); return c ? c->scene.c_str() : ""; }
    const std::vector<Cue>& cues() const { return cues_; }
    void release();
    Ctx ctx;

private:
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    std::vector<Cue> cues_;
    double length_ = 0;
    PictureStore pictures_;
    Target frame_;
    Options opt_;
};
