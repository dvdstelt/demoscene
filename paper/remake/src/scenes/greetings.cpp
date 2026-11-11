// Greetings (ticks 7741-8818): the greetings names embossed into a purple, metallic-looking surface lit by one
// light on an ellipse (paper/re/notes-water.md). There is no water simulation: a static height map is warped,
// the names are added, and the result is bump / environment lit through a light map.
//
// Ports (code32):
//   0x24df  set-up: glyph strip (font doubled vertically, 4 cross-blur passes), slots, palette angle 0x944 / S = 11
//   0x26aa  per-tick callback: palette 0x14dc angle +1 while [0x1e4e6] > 3, light angle +1, names 0x2478,
//           oscillators 0x26d0
//   0x2dc8 / 0x3095  warp grid as the height map: cloud texture bank 0 at the warped position, * 20/32
//   0x26e7 / 0x2700  names into the height map: max(height, blurred glyph * level / 32)
//   0xd28   bump / environment lighting: light map at (x/2 + A + gx, y/2 + E + gy), g = half the central difference
//   0xc80   the 512x256 light map (radial sqrt falloff with the hot-spot step + half the cloud texture, mirrored)
//
// Evaluated exactly instead of the original's shortcuts (all per output pixel):
//   - height map: the warp field of the core (no 8x8 blocks) into the high-resolution cloud texture, sampled
//     bicubic (no nearest texels, no 8-bit heights);
//   - names: the core's high-resolution glyphs, doubled vertically and blurred with a Gaussian of the 4 cross
//     passes' variance (sigma = sqrt 2 px), max-blended at their per-tick levels (interpolated between ticks);
//   - gradient: each part's slope is 0xd28's half central difference over +-1 original pixel (the same slope scale
//     and smoothing), taken from the part on top at each output pixel, so the crease where a name rises out of the
//     background stays sharp instead of smearing over two original pixels;
//   - light map 0xc80 as its continuous function (no 2x2-pixel texels, no 7-bit gradient), the hot-spot step kept;
//   - palette 0x14dc as the continuous ramp at the interpolated angle (the fade level is the core's post pass).
// Dropped: the 1-pixel black border, the glyph cells' clipping of the blur tails, the light-map reads one row
// outside the map (the function continues there).
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "music.h"
#include "paper/core2d.h"
#include "paper/gpu.h"

