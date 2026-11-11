// 303.EXE loader (see exe.h): the LE objects with their internal fixups applied, as remake/tools/le_load.py does.
#include "exe.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

uint64_t fnv1a(const std::vector<uint8_t>& d) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : d) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    if (o + 4 > d.size()) throw std::out_of_range("LE header");
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
uint16_t rd16(const std::vector<uint8_t>& d, size_t o) {
    if (o + 2 > d.size()) throw std::out_of_range("LE header");
    return uint16_t(d[o] | d[o + 1] << 8);
}
void wr32(uint8_t* p, uint32_t v) {
    for (int k = 0; k < 4; k++) p[k] = uint8_t(v >> (8 * k));
}
void wr16(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
}  // namespace

bool Exe303::load(const std::string& path, std::string* err) {
    img_.clear();
    std::vector<uint8_t> d;
    if (!readAll(path, d)) {
        if (err) *err = "cannot read " + path + " (the original 303.EXE is needed next to MUSTARD.PAK)";
        return false;
    }
    // The released 303.EXE (414153 bytes).
    if (d.size() != 414153 || fnv1a(d) != 0xf1668836c9fd1036ull) {
        if (err) {
            char b[256];
            std::snprintf(b, sizeof b, "unexpected 303.EXE contents (%zu bytes, hash %016llx); the remake needs the released "
                          "303.EXE of 414153 bytes", d.size(), (unsigned long long)fnv1a(d));
            *err = b;
        }
        return false;
    }
    try {
        // bound DOS/4GW executables chain several MZ stubs; find the 'LE' header
        size_t le = rd32(d, 0x3c);
        if (le + 2 > d.size() || d[le] != 'L' || d[le + 1] != 'E') {
            static const uint8_t sig[4] = {'L', 'E', 0, 0};
            auto it = std::search(d.begin(), d.end(), sig, sig + 4);
            if (it == d.end()) throw std::runtime_error("no LE header");
            le = size_t(it - d.begin());
        }
        const uint32_t npages = rd32(d, le + 0x14), pgsize = rd32(d, le + 0x28), lastpg = rd32(d, le + 0x2c);
        const size_t objtab = le + rd32(d, le + 0x40), pgtab = le + rd32(d, le + 0x48);
        const uint32_t nobj = rd32(d, le + 0x44);
        const size_t fxpg = le + rd32(d, le + 0x68), fxrec = le + rd32(d, le + 0x6c);
        const size_t datapg = rd32(d, le + 0x80);
        struct Obj { uint32_t vsize, base, pidx, pcnt; size_t size; };
        std::vector<Obj> objs(nobj);
        uint32_t end = kBase;
        for (uint32_t i = 0; i < nobj; i++) {
            Obj& o = objs[i];
            o.vsize = rd32(d, objtab + 24 * i);
            o.base = rd32(d, objtab + 24 * i + 4);
            o.pidx = rd32(d, objtab + 24 * i + 12);
            o.pcnt = rd32(d, objtab + 24 * i + 16);
            o.size = std::max<size_t>(o.vsize, size_t(o.pcnt) * pgsize);
            if (o.base < kBase) throw std::runtime_error("object below the image base");
            end = std::max(end, uint32_t(o.base + o.size));
        }
        img_.assign(end - kBase, 0);
        // pages (LE page table: 3-byte big-endian page number + flags)
        for (const Obj& o : objs)
            for (uint32_t k = 0; k < o.pcnt; k++) {
                size_t e = pgtab + 4 * (size_t(o.pidx) - 1 + k);
                if (e + 3 > d.size()) throw std::out_of_range("page table");
                uint32_t pn = uint32_t(d[e]) << 16 | uint32_t(d[e + 1]) << 8 | d[e + 2];
                size_t src = datapg + size_t(pn - 1) * pgsize, n = pn == npages ? lastpg : pgsize;
                if (src + n > d.size()) throw std::out_of_range("page data");
                std::memcpy(&img_[o.base - kBase + size_t(k) * pgsize], &d[src], n);
            }
        // fixups (internal references only)
        for (const Obj& o : objs) {
            uint8_t* buf = &img_[o.base - kBase];
            for (uint32_t k = 0; k < o.pcnt; k++) {
                const size_t g = size_t(o.pidx) - 1 + k;
                size_t p = fxrec + rd32(d, fxpg + 4 * g), pe = fxrec + rd32(d, fxpg + 4 * (g + 1));
                while (p < pe) {
                    if (p + 2 > d.size()) throw std::out_of_range("fixup record");
                    const uint8_t srcType = d[p], flags = d[p + 1];
                    p += 2;
                    int cnt = 1;
                    int16_t single = 0;
                    if (srcType & 0x20) cnt = d[p++];
                    else { single = int16_t(rd16(d, p)); p += 2; }
                    if (flags & 3) throw std::runtime_error("unsupported (external) fixup");
                    uint32_t tobj;
                    if (flags & 0x40) { tobj = rd16(d, p); p += 2; }
                    else tobj = d[p++];
                    if (tobj < 1 || tobj > nobj) throw std::runtime_error("bad fixup target object");
                    const int stype = srcType & 0x0f;
                    uint32_t toff = 0;
                    if (stype != 2) {
                        if (flags & 0x10) { toff = rd32(d, p); p += 4; }
                        else { toff = rd16(d, p); p += 2; }
                    }
                    const uint32_t tgt = objs[tobj - 1].base + toff;
                    for (int j = 0; j < cnt; j++) {
                        int so = single;
                        if (srcType & 0x20) { so = int16_t(rd16(d, p)); p += 2; }
                        const long at = long(k) * long(pgsize) + so;
                        if (at < 0 || size_t(at) + 4 > o.size) continue;
                        uint8_t* q = buf + at;
                        switch (stype) {
                            case 7: wr32(q, tgt); break;                                         // 32-bit offset
                            case 8: wr32(q, tgt - (o.base + uint32_t(at) + 4)); break;           // 32-bit relative
                            case 2: wr16(q, tobj); break;                                        // selector
                            case 5: wr16(q, tgt); break;                                         // 16-bit offset
                            case 6: wr32(q, tgt); if (size_t(at) + 6 <= o.size) wr16(q + 4, tobj); break;  // 16:32
                            default: break;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        img_.clear();
        if (err) *err = std::string("303.EXE: ") + e.what();
        return false;
    }
    return true;
}

const uint8_t* Exe303::at(uint32_t a, size_t n) const {
    if (a < kBase || size_t(a - kBase) + n > img_.size()) throw std::out_of_range("303.EXE address out of range");
    return img_.data() + (a - kBase);
}

std::string Exe303::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}
