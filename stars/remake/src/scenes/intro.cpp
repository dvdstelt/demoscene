// intro: the title pictures and the four name cards, orders 0x00..0x0a (stars/re/notes-parts2d-a.md section 1).
// Ports 0x5a92 (main's script inside the intro: the picture fades 0x627e / 0x6251 and the cards in between),
// 0x5b84 / 0x5bb0 (set-up: the card records 0x542a.., the palette buffer), 0x5c97 (the polar map of a card), 0x5c68 /
// 0x5e7a / 0x5ece (the card loop and the flower), 0x5e63 (the card's text), 0x61b1 + 0x615b (the band pulsing and
// the card fade), 0x5b91 (the PIT callback: the texture angle).
//
// Pictures 9, 8, 14, 10 fade in and out by 2 per retrace over 128 retraces, 11, 12, 13 by 4 over 64 (main's fader,
// a colour multiply). A card shows a "flower": the 256x256 texture of file 7 (four eyes, one per 64-row band) seen
// through a petal-shaped polar map around (160, 100), turning with the PIT, breathing radially, each band pulsing in
// brightness with its own phase; the name (file 6) pulses with band 0, the word (file 5) does not.
//
// Evaluated exactly instead of the original's shortcuts: the polar map per output pixel (rho, theta of the pixel
// instead of 4096 integer rays; the petal radius r(theta) = A (1 + sin m theta) and the radial offset continuous
// instead of truncated to whole texels), filtered texture reads (sharp texel squares with --original-art), the band
// brightness (3 + sin phi) / 4 and the card level as floats instead of 8-bit products on 6-bit colours, the band
// phases and the card fade blended between drawn frames. The one-frame lag of the radial offset (14 ms) is dropped.
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"

using namespace stars;

namespace {

// The flower of a name card: file 7 through the polar map, per output pixel (0x5c97 + 0x5ece).
const char* kFlowerFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;     // file 7: 256 x 256 texels (redraw or original pixels), premultiplied
uniform int sharp;         // original pixels: sharp texel squares
uniform float petals;      // m: 3, 13, 3, 4
uniform float depth;       // A: 16 or 32 (the shift cl of the record)
uniform float uOff;        // radial texture offset (texels)
uniform float vOff;        // texture angle 2 n (texels)
uniform vec4 band;         // brightness of the bands 0..3, times the card level
uniform vec2 px;           // VGA units per output pixel (x, y)

// texSharp with explicit derivatives (the polar map has a seam where theta wraps)
vec4 sharpGrad(sampler2D t, vec2 c, vec2 gx, vec2 gy) {
    vec2 sz = vec2(textureSize(t, 0));
    vec2 p = c * sz;
    vec2 seam = floor(p + 0.5);
    vec2 d = max(abs(gx * sz) + abs(gy * sz), vec2(1e-5));
    p = seam + clamp((p - seam) / d, -0.5, 0.5);
    return textureGrad(t, p / sz, gx, gy);
}

void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // VGA coordinates
    vec2 d = p - vec2(160.0, 100.0);                       // from the centre of the rays, original pixels
    float rho = length(d);
    float th = atan(d.y, d.x);                             // theta grows clockwise on screen (y down)
    float r = depth * (1.0 + sin(petals * th));            // petal depth 0x1400 table: (S[m i] + 0x10000) >> cl
    // drawn where rho <= 99 - r on the rays (pixel centres up to half a pixel further, r truncated: 100 - r)
    float edge = 100.0 - r - rho;
    // texel: u = r + k (k = steps along the ray) + u_off, v = theta * 256 / pi + 2 n (texel centres: -0.5 for the
    // truncations of r and u_off, +0.5 for the centre)
    float u = r + rho + uOff - 0.5;
    float v = th * 256.0 / PI + vOff;
    // derivatives per output pixel, without the seam of atan
    vec2 dx = vec2(px.x, 0.0), dy = vec2(0.0, -px.y);
    float rr = max(rho, 1e-3);
    vec2 gRho = d / rr, gTh = vec2(-d.y, d.x) / (rr * rr);
    vec2 gR = depth * petals * cos(petals * th) * gTh;
    vec2 gU = gR + gRho, gV = gTh * 256.0 / PI;
    float cover = clamp(edge / max(length(vec2(dot(gU, dx), dot(gU, dy))), 1e-4) + 0.5, 0.0, 1.0);
    float vw = mod(v, 256.0);
    vec2 c = vec2(u, vw) / 256.0;
    vec2 gx = vec2(dot(gU, dx), dot(gV, dx)) / 256.0, gy = vec2(dot(gU, dy), dot(gV, dy)) / 256.0;
    vec4 col = sharp != 0 ? sharpGrad(tex, c, gx, gy) : textureGrad(tex, c, gx, gy);
    // band j = texel rows 64 j .. 64 j + 63 (palette colours 0x10 + 16 j ..): from the nearest eye centre, so that
    // a redraw's soft edge just above row 64 j belongs to its own eye
    int j = int(floor((vw + 1.0) / 64.0)) & 3;
    float b = j == 0 ? band.x : j == 1 ? band.y : j == 2 ? band.z : band.w;
    o = vec4(col.rgb * b * cover, cover);
}
)";

