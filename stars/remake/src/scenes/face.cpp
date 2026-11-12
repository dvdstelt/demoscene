// The face (scene "face"): ports 0x10b26 (set-up: file 35's palette, environment map and remap tables, 0x11a0a the
// face's span list from file 34), 0x1089c (the part: the main-loop script, notes 6.6), 0x10ae2 (the PIT callback:
// light angle, ripple phases, the zoom oscillator 0x11ad8), 0x10a86 (one frame: 0x1178c remap + environment-mapped
// face, 0x10bf3 the warp) and the fader (stars/re/notes-parts2d-b.md section 6).
//
// Every drawn frame (one per retrace) the original remaps the previous frame through one of file 35's colour tables
// (table 15: every colour towards a grey-blue fixed point, table 16: unchanged), draws the head (per-pixel
// coordinates of file 34 into file 35's marbled sphere map, moved by the light) over it and warps the result into
// the next frame: a zoom about the centre plus sine displacements per column and per row. So smears and ripples flow
// out of the face, fading to grey-blue. The remake keeps the palette indices and runs the same feedback in index
// space (the remap tables are the look, notes 6.9 (a)) on a buffer of 4 times the original resolution with nearest
// reads, and evaluates exactly instead of the original's shortcuts:
//   - the warp: zoom and displacements in float ((S1 + S2) z / 65536 with S continuous, the steps' low two bits
//     dropped as the original's `& 0x7fc` does), with the mean of the original's floors (half a pixel towards the
//     top left per floor: they move the point the trails stream from);
//   - the head: the texture coordinates interpolated between the 96 x 188 pixels of file 34 and the outline
//     anti-aliased; on screen the map is sampled bilinearly as a continuous level of the 32-colour ramp;
//   - the light offset 32 cos(2 pi angle / 2048), the zoom oscillator, the phases and the fade evaluated
//     continuously in PIT ticks;
//   - the screen: the index buffer is converted through the remap table and the palette at four texels and
//     interpolated (smooth edges between the trails' colours), the head drawn over it at the output resolution.
// Frames stepped as the capture draws them (one per retrace); the display blends the frames n and n + 1.
#include "exe.h"
#include "scenes/zoomblur.h"
#include "stars/core.h"

namespace {

using namespace stars;

constexpr int kScale = 4;             // index buffer pixels per original pixel
constexpr int kCheckpointEvery = 64;
// The capture: the first frame is on screen 1.53 retraces after the part starts, then one per retrace.
constexpr double kFirstFrame = 1.53;

struct Block {
    int spd[4] = {}, step[4] = {};
    int z = 256, table = 15, face = 1;
};

// The parameters of one drawn frame.
struct Frame {
    float ph[4] = {};     // ripple phases (0x7fc = one turn of 2048), continuous
    float step[4] = {};   // effective steps per column / row (step & ~3)
    float z = 256;
    float light = 0;      // env-map rows of the light offset (32 cos)
    int table = 15;
    bool face = true;
    bool ripple = false;  // the displacements are not all 0 (their floor moves the picture)
};

// The PIT-driven state of 0x10ae2 as continuous functions of time: a quantity that moves `rate` per callback from a
// value it had at time t0.
struct Ramp {
    double v0 = 0, rate = 0, t0 = 0;
    double at(const p2b::Pit& pit, double t) const { return v0 + rate * (pit.cont(t) - pit.cont(t0)); }
    void set(const p2b::Pit& pit, double t, double newRate) { v0 = at(pit, t); t0 = t; rate = newRate; }
};

class Face : public Scene {
public:
    ~Face() override {
        hist_.release();
        for (Texture* t : {&env_, &pal_, &tables_, &geo_}) t->destroy();
    }

    void init(Ctx& c) override {
        std::vector<Cue> q = buildTimeline(c);
        double start = 0;
        for (const Cue& k : q)
            if (k.scene == "face") start = k.start;
        const StarsExe& exe = *c.exe;
        const auto& f35 = exe.file(35);
        // palette, environment map (file 35 read as a 256-wide picture from offset 0) and the 32 remap tables
        std::vector<float> pal(256 * 4);
        for (int i = 0; i < 256; i++) {
            for (int k = 0; k < 3; k++) pal[size_t(i) * 4 + size_t(k)] = f35[size_t(3 * i + k)] / 63.0f;
            pal[size_t(i) * 4 + 3] = 1;
        }
        pal_.createRaw(256, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, pal.data(), false);
        env_.createRaw(256, 256, GL_R8, GL_RED, GL_UNSIGNED_BYTE, f35.data(), false);
        tables_.createRaw(256, 32, GL_R8, GL_RED, GL_UNSIGNED_BYTE, f35.data() + 0x102ff, false);
        buildGeometry(exe);
        schedule(c, start);
        buildShaders();
        hist_.init(320 * kScale, 200 * kScale, GL_R8, kCheckpointEvery);
    }

