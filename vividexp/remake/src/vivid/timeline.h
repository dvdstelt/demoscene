// The running order: the two scripts' lines with the refreshes at which they run, the segments the scenes draw, and
// the palette the DAC holds at any refresh (vividexp/re/NOTES.md "The script", notes-core.md).
//
// The interpreter (0x40b760) runs the lines in order. A command without a music position (POS "xxx") runs and the next
// line follows at once; a command with one draws or waits, polling the position once per refresh, until the
// music has reached it (order >= POS and row >= PAT, Soundtrack::Song::reached). The remake runs every command in no
// time, as an infinitely fast machine would (the capture's loads and inits take 0 to 14 refreshes); a line's `start`
// is the refresh its command begins at and its `end` the refresh the next line begins at.
//
// What is on screen: the effect of a Run command (RunBITMAPEFEK, RunBUMPEFEK, RunZOOMSYNC, WaterEffect,
// GenericEffect) from its start to its end, and between them the pictures the PutPCXSYNC commands copied into VGA
// memory since the last ClearSCR (the "pictures" scene). The palette is the DAC's: LoadPAL, the pictures' own
// palettes (InitPCX with flag 0), the water's palette, and the fades of InitFADE, stepped once per refresh by the
// wait loops and the bitmap and bump effects (not by the water and generic scenes).
#pragma once
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vivid/images.h"
#include "vivid/script.h"

class VividExe;
struct Soundtrack;

namespace vivid {

struct TimedLine {
    ScriptLine line;
    double start = 0, end = 0;     // refreshes: the command begins / the next line begins
};

struct Segment {
    std::string scene;             // "pictures", "zoom", "bitmap", "bump", "water", "generic"
    int part = 1;                  // 1 or 2
    double start = 0, end = 0;     // refreshes
    int run = -1;                  // index in Timeline::lines() of the Run command (-1: the pictures between effects)
    int init = -1;                 // ... of the Init command that set the effect up (-1: none)
};

// The lines of part `part` (1 or 2) with their refreshes, played against `song` (the songs' positions). The
// soundtrack uses it to find where part I ends, which is where part II's module starts (music.cpp).
std::vector<TimedLine> timeScript(const VividExe& exe, int part, const Soundtrack& music);

class Timeline {
public:
    bool build(const VividExe& exe, const Soundtrack& music, std::string* err);

    const std::vector<TimedLine>& lines() const { return lines_; }
    const TimedLine& line(int i) const { return lines_[size_t(i)]; }
    const std::vector<Segment>& segments() const { return segs_; }
    const Segment* segmentAt(double r) const;
    double end() const { return end_; }            // the refresh at which the demo ends (text mode)

    // The palette the DAC holds at refresh r.
    Palette palette(double r) const;

private:
    struct PalEvent {
        double at = 0;             // refresh from which it applies
        Palette from, to;          // a fade from `from` to `to` (a set: from == to)
        int steps = 0;             // refreshes the fade takes (0: a set)
        int first = 0, last = 255; // the entries the fade changes
    };
    double steppedRefreshes(double from, double to) const;   // refreshes of fade-stepping loops in [from, to)
    Palette paletteAt(size_t ev, double r) const;
    std::vector<TimedLine> lines_;
    std::vector<Segment> segs_;
    std::vector<PalEvent> pal_;
    std::vector<std::pair<double, double>> stepping_;   // [start, end) of the lines whose loops step the fades
    double end_ = 0;
};

}  // namespace vivid
