// coretest: checks of the core library (src/jizz) against the Python models in jizz/re/models/core, and an example
// of how a part uses it. Not part of the demo; build it by hand next to a CMake build of the remake (the objects of
// the framework without main.cpp and the music player), for example inside the toolbox:
//
//   cmake -S jizz/remake -B jizz/remake/build-core -G Ninja -DJIZZ_PARTS=none && cmake --build jizz/remake/build-core
//   B=jizz/remake/build-core/CMakeFiles/jizz.dir/src
//   g++ -std=c++20 -O2 -Wall -Ijizz/remake/src -Ijizz/remake/third_party jizz/remake/tools/coretest.cpp
//       $B/{exe,adam,gl,render,demo,timeline}.cpp.o $B/jizz/{core,glow}.cpp.o
//       $(pkg-config --libs sdl3) -o jizz/remake/build-core/coretest        (one command line)
//
// Then compare with the Python models (tools/coretest.py, run from jizz/re):
//   coretest EXE rng                       random generator: outputs and states        -> coretest.py rng FILE
//   coretest EXE glow DIR                  the event-300 rectangles through the glow   -> coretest.py glow DIR
//                                          at 320x200: segments and trail buffer per step
//   coretest EXE seek                      glow state after stepping vs after seeking (max difference)
//   coretest EXE shots DIR T1,T2,...       the example scene below at 960x720
//   coretest EXE tables DIR                GLSL jzPolar (grid of 0x16f4f), jzSpotMap   -> coretest.py tables DIR
//   coretest EXE bench                     GPU time of the example scene and of a glow rebuild at 2560x1920
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "gl.h"
#include "jizz/core.h"
#include "music.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace jizz;

// The tool runs without the soundtrack (ctx.music = nullptr: ctx.timer(t) = t * 1193182); demo.cpp's references to
// the player are satisfied here instead of linking music.cpp / mxmplay.cpp.
double Music::timer(double t) const { return t * kPitHz; }
int Music::position(double) const { return 0; }
double Music::positionTime(int) const { return 0; }

// ---------------------------------------------------------------- 0x18228 (parts3d-a 4.5), as a part would port it
static std::vector<Quad> rectangles(const JizzExe& exe, uint32_t cfg) {
    int32_t v[9];
    for (int i = 0; i < 9; i++) v[i] = exe.s32(cfg + 4 * i);  // xA, xB, yA, yB, RA, RB, angA, angB, n
    float squash = exe.f32(0x18218), bias = exe.f32(0x18224);
    int n = v[8];
    std::vector<Quad> q(n + 1);
    for (int k = 0; k <= n; k++) {
        int i = n - k;
        auto lerp = [&](int a) { return (v[a] * i + v[a + 1] * (n - i)) / n; };  // idiv truncates like C++
        float X = float(lerp(0)), Y = float(lerp(2)), R = float(lerp(4));
        double th = double(float(lerp(6)) + bias) * (2 * 3.14159265358979323846 / 16384);
        float c = float(std::cos(th)) * R, s = float(std::sin(th)) * R;
        float xs[4] = {X + c, X - s, X - c, X + s}, ys[4] = {Y + s * squash, Y + c * squash, Y - s * squash, Y - c * squash};
        for (int j = 0; j < 4; j++) { q[k].x[j] = std::nearbyint(xs[j]); q[k].y[j] = std::nearbyint(ys[j]); }
    }
    return q;
}

static void select300(int row, int& first, int& last) {  // 0x17003 event 300 (0x1731a)
    if (row >= 7) { first = std::min(row, 14) - 7; last = 7; } else { first = 0; last = row; }
}

// ---------------------------------------------------------------- the example scene
// What a part does with the core: a polar-mapped background through the engine colour tables, an inset with its
// frame, the event-300 rectangles through the glow, and a timer fade from white.
struct CoreTest : Scene {
    Shader bg;
    Texture map;
    std::vector<Quad> quads;
    Glow glow{Glow::Overlay};
    RngTrack track;
    Rng start;

