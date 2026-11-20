// The texture interpreter 0x12e72 at high resolution: the same scripts evaluated at N x N (N = 256 * scale) in float,
// in continuous texture coordinates (notes-textures §9). Values are in the original's byte units (0..255).
//
// Coordinates: texel x of the original covers [x, x+1) in texel units and its value stands for its centre x + 0.5;
// hi-res pixel X covers [X/s, (X+1)/s), so formulas the original evaluates at integer pixel x (sines, cones, angle
// field, addxy) are evaluated at x_c = (X + 0.5)/s - 0.5.
//   - random ops (noise 0x132f1, clouds 0x13b5c) are generated at 256x256 by the original's generator and its call
//     order, and held as one value per texel (the box blurs that always follow then act as in the original);
//   - blobs 0x1356a: anti-aliased discs of the same area (radius sqrt(52/pi) texels) at the original's positions;
//   - box blurs 0x13300: continuous boxes of s texels, with the original's +0.5 texel shift down and right;
//   - curves 0x1360b: the continuous Hermite curve swept by the size x size square (L-infinity distance to the
//     polyline of 16*cnt points, anti-aliased);
//   - sines 0x137ea, cones 0x13af4, angle field 0x13cfe: the formulas at continuous coordinates;
//   - warp 0x137aa: bilinear reads at the continuous displaced position (the original reads the nearest texel);
//   - emboss 0x139e9 and the blob gradients 0x12e08: the original's differences over one texel, at every hi-res
//     position;
//   - point ops in float, each integer truncation (>> 8, the blurs' division) replaced by its mean effect
//     (floorMean: a floor averages x - 1/2): dropping them would move every warp by half a texel against the
//     original (the mean |difference| of map4 goes from 4.8 to 2.8 with them); byte wraps (add, addbuf, cones,
//     sines) kept as modulo 256 into [-0.5, 255.5); the dword carry of add/addbuf into the next byte only when asked
//     (testshit, whose faint "9" is made by it; elsewhere it only jitters warps by a texel);
//   - RLE pictures 0x13df4: the 256x200 bitmaps filtered up (Catmull-Rom);
//   - format ops (tile320, to16bit, interleave) only change storage and are skipped.
// The directional blur 0x1e (unused by the scripts) has no high-resolution version: a script using it falls back.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "jizz/texgen_internal.h"

namespace jizz::texgen::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTexelAngle = 2 * kPi / 256;  // one unit of the scripts' angles and frequencies

// sin for the float path (range reduced, Taylor to x^11 on [-pi/2, pi/2]: error < 6e-8)
inline double fastSin(double x) {
    const double q = std::floor(x * (1 / (2 * kPi)) + 0.5);
    double r = x - q * (2 * kPi);
    if (r > kPi / 2)
        r = kPi - r;
    else if (r < -kPi / 2)
        r = -kPi - r;
    const double r2 = r * r;
    return r * (1 + r2 * (-1.0 / 6 + r2 * (1.0 / 120 + r2 * (-1.0 / 5040 + r2 * (1.0 / 362880 + r2 * (-1.0 / 39916800))))));
}

// a byte value modulo 256, into [-0.5, 255.5) (values that round to 0..255 keep their place)
inline float wrap256(float v) {
    if (v >= -0.5f && v < 255.5f) return v;
    return v - 256.0f * std::floor((v + 0.5f) / 256.0f);
}

inline float pos0(float v) { return v > 0 ? v : 0; }

// The original's integer truncations, replaced by their mean effect: floor(x) averages x - b, with b = 1/2 for
// fractions spread evenly, (1 - g/256)/2 when the numerator is a multiple of g / 256 (scale and mix by constants:
// integer pixels times k / 256), (n - 1)/(2n) for a sum divided by n (box blurs). Dropping them instead would leave
// every warp half a texel off and the colours slightly brighter than the original's.
inline float floorMean(float v, float b) { return std::max(v - b, std::min(v, 0.0f)); }  // v>b: v-b, 0<v<=b: 0, else v
inline float biasOver256(int k) {
    k = std::abs(k);
    int g = 256;
    while (k % g) g >>= 1;  // gcd(k, 256)
    return 0.5f * (1.0f - g / 256.0f);
}
constexpr float kMulBias = 0.5f * (1.0f - 1.0f / 256);

using Plane = std::vector<float>;

constexpr uint32_t kLogoScript = 0x12a7d;  // the driver copies its B2 (logo2) after it (0x12d44)

class Hires {
public:
    Hires(const JizzExe& exe, int scale, bool carry)
        : X(exe), k(scale), N(256 * scale), NN(size_t(N) * N), mask(N - 1), carry_(carry) {
        C_ = X.f32(0x141e2);
        F255_ = X.f32(0x141e6);
    }
    HiresRun run(uint32_t script, int maxOps);

private:
    const JizzExe& X;
    const int k, N;
    const size_t NN;
    const int mask;
    const bool carry_;
    std::array<Plane, 3> B[4];
    std::array<Plane, 3> T;  // scratch copies of W's planes (0x13f84)
    Plane blurTmp_;
    int w = 0;
    Rng rng;
    float C_ = 0, F255_ = 0;
    std::array<double, 0x27> msPerOp_{};
    int peak_ = 0;

