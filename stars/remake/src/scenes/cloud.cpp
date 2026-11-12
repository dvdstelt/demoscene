// cloud: two layers of a 4-bit cloud texture (file 30) mapped on a tunnel, layer 1 drifting outwards and turning one
// way, layer 2 inwards at twice the speed and turning the other way, combined through the 16x16 palette file 31
// (purple / orange / pale violet); fade in over 64 ticks, full speed to tick 292, slowing to a stop at tick 520
// (stars/re/notes-parts3d-a.md section 2). Ports 0x126ae (the frame loop), 0x1278e (the PIT callback: the speed sum),
// 0x126fa (the phases: fade in 0x120c4, deceleration, end) and, through cloudfx.h, the precalc 0x1241b, the table
// 0x1259c / 0x124eb and the draw 0x125f4.
//
// Evaluated exactly instead of the original's shortcuts: see cloudfx.h (continuous angle and depth per pixel,
// filtered layers, the 16x16 palette as a 2-D colour ramp); the fade is the palette level n / 64 continuous instead
// of floor(p * n / 64) per drawn frame, and the level the DAC keeps after the fade (63 at the capture's 70 fps) is
// 63 / 64. Dropped: the 2x2 centre of stale words and the undrawn rows 0, 198, 199 (colour 0): the whole frame is
// drawn.
#include <algorithm>

#include "demo.h"
#include "exe.h"
#include "scenes/cloudfx.h"

namespace {

class Cloud : public Scene {
public:
    void init(Ctx& c) override { fx_.init(*c.exe); }

    void render(Ctx& c, double t) override {
        double n = std::max(0.0, c.ticks(t));
        // 0x126fa: DAC = file 31 * n / 64 for n < 64, then the last level sent (63)
        float level = float(std::min(n, 63.0) / 64.0);
        fx_.draw(c, cloudfx::partOffsets(*c.exe, cloudfx::speedSum(n)), Vec3(level, level, level));
    }

private:
    cloudfx::CloudFx fx_;
};

}  // namespace

REGISTER_SCENE(Cloud, "cloud");
