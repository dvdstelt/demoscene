// Paper's soundtrack: C++ port of paper/re/player_ref.py (spec: paper/re/notes-music.md).
//
// The intro's player (code32 0x1c24 init, 0x1e45 IRQ0 tempo accumulator, 0x1ed6 player and its helpers) is
// mirrored routine by routine and drives a model of the GF1 exactly like `player_ref.py --wav` does: the register
// writes of one IRQ are applied at that instant, the volume read (reg 0x89) is answered by the model (real
// hardware behaviour: ramps start from the current, ramped volume), then the GF1 renders the frames until the
// next IRQ (551.2352 frames per tick at 38,587.5 Hz, fractional frames carried by the same accumulator).
// The mix is computed in double precision in the same order as player_ref.py, so it reproduces its float64 mix
// bit for bit; only the final scaling to float differs at the float-rounding level. No DC blocker (finished
// one-shots park on their last byte, mean level about -0.008 / -0.005), as in player_ref.py.
//
// Output rate: the GF1's own 38,587.5 Hz (no resampling here; SDL converts to the device rate). The SDL stream is
// opened at 38,588 Hz (int frequency, 13 ppm fast); positions are always computed from the sample index with
// rate_ = 38587.5, so sample s <-> tick s * kTickHz / 38587.5 exactly, sample 0 = start of tick 0.
#include "music.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

#include "exe.h"

