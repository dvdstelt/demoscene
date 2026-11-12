// Demo framework: render context, part registry, timeline (stars/re/PORT.md is the porters' guide to all of it).
//
// Time: everything is in seconds of music time t. t = 0 is the moment the original's player starts the GUS timers
// (0x1c759 -> 0x1a91c, called by main after its precalc: program time 3.98976 s, frame 279 of stars/re/full.frames);
// the player's first tick follows 20.48 ms later. The clock is the audible position of the soundtrack. Three
// clocks of the original are derived from it (stars/re/NOTES.md "Timing"):
//   - the music position (order << 8 | row, as 0x29f80 returns it) on the GUS timers: ctx.position(t);
//   - the PIT at 1193182 / 0x4286 = 70.0635 Hz, which runs the parts' callbacks: ctx.ticks(t) counts them from the
//     part's start, continuously (the original's tick counter is its integer part, give or take the phase);
//   - the vertical retrace at 70.0863 Hz (mode 13h), which steps main's fades and the per-frame parts:
//     ctx.retraces(t). TickStepper steps per-frame state at a fixed rate.
//
// Screen: the original draws a 320x200 8-bit screen shown at 4:3. The remake renders a 4:3 frame of any size into
// ctx.out. "VGA coordinates" below mean x 0..320 (left to right) and y 0..200 (top to bottom) over the whole frame,
// so the pixel (x, y) of the original covers [x, x+1] x [y, y+1].
#pragma once
#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include "pictures.h"
#include "render.h"

class StarsExe;
struct Soundtrack;
struct Ctx;

struct Scene {
    virtual ~Scene() = default;
    // Once at start-up with the GL context current: read the data from the exe, build textures and meshes.
    virtual void init(Ctx&) {}
    // Draw the frame at music time `t` (seconds) into ctx.out (already bound, viewport set, cleared to black).
    // ctx.start / ctx.end are the part's start and end. Must be a pure function of `t` as far as the viewer can
    // tell: the player seeks, and shots/videos render arbitrary times in any order. Stepped state uses TickStepper
    // (replayed after a backwards seek).
    virtual void render(Ctx& c, double t) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
// In a scene's .cpp: REGISTER_SCENE(CloudScene, "cloud");
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul, Max, Min };
void setBlend(Blend b);

// The original's clocks (see the top of this file).
constexpr double kPitHz = 1193182.0 / 0x4286;                 // 70.0635 Hz: IRQ 0, the parts' PIT callbacks
constexpr double kRetraceHz = 25175000.0 / (800.0 * 449.0);  // 70.0863 Hz: the vertical retrace of mode 13h
constexpr double kCaptureMusicStart = 3.98976;  // program time of t = 0 in stars/re/full.frames (notes-music 5.3)

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

// How ctx.drawPicture places a picture (C++20: c.drawPicture(pic, {.mul = fadeMul(level)})).
struct PictureDraw {
    float x = 0, y = 0, w = 320, h = 200;     // destination rectangle in VGA coordinates
    float sx = 0, sy = 0, sw = -1, sh = -1;   // source rectangle in the original's pixel grid (-1: all of it)
    Vec3 mul = Vec3(1, 1, 1);                 // colour = clamp(colour * mul + add, 0, 1) (fades: mul = L / 256)
    Vec3 add = Vec3(0, 0, 0);
    float alpha = 1.0f;                       // overall opacity
};
// The palette fade (min(63, c * L >> 8), L = 0..256) as PictureDraw::mul.
inline Vec3 fadeMul(double level) { float f = float(level / 256.0); return Vec3(f, f, f); }

struct Ctx {
    int w = 0, h = 0;                      // frame size (4:3)
    const StarsExe* exe = nullptr;         // the original's program image and overlay files
    const Soundtrack* music = nullptr;     // positions and tick times of the original player
    Target* out = nullptr;                 // the target the current part draws into (bound when render() is called)
    int minute = 0;                        // the variant: minute & 1 of the clock at start ([4]); 0 = the capture's
    double start = 0, end = 0;             // the current part's start and end (music time), set before render()
    uint32_t routine = 0;                  // the current part's routine in the original (Cue::routine)

    // ---- the original's clocks
    double ticks(double t) const { return (t - start) * kPitHz; }        // PIT ticks since the part started
    double ticks(double t, double since) const { return (t - since) * kPitHz; }
    double retraces(double t) const { return (t - start) * kRetraceHz; }  // retraces since the part started
    double retraces(double t, double since) const { return (t - since) * kRetraceHz; }
    int position(double t) const;            // 0x29f80: order << 8 | row, the next row the player reads
    double positionTime(int pos) const;      // first music time at which position(t) >= pos
    double afterPos(int x) const { return positionTime(x + 1); }  // `call 0x29f80; cmp ax, x; jbe` ends here
    double tickTime(int k) const;            // music time of the player's tick k (0 = the first)
    // The record of stars/re/full.frames on screen at music time t (frames start at line 0, 70.0863 per second).
    static int captureFrame(double t);

    // ---- pictures (pictures.h): overlay file n as a texture, the redraw (art/fileNN.png) unless --original-art.
    bool art() const { return pictures && pictures->useArt(); }
    const Picture& picture(int n, const PictureOptions& o = {}) { return pictures->get(n, o); }
    // Draws a picture with premultiplied-alpha blending (Blend::Premul stays set). Original pixels are drawn as
    // sharp squares with anti-aliased edges, redraws filtered.
    void drawPicture(const Picture& p, const PictureDraw& d = {});

    // Borrow a scratch target: same size as the frame times `scale`, RGBA16F unless fmt says otherwise.
    TargetPool::Lease temp(bool depth = false, float scale = 1.0f, GLenum fmt = GL_RGBA16F);
    TargetPool::Lease tempSized(int w, int h, bool depth = false, GLenum fmt = GL_RGBA16F);
    // Render another part at `t` into `target` (a placeholder if that part is not built). Restores ctx.out and
    // rebinds it afterwards; set ctx.start / ctx.end for the other part yourself if it reads them.
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
    int depth = 0;  // nesting of renderPart
};

// The running order (timeline.cpp): main's script 0x4d as music times, see stars/re/notes-core.md section 2.
struct Cue {
    std::string scene;
    uint32_t routine = 0;   // the routine main calls (0 = main's own script: picture sequences)
    double start = 0;       // music time at which the routine starts drawing
    double end = 0;         // music time at which it returns
    double hold = 0;        // the screen keeps its last frame until here (>= end: set-up of the next part)
};
std::vector<Cue> buildTimeline(const Ctx& c);
// The music time at which the demo ends (text mode after the last fade): the end of the last cue.

class Demo {
public:
    struct Options {
        std::string artDir;   // the redraws (empty: none)
        bool art = true;      // false: --original-art
        int minute = 0;       // the variant (0 = even minute, as in the capture)
    };
    bool init(const StarsExe& exe, const Soundtrack& music, int w, int h, const Options& o, std::string* err);
    void resize(int w, int h);
    // Render the frame at music time t into the internal target and return it.
    const Target& render(double t);
    double length() const;  // music time at which the demo ends
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
