// p2's frame copy 360c in true colour (dope/re/notes-p2.md sections 3 to 6 and 8, notes-engine 8.1): the motion
// blur through the blend table 3527:0000 (built by 1967 for the weight s = [1965], 1998 for the grey ramp, cleared
// in part by 3a5d), the label written into the blend buffer after the blend (36a2), the credit band of the render
// buffer and the copy of buffer rows 1..200 to the screen; and on the screen the DAC's fade to white (2580 / 2550).
//
// Evaluated exactly instead of the original's shortcuts:
//   - the blend of palette indices through the table, floor((s new + (256 - s) old) / 256) once per drawn frame, is
//     an exponential moving average of continuous ramp levels: the scene's colours lie on the palette's ramp 0..127
//     (black, dark blue, lavender, white; blue leads, so the average of two indices is much bluer than the average
//     of their colours: the trails fade through dark blue, not grey). Each colour c is split into the level L whose
//     ramp colour has the same component sum (the sum grows along the ramp) and the residual c - ramp(L) (0 on the
//     ramp; what a true-colour redraw or an anti-aliased edge adds); both are averaged and the screen shows
//     ramp(L) + residual, which is c itself where nothing is blended. kSub blends per retrace with the weight
//     1 - (1 - s / 256)^(1 / kSub), in a 16-bit buffer; the floor's darkening (up to 256 / s shades below a still
//     picture) is gone, and the trails of fast motion are kSub copies per retrace instead of one;
//   - the screen between two blends is the last blend followed by a partial one (the weight for the fraction of the
//     step) with the scene at that moment, so the picture moves continuously;
//   - T[240..254][*] = 0 (the label's pixels become black in the next blend, the black trail where it moved away)
//     and the label drawn over the blend, with the label's area covered analytically per output pixel;
//   - the grey ramp of the credit band (T[old < 64 or grey][grey] = grey (3 x + 13 y) / 16, scene pixels into the
//     grey ramp, grey pixels to 0 after 3a5d) as rows whose pixels hold no scene; the text level itself is a closed
//     form in p2.cpp, shown here through the palette's grey ramp (colours 128..191) instead of stopping at 59 / 63;
//   - the DAC fade min(63, v + k) as min(1, colour + k / 63), continuous between retraces.
// Kept: the screen row 199 that is never blended (colour 0, black until the fade). After a jump the buffer is
// rebuilt from black so far back that what came before weighs less than kForget; playing forwards it is stepped.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "scenes/p2.h"

#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif

