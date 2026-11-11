#include "assets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

static std::string g_dataDir = ".";

void setDataDir(const std::string& dir) { g_dataDir = dir; }

std::string dataPath(const std::string& name) {
    // The original files are upper case on disk (DOS), but accept any case.
    std::string up = name, lo = name;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
    for (const auto& n : {name, up, lo}) {
        std::string p = g_dataDir + "/" + n;
        if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
    }
    return g_dataDir + "/" + up;
}

Bytes readFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    Bytes b(size_t(n > 0 ? n : 0));
    if (n > 0 && std::fread(b.data(), 1, b.size(), f) != b.size()) { std::fclose(f); throw std::runtime_error("read error " + path); }
    std::fclose(f);
    return b;
}

Bytes unlzw(const uint8_t* data, size_t size, size_t maxOut) {
    Bytes out;
    out.reserve(maxOut ? maxOut : size * 4);
    std::vector<uint16_t> parent(32768);
    std::vector<uint8_t> chr(32768);
    std::vector<uint8_t> stack;
    stack.reserve(32768);
    size_t p = 0;
    uint64_t acc = 0;
    int nacc = 0;
    auto rd = [&](int w, uint32_t& v) -> bool {
        while (nacc < w) {
            if (p >= size) return false;
            acc = (acc << 8) | data[p++];
            nacc += 8;
        }
        nacc -= w;
        v = uint32_t(acc >> nacc) & ((1u << w) - 1);
        return true;
    };
    for (;;) {
        uint32_t next = 259, width = 9, old, cur;
        if (!rd(width, old) || old == 256) break;
        uint8_t ch = uint8_t(old);
        out.push_back(ch);
        bool flushed = false;
        for (;;) {
            if (maxOut && out.size() >= maxOut) { out.resize(maxOut); return out; }
            if (!rd(width, cur) || cur == 256) {
                if (maxOut && out.size() > maxOut) out.resize(maxOut);
                return out;
            }
            if (cur == 258) { flushed = true; break; }
            if (cur == 257) { width++; continue; }
            stack.clear();
            uint32_t c;
            if (cur >= next) { stack.push_back(ch); c = old; }
            else c = cur;
            while (c > 255) { stack.push_back(chr[c]); c = parent[c]; }
            stack.push_back(uint8_t(c));
            ch = uint8_t(c);
            out.insert(out.end(), stack.rbegin(), stack.rend());
            if (next < 32768) { parent[next] = uint16_t(old); chr[next] = ch; next++; }
            old = cur;
        }
        if (!flushed) break;
    }
    if (maxOut && out.size() > maxOut) out.resize(maxOut);
    return out;
}

static uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }

bool Pak::load(const std::string& path) {
    Bytes d = readFile(path);
    if (d.size() < 12 || std::memcmp(d.data(), "ALEX", 4) != 0) return false;
    uint32_t n = rd32(&d[4]), dir = rd32(&d[8]);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* e = &d[dir + 24 * i];
        char name[13] = {};
        std::memcpy(name, e, 12);
        uint32_t off = rd32(e + 12), csz = rd32(e + 16), usz = rd32(e + 20);
        files_[name] = unlzw(&d[off], csz, usz);
    }
    return true;
}

const Bytes& Pak::get(const std::string& name) const {
    auto it = files_.find(name);
    if (it == files_.end()) throw std::runtime_error("pak entry missing: " + name);
    return it->second;
}

std::vector<float> rawPalette(const Bytes& raw) {
    std::vector<float> pal(768);
    for (int i = 0; i < 768; i++) pal[i] = std::min(63, int(raw[i])) / 63.0f;
    return pal;
}

Image imagePaletted(const Bytes& raw, int w, int h, int yOffset, int rows) {
    if (rows < 0) rows = h - yOffset;
    Image im;
    im.w = w;
    im.h = rows;
    im.rgba.resize(size_t(w) * rows * 4);
    const uint8_t* pal = raw.data();
    const uint8_t* px = raw.data() + 768 + size_t(yOffset) * w;
    for (size_t i = 0; i < size_t(w) * rows; i++) {
        int c = px[i];
        for (int k = 0; k < 3; k++) im.rgba[i * 4 + k] = uint8_t(std::min(63, int(pal[c * 3 + k])) * 255 / 63);
        im.rgba[i * 4 + 3] = 255;
    }
    return im;
}

Image imageGray(const Bytes& raw, int w, int h, int yOffset, int rows) {
    if (rows < 0) rows = h - yOffset;
    Image im;
    im.w = w;
    im.h = rows;
    im.rgba.resize(size_t(w) * rows * 4);
    const uint8_t* px = raw.data() + size_t(yOffset) * w;
    for (size_t i = 0; i < size_t(w) * rows; i++) {
        uint8_t v = px[i];
        im.rgba[i * 4 + 0] = im.rgba[i * 4 + 1] = im.rgba[i * 4 + 2] = v;
        im.rgba[i * 4 + 3] = v;
    }
    return im;
}

