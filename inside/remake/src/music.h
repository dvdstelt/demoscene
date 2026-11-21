// Inside's soundtrack ("eyegabo0om" by Yolk and Legend, EYE.AMF): DSMI's AMP 2.00 player as the demo drives it,
// on a model of the GUS (inside/re/notes-music.md), rendered whole at load time.
//
// Clock: the player runs in the demo's timer callback, 4 per screen refresh (demo.h), on DSMI time that advances 35
// units per callback (inside/re/notes-core.md section 1). The callback count M (1 at the music start) is the master
// clock: record k of the capture follows the retrace callback M = 4k - 150. Song row R starts in callback
// rowStart[R]. pcm frame 0 sounds at the display time of record startRefresh (= 37); music time t (seconds, the
// playback clock) is refresh startRefresh + t * kRefreshHz.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class InsideExe;

struct Soundtrack {
    int rate = 48000;                 // stereo int16 interleaved
    std::vector<int16_t> pcm;
    double startRefresh = 0;          // refresh at which pcm frame 0 plays
    double endRefresh = 0;            // refresh at which the demo ends (after the last script entry)

    // The callback in which song row R (= order * 64 + row: the song plays its orders in sequence) starts.
    std::vector<int> rowStart;
    int positionAtCallback(double m) const;      // the row the script sees after callback m (-1 before the first)
    int positionAt(double refresh) const;        // ... at record `refresh` (after its retrace callback)
    double rowCallback(int row) const;           // rowStart[row] (+inf past the end)
    double refreshOf(double t) const;            // music time t (s) -> refresh
    double timeOf(double refresh) const;         // refresh -> music time t (s)
};

// amf: EYE.AMF. Throws std::runtime_error if it is not the module the player expects.
Soundtrack renderSoundtrack(const InsideExe& exe, std::span<const uint8_t> amf, int rate);
