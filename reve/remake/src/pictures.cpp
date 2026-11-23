// The pictures (pictures.h): RAW files of REVE.EXE's archive, and the redraws in reve/remake/art (decoded with
// stb_image on worker threads at start-up).
#include "pictures.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <future>
#include <mutex>
#include <stdexcept>

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
    // The pictures parts draw as they are (reve/re/notes-*.md).
    static const std::vector<PictureInfo> list = {
        // title (entry 0): GFX\REVE.RAW (name at 0x205cfb) through GFX\TEXTURE.RAW's palette (0x205ceb), the
        // title "REVE" centred on black, index 0 not drawn (0x25f0be)
        {"title", 0x205cfb, 0x205ceb, 0, 0, -1, -1, 0},
        // the still pictures (scenes/stills.cpp), with their own palettes: centred sprites (index 0 not drawn) and
        // whole-screen pictures (opaque)
        {"junior", 0x205db0, 0, 0, 0, -1, -1, 0},     // GFX\JUNIOR.RAW, entry 16
        {"mrtommy", 0x205dbf, 0, 0, 0, -1, -1, 0},    // GFX\MRTOMMY.RAW, entry 17
        {"dog", 0x205dcf, 0, 0, 0, -1, -1, 0},        // GFX\DOG.RAW, entry 24
        {"waaf", 0x205ddb, 0, 0, 0, -1, -1, -1},      // GFX\MRWAAF.RAW, entry 25
        {"yocol", 0x205e40, 0, 0, 0, -1, -1, -1},     // GFX\YOCOL.RAW, entry 29
        // the credits (entries 1-4, notes-parts3d.md): bands of 64 rows of GFX\CREDITS.RAW (0x205d08) through
        // GFX\TEXTURE.RAW's palette, drawn as sprites over the 3D, index 0 not drawn (the black outline is index 79)
        {"credits_antony", 0x205d08, 0x205ceb, 0, 0, 231, 64, 0},
        {"credits_shad", 0x205d08, 0x205ceb, 0, 64, 151, 64, 0},
        {"credits_lca", 0x205d08, 0x205ceb, 0, 128, 120, 64, 0},
        {"credits_ajt", 0x205d08, 0x205ceb, 0, 192, 130, 64, 0},
        // the strobes (scenes/strobe.cpp): whole-screen pictures with their own palettes, shown with their negative
        {"pulse", 0x205d46, 0, 0, 0, -1, -1, -1},     // GFX\PULSE.RAW, entries 5 and 6
        {"gene", 0x205dea, 0, 0, 0, -1, -1, -1},      // GFX\GENE.RAW, entries 20 and 21
        {"bouge", 0x205e5b, 0, 0, 0, -1, -1, -1},     // GFX\BOUGE.RAW, entries 27 and 28
    };
    return list;
}

const std::vector<std::string>& detailList() {
    static const std::vector<std::string> list = {};
    return list;
}

std::string detailName(const std::string& rawFile) {
    size_t a = rawFile.find_last_of("/\\");
    std::string base = a == std::string::npos ? rawFile : rawFile.substr(a + 1);
    base = base.substr(0, base.rfind('.'));
    for (char& ch : base) ch = char(std::tolower(uint8_t(ch)));
    return base;
}

const PictureInfo* findPicture(const std::string& name) {
    for (const PictureInfo& p : pictureList())
        if (name == p.name) return &p;
    return nullptr;
}

RawImage rawImage(const ReveExe& exe, const std::string& file) {
    auto d = exe.file(file);
    if (d.size() < 32 + 768 || std::memcmp(d.data(), "mhwanh", 6) != 0)
        throw std::runtime_error(file + ": not a RAW picture");
    RawImage im;
    im.w = d[8] << 8 | d[9];
    im.h = d[10] << 8 | d[11];
    if (d.size() < 800 + size_t(im.w) * im.h) throw std::runtime_error(file + ": short RAW picture");
    for (int i = 0; i < 768; i++) im.pal6[i] = uint8_t(d[32 + i] >> 2);
    im.px.assign(d.begin() + 800, d.begin() + 800 + size_t(im.w) * im.h);
    return im;
}

Image originalPicture(const ReveExe& exe, const PictureInfo& p) {
    RawImage raw = rawImage(exe, exe.cstr(p.file));
    RawImage pal = p.palette ? rawImage(exe, exe.cstr(p.palette)) : RawImage();
    const auto& p6 = p.palette ? pal.pal6 : raw.pal6;
    const int w = p.w < 0 ? raw.w : p.w, h = p.h < 0 ? raw.h : p.h;
    Image im;
    im.w = w;
    im.h = h;
    im.rgba.assign(size_t(w) * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sx = p.x + x, sy = p.y + y;
            if (sx < 0 || sy < 0 || sx >= raw.w || sy >= raw.h) continue;
            uint8_t c = raw.px[size_t(sy) * raw.w + sx], *q = &im.rgba[(size_t(y) * w + x) * 4];
            if (int(c) == p.transparent) continue;
            for (int k = 0; k < 3; k++) q[k] = uint8_t((p6[c * 3 + k] * 255 + 31) / 63);
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

void PictureStore::init(const ReveExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
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
    if (!exe_ || !info) {
        SDL_Log("picture: no picture named '%s'", name.c_str());
        return *cache_.emplace(name, std::move(pic)).first->second;
    }
    try {
        RawImage raw = rawImage(*exe_, exe_->cstr(info->file));
        pic->srcW = info->w < 0 ? raw.w : info->w;
        pic->srcH = info->h < 0 ? raw.h : info->h;
    } catch (const std::exception& e) {
        SDL_Log("picture %s: %s", name.c_str(), e.what());
        return *cache_.emplace(name, std::move(pic)).first->second;
    }
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
    if (!pic->redraw) pic->tex.create(originalPicture(*exe_, *info), false, false);
    return *cache_.emplace(name, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [n, p] : cache_) p->tex.destroy();
    cache_.clear();
    details_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();
}
