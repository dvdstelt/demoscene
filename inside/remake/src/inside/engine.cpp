// The SurRender engine's CPU stages (engine.h; inside/re/notes-engine.md): the vector and matrix library
// 0x413510..0x414e5a, objects (the .sro loader 0x410bb0, normals 0x410918 / 0x410a70, bounds 0x410de8, 0x41172c,
// 0x4116c8, 0x4114c8, the grid 0x410f34, billboards 0x415790, 0x41077c), the scene list 0x411154, the render
// pipeline of 0x40c6ac up to the rasterizers: view latch 0x40c4e4, world transforms 0x40c2b8 / 0x40c224, object ->
// camera 0x40c158, bounding box 0x41a5b8, back faces 0x41a458, projection 0x41afa0, face list 0x41a9d4, the 0x1000
// stage 0x41acc4, lighting 0x41aba4 / 0x41b508, billboards 0x4157d4 / 0x415878, the sort 0x40c384 and the near
// clipper 0x41c0e0, and 0x411e80 (project a point).
//
// Everything is evaluated in doubles. With Fidelity::exact the integer steps of the original are kept (positions
// truncated at load and in world space, integer normals, camera z x 128, 1/16-pixel screen positions, integer
// shades, the 4.12 fixed-point clipper, integer keys), in the original's operation order where it decides a
// truncation (the projection's Winograd form); otherwise the same formulas run without the truncations.
#include "inside/engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "inside/engine_impl.h"

