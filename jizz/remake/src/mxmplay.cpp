// JIZZ's soundtrack: Niklas Beisert's MXMPlay (GUS XM player) in the cut-down build the intro links (0x296dc..
// 0x2a316), ported routine by routine (jizz/re/notes-music.md section 4; mirrors jizz/re/models/music/mxmplay.py,
// which reproduces the capture's GUS register stream, DRAM and music positions). It plays the module that
// mp4synth.cpp builds from JIZZ.EXE on the GF1 model of gf1.cpp.
//
// Routines: 0x29738 xmpInit + 0x2a26f sample upload, 0x29db1 xmpPlay, 0x29e22 xmpStop, 0x2a02a GF1 reset, 0x29ed1 /
// 0x29e38 timer install and IRQ0, 0x299c4 xmpGetPos, 0x2a13c GUS update, 0x2a0b6 set volume, 0x2a11c voice off,
// 0x29b26 player tick, 0x29801 new note, 0x297a8 pitch -> frequency, 0x296e7 volume table, 0x29cc4 volume /
// envelope, the effect handlers 0x299f1..0x29af6 (jump table 0x2a3f7).
//
// Timing: one player tick per IRQ0, every 0x2d86bf / BPM PIT clocks; each IRQ first writes the previous tick's
// results to the GUS, then computes the next tick (a row is heard one tick after xmpGetPos shows it). Music time 0
// is the first IRQ after xmpPlay (xmpGetTimer = -period, jizz_reference.mp4 frame 0); the register writes of an
// IRQ apply at its instant (after the GF1 frames the ramp wait takes), then the GF1 renders up to the next IRQ.
// The soundtrack runs until the position reaches the last entry of the intro's sync table (event -1: the intro
// exits at 15:1c), including the frames up to the next IRQ, and is normalised to 0.9 of its peak (as
// mxmplay.py --wav does).
#include "mxmplay.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "exe.h"
#include "gf1.h"
#include "mp4synth.h"

namespace {

constexpr double kPitHz = 1193182.0;
constexpr uint32_t kTempoK = 0x2d86bf;    // PIT clocks per player tick x BPM
constexpr uint32_t kFreqDiv = 0x1ee;      // GF1 FC = freq * voices / 494, bit 0 cleared
constexpr uint32_t kFreqTab[4] = {0x2a357, 0x2a377, 0x2a397, 0x2a3b7};  // 16 x u16, u16, u16, u32
constexpr uint32_t kSyncTable = 0x6417;   // word position, signed word event; event -1 exits the intro
constexpr int kPlayLogTick = 433;         // capture log tick of xmpPlay (IRQ k is logged as 433 + k)
constexpr uint32_t kNone = 0xffffffffu;   // "no next order"
constexpr int kMaxIrqs = 1 << 20;
constexpr float kLevel = 0.9f;            // output peak

uint32_t s16u(uint32_t v) { return uint32_t(int32_t(int16_t(uint16_t(v)))); }

// ---- the module (notes-music.md section 3) ----
struct Module {
    std::vector<uint8_t> d;
    uint32_t ordnum = 0, restart = 0, channels = 0, insnum = 0, opt = 0, smpstart = 0, smp8 = 0, smp16 = 0;
    uint32_t speed = 0, bpm = 0;
    int32_t lowpitch = 0, highpitch = 0;