namespace {

using namespace paper;

// ---- timing (notes-water.md §1, §4, §6)
constexpr int kFirstIrq = 7744;       // first tick whose IRQ runs the callback 0x26aa
constexpr int kLastTick = 8818;       // the plane flock starts here
constexpr int kLightAngle0 = 88;      // [0x22469] as the 3D scene leaves it (a run-time value, not in the exe)
constexpr int kPalAngle0 = 0x944;     // 0x24df: [0x14dd] = 0x944, shift [0x1505] = 0xb
constexpr int kPalShift = 11;

// ---- names (0x2478 / 0x26e7)
constexpr uint32_t kNameList = 0x23e8;  // NUL-separated strings, the list ends with an empty one
constexpr int kLines = 4;               // slots at 0x2464, line y = 46 * slot
constexpr int kLineY = 46, kTextX = 5, kSpace = 30, kLetterGap = 4;
constexpr int kFullLevel = 32;          // 0x20: T2 row 32 = glyph unchanged

// ---- glyph strip 0x24df: cell 52 x 64 screen pixels, glyph ink from column 2, font rows doubled
constexpr int kCellW = 52, kCellH = 64, kInkX = 2;
constexpr int kPad = 5;                 // atlas padding around a cell (the blur tail reaches ~4.2 px)
constexpr int kAtlasRes = 4;            // atlas texels per screen pixel
constexpr int kAtlasCols = 13;
constexpr int kGlyphs = 26;             // 'a'..'z'

const char* kSurfaceVS = R"(
out vec2 vga;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vga = vec2(p.x * 320.0, (1.0 - p.y) * 200.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Height field (0x3095 + 0x2700), gradient and lighting (0xd28), light map (0xc80), palette (0x14dc).
// Heights are in the original's byte units (0..255), light-map coordinates in light-map texels relative to the
// light centre (texel (256, 128) of 0xc80).
const char* kSurfaceFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D uCloud;    // cloud texture bank 0, high resolution, values = byte / 255
uniform sampler2D uLetters;  // max of the names' blurred glyphs * level / 32, over the screen (1 = 255)
uniform vec2 uLight;         // (A, E) = (64 sin, 16 cos) of the light angle (0xd28 floors them)
uniform vec3 uBase;          // 0x14dc base colour
uniform int uMode;           // 0 colour, 1 height, 2 palette index (checks)

// The two parts of the height map: the warped texture (0x3095 with T2 row 20) and the names (0x2700).
float background(vec2 q) { return 255.0 * (20.0 / 32.0) * texBicubic(uCloud, warpTexel(q) / 256.0).r; }
float names(vec2 q) { return 255.0 * texture(uLetters, vec2(q.x / 320.0, 1.0 - q.y / 200.0)).r; }

// (background, names) and their slopes as 0xd28 takes them: half the central difference over +-1 original pixel
// (gx = (h[x-1] - h[x+1]) / 2), i.e. the derivative of each part smoothed by a 2-pixel box.
void parts(vec2 q, out vec2 h, out vec2 gb, out vec2 gn) {
    vec2 ex = vec2(1.0, 0.0), ey = vec2(0.0, 1.0);
    h = vec2(background(q), names(q));
    gb = 0.5 * vec2(background(q - ex) - background(q + ex), background(q - ey) - background(q + ey));
    gn = 0.5 * vec2(names(q - ex) - names(q + ex), names(q - ey) - names(q + ey));
}

// 0xc80 radial part: v = 300 - 32 sqrt(r) clamped to 0..255; v > 192: a = v - 128, else a = v * 0x5555 >> 16;
// then a * 2 - (a * 2 >> 3). Continuous means of the integer steps; the hot-spot edge (v 192/193) is kept.
float lightRadial(float r) {
    float v = clamp(300.0 - 32.0 * sqrt(r), 0.0, 255.0);
    return v > 192.5 ? 1.75 * (v - 128.0) + 0.375 : max(0.0, 0.58333333 * v - 0.79166667);
}
// 0xc80: min(255, radial + tex[-j][-i] / 2), the cloud texture mirrored through the light-map origin.
float lightMap(vec2 p) {
    float t = 255.0 * texture(uCloud, (vec2(0.5, 128.5) - p) / 256.0).r;
    return min(255.0, lightRadial(length(p)) + 0.5 * t - 0.25);
}

void main() {
    vec2 h, gb, gn;
    parts(vga, h, gb, gn);
    if (uMode == 1) { o = vec4(max(h.x, h.y), 0.0, 0.0, 1.0); return; }
    // The height is max(background, names) (0x2700). The original's difference straddles the crease where a name
    // rises out of the background, so its slope leaks one pixel past the letter edge; here the slope is the one of
    // the part on top at each output pixel, with the switch anti-aliased over one output pixel.
    float d = h.x - h.y;
    float w = clamp(0.5 + d / max(fwidth(d), 1e-3), 0.0, 1.0);
    vec2 g = mix(gn, gb, w);
    // 0xd28: column 112 + A + (x >> 1) + gx, row 14 + E + (y >> 1) + gy with gx = 64 + (h[x-1] - h[x+1]) / 2,
    // i.e. relative to the centre (256, 128): (x/2 + A - 80 + g.x, y/2 + E - 50 + g.y). The floors of A, E, x/2
    // and of the halved difference move the mean by -1.25 texels.
    vec2 p = vga * 0.5 + uLight + vec2(-81.25, -51.25) + g;
    float idx = lightMap(p);
    if (uMode == 2) { o = vec4(idx, 0.0, 0.0, 1.0); return; }
    o = vec4(hueRamp(uBase, idx), 1.0);
}
)";

// One blurred glyph cell of the strip, max-blended into the letter buffer.
const char* kLetterVS = R"(
uniform vec4 uRect;   // VGA rectangle x, y, w, h
uniform vec4 uCell;   // atlas u0, v0, u1, v1
out vec2 uv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    uv = mix(uCell.xy, uCell.zw, c);
    vec2 p = uRect.xy + c * uRect.zw;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
const char* kLetterFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uAtlas;
uniform float uLevel;   // level / 32
void main() { o = vec4(texBicubic(uAtlas, uv).r * uLevel); }
)";

// Per-tick state after the IRQ of a tick: the four slots of 0x2478 and the palette angle steps.
struct TickState {
    std::array<int, kLines> level{};   // 0..32
    std::array<int, kLines> name{};    // index into the name list, -1 = empty
    int palSteps = 0;                  // increments of [0x14dd] so far
};

class Greetings : public Scene {
public:
    void init(Ctx& c) override {
        readNames(*c.exe);
        osc_ = OscSet::greetings(*c.exe);
        std::string head = std::string(kWarpGLSL) + kPaletteGLSL;
        surface_.build(kSurfaceVS, head + kSurfaceFS, "greetings surface");
        letter_.build(kLetterVS, kLetterFS, "greetings letter");
        c.shared<CoreTextures>();
        font_ = &c.shared<Font>().data();
        buildAtlas(c);
        simulate(c);
    }

