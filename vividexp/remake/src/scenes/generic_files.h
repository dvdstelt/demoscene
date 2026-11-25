// The generic 3D scenes' files and their CPU-side evaluation (vividexp/re/notes-generic.md 2, 3, 5.1, 6): the ini
// reader (0x412938 / 0x4129ec with GenericInit's "Common<Key>" / "<Name><Key>" lookup), the .spl reader
// (0x4133f8 / 0x413520 / 0x4137f8), the natural cubic spline (0x413870 / 0x413b90) in double precision, the .m3d
// object (0x412bf0, the mapping coordinates scaled by 0x413330) and the object rotation (0x413d98 / 0x413e6a /
// 0x413f37) as a float matrix. Header only, for src/scenes/generic.cpp.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "exe.h"

namespace generic {

// ---------------------------------------------------------------- ini (0x412938 string, 0x4129ec integer)
// "Key=value" at the start of a line after spaces and tabs, case-sensitive; '#' lines are skipped; "@end" ends the
// search; the value runs to the end of the line without trailing blanks; integers through atoi.
class Ini {
public:
    explicit Ini(std::span<const uint8_t> f) {
        std::string cur;
        for (uint8_t ch : f) {
            if (ch == '\n') { lines_.push_back(cur); cur.clear(); }
            else if (ch != '\r') cur += char(ch);
        }
        lines_.push_back(cur);
    }
    std::optional<std::string> get(const std::string& key) const {
        for (const std::string& l : lines_) {
            size_t p = l.find_first_not_of(" \t");
            if (p == std::string::npos || l[p] == '#') continue;
            if (l.compare(p, 4, "@end") == 0) break;
            if (l.compare(p, key.size(), key) != 0 || p + key.size() >= l.size() || l[p + key.size()] != '=') continue;
            std::string v = l.substr(p + key.size() + 1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            return v;
        }
        return std::nullopt;
    }
    int getInt(const std::string& key, int def) const {
        auto v = get(key);
        return v ? std::atoi(v->c_str()) : def;
    }

private:
    std::vector<std::string> lines_;
};

// A key name from the exe: the string at `addr`, with "%s" / "%d" replaced by `arg`.
inline std::string keyName(const VividExe& exe, uint32_t addr, const std::string& arg = {}) {
    std::string k = exe.cstr(addr);
    for (const char* f : {"%s", "%d"})
        if (size_t p = k.find(f); p != std::string::npos) k.replace(p, 2, arg);
    return k;
}

// GenericInit's per-object lookup: "Common<Key>" (at `common`) if present, else "<Name><Key>" (format at `fmt`).
inline std::optional<std::string> objectKey(const VividExe& exe, const Ini& ini, uint32_t common, uint32_t fmt,
                                            const std::string& name) {
    if (auto v = ini.get(keyName(exe, common))) return v;
    return ini.get(keyName(exe, fmt, name));
}

inline std::string requireKey(const VividExe& exe, const Ini& ini, uint32_t common, uint32_t fmt,
                              const std::string& name) {
    auto v = objectKey(exe, ini, common, fmt, name);
    if (!v) throw std::runtime_error("missing " + keyName(exe, fmt, name));
    return *v;
}

// ---------------------------------------------------------------- .spl (0x4133f8 / 0x413520 / 0x4137f8)
// Text: "@length N" (the header string at kSplLength) gives the rows; lines starting with '#' or '@' are skipped;
// a data row is whitespace-separated integers, anything before the first digit or sign skipped.
struct SplineFile {
    int rows = 0;
    std::vector<std::vector<double>> cols;   // cols[c][k]
};

inline SplineFile readSpl(std::span<const uint8_t> f, const std::string& lengthKey) {
    std::string text(f.begin(), f.end());
    SplineFile s;
    int length = -1;
    std::vector<std::vector<long>> rows;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(i, e - i);
        i = e + 1;
        size_t p = l.find_first_not_of(" \t\r");
        if (p == std::string::npos) continue;
        if (l[p] == '@') {
            if (l.compare(p, lengthKey.size(), lengthKey) == 0) length = std::atoi(l.c_str() + p + lengthKey.size());
            continue;
        }
        if (l[p] == '#') continue;
        std::vector<long> row;
        const char* c = l.c_str();
        while (*c) {
            while (*c && !(std::isdigit(uint8_t(*c)) || *c == '-' || *c == '+')) ++c;
            if (!*c) break;
            char* end = nullptr;
            row.push_back(std::strtol(c, &end, 10));
            if (end == c) ++c; else c = end;
        }
        if (!row.empty()) rows.push_back(std::move(row));
    }
    s.rows = length > 0 ? std::min(length, int(rows.size())) : int(rows.size());
    if (s.rows < 2) throw std::runtime_error("spline: fewer than 2 rows");
    size_t width = rows[0].size();
    s.cols.assign(width, std::vector<double>(size_t(s.rows)));
    for (int k = 0; k < s.rows; ++k)
        for (size_t c = 0; c < width; ++c) s.cols[c][size_t(k)] = c < rows[size_t(k)].size() ? double(rows[size_t(k)][c]) : 0.0;
    return s;
}

// ---------------------------------------------------------------- spline (0x413870 prepare, 0x413b90 evaluate)
// A natural cubic spline through the keys at x = 0, 1, ..., n - 1 (Numerical Recipes spline / splint). The original
// works in 16.16 fixed point on value * 256 and returns the result >> 8; in doubles it is the same within 1.25 units
// (notes-generic.md 5.1).
struct Spline {
    std::vector<double> y, y2;
    explicit Spline(const std::vector<double>& v) : y(v), y2(v.size(), 0.0) {
        size_t n = v.size();
        std::vector<double> u(n, 0.0);
        for (size_t i = 1; i + 1 < n; ++i) {
            double p = 0.5 * y2[i - 1] + 2.0;
            y2[i] = -0.5 / p;
            double d = (y[i + 1] - y[i]) - (y[i] - y[i - 1]);
            u[i] = (3.0 * d - 0.5 * u[i - 1]) / p;   // (6 d / (x[i+1] - x[i-1]) - sig u[i-1]) / p with unit spacing
        }
        for (size_t k = n - 1; k-- > 0;) y2[k] = y2[k] * y2[k + 1] + u[k];
    }
    // s in keys, 0 <= s <= n - 1 (the binary search of 0x413b90: x[lo] < s <= x[hi])
    double operator()(double s) const {
        int n = int(y.size());
        int lo = std::clamp(int(std::ceil(s)) - 1, 0, n - 2);
        int hi = lo + 1;
        double b = s - lo, a = 1.0 - b;
        return a * y[size_t(lo)] + b * y[size_t(hi)] +
               ((a * a * a - a) * y2[size_t(lo)] + (b * b * b - b) * y2[size_t(hi)]) / 6.0;
    }
};

// ---------------------------------------------------------------- .m3d (0x412bf0)
struct Mesh3d {
    struct Vertex { float x, y, z, nx, ny, nz, u, v; };
    std::vector<Vertex> verts;         // one per distinct (vertex, normal) pair
    std::vector<uint32_t> faces;       // three per face (all groups)
};

inline Mesh3d readM3d(std::span<const uint8_t> f) {
    auto u32 = [&](size_t o) {
        if (o + 4 > f.size()) throw std::runtime_error("m3d: truncated");
        uint32_t v; std::memcpy(&v, f.data() + o, 4); return v;
    };
    auto s16 = [&](size_t o) {
        if (o + 2 > f.size()) throw std::runtime_error("m3d: truncated");
        int16_t v; std::memcpy(&v, f.data() + o, 2); return v;
    };
    if (f.size() < 36 || std::memcmp(f.data(), "$M3D", 4) != 0) throw std::runtime_error("not a $M3D object");
    const uint32_t flags = u32(4), nv = u32(8), nn = u32(12), ng = u32(16);
    const uint32_t ov = u32(20), on = u32(28), og = u32(32);
    // GenericInit asks for mapping coordinates and normals per face corner (flags 0x203, types 6..11)
    if (!(flags & 1) || !(flags & 2) || !(flags & 4)) throw std::runtime_error("m3d: no mapping coordinates or normals");
    const size_t vs = 10;
    Mesh3d m;
    std::unordered_map<uint64_t, uint32_t> slot;      // (vertex, normal) -> index in verts
    size_t p = og;
    for (uint32_t g = 0; g < ng; ++g) {
        int nf = s16(p);
        p += 4;
        for (int k = 0; k < nf; ++k, p += 12) {
            for (int c = 0; c < 3; ++c) {
                int vi = s16(p + size_t(c) * 2), ni = s16(p + 6 + size_t(c) * 2);
                if (vi < 0 || uint32_t(vi) >= nv || ni < 0 || uint32_t(ni) >= nn) throw std::runtime_error("m3d: bad index");
                auto [it, added] = slot.try_emplace(uint64_t(vi) << 32 | uint32_t(ni), uint32_t(m.verts.size()));
                if (added) {
                    size_t a = ov + size_t(vi) * vs, b = on + size_t(ni) * 6;
                    uint16_t uu, vv;
                    if (a + 10 > f.size()) throw std::runtime_error("m3d: truncated");
                    std::memcpy(&uu, f.data() + a + 6, 2);
                    std::memcpy(&vv, f.data() + a + 8, 2);
                    // 0x413330 scales u, v (0..65535 = one turn of the 256-texel map) by 0x4000 >> 16 into 8.6
                    // fixed point: texels = u / 256
                    m.verts.push_back({float(s16(a)), float(s16(a + 2)), float(s16(a + 4)), float(s16(b)),
                                       float(s16(b + 2)), float(s16(b + 4)), uu / 256.0f, vv / 256.0f});
                }
                m.faces.push_back(it->second);
            }
        }
    }
    return m;
}

// ---------------------------------------------------------------- rotation (0x413d98, 0x413e6a, 0x413f37)
// world = Rz(C) Rx(A) Ry(B) v + (X, Y, Z) with the engine's signs (notes-generic.md 6), angles 65536 = one turn;
// column-major 3 x 3.
struct Rot {
    double m[9];
    double row2(double x, double y, double z) const { return m[2] * x + m[5] * y + m[8] * z; }
};

inline Rot rotation(double A, double B, double C) {
    const double k = 2.0 * 3.14159265358979323846 / 65536.0;
    const double sa = std::sin(A * k), ca = std::cos(A * k), sb = std::sin(B * k), cb = std::cos(B * k);
    const double sc = std::sin(C * k), cc = std::cos(C * k);
    // rotY: x' = x cB - z sB, z' = x sB + z cB;  rotX: y' = y cA - z sA, z' = y sA + z cA;
    // rotZ: y' = y cC - x sC, x' = y sC + x cC
    double ry[9] = {cb, 0, sb, 0, 1, 0, -sb, 0, cb};        // columns
    double rx[9] = {1, 0, 0, 0, ca, sa, 0, -sa, ca};
    double rz[9] = {cc, -sc, 0, sc, cc, 0, 0, 0, 1};
    auto mul = [](const double* a, const double* b, double* o) {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r) o[c * 3 + r] = a[r] * b[c * 3] + a[3 + r] * b[c * 3 + 1] + a[6 + r] * b[c * 3 + 2];
    };
    double t[9];
    Rot out;
    mul(rx, ry, t);
    mul(rz, t, out.m);
    return out;
}

}  // namespace generic
