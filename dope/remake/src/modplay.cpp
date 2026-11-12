// "ProTracker for GUS replay routine" (Oskari Tammelin, 1994) as DOPE's loader links it, GUS path: GUS init, MOD load
// and sample upload, start / stop, and the GUS interrupt that plays the song (timer 2 ticks, volume-ramp ends that
// switch the samples); dope/re/notes-music.md sections 3-9, a routine-by-routine port that mirrors
// dope/re/models/music/dopeplay.py. The player writes and reads the GUS through gus::Card; the schedule of
// interrupts (the card's events, holds, or a capture's service times) is run(). See modplay.h for the routine map.
#include "modplay.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace modplay {
namespace {

constexpr uint32_t kCode32 = 0x17e0;   // loader.bin: file offset of code32 offset 0
constexpr uint32_t kC2spd = 0x0b37;    // 16 u16: C-2 rate per finetune 0..7, -8..-1
constexpr uint32_t kPeriods = 0x0b5b;  // 96 u16: Amiga periods of 8 octaves (note matching)
constexpr uint32_t kOct1 = 0x0b8b;     // = kPeriods entry 24: the octave the note -> period code starts from
constexpr uint32_t kPanTab = 0x0d7d;   // 32 u8: GF1 pan per voice written at load
constexpr uint32_t kInsTab = 0x11fd;   // 31 instrument records of 0x46 bytes (+0x40: DRAM start)
constexpr uint32_t kSine = 0x22a0;     // 32 u8 vibrato half sine; 4xx reads 64 bytes from here
constexpr uint32_t kLatch = 0x3f66;    // 16 u8: GF1 IRQ latch per IRQ number
constexpr uint32_t kRates = 0x3f76;    // 19 u16: 617400 / n for n = 14..32 active voices
constexpr uint32_t kVolTab = 0x3f9c;   // 65 u16: volume 0..64 -> GF1 register 9
constexpr uint32_t kCount0 = 0x3f2a;   // u8: timer-2 count until the first tick computes one ([0x3f29] before it)
constexpr uint32_t kMaster = 0x1ab6;   // u32: master volume (0x40, never changed)

constexpr uint32_t kAmiga4 = 0xd87b50;   // 0x2dc4: 4 x 3,546,836 Hz (PAL Amiga clock x 4) -> [0xc1b]
constexpr uint32_t kNoteMul = 0x20ab0;   // 0x23ff / 0x2b2d: period of the base octave x 16 x 8363
constexpr uint32_t kTimerUs = 1000000;   // 0x41fc
constexpr int kMaxPeriod = 0xd60;        // [0xb57] = 856 x 4, set by 0x2d15
constexpr int kMinPeriod = 0x1c0;        // [0xb59] = 112 x 4
constexpr int kIrq = 7;                  // ULTRASND=240,3,3,7,7 (the capture's environment)

class Image {
public:
    explicit Image(std::span<const uint8_t> b) : b_(b) {}
    uint32_t u8(uint32_t a) const {
        const size_t o = size_t(kCode32) + a;
        if (o >= b_.size()) throw std::runtime_error("DOPE music: read outside the loader image");
        return b_[o];
    }
    uint32_t u16(uint32_t a) const { return u8(a) | u8(a + 1) << 8; }
    uint32_t u32(uint32_t a) const { return u16(a) | u16(a + 2) << 16; }

private:
    std::span<const uint8_t> b_;
};

// instrument record, 0x46 bytes at 0x11fd + 0x46 (i - 1)
struct Ins {
    int flags = 0;       // +0x31: 1 loop, 2 bidirectional (name starts with "ping")
    uint32_t len = 0;    // +0x32 bytes (0 if <= 2)
    uint32_t ls = 0;     // +0x36 loop start
    uint32_t le = 0;     // +0x3a loop end = min(ls + 2 replen - 2, len - 1) + 2
    int vol = 0;         // +0x3e (not clamped here)
    int ft = 0;          // +0x3f finetune 0..15
    uint32_t dram = 0;   // +0x40 DRAM start
    uint32_t c2spd = 0;  // +0x44 u16 = word [0xb37 + 2 ft]
};

// channel record, 0x3f bytes at 0x1ac0 + 0x3f ch (channel ch plays on GF1 voice ch)
struct Chan {
    uint32_t start = 0;    // +0x04 DRAM start of the sample
    uint32_t lstart = 0;   // +0x08 loop start (DRAM)
    uint32_t end = 0;      // +0x0c end (DRAM): loop end, or start + length
    int pan = 6;           // +0x10 0..15
    int flags = 0;         // +0x11 1 volume, 2 pan, 4 frequency, 8 (re)start the sample
    int ctl = 0;           // +0x12 GF1 voice control: 8 loop, 0x10 bidirectional
    int vol = 0;           // +0x13 0..64
    uint32_t cLs = 0xffff;   // +0x14 loop start last written
    uint32_t cEnd = 0xffff;  // +0x18 end last written
    uint32_t cFc = 0xff;     // +0x1c frequency last written
    int cPan = 7;          // +0x20 pan last written
    int ins = 0;           // +0x21 instrument (1-based; 0-based after an instrument-only cell: notes 8.6)
    uint32_t fc = 0;       // +0x22 u16 GF1 frequency
    uint32_t offset = 0;   // +0x24 sample offset (9xx), cleared on every row
    int eff = 0, par = 0;  // +0x28, +0x29
    uint32_t period = 0;   // +0x2a u16 current period (Amiga period x 4)
    int vibpos = 0;        // +0x2c
    int vdepth = 0;        // +0x2e vibrato depth (low nibble)
    int vspeed = 0;        // +0x2f vibrato speed (high nibble, in place)
    uint32_t base = 0;     // +0x30 u16 the note's period (tone porta target, vibrato centre)
    uint32_t c2spd = 0;    // +0x32
    uint32_t arp[3] = {};  // +0x34, +0x36, +0x38
    int arpcnt = 0;        // +0x3a 0..2, advanced on every tick that is not a row tick
    int note = 0;          // +0x3b octave << 4 | note 1..12
    int pspeed = 0;        // +0x3c tone porta speed
    int tickcnt = 0;       // +0x3d ticks since the row
};

// one event of a channel's track (5 bytes): note, instrument, volume (0xff: none), effect, parameter
struct Event {
    uint8_t note = 0, ins = 0, vol = 0xff, eff = 0, par = 0;
};

class Player {
public:
    Player(std::span<const uint8_t> mod, const Image& img, gus::Card& card, uint32_t eax, uint32_t ebx)
        : m_(mod), img_(img), c_(card), ebx_(ebx) {
        for (uint32_t i = 0; i < 16; i++) c2spd_[i] = img.u16(kC2spd + 2 * i);
        for (uint32_t i = 0; i < 96; i++) periods_[i] = img.u16(kPeriods + 2 * i);
        for (uint32_t i = 0; i < 65; i++) volt_[i] = img.u16(kVolTab + 2 * i);
        for (uint32_t i = 0; i < 19; i++) vdiv_[i] = img.u16(kRates + 2 * i);
        // 4xx reads 0x22a0 + (pos >> 2), pos 0..255: the sine, then the dword [0x22c0] (EAX when the tick was called:
        // AL = 8 from the IRQ handler, the rest from the interrupted code), then the first 28 code bytes of 0x22c4
        for (uint32_t i = 0; i < 64; i++) vibtab_[i] = int(img.u8(kSine + i));
        for (int i = 0; i < 4; i++) vibtab_[32 + i] = int(eax >> (8 * i) & 0xff);
        master_ = img.u32(kMaster);
        count_ = int(img.u8(kCount0));
        for (uint32_t c : c2spd_)
            if (!c) throw std::runtime_error("DOPE music: loader image without C-2 rates");
        if (!volt_[64] || !vdiv_[14] || master_ > 0x40)
            throw std::runtime_error("DOPE music: loader image without the GUS tables");
    }

