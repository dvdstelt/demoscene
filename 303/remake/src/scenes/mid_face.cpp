// 1:32 - 2:00  The rings tunnel (0x16198) and the first three loops of the big mid-section 0x2e8e4:
// L1 flowers lit by the web tunnel, L2 flowers darkened by the line burst, L3 the faceo.dat relief plate
// carrying the flowers. Ported from the exe (notes-mid1.md); callbacks 0x15ffc (tunnel), 0x2a0d4 (lyrics,
// song mode), 0x29aac (flower poles), 0x2bd94 / 0x2b0ec (web and line counters), 0x23384 (face),
// 0x2e798 / 0x2e534 / 0x2e684 (palette) are replayed from the part start every frame.
//
// Both scenes ("rings" and "face") are the same class; what is drawn is decided by the exe's music
// position, so either cue may cover the cut at pos 0x801.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

#include "../exefx.h"

namespace {
// The capture machine renders these parts at about 33 frames per second (a 9-frame beat in the capture's
// frame differences); the per-frame state (spikey history, lyric trails, line feedback) uses this rate.
constexpr float kFrameHz = 33.3f;
// Tunnel: web loop ends at pos 0x740; tunnel.raw loads in about 0.04 s (negative flash seen from v 92.40).
constexpr int kTunnelPos = 0x740;
constexpr float kTunnelLoad = 0.04f;
// 0x2e8e4: starts at pos 0x801 on a white palette; its init (wire, bd2, tex3d, phong1, faceo, 3d.gst)
// takes about 0.47 s in the capture (white until v 103.17), then 0x2e684 flashes in over 16 ticks.
constexpr int kFacePos = 0x801;
constexpr float kFaceLoad = 0.47f;
constexpr int kL2Pos = 0x840, kL3Pos = 0x878;
const char* kFadeGLSL = R"(
// fade table row r (0..63): row 32 = identity, below towards black, above towards white
vec3 fadeRow(vec3 c, float r) { return r <= 32.0 ? c * (r / 32.0) : mix(c, vec3(1.0), (r - 32.0) / 31.0); }
)";

// ------------------------------------------------------------------------------------------------ tunnel
// 0x137b8 ray grid + 0x1cddc mapper, per pixel: cylinder x^2 + y^2 = 1, texture addressed by the hit's
// x (column) and z (row), fade row min(35, 2/s) through tunnel.fad.
const char* kWallFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec3 R0, R1, R2, org;
uniform float uoff;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec3 D = (156.25 * R2 + (px.x - 160.0) * R0 + (px.y - 100.0) * R1) / 8.0;   // grid units
    float C = org.x * org.x + org.y * org.y - 1.0;
    float r2 = D.x * D.x + D.y * D.y;
    if (r2 < 1e-4) r2 = 1e-4;
    float b = D.x * org.x + D.y * org.y;
    float s = (sqrt(abs(4.0 * b * b - 4.0 * r2 * C)) - 2.0 * b) / (2.0 * r2);
    if (s == 0.0) s = 1e-4;
    float X = org.x + s * D.x, Z = org.z + s * D.z;
    vec2 tc = vec2(100.0 * X + 128.0 + uoff, 30.0 * Z + 128.0);   // (column, row)
    float w = min(35.0, 2.0 / s);
    frag = vec4(fadeRow(texture(tex, tc / 256.0).rgb, w), 1.0);
}
)";

// spikey sprite copies (0x1405c / 0x1438c rendered straight to the screen, 0x1c5e3 placement)
const char* kSpikeVS = R"(
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
uniform vec3 r0, r1, r2;
uniform vec3 sprite;   // centre x, y (original pixels), edge length in pixels
noperspective out vec2 nxy;
noperspective out vec2 sp;
void main() {
    vec3 q = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    nxy = vec2(dot(r0, n), dot(r1, n));
    sp = 600.0 * q.xy / (q.z + 2000.0);                 // texels from the 128x128 sprite centre
    vec2 px = sprite.xy + sp * sprite.z / 128.0;
    gl_Position = vec4(px.x / 160.0 - 1.0, 1.0 - px.y / 100.0, q.z / 400.0, 1.0);
}
)";
const char* kSpikeFS = R"(
noperspective in vec2 nxy;
noperspective in vec2 sp;
out vec4 frag;
uniform sampler2D env;
uniform float shade;
void main() {
    if (abs(sp.x) > 64.0 || abs(sp.y) > 64.0) discard;    // outside the 128x128 history bitmap
    vec3 c = texture(env, (120.0 * nxy + 128.0) / 256.0).rgb;
    frag = vec4(fadeRow(c, shade), 1.0);
}
)";

