// Script entry 23 (reve/re/notes-parts2d-b.md section 4): two textured planes interlaced line by line. Even lines:
// GFX\TOM.RAW (a face with glasses), odd lines: GFX\BOZ.RAW (a clown face), both through TOM's palette (the init
// loads TOM.RAW with its palette and BOZ.RAW without; their palettes are the same). One tilt for both; the camera tilts
// up from 0x300 (67.5 degrees) to 0x1f4 (43.9 degrees) and stays, the two pictures turn at different speeds.
//
// Ports the init 0x20516b, the retrace routine 0x2051dd (steps the angle words once per refresh) and the frame routine
// 0x20520c (0x259b50 for TOM, 0x259c98 for BOZ; planes.h).
#include <algorithm>

#include "planes.h"

namespace {
// 0x20516b: `mov dword ptr [x], imm32` (imm32 at +6): a = [0x217574] = 0x300, b1 = [0x217578] = 0, b2 = [0x217580] = 0.
constexpr uint32_t kInitA = 0x2051a5, kInitB1 = 0x2051af, kInitB2 = 0x2051c3;
// 0x2051dd: b1 += 8 (0x2051de), b2 += 12 (0x2051e5), a -= 3 (0x2051ec, `sub`), if (a <= 0x1f4) a = 0x1f4
// (cmp at 0x2051f3, mov at 0x2051ff; imm32 at +6). imm8 at +6.
constexpr uint32_t kStepB1 = 0x2051de, kStepB2 = 0x2051e5, kStepA = 0x2051ec, kMinA = 0x2051f3;

// The cue starts at the entry's first retrace call s (timeline.cpp: 15379, after the init cleared the screen and
// loaded two pictures); record s + n shows the angles after n steps.

struct TomBozScene : Scene {
    planes::Renderer planes;
    Texture tom, boz;
    float a0 = 0, b10 = 0, b20 = 0, da = 0, db1 = 0, db2 = 0, amin = 0;

    void init(Ctx& c) override {
        planes::loadTexture(c, tom, 0x205d98, 0x205d98, 0, 0, 256);   // GFX\TOM.RAW, its palette
        planes::loadTexture(c, boz, 0x205da4, 0x205d98, 0, 0, 256);   // GFX\BOZ.RAW, TOM's palette
        planes.init(c, rawImage(*c.exe, c.exe->cstr(0x205d98)).color(0));
        a0 = float(c.exe->s32(kInitA + 6));
        b10 = float(c.exe->s32(kInitB1 + 6));
        b20 = float(c.exe->s32(kInitB2 + 6));
        db1 = float(c.exe->s8(kStepB1 + 6));
        db2 = float(c.exe->s8(kStepB2 + 6));
        da = -float(c.exe->s8(kStepA + 6));
        amin = float(c.exe->s32(kMinA + 6));
    }

    void render(Ctx& c, double r) override {
        double n = r - c.start;
        if (n < 0) return;   // the init cleared the screen
        double a = std::max(a0 + da * n, double(amin));
        planes.draw(c, tom, 256, planes.geo.camera(a, b10 + db1 * n), boz, 256, planes.geo.camera(a, b20 + db2 * n));
    }
};
}  // namespace

REGISTER_SCENE(TomBozScene, "tomboz");
