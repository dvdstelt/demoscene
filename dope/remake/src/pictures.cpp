// Palettes and pictures (pictures.h): the catalog of the pictures in the part images (dope/re/notes-core.md 8,
// notes-p2.md 2.1, notes-p3p5.md 3.6 / 4.8 / 5, notes-p4.md 2 / 10, notes-p6p7.md 5), the RIX3 / raw / IFF PBM
// readers (p4 14f3 loads the PBM: CMAP >> 2, ByteRun1 BODY), the redraws in dope/remake/art (decoded with stb_image
// on worker threads at start-up) and the original's pixels through a palette.
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

Palette rixPalette(const DopeImage& img, uint16_t seg, uint16_t off) {
    return vgaPalette(img.fileSpan(DopeImage::addr(seg, off) + 0x0a, 768));
}

// ---------------------------------------------------------------- the catalog
const std::vector<PictureDef>& pictureCatalog() {
    using F = PicFormat;
    using K = PicKind;
    static const std::vector<PictureDef> cat = {
        // hand-drawn pictures (redraws in art/, see art/README.md)
        // p1: the "Complex Media Labs" logo, RIX3 1024 x 200, 64 shade levels; the frame is the logo, rows 1..195
        {"p1-logo", 1, 0x00e4, 0x0000, F::Rix3, 0, 0, K::Shade, {}, 0, 1, 1024, 195},
        // p2: the credit strips (4 x 320 x 32, coverage = index - 0x80), the label, the backdrop of the "C"
        {"p2-credits", 2, 0x0451, 0x0000, F::Rix3, 0, 0, K::Mask},
        {"p2-label-blur", 2, 0x2bd2, 0x0000},
        {"p2-background", 2, 0x0ec3, 0x0000, F::Raw, 320, 200, K::Screen, {2, 0x6507, 0x0010, 0, 240}},
        // p3: the nebula (also p4's first background: p4 4EED:0006 holds the same bytes), the label
        {"p3-nebula", 3, 0x4b80, 0x0006, F::Rix3, 0, 0, K::Screen, {}, 0, 0, 0, 0, "p4-nebula"},
        {"p3-label-phong", 3, 0x3342, 0x0000},
        // p4: the labels, the chequerboard (IFF PBM; p5's background, p5 has no copy)
        {"p4-label-fire-clouds", 4, 0x0273, 0x0000},
        {"p4-label-fractal-scape", 4, 0x036e, 0x0000},
        {"p4-chequerboard", 4, 0x177a, 0x0006, F::Pbm, 0, 0, K::Screen, {}, 0, 0, 0, 0, "p5-chequerboard"},
        // p5: the gold (left) and red (right) env maps, the labels, the swirl (p6's background: p6 0C66:0006)
        {"p5-envmaps", 5, 0x03a6, 0x0006},
        {"p5-label-env", 5, 0x248f, 0x0000},
        {"p5-label-bump", 5, 0x258a, 0x0000},
        {"p5-swirl", 5, 0x4ea0, 0x0006, F::Rix3, 0, 0, K::Screen, {}, 0, 0, 0, 0, "p6-swirl"},
        // p6: the rings' env map (the drawn left half; the right half is generated per frame), the labels
        {"p6-envmap", 6, 0x0418, 0x0006, F::Rix3, 0, 0, K::Screen, {}, 0, 0, 128, 128},
        {"p6-label-shadow", 6, 0x3399, 0x0000},
        {"p6-label-morph", 6, 0x3494, 0x0000},
        {"p6-label-metal", 6, 0x358f, 0x0000},
        // p7: the horizon, "dope" (320 x 64, pasted at rows 50..113, the horizon's palette), the labels
        {"p7-horizon", 7, 0x5a83, 0x0006},
        {"p7-dope", 7, 0x6a55, 0x0006},
        {"p7-label-light", 7, 0x3beb, 0x0000},
        {"p7-label-world", 7, 0x3ce6, 0x0000},

        // generated maps and data pictures (original pixels; the parts usually read their indices)
        {"p2-envmaps", 2, 0x6507, 0x0006},                                          // two 128 x 128 env maps
        {"p3-phongmap", 3, 0x0306, 0x0006},                                         // Phong map (left half)
        {"p4-cloud", 4, 0x1ec1, 0x0000, F::Raw, 256, 256, K::Shade, {}, 0, 0, 0, 0, nullptr, 128},
        {"p4-landscape-height", 4, 0x2eec, 0x0000, F::Raw, 256, 256, K::Shade, {}, 0, 0, 0, 0, nullptr, 128},
        {"p4-landscape-colour", 4, 0x3eec, 0x0000, F::Raw, 256, 256, K::Screen, {4, 0x0010, 0x20d2, 0, 16}},
        {"p7-reflection-photo", 7, 0x0382, 0x0006},                                 // light-map base (grey)
        {"p7-spot", 7, 0x37ea, 0x0000, F::Raw, 128, 128, K::Shade},                 // light spot 0..63
    };
    return cat;
}