    explicit Module(std::vector<uint8_t> bytes) : d(std::move(bytes)) {
        if (d.size() < 0x750 || std::memcmp(d.data(), "MXM\0", 4) != 0) throw std::runtime_error("not an MXM module");
        ordnum = u32(4);
        restart = u32(8);
        channels = u32(0xc);
        insnum = u32(0x14);
        speed = u8(0x18);
        bpm = u8(0x19);
        opt = u16(0x1a);
        smpstart = u32(0x1c);
        smp8 = u32(0x20);
        smp16 = u32(0x24);
        lowpitch = int32_t(u32(0x28));
        highpitch = int32_t(u32(0x2c));
        if (channels < 1 || channels > 32 || bpm == 0) throw std::runtime_error("unexpected MXM header");
        // the buffer behind the expanded module is zeroed: the upload reads up to the end of the sample block
        size_t need = size_t(smpstart) + smp8 + 2 * size_t(smp16);
        if (need > (size_t(1) << 24)) throw std::runtime_error("unexpected MXM sample size");
        if (d.size() < need) d.resize(need, 0);
    }
    uint8_t u8(uint32_t a) const {
        if (a >= d.size()) throw std::runtime_error("MXM read out of range");
        return d[a];
    }
    uint32_t u16(uint32_t a) const { return uint32_t(u8(a) | u8(a + 1) << 8); }
    uint32_t u24(uint32_t a) const { return u16(a) | uint32_t(u8(a + 2)) << 16; }
    uint32_t u32(uint32_t a) const { return u16(a) | u16(a + 2) << 16; }
    uint32_t insPtr(uint32_t i) const { return u32(0x14c + 4 * i); }  // instrument i (1..), [D + 0x1a6 + 4i]
};

// ---- channel record (+0x9ac + 0x100 * ch) ----
struct Chan {
    int active = 0;                          // +0x00
    uint32_t start = 0, end = 0, loop = 0;   // +0x01, +0x05, +0x09 DRAM addresses
    int mode = 0;                            // +0x0d GF1 voice control
    int stop = 0, newsmp = 0;                // +0x0e, +0x0f
    int64_t offset = 0;                      // +0x10 restart offset (-1 = none)
    int gfreq = 0, gvol = 0, gpan = 0;       // +0x14, +0x16, +0x18
    uint32_t vol = 0, fvol = 0, pan = 0, fpan = 0;  // +0x19..+0x1c
    int32_t pitch = 0, fpitch = 0;           // +0x1d, +0x21 (1/256 semitone, lower = higher)
    uint32_t ins = 0, insp = 0, rel = 0;     // +0x25, +0x26, +0x2a
    int keyon = 0;                           // +0x2c
    int32_t fade = 0;                        // +0x2d
    uint32_t envpt = 0, envpos = 0;          // +0x31, +0x35
    uint32_t svol = 0, span = 0;             // +0x3d, +0x3e
    uint32_t eff = 0xff, volcmd = 0;         // +0x3f, +0x40
    int32_t target = 0;                      // +0x41 (written, never used)
    uint32_t vslide = 0, fpup = 0, fpdn = 0, retrig = 0, note = 0;  // +0x49, +0x57, +0x58, +0x69, +0x6d
};

class Player {
public:
    Player(const Module& m, const JizzExe& x, gus::Card& g) : m_(m), g_(g) {
        for (int i = 0; i < 16; i++) {
            ft_[0][i] = x.u16(kFreqTab[0] + 2 * uint32_t(i));
            ft_[1][i] = x.u16(kFreqTab[1] + 2 * uint32_t(i));
            ft_[2][i] = x.u16(kFreqTab[2] + 2 * uint32_t(i));
            ft_[3][i] = x.u32(kFreqTab[3] + 4 * uint32_t(i));
        }
        // 0x296e7: 257 words, linear 0..256 -> GF1 log volume
        voltab_[256] = 0xefff;
        for (uint32_t i = 1; i < 256; i++) {
            uint32_t e = 7;
            for (uint32_t v = i; v; v >>= 1) e++;
            voltab_[i] = ((((i << (20 - e)) & 0xfff) | (e << 12)) - 0x1000) & 0xffff;
        }
    }

    // 0x29738 xmpInit: the sample upload 0x2a26f (16-byte blocks; the rest of the length is dropped)
    void init() {
        uint32_t n = m_.smp16 * 2 + m_.smp8;
        for (uint32_t a = 0; a < (n >> 4) << 4; a++) g_.poke(a, m_.u8(m_.smpstart + a));
    }