// ---------------------------------------------------------------- video
namespace {
const int kZigzag[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
const int kZigRow[16] = {0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 6};

struct Idct4 {
    float B[4][4];  // B[u][x]
    Idct4() {
        const double pi = 3.14159265358979323846;
        for (int u = 0; u < 4; u++)
            for (int x = 0; x < 4; x++)
                B[u][x] = float(u == 0 ? 0.5 : std::sqrt(0.5) * std::cos((2 * x + 1) * u * pi / 8.0));
    }
};

// Decode one plane of 4x4 DCT blocks into floats (0..255 range, unclamped precision).
void decodePlane(const uint8_t* src, int w, int h, int q, float* dst) {
    static const Idct4 T;
    uint8_t dcAcc = 0;
    float C[16], tmp[16];
    for (int by = 0; by < h / 4; by++) {
        for (int bx = 0; bx < w / 4; bx++, src += 16) {
            for (int k = 0; k < 16; k++) {
                int v;
                if (k == 0) { dcAcc = uint8_t(dcAcc + src[0]); v = int8_t(dcAcc); }
                else v = int8_t(src[k]);
                // the original stores the dequantised value as a byte; keep that wrap but not the truncation
                double prod = double((kZigRow[k] + 1) * q + 2) * v * 0.2499937501562461;
                double tr = std::trunc(prod);
                int wrapped = int8_t(uint8_t(int(tr) & 0xff));
                C[kZigzag[k]] = float(wrapped + (prod - tr));
            }
            for (int r = 0; r < 4; r++)
                for (int x = 0; x < 4; x++) {
                    float s = 0;
                    for (int u = 0; u < 4; u++) s += T.B[u][x] * C[r * 4 + u];
                    tmp[r * 4 + x] = s;
                }
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    float s = 0;
                    for (int v = 0; v < 4; v++) s += T.B[v][y] * tmp[v * 4 + x];
                    float pix = (s + 32.0f) * 4.0f;
                    dst[(by * 4 + y) * w + bx * 4 + x] = std::clamp(pix, 0.0f, 255.0f);
                }
        }
    }
}
}  // namespace

