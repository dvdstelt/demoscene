// Palettes and pictures: the original's 6-bit VGA palettes in display space, and the pictures inside the part
// images as true-colour textures, either as faithful high-resolution redraws (dope/remake/art/<name>.png, the
// default) or as the original's pixels (--original-art). The pictures are ColoRIX "RIX3" files embedded in the
// part images ("RIX3", u16 width, u16 height, AF 00, 768-byte 6-bit palette at +0x0A, pixels at +0x30A, rows top
// to bottom), raw pixel blocks, and one IFF PBM (p4's chequerboard). Every picture the notes list is in the catalog
// (pictures.cpp) under the name of its dump (dope/re/dump/pictures/<name>.png), which is also its redraw's file name.
//
// A picture texture covers the picture's "frame": the original pixels a redraw replaces (usually the whole picture;
// p1-logo: the 1024 x 195 logo rows 1..195; p6-envmap: the drawn left half, 128 x 128). A redraw has the same
// framing at its own resolution, so a part draws or samples both the same way. Textures hold premultiplied alpha.
//
// Kinds (how the indices become the texture, the same for the original pixels and the redraw):
//   Screen  colours through a palette (the picture's own unless the catalog or PictureOptions say otherwise);
//   Mask    p2's credits: the index - 0x80 (0..62) is the text coverage, as grey (index - 0x80) / 63;
//   Shade   p1's logo and the grey maps: the index is a level, as grey v / (levels - 1); the part colours it.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "render.h"

class DopeExe;
class DopeImage;

// 256 colours in display space (0..1, no sRGB conversion).
using Palette = std::array<Vec3, 256>;

// 6-bit VGA palette bytes (r, g, b, 0..63 each, as the DAC takes them) -> display space v / 63, for the colours
// first .. first + rgb.size() / 3 - 1; the other colours are those of `base` (black by default).
Palette vgaPalette(std::span<const uint8_t> rgb, int first = 0, const Palette& base = Palette{});
// The 256-colour palette of the RIX3 header at seg:off of an image (6-bit, at +0x0A).
Palette rixPalette(const DopeImage& img, uint16_t seg, uint16_t off);

// The DAC scaling of the parts' fades, (v * L) >> 8 with L = 0..256, in display space: colour * L / 256, clamped
// to 1. A colour multiply, so it applies to redraws as well.
inline Vec3 fadeColour(Vec3 c, double level) {
    float f = float(level / 256.0);
    auto one = [&](float v) { return v * f > 1.0f ? 1.0f : v * f; };
    return {one(c.x), one(c.y), one(c.z)};
}

enum class PicFormat { Rix3, Raw, Pbm };
enum class PicKind { Screen, Mask, Shade };

// Where a palette for the original pixels comes from: `count` 6-bit colours at part:seg:off go to DAC first.. .
struct PalSource {
    int part = 0;               // 0: none (the picture's own)
    uint16_t seg = 0, off = 0;
    int first = 0, count = 0;
};

// One picture of a part image. Parts normally name catalog entries (c.picture("p3-nebula")); a part may also pass
// its own PictureDef for anything else in its image (raw blocks, a map): c.picture(PictureDef{...}).
struct PictureDef {
    const char* name;            // catalog name = the redraw's file name art/<name>.png (or .jpg)
    int part;                    // 1..7: the image that holds it
    uint16_t seg, off;           // the RIX3 header, the raw pixels or the IFF FORM (SEG:OFF as in the notes)
    PicFormat fmt = PicFormat::Rix3;
    int w = 0, h = 0;            // raw pictures only (RIX3 and PBM carry their size)
    PicKind kind = PicKind::Screen;
    PalSource pal{};             // Screen: the palette for the original pixels (default: the picture's own; raw: grey)
    int fx = 0, fy = 0, fw = 0, fh = 0;  // the frame in the picture's pixels (fw = 0: the whole picture)
    const char* alias = nullptr; // the same pixels under another name (the other part's dump), also tried for a redraw
    int levels = 64;             // Shade: grey = v / (levels - 1)
};

// The catalog (pictures.cpp): every picture of dope/re/notes-*.md, hand-drawn ones first, then generated maps.
const std::vector<PictureDef>& pictureCatalog();
const PictureDef* findPicture(const std::string& name);  // by name or alias; nullptr if unknown

// A picture's indices as the original has them (the whole picture, not the frame), and its own palette (RIX3: its
// 768 bytes; PBM: CMAP >> 2; raw: the catalog's palette source or a grey ramp v / 255). For effects that need the
// original's indices (masks, levels, maps): readIndexed(*c.exe, *findPicture("p6-envmap")).
struct Indexed {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w * h, rows top to bottom
    Palette pal{};
    uint8_t at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
};
Indexed readIndexed(const DopeExe& exe, const PictureDef& def);

// How the original's pixels are coloured (Screen pictures only). Redraws are true colour and ignore all of this.
struct PictureOptions {
    const Palette* palette = nullptr;  // nullptr: the catalog's / the picture's own palette
    int key = -1;                      // this index is transparent (-1: none)
    int indexAdd = 0;                  // added to every index (mod 256) before the palette
};

struct Picture {
    std::string name;
    Texture tex;              // RGBA8, premultiplied alpha, linear (redraws: mipmapped); clamp to edge
    bool redraw = false;      // true: a redraw (any resolution, square pixels); false: the original's pixels
    PicKind kind = PicKind::Screen;
    int srcW = 0, srcH = 0;   // the frame in original pixels that the texture covers (e.g. 320 x 200, 70 x 44)
};

// Loads and caches pictures (owned by Demo; parts use ctx.picture()).
class PictureStore {
public:
    void init(const DopeExe* exe, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }
    // The redraw's path if art/<name>.png (or .jpg, or the alias's) exists, else "".
    std::string redrawPath(const PictureDef& def) const;
    // The picture as a texture: the redraw when art is on and one exists, else the original's pixels coloured by
    // `o`. Cached per name and options; the reference stays valid until release(). Needs the GL context.
    const Picture& get(const std::string& name, const PictureOptions& o = {});
    const Picture& get(const PictureDef& def, const PictureOptions& o = {});
    void release();

private:
    const DopeExe* exe_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<uint64_t, std::unique_ptr<Picture>> cache_;
};
