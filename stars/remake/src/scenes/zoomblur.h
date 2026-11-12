// Shared by the parts2d-b scenes (particles.cpp, dissolve.cpp, face.cpp): feedback buffers on the GPU that the
// original advances once per drawn frame, and the zoom-blur step of the particles and the dissolve.
//
//   FeedbackHistory  the state after every step, kept as a ring of two targets (the last two states) plus
//                    checkpoints every `every` steps. ensure(m) makes the states after steps m - 1 and m current:
//                    it steps forwards while playing, and after a seek it restarts from the last checkpoint before
//                    m (or from the cleared buffer), so a frame is the same however it was reached (the state
//                    never forgets exactly, so it is always the result of every step from the part start).
//   ZoomBlur         the generated zoom code of 0x13922 / 0x1554d (notes-parts2d-b 3.2): every step the picture is
//                    scaled up about a centre (src = centre + (dst - centre) * scale), blurred by the 4-neighbour
//                    average (variance 0.5 original pixels^2 per axis) and loses the `>> 2` truncation. Here: a
//                    separable Gaussian of the same variance at the buffer's resolution (a horizontal pass, then the
//                    vertical pass at the zoomed position, sampled bilinearly) and the truncation as a loss of 3/8
//                    of a level per step where the four neighbours differ by half a level or more, less where they
//                    are closer, nothing in flat areas (the original's flat areas are equal integers and lose
//                    nothing), clamped at 0. Notes 3.2: the truncation is what makes the trails fade; this keeps the
//                    mean level within a few per cent of the integer model over the whole particle part (a constant
//                    -3/8 everywhere darkens the large flat glows).
//   Pit, CaptureClock the parts' PIT callbacks (the capture's PIT phase) and when the capture's main loops leave
//                    their retrace wait: which drawn frame sees which tick (notes-parts2d-b section 10).
//
// Buffers use "VGA rows from the top": GL row r of a buffer of height H holds VGA y in [r, r + 1] * 200 / H, so a
// fragment's gl_FragCoord.xy / k is its VGA position (k = buffer pixels per original pixel).
#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "demo.h"

namespace p2b {

// Copies the colour of one target into another of the same size (exact).
inline void copyTarget(const Target& from, const Target& to) {
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, from.fbo);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, to.fbo);
    gl::BlitFramebuffer(0, 0, from.w, from.h, 0, 0, to.w, to.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

inline void clearTarget(const Target& t) {
    t.bind();
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(GL_COLOR_BUFFER_BIT);
}

class FeedbackHistory {
public:
    void init(int w, int h, GLenum fmt, int every) {
        w_ = w;
        h_ = h;
        fmt_ = fmt;
        every_ = every;
        for (Target& s : slot_) s.create(w, h, false, fmt);
        have_ = INT_MIN;
    }
    void release() {
        for (Target& s : slot_) s.destroy();
        for (auto& [k, t] : ckpt_) t.destroy();
        ckpt_.clear();
        have_ = INT_MIN;
    }
    int width() const { return w_; }
    int height() const { return h_; }
    // The state after step k (k = -1: the cleared buffer); valid for k = m and m - 1 after ensure(m).
    const Target& state(int k) const { return slot_[k & 1]; }

    // step(k, prev, dst): computes the state after step k into dst from the state after step k - 1.
    template <class Step> void ensure(int m, Step&& step) {
        if (m < 0) m = -1;
        if (have_ == m) return;
        // the best starting point: the current state (if not past m), a checkpoint <= m - 1, or the cleared buffer
        int from = -1;
        for (auto& [k, t] : ckpt_)
            if (k <= m - 1) from = std::max(from, k);
        if (have_ != INT_MIN && have_ <= m && have_ >= from) {
            from = have_;
        } else if (from >= 0) {
            copyTarget(ckpt_.at(from), slot_[from & 1]);
            have_ = from;
        } else {
            clearTarget(slot_[0]);
            clearTarget(slot_[1]);
            have_ = -1;
        }
        while (have_ < m) {
            int k = have_ + 1;
            step(k, slot_[(k - 1) & 1], slot_[k & 1]);
            have_ = k;
            if (k > 0 && k % every_ == 0 && !ckpt_.count(k)) {
                Target& c = ckpt_[k];
                c.create(w_, h_, false, fmt_);
                copyTarget(slot_[k & 1], c);
            }
        }
    }

private:
    int w_ = 0, h_ = 0, every_ = 64;
    GLenum fmt_ = GL_RGBA16F;
    Target slot_[2];
    std::map<int, Target> ckpt_;
    int have_ = INT_MIN;
};

// Vertex shader for fullscreen passes over a buffer (no varyings: the passes use gl_FragCoord).
inline const char* kPassVS = R"(
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Vertex shader for quads in VGA coordinates drawn into a buffer ("rows from the top"): rect = x, y, w, h;
// out vga = the VGA position, q = 0..1 across the quad.
inline const char* kStampVS = R"(
uniform vec4 rect;
out vec2 vga;
out vec2 q;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    q = c;
    vga = rect.xy + c * rect.zw;
    gl_Position = vec4(vga.x / 160.0 - 1.0, vga.y / 100.0 - 1.0, 0.0, 1.0);
}
)";

