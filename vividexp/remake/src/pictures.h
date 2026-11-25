// The pictures and textures from VIVID.EXE's archives, and their redraws.
//
// The original draws every picture as palette indices, and the palette the DAC holds decides the colours (fades
// included), so the scenes draw the original's pictures as indices through ctx.paletteTex() (vivid/images.h). Where
// a hand-drawn picture has a faithful high-resolution redraw (vividexp/remake/art/<name>.png, the default; AGENTS.md
// "Redrawn pictures"), the scene draws the redraw instead and applies what the palette does to the picture's colours
// (each scene's notes say how). `name` is the archive file's base name without its extension ("alkutext" for
// pics/alkutext.pcx). --original-art turns the redraws off.
#pragma once
#include <map>
#include <memory>
#include <string>

#include "render.h"
#include "vivid/images.h"

class VividExe;

class PictureStore {
public:
    void init(const VividExe* exe, std::string artDir, bool useArt);
    bool useArt() const { return useArt_; }
    const std::string& artDir() const { return artDir_; }

    // The art name of an archive file: "pics/alkutext.pcx" -> "alkutext".
    static std::string artName(const std::string& file);
    // art/<name>.png when art is on and the file exists, else "".
    std::string redrawPath(const std::string& name) const;

    // The original picture's indices and own palette, decoded once (cached; throws std::out_of_range if the archive
    // has no such file).
    const vivid::Indexed& indexed(const std::string& file);
    // ... uploaded as GL_R8UI (cached; needs the GL context).
    const Texture& indices(const std::string& file);
    // The redraw as an RGBA8 texture, linear and mipmapped (cached; nullptr when art is off or there is none).
    const Texture* redraw(const std::string& file);
    void release();

private:
    const VividExe* exe_ = nullptr;
    std::string artDir_;
    bool useArt_ = true;
    std::map<std::string, std::unique_ptr<vivid::Indexed>> indexed_;
    std::map<std::string, std::unique_ptr<Texture>> indices_, redraws_;
};
