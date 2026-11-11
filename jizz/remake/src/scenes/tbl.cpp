// "TBL": part 0x12013 with 0x11986 (light, sparks, rays, glow, letters, feedback), 0x11e33 (the bump-lit surface),
// 0x13f98 (HSV) and 0x11f73 (the 32-level palette). A light moves behind the letters "TBL" (the mask [0x662b], the G
// plane of texture build 13 "testshit") over a surface lit through the displacement map [0x6572]; 40 sparks circle
// it on Lissajous figures and shoot rays towards the screen edges; everything is smeared by a per-frame feedback,
// tinted by a per-frame colour pulse, faded in from black and, at 00:30, out to white. jizz/re/notes-parts2d 3.
//
// One step = one frame of the original, on the capture's frame clock (3 retraces of 70 Hz per frame, 4 for the 41
// frames the capture shows slower, while the light passes between the letters): the picture of step n is drawn at the
// output resolution in byte units (bump, spark lines, additive cone, letters, rays), then the feedback runs:
//   c = max(0, X / 8 - 1/2),  y = (c - h) / 8
//   rising (c > h): h += 0 below y = 7/8, y - 1/2 from y = 3/2 on, linear between;  falling: h = max(c, h + y - 1/2)
//   shown = gauss(h)
// The original's integer feedback (7h + (X >> 3)) >> 3 only rises when the input is 8 levels above it (so it stays
// 7 below a steady input) and falls at least one level per frame; this float form reproduces that (on the 341
// recorded frames: mean |difference| of the shown level 0.32 of 31, mean +0.05, background exact; a float EMA is 2.6
// levels brighter, one with a bias of 1/3 per frame 0.7 levels too dark in the background). The 2x2 box of the
// history is a Gaussian of the same variance with the same half-pixel shift. Steps n and n + 1 are blended by the
// fraction. Because the stall gives the trail a long memory, the state is always replayed from the part start
// (with checkpoints), so a time renders the same after any seek and while playing.
//
// Continuous instead of the original's shortcuts: light and spark positions unrounded, the bump as
// max(30, (255 - d)^4 / 2^24) with filtered displacements, the cone over a disc of radius R = 15 + 15 m / 255 (m the
// filtered mask at the light; the original steps it by 16 levels), anti-aliased 4-pixel strokes. The palette is the
// original's: its 32 entries per frame exactly (5:6:5, they set the darks: the background is level 3), linear
// between the entries and blended between frames like the levels. Dropped: the specks where the displaced
// light-table index wraps around, the light-table window, the rows the box filter misses (black anyway).
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "jizz/core.h"
#include "jizz/textures.h"

