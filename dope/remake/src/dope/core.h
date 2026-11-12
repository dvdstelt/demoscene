// The DOPE core library: building blocks several parts share (dope/re/notes-core.md, notes-engine.md and the part
// notes). Ports, as continuous maths where the original used tables or integer steps:
//   the sine tables the parts build from a 512-word quarter wave (p2 19ce, p3 19f0, p4 1ea8, p5 1584, p6 179b,
//     p7 19f4), as sin(2 pi i / 2048) and 16-bit angles as radians; exact entries for checks;
//   the per-retrace integrals of the engine parts' per-retrace routines (p2 2580, p3 1f60, p5 1a70, p6 1cc0,
//     p7 1f40: value += velocity once per retrace, the scripts set velocities and values between "run N retraces"
//     calls), interpolated between retraces;
//   the 70 x 44 label boxes ("routine boxes") and their slide (engine parts' per-retrace routines and frame copies
//     360c / 2b1c / 2adc / 2d5c / 2f8c; p4 1a20 / 1c6b);
//   the weight-table routine 0b00 (p1; byte-identical in p4 1975, p5 2f99, p6 3871) and the palette scale 2550
//     (p2; p5 2f99 / p6 3871 take dx >> 4) that mix and fade palettes, as float mixes;
//   the DAC fades (v * L) >> 8 stepped per retrace (p6 3d22, p7 33be, ...): Fade;
//   palettes as GPU lookup textures for effects that compute palette indices.
//
// How a part uses it:
//
//   #include "dope/core.h"
//   using namespace dope;
//
//   // the camera z of p3: 8192000, velocity -2600 per retrace, changed by the script at retraces 2100, 2240, ...
//   Integrator z{8192000, -2600};
//   z.velocity(2100, -1100).velocity(2240, -600);
//   double cz = z.at(c.retraces(t));                // continuous, exact at whole retraces
//
//   // the label: position -1, speed +1 from retrace 1120, -1 from 3360 (p3); the frame copy draws the position
//   // after the call that releases the frame: one retrace ahead of the 3D state
//   LabelSlide label{-1, {{1120, 1}, {3360, -1}}};
//   c.drawPicture(c.picture("p3-label-phong"), labelDraw(label.position(c.retraces(t) + 1)));
//
//   // p1's palette cross-fade weight (0b00 with dx = pos - 500), continuous: w = clamp((pos - 500) / 4096, 0, 1)
//   double w = weight(pos - 500);  Palette p = mixPalettes(A, B, w);
//
//   // p6's exit fade: L = 256 - 7 j for j = 0..35, one step per retrace from retrace 3991; black from j = 36
//   Fade out{c.retraceTime(3991), kRetraceHz, 256, -7, 36};
//
//   // an effect computing 8-bit indices on the GPU: look them up in a palette texture
//   PaletteTexture pt; pt.set(pal);               // in your shader: prepend dope::kGLSL, then
//   // uniform sampler2D pal; vec3 c = dpPal(pal, index);  or dpPalLerp(pal, index) for a continuous ramp index
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "demo.h"

class DopeImage;