    void render(Ctx& c, double t) override {
        if (frames_.empty()) return;
        int last = int(frames_.size()) - 1;
        double s = (t - c.start) * kRetraceHz - kFirstFrame;  // frame j on screen from start + (1.53 + j) / retrace
        int n = int(std::floor(s));
        float f = float(s - n);
        if (n >= last) { n = last; f = 0; }
        int m = std::min(n + 1, last);
        // frame k's picture is made from the index buffer after frame k - 1: frames n and n + 1 need n - 1 and n
        hist_.ensure(std::max(n, 0), [&](int k, const Target& prev, const Target& dst) { step(k, prev, dst); });

        // the fade, stepped in the callback: read before the frame's retrace wait
        double lag = 0.6 / kRetraceHz;
        double level = pit_.fade(t, lag, 1, 0, 4, 64);
        if (t - lag >= fadeOut_) level = pit_.fade(t, lag, pit_.count(fadeOut_) + 1, 256, -4, 64);

        c.out->bind();
        setBlend(Blend::None);
        show_.use();
        bindCommon(show_);
        hist_.state(n - 1).bindTex(5);   // before frame n
        hist_.state(n).bindTex(6);       // before frame n + 1
        show_.set("prevA", 5);
        show_.set("prevB", 6);
        setFrame(show_, "A", frames_[size_t(std::max(n, 0))]);
        setFrame(show_, "B", frames_[size_t(m)]);
        show_.set("emptyA", n < 1 ? 1 : 0);
        show_.set("f", n < 0 ? 1.0f : f);
        show_.set("blackA", n < 0 ? 1 : 0);
        show_.set("level", float(level / 256.0));
        show_.set("rect", 0.0f, 0.0f, 320.0f, 200.0f);
        drawQuad();
    }

private:
    // File 34 through 0x11a0a: per face pixel the env-map row offset a and column offset b (bits 1..8 and 12..19 of
    // the dword, signed, halved). As a 320x200 texture (a, b, mask) with a and b carried a few pixels past the
    // outline so that the interpolation at the edge stays on the head.
    void buildGeometry(const StarsExe& exe) {
        const auto& f = exe.file(34);
        std::vector<float> a(64000, 0), b(64000, 0), m(64000, 0);
        size_t k = 0;
        unsigned pos = 0;
        while (k + 8 <= f.size() && pos < 0xf8c0) {
            unsigned skip = f[k + 2] | f[k + 3] << 8;
            unsigned cnt = f[k + 4] | f[k + 5] << 8;
            k += 8;
            pos += skip;
            for (unsigned i = 0; i < cnt && k + 4 <= f.size(); i++, k += 4, pos++) {
                uint32_t d = f[k] | f[k + 1] << 8 | f[k + 2] << 16 | uint32_t(f[k + 3]) << 24;
                int aa = int8_t(uint8_t(d >> 1)) >> 1, bb = int8_t(uint8_t(d >> 12)) >> 1;
                if (pos < 64000) {
                    a[pos] = float(aa);
                    b[pos] = float(bb);
                    m[pos] = 1;
                }
            }
        }
        std::vector<float> have = m;
        for (int it = 0; it < 6; it++) {  // grow the coordinates outwards by one pixel per pass
            std::vector<float> na = a, nb = b, nh = have;
            for (int y = 0; y < 200; y++)
                for (int x = 0; x < 320; x++) {
                    size_t i = size_t(y * 320 + x);
                    if (have[i] > 0) continue;
                    float sa = 0, sb = 0, n = 0;
                    for (int v = -1; v <= 1; v++)
                        for (int u = -1; u <= 1; u++) {
                            int xx = x + u, yy = y + v;
                            if (xx < 0 || yy < 0 || xx >= 320 || yy >= 200) continue;
                            size_t j = size_t(yy * 320 + xx);
                            if (have[j] > 0) { sa += a[j]; sb += b[j]; n++; }
                        }
                    if (n > 0) { na[i] = sa / n; nb[i] = sb / n; nh[i] = 1; }
                }
            a = na; b = nb; have = nh;
        }
        std::vector<float> px(64000 * 4);
        for (size_t i = 0; i < 64000; i++) {
            px[i * 4] = a[i];
            px[i * 4 + 1] = b[i];
            px[i * 4 + 2] = m[i];
            px[i * 4 + 3] = 1;
        }
        geo_.createRaw(320, 200, GL_RGBA32F, GL_RGBA, GL_FLOAT, px.data(), true);
    }