    void init(Ctx& c) override {
        static const char* fs = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D map;
uniform vec3 jzESF;
uniform float time;
void main() {
    vec2 vga = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec2 p = vga - vec2(160.0, 100.0);
    vec3 texel = jzTexPolar(map, p, vec2(time * 40.0, -time * 60.0)).rgb;
    float shade = jzSpot(length(p) * 1.2) / 255.0;
    o = vec4(jzColourTable(texel + shade, jzESF), 1.0);
}
)";
        bg.build(kFullscreenVS, std::string(kGLSL) + fs, "coretest bg");
        Image im;  // a 256x256 test map (stripes and rings)
        im.w = im.h = 256;
        im.rgba.resize(256 * 256 * 4);
        for (int j = 0; j < 256; j++)
            for (int i = 0; i < 256; i++) {
                uint8_t* p = &im.rgba[(j * 256 + i) * 4];
                p[0] = uint8_t(((i >> 4) & 1) ? 160 : 40);
                p[1] = uint8_t(((j >> 5) & 1) ? 120 : 30);
                p[2] = uint8_t(spotMap(float(i * 2), float(j)) * 0.5f + 40);
                p[3] = 255;
            }
        map.create(im, true, true);
        quads = rectangles(*c.exe, 0x181f4);
        start = Rng::afterTextures(*c.exe);
    }

    void render(Ctx& c, double t) override {
        const double rate = 25.5;  // event 300's frame rate in the capture
        // background: plain colour tables, inverted at t = 1 s and decaying 9 per frame at 35 fps
        bg.use();
        map.bind(0);
        bg.set("map", 0);
        bg.set("time", float(t));
        ColourTable ct{-40, 256, inversionDecay(t, 1.0, 35.0)};
        ct.set(bg);
        drawFullscreen();
        // an inset: the same background with other colour tables, inside a frame
        Inset in = Inset::fromOffset(20 * 640 + 130 * 2, 175, 110);
        in.scissor(c);
        ColourTable{-200, 0x1c8, 0}.set(bg);
        drawFullscreen();
        Inset::noScissor();
        insetFrame(c, in);
        // the rectangles: one step per frame at 25.5 fps; music row = 4 steps here
        track.start(start, 0);
        auto row = [](int k) { return (k / 4) & 15; };
        Glow::Source src;
        src.params = [](int) { GlowStep s; s.blur = true; s.shift = 1; return s; };
        src.draw = [&](Ctx& cc, int k) {
            int first, last;
            select300(row(k), first, last);
            Rng r = track.at(k, [&](int j) { int f, l; select300(row(j), f, l); return 4 * (l - f + 1); });
            std::vector<Segment> segs;
            jitterQuads(r, quads.data(), first, last, 7, segs);
            drawLines(cc, segs);
        };
        glow.advance(c, t * rate, 0, src);
        glow.fadeTowards(c, kWhite);
        timerFade(c, t, 0.0, 0x2dc6c0);  // from white, 1.23 s
    }
};

