// The intro's font: 29 one-bit glyphs ('a'..'z', '{' = "1", '|' = "9", '}' = "6") decoded by init 0xe2e from the
// RLE data at code32 0x18ed7 into 32-row x 60-byte cells (notes-init.md §5.9), the text routines that place them
// (0x11b0 string loop, 0xe8a glyph, 0xedc glyph magnified 3x7; notes-init.md §8.12), and a high-resolution version
// of the glyphs for the GPU.
//
// Placement (exact, in pixels of the byte buffer the original draws into; x right, y down, (x, y) = top-left of the
// glyph's 32-row box; ink always starts at column 0 of the box):
//   0xe8a  glyph c at the pen, saturating add of 0/255 over w x 32; pen += w + 1. Codes below 'a' advance 10 px, and
//          code 1 additionally adds 0x303e to the linear address (a "newline" whose meaning depends on the stride).
//   0xedc  glyph magnified 3x horizontally, 7x vertically, font rows 0..24 only (stride 320); pen += 3w + 2.
//
// High resolution: each glyph's bitmap is traced into outline loops along its pixel edges; the loops are pulled taut
// inside a half-pixel band (staircases become the lines and curves they sample, corners between straight runs of
// 3+ pixels stay sharp) and lightly rounded; the outline is stored as a signed distance field at 10 texels per font
// pixel (exact segment distances near the outline). Font::draw renders the glyphs anti-aliased at any scale into
// the bound target. The outline stays within half a pixel of the original bitmap everywhere.
//
// Intensity buffers: the original adds glyph bytes into a byte buffer with saturation at 255 and maps the bytes
// through a palette afterwards. Do the same with a UNORM target, e.g. ctx.temp(false, 1, GL_R16): with
// GlyphOp::Add the blender clamps at 1.0 exactly like the saturating byte add (and GlyphOp::Sub at 0). value = the
// byte added at full ink / 255 (1 for the plain 0xe8a, 0.5 for the title's half-strength copies, ...). Float
// targets do not clamp. Blur the buffer afterwards with blur.h to get the original's soft letters.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "render.h"

class PaperExe;
struct Ctx;

namespace paper {

struct FontGlyph {
    int width = 0;                       // [0] of the cell: ink width 8..49
    std::array<uint8_t, 32 * 60> bits{}; // 1 = ink, row-major, 60 per row (as the cell at +1)
    bool at(int x, int y) const { return x >= 0 && x < 60 && y >= 0 && y < 32 && bits[y * 60 + x]; }
};

class FontData {
public:
    static constexpr int kGlyphs = 29;
    explicit FontData(const PaperExe& exe);
    static int glyphIndex(char c);       // 'a'..'}' -> 0..28, anything else -1
    const FontGlyph& glyph(int i) const { return glyphs_[i]; }
    int width(char c) const { int g = glyphIndex(c); return g < 0 ? 0 : glyphs_[g].width; }

    // The original's byte-buffer drawing (exact; for buffers simulated at 320x200 such as the end part's "1996").
    // buf = byte buffer, stride = row pitch; returns the advanced pen x.
    int drawBytes(uint8_t* buf, int bufW, int bufH, int stride, int x, int y, char c) const;     // 0xe8a
    int drawBytesBig(uint8_t* buf, int bufW, int bufH, int stride, int x, int y, char c) const;  // 0xedc

private:
    std::array<FontGlyph, kGlyphs> glyphs_;
};

// A glyph placed in a buffer: top-left of its box at (x, y) buffer pixels, scaled by (sx, sy) (0xedc: 3, 7), only font
// rows [0, rows) drawn (0xedc: 25).
struct GlyphPos {
    int glyph = 0;
    float x = 0, y = 0;
    float sx = 1, sy = 1;
    float rows = 32;
};

// 0x11b0 / 0xe8a: a string drawn from pen (x, y) in a buffer with row pitch `stride` (bytes).
std::vector<GlyphPos> layoutText(const FontData& f, const std::string& s, int x, int y, int stride = 320);
// 0xedc for each character of s (no special codes), from pen (x, y).
std::vector<GlyphPos> layoutBig(const FontData& f, const std::string& s, int x, int y);
// Total advance of 0x11b0 for a string without code 1.
int textAdvance(const FontData& f, const std::string& s);

enum class GlyphOp {
    Add,  // dst + value*coverage (saturates in UNORM targets)
    Sub,  // dst - value*coverage (clamps at 0 in UNORM targets)
    Max,  // max(dst, value*coverage)
    Over  // alpha blend of `color` with coverage (for drawing straight into a colour target)
};

class Font {
public:
    explicit Font(Ctx& c);  // use ctx.shared<Font>()
    const FontData& data() const { return data_; }

    // Draw into the bound target. proj maps buffer pixels to clip space (e.g. pixelProj(320, 200) for a target that
    // covers a 320x200 buffer, or ctx.vgaProj() for the screen). Each channel gets value*coverage (Over: color).
    void draw(const std::vector<GlyphPos>& glyphs, const Mat4& proj, GlyphOp op, float value = 1.0f,
              Vec3 color = Vec3(1, 1, 1));
    void draw(const GlyphPos& g, const Mat4& proj, GlyphOp op, float value = 1.0f, Vec3 color = Vec3(1, 1, 1));
    static Mat4 pixelProj(float w, float h);  // buffer pixels (x right, y down) -> clip space

    GLuint atlas() const { return atlas_.id; }

private:
    struct Cell { float u0, v0, u1, v1; };  // atlas rectangle of the glyph's padded box
    FontData data_;
    Texture atlas_;  // R16F signed distance in font pixels (positive inside)
    std::array<Cell, FontData::kGlyphs> cells_{};
    Shader shader_;
};

}  // namespace paper