// Vertex shader for quads drawn onto ctx.out in VGA coordinates (y down, as Ctx::vgaProj): out vga.
inline const char* kScreenVS = R"(
uniform vec4 rect;
out vec2 vga;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vga = rect.xy + c * rect.zw;
    gl_Position = vec4(vga.x / 160.0 - 1.0, 1.0 - vga.y / 100.0, 0.0, 1.0);
}
)";

// Discrete Gaussian of variance `var` (buffer pixels^2) as bilinear taps: tap 0 at offset 0, then pairs of texels
// merged into one tap each side (offset, weight). Up to kMaxTaps per side.
struct GaussTaps {
    static constexpr int kMax = 8;
    float off[kMax] = {}, w[kMax] = {};
    int n = 0;
    void build(double var) {
        double s = std::sqrt(std::max(var, 1e-4));
        int r = std::max(1, int(std::ceil(3.0 * s)));
        std::vector<double> g(size_t(r) + 1);
        double sum = 0;
        for (int i = 0; i <= r; i++) {
            g[size_t(i)] = std::exp(-0.5 * i * i / (s * s));
            sum += i ? 2 * g[size_t(i)] : g[size_t(i)];
        }
        for (double& v : g) v /= sum;
        n = 0;
        off[n] = 0;
        w[n++] = float(g[0]);
        for (int i = 1; i <= r && n < kMax; i += 2) {
            double a = g[size_t(i)], b = i + 1 <= r ? g[size_t(i) + 1] : 0.0;
            off[n] = float((i * a + (i + 1) * b) / (a + b));
            w[n++] = float(a + b);
        }
    }
};

