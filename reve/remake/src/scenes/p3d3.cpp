// Script entry 14, P3D3 (reve/re/notes-parts3d.md): 3D\P3D3.WLD, the surfer (unshaded) past a big sphere and over a
// checkerboard of cubes with small trees; all faces textured without lighting.
//
// Ports the init 0x205881 (the world and its animation; reve3d::Scene3D), the retrace routine 0x25f3ae (one animation
// step per refresh from the entry's first retrace) and the frame routine 0x20590c: the render and the AA pass 0x2045fa
// (the blur it approximates, in colour).
#include <vector>

#include "demo.h"
#include "reve/engine.h"

namespace {
struct P3d3Scene : Scene {
    reve3d::Scene3D world;
    void init(Ctx& c) override { world.init(c, 0x205e34, /*aaColours=*/true); }   // "3D\P3D3.WLD" (0x20589f)
    void render(Ctx& c, double r) override {
        if (reve3d::originalEngine()) {   // the original's frame: the render, the AA pass into the cleared screen
            std::vector<uint8_t> buf(64000), screen(64000);
            world.renderOriginal(c, c.refreshes(r), buf.data());
            world.original(c).aa(buf.data(), screen.data());
            world.original(c).show(c, screen.data());
            return;
        }
        auto t = c.temp(false, 1.0f, reve3d::kTargetFormat);
        world.render(c, c.refreshes(r), *t);
        c.out->bind();
        reve3d::aaPass(c, *t);
    }
};
}  // namespace

REGISTER_SCENE(P3d3Scene, "p3d3");
