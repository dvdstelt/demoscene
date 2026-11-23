// The running order: the script table 0x205ed0 (reve/re/NOTES.md "The script table", PARTS.md). Main runs the entries
// in order; an entry lasts until the timer interrupt sees the music at or past its end position, at the retrace of
// refresh P = music.refreshOfPosition(end position). The main loop then finishes the frame it draws (shown at
// record P + 1), calls the entry's done routine and the next entry's init with the empty retrace routine 0x20483f
// set, and sets the next entry's retrace routine after the init: its first retrace call is at refresh S = P + the
// init's time. The init mostly unpacks files, so its time depends on the machine; the remake takes the capture's
// (reve/re/full.log "retrace routine set at frame S", 100 million instructions per second, the Pentium the NFO
// asks for), so that every entry runs from the same record as in the capture: record r shows the state after
// r - S retrace calls (notes-parts2d-a.md, notes-parts2d-b.md). Meanwhile the screen holds the last frame, refresh
// P + 1, unless the next init clears the screen (most do, first thing): then it is black from P + 1 (P + 2 for entry
// 22, whose init first loads a picture). Both measured on the capture's records P + 1 .. S - 1.
//
// Scene names are those of src/scenes (reve/re/PORT.md has the porting jobs).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "demo.h"
#include "music.h"

namespace {
// The scene that draws each script entry (PARTS.md); entries not listed show a placeholder named entryNN.
struct EntryScene {
    int entry;
    const char* scene;
};
// clang-format off
constexpr EntryScene kScenes[] = {
    {0, "title"},                                                  // GFX\REVE.RAW
    {1, "credits"}, {2, "credits"}, {3, "credits"}, {4, "credits"},  // 3D\INTRO.WLD and the names
    {5, "pulse"}, {6, "pulse"},                                    // GFX\PULSE.RAW, its negative
    {7, "wiz"},                                                    // GFX\WIZ.RAW over two rotozooms
    {8, "p3d1"},                                                   // 3D\P3D1.WLD
    {9, "playmo"}, {10, "playmo"},                                 // GFX\PLAYMO.RAW in a spiral
    {11, "part2"},                                                 // 3D\PART2.WLD
    {12, "dots"}, {13, "dots"},                                    // dots on a spiral, GFX\SPIRBLUR.PAL
    {14, "p3d3"},                                                  // 3D\P3D3.WLD
    {15, "bouche"},                                                // GFX\BOUCHE.RAW in a spiral
    {16, "junior"}, {17, "mrtommy"},                               // pictures
    {18, "dada"}, {19, "dada"},                                    // GFX\DADA.RAW smeared
    {20, "gene"}, {21, "gene"},                                    // GFX\GENE.RAW
    {22, "part4"},                                                 // 3D\PART4.WLD
    {23, "tomboz"},                                                // GFX\TOM.RAW and GFX\BOZ.RAW warped
    {24, "dog"}, {25, "waaf"},                                     // pictures
    {26, "fin"},                                                   // 3D\FIN.WLD
    {27, "bouge"}, {28, "bouge"},                                  // GFX\BOUGE.RAW
    {29, "yocol"},                                                 // GFX\YOCOL.RAW
};
// After the last entry main runs 0x20538d (the title again) and waits 210 refreshes before it stops the music.
constexpr const char* kEndScene = "end";
// The refreshes from the music position that ends the entry before (for entry 0: the music start) to the first
// retrace call of each entry in the capture, and for the closing title 0x20538d (measured: full.log).
constexpr int kInitRefreshes[] = {3, 5, 2, 1, 1, 2, 1, 1, 9, 4, 1, 7, 4, 1, 5,
                                  4, 2, 2, 2, 1, 3, 1, 12, 3, 1, 2, 5, 3, 1, 2};
constexpr int kEndInitRefreshes = 1;
// The record, after P, from which the screen is black until the entry's first frame (0: the last frame holds).
constexpr int kGapBlack[] = {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
                             1, 1, 0, 0, 0, 1, 0, 2, 1, 0, 1, 1, 1, 0, 1};
// The records before the first retrace call from which the entry's first frame is on screen (its init draws it).
constexpr int kShownEarly[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0,
                               0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// clang-format on

std::string sceneOf(int entry) {
    for (const EntryScene& e : kScenes)
        if (e.entry == entry) return e.scene;
    char b[16];
    std::snprintf(b, sizeof b, "entry%02d", entry);
    return b;
}
}  // namespace

std::vector<Cue> buildTimeline(Ctx& c) {
    std::vector<Cue> cues;
    const double inf = std::numeric_limits<double>::infinity();
    double pos = c.music ? c.music->startRefresh : 0.0;   // P: where the entry before ended (the music start)
    for (ScriptEntry& e : c.script_) {
        const int n = int(sizeof kInitRefreshes / sizeof kInitRefreshes[0]);
        e.start = pos + (e.index < n ? kInitRefreshes[e.index] : 0);
        pos = c.music ? std::max(e.start, c.music->refreshOfPosition(e.endPos)) : inf;
        const int next = e.index + 1;
        const int gap = next < int(sizeof kGapBlack / sizeof kGapBlack[0]) ? kGapBlack[next] : 1;
        const int early = e.index < int(sizeof kShownEarly / sizeof kShownEarly[0]) ? kShownEarly[e.index] : 0;
        cues.push_back({sceneOf(e.index), e.index, e.start, inf, pos + 1, gap ? pos + gap : inf, e.start - early});
    }
    // the closing title after the last entry, up to the end of the demo
    const double end = demoEnd(c);
    if (!cues.empty() && std::isfinite(pos) && end > pos)
        cues.push_back({kEndScene, int(c.script_.size()) - 1, pos + kEndInitRefreshes, end, inf, inf,
                        pos + kEndInitRefreshes});
    for (size_t k = 0; k < cues.size(); k++) {
        double next = k + 1 < cues.size() ? cues[k + 1].start : end;
        cues[k].end = std::max(cues[k].start, next);
        if (k < c.script_.size()) c.script_[k].end = cues[k].end;
    }
    return cues;
}

double demoEnd(const Ctx& c) { return c.music ? c.music->endRefresh : 0.0; }
