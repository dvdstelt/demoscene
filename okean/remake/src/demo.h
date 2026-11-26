// Demo framework: render context, part registry, clocks, cues (okean/re/PORT.md is the porters' guide to all of it).
//
// Time: everything is in screen refreshes of the original, counted as in okean/re/full.frames: refresh k is record k
// of the capture, program time k / 49.851 s (the intro's own "50 Hz" mode: 25.175 MHz / (808 x 625)). Each part
// animates from its retrace callback, which the timer calls once per refresh (okean/re/NOTES.md "The parts' common
// structure"); the music player runs on the GUS timer (19.84 ms per tick), and the parts follow its position. The
// remake's clock is fractional: a part is asked for the frame at, say, refresh 2000.37 and evaluates continuous motion
// there; state the original advances once per callback is stepped per whole refresh with TickStepper (replayed after
// a backwards seek) and blended by the fraction.
//
// Playback, --start, --shots, --video and --bench use seconds of music time t (t = 0 when the soundtrack starts):
// refresh = Soundtrack::refreshOf(t) (music.h).
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

class OkeanExe;
struct Soundtrack;
struct IntroSchedule;
struct Ctx;
struct Cue;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe, build tables, textures, meshes.
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
constexpr double kRefreshHz = 25175000.0 / (808.0 * 625.0);   // 49.8515 refreshes per second (the 50 Hz mode)

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
// Typical use: state stepped once per callback from the part's first callback, blended by the fraction:
//   int n = stepper.advance(c.callbacks(r), [&] { s = State(); }, [&](int k) { s.step(k); });
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

struct Ctx {
    int w = 0, h = 0;                      // frame size (4:3)
    const OkeanExe* exe = nullptr;         // the original's program image
    const Soundtrack* music = nullptr;     // the rendered soundtrack and the player's positions (music.h)
    const IntroSchedule* schedule = nullptr;  // the parts' refreshes and the tick times (schedule.h)
    Target* out = nullptr;                 // the target the current part draws into (bound when render() is called)

    // ---- the current cue (struct Cue below, built by timeline.cpp), set before render()
    const Cue* cue = nullptr;
    double start = 0, end = 0;             // refreshes: the part's first frame (F) / the record after its last

    // ---- the original's clocks (okean/re/notes-core.md section 4, schedule.h)
    // n: the callbacks done before the draw that record r shows (record F shows n = 0; fractional between records:
    // the remake evaluates continuous motion at n). Parts that compute their palette after the retrace wait (title,
    // objects, morph, landscape) use the state after n + 1 callbacks for the palette.
    double callbacks(double r) const { return r - start; }
    // The program time of callback c of the current part (c = 1: its first, in refresh F at line 502), and the music
    // position the callback sees.
    double callbackTime(int c) const;
    int positionAtCallback(int c) const;
    // The music position the draw shown at record F + n reads (after the copy of refresh F + n - 1, line 504).
    int positionAtDraw(int n) const;
    int position(double r) const;          // music position order * 64 + row at the end of record r (-1 before)
    // The record of okean/re/full.frames nearest to refresh r.
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
    int depth = 0;
};

// The running order (timeline.cpp, from schedule.h): one cue per part, from the refresh in which main calls it (the
// previous part's exit refresh) to its own exit refresh. Records enter..start-1 are its set-up and timer calibration:
// black, or the previous part's last frame (the one copied in `enter`).
struct Cue {
    std::string scene;        // the scene name (src/scenes)
    uint32_t entry = 0;       // the part's routine main calls (code32, PARTS.md)
    double enter = 0;         // the previous part's exit refresh
    double start = 0;         // F: the record of its first frame (the refresh of its first callback)
    double end = 0;           // its exit refresh (the next cue's enter)
    bool clears = false;      // enter..start-1 black (else the previous part's last frame)
};
std::vector<Cue> buildTimeline(Ctx& c);
// The refresh at which the intro ends (the program exits).
double demoEnd(const Ctx& c);

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
    };
    bool init(const OkeanExe& exe, const Soundtrack& music, const IntroSchedule& sched, int w, int h, const Options& o,
              std::string* err);
    void resize(int w, int h);
    // Render the frame at refresh r into the internal target and return it.
    const Target& render(double r);
    double length() const { return length_; }  // refresh at which the intro ends
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
