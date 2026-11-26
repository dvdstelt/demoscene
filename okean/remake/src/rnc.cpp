// Unpacks KORSO2.EXE (exe.h): Rob Northen's ProPack method 1, the decoder of the packer stub that sits at the end of
// the packed program (okean/re/NOTES.md "Unpacking"; okean/re/unpack.py checks it against the stub itself: the
// output is okean/re/unpacked/korso2.bin byte for byte). The same packer as DOPE's parts (dope/remake/src/rnc.cpp).
//
// The stub also applies a relocation table (20 sites, all in PMODE's 16-bit kernel); the remake only reads data, so
// it keeps the decoder output, the image relocated for load segment 0.
#include <cstring>

#include "exe.h"

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// CRC-16/ARC: reflected polynomial 0xA001, initial value 0.
uint16_t crc16(const uint8_t* p, size_t n) {
    static uint16_t table[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) {
            uint16_t c = uint16_t(i);
            for (int k = 0; k < 8; k++) c = (c & 1) ? uint16_t((c >> 1) ^ 0xA001) : uint16_t(c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) c = uint16_t((c >> 8) ^ table[(c ^ p[i]) & 0xff]);
    return c;
}

// The bit reader: a 32-bit buffer with n valid bits, taken from the least significant end, and the byte position p
// of the look-ahead word (the buffer always holds the whole word at p above the bits still to be used).
struct Bits {
    const uint8_t* d;
    size_t size, p;
    uint32_t buf;
    int n;
    Bits(const uint8_t* data, size_t sz, size_t start) : d(data), size(sz), p(start), buf(word(start)), n(16) {}
    uint32_t word(size_t q) const {
        if (q + 1 < size) return uint32_t(d[q] | d[q + 1] << 8);
        return q < size ? d[q] : 0;
    }
    uint32_t peek(int k) const { return buf & ((1u << k) - 1); }
    uint32_t take(int k) {
        uint32_t v = peek(k);
        buf >>= k;
        n -= k;
        if (n < 16) {
            p += 2;
            buf |= word(p) << n;
            n += 16;
        }
        return v;
    }
    // A literal run: bytes straight from the stream at p, then the look-ahead word is replaced.
    bool raw(std::vector<uint8_t>& out, uint32_t count) {
        if (p + count > size) return false;
        out.insert(out.end(), d + p, d + p + count);
        p += count;
        n -= 16;
        buf &= (1u << n) - 1;
        buf |= word(p) << n;
        n += 16;
        return true;
    }
};

// A canonical Huffman table: N values with 4-bit code lengths; codes are matched LSB first (bit-reversed).
struct Table {
    struct Code { int len; uint32_t bits; int value; };
    std::vector<Code> codes;  // in order of length

    void read(Bits& b) {
        int n = int(b.take(5));
        int lens[32] = {};
        for (int v = 0; v < n; v++) lens[v] = int(b.take(4));
        codes.clear();
        uint32_t counter = 0;
        for (int L = 1; L <= 16; L++) {
            for (int v = 0; v < n; v++)
                if (lens[v] == L) {
                    uint32_t r = 0, c = counter;
                    for (int k = 0; k < L; k++) { r = r << 1 | (c & 1); c >>= 1; }
                    codes.push_back({L, r, v});
                    counter++;
                }
            counter <<= 1;
        }
    }
    bool decode(Bits& b, int* v) const {
        for (const Code& c : codes)
            if (b.peek(c.len) == c.bits) {
                b.take(c.len);
                *v = c.value;
                return true;
            }
        return false;
    }
    // A value: v < 2 is itself, else (1 << (v - 1)) | (v - 1 more bits).
    bool value(Bits& b, uint32_t* out) const {
        int v;
        if (!decode(b, &v)) return false;
        *out = v < 2 ? uint32_t(v) : (1u << (v - 1)) | b.take(v - 1);
        return true;
    }
};

}  // namespace

// The load module of a packed MZ: 18-byte header ("RNC" 01, sizes, CRCs, leeway, chunk count), the bit stream, the
// stub. Returns false with a message on any inconsistency.
bool unpackRnc(const uint8_t* m, size_t size, std::vector<uint8_t>& out, std::string* err) {
    auto fail = [&](const char* what) {
        if (err) *err = std::string("RNC: ") + what;
        return false;
    };
    if (size < 18 || std::memcmp(m, "RNC\x01", 4) != 0) return fail("not an RNC-1 module");
    uint32_t usize = be32(m + 4), psize = be32(m + 8);
    uint16_t ucrc = uint16_t(m[12] << 8 | m[13]), pcrc = uint16_t(m[14] << 8 | m[15]);
    int chunks = m[17];
    if (18 + size_t(psize) > size) return fail("packed size beyond the module");
    if (crc16(m + 18, psize) != pcrc) return fail("packed CRC mismatch");
    out.clear();
    out.reserve(usize);
    Bits b(m, size, 18);
    b.take(2);  // lock and key flags (both 0)
    Table lit, dist, len;
    for (int c = 0; c < chunks; c++) {
        lit.read(b);
        dist.read(b);
        len.read(b);
        uint32_t pairs = b.take(16);
        for (uint32_t i = 0; i < pairs; i++) {
            uint32_t run;
            if (!lit.value(b, &run) || !b.raw(out, run)) return fail("bad literal run");
            if (i + 1 == pairs) break;
            uint32_t d, l;
            if (!dist.value(b, &d) || !len.value(b, &l)) return fail("bad match");
            d += 1;
            l += 2;
            if (d > out.size() || out.size() + l > usize) return fail("match out of range");
            for (uint32_t k = 0; k < l; k++) out.push_back(out[out.size() - d]);
        }
    }
    if (out.size() != usize) return fail("unpacked size mismatch");
    if (crc16(out.data(), out.size()) != ucrc) return fail("unpacked CRC mismatch");
    return true;
}
