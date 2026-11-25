// Demo framework: render context, scene registry, clocks, the script's timeline, the palette, picture drawing
// (vividexp/re/PORT.md is the porters' guide to all of it).
//
// Time: everything is in screen refreshes of the original, counted as in vividexp/re/full.frames: refresh k is
// record k of the capture, program time (k + 1) / 70.086 s (the VGA's 25.175 MHz / (800 x 449)). The original runs on
// the refresh: MIDAS's timer interrupt waits for each retrace and adds 1 to the demo's frame counter [0x825c5c]
// (vividexp/re/NOTES.md "Clock"); the effects animate from that counter, the palette fades step by it, and the
// script waits for music positions polled once per refresh. The remake's clock is fractional: a scene is asked for
// the frame at, say, refresh 6000.37 and evaluates its motion there; state the original steps once per refresh is
// stepped per whole refresh with TickStepper (replayed after a backwards seek) and blended by the fraction.
//
// Playback, --start, --shots, --video and --bench use seconds of music time t (t = 0 when part I's module starts):
// refresh = music->refreshOf(t) (music.h); --refshots renders refreshes of the capture directly.
//
// Screen: the original draws a 320x200 8-bit screen through a 256-colour palette, shown at 4:3. The remake renders a
// 4:3 frame of any size into ctx.out in true colour: a pixel's colour is what the palette entry of its index shows,
// as a display-space float (a 6-bit value v is v / 63). "VGA coordinates" mean x 0..320 (left to right) and y
// 0..200 (top to bottom) over the whole frame, so the pixel (x, y) of the original covers [x, x+1] x [y, y+1].
// Everything the original shows goes through its palette, so the scenes draw indices through ctx.palette(r), the
// palette the DAC holds at refresh r (fades included: vivid/timeline.h).
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
#include "vivid/images.h"
#include "vivid/timeline.h"

class VividExe;
struct Soundtrack;
struct Ctx;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from VIVID.EXE, build textures, tables, meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at refresh `r` (fractional) into ctx.out (already bound, viewport set, cleared to black).
    // ctx.seg is the segment being drawn (its script lines, start and end). Must be a pure function of `r` as far as
    // the viewer can tell: the player seeks, and shots / videos render arbitrary refreshes in any order. Stepped
    // state uses TickStepper.
    virtual void render(Ctx& c, double r) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
// In a scene's .cpp: REGISTER_SCENE(BumpScene, "bump");
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

// Scratch render targets borrowed from a pool (safe across nested rendering).
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
    int w = 0, h = 0;                      // frame size (4:3)
    const VividExe* exe = nullptr;         // the original's program image and its archives
    const Soundtrack* music = nullptr;     // the rendered soundtrack and the songs' positions (music.h)
    const vivid::Timeline* timeline = nullptr;   // the scripts' lines and segments, the palette (vivid/timeline.h)
    Target* out = nullptr;                 // the target the current scene draws into (bound when render() is called)

    // ---- the segment being drawn (set before render())
    const vivid::Segment* seg = nullptr;
    // Refreshes since the segment's start: the frame counter [0x825c5c] minus its value when the segment's Run
    // command started, as the effects count it (each notes file says which origin its counter has).
    double since(double r) const { return seg ? r - seg->start : r; }

    // ---- the palette the DAC holds at refresh r (fades evaluated continuously), as colours and as a 256 x 1
    // texture (updated once per frame by Demo::render; valid while the frame is drawn)
    vivid::Palette palette(double r) const { return timeline->palette(r); }
    const Texture& paletteTex() const { return paletteTex_; }

    // The capture's record shown at refresh r (for checks).
    int captureRecord(double r) const { return int(std::floor(r)); }

    // ---- pictures (pictures.h): the original's pixels through the palette, or a redraw (art/<name>.png).
    bool art() const { return pictures && pictures->useArt(); }
    // --censor: the nude parts of the pictures (the pin-ups and the title picture) are shown as a mosaic, for a video
    // that is to be published; by default they show the original's pixels.
    bool censor = false;

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another scene at refresh r into `target` for segment `s` (a placeholder if that scene is not built).
    void renderScene(const std::string& name, const vivid::Segment* s, double r, Target& target);
    bool hasScene(const std::string& name) const;

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
    // and shaders that several scenes use). Created with the GL context current.
    template <class T> T& shared() {
        auto& p = shared_[std::type_index(typeid(T))];
        if (!p) p = std::make_shared<T>(*this);
        return *static_cast<T*>(p.get());
    }

    // Owned by Demo.
    Shader imageShader, flatShader;
    PictureStore* pictures = nullptr;
    std::map<std::type_index, std::shared_ptr<void>> shared_;
    TargetPool pool;
    std::map<std::string, std::unique_ptr<Scene>>* scenes = nullptr;
    Texture paletteTex_;
    int depth = 0;
};

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
        bool censor = false;  // --censor
    };
    bool init(const VividExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err);
    void resize(int w, int h);
    // Render the frame at refresh r into the internal target and return it.
    const Target& render(double r);
    double length() const { return timeline_.end(); }   // refresh at which the demo ends
    const vivid::Segment* segmentAt(double r) const { return timeline_.segmentAt(r); }
    const char* sceneAt(double r) const { const vivid::Segment* s = segmentAt(r); return s ? s->scene.c_str() : ""; }
    const vivid::Timeline& timeline() const { return timeline_; }
    void release();
    Ctx ctx;

private:
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    vivid::Timeline timeline_;
    PictureStore pictures_;
    Target frame_;
    Options opt_;
};