namespace {

// ---- static data (code32 offsets, notes-music.md section 3) ----
constexpr uint32_t kPanTab = 0x7a8b;       // 32 bytes: pan of voice v = byte & 15 (voices 16.. read the tune header)
constexpr uint32_t kTune = 0x7a9b;         // u8 channels, u16 rows, u8 instrument slots, then the pattern stream
constexpr uint32_t kVolTab = 0x1e49c;      // 65 bytes: channel volume 0..64 -> GF1 volume high byte
constexpr uint32_t kInitSpeed = 0x1e4e5;   // u8 ticks per row (6)
constexpr uint32_t kInitCount = 0x1e4e6;   // u8 tick countdown (1)
constexpr uint32_t kPeriodSeed = 0x1e4eb;  // 16 u16: Amiga periods * 8, then zeros
constexpr uint32_t kNullInsC2spd = 0x10;   // c2spd as read through instrument pointer 0 (never happens in the tune)
static_assert(kTune + 3 >= kPanTab && kTune + 3 < kPanTab + 32, "instrument count byte doubles as a pan entry");

constexpr int32_t kPitDiv = 0x4295;        // IRQ0 divisor (70.0018 Hz)
constexpr int32_t kTempoK = 0x2d8426;      // player tick = kTempoK / bpm PIT clocks
constexpr int64_t kFreqK = 0x5adc;         // FC = (c2spd * 23260 / period) >> 11
constexpr double kGf1Clock = 617400.0;     // GF1 frame rate = 617400 / active voices
constexpr int kGusBase = 0x240;            // port as logged by run_paper.py (ULTRASND=240,...)

// Master gain. The raw mix is in units of "one voice at full scale, full volume, hard-panned"; Paper's mix
// peaks at 0.96221105257670059 of that (right channel, tick 6992, as computed by player_ref.py and by this file).
// player_ref.py --wav (paper/re/full.wav) scales its peak to 0.9 of full scale, so the same gain is used here:
// kGain = 0.9 / 0.96221105257670059 = 0.93534573063767 (-0.58 dB); pcm_ peaks at 0.900.
constexpr double kMixPeak = 0.96221105257670059;
constexpr double kGain = 0.9 / kMixPeak;

struct Event {
    uint8_t note, ins, vol, eff, par;  // vol 0xff = none
};

struct Instrument {  // record at 0x1e60b + 0x14 * (i - 1)
    uint32_t len = 0, start = 0, loop = 0, end = 0;  // DRAM addresses; loop 0 = one-shot
    uint16_t c2spd = 0;
};

// Everything the intro derives from the exe at start-up (0x1ca4..0x1e1d).
struct Tune {
    int nch = 0, nrows = 0, nins = 0;
    uint16_t period[128] = {};
    uint8_t voltab[65] = {};
    std::vector<Event> events;    // row-major: events[row * nch + ch]
    std::vector<Instrument> ins;  // 1..31 (index 0 unused)
    std::vector<uint8_t> dram;    // GUS DRAM image from address 0 (signed samples)
    const Event& ev(int row, int ch) const { return events[size_t(row) * size_t(nch) + size_t(ch)]; }
};

Tune parseTune(const PaperExe& x) {
    Tune t;
    t.nch = x.u8(kTune);
    t.nrows = x.u16(kTune + 1);
    t.nins = x.u8(kTune + 3);
    if (t.nch < 1 || t.nch > 32 || t.nrows < 1) throw std::runtime_error("bad tune header");
    // period table (0x1ca4): 16 seed words, each further entry = the one 16 entries earlier >> 1
    for (int i = 0; i < 16; i++) t.period[i] = x.u16(kPeriodSeed + 2 * uint32_t(i));
    for (int i = 16; i < 128; i++) t.period[i] = uint16_t(t.period[i - 16] >> 1);
    for (int i = 0; i < 65; i++) t.voltab[i] = x.u8(kVolTab + uint32_t(i));

    // pattern unpack (0x1cbe): channel-major stream, one flag byte per channel and row
    t.events.resize(size_t(t.nrows) * size_t(t.nch));
    uint32_t p = kTune + 4;
    for (int ch = 0; ch < t.nch; ch++) {
        for (int r = 0; r < t.nrows; r++) {
            Event e{0, 0, 0, 0, 0};
            uint8_t f = x.u8(p++);
            int vol = f & 0x3f;
            if (vol == 0x3f) vol = 0x40;
            if (vol == 2) vol = 0xff;  // 2 = "no volume" (0x3f = 64)
            e.vol = uint8_t(vol);
            if (f & 0x40) {
                uint8_t b = x.u8(p), b2 = x.u8(p + 1);
                p += 2;
                e.note = b & 0x7f;
                e.ins = uint8_t(((b >> 7) << 4) + (b2 & 0x0f));
                e.eff = b2 >> 4;
            }
            if (f & 0x80) e.par = x.u8(p++);
            t.events[size_t(r) * size_t(t.nch) + size_t(ch)] = e;
        }
    }

    // instruments (0x1d5b): copy to a buffer that starts with 8 bytes 0x80, 2 pad bytes after each sample
    t.ins.assign(size_t(std::max(t.nins, 31)) + 1, Instrument{});
    std::vector<uint8_t> buf(8, 0x80);
    uint32_t ebp = 8;
    auto append = [&](uint32_t a, uint32_t n) {
        const uint8_t* s = x.at(a, n);
        buf.insert(buf.end(), s, s + n);
    };
    for (int i = 1; i <= t.nins; i++) {
        int L = x.s16(p);
        uint32_t P = p + 2;
        if (L == 0) {  // empty slot
            p = P;
            continue;
        }
        Instrument& r = t.ins[size_t(i)];
        if (L > 0) {  // one-shot: u16 c2spd, L bytes
            uint32_t len = uint32_t(L);
            r.len = len;
            r.start = ebp;
            r.loop = 0;
            r.end = ebp + len;
            r.c2spd = x.u16(P);
            append(P + 2, len);
            append(P + len + 1, 2);  // pad: last sample byte + the next header's first byte
            p = P + len + 2;
        } else {  // looped: u16 loop offset, u16 length, u16 c2spd, data
            uint32_t loopofs = x.u16(P), len = x.u16(P + 2);
            r.len = len;
            r.start = ebp;
            r.loop = ebp + loopofs;
            r.end = ebp + len;
            r.c2spd = x.u16(P + 4);
            append(P + 6, len);
            append(P + 6 + loopofs, 2);  // pad: the two bytes at the loop start
            p = P + uint32_t(-L) + 6;    // stream advance uses |L| (= length + 2)
        }
        ebp += r.len + 2;
    }
    for (uint8_t& b : buf) b ^= 0x80;  // the upload converts unsigned -> signed
    t.dram = std::move(buf);
    return t;
}

// ---- GF1 model (player_ref.py class GF1; notes-music.md section 7) ----
class Gf1 {
public:
    explicit Gf1(const std::vector<uint8_t>& image) : dram_(size_t(1) << 20, 0) {
        if (image.size() > dram_.size()) throw std::runtime_error("DRAM image too large");
        std::memcpy(dram_.data(), image.data(), image.size());
        dram_.resize(dram_.size() + 2, 0);  // a + 1 of the last address
        for (int v = 0; v < 4096; v++) gain_[v] = std::ldexp(double(256 + (v & 255)), (v >> 8) - 24);
        reset();
    }

