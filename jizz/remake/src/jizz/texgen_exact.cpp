// The exact texture generator: the byte-code interpreter 0x12e72 at 256x256 bytes, op for op as JIZZ.EXE runs it
// (jizz/re/notes-textures.md §3, §4; the Python model jizz/re/models/textures/texgen.py), the random ops shared
// with the high-resolution path (0x132f1 noise, 0x1356a blobs, 0x13b5c clouds), the RLE pictures 0x13df4, and the
// driver's post-steps 0x12d87 (map15 stripes) and 0x12e08 (blob gradients).
//
// x87 arithmetic: main sets the precision control to 24 bits, so every fadd/fsub/fmul/fdiv/fsqrt result is rounded
// to a 24-bit mantissa. Float32 arithmetic gives exactly that for operands that are float32 values (SSE on x86-64;
// this file is compiled with -ffp-contract=off so no FMA fuses two roundings). fsin/fcos/fpatan are not affected by
// the precision control: their (double) results are multiplied by values that may have more than 24 bits (fldpi's
// 64-bit pi, another float) and the exact product is rounded once to 24 bits (round24). The original's results were
// recorded with the C library's double sin/cos/atan2; every rounding of such a result is checked for how close it
// is to a rounding boundary (Libm::check): none of the 17 scripts comes within 4 ulps, so any libm whose error is
// below that gives the same bytes (texdump reports the count).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "jizz/texgen_internal.h"

namespace jizz::texgen::detail {

namespace {

// ---------------------------------------------------------------- x87 helpers

// a * b = p + e exactly (Dekker, Veltkamp split; no FMA needed)
inline void twoProd(double a, double b, double& p, double& e) {
    p = a * b;
    const double sa = 134217729.0 * a, sb = 134217729.0 * b;  // 2^27 + 1
    const double ah = sa - (sa - a), al = a - ah;
    const double bh = sb - (sb - b), bl = b - bh;
    e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
}

// a + b = s + e exactly
inline void twoSum(double a, double b, double& s, double& e) {
    s = a + b;
    const double bb = s - a;
    e = (a - (s - bb)) + (b - bb);
}

// Rounds the exact value h + r (|r| below one ulp of h) to float32, to nearest even. (float)h is already right
// unless h lies exactly halfway between two floats; then the sign of r decides.
float round24(double h, double r) {
    const float f = float(h);
    const double fd = f;
    if (fd == h || r == 0) return f;
    const float g = std::nextafter(f, h > fd ? INFINITY : -INFINITY);
    if ((fd + double(g)) * 0.5 != h) return f;
    return r > 0 ? std::max(f, g) : std::min(f, g);
}

// fistp: round to nearest even
inline long rne(double v) {
    const double f = std::floor(v);
    const double d = v - f;
    long i = long(f);
    if (d > 0.5 || (d == 0.5 && (i & 1))) i++;
    return i;
}

// Counts roundings to float32 of values derived from a libm result whose distance to the rounding boundary is
// below `tol` (the change a libm error of 4 ulps could make).
struct Libm {
    int close = 0;
    void check(double h, double tol) {
        const float f = float(h);
        const double fd = f;
        const float g = std::nextafter(f, h >= fd ? INFINITY : -INFINITY);
        const double half = std::fabs(double(g) - fd) * 0.5;
        if (half - std::fabs(h - fd) < tol) close++;
    }
    static double ulps(double v, double scale) {  // 4 ulps of v, times |scale|
        if (v == 0) return 0;
        return std::ldexp(std::fabs(scale), std::ilogb(v) - 52 + 2);
    }
};

// fldpi: the x87's 64-bit pi, as a double pair
constexpr uint64_t kFldpiMantissa = 0xc90fdaa22168c235ull;  // value = mantissa * 2^-62
const double kPiHi = std::ldexp(double(kFldpiMantissa & ~uint64_t(0x7ff)), -62);
const double kPiLo = std::ldexp(double(kFldpiMantissa & 0x7ff), -62);

// fmul of a libm double by fldpi, rounded to 24 bits
float mulPi(double s, Libm& lm) {
    double h, e;
    twoProd(s, kPiHi, h, e);
    const double r = e + s * kPiLo;
    lm.check(h, Libm::ulps(s, kPiHi));
    return round24(h, r);
}

// fmul of a libm double by a float (or an integer of at most 24 bits), rounded to 24 bits
float mulF(double s, double m, Libm& lm) {
    double h, e;
    twoProd(s, m, h, e);
    lm.check(h, Libm::ulps(s, m));
    return round24(h, e);
}

// ---------------------------------------------------------------- the interpreter

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void st32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

class Exact {
public:
    Exact(const JizzExe& exe, bool captureSaves) : X(exe), capture_(captureSaves) {
        for (auto& b : B) b.assign(kBuf, 0);
        C_ = X.f32(0x141e2);    // float 2*pi/256
        F255_ = X.f32(0x141e6); // float 255.0
    }
    ExactRun run(uint32_t script, int maxOps);

private:
    const JizzExe& X;
    bool capture_;
    std::vector<uint8_t> B[5];  // B0..B3, B4 = scratch copy (0x13f84)
    int w = 0;
    Rng rng;
    Libm lm;
    float C_ = 0, F255_ = 0;
    ExactRun out_;

