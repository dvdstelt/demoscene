// KORSO2.EXE (exe.h): reads the file, finds the RNC-packed load module of its one MZ executable, unpacks it (rnc.cpp)
// and checks the result against the released intro.
#include "exe.h"

#include <cstdio>
#include <stdexcept>

std::vector<uint8_t> readFile(const std::string& path, bool* ok) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (ok) *ok = false; return out; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool good = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (ok) *ok = good;
    return out;
}

namespace {
uint64_t fnv1a(const std::vector<uint8_t>& d) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : d) h = (h ^ b) * 0x100000001b3ull;
    return h;
}
// okean/re/unpacked/korso2.bin
constexpr uint32_t kSize = 345399;
constexpr uint64_t kHash = 0x713355ab60e3c5c2ull;
}  // namespace

bool OkeanExe::load(const std::string& exePath, std::string* err) {
    bool ok = false;
    std::vector<uint8_t> exe = readFile(exePath, &ok);
    char b[200];
    if (!ok) {
        if (err) *err = "cannot read " + exePath;
        return false;
    }
    if (exe.size() < 0x20 || exe[0] != 'M' || exe[1] != 'Z') {
        if (err) *err = "KORSO2.EXE: not an MZ executable";
        return false;
    }
    // MZ size: pages * 512, minus (512 - last) when the last-page count is not 0; the module after the header
    auto le16 = [&](size_t p) { return uint16_t(exe[p] | exe[p + 1] << 8); };
    uint16_t last = le16(2), pages = le16(4), hdr = le16(8);
    size_t size = size_t(pages) * 512 - (last ? 512 - last : 0);
    size_t mod = size_t(hdr) * 16;
    if (size > exe.size() || mod >= size) {
        if (err) *err = "KORSO2.EXE is truncated";
        return false;
    }
    std::string e;
    if (!unpackRnc(exe.data() + mod, size - mod, img_, &e)) {
        if (err) *err = "KORSO2.EXE: " + e;
        return false;
    }
    if (img_.size() != kSize || fnv1a(img_) != kHash) {
        std::snprintf(b, sizeof b, "unexpected KORSO2.EXE contents (unpacked %zu bytes, hash %016llx)", img_.size(),
                      (unsigned long long)fnv1a(img_));
        if (err) *err = b;
        return false;
    }
    return true;
}

const uint8_t* OkeanExe::at(uint32_t a, size_t n) const {
    size_t f = size_t(kCode32) + a;
    if (f + n > img_.size()) throw std::out_of_range("KORSO2.EXE: code32 address out of range");
    return img_.data() + f;
}

std::string OkeanExe::cstr(uint32_t a, size_t max) const {
    std::string s;
    for (size_t i = 0; i < max; i++) {
        char c = char(u8(a + uint32_t(i)));
        if (!c) break;
        s += c;
    }
    return s;
}
