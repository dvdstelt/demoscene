// 1:12 - 1:32 and 3:11 - 4:13  Harl, web, paranoid, face floor (0x2e8e4 A/B/C), dance, icons, outro.
// Ported from notes-late.md. All seven scenes share one state machine (Late) that decides from the exe's
// music position which part to draw, so the cue boundaries in timeline.cpp do not have to be exact.
// Every frame the 70 Hz tick callbacks are replayed from the part start (cheap, deterministic, seek-safe).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../exefx.h"

namespace {
constexpr float kTick = float(1.0 / kTickHz);

// ------------------------------------------------------------------------------- shaders
// Video (160x96, shown 2x) at rows 2..193 of the 320x200 frame.
const char* kVideoFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D vid;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    if (px.y < 2.0 || px.y >= 194.0) { frag = vec4(0.0, 0.0, 0.0, 1.0); return; }
    frag = vec4(texBicubic(vid, vec2(px.x / 320.0, (px.y - 2.0) / 192.0)).rgb, 1.0);
}
)";

// Web layer B: six outline words (one 256x64 strip each), rotated with the camera roll, 4/z px per texel.
const char* kWordsFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D words, prev;
uniform vec4 W[6];      // z, W.x, W.y, visible
uniform vec2 cs;        // cos, sin of the roll
uniform float reset;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float X = px.x - 160.0, Y = px.y - 100.0;
    // texture gradients worked out here, in uniform control flow: inside the loop below the implicit ones are
    // undefined, which can pick a wrong mip level and make frames differ between runs
    vec2 dpx = dFdx(px), dpy = dFdy(px);
    float acc = 0.0;
    for (int i = 0; i < 6; i++) {
        if (W[i].w < 0.5) continue;
        float z = W[i].x;
        float u = 128.0 + 0.25 * z * (cs.x * X + cs.y * Y) - W[i].z / z;
        float v = 128.0 + 0.25 * z * (-cs.y * X + cs.x * Y) - W[i].y / z;
        u = clamp(u, 0.0, 255.0); v = clamp(v, 0.0, 255.0);
        if (v < 95.5 || v > 159.5) continue;
        vec2 gx = 0.25 * z * vec2(cs.x * dpx.x + cs.y * dpx.y, -cs.y * dpx.x + cs.x * dpx.y) / vec2(256.0, 384.0);
        vec2 gy = 0.25 * z * vec2(cs.x * dpy.x + cs.y * dpy.y, -cs.y * dpy.x + cs.x * dpy.y) / vec2(256.0, 384.0);
        acc += textureGrad(words, vec2(u / 256.0, (float(i) * 64.0 + v - 96.0) / 384.0), gx, gy).r * 255.0 / 8.0;
    }
    acc = min(acc, 255.0);
    float B = reset > 0.5 ? 0.0 : texture(prev, uv).r;
    frag = vec4(min(255.0, 1.7 * (min(127.0, 0.4 * B) + acc)), 0.0, 0.0, 1.0);
}
)";

// Web combine (0x1c3f4 with the web's L table) and palette. Word colours as in the capture (see notes 1).
const char* kWebFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D lines, trail;
uniform int pol;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float A = texture(lines, uv).r;
    float B = texture(trail, uv).r;
    float ink = clamp(A, 0.0, 63.0) / 63.0;
    vec3 grey = vec3(1.0 - ink);
    // word pixels: the code indexes the YIQ palette (0x40 + 16 min(j / 3, 11) + A / 4), which would give red and
    // magenta. The capture shows blue on white (yellow when negative) instead: the newest copy darkest navy
    // (0, 0, 81), older copies saturated (63, 57, 238) and then lighter. That is what is drawn here.
    float j = B / 8.0;
    float t = clamp((j - 1.0) / 18.0, 0.0, 1.0);
    vec3 word = t < 0.5 ? mix(vec3(0.78, 0.78, 1.0), vec3(0.25, 0.22, 0.93), t / 0.5)
                        : mix(vec3(0.25, 0.22, 0.93), vec3(0.0, 0.0, 0.32), (t - 0.5) / 0.5);
    word *= 1.0 - 0.5 * ink;
    float aa = max(fwidth(j), 0.05);
    vec3 col = mix(grey, word, smoothstep(1.0 - aa, 1.0 + aa, j));
    if (pol != 0) col = 1.0 - col;
    frag = vec4(exePalette(col), 1.0);
}
)";

// Shared 8x8-grid ripple of 0x14920 (evaluated per pixel): (u, v) in texels.
const char* kWarpGLSL = R"(
vec2 rippleUV(vec2 px, vec2 sc, vec2 c0, vec2 ctr, float A, float Fr, float Ph) {
    vec2 t0 = px * sc;
    float r = length(px - ctr);
    float k = 1.0 + A * sin(Fr * r + Ph);
    return (t0 - c0) * k + c0;
}
)";

// Paranoid: weird.raw rippled, lightened by the rippled face clip through pinfx3.fad.
const char* kParanoidFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D weird, vid;
uniform vec3 wp;   // cx, cy, phase (weird)
uniform float vph, vk;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec2 t = rippleUV(px, vec2(32.0 / 41.0, 25.0 / 26.0), vec2(128.0, 100.0), wp.xy, 0.3, 0.05, wp.z);
    vec3 col = texture(weird, t / 256.0).rgb;
    if (vk > 0.0) {
        vec2 q = rippleUV(px, vec2(20.0 / 41.0, 12.0 / 26.0), vec2(80.0, 48.0), vec2(160.0, 100.0), 0.07, 0.02, vph);
        float l = 0.0;
        if (q.x >= 0.0 && q.x < 160.0 && q.y >= 0.0 && q.y < 96.0) l = dot(textureLod(vid, q / vec2(160.0, 96.0), 0.0).rgb, vec3(0.299, 0.587, 0.114));  // explicit LOD: per-pixel branch
        l *= vk;
        col = mix(col, vec3(1.0), clamp(l * 256.0 / 8.0 / 31.0, 0.0, 1.0));
    }
    frag = vec4(col, 1.0);
}
)";

// Palette pass with an optional lightening layer (the face clip in 0x2e8e4 A/B).
const char* kPalPassFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D src, vid;
uniform float vph, vk;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec3 col = texture(src, uv).rgb;
    if (vk > 0.0) {
        vec2 q = rippleUV(px, vec2(20.0 / 41.0, 12.0 / 26.0), vec2(80.0, 48.0), vec2(160.0, 100.0), 0.07, 0.02, vph);
        float l = 0.0;
        if (q.x >= 0.0 && q.x < 160.0 && q.y >= 0.0 && q.y < 96.0) l = dot(textureLod(vid, q / vec2(160.0, 96.0), 0.0).rgb, vec3(0.299, 0.587, 0.114));  // explicit LOD: per-pixel branch
        col = mix(col, vec3(1.0), clamp(l * vk * 32.0 / 31.0, 0.0, 1.0));
    }
    frag = vec4(exePalette(col), 1.0);
}
)";

