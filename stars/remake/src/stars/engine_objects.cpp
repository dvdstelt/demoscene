// BARTi's engine, the CPU side (engine.h): the `B!` object loader 0x78c62 and the `WLDD` world of file 28, the
// morph 0x78fda (per tick) / 0x78ffe (blend), the rotation matrix 0x7984e with exact sin / cos, the bounding radius
// 0x79808 (exact sqrt instead of the table 0x79cbf), and the maps the fillers read: 0x7873a's light curve, the
// texture and bump tables of modes 15 / 20 / 21, env maps. Everything is read from STARS.EXE at run time.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "exe.h"
#include "stars/engine.h"

namespace stars {

namespace {
constexpr double kTurn = 6.283185307179586 / 4096.0;  // 12-bit angle unit

uint16_t rd16(std::span<const uint8_t> b, size_t o) { return uint16_t(b[o] | b[o + 1] << 8); }
int16_t rds16(std::span<const uint8_t> b, size_t o) { return int16_t(rd16(b, o)); }
int32_t rds32(std::span<const uint8_t> b, size_t o) {
    return int32_t(uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24);
}

EngineVertex readVertex(std::span<const uint8_t> b, size_t o) {
    EngineVertex v;
    v.x = rds16(b, o);
    v.y = rds16(b, o + 2);
    v.z = rds16(b, o + 4);
    v.nx = rds16(b, o + 6);
    v.ny = rds16(b, o + 8);
    v.nz = rds16(b, o + 10);
    v.u = b[o + 12];
    v.v = b[o + 13];
    return v;
}
}  // namespace

// =============================================================================================== matrices
Mat3d engineRotation(double a, double b, double c) {
    // 0x7984e: ca = COS[a], sa = COS[a - 0x400] (= sin), likewise b, c; every product >> 10 in the original
    double ca = std::cos(a * kTurn), sa = std::sin(a * kTurn);
    double cb = std::cos(b * kTurn), sb = std::sin(b * kTurn);
    double cc = std::cos(c * kTurn), sc = std::sin(c * kTurn);
    Mat3d r;
    r.m[0][0] = cc * cb;
    r.m[0][1] = -sc * cb;
    r.m[0][2] = sb;
    r.m[1][0] = sc * ca + sb * cc * sa;
    r.m[1][1] = cc * ca - sa * sb * sc;
    r.m[1][2] = -sa * cb;
    r.m[2][0] = sc * sa - sb * cc * ca;
    r.m[2][1] = cc * sa + ca * sb * sc;
    r.m[2][2] = ca * cb;
    return r;
}

Vec3 Mat3d::apply(const Vec3& v) const {
    double x = v.x, y = v.y, z = v.z;
    return Vec3(float(x * m[0][0] + y * m[1][0] + z * m[2][0]), float(x * m[0][1] + y * m[1][1] + z * m[2][1]),
                float(x * m[0][2] + y * m[1][2] + z * m[2][2]));
}

Mat3d Mat3d::operator*(const Mat3d& o) const {
    Mat3d r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
    return r;
}

// =============================================================================================== objects
Object3D::Object3D(Object3D&& o) noexcept { *this = std::move(o); }

Object3D& Object3D::operator=(Object3D&& o) noexcept {
    if (this == &o) return *this;
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (vbo_) gl::DeleteBuffers(1, &vbo_);
    verts = std::move(o.verts);
    faces = std::move(o.faces);
    sets = std::move(o.sets);
    data_ = std::move(o.data_);
    radius = o.radius;
    dirty_ = true;
    vao_ = o.vao_;
    vbo_ = o.vbo_;
    count_ = o.count_;
    o.vao_ = o.vbo_ = 0;
    o.count_ = 0;
    return *this;
}

Object3D::~Object3D() {
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (vbo_) gl::DeleteBuffers(1, &vbo_);
}

bool Object3D::load(const StarsExe& exe, int file) {
    if (!parse(exe.fileSpan(file))) {
        SDL_Log("engine: file %d is not a B! object", file);
        return false;
    }
    return true;
}

bool Object3D::parse(std::span<const uint8_t> b) {
    // +0 "B!" 0x1a, +3 0x0f static / 0x00 morphing, +4 nv, +6 nf, vertices and faces of 16 bytes, then the key sets
    if (b.size() < 8 || b[0] != 'B' || b[1] != '!' || b[2] != 0x1a) return false;
    size_t nv = rd16(b, 4), nf = rd16(b, 6);
    size_t vo = 8, fo = vo + 16 * nv, so = fo + 16 * nf;
    if (b.size() < so) return false;
    verts.resize(nv);
    for (size_t i = 0; i < nv; i++) verts[i] = readVertex(b, vo + 16 * i);
    faces.resize(nf);
    for (size_t i = 0; i < nf; i++) {
        size_t o = fo + 16 * i;
        faces[i] = {rd16(b, o), rd16(b, o + 2), rd16(b, o + 4), rd16(b, o + 6)};
        if (faces[i].a >= nv || faces[i].b >= nv || faces[i].c >= nv) return false;
    }
    sets.clear();
    if (b[3] == 0 && nv > 0)
        for (size_t o = so; o + 16 * nv <= b.size(); o += 16 * nv) {
            std::vector<EngineVertex> s(nv);
            for (size_t i = 0; i < nv; i++) s[i] = readVertex(b, o + 16 * i);
            sets.push_back(std::move(s));
        }
    computeRadius();
    dirty_ = true;
    return true;
}

void Object3D::computeRadius() {
    // 0x79808 over the working set (the table square root 0x79cbf truncates; exact here)
    double r = 0;
    for (const EngineVertex& v : verts) r = std::max(r, std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
    radius = float(r);
}

void Object3D::setShape(const MorphState& s) {
    if (sets.empty()) return;
    int n = int(sets.size());
    const auto& A = sets[size_t(std::clamp(s.a, 0, n - 1))];
    const auto& B = sets[size_t(std::clamp(s.b, 0, n - 1))];
    float w = s.w;
    for (size_t i = 0; i < verts.size() && i < A.size(); i++) {
        EngineVertex& o = verts[i];
        const EngineVertex &p = A[i], &q = B[i];
        o.x = p.x + (q.x - p.x) * w;
        o.y = p.y + (q.y - p.y) * w;
        o.z = p.z + (q.z - p.z) * w;
        o.nx = p.nx + (q.nx - p.nx) * w;  // not renormalised (the shader normalises per pixel)
        o.ny = p.ny + (q.ny - p.ny) * w;
        o.nz = p.nz + (q.nz - p.nz) * w;
    }
    dirty_ = true;
}

bool World::load(const StarsExe& exe, int file) {
    // "WLDD": the bee part reads the object count as the word at +3 (68); records of 0x20 bytes from +8
    std::span<const uint8_t> b = exe.fileSpan(file);
    if (b.size() < 8 || std::memcmp(b.data(), "WLDD", 4) != 0) return false;
    int n = rd16(b, 3);
    objects.clear();
    poses.clear();
    objects.reserve(size_t(n));
    for (int i = 0; i < n; i++) {
        size_t r = 8 + 0x20 * size_t(i);
        if (r + 0x20 > b.size()) return false;
        Pose p;
        p.x = rds32(b, r);
        p.y = rds32(b, r + 4);
        p.z = rds32(b, r + 8);
        p.a = rds16(b, r + 0xc);
        p.b = rds16(b, r + 0xe);
        p.c = rds16(b, r + 0x10);
        size_t off = size_t(rds32(b, r + 0x12));
        if (off >= b.size()) return false;
        Object3D o;
        if (!o.parse(b.subspan(off))) return false;
        objects.push_back(std::move(o));
        poses.push_back(p);
    }
    return true;
}

// =============================================================================================== morphing
bool Morph::load(const StarsExe& exe, uint32_t record, uint32_t weightTable, int k) {
    int shift = exe.u8(record + 8);
    stepsPerKey = exe.u8(record + 9);
    keys = k;
    list.clear();
    for (uint32_t a = record + 0xa;; a += 4) {
        int32_t v = exe.s32(a);
        if (v < 0 || list.size() > 4096) break;
        list.push_back(v);
    }
    weights.resize(128);
    for (int i = 0; i < 128; i++) weights[size_t(i)] = std::ldexp(double(exe.s16(weightTable + 2 * uint32_t(i))), -shift);
    if (stepsPerKey <= 0 || stepsPerKey > 128 || keys <= 0 || int(list.size()) < keys + 1) {
        SDL_Log("engine: morph record 0x%x: %d steps, %d keys, %d list entries", record, stepsPerKey, keys,
                int(list.size()));
        return false;
    }
    return true;
}

MorphState Morph::at(double ticks) const {
    if (int(list.size()) < keys + 1 || stepsPerKey <= 0 || int(weights.size()) < stepsPerKey) return {};
    double t = std::max(0.0, ticks);
    long whole = long(std::floor(t));
    double frac = t - double(whole);
    long key = (whole / stepsPerKey) % keys;
    int step = int(whole % stepsPerKey);
    MorphState s;
    s.a = list[size_t(key)];
    s.b = list[size_t(key + 1)];
    double w0 = weights[size_t(step)];
    double w1 = step + 1 < stepsPerKey ? weights[size_t(step + 1)] : 1.0;  // last tick: on to the next key's set
    s.w = float(w0 + (w1 - w0) * frac);
    return s;
}

MorphState Morph::atTick(long ticks) const {
    if (int(list.size()) < keys + 1 || stepsPerKey <= 0 || int(weights.size()) < stepsPerKey) return {};
    long t = std::max(0L, ticks);
    long key = (t / stepsPerKey) % keys;
    MorphState s;
    s.a = list[size_t(key)];
    s.b = list[size_t(key + 1)];
    s.w = float(weights[size_t(t % stepsPerKey)]);
    return s;
}

// =============================================================================================== maps
bool LightRamp::load(const StarsExe& exe, uint32_t addr) {
    values.resize(128);
    for (int i = 0; i < 128; i++) values[size_t(i)] = float(exe.u8(addr + uint32_t(i)));
    tex.createRaw(128, 1, GL_R32F, GL_RED, GL_FLOAT, values.data(), false, false, false);
    return true;
}

float LightRamp::at(float r) const {
    float x = std::clamp(127.0f - r, 0.0f, 127.0f);
    int i = int(x);
    float f = x - float(i);
    return values[size_t(i)] + (values[size_t(std::min(i + 1, 127))] - values[size_t(i)]) * f;
}

bool IndexMap::create(std::span<const uint8_t> b, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h), 0);
    std::memcpy(px.data(), b.data(), std::min(px.size(), b.size()));
    tex.createRaw(w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, px.data(), false, true, false);
    return true;
}

