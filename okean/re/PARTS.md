# The Sea Robot of Love — part timeline

From the capture `full.*` (run_okean.py at 100 M instructions per second, no command line: the 50 Hz mode).
Frames are screen refreshes at 49.851 Hz (record k of `full.frames` = refresh k, program time k / 49.851 s, the
time column of `full.ticks`). The music player starts in frame 3 (0x13eb1 called by the first part's set-up at
program time 0.063 s); its ticks come from GUS timer 2 every 19.84 ms. Positions are order:row in decimal, as
`full.ticks` shows them at the start of the frame (the part's main loop polls the order and leaves during the
frame in which the new order has been reached).

Almost every frame differs from the one before (the noise backgrounds change every frame): at this speed every
part draws one frame per refresh.

## Parts

| entry | frames | time (s) | music | on screen |
|---|---|---|---|---|
| 0x38469 | 0..2 | 0.00..0.06 | – | black (mode 13h, then the 50 Hz mode); builds the tables of part 0x38520 |
| 0x2c26f | 3..620 | 0.06..12.44 | 0:00..0:63 | grey noise; white beams of light fan out from the centre and the title "the sea ROBOT of love" appears in the noise; fades to black from row 56 |
| 0x2e779 | 621..2330 | 12.46..46.74 | 1:00..6:47 | glowing red wireframes on a dark red background with a bright horizon band: a ribbed sphere, small flying objects, tubes; later orange and green, on a dark green background |
| 0x3554b | 2331..3470 | 46.76..69.61 | 7:00..10:47 | a green wireframe landscape (a grid that ripples and folds) over dark green noise |
| 0x367e5 | 3471..4692 | 69.63..94.12 | 11:00..15:13 | red-pink wireframe objects that morph (tori, lobed bodies) over dark red noise |
| 0x38520 | 4693..5751 | 94.14..115.36 | 15:13..18:47 | a blue wireframe landscape over dark blue noise, with captions in the intro's font: "hmmm", "moving lightsource and shadow", "woww" |
| 0x3c8da | 5752..6892 | 115.38..138.25 | 19:00..23:00 | a magenta web of lines with a pulsing star shape over dark magenta noise; black at the end |
| 0x5334f | 6893..8095 | 138.27..162.38 | 23:00..27:10 | a lavender wireframe sheet that curls, captions "thy shall be upgraded", "thy shall not make whores"; then grey noise with lines |
| 0x4d370 | 8096..9108 | 162.40..182.70 | 27:10..30:37 | white text over grey noise: "forever lowing robot / its the human robot of love", the credits, the greetings; the program exits at frame 9109 (182.74 s) |

## Snapshots

`snaps/part_<entry>.snap`, taken where main calls each part (before its set-up): part_38469 (frame 0), part_2c26f
(3), part_2e779 (621), part_3554b (2331), part_367e5 (3471), part_38520 (4693), part_3c8da (5752), part_5334f (6893),
part_4d370 (8096).
