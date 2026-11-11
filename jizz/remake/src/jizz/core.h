// The JIZZ core library: the building blocks several parts share (jizz/re/notes-core.md). Ports:
//   0x141f6 random generator (and the seeds of 0x11dfe / 0x12e72),       0x6592 spot table, 0x16f4f polar table,
//   0x1a336 / 0x1a229 engine colour tables and the inversion decay of the frame start 0x638b,
//   0x1131b / 0x179f6 fade tables and page fade, 0x17979 / 0x17999 timer fade, 0x1151c inset frame,
//   0x1122f / 0x1145c thick lines, 0x183a9 jittered quads, and the glow chain >> 3 / 0x11eb2 / 0x12111 / 0x11396.
//
// Units used throughout:
//   - VGA coordinates: x 0..320, y 0..200 over the whole frame, y down (demo.h). Where the original works with
//     pixel indices (line end points, quad corners, inset positions), the remake takes the same numbers as floats;
//     the pixel with index (i, j) covers [i, i+1] x [j, j+1] in VGA coordinates.
//   - Colours are display-space floats 0..1; white = (1, 1, 1) (the original's 0xffdf).
//   - Glow and line buffers hold the original's byte values as floats: a line is drawn with 249 (0xf9), which the
//     >> 3 of the glow turns into 31.125, and the fades read v / 32.
//
// ---------------------------------------------------------------------------------------------------------------
// How a part uses it (all functions draw into ctx.out unless they say otherwise):
//
//   #include "jizz/core.h"
//   using namespace jizz;
//
//   // timer fade from white, started when the sync event 301 is set (0x17003: 0x17979(0x2dc6c0)):
//   double since; if (c.event(t, &since) == 301) timerFade(c, t, since, 0x2dc6c0);
//
//   // engine colours (in your shader: kGLSL prepended, uniform vec3 jzESF; colour = jzColourTable(texel + shade, jzESF)):
//   ColourTable ct{-200, 0x1c8, 0};                       // 0x17003 until the 03:10 flash
//   ct = ColourTable{0, 256, inversionDecay(t, tFlash, 35.0)};   // from the flash on: F = 255 - 9 per frame
//   ct.set(shader);
//
//   // glow overlay (0x17003 / 0x169e7 order), stepped at the capture's frame rate:
//   Glow glow;                                            // a member of the part: it keeps its buffers
//   Glow::Source src;
//   src.params = [](int n) { GlowStep s; s.blur = true; s.shift = 1; return s; };
//   src.draw = [&](Ctx& c, int n) {                       // draws step n's lines into c.out (the line buffer)
//       Rng r = rngTrack.at(n, [&](int k) { return 4 * quadCount(k); });
//       std::vector<Segment> segs;
//       jitterQuads(r, quads, first(n), last(n), 7, segs);
//       drawLines(c, segs);                               // value 249 = 0xf9
//   };
//   glow.advance(c, (t - t300) * 25.5, 0, src);           // steps = (t - phase start) * rate; step 0 first
//   glow.fadeTowards(c, kWhite);                          // 0x11396: the page towards white by v / 32
//
//   // inset: 3D view drawn into a sub-rectangle, then the white frame
//   Inset in{130, 20, 175, 110};
//   Mat4 proj = in.clip() * myFullScreenProjection; in.scissor(c); drawScene(); Inset::noScissor();
//   insetFrame(c, in);
//
// GLSL helpers: prepend jizz::kGLSL to a fragment shader (Shader::build adds #version and kCommonGLSL first):
//   shader.build(vs, std::string(jizz::kGLSL) + fs, "name");
// ---------------------------------------------------------------------------------------------------------------
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "demo.h"

class JizzExe;

