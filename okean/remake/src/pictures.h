// The pictures drawn as they are (okean/re/notes-*.md list them per part): pixels in KORSO2.EXE shown through a
// palette. The remake shows faithful high-resolution redraws (okean/remake/art/<name>.png, the default) where one
// exists and the original pixels otherwise or with --original-art. A part never needs to know which: it asks the
// context for the picture by name and draws it where the original does.
//
// A picture texture covers exactly the original's w x h pixels (a redraw the same frame at its own resolution) and
// holds premultiplied alpha: original pixels are the palette's colours (6-bit v -> v / 63), opaque, except the
// transparent index.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render.h"

class OkeanExe;

struct PictureInfo {
    const char* name;      // the art file name (art/<name>.png) and the name parts use
    int w, h;              // size in pixels
    // The original's pixels as RGBA8 with premultiplied alpha, read from the exe (pictures.cpp).
    std::function<Image(const OkeanExe&)> original;
};
const std::vector<PictureInfo>& pictureList();
const PictureInfo* findPicture(const std::string& name);

struct Picture {
    std::string name;
    Texture tex;             // RGBA8, premultiplied alpha, linear (redraws: mipmapped); clamp to edge
    bool redraw = false;     // true: a redraw (any resolution); false: the original's pixels
    int srcW = 0, srcH = 0;  // the original's size in pixels (the texture covers exactly this)
};

// Loads and caches the pictures (owned by Demo; parts use ctx.picture()).
class PictureStore {
public:
    void init(const OkeanExe* exe, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }
    std::string redrawPath(const std::string& name) const;  // art/<name>.png if it exists, else ""
    // The picture as a texture: the redraw when art is on and one exists, else the original's pixels. Cached; the
    // reference stays valid until release(). Needs the GL context. Unknown names give an empty picture (logged).
    const Picture& get(const std::string& name);
    void release();

private:
    const OkeanExe* exe_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<std::string, std::unique_ptr<Picture>> cache_;
};
