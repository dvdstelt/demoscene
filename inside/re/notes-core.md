# Inside: core (start-up, main loop, script, clocks, palette, screen, data file, memory)

Routines: main 0x400010, init 0x4007c0, exit 0x400a10, main loop 0x400cb4, script 0x400b3c (table 0x4222e4),
retrace timer 0x40bd48..0x40bfdf, timer callback 0x40070c, palette 0x400364..0x400612, screen 0x40a83c / 0x400694 /
0x400b14, data file 0x4000e8 / 0x4001c4 / 0x40022c / 0x400614, memory 0x40b10c..0x40b7a4, rand 0x4134cd / 0x4134f1,
music start 0x4010b4 / 0x400efc / 0x401010 / 0x400fcc. Addresses are linear (NOTES.md). M, P, R, c_R, M_ret(k) are
defined in section 1 and used everywhere below. "Record k" is record k of `full.frames` / `full.ticks`.

Checked against the capture (`models/core/`, list in its README):

- the clock model (section 1) against all 13,081 records of `full.ticks`: every row change of the song (2,327) falls
  in the record the model predicts, except the 23 rows of order 36, which take three times as long (2,304 DSMI units:
  speed or tempo change, see the music notes) and fit once that is included; every one of the 3,035 timer interrupts
  that carry GUS writes after the music start is a player tick of the model;
- the palette system (section 6) against the DAC of every record from 38 to 13,080 (13,042 records × 768 values):
  **exact**, including every fade, flash, set and retarget the script does, with the float arithmetic below;
- the script timing (section 3) from the part tick counter and the palette fades;
- hooked runs of the emulator (`models/core/run_inside.py --events`, from the start to record 160, and from the
  snapshots f1446 and f4255 for 150 / 75 records; their `.ticks` equal `full.ticks` line for line): callback times,
  player ticks, frame routine starts and flips, the FPU control word, every rand / srand call.

---------------------------------------------------------------------------------------------------------------

## 1. The clocks (what the remake needs)

### 1.1 Definitions

| name | what | rate | reset |
|---|---|---|---|
| interrupt | IRQ0 of the retrace timer (section 4): 4 per screen refresh, the 1st at the start of the vertical retrace, then 4,085, 8,170, 12,255 PIT clocks later (0, 0.23995, 0.47990, 0.71984 of a refresh) | 280.345 / s | – |
| **M** = [0x4248c4] | timer callbacks since the music start: +1 at the start of 0x40070c, before the music; **the music clock** | 280.345 / s | 0 at the end of init, never again |
| DSMI time [0x4b9148] | += [0x4b9144] = 35 per callback (0x400fcc → 0x40f620); **DSMI time = 35 M** exactly (both are skipped together, see 1.6) | 9,812.08 / s | 0 at 0x40f5a1 (music start) |
| player tick | AMP's poll 0x40d878 (in every callback): while DSMI time − last > period (strictly): tick 0x40d7c0, last += period | – | last = DSMI time at ampInit (0) |
| row | the order / row pair [0x422a5d] / [0x422a61], changed by the player tick that starts a row | – | 0:0 at ampInit |
| **P** = [0x4248c8] | part tick counter: +1 per callback (same place as M) | 280.345 / s | 0 by 0x400af4 / 0x400b04 (a script routine setting the tick / frame routine) |
| retrace callback | the callback of the 1st interrupt of a refresh ([0x4b8f10] = 0): only there the frame clock, the palette fade step, the screen copy and the part's tick routine [0x4248b4] run | 70.0863 / s | – |
| frame clock [0x424880] | float, += 1.0 per retrace callback. **No part reads it** (nor [0x42488c]) | 70.0863 / s | 0 at the end of init |
| frames shown [0x42488c] | float, += 1.0 per frame copied to VGA memory | ≤ 70.0863 / s | 0 at the end of init |

Rates: the VGA refresh is 25.175 MHz / (800 × 449) = 70.0863 Hz (17,024.6 PIT clocks), so 4 × 70.0863 = 280.345
callbacks / s and 35 × 280.345 = 9,812.08 DSMI units / s.

### 1.2 Callbacks, refreshes and records in the capture

- The first callback, **M = 1**, is interrupt 91, at program time 0.551284 s (= 3 × 4,085 PIT clocks after the
  retrace of refresh 37); **M = 2** is the retrace callback of refresh 38 (0.555281 s). Interrupts 1..90 (from frame
  15, when the timer was installed, to the end of init) had no callback.
- Record k is written at program time (k + 1) / 70.0863 s, after the retrace callback of refresh k:
  **M_ret(k) = 4k − 150** for k ≤ 3044, **4k − 151** for k ≥ 3045 (one callback was lost in the credits, 1.6).
  So a record shows the screen and DAC as they are after callback M_ret(k).
- Callback M_ret(k) + s (s = 1, 2, 3) is at (k + 412/449) / 70.0863 s + s × 4,085 / 1,193,182 s.
- Interrupt number = M + 90 (+ 1 from record 3045 on).
- Frame clock at record k = k − 37 (k − 38 from record 11967 on, 1.6). Frames shown: `full.flips`.

### 1.3 The music position

DSMI's default rate (ampInit 0x40d907: [0x4233a8] = 50 Hz, period 10000 / 50 = 200 units) holds for the first 6
player ticks; row 0 is processed by the 6th; from row 0 on the period is 192 units (10000 / 52, `div` = integer)
with 4 ticks per row, so **a row takes 768 DSMI units = 21.943 callbacks = 5.4857 refreshes = 78.271 ms**; in order 36
a row takes 2,304 units (3 × 768). Tick k runs in the first callback M with 35 M > Σ (the sum of the periods of
ticks 1..k), i.e. M = floor(Σ / 35) + 1. The song is played without breaks: order o, row r is song row R = 64 o + r.

**Song row R starts in callback c_R = floor((1200 + 768 R) / 35) + 1** (R ≤ 2304, i.e. up to 36:00),
c_R = floor((1200 + 768 × 2304 + 2304 (R − 2304)) / 35) + 1 in order 36.

Examples: ticks 1..6 in callbacks 6, 12, 18, 23, 29, **35 (row 0, the first note: interrupt 125, GUS writes)**; then
ticks in 40, 46, 51, 57 (row 1), …; row 5 in 145 (1200 + 3840 = 5040 = 35 × 144: the strict ">" puts it in 145).
Order o starts in callback floor((1200 + 49,152 o) / 35) + 1 (o ≤ 36); 36:24, the end, in 52,171.

The song therefore runs at 98.12 % of DSMI's nominal speed (9,812.08 instead of 10,000 units / s, if a unit is
1/10,000 s as the period 10000 / rate suggests) on every machine: the increment is computed from the one-shot period
as trunc(4,085 × 2,500.0 × 3.4809245335561122e−6) = trunc(35.55) = 35 (0x401010, doubles at 0x420392 / 0x42039a),
and the callbacks come 4 per refresh, not one per 4,085 PIT clocks (the 4th interval waits for the retrace).

### 1.4 When the script ran (all 97 entries)

