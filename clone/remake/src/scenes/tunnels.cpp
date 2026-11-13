// Part 4 (0x4037c0, 30:20..36:00): inside three perpendicular tunnels of radius 192 crossing at the origin, the
// camera floating near the crossing and turning; the walls carry the grid texture (table 2) projected along
// (-1, 1, 0) and darken with distance to black at the far ends; dim and pulsing until event 403 (31:00), then black
// and fading up in 1.4 s; event 401 (32:00..35:00) cycles the turning rates; "tunnels meet more tunnels" in a black
// box and glyph 4. clone/re/notes-grid.md sections 7 and 8.
//
// Evaluated exactly instead of the original's shortcuts: the ray, the three cylinder hits, u, v and the light per
// pixel (grid.h) instead of at the 41 x 21 grid entries (whose x4 coordinates and wrong-way interpolation give the
// original's block noise at the far ends, where the texture is now filtered); the state as closed forms of the
// part's clock.
//
// Frame-rate quirks kept (the capture's): the pulse before event 403 is computed from the clock before the frame's dt
// (one frame interval behind: 1.79 t70 units in the capture); the ramp of event 403 and the rate switches of the
// events 401 start at the capture's tau (the timer of the frame before the event frame, 17..24 ms before the row).
#include "scenes/grid.h"

#include <algorithm>
#include <cmath>

#include "exe.h"

namespace {
constexpr double kPulseLag = 1.79;                                   // t70 units: the capture's frame interval
constexpr double kLag403 = 24.04e-3;                                 // s: event 403's tau before 31:00
constexpr double kLag401[] = {22.54e-3, 20.54e-3, 19.04e-3, 17.27e-3};  // s: the events 401 (32:00..35:00)
constexpr uint32_t kRates = 0x421a58;                                // 3 modes x 3 floats: rad per t70 unit
constexpr uint32_t kMode = 0x421a7c;                                 // the mode's initial value

const char* kTunnelsGLSL = R"(
uniform vec3 P;        // the camera: 100 (sin 0.03 T4, sin 0.043 T4, sin 0.053 T4)
uniform vec3 ang;      // a1, a2, a3
uniform float bright;  // B
vec3 gridMap(vec2 p) {
    vec3 r = gridRotate(vec2(p.x - 160.0, p.y - 100.0) / 120.0, ang);
    vec3 d = r * (1.0 / length(r));
    float t = max(max(gridCylinder(d.xy, P.xy),    // the tunnel along z
                      gridCylinder(d.zy, P.zy)),   // along x
                      gridCylinder(d.zx, P.zx));   // along y
    vec3 h = P + d * t;
    float u = (h.x + h.y + h.z) * 8.0 * 4.0 / 256.0 + 128.0;   // trunc(8 s) << 2 in 1/256 texel; -0x8000 = +128
    float v = (h.x + h.y - h.z) * 8.0 * 4.0 / 256.0 + 128.0;
    float L = bright * clamp(46534.0 - t * 878.0 * 0.026 / 256.0 * 192.0, 14048.0, 64535.0) / 256.0;
    return vec3(u, v, L);
}
)";

struct TunnelsScene : Scene {
    gridfill::Filler fill;
    double rate[3][3] = {};
    int mode0 = 0;

    void init(Ctx& c) override {
        fill.build(c, kTunnelsGLSL, "tunnels");
        for (int m = 0; m < 3; m++)
            for (int i = 0; i < 3; i++) rate[m][i] = c.exe->f32(kRates + 4 * uint32_t(3 * m + i));
        mode0 = c.exe->s32(kMode);
    }

    // a_i = the sum over the mode intervals of rate[mode][i] times their length (t70 units); event 401 steps the
    // mode (mod 3) from the capture's tau.
    void angles(const Ctx& c, double t, double a[3]) const {
        int mode = mode0;
        double from = c.origin;
        a[0] = a[1] = a[2] = 0;
        auto add = [&](double to) {
            for (int i = 0; i < 3; i++) a[i] += rate[mode % 3][i] * (c.t70(to) - c.t70(from));
        };
        for (int n = 0;; n++) {
            double at = c.eventTime(401, n);
            if (!std::isfinite(at)) break;
            at -= n < 4 ? kLag401[n] : 0.0;
            if (at >= t) break;
            add(at);
            mode = (mode + 1) % 3;
            from = at;
        }
        add(t);
    }

    // B: the pulse 0.5 + 0.3 sin(0.07 T4) of the previous frame's clock until event 403, then a ramp from 0 to 1.
    double brightness(const Ctx& c, double t, double t4) const {
        if (t < c.eventTime(403)) return std::sin(std::max(0.0, t4 - kPulseLag) * 0.07) * 0.3 + 0.5;
        return std::min(1.0, 0.01 * (c.t70(t) - c.t70(c.eventTime(403) - kLag403)));
    }

    void render(Ctx& c, double t) override {
        double t4 = c.clock(t);
        double a[3];
        angles(c, t, a);
        const Shader& sh = fill.begin(c, t);
        sh.set("P", float(100.0 * std::sin(t4 * 0.03)), float(100.0 * std::sin(t4 * 0.043)),
               float(100.0 * std::sin(t4 * 0.053)));
        sh.set("ang", gridfill::phase(a[0]), gridfill::phase(a[1]), gridfill::phase(a[2]));
        sh.set("bright", float(brightness(c, t, t4)));
        fill.draw();
        gridfill::drawCaption(c, {10, 45, 170, 40, "tunnels_meet_more_tunnels", 20, 70, "glyph4", 40, 0});
    }
};
}  // namespace

REGISTER_SCENE(TunnelsScene, "tunnels");
