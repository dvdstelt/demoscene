// Part 2 (0x4198bf, 0c:00..10:00): the camera inside a flat elliptic tube with the grid texture (table 1) on its
// wall, gliding along it and swaying sideways, each scan line looking in a slightly different direction; the
// brightness ramps from 0.3 to 1 at the start and again from event 201 (0e:00), which also switches the camera
// path; "meet us" in a black box and glyph 1. clone/re/notes-grid.md sections 5 and 8.
//
// Evaluated exactly instead of the original's shortcuts: the ray, the cylinder hit, u, v and the light per pixel
// (grid.h) instead of at the 41 x 21 grid entries; the per-row rotation steps (+0.02, 0.024, 0.016 rad per grid row)
// as a rotation that grows continuously down the screen; the state as closed forms of the part's clock.
//
// Frame-rate quirk kept (the capture's): the camera position (Ox, Oz) is computed from the camera clock before the
// frame's dt is added, the rotation from the clock after it, so the picture shows the position of one frame
// interval earlier: Delta = 1.73 t70 units (24.7 ms, the capture's frame interval in this part). The brightness ramp
// of event 201 starts at the capture's tau (the timer of the frame before the event frame, 7.0 ms before 0e:00).
#include "scenes/grid.h"

#include <algorithm>
#include <cmath>

#include "exe.h"

namespace {
constexpr double kPositionLag = 1.73;     // t70 units: the capture's frame interval in part 2 (notes-grid 8)
constexpr double kLag201 = 7.02e-3;       // s: event 201's tau before 0e:00 in the capture (event_lag.py)

const char* kTubeGLSL = R"(
uniform vec2 cam;      // Ox, and 0.075 Oz reduced by whole texture periods (texels)
uniform vec3 rot;      // r1, r2, r3 at the top grid row (y = 20)
uniform float bright;  // Br
vec3 gridMap(vec2 p) {
    float row = (p.y - 20.0) / 8.0;                                   // grid row, continuous
    vec3 r = gridRotate(vec2(p.x - 160.0, p.y - 100.0) / 120.0, rot + vec3(0.02, 0.024, 0.016) * row);
    float n = length(r);
    vec3 d = vec3(1.0 / n * 0.17 * r.x, r.y / n, r.z / n);            // the x of the ray scaled: an elliptic tube
    float t = gridCylinder(d.xy, vec2(cam.x, 0.0));
    vec2 h = vec2(cam.x + d.x * t, d.y * t);
    float u = cam.y + 0.6 * 256.0 * 0.5 * 0.25 / 256.0 * d.z * t + 128.0;   // 0.075 hz; -0x8000 = +128 texels
    float v = atan(h.x, h.y) * 256.0 / PI + 128.0;                          // twice round the texture per turn
    float L = bright * 878.0 * clamp(53.0 - t * 0.024 / 256.0 * 192.0, 16.0, 64535.0 / 878.0) / 256.0;
    return vec3(u, v, L);
}
)";

struct TubeScene : Scene {
    gridfill::Filler fill;
    double br0 = 0.3;  // [0x428160]: Br's initial value

    void init(Ctx& c) override {
        fill.build(c, kTubeGLSL, "tube");
        br0 = c.exe->f32(0x428160);
    }

    void render(Ctx& c, double t) override {
        double tc = c.clock(t);                                // TC after this frame's dt
        double tcPos = std::max(0.0, tc - kPositionLag);       // TC before it, at the capture's frame interval
        bool path2 = t >= c.eventTime(201);                    // event 201: flag = 1, Br = 0.3
        double ox = path2 ? 110.0 * std::sin(tcPos * 0.013 + 2.0) : 150.0 * std::sin(tcPos * 0.01);
        double oz = path2 ? tcPos * 3.0 : tcPos * 5.0;
        double br = path2 ? 0.3 + 0.01 * (c.t70(t) - c.t70(c.eventTime(201) - kLag201)) : br0 + 0.01 * tc;
        double uz = std::fmod(oz * (0.6 * 256.0 * 0.5 * 0.25 / 256.0), 256.0);

        const Shader& sh = fill.begin(c, t);
        sh.set("cam", float(ox), float(uz));
        sh.set("rot", float(tc * 0.01), float(tc * 0.012), float(tc * 0.008));
        sh.set("bright", float(std::min(1.0, br)));
        fill.draw();
        gridfill::drawCaption(c, {10, 130, 100, 40, "meet_us", 20, 151, "glyph1", 80, 111});
    }
};
}  // namespace

REGISTER_SCENE(TubeScene, "tube");