    // 0x29db1 xmpPlay (with the GUS start 0x2a23f and the timer install 0x29ed1)
    void play(uint32_t order) {
        jumporder_ = order;
        order_ = order;
        row_ = 0;
        gf1Reset(m_.channels);
        g_.select(0);
        g_.port(0, 9);
        for (int i = 0; i < 32; i++) {
            ch_[i] = Chan{};
            ch_[i].pan = m_.u8(0x30 + uint32_t(i));
        }
        globalvol_ = 0x40;
        jumprow_ = 0;
        speed_ = m_.speed;
        tick_ = (m_.speed - 1) & 0xff;
        period_ = kTempoK / m_.bpm;
        remain_ = 0;
        cur_ = 0x10000;
        timer_ = 0xffff0000u - period_;
    }

    void stop() { gf1Reset(14); }  // 0x29e22 xmpStop -> 0x2a25e

    // 0x29e38 IRQ0: true if a player tick ran
    bool irq() {
        timer_ += cur_;
        const uint32_t maxper = 0x10000;
        uint32_t edx = remain_ - maxper;
        if (!(remain_ > maxper)) edx = period_;
        remain_ = edx;
        cur_ = std::min(remain_, maxper);
        if (remain_ != period_) return false;
        gusUpdate();
        playerTick();
        return true;
    }

    int pos() const { return int((order_ & 0xff) << 8 | (row_ & 0xff)); }  // 0x299c4 xmpGetPos
    int32_t timer() const { return int32_t(timer_); }                     // xmpGetTimer right after an IRQ
    uint32_t cur() const { return cur_; }                                  // PIT clocks to the next IRQ

private:
    // 0x2a02a GF1 reset (voices = max(channels, 14)); each write is followed by delays on the real card
    void gf1Reset(uint32_t nch) {
        voices_ = (nch & 0xff) >= 14 ? nch : (nch & ~0xffu) | 14;
        g_.w8(0x4c, 0);
        g_.w8(0x4c, 1);
        g_.w8(0x0e, int(((voices_ - 1) | 0xc0) & 0xff));
        for (int v = 0; v < 32; v++) {
            g_.select(v);
            g_.w16(0x09, 0);
            g_.w8(0x00, 3);  // 0x29f83: write, delay, write again
            g_.w8(0x00, 3);
            g_.w8(0x0d, 3);
            g_.w8(0x0d, 3);
            g_.w8(0x06, 0x3f);
        }
        g_.w8(0x4c, 7);
        g_.select(0);
        g_.port(0, 8);
    }

    // 0x2a13c GUS update: stop the voices that get a new note, wait for the ramps, then write every channel
    void gusUpdate() {
        const uint32_t n = m_.channels;
        for (uint32_t i = 0; i < n; i++) {
            Chan& c = ch_[i];
            if (c.active && c.stop) {
                g_.select(int(i));
                g_.w8(0x00, c.mode | 3);
                g_.w8(0x00, c.mode | 3);
                voiceOff();
                c.stop = 0;
            }
        }
        g_.waitRamps(int(n));  // 0x2a17f: until bit 0 of reg 0x8d, every channel
        for (uint32_t i = 0; i < n; i++) {
            g_.select(int(i));
            Chan& c = ch_[i];
            if (!c.active) {
                voiceOff();
            } else {
                if (c.newsmp) {
                    waddr(0x02, c.loop);
                    waddr(0x04, c.end);
                }
                if (c.offset != -1) {
                    uint32_t a = uint32_t(c.offset) + c.start;
                    if (a >= c.end) a = c.end - 1;
                    waddr(0x0a, a);
                    int ctl = g_.r8(0x80);
                    g_.w8(0x00, (ctl & 0x40) | c.mode);
                }
                g_.r8(0x80);  // result ignored: the code tests bl (= 0x80), not al
                g_.w8(0x0c, c.gpan);
                g_.w16(0x01, c.gfreq);
                setVolume(c.gvol);
            }
            c.newsmp = 0;
            c.offset = -1;
        }
    }

    void waddr(int reg, uint32_t a) {  // 0x2a00b
        g_.w16(reg, int((a >> 7) & 0x1fff));
        g_.w16(reg + 1, int((a << 9) & 0xffff));
    }

    void voiceOff() {  // 0x2a11c: ramp down to 0x04xx
        g_.w8(0x07, 0x04);
        g_.w8(0x08, 0xfc);
        g_.w8(0x0d, 0x40);
        g_.w8(0x0d, 0x40);
    }

