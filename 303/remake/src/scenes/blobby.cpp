// 0:55 - 1:05  The blobby part (one effect whose look switches at random).
#include <cmath>
#include <iterator>

#include "../exefx.h"

namespace {
// ---------------------------------------------------------------------------------------------
// 0:55 - 1:05  Blobby, ported from the exe (main script 0x31142, 0x182e4 render, callback 0x181c4,
// 0x29d1c inset, 0x2a34c lyrics). Background: blobby.raw through the same sphere-ray grid as the
// intro at scale 4. Object: spikey.dat morphing from a sphere (r = 170 + (r0 - 170) sin(t / 700)),
// drawn either as bands of constant view depth or environment-mapped. A mode counter picks the
// texture (bit 0) and the object look (bit 1); it steps at random 8-row boundaries with a white flash.
// In the capture it stepped once, at 0x4c0, so that is scripted here.
const char* kBlobbyBgFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec3 R0, R1, R2;
uniform float focal, off;
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    frag = vec4(texture(tex, sphereRayTexel(px, R0, R1, R2, focal, 4.0, off) / 256.0).rgb, 1.0);
}
)";
const char* kBlobbyObjVS = R"(
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
uniform vec3 r0, r1, r2, offs;
out vec3 vn;
out float vz;
void main() {
    vec3 q = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    vn = vec3(dot(r0, n), dot(r1, n), dot(r2, n));
    vz = q.z;
    vec3 w = q + offs;   // screen: x = 160 + 600 X / Z, y = 100 + 600 Y / Z
    gl_Position = vec4(3.75 * w.x, -6.0 * w.y, ((w.z - 300.0) / 2200.0 * 2.0 - 1.0) * w.z, w.z);
}
)";
const char* kBlobbyObjFS = R"(
in vec3 vn;
in float vz;
out vec4 frag;
uniform sampler2D env;
uniform int contour;
void main() {
    if (contour != 0) {   // texture C: column = view depth, white (60..63) where depth mod 32 is in 4..12
        float m = mod(vz, 32.0), aa = fwidth(vz) * 0.75 + 1e-3;
        float band = smoothstep(3.7 - aa, 3.7 + aa, m) * (1.0 - smoothstep(12.3 - aa, 12.3 + aa, m));
        frag = vec4(vec3(band * 0.97), 1.0);
    } else {
        vec3 nn = normalize(vn);
        frag = vec4(texture(env, (120.0 * nn.xy + 128.0) / 256.0).rgb, 1.0);
    }
}
)";

// lyric positions read off the capture (centre, frame units) for the placements of the cycle words 0..5
struct LyricPlace { float cx, cy; };
const LyricPlace kBlobbyPlaces[] = {{0.27f, 0.885f}, {0.34f, 0.855f}, {0.42f, 0.86f},
                                    {0.76f, 0.52f},  {0.40f, 0.17f},  {0.42f, 0.80f}};

