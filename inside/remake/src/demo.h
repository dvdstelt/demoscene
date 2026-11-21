// Demo framework: render context, part registry, clocks, cues, the palette fade pass (inside/re/PORT.md is the porters'
// guide to all of it).
//
// Time: everything is in screen refreshes of the original, counted as in inside/re/full.frames: refresh k is record
// k of the capture, program time k / 70.086 s (the VGA's 25.175 MHz / (800 x 449)). The original runs on the refresh:
// its timer interrupt fires 4 times per refresh, the first right after the vertical retrace (inside/re/NOTES.md
// "Clock"), the parts' tick routines and the palette fades step once per refresh, the music player runs on DSMI time
// that advances 35 units per interrupt. The remake's clock is fractional: a part is asked for the frame at, say,
// refresh 2000.37 and evaluates continuous motion there; state the original advances once per refresh is stepped per
// whole refresh with TickStepper (replayed after a backwards seek) and blended by the fraction.
//
// Playback, --start, --shots, --video and --bench use seconds of music time t (t = 0 when the soundtrack starts):
// refresh = kMusicRefresh + t * kRefreshHz (music.h).
//
// Screen: the original draws a 320x200 8-bit screen through a 256-colour palette, shown at 4:3. The remake renders a
// 4:3 frame of any size into ctx.out in true colour: a pixel's colour is what the palette entry of its index shows, as
// a display-space float (a 6-bit value v is v / 63). "VGA coordinates" below mean x 0..320 (left to right) and y
// 0..200 (top to bottom) over the whole frame, so the pixel (x, y) of the original covers [x, x+1] x [y, y+1].
// The palette fades and flashes of the original (core 0x400364..0x400612, run by the script) apply to the whole
// screen: Demo::render applies them after the part has drawn (inside/fade.h), so parts draw with the full palette.
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

class InsideExe;
class InsideDat;
struct Soundtrack;
struct Ctx;
struct Cue;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe and INSIDE.DAT, build textures, meshes.
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
// In a scene's .cpp: REGISTER_SCENE(IntroScene, "intro");
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul, Max, Min };
void setBlend(Blend b);

constexpr double kPitHz = 1193182.0;
constexpr double kRefreshHz = 25175000.0 / (800.0 * 449.0);  // 70.0863 refreshes per second
constexpr int kTicksPerRefresh = 4;                          // timer interrupts per refresh (part ticks [0x4248c8])

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
// Typical use: state stepped once per refresh from the part's first tick, blended by the fraction:
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

// One entry of the script table 0x4222e4 (NOTES.md "The script", PARTS.md, notes-core.md 1.4).
struct ScriptEntry {
    int order = 0, row = 0;    // music position: the entry runs once order * 64 + row is reached
    uint32_t routine = 0;      // the routine it calls (0 = end of the demo)
    double callback = 0;       // the callback M in which it runs (its row's start; never before the entry before it)
    double refresh = 0;        // the first record that shows its effect (first palette step, first tick routine call)
};

struct Ctx {
    int w = 0, h = 0;                      // frame size (4:3)
    const InsideExe* exe = nullptr;        // the original's program image
    const InsideDat* dat = nullptr;        // INSIDE.DAT's files
    const Soundtrack* music = nullptr;     // the rendered soundtrack and the player's positions (music.h)
    Target* out = nullptr;                 // the target the current part draws into (bound when render() is called)

    // ---- the current cue (struct Cue below, built by timeline.cpp), set before render()
    const Cue* cue = nullptr;
    double start = 0, end = 0;             // refreshes: the part's routines are set / the next cue's

    // ---- the original's clocks (see the top of this file and inside/re/notes-core.md section 1)
    // Tick steps whose result record r shows: the part's tick routine runs at the retrace callback of records start,
    // start + 1, ..., and the frame shown at record r was drawn after the one of record r - 1.
    double refreshes(double r) const { return r - start; }
    // M = [0x4248c4], callbacks since the music start, at the retrace callback of record r: 4 r - 150.
    static double callback(double r) { return 4.0 * r - 150.0; }
    static double refreshOfCallback(double m) { return (m + 150.0) / 4.0; }
    // P = [0x4248c8], the part tick counter, at callback m: m - the callback in which the part's routines were set.
    double partTicks(double m) const;
    int position(double r) const;          // music position order * 64 + row as the script sees it (-1 before)

    // ---- the script
    const std::vector<ScriptEntry>& script() const { return script_; }
    // Refresh of the n-th entry calling this routine (+inf if none); how many have run by refresh r; the last one
    // that has run by r (-inf if none).
    double entryRefresh(uint32_t routine, int n = 0) const;
    int entryCount(uint32_t routine, double r) const;
    double lastEntry(uint32_t routine, double r) const;

    // The record of inside/re/full.frames nearest to refresh r.
    int captureRecord(double r) const { return int(std::lround(r)); }

    // ---- pictures (pictures.h): the original's pixels through a palette, or the redraw (art/<name>.png).
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

// The running order (timeline.cpp): one cue per part, from the refresh at which the script entry that sets its frame
// routine runs to the next one.
struct Cue {
    std::string scene;        // the scene name (src/scenes)
    uint32_t frameRoutine;    // the frame routine [0x424888] the original runs (PARTS.md)
    double start = 0;         // record of the part's first tick routine call (ScriptEntry::refresh)
    double end = 0;           // the next cue's start (the end of the demo for the last)
    double mReset = 0;        // the callback in which its routines were set: P = M - mReset
};
std::vector<Cue> buildTimeline(Ctx& c);
// The refresh at which the demo ends (the script's end marker).
double demoEnd(const Ctx& c);

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
        bool fade = true;     // false: --nofade (show the parts without the palette fades, for checks)
    };
    bool init(const InsideExe& exe, const InsideDat& dat, const Soundtrack& music, int w, int h, const Options& o,
              std::string* err);
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
    Target frame_, part_;
    Options opt_;
    Shader fadeShader_;
};