    void setVolume(int target) {  // 0x2a0b6: from the current volume (reg 0x89)
        int cur = g_.r16(0x89);
        int lo = cur, hi = target, mode = 0;
        if (cur > target) {
            lo = target;
            hi = cur;
            mode = 0x40;
        }
        int diff = hi - lo;
        if (diff == 0) return;
        if (diff < 0x1000) {  // less than one exponent step: set directly
            g_.w16(0x09, target);
            return;
        }
        lo = std::max(lo, 0x1000);
        hi = std::min(hi, 0xfc00);
        g_.w8(0x07, lo >> 8);
        g_.w8(0x08, hi >> 8);
        g_.w8(0x0d, mode);
        g_.w8(0x0d, mode);
    }

    // 0x297a8: ~6848 * 2^(-pitch / 3072) from four nibble tables (64-bit products, >> 15, low 32 bits)
    uint32_t freq(int32_t pitch) const {
        uint32_t x = uint32_t(pitch) + 0x8000;
        uint64_t f = ft_[3][(x >> 12) & 15];
        f = ((f * ft_[2][(x >> 8) & 15]) >> 15) & 0xffffffffu;
        f = ((f * ft_[1][(x >> 4) & 15]) >> 15) & 0xffffffffu;
        f = ((f * ft_[0][x & 15]) >> 15) & 0xffffffffu;
        return uint32_t(f);
    }

    // 0x29b26 player tick
    void playerTick() {
        const uint32_t n = m_.channels;
        newrow_ = 0;
        for (uint32_t i = 0; i < n; i++) {
            Chan& c = ch_[i];
            c.fvol = c.vol;
            c.fpan = c.pan;
            c.fpitch = c.pitch;
        }
        tick_ = (tick_ + 1) & 0xff;
        if (tick_ == speed_) {
            tick_ = 0;
            if (patdelay_) {
                patdelay_ = (patdelay_ - 1) & 0xff;
            } else {
                newrow_ = 1;
                row_++;
                bool newpat = true;
                if (jumporder_ == kNone) {
                    if (row_ < patrows_) {
                        newpat = false;
                    } else {
                        jumporder_ = order_ + 1;
                        jumprow_ = 0;
                    }
                }
                if (newpat) {
                    // (a change of order clears bytes +0x63 / +0x64 of every channel: never read)
                    order_ = jumporder_ < m_.ordnum ? jumporder_ : m_.restart;
                    row_ = jumprow_;
                    jumporder_ = kNone;
                    uint32_t p = m_.u32(0x350 + 4 * uint32_t(m_.u8(0x50 + (order_ & 0xff))));
                    patrows_ = m_.u32(p);
                    p += 4;
                    for (; jumprow_; jumprow_--) {  // skip rows
                        for (;;) {
                            uint32_t f = m_.u8(p++);
                            if (!f) break;
                            if (f & 0x20) p += 2;
                            if (f & 0x40) p += 1;
                            if (f & 0x80) p += 2;
                        }
                    }
                    patpos_ = p;
                }
                readRow();
            }
        }
        // 0x29ca0: the effects of every tick, then the GF1 values
        for (uint32_t i = 0; i < n; i++) {
            Chan& c = ch_[i];
            fx(0x78 + c.volcmd, c);
            if (c.eff < 0x34) fx(0x34 + c.eff, c);
            if (c.insp) volumeEnvelope(c);
            if (c.fvol > 128) throw std::runtime_error("MXM: volume out of range");
            c.gvol = int(voltab_[2 * c.fvol]);
            c.gpan = int(c.fpan >> 4);
            c.gfreq = int((uint64_t(freq(c.fpitch)) * voices_ / kFreqDiv) & 0xfffe);
        }
    }

