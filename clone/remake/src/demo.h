// Demo framework: render context, part registry, clocks, sync events, timeline (clone/re/PORT.md is the porters'
// guide to all of it).
//
// Time: everything is in seconds of music time t = xmpGetTimer / 1,193,182 (clone/re/notes-music.md section 8): t = 0
// is the player's second timer interrupt, when the first row sounds (the end of tick 12 of clone/re/full.frames,
// program time 0.7216 s). The clock is the audible position of the soundtrack. The original's clocks follow from it
// (clone/re/notes-core.md section 5):
//   - ctx.timer(t): xmpGetTimer, PIT clocks since the music start, continuous;
//   - ctx.t70(t): main's time t70 = timer / 17043 (0x41a128; one unit = 1/70.0101 s). No part reads the timer: each
//     integrates dt = t70 - previous t70 once per drawn frame, so a part's state is a function of ctx.clock(t), the
//     sum of the dt the part has seen: clock0 + t70(t) - t70(origin), where origin is the timer of the frame before
//     the part's first frame (the capture's, notes-core 5.3);
//   - ctx.position(t): xmpGetPos as main uses it, order * 64 + row; it runs one player tick ahead of the sound;
//   - the sync table 0x420499: events fire when position() reaches their row (ctx.eventTime / eventCount / lastEvent).
// State the original advances once per drawn frame without dt is stepped at the capture's frame rate with
// TickStepper (ctx.frames(t)).
//
// Screen: the original draws a 320x200 true-colour screen (0x00RRGGBB) shown at 4:3. The remake renders a 4:3 frame
// of any size into ctx.out. "VGA coordinates" below mean x 0..320 (left to right) and y 0..200 (top to bottom) over
// the whole frame, so the pixel (x, y) of the original covers [x, x+1] x [y, y+1].
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

class CloneExe;
struct Soundtrack;
struct Ctx;
struct Cue;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe, build textures and meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at music time `t` (seconds) into ctx.out (already bound, viewport set, cleared to black).
    // ctx.cue is the cue being drawn (ctx.start, ctx.end, ctx.clock(t), ...). Must be a pure function of `t` as far as
    // the viewer can tell: the player seeks, and shots/videos render arbitrary times in any order. Stepped state uses
    // TickStepper (replayed after a backwards seek).
    virtual void render(Ctx& c, double t) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
// In a scene's .cpp: REGISTER_SCENE(TubeScene, "tube");
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul, Max, Min };
void setBlend(Blend b);

// The original's clocks (see the top of this file).
constexpr double kPitHz = 1193182.0;   // xmpGetTimer's unit: PIT clocks per second
constexpr double kT70Clocks = 17043.0;  // PIT clocks per t70 unit (main's constant 0x41a128 = 1 / 17043)
constexpr double kT70Hz = kPitHz / kT70Clocks;  // 70.0101 t70 units per second
constexpr int kCaptureTick0 = 12;  // clone/re/full.frames: music time 0 is the end of timer tick 12

// Whole steps and the fraction of a fixed-rate clock that started at t0: n = floor((t - t0) * rate).
struct Steps {
    int n;
    double frac;
};
inline Steps stepsAt(double t, double t0, double rate) {
    double s = (t - t0) * rate, f = std::floor(s);
    return {int(f), s - f};
}

// Deterministic stepped simulation that follows a fractional clock, in steps: pass `steps` = (t - t0) * rate.
//   stepper.advance(steps, reset, step): makes sure step(n) has run for every whole n with first <= n <= floor(steps),
//   in order; after a backwards seek it calls reset() and replays from `first`. Returns floor(steps).
// Typical use: state stepped once per original frame from the part's clock origin, blended by the fraction:
//   int n = stepper.advance(c.frames(t), [&] { s = State(); }, [&](int k) { s.step(k); });
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

// The blit 0x404002 with mode 1 at `level` (0x40442c: out = src + level * (dst - src) / 64 per channel; level <= 0
// copies, above 63 it is clamped to 63): the picture's opacity 1 - level / 64, as a continuous level. So level 63 still
// leaves 1/64 of the picture (notes-gfx 7.2): pass the level the original computes, without its truncation.
inline float overlayOpacity(double level) {
    double l = level < 0 ? 0 : level > 63 ? 63 : level;
    return float(1.0 - l / 64.0);
}

// How ctx.drawPicture places a picture (C++20: c.drawPicture(pic, {.x = 20, .y = 151, .w = 42, .h = 9})).
struct PictureDraw {
    float x = 0, y = 0, w = 320, h = 200;     // destination rectangle in VGA coordinates
    float sx = 0, sy = 0, sw = -1, sh = -1;   // source rectangle in the original's pixel grid (-1: all of it)
    Vec3 mul = Vec3(1, 1, 1);                 // colour = clamp(colour * mul + add, 0, 1)
    Vec3 add = Vec3(0, 0, 0);
    float alpha = 1.0f;                       // overall opacity
};

// One entry of the sync table 0x420499 (NOTES.md "The sync table and its events").
struct SyncEntry {
    int pos;      // order * 64 + row
    int event;    // the event value (part numbers 1..11, 101.., -1 = end)
    double time;  // music time at which main runs it: when xmpGetPos reaches pos (ctx.positionTime)
};

struct Ctx {
    int w = 0, h = 0;                      // frame size (4:3)
    const CloneExe* exe = nullptr;         // the original's program image
    const Soundtrack* music = nullptr;     // the rendered soundtrack (music.h)
    Target* out = nullptr;                 // the target the current part draws into (bound when render() is called)

