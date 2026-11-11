// The title ("psychic link" / "sixty four k", ticks 104-1079) and "the end" (10968-11377): the text routine 0x2829
// with its per-tick callback 0x27ae (paper/re/notes-title.md §2). Letters made by 0x2936 (40x40 sprites, one 0xf47
// blur, every second column), placed on pens, jittered by 0x29e7 (table 0x2a42) on every music-counter change and
// drawn by 0x28db (0x2a91 shadow: subtract, 0x2a52 copies: add half) into the palette-index image of the warp
// 0x2dc8 / 0x3095 (core warp.h), coloured by the hue ramp 0x14dc whose angle 0x27ae turns by one step per tick.
// The fade (in from black, out to white) is the core post pass.
//
// High resolution: the warp is evaluated per pixel (core), the letters are the core font's SDF glyphs squashed to
// w/2 x 32 VGA px and blurred by the Gaussian of one 0xf47 call at the sprite's doubled horizontal resolution
// (sigma 0.5 x 1.0 VGA px), pre-rendered once into an atlas of 40x40-VGA-px cells. The index arithmetic stays the
// original's: I = min(255, max(0, I_bg - shadow) + sum of the three half copies), then the ramp.
//
// Timing (checked against paper/re/full.frames, see the notes at TextPart): the capture's DAC palette at record t
// has hue angle t - t0, its pictures the oscillators advanced t - t0 times and the jitter / letter count of the
// music counter of tick t - 1.
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "paper/blur.h"
#include "paper/common.h"
#include "paper/font.h"
#include "paper/gpu.h"
#include "paper/osc.h"
#include "paper/palette.h"
#include "paper/textures.h"
#include "paper/warp.h"

namespace {

using namespace paper;

constexpr uint32_t kTitleText = 0x463a;   // "psychic linksixty four k"
constexpr uint32_t kEndText = 0x4653;     // "   t h e          e n d"
constexpr uint32_t kJitterTable = 0x2a42; // 16 signed bytes
constexpr uint32_t kStepTable = 0x2790;   // 15 x (music counter, letters added)
constexpr int kRecords = 24;              // 0x2936 / 0x29e7 / 0x28db loop counts
constexpr int kSteps = 15;                // 0x27f8: mov ecx, 0xf

// ---------------------------------------------------------------- letters (CPU side)

struct Offset { int dx, dy; };
using Jitter = std::array<std::array<Offset, 4>, kRecords>;  // [record][k]: k = 0 shadow, 1..3 copies

// 0x29e7: for every record and k = 0..3: dy = J[rand & 15], dx = J[rand & 15]
Jitter rollJitter(Rng& rng, const int8_t (&J)[16]) {
    Jitter j{};
    for (auto& rec : j)
        for (Offset& o : rec) {
            o.dy = J[rng.next() & 15];
            o.dx = J[rng.next() & 15];
        }
    return j;
}

struct Record {
    char ch = 0;
    int x = 0, y = 0;  // pen: top-left of the 40x40 sprite on screen
};

// 0x2936: pens from (5, 50), second line from record 12 at (5, 110); advance = ink width / 2 + 15 (space: 15)
std::array<Record, kRecords> layoutRecords(const PaperExe& exe, uint32_t text, const FontData& f) {
    std::array<Record, kRecords> r{};
    int x = 5, y = 50;
    for (int i = 0; i < kRecords; i++) {
        if (i == 12) { x = 5; y = 110; }
        char ch = char(exe.u8(text + i));
        r[i] = {ch, x, y};
        x += (FontData::glyphIndex(ch) < 0 ? 0 : f.width(ch) >> 1) + 15;
    }
    return r;
}

// ---------------------------------------------------------------- GPU resources shared by both parts

const char* kVgaVS = R"(
out vec2 vga;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vga = vec2(p.x * 320.0, (1.0 - p.y) * 200.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// one sprite cell of the atlas onto a VGA rectangle, weighted into (shadow, copies)
const char* kSpriteVS = R"(
uniform vec4 rect;   // x, y, w, h in VGA coordinates
uniform vec4 uvr;    // atlas u0, v0, u1, v1
out vec2 uv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    uv = mix(uvr.xy, uvr.zw, c);
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
const char* kSpriteFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D atlas;
uniform vec2 weight;
void main() { o = vec4(weight * texture(atlas, uv).r, 0.0, 0.0); }
)";

// 0x3095 + 0x28db + palette: background index (T2 row 10 = texel * 10/32), shadow subtracted, copies added
const char* kCompositeFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D cloud;
uniform sampler2D letters;   // r = shadow, g = copies (byte / 255)
uniform vec3 base;
void main() {
    float bg = texture(cloud, warpTexel(vga) / 256.0).r * 255.0 * (10.0 / 32.0);
    vec2 l = texture(letters, vec2(vga.x / 320.0, 1.0 - vga.y / 200.0)).rg * 255.0;
    float idx = min(255.0, max(0.0, bg - l.x) + l.y);
    o = vec4(hueRamp(base, idx), 1.0);
}
)";

