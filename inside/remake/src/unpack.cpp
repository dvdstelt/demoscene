// Unpacks INSIDE.EXE (inside/re/NOTES.md "Unpacking"; inside/re/unpack.py is the same in Python and checks itself
// against the extender's own code run in an emulator). Ports PMODE/W v1.22's loader for its "PMW1" format:
//   the header at e_lfanew (0x3c): entry and stack object and offset, the object table, the fixup table, the data;
//   kernel 0x1e1c, the LZ decompressor: one literal byte, then items selected by bits taken MSB first from
//     little-endian dwords that are read from the same byte stream when the bits run out: 0 = a literal byte,
//     1 = a match whose distance's low byte is the next byte, with three kinds (length 2 with an 8-bit or an 11-bit
//     distance, or lengths 3..270 with a 10- or 12-bit distance), and an end marker;
//   kernel 0x220b..0x22cd and 0x2634, the fixups: per object, blocks of compressed 10-byte records {type, source
//     offset, target object, target offset}; type 7 = 32-bit offset (the target object's base added), type 2 = 16-bit
//     selector.
#include <cstdio>

#include "exe.h"

namespace {
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24 : 0;
}

bool fail(std::string* err, const char* what) {
    if (err) *err = std::string("INSIDE.EXE: ") + what;
    return false;
}
}  // namespace

size_t pmwDecompress(std::span<const uint8_t> in, std::vector<uint8_t>& out) {
    size_t i = 0;
    uint32_t bits = 0;
    int nbits = 0;
    bool bad = false;
    auto byte = [&]() -> uint32_t {
        if (i >= in.size()) { bad = true; return 0; }
        return in[i++];
    };
    auto bit = [&]() -> uint32_t {
        if (nbits == 0) {
            bits = byte();
            bits |= byte() << 8;
            bits |= byte() << 16;
            bits |= byte() << 24;
            nbits = 32;
        }
        --nbits;
        uint32_t b = bits >> 31;
        bits <<= 1;
        return b;
    };
    out.push_back(uint8_t(byte()));
    while (!bad) {
        if (!bit()) {
            out.push_back(uint8_t(byte()));
            continue;
        }
        uint32_t lo = byte(), dist, length = 0;
        if (bit()) {
            uint32_t hi = 0;
            if (bit()) {
                int n = 2 * (1 + int(bit()));
                for (int k = 0; k < n; ++k) hi = hi << 1 | bit();
            }
            for (int k = 0; k < 4 && !length; ++k)
                if (bit()) length = 3 + k;
            if (!length) {
                if (bit()) {
                    length = byte() + 15;
                } else {
                    uint32_t v = 0;
                    for (int k = 0; k < 3; ++k) v = v << 1 | bit();
                    length = v + 7;
                }
            }
            dist = (hi << 8 | lo) + 1;
        } else if (bit()) {
            uint32_t hi = 0;
            for (int k = 0; k < 3; ++k) hi = hi << 1 | bit();
            dist = (hi << 8 | lo) + 1;
            length = 2;
        } else {
            if (lo == 0) return i;   // end marker
            dist = lo;
            length = 2;
        }
        if (dist > out.size()) return 0;
        for (uint32_t k = 0; k < length; ++k) out.push_back(out[out.size() - dist]);
    }
    return 0;
}

