// The palette fades of the whole screen (core 0x400364..0x400612, inside/re/notes-core.md "Palette"), as a colour
// transform. The original keeps a base palette (the part's colours), the palette the DAC shows ("current") and a float
// palette with per-entry adders; a fade sets the start ("current") and steps the float palette towards base x level,
// once per refresh. Every start the script uses is an affine function of the base colour c, the same for every entry
// and channel: white (0x400498: min(c + 63, 63)), the inverse (0x400454: 63 - c), black or c x level. So what the
// screen shows is a * c + b, with (a, b) stepped exactly like the original's float palette, and the remake applies
// that to the part's true-colour frame (Demo::render). The remake interpolates between refreshes (the original steps
// once per refresh and truncates to 6 bits).
#pragma once
#include <vector>

#include "demo.h"

namespace inside {

struct FadeState {
    Vec3 a = Vec3(1, 1, 1);   // out = colour * a + b
    Vec3 b = Vec3(0, 0, 0);
};

// One palette operation of the script or a part, at the refresh whose first step it makes.
struct FadeOp {
    enum Kind { Flash, Inverse, Black, SetBase, Fade } kind;
    double refresh;       // the first refresh that shows its first step
    float level = 1.0f;   // Fade: the target level (base x level)
    int steps = 1;        // Fade: refreshes
};

class Fade {
public:
    explicit Fade(Ctx& c);
    FadeState at(double r) const;            // continuous: between the states of floor(r) and floor(r) + 1
    const std::vector<FadeOp>& ops() const { return ops_; }

private:
    struct Step { double a, b; };            // the state after a refresh's step (affine in the base colour)
    std::vector<FadeOp> ops_;
    int first_ = 0;                          // refresh of states_[0]
    std::vector<Step> states_;
};

// Fragment shader of the fade pass: out = texture * mulc + addc.
extern const char* kFadeFS;

}  // namespace inside
