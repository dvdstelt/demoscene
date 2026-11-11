// The Gravis UltraSound as JIZZ's MXMPlay drives it (jizz/re/notes-music.md sections 4.3-4.6, 6 and 7): the
// register ports (0x29f63..0x2a00b), the sample DRAM, and a model of the GF1 synthesiser that renders the voices.
//
// gus::Card is the capture's fake GUS (run_jizz.py): every register write is remembered per voice and a read
// answers the last written value (| 1 for the status registers 0x80 / 0x8d), the ramp wait takes no time. That is
// what jizz/re/full.gus was recorded with. gus::Gf1 answers the reads from its model, as real hardware does, and
// renders 14 voices at 617,400 / 14 = 44,100 frames per second (mxmplay.py class GF1; the model of
// paper/re/notes-music.md section 7 plus bidirectional loops).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gus {

constexpr int kBase = 0x240;            // port as run_jizz.py configures it (ULTRASND=240,...), for the log
constexpr double kGf1Clock = 617400.0;  // GF1 voice frames per second = kGf1Clock / active voices

class Card {
public:
    Card();
    virtual ~Card() = default;

    std::string* log = nullptr;  // register-write log in run_jizz.py's text format ("tick vN rXX val"), or null
    int tick = 0;                // log tick of the writes

    void port(int p, int val);                // out base + p (p = 0: mix control)
    void select(int v) { voice_ = v & 31; }   // base + 0x102 (not logged)
    void poke(uint32_t a, uint8_t b) {  // DRAM upload (regs 0x43/0x44, base + 0x107; not logged)
        dram_[a & 0xfffff] = b;
        dramChanged_ = true;
    }
    const std::vector<uint8_t>& dram() const { return dram_; }   // 1 MB

    virtual void w8(int reg, int val);   // 0x29f6b: reg -> base+0x103, byte -> base+0x105
    virtual void w16(int reg, int val);  // 0x29fa5: reg -> base+0x103, word -> base+0x104
    virtual int r8(int reg);             // 0x29fbb
    virtual int r16(int reg);            // 0x29fd2
    virtual void waitRamps(int /*voices*/) {}  // the busy wait 0x2a17f: no time passes on the fake card

protected:
    void store(int reg, int val);
    std::vector<uint8_t> dram_;
    bool dramChanged_ = true;
    int voice_ = 0;

private:
    uint16_t regs_[32][0x80] = {};  // last value written per voice and register (the fake card's reads)
};

class Gf1 : public Card {
public:
    Gf1();

    // Output: run() appends the stereo frames (1.0 = one voice at full scale, gain 1, hard panned) to *out and
    // keeps the largest magnitude in peak. Without out the voices only advance.
    std::vector<float>* out = nullptr;
    double peak = 0;
    int waited = 0;  // frames the ramp waits ran since the caller last cleared it

    double frameRate() const { return kGf1Clock / active_; }
    void run(int n);  // n frames of every active voice

    void w8(int reg, int val) override;
    void w16(int reg, int val) override;
    int r8(int reg) override;
    int r16(int reg) override;
    void waitRamps(int voices) override;

private:
    struct Voice {
        int ctl = 3;                          // reg 0x00: bit 0 stopped, 1 stop, 3 loop, 4 bidirectional, 6 back
        int fc = 0;                           // reg 0x01
        int64_t start = 0, end = 0, pos = 0;  // address << 9 | fraction
        int vctl = 3, rate = 0;               // reg 0x0d, reg 0x06
        int64_t lo = 0, hi = 0, vol = 0;      // 12-bit volume << 9
        int pan = 7;
    };

    void reset();
    void hw(int reg, uint32_t d);
    static int64_t rampInc(const Voice& s) { return (int64_t(s.rate & 63) * 512) >> (3 * (s.rate >> 6)); }
    void voiceFrames(Voice& s, int n, bool mix);

    std::vector<int8_t> sdram_;  // DRAM as signed samples (+1 byte for the interpolation at the last address)
    double gain_[4096];          // GF1 12-bit log volume: (256 + mantissa) * 2^(exponent - 24)
    Voice v_[32];
    int active_ = 14;
    bool dac_ = false;
    uint32_t latch_ = 0;         // low byte of the last 16-bit write (an 8-bit write puts it in the data low byte)
    std::vector<double> L_, R_;  // mix of the current block
    std::vector<int64_t> pos_, vol_;
};

}  // namespace gus
