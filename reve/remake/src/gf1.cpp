// The GUS as Reve's music player sees it (the register log, the DRAM and the timers of reve/re/notes-music.md
// sections 2-5 and 9) and a model of the GF1 synthesiser that renders the voices (section 10; the class GF1 of
// reve/re/models/music/lcaplay.py at any output rate). See gf1.h.
#include "gf1.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gus {

// ---------------------------------------------------------------------------------------------- the timers

void Timers::write(int reg, int val) {
    if (reg == 0x45) enable_ = val & 0xff;
    else if (reg == 0x46) count_[0] = val & 0xff;
    else if (reg == 0x47) count_[1] = val & 0xff;
}

void Timers::control(int val) {
    if (val & 0x80) return;  // reset the IRQ flags; the other bits are ignored
    for (int i = 0; i < 2; i++) {
        const int64_t unit = i ? 320 : 80;
        if (!(val & (1 << i))) next_[i] = -1;
        else if (next_[i] < 0) next_[i] = now_ + (256 - count_[i]) * unit;  // counts from the count register now
    }
}

int64_t Timers::nextIrq() {
    if (!(enable_ & 0x0c)) return -1;
    for (;;) {
        int i = -1;
        for (int j = 0; j < 2; j++)
            if (next_[j] >= 0 && (i < 0 || next_[j] < next_[i])) i = j;
        if (i < 0) return -1;
        const int64_t t = next_[i];
        now_ = t;
        next_[i] = t + (256 - count_[i]) * (i ? 320 : 80);  // reloaded at the overflow
        if (enable_ & (4 << i)) {
            // the other timer overflowing at the same instant does so before the IRQ handler can change reg 0x45
            for (int j = 0; j < 2; j++)
                if (next_[j] == t) next_[j] = t + (256 - count_[j]) * (j ? 320 : 80);
            return t;
        }
    }
}

// ---------------------------------------------------------------------------------------------- the card

Card::Card() : dram_(size_t(1) << 20, 0) {}

void Card::logWrite(int reg, int val) {
    if (!log || reg == 0x43 || reg == 0x44) return;
    char s[48];
    int n = reg >= 0x40 ? std::snprintf(s, sizeof s, "%d g r%02x %x\n", tick, unsigned(reg), unsigned(val))
                        : std::snprintf(s, sizeof s, "%d v%d r%02x %x\n", tick, voice_, unsigned(reg), unsigned(val));
    if (n > 0) log->append(s, size_t(n));
}

void Card::port(int p, int val) {
    if (log && p >= 0x240 && p < 0x350) {
        char s[48];
        int n = std::snprintf(s, sizeof s, "%d p%03x %x\n", tick, unsigned(p), unsigned(val));
        if (n > 0) log->append(s, size_t(n));
    }
    if (p == kBase + 8) adlib_ = val & 0xff;
    else if (p == kBase + 9 && adlib_ == 4) timers_.control(val & 0xff);
}

void Card::w8(int reg, int val) {
    val &= 0xff;
    logWrite(reg, val);
    if (reg >= 0x45 && reg <= 0x47) timers_.write(reg, val);
    received(voice_, reg, uint32_t(val) << 8 | latch_);
}

void Card::w16(int reg, int val) {
    val &= 0xffff;
    logWrite(reg, val);
    latch_ = uint32_t(val & 0xff);
    received(voice_, reg, uint32_t(val));
}

void Card::upload(uint32_t a, const uint8_t* src, size_t n) {
    for (size_t i = 0; i < n; i++) dram_[(a + i) & 0xfffff] = src[i];
}

// ---------------------------------------------------------------------------------------------- the GF1 model

namespace {
// GF1 12-bit log volume v (4-bit exponent, 8-bit mantissa) -> gain (256 + mantissa) * 2^(exponent - 24): 0x200 is
// -84 dB, 0xfff just below 1. Entry 4096 (= 1.0) only serves the interpolation above 4095.
struct GainTable {
    double g[4097];
    GainTable() {
        for (int v = 0; v <= 4096; v++) g[v] = std::ldexp(double(256 + (v & 255)), (v >> 8) - 24);
    }
};
const GainTable& gainTable() {
    static const GainTable t;
    return t;
}
}  // namespace