bool Video::load(const std::string& path, bool lumaOnly) {
    Bytes comp = readFile(path);
    Bytes d = unlzw(comp.data(), comp.size());
    if (d.size() < 1) return false;
    const int q = d[0];
    const size_t fsz = 160 * 96 + 2 * 80 * 48;
    frames = int((d.size() - 1) / fsz);
    if (lumaOnly) {
        // greyscale clip (PARANOID): luma plane only, 15360 bytes per frame
        w = 160;
        h = 96;
        frames = int((d.size() - 1) / 15360);
        rgba.resize(size_t(frames) * w * h * 4);
        std::vector<float> Y(160 * 96);
        for (int f = 0; f < frames; f++) {
            decodePlane(d.data() + 1 + size_t(f) * 15360, 160, 96, q, Y.data());
            uint8_t* out = rgba.data() + size_t(f) * w * h * 4;
            for (int i = 0; i < 160 * 96; i++) {
                out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = uint8_t(Y[i] + 0.5f);
                out[i * 4 + 3] = 255;
            }
        }
        return true;
    }
    rgba.resize(size_t(frames) * w * h * 4);
    std::vector<float> Y(160 * 96), I(80 * 48), Q(80 * 48);
    for (int f = 0; f < frames; f++) {
        const uint8_t* s = d.data() + 1 + f * fsz;
        decodePlane(s, 160, 96, q, Y.data());
        decodePlane(s + 15360, 80, 48, q, I.data());
        decodePlane(s + 15360 + 3840, 80, 48, q, Q.data());
        uint8_t* out = rgba.data() + size_t(f) * w * h * 4;
        for (int y = 0; y < 96; y++)
            for (int x = 0; x < 160; x++) {
                // bilinear chroma upsample (centre aligned)
                float cx = std::clamp((x + 0.5f) * 0.5f - 0.5f, 0.0f, 79.0f);
                float cy = std::clamp((y + 0.5f) * 0.5f - 0.5f, 0.0f, 47.0f);
                int x0 = int(cx), y0 = int(cy);
                int x1 = std::min(x0 + 1, 79), y1 = std::min(y0 + 1, 47);
                float fx = cx - x0, fy = cy - y0;
                auto bl = [&](const std::vector<float>& P) {
                    return (P[y0 * 80 + x0] * (1 - fx) + P[y0 * 80 + x1] * fx) * (1 - fy) +
                           (P[y1 * 80 + x0] * (1 - fx) + P[y1 * 80 + x1] * fx) * fy;
                };
                // The original palette is YIQ -> RGB in 6-bit VGA units: Y6 = Y/4, I6 = I/4, Q6 = Q/4.
                float yy = Y[y * 160 + x] / 4.0f, ii = bl(I) / 4.0f, qq = bl(Q) / 4.0f;
                float r = 1.003f * yy + 0.9548f * ii + 0.6178f * qq;
                float g = 0.9967f * yy - 0.27f * ii - 0.644f * qq;
                float b = 1.0084f * yy - 1.11f * ii + 1.6995f * qq;
                uint8_t* o = out + (y * 160 + x) * 4;
                o[0] = uint8_t(std::clamp(r / 63.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                o[1] = uint8_t(std::clamp(g / 63.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                o[2] = uint8_t(std::clamp(b / 63.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                o[3] = 255;
            }
    }
    return true;
}

// ---------------------------------------------------------------- meshes
void Mesh::computeNormals() {
    nrm.assign(pos.size(), 0.0f);
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const float* a = &pos[idx[t] * 3];
        const float* b = &pos[idx[t + 1] * 3];
        const float* c = &pos[idx[t + 2] * 3];
        float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (int k = 0; k < 3; k++)
            for (int j = 0; j < 3; j++) nrm[idx[t + k] * 3 + j] += n[j];
    }
    for (size_t i = 0; i < nrm.size(); i += 3) {
        float l = std::sqrt(nrm[i] * nrm[i] + nrm[i + 1] * nrm[i + 1] + nrm[i + 2] * nrm[i + 2]);
        if (l > 0) for (int j = 0; j < 3; j++) nrm[i + j] /= l;
    }
}

void Mesh::normalize(float target) {
    float c[3] = {0, 0, 0};
    size_t n = pos.size() / 3;
    float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], pos[i * 3 + k]); mx[k] = std::max(mx[k], pos[i * 3 + k]); }
    for (int k = 0; k < 3; k++) c[k] = 0.5f * (mn[k] + mx[k]);
    float r = 0;
    for (size_t i = 0; i < n; i++) {
        float d = 0;
        for (int k = 0; k < 3; k++) d += (pos[i * 3 + k] - c[k]) * (pos[i * 3 + k] - c[k]);
        r = std::max(r, std::sqrt(d));
    }
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 3; k++) pos[i * 3 + k] = (pos[i * 3 + k] - c[k]) / r * target;
    radius = target;
}

static void readTris(Mesh& m, const uint8_t* p, uint32_t nf, uint32_t nv) {
    m.idx.resize(nf * 3);
    for (uint32_t i = 0; i < nf * 3; i++) {
        uint32_t v = rd32(p + 4 * i);
        m.idx[i] = v < nv ? v : 0;
    }
}

Mesh meshSpikey(const Bytes& raw) {
    Mesh m;
    uint32_t nv = rd32(&raw[0]), nf = rd32(&raw[4]);
    const uint8_t* p = &raw[8];
    m.pos.resize(nv * 3);
    m.extra.resize(nv);
    for (uint32_t i = 0; i < nv; i++, p += 16) {
        float r = rdf(p + 12);
        for (int k = 0; k < 3; k++) m.pos[i * 3 + k] = rdf(p + 4 * k) * r;
        m.extra[i] = r;
    }
    readTris(m, p, nf, nv);
    m.computeNormals();
    return m;
}

Mesh meshIcosa(const Bytes& raw) {
    Mesh m;
    uint32_t nv = rd32(&raw[4]), nf = rd32(&raw[8]);
    const uint8_t* p = &raw[12];
    m.pos.resize(nv * 3);
    m.extra.resize(nv);
    for (uint32_t i = 0; i < nv; i++, p += 16) {
        for (int k = 0; k < 3; k++) m.pos[i * 3 + k] = rdf(p + 4 * k);
        m.extra[i] = float(rd32(p + 12));
    }
    readTris(m, p, nf, nv);
    m.computeNormals();
    return m;
}

Mesh meshFace(const Bytes& raw) {
    Mesh m;
    uint32_t nv = rd32(&raw[0]), nf = rd32(&raw[4]);
    const uint8_t* p = &raw[8];
    m.pos.resize(nv * 3);
    for (uint32_t i = 0; i < nv; i++, p += 12)
        for (int k = 0; k < 3; k++) m.pos[i * 3 + k] = rdf(p + 4 * k);
    readTris(m, p, nf, nv);
    m.computeNormals();
    return m;
}