bool unpackInside(const std::vector<uint8_t>& exe, uint32_t base1, uint32_t base2, InsideUnpacked& out,
                  std::string* err) {
    if (exe.size() < 0x40 || exe[0] != 'M' || exe[1] != 'Z') return fail(err, "not an MZ executable");
    const size_t h = rd32(exe, 0x3c);
    if (h + 0x28 > exe.size() || std::memcmp(&exe[h], "PMW1", 4) != 0) return fail(err, "no PMW1 program");
    const uint32_t entObj = rd32(exe, h + 8), entOfs = rd32(exe, h + 12);
    const uint32_t stkObj = rd32(exe, h + 16), stkOfs = rd32(exe, h + 20);
    const uint32_t objTab = rd32(exe, h + 24), nObj = rd32(exe, h + 28);
    const uint32_t fixTab = rd32(exe, h + 32), dataOfs = rd32(exe, h + 36);
    if (nObj != 2 || entObj < 1 || entObj > 2 || stkObj < 1 || stkObj > 2) return fail(err, "unexpected objects");
    struct Obj { uint32_t vsize, csize, flags, fixIdx, fixBlocks, dsize, base; size_t data; std::vector<uint8_t> mem; };
    Obj obj[2];
    size_t pos = h + dataOfs;
    for (int k = 0; k < 2; ++k) {
        size_t e = h + objTab + 24 * k;
        if (e + 24 > exe.size()) return fail(err, "object table cut off");
        obj[k] = {rd32(exe, e), rd32(exe, e + 4), rd32(exe, e + 8), rd32(exe, e + 12), rd32(exe, e + 16),
                  rd32(exe, e + 20), k == 0 ? base1 : base2, pos, {}};
        pos += obj[k].csize;
    }
    if (pos != exe.size()) return fail(err, "object data does not end at the end of the file");
    for (Obj& o : obj) {
        size_t used = pmwDecompress(std::span(exe).subspan(o.data, o.csize), o.mem);
        if (used != o.csize || o.mem.size() != o.dsize || o.dsize > o.vsize) return fail(err, "bad object data");
        o.mem.resize(o.vsize, 0);
    }
    out.fixups = out.selectors = 0;
    for (Obj& o : obj) {
        size_t p = h + fixTab + o.fixIdx;
        for (uint32_t b = 0; b < o.fixBlocks; ++b) {
            if (p + 4 > exe.size()) return fail(err, "fixup table cut off");
            uint32_t csize = exe[p] | exe[p + 1] << 8, dsize = exe[p + 2] | exe[p + 3] << 8;
            std::vector<uint8_t> rec;
            if (pmwDecompress(std::span(exe).subspan(p + 4, std::min<size_t>(csize, exe.size() - p - 4)), rec) !=
                    csize || rec.size() != dsize || dsize % 10)
                return fail(err, "bad fixup block");
            p += 4 + csize;
            for (size_t i = 0; i < rec.size(); i += 10) {
                uint8_t type = rec[i];
                int32_t src = int32_t(rd32(rec, i + 1));
                uint8_t tobj = rec[i + 5];
                uint32_t tofs = rd32(rec, i + 6);
                if (tobj < 1 || tobj > 2 || (type & 0x20)) return fail(err, "bad fixup record");
                if (src < 0) continue;   // the part before the page start
                if ((type & 0xf) == 7) {
                    if (uint32_t(src) + 4 > o.mem.size()) return fail(err, "fixup outside its object");
                    uint32_t v = obj[tobj - 1].base + tofs;
                    std::memcpy(&o.mem[src], &v, 4);
                    ++out.fixups;
                } else if ((type & 0xf) == 2) {
                    if (uint32_t(src) + 2 > o.mem.size()) return fail(err, "fixup outside its object");
                    uint16_t sel = (obj[tobj - 1].flags & 4) ? 0x08 : 0x10;   // flat code / data selector
                    std::memcpy(&o.mem[src], &sel, 2);
                    ++out.selectors;
                } else {
                    return fail(err, "unexpected fixup type");
                }
            }
        }
    }
    if (base2 < base1 + obj[0].vsize) return fail(err, "objects overlap");
    out.image.assign(size_t(base2 - base1) + obj[1].dsize, 0);
    std::memcpy(out.image.data(), obj[0].mem.data(), obj[0].vsize);
    std::memcpy(out.image.data() + (base2 - base1), obj[1].mem.data(), obj[1].dsize);
    out.entry = obj[entObj - 1].base + entOfs;
    out.stack = obj[stkObj - 1].base + stkOfs;
    return true;
}
