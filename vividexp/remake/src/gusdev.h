// MIDAS Sound System 0.5a's "Gravis Ultrasound Sound Device v4.00" (structure 0x824328, code 0x41b1a0..0x41cd10) as
// VIVID.EXE uses it: mono, 8-bit samples uploaded byte by byte through port 3X7, no IRQ or DMA
// (vividexp/re/notes-music.md section 5; vividexp/re/models/music/midas.py's GusDevice). The calls only change the
// voice records; Play (0x41c43e), run by the timer before the player at every player interrupt, writes the registers:
// a new note ramps the old one down, stops the wave, waits until every starting voice's ramp has ended, then sets the
// sample addresses and ramps up. It talks to the card through gus::Card (the capture's fake card or the card's model),
// and reads its tables from VIVID.EXE: the GF1 output rates 0x824452 and the volume table 0x824478.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "gf1.h"

class VividExe;

namespace midas {

class GusDevice {
public:
    GusDevice(const VividExe& exe, gus::Card& card);

    void detect();                 // 0x41b1b5 (the port comes from ULTRASND: 0x220)
    void init(int mode);           // 0x41b267 (mode bit 0: mono)
    void close();                  // 0x41b45d
    void openChannels(int n);      // 0x41b4c3
    void closeChannels();          // 0x41b5ce (ClearChannels 0x41b5ea)

    void setRate(int ch, uint32_t rate);      // 0x41b99f
    int playSound(int ch, uint32_t rate);     // 0x41b80c
    void stopSound(int ch);                   // 0x41b951
    void setVolume(int ch, uint32_t vol);     // 0x41ba68
    int setSample(int ch, int handle);        // 0x41baf1
    int setPosition(int ch, uint32_t pos);    // 0x41bc76
    void setPanning(int ch, int pan);         // 0x41be67 (mono: stored only)
    void setUpdRate(int rate) { updRate_ = rate; }  // 0x41c42b
    // 0x41c001: uploads an 8-bit unsigned sample (type 1; type 0 = empty), returns its handle.
    int addSample(std::span<const uint8_t> data, int type, uint32_t length, int loop, uint32_t loopStart,
                  uint32_t loopEnd, int loopType);
    // 0x41c43e: the register writes of one player interrupt; false if the device is not playing (no player call).
    bool play();

    int outputRate() const { return outRate_; }
    // The DRAM ranges the uploads wrote (address, bytes incl. the padding), for checks.
    const std::vector<uint32_t>& uploads() const { return uploads_; }

private:
    struct Voice {  // 0x8239a4 + 0x4c v
        int flags = 0;      // 1 stop, 2 start, 4 volume, 8 rate, 0x10 sample addresses, 0x40 panning, 0x80 muted
        int sample = 0;     // +4 handle
        int playing = 0;    // +8 handle being played
        uint32_t fc = 0;    // +0xc
        uint32_t rate = 0;  // +0x10 Hz
        uint32_t volume = 0;
        int type = 0, loopMode = 0, loop1 = 0, loop2 = 0;
        uint32_t start = 0, loopStart = 0, loopEnd = 0, loop2Start = 0, loop2End = 0;
        int pan = 0;
        int active = 0;     // +0x48
    };
    struct Sample {  // [0x824324] + 0x30 h
        int type = 0;         // 0: free slot; 1: 8-bit (and empty)
        uint32_t addr = 0, length = 0;
        int mode = 0, loop1 = 0, loop2 = 0;
        uint32_t l1start = 0, l1end = 0, l2start = 0, l2end = 0;
    };
    void reset();                                  // 0x41b436
    void resetState();
    uint32_t fcOf(uint32_t rate) const;
    int volume(const Voice& r) const;              // the volume table entry of the voice's volume
    void delay() {}                                // 0x41b1a0: 8 reads of port 0x300 (no time in the model)

    const VividExe& exe_;
    gus::Card& p_;
    Voice v_[32];
    std::vector<Sample> samples_;  // by handle (0 unused)
    std::vector<uint32_t> uploads_;
    int channels_ = 0, outRate_ = 0, updRate_ = 0, master_ = 0x40;
    int everyVolume_ = 0, open_ = 0, paused_ = 0, mono_ = 0;
    int nextHandle_ = 1, maxHandle_ = 0;
    uint32_t heap_ = 0;  // the DRAM heap (0x41cb45): one free block from 0, samples taken in order
    static constexpr uint32_t kDram = 1u << 20;  // the card's DRAM (the memory test finds 1 MB)
};

}  // namespace midas
