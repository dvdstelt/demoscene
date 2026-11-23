// INSIDE.EXE and INSIDE.DAT: unpacks the release and gives access to the program image by linear address and to the
// data files by name. All of the demo's static data (the script table, the parts' parameters, the music player's
// tables) is read from the program image at the addresses used in inside/re/notes-*.md, and every picture, palette,
// shade table and mesh from INSIDE.DAT, by the name the program asks for. Nothing from the originals is copied into
// the remake's source.
//
// INSIDE.EXE is PMODE/W's own compressed format "PMW1" (unpack.cpp; inside/re/NOTES.md "Unpacking"): two objects,
// compressed with the extender's LZ decompressor, and a compressed fixup table. The image is loaded as the headless
// run loads it: object 1 (code) at linear 0x400000, object 2 (data, then BSS and stack) at 0x420000, so address A is
// image byte A - 0x400000.
// INSIDE.DAT is one LZSS-compressed block with a directory at its end (datfile.cpp; NOTES.md "INSIDE.DAT").
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

// The unpacked program (unpack.cpp).
struct InsideUnpacked {
    std::vector<uint8_t> image;  // object 1 from kBase, zero up to object 2, object 2's initialised data
    uint32_t entry = 0;          // linear entry point (0x40aa48)
    uint32_t stack = 0;          // linear initial ESP (0x4ba6a0)
    int fixups = 0;              // 32-bit offset fixups applied
    int selectors = 0;           // 16-bit selector fixups (written as 0x10, the flat data selector)
};
// Unpacks INSIDE.EXE with object 1 at base1 and object 2 at base2. False with a message on a malformed file.
bool unpackInside(const std::vector<uint8_t>& exe, uint32_t base1, uint32_t base2, InsideUnpacked& out,
                  std::string* err);
// PMODE/W's LZ decompressor (the extender's routine at kernel offset 0x1e1c); returns the input bytes used, or 0.
size_t pmwDecompress(std::span<const uint8_t> in, std::vector<uint8_t>& out);

class InsideExe {
public:
    static constexpr uint32_t kBase = 0x400000;   // object 1 (code)
    static constexpr uint32_t kData = 0x420000;   // object 2 (data)

    // Reads and unpacks INSIDE.EXE and checks the image against the released demo (size and hash).
    bool load(const std::string& exePath, std::string* err);

    std::span<const uint8_t> image() const { return {u_.image.data(), u_.image.size()}; }
    uint32_t end() const { return kBase + uint32_t(u_.image.size()); }  // 0x423c45: the first BSS address

    // Linear address -> data. Throws std::out_of_range outside the image (the BSS holds no data in the file).
    const uint8_t* at(uint32_t a, size_t n = 1) const;
    uint8_t u8(uint32_t a) const { return *at(a); }
    int8_t s8(uint32_t a) const { return int8_t(*at(a)); }
    uint16_t u16(uint32_t a) const { return get<uint16_t>(a); }
    int16_t s16(uint32_t a) const { return get<int16_t>(a); }
    uint32_t u32(uint32_t a) const { return get<uint32_t>(a); }
    int32_t s32(uint32_t a) const { return get<int32_t>(a); }
    float f32(uint32_t a) const { return get<float>(a); }
    double f64(uint32_t a) const { return get<double>(a); }
    std::string cstr(uint32_t a) const;
    std::vector<uint8_t> bytes(uint32_t a, size_t n) const { const uint8_t* p = at(a, n); return {p, p + n}; }
    std::span<const uint8_t> span(uint32_t a, size_t n) const { return {at(a, n), n}; }

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    InsideUnpacked u_;
};

// INSIDE.DAT (datfile.cpp): the program's loader 0x4000e8 (dword unpacked size, then LZSS 0x4011d6), the directory
// at the end of the unpacked block (0x4001c4: dword count, 0x30-byte entries: name, offset at +0x24) and the lookup
// 0x40022c (the first entry with that exact name; '\' becomes '/').
class InsideDat {
public:
    bool load(const std::string& path, std::string* err);
    bool has(const std::string& name) const;
    // The file's bytes: from its offset to the next file's offset (the directory stores no sizes). Throws
    // std::out_of_range if there is no such file.
    std::span<const uint8_t> file(const std::string& name) const;
    std::span<const uint8_t> all() const { return {data_.data(), data_.size()}; }
    // Offset of a file in the unpacked block (the program's pointers into "whole data buffer").
    uint32_t offset(const std::string& name) const;

private:
    std::vector<uint8_t> data_;
    struct Entry { std::string name; uint32_t offset, size; };
    std::vector<Entry> dir_;
};

std::vector<uint8_t> readFile(const std::string& path, bool* ok = nullptr);
uint64_t fnv1a64(std::span<const uint8_t> d);
