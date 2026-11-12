// The particles (scenes "particles" and "particles2"): ports 0x13388 (set-up), 0x133ad / 0x133f1 (the parts, main
// loops 0x1348e / 0x13435), 0x1350e / 0x134e3 (the PIT callbacks), 0x135c5 (the draw step: random blobs 0x138ff,
// the words of file 58, the orbiting blobs 0x138be of variant 2), 0x13579 + 0x13922 (the zoom-blur feedback) and
// the fader 0x4a7b0 / 0x4a80b (stars/re/notes-parts2d-b.md section 4).
//
// Every drawn frame (one per retrace, 70.086 Hz) the original stamps ten white or black blobs into a 5-bit level
// buffer, sometimes a word, in variant 2 four blobs on Lissajous orbits, then zooms the buffer by 128/120 about
// (164.0, 93.1) and blurs it with the 4-neighbour average; the screen shows the result through a 32-colour ramp.
// The remake replays the same steps (stepped exactly, the random generator 0x4af10 replayed from its seed in the
// image, variant 2 continuing where variant 1 stopped) on a float level buffer at 3 times the original resolution:
//   - the 4-pixel-group zoom maps -> the fitted continuous map (scale 15/16, centre 164.0, 93.1; notes 3.2);
//   - the 4-neighbour average -> a Gaussian of the same variance; the `>> 2` truncation -> up to -3/8 level per
//     step where the neighbours differ (zoomblur.h);
//   - the blobs -> anti-aliased ellipses of the same area and extent at the same centres; the words -> file 58's
//     lettering as a smooth (bicubic, thresholded) coverage mask;
//   - the 5-bit level through the 32-entry palette -> a continuous level between the palette entries;
//   - the orbit positions and the palette fade -> evaluated continuously in PIT ticks.
// The display blends the states after steps n and n + 1 by the fraction of the retrace.
#include "exe.h"
#include "scenes/zoomblur.h"
#include "stars/core.h"

