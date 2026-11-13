// CLONE.EXE access: read the file, unpack it (unpack.cpp), check that it is the released intro, read by address.
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

uint64_t fnv1a(std::span<const uint8_t> d) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : d) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

// The released CLONE.EXE (63,994 bytes, Acme & Fudge, Takeover '98): the unpacked image relocated for 0x400000 =
// clone/re/unpacked/clone_img.bin (NOTES.md "Files and tools"), 2,570 dword fixups, BSS 0xb6d4cb bytes.
constexpr uint32_t kImageSize = 0x2846c;
constexpr uint64_t kImageHash = 0x4adf7830170c1b95ull;
constexpr int kFixups = 2570;
}  // namespace

bool CloneExe::load(const std::string& path, std::string* err) {
    std::vector<uint8_t> exe;
    if (!readAll(path, exe)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (!unpackClone(exe, kBase, u_, err)) return false;
    if (u_.image.size() != kImageSize || fnv1a(image()) != kImageHash || u_.fixups != kFixups) {
        char b[200];
        std::snprintf(b, sizeof b, "unexpected CLONE.EXE contents (program image %zu bytes, hash %016llx, %d fixups)",
                      u_.image.size(), (unsigned long long)fnv1a(image()), u_.fixups);
        if (err) *err = b;
        return false;
    }
    return true;
}

const uint8_t* CloneExe::at(uint32_t a, size_t n) const {
    if (a < kBase || size_t(a - kBase) + n > u_.image.size()) throw std::out_of_range("CLONE.EXE address out of range");
    return u_.image.data() + (a - kBase);
}

std::string CloneExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}
