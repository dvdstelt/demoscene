// Vivid Experiment's 3D engine files and spline, as the water scene reads them (and the generic scene can): the
// ini reader 0x412938 / 0x4129ec, the .m3d loader 0x412bf0 (with the mapping scale 0x413330), the .spl reader
// 0x4133f8 / 0x413520 / 0x4137f8 and the natural cubic spline 0x413870 (set-up) / 0x413b90 (evaluation), the latter
// in doubles instead of 16.16 fixed point (vividexp/re/notes-water.md 2, 5, 6.1; notes-generic.md 2, 3, 5.1).
//
// Header-only, so that a build of only some scenes (VIVID_PARTS) gets it with every scene that includes it.
//
//   vivid3d::Ini ini(exe.file("water/water.ini"));       ini.str(key), ini.integer(key, default)
//   vivid3d::M3d mesh = vivid3d::parseM3d(exe.file(name));   vertices, centres, normals, groups of faces
//   vivid3d::Spl spl = vivid3d::parseSpl(exe.file(name));    rows of integers; spl.column(c)
//   vivid3d::NaturalSpline s(spl.column(0));             s(2.5): the value 2.5 keys along (extrapolated outside)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace vivid3d {

namespace detail {
// The file's lines, split at CR and LF.
inline std::vector<std::string> lines(std::span<const uint8_t> f) {
    std::vector<std::string> out;
    std::string cur;
    for (uint8_t b : f) {
        if (b == '\r' || b == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(char(b));
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
inline size_t skipBlanks(const std::string& s, size_t p = 0) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    return p;
}
}  // namespace detail

// ---------------------------------------------------------------- ini (0x412938 string, 0x4129ec integer)
// `Key=value` text. A key is found at the start of a line after spaces and tabs, case-sensitive and directly followed
// by '='; lines starting with '#' are skipped and "@end" ends the search. The value runs to the end of the line,
// trailing spaces cut. Integers are atoi (0x41b12e) of the value, or the caller's default when the key is absent.
class Ini {
public:
    Ini() = default;
    explicit Ini(std::span<const uint8_t> file) : lines_(detail::lines(file)) {}

    std::optional<std::string> str(const std::string& key) const {
        for (const std::string& l : lines_) {
            size_t a = detail::skipBlanks(l);
            if (a < l.size() && l[a] == '#') continue;
            if (l.compare(a, 4, "@end") == 0) break;
            if (l.compare(a, key.size(), key) != 0 || a + key.size() >= l.size() || l[a + key.size()] != '=') continue;
            std::string v = l.substr(a + key.size() + 1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            return v;
        }
        return std::nullopt;
    }
    int integer(const std::string& key, int def) const {
        std::optional<std::string> v = str(key);
        return v ? std::atoi(v->c_str()) : def;
    }

private:
    std::vector<std::string> lines_;
};

// ---------------------------------------------------------------- .m3d (0x412bf0)
// Little-endian: "$M3D", flags (bit 0 vertices carry u, v; bit 1 faces carry normal indices; bit 2 normals present),
// vertex, normal and group counts, then the offsets of the vertices, the group centres, the normals and the groups.
// Vertex: s16 x, y, z (+ u16 u, v). Centre: s16 x, y, z per group (at least one). Normal: s16 x, y, z of length
// 16384. Group: s16 face count, u8 colour, u8 pad, then per face u16 v0, v1, v2 (+ u16 n0, n1, n2: normal indices,
// a numbering of their own). Mapping coordinates: 0..65535 is one turn of a 256-texel map, so texel = u / 256 (the
// engine keeps u >> 2 in 10.6 fixed point, 0x413330).
struct M3d {
    struct Vertex {
        int16_t x = 0, y = 0, z = 0;
        uint16_t u = 0, v = 0;
    };
    struct Face {
        std::array<uint16_t, 3> v{}, n{};
    };
    struct Group {
        uint8_t colour = 0;
        std::vector<Face> faces;
    };
    uint32_t flags = 0;
    std::vector<Vertex> verts;
    std::vector<std::array<int16_t, 3>> centres;
    std::vector<std::array<int16_t, 3>> normals;
    std::vector<Group> groups;
};

inline M3d parseM3d(std::span<const uint8_t> f) {
    auto u16 = [&](size_t o) -> uint16_t {
        if (o + 2 > f.size()) throw std::runtime_error("m3d: truncated");
        return uint16_t(f[o] | f[o + 1] << 8);
    };
    auto s16 = [&](size_t o) { return int16_t(u16(o)); };
    auto u32 = [&](size_t o) { return uint32_t(u16(o)) | uint32_t(u16(o + 2)) << 16; };
    if (f.size() < 36 || std::memcmp(f.data(), "$M3D", 4) != 0) throw std::runtime_error("m3d: not an .m3d file");
    M3d m;
    m.flags = u32(4);
    const uint32_t nv = u32(8), nn = u32(12), ng = u32(16);
    const uint32_t voff = u32(20), coff = u32(24), noff = u32(28), goff = u32(32);
    const bool uv = m.flags & 1, nidx = m.flags & 2;
    const size_t vstride = uv ? 10 : 6;
    for (uint32_t i = 0; i < nv; ++i) {
        size_t o = voff + i * vstride;
        M3d::Vertex v{s16(o), s16(o + 2), s16(o + 4), 0, 0};
        if (uv) { v.u = u16(o + 6); v.v = u16(o + 8); }
        m.verts.push_back(v);
    }
    for (uint32_t i = 0; i < std::max<uint32_t>(ng, 1); ++i)
        m.centres.push_back({s16(coff + 6 * i), s16(coff + 6 * i + 2), s16(coff + 6 * i + 4)});
    if (m.flags & 4)
        for (uint32_t i = 0; i < nn; ++i) m.normals.push_back({s16(noff + 6 * i), s16(noff + 6 * i + 2), s16(noff + 6 * i + 4)});
    size_t o = goff;
    for (uint32_t g = 0; g < ng; ++g) {
        M3d::Group grp;
        const int nf = s16(o);
        grp.colour = f[o + 2];
        o += 4;
        for (int i = 0; i < nf; ++i) {
            M3d::Face face;
            for (int k = 0; k < 3; ++k) face.v[size_t(k)] = u16(o + 2 * size_t(k));
            o += 6;
            if (nidx) {
                for (int k = 0; k < 3; ++k) face.n[size_t(k)] = u16(o + 2 * size_t(k));
                o += 6;
            }
            for (int k = 0; k < 3; ++k)
                if (face.v[size_t(k)] >= nv || (nidx && face.n[size_t(k)] >= m.normals.size()))
                    throw std::runtime_error("m3d: face index out of range");
            grp.faces.push_back(face);
        }
        m.groups.push_back(std::move(grp));
    }
    return m;
}

// ---------------------------------------------------------------- .spl (0x4133f8, 0x413520, 0x4137f8)
// Text: rows of integers (one column per value), headers "@width N", "@length N", "@type ...", "@end" anywhere,
// '#' comment lines. The reader skips lines starting with '#' or '@' and anything before a row's first digit or sign.
struct Spl {
    int width = 0;                       // @width (0 if absent)
    std::vector<std::vector<long>> rows;
    std::vector<double> column(size_t c) const {
        std::vector<double> v;
        for (const auto& r : rows) v.push_back(c < r.size() ? double(r[c]) : 0.0);
        return v;
    }
};

inline Spl parseSpl(std::span<const uint8_t> f) {
    Spl s;
    for (const std::string& l : detail::lines(f)) {
        size_t a = detail::skipBlanks(l);
        if (a >= l.size() || l[a] == '#') continue;
        if (l[a] == '@') {
            if (l.compare(a, 6, "@width") == 0) s.width = std::atoi(l.c_str() + a + 6);
            continue;
        }
        size_t p = l.find_first_of("0123456789-+", a);
        if (p == std::string::npos) continue;
        std::vector<long> row;
        const char* c = l.c_str() + p;
        for (;;) {
            char* e = nullptr;
            long v = std::strtol(c, &e, 10);
            if (e == c) break;
            row.push_back(v);
            c = e;
        }
        if (!row.empty()) s.rows.push_back(std::move(row));
    }
    return s;
}

// ---------------------------------------------------------------- natural cubic spline (0x413870 / 0x413b90)
// Numerical Recipes' spline() / splint() through values y[i] at the knots i = 0 .. n-1, with natural ends (second
// derivative 0 at both). The original runs it in 16.16 on value << 8; here in doubles on the values. Outside
// [0, n-1] the end segment's cubic is extrapolated, as the original's search keeps the end segment.
class NaturalSpline {
public:
    NaturalSpline() = default;
    explicit NaturalSpline(std::vector<double> y) : y_(std::move(y)), y2_(y_.size(), 0.0) {
        const size_t n = y_.size();
        if (n < 3) return;
        std::vector<double> u(n, 0.0);
        for (size_t i = 1; i + 1 < n; ++i) {   // unit knot spacing: sig = 1/2
            const double p = 0.5 * y2_[i - 1] + 2.0;
            y2_[i] = -0.5 / p;
            const double d = (y_[i + 1] - y_[i]) - (y_[i] - y_[i - 1]);
            u[i] = (3.0 * d - 0.5 * u[i - 1]) / p;
        }
        y2_[n - 1] = 0.0;
        for (size_t k = n - 1; k-- > 0;) y2_[k] = y2_[k] * y2_[k + 1] + u[k];
    }
    size_t size() const { return y_.size(); }
    double operator()(double s) const {
        const size_t n = y_.size();
        if (n == 0) return 0.0;
        if (n == 1) return y_[0];
        const double lo = std::clamp(std::floor(s), 0.0, double(n - 2));
        const size_t k = size_t(lo);
        const double b = s - lo, a = 1.0 - b;
        return a * y_[k] + b * y_[k + 1] + ((a * a * a - a) * y2_[k] + (b * b * b - b) * y2_[k + 1]) / 6.0;
    }

private:
    std::vector<double> y_, y2_;
};

}  // namespace vivid3d
