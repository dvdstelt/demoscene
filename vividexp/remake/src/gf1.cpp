// The GUS as MIDAS's sound device sees it (its port accesses in 0x41b1a0..0x41cd10 from the card's side), the card's
// model that answers its reads (vividexp/re/models/music/render.py's GF1), and the GF1 synthesiser at any output
// rate, mono. See gf1.h; vividexp/re/notes-music.md sections 5 and 8.
#include "gf1.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gus {

// ---------------------------------------------------------------------------------------------- the fake card

Card::Card() : dram_(size_t(1) << 20, 0) {}

void Card::port(int p, int val) {
    if (!log) return;
    char b[48];
    const int n = std::snprintf(b, sizeof b, "%d p%03x %x\n", tick, unsigned(kBase + p), unsigned(val));
    if (n > 0) log->append(b, size_t(n));
}

void Card::logWrite(int reg, int val) {
    regs_[voice_][reg & 0x7f] = uint16_t(val);
    if (!log || reg == 0x43 || reg == 0x44) return;  // the capture does not log the DRAM address
    char b[48];
    const int n = reg >= 0x40  // the global registers
                      ? std::snprintf(b, sizeof b, "%d g r%02x %x\n", tick, unsigned(reg), unsigned(val))
                      : std::snprintf(b, sizeof b, "%d v%d r%02x %x\n", tick, voice_, unsigned(reg), unsigned(val));
    if (n > 0) log->append(b, size_t(n));
}

void Card::w8(int reg, int val) {
    val &= 0xff;
    logWrite(reg, val);
    received(voice_, reg, uint32_t(val) << 8 | latch_);
}

void Card::w16(int reg, int val) {
    val &= 0xffff;
    logWrite(reg, val);
    latch_ = uint32_t(val & 0xff);
    received(voice_, reg, uint32_t(val));
}

// run_vivid.py's fake GUS: the last value written to the register (the status registers with bit 0 set); the high
// byte for the registers whose 8-bit read returns the high byte.
int Card::r8(int reg) {
    int v = reg >= 0x80 ? regs_[voice_][reg - 0x80] : 0;
    if (reg == 0x80 || reg == 0x8d) v |= 1;
    if ((reg >= 0x81 && reg <= 0x85) || (reg >= 0x89 && reg <= 0x8b)) return (v >> 8) & 0xff;
    return v & 0xff;
}

int Card::r16(int reg) {
    int v = reg >= 0x80 ? regs_[voice_][reg - 0x80] : 0;
    if (reg == 0x80 || reg == 0x8d) v |= 1;
    return v & 0xffff;
}

// ---------------------------------------------------------------------------------------------- the card's model

Hardware::Hardware() { reset(); }

void Hardware::reset() {
    active_ = 14;
    for (Voice& s : v_) s = Voice{};
}

void Hardware::received(int voice, int reg, uint32_t d) {
    if (writes)
        writes->push_back({clock, int32_t(waited), uint8_t(active_), uint8_t(voice), uint8_t(reg), uint16_t(d)});
    Voice& s = v_[voice & 31];
    switch (reg) {
    case 0x4c:  // reset (bit 0 clear)
        if (!(d & 0x100)) reset();
        break;
    case 0x0e: active_ = std::max(14, int((d >> 8) & 31) + 1); break;
    case 0x00: s.ctl = int((d >> 8) & 0x7f); break;
    case 0x01: s.fc = int(d); break;
    case 0x02: s.start = (s.start & 0xffff) | (int64_t(d & 0x1fff) << 16); break;
    case 0x04: s.end = (s.end & 0xffff) | (int64_t(d & 0x1fff) << 16); break;
    case 0x0a: s.pos = (s.pos & 0xffff) | (int64_t(d & 0x1fff) << 16); break;
    case 0x03: s.start = (s.start & ~int64_t(0xffff)) | d; break;
    case 0x05: s.end = (s.end & ~int64_t(0xffff)) | d; break;
    case 0x0b: s.pos = (s.pos & ~int64_t(0xffff)) | d; break;
    case 0x06: s.rate = int(d >> 8); break;
    case 0x07: s.lo = int64_t((d >> 8) << 4) << 9; break;
    case 0x08: s.hi = int64_t((d >> 8) << 4) << 9; break;
    case 0x09: s.vol = int64_t(d >> 4) << 9; break;
    case 0x0d: s.vctl = int((d >> 8) & 0x7f); break;
    default: break;
    }
}

