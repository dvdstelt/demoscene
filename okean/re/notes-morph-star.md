# Scope "morph + star": the fourth part 0x367e5 (the morphing object) and the sixth part 0x3c8da (the web and the star)

Both parts are the same kind of effect: a wireframe whose edges are drawn as rows of small glowing 8x8 "blobs" that
are **added** to a buffer of per-pixel noise, shown through a black → colour → white palette ramp. The fourth part
is one parametric object, a sphere whose radius is modulated by two travelling sine waves (it "morphs" continuously;
nothing switches at music positions). The sixth part is a tunnel: 16 rings of 8 points stream towards the camera
along the view axis; the far rings overlap into the bright "star" in the middle, the near rings and the vertices
behind the camera make the "web" of rings and spokes.

Addresses are code32 offsets (file offset in `unpacked/korso2.bin` = 0xec0 + address; linear in the run = 0x8fc0 +
address). `T[i]` is the 32-bit sine table at 0x24a3c, `C(a)` = `T[a + 1025]` the cosine (the table at 0x25a40);
section 1. "Callback c" = the c-th call of the part's retrace callback (c = 1 is the first); "state c" = the
variables after c callbacks (state 0 = the values in the file). Positions are order:row in decimal.

Verified (models in `models/morph-star/`, section 6):

| what | how | result |
|---|---|---|
| sine table 0x24a3c | rebuilt from the file's quarter wave with the title part's mirror (0x2c26f), compared with the snapshot `part_367e5.snap` | equal (5121 entries) |
| part variables at the start | `snaps/part_367e5.snap`, `part_3c8da.snap` against the file: 0x35d3c..0x364b0 and 0x38d1c..0x3c128 | equal: both parts start from the file's values |
| fourth part, whole frames | `morph.py` (state from the callback count, noise from the number of drawn frames) against every record 3474..4692 of `full.frames` that got a copy (`verify_morph.py`, mapping from a hooked run; the 28 records without a copy repeat the previous one) | 1190 of 1191 records equal to the byte, DAC equal in all; the other one (3721) is a torn record: the copy to video memory was interrupted by the music interrupt and the frame ended at dword 9993 of the copy (rows 0..123 new, the rest the previous frame, both equal to the model) |
| sixth part, whole frames | `star.py` against every record 5755..6891 that got a copy (`verify_star.py`; 21 repeat the previous one), the 93 caption records with the shared printer's text drawn by `common.caption` | all 1116 records equal to the byte, DAC equal in all |

---------------------------------------------------------------------------------------------------------------

## 1. Shared pieces

### 1.1 The sine table

Both parts read the table the first part completes in its set-up (0x2c26f, scope "title + objects"): entries 0..1024
of the dwords at 0x24a3c are in the file (a quarter wave, peak 1022 at 1020..1024); 0x2c26f writes 1025..2048 =
entries 1024..1, 2049..4096 = −(0..2047), 4097..5120 = 0..1023. Indices are always masked with 0xfff first, so
`T[i]` ≈ 1022 sin(2π i / 4096) (from entry 1025 on, one entry late: 1022 sin(2π (i − 1) / 4096)) and `C(a)` =
`T[a + 1025]` ≈ 1022 cos(2π a / 4096) exactly in phase. The remake evaluates sin / cos with amplitude 1022/1024
where a product is shifted by 10 (or ignores the 0.2 %).

### 1.2 The blob line drawer (0x363d0 in the fourth part, 0x3c46e in the sixth)

Each part has its own copy (every part of the intro has one, with its own kernel: title 0x2bded/0x2beaa/0x2c054,
objects 0x2e490/0x2e550, landscapes 0x35252/0x35319 and 0x381ba/0x3828b, sheet 0x53158). The two here differ only in
the kernel and in three extra trivial rejects in the sixth part's copy. Endpoints are dwords (X1, Y1, X2, Y2) =
fourth part [0x35d9c], [0x35da4], [0x35da0], [0x35da8]; sixth part [0x38df0], [0x38df4], [0x38df8], [0x38dfc].

```c
void line(int X1, int Y1, int X2, int Y2)          // 320x200 pixel coordinates, any range
{
    if (!(Y1 > 0) && Y2 < 0) return;                // both above the screen (roughly)
    if (sixth_part) {                               // 0x3c488..0x3c4dc only in 0x3c46e
        if (!(X1 > 0) && X2 < 0) return;
        if (X1 >= 320 && X2 > 320) return;
        if (Y1 >= 200 && Y2 > 200) return;
    }
    if ((unsigned)Y1 >= (unsigned)Y2) swap(P1, P2); // unsigned: a negative Y counts as huge, so an endpoint
                                                    // above the screen always becomes P2 (the 'Y1 < 0' clip at
                                                    // 0x36476 / 0x3c569 is never reached)
    int dx = X2 - X1, dy = Y2 - Y1;
    unsigned nx = dx >= 0 ? dx + 1 : -dx;           // cdq; xor eax,edx; inc eax
    unsigned ny = dy >= 0 ? dy + 1 : -dy;
    unsigned n = max(nx, ny) >> 1;                  // one blob every ~2 pixels of the major axis
    if (n == 0) return;                             // lines of 0..1 pixel are not drawn
    int sx = (dx << 8) / (int)n, sy = (dy << 8) / (int)n;   // 8.8 steps, idiv: truncated toward zero
    int x = X1 << 8, y = Y1 << 8;
    if (0 <= Y1 && Y1 <= 191 && 0 <= Y2 && Y2 <= 191 && 0 <= X1 && X1 <= 311 && 0 <= X2 && X2 <= 311) {
        for (i = 0; i < n; i++) { blob(x >> 8, y >> 8); x += sx; y += sy; }       // fast path, no checks
    } else {
        for (i = 0; i < ((n & 0xff) ? (n & 0xff) : 256); i++) {                  // dec cl: 8-bit counter!
            int bx = x >> 8, by = y >> 8;           // sar: floor
            if (0 <= by && by <= 191 && 0 <= bx && bx <= 311) blob(bx, by);
            x += sx; y += sy;
        }
    }
}

void blob(int bx, int by)       // 16 dword adds; row table y*320 at 0x360b0 / 0x3c14e (static, 200 dwords)
{
    for (r = 0; r < 8; r++) {
        *(uint32_t *)&buf[(by + r) * 320 + bx]     += K[r][0];     // 32-bit adds: a byte that passes 255
        *(uint32_t *)&buf[(by + r) * 320 + bx + 4] += K[r][1];     // carries into the pixel to its right
    }
}
```