struct TextGpu {
    // Sprite atlas: 6 x 5 cells of 40x40 VGA px (glyph 'a' + g in cell g), kTexel texels per VGA px.
    static constexpr int kCols = 6, kRows = 5, kCell = 40, kTexel = 8;
    Target atlas;
    Shader sprite, composite;

    explicit TextGpu(Ctx& c) {
        sprite.build(kSpriteVS, kSpriteFS, "title sprite");
        composite.build(kVgaVS, std::string(kWarpGLSL) + kPaletteGLSL + kCompositeFS, "title composite");
        buildAtlas(c);
    }

    // 0x2936: glyph at sprite column 20, row 4 of an 80x40 byte sprite, one 0xf47 (sigma 1 sprite px), drawn from
    // every second column: on screen the ink starts at (10.25, 4) (sprite column 20's left edge, see glyphX) and
    // is w/2 x 32 px, blurred by sigma (0.5, 1.0) px.
    void buildAtlas(Ctx& c) {
        GlState saved;
        const float vw = float(kCols * kCell), vh = float(kRows * kCell);
        atlas.create(int(vw) * kTexel, int(vh) * kTexel, false, GL_R16F);
        atlas.bind();
        c.clear();
        std::vector<GlyphPos> gs;
        for (int g = 0; g < 26; g++) {
            GlyphPos p;
            p.glyph = g;
            p.x = float((g % kCols) * kCell) + glyphX;
            p.y = float((g / kCols) * kCell) + 4.0f;
            p.sx = 0.5f;
            p.sy = 1.0f;
            gs.push_back(p);
        }
        c.shared<Font>().draw(gs, Font::pixelProj(vw, vh), GlyphOp::Add, 1.0f);
        blurSigma(c, atlas, 0.5 * kTexel, 1.0 * kTexel, Edge::Zero, GL_R16F);
        gl::BindTexture(GL_TEXTURE_2D, atlas.tex);
        gl::GenerateMipmap(GL_TEXTURE_2D);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    // Screen pixel X of a sprite drawn at pen P shows sprite column 2(X - P): sprite column u (edge coordinate)
    // sits at VGA x = P + (u + 0.5) / 2, so the glyph's first column (u = 20) starts at P + 10.25.
    static constexpr float glyphX = 10.25f;

    // atlas texture rectangle of glyph g (targets are stored bottom-up)
    void cell(int g, float& u0, float& v0, float& u1, float& v1) const {
        u0 = float(g % kCols) / kCols;
        u1 = float(g % kCols + 1) / kCols;
        v0 = 1.0f - float(g / kCols) / kRows;
        v1 = 1.0f - float(g / kCols + 1) / kRows;
    }
};

// ---------------------------------------------------------------- the part

struct TextConfig {
    uint32_t text;       // 24 records
    int callbackTick;    // first record whose state comes from 0x27ae (title 106, the end 10970)
    int visible0;        // [0x2245d] at setup
    WarpShift shift;     // [0x2efc], [0x2f02]
    double hue0;         // [0x14dd] in the first callback
    uint64_t rngJitter, rngOsc, rngRejitter;  // RngPos of 0x29e7 at setup, 0x2756, first re-roll in 0x27ae
    bool blackBefore;    // nothing drawn before the first callback (title: the screen stays black)
};

// Timing against the capture (paper/re/full.frames + full.ticks, checked exactly with the original's integer model):
//  - DAC palette of record t: hue angle hue0 + (t - callbackTick);
//  - pictures of records with two frames in the tick: oscillators advanced (t - callbackTick) times (records with
//    one frame: one tick less, they were started in the previous tick), i.e. first advance at callbackTick + 1;
//  - the jitter and the letter count change one tick after the music counter (0x27ae compares the counter before
//    the player's update in the same IRQ): record t shows the re-roll for counter(t - 1); the first callback
//    re-rolls at once, so the re-roll number is counter(t - 1) - counter(callbackTick).
class TextPart : public Scene {
public:
    explicit TextPart(TextConfig cfg) : cfg_(cfg) {}

    void init(Ctx& c) override {
        const PaperExe& exe = *c.exe;
        const FontData& f = c.shared<Font>().data();
        records_ = layoutRecords(exe, cfg_.text, f);
        int8_t J[16];
        for (int k = 0; k < 16; k++) J[k] = exe.s8(kJitterTable + k);
        for (int k = 0; k < kSteps; k++) steps_[k] = {exe.u8(kStepTable + 2 * k), exe.u8(kStepTable + 2 * k + 1)};
        Rng rng = Rng::at(exe, cfg_.rngJitter);
        setupJitter_ = rollJitter(rng, J);
        osc_ = OscSet::createAt(exe, cfg_.rngOsc, 300, cfg_.callbackTick + 1);
        rng.skip(cfg_.rngRejitter - rng.calls());
        // one re-roll per music-counter value seen by the callbacks (title 117, the end 50)
        int c0 = c.counter(cfg_.callbackTick), c1 = c.counter(cfg_.callbackTick + 1100);
        int n = std::clamp(c1 - c0 + 2, 1, 512);
        rerolls_.reserve(size_t(n));
        for (int k = 0; k < n; k++) rerolls_.push_back(rollJitter(rng, J));
        c.shared<TextGpu>();
        c.shared<CoreTextures>();
    }

