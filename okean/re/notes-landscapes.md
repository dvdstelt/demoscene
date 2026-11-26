# Scope "landscapes": the green canyon (0x3554b) and the blue landscape (0x38469, 0x38520)

Routines: third part 0x3554b (draw 0x353be, line drawer 0x350c8, callback 0x357b3, timer copy 0x2edec..0x2ef1b);
set-up 0x38469 (terrain generator 0x374da, smoothing 0x377b2), called by main before everything else; fifth part
0x38520 (projection 0x38331, line drawer 0x3801c, callback 0x38985, timer copy 0x36cfc..0x36e2b). Addresses are
code32 offsets (file offset = 0xec0 + address, linear in the headless run = 0x8fc0 + address). "Record k" = record k
of `full.frames` (refresh k, 49.851 Hz); "callback n" = the n-th call of the part's retrace callback; `S[i]` = the
sine table at 0x24a3c (section 0.1). Screen coordinates are original 320 x 200 pixels.

Verified against the capture (scripts in `models/landscapes/`, see its README):
- part 3: a Python model run from `snaps/part_3554b.snap` plus the callback count renders **1112 of the 1113 drawn
  frames equal to the capture in all 64,000 indices**, and the palette of every one of them; the 1113th (record 3269)
  differs in rows 189..199 only, because the capture tore there (the emulator recorded while the copy to video memory
  was interrupted by the GUS interrupt), not because of the model;
- set-up 0x38469: the model of 0x374da + 0x377b2 gives the 256 x 256 height map of `snaps/part_2c26f.snap` in all
  65,536 bytes, and the generator's final random state;
- part 5: the model run from `snaps/part_38520.snap` renders **all 1047 drawn frames equal to the capture**, the
  captions included; the palette of every record agrees (records that repeat a drawn frame keep the previous palette,
  as the program does).

-----------------------------------------------------------------------------------------------------------------

## 0. What both parts share

### 0.1 The sine table 0x24a3c

`S[i]`, 5121 dwords at 0x24a3c. Only `S[0..1024]` is in the file (1025 dwords, within 1 of floor(1023 sin(2πi/4096)),
peak 1022); the first part's set-up 0x2c26f builds the rest before any of these parts runs:
`S[1025 + i] = S[1024 - i]` (i < 1024), `S[2049 + i] = -S[i]` (i < 2048), `S[4097 + i] = S[i]` (i < 1024).
Both parts read sine as `S[a]` and cosine as `S[a + 1025]` (the address 0x25a40 = 0x24a3c + 4·1025), with
`a` in 0..4095. So the cosine is exact (`S[a + 1025] ≈ 1023 cos(2πa/4096)`), and the sine of `a ≥ 1025` lags one
entry (`S[a] ≈ 1023 sin(2π(a − 1)/4096)`). Products are shifted right by 10, so every rotation also scales by
about 1023/1024. A remake uses float sin/cos (angle unit 2π/4096).

### 0.2 The noise

Both main loops start a frame by filling the whole 64,000-byte buffer with noise, 16,000 dwords:

```c
for (i = 0; i < 16000; i++) { buf32[i] = x & 0x0f0f0f0f; x = ror32(x + 0x9d, 11) ^ 0x9d; }
```

The generator is the title part's (`ror(x + 0x9d, 11) ^ 0x9d`) with its own state: part 3 `[0x2f224]` (0 in the
file, nothing else changes it before the part), part 5 `[0x37130]`, which is also the terrain generator's state
(section 2): part 5's noise continues from the value the set-up 0x38469 left, 0xd21f2bdb. The state advances
16,000 steps per **drawn frame** and is saved back after the fill. Every pixel is the low nibble of one byte of the
state: index 0..15, about uniform (≈ 4,000 pixels of each value per frame); a new pattern every drawn frame.

### 0.3 The glow blob and the line drawers 0x350c8 / 0x3801c

Both parts draw a line as a row of additive 8 x 8 blobs placed every ~2 pixels. The blob, added with eight pairs of
32-bit `add dword` (`[edi + 0x140·r]` and `[edi + 0x140·r + 4]`), is

```
row 0:  0  1  1  1  1  1  1  0
row 1:  1  1  2  2  2  2  1  1
row 2:  1  2  4  8  8  4  2  1
row 3:  1  2  8 15 15  8  2  1
row 4:  1  2  8 15 15  8  2  1
row 5:  1  2  4  8  8  4  2  1
row 6:  1  1  2  2  2  2  1  1
row 7:  0  1  1  1  1  1  1  0
```

(constants 0x01010100/0x00010101, 0x02020101/0x01010202, 0x08040201/0x01020408, 0x0f080201/0x0102080f, then the
same mirrored). Its top-left corner is the line point, so the glow is centred **3.5 pixels right of and below** the
mathematical line. Sum 200; column sums 6 12 30 52 52 30 12 6, so the profile across a line has σ ≈ 1.51 pixels. The
adds are 32-bit: a byte that passes 255 wraps and carries into the next byte to the right. This never happens in the
capture (largest index seen: 236 in part 3, 143 in part 5), so in practice the index is a plain sum.

The line routine (0x350c8 in part 3; 0x3801c in part 5 is the same with the floating horizon of section 3.5). The
endpoints are 32-bit screen coordinates (`x1, y1, x2, y2` in `[0x2ff8c] [0x2ff90] [0x2ff94] [0x2ff98]`, part 5
`[0x371bc] [0x371c4] [0x371c0] [0x371c8]`):

