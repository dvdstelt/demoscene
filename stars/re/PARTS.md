# STARS — part timeline (from `run_stars.py --out full`, 40 M instructions per second)

One record per displayed frame (70.086 Hz). "time" is program time in seconds at the frame's first line; the music
starts at frame 281 (4.009 s, the first GUS player tick) and `stars_reference.mp4` starts there. "pos" is the music
position order:row (hex) from [0x1c829] / [0x1c828]. "routine (call site)" is what main (0x4d) called; precalc and
clean-up routines get their own rows. "callback" is PIT callback 1 [0x4ab06] while the part runs (0x4abb4 = none).
"fps" = frames drawn per second (vertical syncs the program waited for) over the whole part in the capture; frame
rates are always 70.086 / n because every frame waits for a sync. "20 / 40 / 80 M" are the frame rates over the first
400 frames from the part's snapshot at 20, 40 and 80 M instructions per second, and "PIT frozen" is what still
changes in those 400 frames when IRQ 0 is not delivered (`fpscheck.py`): see "Frame-rate dependence" below.

The demo ends by itself: music stopped at frame 26359, text mode and exit code 0 at frame 26668 (380.50 s).

| frames | time (s) | pos | routine (call site) | callback | files | fps | 20 / 40 / 80 M | PIT frozen | on screen |
|---|---|---|---|---|---|---|---|---|---|
| 0–278 | 0.00–3.98 | — | 0x4ab7d (0x4d), 0x4abd4 (0x56), 0x4a98b (0x5b), 0x1ba6c (0x6f), 0x939 (0x83), load the module (0xb4), 0x1c71b (0xc7) convert and upload, mode 13h (0xe4, frame 62), 0x16945 (0xe6) precalc, 0x5b84 (0xf0) intro set-up, 0x1c759 (0xfe) start the music | — | 1; 50, 49; 5, 6, 7 | | | | text mode (on a real machine PMODE's two lines, "> 80486 processor." and "> protected mode type: ..."; the player's messages are disabled), then black |
| 279–5211 | 3.98–74.37 | 00:00–0a:39 | 0x5a92 (0x10c) intro | 0x5b91 | 9, 8, 11, 12, 13, 14, 10 | 44.9 | 70 (frame 1300) | palette only (per retrace) | title cards, one per order: 0x00 "Join us" (file 9), 0x01 "Bring the family" (8), 0x02 BARTi "Atomic" (0x5c32, card 0x542a) over a colour-cycling flower, 0x03 a woman (11), 0x04 KARL "Powerful" (0x5c47, card 0x5437), 0x05 woman (12), 0x06 GROO "Magic" (0x5c5c, 0x5444), 0x07 woman (13), 0x08 RA "Nutritive" (0x5c63, 0x5451), 0x09 a green poodle (14), 0x0a "STARS wonders of the world" (10). Pictures fade in over 128 retraces (0x627e, files 9, 8, 14, 10) or 64 (0x6251, 11..13), stay until row 0x2e / 0x30 and fade out the same way; the name cards stay until row 0x3e. With an odd minute BARTi and KARL swap (see NOTES.md) |
| 5212–5420 | 74.37–77.35 | 0a:39–0b:00 | 0x5b8f (0x111) free, 0x1241b (0x11f) precalc | — | 30, 31, 33 | | | | black |
| 5421–5941 | 77.35–84.78 | 0b:01–0e:1e | 0x126ae (0x12f) | 0x1278e | | 70.0 | 36 / 70 / 70 | static | a purple-brown cloud texture swirling and zooming around a bright centre |
| 5942–5946 | 84.78–84.85 | 0e:1e–0e:1f | 0x123d8 (0x134), 0x11d8e (0x139) precalc | — | 26, 28, 32, 27, 29 | | | | (last cloud frame) |
| 5947–7015 | 84.85–100.11 | 0e:20–16:03 | 0x11e7f (0x13e) | 0x12136 | | 28.2 | 25 / 36 / 52 | static | a golden 3D bee (object file 28, `WLDD`) flying close to the camera over the cloud background (camera keys every 0x8c ticks, 0x121b4); "23806 faces" in the corner at times |
| 7016–7169 | 100.11–102.30 | 16:03–17:00 | 0x11f48 (0x143) free, 0x65ef (0x148) precalc | — | 18, 15, 16, 19, 17 | | | | black |
| 7170–9475 | 102.30–135.20 | 17:01–27:00 | 0x6534 (0x158) | 0x6905 | | 32.5 | 17 / 34 / 67 | static | a 3D ball of long spikes (object file 18, `B!`), purple / green / white shading, turning in front of a rotating dark purple spiral |
| 9476–9477 | 135.20–135.23 | 27:00 | 0x65ad (0x15d) free, 0x9d17 (0x162) set-up | — | 11, 12, 13 | | | | |
| 9478–11312 | 135.23–161.42 | 27:01–33:23 | 0x975b (0x172) | 0x9932 | | 34.9 | 23 / 35 / 70 | static | the three women pictures warped (waves, stretching, mirrored copies) over the star shape; phases at 0x2800, 0x2b00, 0x2e00, 0x3100 |
| 11313–11497 | 161.42–164.05 | 33:23–35:00 | 0x9dcf (0x177) free, 0x1064 (0x17c) precalc | — | 2, 3, 4 | | | | black |
| 11498–13803 | 164.05–196.96 | 35:01–45:00 | 0x11e8 (0x18c) | 0xf85 | | 35.0 | 23 / 35 / 70 | static | a tunnel of green / blue earth-like texture (256x256 maps 2, 3, palette 4) rushing towards the viewer, black hole in the centre, the view turning |
| 13804–13809 | 196.96–197.04 | 45:00–45:02 | 0x110d (0x191) free, 0xa14d (0x196) precalc | — | 24, 22, 20, 23, 21, 25 | | | | |
| 13810–15971 | 197.04–227.89 | 45:02–54:01 | 0xa0ca (0x1a6) | 0xa4ac | | 22.6 | 11 / 23 / 35 | static | 3D textured surfaces (object file 24) over a blue-violet cloud screen (25): a stone-brick wall turning, a face in relief, a marbled purple surface; a moving white light spot |
| 15972–16642 | 227.89–237.46 | 54:01–58:20 | 0xa346 (0x1ab) free; main shows picture 0x39 (0x1b5), fades in at 0x5500, out at 0x5800 (64 retraces each), clears at 0x5820 | — | 57 | 13.6 | 11 / 11 / 11 | palette only | a picture of a spiky ornament with a face (file 57) fading in and out |
| 16643–18450 | 237.46–263.26 | 58:21–65:0a | 0x164dc (0x20d) | 0x16646 | 49, 51 | 42.6 | 29 / 59 / 70 | static | a 3D chrome star (object file 51; objects of file 50 prepared at 0x16945) turning in front of the camera, a row of small spiky shapes along the bottom |
| 18451–18996 | 263.26–271.05 | 65:0a–69:00 | 0x13388 (0x231) set-up, 0x133ad (0x236) | 0x1350e | 58 | 70.0 | 70 / 70 / 70 | **every frame** | blue glowing particles with soft blurred trails on a dark blue background |
| 18997–18998 | 271.05–271.08 | 69:00 | 0x1339d (0x23b) free, 0x1528c (0x254) set-up | — | 36, 37, 38, 39, 40, 41 | | | | |
| 18999–19468 | 271.08–277.79 | 69:01–6b:31 | 0x1535c (0x264) | 0x1544d | | 34.9 | 23 / 35 / 70 | **every drawn frame** | a brown-toned picture of the woman (from files 39..41) dissolving in a zooming blur, fading out at 0x6b30 |
| 19469–19503 | 277.79–278.29 | 6b:31–6c:00 | 0x1530a (0x269) free, 0x16d86 (0x282) precalc | — | 45, 46, 46, 42, 29, 43, 47, 44 | | | | black |
| 19504–21955 | 278.29–313.27 | 6c:01–7d:00 | 0x16f7c (0x292) | 0x17513 | | 29.5 | 37 / 52 / 70 | static | a 3D stone torso / column (object file 45, blue-grey, textured) with a red torus ring turning around it, a dark green landscape behind (47); ends at order 0x82 inside the part (0x17067) |
| 21956–23432 | 313.27–334.34 | 7d:01–87:00 | 0x174bd (0x297) free, 0x13388 (0x2b0) set-up, 0x133f1 (0x2b5) | 0x134e3 | 58 | 70.0 | 70 / 70 / 70 | **every frame** | the particle effect in white / beige on dark blue with long trails; the words of file 58 ("NOOON / BRING / YOUR / BEAUTY / FAMILY") appear in the particles one after the other |
| 23433–23435 | 334.34–334.39 | 87:00–87:01 | 0x1339d (0x2ba) free, 0x10b26 (0x2d3) set-up | — | 35, 34 | | | | |
| 23436–25162 | 334.39–359.03 | 87:01–92:2f | 0x1089c (0x2e3) | 0x10ae2 | | 70.0 | 35 / 70 / 70 | 25 changes | a blue sculpted face (files 34, 35) with rippling, feedback-like patterns around it; phases at 0x8820, 0x8920, 0x8b00, 0x8b20, 0x8d00, 0x8f00, 0x9000, 0x9200, 0x9210, 0x9230 |
| 25163–26358 | 359.03–376.09 | 93:00–9a:34 | 0x10bd9 (0x2e8) free; main shows the credits (0x2ed..0x422) | — | 54, 53, 55, 52, 10, 56 | 41.1 | 17 / 17 / 17 | palette only | credits, faded in / out over 64 retraces at 0x9500 / 0x9519, 0x9600, 0x9700, 0x9800, 0x9a00 (fade in by 8 over 32), 0x9a20: "SPECIAL EFFECTS KARL" (54) and "3D EFFECTS & MOTION BARTI" (53) (order by minute parity), "MUSIC GROO" (55), "ART RA" (52), "STARS" (10), "NOOON 50 $" (56) |
| 26359–26667 | 376.09–380.50 | 9a:34 | 0x1c77e (0x43a) stop the music, 0x4a006 (0x449), 180 retraces (0x453), fade out over 128 (0x462), 0x7dd (0x467), text mode | — | | 69.9 | | | "NOOON 50 $" fading out |