The main loop (section 3) checks the script after every drawn frame, so an entry runs at the end of the first frame
drawn after its row started, plus the time of the entries before it in the same pass. Below: c_R = callback in
which the row started, **M_entry** = [0x4248c4] when the routine ran (exact where the routine resets P: M(k) −
part_ticks(k); for palette entries the first DAC step, which the palette model reproduces exactly, tells whether it
ran before or after the retrace callback of record k; otherwise the range between the records), the delay
M_entry − c_R in callbacks, and the **first retrace callback after it** (M, and the record that first shows its
effect: its first palette step, its first tick-routine call). Entries of one pass share their time.

| # | pos | routine | what it does | R | c_R | M_entry | delay | first retrace M (record) |
|---|---|---|---|---|---|---|---|---|
| 0 | 0:00 | 0x4038c8 | intro start: tick 0x403214, frame 0x403748 | 0 | 35 | 0 | (−35) | 2 (38) |
| 1 | 3:00 | 0x4031a0 | intro: light drops on ([0x425570] = 1), 130 drops at (rand() % 320, rand() % 200), +8 = 7000 (260 rand calls) | 192 | 4248 | 4250..4254 | 2..6 | 4254 (1101) or 4258 (1102) |
| 2 | 3:63 | 0x4038dc | intro end: routines 0, frees the intro's memory (tag [0x42555c]) | 255 | 5630 | 5631 | 1 | 5634 (1446) |
| 3 | 3:63 | 0x403164 | logo start: palette logo/logo.pal, tick 0x402e30, frame 0x402f1c | 255 | 5630 | 5631 | 1 | 5634 (1446) |
| 4 | 4:32 | 0x400498 | white flash (15) | 288 | 6354 | 6354..6357 | 0..3 | 6358 (1627) |
| 5 | 4:32 | 0x402f10 | logo: [0x42553c] = 1 (the frame routine also draws bitmap [0x425534] at (34, 13)) | 288 | 6354 | 6354..6357 | 0..3 | 6358 (1627) |
| 6 | 5:00 | 0x400498 | white flash (15) | 320 | 7057 | 7058..7061 | 1..4 | 7062 (1803) |
| 7 | 5:32 | 0x400454 | inverse flash (20) | 352 | 7759 | 7762 | 3 | 7766 (1979) |
| 8 | 5:63 | 0x400498 | white flash (15) | 383 | 8439 | 8440 | 1 | 8442 (2148) |
| 9 | 5:63 | 0x403184 | logo end: routines 0, frees tag [0x425500] | 383 | 8439 | 8440 | 1 | 8442 (2148) |
| 10 | 5:63 | 0x4073c8 | credits start: tick 0x405f30, frame 0x406794, palette credits/tunnel.pal | 383 | 8439 | 8440 | 1 | 8442 (2148) |
| 11 | 5:63 | 0x400498 | white flash (15) | 383 | 8439 | 8440 | 1 | 8442 (2148) |
| 12 | 6:32 | 0x400498 | white flash (15) | 416 | 9163 | 9163..9165 | 0..2 | 9166 (2329) |
| 13 | 7:00 | 0x405f1c | credits: white flash, [0x426438] += 1 (next name) | 448 | 9865 | 9866..9869 | 1..4 | 9870 (2505) |
| 14 | 7:32 | 0x405f1c | same | 480 | 10567 | 10567..10569 | 0..2 | 10570 (2680) |
| 15 | 8:00 | 0x405f1c | same | 512 | 11270 | 11270..11273 | 0..3 | 11274 (2856) |
| 16 | 8:32 | 0x405f1c | same | 544 | 11972 | 11974..11977 | 2..5 | 11978 (3032) |
| 17 | 9:00 | 0x405f1c | same | 576 | 12674 | 12674..12676 | 0..2 | 12677 (3207) |
| 18 | 9:32 | 0x405f1c | same | 608 | 13376 | 13376 | 0 | 13377 (3382) |
| 19 | 9:48 | 0x400498 | white flash (15) | 624 | 13727 | 13729 | 2 | 13733 (3471) |
| 20 | 9:48 | 0x405f10 | credits: [0x426430] = 1 | 624 | 13727 | 13729 | 2 | 13733 (3471) |
| 21 | 10:32 | 0x400454 | inverse flash (20) | 672 | 14780 | 14781..14784 | 1..4 | 14785 (3734) |
| 22 | 11:00 | 0x400498 | white flash (15) | 704 | 15483 | 15483..15484 | 0..1 | 15485 (3909) |
| 23 | 11:32 | 0x400454 | inverse flash (20) | 736 | 16185 | 16185..16188 | 0..3 | 16189 (4085) |
| 24 | 11:48 | 0x400438 | fade to black (40) | 752 | 16536 | 16537..16540 | 1..4 | 16541 (4173) |
| 25 | 11:63 | 0x400498 | white flash (15) | 767 | 16865 | 16866 | 1 | 16869 (4255) |
| 26 | 11:63 | 0x4073e8 | credits end: routines 0, frees tag [0x426480] | 767 | 16865 | 16866 | 1 | 16869 (4255) |
| 27 | 11:63 | 0x4057bc | sprite picture 2 ([0x4227fc] = 2): tick 0x404738, frame 0x4047a0, palette nigga/nigga2.pal | 767 | 16865 | 16866 | 1 | 16869 (4255) |
| 28 | 12:32 | 0x404680 | sprites: 15 float parameters 0x422810..0x422848 | 800 | 17589 | 17589 | 0 | 17593 (4436) |
| 29 | 12:32 | 0x400498 | white flash (15) | 800 | 17589 | 17589 | 0 | 17593 (4436) |
| 30 | 12:63 | 0x400498 | white flash (15) | 831 | 18269 | 18269 | 0 | 18273 (4606) |
| 31 | 13:00 | 0x405ba0 | sprite picture 4 ([0x4227fc] = 4): the same routines set again (P = 0), palette nigga/nigga4.pal | 832 | 18291 | 18293 | 2 | 18297 (4612) |
| 32 | 13:32 | 0x405f00 | sprites: [0x422800] = 30 | 864 | 18993 | 18993 | 0 | 18997 (4787) |
| 33 | 13:48 | 0x400438 | fade to black (40) | 880 | 19345 | 19345 | 0 | 19349 (4875) |
| 34 | 14:00 | 0x404728 | sprites end: routines 0 | 896 | 19696 | 19697 | 1 | 19701 (4963) |
| 35 | 14:00 | 0x4098c4 | flower start: builds the scene (SurRender, 732 rand calls; ran until M 19699), tick 0x40940c, frame 0x4095f8, palette flower/jeesus.pal | 896 | 19696 | 19699 | 3 | 19701 (4963) |
| 36 | 14:00 | 0x400498 | white flash (15) | 896 | 19696 | 19699..19700 | 3..4 | 19701 (4963) |
| 37 | 14:04 | 0x400498 | white flash (15) | 900 | 19783 | 19785 | 2 | 19789 (4985) |
| 38 | 14:24 | 0x409358 | flower: n = ([0x42294c] + 1) % 9 → [0x42294c]; rotates object [0x4b6620] (0x4108dc, angle table 0x422918[n] degrees), clears bit 0 of its byte +0xc4, [0x422948] = 0, [0x422950] = 1, [0x422954] = 0; n even: white flash, odd: inverse flash | 920 | 20222 | 20225 | 3 | 20229 (5095) |
| 39 | 14:63 | 0x4093f0 | flower: sets bit 0 of [[0x4b6620] + 0xc4], [0x422950] = 0, white flash | 959 | 21078 | 21081 | 3 | 21085 (5309) |
| 40 | 15:00 | 0x400454 | inverse flash (20) | 960 | 21100 | 21101 | 1 | 21105 (5314) |
| 41 | 15:00 | 0x409358 | flower event (as 38) | 960 | 21100 | 21101 | 1 | 21105 (5314) |
| 42 | 15:04 | 0x400498 | white flash (15) | 964 | 21188 | 21189 | 1 | 21193 (5336) |
| 43 | 15:32 | 0x400498 | white flash (15) | 992 | 21802 | 21805 | 3 | 21809 (5490) |
| 44 | 15:36 | 0x400498 | white flash (15) | 996 | 21890 | 21893 | 3 | 21897 (5512) |
| 45 | 15:48 | 0x4093f0 | flower event (as 39) | 1008 | 22153 | 22157 | 4 | 22161 (5578) |
| 46 | 15:56 | 0x400454 | inverse flash (20) | 1016 | 22329 | 22329 | 0 | 22333 (5621) |
| 47 | 16:00 | 0x409358 | flower event (as 38) | 1024 | 22504 | 22505 | 1 | 22509 (5665) |
| 48 | 16:12 | 0x4093f0 | flower event (as 39) | 1036 | 22768 | 22769 | 1 | 22773 (5731) |
| 49 | 16:16 | 0x409358 | flower event (as 38) | 1040 | 22855 | 22857 | 2 | 22861 (5753) |
| 50 | 16:56 | 0x4093f0 | flower event (as 39) | 1080 | 23733 | 23733 | 0 | 23737 (5972) |
| 51 | 16:56 | 0x400498 | white flash (15) | 1080 | 23733 | 23733 | 0 | 23737 (5972) |
| 52 | 16:60 | 0x400498 | white flash (15) | 1084 | 23821 | 23821 | 0 | 23825 (5994) |
| 53 | 17:00 | 0x409358 | flower event (as 38) | 1088 | 23909 | 23909 | 0 | 23913 (6016) |
| 54 | 17:12 | 0x4093f0 | flower event (as 39) | 1100 | 24172 | 24173 | 1 | 24177 (6082) |
| 55 | 17:32 | 0x409358 | flower event (as 38) | 1120 | 24611 | 24613 | 2 | 24617 (6192) |
| 56 | 17:60 | 0x4093f0 | flower event (as 39) | 1148 | 25225 | 25229 | 4 | 25233 (6346) |
| 57 | 17:60 | 0x400498 | white flash (15) | 1148 | 25225 | 25229 | 4 | 25233 (6346) |
| 58 | 18:00 | 0x409358 | flower event (as 38) | 1152 | 25313 | 25313 | 0 | 25317 (6367) |
| 59 | 18:28 | 0x4093f0 | flower event (as 39) | 1180 | 25927 | 25929 | 2 | 25933 (6521) |
| 60 | 18:32 | 0x409358 | flower event (as 38) | 1184 | 26015 | 26017 | 2 | 26021 (6543) |
| 61 | 18:32 | 0x400454 | inverse flash (20) | 1184 | 26015 | 26017 | 2 | 26021 (6543) |
| 62 | 18:63 | 0x4093f0 | flower event (as 39) | 1215 | 26695 | 26697 | 2 | 26701 (6713) |
| 63 | 19:00 | 0x409358 | flower event (as 38) | 1216 | 26717 | 26717 | 0 | 26721 (6718) |
| 64 | 19:12 | 0x4093f0 | flower event (as 39) | 1228 | 26981 | 26981 | 0 | 26985 (6784) |
| 65 | 19:28 | 0x400454 | inverse flash (20) | 1244 | 27332 | 27333 | 1 | 27337 (6872) |
| 66 | 19:48 | 0x400438 | fade to black (40) | 1264 | 27771 | 27773 | 2 | 27777 (6982) |
| 67 | 19:63 | 0x409e90 | flower end: routines 0 (nothing freed) | 1279 | 28100 | 28101 | 1 | 28105 (7064) |
| 68 | 19:63 | 0x402c28 | flight start: clears both screen buffers, palette flight/map.pal, then 0x4004ac(1.0, 60) (fade in from black over 60), tick 0x4021f8, frame 0x4028e4 | 1279 | 28100 | 28101 | 1 | 28105 (7064) |
| 69 | 23:48 | 0x400438 | fade to black (40) | 1520 | 33388 | 33389..33392 | 1..4 | 33393 (8386) |
| 70 | 23:63 | 0x400498 | white flash (15) | 1535 | 33717 | 33719 | 2 | 33721 (8468) |
| 71 | 23:63 | 0x402c7c | flight end: routines 0, frees the map heightfield [0x424ff8] (0x40b690) | 1535 | 33717 | 33719 | 2 | 33721 (8468) |
| 72 | 23:63 | 0x407f90 | water start: clears both buffers, palette water/water.pal, allocates "water frame 1 / 2", tick 0x407c78, frame 0x407d78 | 1535 | 33717 | 33719 | 2 | 33721 (8468) |
| 73 | 27:48 | 0x408014 | **water** end (not flower): routines 0, frees the water's tag [0x4265f0] | 1776 | 39005 | 39008 | 3 | 39009 (9790) |
| 74 | 27:48 | 0x400498 | white flash (15) | 1776 | 39005 | 39008 | 3 | 39009 (9790) |
| 75 | 27:48 | 0x40585c | sprite picture 3 ([0x4227fc] = 3): routines, palette nigga/nigga9.pal | 1776 | 39005 | 39008 | 3 | 39009 (9790) |
| 76 | 28:63 | 0x400498 | white flash (15) | 1855 | 40739 | 40741 | 2 | 40745 (10224) |
| 77 | 29:00 | 0x405f00 | sprites: [0x422800] = 30 | 1856 | 40761 | 40761 | 0 | 40765 (10229) |
| 78 | 29:24 | 0x400498 | white flash (15) | 1880 | 41287 | 41289 | 2 | 41293 (10361) |
| 79 | 29:24 | 0x40571c | sprite picture 1 ([0x4227fc] = 1): routines, palette nigga/nigga1.pal | 1880 | 41287 | 41289 | 2 | 41293 (10361) |
| 80 | 29:48 | 0x400498 | white flash (15) | 1904 | 41814 | 41817 | 3 | 41821 (10493) |
| 81 | 29:48 | 0x404680 | sprites: the 15 parameters again | 1904 | 41814 | 41817 | 3 | 41821 (10493) |
| 82 | 29:63 | 0x404728 | sprites end: routines 0 | 1919 | 42143 | 42145 | 2 | 42149 (10575) |
| 83 | 29:63 | 0x400498 | white flash (15) | 1919 | 42143 | 42145 | 2 | 42149 (10575) |
| 84 | 29:63 | 0x40a678 | twirl start: [0x42296c] = 0, [0x422970] = 1, parameters from 0x4b6c88.. → 0x4b6d08.., white flash, palette [0x4b6d54] = flower/jeesus.pal, tick 0x40a20c, frame 0x40a264 | 1919 | 42143 | 42145 | 2 | 42149 (10575) |
| 85 | 30:24 | 0x40a098 | twirl: [0x42296c] = ([0x42296c] + 1) % 5 picks the picture and palette (1: nigga/nigga2, 2: nigga/nigga4, 3: flower/jeesus2.raw with flower/jeesus.pal, 4: flight/river.raw with flight/map.pal, 0: flower/jeesus1.raw with jeesus.pal), [0x422970] = +1 % 5 the parameter set (8 floats from 0x4b6c68 + 32 n), sets palette entry 0 to black **in the data block**, white flash, then 0x400364(new palette); [0x4b6d40] = picture + 0x8000 | 1944 | 42692 | 42693 | 1 | 42697 (10712) |
| 86 | 30:24 | 0x400454 | inverse flash (20) | 1944 | 42692 | 42693 | 1 | 42697 (10712) |
| 87 | 30:48 | 0x40a098 | twirl event (picture 2) | 1968 | 43218 | 43218..43220 | 0..2 | 43221 (10843) |
| 88 | 30:12 | 0x40a098 | twirl event (picture 3): **out of order in the table**, so it runs right after 87 (789..791 callbacks after its own row) | 1968 | 43218 | 43218..43220 | 0..2 | 43221 (10843) |
| 89 | 30:56 | 0x400498 | white flash (15) | 1976 | 43394 | 43394..43396 | 0..2 | 43397 (10887) |
| 90 | 31:36 | 0x40a098 | twirl event (picture 4) | 2020 | 44359 | 44359..44360 | 0..1 | 44361 (11128) |
| 91 | 31:63 | 0x400498 | white flash (15) | 2047 | 44952 | 44952 | 0 | 44953 (11276) |
| 92 | 31:63 | 0x40a738 | twirl end: routines 0, frees tag [0x4b6d30] | 2047 | 44952 | 44952 | 0 | 44953 (11276) |
| 93 | 31:63 | 0x40461c | temple start: clears both buffers, white flash, palette temple/temple.pal, tick 0x40407c, frame 0x404424 | 2047 | 44952 | 44952 | 0 | 44953 (11276) |
| 94 | 35:48 | 0x400438 | fade to black (40) | 2288 | 50240 | 50245..50248 | 5..8 | 50249 (12600) |
| 95 | 36:16 | 0x404668 | temple end: routines 0 (nothing is drawn any more; VGA keeps the last frame under the black palette) | 2320 | 51644 | 51646 | 2 | 51649 (12950) |
| 96 | 36:24 | 0x400228 | `ret`; the index then points at entry 97 (1000:0, routine 0) and 0x400b3c sets quit: the demo ends | 2328 | 52171 | 52171.. | – | exit at program time 186.647 s (refresh 13081) |

