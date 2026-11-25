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

namespace {
Image decodePng(const std::string& path) {
    Image im;
    int w = 0, h = 0, n = 0;
    if (uint8_t* d = stbi_load(path.c_str(), &w, &h, &n, 4)) {
        im.w = w;
        im.h = h;
        im.rgba.assign(d, d + size_t(w) * size_t(h) * 4);
        stbi_image_free(d);
    } else {
        SDL_Log("cannot read %s: %s", path.c_str(), stbi_failure_reason());
    }
    return im;
}
}  // namespace

void PictureStore::init(const VividExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    decoding_.clear();
    if (!useArt_) return;
    // decode every redraw in the art folder on worker threads while the scenes initialise
    int count = 0;
    if (char** names = SDL_GlobDirectory(artDir_.c_str(), "*.png", 0, &count)) {
        for (int i = 0; i < count; ++i) {
            std::string file = names[i];
            std::string name = file.substr(0, file.size() - 4);
            std::string path = artDir_ + "/" + file;
            decoding_[name] = std::async(std::launch::async, decodePng, path).share();
        }
        SDL_free(names);
    }
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
    auto dec = decoding_.find(artName(file));
    if (useArt_ && dec != decoding_.end()) {
        const Image& im = dec->second.get();
        if (im.w > 0) {
            tex = std::make_unique<Texture>();
            tex->create(im, false, true);
        }
        decoding_.erase(dec);
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
    for (auto& [k, f] : decoding_) f.wait();
    decoding_.clear();
}
