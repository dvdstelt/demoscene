// The "mp4 cruncher" 0x1632b (Probe): synthesises the samples of JIZZ's music module (notes-music.md section 2;
// mirrors jizz/re/models/music/mp4synth.py, which reproduces dump/smokeout.mxm byte for byte).
// Routines: 0x1632b expand (record loop, pass loop, 0x1628f note loop, 0x162cd decimate + append), 0x161f2 note on,
// 0x1601d sample, 0x15e93 filter, 0x15f55 shaped oscillator, 0x15ede echo; the random generator 0x141f6.
//
// The original runs on the x87 with the precision control at 24 bits: every arithmetic result is rounded once to a
// 24-bit mantissa (nearest even), loads of doubles and integers are exact. Here every such result is computed in
// IEEE double and rounded to float (r()): for two operands of at most 24 bits that is the single rounding (the
// double result is exact or rounds harmlessly). Operations with a full 53-bit double operand (MUL * A0, MUL * B0,
// A * R, s * GAIN, pitch * 1.02, d * FB, 1 - Q) need the exact result rounded once to float: mulOnce() / subOnce()
// form it as an unevaluated double pair (Dekker product, Knuth sum) and decide the one rounding from that.
// This needs IEEE double arithmetic without excess precision or contraction: the file is compiled with
// -ffp-contract=off (/fp:precise on MSVC), see CMakeLists.txt.
#include "mp4synth.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "exe.h"

#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "mp4synth.cpp needs float and double arithmetic evaluated in their own precision (SSE2, not x87)"
#endif

