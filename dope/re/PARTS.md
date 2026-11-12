# DOPE — part timeline (from `run_dope.py`, 100 M instructions/s)

One record per displayed video frame: 70.09 Hz everywhere (mode 13h and p1's tweaked mode both use the 449-line,
25.175 MHz timing). Times are emulated seconds from program start; the music starts with the GUS timer at about
0.11 s (after the samples are uploaded) and is never synchronised with the pictures. "Music" = order:row in hex
(order = index into the order table, 0x00..0x7b) at the first and the last frame of the part. "Drawn fps" = frames
the part rendered per second (key checks per second; the engine parts check the keyboard once per drawn frame).
`python3 timeline.py full.ticks` prints the first table; `sheet.py` makes contact sheets.

| part | frames | time (s) | length (s) | music | drawn fps | what is on screen |
|---|---|---|---|---|---|---|
| loader | 0–6 | 0.00–0.10 | 0.10 | — | — | text mode: "Initializing music replay...", module load and sample upload (instant here; seconds on a real PC), then mode 13h (black) |
| p1 | 7–2704 | 0.10–38.58 | 38.50 | 00:3f–02:3e | — (hardware scroll) | "Complex Media Labs" chrome logo (reward) scrolling in from the right and out to the left, palette cross-fading from violet / blue to red / orange |
| p2 | 2705–10022 | 38.60–143.00 | 104.41 | 02:3e–1d:1f | 65.5 (70 → 44 in the heaviest shots) | credits: two shaded discs (top and bottom) turning while "jmagic code", "jugi music", "jugi graphics", "\"dope\"" fade in and out; then a 3D "C" (Complex logo) on a stand between the discs, camera flights, label "BLUR motion" and motion-blur trails; all in blue / white shades |
| p3 | 10023–13637 | 143.01–194.57 | 51.58 | 1d:1f–2a:26 | 69.9 | label "7800 PHONG polys": a lumpy, Phong-shaded grey object (7800 polygons, m. hakulinen) turning and coming closer over a violet nebula and star picture |
| p4 | 13638–21081 | 194.59–300.79 | 106.21 | 2a:26–45:26 | — (no int 16h) | starts on p3's last picture and palette: "FIRE clouds" — orange fire spirals swirling over the nebula; then "FRACTAL scape" — a grey fractal landscape flight with the fire spirals over it; ends with p5's chequerboard fading in |
| p5 | 21082–25587 | 300.80–365.08 | 64.29 | 45:26–55:3c | 69.7 | over a red / orange marble chequerboard: "ENV mapping" — gold environment-mapped star / flower shapes flying past; "BUMP glow" — a bump-mapped red star-shaped torus |
| p6 | 25588–29596 | 365.09–422.28 | 57.20 | 55:3c–64:1f | 69.8 | over a blue swirl picture, inside rotating blue rings: "SHADOW volume" — the cat figure ("found from some cdrom"); "material MORPH" — its fur turns to metal; "METAL shading" |
| p7 | 29597–32119 | 422.29–458.28 | 36.00 | 64:1f–6d:28 | 56.1 (70 → 41 in the last shots) | black sky with a horizon silhouette strip at the bottom: "multiple LIGHT sources" and "WORLD domination" — a grey octopus / tentacle object (m. hakulinen) turning and growing; then "dope" on black; text mode |
| term | (32120) | 458.31 | — | 6d:28 | — | text mode: "■ TERM", "CDPI v0.2, Disconnecting life support systems.", the music stops, exit |

The module would go on to order 0x7b and loop; the demo cuts it at order 0x6d. Between parts the screen keeps the
previous part's last frame while CDPI loads the next one and its RNC stub unpacks it (3-4 frames here, the disk
time is not emulated). The parts hand over visually: p2 fades to white (its exit sets the whole DAC to 63) and p3
starts from black; p4 starts on p3's picture and ends on p5's chequerboard; p5 ends showing p6's blue swirl picture;
p6 fades out; p7 ends with "dope" on black and sets text mode itself (frame 32119 is the last graphics frame).

## Per part: routines (all in segment 0010 of the part's image; `rep.py <p> func <addr>`)