namespace {

using namespace stars;

constexpr int kScale = 3;            // feedback buffer pixels per original pixel
constexpr int kCheckpointEvery = 64;

struct Blob {
    float x, y, rx, ry, v;  // centre (VGA), semi-axes (VGA px), level 0 / 1
};

struct Step {
    std::vector<Blob> blobs;
    int word = -1;  // entry of the word table drawn in this step
};

// The part's state that carries from step to step and from variant 1 into variant 2.
struct Carry {
    Random rng{0u};
    uint32_t n = 0;      // [0x13044] PIT callbacks, never reset
    uint32_t since = 0;  // [0x13539] callbacks since the word timer was last processed, never reset
};

struct Variant {
    int number;
    uint32_t palette, words;
    int entries;
    int lastOrder;
};
constexpr Variant kV1{1, 0x13048, 0x13178, 13, 0x68};
constexpr Variant kV2{2, 0x130a8, 0x13278, 17, 0x86};

// Step j's frame is on screen from start + (j + 1) / retrace: the capture shows the first drawn frame in the record
// after the part's first one. It is drawn when the retrace wait before that line 0 ends (CaptureClock).
double frameTime(double start, int j) { return start + (j + 1) / kRetraceHz; }
// The part installs its callback just before its first retrace wait (snapshot f18451: no callback yet at the sync).
double installTime(double start) { return start + 0.9 / kRetraceHz; }

struct Schedule {
    std::vector<Step> steps;
    p2b::Pit pit;
    int fadeOut = 1 << 30;  // the callback that starts the fade-out (order >= last_order)
    Carry end;              // the state when the part clears its callback
};

// Replays the part on the CPU: the callbacks (music rows, fade trigger, word timer) and the 31 random numbers per
// drawn frame, in the original's order (notes 4.2, 4.3).
Schedule simulate(const Ctx& c, const Variant& v, double start, Carry s) {
    Schedule out;
    out.pit = p2b::Pit(installTime(start));
    const p2b::Pit& pit = out.pit;
    uint32_t n0 = s.n;
    int32_t timer = 20;
    uint32_t word = 0;
    int done = 0;
    p2b::CaptureClock clk(c);
    auto callbacks = [&](double until) {
        for (int k = done + 1; pit.time(k) <= until; k++) {  // 0x1350e / 0x134e3
            s.n++;
            s.since++;
            int pos = clk.position(pit.time(k));
            if ((pos >> 8) >= v.lastOrder && out.fadeOut > k) out.fadeOut = k;
            if ((pos & 0xff) == 0 || (pos & 0xff) == 0x1e) timer = 20;
            done = k;
        }
    };
    for (int j = 0; j < 100000; j++) {
        double tau = clk.afterSync(frameTime(start, j));
        callbacks(tau);
        Step st;
        for (int i = 0; i < 10; i++) {  // 0x138ff: rows y-3..y, columns x+1..x+5, 16 pixels
            int y = 50 + int(s.rng.next(100));
            int x = 80 + int(s.rng.next(160));
            float val = (s.rng.next(256) & 1) ? 0.0f : 1.0f;
            st.blobs.push_back({x + 3.5f, y - 1.0f, 2.523f, 2.0185f, val});
        }
        s.rng.next(4000);  // 0x13610: the ring that is never drawn
        if (timer != 0) {   // 0x13676: the word
            timer -= int32_t(s.since);
            if (timer < 0) {
                timer = 0;
                if (++word >= uint32_t(v.entries)) word = 0;
            }
            s.since = 0;
            st.word = int(word);
        }
        if (v.number == 2) {  // 0x138be at the tick count: rows y-3..y+3, 37 pixels, centre (x + 3.5, y + 0.5)
            static const int kOrbit[4][4] = {{4, 10, 10, 0}, {6, 12, 10, 0}, {2, 11, 10, 1}, {7, 11, 12, 0}};
            double n = n0 + pit.cont(tau);  // continuous in ticks
            for (const auto& o : kOrbit) {
                double a = (o[3] ? -n : n) * double(1 << o[0]);
                double y = 100 + kSinA(a) / double(1 << o[1]);
                double x = 160 + kSinA(a + 1024) / double(1 << o[2]);
                st.blobs.push_back({float(x + 3.5), float(y + 0.5), 3.432f, 3.432f, 1.0f});
            }
        }
        out.steps.push_back(std::move(st));
        double check = tau + 0.3 / kRetraceHz;  // `while (order() <= last_order)` after the zoom
        if ((clk.position(check) >> 8) > v.lastOrder) {
            callbacks(check);
            break;
        }
    }
    out.end = s;
    return out;
}

const Cue* findCue(const std::vector<Cue>& q, const char* name) {
    for (const Cue& k : q)
        if (k.scene == name) return &k;
    return nullptr;
}

class ParticlesScene : public Scene {
public:
    explicit ParticlesScene(const Variant& v) : v_(v) {}
    ~ParticlesScene() override {
        hist_.release();
        zoom_.release();
        mask_.destroy();
        pal_.destroy();
    }