namespace jizz {

inline const Vec3 kWhite(1.0f, 1.0f, 1.0f);
inline const Vec3 kBlack(0.0f, 0.0f, 0.0f);

// Compiles the core's shaders now (call it from Scene::init); otherwise they are built on first use.
void prepare(Ctx& c);

// =============================================================================================== random generator
// 0x141f6, exact (three dwords of state at 0x141ea / 0x141ee / 0x141f2; returns the new third word). One sequence
// runs through the whole intro: 80 calls in the precalc (0x11dfe) from (0, 0, 0); each texture script reseeds it
// (0x12e72) and the scripts leave it at the seed advanced 131,070 times; after that only the jittered quads
// (0x183a9, 4 calls per quad per drawn frame) call it: 0x17003 at event 300 (620 calls in the capture), then
// 0x169e7 at event 510 (1,076 calls in the capture).
class Rng {
public:
    uint32_t a = 0, b = 0, c = 0;

    Rng() = default;
    Rng(uint32_t a_, uint32_t b_, uint32_t c_) : a(a_), b(b_), c(c_) {}
    uint32_t next();
    uint32_t operator()() { return next(); }
    Rng& skip(uint64_t calls);  // advance by `calls` numbers
    bool operator==(const Rng& o) const { return a == o.a && b == o.b && c == o.c; }
    bool operator!=(const Rng& o) const { return !(*this == o); }

    static constexpr int kPrecalcCalls = 80;       // 0x11dfe
    static constexpr int kTextureCalls = 131070;   // all texture scripts (measured: state at t430 = seed + 131,070)
    static constexpr int kRibbonCalls = 620;       // 0x17003 event 300 in the capture (state at t2515)