// Sheet / tiles of 0x2e8e4 B/C: textured, lit through the phong1 light map by the camera-space normal.
const char* kSheetVS = R"(
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
uniform mat4 mvp;
uniform vec3 r0, r1;
uniform vec2 uvs;
out vec2 tuv, env;
void main() {
    tuv = vec2(128.0 + uvs.x * p.x, 128.0 + uvs.y * p.y) / 256.0;
    env = (128.0 + 126.0 * vec2(dot(r0, n), dot(r1, n))) / 256.0;
    gl_Position = mvp * vec4(p, 1.0);
}
)";
const char* kSheetFS = R"(
in vec2 tuv, env;
out vec4 frag;
uniform sampler2D tex, phong;
uniform int clampUV;
void main() {
    vec2 t = clampUV != 0 ? clamp(tuv, 0.0, 1.0) : tuv;
    vec3 c = texture(tex, t).rgb;
    float l = texture(phong, env).r;
    frag = vec4(clamp(c * (0.55 + 1.1 * l) + vec3(0.45) * l * l * l, 0.0, 1.0), 1.0);
}
)";

// Icons (0x2a840): the 7x5 icon grid (p >> 3), an image line blurred horizontally (lighten), and the feedback.
const char* kIconsFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D img, prev;
uniform int cell[35];
uniform int iconX[8], iconW[8];
uniform int idx, n, row0, h, jx;
uniform float step, gain, finv, reset;
// explicit level 0: src() is called in per-pixel branches and loops, where the implicit mip level is undefined
// (it made the icons differ between runs); the image is always magnified, so level 0 is the right one
float src(float x, float y) { return textureLod(img, vec2(x / 320.0, y / 350.0), 0.0).r * 255.0; }
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float comp = 0.0;
    int cx = int(floor(px.x / 45.0)), cy = int(floor(px.y / 40.0));
    if (cx < 7 && cy < 5) {
        int k = cell[cy * 7 + cx];
        float off = floor(float(45 - iconW[k]) / 2.0);
        float lx = px.x - 45.0 * float(cx) - off;
        float wr = float((iconW[k] + 3) / 4 * 4);
        if (lx >= 0.0 && lx < wr) comp = floor(src(float(iconX[k]) + lx, 70.0 + px.y - 40.0 * float(cy)) / 8.0);
    }
    if (idx >= 0 && n > 0) {
        float y0 = float(100 - h / 2);
        float ly = px.y - y0;
        if (ly >= 0.0 && ly < float(h)) {
            float sum = 0.0;
            float x0 = px.x - float(n / 2) + float(jx);
            for (int k = 0; k < 64; k++) {
                if (k >= n) break;
                float x = x0 + float(k);
                // the exe reads across row ends; keep it within the line
                float yy = float(row0) + ly;
                if (x < 0.0) { x += 320.0; yy -= 1.0; }
                if (x >= 320.0) { x -= 320.0; yy += 1.0; }
                sum += src(x, clamp(yy, float(row0), float(row0 + h) - 0.5));
            }
            comp = max(comp, min(255.0, sum * step / 65536.0));
        }
    }
    float B = reset > 0.5 ? 0.0 : texture(prev, uv).r;
    float i = min(255.0, 0.65 * B + 0.5 * comp);
    float v = min(255.0, i * gain);
    frag = vec4(clamp(v * (1.0 - finv) + (255.0 - v) * finv, 0.0, 255.0), 0.0, 0.0, 1.0);
}
)";
const char* kGreyFS = R"(
in vec2 uv; out vec4 frag;
uniform sampler2D src;
void main() { frag = vec4(exePalette(vec3(texture(src, uv).r / 252.0)), 1.0); }
)";

void setInts(const Shader& s, const char* name, const int* v, int n) {
    for (int i = 0; i < n; i++) gl::Uniform1i(s.loc((std::string(name) + "[" + std::to_string(i) + "]").c_str()), v[i]);
}

// ------------------------------------------------------------------------------- small math
struct M3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 row(int r) const { return {m[r * 3], m[r * 3 + 1], m[r * 3 + 2]}; }
};
M3 mul(const M3& a, const M3& b) {
    M3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return r;
}
// 0x2721c: rotation about axis by asin(|axis|) (Rodrigues)
M3 rot3(Vec3 a) {
    M3 r;
    float s = a.len();
    if (s < 1e-9f) return r;
    Vec3 k = a * (1.0f / s);
    float c = std::sqrt(std::max(0.0f, 1.0f - s * s)), t = 1 - c;
    r.m[0] = c + k.x * k.x * t;       r.m[1] = k.x * k.y * t - k.z * s; r.m[2] = k.x * k.z * t + k.y * s;
    r.m[3] = k.y * k.x * t + k.z * s; r.m[4] = c + k.y * k.y * t;       r.m[5] = k.y * k.z * t - k.x * s;
    r.m[6] = k.z * k.x * t - k.y * s; r.m[7] = k.z * k.y * t + k.x * s; r.m[8] = c + k.z * k.z * t;
    return r;
}
// 0x14760
// The rotation from a to b (both orthonormal, b = a R), applied by the fraction f: smooth camera turns between
// two 70 Hz ticks.
M3 lerpRot(const M3& a, const M3& b, float f) {
    M3 at;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) at.m[i * 3 + j] = a.m[j * 3 + i];
    const M3 R = mul(at, b);
    // atan2 of sine and cosine: acos alone loses the tiny per-tick angles in single precision
    Vec3 axis(R.m[7] - R.m[5], R.m[2] - R.m[6], R.m[3] - R.m[1]);
    const float L = axis.len();
    const float ang = std::atan2(0.5f * L, (R.m[0] + R.m[4] + R.m[8] - 1.0f) * 0.5f);
    if (ang < 1e-7f || L < 1e-9f) return a;
    return mul(a, rot3(axis * (std::sin(ang * f) / L)));
}
M3 rot2(float a, float b) {
    M3 r;
    float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
    r.m[0] = ca * cb; r.m[1] = sa; r.m[2] = -ca * sb;
    r.m[3] = -sa * cb; r.m[4] = ca; r.m[5] = sa * sb;
    r.m[6] = sb; r.m[7] = 0; r.m[8] = cb;
    return r;
}