namespace {

// ---- addresses in the program image (notes-music.md 2.1) ----
constexpr uint32_t kStored = 0x1f644;     // MXM bytes 0..0x7f70 as they are
constexpr uint32_t kStoredLen = 0x7f71;
constexpr uint32_t kRecords = 0x16423;    // 12 synth records of 0x7b bytes
constexpr uint32_t kRecLen = 0x7b;
constexpr uint32_t kRecOrder = 0x163f3;   // 12 dwords: record number + 1, in output order
constexpr uint32_t kRecCopy = 0x15e18;    // where 0x1632b copies the record; the byte after it is code
constexpr uint32_t kK3529 = 0x15dc6;      // qword 3529.0 (samples per note - 1)
constexpr uint32_t kK102 = 0x1605f;       // qword 1.02 (detune of the second oscillator)
constexpr uint32_t kK32767 = 0x16015;     // qword 32767.0
constexpr uint32_t kK4 = 0x1628b;         // float 4.0 (filter x4 notes)
constexpr uint32_t kRngState = 0x141ea;   // 3 dwords: state of the random generator 0x141f6 (0 in the image)

// ---- code constants ----
constexpr int kRecords_n = 12;
constexpr int kNotes = 16;
constexpr int kNoteLast = 0xdc9;          // 3529: samples 0..3529 per note
constexpr int kGlide = 0x6e4;             // glide length in samples
constexpr int kEchoBack = 0x295b;         // echo: 3 notes back ...
constexpr int kEchoWrap = 0xb335;         // ... or, for the first 3 notes, 13 notes ahead
constexpr int kHalveLen = 0xe290;         // record 7 is halved over these bytes
constexpr int kHalvedRecord = 7;
constexpr int kDecimLast = 0x6d60;
constexpr int kCopyLen = 0x1b58 * 4;      // 28,000 bytes appended per pass (+ the last one again)
constexpr int kPrecalcRandCalls = 80;     // calls of 0x141f6 by the precalc 0x11dfe before the expander runs

// ---- x87 24-bit rounding ----

double r(double x) { return double(float(x)); }  // round to a 24-bit mantissa, nearest even

// hi + lo (|lo| <= half an ulp of hi) rounded once to float: f = float(hi) is right unless hi lies exactly halfway
// between f and its neighbour g, in which case lo decides.
double roundPair(double hi, double lo) {
    const float f = float(hi);
    if (double(f) == hi || lo == 0.0 || !std::isfinite(f)) return f;
    const bool up = hi > double(f);  // neighbour on the side of hi
    float g;
    if (f == 0.0f) {
        g = up ? FLT_TRUE_MIN : -FLT_TRUE_MIN;
    } else {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        bits = (up == (f > 0.0f)) ? bits + 1 : bits - 1;
        std::memcpy(&g, &bits, 4);
    }
    if (hi - double(f) != double(g) - hi) return f;  // not a midpoint
    return (lo > 0.0) == (g > f) ? g : f;
}

// Dekker's exact product a * b = hi + lo (no FMA needed; exact for these magnitudes)
void twoProduct(double a, double b, double& hi, double& lo) {
    constexpr double kSplit = 134217729.0;  // 2^27 + 1
    hi = a * b;
    double t = kSplit * a;
    const double ah = t - (t - a), al = a - ah;
    t = kSplit * b;
    const double bh = t - (t - b), bl = b - bh;
    lo = ((ah * bh - hi) + ah * bl + al * bh) + al * bl;
}

double mulOnce(double a, double b) {  // a * b rounded once to 24 bits, for any doubles a, b
    double hi, lo;
    twoProduct(a, b, hi, lo);
    return roundPair(hi, lo);
}

double subOnce(double a, double b) {  // a - b rounded once to 24 bits (Knuth's two-sum)
    const double s = a - b, bb = s - a, err = (a - (s - bb)) + (-b - bb);
    return roundPair(s, err);
}

// fistp with the rounding control at nearest even: adding and removing 1.5 * 2^52 rounds to an integer in the
// default rounding mode (exact for |x| < 2^51; the values here are far smaller)
int64_t fist(double x) {
    constexpr double kMagic = 6755399441055744.0;
    if (!(std::fabs(x) < 2251799813685248.0)) return int64_t(std::nearbyint(x));
    return int64_t((x + kMagic) - kMagic);
}

int32_t s16(uint32_t v) { return int16_t(uint16_t(v)); }
int32_t s8(uint32_t v) { return int8_t(uint8_t(v)); }
uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

// The random generator 0x141f6 (state 0x141ea / 0x141ee / 0x141f2).
struct Rng {
    uint32_t s0 = 0, s1 = 0, s2 = 0;
    uint32_t operator()() {
        uint32_t eax = s0 + 0xe372471u, ebx = s1 + 0xbc5ea9u, ecx = s2 + 0xc8ade7u;
        eax ^= ebx;
        uint64_t p = uint64_t(int64_t(int32_t(eax)) * int64_t(int32_t(ecx)));  // imul ecx
        eax = uint32_t(p);
        uint32_t edx = uint32_t(p >> 32);
        edx = rol(edx, 13) + eax;
        ecx = rol(ecx, 14);
        edx -= ebx;
        eax ^= edx;
        ebx += ecx;
        ecx ^= edx;
        eax -= ebx;
        ebx = rol(ebx, 9);
        p = uint64_t(eax) * ecx;  // mul ecx
        eax = uint32_t(p);
        edx = uint32_t(p >> 32);
        ebx ^= edx;
        eax += 0x1e42ac3u;
        ebx += 0x5edf04u;
        ecx += 0x11a2ec1u;
        s0 = ebx;
        s1 = ecx;
        s2 = eax;
        return eax;
    }
};

// A synth record as copied to 0x15e18 (notes-music.md 2.1), plus the code byte behind the copy: the arrays are
// also read with index 16 (X4[16] = TIE[0], PITCH[16] = X4[0] | X4[1] << 8, TIE[16] = that code byte).
struct Record {
    double a0, b0, r, q, mul, gain, fb;
    int mode, npass, harm;
    uint8_t raw[kRecLen + 1];
    uint32_t pitch(int i) const { return uint32_t(raw[0x3b + 2 * i] | raw[0x3c + 2 * i] << 8); }
    int x4(int i) const { return raw[0x5b + i]; }
    int tie(int i) const { return raw[0x6b + i]; }
};

Record readRecord(const JizzExe& x, int n) {
    Record R;
    std::memcpy(R.raw, x.at(kRecords + kRecLen * uint32_t(n), kRecLen), kRecLen);
    R.raw[kRecLen] = x.u8(kRecCopy + kRecLen);
    auto dbl = [&R](int o) {
        double d;
        std::memcpy(&d, R.raw + o, 8);
        return d;
    };
    R.a0 = dbl(0x00);
    R.b0 = dbl(0x08);
    R.r = dbl(0x10);
    R.q = dbl(0x18);
    R.mode = R.raw[0x20];
    R.npass = R.raw[0x21];
    R.mul = dbl(0x22);
    R.gain = dbl(0x2a);
    R.fb = dbl(0x32);
    R.harm = R.raw[0x3a];
    return R;
}

class Synth {
public:
    explicit Synth(const JizzExe& x) : x_(x) {
        k3529_ = x.f64(kK3529);
        k102_ = x.f64(kK102);
        k32767_ = x.f64(kK32767);
        k4_ = x.f32(kK4);
        rng_.s0 = x.u32(kRngState);
        rng_.s1 = x.u32(kRngState + 4);
        rng_.s2 = x.u32(kRngState + 8);
        for (int i = 0; i < kPrecalcRandCalls; i++) rng_();
        shapeTab_.resize(0x10000);
        for (int32_t p = -0x8000; p < 0x8000; p++) shapeTab_[uint16_t(p)] = shape(p);
    }

