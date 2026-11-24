// The Gravis UltraSound as The Sea Robot of Love's music player drives it (okean/re/notes-music.md sections 3, 6, 7,
// 11 and 12).
//
// gus::Card is the card from the player's side as the capture's emulator models it (run_okean.py; okeanplay.py's class
// Card): the ports and registers, logged in the format of okean/re/full.gus; the 1 MB sample DRAM; GUS timer 2, which
// clocks the player; and the volume ramps with their interrupts, which the player needs because its timer interrupt
// waits for the ramp-end IRQ of each new note (section 7): register 0x89 answers the current volume, register 0x8f the
// next voice of the ramp-IRQ queue, port 2X6 the IRQ status, and any other register select (the player reads select
// 0x09, not 0x89) answers `otherRead` (the emulator: 0; section 11). Times are seconds after the GUS init (program
// time). The ramps run in continuous time (the 12-bit volume as a double, 63 steps per GF1 frame, the end where it
// reaches its limit) and timer 2 overflows every (256 - count) x 320 us, reloading the count register as it is at the
// overflow; its IRQ status is one bit, so an overflow that finds the previous one unserved is lost. events(t) runs the
// hardware up to t; the caller serves the interrupts (modplay.cpp). The arithmetic is okeanplay.py's, operation for
// operation, so that with the capture's interrupt times the register writes come out as in full.gus.
//
// gus::Gf1 is a model of the GF1 synthesiser that renders the voices from the register writes: okeanplay.py's class
// GF1 (the model of dope/remake/src/gf1.cpp: 8-bit samples linearly interpolated, 12-bit log volume with ramps, pan
// 0..15 with a linear law, one-shots and forward loops, stopped voices mixed at their parked position), evaluated at
// any output rate. The card's own frame rate is 617,400 / active voices = 38,587.5 Hz with this player's 16 voices; at
// another rate the position increments and the volume-ramp steps are scaled by (617,400 / voices) / rate, so pitch,
// ramp durations and timing stay the card's while the voices are sampled at the output rate (notes section 16).
// Positions carry 20 bits below the GF1's 9-bit fraction and volumes 24 bits below its 12-bit value; at the card's own
// rate the model gives okeanplay.py's frames.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gus {

constexpr int kBase = 0x240;                      // ULTRASND=240,3,3,7,7: the capture's environment (run_okean.py)
constexpr double kGf1Clock = 1e6 / 1.619695497;   // GF1 voice frames per second x active voices (617,400 Hz)

// The card as the player sees it. Ports are offsets from the base: 0x000 mix control, 0x006 IRQ status, 0x008 /
// 0x009 AdLib index / data (timer control), 0x00b IRQ / DMA latch, 0x00f register control, 0x102 voice select,
// 0x103 register select, 0x104 data low (16-bit writes: the word), 0x105 data high, 0x107 DRAM data.
//
// Log lines (`log`, when set): "<tick> v<voice> r<reg> <value>" for every register write except the DRAM address
// 0x43 / 0x44 (an 8-bit write to a 16-bit register logs `latch | byte << 8`, with the latch of 3X4 byte writes: 0 for
// this player), "<tick> p<port> <value>" for port writes other than 3X2..3X5, 3X7 and 2X8, and "<tick> irq <t|r>
// <time>" from logIrq().
class Card {
public:
    Card();
    virtual ~Card() = default;

    std::string* log = nullptr;  // register-write log, or null
    int tick = 0;                // log tick of the writes (timer interrupts served so far)
    int lost = 0;                // timer overflows that found the previous one still pending
    int otherRead = 0;           // what a read of a write-only register select answers (select 0x09)

    void outb(int p, int val);
    void outw(int p, int val);  // only 0x104
    int inb(int p);             // 0x006, 0x105, 0x107
    int inw(int p);             // only 0x104
    void w8(int reg, int val) { outb(0x103, reg); outb(0x105, val); }   // the player's helpers
    void w16(int reg, int val) { outb(0x103, reg); outw(0x104, val); }
    // PIO upload 0x14788: the bytes, the DRAM address register at the last one
    void upload(uint32_t a, const uint8_t* src, size_t n);
    void logIrq(char kind, double t);
    const std::vector<uint8_t>& dram() const { return dram_; }  // 1 MB