    std::string* log = nullptr;  // register-write log (.gus text), or null
    int tick = 0;                // tick number for the log

    double frameRate() const { return kGf1Clock / active_; }

    void port(int port, int val) {  // writes to ports other than base+0x102..0x107
        if (log) line("%d p%03x %x\n", tick, port, val);
    }
    void select(int v) { voice_ = v & 31; }  // base+0x102, not logged
    void w8(int reg, int val) {               // reg -> base+0x103, byte -> base+0x105
        val &= 0xff;
        logWrite(reg, val);
        hw(reg, uint32_t(val << 8) | latch_);
    }
    void w16(int reg, int val) {  // reg -> base+0x103, word -> base+0x104
        val &= 0xffff;
        logWrite(reg, val);
        latch_ = uint32_t(val & 0xff);
        hw(reg, uint32_t(val));
    }
    int r16(int reg) const {  // only the current volume is ever read
        if (reg == 0x89) return int(((v_[voice_].vol >> 9) << 4) & 0xffff);
        return 0;
    }

    // Run n frames; if L/R are given (n doubles each), mix the active voices into them.
    void advance(int n, double* L, double* R) {
        for (int i = 0; i < active_; i++) {
            Voice& s = v_[i];
            if (L && dac_ && n > 0) mix(s, n, L, R);
            step(s, n);
        }
    }

private:
    struct Voice {
        int ctl = 3;                          // reg 0: bit0 stopped, bit1 stop, bit3 loop
        int fc = 0;                           // reg 1
        int64_t start = 0, end = 0, pos = 0;  // address * 512 + fraction
        int vctl = 3, rate = 0;               // reg 0xd, reg 6
        int64_t lo = 0, hi = 0, vol = 0;      // 12-bit volume * 512
        int pan = 7;
    };

    void reset() {
        active_ = 14;
        dac_ = false;
        for (Voice& s : v_) s = Voice{};
    }

    template <class... A> void line(const char* fmt, A... a) {
        char b[64];
        int n = std::snprintf(b, sizeof b, fmt, a...);
        if (n > 0) log->append(b, size_t(std::min<int>(n, int(sizeof b) - 1)));
    }
    void logWrite(int reg, int val) {
        if (log && reg != 0x43 && reg != 0x44) line("%d v%d r%02x %x\n", tick, voice_, reg, val);
    }

    void hw(int reg, uint32_t d) {
        Voice& s = v_[voice_];
        switch (reg) {
        case 0x4c:
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
        case 0x07: s.lo = int64_t(((d >> 8) << 4)) << 9; break;
        case 0x08: s.hi = int64_t(((d >> 8) << 4)) << 9; break;
        case 0x09: s.vol = int64_t(d >> 4) << 9; break;
        case 0x0c: s.pan = int((d >> 8) & 15); break;
        case 0x0d: s.vctl = int((d >> 8) & 0x7f); break;
        default: break;
        }
    }

    static int64_t rampInc(const Voice& s) { return (int64_t(s.rate & 63) * 512) >> (3 * (s.rate >> 6)); }

    // Advance the wave position and the volume ramp by n frames (player_ref.py GF1.advance).
    static void step(Voice& s, int n) {
        if (!(s.ctl & 3) && n) {
            int64_t p = s.pos + int64_t(n) * (s.fc >> 1);
            if (p > s.end) {
                int64_t len = s.end - s.start;
                if ((s.ctl & 8) && len > 0) {
                    p = s.start + (p - s.end) % len;
                } else {
                    p = s.end;
                    s.ctl |= 1;
                }
            }
            s.pos = p;
        }
        if (!(s.vctl & 3) && n) {
            int64_t inc = rampInc(s);
            if (s.vctl & 0x40) {
                s.vol -= int64_t(n) * inc;
                if (s.vol <= s.lo) {
                    s.vol = s.lo;
                    s.vctl |= 1;
                }
            } else {
                s.vol += int64_t(n) * inc;
                if (s.vol >= s.hi) {
                    s.vol = s.hi;
                    s.vctl |= 1;
                }
            }
        }
    }

