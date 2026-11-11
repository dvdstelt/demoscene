// 0:41 - 0:55  Aap's stripes, ported from the exe (main script 0x30eb1, 0x2d458 background,
// 0x2b110 line burst, 0x2bdb4 web tunnel, 0x29d1c inset, callbacks 0x2d354 and 0x2e798).
//
// Every 8 rows: a new background mapping of wipeout.raw (egg-shaped rings 3/4, S-bands 1/4) with a random
// centre, a white palette flash, and a coin flip whether the line layer lightens or darkens. The line
// layer alternates every 32 rows between a 12-fold burst of 4 moving chords with zoom feedback and a
// receding tunnel of random spider webs. A palette-inverted thumbnail of the frame sits in a window that
// moves every 16 rows. The palette also gets random x1.2 / x0.9 kicks every 2 rows and a negative strobe
// in the last 8 rows of every 128.
#include <cmath>

#include "../exefx.h"

namespace {
constexpr float kPi = 3.14159265359f;

const char* kBgFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D tex, lines;
uniform int mode, darken;
uniform vec2 cxy;
uniform float scroll, seed;
float gnoise(vec2 px) {   // new random texture row per 8x8 grid point and frame, interpolated
    vec2 g = px / 8.0, i = floor(g), f = g - i;
    float a = hash12(i + seed), b = hash12(i + vec2(1.0, 0.0) + seed);
    float c = hash12(i + vec2(0.0, 1.0) + seed), d = hash12(i + vec2(1.0, 1.0) + seed);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float x = px.x - 160.0, y = px.y - 100.0;
    float n = gnoise(px) * 16.0;
    vec2 tc;   // texture column (stripe), row (brightness)
    if (mode != 0) {
        float d2 = (x - cxy.x) * (x - cxy.x) + (y + cxy.y) * (y + cxy.y);
        tc = vec2(d2 * (sqrt(x + 200.0) + 1.0) / 7680.0 + scroll, 240.0 + n);
    } else {
        tc = vec2(atan(0.03 * (y - 28.0 * atan(0.03 * x))) * 100.0 + scroll, 240.0 + n - 6.0 * y * y / 256.0);
    }
    tc.y = clamp(tc.y, 0.5, 255.5);
    vec3 col = texture(tex, tc / 256.0).rgb;
    // wipeout.fad: lighten = rows 32 + r (towards white), darken = rows 31 - r (towards black)
    float r = min(2.0 * texture(lines, uv).r, 31.0);
    col = darken != 0 ? col * (31.0 - r) / 32.0 : mix(col, vec3(1.0), r / 31.0);
    frag = vec4(col, 1.0);
}
)";

const char* kZoomFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D src;
void main() {   // 0x2b110: zoom in x1/0.9 about the centre and fade x0.75 (trunc), per frame
    float v = texture(src, (uv - 0.5) * 0.9 + 0.5).r;
    frag = vec4(max(0.75 * v - 0.25, 0.0), 0.0, 0.0, 1.0);
}
)";

struct Web {
    std::vector<int> ring;  // ring[1..n]
    struct Seg { float a; int r1, r2; };
    std::vector<Seg> seg;
};

