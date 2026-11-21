# Inside — part timeline

From the capture `full.*` (run_inside.py at 150 M instructions per second). Frames are screen refreshes at
70.086 Hz (record k of `full.frames` = refresh k, program time k / 70.086 s). The music starts at refresh 37
(program time 0.542 s); "t" below is seconds since then. One order of the song (64 rows) lasts 5.02 s (12.74 rows
per second). Positions are order:row in decimal, as the script table holds them.

A script entry runs at the end of the first frame the main loop draws after the music reached its position, so
the column "ran at" is the refresh at which its effect is first visible (the routine's palette change or new part).

## Parts

| frame routine | tick routine | refreshes | t (s) | music | frames drawn (fps) | on screen |
|---|---|---|---|---|---|---|
| – (precalc) | – | 0..37 | – | – | – | black (mode 13h from refresh 15) |
| 0x403748 intro | 0x403214 | 38..1445 | 0.01..20.10 | 0:00..3:63 | 974 (48.5) | a picture (intro/intro.raw: a woman's body and a tube of "Amazing Good" glue, digitised) revealed by drifting spots of light ("light drops") that grow; a dark blob stays |
| 0x402f1c logo | 0x402e30 | 1446..2147 | 20.10..30.11 | 3:63..5:63 | 701 (70.0) | the twisted 3D logo object on a red radial background (logo/twlogo.raw), white flashes on the beat; the first refresh shows the intro's pixels through the logo's palette |
| 0x406794 credits | 0x405f30 | 2148..4254 | 30.12..60.17 | 5:63..11:63 | 1921 (63.9) | a twisting colourful tunnel flown through, smoke, a spiked ball; the names (DESTOP, JOUNI, JULIET, ..., YOLK & LEGEND) with small portraits (credits/kredu*.raw) |
| 0x4047a0 sprites / shadow scene | 0x404738 | 4255..4961 | 60.18..70.26 | 11:63..14:00 | 706 (70.0) | digitised photos of people (sprites with shadows) over a warping colourful background |
| 0x4095f8 flower | 0x40940c | 4963..7062 | 70.29..100.24 | 14:00..19:63 | 1443 (48.2) | a green tunnel (LightWave camera path) and red flowers of petals that open and turn |
| 0x4028e4 flight | 0x4021f8 | 7063..8467 | 100.25..120.28 | 19:63..23:63 | 1404 (70.0) | a flight over a landscape under a blue sky with a sun and lens flares; ships |
| 0x407d78 water | 0x407c78 | 8468..9789 | 120.30..139.15 | 23:63..27:48 | 1171 (62.1) | a blue underwater scene: a twisted metal object (water/obuke.sro) turning, fish, the water surface above |
| 0x4047a0 sprites again | 0x404738 | 9790..10573 | 139.16..150.34 | 27:48..29:63 | 783 (70.0) | two more digitised photos of people (a man lying in an orange shirt on a yellow background; two people with masks) |
| 0x40a264 twirl | 0x40a20c | 10574..11275 | 150.34..160.35 | 29:63..31:63 | 701 (70.0) | a picture (flower/jeesus1.raw) twisted in a whirl; its colours change at the events 0x40a098 |
| 0x404424 temple | 0x40407c | 11276..12949 | 160.36..184.24 | 31:63..36:16 | 1212 (50.7) | flight through a temple of grey pipes and slabs with glowing pink flares; fades to black from 35:48 |
| – (end) | – | 12950..13080 | 184.24..186.11 | 36:16..36:24 | – | black; the script's last entry at 36:24 (0x400228, nothing) and the end marker stop the demo; it exits at refresh 13081 (program time 186.65 s) |

## Script entries in the capture

| # | position | routine | ran at refresh | t (s) | what |
|---|---|---|---|---|---|
| 0 | 0:00 | 0x4038c8 | 38 | 0.014 | intro: tick 0x403214, frame 0x403748 |
| 1 | 3:00 | 0x4031a0 | 1101 | 15.181 | intro: new random light drops |
| 2 | 3:63 | 0x4038dc | 1446 | 20.104 | routines off, frees the intro's memory |
| 3 | 3:63 | 0x403164 | 1446 | 20.104 | logo: palette, tick 0x402e30, frame 0x402f1c |
| 4, 5 | 4:32 | 0x400498, 0x402f10 | 1627 | 22.686 | white flash; logo: [0x42553c] = 1 |
| 6 | 5:00 | 0x400498 | 1803 | 25.197 | white flash |
| 7 | 5:32 | 0x400454 | 1978 | 27.694 | inverse flash |
| 8..11 | 5:63 | 0x400498, 0x403184, 0x4073c8, 0x400498 | 2148 | 30.120 | flash; logo off and freed; credits: tick 0x405f30, frame 0x406794, palette; flash |
| 12 | 6:32 | 0x400498 | 2329 | 32.703 | flash |
| 13..18 | 7:00, 7:32, 8:00, 8:32, 9:00, 9:32 | 0x405f1c | 2505, 2680, 2856, 3032, 3207, 3382 | 35.214 .. 47.727 | credits: next name, with a flash |
| 19, 20 | 9:48 | 0x400498, 0x405f10 | 3470 | 48.982 | flash; credits event |
| 21 | 10:32 | 0x400454 | 3734 | 52.749 | inverse flash |
| 22 | 11:00 | 0x400498 | 3909 | 55.246 | flash |
| 23 | 11:32 | 0x400454 | 4085 | 57.757 | inverse flash |
| 24 | 11:48 | 0x400438 | 4173 | 59.013 | fade to black (40 refreshes) |
| 25..27 | 11:63 | 0x400498, 0x4073e8, 0x4057bc | 4255 | 60.183 | flash; credits off and freed; sprites: tick 0x404738, frame 0x4047a0 |
| 28, 29 | 12:32 | 0x404680, 0x400498 | 4435 | 62.751 | sprites event; flash |
| 30 | 12:63 | 0x400498 | 4605 | 65.177 | flash |
| 31 | 13:00 | 0x405ba0 | 4611 | 65.262 | sprites: next picture |
| 32 | 13:32 | 0x405f00 | 4786 | 67.759 | sprites event |
| 33 | 13:48 | 0x400438 | 4874 | 69.015 | fade to black |
| 34 | 14:00 | 0x404728 | 4962 | 70.271 | routines off |
| 35, 36 | 14:00 | 0x4098c4, 0x400498 | 4963 | 70.285 | flower: tick 0x40940c, frame 0x4095f8; flash |
| 37 | 14:04 | 0x400498 | 4984 | 70.584 | flash |
| 38..64 | 14:24 .. 19:12 | 0x409358 / 0x4093f0 (+ flashes) | 5094 .. 6783 | 72.154 .. 96.253 | flower events (alternating) |
| 65 | 19:28 | 0x400454 | 6871 | 97.508 | inverse flash |
| 66 | 19:48 | 0x400438 | 6981 | 99.078 | fade to black |
| 67, 68 | 19:63 | 0x409e90, 0x402c28 | 7063 | 100.248 | flower off; flight: palette, tick 0x4021f8, frame 0x4028e4 |
| 69 | 23:48 | 0x400438 | 8386 | 119.125 | fade to black |
| 70..72 | 23:63 | 0x400498, 0x402c7c, 0x407f90 | 8468 | 120.295 | flash; flight off; water: palette, tick 0x407c78 |
| 73..75 | 27:48 | 0x408014, 0x400498, 0x40585c | 9790 | 139.157 | water off; flash; sprites again |
| 76 | 28:63 | 0x400498 | 10223 | 145.335 | flash |
| 77 | 29:00 | 0x405f00 | 10228 | 145.406 | sprites event |
| 78, 79 | 29:24 | 0x400498, 0x40571c | 10360 | 147.290 | flash; sprites: next picture |
| 80, 81 | 29:48 | 0x400498, 0x404680 | 10492 | 149.173 | flash; sprites event |
| 82..84 | 29:63 | 0x404728, 0x400498, 0x40a678 | 10574 | 150.343 | sprites off; flash; twirl |
| 85, 86 | 30:24 | 0x40a098, 0x400454 | 10711, 10712 | 152.298 | twirl event; inverse flash (one refresh later: the entries run one per call of 0x400b3c, and a frame ended in between) |
| 87, 88 | 30:48 (and 30:12) | 0x40a098 ×2 | 10843 | 154.181 | twirl events (30:12 follows 30:48 in the table, so both run at 30:48) |
| 89 | 30:56 | 0x400498 | 10887 | 154.809 | flash |
| 90 | 31:36 | 0x40a098 | 11128 | 158.248 | twirl event |
| 91..93 | 31:63 | 0x400498, 0x40a738, 0x40461c | 11276 | 160.359 | flash; twirl off; temple: palette, tick 0x40407c, frame 0x404424 |
| 94 | 35:48 | 0x400438 | 12600 | 179.250 | fade to black |
| 95 | 36:16 | 0x404668 | 12950 | 184.244 | routines off |
| 96 | 36:24 | 0x400228 | | | nothing (ret); the next entry (1000:0, routine 0) ends the demo |

## Snapshots

`snaps/f<frame>.snap` at the first refresh with a new frame routine: f38 (intro), f1446 (logo), f2148 (credits),
f4255 (sprites), f4963 (flower), f7063 (flight), f8468 (water), f9790 (sprites again), f10574 (twirl), f11276
(temple).

The capture ends at refresh 13081: 13,044 refreshes (186.1 s) from the music start; 11,026 frames were shown.
