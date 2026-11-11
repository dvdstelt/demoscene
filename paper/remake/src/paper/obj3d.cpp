// The 3D objects of the original read from PAPER.EXE: loaders 0x3cb7 / 0x39a4, the banner 0x3a8c, normals 0x3dc0,
// neighbours and creases 0x3eeb / 0x3f37 (see obj3d.h).
#include "obj3d.h"

#include <cmath>

#include "demo.h"
#include "exe.h"

namespace paper {

namespace {
constexpr uint32_t kTableColours = 0x3db8;    // 8 colour bytes of the word format (0x3cb7: minus 4)
constexpr uint32_t kCompactColours = 0x3a89;  // colour bytes of the byte format (0x39a4: plus al)
constexpr uint32_t kLight = 0x1e48c;          // light point, 3 x int32

int32_t mul32(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }
int32_t sub32(int32_t a, int32_t b) { return int32_t(uint32_t(a) - uint32_t(b)); }

// 0x3dc0 for one face: n = e1 x e2 with e1 = V1 - V0, e2 = V1 - V2 (32-bit products), len = round(sqrt(n.n)) (x87,
// round to nearest even; 1 if 0), n = (n << 8) / len truncating (cdq before the shift: exact for |n| < 2^24).
void intNormal(const IVec3& a, const IVec3& b, const IVec3& c, int32_t out[3]) {
    const int32_t e1x = sub32(b.x, a.x), e1y = sub32(b.y, a.y), e1z = sub32(b.z, a.z);
    const int32_t e2x = sub32(b.x, c.x), e2y = sub32(b.y, c.y), e2z = sub32(b.z, c.z);
    const int32_t n[3] = {sub32(mul32(e1y, e2z), mul32(e2y, e1z)), sub32(mul32(e1z, e2x), mul32(e2z, e1x)),
                          sub32(mul32(e1x, e2y), mul32(e2x, e1y))};
    const double sq = double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2];  // exact (< 2^53)
    int64_t len = int64_t(std::nearbyint(std::sqrt(sq)));
    if (len == 0) len = 1;
    for (int k = 0; k < 3; k++) out[k] = int32_t((int64_t(n[k]) * 256) / len);
}

// 0x2ad0: n+1 samples of the parabola through (0,a), (1,m), (2,c) with the truncated step.
std::vector<int32_t> curve2ad0(int32_t a, int32_t m, int32_t c, int n) {
    const int32_t step = (65536 / n) >> 7;
    const int32_t B = 4 * m - c - 3 * a, A = a - 2 * m + c;
    std::vector<int32_t> out(size_t(n) + 1);
    for (int k = 0, t = 0; k <= n; k++, t += step) {
        const int32_t att = int32_t(uint32_t(A * t) * uint32_t(t));
        out[size_t(k)] = (((B * t + (att >> 8)) >> 8) + 2 * a) >> 1;
    }
    return out;
}

Object3D allocate(const char* name, int nv, int nf) {
    Object3D o;
    o.name = name;
    o.ivert.resize(size_t(nv));
    o.face.resize(size_t(nf));
    return o;
}

void finish(Object3D& o) {
    o.vert.resize(o.ivert.size());
    for (size_t i = 0; i < o.ivert.size(); i++) o.vert[i] = Vec3d(o.ivert[i]);
    o.setupEdges();
    o.updateNormals();
}
}  // namespace

// ---------------------------------------------------------------- Object3D
void Object3D::setVertices(const std::vector<Vec3d>& v) {
    vert = v;
    updateNormals();
}

void Object3D::setVertices(const std::vector<IVec3>& v, bool intNormals) {
    ivert = v;
    vert.resize(v.size());
    for (size_t i = 0; i < v.size(); i++) vert[i] = Vec3d(v[i]);
    if (intNormals) computeIntNormals();
    updateNormals();
}

void Object3D::updateNormals() {
    normal.resize(face.size());
    for (size_t f = 0; f < face.size(); f++) {
        const Vec3d& a = vert[face[f].v[0]];
        const Vec3d& b = vert[face[f].v[1]];
        const Vec3d& c = vert[face[f].v[2]];
        normal[f] = (b - a).cross(b - c).norm();
    }
}

void Object3D::computeIntNormals() {
    for (Face3D& f : face) intNormal(ivert[f.v[0]], ivert[f.v[1]], ivert[f.v[2]], f.n);
}

void Object3D::setupEdges() {
    computeIntNormals();
    const int nf = int(face.size());
    for (int fi = 0; fi < nf; fi++) {
        Face3D& f = face[size_t(fi)];
        for (int k = 0; k < 3; k++) {
            const uint16_t a = f.v[k], b = f.v[(k + 1) % 3];
            // 0x3f37: first other face (list order) containing the reversed edge b -> a
            int found = -1;
            for (int g = 0; g < nf && found < 0; g++) {
                if (g == fi) continue;
                const Face3D& o = face[size_t(g)];
                for (int q = 0; q < 3; q++)
                    if (o.v[q] == b && o.v[(q + 1) % 3] == a) { found = g; break; }
            }
            if (found < 0) {
                f.nb[k] = uint16_t(fi);
                continue;
            }
            f.nb[k] = uint16_t(found);
            const Face3D& o = face[size_t(found)];
            const int32_t dot = f.n[0] * o.n[0] + f.n[1] * o.n[1] + f.n[2] * o.n[2];
            if (dot <= 10000) f.outline |= uint8_t(1 << k);
        }
    }
}

void Object3D::setSketchAll(uint8_t mask) {
    for (Face3D& f : face) f.sketch = mask;
}