namespace {
using namespace jizz;

constexpr int kEndPos = 0x0100;      // 01:00: event 100 ends the part
constexpr double kPit = 1193182.0;
constexpr double kRetrace = 1.0 / 70.0;
// Measured in the capture (full.flips): the third frame reads the light timer 29605; each frame after it takes 3
// retraces (51136 clocks), except these ranges of frames (0-based, inclusive), which take 4 (68182 clocks). The spark
// angles a frame uses were computed at the end of the previous frame, about 26000 clocks before its light read
// (hooked run of the part).
constexpr double kStep2Timer = 29605.0;
constexpr int kSlowFrames[][2] = {{10, 12}, {97, 101}, {106, 110}, {117, 118}, {120, 120}, {202, 204}, {206, 206},
                                  {211, 215}, {222, 222}, {224, 224}, {227, 227}, {309, 313}, {319, 323}, {330, 331},
                                  {333, 333}};
constexpr double kAngleLead = 26000.0;
constexpr int kCheckpoint = 64;       // the feedback state is kept every 64 steps (exact replays, see advance)
constexpr float kInputBias = 0.5f;    // mean of the >> 3 truncation (continuous input)
constexpr float kTrailBias = 0.5f;    // mean of the >> 3 truncation in 0x12111
constexpr float kStall0 = 0.875f;     // rise: none while the input is less than 8 levels above the trail ...
constexpr float kStall1 = 1.5f;       // ... y - 1/2 from (c - h) / 8 = 3/2 on, linear between
constexpr float kTrailFill = 3.0f;    // 0x11dfe fills the buffers with 0x03

const char* kBumpFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D blob;   // [0x6572]: G, B = signed displacements (int8 / 255)
uniform vec2 size;
uniform vec2 L;           // light centre of the bump (VGA): (lx - 0.5, ly + 0.5), the table peak is at (127, 128)
void main() {
    vec2 P = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    // 0x11e33 reads 320 bytes per row from 256-byte rows: x >= 256 is the next row's start
    vec2 t = vec2(P.x - 0.5, P.y - 0.5 + (P.x >= 256.0 ? 1.0 : 0.0));
    vec2 g = jzTex256Grad(blob, t, dFdx(P), dFdy(P)).gb * 255.0;
    o = vec4(max(30.0, jzSpot(length(P - L + g))), 0.0, 0.0, 1.0);
}
)";
const char* kConeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D Y;      // bump and spark lines
uniform sampler2D mask;   // testshit: G = the letters (0 inside, 255 outside), x wrapping at 256
uniform vec2 size;
uniform vec3 G;           // cone centre (lx + 0.5, ly + 0.5) and radius R
void main() {
    vec2 P = vec2(gl_FragCoord.x * (320.0 / size.x), (size.y - gl_FragCoord.y) * (200.0 / size.y));
    float y = texelFetch(Y, ivec2(gl_FragCoord.xy), 0).r;
    float cone = max(0.0, 255.0 * (1.0 - length(P - G.xy) / G.z));    // 0x11c73, saturating
    float m = texture(mask, P / 256.0).g * 255.0;                      // 0x11d12: * mask >> 8
    o = vec4(min(255.0, y + cone) * m / 256.0, 0.0, 0.0, 1.0);
}
)";
const char* kTrailFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D T, X;
uniform vec2 bias;        // (input, trail): the means of the truncations
uniform vec2 stall;       // rise: none below y0 = 7/8 (8 levels), y - 1/2 from y1 = 3/2 on, linear between
void main() {   // 0x11dc6 >> 3 and 0x12111 (7h + c) >> 3, continuous
    ivec2 p = ivec2(gl_FragCoord.xy);
    float h = texelFetch(T, p, 0).r;
    float c = max(texelFetch(X, p, 0).r / 8.0 - bias.x, 0.0);
    float y = (c - h) / 8.0;
    float rise = y < stall.x ? 0.0 : y < stall.y ? (y - stall.x) * (stall.y - 0.5) / (stall.y - stall.x) : y - 0.5;
    o = vec4(c > h ? h + rise : max(c, h + y - bias.y), 0.0, 0.0, 1.0);
}
)";
const char* kGaussFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D src;
uniform vec2 dir;
uniform int n;
uniform vec2 tap[32];   // (offset in pixels along dir, weight): two neighbouring taps per linear-filtered read
void main() {   // one axis of the Gaussian that replaces the 2x2 box 0x11eb2
    vec2 k = 1.0 / vec2(textureSize(src, 0));
    float acc = 0.0;
    for (int i = 0; i < n; i++) acc += tap[i].y * texture(src, (gl_FragCoord.xy + dir * tap[i].x) * k).r;
    o = vec4(acc, 0.0, 0.0, 1.0);
}
)";
const char* kShowFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D A, B;   // shown level of steps n and n + 1
uniform float f;
uniform vec3 lutA[32], lutB[32];   // the palettes of steps n and n + 1
void main() {   // the palette 0x11f73, linear between its 32 entries
    ivec2 p = ivec2(gl_FragCoord.xy);
    float x = clamp(mix(texelFetch(A, p, 0).r, texelFetch(B, p, 0).r, f), 0.0, 31.0);
    int i = int(min(floor(x), 30.0));
    float w = x - float(i);
    o = vec4(mix(mix(lutA[i], lutA[i + 1], w), mix(lutB[i], lutB[i + 1], w), f), 1.0);
}
)";