    void render(Ctx& c, double tick) override {
        if (cfg_.blackBefore && tick < cfg_.callbackTick) {
            c.clear();
            return;
        }
        const int t = int(std::floor(tick));
        TextGpu& gpu = c.shared<TextGpu>();
        Target& out = *c.out;

        // letters: r = shadow, g = sum of the three half-strength copies (byte / 255)
        auto letters = c.tempSized(out.w, out.h, false, GL_RGBA16F);
        letters->bind();
        c.clear();
        drawLetters(gpu, jitterAt(c, t), visibleAt(c, t));

        // background, letters, palette
        out.bind();
        gl::Disable(GL_BLEND);
        const Shader& sh = gpu.composite;
        sh.use();
        setWarpUniforms(sh, warpField(osc_, std::max(tick, double(cfg_.callbackTick))), cfg_.shift);
        c.shared<CoreTextures>().cloud.bind(0);
        letters->bindTex(1);
        sh.set("cloud", 0);
        sh.set("letters", 1);
        double angle = cfg_.hue0 + std::max(0.0, tick - cfg_.callbackTick);
        sh.set("base", hueBase(angle, 11));
        drawFullscreen();
    }

private:
    // 0x27ae: re-roll number of record t (-1: the setup's jitter)
    const Jitter& jitterAt(const Ctx& c, int t) const {
        if (t - 1 < cfg_.callbackTick) return setupJitter_;
        int r = c.counter(t - 1) - c.counter(cfg_.callbackTick);
        return rerolls_[size_t(std::clamp(r, 0, int(rerolls_.size()) - 1))];
    }

    // [0x2245d]: + table 0x2790 entry when the counter (seen one tick late) reaches its step (counters >= 0x20,
    // < 512). From 512 on (the end) the callback adds 1 per fourth counter, without effect: its string ends with a
    // zero record that stops 0x28db.
    int visibleAt(const Ctx& c, int t) const {
        if (t - 1 < cfg_.callbackTick) return cfg_.visible0;
        int v = cfg_.visible0;
        int cnt = c.counter(t - 1), c0 = c.counter(cfg_.callbackTick);
        for (const auto& s : steps_)
            if (s[0] >= 0x20 && s[0] <= cnt && s[0] > c0 && cnt < 512) v += s[1];
        return std::min(v, kRecords);
    }

    // 0x28db: in record order, stop at char 0, skip spaces; shadow at (dx0 - 3, dy0 + 3), copies k = 1..3 at (dxk, dyk)
    void drawLetters(const TextGpu& gpu, const Jitter& jit, int visible) const {
        gl::Enable(GL_BLEND);
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE);
        const Shader& sh = gpu.sprite;
        sh.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, gpu.atlas.tex);
        sh.set("atlas", 0);
        const float cell = float(TextGpu::kCell);
        for (int i = 0; i < visible; i++) {
            const Record& r = records_[size_t(i)];
            if (r.ch == 0) break;
            int g = FontData::glyphIndex(r.ch);
            if (g < 0 || g >= 26) continue;
            float u0, v0, u1, v1;
            gpu.cell(g, u0, v0, u1, v1);
            sh.set("uvr", u0, v0, u1, v1);
            for (int k = 0; k < 4; k++) {
                Offset o = jit[size_t(i)][size_t(k)];
                float x = float(r.x + o.dx - (k == 0 ? 3 : 0)), y = float(r.y + o.dy + (k == 0 ? 3 : 0));
                sh.set("rect", x, y, cell, cell);
                sh.set("weight", k == 0 ? 1.0f : 0.0f, k == 0 ? 0.0f : 0.5f);
                drawQuad();
            }
        }
        gl::Disable(GL_BLEND);
    }

    TextConfig cfg_;
    std::array<Record, kRecords> records_{};
    std::array<std::array<int, 2>, kSteps> steps_{};
    Jitter setupJitter_{};
    std::vector<Jitter> rerolls_;
    OscSet osc_;
};

// Title: [0x2245d] = 0, shifts 5/7, hue angle from 0 (exe value), oscillators 0x2756(300) at tick 106.
struct TitlePart : TextPart {
    TitlePart()
        : TextPart({kTitleText, 106, 0, WarpShift::title(), 0.0, RngPos::kTitleJitter, RngPos::kTitleOsc,
                    RngPos::kTitleRejitter, true}) {}
};

// The end: [0x2245d] = 0x18 (all letters), shifts 5/5, the hue angle the greetings part left (0x944 + its 538
// counted ticks = 2910, from the capture's DAC palettes), oscillators and jitter from the generator state the flock
// left in the capture (RngPos::kEnd*).
constexpr double kEndHue0 = 2910.0;
struct TheEndPart : TextPart {
    TheEndPart()
        : TextPart({kEndText, 10970, 0x18, WarpShift::theEnd(), kEndHue0, RngPos::kEndJitter, RngPos::kEndOsc,
                    RngPos::kEndRejitter, false}) {}
};

REGISTER_SCENE(TitlePart, "title");
REGISTER_SCENE(TheEndPart, "theend");

}  // namespace
