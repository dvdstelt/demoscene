// 1:05 - 1:12 and 2:30 - 3:11, ported from the exe (notes-mid3.md):
//  - 0x15734: photo + chrome spikey + credit words ("alex": alex.raw, statix / code; "vic": vic.raw, vic / music)
//  - 0x2b908 + 0x2d81c: icosa.dat tentacles (17-step delay line of orientations) over 256 unit squares seen from an
//    orbiting camera; "spiral" draws them as hidden-line wireframe through fish.fad, "star" env-maps them with sphere.raw
//  - 0x2e8e4 sub-parts D/E/F ("plasma"): hex corridor, gold metaballs, two ray-traced plasma balls, the polar ray sprite,
//    the camera flying away with roll, flash and fade to white.
// All five scenes share one engine that decides by the capture time what the original showed, so the scenes do not
// depend on the exact cue boundaries.
#include <algorithm>
#include <cmath>
#include <vector>

#include "../exefx.h"

namespace {
// ---------------------------------------------------------------- timing
// Part inits in exe music time (exeTime()), from the white flashes in the capture (alex 65.05, spiral 149.92,
// vic 156.66, star 163.42, plasma 169.95 video s). The parts' animation runs on 70 Hz ticks counted from there;
// their events (words, strobe, ends) come from the music position.
constexpr float kAlexStart = 66.575f, kAlexEnd = 73.40f;
constexpr float kSpiralStart = 151.105f, kVicStart = 157.857f, kStarStart = 164.606f, kPlasmaStart = 171.12f;
constexpr double kHz = kTickHz;

// ---------------------------------------------------------------- 3x3 matrices (row-major, p' = M p)
struct M3 {
    float a[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 row(int i) const { return {a[i * 3], a[i * 3 + 1], a[i * 3 + 2]}; }
    Vec3 col(int j) const { return {a[j], a[3 + j], a[6 + j]}; }
    Vec3 operator*(const Vec3& v) const { return {row(0).dot(v), row(1).dot(v), row(2).dot(v)}; }
    M3 operator*(const M3& b) const {
        M3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r.a[i * 3 + j] = a[i * 3] * b.a[j] + a[i * 3 + 1] * b.a[3 + j] + a[i * 3 + 2] * b.a[6 + j];
        return r;
    }
};
// 0x14760: Rz(a) * Ry(b)
M3 rotAB(float a, float b) {
    float sa = std::sin(a), ca = std::cos(a), sb = std::sin(b), cb = std::cos(b);
    M3 r;
    float v[9] = {ca * cb, sa, -ca * sb, -sa * cb, ca, sa * sb, sb, 0.0f, cb};
    std::copy(v, v + 9, r.a);
    return r;
}
M3 rodrigues(Vec3 n, float th) {
    float c = std::cos(th), s = std::sin(th), k = 1.0f - c;
    M3 r;
    float v[9] = {n.x * n.x * k + c,       n.x * n.y * k - s * n.z, n.x * n.z * k + s * n.y,
                  n.x * n.y * k + s * n.z, n.y * n.y * k + c,       n.y * n.z * k - s * n.x,
                  n.x * n.z * k - s * n.y, n.y * n.z * k + s * n.x, n.z * n.z * k + c};
    std::copy(v, v + 9, r.a);
    return r;
}
// 0x2721c: rotation by asin(|v|) about v
M3 axisAngle(Vec3 v) {
    float L = v.len();
    if (L < 1e-8f) return M3();
    return rodrigues(v * (1.0f / L), std::asin(std::min(L, 1.0f)));
}
// The rotation from a to b (both orthonormal, b = a R), applied by the fraction f: smooth camera turns between
// two 70 Hz ticks.
M3 lerpRot(const M3& a, const M3& b, float f) {
    M3 at;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) at.a[i * 3 + j] = a.a[j * 3 + i];
    const M3 R = at * b;
    // atan2 of sine and cosine: acos alone loses the tiny per-tick angles in single precision
    Vec3 axis(R.a[7] - R.a[5], R.a[2] - R.a[6], R.a[3] - R.a[1]);
    const float L = axis.len();
    const float ang = std::atan2(0.5f * L, (R.a[0] + R.a[4] + R.a[8] - 1.0f) * 0.5f);
    if (ang < 1e-7f || L < 1e-9f) return a;
    return a * rodrigues(axis * (1.0f / L), ang * f);
}
// 0x2d6a4 / 0x27444: turn the camera (rows = right, down, forward) towards a direction with gain k per tick
void lookStep(M3& C, Vec3 d, float k, float roll = 0.0f) {
    float len = d.len();
    if (len <= 0.01f) return;
    d = d * (k / len);
    C = C * axisAngle(d.cross(C.row(2)));
    if (roll != 0.0f) C = C * axisAngle(Vec3(0, 0, roll));
}

// ---------------------------------------------------------------- GL helpers
struct DynMesh {
    GLuint vao = 0, vbo = 0;
    size_t cap = 0;
    int stride = 0;
    void init(std::initializer_list<int> comps) {
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vbo);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        for (int c : comps) stride += c;
        int off = 0, i = 0;
        for (int c : comps) {
            gl::VertexAttribPointer(GLuint(i), c, GL_FLOAT, GL_FALSE, stride * 4, (void*)(size_t(off) * 4));
            gl::EnableVertexAttribArray(GLuint(i));
            off += c;
            i++;
        }
        gl::BindVertexArray(0);
    }
    void draw(const std::vector<float>& v) {
        if (v.empty()) return;
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        size_t bytes = v.size() * 4;
        if (bytes > cap) {
            cap = bytes * 2;
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(cap), nullptr, GL_DYNAMIC_DRAW);
        }
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), v.data());
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / size_t(stride)));
        gl::BindVertexArray(0);
    }
};

Texture makeTex(int w, int h, const std::vector<uint8_t>& rgba, bool linear, bool repeat) {
    Image im;
    im.w = w;
    im.h = h;
    im.rgba = rgba;
    Texture t;
    t.create(im, repeat, false);
    GLint f = linear ? GL_LINEAR : GL_NEAREST;
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    return t;
}

const char* kPalPassFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D src;
void main() { frag = vec4(exePalette(texture(src, uv).rgb), 1.0); }
)";

// A soft line quad (original pixel coordinates) like LineBatch: pos, across, value, alpha.
void lineQuad(std::vector<float>& out, float x0, float y0, float x1, float y1, float value, float hw) {
    float dx = x1 - x0, dy = (y1 - y0) * 1.2f;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) { dx = 1; dy = 0; len = 1; }
    dx /= len;
    dy /= len;
    float nx = -dy * hw, ny = dx * hw / 1.2f;
    float ex = dx * hw * 0.5f, ey = dy * hw * 0.5f / 1.2f;
    float ax = x0 - ex, ay = y0 - ey, bx = x1 + ex, by = y1 + ey;
    const float q[6][3] = {{ax + nx, ay + ny, 1}, {ax - nx, ay - ny, -1}, {bx + nx, by + ny, 1},
                           {bx + nx, by + ny, 1}, {ax - nx, ay - ny, -1}, {bx - nx, by - ny, -1}};
    for (auto& p : q) {
        out.push_back(p[0]);
        out.push_back(p[1]);
        out.push_back(p[2]);
        out.push_back(value);
        out.push_back(1.0f);
    }
}

