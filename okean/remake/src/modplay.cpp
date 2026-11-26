// The Sea Robot of Love's ProTracker player with its GUS driver (code32 0x10414..0x14bd3): GUS init 0x14935, MOD load
// 0x13f28 with the pan table 0x1490c and the PIO upload 0x14788, start 0x13eb1 / 0x14714, stop 0x13f18 / 0x1475b,
// shutdown 0x148ca, and the GUS IRQ 0x1443c that plays the song (timer path 0x14591: the tick 0x1343c, tempo -> timer
// count, the voice loop that ramps each new note down and waits for the nested ramp IRQ, the voice update 0x14663;
// ramp path 0x144b2). okean/re/notes-music.md sections 3-9, a routine-by-routine port that mirrors
// okean/re/models/music/okeanplay.py (started from dope/remake/src/modplay.cpp, the same player family). The player
// writes and reads the GUS through gus::Card; the schedule of interrupts (a capture's service times, or the tick times
// of the remake's schedule with every ramp IRQ at its ramp end) is run(). See modplay.h.
#include "modplay.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "exe.h"

namespace modplay {
namespace {

constexpr uint32_t kModule = 0x0ae0;     // the module, a ProTracker MOD (8CHN), read in place
constexpr uint32_t kOct = 0x10390;       // 12 u16: periods of the base octave (note -> period)
constexpr uint32_t kC2spd = 0x103a8;     // 16 u16: C-2 rates per finetune 0..7, -8..-1
constexpr uint32_t kPer36 = 0x103cc;     // 36 u16: ProTracker periods 856 .. 113 (period -> note, at load)
constexpr uint32_t kAmiga = 0x10414;     // u32: 4 x 3,546,836 Hz (PAL Amiga clock x 4; also stored by the load)
constexpr uint32_t kPanTab = 0x10575;    // 32 u8: GF1 pan per voice, written by the load
constexpr uint32_t kInsTab = 0x10c50;    // 31 instrument records of 0x50 bytes (+0x40: DRAM start)
constexpr uint32_t kCounter = 0x12b71;   // u8: tick countdown, row 0x12b72, order index 0x12b75 (image values)
constexpr uint32_t kRow = 0x12b72;
constexpr uint32_t kOrder = 0x12b75;
constexpr uint32_t kArpClamp = 0x12b82;  // u8: 1 -> arpeggio notes above octave 5 become B-5
constexpr uint32_t kMaster = 0x12b8c;    // u32: master volume (the end part lowers it)
constexpr uint32_t kChanTab = 0x12b98;   // 16 channel records of 0x40 bytes
constexpr uint32_t kVibBug = 0x2f98;     // 4xx / 6xy read [bx + 0x2f98] (16-bit addressing of 0x12f98): module bytes
constexpr uint32_t kCount0 = 0x1438a;    // u8: timer-2 count written by the init
constexpr uint32_t kLatch = 0x143a6;     // 16 u8: GF1 IRQ latch per IRQ number
constexpr uint32_t kVolTab = 0x143ba;    // 64 u16: GF1 volume (register 9) for vol x master >> 6

constexpr uint32_t kNoteMul = 0x20ab0;   // 0x13585 / 0x13d22 / 0x13e0d / 0x13e79: base-octave period x 16 x 8363
constexpr uint32_t kTimerUs = 1000000;   // 0x145b6
constexpr int kMaxPeriod = 0xd60;        // [0x103c8] = 856 x 4, set by the load
constexpr int kMinPeriod = 0x1c0;        // [0x103ca] = 112 x 4
constexpr int kVoices = 16;              // 0x149da: active voices (38,587.5 frames per second)
constexpr int kIrq = 7;                  // ULTRASND=240,3,3,7,7 (the capture's environment)
constexpr double kLate = 1.5e-6;         // an interrupt this much after its event counts as held

// instrument record, 0x50 bytes at 0x10c50 + 0x50 (i - 1)
struct Ins {
    int flags = 0;       // +0x31: 1 loop
    uint32_t len = 0;    // +0x32 bytes (0 if <= 2)
    uint32_t ls = 0;     // +0x36 loop start
    uint32_t le = 0;     // +0x3a loop end = min(ls + 2 replen - 2, len - 1) + 2
    int vol = 0;         // +0x3e (not clamped here)
    int ft = 0;          // +0x3f finetune 0..15
    uint32_t dram = 0;   // +0x40 DRAM start
    uint32_t c2spd = 0;  // +0x44 u16 = word [0x103a8 + 2 ft]
};

// channel record, 0x40 bytes at 0x12b98 + 0x40 ch (channel ch plays on GF1 voice ch)
struct Chan {
    uint32_t start = 0;    // +0x04 DRAM start of the sample
    uint32_t lstart = 0;   // +0x08 loop start (DRAM)
    uint32_t end = 0;      // +0x0c end (DRAM): loop end, or start + length - 1
    int pan = 0;           // +0x10 0..15 (7 at every note; E8x)
    int flags = 0;         // +0x11 1 volume, 2 pan, 4 frequency, 8 (re)start the sample
    int ctl = 0;           // +0x12 GF1 voice control: 0 one-shot, 8 loop
    int vol = 0;           // +0x13 0..63
    uint32_t cLs = 0;      // +0x14 loop start last written
    uint32_t cEnd = 0;     // +0x18 end last written
    uint32_t cFc = 0;      // +0x1c u16 frequency last written
    int cPan = 0;          // +0x20 pan last written
    int ins = 0;           // +0x21 instrument (1-based; 0-based after an instrument-only cell: notes 9.5)
    uint32_t fc = 0;       // +0x22 u16 GF1 frequency
    uint32_t offset = 0;   // +0x24 sample offset (9xx), cleared on every row
    int eff = 0, par = 0;  // +0x28, +0x29
    uint32_t period = 0;   // +0x2a u16 current period (Amiga period x 4)
    int vibpos = 0;        // +0x2c (low byte)
    int vdepth = 0;        // +0x2e vibrato depth (low nibble)
    int vspeed = 0;        // +0x2f vibrato speed (high nibble, in place)
    uint32_t base = 0;     // +0x30 u16 the note's period (tone porta target, vibrato centre)
    uint32_t c2spd = 0;    // +0x32
    uint32_t arp[3] = {};  // +0x34, +0x36, +0x38
    int arpcnt = 0;        // +0x3a 0..2
    int note = 0;          // +0x3b octave << 4 | note 1..12 (octaves 3..5 for the ProTracker range)
    int pspeed = 0;        // +0x3c tone porta speed
    int tickcnt = 0;       // +0x3d ticks since the row
};

// one event of a channel's track (5 bytes): note, instrument, volume (0xff: none), effect, parameter
struct Event {
    uint8_t note = 0, ins = 0, vol = 0xff, eff = 0, par = 0;
};

class Player {
public:
    Player(const OkeanExe& exe, gus::Card& card) : x_(exe), c_(card) {
        if (exe.end() <= kModule) throw std::runtime_error("music: the program image has no module");
        mod_ = exe.span(kModule, exe.end() - kModule);
        for (uint32_t i = 0; i < 16; i++) c2spd_[i] = exe.u16(kC2spd + 2 * i);
        for (uint32_t i = 0; i < 36; i++) per36_[i] = exe.u16(kPer36 + 2 * i);
        for (uint32_t i = 0; i < 64; i++) volt_[i] = exe.u16(kVolTab + 2 * i);
        for (uint32_t i = 0; i < 32; i++) vib_[i] = exe.u8(kVibBug + i);
        arpclamp_ = exe.u8(kArpClamp);
        amiga_ = exe.u32(kAmiga);
        master0_ = int(exe.u32(kMaster));
        order_ = exe.u8(kOrder);
        row_ = exe.u8(kRow);
        counter_ = exe.u8(kCounter);
        for (uint32_t c : c2spd_)
            if (!c) throw std::runtime_error("music: the program image has no C-2 rates");
        if (!amiga_ || !volt_[63] || master0_ < 0 || master0_ > 64)
            throw std::runtime_error("music: the program image has no player tables");
    }