    float* P(int b, int p) {
        if (b < 0 || b > 3) throw std::runtime_error("texgen hires: buffer index out of range");
        Plane& pl = B[b][p];
        if (pl.empty()) pl.assign(NN, 0.0f);
        return pl.data();
    }
    const float* copyOfW(int p) {
        T[p].assign(P(w, p), P(w, p) + NN);
        return T[p].data();
    }
    float xc(int i) const { return (float(i) + 0.5f) / float(k) - 0.5f; }  // the original's pixel coordinate
    void fromBytes(const uint8_t* src, float* dst) const {  // one value per texel
        for (int ty = 0; ty < 256; ty++) {
            float* row = dst + size_t(ty) * k * N;
            for (int tx = 0; tx < 256; tx++) std::fill_n(row + tx * k, k, float(src[ty * 256 + tx]));
            for (int j = 1; j < k; j++) std::copy_n(row, N, row + size_t(j) * N);
        }
    }

    // last op index that reads each plane of B0..B3 (static pass over the script), to free planes after it
    std::array<std::array<int, 3>, 4> lastRead_{};
    void liveness(uint32_t a, int maxOps, bool keepB2);

    void opAdd(const Planes& Pm, int v);
    void opAddBuf(const Planes& Pm, int n);
    void opBlur(const Planes& Pm, int s);
    void opHsv2Rgb();
    void opRgb2Hsv();
    void opBlobs(int p, int n);
    void opEmboss(int p, int ang, int s, int dplane, int n);
    void opSines(int p, const int8_t prm[6], int amp);
    void opWarp(const Planes& Pm, int n);
    void opCurve(int p, int n, int size, const int16_t pts[8]);
    void opCone(int p, int r);
    void opAngle(int p);
};

// Which planes each op reads (with W as it is at that op), so a plane can be freed after its last read. Ops that
// write only part of a plane read it; fill, noise, sines, clouds, angle, pictures and copy destinations do not.
void Hires::liveness(uint32_t a, int maxOps, bool keepB2) {
    for (auto& b : lastRead_) b.fill(-1);
    int ww = 0;
    for (int i = 0;; i++) {
        const uint8_t op = i == maxOps ? 0x01 : X.u8(a);
        const Planes Pm = planesOf(X, X.u8(a + 1));
        auto rd = [&](int b, int p) {
            if (b >= 0 && b < 4) lastRead_[b][p] = i;
        };
        auto rdMask = [&](int b) {
            for (int j = 0; j < Pm.n; j++) rd(b, Pm.p[j]);
        };
        auto rdAll = [&](int b) {
            for (int p = 0; p < 3; p++) rd(b, p);
        };
        switch (op) {
        case 0x00: ww = X.u8(a + 1) & 7; break;
        case 0x01:
            rdAll(ww);
            if (keepB2) rdAll(2);
            return;
        case 0x04: case 0x05: case 0x06: case 0x09: case 0x0c: case 0x0d: case 0x18: case 0x1e: rdMask(ww); break;
        case 0x07: case 0x08: case 0x0b: case 0x23: rdAll(ww); break;
        case 0x0a: case 0x16: case 0x17: case 0x1c: rd(ww, Pm.p[0]); break;
        case 0x0e: case 0x0f: case 0x10:
            rdMask(ww);
            rdMask(X.u8(a + 2));
            break;
        case 0x11:
            rdMask(ww);
            rdMask(X.u8(a + 2));
            rd(X.u8(a + 4), planesOf(X, X.u8(a + 3)).p[0]);
            break;
        case 0x12:
            rd(ww, Pm.p[0]);
            ww = X.u8(a + 5);
            break;
        case 0x13:
            rdMask(ww);
            rd(X.u8(a + 3), planesOf(X, X.u8(a + 2)).p[0]);
            break;
        case 0x15:
            rdMask(ww);
            rd(X.u8(a + 2), 0);
            rd(X.u8(a + 2), 1);
            break;
        case 0x19: rdMask(ww); break;
        case 0x1a:
            rd(ww, Pm.p[0]);
            ww = X.u8(a + 3);
            break;
        case 0x1b:
            rd(ww, 0);
            rd(ww, 1);
            break;
        default: break;
        }
        a += opLength(X, a);
    }
}

