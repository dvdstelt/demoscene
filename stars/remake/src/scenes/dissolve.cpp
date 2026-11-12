// The dissolving woman (scene "dissolve"): ports 0x1528c (set-up), 0x1535c (the part's loop), 0x1544d (the PIT
// callback: the swirling zoom offset), 0x15500 (the stamp of a masked picture, files 36..41), 0x1549b + 0x1554d (the
// three-plane zoom-blur feedback) and 0x79fad / 0x7a061 (the G / R-B checkerboard conversion to 256 colours)
// (stars/re/notes-parts2d-b.md section 5).
//
// While the music row is a multiple of 16 the part stamps one of three masked pictures of RA's woman (the intro's
// pictures 11, 12, 13 at their screen position, the figure only) into an 18-bit RGB buffer; every drawn frame
// (every second retrace, 35.04 Hz in the capture) the buffer is zoomed by 128/119 about a centre that circles twice
// a second and blurred, so the picture explodes into a brown swirl. The remake replays the same steps in float RGB
// at 3 times the original resolution (zoomblur.h):
//   - the stamps: the redraws of 11, 12 and 13 masked like the original (the background of the original pictures,
//     indices 0 and 102..108, decided per pixel of the redraw near the outline of files 36..38); with
//     --original-art, or for a picture without a redraw, the 6:6:6 pixels of files 36..41 as they are;
//   - the zoom: the fitted continuous map (scale 119/128 about (162.1, 93.4), notes 3.2) plus the offset of the
//     callback as a continuous function (dx = 1023 sin / 256 + 0.5, dy = 1023 cos / 256 - 0.5, notes 5.7);
//   - the blur and the truncation as for the particles;
//   - the checkerboard of (R, 0, B) and (0, G, 0) pixels (R in 15 levels, G and B in 16) is shown as the RGB
//     planes at the light the eye mixed from it: half of it, i.e. 0.5^(1 / 2.2) = 0.73 of each display value.
// The display blends the states after steps n and n + 1 by the fraction of the 35 Hz step.
#include "exe.h"
#include "scenes/zoomblur.h"
#include "stars/core.h"

namespace {

using namespace stars;

constexpr int kScale = 3;
constexpr int kCheckpointEvery = 48;
constexpr float kMixedLight = 0.7297f;  // 0.5^(1/2.2): two pixels, each with part of the colour, seen as one
// The capture: the first drawn frame is on screen 1.35 retraces after the part starts (it waits for the retrace
// after its first zoom), then one every second retrace. An iteration starts (reads the position, stamps) 1.6
// retraces before its frame is shown: the stamps of 6a:00 and 6b:00 fall on the frames the capture shows them in.
constexpr double kFirstFrame = 1.35;
constexpr double kTop = 1.6;

struct Step {
    int stamp = -1;      // 0..2: files 36 / 39, 37 / 40, 38 / 41
    float dx = 0, dy = 0;
};

struct Schedule {
    std::vector<Step> steps;
    p2b::Pit pit;
    double fadeOut = 1e30;  // music time of the iteration that starts the fade-out
};

class Dissolve : public Scene {
public:
    ~Dissolve() override {
        hist_.release();
        zoom_.release();
        for (Texture& t : stamp_) t.destroy();
        for (Target& t : redrawStamp_) t.destroy();
    }