const PictureDef* findPicture(const std::string& name) {
    for (const PictureDef& d : pictureCatalog())
        if (name == d.name || (d.alias && name == d.alias)) return &d;
    return nullptr;
}

// ---------------------------------------------------------------- reading the original's pixels
namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

Palette greyRamp() {
    Palette p;
    for (int i = 0; i < 256; i++) p[size_t(i)] = Vec3(i / 255.0f, i / 255.0f, i / 255.0f);
    return p;
}

// IFF "FORM" ... "PBM ": BMHD (width, height), CMAP (8-bit RGB), BODY (ByteRun1: n >= 0 copies n + 1 bytes, n < 0
// repeats the next byte 1 - n times, -128 is a no-op).
bool readPbm(const DopeImage& img, uint32_t a, Indexed& out) {
    const uint8_t* f = img.at(a, 12);
    if (std::memcmp(f, "FORM", 4) != 0 || std::memcmp(f + 8, "PBM ", 4) != 0) return false;
    uint32_t end = a + 8 + be32(f + 4);
    uint32_t p = a + 12;
    std::span<const uint8_t> body;
    while (p + 8 <= end) {
        const uint8_t* c = img.at(p, 8);
        uint32_t n = be32(c + 4);
        std::span<const uint8_t> d = img.fileSpan(p + 8, n);
        if (std::memcmp(c, "BMHD", 4) == 0 && n >= 4) {
            out.w = d[0] << 8 | d[1];
            out.h = d[2] << 8 | d[3];
        } else if (std::memcmp(c, "CMAP", 4) == 0) {
            for (size_t i = 0; i + 2 < n && i / 3 < 256; i += 3)
                out.pal[i / 3] = Vec3((d[i] >> 2) / 63.0f, (d[i + 1] >> 2) / 63.0f, (d[i + 2] >> 2) / 63.0f);
        } else if (std::memcmp(c, "BODY", 4) == 0) {
            body = d;
        }
        p += 8 + n + (n & 1);
    }
    if (out.w <= 0 || out.h <= 0 || body.empty()) return false;
    out.px.assign(size_t(out.w) * size_t(out.h), 0);
    size_t o = 0, i = 0;
    while (o < out.px.size() && i < body.size()) {
        int n = int8_t(body[i++]);
        if (n >= 0) {
            for (int k = 0; k <= n && i < body.size() && o < out.px.size(); k++) out.px[o++] = body[i++];
        } else if (n != -128 && i < body.size()) {
            uint8_t v = body[i++];
            for (int k = 0; k < 1 - n && o < out.px.size(); k++) out.px[o++] = v;
        }
    }
    return true;
}
}  // namespace

Indexed readIndexed(const DopeExe& exe, const PictureDef& def) {
    const DopeImage& img = exe.part(def.part);
    uint32_t a = DopeImage::addr(def.seg, def.off);
    Indexed r;
    switch (def.fmt) {
        case PicFormat::Rix3: {
            if (std::memcmp(img.at(a, 4), "RIX3", 4) != 0) {
                SDL_Log("picture %s: no RIX3 header at %04X:%04X", def.name, def.seg, def.off);
                return r;
            }
            r.w = img.file16(a + 4);
            r.h = img.file16(a + 6);
            r.pal = vgaPalette(img.fileSpan(a + 0x0a, 768));
            auto px = img.fileSpan(a + 0x30a, size_t(r.w) * size_t(r.h));
            r.px.assign(px.begin(), px.end());
            break;
        }
        case PicFormat::Raw: {
            r.w = def.w;
            r.h = def.h;
            r.pal = greyRamp();
            auto px = img.fileSpan(a, size_t(r.w) * size_t(r.h));
            r.px.assign(px.begin(), px.end());
            break;
        }
        case PicFormat::Pbm:
            if (!readPbm(img, a, r)) SDL_Log("picture %s: cannot read the IFF PBM", def.name);
            break;
    }
    if (def.pal.part > 0 && def.pal.count > 0) {
        const DopeImage& pi = exe.part(def.pal.part);
        r.pal = vgaPalette(pi.span(def.pal.seg, def.pal.off, size_t(def.pal.count) * 3), def.pal.first, r.pal);
    }
    return r;
}