    uint8_t* plane(int b, int p) { return B[b].data() + p * kPlane; }
    uint8_t* buf(int b) {
        if (b < 0 || b > 4) throw std::runtime_error("texgen: buffer index out of range");
        return B[b].data();
    }
    const uint8_t* temp() {  // 0x13f84
        B[4] = B[w];
        return B[4].data();
    }
    void markContent() {  // before a format op: keep the planar content of W
        if (w < 4 && out_.content[w].empty()) out_.content[w] = B[w];
    }

    void opBlur(const Planes& P, int s, int sh);
    void opHsv2Rgb();
    void opRgb2Hsv();
    void opBlobs(int p, int n);
    void opEmboss(int p, int ang, int s, int dplane, int n);
    void opSines(int p, const int8_t prm[6], int amp);
    void opCurve(int p, int n, int size, const int16_t pts[8]);
    void opCone(int p, int r);
    void opAngle(int p);
    void opMblur(const Planes& P, int dx, int dy, int n);
};

ExactRun Exact::run(uint32_t a, int maxOps) {
    rng = scriptSeed(X);
    for (int i = 0; i < 4; i++) std::fill(B[i].begin(), B[i].end(), uint8_t(0));
    w = 0;
    out_ = ExactRun{};
    for (int guard = 0; guard < 10000; guard++) {
        const uint8_t op = guard == maxOps ? 0x01 : X.u8(a), m = X.u8(a + 1);
        const Planes P = planesOf(X, m);
        uint8_t* W = buf(w);
        switch (op) {
        case 0x00: w = m & 7; buf(w); break;
        case 0x01:
            out_.result = B[w];
            out_.wEnd = w;
            for (int i = 0; i < 4; i++) out_.buffers[i] = B[i];
            out_.closeCalls = lm.close;
            return std::move(out_);
        case 0x02:
            for (int i = 0; i < P.n; i++) std::memset(plane(w, P.p[i]), X.u8(a + 2), kPlane);
            break;
        case 0x03: noise(rng, W); break;
        case 0x04: opBlur(P, 2, 2); break;
        case 0x05: opBlur(P, 4, 4); break;
        case 0x06: opBlur(P, 8, 6); break;
        case 0x07: opHsv2Rgb(); break;
        case 0x08: opRgb2Hsv(); break;
        case 0x09: {  // add a byte to every byte, as dword adds: a wrapping byte carries into the next one
            const uint32_t v = X.u8(a + 2) * 0x01010101u;
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                for (int j = 0; j < kPlane; j += 4) st32(d + j, le32(d + j) + v);
            }
            break;
        }
        case 0x0a: opBlobs(P.p[0], X.u16(a + 2)); break;
        case 0x0b: {  // move all three planes by (dx, dy), wrapping
            const int dx = X.u8(a + 1), dy = X.u8(a + 2);
            const uint8_t* T = temp();
            for (int p = 0; p < 3; p++)
                for (int y = 0; y < 256; y++)
                    for (int x = 0; x < 256; x++)
                        W[p * kPlane + ((y + dy) & 255) * 256 + ((x + dx) & 255)] = T[p * kPlane + y * 256 + x];
            break;
        }
        case 0x0c:
        case 0x0d: {  // scale by k/256; 0x0c saturates at 255, 0x0d keeps the low byte
            const int k = X.s16(a + 2);
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                for (int j = 0; j < kPlane; j++) {
                    int v = (int(d[j]) * k) >> 8;
                    if (op == 0x0c && v > 255) v = 255;
                    d[j] = uint8_t(v & 255);
                }
            }
            break;
        }
        case 0x0e: {  // multiply by B[n]
            const uint8_t* S = buf(X.u8(a + 2));
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                const uint8_t* s = S + P.p[i] * kPlane;
                for (int j = 0; j < kPlane; j++) d[j] = uint8_t((int(s[j]) * d[j]) >> 8);
            }
            break;
        }
        case 0x0f: {  // add B[n] as dwords
            const uint8_t* S = buf(X.u8(a + 2));
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                const uint8_t* s = S + P.p[i] * kPlane;
                for (int j = 0; j < kPlane; j += 4) st32(d + j, le32(d + j) + le32(s + j));
            }
            break;
        }
        case 0x10: {  // mix with B[n] by a fixed alpha (alpha 0 gives 0)
            const uint8_t* S = buf(X.u8(a + 2));
            const int al = X.u8(a + 3), ia = (256 - al) & 255;
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                const uint8_t* s = S + P.p[i] * kPlane;
                for (int j = 0; j < kPlane; j++) d[j] = uint8_t((int(s[j]) * al + int(d[j]) * ia) >> 8);
            }
            break;
        }
        case 0x11: {  // mix with B[n] by the alpha plane FIRST(mm) of B[k]; alpha 0 keeps W
            const uint8_t* S = buf(X.u8(a + 2));
            const uint8_t* A = buf(X.u8(a + 4)) + planesOf(X, X.u8(a + 3)).p[0] * kPlane;
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                const uint8_t* s = S + P.p[i] * kPlane;
                for (int j = 0; j < kPlane; j++) {
                    const int c = A[j];
                    if (c) d[j] = uint8_t((int(s[j]) * c + int(d[j]) * ((256 - c) & 255)) >> 8);
                }
            }
            break;
        }
        case 0x12: {  // emboss plane FIRST(m) of W into plane FIRST(b) of B[n]; W = B[n]
            const int n = X.u8(a + 5);
            opEmboss(P.p[0], X.u8(a + 2), X.s8(a + 3), planesOf(X, X.u8(a + 4)).p[0], n);
            w = n;
            break;
        }
        case 0x13: {  // saturating add of plane FIRST(b) of B[n] (the same plane for every plane of m)
            const uint8_t* S = buf(X.u8(a + 3)) + planesOf(X, X.u8(a + 2)).p[0] * kPlane;
            const std::vector<uint8_t> src(S, S + kPlane);
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                for (int j = 0; j < kPlane; j++) d[j] = uint8_t(std::min(255, int(d[j]) + src[j]));
            }
            break;
        }
        case 0x14: {
            int8_t prm[6];
            for (int i = 0; i < 6; i++) prm[i] = X.s8(a + 2 + i);
            opSines(P.p[0], prm, X.u8(a + 8));
            break;
        }
        case 0x15: {  // displace by the (U, V) = (R, G) planes of B[n], nearest texel
            const uint8_t* S = buf(X.u8(a + 2));
            const uint8_t* T = temp();
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                const uint8_t* t = T + P.p[i] * kPlane;
                for (int j = 0; j < kPlane; j++) d[j] = t[S[j] + 256 * S[kPlane + j]];
            }
            break;
        }
        case 0x16:
        case 0x17: {
            int16_t pts[8];
            for (int i = 0; i < 8; i++) pts[i] = X.s16(a + 4 + 2 * i);
            opCurve(P.p[0], X.u8(a + 2) * 16, X.u8(a + 3), pts);
            break;
        }
        case 0x18:
            for (int i = 0; i < P.n; i++) {
                uint8_t* d = plane(w, P.p[i]);
                for (int j = 0; j < kPlane; j++) d[j] = uint8_t(~d[j]);
            }
            break;
        case 0x19: {  // copy planes(m) of W to B[n]
            uint8_t* D = buf(X.u8(a + 2));
            for (int i = 0; i < P.n; i++) std::memmove(D + P.p[i] * kPlane, W + P.p[i] * kPlane, kPlane);
            break;
        }
        case 0x1a: {  // copy plane FIRST(m) of W to plane FIRST(b) of B[n]; W = B[n]
            const int n = X.u8(a + 3);
            std::memmove(buf(n) + planesOf(X, X.u8(a + 2)).p[0] * kPlane, W + P.p[0] * kPlane, kPlane);
            w = n;
            break;
        }
        case 0x1b:  // R += x, G += y
            for (int y = 0; y < 256; y++)
                for (int x = 0; x < 256; x++) {
                    W[y * 256 + x] = uint8_t(W[y * 256 + x] + x);
                    W[kPlane + y * 256 + x] = uint8_t(W[kPlane + y * 256 + x] + y);
                }
            break;
        case 0x1c: opCone(P.p[0], X.u8(a + 2)); break;
        case 0x1d: clouds(rng, X.u8(a + 2), plane(w, P.p[0])); break;
        case 0x1e: opMblur(P, X.u8(a + 2), X.u8(a + 3), X.u8(a + 4)); break;
        case 0x1f: opAngle(P.p[0]); break;
        case 0x20: {  // 320x200 tiled copy of each plane
            markContent();
            const uint8_t* T = temp();
            for (int p = 0; p < 3; p++)
                for (int y = 0; y < 200; y++)
                    for (int x = 0; x < 320; x++) W[p * kPlane + y * 320 + x] = T[p * kPlane + y * 256 + (x & 255)];
            break;
        }
        case 0x21: {  // planar -> 16-bit 5:6:5 pixels (the VBE field layout of the capture's mode)
            markContent();
            const uint8_t* T = temp();
            for (int i = 0; i < kPlane; i++) {
                const unsigned px = (unsigned(T[i]) >> 3) << 11 | (unsigned(T[kPlane + i]) >> 2) << 5 |
                                    unsigned(T[2 * kPlane + i]) >> 3;
                W[2 * i] = uint8_t(px);
                W[2 * i + 1] = uint8_t(px >> 8);
            }
            break;
        }
        case 0x22: rle4(X, X.u32(a + 2), plane(w, P.p[0])); break;
        case 0x23: {  // 3x3 colour matrix, signed bytes / 128, clamped
            int M[9];
            for (int i = 0; i < 9; i++) M[i] = X.s8(a + 1 + i);
            const uint8_t* T = temp();
            for (int c = 0; c < 3; c++)
                for (int i = 0; i < kPlane; i++) {
                    const int v = (T[i] * M[3 * c] + T[kPlane + i] * M[3 * c + 1] + T[2 * kPlane + i] * M[3 * c + 2]) >> 7;
                    W[c * kPlane + i] = uint8_t(std::clamp(v, 0, 255));
                }
            break;
        }
        case 0x24: {  // planar -> interleaved R, G, B
            markContent();
            const uint8_t* T = temp();
            for (int i = 0; i < kPlane; i++)
                for (int c = 0; c < 3; c++) W[3 * i + c] = T[c * kPlane + i];
            break;
        }
        case 0x25: break;
        case 0x26:
            if (capture_) out_.saves.emplace_back(X.cstr(a + 1), B[w]);
            break;
        default: throw std::runtime_error("texgen: unknown op");
        }
        a += opLength(X, a);
    }
    throw std::runtime_error("texgen: script does not end");
}

