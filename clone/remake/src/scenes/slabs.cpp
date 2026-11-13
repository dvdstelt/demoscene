// Part 10 (0x41958e; field 0x418fd2, voxels 0x4193b6, cube 0x418b86, buffers 0x419373; 36:00..3a:00): three metaballs
// on a 4 x 10 x 4 voxel grid drawn as cubes with map B: a tall column turning slowly, cut by the window (rows
// 20..179), Gouraud-shaded by the sine field of part 9 (its colour phases continue); a band of 4 x 8 tiles (rows
// 76..171) behind. 39:00..39:3e: the light pulses 1.2 / 1.0 every two rows (events 1011 / 1012). Rows 196..199 are not
// written (black). clone/re/notes-parts3d.md section 6; the engine is cmc::Engine (src/clone/engine.h), the cubes and
// the field tiles3d.h.
//
// State as closed forms of the part's clock T (t70 units since the capture's origin, 6.6): A10 = 0.002 T, the blob time
// E10 = 0.3 T, the colour phases (0.02, 0.03, 0.04) (T9 + T) with T9 part 9's clock at its last frame (the capture's
// origin of part 5 minus that of part 9: 1990.64). The light at the events' music times. Nothing depends on the frame
// rate.
//
// The original's grid bugs are kept, they shape the column: the field array is laid out as [6][6][6] (strides 36, 6, 1)
// but b runs to 10, so cell (a, b) and (a + 1, b - 6) share a slot and the later write wins (the bottom of the column
// repeats the top shifted by one in a; 24 % of the cubes); the neighbour tests read the flat array, k + 2 for +x as in
// part 9, and past its end for the a + 1 test of a = 4: the slot 224 is E10 [0xf73924] (full from E10 = 90) and 225
// part 2's camera time [0xf73928] (994.8 in the capture, always full), the rest of what follows the array is below 90
// (notes-parts3d 6.4). Evaluated exactly: see engine.h and tiles3d.h.
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include "clone/engine.h"
#include "scenes/tiles3d.h"

namespace {
constexpr double kPart2CameraTime = 994.8;   // [0xf73928] after part 2, measured in the capture

struct SlabsScene : Scene {
    p3d::Cubes cubes;
    cmc::EngineMesh mesh;
    float amp = 2.3f;
    int R[3] = {35, 35, 35};
    double T9 = 1990.64;   // part 9's clock at its end (t70 units)
    float g[256] = {};     // [0xf735a4]: the flat array k = 36 a + 6 b + e, and what follows it up to k = 255

    void init(Ctx& c) override {
        cubes.read(*c.exe);
        amp = c.exe->f32(0x428144);                                          // 2.3f
        for (int k = 0; k < 3; k++) R[k] = c.exe->s32(0x428148 + 4 * uint32_t(k));   // 35, 35, 35
        mesh.shade = cmc::Shade::Corners;
        mesh.map = cmc::Tex::MapB;
        c.shared<cmc::Engine>().prepare(cmc::Tex::MapB);
        c.shared<p3d::TileBand>();
        const double C0[3] = {0, 0, 0};
        cubes.clear(mesh);
        cubes.cube(mesh, 0, 0, 150, 0x3f, 15, 0.009, C0);
        mesh.pos = cubes.src;
        p3d::warmUp(c, mesh);
        // part 9 integrates its colour phases from its origin to its last frame, the timer of the next part's origin
        std::vector<Cue> cues = buildTimeline(c);
        for (size_t i = 0; i + 1 < cues.size(); i++)
            if (cues[i].part == 9) T9 = c.t70(cues[i + 1].origin) - c.t70(cues[i].origin);
    }

    // 0x418fd2: three blobs, the second coordinate (b) with twice the amplitude about 6, written e outer, b, a inner
    // into the [6][6][6] slots (the aliasing above)
    void field(double E) {
        auto c3 = [&](double arg) { return 8 * (amp * std::sin(arg) + 3); };
        auto c6 = [&](double arg) { return 8 * (double(amp * 2.0f) * std::sin(arg) + 6); };
        const double b[3][3] = {{c3(E / 23), c6(E / 33 + 1), c3(E / 43 + 2)},
                                {c3(E / 30 + 3), c6(E / 43 + 4), c3(E / 24 + 5)},
                                {c3(E / 30 + 6), c6(E / 43 + 7), c3(E / 24 + 8)}};   // blob j at (a, b, e)
        std::fill(std::begin(g), std::end(g), 0.0f);
        for (int e = 1; e <= 4; e++)
            for (int bi = 1; bi <= 10; bi++)
                for (int a = 1; a <= 4; a++) g[36 * a + 6 * bi + e] = p3d::blobField(8 * a, 8 * bi, 8 * e, b, R);
        g[224] = float(E);                    // E10, read by the a + 1 test of (4, 7, 2)
        g[225] = float(kPart2CameraTime);     // read by the a + 1 test of (4, 7, 3)
    }

    // 0x4193b6: a outer, b, e inner; neighbours k -+ 36 (a), k -+ 6 (b), k - 1 and k + 2 (e)
    void voxels(const double (&C)[3]) {
        cubes.clear(mesh);
        for (int a = 1; a <= 4; a++)
            for (int bi = 1; bi <= 10; bi++)
                for (int e = 1; e <= 4; e++) {
                    p3d::Voxel v = p3d::cubeMask(g, 36 * a + 6 * bi + e, 36, 6);
                    if (v.mask)
                        cubes.cube(mesh, 30.0f * (e - 3), 30.0f * (bi - 3), 30.0f * (a - 3), v.mask, v.size, 0.009, C);
                }
    }

    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        float A10 = float(0.002 * T);                                                  // [0xf73920]
        const double C[3] = {0.02 * (T9 + T), 0.03 * (T9 + T), 0.04 * (T9 + T)};       // part 9's, continued
        field(0.3 * T);                                                                // E10 [0xf73924]
        voxels(C);

        mesh.pos.resize(cubes.src.size());
        cmc::transform(cmc::Mat34::part(A10, float(1.1 * A10), float(0.8 * A10), 4.0f / 3.0f, 150.0f), cubes.src,
                       mesh.pos);

        c.shared<p3d::TileBand>().draw(c, 24, [](int row, int) { return row >= 2 && row <= 5; }, [](int) { return 1.0; });
        float light = c.lastEvent(1011, t) > c.lastEvent(1012, t) ? 1.2f : 1.0f;     // [0x427f70]
        auto& eng = c.shared<cmc::Engine>();
        eng.begin(c, cmc::Window::at(c, t));                                           // event 10: rows 20..179
        eng.draw(mesh, light);
        eng.end();
    }
};
}  // namespace

REGISTER_SCENE(SlabsScene, "slabs");
