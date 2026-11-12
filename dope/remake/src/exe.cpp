// DOPE.EXE and DOPE.MOD access (exe.h): read the files, unpack the chain (rnc.cpp), check that it is the released
// demo, read by address.
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

// Size and FNV-1a hash of every unpacked image (= dope/re/unpacked/<name>.bin) and of DOPE.MOD.
struct Known { const char* name; uint32_t size; uint64_t hash; };
constexpr Known kImages[kDopeImages] = {
    {"loader", 0x63e2, 0x67673412c9da97daull}, {"p1", 0x3429a, 0x96e34d0aab7a1692ull},
    {"p2", 0x6dd46, 0xcb7b87d0e3b91160ull},    {"p3", 0x5b51a, 0xd30821bb6ff35a83ull},
    {"p4", 0x766d0, 0x592b5bf83ac89da0ull},    {"p5", 0x5e71a, 0xdeacb957603bd9b1ull},
    {"p6", 0x6d060, 0x4da1c5329c1a6986ull},    {"p7", 0x6fb4e, 0x15886546b081ad66ull},
    {"term", 0x15, 0x1545a28afff32014ull},
};
constexpr Known kMod{"DOPE.MOD", 957334, 0xeed999ce65322775ull};

bool fileExists(const std::string& p) {
    if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

// DOPE.MOD next to DOPE.EXE (the loader opens "dope.mod" in the current directory).
std::string modNextTo(const std::string& exePath) {
    size_t s = exePath.find_last_of("/\\");
    std::string dir = s == std::string::npos ? "" : exePath.substr(0, s + 1);
    for (const char* n : {"DOPE.MOD", "dope.mod", "Dope.mod", "DOPE.mod"})
        if (fileExists(dir + n)) return dir + n;
    return dir + "DOPE.MOD";
}
}  // namespace

const uint8_t* DopeImage::at(uint32_t a, size_t n) const {
    if (size_t(a) + n > data.size()) throw std::out_of_range(name + ": address out of range");
    return data.data() + a;
}

std::string DopeImage::cstr(uint16_t seg, uint16_t off) const {
    std::string s;
    for (uint32_t a = addr(seg, off); *at(a); a++) s.push_back(char(*at(a)));
    return s;
}

std::string DopeImage::dstr(uint16_t seg, uint16_t off) const {
    std::string s;
    for (uint32_t a = addr(seg, off); *at(a) != '$'; a++) s.push_back(char(*at(a)));
    return s;
}

const DopeImage& DopeExe::part(int n) const {
    if (n < 1 || n > 7) throw std::out_of_range("DOPE.EXE has no part " + std::to_string(n));
    return img_[size_t(n)];
}

const DopeImage& DopeExe::image(int i) const {
    if (i < 0 || i >= kDopeImages) throw std::out_of_range("DOPE.EXE has no image " + std::to_string(i));
    return img_[size_t(i)];
}

bool DopeExe::check(std::string* err) const {
    char b[200];
    for (int i = 0; i < kDopeImages; i++) {
        const auto& d = img_[size_t(i)].data;
        if (d.size() != kImages[i].size || fnv1a(d) != kImages[i].hash) {
            std::snprintf(b, sizeof b, "unexpected DOPE.EXE contents (%s: %zu bytes, hash %016llx)", kImages[i].name,
                          d.size(), (unsigned long long)fnv1a(d));
            if (err) *err = b;
            return false;
        }
    }
    if (mod_.size() != kMod.size || fnv1a(mod_) != kMod.hash) {
        std::snprintf(b, sizeof b, "unexpected DOPE.MOD (%zu bytes, hash %016llx)", mod_.size(),
                      (unsigned long long)fnv1a(mod_));
        if (err) *err = b;
        return false;
    }
    return true;
}

bool DopeExe::load(const std::string& exePath, const std::string& modPath, std::string* err) {
    std::vector<uint8_t> exe;
    if (!readAll(exePath, exe)) {
        if (err) *err = "cannot read " + exePath;
        return false;
    }
    std::array<std::vector<uint8_t>, kDopeImages> raw;
    if (!unpackDope(exe, raw, err)) return false;
    for (int i = 0; i < kDopeImages; i++) {
        img_[size_t(i)].name = kImages[i].name;
        img_[size_t(i)].data = std::move(raw[size_t(i)]);
    }
    modPath_ = modPath.empty() ? modNextTo(exePath) : modPath;
    if (!readAll(modPath_, mod_)) {
        if (err) *err = "cannot read " + modPath_ + " (DOPE.MOD goes next to DOPE.EXE)";
        return false;
    }
    return check(err);
}