// camera_27444 state
struct Cam {
    M3 M;
    Vec3 pos, vel, tgt, look;
    float roll = 0, damp = 0.79f;
    void tick(int mode) {
        Vec3 d = look - pos;
        float dist = d.len();
        if (dist > 0.01f) {
            Vec3 n = d * (0.05f / dist);
            Vec3 axis = (mode & 1) ? n.cross(M.row(2)) : Vec3();
            M = mul(M, rot3(axis));
            if (roll != 0) M = mul(M, rot3(Vec3(0, 0, roll)));
        }
        float* p = &pos.x; float* v = &vel.x; const float* t = &tgt.x;
        for (int k = 0; k < ((mode & 2) ? 3 : 2); k++) {
            v[k] = damp * v[k] + 0.002f * (t[k] - p[k]);
            p[k] += v[k];
        }
    }
};

// ------------------------------------------------------------------------------- part times (music seconds)
float tHarl() { return exeTime(V(71.65f)); }
float tWeb() { return musicTimeOfPos(0x680); }
float tWebEnd() { return musicTimeOfPos(0x740); }
constexpr float kParanoidStart = 192.85f;  // tick-driven end of the plasma chain (capture: fade-in from VT 191.7)
float tParEnd() { return musicTimeOfPos(0xf00); }
float tB() { return musicTimeOfPos(0xf00) + 37 * kTick; }  // A ends when the camera reaches z 14000 (37 ticks)
float tC() { return musicTimeOfPos(0xf80); }
// C runs until the palette level reaches 0
float tDance() {
    static float t = -1;
    if (t > 0) return t;
    const float t0 = tC();
    ExePalette p;
    p.level = 0x8000; p.target = 0x4000; p.speed = 70; p.invert = true;
    int k = 0;
    while (p.level > 0 && k < 5000) {
        if (musicPos(t0 + k * kTick) > 0xf80 + 0x28) { p.target = 0; p.speed = 100; }
        p.tick();
        k++;
    }
    return t = t0 + k * kTick;
}
float tAlex() { return musicTimeOfPos(0x1060); }
float tIcons() { return musicTimeOfPos(0x1090); }
constexpr float kSongEnd = 244.87f;
int posAt(float m) { return musicPos(std::min(m, kSongEnd - 0.01f)); }

// ------------------------------------------------------------------------------- lyric tick (0x2a0d4)
struct LyricTicker {
    ExeRand rnd;
    LyricState lyr;
    int last = -1, jit[32] = {0};
    const std::array<int, 16>* J;  // jitter table (0x52f10)
    LyricTicker(uint32_t seed, const std::array<int, 16>& jitter) : rnd(seed), J(&jitter) {}
    void place(Ctx& c, const std::string& w, float cx, float cy) {
        lyr.word = w;
        lyr.width = LyricLayer::wordWidth(c, w);
        lyr.ty = rnd.next() % 144;
        lyr.tx = rnd.next() % std::max(1, 320 - lyr.width);
        if (cx >= 0) {  // the capture's realisation
            lyr.tx = int(cx * 320.0f) - lyr.width / 2;
            lyr.ty = std::clamp(int(cy * 200.0f) - 28, 0, 143);
        }
        lyr.zoom = false;
    }
    // returns true when the position changed
    bool tick(int pos) {
        bool ch = false;
        if (pos != last) {
            last = pos;
            ch = true;
            if ((pos & 3) == 0) {
                for (int& j : jit) j = int(float((*J)[size_t(rnd.next() & 15)]) * 0.7f);
                if ((rnd.next() & 15) == 0) lyr.finv = lyr.finv > 0.5f ? 0.0f : 1.0f;
                lyr.smear = rnd.next() % 12;
                lyr.zoom = false;
                if ((rnd.next() & 15) < 2) {
                    lyr.ty = rnd.next() % 144;
                    lyr.tx = rnd.next() % std::max(1, 320 - lyr.width);
                    lyr.zoom = true;
                }
            }
        }
        if (lyr.finv > 0) lyr.finv = std::max(0.0f, lyr.finv - 0.021f);
        lyr.jit[0] = jit[0]; lyr.jit[1] = jit[1]; lyr.jit[2] = jit[2]; lyr.jit[3] = jit[3]; lyr.jit[4] = jit[7];
        return ch;
    }
};
// Lyric placements read off the capture (centre, frame units; cx < 0: keep the random placement).
struct Place { float cx, cy; };
// harl: cycle mode, continuing after blobby with cycle word 7; the first six words
const Place kHarlPlaces[] = {{0.55f, 0.53f}, {0.73f, 0.80f}, {0.40f, 0.17f}, {0.63f, 0.26f}, {0.17f, 0.17f}, {0.60f, 0.29f}};
// dance: song mode; the capture shows song table entries 20.. (from order 31 row 58) at their music positions
constexpr int kDanceFirstSong = 20;
const Place kDancePlaces[] = {{0.53f, 0.85f}, {-1, 0},        {0.73f, 0.60f}, {0.20f, 0.27f}, {0.52f, 0.24f},
                              {0.62f, 0.70f}, {0.55f, 0.25f}, {0.46f, 0.50f}, {0.52f, 0.27f}};

// ------------------------------------------------------------------------------- the shared engine
struct Late {
    bool inited = false;
    Shader video, words, web, paranoid, palpass, sheet, icons, grey;
    LyricLayer harlLyr, danceLyr;
    StepClock harlClk, danceClk, wordClk, iconClk;
    LineBatch lines;
    Target lineT, wordT[2], iconT[2];
    int wordCur = 0, iconCur = 0;
    Texture wordTex;
    unsigned vao = 0, vbo = 0, ibo = 0;
    // web tile pattern
    std::vector<float> webV;      // x, y pairs
    std::vector<int> webE;        // edge pairs
    std::vector<int> webHeavy;    // per edge: unused (direction decided at draw time)
    float wordX[6] = {0};