Gf1::Gf1(const std::vector<uint8_t>& dram, double rate) : rate_(rate) {
    auto d = std::make_shared<std::vector<float>>(dram.size() + 1, 0.0f);
    for (size_t i = 0; i < dram.size(); i++) (*d)[i] = float(int8_t(dram[i])) / 128.0f;
    if (!dram.empty()) (*d)[dram.size()] = (*d)[0];  // the address after 0xfffff wraps to 0
    dram_ = std::move(d);
    gainTable();
    reset();
}

void Gf1::reset() {
    active_ = 14;
    dac_ = false;
    for (Voice& s : v_) s = Voice{};
}

void Gf1::scale(Voice& s) const {
    const double k = frameRate() / rate_;  // card frames per output frame (0.804 at 48 kHz with 16 voices)
    s.inc = std::llround(double(s.fc >> 1) * double(int64_t(1) << (kPosFrac - 9)) * k);
    const int64_t step = (int64_t(s.rate & 63) << kVolFrac) >> (3 * (s.rate >> 6));  // per card frame
    s.vinc = std::llround(double(step) * k);
}

void Gf1::write(int voice, int reg, uint32_t d) {
    Voice& s = v_[voice & 31];
    constexpr int kSub = kPosFrac - 9;  // bits below the register's fraction
    switch (reg) {
    case 0x4c:  // reset (bit 0 clear), DAC enable (bit 1)
        if (!(d & 0x100)) reset();
        dac_ = (d & 0x200) != 0;
        break;
    case 0x0e:
        active_ = std::max(14, int((d >> 8) & 31) + 1);
        for (Voice& u : v_) scale(u);
        break;
    case 0x00: s.ctl = int((d >> 8) & 0x7f); break;
    case 0x01:
        s.fc = int(d & 0xffff);
        scale(s);
        break;
    // address high: bits 19..7 of the address; low: bits 6..0 and the 9-bit fraction (the sub-fraction is cleared)
    case 0x02: s.start = (s.start & ((int64_t(1) << (16 + kSub)) - 1)) | (int64_t(d & 0x1fff) << (16 + kSub)); break;
    case 0x04: s.end = (s.end & ((int64_t(1) << (16 + kSub)) - 1)) | (int64_t(d & 0x1fff) << (16 + kSub)); break;
    case 0x0a: s.pos = (s.pos & ((int64_t(1) << (16 + kSub)) - 1)) | (int64_t(d & 0x1fff) << (16 + kSub)); break;
    case 0x03: s.start = (s.start & ~((int64_t(1) << (16 + kSub)) - 1)) | (int64_t(d & 0xffff) << kSub); break;
    case 0x05: s.end = (s.end & ~((int64_t(1) << (16 + kSub)) - 1)) | (int64_t(d & 0xffff) << kSub); break;
    case 0x0b: s.pos = (s.pos & ~((int64_t(1) << (16 + kSub)) - 1)) | (int64_t(d & 0xffff) << kSub); break;
    case 0x06:
        s.rate = int((d >> 8) & 0xff);
        scale(s);
        break;
    case 0x07: s.lo = int64_t((d >> 8) & 0xff) << (4 + kVolFrac); break;  // ramp limits: 8 bits = volume >> 4
    case 0x08: s.hi = int64_t((d >> 8) & 0xff) << (4 + kVolFrac); break;
    case 0x09: s.vol = int64_t((d >> 4) & 0xfff) << kVolFrac; break;
    case 0x0c: s.pan = int((d >> 8) & 15); break;
    case 0x0d: s.vctl = int((d >> 8) & 0x7f); break;
    default: break;
    }
}

