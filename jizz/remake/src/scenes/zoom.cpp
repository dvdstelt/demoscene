// The radial zoom: part 0x177bf with the warp 0x16d43 (radius-indexed variant of 0x16dc8). map5 or map7 seen
// through the polar table, the radius bent by a travelling sine wave (the radius table [0x179f2] of 0x178ac), the
// angle and radius offsets [0x16cd1] moving with the timer; a white timer flash (0x17979 / 0x17999) at the part start
// and at each sync event, which also picks the texture and the zoom direction. jizz/re/notes-parts2d 5.
//
// Per pixel, at the output resolution (X, Y from the screen centre in original pixels, r = 1.3 |(X, Y)|):
//   col = atan2(Y, X) * 256 / pi + ao                                   (one texture width per half turn)
//   row = r + 60 (sin(phi + 0.008 (r + 1)) + 0.7) + ro                  (the radius table, continuous in r)
// read with a filtered, wrapping texture lookup whose footprint follows the mapping (no seam at the atan2 cut).
// Dropped: the integer radius index of the table, the nearest texel, the carry of the angle offset into the row
// (a one-texel step along a line through the centre) and the 32 steps of the fade.
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "jizz/core.h"
#include "jizz/textures.h"

namespace {
using namespace jizz;

constexpr int kPos = 0x0b00;  // 0b:00: event 2817 starts the part

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform vec2 size;     // target size in pixels
uniform float phi;     // phase of the wave (radians) at r = -1
uniform vec2 off;      // (ao, ro): angle and radius offsets in texels
uniform vec3 wave;     // (amplitude 60, bias 0.7, phase step per radius unit 0.008)
void main() {
    vec2 vga = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    vec2 p = vga - vec2(160.0, 100.0);
    vec2 pr = jzPolar(p);                                   // (angle col, radius r)
    float s = phi + wave.z * (pr.y + 1.0);
    vec2 t = vec2(pr.x + off.x, pr.y + wave.x * (sin(s) + wave.y) + off.y);
    // footprint: d(col, row) = diag(1, d row / d r) * J dp
    mat2 J = jzPolarJacobian(p);
    vec2 k = vec2(1.0, 1.0 + wave.x * wave.z * cos(s));
    vec2 dx = k * (J * dFdx(p)), dy = k * (J * dFdy(p));
    o = vec4(jzTex256Grad(tex, t, dx, dy).rgb, 1.0);
}
)";

class ZoomScene : public Scene {
public:
    void init(Ctx& c) override {
        c.shared<Textures>();
        jizz::prepare(c);
        const JizzExe& e = *c.exe;
        k07_ = e.f32(0x179e2);   // 0.7
        k60_ = e.f32(0x179ea);   // 60.0
        k008_ = e.f32(0x179ee);  // 0.008 per radius unit
        twoPi16k_ = e.f64(0x11596);
        start_ = c.positionTime(kPos);
        sh_.build(kFullscreenVS, std::string(kGLSL) + kFS, "zoom");
    }

    void render(Ctx& c, double t) override {
        const Textures& tx = c.shared<Textures>();
        // 0x1784e: the event picks texture, zoom direction and flash; the part sets 0xb00 after reacting
        double since = start_;
        int ev = c.event(t, &since);
        Tex tex = Tex::Map5;
        bool flip = false;
        uint32_t rate = 0x28c0d0;  // event 2817 (0b:00, the part start)
        if (ev == 0xc00) rate = 0x3d0900;                                                   // 0c:00, 0c:20
        else if (ev == 0xc10) { tex = Tex::Map7; flip = true; rate = 0x380310; }           // 0c:10, 0c:30
        else since = start_;

        const double tp = c.timer(t) - c.timer(start_);  // time() in PIT clocks
        // 0x178ac: p = (t * 0xfdf99209) >> 32 (signed), phase = p * 2pi/16384
        const double p = tp * (double(int32_t(0xfdf99209u)) / 4294967296.0);
        const double phi = std::fmod(p, 16384.0) * twoPi16k_;
        // 0x17902: e = 3t; ro = (e >> 15) & 255 (^ 255 when flipped), ao = (e >> 16) & 255
        double ro = std::fmod(3.0 * tp / 32768.0, 256.0), ao = std::fmod(3.0 * tp / 65536.0, 256.0);
        if (flip) ro = 255.0 - ro;

        c.out->bind();
        setBlend(Blend::None);
        sh_.use();
        tx.bind(tex, 0);
        sh_.set("tex", 0);
        sh_.set("size", float(c.out->w), float(c.out->h));
        sh_.set("phi", float(phi));
        sh_.set("off", float(ao), float(ro));
        sh_.set("wave", k60_, k07_, k008_);
        drawFullscreen();
        timerFade(c, t, since, rate, kWhite);  // 0x17999 (target white, 0x1131b at 0x177e4)
    }

private:
    Shader sh_;
    double start_ = 0, twoPi16k_ = 0;
    float k07_ = 0, k60_ = 0, k008_ = 0;
};

REGISTER_SCENE(ZoomScene, "zoom");
}  // namespace