    static Rng afterPrecalc();                     // (0, 0, 0) advanced 80 times
    static Rng textureSeed(const JizzExe& exe);    // the state 0x12e72 sets before each texture script
    // The state after the texture generator = the state the event-300 rectangles start from: the texture seed
    // advanced 131,070 times (0x44273c73, 0x19e7ee6a, 0xd3a3c270). The 510 rectangles of 0x169e7 start where the
    // 300 ones stopped: afterTextures(exe).skip(kRibbonCalls) is the capture's state there.
    static Rng afterTextures(const JizzExe& exe);
};

// The 80 numbers of the precalc 0x11dfe, in call order: TBL's spark speeds 0x11666[i] = r[2i] and the start
// values 0x115c6[i] = r[2i + 1] (i = 0..39; notes-parts2d 3.2).
std::array<uint32_t, 80> precalcRandoms();

// The generator's state before step n of a stepped phase, replayed from the state before step `first`.
// calls(k) = number of numbers step k takes. Requests for a later step continue from the last request; an earlier
// step replays from the start (cheap: a few thousand calls).
class RngTrack {
public:
    void start(const Rng& s, int first) {
        if (valid_ && s == start_ && first == first_) return;
        start_ = cur_ = s; first_ = at_ = first; valid_ = true;
    }
    template <class Calls> Rng at(int n, Calls&& calls) {
        if (n < at_) { cur_ = start_; at_ = first_; }
        while (at_ < n) { cur_.skip(uint64_t(calls(at_))); at_++; }
        return cur_;
    }
private:
    Rng start_, cur_;
    int first_ = 0, at_ = 0;
    bool valid_ = false;
};

// =============================================================================================== precalc functions
// 0x6592 spot table (256x256 bytes, centre at column 127, row 128): (255 - r)^4 / 2^24 in byte units
// (0 .. 252.02), r = distance in texels of the 256-texel map; 0 beyond r = 255.
float spot(float r);
// The spot table as a map, at continuous texel coordinates (i, j) (texel (i, j) of the table is at exactly (i, j);
// wraps modulo 256 like the original's reads).
float spotMap(float i, float j);
// 0x16f4f polar table: (x, y) in original pixels from the centre (VGA orientation, y down) -> (u, v) in texel units
// of a 256x256 map: u = 256 atan2(y, x) / pi (512 units per turn: the map repeats twice around the centre),
// v = 1.3 sqrt(x^2 + y^2); both wrap modulo 256 in the texture read. For the screen's own centred window
// (table offset 0x29270), x = X - 160, y = Y - 100 with (X, Y) VGA coordinates.
void polar(float x, float y, float& u, float& v);

// GLSL, prepend to fragment shaders (uses PI from kCommonGLSL):
//   float jzSpot(float r)                    spot(r) above
//   float jzSpotMap(vec2 t)                  spotMap(t.x, t.y) above
//   vec2  jzPolar(vec2 p)                    polar(p) above: (u, v) in texel units
//   mat2  jzPolarJacobian(vec2 p)            d(u, v) / d(x, y) (for derivatives without a seam at the atan2 cut)
//   vec4  jzTex256(sampler2D m, vec2 t)      filtered read of a 256-texel map (GL_REPEAT) at texel coordinates t,
//                                            texel i's centre at t = i (the original's integer index); the map may
//                                            have any resolution, it covers the original's 256 texels
//   vec4  jzTex256Grad(m, t, dtdx, dtdy)     same with explicit derivatives (texel units)
//   vec4  jzTexPolar(m, vec2 p, vec2 off)    m read at jzPolar(p) + off, mipmapped without the atan2 seam
//   vec3  jzColourTable(vec3 i, vec3 esf)    0x1a336: i = texel + shade per channel (0..2, each byte / 255),
//                                            esf = (E, S, F) as ColourTable::set uploads them
//   vec3  jzFade(vec3 c, vec3 C, float v)    0x1131b row v (0..32, continuous): mix(c, C, v / 32)
//   float jzGlowValue(sampler2D a, sampler2D b, float f, vec2 uv)   Glow result blended (see Glow::bind)
extern const char* kGLSL;

// =============================================================================================== colour tables
// 0x1a336(E) with S = [0x1a225], F = [0x1a478]: what the vector engine's 768-entry tables do to texel + shade,
// per channel, in 0..1:  x = clamp((texel + shade) * S / 256 + E / 255, 0, 1);  x = mix(x, 1 - x, F / 256).
// Parameter sets used in the intro (notes-core 6.3): precalc / plain {0, 256, 0}; 0x17003 until 03:10
// {-200, 0x1c8, 0}; 0x17003 from the 03:10 flash on {0, 256, F decaying}; 0x15d20 {-40, 256, 0};
// 0x10fda {-50, 0x132, 255 during event 0x1400 else 0}.
struct ColourTable {
    float E = 0;    // brightness, signed 8-bit units
    float S = 256;  // contrast, 8.8 fixed point (256 = 1.0)
    float F = 0;    // inversion 0..255 (255 = negative)
    Vec3 apply(Vec3 texel, float shade) const;          // texel and shade 0..1 (byte / 255)
    void set(const Shader& s, const char* uniform = "jzESF") const;  // uploads vec3(E, S, F)
};

// The inversion F after a flash (0x17003 event 311: F = 255, decay 9 per frame in 0x638b): F after k frames is
// max(0, f0 - k * dec) (255, 246, ..., 3, 0: 29 frames non-zero), k = (t - t0) * rate frames after the trigger
// frame t0 (which is drawn with f0). blend: linear between frame k and k + 1 (smooth at any refresh rate); false
// keeps the value of frame k. Returns 0 before t0.
float inversionDecay(double t, double t0, double rate, int f0 = 255, int dec = 9, bool blend = true);

// =============================================================================================== fades
// 0x17979(rate) at music time t0 / 0x17999 at t: level = 1023 - (timer(t) - timer(t0)) * rate / 2^32 (continuous,
// the original uses level >> 5 in 32 steps), 0 when done or before t0.
double timerFadeLevel(const Ctx& c, double t, double t0, uint32_t rate);
// The timer fade as a pass over ctx.out: out = mix(out, C, level / 1024). C = white everywhere except the logo
// part 0x1212a (black). Rates are code immediates (notes-core 7.3: 0x2c4020, 0x2dc6c0, ...).
void timerFade(Ctx& c, double t, double t0, uint32_t rate, Vec3 C = kWhite);
// 0x179f6 with the tables of 0x1131b: out = mix(out, C, amount); a caller with its own 5-bit level k passes
// amount = k / 32 (continuous; the original's row 0 = c * 31/32 and the dropped green bit are not modelled).
void pageFade(Ctx& c, float amount, Vec3 C = kWhite);

// =============================================================================================== insets
// The engine's viewport: [0x15c3b] = page offset y * 640 + 2x, [0x18d64] = w, [0x18d68] = h, in VGA pixels.
struct Inset {
    float x = 0, y = 0, w = 320, h = 200;