HiresRun Hires::run(uint32_t a, int maxOps) {
    HiresRun out;
    const bool logo = a == kLogoScript;
    rng = scriptSeed(X);
    w = 0;
    liveness(a, maxOps, a == kLogoScript);
    for (int guard = 0; guard < 10000; guard++) {
        const uint8_t op = guard == maxOps ? 0x01 : X.u8(a), m = X.u8(a + 1);
        const Planes Pm = planesOf(X, m);
        const auto t0 = std::chrono::steady_clock::now();
        switch (op) {
        case 0x00:
            w = m & 7;
            if (w > 3) throw std::runtime_error("texgen hires: buffer index out of range");
            break;
        case 0x01:
            for (int p = 0; p < 3; p++) out.result[p] = B[w][p].empty() ? Plane(NN, 0.0f) : B[w][p];
            for (int p = 0; p < 3; p++) out.b2[p] = B[2][p].empty() ? Plane(NN, 0.0f) : std::move(B[2][p]);
            out.wEnd = w;
            out.ok = true;
            out.msPerOp = msPerOp_;
            out.peakPlanes = peak_;
            return out;
        case 0x02:
            for (int i = 0; i < Pm.n; i++) std::fill_n(P(w, Pm.p[i]), NN, float(X.u8(a + 2)));
            break;
        case 0x03: {
            std::vector<uint8_t> bytes(kBuf);
            noise(rng, bytes.data());
            for (int p = 0; p < 3; p++) fromBytes(bytes.data() + p * kPlane, P(w, p));
            break;
        }
        case 0x04: opBlur(Pm, 2); break;
        case 0x05: opBlur(Pm, 4); break;
        case 0x06: opBlur(Pm, 8); break;
        case 0x07: opHsv2Rgb(); break;
        case 0x08: opRgb2Hsv(); break;
        case 0x09: opAdd(Pm, X.u8(a + 2)); break;
        case 0x0a: opBlobs(Pm.p[0], X.u16(a + 2)); break;
        case 0x0b: {  // move by (dx, dy) texels
            const int dx = X.u8(a + 1) * k, dy = X.u8(a + 2) * k;
            for (int p = 0; p < 3; p++) {
                if (B[w][p].empty()) continue;
                const float* t = copyOfW(p);
                float* d = P(w, p);
                for (int y = 0; y < N; y++) {
                    const float* src = t + size_t(y) * N;
                    float* dst = d + size_t((y + dy) & mask) * N;
                    std::copy_n(src, N - dx, dst + dx);
                    std::copy_n(src + N - dx, dx, dst);
                }
            }
            break;
        }
        case 0x0c:
        case 0x0d: {
            const int kv = X.s16(a + 2);
            const float kk = float(kv) / 256.0f, b = biasOver256(kv);
            for (int i = 0; i < Pm.n; i++) {
                float* d = P(w, Pm.p[i]);
                for (size_t j = 0; j < NN; j++) {
                    const float v = floorMean(pos0(d[j]) * kk, b);
                    d[j] = op == 0x0c && v >= 0 ? std::min(v, 255.0f) : wrap256(v);
                }
            }
            break;
        }
        case 0x0e: {
            if (logo && out.beforeMul[0].empty())  // the clouds (kLogoClouds)
                for (int p = 0; p < 3; p++) out.beforeMul[p] = B[w][p].empty() ? Plane(NN, 0.0f) : B[w][p];
            const int n = X.u8(a + 2);
            for (int i = 0; i < Pm.n; i++) {
                float* d = P(w, Pm.p[i]);
                const float* s = P(n, Pm.p[i]);
                for (size_t j = 0; j < NN; j++) d[j] = floorMean(pos0(s[j]) * pos0(d[j]) * (1.0f / 256), kMulBias);
            }
            break;
        }
        case 0x0f: opAddBuf(Pm, X.u8(a + 2)); break;
        case 0x10: {
            const int n = X.u8(a + 2), al = X.u8(a + 3), ia = (256 - al) & 255;
            const float b = biasOver256(al);
            for (int i = 0; i < Pm.n; i++) {
                float* d = P(w, Pm.p[i]);
                const float* s = P(n, Pm.p[i]);
                for (size_t j = 0; j < NN; j++) d[j] = floorMean((pos0(s[j]) * al + pos0(d[j]) * ia) * (1.0f / 256), b);
            }
            break;
        }
        case 0x11: {
            const int n = X.u8(a + 2);
            const float* A = P(X.u8(a + 4), planesOf(X, X.u8(a + 3)).p[0]);
            for (int i = 0; i < Pm.n; i++) {
                float* d = P(w, Pm.p[i]);
                const float* s = P(n, Pm.p[i]);
                for (size_t j = 0; j < NN; j++) {
                    const float c = std::clamp(A[j], 0.0f, 255.0f);
                    const float v = (pos0(s[j]) * c + pos0(d[j]) * (256 - c)) * (1.0f / 256);
                    d[j] = floorMean(v, kMulBias * std::min(c, 1.0f));
                }
            }
            break;
        }
        case 0x12: {
            const int n = X.u8(a + 5);
            opEmboss(Pm.p[0], X.u8(a + 2), X.s8(a + 3), planesOf(X, X.u8(a + 4)).p[0], n);
            w = n;
            break;
        }
        case 0x13: {
            const Plane src(P(X.u8(a + 3), planesOf(X, X.u8(a + 2)).p[0]),
                            P(X.u8(a + 3), planesOf(X, X.u8(a + 2)).p[0]) + NN);
            for (int i = 0; i < Pm.n; i++) {
                float* d = P(w, Pm.p[i]);
                for (size_t j = 0; j < NN; j++) d[j] = std::min(255.0f, d[j] + src[j]);
            }
            break;
        }
        case 0x14: {
            int8_t prm[6];
            for (int i = 0; i < 6; i++) prm[i] = X.s8(a + 2 + i);
            opSines(Pm.p[0], prm, X.u8(a + 8));
            break;
        }
        case 0x15: opWarp(Pm, X.u8(a + 2)); break;
        case 0x16:
        case 0x17: {
            int16_t pts[8];
            for (int i = 0; i < 8; i++) pts[i] = X.s16(a + 4 + 2 * i);
            opCurve(Pm.p[0], X.u8(a + 2) * 16, X.u8(a + 3), pts);
            break;
        }
        case 0x18:
            for (int i = 0; i < Pm.n; i++) {
                float* d = P(w, Pm.p[i]);
                for (size_t j = 0; j < NN; j++) d[j] = 255.0f - d[j];
            }
            break;
        case 0x19: {
            const int n = X.u8(a + 2);
            for (int i = 0; i < Pm.n; i++)
                if (n != w) std::copy_n(P(w, Pm.p[i]), NN, P(n, Pm.p[i]));
            break;
        }
        case 0x1a: {
            const int n = X.u8(a + 3), dp = planesOf(X, X.u8(a + 2)).p[0];
            if (n != w || dp != Pm.p[0]) std::copy_n(P(w, Pm.p[0]), NN, P(n, dp));
            w = n;
            break;
        }
        case 0x1b: {  // R += x, G += y
            float* r = P(w, 0);
            float* g = P(w, 1);
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    const size_t j = size_t(y) * N + x;
                    r[j] = wrap256(r[j] + xc(x));
                    g[j] = wrap256(g[j] + xc(y));
                }
            break;
        }
        case 0x1c: opCone(Pm.p[0], X.u8(a + 2)); break;
        case 0x1d: {
            std::vector<uint8_t> plane(kPlane);
            clouds(rng, X.u8(a + 2), plane.data());
            fromBytes(plane.data(), P(w, Pm.p[0]));
            break;
        }
        case 0x1f: opAngle(Pm.p[0]); break;
        case 0x20:  // format ops: storage only
        case 0x21:
        case 0x24:
        case 0x25:
        case 0x26: break;
        case 0x22: {  // the picture fills rows 0..199 of a cleared plane in every script: rows 200.. stay 0
            std::vector<uint8_t> plane(kPlane, 0);
            rle4(X, X.u32(a + 2), plane.data());
            const Plane up = upsamplePicture(plane.data(), k);
            std::copy(up.begin(), up.end(), P(w, Pm.p[0]));
            break;
        }
        case 0x23: {
            int M[9];
            for (int i = 0; i < 9; i++) M[i] = X.s8(a + 1 + i);
            const float* r = copyOfW(0);
            const float* g = copyOfW(1);
            const float* b = copyOfW(2);
            for (int c = 0; c < 3; c++) {
                float* d = P(w, c);
                for (size_t j = 0; j < NN; j++)
                    d[j] = std::clamp((pos0(r[j]) * M[3 * c] + pos0(g[j]) * M[3 * c + 1] + pos0(b[j]) * M[3 * c + 2]) *
                                              (1.0f / 128) - 0.5f,
                                      0.0f, 255.0f);
            }
            break;
        }
        default:
            out.why = "op without a high-resolution version";
            return out;
        }
        if (op < 0x27) msPerOp_[op] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        {
            int live = blurTmp_.empty() ? 0 : 1;
            for (auto& b : B)
                for (auto& p : b) live += !p.empty();
            for (auto& t : T) live += !t.empty();
            peak_ = std::max(peak_, live);
        }
        for (int b = 0; b < 4; b++)
            for (int p = 0; p < 3; p++)
                if (lastRead_[b][p] <= guard && !B[b][p].empty()) Plane().swap(B[b][p]);  // not read again
        for (auto& t : T) Plane().swap(t);
        a += opLength(X, a);
    }
    throw std::runtime_error("texgen hires: script does not end");
}

