// The STARS core library: building blocks several parts share (stars/re/notes-core.md, and the tables the part
// notes list). Ports:
//   0x4af10 the random generator (seed dword [0x4af35]),
//   0xa740 / 0x14260 / 0xffb8 / 0x5599c the sine and cosine tables, as continuous functions and as exact reads,
//   0x4a720 / 0x17e96 / 0x4a80b / 0x4a7b0 the palette fader (main's fades per retrace, the parts' per PIT tick),
//   palettes as GPU lookup textures for effects that compute palette indices.
//
// How a part uses it:
//
//   #include "stars/core.h"
//   using namespace stars;
//
//   // main's fade of picture 57: fade_17e96(+4, 64) once the position passes 0x5500 (notes-core 2.2)
//   Fade in = mainFade(c.afterPos(0x5500), 0, +4, 64);
//   c.drawPicture(c.picture(57), {.mul = fadeMul(in.level(t))});
//
//   // a part's fade stepped by its PIT callback from the part start: level 0, +4 per tick, 64 ticks
//   Fade f{c.start, kPitHz, 0, 4, 64};
//
//   // the sine table 0xa740 at a fractional index (S[i] = round(65528 sin(2 pi i / 4096)))
//   double s = kSinA(i);            // continuous
//   int32_t e = kSinA.at(*c.exe, k); // the original's entry k (exact ports, checks)
//
//   // the particles' random numbers, replayed from the seed
//   Random r(*c.exe); uint32_t v = r.next(320);
//
//   // an effect computing 8-bit indices on the GPU: look them up in a palette texture
//   PaletteTexture pt; pt.set(vgaPalette(file));      // in your shader: prepend stars::kGLSL, then
//   // uniform sampler2D pal; vec3 c = stPal(pal, index);  or stPalLerp(pal, index) for a continuous ramp index
#pragma once
#include <cmath>
#include <cstdint>

#include "demo.h"

class StarsExe;

namespace stars {

// =============================================================================================== random generator
// 0x4af10: s = rol32((s ^ 0x78d47e5d) * 0xd413d473, 18); returns s % n. One sequence from the seed [0x4af35]
// (0x7ea9) runs through the whole demo: 16,926 calls in the particle part 0x133ad, then 45,787 in 0x133f1
// (notes-core section 7, the capture without a second monitor).
class Random {
public:
    explicit Random(const StarsExe& exe);  // the seed from the image
    explicit Random(uint32_t seed) : s(seed) {}
    uint32_t step() { s = rotl((s ^ 0x78d47e5du) * 0xd413d473u, 18); return s; }
    uint32_t next(uint32_t n) { return n ? step() % n : step(); }  // EAX = n -> EAX = s % n
    void skip(uint64_t calls) { while (calls--) step(); }
    uint32_t s;

private:
    static uint32_t rotl(uint32_t v, int k) { return v << k | v >> (32 - k); }
};

// =============================================================================================== sine tables
// A static table of the image: entry i = amp * sin(2 pi i / period) (or cos), rounded or truncated as the notes
// say. operator() evaluates it continuously at a fractional index (use this to drop the table's quantisation);
// at() reads the original's entry from the exe (exact ports and checks). Indices wrap per period.
struct SineTable {
    uint32_t addr;   // image address
    int period;      // entries per turn
    int entries;     // entries in the table (0xa740 has 5120 = a period and a quarter, so cos = S[i + 1024])
    int bytes;       // 2 (int16) or 4 (int32)
    double amp;
    bool cosine;
    double operator()(double i) const {
        double a = 6.283185307179586 * i / period;
        return amp * (cosine ? std::cos(a) : std::sin(a));
    }
    int32_t at(const StarsExe& exe, int i) const;
};
// 0xa740: S[i] = round(65528 sin(2 pi i / 4096)), 5120 dwords (notes-parts2d-a; intro, warp, tunnel, particles, face)
inline constexpr SineTable kSinA{0xa740, 4096, 5120, 4, 65528.0, false};
// 0x14260: round(1023 sin(2 pi i / 2048)), 2048 int16 (dissolve)
inline constexpr SineTable kSinB{0x14260, 2048, 2048, 2, 1023.0, false};
// 0xffb8: round(1024 sin(2 pi i / 512)), 512 dwords (face)
inline constexpr SineTable kSinC{0xffb8, 512, 512, 4, 1024.0, false};
// 0x5599c: the engine's COS[i] = trunc(1023 cos(2 pi i / 4096)), 4096 int16; COS[(i - 0x400) & 0xfff] is the sine
inline constexpr SineTable kCos{0x5599c, 4096, 4096, 2, 1023.0, true};

// =============================================================================================== palette fades
// The fader (notes-core section 3): a level 0..256 stepped by `step` per tick of its clock, `count` steps, applied
// as DAC = min(63, (p * level) >> 8) -> colour * level / 256 clamped (fadeColour / fadeMul in pictures.h, demo.h).
//   main's fades 0x17e96 step once per retrace (rate kRetraceHz); the parts step the fader in their PIT callback
//   (rate kPitHz, from the callback install) and show it once per drawn frame.
struct Fade {
    double t0 = 0;              // music time at which the level starts to move (step k is taken at t0 + k / rate)
    double rate = kRetraceHz;   // steps per second
    double from = 0, step = 4;  // level before the first step, change per step
    int count = 64;             // steps
    // Continuous: from + step * clamp((t - t0) * rate, 0, count).
    double level(double t) const;
    // In whole steps, as the original shows it: from + step * clamp(floor((t - t0) * rate), 0, count).
    double levelStepped(double t) const;
    double end() const { return t0 + count / rate; }
};
// main's fade_17e96(step, n) / fade_out_17eba called at music time tc (usually ctx.afterPos(x)): n + 1 palette
// writes, one per retrace, the first one the old level. In the capture step k is on screen from tc + (k + d) / 70.086
// s, d = 0.05 .. 0.73 depending on where in the frame tc falls (picture 57: d = 0.05 for the fade in, 0.73 for the fade
// out), so on average d = 0.5 and the continuous level through the middle of the steps starts at tc + 1 / 70.086.
inline Fade mainFade(double tc, double from, double step, int n) { return {tc + 1.0 / kRetraceHz, kRetraceHz, from, step, n}; }

// =============================================================================================== palettes on the GPU
// A palette as a 256x1 RGBA16F texture (nearest), for shaders that compute the original's palette indices.
struct PaletteTexture {
    Texture tex;
    void set(const Palette& p);  // creates or updates (needs the GL context)
    void bind(int unit) const { tex.bind(unit); }
};

// GLSL helpers; prepend to a fragment shader: shader.build(vs, std::string(stars::kGLSL) + fs, "name").
//   vec3 stPal(sampler2D pal, float i)       colour of index i (rounded, wraps at 256), as the DAC shows it
//   vec3 stPalLerp(sampler2D pal, float i)   linear between the entries floor(i) and floor(i) + 1 (for a ramp
//                                            index that the original truncated; i is clamped to 0..255)
//   float stSinA(float i)                    S[i] of 0xa740, continuous (65528 sin(2 pi i / 4096))
extern const char* kGLSL;

}  // namespace stars
