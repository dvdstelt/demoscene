// The GUS as DOPE's music player sees it (the card of dope/re/notes-music.md sections 3, 6, 9 and 10: registers, log,
// DRAM, timer 2, volume ramps with their IRQ queue; dopeplay.py's class Card) and a model of the GF1 synthesiser that
// renders the voices (section 11; dopeplay.py's class GF1 at any output rate). See gf1.h.
#include "gf1.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace gus {

namespace {

// registers the card treats as 16 bit (an 8-bit write puts the byte in the high half)
bool wide(int reg) {
    return reg == 0x01 || reg == 0x02 || reg == 0x03 || reg == 0x04 || reg == 0x05 || reg == 0x09 || reg == 0x0a ||
           reg == 0x0b;
}

constexpr double kRampDiv[4] = {1, 8, 64, 512};  // reg 6 bits 6..7: one ramp step every 1, 8, 64, 512 frames

}  // namespace

// ---------------------------------------------------------------------------------------------- the card

Card::Card() : dram_(size_t(1) << 20, 0) {
    for (Voice& v : v_) {
        v.r[0x00] = 3;
        v.r[0x0d] = 3;
        v.r[0x0c] = 7;
    }
    g_[0x0e] = 13;
}

double Card::rate() const { return kGf1Clock / std::max(14, (g_[0x0e] & 0x1f) + 1); }

double Card::rampEnd(int vn) const {
    const Voice& v = v_[vn];
    const int rc = v.r[0x0d];
    if ((rc & 3) || !(rc & 0x20)) return -1;
    const double inc = double(v.r[6] & 0x3f) / kRampDiv[(v.r[6] >> 6) & 3];
    if (!(inc > 0)) return -1;
    const double d = (rc & 0x40) ? v.vol - double(v.r[7] << 4) : double(v.r[8] << 4) - v.vol;
    return v.t + std::max(d, 0.0) / inc / rate();
}

void Card::sim(int vn, double t) {
    Voice& v = v_[vn];
    const double dt = t - v.t;
    v.t = t;
    if (dt <= 0) return;
    const int rc = v.r[0x0d];
    if (rc & 3) return;
    const double inc = double(v.r[6] & 0x3f) * dt * rate() / kRampDiv[(v.r[6] >> 6) & 3];
    const double lo = double(v.r[7] << 4), hi = double(v.r[8] << 4);
    bool hit;
    if (rc & 0x40) {
        v.vol -= inc;
        hit = v.vol <= lo;
    } else {
        v.vol += inc;
        hit = v.vol >= hi;
    }
    if (hit) {  // (looped ramps, bit 3, are not used by this player)
        v.vol = (rc & 0x40) ? lo : hi;
        v.r[0x0d] |= 1;
        if (rc & 0x20) {
            v.r[0x0d] |= 0x80;
            if (!queued(vn)) queue_.push_back(vn);
        }
    }
    v.vol = std::min(std::max(v.vol, 0.0), 4095.0);
}

bool Card::queued(int vn) const { return std::find(queue_.begin(), queue_.end(), vn) != queue_.end(); }

void Card::irqBits() {
    status_ &= ~0x60;
    for (int vn : queue_)
        if (v_[vn].r[0x0d] & 0x80) status_ |= 0x40;
}

Card::Event Card::nextEvent() const {
    Event e;
    if (t2Run_) e = {t2Next_, -1};
    for (int vn = 0; vn < 32; vn++) {
        const double te = rampEnd(vn);
        if (te >= 0 && (e.t < 0 || te < e.t)) e = {te, vn};
    }
    return e;
}

void Card::events(double t, const std::vector<int>* order) {
    for (;;) {
        bool any = false;
        double bt = 0;
        int bv = 0;
        if (t2Run_ && t2Next_ <= t) {
            any = true;
            bt = t2Next_;
            bv = -1;
        }
        for (int vn = 0; vn < 32; vn++) {
            const double te = rampEnd(vn);
            if (te >= 0 && te + 1e-9 <= t && (!any || te < bt || (te == bt && vn < bv))) {
                any = true;
                bt = te;
                bv = vn;
            }
        }
        if (!any) break;
        if (bv < 0) {
            if (g_[0x45] & 8) {
                if (status_ & 8) lost++;
                else t2Raised_ = bt;
                status_ |= 8;
            }
            t2Next_ += (256 - g_[0x47]) * 320e-6;
        } else {
            sim(bv, std::min(bt + 1e-9, t));  // (a hair past the end: float rounding)
            if (!(v_[bv].r[0x0d] & 1)) throw std::logic_error("GUS model: a ramp did not end at its end time");
        }
    }
    for (int vn = 0; vn < 32; vn++) sim(vn, t);
    now_ = t;
    if (order && !queue_.empty()) {
        std::vector<int> a = *order, b = queue_;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) throw std::runtime_error("GUS model: the ramp ends pending differ from the capture's");
        reordered += *order != queue_;
        queue_ = *order;
    }
    irqBits();
}