int Hardware::r8(int reg) {
    const Voice& s = v_[voice_];
    if (reg == 0x80) return s.ctl;
    if (reg == 0x8d) return s.vctl;
    return 0;
}

int Hardware::r16(int reg) {
    if (reg == 0x89) return int(((v_[voice_].vol >> 9) << 4) & 0xffff);  // the current (ramped) volume
    return 0;
}

void Hardware::pass() {
    run(1);
    waited++;
    passes++;
}

void Hardware::advanceTo(double t) {
    if (!timeSet_) {
        time_ = t;
        timeSet_ = true;
        return;
    }
    const int64_t n = int64_t((t - time_) * frameRate());
    if (n > 0) run(n);
}

void Hardware::run(int64_t n) {
    if (n <= 0) return;
    frames += n;
    for (int i = 0; i < active_; i++) voiceFrames(v_[i], n);
    time_ += double(n) / frameRate();
}

// n frames at once, as render.py's GF1.voice_frames: wave position (one-shot, forward loop, bidirectional loop through
// an unfolded coordinate, backwards), then the volume ramp.
void Hardware::voiceFrames(Voice& s, int64_t n) {
    const int ctl = s.ctl;
    const int64_t p0 = s.pos, inc = s.fc >> 1, st = s.start, en = s.end, len = en - st;
    if (!(ctl & 3) && inc != 0) {
        const bool loop = (ctl & 8) && len > 0;
        const bool back = (ctl & 0x40) != 0;
        if ((ctl & 0x18) == 0x18 && len > 0) {
            const int64_t per = 2 * len;
            const int64_t u0 = back && st <= p0 && p0 <= en ? st + per - (p0 - st) : p0;
            const int64_t u = u0 + n * inc;
            bool goingBack = false;
            if (u <= en) {
                s.pos = u;
            } else {
                const int64_t m = (u - st) % per;
                goingBack = m > len;
                s.pos = goingBack ? st + per - m : st + m;
            }
            s.ctl = (ctl & ~0x40) | (goingBack ? 0x40 : 0);
        } else if (back) {
            int64_t p = p0 - n * inc;
            if (p < st) {
                if (loop) {
                    p = en - (st - p) % len;
                } else {
                    p = st;
                    s.ctl = ctl | 1;
                }
            }
            s.pos = p;
        } else {
            int64_t p = p0 + n * inc;
            if (p > en) {
                if (loop) {
                    p = st + (p - en) % len;
                } else {
                    p = en;
                    s.ctl = ctl | 1;
                }
            }
            s.pos = p;
        }
    }
    const int64_t vinc = rampInc(s);
    if (!(s.vctl & 3) && vinc) {
        if (s.vctl & 0x40) {
            int64_t v = s.vol - n * vinc;
            if (v <= s.lo) {
                v = s.lo;
                s.vctl |= 1;
            }
            s.vol = v;
        } else {
            int64_t v = s.vol + n * vinc;
            if (v >= s.hi) {
                v = s.hi;
                s.vctl |= 1;
            }
            s.vol = v;
        }
    }
}

// ---------------------------------------------------------------------------------------------- the synthesiser