The delays are 0..8 callbacks (0..2 refreshes; mostly 0..3) and depend on the frame rate of the machine. 0x400498 =
white flash, 0x400454 = inverse flash, 0x400438 = fade to black (section 6). The table's routine for "flower event
(as 38)" alternates flash / inverse by the parity of its counter: 1, 2, …, 8, 0, 1, … → inverse, flash, inverse, …
(entries 38, 41, 47, 49, 53, 55, 58, 60, 63: n = 1..9 mod 9 = 1, 2, 3, 4, 5, 6, 7, 8, 0).

### 1.5 Parts: origin of their clocks, frame rate, latency

| part | start (entry) | M_reset (P = M − M_reset) | first tick-routine call: record, M, P | clock the part reads | frames shown / refresh in the capture | frame routine waits for the copy |
|---|---|---|---|---|---|---|
| intro | 0 | 0 | 38, 2, 2 | P in the tick routine 0x403214 (= M) | 0.692 | no |
| logo | 3 | 5631 | 1446, 5634, 3 | none (per-refresh tick state) | 1.000 | no |
| credits | 10 | 8440 | 2148, 8442, 2 | none | 0.912 | no |
| sprite picture 2 | 27 | 16866 | 4255, 16869, 3 | M in the frame routine 0x4047a0 | 1.000 | yes |
| sprite picture 4 | 31 | 18293 | 4612, 18297, 4 | M | 1.000 | yes |
| flower | 35 | 19699 | 4963, 19701, 2 | none | 0.687 | yes |
| flight | 68 | 28101 | 7064, 28105, 4 | P in the tick routine (0x401dbc from 0x4021f8) | 1.000 | no |
| water | 72 | 33719 | 8468, 33721, 2 | M in the frame routine (0x407400 via 0x4078d4) | 0.887 | no |
| sprite picture 3 | 75 | 39008 | 9790, 39009, 1 | M | 1.000 | yes |
| sprite picture 1 | 79 | 41289 | 10361, 41293, 4 | M | 1.000 | yes |
| twirl | 84 | 42145 | 10575, 42149, 4 | none | 1.000 | no |
| temple | 93 | 44952 | 11276, 44953, 1 | M in the tick routine (0x403f10 from 0x40407c) and in the frame routine (0x4040b0) | 0.725 | no |