// ================================================================ 0x15734: photo, spikey, credit words
const char* kPhotoFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D photo, pal, credits, grain;
uniform float bs, gain;        // box width in original pixels, darkening of the box sum table
uniform vec4 goff;             // grain strip offsets (0x1c789: 4 strips of 16000 bytes)
uniform int nw;
uniform vec4 word[2];          // x, y, source x, width (credits.raw)
uniform vec4 jit[18];          // per word: 4 shadow passes, 5 additive passes (xy)
float photoAt(float x, float y) {   // the photo sits in a linear buffer: rows wrap into each other
    if (x < 0.0) { x += 320.0; y -= 1.0; }
    else if (x >= 320.0) { x -= 320.0; y += 1.0; }
    if (y < 0.0 || y >= 200.0) return 0.0;
    return texture(photo, vec2(x / 320.0, y / 200.0)).r * 127.5;
}
float wordAt(int j, vec2 q) {
    vec4 w = word[j];
    if (q.x < 0.0 || q.y < 0.0 || q.x >= w.w || q.y >= 32.0) return 0.0;
    float sx = clamp(w.z + q.x, w.z + 0.5, w.z + w.w - 0.5);
    return textureLod(credits, vec2(sx / 512.0, q.y / 32.0), 0.0).r * 255.0 / 8.0;
}
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    // 0x1c91d: horizontal box sum of width bs through the table T[i] = i * step >> 16
    int n = int(ceil(bs - 0.001));
    float acc = 0.0;
    for (int k = 0; k < 64; k++) {
        if (k >= n) break;
        acc += photoAt(px.x - 0.5 * bs + (float(k) + 0.5) * bs / float(n), px.y);
    }
    float idx = acc / float(n) * gain;
    // 0x1c789: grain from a fixed noise table at random offsets, clamped to the 128 photo colours
    ivec2 ip = ivec2(floor(px));
    int strip = min(ip.y / 50, 3);
    int i = int(goff[strip]) + (ip.y - 50 * strip) * 320 + ip.x;
    float g = texelFetch(grain, ivec2(i & 255, (i >> 8) & 127), 0).r * 255.0 - 128.0;
    idx = clamp(idx + g, 0.0, 127.0);
    // credit words: 4 jittered subtractive shadow passes at (+3, +3), then 5 jittered additive passes
    for (int j = 0; j < 2; j++) {
        if (j >= nw) break;
        for (int p = 0; p < 4; p++) idx = max(0.0, idx - wordAt(j, px - word[j].xy - vec2(3.0) - jit[j * 9 + p].xy));
        for (int p = 4; p < 9; p++) idx = min(127.0, idx + wordAt(j, px - word[j].xy - jit[j * 9 + p].xy));
    }
    frag = vec4(texture(pal, vec2((idx + 0.5) / 128.0, 0.5)).rgb, 1.0);
}
)";
const char* kSpikeyVS = R"(
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
uniform vec3 r0, r1, r2, offs;
noperspective out vec2 tc;
void main() {
    vec3 q = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    vec3 nn = vec3(dot(r0, n), dot(r1, n), dot(r2, n));
    tc = (nn.xy * 120.0 + 128.0) / 256.0;      // phong.raw at the (inward) normal
    vec3 w = q + offs;                         // screen: 160 + 600 X / Z, 100 + 600 Y / Z
    float z = 1.0 - (w.z - 300.0) / 1100.0;    // farther = smaller: the exe draws its buckets near to far
    gl_Position = vec4(3.75 * w.x, -6.0 * w.y, z * w.z, w.z);
}
)";
const char* kSpikeyFS = R"(
noperspective in vec2 tc;
out vec4 frag;
uniform sampler2D env;
void main() { frag = vec4(texture(env, tc).rgb, 1.0); }
)";

// Tables of 0x15734 in the exe: the credit words (0x52f20, 6 x int32 {x, y, source x, width} in credits.raw,
// k = sel + 3 j) and the spikey per part (0x52f8c, 3 x float {a1, a2, ox, oy, oz}; sel 2 is unused).
constexpr uint32_t kCreditWords = 0x52f20, kCreditObjects = 0x52f8c;

// tick callback 0x154d4 (+ the palette tick 0x10124 before it)
struct CreditSim {
    int z = 4000, v = 5, n = 0, tickc = 0, shown = 0x8000;
    int J[32] = {};
    ExePalette pal;
    ExeRand rnd;
    const std::array<int, 16>* N;  // jitter table (0x52f10)
    CreditSim(uint32_t seed, const std::array<int, 16>& jitter) : rnd(seed), N(&jitter) {
        pal.level = 0x8000;
        pal.target = 0x4000;
        pal.speed = 300;
    }
    void tick(int pos, int S) {
        tickc++;
        pal.tick();
        shown = pal.level;
        int z1 = z + v;
        v -= z1 / 300;
        z = z1 * 252 / 256;  // damped spring
        if ((rnd.next() & 255) < 5) v += 150 + (rnd.next() & 63);  // random kick
        if ((tickc & 7) == 0)
            for (int& j : J) j = int(0.7f * float((*N)[size_t(rnd.next() & 15)]));
        if (pos > S + 0x20) n = 1;
        if (pos > S + 0x40) n = 2;
        if (pos > S + 0x80) n = 3;
        if (pos > S + 0x78 && !(pos & 1)) {  // white strobe in the last rows
            pal.level = 0x8000;
            pal.speed = 1800;
        }
    }
};

struct CreditPart {
    Shader photoSh, objSh;
    Texture photo[2], pal[2], grain;
    GLuint vao[2] = {0, 0}, vbo[2] = {0, 0}, ibo = 0;
    int nIdx = 0;
    bool ready = false;
    float words[6][4] = {}, offs[2][3] = {};

