// The end picture: main's last loop 0x18c25. "~JIZZ~ TBL'97" (the 16-bit picture [0x657a] that texture build 14
// "logo1" leaves: clouds times the RLE picture at 0x275b8) fades in from white with the timer fade 0x17979(0x200b20)
// / 0x17999 (white fade tables, still those of 0x177bf), until the sync table ends (15:1c). jizz/re/notes-parts2d 7.
//
// The fade is continuous (the original steps through 32 levels of 0x179f6). The picture, with the redraw
// (art/lettering.png, jizz/art.h): the script's clouds (Tex::LogoClouds) times the redraw's letters at the
// picture's solid level, as the script multiplies them (logo1 = clouds x picture / 256); the letters are the
// redraw's coverage at 1/2, anti-aliased at the output resolution. With --original-art (or without the file): the
// high-resolution logo1 texture, filtered.
#include <algorithm>

#include "demo.h"
#include "jizz/art.h"
#include "jizz/core.h"
#include "jizz/textures.h"

namespace {
using namespace jizz;

constexpr int kPos = 0x1500;             // 15:00: 0x10fda returns, main starts the fade and the loop
constexpr uint32_t kFadeRate = 0x200b20; // 0x18c1b: eax for 0x17979 (1.754 s)

const char* kArtFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D clouds;  // the script's clouds (256x256 texels, wrapping): logo1 = clouds x picture / 256
uniform sampler2D art;     // the redraw: coverage over the 256x200 picture, x wrapping at 256 (tile320)
uniform vec2 size;
uniform float level;       // the picture's solid level / 256
void main() {
    vec2 vga = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    float m = texture(art, vga / vec2(256.0, 200.0)).r;
    float inner = clamp((m - 0.5) / max(fwidth(m), 1e-3) + 0.5, 0.0, 1.0);  // the letters, one output pixel of edge
    vec3 c = max(texture(clouds, vga / 256.0).rgb * level - 0.5 / 255.0, 0.0);  // the multiply's floor as its mean
    o = vec4(c * inner, 1.0);
}
)";

class EndScene : public Scene {
public:
    void init(Ctx& c) override {
        const Textures& tx = c.shared<Textures>();
        jizz::prepare(c);
        start_ = c.positionTime(kPos);
        ArtImage im;
        if (loadArtGrey(c, "lettering", &im)) {
            uploadArt(im, art_);
            const uint8_t* pa = tx.planar(Tex::PictureA);
            level_ = float(*std::max_element(pa, pa + 65536)) / 256.0f;  // 238: the letters' solid pixels
            shader_.build(kFullscreenVS, kArtFS, "end picture (redraw)");
        }
    }

    void render(Ctx& c, double t) override {
        const Textures& tx = c.shared<Textures>();
        setBlend(Blend::None);
        if (art_.id) {
            shader_.use();
            tx.bind(Tex::LogoClouds, 0);
            art_.bind(1);
            shader_.set("clouds", 0);
            shader_.set("art", 1);
            shader_.set("size", float(c.out->w), float(c.out->h));
            shader_.set("level", level_);
            drawFullscreen();
        } else {
            // the picture is 320x200 with x wrapping at 256 (tile320): VGA x maps to u = x / 256
            c.drawTexture(tx.id(Tex::Logo1), 0, 0, 320, 200, 1.0f, 0, 0, 320.0f / 256, 200.0f / 256);
        }
        timerFade(c, t, start_, kFadeRate, kWhite);  // 0x17999: from white to the picture
    }

private:
    Shader shader_;
    Texture art_;  // the redraw (id 0 with --original-art)
    double start_ = 0;
    float level_ = 1;
};

REGISTER_SCENE(EndScene, "end");
}  // namespace
