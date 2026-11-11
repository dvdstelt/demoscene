# JIZZ — part timeline (from `run_jizz.py --out full`, default speed 100 M instructions/s)

Ticks are timer interrupts: 0x10000 PIT clocks (18.2 Hz) during the precalc, then MXMPlay's player tick of 19,759
clocks (151 BPM: 60.39 Hz, constant). Music time 0 = the end of tick 434, the first player tick (program time
23.93 s); `jizz_reference.mp4` starts there. "pos" = music order:row (xmpGetPos), "events" = values of the sync
variable [0x6413] seen at tick ends (see NOTES.md; parts also rewrite it). Parts are main-line loops called one
after the other from main; there is no per-tick effect callback (the IRQ0 handler only plays music). Every frame of
every part is `call 0x638b` (frame start: next of 4 pages, flash decay, sync) ... draw into [0x2060] ...
`call 0x651f` (flip with 4F07 during retrace), `call 0x2058` (key test, always "no key"). fps = frames flipped per
second (100 M instructions/s; in brackets 25 / 200 M instructions/s; 70 Hz retrace cap).

| ticks | music time (s) | pos | events | part (main call site) | per-frame calls | fps | what is on screen |
|---|---|---|---|---|---|---|---|
| 0–433 | (−23.93–0) | | | precalc: 0x6592 .. 0x18228 tables, 0x1632b music (ticks 1–48), xmpInit (48), 0x12c50 textures (49–433), 0x6305 mode set, 0x29db1 xmpPlay | | | text mode: banner, "extracting music", "uploading samples to gus", "decrunching gfx" with percentages (black in the capture) |
| 434–1361 | 0–15.35 | 0:00–1:00 | 0, 10, 50; returns at 100 | 0x12013 (0x18bee) | 0x11986 (rays; light position from the timer), 0x13f98 (colour), 0x11f73, LUT copy of the 8-bit buffer [0x652a] to the page (0x120b5) | 22 (6 / 35) | "TBL" (dark letters) behind a light burst: white rays and sparks around a moving centre, tinted violet / blue / teal; fades in from black, ends in a white flash |
| 1362–2510 | 15.35–34.38 | 1:00–4:00 | 120, 128, 130, 201, 205, 210/215/220 on the beats, 225, 230, 300, 310 (311 → flash); returns at >= 400 | 0x17003 (0x18bf3) | 0x15bb0 / 0x15bef (3D object), 0x1131b, 0x17762, 0x17553, 0x1770b, 0x18009, 0x183a9, 0x11eb2, 0x12111, 0x11396, 0x174e5, 0x1151c (white inset frame), 0x17737 (white page), 0x17979 / 0x17999 (timer fades), 0x1a229 (flash) | 35 (11 / 63) | pink / red / black twisted ribbons (env-mapped tubes) over a blue cloud floor and ceiling; the floor/ceiling turn into a rainbow mirror plane with white rings pulsing on the beat; the ribbons return in a framed inset window, full screen (yellow / pink), and in the inset again |
| 2511–3278 | 34.38–47.10 | 4:00–6:00 | 400/410 alternating, 500, 510/520 alternating; returns at 0x257 | 0x169e7 (0x18bf8) | 0x11f73, 0x13eec, 0x15bb0, 0x13d73, 0x17727, 0x18228, 0x16cd3, 0x183a9, 0x15bef, 0x1151c, 0x11eb2, 0x12111, 0x11396 | 39 (14 / 69) | a grey-green textured tunnel with a framed inset of a green tunnel; the tunnel full screen; then white outline rectangles spinning over a swirling radial-blur background |
| 3279–4046 | 47.10–59.81 | 6:00–8:00 | 600 | 0x15c8b (0x18bfd) | 0x17737, 0x15bb0, 0x13eec, 0x15bef, 0x17979 / 0x17999 | 23 (7 / 36) | a yellow env-mapped tube / torus object with pink propeller-like spikes, camera close, over a grey swirling texture |
| 4047–4430 | 59.81–66.17 | 8:00–9:00 | 800 (the part turns 810 / 820 / 830 back into 800 and starts a timer pulse); returns at 900 | 0x1212a (0x18c02) | 0x17737, 0x1131b, 0x15bb0, 0x13d73, 0x123df, 0x16dc8, 0x179f6, 0x1239b | 35 (11 / 70) | the "jizz" logo by Sick Sjaak (texture logo2) over a star-shaped radial warp in dark green / purple that brightens on 810 / 820 / 830 |
| 4431–5199 | 66.17–78.91 | 9:00–11:00 | 900, 2560 (0a:00), 900; returns at 0xb00 | 0x15d20 (0x18c07) | 0x1131b, 0x1f5c5, 0x13eec, 0x15bef, 0x17979 / 0x17999 | 24 (7 / 39) | a spiky flower-like 3D object (purple / gold, env-mapped) turning over moving blue clouds |
| 5200–5966 | 78.91–91.61 | 11:00–13:00 | 2817 → 2816 (the part sets 0xb00) | 0x177bf (0x18c0c) | 0x1131b, 0x13d73, 0x16d43, 0x17979 / 0x17999 | 55 (25 / 70) | a rotating, zooming radial texture (green / purple swirl with a star-shaped centre); ends in a white flash |
| 5967–7502 | 91.61–117.05 | 13:00–17:00 | 3328, 3840, 4097 (the part sets 0x1001); returns at 0x1100 | 0x17c8a (0x18c11) | 0x17737, 0x13d73, 0x17b2f, 0x17979 / 0x17999 | 66 (17 / 70) | after the flash: dark purple / blue "field line" patterns (several poles with curved bands between them) that move and merge |
| 7503–9038 | 117.05–142.48 | 17:00–21:00 | 4352, 4864 (the part sets 0x1300), 5120 (white while it lasts), 4864; returns at 0x1500 | 0x10fda (0x18c16) | 0x15bb0, 0x1f5c5, 0x7aae, 0x769f, 0x159fb, 0x154bc, 0x1f5d1 (vector engine), 0x17979 / 0x17999 | 23 (6 / 35) | gold / chrome env-mapped metaballs ("blobs") merging and splitting over a swirling green / purple / white background; white during 14:00–14:10 |
| 9039–9356 | 142.48–147.75 | 21:00–21:1c | 5376 → -1 | 0x17979(eax 0x200b20) (0x18c20), loop 0x18c25 | copy of the end picture [0x657a] to the page, 0x17999 (fade) | 70 (54 / 70) | "~JIZZ~ TBL'97" (texture logo1) on black |
| 9357 | 147.75 | 21:1c | -1 | 0x18c59 | | | xmpStop, text mode, banner + credits, exit (program time 171.69 s) |

