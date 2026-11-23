// INSIDE.EXE and INSIDE.DAT access: read the files, unpack them (unpack.cpp, below for the DAT), check that they are
// the released demo's, read by address and by name.
#include "exe.h"

#include <cstdio>
#include <stdexcept>

std::vector<uint8_t> readFile(const std::string& path, bool* ok) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (ok) *ok = false;
        return out;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool good = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (ok) *ok = good;
    return out;
}

uint64_t fnv1a64(std::span<const uint8_t> d) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : d) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

namespace {
// The released INSIDE.EXE (113,134 bytes, CNCD, The Gathering 1996): object 1 at 0x400000 and object 2's data at
// 0x420000 = inside/re/unpacked/inside_img.bin up to 0x423c45 (NOTES.md "Unpacking"), 4,515 offset fixups.
constexpr uint32_t kImageSize = 0x23c45;
constexpr uint64_t kImageHash = 0x717804c4954ab9f3ull;
constexpr int kFixups = 4515;
// INSIDE.DAT (1,349,919 bytes) unpacks to 2,721,868 bytes.
constexpr uint32_t kDatSize = 2721868;
constexpr uint64_t kDatHash = 0x08ee50e10b8d127eull;
}  // namespace

bool InsideExe::load(const std::string& path, std::string* err) {
    bool ok = false;
    std::vector<uint8_t> exe = readFile(path, &ok);
    if (!ok) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (!unpackInside(exe, kBase, kData, u_, err)) return false;
    if (u_.image.size() != kImageSize || fnv1a64(image()) != kImageHash || u_.fixups != kFixups) {
        char b[200];
        std::snprintf(b, sizeof b, "unexpected INSIDE.EXE contents (program image %zu bytes, hash %016llx, %d fixups)",
                      u_.image.size(), (unsigned long long)fnv1a64(image()), u_.fixups);
        if (err) *err = b;
        return false;
    }
    return true;
}

const uint8_t* InsideExe::at(uint32_t a, size_t n) const {
    if (a < kBase || size_t(a - kBase) + n > u_.image.size()) throw std::out_of_range("INSIDE.EXE address out of range");
    return u_.image.data() + (a - kBase);
}

std::string InsideExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}

bool InsideDat::load(const std::string& path, std::string* err) {
    bool ok = false;
    std::vector<uint8_t> raw = readFile(path, &ok);
    if (!ok || raw.size() < 8) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    const uint32_t size = raw[0] | raw[1] << 8 | raw[2] << 16 | uint32_t(raw[3]) << 24;
    if (size != kDatSize) {
        if (err) *err = "unexpected INSIDE.DAT (unpacked size " + std::to_string(size) + ")";
        return false;
    }
    // LZSS (0x4011d6): a flag byte for 8 items, bit 0 first; 1 = a literal, 0 = two bytes b0, b1: distance
    // (b1 & 0xf0) << 4 | b0, length (b1 & 15) + 3, copied byte by byte.
    data_.assign(size, 0);
    size_t i = 4, o = 0;
    uint32_t flags = 0;
    while (o < size) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (i >= raw.size()) break;
            flags = raw[i++] | 0xff00;
        }
        if (flags & 1) {
            if (i >= raw.size()) break;
            data_[o++] = raw[i++];
        } else {
            if (i + 1 >= raw.size()) break;
            uint32_t b0 = raw[i], b1 = raw[i + 1];
            i += 2;
            uint32_t dist = (b1 & 0xf0) << 4 | b0, len = (b1 & 15) + 3;
            if (dist == 0 || dist > o) break;
            for (uint32_t k = 0; k < len && o < size; ++k, ++o) data_[o] = data_[o - dist];
        }
    }
    if (o != size || fnv1a64(all()) != kDatHash) {
        if (err) *err = "unexpected INSIDE.DAT contents";
        return false;
    }
    const uint32_t n = data_[size - 4] | data_[size - 3] << 8 | data_[size - 2] << 16 | uint32_t(data_[size - 1]) << 24;
    const uint32_t d0 = size - 4 - n * 0x30;
    std::vector<uint32_t> offs;
    for (uint32_t k = 0; k < n; ++k) {
        const uint8_t* e = &data_[d0 + k * 0x30];
        std::string name(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 0x24));
        uint32_t off = e[0x24] | e[0x25] << 8 | e[0x26] << 16 | uint32_t(e[0x27]) << 24;
        dir_.push_back({name, off, 0});
        offs.push_back(off);
    }
    offs.push_back(d0);
    for (Entry& e : dir_) {
        uint32_t next = d0;
        for (uint32_t x : offs)
            if (x > e.offset && x < next) next = x;
        e.size = next - e.offset;
    }
    return true;
}

bool InsideDat::has(const std::string& name) const {
    for (const Entry& e : dir_)
        if (e.name == name) return true;
    return false;
}

std::span<const uint8_t> InsideDat::file(const std::string& name) const {
    for (const Entry& e : dir_)
        if (e.name == name) return {data_.data() + e.offset, e.size};
    throw std::out_of_range("INSIDE.DAT has no " + name);
}

uint32_t InsideDat::offset(const std::string& name) const {
    for (const Entry& e : dir_)
        if (e.name == name) return e.offset;
    throw std::out_of_range("INSIDE.DAT has no " + name);
}
