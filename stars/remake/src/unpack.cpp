// Unpacker for STARS.EXE, written from stars/re/NOTES.md ("File layout") and notes-core.md section 9 (the
// description stars/re/models/core/unpack_spec.py was written from and checked against the original code run in an
// emulator, stars/re/unpack.py):
//   1. Infiny Loader: the stub at file offset 0x125a0 decrypts the PKLITE image (file 0xa0..0x125a0) word by word
//      (its loop at stub 0x1b3 / 0x258) and checks it with a checksum;
//   2. PKLITE 1.1x: the decompressor at image offset 0x44 (compressed data from 0x290), then the relocation groups;
//   3. the overlay: 58 files listed backwards from the end of the file (0x4a9f3), DIET-compressed when they carry the
//      "dlz" signature (0x4ac8b, decompressor 0x4ad47).
#include <cstdio>

#include "exe.h"

namespace {
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(rd16(p) | uint32_t(rd16(p + 2)) << 16); }

// The bit reader of PKLITE and DIET: 16-bit little-endian words, least significant bit first. The next word is
// fetched as soon as the 16th bit of the current one has been taken, so it comes before any byte read after that
// bit; the first word is fetched before the first bit. Reads past the end give zeros and set `over`.
class Bits {
public:
    Bits(const uint8_t* d, size_t n, size_t pos) : d_(d), n_(n), p_(pos) { w_ = word(); }
    uint32_t byte() {
        if (p_ >= n_) { over = true; p_++; return 0; }
        return d_[p_++];
    }
    uint32_t bit() {
        uint32_t b = w_ & 1;
        w_ >>= 1;
        if (--left_ == 0) {
            w_ = word();
            left_ = 16;
        }
        return b;
    }
    size_t pos() const { return p_; }
    bool over = false;

private:
    uint32_t word() { uint32_t lo = byte(); return lo | byte() << 8; }
    const uint8_t* d_;
    size_t n_, p_;
    uint32_t w_ = 0;
    int left_ = 16;
};

// Copies n bytes from `dist` bytes back, one at a time (overlapping copies repeat bytes). False if out of range.
bool copyBack(std::vector<uint8_t>& out, size_t dist, int n) {
    if (dist == 0 || dist > out.size()) return false;
    for (int i = 0; i < n; i++) out.push_back(out[out.size() - dist]);
    return true;
}

// ---------------------------------------------------------------- stage 1: Infiny Loader
constexpr size_t kPkStart = 0xa0, kPkEnd = 0x125a0;  // the encrypted PKLITE image (its MZ header removed)
constexpr uint16_t kPkChecksum = 0x0981;              // the value the stub compares with (stub offset 0x2d8)

std::vector<uint8_t> decrypt(const std::vector<uint8_t>& exe, uint16_t* checksum) {
    std::vector<uint8_t> img(exe.begin() + kPkStart, exe.begin() + kPkEnd);
    uint16_t ck = 0;
    for (size_t i = 0; i + 1 < img.size(); i += 2) {
        uint16_t w = rd16(&img[i]);
        w = uint16_t(((uint16_t(w - 0xbc67 - 0x305e + 0x9af9) ^ 0xc4f5) - 0x7b9b));
        img[i] = uint8_t(w);
        img[i + 1] = uint8_t(w >> 8);
        ck ^= w;
        ck = uint16_t(ck + (w & 0xff) * (w >> 8));
    }
    *checksum = ck;
    return img;
}

// ---------------------------------------------------------------- stage 2: PKLITE 1.1x
// Prefix codes (first bit read = first character).
struct Code { const char* bits; int value; };
constexpr int kSpecial = -1;  // `011100` + byte: 0xfe = no output, 0xff = end, else length 25 + byte
constexpr Code kLenCodes[] = {
    {"10", 2},          {"11", 3},          {"000", 4},         {"0010", 5},        {"0011", 6},
    {"0100", 7},        {"01010", 8},       {"01011", 9},       {"01100", 10},      {"011010", 11},
    {"011011", 12},     {"0111010", 13},    {"0111011", 14},    {"0111100", 15},    {"01111010", 16},
    {"01111011", 17},   {"01111100", 18},   {"011111010", 19},  {"011111011", 20},  {"011111100", 21},
    {"011111101", 22},  {"011111110", 23},  {"011111111", 24},  {"011100", kSpecial},
};
constexpr Code kOfsCodes[] = {
    {"1", 0}, {"0000", 1}, {"0001", 2}, {"00100", 3}, {"00101", 4}, {"00110", 5}, {"00111", 6},
    // 010000 .. 010110 = 7 .. 13 and the 7-bit codes 0101110 .. 0111111 = 14 .. 31 (code - 32) are added below
};