// A card record (0x542a, 0x5437, 0x5444, 0x5451: 13 bytes, static).
struct CardRecord {
    int name = 0, word = 0;  // image of file 6 / of file 5
    int petals = 3;          // step / 4 (bytes of the sine table per ray)
    float depth = 16;        // 2^(16 - cl)
};

// One step of 0x5a92's script: a picture with its fades, or a card.
struct Show {
    bool card = false;
    int file = 0;              // picture
    Fade in, out;              // picture: main's fades (0x17e96 / 0x17eba)
    uint32_t rec = 0;          // card record
    double t0 = 0, t1 = 0;     // card: 0x5e7a (start), the loop's end (row 0x3f)
    int steps0 = 0;            // card: band phase steps (0x61b1 calls) before its first loop frame
};

class Intro : public Scene {
public:
    ~Intro() override {
        texels_.destroy();
        if (flower_.id()) gl::DeleteProgram(flower_.id());
    }

    void init(Ctx& c) override {
        flower_.build(kFullscreenVS, kFlowerFS, "intro flower");
        for (uint32_t a : {0x542au, 0x5437u, 0x5444u, 0x5451u}) {
            CardRecord r;
            r.name = c.exe->u16(a);
            r.word = c.exe->u16(a + 2);
            r.depth = float(65536 >> c.exe->u8(a + 4));
            r.petals = int(c.exe->u32(a + 5) / 4);
            cards_[a] = r;
        }
        for (int j = 0; j < 5; j++) phase0_[j] = c.exe->s32(0x5a7a + 4 * uint32_t(j));
        for (int n : {9, 8, 11, 12, 13, 14, 10}) c.picture(n);  // load the textures now
        c.picture(5);
        c.picture(6);
        if (!c.picture(7).redraw) {
            // the original texels with mipmaps: the polar map minifies the texture strongly near the centre
            const auto& f = c.exe->file(7);
            Palette pal = vgaPalette(std::span<const uint8_t>(f.data(), 0xc0), 0x10);
            Image im;
            im.w = im.h = 256;
            im.rgba.resize(256 * 256 * 4);
            for (size_t i = 0; i < 256 * 256; i++) {
                const Vec3& col = pal[f[0xc0 + i]];
                im.rgba[i * 4 + 0] = uint8_t(clamp01(col.x) * 255.0f + 0.5f);
                im.rgba[i * 4 + 1] = uint8_t(clamp01(col.y) * 255.0f + 0.5f);
                im.rgba[i * 4 + 2] = uint8_t(clamp01(col.z) * 255.0f + 0.5f);
                im.rgba[i * 4 + 3] = 255;
            }
            texels_.create(im, true, true);
        }
    }

