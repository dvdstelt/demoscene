// The 3D engine's data (engine.h): the per-part options (notes-engine 12), object records (3.1: p2 1810, p3 1810,
// p5 1412, p6 1620, p7 1810) and meshes (3.2) read from the part images, env maps, the bump tables (7.4: the data
// at p2 / p3 / p7 0010:0000, the generators p5 3099 and p6 3913 as continuous offsets) and bump maps with the
// fillers' texel addressing (7.3: `shld ebx, ecx, k`), and the float form of the vertex transform (5.1, 5.2 and
// 11.1: the matrix 1d86 and the vertex loop 1c60 / 1b60 of p2 and their copies) on the CPU, with the integer screen
// positions and the 16-bit facing test of 3377 (5.2, 5.4).
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "dope/engine.h"
#include "exe.h"

namespace dope {

// =============================================================================================== per-part options
EnginePart EnginePart::of(int part) {
    EnginePart p;
    p.objects = 0x1810, p.envTable = 0x0400, p.texTable = 0x0408, p.bumpTable = 0x0000;
    switch (part) {
    case 2: p.texShift = 9; p.rowPhase = 0; break;
    case 3: p.texSource = TexSource::Normal; p.texShift = 8; break;
    case 5:
        p.texShift = 10, p.pivotAfter = true;
        p.objects = 0x1412, p.envTable = 0x0000, p.texTable = 0x0008, p.bumpTable = -1;
        break;
    case 6:
        p.texShift = 10, p.envNegate = true;
        p.objects = 0x1620, p.envTable = 0x0210, p.texTable = 0x0218, p.bumpTable = -1;
        break;
    case 7: p.texSource = TexSource::Normal; p.texShift = 8; break;
    default: break;
    }
    return p;
}

std::array<uint16_t, 4> segmentTable(const DopeImage& img, uint16_t off) {
    std::array<uint16_t, 4> s{};
    for (int i = 0; i < 4; i++) s[size_t(i)] = img.cs16(uint16_t(off + 2 * i));
    return s;
}

// =============================================================================================== records
ObjectRecord ObjectRecord::read(const DopeImage& img, uint16_t addr) {
    ObjectRecord r;
    auto w = [&](int o) { return img.cs16(uint16_t(addr + o)); };
    auto sw = [&](int o) { return double(img.css16(uint16_t(addr + o))); };
    auto sd = [&](int o) { return double(img.css32(uint16_t(addr + o))); };
    r.addr = addr;
    r.seg = w(0x00);
    r.off = w(0x02) != 0;
    r.f = img.css16(uint16_t(addr + 0x04));
    r.pose = Pose{sd(0x06), sd(0x0a), sd(0x0e), double(w(0x1e)), double(w(0x20)), double(w(0x22))};
    r.vel = Pose{sd(0x12), sd(0x16), sd(0x1a), sw(0x24), sw(0x26), sw(0x28)};
    r.envAdd = w(0x2a) & 0xff;
    r.texAdd = w(0x2a) >> 8;
    r.cullFlip = w(0x2c) != 0;
    for (int i = 0; i < 3; i++) r.pivot[i] = sd(0x2e + 4 * i);
    return r;
}

std::vector<ObjectRecord> readObjectList(const DopeImage& img, uint16_t first) {
    std::vector<ObjectRecord> list;
    for (uint32_t a = first; a + 0x3a <= 0x10000; a += 0x3a) {
        if (img.cs16(uint16_t(a)) == 0) break;
        list.push_back(ObjectRecord::read(img, uint16_t(a)));
    }
    return list;
}

Object Object::from(const ObjectRecord& r, const EngineMesh* m) {
    Object o;
    o.mesh = m;
    o.pose = r.pose;
    o.f = r.f;
    for (int i = 0; i < 3; i++) o.pivot[i] = r.pivot[i];
    o.cullFlip = r.cullFlip;
    o.envAdd = r.envAdd;
    o.texAdd = r.texAdd;
    return o;
}

// =============================================================================================== meshes
bool EngineMesh::load(const DopeImage& img, uint16_t s) {
    verts.clear();
    faces.clear();
    seg = s;
    try {
        int nv = img.u16(s, 0), nf = img.u16(s, 2);
        uint32_t base = DopeImage::addr(s, 0);
        uint32_t oPos = base + img.u16(s, 4), oFace = base + img.u16(s, 6), oNrm = base + img.u16(s, 8),
                 oTex = base + img.u16(s, 10);
        auto s16 = [&](uint32_t a) { return int16_t(img.file16(a)); };
        verts.resize(size_t(nv));
        for (int i = 0; i < nv; i++) {
            Vertex& v = verts[size_t(i)];
            uint32_t p = oPos + 6u * uint32_t(i), n = oNrm + 6u * uint32_t(i), t = oTex + 4u * uint32_t(i);
            v.x = s16(p), v.y = s16(p + 2), v.z = s16(p + 4);
            v.nx = s16(n), v.ny = s16(n + 2), v.nz = s16(n + 4);
            v.lo = img.file16(t), v.hi = img.file16(t + 2);
        }
        faces.resize(size_t(nf));
        for (int j = 0; j < nf; j++) {
            uint32_t f = oFace + 8u * uint32_t(j);
            Face& fc = faces[size_t(j)];
            fc.attr = img.file16(f);
            fc.a = uint16_t(img.file16(f + 2) / 4), fc.b = uint16_t(img.file16(f + 4) / 4),
            fc.c = uint16_t(img.file16(f + 6) / 4);
            if (fc.a >= nv || fc.b >= nv || fc.c >= nv) throw std::out_of_range("face corner");
        }
    } catch (const std::out_of_range&) {
        verts.clear();
        faces.clear();
        return false;
    }
    if (vbo_) {   // reloaded: upload again on the next draw
        gl::DeleteBuffers(1, &vbo_);
        gl::DeleteBuffers(1, &ibo_);
        gl::DeleteVertexArrays(1, &vao_);
        vao_ = vbo_ = ibo_ = 0;
    }
    return !verts.empty();
}

EngineMesh::~EngineMesh() {
    if (vbo_) gl::DeleteBuffers(1, &vbo_);
    if (ibo_) gl::DeleteBuffers(1, &ibo_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
}

void EngineMesh::upload() const {
    // per vertex: position, normal, texture words (hi, lo); faces grouped by attr (stable: mesh order kept)
    std::vector<float> v;
    v.reserve(verts.size() * 8);
    for (const Vertex& p : verts) {
        const float d[8] = {float(p.x),  float(p.y),  float(p.z),  float(p.nx),
                            float(p.ny), float(p.nz), float(p.hi), float(p.lo)};
        v.insert(v.end(), d, d + 8);
    }
    std::vector<uint32_t> order(faces.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = uint32_t(i);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return faces[a].attr < faces[b].attr; });
    std::vector<uint32_t> idx;
    idx.reserve(faces.size() * 3);
    groups_.clear();
    for (uint32_t i : order) {
        const Face& f = faces[i];
        if (groups_.empty() || groups_.back().attr != f.attr) groups_.push_back({f.attr, int(idx.size()), 0});
        idx.push_back(f.a);
        idx.push_back(f.b);
        idx.push_back(f.c);
        groups_.back().count += 3;
    }
    gl::GenVertexArrays(1, &vao_);
    gl::BindVertexArray(vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * 4), v.data(), GL_STATIC_DRAW);
    gl::GenBuffers(1, &ibo_);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    for (GLuint i = 0; i < 3; i++) gl::EnableVertexAttribArray(i);
    gl::BindVertexArray(0);
}

