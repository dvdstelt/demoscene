# clone meets clone — part timeline

From the capture `full.*` (run_clone.py at 150 M instructions per second, 32-bit video mode). Ticks are timer
ticks: 18.2 Hz while the precalc runs (ticks 0..10), then MXMPlay's player tick, 22,100 PIT clocks = 53.99 Hz
(135 BPM, speed 3: one row every 3 ticks, one order of 64 rows every 192 ticks = 3.556 s). Music time = the player
timer / 1,193,182 (xmpGetTimer: 0 at tick 12 = the second interrupt, program time 0.722 s). Positions are
order:row in hex. "part" is main's part variable [ebp−0x14] (the switch at 0x400e80); the part draws one frame per
call. Frames = frames shown (calls of 0x400000) while the part was active.

The song has 61 orders (00..3c) and lasts 216.9 s; the intro ends with it: event −1 at 3c:3f sets the end flag,
main stops the music (tick 11723, music time 216.93 s), switches to text mode and exits at program time 217.63 s.
19,230 frames were shown (88 fps on average; no retrace wait, so the rate is whatever the CPU gives).

| # | routine (set-up) | music | ticks | music time (s) | frames (fps) | on screen |
|---|---|---|---|---|---|---|
| – | precalc (main) | – | 0..10 | – (program 0.00..0.65) | – | text mode: title, "samples [....] - uploading", "textures [....] - uploading" |
| 1 | 0x405527 (0x4053f2 at event 1) | 00:00..08:20 | 11..1644 | 0.00..30.25 | 1385 (45.8) | a glowing violet particle swarm on black: a few dots, a curve, a spiral, a ring, a pulsing ball; from 04:00 radial-blur streaks (events 101 / 102 / 103 switch them); trails (8-bit buffer with per-frame feedback), drawn in rows 16..175 through per-band palettes that darken towards the top and bottom; 08:00 the swarm ends and the title "clone meets clone" fades in on black (event 106) |
| 11 | 0x419dc7 (pictures 0x419d5b) | 08:20..0c:00 | 1645..2315 | 30.27..42.67 | 8995 (725) | a mosaic of coloured squares (orange, pink, violet) over the top four fifths of the screen with a black box holding the title, fading in; from 0b:00 (event 1101, [0x428468] = 1) it fades out; black from about 0b:30 |
| 2 | 0x4198bf | 0c:00..10:00 | 2316..3083 | 42.69..56.90 | 576 (40.5) | a warped surface in perspective with a grid of crossing dark lines over pink / violet light (grid filler, texture [0x4207c8]), the camera gliding over it; "meet us" (black box) and glyph 1 at the bottom left; brightness rises from 0.3 to 1 ([0x428160]); event 201 at 0e:00 changes the camera path |
| 8 | 0x417929 (0x417563 in the precalc) | 10:00..14:18 | 3084..3924 | 56.92..72.48 | 840 (54.0) | a textured blob (geosphere, map B: a lattice pattern) turning and deforming in front of a horizontal band of coloured squares; events 801 (10:04..10:10, [0x427ff0] += 2) and 803 (14:00..14:18, [0x427fe4] −= 1, [0x427f70] −= 0.25) change it; at the end it shrinks and darkens |
| 6 | 0x406677 (0x4065c5 at event 6) | 14:18..18:00 | 3925..4620 | 72.50..85.37 | 372 (28.9) | a blue particle ring with radial-blur streaks (part 1's technique in blue), bursts of light; event 601 at 17:20 |
| 9 | 0x4185e5 (buffers 0x418402) | 18:00..20:00 | 4621..6155 | 85.39..113.80 | 1550 (54.6) | pieces of a textured surface (map C, a lattice) built from boxes / columns, flying and turning in front of the band of squares; event 901 at 18:08, 902 every 8 rows from 19:00 (cycles [0x428140] through 0, 1, 2) |
| 5 | 0x403530 | 20:00..24:00 | 6156..6923 | 113.82..128.02 | 591 (41.6) | a warped, rotating plasma of dark grid lines on pink / violet (grid filler with the colour table 0xe35128, set at event 5); "get them clones" (black box) and glyph 2 at the top right; events 501 (21:00, 22:00, 23:00) reverse the rotation |
| 7 | 0x416fce (0x416cca in the precalc) | 24:00..29:00 | 6924..7883 | 128.04..145.80 | 1282 (72.2) | a textured blob (geosphere, map A: spots / stripes) morphing and turning in front of a square of coloured tiles on the left; events 701 (28:00..28:10); black at the end |
| 3 | 0x405c45 (0x405b10 at event 3) | 29:00..30:20 | 7884..9323 | 145.82..172.48 | 1382 (51.9) | a blue cloudy field of soft dots, fading in from black, drifting; "connect the dots" (black box) and glyph 3 at the bottom right; event 301 at 30:00 fades it out |
| 4 | 0x4037c0 | 30:20..36:00 | 9324..10379 | 172.49..192.04 | 763 (39.0) | a tunnel: dark ring lines on pink / violet light receding into a black centre that wanders (grid filler); "tunnels meet more tunnels" (black box) and glyph 4 at the top left; events 403 (31:00), 402 + 401 (32:00), 401 every order to 35:00 (cycles [0x421a7c]) |
| 10 | 0x41958e (buffers 0x419373) | 36:00..3a:00 | 10380..11147 | 192.05..206.26 | 1181 (83.1) | like part 9 with map B: textured boxes / slabs over the band of squares; events 1011 / 1012 alternate every 2 rows at 39:00..39:3e ([0x427f70] = 1.2 / 1.0: a pulse) |
| 1 | 0x405527 | 3a:00..3c:3f | 11148..11723 | 206.28..216.93 | 283 (26.6) | part 1 again (events 104 + 111: blur on, title off): a dark ring in a violet streak storm; 3c:00 (event 105) a white-violet flash that fades ([0x422504] = 255); 3c:10 (event 107) the credits ".aap .echo .simon" fade in on black |

Part 99 (0x400079: black with a blue band in rows 20..179) has a switch entry but no event uses it.

## Events in the capture

The sync table is checked at the start of every frame, so an event runs at the first frame after the music
reached its position (`full.ticks` column 8 = the table index; music time from `full.ticks` column 6):

| tick | music time | position | events (value) |
|---|---|---|---|
| 11 | 0.000 | 00:00 | 1 (part 1) |
| 108 | 1.797 | 00:20 | 103 |
| 780 | 14.243 | 04:00 | 101 |
| 1549 | 28.487 | 08:00 | 102, 106 |
| 1645 | 30.265 | 08:20 | 11 (part 11) |
| 2124 | 39.137 | 0b:00 | 1101 |
| 2316 | 42.693 | 0c:00 | 2 (part 2) |
| 2700 | 49.805 | 0e:00 | 201 |
| 3084 | 56.918 | 10:00 | 8 (part 8) |
| 3096, 3108, 3120, 3132 | 57.140 .. 57.807 | 10:04, 10:08, 10:0c, 10:10 | 801 |
| 3852, 3876, 3900, 3924 | 71.143 .. 72.476 | 14:00, 14:08, 14:10, 14:18 | 803 |
| 3925 | 72.495 | 14:18 | 6 (part 6) |
| 4524 | 83.589 | 17:20 | 601 |
| 4621 | 85.386 | 18:00 | 9 (part 9) |
| 4644 | 85.812 | 18:08 | 901 |
| 4812 .. 6012 | 88.924 .. 111.150 | 19:00, 19:08, 19:10, 1a:00, … 1f:10 | 902 (21 times) |
| 6156 | 113.817 | 20:00 | 5 (part 5) |
| 6348, 6540, 6732 | 117.373, 120.929, 124.486 | 21:00, 22:00, 23:00 | 501 |
| 6924 | 128.042 | 24:00 | 7 (part 7) |
| 7692, 7717, 7740 | 142.267, 142.730, 143.156 | 28:00, 28:08, 28:10 | 701 |
| 7884 | 145.823 | 29:00 | 3 (part 3) |
| 9228 | 170.716 | 30:00 | 301 |
| 9324 | 172.494 | 30:20 | 4 (part 4) |
| 9420 | 174.273 | 31:00 | 403 |
| 9612 | 177.829 | 32:00 | 402, 401 |
| 9804, 9996, 10188 | 181.385, 184.941, 188.497 | 33:00, 34:00, 35:00 | 401 |
| 10380 | 192.054 | 36:00 | 10 (part 10) |
| 10956 .. 11142 | 202.722 .. 206.167 | 39:00 .. 39:3e, every 2 rows | 1011 / 1012 alternating |
| 11148 | 206.278 | 3a:00 | 104, 111 (part 1) |
| 11533 | 213.409 | 3c:00 | 105 |
| 11581 | 214.298 | 3c:10 | 107 |
| 11721 | 216.9 | 3c:3f | −1 (end) |

After event −1 the frame's event loop runs on past the end of the table (index 98, 99: the bytes after it, the page
counter and heap variables at 0x4207a9..) before the end flag stops the loop; those values match no event.