bool BumpMap::create(std::span<const uint8_t> b, float bias, float scale, bool pr) {
    pairs = pr;
    int w = pairs ? 128 : 256;
    std::vector<float> px(size_t(w) * 256 * 2);
    auto at = [&](size_t i) { return i < b.size() ? (float(b[i]) + bias) * scale : bias * scale; };
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < w; x++) {
            size_t i = size_t(y) * 256 + size_t(pairs ? 2 * x : x);
            size_t o = (size_t(y) * size_t(w) + size_t(x)) * 2;
            px[o] = at(i);
            px[o + 1] = at((i + 1) & 0xffff);  // the next byte (u = 255: the next row's first)
        }
    tex.createRaw(w, 256, GL_RG16F, GL_RG, GL_FLOAT, px.data(), true, true, false);
    return true;
}

bool EnvTexture::makeRgb(std::span<const uint8_t> idx, const Palette& pal) {
    Image im;
    im.w = im.h = 256;
    im.rgba.resize(256 * 256 * 4);
    for (size_t i = 0; i < 65536; i++) {
        const Vec3& c = pal[i < idx.size() ? idx[i] : 0];
        im.rgba[4 * i + 0] = uint8_t(std::lround(std::clamp(c.x, 0.0f, 1.0f) * 255.0f));
        im.rgba[4 * i + 1] = uint8_t(std::lround(std::clamp(c.y, 0.0f, 1.0f) * 255.0f));
        im.rgba[4 * i + 2] = uint8_t(std::lround(std::clamp(c.z, 0.0f, 1.0f) * 255.0f));
        im.rgba[4 * i + 3] = 255;
    }
    tex.create(im, true, true);
    return true;
}

bool EnvTexture::makeLevel(std::span<const uint8_t> values) {
    std::vector<uint8_t> px(65536, 0);
    std::memcpy(px.data(), values.data(), std::min(px.size(), values.size()));
    tex.createRaw(256, 256, GL_R8, GL_RED, GL_UNSIGNED_BYTE, px.data(), true, true, true);
    return true;
}

}  // namespace stars