## main's script (all music positions main waits for)

main polls `0x29f80` (AX = order << 8 | row) and continues when AX is *greater* than the value:
0xb00 → 0x126ae; 0x1700 → 0x6534; 0x2700 → 0x975b; 0x3500 → 0x11e8; 0x4500 → 0xa0ca; 0x5500 fade in picture 57;
0x5800 fade out; 0x5820 → 0x164dc; 0x6100 → 0x13388 / 0x133ad; 0x6900 → 0x1535c; 0x6c00 → 0x16f7c; 0x8700 →
0x1089c; then the credits at 0x9500, 0x9519, 0x9600, 0x9700, 0x9800, 0x9a00, 0x9a20. The intro (0x5a92) waits for
0x000 .. 0xa00 itself. Parts that return before their position is reached leave main waiting on a black or frozen
screen (e.g. frames 5212–5420). Inside parts: 0x5c77 (row > 0x3e), 0x6265 (row > 0x30), 0x6292 (row > 0x2e),
0x9774 .. 0x97ee (0x2800 / 0x2b00 / 0x2e00 / 0x3100), 0x108e2 .. 0x10a6d (0x8820 .. 0x9230), 0x120b3 (order 0x17),
0x1347b / 0x134cf / 0x13557 (order vs [0x134e2]), 0x134e9 / 0x13514 (row 0), 0x15384 (0x6b30), 0x153b6 (row & 0xf,
order 0x69), 0x17067 (order > 0x82), 0x659c (order 0x27), 0x123d (order > 0x44), 0xfb6 (order 0x43). 0x1c7a3 returns AL =
order, AH = row; 0x29f80 returns AH = order, AL = row.

