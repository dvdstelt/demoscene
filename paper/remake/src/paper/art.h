// The redrawn pictures (paper/remake/art, see its README): high-resolution redraws of the original's hand-drawn
// bitmaps, shown by default; --original-art (ctx.artDir empty) shows the original's pixels. Paper has three, all on
// the logo page (scenes/logo.cpp, which the first peel (scene3d.cpp) and the teal part draw through
// ctx.renderPart("logo")): the "Paper" pencil sketch at code32 0x1bf03 (art/sketch.png), the text "present the" /
// "intro" that 0x5623 draws with the font (art/text.png, a coverage mask) and the finished ink of the ink image
// 0x196c3 (art/ink.png, colour with alpha).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Ctx;

namespace paper {

// An 8-bit picture with 1 (grey) or 4 (RGBA, straight alpha) channels, row 0 at the top.
struct ArtImage {
    int w = 0, h = 0, channels = 1;
    std::vector<uint8_t> px;  // w * h * channels
};

// Reads art/<name>.png (converted to grey) when the redraws are on and the file exists. False otherwise, and when
// the file cannot be read (logged): the caller then uses the original's pixels.
bool loadArtGrey(const Ctx& c, const std::string& name, ArtImage* out);
// The same, converted to RGBA with straight alpha.
bool loadArtRgba(const Ctx& c, const std::string& name, ArtImage* out);

}  // namespace paper
