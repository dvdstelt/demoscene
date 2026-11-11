// The end picture: main's last loop 0x18c25. "~JIZZ~ TBL'97" (the 16-bit picture [0x657a] that texture build 14
// "logo1" leaves: clouds times the RLE picture at 0x275b8) fades in from white with the timer fade 0x17979(0x200b20)
// / 0x17999 (white fade tables, still those of 0x177bf), until the sync table ends (15:1c). jizz/re/notes-parts2d 7.
//
// The fade is continuous (the original steps through 32 levels of 0x179f6); the picture is the high-resolution
// logo1 texture, filtered.
#include "demo.h"
#include "jizz/core.h"
#include "jizz/textures.h"

namespace {
using namespace jizz;

constexpr int kPos = 0x1500;             // 15:00: 0x10fda returns, main starts the fade and the loop
constexpr uint32_t kFadeRate = 0x200b20; // 0x18c1b: eax for 0x17979 (1.754 s)

class EndScene : public Scene {
public:
    void init(Ctx& c) override {
        c.shared<Textures>();
        jizz::prepare(c);
        start_ = c.positionTime(kPos);
    }

    void render(Ctx& c, double t) override {
        const Textures& tx = c.shared<Textures>();
        setBlend(Blend::None);
        // the picture is 320x200 with x wrapping at 256 (tile320): VGA x maps to u = x / 256
        c.drawTexture(tx.id(Tex::Logo1), 0, 0, 320, 200, 1.0f, 0, 0, 320.0f / 256, 200.0f / 256);
        timerFade(c, t, start_, kFadeRate, kWhite);  // 0x17999: from white to the picture
    }

private:
    double start_ = 0;
};

REGISTER_SCENE(EndScene, "end");
}  // namespace
