// The clone meets clone core library: building blocks several parts share (clone/re/notes-core.md sections 9..12,
// notes-gfx.md sections 2..7). Ports:
//   0x420491 the shared random generator (inlined at 31 sites in parts 1, 3, 6);
//   0x4034e7 / 0x4167b7 the log curves and their exp tables 0x420854 / 0x426b8c, as the gains they amount to;
//   0x428164 the tile colours (parts 7..11);
//   0x404002 / 0x40442c the picture blit with a level (ctx.overlay in demo.h) and the closed forms of the fade levels
//            of the title and credits (parts 1 and 11, notes-gfx 7.4);
//   0x404a08 the 8-bit buffers expanded through 256-entry palettes, as palette textures for shaders;
//   0x4045b4 the feedback blend, as its weight (the parts blend on the GPU).
// The generated textures (maps A, B, C and the grid's colour tables) are in textures.h.
//
// The namespace is cmc ("clone" is a POSIX function name).
//
// How a part uses it:
//
//   #include "clone/core.h"
//   using namespace cmc;
//
//   // a variable the original integrates v = clamp(v + rate * dt, lo, hi) once per frame from an event on
//   Ramp fade{c.eventTime(1101), 1.0, -0.0065, 0.0, 1.0};
//   double f = fade.at(c, t);
//
//   // the title of part 11 over its black box, at the original's blend level (continuous)
//   c.overlay("title_clone_meets_clone", 141, 104, tilesTitleLevel(c, t));
//
//   // the particles' random numbers, replayed from the image's seed
//   Random r(*c.exe); r.skip(kRandomAtPart6); int v = r.next();
//
//   // an 8-bit buffer computed on the GPU, coloured through a palette
//   PaletteTexture pt; pt.set(colours);   // in your shader: prepend cmc::kGLSL, then
//   // uniform sampler2D pal; vec3 c = cmPal(pal, index);  or cmPalLerp(pal, index) for a continuous index
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "demo.h"

class CloneExe;

