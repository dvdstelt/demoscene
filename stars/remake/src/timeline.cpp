// The running order: main's script 0x4d (stars/re/notes-core.md section 2) as music times. main calls the parts one
// after the other; before most of them it waits until the music position passes a value (`call 0x29f80; cmp ax, x;
// jbe`, ctx.afterPos(x)). So a part starts at the later of
//   - its position (from the music: exact), and
//   - the moment main got there: the end of the part before plus the set-up routines in between. A part's end is
//     its own end test (a PIT tick count, a music position, retraces) and exact; the set-up times depend on the CPU
//     and are taken from the capture (stars/re/full.frames at 40 M instructions per second): the first frame the
//     part drew there, capture(f).
// Between a part's return and the next part's first frame the screen keeps whatever the part left: its last frame
// (Cue::hold) when it did not clear the screen, black otherwise.
// Scene names are those of src/scenes (stars/re/PORT.md has the porting jobs).
#include <algorithm>
#include <cmath>

#include "demo.h"

namespace {
// Music time at which frame f of the capture starts (its line 0).
double capture(int f) { return f / kRetraceHz - kCaptureMusicStart; }
}  // namespace

std::vector<Cue> buildTimeline(const Ctx& c) {
    std::vector<Cue> q;
    double t = 0;  // where main is in its script
    auto ticks = [](double n) { return n / kPitHz; };
    auto retraces = [](double n) { return n / kRetraceHz; };
    auto wait = [&](int x) { t = std::max(t, c.afterPos(x)); };   // busy loop on the music position
    auto notBefore = [&](int frame) { t = std::max(t, capture(frame)); };  // set-up time measured in the capture
    auto part = [&](const char* scene, uint32_t routine, double end) {
        q.push_back({scene, routine, t, end, end});
        t = end;
    };
    auto holdUntilNext = [&] { q.back().hold = t; };  // call when the next part starts: its last frame stayed

    // 0x5a92: title pictures and name cards, orders 0..0x0a. It installs its PIT callback at the music start and
    // returns when picture 10 has faded out: fade_out(-2, 128) = 129 retraces after row 0x2e of order 0x0a.
    part("intro", 0x5a92, c.afterPos(0x0a2e) + retraces(129));
    // 0x5b8f free, 0x1241b cloud precalc (black palette)
    wait(0x0b00);
    part("cloud", 0x126ae, t + ticks(520));  // returns at tick 0x208 (notes-parts3d-a 2.4)
    // 0x123d8 free, 0x11d8e bee set-up: the last cloud frame stays
    notBefore(5947);
    holdUntilNext();
    part("bee", 0x11e7f, t + ticks(1068));  // fade out ends at tick 1068 (3.4), then clears the screen
    // 0x11f48 free, 0x65ef spike ball set-up (black)
    wait(0x1700);
    part("spikeball", 0x6534, c.positionTime(0x2700));  // ends when the order is 0x27 (4.4)
    // 0x65ad free, 0x9d17 warp set-up: the last spike ball frame stays
    wait(0x2700);
    holdUntilNext();
    {
        // 0x975b: five blocks of 0x104 + 1 and 4 x (0x186 + 1) ticks; each waits for its position (notes-parts2d-a 2.4)
        double b = t + ticks(0x104 + 1);
        for (int pos : {0x2800, 0x2b00, 0x2e00, 0x3100}) b = std::max(b, c.afterPos(pos)) + ticks(0x186 + 1);
        part("warp", 0x975b, b);  // clears the screen
    }
    // 0x9dcf free, 0x1064 tunnel set-up (black)
    wait(0x3500);
    part("tunnel", 0x11e8, c.positionTime(0x4500));  // ends when the order passes 0x44 (3.4), clears the screen
    // 0x110d free, 0xa14d set-up of the textured surface: it ends after the position
    wait(0x4500);
    notBefore(13810);
    part("wall", 0xa0ca, t + ticks(2157));  // ends at tick 2157 (notes-parts3d-b 3.6)
    // 0xa346 free; main's picture 57: shown with a black palette, faded in at 0x5500 and out at 0x5800 (64
    // retraces each), screen cleared; then main waits for 0x5820
    part("ornament", 0, c.afterPos(0x5820));
    part("star", 0x164dc, t + ticks(1806));  // ends at tick 1806 (notes-parts3d-b 4.2); main clears the screen
    // 0x6100 has passed; 0x13388 set-up
    wait(0x6100);
    notBefore(18451);
    part("particles", 0x133ad, c.positionTime(0x6900));  // ends when the order passes 0x68 (notes-parts2d-b 4.2)
    // 0x1339d free, clear, 0x1528c dissolve set-up
    wait(0x6900);
    part("dissolve", 0x1535c, c.afterPos(0x6b30));  // ends at 6b:31 (5.2); main clears the screen
    // 0x1530a free, 0x16d86 torso set-up
    wait(0x6c00);
    part("torso", 0x16f7c, t + ticks(2447));  // ends by its fade at tick 2447 (5.2); main clears the screen
    // 0x174bd free, 0x13388 set-up, no wait
    notBefore(21956);
    part("particles2", 0x133f1, c.positionTime(0x8700));  // ends when the order passes 0x86
    // 0x1339d free, clear, 0x10b26 face set-up
    wait(0x8700);
    part("face", 0x1089c, c.afterPos(0x9230));  // ends at 92:31 (notes-parts2d-b 6.6)
    // 0x10bd9 free; main's credits (notes-core 2.2): the last picture (56) fades in at 0x9a20 (65 retraces), then
    // 20 retraces, the music stops, 180 retraces, fade out over 129 retraces, text mode: the end of the demo
    part("credits", 0, c.afterPos(0x9a20) + retraces(65 + 20 + 180 + 129));
    return q;
}
