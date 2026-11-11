// JIZZ.EXE access: load, unpack (adam.cpp), check that it is the released file, read by address.
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

bool JizzExe::check(std::string* err) const {
    // The unpacked image of the released JIZZ.EXE (63,960 bytes, Wired '97).
    if (img_.size() == 0x33000 && fnv1a(img_) == 0x043f5935c9099e39ull) return true;
    if (err) {
        char b[160];
        std::snprintf(b, sizeof b, "unexpected JIZZ.EXE contents (unpacked %zu bytes, hash %016llx)", img_.size(),
                      (unsigned long long)fnv1a(img_));
        *err = b;
    }
    return false;
}

bool JizzExe::load(const std::string& path, std::string* err) {
    std::vector<uint8_t> exe;
    if (!readAll(path, exe)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    uint32_t entry = 0;
    if (!unadam(exe, img_, &entry, err)) return false;
    return check(err);
}

bool JizzExe::loadImage(const std::string& path, std::string* err) {
    if (!readAll(path, img_)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    return check(err);
}

const uint8_t* JizzExe::at(uint32_t a, size_t n) const {
    if (size_t(a) + n > img_.size()) throw std::out_of_range("JIZZ.EXE address out of range");
    return img_.data() + a;
}

std::string JizzExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}
