// Part 6 (0x406677, 14:18..18:00): 3000 blue dots on the lattice points of a cube's surface, blinking (short lives,
// respawned at random lattice points), turning and breathing (a sine scale per axis, 0x40600f) through the fisheye
// projection: a ball of dots, radially blurred every frame with the vertical direction swinging with sin(phase)
// (Dune's blur in mode 1: radial streaks, then horizontal ones); no trails. From 17:20 (event 601) the dots die out
// in about 1.2 s, then black. clone/re/notes-particles.md sections 3, 5, 7, 8.
//
// Which dots live is stepped at the capture's frames (particles_sim.h: its particles and random numbers); between
// two frames the dots of the frame drawn move on with the part's clock (rotation, breathing, the blur's level and
// phase are all linear in it), so the motion is smooth at any frame rate and the blinking stays on its frames.
// Evaluated exactly: the stamps and the blur as particles.h describes; the deformation of the integer lattice
// positions is that of lattice points anyway (notes 8).
#include <algorithm>
#include <cmath>
#include <string>

#include "exe.h"
#include "scenes/particles.h"

namespace {
using namespace particles;

const char* kDisplayFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D S;   // the stamps (full resolution)
uniform sampler2D E;   // the blur's echo
uniform vec3 pal;
void main() {
    float v = withEcho(texture(S, vga / vec2(320.0, 200.0)).r, E, vga);
    o = vec4(min(vec3(1.0), pal * v), 1.0);
}
)";

struct RingScene : Scene {
    Chain* chain = nullptr;
    Gl* gl = nullptr;
    Shader display;
    Kernel kernel;
    Vec3 pal;
    RadialBlur blur;
    Target stamps;
    int bw = 0, bh = 0;
    std::vector<Dot> dots;

    ~RingScene() override {
        blur.destroy();
        stamps.destroy();
    }

    void init(Ctx& c) override {
        chain = &c.shared<Chain>();
        gl = &c.shared<Gl>();
        display.build(kVgaVS, std::string(kEchoGLSL) + kDisplayFS, "ring display");
        kernel = stampKernel(1);
        pal = paletteFactors(95, 127, 274);  // 0x4065c5: 0x404a3e(95, 127, 274)
        blur.create();
    }

    // The dots of the current frame at dT t70 units after it: the lattice point scaled by
    // S = sum over the axes of (sin(i / 56 + g_axis) / 6 + 2 / 3), g_axis = g, 0.8 g, 1.3 g (0x40600f), turned by M.
    void collect(const Run<Ring>& r, double dT) {
        const Ring& p = r.part;
        const Pool& s = r.pool;
        double g = p.g + double(p.rate) * dT, g8 = g * 0.8, g13 = g * 1.3;
        Mat34 M = rotation(p.a + 0.002 * dT, p.b + 0.003 * dT, p.c + 0.004 * dT);
        float reach = kernel.halfWidth + 1;
        dots.clear();
        for (int i = 0; i < s.count; i++) {
            const Particle& q = s.p[size_t(i)];
            double x = std::trunc(q.x), y = std::trunc(q.y), z = std::trunc(q.z);
            double S = (std::sin(x / 56.0 + g) / 6.0 + 2.0 / 3.0) + (std::sin(y / 56.0 + g8) / 6.0 + 2.0 / 3.0) +
                       (std::sin(z / 56.0 + g13) / 6.0 + 2.0 / 3.0);
            Dot d;
            if (!project(M, x * S, y * S, z * S, 0.008, d)) continue;
            if (d.x < -reach || d.x > 320 + reach || d.y < 20 - reach || d.y > 180 + reach) continue;
            dots.push_back(d);
        }
    }

    void render(Ctx& c, double t) override {
        Run<Ring>& r = chain->ring;
        int w, h;
        bufferSize(c, w, h);
        if (w != bw || h != bh) {
            bw = w;
            bh = h;
            stamps.create(w, h, false, GL_R16F);
        }
        int k = std::min(r.frames.at(t), r.frames.count() - 2);
        r.stepTo(k);
        double dT = c.t70(t) - c.t70(r.frames.time[size_t(k)]);
        collect(r, dT);
        clearTarget(stamps);
        gl->sprites(stamps, dots, kernel);
        clearTarget(blur.in);
        gl->sprites(blur.in, dots, kernel);
        // 0x403df7 mode 1: level 0.85 + 0.14 sin(6 a), vertical factor sin(phase), phase += 0.04 dt per call
        blur.run(*gl, r.part.blurLevel(dT), float(std::sin(r.pool.phase + 0.04 * dT)));

        c.out->bind();
        setBlend(Blend::None);
        display.use();
        stamps.bindTex(0);
        blur.echo.bindTex(1);
        display.set("S", 0);
        display.set("E", 1);
        display.set("pal", pal);
        display.set("flipY", 1);
        display.set("rect", 0.0f, 20.0f, 320.0f, 160.0f);
        drawQuad();
    }
};
}  // namespace

REGISTER_SCENE(RingScene, "ring");