// =============================================================================================== env maps
EnvMap EnvMap::picture(const Picture& p, int half) {
    EnvMap e;
    e.tex = p.tex.id;
    e.texW = p.tex.w, e.texH = p.tex.h;
    if (half == 0 || half == 1) {
        e.u0 = 0.5f * float(half);
        e.u1 = e.u0 + 0.5f;
    }
    return e;
}

EnvMap EnvMap::levelMap(unsigned tex, int w, int h, float x, float y, float size, const PaletteTexture* pal,
                        float base, float scale) {
    EnvMap e;
    e.tex = tex;
    e.texW = w, e.texH = h;
    e.u0 = x / float(w);
    e.u1 = (x + size) / float(w);
    e.v0 = y / float(h);
    e.v1 = (y + size) / float(h);
    e.levels = pal;
    e.levelBase = base;
    e.levelScale = scale;
    return e;
}

static void uploadRgba16f(Texture& t, int w, int h, const std::vector<float>& px, bool repeat) {
    if (t.id && t.w == w && t.h == h) {
        gl::BindTexture(GL_TEXTURE_2D, t.id);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
        gl::GenerateMipmap(GL_TEXTURE_2D);
        return;
    }
    t.destroy();
    t.createRaw(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, px.data(), true, repeat, true);
}

void EnvTexture::set(std::span<const uint8_t> indices, int w, int h, int stride, const Palette& pal) {
    std::vector<float> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t k = size_t(y) * size_t(stride) + size_t(x);
            const Vec3& c = pal[k < indices.size() ? indices[k] : 0];
            float* o = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
            o[0] = c.x, o[1] = c.y, o[2] = c.z, o[3] = 1.0f;
        }
    uploadRgba16f(tex, w, h, px, false);
}

