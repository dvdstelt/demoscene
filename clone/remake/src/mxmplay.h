// MXMPlay "cubic tiny gus xm player v1.4" (Niklas Beisert / pascal) in the build clone meets clone links (code
// 0x4018d4..0x40266c, tables 0x40266d..0x40296c), ported routine by routine from clone/re/models/music/mxmplay.py,
// which reproduces the capture's GUS DRAM, register stream and music positions (clone/re/notes-music.md section 4).
#pragma once
#include <cstdint>
#include <memory>
#include <span>

namespace gus {
class Card;
}

namespace mxm {

// The player on one card. module: the buffer modsynth::buildModule makes; image: the unpacked program image (the
// frequency tables 0x4026ad.., the vibrato quarter sine 0x40266d, the effect jump table 0x40274d). Both must
// outlive the player. Throws std::runtime_error on a module or image this port does not expect.
class Player {
public:
    Player(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void init();              // 0x40196a xmpInit: the sample upload 0x4025de (985,424 bytes from DRAM 0)
    void play(int order);     // 0x402129 xmpPlay: GF1 reset (18 voices), mix control, channels, timer install
    void setVolume(int v);    // 0x401bde xmpSetVolume (main: 0x20 right after xmpPlay)
    void stop();              // 0x40219a xmpStop: GF1 reset to 14 voices
    bool irq();               // 0x4021ac IRQ0: GUS update, then one player tick; true if a tick ran

    int pos() const;          // 0x401bf4 xmpGetPos: order << 8 | row
    uint32_t timer() const;   // [+0x39]: xmpGetTimer at the last IRQ (PIT clocks; signed)
    uint32_t cur() const;     // [+0x35]: PIT clocks from the last IRQ to the next one

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace mxm
