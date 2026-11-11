// The global palette fade level [0x1edb5] for every tick of the intro, and the post pass that applies it to each
// finished frame (installed into the framework's postPass() by a static initializer in fade.cpp).
//
// The original's IRQ0 (0x1e45) moves the level by [0x1edb9] towards [0x1edbd] once per tick and re-uploads the DAC
// (0x15c0, see palette.h applyLevel). The main line only sets target/step: at part starts, when the music counter
// reaches a part's end ("fade out"), and the parts that wait for the level (title: until white; teal, greetings,
// flock: until black). The event ticks below were taken from an emulator run that reproduces the reference capture
// (hooked run_paper.py from the snapshots, the flock part at 34 fps as in full.frames), and the resulting level
// equals that run's [0x1edb5] at every tick 0..12234 and the capture's DAC palette at every tick.
//
//   tick   target  step   what
//    100   0x8000   150   end of init: fade in (title shown from 104; full at 318)
//   1046   0xff00  1000   title: counter >= 112 -> white (white at 1078, loop ends)
//   1080   0x8000   400   sketch: from white to normal (1161)
//   7631        0   300   teal: counter >= 896 -> black (7740)
//   7744   0x8000   400   greetings: fade in (7825)
//   8707        0   300   greetings: counter >= 1024 -> black (8816)
//   8818   0x8000   300   flock: fade in. Level is set to 300 first: the capture's harness gives the level one step at
//                          the first loop test, else a fast machine skips the part (NOTES.md); so 600 after tick 8818
//  10857        0   300   flock: counter >= 1280 -> black (10966)
//  10968   0x8000   400   "the end": fade in (11049)
//  11344   0xff00  1000   "the end": counter >= 1338 -> white (11376)
//  11377   0x8000   512   closing logo: from white to normal (11440); no fade at the exit
#pragma once

namespace paper {

// [0x1edb5] after the IRQ of tick `tick` (integer ticks: exact), linearly interpolated towards the next tick's value
// at fractional ticks. 0 = black, 0x8000 = palette unchanged, 0xff00 = white. Before tick 0: 0; after the end: the
// last value.
double fadeLevel(double tick);
int fadeLevelAt(int tick);        // exact per-tick value
int fadeTarget(int tick);         // [0x1edbd] after the IRQ of `tick`
int fadeStep(int tick);           // [0x1edb9]

}  // namespace paper