    void render(Ctx& c, double t) override {
        if (shows_.empty() || start_ != c.start || minute_ != c.minute) schedule(c);
        for (const Show& s : shows_) {
            if (s.card) {
                if (t >= s.t0 && t < s.t1) drawCard(c, s, t);
            } else {  // shown with a black palette, faded in, held, faded out
                double level = t < s.out.t0 ? s.in.level(t) : s.out.level(t);
                if (level > 0) c.drawPicture(c.picture(s.file), {.mul = fadeMul(level)});
            }
        }
    }

private:
    // main's script 0x5a92 as music times: each step starts when main gets there and the position has passed.
    void schedule(const Ctx& c) {
        shows_.clear();
        start_ = c.start;
        minute_ = c.minute;
        double t = c.start;
        const double frame = 1.0 / kRetraceHz;
        auto wait = [&](int pos) { t = std::max(t, c.afterPos(pos)); };
        // 0x627e (step 2, 128 retraces, out after row 0x2e) / 0x6251 (step 4, 64, row 0x30); a fade of n steps
        // writes the palette n + 1 times, one per retrace
        auto picture = [&](int order, int file, bool slow) {
            int step = slow ? 2 : 4, n = slow ? 128 : 64, row = slow ? 0x2e : 0x30;
            wait(order << 8);
            Show s;
            s.file = file;
            s.in = mainFade(t, 0, step, n);
            t += (n + 1) * frame;
            wait(order << 8 | row);
            s.out = mainFade(t, 256, -step, n);
            t += (n + 1) * frame;
            shows_.push_back(s);
        };
        int steps = 0;  // calls of 0x61b1 so far (the band phases are never reset)
        auto card = [&](int order, uint32_t rec) {
            wait(order << 8);
            Show s;
            s.card = true;
            s.rec = rec;
            s.t0 = t;
            // the loop draws one frame per retrace and ends after the first frame that sees row 0x3f
            double end = c.afterPos(order << 8 | 0x3e);
            int frames = std::max(1, int(std::ceil((end - t) * kRetraceHz + 0.5)));
            s.t1 = t + frames * frame;
            s.steps0 = steps + 1;  // 0x5e7a calls 0x61b1 once before the loop
            steps += frames + 1;
            t = s.t1;
            shows_.push_back(s);
        };
        bool odd = (c.minute & 1) != 0;
        picture(0x0, 9, true);
        picture(0x1, 8, true);
        card(0x2, odd ? 0x5437 : 0x542a);  // 0x5c32
        picture(0x3, 11, false);
        card(0x4, odd ? 0x542a : 0x5437);  // 0x5c47
        picture(0x5, 12, false);
        card(0x6, 0x5444);
        picture(0x7, 13, false);
        card(0x8, 0x5451);
        picture(0x9, 14, true);
        picture(0xa, 10, true);
    }

    void drawCard(Ctx& c, const Show& s, double t) {
        const CardRecord& rec = cards_.at(s.rec);
        // drawn frames since 0x5e7a (frame k is on screen from k retraces after it), blended between frames
        double k = c.retraces(t, s.t0);
        // card level (0x615b): +2 per frame from 0 to 128; the fade out starts with the first frame that sees 290
        // PIT ticks since 0x5e7a (the first tick comes half a tick after it on average), then -2 per frame
        int kOut = int(std::ceil((290 - 0.5) / kPitHz * kRetraceHz));
        double level = std::min(128.0, 2.0 * k) - 2.0 * std::clamp(k - (kOut + 1), 0.0, 64.0);
        double L = std::max(0.0, level) / 128.0;
        // band phases (0x61b1): +0x190 per call, byte offsets into the sine table (0x4000 per turn)
        double calls = s.steps0 + k;
        auto bright = [&](int j) {
            double ph = phase0_[j] + 0x190 * calls;
            return (3.0 + std::sin(2.0 * kPi * ph / 16384.0)) / 4.0;
        };
        // PIT: tex_angle = 2 n, u_off = 16 (1 + sin(2 pi n / 64)), n = ticks since 0x5a92 installed the callback
        double n = c.ticks(t);
        double uOff = 16.0 * (1.0 + std::sin(2.0 * kPi * n / 64.0));

        const Picture& tex = c.picture(7);
        setBlend(Blend::Premul);
        flower_.use();
        (tex.redraw ? tex.tex : texels_).bind(0);
        flower_.set("tex", 0);
        flower_.set("sharp", tex.redraw ? 0 : 1);
        flower_.set("petals", float(rec.petals));
        flower_.set("depth", rec.depth);
        flower_.set("uOff", float(uOff));
        flower_.set("vOff", float(std::fmod(2.0 * n, 256.0)));
        flower_.set("band", float(bright(1) * L), float(bright(2) * L), float(bright(3) * L), float(bright(4) * L));
        flower_.set("px", 320.0f / float(c.w), 200.0f / float(c.h));
        drawFullscreen();
        // the text (0x5e63): opaque over the flower; the name pulses with band 0
        float b0 = float(bright(0) * L);
        c.drawPicture(c.picture(5), {.x = 88, .y = 150, .w = 144, .h = 36, .sy = 36.0f * float(rec.word), .sh = 36,
                                     .mul = Vec3(float(L), float(L), float(L))});
        c.drawPicture(c.picture(6), {.x = 96, .y = 60, .w = 128, .h = 74, .sy = 74.0f * float(rec.name), .sh = 74,
                                     .mul = Vec3(b0, b0, b0)});
    }

    Shader flower_;
    Texture texels_;  // --original-art: file 7's texels, mipmapped
    std::map<uint32_t, CardRecord> cards_;
    double phase0_[5] = {};
    std::vector<Show> shows_;
    double start_ = -1;
    int minute_ = -1;
};

}  // namespace

REGISTER_SCENE(Intro, "intro");
