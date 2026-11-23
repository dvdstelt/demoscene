// The 3D engine's CPU part (engine.h; reve/re/notes-engine.md): the WLD loader 0x21d440 / 0x21d538 / 0x21d9ac, the
// animation 0x25f3ae as continuous motion, and the render 0x25f5b5's camera, culling, lighting points and orders.
#include "reve/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "exe.h"

namespace reve3d {

namespace {
constexpr double kTurn = 6.283185307179586 / 8192;   // angles: 8192 a turn ((a & 0x1ffe) / 2 into 0x2176c8's 4096)

int32_t rd32(std::span<const uint8_t> d, size_t p) {
    uint32_t v;
    std::memcpy(&v, d.data() + p, 4);
    return int32_t(v);
}
uint16_t rd16(std::span<const uint8_t> d, size_t p) {
    uint16_t v;
    std::memcpy(&v, d.data() + p, 2);
    return v;
}
int32_t wrap32(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
int16_t clamp16(int64_t v) { return int16_t(std::clamp<int64_t>(v, -0x8000, 0x7fff)); }
// 0x2175dc: floor(sqrt) of an unsigned 32-bit value
uint32_t isqrt32(uint32_t x) {
    uint32_t r = uint32_t(std::sqrt(double(x)));
    while (uint64_t(r) * r > x) r--;
    while (uint64_t(r + 1) * (r + 1) <= x) r++;
    return r;
}
// idiv's quotient, clamped to a word as the loader does (the dividends never overflow idiv)
int16_t divClamp(int64_t num, uint32_t den) { return clamp16(num / int64_t(den)); }

M3 rotX(double a) {
    double c = std::cos(a), s = std::sin(a);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
}
M3 rotY(double a) {
    double c = std::cos(a), s = std::sin(a);
    return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
}
M3 rotZ(double a) {
    double c = std::cos(a), s = std::sin(a);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}  // namespace

M3 M3::operator*(const M3& b) const {
    M3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
    return r;
}
V3 M3::operator*(const V3& v) const {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

// ---------------------------------------------------------------------------------------------- WLD
bool World::load(std::span<const uint8_t> d, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    // byte 1; dwords total vertices, total faces, objects, 0, 0
    if (d.size() < 0x15 || d[0] != 1) return fail("WLD: bad header");
    int no = rd32(d, 9);
    size_t p = 0x15;
    objects.clear();
    for (int k = 0; k < no; k++) {
        // byte 0; dwords vertices, faces, x, y, z
        if (p + 0x15 > d.size() || d[p] != 0) return fail("WLD: bad object");
        Object o;
        int nv = rd32(d, p + 1), nf = rd32(d, p + 5);
        for (int i = 0; i < 3; i++) o.pos[i] = rd32(d, p + 9 + 4 * i);
        p += 0x15;
        if (nv <= 0 || nf <= 0 || p + size_t(nf) * 9 + size_t(nv) * 10 > d.size()) return fail("WLD: truncated");
        o.faces.resize(nf);
        for (Face& f : o.faces) {   // byte type, colour, texture; words a, b, c
            f.type = d[p];
            f.colour = d[p + 1];
            f.tex = d[p + 2];
            for (int i = 0; i < 3; i++) f.v[i] = rd16(d, p + 3 + 2 * i);
            p += 9;
        }
        o.verts.resize(nv);
        for (Vertex& v : o.verts) {   // words x, y, z, u, v
            v.x = int16_t(rd16(d, p));
            v.y = int16_t(rd16(d, p + 2));
            v.z = int16_t(rd16(d, p + 4));
            v.u = rd16(d, p + 6);
            v.v = rd16(d, p + 8);
            p += 10;
        }
        for (Face& f : o.faces)
            for (uint16_t i : f.v)
                if (i >= nv) return fail("WLD: vertex index out of range");
        // 0x21d668: face normals (32-bit products, 64-bit sum of squares), added to the face's FIRST vertex only
        std::vector<std::array<int32_t, 3>> acc(nv, {0, 0, 0});
        for (Face& f : o.faces) {
            const Vertex &a = o.verts[f.v[0]], &b = o.verts[f.v[1]], &c = o.verts[f.v[2]];
            auto prod = [](int x, int y) { return wrap32(int64_t(x) * y); };
            int32_t n[3] = {
                wrap32(int64_t(prod(b.y - a.y, c.z - a.z)) - prod(b.z - a.z, c.y - a.y)),
                wrap32(int64_t(prod(b.z - a.z, c.x - a.x)) - prod(b.x - a.x, c.z - a.z)),
                wrap32(int64_t(prod(b.x - a.x, c.y - a.y)) - prod(b.y - a.y, c.x - a.x))};
            uint64_t q = 0;
            for (int32_t v : n) q += uint64_t(int64_t(v) * v);
            uint32_t hi = uint32_t(q >> 32);
            uint32_t len;
            int sh;
            if (hi == 0 || hi == 0xffffffffu) {
                len = isqrt32(uint32_t(q));
                sh = 15;
            } else {
                len = isqrt32(uint32_t(q >> 8));
                sh = 11;
            }
            if (!len) len = 1;
            for (int i = 0; i < 3; i++) {
                f.n[i] = divClamp(int64_t(n[i]) * (int64_t(1) << sh), len);
                acc[f.v[0]][i] = wrap32(int64_t(acc[f.v[0]][i]) + f.n[i]);
            }
        }
        // 0x21d8af: vertex normals, length 32767
        for (int i = 0; i < nv; i++) {
            uint64_t q = 0;
            for (int32_t v : acc[i]) q += uint64_t(int64_t(v) * v);
            uint32_t len = isqrt32(uint32_t(q >> 16));
            if (!len) len = 1;
            for (int k2 = 0; k2 < 3; k2++) o.verts[i].n[k2] = divClamp(int64_t(acc[i][k2]) * 128, len);
        }
        // 0x21d9ac: the bounding box
        int16_t lo[3] = {0x7fff, 0x7fff, 0x7fff}, hi[3] = {-0x7fff, -0x7fff, -0x7fff};
        for (const Vertex& v : o.verts) {
            const int16_t c[3] = {v.x, v.y, v.z};
            for (int i = 0; i < 3; i++) {
                lo[i] = std::min(lo[i], c[i]);
                hi[i] = std::max(hi[i], c[i]);
            }
        }
        for (int k2 = 0; k2 < 8; k2++) {
            o.box[k2][0] = k2 >= 4 ? hi[0] : lo[0];
            o.box[k2][1] = (k2 & 2) ? hi[1] : lo[1];
            o.box[k2][2] = (k2 & 1) ? hi[2] : lo[2];
        }
        objects.push_back(std::move(o));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------- ANI
bool Anim::load(std::span<const uint8_t> d, const World& w, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    // byte 0; byte refreshes per key; dword delta keys; key 0 and the delta keys:
    //   word (unused); dwords camera x, y, z, yaw, pitch, roll; {word object, 6 dwords} ...; word 0xffff
    if (d.size() < 6) return fail("ANI: short");
    per_ = std::max(1, int(d[1]));
    nkeys_ = rd32(d, 2);
    keys_.clear();
    size_t p = 6;
    const int nobj = int(w.objects.size());
    while (p + 0x1a <= d.size() && int(keys_.size()) <= nkeys_) {
        Key k;
        for (int i = 0; i < 6; i++) k.cam[i] = rd32(d, p + 2 + 4 * i);
        p += 0x1a;
        for (;;) {
            if (p + 2 > d.size()) return fail("ANI: truncated key");
            uint16_t o = rd16(d, p);
            if (o == 0xffff) {
                p += 2;
                break;
            }
            if (p + 0x1a > d.size() || o >= nobj) return fail("ANI: bad object record");
            std::array<int32_t, 6> v;
            for (int i = 0; i < 6; i++) v[i] = rd32(d, p + 2 + 4 * i);
            k.objs.push_back({o, v});
            p += 0x1a;
        }
        keys_.push_back(std::move(k));
    }
    if (int(keys_.size()) != nkeys_ + 1) return fail("ANI: missing keys");
    // key 0: the camera's integers (fractions 0), the listed objects' positions (their angle dwords are ignored: the
    // angles start at 0); objects key 0 does not list keep the WLD's position
    IState s;
    for (int i = 0; i < 6; i++) s.cam[i] = int64_t(keys_[0].cam[i]) << 32;
    s.obj.resize(nobj);
    for (int i = 0; i < nobj; i++)
        s.obj[i] = {int64_t(w.objects[i].pos[0]) << 16, int64_t(w.objects[i].pos[1]) << 16,
                    int64_t(w.objects[i].pos[2]) << 16, 0, 0, 0};
    for (auto& [o, v] : keys_[0].objs)
        for (int i = 0; i < 3; i++) s.obj[o][i] = int64_t(v[i]) << 16;
    atKey_.clear();
    atKey_.push_back(std::move(s));
    for (int k = 1; k <= nkeys_; k++) {
        IState n = atKey_.back();
        for (int i = 0; i < 6; i++) n.cam[i] += int64_t(per_) * (int64_t(keys_[k].cam[i]) << 16);
        for (auto& [o, v] : keys_[k].objs)
            for (int i = 0; i < 6; i++) n.obj[o][i] += int64_t(per_) * v[i];
        atKey_.push_back(std::move(n));
    }
    return true;
}

Anim::IState Anim::exact(long m) const {
    if (m <= 0) return atKey_[0];
    long k = (m - 1) / per_;          // the delta key being applied (0-based)
    long j = m - k * per_;            // its steps so far, 1..per
    IState s = atKey_[size_t(k)];
    const Key& key = keys_[size_t(k + 1)];
    for (int i = 0; i < 6; i++) s.cam[i] += j * (int64_t(key.cam[i]) << 16);
    for (auto& [o, v] : key.objs)
        for (int i = 0; i < 6; i++) s.obj[o][i] += j * v[i];
    return s;
}

void Anim::intState(long n, int32_t cam[6], std::vector<std::array<int32_t, 6>>& obj) const {
    const IState s = exact(n % period());
    for (int i = 0; i < 6; i++) cam[i] = int32_t(s.cam[i] >> 32);
    obj.resize(s.obj.size());
    for (size_t o = 0; o < s.obj.size(); o++)
        for (int i = 0; i < 6; i++) obj[o][size_t(i)] = wrap32(s.obj[o][size_t(i)]);
}

State Anim::toState(const IState& s) const {
    State r;
    for (int i = 0; i < 6; i++) r.cam[i] = double(s.cam[i]) / 4294967296.0;
    r.obj.resize(s.obj.size());
    for (size_t o = 0; o < s.obj.size(); o++)
        for (int i = 0; i < 6; i++) r.obj[o][i] = double(s.obj[o][i]) / 65536.0;
    return r;
}

State Anim::at(double n) const {
    n = std::max(n, 0.0);
    const double whole = std::floor(n);
    const double f = n - whole;
    const long P = period();
    const long m = long(std::fmod(whole, double(P)));
    State a = toState(exact(m));
    if (f <= 0 || m + 1 == P) return a;   // the step that re-reads key 0 is a jump: no blend into it
    State b = toState(exact(m + 1));
    for (int i = 0; i < 6; i++) a.cam[i] += f * (b.cam[i] - a.cam[i]);
    for (size_t o = 0; o < a.obj.size(); o++)
        for (int i = 0; i < 6; i++) a.obj[o][i] += f * (b.obj[o][i] - a.obj[o][i]);
    return a;
}

// ---------------------------------------------------------------------------------------------- constants
void Constants::read(const ReveExe& e) {
    nearZ = double(e.s32(0x21e5c5));
    cx = double(e.s32(0x21e5dd));
    cy = double(e.s8(0x21e5fa));
    focal = double(1 << (16 - e.u8(0x21e5d6)));
    farZ = double(e.s32(0x21de0b)) / 32768.0;
    flatDist = double(1 << (e.u8(0x21e799) / 2));
    vertexDist = double(1 << (e.u8(0x21e837) / 2));
    boxColour = e.u8(0x21d402);
}

// ---------------------------------------------------------------------------------------------- one frame
Frame setupFrame(const World& w, const State& s, const Constants& k, bool part4) {
    Frame f;
    // 0x21db9c: the rows of M are the camera's right, up and forward vectors (world z is up)
    const double yaw = s.cam[3] * kTurn, pitch = s.cam[4] * kTurn, roll = s.cam[5] * kTurn;
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const V3 fwd = {cy * cp, sy * cp, sp};
    const V3 right0 = {sy, -cy, 0}, up0 = {-cy * sp, -sy * sp, cp};
    const double cr = std::cos(roll), sr = std::sin(roll);
    for (int i = 0; i < 3; i++) {
        f.camera.m[0][i] = cr * right0[i] - sr * up0[i];
        f.camera.m[1][i] = sr * right0[i] + cr * up0[i];
        f.camera.m[2][i] = fwd[i];
    }
    f.eye = {s.cam[0], s.cam[1], s.cam[2]};
    const M3& M = f.camera;
    struct Sorted {
        double key;
        int vis, obj;
    };
    std::vector<Sorted> vis;
    f.objects.resize(w.objects.size());
    for (size_t i = 0; i < w.objects.size(); i++) {
        const Object& o = w.objects[i];
        ObjectView& v = f.objects[i];
        v.index = int(i);
        const V3 pos = {s.obj[i][0], s.obj[i][1], s.obj[i][2]};
        const double a = s.obj[i][3] * kTurn, b = s.obj[i][4] * kTurn, c = s.obj[i][5] * kTurn;
        const M3 R = rotY(b) * rotZ(c) * rotX(a);                     // 0x21e354
        v.rot = M * R;                                                // 0x21e158: C = M R
        const V3 rel = {pos[0] - f.eye[0], pos[1] - f.eye[1], pos[2] - f.eye[2]};
        v.trans = M * rel;
        // 0x21e9bc: the lighting point, through the rotation of the negated angles (not R's inverse)
        const M3 Rn = rotY(-b) * rotZ(-c) * rotX(-a);
        v.light = Rn * V3{-rel[0], -rel[1], -rel[2]};
        // PART4's boxes (0x25f26e): the rotated box; drawn when its last corner is in front
        for (int c8 = 0; c8 < 8; c8++) {
            V3 p = v.rot * V3{double(o.box[c8][0]), double(o.box[c8][1]), double(o.box[c8][2])};
            v.boxCam[c8] = {p[0] + v.trans[0], p[1] + v.trans[1], p[2] + v.trans[2]};
        }
        v.box = v.boxCam[7][2] > k.nearZ;
        // 0x21e61c / 0x21ddf8: P = M (pos + h), h = -camera / 2: the far cull and the object's sort key
        const V3 half = {pos[0] - 0.5 * f.eye[0], pos[1] - 0.5 * f.eye[1], pos[2] - 0.5 * f.eye[2]};
        const double pz = dot(fwd, half);
        if (pz >= k.farZ) continue;
        // the unrotated box at the object's position: drawn if a corner in front projects inside 0..320 x 0..200
        int codes[8], stored = 0;
        bool inside = false;
        for (int c8 = 0; c8 < 8 && !inside; c8++) {
            V3 q = M * V3{o.box[c8][0] + rel[0], o.box[c8][1] + rel[1], o.box[c8][2] + rel[2]};
            if (q[2] <= k.nearZ) continue;
            double sx = k.cx + k.focal * q[0] / q[2], syy = k.cy - k.focal * q[1] / q[2];
            int code = (sx < 0) | (sx > 2 * k.cx) << 1 | (syy < 0) << 2 | (syy > 2 * k.cy) << 3;
            if (!code) inside = true;
            else codes[stored++] = code;
        }
        if (!inside) {
            if (part4) continue;   // 0x21dfb8: no corner inside, no object
            // 0x21ddf8: culled when the stored outcodes are all equal; with fewer than 8 stored the original compares
            // bytes left from earlier objects (it decided that way only for objects that put no pixel on the screen,
            // notes-engine.md): drawn here
            bool same = stored == 8;
            for (int j = 1; j < stored && same; j++) same = codes[j] == codes[0];
            if (same) continue;
        }
        v.drawn = true;
        vis.push_back({std::floor(pz / 2), int(vis.size()), int(i)});
    }
    // the object list, back to front: keys P.z >> 16 (high word of 1.15 fixed point), ties: the later one first
    std::sort(vis.begin(), vis.end(), [](const Sorted& x, const Sorted& y) {
        return x.key != y.key ? x.key > y.key : x.vis > y.vis;
    });
    for (const Sorted& e : vis) f.drawOrder.push_back(e.obj);
    // per drawn object, its faces back to front: keys = the camera z (truncated) of the face's first vertex, ties:
    // the later face first (0x21e9bc, 0x21e6ec)
    std::vector<std::pair<double, uint32_t>> keys;
    for (int i : f.drawOrder) {
        const Object& o = w.objects[size_t(i)];
        ObjectView& v = f.objects[size_t(i)];
        const double* r2 = v.rot.m[2];
        keys.clear();
        for (uint32_t j = 0; j < o.faces.size(); j++) {
            const Vertex& a = o.verts[o.faces[j].v[0]];
            double z = r2[0] * a.x + r2[1] * a.y + r2[2] * a.z + v.trans[2];
            keys.push_back({std::floor(z), j});
        }
        std::sort(keys.begin(), keys.end(), [](const auto& x, const auto& y) {
            return x.first != y.first ? x.first > y.first : x.second > y.second;
        });
        v.order.resize(keys.size());
        for (size_t j = 0; j < keys.size(); j++) v.order[j] = keys[j].second;
    }
    return f;
}

}  // namespace reve3d
