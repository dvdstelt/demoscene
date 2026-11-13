// Unpacks CLONE.EXE (clone/re/NOTES.md "File layout", "The veloader", "The decompressor", "Fixups" and
// clone/re/notes-core.md section 13; models/core/veunpack.py is the same in Python). Ports the veloader's work:
//   0x10016 the loader: MZ size -> LE header -> "VE" header behind the LE program's data page;
//   0x103d1 the decompressor: LZ77 with a 4 KB window and canonical Huffman codes (a 16-symbol code for the code
//           lengths, a 280-symbol literal / distance code, a 22-symbol length code), bits MSB first from big-endian
//           16-bit words;
//   0x10289 the fixups: runs of dword offsets (base added) with base-128 deltas.
// The distance and length tables are read from the veloader in the file (they are Deflate's, cut to 24 and 22
// codes). The intro needs no selector fixups and no PMODE/W.
#include <algorithm>
#include <cstdio>

#include "exe.h"

namespace {
struct Reader {
    const std::vector<uint8_t>& d;
    std::string* err;
    bool ok = true;
    uint32_t u8(size_t o) { return o < d.size() ? d[o] : fail(o); }
    uint32_t u16(size_t o) { return u8(o) | u8(o + 1) << 8; }
    uint32_t u32(size_t o) { return u16(o) | u16(o + 2) << 16; }
    uint32_t fail(size_t o) {
        if (ok && err) {
            char b[96];
            std::snprintf(b, sizeof b, "CLONE.EXE is too short (offset 0x%zx)", o);
            *err = b;
        }
        ok = false;
        return 0;
    }
};

// Big-endian 16-bit words, bits MSB first, through a left-aligned 32-bit window refilled while 16 or fewer bits are
// left (so the reader looks up to 2 bytes past the end of the stream: those read as zero, notes-core 13).
class Bits {
public:
    Bits(std::span<const uint8_t> d, size_t pos) : d_(d), p_(pos) {}
    uint32_t peek() {
        while (n_ <= 16) {
            uint32_t w = byte(p_) << 8 | byte(p_ + 1);
            p_ += 2;
            buf_ |= w << (16 - n_);
            n_ += 16;
        }
        return buf_;
    }
    uint32_t take(int k) {
        if (k == 0) return 0;
        uint32_t v = peek() >> (32 - k);
        buf_ = k == 32 ? 0 : buf_ << k;
        n_ -= k;
        return v;
    }

private:
    uint32_t byte(size_t i) const { return i < d_.size() ? d_[i] : 0; }
    std::span<const uint8_t> d_;
    size_t p_;
    uint32_t buf_ = 0;
    int n_ = 0;
};

// A canonical code (0x10414..0x10472): for L = 1..15 the symbols of length L in increasing order get consecutive
// codes; the lengths must fill the code space exactly.
class Code {
public:
    bool build(const std::vector<int>& lengths) {
        uint64_t code = 0;
        groups_.clear();
        for (int L = 1; L <= 15; L++) {
            Group g;
            g.len = L;
            g.first = code;
            for (size_t s = 0; s < lengths.size(); s++)
                if (lengths[s] == L) g.syms.push_back(int(s));
            if (g.syms.empty()) continue;
            code += uint64_t(g.syms.size()) << (32 - L);
            g.last = (code - 1) | ((uint64_t(1) << (32 - L)) - 1);
            groups_.push_back(std::move(g));
        }
        return code == (uint64_t(1) << 32);
    }
    // The shortest L whose last code (left-aligned, low bits ones) is >= the next 32 bits; -1 if none.
    int decode(Bits& b) const {
        uint64_t w = b.peek();
        for (const Group& g : groups_)
            if (g.last >= w) {
                b.take(g.len);
                return g.syms[size_t((w - g.first) >> (32 - g.len))];
            }
        return -1;
    }

private:
    struct Group { int len; uint64_t first, last; std::vector<int> syms; };
    std::vector<Group> groups_;
};

struct Tables {
    uint32_t distBase[24], lenBase[22];
    int distExtra[24], lenExtra[22];
};

bool decompress(std::span<const uint8_t> src, size_t outSize, const Tables& t, std::vector<uint8_t>& out,
                std::string* err) {
    auto bad = [&](const char* what) {
        if (err) *err = std::string("CLONE.EXE: ") + what;
        return false;
    };
    if (src.size() < 19) return bad("packed stream too short");
    std::vector<int> lc(16), ln(22), ld(280);
    for (int i = 0; i < 16; i++) lc[size_t(i)] = (src[size_t(i / 2)] >> (4 * (i & 1))) & 15;      // bytes 0..7
    for (int i = 0; i < 22; i++) ln[size_t(i)] = (src[size_t(8 + i / 2)] >> (4 * (i & 1))) & 15;  // bytes 8..18
    Bits bits(src, 19);
    Code lcode, litdist, lencode;
    if (!lcode.build(lc)) return bad("incomplete code lengths");
    for (int& l : ld)
        if ((l = lcode.decode(bits)) < 0) return bad("bad code length");
    if (!litdist.build(ld) || !lencode.build(ln)) return bad("incomplete code lengths");
    out.clear();
    out.reserve(outSize);
    while (out.size() < outSize) {
        int s = litdist.decode(bits);
        if (s < 0) return bad("bad code");
        if (s < 256) {
            out.push_back(uint8_t(s));
            continue;
        }
        int k = s - 256;
        if (k >= 24) return bad("bad distance code");
        size_t dist = t.distBase[k] + bits.take(t.distExtra[k]);
        int j = lencode.decode(bits);
        if (j < 0) return bad("bad length code");
        size_t len = t.lenBase[j] + bits.take(t.lenExtra[j]);
        if (dist > out.size()) return bad("distance before the start");
        for (size_t i = 0; i < len; i++) out.push_back(out[out.size() - dist]);  // byte by byte: overlap allowed
    }
    out.resize(outSize);
    return true;
}

// 0x10289: records {byte type, word count (0 = 65536), dword first site}, then count - 1 big-endian base-128 deltas.
bool applyFixups(std::vector<uint8_t>& img, const std::vector<uint8_t>& fix, uint32_t base, int* applied,
                 std::string* err) {
    size_t p = 0;
    auto byte = [&]() -> int { return p < fix.size() ? fix[p++] : -1; };
    auto bad = [&](const char* what) {
        if (err) *err = std::string("CLONE.EXE fixups: ") + what;
        return false;
    };
    *applied = 0;
    for (;;) {
        int type = byte();
        if (type < 0) return bad("no end record");
        if (type == 0) return true;
        if (type >= 4) return bad("unknown record type");
        if (type & 2) return bad("selector fixups (not in the released intro)");
        if (p + 6 > fix.size()) return bad("record too short");
        uint32_t count = fix[p] | fix[p + 1] << 8;
        uint32_t site = fix[p + 2] | fix[p + 3] << 8 | fix[p + 4] << 16 | uint32_t(fix[p + 5]) << 24;
        p += 6;
        if (count == 0) count = 65536;
        for (uint32_t i = 0; i < count; i++) {
            if (i) {
                uint32_t delta = 0;
                int b;
                do {
                    if ((b = byte()) < 0) return bad("delta past the end");
                    delta = delta << 7 | uint32_t(b & 0x7f);
                } while (b & 0x80);
                site += delta;
            }
            if (size_t(site) + 4 > img.size()) return bad("site outside the image");
            uint32_t v = img[site] | img[site + 1] << 8 | img[site + 2] << 16 | uint32_t(img[site + 3]) << 24;
            v += base;
            for (int k = 0; k < 4; k++) img[site + size_t(k)] = uint8_t(v >> (8 * k));
            site += 4;
            ++*applied;
        }
    }
}
}  // namespace

