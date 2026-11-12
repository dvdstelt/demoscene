// Palettes and pictures: the original's 6-bit VGA palettes in display space, and the hand-drawn pictures of the
// overlay as true-colour textures, either as faithful high-resolution redraws (stars/remake/art/fileNN.png, the
// default) or as the original's pixels through their palette (--original-art). See stars/re/notes-core.md
// section 14 for every picture-like file and how the demo uses it, and stars/remake/art/README.md for the redraws.
//
// A picture texture covers the original's whole pixel grid of the file (320x200 for the screens, the stacked
// sheet for the sprite files, 256x256 for the maps): a redraw has the same framing at its own resolution, so a
// part draws or samples both the same way. Textures hold premultiplied alpha (opaque pictures are unaffected).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "render.h"

class StarsExe;

// 256 colours in display space (0..1, no sRGB conversion).
using Palette = std::array<Vec3, 256>;

// 6-bit VGA palette bytes (r, g, b, 0..63 each, as the DAC takes them) -> display space v / 63, for the colours
// first .. first + rgb.size() / 3 - 1; the other colours are those of `base` (black by default).
Palette vgaPalette(std::span<const uint8_t> rgb, int first = 0, const Palette& base = Palette{});

// The palette fade of 0x4a720 / 0x17e96 (DAC = min(63, (p * L) >> 8), L = 0..256) in display space: colour *
// L / 256, clamped to 1. A colour multiply, so it applies to redraws as well.
inline Vec3 fadeColour(Vec3 c, double level) {
    float f = float(level / 256.0);
    auto one = [&](float v) { return v * f > 1.0f ? 1.0f : v * f; };
    return {one(c.x), one(c.y), one(c.z)};
}

// How the original's pixels of a file are coloured. Redraws are true colour and ignore all of this.
struct PictureOptions {
    // nullptr: the file's own palette (0xfd00 pictures: the 768 bytes before the pixels; the sprite files 5 / 6 and
    // the flower texture 7: their colours at DAC 1..15 / 0x60..0x6f / 0x10..0x4f; the face map 35: its first 768
    // bytes); a grey ramp v / 255 for files without one (screens 19, 25, 27, 47, 58, the maps).
    const Palette* palette = nullptr;
    int key = -2;          // this index is transparent; -1 none, -2 the file's default (0 for the sprites 5 and 6)
    int opaqueBelow = -2;  // indices >= this are transparent; -2 the file's default (0x50 for file 32), 256 none
    int indexAdd = 0;      // added to every index (mod 256) before the palette: file 47 is shown at v + 0xe6
};

struct Picture {
    int file = 0;
    Texture tex;             // RGBA8, premultiplied alpha, linear (redraws: mipmapped); clamp to edge
    bool redraw = false;     // true: a redraw (any resolution, square pixels); false: the original's pixels
    int srcW = 0, srcH = 0;  // the original's pixel grid that the texture covers (e.g. 320 x 200; file 5: 144 x 144)
};

// The original's pixel grid of overlay file n as 0x4a9f3 loads it: where the pixels start, width and height.
// False for files that are not pictures or maps (objects, tables, palettes, the module).
struct PictureLayout { uint32_t offset = 0; int w = 0, h = 0; };
bool pictureLayout(int n, PictureLayout* layout);

// Loads and caches pictures (owned by Demo; parts use ctx.picture()).
class PictureStore {
public:
    void init(const StarsExe* exe, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }
    // The redraw's path if art/fileNN.png (or .jpg) exists, else "".
    std::string redrawPath(int n) const;
    // Overlay file n as a texture: the redraw when art is on and one exists, else the original's pixels coloured by
    // `o`. Cached per file and options; the reference stays valid until release(). Needs the GL context.
    const Picture& get(int n, const PictureOptions& o = {});
    void release();

private:
    const StarsExe* exe_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<uint64_t, std::unique_ptr<Picture>> cache_;
};