The kernels (the dwords at 0x36618.. / 0x3c710.., as pixel values, the blob's top-left at (bx, by)):

```
fourth part (sum 164)        sixth part (sum 208)
0 1 1 1 1 1 1 0              0 2 2 2 2 2 2 0
1 1 2 2 2 2 1 1              2 4 4 4 4 4 4 2
1 2 4 8 8 4 2 1              2 4 4 4 4 4 4 2
1 2 6 8 8 6 2 1              2 4 4 8 8 4 4 2
1 2 6 8 8 6 2 1              2 4 4 8 8 4 4 2
1 2 4 8 8 4 2 1              2 4 4 4 4 4 4 2
1 1 2 2 2 2 1 1              2 4 4 4 4 4 4 2
0 1 1 1 1 1 1 0              0 2 2 2 2 2 2 0
```

What a line looks like, and how to evaluate it at high resolution:

- The blobs sit at P1 + i·(P2 − P1)/n, i = 0..n−1 (the last one about one step short of P2), with the **kernel's
  centre at +3.5, +3.5 pixels**: every line, and so the whole picture, is shifted 3.5 pixels right and down from the
  projected coordinates. Keep the offset (centre the glow on P + (3.5, 3.5) in 320x200 units).
- Blob spacing along the line is |P2 − P1| / n ≈ 2 / max(|cos α|, |sin α|) pixels: the summed brightness per unit
  length is proportional to max(|cos α|, |sin α|), so 45° lines get 1/√2 of the light of horizontal and vertical
  ones. Cross profile of a horizontal line (row sums / 2): fourth part 3 6 15 17 17 15 6 3, sixth part 6 14 14 18
  18 14 14 6; of a vertical line (column sums / 2): fourth 3 6 13 19 19 13 6 3, sixth 6 14 14 18 18 14 14 6. The
  remake: treat the kernel as a continuous function (bilinear over the 8x8 grid, 8 x 8 pixels of the 320x200 frame,
  i.e. 8 wide and 9.6 tall at 4:3), integrate it along each line with the density n / |P2 − P1| per 320x200 pixel
  of length, add everything (and the noise) per output pixel, then look the sum up in the palette ramp.
- Whole-blob clipping: a blob is drawn only if its top-left is in [0, 311] x [0, 191]; so nothing ever reaches
  column 319 or row 199, and lines end about 4 pixels before the left and top edges and 1..8 pixels before the right
  and bottom edges. Artefact: clip at the real screen edges instead.
- The slow path counts with `cl`: a line that is not entirely on screen and has more than 255 steps gets only
  n mod 256 blobs, from P1 (the endpoint with the smaller unsigned Y). Never happens in the fourth part (its longest
  lines have n ≈ 254); in the sixth it cuts one or two of the long spokes short in about half of the frames (3..44
  on-screen blobs lost per frame in a sample of 28 frames, `trunc.py`): a spoke that stops in mid-screen for a frame.
  Artefact of the 8-bit counter: draw the whole line.
- Lines shorter than 2 pixels (n = 0) are not drawn: invisible at high resolution anyway.
- Overlaps add up; the sum wraps above 255 with a carry into the next pixel (sections 2.7, 3.5): artefact.

### 1.3 Noise

Each frame starts by filling the whole 64,000-byte buffer with noise (so both parts overwrite the whole frame), then
the lines are added. Both generators run once per **drawn frame** (16,000 dwords, one dword = 4 pixels, row-major),
not per refresh: on a machine that drops frames the noise changes less often. Statistics (from the models, 3 frames
each): every index equally likely, mean and deviation of a uniform distribution, no correlation between neighbours
(|r| < 0.025). A remake draws fresh uniform noise of the same range per displayed frame; the original grain is one
320x200 pixel (1 x 1.2 at 4:3).

---------------------------------------------------------------------------------------------------------------

## 2. The fourth part 0x367e5: the morphing object

### 2.1 What is drawn, and when

A wireframe of 32 latitude rings x 16 meridians, red-pink to white over dark red noise (indices 0..15), turning
and tumbling, its radius rippled by two sine waves that travel over it, coming close to the camera and receding.
It builds up edge by edge at the start: first the rings one after the other (they look like a stack of tori), then
the meridians column by column. At order 15 it fades out (with a jump back up at row 5) and the part ends when the
fade reaches 0.

Records are `full.frames` records (time = the refresh's start), "state" the callback count the record's picture
was drawn with, "callbacks" the callbacks whose fade value the record's palette shows (section 2.8).

| what | state / callbacks | records | time (s) | music | |
|---|---|---|---|---|---|
| set-up | – | 3471..3473 | 69.63..69.67 | 11:00 | part 3's last frame stays (with part 4's palette, written at the start of the set-up); the timer calibration takes two refreshes; the first frame is drawn in refresh 3473 |
| build-up | state 0..123 | 3474..3597 | 69.69..72.16 | 11:00..11:20 | edge count 8 (c + 1): rings 0..31 complete at c = 1, 3, .., 63 (ring 0 is a point: invisible; the first two records show only noise); meridians: column k complete at c = 63 + 3.75 (k + 1); all 992 from c = 123 |
| the object | state 124..1137 | 3598..4611 | 72.18..92.50 | 11:21..14:47 | |
| fade, first ramp | callbacks 1139..1167 | 4612..4640 | 92.52..93.08 | 15:00..15:04 | fade 63 → 35 (−1 per callback) |
| fade, jump | callbacks 1168..1173 | 4641..4646 | 93.10..93.20 | 15:05 | every callback of row 5 sets 48 and decrements: 47 |
| fade, second ramp | callbacks 1174..1220 | 4647..4693 | 93.22..94.14 | 15:06..15:13 | 46 → 0; the main loop leaves after copying the frame with fade 0 (black); part 5 starts in the same refresh 4693 and overwrites that record |

Part length: 1220 callbacks (24.47 s). The capture drops 28 refreshes in this part (section 2.8).

### 2.2 Data

| what | where | format | use |
|---|---|---|---|
| palette | 0x35db0, 768 bytes | 256 x RGB, 6-bit | 0 = black; 1..15 dark reds (1,0,0)..(15,0,5); a ramp through red-pink (32 = 27,5,11; 64 = 45,21,21; 96 = 61,47,35) to white: 141 = (63,62,61), **143..253 and 255 = (63,63,63)**, 254 = (33,33,33) grey (the font's grey) |
| row table | 0x360b0, 200 dwords | y·320 | slow path of the line drawer |
| initial variables | 0x35d3c.. | dwords | [0x35d3c] fade = 64, [0x35d40] edge count = 8, [0x35d44] noise seed = 0, [0x35dac] z offset = 0xb54 (2900), the rest 0 ([0x35d54..5c] = 6, 2, 4 are never referenced) |
| faded palette | 0x35a3c, 768 bytes | buffer | written in order 15 |
| buffers | getmem: draw buffer [0x35d94] (0xfa10 bytes, rounded up to 16), base mesh [0x35d88] (512 x 12), deformed mesh [0x35d8c] (512 x 12), edges [0x35d84] (0x80000 allocated, 992 x 8 used), projected [0x35d90] (512 x 8) | | |
| retrace timer copy | 0x3590c install, 0x35987 calibrate, 0x35960 restore, handler 0x359d8, callback pointer [0x35900] = 0x36af4 | | core scope |

### 2.3 Set-up (0x367e5..0x3693e)

```c
buf = (getmem(0xfa10) | 0xf) + 1;              // 16-byte aligned 64,000-byte draw buffer
// edges 0x3683a: vertex numbers (stored x8, as offsets into the projected array)
e = 0;
for (r = 0, b = 0; r < 32; r++, b = (b + 16) & 0x1ff)          // 512 ring edges, ring by ring
    for (i = 0; i < 16; i++) edge[e++] = { b + i, b + ((i + 1) & 15) };
for (k = 0; k < 16; k++)                                         // 480 meridian edges, column by column
    for (s = 16, j = 0; j < 30; j++, s = (s + 16) & 0x1ff)
        edge[e++] = { k + s, k + s + 16 };                       // ring 1-2, 2-3, .., 30-31 (ring 0 not joined)
// sphere 0x368ba: ring r = 0..31 (latitude 64r of 4096), point k = 0..15 (longitude 256k)
for (r = 0; r < 32; r++) for (k = 0; k < 16; k++)
    base[16r + k] = { (T[256k] * T[64r]) >> 10,  C(64r),  (C(256k) * T[64r]) >> 10 };
DAC[0..255] = palette 0x35db0;
install timer (0x3590c), calibrate (0x35987);   // two refreshes
```

So the base object is a sphere of radius ≈ 1020 (x, z: 1022²/1024; y: 1022) sampled at latitudes φ = r·π/32,
r = 0..31 (0° to 174.4°: ring 0 is the pole at y = +1022, ring 31 a small ring of radius ≈ 100 at y ≈ −1017) and
longitudes θ = k·π/8. Ring 0's 16 edges have length 0 (not drawn) and ring 0 is not joined to ring 1, so the +y
pole is open (ring 1, radius ≈ 100, is the visible top); the 16 meridians converge on ring 31 and make a bright spot
there.

### 2.4 The main loop (0x36943..0x36ae2), once per drawn frame

```c
for (;;) {
    // noise 0x36943
    x = seed;                                   // [0x35d44], 0 at the start, continues frame to frame
    for (i = 0; i < 16000; i++) { ((uint32_t *)buf)[i] = x & 0x0f0f0f0f; x = ror(x + 0x9d, 11) ^ 0x9d; }
    seed = x;                                   // indices 0..15, the first part's generator

    // morph 0x36978 (reads p4c = [0x35d4c], p50 = [0x35d50])
    for (r = 0; r < 32; r++) {
        e = (T[(p4c + 0x180 r) & 0xfff] >> 3) + 0x200;
        for (k = 0; k < 16; k++) {
            s = (T[(p50 + 0x100 k) & 0xfff] >> 4) + e;               // 512 = scale 1
            v[16r + k] = { (base.x * s) >> 9, (base.y * s) >> 9, (base.z * s) >> 9 };    // sar
        }
    }

    // rotate and project 0x366bd (angles from the callback; their sines and cosines are patched into the
    // immediates at 0x36739/0x3673e (a64), 0x3675d/0x36762 (a60), 0x36781/0x36786 (a68))
    for each v:
        x1 = (x  C(a64) - T[a64] z ) >> 10;   z1 = (z  C(a64) + T[a64] x ) >> 10;   // about the object's axis
        y1 = (y  C(a60) - T[a60] z1) >> 10;   z2 = (z1 C(a60) + T[a60] y ) >> 10;   // pitch
        x2 = (x1 C(a68) - T[a68] y1) >> 10;   y2 = (y1 C(a68) + T[a68] x1) >> 10;   // roll (about the view axis)
        z  = z2 + zoff;  if (z <= 0) z = 1;   // never: z >= 551 in this part
        X = (x2 << 8) / z + 160;  Y = (y2 << 8) / z + 100;           // idiv, truncated

    for (i = 0; i < nedge; i++) line(P[edge[i].a], P[edge[i].b]);   // nedge read once, [0x35d40]
    printer(buf);                               // 0x4cacc: its script is at empty lines during this part

    wait until port 3DAh bit 3 set, then until clear;               // the retrace (the callback runs at its start)
    if (order == 15) {                          // 0x36a74
        for (i = 0; i < 768; i++) out[i] = (palette[i] * fade) >> 6; // fade = [0x35d3c], after the callback
        DAC[0..255] = out;
    }
    copy buf to A000:0000 (16,000 movsd);
    if (fade == 0) break;                       // the only exit besides Esc; the order is not polled
    if (inb(0x60) == 1) break;
}
restore timer (0x35960);
```

### 2.5 The callback 0x36af4 (once per refresh) and the closed forms

```c
// 0x36af5..0x36b5b: the no-GUS fake clock (counter byte 0x36b07), core scope
if (order == 15) {
    if (row == 5) fade = 0x30;
    if (fade) fade--;
}
if (nedge < 0x3e0) nedge += 8;
p6c += 14; p70 += 11; p74 += 13; p78 += 6; p7c += 5; p80 += 12;   // all & 0xfff ([0x35d6c..0x35d80])
a60 = ((T[p6c] + T[p70]) >> 2) & 0xfff;
(T[p74] + T[p78]) >> 7;                        // computed and thrown away: p74 / p78 have no effect
a64 = (a64 + 4) & 0xfff;
a68 = ((T[p7c] + T[p80]) >> 2) & 0xfff;
p4c = (p4c + 0x1c) & 0xfff;  p50 = (p50 - 0x19) & 0xfff;
p48 = (p48 + 7) & 0xfff;     zoff = T[p48] + 0xb90;
```

State c (all phases start at 0):

| variable | state c | meaning |
|---|---|---|
| nedge | min(8c + 8, 992) | edges drawn, in the order of 2.3 |
| a64 | 4c mod 4096 | turn about the object's own axis: one turn per 1024 callbacks (20.54 s) |
| a60 | ((T[14c] + T[11c]) >> 2) mod 4096 | pitch, ±511 (±45°): periods 292.6 and 372.4 callbacks |
| a68 | ((T[5c] + T[12c]) >> 2) mod 4096 | roll, ±45°: periods 819.2 and 341.3 callbacks |
| p4c | 28c mod 4096 | phase of the ring wave |
| p50 | −25c mod 4096 | phase of the meridian wave |
| zoff | T[7c] + 2960 for c ≥ 1 (2900 at c = 0, invisible) | distance 1938..3982, period 585.1 callbacks (11.74 s) |
| fade | 64 until order 15; then −1 per callback, set to 47 by every callback of row 5, −1 per callback to 0 | palette scale /64, used in order 15 only |

(T[n·c] means T[(n·c) mod 4096].)

### 2.6 The morph, in continuous terms

Vertex (r, k), r = 0..31, k = 0..15, at callback time c (any real c for the remake):

```
φ = r π/32,  θ = k π/8
P0 = R (sin θ sin φ,  cos φ,  cos θ sin φ),          R = 1022 (x, z: 1022²/1024)
s  = 1 + (1022/4096) sin(6φ + 2π·28c/4096) + (1022/8192) sin(θ − 2π·25c/4096)
P  = s · P0
```

- The ring wave: 0x180 = 384 per ring = 6 φ: three bulges from pole to pole, ±25 % of the radius, travelling from
  the −y end towards the +y pole (ring index decreasing) at 28/384 = 0.073 rings per callback (one period per
  146.3 callbacks, 2.93 s).
- The meridian wave: 0x100 per column = one period around the axis: one side bulges out by 12.5 % and the other in,
  and the bulge turns about the axis by −25/4096 turn per callback (one turn per 163.8 callbacks, 3.29 s).
- So the object is never a fixed shape: one parametric surface whose two waves run continuously; the "tori" at the
  start are the rings drawn before the meridians, the "lobed bodies" are the three bulges seen from changing angles.
  No music position changes the shape.

Then rotate (x about the axis by a64, then pitch a60 about x, then roll a68 about the view axis), z += zoff,
X = 160 + 256 x / z, Y = 100 + 256 y / z in 320x200 pixels (shown at 4:3, so the object is 1.2 times taller than a
square-pixel projection would make it; keep that). The object's edges reach 254 pixels from the centre horizontally
and 259 vertically when it is closest (zoff minimum at c ≈ 439 and 1024: 78.5 s and 90.2 s).

### 2.7 Pixels and colours

index = noise (0..15) + Σ kernel values of all blobs covering the pixel (2-way dword adds, section 1.2). One line
alone peaks at about 17–19 + noise (indices up to ~35: red-pink); where lines cross or bunch up (the silhouette, the
ring 31 spot where 16 meridians meet) the sum climbs into the white end; in a sample of 48 frames (every 25th) the
largest sum per frame was 47..268 (most above 200), and only 2 of those frames had any byte above 255 (1 and 5
pixels): those wrap to dark and carry 1 into the pixel to the right. Sums of exactly 254 show the grey (33,33,33).
Both are 8-bit artefacts: the remake treats every sum ≥ 143 as white.

Palette: written once at the set-up; in order 15 the main loop writes palette · fade >> 6 (integer, per component)
every drawn frame, with the fade after the callback of that refresh. The fade has a visible step: it ramps down from
64/64 to 35/64 over rows 0..4, jumps back to 47/64 for row 5 and then ramps down to black (`sheet.py` labels the
row 0f:05, in hexadecimal). Keep it.

### 2.8 Which record shows which state

The callback runs at the start of the retrace (line 502 of 625) of refresh 3473 + c; the main loop, which finished
drawing before the retrace, waits for the retrace to end, writes the palette (order 15) and copies the buffer.
`full.frames` record F holds what was copied in refresh F (after its retrace; a monitor shows it during refresh
F + 1). The frame copied in refresh F was drawn with state c = F − 3474 and the noise of drawn frame n (0-based,
n = F − 3474 minus the refreshes dropped so far). Its palette (order 15 only) is computed after the retrace wait, so
it shows the fade one callback later: the value after callback F − 3473.

In the capture 28 refreshes have no copy (3543, 3555, 3632, 3638, 3727, 3733, ..., one or two every ~90 refreshes):
the GUS interrupt arrived 0.1..1.9 ms before the retrace and the player's handler waits inside the interrupt for its
volume-ramp interrupt, so the main loop missed the 2-line (64 µs) retrace pulse and waited a whole refresh. That
record repeats the previous one, and the next record shows state F − 3475. Record 3721 is torn (the verification table at the top). This is
a property of the music driver's timing, not of the part; the remake draws every refresh.

### 2.9 Time bases

- Retrace callback: everything that moves (2.5).
- Per drawn frame: the noise only.
- Music: order 15 (the fade starts, the palette is rewritten per frame), order 15 row 5 (the fade jumps to 47), the
  end of the part when the fade reaches 0 (15:13 in the capture). The part's start (order 11) is main's business:
  the third part's loop leaves when order 11 is reached.
- Random: one generator, `x = ror(x + 0x9d, 11) ^ 0x9d`, seed [0x35d44] = 0 (its own, not continued from another
  part), 16,000 steps per drawn frame, bytes masked to 0..15.

---------------------------------------------------------------------------------------------------------------

## 3. The sixth part 0x3c8da: the web and the star (a tunnel)

### 3.1 What is drawn, and when

A tube of 16 rings x 8 points lies along the view axis, its near end behind the camera. The rings move towards the
viewer at a constant speed; a new ring with new radii appears at the far end every 8 refreshes and the nearest one
drops out. Each ring's 8 radii are samples of 8 sines with slightly different frequencies, so the cross-section
changes from ring to ring: 4- and 8-pointed stars, lopsided polygons. Far rings project small and overlap into the
white "star" in the middle; near rings are large (the "web"); edges to vertices behind the camera become spokes that
run off the screen. The tube's direction wobbles (pitch and yaw). Magenta over dark magenta noise (indices 0..31).

"State" = the callback count the record was drawn with (its palette uses the same state, section 3.5).

| what | state | records | time (s) | music | |
|---|---|---|---|---|---|
| set-up | – | 5752..5754 | 115.38..115.42 | 19:00 | part 5's last frame and palette stay; timer calibration (two refreshes); first frame drawn in refresh 5754 |
| fade-in | 0..16 | 5755..5771 | 115.44..115.76 | 19:00..19:02 | palette · c/16 (record 5755 is black) |
| the initial tube | ..121 | ..5876 | ..117.87 | ..19:20 | the history starts with radius 512 everywhere (a narrow round tube); the ring shifts at c = 1, 9, .., 121 replace it ring by ring from the far end |
| the tunnel | 122..1042 | 5877..6797 | | | |
| caption | 854..948 | 6609..6703 | 132.58..134.46 | 22:00..22:15 | "the sea robot of love" by the shared printer (3.6) |
| fade-out | 1043..1058 | 6798..6813 | 136.37..136.67 | 22:32..22:34 | −1 per callback from 16 to 0 |
| black | 1058..1137 | 6813..6893 | ..138.27 | 22:34..22:47 | |
| exit | | 6893 | 138.27 | 23:00 | the frame drawn with state 1137 (over refreshes 6892–6893) is the last; the loop leaves after the copy in which it sees order 23; part 7 starts in refresh 6893 and overwrites that record |

### 3.2 Data

| what | where | format | use |
|---|---|---|---|
| palette | 0x3bb24, 768 bytes | 256 x RGB, 6-bit | 0 = black; 1..31 dark magentas (1,0,1)..(23,6,15); ramp through pink (64 = 50,22,27; 80 = 63,32,36) to white at 142 = (63,63,63); 142..253 and 255 white, 254 = (33,33,33) |
| faded palette | 0x3be28, 768 bytes | buffer | written by 0x3c128, sent to the DAC every frame |
| fade | [0x3be24], dword, 0 | 0..16 | |
| edge list | 0x39c24, 248 x 8 bytes | byte vertex a at +0, byte b at +4 | static: rings i = 0..15: (8i + k, 8i + (k+1) mod 8), k = 0..7 (128 edges), then (8i + k, 8i + 8 + k) for i = 0..14 (120 edges); only vertices 0..127 |
| radius history | 0x39a04, 136 dwords | 16 rings x 8 radii + 8 spare | file: 512 x 128, then 8 x 128 (overwritten before use) |
| row table | 0x3c14e, 200 dwords | y·320 | |
| initial variables | 0x38d1c.. | dwords | noise seed [0x38d1c] = 0x8c25; scroll [0x38db0] = 0; z offset [0x38db8] = 0x400; pitch phases [0x38dc0], [0x38dc4], [0x38dc8] = 0x400; yaw phases [0x38da4] = 0, [0x38da8] = 0, [0x38dac] = 0x400; radius phases [0x38dd0..0x38dec] = 0, 0x800, 0, 0x800, 0, 0x800, 0, 0x800 |
| meshes | 0x38e04 (128 x 12: the tube, rebuilt every frame), 0x39404 (128 x 12: a static ring set, below) | x, y, z dwords | |
| projected | 0x3a3e4, 256 x 12 | X, Y, Y | |
| buffer | getmem(0xfa00) → [0x38e00] | | draw buffer |
| retrace timer copy | 0x38bec install, 0x38c67 calibrate, 0x38c40 restore, handler 0x38cb8, callback pointer [0x38be0] = 0x3cad3 | | core scope |

The set-up also builds a second set of 16 rings x 8 points at 0x39404 (0x3c8ef: radius 5·T = 5110, angles 0x100 +
0x200 k, y = −0xc00 + 0x800 i). 0x3c7b5 projects these 128 vertices every frame too, but no edge uses them: dead,
the remake skips them.

### 3.3 The main loop (0x3c94b..0x3cac1), once per drawn frame

```c
for (;;) {
    scroll_f = scroll;                                   // [0x38db4] = [0x38db0]
    // tube 0x3c955: ring i = 0..15, point k = 0..7
    for (i = 0; i < 16; i++) for (k = 0; k < 8; k++) {
        a = 0x200 k;  r = hist[8i + k];
        v[8i + k] = { (T[a] * r) >> 8,  -0x800 + 0x400 i,  (r * C(a)) >> 8 };
    }
    // noise 0x3c9b4: a different generator; the seed accumulates every value
    x = seed;
    for (i = 0; i < 16000; i++) {
        ((uint32_t *)buf)[i] = x & 0x1f1f1f1f;           // indices 0..31
        x = rol(x + 0x9d, 11) ^ 0x9d;
        seed += x;                                       // [0x38d1c]: next frame starts from the sum
    }
    // angles 0x3c9e7 from the callback's phases
    A = (((((T[dc0] >> 1) + T[dc4]) >> 1) + T[dc8]) >> 2) + 0x400) & 0xfff;    // [0x38dbc]
    B = ((((T[da4] >> 1) + T[da8]) >> 1) + T[dac]) >> 2) & 0xfff;             // [0x38dcc]

    // 0x3c7b5: project all 256 vertices (the 128 at 0x39404 for nothing), then the 248 edges
    for each v:
        y += scroll_f;
        y1 = (y  C(A) - T[A] z ) >> 10;   z1 = (z  C(A) + T[A] y ) >> 10;     // pitch: A ≈ 90° turns the
        x1 = (x  C(B) - T[B] z1) >> 10;   z2 = (z1 C(B) + T[B] x ) >> 10;     // tube axis into the view axis
        z = z2 + 0x400;
        if (z > 0) { X = (x1 << 6) / z;  Y = (y1 << 6) / z; }              // idiv, truncated
        else       { X = x1 * (1 - z);   Y = y1 * (1 - z); }                // behind the camera: thrown far
        X += 160; Y += 100;                                                 // out along (x1, y1)
    for (e = 0; e < 248; e++) line(P[edge[e].a], P[edge[e].b]);          // 0x3c46e
    printer(buf);                                         // 0x4cacc
    for (i = 0; i < 768; i++) faded[i] = (palette[i] * fade) >> 4;        // 0x3c128, before the retrace wait
    wait until port 3DAh bit 3 set, then until clear;
    DAC[0..255] = faded;
    copy buf to A000:0000;
    if (order == 23) break;
    if (inb(0x60) == 1) break;
}
restore timer (0x38c40);
```

### 3.4 The callback 0x3cad3 (once per refresh) and the closed forms

```c
// 0x3cad4..0x3cb3a: the no-GUS fake clock (counter byte 0x3cae6), core scope
if (order != 22)         { if (fade != 16) fade++; }
else if (row >= 32)      { if (fade != 0)  fade--; }
scroll = (scroll - 0x80) & 0x3ff;
if (scroll == 0x380) {                                    // callbacks 1, 9, 17, ...
    for (k = 0; k < 8; k++) hist[128 + k] = (T[ph[k]] >> 1) + 0x400;     // the new ring, 513..1535
    memmove(&hist[0], &hist[8], 128 * 4);                 // every ring moves one down; ring 15 = the new one
}
ph[k] = (ph[k] + {0x29, 0x2b, 0x2c, 0x29, 0x2d, 0x2c, 0x2a, 0x2b}[k]) & 0xfff;    // [0x38dd0..0x38dec]
dc0 += 0x10; dc4 -= 6; dc8 += 0x17;                       // & 0xfff
da4 -= 0xd;  da8 += 0x13; dac += 8;                       // & 0xfff
```

State c:

| variable | state c |
|---|---|
| scroll | −128c mod 1024 |
| rings created | m = ceil(c / 8); ring s (s = 1..m) is created by callback 8s − 7 with radii r_k(s) = 1024 + (T[(ph0_k + inc_k · 8(s − 1)) mod 4096] >> 1), ph0 = (0, 2048, 0, 2048, 0, 2048, 0, 2048), inc = (41, 43, 44, 41, 45, 44, 42, 43) |
| history | ring i (0..15) holds ring s = m − 15 + i, or radius 512 if s < 1 |
| A | (1024 + ((((T[1024 + 16c] >> 1) + T[1024 − 6c]) >> 1) + T[1024 + 23c]) >> 2)) mod 4096 |
| B | ((((T[−13c] >> 1) + T[19c]) >> 1) + T[1024 + 8c]) >> 2) mod 4096 |
| fade | min(c, 16) until order 22 row 32; then −1 per callback to 0. Once order 23 is reached the callbacks raise it again (1 after callback 1138), so the last frame, drawn over two refreshes, may go out at 1/16 brightness for one refresh; part 7 overwrites that record in the capture |