// add (0x09) and addbuf (0x0f): modulo 256; with carry_, a byte that wraps adds 1 to the next texel of its dword
// (the original's packed adds), at the same position inside the texel
void Hires::opAdd(const Planes& Pm, int v) {
    std::vector<float> cy(k);
    for (int i = 0; i < Pm.n; i++) {
        float* d = P(w, Pm.p[i]);
        for (int y = 0; y < N; y++) {
            float* row = d + size_t(y) * N;
            if (!carry_) {
                for (int x = 0; x < N; x++) row[x] = wrap256(row[x] + v);
                continue;
            }
            for (int x = 0; x < N; x++) {
                const int tx = x / k, sub = x % k;
                if ((tx & 3) == 0) cy[sub] = 0;
                const float s = row[x] + v + cy[sub];
                cy[sub] = s >= 255.5f ? 1.0f : 0.0f;
                row[x] = wrap256(s);
            }
        }
    }
}

void Hires::opAddBuf(const Planes& Pm, int n) {
    std::vector<float> cy(k);
    for (int i = 0; i < Pm.n; i++) {
        float* d = P(w, Pm.p[i]);
        const float* sp = P(n, Pm.p[i]);
        for (int y = 0; y < N; y++) {
            float* row = d + size_t(y) * N;
            const float* srow = sp + size_t(y) * N;
            for (int x = 0; x < N; x++) {
                if (!carry_) {
                    row[x] = wrap256(row[x] + srow[x]);
                    continue;
                }
                const int tx = x / k, sub = x % k;
                if ((tx & 3) == 0) cy[sub] = 0;
                const float s = row[x] + srow[x] + cy[sub];
                cy[sub] = s >= 255.5f ? 1.0f : 0.0f;
                row[x] = wrap256(s);
            }
        }
    }
}

