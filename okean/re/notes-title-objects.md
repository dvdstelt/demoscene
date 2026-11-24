# Scope "title + objects": the first part 0x2c26f and the second part 0x2e779

Routines. Title: entry and main loop 0x2c26f, retrace callback 0x2c47b, beams 0x2c0f9 with the two stamp-line
drawers 0x2bc7c (outer beam) and 0x2bf4f (inner beam, masked); its retrace timer is the one at 0x14bec (install),
0x14c67 (calibrate), 0x14c40 (restore), handler 0x14cb8, callback pointer [0x14be0] = 0x2c47b. Objects: entry and
main loop 0x2e779, retrace callback 0x2eb81, transform and project 0x2e5f5, stamp-line drawer 0x2e304; its timer
copy is 0x2c57c (install), 0x2c5f7 (calibrate), 0x2c5d0 (restore), handler 0x2c648 (real-mode stub 0x2c684),
callback pointer [0x2c570] = 0x2eb81. Both parts call the shared text printer 0x4cacc every frame (it draws nothing
in these two parts, see 1.5 and 2.4).

Addresses are code32 offsets (file offset = 0xec0 + address, linear in the emulator = 0x8fc0 + address).
"Record k" = record k of `full.frames`; the record is written at the **end** of refresh interval k (time
(k + 1) / 49.851 s; the `.ticks` time column is the interval's start, k / 49.851 s), and the retrace, with the
callback, falls at about 0.80 of the interval (line 502 of 625). Screen coordinates are 320 x 200 pixels (shown at
4:3). `T[i]` is the dword sine table at 0x24a3c **as the title's set-up leaves it** (1.2), `C[i] = T[i + 0x401]`
(the dwords at 0x25a40, a cosine). Angles are 12-bit (4096 = one turn).

Verified (scripts in `models/title-objects/`, see its README):

- **Title: every record 5..620 (616 of 616) bit for bit** (all 64,000 indices), from the snapshot `part_2c26f`
  (noise seed, mask, memory around the mask) and the callback count alone; the palette of every record 4..620
  (the fade) exactly.
- **Objects: 1678 of the 1679 copies of the part (records 624..2331 minus the 29 skipped intervals) bit for bit**
  from `part_2e779` plus the callback count and the music positions the callbacks saw; the one left (record 1191)
  is a record taken in the middle of the copy to video memory (rows 0..155 equal the model). Every object's
  rotation/translation variables at each of the 10,075 calls of 0x2e5f5 in an emulator log equal the callback
  model's; the palette of every record exactly.
- The generated sine table equals the one in `snaps/part_2e779.snap` (all 0x1401 entries).

---------------------------------------------------------------------------------------------------------------

# Part 1: the title (0x2c26f), records 3..620, music 0:00..0:63

## 1.1 What it draws, and when

Grey noise; 256 beams of light from the screen centre, drawn as rows of 8 x 8 stamps that add 1 to the pixel
index; the start points of the beams lie on an ellipse that grows from the centre (radius pulse with a period of
1024 callbacks) and slowly turns. A 320 x 200 one-bit picture of the title lettering "the sea / ROBOT / of love"
(between two horizontal rules) is never drawn: it is a **mask** that removes light. Inside the ellipse the stamps
that would fall on a letter are skipped, and a beam whose start point lies on a letter loses its whole outer part,
so the letters appear as dark shapes in the light with dark radial shadows behind them. From row 0:56 the palette
fades to black in 32 refreshes.

| records | music | callbacks c | what |
|---|---|---|---|
| 3..4 | before 0:00 | – | black (set-up: sine table, music start 0x13eb1, buffer, timer calibration, palette) |
| 5..62 | 0:00..0:04 | 0..57 | noise only: every start point lies on the "B" of ROBOT around the centre, so no beam is drawn |
| 63 | 0:04 | 58 | the first beams (R = 65), from start points in the counters (holes) of the "B" |
| 63..71 | 0:04..0:05 | 58..66 | more start points leave the "B"; the far points move out with R and at c = 66 the beams reach the screen edge |
| 71..517 | 0:05..0:41 | 66..512 | the start ellipse grows (semi-axes 0.094 R x 0.062 R pixels, R = 1024 - 1023 cos(2 pi c / 1024)); the letters appear as it sweeps over them |
| 517 | 0:41 | 512 | R = 2047: the ellipse (192 x 128 px semi-axes) encloses the screen |
| 573..604 | 0:56..0:61 | 568..599 | palette fade to black, 8/256 per callback |
| 604..620 | 0:61..0:63 | 599..615 | black (indices still drawn) |
| 621 | 1:00 | – | the objects part (its set-up clears the screen) |

## 1.2 Set-up (0x2c26f..0x2c30a) and the sine table

```c
// 0x2c26f: complete the sine table. The file holds T[0..0x400] (a quarter wave); the rest of the 4096 + 1024
// entries are written here (the table is used by all later parts as left here).
for (k = 0; k < 0x400; k++) T[0x401 + k] = T[0x400 - k];   // 0x2c26f: copies backwards from 0x25a3c to 0x25a40
for (k = 0; k < 0x800; k++) T[0x801 + k] = -T[k];          // 0x2c28a
for (k = 0; k < 0x400; k++) T[0x1001 + k] = T[k];          // 0x2c2ad: rep movsd
if (byte[0x142d6] != 1) music_start();                     // 0x13eb1 (byte 0x142d6 = 1: no GUS)
push [8]; buffer = getmem_high(0xfa00); [0x29a58] = buffer; // freed by pop [8] at the end
timer_install(); timer_calibrate();                        // 0x14bec, 0x14c67
dac_write(0, palette 0x2b65c, 768);                        // 6-bit values
p = [0x29a40] = 0;                                         // 0x2c30a. q = [0x29a44] is NOT reset: 0xc00 from the file
```

The file's quarter wave is T[i] = trunc(1023 sin(2 pi i / 4096)) for i = 0..0x400 except 10 entries that are one
lower (94, 280, 322, 439, 463, 480, 617, 758, 853, 905) and T[0x400] = 1022. Because the mirror copies start one
entry late, the generated table is sin(2 pi i / 4096) for i <= 0x400 but sin(2 pi (i - 1) / 4096) for
i = 0x401..0x1400 (a slip of 1/4096 turn), with T[0x800] = T[1] = 1 and T[0x801] = 0; C[k] = T[0x401 + k] =
1023 cos(2 pi k / 4096) for k <= 0x3ff. The remake uses 1023 sin / 1023 cos; the slip is invisible.

## 1.3 Static data, buffers and variables

| what | where | format / content |
|---|---|---|
| title mask | 0x1502c, 64,000 bytes (to 0x24a2c) | 320 x 200 bytes, 0 = clear, 1 = letter; never written. `models/title-objects/title_mask_1502c.png` (1.7) |
| palette | 0x2b65c, 768 bytes | 6-bit RGB (1.6) |
| faded palette | 0x14d20, 768 bytes | written by the fade |
| sine | 0x24a3c, 0x1401 dwords | 1.2 |
| fade factor f | [0x15020] dword | 0x100 in the file |
| noise seed | [0x14d1c] dword | 0x303b = 12347 in the file; 0x38469 does not use it, so it starts the title with that value; only the title uses it |
| p (rotation) | [0x29a40] | set to 0 by the set-up |
| q (radius phase) | [0x29a44] | 0xc00 in the file |
| points | 0x29a5c, 256 x 12 bytes | x, y (dwords), third dword unused |
| projected | 0x2a65c, 256 x 16 bytes | X1, Y1, X2, Y2 |
| line endpoints | [0x29a48] x1, [0x29a4c] y1, [0x29a50] x2, [0x29a54] y2 | |
| fake clock counter | byte [0x2c48e] (inside the code, jumped over by `jmp 0x2c48f`) | |
| unused | [0x15024], [0x15028]: per beam a = ((a + 0x5c3) * 0x541) & 0xfff, b = ((b + 0x4fb) * 0x1187) & 0xfff; the callback's [0x24a2c] = (T[0x19c] + 0x100) << 4 and `and [0x24a30/34/38], 0x1ff` | written, never read: leftovers |

## 1.4 The retrace callback 0x2c47b (49.851 Hz)

```c
if (byte[0x142d6] == 1) {                       // no GUS: fake music clock (also in the objects' callback)
    if (++byte[0x2c48e] == 4) {
        byte[0x2c48e] = 0;
        row = ++word[0x12b73];
        if ((order != 0 && row == 0x30) || row == 0x40) { word[0x12b73] = 0; byte[0x12b75]++; }
    }
}
if ((short)word[0x12b73] >= 0x38 && f != 0) f -= 8;    // the fade (row 56 of order 0; the part only sees order 0)
q = (q + 4) & 0xfff;
p = (p + 1) & 0xfff;
// + the unused writes of 1.3
```

Closed forms, with c = callbacks of this part so far: **p = c mod 4096, q = (0xc00 + 4c) mod 4096**,
f = max(0, 256 - 8 n) with n = callbacks that saw row >= 56.

## 1.5 The main loop (0x2c314..0x2c469), one frame

```c
for (;;) {
    // 0x2c314: 256 points on an ellipse
    R = T[q] + 0x400;                                   // 1..2047;  = 1024 - 1023 cos(2 pi c / 1024)
    for (i = 0, a = p; i < 256; i++, a = (a + 16) & 0xfff) {
        pt[i].x = (((3 * T[a]) >> 1) * R) >> 11;        // sar; (3T)>>1 then imul, then sar 11
        pt[i].y = (C[a] * R) >> 11;
    }
    // 0x2c378: noise, all 64,000 bytes
    s = [0x14d1c];
    for (i = 0; i < 16000; i++) {
        dword(buffer + 4 i) = s & 0x07070707;           // four pixels 0..7
        s = ror32(s + 0x9d, 11) ^ 0x9d;
    }
    [0x14d1c] = s;
    beams();                                            // 0x2c0f9, 1.5.1
    [0x4cac4] = buffer; text_printer();                 // 0x4cacc: nothing is drawn (below)
    wait until port 3DAh bit 3 set; wait until it is clear;     // after the retrace (and its callback)
    if (order == 0 && (short)row >= 0x38) {             // the fade, written every frame from row 56 on
        for (i = 0; i < 768; i++) pal[i] = clamp((P[i] * f) >> 8, 0, 63);   // P = 0x2b65c, into 0x14d20
        dac_write(0, pal, 768);
    }
    copy buffer -> A0000h (16,000 movsd);
    if (order == 1) break;                              // the part ends
    if (in(0x60) == 1) break;                           // Esc
}
timer_restore(); pop [8];
```

The text printer: its string pointer [0x4c8e4] = 0x4c8e8 points to "\r@\r@...". '\r' only measures the next line,
'@' waits (it returns with the pointer unchanged until the order or bit 4 of the row changes, then moves on). In
this part it advances over the "\r@" pairs and draws nothing (the bit-exact match of every record confirms it).
The core scope describes the printer.

### 1.5.1 The beams: 0x2c0f9

```c
for (i = 0; i < 256; i++) {                     // projection with a fixed depth: z = 0x400 and z = 0x10
    X1 = idiv(pt[i].x << 7, 0x400) + 160;  Y1 = idiv(pt[i].y << 7, 0x400) + 100;   // x / 8, truncated toward 0
    X2 = idiv(pt[i].x << 7, 0x10) + 160;   Y2 = idiv(pt[i].y << 7, 0x10) + 100;    // 8 x
}
for (i = 0; i < 256; i++) {
    if (byte[0x1502c + Y1 * 320 + X1] == 0)     // the mask at the start point (read even when it is off the
        line_outer(X1, Y1, X2, Y2);             // 0x2bc7c     screen: then it reads the memory around the mask)
    line_inner(X1, Y1, 160, 100);               // 0x2bf4f
    // + the unused [0x15024] / [0x15028] steps
}
```

Both drawers (0x2bc7c and 0x2bf4f) start the same way:

```c
if (!((unsigned)y1 < (unsigned)y2)) swap(P1, P2);       // sort by y as UNSIGNED: a negative y counts as larger
nx = x2 - x1;  nx = nx >= 0 ? nx + 1 : -nx;             // cdq; xor eax, edx; inc eax
ny = y2 - y1;  ny = ny >= 0 ? ny + 1 : -ny;
n = max_unsigned(nx, ny);  n = (n >> 2) - 1;
if (n <= 0) return;
dx = idiv((x2 - x1) << 8, n);  dy = idiv((y2 - y1) << 8, n);    // 8.8 steps, about 4 pixels long
if (y1 < 0) { x1 += (-y1) * dx; y1 = 0; }               // a "clip" that adds an 8.8 step to an integer: wrong
X = x1 << 8;  Y = y1 << 8;
fast = 0 <= y1 <= 191 && 0 <= y2 <= 191 && 0 <= x1 <= 311 && 0 <= x2 <= 311;   // after the clip
for (k = 0; k < n; k++, X += dx, Y += dy) {             // stamps at P1 + k * step, k = 0..n-1 (P2 not reached)
    sx = X >> 8;  sy = Y >> 8;                          // sar
    if (!fast && (sy < 0 || sy > 191 || sx < 0 || sx > 311)) {
        0x2bc7c: return;                                // outer beam: stop at the first stamp off the screen
        0x2bf4f: continue;                              // inner beam: skip it
    }
    0x2bf4f only: if (byte[0x1502c + sy * 320 + sx] != 0) continue;   // the mask at the stamp's top left pixel
    stamp(sx, sy);
}
// (0x2bf4f has no fast path: it always tests; 0x2bc7c's fast path gives the same stamps.)
```

`stamp(sx, sy)` adds 1 to 60 bytes of the 8 x 8 square with top left (sx, sy): all but the four corners, as 16
dword adds (`add dword [edi], 0x1010100` ... `[edi + 0x8c4], 0x10101`). The sums never pass 255 in this part (the
maximum stamp count is 73), so no byte carries into its neighbour.

What this draws, in continuous terms (theta_i = 2 pi (c + 16 i) / 4096, the screen y axis pointing down):

- start point S_i = centre + (x_i / 8, y_i / 8) with x_i = 1.5 * 1023 R sin(theta_i) / 2048 and
  y_i = 1023 R cos(theta_i) / 2048, i.e. an ellipse with semi-axes 0.0937 R (horizontal) and 0.0624 R (vertical)
  pixels; far point F_i = centre + 8 (x_i, y_i) (64 times as far from the centre);
- the 256 rays are 1/256 turn apart and turn by 1/4096 turn per callback (counter-clockwise on the screen), so the
  fan repeats every 16 callbacks; R pulses with a period of 1024 callbacks (20.5 s): 2 at c = 0, 2047 at c = 512;
- **inner beam**: stamps from S_i to the centre (or from the centre to S_i when S_i is below it), every stamp whose
  top left pixel is a letter pixel left out, stamps whose top left pixel is outside 0..311 x 0..191 left out;
- **outer beam**: only if the mask at S_i is clear: stamps from S_i outwards towards F_i (the unsigned sort puts
  the on-screen end first also for beams that go up), ending at the first stamp outside 0..311 x 0..191; when S_i
  itself is off the screen nothing is drawn, except in the case below.

Quirk (the y clip): when both Y1 and Y2 are negative (a beam pointing up whose start point is above the screen,
R > ~1600), the unsigned sort starts at the far point, the clip moves x by (-y1) * dx (an 8.8 step times a pixel
count) and starts at y = 0. For any beam not exactly vertical that x is far off the screen and nothing is drawn;
for the vertical beam (x = 160) it draws a full-height column of stamps at x 160..167 from y = 0 downwards. That
happens only if the mask byte read at the off-screen start point (player code and data at 0x12f0c..0x14bcc, read
through the out-of-range mask index) is 0: it is in 15 callbacks, c = 449, 464, 465, 480, 481, 496, 497, 512, 513,
528, 529, 544, 545, 560, 561 (records c + 5): a faint vertical bar through the centre in two of every 16 frames
from record 454 to 566. A remake should leave it out (a clipping bug), or draw it in exactly those frames.

## 1.6 Pixel index and palette

index = noise (0..7) + the number of stamps covering the pixel (0..73 in the capture; each beam puts about two
stamps on each pixel of its 8-pixel-wide band, because stamps are about 4 pixels apart). Palette 0x2b65c:
0 = black, 1..47 a grey ramp with a slight blue tint (1 = (5,5,5), 8 = (18,16,18), 16 = (29,29,32),
32 = (46,46,48), 47 = (63,63,63)), 48..103 white (68..94 slightly warm, down to (63,61,59)), 104..255 white
except 111 = (63,34,63) and 254 = (33,33,33), which are never reached. So the index is an intensity on a ramp
that saturates at 47; the remake can accumulate the beam coverage as a continuous value, add the noise, and look
it up in entries 0..47 (clamped above 47).

The fade: DAC = (P * f) >> 8 with f = 256 - 8 n (n = callbacks since the first one that saw row >= 56); record 573
shows f = 248, record 604 f = 0 (black).

## 1.7 The title lettering

The shape is a **bitmap in the executable**, not the intro's font and not geometry: 0x1502c, 64,000 bytes, one
byte per pixel of a 320 x 200 screen, value 1 = letter (19,235 pixels), 0 = clear. Written to
`models/title-objects/title_mask_1502c.png` (white = 1). Layout: "the sea" rows 14..61 (x 30..283), a rule rows
68..73 (x 19..296), "ROBOT" rows 76..122 (x 34..290), a rule rows 125..130 (x 19..296), "of love" rows 134..176
(x 62..265). The screen centre (160, 100) lies in the "B". The letters are a bold grotesque (Helvetica Bold like:
lower case "the sea", "of love", capitals "ROBOT"), one bit, not anti-aliased; the intro's own font (0x3cec2, used
by the text printer) is a different, lighter typeface with anti-aliased edges. It looks typeset with a paint
program's text tool rather than drawn by hand (a guess from the regular letter shapes; graphics are credited to der
piipo). It is never shown as a picture: only the effect reads it, as a mask. A remake that wants sharp shadows
needs the shape at high resolution (a faithful vector redraw of exactly these glyphs and rules, or a 4x upscale of
the bitmap); the mask is tested at a stamp's top left pixel, so the dark letter shapes appear offset by about
(+3.5, +3.5) pixels from the mask and softened by the 8-pixel stamps.

