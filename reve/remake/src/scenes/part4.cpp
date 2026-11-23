// Script entry 22, PART4 (reve/re/notes-parts3d.md): 3D\PART4.WLD, the surfer on a path of tiles through flat-shaded
// letters and numbers, rings and pillars, every object outlined by its bounding box.
//
// Ports the init 0x205a11 (the world and its animation, and its two switches: [0x21d43c] = 0x21dfb8, the cull that
// draws an object only when a box corner projects inside the screen, and [0x2174c6] = 1, the boxes 0x25f26e;
// reve3d::Scene3D with part4 = true), the retrace routine 0x25f3ae (one animation step per refresh from the entry's
// first retrace) and the frame routine 0x205ac0: the render with the boxes and the AA pass 0x2045fa (the blur it
// approximates, in colour, over the boxes too).
#include <vector>

#include "demo.h"
#include "reve/engine.h"

namespace {
struct Part4Scene : Scene {
    reve3d::Scene3D world;
    void init(Ctx& c) override { world.init(c, 0x205e4e, /*aaColours=*/true, /*part4=*/true); }   // "3D\PART4.WLD" (0x205a44)
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

REGISTER_SCENE(Part4Scene, "part4");