    // Sample at position p (20.9 fixed point): signed 8-bit, linear interpolation, 1.0 = full scale.
    double sample(int64_t p) const {
        uint32_t a = uint32_t(p >> 9) & 0xfffff;
        double f = double(p & 511) * (1.0 / 512.0);
        double d0 = double(dram_[a]) * (1.0 / 128.0), d1 = double(dram_[a + 1]) * (1.0 / 128.0);
        return d0 + (d1 - d0) * f;
    }
    // Volume of frame k of this block (frame 0 = the current volume, then the ramp, clamped).
    static int64_t rampVol(const Voice& s, int64_t inc, int k) {
        if (k == 0) return s.vol;
        if (s.vctl & 0x40) return std::max(s.vol - int64_t(k) * inc, s.lo);
        return std::min(s.vol + int64_t(k) * inc, s.hi);
    }

    // Mix n frames of one voice into L/R (player_ref.py GF1._render): positions pos + k * (fc >> 1), wrapped to
    // the loop or parked at the end; every voice is mixed, stopped ones at their parked position. Same operations
    // in the same order as the numpy code, so the double sums are identical.
    template <bool Ramp> void mixRunning(const Voice& s, int n, double* L, double* R, double pl, double pr) const {
        const int64_t inc = s.fc >> 1, end = s.end, start = s.start, len = end - start;
        const bool loop = (s.ctl & 8) && len > 0;
        const int64_t vinc = rampInc(s);
        const double g0 = gain_[(s.vol >> 9) & 4095];
        int64_t p = s.pos, r = -1;
        for (int k = 0; k < n; k++, p += inc) {
            int64_t q = p;
            if (p > end) {
                if (loop) {
                    if (r < 0) {
                        r = (p - end) % len;  // == (p - end) % len for this and (incrementally) all later frames
                    } else {
                        r += inc;
                        while (r >= len) r -= len;
                    }
                    q = start + r;
                } else {
                    q = end;
                }
            }
            double g = sample(q) * (Ramp ? gain_[(rampVol(s, vinc, k) >> 9) & 4095] : g0);
            L[k] += g * pl;
            R[k] += g * pr;
        }
    }
    void mix(const Voice& s, int n, double* L, double* R) const {
        const double pl = (15 - s.pan) / 15.0, pr = s.pan / 15.0;
        const bool ramp = !(s.vctl & 3);
        if (s.ctl & 3) {  // stopped: constant DRAM[pos] (interpolated) x volume
            const double smp = sample(s.pos);
            if (smp == 0.0) return;  // adds exactly zero (all voices before their first note, voices 10..15)
            if (!ramp) {
                const double g = smp * gain_[(s.vol >> 9) & 4095], gl = g * pl, gr = g * pr;
                for (int k = 0; k < n; k++) {
                    L[k] += gl;
                    R[k] += gr;
                }
            } else {
                const int64_t vinc = rampInc(s);
                for (int k = 0; k < n; k++) {
                    double g = smp * gain_[(rampVol(s, vinc, k) >> 9) & 4095];
                    L[k] += g * pl;
                    R[k] += g * pr;
                }
            }
            return;
        }
        if (ramp)
            mixRunning<true>(s, n, L, R, pl, pr);
        else
            mixRunning<false>(s, n, L, R, pl, pr);
    }

    std::vector<int8_t> dram_;
    double gain_[4096];  // GF1 12-bit log volume: (256 + mantissa) * 2^(exponent - 24)
    Voice v_[32];
    int voice_ = 0;
    int active_ = 14;
    bool dac_ = false;
    uint32_t latch_ = 0;  // low byte of the last 16-bit write (an 8-bit write fills the data low byte with it)
};

// ---- the player (0x1c24 init, 0x1e45 IRQ, 0x1ed6 player; player_ref.py class Player) ----
class Player {
public:
    Player(const Tune& t, Gf1& g, const PaperExe& x) : t_(t), g_(g) {
        speed = x.u8(kInitSpeed);
        count = x.u8(kInitCount);
        for (int i = 0; i < 32; i++) pan_[i] = x.u8(kPanTab + uint32_t(i));
        nullC2spd_ = x.u16(kNullInsC2spd);
        ch_.assign(size_t(t.nch), Channel{});
    }

