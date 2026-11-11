// 0:00 - 0:41  Intro, ported from the exe (part routine 0x14f04, credits 0x14a34, tick callback 0x1448c).
//
// Per frame the original:
//  1. sphere-maps neuron.raw: a 41x26 grid of view rays through a rotating camera (0x1cd4b matrix,
//     0x135dc rays), texel = 128 + 128 * ray.xy / |ray| (+ a scrolling offset), interpolated in 8x8 blocks;
//  2. ghosts it into the previous frame through intro.gst, which is 1/4 new + 3/4 previous;
//  3. until 27.7 s: draws opening1.raw ("statix / vic / acme") through another grid that blends a
//     rotating sine swirl (the many repeated copies) with a rippling identity mapping (the big names),
//     lit by moving rings, and adds it to the ghosted frame through intro.fad. Because the credits are
//     added to the feedback buffer, they settle at 4x the per-frame amount and saturate to white;
//  4. after 27.7 s: adds the "303" glyph strip, spreading the glyphs apart and blurring it horizontally
//     with a damped random spring;
//  5. the palette fades in from black over 18 s, multiplies by 3 on every half pattern, inverts at
//     27.7 s and flickers to white from 34.5 s.
// The remake evaluates both grids per pixel instead of per 8x8 block and keeps the colours in floating point.
#include <cmath>
#include <cstdint>

#include "../fx.h"

namespace {
constexpr float kTwoPi = 6.28318530718f;
// The intro starts when the position counter reaches 0x60 (order 1) plus the time its init takes
// (0.15 s fitted by correlating the camera against the capture)
const float kStart = musicTimeOfPos(0x60) + 0.15f;

// 1024-step sine table of the original (65536 * sin(2 pi i / 1024)), here continuous and normalised.
float tsin(float i) { return std::sin(i * kTwoPi / 1024.0f); }

// The 11 glyphs of 303b.raw: first columns at 0x52eb8, last columns at 0x52ee4 (int32; 0x14f04 adds 1 to the latter).
constexpr uint32_t kGlyphFirst = 0x52eb8, kGlyphLast = 0x52ee4;

// State advanced by the 70 Hz callbacks (0x10124 palette, 0x14a2c credits clock, 0x1448c intro).
struct IntroState {
    int ticks = 0;
    float F1 = 1.0f, F2 = 1.0f;  // credits: swirl amount, darkness
    float spread = 0.7f;         // 303 glyph spacing
    int stage = 0;
    int springX = 10000, springV = 50, countdown = 400;
    int level = 0, target = 0x4000, speed = 13;  // palette level, 0x4000 = normal
    bool invert = false;
    float flash = 1.0f;
    int oldPos = 0;
    uint32_t seed = 1;
    int stripW = 0;  // box width of the last drawn strip (0 = not drawn yet)
    float stripSpread = 0.7f;

    int rnd() {  // Watcom rand()
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }

    void tick(int pos) {
        // 0x10124: palette level towards its target, flash multiplier decays
        if (level == target) speed = 0;
        if (level < target) level = std::min(level + speed, target);
        if (level > target) level = std::max(level - speed, target);
        flash = (flash - 1.0f) * 0.98f + 1.0f;
        // 0x1448c
        if (pos > 0x260) {
            F1 += 0.005f;
            F2 += 0.001f;
        } else if (pos > 0x140) {
            if (pos > 0x160 && F1 > 0) F1 = std::max(0.0f, F1 - 0.002f);
            if (F2 > 0) F2 = std::max(0.0f, F2 - 0.002f);
        }
        F1 = std::min(F1, 1.0f);
        F2 = std::min(F2, 1.0f);
        if (stage != 0) {
            int s = springX + springV;
            springV -= s / 1200;
            springX = (s * 253) / 256;
            if (countdown) countdown--;
            if ((rnd() & 0xff) < 5) springV += (rnd() & 0x7f) + 50;
            spread += 0.00125f;
        }
        ticks++;
        if (pos != oldPos) {
            if ((oldPos & 0x3f) == 0) flash = 3.0f;
            oldPos = pos;
            if (pos >= 0x280 && stage == 0) {  // inverted palette flash, credits end
                stage = 1;
                invert = true;
                level = 0x8000;
                speed = 400;
            }
            if (pos >= 0x300 && (pos & 1) == 0 && stage < 3) {  // flicker towards white
                level = std::min(0x8000, (pos - 0x300) * 128 + 0x4000);
                speed = 0x9c4;
                invert = false;
            }
            if (pos >= 0x378 && stage == 1) stage = 2;
            if (pos >= 0x380 && stage == 2) stage = 3;
        }
        // main loop: the strip is redrawn only while its blur is narrower than 64 pixels
        int e = std::abs(springX / 128) + countdown / 8;
        if (stage != 0 && e < 64) {
            stripW = e + 1;
            stripSpread = spread;
        }
    }
};

const char* kFeedbackFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D accPrev, neuron, opening;
uniform vec3 R0, R1, R2;
uniform float focal, uOff, w, kAdd;
uniform int credits;
uniform vec2 rippleC;
uniform float ripplePh, P, Q, cosT, sinT, phX, phY, lightPh, F1, F2;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // original screen pixels, y down
    vec2 g = px / 8.0;                                     // original grid units
    vec3 dir = (g.x - 20.5) * R0 + (g.y - 13.0) * R1 + focal * R2;
    vec2 tc = vec2(128.0 + 128.0 * dir.x / length(dir) + uOff, 128.0 + 128.0 * dir.y / length(dir));
    vec3 col = texture(neuron, tc / 256.0, -0.75).rgb;   // the original point-samples; keep it crisp
    vec3 acc = mix(texture(accPrev, uv).rgb, col, w);
    if (credits != 0) {
        // identity mapping (screen x - 32) with a radial zoom ripple around a wandering centre
        float sc = 1.0 + 0.3 * sin(0.03 * length(px - rippleC) + ripplePh);
        vec2 base = vec2(128.0, 100.0) + (vec2(px.x - 32.0, px.y) - vec2(128.0, 100.0)) * sc;
        // rotating sine swirl that repeats the image across the screen
        float hx = sin(g.x * P / 30.0 + phX), gy = cos(g.y * Q / 30.0 - phY);
        vec2 sw = vec2(128.0 + 128.0 * hx * cosT + 100.0 * gy * sinT, 100.0 + 100.0 * gy * cosT - 128.0 * hx * sinT);
        vec2 t = clamp(mix(base, sw, F1), 0.0, 255.0);
        float tex = texture(opening, (t + 0.5) / 256.0).r * 255.0 * (1.0 - smoothstep(199.0, 200.0, t.y));
        // light: rings around the screen centre, darkened by F2
        float L = clamp(64.0 * (1.0 - F2) - 20.0 - 20.0 * sin(length(g - vec2(20.0, 13.0)) + lightPh), 0.0, 63.0);
        // (texel / 8) * light / 64, then / 4 into intro.fad, which adds ~9/255 per level
        float k = max(tex / 8.0 * L / 64.0 - 2.0, 0.0) / 4.0;
        acc += vec3(k * 9.0 / 255.0) * kAdd;
    }
    frag = vec4(clamp(acc, 0.0, 1.0), 1.0);
}
)";

const char* kPresentFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D acc, glyphs;
uniform vec4 glyph[11];   // x0, width, centre - 128
uniform float level, flashM, spread, blurW;
uniform int invert, strip;
float glyphAt(float x, float row) {
    for (int i = 10; i >= 0; i--) {   // later glyphs overwrite earlier ones, with a black column each side
        float wd = glyph[i].y;
        float left = floor(256.0 + glyph[i].z * spread + 0.5) - floor(wd * 0.5) - 96.0;
        if (x >= left - 1.0 && x < left + wd + 1.0) {
            if (x < left || x >= left + wd) return 0.0;
            // explicit level 0: inside this per-pixel loop the implicit mip level is undefined (it made frames
            // differ between runs); the strip is always magnified, so level 0 is what the GPU should pick anyway
            return textureLod(glyphs, vec2((glyph[i].x + x - left) / 256.0, (row + 0.5) / 38.0), 0.0).r;
        }
    }
    return 0.0;
}
void main() {
    vec3 p = texture(acc, uv).rgb;
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float row = px.y - 81.0;
    if (strip != 0 && row >= 0.0 && row < 37.0) {
        // horizontal box blur of width blurW, dimmed by (1 - w * 1000 / 65536) as in 0x1c91d
        float x0 = px.x - floor(blurW * 0.5), sum = 0.0;
        const int N = 32;
        for (int k = 0; k < N; k++) sum += glyphAt(x0 + (float(k) + 0.5) * blurW / float(N), floor(row));
        float v = sum / float(N) * (1.0 - blurW * 1000.0 / 65536.0) * 255.0 / 4.0;   // 0..63
        p += vec3(v * 9.0 / 255.0);
    }
    p = min(p, vec3(1.0));
    // palette level (0x1019c): < 64 fades to black, > 64 to white or (invert) to the negative
    vec3 q;
    if (level < 64.0) q = p * level / 64.0;
    else if (level < 65.0) q = p;
    else {
        float c = min(level - 65.0, 63.0);
        q = invert != 0 ? ((1.0 - p) * c + p * (63.0 - c)) / 64.0 : c / 63.0 + p * (63.0 - c) / 64.0;
    }
    frag = vec4(min(q * flashM, vec3(1.0)), 1.0);
}
)";

struct Intro : Scene {
    Shader feedback, present;
    Target acc[2];
    int cur = 0;
    float lastT = -1e9f;
    int glyphX0[11] = {}, glyphX1[11] = {};  // first column, last column + 1

    void init(Ctx& c) override {
        feedback.build(kFullscreenVS, kFeedbackFS, "intro feedback");
        present.build(kFullscreenVS, kPresentFS, "intro present");
        for (uint32_t i = 0; i < 11; i++) {
            glyphX0[i] = c.exe.s32(kGlyphFirst + 4 * i);
            glyphX1[i] = c.exe.s32(kGlyphLast + 4 * i) + 1;
        }
    }

