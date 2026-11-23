// Script entry 11, PART2 (reve/re/notes-parts3d.md): 3D\PART2.WLD, the surfer over a textured landscape toward a
// spiked tower; all faces Gouraud-textured.
//
// Ports the init 0x2057b0 (the world and its animation; reve3d::Scene3D), the retrace routine 0x25f3ae (one animation
// step per refresh from the entry's first retrace) and the frame routine 0x205850: the render, copied to the screen as
// it is (no AA pass in this entry).
#include <vector>

#include "demo.h"
#include "reve/engine.h"

namespace {
struct Part2Scene : Scene {
    reve3d::Scene3D world;
    void init(Ctx& c) override { world.init(c, 0x205e27, /*aaColours=*/false); }   // "3D\PART2.WLD" (0x2057e3)
    void render(Ctx& c, double r) override {
        if (reve3d::originalEngine()) {   // the original's frame, copied as it is
            std::vector<uint8_t> buf(64000);
            world.renderOriginal(c, c.refreshes(r), buf.data());
            world.original(c).show(c, buf.data());
            return;
        }
        auto t = c.temp(false, 1.0f, reve3d::kTargetFormat);
        world.render(c, c.refreshes(r), *t);
        c.out->bind();
        setBlend(Blend::None);
        c.drawTarget(*t);
    }
};
}  // namespace

REGISTER_SCENE(Part2Scene, "part2");