    int speed = 0, count = 0;  // [0x1e4e5], [0x1e4e6] (u8)
    int row = 0;               // [0x1ea8b] (0 in the image, -1 after init)
    bool on = false;           // [0x1ea93]

    // 0x22f8: GF1 init (also the sound-off path 0x1c8e)
    void gf1Init() {
        g_.port(kGusBase, 9);  // mix control: line in off, output on, latches on
        g_.w8(0x4c, 0);        // reset, then run
        g_.w8(0x4c, 1);
        g_.w8(0x41, 0);
        g_.w8(0x45, 0);
        g_.w8(0x49, 0);
        g_.w8(0x0e, 0xcf);  // 16 active voices -> 38587.5 Hz
        for (int cl = 32; cl >= 1; cl--) {
            g_.select(cl - 1);
            g_.w8(0x00, 3);
            g_.w8(0x0d, 3);
            g_.w8(0x06, 0x3f);
            g_.w8(0x09, 0x3f);
            g_.w8(0x01, 0);
            g_.w16(0x09, 0);
            g_.w8(0x0c, pan_[cl - 1] & 0x0f);
        }
        g_.w8(0x4c, 3);  // run, DAC on
    }

    // 0x1c24 with a GUS present
    void soundInit() {
        gf1Init();
        row = -1;
        setTempo(0x7d);
        // the upload loop counts the instrument count byte [0x7a9e] down to 0 in place; it is also the pan of
        // voice 19, so the re-init at exit writes pan 0 there
        pan_[kTune + 3 - kPanTab] = 0;
    }

    // 0x1e45, music part
    void irq() {
        if (!on) return;
        acc_ -= kPitDiv;
        while (acc_ <= 0) {
            play();
            acc_ += tempo_;
        }
    }

private:
    struct Channel {  // 0x1e88b + 16k
        int64_t per = 0, tgt = 0;
        int ins = 0;
        int vol = 0;
        int pspd = 0, vsl = 0;
    };

    void setTempo(int bpm) {  // 0x1e1e
        if (bpm) tempo_ = kTempoK / bpm;
    }

    void setFreq(const Channel& c, int64_t per) {  // 0x21d9
        int64_t c2 = c.ins ? t_.ins[size_t(c.ins)].c2spd : nullC2spd_;
        if (per == 0) per = 1;
        int64_t q = (c2 * kFreqK) / per;
        g_.w16(0x01, int(q >> 11));
    }

    void startSample(const Channel& c) {  // 0x2205
        const Instrument& r = t_.ins[size_t(c.ins)];
        g_.w8(0x00, 3);
        g_.w8(0x00, 3);
        uint32_t a = r.start + ofs_;
        ofs_ = 0;
        const uint32_t regs[3][2] = {{0x0a, a}, {0x02, r.loop}, {0x04, r.end}};
        for (const auto& rv : regs) {
            g_.w16(int(rv[0]), int((rv[1] >> 7) & 0xffff));      // address bits 19..7
            g_.w16(int(rv[0]) + 1, int((rv[1] << 9) & 0xffff));  // bits 6..0 in 15..9, fraction 0
        }
        g_.w8(0x00, r.loop ? 8 : 0);  // forward, 8-bit, loop on/off
    }

    void setVolume(int vol) {  // 0x2112
        int bh = t_.voltab[std::min(vol, 64)];
        if (bh <= 5)
            bh = 5;
        else if (bh >= 0xfa)
            bh = 0xfa;
        g_.w8(0x06, 0);
        g_.w8(0x0d, 3);
        int bl = (g_.r16(0x89) >> 8) & 0xff;  // current (ramped) volume
        if (bl == bh) return;
        if (bl < bh) {
            g_.w8(0x07, bl);
            g_.w8(0x08, bh);
            g_.w8(0x0d, 0x00);
        } else {
            g_.w8(0x07, bh);
            g_.w8(0x08, bh);
            g_.w8(0x0d, 0x40);
        }
        g_.w8(0x06, 0x30);
    }