// ---------------------------------------------------------------- the store
namespace {
uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

bool exists(const std::string& p) {
    if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

// Decoded redraws, premultiplied, waiting to be uploaded (by path).
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
    if (ch == 4)  // premultiply
        for (size_t i = 0; i < d.im.rgba.size(); i += 4) {
            unsigned a = d.im.rgba[i + 3];
            for (int k = 0; k < 3; k++) d.im.rgba[i + k] = uint8_t((d.im.rgba[i + k] * a + 127) / 255);
        }
    return d;
}

uint8_t to8(float v) { return uint8_t(clamp01(v) * 255.0f + 0.5f); }

// The frame of the picture as RGBA8 in the same representation as its redraw (see PicKind).
Image originalPixels(const DopeExe& exe, const PictureDef& def, const PictureOptions& o) {
    Indexed ix = readIndexed(exe, def);
    int fx = def.fx, fy = def.fy, fw = def.fw > 0 ? def.fw : ix.w - fx, fh = def.fh > 0 ? def.fh : ix.h - fy;
    Image im;
    if (ix.px.empty() || fw <= 0 || fh <= 0) return im;
    const Palette& pal = o.palette ? *o.palette : ix.pal;
    im.w = fw;
    im.h = fh;
    im.rgba.resize(size_t(fw) * fh * 4);
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            int v = ix.at(fx + x, fy + y);
            uint8_t* px = &im.rgba[(size_t(y) * fw + x) * 4];
            if (v == o.key) { px[0] = px[1] = px[2] = px[3] = 0; continue; }
            Vec3 c;
            switch (def.kind) {
                case PicKind::Screen: c = pal[size_t((v + o.indexAdd) & 255)]; break;
                case PicKind::Mask: { float g = clamp01((v - 0x80) / 63.0f); c = Vec3(g, g, g); break; }
                case PicKind::Shade: { float g = clamp01(v / float(def.levels - 1)); c = Vec3(g, g, g); break; }
            }
            px[0] = to8(c.x);
            px[1] = to8(c.y);
            px[2] = to8(c.z);
            px[3] = 255;
        }
    return im;
}
}  // namespace

void PictureStore::init(const DopeExe* exe, std::string artDir, bool useArt) {
    exe_ = exe;
    artDir_ = std::move(artDir);
    useArt_ = useArt && !artDir_.empty();
    if (!useArt_) return;
    // decode every redraw of the catalog on worker threads now; get() uploads them
    std::lock_guard<std::mutex> g(g_pendingLock);
    for (const PictureDef& d : pictureCatalog()) {
        std::string p = redrawPath(d);
        if (!p.empty() && !g_pending.count(p)) g_pending[p] = std::async(std::launch::async, decode, p).share();
    }
}

std::string PictureStore::redrawPath(const PictureDef& def) const {
    if (artDir_.empty()) return "";
    for (const char* n : {def.name, def.alias})
        if (n)
            for (const char* ext : {".png", ".jpg"}) {
                std::string p = artDir_ + "/" + n + ext;
                if (exists(p)) return p;
            }
    return "";
}

const Picture& PictureStore::get(const std::string& name, const PictureOptions& o) {
    const PictureDef* d = findPicture(name);
    if (!d) {
        static Picture none;
        SDL_Log("picture: %s is not in the catalog", name.c_str());
        return none;
    }
    return get(*d, o);
}

const Picture& PictureStore::get(const PictureDef& def, const PictureOptions& o) {
    std::string path = useArt_ ? redrawPath(def) : "";
    uint64_t h = fnv(0xcbf29ce484222325ull, def.name, std::strlen(def.name));
    if (path.empty()) {  // the options only matter for the original's pixels
        h = fnv(h, &o.key, sizeof o.key);
        h = fnv(h, &o.indexAdd, sizeof o.indexAdd);
        if (o.palette) h = fnv(h, o.palette->data(), sizeof(Palette));
    }
    auto it = cache_.find(h);
    if (it != cache_.end()) return *it->second;

    auto pic = std::make_unique<Picture>();
    pic->name = def.name;
    pic->kind = def.kind;
    if (!path.empty()) {
        std::shared_future<Decoded> fut;
        {
            std::lock_guard<std::mutex> g(g_pendingLock);
            auto p = g_pending.find(path);
            if (p != g_pending.end()) {
                fut = p->second;
                g_pending.erase(p);  // the decoded pixels go with the last reference, after the upload
            }
        }
        const Decoded& dec = fut.valid() ? fut.get() : decode(path);
        if (dec.im.w > 0) {
            pic->tex.create(dec.im, false, true);
            pic->redraw = true;
        } else {
            SDL_Log("picture: cannot read %s (%s): using the original's pixels", path.c_str(), dec.err.c_str());
        }
    }
    Image orig = originalPixels(*exe_, def, o);  // also the frame size of a redraw (< 1 ms)
    pic->srcW = orig.w;
    pic->srcH = orig.h;
    if (!pic->redraw && orig.w > 0) pic->tex.create(orig, false, false);
    return *cache_.emplace(h, std::move(pic)).first->second;
}

void PictureStore::release() {
    for (auto& [h, p] : cache_) p->tex.destroy();
    cache_.clear();
    std::lock_guard<std::mutex> g(g_pendingLock);
    g_pending.clear();  // waits for running decodes
}