    void init(Ctx& c) override {
        std::vector<Cue> q = buildTimeline(c);
        double start = 0;
        for (const Cue& k : q)
            if (k.scene == "dissolve") start = k.start;
        schedule(c, start);
        buildShaders();
        hist_.init(320 * kScale, 200 * kScale, GL_RGBA16F, kCheckpointEvery);
        zoom_.init(320 * kScale, 200 * kScale, GL_RGBA16F, 63.0f, 2.0f / 12.0f);
        // the three masked pictures as the files give them (span lists 36..38, planes 39..41)
        for (int k = 0; k < 3; k++) {
            decode(*c.exe, 36 + k, 39 + k, mask_[k], rgb_[k]);
            Image im;
            im.w = 320;
            im.h = 200;
            im.rgba.resize(64000 * 4);
            for (int i = 0; i < 64000; i++) {
                for (int ch = 0; ch < 3; ch++) im.rgba[size_t(i) * 4 + size_t(ch)] = uint8_t((rgb_[k][size_t(i) * 3 + size_t(ch)] * 255 + 31) / 63);
                im.rgba[size_t(i) * 4 + 3] = mask_[k][size_t(i)] ? 255 : 0;
            }
            stamp_[k].create(im, false, false);
        }
        // with art: the redraws of 11..13 masked at their resolution
        for (int k = 0; k < 3; k++) {
            const Picture& p = c.picture(11 + k);
            if (c.art() && p.redraw) maskRedraw(c, k, p);
        }
    }

    void render(Ctx& c, double t) override {
        if (sched_.steps.empty()) return;
        int last = int(sched_.steps.size()) - 1;
        double s = ((t - c.start) * kRetraceHz - kFirstFrame) / 2;  // step j on screen from start + (1.35 + 2 j) / retrace
        int n = int(std::floor(s));
        float f = float(s - n);
        if (n >= last) { n = last; f = 0; }
        int m = std::min(n + 1, last);
        hist_.ensure(m, [&](int k, const Target& prev, const Target& dst) { step(k, prev, dst); });

        // the fade: in from the first callback, out from the iteration that saw 6b:1a; the level is read just
        // before the retrace wait, half a retrace before the frame is shown
        const p2b::Pit& pit = sched_.pit;
        double lag = 0.5 / kRetraceHz;
        double level = pit.fade(t, lag, 1, 0, 4, 64);
        if (t - lag >= sched_.fadeOut) {
            int k0 = pit.count(sched_.fadeOut) + 1;
            level = pit.fade(t, lag, k0, pit.fade(sched_.fadeOut + lag, lag, 1, 0, 4, 64), -4, 64);
        }

        c.out->bind();
        setBlend(Blend::None);
        show_.use();
        hist_.state(std::max(n, -1)).bindTex(0);
        hist_.state(m).bindTex(1);
        show_.set("a", 0);
        show_.set("b", 1);
        show_.set("f", n < 0 ? 1.0f : f);
        show_.set("first", n < 0 ? 1 : 0);
        show_.set("gain", float(kMixedLight * level / 256.0));
        show_.set("rect", 0.0f, 0.0f, 320.0f, 200.0f);
        drawQuad();
    }

private:
    // The part's loop on the CPU: the position read at the top of each iteration (end, fade-out, stamps) and the
    // offset the callback has made when the zoom runs (notes 5.2).
    void schedule(Ctx& c, double start) {
        sched_.pit = p2b::Pit(start);
        const p2b::Pit& pit = sched_.pit;
        p2b::CaptureClock clk(c);
        for (int j = 0; j < 100000; j++) {
            // the iteration starts after the previous frame's conversion; the first when main saw 69:01
            double shown = start + (kFirstFrame + 2 * j) / kRetraceHz;
            double top = j == 0 ? start + p2b::CaptureClock::kGusLag + 1e-6 : shown - kTop / kRetraceHz;
            int pos = clk.position(top);
            if (pos > 0x6b30) break;
            if (pos > 0x6b19 && sched_.fadeOut > 1e29) sched_.fadeOut = top;
            Step st;
            int order = pos >> 8;
            if ((pos & 0xf) == 0 && order >= 0x69 && order <= 0x6b) st.stamp = order - 0x69;
            // 0x1544d: phase += 0x78 per callback; the zoom runs between the top and the retrace wait
            double zoomAt = shown - 0.8 / kRetraceHz;
            double a = 6.283185307179586 * 0x78 * pit.cont(zoomAt) / 4096.0;
            st.dx = float(1023.0 * std::sin(a) / 256.0 + 0.5);
            st.dy = float(1023.0 * std::cos(a) / 256.0 - 0.5);
            sched_.steps.push_back(st);
        }
    }