    void init(Ctx& c) {
        if (ready) return;
        ready = true;
        photoSh.build(kFullscreenVS, kPhotoFS, "credits photo");
        objSh.build(kSpikeyVS, kSpikeyFS, "credits spikey");
        const char* files[2] = {"alex.raw", "vic.raw"};
        for (int s = 0; s < 2; s++) {
            const Bytes& raw = c.pak.get(files[s]);
            std::vector<uint8_t> px(320 * 200 * 4), pl(128 * 4);
            for (int i = 0; i < 64000; i++) px[size_t(i) * 4] = raw[768 + size_t(i)];
            for (int i = 0; i < 128; i++) {  // photo colours 0..127 = file palette entries 0, 2, ..., 254
                for (int k = 0; k < 3; k++) pl[size_t(i) * 4 + size_t(k)] = uint8_t(std::min(63, int(raw[size_t(i) * 6 + size_t(k)])) * 255 / 63);
                pl[size_t(i) * 4 + 3] = 255;
            }
            photo[s] = makeTex(320, 200, px, true, false);
            pal[s] = makeTex(128, 1, pl, true, false);
        }
        // B1: 32 KB of noise, 2 * N[rand() & 15]
        ExeRand r(1997);
        std::vector<uint8_t> g(256 * 128 * 4);
        const std::array<int, 16>& N = exeJitter(c);
        for (int i = 0; i < 32768; i++) g[size_t(i) * 4] = uint8_t(128 + 2 * N[size_t(r.next() & 15)]);
        grain = makeTex(256, 128, g, false, false);
        // spikey (0x13e78): sel 0 radius r, sel 1 turned inside out (radius 300 - r)
        const Mesh& m = c.meshData("spikey");
        float A1[2], A2[2];
        for (uint32_t s = 0; s < 2; s++) {
            const uint32_t o = kCreditObjects + 20 * s;
            A1[s] = c.exe.f32(o);
            A2[s] = c.exe.f32(o + 4);
            for (uint32_t k = 0; k < 3; k++) offs[s][k] = c.exe.f32(o + 8 + 4 * k);
        }
        for (uint32_t k = 0; k < 6; k++)
            for (uint32_t q = 0; q < 4; q++) words[k][q] = float(c.exe.s32(kCreditWords + 16 * k + 4 * q));
        nIdx = int(m.idx.size());
        for (int s = 0; s < 2; s++) {
            size_t nv = m.pos.size() / 3;
            std::vector<Vec3> P(nv), N(nv);
            float R = 50.0f * A2[s] + 120.0f;
            for (size_t i = 0; i < nv; i++) {
                float r = m.extra[i];
                Vec3 d(m.pos[i * 3] / r, m.pos[i * 3 + 1] / r, m.pos[i * 3 + 2] / r);
                P[i] = d * (R + (r - R) * A1[s]);
            }
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                Vec3 v0 = P[m.idx[t]], v1 = P[m.idx[t + 1]], v2 = P[m.idx[t + 2]];
                Vec3 f = (v2 - v1).cross(v0 - v1);  // inward
                N[m.idx[t]] = N[m.idx[t]] + f;
                N[m.idx[t + 1]] = N[m.idx[t + 1]] + f;
                N[m.idx[t + 2]] = N[m.idx[t + 2]] + f;
            }
            std::vector<float> vb(nv * 6);
            for (size_t i = 0; i < nv; i++) {
                Vec3 nn = N[i].norm();
                float e[6] = {P[i].x, P[i].y, P[i].z, nn.x, nn.y, nn.z};
                std::copy(e, e + 6, &vb[i * 6]);
            }
            gl::GenVertexArrays(1, &vao[s]);
            gl::BindVertexArray(vao[s]);
            gl::GenBuffers(1, &vbo[s]);
            gl::BindBuffer(GL_ARRAY_BUFFER, vbo[s]);
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(vb.size() * 4), vb.data(), GL_STATIC_DRAW);
            gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
            gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
            gl::EnableVertexAttribArray(0);
            gl::EnableVertexAttribArray(1);
            if (!ibo) {
                gl::GenBuffers(1, &ibo);
                gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
                gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.idx.size() * 4), m.idx.data(), GL_STATIC_DRAW);
            } else {
                gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
            }
            gl::BindVertexArray(0);
        }
    }

    // Renders the frame (before the palette) into dst; returns the palette state to show.
    ExePalette render(Ctx& c, int sel, float v, const Target& dst) {
        const float start = sel == 0 ? kAlexStart : kVicStart;
        const int S = sel == 0 ? 0x540 : 0xc00;
        const float t0 = sel == 0 ? 0.0f : 4.0f * float((kAlexEnd - kAlexStart) * kHz);  // spin_t is never reset
        const double tk = std::max(0.0, double(v - start) * kHz);
        const int K = int(tk);
        CreditSim s(sel == 0 ? 255843u : 1125054u, exeJitter(c));  // seeds whose kicks match the capture's blur pumping
        for (int k = 1; k <= K; k++) {
            float vk = start + float(k / kHz);
            s.tick(musicPos(vk), S);
        }
        CreditSim nx = s;  // one tick ahead, for a smooth blur between ticks
        nx.tick(musicPos(start + float((K + 1) / kHz)), S);
        const float fr = float(tk - K);
        const float zf = float(s.z) + (float(nx.z) - float(s.z)) * fr;
        const float bs = 1.0f + std::min(std::fabs(zf) / 128.0f, 60.0f);
        const float gain = 0.8f * (65535.0f - 1100.0f * bs) / 65536.0f;

        const Texture& env = c.tex("phong");
        const Texture& cred = c.tex("credits");
        dst.bind();
        setBlend(Blend::None);
        photoSh.use();
        photo[sel].bind(0);
        pal[sel].bind(1);
        cred.bind(2);
        grain.bind(3);
        photoSh.set("photo", 0);
        photoSh.set("pal", 1);
        photoSh.set("credits", 2);
        photoSh.set("grain", 3);
        photoSh.set("bs", bs);
        photoSh.set("gain", gain);
        const int fstep = int((v - start) * 35.0f);  // new grain offsets every rendered frame (~35 fps)
        float go[4];
        for (int i = 0; i < 4; i++) go[i] = float(int(hash1(float(fstep) * 4.123f + float(i) * 1.37f + float(sel)) * 16384.0f) & 0x3fff);
        photoSh.set("goff", go[0], go[1], go[2], go[3]);
        // words: k = sel + 3 j, shown while 3 * (n % 3) > k
        int nw = 0;
        float wd[8] = {}, jt[72] = {};
        for (int j = 0; j < 2; j++) {
            int k = sel + 3 * j;
            if (3 * (s.n % 3) <= k) break;
            for (int q = 0; q < 4; q++) wd[j * 4 + q] = words[k][q];
            for (int p = 0; p < 9; p++) {
                int pp = p < 8 ? p : 4;
                jt[(j * 9 + p) * 4] = float(s.J[16 * j + 2 * pp]);
                jt[(j * 9 + p) * 4 + 1] = float(s.J[16 * j + 2 * pp + 1]);
            }
            nw++;
        }
        photoSh.set("nw", nw);
        gl::Uniform4fv(photoSh.loc("word"), 2, wd);
        gl::Uniform4fv(photoSh.loc("jit"), 18, jt);
        drawFullscreen();

        // spikey: Rz(t/700) Ry(t/243), t += 4 per tick
        const float t = t0 + 4.0f * float(tk);
        M3 R = rotAB(t / 700.0f, t / 243.0f);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        gl::Enable(GL_DEPTH_TEST);
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_BACK);
        objSh.use();
        env.bind(0);
        objSh.set("env", 0);
        objSh.set("r0", R.row(0));
        objSh.set("r1", R.row(1));
        objSh.set("r2", R.row(2));
        objSh.set("offs", offs[sel][0], offs[sel][1], offs[sel][2]);
        gl::BindVertexArray(vao[sel]);
        gl::DrawElements(GL_TRIANGLES, nIdx, GL_UNSIGNED_INT, nullptr);
        gl::BindVertexArray(0);
        gl::Disable(GL_CULL_FACE);
        gl::Disable(GL_DEPTH_TEST);

        ExePalette p = s.pal;
        if (sel == 1) p.level = s.shown;  // vic: the palette is uploaded before the strobe re-arms it
        if (s.n >= 3) p.level = 0x8000;   // part over: the next part starts from white
        else if (sel == 0) p.frac = fr;   // blend the fade towards the next tick
        return p;
    }
};