```c
if (!(y1 > 0) && y2 < 0) return;                       /* both above the screen (signed) */
if (!((uint32)y1 < (uint32)y2)) swap(p1, p2);          /* UNSIGNED compare: a negative y counts as huge, so
                                                          after this y1 >= 0 always (the y1 < 0 clip at 0x3516e /
                                                          0x380c2 is never taken) */
ax = x2 - x1;  ax = ax >= 0 ? ax + 1 : -ax;            /* cdq; xor eax, edx; inc eax */
ay = y2 - y1;  ay = ay >= 0 ? ay + 1 : -ay;
n = max_u(ax, ay) >> 1;  if (n == 0) return;           /* about one point per 2 pixels of the major axis */
sx = idiv(edx:eax = sign(x2-x1):(uint32)((x2-x1) << 8), n);   /* cdq BEFORE shl: steps in 24.8 */
sy = idiv(... (y2-y1) << 8 ..., n);
X = x1 << 8;  Y = y1 << 8;                             /* 24.8 */
if (0 <= y1,y2 <= 191 && 0 <= x1,x2 <= 311) {         /* fast path: no per-point clip */
    repeat n times (part 3: dec ecx; part 5: dec cl): blob(X >> 8, Y >> 8); X += sx; Y += sy;
} else {                                                /* clipped path */
    repeat (n & 0xff) times, 256 if that is 0 (dec cl):
        if (0 <= Y >> 8 <= 191 && 0 <= X >> 8 <= 311) blob(X >> 8, Y >> 8);   /* sar; js / ja 0xbf, 0x137 */
        X += sx; Y += sy;
}
```

The points are `p1 + i·(p2 − p1)/n`, i = 0..n−1: the last point stops one step short of `p2`. A point is drawn only if
its blob lies in x 0..318, y 0..198: column 319 and row 199 never get glow, and lines vanish 8 pixels before the
right and bottom edges. Row addresses come from a table of `y·320` (part 3 0x34da8, part 5 0x377fc, 200 dwords,
static). Part 3's fast path computes `y·320` with `imul`. Each part has its own copy (0x350c8 is called only from
0x3553c, 0x3801c only from 0x388e6); the same blob code appears a third time in the seventh part's line drawer
(0x53104..0x531f1, sheet scope).

### 0.4 Timer copies, callbacks, records

Both parts have a copy of the first part's retrace timer (same instructions, own addresses; core scope):

| | install | restore | measure | IRQ 0 handler | real-mode stub | count word | callback pointer | callback |
|---|---|---|---|---|---|---|---|---|
| part 3 | 0x2edec | 0x2ee40 | 0x2ee67 | 0x2eeb8 | 0x2eef4 | 0x2ef0d | [0x2ede0] | 0x357b3 |
| part 5 | 0x36cfc | 0x36d50 | 0x36d77 | 0x36dc8 | 0x36e04 | 0x36e1d | [0x36cf0] | 0x38985 |

Both callbacks start with the no-GUS fake music clock (`[0x142d6] == 1`; part 3 counts the row with `inc word`,
part 5 with `inc byte [0x12b73]`; the core scope describes it). With a GUS they only do what sections 1.3 and 3.3
say.

Each main loop: draw into the buffer, wait for the retrace start and end, (palette), copy 64,000 bytes to video
memory, check the order. The callback runs at the retrace start (just before the main loop sees it), so the frame
copied in refresh k's vertical blank was drawn during refresh k − 1, after refresh k − 1's callback. In the capture
(record k = video memory at the end of refresh k):

- part 3: record k shows the state after **n = k − 2334** callbacks (first callback in refresh 2334; drawn frame 0,
  n = 0, is record 2334);
- part 5: record k shows **n = k − 4696** (first callback in refresh 4696; drawn frame 0, n = 0, is record 4696).

This holds wherever every refresh is drawn. At 100 M instructions per second the emulated PC sometimes needs two
refreshes for a frame (24 times in part 3, 9 times in part 5): that record repeats the previous frame and the next
one jumps two callbacks. `framemap.py` finds the exact mapping from the noise. A remake draws every refresh.

-----------------------------------------------------------------------------------------------------------------

## 1. Third part 0x3554b: the green canyon (records 2331..3470, music 7:00..10:47)

### 1.1 What it draws

A 16 x 16-vertex wireframe of a canyon floor with two walls, glowing yellow-green over dark green noise. The camera
flies along the canyon (32 units per callback), swaying sideways, turning left and right and banking into the turns.
The grid slides in from behind the camera in the first 64 callbacks. From 10:16 the palette cross-fades from green
to red over 128 callbacks and the grid flies past the camera and away, leaving red noise.

| records | music | callbacks n | on screen |
|---|---|---|---|
| 2331..2333 | 7:00 | – | the second part's last picture, now through the green palette (the set-up writes the DAC at once and does not clear video memory) |
| 2334..~2400 | 7:00.. | 0..64 | the grid slides in from behind the camera (`slide`) |
| ..3281 | ..10:15 | ..947 | the canyon flight |
| 3282..3408 | 10:16..10:37 | 948..1074 | palette green → red; the grid moves on past the camera without new rows: only noise from record 3394 |
| 3409..3470 | 10:37..10:47 | 1075..1136 | red noise (palette fully red) |

