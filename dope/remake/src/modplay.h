// DOPE's music player: "ProTracker for GUS replay routine, Copyright (C) 1994 Oskari Tammelin" ("gus unit raped by
// jmagic") in the loader's 32-bit code, GUS path, ported routine by routine (dope/re/notes-music.md; mirrors
// dope/re/models/music/dopeplay.py, which reproduces the capture's GUS memory full.dram, register stream full.gus and
// music positions full.ticks).
//
// Routines (code32 offsets; loader.bin file offset = 0x17e0 + offset): 0x45b4 GUS init (DRAM probe, timer 2), 0x2d15
// MOD load (28 channels; 0x4587 pan table, 0x4450 PIO sample upload), 0x2c17 start (0x4369), 0x2cff stop (0x43d6,
// 0x4545), 0x4040 GUS IRQ: timer 2 -> 0x41e9 (0x22c4 tick: row with the effect tables 0x2579 / 0x25c1, tick effects
// 0x251f with 0x2601 / 0x2649; tempo -> timer count; 0x421a note-off ramps; 0x4299 voice update), volume ramp ->
// 0x40d8 (switch to the new sample; 0x4299 after the last ramp of the tick).
//
// Static tables are read from the loader image by address: C-2 rates 0xb37, periods 0xb5b / 0xb8b, pan 0xd7d,
// instrument records 0x11fd, the vibrato sine 0x22a0 with the 32 bytes after it (4xx reads them: the dword [0x22c0]
// and the code of 0x22c4, notes section 8.6), IRQ latches 0x3f66, frame rates 0x3f76, volume table 0x3f9c, the first
// timer count 0x3f2a, the master volume 0x1ab6.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "gf1.h"

namespace modplay {

// Timer interrupts the capture serves before main stops the music: log ticks 1..22205 of full.gus (position 6d:28).
// The first two come before the player starts (the start is called after the second), so player tick k (k = 0 reads
// row 00:00) is timer interrupt k + 3.
constexpr int kCaptureTicks = 22205;
constexpr int kStartAfter = 2;

// Interrupts held off by the CPU (cli): an interrupt raised in [from, to) (card time, s) is served at `to`; a second
// timer overflow in the window finds the first unserved and is lost (notes section 7.3).
struct Hold {
    double from, to;
};

// One interrupt of a capture's log (the `irq` lines of full.gus): kind 't' (timer) or 'r' (ramp), service time, and
// for ramp IRQs the voices in the order the handler served them (its `r0d 3` writes).
struct CaptureIrq {
    char kind = 't';
    double time = 0;
    std::vector<int> order;
};

struct Options {
    int ticks = kCaptureTicks;  // timer interrupts before the stop
    // Schedule: the capture's service times (the check; ticks is then the length of the list), or the hardware's:
    // every interrupt at its event (ramp ends + 0.1 us, as the emulator raises them) unless a hold delays it.
    const std::vector<CaptureIrq>* capture = nullptr;
    bool captureOrder = true;   // with capture: queue simultaneous ramp ends in the capture's order
    std::vector<Hold> holds;    // without capture
    uint32_t eax = 0x808;       // EAX at the tick call ([0x22c0], read by 4xx): 0x808 in the capture
    uint32_t ebx = 0;           // EBX of the code the ramp IRQ interrupts (bit 15 -> reg 3/5/0x0b bit 8): 0 there
};

struct Position {
    int order = 0, row = 0, countdown = 0;  // [0x1aa0], [0x1a9c], [0x1a9b]
    bool operator==(const Position&) const = default;
};

struct Run {
    std::vector<double> timerEvent;  // per timer interrupt (log tick n = index + 1): the overflow (card time, s)
    std::vector<double> timerServed; // the time its handler ran
    std::vector<Position> pos;       // the position after it (for the start's interrupt: after the start)
    double stopTime = 0;   // the stop: at the timer overflow after the last tick (no sound after it)
    uint32_t dramEnd = 0;  // end of the uploaded samples
    int irqs = 0;          // interrupts served
    int early = 0;         // capture schedule: interrupts served more than 1.5 us before their event
    int late = 0;          // interrupts served more than 1.5 us after their event
    double maxLate = 0;
    int stale = 0;         // ramp IRQs served for a voice whose switch was done
    int notes = 0;         // note starts read from the pattern (0x2379, not tone portamento)
};

// Runs the GUS init 0x45b4 at card time 0, the module load 0x2d15 (instantly), the interrupts of the schedule
// (the start 0x2c17 after the second timer interrupt) and the stop 0x2cff at Run::stopTime, on `card`. Throws
// std::runtime_error when the module or the loader image is not what the player expects.
void run(std::span<const uint8_t> mod, std::span<const uint8_t> loader, gus::Card& card, const Options& opt, Run& out);

}  // namespace modplay