## 1.8 Time bases and which record shows what

- Callback (49.851 Hz): p, q, f (closed forms in 1.4). Record k (5..620) shows the frame drawn with **c = k - 5**
  callbacks done; the copy to video memory follows callback c + 1, which falls in interval k (the `.ticks`
  timer_irqs column, counted at the end of the interval, is k - 4). No interval of the title was missed.
- Per drawn frame: only the noise (16,000 generator steps per frame). On a slow machine the noise changes less
  often; everything else follows the callback.
- Music: the part runs while order = 0; the fade starts with the first callback that sees row >= 56; the main
  loop leaves after the copy in which it sees order 1.
- Random numbers: `s = ror32(s + 0x9d, 11) ^ 0x9d` from [0x14d1c] = 12347 at the part start, 16,000 calls per
  drawn frame, each call gives four pixels (its four bytes & 7). The indices are uniform 0..7 (12.5 % each), i.e.
  dark grey 0..16/63, independent per pixel to the eye. A remake can draw per-pixel uniform noise of 8 levels at
  the original's pixel size (or at its own resolution; the look is fine grain).

## 1.9 Low-resolution shortcuts and what they approximate

- Rays are rows of 8 x 8 stamps (corners cut) about 4 pixels apart, adding 1: a ray is a band 8 pixels wide with
  about 2 units of intensity (60 / ~4 / 8 per pixel), slightly dotted along its length. Evaluate as an
  anti-aliased band of the same width (8/320 of the screen width) and intensity, from the same start point
  (stamp top left = band offset of +3.5 px right and down from the true ray) to the same end (the screen edge for
  outer rays; the centre, minus one step, for inner rays).