    void render(Ctx& c, double tick) override {
        if (tick < kFirstIrq - 1) { c.clear(); return; }  // precalc: the screen is black (level 0 until 7744)
        Target& out = *c.out;
        int n = int(std::floor(tick));
        double f = tick - n;
        const TickState& a = stateAt(n);
        const TickState& b = stateAt(n + 1);

        auto letters = c.temp(false, 1.0f, GL_R32F);
        letters->bind();
        c.clear();
        drawNames(a, b, f);

        out.bind();
        drawSurface(c, letters->tex, tick, a, b, f, 0);
        if (const char* dir = std::getenv("PAPER_GREETINGS_DUMP")) dumpChecks(c, letters->tex, tick, a, b, f, dir);
        out.bind();
    }

private:
    std::vector<std::string> names_;
    OscSet osc_;
    Shader surface_, letter_;
    Target atlas_;
    std::vector<TickState> states_;  // ticks kFirstIrq - 1 .. kLastTick + 1
    const FontData* font_ = nullptr;

    void readNames(const PaperExe& exe) {
        uint32_t p = kNameList;
        for (;;) {
            std::string s = exe.cstr(p);
            if (s.empty()) break;
            names_.push_back(s);
            p += uint32_t(s.size()) + 1;
        }
    }

    // ---- 0x24df glyph strip, at kAtlasRes texels per screen pixel: glyph at column 2 of its cell, font rows
    // doubled (sy = 2), then the 4 cross-blur passes as one Gaussian (variance 2 px^2 per axis).
    void buildAtlas(Ctx& c) {
        Font& font = c.shared<Font>();
        int rows = (kGlyphs + kAtlasCols - 1) / kAtlasCols;
        float w = float(kAtlasCols * (kCellW + 2 * kPad)), h = float(rows * (kCellH + 2 * kPad));
        GlState saved;
        atlas_.create(int(w) * kAtlasRes, int(h) * kAtlasRes, false, GL_R32F);
        atlas_.bind();
        c.clear();
        std::vector<GlyphPos> gs;
        for (int g = 0; g < kGlyphs; g++) {
            float cx, cy;
            cellOrigin(g, cx, cy);
            GlyphPos p;
            p.glyph = g;
            p.x = cx + kPad + kInkX;
            p.y = cy + kPad;
            p.sx = 1;
            p.sy = 2;
            gs.push_back(p);
        }
        font.draw(gs, Font::pixelProj(w, h), GlyphOp::Max, 1.0f);
        double sigma = passesSigma(4) * kAtlasRes;
        blurSigma(c, atlas_, sigma, sigma, Edge::Zero, GL_R32F);
    }

    static void cellOrigin(int g, float& x, float& y) {
        x = float((g % kAtlasCols) * (kCellW + 2 * kPad));
        y = float((g / kAtlasCols) * (kCellH + 2 * kPad));
    }

    // ---- 0x26aa per tick: names 0x2478 and the palette steps, for every tick of the part (cheap: done once)
    void simulate(Ctx& c) {
        TickState s;
        s.level.fill(0);
        s.name.fill(-1);
        int cur = 0, next = 0;
        bool loading = false;
        states_.push_back(s);  // kFirstIrq - 1: before the first callback
        for (int t = kFirstIrq; t <= kLastTick + 1; t++) {
            if (c.music && c.music->rowCountdown(t) > 3) s.palSteps++;
            int& lv = s.level[cur];
            if (loading) {
                if (++lv == kFullLevel) { loading = false; cur = (cur + 1) & 3; }
            } else if (lv == 0) {  // empty: take the next name (the list wraps at its empty string)
                loading = !names_.empty();
                s.name[cur] = loading ? next : -1;
                if (loading) next = (next + 1) % int(names_.size());
            } else {
                lv--;
            }
            states_.push_back(s);
        }
    }

    const TickState& stateAt(int t) const {
        int i = t - (kFirstIrq - 1);
        i = i < 0 ? 0 : (i >= int(states_.size()) ? int(states_.size()) - 1 : i);
        return states_[size_t(i)];
    }

    static double palAngle(const TickState& s) { return kPalAngle0 + (s.palSteps > 0 ? s.palSteps - 1 : 0); }
    static double lightAngle(int t) { return kLightAngle0 + std::max(0, t - (kFirstIrq - 1)); }

