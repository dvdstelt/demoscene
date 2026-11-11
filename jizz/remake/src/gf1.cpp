// The GUS as MXMPlay sees it: the fake card of the capture (register memory) and the GF1 synthesiser model that
// renders the voices. Ports the register I/O of 0x29f63..0x2a00b from the card's side; the model is mxmplay.py's
// class GF1 (jizz/re/notes-music.md section 7). The mix is computed in double precision with the same operations
// in the same order as mxmplay.py's numpy code, so it gives the same doubles (this file is compiled without
// floating-point contraction, see CMakeLists.txt).
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
    int n = std::snprintf(b, sizeof b, "%d p%03x %x\n", tick, kBase + p, unsigned(val));
    if (n > 0) log->append(b, size_t(n));
}

void Card::store(int reg, int val) {
    regs_[voice_][reg & 0x7f] = uint16_t(val);
    if (!log) return;
    char b[48];
    int n = std::snprintf(b, sizeof b, "%d v%d r%02x %x\n", tick, voice_, unsigned(reg), unsigned(val));
    if (n > 0) log->append(b, size_t(n));
}

void Card::w8(int reg, int val) { store(reg, val & 0xff); }
void Card::w16(int reg, int val) { store(reg, val & 0xffff); }

// run_jizz.py's fake GUS: the last value written to the register (the status registers with bit 0 set: "voice
// stopped", "ramp stopped"); the high byte for the registers whose 8-bit read returns the high byte.
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

// ---------------------------------------------------------------------------------------------- the GF1 model

Gf1::Gf1() {
    for (int v = 0; v < 4096; v++) gain_[v] = std::ldexp(double(256 + (v & 255)), (v >> 8) - 24);
    reset();
}

void Gf1::reset() {
    active_ = 14;
    dac_ = false;
    for (Voice& s : v_) s = Voice{};
}

void Gf1::w8(int reg, int val) {
    Card::w8(reg, val);
    hw(reg, (uint32_t(val & 0xff) << 8) | latch_);
}

void Gf1::w16(int reg, int val) {
    Card::w16(reg, val);
    latch_ = uint32_t(val & 0xff);
    hw(reg, uint32_t(val & 0xffff));
}

int Gf1::r8(int reg) {
    const Voice& s = v_[voice_];
    if (reg == 0x80) return s.ctl;
    if (reg == 0x8d) return s.vctl;
    return 0;
}

int Gf1::r16(int reg) {
    if (reg == 0x89) return int(((v_[voice_].vol >> 9) << 4) & 0xffff);  // the current (ramped) volume
    return 0;
}

void Gf1::hw(int reg, uint32_t d) {
    Voice& s = v_[voice_];
    switch (reg) {
    case 0x4c:  // reset (bit 0 clear), DAC enable (bit 1)
        if (!(d & 0x100)) reset();
        dac_ = (d & 0x200) != 0;
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
    case 0x0c: s.pan = int((d >> 8) & 15); break;
    case 0x0d: s.vctl = int((d >> 8) & 0x7f); break;
    default: break;
    }
}

// 0x2a17f polls reg 0x8d of every channel until bit 0 (ramp stopped) is set; the GF1 runs meanwhile.
void Gf1::waitRamps(int voices) {
    int64_t n = 0;
    for (int i = 0; i < voices; i++) {
        const Voice& s = v_[i];
        if (s.vctl & 3) continue;
        int64_t inc = rampInc(s);
        if (!inc) continue;
        int64_t dist = (s.vctl & 0x40) ? s.vol - s.lo : s.hi - s.vol;
        int64_t frames = dist > 0 ? (dist + inc - 1) / inc : 1;
        n = std::max(n, std::max<int64_t>(1, frames));
    }
    run(int(n));
    waited += int(n);
}

void Gf1::run(int n) {
    if (n <= 0) return;
    if (!out) {
        for (int i = 0; i < active_; i++) voiceFrames(v_[i], n, false);
        return;
    }
    if (dramChanged_) {
        sdram_.assign(dram_.begin(), dram_.end());
        sdram_.push_back(0);
        dramChanged_ = false;
    }
    L_.assign(size_t(n), 0.0);
    R_.assign(size_t(n), 0.0);
    if (pos_.size() < size_t(n)) {
        pos_.resize(size_t(n));
        vol_.resize(size_t(n));
    }
    for (int i = 0; i < active_; i++) voiceFrames(v_[i], n, true);
    const size_t at = out->size();
    out->resize(at + 2 * size_t(n), 0.0f);
    if (!dac_) return;
    float* o = out->data() + at;
    double pk = peak;
    for (int k = 0; k < n; k++) {
        pk = std::max(pk, std::max(std::fabs(L_[size_t(k)]), std::fabs(R_[size_t(k)])));
        o[2 * k] = float(L_[size_t(k)]);
        o[2 * k + 1] = float(R_[size_t(k)]);
    }
    peak = pk;
}