    // ---- the hardware in time ----
    struct Event {
        double t = -1;  // < 0: none
        int voice = 0;  // -1: timer 2 overflow, else the voice whose ramp ends
    };
    Event nextEvent() const;  // the earliest hardware event that can still raise an IRQ
    // Runs timer overflows and ramp ends up to t in time order (simultaneous ones: the timer, then by voice) and sets
    // the IRQ status.
    void events(double t);
    bool irq() const { return (status_ & 0x6c) != 0; }
    int status() const { return status_; }
    double now() const { return now_; }
    double timerNext() const { return t2Next_; }      // the next timer overflow
    double timerRaised() const { return t2Raised_; }  // the overflow whose IRQ is (or was last) pending

protected:
    // What the GF1 receives: the voice, the register and the 16-bit data word (an 8-bit write to a 16-bit register
    // puts its byte in the high half and the low byte of the last 16-bit write in the low half; to an 8-bit register,
    // the byte in the high half).
    virtual void received(int /*voice*/, int /*reg*/, uint32_t /*d*/) {}

private:
    struct Voice {
        int r[0x20] = {};   // voice registers as written (0x00 and 0x0d without bit 7: those are the IRQ flags)
        double t = 0;       // time of vol
        double vol = 0;     // 12-bit volume
    };
    double rate() const;            // GF1 frames per second
    double rampEnd(int vn) const;   // when voice vn's running ramp with IRQ reaches its limit (< 0: none)
    void sim(int vn, double t);     // the voice's ramp up to t
    void irqBits();
    void write(int reg, int val, int size);
    int read(int reg);
    void logPort(int p, int val);
    bool queued(int vn) const;

    std::vector<uint8_t> dram_;
    Voice v_[32];
    int g_[0x100] = {};            // global registers (0x0e, 0x41..0x4c)
    std::vector<int> queue_;       // voices with a ramp IRQ pending, in the order reg 0x8f reports them
    double now_ = 0, t2Next_ = 0, t2Raised_ = 0;
    bool t2Run_ = false;
    int status_ = 0;               // 2X6: bit 3 timer 2, bit 6 volume ramp
    int voice_ = 0, reg_ = 0, lo_ = 0, adlib_ = 0;
    uint32_t addr_ = 0;            // DRAM address (regs 0x43 / 0x44)
    uint32_t latch_ = 0;           // low byte of the last 16-bit register write
};

// The GF1 synthesiser. Copyable: a copy is a snapshot of all voices (the DRAM samples are shared).
class Gf1 {
public:
    Gf1(const std::vector<uint8_t>& dram, double rate);  // the uploaded DRAM; output frames per second

    void write(int voice, int reg, uint32_t d);  // a register write as Card::received sees it
    void advance(int64_t n);                     // n frames, voices only (no output)
    // n frames: moving or ramping voices are added frame by frame to L / R; a voice that stays constant from frame
    // k on adds its value to dcL[k] / dcR[k] and subtracts it at [n] (the caller turns these into prefix sums).
    void mix(int n, double* L, double* R, double* dcL, double* dcR);
    double frameRate() const { return kGf1Clock / active_; }  // the card's frames per second
    uint32_t mixed = 0xffffffffu;  // voices that mix() outputs (the others only advance; for checks)

private:
    static constexpr int kPosFrac = 29;  // position: byte address << 29 (the GF1's 9-bit fraction + 20 bits)
    static constexpr int kVolFrac = 24;  // volume: 12-bit GF1 volume << 24
    struct Voice {
        int ctl = 3;                          // reg 0x00: bit 0 stopped, 1 stop, 3 loop, 4 bidirectional, 6 backwards
        int fc = 0;                           // reg 0x01: increment << 1 in 1/512 bytes per GF1 frame
        int64_t start = 0, end = 0, pos = 0;  // regs 0x02/0x03, 0x04/0x05, 0x0a/0x0b
        int64_t inc = 0;                      // position step per output frame
        int vctl = 3, rate = 0;               // reg 0x0d (bit 6: ramp down), reg 0x06
        int64_t lo = 0, hi = 0, vol = 0;      // regs 0x07, 0x08, 0x09
        int64_t vinc = 0;                     // volume step per output frame
        int pan = 7;                          // reg 0x0c: 0 left .. 15 right
    };

    void reset();
    void scale(Voice& s) const;  // inc / vinc from fc / rate, the active voices and the output rate
    static void stepTo(Voice& s, int64_t n);  // n frames at once (the same as n single steps)
    void mixVoice(Voice& s, int n, double* L, double* R, double* dcL, double* dcR) const;
    double sample(int64_t pos) const;  // DRAM at a position, linearly interpolated, 1.0 = full scale
    static double gain(int64_t vol);   // GF1 log volume -> linear gain (mantissa linear, as the card's table)

    std::shared_ptr<const std::vector<float>> dram_;  // DRAM bytes as signed samples / 128 (+1 for the last byte)
    double rate_;
    Voice v_[32];
    int active_ = 14;
    bool dac_ = false;
};

}  // namespace gus
