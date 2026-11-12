// p4: FIRE clouds over p3's nebula, the FRACTAL scape, and the change to p5's chequerboard (dope/re/notes-p4.md).
// Ports the main routine 1caa (its seven steps: set-up, then "draw frames until the retrace counter [1969] reaches
// N" with the script's writes between them), the per-retrace routine 1a20 (the landscape position [2113], the
// label slide [196d], the fade A [1971] with its bounds, and on odd retraces the fire position [157a], the fire
// phase [1578] and the palette 1975 / 1999), the composite 1b91 / 1c6b and the labels at the bottom right. The fire
// feedback 1aab is in p4_fire.cpp, the landscape 1ee8 / 1efc in p4_land.cpp.
//
// The screen index 16 L + c and the palette mix 1999 (DAC = (1 - A / 4096) pal16[c] + (A / 4096) fire[L], both
// weights floored to 1/256) are kept as separate colours: the background (the nebula, the landscape, the
// chequerboard; picture redraws or the original's pixels) drawn with the weight 1 - a, then the fire's colour
// added with the weight a, a = A / 4096 continuous between retraces (the original writes the DAC on odd retraces
// only). Dropped with that: the one-step darkening of every colour while a < 16 / 4096 (the start of p4, and its
// last frames: the chequerboard is shown at full brightness, as in its picture). The label is opaque and not faded.
//
// Time (dope/re/PORT.md "Time"): p4's frame loop draws continuously and its palette changes in the handler, so the
// record of IRQ k shows the state after IRQ k and the main loop's answer to it (a step's writes come before the next
// IRQ, notes-p4 3): k = c.retraces(t) + 1, continuous; the state is interpolated between IRQ k and k + 1. The fire
// steps once per odd IRQ (step m at IRQ 2m - 1) and is blended between steps. The script's state is replayed from
// the part start at start-up (7430 IRQs of integer arithmetic), so every time is a lookup.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "demo.h"
#include "dope/core.h"
#include "exe.h"
#include "scenes/p4.h"

namespace {

enum class Bg : uint8_t { Nebula, Landscape, Chequerboard };

// The state after IRQ k (and the main loop's answer to it).
struct State {
    int A = 0;          // [1971] the fire weight, 0..4096
    int phase = 0;      // [1578] the fire's sine phase
    int d = -1;         // [196d] the label slide
    Bg bg = Bg::Nebula; // what the background buffer 4f1e holds
    int label = 1;      // [196b]: label 1 (0273) or 2 (036e)
};

class FireScene : public Scene {
public:
    void init(Ctx& c) override {
        script(c.exe->part(4));
        for (const char* n : {"p3-nebula", "p4-chequerboard", "p4-label-fire-clouds", "p4-label-fractal-scape"})
            c.picture(n);
        fire_.init(c);
        land_.init(c);
    }