    std::vector<Position> positions;  // after each timer IRQ
    uint32_t dramEnd = 0;
    int stale = 0, notes = 0;
    bool started() const { return play_; }
    Position position() const { return {order_, row_, counter_}; }

    // ---- start-up ----

    // 0x45b4 (after 0x490a has read ULTRASND=240,3,3,7,7: port [0x3f1c] = 0x240, IRQ [0x3f26] = 7)
    void gusInit() {
        c_.outb(0x000, 0x0b);  // mix control: line in off, output off
        c_.w8(0x4c, 0);        // reset (+ delay: 7 reads of port 0x300)
        c_.w8(0x4c, 1);
        c_.w8(0x41, 0);
        c_.w8(0x45, 0);
        c_.w8(0x49, 0);
        c_.w8(0x0e, 0xdf);  // 32 voices for now
        for (int v = 31; v >= 0; v--) {
            for (int k = 0; k < 2; k++) {  // each voice twice (bl ^= 0x80)
                c_.outb(0x102, v);
                c_.w8(0x00, 3);
                c_.w8(0x0d, 3);
                c_.w16(0x09, int(volt_[0]));  // 0x4e20
                c_.w8(0x06, 0x3f);            // ramp rate: 63 steps per frame, never changed
                c_.w8(0x0c, 7);
            }
        }
        c_.outb(0x00f, 5);
        c_.outb(0x000, 8);
        c_.outb(0x00b, 0);
        c_.outb(0x00f, 0);
        c_.outb(0x000, 0x4b);
        c_.outb(0x00b, int(img_.u8(kLatch + kIrq)));  // 0x44: IRQ 7, the same for both GF1 IRQs
        c_.outb(0x000, 9);
        // DRAM size probe: increment the byte at 0x3ffff, 0x7ffff, 0xbffff, 0xfffff and read it back
        c_.w16(0x43, 0xffff);
        c_.outb(0x103, 0x44);
        for (int cl = 3; cl < 16; cl += 4) {
            c_.outb(0x105, cl);
            const int b = (c_.inb(0x107) + 1) & 0xff;
            c_.outb(0x107, b);
            if (c_.inb(0x107) != b) break;  // [0x3f27]++ otherwise (written only)
        }
        c_.w8(0x4c, 7);     // run, DAC on, GF1 IRQs on
        c_.w8(0x47, 0xff);  // timer 2 count 0xff (320 us)
        c_.w8(0x45, 8);     // timer 2 IRQ on
        c_.outb(0x008, 4);
        c_.outb(0x009, 2);  // start timer 2
        // IRQ 7 -> handler 0x4040 (the word at 0x404b is patched with `mov eax, eax`), then acknowledge
        c_.outb(0x103, 0x8f);
        c_.inb(0x105);
        play_ = false;
    }

