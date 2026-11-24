// The intro's text (text.h): the shared printer 0x4cacc's pages and layout, der piipo's font drawn from the "font"
// picture (the original sheet 0x3cec2 or its redraw) in the part's colours.
#include "okean/text.h"

#include <algorithm>
#include <cmath>

#include "exe.h"

namespace okean {

namespace {
constexpr uint32_t kWidths = 0x4c8c2;   // 26 bytes, a..z
constexpr uint32_t kPages = 0x4c8e8;    // the shared printer's first page
constexpr int kPageCount = 99;
constexpr int kCell = 38;               // glyph cell height (rows)
constexpr int kSpace = 4;

const char* kGlyphVS = R"(
uniform vec4 rect;   // x, y, w, h in VGA coordinates
uniform vec4 uvr;
out vec2 uv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    uv = mix(uvr.xy, uvr.zw, c);
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
// The font picture holds premultiplied white (body) and red (outline) on transparent: body = green, outline = red
// minus green; colour = body x entry 216 + outline x entry 7 (an original edge pixel, grey 33/63, is 33/63 of the
// body, which is entry 254 in every part's palette).
const char* kGlyphFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform int sharp;
uniform vec3 body;
uniform vec3 outline;
void main() {
    vec4 t = sharp != 0 ? texSharp(tex, uv) : texture(tex, uv);
    float b = t.g, l = max(t.r - t.g, 0.0);
    o = vec4(b * body + l * outline, t.a);
}
)";
}  // namespace

Printer::Printer(Ctx& c) : exe_(c.exe) {
    int x = 0;
    for (int k = 0; k < 26; k++) {
        w_[size_t(k)] = exe_->u8(kWidths + uint32_t(k));
        x_[size_t(k)] = x;
        x += w_[size_t(k)];
    }
    uint32_t a = kPages;
    for (int i = 0; i < kPageCount; i++) {
        pages_.push_back(a);
        while (exe_->u8(a) != '@') a++;
        a++;
    }
    shader_.build(kGlyphVS, kGlyphFS, "okean glyph");
}

std::vector<std::string> Printer::page(uint32_t addr) const {
    std::vector<std::string> lines;
    for (uint32_t a = addr; a < addr + 4096; a++) {
        uint8_t ch = exe_->u8(a);
        if (ch == '@' || ch == 0) break;
        if (ch == '\r') lines.emplace_back();
        else if (!lines.empty()) lines.back() += char(ch);
    }
    return lines;
}

int Printer::width(const std::string& line) const {
    int w = 0;
    for (char ch : line) {
        if (ch == ' ') w += kSpace;
        else if (ch >= 'a' && ch <= 'z') w += w_[size_t(ch - 'a')];
    }
    return w;
}

std::vector<std::string> Printer::caption(Ctx& c, double r) const {
    // the draw of record F + n prints the page the previous draw's call selected: one page per 16 rows (rows 0, 16,
    // 32, and 48 in order 0), page 1 + row / 16 in order 0 and 5 + 3 (order - 1) + row / 16 from order 1 on
    int n = int(std::floor(r - c.start));
    int pos = c.positionAtDraw(std::max(n - 1, 0));
    if (pos < 0) return {};
    int order = pos / 64, row = pos % 64;
    int p = order == 0 ? 1 + row / 16 : 5 + 3 * (order - 1) + row / 16;
    if (p < 0 || p >= kPageCount) return {};
    return page(pages_[size_t(p)]);
}

void Printer::drawCaption(Ctx& c, double r, const std::array<Vec3, 256>& pal) {
    for (const std::string& line : caption(c, r))
        if (!line.empty()) drawLine(c, line, 162.0f, pal);
}

void Printer::drawLine(Ctx& c, const std::string& line, float y, const std::array<Vec3, 256>& pal) {
    const Picture& font = c.picture("font");
    if (!font.tex.id) return;
    setBlend(Blend::Premul);
    shader_.use();
    font.tex.bind(0);
    shader_.set("tex", 0);
    shader_.set("sharp", font.redraw ? 0 : 1);
    shader_.set("body", pal[216]);
    shader_.set("outline", pal[7]);
    float x = float(160 - (width(line) >> 1));
    for (char ch : line) {
        if (ch == ' ') { x += kSpace; continue; }
        if (ch < 'a' || ch > 'z') continue;
        int k = ch - 'a', w = w_[size_t(k)];
        shader_.set("rect", x, y, float(w), float(kCell));
        shader_.set("uvr", float(x_[size_t(k)]) / 285.0f, 0.0f, float(x_[size_t(k)] + w) / 285.0f, 1.0f);
        drawQuad();
        x += float(w);
    }
    setBlend(Blend::None);
}

}  // namespace okean