- Integer projection (x / 8 and 8 x truncated), 8.8 stepping, the step count (max(|dx|, |dy|) / 4 - 1) and the
  12-bit sine table: use the continuous formulas of 1.5.1.
- The mask is a 1-bit 320 x 200 picture; the per-stamp mask test makes the letters blocky: test the redrawn mask
  per pixel.
- The noise is 8 levels per 320 x 200 pixel.

## 1.10 Whole frame

Yes: the noise writes all 64,000 bytes every frame, the beams add to them.

## 1.11 Not determined

- Whether the title bitmap was drawn by hand or set with a typeface (1.7).
- On real hardware the out-of-screen mask reads (1.5.1) read the same memory (player code and channel data), so the
  vertical bar should appear there too; not checked on hardware.

---------------------------------------------------------------------------------------------------------------

# Part 2: the objects (0x2e779), records 621..2330, music 1:00..6:47

## 2.1 What it draws, and when

One wireframe object, an extruded 8-tooth cog (64 vertices, 96 edges), drawn in several copies with glowing lines
(8 x 8 stamps with a weighted pattern, added to the index) over a background of horizontal bands (two sines of the
row, one scrolling) with a per-row noise pattern. Orders 1-2: eight copies turned 1/8 turn apart about the
vertical axis form a ribbed ball; the camera starts inside it and backs away; in order 2 the copies move apart on a
horizontal ring (eight small flying cogs) and from row 32 fly away into the distance. Orders 3-6: five copies in a
row along their common axis form a tube that slides in, scrolls endlessly, swings and tumbles; from order 4 the
palette blends from red to green in 256 refreshes (through orange and brown); from 6:32 the tube stops looping
and slides out along its axis.

