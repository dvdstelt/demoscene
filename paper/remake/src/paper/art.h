// The redrawn pictures (paper/remake/art, see its README): high-resolution redraws of the original's hand-drawn
// bitmaps, shown by default; --original-art (ctx.artDir empty) shows the original's pixels. Paper has one: the
// "Paper" pencil sketch at code32 0x1bf03 (art/sketch.png), used by the logo part (scenes/logo.cpp), which the
// first peel (scene3d.cpp) and the teal part draw through ctx.renderPart("logo").
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Ctx;

namespace paper {

// An 8-bit greyscale picture, row 0 at the top.
struct GreyImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w * h
};

// Reads art/<name>.png (converted to grey) when the redraws are on and the file exists. False otherwise, and when
// the file cannot be read (logged): the caller then uses the original's pixels.
bool loadArtGrey(const Ctx& c, const std::string& name, GreyImage* out);

}  // namespace paper