A tick routine runs once per refresh, in the retrace callback, so at its n-th call (n = 1, 2, …) P = P₁ + 4 (n − 1)
and M = M₁ + 4 (n − 1) with the values of the table (only the temple misses one call, at record 11967, 1.6).

**Frame latency.** A frame routine reads the clocks while it draws; the frame appears at the first retrace callback
after its flip (0x400b14). Measured (hooked runs) as M of that retrace callback minus M at the frame routine's start:
intro 6..10 callbacks (0.69 frames per refresh), logo 4..7 (1.2 frames drawn per refresh, 18 % of them replaced
before being shown), sprites exactly 4 (the routine waits for the copy, so every frame starts right after a retrace
callback and is shown at the next one). On an infinitely fast machine the latency would be 0..1 callbacks for the
parts that do not wait and exactly 4 for those that do (sprites, flower).

### 1.6 Irregularities in the capture

- **[0x42278c]** (incremented by the credits frame routine 0x406794 around 0x4061fc): while set, the callback returns
  at once: M, P, the music (DSMI time) and the retrace work all skip that interrupt. It hit once, between records 3044
  and 3045 (a non-retrace interrupt): from there M = interrupt − 91 and the retrace callbacks are at M ≡ 1 (mod 4)
  instead of 2. The music lost the same callback, so the music position relative to M is unchanged.
- **[0x4248c0]** (the "busy" counter: data file lookups, part inits, the music start, the screenshot, and the frame
  routines of logo 0x402f1c, temple 0x404424 and water 0x407d78 around their scene set-up 0x402e44 / 0x4040b0 /
  0x407cac): the retrace callback skips its frame part (frame clock, palette step, copy, tick routine); M, P and the
  music go on. It hit once, record 11967 (temple): the frame clock lags by 1 from there, the temple's tick routine
  missed one call, no fade was running.