| records | music | what |
|---|---|---|
| 621..623 | 1:00 | black: the set-up clears video memory and writes palette 0x2d9e4; first callback in interval 624 |
| 624..~706 | 1:00..1:11 | the camera inside the ball (zdist from -2048, +64 per callback): long lines across the whole screen, many to vertices behind the camera (2.9) |
| ~706..905 | 1:11..1:47 | the ribbed ball recedes and turns (angD +15, angC0 +2 per callback); 811..914 the centre is so bright that indices pass 255 |
| 906..1095 | 2:00..2:31 | the eight cogs move apart on a ring (xoff +256 per callback) and spin (angA +64) |
| 1096..1190 | 2:32..2:47 | they fly away (zdist +0x634 per callback) and vanish |
| 1191..1318 | 3:00..3:21 | the tube of five cogs slides in along its axis (tubeX from -32764, +256 per callback) |
| 1319..2235 | 3:21..6:31 | the tube scrolls through (period 32 callbacks), swings about the vertical axis (sum of three sines) and tumbles (angD +5) while each cog spins (angA +16) |
| 1476..1732 | 4:00..4:43 | palette blend 0x2d9e4 (red) -> 0x2dce4 (green), 8/2048 per callback |
| 2236..2331 | 6:32..6:47 | the tube no longer wraps and slides out along its axis |
| 2331 | 7:00 | the last copy (order 7 seen); the record already has the next part's palette |

