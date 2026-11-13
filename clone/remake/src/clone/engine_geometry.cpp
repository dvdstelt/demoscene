// The 3D engine's CPU side (engine.h): the matrix routines 0x404c9f, 0x404cd6, 0x404d5c, 0x404e36, 0x404f0f, 0x404feb
// and the transform 0x4050e9 (notes-engine 3), the geosphere generator 0x4174c4 / 0x417420 / 0x4172c8 (4), the vertex
// normals 0x416b61 (5), the back-face test and depth key of 0x416929 (6). The original's x87 runs with 64-bit
// mantissas and truncation; here double intermediates and float stores, which give the same geometry to float
// rounding (the int16 geosphere exactly).
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "clone/engine.h"
#include "exe.h"

namespace cmc {

// =============================================================================================== matrices
namespace {
// The pattern of the three rotations: rows a and b mixed by (c, s) of the float angle, all four columns.
void rotRows(float m[12], float ang, int a, int b) {
    float s = float(std::sin(double(ang))), c = float(std::cos(double(ang)));
    for (int k = 0; k < 4; k++) {
        double ra = m[4 * a + k], rb = m[4 * b + k];
        m[4 * a + k] = float(ra * c - rb * s);
        m[4 * b + k] = float(rb * c + ra * s);
    }
}
}  // namespace

Mat34& Mat34::rotX(float a) { rotRows(m, a, 1, 2); return *this; }
Mat34& Mat34::rotY(float a) { rotRows(m, a, 2, 0); return *this; }
Mat34& Mat34::rotZ(float a) { rotRows(m, a, 0, 1); return *this; }

Mat34& Mat34::scale(float sx, float sy, float sz) {
    for (int k = 0; k < 4; k++) {
        m[k] = float(double(m[k]) * sx);
        m[4 + k] = float(double(m[4 + k]) * sy);
        m[8 + k] = float(double(m[8 + k]) * sz);
    }
    return *this;
}

Mat34& Mat34::translate(float tx, float ty, float tz) {
    m[3] = float(double(m[3]) + tx);
    m[7] = float(double(m[7]) + ty);
    m[11] = float(double(m[11]) + tz);
    return *this;
}

Vec3 Mat34::apply(const Vec3& p) const {
    // ((m1 y + m0 x) + m2 z) + m3, each row in extended precision, stored as a float
    auto row = [&](int r) {
        return float(((double(m[4 * r + 1]) * p.y + double(m[4 * r]) * p.x) + double(m[4 * r + 2]) * p.z) +
                     double(m[4 * r + 3]));
    };
    return Vec3(row(0), row(1), row(2));
}

Mat34 Mat34::part(float a, float b, float c, float sy, float tz) {
    Mat34 m;
    m.rotX(a).rotY(b).rotZ(c).scale(1.0f, sy, 1.0f);
    if (tz != 0) m.translate(0, 0, tz);
    return m;
}

void transform(const Mat34& m, std::span<const Vec3> src, std::span<Vec3> dst) {
    size_t n = std::min(src.size(), dst.size());
    for (size_t i = 0; i < n; i++) dst[i] = m.apply(src[i]);
}

// =============================================================================================== the geosphere
namespace {
struct GeoBuilder {
    bool exact = false;
    double radius = 300.0;                      // 0x4172c8 pushes every midpoint out to 300
    std::vector<std::array<int, 3>> iv;         // int16 vertices (integer style)
    std::vector<std::array<double, 3>> dv;      // exact positions
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> edges;   // exact style: midpoint per edge
    std::vector<std::array<uint32_t, 3>> faces;

