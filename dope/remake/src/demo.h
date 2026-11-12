// Demo framework: render context, part registry, timeline (dope/re/PORT.md is the porters' guide to all of it).
//
// Time: everything is in seconds of music time t, the clock of the soundtrack (music.h documents its zero; the
// first player tick, row 00:00, is at music->timeOfTick(0)). DOPE never synchronises the pictures with the music:
// the loader starts the player and then runs the seven parts one after the other, and each part times itself by the
// vertical retrace (70.0863 Hz, its int 8 handler runs once per displayed frame). So the remake places the parts at
// fixed times after the first player tick, taken from the reference capture (dope/re/full.frames, a run without
// disk time at 100 M instructions per second) with the three palette fades that the emulator ran four times too
// fast at one step per retrace, as on a real VGA (timeline.cpp, notes-core.md sections 5.2 and 6). Within a part:
//   - ctx.start is the part's first per-retrace call (its int 8 handler's first run): the part's clock origin;
//   - ctx.retraces(t) = (t - start) * 70.0863: continuous. During the frame of the (n+1)-th per-retrace call it runs
//     from n to n + 1. What that frame shows depends on the part (PORT.md "Time"): the engine parts' 3D scene shows
//     the state after n calls (the frame is built before the call and copied after it), their label and p1's / p4's
//     palette and scroll the state after n + 1 calls;
//   - TickStepper steps state that the original advances per retrace or per drawn frame.
//
// Screen: the original draws a 320x200 8-bit screen shown at 4:3. The remake renders a 4:3 frame of any size into
// ctx.out. "VGA coordinates" below mean x 0..320 (left to right) and y 0..200 (top to bottom) over the whole frame,
// so the pixel (x, y) of the original covers [x, x+1] x [y, y+1].
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

class DopeExe;
struct Soundtrack;
struct Ctx;
struct Cue;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe, build textures and meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at music time `t` (seconds) into ctx.out (already bound, viewport set, cleared to black).
    // ctx.start / ctx.end are the part's clock origin and end; t runs from ctx.load (the part's set-up screen,
    // t < start) to just before ctx.end. Must be a pure function of `t` as far as the viewer can tell: the player
    // seeks, and shots/videos render arbitrary times in any order. Stepped state uses TickStepper (replayed after a
    // backwards seek).
    virtual void render(Ctx& c, double t) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
// In a scene's .cpp: REGISTER_SCENE(LogoScene, "p1");
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul, Max, Min };
void setBlend(Blend b);

// The retrace of the 400-line 70 Hz timing (mode 13h and p1's mode X): 25.175 MHz / (800 x 449).
constexpr double kRetraceHz = 25175000.0 / (800.0 * 449.0);  // 70.0863 Hz
// Program time (s) of the first player tick (row 00:00, log tick 3) in dope/re/full.frames (notes-music.md 1).
constexpr double kCaptureFirstTick = 0.130240;

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
// Typical use: state stepped once per retrace from the part start, blended by the fraction:
//   int n = stepper.advance(c.retraces(t), [&] { s = State(); }, [&](int k) { s.step(k); });
struct TickStepper {
    int first = 0;         // first step that runs
    int done = INT_MIN;    // last step run
    template <class Reset, class Step>
    int advance(double steps, Reset&& reset, Step&& step) {
        int target = int(std::floor(steps));
        if (done == INT_MIN || target < done) { reset(); done = first - 1; }
        while (done < target) step(++done);
        return target;
    }
};

// The reference capture's frame grid and the remake's. Capture record f (dope/re/full.frames) covers program time
// [f, f + 1) / kRetraceHz. The remake counts "records" on the same grid (record r starts at music time
// recordTime(r)) up to p5's hand-over fade; from there the remake runs later than the capture, because the three
// fades of notes-core 5.2 take one retrace per step instead of a quarter. `sync` holds the corresponding points
// (timeline.cpp); between them the map is linear, outside them it has slope 1.
struct CaptureClock {
    double zero = 0;  // capture program time of music time 0: kCaptureFirstTick - music->timeOfTick(0)
    std::vector<std::pair<double, double>> sync;  // (remake record, capture record), increasing
    double recordTime(double rec) const { return rec / kRetraceHz - zero; }  // music time of remake record rec
    double record(double t) const { return (t + zero) * kRetraceHz; }        // remake record at music time t
    double toCapture(double rec) const;                                     // remake record -> capture record
    int frame(double t) const { return int(std::floor(toCapture(record(t)))); }  // capture record shown at t
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

// How ctx.drawPicture places a picture (C++20: c.drawPicture(pic, {.x = a - 70, .y = 8, .w = 70, .h = 44})).
struct PictureDraw {
    float x = 0, y = 0, w = 320, h = 200;     // destination rectangle in VGA coordinates
    float sx = 0, sy = 0, sw = -1, sh = -1;   // source rectangle in the picture's frame, original pixels (-1: all)
    Vec3 mul = Vec3(1, 1, 1);                 // colour = clamp(colour * mul + add, 0, 1) (fades: mul = L / 256)
    Vec3 add = Vec3(0, 0, 0);
    float alpha = 1.0f;                       // overall opacity
    float clipX0 = -1e9f, clipX1 = 1e9f;      // only the part of the rectangle with clipX0 <= x < clipX1 is drawn
};
// The DAC scaling (v * L) >> 8, L = 0..256, as PictureDraw::mul.
inline Vec3 fadeMul(double level) { float f = float(level / 256.0); return Vec3(f, f, f); }

struct Ctx {
    int w = 0, h = 0;                      // frame size (4:3)
    const DopeExe* exe = nullptr;          // the original's images (exe.h) and DOPE.MOD
    const Soundtrack* music = nullptr;     // tick times and positions of the original player
    Target* out = nullptr;                 // the target the current part draws into (bound when render() is called)
    CaptureClock clock;                    // music time <-> the reference capture's frames

