// Part 8 (0x417929, set-up 0x417563, deform 0x4176d0; 10:00..14:18): the geosphere deformed into a lumpy blob with map
// B (a sphere of radius 600 with plateau-topped bumps up to 750), turning, flies in from z = 9400 to 1400 in 1.52 s
// while it brightens (light 1400 / Z); faces whose mean depth is beyond 6826.7 are not drawn, so it comes in face by
// face. A band of 4 x 8 coloured tiles (rows 52..147) appears two columns at a time (801 at 10:04, 10:08, 10:0c,
// 10:10), every tile pulsing between 80 and 100 % at its own speed; from 14:00 (803) the tiles fade out in 1.3 s and
// the blob darkens in three steps. Rows 0..3 and 196..199 are not written (black). clone/re/notes-parts3d.md
// section 4; the engine is cmc::Engine (src/clone/engine.h).
//
// State as closed forms of the part's clock T (t70 units since the capture's origin, 4.4): tt = 0.05 T, A8 = 0.0075 T,
// P = 0.03 T, Z = max(1400, 9400 - 75 T) - (803 events), the light 1400 / Z, then 1 - 0.25 per 803 event; the tile fade
// 1 - 0.011 (t70 - t70(803)) from the first 803 event (the original starts it in the frame that sees the event,
// integrated from the frame before, up to one frame earlier: invisible). Event 802 is not in the sync table: the
// deformation's [0x427fe8] stays 0. Nothing depends on the frame rate.
//
// Kept: the level-4 geosphere with its int16 vertices, the deformation at the integer vertices, the far cut (the
// engine's), the window rows 20..179. Evaluated exactly: see engine.h and tiles3d.h.
#include <algorithm>
#include <cmath>

#include "clone/engine.h"
#include "scenes/tiles3d.h"

namespace {
struct BlobScene : Scene {
    cmc::Geosphere geo;
    cmc::EngineMesh mesh;
    float K = 69.115036f, speed = 0.03f, Z0 = 9400.0f, fade0 = 1.0f;

    void init(Ctx& c) override {
        geo = cmc::Geosphere::make(*c.exe, 4);                 // 0x4174c4 again
        mesh = geo.mesh(cmc::Tex::MapB, c.exe->f64(0x41a554)); // 0x417563: uv = 0.004 (x, y), map B
        c.shared<cmc::Engine>().prepare(cmc::Tex::MapB);
        c.shared<p3d::TileBand>();
        K = c.exe->f32(0x427ff8);                              // 22 pi
        speed = c.exe->f32(0x427ff4);                          // 0.03f
        Z0 = c.exe->f32(0x427fe4);                             // 9400.0f
        fade0 = c.exe->f32(0x427fec);                          // the tile fade, 1.0f
        for (size_t i = 0; i < mesh.pos.size(); i++) mesh.pos[i] = geo.verts[i] + Vec3(0, 0, 2000);
        p3d::warmUp(c, mesh);
    }

    // 0x4176d0 with [0x427fe8] = 0: the scale is 2 where the sum of the sines is <= 0, rises gently to 2.5 and is 2.5
    // above; z += Z
    void deform(float P, float Z) {
        float P8 = float(P * 0.8), P13 = float(P * 1.3);
        for (size_t i = 0; i < mesh.pos.size(); i++) {
            const Vec3& o = geo.verts[i];
            float s = float(2 + 0.5 * (std::sin(o.x / K + P) + std::sin(o.y / K + P8) + std::sin(o.z / K + P13)));
            if (s < 2) {
                s = 2;
            } else if (s > 3) {
                s = 2.5f;   // min((s - 3) * 2.4, 1.5) * [0x427fe8] + 2.5, at most 3.5
            } else if (s > 2) {
                float k = std::min(float(1 + (double(s) - 2) * 0.1), 1.5f);
                s = std::min(float(double(s) * k), 2.5f);
            }
            Vec3& p = mesh.pos[i];
            p = Vec3(p.x * s, p.y * s, p.z * s + Z);
        }
    }

    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        int n803 = c.eventCount(803, t);
        float Z = float(std::max(1400.0, double(Z0) - 75.0 * T) - n803);       // [0x427fe4]
        float light = n803 ? float(1.0 - 0.25 * n803) : 1400.0f / Z;          // [0x427f70]
        double fade = fade0;                                                   // [0x427fec]
        if (n803) fade = std::max(0.0, fade0 - 0.011 * (c.t70(t) - c.t70(c.eventTime(803, 0))));
        double tt = 0.05 * T;                                                  // [0xf72d70]
        float A8 = float(0.0075 * T), P = float(speed * T);
        int cols = 2 * c.eventCount(801, t);                                   // [0x427ff0]

        // the tile factor: fade times w = min(1, sin(R tt 0.01 + 0.2 idx) 0.1 + 0.9), R the tile's red value
        c.shared<p3d::TileBand>().draw(
            c, 24, [&](int row, int col) { return row >= 1 && row <= 4 && col < cols; },
            [&](int idx) {
                double w = std::sin(p3d::tileRed(*c.exe, idx) * tt * 0.01 + idx * 0.2) * 0.1 + 0.9;
                return fade * std::min(1.0, w);
            });

        cmc::transform(cmc::Mat34::part(A8, float(1.1 * A8), float(0.8 * A8), 40.0f / 27.0f), geo.verts, mesh.pos);
        deform(P, Z);
        auto& eng = c.shared<cmc::Engine>();
        eng.begin(c, cmc::Window::at(c, t));                                   // rows 20..179
        eng.draw(mesh, light);
        eng.end();
    }
};
}  // namespace

REGISTER_SCENE(BlobScene, "blob");
