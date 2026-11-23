// The running order: the script table 0x205ed0 (reve/re/NOTES.md "The script table", PARTS.md). Main runs the entries
// in order; an entry lasts until the timer interrupt sees the music at or past its end position, at a retrace.
//
// The original then finishes the frame it is drawing, calls the entry's done routine and the next entry's init (which
// may load files for several refreshes) before the next retrace routine runs; the remake switches at the retrace at
// which the position is reached, as the original would on an infinitely fast machine: entry i + 1 starts at
// music.refreshOfPosition(endPos of entry i). The first entry starts when the music starts.
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
    double start = c.music ? c.music->startRefresh : 0.0;
    for (ScriptEntry& e : c.script_) {
        double end = c.music ? c.music->refreshOfPosition(e.endPos) : std::numeric_limits<double>::infinity();
        e.start = start;
        e.end = std::max(start, end);
        cues.push_back({sceneOf(e.index), e.index, e.start, e.end});
        start = e.end;
    }
    double end = demoEnd(c);
    if (!cues.empty() && end > start) cues.push_back({kEndScene, int(c.script_.size()) - 1, start, end});
    return cues;
}

double demoEnd(const Ctx& c) { return c.music ? c.music->endRefresh : 0.0; }