    static Block readBlock(const StarsExe& exe, uint32_t addr) {
        Block b;
        for (int i = 0; i < 4; i++) {
            b.spd[i] = exe.s32(addr + 4 + 8 * uint32_t(i));
            b.step[i] = exe.s32(addr + 8 + 8 * uint32_t(i));
        }
        b.z = exe.s32(addr + 36);
        b.table = exe.s32(addr + 40);
        b.face = exe.s32(addr + 44);
        return b;
    }

    // The main loop of 0x1089c (notes 6.6) on the CPU, one entry per drawn frame. The callback's quantities are
    // continuous in ticks; the script's checks use the music position after each frame.
    void schedule(Ctx& c, double start) {
        const StarsExe& exe = *c.exe;
        pit_ = p2b::Pit(start);
        p2b::CaptureClock clk(c);
        Block P = readBlock(exe, 0xf740);
        Ramp ph[4], angle;
        angle.v0 = exe.u32(0x107ca);  // 0x180
        bool osc = true;              // osc_start(0xd0, 0x130, 0x32, &P.z): z = 256 + 48 sin(50 n / 4096 turns)
        double zHeld = 256;
        auto zAt = [&](double t) { return osc ? 256.0 + 48.0 * std::sin(6.283185307179586 * 0x32 * pit_.cont(t) / 4096.0) : zHeld; };
        auto load = [&](uint32_t addr, double t) {
            P = readBlock(exe, addr);
            for (int i = 0; i < 4; i++) ph[i] = Ramp{0, double(P.spd[i]), t};
            zHeld = P.z;
        };
        int phase = 0, cnt = 0;
        unsigned acc = exe.u16(0x10a7e), inc = exe.u16(0x10a80);
        bool extra = false;
        for (int j = 0; j < 100000; j++) {
            double shown = start + (kFirstFrame + j) / kRetraceHz;
            double warpAt = shown, check = shown + 0.4 / kRetraceHz;
            if (phase == 8 && cnt) {  // before the frame: z + 1 every 4th frame, 12 times
                cnt--;
                if ((cnt & 3) == 0) zHeld += 1;
            }
            Frame fr;
            for (int i = 0; i < 4; i++) {
                fr.ph[i] = float(ph[i].at(pit_, warpAt));
                fr.step[i] = float(P.step[i] & ~3);
                fr.ripple = fr.ripple || P.spd[i] != 0 || P.step[i] != 0;
            }
            fr.z = float(zAt(warpAt));
            double a = angle.at(pit_, shown - 0.5 / kRetraceHz);  // read before the retrace wait
            fr.light = float(32.0 * std::cos(6.283185307179586 * a / 2048.0));
            fr.table = P.table;
            fr.face = P.face > 0;
            frames_.push_back(fr);
            // the checks after the frame
            int pos = clk.position(check);
            bool done = false;
            switch (phase) {
                case 0: if (pos >= 0x8820) { zHeld = zAt(check); osc = false; angle.set(pit_, check, 0x50); phase = 1; } break;
                case 1: if (angle.at(pit_, check) >= 0x980) { angle.set(pit_, check, 0); load(0x107dc, check); phase = 2; } break;
                case 2: if (pos > 0x8920) phase = 3; break;
                case 3: if (pos > 0x8b00) { angle.set(pit_, check, 0x1e); phase = 4; } break;
                case 4: if (pos > 0x8b20) { load(0x1081c, check); phase = 5; } break;
                case 5: if (pos > 0x8d00) { P.face = 0; phase = 6; } break;
                case 6:  // the strobe: after a frame without the face, acc += inc; on a carry one frame with it
                    if (!extra) {
                        acc += inc;
                        if (acc > 0xffff) { acc &= 0xffff; P.face = 1; extra = true; break; }
                    } else {
                        P.face = 0;
                        extra = false;
                    }
                    inc = (inc + 0x32) & 0xffff;
                    if (pos > 0x8f00) { P.face = 1; phase = 7; }
                    break;
                case 7: if (pos > 0x9000) { load(0x1085c, check); cnt = 0x30; phase = 8; } break;
                case 8: if (pos > 0x9200) { P.face = 0; zHeld += 0x16; phase = 9; } break;
                case 9: if (pos > 0x9210) { fadeOut_ = check; phase = 10; } break;
                case 10: if (pos > 0x9230) done = true; break;
            }
            if (done) break;
        }
    }

