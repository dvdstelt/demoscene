// Page peel 0x2b55 / 0x2b20 / 0x2b6c / 0x2ca2 / 0x2ad0 (paper/re/notes-scene3d.md §9) as one GPU pass.
// Used twice: the live 3D scene peels off to reveal the logo page (old = scene, new = logo page, first tick 5483) and
// the logo page peels off the teal canyon (old = logo page, new = canyon, first tick 6018).
//
// Progress p (0x2b20, per tick): p = 0 at the reset (0x2b55), then +4 per tick up to 256, +7 per tick above, capped
// at 700 (128 ticks). Checked against the emulator ([0x1eaa9] after each IRQ): scene peel p = 4 after tick 5483,
// 256 after 5546, 700 after 5610; teal peel 4 after 6018, 700 after 6145.
//
// Geometry, continuous: the page's horizontal line at source row y_src = 200 - 100 s (s = 0 bottom edge .. 2 top edge)
// lies at screen row a(s) and is scaled about x = 160 by 1/sc(s), sc = 1 + b(s)/2048, where a and b are the
// parabolas of 0x2ad0 through (P0, P1, 0) at s = 0, 1, 2:
//   a: P0 = 200 cos(p), P1 = 100 cos(p/2);   b: P0 = -1600 sin(p), P1 = -800 sin(p/2)   (angles in 1/1024 turn)
// The original draws 25 strips in order of increasing s, later ones over earlier ones; a strip where a increases with s
// is upside down and shows the back of the page (the raw paper grain as grey ramp indices: dark grey); a front
// strip is only drawn above the highest screen row reached so far (min a over [0, s]) - it is hidden under the fold.
// Rows below max a show the new picture. Here every output row solves a(s) = y (at most two roots), applies the same
// visibility rule to each root, and takes the visible one with the larger s; the old picture is sampled bicubic at
// (160 + (x - 160) sc, y_src). Rows are supersampled where the choice changes inside a pixel (anti-aliased fold).
//
// Quirk decisions (fidelity rules):
//   - stale top rows: dropped. The original's 0x2ad0 step (20/256 instead of 20.48/256) stops the curves at s = 1.953,
//     so rows above a(1.953) (about 4..6 rows while p < 512) keep the last frame. Here s runs to exactly 2 (source row 0
//     at a(2) = 0), which also makes p = 0 the identity instead of a 2.4 % squeeze.
//   - squeezed clipped strips: dropped (each row maps to its own source row; the original squeezes a strip's 8
//     source rows into its visible part when the fold clips it).
//   - nearest sampling, 25 strips, linear zoom inside a strip: replaced by the continuous evaluation and filtering.
//   - sc > 1 (back strips for p > 512) reads past row ends: the back wraps around (it is grain, as in the original).
//   - the back of the page = raw grain as colour indices (grey 27..30 of 63, darker than the paper): kept, it is the
//     look of the turned page.
#pragma once
#include "render.h"

struct Ctx;

namespace paper {

constexpr int kPeelSceneFirstTick = 5483;  // 3D scene -> logo page: p = 4 after the IRQ of this tick
constexpr int kPeelTealFirstTick = 6018;   // logo page -> teal canyon

int peelProgressAt(int tick, int firstTick);        // exact [0x1eaa9] after the IRQ of `tick` (0 before)
double peelProgress(double tick, int firstTick);    // linearly interpolated between ticks

struct PeelCurves {
    double a0, a1;  // a(s) through (a0, a1, 0): screen rows
    double b0, b1;  // b(s) through (b0, b1, 0): zoom
    double a(double s) const;
    double b(double s) const;
    double da(double s) const;  // derivative
};
PeelCurves peelCurves(double p);

// What a screen row (VGA y, continuous) shows: kind 0 = new picture, 1 = old picture (front), 2 = page back; s,
// source row 200 - 100 s and the horizontal scale sc (source pixels per screen pixel).
struct PeelHit {
    int kind = 0;
    double s = 0, srcRow = 0, sc = 1;
};
PeelHit peelRow(const PeelCurves& c, double y);

class Peel {
public:
    explicit Peel(Ctx& c);  // use ctx.shared<Peel>()
    // Draws the whole frame into the bound target. oldTex / newTex: full-frame pictures as render targets (stored
    // bottom-up like every Target; e.g. Target::tex), p = progress (peelProgress).
    void draw(Ctx& c, GLuint oldTex, GLuint newTex, double p);

private:
    Shader shader_;
};

}  // namespace paper