// Advances one voice by n frames (wave position, then volume ramp) and, with mix, adds its output to L_ / R_:
// per frame DRAM[a] .. DRAM[a + 1] interpolated by the 9-bit fraction, / 128, x gain(volume), x pan law
// (L = (15 - pan) / 15, R = pan / 15). Frame 0 uses the current position and volume. Stopped voices are mixed at
// their parked position. Loops: forward (bit 3) and bidirectional (bits 3 + 4, direction bit 6) as DOSBox-X does.
void Gf1::voiceFrames(Voice& s, int n, bool mix) {
    int64_t* pos = mix ? pos_.data() : nullptr;
    const int ctl = s.ctl;
    const int64_t p0 = s.pos, inc = s.fc >> 1, st = s.start, en = s.end, len = en - st;
    bool constPos = true;
    if (!(ctl & 3) && inc != 0) {
        constPos = false;
        const bool loop = (ctl & 8) && len > 0;
        const bool back = (ctl & 0x40) != 0;
        if ((ctl & 0x18) == 0x18 && len > 0) {
            // bidirectional: an unfolded forward coordinate u, plain up to the loop end, then folded with period
            // 2 * len (a voice moving backwards inside the loop is mapped to u = st + 2 * len - (p0 - st))
            const int64_t per = 2 * len;
            const int64_t u0 = back && st <= p0 && p0 <= en ? st + per - (p0 - st) : p0;
            if (pos) {
                int64_t u = u0, m = -1;
                for (int k = 0; k < n; k++, u += inc) {
                    if (u <= en) {
                        pos[k] = u;
                        continue;
                    }
                    if (m < 0) {
                        m = (u - st) % per;
                    } else {
                        m += inc;
                        while (m >= per) m -= per;
                    }
                    pos[k] = m > len ? st + per - m : st + m;
                }
            }
            const int64_t u = u0 + int64_t(n) * inc;
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
            if (pos)
                for (int k = 0; k < n; k++) {
                    int64_t q = p0 - int64_t(k) * inc;
                    if (q < st) q = loop ? en - (st - q) % len : st;
                    pos[k] = q;
                }
            int64_t p = p0 - int64_t(n) * inc;
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
            if (pos) {
                // frames up to the end move freely; after it they wrap (loop) or park at the end
                int64_t freeFrames = p0 > en ? 0 : (en - p0) / inc + 1;
                int kf = int(std::min<int64_t>(freeFrames, n)), k = 0;
                for (int64_t q = p0; k < kf; k++, q += inc) pos[k] = q;
                for (; k < n; k++) {
                    int64_t q = p0 + int64_t(k) * inc;
                    pos[k] = loop ? st + (q - en) % len : en;
                }
            }
            int64_t p = p0 + int64_t(n) * inc;
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

    // volume ramp (rate 0x3f: 63 steps of 4096 per frame), from the current volume to the limit
    constexpr int64_t kVolMax = (int64_t(4096) << 9) - 1;
    const int64_t v0 = s.vol;
    bool constVol = true;
    if (!(s.vctl & 3) && rampInc(s)) {
        const int64_t vinc = rampInc(s);
        const bool down = (s.vctl & 0x40) != 0;
        if (mix) {
            constVol = false;
            vol_[0] = std::clamp<int64_t>(v0, 0, kVolMax);
            for (int k = 1; k < n; k++) {
                int64_t v = down ? std::max(v0 - int64_t(k) * vinc, s.lo) : std::min(v0 + int64_t(k) * vinc, s.hi);
                vol_[size_t(k)] = std::clamp<int64_t>(v, 0, kVolMax);
            }
        }
        if (down) {
            int64_t v = v0 - int64_t(n) * vinc;
            if (v <= s.lo) {
                v = s.lo;
                s.vctl |= 1;
            }
            s.vol = v;
        } else {
            int64_t v = v0 + int64_t(n) * vinc;
            if (v >= s.hi) {
                v = s.hi;
                s.vctl |= 1;
            }
            s.vol = v;
        }
    }
    if (!mix) return;

    const double pl = double(15 - s.pan) / 15.0, pr = double(s.pan) / 15.0;
    const int8_t* dram = sdram_.data();
    auto sample = [dram](int64_t q) {
        uint32_t a = uint32_t(q >> 9) & 0xfffff;
        double f = double(q & 511) / 512.0;
        double d0 = double(dram[a]) / 128.0, d1 = double(dram[a + 1]) / 128.0;
        return d0 + (d1 - d0) * f;
    };
    double* L = L_.data();
    double* R = R_.data();
    const double g0 = gain_[std::clamp<int64_t>(v0, 0, kVolMax) >> 9];
    if (constPos && constVol) {
        const double g = sample(p0) * g0;
        if (g == 0.0) return;  // adds +0.0: no change (the sums are never -0.0)
        const double gl = g * pl, gr = g * pr;
        for (int k = 0; k < n; k++) {
            L[k] += gl;
            R[k] += gr;
        }
        return;
    }
    for (int k = 0; k < n; k++) {
        const double g = sample(constPos ? p0 : pos[k]) * (constVol ? g0 : gain_[vol_[size_t(k)] >> 9]);
        L[k] += g * pl;
        R[k] += g * pr;
    }
}

}  // namespace gus