void EnvTexture::setColours(std::span<const Vec3> rgb, int w, int h) {
    std::vector<float> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < size_t(w) * size_t(h) && i < rgb.size(); i++) {
        px[i * 4 + 0] = rgb[i].x, px[i * 4 + 1] = rgb[i].y, px[i * 4 + 2] = rgb[i].z, px[i * 4 + 3] = 1.0f;
    }
    uploadRgba16f(tex, w, h, px, false);
}

EnvMap EnvTexture::map(int half) const {
    EnvMap e;
    e.tex = tex.id;
    e.texW = tex.w, e.texH = tex.h;
    if (half == 0 || half == 1) {
        e.u0 = 0.5f * float(half);
        e.u1 = e.u0 + 0.5f;
    }
    return e;
}

// =============================================================================================== bump maps
BumpOffsets bumpTable(const DopeImage& img, uint16_t off) {
    BumpOffsets o{};
    for (int t = 0; t < 256; t++) {
        int w = img.css16(uint16_t(off + 2 * t));
        int dx = ((w + 128) & 255) - 128;   // the low byte as a column offset; its borrow is in the high byte
        o[size_t(t)] = {float(dx), float((w - dx) / 256)};
    }
    return o;
}

BumpOffsets bumpNibbles() {
    BumpOffsets o{};
    for (int t = 0; t < 256; t++) o[size_t(t)] = {float((t & 15) - 8), float((t >> 4) - 8)};
    return o;
}

bool BumpMap::create(const DopeImage& img, uint16_t seg, const BumpOffsets& o, const EnginePart& part, Read read) {
    try {
        return create(img.span(seg, 0, 127 * 256 + 128), 256, o, part, read);
    } catch (const std::out_of_range&) {
        return false;
    }
}

bool BumpMap::create(std::span<const uint8_t> codes, int stride, const BumpOffsets& o, const EnginePart& part,
                     Read read) {
    if (codes.size() < size_t(127 * stride + 128)) return false;
    // 7.3: column = (hi << s) / 256, row = (lo << s) / 256 + phase (s = k - 8); with lowResRows the rows the
    // original reads, (j << s) + phase, form a map of 128 >> s rows addressed by lo / 256
    int s = std::clamp(part.texShift - 8, 0, 2);
    int phase = s == 0 ? 0 : std::clamp(part.rowPhase, 0, (1 << s) - 1);
    bool low = part.lowResRows && s > 0;
    rows = low ? 128 >> s : 128;
    colScale = float(1 << s) / 256.0f;
    rowScale = low ? 1.0f / 256.0f : float(1 << s) / 256.0f;
    rowOffset = low ? 0.0f : float(phase);
    // PerTexel: kSub x (kSub << s) values per texel (square in texture words: a low-resolution row is 2^s times as
    // tall as a column is wide)
    const int sx = read == Read::PerTexel ? kSub : 1;
    const int sy = read == Read::PerTexel ? (low ? kSub << s : kSub) : 1;
    const int w = 128 * sx, h = rows * sy;
    std::vector<float> px(size_t(w) * size_t(h) * 2);
    for (int y = 0; y < h; y++) {
        int r = y / sy;
        int src = low ? (r << s) + phase : r;
        for (int x = 0; x < w; x++) {
            const auto& d = o[codes[size_t(src) * size_t(stride) + size_t(x / sx)]];
            px[(size_t(y) * size_t(w) + size_t(x)) * 2 + 0] = d[0];
            px[(size_t(y) * size_t(w) + size_t(x)) * 2 + 1] = d[1];
        }
    }
    tex.destroy();
    tex.createRaw(w, h, GL_RG16F, GL_RG, GL_FLOAT, px.data(), true, true, true);
    return true;
}

// =============================================================================================== the transform
std::array<double, 9> engineRotation(double a, double b, double c) {
    a = angleRad(std::fmod(a, 65536.0)), b = angleRad(std::fmod(b, 65536.0)), c = angleRad(std::fmod(c, 65536.0));
    double sa = std::sin(a), ca = std::cos(a), sb = std::sin(b), cb = std::cos(b), sc = std::sin(c), cc = std::cos(c);
    // 1d86 without the table and the Q15 products: R = Rz(-c) Rx(-a) Ry(b)
    return {cb * cc - sb * sa * sc,    ca * sc, sb * cc + cb * sa * sc,
            -(cb * sc + sb * sa * cc), ca * cc, -(sb * sc - cb * sa * cc),
            -sb * ca,                  -sa,     cb * ca};
}