namespace inside::sr {

namespace {

int64_t ti(double x) { return int64_t(std::trunc(x)); }                     // float -> int toward zero (0x40ba78)
int64_t tdiv(int64_t a, int64_t b) { return a / b; }                         // C division (idiv)
int64_t s16(int64_t x) { return int64_t(int16_t(uint16_t(uint64_t(x) & 0xffff))); }
int64_t s32(int64_t x) { return int64_t(int32_t(uint32_t(uint64_t(x) & 0xffffffffu))); }

uint32_t rdU32(std::span<const uint8_t> d, size_t o) {
    if (o + 4 > d.size()) throw std::runtime_error("sro: truncated");
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
uint16_t rdU16(std::span<const uint8_t> d, size_t o) {
    if (o + 2 > d.size()) throw std::runtime_error("sro: truncated");
    return uint16_t(d[o] | d[o + 1] << 8);
}

}  // namespace

// ------------------------------------------------------------------------------------------------ vectors, matrices
double V3::len() const { return std::sqrt((y * y + x * x) + z * z); }   // 0x4136fc

M3 M3::operator*(const M3& b) const {
    M3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = (m[i][1] * b.m[1][j] + m[i][0] * b.m[0][j]) + m[i][2] * b.m[2][j];
    return r;
}

V3 M3::operator*(const V3& v) const {
    return {(v.x * m[0][0] + v.y * m[0][1]) + v.z * m[0][2], (v.x * m[1][0] + v.y * m[1][1]) + v.z * m[1][2],
            (v.x * m[2][0] + v.y * m[2][1]) + v.z * m[2][2]};
}

M3 M3::transposed() const {
    M3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = m[j][i];
    return r;
}

M3 rotateX(const M3& a, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    M3 r = a;
    for (auto& w : r.m) {
        double y = w[1], z = w[2];
        w[1] = y * c + z * s;
        w[2] = z * c - y * s;
    }
    return r;
}

M3 rotateY(const M3& a, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    M3 r = a;
    for (auto& w : r.m) {
        double x = w[0], z = w[2];
        w[0] = x * c - z * s;
        w[2] = z * c + x * s;
    }
    return r;
}

M3 rotateZ(const M3& a, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    M3 r = a;
    for (auto& w : r.m) {
        double x = w[0], y = w[1];
        w[0] = x * c + y * s;
        w[1] = y * c - x * s;
    }
    return r;
}

M3 rotX(double a) { return rotateX(M3(), a); }
M3 rotY(double a) { return rotateY(M3(), a); }
M3 rotZ(double a) { return rotateZ(M3(), a); }
M3 euler(double a, double b, double c) { return rotateZ(rotateY(rotateX(M3(), a), b), c); }

M3 look(V3 dir) {
    auto unit = [](V3 v) { double l = v.len(); return l > 0 ? v * (1.0 / l) : v; };
    V3 d = unit(dir);
    V3 r0 = unit(V3(-d.z, 0, d.x));
    V3 r1 = unit(d.cross(r0));
    M3 m;
    const V3 rows[3] = {r0, r1, d};
    for (int i = 0; i < 3; i++) {
        m.m[i][0] = rows[i].x;
        m.m[i][1] = rows[i].y;
        m.m[i][2] = rows[i].z;
    }
    return m;
}

M3 axisAngle(V3 a, double ang) {
    double c = std::cos(ang), s = std::sin(ang), t = 1 - c;
    M3 r;
    r.m[0][0] = t * a.x * a.x + c;       r.m[0][1] = t * a.x * a.y - s * a.z; r.m[0][2] = t * a.x * a.z + s * a.y;
    r.m[1][0] = t * a.x * a.y + s * a.z; r.m[1][1] = t * a.y * a.y + c;       r.m[1][2] = t * a.y * a.z - s * a.x;
    r.m[2][0] = t * a.x * a.z - s * a.y; r.m[2][1] = t * a.y * a.z + s * a.x; r.m[2][2] = t * a.z * a.z + c;
    return r;
}

void toAxisAngle(const M3& r, V3& axis, double& angle) {
    V3 w(r.m[2][1] - r.m[1][2], r.m[0][2] - r.m[2][0], r.m[1][0] - r.m[0][1]);
    double s = 0.5 * w.len(), c = 0.5 * (r.m[0][0] + r.m[1][1] + r.m[2][2] - 1.0);
    angle = std::atan2(s, c);
    axis = s > 0 ? w * (0.5 / s) : V3(1, 0, 0);
}

Raster rasterAt(uint32_t routine) {
    switch (routine) {
        case 0x412db0: return Raster::Tex;
        case 0x414e5c: return Raster::TexShade;
        case 0x412264: return Raster::TexShadeFog;
        case 0x41669c: return Raster::TexBlend;
        case 0x416dc8: return Raster::ShadeBlend;
        case 0x4172e0: return Raster::TexEnv;
        default: break;
    }
    throw std::invalid_argument("not a SurRender rasterizer: " + std::to_string(routine));
}

Fidelity& fidelity() {
    static Fidelity f = [] {
        Fidelity r;
        const char* e = SDL_getenv("INSIDE_ENGINE");
        if (!e || !*e) return r;
        std::string s = e;
        size_t p = 0;
        while (p <= s.size()) {
            size_t q = s.find(',', p);
            std::string w = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
            if (w == "original") r = Fidelity::original();
            else if (w == "affine") r.perspective = false;
            else if (w == "nearest") r.filter = false;
            else if (w == "exact") r.exact = true;
            else if (w.rfind("msaa=", 0) == 0) r.samples = std::max(0, std::atoi(w.c_str() + 5));
            else if (!w.empty()) SDL_Log("INSIDE_ENGINE: unknown setting '%s'", w.c_str());
            if (q == std::string::npos) break;
            p = q + 1;
        }
        SDL_Log("engine: %s, %s, %s, MSAA %d", r.perspective ? "perspective" : "affine",
                r.filter ? "filtered" : "nearest", r.exact ? "exact integers" : "floats", r.samples);
        return r;
    }();
    return f;
}

// ------------------------------------------------------------------------------------------------ objects
void Object::loadSro(std::span<const uint8_t> d, int32_t scale, int material) {
    const bool ex = fidelity().exact;
    // 0x410bb0 zeroes the header: identity rotation, position 0, no parent, flags 0
    *this = Object();
    kind = Kind::Mesh;
    size_t voff = rdU32(d, 4), nv = rdU32(d, 8), nf = rdU32(d, 0x10);
    double s = double(scale) * (1.0 / 65536);
    verts.resize(nv);
    for (size_t i = 0; i < nv; i++) {
        size_t o = voff + 12 * i;
        double x = int16_t(rdU16(d, o)), y = int16_t(rdU16(d, o + 2)), z = int16_t(rdU16(d, o + 4));
        uint16_t u = rdU16(d, o + 6), v = rdU16(d, o + 8);
        Vertex& p = verts[i];
        p.p = ex ? V3(double(ti(x * s)), double(ti(y * s)), double(ti(z * s))) : V3(x * s, y * s, z * s);
        p.u = ex ? double(u >> 4) : u / 16.0;               // 12-bit fractions of the texture
        p.v = ex ? double(-(v >> 4)) : -(v / 16.0);         // flipped vertically
    }
    faces.resize(nf);
    size_t fo = voff + 12 * nv;   // the faces follow the vertices (the header's offset is not read)
    for (size_t i = 0; i < nf; i++) {
        size_t o = fo + 8 * i;
        Face& f = faces[i];
        f.a = int16_t(rdU16(d, o + 2));
        f.b = int16_t(rdU16(d, o + 4));
        f.c = int16_t(rdU16(d, o + 6));
        f.material = material;
        if (f.a < 0 || f.b < 0 || f.c < 0 || size_t(f.a) >= nv || size_t(f.b) >= nv || size_t(f.c) >= nv)
            throw std::runtime_error("sro: bad vertex index");
    }
    computeNormals();
    computeBounds();
}

void Object::setRaster(Raster r) {
    for (Face& f : faces) f.raster = r;
}

void Object::computeNormals() {
    const bool ex = fidelity().exact;
    std::vector<V3> acc(verts.size());
    for (Face& f : faces) {   // 0x410918: (p1 - p0) x (p2 - p0) at length 1024 (factor 1024 if the length <= 1)
        V3 p0 = verts[size_t(f.a)].p, b = verts[size_t(f.b)].p - p0, a = verts[size_t(f.c)].p - p0;
        V3 n(b.y * a.z - b.z * a.y, b.z * a.x - b.x * a.z, b.x * a.y - b.y * a.x);
        double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        double k = l > 1.0 ? 1024.0 / l : 1024.0;
        f.n = ex ? V3(double(ti(n.x * k)), double(ti(n.y * k)), double(ti(n.z * k))) : n * k;
        for (int v : {f.a, f.b, f.c}) acc[size_t(v)] = acc[size_t(v)] + f.n;
    }
    for (size_t i = 0; i < verts.size(); i++) {   // 0x410a70: the faces' sum at length 0.8 x 1024
        V3 s = acc[i];
        double l = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        double k = (l > 1.0 ? 1024.0 / l : 1024.0) * 0.8;
        verts[i].n = ex ? V3(double(ti(s.x * k)), double(ti(s.y * k)), double(ti(s.z * k))) : s * k;
    }
}

void Object::computeBounds() {
    if (verts.empty()) { bmin = bmax = V3(); radius = 0; return; }
    bmin = bmax = verts[0].p;
    double r = 0;
    for (const Vertex& v : verts) {
        bmin = V3(std::min(bmin.x, v.p.x), std::min(bmin.y, v.p.y), std::min(bmin.z, v.p.z));
        bmax = V3(std::max(bmax.x, v.p.x), std::max(bmax.y, v.p.y), std::max(bmax.z, v.p.z));
        r = std::max(r, std::sqrt(v.p.x * v.p.x + v.p.y * v.p.y + v.p.z * v.p.z));
    }
    radius = fidelity().exact ? std::trunc(r) : r;
}

void Object::translate(V3 d) {
    for (Vertex& v : verts) v.p = v.p + d;
    computeBounds();
}

void Object::scale(double sx, double sy, double sz) {
    const bool ex = fidelity().exact;
    auto f = [&](double v, double s) { return ex ? std::trunc(v * double(float(s))) : v * s; };
    for (Vertex& v : verts) v.p = V3(f(v.p.x, sx), f(v.p.y, sy), f(v.p.z, sz));
}

void Object::morph(const Object& a, const Object& b, double t) {
    const bool ex = fidelity().exact;
    double tf = ex ? double(float(t)) : t;
    auto mix = [&](V3 p, V3 q) {
        V3 r = p + (q - p) * tf;
        return ex ? V3(std::trunc(r.x), std::trunc(r.y), std::trunc(r.z)) : r;
    };
    size_t nf = std::min({faces.size(), a.faces.size(), b.faces.size()});
    for (size_t i = 0; i < nf; i++) faces[i].n = mix(a.faces[i].n, b.faces[i].n);
    size_t nv = std::min({verts.size(), a.verts.size(), b.verts.size()});
    for (size_t i = 0; i < nv; i++) {
        verts[i].n = mix(a.verts[i].n, b.verts[i].n);
        verts[i].p = mix(a.verts[i].p, b.verts[i].p);
    }
}

void Object::makeGrid(int nx, int nz, double sx, double sz, int material) {
    // 0x410f34 writes the .sro image (int16 positions, u = v = 0), loaded at scale 1
    *this = Object();
    kind = Kind::Mesh;
    const bool ex = fidelity().exact;
    double hx = ex ? double(tdiv(int64_t(nx) * int64_t(sx), 2)) : nx * sx / 2;
    double hz = ex ? double(tdiv(int64_t(nz) * int64_t(sz), 2)) : nz * sz / 2;
    for (int i = 0; i <= nz; i++)
        for (int j = 0; j <= nx; j++) {
            Vertex v;
            v.p = V3(j * sx - hx, 0, i * sz - hz);
            verts.push_back(v);
        }
    for (int i = 0; i < nz; i++)
        for (int j = 0; j < nx; j++) {
            int r0 = i * (nx + 1) + j, r1 = (i + 1) * (nx + 1) + j;
            Face f;
            f.material = material;
            f.a = r0; f.b = r0 + 1; f.c = r1;
            faces.push_back(f);
            f.a = r1; f.b = r0 + 1; f.c = r1 + 1;
            faces.push_back(f);
        }
    computeNormals();
    computeBounds();
}

void Object::makeBillboard(const Sprite& s) {
    *this = Object();
    kind = Kind::Billboard;
    sprite = s;
}

void Object::lookAt(V3 target) {
    rot = look(target - wpos);
    flags |= kNoParentRot;
}

// ------------------------------------------------------------------------------------------------ scene
void Scene::add(Object& o) {
    if (std::find(list.begin(), list.end(), &o) == list.end()) list.insert(list.begin(), &o);
}

void Scene::remove(Object& o) { list.erase(std::remove(list.begin(), list.end(), &o), list.end()); }

namespace {

// 0x40c224: the chain of the parents' own rotations and positions
void chain(const Object& o, M3& R, V3& P) {
    if (!o.parent) { R = o.rot; P = o.pos; return; }
    M3 Rp;
    V3 Pp;
    chain(*o.parent, Rp, Pp);
    V3 t = Rp * o.pos;
    P = Pp + t;
    R = Rp * o.rot;
}

}  // namespace

void Scene::updateTransforms() {   // 0x40c2b8
    const bool ex = fidelity().exact;
    for (Object* o : list) {
        if (!o->parent || ((o->flags & kNoParentRot) && (o->flags & kNoParentPos))) {
            o->wrot = o->rot;
            o->wpos = o->pos;
            continue;
        }
        M3 R;
        V3 P;
        chain(*o, R, P);
        o->wpos = (o->flags & kNoParentPos) ? o->pos : ex ? V3(std::trunc(P.x), std::trunc(P.y), std::trunc(P.z)) : P;
        o->wrot = (o->flags & kNoParentRot) ? o->rot : R;
    }
}

namespace {

// The view as 0x40c4e4 latches it.
struct View {
    double cx, cy;          // projection centre (pixels of the engine's screen)
    double xs, ys;          // XS, YS: 128 fov W / 320, 128 fov H / 240 (truncated in exact mode)
    double x0, y0, x1, y1;  // viewport, clamped to the buffer
    double W, H;            // buffer size
    double near, far;       // camera z x 128, units
    double ambient;
};

View latch(const Scene& s, bool ex) {
    View v;
    v.W = s.width;
    v.H = s.height;
    v.cx = s.x0 + tdiv(s.x1 - s.x0, 2);
    v.cy = s.y0 + tdiv(s.y1 - s.y0, 2);
    double a = double(s.width) * 32768.0, b = double(s.height) * 32768.0;
    v.xs = a * s.fov * (1.0 / 81920);
    v.ys = b * s.fov * (1.0 / 61440);
    if (ex) { v.xs = std::trunc(v.xs); v.ys = std::trunc(v.ys); }
    v.x0 = std::max(s.x0, 0);
    v.y0 = std::max(s.y0, 0);
    v.x1 = std::min(s.x1, s.width);
    v.y1 = std::min(s.y1, s.height);
    v.near = s.near;
    v.far = s.far;
    v.ambient = s.ambient;
    return v;
}

}  // namespace

bool Scene::project(V3 world, double& x, double& y) const {   // 0x411e80
    if (!camera) return false;
    View v = latch(*this, fidelity().exact);
    V3 p = camera->wrot.transposed() * (world - camera->wpos);
    if (v.near / 256 > p.z) return false;
    x = v.cx + v.xs * p.x / (128 * p.z);
    y = v.cy + v.ys * p.y / (128 * p.z);
    return true;
}

// ------------------------------------------------------------------------------------------------ the pipeline
namespace detail {

namespace {

constexpr uint32_t kOutLeft = 0x10000, kOutRight = 0x20000, kOutTop = 0x40000, kOutBottom = 0x80000,
                   kOutNear = 0x100000;

// A vertex of a face as the rasterizers see it.
struct CV {
    double x = 0, y = 0, z = 0;          // camera space, units (floats)
    int64_t sx = 0, sy = 0;              // exact: screen position, 1/16 pixel
    int64_t zc = 0, px = 0, py = 0;      // exact: camera z x 128, projection numerators / 2048
    double u = 0, v = 0, eu = 0, ev = 0, s1 = 0, s2 = 0;
};

// Per object vertex, this frame.
struct PV {
    CV c;
    uint32_t flags = 0;
    bool used = false;
};

struct Item {
    double key;
    const Object* obj;
    int face;                            // -1: billboard
    size_t pv;                           // first PV of the object in the frame's array
};

struct Builder {
    const Resources& res;
    const Fidelity& f;
    View view;
    bool ex;
    std::vector<PV> pvs;
    std::vector<Item> items;
    std::vector<GpuVertex>* out = nullptr;
    bool blended = false;
    V3 camPos;
    M3 Ct;
    std::vector<const Object*> lights;