// 0x2bdb4 init: random ring radii, spokes and rungs
Web makeWeb(ExeRand& R) {
    Web w;
    w.ring.assign(64, 0);
    int n = 0;
    for (int r = 10; r < 200 && n < 40; r += 12) {
        int b = r + (R.next() & 7);
        if ((R.next() & 31) < 25) w.ring[size_t(++n)] = b;
        if ((R.next() & 31) < 5) { w.ring[size_t(n + 1)] = b + (R.next() & 7) - 3; n++; }
        if ((R.next() & 31) < 5) { b += (R.next() & 7) - 3; w.ring[size_t(n + 1)] = b; n++; }
    }
    w.ring.resize(size_t(n + 1));
    const int last = w.ring[size_t(n)] - 1;
    for (int j = 0; j < 15 && w.seg.size() <= 500; j++) {
        w.seg.push_back({float(int(j * 2.0 * kPi * 1024.0 / 15.0) + (R.next() & 0xff)), w.ring[1] - 1, last});
        for (int p = 8; p > 5; p--)
            if ((R.next() & 31) < p) {
                int ri = std::min(2 + (R.next() & 3), n);
                w.seg.push_back({w.seg.back().a + 60.0f, w.ring[size_t(ri)] - 1, last});
            }
    }
    for (int k = 0; k < 8 && w.seg.size() <= 500; k++) {
        int m = int(w.seg.size());
        float a0 = w.seg[size_t(R.next() % m)].a, a1 = w.seg[size_t((R.next() % 4) % m)].a, step = 50;
        if (a1 < a0) a1 += 6433.0f;
        int q = R.next() % std::max(1, n - 1);
        if (k == 0) { a0 = 0; a1 = 6433.0f; step = 100; q = 1; }
        for (float a = a0; a < a1 && w.seg.size() <= 500; a += step)
            w.seg.push_back({a, w.ring[size_t(q + 1)] - 1, w.ring[size_t(std::min(q + 2, n))] - 1});
    }
    return w;
}

// State advanced by the 70 Hz callbacks, replayed from the start of the part.
struct AapState {
    ExePalette pal;
    ExeRand rnd{303};
    int last = -1, last2 = -1;
    int winx = 10, winy = 10;
    bool darken = false;
    float cx = 100, cy = 30;
    int mode = 1;
    int t58088 = 354;
    double tL = 8763008.0, sL = 1.0;  // 0x2b0ec
    int Tweb = 0;                      // 0x2bd94
    AapState() {
        pal.level = 0x8000;  // white while loading, then the wipeout palette in 16 ticks
        pal.target = 0x4000;
        pal.speed = 1000;
    }
    void tick(int pos) {
        pal.tick();
        tL += 5;  // 0x2b0ec
        sL += 4;
        if (pos != last) {  // 0x2d354
            last = pos;
            if ((pos & 0xf) == 0) {
                if ((pos & 0x1f) == 0) {
                    winx = rnd.next() % 200 + 5;
                    winy = rnd.next() % 120 + 5;
                }
                if ((rnd.next() & 15) < 8) darken = !darken;
                pal.flash(600);
                t58088 += 50;
                cx = float(rnd.next() % 320 - 160);
                cy = float(rnd.next() % 200 - 100);
                mode = rnd.next() & 3;
            }
        }
        t58088 += 1;
        Tweb = (Tweb + 4) % 1024;  // 0x2bd94
        if (pos != last2) {        // 0x2e798
            last2 = pos;
            if ((pos & 3) == 0 && (rnd.next() & 15) < 7) pal.mult = 1.2f;
            if ((pos & 3) == 0 && (rnd.next() & 15) < 2) pal.mult = 0.9f;
            if ((pos & 0xff) > 0xef) {
                pal.flash(1000);
                pal.invert = true;
            } else {
                pal.invert = false;
            }
        }
    }
};

struct Aap : Scene {
    Shader bg, zoom;
    Target lines[2];
    int cur = 0;
    StepClock clock;
    LineBatch batch;
    Web web;

    void init(Ctx&) override {
        bg.build(kFullscreenVS, kBgFS, "aap bg");
        zoom.build(kFullscreenVS, kZoomFS, "aap zoom");
        ExeRand r(1997);
        web = makeWeb(r);
    }

    void drawChords(const AapState& s) {  // 0x2b110 + 0x2afb4: 4 chords, 12-fold rotated in pixel space
        double t = s.tL;
        for (int k = 0; k < 4; k++, t += s.sL) {
            float x1 = std::trunc(float(160.0 * std::sin(0.001 * t + 23.0) + 160.0));
            float y1 = std::trunc(float(100.0 * std::sin(0.00251 * t + 72.0) + 100.0));
            float x2 = std::trunc(float(160.0 * std::sin(172.0 - 0.00253 * t) + 160.0));
            float y2 = std::trunc(float(100.0 - 100.0 * std::sin(0.00351 * t + 42.0)));
            for (int r = 0; r < 12; r++) {
                float a = float(r) * 2.0f * kPi / 12.0f, ca = std::cos(a), sa = std::sin(a);
                auto rot = [&](float x, float y, float& ox, float& oy) {
                    float X = x - 160, Y = y - 100;
                    ox = 160 + X * ca - Y * sa;
                    oy = 100 + Y * ca + X * sa;
                };
                float ax, ay, bx, by;
                rot(x1, y1, ax, ay);
                rot(x2, y2, bx, by);
                batch.add(ax, ay, bx, by, 4.0f);
            }
        }
    }

