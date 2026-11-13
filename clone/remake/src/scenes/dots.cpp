// Part 3 (0x405c45, 29:00..30:20): a blue cloud. Dots fly out of a cube around the centre (growing to 5000), and
// every frame the picture is blurred, magnified 2.048 times about the centre and darkened to 3/4 (0x404621,
// 0x405f8c), so every dot blows up into soft blobs; black bars over rows 0..19 and 180..199, a black box with
// "connect the dots" and glyph 3 fading in as the dot count grows. At 30:00 (event 301) the dots are cut away in
// about a second, the cloud fades and the text fades out again. clone/re/notes-particles.md sections 3, 6, 7, 8.
//
// The particles are the capture's (particles_sim.h). The magnify-and-darken is a per-frame process: the GPU keeps
// buffer 1 (the blurred frame) as a float target stepped at the capture's frames, rebuilt from the last 64 frames
// after a jump ((3/4)^64 is below float precision). Between two frames the picture continues smoothly: the frame's
// dots at the part's clock, plus the rest of buffer 1 magnified by 2.048^f and darkened by 0.75^f, the frame's own
// dots entering it with weight f (f = the fraction of the frame interval); at f = 1 that is the next frame without
// its births, deaths and blur.
//
// Evaluated exactly instead of the original's shortcuts: the 4-tap blur (no centre tap) as a Gaussian of the same
// variance (0.5 original pixel^2 per axis); the nearest-pixel magnification (integer maps) as a filtered one about
// its fixed point (159.52, 99.52); the truncations of >> 2 and 3 v >> 2 as their mean loss (3/8 level each); the
// text level max(0, 63 - 0.05 target) without truncation, blended between frames.
#include <algorithm>
#include <cmath>
#include <string>

#include "exe.h"
#include "scenes/particles.h"

namespace {
using namespace particles;

// 0x405f8c: source = r (dest - 160.5) + 160 (vertically 100.5, 100) with r = 32000 / 65536, in VGA coordinates.
constexpr double kMagnify = 32000.0 / 65536.0;
constexpr double kFixX = (160.0 - 160.5 * kMagnify) / (1.0 - kMagnify);  // the fixed point (159.52, 99.52)
constexpr double kFixY = (100.0 - 100.5 * kMagnify) / (1.0 - kMagnify);
constexpr float kLoss = 3.0f / 8.0f;  // mean loss of a truncating >> 2 / 3 v >> 2

// buffer 0 = 3/4 of buffer 1 magnified (filtered), minus the truncation's loss
const char* kMagnifyFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D H;
uniform vec2 fixpt;
uniform float scale;
void main() {
    vec2 src = fixpt + scale * (uv * vec2(320.0, 200.0) - fixpt);
    o = vec4(max(0.0, 0.75 * texture(H, src / vec2(320.0, 200.0)).r - 0.375), 0.0, 0.0, 0.0);
}
)";

// one axis of the Gaussian that stands for 0x404621 (the stamps saturate at 255 before it)
const char* kBlurFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D src;
uniform vec2 dir;      // one target pixel along the axis, in uv
uniform float sigma;   // in target pixels
uniform int radius, last;
void main() {
    float sum = 0.0, wsum = 0.0;
    for (int i = -radius; i <= radius; i++) {
        float w = exp(-0.5 * float(i * i) / (sigma * sigma));
        sum += w * min(255.0, texture(src, uv + dir * float(i)).r);
        wsum += w;
    }
    float v = sum / wsum;
    o = vec4(last != 0 ? max(0.0, v - 0.375) : v, 0.0, 0.0, 0.0);
}
)";