// ================================================================ 0x2b908 + 0x2d81c: tentacles and squares
const char* kWireVS = R"(
layout(location = 0) in vec2 pos;
layout(location = 1) in float across;
layout(location = 2) in float val;
layout(location = 3) in float alpha;
out float a, v, al;
void main() {
    a = across; v = val; al = alpha;
    gl_Position = vec4(pos.x / 160.0 - 1.0, 1.0 - pos.y / 100.0, 0.0, 1.0);
}
)";
const char* kWireFS = R"(
in float a, v, al;
out vec4 frag;
void main() { frag = vec4(v * max(0.0, 1.0 - abs(a)), 0.0, 0.0, al); }
)";
const char* kTexTriVS = R"(
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 tex;
noperspective out vec2 tc;
void main() {
    tc = tex;
    gl_Position = vec4(pos.x / 160.0 - 1.0, 1.0 - pos.y / 100.0, 0.0, 1.0);
}
)";
const char* kTexTriFS = R"(
noperspective in vec2 tc;
out vec4 frag;
uniform sampler2D env;
void main() { frag = vec4(texture(env, tc).rgb, 1.0); }
)";
const char* kFishFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D l1, col, l2;
uniform int mode;
uniform vec3 dev[32];          // fish.fad rows 32..63 applied to black, minus a plain lerp to white
vec3 devAt(float v) {
    v = clamp(v, 0.0, 31.0);
    int i = int(min(v, 30.0));
    return mix(dev[i], dev[i + 1], v - float(i));
}
vec3 lighten(vec3 c, float v) { return clamp(mix(c, vec3(1.0), v / 31.0) + devAt(v) * (1.0 - luma(c)), 0.0, 1.0); }
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float m = (px.x >= 2.0 && px.x < 318.0 && px.y >= 2.0 && px.y < 198.0) ? 1.0 : 0.0;   // 0x2acf0 clips lines
    vec3 c = mode == 0 ? lighten(vec3(0.0), min(texture(l1, uv).r * m, 31.0)) : texture(col, uv).rgb;
    c = lighten(c, min(2.0 * texture(l2, uv).r * m, 31.0));   // 0x1c490: squares x2 through fish.fad
    frag = vec4(exePalette(c), 1.0);
}
)";

constexpr float kFrameHz = 35.0f;  // the original's frame rate here (spin speed of the delay line)
constexpr float kLineV = 4.0f, kLineW = 1.5f;

struct Squares {
    Vec3 p[256];
    void gen(uint32_t seed) {
        ExeRand r(seed);
        for (Vec3& q : p) {
            float x = float(r.next() % 41 - 20), y = float(r.next() % 41 - 20), z = float(r.next() % 41 - 20);
            q = Vec3(x, y, z);
        }
    }
};
Vec3 squareCam(float T) {
    float A = 5.0f * std::sin(T / 1500.0f + 435.0f), B = 5.0f * std::sin(T * 0.000613496933f + 1435.0f);
    return Vec3(std::cos(A) * std::sin(B), std::cos(A) * std::cos(B), std::sin(A)) * 30.0f;
}

struct TentaclePart {
    Shader wireSh, texSh, fishSh, palSh;
    DynMesh wire, tri;
    LineBatch lines;
    std::vector<Vec3> base;
    std::vector<int> group;
    std::vector<unsigned> idx;
    std::vector<float> dev;
    bool ready = false;
    Squares sqA, sqC;

    void init(Ctx& c) {
        if (ready) return;
        ready = true;
        wireSh.build(kWireVS, kWireFS, "hidden-line wire");
        texSh.build(kTexTriVS, kTexTriFS, "tentacle tex");
        fishSh.build(kFullscreenVS, std::string(kExePaletteGLSL) + kFishFS, "fish combine");
        wire.init({2, 1, 1, 1});
        tri.init({2, 2});
        const Mesh& m = c.meshData("icosa");
        size_t nv = m.pos.size() / 3;
        base.resize(nv);
        group.resize(nv);
        for (size_t i = 0; i < nv; i++) {
            base[i] = Vec3(m.pos[i * 3], m.pos[i * 3 + 1], m.pos[i * 3 + 2]) * (1.0f / 120.0f);
            group[i] = std::clamp(int(m.extra[i]), 0, 16);
        }
        idx = m.idx;
        dev = fadDarkTint(c, "fish.fad", "sphere.raw");
        sqA.gen(303);
        sqC.gen(1997);
    }