    void render(Ctx& c, double t) override {
        int last = int(st_.size()) - 1;
        double k = std::clamp(c.retraces(t) + 1.0, 1.0, double(last));
        int k0 = int(std::floor(k)), k1 = std::min(k0 + 1, last);
        double f = k - k0;
        const State& s0 = st_[size_t(k0)];
        const State& s1 = st_[size_t(k1)];
        double a = (s0.A + f * (s1.A - s0.A)) / 4096.0;   // 1999: the fire's weight A / 1000h

        // the background, weighted 1 - a
        float bgw = float(1.0 - a);
        if (bgw > 0) {
            PictureDraw pd;
            pd.mul = Vec3(bgw, bgw, bgw);
            switch (s0.bg) {
                case Bg::Nebula: c.drawPicture(c.picture("p3-nebula"), pd); break;
                case Bg::Chequerboard: c.drawPicture(c.picture("p4-chequerboard"), pd); break;
                case Bg::Landscape: land_.draw(c, k * 2.0 / 8.0, bgw); break;   // P = [2113] = 2k, v = P / 8
            }
        }
        // the fire, weighted a: step m after the odd IRQ 2m - 1
        if (a > 0) {
            double m = std::min((k + 1.0) / 2.0, double(steps_.size()));
            int n = int(std::floor(m));
            fire_.advance(c, n, steps_);
            fire_.draw(n, m - n, a);
        }
        // the label (1c6b): opaque, at x = 319 - d, rows 148..191, cut off at x = 319
        double d = s0.d + f * (s1.d - s0.d);
        if (d > 0)
            c.drawPicture(c.picture(s0.label == 1 ? "p4-label-fire-clouds" : "p4-label-fractal-scape"),
                          dope::labelDrawP4(d));
    }

private:
    // 1caa's script with 1a20 per IRQ, in the original's integer arithmetic: st_[k] = the state after IRQ k.
    void script(const DopeImage& p4) {
        State s;
        s.phase = p4.cs16(0x1578);              // the image's initial values
        s.d = p4.css16(0x196d);
        int ldir = p4.css16(0x196f);
        int fstep = p4.css16(0x1576);           // 23h
        int Astep = 1;                          // 1d05 mov [1971], 0; 1d0c mov [1973], 1
        s.A = 0;
        int Alow = p4.css16(0x1a59);            // the immediates of 1a54 cmp [1971], lo and 1a5d cmp [1971], hi
        int Ahigh = p4.cs16(0x1a62);
        int cnt = 0, step = 1;
        st_.assign(1, s);
        for (int k = 1;; k++) {
            // 1a20
            cnt++;
            s.d += ldir;
            if (s.d < 0 || s.d >= 0x4b) ldir = 0;                         // 1a34..1a44
            s.A += Astep;
            if (s.A < Alow || (s.A & 0xffff) >= Ahigh) Astep = 0;         // 1a54..1a66
            if (k & 1) s.phase = (s.phase + fstep) & 0xffff;              // 1a6d..1a85 (and [157a] -= 10000h)
            // 1caa: the main loop compares [1969] after every frame, so a step starts right after its IRQ
            bool done = false;
            switch (step) {
                case 1:
                    if (cnt >= 0x578) { step = 2; ldir = 1; fstep = 0x1f; }                         // 1d26..1d36
                    break;
                case 2:
                    if (cnt >= 0xb46) { step = 3; ldir = -1; fstep = 0x1d; cnt = 0; }               // 1d4a..1d61
                    break;
                case 3:                                                                          // 1d75..1da7
                    if (cnt >= 0x500) { step = 4; Astep = -2; Alow = 0x600; cnt = 0; fstep = 0x1b; s.bg = Bg::Landscape; }
                    break;
                case 4:                                                                          // 1dbc..1dd3
                    if (cnt >= 0x41a) { step = 5; fstep = 0x19; s.label = 2; ldir = 1; }
                    break;
                case 5:                                                                          // 1ded..1e19
                    if (cnt >= 0x6d6) { step = 6; fstep = 0x17; ldir = -1; Astep = 5; Alow = 0; Ahigh = 0x1000; cnt = 0; }
                    break;
                case 6:                                                                          // 1e31..1e6b
                    if (cnt >= 0x1ea) { step = 7; Astep = -4; cnt = 0; s.A = 0x1000; s.bg = Bg::Chequerboard; }
                    break;
                default:
                    done = cnt >= 0x400;                                                         // 1e78: the exit
                    break;
            }
            st_.push_back(s);
            if (done) break;
        }
        // the fire steps: step m at the odd IRQ 2m - 1 (1aab recomputes when the offset changes: once per odd IRQ)
        steps_.clear();
        for (int m = 1; 2 * m - 1 < int(st_.size()); m++) steps_.push_back({m, st_[size_t(2 * m - 1)].phase});
    }

    std::vector<State> st_;
    std::vector<P4Fire::Step> steps_;
    P4Fire fire_;
    P4Land land_;
};

}  // namespace

REGISTER_SCENE(FireScene, "p4");
