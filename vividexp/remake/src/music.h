// Vivid Experiment's soundtrack: EXPRMNT1.XM ("Vivid" by Wode, part I) and EXPRMNT2.XM ("Experiment II" by Swallow,
// part II), played by a port of MIDAS Sound System 0.5a's XM player and its GUS sound device on a model of the GF1
// (xmplay.*, gusdev.*, gf1.*; vividexp/re/notes-music.md), driven by MIDAS's screen-synchronised timer (section 6) and
// rendered whole at load time.
//
// Clock: the remake's time is the screen refresh of the capture (demo.h): refresh k is record k of
// vividexp/re/full.frames, displayed at program time (k + 1) / 70.086 s. Music time t (seconds, the playback clock,
// t = 0 at refresh song[0].startRefresh = 23, the record in which part I's module starts) is refresh
// song[0].startRefresh + t * kRefreshHz; pcm frame 0 sounds at the display time of record 23 (program time 24 / 70.086
// s), and every GUS write sounds at its program time (its timer interrupt plus the card frames the device's ramp
// waits let pass before it). Each song reports the music position the script waits for: the order and row the demo
// reads at each refresh's retrace (0x400104, notes-core.md 9).
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vivid/timeline.h"

class VividExe;

struct Soundtrack {
    int rate = 48000;                 // stereo int16 interleaved (both channels the same: MIDAS plays mono here)
    std::vector<int16_t> pcm;         // sample 0 = music time 0 = refresh song[0].startRefresh

    // A row the player starts: `refresh` is the record from which the demo sees it, the first whose retrace comes
    // after the player's tick that read it (the waits poll the position at the retrace); `at` is that tick's instant
    // on the same scale (program time in refreshes - 1: when the row is heard).
    struct Row { int order, row; double refresh, at; };
    struct Song {
        double startRefresh = 0;      // the record in which the module starts playing
        double stopRefresh = 0;       // the record after whose retrace the demo stops it
        std::vector<Row> rows;        // every row the player plays, in time order
        // The position the demo reads at refresh r (the last row it sees at or before r); {0, 0} before the start.
        std::pair<int, int> positionAt(double r) const;
        // WaitSYNC (0x40279c) started at refresh `from`: at least one refresh, then the first refresh whose retrace
        // sees a position with order >= pos and row >= pat; +inf if never.
        double reached(int pos, int pat, double from) const;
    };
    Song song[2];                     // part I, part II
    double endRefresh = 0;            // the refresh at which the demo ends

    double refreshOf(double t) const;   // music time (s) -> refresh
    double timeOf(double refresh) const;
};

// The refresh at which the script of part `part` (1, 2) ends when played against `music`'s positions: part I's end
// starts part II, part II's ends the demo.
using ScriptEnd = std::function<double(int part, const Soundtrack& music)>;

// Renders both songs: part I's module starts in record 23 (as in the capture), part II's 12 records after part I's
// script ends (tmrSyncScr at the next retrace, then 10 refresh and 7 player interrupts, as in the capture). Throws
// std::runtime_error if the modules are not ones MIDAS loads. The player runs in well under a second; the mix is
// computed on worker threads.
Soundtrack renderSoundtrack(const VividExe& exe, std::span<const uint8_t> xm1, std::span<const uint8_t> xm2, int rate,
                            const ScriptEnd& scriptEnd);

// ... with the scripts' timing (vivid::timeScript: the last line's end). Inline so that tools that link the music
// without the framework (tools/test_music.cpp) need not link the timeline.
inline Soundtrack renderSoundtrack(const VividExe& exe, std::span<const uint8_t> xm1, std::span<const uint8_t> xm2,
                                   int rate) {
    return renderSoundtrack(exe, xm1, xm2, rate, [&exe](int part, const Soundtrack& music) {
        const std::vector<vivid::TimedLine> lines = vivid::timeScript(exe, part, music);
        return lines.empty() ? music.song[part - 1].startRefresh : lines.back().end;
    });
}

// ---- for checks (tools/test_music.cpp) ----

// One part as a run plays it: tmrSyncScr waits for the retrace of syncRefresh, midasPlayModule runs after the
// startAfter-th interrupt from there, midasStopModule after the retrace of stopRefresh.
struct MusicPart {
    std::span<const uint8_t> xm;
    int syncRefresh = 4, startAfter = 31, stopRefresh = 0;
};
// A run of one or more parts (the whole demo: both; `vivid.exe 2`: part II) from the device's first Detect to the
// exit, as vividexp/re/models/music/player.py runs it. fakeGus: the capture's fake card answers the device's reads
// (vividexp/re/full.gus, part2.gus); else the card's model does, as on hardware (render.py).
struct MusicTrace {
    std::string gus;                  // register writes in full.gus's text format (stamp: the timer interrupt)
    std::vector<uint8_t> dram;        // the card's DRAM at the end
    struct Tick { int irq; double clock; int part, order, row; };   // every player tick of the GMP, PIT clocks
    std::vector<Tick> ticks;
    std::vector<int> stopIrq;         // per part: the interrupt after which its music stops
    int64_t writes = 0, passes = 0;   // register writes; passes of the ramp waits that let the card run (model)
};
MusicTrace traceMusic(const VividExe& exe, std::span<const MusicPart> parts, bool fakeGus);
