// The Gravis UltraSound as MIDAS Sound System 0.5a's GUS sound device drives it (vividexp/re/notes-music.md sections
// 5 and 8), from Inside's gf1.* (itself clone meets clone's, JIZZ's and STARS's).
//
// gus::Card is the capture's fake GUS (vividexp/re/run_vivid.py): it logs every port and register write in the
// format of vividexp/re/full.gus ("stamp vN rXX val" for the voice registers, "stamp g rXX val" for the global ones
// from 0x40 on, except the DRAM address registers 0x43 / 0x44, "stamp p220 val" for the mix control port), keeps the
// 1 MB sample DRAM (the device pokes every byte through port 3X7), and answers a register read with the last value
// written (| 1 for the status registers 0x80 / 0x8d: "voice stopped", "ramp stopped"). That is what full.gus was
// recorded with: every volume ramp starts at 0x05, and the device's wait for ramps ends at once.
//
// gus::Hardware answers the reads from a model of the GF1 running at the card's own rate, 617,400 / active voices
// frames per second (38,587.5 Hz with part I's 16 voices, 30,870 Hz with part II's 20), as
// vividexp/re/models/music/render.py does: the volume the device reads before a ramp is the voice's current (ramped)
// volume, and each pass of the device's wait for the ramps of the voices it starts lets the card run one frame. Its
// clock follows render.py's: advanceTo(t) runs the whole frames from the card's time to t. It can record every write
// with the instant it takes effect, for gus::Synth.
//
// gus::Synth renders the voices from those writes at any output rate. The card's position increments and volume-ramp
// steps are scaled by (617,400 / voices) / rate, so pitch, ramp durations and timing stay the card's; only the
// sampling of the voices gets finer. Positions carry 23 bits below the GF1's 9-bit fraction, volumes 24 bits below its
// 12-bit value. Voices: 8-bit DRAM data (MIDAS converts 16-bit samples to 8 bits, so voice control bit 2 is never
// set), linear interpolation between address a and a + 1, one-shots, forward and bidirectional loops (as DOSBox-X and
// mxmplay.py model them), volume ramps, the 12-bit log volume (256 + mantissa) * 2^(exponent - 24). The output is
// mono: MIDAS runs in mono mode here, every voice stays at pan 8 (notes-music 0, 5.3). Stopped voices are mixed at
// their parked position.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gus {

constexpr int kBase = 0x220;            // ULTRASND's port: the capture's card (for the log)
constexpr double kGf1Clock = 617400.0;  // GF1 voice frames per second x active voices

// ---------------------------------------------------------------------------------------------- the fake card

class Card {
public:
    Card();
    virtual ~Card() = default;

    std::string* log = nullptr;  // register-write log in full.gus's text format, or null
    int tick = 0;                // log stamp of the writes (the timer interrupt)

    void port(int p, int val);               // out base + p (MIDAS: p = 0, the mix control port 2X0)
    void select(int v) { voice_ = v & 31; }  // out base + 0x102 (not logged)
    void poke(uint32_t a, uint8_t b) { dram_[a & 0xfffff] = b; }  // a DRAM byte through 3X7 (not logged)
    const std::vector<uint8_t>& dram() const { return dram_; }    // 1 MB

    void w8(int reg, int val);   // reg -> base + 0x103, byte -> base + 0x105
    void w16(int reg, int val);  // reg -> base + 0x103, word -> base + 0x104
    virtual int r8(int reg);
    virtual int r16(int reg);
    // The device's start loop (0x41c43e) found a voice whose ramp is still running and polls again: card time passes.
    // On the fake card it never happens (its reads say every ramp has ended).
    virtual void pass() {}

protected:
    // What the GF1 receives: the selected voice, the register and the 16-bit data word (an 8-bit write puts its byte
    // in the high half; the low half is then the low byte of the last 16-bit write).
    virtual void received(int /*voice*/, int /*reg*/, uint32_t /*d*/) {}
    int voice_ = 0;

private:
    void logWrite(int reg, int val);
    std::vector<uint8_t> dram_;
    uint16_t regs_[32][0x80] = {};  // last value written per voice and register (the fake card's reads)
    uint32_t latch_ = 0;
};

// ---------------------------------------------------------------------------------------------- the card's model

// A register write as the synthesiser receives it, and when: at instant `clock` (PIT clocks of program time: the
// timer interrupt that runs the device, or the main line's instant) plus `waited` card frames at 617,400 / `voices`
// frames per second (the ramp waits of that interrupt before the write).
struct Write {
    double clock;
    int32_t waited;
    uint8_t voices, voice, reg;
    uint16_t d;
};

class Hardware : public Card {
public:
    Hardware();

    std::vector<Write>* writes = nullptr;  // if set, every write is appended with clock and waited
    double clock = 0;                      // set by the caller: the instant of the writes that follow
    int waited = 0;                        // frames the ramp waits ran since the caller last cleared it
    int64_t frames = 0;                    // card frames run in all
    int64_t passes = 0;                    // passes of the device's ramp wait in all

    double frameRate() const { return kGf1Clock / active_; }
    int voices() const { return active_; }
    // render.py's GF1.advance_to: the first call sets the card's time; later ones run the whole frames from it to t
    // (seconds). The card's time advances by the frames run, the waits' too.
    void advanceTo(double t);
    void run(int64_t n);  // n card frames of every active voice

    int r8(int reg) override;
    int r16(int reg) override;
    void pass() override;  // one card frame per pass of the device's poll loop (render.py's guess)

protected:
    void received(int voice, int reg, uint32_t d) override;

private:
    struct Voice {
        int ctl = 3;                          // reg 0x00: bit 0 stopped, 1 stop, 3 loop, 4 bidi, 6 back
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
    double time_ = 0;  // seconds of program time of the card's next frame
    bool timeSet_ = false;
};

// ---------------------------------------------------------------------------------------------- the synthesiser

// Copyable: a copy is a snapshot of all voices (the DRAM is shared).
class Synth {
public:
    explicit Synth(double rate);  // output frames per second

    // The card's DRAM from now on (MIDAS uploads each part's samples over the last part's).
    void setDram(std::shared_ptr<const std::vector<float>> d8) { d8_ = std::move(d8); }
    static std::shared_ptr<const std::vector<float>> dramSamples(const std::vector<uint8_t>& dram);

    void write(int voice, int reg, uint32_t d);  // a register write as Card::received sees it
    void advance(int64_t n);                     // n frames, voices only (no output)
    // n frames: moving or ramping voices are added frame by frame to out; a voice that stays constant from frame k on
    // adds its value to dc[k] and subtracts it at dc[n] (the caller turns these into prefix sums).
    void mix(int n, double* out, double* dc);

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
    };

    void reset();
    void scale(Voice& s) const;
    static void stepTo(Voice& s, int64_t n);  // n frames at once (the same as n single frames)
    void mixVoice(Voice& s, int n, double* out, double* dc) const;
    double sample(int64_t pos) const;  // DRAM at a position, interpolated, 1.0 = full scale
    static double gain(int64_t vol);   // GF1 log volume -> linear gain (exact at the card's 12-bit steps)

    std::shared_ptr<const std::vector<float>> d8_;  // DRAM as signed bytes / 128
    double rate_;
    Voice v_[32];
    int active_ = 14;
    bool dac_ = false;
};

}  // namespace gus