    // 0x2d15: header, instruments, orders, patterns -> per-channel tracks of 5-byte events, samples -> DRAM
    void load() {
        need(0x43c);
        const uint8_t* tag = m_.data() + 0x438;
        int n;
        if (tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N') n = int(tag[0]) - 0x30;
        else if (tag[0] == 'M' && tag[1] == '.' && tag[2] == 'K' && tag[3] == '.') n = 4;
        else if (tag[2] == 'C' && tag[3] == 'H') n = ((int(tag[0]) - 0x30) * 10 + ((int(tag[1]) - 0x30) & 0xf)) & 0xff;
        else n = 4;
        if (n < 1 || n > 32) throw std::runtime_error("DOPE music: unexpected channel count");
        nch_ = n;  // [0xc7b] = 28
        for (int v = 0; v < 32; v++) {  // 0x4587 with the table 0xd7d
            c_.outb(0x102, v);
            c_.w8(0x0c, int(img_.u8(kPanTab + uint32_t(v))));
        }
        speed_ = 6;   // [0xc76]
        bpm_ = 125;   // [0xc77]
        uint32_t o = 20;
        for (int i = 0; i < 31; i++, o += 30) {
            Ins& I = ins_[i];
            const uint32_t ln = be16(o + 22), ft = m_[o + 24], vol = m_[o + 25], ls = be16(o + 26), rl = be16(o + 28);
            I.len = ln * 2 > 2 ? ln * 2 : 0;
            I.ft = int(ft & 0xf);
            I.c2spd = c2spd_[I.ft];
            I.vol = int(vol);
            I.ls = ls * 2;
            const uint32_t rr = (rl - 1) * 2;  // dec ebx; shl ebx, 1
            uint32_t le = I.ls + rr;
            if (le > I.len) le = I.len - 1;
            I.le = le + 2;
            if (rr < 0x80000000u && rr > 2) {  // jle (signed): loops of 4 bytes or less are ignored
                I.flags = 1;
                if (m_[o] == 'p' && m_[o + 1] == 'i' && m_[o + 2] == 'n' && m_[o + 3] == 'g') I.flags |= 2;
            }
            I.dram = img_.u32(kInsTab + 0x46 * uint32_t(i) + 0x40);
        }
        songlen_ = m_[0x3b6];  // [0xc7a] (the restart byte is ignored)
        for (int i = 0; i < 256; i++) orders_[i] = i < 128 ? m_[0x3b8 + i] : 0;  // 0xc7d: 128 + 128 zero bytes
        npat_ = *std::max_element(orders_, orders_ + 128) + 1;                    // [0xc78]: over all 128 entries
        // per channel: npat x 64 events (note, instrument, volume, effect, parameter); default 0 0 ff 0 0
        tracks_.assign(size_t(n) * size_t(npat_) * 64, Event{});
        o = 0x43c;
        need(o + uint32_t(npat_) * uint32_t(n) * 256);
        for (int p = 0; p < npat_; p++) {
            for (int ch = 0; ch < n; ch++) {
                for (int row = 0; row < 64; row++) {
                    const uint8_t* b = m_.data() + o + (uint32_t(row) * uint32_t(n) + uint32_t(ch)) * 4;
                    Event& ev = track(ch, p * 64 + row);
                    const uint32_t per = uint32_t(b[0] << 8 | b[1]) & 0xfff;
                    int note = 0;
                    if (per) {
                        int k = 0x60;  // unmatched: 0x60 -> note 0x81 (period 906 in DOPE.MOD)
                        for (int j = 0; j < 0x60; j++)
                            if (periods_[j] == per) {
                                k = j;
                                break;
                            }
                        note = ((k / 12) << 4) + k % 12 + 1;
                    }
                    ev.note = uint8_t(note);
                    ev.ins = uint8_t((b[0] & 0xf0) | b[2] >> 4);
                    if ((b[2] & 0xf) == 0xc) {
                        ev.vol = b[3];  // Cxx becomes the volume byte
                    } else {
                        ev.eff = b[2] & 0xf;
                        ev.par = b[3];
                    }
                }
            }
            o += uint32_t(n) * 256;
        }
        // samples: one after the other from DRAM 0, by PIO (0x4450); byte 0 (and byte 1 of one-shots) zeroed
        uint32_t a = 0;
        std::vector<uint8_t> buf;
        for (Ins& I : ins_) {
            if (!I.len) continue;
            if (I.len > 0x10000) throw std::runtime_error("DOPE music: a sample over 64 KB (0x2d15 exits)");
            need(o + I.len);
            buf.assign(m_.begin() + o, m_.begin() + o + I.len);
            o += I.len;
            I.dram = a;
            buf[0] = 0;
            if (!(I.flags & 1)) buf[1] = 0;
            if (a + I.len > (1u << 20)) throw std::runtime_error("DOPE music: samples over 1 MB");
            c_.upload(a, buf.data(), buf.size());
            a += I.len;
        }
        dramEnd = a;
        regSel_ = 0x43;
    }

    // 0x2c17: reset the 32 channel records and the position, then 0x4369
    void start() {
        for (Chan& c : ch_) c = Chan{};
        counter_ = 1;     // [0x1a9b]
        row_ = 0x3f;      // [0x1a9c]
        roww_ = 0;        // [0x1a9d]
        brk_ = 0x3f;      // [0x1aa7]
        nextrow_ = 0;     // [0x1aa8]
        patdelay_ = 0;    // [0x1aa9]
        ordidx_ = 0xff;   // [0x1a9f] (0 - 1)
        order_ = 0;       // [0x1aa0]
        pattern_ = orders_[0];
        loopcnt_ = 0;     // [0x1aab]
        looprow_ = 0;     // [0x1aaa]
        // 0x4369
        const int n = std::max(nch_, 14);
        div_ = vdiv_[n - 14];  // [0x3f18] = 22050
        c_.w8(0x0e, (n - 1) | 0xc0);  // 0xdb: 28 voices
        play_ = true;                 // [0x3f0c] = 0x41e9
    }

    // 0x2cff: 0x43d6 (stop every voice), 0x4545 (IRQ vector back, GF1 reset, output off)
    void stop() {
        play_ = false;
        for (int v = 31; v >= 0; v--) {
            c_.outb(0x102, v);
            for (int k = 0; k < 2; k++) {
                c_.w8(0x00, 3);
                c_.w8(0x0d, 3);
                c_.w8(0x09, 0);
            }
        }
        c_.w8(0x4c, 0);
        c_.outb(0x000, 0x0b);
    }

    // ---- the interrupt ----

    // 0x4040: the PIC EOI, then one source: the volume ramp first, else timer 2
    void irq() {
        const int st = c_.inb(0x006);
        if (st & 0x40) {
            rampIrq();
        } else if (st & 8) {
            c_.w8(0x47, count_);  // the count computed after the previous tick
            c_.w8(0x45, 0);
            c_.w8(0x45, 8);
            if (play_) {  // 0x41e9
                tick();
                tempo();
                noteOff();
            }
            positions.push_back(position());
        }
        c_.outb(0x103, regSel_);
    }

private:
    uint32_t be16(uint32_t a) const {
        need(a + 2);
        return uint32_t(m_[a] << 8 | m_[a + 1]);
    }
    void need(size_t end) const {
        if (end > m_.size()) throw std::runtime_error("DOPE music: module too short");
    }
    Event& track(int ch, int i) { return tracks_[(size_t(ch) * size_t(npat_)) * 64 + size_t(i)]; }
    const Ins& ins(uint32_t i) const {
        if (i >= 31) throw std::runtime_error("DOPE music: instrument number outside the table");
        return ins_[i];
    }

    // 0x41e9 after the tick: BPM -> timer-2 count. n = 1000000 / (32 BPM) in 80 us units, the GUS gets n >> 2 units
    // of 320 us; the remainder -(n & 3) goes to [0x3f29] and is never read
    void tempo() {
        const uint32_t n = kTimerUs / uint32_t(bpm_ * 32);
        count_ = int((0u - (n >> 2)) & 0xff);
    }

    // 0x421a: every channel that (re)starts its sample ramps its voice down first, with the ramp IRQ on
    void noteOff() {
        for (int v = 0; v < 32; v++) {
            done_[v] = 1;
            if (ch_[v].flags & 8) {
                c_.outb(0x102, v);
                done_[v] = 0;
                c_.outb(0x103, 0x89);
                const int bx = c_.inw(0x104);  // the voice's current volume
                c_.w8(0x07, 0x40);
                c_.w8(0x08, bx >> 8);
                c_.w8(0x0d, 0x60);  // down to 0x400, IRQ at the end
            }
        }
        if (allDone()) update();
    }

    bool allDone() const {
        for (int d : done_)
            if (!d) return false;
        return true;
    }

    // 0x40d8: for each voice whose ramp has ended: stop it, program the new sample, start it
    void rampIrq() {
        c_.outb(0x103, 0x8f);
        int al = c_.inb(0x105);
        uint32_t bx = ebx_ & 0xffff;
        for (;;) {
            const int v = al & 0x1f;
            c_.outb(0x102, v);
            if (done_[v]) stale++;
            done_[v] = 1;
            c_.w8(0x0d, 3);
            c_.w8(0x00, 3);
            Chan& ch = ch_[v];
            // addr(hi, lo, a): shrd bx, si, 7; reg hi = address bits 7..19; reg lo (8-bit write, high byte) = address
            // bits 0..6 << 1 and, in bit 0 (the fraction's 1/2), bit 15 of the previous BX
            auto addr = [&](int rh, int rl, uint32_t a) {
                bx = ((bx >> 7) | (a << 9)) & 0xffff;
                c_.w16(rh, int((a >> 7) & 0xffff));
                c_.w8(rl, int(bx >> 8));
            };
            if (ch.lstart != ch.cLs) {
                ch.cLs = ch.lstart;
                addr(2, 3, ch.lstart);
            }
            if (ch.end != ch.cEnd) {
                ch.cEnd = ch.end;
                addr(4, 5, ch.end);
            }
            addr(0x0a, 0x0b, ch.start + ch.offset);
            c_.w8(0x00, ch.ctl);  // start: 0 one-shot, 8 loop, 0x18 bidirectional loop
            c_.outb(0x103, 0x8f);
            al = c_.inb(0x105);
            if (al & 0x40) break;  // no more voices with a ramp IRQ pending
        }
        if (allDone()) update();
    }

    // 0x4299: frequency, pan and volume of every channel with pending changes
    void update() {
        for (int v = 0; v < 32; v++) {
            Chan& ch = ch_[v];
            c_.outb(0x102, v);
            int cl = ch.flags;
            ch.flags = 0;
            if (!cl) continue;
            if (cl & 8) cl = 7;
            if (cl & 4) {
                if (ch.period) {  // two truncating divisions
                    const uint64_t hz = kAmiga4 / ch.period;
                    ch.fc = uint32_t((hz * 0x400) / uint64_t(div_)) & 0xffff;
                }
                if (ch.fc != ch.cFc) {
                    ch.cFc = ch.fc;
                    c_.w16(0x01, int(ch.fc));
                }
            }
            if ((cl & 2) && ch.pan != ch.cPan) {
                ch.cPan = ch.pan;
                c_.w8(0x0c, ch.pan);
            }
            if (master_ != mvolCache_ || (cl & 1)) {  // [0x3f14]: true once, for the first channel
                mvolCache_ = master_;
                c_.w16(0x09, int(volt_[(uint32_t(ch.vol) * master_) >> 6]));
            }
        }
    }

    // ---- the tick ----

    // 0x22c4
    void tick() {
        counter_ = (counter_ - 1) & 0xff;
        if (counter_) return tickEffects();
        counter_ = speed_;
        if (patdelay_) {
            patdelay_--;
            return tickEffects();
        }
        if (row_ == brk_) {
            row_ = nextrow_;
            roww_ = row_;
            ordidx_ = (ordidx_ + 1) & 0xff;
            if (ordidx_ == songlen_) ordidx_ = 0;
            order_ = ordidx_;
            pattern_ = orders_[ordidx_];
        } else {
            row_ = (row_ + 1) & 0xff;
        }
        nextrow_ = 0;
        brk_ = 0x3f;
        if (row_ >= 64 || pattern_ >= npat_) throw std::runtime_error("DOPE music: row outside the pattern");
        for (int k = 0; k < nch_; k++) {
            roww_ = row_;
            rowChannel(ch_[k], track(k, pattern_ * 64 + row_));
        }
    }

    // word [0xb8b + 2 n]: the period table 0xb5b from index 24 on (n is a signed 32-bit index: the arpeggio of a
    // channel without a note reads before it)
    uint32_t oct1(uint32_t n) const { return img_.u16(kOct1 + 2 * n); }

    // 0x23e8 / 0x2b15: (period of the note in the octave at 0xb8b x 0x20ab0 >> octave) / C-2 rate
    uint32_t notePeriod(int note, uint32_t c2spd) const {
        const uint32_t p = oct1(uint32_t((note & 0xf) - 1)) * kNoteMul;
        return ((p >> ((note >> 4) & 31)) / c2spd) & 0xffff;
    }

    // 0x2379
    void rowChannel(Chan& C, const Event& ev) {
        C.offset = 0;
        C.eff = ev.eff;
        C.par = ev.par;
        if (ev.note) {
            int noins = 0;
            int bl = ev.ins;
            if (bl == 0) {
                bl = C.ins;
                if (bl == 0) return rowNoteless(C, ev);
                noins = 1;
            }
            C.ins = bl;
            const Ins& I = ins(uint32_t(bl - 1) & 0xff);
            C.note = ev.note;
            C.c2spd = I.c2spd;
            C.base = notePeriod(ev.note, I.c2spd);
            if (ev.eff == 3 || ev.eff == 5) return rowNoteless(C, ev);  // tone portamento: the note is the target
            notes++;
            C.period = C.base;
            C.flags = 0xf;
            C.ctl = 0;
            C.vibpos = 0;
            C.offset = 0;
            C.start = I.dram;
            C.end = I.dram + I.len;
            if (I.flags & 1) {
                C.lstart = I.ls + I.dram;
                C.end = I.le + I.dram;
                C.ctl |= 8;
                if (I.flags & 2) C.ctl |= 0x10;
            }
            if (noins != 1) {
                C.vol = std::min(I.vol, 0x40);
                C.flags |= 1;
            }
            if (ev.eff == 0xe && ev.par >> 4 == 0xd) C.flags = 0;
            return rowVolume(C, ev);
        }
        rowNoteless(C, ev);
    }

    // 0x24a4: an instrument number without a note (or with tone porta) resets the volume; the stored instrument
    // number becomes 0-based here
    void rowNoteless(Chan& C, const Event& ev) {
        if (ev.ins) {
            const int b = (ev.ins - 1) & 0xff;
            C.ins = b;
            C.vol = std::min(ins(uint32_t(b)).vol, 0x40);
            C.flags |= 1;
        }
        rowVolume(C, ev);
    }

    // 0x24cc: the volume byte (Cxx), then the row effect (tables 0x2579 / 0x25c1)
    void rowVolume(Chan& C, const Event& ev) {
        if (ev.vol != 0xff) {
            C.flags |= 1;
            C.vol = std::min(int(ev.vol), 0x40);
        }
        C.tickcnt = 0;
        const int eff = C.eff;
        int par = C.par;
        switch (eff) {
        case 0x0:  // 0x2ba3 arpeggio: base, +x, +x
            if (par) {
                C.arp[0] = C.period;
                int cl = C.note >> 4;
                uint32_t n = uint32_t((C.note & 0xf) - 1) + uint32_t(par >> 4);
                if (n > 11) {
                    cl++;
                    n -= 11;  // (11, not 12)
                    if (n > 11) {
                        cl++;
                        n -= 11;
                    }
                }
                const uint32_t p = ((oct1(n) << 4) >> (cl & 31)) & 0xffff;
                C.arp[1] = C.arp[2] = p;
            }
            break;
        case 0x3:  // 0x28e8
            if (!par) par = C.pspeed;
            C.pspeed = C.par = par;
            break;
        case 0x4: {  // 0x2854
            int d = par & 0xf, s = par & 0xf0;
            if (!d) d = C.vdepth;
            if (!s) s = C.vspeed;
            C.vdepth = d;
            C.vspeed = s;
            C.par = d | s;
            break;
        }
        case 0x8:  // 0x2b06
            C.pan = par >> 4;
            C.flags |= 2;
            break;
        case 0x9: {  // 0x270a
            uint32_t off = uint32_t(par) << 8;
            if (off) {
                const uint32_t lim = C.end - C.start;
                if (off >= lim) off = lim - 1;
                C.offset = off;
            }
            C.flags |= 8;  // (re)starts the sample, with or without a note
            break;
        }
        case 0xb:  // 0x2a57
            brk_ = roww_ & 0xff;
            ordidx_ = (par - 1) & 0xff;
            break;
        case 0xd:  // 0x26d7
            if (par >= 0x64) par = 0x63;
            nextrow_ = ((par & 0xf) + (par >> 4) * 2 + (par >> 4) * 8) & 0xff;
            brk_ = roww_ & 0xff;
            break;
        case 0xf:  // 0x2689
            if (par == 0) {
                // [0x1aa1] playing = 0: nothing reads it
            } else if (par > 0x1f) {
                bpm_ = par;
            } else {
                speed_ = counter_ = par;
            }
            break;
        case 0xe: {
            const int x = par >> 4, y = par & 0xf;
            switch (x) {
            case 0x1: {  // 0x29db
                const uint32_t p = (C.period - uint32_t(y) * 4) & 0xffff;
                C.period = p < uint32_t(kMinPeriod) ? uint32_t(kMinPeriod) : p;
                C.flags |= 4;
                break;
            }
            case 0x2: {  // 0x2a05
                const uint32_t p = (C.period + uint32_t(y) * 4) & 0xffff;
                C.period = p > uint32_t(kMaxPeriod) ? uint32_t(kMaxPeriod) : p;
                C.flags |= 4;
                break;
            }
            case 0x5:  // 0x2b15
                C.base = C.period = notePeriod(C.note, c2spd_[y]);
                break;
            case 0x6:  // 0x2b51
                if (y == 0) {
                    looprow_ = roww_ & 0xff;
                } else {
                    if (loopcnt_ == 0) {
                        loopcnt_ = y;
                    } else {
                        loopcnt_--;
                        if (loopcnt_ == 0) break;
                    }
                    brk_ = roww_ & 0xff;
                    nextrow_ = looprow_;
                    ordidx_ = (ordidx_ - 1) & 0xff;
                }
                break;
            case 0x8:  // 0x2b06 with AH = y: pan = y >> 4 = 0
                C.pan = y >> 4;
                C.flags |= 2;
                break;
            case 0xa:  // 0x2a2f
                C.vol = std::min((C.vol + y) & 0xff, 0x40);
                C.flags |= 1;
                break;
            case 0xb:  // 0x2a45
                C.vol = C.vol >= y ? C.vol - y : 0;
                C.flags |= 1;
                break;
            case 0xd:  // 0x2a96: the note starts now anyway
                C.flags |= 0xf;
                break;
            case 0xe:  // 0x2a9f
                patdelay_ = y;
                break;
            default: break;  // E0, E3, E4, E7, E9, EC, EF: nothing on the row
            }
            break;
        }
        default: break;  // 1, 2, 5, 6, 7, A, C: nothing on the row
        }
    }

    // vol - ah (8 bit): a negative result -> 0, >= 64 -> 64
    static void volslide(Chan& C, int ah) {
        int al = (C.vol - ah) & 0xff;
        if (al & 0x80) al = 0;
        C.vol = std::min(al, 0x40);
    }

    // 0x251f: every channel on the ticks between rows (tables 0x2601 / 0x2649)
    void tickEffects() {
        for (int i = 0; i < nch_; i++) {
            Chan& C = ch_[i];
            C.tickcnt = (C.tickcnt + 1) & 0xff;
            if (++C.arpcnt >= 3) C.arpcnt = 0;
            const int eff = C.eff, par = C.par;
            switch (eff) {
            case 0x0:  // 0x2bf8
                if (par) {
                    C.period = C.arp[C.arpcnt];
                    C.flags = 4;  // (mov: replaces the flags)
                }
                break;
            case 0x1:  // 0x272d
                if (C.period) {
                    const uint32_t p = (C.period - (uint32_t(par) << 2)) & 0xffff;
                    C.period = int16_t(p) < kMinPeriod ? uint32_t(kMinPeriod) : p;
                    C.flags |= 4;
                }
                break;
            case 0x2:  // 0x2793
                if (C.period) {
                    const uint32_t p = (C.period + (uint32_t(par) << 2)) & 0xffff;
                    C.period = p > uint32_t(kMaxPeriod) ? uint32_t(kMaxPeriod) : p;
                    C.flags |= 4;
                }
                break;
            case 0x3:  // 0x28a5
            case 0x5: {  // 0x28fb
                uint32_t ax;
                if (eff == 5) {
                    volslide(C, ((par & 0xf) - (par >> 4)) & 0xff);
                    ax = (uint32_t(C.pspeed) << 2) & 0xffff;
                } else {
                    ax = (uint32_t(par) << 2) & 0xffff;
                }
                const uint32_t dx = C.base;
                uint32_t bx = C.period;
                if (bx < dx) {
                    bx = (bx + ax) & 0xffff;
                    if (bx > dx) C.period = dx;
                }
                if (bx > dx) {
                    const bool borrow = bx < ax;
                    bx = (bx - ax) & 0xffff;
                    if (borrow) bx = eff == 3 ? dx : 0;
                    if (bx < dx) bx = dx;
                }
                C.period = bx;
                C.flags |= eff == 3 ? 4 : 5;
                break;
            }
            case 0x4:  // 0x27f9
            case 0x6: {  // 0x295b
                int p;
                if (eff == 6) {
                    volslide(C, ((par & 0xf) - (par >> 4)) & 0xff);
                    p = C.vdepth | C.vspeed;
                } else {
                    p = par;
                }
                C.vibpos = (C.vibpos + ((p & 0xf0) >> 2)) & 0xff;
                // 4xx: 64 entries (no `and 0x1f`: the positive half reads past the sine, notes 8.6); 6xy: the sine
                const int m = eff == 4 ? vibtab_[C.vibpos >> 2] : vibtab_[(C.vibpos >> 2) & 0x1f];
                int ax = (m * (p & 0xf)) >> 5;
                if (!(C.vibpos & 0x80)) ax = -ax;
                uint32_t a = uint32_t(ax + int(C.base)) & 0xffff;
                if (eff == 4) {
                    if (int16_t(a) < kMinPeriod) a = kMinPeriod;
                } else if (a < uint32_t(kMinPeriod)) {
                    a = kMinPeriod;
                }
                if (a > uint32_t(kMaxPeriod)) a = kMaxPeriod;
                C.period = a;
                C.flags |= eff == 4 ? 4 : 5;
                break;
            }
            case 0xa: {  // 0x287a: up x, else down y
                const int x = par >> 4, y = par & 0xf;
                volslide(C, x ? (-x & 0xff) : y);
                C.flags |= 1;
                break;
            }
            case 0xe: {
                const int x = par >> 4, y = par & 0xf;
                if (x == 9 && y == C.tickcnt) {  // 0x2aaa retrigger
                    C.flags |= 0xd;
                    C.tickcnt = 0;
                    const Ins& I = ins(uint32_t(C.ins - 1) & 0xff);
                    C.start = I.dram + C.offset;
                    C.end = C.start + I.len;
                    if (I.flags & 1) {
                        C.lstart = I.ls + I.dram;
                        C.end = I.le + I.dram;
                        C.ctl |= 8;
                    }
                    C.vol = std::min(I.vol, 0x40);
                } else if (x == 0xc && y == C.tickcnt) {  // 0x2a6e note cut
                    C.vol = 0;
                    C.flags |= 1;
                } else if (x == 0xd && y == C.tickcnt) {  // 0x2a84 note delay
                    C.flags |= 0xf;
                }
                break;
            }
            default: break;  // 7 (tremolo), 8, 9, B, C, D, F: nothing on the ticks
            }
        }
    }

    std::span<const uint8_t> m_;
    const Image& img_;
    gus::Card& c_;
    uint32_t ebx_;
    uint32_t c2spd_[16] = {}, periods_[96] = {}, volt_[65] = {}, vdiv_[19] = {};
    int vibtab_[64] = {};
    uint32_t master_ = 0, mvolCache_ = 0;  // [0x1ab6], [0x3f14]
    int count_ = 0;                        // [0x3f2a]
    bool play_ = false;                    // [0x3f0c] = 0x41e9
    int done_[32] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    int regSel_ = 0x43;                    // [0x3f61]
    int nch_ = 4, speed_ = 6, bpm_ = 125, songlen_ = 0, npat_ = 0;
    int orders_[256] = {};
    Ins ins_[31];
    std::vector<Event> tracks_;
    Chan ch_[32];
    int order_ = 0, row_ = 0, counter_ = 1;  // [0x1aa0], [0x1a9c], [0x1a9b] in the image
    int roww_ = 0, brk_ = 0x3f, nextrow_ = 0, patdelay_ = 0, ordidx_ = 0, pattern_ = 0, loopcnt_ = 0, looprow_ = 0;
    uint32_t div_ = 22050;  // [0x3f18]
};

}  // namespace

void run(std::span<const uint8_t> mod, std::span<const uint8_t> loader, gus::Card& card, const Options& opt, Run& out) {
    const Image img(loader);
    Player pl(mod, img, card, opt.eax, opt.ebx);
    out = Run{};
    card.tick = 0;
    pl.gusInit();  // at card time 0: timer 2 starts with count 0xff
    pl.load();     // (instant here; with interrupts off on the original)
    out.dramEnd = pl.dramEnd;
    int ntimer = 0;
    size_t i = 0;
    for (;;) {
        double td, tlog, te;
        char kind = 0;
        if (opt.capture) {
            // the capture's service time, or the event itself when the log has it to the microsecond (the
            // emulator raises a ramp end 0.1 us after it, dopeplay.py's check)
            if (i >= opt.capture->size()) break;
            const CaptureIrq& c = (*opt.capture)[i];
            const gus::Card::Event ev = card.nextEvent();
            if (ev.t < 0) throw std::runtime_error("DOPE music: an interrupt of the capture without an event");
            te = ev.t;
            tlog = c.time;
            td = tlog - te > 1.5e-6 ? tlog : te + (ev.voice >= 0 ? 1e-7 : 0.0);
            card.events(td, c.kind == 'r' && opt.captureOrder ? &c.order : nullptr);
            kind = c.kind;
        } else if (!card.irq()) {
            const gus::Card::Event ev = card.nextEvent();
            if (ev.t < 0) throw std::runtime_error("DOPE music: the GUS has no event left");
            if (ntimer >= opt.ticks && ev.voice < 0) break;
            te = ev.t;
            td = te + (ev.voice >= 0 ? 1e-7 : 0.0);
            for (const Hold& h : opt.holds)
                if (td >= h.from && td < h.to) td = h.to;
            card.events(td);
            tlog = td;
        } else {  // a second source still pending after a handler (after a hold): served at once
            if (ntimer >= opt.ticks && !(card.status() & 0x40)) break;
            te = td = tlog = card.now();
        }
        if (!card.irq()) throw std::runtime_error("DOPE music: no interrupt pending");
        const char got = (card.status() & 0x40) ? 'r' : 't';
        if (kind && got != kind) throw std::runtime_error("DOPE music: the capture served another interrupt kind");
        kind = got;
        if (kind == 't') {
            ntimer++;
            card.tick = ntimer;
            if (!opt.capture) te = card.timerRaised();
            out.timerEvent.push_back(card.timerRaised());
            out.timerServed.push_back(td);
        }
        if (pl.started()) {
            if (td - te > 1.5e-6) {
                out.late++;
                out.maxLate = std::max(out.maxLate, td - te);
            }
            if (te - td > 1.5e-6) out.early++;
        }
        card.logIrq(kind, tlog);
        pl.irq();
        out.irqs++;
        if (kind == 't' && ntimer == kStartAfter) {
            pl.start();  // (main, between two timer interrupts)
            pl.positions.back() = pl.position();
            card.lost = 0;
        }
        i++;
    }
    // the stop: before the next timer overflow (the card runs up to it first: nothing after it is heard)
    out.stopTime = card.timerNext();
    card.events(out.stopTime);
    pl.stop();
    out.pos = std::move(pl.positions);
    out.stale = pl.stale;
    out.notes = pl.notes;
}

}  // namespace modplay