namespace cmc {

// =============================================================================================== random generator
// 0x420491 (notes-core 9): seed = seed * 0x41c64e6d + 0x3039, returns (seed >> 10) & 0x7fff. One sequence from the
// image's seed (1) for the whole intro, never reseeded; parts 1, 3 and 6 draw from it, so what a part gets depends on
// how many numbers were drawn before it (frame-rate dependent where parts draw per frame).
class Random {
public:
    explicit Random(const CloneExe& exe);  // the seed from the image (dword 0x420491)
    explicit Random(uint32_t seed) : s(seed) {}
    int next() { s = s * 0x41c64e6du + 0x3039u; return int((s >> 10) & 0x7fff); }
    void skip(uint64_t n);  // n calls of next(), in O(log n)
    uint32_t s;
};
// Numbers drawn before a part started, in the capture (notes-core 9, models/core/rngstate.py; parts 2, 4, 5 and 7..11
// draw none): part 1 starts with 0, part 6 with 84,546 (all drawn by part 1's first run), part 3 with 216,354, the
// second run of part 1 with 315,145.
constexpr uint64_t kRandomAtPart6 = 84546;
constexpr uint64_t kRandomAtPart3 = 216354;
constexpr uint64_t kRandomAtPart1Again = 315145;

// =============================================================================================== time
// A value the original advances once per drawn frame by rate * dt (t70 units) and clamps to [lo, hi], from v0 at
// music time t0 (an event, or a part start: use c.origin there). Linear in dt, so a closed form of t70:
//   at(c, t) = clamp(v0 + rate * (t70(t) - t70(t0)), lo, hi)
// (exact as long as the clamp only stops the value at its end; the original starts it with the dt of the event's
// frame, i.e. from the frame before the event, 0..1 frame early: pass that t0 to match a capture frame by frame.)
struct Ramp {
    double t0 = 0, v0 = 0, rate = 0;
    double lo = -std::numeric_limits<double>::infinity(), hi = std::numeric_limits<double>::infinity();
    double at(const Ctx& c, double t) const;
};

// The overlay fade levels (notes-gfx 7.4), continuous, for ctx.overlay(picture, x, y, level):
//   part 1 title: [0x422514] = 63 - 0.45 dt per frame from event 106 (08:00), >= 0; drawn while [0x422510] != 0
//   (event 106 to event 111); credits: [0x42250c] = 63 - 0.75 dt per frame from event 107 (3c:10), >= 0;
//   part 11 title: f = [0x428464] = 1 - 0.0065 dt per frame from event 1101 (0b:00), >= 0, level = (1 - f) * 63.
double titleLevel(const Ctx& c, double t);       // part 1 (positions (141, 104))
double creditsLevel(const Ctx& c, double t);     // part 1 (141, 74), from event 107
double tilesTitleLevel(const Ctx& c, double t);  // part 11 (141, 104)

// =============================================================================================== colour encodings
// The textures store brightness as a logarithm so that the fillers can scale it by adding to the level
// (notes-gfx 2.4, notes-core 10.1 / 10.3 / 11.6 / 11.7):
//   0x4034e7 enc23(x) = clamp(trunc(ln(1 + x) * 23 + 128), 0, 255)          (grid colour tables)
//   0x4167b7 enc90(x) = clamp(trunc(ln((1 + x) / 128) * 1000 / 11 + 128), 0, 255)   (the maps)
// decoded by the exp tables 0x420854 / 0x426b8c at level L + k: (1 + x) * gain(k). The remake keeps linear values
// (textures.h: V = 1 + x on the 0..255 scale) and multiplies by the gain of the fillers' shade offset k:
inline double gain23(double k) { return std::exp((k - 128.0) / 23.0); }   // grid filler: 4.4 % per level
inline double gain90(double k) { return std::exp((k - 128.0) * 0.011); }  // texture mapper: 1.1 % per level
// The original's byte encoders (for exact checks against the snapshots; float argument as the code passes it).
int enc23(float x);
int enc90(float x);

// =============================================================================================== tile colours
// 0x428164: 64 entries of three dwords R, G, B in percent (an 8 x 8 mosaic, orange at the top left to violet at the
// bottom right; notes-parts3d 2.4, notes-grid 4). The parts index it by dword (idx += 3 per tile) and colour a tile
// trunc(value * 0.0125f * 255 * F), clamped at 255. tileColour returns value * 0.0125 per channel (80 % = 1.0,
// unclamped: multiply by the part's factor, then clamp); black for indices past the table (parts 8 and 10 read past
// it for tiles they always draw black).
Vec3 tileColour(const CloneExe& exe, int dwordIndex);
constexpr int kTileEntries = 64;

// =============================================================================================== palettes on the GPU
// 0x404a08 expands the 8-bit buffers of parts 1, 3, 6 in 8 x 8 blocks through a 256-entry dword palette chosen per
// block (per band of rows); the remake looks the index up per pixel. A palette of 256 display-space colours as a
// 256 x 1 RGBA16F texture (nearest).
using Colours = std::array<Vec3, 256>;
struct PaletteTexture {
    Texture tex;
    void set(const Colours& p);  // creates or updates (needs the GL context)
    void bind(int unit) const { tex.bind(unit); }
};
// 0x00RRGGBB dwords (as the original's palettes hold them) -> display space (byte / 255).
inline Vec3 rgbColour(uint32_t v) { return Vec3(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f); }

// =============================================================================================== feedback
// 0x4045b4 (part 1, once per frame): dst = (15 dst + src) >> 4, an exponential approach to src with factor 15/16 per
// original frame (the floor makes values rising towards src stop 1..15 below it: an 8-bit artefact). On the GPU:
// draw src over dst with Blend::Alpha and alpha = kFeedbackWeight, once per original frame (TickStepper).
constexpr float kFeedbackWeight = 1.0f / 16.0f;

// GLSL helpers; prepend to a fragment shader: shader.build(vs, std::string(cmc::kGLSL) + fs, "name").
//   vec3 cmPal(sampler2D pal, float i)       colour of index i (rounded, wraps at 256)
//   vec3 cmPalLerp(sampler2D pal, float i)   linear between the entries floor(i) and floor(i) + 1 (i clamped 0..255)
//   float cmGain23(float k), cmGain90(float k)   the gains above
extern const char* kGLSL;

}  // namespace cmc