    // 0x4172c8: the midpoint of i and j on the sphere; shared edges give the same vertex
    uint32_t midpoint(uint32_t i, uint32_t j) {
        if (exact) {
            auto key = std::make_pair(std::min(i, j), std::max(i, j));
            auto it = edges.find(key);
            if (it != edges.end()) return it->second;
            std::array<double, 3> p;
            for (int k = 0; k < 3; k++) p[k] = dv[i][k] + dv[j][k];
            double l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            for (double& c : p) c *= radius / l;
            dv.push_back(p);
            uint32_t n = uint32_t(dv.size() - 1);
            edges[key] = n;
            return n;
        }
        // int16 arithmetic with C's truncations: (a + b) / 2, len = (int)sqrt(...), c = (int)(m * 300 / len)
        std::array<int, 3> m;
        for (int k = 0; k < 3; k++) m[k] = (iv[i][k] + iv[j][k]) / 2;
        int len = int(std::sqrt(double(m[1] * m[1]) + double(m[0] * m[0]) + double(m[2] * m[2])));
        std::array<int, 3> p;
        for (int k = 0; k < 3; k++) p[k] = int(int16_t(len ? m[k] * int(radius) / len : 0));
        for (size_t k = 0; k < iv.size(); k++)
            if (iv[k] == p) return uint32_t(k);
        iv.push_back(p);
        return uint32_t(iv.size() - 1);
    }
    // 0x417420: depth first; the middle triangle continues in the loop and is emitted last
    void subdivide(uint32_t a, uint32_t b, uint32_t c, int n) {
        while (n) {
            uint32_t ab = midpoint(a, b), bc = midpoint(b, c);
            n--;
            uint32_t ca = midpoint(c, a);
            subdivide(a, ab, ca, n);
            subdivide(ab, b, bc, n);
            subdivide(ca, bc, c, n);
            a = ab;
            b = bc;
            c = ca;
        }
        faces.push_back({a, b, c});
    }
};
}  // namespace

Geosphere Geosphere::make(const CloneExe& exe, int levels, bool exact) {
    GeoBuilder g;
    g.exact = exact;
    for (int k = 0; k < 6; k++) {  // 0x427f9c: 6 x 3 dwords, the low 16 bits used
        std::array<int, 3> v;
        for (int c = 0; c < 3; c++) v[c] = int(int16_t(exe.u32(0x427f9c + 12 * k + 4 * c) & 0xffff));
        g.iv.push_back(v);
        g.dv.push_back({double(v[0]), double(v[1]), double(v[2])});
    }
    for (int k = 0; k < 8; k++)  // 0x427f84: 8 faces of 3 vertex indices (bytes)
        g.subdivide(exe.u8(0x427f84 + 3 * k), exe.u8(0x427f84 + 3 * k + 1), exe.u8(0x427f84 + 3 * k + 2),
                    std::max(levels, 0));
    Geosphere s;
    if (exact)
        for (const auto& p : g.dv) s.verts.emplace_back(float(p[0]), float(p[1]), float(p[2]));
    else
        for (const auto& p : g.iv) s.verts.emplace_back(float(p[0]), float(p[1]), float(p[2]));
    s.faces = std::move(g.faces);
    return s;
}

EngineMesh Geosphere::mesh(Tex map, double uvScale) const {
    EngineMesh m;
    m.pos.assign(verts.size(), Vec3());
    m.shade = Shade::Normals;
    m.map = map;
    m.faces.reserve(faces.size());
    for (const auto& f : faces) {
        EngineFace e;
        for (int i = 0; i < 3; i++) {
            e.v[i] = f[size_t(i)];
            e.uv[i][0] = float(verts[f[size_t(i)]].x * uvScale);
            e.uv[i][1] = float(verts[f[size_t(i)]].y * uvScale);
        }
        m.faces.push_back(e);
    }
    return m;
}

// =============================================================================================== normals, sort
void vertexNormals(std::span<const Vec3> pos, std::span<const EngineFace> faces, std::vector<Vec3>& out) {
    std::vector<double> acc(pos.size() * 3, 0.0);
    for (const EngineFace& f : faces) {
        if (f.v[0] >= pos.size() || f.v[1] >= pos.size() || f.v[2] >= pos.size()) continue;
        const Vec3 &p0 = pos[f.v[0]], &p1 = pos[f.v[1]], &p2 = pos[f.v[2]];
        double ax = double(p0.x) - p1.x, ay = double(p0.y) - p1.y, az = double(p0.z) - p1.z;
        double bx = double(p2.x) - p1.x, by = double(p2.y) - p1.y, bz = double(p2.z) - p1.z;
        double n[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
        for (uint32_t v : f.v)
            for (int k = 0; k < 3; k++) acc[size_t(v) * 3 + size_t(k)] += n[k];
    }
    out.resize(pos.size());
    for (size_t i = 0; i < pos.size(); i++) {
        double x = acc[i * 3], y = acc[i * 3 + 1], z = acc[i * 3 + 2], l = std::sqrt(x * x + y * y + z * z);
        double r = l > 0 ? 1.0 / l : 0.0;
        out[i] = Vec3(float(x * r), float(y * r), float(z * r));
    }
}

FaceSort sortFace(const Vec3& v0, const Vec3& v1, const Vec3& v2) {
    FaceSort s;
    // (v1.y v2.x - v1.x v2.y) v0.z > v0.x (v1.y v2.z - v1.z v2.y) - v0.y (v1.x v2.z - v1.z v2.x): det < 0
    double st0 = (double(v1.y) * v2.x - double(v1.x) * v2.y) * v0.z;
    double st1 = double(v0.x) * (double(v1.y) * v2.z - double(v1.z) * v2.y) -
                 double(v0.y) * (double(v1.x) * v2.z - double(v1.z) * v2.x);
    s.hidden = st0 > st1;
    // trunc((z0 + z1 + z2) * 0.2) with the double 0.2 at 0x41a4b8 (a code constant: the bucket width 5/3 of mean z)
    double k = (double(v0.z) + v1.z + v2.z) * 0.2;
    s.key = k >= double(kBuckets) ? kBuckets : k < 0 ? 0 : int(k);
    s.cut = k >= double(kBuckets);
    return s;
}

// =============================================================================================== the window
Window Window::at(const Ctx& c, double t) {
    double e7 = c.lastEvent(7, t), e10 = c.lastEvent(10, t);
    return e7 > e10 ? full() : band();
}

}  // namespace cmc