- On a real machine both happen when an interrupt falls into those windows (in the capture: once in the 8,428
  interrupts of the credits, once in the 3,698 refreshes of logo, water and temple). A remake ignores them.

### 1.7 How the remake should time things

- One master clock: M as a real number (280.345 per second). M = 1 at the music start; the song's first note is at
  M = 35; row R starts at c_R (1.3); DSMI time 35 M drives the player.
- Refresh n = record 38 + n has its retrace callback at **M = 4n + 2** (4n + 1 from record 3045 on if the capture is
  to be matched callback for callback; the difference is 1/4 refresh against the music). Everything the original
  steps per refresh (tick routines, palette steps) is stepped there; everything it reads from [0x4248c4] /
  [0x4248c8] is evaluated from M (P = M − M_reset of the part, 1.5).
- Script events: at M_entry of the table (1.4), which puts every visible effect (fade start, first tick, new
  routines) in the same refresh as in the capture. Where only a range is known, the "first retrace" column is what
  matters: no entry with a range resets a clock.
- Frame routines: render refresh n with the clocks M_ret − L: L = 4 for the parts that wait for the copy (sprites,
  flower: exactly what the original does on any machine that keeps 70 frames per second), L = 0 for the others (what
  the original approaches on a fast machine; the capture has 4..10, so a remake shows those parts 1..2 refreshes
  earlier than the capture). State this under "Known differences".
- Random numbers: see section 9: init-time sequences replay exactly, per-frame ones do not.

### 1.8 The first refreshes after the music start

| record (refresh) | what happens before it |
|---|---|
| 15 | int 10h mode 13h, timer installed (interrupts from record 16 on, no callback) |
| 37 | ampInit / ampPlayModule done (position 0:0 shows), DAC never written (all 0 in the emulator; the default mode 13h palette on a real VGA, over a cleared screen) |
| 38 | interrupt 90; init ends: callback installed, M = P = frame clock = 0; main loop: entry 0 (M = 0) sets the intro's routines, the first intro frame starts; callback 1 (interrupt 91); callback 2 = retrace: frame clock 1, fade step 1 of 120 (the intro init's 0x4004ac(1.0, 120) from black), intro tick routine (P = 2), nothing to copy yet |
| 39 | the first frame (flipped at M = 5) is copied at M = 6; player tick 1 at M = 6 (silent) |
| 46..47 | player tick 6 = row 0 at M = 35 (interrupt 125, program time 0.672850 s): the first note |

---------------------------------------------------------------------------------------------------------------

## 2. main 0x400010, init 0x4007c0, exit 0x400a10

```c
int main(int argc, char **argv)                 // 0x400010
{
    atexit(exit_handler);                       // 0x40a787(0x400a10)
    if ([0x422974] == 1) [0x42297c] += [0x422978];   // VGA pointer offset for another extender: 0, unused
    [0x42277c] = 0;                             // debug: off (font, overlay text, inside.lst, inside.dmp)
    [0x422780] = 1;                             // read data from INSIDE.DAT (0: loose files)
    for (i = 1; i < argc; i++) ;                // command line ignored
    init();                                     // 0x4007c0
    main_loop();                                // 0x400cb4
}                                               // return → exit → 0x400a10
```

init 0x4007c0, in order:
1. sprintf(0x424078, "INSIDE by CNCD - please see file 'readme.txt' for more information") (0x40bff2): the text
   printed at exit (error messages overwrite the same buffer, 0x400330 = sprintf + exit(−1)).
2. [0x42487c] = the largest free memory block (0x400070: DPMI 0500h).
3. 0x40b40c(2500): the named-allocation list, 2,500 nodes of 24 bytes.
4. ([0x422780] = 0 only: truncate "inside.lst".)
5. [0x4b6ed8] = 1 (allocations are zero-filled), [0x4b6ed4] = 1 (warn about null blocks), [0x4248c0] = 0,
   [0x42488c] = 0, [0x424880] = 0; tag = 0x40b3c0() → [0x4248a8], 0x40b4d0(tag).
6. 0x4001c4: INSIDE.DAT (section 8).
7. 0x40a83c(320, 200, x0 0, y0 0, x1 320, y1 200): screen geometry (section 7).
8. Allocations (0x40b4d8, zero-filled): "screen buffer 1" 64,000 → [0x4248d0] = draw target [0x4b6ec0];
   "screen buffer 2" → [0x4248cc]; "base palette" 768 → [0x424890]; "current palette" 768 → [0x4248a4];
   "fl. point palette" 3,072 → [0x4248b8]; "fl. point palette adders" 3,072 → [0x424884]; "IRQ stack" 65,536 →
   [0x424878].
9. If [0x42487c] < 5,500,000: "Inside requires approx. 5500 kB of free memory in order to run." and exit.
10. 0x400d00: debug font (skipped). 0x40c034: engine tables (pointers into 0x4b9408..0x4b94c8, used by 0x413e68).
11. [0x4248b4] = [0x424888] = [0x422984] = [0x4248bc] = [0x4b8f04] = [0x4248ac] = 0;
    0x40a7c4([0x424878] + 0xf618): the timer handler's stack top [0x4b6ea2].
12. 0x40c6e4: keyboard handler 0x40c721 on vector 9 (key table 0x4b9028[128], 1 = down; [0x4b9024] = last code).
13. 0x40bd48(4): the retrace timer (section 4). int 10h AX = 13h (unless [0x422988], a no-graphics debug flag, = 0).
14. Part inits: 0x403770 intro, 0x402f9c logo, 0x406d10 credits, 0x402948 flight, 0x404464 temple, 0x404fe8 /
    0x40506c / 0x405110 / 0x4051d4 sprite pictures 1, 2, 9, 4, 0x407dec water, 0x409730 flower, 0x405298 sprite scene,
    0x409ea0 twirl. (The intro init sets its palette and starts a fade, 0x400364 + 0x4004ac(1.0, 120); the sprite
    inits set entry 0 of nigga2 / nigga9 / nigga4.pal to black in the data block; the flower's scene reader puts a 0
    over the last byte of flower/tunnel1.lws.)
15. 0x4010b4: music start (section 5).
16. 0x401010([0x4b8f00]): DSMI time increment = trunc(4,085 × 2500.0 × 3.4809245335561122e−6) = 35.
17. [0x4b8f04] = 0x40070c (callback on), then [0x4248c8] = [0x4248c4] = [0x424880] = [0x42488c] = 0.

exit_handler 0x400a10 (atexit): [0x42279c] = 1; [0x4b8f04] = 0 (callback off), routines 0, [0x422984] = 0; the
current palette = 0 and written to the DAC (black); int 10h AX = 3; 0x40bd8c: PIT channel 0 back to mode 2 count 0
(18.2 Hz), old IRQ0 vector; 0x40c714: old keyboard vector; (debug: "inside.dmp"); 0x401054: music stop
(ampStopModule 0x40dc21, free the module 0x40fb20, 0x40f775, SDIStopOutput [0x4227a8], unload the driver 0x40f51c);
(debug: "memory used %d"); prints the text buffer 0x424078 ("%s"). The capture's program exits with code 1 at 186.65 s.

