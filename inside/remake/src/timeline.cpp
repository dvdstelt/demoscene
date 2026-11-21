// The running order: the script table 0x4222e4 (NOTES.md "The script", PARTS.md) runs an entry's routine once the
// music reaches its position; the routines below set a part's tick and frame routines (0x400af4 / 0x400b04), the
// "off" routines clear them (the screen then keeps its last frame, which the fades have turned black). A cue runs
// from the refresh at which its start entry runs to the next start or off entry.
//
// The original runs an entry at the end of the first frame the main loop draws after the music reached the position,
// so up to one drawn frame late; the remake runs it at the refresh at which the position is reached
// (Soundtrack::positionRefresh).
//
// Scene names are those of src/scenes (inside/re/PORT.md has the porting jobs).
#include <algorithm>
#include <limits>

#include "demo.h"
#include "music.h"

namespace {
struct PartStart {
    uint32_t routine;      // the script entry's routine
    const char* scene;     // src/scenes name
    uint32_t frameRoutine; // the frame routine it sets (PARTS.md)
};
// clang-format off
constexpr PartStart kStarts[] = {
    {0x4038c8, "intro",   0x403748},   // light drops over the intro picture
    {0x403164, "logo",    0x402f1c},   // the twisted logo on the red background
    {0x4073c8, "credits", 0x406794},   // the credits tunnel
    {0x4057bc, "sprites", 0x4047a0},   // the pictures of people and the shadow scene
    {0x4098c4, "flower",  0x4095f8},   // the green tunnel and the flowers
    {0x402c28, "flight",  0x4028e4},   // the flight over the landscape
    {0x407f90, "water",   0x407d78},   // under water
    {0x40585c, "sprites", 0x4047a0},   // the pictures of people again
    {0x40a678, "twirl",   0x40a264},   // the twirl
    {0x40461c, "temple",  0x404424},   // the temple of pipes
};
// The routines that end a part (they clear the part's routines and free its memory).
constexpr uint32_t kOffs[] = {0x4038dc, 0x403184, 0x4073e8, 0x404728, 0x409e90, 0x402c7c, 0x408014, 0x40a738,
                              0x404668};
// clang-format on
}  // namespace

std::vector<Cue> buildTimeline(Ctx& c) {
    // entries run in table order (0x400b3c), so an entry never runs before the one in front of it (30:12 follows
    // 30:48 in the table and runs with it)
    double prev = -std::numeric_limits<double>::infinity();
    for (ScriptEntry& e : c.script_) {
        e.refresh = e.routine ? std::max(prev, c.positionRefresh(e.order * 64 + e.row))
                              : std::numeric_limits<double>::infinity();
        prev = e.refresh;
    }
    std::vector<Cue> cues;
    for (const ScriptEntry& e : c.script_) {
        bool off = false;
        for (uint32_t o : kOffs) off |= e.routine == o;
        const PartStart* s = nullptr;
        for (const PartStart& p : kStarts)
            if (p.routine == e.routine) s = &p;
        if ((off || s || !e.routine) && !cues.empty() && cues.back().end > e.refresh) cues.back().end = e.refresh;
        if (s) cues.push_back({s->scene, s->frameRoutine, e.refresh, std::numeric_limits<double>::infinity()});
    }
    if (!cues.empty() && !std::isfinite(cues.back().end)) cues.back().end = demoEnd(c);
    return cues;
}

double demoEnd(const Ctx& c) { return c.music ? c.music->endRefresh : 0.0; }