Every frame overwrites the whole buffer (the noise). The text printer 0x4cacc is called every frame (pages "\r@",
empty: it draws nothing, but its page pointer advances with the music; core scope).

### 1.2 Data

| what | where | format |
|---|---|---|
| palette green | 0x347a8, 768 bytes | 6-bit RGB; 0 black, 1..15 dark grey-green (the noise), 16..158 rising through green and yellow, 159..253 and 255 white (63,63,63), 254 grey (33,33,33) |
| palette red | 0x34aa8, 768 bytes | 0 black, 1..15 dark red, rising through red and orange to white at 143..255 |
| working palette | 0x2ef1c, 768 bytes | copy of green at set-up; the mix during the cross-fade |
| map | 0x2f2b0, 3,200 bytes | 200 rows x 16 bytes, static: heights 0..255; columns 1..14 form a U-shaped valley (lowest in the middle columns); columns 0 and 15 (239..255, 163..255) are not used: the walls are fixed (below) |
| vertices | 0x2ffa8, 256 x 12 bytes | (x, y, z) dwords, static x, y: vertex k = 16c + r (c, r = 0..15): x = −2048 + 256c, y = −512 + 256r; z written every frame |
| edges | 0x30ba8, 480 x 8 bytes | vertex indices at byte +0 and +4: 240 edges (16c+r, 16c+r+1) along y, then 240 edges (16c+r, 16c+r+16) across |
| projected | 0x31aa8, 256 x 12 bytes | (sx, sy, sy) per vertex |
| row table | 0x34da8, 200 dwords | y·320 |
| draw buffer | `[0x2ff9c]` = getmem(0xfa00) | 320 x 200 indices |
| constants | `[0x2ff48]` = 1024 (eye distance), `[0x2ff50]` = 256 (pitch, 22.5°), `[0x2ff54]` = 1024 (90°) | file, never written |
| state | `[0x2ff30]` a, `[0x2ff34]` b, `[0x2ff38]` scroll, `[0x2ff40]` slide = −4096, `[0x2f2ac]` row = 0, `[0x2f21c]` w1 = 128, `[0x2f220]` w2 = 0, `[0x2f224]` noise = 0, history 0x2f228..0x2f2a8 (33 dwords, 0), `[0x2ff58]` yaw = 0 | initial values from the file |

Whether the canyon map is hand-made or generated offline cannot be told; it is static data.

### 1.3 Set-up and callback

Set-up (0x3554b..0x3558b): `push [8]`; copy green to 0x2ef1c; buffer = getmem(0xfa00); write green to the DAC;
install the timer and measure one frame (0x2edec, 0x2ee67).

Callback 0x357b3, per refresh (`order` = `[0x12b75]`, `row` = word `[0x12b73]` at that moment):

```c
a = (a + 8) & 0xfff;  b = (b + 0x13) & 0xfff;
scroll -= 0x20;
if (order == 10 && row >= 16) { if (w1) { w1--; w2++; } }   /* cross-fade, 128 callbacks */
else scroll &= 0xff;
if (scroll == 0xe0) { row_ofs += 16; if (row_ofs >= 0xc80) row_ofs -= 0xc80; if (row_ofs < 0) row_ofs += 0xc80; }
if (order < 10 && slide != 0) slide += 0x40;
```

Closed forms after n callbacks (N1 = the first callback with the music at 10:16 or later, 948 in the capture):
- `a = 8n mod 4096`, `b = 19n mod 4096` (periods 512 and 215.6 callbacks);
- n < N1: `scroll = (−32n) mod 256`, `row_ofs = 16·ceil(n/8) mod 3200`; together map row v lies at
  `y = −512 + 256 (v − R) + scroll` with R = ceil(n/8) mod 200, which is `y = −512 + 256v − 32n` (map rows taken
  mod 200): the camera moves 32 units (1/8 row) per callback, a new row every 8 callbacks, the 200-row map repeats
  every 1600 callbacks (the part uses rows 0..134);
- n ≥ N1: `scroll` no longer wraps (it goes on −32 per callback from its value at N1 − 1, never equals 0xe0 again),
  `row_ofs` stays: the 16 x 16 window keeps moving towards −y and passes the camera;
- `slide = −4096 + 64·min(n, 64)`;
- `w1 = 128 − clamp(n − N1 + 1, 0, 128)`, `w2 = 128 − w1`.

### 1.4 Main loop (0x35590..0x357a7) and the frame (0x353be)

```c
for (;;) {
    scr = scroll;  sl = slide;                                       /* [0x2ff3c], [0x2ff44] */
    for (c = 0; c < 16; c++) for (r = 0; r < 16; r++)                /* 0x355a4 */
        V[16c + r].z = (c == 0 || c == 15) ? 0x800
                     : map[(row_ofs + 16r + c) mod 0xc80] * 8 - 0x400;   /* walls 2048, floor -1024..1016 */
    noise();                                                          /* 0.2 */
    hist[32] = yaw;  memmove(hist, hist + 1, 32 * 4);                 /* hist[0] = yaw of 32 drawn frames ago */
    s = S[a] + S[b];
    sway = (S[(a + 0xc00) & 0xfff] + S[(b + 0xc00) & 0xfff]) >> 1;   /* [0x2ff4c], about -(cos a + cos b)*511 */
    yaw  = (s >> 2) & 0xfff;                                          /* [0x2ff58], +-511 = +-45 deg */
    roll = (S[(hist[0] - yaw) & 0xfff] >> 1) & 0xfff;                 /* [0x2ff5c], +-511 */
    frame();                                                          /* 0x353be */
    text_printer(buf);                                                /* 0x4cacc: nothing on these pages */
    if (order == 10 && row >= 16)                                     /* 0x356e8 */
        for (i = 0; i < 768; i++) pal[i] = (green[i] * w1 + red[i] * w2) >> 7;
    wait_retrace_start(); wait_retrace_end();
    if (order == 10 && row >= 16) write_dac(pal);                     /* 0x35748 */
    copy(buf, video);
    if (order == 11 || inport(0x60) == 1) break;
}
```