## 3. Main loop 0x400cb4 and the script 0x400b3c

```c
void main_loop(void)                            // 0x400cb4
{
    while (!quit /*[0x422788]*/ && !key[1] /*Esc, [0x4b9029]*/) {
        if (frame_fn /*[0x424888]*/) frame_fn();    // draws one frame and flips (0x400b14)
        while (!quit && script()) ;                 // every entry that is due, in table order
        if (key[0x1f] /*'S', [0x4b9047]*/) screenshot();   // 0x400c40: shot%u.tga of [0x4248d0], base palette
    }
}

int script(void)                                // 0x400b3c, table 0x4222e4: {int order, row; void (*fn)(void)}
{
    int pos = amp_order() * 64 + amp_row();     // 0x40de09 / 0x40de21: [0x422a5d] (byte), [0x422a61] (word);
                                                // -1 / -1 before ampInit ([0x423399] bit 0 clear)
    int ran = 0;
    if (pos >= table[idx].order * 64 + table[idx].row) {   // idx = [0x4248bc]
        table[idx].fn(); idx++; ran = 1;
    }
    if (table[idx].fn == 0) quit = 1;           // entry 97 = {1000, 0, 0}
    if (key[1]) quit = 1;
    return ran;
}
```

The order is read before the row; the player changes both in one interrupt, so a tick between the two reads gives
the old order with the new row (pos too small): harmless, the entry runs after the next frame. The table has 98
entries (97 + the end); the routines are listed in 1.4. Entries with the same position run in table order in one
pass, before the next frame; entry 88 (30:12) follows 87 (30:48).

## 4. The retrace timer 0x40bd48..0x40bfdf

```c
void timer_install(int n /*4*/)                 // 0x40bd48
{
    callback /*[0x4b8f04]*/ = 0; depth /*[0x4b8f08]*/ = 0;
    period /*[0x4b8f00]*/ = 0x1000; factor /*[0x4b8f14]*/ = 960;
    if (n > 0) { timer_measure(n); set_vector(8, 0x40becf); }   // 0x40bdad, 0x40a7ca (keeps the old one)
}

void timer_measure(int n)                       // 0x40bdad
{
    count_in_frame /*[0x4b8f10]*/ = 0; per_frame /*[0x4b8f0c]*/ = n;
    cli();
    for (tries = 100; ; ) {
        if (--tries == 0) { p2 = 0xafc8; break; }        // fallback 45,000
        wait_retrace_start();                  // 0x3da bit 3: wait while set, then until set
        pit(0x36, 0);                          // ch 0, mode 3, count 65536
        wait_retrace_start(); latch(); p1 = -read16() & 0xffff;   // mode 3 counts by 2: 2 x PIT clocks per refresh
        wait_retrace_start(); pit(0x36, 0);
        wait_retrace_start(); latch(); p2 = -read16() & 0xffff;
        if (abs(p2 - p1) <= 4) break;
    }
    period = ((p2 * 960 / 1000) / n) >> 1;     // unsigned div, signed idiv: 34,049 -> 32,687 -> 8,171 -> 4,085
    wait_retrace_start();
    pit(0x30, period);                         // ch 0, mode 0 (one shot)
    bios_acc /*[0x4b8efc]*/ = 0; outb(0x20, 0x20);
}

void irq0(void)                                 // 0x40becf (iretd)
{
    cli();
    if (depth == 0) { switch to the IRQ stack [0x4b6ea2]; fnstenv; fnsave; }   // FPU now in its init state (CW 0x37f)
    sti(); depth++;
    int at_retrace = 0;
    if (++count_in_frame >= per_frame) {       // the 4th interrupt after a retrace
        count_in_frame = 0; at_retrace = 1;
        wait_retrace_start();                  // busy wait, ~684 PIT clocks (0.57 ms) per refresh
        pit(0x30, period); outb(0x20, 0x20);   // EOI before the callback: other IRQs and IRQ0 can nest
    } else
        pit(0x30, period);                     // EOI after the callback
    if (callback) callback();                  // 0x40070c
    cli(); if (--depth == 0) { frstor; fldenv; back to the old stack; }
    sti();
    if (at_retrace) return;
    if ((uint16)(bios_acc += period) overflowed) chain to the old vector [0x4b6e40] (it sends the EOI);
    else outb(0x20, 0x20);
}
```

So the interrupts of a refresh are at the retrace + 0, 4,085, 8,170, 12,255 PIT clocks, and the 4th fires at
16,340, ~684 clocks before the next retrace, where it waits. The BIOS clock gets 3 × 4,085 per refresh (13.1 Hz
instead of 18.2). The interrupt handler runs with the FPU re-initialised by `fnsave`: **control word 0x037F (64-bit
precision, nearest)** in the callback, the tick routines and the palette steps, while main-line code runs with
Watcom's 0x127F (section 10). A nested interrupt (only possible inside a retrace callback, after its early EOI) does
not save the FPU or switch stacks.

## 5. The timer callback 0x40070c and the music

```c
void callback(void)                             // 0x40070c, every interrupt
{
    if ([0x42278c]) return;                     // credits guard (1.6): the interrupt is lost for everything
    M++;  P++;                                  // [0x4248c4], [0x4248c8]
    music_poll();                               // 0x400fcc
    if (busy_frame /*[0x422798]*/) return;      // reentered (nested interrupt in a long retrace callback)
    busy_frame++;
    if ([0x4248c0] == 0 && count_in_frame == 0) {   // retrace callback, not busy
        frame_clock += 1.0f;                    // [0x424880]
        palette_step();                         // 0x400540
        if (frame_ready /*[0x422984]*/ == 1) {
            frame_ready = 2;
            copy_to_vga([0x4248cc]);            // 0x400694
            frames_shown += 1.0f;               // [0x42488c]
            frame_ready = 0;
        }
        if (tick_fn /*[0x4248b4]*/) tick_fn();
    }
    busy_frame--;
}

void music_poll(void)                           // 0x400fcc
{
    if (![0x4227a0]) return;                    // music started
    dsmi_time += 35;                            // 0x40f620: [0x4b9148] += [0x4b9144]
    if ([0x4227ac] || [0x422784]) return;       // reentered / sound error
    [0x4227ac]++;
    driver_poll(0);                             // 0x40faaf: the driver's function +0x5c of device 0
    amp_poll();                                 // 0x40d878: player ticks that are due (1.3)
    [0x4227ac]--;
}
```

