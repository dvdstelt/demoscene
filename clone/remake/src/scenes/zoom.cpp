// Part 5 (0x403530, 20:00..24:00): a 2D rotating zoom of the grid texture (table 2 from event 5) whose zoom varies
// over the screen like a plasma (two products of sines), brighter where it is zoomed out; the rotation reverses at
// each event 501 (21:00, 22:00, 23:00); "get them clones" in a black box and glyph 2. clone/re/notes-grid.md
// sections 6 and 8.
//
// Evaluated exactly instead of the original's shortcuts: the zoom field Z, the rotated coordinates and the light per
// pixel (grid.h) instead of at the 41 x 21 grid entries; where Z is small the original's 8 x 8 blocks interpolate
// across the texture's wrap (block noise), here the texture is filtered (mipmaps). T and R as closed forms of the
// part's clock.
//
// Frame-rate quirk kept (the capture's): R integrates the new direction from the timer of the frame before the event
// frame, 10.3 / 12.5 / 14.8 ms before 21:00 / 22:00 / 23:00 in the capture (event_lag.py), so the angle after each
// flip is that of the capture (0.34 degrees from exact flips at the row starts after the three).
#include "scenes/grid.h"

#include <cmath>

#include "exe.h"

namespace {
constexpr double kLag501[] = {10.27e-3, 12.52e-3, 14.77e-3};  // s: the capture's tau before each event 501

const char* kZoomGLSL = R"(
uniform vec4 ph;       // 4T, 6T, 7T, 8T (mod 2 pi)
uniform float s5;      // sin 5T
uniform vec2 rot;      // cos R, sin R
vec3 gridMap(vec2 p) {
    float c = p.x / 8.0, r = (p.y - 20.0) / 8.0;                          // grid column and row, continuous
    float S = sin(0.296 * r + ph.z) * (130.0 * sin(0.141 * c + ph.x)) + sin(0.176 * r + ph.w) * (55.0 * sin(0.261 * c + ph.y));
    float Z = S * s5 + 200.0;                                             // 15..385
    vec2 q = vec2(p.x - 160.0, p.y - 100.0) * (float(0x83400) / 8.0 / 256.0);   // 262.5 texels per pixel at Z = 1
    float u = (rot.x * q.x - rot.y * q.y) / Z + 128.0;
    float v = (rot.y * q.x + rot.x * q.y) / Z + 128.0;
    float L = (Z * 32768.0 * 0.0025 + 18432.0) / 256.0;                    // 72 + 0.32 Z
    return vec3(u, v, L);
}
)";

struct ZoomScene : Scene {
    gridfill::Filler fill;
    double dir0 = 1.0;  // [0x421a54]: the direction's initial value; event 501 multiplies it by -1

    void init(Ctx& c) override {
        fill.build(c, kZoomGLSL, "zoom");
        dir0 = c.exe->f32(0x421a54);
    }

    // R = 0.01 * the integral of dir over the part's clock, dir flipping at the capture's tau of each event 501.
    double angle(const Ctx& c, double t) const {
        double r = 0, dir = dir0, from = c.origin;
        for (int n = 0;; n++) {
            double at = c.eventTime(501, n);
            if (!std::isfinite(at)) break;
            at -= n < 3 ? kLag501[n] : 0.0;
            if (at >= t) break;
            r += dir * (c.t70(at) - c.t70(from));
            dir = -dir;
            from = at;
        }
        r += dir * (c.t70(t) - c.t70(from));
        return 0.005 * 2.0 * r;
    }

    void render(Ctx& c, double t) override {
        double T = 0.005 * c.clock(t);  // T += dt * 0.005, R += dt * 0.005 * 2 * dir
        double R = angle(c, t);
        const Shader& sh = fill.begin(c, t);
        sh.set("ph", gridfill::phase(4 * T), gridfill::phase(6 * T), gridfill::phase(7 * T), gridfill::phase(8 * T));
        sh.set("s5", float(std::sin(5 * T)));
        sh.set("rot", float(std::cos(R)), float(std::sin(R)));
        fill.draw();
        gridfill::drawCaption(c, {180, 30, 130, 40, "get_them_clones", 215, 53, "glyph2", 140, 0});
    }
};
}  // namespace

REGISTER_SCENE(ZoomScene, "zoom");
