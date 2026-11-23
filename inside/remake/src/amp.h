// DSMI's AMF loader and its module player AMP 2.00 ("(C) 1992,1994 Otto Chrons") as INSIDE.EXE links them (loader
// 0x4103d0 / 0x40fcc8 / 0x40fc60, player 0x40c788..0x40e03e), ported routine by routine from
// inside/re/models/music/amf.py and dsmi.py (class Amp), which reproduce the capture's GUS register writes and music
// positions (inside/re/notes-music.md sections 2 and 5). The player drives the GUS driver (gusdrv.h) through DSMI's
// CDI layer, which sends every channel to it (channel = voice).
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class InsideExe;

namespace gusdrv {
class Driver;
}

namespace amp {

// A module as the loader leaves it in memory (MODULEINFO, notes-music 2.3). Instrument records are kept as AMP reads
// them; the sample data and the tracks point into the file, which must outlive the module.
struct Instrument {
    uint32_t index = 0;      // +0x2e in the file: the 1-based sample index
    uint32_t length = 0;     // +0x32
    uint16_t rate = 0;       // +0x36 Hz at note 60
    uint8_t volume = 0;      // +0x38 0..64
    uint32_t loopStart = 0;  // +0x39
    uint32_t loopEnd = 0;    // +0x3d (0: no loop)
    std::span<const uint8_t> data;  // the sample (8-bit unsigned), empty if none
    uint32_t handle = 0;     // AMP's +0x2e after loading: the data's address (here: 0x1000000 + its file offset)
};

struct Module {
    int version = 0;
    int channels = 0;           // +0x29
    int8_t pan[32] = {};        // +0x2a
    int tempo = 125, speed = 6; // +0x5a, +0x5b
    struct Order {
        int rows = 64;
        int track[32];          // index into tracks, -1 = none
    };
    std::vector<Order> orders;  // +0x50
    std::vector<Instrument> instruments;  // +0x4b
    // The stored tracks with their in-memory header {word events, byte 0} and the events {row, command, value};
    // AMP reads from offset 3. (Three 0xff bytes follow the last event, where the loader's block has 3 spare bytes.)
    std::vector<std::vector<uint8_t>> tracks;
};

// ampLoadAMF: reads an AMF of versions 12..14 (EYE.AMF is 14). Throws std::runtime_error on anything else.
Module loadAMF(std::span<const uint8_t> file);

// AMP on one driver. exe: INSIDE.EXE's image (the semitone, period, sine and arpeggio tables, read at construction).
class Player {
public:
    Player(const Module& m, const InsideExe& exe, gusdrv::Driver& drv);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void init();                     // ampInit 0x40d907(0) (DSMI time 0)
    int play(int first, int option); // ampPlay 0x40d998 from ampPlayModule 0x40dbb9: pans, first order, uploads
    void stop();                     // ampStopModule 0x40dc21
    void poll(uint32_t dsmiTime);    // ampPoll 0x40d878: the player ticks that are due

    int order() const;               // ampGetPattern 0x40de09 (the order)
    int row() const;                 // ampGetRow 0x40de21 (the row read last)
    // If set, every row the player reads is appended as order * 64 + row.
    std::vector<int>* rowLog = nullptr;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace amp
