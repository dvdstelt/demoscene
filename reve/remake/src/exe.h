// REVE.EXE: unpacks the release and gives access to the demo's program image by address and to the files it carries
// by name. All of the demo's static data (the script table, the parts' parameters, the music player's tables) is read
// from the program image at the addresses used in reve/re/notes-*.md, and every picture, palette, table, 3D world and
// the module from the archive, by the name the program asks for. Nothing from the original is copied into the
// remake's source.
//
// REVE.EXE (reve/re/NOTES.md "File layout") is a small real-mode loader followed by "LCA's Packer" archive: 36
// LZW-compressed files (unpack.cpp). The first, REVE.LFL, is the demo: its image is relocated as LCA's FLAT OS loads
// it in the headless run, to 0x20427c, so address A of the notes is image byte A - 0x20427c.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

// One file of the archive (unpack.cpp).
struct ReveFile {
    std::string name;            // as stored and as the program asks for it, e.g. "GFX\TEXTURE.RAW"
    uint32_t offset = 0;         // of its record in REVE.EXE
    uint32_t checksum = 0;       // the record's checksum (ror32(s + byte, 3) over the unpacked bytes)
    std::vector<uint8_t> data;   // unpacked
};
// Parses the archive at the end of REVE.EXE and unpacks every file (checking size and checksum). False with a message
// on a malformed file.
bool unpackArchive(std::span<const uint8_t> exe, std::vector<ReveFile>& files, std::string* err);
// LCA's LZW (the extender's routine at kernel offset 0x9284): 9..13-bit codes, 256 = end, 257 = reset. Returns false
// if the stream ends early or a code is invalid.
bool lzwDecode(std::span<const uint8_t> in, size_t size, std::vector<uint8_t>& out);
uint32_t lcaChecksum(std::span<const uint8_t> d);
// REVE.LFL: the 0x1c-byte header, the image, "LCAr" and the relocations; the image relocated to `base`.
bool relocateLfl(std::span<const uint8_t> lfl, uint32_t base, std::vector<uint8_t>& image, int* relocs,
                 std::string* err);

class ReveExe {
public:
    static constexpr uint32_t kBase = 0x20427c;   // the image's address in the headless run (reve/re/rep.py)

    // Reads and unpacks REVE.EXE and checks it against the released demo (size and hash).
    bool load(const std::string& exePath, std::string* err);

    // ---- the files of the archive
    bool has(const std::string& name) const { return files_.count(name) != 0; }
    // The unpacked file. Throws std::out_of_range if there is no such file.
    std::span<const uint8_t> file(const std::string& name) const;

    // ---- the program image (REVE.LFL relocated to kBase)
    std::span<const uint8_t> image() const { return {image_.data(), image_.size()}; }
    uint32_t end() const { return kBase + uint32_t(image_.size()); }
    // Address -> data. Throws std::out_of_range outside the image (buffers the demo allocates are not in it).
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
    std::map<std::string, std::vector<uint8_t>> files_;
    std::vector<uint8_t> image_;
};

std::vector<uint8_t> readFile(const std::string& path, bool* ok = nullptr);
uint64_t fnv1a64(std::span<const uint8_t> d);