    void run(std::vector<uint8_t>& out) {
        for (int it = 0; it < kRecords_n; it++) {  // [0x163eb]
            const int recno = int(x_.u32(kRecOrder + 4 * uint32_t(it))) - 1;
            ph_ = ph2_ = 0;
            a_ = b_ = q_ = rc_ = d_ = 0.0;
            R_ = readRecord(x_, recno);
            omq_ = subOnce(1.0, R_.q);
            for (int i = 0; i < 256; i++) echoTab_[i] = fist(mulOnce(double(i - 128), R_.fb));
            for (int pass = 0; pass <= R_.npass; pass++) {  // [0x163e6]
                noteOn();  // with seg = 16 (0 for the very first): an extra MUL step of A0 / B0
                for (seg_ = 0; seg_ < kNotes; seg_++) {  // 0x1628f
                    if (R_.tie(seg_) == 0) noteOn();
                    k_ = 0;
                    for (int j = 0; j <= kNoteLast; j++) sample(j);
                }
                seg_ = kNotes;
                echo();
                flush(recno, out);
            }
        }
    }

private:
    // 0x161f2
    void noteOn() {
        y1_ = 0;
        ph_ = 0x8002;
        R_.b0 = mulOnce(R_.mul, R_.b0);
        b_ = R_.b0;
        R_.a0 = mulOnce(R_.mul, R_.a0);
        a_ = R_.a0;
        q_ = R_.q;
        rc_ = R_.r;
        if (R_.x4(seg_) == 1) {
            b_ = r(b_ * k4_);
            a_ = r(a_ * k4_);
        }
        pitch_ = R_.pitch(seg_);
    }

    // 32767 * t^7 of the phase as a signed 16-bit value (0x15f55; tabulated in shapeTab_)
    double shape(int32_t p) const {
        double t = p > 0 ? double(32767 - p) : double(p);
        t = r(t / k32767_);
        const double t2 = r(t * t), t4 = r(t2 * t2), t6 = r(t2 * t4);
        return r(r(t6 * k32767_) * t);
    }

    // 0x15f55: mode 2 oscillator; the loop recomputes (and overwrites) the base on every pass
    int64_t shaped() {
        int64_t s = 0;
        uint32_t n = 0;
        for (;;) {
            s = fist(shapeTab_[ph_ & 0xffff]);
            if (R_.harm != 0) {
                uint32_t e = cnt_ * n;
                uint32_t eax = uint32_t((uint64_t(e) * uint32_t(pitch_)) >> 7);  // mul; shrd eax, edx, 7
                if (!(n & 1)) eax = 0u - eax;
                ph2_ = (eax + ph_) & 0xffff;
                s = fist(r(shapeTab_[ph2_] + double(s)));
            }
            n++;
            if (s8(n) > s8(uint32_t(R_.harm))) break;
        }
        return s;
    }

    // 0x15e93: the swept 2-pole resonator; x = input, y1 = previous output
    int64_t filter(int64_t x) {
        a_ = mulOnce(a_, rc_);
        const double c = r(a_ + b_);
        const double m = r(c * r(double(x + y1_)));
        const double dd = r(d_ - m);
        d_ = r(omq_ * dd);
        return fist(r(d_ + double(y1_)));
    }