void projectObject(const EnginePart& part, const Pose& camera, const Object& o, std::vector<ProjectedVertex>& out) {
    out.clear();
    if (!o.mesh) return;
    Pose p = o.pose + camera;
    std::array<double, 9> R = engineRotation(p.a, p.b, p.c);
    double tx = p.x / 128.0, ty = p.y / 128.0, tz = p.z / 128.0;
    double sign = part.envNegate ? -1.0 : 1.0;
    out.reserve(o.mesh->verts.size());
    for (const EngineMesh::Vertex& v : o.mesh->verts) {
        double x = v.x + o.pivot[0], y = v.y + o.pivot[1], z = v.z + o.pivot[2];
        double V[3];
        for (int i = 0; i < 3; i++) {
            V[i] = R[size_t(i * 3)] * x + R[size_t(i * 3 + 1)] * y + R[size_t(i * 3 + 2)] * z;
            if (part.pivotAfter) V[i] += o.pivot[i] / 32768.0;
        }
        ProjectedVertex q;
        q.vz = V[2];
        q.d = tz - V[2];
        q.x = 160.0 - o.f / 8.0 * (V[0] + tx) / q.d;
        q.y = 99.0 - 3.0 * o.f / 32.0 * (V[1] + ty) / q.d;
        double nx = R[0] * v.nx + R[1] * v.ny + R[2] * v.nz, ny = R[3] * v.nx + R[4] * v.ny + R[5] * v.nz;
        q.eu = 64.0 + sign * nx / 512.0;
        q.ev = 64.0 + sign * ny / 512.0;
        if (part.texSource == EnginePart::TexSource::Mesh) {
            q.thi = v.hi, q.tlo = v.lo;
        } else {
            auto w15 = [&](int n) { return part.wrap15 ? double(((n + 16384) & 32767) - 16384) : double(n); };
            q.thi = w15(v.ny), q.tlo = w15(v.nx);
        }
        out.push_back(q);
    }
}

ScreenInt screenInt(const ProjectedVertex& v) {
    // 1c60: q = Px f / D (idiv, truncated), x = (q >> 3) + ((q >> 2) & 1) + 160; q = 3 (Py f / D),
    // row = (q >> 5) + ((q >> 4) & 1) + 100. In the float form Px f / D = 8 (x - 160), Py f / D = 32 (row - 100) / 3.
    constexpr double kBig = 1e15;   // far beyond any 16-bit value; keeps the conversion defined
    int64_t qx = int64_t(std::trunc(std::clamp(8.0 * (v.x - 160.0), -kBig, kBig)));
    int64_t qy = 3 * int64_t(std::trunc(std::clamp(32.0 * (v.y + 1.0 - 100.0) / 3.0, -kBig, kBig)));
    return {(qx >> 3) + ((qx >> 2) & 1) + 160, (qy >> 5) + ((qy >> 4) & 1) + 100};
}

int overflowFaces(const Object& o, const std::vector<ProjectedVertex>& pv, std::vector<FaceCull>& out) {
    out.assign(o.mesh ? o.mesh->faces.size() : 0, FaceCull::Float);
    if (!o.mesh || pv.size() < o.mesh->verts.size()) return 0;
    thread_local std::vector<ScreenInt> s;
    s.resize(pv.size());
    for (size_t i = 0; i < pv.size(); i++) s[i] = screenInt(pv[i]);
    // scr[g] holds 16-bit words; 3377 subtracts and multiplies them with 16-bit sub / imul
    auto w16 = [](int64_t v) { return int16_t(uint16_t(uint64_t(v) & 0xffff)); };
    int n = 0;
    for (size_t j = 0; j < out.size(); j++) {
        const EngineMesh::Face& f = o.mesh->faces[j];
        const ScreenInt &a = s[f.a], &b = s[f.b], &c = s[f.c];
        int16_t xa = w16(a.x), ya = w16(a.y), xb = w16(b.x), yb = w16(b.y), xc = w16(c.x), yc = w16(c.y);
        int16_t p1 = w16(int32_t(w16(xb - xa)) * int32_t(w16(yc - ya)));
        int16_t p2 = w16(int32_t(w16(xc - xa)) * int32_t(w16(yb - ya)));
        int16_t ax = w16(int32_t(p1) - int32_t(p2));
        int64_t exact = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        bool draw16 = o.cullFlip ? ax < 0 : ax >= 0;
        bool drawExact = o.cullFlip ? exact < 0 : exact >= 0;
        if (draw16 == drawExact) continue;
        out[j] = draw16 ? FaceCull::Force : FaceCull::Drop;
        n++;
    }
    return n;
}

}  // namespace dope