// ---------------------------------------------------------------- loaders
Object3D loadTableObject(const PaperExe& exe, uint32_t entry) {
    const int nv = exe.u8(entry), nf = exe.u8(entry + 1);
    const int32_t ox = exe.s16(entry + 2), oy = exe.s16(entry + 4), oz = exe.s16(entry + 6);
    uint32_t p = exe.u32(entry + 8);
    Object3D o = allocate("table", nv, nf);
    for (int i = 0; i < nv; i++, p += 6)
        o.ivert[size_t(i)] = IVec3(exe.s16(p) + ox, exe.s16(p + 2) + oy, exe.s16(p + 4) + oz);
    for (int i = 0; i < nf; i++, p += 4) {
        Face3D& f = o.face[size_t(i)];
        for (int k = 0; k < 3; k++) f.v[k] = exe.u8(p + uint32_t(k));
        const uint8_t attr = exe.u8(p + 3);
        f.base = uint8_t(exe.u8(kTableColours + (attr >> 4)) - 4);
        f.add = 0x20;
        f.outline = attr & 7;
    }
    finish(o);
    return o;
}

Object3D loadCompactObject(const PaperExe& exe, uint32_t p, uint8_t colourOffset) {
    const int nv = exe.u8(p), nf = exe.u8(p + 1);
    p += 2;
    Object3D o = allocate("compact", nv, nf);
    for (int i = 0; i < nv; i++, p += 3)
        o.ivert[size_t(i)] = IVec3(exe.s8(p) * 8, exe.s8(p + 1) * 8, exe.s8(p + 2) * 8);
    for (int i = 0; i < nf; i++, p += 4) {
        Face3D& f = o.face[size_t(i)];
        for (int k = 0; k < 3; k++) f.v[k] = exe.u8(p + uint32_t(k));
        const uint8_t attr = exe.u8(p + 3);
        f.base = uint8_t(exe.u8(kCompactColours + (attr >> 4)) + colourOffset);
        f.add = 0;
        f.outline = attr & 7;
    }
    finish(o);
    return o;
}

std::vector<int32_t> bannerSag() { return curve2ad0(0, 90, 0, 17); }

Object3D makeBanner() {
    constexpr int kCols = 17, kRows = 11;
    Object3D o = allocate("banner", kCols * kRows, 2 * (kCols - 1) * (kRows - 1));
    // faces in 0x3a8c's order: rows from the top, columns from the left, per cell A = (v, v+1, v+18) and
    // B = (v+18, v+17, v); A: top row -> edge 0 (v->v+1) flagged, last column -> edge 1 (v+1->v+18);
    // B: first column -> edge 1 (v+17->v), bottom row -> edge 0 (v+18->v+17)
    size_t fi = 0;
    for (int r = 0; r < kRows - 1; r++)
        for (int c = 0; c < kCols - 1; c++) {
            const int v = r * kCols + c;
            Face3D& a = o.face[fi++];
            a.v[0] = uint16_t(v); a.v[1] = uint16_t(v + 1); a.v[2] = uint16_t(v + 18);
            a.base = 0x30;
            a.outline = uint8_t((c == kCols - 2 ? 2 : 0) | (r == 0 ? 1 : 0));
            Face3D& b = o.face[fi++];
            b.v[0] = uint16_t(v + 18); b.v[1] = uint16_t(v + 17); b.v[2] = uint16_t(v);
            b.base = 0x30;
            b.outline = uint8_t((c == 0 ? 2 : 0) | (r == kRows - 2 ? 1 : 0));
        }
    // set-up shape: 0x3c3e with the noise table of main's set-up (all zero), i.e. 0x3b69 with W = 0:
    //   X = 0x7bc + u + 2 * ((-64 * v) >> 11), Y = -0x123 + v + 2 * ((-64 * v) >> 11), Z = (u >> 9) - 3 + 2 * ((-80 * v) >> 8)
    const std::vector<int32_t> sag = bannerSag();
    for (int r = 0; r < kRows; r++)
        for (int c = 0; c < kCols; c++) {
            const int32_t u = 128 * c, v = 95 * r;
            const int32_t X = 0x7bc + u + 2 * ((-64 * v) >> 11);
            const int32_t Y = -0x123 + v + 2 * ((-64 * v) >> 11);
            const int32_t Z = (u >> 9) - 3 + 2 * ((-80 * v) >> 8);
            o.ivert[size_t(r * kCols + c)] = IVec3(X, Y + sag[size_t(c)], Z);
        }
    finish(o);
    return o;
}

// ---------------------------------------------------------------- Objects3D
Objects3D::Objects3D(Ctx& c) : Objects3D(*c.exe) {}

Objects3D::Objects3D(const PaperExe& exe) {
    static const char* kNames[] = {"sun", "path", "house", "pole-left", "pole-right", "tree5", "tree6", "tree7",
                                   "banner", "windows", "flower", "pencil"};
    for (int s = 0; s < 11; s++) {
        Object3D o = loadTableObject(exe, kTable + 12 * uint32_t(s));
        if (s == Banner) thirdPole = std::move(o);
        else scene[s] = std::move(o);
    }
    scene[Banner] = makeBanner();
    scene[Pencil] = loadCompactObject(exe, kPencilData, 0);
    for (int s = 0; s < kSlots; s++) scene[s].name = kNames[s];
    thirdPole.name = "pole-third";
    tealPlane = loadCompactObject(exe, kTealPlaneData, kTealPlaneColour);
    tealPlane.name = "teal-plane";
    flockPlane = loadCompactObject(exe, kFlockPlaneData, kFlockPlaneColour);
    flockPlane.name = "flock-plane";
    ilight = IVec3(exe.s32(kLight), exe.s32(kLight + 4), exe.s32(kLight + 8));
    light = Vec3d(ilight);
}

}  // namespace paper
