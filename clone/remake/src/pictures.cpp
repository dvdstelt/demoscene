// The stored pictures (pictures.h): the decoder 0x403f53 with the grey palettes of main 0x4008d4..0x400946 and
// 0x419d5b, and the redraws in clone/remake/art (decoded with stb_image on worker threads at start-up).
#include "pictures.h"

#include <SDL3/SDL.h>

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
    // main 0x400946..0x400a4a and 0x419d5b: 0x403f53(pixels, palette, w, h, keep_f0) (notes-gfx 7.1)
    static const std::vector<PictureInfo> list = {
        {"meet_us", 0x41a950, 42, 9, 'A', false, true},                       // part 2
        {"get_them_clones", 0x41aaca, 85, 12, 'A', false, true},              // part 5
        {"connect_the_dots", 0x41aec6, 88, 10, 'A', false, true},             // part 3
        {"tunnels_meet_more_tunnels", 0x41b236, 142, 10, 'A', false, true},   // part 4
        {"glyph1", 0x41b802, 77, 65, 'B', true, false},                       // part 2
        {"glyph2", 0x41cb8f, 63, 67, 'B', true, false},                       // part 5
        {"glyph3", 0x41dc0c, 68, 68, 'B', true, false},                       // part 3
        {"glyph4", 0x41ee1c, 56, 64, 'B', true, false},                       // part 4
        {"title_clone_meets_clone", 0x41fc5c, 101, 9, 'C', false, true},      // parts 1, 11
        {"credits_aap_echo_simon", 0x41ffe9, 37, 32, 'C', false, true},       // part 1
    };
    return list;
}

const PictureInfo* findPicture(const std::string& name) {
    for (const PictureInfo& p : pictureList())
        if (name == p.name) return &p;
    return nullptr;
}

std::array<uint8_t, 256> greyPalette(const CloneExe& exe, char palette) {
    std::array<uint8_t, 256> g{};
    if (palette == 'C') {
        for (int c = 0; c < 16; c++) g[size_t(c)] = uint8_t(c * 63 / 15);
        return g;
    }
    // percentages, dword k = entry 240 + k (k = 15 - c in main's loop)
    uint32_t table = palette == 'A' ? 0x41b7c2 : 0x41fc1c;
    for (int k = 0; k < 16; k++) g[size_t(240 + k)] = uint8_t(exe.u32(table + 4 * uint32_t(k)) * 63 / 100);
    return g;
}

Image originalPicture(const CloneExe& exe, const PictureInfo& p) {
    std::array<uint8_t, 256> pal = greyPalette(exe, p.palette);
    const uint8_t* src = exe.at(p.addr, size_t(p.w) * p.h);
    Image im;
    im.w = p.w;
    im.h = p.h;
    im.rgba.resize(size_t(p.w) * p.h * 4);
    for (int i = 0; i < p.w * p.h; i++) {
        uint8_t c = src[i], *px = &im.rgba[size_t(i) * 4];
        if (c == 0 || (c == 0xf0 && !p.keepF0)) continue;  // 0xffffffff: skipped by the blits
        uint8_t v = uint8_t((pal[c] * 255 + 31) / 63);
        px[0] = px[1] = px[2] = v;
        px[3] = 255;
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

Decoded decode(const std::string& path, bool text) {
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
    bool alpha = ch == 2 || ch == 4;
    for (size_t i = 0; i < d.im.rgba.size(); i += 4) {
        uint8_t* p = &d.im.rgba[i];
        if (alpha) {  // premultiply
            for (int k = 0; k < 3; k++) p[k] = uint8_t((p[k] * p[3] + 127) / 255);
        } else if (text) {  // white letters on black: the grey is the coverage of white
            uint8_t g = uint8_t((p[0] + p[1] + p[2] + 1) / 3);
            p[0] = p[1] = p[2] = p[3] = g;
        }
    }
    return d;
}
}  // namespace

void PictureStore::init(const CloneExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    if (!useArt_) return;
    // decode every redraw on worker threads now; get() uploads them
    std::lock_guard<std::mutex> g(g_pendingLock);
    for (const PictureInfo& p : pictureList()) {
        std::string path = redrawPath(p.name);
        if (!path.empty()) g_pending[p.name] = std::async(std::launch::async, decode, path, p.text).share();
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
        SDL_Log("picture: no stored picture named '%s'", name.c_str());
        return *cache_.emplace(name, std::move(pic)).first->second;
    }
    pic->srcW = info->w;
    pic->srcH = info->h;
    pic->text = info->text;
    std::string path = useArt_ ? redrawPath(name) : "";
    if (!path.empty()) {
        std::shared_future<Decoded> fut;
        {
            std::lock_guard<std::mutex> g(g_pendingLock);
            auto p = g_pending.find(name);
            if (p != g_pending.end()) {
                fut = p->second;
                g_pending.erase(p);  // the decoded pixels go with the last reference, after the upload
            }
        }
        Decoded own;
        if (!fut.valid()) own = decode(path, info->text);
        const Decoded& d = fut.valid() ? fut.get() : own;
        if (d.im.w > 0) {
            pic->tex.create(d.im, false, true);
            pic->redraw = true;
        } else {
            SDL_Log("picture: cannot read %s (%s): using the original's pixels", path.c_str(), d.err.c_str());
        }
    }
    if (!pic->redraw) pic->tex.create(originalPicture(*exe_, *info), false, false);
    return *cache_.emplace(name, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [n, p] : cache_) p->tex.destroy();
    cache_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();  // waits for running decodes
}