    void init(Ctx& c) {
        if (inited) return;
        inited = true;
        video.build(kFullscreenVS, kVideoFS, "late video");
        words.build(kFullscreenVS, kWordsFS, "web words");
        web.build(kFullscreenVS, std::string(kExePaletteGLSL) + kWebFS, "web combine");
        paranoid.build(kFullscreenVS, std::string(kWarpGLSL) + kParanoidFS, "paranoid");
        palpass.build(kFullscreenVS, std::string(kExePaletteGLSL) + kWarpGLSL + kPalPassFS, "late palette");
        sheet.build(kSheetVS, kSheetFS, "sheet");
        icons.build(kFullscreenVS, kIconsFS, "icons");
        grey.build(kFullscreenVS, std::string(kExePaletteGLSL) + kGreyFS, "icons grey");
        harlLyr.init(c);
        danceLyr.init(c);
        buildWeb(c);
        buildWords(c);
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vbo);
        gl::GenBuffers(1, &ibo);
    }

    // 0x2c5c0 init: 9x9 mirror-symmetric tiles built from an octagon
    void buildWeb(Ctx& c) {
        ExeRand r(0x303303);
        int g[9][9];
        for (int i = 0; i < 5; i++)
            for (int k = 0; k < 5; k++) {
                int v = c.exe.u8(0x58078 + uint32_t(r.next() & 15));  // tile type table (16 bytes)
                g[i][k] = g[i][8 - k] = g[8 - i][k] = g[8 - i][8 - k] = v;
            }
        for (int i = 0; i < 6; i++) wordX[i] = float(r.next() - 0x4000) * 0.025f;
        wordX[0] = 0.0f;    // the capture's first word sits on the axis
        wordX[1] = -340.0f;  // the second word leaves to the right in the capture
        wordX[4] = -50.0f;   // the fifth word enters bottom left in the capture
        static const int oct[8][2] = {{-6, -2}, {-6, 2}, {-2, 6}, {2, 6}, {6, 2}, {6, -2}, {2, -6}, {-2, -6}};
        for (int b = 0; b < 9; b++)
            for (int j = 0; j < 9; j++) {
                float X = -48.0f + 12.0f * b, Y = -48.0f + 12.0f * j;
                int v = g[j][b];
                int base = int(webV.size() / 2);
                for (auto& o : oct) { webV.push_back(X + o[0]); webV.push_back(Y + o[1]); }
                auto E = [&](int a, int bb) { webE.push_back(base + a); webE.push_back(base + bb); };
                switch (v) {
                    case 0: E(0, 5); E(1, 4); E(7, 2); E(6, 3); break;
                    case 1: E(7, 6); E(5, 4); E(3, 2); E(1, 0); [[fallthrough]];
                    case 2: E(0, 7); E(6, 5); E(4, 3); E(2, 1); break;
                    case 3: {
                        static const float q[5][2] = {{0, 0}, {-2, -2}, {-2, 2}, {2, 2}, {2, -2}};
                        for (auto& p : q) { webV.push_back(X + p[0]); webV.push_back(Y + p[1]); }
                        E(0, 9); E(9, 7); E(6, 12); E(12, 5); E(1, 10); E(2, 10); E(3, 11); E(4, 11); E(9, 11); E(12, 10);
                        break;
                    }
                }
            }
    }

    // fontout.raw words (pointer table 0x58060), glyphs overlapping by 4 columns, centred on column 128.
    // Read as the file's coverage bytes (glyph runs found in them), so an art/ redraw does not replace it.
    void buildWords(Ctx& c) {
        const Bytes& raw = c.pak.get("fontout.raw");
        // glyph k of a..z = the k-th run of non-empty columns at least 4 wide; a column is empty when all
        // 64 values >> 3 are <= 1 (0x2d038 / 0x29860)
        std::vector<std::array<int, 2>> G;
        auto empty = [&](int x) {
            for (int y = 0; y < 64; y++) if ((raw[size_t(y) * 1024 + size_t(x)] >> 3) > 1) return false;
            return true;
        };
        for (int x = 0; G.size() < 26 && x < 1024;) {
            while (x < 1024 && empty(x)) x++;
            int s0 = x;
            while (x < 1024 && !empty(x)) x++;
            if (x - s0 >= 4) G.push_back({s0, x - s0});
        }
        auto glyph = [&](char ch) -> const std::array<int, 2>* {
            int k = ch - 'a';
            return k >= 0 && k < int(G.size()) ? &G[size_t(k)] : nullptr;
        };
        std::vector<uint8_t> tb(256 * 384 * 4, 0);
        for (int i = 0; i < 6; i++) {
            const std::string word = c.exe.str(0x58060 + 4 * uint32_t(i));
            int width = 0;
            for (char ch : word) width += (glyph(ch) ? (*glyph(ch))[1] : 12) - 4;  // unknown characters: 12 - 4
            int x = 128 - width / 2;
            for (char ch : word) {
                const std::array<int, 2>* gp = glyph(ch);
                if (!gp) { x += 12 - 4; continue; }
                const int* g = gp->data();
                for (int y = 0; y < 64; y++)
                    for (int k = 0; k < g[1]; k++) {
                        int xx = x + k;
                        if (xx < 0 || xx > 255) continue;
                        uint8_t v = uint8_t((raw[size_t(y) * 1024 + size_t(g[0] + k)] >> 3) * 8);
                        uint8_t* d = &tb[(size_t(i * 64 + y) * 256 + size_t(xx)) * 4];
                        d[0] = d[1] = d[2] = std::max(d[0], v);
                        d[3] = 255;
                    }
                x += g[1] - 4;
            }
        }
        wordTex.createEmpty(256, 384, GL_RGBA8, false);
        wordTex.update(tb.data());
    }

    void ensureTargets(Ctx& c) {
        if (lineT.w != c.w || lineT.h != c.h) {
            lineT.create(c.w, c.h, false, GL_R16F);
            for (Target& t : wordT) t.create(c.w, c.h, false, GL_R16F);
            for (Target& t : iconT) t.create(c.w, c.h, false, GL_R16F);
        }
    }

    void drawVideoFrame(Ctx& c, Texture& v) {
        Target& f = c.scratch[0];
        f.bind();
        setBlend(Blend::None);
        video.use();
        v.bind(0);
        video.set("vid", 0);
        drawFullscreen();
    }

    // ------------------------------------------------------------------ Harl
    void harl(Ctx& c, float m) {
        const float t0 = tHarl();
        const double tk = std::max(0.0, double(m - t0) * kTickHz);
        const int n = int(tk) + 1;
        ExePalette pal;
        pal.level = 0x8000; pal.target = 0x4000; pal.speed = 1000;
        LyricTicker L(0x4a524c, exeJitter(c));
        int widx = 7, placed = 0;
        auto next = [&]() {
            const std::string& w = lyricCycle(c)[size_t(widx)];
            widx = (widx + 1) % 10;
            float cx = -1, cy = 0;
            if (placed < 6) { cx = kHarlPlaces[placed].cx; cy = kHarlPlaces[placed].cy; }
            placed++;
            L.place(c, w, cx, cy);
        };
        next();  // the init sets the new-word flag: first word on the first frame
        for (int k = 0; k < n; k++) {
            int pos = musicPos(t0 + k * kTick);
            pal.tick();
            bool ch = L.tick(pos);
            if (ch && (pos & 0x1f) == 0 && pos > 0x5c0) next();  // Harl starts at 0x5c0 with the init word
        }
        pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        // one frame per 6 ticks (callback 0x30990 with [0x580c0] = 5), from frame 0
        int frame = std::min(int(tk / 6.0), c.videoFrames("HARL") - 1);
        Texture& v = c.video("HARL", float(frame));
        drawVideoFrame(c, v);
        harlLyr.update(c, L.lyr, harlClk.steps(c));
        c.out->bind();
        ExeComposite k;
        k.lyrics = &harlLyr.acc();
        exeComposite(c, c.scratch[0], k, pal);
    }

    // ------------------------------------------------------------------ Web
    void webPart(Ctx& c, float m) {
        ensureTargets(c);
        const float t0 = tWeb();
        const double tk = std::max(0.0, double(m - t0) * kTickHz);
        const int n = int(tk) + 1;
        ExePalette pal;
        pal.level = 0x8000; pal.target = 0x4000; pal.speed = 1000;
        int pol = 0, last = musicPos(t0);
        for (int k = 0; k < n; k++) {
            int pos = musicPos(t0 + k * kTick);
            if (pos != last) {
                last = pos;
                if ((pos & 0x1f) == 0 && pal.level == pal.target) {
                    pal.speed = 500; pal.invert = true; pal.level = 0x8000; pol ^= 1;
                }
            }
            pal.tick();
        }
        pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        const float nt = float(tk);
        // camera (callback 0x2c360 + camera_27444): continuous in ticks
        const float camZ = 0.01f * (-4.0f * nt + 0.0081215f * nt * (nt - 1.0f) * 0.5f);
        const float camX = 20.0f * std::sin(nt / 150.0f + 1.27860f), camY = 20.0f * std::sin(nt / 73.0f + 1.74334f);
        const float phi = 0.0051f * nt, cph = std::cos(phi), sph = std::sin(phi);

        // layer A: the line planes
        const int cz = int(std::trunc(camZ));
        std::vector<float> sx(webV.size() / 2), sy(webV.size() / 2);
        std::vector<char> ok(webV.size() / 2);
        for (int kk = cz - 1; kk < cz + 10; kk++) {
            float d = float(kk) - camZ;
            if (d < -2.0f || d >= 12.0f || d <= 0.05f) continue;
            const float s = 5.0f / d;
            for (size_t i = 0; i < sx.size(); i++) {
                float dx = webV[i * 2] - camX, dy = webV[i * 2 + 1] - camY;
                sx[i] = 160.0f + (cph * dx - sph * dy) * s;
                sy[i] = 100.0f + (sph * dx + cph * dy) * s;
            }
            for (size_t e = 0; e < webE.size(); e += 2) {
                int a = webE[e], b = webE[e + 1];
                float x0 = sx[size_t(a)], y0 = sy[size_t(a)], x1 = sx[size_t(b)], y1 = sy[size_t(b)];
                if (std::max(x0, x1) < 0 || std::min(x0, x1) > 320 || std::max(y0, y1) < 0 || std::min(y0, y1) > 200) continue;
                if (std::abs(x1 - x0) > 4000 || std::abs(y1 - y0) > 4000) continue;
                // plus-shaped pen: ~6 counts across the line; x-major rising lines use the 4-row pen (twice as heavy)
                bool xmaj = std::abs(x1 - x0) >= std::abs(y1 - y0);
                bool rising = xmaj && ((x1 - x0) * (y1 - y0) < 0);
                lines.add(x0, y0, x1, y1, rising ? 3.0f : 1.5f, 1.3f);
            }
        }
        lineT.bind();
        gl::ClearColor(0, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        setBlend(Blend::Add);
        lines.draw();

        // layer B: words with the feedback trail (per rendered frame; the capture shows copies about 1/30 s apart)
        float Wv[24] = {0};
        for (int i = 0; i < 6; i++) {
            float w = -15.0f - 5.0f * i + 0.05f * nt;
            float z = (45.0f - w * w) * 0.25f;
            Wv[i * 4] = z;
            Wv[i * 4 + 1] = wordX[i];
            Wv[i * 4 + 2] = -450.0f + 150.0f * i;
            Wv[i * 4 + 3] = (z > 0.05f && z < 20.0f) ? 1.0f : 0.0f;
        }
        int steps = wordClk.steps(c, 30.0f);
        bool reset = steps < 0;
        if (reset) steps = 8;
        setBlend(Blend::None);
        for (int s = 0; s < steps; s++) {
            Target& dst = wordT[wordCur ^ 1];
            dst.bind();
            words.use();
            wordTex.bind(0);
            wordT[wordCur].bindTex(1);
            words.set("words", 0);
            words.set("prev", 1);
            gl::Uniform4fv(words.loc("W"), 6, Wv);
            words.set("cs", cph, sph);
            words.set("reset", reset && s == 0 ? 1.0f : 0.0f);
            drawFullscreen();
            wordCur ^= 1;
        }
        c.out->bind();
        web.use();
        lineT.bindTex(0);
        wordT[wordCur].bindTex(1);
        web.set("lines", 0);
        web.set("trail", 1);
        web.set("pol", pol);
        pal.apply(web);
        drawFullscreen();
    }

    // ------------------------------------------------------------------ Paranoid
    void paranoidPart(Ctx& c, float m) {
        Texture& weird = c.tex("weird");
        const float t0 = kParanoidStart;
        if (m < t0) {  // white while the part loads
            c.out->bind();
            gl::ClearColor(1, 1, 1, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT);
            return;
        }
        const double tk = double(m - t0) * kTickHz;
        const int n = int(tk) + 1;
        ExePalette pal;
        pal.level = 0x8000; pal.target = 0x4000; pal.speed = 100;
        int last = musicPos(t0), frame = 0;
        for (int k = 0; k < n; k++) {
            int pos = musicPos(t0 + k * kTick);
            if (pos != last) { last = pos; if ((pos & 3) == 0) frame = (frame + 1) % 44; }
            pal.tick();
        }
        pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        const float t1 = float(7.0 * tk), t2 = float(2.0 * tk);
        Texture& v = c.video("PARANOID", float(frame));
        Target& f = c.scratch[0];
        f.bind();
        setBlend(Blend::None);
        paranoid.use();
        weird.bind(0);
        v.bind(1);
        paranoid.set("weird", 0);
        paranoid.set("vid", 1);
        paranoid.set("wp", std::trunc(80.0f * std::sin(0.01f * t1 + 24.0f) + 160.0f), std::trunc(50.0f * std::cos(t1 / 143.0f + 34.0f) + 100.0f),
                     t1 / 72.0f);
        paranoid.set("vph", t2 / 172.0f);
        paranoid.set("vk", 1.0f);
        drawFullscreen();
        c.out->bind();
        ExeComposite k;
        k.winx = 32;
        k.winy = 100;
        k.open = std::min(1.0f, 0.01f * float(tk));
        exeComposite(c, f, k, pal);
    }

    // ------------------------------------------------------------------ 0x2e8e4 A, B, C
    void sheetMesh(const std::vector<float>& v, const std::vector<unsigned>& idx) {
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
        gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_DYNAMIC_DRAW);
        gl::DrawElements(GL_TRIANGLES, GLsizei(idx.size()), GL_UNSIGNED_INT, nullptr);
        gl::BindVertexArray(0);
    }

    void faceFloor(Ctx& c, float m) {
        Texture& hex = c.tex("tex3d#1");
        Texture& blue = c.tex("tex3d#2");
        Texture& phong = c.tex("phong1");
        const float tA = tParEnd(), tb = tB(), tc = tC();
        const double tk = std::max(0.0, double(m - tA) * kTickHz);
        const int n = int(tk) + 1;
        // replay the ticks from the start of A
        ExePalette pal;
        pal.level = 0x8000; pal.target = 0x4000; pal.speed = 150;
        Cam cam;
        cam.pos = Vec3(0, 0, 13500);
        float w = 5.9f, p = 6.15f, Z = 15000.0f, rollAcc = 0, camT = 0, sheetT = 523.0f + 3.0f * float((tA - kParanoidStart) * kTickHz);
        float ovk = 1.0f;
        int phase = 0, cStart = -1, last = musicPos(tA), vframe = 0;
        float I = 0, a = 0;
        int nTile = 45;
        struct Tile { float x, y, z, a, b, wa, wb, ph, fr, s, c; };
        std::vector<Tile> tiles;
        ExeRand rnd(0x2e8e4);
        auto step = [&](int k) {  // one 70 Hz tick
            const float tm = tA + k * kTick;
            int pos = musicPos(tm);
            if (pos != last) { last = pos; if ((pos & 3) == 0) vframe = (vframe + 1) % 44; }
            if (phase == 0 && (cam.pos.z >= 14000.0f || tm >= tb)) phase = 1;
            if (phase == 1 && tm >= tc) {
                phase = 2;
                cStart = k;
                pal.level = 0x8000; pal.invert = true; pal.speed = 70; pal.target = 0x4000;
                cam.damp = 0.87f;
                // tiles_init: the sheet's current shape
                for (int j = -10; j <= 9; j++)
                    for (int i = -16; i <= 15; i++) {
                        Tile t{};
                        t.x = 15.0f * i; t.y = 15.0f * j;
                        t.z = 60.0f * std::cos(0.2f * j - 0.013f * sheetT + 34.0f) * std::sin(i / 7.0f + 0.0164375f * sheetT);
                        t.wa = float(rnd.next() - 0x4000) * 3.3333e-6f;
                        t.wb = float(rnd.next() - 0x4000) * 3.3333e-6f;
                        t.ph = float(rnd.next()) * 3.3333e-4f;
                        t.fr = 0.1f / (30.0f + 0.001f * float(rnd.next()));
                        float q = 1e-4f * float(i * i + j * j);
                        t.s = std::sin(q); t.c = std::cos(q);
                        tiles.push_back(t);
                    }
            }
            sheetT += 3.0f;
            camT += 4.0f;
            pal.tick();
            if (phase == 0) {  // 0x2e05c last phase
                cam.pos.z += w; w += 0.1f;
                cam.pos.z += 6.0f;
                p += 0.01f;
                cam.roll = 0.04f * std::sin(p);
                pal.target = 0x4000; pal.speed = 150;
            } else if (phase == 1) {  // 0x2e2fc
                p += 0.01f;
                float d = 6.0f + w;
                Z += d; cam.pos.z += d;
                cam.roll = 0.004f * std::sin(p);
            } else {  // 0x2e240 + 0x28a80
                p += 0.01f;
                Z += 15.0f; cam.pos.z += 14.0f;
                cam.roll = 0.004f * std::sin(p);
                if (pos > 0xf80 + 0x20) { I = std::trunc(I + a); a += 0.4f; }
                if (pos > 0xf80 + 0x28) { pal.target = 0; pal.speed = 100; }
                for (Tile& t : tiles) {
                    float x = t.c * t.x - t.s * t.y, y = t.s * t.x + t.c * t.y;
                    t.x = x * 1.007f; t.y = y;
                    t.z += 3.0f + 0.3f * std::sin(float(nTile) * t.fr + t.ph);
                    t.a += t.wa; t.b += t.wb;
                }
                nTile++;
            }
            if (phase < 2) ovk = std::max(0.0f, ovk - 0.005f);
            // camera targets (cam_targets_276f8 / 0x28aa0)
            float R = 290.0f + 50.0f * std::sin(0.0005f * camT + 64.0f);
            float sw = std::sin(3.0f * camT / 700.0f + 34.0f);
            cam.tgt = Vec3(std::cos(3.0f * camT / 1100.0f) * sw * R, std::sin(3.0f * camT / 1100.0f) * sw * R, 0);
            if (phase == 1) { cam.tgt.x *= 3.0f; cam.tgt.y *= 1.5f; }
            if (phase == 0) cam.look = Vec3(0, 0, 16000.0f + cam.pos.z);
            else cam.look = Vec3(0, 0, Z);
            if (phase == 2) { cam.tgt = Vec3(0, 0, 0); }
            cam.tick(1);
            if (phase == 2) { cam.pos.x = 0; cam.pos.y = 0; }
            rollAcc += cam.roll;
        };
        for (int k = 0; k < n; k++) step(k);
        (void)rollAcc;
        // Everything above moves once per tick: show it blended towards the next tick, so the camera, the
        // sheet and the tiles move smoothly at any frame rate (phase, video frame and palette events stay discrete).
        {
            const Cam cam0 = cam;
            const float Z0 = Z, sheetT0 = sheetT, I0 = I, ovk0 = ovk;
            const ExePalette pal0 = pal;
            const int phase0 = phase, vframe0 = vframe;
            const std::vector<Tile> tiles0 = tiles;
            step(n);
            const float f = float(tk - std::floor(tk));
            auto mixf = [f](float x, float y) { return x + (y - x) * f; };
            if (phase == phase0) {
                cam.pos = cam0.pos + (cam.pos - cam0.pos) * f;
                cam.M = lerpRot(cam0.M, cam.M, f);
                Z = mixf(Z0, Z); sheetT = mixf(sheetT0, sheetT); I = mixf(I0, I); ovk = mixf(ovk0, ovk);
                pal.level = int(std::lround(mixf(float(pal0.level), float(pal.level))));
                if (tiles.size() == tiles0.size())
                    for (size_t i = 0; i < tiles.size(); i++) {
                        Tile& t = tiles[i];
                        const Tile& t0 = tiles0[i];
                        t.x = mixf(t0.x, t.x); t.y = mixf(t0.y, t.y); t.z = mixf(t0.z, t.z);
                        t.a = mixf(t0.a, t.a); t.b = mixf(t0.b, t.b);
                    }
            } else {  // a phase change starts next tick: show this one
                cam = cam0; Z = Z0; sheetT = sheetT0; I = I0; ovk = ovk0; pal = pal0; tiles = tiles0;
                phase = phase0;
            }
            vframe = vframe0;
        }
        // world -> GL: (x, -y, -z) / 290
        const float S = 1.0f / 290.0f;
        auto toGL = [&](Vec3 v) { return Vec3(v.x * S, -v.y * S, -v.z * S); };
        Vec3 fw = cam.M.row(2), dn = cam.M.row(1);
        WorldParams wp;
        wp.eye = toGL(cam.pos);
        wp.target = wp.eye + Vec3(fw.x, -fw.y, -fw.z);
        wp.up = Vec3(-dn.x, dn.y, dn.z);
        wp.floorY = -1.0f;
        wp.ceilY = 1.0f;
        wp.fovy = 2.0f * std::atan(0.4f);
        wp.fogDist = 8.0f;
        wp.bright = 1.2f;
        wp.texScale = 290.0f / 512.0f;  // 256 texels per 512 units
        wp.glintAmount = 0.0f;
        Target& f = c.scratch[0];
        f.bind();
        setBlend(Blend::None);
        drawWorld(c, wp);
        // the sheet (A, B) or the tiles (C)
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        gl::Enable(GL_DEPTH_TEST);
        Mat4 mvp = worldProj(c, wp) * worldView(wp) * Mat4::scale(S, -S, -S);
        sheet.use();
        sheet.set("mvp", mvp);
        sheet.set("r0", cam.M.row(0));
        sheet.set("r1", cam.M.row(1));
        phong.bind(1);
        sheet.set("phong", 1);
        std::vector<float> v;
        std::vector<unsigned> idx;
        if (phase < 2) {
            const float Ta = 0.0164375f * sheetT, Tb = 0.013f * sheetT;
            for (int j = -10; j <= 10; j++)
                for (int i = -16; i <= 16; i++) {
                    float h = 60.0f * std::sin(i / 7.0f + Ta) * std::cos(0.2f * j - Tb + 34.0f);
                    float gx = 60.0f * std::cos(0.2f * j - Tb + 34.0f) * std::sin((10.0f * i + 1.0f) / 70.0f + Ta) - h;
                    float gy = 60.0f * std::sin(i / 7.0f + Ta) * std::cos(0.02f * (10.0f * j + 1.0f) - Tb + 34.0f) - h;
                    float l = 1.0f / std::sqrt(1.0f + gx * gx + gy * gy);
                    v.insert(v.end(), {15.0f * i, 15.0f * j, Z + h, gx * l, gy * l, l});
                }
            for (int j = 0; j < 20; j++)
                for (int i = 0; i < 32; i++) {
                    unsigned a0 = unsigned(j * 33 + i);
                    idx.insert(idx.end(), {a0, a0 + 1, a0 + 34, a0, a0 + 34, a0 + 33});
                }
            blue.bind(0);
            sheet.set("tex", 0);
            sheet.set("uvs", 0.390625f, 0.625f);
            sheet.set("clampUV", 1);
        } else {
            unsigned b = 0;
            for (const Tile& t : tiles) {
                M3 R = rot2(t.a, t.b);
                Vec3 r0 = R.row(0) * 15.0f, r1 = R.row(1) * 15.0f, nn = R.row(2);
                Vec3 p0(t.x, t.y, t.z + Z - I);
                Vec3 q[4] = {p0, p0 + r0, p0 + r0 + r1, p0 + r1};
                for (Vec3& qq : q) v.insert(v.end(), {qq.x, qq.y, qq.z, nn.x, nn.y, nn.z});
                idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
                b += 4;
            }
            hex.bind(0);
            sheet.set("tex", 0);
            sheet.set("uvs", 0.78125f, 1.25f);
            sheet.set("clampUV", 0);
        }
        sheetMesh(v, idx);
        gl::Disable(GL_DEPTH_TEST);
        // face overlay (A, B) and palette
        Texture& vid = c.video("PARANOID", float(vframe));
        c.out->bind();
        palpass.use();
        f.bindTex(0);
        vid.bind(1);
        palpass.set("src", 0);
        palpass.set("vid", 1);
        palpass.set("vph", float(2.0 * (double(m - kParanoidStart) * kTickHz)) / 172.0f);
        palpass.set("vk", phase < 2 ? ovk : 0.0f);
        pal.apply(palpass);
        drawFullscreen();
        (void)cStart;
    }

    // ------------------------------------------------------------------ Dance
    void dance(Ctx& c, float m) {
        const float t0 = tDance(), ta = tAlex();
        const bool alex = m >= ta;
        const float ts = alex ? ta : t0;
        const double tk = std::max(0.0, double(m - ts) * kTickHz);
        // the lyric state runs from the dance start
        const int nl = int(std::max(0.0, double(m - t0) * kTickHz)) + 1;
        ExePalette pal;
        pal.level = 0x8000; pal.target = 0x4000; pal.speed = 1000;
        LyricTicker L(0x10000, exeJitter(c));
        L.place(c, "", -1, 0);
        const std::vector<SongEntry>& song = songTable(c);
        size_t wi = 0;
        for (int k = 0; k < nl; k++) {
            int pos = musicPos(t0 + k * kTick);
            bool ch = L.tick(pos);
            if (ch)
                for (size_t i = wi; i < std::size(kDancePlaces); i++) {
                    const SongEntry& e = song[size_t(kDanceFirstSong) + i];
                    if (songPos(e) == pos) { L.place(c, e.word, kDancePlaces[i].cx, kDancePlaces[i].cy); wi = i + 1; }
                }
        }
        const int n = int(tk) + 1;
        for (int k = 0; k < n; k++) pal.tick();
        pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        const char* clip = alex ? "ALEX" : "VS";
        int frame = std::min(int(tk / 8.0), c.videoFrames(clip) - 1);
        Texture& v = c.video(clip, float(frame));
        drawVideoFrame(c, v);
        danceLyr.update(c, L.lyr, danceClk.steps(c));
        c.out->bind();
        ExeComposite k;
        k.lyrics = &danceLyr.acc();
        exeComposite(c, c.scratch[0], k, pal);
    }

    // ------------------------------------------------------------------ Icons + MC-303 (0x2a840), then black
    void iconsPart(Ctx& c, float m) {
        ensureTargets(c);
        Texture& img = c.tex("final");
        const float t0 = tIcons();
        const double tk = std::max(0.0, double(m - t0) * kTickHz);
        const int n = int(tk) + 1;
        ExePalette pal;
        pal.level = 0x8000; pal.target = 0x4000; pal.speed = 100;
        ExeRand rnd(0x303);
        int cell[35] = {0};
        for (int i = 0; i < 34; i++) cell[i] = rnd.next() & 7;
        const std::array<int, 16>& jitter = exeJitter(c);
        int idx = -1, e = 0xc0, eT = 0, last = -1, J[32] = {0};
        float f = 0;
        for (int k = 0; k < n; k++) {
            int pos = posAt(t0 + k * kTick);
            if (pos != last) {
                last = pos;
                for (int& j : J) j = int(0.7f * float(jitter[size_t(rnd.next() & 15)]));
                if ((pos & 3) == 0) {
                    if ((rnd.next() & 0x1f) == 0) f = f > 0.5f ? 0.0f : 1.0f;
                    for (int i = 0; i < 16; i++) {
                        int vv = rnd.next() & 7;
                        int r = rnd.next() % 5;
                        int cc = rnd.next() % 7;
                        cell[r * 7 + cc] = vv;
                    }
                }
                if ((pos & 0x3f) == 0 && idx < 3) { idx++; eT = 0; e = 0xc0; }
                if ((pos & 0x3f) > 0x32 && idx < 3) eT = 0x400;
            }
            if (e < eT) e += 3;
            if (e > eT) e -= 2;
            if (f > 0) f -= 0.021f;
            if (f < 0) f = 0;
            if (idx > 2) { pal.speed = 10; pal.target = 0; }
            pal.tick();
        }
        pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        if (m >= kSongEnd) J[0] = J[7] = 0;  // the song has ended; the capture froze on a calm frame (gain 1)
        c.out->bind();
        if (pal.level <= 0) {  // the loop has ended: black until a key is pressed
            gl::ClearColor(0, 0, 0, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT);
            return;
        }
        // final.raw: icon source columns (0x57fd4) and widths (0x57ff4), line rows and heights (0x58014)
        int row0[4], hh[4], iconX[8], iconW[8];
        for (uint32_t i = 0; i < 4; i++) {
            row0[i] = c.exe.s32(0x58014 + 8 * i);
            hh[i] = c.exe.s32(0x58014 + 8 * i + 4);
        }
        for (uint32_t i = 0; i < 8; i++) {
            iconX[i] = c.exe.s32(0x57fd4 + 4 * i);
            iconW[i] = c.exe.s32(0x57ff4 + 4 * i);
        }
        int s = e / 3, nn = 0, jx = 0;
        if (idx >= 0) {
            if (s < 10) { s += 3 * J[0]; }
            if (s < 64) nn = std::abs(s) + 1;
        }
        float step = nn > 0 ? float((0x10000 - 1000 * nn) / nn) : 0.0f;
        float gain = 1.0f + 0.2f * float(std::abs(J[7]));
        int steps = iconClk.steps(c, 35.0f);
        bool reset = steps < 0;
        if (reset) steps = 12;
        setBlend(Blend::None);
        for (int k = 0; k < steps; k++) {
            Target& dst = iconT[iconCur ^ 1];
            dst.bind();
            icons.use();
            img.bind(0);
            iconT[iconCur].bindTex(1);
            icons.set("img", 0);
            icons.set("prev", 1);
            setInts(icons, "cell", cell, 35);
            setInts(icons, "iconX", iconX, 8);
            setInts(icons, "iconW", iconW, 8);
            icons.set("idx", idx);
            icons.set("n", nn);
            icons.set("row0", idx >= 0 ? row0[idx] : 0);
            icons.set("h", idx >= 0 ? hh[idx] : 0);
            icons.set("jx", jx);
            icons.set("step", step);
            icons.set("gain", gain);
            icons.set("finv", f);
            icons.set("reset", reset && k == 0 ? 1.0f : 0.0f);
            drawFullscreen();
            iconCur ^= 1;
        }
        c.out->bind();
        grey.use();
        iconT[iconCur].bindTex(0);
        grey.set("src", 0);
        pal.apply(grey);
        drawFullscreen();
    }

    void render(Ctx& c) {
        init(c);
        // textures first (creating one binds it)
        c.tex("weird"); c.tex("tex3d#1"); c.tex("tex3d#2"); c.tex("phong1"); c.tex("final");
        const float m = exeTime(c.T);
        if (m < tWeb()) harl(c, m);
        else if (m < tWebEnd() + 2.0f && m < 150.0f) webPart(c, std::min(m, tWebEnd() - 0.001f));
        else if (m < tParEnd()) paranoidPart(c, m);
        else if (m < tDance()) faceFloor(c, m);
        else if (m < tIcons()) dance(c, m);
        else iconsPart(c, m);
        setBlend(Blend::None);
    }
};
Late& late() {
    static Late l;
    return l;
}

struct Harl : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
struct Web : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
struct Paranoid : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
struct FaceFloor : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
struct Dance : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
struct Icons : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
struct Outro : Scene { void render(Ctx& c, float, float) override { late().render(c); } };
REGISTER_SCENE(Harl, "harl");
REGISTER_SCENE(Web, "web");
REGISTER_SCENE(Paranoid, "paranoid");
REGISTER_SCENE(FaceFloor, "facefloor");
REGISTER_SCENE(Dance, "dance");
REGISTER_SCENE(Icons, "icons");
REGISTER_SCENE(Outro, "outro");
}  // namespace