    void drawWebs(const AapState& s) {  // 0x2bdb4 frame: receding layers of the same web
        const float T = float(s.Tweb);
        for (int i = -100; i < 100; i++) {
            float z = 1.7f * float(i) + 0.01f * T;
            if (z <= 0.07f || z >= 15.0f) continue;
            float sc = 1.0f / z;
            float cx = 160.0f + 50.0f * std::sin(T / 748.0f + 5342.0f) / z;
            float cy = 100.0f + 50.0f * std::sin(T / 638.0f + 342.0f) / z;
            float rot = 4.0f * std::sin(10.0f * float(i) + 0.001f * T);
            for (size_t k = 1; k < web.ring.size(); k++) batch.circle(cx, cy, float(web.ring[k]) * sc, 2.0f);
            for (const Web::Seg& g : web.seg) {
                float a = g.a / 1024.0f + rot, sa = std::sin(a), ca = std::cos(a);
                batch.add(cx + sa * float(g.r1) * sc, cy + ca * float(g.r1) * sc, cx + sa * float(g.r2) * sc,
                          cy + ca * float(g.r2) * sc, 2.0f);
            }
        }
    }

    void render(Ctx& c, float, float) override {
        if (lines[0].w != c.w || lines[0].h != c.h)
            for (Target& t : lines) t.create(c.w, c.h, false, GL_R16F);
        const Texture& tex = c.tex("wipeout");
        const float T = exeTime(c.T), T0 = musicTimeOfPos(0x380);
        AapState s;
        const double ticksF = std::max(0.0, double(T - T0) * kTickHz);
        const int n = int(ticksF) + 1;
        for (int k = 0; k < n; k++) s.tick(musicPos(T0 + float(k / kTickHz)));
        s.pal.frac = float(ticksF - std::floor(ticksF));  // blend the fade towards the next tick
        const int pos = musicPos(T);
        const bool burst = (pos & 0x7f) < 0x40;

        // line layer, advanced at a fixed 70 Hz
        int steps = clock.steps(c);
        if (steps < 0) {
            for (Target& t : lines) {
                t.bind();
                gl::ClearColor(0, 0, 0, 0);
                gl::Clear(GL_COLOR_BUFFER_BIT);
            }
            steps = burst ? 6 : 1;
        }
        for (int k = 0; k < steps; k++) {
            Target& dst = lines[cur ^ 1];
            dst.bind();
            setBlend(Blend::None);
            if (burst) {
                zoom.use();
                lines[cur].bindTex(0);
                zoom.set("src", 0);
                drawFullscreen();
                drawChords(s);
            } else {
                gl::ClearColor(0, 0, 0, 0);
                gl::Clear(GL_COLOR_BUFFER_BIT);
                drawWebs(s);
            }
            batch.draw();
            cur ^= 1;
        }

        // background through the line layer
        Target& frame = c.scratch[0];
        frame.bind();
        setBlend(Blend::None);
        bg.use();
        tex.bind(0);
        lines[cur].bindTex(1);
        bg.set("tex", 0);
        bg.set("lines", 1);
        bg.set("mode", s.mode != 0 ? 1 : 0);
        bg.set("darken", s.darken ? 1 : 0);
        bg.set("cxy", s.cx, s.cy);
        bg.set("scroll", 100.0f * float(s.t58088) / 256.0f);
        bg.set("seed", float(int(ticksF)) * 0.37f);
        drawFullscreen();

        // inverted thumbnail window, palette
        c.out->bind();
        ExeComposite k;
        k.winx = float(s.winx);
        k.winy = float(s.winy);
        k.open = 1.0f;
        exeComposite(c, frame, k, s.pal);
    }
};
REGISTER_SCENE(Aap, "aap");
}  // namespace