    // 0x15500's data: the span list (u16 skip, u16 count, 0xffff ends) and the R, G, B bytes of the spans.
    static void decode(const StarsExe& exe, int spans, int data, std::vector<uint8_t>& mask, std::vector<uint8_t>& rgb) {
        const auto& sp = exe.file(spans);
        const auto& d = exe.file(data);
        mask.assign(64000, 0);
        rgb.assign(64000 * 3, 0);
        size_t di = 0;
        for (int p = 0; p < 3; p++) {
            size_t o = 0;
            for (size_t k = 0; k + 1 < sp.size();) {
                unsigned skip = sp[k] | sp[k + 1] << 8;
                k += 2;
                if (skip == 0xffff || k + 1 >= sp.size()) break;
                o += skip;
                unsigned cnt = sp[k] | sp[k + 1] << 8;
                k += 2;
                if (cnt == 0xffff) break;
                for (unsigned i = 0; i < cnt; i++, o++, di++) {
                    if (o >= 64000 || di >= d.size()) continue;
                    rgb[o * 3 + size_t(p)] = d[di];
                    mask[o] = 1;
                }
            }
        }
    }

    // The redraw of picture 11 + k with the figure mask at its own resolution: a redraw pixel is part of the figure
    // where the original's mask (file 36 + k) says so two pixels away from its outline; near the outline, where its
    // colour is closer to a colour of the original's figure than to the background colours (palette indices 0 and
    // 102..108 of the picture, notes-parts2d-b section 2).
    void maskRedraw(Ctx& c, int k, const Picture& p) {
        const auto& f = c.exe->file(11 + k);
        Palette pal = vgaPalette(std::span<const uint8_t>(f.data(), 0x300));
        std::vector<bool> used(256, false);
        for (int i = 0; i < 64000; i++)
            if (mask_[k][size_t(i)]) used[f[0x300 + size_t(i)]] = true;
        // palette: rgb, alpha 1 = a figure colour, 0.5 = a background colour, 0 = neither
        std::vector<float> pt(256 * 4, 0.0f);
        for (int i = 0; i < 256; i++) {
            bool bg = i == 0 || (i >= 102 && i <= 108);
            pt[size_t(i) * 4 + 0] = pal[size_t(i)].x;
            pt[size_t(i) * 4 + 1] = pal[size_t(i)].y;
            pt[size_t(i) * 4 + 2] = pal[size_t(i)].z;
            pt[size_t(i) * 4 + 3] = bg ? 0.5f : (used[size_t(i)] ? 1.0f : 0.0f);
        }
        Texture palTex;
        palTex.createRaw(256, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, pt.data(), false);
        // the original mask, eroded and dilated by 2 pixels: R = inside, G = near
        std::vector<uint8_t> band(64000 * 2);
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                bool all = true, any = false;
                for (int v = -2; v <= 2; v++)
                    for (int u = -2; u <= 2; u++) {
                        int xx = std::clamp(x + u, 0, 319), yy = std::clamp(y + v, 0, 199);
                        bool m = mask_[k][size_t(yy * 320 + xx)] != 0;
                        all = all && m;
                        any = any || m;
                    }
                band[size_t(y * 320 + x) * 2] = all ? 255 : 0;
                band[size_t(y * 320 + x) * 2 + 1] = any ? 255 : 0;
            }
        Texture bandTex;
        bandTex.createRaw(320, 200, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, band.data(), false);
        Target& out = redrawStamp_[k];
        out.create(p.tex.w, p.tex.h, false, GL_RGBA8);
        out.bind();
        setBlend(Blend::None);
        maskShader_.use();
        p.tex.bind(0);
        bandTex.bind(1);
        palTex.bind(2);
        maskShader_.set("pic", 0);
        maskShader_.set("band", 1);
        maskShader_.set("pal", 2);
        drawFullscreen();
        palTex.destroy();
        bandTex.destroy();
        haveRedraw_[k] = true;
    }