## Snapshots

`snaps/t<tick>.snap`, taken at the first timer tick after each part change (run_clone.py `--snap-parts`): t11
(part 1; all precalc done), t1645 (11), t2316 (2), t3084 (8), t3925 (6), t4621 (9), t6156 (5), t6924 (7), t7884
(3), t9324 (4), t10380 (10), t11148 (1 again).

## Frame-rate dependence

Measured two ways from every snapshot (400 ticks each, `fpscheck.py`):

- **Frozen timer** (`--freeze-timer`: xmpGetTimer constant, so dt = 0 after the first frame): in every part the
  picture stops; it changes only when a sync event fires (part 8 at the 801 events, part 4 at 403), and in part 1
  the 8-bit feedback buffer settles over about 100 frames before it stops (t11148: the frame changes by 24 levels
  on average, then not at all). So nothing moves without dt except the feedback trails.
- **Two speeds** (55.5 M and 311 M instructions per second, against the capture at 150 M; ratios chosen so the
  frames do not lock in phase with the capture's): frames of the two runs drawn with timer values less than 1 ms
  apart, mean absolute RGB difference (0..255), compared with the change between consecutive frames of the
  capture. Frame rates over the same 400 ticks from each snapshot:

  | part | fps at 55.5 M / 150 M / 311 M | difference at equal time, 55.5 M / 311 M (median) | change per frame | reading |
  |---|---|---|---|---|
  | 1 | 30 / 80 / 168 | 13 / 12 | 0.5 | depends on the frame rate: the 8-bit feedback (0x4045b4, a per-frame decay of the trails) and the particles' integration; the trails and the blur differ visibly |
  | 11 | 272 / 735 / 1525 | 0.00 / 0.01 | 0.3 | timer only |
  | 2 | 15 / 41 / 84 | 13 / 4.7 | 12 | depends (more at low rates). Guess: the camera time [0xf73928] += dt starts with the previous part's last dt and accumulates in truncating float; the grid scope should find the cause |
  | 8 | 22 / 60 / 125 | 0.21 / 0.26 | 5 | timer only (up to rounding) |
  | 6 | 10 / 28 / 59 | 0.74 / 0.82 | 3 | slightly |
  | 9 | 29 / 79 / 163 | 0.46 / 2.8 (max 16) | 5 | mostly timer; some frames differ |
  | 5 | 15 / 42 / 86 | 6.9 / 1.1 | 5 | depends at low rates. Likely the direction flips (event 501): they happen at the first frame after the music position, so the angle after a flip carries that frame's lag |
  | 7 | 16 / 43 / 88 | 0.20 / 0.17 | 6 | timer only |
  | 3 | 22 / 59 / 125 | 10 / 11 | 2.5 | depends: the dot field (8-bit buffers, like part 1) |
  | 4 | 15 / 39 / 81 | 0.54 / 0.69 (max 9) | 6 | mostly timer; differences around the 401..403 events |
  | 10 | 29 / 77 / 160 | 0.10 / 0.08 | 2.5 | timer only |
  | 1 (end) | 10 / 26 / 54 | 14 / 14 | 0.7–2.3 | as part 1 |

  The capture's frame rates (150 M instructions per second) are the reference for the look of the frame-rate
  dependent parts (1, 3, and less 2, 5, 6). A 133 MHz Pentium of 1998 would be somewhat slower than the capture: it
  also pays for writes to video memory and for port I/O, which the emulation does not charge.