struct BlobbyState {
    ExePalette pal;
    ExeRand rnd{4242};
    int t = 0, mode = 0;
    float pa = 0, pb = 0, pc = 1000;
    int lastKick = 0x47e, lastLyr = -1, lastBl = -1;
    int jit[32] = {0};
    float inset = 0;
    LyricState lyr;
    int idx = 0;
    bool newLyric = true, loaded = false;
    int ticks = 0;
    BlobbyState() {
        pal.level = 0x8000;  // white while loading
        pal.target = 0x4000;
        pal.speed = 0;
    }
    void placeWord(Ctx& c) {
        const int w = idx;
        lyr.word = lyricCycle(c)[size_t(w)];
        idx = (idx + 1) % 10;
        lyr.width = LyricLayer::wordWidth(c, lyr.word);
        lyr.ty = rnd.next() % 144;
        lyr.tx = rnd.next() % std::max(1, 320 - lyr.width);
        if (w < int(std::size(kBlobbyPlaces))) {
            const LyricPlace& p = kBlobbyPlaces[w];
            lyr.tx = int(p.cx * 320.0f) - lyr.width / 2;
            lyr.ty = int(p.cy * 200.0f) - 28;
        }
        lyr.zoom = false;
    }
    void tick(Ctx& c, int pos) {
        if (!loaded && ticks >= 10) {  // ~0.15 s of loading, then white -> palette in 16 ticks
            loaded = true;
            pal.speed = 1000;
        }
        ticks++;
        pal.tick();
        if (pos != lastKick) {  // 0x2e798
            lastKick = pos;
            if ((pos & 3) == 0 && (rnd.next() & 15) < 7) pal.mult = 1.2f;
            if ((pos & 3) == 0 && (rnd.next() & 15) < 2) pal.mult = 0.9f;
            if ((pos & 0xff) > 0xef) {
                pal.flash(1000);
                pal.invert = true;
            } else {
                pal.invert = false;
            }
        }
        if (pos != lastLyr) {  // 0x2a0d4 (cycle mode)
            lastLyr = pos;
            if ((pos & 3) == 0) {
                if ((rnd.next() & 15) == 0) lyr.finv = lyr.finv > 0.5f ? 0.0f : 1.0f;
                lyr.smear = rnd.next() % 12;
                lyr.zoom = false;
                if ((rnd.next() & 15) < 2) {
                    lyr.ty = rnd.next() % 144;
                    lyr.tx = rnd.next() % std::max(1, 320 - lyr.width);
                    lyr.zoom = true;
                }
            }
            if ((pos & 0x1f) == 0) newLyric = true;
        }
        if (lyr.finv > 0) lyr.finv = std::max(0.0f, lyr.finv - 0.021f);
        if (newLyric) {  // picked up by the next frame of 0x2a34c
            newLyric = false;
            placeWord(c);
        }
        t += 3;  // 0x181c4
        if (pos != lastBl) {
            lastBl = pos;
            if ((pos & 3) == 0) {
                const std::array<int, 16>& J = exeJitter(c);
                for (int& j : jit) j = int(float(J[size_t(rnd.next() & 15)]) * 0.7f);
            }
            if ((pos & 0xf) == 0) {
                rnd.next();
                if (pos == 0x4c0) {  // the capture's realisation of the P = 1/4 switch
                    pal.flash(400);
                    pal.target = 0x4000;
                    mode = (mode + 3) & 3;
                    pa = float(rnd.next() % 400 - 200);
                    pb = float(rnd.next() % 400 - 200);
                    pc = float(rnd.next() % 500 + 500);
                }
            }
        }
        inset = std::min(1.0f, inset + 0.01f);  // 0x29ce4
        lyr.jit[0] = jit[0];
        lyr.jit[1] = jit[1];
        lyr.jit[2] = jit[2];
        lyr.jit[3] = jit[3];
        lyr.jit[4] = jit[7];
    }
};

struct Blobby : Scene {
    Shader bg, obj;
    LyricLayer lyrics;
    StepClock clock;
    Mesh morph;
    unsigned vao = 0, vbo = 0, ibo = 0;
    std::vector<float> tint;

    void init(Ctx& c) override {
        bg.build(kFullscreenVS, std::string(kSphereRayGLSL) + kBlobbyBgFS, "blobby bg");
        obj.build(kBlobbyObjVS, kBlobbyObjFS, "blobby object");
        lyrics.init(c);
        tint = fadDarkTint(c, "blobby.fad", "blobby.raw");
        morph = c.meshData("spikey");
        gl::GenVertexArrays(1, &vao);
        gl::BindVertexArray(vao);
        gl::GenBuffers(1, &vbo);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(morph.pos.size() * 2 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
        gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::GenBuffers(1, &ibo);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(morph.idx.size() * 4), morph.idx.data(), GL_STATIC_DRAW);
        gl::BindVertexArray(0);
    }