// The zoom-blur step (one call per step): src (with the step's stamps already in it) -> dst, through `tmp`.
class ZoomBlur {
public:
    // levels = the original's maximum level (31 or 63): the buffer holds level / levels. extraVar: variance (original
    // pixels^2 per axis) that the original adds per step besides the kernel: its nearest reads of the zoomed
    // position are off by up to half a pixel (uniform: 1/12), the dissolve's integer offsets as much again.
    bool init(int bufW, int bufH, GLenum fmt, float levels, float extraVar) {
        k_ = bufW / 320.0f;
        levels_ = levels;
        tmp_.create(bufW, bufH, false, fmt);
        work_.create(bufW, bufH, false, fmt);
        // the original's kernel: variance 0.5 px^2 per axis; the bilinear read of the zoom pass adds about 1/6 buffer
        // px^2 on each axis (the mean of f (1 - f)), so the two Gaussians are that much narrower
        taps_.build((0.5 + extraVar) * k_ * k_ - 1.0 / 6.0);
        const char* h = R"(
uniform sampler2D src;
uniform float toff[8];
uniform float tw[8];
uniform int ntap;
out vec4 o;
void main() {
    vec2 sz = vec2(textureSize(src, 0));
    vec2 p = gl_FragCoord.xy;
    vec4 s = texture(src, p / sz) * tw[0];
    for (int i = 1; i < ntap; i++)
        s += (texture(src, (p + vec2(toff[i], 0.0)) / sz) + texture(src, (p - vec2(toff[i], 0.0)) / sz)) * tw[i];
    o = s;
}
)";
        const char* v = R"(
uniform sampler2D src;
uniform sampler2D raw;  // the source before the blur (for the truncation)
uniform float toff[8];
uniform float tw[8];
uniform int ntap;
uniform vec2 centre;   // VGA coordinates
uniform vec2 scale;    // src = centre + (dst - centre) * scale + shift
uniform vec2 shift;
uniform float k;       // buffer pixels per original pixel
uniform float levels;
out vec4 o;
void main() {
    vec2 sz = vec2(textureSize(src, 0));
    vec2 dst = gl_FragCoord.xy / k;
    vec2 p = (centre + (dst - centre) * scale + shift) * k;
    vec4 s = texture(src, p / sz) * tw[0];
    for (int i = 1; i < ntap; i++)
        s += (texture(src, (p + vec2(0.0, toff[i])) / sz) + texture(src, (p - vec2(0.0, toff[i])) / sz)) * tw[i];
    // the `>> 2`: the four neighbours of the source (one original pixel away), their spread in levels
    vec4 a = texture(raw, (p - vec2(k, 0.0)) / sz), b = texture(raw, (p + vec2(k, 0.0)) / sz);
    vec4 c = texture(raw, (p - vec2(0.0, k)) / sz), d = texture(raw, (p + vec2(0.0, k)) / sz);
    vec4 spread = (max(max(a, b), max(c, d)) - min(min(a, b), min(c, d))) * levels;
    o = max(s - 0.375 / levels * clamp(spread / 0.5, 0.0, 1.0), vec4(0.0));
}
)";
        return blurH_.build(kPassVS, h, "zoomblur-h") && zoomV_.build(kPassVS, v, "zoomblur-v");
    }
    void release() {
        tmp_.destroy();
        work_.destroy();
    }
    // The buffer that the step's stamps are drawn into: a copy of the previous state.
    Target& begin(const Target& prev) {
        copyTarget(prev, work_);
        return work_;
    }
    // work (prev + stamps) -> dst: zoom about `centre` by `scale` (source = centre + (dst - centre) * scale + shift).
    void finish(const Target& dst, float cx, float cy, float scale, float shiftX, float shiftY) {
        setBlend(Blend::None);
        tmp_.bind();
        blurH_.use();
        work_.bindTex(0);
        blurH_.set("src", 0);
        setTaps(blurH_);
        drawFullscreen();
        dst.bind();
        zoomV_.use();
        tmp_.bindTex(0);
        work_.bindTex(1);
        zoomV_.set("src", 0);
        zoomV_.set("raw", 1);
        setTaps(zoomV_);
        zoomV_.set("centre", cx, cy);
        zoomV_.set("scale", scale, scale);
        zoomV_.set("shift", shiftX, shiftY);
        zoomV_.set("k", k_);
        zoomV_.set("levels", levels_);
        drawFullscreen();
    }

private:
    void setTaps(const Shader& s) const {
        gl::Uniform1fv(s.loc("toff"), GaussTaps::kMax, taps_.off);
        gl::Uniform1fv(s.loc("tw"), GaussTaps::kMax, taps_.w);
        s.set("ntap", taps_.n);
    }
    float k_ = 1, levels_ = 31;
    GaussTaps taps_;
    Target tmp_, work_;
    Shader blurH_, zoomV_;
};

// The music time of capture record f's line 0 (as timeline.cpp; for values measured in the capture).
inline double captureTime(int f) { return f / kRetraceHz - kCaptureMusicStart; }

