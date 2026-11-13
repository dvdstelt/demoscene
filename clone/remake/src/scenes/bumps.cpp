// Part 7 (0x416fce, set-up 0x416cca, deform 0x416e37; 24:00..29:00): the geosphere with map A, deformed into a blob
// with large bumps that travel over its surface, turning, at the right; a 6 x 5 block of tiles (x 40..239, rows
// 28..171) at the left; both fade in over 3.2 s. Event 701 freezes the motion (28:00), turns the tiles black (28:08)
// and stops drawing the blob (28:10): black to the end. clone/re/notes-parts3d.md section 3; the engine (geosphere,
// normals, cull, draw) is cmc::Engine (src/clone/engine.h).
//
// State as closed forms of the part's clock T (t70 units since the capture's origin): A7 = 0.0075 T, P = 0.03 T, the
// fade min(1, 0.0045 T), the light min(2 fade, 1) (3.5). The motion stops where the capture's stops: at t70 9956.198,
// the timer of the frame before the frame that saw 28:00 (A7 = 7.4680, P = 29.872); the original stops it at the
// frame before the event frame, so where it stops depends on the frame rate. The other two 701 events act at their
// music time.
//
// Kept from the original: the level-4 geosphere with its int16 vertices (a finer sphere resolves the creases of the
// deformation's clamp in the Gouraud shading, which level 4 averages away, and the bumps look different), the
// deformation evaluated at the integer vertices, the window of event 7 (the full height: the blob is cut by the
// screen's edges). Evaluated exactly: see engine.h (per-pixel perspective, continuous shade and decode, MSAA, a depth
// buffer) and tiles3d.h (the tile colours without truncation, anti-aliased).
#include <algorithm>
#include <cmath>

#include "clone/engine.h"
#include "scenes/tiles3d.h"

namespace {
constexpr double kFreezeT70 = 9956.198;   // the capture's t70 at which A7 and P stop (notes-parts3d 3.4)

struct BumpsScene : Scene {
    cmc::Geosphere geo;
    cmc::EngineMesh mesh;
    float K = 112.0f, speed = 0.03f;

    void init(Ctx& c) override {
        geo = cmc::Geosphere::make(*c.exe, 4);                 // 0x4174c4: 1026 vertices, 2048 faces
        mesh = geo.mesh(cmc::Tex::MapA, c.exe->f64(0x41a4d0)); // 0x416cca: uv = 0.004 (x, y), map A
        c.shared<cmc::Engine>().prepare(cmc::Tex::MapA);
        c.shared<p3d::TileBand>();
        K = c.exe->f32(0x427f7c);                              // 112.0f
        speed = c.exe->f32(0x427f78);                          // 0.03f
        for (size_t i = 0; i < mesh.pos.size(); i++) mesh.pos[i] = geo.verts[i] + Vec3(0, 0, 2000);
        p3d::warmUp(c, mesh);
    }

    // 0x416e37: a radial scale s(vertex on the undeformed sphere, P), then z += 2000
    void deform(float P) {
        float P8 = float(P * 0.8), P13 = float(P * 1.3);
        for (size_t i = 0; i < mesh.pos.size(); i++) {
            const Vec3& o = geo.verts[i];
            float s = float((0.5 * std::sin(o.x / K + P) + 2.0 / 3) + (0.5 * std::sin(o.y / K + P8) + 2.0 / 3) +
                            (0.5 * std::sin(o.z / K + P13) + 2.0 / 3));
            if (s > 2) {
                float k = std::min(float(1 + 3 * (double(s) - 2)), 1.5f);
                s = float(double(s) * k);
            }
            Vec3& p = mesh.pos[i];
            p = Vec3(p.x * s, p.y * s, p.z * s + 2000.0f);
        }
    }

    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        int n701 = c.eventCount(701, t);                                       // [0x427f74]
        double Tm = std::min(T, kFreezeT70 - c.t70(c.origin) + c.clock0);      // A7 and P stop
        float A7 = float(0.0075 * Tm), P = float(speed * Tm);
        double fade = std::min(1.0, 0.0045 * T);                               // [0x427f80]
        float light = fade >= 0.5 ? 1.0f : float(fade * 2);                    // [0x427f70]

        c.shared<p3d::TileBand>().draw(
            c, 0, [&](int row, int col) { return n701 < 2 && row >= 0 && row <= 5 && col >= 1 && col <= 5; },
            [&](int) { return fade; });
        if (n701 >= 3) return;                                                 // 28:10: no sort, no draw

        cmc::transform(cmc::Mat34::part(A7, float(1.1 * A7), float(0.8 * A7), 4.0f / 3.0f), geo.verts, mesh.pos);
        deform(P);
        auto& eng = c.shared<cmc::Engine>();
        eng.begin(c, cmc::Window::at(c, t));                                   // event 7: the full height
        eng.draw(mesh, light);
        eng.end();
    }
};
}  // namespace

REGISTER_SCENE(BumpsScene, "bumps");