// the picture between frames k and k + 1 (see the top of the file), through the palette, rows 20..179
const char* kDisplayFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D H;    // buffer 1 at frame k
uniform sampler2D B;    // frame k's dots as they enter buffer 1 (blurred stamps)
uniform sampler2D F;    // frame k's dots at the current time (blurred stamps)
uniform vec2 fixpt;
uniform float f, scale, gain;
uniform vec3 pal;
void main() {
    vec2 src = (fixpt + scale * (vga - fixpt)) / vec2(320.0, 200.0);
    float hist = max(0.0, gain * (texture(H, src).r - (1.0 - f) * texture(B, src).r) - f * 0.375);
    float v = min(255.0, texture(F, vga / vec2(320.0, 200.0)).r + hist);
    o = vec4(min(vec3(1.0), pal * v), 1.0);
}
)";

struct DotsScene : Scene {
    static constexpr int kRebuild = 64;  // frames: (3/4)^64 is below float precision

    Chain* chain = nullptr;
    Gl* gl = nullptr;
    Shader magnify, blur, display;
    Kernel kernel, blurred;
    Vec3 pal;
    Target work, tmp, hbuf[2], born[2], fresh;  // R32F: buffer 0, blur pass, buffer 1 and the frame's blurred dots
    int frameOf[2] = {-1, -1}, bw = 0, bh = 0;
    std::vector<Dot> dots;

    ~DotsScene() override {
        for (Target* t : {&work, &tmp, &hbuf[0], &hbuf[1], &born[0], &born[1], &fresh}) t->destroy();
    }

    void init(Ctx& c) override {
        chain = &c.shared<Chain>();
        gl = &c.shared<Gl>();
        magnify.build(kFullscreenVS, kMagnifyFS, "dots magnify");
        blur.build(kFullscreenVS, kBlurFS, "dots blur");
        display.build(kVgaVS, kDisplayFS, "dots display");
        kernel = stampKernel(1);
        blurred = stampKernel(1, 0.5);       // the stamps after 0x404621: variance + 0.5 per axis
        pal = paletteFactors(127, 159, 255);  // 0x405b10: 0x404a3e(127, 159, 255)
    }

    void collect(const Run<Dots>& r, double dT) {
        const Dots& p = r.part;
        const Pool& s = r.pool;
        double reach = blurred.halfWidth + 1, rate = 1.0051 * dT;  // the angles' rates (0.003, 0.004, 0.005) * 1.0051
        Mat34 M = rotation(p.a + 0.003 * rate, p.b + 0.004 * rate, p.c + 0.005 * rate);
        dots.clear();
        for (int i = 0; i < s.count; i++) {
            const Particle& q = s.p[size_t(i)];
            Dot d;
            if (!project(M, q.x + 0.3 * q.vx * dT, q.y + 0.3 * q.vy * dT, q.z + 0.3 * q.vz * dT, 0.0, d)) continue;
            if (d.x < -reach || d.x > 320 + reach || d.y < 20 - reach || d.y > 180 + reach) continue;
            dots.push_back(d);
        }
    }

    void pass(const Shader& s, const Target& dst) {
        dst.bind();
        s.use();
        drawFullscreen();
    }

    // Buffer 1 of frame j into hbuf[dst] from hbuf[src] (src < 0: from black), and frame j's blurred dots.
    void step(Run<Dots>& r, int j, int dst, int src) {
        r.stepTo(j);
        collect(r, 0.0);
        gl::Disable(GL_BLEND);
        if (src < 0) {
            clearTarget(work);
        } else {
            magnify.use();
            hbuf[src].bindTex(0);
            magnify.set("H", 0);
            magnify.set("fixpt", float(kFixX), float(kFixY));
            magnify.set("scale", float(kMagnify));
            pass(magnify, work);
        }
        gl->sprites(work, dots, kernel);  // 0x404b84 adds the stamps to buffer 0
        gl::Disable(GL_BLEND);
        blur.use();
        blur.set("src", 0);
        double sx = std::sqrt(0.5) * bw / 320.0, sy = std::sqrt(0.5) * bh / 200.0;
        work.bindTex(0);
        blur.set("dir", 1.0f / float(bw), 0.0f);
        blur.set("sigma", float(sx));
        blur.set("radius", int(std::ceil(3.0 * sx)));
        blur.set("last", 0);
        pass(blur, tmp);
        tmp.bindTex(0);
        blur.set("dir", 0.0f, 1.0f / float(bh));
        blur.set("sigma", float(sy));
        blur.set("radius", int(std::ceil(3.0 * sy)));
        blur.set("last", 1);
        pass(blur, hbuf[dst]);
        clearTarget(born[dst]);
        gl->sprites(born[dst], dots, blurred);
        frameOf[dst] = j;
    }