class PrefixTable {
public:
    template <size_t N> explicit PrefixTable(const Code (&codes)[N]) {
        for (auto& row : lut_) row.fill(kNone);
        for (const Code& c : codes) add(c.bits, c.value);
    }
    void addValue(int len, int code, int value) { lut_[len][code] = value; }
    // Reads one code; false on an invalid one.
    bool read(Bits& b, int* value) const {
        int v = 0;
        for (int len = 1; len < kMaxLen; len++) {
            v = v << 1 | int(b.bit());
            if (lut_[len][v] != kNone) { *value = lut_[len][v]; return true; }
        }
        return false;
    }

private:
    static constexpr int kMaxLen = 10, kNone = -1000;
    void add(const char* s, int value) {
        int v = 0, len = 0;
        for (; *s; s++, len++) v = v << 1 | (*s == '1');
        lut_[len][v] = value;
    }
    std::array<std::array<int, 1 << kMaxLen>, kMaxLen> lut_;
};

const PrefixTable& lengthCodes() {
    static const PrefixTable t(kLenCodes);
    return t;
}

const PrefixTable& offsetCodes() {
    static const PrefixTable t = [] {
        PrefixTable p(kOfsCodes);
        for (int v = 0; v <= 6; v++) p.addValue(6, 0x10 | v, 7 + v);              // 01 + 4 bits
        for (int c = 0x2e; c <= 0x3f; c++) p.addValue(7, c, c - 32);             // 0101110 .. 0111111
        return p;
    }();
    return t;
}

constexpr size_t kPkData = 0x290;      // compressed data in the decrypted image
constexpr uint16_t kLoadSegment = 0x1000;  // stars/re/unpacked/stars_img.bin is relocated for this segment

bool pklite(const std::vector<uint8_t>& img, std::vector<uint8_t>& out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = std::string("PKLITE: ") + m; return false; };
    out.clear();
    out.reserve(0x90000);
    Bits b(img.data(), img.size(), kPkData);
    for (;;) {
        if (b.over || out.size() > 0x100000) return fail("corrupt data");
        if (b.bit() == 0) {
            out.push_back(uint8_t(b.byte()));
            continue;
        }
        int n = 0;
        if (!lengthCodes().read(b, &n)) return fail("bad length code");
        if (n == kSpecial) {
            uint32_t x = b.byte();
            if (x == 0xfe) continue;  // segment normalisation
            if (x == 0xff) break;     // end of data
            n = int(25 + x);
        }
        int hi = 0;
        if (n != 2 && !offsetCodes().read(b, &hi)) return fail("bad offset code");
        if (!copyBack(out, size_t(hi) << 8 | b.byte(), n)) return fail("bad offset");
    }
    // relocation groups (count byte, segment word, count offset words; count 0 ends), then SS, SP, CS, IP
    size_t p = b.pos();
    auto need = [&](size_t k) { return p + k <= img.size(); };
    std::vector<uint32_t> relocs;
    for (;;) {
        if (!need(1)) return fail("truncated relocations");
        int count = img[p++];
        if (count == 0) break;
        if (!need(2 + 2 * size_t(count))) return fail("truncated relocations");
        uint32_t seg = rd16(&img[p]);
        p += 2;
        for (int i = 0; i < count; i++, p += 2) relocs.push_back(seg * 16 + rd16(&img[p]));
    }
    if (!need(8)) return fail("truncated header");
    uint32_t ss = rd16(&img[p]), sp = rd16(&img[p + 2]);
    uint32_t memory = ss * 16 + sp;  // the end of the stack: the program's memory
    if (memory < out.size() || memory > 0x100000) return fail("bad stack");
    for (uint32_t r : relocs) {
        if (size_t(r) + 2 > out.size()) return fail("bad relocation");
        uint16_t w = uint16_t(rd16(&out[r]) + kLoadSegment);
        out[r] = uint8_t(w);
        out[r + 1] = uint8_t(w >> 8);
    }
    out.resize(memory, 0);
    return true;
}

