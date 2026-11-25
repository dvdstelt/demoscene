// Vivid Experiment's soundtrack: EXPRMNT1.XM ("Vivid" by Wode, part I) and EXPRMNT2.XM ("Experiment II" by Swallow,
// part II), played by a port of MIDAS Sound System 0.5a's XM player and its GUS sound device on a model of the GF1
// (xmplay.*, gusdev.*, gf1.*; vividexp/re/notes-music.md), rendered whole at load time.
//
// Clock: the remake's time is the screen refresh of the capture (demo.h): refresh k is record k of
// vividexp/re/full.frames. Music time t (seconds, the playback clock, t = 0 at part I's module start) is refresh
// song[0].startRefresh + t * kRefreshHz. Each song reports the music position the script waits for: the order and
// row the demo reads after each refresh's retrace (0x400104, notes-core.md).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

class VividExe;

struct Soundtrack {
    int rate = 48000;                 // stereo int16 interleaved
    std::vector<int16_t> pcm;         // sample 0 = music time 0 = refresh song[0].startRefresh

    struct Row { int order, row; double refresh; };   // a row the player starts, and the refresh it starts at
    struct Song {
        double startRefresh = 0;      // refresh at which the module starts playing
        double stopRefresh = 0;       // refresh at which the demo stops it
        std::vector<Row> rows;        // every row the player plays, in time order
        // The position the demo reads at refresh r (the last row started at or before r); {0, 0} before the start.
        std::pair<int, int> positionAt(double r) const;
        // The first refresh >= from at which the position (order, row) satisfies order >= pos and row >= pat, polled
        // once per refresh as WaitSYNC does (notes-core.md); +inf if never.
        double reached(int pos, int pat, double from) const;
    };
    Song song[2];                     // part I, part II
    double endRefresh = 0;            // the refresh at which the demo ends

    double refreshOf(double t) const;   // music time (s) -> refresh
    double timeOf(double refresh) const;
};

// Renders both songs. Throws std::runtime_error if the modules are not the release's.
Soundtrack renderSoundtrack(const VividExe& exe, std::span<const uint8_t> xm1, std::span<const uint8_t> xm2, int rate);