// 0x13300: box blur s x s over offsets -s/2 .. s/2-1, floor(sum / s^2) (separable integer sums, same result)
void Exact::opBlur(const Planes& P, int s, int sh) {
    const uint8_t* T = temp();
    const int h = s / 2;
    std::vector<int> rows(kPlane);
    for (int i = 0; i < P.n; i++) {
        const uint8_t* t = T + P.p[i] * kPlane;
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                int sum = 0;
                for (int j = 0; j < s; j++) sum += t[y * 256 + ((x - h + j) & 255)];
                rows[y * 256 + x] = sum;
            }
        uint8_t* d = plane(w, P.p[i]);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                int sum = 0;
                for (int j = 0; j < s; j++) sum += rows[((y - h + j) & 255) * 256 + x];
                d[y * 256 + x] = uint8_t(sum >> sh);
            }
    }
}

// 0x13378 / 0x13f98: H, S, V in planes R, G, B -> R, G, B; table 0x1403a: per sector the planes of V, p1, p2
void Exact::opHsv2Rgb() {
    static const int order[6][3] = {{0, 1, 2}, {1, 0, 2}, {1, 2, 0}, {2, 1, 0}, {2, 0, 1}, {0, 2, 1}};
    uint8_t* W = B[w].data();
    for (int i = 0; i < kPlane; i++) {
        const int H = W[i], S = W[kPlane + i], V = W[2 * kPlane + i];
        const int h6 = H * 6, sec = h6 >> 8;
        int f = h6 & 255;
        if ((sec & 1) == 0) f = 255 - f;
        const int p1 = ((255 - ((f * S) >> 8)) * (V + 1)) >> 8;
        const int p2 = ((255 - S) * (V + 1)) >> 8;
        W[order[sec][0] * kPlane + i] = uint8_t(V);
        W[order[sec][1] * kPlane + i] = uint8_t(p1);
        W[order[sec][2] * kPlane + i] = uint8_t(p2);
    }
}