    std::function<void()> waiter;               // serves the ramp IRQ the timer path waits for (run())
    std::function<int(double)> masterFn;        // [0x12b8c] at card time t, or null
    std::vector<Position> positions;            // after each timer IRQ
    uint32_t dramEnd = 0;
    int notes = 0, ramps = 0;
    bool rowRead = false;                       // the last tick read a row
    bool started() const { return play_; }
    Position position() const { return {order_, row_, counter_}; }

    // ---- start-up ----

    // 0x14935 (after 0x14b4b has read ULTRASND=240,3,3,7,7: port [0x1436c] = 0x240, IRQ [0x14389] = 7)
    void gusInit() {
        c_.outb(0x000, 0x0b);  // mix control: line in off, output off
        c_.w8(0x4c, 0);        // reset (+ delay: 7 reads of port 0x300)
        c_.w8(0x4c, 1);
        c_.w8(0x41, 0);
        c_.w8(0x45, 0);
        c_.w8(0x49, 0);
        c_.w8(0x0e, (kVoices - 1) | 0xc0);  // 16 voices: 0xcf
        for (int v = 31; v >= 0; v--) {
            for (int k = 0; k < 2; k++) {  // each voice twice (bl ^= 0x80)
                c_.outb(0x102, v);
                c_.w16(0x09, 0);
                c_.w8(0x00, 0);     // (0: running, at address 0 with FC 0)
                c_.w8(0x0d, 3);
                c_.w8(0x06, 0x3f);  // ramp rate: 63 steps per frame, never changed
                c_.w8(0x0c, 7);
            }
        }
        c_.outb(0x00f, 5);
        c_.outb(0x000, 8);
        c_.outb(0x00b, 0);
        c_.outb(0x00f, 0);
        c_.outb(0x000, 0x4b);
        c_.outb(0x00b, int(x_.u8(kLatch + kIrq)));  // 0x44: IRQ 7, the same for both GF1 IRQs
        c_.outb(0x000, 9);
        // DRAM size probe: increment the byte at 0x3ffff, 0x7ffff, 0xbffff, 0xfffff and read it back
        c_.w16(0x43, 0xffff);
        c_.outb(0x103, 0x44);
        for (int cl = 3; !(cl & 0x10); cl += 4) {
            c_.outb(0x105, cl);
            const int b = (c_.inb(0x107) + 1) & 0xff;
            c_.outb(0x107, b);
            if (c_.inb(0x107) != b) break;  // [0x14376]++ otherwise (written only)
        }
        c_.w8(0x4c, 7);                       // run, DAC on, GF1 IRQs on
        c_.w8(0x47, int(x_.u8(kCount0)));     // timer 2 count [0x1438a] = 0xc2: 62 x 320 us
        c_.w8(0x45, 8);                       // timer 2 IRQ on
        c_.outb(0x008, 4);
        c_.outb(0x009, 2);                    // start timer 2
        // IRQ 7: handler 0x1443c (the word at 0x14447 is patched with `mov eax, eax`), then acknowledge
        c_.outb(0x103, 0x8f);
        c_.inb(0x105);
        play_ = false;                        // [0x14385] = 0x2a0 (ret)
    }

