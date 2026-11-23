// Script entries 1-4, the INTRO (reve/re/notes-parts3d.md): 3D\INTRO.WLD, a chrome surfer flying around a knotted tube,
// with the name of one member of the group over it: "LCA" (entry 1), "AJT" (2), "ANTONY" (3), "SHAD" (4).
//
// Ports the init 0x2048e2 (the world, its animation, GOURAUD.PAL; reve3d::Scene3D), the retrace routine 0x25f3ae
// (one animation step per refresh, from entry 1's first retrace on: entries 2-4 have no init and the animation runs on;
// it loops every 626 refreshes) and the frame routines 0x2049c4 / 0x204a10 / 0x204a5c / 0x204aa4: the render, the AA
// pass 0x2045fa (here the blur it approximates, in colour), then the name, drawn by the sprite routine 0x25f0be
// centred, sharp, over the blurred 3D.
#include <algorithm>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "reve/engine.h"

namespace {
// Per entry 1..4: the picture (a band of GFX\CREDITS.RAW, src/pictures.cpp) and the address of the frame routine's
// `mov bl, width` immediate (`mov bh, height` follows it at +2).
struct Name {
    const char* picture;
    uint32_t width;
};
constexpr Name kNames[4] = {{"credits_lca", 0x2049e8},
                            {"credits_ajt", 0x204a34},
                            {"credits_antony", 0x204a7a},
                            {"credits_shad", 0x204ac8}};
// 0x25f0be centres the sprite: top-left (cx - (w >> 1), cy - (h >> 1)): lea edi, [edi + eax + 0xa0] / add eax, 0x64
constexpr uint32_t kSpriteCx = 0x25f0dc, kSpriteCy = 0x25f0c8;

constexpr uint32_t kCreditsName = 0x205d08;   // "GFX\CREDITS.RAW" (0x2048e3)

struct CreditsScene : Scene {
    reve3d::Scene3D world;
    RawImage credits;   // REVE_ENGINE=original: the sprite's indices
    int w[4] = {}, h[4] = {}, cx = 160, cy = 100;

    void init(Ctx& c) override {
        world.init(c, 0x205d39, /*aaColours=*/true);   // the name 0x2048fb passes: "3D\INTRO.WLD"
        for (int i = 0; i < 4; i++) {
            w[i] = c.exe->u8(kNames[i].width);
            h[i] = c.exe->u8(kNames[i].width + 2);
        }
        cx = c.exe->s32(kSpriteCx);
        cy = c.exe->s8(kSpriteCy);
    }

    void render(Ctx& c, double r) override {
        // the animation counts from entry 1's first retrace (c.script()[1].start) through entries 2-4
        const double n = std::max(0.0, r - c.script()[1].start);
        const int e = std::clamp(c.cue->entry, 1, 4) - 1;
        if (reve3d::originalEngine()) {   // the original's frame: render, AA pass into [0x205e88], the sprite's indices
            std::vector<uint8_t> buf(64000), screen(64000);
            world.renderOriginal(c, n, buf.data());
            world.original(c).aa(buf.data(), screen.data());
            if (credits.px.empty()) credits = rawImage(*c.exe, c.exe->cstr(kCreditsName));
            // the frame routine's `add esi, offset` (none in entry 3's) and `mov edx, stride` (0x2049ec, 231)
            const uint32_t off = e == 2 ? 0 : c.exe->u32(kNames[e].width - 5);
            const int stride = c.exe->s32(0x2049ec), x0 = cx - (w[e] >> 1), y0 = cy - (h[e] >> 1);
            for (int y = 0; y < h[e]; y++)
                for (int x = 0; x < w[e]; x++) {
                    size_t s = off + size_t(y) * stride + x;
                    uint8_t i = s < credits.px.size() ? credits.px[s] : 0;
                    if (i) screen[size_t(y0 + y) * 320 + x0 + x] = i;
                }
            world.original(c).show(c, screen.data());
            return;
        }
        {
            auto t = c.temp(false, 1.0f, reve3d::kTargetFormat);
            world.render(c, n, *t);
            c.out->bind();
            reve3d::aaPass(c, *t);
        }
        c.drawPicture(c.picture(kNames[e].picture), {.x = float(cx - (w[e] >> 1)), .y = float(cy - (h[e] >> 1)),
                                                     .w = float(w[e]), .h = float(h[e])});
    }
};
}  // namespace

REGISTER_SCENE(CreditsScene, "credits");
