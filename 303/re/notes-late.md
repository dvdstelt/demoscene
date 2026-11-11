# 303.EXE late parts: web, harl, paranoid .. end of 0x2e8e4, dance, icons, outro

Sections 1-2 (web, harl) are by this agent. Sections 3 (paranoid .. end of 0x2e8e4) and 4 (dance, icons, outro) were
written by two forked sub-agents and merged here, with time conversions and remake line numbers updated.
The remake was restructured during this work: all scenes covered here now live in **remake/src/scenes/late.cpp**
(Harl :13, Web :38, Paranoid :134, FaceFloor :157, Dance :194, Icons :210, Outro :249). Line references were
rewritten accordingly.

## 0. Times and the capture

- **m** = music seconds. The position counter [0x9b844] was converted with rows.txt (pos.py's music column).
- **v (fit)** = coordinator's visual fit `1.00643*m - 1.953`. **VT** = pos.py's audio relation `1.00126*m - 1.395`.
- **Which one matches the capture depends on where in the demo you are.** I checked frame by frame:
  - Around 82-93 s the fit matches to within one 30 fps frame: the web's five polarity flips start at capture 83.80 /
    85.50 / 87.20 / 88.90 / 90.63 against fit 83.80 / 85.51 / 87.23 / 88.94 / 90.65 (VT is 0.1 s late).
  - In the late parts the capture follows VT instead. The 0xf00 cut to white is at capture 196.87 (VT 196.85, fit
    197.31), and the ALEX white flash at 0x1060 is at capture 215.37 (VT 215.33, fit 215.89).
  - So the capture's offset drifts, and the fit should not be extrapolated past ~100 s.

**End positions of every sub-part in this scope:**

| part / sub-part | end condition | end pos | **m** | VT | v (fit) |
|---|---|---|---|---|---|
| Harl (inline 0x314b6) | pos >= 0x680 | 0x680 | **83.504** | 82.215 | 82.088 |
| Web (inline 0x31802) | pos >= 0x740 | 0x740 | **93.716** | 92.439 | 92.366 |
| Paranoid (in 0x2e8e4) | pos >= [0x9b840]+0x80, [0x9b840]=0xe80 | 0xf00 | **197.991** | 196.845 | 197.311 |
| A: transition | camera z >= 14000 (37 ticks) | ~0xf0a | ~198.53 | ~197.38 | - |
| B: sheet chase | pos >= 0xf00+0x80 | 0xf80 | **204.703** | 203.566 | 204.066 |
| C: tiles, fade to black | palette level <= 0 (~373 ticks after the C start) | ~0xfe5 | **~210.03** | ~208.9 | - |
| = end of 0x2e8e4 | | | ~210.03 | ~208.9 | |
| Dance VS (inline 0x31b17) | pos >= [0x9b840]+0x60, [0x9b840]=0x1000 | 0x1060 | **216.449** | 215.327 | 215.888 |
| Dance ALEX (inline 0x31df9) | pos >= 0x1000+0x90 | 0x1090 | **218.966** | 217.847 | 218.421 |
| Icons + MC-303 (0x2a840) | palette level <= 0 (fade 10/tick from 0x1180) | - | **~254.90** (black from ~254.53) | 253.83 | - |
| Outro 0x10dc4 | no visual: closes the PAK, then a black screen until a key is pressed | - | - | - | - |

## 1. Web (main script 0x316f6 - 0x3193d; routines 0x2c5c0, 0x2d038; combine 0x1c3f4)

Scratch: `scratchpad/re/late/` (listings `f*.s`, `lin2c5c0.s`, `lin2d038.s`; `x87emu.py` = small x87 emulator used to
check the matrix builders; `websim.py` = Python reimplementation of the whole part, frames in `late/sim/`;
capture frames in `late/cap/`, 30 fps sequence `late/cap/seq/`).

### What it draws

- **Layer A ("web", 0x2c5c0):** a flat line pattern on a 9x9 grid of 12-unit cells. Each cell holds one of four random
  line tiles built from an octagon, and the grid is mirror-symmetric in x and y. The same pattern is repeated on 10
  parallel planes spaced 1 unit apart in z and seen in perspective. The camera wobbles in x/y, rolls clockwise at a
  constant rate, backs away for 7 s and then flies forward. Lines are drawn with an additive plus-shaped pen into a
  coverage buffer, so overlaps get darker on white. This gives the dense radial "scribble" in the centre and big
  octagon cells at the edges.
- **Layer B ("words", 0x2d038):** six lowercase outline words from fontout.raw: **success, valhalla, oasis, jello,
  analogue, sparc**. Each word enters from far off-screen, shrinks towards the centre, turns round and flies back out.
  The words rotate with the camera roll and leave a feedback trail of 0.68 per rendered frame.
- **Combine (0x1c3f4):** where layer B is non-zero, the web pixel is recoloured. The word intensity picks one of 12
  colour ramps of the YIQ palette left over from Harl, and the web coverage sets the luma.
- **Polarity:** every 16 rows the whole palette swaps to its negative, with a 0.47 s cross-fade. The background goes
  white, black, white, black, white, black.

### Main script (0x316f6 - 0x3193d)

```c
// entered when the Harl loop sees pos >= 0x680
speed = 0; level = 0x8000; bright = 1.0; DAC[all 768] = 63; memset(screen, 0, 64000);   // instant white
target = 0x4000;
web(0x17);      // 0x2c5c0 init
words(0x17);    // 0x2d038 init
// colour table in the light/fade table L = [0x5b67c] (64K, 256-byte rows); only rows 32..63, columns 0..63:
for (i = 0; i < 64; i++) L[32][i] = i;
for (j = 1; j < 32; j++) for (i = 0; i < 64; i++) L[32 + j][i] = 0x40 + 16 * min(j / 3, 11) + i / 4;
//   (columns 64..255 of those rows keep truecol.fad, loaded at the start of Harl)
speed = 1000;                         // white -> normal in 17 ticks (0.24 s), linear in DAC units
A = 0x5fda8; B = 0x6f7a8;             // B is NOT cleared: it still holds the last Harl frame
do {
    web(A);                           // A = line coverage (cleared every frame)
    words(B);                         // B = T2[(T1[B] + W) & 255], W = this frame's word layer
    combine(B, A);                    // 0x1c3f4: A[p] = L[32 + (B[p] >> 3)][A[p]]
    memcpy(screen, A, 64000);         // 0x1bbec
    music_service(); if (kbhit()) quit_demo();
} while ([0x9b844] < 0x740);
words(0x48);   // remove callback 0x2c2a8, free texture
web(0x48);     // remove callback 0x2c360, free [0x146374] (allocated at init, never used)
```
`[0x8ebb0]` (palette "invert" mode) is 0 at the start: it was set by blobby's init and cleared by blobby's free. The web
therefore fades in from white. The first polarity flip sets it to 1, and it stays 1 for the rest of the part.

### 0x2c5c0 web: init (0x17)

```c
pos = 0.0f;  vel = -4.0f;                       // [0x148f84], [0x148f88]
// 9x9 tile grid g[row*9+col], 4-fold mirror symmetric; tbl @0x58078 (16 bytes):
tbl = {0,0,1,3, 1,1,1,1, 1,3,3,3, 3,3,2,2};     // P(0)=2/16, P(1)=6/16, P(2)=2/16, P(3)=6/16, never empty
for (i = 0; i < 5; i++) for (k = 0; k < 5; k++) {
    v = tbl[rand() & 15];
    g[i][k] = g[i][8-k] = g[8-i][k] = g[8-i][8-k] = v;   // row 4 / column 4 get written twice
}
// vertices (float x, y; plus projected sx, sy), edges (int pairs)
for (b = 0; b < 9; b++) for (j = 0; j < 9; j++) {        // X = -48 + 12 b (vertex x), Y = -48 + 12 j (vertex y)
    v = g[b + 9*j];                                       // i.e. g[row j][col b]
    base = nverts;
    oct = {(-6,-2), (-6,2), (-2,6), (2,6), (6,2), (6,-2), (2,-6), (-2,-6)};   // vertices 0..7, +(X,Y)
    switch (v) {
    case 0: edges (0,5) (1,4) (7,2) (6,3);            // "#": lines y=-2, y=+2, x=-2, x=+2 across the whole cell
    case 1: edges (7,6) (5,4) (3,2) (1,0) + case 2;   // full octagon (falls through into case 2)
    case 2: edges (0,7) (6,5) (4,3) (2,1);            // the four cut corners only (diagonals)
    case 3: + vertices 8..12 = (0,0) (-2,-2) (-2,2) (2,2) (2,-2);   // vertex 8 unused
            edges (0,9) (9,7) (6,12) (12,5) (1,10) (2,10) (3,11) (4,11) (9,11) (12,10);
            // four L-corners from the edge midpoints to the inner square corners + its two diagonals (X)
    }
}
// the octagon sides lie on the cell border (x=+-6 for |y|<=2 ...), so neighbouring tiles join up.
grey ramp: for (i = 0; i < 64; i++) pal[i] = (63-i, 63-i, 63-i);   // 0x5faa8; index 0 = white, 63 = black
[0xc1d64] = 0;  rot2(0, 0, 0xa0120);    // 0x14760: camera matrix = identity
add_tick_callback(0x2c360);
```
The tile pattern and the word x offsets depend on the Watcom rand() state at this point. That state depends on
everything before it, including frame counts, so **the exact pattern is effectively random per run**. Only the
probabilities and the symmetry are fixed.

### 0x2c5c0 web: callback 0x2c360 (every 70 Hz tick)

```c
if ([0x9b844] != last) {                     // last = [0x57d9c]
    last = [0x9b844];
    if ((last & 0x1f) == 0 && level == target) {      // every 16 rows, no fade running
        speed = 500; [0x8ebb0] = 1; level = 0x8000;   // start at the full negative of the NEW palette...
        for (i = 0; i < 768; i++) pal[i] ^= 0x3f;     // ...which IS the old palette: smooth cross-fade
        [0x149390] ^= 1;                              // flag, not read in this part
    }
}
pos += vel;  if (vel < 4.0f) vel += 0.0081215f;   // [0x50fc0] = 4.0, [0x50fc4] = 0.008121499791741371
[0xc1d64]++;                                      // pending ticks for the camera (0x27444)
[0x5805c]++;                                      // t, data init 2204727, only touched here
```
The palette tick (0x10124/0x1019c) with `[0x8ebb0]=1` shows `((63-P)*w + P*(63-w)) >> 6`, where
`w = (level>>8) - 65`. level falls 500 per tick, so the cross-fade from the old polarity to the inverted one is
linear in DAC units and lasts 33 ticks (0.468 s). It also inverts palette entries 64..255 (the word colours).

### 0x2c5c0 web: render (per frame)

```c
t = [0x5805c];                                                // ticks, see above
sa = sin(t * 0.006666666666666666 + 100.0);  sb = sin(t * 0.013698630136986302 + 12400.0);
camX = 20*sa;  camY = 20*sb;  camZ = pos * 0.01f;             // [0xa018c], [0xa0190], [0xa0194]
look = (0, 0, camZ + 1000);  roll = 0.0051f;                  // [0xc1d40..48], [0xc1d68]
memset(A, 0, 64000);
camera_27444(1);                                              // runs [0xc1d64] times, see below
c = trunc(camZ);                                              // toward zero (0x3235e)
for (k = c - 1; k < c + 10; k++) {                            // plane z = k
    d = k - camZ;
    if (d < -2.0f || d >= 12.0f) continue;
    for each vertex (x, y):
        dx = x - camX; dy = y - camY;
        zr = dx*M[6] + dy*M[7] + d*M[8];                      // M = 3x3 row-major camera matrix 0xa0120
        if (zr > 0.05) { s = 5.0f / zr;
            sx = 160 + (dx*M[0] + dy*M[1] + d*M[2]) * s;
            sy = 100 + (dx*M[3] + dy*M[4] + d*M[5]) * s; }
        else sx = 8763008.0f (invalid);
    for each edge (a, b): if both valid: line(A, trunc(sx_a), trunc(sy_a), trunc(sx_b), trunc(sy_b));   // 0x2acf0
}
```
- The camera is at z <= 0 for the whole part, so `trunc` = ceil. The drawn planes are `d = f, f+1, ..., f+9` with
  `f = ceil(camZ) - camZ` in [0, 1). The nearest plane is only drawn once f > 0.05 (the zr test is per vertex, so a
  plane crossing 0.05 loses edges one by one). There is no fog: the farthest plane (d < 10) pops in or out when camZ
  crosses an integer.
- **Scale:** a cell is 12 units, giving 60/d px. The whole 108-unit pattern spans +-270 px at d=1 and +-27 px at
  d=10. The camera offset camX moves a plane by -100/d px.
- **camZ(n)** at tick n after init: `pos(n) = -4n + 0.0081215*n(n-1)/2` (until vel reaches 4 at n=986), so
  camZ = 0.01*pos.
  - 0 -> -9.87 at n=493 (7.03 s): the planes recede, i.e. a zoom-out.
  - Then it flies forward: -7.8 at the part end (~10.2 s), with vel = 1.84/tick = 1.29 planes/s.
- **camX, camY:** with `t = 2204727 + n`, these reduce to `camX = 20 sin(n/150 + 1.27860)` (period 942 ticks = 13.4 s)
  and `camY = 20 sin(n/73 + 1.74334)` (period 459 ticks = 6.54 s). At the start camX=19.2, camY=19.7.

**camera_27444(mode=1)** (also used by 0x2e8e4, see 3b). Once per pending tick (`while ([0xc1d64]) {...; [0xc1d64]--}`):
```c
d = look - cam;  dist = |d|;
if (dist > 0.01) {
    n = d * (0.05 / dist);                         // [0x57d80] = 0.05
    axis = (mode & 1) ? cross(n, M.row2) : 0;      // |axis| = 0.05 sin(angle to the target)
    M = M * rot3(axis);                            // 0x2721c: Rodrigues, angle asin(|axis|); 0x18b18: 3x3 multiply
    if (roll != 0) M = rot3((0, 0, roll)) * M;     // = Rz(asin(0.0051)) * M
}
for (k = 0; k < ((mode & 2) ? 3 : 2); k++) {      // x, y only here
    vel[k] = 0.79*vel[k] + 0.002*(tgt[k] - cam[k]); cam[k] += vel[k];   // tgt [0xc1d4c..], vel [0xc1d58..]
}
```
rot3(0, 0, s) = [[c, -s', 0], [s', c, 0], [0, 0, 1]] with s' = sin(asin s). This was checked by emulating 0x2721c.
- **Roll:** every tick the matrix is pre-multiplied by Rz(0.0051 rad). The pattern on screen turns **clockwise**
  (y down) at 0.0051 rad/tick = 0.3574 rad/s (20.5 deg/s, one turn in 17.6 s), so the angle is
  `phi = 0.0051 * ticks since init`. By the part end that is 3.65 rad.
- **Turn towards the look target:** the target is (0, 0, camZ+1000), which tilts the view by at most ~1.1 deg. Its
  visible effect is about 0.1 px, so it can be ignored.
- **Position spring:** tgt x/y are 0, because they are written only by later parts. The spring therefore pulls camX/camY
  about 1 % towards 0 per pending tick, applied after the per-frame reset. This can also be ignored.

**line 0x2acf0(buf, x0, y0, x1, y1)** clips to x in [3, 317], y in [3, 197] (the frame keeps a 3-pixel border). It is a
16.16 DDA along the major axis: the start is the end with the smaller major coordinate, and the step count is
|major delta|, so the last pixel is not drawn. The minor coordinate starts at the integer value and steps when the
fraction overflows (floor). It adds a pen per step using dword adds, so a byte overflow carries into the next pixel
and nothing saturates:
```
y-major (0x1bf7e) and x-major with dy>=0 (0x1bfb7):     x-major with dy<0:
          . 1 .        (row y-1)                                  . 1 .     (y-1)
          1 2 1  <- centre is (x-1, y)                            1 2 1     (y)
          . 1 .        (row y+1)                                  1 2 1     (y+1)
                                                                  . 1 .     (y+2)
```
So rising diagonals (going right and up) are drawn twice as heavy. A single line therefore gives values 1..2, which
shows as DAC 61..62 of 63, very faint. Only overlapping planes in the centre get dark. Values >= 64 index YIQ colours
through L row 32, which is truecol.fad there; this is rare.

### 0x2d038 words: init (0x17)

```c
N = [0x148f8c] = 6;
for (i = 0; i < 6; i++) {          // W[i] at 0x148f90 + 16 i
    W[i].x = (rand() - 0x4000) * 0.025;     // -409.6 .. +409.6, random
    W[i].y = -450 + 150 i;                  // -450, -300, -150, 0, 150, 300
    W[i].z = 0;
    W[i].w = -15 - 5 i;                     // -15 ... -40
}
tex = alloc64K();                  // [0x14637c], 256x256, 64K-aligned (0x10ab4: texture RAM, zeroed)
acc = tempalloc(64000);            // [0x146378]
T1[i] = min(127, trunc(0.4 i));    // 0x148b84
T2[i] = min(255, trunc(1.7 i));    // 0x148c84
add_tick_callback(0x2c2a8);
words = {"success", "valhalla", "oasis", "jello", "analogue", "sparc"};   // pointer table 0x58060
```
**callback 0x2c2a8 (per tick):** `for i: W[i].w += 0.05f; W[i].z = (45.0f - w*w) * 0.25;` (0x50fb8 = 45,
0x50fbc = 0.05, 0x50fb0 = 0.25).

### 0x2d038 words: render(dst = B)

```c
memset(acc, 0, 64000);
for (i = 0; i < N; i++) {
    z = W[i].z;  if (!(z > 0.05 && z < 20.0)) continue;    // z <= 11.25 always
    memset(tex + 96*256, 0, 64*256);                         // rows 96..159
    // text: fontout.raw (1024x64, loaded at startup by 0x29860, every byte >> 3 -> 0..31);
    // glyph k of "abc..z" = k-th run of non-empty columns (column empty if all 64 values <= 1) at least 4 wide;
    // advance = glyph width - 4 (glyphs overlap by 4), unknown char = 12 - 4; text centred on column 128:
    x = 128 - width/2;  for each char: blit 64 rows at (x, row 96) with tex = max(tex, glyph); x += w - 4;
    zp = 2*z;
    // 0x2c428: 41x26 grid (screen points (8i, 8j)), u = column, v = row (8.8 fixed point), clamped to 0..255:
    //   u(i,j) = 128 + zp*(M[0]*(i-20) + M[3]*(j-12.5)) - 2*W[i].y/zp
    //   v(i,j) = 128 + zp*(M[1]*(i-20) + M[4]*(j-12.5)) - 2*W[i].x/zp
    // 0x1d256: 8x8-block bilinear interpolation of (u,v), nearest texel, acc = min(255, acc + tex[v][u])
}
for (p = 0; p < 64000; p++) B[p] = T2[(T1[B[p]] + acc[p]) & 255];     // 0x1be67 (byte add wraps)
```
Glyph table (fontout.raw columns, `start:width`): a 5:27 b 44:29 c 84:27 d 121:29 e 162:27 f 198:21 g 226:28 h 268:27
i 309:10 j 327:15 k 356:27 l 394:10 m 418:41 n 473:26 o 512:29 p 554:28 q 593:28 r 634:20 s 662:27 t 699:19 u 729:26
v 766:29 w 804:41 x 853:29 y 890:30 z 928:27.
Measured word widths in texels: success 160, valhalla 135, oasis 100, jello 71, analogue 168, sparc 109. Ink rows are
102..151: the x-height is rows 112..141, ascenders start at 102 and descenders end at 151.

**The same mapping in screen terms.** M's 2x2 block is Rz(phi), the web's roll.
- **Scale:** 4/z px per texel. A 256-texel span is 91 px at z = 11.25 and 341 px at z = 3.
- **Orientation:** the texture's +u axis points along screen direction (cos phi, sin phi) (y down), so the words turn
  clockwise together with the web.
- **Position:** texel (128, 128) is drawn at `(160, 100) + Rz(phi) * (4*W.y/z^2, 4*W.x/z^2)`. **W.y moves the word
  horizontally and W.x vertically.** At the far point (z = 11.25) the offset is under 15 px. At z = 3, W.y = -450
  gives 200 px, so the words fly in from outside and leave the same way. The direction keeps turning with phi.
- **Check against the capture at v 85.5:** the formula puts "success" (z = 8.85, phi = 69.6 deg) about (-8, -22) px
  from the centre, 74 px long, reading downwards. The capture shows (-10, -19) px, 73 px, about 72 deg. This also
  confirms that the roll and the words share the same tick counter.

**Timeline of the words** (n counted from web init, which is music 83.504 = pos 0x680; "z>=2"
is close to when a word reaches the screen; it is fully on screen from z ~ 3, about 0.1 s later):

| word | W.y | z > 0.05 | z >= 2 | far point z=11.25 | z < 2 again | gone |
|---|---|---|---|---|---|---|
| success | -450 | m 85.87 | m 86.05 | m 87.78 | m 89.52 | m 89.69 |
| valhalla | -300 | m 87.30 | m 87.48 | m 89.21 | m 90.95 | m 91.12 |
| oasis | -150 | m 88.73 | m 88.90 | m 90.64 | m 92.37 | m 92.55 |
| jello | 0 | m 90.15 | m 90.33 | m 92.06 | (93.80) | after the end |
| analogue | 150 | m 91.58 | m 91.76 | (93.49) | - | - |
| sparc | 300 | m 93.01 | m 93.18 | - | - | - |

The part ends at m 93.716, so sparc only flies in large and offset during the last 0.5 s. The capture shows the first
word at v 85.0, which is m 86.4.

**Feedback and colour.**
- **Trail:** B decays by `T2[T1[B]]`, about 0.68 per **rendered frame**, so its length depends on the frame rate. For a
  still outline (W = 31), B settles at 159 (j = B>>3 = 19).
- **Colour index:** `j == 0` shows the plain web A through the grey ramp. Otherwise the index is
  `0x40 + 16*min(j/3, 11) + A/4`, which hits the YIQ palette set by Harl's 0x13244 (`Y = 4*(c&15)`,
  `I = 16*((c>>4)&3)`, `Q = 16*((c>>6)&3)`, `R = 1.003Y + 0.9548I + 0.6178Q`, `G = 0.9967Y - 0.27I - 0.644Q`,
  `B = 1.0084Y - 1.11I + 1.6995Q`).
  - Ramp c = min(j/3, 11) uses I = 16*(c&3) and Q = 16*(1 + (c>>2)). Its luma is Y = 4*(A/4) ~ A, so the empty
    background inside a word (A=0) is a dark colour.
  - c=0 (B 8..23) is navy (9,0,27). c=1..3 (B 24..95) are dark red to red (25,0,9)..(55,0,0). c=4 (B 96..119) is
    violet (20,0,54). c=5..7 are purple to magenta, e.g. c=6 = (50,0,19). c=8 is (29,0,63). c=9..11 are magenta to
    pink.
  - So, by the code, a word should start red, sit magenta while still, and fade through violet and navy.
  - The negative phases invert these colours too.