    // 0x13f28 (ESI = 0xae0): header, instruments, orders, patterns -> per-channel tracks of 5-byte events, samples ->
    // DRAM. Runs with interrupts off.
    void load() {
        need(0x43c);
        const uint8_t* tag = mod_.data() + 0x438;
        int n = 4;  // [0x10473]
        if (tag[0] == '6' && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N') n = 6;
        if (tag[0] == '8' && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N') n = 8;
        nch_ = n;
        for (int v = 0; v < 32; v++) {  // 0x1490c with the table 0x10575
            c_.outb(0x102, v);
            c_.w8(0x0c, int(x_.u8(kPanTab + uint32_t(v))));
        }
        speed_ = 6;   // [0x1046e]
        bpm_ = 125;   // [0x1046f]
        uint32_t o = 20;
        for (int i = 0; i < 31; i++, o += 30) {
            Ins& I = ins_[i];
            const uint32_t ln = be16(o + 22), ft = mod_[o + 24], vol = mod_[o + 25], ls = be16(o + 26),
                           rl = be16(o + 28);
            I.len = ln * 2 > 2 ? ln * 2 : 0;
            I.ft = int(ft & 0xf);
            I.c2spd = c2spd_[I.ft];
            I.vol = int(vol);
            I.ls = ls * 2;
            const uint32_t rr = (rl - 1) * 2;  // dec ebx; shl ebx, 1
            uint32_t le = I.ls + rr;
            if (le > I.len) le = I.len - 1;
            I.le = le + 2;
            if (rr < 0x80000000u && rr > 2) I.flags = 1;  // jle (signed): loops of 4 bytes or less are ignored
            I.dram = x_.u32(kInsTab + 0x50 * uint32_t(i) + 0x40);
        }
        songlen_ = mod_[0x3b6];  // [0x10472] (the restart byte is ignored)
        for (int i = 0; i < 256; i++) orders_[i] = i < 128 ? mod_[0x3b8 + size_t(i)] : 0;  // 0x10475: + 128 zeros
        npat_ = *std::max_element(orders_, orders_ + 128) + 1;  // [0x10470]: over all 128 entries
        // per channel: npat x 64 events (note, instrument, volume, effect, parameter); default 0 0 ff 0 0
        tracks_.assign(size_t(n) * size_t(npat_) * 64, Event{});
        o = 0x43c;
        need(o + uint32_t(npat_) * uint32_t(n) * 256);
        for (int p = 0; p < npat_; p++) {
            for (int ch = 0; ch < n; ch++) {
                for (int row = 0; row < 64; row++) {
                    const uint8_t* b = mod_.data() + o + (uint32_t(row) * uint32_t(n) + uint32_t(ch)) * 4;
                    Event& ev = track(ch, p * 64 + row);
                    const uint32_t per = uint32_t(b[0] << 8 | b[1]) & 0xfff;
                    int note = 0;
                    if (per) {
                        int k = 36;  // (unmatched: 0x61)
                        for (int j = 0; j < 36; j++)
                            if (per36_[j] == per) {
                                k = j;
                                break;
                            }
                        note = ((k / 12 + 3) << 4) + k % 12 + 1;
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
        // samples: one after the other from DRAM 0, by PIO (0x14788), unchanged
        uint32_t a = 0;
        for (Ins& I : ins_) {
            if (!I.len) continue;
            if (I.len > 0x10000) throw std::runtime_error("music: a sample over 64 KB (0x13f28 exits)");
            need(o + I.len);
            I.dram = a;
            if (a + I.len > (1u << 20)) throw std::runtime_error("music: samples over 1 MB");
            c_.upload(a, mod_.data() + o, I.len);
            o += I.len;
            a += I.len;
        }
        dramEnd = a;
        regSel_ = 0x43;
    }

    // 0x13eb1: the channel records as in the image with the volumes 0, the position reset, then 0x14714
    void start() {
        for (uint32_t c = 0; c < 16; c++) {
            const uint32_t r = kChanTab + 0x40 * c;
            Chan& C = ch_[c];
            C = Chan{};
            C.start = x_.u32(r + 0x04);
            C.lstart = x_.u32(r + 0x08);
            C.end = x_.u32(r + 0x0c);
            C.pan = x_.u8(r + 0x10);
            C.flags = x_.u8(r + 0x11);
            C.ctl = x_.u8(r + 0x12);
            C.vol = 0;  // the start resets the volumes
            C.cLs = x_.u32(r + 0x14);
            C.cEnd = x_.u32(r + 0x18);
            C.cFc = x_.u16(r + 0x1c);
            C.cPan = x_.u8(r + 0x20);
            C.ins = x_.u8(r + 0x21);
            C.fc = x_.u16(r + 0x22);
            C.offset = x_.u32(r + 0x24);
            C.eff = x_.u8(r + 0x28);
            C.par = x_.u8(r + 0x29);
            C.period = x_.u16(r + 0x2a);
            C.vibpos = x_.u8(r + 0x2c);
            C.vdepth = x_.u8(r + 0x2e);
            C.vspeed = x_.u8(r + 0x2f);
            C.base = x_.u16(r + 0x30);
            C.c2spd = x_.u16(r + 0x32);
            for (uint32_t k = 0; k < 3; k++) C.arp[k] = x_.u16(r + 0x34 + 2 * k);
            C.arpcnt = x_.u8(r + 0x3a);
            C.note = x_.u8(r + 0x3b);
            C.pspeed = x_.u8(r + 0x3c);
            C.tickcnt = x_.u8(r + 0x3d);
        }
        counter_ = 1;                  // [0x12b71]
        roww_ = 0;                     // [0x12b73]
        row_ = 0x3f;                   // [0x12b72]
        brk_ = 0x3f;                   // [0x12b7d]
        nextrow_ = 0;                  // [0x12b7e]
        patdelay_ = 0;                 // [0x12b7f]
        order_ = (order_ - 1) & 0xff;  // [0x12b75]: 0 - 1
        loopcnt_ = 0;                  // [0x12b81]
        looprow_ = 0;                  // [0x12b80]
        pattern_ = orders_[0];         // [0x12b78] (set by the load)
        // 0x14714: [0x14377] = ((2 BPM / 5) << 32) / 0x411 (never read), [0x1437f] = 20, [0x14381] = 0
        play_ = true;                  // [0x14385] = 0x14591
    }

    // 0x13f18 -> 0x1475b (stop the 16 voices; their volumes stay), then main's `call [0x12b4c]` = 0x148ca (IRQ vector
    // back, GF1 reset, output off)
    void stop() {
        play_ = false;
        for (int v = kVoices - 1; v >= 0; v--) {
            c_.outb(0x102, v);
            c_.w8(0x00, 3);
        }
        c_.w8(0x4c, 0);
        c_.outb(0x000, 0x0b);
    }

    // ---- the interrupt ----

    // 0x1443c: EOI, sti; one source: the volume ramp first, else timer 2
    void irq() {
        const int st = c_.inb(0x006);
        if (st & 0x40) {
            rampIrq();
        } else if (st & 8) {
            c_.w8(0x45, 0);
            c_.w8(0x45, 8);
            if (play_) timer();
            positions.push_back(position());
        }
        c_.outb(0x103, regSel_);
    }

    // 0x1343c
    void tick() {
        rowRead = false;
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
            order_ = (order_ + 1) & 0xff;
            if (order_ == songlen_) order_ = 0;
            pattern_ = orders_[order_];
        } else {
            row_ = (row_ + 1) & 0xff;
        }
        nextrow_ = 0;
        brk_ = 0x3f;
        rowRead = true;
        if (size_t(pattern_) * 64 + size_t(row_) >= size_t(npat_) * 64)
            throw std::runtime_error("music: a row outside the patterns");
        for (int k = 0; k < nch_; k++) {
            roww_ = row_;
            rowChannel(ch_[k], track(k, pattern_ * 64 + row_));
            // (0x14284 stores the volume per channel and note in a table nothing reads)
        }
    }

private:
    uint32_t be16(uint32_t a) const {
        need(a + 2);
        return uint32_t(mod_[a] << 8 | mod_[a + 1]);
    }
    void need(size_t end) const {
        if (end > mod_.size()) throw std::runtime_error("music: the module runs past the program image");
    }
    Event& track(int ch, int i) { return tracks_[(size_t(ch) * size_t(npat_)) * 64 + size_t(i)]; }
    const Ins& ins(uint32_t i) const {
        if (i >= 31) throw std::runtime_error("music: instrument number outside the table");
        return ins_[i];
    }
    int master() const { return masterFn ? masterFn(c_.now()) : master0_; }

    // 0x14591: the tick, the timer count, then the voices one by one
    void timer() {
        tick();
        // tempo -> timer-2 count: 2 BPM / 5 ticks per second, 1e6 / that in us, / 320 us; all divisions truncate
        const uint32_t hz = uint32_t(bpm_ * 2) / 5;
        const uint32_t n = (kTimerUs / hz) / 320;
        c_.w8(0x47, int((0x100 - n) & 0xff));
        for (int v = 0; v < kVoices; v++) {  // ecx = 15 .. 0, voice = ecx ^ 15
            Chan& C = ch_[v];
            c_.outb(0x102, v);
            int cl = C.flags;
            C.flags = 0;
            if (!cl) continue;
            if (cl & 8) {
                c_.outb(0x103, 9);  // select 9, not 0x89: the card answers otherRead
                const int bx = c_.inw(0x104);
                c_.w8(0x07, 4);           // ramp down to 0x040 ..
                c_.w8(0x08, bx >> 8);
                c_.w8(0x0d, 0x60);        // .. with the ramp IRQ
                ramps++;
                flag_ = false;
                bx_ = uint32_t(bx);       // BX of the code the ramp IRQ interrupts
                if (waiter) waiter();     // sti'd busy-wait at 0x14646 until the ramp path sets [0x144b1]
                if (!flag_) throw std::runtime_error("music: the ramp IRQ did not come");
                cl = 7;
            }
            update(C, cl);
        }
    }

    // 0x144b2: one voice whose ramp has ended: stop it, program the new sample, start it
    void rampIrq() {
        c_.outb(0x103, 0x8f);
        const int v = c_.inb(0x105) & 0x1f;
        c_.outb(0x102, v);
        c_.w8(0x0d, 3);
        c_.w8(0x00, 3);
        Chan& C = ch_[v & 15];
        uint32_t bx = bx_ & 0xffff;
        // addr(hi, lo, a): shrd bx, si, 7; reg hi = address bits 7..19; reg lo (8-bit write, high byte) = address
        // bits 0..6 << 1 and, in bit 0 (the fraction's 1/2), bit 15 of the previous BX
        auto addr = [&](int rh, int rl, uint32_t a) {
            bx = ((bx >> 7) | (a << 9)) & 0xffff;
            c_.w16(rh, int((a >> 7) & 0xffff));
            c_.w8(rl, int(bx >> 8));
        };
        if (C.lstart != C.cLs) {
            C.cLs = C.lstart;
            addr(2, 3, C.lstart);
        }
        if (C.end != C.cEnd) {
            C.cEnd = C.end;
            addr(4, 5, C.end);
        }
        addr(0x0a, 0x0b, C.start + C.offset);
        c_.w8(0x00, C.ctl);  // start: 0 one-shot, 8 loop
        flag_ = true;        // [0x144b1] = 1
    }

    // 0x14663: frequency, pan and volume of the selected voice (cl: 4 frequency, 2 pan, 1 volume)
    void update(Chan& C, int cl) {
        if (cl & 4) {
            if (C.period) {  // FC = Hz / 37, rounded (0xe74 >> 1 with carry: 1850)
                const uint32_t hz = amiga_ / C.period;
                const uint32_t q = hz * 100 / 0xe74, r = hz * 100 % 0xe74;
                C.fc = (q + (r >= 1850 ? 1 : 0)) & 0xffff;
            }
            if (C.fc != C.cFc) {
                C.cFc = C.fc;
                c_.w16(0x01, int(C.fc));
            }
        }
        if ((cl & 2) && C.pan != C.cPan) {
            C.cPan = C.pan;
            c_.w8(0x0c, C.pan);
        }
        if (cl & 1) {
            const uint32_t i = (uint32_t(C.vol) * uint32_t(master())) >> 6;
            if (i >= 64) throw std::runtime_error("music: volume outside the table");
            c_.w16(0x09, int(volt_[i]));
        }
    }

    // word [0x10390 + 2 n] (n as the code's 32-bit index)
    uint32_t oct(uint32_t n) const { return x_.u16(kOct + 2 * n); }

    // 0x13574 / 0x13d0a: (period of the note in the octave at 0x10390 x 0x20ab0 >> octave) / C-2 rate
    uint32_t notePeriod(int note, uint32_t c2spd) const {
        if (!c2spd) throw std::runtime_error("music: a note without a C-2 rate");
        const uint32_t p = oct(uint32_t((note & 0xf) - 1)) * kNoteMul;
        return ((p >> ((note >> 4) & 31)) / c2spd) & 0xffff;
    }

    // 0x13db5 / 0x13e21: the note x semitones up, octave wrap by 11 (not 12), clamped to B-5 if [0x12b82] = 1
    uint32_t arpPeriod(const Chan& C, int x) const {
        int cl = C.note >> 4;
        uint32_t n = uint32_t((C.note & 0xf) - 1 + x);
        if (n > 11) {
            cl++;
            n -= 11;
            if (n > 11) {
                cl++;
                n -= 11;
            }
        }
        if (arpclamp_ == 1 && cl >= 6) {
            cl = 5;
            n = 11;
        }
        if (!C.c2spd) throw std::runtime_error("music: an arpeggio without a C-2 rate");
        return ((oct(n) * kNoteMul) >> (cl & 31)) / C.c2spd & 0xffff;
    }

    // 0x134f4
    void rowChannel(Chan& C, const Event& ev) {
        C.offset = 0;
        C.eff = ev.eff;
        C.par = ev.par;
        if (ev.note) {
            int keep = 0;  // [0x12b90]
            int bl = ev.ins;
            if (bl == 0) {
                bl = C.ins;
                if (bl == 0) return rowNoteless(C, ev);
                keep = 1;
            }
            C.ins = bl;
            const Ins& I = ins(uint32_t(bl - 1) & 0xff);
            C.note = ev.note;
            C.c2spd = I.c2spd;
            C.base = notePeriod(ev.note, I.c2spd);
            if (ev.eff == 3 || ev.eff == 5) return rowNoteless(C, ev);  // tone portamento: the note is the target
            notes++;
            C.period = C.base;
            C.flags = 0x0c;  // frequency + restart (no pan bit)
            C.pan = 7;
            C.ctl = 0;
            C.vibpos = 0;
            C.offset = 0;
            C.start = I.dram;
            C.end = I.dram + I.len - 1;
            if (I.flags & 1) {
                C.lstart = I.ls + I.dram;
                C.end = I.le + I.dram;
                C.ctl |= 8;
            }
            if (keep != 1) {
                C.vol = std::min(I.vol, 0x3f);
                C.flags |= 1;
            }
            if (ev.eff == 0xe && ev.par >> 4 == 0xd) C.flags = 0;
            return rowVolume(C, ev);
        }
        rowNoteless(C, ev);
    }

    // 0x13640: an instrument number without a note (or with tone porta) resets the volume; the stored instrument
    // number becomes 0-based here
    void rowNoteless(Chan& C, const Event& ev) {
        if (ev.ins) {
            const int b = (ev.ins - 1) & 0xff;
            C.ins = b;
            C.vol = std::min(ins(uint32_t(b)).vol, 0x3f);
            C.flags |= 1;
        }
        rowVolume(C, ev);
    }

    // 0x13670: the volume byte (Cxx), then the row effect (tables 0x1374c / 0x13794)
    void rowVolume(Chan& C, const Event& ev) {
        if (ev.vol != 0xff) {
            C.flags |= 1;
            C.vol = std::min(int(ev.vol), 0x3f);
        }
        C.tickcnt = 0;
        const int eff = C.eff;
        int par = C.par;
        switch (eff) {
        case 0x0:  // 0x13da0 arpeggio: base, +x, +y
            if (par) {
                C.arpcnt = 0;
                C.arp[0] = C.period;
                C.arp[1] = arpPeriod(C, par >> 4);
                C.arp[2] = arpPeriod(C, par & 0xf);
            }
            break;
        case 0x3:  // 0x13aec
            if (!par) par = C.pspeed;
            C.pspeed = C.par = par;
            break;
        case 0x4: {  // 0x13a30
            int d = par & 0xf, s = par & 0xf0;
            if (!d) d = C.vdepth;
            if (!s) s = C.vspeed;
            C.vdepth = d;
            C.vspeed = s;
            C.par = d | s;
            break;
        }
        case 0x9: {  // 0x138c1 (no test for 900: offset 0, restart anyway)
            uint32_t off = uint32_t(par) << 8;
            const uint32_t lim = C.end - C.start;
            if (off >= lim) off = lim - 1;
            C.offset = off;
            C.flags |= 8;
            break;
        }
        case 0xb:  // 0x13ca6
            brk_ = roww_ & 0xff;
            order_ = (par - 1) & 0xff;
            break;
        case 0xd:  // 0x1388a
            if (par >= 0x64) par = 0x63;
            nextrow_ = ((par & 0xf) + (par >> 4) * 2 + (par >> 4) * 8) & 0xff;
            brk_ = roww_ & 0xff;
            break;
        case 0xf:  // 0x1385c
            if (par == 0) {
                // F00: nothing
            } else if (par > 0x1f) {
                bpm_ = par;
            } else {
                speed_ = counter_ = par;
            }
            break;
        case 0xe: {
            const int x = par >> 4, y = par & 0xf;
            switch (x) {
            case 0x1: {  // 0x13c1a
                const uint32_t p = (C.period - uint32_t(y) * 4) & 0xffff;
                C.period = p < uint32_t(kMinPeriod) ? uint32_t(kMinPeriod) : p;
                C.flags |= 4;
                break;
            }
            case 0x2: {  // 0x13c48
                const uint32_t p = (C.period + uint32_t(y) * 4) & 0xffff;
                C.period = p > uint32_t(kMaxPeriod) ? uint32_t(kMaxPeriod) : p;
                C.flags |= 4;
                break;
            }
            case 0x5:  // 0x13d0a
                C.base = C.period = notePeriod(C.note, c2spd_[y]);
                break;
            case 0x6:  // 0x13d46
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
                    order_ = (order_ - 1) & 0xff;
                }
                break;
            case 0x8:  // 0x13cfe
                C.pan = y;
                C.flags |= 2;
                break;
            case 0xa:  // 0x13c76
                C.vol = std::min((C.vol + y) & 0xff, 0x3f);
                C.flags |= 1;
                break;
            case 0xb:  // 0x13c90
                C.vol = C.vol >= y ? C.vol - y : 0;
                C.flags |= 1;
                break;
            case 0xd:  // 0x13ce5: no start on the row
                C.flags = 0;
                break;
            case 0xe:  // 0x13cee
                patdelay_ = y;
                break;
            default: break;  // E0, E3, E4, E7, E9 (retrigger!), EC (on the row), EF: nothing
            }
            break;
        }
        default: break;  // 1, 2, 5, 6, 7, 8 (!), A, C: nothing on the row
        }
    }

    // vol - ah (8 bit): a negative result -> 0, >= 64 -> 63
    static void volslide(Chan& C, int ah) {
        int al = (C.vol - ah) & 0xff;
        if (al & 0x80) al = 0;
        C.vol = al >= 0x40 ? 0x3f : al;
    }

    // 0x136de: every channel on the ticks between rows (tables 0x137d4 / 0x1381c)
    void tickEffects() {
        for (int i = 0; i < nch_; i++) {
            Chan& C = ch_[i];
            C.tickcnt = (C.tickcnt + 1) & 0xff;
            if (++C.arpcnt == 3) C.arpcnt = 0;
            const int eff = C.eff, par = C.par;
            switch (eff) {
            case 0x0:  // 0x13e92
                if (par) {
                    C.period = C.arp[C.arpcnt % 3];
                    C.flags = 4;  // (mov: replaces the flags)
                }
                break;
            case 0x1:  // 0x138e3
                if (C.period) {
                    const uint32_t p = (C.period - (uint32_t(par) << 2)) & 0xffff;
                    C.period = int16_t(p) < kMinPeriod ? uint32_t(kMinPeriod) : p;
                    C.flags |= 4;
                }
                break;
            case 0x2:  // 0x13951
                if (C.period) {
                    const uint32_t p = (C.period + (uint32_t(par) << 2)) & 0xffff;
                    C.period = p > uint32_t(kMaxPeriod) ? uint32_t(kMaxPeriod) : p;
                    C.flags |= 4;
                }
                break;
            case 0x3:    // 0x13a95
            case 0x5: {  // 0x13b03
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
            case 0x4:    // 0x139bf
            case 0x6: {  // 0x13b7f
                int p;
                if (eff == 6) {
                    volslide(C, ((par & 0xf) - (par >> 4)) & 0xff);
                    p = C.vdepth | C.vspeed;
                } else {
                    p = par;
                }
                C.vibpos = (C.vibpos + ((p & 0xf0) >> 2)) & 0xff;
                // the "sine" is 32 bytes of the module (notes 9.5); mul dl, rol ax 1, xchg al ah, and ah 1: ax >> 7
                int ax = (vib_[(C.vibpos >> 2) & 0x1f] * (p & 0xf)) >> 7;
                if (!(C.vibpos & 0x80)) ax = -ax;
                uint32_t a = uint32_t(ax * 4 + int(C.base)) & 0xffff;
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
            case 0xa: {  // 0x13a5e: up x, else down y
                const int x = par >> 4, y = par & 0xf;
                volslide(C, x ? (-x & 0xff) : y);
                C.flags |= 1;
                break;
            }
            case 0xe: {
                const int x = par >> 4, y = par & 0xf;
                if (x == 0xc && y == C.tickcnt) {  // 0x13cbd note cut
                    C.vol = 0;
                    C.flags |= 1;
                } else if (x == 0xd && y == C.tickcnt) {  // 0x13cd3 note delay
                    C.flags |= 0xf;
                }
                break;  // E9x 0x13cf9: nothing
            }
            default: break;  // 7 (tremolo), 8, 9, B, C, D, F: nothing on the ticks
            }
        }
    }

    const OkeanExe& x_;
    gus::Card& c_;
    std::span<const uint8_t> mod_;
    uint32_t c2spd_[16] = {}, per36_[36] = {}, volt_[64] = {};
    int vib_[32] = {};
    uint32_t arpclamp_ = 0, amiga_ = 0;
    int master0_ = 63;
    bool play_ = false;     // [0x14385] = 0x14591
    bool flag_ = true;      // [0x144b1]: set by the ramp path, cleared before each wait
    uint32_t bx_ = 0;       // BX at the wait: the value read from select 0x09
    int regSel_ = 0x43;     // [0x143a1]: register select restored at the end of the IRQ
    int nch_ = 4, speed_ = 6, bpm_ = 125, songlen_ = 0, npat_ = 0;
    int orders_[256] = {};
    Ins ins_[31];
    std::vector<Event> tracks_;
    Chan ch_[16];
    int order_ = 0, row_ = 0, counter_ = 1;  // [0x12b75], [0x12b72], [0x12b71]
    int roww_ = 0, brk_ = 0x3f, nextrow_ = 0, patdelay_ = 0, pattern_ = 0, loopcnt_ = 0, looprow_ = 0;
};

}  // namespace

void run(const OkeanExe& exe, gus::Card& card, const Options& opt, Run& out) {
    Player pl(exe, card);
    out = Run{};
    if (opt.master) pl.masterFn = [&](double t) { return opt.master(t); };
    card.otherRead = opt.otherRead;
    card.tick = 0;
    pl.gusInit();  // at card time 0: timer 2 starts with count 0xc2
    pl.load();     // (instant here; with interrupts off on the original)
    out.dramEnd = pl.dramEnd;

    struct Irq {
        char kind;
        double te, td, tlog;  // the event, the service time, the time logged
    };
    int ntimer = 0;
    size_t ci = 0;  // next capture interrupt
    size_t tj = 0;  // next player tick of opt.tickTime
    int captureTimers = 0;
    if (opt.capture)
        for (const CaptureIrq& q : *opt.capture) captureTimers += q.kind == 't';
    else if (!opt.tickTime)
        throw std::logic_error("music: no schedule");

    // the next interrupt: its kind, event time and service time
    auto next = [&](char want) -> Irq {
        const gus::Card::Event ev = card.nextEvent();
        if (ev.t < 0) throw std::runtime_error("music: the GUS has no event left");
        Irq q{0, ev.t, 0, 0};
        if (opt.capture) {
            if (ci >= opt.capture->size()) throw std::runtime_error("music: the capture has no interrupt left");
            const CaptureIrq& c = (*opt.capture)[ci++];
            q.kind = c.kind;
            q.tlog = c.time;
            q.td = c.time - ev.t > kLate ? c.time : ev.t + (ev.voice >= 0 ? 1e-7 : 0.0);
        } else {
            q.kind = ev.voice >= 0 ? 'r' : 't';
            if (q.kind == 'r') {
                q.td = ev.t + 1e-7;
            } else if (ntimer < kStartAfter) {
                q.td = ev.t;
            } else {
                const double t = (*opt.tickTime)[tj++];
                q.td = t - ev.t > kLate ? t : ev.t;
                if (ev.t - t > kLate) out.early++;  // (served at the overflow: it cannot come before it)
            }
            q.tlog = q.td;
        }
        if (q.kind != want)
            throw std::runtime_error(std::string("music: expected a ") + (want == 'r' ? "ramp" : "timer") +
                                     " interrupt");
        return q;
    };
    auto serve = [&](const Irq& q) {
        card.events(q.td);
        if (!card.irq()) throw std::runtime_error("music: no interrupt pending");
        const char got = (card.status() & 0x40) ? 'r' : 't';
        if (got != q.kind) throw std::runtime_error("music: the schedule serves another interrupt kind");
        if (got == 't') {
            ntimer++;
            card.tick = ntimer;
            out.timerEvent.push_back(card.timerRaised());
            out.timerServed.push_back(q.td);
        }
        if (pl.started()) {
            if (q.td - q.te > kLate) {
                out.late++;
                out.maxLate = std::max(out.maxLate, q.td - q.te);
            }
            if (q.te - q.td > kLate) out.early++;
        }
        card.logIrq(got, q.tlog);
        out.irqs++;
        pl.irq();
    };
    pl.waiter = [&] { serve(next('r')); };

    for (;;) {
        if (opt.capture) {
            if (ntimer >= captureTimers) break;
        } else if (ntimer >= kStartAfter) {
            if (tj >= opt.tickTime->size()) break;
            if (opt.stopTime >= 0 && (*opt.tickTime)[tj] > opt.stopTime) break;
        }
        serve(next('t'));
        if (ntimer == kStartAfter) {
            out.prestart = pl.positions.back();  // (frames recorded before the start see the image's values)
            pl.start();                          // (the first part's set-up, between two timer interrupts)
            pl.positions.back() = pl.position();
            card.lost = 0;
        }
    }
    // the stop and the shutdown (main 0x10241 .. 0x10246)
    if (opt.capture) {
        out.stopTime = card.now();
    } else {
        out.stopTime = opt.stopTime >= 0 ? std::max(opt.stopTime, card.now()) : card.timerNext();
        card.events(out.stopTime);
    }
    pl.stop();
    out.pos = std::move(pl.positions);
    out.notes = pl.notes;
    out.ramps = pl.ramps;
}

void songPositions(const OkeanExe& exe, int ticks, std::vector<Position>& pos, std::vector<uint8_t>& newRow) {
    gus::Card card;
    Player pl(exe, card);
    pl.gusInit();
    pl.load();
    pl.start();
    pos.clear();
    newRow.clear();
    for (int j = 0; j < ticks; j++) {
        pl.tick();
        pos.push_back(pl.position());
        newRow.push_back(pl.rowRead ? 1 : 0);
    }
}

}  // namespace modplay
