// Part 1 (0x405527; both runs: 00:00..08:20 and 3a:00..3c:3f): a violet swarm of up to 5000 glowing dots strung
// along a slowly turning 3D Lissajous path, with trails (the feedback buffer 0x4045b4) and a projection whose
// "fisheye" term breathes; from 04:00 (event 101) 2000 dots and Dune's radial blur, its streak length pulsing; at
// 08:00 (102, 106) the dots vanish, the trails fade and the title fades in. The second run (no set-up) spawns 2000
// dots in one frame along a short stretch of the path (104), flashes white to pink to violet to black at 3c:00 (105)
// and fades in the credits at 3c:10 (107). clone/re/notes-particles.md sections 3, 4, 7, 8.
//
// The particles are the capture's (particles_sim.h: stepped at its frame times, its random numbers). The trails are
// a per-frame process, so the GPU keeps buffer 1 (the feedback) as a float target stepped at those frames too, and
// the picture between two frames is the blend of the buffers of both (the trails move at the capture's frame rate,
// smoothly). After a jump the buffer is rebuilt from the last 107 frames (what came before contributes < 1/1000).
//
// Evaluated exactly instead of the original's shortcuts (see particles.h for the stamps and the blur): the feedback
// (15 b1 + b0) >> 4 as (15 b1 + b0) / 16 minus its truncation's average loss (15/32 per frame), which keeps the
// trails ending (a level below 16 still drops by about one per frame) without the 8-bit steps; the flash and the
// title / credits fades without truncation.
#include <algorithm>
#include <cmath>
#include <string>

#include "clone/core.h"
#include "exe.h"
#include "scenes/particles.h"

namespace {
using namespace particles;

const char* kFeedbackFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D G;      // buffer 1 at the previous frame
uniform sampler2D S;      // the frame's stamps (buffer 0)
uniform sampler2D E;      // the blur's echo (320 x 200)
uniform int blurOn, flashOn;
uniform float flash;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float s = texelFetch(S, p, 0).r;
    float b = blurOn != 0 ? withEcho(s, E, uv * vec2(320.0, 200.0)) : min(255.0, s);
    float g = texelFetch(G, p, 0).r;
    float n = max(0.0, (15.0 * g + b) / 16.0 - 15.0 / 32.0);      // 0x4045b4 and its truncation's mean loss
    o = vec4(flashOn != 0 ? flash : n, 0.0, 0.0, 0.0);
}
)";

const char* kDisplayFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D G0, G1;   // buffer 1 at frames k and k + 1
uniform float f;
uniform vec3 pal;           // min(255, C v / 92) / 255 per channel
void main() {
    vec2 uv = vga / vec2(320.0, 200.0);
    float d = mix(texture(G0, uv).r, texture(G1, uv).r, f);
    o = vec4(min(vec3(1.0), pal * d), 1.0);
}
)";

struct SwarmScene : Scene {
    static constexpr int kRebuild = 107;  // frames: (15/16)^107 < 1/1000

    Chain* chain = nullptr;
    Gl* gl = nullptr;
    Shader feedback, display;
    Kernel kernel;
    Vec3 pal;
    RadialBlur blur;
    Target stamps, buf[2];  // the frame's stamps (R16F), buffer 1 at two frames (R32F)
    int frameOf[2] = {-1, -1}, run = -1, bw = 0, bh = 0;
    bool flashOf[2] = {false, false};  // the frame filled buffer 1 with the flash
    std::vector<Dot> dots;

    ~SwarmScene() override {
        blur.destroy();
        stamps.destroy();
        buf[0].destroy();
        buf[1].destroy();
    }

    void init(Ctx& c) override {
        chain = &c.shared<Chain>();
        gl = &c.shared<Gl>();
        feedback.build(kFullscreenVS, std::string(kEchoGLSL) + kFeedbackFS, "swarm feedback");
        display.build(kVgaVS, kDisplayFS, "swarm display");
        kernel = stampKernel(0);
        pal = paletteFactors(127, 95, 274);  // 0x4053f2: 0x404a3e(127, 95, 274) for all eight band palettes
        blur.create();
    }

    // The dots of the run's current frame, moved on by dT t70 units (0: the frame itself).
    void collect(const Run<Swarm>& r, double dT) {
        const Swarm& p = r.part;
        const Pool& s = r.pool;
        Mat34 M = rotation(p.a + 0.001 * dT, p.b + 0.002 * dT, p.c + 0.003 * dT);
        double k = p.fisheyeK(dT);
        float reach = kernel.halfWidth + 1;
        dots.clear();
        for (int i = 0; i < s.count; i++) {
            const Particle& q = s.p[size_t(i)];
            Dot d;
            if (!project(M, q.x + 0.3 * q.vx * dT, q.y + 0.3 * q.vy * dT, q.z + 0.3 * q.vz * dT, k, d)) continue;
            if (d.x < -reach || d.x > 320 + reach || d.y < 20 - reach || d.y > 180 + reach) continue;
            dots.push_back(d);
        }
    }

