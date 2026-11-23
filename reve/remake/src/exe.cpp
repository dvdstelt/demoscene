// REVE.EXE access: read the file, check that it is the released demo, unpack the archive (unpack.cpp), relocate the
// demo's image, read by address and by name.
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
// The released REVE.EXE ("final version", May 1996): 999,999 bytes.
constexpr size_t kExeSize = 999999;
constexpr uint64_t kExeHash = 0x5128e7d16f3309b2ull;   // FNV-1a 64 of the whole file
constexpr size_t kFiles = 36;   // REVE.LFL and 35 data files
constexpr uint32_t kImageSize = 0x6d9c0;
constexpr int kRelocs = 2937;
}  // namespace

bool ReveExe::load(const std::string& path, std::string* err) {
    bool ok = false;
    std::vector<uint8_t> exe = readFile(path, &ok);
    if (!ok) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (exe.size() != kExeSize || fnv1a64(exe) != kExeHash) {
        char b[160];
        std::snprintf(b, sizeof b, "unexpected REVE.EXE (%zu bytes, hash %016llx): the remake needs the final version",
                      exe.size(), (unsigned long long)fnv1a64(exe));
        if (err) *err = b;
        return false;
    }
    std::vector<ReveFile> files;
    if (!unpackArchive(exe, files, err)) return false;
    if (files.size() != kFiles || files[0].name != "REVE.LFL") {
        if (err) *err = "unexpected archive in REVE.EXE";
        return false;
    }
    int relocs = 0;
    if (!relocateLfl(files[0].data, kBase, image_, &relocs, err)) return false;
    if (image_.size() != kImageSize || relocs != kRelocs) {
        if (err) *err = "unexpected REVE.LFL";
        return false;
    }
    files_.clear();
    for (ReveFile& f : files) files_[f.name] = std::move(f.data);
    return true;
}

std::span<const uint8_t> ReveExe::file(const std::string& name) const {
    auto it = files_.find(name);
    if (it == files_.end()) throw std::out_of_range("REVE.EXE has no file " + name);
    return {it->second.data(), it->second.size()};
}

const uint8_t* ReveExe::at(uint32_t a, size_t n) const {
    if (a < kBase || size_t(a - kBase) + n > image_.size()) throw std::out_of_range("REVE.LFL address out of range");
    return image_.data() + (a - kBase);
}

std::string ReveExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}