    // ---- 0x26e7 / 0x2700: each letter's blurred cell, max-blended at level / 32 into the bound target
    void drawNames(const TickState& a, const TickState& b, double f) {
        letter_.use();
        atlas_.bindTex(0);
        letter_.set("uAtlas", 0);
        setBlend(Blend::Max);
        float aw = float(atlas_.w) / kAtlasRes, ah = float(atlas_.h) / kAtlasRes;
        for (int s = 0; s < kLines; s++) {
            double level = a.level[s] + (b.level[s] - a.level[s]) * f;
            int name = a.name[s] >= 0 ? a.name[s] : b.name[s];
            if (level <= 0 || name < 0) continue;
            letter_.set("uLevel", float(level / kFullLevel));
            int x = kTextX;
            for (char ch : names_[size_t(name)]) {
                if (ch == ' ') { x += kSpace; continue; }
                int g = FontData::glyphIndex(ch);
                if (g < 0 || g >= kGlyphs) continue;
                float cx, cy;
                cellOrigin(g, cx, cy);
                float cw = kCellW + 2 * kPad, chh = kCellH + 2 * kPad;
                letter_.set("uRect", float(x - kPad), float(kLineY * s - kPad), cw, chh);
                letter_.set("uCell", cx / aw, 1.0f - cy / ah, (cx + cw) / aw, 1.0f - (cy + chh) / ah);
                drawQuad();
                x += font_->glyph(g).width + kLetterGap;
            }
        }
        setBlend(Blend::None);
    }

    // Into the bound target: the lit surface (mode 0), or the height / palette index for the checks (modes 1, 2).
    void drawSurface(Ctx& c, GLuint letters, double tick, const TickState& a, const TickState& b, double f,
                     int mode) {
        surface_.use();
        setWarpUniforms(surface_, warpField(osc_, tick), WarpShift::greetings());
        c.shared<CoreTextures>().cloud.bind(0);
        surface_.set("uCloud", 0);
        gl::ActiveTexture(GL_TEXTURE0 + 1);
        gl::BindTexture(GL_TEXTURE_2D, letters);
        surface_.set("uLetters", 1);
        int n = int(std::floor(tick));
        double th = 2.0 * 3.14159265358979323846 * (lightAngle(n) + f) / 1024.0;
        surface_.set("uLight", float(64.0 * std::sin(th)), float(16.0 * std::cos(th)));
        double pa = palAngle(a) + (palAngle(b) - palAngle(a)) * f;
        surface_.set("uBase", hueBase(pa, kPalShift));
        surface_.set("uMode", mode);
        drawFullscreen();
        gl::ActiveTexture(GL_TEXTURE0);
    }

    // Check hook: PAPER_GREETINGS_DUMP=dir writes the height field and the palette index at the original's 320x200
    // pixel centres (float32, bottom-up rows) as dir/h_<tick>.f32 and dir/i_<tick>.f32, and the per-tick state
    // (tick, 4 x (level, name), shown palette angle, light angle) as dir/states.txt.
    void dumpChecks(Ctx& c, GLuint letters, double tick, const TickState& a, const TickState& b, double f,
                    const char* dir) {
        auto t = c.tempSized(320, 200, false, GL_R32F);
        std::vector<float> px(320 * 200 * 4);
        for (int mode = 1; mode <= 2; mode++) {
            t->bind();
            drawSurface(c, letters, tick, a, b, f, mode);
            gl::ReadPixels(0, 0, 320, 200, GL_RGBA, GL_FLOAT, px.data());
            char name[512];
            std::snprintf(name, sizeof(name), "%s/%c_%d.f32", dir, mode == 1 ? 'h' : 'i', int(std::lround(tick)));
            if (FILE* fp = std::fopen(name, "wb")) {
                for (size_t i = 0; i < 64000; i++) std::fwrite(&px[i * 4], 4, 1, fp);
                std::fclose(fp);
            }
        }
        char name[512];
        std::snprintf(name, sizeof(name), "%s/states.txt", dir);
        if (FILE* fp = std::fopen(name, "w")) {
            for (int t = kFirstIrq - 1; t <= kLastTick; t++) {
                const TickState& s = stateAt(t);
                std::fprintf(fp, "%d", t);
                for (int k = 0; k < kLines; k++) std::fprintf(fp, " %d %d", s.level[k], s.name[k]);
                std::fprintf(fp, " %.0f %.0f\n", palAngle(s), lightAngle(t));
            }
            std::fclose(fp);
        }
    }
};

REGISTER_SCENE(Greetings, "greetings");

}  // namespace
