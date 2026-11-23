// "LCA's Packer", the archive at the end of REVE.EXE, and its LZW: the routines of LCA's FLAT OS V1.0 that the demo
// calls with int 0FFh AX=0209h (kernel 0x8f03: the lookup 0x9228, the decompressor 0x9284 with its bit reader 0x93c0,
// output 0x93f4 and table 0x9414), and the extender's relocation of REVE.LFL (kernel 0x1d30). reve/re/unpack.py is
// the Python version, checked against the extender's own code; reve/re/NOTES.md "File layout" describes the format.
#include <cstdio>

#include "exe.h"

namespace {
uint32_t rd32(std::span<const uint8_t> d, size_t o) {
    return d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24;
}
constexpr char kMagic[] = "LCA's Packer\x1a";
constexpr size_t kMagicLen = sizeof kMagic - 1;
}  // namespace

uint32_t lcaChecksum(std::span<const uint8_t> d) {
    uint32_t s = 0;
    for (uint8_t b : d) {
        s += b;
        s = s >> 3 | s << 29;
    }
    return s;
}

bool lzwDecode(std::span<const uint8_t> in, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(size);
    size_t pos = 0;  // bit position
    const size_t bits = in.size() * 8;
    auto code = [&](int n) -> int {
        if (pos + n > bits) return -1;
        uint32_t v = 0;
        size_t b = pos >> 3;
        for (int i = 0; i < 4 && b + i < in.size(); i++) v |= uint32_t(in[b + i]) << (8 * i);
        v >>= pos & 7;
        pos += n;
        return int(v & ((1u << n) - 1));
    };
    std::vector<uint16_t> prefix(8192 + 4096);
    std::vector<uint8_t> chr(8192 + 4096);
    std::vector<uint8_t> stack;
    for (;;) {
        int nbits = 9, limit = 0x200, next = 0x102;
        int c = code(nbits);
        if (c < 0 || c > 0x101) return false;
        if (c == 0x100) return true;
        if (c == 0x101) continue;
        int old = c, first = c;
        out.push_back(uint8_t(c));
        for (;;) {
            c = code(nbits);
            if (c < 0) return false;
            if (c == 0x100) return true;
            if (c == 0x101) break;
            if (c > next || (c >= 0x102 && c >= int(prefix.size()))) return false;
            stack.clear();
            int x = c;
            if (c >= next) {  // the code being defined: the previous string and its first byte
                stack.push_back(uint8_t(first));
                x = old;
            }
            while (x >= 0x100) {
                stack.push_back(chr[x]);
                x = prefix[x];
            }
            stack.push_back(uint8_t(x));
            first = x;
            out.insert(out.end(), stack.rbegin(), stack.rend());
            if (next < int(prefix.size())) {
                prefix[next] = uint16_t(old);
                chr[next] = uint8_t(first);
            }
            next++;
            old = c;
            if (next >= limit && nbits < 13) {
                nbits++;
                limit <<= 1;
            }
        }
    }
}

bool unpackArchive(std::span<const uint8_t> exe, std::vector<ReveFile>& files, std::string* err) {
    files.clear();
    size_t a = 0;
    for (; a + kMagicLen <= exe.size(); a++)
        if (std::memcmp(exe.data() + a, kMagic, kMagicLen) == 0) break;
    if (a + kMagicLen + 2 > exe.size()) {
        if (err) *err = "REVE.EXE holds no LCA archive";
        return false;
    }
    size_t p = a + kMagicLen + 2;
    while (p + 14 <= exe.size()) {
        const uint32_t rsize = rd32(exe, p), usize = rd32(exe, p + 4), sum = rd32(exe, p + 8);
        const uint8_t method = exe[p + 12];
        size_t e = p + 13;
        while (e < exe.size() && e < p + 13 + 80 && exe[e]) e++;
        if (e >= exe.size() || exe[e] || e == p + 13 || p + rsize > exe.size() || rsize < e + 1 - p) break;
        ReveFile f;
        f.name.assign(reinterpret_cast<const char*>(exe.data() + p + 13), e - (p + 13));
        f.offset = uint32_t(p);
        f.checksum = sum;
        if (method != 1 || !lzwDecode(exe.subspan(e + 1, p + rsize - (e + 1)), usize, f.data) ||
            f.data.size() != usize || lcaChecksum(f.data) != sum) {
            if (err) *err = "REVE.EXE: cannot unpack " + f.name;
            return false;
        }
        files.push_back(std::move(f));
        p += rsize;
    }
    if (files.empty()) {
        if (err) *err = "REVE.EXE: empty archive";
        return false;
    }
    return true;
}

bool relocateLfl(std::span<const uint8_t> lfl, uint32_t base, std::vector<uint8_t>& image, int* relocs,
                 std::string* err) {
    constexpr char kSig[] = "LCA's FLAT OS V1.0";
    if (lfl.size() < 0x20 || std::memcmp(lfl.data(), kSig, sizeof kSig - 1) != 0) {
        if (err) *err = "REVE.LFL: not a FLAT OS program";
        return false;
    }
    const uint32_t isize = rd32(lfl, 0x14);
    if (0x20 + size_t(isize) > lfl.size() || std::memcmp(lfl.data() + 0x1c + isize, "LCAr", 4) != 0) {
        if (err) *err = "REVE.LFL: no relocation table";
        return false;
    }
    image.assign(lfl.begin() + 0x1c, lfl.begin() + 0x1c + isize);
    int n = 0;
    for (size_t o = 0x20 + size_t(isize); o + 4 <= lfl.size(); o += 4, n++) {
        const uint32_t at = rd32(lfl, o);
        if (size_t(at) + 4 > image.size()) {
            if (err) *err = "REVE.LFL: relocation out of range";
            return false;
        }
        uint32_t v = rd32(image, at) + base;
        std::memcpy(image.data() + at, &v, 4);
    }
    if (relocs) *relocs = n;
    return true;
}