    void init(Ctx& c) override {
        const StarsExe& exe = *c.exe;
        // the schedule: variant 1 from the image's seed; variant 2 from where variant 1 stopped
        std::vector<Cue> q = buildTimeline(c);
        const Cue* c1 = findCue(q, "particles");
        const Cue* me = findCue(q, v_.number == 1 ? "particles" : "particles2");
        if (!c1 || !me) return;
        Carry s;
        s.rng = Random(exe);
        Schedule s1 = simulate(c, kV1, c1->start, s);
        sched_ = v_.number == 1 ? std::move(s1) : simulate(c, kV2, me->start, s1.end);
        // the palette ramp and the word table
        std::vector<float> pal(32 * 4);
        for (int i = 0; i < 32; i++) {
            for (int k = 0; k < 3; k++) pal[size_t(i) * 4 + size_t(k)] = exe.u8(v_.palette + uint32_t(3 * i + k)) / 63.0f;
            pal[size_t(i) * 4 + 3] = 1;
        }
        pal_.createRaw(32, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, pal.data(), false);
        for (int i = 0; i < v_.entries; i++) {
            Word w;
            uint32_t e = v_.words + uint32_t(16 * i);
            w.dst = exe.u32(e);
            w.src = exe.u32(e + 4);
            w.w = int(exe.u32(e + 8));
            w.h = int(exe.u32(e + 12));
            words_.push_back(w);
        }
        // file 58: the lettering as a 0 / 1 mask
        const auto& f58 = exe.file(58);
        std::vector<uint8_t> m(64000);
        for (size_t i = 0; i < m.size() && i < f58.size(); i++) m[i] = f58[i] ? 255 : 0;
        mask_.createRaw(320, 200, GL_R8, GL_RED, GL_UNSIGNED_BYTE, m.data(), false);

        hist_.init(320 * kScale, 200 * kScale, GL_R16F, kCheckpointEvery);
        zoom_.init(320 * kScale, 200 * kScale, GL_R16F, 31.0f, 1.0f / 12.0f);
        buildShaders();
    }

    void render(Ctx& c, double t) override {
        if (sched_.steps.empty()) return;
        int last = int(sched_.steps.size()) - 1;
        double s = (t - c.start) * kRetraceHz - 1.0;  // step j is on screen from start + (j + 1) / retrace
        int n = int(std::floor(s));
        float f = float(s - n);
        if (n >= last) { n = last; f = 0; }
        int m = std::min(n + 1, last);
        hist_.ensure(m, [&](int k, const Target& prev, const Target& dst) { step(k, prev, dst); });

        // the palette fade, stepped in the callbacks: in from the first, out from the one that saw the last order;
        // the palette shown with a frame is the level read at the start of the loop, 0.87 frames before
        const p2b::Pit& pit = sched_.pit;
        double lag = 0.87 / kRetraceHz;
        double level = pit.fade(t, lag, 1, 0, 4, 64);
        if (pit.cont(t - lag) > sched_.fadeOut) level = pit.fade(t, lag, sched_.fadeOut + 1, 256, -4, 64);

        c.out->bind();
        setBlend(Blend::None);
        show_.use();
        hist_.state(std::max(n, -1)).bindTex(0);
        hist_.state(m).bindTex(1);
        pal_.bind(2);
        show_.set("a", 0);
        show_.set("b", 1);
        show_.set("pal", 2);
        show_.set("f", n < 0 ? 1.0f : f);
        show_.set("first", n < 0 ? 1 : 0);
        show_.set("level", float(level / 256.0));
        show_.set("rect", 0.0f, 0.0f, 320.0f, 200.0f);
        drawQuad();
    }

private:
    struct Word {
        uint32_t dst = 0, src = 0;
        int w = 0, h = 0;
    };