    Builder(const Resources& r, const Fidelity& fi) : res(r), f(fi), ex(fi.exact) {}

    // 0x40c158: T = 128 Ct (obj - cam) (truncated in exact mode), M = Ct obj.rot
    void toCamera(const Object& o, M3& M, V3& T) const {
        V3 v = Ct * (o.wpos - camPos);
        T = v * 128.0;
        if (ex) T = V3(std::trunc(T.x), std::trunc(T.y), std::trunc(T.z));
        M = Ct * o.wrot;
    }

    uint32_t screenFlags(double x, double y) const {   // pixels of the engine's screen
        uint32_t fl = 0;
        if (view.x0 > x) fl |= kOutLeft;
        if (!(view.x1 > x)) fl |= kOutRight;
        if (view.y0 > y) fl |= kOutTop;
        if (!(view.y1 > y)) fl |= kOutBottom;
        return fl;
    }

    bool bboxCulled(const Object& o, const M3& M, const V3& T) const {   // 0x41a5b8
        if (o.flags & kNoBBox) return false;
        uint32_t fand = ~0u, forr = 0;
        for (int k = 0; k < 8; k++) {
            V3 c((k & 1) ? o.bmax.x : o.bmin.x, (k & 2) ? o.bmax.y : o.bmin.y, (k & 4) ? o.bmax.z : o.bmin.z);
            V3 p = T + M * (c * 128.0);
            uint32_t fl = 0;
            if (!(p.z > 16384)) {
                fl = kOutNear;
            } else {
                double d = 1.0 / (p.z * 128);
                fl = screenFlags(view.xs * d * p.x + view.cx, view.ys * d * p.y + view.cy);
            }
            fand &= fl;
            forr |= fl;
        }
        return forr != 0 && fand != 0;
    }