// box of s texels = s*k hi-res pixels; the window [X + off, X + off + L] (pixel j covers [j, j+1)) is centred half
// a texel up-left of the pixel's centre, as the original's offsets -s/2 .. s/2-1 are
void Hires::opBlur(const Planes& Pm, int s) {
    const int L = s * k;
    const double off = 0.5 - k * 0.5 - L * 0.5;
    const int o0 = int(std::floor(off));
    const float fa = float(off - o0), fb = 1 - fa;  // weights: pixel o0 fb, o0+1 .. o0+L-1 one each, o0+L fa
    const double inv = 1.0 / L;
    const float bias = float(s * s - 1) / float(2 * s * s);  // floor(sum / s^2)
    constexpr int kRows = 4;  // rows per pass of the horizontal running sums (independent chains)
    const int E = N + L + 1;
    std::vector<float>& tmp = blurTmp_;
    tmp.resize(NN);
    std::vector<float> ext(size_t(kRows) * E);
    std::vector<float> acc(N);
    for (int i = 0; i < Pm.n; i++) {
        if (B[w][Pm.p[i]].empty()) continue;  // all zero: stays zero
        float* d = P(w, Pm.p[i]);
        // horizontal: d -> tmp; ext[j] = row[x + o0 + j] (wrapping); window sums of ext[x+1 .. x+L-1] run along
        for (int y = 0; y < N; y += kRows) {
            for (int r = 0; r < kRows; r++) {
                const float* row = d + size_t(y + r) * N;
                float* e = ext.data() + size_t(r) * E;
                for (int j = 0; j < E; j++) e[j] = row[(j + o0) & mask];
            }
            double sum[kRows];
            for (int r = 0; r < kRows; r++) {
                const float* e = ext.data() + size_t(r) * E;
                sum[r] = 0;
                for (int j = 1; j <= L - 1; j++) sum[r] += e[j];
            }
            for (int x = 0; x < N; x++)
                for (int r = 0; r < kRows; r++) {
                    const float* e = ext.data() + size_t(r) * E;
                    tmp[size_t(y + r) * N + x] = float((fb * e[x] + sum[r] + fa * e[x + L]) * inv);
                    sum[r] += double(e[x + L]) - double(e[x + 1]);
                }
        }
        // vertical: tmp -> d, a running sum per column (float, summed afresh every 64 rows against drift)
        auto restart = [&](int y) {
            std::fill(acc.begin(), acc.end(), 0.0f);
            for (int j = y + o0 + 1; j <= y + o0 + L - 1; j++) {
                const float* row = tmp.data() + size_t(j & mask) * N;
                for (int x = 0; x < N; x++) acc[x] += row[x];
            }
        };
        const float finv = float(inv);
        for (int y = 0; y < N; y++) {
            if ((y & 63) == 0) restart(y);
            const float* r0 = tmp.data() + size_t((y + o0) & mask) * N;
            const float* rL = tmp.data() + size_t((y + o0 + L) & mask) * N;
            const float* r1 = tmp.data() + size_t((y + o0 + 1) & mask) * N;
            float* o = d + size_t(y) * N;
            float* ac = acc.data();
            for (int x = 0; x < N; x++) {
                o[x] = floorMean((fb * r0[x] + ac[x] + fa * rL[x]) * finv, bias);
                ac[x] += rL[x] - r1[x];
            }
        }
    }
}

// 0x13f98 in continuous form: H (mod 256, 6 sectors), S, V; the sector's falling edge uses 256 - f so the hexcone
// is continuous across sector borders
void Hires::opHsv2Rgb() {
    static const int order[6][3] = {{0, 1, 2}, {1, 0, 2}, {1, 2, 0}, {2, 1, 0}, {2, 0, 1}, {0, 2, 1}};
    float* pl[3] = {P(w, 0), P(w, 1), P(w, 2)};
    for (size_t j = 0; j < NN; j++) {
        float H = pl[0][j];
        if (H < 0 || H >= 256) H -= 256.0f * std::floor(H * (1.0f / 256));
        const float S = std::clamp(pl[1][j], 0.0f, 255.5f), V = std::clamp(pl[2][j], 0.0f, 255.5f);
        const float h6 = H * 6;
        const int sec = std::clamp(int(h6 * (1.0f / 256)), 0, 5);
        float f = h6 - float(sec * 256);
        if ((sec & 1) == 0) f = 256 - f;
        const float v1 = (V + 1) * (1.0f / 256);
        const float p1 = floorMean((255.5f - f * S * (1.0f / 256)) * v1, 0.5f);  // two floors
        const float p2 = floorMean((255 - S) * v1, 0.5f);
        pl[order[sec][0]][j] = V;
        pl[order[sec][1]][j] = p1;
        pl[order[sec][2]][j] = p2;
    }
}