## 2.2 Static data, buffers and variables

| what | where | format / content |
|---|---|---|
| palette 0 (red) | 0x2d9e4, 768 bytes | 6-bit: 0, 1 black; 2..73 red ramp (R 1 -> 63; G, B from index 29, (63,28,24) at 73); 73..143 orange, yellow, white ((63,46,34) at 95, (63,61,53) at 128); 143..255 white; 254 = (33,33,33) |
| palette 1 (green) | 0x2dce4, 768 bytes | 6-bit: 0 black, 1..16 dark grey-green ((8,10,9) at 7, (11,13,12) at 16), 16..~110 olive to khaki ((20,23,8) at 32, (33,36,21) at 64, (46,49,38) at 104), ~110..158 to pale yellow, 159..255 white |
| edges | 0x2d0e4, 96 x 8 bytes | byte 0 = vertex a, byte 4 = vertex b, other bytes 0 (2.3) |
| vertices | 0x2cae4, 64 x 12 bytes | generated (2.3) |
| unused vertices | 0x2cde4, 64 x 12 bytes | generated with radii 2046 / 6138, never read |
| projected | 0x2d3e4, 64 x 8 bytes | X, Y dwords |
| row values | 0x2c9c8, 200 bytes | background value per row (2.5) |
| buffer | [0x2cae0] = (getmem_high(0xfa05) \| 3) + 1 | 64,000 bytes, dword aligned |
| line endpoints | [0x2cad0] x1, [0x2cad4] y1, [0x2cad8] x2, [0x2cadc] y2 | |
| step shift | byte 0x2e376 = the immediate of `shr ecx, imm8` at 0x2e374 | 0x78 in the file; the set-up writes 2, the callback writes 1 from order 3 on |

Variables (dwords, values in the file = at the part start):

| name | address | start | meaning |
|---|---|---|---|
| angA | 0x2cabc | 0 | rotation about the cog's own axis z (x-y plane) |
| angB | 0x2c9bc | 0 | rotation in the x-z plane before the translation (0x400 from order 3: the cog axis becomes x) |
| xoff | 0x2ca94 | 0 | x translation (order 2: the ring radius) |
| xoff2 | 0x2c9c0 | 0 | x translation per copy (the tube; written by the main loop) |
| angC | 0x2cab4 | 0 | rotation in the x-z plane after the translation (about the vertical axis) |
| angC0 | 0x2cab8 | 0 | base of angC in orders 1-2 |
| angD | 0x2ca98 | 0 | rotation in the y-z plane (about the horizontal x axis) |
| zdist | 0x2ca90 | -2048 | z translation (distance) |
| tubeX | 0x2c9c4 | -32764 | tube scroll |
| flag | 0x2c9b8 | 0 | set to 1 when tubeX first wraps; nothing else reads it |
| ph1, ph2, ph3 | 0x2caa8, 0x2caac, 0x2cab0 | 0 | phases of angC's sines (order >= 3) |
| bg1, bg2 | 0x2cac0, 0x2cac4 | 2048, 3328 | background phases |
| bgd1, bgd2 | 0x2cac8, 0x2cacc | 10, 32 | background phase steps per row |
| palw0, palw1 | 0x2c9ac, 0x2c9b0 | 2048, 0 | palette blend weights |
| seed | 0x2c9b4 | 135477 (0x21135) | background noise generator (only this part uses it) |
| fake clock | byte 0x2eb94 | | as in the title |

## 2.3 The mesh: an extruded cog