// ---------------------------------------------------------------- stage 3: the overlay and DIET
constexpr int kDirEntry = 12;   // dword n, int32 offset from the end of the file, dword stored size
constexpr size_t kDietHeader = 17;

bool diet(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    if (n < kDietHeader) return false;
    size_t size = size_t(src[14] >> 2) << 16 | rd16(src + 15);  // 0x4ac9a
    out.clear();
    out.reserve(size);
    Bits b(src, n, kDietHeader);
    for (;;) {
        if (b.over || out.size() > size) return false;
        if (b.bit()) {
            out.push_back(uint8_t(b.byte()));
            continue;
        }
        uint32_t x = b.bit();
        uint32_t lo = b.byte(), hb = 0xff;
        int len = 2;
        if (x == 0) {
            if (b.bit() == 0) {
                if (lo == 0xff) {
                    if (b.bit()) continue;  // nothing (segment change)
                    break;                  // end
                }
            } else {
                for (int i = 0; i < 3; i++) hb = (hb << 1 | b.bit()) & 0xff;
                hb = (hb - 1) & 0xff;
            }
        } else {
            hb = (hb << 1 | b.bit()) & 0xff;
            if (b.bit() == 0) {
                uint32_t d = 2;
                for (int i = 0; i < 3; i++) {
                    if (b.bit()) break;
                    hb = (hb << 1 | b.bit()) & 0xff;
                    d = (d << 1) & 0xff;
                }
                hb = (hb - d) & 0xff;
            }
            len = 0;
            for (int l = 3; l <= 6 && !len; l++)
                if (b.bit()) len = l;
            if (!len) {
                if (b.bit()) {
                    len = 7 + int(b.bit());
                } else if (b.bit()) {
                    len = int(b.byte()) + 17;
                } else {
                    int v = int(b.bit()) << 2;
                    v |= int(b.bit()) << 1;
                    v |= int(b.bit());
                    len = 9 + v;
                }
            }
        }
        if (!copyBack(out, 0x10000 - (hb << 8 | lo), len)) return false;
    }
    return out.size() == size;
}

bool overlay(const std::vector<uint8_t>& exe, StarsUnpacked& u, std::string* err) {
    char m[160];
    const size_t dir = size_t(StarsExe::kFiles) * kDirEntry;
    if (exe.size() < kPkEnd + dir) {
        if (err) *err = "STARS.EXE: no overlay";
        return false;
    }
    for (int n = 1; n <= StarsExe::kFiles; n++) {
        const uint8_t* e = &exe[exe.size() - size_t(n) * kDirEntry];
        int64_t pos = int64_t(exe.size()) + int32_t(rd32(e + 4));
        uint32_t size = rd32(e + 8);
        if (rd32(e) != uint32_t(n) || pos < int64_t(kPkEnd) || pos + size > int64_t(exe.size() - dir)) {
            std::snprintf(m, sizeof m, "STARS.EXE: bad overlay directory entry %d", n);
            if (err) *err = m;
            return false;
        }
        const uint8_t* raw = &exe[size_t(pos)];
        if (size >= kDietHeader && raw[6] == 'd' && raw[7] == 'l' && raw[8] == 'z') {
            if (!diet(raw, size, u.files[n])) {
                std::snprintf(m, sizeof m, "STARS.EXE: overlay file %d does not unpack", n);
                if (err) *err = m;
                return false;
            }
        } else {
            u.files[n].assign(raw, raw + size);  // stored as is (files 26 and 31)
        }
    }
    return true;
}
}  // namespace

bool unpackStars(const std::vector<uint8_t>& exe, StarsUnpacked& u, std::string* err) {
    if (exe.size() < kPkEnd || rd16(exe.data()) != 0x5a4d) {
        if (err) *err = "not an MS-DOS executable";
        return false;
    }
    uint16_t ck = 0;
    std::vector<uint8_t> img = decrypt(exe, &ck);
    if (ck != kPkChecksum) {
        if (err) *err = "STARS.EXE: the Infiny Loader checksum does not match (not the released file?)";
        return false;
    }
    if (!pklite(img, u.image, err)) return false;
    return overlay(exe, u, err);
}