// The PIT callbacks of a part (IRQ 0 at kPitHz). The PIT runs freely; the remake uses the capture's phase, measured
// in the snapshots stars/re/snaps/f*.snap (the next PIT tick after their vertical sync): the ticks fall at the music
// times t with t * kPitHz = 0.924 (mod 1). A part sees the ticks after it installed its callback.
struct Pit {
    static constexpr double kPhase = 0.924;
    double install = 0;  // music time at which the part installed its callback
    double first = 0;    // the first tick after it
    explicit Pit(double t0 = 0) : install(t0) {
        first = (std::floor(t0 * kPitHz - kPhase) + 1 + kPhase) / kPitHz;
    }
    // the callbacks that have run at music time t (the original's counter)
    int count(double t) const { return t < first ? 0 : int(std::floor((t - first) * kPitHz)) + 1; }
    // the same as a continuous function (through the middle of the steps)
    double cont(double t) const { return std::max(0.0, (t - first) * kPitHz + 0.5); }
    // the music time of callback k (k = 1: the first)
    double time(int k) const { return first + (k - 1) / kPitHz; }
    // the fader stepped in the callbacks (0x4a80b) from callback k0 on (level `from`, `step` per tick, n ticks),
    // as the palette shows it at music time t when the level is read `lag` seconds before the frame is shown
    double fade(double t, double lag, int k0, double from, double step, int n) const {
        double k = std::clamp(cont(t - lag) - (k0 - 1), 0.0, double(n));
        return from + step * k;
    }
};


// The capture's frame timing (stars/re/full.frames, made by stars/re/run_stars.py): when a part's main loop leaves
// its retrace wait (0x4a830: the vertical sync starts at line 412, ends at line 414 of 449) and draws. The emulator
// lets a polling loop jump to the next change of the retrace bit after half a quantum (1/64 of a 70 Hz frame), so
// the wait usually ends at line 0 of the next frame, earlier when an interrupt falls into the sync: at that
// interrupt, or at line 414. Which drawn frame sees which PIT tick depends on it where the two are close (the
// particles' word timer, notes 4.5). Interrupts of the capture: the PIT ticks (Pit::kPhase), the GUS timer 2 (the
// player's ticks; the emulator raises them 8 lines after the music's tick times, measured in a timing run of
// 0x133f1) and the GUS timer 1 that keeps running every 20 ms without an IRQ (phase measured likewise).
struct CaptureClock {
    const Ctx& c;
    static constexpr double kLine = 1.0 / (449.0 * kRetraceHz);
    static constexpr double kGusLag = 8 * kLine;
    static constexpr double kTimer1 = 0.02, kTimer1Phase = 0.000735;
    mutable int k = 0;  // cursor into the player's ticks
    explicit CaptureClock(const Ctx& cc) : c(cc) {}

    double nextGus(double t) const {
        while (k > 0 && c.tickTime(k - 1) + kGusLag > t) k--;
        while (c.tickTime(k) + kGusLag <= t && k < 1 << 20) k++;
        return c.tickTime(k) + kGusLag;
    }
    double nextEvent(double t) const {
        double pit = (std::floor(t * kPitHz - Pit::kPhase) + 1 + Pit::kPhase) / kPitHz;
        if (pit <= t) pit += 1 / kPitHz;
        double t1 = kTimer1Phase + kTimer1 * (std::floor((t - kTimer1Phase) / kTimer1) + 1);
        return std::min({pit, t1, nextGus(t)});
    }
    // g = line 0 of the frame after the sync: the moment the wait ends (and the frame is drawn)
    double afterSync(double g) const {
        const double vs0 = g - 37 * kLine, vs1 = g - 35 * kLine, quantum = 1.0 / (70.0 * 32.0);
        double now = vs0;
        for (int guard = 0; guard < 64 && now < vs1; guard++) {
            double e = nextEvent(now);
            double t = now + std::min(quantum, e - now) / 2;
            now = std::min(t < vs1 ? vs1 : g, e);
        }
        return now + 1e-7;  // an interrupt at that moment is served first
    }
    // the music position that a callback reads at time t (the player's variables change in its interrupt)
    int position(double t) const { return c.position(t - kGusLag); }
};

}  // namespace p2b