// One frame of a voice is: output at (pos, vol), then pos += inc (a looped voice past its end goes back by the
// loop length, a one-shot parks on its end and stops) and the volume ramp takes one step (and stops at its limit).
// stepTo does n such frames at once; the result is the same as n single steps, so a render can be cut anywhere.
void Gf1::stepTo(Voice& s, int64_t n) {
    if (n <= 0) return;
    if (!(s.ctl & 3) && s.inc > 0) {
        const int64_t len = s.end - s.start;
        int64_t p = s.pos + n * s.inc;
        if (p > s.end) {
            if ((s.ctl & 8) && len > 0) {
                p -= (p - s.end + len - 1) / len * len;
            } else {
                p = s.end;
                s.ctl |= 1;
            }
        }
        s.pos = p;
    }
    if (!(s.vctl & 3) && s.vinc > 0) {
        if (s.vctl & 0x40) {
            int64_t v = s.vol - n * s.vinc;
            if (v <= s.lo) {
                v = s.lo;
                s.vctl |= 1;
            }
            s.vol = v;
        } else {
            int64_t v = s.vol + n * s.vinc;
            if (v >= s.hi) {
                v = s.hi;
                s.vctl |= 1;
            }
            s.vol = v;
        }
    }
}

void Gf1::advance(int64_t n) {
    for (int i = 0; i < active_; i++) stepTo(v_[i], n);
}

double Gf1::sample(int64_t pos) const {
    const float* d = dram_->data();
    const uint32_t a = uint32_t(pos >> kPosFrac) & 0xfffff;
    const double f = double(pos & ((int64_t(1) << kPosFrac) - 1)) * (1.0 / double(int64_t(1) << kPosFrac));
    const double d0 = d[a], d1 = d[a + 1];
    return d0 + (d1 - d0) * f;
}

double Gf1::gain(int64_t vol) {
    const int64_t v = std::clamp<int64_t>(vol, 0, (int64_t(4096) << kVolFrac) - 1);
    const double* g = gainTable().g;
    const int i = int(v >> kVolFrac);
    const double f = double(v & ((int64_t(1) << kVolFrac) - 1)) * (1.0 / double(int64_t(1) << kVolFrac));
    return g[i] + (g[i + 1] - g[i]) * f;  // exact at the card's 12-bit steps
}

void Gf1::mix(int n, double* L, double* R, double* dcL, double* dcR) {
    if (n <= 0) return;
    if (!dac_) return advance(n);
    for (int i = 0; i < active_; i++) {
        if (mixed >> i & 1) mixVoice(v_[i], n, L, R, dcL, dcR);
        else stepTo(v_[i], n);
    }
}

// Per frame: DRAM[a] .. DRAM[a + 1] interpolated by the fraction, x gain(volume), x pan law (L = (15 - pan) / 15,
// R = pan / 15). A voice that stopped and whose volume does not ramp is mixed at its parked position.
void Gf1::mixVoice(Voice& s, int n, double* L, double* R, double* dcL, double* dcR) const {
    const double pl = double(15 - s.pan) / 15.0, pr = double(s.pan) / 15.0;
    int k = 0;
    while (k < n) {
        const bool moving = !(s.ctl & 3) && s.inc > 0;
        const bool ramping = !(s.vctl & 3) && s.vinc > 0;
        if (!moving && !ramping) {  // constant from frame k on
            const double x = sample(s.pos) * gain(s.vol);
            if (x != 0.0) {
                dcL[k] += x * pl;
                dcR[k] += x * pr;
                dcL[n] -= x * pl;
                dcR[n] -= x * pr;
            }
            return;
        }
        if (ramping) {  // a few frames (a ramp takes at most ~120 frames at 44.1 kHz): one by one
            for (; k < n;) {
                const double x = sample(s.pos) * gain(s.vol);
                L[k] += x * pl;
                R[k] += x * pr;
                k++;
                stepTo(s, 1);
                if (s.vctl & 3) break;
            }
            continue;
        }
        // moving at a constant volume: the bulk of the work
        const double g = gain(s.vol), gl = g * pl, gr = g * pr;
        const int64_t inc = s.inc, en = s.end, len = en - s.start;
        const bool loop = (s.ctl & 8) && len > 0;
        int64_t p = s.pos;
        while (k < n) {
            // frames before the position passes the end
            const int64_t free = p > en ? 1 : (en - p) / inc + 1;
            const int m = int(std::min<int64_t>(free, n - k));
            for (int j = 0; j < m; j++, k++, p += inc) {
                const double x = sample(p);
                L[k] += x * gl;
                R[k] += x * gr;
            }
            if (p > en) {
                if (loop) {
                    p -= (p - en + len - 1) / len * len;
                } else {
                    p = en;
                    s.ctl |= 1;
                    break;
                }
            }
        }
        s.pos = p;
    }
}

}  // namespace gus
