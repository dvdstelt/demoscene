// VIVID.EXE access: read the file, unpack it (unpack.cpp), check that it is the released demo's, read by address and
// read the archives' files by name (vividexp/re/NOTES.md "The file archives").
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
// The released VIVID.EXE (1,874,784 bytes, Doomsday, Assembly 1996): objects 1 and 2 and object 3's data =
// vividexp/re/unpacked/vivid_img.bin up to 0x825c3f (NOTES.md "Unpacking"), 8,154 offset fixups.
constexpr uint32_t kImageSize = 0x425c3f;
constexpr uint64_t kImageHash = 0x3dfb988f47cfed1eull;
constexpr int kFixups = 8154;
// 0x42b25c stores the archives' table addresses from these instructions' immediates, and their sizes from the next
// table (the last one's end at 0x42b299).
constexpr uint32_t kTableImm[3] = {0x42b264, 0x42b26b, 0x42b273};
constexpr uint32_t kEndImm = 0x42b299;
constexpr int kEntries = 128, kEntrySize = 40;
}  // namespace

bool VividExe::load(const std::string& path, std::string* err) {
    bool ok = false;
    std::vector<uint8_t> exe = readFile(path, &ok);
    if (!ok) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    const uint32_t bases[3] = {kBase, kObj2, kData};
    if (!unpackVivid(exe, bases, u_, err)) return false;
    if (u_.image.size() != kImageSize || fnv1a64(image()) != kImageHash || u_.fixups != kFixups) {
        char b[200];
        std::snprintf(b, sizeof b, "unexpected VIVID.EXE contents (program image %zu bytes, hash %016llx, %d fixups)",
                      u_.image.size(), (unsigned long long)fnv1a64(image()), u_.fixups);
        if (err) *err = b;
        return false;
    }
    files_.clear();
    for (int k = 0; k < 3; ++k) {
        const uint32_t table = u32(kTableImm[k]);
        const uint32_t end = k < 2 ? u32(kTableImm[k + 1]) : u32(kEndImm);
        for (int i = 0; i < kEntries; ++i) {
            const uint32_t e = table + uint32_t(i * kEntrySize);
            const char* name = reinterpret_cast<const char*>(at(e, kEntrySize));
            std::string n(name, strnlen(name, 32));
            if (n.empty() || n == "eof.eof") break;
            FileEntry f{n, table + u32(e + 36), u32(e + 32), k};
            if (f.address < table || f.address + f.size > end) {
                if (err) *err = "VIVID.EXE: archive entry " + n + " outside its archive";
                return false;
            }
            files_.push_back(f);
        }
    }
    return true;
}

const uint8_t* VividExe::at(uint32_t a, size_t n) const {
    if (a < kBase || size_t(a - kBase) + n > u_.image.size()) throw std::out_of_range("VIVID.EXE address out of range");
    return u_.image.data() + (a - kBase);
}

std::string VividExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}

const VividExe::FileEntry* VividExe::find(const std::string& name) const {
    const FileEntry* hit = nullptr;
    for (const FileEntry& f : files_)
        if (f.name == name) hit = &f;   // the last archive wins
    return hit;
}

bool VividExe::has(const std::string& name) const { return find(name) != nullptr; }

std::span<const uint8_t> VividExe::file(const std::string& name) const {
    const FileEntry* f = find(name);
    if (!f) throw std::out_of_range("VIVID.EXE has no file " + name);
    return span(f->address, f->size);
}

uint32_t VividExe::address(const std::string& name) const {
    const FileEntry* f = find(name);
    if (!f) throw std::out_of_range("VIVID.EXE has no file " + name);
    return f->address;
}