    // star == false: spiral (A, wireframe); true: star (C, env-mapped)
    ExePalette render(Ctx& c, bool star, float v) {
        const Texture& env = c.tex("sphere");
        const float start = star ? kStarStart : kSpiralStart;
        const double ticksA = double(kVicStart - 0.07f - kSpiralStart) * kHz;  // A's callbacks stop at its end
        const double tk = std::max(0.0, double(v - start) * kHz);
        // palette: white -> image (A: step 1000, C: step 500)
        ExePalette pal;
        pal.level = 0x8000;
        pal.target = 0x4000;
        pal.speed = star ? 500 : 1000;
        for (int k = 0; k < int(tk); k++) pal.tick();
        pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        // camera (0x2d81c): orbit at radius 30, lazy look at the origin, C reset at the part init
        M3 C;
        const int K = int(tk);
        const float T0 = 2354.0f + 6.0f * float(star ? ticksA : 0.0);
        for (int k = 1; k <= K; k++) lookStep(C, squareCam(T0 + 6.0f * float(k)) * -1.0f, 0.1f);
        {  // the camera turns once per tick: blend towards the next tick's orientation, as the position does
            M3 C1 = C;
            lookStep(C1, squareCam(T0 + 6.0f * float(K + 1)) * -1.0f, 0.1f);
            C = lerpRot(C, C1, float(tk - double(K)));
        }
        const Vec3 campos = squareCam(T0 + 6.0f * float(tk));
        // orientation delay line (0x2b908): O(k+1) = O(k) R(v(t_k)), shell g uses O(k - g)
        const float tau = float(v - start) * kFrameHz;
        const int KF = int(tau);
        const float ff = tau - float(KF);
        const float t0 = 10.0f * float(star ? ticksA : 0.0);
        std::vector<M3> O(size_t(KF) + 2);
        std::vector<Vec3> stepAx(size_t(KF) + 2);
        std::vector<float> stepAng(size_t(KF) + 2, 0.0f);
        for (int k = 0; k <= KF; k++) {
            float t = t0 + 10.0f * float(k) * float(kHz) / kFrameHz;
            Vec3 w = Vec3(std::sin(t / 700.0f), std::sin(t * 0.00107411386f), std::sin(t * 0.002f)) * 0.12f;
            float L = w.len();
            stepAx[size_t(k)] = L > 1e-8f ? w * (1.0f / L) : Vec3(1, 0, 0);
            stepAng[size_t(k)] = L > 1e-8f ? std::asin(std::min(L, 1.0f)) : 0.0f;
            O[size_t(k) + 1] = O[size_t(k)] * rodrigues(stepAx[size_t(k)], stepAng[size_t(k)]);
        }
        M3 Mg[17];
        for (int g = 0; g < 17; g++) {
            int j = KF - g;  // M[g] = O(j) advanced by the fraction of the current step
            if (j < 0) Mg[g] = M3();
            else Mg[g] = O[size_t(j)] * rodrigues(stepAx[size_t(j)], stepAng[size_t(j)] * ff);
        }
        // transform (0x2b394), normals (0x2b570)
        const Vec3 P = C * campos;
        const size_t nv = base.size();
        std::vector<Vec3> cam(nv), nacc(nv);
        std::vector<float> sx(nv), sy(nv);
        for (size_t i = 0; i < nv; i++) {
            cam[i] = Mg[group[i]] * base[i] - P;
            float z = std::max(cam[i].z, 1e-3f);
            sx[i] = 160.0f + cam[i].x * 300.0f / z;
            sy[i] = 100.0f + cam[i].y * 300.0f / z;
        }
        const size_t nf = idx.size() / 3;
        for (size_t f = 0; f < nf; f++) {
            unsigned a = idx[f * 3], b = idx[f * 3 + 1], d = idx[f * 3 + 2];
            Vec3 N = (cam[a] - cam[b]).cross(cam[d] - cam[b]);  // inward
            nacc[a] = nacc[a] + N;
            nacc[b] = nacc[b] + N;
            nacc[d] = nacc[d] + N;
        }
        // all faces land in depth bucket 0, so they are drawn in reverse list order (0x2b780), front faces only
        std::vector<float> vb;
        vb.reserve(nf * 21 * 5);
        for (size_t ff2 = nf; ff2-- > 0;) {
            unsigned a = idx[ff2 * 3], b = idx[ff2 * 3 + 1], d = idx[ff2 * 3 + 2];
            if (cam[a].z <= 20.0f || cam[b].z <= 20.0f || cam[d].z <= 20.0f) continue;
            float cr = (sx[b] - sx[a]) * (sy[d] - sy[b]) - (sy[b] - sy[a]) * (sx[d] - sx[b]);
            if (cr >= 0.0f) continue;
            if (!star) {
                const unsigned vv[3] = {a, b, d};
                for (unsigned q : vv) {  // 0x1f88b fills the face with 0 (hidden-line removal)
                    float e[5] = {sx[q], sy[q], 0.0f, 0.0f, 0.0f};
                    vb.insert(vb.end(), e, e + 5);
                }
                lineQuad(vb, sx[a], sy[a], sx[b], sy[b], kLineV, kLineW);
                lineQuad(vb, sx[d], sy[d], sx[b], sy[b], kLineV, kLineW);
                lineQuad(vb, sx[a], sy[a], sx[d], sy[d], kLineV, kLineW);
            } else {
                const unsigned vv[3] = {a, b, d};
                for (unsigned q : vv) {
                    Vec3 n = nacc[q] * (1.0f / std::max(nacc[q].len(), 0.001f));
                    float e[4] = {sx[q], sy[q], (n.x * 120.0f + 128.0f) / 256.0f, (n.y * 120.0f + 128.0f) / 256.0f};
                    vb.insert(vb.end(), e, e + 4);
                }
            }
        }
        Target& L1 = c.scratch[1];
        Target& col = c.scratch[0];
        Target& L2 = c.scratch[2];
        if (!star) {
            L1.bind();
            gl::ClearColor(0, 0, 0, 0);
            gl::Clear(GL_COLOR_BUFFER_BIT);
            gl::Enable(GL_BLEND);
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFunc(GL_ONE, GL_SRC_ALPHA);  // fills (alpha 0) erase, lines (alpha 1) add
            wireSh.use();
            wire.draw(vb);
            setBlend(Blend::None);
        } else {
            col.bind();
            gl::ClearColor(0, 0, 0, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT);
            setBlend(Blend::None);
            texSh.use();
            env.bind(0);
            texSh.set("env", 0);
            tri.draw(vb);
        }
        // squares (0x2d81c): unit squares in world xy planes at the integer points, glow lines
        L2.bind();
        gl::ClearColor(0, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        const Squares& sq = star ? sqC : sqA;
        const Vec3 ex = C.col(0), ey = C.col(1);
        for (const Vec3& p : sq.p) {
            Vec3 d = p - campos;
            Vec3 A(C.row(0).dot(d), C.row(1).dot(d), C.row(2).dot(d));
            if (A.z <= 5.0f) continue;
            Vec3 B = A + ex, Cc = B + ey, D = A + ey;
            if (B.z <= 0.5f || Cc.z <= 0.5f || D.z <= 0.5f) continue;
            auto pr = [](Vec3 q, float& x, float& y) { x = 160.0f + 300.0f * q.x / q.z; y = 100.0f + 300.0f * q.y / q.z; };
            float ax, ay, bx, by, cx, cy, dx, dy;
            pr(A, ax, ay);
            pr(B, bx, by);
            pr(Cc, cx, cy);
            pr(D, dx, dy);
            lines.add(ax, ay, bx, by, kLineV, kLineW);
            lines.add(ax, ay, dx, dy, kLineV, kLineW);
            lines.add(cx, cy, bx, by, kLineV, kLineW);
            lines.add(cx, cy, dx, dy, kLineV, kLineW);
        }
        lines.draw();
        gl::ClearColor(0, 0, 0, 1);
        // combine and palette
        c.out->bind();
        setBlend(Blend::None);
        fishSh.use();
        L1.bindTex(0);
        col.bindTex(1);
        L2.bindTex(2);
        fishSh.set("l1", 0);
        fishSh.set("col", 1);
        fishSh.set("l2", 2);
        fishSh.set("mode", star ? 1 : 0);
        gl::Uniform3fv(fishSh.loc("dev"), 32, dev.data());
        pal.apply(fishSh);
        drawFullscreen();
        return pal;
    }

    // camera state at the end of the star part (the plasma part starts from it)
    void starEndCamera(M3& C, Vec3& pos) const {
        const double ticksA = double(kVicStart - 0.07f - kSpiralStart) * kHz;
        const double tk = double(kPlasmaStart - 0.07f - kStarStart) * kHz;
        const float T0 = 2354.0f + 6.0f * float(ticksA);
        C = M3();
        for (int k = 1; k <= int(tk); k++) lookStep(C, squareCam(T0 + 6.0f * float(k)) * -1.0f, 0.1f);
        pos = squareCam(T0 + 6.0f * float(int(tk)));
    }
};

// ================================================================ plasma D/E/F
const char* kPlasmaFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D floorTex, goldTex, blueTex, pinkTex, rayTex, noiseTex;
uniform vec3 P, R0, R1, R2;            // camera position and axes (right, down, forward)
uniform int nballs;                    // metaballs (world units)
uniform vec4 mb[4];                    // xyz centre, w strength
uniform vec3 bc;                       // plasma ball centre
uniform vec2 rad;                      // radii of the blue and the pink ball
uniform vec3 m1a, m1b, m2a, m2b;       // texture rotation rows of both balls
uniform float bT, bLvl;                // texture scroll T, fade level / 32
uniform int sprite;
uniform vec4 sprRect;                  // centre x, y, half width, half height (original pixels)
uniform float sprS, sprT, sprNoise;
float field(vec3 p) {
    float f = 0.0;
    for (int k = 0; k < 4; k++) {
        if (k >= nballs) break;
        vec3 d = p - mb[k].xyz;
        f += 16.0 * mb[k].w / (dot(d, d) + 16e-5);
    }
    return f;
}
vec3 addSat(vec3 a, vec3 b) { return min(a + b, vec3(1.0)); }   // 3d.gst
vec3 ball(vec3 o, vec3 d, float R, vec3 ra, vec3 rb, sampler2D tex) {
    if (R <= 0.0) return vec3(0.0);
    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - R * R;
    float disc = b * b - a * c;
    if (disc < 0.0) return vec3(0.0);
    float l = (-b + sqrt(disc)) / a;          // far root: the back inner wall
    if (l <= 0.0) return vec3(0.0);
    vec3 n = (o + l * d) * 300.0 / R;
    vec2 t = vec2(dot(n, ra) + 0.1 * bT, dot(n, rb));
    return texture(tex, t / 256.0).rgb * bLvl;
}
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec3 d = (px.x - 160.0) * R0 + (px.y - 100.0) * R1 + 300.0 * R2;
    // corridor (0x27014 / 0x1cddc): planes y = +-290, texel = world (x, z) / 2, light min(1, 1406 / depth)
    vec3 col = vec3(0.0);
    if (abs(d.y) > 1e-6) {
        float t = (290.0 - P.y) / d.y;
        if (t < 0.0) t = (-290.0 - P.y) / d.y;
        if (t > 0.0) {
            vec3 h = P + t * d;
            float z = t * 300.0;
            col = texture(floorTex, vec2(h.x, h.z) / 512.0).rgb * clamp(45000.0 / z, 1.0, 32.0) / 32.0;
        }
    }
    // metaballs (0x26c14): iso 0.2 of sum w / r^2 in 4-unit grid cells, env-mapped by the inward normal
    if (nballs > 0) {
        vec3 dn = normalize(d);
        float wsum = 0.0;
        vec3 cen = vec3(0.0);
        for (int k = 0; k < 4; k++) { if (k >= nballs) break; wsum += mb[k].w; cen += mb[k].xyz; }
        cen /= float(nballs);
        float rmax = sqrt(80.0 * wsum);
        float bR = rmax + 40.0;
        vec3 oc = P - cen;
        float b = dot(oc, dn), cc = dot(oc, oc) - bR * bR, disc = b * b - cc;
        if (wsum > 1e-4 && disc > 0.0) {
            float s = max(-b - sqrt(disc), 0.0), e = -b + sqrt(disc);
            bool hit = false;
            for (int i = 0; i < 160 && s < e; i++) {
                vec3 p = P + dn * s;
                if (field(p) > 0.2) { hit = true; break; }
                float mr = 1e9;
                for (int k = 0; k < 4; k++) { if (k >= nballs) break; mr = min(mr, length(p - mb[k].xyz)); }
                s += max(0.25, mr - rmax);
            }
            if (hit) {
                float lo = s - 0.25, hi = s;
                for (int i = 0; i < 8; i++) { float m = 0.5 * (lo + hi); if (field(P + dn * m) > 0.2) hi = m; else lo = m; }
                vec3 v = P + dn * hi;
                if (dot(v - P, R2) > 40.0) {
                    vec3 N = vec3(0.0);
                    for (int k = 0; k < 4; k++) {
                        if (k >= nballs) break;
                        vec3 q = mb[k].xyz - v;
                        float l = length(q);
                        N += q / max(l * l * l, 1e-5);
                    }
                    N = normalize(N);
                    col = texture(goldTex, (128.0 + 126.0 * vec2(dot(N, R0), dot(N, R1))) / 256.0).rgb;
                }
            }
        }
    }
    // the two plasma balls (0x28100 / 0x27958), additive
    vec3 dc = (px.x - 160.0) * vec3(1, 0, 0) + (px.y - 100.0) * vec3(0, 1, 0) + vec3(0, 0, 300);
    vec3 o = vec3(dot(P - bc, R0), dot(P - bc, R1), dot(P - bc, R2));
    col = addSat(col, ball(o, dc, rad.x, m1a, m1b, blueTex));
    col = addSat(col, ball(o, dc, rad.y, m2a, m2b, pinkTex));
    // gold ray sprite (0x28330 / 0x27f58): polar table x tex3d page 4
    if (sprite != 0) {
        vec2 q = px - sprRect.xy;
        if (abs(q.x) <= sprRect.z && abs(q.y) <= sprRect.w) {
            float ang = fract(atan(q.x, q.y) / (2.0 * PI)) * 256.0;
            float r = min(255.0, length(q));
            float s1 = sin(0.0453 * sprT + ang * PI / 8.0);
            float A = max(0.0, 30.0 * s1 * s1 - 15.0 + 7.0 * sin(0.142 * sprT + 43.0));
            float s2 = sin(ang * PI / 32.0 + 12.0 - 0.1345 * sprT);
            float B = max(0.0, 100.0 * s2 * s2 - 70.0);
            float N = texture(noiseTex, vec2((ang + sprNoise + 0.5) / 256.0, 0.5)).r * 255.0;
            float row = min(255.0, (A + B + 30.0 + N) * sprS);
            col = addSat(col, textureLod(rayTex, vec2((r + 0.5) / 256.0, (row + 0.5) / 256.0), 0.0).rgb);
        }
    }
    frag = vec4(exePalette(col), 1.0);
}
)";