    void trigger(const Channel& c) {  // 0x20f2
        setFreq(c, c.per);
        startSample(c);
        setVolume(c.vol);
    }

    void play() {  // 0x1ed6
        count = (count - 1) & 0xff;
        if (!(count > 0 && count < 0x80)) {  // 'dec byte; jg'
            count = speed;
            row++;
            if ((row & 0xffff) >= t_.nrows) row = 0;
            for (int k = 0; k < t_.nch; k++) {
                Channel& c = ch_[size_t(k)];
                g_.select(t_.nch - 1 - k);  // channel k -> voice 9 - k
                const Event& e = t_.ev(row, k);
                bool trig = false;
                if (e.note) {
                    trig = true;
                    c.tgt = t_.period[e.note];
                }
                if (e.ins) {
                    c.ins = e.ins;
                    trig = true;
                }
                if (e.eff) {
                    if (e.eff == 0xa) speed = e.par;
                    if (e.eff == 9) setTempo(e.par);
                    if (e.eff == 0xd) ofs_ = uint32_t(e.par) << 8;
                    if (e.eff == 3 || e.eff == 8) trig = false;
                }
                if (trig) {
                    c.per = c.tgt;
                    trigger(c);
                }
                if (e.vol != 0xff) {
                    c.vol = e.vol;
                    setVolume(e.vol);
                }
            }
        }
        // per-tick effects (the row tick included)
        for (int k = 0; k < t_.nch; k++) {
            Channel& c = ch_[size_t(k)];
            g_.select(t_.nch - 1 - k);
            const Event& e = t_.ev(row, k);
            if (e.eff >= 1 && e.eff <= 3) {
                if (e.eff == 1) c.tgt = 0;
                if (e.eff == 2) c.tgt = 0x7ffff;
                if (count == 3) continue;  // no slide while the countdown is 3
                int spd = e.par ? e.par : c.pspd;
                c.pspd = spd;
                int64_t per = c.per;
                if (per == c.tgt) continue;
                if (per > c.tgt)
                    per = std::max(per - spd, c.tgt);
                else
                    per = std::min(per + spd, c.tgt);
                c.per = per;
                setFreq(c, per);
            } else if (e.eff == 4) {  // volume slide: only the low nibble works (bug; unused)
                int spd = e.par ? e.par : c.vsl;
                c.vsl = spd;
                int v = c.vol - (spd & 0x0f);
                if (v < 0) v = 0;
                c.vol = v;
                setVolume(v);
            } else if (e.eff == 8) {  // note delay
                if (((count - 1) & 0xff) == e.par) {
                    c.per = c.tgt;
                    trigger(c);
                }
            }
        }
    }

    const Tune& t_;
    Gf1& g_;
    std::vector<Channel> ch_;
    uint8_t pan_[32] = {};
    int nullC2spd_ = 0;
    int64_t acc_ = 0;    // [0x1e5fb]
    int64_t tempo_ = 0;  // [0x1e5ff]
    uint32_t ofs_ = 0;   // [0x1c20] pending sample offset
};

constexpr int kFrameBytes = int(sizeof(float) * 2);

}  // namespace

// Locking: SDL holds the stream's lock while it runs audioCallback, which then takes lock_. Every main-thread
// access to the shared state therefore locks the stream first (SDL_LockAudioStream) and lock_ second.

Music::Music() { nsAtCallback_ = SDL_GetTicksNS(); }
Music::~Music() { stopPlayback(); }

bool Music::load(const PaperExe& exe, std::string* err) { return render(exe, err, nullptr); }

