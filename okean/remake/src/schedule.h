// The intro's clocks (okean/re/notes-core.md section 4): when the player's ticks are served, when each part starts and
// ends, and the master volume of the end part's fade. Everything is computed from the song's positions per tick
// (music.h songTicks) and the parts' exit rules; it reproduces the capture's 1,494 row times and part changes.
//
// - Refresh k starts at program time k T (T = 808 x 625 / 25.175 MHz = 20.0596 ms); its retrace, where every part's
//   callback runs, at line 502 (k T + 16.112 ms); the main loops copy their frame at line ~504, test their exit
//   condition right after the copy, and draw the next frame from there.
// - GUS timer 2 expires every 19.84 ms from program time 0. Expiries 1..3 come before the player starts; player tick
//   j is the j-th interrupt served after them. Each part's timer calibration keeps interrupts off over a window: the
//   first expiry in it is served at its end, a second one is lost (the GUS latches one IRQ). The title's window runs
//   from refresh 3 line 106 to refresh 4 line 504; every later part's from line 504 of the previous part's exit
//   refresh X to line 504 of X + 2. The part's first callback comes in refresh F = X + 3 (the title's: 5).
// - Record k of the capture shows the frame drawn after n = k - F callbacks of the part showing then.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "music.h"

class OkeanExe;

struct PartSchedule {
    std::string scene;     // src/scenes name
    uint32_t entry = 0;    // the routine main calls (code32)
    int enter = 0;         // the refresh in which main calls it: the previous part's exit refresh X (its last copy)
    int first = 0;         // F = enter + 3: the refresh of its first callback = the record of its first frame
    int end = 0;           // its own exit refresh X: from that record on the next part's set-up shows
    bool clears = false;   // records enter..first-1 are black (its set-up clears video memory, record X included);
                           // else they keep the previous part's last frame (the one copied in X)
};

struct IntroSchedule {
    MusicSchedule music;
    std::vector<PartSchedule> parts;
    int exitRefresh = 0;   // the refresh in which the intro sets text mode (the end part's last refresh)
};

constexpr double kLine = 1.0 / 625.0;       // one scan line in refreshes
constexpr double kCallbackLine = 502.0;     // the retrace: the callbacks
constexpr double kCopyLine = 504.0;         // the main loops' copy and exit test
constexpr double kTickSeconds = 62 * 320e-6;  // GUS timer 2 at count 0xc2

IntroSchedule buildSchedule(const SongTicks& song);