// 0x13f98 (AL = hue, BL = saturation, CL = value) -> R, G, B bytes
std::array<int, 3> hsv(int h, int s, int v) {
    const int e = h * 6, sector = e >> 8;
    int f = e & 255;
    if (!(sector & 1)) f = 255 - f;
    const int X = ((255 - ((f * s) >> 8)) * (v + 1)) >> 8, P = ((255 - s) * (v + 1)) >> 8;
    static const int order[6][3] = {{0, 1, 2}, {1, 0, 2}, {1, 2, 0}, {2, 1, 0}, {2, 0, 1}, {0, 2, 1}};  // V, X, P
    std::array<int, 3> c{};
    c[order[sector][0]] = v;
    c[order[sector][1]] = X;
    c[order[sector][2]] = P;
    return c;
}

// 0x11f73: the 32 palette entries for colour c and offset off, as display colours (5:6:5 fields over their maximum)
void palette(const std::array<int, 3>& c, int off, float* out) {
    static const int bits[3] = {5, 6, 5};
    for (int l = 0; l < 32; l++) {
        const int b = std::clamp(l + off, 0, 31);
        for (int ch = 0; ch < 3; ch++) {
            const int a = std::min(255, c[ch] + 8 * b);
            out[3 * l + ch] = float((a * b) >> (13 - bits[ch])) / float((1 << bits[ch]) - 1);
        }
    }
}

class TblScene : public Scene {
public:
    void init(Ctx& c) override {
        const Textures& tx = c.shared<Textures>();
        jizz::prepare(c);
        const JizzExe& e = *c.exe;
        mulX_ = e.u32(0x11582);
        mulY_ = e.u32(0x11586);
        cx_ = e.s16(0x1158a);
        cy_ = e.s16(0x1158c);
        ax_ = e.s16(0x1158e);
        ay_ = e.s16(0x11590);
        twoPi16k_ = e.f64(0x11596);
        boxX_ = e.f32(0x115ae);
        boxY_ = e.f32(0x115b2);
        rayK_ = e.f32(0x115b6);
        sparkR_ = e.f32(0x115c2);
        const std::array<uint32_t, 80> r = precalcRandoms();  // 0x11dfe
        for (int i = 0; i < 40; i++) {
            speed_[2 * i] = int16_t(r[2 * i] & 0xffff);
            speed_[2 * i + 1] = int16_t(r[2 * i] >> 16);
            start_[2 * i] = uint16_t(r[2 * i + 1] & 0xffff);
            start_[2 * i + 1] = uint16_t(r[2 * i + 1] >> 16);
        }
        const uint8_t* g = tx.planar(Tex::Testshit) + 0x10000;  // [0x662b]: G plane, read 320 wide (x & 255)
        mask_.resize(320 * 200);
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) mask_[y * 320 + x] = g[y * 256 + (x & 255)];

        buildClock(c);
        replayFrameState(c);

        bump_.build(kFullscreenVS, std::string(kGLSL) + kBumpFS, "tbl bump");
        cone_.build(kFullscreenVS, kConeFS, "tbl cone");
        trail_.build(kFullscreenVS, kTrailFS, "tbl trail");
        gauss_.build(kFullscreenVS, kGaussFS, "tbl gauss");
        show_.build(kFullscreenVS, kShowFS, "tbl show");
    }

    void render(Ctx& c, double t) override {
        Target* out = c.out;
        const double steps = stepsAt(t);
        const int n = int(steps);
        const float f = float(steps - n);
        advance(c, n + 1);
        c.out = out;
        out->bind();
        setBlend(Blend::None);
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_SCISSOR_TEST);

        float lut[2][96];
        for (int i = 0; i < 2; i++) palette(pulseColour(state(n + i).pulse), state(n + i).fade >> 3, lut[i]);
        show_.use();
        slot_[1 - cur_].shown.bindTex(0);
        slot_[cur_].shown.bindTex(1);
        show_.set("A", 0);
        show_.set("B", 1);
        show_.set("f", f);
        gl::Uniform3fv(show_.loc("lutA[0]"), 32, lut[0]);
        gl::Uniform3fv(show_.loc("lutB[0]"), 32, lut[1]);
        drawFullscreen();
        aaRect(c, 0, 194, 320, 6, kBlack);  // 0x120de: rows 194..199 black
    }

