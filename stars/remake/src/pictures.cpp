// Palettes and pictures (pictures.h): the layouts of the picture-like overlay files (stars/re/notes-core.md sections
// 13 and 14), the redraws in stars/remake/art (decoded with stb_image on worker threads at start-up) and the
// original's pixels through a palette (0x17e50 shows a 0xfd00 picture: its palette, then the 320x200 pixels).
#include "pictures.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
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

Palette vgaPalette(std::span<const uint8_t> rgb, int first, const Palette& base) {
    Palette p = base;
    for (size_t i = 0; i + 2 < rgb.size() && first + int(i / 3) < 256; i += 3)
        p[size_t(first) + i / 3] = Vec3(rgb[i] / 63.0f, rgb[i + 1] / 63.0f, rgb[i + 2] / 63.0f);
    return p;
}

bool pictureLayout(int n, PictureLayout* l) {
    switch (n) {
        case 8: case 9: case 10: case 11: case 12: case 13: case 14:  // 768-byte palette + 320x200
        case 52: case 53: case 54: case 55: case 56: case 57:
            *l = {0x300, 320, 200}; return true;
        case 19: case 25: case 27: case 47: case 58:                     // 320x200 without palette
            *l = {0, 320, 200}; return true;
        case 5: *l = {0x2d, 144, 4 * 36}; return true;                   // 15 colours, 4 sprites of 144x36
        case 6: *l = {0x30, 128, 4 * 74}; return true;                   // 16 colours, 4 sprites of 128x74
        case 7: *l = {0xc0, 256, 256}; return true;                      // 64 colours, 256x256 texture
        case 32: *l = {0, 80, 36}; return true;                          // sprite "23806 faces"
        case 35: *l = {0x300, 256, 256}; return true;                    // palette + 0xffff map (+ remap tables)
        case 2: case 3: case 16: case 17: case 21: case 23: case 43:     // 256x256 maps
        case 29: case 30: case 44: case 49:                              // (29, 44, 49: 0xffff bytes)
            *l = {0, 256, 256}; return true;
        default: return false;
    }
}

namespace {
// The file's own palette, or a grey ramp.
Palette ownPalette(int n, const std::vector<uint8_t>& f) {
    std::span<const uint8_t> d(f.data(), f.size());
    if (f.size() == 0xfd00 || n == 35) return vgaPalette(d.first(0x300));
    if (n == 5) return vgaPalette(d.first(0x2d), 1);
    if (n == 6) return vgaPalette(d.first(0x30), 0x60);
    if (n == 7) return vgaPalette(d.first(0xc0), 0x10);
    Palette p;
    for (int i = 0; i < 256; i++) p[size_t(i)] = Vec3(i / 255.0f, i / 255.0f, i / 255.0f);
    return p;
}

uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

bool exists(const std::string& p) {
    if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

// Decoded redraws, premultiplied, waiting to be uploaded.
struct Decoded { Image im; std::string err; };
std::mutex g_pendingLock;
std::map<int, std::shared_future<Decoded>> g_pending;

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
    if (ch == 4)  // premultiply (sprite redraws keep coloured or magenta texels under alpha 0)
        for (size_t i = 0; i < d.im.rgba.size(); i += 4) {
            unsigned a = d.im.rgba[i + 3];
            for (int k = 0; k < 3; k++) d.im.rgba[i + k] = uint8_t((d.im.rgba[i + k] * a + 127) / 255);
        }
    return d;
}

Image originalPixels(int n, const std::vector<uint8_t>& f, const PictureLayout& l, const PictureOptions& o) {
    Palette own;
    const Palette* pal = o.palette;
    if (!pal) { own = ownPalette(n, f); pal = &own; }
    int key = o.key != -2 ? o.key : (n == 5 || n == 6 ? 0 : -1);
    int below = o.opaqueBelow != -2 ? o.opaqueBelow : (n == 32 ? 0x50 : 256);
    Image im;
    im.w = l.w;
    im.h = l.h;
    im.rgba.resize(size_t(l.w) * l.h * 4);
    for (int i = 0; i < l.w * l.h; i++) {
        size_t a = size_t(l.offset) + size_t(i);
        int v = a < f.size() ? f[a] : 0;  // 29, 44, 49 are one byte short of 256x256
        uint8_t* px = &im.rgba[size_t(i) * 4];
        if (v == key || v >= below) { px[0] = px[1] = px[2] = px[3] = 0; continue; }
        const Vec3& c = (*pal)[size_t((v + o.indexAdd) & 255)];
        px[0] = uint8_t(clamp01(c.x) * 255.0f + 0.5f);
        px[1] = uint8_t(clamp01(c.y) * 255.0f + 0.5f);
        px[2] = uint8_t(clamp01(c.z) * 255.0f + 0.5f);
        px[3] = 255;
    }
    return im;
}
}  // namespace

void PictureStore::init(const StarsExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    if (!useArt_) return;
    // decode every redraw on worker threads now; get() uploads them
    std::lock_guard<std::mutex> g(g_pendingLock);
    for (int n = 1; n <= StarsExe::kFiles; n++) {
        std::string p = redrawPath(n);
        if (!p.empty()) g_pending[n] = std::async(std::launch::async, decode, p).share();
    }
}

std::string PictureStore::redrawPath(int n) const {
    if (artDir_.empty()) return "";
    char name[32];
    for (const char* ext : {"png", "jpg"}) {
        std::snprintf(name, sizeof name, "/file%02d.%s", n, ext);
        if (exists(artDir_ + name)) return artDir_ + name;
    }
    return "";
}

const Picture& PictureStore::get(int n, const PictureOptions& o) {
    PictureLayout l;
    if (!exe_ || !pictureLayout(n, &l)) {
        static Picture none;
        SDL_Log("picture: file %d is not a picture", n);
        return none;
    }
    std::string path = useArt_ ? redrawPath(n) : "";
    uint64_t h = fnv(0xcbf29ce484222325ull, &n, sizeof n);
    if (path.empty()) {  // the options only matter for the original's pixels
        h = fnv(h, &o.key, sizeof o.key);
        h = fnv(h, &o.opaqueBelow, sizeof o.opaqueBelow);
        h = fnv(h, &o.indexAdd, sizeof o.indexAdd);
        if (o.palette) h = fnv(h, o.palette->data(), sizeof(Palette));
    }
    auto it = cache_.find(h);
    if (it != cache_.end()) return *it->second;

    auto pic = std::make_unique<Picture>();
    pic->file = n;
    pic->srcW = l.w;
    pic->srcH = l.h;
    if (!path.empty()) {
        std::shared_future<Decoded> fut;
        {
            std::lock_guard<std::mutex> g(g_pendingLock);
            auto p = g_pending.find(n);
            if (p != g_pending.end()) {
                fut = p->second;
                g_pending.erase(p);  // the decoded pixels go with the last reference, after the upload
            }
        }
        const Decoded& d = fut.valid() ? fut.get() : decode(path);
        if (d.im.w > 0) {
            pic->tex.create(d.im, false, true);
            pic->redraw = true;
        } else {
            SDL_Log("picture: cannot read %s (%s): using the original's pixels", path.c_str(), d.err.c_str());
        }
    }
    if (!pic->redraw) pic->tex.create(originalPixels(n, exe_->file(n), l, o), false, false);
    return *cache_.emplace(h, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [h, p] : cache_) p->tex.destroy();
    cache_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();  // waits for running decodes
}