The ring positions in continuous form: ring s sits at y = 13312 + 1024 s − 128 c (it appears at y = 14208 and
moves 128 units per callback, 6381 per second, towards −y); it is dropped when it would pass y = −2048 + scroll,
i.e. the tube always covers y ∈ (−2048, 14208] in steps of 1024: exactly 16 rings, one appearing at the far end
and one vanishing at the near end at the same callback. With A near 90° the depth is about y + 1024: ring 0 is
always behind the camera, ring 1 just in front (projected far off the screen), ring 3 is the large visible ring of
the web, ring 15 projects to about 18 pixels (x r/1024) inside the white star. So the ring that vanishes and its
edges are far outside the screen, and the new ring appears about 1.5 pixels inside the previous one, within the
star: the steps should not be visible (from the geometry; not checked frame by frame).

Radius phases: neighbouring rings differ by 8·inc ≈ 328..360 units (29°..32°); the points 0/2/4/6 start in phase
and 1/3/5/7 opposite (ph0), so the first rings are 4-pointed stars (long and short points alternating); since the
frequencies differ by up to 4 per callback, the relative phases drift (period 1024 callbacks for the extremes 0x29
and 0x2d) and the cross-sections wander through stars and lopsided polygons.

Angles: A = 1024 ± (T/16 + T/8 + T/4) — the tube axis is pitched 90° (into the view direction) ± 39° (three sines:
amplitudes 64, 128, 256 units, periods 256, 682.7 and 178.1 callbacks); B = yaw ± 39° (amplitudes 64, 128, 256;
periods 315.1, 215.6, 512 callbacks).