    void render(Ctx& c, float, float) override {
        const Texture& tex0 = c.tex("blobby#0");
        const Texture& tex1 = c.tex("blobby#1");
        const float T = exeTime(c.T), T0 = musicTimeOfPos(0x480);
        BlobbyState s;
        const double ticksF = std::max(0.0, double(T - T0) * kTickHz);
        const int n = int(ticksF) + 1;
        for (int k = 0; k < n; k++) s.tick(c, musicPos(T0 + float(k / kTickHz)));
        const float t = float(3.0 * ticksF);  // continuous between ticks

        // background (0x1cd4b + 0x135dc at scale 4 + 0x1d0dd)
        Target& frame = c.scratch[0];
        frame.bind();
        setBlend(Blend::None);
        Vec3 R0, R1, R2;
        exeMatrix(t / 7.0f, t / 2.0f, R0, R1, R2);
        const float k1024 = 6.28318530718f / 1024.0f;
        bg.use();
        ((s.mode & 1) ? tex0 : tex1).bind(0);
        bg.set("tex", 0);
        bg.set("R0", R0);
        bg.set("R1", R1);
        bg.set("R2", R2);
        bg.set("focal", (3400.0f + 2048.0f * std::sin((t / 2.0f + 0x156) * k1024)) / 256.0f);
        bg.set("off", (15.0f * t + 16384.0f * std::sin((t / 3.0f + 0x53e) * k1024)) / 256.0f);
        drawFullscreen();

        // object: morph (0x13e78), rotate and project (0x1405c), fill (0x14290 / 0x180b8)
        const Mesh& base = c.meshData("spikey");
        const float S1 = std::sin(t / 700.0f);
        for (size_t i = 0; i < base.pos.size(); i += 3) {
            float r0 = base.extra[i / 3], k = (170.0f + (r0 - 170.0f) * S1) / r0;
            for (int j = 0; j < 3; j++) morph.pos[i + size_t(j)] = base.pos[i + size_t(j)] * k;
        }
        morph.computeNormals();
        std::vector<float> v(morph.pos.size() * 2);
        for (size_t i = 0; i < morph.pos.size() / 3; i++)
            for (int j = 0; j < 3; j++) {
                v[i * 6 + size_t(j)] = morph.pos[i * 3 + size_t(j)];
                v[i * 6 + 3 + size_t(j)] = morph.nrm[i * 3 + size_t(j)];
            }
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(v.size() * sizeof(float)), v.data());
        float A = t / 700.0f, B = t / 243.0f, sA = std::sin(A), cA = std::cos(A), sB = std::sin(B), cB = std::cos(B);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        gl::Enable(GL_DEPTH_TEST);
        obj.use();
        tex0.bind(0);
        obj.set("env", 0);
        obj.set("contour", (s.mode & 2) ? 0 : 1);
        obj.set("r0", cA * cB, sA, -cA * sB);
        obj.set("r1", -sA * cB, cA, sA * sB);
        obj.set("r2", sB, 0.0f, cB);
        obj.set("offs", 100.0f * std::sin(s.pa + t / 300.0f), 100.0f * std::sin(s.pb + t / 542.0f),
                1200.0f + 600.0f * std::sin(s.pc + t / 223.0f));
        gl::DrawElements(GL_TRIANGLES, GLsizei(morph.idx.size()), GL_UNSIGNED_INT, nullptr);
        gl::BindVertexArray(0);
        gl::Disable(GL_DEPTH_TEST);

        // lyrics, then inset at (193, 10), lyric lightening and palette
        lyrics.update(c, s.lyr, clock.steps(c));
        c.out->bind();
        ExeComposite k;
        k.winx = 193;
        k.winy = 10;
        k.open = s.inset;
        k.lyrics = &lyrics.acc();
        k.tint = &tint;
        exeComposite(c, frame, k, s.pal);
    }
};
REGISTER_SCENE(Blobby, "blobby");

}  // namespace
