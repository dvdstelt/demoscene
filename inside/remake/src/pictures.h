// The pictures drawn as they are (inside/re/notes-*.md list them per part): 8-bit pixels in INSIDE.DAT shown
// through a palette from INSIDE.DAT. The remake shows faithful high-resolution redraws (inside/remake/art/<name>.png,
// the default) where one exists and the original pixels otherwise or with --original-art. A part never needs to know
// which: it asks the context for the picture by name and draws it where the original does.
//
// A picture texture covers exactly the original's w x h pixels (a redraw the same frame at its own resolution) and
// holds premultiplied alpha: original pixels are the palette's colours (6-bit v -> v / 63), opaque, except the
// transparent index if the picture has one.
//
// Pictures that an effect reads through palette indices or shade tables are not pictures in this sense: the parts
// read those files themselves (indexed textures, inside/core.h).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render.h"

class InsideDat;
class InsideExe;

struct PictureInfo {
    const char* name;      // the art file name (art/<name>.png) and the name parts use
    uint32_t file;         // address in INSIDE.EXE of the name of the INSIDE.DAT file with the 8-bit pixels
    uint32_t palette;      // ... of its palette (768 bytes, 6 bit)
    int w, h;              // size in pixels
    int offset = 0;        // first pixel in the file (after a header)
    int transparent = -1;  // the index that is not drawn (-1: none)
    bool photo = false;    // a digitised photo: never redrawn
};
const std::vector<PictureInfo>& pictureList();
const PictureInfo* findPicture(const std::string& name);

// The original's picture as RGBA8 with premultiplied alpha.
Image originalPicture(const InsideExe& exe, const InsideDat& dat, const PictureInfo& p);
// A 768-byte 6-bit palette from INSIDE.DAT as 256 RGB floats 0..1 (v / 63; values above 63 are clamped, as 0x400364).
std::array<Vec3, 256> datPalette(const InsideDat& dat, const std::string& file);

struct Picture {
    std::string name;
    Texture tex;             // RGBA8, premultiplied alpha, linear (redraws: mipmapped); clamp to edge
    bool redraw = false;     // true: a redraw (any resolution); false: the original's pixels
    int srcW = 0, srcH = 0;  // the original's size in pixels (the texture covers exactly this)
};

// Loads and caches the pictures (owned by Demo; parts use ctx.picture()).
class PictureStore {
public:
    void init(const InsideExe* exe, const InsideDat* dat, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }
    std::string redrawPath(const std::string& name) const;  // art/<name>.png if it exists, else ""
    // The picture as a texture: the redraw when art is on and one exists, else the original's pixels. Cached; the
    // reference stays valid until release(). Needs the GL context. Unknown names give an empty picture (logged).
    const Picture& get(const std::string& name);
    void release();

private:
    const InsideExe* exe_ = nullptr;
    const InsideDat* dat_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<std::string, std::unique_ptr<Picture>> cache_;
};
