// p2's motion blur and screen composition (p2_blur.cpp), driven by the scene (p2.cpp): the frame copy 360c (blend
// through the table 3527:0000, the label into the blend buffer, the credit band) as a feedback buffer in true colour,
// and the display: the credit text in the band's grey ramp, the never-blended screen row 199 and the DAC's white
// fade (2580 with 2550).
#pragma once
#include <array>

#include "demo.h"
#include "dope/core.h"

// Blend n of 360c: the original's frame n (frame clock n: the state after n per-retrace calls). The original blends
// once per drawn frame, at most once per retrace; the remake blends once per retrace, as a fast PC draws it.
struct P2Blend {
    double r = 0;                  // the scene's state (retraces since the part start, whole)
    float weight = 1;              // the new frame's weight s / 256 (s = [1965])
    double label = -1;             // the label position [1955] the copy writes after this blend (<= 0: none)
    float zeroY0 = 0, zeroY1 = 0;  // screen rows [y0, y1) whose pixels are in the grey ramp: no scene (T = grey or 0)
};

// What the display shows on top of the blur at frame clock f.
struct P2Show {
    double label = -1;             // the label position at f (continuous; <= 0: none)
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
    // Blend n (n >= 0; before blend 0 the buffer is black).
    virtual P2Blend blend(int n) const = 0;
};

class P2Blur {
public:
    // After a jump the buffer is rebuilt from black this far back: the weight of what came before < kForget.
    static constexpr double kForget = 1.0 / 1000.0;

    void init(Ctx& c, const Palette& pal);
    // Draws the screen at frame clock f into c.out: the blend buffers after blends floor(f) and floor(f) + 1
    // (stepped forwards from the last state, or rebuilt) mixed by the fraction of f, the label, then `show`.
    void render(Ctx& c, P2Source& src, double f, const P2Show& show);
    void release();

private:
    void ensure(int w, int h);
    // Makes acc_[cur_] hold the buffer after blend n and acc_[prev_] the one after blend n - 1.
    void advance(Ctx& c, P2Source& src, int n);
    // One blend of 360c into the bound target: the buffer after the previous blend (or black), the frame in new_.
    void blendPass(Ctx& c, const Target* prev, double prevLabel, const P2Blend& b);
    // The screen: the buffers after two consecutive blends (nullptr: black) mixed by k, the label, `show`.
    void displayPass(Ctx& c, const Target* a, const Target* b, float k, const P2Show& show);
    void bindCommon(Ctx& c);

    Shader blend_, display_;
    dope::PaletteTexture pal_;
    Texture level_;          // component sum -> ramp level
    // The blend buffers (ramp level, residual; the label's area black, as the next blend's table makes it) and the
    // render buffer (colour). acc_[cur_] holds the buffer after blend have_, acc_[prev_] after blend have_ - 1.
    Target acc_[3], new_;
    int cur_ = 0, prev_ = 1;
    int have_ = 0;
    bool valid_ = false, prevBlack_ = true, curBlack_ = true;
    double haveLabel_ = -1;  // the label position written by blend have_
    int w_ = 0, h_ = 0;
};