    // 0x41a458: which faces are drawn
    void backFaces(const Object& o, std::vector<char>& vis) const {
        vis.assign(o.faces.size(), 0);
        if (o.flags & kNoCull) {
            for (size_t i = 0; i < o.faces.size(); i++) vis[i] = !(o.faces[i].flags & kFaceHidden);
            return;
        }
        V3 eye = o.wrot.transposed() * (camPos - o.wpos);
        for (size_t i = 0; i < o.faces.size(); i++) {
            const Face& fc = o.faces[i];
            if (fc.flags & kFaceDoubleSided) { vis[i] = 1; continue; }
            if (fc.flags & kFaceHidden) continue;
            V3 p = o.verts[size_t(fc.a)].p, n = fc.n;
            vis[i] = ((eye.y - p.y) * n.y + (eye.x - p.x) * n.x) + (eye.z - p.z) * n.z >= 0;
        }
    }

    // 0x41afa0 for the used vertices
    void project(const Object& o, const M3& M, const V3& T, size_t base) {
        if (ex) {
            double c = 1.0 / 256, xsc = view.xs * c, ysc = view.ys * c, P = xsc * 2048, Q = ysc * 2048;
            double m[3][3];
            for (int k = 0; k < 3; k++) {
                m[0][k] = M.m[0][k] * P;
                m[1][k] = M.m[1][k] * Q;
                m[2][k] = M.m[2][k] * 128;
            }
            double cz = -m[2][0] * m[2][1] + T.z;
            double cxx = (16 * T.x) * xsc + (-m[0][0] * m[0][1]);
            double cyy = (16 * T.y) * ysc + (-m[1][0] * m[1][1]);
            double cx16 = view.cx * 16, cy16 = view.cy * 16;
            for (size_t i = 0; i < o.verts.size(); i++) {
                PV& p = pvs[base + i];
                if (!p.used) continue;
                double x = o.verts[i].p.x, y = o.verts[i].p.y, z = o.verts[i].p.z, nxy = -x * y;
                double Z = (((x + m[2][1]) * (y + m[2][0]) + cz) + nxy) + z * m[2][2];
                double X = (((x + m[0][1]) * (y + m[0][0]) + cxx) + nxy) + z * m[0][2];
                double Y = (((y + m[1][0]) * (x + m[1][1]) + cyy) + nxy) + z * m[1][2];
                p.c.zc = ti(Z);
                p.c.px = tdiv(ti(X), 2048);
                p.c.py = tdiv(ti(Y), 2048);
                p.c.z = double(p.c.zc) / 128;
                if (view.near > double(p.c.zc)) {
                    p.flags = kOutNear;
                } else {
                    double q = 2.0 / Z;
                    p.c.sx = ti(X * q + cx16);
                    p.c.sy = ti(Y * q + cy16);
                    p.flags = screenFlags(double(p.c.sx) / 16, double(p.c.sy) / 16);
                }
            }
            return;
        }
        V3 t = T * (1.0 / 128);
        for (size_t i = 0; i < o.verts.size(); i++) {
            PV& p = pvs[base + i];
            if (!p.used) continue;
            V3 c = M * o.verts[i].p + t;
            p.c.x = c.x;
            p.c.y = c.y;
            p.c.z = c.z;
            if (view.near > 128 * c.z) {
                p.flags = kOutNear;
            } else {
                p.flags = screenFlags(view.cx + view.xs * c.x / (128 * c.z), view.cy + view.ys * c.y / (128 * c.z));
            }
        }
    }

