// STARS's music player: LCA's MOD-Player V3.02 (C) Cedric Bermond, GUS path, ported routine by routine from the
// program image (stars/re/notes-music.md; mirrors stars/re/models/music/lcaplay.py, which reproduces the capture's
// GUS register stream full.gus, GUS memory full.dram and music positions full.ticks).
//
// Routines (image offsets): 0x1ba6c detect (0x1bceb GF1 init and IRQ/DMA latches, 0x1bfda SB probe), 0x1a1b0
// module conversion (0x1a38e events, 0x1a3d3 sample upload with 0x1a699 / 0x1a76f / 0x1a7a6, 0x1a594 period ->
// FC table, 0x1a514 tempo), 0x1a91c start, 0x1a9ec GF1 reset, 0x1a9a2 stop, 0x1abb0 timer IRQ = player tick
// (0x1abea tick effects, table 0x1ac04; 0x1b210 row, table 0x1b298), 0x1b734 note start, 0x1b848 / 0x1b8a4 volume
// ramp and frequency write, 0x1c7a3 / 0x29f80 position.
//
// The tables are read from the program image by address: finetune periods 0x296c8, sine 0x29b48, volume 0x29bdc,
// pan 0x29bbb, voice-count divisors 0x29c5e, IRQ / DMA latch codes 0x29ba3 / 0x29bb3; the FC table is computed as
// 0x1a594 does, from the constant at 0x1a5cd and the divisor.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "gf1.h"

namespace lca {

// Player ticks the capture runs: main stops the music (0x1c77e -> 0x1a9a2) after tick 16870 (position 9a:34).
constexpr int kCaptureTicks = 16871;

// The player from the card detection to the stop, as the capture ran it.
struct Run {
    std::vector<int64_t> tickUs;  // tick k: microseconds after the GUS timers were started (0x1a91c)
    std::vector<int> pos;         // position word (order << 8 | row, the next row to be read) after tick k
    int pos0 = 0;                 // position before tick 0
    int64_t stopUs = 0;           // the timer IRQ after the last tick (where the capture's stop falls)
};

// Runs 0x1ba6c detect, 0x1a1b0 convert, 0x1a91c start, `ticks` timer IRQs and 0x1a9a2 stop on `card`, with the
// card's timers deciding when each tick runs. beforeTick(k, us) is called before tick k, and with k = ticks and
// Run::stopUs before the stop (card.tick is k for the tick's writes; the start-up writes carry tick 0, the stop
// ticks - 1, as in full.gus). Throws std::runtime_error on a module or image it cannot handle.
void run(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card, int ticks, Run& out,
         const std::function<void(int, int64_t)>& beforeTick = {});

}  // namespace lca
