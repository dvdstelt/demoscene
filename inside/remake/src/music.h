// Inside's soundtrack ("eyegabo0om" by Yolk and Legend, EYE.AMF): DSMI's AMP 2.00 player as the demo drives it,
// on a model of the GUS (inside/re/notes-music.md), rendered whole at load time.
//
// Clock: the player runs in the demo's timer interrupt, 4 per screen refresh (demo.h), on DSMI time that advances 35
// units per interrupt. So the music is locked to the refresh: everything here is in refreshes of the capture
// (inside/re/full.frames record k = refresh k). pcm frame 0 sounds at refresh startRefresh; music time t (seconds,
// the playback clock) is refresh startRefresh + t * kRefreshHz.
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

    // The music position order * 64 + row the script sees (DSMI's current order and row, NOTES.md "The script")
    // from refresh `firstRefresh` on, one entry per refresh (the value after the refresh's last interrupt).
    int firstRefresh = 0;
    std::vector<int> positions;
    int positionAt(double refresh) const;        // -1 before the music starts
    double positionRefresh(int pos) const;       // first refresh whose position is >= pos (+inf if never)
    double refreshOf(double t) const;            // music time t (s) -> refresh
    double timeOf(double refresh) const;         // refresh -> music time t (s)
};

// amf: EYE.AMF. Throws std::runtime_error if it is not the module the player expects.
Soundtrack renderSoundtrack(const InsideExe& exe, std::span<const uint8_t> amf, int rate);