// ------------------------------------------------------------------------------------------------ flowers
// 0x29ae8: three moving 1/z poles warp tex3d image 5 (column = T + sum 10000 dx / (1 + r^2), row = sum
// 10000 dy / (1 + r^2)), evaluated per pixel instead of per 8x8 grid point.
const char* kFlowerGLSL = R"(
uniform sampler2D flowerTex;
uniform vec2 pole0, pole1, pole2;
uniform float flowT;
vec2 flowerAdd(vec2 c, vec2 p) {
    vec2 d = c - p;
    return d * (10000.0 / (1.0 + dot(d, d)));
}
vec3 flowerColor(vec2 p) {   // p: B pixel (x, y)
    vec2 tc = vec2(flowT, 0.0) + flowerAdd(pole0, p) + flowerAdd(pole1, p) + flowerAdd(pole2, p);
    // near the poles the stripes get denser than a pixel; the original's 8x8 interpolation shows them as
    // coloured sparkle rather than grey, so the filter footprint is capped
    vec2 gx = dFdx(tc), gy = dFdy(tc);
    float lod = 0.5 * log2(max(max(dot(gx, gx), dot(gy, gy)), 1e-8));
    return textureLod(flowerTex, tc / 256.0, clamp(lod, 0.0, 2.5)).rgb;
}
)";

// L1 / L2: flowers lightened (0x1c490) or darkened (0x1c503) by the line buffer A
const char* kFlowersFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D lines;
uniform int darken;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec3 col = flowerColor(px);
    float a = mod(texture(lines, uv).r, 128.0);      // A & 127
    float r = min(31.0, 2.0 * a);
    col = darken != 0 ? fadeRow(col, 31.0 - r) : fadeRow(col, 32.0 + r);
    frag = vec4(col, 1.0);
}
)";

// L3: the faceo.dat plate (0x22be0 projection, 0x23170 / 0x1e223 textured fill): flowers at the rotated
// point's (x, y), brightness phong1[ev][eu] through 3d.fad
const char* kPlateVS = R"(
layout(location = 0) in vec3 scr;   // sx, sy, depth
layout(location = 1) in vec2 tex;   // B pixel
layout(location = 2) in vec2 envc;  // phong1 texel (eu, ev)
noperspective out vec2 tc;
noperspective out vec2 ec;
void main() {
    tc = tex;
    ec = envc;
    gl_Position = vec4(scr.x / 160.0 - 1.0, 1.0 - scr.y / 100.0, (scr.z - 200.0) / 200.0, 1.0);
}
)";
const char* kPlateFS = R"(
noperspective in vec2 tc;
noperspective in vec2 ec;
out vec4 frag;
uniform sampler2D phong;
void main() {
    float r = texture(phong, ec / 256.0).r * 255.0;
    frag = vec4(fadeRow(flowerColor(tc), r), 1.0);
}
)";

// 0x2b110 zoom feedback (x1/0.9 about the centre, x0.75 trunc), per rendered frame
const char* kZoomFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D src;
void main() {
    float v = texture(src, (uv - 0.5) * 0.9 + 0.5).r;
    frag = vec4(max(0.75 * v - 0.25, 0.0), 0.0, 0.0, 1.0);
}
)";

// ------------------------------------------------------------------------------------------------ lyrics
// The song table of 0x2a0d4 is read from the exe: songTable() / songIndex() in exefx.h.

// The capture's realisation of the random lyric choices (read off the capture, original pixels).
struct LyrPlace { int pos, tx, ty; };
const LyrPlace kWordAt[] = {{0x78e, 91, 94}, {0x7aa, 120, 16}, {0x7b2, 220, 74},
                            {0x7ce, 200, 25}, {0x7e2, 67, 11}, {0x7fa, 7, 89}};
const LyrPlace kZoomAt[] = {{0x7bc, 100, 141}, {0x7e4, 132, 72}, {0x7e8, 33, 80}, {0x7f0, 53, 8}};
const int kInvertAt[] = {0x748, 0x754, 0x764, 0x798, 0x7a4};