void Card::logPort(int p, int val) {
    if (!log) return;
    char s[48];
    const int n = std::snprintf(s, sizeof s, "%d p%03x %x\n", tick, unsigned(kBase + p), unsigned(val));
    if (n > 0) log->append(s, size_t(n));
}

void Card::logIrq(char kind, double t) {
    if (!log) return;
    char s[64];
    const int n = std::snprintf(s, sizeof s, "%d irq %c %.6f\n", tick, kind, t);
    if (n > 0) log->append(s, size_t(n));
}

void Card::outb(int p, int val) {
    val &= 0xff;
    switch (p) {
    case 0x102: voice_ = val & 31; break;
    case 0x103: reg_ = val; break;
    case 0x104: lo_ = val; break;
    case 0x105: write(reg_, (wide(reg_) || reg_ == 0x43) ? (lo_ | val << 8) : val, 1); break;
    case 0x107: dram_[addr_ & 0xfffff] = uint8_t(val); break;
    case 0x008: adlib_ = val; break;
    case 0x009:
        if (adlib_ == 4) {
            if (val & 0x80) {
                status_ &= ~0x0c;
            } else {
                if (val & 1) throw std::runtime_error("GUS model: timer 1 is not modelled");
                const bool r2 = (val & 2) != 0;
                if (r2 && !t2Run_) t2Next_ = now_ + (256 - g_[0x47]) * 320e-6;
                t2Run_ = r2;
            }
        }
        logPort(p, val);
        break;
    default: logPort(p, val); break;
    }
}

void Card::outw(int p, int val) {
    if (p != 0x104) throw std::logic_error("GUS model: 16-bit write to a port other than 3X4");
    write(reg_, val & 0xffff, 2);
}

void Card::write(int reg, int val, int size) {
    if (reg == 0x43) {
        addr_ = (addr_ & 0xff0000u) | uint32_t(val & 0xffff);
        return;
    }
    if (reg == 0x44) {
        addr_ = (addr_ & 0xffffu) | uint32_t(val & 0xff) << 16;
        return;
    }
    if (log) {
        char s[48];
        const int n = std::snprintf(s, sizeof s, "%d v%d r%02x %x\n", tick, voice_, unsigned(reg), unsigned(val));
        if (n > 0) log->append(s, size_t(n));
    }
    uint32_t d;
    if (size == 2) {
        d = uint32_t(val) & 0xffff;
        latch_ = d & 0xff;
    } else {
        d = wide(reg) ? (uint32_t(val >> 8) & 0xff) << 8 | latch_ : (uint32_t(val) & 0xff) << 8;
    }
    received(voice_, reg, d);
    if (reg < 0x20) {
        const int vn = voice_;
        Voice& v = v_[vn];
        sim(vn, now_);
        if (reg == 0x00) v.r[0] = val & 0x7f;
        else if (reg == 0x0d) v.r[0x0d] = val & 0x7f;
        else if (reg == 0x0e) g_[0x0e] = val;
        else {
            v.r[reg] = val;
            if (reg == 0x09) v.vol = double(val >> 4);
        }
        if (!((v.r[0] & 0x80) || (v.r[0x0d] & 0x80)) && queued(vn))
            queue_.erase(std::find(queue_.begin(), queue_.end(), vn));
        irqBits();
    } else {
        g_[reg & 0xff] = val;
        if (reg == 0x45) {
            if (!(val & 8)) status_ &= ~8;
            if (!(val & 4)) status_ &= ~4;
        }
    }
}

int Card::read(int reg) {
    if (reg == 0x8f) {
        if (queue_.empty()) return 0xe0;
        const int vn = queue_.front();
        queue_.erase(queue_.begin());
        Voice& v = v_[vn];
        const int r = vn | 0x20 | ((v.r[0] & 0x80) ? 0 : 0x80) | ((v.r[0x0d] & 0x80) ? 0 : 0x40);
        v.r[0] &= ~0x80;
        v.r[0x0d] &= ~0x80;
        irqBits();
        return r;
    }
    if (reg == 0x89) {
        sim(voice_, now_);
        return int(v_[voice_].vol) << 4;
    }
    throw std::logic_error("GUS model: read of a register the player does not read");
}

int Card::inb(int p) {
    if (p == 0x107) return dram_[addr_ & 0xfffff];
    if (p == 0x105) return read(reg_) & 0xff;
    if (p == 0x006) return status_ & 0xec;
    throw std::logic_error("GUS model: read of a port the player does not read");
}