struct Metaball {
    Vec3 dir;
    float d = 0.0f;
    float f[3], ph[3];
};

constexpr int kFLoad = 12;  // ticks (~0.17 s) between the end of E and F's first tick

struct PlasmaSim {
    // camera (0x27444)
    M3 M;
    Vec3 P, V, S, Tg{-1000, 0, 60}, Bc{-1000, 0, 60};
    float turn = 0.05f, damp = 0.6f, roll = 0.0f, tau = 0.0f;
    // D (0x2e3ac)
    int s = -500, c = -100, lvl = 0, phase = 0, ticks = 0;
    float w[4] = {1, 1, 1, 1};
    // E (0x2e360)
    float g = -0.8f, scale = 0.0f;
    // F (0x2e05c)
    int e = 0;
    float speed = 0.0f, fw = 1.0f, ph = 0.0f;
    bool firstFlash = true;
    // balls (0x280cc), metaballs
    float sprT = 0.0f;
    int noiseOff = 0, bT = 1200, n = 0;
    Metaball mb[4];
    ExePalette pal;
    int phaseTicks = 0;

    void path() {
        if (phase < 2) {  // 0x27804: orbit around the ball site
            float R = phase == 0 ? 230.0f : 300.0f;
            float r = R + R / 6.0f * std::sin(tau * 0.0005f + 64.0f);
            float A = tau / 1100.0f, B = tau / 700.0f + 34.0f;
            S = Bc + Vec3(std::sin(B) * std::cos(A), std::sin(B) * std::sin(A), std::cos(B)) * r;
        } else {  // 0x276f8: x, y only
            float r = 50.0f * std::sin(0.0005f * tau + 64.0f) + 290.0f;
            float sb = std::sin(3.0f * tau / 700.0f + 34.0f), a = 3.0f * 0.000909090909f * tau;
            S.x = Bc.x + r * sb * std::cos(a);
            S.y = Bc.y + r * sb * std::sin(a);
        }
    }
    void tick() {
        ticks++;
        phaseTicks++;
        pal.tick();
        if (ticks == 1) {  // 0x2e684: switches back to solid mode, white flash
            pal.level = 0x8000;
            pal.target = 0x4000;
            pal.speed = 1000;
        }
        tau += 4.0f;
        n += 3;
        for (Metaball& b : mb)
            if (b.d > 0) b.d *= 0.99f;
        sprT += 0.23f;
        noiseOff++;
        bT += 7;
        if (phase == 0) {
            s++;
            c = std::min(s / 10, 0);
            lvl = std::clamp(s / 10 + 32, 0, 32);
            if (s > -100)
                for (float& x : w) x = std::max(0.0f, x - 0.003f);
            if (w[1] <= 0.001f) {  // end of D
                phase = 1;
                phaseTicks = 0;
                lvl = 32;
            }
        } else if (phase == 1) {
            g = std::min(g + 0.008f, 1.0f);
            scale = std::max(g, 0.0f);
            if (scale >= 1.0f) {
                phase = 2;
                phaseTicks = 0;
            }
        } else if (phaseTicks > kFLoad) {  // F's callback starts after 0x2902c has loaded phong1.raw
            ph += 0.01f;
            if (roll > -0.02f) roll -= 0.0001f;
            if (e > 900) {
                turn = 0.05f;
                Bc = Vec3(0, 0, 0);
                Tg.x = Tg.y = 0.0f;
                P.z += fw;
                fw += 0.1f;
                roll = 0.04f * std::sin(ph);
                Tg.z = float(e + 15000);
                e += 2;
                if (e > 1000) {
                    pal.target = 0x4000;
                    pal.speed = 150;
                }
            } else if (P.z > 1500.0f) {
                if (firstFlash) {
                    pal.level = 0x8000;
                    pal.target = 0x4000;
                    pal.speed = 400;
                    firstFlash = false;
                }
                scale += 0.2f;
                e += 7;
                if (P.z > 1700.0f) {
                    pal.speed = 100;
                    pal.target = 0x8000;
                }
            }
            if (speed < 6.0f) speed += 0.02f;
            P.z += speed;
            if (P.z > 500.0f) scale += 0.01f;
            c = e;
        }
        // camera integrator, once per tick
        path();
        Vec3 d = Tg - P;
        lookStep(M, d, turn, roll);
        int nc = phase < 2 ? 3 : 2;
        float* Vp = &V.x;
        float* Pp = &P.x;
        const float* Sp = &S.x;
        for (int i = 0; i < nc; i++) {
            Vp[i] = Vp[i] * damp + (Sp[i] - Pp[i]) * 0.002f;
            Pp[i] += Vp[i];
        }
    }
};

