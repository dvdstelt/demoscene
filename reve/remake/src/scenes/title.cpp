// The title (script entry 0: init 0x204840, frame routine 0x20489c; reve/re/PARTS.md): the picture GFX\REVE.RAW,
// "RĒVE" in white, on black, centred by the sprite routine 0x25f0be, for the first pattern of the music. The init
// sets the palette of GFX\TEXTURE.RAW (0x25a1fb with EAX = 1) and loads REVE.RAW without its own, so the title shows
// through TEXTURE.RAW's palette; index 0 is not drawn.
#include "demo.h"
#include "exe.h"

namespace {
// 0x25f0be centres a w x h sprite: top-left = (160 - w / 2, 100 - h / 2), w and h halved by shifts.
struct TitleScene : Scene {
    int w = 0, h = 0;
    void init(Ctx& c) override {
        w = c.exe->u8(0x2048a4);   // mov bl, 0xe3 in the frame routine: the width
        h = c.exe->u8(0x2048a6);   // mov bh, 0x50: the height
    }
    void render(Ctx& c, double) override {
        const Picture& p = c.picture("title");
        c.drawPicture(p, {.x = float(160 - (w >> 1)), .y = float(100 - (h >> 1)), .w = float(w), .h = float(h)});
    }
};
}  // namespace

REGISTER_SCENE(TitleScene, "title");
