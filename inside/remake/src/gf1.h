// The Gravis UltraSound as Inside's DSMI driver SDIGUS.DSD drives it (inside/re/notes-music.md sections 7, 8 and 11),
// from clone meets clone's gf1.* (itself JIZZ's and STARS's, extended with 16-bit voices).
//
// gus::Card is the capture's fake GUS (run_inside.py): it logs every port and register write in the format of
// inside/re/full.gus ("stamp vN rXX val" for the voice registers, "stamp g rXX val" for the global ones from 0x40 on,
// except the DRAM address registers 0x43 / 0x44, "stamp pPPP val" for a port), keeps the 1 MB sample DRAM, and
// answers a register read with the last value written (| 1 for the status registers 0x80 / 0x8d), so the driver's
// ramp wait ends at once. That is what full.gus was recorded with.
//
// gus::Hardware answers the reads from a model of the GF1 running at the card's own rate, 617,400 / active voices
// frames per second (25,725 Hz with DSMI's 24 voices), as inside/re/models/music/gf1.py does: the volume the driver
// reads before a ramp is the voice's current (ramped) volume, and the driver's bounded wait for a ramp (20 polls of
// the selected voice) lets the card run one frame while that voice is still ramping. It can record every write with
// the instant it takes effect, for gus::Synth.
//
// gus::Synth renders the voices from those writes at any output rate. The card's position increments and volume-
// ramp steps are scaled by (617,400 / voices) / rate, so pitch, ramp durations and timing stay the card's; only the
// sampling of the voices gets finer. Positions carry 23 bits below the GF1's 9-bit fraction, volumes 24 bits
// below its 12-bit value. Voices: 8-bit and 16-bit DRAM data (voice control bit 2; a 16-bit voice's 20-bit address
// a is a word address, its word is at byte (a & 0xc0000) | (a & 0x1ffff) << 1, signed little endian), linear
// interpolation between address a and a + 1 (each translated), one-shots, forward and bidirectional loops (as
// DOSBox-X and mxmplay.py model them), volume ramps, pan law L = (15 - pan) / 15, R = pan / 15, the 12-bit log
// volume (256 + mantissa) * 2^(exponent - 24). Stopped voices are mixed at their parked position. (DSMI uses 8-bit
// voices, forward loops and one-shots only.)
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gus {

constexpr int kBase = 0x220;            // SOUND.CFG's port: the capture's card (for the log)
constexpr double kGf1Clock = 617400.0;  // GF1 voice frames per second x active voices
constexpr int kGf1ClockInt = 617400;

// ---------------------------------------------------------------------------------------------- the fake card

class Card {
public:
    Card();
    virtual ~Card() = default;

    std::string* log = nullptr;  // register-write log in full.gus's text format, or null
    int tick = 0;                // log stamp of the writes (the timer interrupt)

    void port(int p, int val);               // out base + p (p = 0: mix control 2X0, 0x100: MIDI control 3X0)
    void select(int v) { voice_ = v & 31; }  // 0x1152c0c: base + 0x102 (not logged)
    void poke(uint32_t a, uint8_t b) { dram_[a & 0xfffff] = b; }  // what the DMA upload stores (not logged)
    const std::vector<uint8_t>& dram() const { return dram_; }    // 1 MB

    void w8(int reg, int val);           // 0x1152c30: reg -> base+0x103, byte -> base+0x105
    void w16(int reg, int val);          // 0x1152cb6: reg -> base+0x103, word -> base+0x104
    virtual int r8(int reg);             // 0x1152d3f
    virtual int r16(int reg);            // 0x1152d7a
    // The driver's ramp wait 0x1152f50: at most 20 reads of reg 0x8d of the selected voice, until bit 0 (ramp
    // stopped). On the fake card the first read ends it.
    virtual void waitVoice();

protected:
    // What the GF1 receives: the selected voice, the register and the 16-bit data word (an 8-bit write puts its
    // byte in the high half; the low half is then the low byte of the last 16-bit write).
    virtual void received(int /*voice*/, int /*reg*/, uint32_t /*d*/) {}
    int voice_ = 0;

private:
    void logWrite(int reg, int val);
    std::vector<uint8_t> dram_;
    uint16_t regs_[32][0x80] = {};  // last value written per voice and register (the fake card's reads)
    uint32_t latch_ = 0;
};

// ---------------------------------------------------------------------------------------------- the card's model

// A register write as the synthesiser receives it, and when: at instant `clock` of the caller's time base (Inside:
// the timer interrupt that runs the driver) plus `waited` card frames at 617,400 / `voices` frames per second (the
// ramp waits of that interrupt before the write).
struct Write {
    int64_t clock;
    int64_t cardFrame;  // the card frames the model had run when it received the write
    int32_t waited;
    uint8_t voices, voice, reg;
    uint16_t d;
};

class Hardware : public Card {
public:
    Hardware();

    std::vector<Write>* writes = nullptr;  // if set, every write is appended with clock and waited
    int64_t clock = 0;                     // set by the caller: the instant of the writes that follow
    int waited = 0;                        // frames the ramp waits ran since the caller last cleared it
    int64_t frames = 0;                    // card frames run in all

    double frameRate() const { return kGf1Clock / active_; }
    int voices() const { return active_; }
    void run(int n);  // n card frames of every active voice

    int r8(int reg) override;
    int r16(int reg) override;
    // The 20 polls take about one GF1 frame (40 us on the ISA bus): the card runs one frame if the selected voice is
    // still ramping (gf1.py wait_voice).
    void waitVoice() override;

protected:
    void received(int voice, int reg, uint32_t d) override;

private:
    struct Voice {
        int ctl = 3;                          // reg 0x00: bit 0 stopped, 1 stop, 2 16 bit, 3 loop, 4 bidi, 6 back
        int fc = 0;                           // reg 0x01
        int64_t start = 0, end = 0, pos = 0;  // address << 9 | fraction
        int vctl = 3, rate = 0;               // reg 0x0d, reg 0x06
        int64_t lo = 0, hi = 0, vol = 0;      // 12-bit volume << 9
    };
    void reset();
    static int64_t rampInc(const Voice& s) { return (int64_t(s.rate & 63) * 512) >> (3 * (s.rate >> 6)); }
    static void voiceFrames(Voice& s, int64_t n);

    Voice v_[32];
    int active_ = 14;
};

// ---------------------------------------------------------------------------------------------- the synthesiser

// Copyable: a copy is a snapshot of all voices (the DRAM samples are shared).
class Synth {
public:
    Synth(const std::vector<uint8_t>& dram, double rate);  // the uploaded DRAM; output frames per second

    void write(int voice, int reg, uint32_t d);  // a register write as Card::received sees it
    void advance(int64_t n);                     // n frames, voices only (no output)
    // n frames: moving or ramping voices are added frame by frame to L / R; a voice that stays constant from frame
    // k on adds its value to dcL[k] / dcR[k] and subtracts it at [n] (the caller turns these into prefix sums).
    void mix(int n, double* L, double* R, double* dcL, double* dcR);
    uint32_t mixed = 0xffffffffu;  // voices that mix() outputs (the others only advance; for checks)

private:
    static constexpr int kPosFrac = 32;  // position: address << 32 (the GF1's 9-bit fraction + 23 bits)
    static constexpr int kVolFrac = 24;  // volume: 12-bit GF1 volume << 24
    struct Voice {
        int ctl = 3;                          // reg 0x00
        int fc = 0;                           // reg 0x01: increment << 1 in 1/512 addresses per card frame
        int64_t start = 0, end = 0, pos = 0;  // regs 0x02/0x03 (loop start), 0x04/0x05, 0x0a/0x0b
        int64_t inc = 0;                      // position step per output frame
        int vctl = 3, rate = 0;               // reg 0x0d (bit 6: ramp down), reg 0x06
        int64_t lo = 0, hi = 0, vol = 0;      // regs 0x07, 0x08, 0x09
        int64_t vinc = 0;                     // volume step per output frame
        int pan = 7;                          // reg 0x0c: 0 left .. 15 right
    };

    void reset();
    void scale(Voice& s) const;
    static void stepTo(Voice& s, int64_t n);  // n frames at once (the same as n single frames)
    template <bool W16> void mixVoice(Voice& s, int n, double* L, double* R, double* dcL, double* dcR) const;
    template <bool W16> double sample(int64_t pos) const;  // DRAM at a position, interpolated, 1.0 = full scale
    static double gain(int64_t vol);  // GF1 log volume -> linear gain (exact at the card's 12-bit steps)

    std::shared_ptr<const std::vector<float>> d8_, d16_;  // DRAM as signed bytes / 128 and words / 32768
    double rate_;
    Voice v_[32];
    int active_ = 14;
    bool dac_ = false;
};

}  // namespace gus