    void readRow() {
        uint32_t p = patpos_;
        for (uint32_t i = 0; i < m_.channels; i++) {
            Chan& c = ch_[i];
            note_ = ins_ = vol_ = eff_ = par_ = 0;
            c.eff = 0xff;
            uint32_t f = m_.u8(p);
            if (f != 0 && (f & 0x1f) == i) {
                p++;
                if (f & 0x20) {
                    note_ = m_.u8(p);
                    ins_ = m_.u8(p + 1);
                    p += 2;
                }
                if (f & 0x40) vol_ = m_.u8(p++);
                if (f & 0x80) {
                    eff_ = m_.u8(p);
                    par_ = m_.u8(p + 1);
                    p += 2;
                }
            }
            newNote(c);
            uint32_t v = vol_;
            vol_ &= 0x0f;
            c.volcmd = v >> 4;
            fx(0x68 + (v >> 4), c);  // volume column, row tick
            if (eff_ < 0x34) {
                c.eff = eff_;
                fx(eff_, c);  // effect, row tick
            }
        }
        patpos_ = p + 1;
    }

    // 0x29801 new note (row tick, per channel)
    void newNote(Chan& c) {
        bool porta = eff_ == 3 || eff_ == 5 || vol_ >= 0xf0;
        bool keyoff = false;
        if (eff_ == 0x14 && par_ == 0) {  // K00
            keyoff = true;
            c.keyon = 0;
        } else if (note_ == 0x61) {  // key off note
            note_ = 0;
            keyoff = true;
            c.keyon = 0;
        }
        if (ins_ != 0 && ins_ <= m_.insnum) c.ins = ins_;
        if (c.ins == 0) return;
        if (note_ != 0) {
            if (ins_ != 0) c.keyon = 1;
            c.note = note_;
            if (eff_ == 0x31 && par_ != 0) return;  // EDx (note delay): the note is dropped
            const uint32_t note = note_ - 1;
            if (porta) {
                c.target = int32_t(s16u(0x3000 - ((note << 8) + c.rel)));
            } else {
                c.stop = 1;
                const uint32_t insp = m_.insPtr(c.ins);
                const uint32_t smp = m_.u8(insp + 4 + note);
                if (smp >= m_.u32(insp)) return;
                const uint32_t h = insp + 0x100 + 16 * smp;
                c.active = 1;
                c.newsmp = 1;
                c.start = m_.u24(h);
                c.loop = m_.u24(h + 3);
                c.end = m_.u24(h + 6);
                c.mode = m_.u8(h + 9);
                if (ins_ != 0) {
                    c.insp = insp;
                    c.svol = m_.u8(h + 0xa);
                    c.span = m_.u8(h + 0xb);
                }
                c.rel = m_.u16(h + 0xc);
                c.pitch = c.fpitch = c.target = int32_t(s16u(0x3000 - ((note << 8) + c.rel)));
                c.offset = 0;
            }
        }
        // 0x2994c: an instrument resets volume, panning, fade and envelope
        if (c.keyon && ins_ != 0) {
            c.vol = c.fvol = c.svol;
            if (!(m_.opt & 2)) c.pan = c.fpan = c.span;
            c.fade = 0x8000;
            c.envpt = 0;
            c.envpos = 0;
        }
        if (keyoff && ins_ == 0 && m_.u8(c.insp + 0x6a) == 0) c.fade = 0;  // no envelope: key off cuts
    }

    // 0x29cc4..0x29d6d: master and global volume, fadeout, volume envelope (stepped per tick)
    void volumeEnvelope(Chan& c) {
        const uint32_t ip = c.insp;
        uint32_t v = ((c.fvol * mastervol_) >> 6) & 0xff;
        v = (v * globalvol_) & 0xffff;
        v = ((uint32_t((uint64_t(v) * uint32_t(c.fade)) >> 16)) >> 4) & 0xff;
        c.fvol = v;
        if (!c.keyon) c.fade = std::max<int32_t>(c.fade - int32_t(m_.u16(ip + 0x64)), 0);
        uint32_t pt = c.envpt;
        if (c.envpos != 0 && (pt & 0xff) == m_.u8(ip + 0x6d)) {
            pt = (pt & ~0xffu) | m_.u8(ip + 0x6c);
            c.envpt = pt;
        }
        const uint32_t e = ip + 0x6e + 4 * pt;
        if ((pt & 0xff) == m_.u8(ip + 0x6a)) {
            c.fvol = ((m_.u8(e + 2) * c.fvol) >> 6) & 0xff;
            return;
        }
        const uint32_t ln = m_.u16(e);
        const int32_t v0 = int16_t(m_.u16(e + 2)), v1 = int16_t(m_.u16(e + 6));
        const int32_t num = int32_t(int16_t(uint16_t(v1 - v0))) * int32_t(int16_t(uint16_t(c.envpos)));
        const int32_t den = int16_t(uint16_t(ln));
        if (den == 0) throw std::runtime_error("MXM: envelope segment of length 0");
        const int32_t q = num / den;  // idiv: truncates toward zero
        const uint32_t val = uint32_t(q + int32_t(m_.u8(e + 2))) & 0xff;
        c.fvol = ((val * c.fvol) >> 6) & 0xff;
        uint32_t pos = c.envpos;
        if (!(pos == 0 && c.keyon && (c.envpt & 0xff) == m_.u8(ip + 0x6b))) {
            pos++;
            if (pos >= ln) {
                pos = 0;
                c.envpt++;
            }
        }
        c.envpos = pos & 0xffff;
    }