// 0x133ad / 0x14088: R, G, B -> H, S, V. Table 0x14156 per e = (R>G) | (R>B)<<1 | (G>B)<<2: where R, G, B go among
// max (0), mid (1), min (2), written in the order R, G, B, and the sector.
void Exact::opRgb2Hsv() {
    static const int tab[8][4] = {{2, 1, 0, 3}, {1, 2, 0, 4}, {0, 1, 1, 0}, {0, 2, 1, 5},
                                  {2, 0, 1, 2}, {0, 1, 2, 0}, {1, 0, 2, 1}, {0, 1, 2, 0}};
    uint8_t* W = B[w].data();
    for (int i = 0; i < kPlane; i++) {
        const int R = W[i], G = W[kPlane + i], Bc = W[2 * kPlane + i];
        const int e = (R > G) | (R > Bc) << 1 | (G > Bc) << 2;
        int slot[3] = {0, 0, 0};
        slot[tab[e][0]] = R;
        slot[tab[e][1]] = G;
        slot[tab[e][2]] = Bc;
        const int mx = slot[0], md = slot[1], mn = slot[2], sec = tab[e][3];
        int H = 0, S = 0;
        if (mx != mn) {
            const int q = 0xff00 / (mx ? mx : 1);
            const int mid2 = 255 - (((md * q) >> 8) & 255);
            S = 255 - (((mn * q) >> 8) & 255);
            const int q2 = 0xff00 / (S ? S : 1);
            int f = 255 - (((mid2 * q2) >> 8) & 255);
            if (sec & 1) f = 255 - f;
            H = ((sec * 256 + f) / 6) & 255;
        }
        W[i] = uint8_t(H);
        W[kPlane + i] = uint8_t(S);
        W[2 * kPlane + i] = uint8_t(mx);
    }
}