    void prepare(Run<Dots>& r, int k) {
        int hi = frameOf[0] >= frameOf[1] ? 0 : 1;
        int have = frameOf[hi];
        if (have < 0 || have > k || k - have > kRebuild) {
            int start = std::max(0, k - kRebuild);
            frameOf[0] = frameOf[1] = -1;
            if (start == 0) {
                clearTarget(hbuf[0]);  // the set-up 0x404ad3 clears the buffers; frame 0 has no dots
                clearTarget(born[0]);
                frameOf[0] = 0;
            } else {
                step(r, start, 0, -1);
            }
            hi = 0;
            have = frameOf[0];
        }
        while (have < k) {
            step(r, have + 1, 1 - hi, hi);
            hi = 1 - hi;
            have++;
        }
    }

    // [0x4224fc] after frame k + 1 (the target only depends on itself, dt and event 301; 0x405c45)
    static int nextTarget(const Run<Dots>& r, int k) {
        int target = r.pool.target;
        if (k + 1 >= r.frames.count()) return target;
        bool end = r.part.end || r.eventAt(k + 1, 301);
        double dt = r.frames.dt[size_t(k + 1)];
        if (target < 5000 && !end) return std::min(5000, int(dt * 5.0 + target));
        if (target != 0 && end) return std::max(0, int(target - dt * 50.0));
        return target;
    }

    void render(Ctx& c, double t) override {
        Run<Dots>& r = chain->dots;
        int w, h;
        bufferSize(c, w, h);
        if (w != bw || h != bh) {
            bw = w;
            bh = h;
            for (Target* tg : {&work, &tmp, &hbuf[0], &hbuf[1], &born[0], &born[1], &fresh})
                tg->create(w, h, false, GL_R32F);
            frameOf[0] = frameOf[1] = -1;
        }
        int k = std::min(r.frames.at(t), r.frames.count() - 2);
        prepare(r, k);
        int a = frameOf[0] == k ? 0 : 1;
        r.stepTo(k);
        double f = r.fraction(k, t), dT = c.t70(t) - c.t70(r.frames.time[size_t(k)]);
        collect(r, dT);
        clearTarget(fresh);
        gl->sprites(fresh, dots, blurred);

        c.out->bind();
        setBlend(Blend::None);
        display.use();
        hbuf[a].bindTex(0);
        born[a].bindTex(1);
        fresh.bindTex(2);
        display.set("H", 0);
        display.set("B", 1);
        display.set("F", 2);
        display.set("fixpt", float(kFixX), float(kFixY));
        display.set("f", float(f));
        display.set("scale", float(std::pow(kMagnify, f)));
        display.set("gain", float(std::pow(0.75, f)));
        display.set("pal", pal);
        display.set("flipY", 1);
        display.set("rect", 0.0f, 20.0f, 320.0f, 160.0f);
        drawQuad();

        // 0x4043be: the black box; the caption and glyph 3 at max(0, 63 - 0.05 target) (0x404002 mode 1)
        double level0 = std::max(0.0, 63.0 - 0.05 * r.pool.target);
        double level1 = std::max(0.0, 63.0 - 0.05 * nextTarget(r, k));
        double level = level0 + (level1 - level0) * f;
        c.rect(162, 135, 148, 40, Vec3(0, 0, 0));
        c.overlay("connect_the_dots", 212, 160, level);
        c.overlay("glyph3", 212, 87, level);
        setBlend(Blend::None);
    }
};
}  // namespace

REGISTER_SCENE(DotsScene, "dots");
