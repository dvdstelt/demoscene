// The ten stored pictures (clone/re/notes-gfx.md sections 5..7): four captions, four hand-drawn glyphs, the title
// and the credits, Aap's 8-bit pixels at 0x41a950..0x420489 that the precalc turns into 32-bit pictures through three
// grey palettes (0x403f53; palettes A and B from the percentage tables 0x41b7c2 / 0x41fc1c, palette C of 0x419d5b).
// The remake shows faithful high-resolution redraws (clone/remake/art/<name>.png, the default) or the original pixels
// (--original-art). A part never needs to know which: it asks the context for the picture by name and draws it with
// ctx.overlay (the blit 0x404002 with its blend level) at the original's position.
//
// A picture texture covers exactly the original's w x h pixels (a redraw the same frame at its own resolution) and
// holds premultiplied alpha:
//   - original pixels: the 6-bit grey v of the palette as v / 63 (white is 1.0), opaque; index 0 (and 0xf0 for the
//     texts of palette A) transparent, as the decoder 0x403f53 makes them 0xffffffff for the blits to skip;
//   - text redraws (white letters anti-aliased on black, 8-bit grey): the grey is the coverage of white, so over black
//     they look like the original and elsewhere (part 1's title over the particles) the letters have no dark rim;
//   - glyph redraws (grey with alpha): as they are.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render.h"

class CloneExe;

// Where a stored picture is and how 0x403f53 decodes it (addresses and sizes as main / 0x419d5b pass them).
struct PictureInfo {
    const char* name;  // the art file name (art/<name>.png) and the name parts use
    uint32_t addr;     // 8-bit pixels, w * h bytes, row by row
    int w, h;
    char palette;      // 'A', 'B' or 'C'
    bool keepF0;       // 0x403f53's last argument: false = index 0xf0 is transparent too
    bool text;         // a text (white on black) rather than a glyph
};
const std::vector<PictureInfo>& pictureList();
const PictureInfo* findPicture(const std::string& name);

// The 6-bit grey of each index (0..63) of palette 'A', 'B' (main 0x4008d4: entry 240 + k = table[k] * 63 / 100, the
// others 0) or 'C' (0x419d5b: entry c = c * 63 / 15 for c < 16, the others unused: 0).
std::array<uint8_t, 256> greyPalette(const CloneExe& exe, char palette);

// The original's picture as RGBA8 with premultiplied alpha: grey v -> 255 * v / 63, transparent pixels 0.
Image originalPicture(const CloneExe& exe, const PictureInfo& p);

struct Picture {
    std::string name;
    Texture tex;             // RGBA8, premultiplied alpha, linear (redraws: mipmapped); clamp to edge
    bool redraw = false;     // true: a redraw (any resolution); false: the original's pixels
    bool text = false;       // a text picture (PictureInfo::text)
    int srcW = 0, srcH = 0;  // the original's size in pixels (the texture covers exactly this)
};

// Loads and caches the pictures (owned by Demo; parts use ctx.picture()).
class PictureStore {
public:
    void init(const CloneExe* exe, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }
    std::string redrawPath(const std::string& name) const;  // art/<name>.png if it exists, else ""
    // The picture as a texture: the redraw when art is on and one exists, else the original's pixels. Cached; the
    // reference stays valid until release(). Needs the GL context. Unknown names give an empty picture (logged).
    const Picture& get(const std::string& name);
    void release();

private:
    const CloneExe* exe_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<std::string, std::unique_ptr<Picture>> cache_;
};
