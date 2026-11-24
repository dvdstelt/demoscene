// The pictures (pictures.h): pixels and palettes from KORSO2.EXE, and the redraws in okean/remake/art (decoded with
// stb_image on worker threads at start-up).
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

namespace {
// der piipo's font (notes-core.md section 6): the sheet at 0x3cec2, 320 bytes per row, 38 rows, the 26 glyphs in
// columns 0..284. Index 216 (the body) white, 254 (single edge pixels, the grey 33/63) grey, 7 (the outline) red,
// 0 transparent: the printer (okean/text.cpp) colours body and outline with the part's entries 216 and 7.
Image fontSheet(const OkeanExe& exe) {
    Image im;
    im.w = 285;
    im.h = 38;
    im.rgba.assign(size_t(im.w) * im.h * 4, 0);
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            uint8_t c = exe.u8(0x3cec2 + uint32_t(y * 320 + x)), *q = &im.rgba[size_t(y * im.w + x) * 4];
            if (c == 216) q[0] = q[1] = q[2] = 255;
            else if (c == 254) q[0] = q[1] = q[2] = uint8_t((33 * 255 + 31) / 63);
            else if (c == 7) q[0] = 255;
            else if (c != 0) q[0] = q[1] = q[2] = 255;   // (none in the sheet)
            q[3] = c ? 255 : 0;
        }
    return im;
}
// The title's lettering "the sea ROBOT of love" (notes-title-objects.md): a 1-bit 320x200 mask at 0x1502c that the
// beams skip; never drawn itself. White = letter, opaque.
Image titleMask(const OkeanExe& exe) {
    Image im;
    im.w = 320;
    im.h = 200;
    im.rgba.assign(size_t(im.w) * im.h * 4, 0);
    for (int i = 0; i < im.w * im.h; i++) {
        uint8_t v = exe.u8(0x1502c + uint32_t(i)) ? 255 : 0, *q = &im.rgba[size_t(i) * 4];
        q[0] = q[1] = q[2] = v;
        q[3] = 255;
    }
    return im;
}
}  // namespace

const std::vector<PictureInfo>& pictureList() {
    // The pictures parts draw as they are (okean/re/notes-*.md).
    static const std::vector<PictureInfo> list = {
        {"font", 285, 38, fontSheet, true},
        {"title_mask", 320, 200, titleMask, false},
    };
    return list;
}

const PictureInfo* findPicture(const std::string& name) {
    for (const PictureInfo& p : pictureList())
        if (name == p.name) return &p;
    return nullptr;
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

void PictureStore::init(const OkeanExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    if (!useArt_) return;
    std::lock_guard<std::mutex> g(g_pendingLock);
    for (const PictureInfo& p : pictureList()) {
        std::string path = redrawPath(p.name);
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
    if (!exe_ || !info) {
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
            if (info->alphaFromBlack) {
                Image im = d.im;
                for (size_t i = 0; i < im.rgba.size(); i += 4)
                    im.rgba[i + 3] = std::max({im.rgba[i], im.rgba[i + 1], im.rgba[i + 2]});
                pic->tex.create(im, false, true);
            } else {
                pic->tex.create(d.im, false, true);
            }
            pic->redraw = true;
        } else {
            SDL_Log("picture: cannot read %s (%s): using the original's pixels", path.c_str(), d.err.c_str());
        }
    }
    if (!pic->redraw) pic->tex.create(info->original(*exe_), false, false);
    return *cache_.emplace(name, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [n, p] : cache_) p->tex.destroy();
    cache_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();
}