```c
// 0x2e7c2 (set-up): 64 vertices; vertex n = 32 h + 4 j + k (h = 0: z = +0x400, h = 1: z = -0x400; j = 0..7)
for (h = 0; h < 2; h++) {
    z = h ? -0x400 : 0x400;  a = 0;
    for (j = 0; j < 8; j++) {
        v[32h+4j+0] = ( T[a],    C[a],    z);   v[32h+4j+1] = (3 T[a], 3 C[a], z);   a += 0x100;
        v[32h+4j+2] = (3 T[a], 3 C[a], z);      v[32h+4j+3] = ( T[a],   C[a],  z);   a += 0x100;
    }
}
```

So each face is a cog outline: r = 1023 and 3069 (3 x 1023), (x, y) = r (sin a, cos a), a = 0, 22.5, 45, ...
degrees; the polygon 0-1-2-...-31-0 goes out along a radius, along the outer chord, in along the next radius,
along the inner chord: 8 square teeth of 22.5 degrees with 22.5-degree gaps. The edges (static, 0x2d0e4) are the
front outline (i, i+1 mod 32), the back outline (32+i, 32+(i+1) mod 32) and the 32 connectors (i, 32+i). The
outer and inner edges are chords (straight), not arcs. Half length 1024, outer radius 3069.

## 2.4 The main loop (0x2e9ca..0x2eb6f)

```c
// set-up 0x2e779: byte[0x2e376] = 2; clear A0000h (16,000 dwords); buffer; palette 0x2d9e4 -> DAC; mesh (2.3);
// timer_install() 0x2c57c; timer_calibrate() 0x2c5f7
for (;;) {
    background();                                   // 2.5: writes all 64,000 bytes
    if (order < 3) {                                // 0x2ea68 (order read here, after the background)
        angC = angC0;
        for (k = 0; k < 8; k++) { draw_object(); angC = (angC + 0x1ff) & 0xfff; }   // left at angC0 + 8 * 0x1ff
    } else {
        xoff2 = tubeX - 0x5000;
        for (k = 0; k < 5; k++) { draw_object(); xoff2 += 0x2000; }                 // left at tubeX + 0x5000
    }
    [0x4cac4] = buffer; text_printer();             // 0x4cacc: draws nothing in this part (as in the title)
    wait until port 3DAh bit 3 set; wait until it is clear;
    if (order >= 4) {                               // the blend, written every frame from order 4 on
        for (i = 0; i < 768; i++) pal[i] = (P0[i] * palw0 + P1[i] * palw1) >> 11;   // into 0x2c6ac
        dac_write(0, pal, 768);
    }
    copy buffer -> A0000h;
    if (order == 7) break;
    if (in(0x60) == 1) break;
}
timer_restore(); pop [8];                            // 0x2c5d0
```

`draw_object()` = 0x2e5f5: transform and project the 64 vertices, then `line(P[a], P[b])` (0x2e304) for the 96
edges in their order.

```c
// 0x2e5f5, per vertex (x, y, z); rot(u, v, ang): u' = (u C[ang] - v T[ang]) >> 10, v' = (v C[ang] + u T[ang]) >> 10
//   (32-bit imul, sar 10; the table amplitude 1023 makes every rotation shrink by 1023/1024)
(x, y) = rot(x, y, angA);          // about the cog axis
(x, z) = rot(x, z, angB);
x += xoff + xoff2;
(x, z) = rot(x, z, angC);          // about the vertical (screen y) axis
(y, z) = rot(y, z, angD);          // about the horizontal (screen x) axis
z += zdist;
if (z > 0) { X = idiv(x << 7, z);  Y = idiv(y << 7, z); }    // focal length 128 pixels, x and y alike
else       { X = x * (1 - z);      Y = y * (1 - z); }        // behind the camera: 32-bit imul, no clipping
P = (X + 160, Y + 100);
```

The same focal length in x and y on the 320 x 200 grid means the picture, shown at 4:3, is stretched vertically by
1.2 (keep it).

```c
// 0x2e304: line with stamps
if (!(y1 > 0) && y2 < 0) return;                 // both above the screen
if (!((unsigned)y1 < (unsigned)y2)) swap;        // unsigned sort; with the test above, y1 >= 0 after it
nx, ny as in the title (|d| + 1 for d >= 0);
n = max_unsigned(nx, ny) >> shift;               // shift = byte 0x2e376: 2 in orders 1-2, 1 from order 3
if (n == 0) return;
dx = idiv((x2 - x1) << 8, n);  dy = idiv((y2 - y1) << 8, n);   // 32-bit: (x2 - x1) << 8 wraps for |dx| >= 2^23
if (y1 < 0) { x1 += (-y1) * dx; y1 = 0; }        // the title's wrong clip; never runs here (see the sort)
X = x1 << 8;  Y = y1 << 8;
if (0 <= y1 <= 191 && 0 <= y2 <= 191 && 0 <= x1 <= 311 && 0 <= x2 <= 311)
    for (k = 0; k < n; k++)  { stamp(X >> 8, Y >> 8); X += dx; Y += dy; }          // fast path
else {
    cnt = n & 0xff;                              // 'dec cl': only the low byte of n counts (0 means 256)
    do {
        sx = X >> 8; sy = Y >> 8;
        if (0 <= sy <= 191 && 0 <= sx <= 311) stamp(sx, sy);   // off-screen stamps are skipped, not the end
        X += dx; Y += dy;
        cnt = (cnt - 1) & 0xff;
    } while (cnt != 0);
}
```

`stamp(sx, sy)` adds this 8 x 8 pattern (row by row, columns left to right) to the bytes at (sx, sy) as 16 dword
adds, in the order [0], [+0x140] ... [+0x8c0], [+4], [+0x144] ... [+0x8c4]:

```
6 1 1 1 1 1 1 6
1 1 1 1 1 1 1 1
1 1 1 2 2 1 1 1
1 1 2 6 6 2 1 1
1 1 2 6 6 2 1 1
1 1 1 2 2 1 1 1
1 1 1 1 1 1 1 1
6 1 1 1 1 1 1 6        (sum 112)
```

Because the adds are dwords (unaligned), a byte that passes 255 wraps and carries 1 into the next byte of that
dword; the result then depends on the order of the adds (the Python model `objects.py: stamp_exact` follows it).
This happens in 78 records, 811..914 (the small bright ball, where all lines meet).

## 2.5 The background (0x2e9ca)

```c
a = bg1; b = bg2;                                     // bg1 = 2048 always, bg2 = (3328 - 16 c) mod 4096
for (y = 0; y < 200; y++) {
    rowv[y] = ((T[a] + 0x400) >> 6) + ((T[b] + 0x400) >> 8);    // 0..31 + 0..7
    a = (a + 10) & 0xfff;  b = (b + 32) & 0xfff;      // bgd1, bgd2
}
s = seed;
for (y = 0; y < 200; y++) {
    d = rowv[y] * 0x01010101 + (s & 0x03030303);     // four pixels: row value + 0..3 each
    fill row y with d (80 dwords: the same 4-pixel pattern across the whole row);
    s = rol32(s + 0x9d, 11) ^ 0x9d;                   // ROL (the title's generator uses ROR)
}
seed = s;
```

In continuous terms: rowv(y) = (1024 - 1023 sin(2 pi 10 y / 4096)) / 64 + (1024 + 1023 sin(2 pi (bg2 + 32 y) /
4096)) / 256, both truncated. The first term is 16 at the top, 0 at y = 102 and 14 at the bottom: bright red at
the top and bottom edges, black in the middle (the dark "horizon"); the second term adds bands with a period of
128 rows that move down by half a row per callback (25 rows per second). The noise is a random 4-pixel pattern
(values 0..3) per row, repeated across the row, new every drawn frame: fine vertical-stripe texture that changes
row by row. Background indices are 0..41.

## 2.6 The callback 0x2eb81 (49.851 Hz)

```c
fake music clock (as 1.4, counter byte 0x2eb94);
if (order >= 4 && palw0 != 0) { palw0 -= 8; palw1 += 8; }
if (order < 3) {
    angD = (angD + 15) & 0xfff;  angC0 = (angC0 + 2) & 0xfff;  zdist += 0x40;
    if (order >= 2) {
        xoff += 0x100;  angA = (angA + 0x40) & 0xfff;
        if ((short)row >= 0x20) zdist += 0x5f4;
    }
} else {
    byte[0x2e376] = 1;  xoff = 0;  angB = 0x400;  zdist = 0x1555;  angC0 = 0x400;
    ph1 = (ph1 + 13) & 0xfff;  ph2 = (ph2 + 23) & 0xfff;  ph3 = (ph3 - 9) & 0xfff;
    angA = (angA + 16) & 0xfff;  angD = (angD + 5) & 0xfff;
    angC = ((((T[ph1] >> 1) + T[ph2]) >> 1) + T[ph3]) & 0xfff;   // sar
    tubeX += 0x100;
    if (!(order >= 6 && (short)row >= 0x20) && tubeX >= 0) { flag = 1; tubeX &= 0x1fff; }
}
bg2 = (bg2 - 16) & 0xfff;  bg1 &= 0xfff;
```

Closed forms (c = callbacks of the part; n1 = callbacks that saw order 1 or 2, n2 = those that saw order 2,
n2b = those that saw order 2 with row >= 32, m = those that saw order >= 3, m4 = those that saw order >= 4):

- angD = 15 n1 + 5 m, angC0 = 2 n1, angA = 64 n2 + 16 m (mod 4096); zdist = -2048 + 64 n1 + 0x5f4 n2b while
  order < 3, then 0x1555; xoff = 256 n2 (order 2), then 0;
