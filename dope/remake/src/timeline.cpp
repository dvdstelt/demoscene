// The running order: CDPI (the loader's chain loader, 00EB:0131) runs p1..p7 one after the other while the player
// plays the module on the GUS timer; nothing waits for the music and nothing in the parts reads it
// (dope/re/notes-core.md sections 2, 5 and 6, PARTS.md). Each part times itself by the vertical retrace from its
// first int 8 handler run, and its length is a fixed number of retraces. So the schedule is a list of fixed times
// after the first player tick, taken from the reference capture (dope/re/full.frames: no disk time, 100 M
// instructions per second), on the capture's frame grid ("records", one per displayed frame, record f starting at
// program time f / 70.0863 s; music time = program time - c.clock.zero):
//   - a part starts (ctx.start) at the record of its first handler run, its set-up screen (ctx.load) where it
//     first changes the screen, and it ends (ctx.end) after its last frame;
//   - between parts the screen keeps the last frame where CDPI's loading leaves it (Cue::hold), black otherwise;
//   - three fades of the parts pace themselves by `while (!(in(0x3DA) & 8));` alone: p5's hand-over cross-fade
//     (3038, 65 waits), p6's exit fade (3d22, 37 steps) and p7's exit fade (33be, 257 steps). On a VGA each takes one
//     retrace per step; the emulator did about four per captured frame (notes-core 5.2). The remake takes one retrace
//     per step, so from p5's hand-over on it runs later than the capture: by 48 records after p5's fade (the swirl
//     appears at record 25636 instead of 25588), by 74 after p6's (black at 29672 instead of 29598), and the end
//     (text mode) by 266 (32386 instead of 32120). c.clock.sync maps the remake's records back to the capture's for
//     the comparisons (Ctx::captureFrame).
// Where the fade steps fall (j = the step's index, r0 = the record of the first step): the step is on screen in
// record r0 + j; the first step comes one retrace after the timer is restored, as in the capture (p5: r0 = 25572 =
// p5's start + 4481 retraces; p6: 29636 = start + 3991; p7: 32130 = start + 2451); the black and the text mode of
// p6 / p7 and the swirl of p5 are written right after the last step without another wait, so they replace it in
// the same record (p5: j = 64, p6: j = 36, p7: j = 256).
// Scene names are those of src/scenes (dope/re/PORT.md has the porting jobs).
#include <algorithm>
#include <cmath>

#include "demo.h"

std::vector<Cue> buildTimeline(Ctx& c) {
    // The capture's records of the three corrected fades and the remake's (see above).
    constexpr double kP5Fade = 25572, kP5Swirl = 25588;        // capture: first fade record, swirl alone
    constexpr double kP6Fade = 29588, kP6Black = 29598;        // capture: first fade record, black
    constexpr double kP7Fade = 32056, kP7Text = 32120;         // capture: first fade record, text mode
    constexpr double kP5Steps = 64, kP6Steps = 36, kP7Steps = 256;  // records from the first fade step to the end
    constexpr double kShift5 = kP5Fade + kP5Steps - kP5Swirl;            // 48
    constexpr double kShift6 = kShift5 + kP6Fade + kP6Steps - kP6Black;  // 74 (the end: 74 + 32056 + 256 - 32120 = 266)
    c.clock.sync = {
        {kP5Fade, kP5Fade},
        {kP5Fade + kP5Steps, kP5Swirl},
        {kP6Fade + kShift5, kP6Fade},
        {kP6Fade + kShift5 + kP6Steps, kP6Black},
        {kP7Fade + kShift6, kP7Fade},
        {kP7Fade + kShift6 + kP7Steps, kP7Text},
    };
    auto R = [&](double rec) { return c.clock.recordTime(rec); };

    std::vector<Cue> q;
    // Before p1: the loader's mode 13h, black (records 7..14; the text-mode start-up is not shown).
    // p1 0bd5: mode X, black; first handler run in record 15; the logo leaves at 2664, the 2689th run (record 2703)
    // makes pos 0x1502 and p1 exits (notes-core 4). Black after.
    q.push_back({"p1", 1, R(15), R(15), R(2704), R(2704)});
    // p2 376c: mode 13h at 2709 (black); first handler run 2713; the script's 153 steps end after retrace 7309
    // (record 10021, with the capture's step overshoot, notes-p2 9.2); the exit 43ed sets the whole DAC to 63: white
    // in records 10023..10025 (= p2's retraces 7310..7312), until p3 sets mode 13h in record 10026.
    q.push_back({"p2", 2, R(2713), R(2713), R(10026), R(10026)});
    // p3 2bec: mode 13h and the palette at 10026: the screen is colour 0 = (0, 0, 3) until the first frame; first
    // handler run 10032; 3605 retraces, the last state (3604) in record 13636; the plain nebula stays: p4 starts on
    // it (notes-p3p5 1).
    q.push_back({"p3", 3, R(10026), R(10032), R(13637), R(13651)});
    // p4 1caa: reads p3's screen and DAC (no visible change); first handler run 13651 (IRQ k in record 13650 + k);
    // 7430 IRQs, then the plain chequerboard (record 21081), which stays: p5 starts on it (notes-p4 1, 7).
    q.push_back({"p4", 4, R(13651), R(13651), R(21081), R(21091)});
    // p5 3089: first handler run 21091; 4480 retraces (records 21091..25570); the timer is restored, then the
    // hand-over cross-fade to p6's swirl (3038, one retrace per step from record 25572), the swirl alone from 25636;
    // it stays until p6 starts (CDPI and p6's set-up: 9 records, as in the capture).
    q.push_back({"p5", 5, R(21091), R(21091), R(kP5Fade + kP5Steps + 1), R(25597 + kShift5)});
    // p6 38e8: no mode set, starts on the swirl; first handler run 25645 (capture 25597); 3990 retraces; the exit
    // fade 3d22 (L = 256, 249, ..., 4 from record 29636), black from 29672. Black until p7's first frame.
    q.push_back({"p6", 6, R(25597 + kShift5), R(25597 + kShift5), R(kP6Fade + kShift5 + kP6Steps),
                 R(kP6Fade + kShift5 + kP6Steps)});
    // p7 3194: mode 13h (black); first handler run 29679 (capture 29605); 2100 retraces of script, "dope" for 350
    // retraces, the exit fade 33be (L = 256, ..., 0 from record 32130), then text mode (32386): the end of the demo
    // (TERM and CDPI print their lines in text mode and the loader stops the music at once).
    q.push_back({"p7", 7, R(29605 + kShift6), R(29605 + kShift6), R(kP7Fade + kShift6 + kP7Steps),
                 R(kP7Fade + kShift6 + kP7Steps)});
    return q;
}
