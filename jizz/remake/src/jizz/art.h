// The redrawn pictures (jizz/remake/art, see its README): high-resolution redraws of the intro's two hand-drawn
// pictures, the 4-bit RLE bitmaps of texture script 14 (0x12a7d, decoder 0x13df4), shown by default;
// --original-art (ctx.artDir empty) shows the original's pixels. Both are coverage masks over the 256x200 picture
// (white = the picture's solid level), which the parts multiply with the script's clouds themselves:
//   - art/logo.png: the "jizz" logo at 0x27d68 (logo2 = clouds x this picture; scenes/logo.cpp)
//   - art/lettering.png: "~JIZZ~ TBL'97" at 0x275b8 (logo1 = clouds x this picture; scenes/end.cpp). The TBL part's
//     letter mask (testshit) is a generated texture built from the same picture and keeps the original.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "render.h"

struct Ctx;

namespace jizz {

// An 8-bit grey picture, row 0 at the top.
struct ArtImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w * h
};

// Reads art/<name>.png (converted to grey) when the redraws are on and the file exists. False otherwise, and when
// the file cannot be read (logged): the caller then uses the original's pixels.
bool loadArtGrey(const Ctx& c, const std::string& name, ArtImage* out);

// The mask as a GL texture: R8, linear with trilinear mipmaps, wrapping (the pictures repeat every 256 columns on
// the 320-wide screen, tile320; the rows cover 0..200).
void uploadArt(const ArtImage& im, Texture& tex);

}  // namespace jizz