namespace {

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D acc;       // the blend buffer after the previous blend (5517): ramp level / 127, residual / 2 + 1/2
uniform int hasAcc;          // 0: black (the part start, or the start of a rebuild)
uniform sampler2D frame;     // the render buffer of this blend (4527), colours
uniform float weight;        // its weight
uniform float labelPrev;     // the label position written after the previous blend (36a2)
uniform float labelCur;      // ... after this one
uniform sampler2D label;     // p2-label-blur, 70 x 44, opaque
uniform int labelSharp;
uniform vec2 zeroRows;       // rows in the grey ramp: no scene
uniform vec2 pix;            // one output pixel in VGA units
uniform sampler2D pal;       // p2's DAC palette
uniform sampler2D level;     // component sum / 3 -> the ramp level with that sum, / 127
uniform float levelN;        // entries of `level`
uniform int display;         // 1: the screen (credit text, row 199, the DAC fade)
uniform vec2 greyRows;
uniform int slots;
uniform vec4 slotStrip, slotRow, slotCoef;
uniform sampler2D credits;   // p2-credits: grey = text level / 63, four strips of 32 rows
uniform int creditsSharp;
uniform float creditsHalf;   // half a texel of the credits texture, in picture rows
uniform float fadeAdd;

const vec4 kBlack = vec4(0.0, 0.5, 0.5, 0.5);
// a colour as (ramp level / 127, residual / 2 + 1/2): blended linearly, this is the original's index blend
vec4 toRamp(vec3 c) {
    float s = clamp((c.r + c.g + c.b) / 3.0, 0.0, 1.0);
    float L = 127.0 * texture(level, vec2((s * (levelN - 1.0) + 0.5) / levelN, 0.5)).r;
    return vec4(L / 127.0, (c - dpPalLerp(pal, L)) * 0.5 + 0.5);
}
vec3 fromRamp(vec4 a) { return dpPalLerp(pal, a.r * 127.0) + (a.gba - 0.5) * 2.0; }
// the share of the pixel [p - h / 2, p + h / 2] inside [a, b)
float cover(float a, float b, float p, float h) {
    return clamp((min(p + 0.5 * h, b) - max(p - 0.5 * h, a)) / h, 0.0, 1.0);
}
// 36a2: the label at position a covers x in [a - 70, a), cut off at x = 0, screen rows 8..51
float labelCover(float a, vec2 p) {
    return cover(max(a - 70.0, 0.0), max(a, 0.0), p.x, pix.x) * cover(8.0, 52.0, p.y, pix.y);
}
// the credit text level 0..63 (the grey ramp's index - 128): the strips' levels times their coefficients
float textLevel(vec2 p) {
    float y = 0.0;
    for (int i = 0; i < 4; i++) {
        if (i >= slots) break;
        float row = 32.0 * (slotStrip[i] - 1.0) + clamp(p.y - slotRow[i], creditsHalf, 32.0 - creditsHalf);
        vec2 t = vec2(p.x / 320.0, row / 128.0);
        vec4 m = creditsSharp != 0 ? texSharp(credits, t) : texture(credits, t);
        y += slotCoef[i] * cover(slotRow[i], slotRow[i] + 32.0, p.y, pix.y) * 63.0 * m.r;
    }
    return y;
}
void main() {
    ivec2 ij = ivec2(gl_FragCoord.xy);
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec4 old = hasAcc != 0 ? texelFetch(acc, ij, 0) : kBlack;
    vec4 a = mix(old, toRamp(texelFetch(frame, ij, 0).rgb), weight);
    a = mix(a, kBlack, labelCover(labelPrev, p));                    // T[240..254][*] = 0
    a = mix(a, kBlack, cover(zeroRows.x, zeroRows.y, p.y, pix.y));   // the grey ramp holds no scene
    vec2 lt = vec2((p.x - (labelCur - 70.0)) / 70.0, (p.y - 8.0) / 44.0);
    vec4 l = labelSharp != 0 ? texSharp(label, lt) : texture(label, lt);
    a = mix(a, toRamp(l.rgb), labelCover(labelCur, p));
    if (display == 0) {
        o = a;
        return;
    }
    vec3 c = clamp(fromRamp(a), 0.0, 1.0);
    float y = textLevel(p);
    c = mix(c, dpPalLerp(pal, 128.0 + y), cover(greyRows.x, greyRows.y, p.y, pix.y));
    c *= 1.0 - cover(199.0, 200.0, p.y, pix.y);   // buffer row 200 is never blended: colour 0
    o = vec4(min(c + fadeAdd, vec3(1.0)), 1.0);
}
)";

enum Unit { kAcc, kFrame, kLabel, kCredits, kPal, kLevel };

constexpr int kLevelN = 2048;   // entries of the sum -> level table

}  // namespace

void P2Blur::init(Ctx& c, const Palette& pal) {
    shader_.build(kFullscreenVS, std::string(dope::kGLSL) + kFS, "p2 blur");
    pal_.set(pal);
    // the ramp 0..127 between its entries (as dpPalLerp reads it): the level whose colour has the component sum 3 s,
    // for s = 0..1 (the sum grows along the ramp; where two entries have the same sum, steeply from one to the next)
    float sum[128];
    for (int i = 0; i < 128; i++) {
        const Vec3& v = pal[size_t(i)];
        sum[i] = (v.x + v.y + v.z) / 3.0f + float(i) * 1e-6f;
    }
    std::vector<float> lv(kLevelN);
    int i = 0;
    for (int j = 0; j < kLevelN; j++) {
        float s = float(j) / float(kLevelN - 1);
        while (i < 126 && sum[i + 1] <= s) i++;
        float t = std::clamp((s - sum[i]) / (sum[i + 1] - sum[i]), 0.0f, 1.0f);
        lv[size_t(j)] = s >= sum[127] ? 1.0f : (float(i) + t) / 127.0f;
    }
    level_.createRaw(kLevelN, 1, GL_R16, GL_RED, GL_FLOAT, lv.data(), true, false, false);   // level / 127
    c.picture("p2-label-blur");
    c.picture("p2-credits");
}

void P2Blur::release() {
    for (Target& t : acc_) t.destroy();
    new_.destroy();
    level_.destroy();
    w_ = h_ = 0;
    valid_ = false;
}

