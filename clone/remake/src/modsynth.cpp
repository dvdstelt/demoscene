// The module builder 0x414aee and its two sample synthesisers, the drums of 0x4149b4 (synth A) and the tones of
// 0x414a38 (synth B); clone/re/notes-music.md section 2, clone/re/models/music/modsynth.py.
//
// The original computes on the x87 with control word 0x1f7f: 64-bit mantissas, every result truncated toward zero.
// With truncation every intermediate value can be computed exactly and truncated once, so doubles rounded to
// nearest give the same samples (all 490,568 checked against the capture, notes section 2.4) provided that the
// float stores (amp x 255, f - df, df - ddf) and the float -> int conversions truncate. This file must be compiled
// without floating-point contraction (no FMA for s1 * D + P; see CMakeLists.txt).
//
// Everything is read from the image: the stored module part (0x406862, 0xe153 bytes), the synthesisers' arguments
// (decoded from the call sequence 0x414b1f..0x414efd: push imm8 / imm32, mov edx = sample count, lea ecx /
// add ecx = destination, call), and the constants 255.0 (0x41a450, 0x41a454) and 2 pi (0x41a458).
#include "modsynth.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace modsynth {

namespace {

constexpr uint32_t kBufSize = 0x124f80;          // the allocation (mov eax, 0x124f80 at 0x414afd)
constexpr uint32_t kStored = 0x406862;           // stored module part ...
constexpr uint32_t kStoredLen = 0xe153;          // ... copied by the byte loop 0x414b0b
constexpr uint32_t kCallsFrom = 0x414b1f, kCallsTo = 0x414efd;
constexpr uint32_t kSynthA = 0x4149b4, kSynthB = 0x414a38;
constexpr uint32_t k255A = 0x41a450, k255B = 0x41a454, kTwoPi = 0x41a458;
constexpr int kClamp = 0x7e81;                   // cmp with 0x7e81 / 0xffff817f
constexpr int kCalls = 20;

class Image {
public:
    explicit Image(std::span<const uint8_t> d) : d_(d) {}
    uint8_t u8(uint32_t a) const {
        if (a < kImageBase || a - kImageBase >= d_.size())
            throw std::runtime_error("CLONE.EXE image: address out of range (music module)");
        return d_[a - kImageBase];
    }
    uint32_t u32(uint32_t a) const { return u8(a) | u8(a + 1) << 8 | u8(a + 2) << 16 | uint32_t(u8(a + 3)) << 24; }
    float f32(uint32_t a) const {
        const uint32_t b = u32(a);
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    double f64(uint32_t a) const {
        const uint64_t b = u32(a) | uint64_t(u32(a + 4)) << 32;
        double d;
        std::memcpy(&d, &b, 8);
        return d;
    }

private:
    std::span<const uint8_t> d_;
};

struct Call {
    uint32_t synth = 0;           // kSynthA / kSynthB
    uint32_t ofs = 0, n = 0;      // destination (offset in the buffer) and sample count
    float arg[5] = {};            // stack arguments in order (the last push is the first argument)
    int args = 0;
};

// Decodes 0x414b1f..0x414efd (notes section 2.1): each call's arguments are the immediates pushed since the
// previous call, its count the last `mov edx` before it, its destination ecx (lea ecx, [ebx + disp], then add ecx).
std::vector<Call> decodeCalls(const Image& img) {
    std::vector<Call> calls;
    std::vector<uint32_t> pushes;
    uint32_t edx = 0, ecx = 0;
    bool haveEcx = false;
    for (uint32_t a = kCallsFrom; a < kCallsTo;) {
        const uint8_t op = img.u8(a), op2 = img.u8(a + 1);
        if (op == 0x6a) {  // push imm8 (sign-extended)
            pushes.push_back(uint32_t(int32_t(int8_t(op2))));
            a += 2;
        } else if (op == 0x68) {  // push imm32
            pushes.push_back(img.u32(a + 1));
            a += 5;
        } else if (op == 0xba) {  // mov edx, imm32 (the count, or the "." string for int 21h)
            edx = img.u32(a + 1);
            a += 5;
        } else if (op == 0x8d && op2 == 0x8b) {  // lea ecx, [ebx + disp32]
            ecx = img.u32(a + 2);
            haveEcx = true;
            a += 6;
        } else if (op == 0x81 && op2 == 0xc1) {  // add ecx, imm32
            ecx += img.u32(a + 2);
            a += 6;
        } else if (op == 0x89 && op2 == 0xc8) {  // mov eax, ecx
            a += 2;
        } else if (op == 0x60 || op == 0x61) {  // pushal / popal
            a += 1;
        } else if ((op == 0xb4 && op2 == 0x09) || (op == 0xcd && op2 == 0x21)) {  // mov ah, 9 / int 21h
            a += 2;
        } else if (op == 0xe8) {  // call rel32
            const uint32_t target = a + 5 + img.u32(a + 1);
            const int want = target == kSynthA ? 4 : target == kSynthB ? 5 : -1;
            if (want < 0 || int(pushes.size()) != want || !haveEcx)
                throw std::runtime_error("CLONE.EXE image: unexpected module builder code");
            Call c;
            c.synth = target;
            c.ofs = ecx;
            c.n = edx;
            c.args = want;
            for (int i = 0; i < want; i++) {
                const uint32_t b = pushes[size_t(want - 1 - i)];
                std::memcpy(&c.arg[i], &b, 4);
            }
            if (c.n == 0 || c.n > kBufSize / 2 || c.ofs > kBufSize - 2 * c.n)
                throw std::runtime_error("CLONE.EXE image: module builder writes outside its buffer");
            calls.push_back(c);
            pushes.clear();
            a += 5;
        } else {
            throw std::runtime_error("CLONE.EXE image: unexpected instruction in the module builder");
        }
    }
    if (calls.size() != size_t(kCalls)) throw std::runtime_error("CLONE.EXE image: expected 20 sample synthesisers");
    return calls;
}

// fstp dword with the rounding control "toward zero"
float truncToFloat(double d) {
    float r = float(d);  // to nearest
    if (std::fabs(double(r)) > std::fabs(d)) r = std::nextafter(r, 0.0f);
    return r;
}

// fistp (truncating) and the clamp to +-0x7e81
int16_t toSample(double y) {
    const int v = int(y);
    return int16_t(std::clamp(v, -kClamp, kClamp));
}

void store(uint8_t* dst, int i, int16_t v) {
    dst[2 * i] = uint8_t(uint16_t(v) & 0xff);
    dst[2 * i + 1] = uint8_t(uint16_t(v) >> 8);
}

// 0x4149b4: x[i] = trunc(sin(i * f_i) * amp * 255), f and df stepped (and stored as floats) before the sample
void synthA(uint8_t* dst, int n, float amp, float f, float df, float ddf, float k255) {
    const double a = double(truncToFloat(double(amp) * double(k255)));
    for (int i = 0; i < n; i++) {
        const float fn = truncToFloat(double(f) - double(df));
        const float dfn = truncToFloat(double(df) - double(ddf));
        const double x = double(i) * double(fn);  // exact (fild i; fmul dword [f])
        store(dst, i, toSample(std::sin(x) * a));
        f = fn;
        df = dfn;
    }
}

// 0x414a38: x[i] = trunc(sin(2 pi i / E) * sin(2 pi i / (P + D sin(2 pi i / M) + i / 999999999)) * amp * 255)
void synthB(uint8_t* dst, int n, float P, float M, float D, float E, float amp, float k255, double twoPi) {
    const double a = double(truncToFloat(double(amp) * double(k255)));
    for (int i = 0; i < n; i++) {
        const double di = double(i);
        const double s1 = std::sin(twoPi / double(M) * di);
        const double t = s1 * double(D);                      // (two roundings: no FMA)
        const double per = (t + double(P)) + double(i / 999999999);  // idiv: 0 for every count here
        const double s2 = std::sin(twoPi / per * di);         // fstp qword: exact
        const double g = std::sin(twoPi / double(E) * di);
        store(dst, i, toSample(g * s2 * a));
    }
}

}  // namespace

std::vector<uint8_t> buildModule(std::span<const uint8_t> image) {
    const Image img(image);
    const std::vector<Call> calls = decodeCalls(img);
    std::vector<uint8_t> buf(kBufSize, 0);
    for (uint32_t i = 0; i < kStoredLen; i++) buf[i] = img.u8(kStored + i);
    if (std::memcmp(buf.data(), "MXM\0", 4) != 0) throw std::runtime_error("CLONE.EXE image: no MXM module at 0x406862");
    const float k255a = img.f32(k255A), k255b = img.f32(k255B);
    const double twoPi = img.f64(kTwoPi);

    // the 20 calls write disjoint ranges: run them on worker threads
    std::atomic<int> next{0};
    auto worker = [&] {
        for (int k; (k = next.fetch_add(1)) < kCalls;) {
            const Call& c = calls[size_t(k)];
            uint8_t* dst = buf.data() + c.ofs;
            if (c.synth == kSynthA)
                synthA(dst, int(c.n), c.arg[0], c.arg[1], c.arg[2], c.arg[3], k255a);
            else
                synthB(dst, int(c.n), c.arg[0], c.arg[1], c.arg[2], c.arg[3], c.arg[4], k255b, twoPi);
        }
    };
    const int nt = int(std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 8u));
    std::vector<std::thread> pool;
    for (int t = 1; t < nt; t++) pool.emplace_back(worker);
    worker();
    for (std::thread& t : pool) t.join();
    return buf;
}

}  // namespace modsynth