    int32_t clampPitch(int32_t p) const {  // 0x299d4
        if (p <= m_.lowpitch) p = m_.lowpitch;
        if (p >= m_.highpitch) p = m_.highpitch;
        return p;
    }

    // the effect handlers of this build (jump table 0x2a3f7); everything else is 0x299f1 (ret)
    void fx(uint32_t idx, Chan& c) {
        const uint32_t par = par_;
        switch (idx) {
        case 0x05: case 0x06: case 0x0a:  // 0x29ac0: volume slide memory
            if (par) c.vslide = par;
            break;
        case 0x08: c.pan = c.fpan = par; break;                       // 0x29a4a 8xx set panning
        case 0x0c: c.vol = c.fvol = std::min(par, 0x40u); break;      // 0x29a3a Cxx set volume
        case 0x0d:                                                    // 0x299f2 Dxx pattern break (decimal)
            if (jumporder_ == kNone) jumporder_ = order_ + 1;
            jumprow_ = (par >> 4) * 10 + (par & 15);
            break;
        case 0x0f:  // 0x29a1b Fxx: speed below 0x20, else tempo
            if (par >= 0x20)
                period_ = kTempoK / par;
            else if (par)
                speed_ = par;
            break;
        case 0x25:  // 0x29a64 E1x fine porta up
            if (par) c.fpup = par;
            c.pitch = c.fpitch = clampPitch(c.pitch - int32_t(c.fpup << 4));
            break;
        case 0x26:  // 0x29a86 E2x fine porta down
            if (par) c.fpdn = par;
            c.pitch = c.fpitch = clampPitch(c.pitch + int32_t(c.fpdn << 4));
            break;
        case 0x32: patdelay_ = par; break;  // 0x29a14 EEx pattern delay
        case 0x3e: {                        // 0x29acb Axy volume slide, every tick but the row tick
            uint32_t al = c.vol;
            const uint32_t bl = c.vslide;
            if (!newrow_) {
                if (bl & 0xf0)
                    al = std::min(al + (bl >> 4), 0x40u);
                else
                    al = al >= bl ? al - bl : 0;
            }
            c.vol = c.fvol = al;
            break;
        }
        case 0x61:  // 0x29af6 E9x retrigger: the interval is never set, so this does nothing
            if (c.retrig && tick_ % c.retrig == 0) c.offset = 0;
            break;
        case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d:  // volume column 0x10..0x50: set volume
            c.vol = c.fvol = idx == 0x6d ? 0x40 : (idx - 0x69) * 0x10 + vol_;
            break;
        case 0x74: c.pan = c.fpan = (vol_ << 4) | vol_; break;  // volume column 0xCx: set panning
        default: break;
        }
    }

