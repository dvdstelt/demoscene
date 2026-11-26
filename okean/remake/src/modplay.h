// The Sea Robot of Love's music player: a ProTracker MOD player with its own GUS driver in KORSO2.EXE's code32
// (0x10414..0x14bd3), a version of DOPE's "ProTracker for GUS replay routine (C) 1994 Oskari Tammelin" (the intro's
// INFOFILE credits "the player by Phantom"), ported routine by routine (okean/re/notes-music.md; mirrors
// okean/re/models/music/okeanplay.py, which reproduces the capture's GUS memory full.dram, register stream full.gus and
// music positions full.ticks).
//
// Routines (code32 offsets; file offset in the unpacked image = 0xec0 + offset): 0x14935 GUS init (16 voices, DRAM
// probe, timer 2 at count 0xc2), 0x13f28 MOD load (the module in place at 0xae0; 0x1490c pan table, 0x14788 PIO
// sample upload), 0x13eb1 start (0x14714), 0x13f18 stop (0x1475b), 0x148ca shutdown, and the GUS IRQ 0x1443c: timer
// 2 -> 0x14591 (0x1343c tick: row with the effect tables 0x1374c / 0x13794, tick effects 0x136de with 0x137d4 /
// 0x1381c; tempo -> timer count; then voice by voice 0..15: a voice that (re)starts its sample is ramped down with the
// ramp IRQ on, and the handler waits with interrupts enabled until the nested ramp IRQ has switched the voice; 0x14663
// writes frequency, pan and volume), volume ramp -> 0x144b2 (one voice: stop it, program the new sample, start it).
//
// Static data is read from the program image by address: the module 0xae0, the base-octave periods 0x10390, the C-2
// rates 0x103a8, the ProTracker periods 0x103cc, the Amiga clock 0x10414, the pan table 0x10575, the instrument
// records 0x10c50, the start position 0x12b71..0x12b75, the arpeggio clamp 0x12b82, the master volume 0x12b8c, the
// channel records 0x12b98, the 32 module bytes 4xx reads as its sine (0x2f98: 16-bit addressing of 0x12f98, notes
// 9.5), the first timer count 0x1438a, the IRQ latches 0x143a6 and the volume table 0x143ba.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "gf1.h"

class OkeanExe;

namespace modplay {

// Timer interrupts the capture serves before main stops the music (full.gus log ticks 1..9203, position 30:37). The
// first three come before the player starts (the first part's set-up calls the start after the third), so player
// tick j (j = 0 reads row 00:00) is timer interrupt j + 4.
constexpr int kCaptureTicks = 9203;
constexpr int kStartAfter = 3;

struct Position {
    int order = 0, row = 0, countdown = 0;  // [0x12b75], [0x12b72], [0x12b71]
    bool operator==(const Position&) const = default;
};

// One interrupt of a capture's log (the `irq` lines of full.gus): kind 't' (timer) or 'r' (ramp), service time.
struct CaptureIrq {
    char kind = 't';
    double time = 0;
};

struct Options {
    // Schedule: the capture's service times of every interrupt (the check; the stop after the last), or
    // tickTime: player tick j served at tickTime[j] (or at its timer overflow when that is within 1.5 us, or later),
    // the timer interrupts before the start at their overflows, every ramp IRQ at its ramp end (+ 0.1 us, as the
    // emulator raises it); the stop at stopTime, or at the timer overflow after the last tick if stopTime < 0.
    const std::vector<CaptureIrq>* capture = nullptr;
    const std::vector<double>* tickTime = nullptr;
    double stopTime = -1;
    // [0x12b8c] at card time t (the end part's fade lowers it); null: the image's value.
    std::function<int(double)> master;
    int otherRead = 0;  // what the GF1 answers for a read of register select 0x09 (the emulator: 0)
};

struct Run {
    std::vector<double> timerEvent;  // per timer interrupt (log tick n = index + 1): its overflow (card time, s)
    std::vector<double> timerServed; // the time its handler ran
    std::vector<Position> pos;       // the position after it (for the start's interrupt: after the start)
    Position prestart;               // the position after interrupt kStartAfter, before the start
    double stopTime = 0;   // the stop 0x13f18 and the shutdown 0x148ca (no sound after them)
    uint32_t dramEnd = 0;  // end of the uploaded samples
    int irqs = 0;          // interrupts served
    int early = 0;         // interrupts served more than 1.5 us before their event (the schedule disagrees)
    int late = 0;          // interrupts served more than 1.5 us after their event
    double maxLate = 0;
    int notes = 0;         // note starts read from the pattern (not tone portamento)
    int ramps = 0;         // ramp-downs waited for
};

// Runs the GUS init 0x14935 at card time 0, the module load 0x13f28 (instantly), the interrupts of the schedule (the
// start 0x13eb1 after the third timer interrupt), the stop 0x13f18 and the shutdown 0x148ca, on `card`. Throws
// std::runtime_error when the program image is not what the player expects.
void run(const OkeanExe& exe, gus::Card& card, const Options& opt, Run& out);

// The tick 0x1343c alone (the positions do not depend on the card or on time): the position after each of `ticks`
// player ticks from the start on, and whether the tick read a row.
void songPositions(const OkeanExe& exe, int ticks, std::vector<Position>& pos, std::vector<uint8_t>& newRow);

}  // namespace modplay