struct PlasmaPart {
    Shader sh;
    Texture noise;
    bool ready = false;
    Metaball mb0[4];
    void init(Ctx&) {
        if (ready) return;
        ready = true;
        sh.build(kFullscreenVS, std::string(kExePaletteGLSL) + kPlasmaFS, "plasma");
        ExeRand r(4711);
        std::vector<uint8_t> nz(256 * 4);
        for (int i = 0; i < 256; i++) nz[size_t(i) * 4] = uint8_t(r.next() & 15);
        noise = makeTex(256, 1, nz, false, true);  // one value per 1/256 turn: thin rays
        for (Metaball& b : mb0) {  // 0x268bc
            float a = float(r.next()) * kPi / 32768.0f, bb = float(r.next()) * kPi / 65536.0f;
            b.dir = Vec3(std::cos(a) * std::sin(bb), std::sin(a) * std::sin(bb), std::cos(bb));
            b.d = 0.0f;  // all four have arrived by the end of the blobs part
            for (float& f : b.f) f = float(r.next() / 250 + 48);
            for (float& p : b.ph) p = float(r.next());
        }
    }

    void render(Ctx& c, float v, const TentaclePart& tent) {
        const Texture& tFloor = c.tex("tex3d#1");
        const Texture& tGold = c.tex("tex3d#0");
        const Texture& tBlue = c.tex("tex3d#2");
        const Texture& tPink = c.tex("tex3d#3");
        const Texture& tRay = c.tex("tex3d#4");
        PlasmaSim s;
        tent.starEndCamera(s.M, s.P);
        s.tau = kTau0;
        s.n = 52000;
        s.sprT = 300.0f;
        s.bT = 23000;
        for (int i = 0; i < 4; i++) s.mb[i] = mb0[i];
        s.pal.level = 0x8000;
        s.pal.target = 0x4000;
        s.pal.speed = 100;
        const double tk = std::max(0.0, double(v - kPlasmaStart) * kHz);
        for (int k = 0; k < int(tk); k++) s.tick();
        s.pal.frac = float(tk - std::floor(tk));  // blend the fade towards the next tick
        // The simulation steps once per 70 Hz tick: show it blended towards the next tick, so the camera, the
        // balls and the sprite move smoothly at any frame rate. Discrete state (phase, palette) is the current tick's.
        PlasmaSim s1 = s;
        s1.tick();
        s1.path();
        s.path();
        const float f = float(tk - std::floor(tk));
        auto mixf = [f](float a, float b) { return a + (b - a) * f; };
        auto mixv = [f](const Vec3& a, const Vec3& b) { return a + (b - a) * f; };
        const Vec3 P = mixv(s.P, s1.P), Bc = mixv(s.Bc, s1.Bc);
        const M3 M = lerpRot(s.M, s1.M, f);

        c.out->bind();
        setBlend(Blend::None);
        sh.use();
        tFloor.bind(0);
        tGold.bind(1);
        tBlue.bind(2);
        tPink.bind(3);
        tRay.bind(4);
        noise.bind(5);
        sh.set("floorTex", 0);
        sh.set("goldTex", 1);
        sh.set("blueTex", 2);
        sh.set("pinkTex", 3);
        sh.set("rayTex", 4);
        sh.set("noiseTex", 5);
        sh.set("P", P);
        sh.set("R0", M.row(0));
        sh.set("R1", M.row(1));
        sh.set("R2", M.row(2));
        // metaballs (D only)
        int nb = s.phase == 0 ? 4 : 0;
        float mbv[16] = {};
        const Vec3 cg = Bc * 0.25f;
        const float nn = mixf(float(s.n), float(s1.n));
        for (int k = 0; k < nb; k++) {
            const Metaball& b = s.mb[k];
            const float wk = mixf(s.w[k], s1.w[k]), dk = mixf(b.d, s1.mb[k].d);
            float amp = 3.8f * wk / (1.0f + dk);
            Vec3 p = cg + b.dir * dk + Vec3(std::sin(nn / b.f[0] + b.ph[0]), std::sin(nn / b.f[1] + b.ph[1]), std::sin(nn / b.f[2] + b.ph[2])) * amp;
            mbv[k * 4] = p.x * 4.0f;
            mbv[k * 4 + 1] = p.y * 4.0f;
            mbv[k * 4 + 2] = p.z * 4.0f;
            mbv[k * 4 + 3] = wk;
        }
        sh.set("nballs", nb);
        gl::Uniform4fv(sh.loc("mb"), 4, mbv);
        // plasma balls
        const float T = mixf(float(s.bT), float(s1.bT));
        // D grows the balls' radius and brightness by one step every 10 ticks (c = s / 10, lvl = s / 10 + 32);
        // continuous here (+0.45: the mean of C's truncation towards zero), so the balls fade in without steps
        float cR = mixf(float(s.c), float(s1.c)), lvlF = mixf(float(s.lvl), float(s1.lvl));
        if (s.phase == 0 && s1.phase == 0) {
            const float q = mixf(float(s.s), float(s1.s)) / 10.0f + 0.45f;
            cR = std::min(q, 0.0f);
            lvlF = std::clamp(q + 32.0f, 0.0f, 32.0f);
        }
        float r1 = std::max(0.0f, cR + 60.0f - 6.0f * std::sin(25.0f * 9.43e-5f * T + 43.0f));
        float r2 = std::max(0.0f, cR + 68.0f + 6.0f * std::cos(25.0f * 5.3e-5f * T + 23.0f));
        M3 m1 = rotAB(0.001f * T, 0.000732f * T), m2 = rotAB(4.0f + 0.001124f * T, 7.0f - 0.000532f * T);
        sh.set("bc", Bc);
        sh.set("rad", r1, r2);
        sh.set("m1a", m1.row(0));
        sh.set("m1b", m1.row(1));
        sh.set("m2a", m2.row(0));
        sh.set("m2b", m2.row(1));
        sh.set("bT", T);
        sh.set("bLvl", lvlF / 32.0f);
        // sprite (E, F)
        int spr = 0;
        if (s.phase >= 1) {
            Vec3 d = Bc - P;
            float X = M.row(0).dot(d), Y = M.row(1).dot(d), Z = M.row(2).dot(d);
            if (Z > 40.0f) {
                float k = 300.0f / Z, sc = mixf(s.scale, s1.scale) * k;
                float w = 88.0f * sc, h = std::min(w, 200.0f);
                w = std::min(w, 320.0f);
                sh.set("sprRect", 160.0f + X * k, 100.0f + Y * k, w, h);
                sh.set("sprS", sc);
                spr = 1;
            }
        }
        sh.set("sprite", spr);
        sh.set("sprT", mixf(s.sprT, s1.sprT));
        sh.set("sprNoise", float(s.noiseOff & 255) + f);  // the rays turn 1/256 per tick: smoothly in between
        s.pal.apply(sh);
        drawFullscreen();
    }
    static constexpr float kTau0 = 39300.0f;  // camera orbit phase at D (unknown, fitted to the capture)
};