Music start 0x4010b4 (skipped if [0x422784], the sound error flag; [0x4248c0] held throughout): 0x40e900(0x28) and
0x40e9b0 (DSMI run-time set-up, DOS memory buffers); 0x40f5a1(36): DSMI time service (time 0, increment 36,
registers GetDSMITime / LockDSMITime / UnlockDSMITime for the driver); registers MCPSampleRealAddress and two stubs;
0x40f6cc: CDI tables; 0x400efc: reads SOUND.CFG (13 bytes: the driver file name, then 92 bytes of device settings
→ 0x4249fc), registers GetEnvString / SetVector, loads the driver (0x40ef10, here SDIGUS.DSD), resolves
SDIInitDevice / SDIStartOutput / SDIStopOutput and calls SDIInitDevice(0x4249fc) — failure: "Error loading sound
driver. Please run 'setup.exe' first"; SDIStartOutput(); ampInit(0) 0x40d907 (position 0:0, rate 50 Hz, last tick
time = DSMI time = 0); 0x40fa69(24, 0): the driver's function +0x54 of device 0 with 24 (probably the number of
channels); loads
"eye.amf" (0x4103d0, failure: "Error in loading module 'eye.amf'. Please configure"); ampPlayModule 0x40dbb9(1,
module); [0x4227a0] = 1. Then init calls 0x401010 (section 2 step 16). The music scope covers DSMI itself.

## 6. Palette 0x400364..0x400612

State: base [0x424890] (768 bytes, 6-bit), current [0x4248a4] (768 bytes: what the DAC shows), fl [0x4248b8] (768
float32), add [0x424884] (768 float32), steps left [0x4248ac] (int), level [0x422790] (float32, initially 0.0).

```c
void fade(float level, int n)                   // 0x4004ac (stdcall-like: level, n on the stack)
{
    if (n <= 0) n = 1;
    double r = 1.0 / n;                         // x87, main line: 53-bit
    for (i = 0; i < 768; i++) {
        fl[i]  = (float)current[i];
        add[i] = (float)((base[i] * level - fl[i]) * r);   // one rounding per operation, then to float32
    }
    level_ = level; steps = n;
}

void set_palette(const uint8 *p)                // 0x400364
{
    for (i = 0; i < 768; i++) base[i] = min(p[i], 63);
    fade(level_, steps);                        // retarget the running fade; no fade running: 1 step
}

void palette_step(void)                         // 0x400540, retrace callback (64-bit precision there)
{
    if (steps <= 0) return;
    steps--;
    for (i = 0; i < 768; i++) {
        fl[i] = (float)(fl[i] + add[i]);
        if (fl[i] < 0) fl[i] = 0;               // 0 > fl
        if (fl[i] > 63.0f) fl[i] = 63.0f;
        current[i] = (int)fl[i];               // truncate (0x40ba78)
    }
    if (![0x422988]) write all 256 entries of current to the DAC (0x3c8 = 0, 0x3c9 x 768);
}

void white_flash(void)    { flash_add(63, 15); }                       // 0x400498
void flash_add(int a, int n)                                            // 0x4003ec
{ for (i) current[i] = clamp(base[i] + a, 0, 63); fade(1.0f, n); }
void inverse_flash(void)  { for (i) current[i] = (63 - base[i]) & 63; fade(1.0f, 20); }   // 0x400454
void inverse_n(int n)     { for (i) current[i] = (63 - base[i]) & 63; fade(1.0f, n); }    // 0x4003a8, unused
void fade_to_black(void)  { fade(0.0f, 40); }                          // 0x400438
void fade_100(void)       { fade(100.0f, 0); }                         // 0x400444, unused
```

- The DAC is only written by a step, so a flash never shows its start value: the first visible value is
  63 + (base − 63) / 15 (white flash) or (63 − base) + (2 base − 63) / 20 (inverse), at the first retrace callback
  after the call; the fade ends after n steps at trunc of the accumulated float32, which is often just below
  base × level: after a white flash the values 4, 7, 8, 11, 12, 15, 16, 19, 22, 23, 26, 27, 30, 31, 34, 37, 38, 41, 42,
  45, 46, 49, 52, 53, 56, 57, 60, 61 (28 of 64) end one lower, after an inverse flash 25 of 64 (2, 5, 7, 10, …, 62),
  and stay so until the next palette change.
- Who calls them: only script routines (table 1.4), the intro init (0x400364(intro/intro.pal) then 0x4004ac(1.0,
  120): fade in from black over the first 120 refreshes) and the flight start (0x400364 then 0x4004ac(1.0, 60)). No
  part writes the palette arrays or the DAC itself.
- Palettes at run time are the files in INSIDE.DAT as the program holds them: nigga/nigga2.pal, nigga4.pal,
  nigga9.pal have entry 0 set to black by the sprite inits (0x40506c, 0x4051d4, 0x405110); every twirl event sets entry
  0 of the palette it loads to black (nigga2, nigga4, jeesus, map.pal: the later ones are already modified).
- For the remake: evaluate a fade continuously as base × level + (start − base × level) × (1 − s / n), s = steps
  done (stepped once per refresh), clamped to 0..63, colour = value / 63, ending exactly at base × level. The
  truncation to 6 bits and the one-lower ends (float32 drift) are low-resolution artefacts; the step timing (one per
  refresh, starting at the first retrace callback after the call) is not.

## 7. Screen 0x40a83c, 0x400b14, 0x400694

- 0x40a83c(EAX width, EDX height, EBX x0, ECX y0, [esp+4] x1, [esp+8] y1) (`ret 8`): pitch [0x4b6ec4] = 320, height
  [0x4b6eac] = 200, clip window x0 [0x4b6eb8], y0 [0x4b6ebc], x1 [0x4b6eb0], y1 [0x4b6eb4] (exclusive). Called once
  by init with (320, 200, 0, 0, 320, 200); nothing changes the window later (the engine's bitmap routines 0x415e50..
  only read it), so every copy is the full 320 × 200.
- 0x400b14 flip: swap [0x4248cc] / [0x4248d0]; draw target [0x4b6ec0] = the new [0x4248d0] (the buffer that was
  waiting to be shown or was shown last: its content is the frame flipped two flips earlier); frame_ready
  [0x422984] = 1. It never waits: a second flip before the copy replaces the waiting frame. The sprite frame routine
  (0x4047a0, after its flip at 0x404fc8) and the flower frame routine (0x4095f8, at 0x409716) wait until
  [0x422984] = 0 (copied); the others draw as fast as they can.
- 0x400694(EAX buffer) copy: rows y0..y1−1, bytes x0..min(x1, pitch)−1 of each, to [0x42297c] = 0xa0000 (memcpy
  0x40b784 per row). Called only by the retrace callback, for [0x4248cc].
- 0x400bec: debug overlay ("%02d:%02d" position, "%05d kB" memory), off; every frame routine calls it before its
  flip.

## 8. INSIDE.DAT 0x4000e8, 0x4001c4, 0x40022c, 0x400614

- 0x4000e8(name, &size): fopen "rb", size = file length; malloc(size + 0x28); read dword U (unpacked size) and the
  rest (size − 4 bytes); 0x28 bytes 0xff after the end; allocate "whole data buffer" U + 0x28 bytes (zero-filled);
  0x4011c0 / 0x4011d6 LZSS (as `datfile.py`: a flag byte, bit 0 first, 1 = literal, 0 = two bytes b0 b1: distance
  (b1 & 0xf0) << 4 | b0, length (b1 & 15) + 3, copied byte by byte; the end test is after each literal or whole match,
  so up to 17 bytes past U can be written: the 0x28 spare bytes); free the packed data. The file is not closed.