namespace {
// GF1 12-bit log volume v (4-bit exponent, 8-bit mantissa) -> gain (256 + mantissa) * 2^(exponent - 24): 0xfff is
// just below 1. Entry 4096 (= 1.0) only serves the interpolation above 4095.
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

std::shared_ptr<const std::vector<float>> Synth::dramSamples(const std::vector<uint8_t>& dram) {
    auto d8 = std::make_shared<std::vector<float>>(size_t(1) << 20, 0.0f);
    for (size_t i = 0; i < dram.size() && i < d8->size(); i++) (*d8)[i] = float(int8_t(dram[i])) / 128.0f;
    return d8;
}

Synth::Synth(double rate) : rate_(rate) {
    d8_ = std::make_shared<std::vector<float>>(size_t(1) << 20, 0.0f);
    gainTable();
    reset();
}

void Synth::reset() {
    active_ = 14;
    dac_ = false;
    for (Voice& s : v_) s = Voice{};
}

void Synth::scale(Voice& s) const {
    const double k = kGf1Clock / active_ / rate_;  // card frames per output frame (0.80 at 48 kHz with 16 voices)
    s.inc = std::llround(double(s.fc >> 1) * double(int64_t(1) << (kPosFrac - 9)) * k);
    const int64_t step = (int64_t(s.rate & 63) << kVolFrac) >> (3 * (s.rate >> 6));  // per card frame
    s.vinc = std::llround(double(step) * k);
}

void Synth::write(int voice, int reg, uint32_t d) {
    Voice& s = v_[voice & 31];
    constexpr int kSub = kPosFrac - 9;  // bits below the register's fraction
    constexpr int64_t kLow = (int64_t(1) << (16 + kSub)) - 1;
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
    case 0x02: s.start = (s.start & kLow) | (int64_t(d & 0x1fff) << (16 + kSub)); break;
    case 0x04: s.end = (s.end & kLow) | (int64_t(d & 0x1fff) << (16 + kSub)); break;
    case 0x0a: s.pos = (s.pos & kLow) | (int64_t(d & 0x1fff) << (16 + kSub)); break;
    case 0x03: s.start = (s.start & ~kLow) | (int64_t(d & 0xffff) << kSub); break;
    case 0x05: s.end = (s.end & ~kLow) | (int64_t(d & 0xffff) << kSub); break;
    case 0x0b: s.pos = (s.pos & ~kLow) | (int64_t(d & 0xffff) << kSub); break;
    case 0x06:
        s.rate = int((d >> 8) & 0xff);
        scale(s);
        break;
    case 0x07: s.lo = int64_t((d >> 8) & 0xff) << (4 + kVolFrac); break;  // ramp limits: 8 bits = volume >> 4
    case 0x08: s.hi = int64_t((d >> 8) & 0xff) << (4 + kVolFrac); break;
    case 0x09: s.vol = int64_t((d >> 4) & 0xfff) << kVolFrac; break;
    case 0x0d: s.vctl = int((d >> 8) & 0x7f); break;
    default: break;  // 0x0c pan: always 8 (mono)
    }
}