    // ---- the current part (set before render())
    std::string scene;                     // its scene name ("p1" .. "p7")
    int part = 0;                          // 1..7
    double load = 0;                       // the part takes over the screen (set-up screen until start)
    double start = 0;                      // its first per-retrace call: the part's clock origin
    double end = 0;                        // its last frame ends (exit fades included)

    // ---- the original's clocks
    double retraces(double t) const { return (t - start) * kRetraceHz; }  // per-retrace calls since start, continuous
    double retraces(double t, double since) const { return (t - since) * kRetraceHz; }
    double retraceTime(double r) const { return start + r / kRetraceHz; }  // music time at r retraces
    int position(double t) const;            // the player's position, order << 8 | row (only for display: nothing
    double positionTime(int pos) const;      //   in DOPE reads the music); first music time position(t) >= pos
    double tickTime(int k) const;            // music time of the player's tick k (0 = the first, row 00:00)
    int captureFrame(double t) const { return clock.frame(t); }  // the capture record that matches the remake at t

    // ---- pictures (pictures.h): the redraw (art/<name>.png) unless --original-art, else the original's pixels.
    bool art() const { return pictures && pictures->useArt(); }
    const Picture& picture(const std::string& name, const PictureOptions& o = {}) { return pictures->get(name, o); }
    const Picture& picture(const PictureDef& def, const PictureOptions& o = {}) { return pictures->get(def, o); }
    // Draws a picture with premultiplied-alpha blending (Blend::Premul stays set). Original pixels are drawn as
    // sharp squares with anti-aliased edges, redraws filtered.
    void drawPicture(const Picture& p, const PictureDraw& d = {});

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another part at `t` into `target` (a placeholder if that part is not built), with that part's cue
    // (start, end, ...) set while it renders. Restores the current part's values and rebinds ctx.out afterwards.
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
    // Text in a 5x7 pixel font (for placeholders and debugging): `size` = VGA units per font pixel.
    void text(float x, float y, float size, const std::string& s, Vec3 color);

    // One shared instance of T per context, created on first use as T(ctx) (for the core library: tables,
    // textures and shaders that several parts use). Created with the GL context current.
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
    const std::vector<Cue>* cues = nullptr;
    int depth = 0;  // nesting of renderPart
};

// The running order (timeline.cpp): CDPI runs p1..p7 one after the other (notes-core.md sections 2, 5, 6).
struct Cue {
    std::string scene;      // "p1" .. "p7"
    int part = 0;           // 1..7
    double load = 0;        // the part takes over the screen (its set-up screen until start)
    double start = 0;       // its first per-retrace call
    double end = 0;         // its last frame ends; render() is called for load <= t < end
    double hold = 0;        // the screen keeps the frame before end until here (>= end); black after
};
std::vector<Cue> buildTimeline(Ctx& c);  // also fills c.clock.sync

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
    };
    bool init(const DopeExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err);
    void resize(int w, int h);
    // Render the frame at music time t into the internal target and return it.
    const Target& render(double t);
    double length() const;  // music time at which the demo ends (p7 sets text mode)
    const Cue* cueAt(double t) const;
    const char* partAt(double t) const { const Cue* c = cueAt(t); return c ? c->scene.c_str() : ""; }
    const std::vector<Cue>& cues() const { return cues_; }
    // Frees every GL object of the parts and the shared core objects. Call it while the GL context still exists.
    void release();
    Ctx ctx;

private:
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    std::vector<Cue> cues_;
    PictureStore pictures_;
    Target frame_;
};