- 0x4001c4: block → [0x424894]; N = dword at block + U − 4 → [0x42489c] (93); directory = block + U − 4 − 0x30 N →
  [0x4248b0].
- 0x40022c(name): [0x4248c0]++; copy the name, lower-case it (0x40b953), '\' → '/'; the first directory entry whose
  name (0x24 bytes) strcmp-equals it (0x40b980); not found: "cannot find file %s from inside.dat" and exit;
  [0x4248c0]−−; return block + entry[+0x24].
- 0x400614(name) (every part): [0x422780] ? 0x40022c(name) : load the loose file (0x40baa0; debug: log to
  inside.lst).

## 9. Memory and random numbers

- 0x40b10c: Watcom's malloc (sizes rounded up to 4, minimum 12; grows through DPMI 0501h).
- 0x40b4d8(EAX size, EDX name): takes a node (24 bytes: +0 next, +4 name copy, +8 pointer, +0xc size, +0x10 tag,
  +0x14 references = 1) from the free list [0x4b6ee8], pushes it on the used list [0x4b6ecc], malloc(size),
  **zero-fills it** ([0x4b6ed8] = 1), tag = the current tag [0x4b6ec8]; counts blocks [0x4b6ed0] and bytes
  [0x4b6ee4]. size ≤ 0: no memory, "trying to allocate null block" (debug print). Returns the pointer.
- 0x40b3c0: new tag (++[0x4229ac]); 0x40b4d0(tag): current tag. Each part init takes a tag; its end routine frees
  everything with it.
- 0x40b5c4(prev): unlink the node after prev (0: the head), references −1, at 0 free memory and name, node back to
  the free list. 0x40b690(ptr): the same for the node holding ptr. 0x40b6fc(tag): every node with that tag.
- 0x40b784(EAX dst, EDX src, EBX n): memcpy (rep movsd, rep movsb). 0x40b7a4 → 0x40ad40(EAX dst, EDX byte, EBX n):
  memset.
- **rand 0x4134cd**: seed (dword at 0x423818, initially 1; 0x4134c7 returns its address) = seed × 0x41c64e6d +
  0x3039; returns (seed >> 16) & 0x7fff. **srand 0x4134f1**: seed = EAX; called once, srand(0x45eafc) in the flight
  init 0x402948 (an address used as a constant).
- The one generator is shared by everything, in this order in the capture:
  intro init 0x403770: 260 calls from seed 1; credits init 0x40684c: 13,312 (from 0x8e60e23d); flight init: srand
  (0x45eafc), 0x4018d0: 5, 0x402948: 4; temple init 0x4039c8: 30 (from 0x09c4603d); water init 0x4079f4: 24 (from
  0xef012bc7) → 0xf83d63af at the end of init. Then at run time: the intro frame routine (0x40337c, a variable
  number per frame: 23..294) and the intro tick routine 0x403214 (0..2 per refresh) interleaved, entry 1 (260): in
  all 51,348 calls during the intro of the capture → 0xde09b53b; the flower start 0x4098c4: 732 → 0x95a8f93f; then
  the flight frame routine (0x402638, from 0x4028e4) and the water frame routine (0x4078d4, 0x407400) per frame.
  So the init sequences replay exactly from these seeds; the intro's, the flight's and the water's run-time
  sequences depend on the frame rate (how many frames were drawn between tick routine calls) and cannot be
  replayed exactly; entry 1 and the flower start get frame-rate dependent seeds (the capture's are above).

## 10. FPU

Watcom's start-up (0x41b790 → 0x41d4b8: fninit, 387 check, fldcw) loads the control word from 0x423bb0 = **0x127F:
round to nearest, 53-bit (double) precision**, all exceptions masked. Every main-line routine (frame routines, part
inits, script routines, 0x4004ac) computes with that: + − × ÷ and sqrt give exactly the IEEE double results, so a
Python model in doubles reproduces them (fsin / fcos and friends aside). Float → int conversions call 0x40ba78
(control word with RC = truncate, frndint) and then `fistp`: truncation toward zero. Inside the timer interrupt the
handler's `fnsave` leaves the FPU in its reset state, **control word 0x037F (64-bit precision)**: the tick
routines, the palette steps and the music run in extended precision (the hooked run shows 0x127f at every frame
routine start and 0x37f at every callback). The palette model gives the same DACs with either precision.

## 11. Corrections to NOTES.md, BRIEF.md and run_inside.py

- BRIEF / NOTES "the FPU otherwise rounds to nearest at 64-bit precision": main-line code runs at 53-bit precision
  (0x127F); only the interrupt (tick routines, palette steps) at 64-bit (section 10).
- The frame clock [0x424880] and [0x42488c] are read by nothing but the callback; the parts read M = [0x4248c4]
  (temple, sprites, water: the music clock, never reset after init) or P = [0x4248c8] (intro, flight), section 1.5.
  NOTES does not mention [0x4248c4].
- NOTES "it never waits": the sprite and flower frame routines wait for the copy after their flip (section 7).
- NOTES "a part that narrows the window (letterbox)": no part changes the clip window; every copy is 320 × 200.
- NOTES callback description: add the [0x42278c] guard (whole callback skipped, credits), the [0x4248c0] busy
  counter (retrace work skipped), that the BIOS chain happens only on non-retrace interrupts, and that 0x400fcc also
  calls the driver's poll (0x40faaf) before AMP's.
- NOTES "the seed lives behind 0x4134c7": at 0x423818, initially 1; srand once (0x45eafc).
- NOTES "each call runs at most one script entry": true, but the main loop calls it until nothing is due, so all due
  entries run before the next frame.
- BRIEF's flower list includes 0x408014: it ends the water part (frees the water's tag [0x4265f0]).
- Palette: the flash start value is never shown (section 6); 0x400364 with no fade running takes one step at the
  next retrace.
- `run_inside.py`: `--load` leaves `next_frame = FRAME`, so a resumed run writes records without emulating anything
  (time stands still); it needs `next_frame = FRAME * (st['frame'] + 1)` after loading the snapshot (the copy in
  `models/core/run_inside.py` has that, and its output equals `full.ticks`). Its docstring says record k is the
  picture from time k / 70.086 s: it is written at (k + 1) / 70.086 s (the `.ticks` time column), after the retrace
  of refresh k; and "[0x424880] (float, 280 Hz)" is 70 Hz (one per retrace callback).

## 12. Not determined

- Entry 1 (3:00) ran at M 4250..4254: its first effect is at record 1101 or 1102 (the intro scope can tell from the
  drops in the frames).
- Which of the three non-retrace interrupts between records 3044 and 3045 was lost (it only matters for a GUS
  replay; no player tick fell there, the tick model fits with either).
- The reason the rows of order 36 take 2,304 units (speed 12 or a tempo change): the music scope.
- The real-hardware latency of frames (it depends on the CPU and the VGA copy speed); on a slow machine a long
  retrace callback lets the next timer interrupts nest (they keep M, P and the music running, the frame part is not
  reentered).