void P2Blur::ensure(int w, int h) {
    if (w == w_ && h == h_) return;
    for (Target& t : acc_) t.create(w, h, false, GL_RGBA16);   // 16-bit: the small weights still move the average
    new_.create(w, h, false, GL_RGBA8);   // the scene at display precision (the blend buffer keeps 16 bits)
    w_ = w;
    h_ = h;
    valid_ = false;
}

void P2Blur::pass(Ctx& c, const Target* prev, double prevLabel, const P2Blend& b, const P2Show* show) {
    const Picture& label = c.picture("p2-label-blur");
    const Picture& credits = c.picture("p2-credits");
    setBlend(Blend::None);
    shader_.use();
    gl::ActiveTexture(GL_TEXTURE0 + kAcc);
    gl::BindTexture(GL_TEXTURE_2D, prev ? prev->tex : 0);
    new_.bindTex(kFrame);
    label.tex.bind(kLabel);
    credits.tex.bind(kCredits);
    pal_.bind(kPal);
    level_.bind(kLevel);
    gl::ActiveTexture(GL_TEXTURE0);
    shader_.set("acc", int(kAcc));
    shader_.set("frame", int(kFrame));
    shader_.set("label", int(kLabel));
    shader_.set("credits", int(kCredits));
    shader_.set("pal", int(kPal));
    shader_.set("level", int(kLevel));
    shader_.set("levelN", float(kLevelN));
    shader_.set("hasAcc", prev ? 1 : 0);
    shader_.set("weight", b.weight);
    shader_.set("labelPrev", float(prevLabel));
    shader_.set("labelCur", float(b.label));
    shader_.set("labelSharp", label.redraw ? 0 : 1);
    shader_.set("zeroRows", b.zeroY0, b.zeroY1);
    shader_.set("pix", 320.0f / float(w_), 200.0f / float(h_));
    shader_.set("display", show ? 1 : 0);
    if (show) {
        shader_.set("greyRows", show->greyY0, show->greyY1);
        shader_.set("slots", show->slots);
        shader_.set("slotStrip", show->strip[0], show->strip[1], show->strip[2], show->strip[3]);
        shader_.set("slotRow", show->row[0], show->row[1], show->row[2], show->row[3]);
        shader_.set("slotCoef", show->coef[0], show->coef[1], show->coef[2], show->coef[3]);
        shader_.set("creditsSharp", credits.redraw ? 0 : 1);
        shader_.set("creditsHalf", 0.5f * float(credits.srcH) / float(std::max(1, credits.tex.h)));
        shader_.set("fadeAdd", show->fadeAdd);
    }
    drawFullscreen();
}

void P2Blur::advance(Ctx& c, P2Source& src, int m) {
    const int first = firstBlend();
    m = std::max(m, first - 1);
    if (valid_ && have_ == m) return;
    // the oldest blend that still counts: before it, everything weighs less than kForget (or the part start)
    bool forward = false;
    int m0 = m;
    double keep = 1;
    while (m0 >= first && keep > kForget) {
        if (valid_ && m0 == have_) {   // the buffer we have is recent enough: step forwards from it
            forward = true;
            break;
        }
        keep *= 1.0 - src.blend(m0).weight;
        m0--;
    }
    double label = forward ? haveLabel_ : m0 >= first ? src.blend(m0).label : -1.0;
    bool black = !forward;
    for (int k = m0 + 1; k <= m; k++) {
        P2Blend b = src.blend(k);
        src.drawScene(c, new_, b.r);
        acc_[1 - cur_].bind();
        pass(c, black ? nullptr : &acc_[cur_], label, b, nullptr);
        cur_ = 1 - cur_;
        black = false;
        label = b.label;
    }
    if (black) {   // nothing blended since the start (r before the first blend): black
        acc_[cur_].bind();
        gl::ClearColor(0, 0.5f, 0.5f, 0.5f);   // kBlack: level 0, no residual
        gl::Clear(GL_COLOR_BUFFER_BIT);
    }
    have_ = m;
    haveLabel_ = label;
    valid_ = true;
}

void P2Blur::render(Ctx& c, P2Source& src, double r, const P2Show& show) {
    Target& out = *c.out;
    ensure(out.w, out.h);
    advance(c, src, int(std::floor(r * kSub)));
    P2Blend b = src.partial(r);
    if (b.weight > 0) src.drawScene(c, new_, b.r);
    out.bind();
    pass(c, &acc_[cur_], haveLabel_, b, &show);
}
