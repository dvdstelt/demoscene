// The pictures drawn as they are (reve/re/notes-*.md list them per part): RAW files of REVE.EXE's archive (8-bit pixels
// with their own palette, NOTES.md "RAW pictures") shown through the palette the demo gives them. The remake shows
// faithful high-resolution redraws (reve/remake/art/<name>.png, the default) where one exists and the original pixels
// otherwise or with --original-art. A part never needs to know which: it asks the context for the picture by name and
// draws it where the original does.
//
// A picture texture covers exactly the original's w x h pixels (a redraw the same frame at its own resolution) and
// holds premultiplied alpha: original pixels are the palette's colours (6-bit v -> v / 63), opaque, except the
// transparent index if the picture has one.
//
// Pictures that an effect reads through palette indices or tables are not pictures in this sense: the parts read
// those files themselves (rawImage below).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render.h"

class ReveExe;

// A RAW file of the archive ("mhwanh": 32-byte header with big-endian width, height and palette size, 768 bytes of
// 8-bit palette, the pixels). The palette as the demo's loader 0x25a1fb gives it to the DAC: 6-bit values v >> 2.
struct RawImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px;            // w * h indices, row by row
    std::array<uint8_t, 768> pal6{};    // 6-bit palette (v >> 2 of the file's 8-bit values)
    Vec3 color(int i) const { return Vec3(pal6[i * 3] / 63.0f, pal6[i * 3 + 1] / 63.0f, pal6[i * 3 + 2] / 63.0f); }
};
// The RAW file `file` ("GFX\\WIZ.RAW"). Throws std::runtime_error if it is missing or malformed.
RawImage rawImage(const ReveExe& exe, const std::string& file);

struct PictureInfo {
    const char* name;      // the art file name (art/<name>.png) and the name parts use
    uint32_t file;         // address in the image of the name of the RAW file with the 8-bit pixels
    uint32_t palette;      // ... of the RAW file whose palette it is shown with (0: its own)
    int x = 0, y = 0;      // the part of the RAW picture (in its pixels) ...
    int w = -1, h = -1;    // ... and its size (-1: all of it)
    int transparent = -1;  // the index that is not drawn (-1: none)
};
const std::vector<PictureInfo>& pictureList();
const PictureInfo* findPicture(const std::string& name);

// The textures and maps that the parts read by palette index and that have an upscale in art/tex_<name>.jpg
// (art/README.md): exactly 4 x the original's size. The name is the RAW file's base name in lower case ("texture" for
// GFX\TEXTURE.RAW; detailName gives it).
const std::vector<std::string>& detailList();
std::string detailName(const std::string& rawFile);

// The original's picture as RGBA8 with premultiplied alpha.
Image originalPicture(const ReveExe& exe, const PictureInfo& p);

struct Picture {
    std::string name;
    Texture tex;             // RGBA8, premultiplied alpha, linear (redraws: mipmapped); clamp to edge
    bool redraw = false;     // true: a redraw (any resolution); false: the original's pixels
    int srcW = 0, srcH = 0;  // the original's size in pixels (the texture covers exactly this)
};

// Loads and caches the pictures (owned by Demo; parts use ctx.picture()).
class PictureStore {
public:
    void init(const ReveExe* exe, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }
    std::string redrawPath(const std::string& name) const;  // art/<name>.png if it exists, else ""
    // The picture as a texture: the redraw when art is on and one exists, else the original's pixels. Cached; the
    // reference stays valid until release(). Needs the GL context. Unknown names give an empty picture (logged).
    const Picture& get(const std::string& name);
    // The upscale of a texture of detailList() (RGBA8, not premultiplied), decoded on a worker thread at init:
    // nullptr when art is off or there is none. Cached; valid until release().
    const Image* detail(const std::string& name);
    // The redraw art/<name>.png (or .jpg) of a texture that a part maps itself (the planes' DADA, TOM, BOZ), decoded
    // as RGBA8 (premultiplied; the textures are opaque): w = 0 when art is off or there is none. Not cached.
    Image redrawImage(const std::string& name) const;
    void release();

private:
    const ReveExe* exe_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<std::string, std::unique_ptr<Picture>> cache_;
    std::map<std::string, std::unique_ptr<Image>> details_;
};