## Snapshots (`snaps/f<frame>_<routine>.snap`)

Taken at the first frame after main called the routine (when main calls several in one frame, the last one names
it): f46_939, f59_1c71b, f63_16945, f235_5b84, f279_5a92, f5212_1241b, f5421_126ae, f5942_11d8e, f5947_11e7f,
f7016_65ef, f7170_6534, f9476_9d17, f9478_975b, f11313_1064, f11498_11e8, f13804_a14d, f13810_a0ca, f15972_a346,
f16643_164dc, f18451_133ad, f18997_1528c, f18999_1535c, f19469_16d86, f19504_16f7c, f21956_133f1, f23433_10b26,
f23436_1089c, f25163_10bd9, f26359_4a006. To get one at another frame: `run_stars.py --load --snap snaps/<s> --out
x --frames F+1 --snap-at F --snap-out 'x_f{frame}.snap'`.

## Frame-rate dependence

`fpscheck.py` runs 400 frames from a part's snapshot with IRQ 0 not delivered: the part's PIT callback then never
runs. In 0x126ae, 0x11e7f, 0x6534, 0x975b, 0x11e8, 0xa0ca, 0x164dc and 0x16f7c the screen changed only 1–3 times
(the first frames) — their motion is stepped by the PIT callback (70.064 Hz) or derived from it, and the frame rate
only decides how many of those states are shown. The intro and the picture / credit sequences change only the
palette (fades and colour cycling, stepped per retrace). These parts move without the PIT:

- 0x133ad and 0x133f1 (particles): every frame changes. They draw at 70 fps even at 20 M instructions per second,
  so on any machine of the time that was fast enough they step once per retrace (70.086 Hz).
- 0x1535c (the dissolving picture): changes on every drawn frame, and draws 23 / 35 / 70 fps at 20 / 40 / 80 M: its
  speed depends on the machine. The capture's 35 fps is the reference.
- 0x1089c (the face): 25 changes in 400 frames without the PIT; the part has many music-position phases and may
  step some of its state per frame. To be checked by its scope.

So the capture's frame rates (fps column) are what a 486DX2/66-class machine shows; the remake can render the
tick-driven parts at any rate, and needs fixed step rates (70.086 Hz, 35 Hz) for the three per-frame parts.
