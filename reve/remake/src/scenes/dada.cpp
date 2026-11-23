// Script entries 18 and 19 (reve/re/notes-parts2d-b.md section 4): two textured planes interlaced line by line, both
// tumbling (the camera tilts over and over) and turning. Even lines: the 64 x 64 tile at the top left of
// GFX\TEXTURE.RAW (copied to [0x205e94] by the init 0x20508d), odd lines: GFX\DADA.RAW (a winged pony), both through
// DADA's palette (the init loads DADA.RAW with its palette; TEXTURE.RAW's is the same).
//
// Ports the init 0x20508d, the retrace routines 0x2050ce (entry 18) and 0x2050ed (entry 19), which step the angle
// words once per refresh, and the frame routine 0x20510c (0x259a08 for the tile, 0x259c98 for DADA; planes.h).
#include <algorithm>

#include "planes.h"

namespace {
// The angle words [0x217574] a1, [0x217578] b1 (tile), [0x21757c] a2, [0x217580] b2 (DADA): 0 in the image (no init
// sets them before entry 18), then each retrace routine adds its immediates: `add dword ptr [x], imm8` at these
// addresses, the imm8 at +6.
constexpr uint32_t kAngles = 0x217574;
constexpr uint32_t kStep18[4] = {0x2050cf, 0x2050d6, 0x2050dd, 0x2050e4};   // +10, +5, +10, +5
constexpr uint32_t kStep19[4] = {0x2050ee, 0x2050f5, 0x2050fc, 0x205103};   // +10, +5, +5, +2

// The cues start at the entries' first retrace calls s (timeline.cpp: 12623 for entry 18, after its init loaded
// DADA.RAW, 13241 for entry 19): record s + n shows the angles after n steps.

struct DadaScene : Scene {
    planes::Renderer planes;
    Texture tile, dada;
    float a0[4] = {}, step18[4] = {}, step19[4] = {};

    void init(Ctx& c) override {
        // 0x20508d: DADA.RAW (name at 0x205d8b) with its palette; the tile from [0x21b140] = GFX\TEXTURE.RAW (name at
        // 0x205ceb, loaded by entries 0 and 12), 64 rows of 64 bytes from (0, 0)
        planes::loadTexture(c, tile, 0x205ceb, 0x205d8b, 0, 0, 64);
        planes::loadTexture(c, dada, 0x205d8b, 0x205d8b, 0, 0, 256);
        planes.init(c, rawImage(*c.exe, c.exe->cstr(0x205d8b)).color(0));
        for (int i = 0; i < 4; i++) {
            a0[i] = float(c.exe->s32(kAngles + 4 * i));
            step18[i] = float(c.exe->s8(kStep18[i] + 6));
            step19[i] = float(c.exe->s8(kStep19[i] + 6));
        }
    }

    void render(Ctx& c, double r) override {
        const int e = c.cue->entry;
        const auto& script = c.script();
        double s18 = e == 18 ? c.start : script[e - 1].start;
        double s19 = e == 18 ? c.end : c.start;
        double n18 = std::min(r - s18, s19 - s18), n19 = std::max(r - s19, 0.0);
        if (n18 < 0) return;   // before the first step: the init (black)
        double w[4];
        for (int i = 0; i < 4; i++) w[i] = a0[i] + step18[i] * n18 + step19[i] * n19;
        // a = 0 puts the eye in the plane: every row black (du = dv = 0 in 0x25a048), as the first record shows
        planes.draw(c, tile, 64, planes.geo.camera(w[0], w[1]), dada, 256, planes.geo.camera(w[2], w[3]));
    }
};
}  // namespace

REGISTER_SCENE(DadaScene, "dada");
