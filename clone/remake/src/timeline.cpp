// The running order: main 0x4001ab draws the part its variable [ebp-0x14] names (the switch 0x400e80) and the sync
// events 1..11 and 111 of the table 0x420499 set that variable (clone/re/notes-core.md section 4, PARTS.md). So a cue
// runs from the music time of its event (when xmpGetPos reaches the row, Ctx::positionTime) to the next one; the
// original changes part at the first frame after that (up to one frame later, PARTS.md has the capture's frames).
//
// Each part integrates dt once per frame, starting with the dt of its first frame, so its clock starts at the timer
// of the frame before: the previous part's last frame, 0 to one frame (up to 17 ms) before the event. The origins
// are those of the capture (notes-core 5.3, measured at 150 M instructions per second: full.flips column 4 / 17043).
// Part 1 starts with the image's dt = 6.0 and t70_prev = 0 (notes-core 5.2), and its second run (event 111, no
// set-up) continues the clock of the first.
//
// Scene names are those of src/scenes (clone/re/PORT.md has the porting jobs).
#include "demo.h"

namespace {
struct PartInfo {
    int part;            // main's part number
    const char* scene;   // src/scenes name
    uint32_t routine;    // 0x400e80's target
    double fps;          // the capture's frame rate in the part (PARTS.md)
};
// clang-format off
constexpr PartInfo kParts[] = {
    {1,  "swarm",   0x405527, 45.8},  // violet particle swarm, radial blur, the title (particles)
    {11, "tiles",   0x419dc7, 725},   // mosaic of tiles with the title box (grid)
    {2,  "tube",    0x4198bf, 40.5},  // the flat tube, "meet us" (grid)
    {8,  "blob",    0x417929, 54.0},  // geosphere blob with map B flying in, band of tiles (parts3d)
    {6,  "ring",    0x406677, 28.9},  // blue particle ring with radial blur (particles)
    {9,  "cubes",   0x4185e5, 54.6},  // metaballs as cubes with map C (parts3d)
    {5,  "zoom",    0x403530, 41.6},  // the warped rotating zoom, "get them clones" (grid)
    {7,  "bumps",   0x416fce, 72.2},  // geosphere with map A, block of tiles (parts3d)
    {3,  "dots",    0x405c45, 51.9},  // blue field of soft dots, "connect the dots" (particles)
    {4,  "tunnels", 0x4037c0, 39.0},  // three crossing tunnels, "tunnels meet more tunnels" (grid)
    {10, "slabs",   0x41958e, 83.1},  // metaballs as slabs with map B (parts3d)
};
// clang-format on

// The capture's clock origin of each part: t70 of the frame before its first frame (notes-core 5.3).
double captureOrigin(int part, bool again) {
    switch (part) {
        case 1: return again ? 14438.8363 : 0.0;  // first run: t70_prev = 0 (the image's value)
        case 11: return 2114.1087;
        case 2: return 2986.3272;
        case 8: return 3981.1581;
        case 6: return 5071.0155;
        case 9: return 5973.9528;
        case 5: return 7964.5946;
        case 7: return 8960.4406;
        case 3: return 10206.2020;
        case 4: return 12073.6714;
        case 10: return 13442.0276;
        default: return 0.0;
    }
}

// Part 1's integrated dt where its first run ends: 6.0 (the image's dt, its first frame) + t70 of its last frame
// (= part 11's origin; the dt telescope from t70_prev = 0).
constexpr double kPart1FirstDt = 6.0;
}  // namespace

std::vector<Cue> buildTimeline(const Ctx& c) {
    std::vector<Cue> q;
    for (const SyncEntry& e : c.sync) {
        int part = e.event == 111 ? 1 : (e.event >= 1 && e.event <= 11 ? e.event : 0);
        if (!part) continue;
        const PartInfo* info = nullptr;
        for (const PartInfo& p : kParts)
            if (p.part == part) info = &p;
        if (!info) continue;
        bool again = e.event == 111;
        Cue k;
        k.scene = info->scene;
        k.part = part;
        k.routine = info->routine;
        k.event = e.event;
        k.start = e.time;
        k.origin = c.t70ToTime(captureOrigin(part, again));
        k.clock0 = part == 1 ? kPart1FirstDt + (again ? captureOrigin(11, false) : 0.0) : 0.0;
        k.fps = again ? 26.6 : info->fps;  // part 1's second run: 26.6 fps in the capture
        if (!q.empty()) q.back().end = k.start;
        q.push_back(k);
    }
    if (!q.empty()) q.back().end = introLength(c);
    return q;
}

// Event -1 (3c:3f) sets the end flag; the loop ends at the top of the next frame, then xmpStop. In the capture that
// is after interrupt 11713, three player ticks after the row was read (notes-music section 1; frame-rate dependent).
double introLength(const Ctx& c) {
    double endEvent = c.eventTime(-1);
    return (std::isfinite(endEvent) ? endEvent : 0.0) + 3 * c.tickPeriod();
}