    void buildShaders() {
        const char* blob = R"(
in vec2 vga;
in vec2 q;
uniform vec4 blob;   // centre, semi-axes
uniform float val;
out vec4 o;
void main() {
    float e = length((vga - blob.xy) / blob.zw);
    float cov = clamp(0.5 + (1.0 - e) / max(fwidth(e), 1e-4), 0.0, 1.0);
    o = vec4(val * cov, 0.0, 0.0, cov);
}
)";
        // the word: file 58's pixels inside the entry's source rectangle, interpolated (Catmull-Rom) and thresholded
        const char* word = R"(
in vec2 vga;
in vec2 q;
uniform sampler2D mask;
uniform vec4 src;    // x, y, w, h in file 58's pixels
out vec4 o;
float texel(ivec2 i) {
    if (i.x < int(src.x) || i.y < int(src.y) || i.x >= int(src.x + src.z) || i.y >= int(src.y + src.w)) return 0.0;
    return texelFetch(mask, i, 0).r;
}
vec4 cr(float t) {
    return vec4(t * (-0.5 + t * (1.0 - 0.5 * t)), 1.0 + t * t * (-2.5 + 1.5 * t), t * (0.5 + t * (2.0 - 1.5 * t)),
                t * t * (-0.5 + 0.5 * t));
}
void main() {
    vec2 p = src.xy + q * src.zw - 0.5;
    vec2 i0 = floor(p);
    vec4 wx = cr(p.x - i0.x), wy = cr(p.y - i0.y);
    float v = 0.0;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) v += wx[x] * wy[y] * texel(ivec2(i0) + ivec2(x - 1, y - 1));
    float cov = clamp(0.5 + (v - 0.5) / max(fwidth(v), 1e-4), 0.0, 1.0);
    o = vec4(cov, 0.0, 0.0, cov);
}
)";
        const char* show = R"(
in vec2 vga;
uniform sampler2D a;
uniform sampler2D b;
uniform sampler2D pal;
uniform float f;
uniform int first;
uniform float level;
out vec4 o;
void main() {
    vec2 uv = vga / vec2(320.0, 200.0);
    float va = first != 0 ? 0.0 : texBicubic(a, uv).r;
    float l = clamp(mix(va, texBicubic(b, uv).r, f), 0.0, 1.0) * 31.0;
    int i = int(floor(l));
    vec3 c = mix(texelFetch(pal, ivec2(i, 0), 0).rgb, texelFetch(pal, ivec2(min(i + 1, 31), 0), 0).rgb, l - float(i));
    o = vec4(min(c * level, vec3(1.0)), 1.0);
}
)";
        blobShader_.build(p2b::kStampVS, blob, "particles-blob");
        wordShader_.build(p2b::kStampVS, word, "particles-word");
        show_.build(p2b::kScreenVS, show, "particles-show");
    }

    // One drawn frame: the stamps into the previous state, then the zoom-blur.
    void step(int k, const Target& prev, const Target& dst) {
        const Step& st = sched_.steps[size_t(k)];
        Target& work = zoom_.begin(prev);
        work.bind();
        setBlend(Blend::Premul);
        blobShader_.use();
        for (const Blob& b : st.blobs) {
            blobShader_.set("blob", b.x, b.y, b.rx, b.ry);
            blobShader_.set("val", b.v);
            blobShader_.set("rect", b.x - b.rx - 1, b.y - b.ry - 1, 2 * b.rx + 2, 2 * b.ry + 2);
            drawQuad();
        }
        if (st.word >= 0) {
            const Word& w = words_[size_t(st.word)];
            wordShader_.use();
            mask_.bind(0);
            wordShader_.set("mask", 0);
            wordShader_.set("src", float(w.src % 320), float(w.src / 320), float(w.w), float(w.h));
            wordShader_.set("rect", float(w.dst % 320), float(w.dst / 320), float(w.w), float(w.h));
            drawQuad();
        }
        // 0x13922's maps: src = 0.9375 dst + b, fitted expansion centre (164.0, 93.1) (notes 3.2, zoom_fit.py)
        zoom_.finish(dst, 164.0f, 93.1f, 0.9375f, 0, 0);
    }

    Variant v_;
    Schedule sched_;
    std::vector<Word> words_;
    Texture mask_, pal_;
    p2b::FeedbackHistory hist_;
    p2b::ZoomBlur zoom_;
    Shader blobShader_, wordShader_, show_;
};

struct Particles1 : ParticlesScene {
    Particles1() : ParticlesScene(kV1) {}
};
struct Particles2 : ParticlesScene {
    Particles2() : ParticlesScene(kV2) {}
};

}  // namespace

REGISTER_SCENE(Particles1, "particles");
REGISTER_SCENE(Particles2, "particles2");