// 0x14088 in continuous form (same sector table 0x14156)
void Hires::opRgb2Hsv() {
    static const int tab[8][4] = {{2, 1, 0, 3}, {1, 2, 0, 4}, {0, 1, 1, 0}, {0, 2, 1, 5},
                                  {2, 0, 1, 2}, {0, 1, 2, 0}, {1, 0, 2, 1}, {0, 1, 2, 0}};
    float* pl[3] = {P(w, 0), P(w, 1), P(w, 2)};
    for (size_t j = 0; j < NN; j++) {
        const float R = std::clamp(pl[0][j], 0.0f, 255.5f), G = std::clamp(pl[1][j], 0.0f, 255.5f),
                    Bc = std::clamp(pl[2][j], 0.0f, 255.5f);
        const int e = (R > G) | (R > Bc) << 1 | (G > Bc) << 2;
        float slot[3] = {0, 0, 0};
        slot[tab[e][0]] = R;
        slot[tab[e][1]] = G;
        slot[tab[e][2]] = Bc;
        const float mx = slot[0], md = slot[1], mn = slot[2];
        const int sec = tab[e][3];
        float H = 0, S = 0;
        if (mx - mn > 1e-4f) {
            S = 255 * (mx - mn) / mx;
            float f = 256 * (md - mn) / (mx - mn);
            if (sec & 1) f = 256 - f;
            H = wrap256((sec * 256 + f) * (1.0f / 6));
        }
        pl[0][j] = H;
        pl[1][j] = S;
        pl[2][j] = mx;
    }
}

// blobs: anti-aliased discs with the area of the original's 8x8 shape (52 texels), centred (x0 + 2, y0 + 4)
void Hires::opBlobs(int p, int n) {
    float* d = P(w, p);
    const double R = std::sqrt(52.0 / kPi) * k;  // radius in hi-res pixels
    const double in2 = (R - 0.5) * (R - 0.5), out2 = (R + 0.5) * (R + 0.5);
    for (int i = 0; i < n; i++) {
        const Blob b = nextBlob(rng);
        const double cx = (b.x0 + 2) * double(k), cy = (b.y0 + 4) * double(k);
        const float v = b.v;
        const int x0 = int(std::floor(cx - R - 1)), x1 = int(std::ceil(cx + R + 1));
        const int y0 = int(std::floor(cy - R - 1)), y1 = int(std::ceil(cy + R + 1));
        for (int y = y0; y <= y1; y++) {
            const double ddy = y + 0.5 - cy;
            float* row = d + size_t(y & mask) * N;
            for (int x = x0; x <= x1; x++) {
                const double ddx = x + 0.5 - cx, d2 = ddx * ddx + ddy * ddy;
                if (d2 >= out2) continue;
                float& o = row[x & mask];
                if (d2 <= in2) {
                    o = v;
                    continue;
                }
                const float cov = float(std::clamp(R - std::sqrt(d2) + 0.5, 0.0, 1.0));
                o += (v - o) * cov;
            }
        }
    }
}

// emboss: the original's one-texel forward differences at every hi-res position, light vector as in the original
void Hires::opEmboss(int p, int ang, int s, int dplane, int n) {
    const double th = double(float(double(ang) * double(C_)));
    const double sn = std::sin(th), cs = -std::cos(th);
    const double kk = s * 64 / (std::fabs(sn) + std::fabs(cs));
    const float Cx = float(kk * cs / 256), Cy = float(kk * sn / 256);
    const Plane src(P(w, p), P(w, p) + NN);
    float* d = P(n, dplane);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            const float c = src[size_t(y) * N + x];
            const float dx = src[size_t(y) * N + ((x + k) & mask)] - c;
            const float dy = src[size_t((y + k) & mask) * N + x] - c;
            d[size_t(y) * N + x] = std::clamp(dx * Cx + dy * Cy - 0.5f, 0.0f, 255.0f);  // v >> 8
        }
}