    static Inset fromOffset(uint32_t pageOffset, float w, float h) {
        return {float((pageOffset % 640) / 2), float(pageOffset / 640), w, h};
    }
    bool full() const { return x == 0 && y == 0 && w == 320 && h == 200; }
    // Clip-space transform that shows a full-screen view in the inset: clip_inset = clip() * clip_full (the engine
    // projects into the w x h viewport with the full screen's view scaled: x = w/2 + (w/320) dist X/Z).
    Mat4 clip() const;
    // Limits drawing to the inset (scissor rounded outwards to whole output pixels; the frame covers the edge).
    void scissor(const Ctx& c) const;
    static void noScissor();
};
// 0x1151c: a 1-pixel (of 320) outline just outside the inset, [x-1, x+w+1] x [y-1, y+h+1], with all four corners
// (the original misses the two right corners for odd widths). Drawn only when the offset is not 0, like the
// original (inset (0, 0) has no frame).
void insetFrame(Ctx& c, const Inset& in, Vec3 colour = kWhite);
// Anti-aliased axis-aligned rectangle in VGA coordinates (alpha blended into ctx.out).
void aaRect(Ctx& c, float x, float y, float w, float h, Vec3 colour, float alpha = 1.0f);

// =============================================================================================== lines
// A 0x1145c line between pixel indices (x0, y0) and (x1, y1) (floats allowed: unrounded positions).
struct Segment {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
// 0x1145c draws nothing when an end point is outside the 320x200 page (no clipping): the test on the rounded
// coordinates. Parts that want to keep this rejection filter with it (jitterQuads does).
bool onScreen(const Segment& s);

// Draws 0x1145c lines into ctx.out (binds it) as anti-aliased strokes with the original's shape: 4 pixels (of 320)
// thick along the minor axis, from the end pixels' row (x-major: rows y..y+3) or column (y-major: x..x+3), i.e.
// 1.5 pixels below / right of the line through the end pixels' centres, and half a pixel past each end pixel.
// Overwrites with coverage, dst = mix(dst, value, coverage), like the original's stores; `value` goes to every
// channel (normally a single-channel float buffer such as the glow's line buffer). Leaves blending and the depth
// test off.
void drawLines(Ctx& c, const Segment* segs, size_t n, float value = 249.0f);
inline void drawLines(Ctx& c, const std::vector<Segment>& segs, float value = 249.0f) {
    if (!segs.empty()) drawLines(c, segs.data(), segs.size(), value);
}

// Four corners (pixel indices, floats allowed) of a quad as 0x18228 writes them (corner k to corner (k+1) & 3).
struct Quad {
    float x[4] = {}, y[4] = {};
};
// 0x183a9: quads first..last (inclusive), one random number r per edge k (corner k to corner (k+1) & 3); the end
// points get + (r & mask), + (r ror 4 & mask) to x0, y0 and + (r ror 8 & mask), + (r ror 12 & mask) to x1, y1
// (mask 7 in both callers). Appends the edges that 0x1145c would draw (onScreen, not zero-length) to `out`; the
// generator advances 4 calls per quad either way.
void jitterQuads(Rng& rng, const Quad* quads, int first, int last, uint32_t mask, std::vector<Segment>& out);

// =============================================================================================== glow
// One step of the glow chain = one drawn frame of the original (notes-core 8.6).
struct GlowStep {
    bool blur = false;     // 0x11eb2 (2x2 box -> Gaussian of sigma 0.5 original pixels, shifted half a pixel
                           //   up-left like the box); Overlay order: in place on the line buffer; Tbl: trail -> B
    int shift = 1;         // 0x12111: trail T += (B - T) / 2^shift; 0 = no trail (T = B)
    float trailBias = 0;   // subtracted from the trail per step (models 0x12111's truncation; notes-parts2d 3.6)
};

// The glow pipeline on the GPU, at the output resolution (times `scale`), values in byte units (single-channel
// float targets). Per step:
//   Overlay (0x17003, 0x169e7): B = (B with this step's lines drawn over it) / 8; if blur: B = gauss(B);
//                               T = T + (B - T) / 2^n. The result is T (0x11396: page towards C by T / 32).
//                               The capture's rates: rings 35.5 fps (shift 2, no blur), rectangles 25.5 fps
//                               (event 300) and 34.5 fps (event 510), both blur and shift 1.
//   Tbl (TBL 0x11986):          X = this step's picture (drawn on 0, byte units); T = T + (X / 8 - T) / 2^n;
//                               B = blur ? gauss(T) : T. The result is B (TBL reads it through its LUT); TBL uses
//                               blur = true, shift = 3 at 70/3 fps.
// gauss: 0x11eb2's 2x2 box has a variance of 1/4 pixel^2 per axis and is centred half a pixel right / down; the
// remake uses a Gaussian of sigma 0.5 original pixels with the same half-pixel offset, at the buffer resolution.
// Measured against the integer model (tools/coretest.py glow): same picture (correlation 0.98-0.999), the float
// trail is about 6 % brighter on average because the original truncates at every step (trailBias can model that).
// The state is kept between frames and advanced step by step while the steps increase; after any other jump it
// is rebuilt by simulating the last N steps from cleared buffers (N such that older state contributes < 1/1000,
// from the steps' shifts: 52 steps at shift 3, 10 at shift 1), or from the phase start (Source::reset) when that
// is closer. The result is blended between step n and n + 1 by the fraction of `steps`.
class Glow {
public:
    enum Order { Overlay, Tbl };
    struct Source {
        std::function<GlowStep(int n)> params;                        // empty: GlowStep{} for every step
        std::function<void(Ctx& c, int n)> draw;                      // step n's lines / picture into c.out
        std::function<void(Ctx& c, Target& lines, Target& trail)> reset;  // state before step `first`; empty: 0
    };

