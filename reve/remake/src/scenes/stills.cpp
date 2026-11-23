// The still pictures (reve/re/PARTS.md; notes-parts2d-b.md): script entries whose init clears the screen and draws
// one RAW picture with its own palette (0x25a1fb with EAX = 1), and whose retrace and frame routines are empty
// (0x20483f), so the picture stays until the next entry; and the closing title 0x20538d, which main runs after the
// last entry. A picture is either centred by the sprite routine 0x25f0be (index 0 not drawn; the screen is black
// there anyway) or copied over the whole screen.
//
//   entry 16  init 0x205261  GFX\JUNIOR.RAW   214 x 137, centred
//   entry 17  init 0x2052a0  GFX\MRTOMMY.RAW  200 x 200, centred
//   entry 24  init 0x2052df  GFX\DOG.RAW      187 x 124, centred
//   entry 25  init 0x20531e  GFX\MRWAAF.RAW   320 x 200, the whole screen
//   entry 29  init 0x205368  GFX\YOCOL.RAW    320 x 200, the whole screen
//   end       0x20538d       GFX\REVE.RAW     227 x 80, centred (the title of entry 0, here with its own palette,
//                                             which gives its four colours the same values as TEXTURE.RAW's)
//
// The sizes are read from the routines' `mov bl, w` / `mov bh, h` immediates.
#include "demo.h"
#include "exe.h"

namespace {
struct Still {
    const char* scene;
    const char* picture;   // pictures.cpp
    uint32_t wImm, hImm;   // addresses of the width and height immediates (0: the whole screen)
};
// clang-format off
constexpr Still kStills[] = {
    {"junior",  "junior",  0x205291, 0x205293},
    {"mrtommy", "mrtommy", 0x2052d0, 0x2052d2},
    {"dog",     "dog",     0x20530f, 0x205311},
    {"waaf",    "waaf",    0, 0},
    {"yocol",   "yocol",   0, 0},
    {"end",     "title",   0x2053b8, 0x2053ba},
};
// clang-format on

struct StillScene : Scene {
    const Still* s;
    int w = 320, h = 200;
    explicit StillScene(const Still* s_) : s(s_) {}
    void init(Ctx& c) override {
        if (s->wImm) {
            w = c.exe->u8(s->wImm);
            h = c.exe->u8(s->hImm);
        }
    }
    void render(Ctx& c, double) override {
        const Picture& p = c.picture(s->picture);
        // 0x25f0be: top-left (160 - w / 2, 100 - h / 2); a whole-screen picture at (0, 0)
        c.drawPicture(p, {.x = float(s->wImm ? 160 - (w >> 1) : 0), .y = float(s->wImm ? 100 - (h >> 1) : 0),
                          .w = float(w), .h = float(h)});
    }
};

struct Registrar {
    Registrar() {
        for (const Still& s : kStills) sceneRegistry()[s.scene] = [p = &s] { return (Scene*)new StillScene(p); };
    }
} s_registrar;
}  // namespace
