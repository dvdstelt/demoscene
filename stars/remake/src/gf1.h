// The Gravis UltraSound as STARS's music player (LCA's MOD-Player V3.02, GUS path) drives it; see
// stars/re/notes-music.md sections 2-5, 9 and 10.
//
// gus::Card is the card from the player's side: the ports and registers it writes, logged in run_stars.py's format
// (the format of stars/re/full.gus), the 1 MB sample DRAM, and the two GUS timers that clock the player (section
// 5.3). The player never reads a register whose value changes what it writes, so the card needs no answers.
//
// gus::Gf1 is a model of the GF1 synthesiser that renders the voices from the register writes: lcaplay.py's class
// GF1 (the model of paper/re/notes-music.md section 7 and jizz/remake/src/gf1.cpp, forward loops and one-shots),
// evaluated at any output rate. The card's own frame rate is 617,400 / active voices = 19,293.75 Hz with this
// player's 32 voices; at another rate the position increments and the volume-ramp steps are scaled by
// (617,400 / voices) / rate, so pitch, ramp durations and timing stay the card's while the voices are sampled more
// finely (section 11: only aliasing and interpolation change). Positions carry 20 bits below the GF1's 9-bit
// fraction and volumes 24 bits below its 12-bit value; at the card's own rate the model gives lcaplay.py's frames.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gus {

constexpr int kBase = 0x240;            // ULTRASND=240,3,3,7,7: the capture's environment (run_stars.py)
constexpr double kGf1Clock = 617400.0;  // GF1 voice frames per second x active voices

// The GUS timers (AdLib compatible): timer 1 counts 80 us steps, timer 2 320 us steps. A started timer overflows
// every (256 - count) steps and reloads the count register as it is at the overflow, so a count written by a tick
// takes effect one period later. An overflow raises the GUS IRQ (one player tick) when reg 0x45 enables that
// timer's IRQ (bit 2 timer 1, bit 3 timer 2). Times are integer microseconds after the timers were started.
class Timers {
public:
    void write(int reg, int val);  // regs 0x45 (IRQ enables), 0x46 (timer 1 count), 0x47 (timer 2 count)
    void control(int val);         // AdLib register 4 (2X8 = 4, value to 2X9): bit 7 reset flags, bits 0/1 start
    int64_t nextIrq();             // time of the next IRQ (us), or -1 if no enabled timer runs

private:
    int64_t now_ = 0;
    int64_t next_[2] = {-1, -1};  // next overflow of timer 1 / 2 (-1: stopped)
    int count_[2] = {0, 0};
    int enable_ = 0;
};

// The card as the player writes it. Every write can be logged as run_stars.py logs it: "<tick> v<voice> r<reg>
// <val>" for voice registers (< 0x40; 8-bit writes log the byte, 16-bit writes the word), "<tick> g r<reg> <val>"
// for global registers (not the DRAM address 0x43 / 0x44), "<tick> p<port> <val>" for other ports in 0x240..0x34f
// (the voice select 3X2, the register select 3X3 and the DRAM data 3X7 are not logged).
class Card {
public:
    Card();
    virtual ~Card() = default;

    std::string* log = nullptr;  // register-write log, or null
    int tick = 0;                // log tick of the writes

    void port(int p, int val);   // out p, val (absolute port)
    void select(int v) { voice_ = v & 31; }  // 3X2 (not logged)
    void w8(int reg, int val);   // reg -> 3X3, byte -> 3X5
    void w16(int reg, int val);  // reg -> 3X3, word -> 3X4
    void upload(uint32_t a, const uint8_t* src, size_t n);  // DRAM by DMA or by regs 0x43 / 0x44 + 3X7 (not logged)
    const std::vector<uint8_t>& dram() const { return dram_; }  // 1 MB
    Timers& timers() { return timers_; }

protected:
    // What the GF1 receives: the selected voice, the register and the 16-bit data word (an 8-bit write puts its
    // byte in the high half; the low half is then the low byte of the last 16-bit write).
    virtual void received(int /*voice*/, int /*reg*/, uint32_t /*d*/) {}

private:
    void logWrite(int reg, int val);
    std::vector<uint8_t> dram_;
    Timers timers_;
    int voice_ = 0;
    int adlib_ = 0;           // AdLib register selected by 2X8
    uint32_t latch_ = 0;      // low byte of the last 16-bit write
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
        int ctl = 3;                          // reg 0x00: bit 0 stopped, 1 stop, 3 loop
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