int Card::inw(int p) {
    if (p != 0x104) throw std::logic_error("GUS model: 16-bit read of a port other than 3X4");
    return read(reg_) & 0xffff;
}

void Card::upload(uint32_t a, const uint8_t* src, size_t n) {
    for (size_t i = 0; i < n; i++) dram_[(a + i) & 0xfffff] = src[i];
    if (n) addr_ = uint32_t(a + n - 1);
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
    const double k = frameRate() / rate_;  // card frames per output frame (1/2 at 44.1 kHz with 28 voices)
    s.inc = std::llround(double(s.fc >> 1) * double(int64_t(1) << (kPosFrac - 9)) * k);
    const int64_t step = (int64_t(s.rate & 63) << kVolFrac) >> (3 * (s.rate >> 6));  // per card frame
    s.vinc = std::llround(double(step) * k);
}

void Gf1::write(int voice, int reg, uint32_t d) {
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
    case 0x0c: s.pan = int((d >> 8) & 15); break;
    case 0x0d: s.vctl = int((d >> 8) & 0x7f); break;
    default: break;
    }
}

// One frame of a voice is: output at (pos, vol), then the position moves by inc and the volume ramp takes one step
// (and stops at its limit). Forward loop: past the end the position goes back by the loop length; one-shot: it parks
// on the end and the voice stops. Bidirectional loop (bits 3 and 4): an unfolded coordinate u that runs forward, plain
// up to the loop end, then folded with period 2 x the loop length (bit 6 set on the way back); a voice moving
// backwards is mapped to u = start + 2 len - (pos - start). stepTo does n frames at once; the result is the same as
// n single steps, so a render can be cut anywhere.
void Gf1::stepTo(Voice& s, int64_t n) {
    if (n <= 0) return;
    if (!(s.ctl & 3) && s.inc > 0) {
        const int64_t st = s.start, en = s.end, len = en - st;
        if ((s.ctl & 0x18) == 0x18 && len > 0) {
            const int64_t per = 2 * len;
            const bool back = (s.ctl & 0x40) != 0;
            const int64_t u = (back && st <= s.pos && s.pos <= en ? st + per - (s.pos - st) : s.pos) + n * s.inc;
            bool goingBack = false;
            if (u <= en) {
                s.pos = u;
            } else {
                const int64_t m = (u - st) % per;
                goingBack = m > len;
                s.pos = goingBack ? st + per - m : st + m;
            }
            s.ctl = (s.ctl & ~0x40) | (goingBack ? 0x40 : 0);
        } else {
            int64_t p = s.pos + n * s.inc;
            if (p > en) {
                if ((s.ctl & 8) && len > 0) {
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
// R = pan / 15), in dopeplay.py's order of operations. A voice that stopped and whose volume does not ramp is mixed
// at its parked position.
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
        if (ramping) {  // a few frames (a ramp takes at most ~100 frames at 44.1 kHz): one by one
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
        const double g = gain(s.vol);
        const int64_t inc = s.inc, st = s.start, en = s.end, len = en - st;
        if ((s.ctl & 0x18) == 0x18 && len > 0) {
            const int64_t per = 2 * len;
            const bool back = (s.ctl & 0x40) != 0;
            int64_t u = back && st <= s.pos && s.pos <= en ? st + per - (s.pos - st) : s.pos;
            int64_t m = -1;
            for (; k < n; k++, u += inc) {
                int64_t p = u;
                if (u > en) {
                    if (m < 0) {
                        m = (u - st) % per;
                    } else {
                        m += inc;
                        while (m >= per) m -= per;
                    }
                    p = m > len ? st + per - m : st + m;
                }
                const double x = sample(p) * g;
                L[k] += x * pl;
                R[k] += x * pr;
            }
            bool goingBack = false;
            if (u <= en) {
                s.pos = u;
            } else {
                const int64_t mm = (u - st) % per;
                goingBack = mm > len;
                s.pos = goingBack ? st + per - mm : st + mm;
            }
            s.ctl = (s.ctl & ~0x40) | (goingBack ? 0x40 : 0);
            return;
        }
        const bool loop = (s.ctl & 8) && len > 0;
        int64_t p = s.pos;
        while (k < n) {
            // frames before the position passes the end
            const int64_t free = p > en ? 1 : (en - p) / inc + 1;
            const int m = int(std::min<int64_t>(free, n - k));
            for (int j = 0; j < m; j++, k++, p += inc) {
                const double x = sample(p) * g;
                L[k] += x * pl;
                R[k] += x * pr;
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