namespace dope {

constexpr double kTwoPi = 6.283185307179586;

// =============================================================================================== sine and angles
// Every part with a sine table builds it from a quarter wave of 512 words at cs:Q (round(32767 sin(2 pi i / 2048)),
// entry 510 = 32767 instead of 32766): entries 512..1023 mirror it ([1023 - i] = [i], so the second and fourth
// quarters run one entry ahead), 1024..2047 negate 0..1023, 2048..2559 repeat 0..511 (the cosine overrun).
// Q by part (index 1..7; p1 has no table): p2 0410, p3 0410, p4 0000, p5 0012, p6 0220, p7 0410.
constexpr uint16_t kSineQuarter[8] = {0, 0, 0x0410, 0x0410, 0x0000, 0x0012, 0x0220, 0x0410};
int16_t sineEntry(const DopeImage& img, uint16_t quarter, int i);  // entry i (0..2559) of the built table
inline double sine2048(double i) { return 32767.0 * std::sin(kTwoPi * i / 2048.0); }  // continuous
inline double angleRad(double a) { return a * (kTwoPi / 65536.0); }  // 16-bit angle (65536 = one turn) -> radians

// =============================================================================================== per-retrace motion
// value += velocity once per per-retrace call; before the call that starts a script step (after k calls) the
// script may set the velocity and the value. at(r) = the value after r calls, linear between whole calls (exact at
// whole r for integer inputs). Angles: integrate unwrapped and take angleRad(at(r)) (the 16-bit wrap is a turn).
struct Integrator {
    struct Set { int k; bool hasValue, hasVelocity; double value, velocity; };
    double v0 = 0, vel0 = 0;   // the value and the velocity before the first call
    std::vector<Set> sets;     // in order of k
    Integrator() = default;
    Integrator(double value, double velocity) : v0(value), vel0(velocity) {}
    Integrator& velocity(int k, double v) { sets.push_back({k, false, true, 0, v}); return *this; }
    Integrator& value(int k, double x) { sets.push_back({k, true, false, x, 0}); return *this; }
    double at(double r) const;
};

// =============================================================================================== the label boxes
// The 70 x 44 opaque label of the parts' "routine boxes" ("phong BLUR motion", "7800 PHONG polys", ...). Per retrace
// call: a += speed; if (a < 0 || a >= 75) speed = 0 (p2 [1955]/[1957], p3 [19c4]/[19c6], p5 [1558]/[155a],
// p6 [1768]/[176a], p7 [19c8]/[19ca], p4 [196d]/[196f]). The script sets the speed (+1 slides in over 70 + 5
// retraces, -1 out over 76) before the call that starts a step: (k, speed) acts from call k + 1.
struct LabelSlide {
    int a0 = -1;                                // the position before the first call (p5: 0)
    std::vector<std::pair<int, int>> speeds;    // (k, speed), in order of k
    int at(int n) const;                        // after n calls
    double position(double r) const;            // continuous, between at(floor r) and at(floor r + 1)
};
// Where the label is drawn for position a (opaque, all 70 x 44 pixels; a < 0 draws nothing):
//   engine parts (frame copies p2 360c, p3 2b1c, p5 2adc, p6 2d5c, p7 2f8c): screen rows 8..51, left edge at
//   x = a - 70, the part left of the screen cut off (a <= 70: columns 70 - a..69 at x 0..a-1; then 5 pixels more);
inline PictureDraw labelDraw(double a) {
    PictureDraw d;
    d.x = float(a - 70.0); d.y = 8; d.w = 70; d.h = 44; d.clipX0 = 0;
    return d;
}
//   p4 (1c6b): screen rows 148..191, left edge at x = 319 - d, cut off at x = 319 (d <= 70: columns 0..d-1).
inline PictureDraw labelDrawP4(double d) {
    PictureDraw r;
    r.x = float(319.0 - d); r.y = 148; r.w = 70; r.h = 44; r.clipX1 = 319;
    return r;
}

// =============================================================================================== palettes
// 0b00 (p1; p4 1975, p5 2f99, p6 3871): if (dx < 0) dx = 0; w = min(dx >> 4, 256); t[i] = (i * w) >> 8, i = 0..63.
// Mixing two palettes with it (p1 0b24, p4 1999, p5 3038, p6 3895): DAC = min(63, tA[A[v]] + tB[B[v]]).
std::array<uint8_t, 64> weightTable(int dx);
// The same weight, continuous (w / 256 without the steps): clamp(dx / 4096, 0, 1).
inline double weight(double dx) { return dx <= 0 ? 0.0 : dx >= 4096 ? 1.0 : dx / 4096.0; }
// (1 - w) a + w b per colour: the float form of the weight-table mixes (both weights floored separately in the
// original, up to 2/63 darker; dropped).
Vec3 mixColour(Vec3 a, Vec3 b, double w);
Palette mixPalettes(const Palette& a, const Palette& b, double w);
// 2550 (p2): ax = start, dx = step clamped to 0..0x100: t[i] = ah; if (ax <= 0x3f00) ax += dx (i = 0..63).
// p2's fade to white uses start = level, step 0x100: t[v] = min(63, v + level / 256), i.e. addLevel below.
std::array<uint8_t, 64> scaleTable(int start, int step);
inline Vec3 addLevel(Vec3 c, double k) {  // every component + k / 63, at most 1
    float a = float(k / 63.0);
    return {std::fmin(1.0f, c.x + a), std::fmin(1.0f, c.y + a), std::fmin(1.0f, c.z + a)};
}

// =============================================================================================== fades
// A level stepped once per tick of its clock (here: per retrace): from + step * clamp((t - t0) * rate, 0, count),
// step k on screen from t0 + k / rate. The DAC scaling (v * L) >> 8 is fadeColour / fadeMul (pictures.h, demo.h).
struct Fade {
    double t0 = 0;               // music time of step 0 (the first write)
    double rate = kRetraceHz;    // steps per second
    double from = 256, step = -1;
    int count = 256;             // steps after the first
    double level(double t) const;         // continuous
    double levelStepped(double t) const;  // in whole steps, as the original shows it
    double end() const { return t0 + count / rate; }
};

// =============================================================================================== palettes on the GPU
// A palette as a 256x1 RGBA16F texture (nearest), for shaders that compute the original's palette indices.
struct PaletteTexture {
    Texture tex;
    void set(const Palette& p);  // creates or updates (needs the GL context)
    void bind(int unit) const { tex.bind(unit); }
};

// GLSL helpers; prepend to a fragment shader: shader.build(vs, std::string(dope::kGLSL) + fs, "name").
//   vec3 dpPal(sampler2D pal, float i)       colour of index i (rounded, wraps at 256), as the DAC shows it
//   vec3 dpPalLerp(sampler2D pal, float i)   linear between the entries floor(i) and floor(i) + 1 (a ramp index
//                                            computed continuously; i is clamped to 0..255)
extern const char* kGLSL;

}  // namespace dope