// 0x1356a: n blobs (8x8 discs, rows 4,6,8,8,8,8,6,4 wide) of a random grey at random positions
void Exact::opBlobs(int p, int n) {
    static const int dx[8] = {0, -1, -2, -2, -2, -2, -1, 0}, wd[8] = {4, 6, 8, 8, 8, 8, 6, 4};
    uint8_t* d = plane(w, p);
    for (int k = 0; k < n; k++) {
        const Blob b = nextBlob(rng);
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < wd[j]; i++) d[((b.y0 + j) & 255) * 256 + ((b.x0 + dx[j] + i) & 255)] = b.v;
    }
}

// 0x139e9: directional slope of plane p of W into plane dplane of B[n]
void Exact::opEmboss(int p, int ang, int s, int dplane, int n) {
    const float th = float(double(ang) * double(C_));
    const double sn = std::sin(double(th)), cs = -std::cos(double(th));
    lm.check(sn, Libm::ulps(sn, 1));
    lm.check(cs, Libm::ulps(cs, 1));
    const float cy32 = float(sn), cx32 = float(cs);
    double sum, err;
    twoSum(std::fabs(sn), std::fabs(cs), sum, err);
    lm.check(sum, Libm::ulps(std::max(std::fabs(sn), std::fabs(cs)), 2));
    const float den = round24(sum, err);
    const float k = float(s * 64) / den;
    const float Cx = k * cx32, Cy = k * cy32;
    const std::vector<uint8_t> src(plane(w, p), plane(w, p) + kPlane);
    uint8_t* d = buf(n) + dplane * kPlane;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const int c = src[y * 256 + x];
            const int dx = src[y * 256 + ((x + 1) & 255)] - c, dy = src[((y + 1) & 255) * 256 + x] - c;
            const float pdx = float(dx) * Cx, pdy = float(dy) * Cy;
            const float v = pdx + pdy;
            const long iv = rne(v);
            d[y * 256 + x] = iv < 0 ? 0 : iv >= 0xffff ? 255 : uint8_t((iv >> 8) & 255);
        }
}