    // 0x41acc4: u, v from the camera-space normal and the projection numerators
    void envMap(const Object& o, const M3& M, size_t base) {
        for (size_t i = 0; i < o.verts.size(); i++) {
            PV& p = pvs[base + i];
            if (!p.used) continue;
            V3 n = o.verts[i].n * 2.0;
            double U = (n.y * M.m[0][1] + n.x * M.m[0][0]) + n.z * M.m[0][2];
            double V = (n.x * M.m[1][0] + n.y * M.m[1][1]) + n.z * M.m[1][2];
            if (ex) {
                p.c.u = double(s16(-(ti(U + 2048) & 0xfff) - tdiv(p.c.px, 64)));
                p.c.v = double(s16(tdiv(p.c.py, 64) + (ti(V + 2048) & 0xfff)));
            } else {   // px / 64 = XS xc / 16384 (the camera-space x, not divided by z)
                p.c.u = -(U + 2048) - view.xs * p.c.x / 16384;
                p.c.v = (V + 2048) + view.ys * p.c.y / 16384;
            }
        }
    }

    // 0x41aba4 / 0x41b508
    void light(const Object& o, size_t base) {
        for (size_t i = 0; i < o.verts.size(); i++)
            if (pvs[base + i].used) pvs[base + i].c.s1 = view.ambient;
        for (const Object* L : lights) {
            V3 dd = o.pos - L->pos;   // the own positions (0x41063c)
            double dist0 = std::sqrt(dd.x * dd.x + dd.y * dd.y + dd.z * dd.z);
            if ((ex ? std::trunc(dist0) : dist0) > o.radius + L->range) continue;
            V3 lo = o.wrot.transposed() * (L->wpos - o.wpos);
            double I = L->intensity, R = L->range, IR = I / R;
            for (size_t i = 0; i < o.verts.size(); i++) {
                PV& p = pvs[base + i];
                if (!p.used) continue;
                const Vertex& v = o.verts[i];
                V3 d = lo - v.p;
                double dist = std::sqrt((d.y * d.y + d.x * d.x) + d.z * d.z);
                if (dist > R) continue;
                double dot = (v.n.y * d.y + v.n.x * d.x) + v.n.z * d.z;
                if (0 > dot) continue;
                double q = dot / (dist * 512);
                double a = 1 > dist ? I : I - dist * IR;
                double c = a * q;
                if (c > 0) p.c.s1 = ex ? double(s16(ti(p.c.s1 + c))) : p.c.s1 + c;
                if (p.c.s1 > 992) p.c.s1 = 992;
            }
        }
    }