// ================================================================ engine
struct Engine {
    CreditPart credits;
    TentaclePart tent;
    PlasmaPart plasma;
    Shader palSh;
    bool ready = false;
    void init(Ctx& c) {
        if (ready) return;
        ready = true;
        credits.init(c);
        tent.init(c);
        plasma.init(c);
        palSh.build(kFullscreenVS, std::string(kExePaletteGLSL) + kPalPassFS, "mid3 palette");
    }
    void white(Ctx& c) {
        c.out->bind();
        gl::ClearColor(1, 1, 1, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        gl::ClearColor(0, 0, 0, 1);
    }
    void creditFrame(Ctx& c, int sel, float v) {
        Target& f = c.scratch[0];
        ExePalette p = credits.render(c, sel, v, f);
        c.out->bind();
        setBlend(Blend::None);
        palSh.use();
        f.bindTex(0);
        palSh.set("src", 0);
        p.apply(palSh);
        drawFullscreen();
    }
    void draw(Ctx& c) {
        const float v = exeTime(c.T);
        if (v < 100.0f) {
            if (v < kAlexStart) white(c);
            else creditFrame(c, 0, v);
        } else if (v < kSpiralStart) white(c);
        else if (v < kVicStart) tent.render(c, false, v);
        else if (v < kStarStart) creditFrame(c, 1, v);
        else if (v < kPlasmaStart) tent.render(c, true, v);
        else plasma.render(c, v, tent);
        setBlend(Blend::None);
    }
};
Engine& engine() {
    static Engine e;
    return e;
}

struct AlexScene : Scene {
    void init(Ctx& c) override { engine().init(c); }
    void render(Ctx& c, float, float) override { engine().draw(c); }
};
struct SpiralScene : AlexScene {};
struct VicScene : AlexScene {};
struct StarScene : AlexScene {};
struct PlasmaScene : AlexScene {};
REGISTER_SCENE(AlexScene, "alex");
REGISTER_SCENE(SpiralScene, "spiral");
REGISTER_SCENE(VicScene, "vic");
REGISTER_SCENE(StarScene, "star");
REGISTER_SCENE(PlasmaScene, "plasma");

}  // namespace