// One frame of a voice is: output at (pos, vol), then the position moves by inc and the volume ramp takes one step
// (and stops at its limit). Forward: past the end a forward loop goes back by whole loop lengths, a one-shot parks
// on its end and stops. Bidirectional: the position runs freely up to the end, then is reflected at both loop points
// (computed through the unfolded coordinate u: period 2 x loop length; a voice moving backwards inside the loop has
// u = start + 2 len - (pos - start); one moving backwards outside it moves forwards). Backwards (only a restart that
// keeps a bidirectional voice's direction bit): below the loop start a forward loop goes up by whole loop lengths, a
// one-shot parks on the start. stepTo does n frames at once; the result is the same as n single frames, so a render
// can be cut anywhere.
void Synth::stepTo(Voice& s, int64_t n) {
    if (n <= 0) return;
    if (!(s.ctl & 3) && s.inc > 0) {
        const int ctl = s.ctl;
        const int64_t st = s.start, en = s.end, len = en - st, inc = s.inc;
        if ((ctl & 0x18) == 0x18 && len > 0) {
            const int64_t per = 2 * len, p0 = s.pos;
            const int64_t u0 = (ctl & 0x40) && st <= p0 && p0 <= en ? st + per - (p0 - st) : p0;
            const int64_t u = u0 + n * inc;
            bool back = false;
            if (u <= en) {
                s.pos = u;
            } else {
                const int64_t m = (u - st) % per;
                back = m > len;
                s.pos = back ? st + per - m : st + m;
            }
            s.ctl = (ctl & ~0x40) | (back ? 0x40 : 0);
        } else if (ctl & 0x40) {
            int64_t p = s.pos - n * inc;
            if (p < st) {
                if ((ctl & 8) && len > 0) {
                    p += (st - p + len - 1) / len * len;
                } else {
                    p = st;
                    s.ctl |= 1;
                }
            }
            s.pos = p;
        } else {
            int64_t p = s.pos + n * inc;
            if (p > en) {
                if ((ctl & 8) && len > 0) {
                    p -= (p - en + len - 1) / len * len;
                } else {
                    p = en;
                    s.ctl |= 1;
                }
            }
            s.pos = p;
        }
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

void Synth::advance(int64_t n) {
    for (int i = 0; i < active_; i++) stepTo(v_[i], n);
}

double Synth::sample(int64_t pos) const {
    const uint32_t a = uint32_t(pos >> kPosFrac) & 0xfffff, b = (a + 1) & 0xfffff;
    const double f = double(uint32_t(pos)) * (1.0 / 4294967296.0);
    const float* d = d8_->data();
    const double d0 = d[a], d1 = d[b];
    return d0 + (d1 - d0) * f;
}

double Synth::gain(int64_t vol) {
    const int64_t v = std::clamp<int64_t>(vol, 0, (int64_t(4096) << kVolFrac) - 1);
    const double* g = gainTable().g;
    const int i = int(v >> kVolFrac);
    const double f = double(v & ((int64_t(1) << kVolFrac) - 1)) * (1.0 / double(int64_t(1) << kVolFrac));
    return g[i] + (g[i + 1] - g[i]) * f;
}

void Synth::mix(int n, double* out, double* dc) {
    if (n <= 0) return;
    if (!dac_) return advance(n);
    for (int i = 0; i < active_; i++) mixVoice(v_[i], n, out, dc);
}

// Per frame: the sample at the position x gain(volume).
void Synth::mixVoice(Voice& s, int n, double* out, double* dc) const {
    int k = 0;
    while (k < n) {
        const bool moving = !(s.ctl & 3) && s.inc > 0;
        const bool ramping = !(s.vctl & 3) && s.vinc > 0;
        if (!moving && !ramping) {  // constant from frame k on
            const double x = sample(s.pos) * gain(s.vol);
            if (x != 0.0) {
                dc[k] += x;
                dc[n] -= x;
            }
            return;
        }
        if (ramping) {  // a ramp lasts at most ~70 frames at 48 kHz: frame by frame
            while (k < n) {
                out[k] += sample(s.pos) * gain(s.vol);
                k++;
                stepTo(s, 1);
                if (s.vctl & 3) break;
            }
            continue;
        }
        // moving at a constant volume: the bulk of the work
        const double g = gain(s.vol);
        const int64_t inc = s.inc, st = s.start, en = s.end, len = en - st;
        if ((s.ctl & 0x18) == 0x18 && len > 0) {  // bidirectional: through the unfolded coordinate
            const int64_t per = 2 * len, p0 = s.pos;
            int64_t u = (s.ctl & 0x40) && st <= p0 && p0 <= en ? st + per - (p0 - st) : p0, m = -1;
            const int cnt = n - k;
            for (int j = 0; j < cnt; j++, k++, u += inc) {
                int64_t q = u;
                if (u > en) {
                    if (m < 0) m = (u - st) % per;
                    else if ((m += inc) >= per) m %= per;
                    q = m > len ? st + per - m : st + m;
                }
                out[k] += sample(q) * g;
            }
            stepTo(s, cnt);
            continue;
        }
        if (s.ctl & 0x40) {  // backwards (not reached by this device's writes): frame by frame
            while (k < n && !(s.ctl & 3)) {
                out[k] += sample(s.pos) * g;
                k++;
                stepTo(s, 1);
            }
            continue;
        }
        const bool loop = (s.ctl & 8) && len > 0;
        int64_t p = s.pos;
        while (k < n) {
            // frames before the position passes the end
            const int64_t free = p > en ? 1 : (en - p) / inc + 1;
            const int m = int(std::min<int64_t>(free, n - k));
            for (int j = 0; j < m; j++, k++, p += inc) out[k] += sample(p) * g;
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