- angC (order >= 3) = (((1023 sin(2 pi 13 m / 4096) / 2 + 1023 sin(2 pi 23 m / 4096)) / 2 + 1023
  sin(-2 pi 9 m / 4096)) mod 4096: a swing of up to +-1790/4096 turn (+-157 degrees) around 0;
- tubeX = -32764 + 256 m until it reaches >= 0 (m = 128, record ~1319), then (4 + 256 (m - 128)) mod 8192 (the
  cogs are 8192 apart, so the tube loops seamlessly every 32 callbacks) until the first callback that sees 6:32,
  then it grows by 256 per callback without wrapping;
- palw1 = min(2048, 8 m4), palw0 = 2048 - palw1 (blend done after 256 callbacks);
- bg2 = (3328 - 16 c) mod 4096.

In orders 1-2 the main loop uses angC = angC0 + 0x1ff k for the eight copies (k = 0..7, 0x1ff = 1/8 turn minus
1/4096) and xoff2 = 0; in orders >= 3 xoff2 = tubeX - 0x5000 + 0x2000 k (k = 0..4) and angC from the callback.
In the first draw of order 3 (record 1192) angC is the value the main loop left (angC0 + 8 x 0x1ff), because the
callback had not yet seen order 3.

## 2.7 Time bases, music events, and which record shows what

- Callback: everything in 2.6. Per drawn frame: only the background noise (200 generator steps per frame) and
  angC / xoff2, which the main loop recomputes from callback state (no accumulation). On a slow machine only the
  noise changes less often.
- Music positions: order 2 (record 906): the cogs spread and spin; 2:32 (1096): they fly off; order 3 (1191): the
  tube (step shift 1: twice as many stamps per line, so the lines are twice as bright); order 4 (1476): the palette
  blend; 6:32 (2236): the tube stops looping; order 7 (2331): the part ends after the copy in which the main loop
  sees order 7.
- Record k (624..2331) shows the frame drawn with **c = k - 624** callbacks done; the copy follows callback
  c + 1 in interval k. Record 2331 shows the last copy with the next part's palette. In 29 intervals of the capture (651, 746, 835, 924, 930, 1013, 1019, 1102, 1197, 1203,
  1286, 1298, 1375, 1381, 1464, 1476, 1559, 1565, 1654, 1737, 1826, 1832, 1915, 1921, 1927, 2010, 2099, 2188,
  2277) the main loop missed the retrace: a music tick with new notes arrived just before it, and the player's
  interrupt handler waits for its volume-ramp interrupt longer than the 2-line retrace lasts. The record of such an
  interval repeats the previous one, the next record shows c = k - 625, and from then on the draws (and the noise)
  are one behind the refresh count. Record 1191 was taken while the copy was interrupted the same way (rows 0..155
  new, 156..199 old). A remake draws every refresh.
- Random numbers: `s = rol32(s + 0x9d, 11) ^ 0x9d` from [0x2c9b4] = 135477 at the part start, 200 calls per drawn
  frame, one per row, the four bytes & 3 giving a 4-pixel pattern (uniform 0..3).

## 2.8 Pixel index and palette

index = rowv(y) + noise (0..3) + the sum of the stamp patterns covering the pixel (mod 256 with the carries of
2.4). The palette maps it as an intensity ramp: background 0..41 is black to mid red (green in palette 1), lines
add 1..6 per stamp, overlaps go through orange and yellow to white at 143 (159 in the green palette). DAC =
(P0 * palw0 + P1 * palw1) >> 11, written every frame from order 4 (before it, P0 as written by the set-up).

## 2.9 Low-resolution shortcuts and quirks

- The lines are rows of 8 x 8 stamps 4 pixels apart (orders 1-2) or 2 pixels apart (orders 3+). Averaged along a
  horizontal line the stamp gives a cross-section of 4.5, 2, 2.5, 5, 5, 2.5, 2, 4.5 index units per pixel row
  (twice that with step 2): a bright core with two thinner bright edges 3.5 pixels away, and bright dots where
  the corner and centre 6s of successive stamps meet: the "glowing", sparkling look. The stamp's top left is on
  the line, so the glow is centred 3.5 pixels right of and below the true line. A remake can splat the same kernel
  (scaled: 8/320 of the screen width) at the same spacing with sub-pixel positions, or integrate it along the
  line (which loses the dots).
- Integer vertex positions (idiv), 8.8 steps, the 12-bit sine table and the >> 10 rotations: use floats.
- **Lines with an end behind the camera** (z <= 0): projected as (160 + x (1 - z), 100 + y (1 - z)), i.e. thrown far
  out in the direction of (x, y) without clipping. They give visible lines in 120 records (624..706, the camera
  inside the ball, and 1511..2249 in short runs, when the tube's ends swing behind the camera).
- **Long lines cut short**: off-screen lines count only the low byte of their stamp count ('dec cl'), so a line
  longer than 1020 pixels (510 from order 3) stops after ((n - 1) & 0xff) + 1 stamps from its first end (the end
  with the smaller y as unsigned). Visible in 150 records (624..706 and short runs in 1448..2251) as long lines
  that end in the middle of the screen, with lengths that jump from frame to frame. For such lines dx << 8 can
  also wrap (|dx| >= 2^23, only for ends behind the camera), which changes their direction.
  A remake that wants these frames as they are should compute the projected ends and the count n with the
  original's integer formulas (in 320 x 200 units) and draw the fraction (((n - 1) & 0xff) + 1) / n of the line;
  drawing them in full is the other choice (a Known difference).
- Index overflow: in records 811..914 the centre of the ball passes 255 and wraps to dark specks inside the white
  (with byte carries). An 8-bit artefact: clamp at white.
- Background: 200 rows of an integer sum of two sine terms (truncated to 32 + 8 levels) and a 4-pixel noise
  pattern per row: evaluate the sines continuously per (high-resolution) row; keep the per-row 4-pixel noise
  pattern (scaled) for the texture.

## 2.10 Whole frame

Yes: the background writes all 64,000 bytes every frame; the lines add to them. Records 621..623 are black (the
set-up clears video memory).

## 2.11 Not determined

- Nothing that changes the picture: every copy except the torn record is reproduced. The vertex set at 0x2cde4 is
  built and never read (maybe meant for another object).

---------------------------------------------------------------------------------------------------------------

## Line drawers in other parts

The title's drawers (0x2bc7c, 0x2bf4f; stamp of ones) and the objects' drawer (0x2e304; weighted stamp, step
shift patched at 0x2e376) exist only in these parts. Other parts have drawers of the same build with other stamps
(for example 0x35252, 0x3655a, 0x381bc: rows 0 1 1 1 / 1 1 2 2 / 1 2 4 8 / 1 2 8 15 ...), described in their scopes.