// 0x137ea: sine plasma, first plane (notes §4.9): v = round(R(y) * sin(pi*sin X + Y + Z)), signed, low byte
void Exact::opSines(int p, const int8_t prm[6], int amp) {
    float c[6];
    for (int i = 0; i < 6; i++) c[i] = float(double(prm[i]) * double(C_));
    const float dX = c[0], dY = c[1], dA = c[2], dB = c[3], dC = c[4], dZ = c[5];
    float A = 0, Bv = 0, Cv = 0;
    float rowR[256], X0[256], Z0[256];
    for (int y = 0; y < 256; y++) {
        A = A + dA;
        float t = mulPi(std::sin(double(A)), lm);
        Bv = Bv + dB;
        t = t + Bv;
        t = t + Cv;
        rowR[y] = mulF(std::sin(double(t)), double(amp), lm);
        Cv = Cv + dC;
        X0[y] = Cv;
        Z0[y] = A;
    }
    uint8_t* d = plane(w, p);
    for (int y = 0; y < 256; y++) {
        float Xs = X0[y], Zs = Z0[y], Ys = 0;  // X, Y, Z of notes §4.9
        for (int x = 0; x < 256; x++) {
            Xs = Xs + dX;
            float t = mulPi(std::sin(double(Xs)), lm);
            Ys = Ys + dY;
            t = t + Ys;
            Zs = Zs + dZ;
            t = t + Zs;
            const float v = mulF(std::sin(double(t)), double(rowR[y]), lm);
            d[y * 256 + x] = uint8_t(rne(v) & 255);
        }
    }
}

// 0x1360b: Hermite curve in polar coordinates around pixel (0,0), squares of size x size of 255 at n points
void Exact::opCurve(int p, int n, int size, const int16_t pts[8]) {
    uint8_t* d = plane(w, p);
    const float inv = 1.0f / float(n);
    float F[2][4];
    for (int k = 0; k < 2; k++) {
        const int p0 = pts[4 * k], p1 = pts[4 * k + 1], t0 = pts[4 * k + 2], t1 = pts[4 * k + 3];
        F[k][0] = float(p0);
        F[k][1] = float(t0);
        F[k][2] = float(-(3 * (p0 - p1) + 2 * t0 + t1));
        F[k][3] = float(2 * (p0 - p1) + t0 + t1);
    }
    float t = 0;
    for (int i = 0; i < n; i++) {
        t = t + inv;
        const float t2 = t * t, t3 = t2 * t;
        float Xs = t3 * F[0][3], Ys = t3 * F[1][3];
        Xs = t2 * F[0][2] + Xs;  // separate roundings (no FMA: -ffp-contract=off)
        Ys = t2 * F[1][2] + Ys;
        const float xv = (t * F[0][1] + F[0][0]) + Xs;
        const float yv = (t * F[1][1] + F[1][0]) + Ys;
        const float ang = yv * C_;
        const long py = rne(-double(mulF(std::sin(double(ang)), double(xv), lm)));
        const long px = rne(double(mulF(std::cos(double(ang)), double(xv), lm)));
        const int cx = int((px - size) & 255), cy = int((py - size) & 255);
        for (int j = 0; j < size; j++)
            for (int k = 0; k < size; k++) d[((cy + j) & 255) * 256 + ((cx + k) & 255)] = 255;
    }
}

// 0x13af4: adds round(255 * (r - d) / r) where it is >= 0, d = distance to pixel (0,0) on the torus
void Exact::opCone(int p, int r) {
    const float K = -(F255_ / float(r));
    uint8_t* d = plane(w, p);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const int sx = int8_t(x), sy = int8_t(y);  // -128..127: the distance on the torus
            const float dist = std::sqrt(float(sx * sx + sy * sy));
            const float v = (dist - float(r)) * K;
            const long iv = rne(v);
            if (iv >= 0) d[y * 256 + x] = uint8_t(d[y * 256 + x] + iv);
        }
}

