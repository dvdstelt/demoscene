// Reve's music player: LCA's MOD-Player V3.03 (C) Cedric Bermond, GUS path, as REVE.LFL links it, ported routine
// by routine from the program image (reve/re/notes-music.md; mirrors reve/re/models/music/lcaplay.py, which
// reproduces the capture's GUS register stream full.gus, GUS memory full.dram and timer interrupts and music
// positions full.ticks). V3.03 is V3.02 (STARS) with the differences of notes-music.md section 0.
//
// Routines (linear addresses, image at 0x20427c): 0x25f698 detect (0x25f8d7 GF1 init and IRQ / DMA latches; the SB
// probe 0x25fbc6 only runs without a GUS), 0x2601dc load -> 0x26e16c module conversion (standard 31-sample MOD:
// 0x26e31b events, 0x26e36f sample upload with 0x26e9bb / 0x26ea91 / 0x26eac8, 0x26e8ba period -> FC table,
// 0x26e4aa tempo), 0x26ec3e start, 0x26ed0e GF1 reset, 0x26ecc4 stop, 0x26eecc timer IRQ = player tick (0x26ef18 tick
// effects, table 0x26ef34; 0x26f544 row, table 0x26f5cc), 0x26fa68 note start, 0x26fb7c / 0x26fbd8 volume ramp and
// frequency write, 0x2602eb position.
//
// The tables are read from the program image by address: finetune periods 0x26db4c, sine 0x26dfcc, volume 0x26e060,
// pan 0x26e03f, voice-count divisors 0x26e0e2, IRQ / DMA latch codes 0x26e027 / 0x26e037, the initial voice count
// [0x26e012]; the FC table is computed as 0x26e8ba does, from the constant at 0x26e8f3 and the divisor.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "gf1.h"

namespace lca {

constexpr uint32_t kImageBase = 0x20427c;   // REVE.LFL's image as the extender loads it in the headless run
// Where the extender loads ZIC\PULSE.MOD in the headless run (reve/re/full.log, service 0209h): the sample DMA blocks
// split at the 64 KB pages of this physical address, which only changes the register log (not the DRAM).
constexpr uint32_t kModuleAddr = 0x345e30;

// The player from the card detection to the stop.
struct Run {
    std::vector<int64_t> tickUs;  // tick k: microseconds after the GUS timers were started (0x26ec3e)
    std::vector<int> pos;         // position word (order << 8 | row, the next row to be read) after tick k
    int pos0 = 0;                 // position before tick 0
    int64_t stopUs = 0;           // when main stops the music (0x2602c6), set by the stop rule
};

// Called before every timer IRQ with the run so far and the time of that IRQ (us after the timer start); returns
// true when main stops the music before it, and then sets run.stopUs (<= nextUs).
using StopRule = std::function<bool(Run& run, int64_t nextUs)>;

// Runs 0x25f698 detect, 0x26e16c convert, 0x26ec3e start, the timer IRQs the card's timers raise until `stop` says
// so, and 0x26ecc4 stop, on `card`. beforeTick(k, us) is called before tick k, and with k = ticks and Run::stopUs
// before the stop. card.tick is the log stamp: the start-up writes carry 0, tick k's writes k + 1 (run_reve.py
// stamps writes with the GUS timer interrupts counted so far), the stop the number of ticks. image: REVE.LFL's
// image (at kImageBase); module: ZIC\PULSE.MOD. Throws std::runtime_error on a module or image it cannot handle.
void run(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card, const StopRule& stop,
         Run& out, const std::function<void(int, int64_t)>& beforeTick = {});

}  // namespace lca