    void render(Ctx& c, float, float) override {
        if (acc[0].w != c.w || acc[0].h != c.h)
            for (Target& a : acc) a.create(c.w, c.h, false);
        // create the textures before binding anything (creation binds to the active unit)
        const Texture& neuron = c.tex("neuron");
        const Texture& opening = c.tex("opening1");
        const Texture& glyphs = c.tex("303b");
        const float T = exeTime(c.T);
        // replay the 70 Hz callbacks from the start of the intro
        IntroState s;
        const double ticksF = std::max(0.0, double(T - kStart) * kTickHz);
        const int n = T >= kStart ? int(ticksF) + 1 : 0;
        for (int k = 0; k < n; k++) s.tick(musicPos(kStart + float(k / kTickHz)));
        // clocks, continuous between ticks: the camera counter runs +3 per tick, the credits clock +6
        const float cnt = float(3.0 * ticksF), C = float(25034.0 + 6.0 * ticksF);

        // Feedback weight: the original blends 1/4 new per rendered frame and does not wait for vsync.
        // Until 27.7 s each frame also pumps the vocal MP2 decoder four times, so it runs slowly and the
        // trails are long; afterwards it is fast and the picture crisp. The frame rates are matched by eye.
        const float fps0 = s.stage == 0 ? 40.0f : 140.0f;
        bool reset = std::fabs(c.T - lastT - c.dt) > 0.25f || lastT < -1e8f;
        lastT = c.T;
        float w = reset ? 1.0f : 1.0f - std::pow(0.75f, std::clamp(c.dt, 0.0f, 0.1f) * fps0);

        // camera (0x1cd4b): a = cnt / 7, b = cnt / 2 in 1024ths of a turn
        float a = cnt / 7.0f, b = cnt / 2.0f;
        float sa = tsin(a), ca = tsin(a + 256), sb = tsin(b), cb = tsin(b + 256);
        float focal = (3400.0f + 2048.0f * tsin(cnt / 2.0f + 0x156)) / 256.0f;
        float off = (15.0f * cnt + 16384.0f * tsin(cnt / 3.0f + 0x53e)) / 256.0f;

        Target& dst = acc[cur];
        const Target& prev = acc[cur ^ 1];
        dst.bind();
        setBlend(Blend::None);
        feedback.use();
        prev.bindTex(0);
        neuron.bind(1);
        opening.bind(2);
        feedback.set("accPrev", 0);
        feedback.set("neuron", 1);
        feedback.set("opening", 2);
        feedback.set("R0", cb, 0.0f, -sb);
        feedback.set("R1", -sb * sa, ca, -cb * sa);
        feedback.set("R2", sb * ca, sa, cb * ca);
        feedback.set("focal", focal);
        feedback.set("uOff", off);
        feedback.set("w", w);
        feedback.set("kAdd", w / 0.25f);
        feedback.set("credits", s.stage == 0 && T >= kStart ? 1 : 0);
        feedback.set("rippleC", 160.0f + 50.0f * std::sin(C * 0.001f + 24.0f), 100.0f + 50.0f * std::cos(C * 0.00134589502f + 34.0f));
        feedback.set("ripplePh", C / 172.0f);
        float theta = 7.0f * std::sin(C * 0.001f);
        feedback.set("cosT", std::cos(theta));
        feedback.set("sinT", std::sin(theta));
        feedback.set("P", 10.0f + 5.0f * std::sin(C * 0.000651890482f + 235.0f));
        feedback.set("Q", 10.0f + 5.0f * std::sin(C * 0.00069735007f + 1235.0f));
        feedback.set("phX", C * 0.0005f);
        feedback.set("phY", C * 0.000309214595f);
        feedback.set("lightPh", C * 0.00427350427f);
        feedback.set("F1", s.F1);
        feedback.set("F2", s.F2);
        drawFullscreen();

        c.out->bind();
        present.use();
        dst.bindTex(0);
        glyphs.bind(1);
        present.set("acc", 0);
        present.set("glyphs", 1);
        float g[44];
        for (int i = 0; i < 11; i++) {
            int wd = glyphX1[i] - glyphX0[i];
            g[i * 4 + 0] = float(glyphX0[i]);
            g[i * 4 + 1] = float(wd);
            g[i * 4 + 2] = float(wd / 2 + glyphX0[i] - 128);
            g[i * 4 + 3] = 0.0f;
        }
        gl::Uniform4fv(present.loc("glyph"), 11, g);
        present.set("level", T >= kStart ? float(s.level) / 256.0f : 0.0f);
        present.set("flashM", s.flash);
        present.set("invert", s.invert ? 1 : 0);
        present.set("strip", s.stripW > 0 ? 1 : 0);
        present.set("spread", s.stripSpread);
        present.set("blurW", float(std::max(s.stripW, 1)));
        drawFullscreen();
        cur ^= 1;
    }
};
REGISTER_SCENE(Intro, "intro");
}  // namespace