    void mesh(const Object& o) {
        M3 M;
        V3 T;
        toCamera(o, M, T);
        if (bboxCulled(o, M, T)) return;
        std::vector<char> vis;
        backFaces(o, vis);
        size_t base = pvs.size();
        pvs.resize(base + o.verts.size());
        for (size_t i = 0; i < o.verts.size(); i++) {
            const Vertex& v = o.verts[i];
            CV& c = pvs[base + i].c;
            c.u = v.u;
            c.v = v.v;
            c.eu = v.eu;
            c.ev = v.ev;
            c.s1 = v.shade;
            c.s2 = v.shade2;
        }
        for (size_t i = 0; i < o.faces.size(); i++)
            if (vis[i])
                for (int k : {o.faces[i].a, o.faces[i].b, o.faces[i].c}) pvs[base + size_t(k)].used = true;
        project(o, M, T, base);
        // 0x41a9d4: the face list; the vertices of listed faces are the ones lit and mapped
        std::vector<char> listed(o.verts.size(), 0);
        size_t first = items.size();
        for (size_t i = 0; i < o.faces.size(); i++) {
            if (!vis[i]) continue;
            const Face& fc = o.faces[i];
            const PV &a = pvs[base + size_t(fc.a)], &b = pvs[base + size_t(fc.b)], &c = pvs[base + size_t(fc.c)];
            uint32_t orr = (a.flags | b.flags | c.flags) & 0xff0000, andd = (a.flags & b.flags & c.flags) & 0xff0000;
            if (orr & andd) continue;
            double key;
            if (ex) {
                int64_t k = ti(double(a.c.zc + b.c.zc + c.c.zc) * (1.0 / 3)) - int64_t(o.bias);
                key = double(std::max<int64_t>(0, tdiv(k, 16)));
            } else {
                key = std::max(0.0, ((a.c.z + b.c.z + c.c.z) * 128.0 / 3.0 - o.bias) / 16.0);
            }
            items.push_back({key, &o, int(i), base});
            listed[size_t(fc.a)] = listed[size_t(fc.b)] = listed[size_t(fc.c)] = 1;
        }
        if (items.size() == first) return;
        for (size_t i = 0; i < o.verts.size(); i++) pvs[base + i].used = listed[i] != 0;
        if (o.flags & kEnvMap) envMap(o, M, base);
        if (o.flags & kLit) light(o, base);
    }

    void billboard(const Object& o) {   // 0x4157d4
        M3 M;
        V3 T;
        toCamera(o, M, T);
        if (0 > T.z) return;
        double key = ex ? double(std::max<int64_t>(0, tdiv(ti(o.sprite.bias + T.z), 16)))
                        : std::max(0.0, (o.sprite.bias + T.z) / 16.0);
        size_t base = pvs.size();
        PV p;
        p.c.x = T.x;
        p.c.y = T.y;
        p.c.z = T.z;
        pvs.push_back(p);
        items.push_back({key, &o, -1, base});
    }

