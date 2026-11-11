#include "exe.h"

#include <cstdio>
#include <stdexcept>

namespace {
bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

uint64_t fnv1a(const std::vector<uint8_t>& d) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : d) h = (h ^ b) * 0x100000001b3ull;
    return h;
}
}  // namespace

bool PaperExe::check(std::string* err) const {
    // The unpacked image of the released PAPER.EXE (63845 bytes, 2 Nov 1996).
    if (img_.size() == 0x269e0 && fnv1a(img_) == 0xd181e907f22e171full) return true;
    if (err) {
        char b[160];
        std::snprintf(b, sizeof b, "unexpected PAPER.EXE contents (unpacked %zu bytes, hash %016llx)", img_.size(),
                      (unsigned long long)fnv1a(img_));
        *err = b;
    }
    return false;
}

bool PaperExe::load(const std::string& path, std::string* err) {
    std::vector<uint8_t> exe;
    if (!readAll(path, exe)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (!unpklite(exe, img_, err)) return false;
    return check(err);
}

bool PaperExe::loadImage(const std::string& path, std::string* err) {
    if (!readAll(path, img_)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    return check(err);
}

const uint8_t* PaperExe::at(uint32_t a, size_t n) const {
    size_t o = size_t(a) + kCode32;
    if (o + n > img_.size()) throw std::out_of_range("code32 offset out of range");
    return img_.data() + o;
}

std::string PaperExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}