`frame()` (0x353be), for each of the 256 vertices (`R(t, p, q)`: `p' = (p·cos t − sin t·q) >> 10`,
`q' = (q·cos t + sin t·p) >> 10`, with `sin t = S[t]`, `cos t = S[t + 1025]`, 32-bit `imul`, `sar`):

```c
x = V.x;  y = V.y + scr + sl;  z = V.z;
(y, z) = R(1024, y, z);          /* 90 deg: the map's y becomes depth, its height becomes -y (S[2049] = 0) */
(x, y) = R(roll, x, y);          /* bank */
x -= sway;
(x, z) = R(yaw, x, z);           /* heading */
(y, z) = R(256, y, z);           /* look down 22.5 deg */
z += 1024;
if (z > 0) { sx = idiv(x << 8, z); sy = idiv(y << 8, z); }     /* cdq before shl (0.3) */
else       { sx = x * (1 - z);  sy = y * (1 - z); }             /* behind the eye: pushed far away */
P.sx = sx + 160;  P.sy = sy + 100;
```

then `line(P[a], P[b])` for the 480 edges in table order (no hidden-line removal: the glows simply add).

The roll is a bank into the turn: the sine of how much the heading changed over the last 32 drawn frames.

### 1.5 Pixel index and palette

`index = noise (0..15) + Σ blob values` (0.3). The palettes are intensity ramps: 0..15 dark (noise), then brighter
with the glow, white from 159 (green) / 143 (red). Before 10:16 the DAC holds the green palette, set once at the
set-up. From 10:16 every drawn frame writes `(green·w1 + red·w2) >> 7` in the retrace.

### 1.6 Time bases and music events

- Retrace callback: `a`, `b`, `scroll`, `row_ofs`, `slide`, `w1/w2` (closed forms in 1.3).
- Per drawn frame: the noise (16,000 steps) and the yaw history: the roll uses the yaw of **32 drawn frames ago**.
  In the capture that is 32 refreshes (0.64 s). On a machine that draws every second refresh it is 1.28 s, and the
  bank is larger and later. A remake: `roll(n) = (S[(yaw(n − 32) − yaw(n)) & 0xfff] >> 1) & 0xfff` with
  `yaw(m) = ((S[8m & 0xfff] + S[19m & 0xfff]) >> 2) & 0xfff` and `yaw(m < 0) = 0` (the history starts at 0, so the
  first 32 frames bank towards the start heading 0).
- Music: 10:16 (first callback with order 10 and row ≥ 16; record 3281/3282 in the capture) starts the cross-fade
  and lets the grid fly away; order 11 ends the part (after the copy of the frame in which the main loop sees it;
  part 4's first record is 3471). The slide is limited to `order < 10`, which never matters (it ends at n = 64).

### 1.7 Shortcuts a remake evaluates exactly

- `S[]` lookups, `>> 10` rotations, integer `/z` projection → float trigonometry and projection.
- The line as 8 x 8 blobs every ~2 pixels of the major axis, anchored at the top-left → a continuous glow stroke
  with the blob's profile (σ ≈ 1.51 px, peak 15 per blob; dots spaced 2 px along the major axis, so a line's glow
  per unit length scales with cos of its angle to the nearest axis: diagonals carry 1/√2 of the glow of axis lines),
  centred 3.5 px right/down of the line. The clip of the points to x ≤ 311, y ≤ 191 (lines vanish 8 px before the
  right and bottom edge) is a 320 x 200 artefact.
- The index sum → a continuous intensity looked up in the palette ramp (no wrap: it never happens).
- Noise: a new pattern of 16 equally likely levels (indices 0..15) every frame.

### 1.8 Not determined