// sines: R(y) * sin(pi*sin((p4*v + p0*u)w) + p1*u*w + (p2*v + p5*u)w), R(y) = amp*sin(pi*sin(p2*v*w) + p3*v*w + p4*y*w),
// u = x + 1, v = y + 1, at the continuous coordinates (notes §4.9)
void Hires::opSines(int p, const int8_t prm[6], int amp) {
    const double w0 = kTexelAngle;
    std::vector<double> sinBX(N), cosBX(N), cY(N);
    for (int x = 0; x < N; x++) {
        const double u = xc(x) + 1.0;
        sinBX[x] = std::sin(prm[0] * u * w0);
        cosBX[x] = std::cos(prm[0] * u * w0);
        cY[x] = (prm[1] + prm[5]) * u * w0;
    }
    float* d = P(w, p);
    for (int y = 0; y < N; y++) {
        const double yc = xc(y), v = yc + 1.0;
        const double A = prm[2] * v * w0;
        const double R = amp * std::sin(kPi * std::sin(A) + prm[3] * v * w0 + prm[4] * yc * w0);
        const double aX = prm[4] * v * w0, sA = std::sin(aX), cA = std::cos(aX);
        float* row = d + size_t(y) * N;
        for (int x = 0; x < N; x++) {
            const double sX = sA * cosBX[x] + cA * sinBX[x];
            row[x] = wrap256(float(R * fastSin(kPi * sX + cY[x] + A)));
        }
    }
}

// warp: W(x) = copy(x + d(x)) with the displacement planes R, G of B[n] holding d + x (addxy); bilinear, wrapping
void Hires::opWarp(const Planes& Pm, int n) {
    const float* U = P(n, 0);
    const float* V = P(n, 1);
    const float* src[3] = {nullptr, nullptr, nullptr};
    for (int i = 0; i < Pm.n; i++) src[i] = copyOfW(Pm.p[i]);
    float* dst[3] = {nullptr, nullptr, nullptr};
    for (int i = 0; i < Pm.n; i++) dst[i] = P(w, Pm.p[i]);
    for (size_t j = 0; j < NN; j++) {
        const float sx = (U[j] + 0.5f) * k - 0.5f, sy = (V[j] + 0.5f) * k - 0.5f;
        const float fx0 = std::floor(sx), fy0 = std::floor(sy);
        const float fx = sx - fx0, fy = sy - fy0;
        const int x0 = int(fx0) & mask, y0 = int(fy0) & mask, x1 = (x0 + 1) & mask, y1 = (y0 + 1) & mask;
        const size_t a = size_t(y0) * N, b = size_t(y1) * N;
        for (int i = 0; i < Pm.n; i++) {
            const float* s = src[i];
            const float top = s[a + x0] + (s[a + x1] - s[a + x0]) * fx;
            const float bot = s[b + x0] + (s[b + x1] - s[b + x0]) * fx;
            dst[i][j] = top + (bot - top) * fy;
        }
    }
}

// L-infinity distance from (ax, ay) to the segment s*(dx, dy), s in [0, 1]
inline double linfSegment(double ax, double ay, double dx, double dy) {
    double best = std::max(std::fabs(ax), std::fabs(ay));
    auto at = [&](double s) {
        s = std::clamp(s, 0.0, 1.0);
        best = std::min(best, std::max(std::fabs(ax - s * dx), std::fabs(ay - s * dy)));
    };
    at(1.0);
    if (dx != 0) at(ax / dx);
    if (dy != 0) at(ay / dy);
    if (dx != dy) at((ax - ay) / (dx - dy));
    if (dx != -dy) at((ax + ay) / (dx + dy));
    return best;
}

// curve: union of the size x size squares [px - size, px] x [py - size, py] along the continuous curve
void Hires::opCurve(int p, int n, int size, const int16_t pts[8]) {
    double F[2][4];
    for (int c = 0; c < 2; c++) {
        const int p0 = pts[4 * c], p1 = pts[4 * c + 1], t0 = pts[4 * c + 2], t1 = pts[4 * c + 3];
        F[c][0] = p0;
        F[c][1] = t0;
        F[c][2] = -(3 * (p0 - p1) + 2 * t0 + t1);
        F[c][3] = 2 * (p0 - p1) + t0 + t1;
    }
    const double h = size * 0.5;
    std::vector<double> cx(n), cy(n);  // square centres, texel units
    for (int i = 0; i < n; i++) {
        const double t = double(i + 1) / n;
        const double r = F[0][0] + t * (F[0][1] + t * (F[0][2] + t * F[0][3]));
        const double a = (F[1][0] + t * (F[1][1] + t * (F[1][2] + t * F[1][3]))) * kTexelAngle;
        cx[i] = r * std::cos(a) - h;
        cy[i] = -r * std::sin(a) - h;
    }
    std::vector<float> dist(NN, 1e9f);
    const double inner = h - 0.5 / k;  // fully covered below this distance
    for (int i = 0; i < n; i++) {
        const int i1 = std::min(i + 1, n - 1);
        const double ax0 = cx[i], ay0 = cy[i], ddx = cx[i1] - ax0, ddy = cy[i1] - ay0;
        const double m = h + 1.0 / k;
        const int X0 = int(std::floor((std::min(ax0, ax0 + ddx) - m) * k)), X1 = int(std::ceil((std::max(ax0, ax0 + ddx) + m) * k));
        const int Y0 = int(std::floor((std::min(ay0, ay0 + ddy) - m) * k)), Y1 = int(std::ceil((std::max(ay0, ay0 + ddy) + m) * k));
        for (int Y = Y0; Y <= Y1; Y++) {
            const double qy = (Y + 0.5) / k - ay0;
            float* drow = dist.data() + size_t(Y & mask) * N;
            for (int Xp = X0; Xp <= X1; Xp++) {
                float& dv = drow[Xp & mask];
                if (dv <= inner) continue;
                const double dd = linfSegment((Xp + 0.5) / k - ax0, qy, ddx, ddy);
                if (dd < dv) dv = float(dd);
            }
        }
    }
    float* d = P(w, p);
    for (size_t j = 0; j < NN; j++) {
        const float cov = std::clamp(float((h - dist[j]) * k + 0.5), 0.0f, 1.0f);
        d[j] += (255.0f - d[j]) * cov;
    }
}

