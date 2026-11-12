// The art folder (art.h): lists the redrawn pictures and restored video frames and decodes them with stb_image on
// worker threads (all pictures at start-up; video frames on demand, two ahead of the frame shown).
#include "art.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <utility>

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

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}
// 0: not a picture, 1: JPEG, 2: PNG (preferred when both exist)
int imageKind(const fs::path& p) {
    std::string e = lower(p.extension().string());
    if (e == ".png") return 2;
    if (e == ".jpg" || e == ".jpeg") return 1;
    return 0;
}
bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}
}  // namespace

ArtStore::Decoded ArtStore::decode(const std::string& path) {
    Decoded d;
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px) {
        d.err = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        return d;
    }
    d.im.w = w;
    d.im.h = h;
    d.im.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return d;
}

void ArtStore::init(const std::string& dir, bool on) {
    release();
    dir_ = dir;
    on_ = on && !dir.empty();
    if (!on_) return;
    std::error_code ec;
    std::map<std::string, int> kinds;
    for (const auto& e : fs::directory_iterator(fs::path(dir), ec)) {
        if (!e.is_regular_file(ec)) continue;
        int k = imageKind(e.path());
        std::string name = lower(e.path().stem().string());
        if (k == 0 || kinds[name] >= k) continue;
        kinds[name] = k;
        picturePaths_[name] = e.path().string();
    }
    for (const auto& [name, path] : picturePaths_) picturePending_[name] = std::async(std::launch::async, decode, path);

    for (const auto& e : fs::directory_iterator(fs::path(dir) / "video", ec)) {
        if (!e.is_directory(ec)) continue;
        Clip& c = clips_[upper(e.path().filename().string())];
        std::map<int, int> fk;
        for (const auto& f : fs::directory_iterator(e.path(), ec)) {
            int k = imageKind(f.path());
            std::string stem = f.path().stem().string();
            if (k == 0 || !f.is_regular_file(ec) || !allDigits(stem) || stem.size() > 6) continue;
            int n = std::stoi(stem);
            if (fk[n] >= k) continue;
            fk[n] = k;
            c.files[n] = f.path().string();
        }
        prefetch(c, 0);  // the first frames, so that a clip does not stall when it starts
    }
    std::string clips;
    for (const auto& [name, c] : clips_)
        if (!c.files.empty()) clips += " " + name + " (" + std::to_string(c.files.size()) + ")";
    SDL_Log("art: %s: %d pictures, restored video frames:%s", dir.c_str(), int(picturePaths_.size()),
            clips.empty() ? " none" : clips.c_str());
}

bool ArtStore::hasPicture(const std::string& name) const { return on_ && picturePaths_.count(lower(name)) != 0; }

const Image* ArtStore::picture(const std::string& name) {
    if (!on_) return nullptr;
    const std::string key = lower(name);
    auto it = pictures_.find(key);
    if (it != pictures_.end()) return it->second.get();
    auto p = picturePending_.find(key);
    if (p == picturePending_.end()) return nullptr;
    Decoded d = p->second.get();
    picturePending_.erase(p);
    std::unique_ptr<Image> im;
    if (d.im.w > 0) im = std::make_unique<Image>(std::move(d.im));
    else SDL_Log("art: cannot read %s (%s): using the original's pixels", picturePaths_[key].c_str(), d.err.c_str());
    return (pictures_[key] = std::move(im)).get();
}

int ArtStore::videoFiles(const std::string& clip) const {
    if (!on_) return 0;
    auto it = clips_.find(upper(clip));
    return it == clips_.end() ? 0 : int(it->second.files.size());
}

void ArtStore::prefetch(Clip& c, int frame) {
    // decode frame .. frame + 2 (not the one already shown), drop the others (a seek); dropping a future waits for
    // its decode to finish
    for (auto it = c.pending.begin(); it != c.pending.end();)
        it = (it->first < frame || it->first > frame + 2) ? c.pending.erase(it) : std::next(it);
    for (int f = frame; f <= frame + 2; f++) {
        auto file = c.files.find(f);
        if (file != c.files.end() && f != c.current && !c.pending.count(f))
            c.pending[f] = std::async(std::launch::async, decode, file->second);
    }
}

const Image* ArtStore::videoFrame(const std::string& clip, int frame) {
    if (!on_) return nullptr;
    auto ci = clips_.find(upper(clip));
    if (ci == clips_.end()) return nullptr;
    Clip& c = ci->second;
    if (!c.files.count(frame)) return nullptr;
    if (c.current != frame) {
        prefetch(c, frame);
        Decoded d = c.pending[frame].get();
        c.pending.erase(frame);
        if (d.im.w <= 0) {
            SDL_Log("art: cannot read %s (%s): using the decoded frame", c.files[frame].c_str(), d.err.c_str());
            c.files.erase(frame);
            return nullptr;
        }
        c.current = frame;
        c.currentImage = std::move(d.im);
        prefetch(c, frame);  // the two frames after it
    }
    return &c.currentImage;
}

void ArtStore::release() {
    picturePending_.clear();  // waits for running decodes
    pictures_.clear();
    picturePaths_.clear();
    clips_.clear();
}
