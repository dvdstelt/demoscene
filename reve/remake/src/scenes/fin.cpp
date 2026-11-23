// Script entry 26, FIN (reve/re/notes-parts3d.md): 3D\FIN.WLD, a spiked ellipse logo with "PULSE" around it flying in
// and turning, leaving trails.
//
// Ports the init 0x205bb0 (the world and its animation; reve3d::Scene3D), the retrace routine 0x205c87 (0x25f3ae: one
// animation step per refresh from the entry's first retrace) and the frame routine 0x205c90: the render, blended into
// the screen by 0x2046c7, screen = T[new][screen] with GFX\ANTIALIA.PAL (the average of the two). The blend runs once
// per drawn frame; the capture draws FIN every refresh (70.1 frames per second), so the remake steps the trail once
// per refresh as continuous motion: the screen at n steps = sum over k of 2^-(k+1) x the frame at n - k, from the
// black screen the init leaves. The trail is cut after 8 frames: the oldest takes the remaining weight 2^-7 (as if
// the screen before it had been that frame), so the total stays 1. The frames from k = 2 on (a quarter of the weight
// together) are drawn without multisampling.
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "reve/engine.h"

namespace {
constexpr int kTrail = 7;

struct FinScene : Scene {
    reve3d::Scene3D world;
    void init(Ctx& c) override { world.init(c, 0x205e69, /*aaColours=*/false); }   // "3D\FIN.WLD" (0x205be3)
    void render(Ctx& c, double r) override {
        const double n = c.refreshes(r);
        if (n < 0) return;
        if (reve3d::originalEngine()) {   // every frame from the first (one per refresh), blended into the screen
            std::vector<uint8_t> buf(64000), screen(64000);
            for (long m = 0; m <= long(n); m++) {
                world.renderOriginal(c, double(m), buf.data());
                world.original(c).blend(buf.data(), screen.data());
            }
            world.original(c).show(c, screen.data());
            return;
        }
        const int frames = std::min(kTrail, int(std::floor(n))) + 1;
        auto t = c.temp(false, 1.0f, reve3d::kTargetFormat);
        for (int k = 0; k < frames; k++) {
            world.render(c, n - k, *t, /*msaa=*/k < 2);
            c.out->bind();
            reve3d::addWeighted(c, *t, float(std::ldexp(1.0, -(k + (k == kTrail ? 0 : 1)))));
        }
    }
};
}  // namespace

REGISTER_SCENE(FinScene, "fin");