    explicit Glow(Order order = Overlay, float scale = 1.0f, bool blendSteps = true)
        : order_(order), scale_(scale), blend_(blendSteps) {}
    Glow(const Glow&) = delete;
    Glow& operator=(const Glow&) = delete;
    ~Glow();

    // Brings the state to `steps` (fractional: (t - phase start) * rate) where `first` is the first step after the
    // reset. Calls src.draw for the steps it simulates (with c.out = the line buffer, bound, blending off), in
    // order. Leaves ctx.out bound, blending, scissor and depth test off.
    void advance(Ctx& c, double steps, int first, const Source& src);
    void invalidate() { have_ = kNone; }  // forces a rebuild (e.g. when the source's content changed)
    void prepare(Ctx& c) { ensure(c); }   // allocates the buffers now (from Scene::init) instead of on first use

    // 0x11396: ctx.out = mix(ctx.out, C, v / 32), v = the result blended between the two step states.
    void fadeTowards(Ctx& c, Vec3 C = kWhite) const;
    // For own shaders: binds the result of step n to `unit` and of step n + 1 to `unit + 1`; the value is
    // mix(a, b, fraction()) (GLSL jzGlowValue). Both are single-channel (red) float textures, linear filtered.
    void bind(int unit) const;
    float fraction() const { return blend_ ? float(frac_) : 0.0f; }
    int step() const { return have_ - 1; }  // n: the earlier of the two states shown

    static void fill(Target& t, float v);  // clears a target to v (for Source::reset)

private:
    struct Slot { Target lines, trail; };
    static constexpr int kNone = -0x7fffffff;
    void ensure(Ctx& c);
    void doStep(Ctx& c, int k, const Source& src);
    void resetSlot(Ctx& c, Slot& s, bool phaseStart, const Source& src);
    int history(int want, int first, const Source& src) const;
    const Target& result(const Slot& s) const { return order_ == Overlay ? s.trail : s.lines; }

    Order order_;
    float scale_;
    bool blend_;
    Slot slot_[2];
    int cur_ = 0;          // slot holding S(have_); the other holds S(have_ - 1)
    int have_ = kNone;     // last step simulated
    int first_ = 0;
    int w_ = 0, h_ = 0;
    double frac_ = 0;
};

}  // namespace jizz