    // Buffer 1 at frame j into buf[dst] from buf[src] (src < 0: from black).
    void step(Run<Swarm>& r, int j, int dst, int src) {
        r.stepTo(j);
        collect(r, 0.0);
        clearTarget(stamps);
        gl->sprites(stamps, dots, kernel);
        bool blurOn = r.part.mode == 1;
        if (blurOn) {
            clearTarget(blur.in);
            gl->sprites(blur.in, dots, kernel);
            blur.run(*gl, r.part.blurLevel(0.0), 1.0f);  // mode 0: the vertical factor is 1
        }
        if (src < 0) {
            clearTarget(buf[1 - dst]);
            src = 1 - dst;
        }
        gl::Disable(GL_BLEND);
        buf[dst].bind();
        feedback.use();
        buf[src].bindTex(0);
        stamps.bindTex(1);
        blur.echo.bindTex(2);
        feedback.set("G", 0);
        feedback.set("S", 1);
        feedback.set("E", 2);
        feedback.set("blurOn", blurOn ? 1 : 0);
        feedback.set("flashOn", r.part.flashOn ? 1 : 0);
        feedback.set("flash", r.part.flash);
        drawFullscreen();
        frameOf[dst] = j;
        flashOf[dst] = r.part.flashOn;
    }

    // Makes buffer 1 of frames k and k + 1 available: stepped on from the frames held, or rebuilt.
    void prepare(Run<Swarm>& r, int runIndex, int k) {
        int want = k + 1;
        int hi = frameOf[0] >= frameOf[1] ? 0 : 1;
        int have = frameOf[hi];
        bool ok = run == runIndex && have >= 0 && have <= want && want - have <= kRebuild &&
                  (have < want || frameOf[1 - hi] == k);
        if (!ok) {
            int start = std::max(0, want - kRebuild);
            run = runIndex;
            frameOf[0] = frameOf[1] = -1;
            if (start == 0) {
                clearTarget(buf[0]);  // frame 0: buffer 1 is black at the start of both runs
                frameOf[0] = 0;
                flashOf[0] = false;
            } else {
                step(r, start, 0, -1);  // from black: older frames would contribute < 1/1000
            }
            hi = 0;
            have = frameOf[0];
        }
        while (have < want) {
            step(r, have + 1, 1 - hi, hi);
            hi = 1 - hi;
            have++;
        }
    }

    void render(Ctx& c, double t) override {
        bool again = c.cue && c.cue->event == 111;
        Run<Swarm>& r = again ? chain->swarm2 : chain->swarm1;
        int w, h;
        bufferSize(c, w, h);
        if (w != bw || h != bh) {
            bw = w;
            bh = h;
            stamps.create(w, h, false, GL_R16F);
            buf[0].create(w, h, false, GL_R32F);
            buf[1].create(w, h, false, GL_R32F);
            frameOf[0] = frameOf[1] = -1;
        }
        int k = std::min(r.frames.at(t), r.frames.count() - 2);
        prepare(r, again ? 1 : 0, k);
        int a = frameOf[0] == k ? 0 : 1;

        c.out->bind();
        setBlend(Blend::None);
        display.use();
        buf[a].bindTex(0);
        buf[1 - a].bindTex(1);
        display.set("G0", 0);
        display.set("G1", 1);
        // the flash starts on its frame (a sudden change: not blended in from the frame before)
        display.set("f", flashOf[1 - a] && !flashOf[a] ? 0.0f : float(r.fraction(k, t)));
        display.set("pal", pal);
        display.set("flipY", 1);
        display.set("rect", 0.0f, 20.0f, 320.0f, 160.0f);  // the expansion writes rows 20..179
        drawQuad();

        // the title (0x41fc5c) from event 106 until 111 and the credits (0x41ffe9) from 107, at their blend levels
        if (t >= c.eventTime(106) && t < c.eventTime(111))
            c.overlay("title_clone_meets_clone", 141, 104, cmc::titleLevel(c, t));
        if (t >= c.eventTime(107)) c.overlay("credits_aap_echo_simon", 141, 74, cmc::creditsLevel(c, t));
        setBlend(Blend::None);
    }
};
}  // namespace

REGISTER_SCENE(SwarmScene, "swarm");