// 0x13cfe: atan2(y, x) around pixel (0,0) in 1/256 turns (fpatan, fdiv by the float 2*pi/256)
void Exact::opAngle(int p) {
    uint8_t* d = plane(w, p);
    const double C = C_;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const double at = std::atan2(double(int8_t(y)), double(int8_t(x)));
            const double q = at / C;
            double pr, e;
            twoProd(q, C, pr, e);
            const double rem = (at - pr) - e;  // sign of the exact quotient's remainder
            lm.check(q, Libm::ulps(at, 1.0 / C));
            d[y * 256 + x] = uint8_t(rne(round24(q, rem)) & 255);
        }
}

// 0x13c7a (op 0x1e, not used by the scripts): average of n samples stepping (dx, dy)
void Exact::opMblur(const Planes& P, int dx, int dy, int n) {
    const uint8_t* T = temp();
    const int st = (-(n >> 1)) & 255;
    const int ox = (int8_t(dx) * int8_t(st)) & 255, oy = (int8_t(dy) * int8_t(st)) & 255;
    for (int i = 0; i < P.n; i++) {
        const uint8_t* t = T + P.p[i] * kPlane;
        uint8_t* d = plane(w, P.p[i]);
        for (int j = 0; j < kPlane; j++) {
            const int start = ((oy << 8) + ox + j) & 0xffff, sx = start & 255, sy = start >> 8;
            int acc = 0;
            for (int s = 0; s < n; s++) acc += t[((sy + s * dy) & 255) * 256 + ((sx + s * dx) & 255)];
            d[j] = uint8_t((acc & 0xffff) / (n ? n : 1));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- shared pieces

Rng scriptSeed(const JizzExe& exe) {
    Rng r;  // 0x12e72: mov dword [0x141ea], imm32 (three times, 10-byte instructions from 0x12e73)
    r.s0 = exe.u32(0x12e73 + 6);
    r.s1 = exe.u32(0x12e7d + 6);
    r.s2 = exe.u32(0x12e87 + 6);
    return r;
}

Planes planesOf(const JizzExe& exe, uint8_t mask) {
    const uint32_t e = 0x12e4a + 4 * (mask & 7);
    Planes P;
    const int first = exe.u8(e), gap = exe.u8(e + 1), count = exe.u8(e + 2);
    P.n = std::min(count, 3);
    for (int i = 0; i < P.n; i++) P.p[i] = std::min(first + i * (1 + gap), 2);
    return P;
}

void noise(Rng& r, uint8_t* buf) {
    for (int i = 0; i < kBuf; i += 4) st32(buf + i, r.next());
}

Blob nextBlob(Rng& r) {
    r.next();
    const uint32_t pos = r.ebx;
    const uint8_t v = uint8_t(r.next());
    return {int(pos & 255), int((pos >> 8) & 255), v};
}

namespace {
struct Clouds {
    Rng& rng;
    int k;
    std::vector<int> pl;
    Clouds(Rng& r, int k_) : rng(r), k(k_), pl(kPlane, 0) {}
    int edge(int x, int y, int ox, int oy, int ex, int ey, int half) {
        const int i = ((y + oy) & 255) * 256 + ((x + ox) & 255);
        if (pl[i] != 0) return pl[i];
        const uint32_t r = rng.next();
        const int disp = int(r % uint32_t(2 * k + 1)) - k;
        int v = (disp * half) >> 1;
        v += pl[(y & 255) * 256 + (x & 255)];
        v += pl[((y + ey) & 255) * 256 + ((x + ex) & 255)];
        v >>= 1;
        v = std::clamp(v, 0, 254) + 1;
        pl[i] = v;
        return v;
    }
    void quad(int x, int y, int size) {
        if (size <= 1) return;
        const int half = size >> 1;
        int v;
        if (size == 256)
            v = int(rng.next() & 255);
        else
            v = (edge(x, y, half, 0, size, 0, half) + edge(x, y, 0, half, 0, size, half) +
                 edge(x + size, y, 0, half, 0, size, half) + edge(x, y + size, half, 0, size, 0, half)) >>
                2;
        pl[((y + half) & 255) * 256 + ((x + half) & 255)] = v & 255;
        quad(x, y, half);
        quad(x + half, y, half);
        quad(x + half, y + half, half);
        quad(x, y + half, half);
    }
};
}  // namespace

// 0x13b5c: memset, the first value at (0,0), then the recursive subdivision 0x13b8b / 0x13c03
void clouds(Rng& r, int k, uint8_t* plane) {
    Clouds c(r, k);
    c.pl[0] = int(r.next() & 255);
    c.quad(0, 0, 256);
    for (int i = 0; i < kPlane; i++) plane[i] = uint8_t(c.pl[i]);
}

void rle4(const JizzExe& exe, uint32_t addr, uint8_t* plane) {
    uint32_t pos = addr;
    bool hi = false;
    auto nib = [&]() {
        const uint8_t b = exe.u8(pos);
        const int v = hi ? b >> 4 : b & 15;
        if (hi) pos++;
        hi = !hi;
        return v;
    };
    int left = 0xc800, at = 0;
    while (left > 0) {
        const int n = nib();
        int cnt = 1;
        uint8_t v = uint8_t(n * 0x11);
        if (n == 0) {
            cnt = nib();
            cnt |= nib() << 4;
            cnt |= nib() << 8;
            cnt |= nib() << 12;
            v = 0;
        } else if (n == 15) {
            cnt = nib();
            cnt |= nib() << 4;
            v = 255;
        }
        if (cnt == 0) throw std::runtime_error("texgen: RLE run of length 0");
        for (int i = 0; i < cnt && at < kPlane; i++) plane[at++] = v;
        left -= cnt;
    }
}

uint32_t opLength(const JizzExe& exe, uint32_t a) {
    switch (exe.u8(a)) {
    case 0x01: case 0x03: case 0x07: case 0x08: case 0x1b: case 0x20: case 0x21: case 0x24: case 0x25: return 1;
    case 0x00: case 0x04: case 0x05: case 0x06: case 0x18: case 0x1f: return 2;
    case 0x02: case 0x09: case 0x0b: case 0x0e: case 0x0f: case 0x15: case 0x19: case 0x1c: case 0x1d: return 3;
    case 0x0a: case 0x0c: case 0x0d: case 0x10: case 0x13: case 0x1a: return 4;
    case 0x11: case 0x1e: return 5;
    case 0x12: case 0x22: return 6;
    case 0x14: return 9;
    case 0x23: return 10;
    case 0x16: case 0x17: return 20;
    case 0x26: return uint32_t(exe.cstr(a + 1).size()) + 2;
    default: throw std::runtime_error("texgen: unknown op");
    }
}

ExactRun runExact(const JizzExe& exe, uint32_t script, bool captureSaves, int maxOps) {
    auto ex = std::make_unique<Exact>(exe, captureSaves);
    return ex->run(script, maxOps);
}

// 0x12d87: map15 rows darkened by (150 + round(100 sin(y * 0x240 * 2pi/16384))) / 256
void stripesExact(const JizzExe& exe, uint8_t* map15) {
    const double D = exe.f64(0x11596);  // double 2*pi/16384
    Libm lm;
    for (int y = 0; y < 256; y++) {
        double h, e;
        twoProd(double(y * 0x240), D, h, e);
        const float a = round24(h, e);
        const long s = rne(mulF(std::sin(double(a)), 100.0, lm));
        const int m = int((s + 150) & 255);
        for (int p = 0; p < 3; p++)
            for (int x = 0; x < 256; x++) {
                uint8_t& v = map15[p * kPlane + y * 256 + x];
                v = uint8_t((int(v) * m) >> 8);
            }
    }
}

int libmCloseCallsStripes(const JizzExe& exe) {
    const double D = exe.f64(0x11596);
    Libm lm;
    for (int y = 0; y < 256; y++) {
        double h, e;
        twoProd(double(y * 0x240), D, h, e);
        mulF(std::sin(double(round24(h, e))), 100.0, lm);
    }
    return lm.close;
}

// 0x12e08: B = R[y+1] - R[y-1]; G[y][x-1] = R[y][x+1] - R[y][x-1] (bytes, wrapping)
void gradientsExact(uint8_t* t) {
    const std::vector<uint8_t> R(t, t + kPlane);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            t[2 * kPlane + y * 256 + x] = uint8_t(R[((y + 1) & 255) * 256 + x] - R[((y - 1) & 255) * 256 + x]);
            t[kPlane + y * 256 + ((x - 1) & 255)] = uint8_t(R[y * 256 + ((x + 1) & 255)] - R[y * 256 + ((x - 1) & 255)]);
        }
}

}  // namespace jizz::texgen::detail