bool Music::render(const PaperExe& exe, std::string* err, std::string* gusLog) {
    try {
        Tune tune = parseTune(exe);
        Gf1 gus(tune.dram);
        gus.log = gusLog;
        Player pl(tune, gus, exe);

        std::vector<float> pcm;
        pcm.reserve(size_t(std::ceil(kEndTick * kGf1Clock / 16 / kTickHz)) * 2 + 16);
        std::vector<int16_t> ctr(static_cast<size_t>(kEndTick)), cdn(static_cast<size_t>(kEndTick));
        std::vector<double> L, R;
        double facc = 0;
        gus.tick = 0;
        pl.soundInit();  // tick 0: 0x1c24 runs before the first IRQ
        double rate = gus.frameRate();
        for (int t = 0; t < kEndTick; t++) {
            gus.tick = t;
            if (t == kMusicStart) pl.on = true;
            pl.irq();  // IRQ0 at the end of tick t
            ctr[size_t(t)] = int16_t(pl.row);
            cdn[size_t(t)] = int16_t(pl.count);
            facc += gus.frameRate() / kTickHz;  // frames until the next IRQ
            int n = int(facc);
            facc -= n;
            L.assign(size_t(n), 0.0);
            R.assign(size_t(n), 0.0);
            gus.advance(n, L.data(), R.data());
            for (int k = 0; k < n; k++) {
                pcm.push_back(float(L[size_t(k)] * kGain));
                pcm.push_back(float(R[size_t(k)] * kGain));
            }
        }
        gus.tick = kEndTick;
        pl.on = false;
        pl.gf1Init();  // 0x1c8e: sound off (only shows in the log)

        SDL_AudioStream* s = stream_;
        if (s) SDL_LockAudioStream(s);
        {
            std::lock_guard<std::mutex> g(lock_);
            pcm_ = std::move(pcm);
            rate_ = rate;
            counter_ = std::move(ctr);
            countdown_ = std::move(cdn);
        }
        if (s) SDL_UnlockAudioStream(s);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

int Music::counter(int t) const {
    if (counter_.empty()) return -1;
    return counter_[size_t(std::clamp(t, 0, int(counter_.size()) - 1))];
}

int Music::rowCountdown(int t) const {
    if (countdown_.empty()) return 1;
    return countdown_[size_t(std::clamp(t, 0, int(countdown_.size()) - 1))];
}

// ---- playback (pattern of the 303 remake's audio.cpp) ----

void Music::audioCallback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    // SDL holds the stream's lock while calling this; lock_ is taken second (main thread: same order).
    Music* m = static_cast<Music*>(user);
    int frames = additional / kFrameBytes;
    if (frames <= 0) return;
    std::lock_guard<std::mutex> g(m->lock_);
    m->buf_.assign(size_t(frames) * 2, 0.0f);
    if (!m->paused_) {
        size_t total = m->pcm_.size() / 2;
        if (m->playPos_ < total) {
            size_t k = std::min(size_t(frames), total - m->playPos_);
            std::memcpy(m->buf_.data(), m->pcm_.data() + m->playPos_ * 2, k * 2 * sizeof(float));
        }
        m->playPos_ += size_t(frames);  // silence after the end
    }
    SDL_PutAudioStreamData(stream, m->buf_.data(), frames * kFrameBytes);
    if (!m->paused_ && m->rate_ > 0) {
        // audible "now" = everything handed over minus what is still queued in the stream
        int queued = SDL_GetAudioStreamQueued(stream) / kFrameBytes;
        m->posAtCallback_ = (double(m->playPos_) - double(queued)) * kTickHz / m->rate_;
        m->nsAtCallback_ = SDL_GetTicksNS();
        m->awaitCallback_ = false;
        if (m->posAtCallback_ >= kEndTick) m->ended_ = true;
    }
}

bool Music::startPlayback() {
    stopPlayback();
    {
        std::lock_guard<std::mutex> g(lock_);
        // the clock (re)starts now from the current position, with or without a device
        posAtCallback_ = lastTick_;
        nsAtCallback_ = SDL_GetTicksNS();
        lastNs_ = 0;
    }
    if (pcm_.empty() || rate_ <= 0) return false;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("audio: %s", SDL_GetError());
            return false;
        }
        ownAudioInit_ = true;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = int(std::lround(rate_));  // 38588 for 38587.5 Hz (13 ppm fast; positions use rate_)
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Music::audioCallback, this);
    if (!s) {
        SDL_Log("audio: %s", SDL_GetError());
        if (ownAudioInit_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            ownAudioInit_ = false;
        }
        return false;
    }
    SDL_LockAudioStream(s);
    {
        std::lock_guard<std::mutex> g(lock_);
        stream_ = s;
        haveDevice_ = true;
        posAtCallback_ = lastTick_;
        playPos_ = size_t(std::llround(lastTick_ * rate_ / kTickHz));
        nsAtCallback_ = SDL_GetTicksNS();
        lastNs_ = 0;
        awaitCallback_ = true;
    }
    SDL_UnlockAudioStream(s);
    SDL_ResumeAudioStreamDevice(s);
    return true;
}