    // ---- drawing
    void vertex(const CV& c, int16_t raster, const Face* fc, const Material* m) {
        GpuVertex g{};
        double X, Y, w;
        if (ex) {
            X = double(c.sx) / 16 - 0.5;   // the rasterizers sample the engine's screen at (c + 1, r + 15/16)
            Y = double(c.sy) / 16 - 0.4375;
            w = double(c.zc) / 128;
        } else {
            w = c.z;
            X = view.cx - 0.5 + view.xs * c.x / (128 * w);
            Y = view.cy - 0.4375 + view.ys * c.y / (128 * w);
        }
        g.pos[0] = float((2 * X / view.W - 1) * w);
        g.pos[1] = float((1 - 2 * Y / view.H) * w);
        g.pos[2] = 0;
        g.pos[3] = float(w);
        const TexInfo* t = m && m->texture >= 0 ? &(*res.textures)[size_t(m->texture)] : nullptr;
        double tw = t ? t->w : 256, th = t ? t->h : 256;
        g.uv[0] = float((c.u + 1) * tw / 4096);   // texel = floor((u + 1) 2^wlog / 4096)
        g.uv[1] = float((c.v + 1) * th / 4096);
        g.uv[2] = float((c.eu + 1) / 16);         // the second texture: 256 x 256
        g.uv[3] = float((c.ev + 1) / 16);
        g.lev[0] = float((c.s1 + 1) / 16);        // row = floor((shade + 1) / 16)
        g.lev[1] = float((c.s2 + 1) / 16);
        auto rows = [&](int tab) { return int16_t(tab >= 0 ? (*res.tableRows)[size_t(tab)] : 1); };
        g.a[0] = raster;
        g.a[1] = int16_t(m ? std::max(m->texture, 0) : 0);
        g.a[2] = int16_t(m ? std::max(m->texture2, 0) : 0);
        g.a[3] = int16_t(m ? std::max(m->table1, 0) : 0);
        g.b[0] = int16_t(m ? std::max(m->table2, 0) : 0);
        g.b[1] = rows(m ? m->table1 : -1);
        g.b[2] = rows(m ? m->table2 : -1);
        g.b[3] = int16_t(t ? (t->wlog | t->hlog << 4) : (8 | 8 << 4));
        (void)fc;
        out->push_back(g);
    }

    // 0x41c0e0: the point of the edge s (in front) -> e (behind) at the near plane
    CV clipPoint(const CV& s, const CV& e) const {
        CV r;
        if (ex) {
            int64_t near = int64_t(view.near), t = 0;
            if (s.zc - e.zc != 0) t = ((s.zc - near) * tdiv(int64_t(1) << 30, s.zc - e.zc)) >> 18;
            int64_t K = tdiv(int64_t(1) << 30, near >> 8);
            auto lerpFix = [&](int64_t a, int64_t b) { return ((t * b) >> 12) + (((4096 - t) * a) >> 12); };
            r.sx = ((lerpFix(s.px, e.px) * K) >> 26) + int64_t(view.cx) * 16;
            r.sy = ((lerpFix(s.py, e.py) * K) >> 26) + int64_t(view.cy) * 16;
            auto at = [&](double a, double b) { return double(s16((t * int64_t(b) + (4096 - t) * int64_t(a)) >> 12)); };
            r.u = at(s.u, e.u);
            r.v = at(s.v, e.v);
            r.eu = at(s.eu, e.eu);
            r.ev = at(s.ev, e.ev);
            r.s1 = at(s.s1, e.s1);
            r.s2 = at(s.s2, e.s2);
            r.zc = s32((t * e.zc + (4096 - t) * s.zc) >> 12);
            return r;
        }
        double nz = view.near / 128, t = (s.z - nz) / (s.z - e.z);
        auto at = [&](double a, double b) { return a + (b - a) * t; };
        r.x = at(s.x, e.x);
        r.y = at(s.y, e.y);
        r.z = nz;
        r.u = at(s.u, e.u);
        r.v = at(s.v, e.v);
        r.eu = at(s.eu, e.eu);
        r.ev = at(s.ev, e.ev);
        r.s1 = at(s.s1, e.s1);
        r.s2 = at(s.s2, e.s2);
        return r;
    }

    bool behind(const CV& c) const { return ex ? c.zc < int64_t(view.near) : 128 * c.z < view.near; }
    double depth(const CV& c) const { return ex ? double(c.zc) : c.z; }

    void face(const Item& it) {
        const Object& o = *it.obj;
        const Face& fc = o.faces[size_t(it.face)];
        const Material* m = fc.material >= 0 && size_t(fc.material) < res.materials->size()
                                ? &(*res.materials)[size_t(fc.material)] : nullptr;
        CV v[3] = {pvs[it.pv + size_t(fc.a)].c, pvs[it.pv + size_t(fc.b)].c, pvs[it.pv + size_t(fc.c)].c};
        if (m && m->fixedUV)   // 0x41be3c: the material's coordinates for the face's three vertices
            for (int k = 0; k < 3; k++) {
                v[k].u = m->uv[2 * k];
                v[k].v = m->uv[2 * k + 1];
            }
        int16_t raster = int16_t(fc.raster);
        if (fc.raster == Raster::TexBlend || fc.raster == Raster::ShadeBlend) blended = true;
        auto tri = [&](const CV& a, const CV& b, const CV& c) {
            vertex(a, raster, &fc, m);
            vertex(b, raster, &fc, m);
            vertex(c, raster, &fc, m);
        };
        int n = int(behind(v[0])) + int(behind(v[1])) + int(behind(v[2]));
        if (n == 0) { tri(v[0], v[1], v[2]); return; }
        if (n == 3) return;
        CV a = v[0], b = v[1], c = v[2];
        if (n == 2) {   // sort by z descending: a in front
            if (depth(a) < depth(b)) std::swap(a, b);
            if (depth(b) < depth(c)) std::swap(b, c);
            if (depth(a) < depth(b)) std::swap(a, b);
            CV ab = clipPoint(a, b), ac = clipPoint(a, c);
            tri(a, ab, ac);
        } else {        // c = the vertex behind
            if (behind(a)) std::swap(a, c);
            if (behind(b)) std::swap(b, c);
            CV ac = clipPoint(a, c), bc = clipPoint(b, c);
            tri(a, b, ac);
            tri(b, ac, bc);
        }
    }

