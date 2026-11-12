// p2's motion blur and screen composition (p2_blur.cpp), driven by the scene (p2.cpp): the frame copy 360c (blend
// through the table 3527:0000, the label into the blend buffer, the credit band) as a feedback buffer in true colour,
// and the display: the credit text in the band's grey ramp, the never-blended screen row 199 and the DAC's white
// fade (2580 with 2550).
#pragma once
#include <array>

#include "demo.h"
#include "dope/core.h"

// One blend of 360c at retrace time r (continuous). The original blends once per drawn frame; the remake blends
// P2Blur::kSub times per retrace, and the blends of the original's frame n are spread over r in (n - 1, n], so that
// at whole r = n the buffer holds what record start + n shows.
struct P2Blend {
    double r = 0;                  // the scene's state (retraces since the part start)
    float weight = 1;              // the new frame's weight (1 - (1 - s / 256)^(1 / kSub), s = [1965])
    double label = -1;             // the label position [1955] the copy writes after this blend (<= 0: none)
    float zeroY0 = 0, zeroY1 = 0;  // screen rows [y0, y1) whose pixels are in the grey ramp: no scene (T = grey or 0)
};

// What the display shows on top of the blur at time r.
struct P2Show {
    float greyY0 = 0, greyY1 = 0;  // screen rows [y0, y1) in the grey ramp: the credit text level is shown there
    int slots = 0;                 // credit strips with a non-zero level: strip, top screen row, coefficient
    std::array<float, 4> strip{}, row{}, coef{};
    float fadeAdd = 0;             // the DAC's white fade: every component + k / 63
};

// The scene that P2Blur blends: the 3D frame at a time, and the blends' parameters.
class P2Source {
public:
    virtual ~P2Source() = default;
    // The engine's render buffer 4527 at retrace time r (the backdrop or black, the discs, the "C"), into dst.
    virtual void drawScene(Ctx& c, Target& dst, double r) = 0;
    // Blend m (r = m / kSub); the first one is firstBlend() (r in (-1, 0]: the part's frame 0).
    virtual P2Blend blend(int m) const = 0;
    // The partial blend of the display at r (between blends floor(r kSub) and the next): weight, label, rows.
    virtual P2Blend partial(double r) const = 0;
};

class P2Blur {
public:
    static constexpr int kSub = 4;   // blends per retrace (the original: one per drawn frame, at most one per retrace)
    static int firstBlend() { return -(kSub - 1); }
    // After a jump the buffer is rebuilt from black this far back: the weight of what came before < kForget.
    static constexpr double kForget = 1.0 / 1000.0;

    void init(Ctx& c, const Palette& pal);
    // Draws the screen at retrace time r into c.out: the blend buffer stepped to blend floor(r kSub) (forwards from
    // the last state, or rebuilt), the partial blend to r, the label, then `show`.
    void render(Ctx& c, P2Source& src, double r, const P2Show& show);
    void release();

private:
    void ensure(int w, int h);
    void advance(Ctx& c, P2Source& src, int m);
    // One pass of the blend shader: acc (or black) blended with the frame in new_, the label, into the bound target.
    void pass(Ctx& c, const Target* prev, double prevLabel, const P2Blend& b, const P2Show* show);

    Shader shader_;
    dope::PaletteTexture pal_;
    Texture level_;          // component sum -> ramp level
    Target acc_[2], new_;    // the blend buffers (ramp level, residual) and the render buffer (colour)
    int cur_ = 0;            // acc_[cur_] holds the buffer after blend have_
    int have_ = 0;
    bool valid_ = false;
    double haveLabel_ = -1;  // the label position written by blend have_
    int w_ = 0, h_ = 0;
};