private:
    struct Step { int fade = 0, pulse = 0; };
    struct Slot { Target trail, shown; };

    // The frame clock: the music time at which step k's frame starts (its light timer read).
    void buildClock(Ctx& c) {
        const double end = c.positionTime(kEndPos);
        const double t2 = (kStep2Timer - c.timer(0)) / kPit;
        times_.clear();
        double t = t2 - 6 * kRetrace;
        for (int k = 0; t < end + 0.2; k++) {
            times_.push_back(t);
            bool slow = false;
            for (const auto& r : kSlowFrames) slow = slow || (k >= r[0] && k <= r[1]);
            t += (slow ? 4 : 3) * kRetrace;
        }
    }
    // Steps since the part start at music time t (fractional: blended between frame starts).
    double stepsAt(double t) const {
        if (t <= times_.front()) return 0.0;
        const size_t k = size_t(std::upper_bound(times_.begin(), times_.end(), t) - times_.begin()) - 1;
        if (k + 1 >= times_.size()) return double(times_.size() - 1);
        return k + (t - times_[k]) / (times_[k + 1] - times_[k]);
    }

    // 0x12013's per-frame state, replayed from the part start: the pulse (+2 per frame) and the fade (from -260,
    // +9 per frame to 0; event 10: 0; event 50: +23 per frame while below 256).
    void replayFrameState(Ctx& c) {
        const int count = int(times_.size());
        steps_.assign(size_t(count), Step{});
        int fade = -260, pulse = 0;
        for (int k = 0; k < count; k++) {
            const int ev = c.event(stepTime(k));
            pulse = (pulse + 2) & 255;
            if (ev == 10 || ev == 20 || ev == 30 || ev == 40) fade = 0;
            else if (ev == 50) { if (fade < 0x100) fade += 0x17; }
            else if (ev != 100) { fade += 9; if (fade >= 0) fade = 0; }
            steps_[size_t(k)] = {fade, pulse};
        }
    }
    const Step& state(int k) const { return steps_[size_t(std::clamp(k, 0, int(steps_.size()) - 1))]; }
    double stepTime(int k) const { return times_[size_t(std::clamp(k, 0, int(times_.size()) - 1))]; }

    // 0x1201d: triangle of the pulse -> hue 154..181 -> 0x13f98
    static std::array<int, 3> pulseColour(int p) {
        const int a = p < 0x80 ? p : 255 - p;
        return hsv((((a >> 2) * 0xe6) >> 8) + 0x9a, 0xff, 0xff);
    }

    // Mask [0x662b] at VGA coordinates, bilinear (pixel (x, y) at its centre (x + 0.5, y + 0.5)), 0..255.
    float maskAt(double X, double Y) const {
        const double fx = std::clamp(X - 0.5, 0.0, 319.0), fy = std::clamp(Y - 0.5, 0.0, 199.0);
        const int x = std::min(int(fx), 318), y = std::min(int(fy), 198);
        const double ax = fx - x, ay = fy - y;
        auto m = [&](int i, int j) { return double(mask_[j * 320 + i]); };
        return float((m(x, y) * (1 - ax) + m(x + 1, y) * ax) * (1 - ay) + (m(x, y + 1) * (1 - ax) + m(x + 1, y + 1) * ax) * ay);
    }

    struct Frame {
        float lx = 0, ly = 0, R = 15;
        std::vector<Segment> sparks, rays;
    };
    // 0x11986 for step k: light from the timer at the frame start, spark offsets from the angles computed at the
    // end of the previous frame (the random start angles in the first frame), rays from the spark points outside
    // the letters to 0.95 of the way to the edge of the box [0, 316] x [0, 196].
    Frame frame(const Ctx& c, int k) const {
        Frame F;
        const double T = c.timer(stepTime(k));  // xmpGetTimer + [0x6542] (0 in this part)
        auto turn = [&](double units) { return std::sin(std::fmod(units, 16384.0) * twoPi16k_); };
        F.lx = float(cx_ + ax_ * turn(T * mulX_ / 4294967296.0));
        F.ly = float(cy_ + ay_ * turn(T * mulY_ / 4294967296.0));
        F.R = 15.0f + maskAt(F.lx + 0.5, F.ly + 0.5) * (15.0f / 255.0f);
        const double Ta = T - kAngleLead;
        for (int i = 0; i < 40; i++) {
            double angx, angy;
            if (k == 0) { angx = start_[2 * i]; angy = start_[2 * i + 1]; }
            else { angx = Ta * speed_[2 * i] * 256.0 / 4294967296.0; angy = Ta * speed_[2 * i + 1] * 256.0 / 4294967296.0; }
            const double dx = sparkR_ * turn(angx), dy = sparkR_ * turn(angy);
            const float sx = F.lx + float(dx), sy = F.ly + float(dy);
            F.sparks.push_back({F.lx, F.ly, sx, sy});
            if (maskAt(sx + 0.5, sy + 0.5) < 0.5f) continue;  // spark inside a letter: no ray
            double kk = 1e30;
            if (dx > 0) kk = std::min(kk, (boxX_ - F.lx) / dx);
            else if (dx < 0) kk = std::min(kk, -F.lx / dx);
            if (dy > 0) kk = std::min(kk, (boxY_ - F.ly) / dy);
            else if (dy < 0) kk = std::min(kk, -F.ly / dy);
            if (kk > 1e29) continue;
            kk *= rayK_;
            F.rays.push_back({sx, sy, F.lx + float(kk * dx), F.ly + float(kk * dy)});
        }
        return F;
    }

    // Step k's picture in byte units into X (the work buffer before >> 3).
    void drawPicture(Ctx& c, int k, Target& X) {
        const Textures& tx = c.shared<Textures>();
        const Frame F = frame(c, k);
        auto Y = c.tempSized(X.w, X.h, false, GL_R16F);
        Y->bind();
        setBlend(Blend::None);
        bump_.use();
        tx.bind(Tex::BlobHeights, 0);
        bump_.set("blob", 0);
        bump_.set("size", float(Y->w), float(Y->h));
        bump_.set("L", F.lx - 0.5f, F.ly + 0.5f);
        drawFullscreen();
        c.out = &*Y;
        drawLines(c, F.sparks, 249.0f);  // 0x1145c from the light to each spark point
        X.bind();
        setBlend(Blend::None);
        cone_.use();
        Y->bindTex(0);
        tx.bind(Tex::Testshit, 1);
        cone_.set("Y", 0);
        cone_.set("mask", 1);
        cone_.set("size", float(X.w), float(X.h));
        cone_.set("G", F.lx + 0.5f, F.ly + 0.5f, F.R);
        drawFullscreen();
        c.out = &X;
        drawLines(c, F.rays, 249.0f);    // the rays, over the letters
    }

    void ensure(Ctx& c) {
        if (w_ == c.w && h_ == c.h && slot_[0].trail.fbo) return;
        for (Slot& s : slot_) {
            s.trail.create(c.w, c.h, false, GL_R16F);
            s.shown.create(c.w, c.h, false, GL_R16F);
        }
        for (auto& [k, t] : checkpoints_) t.destroy();
        checkpoints_.clear();
        w_ = c.w;
        h_ = c.h;
        have_ = kNone;
    }

    static void copy(const Target& src, Target& dst) {
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);
        gl::BlitFramebuffer(0, 0, src.w, src.h, 0, 0, dst.w, dst.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    static void fill(Target& t, float v) {
        t.bind();
        gl::ClearColor(v, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT);
    }

    // Brings the feedback to step `want` (slot cur_ = step want, the other slot = step want - 1). The state always
    // comes from replaying every step since the part start (the stall of the rise gives the trail a long memory), so
    // a time looks the same after any seek and while playing: forwards it steps on; after a jump back it restarts
    // from the latest checkpoint before `want` (the trail after every 64th step, kept as it is passed) or from the
    // original's fill value at the part start.
    void advance(Ctx& c, int want) {
        ensure(c);
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_SCISSOR_TEST);
        auto it = checkpoints_.lower_bound(want);
        const int from = it == checkpoints_.begin() ? -1 : std::prev(it)->first;
        if (have_ == kNone || want < have_ || have_ < from) {
            if (from < 0) {
                for (Slot& s : slot_) {
                    fill(s.trail, kTrailFill);
                    fill(s.shown, kTrailFill);
                }
            } else {
                copy(checkpoints_[from], slot_[cur_].trail);
                blur(c, slot_[cur_].trail, slot_[cur_].shown);
            }
            have_ = from;
        }
        while (have_ < want) {
            step(c, have_ + 1);
            if ((have_ + 1) % kCheckpoint == 0 && !checkpoints_.count(have_)) {
                Target& t = checkpoints_[have_];
                t.create(w_, h_, false, GL_R16F);
                copy(slot_[cur_].trail, t);
            }
        }
    }

    void step(Ctx& c, int k) {
        Slot& S = slot_[cur_];
        Slot& D = slot_[1 - cur_];
        {
            auto X = c.tempSized(w_, h_, false, GL_R16F);
            drawPicture(c, k, *X);
            D.trail.bind();
            setBlend(Blend::None);
            trail_.use();
            S.trail.bindTex(0);
            X->bindTex(1);
            trail_.set("T", 0);
            trail_.set("X", 1);
            trail_.set("bias", kInputBias, kTrailBias);
            trail_.set("stall", kStall0, kStall1);
            drawFullscreen();
        }
        blur(c, D.trail, D.shown);
        cur_ = 1 - cur_;
        have_ = k;
    }

    // 0x11eb2: (h[k] + h[k+1] + h[k+320] + h[k+321]) / 4 -> a Gaussian of sigma 1/2 original pixel centred half a
    // pixel right and down, at the output resolution.
    void blur(Ctx& c, const Target& src, Target& dst) {
        auto tmp = c.tempSized(dst.w, dst.h, false, GL_R16F);
        gauss_.use();
        gauss_.set("src", 0);
        auto axis = [&](const Target& s, Target& d, float px, int dx, int dy) {
            const float sigma = 0.5f * px, centre = 0.5f * px;  // in output pixels
            int lo = int(std::floor(centre - 3.0f * sigma)), hi = int(std::ceil(centre + 3.0f * sigma));
            if (hi - lo + 1 > 64) { lo = int(std::floor(centre)) - 31; hi = lo + 63; }
            float w[64] = {}, sum = 0;
            for (int i = lo; i <= hi; i++) sum += w[i - lo] = std::exp(-0.5f * (i - centre) * (i - centre) / (sigma * sigma));
            float tap[64] = {};
            int n = 0;
            for (int i = 0; i <= hi - lo; i += 2, n++) {
                const float a = w[i] / sum, b = (i + 1 <= hi - lo ? w[i + 1] : 0.0f) / sum;
                tap[2 * n] = float(lo + i) + (a + b > 0 ? b / (a + b) : 0.0f);
                tap[2 * n + 1] = a + b;
            }
            d.bind();
            s.bindTex(0);
            gauss_.set("dir", float(dx), float(dy));
            gauss_.set("n", n);
            gl::Uniform2fv(gauss_.loc("tap[0]"), 32, tap);
            drawFullscreen();
        };
        axis(src, *tmp, dst.w / 320.0f, 1, 0);     // right
        axis(*tmp, dst, dst.h / 200.0f, 0, -1);    // down (GL rows count upwards)
    }

    static constexpr int kNone = -0x7fffffff;
    Shader bump_, cone_, trail_, gauss_, show_;
    Slot slot_[2];
    std::map<int, Target> checkpoints_;  // trail after step k
    int cur_ = 0, have_ = kNone, w_ = 0, h_ = 0;
    std::vector<Step> steps_;
    std::vector<double> times_;  // music time of each step's frame start
    std::vector<uint8_t> mask_;
    std::array<int16_t, 80> speed_{};
    std::array<uint16_t, 80> start_{};
    double twoPi16k_ = 0;
    uint32_t mulX_ = 0, mulY_ = 0;
    int cx_ = 158, cy_ = 98, ax_ = -90, ay_ = 13;
    float boxX_ = 316, boxY_ = 196, rayK_ = 0.95f, sparkR_ = 30;
};

REGISTER_SCENE(TblScene, "tbl");
}  // namespace