bool unpackClone(const std::vector<uint8_t>& exe, uint32_t base, CloneUnpacked& out, std::string* err) {
    Reader r{exe, err};
    if (r.u16(0) != 0x5a4d) {
        if (err) *err = "CLONE.EXE: not an MZ executable";
        return false;
    }
    // MZ size rounded up to 8 = the LE header; the VE header follows the LE program's last data page.
    uint32_t lastPage = r.u16(2), pages = r.u16(4);
    uint32_t mz = lastPage ? (pages - 1) * 512 + lastPage : pages * 512;
    uint32_t le = (mz + 7) & ~7u;
    if (r.u16(le) != 0x454c) {  // "LE"
        if (err) *err = "CLONE.EXE: no LE program behind the stub";
        return false;
    }
    uint32_t dataPages = r.u32(le + 0x80), lePages = r.u32(le + 0x14), leLast = r.u32(le + 0x2c);
    uint32_t ve = dataPages + lePages * 0x1000 - (leLast ? 0x1000 - leLast : 0);
    if (r.u16(ve) != 0x4556) {  // "VE"
        if (err) *err = "CLONE.EXE: no VE header behind the LE program";
        return false;
    }
    // The veloader's tables (object 1 = the first data page; offsets in the object, NOTES.md "The decompressor").
    Tables t;
    for (int k = 0; k < 24; k++) {
        t.distBase[k] = r.u32(dataPages + 0x788 + 4 * uint32_t(k));
        t.distExtra[k] = int(r.u8(dataPages + 0x840 + uint32_t(k)));
    }
    for (int j = 0; j < 22; j++) {
        t.lenBase[j] = r.u32(dataPages + 0x7e8 + 4 * uint32_t(j));
        t.lenExtra[j] = int(r.u8(dataPages + 0x858 + uint32_t(j)));
    }
    uint32_t imgOfs = r.u32(ve + 4), fixOfs = r.u32(ve + 8);
    uint32_t imgPacked = r.u32(ve + 0x14), fixPacked = r.u32(ve + 0x18);
    uint32_t imgSize = r.u32(ve + 0x24), fixSize = r.u32(ve + 0x28);
    out.bss = r.u32(ve + 0x2c);
    out.entry = r.u32(ve + 0x34);
    out.stack = r.u32(ve + 0x38);
    uint32_t mImg = r.u8(ve + 0x40), mFix = r.u8(ve + 0x41);
    if (!r.ok) return false;
    auto stream = [&](uint32_t ofs, uint32_t packed, uint32_t size, uint32_t method, std::vector<uint8_t>& dst,
                      const char* what) {
        size_t a = size_t(ve) + ofs;
        if (a + packed > exe.size() || method >= 3 || (method == 0 && packed < size)) {
            if (err) *err = std::string("CLONE.EXE: bad ") + what + " stream";
            return false;
        }
        std::span<const uint8_t> src(exe.data() + a, packed);
        if (method == 0) {
            dst.assign(src.begin(), src.begin() + size);
            return true;
        }
        return decompress(src, size, t, dst, err);
    };
    std::vector<uint8_t> fix;
    if (!stream(imgOfs, imgPacked, imgSize, mImg, out.image, "image") ||
        !stream(fixOfs, fixPacked, fixSize, mFix, fix, "fixup"))
        return false;
    return applyFixups(out.image, fix, base, &out.fixups, err);
}
