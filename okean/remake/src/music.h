// The Sea Robot of Love's soundtrack (dune's 8-channel MOD at code32 0xae0 of KORSO2.EXE): the intro's ProTracker
// player on its GUS driver (modplay.*) and a model of the GUS (gf1.*), rendered whole at load time
// (okean/re/notes-music.md).
//
// Clock (okean/re/notes-core.md section 4, notes-music.md section 8): the player ticks on GUS timer 2, 19.84 ms per
// tick on the timer's own grid, independently of the screen; the parts follow its position. What the player does at
// a tick does not depend on when the tick comes, only the master volume [0x12b8c] (the end part's fade) does. So the
// song is first run for its positions (songTicks), schedule.cpp works out when each tick is served (the grid, the
// tick the first part's timer calibration holds off, the ticks lost at the later part starts) and when the parts
// change, and renderSoundtrack plays the song at those times.
//
// Program time 0 is the start of refresh 0 of the capture (refresh k starts at k / kRefreshHz). pcm frame 0 sounds
// at program time 0. The playback clock (music time t, seconds) is program time; the picture shown at t is the record
// the original's monitor shows then: record k is scanned out during refresh k + 1, so refreshOf(t) = t * Hz - 1.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

class OkeanExe;

// The song tick by tick, from the player (player tick j = 0 is the first tick, which reads row 0:00; the capture's
// interrupt n = j + 4).
struct SongTicks {
    std::vector<int> pos;           // order * 64 + row as [0x12b75] / [0x12b72] read after tick j
    std::vector<uint8_t> newRow;    // 1 where tick j reads a row (its countdown ran out)
};
// Runs the player for `ticks` ticks (no sound). Throws std::runtime_error if the module is not the one expected.
SongTicks songTicks(const OkeanExe& exe, int ticks);

// When the player's ticks are served and what the master volume is (schedule.cpp).
struct MusicSchedule {
    std::vector<double> tickTime;               // program time (s) at which player tick j is served
    std::vector<std::pair<double, int>> master; // (program time, [0x12b8c]) from then on; 63 before the first entry
    double stopTime = 0;                        // program time at which main stops the music (0x13f18)
};

struct Soundtrack {
    int rate = 48000;                 // stereo int16 interleaved
    std::vector<int16_t> pcm;         // from program time 0
    double endTime = 0;               // program time (s) at which the intro ends (text mode)

    // Every row the song plays, in order: its start (the program time of its tick) and its position.
    std::vector<double> rowTime;
    std::vector<int> rowPos;
    // The tick schedule and the song (for the parts and the timeline).
    MusicSchedule schedule;
    SongTicks song;

    int positionAtTime(double programTime) const;   // order * 64 + row then (-1 before the first row)
    int positionAt(double refresh) const;           // ... at the end of record `refresh` (as full.ticks)
    // The program time of the first row at or after position `pos` (order * 64 + row) that starts at or after
    // program time `after` (+inf if none).
    double timeOfPosition(int pos, double after = -1e9) const;
    double refreshOf(double t) const;               // music time t (s) -> the refresh (record) shown then
    double timeOf(double refresh) const;            // refresh -> music time t (s) at which it is shown
};

// Renders the soundtrack: the song played at the schedule's tick times on the GF1 model, from program time 0 to the
// stop, at `rate` frames per second. Fills pcm, rowTime / rowPos, schedule and song.
Soundtrack renderSoundtrack(const OkeanExe& exe, const SongTicks& song, const MusicSchedule& sch, int rate);
