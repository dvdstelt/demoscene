// Part 9 (0x4185e5; field 0x418094, voxels 0x418445, cube 0x417c48, buffers 0x418402; 18:00..20:00): three metaballs
// on Lissajous paths sampled on a 6 x 6 x 6 voxel grid and drawn as cubes with map C: small separate cubes at the
// edges of the blobs, solid blocks inside; grown from nothing from 18:08 (901, a fade over 2.86 s), turning about one
// of three axis combinations at a time (event 902 switches it), Gouraud-shaded by a drifting 3D sine field (light
// 1.2); a band of 2 x 8 tiles (rows 76..123) behind. Rows 0..3 and 196..199 are not written (black).
// clone/re/notes-parts3d.md section 5; the engine is cmc::Engine (src/clone/engine.h), the cubes and the field
// tiles3d.h.
//
// State as closed forms of the part's clock T (t70 units since the capture's origin, 5.7): the colour phases C = (0.02,
// 0.03, 0.04) T, the blob time E9 = 0.2 T, the fade min(1, 0.005 (t70 - t70(901))) from event 901 (the original from
// the frame before the frame that sees it: up to one frame earlier, invisible). The angles A0, A1, A2 (one of them
// turns at a time, at 0.003 (k + 1) per unit) are piecewise linear: event 902 switches k at the timer of the frame
// before the frame that sees it, 0.07..2.6 units before the row, which turns the block by up to 5 degrees against
// switching at the rows. The switches here are the capture's (notes-parts3d 5.3: measured t70 values), so the poses
// are the capture's; at another frame rate the original's differ by that much.
//
// Kept: the 6 x 6 x 6 grid, the half-size field / 6 with the 0.5 and 15 thresholds, the culled faces with the
// original's x + 2 neighbour test, the texture projected from the full cells, the shades at the shrunk corners, the
// window rows 20..179. The faces inside solid blocks that only the original's sort can show are hidden by the depth
// buffer. Evaluated exactly: see engine.h and tiles3d.h.
#include <algorithm>
#include <cmath>

#include "clone/engine.h"
#include "scenes/tiles3d.h"

namespace {
// The capture's t70 of the 21 axis switches of event 902 (notes-parts3d 5.3): k = 1, 2, 0, 1, 2, 0, ...
constexpr double kSwitch[21] = {6221.136, 6252.660, 6284.763, 6470.633, 6502.980, 6532.472, 6720.286,
                                6751.827, 6782.598, 6968.555, 7000.307, 7031.674, 7218.121, 7249.855,
                                7279.050, 7466.759, 7498.406, 7529.124, 7715.729, 7745.307, 7777.902};

struct CubesScene : Scene {
    p3d::Cubes cubes;
    cmc::EngineMesh mesh;
    float amp = 2.5f;
    int R[3] = {35, 35, 35};
    float G[512] = {};   // [0xf72d74]: float G[8][8][8], z, y, x; the border cells stay 0

    void init(Ctx& c) override {
        cubes.read(*c.exe);
        amp = c.exe->f32(0x428000);                                          // 2.5f
        for (int k = 0; k < 3; k++) R[k] = c.exe->s32(0x428004 + 4 * uint32_t(k));   // 35, 35, 35
        mesh.shade = cmc::Shade::Corners;
        mesh.map = cmc::Tex::MapC;
        c.shared<cmc::Engine>().prepare(cmc::Tex::MapC);
        c.shared<p3d::TileBand>();
        const double C[3] = {0, 0, 0};
        cubes.clear(mesh);
        cubes.cube(mesh, 0, 0, 180, 0x3f, 15, 0.003, C);
        mesh.pos = cubes.src;
        p3d::warmUp(c, mesh);
    }

    // A0, A1, A2 at t70 u: the time spent with each k since the origin, times 0.003 (k + 1)
    static void angles(double u0, double u, double A[3]) {
        A[0] = A[1] = A[2] = 0;
        double from = u0;
        int k = 0;
        for (double sw : kSwitch) {
            if (sw >= u) break;
            A[k] += 0.003 * (k + 1) * (sw - from);
            from = sw;
            k = (k + 1) % 3;
        }
        A[k] += 0.003 * (k + 1) * (u - from);
    }

    // 0x418094: three blobs on Lissajous paths (centres 12..52 on the grid 8..48), the field times the fade
    void field(double E, double fade) {
        auto cc = [&](double arg) { return 8 * (amp * std::sin(arg) + 4); };
        double c0 = cc(E / 23), c1 = cc(E / 33 + 1), c2 = cc(E / 43 + 2), c3 = cc(E / 30 + 3), c4 = cc(E / 43 + 4),
               c5 = cc(E / 24 + 5), c6 = cc(E / 30 + 6), c7 = cc(E / 43 + 7), c8 = cc(E / 24 + 8);
        const double b[3][3] = {{c2, c1, c0}, {c5, c4, c3}, {c8, c7, c6}};   // blob j at (x, y, z)
        for (int x = 1; x <= 6; x++)
            for (int y = 1; y <= 6; y++)
                for (int z = 1; z <= 6; z++)
                    G[z * 64 + y * 8 + x] = float(p3d::blobField(8 * x, 8 * y, 8 * z, b, R) * fade);
    }

    // 0x418445: a cube per cell, at 30 (i - 4): the block's centre is (-15, -15, -15)
    void voxels(const double (&C)[3]) {
        cubes.clear(mesh);
        for (int z = 1; z <= 6; z++)
            for (int y = 1; y <= 6; y++)
                for (int x = 1; x <= 6; x++) {
                    p3d::Voxel v = p3d::cubeMask(G, z * 64 + y * 8 + x, 64, 8);
                    if (v.mask)
                        cubes.cube(mesh, 30.0f * (x - 4), 30.0f * (y - 4), 30.0f * (z - 4), v.mask, v.size, 0.003, C);
                }
    }

    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        double A[3];
        angles(c.t70(c.origin), c.t70(t), A);
        double t901 = c.eventTime(901);
        double fade = t >= t901 ? std::min(1.0, 0.005 * (c.t70(t) - c.t70(t901))) : 0.0;   // [0x428010]
        const double C[3] = {0.02 * T, 0.03 * T, 0.04 * T};                                  // [0xf73588..]
        field(0.2 * T, fade);                                                                // E9 [0xf735a0]
        voxels(C);

        mesh.pos.resize(cubes.src.size());
        float a = float(A[0] + A[1] - A[2]), b = float(1.1 * (A[0] - A[1] + A[2])), r = float(0.8 * (A[1] - A[0] + A[2]));
        cmc::transform(cmc::Mat34::part(a, b, r, 4.0f / 3.0f, 180.0f), cubes.src, mesh.pos);

        c.shared<p3d::TileBand>().draw(c, 0, [](int row, int) { return row >= 2 && row <= 3; }, [](int) { return 1.0; });
        auto& eng = c.shared<cmc::Engine>();
        eng.begin(c, cmc::Window::at(c, t));                                                 // rows 20..179
        eng.draw(mesh, 1.2f);                                                                // event 9
        eng.end();
    }
};
}  // namespace

REGISTER_SCENE(CubesScene, "cubes");
