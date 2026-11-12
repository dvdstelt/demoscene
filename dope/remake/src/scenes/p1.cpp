// p1: the "Complex Media Labs" logo scroller (dope/re/notes-core.md sections 4 and 8). The 1024 x 195 chrome logo
// (`p1-logo`, a map of 64 shade levels) slides in from the right and out to the left at half a pixel per retrace
// over black, rows 0..193 (rows 194..199 are below the line-compare split: black), while its colours cross-fade
// from the violet ramp A (p1 cs:04F0) to the orange ramp B (cs:07F8).
//
// Ports: the set-up and main loop 0bd5..0d1f (the logo blit 0b5c into mode X, the virtual screen 1344 pixels wide
// with the logo at virtual x 320..1343, the exit at pos > 1500h), the per-retrace handler 0020 with the start
// address 0b8b and the pel panning 0ba3 (scroll = pos / 4 pixels, pos += 2 per retrace), the weight tables 0b00
// built from pos - 1F4h by the main loop, and the DAC mix 0b24.
//
// Evaluated exactly instead of the original's shortcuts:
//   - the scroll in whole pixels every second retrace (start address in 4-pixel units + panning) is continuous:
//     x = 320 + column - r / 2 (r = retraces, continuous);
//   - the weights (1/256 steps every 8 retraces, both products floored, up to 2/63 darker) are a continuous
//     w = clamp((2 r - 500) / 4096, 0, 1), mixed in float: colour = (1 - w) A(s) + w B(s);
//   - the 64 shade indices are a continuous shade s (filtered redraw, or the original pixels with anti-aliased
//     edges), coloured by the ramps interpolated between their 64 entries (dpPalLerp);
//   - the split's edge at row 194 and the logo's ends are covered analytically per output pixel.
// Dropped: the never-shown last logo row (picture row 195), the misc register's 480-line sync polarity.
#include <algorithm>
#include <string>

#include "demo.h"
#include "dope/core.h"
#include "exe.h"

namespace {

// 0d0e cmp [16A], 1500h / ja: p1 exits after the run that makes pos 1502h, so pos 1500h (2688 retraces) is the last
// position on screen.
constexpr double kLastPos = 0x1500;
// 0ce8 sub dx, 1F4h: the cross-fade weight is taken from pos - 500 (0b00: w = clamp(dx >> 4, 0, 256) / 256).
constexpr double kFadeStart = 0x1f4;
// The virtual screen: 0c8a..0cac blit the logo at byte 50h of each 336-byte line (CR13 = A8h words): x 320..1343.
constexpr float kLogoX = 0x50 * 4;
// The line compare at scan line 183h (0bd5): screen rows from (183h + 1) / 2 = 194 on show VGA offset 0 (black).
constexpr float kSplitRow = (0x183 + 1) / 2;
constexpr float kLogoW = 1024;  // the logo frame (pictures.cpp: rows 1..195 of the RIX3 picture)
constexpr float kLogoH = 195;

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D logo;     // the logo frame: grey = shade / 63 (premultiplied alpha)
uniform sampler2D palA;     // the violet ramp, colours 0..63
uniform sampler2D palB;     // the orange ramp
uniform int sharp;          // 1: the original's pixels (sharp squares), 0: a redraw (filtered)
uniform float logoX;        // VGA x of the logo's column 0
uniform float w;            // weight of ramp B
uniform float split;        // first black row (the line compare)
uniform vec2 pix;           // one output pixel in VGA units
uniform vec2 logoSize;
void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec2 q = vec2(p.x - logoX, p.y);                    // logo pixels
    // the share of this output pixel that lies inside the logo's columns and above the split
    float cx = clamp((q.x + 0.5 * pix.x) / pix.x, 0.0, 1.0) * clamp((logoSize.x - q.x + 0.5 * pix.x) / pix.x, 0.0, 1.0);
    float cy = clamp((split - p.y + 0.5 * pix.y) / pix.y, 0.0, 1.0);
    float cover = cx * cy;
    vec2 t = clamp(q, vec2(0.0), logoSize) / logoSize;     // (sampled everywhere: the filters need derivatives)
    vec4 g = sharp != 0 ? texSharp(logo, t) : texture(logo, t);
    float s = clamp(g.a > 0.0 ? g.r / g.a : 0.0, 0.0, 1.0) * 63.0;   // the shade level, continuous
    vec3 c = mix(dpPalLerp(palA, s), dpPalLerp(palB, s), w) * g.a;
    o = vec4(c * cover, 1.0);
}
)";

class LogoScene : public Scene {
public:
    void init(Ctx& c) override {
        const DopeImage& p1 = c.exe->part(1);
        // 0b24 mixes the first 64 colours (192 bytes) of the two 768-byte palettes
        palA_.set(vgaPalette(p1.csSpan(0x04f0, 192)));
        palB_.set(vgaPalette(p1.csSpan(0x07f8, 192)));
        c.picture("p1-logo");  // load (and upload) now
        shader_.build(kFullscreenVS, std::string(dope::kGLSL) + kFS, "p1");
    }

    void render(Ctx& c, double t) override {
        // record start + n shows the start address, panning and DAC of pos = 2 n (PORT.md "Time")
        double pos = std::clamp(2.0 * c.retraces(t), 0.0, kLastPos);
        const Picture& logo = c.picture("p1-logo");
        setBlend(Blend::None);
        shader_.use();
        logo.tex.bind(0);
        palA_.bind(1);
        palB_.bind(2);
        shader_.set("logo", 0);
        shader_.set("palA", 1);
        shader_.set("palB", 2);
        shader_.set("sharp", logo.redraw ? 0 : 1);
        shader_.set("logoX", float(kLogoX - pos / 4.0));   // virtual x 320 + column, scrolled by pos >> 2
        shader_.set("w", float(dope::weight(pos - kFadeStart)));
        shader_.set("split", kSplitRow);
        shader_.set("pix", 320.0f / float(c.out->w), 200.0f / float(c.out->h));
        shader_.set("logoSize", kLogoW, kLogoH);
        drawFullscreen();
    }

private:
    Shader shader_;
    dope::PaletteTexture palA_, palB_;
};

}  // namespace

REGISTER_SCENE(LogoScene, "p1");
