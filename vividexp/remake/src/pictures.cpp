// The pictures (pictures.h): decoded from VIVID.EXE's archives, and the redraws in vividexp/remake/art.
#include "pictures.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "exe.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "stb_image.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

void PictureStore::init(const VividExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
}

std::string PictureStore::artName(const std::string& file) {
    size_t a = file.find_last_of("/\\");
    std::string base = a == std::string::npos ? file : file.substr(a + 1);
    size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

std::string PictureStore::redrawPath(const std::string& name) const {
    if (!useArt_) return "";
    std::string p = artDir_ + "/" + name + ".png";
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        std::fclose(f);
        return p;
    }
    return "";
}

const vivid::Indexed& PictureStore::indexed(const std::string& file) {
    auto& p = indexed_[file];
    if (!p) p = std::make_unique<vivid::Indexed>(vivid::loadPicture(*exe_, file));
    return *p;
}

const Texture& PictureStore::indices(const std::string& file) {
    auto& t = indices_[file];
    if (!t) t = std::make_unique<Texture>(vivid::uploadIndices(indexed(file)));
    return *t;
}

const Texture* PictureStore::redraw(const std::string& file) {
    auto it = redraws_.find(file);
    if (it != redraws_.end()) return it->second.get();
    std::unique_ptr<Texture> tex;
    std::string path = redrawPath(artName(file));
    if (!path.empty()) {
        int w = 0, h = 0, n = 0;
        if (uint8_t* d = stbi_load(path.c_str(), &w, &h, &n, 4)) {
            Image im;
            im.w = w;
            im.h = h;
            im.rgba.assign(d, d + size_t(w) * size_t(h) * 4);
            stbi_image_free(d);
            tex = std::make_unique<Texture>();
            tex->create(im, false, true);
        } else {
            SDL_Log("cannot read %s: %s", path.c_str(), stbi_failure_reason());
        }
    }
    Texture* raw = tex.get();
    redraws_[file] = std::move(tex);
    return raw;
}

void PictureStore::release() {
    for (auto& [k, t] : indices_) if (t) t->destroy();
    for (auto& [k, t] : redraws_) if (t) t->destroy();
    indices_.clear();
    redraws_.clear();
    indexed_.clear();
}