| part | entry / main script | frame loop ("run N retraces") and what it calls | int 8 handler / per-retrace routine / timer set-up | script |
|---|---|---|---|---|
| p1 | 0bd5: mode 13h, unchained 320x200 with a 1344-pixel virtual line and a split at line 194 (0c02..0c89), logo blit 0b5c (1024x195, 4 planes), loop 0cae..: palette cross-fade weights from the scroll position (0b00 with the tables at 016c / 01ac), until the position [016a] reaches 1500h | — (everything in the IRQ) | 0020 / 0b8b (CRTC start = [016a] >> 4), 0ba3 (pel panning = ([016a] >> 2) & 3), 0b24 (DAC from the mixed palettes); position += 2 per retrace / 0076, 00b9 (count = 2P*990/2048) | position 0..0x1500 at 2 per retrace = 2688 retraces |
| p2 | 376c → 3765 (19ce sine table), 1998 (table at 3527:0080), 377f (script, 153 steps) | 371e: 354a, 3585, 3310, 360c (wait for [1924], copy to A000); drawn frames [192c], retraces [1928] | 1a21 / 2580 (objects' motion, palette fade, [1924]++, [1928]++) / 1a5f (2P*1000/2048) | 7296 retraces |
| p3 | 2bec → 2be5 (19f0), 2c22 (script, 21 steps) | 2ba4: 2a5a, 2a95, 2820, 2b1c; [199e] drawn, [199a] retraces | 1a43 / 1f60 / 1a86 | 3605 retraces |
| p4 | 1caa: reads the DAC (first 16 colours) and A000 (p3's picture) into 4f1e:0000, tables 1ea8, 1b36, 1b56; steps until the retrace counter [1969] reaches 0578, 0b46, 0500, 041a, 06d6, 01ea, ... | per frame 1b91 (1aab: per-pixel lookup through tables at 07d9 / 66cd / 4f1e, with self-modified offsets at 1b0b / 1b20; 1c6b), later 1ee8 + 1efc; ESC by port 60h | 1420 / 1a20 (fades [196d], [1971], palette 1975 / 1999) / 1469 | retrace counts in the main routine |
| p5 | 3089 → 2bae (1584), 30e2 (script, 28 steps) | 2b66: 2a17, 2a52, 27e0, 2adc; [152c] drawn, [1528] retraces | 15d7 / 1a70 / 161a | 4480 retraces |
| p6 | 38e8 → 2ecf (179b, 1782); unmasks all IRQs (port 21h = 0); mouse reset (int 33h AX=0 → [1758]); 3951 (script, 16 steps) | 2de6: 2c9c, 2cd9, ..., 38ca, 2a60, 2d5c, mouse buttons (int 33h AX=3, only with a driver) add to [1762] / [1760]; [173a] drawn, [1736] retraces | 17ee / 1cc0 / 1834 | 3990 retraces |
| p7 | 3194 → 318d (19f4), 31ca (script, 7 steps), text mode at 3410 | 3070: 2eca, 2f05, 3046 (5x), 2c90, 2f8c; [199e] drawn, [199a] retraces | 1a47 / 1f40 / 1a8a | 2100 retraces |

Exit paths (key pressed or script done): p2 43ed, p3 2f57, p5 3747, p6 3d22, p7 33be: restore int 8 and the PIT
(mode 3, count 0), exit code 0 (next part) or 0xFF (key: CDPI ends the demo).

The script lengths add up to less than the part lengths: each step ends at the first drawn frame after its retrace
count (up to one frame of overshoot per step), and every part spends a few frames on set-up (mode set, timer
calibration = 2-3 retraces, tables).

## Frame-rate dependence

At 100 M instructions/s the engine parts draw a frame at every retrace except in p2's heaviest shots (the "C" logo
close-ups, 44-60 fps). Motion, fades and step lengths are counted in retraces in the per-retrace routines, so a
slower machine draws fewer frames but shows the same motion, except for: the overshoot of each script step (one drawn
frame at most), and any state the frame loops advance per drawn frame (to be checked per part by the decompilers;
see BRIEF.md). p1 and p4 do all their timing in retraces.

## Snapshots

`snaps/part_<p>.snap` (p1..p7, term): where CDPI has just started the part (`--load snaps/part_p3.snap`).
