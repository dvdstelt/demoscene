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
    // The pictures parts draw as they are (inside/re/notes-*.md), with the addresses of their file names.
    static const std::vector<PictureInfo> list = {
        // logo: logo/inside.raw, the title "INSIDE", drawn from 4:32 at (34, 13), index 0 transparent
        // (notes-intro-logo.md; 0x415df0 type 3)
        {"logo_inside", 0x4208e8, 0x4208d8, 249, 139, 0, 0},
        // water: water/water.raw, the background seen through the water (a digitised photo of a room, no people),
        // full screen, shifted per pixel by the ripple (notes-water.md)
        {"water", 0x4213d4, 0x4213c4, 320, 200, 0, -1},
        // the sprites parts' photos nigga/spriteK.raw with their pictures' palettes, unscaled, index 0 transparent
        // (notes-sprites.md 1; 0x415df0 type 3); the part asks for them by file name
        {"sprite1", 0x420ce8, 0x420cac, 164, 156, 0, 0},
        {"sprite2", 0x420d48, 0x420d0c, 172, 200, 0, 0},
        {"sprite9", 0x420da8, 0x420d6c, 274, 147, 0, 0},
        {"sprite4", 0x420e30, 0x420df4, 170, 168, 0, 0},
    };
    return list;
}

const std::vector<std::string>& detailList() {
    static const std::vector<std::string> list = {
        "wtrmask", "tunnel", "piikkis",                // credits: the water mask (levels), the tunnel, the ball
        "twlogo", "inmask",                            // logo: the 3D logo's texture, the lettering's mask
        "nigga1", "nigga2", "nigga4", "nigga9",        // sprites (warp, room) and twirl: the backgrounds
        "jeesus1", "jeesus2",                          // flower and twirl: the environment map, the tube
        "temple", "waterenv",                          // temple, water: the chrome spheres
        "intro",                                       // intro: the photo collage
    };
    return list;
}

std::string detailName(const std::string& datFile) {
    size_t a = datFile.find_last_of("/\\");
    std::string base = a == std::string::npos ? datFile : datFile.substr(a + 1);
    return base.substr(0, base.rfind('.'));
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

Image originalPicture(const InsideExe& exe, const InsideDat& dat, const PictureInfo& p) {
    auto pal = dat.file(exe.cstr(p.palette));
    auto px = dat.file(exe.cstr(p.file));
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

void PictureStore::init(const InsideExe* exe, const InsideDat* dat, std::string artDir, bool useArt) {
    exe_ = exe;
    dat_ = dat;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    if (!useArt_) return;
    std::lock_guard<std::mutex> g(g_pendingLock);
    for (const PictureInfo& p : pictureList()) {
        std::string path = redrawPath(p.name);
        if (!path.empty()) g_pending[p.name] = std::async(std::launch::async, decode, path).share();
    }
    for (const std::string& n : detailList()) {
        std::string path = redrawPath("tex_" + n);
        if (!path.empty()) g_pending["tex_" + n] = std::async(std::launch::async, decode, path).share();
    }
}

const Image* PictureStore::detail(const std::string& name) {
    if (!useArt_) return nullptr;
    auto it = details_.find(name);
    if (it != details_.end()) return it->second.get();
    std::string key = "tex_" + name;
    std::shared_future<Decoded> fut;
    {
        std::lock_guard<std::mutex> g(g_pendingLock);
        auto p = g_pending.find(key);
        if (p != g_pending.end()) {
            fut = p->second;
            g_pending.erase(p);
        }
    }
    std::unique_ptr<Image> im;
    if (fut.valid()) {
        const Decoded& d = fut.get();
        if (d.im.w > 0) im = std::make_unique<Image>(d.im);
        else SDL_Log("picture: cannot read the upscale %s (%s)", key.c_str(), d.err.c_str());
    }
    return details_.emplace(name, std::move(im)).first->second.get();
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
    if (!exe_ || !dat_ || !info) {
        SDL_Log("picture: no picture named '%s'", name.c_str());
        return *cache_.emplace(name, std::move(pic)).first->second;
    }
    pic->srcW = info->w;
    pic->srcH = info->h;
    std::string path = useArt_ ? redrawPath(name) : "";
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
    if (!pic->redraw) pic->tex.create(originalPicture(*exe_, *dat_, *info), false, false);
    return *cache_.emplace(name, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [n, p] : cache_) p->tex.destroy();
    cache_.clear();
    details_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();
}