// ------------------------------------------------------------------------------------------------ webs / lines
struct Web {
    std::vector<int> ring;  // ring[1..n]
    struct Seg { float a; int r1, r2; };
    std::vector<Seg> seg;
};

// 0x2bdb4(0x17): random ring radii, spokes and rungs
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

void drawWebs(LineBatch& batch, const Web& web, float T) {  // 0x2bdb4 frame
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

void drawChords(LineBatch& batch, double tL, double sL) {  // 0x2b110 + 0x2afb4: 4 chords, 12-fold
    double t = tL;
    for (int k = 0; k < 4; k++, t += sL) {
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

// ------------------------------------------------------------------------------------------------ state
// Tunnel, replayed per tick (0x15ffc, 0x2a0d4 in song mode, palette engine).
struct TunnelState {
    ExePalette pal;
    ExeRand rnd{0x303a};
    float R = 0, oz = -1.0f, vRing = 0.02f, vCam = 0.02f, A = 0;
    int t = 0, k = 0, ticks = 0;
    // lyrics
    LyricState lyr;
    int lastLyr = -1, lastIdx = 40;
    bool newLyric = false;
    TunnelState() {
        pal.level = 0x8000;   // flash at the start; the exe's invert flag is still set here (negative flash)
        pal.target = 0x4000;
        pal.speed = 300;
        pal.invert = true;
    }
    static bool listed(const int* a, size_t n, int pos) { return std::find(a, a + n, pos) != a + n; }
    void lyricTick(Ctx& c, int pos, bool songMode) {  // 0x2a0d4
        if (pos != lastLyr) {
            if (songMode) {
                int i = songIndex(c, pos);
                if (i != 40 && i != lastIdx) {
                    lastIdx = i;
                    newLyric = true;
                }
            }
            lastLyr = pos;
            if ((pos & 3) == 0) {
                rnd.next();  // P = 1/16 inversion: the capture's realisation is scripted
                if (listed(kInvertAt, std::size(kInvertAt), pos)) lyr.finv = lyr.finv > 0.5f ? 0.0f : 1.0f;
                lyr.smear = rnd.next() % 12;
                lyr.zoom = false;
                bool zoom = (rnd.next() & 15) < 2;
                if (zoom) {
                    int ty = rnd.next() % 144, tx = rnd.next() % std::max(1, 320 - lyr.width);
                    zoom = pos < 0x78e;  // once words are visible only the capture's jumps are kept
                    if (zoom) {
                        lyr.tx = tx;
                        lyr.ty = ty;
                    }
                }
                for (const LyrPlace& z : kZoomAt)
                    if (z.pos == pos) {
                        zoom = true;
                        lyr.tx = z.tx;
                        lyr.ty = z.ty;
                    }
                lyr.zoom = zoom;
            }
        }
        if (lyr.finv > 0) lyr.finv = std::max(0.0f, lyr.finv - 0.021f);
        if (newLyric) {  // 0x2a34c frame
            newLyric = false;
            lyr.word = songTable(c)[size_t(lastIdx)].word;
            lyr.width = LyricLayer::wordWidth(c, lyr.word);
            lyr.ty = rnd.next() % 144;
            lyr.tx = rnd.next() % std::max(1, 320 - lyr.width);
            for (const LyrPlace& w : kWordAt)
                if (w.pos == pos) {
                    lyr.tx = w.tx;
                    lyr.ty = w.ty;
                }
        }
    }
    void tick(Ctx& c, int pos, float frames) {
        pal.tick();
        lyricTick(c, pos, true);
        R -= vRing;  // 0x15ffc
        oz -= vCam;
        t += 4;
        if (pos > kTunnelPos + 0x80) {
            if (k == 0) {
                pal.speed = 300;
                pal.level = 0x8000;
            }
            vCam -= 0.0001f;
            k -= 5;
        }
        if (frames > 68.0f && vRing < 0.05f) {
            vRing += 0.0001f;
            vCam += 0.0001f;
            A += 0.0001f;
        }
        ticks++;
    }
};

// 0x2e8e4 L1-L3, replayed per tick from the end of its init.
struct FaceState {
    ExePalette pal;
    ExeRand rnd{0x2e8e4};
    struct Pole { float a, b, da, db; } P[3];
    int flowT = 0, webT = 0, last = -1, k = 200;
    bool l2 = false, l3 = false;
    FaceState() {
        pal.level = 0x8000;  // 0x2e684 on the first tick: white -> 3d palette in 16 ticks
        pal.target = 0x4000;
        pal.speed = 1000;
        // 0x29ae8(0x17) draws a = rand(), b = rand(), da, db = (rand() - 16384) / 900000 rad per tick. The capture's
        // realisation, fitted to the rosette centres seen at v 103.5-119.5: phase at v 104.0 and rad/s of video.
        static const float kFit[3][4] = {
            {0.960f, -1.164f, 4.014f, 0.953f}, {0.698f, 0.614f, 4.276f, -0.699f}, {4.538f, -0.953f, 4.014f, 1.101f}};
        const float perTick = 1.003f / float(kTickHz), n104 = 0.83f * float(kTickHz) / 1.003f;  // init end = v 103.17
        for (int i = 0; i < 3; i++) {
            P[i].da = kFit[i][1] * perTick;
            P[i].db = kFit[i][3] * perTick;
            P[i].a = kFit[i][0] - P[i].da * n104;
            P[i].b = kFit[i][2] - P[i].db * n104;
        }
        flowT = 62;  // stripe phase (T starts at an unknown tick count): best match to the capture's colours
    }
    void tick(int pos) {
        pal.tick();
        if (pos != last) {  // 0x2e798 (its x1.2 / x0.9 kicks are undone by 0x2e534 in the same tick)
            last = pos;
            if ((pos & 0xff) > 0xef) {
                pal.flash(1000);
                pal.invert = true;
            } else {
                pal.invert = false;
            }
        }
        if (!l2 && pos >= kL2Pos) {  // L2 start: white, negative for one row, 1.17 s fade
            l2 = true;
            pal.level = 0x8000;
            pal.speed = 200;
            pal.invert = true;
        }
        if (!l3 && pos >= kL3Pos) {  // L3 start: 2.34 s fade; 0x23384 registered
            l3 = true;
            pal.level = 0x8000;
            pal.speed = 100;
            pal.invert = true;
        }
        if (l3) k += 2;  // 0x23384
        for (Pole& p : P) {  // 0x29aac
            p.a += p.da;
            p.b += p.db;
        }
        flowT++;
        webT = (webT + 4) % 1024;  // 0x2bd94
    }
};

// ------------------------------------------------------------------------------------------------ scene
struct MidFace : Scene {
    Shader wall, spike, flowers, plate, zoom;
    LyricLayer lyrics;
    StepClock lyrClock, lineClock;
    std::vector<float> tint;
    Mesh spk;
    unsigned spkVao = 0, spkVbo = 0, spkIbo = 0;
    unsigned plVao = 0, plVbo = 0, plIbo = 0;
    LineBatch batch;
    Web web;
    Target lines[2];
    int cur = 0;

    void init(Ctx& c) override {
        wall.build(kFullscreenVS, std::string(kFadeGLSL) + kWallFS, "rings wall");
        spike.build(kSpikeVS, std::string(kFadeGLSL) + kSpikeFS, "rings spikey");
        flowers.build(kFullscreenVS, std::string(kFadeGLSL) + kFlowerGLSL + kFlowersFS, "face flowers");
        plate.build(kPlateVS, std::string(kFadeGLSL) + kFlowerGLSL + kPlateFS, "face plate");
        zoom.build(kFullscreenVS, kZoomFS, "face zoom");
        lyrics.init(c);
        tint = fadDarkTint(c, "tunnel.fad", "tunnel.raw");
        ExeRand wr(0x2bdb4);
        web = makeWeb(wr);

        spk = c.meshData("spikey");
        gl::GenVertexArrays(1, &spkVao);
        gl::BindVertexArray(spkVao);
        gl::GenBuffers(1, &spkVbo);
        gl::BindBuffer(GL_ARRAY_BUFFER, spkVbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(spk.pos.size() * 2 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
        gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::GenBuffers(1, &spkIbo);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, spkIbo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(spk.idx.size() * 4), spk.idx.data(), GL_STATIC_DRAW);

        const Mesh& face = c.meshData("faceo");
        gl::GenVertexArrays(1, &plVao);
        gl::BindVertexArray(plVao);
        gl::GenBuffers(1, &plVbo);
        gl::BindBuffer(GL_ARRAY_BUFFER, plVbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(face.pos.size() / 3 * 7 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, (void*)0);
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 28, (void*)12);
        gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 28, (void*)20);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::EnableVertexAttribArray(2);
        gl::GenBuffers(1, &plIbo);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, plIbo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(face.idx.size() * 4), face.idx.data(), GL_STATIC_DRAW);
        gl::BindVertexArray(0);
    }

    // ---------------------------------------------------------------- tunnel
    void renderTunnel(Ctx& c, float T) {
        const Texture& wallTex = c.tex("tunnel#0");
        const Texture& envTex = c.tex("tunnel#1");
        const Mesh& base = c.meshData("spikey");
        const float T0 = musicTimeOfPos(kTunnelPos) + kTunnelLoad;
        const float el = std::max(0.0f, T - T0);
        TunnelState s;
        const double ticksF = double(el) * kTickHz;
        const int n = int(ticksF);
        // 0x2a34c(0x17) calls 0x2a0d4 once (cycle mode), then the tunnel sets song mode
        s.lyricTick(c, kTunnelPos, false);
        s.newLyric = true;
        s.lyricTick(c, kTunnelPos, true);
        for (int i = 0; i < n; i++) {
            float tt = T0 + float((i + 1) / kTickHz);
            s.tick(c, musicPos(tt), float((i + 1) / kTickHz) * kFrameHz);
        }
        const float frac = float(ticksF - n);
        const float frames = el * kFrameHz;
        // continuous versions of the per-tick values between ticks
        const float t = float(s.t) + 4.0f * frac;
        const float k = float(s.k) - (s.k != 0 ? 5.0f * frac : 0.0f);
        const float oz = s.oz - s.vCam * frac, R = s.R - s.vRing * frac;

        // camera (0x1cd4b with pitch, yaw in 1024ths of a turn)
        const float pitch = 10.0f * std::sin(0.0025f * k), yaw = 512.0f * std::sin(0.001f * k);
        Vec3 R0, R1, R2;
        exeMatrix(pitch, yaw, R0, R1, R2);
        const float ox = 15.0f * s.A * std::sin(t / 423.0f);
        const float oy = 12.0f * s.A * std::cos(0.0025f * t + 432.0f);
        const float uoff = (16384.0f * std::sin((t / 3.0f + 1342.0f) * kPi * 2.0f / 1024.0f) + 15.0f * t) / 256.0f;

        Target& frame = c.scratch[0];
        frame.bind();
        setBlend(Blend::None);
        wall.use();
        wallTex.bind(0);
        wall.set("tex", 0);
        wall.set("R0", R0);
        wall.set("R1", R1);
        wall.set("R2", R2);
        wall.set("org", ox, oy, oz);
        wall.set("uoff", uoff);
        drawFullscreen();

        // ring sprites, far to near; each shows the spikey as rendered (64 - d) frames earlier
        int dir = R2.z < 0 ? 1 : -1;
        int i0 = -6 * dir, i1 = 6 * dir;
        if (frames < 65.0f) { i0 = 0; i1 = 1; dir = 1; }
        const int q = int(oz - R);
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
        spike.use();
        envTex.bind(0);
        spike.set("env", 0);
        gl::BindVertexArray(spkVao);
        gl::BindBuffer(GL_ARRAY_BUFFER, spkVbo);
        std::vector<float> v(spk.pos.size() * 2);
        for (int i = i0; i != i1; i += dir) {
            int ring = q / 2 + i;
            int d = (10 * ring) & 127;
            if (d >= 64) d = 127 - d;
            Vec3 dv(-ox, -oy, 2.0f * float(ring) + R - oz);
            float X = R0.dot(dv), Y = R1.dot(dv), Z = R2.dot(dv);
            if (Z <= 0.1f) continue;
            float P = 156.25f / Z, size = 0.8f * P;
            if (int(size) <= 3) continue;
            float shade = std::min(32.0f, 1.2f * size);
            // spikey state of that frame (0x13e78 morph, Rz(t/700) Ry(t/243))
            float age = d == 0 ? 0.0f : float(64 - d);
            float tc = std::max(0.0f, t - 4.0f * float(kTickHz) * age / kFrameHz);
            float m = std::sin(tc / 300.0f), Rb = 50.0f * std::sin(0.01f * tc) + 120.0f;
            for (size_t j = 0; j < base.extra.size(); j++) {
                float p = base.extra[j], f = (Rb + (p - Rb) * m) / p;
                for (int e = 0; e < 3; e++) spk.pos[j * 3 + size_t(e)] = base.pos[j * 3 + size_t(e)] * f;
            }
            spk.computeNormals();
            for (size_t j = 0; j < spk.pos.size() / 3; j++)
                for (int e = 0; e < 3; e++) {
                    v[j * 6 + size_t(e)] = spk.pos[j * 3 + size_t(e)];
                    v[j * 6 + 3 + size_t(e)] = spk.nrm[j * 3 + size_t(e)];
                }
            gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(v.size() * sizeof(float)), v.data());
            float a = tc / 700.0f, b = tc / 243.0f, s7 = std::sin(a), c7 = std::cos(a), s2 = std::sin(b), c2 = std::cos(b);
            spike.set("r0", c7 * c2, s7, -c7 * s2);
            spike.set("r1", -s7 * c2, c7, s7 * s2);
            spike.set("r2", s2, 0.0f, c2);
            spike.set("sprite", P * X + 160.0f, P * Y + 100.0f, size);
            spike.set("shade", shade);
            gl::Clear(GL_DEPTH_BUFFER_BIT);
            gl::DrawElements(GL_TRIANGLES, GLsizei(spk.idx.size()), GL_UNSIGNED_INT, nullptr);
        }
        gl::BindVertexArray(0);
        gl::Disable(GL_DEPTH_TEST);

        // lyrics (0x2a34c into B), A = F[32 + B/8][A], palette
        lyrics.update(c, s.lyr, lyrClock.steps(c, kFrameHz));
        c.out->bind();
        ExeComposite comp;
        comp.lyrics = &lyrics.acc();
        comp.tint = &tint;
        exeComposite(c, frame, comp, s.pal);
    }

    // ---------------------------------------------------------------- face
    void bindFlowers(const Shader& sh, const Texture& ft, const FaceState& s, float frac) {
        ft.bind(0);
        sh.set("flowerTex", 0);
        const char* names[3] = {"pole0", "pole1", "pole2"};
        for (int i = 0; i < 3; i++) {
            const FaceState::Pole& p = s.P[i];
            double a = double(p.a) + double(p.da) * frac, b = double(p.b) + double(p.db) * frac;
            sh.set(names[i], float(160.0 + 120.0 * std::sin(a)), float(100.0 + 80.0 * std::sin(b)));
        }
        sh.set("flowT", float(s.flowT) + frac);
    }

    void renderFace(Ctx& c, float T) {
        const Texture& ft = c.tex("tex3d#5");
        const Texture& phong = c.tex("phong1");
        if (lines[0].w != c.w || lines[0].h != c.h)
            for (Target& t : lines) t.create(c.w, c.h, false, GL_R16F);
        const float T0 = musicTimeOfPos(kFacePos) + kFaceLoad;
        FaceState s;
        const double ticksF = std::max(0.0, double(T - T0) * kTickHz);
        const int n = int(ticksF);
        for (int i = 0; i < n; i++) s.tick(musicPos(T0 + float((i + 1) / kTickHz)));
        const float frac = float(ticksF - n);
        const int pos = musicPos(T);
        Target& frame = c.scratch[0];

        if (T < T0) {  // loading: palette all white
            s.pal.level = 0x8000;
            s.pal.invert = false;
            frame.bind();
            gl::ClearColor(0, 0, 0, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT);
        } else if (pos < kL3Pos) {
            const bool l2 = pos >= kL2Pos;
            // line layer A at the original frame rate: L1 web tunnel (cleared every frame), L2 line burst
            int steps = lineClock.steps(c, kFrameHz);
            if (steps < 0) {
                for (Target& t : lines) {
                    t.bind();
                    gl::ClearColor(0, 0, 0, 0);
                    gl::Clear(GL_COLOR_BUFFER_BIT);
                }
                steps = l2 ? 6 : 1;
            }
            // 0x2b0ec counters: running since Aap (0x380-0x480, about 954 ticks) and again since this init
            const double nl = 954.0 + (double(T) - (musicTimeOfPos(kFacePos) + 0.05)) * kTickHz;
            for (int i = 0; i < steps; i++) {
                Target& dst = lines[cur ^ 1];
                dst.bind();
                setBlend(Blend::None);
                if (l2) {
                    zoom.use();
                    lines[cur].bindTex(0);
                    zoom.set("src", 0);
                    drawFullscreen();
                    drawChords(batch, 8763008.0 + 5.0 * nl, 1.0 + 4.0 * nl);
                } else {
                    gl::ClearColor(0, 0, 0, 0);
                    gl::Clear(GL_COLOR_BUFFER_BIT);
                    drawWebs(batch, web, float(s.webT) + 4.0f * frac);
                }
                batch.draw();
                cur ^= 1;
            }
            frame.bind();
            setBlend(Blend::None);
            flowers.use();
            bindFlowers(flowers, ft, s, frac);
            lines[cur].bindTex(1);
            flowers.set("lines", 1);
            flowers.set("darken", l2 ? 1 : 0);
            drawFullscreen();
        } else {
            drawPlate(c, s, frac, ft, phong, frame);
        }
        c.out->bind();
        ExeComposite comp;
        exeComposite(c, frame, comp, s.pal);
    }

    void drawPlate(Ctx& c, const FaceState& s, float frac, const Texture& ft, const Texture& phong, Target& frame) {
        const Mesh& face = c.meshData("faceo");
        const size_t nv = face.pos.size() / 3;
        const float k = float(s.k) + 2.0f * frac;
        // 0x23540: Mrel = Rz(A) Ry(B), relief push-back D
        const float A = 0.2f * std::cos(k / 144.0f + 54.0f), B = 0.3f * std::sin(k / 86.0f);
        const float D = std::max(0.0f, 150.0f * std::sin(0.005f * k + 1.0f) + 50.0f);
        const float cA = std::cos(A), sA = std::sin(A), cB = std::cos(B), sB = std::sin(B);
        std::vector<Vec3> P(nv), N(nv);
        for (size_t i = 0; i < nv; i++) {
            Vec3 v(face.pos[i * 3], face.pos[i * 3 + 1], face.pos[i * 3 + 2]);
            if (v.z >= -1.0f) {
                P[i] = Vec3(v.x, v.y, 0.0f);
            } else {
                Vec3 q(cA * cB * v.x + sA * v.y - cA * sB * v.z, -sA * cB * v.x + cA * v.y + sA * sB * v.z, sB * v.x + cB * v.z);
                q.z = std::min(q.z + D, 0.0f);
                P[i] = q;
            }
        }
        // 0x22e14: vertex normals = normalised sum of (v0 - v1) x (v2 - v1) in camera space
        for (size_t f = 0; f + 2 < face.idx.size(); f += 3) {
            const Vec3 &a = P[face.idx[f]], &b = P[face.idx[f + 1]], &cc = P[face.idx[f + 2]];
            Vec3 nn = (a - b).cross(cc - b);
            for (int e = 0; e < 3; e++) N[face.idx[f + size_t(e)]] = N[face.idx[f + size_t(e)]] + nn;
        }
        std::vector<float> vb(nv * 7);
        for (size_t i = 0; i < nv; i++) {
            float depth = P[i].z + 290.0f;
            Vec3 nn = N[i].norm();
            float* o = &vb[i * 7];
            o[0] = 160.0f + 300.0f * P[i].x / depth;
            o[1] = 100.0f + 300.0f * P[i].y / depth;
            o[2] = depth;
            o[3] = P[i].x + 160.0f;
            o[4] = P[i].y + 100.0f;
            o[5] = 120.0f * nn.x + 128.0f;
            o[6] = 120.0f * nn.y + 128.0f;
        }
        frame.bind();
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        setBlend(Blend::None);
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_BACK);
        plate.use();
        bindFlowers(plate, ft, s, frac);
        phong.bind(1);
        plate.set("phong", 1);
        gl::BindVertexArray(plVao);
        gl::BindBuffer(GL_ARRAY_BUFFER, plVbo);
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(vb.size() * sizeof(float)), vb.data());
        gl::DrawElements(GL_TRIANGLES, GLsizei(face.idx.size()), GL_UNSIGNED_INT, nullptr);
        gl::BindVertexArray(0);
        gl::Disable(GL_CULL_FACE);
        gl::Disable(GL_DEPTH_TEST);
    }

    void render(Ctx& c, float, float) override {
        const float T = exeTime(c.T);
        if (musicPos(T) < kFacePos) renderTunnel(c, T);
        else renderFace(c, T);
    }
};

struct Rings : MidFace {};
struct Face : MidFace {};
REGISTER_SCENE(Rings, "rings");
REGISTER_SCENE(Face, "face");

}  // namespace
