// The pictures (pictures.h): 8-bit pixels and palettes from INSIDE.DAT, and the redraws in inside/remake/art
// (decoded with stb_image on worker threads at start-up).
#include "pictures.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <future>
#include <mutex>

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

const std::vector<PictureInfo>& pictureList() {
    // The pictures parts draw as they are (inside/re/notes-*.md). Photos are never redrawn.
    static const std::vector<PictureInfo> list = {
        {"intro", "intro/intro.raw", "intro/intro.pal", 320, 200, 0, -1, true},
    };
    return list;
}

const PictureInfo* findPicture(const std::string& name) {
    for (const PictureInfo& p : pictureList())
        if (name == p.name) return &p;
    return nullptr;
}

std::array<Vec3, 256> datPalette(const InsideDat& dat, const std::string& file) {
    std::array<Vec3, 256> pal{};
    auto d = dat.file(file);
    for (size_t i = 0; i < 256 && i * 3 + 2 < d.size(); i++) {
        auto c = [&](size_t k) { return float(std::min<int>(d[i * 3 + k], 63)) / 63.0f; };
        pal[i] = Vec3(c(0), c(1), c(2));
    }
    return pal;
}

Image originalPicture(const InsideDat& dat, const PictureInfo& p) {
    auto pal = dat.file(p.palette);
    auto px = dat.file(p.file);
    Image im;
    im.w = p.w;
    im.h = p.h;
    im.rgba.assign(size_t(p.w) * p.h * 4, 0);
    for (int i = 0; i < p.w * p.h; i++) {
        size_t o = size_t(p.offset) + size_t(i);
        if (o >= px.size()) break;
        uint8_t c = px[o], *q = &im.rgba[size_t(i) * 4];
        if (int(c) == p.transparent) continue;
        for (int k = 0; k < 3; k++) {
            int v = size_t(c) * 3 + k < pal.size() ? std::min<int>(pal[size_t(c) * 3 + k], 63) : 0;
            q[k] = uint8_t((v * 255 + 31) / 63);
        }
        q[3] = 255;
    }
    return im;
}

namespace {
bool exists(const std::string& p) {
    if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

// Decoded redraws, premultiplied, waiting to be uploaded.
struct Decoded { Image im; std::string err; };
std::mutex g_pendingLock;
std::map<std::string, std::shared_future<Decoded>> g_pending;

Decoded decode(const std::string& path) {
    Decoded d;
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px) {
        d.err = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        return d;
    }
    d.im.w = w;
    d.im.h = h;
    d.im.rgba.assign(px, px + size_t(w) * h * 4);
    stbi_image_free(px);
    if (ch == 2 || ch == 4)
        for (size_t i = 0; i < d.im.rgba.size(); i += 4) {
            uint8_t* p = &d.im.rgba[i];
            for (int k = 0; k < 3; k++) p[k] = uint8_t((p[k] * p[3] + 127) / 255);
        }
    return d;
}
}  // namespace

void PictureStore::init(const InsideDat* dat, std::string artDir, bool useArt) {
    dat_ = dat;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    if (!useArt_) return;
    std::lock_guard<std::mutex> g(g_pendingLock);
    for (const PictureInfo& p : pictureList()) {
        std::string path = p.photo ? "" : redrawPath(p.name);
        if (!path.empty()) g_pending[p.name] = std::async(std::launch::async, decode, path).share();
    }
}

std::string PictureStore::redrawPath(const std::string& name) const {
    if (artDir_.empty()) return "";
    for (const char* ext : {".png", ".jpg"})
        if (exists(artDir_ + "/" + name + ext)) return artDir_ + "/" + name + ext;
    return "";
}

const Picture& PictureStore::get(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return *it->second;
    const PictureInfo* info = findPicture(name);
    auto pic = std::make_unique<Picture>();
    pic->name = name;
    if (!dat_ || !info) {
        SDL_Log("picture: no picture named '%s'", name.c_str());
        return *cache_.emplace(name, std::move(pic)).first->second;
    }
    pic->srcW = info->w;
    pic->srcH = info->h;
    std::string path = useArt_ && !info->photo ? redrawPath(name) : "";
    if (!path.empty()) {
        std::shared_future<Decoded> fut;
        {
            std::lock_guard<std::mutex> g(g_pendingLock);
            auto p = g_pending.find(name);
            if (p != g_pending.end()) {
                fut = p->second;
                g_pending.erase(p);
            }
        }
        Decoded own;
        if (!fut.valid()) own = decode(path);
        const Decoded& d = fut.valid() ? fut.get() : own;
        if (d.im.w > 0) {
            pic->tex.create(d.im, false, true);
            pic->redraw = true;
        } else {
            SDL_Log("picture: cannot read %s (%s): using the original's pixels", path.c_str(), d.err.c_str());
        }
    }
    if (!pic->redraw) pic->tex.create(originalPicture(*dat_, *info), false, false);
    return *cache_.emplace(name, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [n, p] : cache_) p->tex.destroy();
    cache_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();
}