- **The capture disagrees with the colours.** In the white phases every saturated pixel has hue 210..270 (blue). The
  darkest word pixels are (0,0,81)/255 and the most saturated are about (63,57,238), with R ~ G. In the black phases
  the words are yellow (hue 60..75). There is no red or magenta in any web frame of the 318-frame sequence (late/cap/seq).
  - Blue with G >= R is not reachable from this YIQ formula, which always gives R >= G.
  - The geometry, timing, words and polarity flips all match the capture exactly, so the mismatch is limited to the
    colour mapping (palette entries 64..255 or the L table). It is unresolved; see "not determined".
  - The remake's blue-on-white / yellow-on-black (late.cpp:79) matches the capture and should be kept.

### Time base

| quantity | base |
|---|---|
| camera roll phi, look-target turn, cam x/y spring (0x27444 catch-up) | per tick (pending counter [0xc1d64]) |
| camZ (pos/vel), t for camX/camY, word w/z (callbacks 0x2c360, 0x2c2a8) | per tick from web init |
| polarity flips | position: pos changes to a multiple of 0x20 (only while no fade runs) |
| flip cross-fade, initial white fade | per tick (palette tick) |
| A (web) | rebuilt per frame |
| B feedback trail (x0.68) | per rendered frame (the capture machine's rate is unknown) |
| tile pattern, W.x | rand() at init (random per run) |

### Times (music seconds m; v = capture time via the coordinator's fit, checked to within one 30 fps frame here)

| event | pos | m | v (fit) | capture |
|---|---|---|---|---|
| Harl ends, web init, instant white then 0.24 s fade | 0x680 | 83.504 | 82.09 | cut at 82.03-82.07 |
| flip to black (0.47 s cross-fade) | 0x6a0 | 85.206 | 83.80 | 83.80-84.20 |
| flip to white | 0x6c0 | 86.908 | 85.51 | 85.50-85.93 |
| flip to black | 0x6e0 | 88.610 | 87.23 | 87.20-87.60 |
| flip to white | 0x700 | 90.312 | 88.94 | 88.90-89.30 |
| flip to black | 0x720 | 92.014 | 90.65 | 90.63-91.03 |
| **end** | 0x740 | **93.716** | **92.37** | 92.37-92.40 |

### Differences from the remake (remake/src/scenes/late.cpp, the Web struct; most visible first)

1. **late.cpp:49-69:** ten rotating wireframe copies of the icosa/spikey meshes. The original is a 2D line pattern
   instead: 9x9 cells of four octagon-based tiles, mirror-symmetric. It sits on 10 planes 1 unit apart, projected with
   `160 + 5x/z`, `100 + 5y/z`, the camera is offset by (camX, camY), and lines are drawn with an additive plus-pen into
   a grey ramp (DAC 63 - count).
2. **late.cpp:72-73:** the words "aap", "graphics", "design" are wrong. They are **success, valhalla, oasis, jello,
   analogue, sparc**, in lowercase fontout.raw (outline font), each on its own z(t) parabola from the table above.
   Several can be on screen at once: at 90.5 v the capture shows oasis, jello and analogue together.
3. **late.cpp:82-86:** the remake turns the text in 3D (rotY, z-translate). The original only rotates in the screen
   plane by the camera roll (`phi = 0.0051 * ticks`, clockwise), scales by 4/z px per texel, and offsets by
   `Rz(phi)*(4*W.y, 4*W.x)/z^2`. The web pattern turns with the same phi, and the remake's web does not roll at all.
4. **late.cpp:41-45:** the 2-beat dark[] table is wrong. The original flips polarity every 16 rows: white until m
   85.206, then black, white (86.908), black (88.610), white (90.312), black (92.014) to the end. Each flip is a
   0.468 s linear DAC cross-fade between the palette and its negative, not a hard switch.
5. **Camera motion is missing.**
   - Zoom: the planes first recede (camZ 0 -> -9.87 over 7.03 s), then approach (-7.8 at the end), following the
     constant-acceleration formula above.
   - Parallax: camX = 20 sin(n/150 + 1.2786), camY = 20 sin(n/73 + 1.7433), moving near planes up to 100 px.
6. **Feedback trail on the words:** `B = T2[(T1[B] + W) & 255]` (x0.4 then x1.7, ~0.68 per frame) is not in the
   remake. It gives the doubled outlines visible in the capture. At the start, B also still holds the last Harl frame,
   which decays during the white fade-in.
7. **late.cpp:87:** the start flash is 81.95-82.3 smoothstep. The original goes instantly to white at m 83.504
   (v 82.09), then fades linearly over 17 ticks (0.24 s).
8. **timeline.cpp:16:** the web runs V(81.95)-V(92.5). The original runs m 83.504-93.716 (v 82.09-92.37).

### Not determined

- The colour mismatch described above. Was palette 64..255 different in the captured build, or is something else
  remapping the word colours? No other code in the exe writes 0x5faa8 or the L rows between Harl and the web.
- The tile pattern and W.x are rand()-dependent, so the capture's exact pattern can't be derived from code. W.x for
  success looks close to 0 in the capture.
- The render frame rate of the capture machine, which sets the trail length.

---

## 2. Harl (main script 0x313cd - 0x316f6; video 0x12ccc/0x13244/0x13174/0x131ac/0x1b819, 0x11308; overlay 0x2a34c; callback 0x30990)

### How the video is shown

This is the same inlined player as the dance (decoder details in 4.1).
```c
fx2a34c(0x17);                          // lyric overlay init (other agent)
open_video([0x580c4] = "harl.lzw");     // 0x12ccc (data default, [0x580c0] = 5 also data default)
yiq_palette();                          // 0x13244 -> 0x5faa8 + DAC (formula in section 1 / 4.1)
Ybuf = tempalloc(0x3c00); IQbuf = tempalloc(0x3c00); frame = 0;
add_tick_callback(0x30990);  want = 1;
load_fad("truecol.fad") -> [0x5b67c];  // 64 rows: row 0 black ... row 32 ~identity ... row 63 = index 15 (white)
bright = 1.0; speed = 0; level = 0x8000; DAC = all 63; memset(screen, 0); speed = 1000; target = 0x4000;
memset(B = 0x6f7a8, 0); [0x9b840] = 0;
do {
    fx2a34c(A = 0x5fda8);                               // overlay layer into A
    if (want) { want = 0;
        if (frame == 0) lzw_restart(stream, 1);         // 0x11308
        frame = (frame + 1) % 200;
        decode Y (160x96, 0x13174) and I/Q (2 x 80x48, 0x131ac); }
    dither_yiq(Ybuf, IQbuf, B + 640);                   // 0x1b819 every frame: 320x192 at rows 2..193
    lighten(src = A, dst = B);                          // 0x1c3f4: B = truecol[32 + (A>>3)][B]
    memcpy(screen, B);
} while ([0x9b844] < 0x680);
remove_tick_callback(0x30990); fx2a34c(0x48);
// callback 0x30990: if (cnt == 0) { cnt = [0x580c0]; want = 1; } else cnt--;
```
(The `ecx == 0x17 / 0x48` branches inside the loop body are dead copies, because ecx = 0x6f7a8.)

**Frame rate:** a new frame every `[0x580c0] + 1 = 6` ticks = **11.68 fps**. This is per tick, and frames are not
caught up. HARL.LZW holds 200 frames, but only frames 0..~120 are reached before 0x680, so the clip never loops in the
demo. The image is 320x192 starting at row 2, with a 4x4 ordered dither into the 16-luma x 4 I x 4 Q palette.

### What 0x2a34c adds on top (summary only; another agent decompiles it)

- **The lyric word:**
  - It is copied from fontblur.raw (560x96, blurred letters) into a 256x96 word buffer when a new word starts. Its
    position is random: `y = rand()%144`, `x = rand()%(320 - width)`.
  - Each frame it is blitted with scaling (0x29eac) into [0x13efc4], into the rect (x, y)-(x+width, y+56), and each
    corner jitters by the per-row random bytes J (0x15494).
  - Every 2 rows, with p = 2/16, the word gets a new random position and is drawn 3x larger, into
    (x-width, y-56)-(x+2*width, y+112), until the next 2-row tick.
  - Then `A = map[min(255, T1[A] + src/2)]` (0x1c058, T1 = trunc(0.7 i)). This is a feedback trail, decaying 0.7 per
    frame.
- **Mixing into the video:** A is mixed in by lightening through truecol.fad rows 32..63. A bright overlay pushes the
  video towards white (index 15); A = 0 leaves it about unchanged.
- **Word sequence:** with `[0x1473b8] = 0` (Harl/blobby mode) the word changes every 16 rows (pos & 0x1f == 0) and
  cycles through table 0x57dc0 with an index that is never reset: w0 / w1 / w2 / w3 /
  w4 / w5 / w6 / w7 / w8 / w9.
  - Harl continues where blobby stopped: w7, w8, w9, w0, w1, w2.
    This matches the remake's lyric list.
- **Random white flashes:** every 2 rows, with p = 1/16, f toggles to 1 (if f <= 0.5) and decays 0.021 per tick
  (0.68 s). The overlay map becomes `v(1-f) + (255-v)f`, so the overlay goes near 255 everywhere and the whole video
  flashes towards white.
- **Brightness jitter:** the overlay map also gets an offset of `+5*J` (signed byte 0x9c637).
- **Init:** 0x2a34c(0x17) sets the new-word flag, so the first word appears on Harl's first frame.
- **Note for blobby (not in this scope):** the exe's words are "w4" and "w5". The remake's lyric list
  had "v1's misheard w4" and "v1's misheard w5".

### Times

| event | pos | m | v (fit) |
|---|---|---|---|
| Harl start | end of 0x15734 (not in this scope) | - | - |
| Harl end | 0x680 | 83.504 | 82.09 |

### Differences from the remake (late.cpp Harl struct)

1. **late.cpp:18:** `frame = local * n / 10.3` plays all 200 frames in 10.3 s (19.4 fps). The original plays
   **11.68 fps** (one frame per 6 ticks) from frame 0 at the part start, reaching only about frame 120.
2. **late.cpp:22-31:** the "smeared band of columns on the off-beats" is not in the video path. On top of the video
   there is only the 0x2a34c lyric overlay, made of a fontblur word with a 0.7 feedback trail, jittered corners and
   occasional 3x enlargement, lightening the video through truecol.fad. There are also random whole-frame white flashes (p = 1/16
   every 2 rows, 0.68 s decay).
3. **late.cpp:21:** the picture should be 320x192 at rows 2..193 with black rows top and bottom, and 4x4
   ordered-dithered. The remake stretches it full-frame.
4. **late.cpp:32:** start flash 0.35 s smoothstep. The original goes instantly to white and then fades linearly over
   17 ticks (0.24 s).
5. **timeline.cpp:15:** Harl ends at m 83.504 (v 82.09), not V(81.95).

---

## 3. Paranoid ... end of 0x2e8e4 (0x2ffcb - 0x3098e)

> **Times in this section are pos.py "video" times (VT = 1.00126*m - 1.395), as written by the sub-agent. For the
> late parts VT is what matches the capture.** Music seconds (m) for the key events:
> paranoid start ([0x9b840]=0xe80; actual start is tick-driven, about VT 191.7) = m ~192.85 |
> paranoid end 0xf00 = **m 197.991** | A end (37 ticks after that) = m ~198.53 | B end 0xf80 = **m 204.703** |
> tile pull-back pos 0xfa1 = m 206.486 | fade-to-black trigger 0xfa9 = m 206.905 |
> C end (level 0, about pos 0xfe5) = **m ~210.03** = end of 0x2e8e4 | face overlay gone VT 199.7 = m 200.84.
> Line numbers refer to remake/src/scenes/late.cpp (the scenes moved there from part3.cpp; old line - 289).

Scratch: `scratchpad/re/late3/` (listings `f*.s`, `chain.py` tick simulation, `dump.py` texture/fade dumps in `img/`,
capture frames in `frames/`, `frames2/` with contact sheets `sheet1.png`, `sheet2.png`).

### 0x14760 = rotation matrix from two angles (shared helper, also used by the Web part)

`void rot2(float a, float b, float m[9])` - three **stack** args (pushed b-first order: `push m; push b; push a`),
callee pops 12 bytes (`ret 0xc`). Uses fsin (0x32b72) / fcos (0x32b68). Writes a row-major 3x3:

```
m[0]= cos a*cos b   m[1]= sin a   m[2]= -cos a*sin b
m[3]=-sin a*cos b   m[4]= cos a   m[5]=  sin a*sin b
m[6]= sin b         m[7]= 0       m[8]=  cos b
```
i.e. M = Rz(a)*Ry(b) with Rz=[[c,s,0],[-s,c,0],[0,0,1]], Ry=[[cb,0,-sb],[0,1,0],[sb,0,cb]].
`rot2(0,0,0xa0120)` resets the camera matrix to identity (0x302f9). The camera matrix at 0xa0120 is three rows:
right (0xa0120..28), up (0xa012c..34), forward (0xa0138..40); camera position is 0xa018c/0xa0190/0xa0194 (x,y,z).

Other small helpers met here: 0x3235e = truncate st0 toward zero (sets RC=chop, frndint); 0x1bbec(src,dst,n) = memcpy;
0x187b4(n) = zeroed bump alloc, 0x186e8(n) = non-zeroed bump alloc ("3d engine tempalloc"); 0x10ab4 = 64K alloc.

### Global mechanisms needed to read these parts

**Palette fade (per 70 Hz tick, 0x10124 called from the tick handler before the callbacks).**
L=[0x52918] level, T=[0x5291c] target, S=[0x8eba8] speed, F=[0x52920] float brightness, X=[0x8ebb0] invert flag,
P = 6-bit palette at 0x5faa8:
```
if (L==T) S=0;  if (L<T) L=min(L+S,T);  if (L>T) L=max(L-S,T);
F = (F-1)*0.98 + 1;                          // brightness flashes decay 2%/tick toward 1
c = L>>8;                                    // 0..0x80
c==0x40 : out = min(63, trunc(P*F))
c< 0x40 : out = min(63, trunc((P*c>>6)*F))                 // fade to black
c> 0x40 : w=c-0x41 (0..63), iw=63-w;
          X==0 : out = min(63, trunc((w + (P*iw>>6))*F))     // fade to white
          X!=0 : out = min(63, trunc((((63-P)*w + P*iw)>>6)*F))  // fade to the INVERTED palette
```
0x4000 = normal, 0x8000 = white (or fully inverted when X=1), 0 = black. Speed 100 => 0x8000->0x4000 in 164 ticks (2.34 s).

**Fade tables (*.fad, 16384 bytes = 64 rows x 256, loaded by 0x106a8 into [0x5b67c]).** Rows 0..31 multiply the
colour by r/32 (darken), row 32 is identity, rows 33..63 lighten toward white by (r-32)/31 (row 63 = white).
Checked numerically for pinfx3.fad (weird palette) and 3d.fad (tex3d palette).

**0x1c3f4(src=eax, dst=edx)** (other agent decompiles it): for all 64000 bytes (fixed count, ebx ignored)
`dst = fad[(32 + (src>>3))*256 + dst]` -> "lighten dst toward white by src/256" (screen-like blend, 32 levels).

**Callback slots.** 0x10bd8 accepts 16 slots (dedupes), but the tick handler 0x1bc01 only calls slots 0..7.
I replayed every register/unregister in 0x2e8e4 (and the inits it calls; note 0x23540 registers 0x23384 only with
arg 0x2b, not 0x17). Result: every callback below lands in slots 0..7, so all run. Important: **0x2e798 (the main
script's random F=1.2/0.9 flicker + inverted window on (pos&0xff)>=0xf0) is unregistered at 0x2ef7c and is NOT
active in any sub-part covered here.** 0x2e684 (palette/fade-table swap on [0x1499e8] change) is in slot 7 and active,
but [0x1499e8] stays 0, so it does nothing after the plasma start (palette = 3d palette 0x1496e8, [0x5b67c] = 3d.fad
+ 3d.gst copy, corridor texture = tex3d#1 because [0x57d74]=0).

Before paranoid (0x2ffcb) the callback table (0x40 bytes at 0x8ec04) is saved to the stack and cleared, the 64K
at [0x5b67c] is saved to a temp buffer; after paranoid (0x30246..0x302cb) both are restored and the 3d palette
0x1496e8 is copied back to 0x5faa8. So the plasma state machine (0x2e05c) is frozen during paranoid and resumes in A.

Active callbacks per sub-part (slot order):
- paranoid: 0x16968 (weird time), 0x16b00 (video frame flag), 0x29ce4 (inset size).
- A: 0x28a10 (sheet time [0x57d8c]+=3), 0x2e05c (plasma state machine), 0x16b00, 0x2e838 (overlay fade), 0x2764c (cam ticks), 0x280cc, 0x2e684.
- B: 0x28a10, 0x2e2fc, 0x16b00, 0x2e838, 0x2764c, 0x2e684.
- C: 0x28a10, 0x28a80 (tile ticks), 0x2e240, 0x2764c, 0x2e684.

### Timeline (end positions, verified against the capture)

| sub-part | code | start condition | end test | end pos | end video t |
|---|---|---|---|---|---|
| plasma chain (not mine) | 0x2fbb5/0x2fd3c/0x2fe96 | after star (0xd00) | tick-driven: [0x1493dc] set by 0x2e05c | ~0xe94..0xe9c | ~191.7 (capture: first faint face 191.8) |
| **paranoid** | 0x30088-0x301ea | [0x9b840]=(pos+0x20)&~0x3f = **0xe80** | pos >= [0x9b840]+0x80 | **0xf00** | **196.845** (capture: white at 196.85-196.95) |
| **A** (transition) | 0x302d0-0x304e7 | - | tick-driven: camera z [0xa0194] >= 15000-1000 | ~0xf0a | ~197.38 (37 ticks after 196.85) |
| **B** (sheet chase) | 0x30515-0x306f8 | [0x9b840]=round64 = **0xf00** | pos >= [0x9b840]+0x80 | **0xf80** | **203.566** (capture: inverted at 203.65) |
| **C** (tiles) | 0x3077f-0x308dd | [0x9b840]=round64 = **0xf80** | fade level [0x52918] <= 0 | ~0xfe5 | **~208.9** (capture: black 208.7, VS at 209.0) |
| end of 0x2e8e4 | 0x308ed-0x3098e | frees, 0x26fd8, sets [0x9b840]=round64 | - | ~0xfe5 | ~208.9 |

The dance part then sets [0x9b840]=round64(pos) = **0x1000** (0x31af7), so VS ends at 0x1060 (215.33) and ALEX at
0x1090 (217.85), matching the remake's icons start.

Plasma-chain length (for the other agent / paranoid start): 0x2e3ac waits 400 ticks ([0x5809c] = -500 -> -100) then
[0xa1558] 1.0 -= 0.003/tick -> 333 ticks; 0x2e360 [0x58098] -0.8 += 0.008/tick -> 226 ticks; 0x2e05c ~578 ticks with
camera z0 = 0 (it depends on the camera z at its start: +-50 ticks per +-300 units). Total ~1537 ticks = 21.96 s after
0xd00 -> ~191.9 video at z0=0. The capture shows the paranoid fade-in starting ~191.7, i.e. before the rounding
boundary 0xea0 (191.805): [0x9b840]=0xe80 is confirmed by the end at 196.85. **This rounding is fragile** (a few
tenths of a second): the remake should hard-code 0xe80/0xf00, not derive it.

---

### 3a. Paranoid (0x30088 - 0x301ea; routines 0x16970, 0x16b34, 0x29d1c, 0x1c3f4)

**What it draws.** weird.raw (256x256, teal/blue organic texture) mapped full screen through a moving radial ripple;
the PARANOID.LZW greyscale face (160x96, also rippled, scaled to full screen) "lightens" it through pinfx3.fad (bright
face areas go white, dark areas show the texture); a 1/3-scale colour-inverted thumbnail of the whole frame opens as a
growing window at (32,100)-(138,166) with a white 1-px frame. Fades in from white.

**Setup (0x2ffcb-0x300c5).**
```
save callbacks, clear; save 64K [0x5b67c]
F=1.0; L=0x8000; S=0; VGA palette := all 63 (immediately white); memset(screen,0); T=0x4000
weird_init():  tex=alloc64K; load "weird.raw": palette(768) -> 0x5faa8, 65536 -> tex; add_cb(0x16968)
video_init():  vtex=alloc64K; vbuf=alloc(0x7800); open "paranoid.lzw" (0x12ccc); frame=0; add_cb(0x16b00); newframe=0xff
[0x9b840] = (pos+0x20)&~0x3f            // = 0xe80
load_fad("pinfx3.fad") -> [0x5b67c]
S = 100                                 // fade from white over 164 ticks = 2.34 s
inset_init(mode 0x17, edx=1)            // see 0x29d1c
```
**Frame loop.**
```
weird_render(A=0x5fda8); video_render(B=0x6f7a8);
lighten(B -> A)            // 0x1c3f4: A = pinfx3[(32+(B>>3))*256 + A]
inset_render(A, dst=0x67ac8 /* = A + 100*320 + 32 */);
copy A -> screen;  until pos >= 0xe80+0x80
```
**End (0x301ea).** inset/weird/video freed; F=1, L=0x8000, S=0, VGA palette all white, screen cleared (hard cut to
white at 196.85); restore callbacks, 3d palette, saved [0x5b67c]; T=0x4000.

**Grid warp (0x14920) + 8x8 grid mapper (0x1d0dd)**, shared by weird and video:
```
grid[26][41] of {v (8.8), u (8.8)}, screen point (8i, 8j), i=0..40, j=0..25 (8x8 blocks, 40x25 cells)
warp(cx, cy, U0, V0, A, Fr, Ph):
  r = sqrt((8i-cx)^2 + (8j-cy)^2)                 // screen pixels
  k = 1 + A*sin(Fr*r + Ph)
  v = trunc((v - V0*256)*k + V0*256);  u = trunc((u - U0*256)*k + U0*256)
mapper: bilinear interpolation of (u,v) inside each 8x8 block, nearest texel,
        texel = tex[(v>>8 & 255)*256 + (u>>8 & 255)] (both axes wrap at 256), u fraction rounded with +0x80.
```
- weird (0x16970 render): `u0 = i*65536/41, v0 = j*51200/26` (texels u 0..249.8, v 0..192.3, i.e. ~0.78 texel/px
  horizontally, 0.96 vertically); t1 = [0x9c9b4] (+= 7 per tick in 0x16968, starts at 0, only used here);
  `cx = trunc(80*sin(0.01*t1 + 24) + 160)`, `cy = trunc(50*cos(t1/143 + 34) + 100)` (0.00699300699 = 1/143),
  `warp(cx, cy, U0=128, V0=100, A=0.3, Fr=0.05, Ph=t1/72)`.
  Per second: centre x phase 4.9 rad/s, centre y 3.43 rad/s, ripple phase 6.8 rad/s.
- video (0x16b34 render): when newframe: if frame==0 rewind stream (0x11308(stream,1)); decode luma 160x96
  (0x13174, 15360 bytes, 8-bit) and copy into the 256-stride texture; `frame = (frame+1) % 44` (only frames 0..43 of
  the 45 in the file are ever shown). newframe is set by callback 0x16b00 whenever pos changes to a multiple of 4
  half-rows, i.e. **one video frame every 2 rows = 0.20975 s (4.77 fps), locked to the music**.
  Grid: `u0 = i*0xa000/41, v0 = j*0x6000/26` (the 160x96 frame stretched to full screen);
  t2 = [0x9c9b0] (+= 2 per tick in 0x16b00, starts 0); `warp(160, 100, U0=80, V0=48, A=0.07, Fr=0.02, Ph=t2/172)`
  (0.00581395349 = 1/172 -> 0.81 rad/s). At the ripple maximum it samples a few texels outside the 160x96 area.

**Inset 0x29d1c** (mode 0x17: size f=[0x147398]=0; mode 0x2b would start at 1.0; edx: 1 = inverting LUT, else identity).
```
init: buf = alloc64K; lut = buf[0..255]:
      lut[i] = nearest palette index (sum of squared 6-bit RGB differences, 0x10474) to (63-R_i, 63-G_i, 63-B_i)
      using the palette in 0x5faa8 at init time (= weird.raw palette); add_cb(0x29ce4): f = min(1, f + 0.01) per tick
render(src=A, dst):
  thumb = buf+0x100 (stride 320): for y<67, x<108: thumb[y][x] = lut[src[3y][3x]]   // 1/3 nearest downscale (0x1c0d6)
  W = trunc(106 f); H = trunc(66 f); ox = trunc(53 (1-f)); oy = trunc(33 (1-f))
  blit W x H from thumb(ox,oy) to dst + oy*320 + ox  (dst = screen (32,100)),
  plus a 1-px border of colour 255 (= white in weird.raw) around it (0x1c117); nothing if W or H = 0.
```
So the window opens from its centre (85,133) to the full (32..138, 100..166) = (0.100, 0.500, 0.331 w, 0.330 h of
the frame) in 100 ticks = **1.43 s, linear**, and always shows the matching part of a whole-frame thumbnail
(full face, colours inverted: the teal/blue picture becomes orange/brown).

**Time bases.** t1, t2, f: per 70 Hz tick (callbacks). Video frame: from [0x9b844] (every 4 half-rows).
Palette fade: per tick. Grids are recomputed per rendered frame from those values.

**Differences from the remake (remake/src/scenes/late.cpp).**
1. :141 the base layer is a duotone of the clip; original = weird.raw rippled texture (u,v formulas above), with the
   clip only lightening it via pinfx3.fad (`out = c + (63-c)*level/31`, level = luma>>3). The face's dark areas show
   the texture, not dark blue. Most visible difference.
2. :139 / :146 video rate 12 fps over all frames; original 1 frame per 2 rows (4.77 fps) synced to the music, 44-frame
   loop restarting at frame 0 at the part start.
3. :142-148 inset: original is an inverted-colour 1/3 thumbnail of the composed frame (same frame, not +20 frames;
   colours from the nearest-inverse LUT, not a brown duotone), rect (0.100, 0.500, 0.331, 0.330) instead of
   (0.21, 0.5, 0.22, 0.26), growing linearly from its centre (85/320, 133/200) during the first 1.43 s of the part
   (from 0 size), not 192.9-193.5 smoothstep from a small rect; the original crops (window opens) rather than scales.
   Border: 1 px white, outside the image (remake :145 is close).
4. :137 / :150 start: original fades in from white starting ~191.7 with L linear over 2.34 s (palette mix
   P*(63-w)/64 + w); remake 191.3 smoothstep over 1 s.
5. :151 end: original hard cut to full white at 196.845 (0xf00), not a 0.5 s fade from 196.9.
6. Both layers ripple (weird strongly: +-30 % zoom around a wandering centre; video +-7 %); remake has none.

---

### 3b. Sub-part A (0x302d0 - 0x304e7): transition, ~0.53 s

**What it draws.** The hex corridor and the waving blue sheet (both continuing from the plasma chain), with the
paranoid clip lightening the whole frame at full strength, all fading in from white.

```
setup: z=[0xa0194] = 15000 + (-1500) = 13500; cam x=y=0; camera matrix = identity (rot2(0,0,0xa0120));
       S=100 (but 0x2e05c forces T=0x4000, S=150 every tick -> white fade takes 109 ticks = 1.56 s);
       video_init() again (stream restarts at frame 0; t2 keeps counting); add_cb(0x2e838)
loop:  cam_targets_276f8(); camera_27444(1); corridor_27670(A); spheres_28100(A); flare_28330(A);
       if ([0x1493d8]) sheet_2902c(A);          // set to 1 by 0x2e05c, so the sheet is drawn
       overlay(A);                               // see below
       copy A -> screen
until !(14000 > z)
```
`overlay(A)` (also in B): if k=[0x580bc] > 0: video_render(B); `lut[i]=trunc(i*k)`, i<256; `B[n]=lut[B[n]]` for
65536 bytes (overruns the 64000-byte buffer by 1536); lighten(B -> A) with 3d.fad. k = 1.0 at A start (data init,
only written by 0x2e838), `k -= 0.005` per tick, clamped at 0 -> the face fades out linearly over 200 ticks = 2.86 s
(gone at ~199.7 video, verified: still visible at 198.5, gone at 199.5).

0x2e05c keeps running in its last phase ([0xc1d70] > 1000): every tick `z += w; w += 0.1` (w = [0x580b4] = 5.9 at A
start), `z += v` (v = [0x1493d4] = 6), look target = (0, 0, [0xc1d70] + 15000) (≈16000+), roll rate [0xc1d68] =
0.04*sin(p) rad/tick (p = [0x1493d0] += 0.01/tick, ≈6.15 here), orbit offsets [0xc1d34..44] zeroed, [0xc1d70] += 2,
T=0x4000, S=150. z reaches 14000 after 37 ticks; w ends at ≈9.6. The 0x28100 spheres (radius [0xc1d70]+60.. at the
origin) and the 0x28330 flare are behind the camera and are culled (0x27958 / 0x28330 test camera-space z).

### 3c. Sub-part B (0x30515 - 0x306f8): chasing the waving sheet, 197.38 -> 203.566

**What it draws.** Flight along the hex corridor (floor and ceiling) behind a waving 480x300 sheet textured with the
blue tex3d#2 texture and phong-lit; the camera orbits sideways around the sheet's axis while keeping a constant
distance; the face overlay fades out during the first ~2.3 s.

```
[0x9b840] = round64(pos) = 0xf00; unregister 0x2e05c; spheres_free; add_cb(0x2e2fc)
loop: cam_targets_276f8(); tx=[0xc1d4c]*=3; ty=[0xc1d50]*=1.5;     // per frame, on freshly computed targets
      camera_27444(1); corridor_27670(A); sheet_2902c(A); overlay(A); copy -> screen
until pos >= 0xf80
callback 0x2e2fc per tick: p += 0.01; d = v + w (= 6 + 9.6 = 15.6);
      Z=[0x57d88] += d; z=[0xa0194] += d; look target z [0xc1d48] = Z; roll [0xc1d68] = 0.004*sin(p)
```
Z starts at 15000 (data) and z at ~14007 -> the sheet stays ~990 units ahead; camera speed 15.6 units/tick =
1092 units/s = 3.77 corridor half-heights/s. Total roll over B ≈ 0.4*(cos 6.15 - cos 10.49) ≈ 0.6 rad.

**cam_targets_276f8** (t = [0xc1d6c], += 4.0 per tick by 0x2764c, never reset):
```
R  = 290 + 50*sin(0.0005 t + 64)
tx = cos(3t/1100) * sin(3t/700 + 34) * R + [0xc1d34]      // [0xc1d34], [0xc1d38] are 0 here
ty = sin(3t/1100) * sin(3t/700 + 34) * R + [0xc1d38]
```
(0.000909090909 = 1/1100, 0.00142857143 = 1/700). In B: x amplitude up to ±1020, y ±510 (after *3, *1.5).

**camera_27444(mode)** runs once per pending tick (counter [0xc1d64] incremented by 0x2764c), so it is frame-rate
independent:
```
d = look - campos; dist = |d|
if dist > 0.01:
   n = d * (turn/dist)  (turn = [0x57d80] = 0.05)
   if (mode & 1) axis = n x forward  else axis = 0
   camM = camM * rot3(axis) (0x2721c + 0x18b18)        // turns toward the target, ~0.05 rad/tick * sin(angle)
   if roll != 0: camM = camM * rot3(0, 0, roll)         // roll = [0xc1d68] rad per tick about forward
for k in 0 .. (mode&2 ? 2 : 1):                         // x,y (mode 1) or x,y,z (mode 3)
   vel[k] = damp*vel[k] + 0.002*(target[k] - pos[k]); pos[k] += vel[k]   // damp = [0x57d7c] = 0.79 (0.87 in C)
```
Targets: pos targets [0xc1d4c..54], look target [0xc1d40..48], velocities [0xc1d58..60].

**corridor_27670** (0x27014 grid + 0x1cddc mapper; also used by other parts): 41x26 rays, ray(i,j) = 37.5*forward +
(i-20.5)*right + (j-13)*up (focal 300 px); hit the plane y=+290, or y=-290 if that t is negative; texel = hit (x,z)/2
(tex3d#1 purple hex, 256 texels per 512 units, wraps); shade = clamp(307200/t, 0x100, 0x2000) -> 3d.fad row
shade>>8 (1..32): full brightness up to ~1406 units ahead, then ∝ 1/distance. Interpolated per 8x8 block.

**sheet_2902c** (render; init loads phong1.raw (256x256 light map) to [0x5b670], add_cb(0x28a10): T=[0x57d8c] += 3
per tick, data init 523):
```
for j=-10..10, i=-16..16 (21 x 33 = 693 vertices, 1280 triangles):
   Ta = 0.0164375*T;  Tb = 0.013*T
   h  = 60 * sin(i/7 + Ta) * cos(0.2 j - Tb + 34)
   pos = (15 i, 15 j, Z + h)                       // 480 x 300 sheet at depth Z=[0x57d88]
   gx = 60*cos(0.2j - Tb + 34)*sin((10i+1)/70 + Ta) - h
   gy = 60*sin(i/7 + Ta)*cos(0.02(10j+1) - Tb + 34) - h
   nrm = (gx, gy, 1)/sqrt(1 + gx^2 + gy^2)         // forward-difference normal
project (0x26524): screen = (160 + 300 x/z, 100 + 300 y/z); env coords = 128 + 126 * (camera-space normal x, y);
                   triangles bucket-sorted back to front, bucket = clamp((z-120)/12, 0, 99)
draw (0x285d8 -> 0x1e223): texture tex3d#2 ([0xc1d24], blue plasma), u = 128 + 0.390625 x, v = 128 + 0.625 y
     (central 188x188 texels, no wrap), lit by the env coords through phong1 / 3d tables
```
Wave speeds: 3.45 rad/s along x (wavelength 44 columns = 660 units), 2.73 rad/s along y (wavelength 31 rows = 471).

### 3d. Sub-part C (0x30708 - 0x308dd): the sheet bursts into tiles, 203.566 -> ~208.9

**What it draws.** Same corridor, looking straight ahead; the sheet is replaced by 640 flat 15x15 tiles (hex
texture) that swirl outward, stretch horizontally, tumble and then rush back past the camera. Palette starts fully
INVERTED (purple/blue -> green/yellow) and fades back toward normal, then everything fades to black.

```
unregister 0x2e2fc; video_free; unregister 0x2e838
L = 0x8000; X=[0x8ebb0] = 1 (inverted mode); S = 70; T = 0x4000; [0x1473b8] = 1
tiles_init_28c64(); roll/orbit offsets [0xc1d34..3c] = 0; add_cb(0x2e240)
[0x9b840] = round64(pos) = 0xf80
loop: cam_targets_276f8();   // immediately overwritten by:
      cam_targets_28aa0();   // tx=cos(t/1100) sin(t/700+34) R2, ty=sin(t/1100) sin(t/700+34) R2,
                             // tz=Z + 2 R2 cos(t/700+34), R2 = 300 + 50 sin(0.0005t+64);
                             // look = (0, 0, Z); damp [0x57d7c] = 0.87
      camera_27444(1); cam x = cam y = 0 (forced every frame -> tx/ty/tz have no visible effect)
      corridor_27670(A); tiles_render_28c64(A)
      if (pos > 0xf80 + 0x28) { T = 0; S = 100; }      // fade to black, checked per frame
      copy -> screen
until L <= 0
callback 0x2e240 per tick: p += 0.01; Z += 15; z += 14; look z = Z; roll = 0.004*sin(p);
      if (pos > 0xf80 + 0x20) { I = trunc(I + a); a += 0.4; }   // I=[0x13efbc], a=[0x1493ac], both start 0
```
X is never cleared during C (0x2e798 is not active), so the first ~2.15 s show the inverted palette mixing back
toward normal (L 0x8000 -> 22268, i.e. still 1/3 inverted, looks grey at 205.0), then L falls 100/tick through
normal (0x4000 at ~206.6) to black. Fade-to-black trigger at pos 0xfa9 ≈ 205.7 video; L reaches 0 ≈ 373 ticks after
the C start -> ~208.9 video (pos ~0xfe5). Tile pull-back starts at pos 0xfa1 ≈ 205.25.

**tiles_init (0x28c64(0x17))** - 640 records of 44 bytes in a zeroed buffer:
```
for j=-10..9, i=-16..15:              // 20 x 32 tiles covering the sheet exactly
   p = (15 i, 15 j, 60 * cos(0.2 j - 0.013 T + 34) * sin(i/7 + 0.0164375 T))   // same T as the sheet: seamless
   angles a = b = 0
   wa = (rand()-0x4000) * 3.3333e-6;  wb = (rand()-0x4000) * 3.3333e-6       // ±0.0546 rad/tick
   phase = rand() * 3.3333e-4 (0..10.9);  freq = 0.1 / (30 + 0.001*rand())   // 0.0016..0.0033 rad/tick
   swirl s = sin(1e-4 (i^2+j^2)), c = cos(1e-4 (i^2+j^2))
vertex/triangle buffers: 4 vertices, 2 triangles per tile (2560 / 1280); add_cb(0x28a80): n=[0x57d90]++ (data
init 45), pending++.
```
**tiles_update (0x28b54)**, run once per pending tick:
```
for each tile:
   (x, y) = (c x - s y, s x + c y)      // rotate the anchor about the sheet axis; corner tiles 0.0356 rad/tick
   x *= 1.007                           // horizontal spread only (y is not scaled)
   z += 3 + 0.3 sin(n*freq + phase)
   a += wa; b += wb
if [0xa12b8] <= 0: [0xa12b8] = 50      // purpose unknown
```
**tiles_render**: M = rot2(a, b); v0 = (x, y, z + Z - I), v1 = v0 + 15*M.row0, v2 = v0 + 15*(row0+row1),
v3 = v0 + 15*row1; normal = M.row2 (only stored on v0); project (0x26524); draw (0x287c4 -> 0x1e223) each triangle
with texture tex3d#1 ([0xc1d20], hex) addressed **planar from the current world position** u = 128 + 0.78125 x,
v = 128 + 1.25 y (so the texture slides across tiles as they move), and **flat** env shading (all three corners get
v0's env coords).
Relative motion: tiles move +18 - a per tick, camera +14 -> they recede 4/tick until 205.25, then a grows 0.4/tick
and the tiles (≈1460 units ahead) pass the camera ~96 ticks (1.4 s) later, ≈206.6 video.

### 3e. Differences: FaceFloor (late.cpp:156-191) vs A+B+C

1. :172-186 the "card" is the paranoid clip on a spinning quad until 204. Original: the card is the waving
   480x300 sheet with the **blue tex3d#2 texture** (height field and phong as above, no face); the face is only a
   full-screen lighten overlay that fades out linearly from 196.85 to ~199.7.
2. Missing tile burst (203.57 onward): 640 hex-textured tiles swirling (anchor rotation 1e-4(i²+j²) rad/tick),
   stretching x by 1.007/tick, tumbling, receding, then rushing back past the camera from 205.25.
3. :187 the green-white flash is really the **inverted palette** ([0x8ebb0]=1, L=0x8000) at 203.566 fading at
   70/tick toward normal (only to 1/3), then :169 fade to black from ≈205.7 at 100/tick, black at ≈208.9 (remake
   dims to 0.2 between 205.5 and 208.0; timeline.cpp:29 ends the scene at 208.4, should be ~208.9).
4. :160/:188 start: original A starts at 196.845 fully white and fades at 150/tick (1.56 s); remake 197.4 + 0.4 s.
5. :162-165 camera: original B speed 1092 units/s (3.77 corridor half-heights/s; C 980 u/s = 3.4 hh/s) vs remake
   1.5 hh/s; original keeps the sheet ~990 units (3.4 hh) ahead at screen centre while the camera position
   spring-follows a sideways orbit (x ±1020, y ±510 units, period ~8 s) - the sheet is seen strongly obliquely;
   roll is small (≈0.6 rad over B, ≈0.17 rad in C, rate 0.004 sin(p) rad/tick) vs remake 0.6 + 0.25 t (2.75 rad).
   In C the camera is centred (x=y=0) and looks straight ahead.
6. :166-168 corridor: planes at y=±290, hex texture 512 units per repeat (1.77 half-heights), brightness 1 up to
   1406 units (4.85 hh) then ∝ 1/distance through 3d.fad (remake: fogDist 5, texScale 0.35 in its own units).

### What I could not determine
- Exact paranoid start: the plasma chain is tick-driven (~1537 ticks after 0xd00, ±50 depending on the camera z
  when 0x2e05c starts); capture says ~191.7. Only matters for the white hold before the fade-in.
- Internals of the polygon fillers 0x1e223 / 0x285d8 (how phong1.raw and 3d.gst combine with the texture) and of
  0x1cddc's shading; 0x287c4 issues a second fill per triangle with permuted vertices (0x289e5 loop) that I did not
  fully decode. [0xa12b8] (forced to >= 50 by the tile update) is read elsewhere; purpose unknown.
- Output range of the video decoder clamp table 0x978d0 (assumed 0..255 luma, as in the remake's decoder).

---

## 4. Dance, Icons, Outro

> **Times in this section are pos.py "video" times (VT), as written by the sub-agent; VT matches the capture here.**
> Music seconds (m): dance [0x9b840]=0x1000 (m 211.415); start about VT 208.9-209.1 = m ~210.03-210.23 |
> VS end 0x1060 = **m 216.449** | ALEX end 0x1090 = **m 218.966** | icons images at 0x10c0 m 221.483,
> 0x1100 m 224.839, 0x1140 m 228.195, 0x1180 (MC-303) m 231.551 | blur-outs 0x10f4 m 224.210, 0x1134 m 227.566,
> 0x1174 m 230.922 | white fade-in done VT 220.19 = m 221.31 | music ends m 244.87 (VT 243.78) | screen black
> VT 253.46 = **m 254.53** | icons loop exits VT 253.83 = **m 254.90**. After that: black until a key is pressed.
> Line numbers refer to remake/src/scenes/late.cpp (moved from part3.cpp; old line - 289) and to the current
> timeline.cpp.

Scratch: `scratchpad/re/late4/` (function listings `f*.s`, `lyrics.txt`, capture frames `tile*.png`,
`final.png` = final.raw as an image, `an.py`/`an.txt` = capture brightness per frame).
All times below are video times (VT) computed with pos.py, and checked against the capture
(`Acme - 303 ... .mp4`, 30 fps) where stated. Ticks = vertical-retrace timer callbacks (MIDAS `tmrSyncScr`,
0x371ec/0x374d0), 70.086 Hz in mode 13h.

### 4.0 Shared machinery used by these parts (only what matters here)

**Palette fade (per tick, 0x10124 called from the timer 0x1bc01):**
```c
// level=[0x52918], target=[0x5291c], speed=[0x8eba8], bright=[0x52920] (float), whiteMode=[0x8ebb0]
if (level == target) speed = 0;
if (level < target) level = min(level + speed, target);
if (level > target) level = max(level - speed, target);
bright = (bright - 1.0f) * 0.98 + 1.0f;          // flash multiplier decays to 1
setpal(level):                                    // 0x1019c, base palette at 0x5faa8 (6-bit)
  L = level >> 8;
  L == 64: out = min(63, trunc(p * bright));
  L <  64: out = min(63, trunc(((p * L) >> 6) * bright));        // fade to black, linear in DAC units
  L >  64: k = L - 65; out = min(63, trunc((k + ((p * (63 - k)) >> 6)) * bright));  // to white (whiteMode==0)
```
So `level 0x8000 -> 0x4000` = fade from white, `0x4000 -> 0` = fade to black; duration = 0x4000/speed ticks.

**Part position:** `[0x9b844] = 2*(order*64+row) - 2 + [0x149a04]`. Nothing in the code writes `[0x149a04]`
(only read at 0x133e4), so the position is always even and changes once per row.
`[0x9b840] = ([0x9b844] + 0x20) & ~0x3f` (low byte only, i.e. round to 32 rows).

**Start of this section (end of 0x2e8e4, brief):** the last sub-part of 0x2e8e4 (loop 0x3079a–0x308d7) starts at
`[0x9b840]=0xf80` (VT 203.57) with level 0x8000, target 0x4000, speed 0x46 (white fade-in); once
`[0x9b844] > [0x9b840]+0x28` (pos 0xfaa, VT 205.77) every frame sets target 0, speed 100; the loop runs while
`level > 0`. Level at 205.77 is about 0x8000 - 70*154 = ~22000, so the loop ends ~220 ticks (3.1 s) later,
**VT ~208.9**. Capture: black at 208.8–209.0, dance flash at ~209.1. (Exact value depends on load time;
see the parent's notes for 0x2e8e4.)

---

### 4.1 Dance: vs.lzw then alex.lzw (main script 0x31a13–0x31ffb)

**What it draws:** full-screen YIQ video clips (VS: the dancer; ALEX: people at PCs), 160x96 decoded, shown 2x
with a 4x4 ordered dither into a 256-colour YIQ palette, with the 0x2a34c lyric overlay combined on top.

**Pseudo-C (inlined video "effect"; the `==0x17`/`==0x48` branches in the loop are dead copies):**
```c
// ---- setup VS (0x31a13)
fx2a34c(0x17);                     // overlay init (clears [0x1473b8])
[0x1473b8] = 1;                    // overlay uses the *sung lyric* table (see below)
[0x580c0]  = 7;                    // video period: 7+1 ticks per frame
open_video("vs.lzw");              // 0x12ccc: open LZW stream, build dequant/IDCT + dither tables
yiq_palette();                     // 0x13244 -> 0x5faa8 and DAC (formula below)
Ybuf  = tempalloc(0x3c00);         // [0x1493b8]  (0x186e8 = bump allocator)
IQbuf = tempalloc(0x3c00);         // [0x1493bc]
frame = 0;                         // [0x1493c0]
add_tick_callback(0x30990);
want = 1;                          // [0x149a08]
load_fad("truecol.fad");           // used by the overlay combine
bright = 1.0; speed = 0; level = 0x8000; DAC[all] = 63;     // white
memset(screen, 0, 64000);
speed = 1000; target = 0x4000;     // white -> normal in 17 ticks (0.24 s)
memset(B = 0x6f7a8, 0, 64000);
[0x9b840] = ([0x9b844] + 0x20) & ~0x3f;                      // = 0x1000 in practice (see times)
do {
    fx2a34c(A = 0x5fda8);          // overlay layer (other agent)
    if (want) {
        want = 0;
        if (frame == 0) lzw_restart(stream, 1);              // 0x11308: rewind to first frame
        frame = (frame + 1) % 200;
        decode_plane(Ybuf, 160x96);                          // 0x13174: 0x3c00 bytes LZW -> 4x4 DCT (0x1b648)
        decode_planes(IQbuf, 2 x 80x48);                     // 0x131ac: I at +0, Q at +0xf00
    }
    dither_yiq(Ybuf, IQbuf, B + 2*320);                      // 0x1b819, every frame
    fx1c3f4(A, B, 64000);          // combine overlay A into B (other agent)
    memcpy(screen, B, 64000);      // 0x1bbec
    music_service();               // 0x332a4, 0x1259c
    if (kbhit()) quit_demo();      // prints [0x9b844] with "%x"
} while ([0x9b840] + 0x60 > [0x9b844]);

// ---- switch to ALEX (0x31d03): same setup without the overlay init and without recomputing [0x9b840]
remove_tick_callback(0x30990);
[0x580c4] = "alex.lzw"; [0x580c0] = 7; open_video; yiq_palette; 2 new tempallocs; frame = 0;
add_tick_callback(0x30990); want = 1; load_fad("truecol.fad");
bright = 1.0; level = 0x8000; DAC = white; speed = 1000; target = 0x4000;   // second 0.24 s white flash
memset(screen,0,64000); memset(B,0,64000);
do { ...same body... } while ([0x9b840] + 0x90 > [0x9b844]);
remove_tick_callback(0x30990); fx2a34c(0x48);

// ---- tick callback 0x30990 (70 Hz)
if (cnt == 0) { cnt = [0x580c0]; want = 1; } else cnt--;    // cnt=[0x1493b4], never reset between clips
```
**Frame rate:** one decode every `[0x580c0]+1 = 8` ticks = **8.76 fps** (decode only when `want`, no catching
up). HARL uses the data default `[0x580c0]=5` -> 6 ticks = 11.68 fps (note for the Harl owner). Verified on the
capture: picture changes every 3–4 capture frames (0.10–0.13 s) in both clips.

**Frame counts (decompressed size - 1) / 23040:** VS.LZW 147, ALEX.LZW 43, HARL.LZW 200. The counter wraps at 200
(rewind at 0), irrelevant here: VS shows only frames 0..~55, ALEX frames 0..~21.

**Dither/output (0x1b819):** 80x48 chroma blocks -> 4x4 output pixels; each Y sample -> 2x2 pixels.
Output 320x192 written at rows 2..193 of the 320x200 buffer (2 black rows on top, 6 at the bottom).
```c
for (by = 0; by < 48; by++) for (bx = 0; bx < 80; bx++)
  for (r = 0; r < 4; r++) {                       // output row 4*by+r, source Y row 2*by + (r>>1)
    uint32 c = tabIQ[r][I[by][bx] >> 2] >> 2 | tabIQ[r][Q[by][bx] >> 2];  // tables 0x979d0 + 0x100*r
    for (x = 0; x < 4; x++) {                     // source Y col 2*bx + (x>>1)
      v = Y + D[r][x]; Y4 = v > 255 ? 15 : v >> 4;
      out = Y4 + byte x of c;                     // index = Y4 | I2<<4 | Q2<<6
    }
  }
D = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}}   // 4x4 Bayer
```
The 2-bit chroma dither tables at 0x979d0..0x97dcf are built by 0x12ccc (not decoded).
**Palette (0x13244):** `Y=(c&15)*4, I=((c>>4)&3)*16, Q=((c>>6)&3)*16;
R=1.003Y+0.9548I+0.6178Q, G=0.9967Y-0.27I-0.644Q, B=1.0084Y-1.11I+1.6995Q`, truncated, clamped 0..63
(the remake's assets.cpp matrix matches).

**Overlay text in the dance:** 0x2a34c with `[0x1473b8]=1` takes the word from table 0x57f30, chosen when the
current (order,row) equals an entry of the pair tables 0x57e8c (order) / 0x57de8 (row) (orders 21/25/27 are
treated as 17, 22/26/28 as 18). Entries hit in this part (sN = the N-th word hit, from table 0x57f30; the words themselves are not copied into
these notes; order/row, VT of the trigger):
`s0` 31/58 209.55, `s1` 31/62 209.97, `s2` 32/7 210.92, `s3` 32/16 211.86, `s4` 32/24 212.70,
`s5` 32/28 213.12, `s6` 32/38 214.17, `s7` 32/50 215.43, `s8` 32/58 216.27.
Capture confirms s0 (209.7), s1 (210.7), s2 (211.1–211.6), s3 (212.2), s4 (213.0).
(The full table is at 0x57f30. How the word is drawn is 0x2a34c's business.)

**Time base:** frame advance = tick callback (8 ticks); dither/combine = per rendered frame; fades = tick;
part end = position.

**Times:** VS from the end of 0x2e8e4 (~208.9; capture ~209.1) to `[0x9b844] >= 0x1060` = **215.33**;
ALEX to `>= 0x1090` = **217.85** (capture: white flash at 215.4, icons white at 218.0).
`[0x9b840]` must be 0x1000: the dance starts after pos 0xfe0 (208.61), and only 0x1000 gives the observed ends
(0xfc0 would end ALEX at 214.49).

**Differences from the remake (late.cpp Dance, timeline.cpp):**
1. late.cpp:199–202 plays both clips at 19.5 fps and all 147 VS frames before switching. Original: 8.76 fps
   (8 ticks/frame), VS frames 0..~55 then ALEX frames 0..~21; switch at 215.33, not 208.4+147/19.5 = 215.94.
2. timeline.cpp:28–29 dance starts 208.4; original ~208.9–209.1 (after facefloor's fade-to-black loop).
3. late.cpp:203 fades in from black over 0.5 s; original is a 0.24 s white flash (level 0x8000->0x4000 at
   1000/tick) at the VS start **and again at the switch to ALEX** (missing in the remake).
4. late.cpp:204 white flash at 217.4–217.9 does not exist in the dance; the white is the icons part's
   2.34 s fade from white starting at 217.85.
5. Picture geometry: original 320x192 at y=2 (black rows 0–1 and 194–199); remake stretches to full frame.
   Original has the 4x4 ordered dither and 16 luma / 4x4 chroma levels (README already notes no dithering).
6. Lyrics (timeline.cpp:58–63): missing s0 (209.55) and s1 (209.97); s2 should start 210.92 (not
   210.10); s4 (212.70) and s5 (213.12) are separate words; s6 214.17 (not 213.80);
   s7 215.43; s8 216.27.

---

### 4.2 Icons + MC-303 (main loop 0x320be, routine 0x2a840 "final.raw", callback 0x2a6d8)

**What it draws:** a static 7x5 grid of dim hand-drawn icons that are randomly re-shuffled, over which four
images from final.raw are blurred in and out one after another, centred vertically: "VIC ☼ ACME",
"STATIX ☞ PSYCHIC LINK", "vic@luna.nl ☎ ajbe2@cam.ac.uk", and finally the full-screen MC-303 photo.
Everything goes through a feedback/echo pass with random brightness pumping and random negative flashes.
The remake's separate "outro" is just the fourth image of this routine.

**final.raw** = 320x350, 8-bit grey (no palette; palette is a grey ramp). Layout (see late4/final.png):
rows 1–31 credit line 0, rows 32–63 line 1, rows 70–109 icon strip, rows 124–149 line 2 (e-mails),
rows 150–349 MC-303 photo (the tilt and the dark rounded vignette are in the photo itself).

**Tables (data):**
```
iconX[8] @0x57fd4 = {3, 48, 99, 140, 177, 228, 272, 293}   // source x in rows 70..109
iconW[8] @0x57ff4 = {37, 43, 34, 29, 42, 37, 14, 25}
iconOff[k] = (45 - iconW[k]) / 2 = {4, 1, 5, 8, 1, 4, 15, 10}   // built at init into 0x13efd8
line[4]  @0x58014 = {row0,h} = {1,31}, {32,32}, {124,26}, {150,200}
```
**Init (0x2a840(0x17)):**
```c
for i in 0..255: T1[i] = trunc(i * 0.65);                 // 0x1473bc (shared with 0x2a34c)
img  = tempalloc(0x1b580); tmp = tempalloc(0x1b580);      // [0x13efc8], [0x13efd0]
btab = tempalloc(0x4000);  comp = tempalloc(0xfa00);      // [0x13efd4], [0x13efcc]
for c in 0..255: pal[c] = (c/4, c/4, c/4);                // 0x5faa8, grey ramp
load "final.raw" into img;
idx = -1;                                                 // [0x1475e0]
for i in 0..33: cell[i] = rand() & 7;                     // 0x1475bc; cell[34] keeps its BSS value 0
blurDstStride = 320;                                      // 0x1c9d4 patches 0x1c9c6
e = 0xc0; eTarget = 0;                                    // [0x1475e4], [0x1475e8]
add_tick_callback(0x2a6d8);
```
**Main script around it (0x3203f–0x321cc):**
```c
fx2a840(0x17);
level = 0x8000; speed = 0; bright = 1.0; DAC = white; memset(screen,0,64000);
[0x9b840] = 0; target = 0x4000; speed = 100;              // white -> normal over 164 ticks = 2.34 s
B = 0x6f7a8;                                              // NOT cleared: last dance frame decays in the feedback
do { fx2a840(B); memcpy(screen, B, 64000); music_service(); if (kbhit()) quit_demo(); }
while (level > 0);
fx2a840(0x48);                                            // removes callback 0x2a6d8
```
**Render (0x2a840(dst)):**
```c
memset(comp, 0, 64000);
for (r = 0; r < 5; r++) for (c = 0; c < 7; c++) {         // cells 45x40, x = 45c, y = 40r (5 px right margin)
    k = cell[r*7 + c];
    // 0x1c01e: 40 rows, width rounded up to 4, each byte p>>3 (0..31)
    copy_dim(img + 70*320 + iconX[k], comp + r*40*320 + 45*c + iconOff[k], iconW[k]);
}
if (idx >= 0) {
    s = e / 3;                                            // C division
    if (s < 10) s += 3 * (int8)J[0];                      // J = 0x9c630.., random, see callback
    if (s < 64) {
        n = abs(s) + 1;
        step = (0x10000 - 1000*n) / n;
        for (i = 0; i < 0x4000; i++) btab[i] = min(i*step, 0xffffff) >> 16;   // = min(255, i*step>>16)
        // 0x1c91d: horizontal box blur, n taps, window starts at x - n/2 (reads across row ends)
        for (y = 0; y < h[idx]; y++) { sum = Σ src[x-n/2 .. x-n/2+n-1]; tmp[y][x] = btab[sum]; }
        y0 = 100 - h[idx]/2;                              // 85, 84, 87, 0
        for (i = 0; i < h[idx]*320; i++) comp[y0*320+i] = max(comp[y0*320+i], tmp[i]);   // lighten
    }
}
// brightness/negative map, rebuilt every frame
g = 1 + 0.2 * abs((int8)J[7]);                            // byte 0x9c637
for (i = 0; i < 256; i++) {
    v = min(255, trunc(i * g));
    w = trunc(v*(1-f) + (255-v)*f);                       // f = [0x1473b0]
    map2[i] = clamp(w, 0, 255);                           // 0x1474bc
}
if (idx > 2) { speed = 10; target = 0; }                  // MC-303 shown: start the long fade to black
// 0x1c058: feedback into the persistent frame buffer
for (i = 0; i < 64000; i++) dst[i] = map2[min(255, T1[dst[i]] + (comp[i] >> 1))];
```
Effect of the blur table: output = floor(sum*step/65536) ~ average * (1 - 0.01526*n), so a wide blur is also
dark (n=64 -> 2%, n=10 -> 85%, n=1 -> 98.5%). Feedback steady state = comp/(2*0.35) = 1.43*comp
(saturates for comp >= 178); icons end up at ~18% of their source grey. With gain g=1.4 the steady state is
7.8*comp, with g=1.8 the loop is unstable and the screen saturates to white within a few frames.

**Callback 0x2a6d8 (every tick):**
```c
if ([0x9b844] != last) {                                  // once per row
    last = [0x9b844];
    reroll_jitter();                                      // 0x15494: J[0..31] (0x9c630..) = trunc(0.7*tab[rand()&15])
                                                          // tab@0x52f10 = {0,0,1,1,-1,-1,2,-2,6,-4,2,1,4,-1,-6,-2}
                                                          // -> each J: 0 (p 8/16), +1 (2/16), -1 (2/16),
                                                          //    +4, -2, +2, -4 (1/16 each)
    if ((last & 3) == 0) {                                // every 2 rows
        if ((rand() & 0x1f) == 0) f = (f > 0.5) ? 0 : 1;  // random negative flash, p = 1/32
        for (i = 0; i < 16; i++) { v = rand() & 7; cell[(rand()%5)*7 + rand()%7] = v; }
    }
    if ((last & 0x3f) == 0 && idx < 3) { idx++; eTarget = 0; e = 0xc0; }   // next image, starts at s=63
    if ((last & 0x3f) > 0x32 && idx < 3) eTarget = 0x400;                    // blur out
}
if (e < eTarget) e += 3;  if (e > eTarget) e -= 2;
if (f > 0) f -= 0.021;  if (f < 0) f = 0;                 // flash decays over 48 ticks (0.68 s)
```
rand() is Watcom's LCG (`x = x*0x41c64e6d + 0x3039; (x>>16)&0x7fff`, 0x34974), shared with everything else.

**Time base:** e/eTarget, f decay, palette fade = per tick; idx, blur-out trigger, shuffle, flash trigger,
jitter J = per position change (row); icon copy, blur, feedback 0.65/half-add, map2 = per rendered frame
(no vsync wait in this loop, so the trail length depends on render speed).

**Times (VT):** part start 217.85 (white, fades to normal by 220.19; capture 218.0 -> 220.2).
Blur-in = 95 ticks (1.36 s) from n=64 to sharp, jitter active once s<10 (after 81 ticks);
blur-out starts at pos&0x3f = 0x34 and is invisible after 64 ticks (0.91 s).
| image | rows | y0 | appears (pos) | sharp | blur-out | gone/replaced |
|---|---|---|---|---|---|---|
| VIC ☼ ACME | 1–31 | 85 | 220.37 (0x10c0) | 221.72 | 223.10 (0x10f4) | 224.01 (line 1 takes over at 223.73) |
| STATIX ☞ PSYCHIC LINK | 32–63 | 84 | 223.73 (0x1100) | 225.08 | 226.46 (0x1134) | 227.37 (replaced 227.09) |
| e-mail line | 124–149 | 87 | 227.09 (0x1140) | 228.44 | 229.82 (0x1174) | replaced 230.45 |
| MC-303 photo | 150–349 | 0 | 230.45 (0x1180) | 231.80 | never | fades with the palette |

From 230.45 the palette fades linearly from L=64 to 0 at 10/tick (1639 ticks, one 1/64 step every 25.6 ticks =
0.365 s): screen black (L=0) at **253.46**, loop exits at **253.83**. The song ends at music 244.87 = VT 243.78;
after that `[0x9b844]` stops changing, so shuffle/jitter/flash triggers stop and the image freezes while it fades.
Capture check: frozen image from 243.9, brightness steps every 0.33–0.40 s, black from ~252.4 (the last ~4
levels are below the capture's black level).

**Differences from the remake (late.cpp Icons/Outro, timeline.cpp):**
1. late.cpp:219–229: scrolling, tiled rows of the whole icon strip (rows 71–107). Original: static 7x5 grid,
   one icon per 45x40 cell (table above, source rows 70–109), 16 random cells re-assigned every 2 rows; no
   scrolling; icons at p/8 then ~1.43x feedback (~18% grey), no tint (late.cpp:228 tints 0.75/0.75/0.8).
2. late.cpp:221 brightness ramp 224–226 does not exist; brightness instead pumps per row by gain
   1+0.2|J[7]| (1, 1.2, 1.4, 1.8) through the feedback, which saturates the whole screen on 1.8 rows.
3. late.cpp:232 source rows wrong: {1,26},{35,61},{125,148} -> {1..31},{32..63},{124..149}; images are drawn
   1:1 at full width (320 px), centred at y = 100 - h/2.
4. late.cpp:235–241: slide-in from the right with 7 smeared copies and a black band (late.cpp:238). Original:
   no motion, no band; a horizontal box blur shrinking from 64 px to 1 px over 1.36 s while brightening
   (factor 1-0.0153n), jitter of 3*J[0] px when sharp, and blur-out 0.91 s before the next image.
5. late.cpp:232 times {221.0–224.3},{224.4–227.6},{227.6–229.6} -> table above (220.37 / 223.73 / 227.09 starts).
6. late.cpp:216–217: one fixed negative flash at 229.6–230.4. Original: random negative flashes (p=1/32 every
   2 rows, f=1 decaying over 0.68 s, v' = v(1-f)+(255-v)f) during the whole part until the song ends.
7. No feedback in the remake: original `dst = map2[min(255, trunc(0.65*dst) + comp/2)]` every frame, starting
   from the last dance frame.
8. late.cpp:243 0.7 s white flash; original 2.34 s linear (DAC) fade from white, 217.85–220.19.
9. timeline.cpp:30–31: icons/outro split at 230.6 -> 230.45; whole part ends 253.46 (black) / 253.83 (loop end).
10. Outro (late.cpp:254–257): zoom 1.12+0.012*local and source from row 155 -> original rows 150–349 at 1:1,
    no zoom, no rotation, no tint (late.cpp:257 tints 0.82/0.86/0.95); the dim icon grid stays visible
    through dark parts of the photo (lighten), and the photo is blurred in like the text lines.
11. late.cpp:258–272 vignette shader: not in the code; the dark rounded corners are part of final.raw.
12. late.cpp:273–275 flashes at 230.6–231.6 (white), 235.6–236.2, 237.6–238.4: original has the blur-in at
    230.45–231.80 and random negative flashes / gain pumping instead of fixed ones.
13. late.cpp:276 smoothstep fade 242.0–252.5 -> linear palette fade 230.45–253.46 in 64 steps; also freeze
    (no jitter/flicker) from 243.78.

---

### 4.3 "Outro" 0x10dc4 and the end of the main script (0x32215–0x3232f)

0x10dc4 is **not** a visual: it closes the data library (`if ([0x8ec50]) { [0x8ec50]=0; fclose([0x8ec48]);
free([0x8ec4c]); [0x8ec4c]=0; }`, same code as the start of 0x10e00 "open mustard.pak").
```c
// after the icons loop
part_cleanup();  part_cleanup();   // 0x332a4, 0x133b0, [0x9b840]=round, tempalloc reset 0x18898,
                                   // 0x10bac(0), clear all tick callbacks 0x10c0c(0), [0x5d698]=0, flush keys
close_library();                   // 0x10dc4
while (kbhit()) getch();
while (!kbhit()) { music_service(); }      // 0x332a4 + 0x1259c: black screen (palette level 0), music
                                           // (already finished at 243.78) until a key is pressed
while (kbhit()) getch();
stop module (0x34874), timer sync off (0x374d0(rate,0,0,0)), text mode (0x10c34(3)),
printf "midas free module...\n", free module (0x37b00), "midas close...\n", MIDAS close (0x344dc),
"midas done\n", "stop...\n", "nothing...\n"; return.
```
So after VT 253.83 the original shows a black screen until a key is pressed (the capture is cut at 253.33).
Remake difference: timeline.cpp:31 outro scene 230.6–253.4 should be part of the icons routine (see 4.2);
no separate visual outro exists.

### 4.4 Not determined
- The exact dance start (depends on 0x2e8e4's final fade level and load time; ~208.9 computed, ~209.1 on capture).
- The 2-bit chroma dither tables 0x979d0.. (built in 0x12ccc) and the 4x4 DCT details (0x1b648) — the remake's
  float decoder already covers the codec.
- What 0x2a34c draws and how 0x1c3f4 combines it (other agents); only the lyric selection is listed above.
- Render frame rate of the original machine, which sets the per-frame feedback trail length in the icons part.