    void buildShaders() {
        // shared: the warp of 0x10bf3 in VGA coordinates, the head
        const char* common = R"(
uniform sampler2D env;     // file 35 as 256 x 256 indices
uniform sampler2D pal;     // 256 colours
uniform sampler2D tables;  // 32 remap tables
uniform sampler2D geo;     // a, b, mask of the head (320 x 200)
const float TAU = 6.283185307179586;
float S(float i) { return 1024.0 * sin(TAU * i / 2048.0); }
// the source position (VGA coordinates) of the frame at VGA position p; .z = 0 where the rows run out (black)
vec3 warp(vec2 p, vec4 ph, vec4 st, float z, float ripple) {
    vec2 c = p - 0.5;   // column / row index
    float dy = (S(ph.x + c.x * st.x) + S(ph.y + c.x * st.y)) * z / 65536.0;
    float dx = (S(ph.z + c.y * st.z) + S(ph.w + c.y * st.w)) * z / 65536.0;
    float fz = -0.5 * min(abs(z - 256.0), 1.0);   // the mean of the floors (notes 6.4)
    vec2 s = vec2(160.0, 100.0) + (c - vec2(160.0, 100.0)) * (z / 256.0) + vec2(dx, dy) + vec2(fz) - vec2(0.5 * ripple) + 0.5;
    // a column left of 0 or right of 319 reads the neighbouring row's end; rows outside are the zero margins
    if (s.x < 0.0) { s.x += 320.0; s.y -= 1.0; }
    else if (s.x >= 320.0) { s.x -= 320.0; s.y += 1.0; }
    return vec3(s, (s.y < 0.0 || s.y >= 200.0) ? 0.0 : 1.0);
}
// the head at VGA position q: x = coverage, y = env-map row, z = column
vec3 head(vec2 q, float light) {
    vec4 g = texBicubic(geo, q / vec2(320.0, 200.0));
    return vec3(g.b, g.r + 160.0 + light, g.g + 128.0);
}
float remap(int table, float idx) { return texelFetch(tables, ivec2(int(idx * 255.0 + 0.5), table), 0).r; }
vec3 colour(float idx) { return texelFetch(pal, ivec2(int(idx * 255.0 + 0.5), 0), 0).rgb; }
)";
        // one frame into the index buffer: remap(prev) at the warped position, the head where it covers
        std::string stepFs = std::string(common) + R"(
uniform sampler2D prev;
uniform vec4 ph; uniform vec4 st; uniform float z; uniform float ripple; uniform float light;
uniform int table; uniform int face;
uniform float k;
out vec4 o;
void main() {
    vec2 p = gl_FragCoord.xy / k;
    vec3 s = warp(p, ph, st, z, ripple);
    if (s.z == 0.0) { o = vec4(0.0); return; }
    if (face != 0) {
        vec3 h = head(s.xy, light);
        if (h.x >= 0.5) {
            o = vec4(texelFetch(env, clamp(ivec2(floor(h.zy + 0.5)), ivec2(0), ivec2(255)), 0).r);
            return;
        }
    }
    ivec2 i = clamp(ivec2(floor(s.xy * k)), ivec2(0), textureSize(prev, 0) - 1);
    o = vec4(remap(table, texelFetch(prev, i, 0).r));
}
)";
        std::string showFs = std::string(common) + R"(
in vec2 vga;
uniform sampler2D prevA; uniform sampler2D prevB;
uniform vec4 phA; uniform vec4 stA; uniform float zA; uniform float rippleA; uniform float lightA;
uniform int tableA; uniform int faceA;
uniform vec4 phB; uniform vec4 stB; uniform float zB; uniform float rippleB; uniform float lightB;
uniform int tableB; uniform int faceB;
uniform int emptyA; uniform int blackA;
uniform float f; uniform float level;
uniform float k;
out vec4 o;
// the frame made from the index buffer `prev` with these parameters, at VGA position p
vec3 frame(sampler2D prev, int empty, vec2 p, vec4 ph, vec4 st, float z, float ripple, float light, int table, int face) {
    vec3 s = warp(p, ph, st, z, ripple);
    vec3 h = head(s.xy, light);
    float cov = clamp(0.5 + (h.x - 0.5) / max(fwidth(h.x), 1e-4), 0.0, 1.0);   // (before any branch)
    if (s.z == 0.0) return vec3(0.0);
    // the remapped background: four index texels through the table and the palette, interpolated
    vec2 b = s.xy * k - 0.5;
    ivec2 i0 = ivec2(floor(b));
    vec2 fr = b - vec2(i0);
    ivec2 mx = textureSize(prev, 0) - 1;
    vec3 c[4];
    for (int n = 0; n < 4; n++) {
        ivec2 i = clamp(i0 + ivec2(n & 1, n >> 1), ivec2(0), mx);
        c[n] = empty != 0 ? colour(remap(table, 0.0)) : colour(remap(table, texelFetch(prev, i, 0).r));
    }
    vec3 bg = mix(mix(c[0], c[1], fr.x), mix(c[2], c[3], fr.x), fr.y);
    if (face == 0) return bg;
    // the head: the map as a continuous level of the first ramp (indices 3..31), the outline anti-aliased
    float lv = texture(env, (h.zy + 0.5) / 256.0).r * 255.0;
    int l0 = int(floor(lv));
    vec3 hc = mix(colour(float(l0) / 255.0), colour(float(min(l0 + 1, 255)) / 255.0), lv - float(l0));
    return mix(bg, hc, cov);
}
void main() {
    vec3 a = blackA != 0 ? vec3(0.0) : frame(prevA, emptyA, vga, phA, stA, zA, rippleA, lightA, tableA, faceA);
    vec3 b = frame(prevB, 0, vga, phB, stB, zB, rippleB, lightB, tableB, faceB);
    o = vec4(min(mix(a, b, f) * level, vec3(1.0)), 1.0);
}
)";
        stepShader_.build(p2b::kPassVS, stepFs, "face-step");
        show_.build(p2b::kScreenVS, showFs, "face-show");
    }

    void bindCommon(const Shader& s) const {
        env_.bind(1);
        pal_.bind(2);
        tables_.bind(3);
        geo_.bind(4);
        s.set("env", 1);
        s.set("pal", 2);
        s.set("tables", 3);
        s.set("geo", 4);
        s.set("k", float(kScale));
    }

    static void setFrame(const Shader& s, const std::string& sfx, const Frame& fr) {
        s.set(("ph" + sfx).c_str(), fr.ph[0], fr.ph[1], fr.ph[2], fr.ph[3]);
        s.set(("st" + sfx).c_str(), fr.step[0], fr.step[1], fr.step[2], fr.step[3]);
        s.set(("z" + sfx).c_str(), fr.z);
        s.set(("ripple" + sfx).c_str(), fr.ripple ? 1.0f : 0.0f);
        s.set(("light" + sfx).c_str(), fr.light);
        s.set(("table" + sfx).c_str(), fr.table);
        s.set(("face" + sfx).c_str(), fr.face ? 1 : 0);
    }

    // One drawn frame (0x10a86) into the index buffer.
    void step(int k, const Target& prev, const Target& dst) {
        dst.bind();
        setBlend(Blend::None);
        stepShader_.use();
        bindCommon(stepShader_);
        prev.bindTex(0);
        stepShader_.set("prev", 0);
        setFrame(stepShader_, "", frames_[size_t(k)]);
        drawFullscreen();
    }

    std::vector<Frame> frames_;
    p2b::Pit pit_;
    double fadeOut_ = 1e30;
    Texture env_, pal_, tables_, geo_;
    p2b::FeedbackHistory hist_;
    Shader stepShader_, show_;
};

}  // namespace

REGISTER_SCENE(Face, "face");