- How the record 3269 would have looked on hardware (it shows the capture's tear, section 1 intro).
- The canyon map's origin (static 3,200 bytes).

-----------------------------------------------------------------------------------------------------------------

## 2. Set-up 0x38469: the height map of the fifth part (records 0..2, before the first part)

main calls 0x38469 first of all. It makes a 256 x 256 height map and the fifth part's draw buffer, both kept for
the whole run (allocated before any part's `push [8]`).

```c
p = getmem(0x20000);  map = (p | 0xffff) + 1;   /* 64 KB aligned: [0x37148] = 0x110000 in the run */
buf = getmem(0x10000);                           /* [0x371b4] = 0x12fe70: part 5's draw buffer */
memset(map, 0, 0x10000);                         /* twice */
patch(map);                                      /* self-modifying code, below */
y0 = 0; y1 = 0x10000;  (x0, x1) = (0, 0x100);    /* [0x371c4], [0x371c8], eax, ecx */
subdivide(x0, x1, y0, y1);                       /* 0x374da */
smooth();                                        /* 0x377b2 */
```

**Self-modifying code.** The map is addressed as `[ebx + disp32]` with `bx` = 16-bit index (row in `bh`, column in
`bl`), so it wraps in both directions. The 13 `disp32` fields hold 0x12345678 in the file and get the map address:
the instructions at 0x3751a, 0x37528, 0x3753a, 0x37548, 0x37559, 0x375c4 (store), 0x375cc, 0x37637 (store),
0x37646, 0x376b1 (store), 0x376bd, 0x37728 (store), 0x37750 (store), fields at +2: 0x3751c, 0x3752a, 0x3753c,
0x3754a, 0x3755b, 0x375c6, 0x375ce, 0x37639, 0x37648, 0x376b3, 0x376bf, 0x3772a, 0x37752. A remake indexes a
256 x 256 array with wrap-around instead.

**0x374da, midpoint displacement** (recursive; coordinates: columns `x0, x1` plain, rows `y0, y1` stored ×256 in
`[0x371c4]`, `[0x371c8]`; `m(r, c)` = `map[(r & 255) · 256 + (c & 255)]`; random state `[0x37130]`, 0 in the file):

```c
rnd() { s = ror32(s + 0x3b76, 13) ^ 0x2cfb; return s; }
mid(a, b, size) {                                   /* an edge midpoint */
    d = ((rnd() & 0xff) - 0x80) * size >> 5;        /* imul by [0x37144] = size, sar 5: up to +-4*size */
    v = ((a + b) >> 1) - d;
    return v <= 30 ? 30 : v >= 225 ? 225 : v;
}
subdivide(x0, x1, y0, y1) {
    size = x1 - x0;  h = size >> 1;  if (h == 0) return;
    xm = x0 + h;  ym = y0 + (y1 - y0) / 2;
    c00 = m(y0,x0); c10 = m(y0,x1); c11 = m(y1,x1); c01 = m(y1,x0);
    if (m(ym, x0) == 0) m(ym, x0) = mid(c00, c01, size);   /* left   */
    if (m(ym, x1) == 0) m(ym, x1) = mid(c10, c11, size);   /* right  */
    if (m(y0, xm) == 0) m(y0, xm) = mid(c00, c10, size);   /* top    */
    if (m(y1, xm) == 0) m(y1, xm) = mid(c01, c11, size);   /* bottom */
    m(ym, xm) = (c00 + c10 + c01 + c11) >> 2;              /* centre: no displacement, always written */
    subdivide(x0, xm, y0, ym);  subdivide(x0, xm, ym, y1); /* order: top-left, bottom-left, */
    subdivide(xm, x1, ym, y1);  subdivide(xm, x1, y0, ym); /* bottom-right, top-right */
}
```

"0 = not yet set" works because displaced values are ≥ 30. All four corners of the whole map are the cell (0, 0),
which is never set (stays 0), and the centre (128, 128) is their average, 0: the raw map has these two zero cells.
The wrap makes the right/bottom midpoints the same cells as the left/top ones, so the map tiles. The generator calls
`rnd()` 43,690 times and leaves `[0x37130]` = 0xd21f2bdb (part 5's noise continues from there). Raw values: 0..225
(1,438 cells are 30 and 167 are 225, the clamp limits).

**0x377b2, smoothing**, in place, cells in memory order (Gauss–Seidel: the left and upper neighbours are already
smoothed):

```c
for (i = 0; i < 0x10000; i++) {
    e = m[i] + m_word_hi(i) + m[(i - 1) & 0xffff] + m[(i + 1) & 0xffff] + m[(i - 256) & 0xffff];
    m[i] = (e * 0x33) >> 8;        /* about e / 5, low byte */
}
```

`m_word_hi(i)` is the high byte of a word load at `map + i`: `m[i + 1]`, except for i = 0xffff, where it is the byte
after the map (still inside the 128 KB allocation; 0 in the run). So the right neighbour counts twice. (The first
cell also adds `edx`'s bits 8..15 left from before the call; they are 0 here: the model with 0 matches.) Result: heights
16..222, mean 97.6. The map is generated once and never changes. It is procedural: a remake regenerates it from the
seed `[0x37130]` (file) with this code; its resolution stays 256 x 256, because the wireframe samples it per cell.

-----------------------------------------------------------------------------------------------------------------

## 3. Fifth part 0x38520: the blue landscape (records 4693..5751, music 15:13..18:47)

### 3.1 What it draws

A 32 x 32-vertex wireframe of the height map, light blue glow over dark blue noise, seen from 5,600 units away by a
camera that pitches, turns and rolls slowly while the landscape scrolls towards it. Edges are drawn from near to far
with a floating horizon, so lines behind nearer ridges are hidden: the "shadow" areas are noise with no lines. The
picture fades in over 256 callbacks, the captions "hmmm", "moving lightsource and shadow" (twice as long) and "woww"
appear at the bottom, and from order 18 it fades out over 256 callbacks.

| records | music | callbacks n | on screen |
|---|---|---|---|
| 4693..4695 | 15:13 | – | black (set-up clears video memory, DAC all 0) |
| 4696..~4952 | 15:14.. | 0..256 | fade in (drawn frame 0, n = 0, is drawn with the file's start values but its palette is 0: invisible) |
| 5184..5278 | 17:00..17:15 | 488..582 | caption "hmmm" |
| 5279..5468 | 17:16..17:47 | 583..772 | caption "moving lightsource and shadow" (two pages, same text) |
| 5467..~5722 | 18:00.. | 772..1027 | fade out (palette factor 256 → 0) |
| 5469..5563 | 18:00..18:15 | 773..867 | caption "woww" |
| ..5751 | ..18:47 | ..1055 | black; order 19 ends the part (record 5752 is part 6) |

Every frame overwrites the whole buffer (noise); the captions overwrite their pixels.

### 3.2 Data and buffers

| what | where | format |
|---|---|---|
| height map | `[0x37148]` (64 KB aligned, 0x110000 in the run) | 256 x 256 bytes, section 2 |
| draw buffer | `[0x371b4]` (from 0x38469) | 320 x 200 |
| vertices | `[0x371a8]` = getmem(0x3000) | 1024 x (x, y, z): vertex k = 32r + c: x = −4096 + 256c, z = −4096 + 256r (built by 0x385f5), y written every frame |
| edges | `[0x371a4]` = getmem(0x100000) | 1984 x (a·12, b·12): 992 edges (32r+c, 32r+c+1) (c < 31), then 992 edges (c+32i, c+32i+32) (i < 31) (built by 0x38585) |
| projected | `[0x371b0]` = getmem(0x3000) | 1024 x (sx, sy, z) |
| sort arrays | `[0x3719c]`, `[0x371a0]` = getmem(0x1f10) each, adjacent | keys (dwords) |
| horizon | 0x37b1c, 320 dwords | per column, set to 200 every frame |
| row table | 0x377fc, 200 dwords | y·320 |
| palette | 0x371da, 768 bytes | 6-bit RGB: 0 black, 1..15 dark blue (noise), rising through blue to white at 151..253 and 255, 254 grey (33,33,33) |
| working palette | 0x36e30, 768 bytes | 0 in the file: written to the DAC at set-up (black), then `(0x371da · fade) >> 8` every frame |
| constants | `[0x371cc]` = 5600 (eye distance) | file |
| state | `[0x36e2c]` fade = 0; phases `[0x37180]..[0x37194]`, `[0x37158]`, `[0x3715c]`, `[0x37160]`, `[0x37164]` = 0; `[0x371d4]` zpos = 0; `[0x37174]` pitch = 600, `[0x37178]` yaw = 0, `[0x3717c]` roll = 0, `[0x37150]` height = 0x8000, `[0x3714c]` fx = 0xf60, `[0x37154]` fz = 0, `[0x371d8]` map position = 0 | file; the last six are what drawn frame 0 sees before any callback |

### 3.3 Set-up and callback

Set-up (0x38520..0x38729): `push [8]`; clear video memory; the allocations and tables above; write 0x36e30 (0) to
the DAC; patch the draw buffer address into the line drawer (`add edi, imm32` at 0x381b4 and 0x38285, fields
0x381b6, 0x38287, 0x0fffffff in the file); add the start phases (each `& 0xfff`): `[0x37180]` += 0x2b08,
`[0x37184]` += 0x17e8, `[0x37188]` += 0x17e8, `[0x3718c]` += 0xe58, `[0x37190]` += 0x1320, `[0x37194]` += 0x2178,
`[0x37160]` += 0x85e0, `[0x37164]` −= 0x7788, `[0x3715c]` += 0x2178, `[0x37158]` += 0x8f70; `[0x371d4]` −= 0x9900
(no mask); install the timer.

Callback 0x38985, per refresh:

```c
if (order != 0x12) { if (fade != 0x100) fade++; } else if (fade) fade--;
p0=(p0+9)&0xfff; p1=(p1+5)&0xfff; q0=(q0+5)&0xfff; q1=(q1+3)&0xfff; r0=(r0+4)&0xfff; r1=(r1+7)&0xfff;
pitch = ((((S[p0] + S[p1]) >> 2) + 0x32c) >> 1) & 0xfff;     /* [0x37174]: 150..661 = 13..58 deg down */
yaw   = ((S[q0] + S[q1]) >> 1) & 0xfff;                      /* [0x37178]: +-1022 = +-90 deg */
roll  = ((S[r0] + S[r1]) >> 2) & 0xfff;                      /* [0x3717c]: +-511 = +-45 deg */
l0 = (l0 + 0x1c) & 0xfff;  l1 = (l1 - 0x19) & 0xfff;          /* [0x37160], [0x37164]: never read (3.6) */
h = (h + 7) & 0xfff;    height = (S[h] >> 3) + 0x464;          /* [0x37150]: 1124 - 128 .. 1124 + 127 */
w = (w + 0x1e) & 0xfff; xpos = (S[w] >> 4) + 0x5a00;           /* [0x371d0]: 8.8 map column, sway -64..+63 */
zpos -= 0x20;                                                   /* [0x371d4]: 8.8 map row, 1/8 cell per callback */
fx = xpos & 0xff;  fz = zpos & 0xff;                            /* [0x3714c], [0x37154] */
mappos = ((xpos >> 8) & 0xff) | (((uint32)zpos >> 8 & 0xff) << 8);   /* [0x371d8] word */
```

(p0..r1 = `[0x37180]`..`[0x37194]`, h = `[0x3715c]`, w = `[0x37158]`.) Closed forms after n ≥ 1 callbacks:
p0 = 0xb08 + 9n, p1 = 0x7e8 + 5n, q0 = 0x7e8 + 5n, q1 = 0xe58 + 3n, r0 = 0x320 + 4n, r1 = 0x178 + 7n,
h = 0x178 + 7n, w = 0xf70 + 30n (all mod 4096); zpos = −0x9900 − 32n; fade = min(n, 256) before order 18, then
256 − (n − N2 + 1) down to 0, N2 = the first callback in order 18 (772 in the capture).

### 3.4 Main loop (0x3872e..0x38973)

```c
for (;;) {
    for (x = 0; x < 320; x++) hor[x] = 200;
    noise();                                                    /* seed [0x37130], 0.2 */
    bl0 = mappos & 0xff;  bh0 = mappos >> 8;                    /* 0x38774 */
    for (r = 0; r < 32; r++) for (c = 0; c < 32; c++)
        V[32r + c].y = (m((bh0 - r) & 0xff, (bl0 - c) & 0xff) - 0x80) << 5;     /* -3584..3008 */
    project();                                                  /* 0x38331 */
    cnt = 0;                                                    /* [0x37198] */
    for (e = 0; e < 1984; e++) {                                /* 0x387c1 */
        s = P[E[e].a].z + P[E[e].b].z;
        if (s > 0) key[cnt++] = s + (e << 16);
    }
    radix_sort(key, cnt);                                       /* 0x3881b */
    for (i = 0; i < cnt; i++) {                                 /* 0x38896 */
        e = key[i] >> 16;
        line_h(P[E[e].a].sx, P[E[e].a].sy, P[E[e].b].sx, P[E[e].b].sy);      /* 0x3801c */
    }
    text_printer(buf);                                          /* 0x4cacc: the captions (3.7) */
    wait_retrace_start(); wait_retrace_end();
    for (i = 0; i < 768; i++) pal[i] = (src[i] * fade) >> 8;    /* fade read AFTER the retrace's callback */
    write_dac(pal);  copy(buf, video);
    if (order == 0x13 || inport(0x60) == 1) break;
}
```

A map cell (column u, row v) is at world `x = xpos − 256u − 4096`, `z = zpos − 256v − 4096` (both mod 65536 in the
window sense): the 32 x 32 window `u = X − c`, `v = Z − r` (X, Z = integer parts of xpos, zpos) follows the camera,
so vertices appear and disappear at the window's far and side edges, a row every 8 callbacks. Higher map values give
larger y, which is lower on the screen (y grows downwards).

**project() (0x38331).** Self-modifying: the sine and cosine of the three angles are written into the immediates
of `mov edx, imm32` / `mov ebp, imm32` (yaw at 0x383be/0x383c3, fields 0x383bf/0x383c4; pitch at 0x383e2/0x383e7,
fields 0x383e3/0x383e8; roll at 0x38406/0x3840b, fields 0x38407/0x3840c; 0x0fffffff in the file). Per vertex, with
`R()` as in 1.4:

```c
x = V.x + fx;  y = V.y + height;  z = V.z + fz;
(x, z) = R(yaw, x, z);
(y, z) = R(pitch, y, z);
(x, y) = R(roll, x, y);
z += 5600;
if (z > 0) { P.sx = (int32)(x << 8) / z + 160;  P.sy = (int32)(y << 8) / z + 100; }   /* shl, then cdq */
P.z = z;                     /* for z <= 0 sx, sy keep the previous frame's values */
```

In the capture z ≤ 0 happens only in drawn frames 181..183 (vertex 31, z down to −46): its two edges are drawn
towards its stale projection (1056, 41034), far off screen. The allocation is fresh `getmem` memory, so before the
first projection it holds whatever was there (taken from the snapshot by the model; never used in the capture).

**radix_sort (0x3881b..0x38894).** Self-modifying: the two array addresses go into the immediates of
`mov esi, imm32` (0x38840, field 0x38841, array 1), `mov ecx, imm32` (0x38845, field 0x38846, array 2) and
`mov esi, imm32` (0x38878, field 0x38879, array 2) every frame. Twelve passes for bit = 3..14: a stable partition
of array 1, keys with the bit clear stay in array 1 (compacted), keys with the bit set go to array 2, then array 2 is
appended. Result: the keys in ascending order of `(s >> 3) & 0xfff`, equal keys in edge order, i.e. **near edges
first** (sum of the two depths, in steps of 8). Quirks without effect: the append copies `2 · n2` dwords instead of
`n2` (`shr ecx, 1` of a byte count), writing garbage after the valid keys (into array 2, behind its read pointer);
bit 15 of `s` is not sorted (s never exceeds 23,342 after the first frame; the invisible drawn frame 0 has larger
sums). Every edge has s > 0 in the capture, so all 1984 edges are drawn every frame.

### 3.5 The line drawer with the floating horizon (0x3801c)

As 0.3, with per point the test against `hor[]` (the y of the highest point drawn so far in that column):

```c
fast path:  for each point: xx = X >> 8, yy = Y >> 8;
                if (yy >= hor[xx]) goto clipped_tail;      /* 0x38273 jumps into the clipped loop: the rest of
                                                              the line is drawn by the clipped path */
                hor[xx] = yy;  blob(xx, yy);
clipped path: for each point: if (0 <= yy <= 191 && 0 <= xx <= 311 && yy < hor[xx]) {
                hor[xx] = yy;  blob(xx, yy);  buf[yy*320 + xx]++;   /* 0x38253: an extra +1 at the point */
              }
```

Both loops count with `dec cl`. The horizon holds the top-left corner of the blobs, per column of the points only
(the columns between two points of a line, ~2 px apart, are not raised), and a blob reaches 7 px below its point.
So a farther line is drawn where its points are strictly above every nearer point in their column; behind ridges the
lines vanish and leave noise (the dark "shadow" areas), with some leaks in the columns between points.

### 3.6 "moving lightsource and shadow"

There is no light source and no shadow computation in this part. Everything drawn is accounted for by the code
above (the model without any lighting reproduces every frame exactly): every line gets the same blob, and the dark
areas are the hidden-line removal of 3.5, whose shape moves with the camera. The only trace of a light are two
angles that the callback advances and nothing reads: `[0x37160]` (start 0x5e0, +0x1c per callback) and `[0x37164]`
(start 0x878, −0x19 per callback) (guess: left over from a removed light direction). The caption describes the
look, not a computation.

### 3.7 Captions

Drawn by the shared printer 0x4cacc (core scope) into the buffer after the lines, every frame: font strip at 0x3cec2
(320 bytes per row, 38 rows; hand-drawn, core scope), glyph widths 0x4c8c2, advances 0x4c861; glyph pixels ≠ 0 are
copied opaque (the font's indices 7, 216 and 254 show as (3,7,9), white and grey (33,33,33) in this palette, so
the text fades with the picture). Each '\r' line is centred: `x = 160 − width/2` (width = sum of advances, 4 per space,
up to the next '\r'), rows 162..199. The pages ('\r' ... '@') in the script, from the part's start pointer 0x4c946:

| page | text | x | shown (capture) | music |
|---|---|---|---|---|
| 0x4c946..0x4c950 | (empty) x 6 | – | 4696..5183 | 15:13..16:47 |
| 0x4c952 | "hmmm" (0x4c953) | 129..190 | records 5184..5278 | 17:00..17:15 |
| 0x4c958 | "moving lightsource and shadow" (0x4c959) | 10..309 | 5279..5373 | 17:16..17:31 |
| 0x4c977 | "moving lightsource and shadow" (0x4c978) | 10..309 | 5374..5468 | 17:32..17:47 |
| 0x4c996 | "woww" (0x4c997) | 128..191 | 5469..5563 | 18:00..18:15 |
| 0x4c99c.. | (empty) | – | 5564.. | 18:16.. |

A page stays until the printer sees bit 4 of the row or the order change (every 16 rows); it advances after drawing
the old page once more, so a caption first appears in the second drawn frame after the music reaches its row (in the
capture 2 records after the row's first record: 17:00 is first in record 5182, "hmmm" in 5184).

### 3.8 Pixel index and palette

`index = noise (0..15) + Σ blobs (+1 at the point of each blob drawn by the clipped path)`, then the caption's
pixels replace it. Palette 0x371da × fade/256 (integer), written every drawn frame in the vertical blank with the
fade of that blank's callback (one callback newer than the picture's state).

### 3.9 Time bases and music events

- Retrace callback: all camera variables, the map position, the fade (closed forms in 3.3).
- Per drawn frame: the noise; the stale projections of vertices behind the eye (3.4).
- Music: order 18 starts the fade-out (first callback in order 18: 772, record 5467); order 19 ends the part. The
  captions follow the 16-row blocks (3.7).

### 3.10 Shortcuts a remake evaluates exactly

- As 1.7 (trigonometry, projection, glow strokes, noise, palette ramp, edge clip).
- The floating horizon at the points' columns → hidden-line removal per output column on continuous lines, drawn near
  to far (a per-column "highest drawn so far" buffer at the output resolution); the leaks between points and the
  extra +1 of the clipped path are 320 x 200 artefacts.
- The depth sort in steps of 8 by the sum of the two depths → sort by the edge's mean depth.
- The fade `(v · fade) >> 8` of 6-bit values → a continuous factor fade/256.
- Keep: the 32 x 32 window over the 256 x 256 map with its popping edges, the 3.5 px glow offset (or not: the remake
  decides), the captions' timing.

### 3.11 Not determined

- Whether a light source was planned (3.6): only the two unused angles hint at it.

-----------------------------------------------------------------------------------------------------------------

## 4. For the remake: what to read from the original

| what | address | size |
|---|---|---|
| sine quarter | 0x24a3c | 1025 dwords (rest built as in 0.1) |
| part 3 palettes | 0x347a8 (green), 0x34aa8 (red) | 768 bytes each |
| part 3 canyon map | 0x2f2b0 | 3,200 bytes (200 x 16) |
| part 3 vertices x, y | 0x2ffa8 | 256 x 12 bytes (or the formula in 1.2) |
| part 3 edges | 0x30ba8 | 480 x 8 bytes (or the formula) |
| part 3 constants | 0x2ff48, 0x2ff50, 0x2ff54, 0x2ff40 (slide start), 0x2f21c (128) | dwords |
| part 5 terrain seed | 0x37130 | dword (0); generator 2 |
| part 5 palette | 0x371da | 768 bytes |
| part 5 eye distance | 0x371cc | dword (5600) |
| part 5 start values | 0x37174 (600), 0x37150 (0x8000), 0x3714c (0xf60) | drawn frame 0 only (invisible) |
| noise seed of part 3 | 0x2f224 | dword (0) |
| captions | 0x4c952.. (script), font 0x3cec2 | core scope |

The step constants (8, 0x13, 0x20, 0x40, 9, 5, 5, 3, 4, 7, 0x1c, 0x19, 7, 0x1e, 0x20, 0x32c, 0x464, 0x5a00, the
start phases of 3.3, the blob) are code constants.