// ---------------------------------------------------------------- helpers
static void readRgba(const Target& t, std::vector<uint8_t>& out) {
    std::vector<uint8_t> px(size_t(t.w) * t.h * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    out.resize(px.size());
    for (int y = 0; y < t.h; y++)
        std::memcpy(&out[size_t(y) * t.w * 4], &px[size_t(t.h - 1 - y) * t.w * 4], size_t(t.w) * 4);
}

// The glow's blended result as a 320x200 picture of values (resolve through a fade towards (1,1,1) of a black
// page: out = v / 32).
static void glowValues(Ctx& c, const Glow& g, Target& tmp, std::vector<float>& v) {
    c.out = &tmp;
    tmp.bind();
    c.clear(Vec3(0, 0, 0));
    g.fadeTowards(c, kWhite);
    std::vector<float> px(size_t(tmp.w) * tmp.h * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, tmp.fbo);
    gl::ReadPixels(0, 0, tmp.w, tmp.h, GL_RGBA, GL_FLOAT, px.data());
    v.resize(size_t(tmp.w) * tmp.h);
    for (int y = 0; y < tmp.h; y++)
        for (int x = 0; x < tmp.w; x++) v[size_t(y) * tmp.w + x] = px[(size_t(tmp.h - 1 - y) * tmp.w + x) * 4] * 32.0f;
}

// The segments of step k: test 0 = event 300 (row = k / 4, jitter 7); test 1 = only the two axis-aligned key
// rectangles without jitter.
static std::vector<Segment> stepSegments(const std::vector<Quad>& quads, RngTrack& track, int k, int test) {
    int n = int(quads.size()) - 1;
    auto sel = [&](int j, int& f, int& l) { select300((j / 4) & 15, f, l); };
    std::vector<Segment> segs;
    if (test == 1) {
        Rng r;
        jitterQuads(r, quads.data(), 0, 0, 0, segs);
        jitterQuads(r, quads.data(), n, n, 0, segs);
        return segs;
    }
    int first, last;
    sel(k, first, last);
    Rng r = track.at(k, [&](int j) { int f, l; sel(j, f, l); return 4 * (l - f + 1); });
    jitterQuads(r, quads.data(), first, last, 7, segs);
    return segs;
}

static Glow::Source rectSource(const std::vector<Quad>& quads, RngTrack& track, int test) {
    Glow::Source src;
    src.params = [](int) { GlowStep s; s.blur = true; s.shift = 1; return s; };
    src.draw = [&quads, &track, test](Ctx& c, int k) { drawLines(c, stepSegments(quads, track, k, test)); };
    return src;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "coretest EXE rng|glow DIR|seek|shots DIR T,..\n"); return 1; }
    JizzExe exe;
    std::string err;
    if (!exe.load(argv[1], &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::string mode = argv[2];

    if (mode == "rng") {
        Rng g;
        for (int i = 0; i < 1000; i++) std::printf("%08x\n", g.next());
        auto pr = [](const char* n, Rng r) { std::printf("%s %08x %08x %08x\n", n, r.a, r.b, r.c); };
        pr("precalc", Rng::afterPrecalc());
        pr("seed", Rng::textureSeed(exe));
        pr("textures", Rng::afterTextures(exe));
        pr("ribbons", Rng::afterTextures(exe).skip(Rng::kRibbonCalls));
        auto p = precalcRandoms();
        std::printf("precalc[0] %08x precalc[79] %08x\n", p[0], p[79]);
        return 0;
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow("coretest", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext glc = win ? SDL_GL_CreateContext(win) : nullptr;
    if (!glc || !gl::load()) { std::fprintf(stderr, "no GL 3.3: %s\n", SDL_GetError()); return 1; }

    Ctx c;
    c.exe = &exe;
    {
        int w = 320, h = 200;
        if (mode == "shots") { w = 960; h = 720; }
        if (mode == "bench") { w = 2560; h = 1920; }
        c.w = w;
        c.h = h;
        Target frame;
        frame.create(w, h, true, GL_RGBA16F);
        c.out = &frame;
        std::vector<Quad> quads = rectangles(exe, 0x181f4);

        if (mode == "glow") {  // 60 steps of event 300 (row = step / 4), and 40 steps of only the key rectangles
            std::string dir = argc > 3 ? argv[3] : ".";
            FILE* fq = std::fopen((dir + "/quads.txt").c_str(), "w");
            for (const Quad& q : quads)
                std::fprintf(fq, "%g %g %g %g %g %g %g %g\n", q.x[0], q.y[0], q.x[1], q.y[1], q.x[2], q.y[2], q.x[3], q.y[3]);
            std::fclose(fq);
            for (int test = 0; test < 2; test++) {
                RngTrack track;
                track.start(Rng::afterTextures(exe), 0);
                Glow g(Glow::Overlay, 1.0f, false);
                Glow::Source src = rectSource(quads, track, test);
                FILE* f = std::fopen((dir + (test == 0 ? "/glow_rects.f32" : "/glow_keys.f32")).c_str(), "wb");
                FILE* fs = std::fopen((dir + (test == 0 ? "/segs_rects.txt" : "/segs_keys.txt")).c_str(), "w");
                std::vector<float> v;
                for (int k = 0; k < (test == 0 ? 60 : 40); k++) {
                    std::fprintf(fs, "%d", k);
                    for (const Segment& s : stepSegments(quads, track, k, test))
                        std::fprintf(fs, " %g,%g,%g,%g", s.x0, s.y0, s.x1, s.y1);
                    std::fprintf(fs, "\n");
                    g.advance(c, k, 0, src);  // the state after step k (fraction 0)
                    glowValues(c, g, frame, v);
                    std::fwrite(v.data(), 4, v.size(), f);
                }
                std::fclose(f);
                std::fclose(fs);
            }
            std::printf("wrote %s\n", dir.c_str());
        } else if (mode == "tables") {  // GLSL jzPolar on the grid of 0x16f4f, jzSpotMap on 256x256
            static const char* fs = R"(
in vec2 uv;
out vec4 o;
uniform int which;
uniform vec2 size;
void main() {
    vec2 ij = vec2(gl_FragCoord.x - 0.5, size.y - 0.5 - gl_FragCoord.y);   // column, row (top-down)
    if (which == 0) o = vec4(jzPolar(ij - vec2(299.5, 169.5)), 0.0, 1.0);
    else o = vec4(jzSpotMap(ij), 0.0, 0.0, 1.0);
}
)";
            Shader sh;
            sh.build(kFullscreenVS, std::string(kGLSL) + fs, "tables");
            std::string dir = argc > 3 ? argv[3] : ".";
            for (int which = 0; which < 2; which++) {
                int tw = which ? 256 : 600, th = which ? 256 : 340;
                Target t;
                t.create(tw, th, false, GL_RGBA32F);
                t.bind();
                sh.use();
                sh.set("which", which);
                sh.set("size", float(tw), float(th));
                drawFullscreen();
                std::vector<float> px(size_t(tw) * th * 4), out(px.size());
                gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
                gl::ReadPixels(0, 0, tw, th, GL_RGBA, GL_FLOAT, px.data());
                for (int y = 0; y < th; y++)
                    std::memcpy(&out[size_t(y) * tw * 4], &px[size_t(th - 1 - y) * tw * 4], size_t(tw) * 16);
                FILE* f = std::fopen((dir + (which ? "/spot.f32" : "/polar.f32")).c_str(), "wb");
                std::fwrite(out.data(), 4, out.size(), f);
                std::fclose(f);
                t.destroy();
            }
            std::printf("wrote %s/polar.f32, spot.f32\n", dir.c_str());
        } else if (mode == "seek") {
            RngTrack track;
            track.start(Rng::afterTextures(exe), 0);
            Glow::Source src = rectSource(quads, track, 0);
            src.params = [](int k) { GlowStep s; s.blur = true; s.shift = k < 30 ? 1 : 3; return s; };
            std::vector<float> a, b, d;
            for (double target : {37.4, 55.7}) {
                Glow g1, g2, g3;
                for (double s = 0; s <= target + 1e-9; s += 0.37) g1.advance(c, s, 0, src);  // playing
                g1.advance(c, target, 0, src);
                glowValues(c, g1, frame, a);
                g2.advance(c, target, 0, src);  // a seek from nothing
                glowValues(c, g2, frame, b);
                g3.advance(c, target + 40, 0, src);  // a seek backwards
                g3.advance(c, target, 0, src);
                glowValues(c, g3, frame, d);
                float m1 = 0, m2 = 0, mx = 0;
                for (size_t i = 0; i < a.size(); i++) {
                    m1 = std::max(m1, std::fabs(a[i] - b[i]));
                    m2 = std::max(m2, std::fabs(a[i] - d[i]));
                    mx = std::max(mx, a[i]);
                }
                std::printf("steps %.1f: max value %.3f; |playing - seek| max %.4f, |playing - seek back| max %.4f\n",
                            target, mx, m1, m2);
            }
        } else if (mode == "shots") {
            std::string dir = argc > 3 ? argv[3] : ".";
            std::vector<double> times;
            for (const char* p = argc > 4 ? argv[4] : "0.5"; *p;) {
                times.push_back(std::atof(p));
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
            CoreTest scene;
            scene.init(c);
            std::vector<uint8_t> px;
            for (double t : times) {
                c.out = &frame;
                frame.bind();
                c.clear();
                scene.render(c, t);
                readRgba(frame, px);
                char name[512];
                std::snprintf(name, sizeof name, "%s/core_%06.3f.png", dir.c_str(), t);
                stbi_write_png(name, frame.w, frame.h, 4, px.data(), frame.w * 4);
                std::printf("wrote %s\n", name);
            }
        }
        if (mode == "bench") {
            CoreTest scene;
            scene.init(c);
            auto frameMs = [&](double t) {
                uint64_t t0 = SDL_GetTicksNS();
                c.out = &frame;
                frame.bind();
                c.clear();
                scene.render(c, t);
                gl::Finish();
                return (SDL_GetTicksNS() - t0) * 1e-6;
            };
            frameMs(0.5);
            std::vector<double> ms;
            for (int i = 0; i < 300; i++) ms.push_back(frameMs(1.0 + i / 60.0));
            std::sort(ms.begin(), ms.end());
            std::printf("example scene at 2560x1920, playing: median %.2f ms, 95%% %.2f ms, max %.2f ms\n",
                        ms[ms.size() / 2], ms[ms.size() * 95 / 100], ms.back());
            ms.clear();
            for (int i = 0; i < 20; i++) ms.push_back(frameMs(i & 1 ? 2.0 : 4.0));  // seek: a rebuild each frame
            std::sort(ms.begin(), ms.end());
            std::printf("example scene at 2560x1920, seeking (rebuild of 10 steps): median %.2f ms\n", ms[ms.size() / 2]);
            for (int blur = 0; blur < 2; blur++) {  // one glow step per call
                RngTrack track;
                track.start(Rng::afterTextures(exe), 0);
                Glow::Source src = rectSource(quads, track, 0);
                src.params = [blur](int) { GlowStep st; st.blur = blur; st.shift = 1; return st; };
                Glow g;
                g.advance(c, 0, 0, src);
                gl::Finish();
                uint64_t t0 = SDL_GetTicksNS();
                for (int k = 1; k <= 50; k++) g.advance(c, k, 0, src);
                gl::Finish();
                std::printf("glow step at 2560x1920, blur %d: %.2f ms\n", blur, (SDL_GetTicksNS() - t0) * 1e-6 / 50);
            }
        }
        c.pool.clear();
        frame.destroy();
        c.shared_.clear();
    }
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