    // 0x415878: a screen-aligned rectangle; the columns x0/16 .. x1/16 - 1 sample the bitmap at 16 c + 15
    void sprite(const Item& it) {
        const Object& o = *it.obj;
        const Sprite& sp = o.sprite;
        if (sp.texture < 0 || sp.w <= 0 || sp.h <= 0) return;
        V3 T(pvs[it.pv].c.x, pvs[it.pv].c.y, pvs[it.pv].c.z);
        double z = T.z * 0.0078125;
        if (double(tdiv(int64_t(view.near), 128)) > z || view.far < z) return;
        double X = T.x * sp.scale * (view.xs / 1024) / z, Y = T.y * sp.scale * (view.xs / 1024) / z;   // XS for y too
        double Wd = view.xs * (sp.w * sp.size) / z, Hd = view.ys * (sp.h * sp.size) / z;
        if (!(Wd > 0) || !(Hd > 0)) return;
        double x0 = view.cx * 16 + X - Wd * 0.5, y0 = view.cy * 16 + Y - Hd * 0.5;
        if (ex) {   // the corner from the exact size, the extent truncated
            x0 = std::trunc(x0);
            y0 = std::trunc(y0);
            Wd = std::trunc(Wd);
            Hd = std::trunc(Hd);
        }
        // pixel c covers u = (16 c + 15 - x0) w / Wd at its centre c + 1/2: the rectangle (x0 - 7) / 16 ..
        double qx0 = (x0 - 7) / 16, qx1 = (x0 + Wd - 7) / 16, qy0 = (y0 - 7) / 16, qy1 = (y0 + Hd - 7) / 16;
        const TexInfo& t = (*res.textures)[size_t(sp.texture)];
        int16_t raster = sp.table >= 0 ? kSpriteTable : kSpriteOpaque;
        if (sp.table >= 0) blended = true;
        auto corner = [&](double X_, double Y_, double u, double v) {
            GpuVertex g{};
            g.pos[0] = float(2 * X_ / view.W - 1);
            g.pos[1] = float(1 - 2 * Y_ / view.H);
            g.pos[3] = 1;
            g.uv[0] = float(u);
            g.uv[1] = float(v);
            g.uv[2] = float(sp.w);
            g.uv[3] = float(sp.h);
            g.a[0] = raster;
            g.a[1] = int16_t(sp.texture);
            g.a[3] = int16_t(std::max(sp.table, 0));
            g.b[1] = int16_t(sp.table >= 0 ? (*res.tableRows)[size_t(sp.table)] : 1);
            g.b[2] = 1;
            g.b[3] = int16_t(t.wlog | t.hlog << 4);
            out->push_back(g);
        };
        double w = sp.w, h = sp.h;
        corner(qx0, qy0, 0, 0);
        corner(qx1, qy0, w, 0);
        corner(qx0, qy1, 0, h);
        corner(qx0, qy1, 0, h);
        corner(qx1, qy0, w, 0);
        corner(qx1, qy1, w, h);
    }
};

}  // namespace

void buildFrame(Scene& s, const Resources& res, const Fidelity& f, std::vector<GpuVertex>& out, bool& blended) {
    out.clear();
    blended = false;
    if (!s.camera) return;
    s.updateTransforms();
    Builder b(res, f);
    b.out = &out;
    b.view = latch(s, f.exact);
    b.camPos = s.camera->wpos;
    b.Ct = s.camera->wrot.transposed();
    for (const Object* o : s.list)
        if ((o->flags & kLight) && !(o->flags & kHidden)) b.lights.push_back(o);
    for (const Object* o : s.list) {   // 0x40c0e0: every object's pipeline in list order
        if (o->flags & kHidden) continue;
        if (o->kind == Object::Kind::Mesh) b.mesh(*o);
        else if (o->kind == Object::Kind::Billboard) b.billboard(*o);
    }
    // 0x40c384: key descending; equal keys in list order, then face order
    std::stable_sort(b.items.begin(), b.items.end(), [](const Item& x, const Item& y) { return x.key > y.key; });
    for (const Item& it : b.items) {
        if (it.face >= 0) b.face(it);
        else b.sprite(it);
    }
    blended = b.blended;
}

}  // namespace detail

}  // namespace inside::sr
