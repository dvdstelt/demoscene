// The running order: main calls the parts one after the other (okean/re/NOTES.md "Main flow", PARTS.md); schedule.cpp
// works out when each one starts and ends. Scene names are those of src/scenes.
#include "demo.h"
#include "music.h"
#include "schedule.h"

std::vector<Cue> buildTimeline(Ctx& c) {
    std::vector<Cue> cues;
    for (const PartSchedule& p : c.schedule->parts)
        cues.push_back({p.scene, p.entry, double(p.enter), double(p.first), double(p.end), p.clears});
    return cues;
}

// The intro ends when text mode is set (the end part's last refresh, at its copy line): the record then on screen.
double demoEnd(const Ctx& c) { return c.music->refreshOf(c.schedule->music.stopTime); }
