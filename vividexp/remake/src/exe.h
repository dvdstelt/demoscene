// VIVID.EXE: unpacks the release and gives access to the program image by linear address and to the demo's files by
// name. Every table, parameter, picture, palette, script and object the remake uses is read from here, at the
// addresses and names in vividexp/re/notes-*.md; the modules are read from EXPRMNT1.XM and EXPRMNT2.XM. Nothing from
// the originals is copied into the remake's source.
//
// VIVID.EXE is PMODE/W's own compressed format "PMW1" (unpack.cpp; vividexp/re/NOTES.md "Unpacking"): three objects,
// compressed with the extender's LZ decompressor, and a compressed fixup table. The image is loaded as the headless
// run loads it: object 1 (code, then the file archives) at linear 0x400000, object 2 at 0x810000, object 3 (data,
// then BSS and stack) at 0x820000, so address A is image byte A - 0x400000.
//
// The files are in three archives linked into object 1 (NOTES.md "The file archives"): a table of 128 entries of 40
// bytes {char name[32]; uint32 size; uint32 offset from the table}, ended by "eof.eof", then the files. The program
// finds the tables through the routine 0x42b25c (immediates at 0x42b264, 0x42b26b, 0x42b273; the end at 0x42b299) and
// looks a name up in all three, the last archive that has it winning (0x40241c).
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// The unpacked program (unpack.cpp).
struct VividUnpacked {
    std::vector<uint8_t> image;  // objects 1 and 2 at their bases, object 3's initialised data, zero between them
    uint32_t entry = 0;          // linear entry point (0x410f5c)
    uint32_t stack = 0;          // linear initial ESP (0x847fc0)
    int fixups = 0;              // 32-bit offset fixups applied
    int selectors = 0;           // 16-bit selector fixups
};
// Unpacks VIVID.EXE with its objects at bases[0..2]. False with a message on a malformed file.
bool unpackVivid(const std::vector<uint8_t>& exe, const uint32_t (&bases)[3], VividUnpacked& out, std::string* err);
// PMODE/W's LZ decompressor (the extender's routine at kernel offset 0x1e1c); returns the input bytes used, or 0.
size_t pmwDecompress(std::span<const uint8_t> in, std::vector<uint8_t>& out);

class VividExe {
public:
    static constexpr uint32_t kBase = 0x400000;   // object 1 (code and archives)
    static constexpr uint32_t kObj2 = 0x810000;
    static constexpr uint32_t kData = 0x820000;   // object 3 (data)

    // Reads and unpacks VIVID.EXE, checks the image against the released demo (size and hash) and reads the archives.
    bool load(const std::string& exePath, std::string* err);

    std::span<const uint8_t> image() const { return {u_.image.data(), u_.image.size()}; }
    uint32_t end() const { return kBase + uint32_t(u_.image.size()); }  // 0x825c3f: the first BSS address

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

    // The demo's files (0x40241c): the last archive that has the name wins. Names as the scripts write them
    // ("pics/taivas.pcx"); case and '\' are not changed, as the program does not change them.
    bool has(const std::string& name) const;
    std::span<const uint8_t> file(const std::string& name) const;   // throws std::out_of_range if there is none
    uint32_t address(const std::string& name) const;                // the file's linear address
    struct FileEntry { std::string name; uint32_t address, size; int archive; };
    const std::vector<FileEntry>& files() const { return files_; }

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    const FileEntry* find(const std::string& name) const;
    VividUnpacked u_;
    std::vector<FileEntry> files_;
};

std::vector<uint8_t> readFile(const std::string& path, bool* ok = nullptr);
uint64_t fnv1a64(std::span<const uint8_t> d);