// cone: adds max(0, 255 (r - d) / r), d = torus distance to the centre of texel (0, 0); modulo 256
void Hires::opCone(int p, int r) {
    float* d = P(w, p);
    const double K = 255.0 / r;
    std::vector<double> sq(N);
    for (int i = 0; i < N; i++) {
        double c = xc(i);
        if (c >= 127.5) c -= 256;
        sq[i] = c * c;
    }
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            const double v = (r - std::sqrt(sq[x] + sq[y])) * K;
            if (v > 0) {
                float& o = d[size_t(y) * N + x];
                o = wrap256(float(o + v));
            }
        }
}

// angle field: atan2 around the centre of texel (0, 0) in 1/256 turns, with the seams at x, y = -128
void Hires::opAngle(int p) {
    float* d = P(w, p);
    for (int y = 0; y < N; y++) {
        double cy = xc(y);
        if (cy >= 127.5) cy -= 256;
        for (int x = 0; x < N; x++) {
            double cx = xc(x);
            if (cx >= 127.5) cx -= 256;
            d[size_t(y) * N + x] = wrap256(float(std::atan2(cy, cx) / kTexelAngle));
        }
    }
}

}  // namespace

HiresRun runHires(const JizzExe& exe, uint32_t script, int scale, bool emulateCarry, int maxOps) {
    Hires h(exe, scale, emulateCarry);
    return h.run(script, maxOps);
}

// 0x12d87: (150 + 100 sin(2pi * 0x240 * y / 16384)) / 256 per row, at the continuous row coordinate
void stripesHires(int scale, std::array<std::vector<float>, 3>& m) {
    const int N = 256 * scale;
    for (int y = 0; y < N; y++) {
        const double yc = (y + 0.5) / scale - 0.5;
        const float f = float((150 + 100 * std::sin(2 * kPi * 0x240 * yc / 16384)) / 256);
        for (auto& pl : m)
            for (int x = 0; x < N; x++) pl[size_t(y) * N + x] *= f;
    }
}

// 0x12e08: B = R(y + 1) - R(y - 1), G = R(x + 2) - R(x) (one texel = scale pixels), signed
void gradientsHires(int scale, std::array<std::vector<float>, 3>& t) {
    const int N = 256 * scale, mask = N - 1;
    const std::vector<float>& R = t[0];
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            t[2][size_t(y) * N + x] = R[size_t((y + scale) & mask) * N + x] - R[size_t((y - scale) & mask) * N + x];
            t[1][size_t(y) * N + x] = R[size_t(y) * N + ((x + 2 * scale) & mask)] - R[size_t(y) * N + x];
        }
}

// Catmull-Rom, separable, wrapping; texel x stands for x + 0.5
std::vector<float> upsamplePicture(const uint8_t* plane, int scale) {
    const int N = 256 * scale;
    auto weights = [&](int i, int& base, float w[4]) {
        const double c = (i + 0.5) / scale - 0.5;
        const double f0 = std::floor(c), t = c - f0;
        base = int(f0) - 1;
        const double t2 = t * t, t3 = t2 * t;
        w[0] = float(-0.5 * t3 + t2 - 0.5 * t);
        w[1] = float(1.5 * t3 - 2.5 * t2 + 1);
        w[2] = float(-1.5 * t3 + 2 * t2 + 0.5 * t);
        w[3] = float(0.5 * t3 - 0.5 * t2);
    };
    std::vector<float> rows(size_t(256) * N);  // horizontal pass: 256 rows of N
    for (int x = 0; x < N; x++) {
        int b;
        float wt[4];
        weights(x, b, wt);
        for (int y = 0; y < 256; y++) {
            float s = 0;
            for (int i = 0; i < 4; i++) s += wt[i] * plane[y * 256 + ((b + i) & 255)];
            rows[size_t(y) * N + x] = s;
        }
    }
    std::vector<float> out(size_t(N) * N);
    for (int y = 0; y < N; y++) {
        int b;
        float wt[4];
        weights(y, b, wt);
        for (int x = 0; x < N; x++) {
            float s = 0;
            for (int i = 0; i < 4; i++) s += wt[i] * rows[size_t((b + i) & 255) * N + x];
            out[size_t(y) * N + x] = std::clamp(s, 0.0f, 255.0f);
        }
    }
    return out;
}

}  // namespace jizz::texgen::detail