    // ---- the current cue (struct Cue below, built by timeline.cpp), set before render()
    const Cue* cue = nullptr;
    double start = 0, end = 0;             // music time: the cue's event fires / the next cue's event fires
    double origin = 0;                     // music time at which clock(t) = clock0: the part's dt starts there
    double clock0 = 0;                     // the part's integrated dt (t70 units) at origin
    int part = 0;                          // main's part number [ebp-0x14] (1..11)

    // ---- the original's clocks (see the top of this file)
    double timer(double t) const { return t * kPitHz; }               // xmpGetTimer: PIT clocks since music start
    double t70(double t) const { return timer(t) / kT70Clocks; }      // main's t70 [0xe9572e]
    double t70ToTime(double u) const { return u * kT70Clocks / kPitHz; }  // music time of a t70 value
    // The current part's clock: the sum of the dt it has integrated at the frame drawn with timer(t), in t70 units:
    // clock0 + t70(t) - t70(origin). A variable `v += k * dt` from v0 at the part's first frame is v0 + k * clock(t).
    double clock(double t) const { return clock0 + t70(t) - t70(origin); }
    // Original frames since origin at the capture's frame rate in this part (Cue::fps), for per-frame state.
    double frames(double t) const;
    int position(double t) const;            // xmpGetPos as main uses it: order * 64 + row (one tick before it sounds)
    double positionTime(int pos) const;      // first music time with position(t) >= pos: main sees the row (events)
    double rowTime(int pos) const;           // music time at which row pos is heard (positionTime + one tick)
    double tickPeriod() const { return tickClocks / kPitHz; }  // one player tick (22,100 PIT clocks = 18.52 ms)

    // ---- the sync events (main runs them once per frame before the part draws; the remake at their music time)
    const std::vector<SyncEntry>& syncTable() const { return sync; }
    double eventTime(int event, int n = 0) const;   // music time of the n-th entry with this event (+inf if none)
    int eventCount(int event, double t) const;      // how many entries with this event have fired by t
    double lastEvent(int event, double t) const;    // music time of the last one fired by t (-inf if none)

    // The record of clone/re/full.frames nearest to music time t (one per timer tick, 53.99 per second).
    int captureTick(double t) const { return kCaptureTick0 + int(std::lround(t / tickPeriod())); }

    // ---- pictures (pictures.h): the ten stored pictures by name (clone/re/notes-gfx.md section 7: meet_us,
    // get_them_clones, connect_the_dots, tunnels_meet_more_tunnels, glyph1..glyph4, title_clone_meets_clone,
    // credits_aap_echo_simon), the redraw (art/<name>.png) unless --original-art.
    bool art() const { return pictures && pictures->useArt(); }
    const Picture& picture(const std::string& name) { return pictures->get(name); }
    // The original's blit 0x404002 of a picture with its top-left at (x, y) in VGA coordinates, 1:1 in original
    // pixels, at the original's blend level (0 = opaque .. 63, see overlayOpacity; continuous). Transparent pixels
    // keep what is below. Premultiplied blending (Blend::Premul stays set).
    void overlay(const Picture& p, float x, float y, double level = 0);
    void overlay(const std::string& name, float x, float y, double level = 0) { overlay(picture(name), x, y, level); }
    // A picture anywhere (destination rectangle in VGA coordinates, source rectangle in the original's pixels).
    void drawPicture(const Picture& p, const PictureDraw& d);

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another part at `t` into `target` (a placeholder if that part is not built). Restores ctx.out and
    // rebinds it afterwards. The cue fields stay those of the current cue.
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
    // 0x4043be fill rectangle (the black boxes, the tiles): a flat rectangle in VGA coordinates.
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
    std::vector<SyncEntry> sync;  // the sync table with the music time of each entry (Demo::init)
    double tickClocks = 22100;    // PIT clocks per player tick: 0x2d86bf / BPM of the module (Demo::init)
    int speed = 3;                // player ticks per row (the module's speed; the song never changes it)
    int depth = 0;                // nesting of renderPart
};

// The running order (timeline.cpp): one cue per sync event that selects a part (events 1..11 and 111), from the
// event's music time to the next such event; the part's clock from the capture's origin (notes-core 5.3).
struct Cue {
    std::string scene;      // the scene name (src/scenes)
    int part = 0;           // main's part number
    uint32_t routine = 0;   // the part routine the switch 0x400e80 calls
    int event = 0;          // the sync event that selects it
    double start = 0;       // music time at which the event fires (the part's first frame)
    double end = 0;         // music time at which the next cue starts (the end of the intro for the last)
    double origin = 0;      // music time of the timer the part's dt starts from (the previous part's last frame)
    double clock0 = 0;      // the part's integrated dt at origin (t70 units): 6.0 for part 1 (5.2), 0 for most
    double fps = 60;        // frames per second the capture shows in this part (PARTS.md), for ctx.frames
};
std::vector<Cue> buildTimeline(const Ctx& c);
// The music time at which the intro ends (timeline.cpp: main stops the music after event -1).
double introLength(const Ctx& c);

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
    };
    bool init(const CloneExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err);
    void resize(int w, int h);
    // Render the frame at music time t into the internal target and return it.
    const Target& render(double t);
    double length() const { return length_; }  // music time at which the intro ends
    const Cue* cueAt(double t) const;
    const char* partAt(double t) const { const Cue* c = cueAt(t); return c ? c->scene.c_str() : ""; }
    const std::vector<Cue>& cues() const { return cues_; }
    // Frees every GL object of the parts and the shared core objects. Call it while the GL context still exists.
    void release();
    Ctx ctx;

private:
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    std::vector<Cue> cues_;
    double length_ = 0;
    PictureStore pictures_;
    Target frame_;
};