Geometry in continuous terms: point k of ring s at angle 2πk/8, radius ≈ 4·r_k (x = T·r/256), at axial position y;
rotate (y, z) by A, then (x, z) by B, depth z + 1024, screen X = 160 + 64 x / z, Y = 100 + 64 y / z when z > 0, and
X = 160 + x (1 − z), Y = 100 + y (1 − z) when z ≤ 0. Keep this second branch: it is what makes the spokes (the edges
from the visible rings to the ring behind the camera run outward along the direction of the far vertex's (x, y)).
In floats, z in (0, 1) gives a scale above 64, z ≤ 0 a scale of at least 1; both put the vertex thousands of pixels
out, so the spoke direction is continuous across z = 0.

### 3.5 Pixels and colours

index = noise (0..31) + Σ kernel values (sixth-part kernel). One line peaks at about 18 + noise (≤ 49); the star in
the middle and the crossings go white. Largest sum per frame in a sample of 45 frames (every 25th): 133..251, never
above 255 (no wrap, no carry); the grey 254 is possible but rare. Treat ≥ 142 as white.

The palette is rewritten every drawn frame as palette · fade >> 4 (integer), with the fade as it was before the
retrace wait (the callback of the refresh in which the frame was drawn). Fade-in over the first 16 callbacks,
fade-out over 16 callbacks from order 22 row 32, black until order 23.

### 3.6 The caption (shared printer 0x4cacc, core scope)

The main loop calls the printer after the lines, so its text is opaque over everything. In this part its script
(0x4c8e8) reaches "the sea robot of love": centred (x = 160 − 200/2 = 60 .. 259), top row 162, 38 rows (to 199),
glyph pixels 7, 216 and 254 written where the font byte is not 0. The printer moves to its next line when it sees
the music position change (order, or bit 4 of the row), and it sees it while drawing, so a line appears one drawn
frame after the change: in the capture the caption is in exactly the frames whose drawing started at 22:00..22:15
(records 6609..6703, 93 drawn frames), `common.caption` + `verify_star.py`.

### 3.7 Which record shows which state

The same order as the fourth part: callback c runs at the retrace of refresh 5754 + c; record F (copied in refresh
F) shows state c = F − 5755 and the noise of drawn frame n = c minus the refreshes dropped so far (21 up to record 6891,
for the same reason as in 2.8).

### 3.8 Time bases

- Retrace callback: scroll, ring creation (every 8th callback), radii phases, angle phases, fade.
- Main loop: A and B are computed per drawn frame, but only from callback state: no frame-rate dependence.
- Per drawn frame: the noise (its seed is the running sum of the generator's values).
- Music: fade-out at order 22 row 32, exit at order 23, the caption by the printer at 22:00..22:15.
- Random: `x = rol(x + 0x9d, 11) ^ 0x9d` (rol, not ror as in the fourth part), started each frame from the seed
  [0x38d1c] (0x8c25 in the file, the part's own), seed += x for each of the 16,000 steps; bytes masked to 0..31.

---------------------------------------------------------------------------------------------------------------

## 4. Rendering shortcuts the remake evaluates exactly

| original | what it approximates | remake |
|---|---|---|
| 4096-entry sine table, amplitude 1022, fixed point >> 8 / 9 / 10 | sin / cos and real products | float sin / cos (×1022/1024 if wanted) |
| integer perspective division, truncated | x / z | float |
| blobs every ~2 px along the major axis, 8.8 steps truncated | a glowing line: the line integral of the kernel | continuous line integral of the bilinear kernel (8 x 8 original pixels), density max(|cos α|, |sin α|)/2 per original pixel, centred at +3.5 px |
| index = noise + Σ kernels, 8-bit with dword carries | an intensity | float sum, palette ramp interpolated, clamp at the white entry (143 / 142); no wrap, no grey 254 |
| whole-blob clipping to top-left ∈ [0,311] x [0,191] | lines clipped at the screen edges | clip at the edges |
| `dec cl` in the slow path (n mod 256 blobs) | the whole line | the whole line (sixth part: occasionally a spoke stopped short for a frame in the original) |
| per-pixel noise at 320x200, new per drawn frame | grain | uniform noise of the same index range per displayed frame |
| 32 x 16 sphere, 16 x 8 tube | the surfaces | keep the same vertices and edges: they are the wireframe (more segments would change the picture) |

## 5. Not determined / guesses

- Whether the fade jump at 15:05 in the fourth part was intended (probably meant "start the fade at row 5"; the
  code decrements from the first callback of order 15). It is what the original shows.
- The dead code (the (T[p74] + T[p78]) >> 7 sum in the fourth part's callback, the static ring set at 0x39404 in the
  sixth part) suggests earlier versions of both effects; nothing visible depends on it.
- The refresh drops and the torn record are properties of this capture (100 M instructions per second, the GUS
  driver waiting inside its interrupt); a real machine drops differently.

## 6. Models (`models/morph-star/`)

| file | what |
|---|---|
| `common.py` | program image access, the sine table rebuilt from the file, the line drawer's blob positions (`line_positions`, both copies), the exact blob adds with carries (`add_blobs`), the caption (`caption`, a port of what 0x4cacc draws for one line), capture access |
| `morph.py` | the fourth part: edges, sphere, closed-form state from c, noise, morph, projection, frame(c, seed) |
| `star.py` | the sixth part: static data from the file, state from c (history replayed), tube, projection, noise, frame(c, seed) |
| `hookrun.py` | runs `run_okean.py` (a copy) from a part's snapshot and logs every callback, main-loop start and copy with the variables (`<out>_hooks.log`), optionally the draw buffer at chosen copies |
| `hooks.py` | reads that log: record → (callback count, drawn frame number, variables) |
| `verify_morph.py`, `verify_star.py` | compare the models with every record of `full.frames` (pixels and DAC) |
| `stats.py`, `trunc.py` | index statistics per frame (largest sums, wraps); effect of the 8-bit step counter |

```
cd okean/re/models/morph-star
toolbox run -c acme303 python3 hookrun.py --load ../../snaps/part_367e5.snap --out p4 --frames 4700 --part 4
toolbox run -c acme303 python3 hookrun.py --load ../../snaps/part_3c8da.snap --out p6 --frames 6900 --part 6
toolbox run -c acme303 python3 verify_morph.py      # 1191 records: all equal except the torn 3721
toolbox run -c acme303 python3 verify_star.py       # 1116 records (93 with the caption): all equal
```