void Music::stopPlayback() {
    if (!stream_) return;
    // SDL_Quit destroys all streams; only touch ours while the audio subsystem is still up. After
    // SDL_DestroyAudioStream no callback runs any more.
    const bool alive = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    if (alive) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    if (ownAudioInit_ && alive) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ownAudioInit_ = false;
    std::lock_guard<std::mutex> g(lock_);
    double t = clockLocked(SDL_GetTicksNS());  // continue on the timer from the last audible position
    haveDevice_ = false;
    awaitCallback_ = false;
    posAtCallback_ = t;
    nsAtCallback_ = lastNs_;
    playPos_ = rate_ > 0 ? size_t(std::llround(t * rate_ / kTickHz)) : 0;
}

double Music::clockLocked(uint64_t now) {
    constexpr double kStartWait = 0.5;           // s: longest wait for the first callback before running anyway
    constexpr double kSnap = 0.1 * kTickHz;      // ticks: larger errors are not slewed
    constexpr double kSlewTau = 0.5;             // s: time constant of the slew towards the callback estimate
    double raw = posAtCallback_;                 // position estimate: last callback + elapsed time
    if (!paused_ && now > nsAtCallback_) {
        double el = double(now - nsAtCallback_) * 1e-9;
        if (awaitCallback_) el = std::max(0.0, el - kStartWait);  // hold until the device starts pulling
        raw += el * kTickHz;
    }
    double t = raw;
    if (haveDevice_ && !paused_ && lastNs_ != 0 && now > lastNs_) {
        // The callback estimate jitters by a few ms (device period, scheduling). Run at the nominal rate from the
        // last returned value and slew towards the estimate (rate within +-25%); snap when it is far off.
        double dt = double(now - lastNs_) * 1e-9, pred = lastTick_ + dt * kTickHz, err = raw - pred;
        if (std::fabs(err) < kSnap) {
            double lim = 0.25 * dt * kTickHz;
            t = pred + std::clamp(err * std::min(1.0, dt / kSlewTau), -lim, lim);
        }
    }
    t = std::max(t, lastTick_);  // never backwards between seeks
    lastTick_ = t;
    lastNs_ = now;
    if (t >= kEndTick) ended_ = true;
    return t;
}

double Music::tick() {
    SDL_AudioStream* s = stream_;
    if (s) SDL_LockAudioStream(s);
    double t;
    {
        std::lock_guard<std::mutex> g(lock_);
        t = clockLocked(SDL_GetTicksNS());
    }
    if (s) SDL_UnlockAudioStream(s);
    return t;
}

void Music::seekTick(double t) {
    t = std::max(0.0, t);
    SDL_AudioStream* s = stream_;
    if (s) SDL_LockAudioStream(s);
    {
        std::lock_guard<std::mutex> g(lock_);
        playPos_ = rate_ > 0 ? size_t(std::llround(t * rate_ / kTickHz)) : 0;
        if (s) SDL_ClearAudioStream(s);
        posAtCallback_ = t;
        lastTick_ = t;
        nsAtCallback_ = SDL_GetTicksNS();
        lastNs_ = 0;
        awaitCallback_ = haveDevice_;
        ended_ = t >= kEndTick;
    }
    if (s) SDL_UnlockAudioStream(s);
}

void Music::setPaused(bool p) {
    SDL_AudioStream* s = stream_;
    if (s) SDL_LockAudioStream(s);
    {
        std::lock_guard<std::mutex> g(lock_);
        if (p != paused_) {
            uint64_t now = SDL_GetTicksNS();
            if (p) {
                // freeze at the current position; playback resumes from exactly there
                double t = clockLocked(now);
                posAtCallback_ = t;
                playPos_ = rate_ > 0 ? size_t(std::llround(t * rate_ / kTickHz)) : 0;
            }
            if (s) SDL_ClearAudioStream(s);  // drop queued music on pause, queued silence on resume
            nsAtCallback_ = now;
            lastNs_ = 0;
            awaitCallback_ = haveDevice_ && !p;
            paused_ = p;
        }
    }
    if (s) SDL_UnlockAudioStream(s);
}