    // 0x1601d: one sample
    void sample(int j) {
        int64_t s = s16(ph_);
        if (R_.harm != 0) {
            if (pitch_ != detunePitch_) {
                detunePitch_ = pitch_;
                detune_ = uint32_t(fist(mulOnce(double(pitch_), k102_)));
            }
            ph2_ = (ph2_ + detune_) & 0xffff;
            s += s16(ph2_);
        }
        if (R_.mode == 2)
            s = shaped();
        else if (R_.mode == 4)
            s = int64_t((rng_() & 0xffffff) % 0xfa01) - 0x7d00;
        if (seg_ != 0 && R_.tie(seg_) == 1) {  // glide from the previous note's pitch
            if (k_ < kGlide) {
                const double k2 = double(2 * k_);
                double u = r(r(k3529_ - k2) / k3529_);
                u = r(u * double(R_.pitch(seg_ - 1)));
                const double v = r(r(k2 / k3529_) * double(R_.pitch(seg_)));
                pitch_ = fist(r(u + v));
            } else {
                pitch_ = R_.pitch(seg_);
            }
        }
        ph_ = (ph_ + uint32_t(pitch_)) & 0xffff;
        s = filter(fist(mulOnce(double(s), R_.gain)));
        cnt_++;
        y1_ = s;
        if (seg_ <= 15 && R_.tie(seg_ + 1) == 0) {  // the next note is not tied: linear fade-out
            const double f = r(1.0 - r(double(j) / k3529_));
            s = fist(r(f * double(s)));
        }
        if (s > 0x7fff) s = 0x7fff;
        if (s < -0x7fff) s = -0x7fff;
        W_[cnt_ & 0xffff] = uint8_t((s >> 8) & 0xff);
        k_++;
    }

    // 0x15ede: feedback echo over the 16 notes, in place
    void echo() {
        for (int e = 0; e < kNotes; e++)
            for (int bx = 0; bx <= kNoteLast; bx++) {
                const int i = e * kNoteLast + bx;
                const int dly = e >= 3 ? s8(W_[i - kEchoBack]) : s8(W_[i + kEchoWrap]);
                int64_t a = s8(W_[i]) + echoTab_[dly + 128];
                if (a > 0x7f) a = 0x7f;
                if (a < -0x7f) a = -0x7f;
                W_[i] = uint8_t(a & 0xff);
            }
    }

    // 0x162cd: (halve record 7,) decimate by 2, append 28,000 bytes + the last one again
    void flush(int recno, std::vector<uint8_t>& out) {
        if (recno == kHalvedRecord)
            for (int i = 0; i < kHalveLen; i++) W_[i] = uint8_t((s8(W_[i]) >> 1) & 0xff);
        for (int i = 0; i <= kDecimLast; i++) W_[i] = W_[2 * i];
        cnt_ = 0;
        out.insert(out.end(), W_, W_ + kCopyLen);
        out.push_back(W_[kCopyLen - 1]);
    }

    const JizzExe& x_;
    double k3529_, k102_, k32767_, k4_;
    Rng rng_;
    Record R_{};
    uint8_t W_[0x10000] = {};  // [0x15dce], zeroed by main
    uint32_t cnt_ = 0;         // [0x15dd2]
    int seg_ = 0;              // [0x15e00]
    int k_ = 0;                // [0x15dfe]
    int64_t y1_ = 0;           // [0x15dfa]
    int64_t pitch_ = 0;        // [0x15df2]
    uint32_t ph_ = 0, ph2_ = 0;  // [0x15dd6], [0x15dd8]
    double a_ = 0, b_ = 0, q_ = 0, rc_ = 0, d_ = 0;  // [0x15de2], [0x15dea], [0x15e04], [0x15e0c], [0x15dda]
    double omq_ = 0;           // 1 - Q, rounded once
    int64_t echoTab_[256] = {};  // fist(d * FB) for d = -128..127
    std::vector<double> shapeTab_;  // shape() of every 16-bit phase
    int64_t detunePitch_ = -1;      // fist(pitch * 1.02) of this pitch is in detune_
    uint32_t detune_ = 0;
};

}  // namespace

std::vector<uint8_t> expandModule(const JizzExe& exe) {
    std::vector<uint8_t> mod = exe.bytes(kStored, kStoredLen);
    mod.reserve(mod.size() + 32 * (kCopyLen + 1));
    Synth syn(exe);
    syn.run(mod);
    return mod;
}