    const Module& m_;
    gus::Card& g_;
    uint32_t ft_[4][16] = {};
    uint32_t voltab_[257] = {};
    Chan ch_[32];
    uint32_t globalvol_ = 0, mastervol_ = 0x40;  // +0x00, +0x01 (xmpSetVolume 0x299ae is never called)
    uint32_t tick_ = 0, speed_ = 0, newrow_ = 0;  // +0x03, +0x04, +0x05
    uint32_t row_ = 0, patpos_ = 0, patrows_ = 0;  // +0x06, +0x0a, +0x0e
    uint32_t order_ = 0, jumporder_ = 0, jumprow_ = 0, patdelay_ = 0;  // +0x12, +0x16, +0x1a, +0x1e
    uint32_t note_ = 0, ins_ = 0, vol_ = 0, eff_ = 0, par_ = 0;  // +0x1f..+0x23: the event being read
    uint32_t timer_ = 0, cur_ = 0, period_ = 0, remain_ = 0;  // +0x3a, +0x36, +0x42, +0x46
    uint32_t voices_ = 14;  // +0x54
};

// The last entry of the sync table (event -1): the position at which the intro stops the music and exits.
int exitPosition(const JizzExe& x) {
    for (uint32_t a = kSyncTable; a < kSyncTable + 4 * 256; a += 4)
        if (x.s16(a + 2) == -1) return x.u16(a);
    throw std::runtime_error("JIZZ.EXE: no end in the sync table");
}

struct Run {
    std::vector<int> pos;      // xmpGetPos after IRQ k (index k - 1)
    std::vector<double> time;  // music time of IRQ k
    double timer0 = 0;         // xmpGetTimer at IRQ 1 (music time 0)
    double rate = 0;           // GF1 frame rate while playing
};

// xmpInit, xmpPlay, the IRQs up to and including the one at which the position reaches exitPos (with the frames
// up to the next IRQ when a GF1 model renders), xmpStop.
void runSong(const Module& m, const JizzExe& x, gus::Card& g, gus::Gf1* model, int exitPos, Run& r) {
    Player pl(m, x, g);
    pl.init();
    g.tick = kPlayLogTick;
    pl.play(0);
    double facc = 0;
    for (int k = 1;; k++) {
        if (k > kMaxIrqs) throw std::runtime_error("MXM: the music never reaches the end of the sync table");
        g.tick = kPlayLogTick + k;
        pl.irq();
        if (k == 1) r.timer0 = pl.timer();
        const int p = pl.pos();
        r.pos.push_back(p);
        r.time.push_back((double(pl.timer()) - r.timer0) / kPitHz);
        if (model) {
            // GF1 frames until the next IRQ, less those the ramp wait of this IRQ already ran
            facc += model->frameRate() * double(pl.cur()) / kPitHz - double(model->waited);
            model->waited = 0;
            const int n = int(facc);
            facc -= n;
            model->run(n);
            r.rate = model->frameRate();
        }
        if (p >= exitPos) break;
    }
    pl.stop();
}

}  // namespace

bool renderSoundtrack(const JizzExe& exe, Soundtrack& out, std::string* err) {
    try {
        const Module m(expandModule(exe));
        gus::Gf1 g;
        std::vector<float> pcm;
        pcm.reserve(size_t(44100) * 2 * 150);
        g.out = &pcm;
        Run r;
        runSong(m, exe, g, &g, exitPosition(exe), r);
        const double gain = g.peak > 0 ? kLevel / g.peak : 1.0;
        for (float& s : pcm) s = float(double(s) * gain);
        out.pcm = std::move(pcm);
        out.rate = r.rate;
        out.timer0 = r.timer0;
        out.rows.clear();
        for (size_t i = 0; i < r.pos.size(); i++)
            if (out.rows.empty() || out.rows.back().pos != r.pos[i]) out.rows.push_back({r.pos[i], r.time[i]});
        out.length = r.time.back();
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool traceSoundtrack(const JizzExe& exe, bool fakeGus, MusicTrace& out, std::string* err) {
    try {
        out.module = expandModule(exe);
        const Module m(out.module);
        gus::Card fake;
        gus::Gf1 model;
        gus::Card& g = fakeGus ? fake : model;
        out.gus.clear();
        out.gus.reserve(size_t(16) << 20);
        g.log = &out.gus;
        Run r;
        runSong(m, exe, g, fakeGus ? nullptr : &model, exitPosition(exe), r);
        out.dram = g.dram();
        out.pos = std::move(r.pos);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}