Snapshots of the full run (in `snaps/`, `--load --snap snaps/t<T>.snap`): 430 (before the mode set), 440, 1365,
2515, 3285, 4050, 4435, 5205, 5970, 7506, 9045 (a few ticks into each part).

## Frame-rate dependence

Runs from the tick-430 snapshot at 25 M and 200 M instructions/s play the same parts (no part is skipped at any
speed). The music starts at tick 445 / 432 instead of 434 (the precalc is CPU bound); measured from the music
start every run ends after 147.75 s. Part changes lag the music position by up to about two frames (0x638b checks
the position at the start of a frame, the part tests the event after drawing): part 0x17003 starts 4 ticks after
the music reaches 1:00 at 22 fps, 17 ticks after it at 5.7 fps.
`--freeze-timer` runs (xmpGetTimer constant) from each part's snapshot show what moves without time passing:

| part | with the timer frozen | frame-rate dependent |
|---|---|---|
| 0x12013 | fades and the colour of the rays keep changing | yes: fade-in +9 per frame (29 frames), fade to white +0x17 per frame (12 frames), colour pulse [0x12012] += 2 per frame (128-frame cycle) |
| 0x17003 | static | the white flash at event 311 (03:10) decays 9/255 per frame in 0x638b (29 frames); the rest uses the timer |
| 0x169e7, 0x15c8b, 0x15d20, 0x177bf, 0x17c8a, 0x10fda | static after the first frames | no |
| 0x1212a | static except at the 810 / 820 / 830 events (the pulse that follows uses the timer) | no |
| 0x18c25 | static (timer fade) | no |

So only the "TBL" part (and the one flash in the ribbon part) needs a chosen frame rate; the capture's 22 fps
(35 fps for the flash) is what a 100 M instructions/s machine gives. `--part-budget 12013=N` sets the speed for that
part only.