    void buildShaders() {
        const char* mask = R"(
uniform sampler2D pic;
uniform sampler2D band;
uniform sampler2D pal;
out vec4 o;
void main() {
    ivec2 sz = textureSize(pic, 0);
    ivec2 px = ivec2(gl_FragCoord.xy);              // rows from the top, as the picture
    vec4 c = texelFetch(pic, px, 0);
    vec2 vga = (vec2(px) + 0.5) / vec2(sz) * vec2(320.0, 200.0);
    vec2 b = texelFetch(band, ivec2(vga), 0).rg;
    float m;
    if (b.r > 0.5) m = 1.0;
    else if (b.g < 0.5) m = 0.0;
    else {
        float dFig = 1e9, dBg = 1e9;
        for (int i = 0; i < 256; i++) {
            vec4 p = texelFetch(pal, ivec2(i, 0), 0);
            vec3 d = p.rgb - c.rgb;
            float e = dot(d, d);
            if (p.a > 0.75) dFig = min(dFig, e);
            else if (p.a > 0.25) dBg = min(dBg, e);
        }
        m = dFig < dBg ? 1.0 : 0.0;
    }
    o = vec4(c.rgb * m, m);
}
)";
        // a stamp: the picture over the whole screen, premultiplied
        const char* stamp = R"(
in vec2 vga;
in vec2 q;
uniform sampler2D pic;
uniform int sharp;
out vec4 o;
void main() {
    vec2 uv = vga / vec2(320.0, 200.0);
    o = sharp != 0 ? texSharp(pic, uv) : texture(pic, uv);
}
)";
        const char* show = R"(
in vec2 vga;
uniform sampler2D a;
uniform sampler2D b;
uniform float f;
uniform int first;
uniform float gain;
out vec4 o;
void main() {
    vec2 uv = vga / vec2(320.0, 200.0);
    vec3 va = first != 0 ? vec3(0.0) : texBicubic(a, uv).rgb;
    vec3 c = clamp(mix(va, texBicubic(b, uv).rgb, f), 0.0, 1.0);
    o = vec4(min(c * gain, vec3(1.0)), 1.0);
}
)";
        maskShader_.build(p2b::kPassVS, mask, "dissolve-mask");
        stampShader_.build(p2b::kStampVS, stamp, "dissolve-stamp");
        show_.build(p2b::kScreenVS, show, "dissolve-show");
    }

    // One drawn frame of 0x1535c: the stamp into the previous state, then the zoom-blur with the step's offset.
    void step(int k, const Target& prev, const Target& dst) {
        const Step& st = sched_.steps[size_t(k)];
        Target& work = zoom_.begin(prev);
        if (st.stamp >= 0) {
            work.bind();
            setBlend(Blend::Premul);
            stampShader_.use();
            bool redraw = haveRedraw_[st.stamp];
            if (redraw) {
                redrawStamp_[st.stamp].bindTex(0);
            } else {
                stamp_[st.stamp].bind(0);
            }
            stampShader_.set("pic", 0);
            stampShader_.set("sharp", redraw ? 0 : 1);
            stampShader_.set("rect", 0.0f, 0.0f, 320.0f, 200.0f);
            drawQuad();
        }
        // 0x1554d's maps: src = (119/128) dst + b about the fitted centre (162.1, 93.4), plus the offset
        zoom_.finish(dst, 162.1f, 93.4f, 119.0f / 128.0f, st.dx, st.dy);
    }

    Schedule sched_;
    std::vector<uint8_t> mask_[3], rgb_[3];
    Texture stamp_[3];
    Target redrawStamp_[3];
    bool haveRedraw_[3] = {false, false, false};
    p2b::FeedbackHistory hist_;
    p2b::ZoomBlur zoom_;
    Shader maskShader_, stampShader_, show_;
};

}  // namespace

REGISTER_SCENE(Dissolve, "dissolve");
