# Scope "p4": FIRE clouds over p3's picture, FRACTAL scape, hand-over to p5

p4 does not use the 3D engine. It is one main routine (1caa) that runs seven steps, each a busy loop "draw a frame,
test ESC, compare the retrace counter", around three things: a fire feedback buffer seen through a polar map
(1aab), a voxel landscape (1efc) and a palette cross-fade (1999) that mixes a 16-colour background with 15 fire
levels.

Addresses are `SEG:OFF` in `unpacked/p4.bin` (file offset = SEG*16 + OFF; code and variables in segment 0010).
In the run, p4 is loaded at segment 0F64: image offset X is at linear 0xF640 + X, code runs at CS = 0F74, and an
image segment S is S + 0F64 (the relocated words in the image hold S). "IRQ k" = the k-th call of the per-retrace
routine since the timer was installed (k = 1 in frame 13651); IRQ k falls in capture frame 13650 + k for the whole
part. Colours are 6-bit VGA values (0..63). Pixel (x, y) covers [x, x+1) x [y, y+1) of the 320x200 frame.

Verified (models in `models/p4/`, section 11):

| what | how | result |
|---|---|---|
| tables built at run time: sine 1ea8, cloud scaling 1b36, blend table 1b56 | built from the image only, compared with the run's memory after set-up | equal |
| fire feedback 1aab | stepped from a zero buffer with the formula offsets, CRC32 of the 32000-byte buffer after each of the 3715 recomputes of the part (hooked run) | 3715 / 3715 equal; offset formula equal at all 34006 frame-loop calls |
| state from the IRQ count (phase, position, fade, label, landscape position) | `timeline.py` against all 34006 logged calls | equal (4 calls fall between a step's write and the next IRQ, see 3) |
| landscape 1efc | 24 buffers 4f1e dumped after 1efc | 13 equal with one [2113] (12 with the value read after 1efc, 1 with the value before); the other 11 equal when the rows before row index k use [2113] − 2 (an IRQ came while the frame was drawn) |
| whole frames: composite 1b91, dither, label, palette 1999 | 77 capture frames (every 97th of the part) against `full.frames` | DAC 0..255 equal in all 77; pixels equal in 64; the 13 others (landscape stage) are torn: every row matches one of the last loop iterations' composites except one row split inside (`check_torn.py`) |

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

The step thresholds are retrace counts in the main routine. All motion runs in the per-retrace routine; nothing
reads the music. Times are capture times of the frame in which IRQ k falls.

| step | IRQs k | frames | time (s) | music | on screen |
|---|---|---|---|---|---|
| set-up | — | 13638–13650 | 194.59–194.76 | 2a:26–2a:28 | p3's last screen (the nebula) stays: CDPI loads p4, the RNC stub unpacks it (13638–13646), 1caa builds its tables, draws one frame (= the nebula unchanged), calibrates the timer (2 retraces) |
| 1 | 1–1400 | 13651–15050 | 194.77–214.73 | 2a:28–2f:2d | fire spirals fade in over the nebula: fade A = k (A/4096 = weight of the fire); swirl phase step 35 |
| 2 | 1401–2886 | 15051–16536 | 214.75–235.94 | 2f:2e–35:06 | label "larusse FIRE clouds" slides in at the bottom right (k 1401–1476); A → 2886; phase step 31 |
| 3 | 2887–4166 | 16537–17816 | 235.95–254.20 | 35:06–39:30 | label slides out (k 2887–2962); A reaches 4096 at k = 4096 (frame 17746, 253.20 s): the nebula is gone, fire on black; phase step 29 |
| 4 | 4167–5216 | 17817–18866 | 254.22–269.18 | 39:30–3d:23 | "FRACTAL scape": a grey voxel landscape replaces the nebula and fades in under the fire (A −2 per IRQ, stops at 1534 at k = 5447, 272.5 s); phase step 27 |
| 5 | 5217–5916 | 18867–19566 | 269.20–279.17 | 3d:24–40:06 | label "larusse FRACTAL scape" slides in (k 5217–5292); phase step 25 |
| 6 | 5917–6406 | 19567–20056 | 279.18–286.16 | 40:06–41:38 | label slides out (k 5917–5992); A +5 per IRQ: 1534 → 3984 (the landscape fades out); phase step 23 |
| 7 | 6407–7430 | 20057–21080 | 286.18–300.77 | 41:38–45:26 | the chequerboard (p5's background) replaces the landscape; A jumps to 4096 and falls 4 per IRQ to 0: the chequerboard fades in, the fire fades out |
| exit | — | 21081 | 300.79 | 45:26 | the plain chequerboard is copied to the screen, the timer restored; p5 starts at frame 21082 |

Length from the timer start: 7430 retraces = 106.0 s (frames 13651–21080).

## 2. Data

All "read from the image" items must be read by the remake from `unpacked/p4.bin` at these offsets.

| what | where (file offset) | format | origin / use |
|---|---|---|---|
| sine table | 0010:0000 (0x100), 512 words | int16 quarter wave; 1ea8 mirrors it into 0400..07ff, negates 0000..07ff into 0800..0fff and copies the quarter to 1000..13ff: word i (0..2047) = sin, period 2048 words | entry i = round(32767 sin(2πi/2048)) for i ≤ 511 except entry 510 = 32767 (formula 32766). The mirror maps word 512 + j to q[511 − j] = the sine of entry 513 + j, so the second and fourth quarters run one entry ahead. Evaluate `sin` instead |
| fire palette | 0010:15ae (0x16ae), 15 × 3 bytes | 6-bit RGB: (0,0,0) (14,3,3) (29,8,7) (39,15,10) (49,23,14) (59,33,19) (60,40,25) (61,49,32) (62,56,39) (62,57,44) (62,59,50) (63,61,56) (63,62,59) (63,63,63) (63,63,63) | colour of fire level 0..14 (black → red → orange → yellow → white) |
| landscape palette | 0010:20d2 (0x21d2), 16 × 3 | greys 0,3,7,12,16,20,24,29,33,37,41,46,50,54,58,63 | replaces the 16 background colours in step 4 |
| depth table | 0010:2058 (0x2158), 61 words (index 1..55 used) | 0..40 step 1, 42..60 step 2, 64..96 step 4 (index 55 = 80) | depth rows of the landscape |
| landscape u position | 0010:2111 word 0100h | constant (never written) | camera's lateral position: 0100h / 16 = 16 texels |
| label 1 | RIX3 at 0273:0000 (0x2730) | "RIX3", u16 width 70, u16 height 44, byte AFh, byte 0, 768-byte 6-bit palette, 70 × 44 pixels (at +30Ah) | hand-drawn "larusse FIRE clouds", greys 240..254; palette 240..255 at +2DAh: greys (17 i) >> 2 = 0, 4, 8, 12, 17, …, 63 |
| label 2 | RIX3 at 036e:0000 (0x36e0) | the same format, the same palette | hand-drawn "larusse FRACTAL scape" |
| swirl map | 07d9:0000 (0x7d90), 32000 words | word = v·256 + u; fire pixel p (= y·160 + x) uses word 31999 − p | generated polar map (section 4.2) |
| chequerboard | IFF "FORM" … "PBM " at 177a:0006 (0x177a6), FORM length 6E24h, ends 0x1e5d1 | BMHD 320 × 200, CMAP 256 × 8-bit RGB (p4 keeps entries 0..15, >> 2), BODY ByteRun1 | hand-drawn red / orange marble chequerboard, pixels 0..14; p5's background |
| (unused) | 0x1e910..0x1ec0f | a 256-entry grey palette | leftover header of the cloud picture; not referenced |
| cloud texture | 1ec1:0000 (0x1ec10), 65536 bytes | 256 × 256, values 0..127 | the fire's source: bright streaks and swooshes on black. Rendered or processed image, not pixel art (guess) |
| height map | 2eec:0000 (0x2eec0), 65536 bytes | 256 × 256, values 0..118, tiles seamlessly | terrain: smooth hills with drawn-in river beds and a square plateau with a ramp; made in a tool and touched up by hand (guess) |
| colour map | 3eec:0000 (0x3eec0), 65536 bytes | 256 × 256, values 1..12 (indices into the landscape palette) | pre-shaded terrain colours: correlates with the height slope (+0.53 with h[x+1] − h[x], −0.61 with h[x−1] − h[x]: lit from −u) plus texture streaks |
| nebula | RIX3 at 4eed:0006 (0x4eed6), palette at 4eed:0010, pixels at 4f1e:0000 (0x4f1e0) | 320 × 200, 16 colours | hand-drawn violet nebula with stars: p3's background. The same bytes are in p3 (RIX3 at p3 file offset 0x4b806). p4 never shows this copy: 1caa overwrites 4f1e with VGA memory, which holds p3's last screen; that screen equals this picture exactly, and DAC 0..15 at p4's start equals its palette 0..15 |

Buffers (contents generated at run time): 0469:0000 strip 320 × 44 (rows 148..191 with the label), 4f1e:0000 the
background 320 × 200 (values 0..15), 5ecd:0000 the blend table (128 rows of 256 bytes), 66cd:0000 the fire buffer
160 × 200, 0010:157e the 16 background colours, 0010:15e9 / 1629 two 64-byte scale tables, 0010:1669 the 240-colour
palette output, 0010:2121 / 23a1 per-column buffers of the landscape (y, and a colour that is written and never
read).

## 3. Main routine 1caa and the per-retrace routine 1a20

```c
// 1caa
read DAC 0..15 into pal16 (0010:157e)                  // p3's colours
copy A000:0000..F9FF to 4f1e:0000                      // p3's last screen = the nebula (BG)
DAC 240..255 = label1.palette[240..255]                 // [196b] = 0273: RIX3 + 2DAh; never reloaded for label 2
build_sine();                                           // 1ea8
cloud[i] = min((cloud[i] * 7) >> 2, 127)  for all 65536 // 1b36, in place at 1ec1
blend_si = 0x18; build_blend();                         // [1b34], 1b56
fire[0..31999] = 0;                                     // 66cd
A = 0; Astep = 1;                                       // [1971], [1973]
frame();                                                // 1b91: offset 0 = the immediate in the image, so no
                                                        // fire step; draws the nebula unchanged
install_timer();                                        // 1469 (count = 2P*1000/2048, core scope)
loop { frame(); } until ESC or cnt >= 1400;             // cnt = [1969]
ldir = 1; fstep = 0x1f;                                 // [196f], [1576]
loop { frame(); } until cnt >= 2886;                    // cnt not reset
ldir = -1; fstep = 0x1d; cnt = 0;
loop { frame(); } until cnt >= 1280;
Astep = -2; Alow = 0x600; cnt = 0; fstep = 0x1b;        // Alow is the immediate at 1a59 (self-modified)
pal16 = landscape palette (0010:20d2);
loop { clear BG to 2 (1ee8); landscape (1efc); frame(); } until cnt >= 1050;
fstep = 0x19; label = label2 ([196b] = 036e); ldir = 1;
loop { 1ee8; 1efc; frame(); } until cnt >= 1750;        // cnt not reset
fstep = 0x17; ldir = -1; Astep = 5; Alow = 0; Ahigh = 0x1000; cnt = 0;   // Ahigh: the immediate at 1a62
loop { 1ee8; 1efc; frame(); } until cnt >= 490;
Astep = -4; cnt = 0; A = 0x1000;
load_pbm(177a:0000 -> palette 4eed:0010, pixels 4eed:0310 = 4f1e:0000);   // 14f3: the chequerboard into BG
pal16 = 4eed:0010..003f (its colours 0..15);
loop { frame(); } until cnt >= 1024;
copy 4f1e:0000 to A000 (64000 bytes); restore_timer(); exit(0);          // 14da
// ESC: port 60h & 7Fh == 1 after every frame -> restore_timer(); text mode; exit(0xFF)
```

The steps change right after the threshold IRQ (all checked: a step's writes come before the next IRQ, so the
first IRQ of the new step already uses them). Framework routines (core scope): 1410 wait for the start of a
retrace, 1420 int 8 handler (EOI, `inc [141e]`, wait for the retrace, PIT one-shot with [1404], `call far 1a20`,
chain to the old vector [1400] when the accumulator [1406] overflows), 1469 install + 1497 calibration (count =
−latch · 1000 >> 11), 14da restore (vector, then 1486: PIT mode 3 count 0 between two retrace waits).

```c
// 1a20, every retrace (IRQ k)
P += 2;                                   // [2113] landscape position, from the timer start: P = 2k
cnt += 1;                                 // [1969]
d += ldir;                                // [196d] label slide, starts at -1
if (d < 0 || (uint16)d >= 75) ldir = 0;   // stops at -1 or 75
A += Astep;                               // [1971] fade (signed)
if (A < Alow || (uint16)A >= Ahigh) Astep = 0;   // A is not clamped: it stops one step past the bound
if (k & 1) {                              // [141e] odd: half the retraces
    pos -= 0x10000;                       // [157a] dword
    phase += fstep;                       // [1578]
    palette();                            // 1975 x 2 + 1999: DAC 0..239 from pal16, A
}
```

So, after IRQ k (m = (k + 1) / 2 odd IRQs):

- `pos = −65536·m`; `phase = Σ fstep` over the odd IRQs j ≤ k with fstep 35 for j ≤ 1400, 31 for 1401..2886, 29
  for 2887..4166, 27 for 4167..5216, 25 for 5217..5916, 23 after.
- `A`: k (k ≤ 4096), 4096 (until 4166), 4096 − 2(k − 4166) down to 1534 (stops at k = 5447: 1536 is not below the
  bound 0x600, 1534 is), 1534 + 5(k − 5916) for 5917..6406 (3984 at 6406), then 4096 − 4(k − 6406) (0 at 7430).
- label width `d`: k − 1401 for 1401..1476 (max 75), 75 − (k − 2886) down to −1 (k = 2962); the same for label 2
  from 5217 and 5917.
- `P = 2k` (mod 65536; at most 14860 here).

## 4. FIRE clouds

### 4.1 The fire step — 1aab (called by 1b91 every loop iteration)

```c
off = (int32)(SINE[(phase & 0xffe) / 2] + pos) >> 8;   // SINE = words at 0010:0000; pos multiple of 65536:
off &= 0xffff;                                          //   off = (-256*m + floor(SINE/256)) & 0xffff
if (off == imm_1b0b) return;                            // immediate of `add bx, 0` at 1b09 (self-modified)
imm_1b0b = imm_1b20 = off;                              // patches both `add bx, imm16` (1b09, 1b1e)
for (p = 0; p < 32000; p++) {                           // the loop runs ecx = 3E7Fh..0 and di = 0..7CFEh
    a = (MAP[31999 - p] + off) & 0xffff;                // MAP = 07d9:0000 words: v*256 + u
    F[p] = BLEND[F[p]][CLOUD[a]];                       // F = 66cd (160 x 200), CLOUD = 1ec1 (scaled)
}
// BLEND (1b56, si = 18h): BLEND[r][c] = min((24*c + 232*r + 0x3f) >> 8, 0x70), r, c = 0..127
```

- So per pixel `F ← min(floor((232·F + 24·C + 63) / 256), 112)`: an exponential moving average of the texture seen
  at that pixel, weight 24/256 = 9.4 % per step, memory 1/(1 − 232/256) ≈ 10.7 steps (0.3 s).
- `CLOUD = min(⌊7·raw/4⌋, 127)`, raw 0..127: the texture is brightened 1.75× and clips.
- The offset changes at every odd IRQ (the `pos` term alone moves it by −256), so the buffer steps exactly once per
  odd IRQ as long as a loop iteration takes less than two retraces. In the hooked run it did all 3715 steps (35.04
  per second); the loop ran 6 times per retrace over the nebula and the chequerboard, 1.3 times per retrace over
  the landscape. On a slower PC two updates could fall into one iteration and the fire would get fewer steps.
- **Time base**: steps at odd IRQs (35.04 Hz), in the frame loop. The state is a pure per-pixel recursion; there
  is no spatial coupling between pixels.
- The integer recursion has a dead zone: F stops rising about 8 below the texture value and stops falling about
  2.6 above it (floor((24C + 232F + 63)/256) = F for F in (C − 8.04, C + 2.63]). Over the part (`fire_float.py`):
  integer mean F = 12.4; float `min((24C + 232F)/256, 112)` = 14.0; float `min((24C + 232F + 63)/256 − 0.5, 112)`
  (≥ 0) = 11.7, mean |difference| 1.0 of 112. The last one is the closest float form.

### 4.2 The swirl map (07d9)

Generated before release, stored in the image (no code builds it). Polar coordinates around the screen centre,
with the vertical axis weighted 1.4 (in 320 × 200 pixels). In fire pixels (fx = 0..159, fy = 0..199, a fire pixel
is two screen pixels wide):

```
X = fx - 79.5,  Y = 0.7 * (fy - 99.5)
v = floor(1.254 + 2.416 * sqrt(X^2 + Y^2))         // radius, max 255 (8 entries)
u = round(128/pi * atan2(Y, -X)) & 255             // angle: left 0, bottom 64, right 128, top 192
```

v is exact for 89 % of the entries and within ±1 for all; u is exact for 77 %, within ±1 for 98.7 %; 152 entries
next to the centre lines (rows 86..113, columns 73..86) are off by up to 25 (the generator wrote the same value
along the axes). In screen pixels, with (Xs, Ys) the distance of the pixel centre from (160, 100):
`v ≈ 1.254 + 1.208·√(Xs² + (1.4·Ys)²)`, `u = 128/π·atan2(1.4·Ys, −Xs)`. On a 4:3 display one row is 1.2 pixel
widths high, so the rings are ellipses 1.17 times as wide as tall.

With the offset: the texture byte read is at linear address `256·(v − m) + u + s` (mod 65536), s = ⌊SINE/256⌋ in
−128..127. So the texture moves outward one texel row per step (35 rows per second; about 29 screen pixels per
second horizontally) and turns by s/256 of a turn (up to ±1/2) with the sine. The swing period is 4096 / fstep steps: 3.34 s at
35, 5.08 s at 23. When u + s leaves 0..255 the read wraps into the neighbouring texture row (a 1-row shift along one
ray, invisible after the feedback). The texture wraps after 256 columns and 256 rows; it is nearly black along
its edges (mean 0.2..3.3 of 127), so the wraps show no seam.

### 4.3 The frame — 1b91 / 1c6b (every loop iteration)

```c
fire_step();                                              // 1aab
for (y = 0; y < 200; y++)                                 // rows 0..147 and 192..199 straight to A000,
  for (x = 0; x < 320; x++) {                             // rows 148..191 into the strip 0469 first
    f = F[y*160 + x/2];                                   // each fire value covers 2 pixels
    s = ((2*f + D[y & 3][x & 3]) & 0xF0) + BG[y*320 + x]; // BG 0..15; 2f + D <= 231: no carry between bytes
  }
// D = bytes of edx = 01040007h, rotated left 8 bits per row:
//   D = {7,0,4,1}, {1,7,0,4}, {4,1,7,0}, {0,4,1,7}   (row 0..3; the strip rows continue the same rotation)
// label (strip only): if (d >= 0) copy label columns 0..min(d,70)-1 to x = 319-d.., rows 148..191 (opaque)
// copy strip to A000 rows 148..191
```

The screen index is 16·L + c: fire level L = ⌊(2F + D)/16⌋ = 0..14 and background colour c = 0..15. There is no
wait for the retrace: VGA memory is rewritten continuously and a capture can show two iterations (tearing).

Label position: the left edge is at x = 319 − d (d = 0..75) and the visible width is min(d, 70): the label enters
from the right edge over 70 retraces, moves 5 more pixels left (fully visible at x 244..313, 6 fire columns to
its right), and leaves the same way. The width cap applies only to the copy, not to the position.

### 4.4 The palette — 1975 / 1999 (odd IRQs)

```c
// 1975(bx = table, dx = weight): dx < 0 -> 0; dx >>= 4; dx = min(dx, 256); table[i] = (i * dx) >> 8, i = 0..63
Sb = 1975(0x1000 - A);  Sf = 1975(A);                    // 15e9, 1629
for (L = 0; L < 15; L++) for (c = 0; c < 16; c++) for (ch = 0; ch < 3; ch++)
    DAC[16*L + c][ch] = min(Sb[pal16[c][ch]] + Sf[FIREPAL[L][ch]], 63);
// DAC 240..255: the label greys, set once
```

That is a cross-fade per colour: `colour = (1 − a)·background + a·fire(L)` with a = A/4096 (quantised to 1/256,
with both weights floored separately, so for A = 1..15 the background weight is 255/256 and the fire weight 0).
Fire level 0 is black: where there is no fire the background is darkened by (1 − a). At A = 4096 the background
is invisible.

## 5. FRACTAL scape — 1ee8, 1efc (every loop iteration in steps 4–6)

```c
// 1ee8: BG[0..63999] = 2                                 (the sky: landscape grey 2 = 7/63)
// 1efc, P = [2113] read when each depth row starts (an IRQ may change it inside a frame)
for (i = 55; i >= 1; i--) {                               // [2117] = 6Eh .. 2 step 2, far to near
    z = 16*DIST[i] + 15 - (P & 15);                        // DIST = 0010:2058
    base  = (i == 1) ? 0x6400 : 0x48 + 0x8000 / z;         // horizon row 72
    scale = (i == 1) ? 0      : 0xAAAA / z;                // 43690 / z
    step  = z << 5;                                        // u per column, 8.16 fixed point
    U = (0x100 << 12) - (step << 7);                       // [2111] = 100h: u = 16 - z/16
    vrow = ((P >> 3) + (z >> 5)) & 255;                    // texel row (P<<5 >> 8, then + z>>5, byte)
    for (col = 0; col < 319; col++) {                      // column 319 is never drawn (stays sky)
        U += step;                                          // u byte wraps without carry into the row
        bx = vrow*256 + ((U >> 16) & 255);
        frac = (U & 0xffff) >> 1;                           // 15 bits
        h = (HM[bx] << 8) + ((HM[bx+1] - HM[bx]) * frac >> 7);  // 8.8, linear along u only; bx+1 is the
                                                                //   next linear byte (row+1 at u = 255)
        y = base - (h * scale >> 16);
        y = (y < 0) ? -1 : min(y, 199);
        if (y > ybuf[col])                                  // nearer row lower on screen than the last one
            for (r = y; r > ybuf[col]; r--) BG[r*320 + col] = CM[bx];   // colour of this row, nearest texel
        ybuf[col] = y;                                      // also when nothing is drawn
    }
}
// ybuf starts at 6400h (image), and is 199 after every frame (row 1 forces y = 199)
```

Equivalent continuous camera (320 × 200 frame, pixel centres at +0.5; d = z / 32 texels of depth):

```
X = 127.5 + 64 * (u - 16) / d                     // the direction of travel projects to x = 127.5, not 160
Y = 72 + 0.5 + (16/3) * (192 - H) / d             // horizon row 72, camera 192 height units up;
                                                  //   one texel = 12 height units (64 / (16/3))
```

(43690/8192 = 5.33325 ≈ 16/3; 1024 / 5.33325 = 192.0. The +0.5: a span fills rows up to the farther row's y + 1.)

- Depth rows: d = 0.5..20 step 0.5 (DIST 1..40), 21..30 step 1, 32..40 step 2, plus the sub-step (15 − P&15)/32.
  The terrain ends at d ≈ 40.5 texels; beyond it the sky shows (flat ground H = 0 ends at row ≈ 98).
- Motion: only P. Heights and colours are read at texel row ⌊P/8⌋ + ⌊z/32⌋, while the rows' screen depth only
  slides by the sub-step (15/32 texel per 16 P). Seen by a viewer: the camera flies straight along +v at P/8
  texels = 0.25 texel per retrace (17.5 texels per second), u fixed at 16, height fixed, no turning; the terrain
  advances in 1-texel jumps every 4 retraces with a small back-and-forth of the row lines (a sawtooth of 0.47
  texel per 8 retraces).
- The map wraps in u and v (it tiles seamlessly). At d = 40 the view spans u = −63..136.
- **Time base**: P = 2k (retraces); drawn per loop iteration (in the capture about 1.3 iterations per retrace, so
  every retrace shows a new landscape). An IRQ inside 1efc changes P between depth rows of one frame (seen in 11
  of the 24 dumped frames).

## 6. Labels

Both 70 × 44, RIX3 (section 2), pixels 240..254, opaque rectangle (no transparency; colour 240 = black border).
Shown in the strip at rows 148..191, x = 319 − d (4.3). Label 2 uses label 1's palette (identical). Time base:
retraces (d ±1 per IRQ).

## 7. Hand-overs

- **From p3**: p4 takes p3's last screen and DAC 0..15 as its background. Both equal the nebula RIX3 that p4
  carries (section 2), so the remake can start from the nebula picture and its palette. At IRQ 1 (A = 1) the
  background weight becomes 255/256, which lowers every non-zero colour component by one 6-bit step: a
  quantisation step, not intended.
- **To p5**: 14f3 loads the IFF PBM: search "CMAP" word by word from 177a:0000 (at most 1388h words), copy 768
  bytes after the chunk header, each >> 2, to 4eed:0010; search "BODY"; ByteRun1 into 4eed:0310 (= 4f1e:0000)
  until 64000 bytes (n ≥ 0: copy n+1 bytes; n < 0: repeat the next byte 1−n times; n = −128 would repeat 65409 times,
  the file has none; a Python decoder gives the emulator's bytes and palette). The
  step's A falls from 4096 to 0, but the palette is written only on odd IRQs: the last write is at IRQ 7429 with
  A = 4, which floors the background weight to 255/256. **p4's last frames, and p5's first frames (which keep this
  DAC), show the chequerboard with every non-zero colour component one 6-bit step darker than its palette**
  (capture: DAC 0..2 = (2,0,0) (6,0,0) (10,0,0) against the picture's (3,0,0) (7,0,0) (11,0,0)). The screen
  indices are 16·L + c until the final copy replaces them with the plain picture (frame 21081); with the fire
  weight 0 all levels show the same colour, so nothing changes visibly.

## 8. Time bases (summary)

| variable | time base |
|---|---|
| step thresholds, A fade, label slide d, landscape position P | retraces: every IRQ |
| swirl phase, swirl outward motion (pos), palette writes | odd IRQs (35.04 Hz) |
| fire feedback step | the frame loop, once per change of the offset = once per odd IRQ on a fast enough PC (all 3715 in the capture) |
| landscape drawing | per loop iteration with the current P |
| music | not read |
| random numbers | none |

## 9. For a high-resolution remake

Evaluate exactly instead of copying:

- **Fire**: per output pixel, at any resolution: the polar map of 4.2 as a formula (drop the axis glitches and the
  ±1 rounding), the texture read filtered (the address is 256·row + column; the remake can read
  (u + s) mod 256 on row v − m, ignoring the 1-row carry), F stepped once per odd IRQ with the float recursion
  `F = min((24·C + 232·F + 63)/256 − 0.5, 112)` (closest to the integer one), or exactly as `F_m = Σ_j w_j C_{m−j}`
  without a state buffer (weights (24/256)(232/256)^j; the terms after 100 hold 5·10⁻⁵ of the weight, after 150
  4·10⁻⁷; ignores the cap at 112, which only matters where C > 112). Seeking: replay from the part start or from
  150 steps back. Between steps the original holds F; the remake can blend F_m and F_{m+1} by the fraction.
- **Fire level** instead of the 4 × 4 dither (thresholds 0, 1, 4, 7 of 16): the dither's mean is
  L = 2F/16 + (3 − 7.5)/16 = F/8 − 0.28; use `L = max(0, F/8 − 0.28)` and interpolate the 15-colour fire palette
  (clamp at 14). The fire is 160 wide in the original (each value doubled): compute it per pixel.
- **Palette cross-fade** in true colour: `rgb = (1 − a)·background_rgb + a·fire_rgb(L)`, a = A/4096 from the IRQ
  count with the fraction of a retrace (the original writes it at 35 Hz and floors both weights to 1/256).
- **Landscape**: ray-march the height map with the camera of section 5 (continuous d, bilinear heights, filtered
  colours), v_cam = P/8 texels continuous (drop the 1-texel jumps and the row sawtooth), far limit d ≈ 40.5
  texels, sky = landscape grey 2. Draw column 319 too.
- Sine table: `sin(2π·i/2048)`.

Keep (part of the look): the off-centre vanishing point of the landscape (x = 127.5), the flat dark-grey sky and
the hard end of the terrain at d ≈ 40.5, the elliptical (1.4) swirl, the 1.75× clipped texture and the 112 cap
(white-hot cores), the cross-fade that dims the background where there is no fire, the label position (6 pixels
from the right edge when fully in) and its slide of one pixel per retrace, the timings above.

Drop (artefacts): tearing; the P change inside a frame; the one-step darkening at A = 1..15 (start of p4) unless the
p5 port depends on the darker chequerboard DAC (section 7); the never-drawn column 319; the 2-pixel-wide fire
pixels; nearest-neighbour colours and v-nearest heights in the landscape.

## 10. Pictures for redraws

| picture | hand-drawn? | how p4 uses it | what a redraw needs |
|---|---|---|---|
| nebula (`dump/pictures/p4-nebula.png`, 16 colours) | yes (p3's background) | background colour c through the cross-fade: only its palette colour enters, linearly | a true-colour redraw can replace it directly: `(1 − a)·redraw + a·fire` |
| chequerboard (`p4-chequerboard.png`, 15 colours) | yes (p5's background) | the same, in step 7 | the same; it must match p5's redraw at the hand-over |
| labels (`p4-label-fire-clouds.png`, `p4-label-fractal-scape.png`, 70 × 44 greys) | yes | drawn as-is, opaque | a redraw replaces it directly at the same place and size |
| cloud texture (`p4-cloud.png`, shown ×2) | no (rendered / processed image, guess) | sampled through the polar map and the feedback, brightened 1.75× and clipped at 127 | not worth redrawing; use the original filtered. A redraw would have to keep the value range (0..127 before scaling) and the dark edges (it wraps) |
| height map (`p4-landscape-height.png`, shown ×2) | partly (drawn rivers and plateau, guess) | geometry | use the original, interpolated |
| colour map (`p4-landscape-colour.png`, landscape palette) | partly (shading from the heights plus texture, guess) | nearest-texel colour of each landscape span, one of 16 greys | a 4× redraw as continuous grey (the landscape palette's ramp) would sharpen the terrain; it must keep the shading, which matches the height map |

## 11. Models (`models/p4/`, see its README.md)

`hook_p4.py` runs the part from `snaps/part_p4.snap` with hooks (all state into `models/p4/out/`); `fire_model.py`,
`land_model.py`, `timeline.py`, `check_capture.py`, `check_torn.py` check the readings above; `fire_float.py`
compares float forms of the feedback; `fit_tmap.py` fits the swirl map; `dump_pictures.py` writes the PNGs.

## 12. Not determined / guesses

- The swirl map's generator: the fit is within ±1 except near the axes; the exact formula (and why 1.4) is unknown.
- Whether the cloud texture, the height map and the colour map were drawn or generated: judged from their look.
- The landscape's sampling row ⌊P/8⌋ against the sub-step 15 − (P & 15): with ⌊P/32⌋ the rows would move
  consistently. Whether the faster flow was intended is unknown; the description above follows the code.
- The per-column colour buffer 0010:23a1 (an interpolated colour, written when a span is hidden and never read)
  looks like the remains of colour interpolation along u.
- On a CPU too slow to finish a loop iteration within two retraces the fire would step less often; not measured.
