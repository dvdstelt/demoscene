# Scope "title": title text, "the end", closing logo

All addresses are code32 offsets (image offset = code32 + 0xd00). "Tick" = 70.0018 Hz IRQ0 tick, "counter" = music
counter [0x1ea8b]. Tick numbers are those of full.frames / full.ticks (the reference capture).

Everything below was checked bit-exactly: a Python model (scratch: `re-title/model.py`, `check1..6.py`) reproduces
the font, glyph sprites, texture, shade tables, palette, the warp grid, the warped background, the letter compositing,
the logo buffer, the "1996" buffer (incl. blur counts), the logo composite and the final VGA image from emulator dumps
taken at frame start (hooked copy of run_paper.py, re-runs identical to full.frames).

## 0. Summary of what is on screen

| part | ticks | counter | routine / callback | picture |
|---|---|---|---|---|
| title | 104–1078 (setup 104–106, first frame 106) | 0..116 | 0x2829 / 0x27ae | 256x256 cloud texture domain-warped by a slowly moving smooth random field, tinted with a hue that rotates once per 1024 ticks (green → teal → blue → purple → mauve → olive; it is *not* just dark blue); "psychic link" / "sixty four k" appear letter by letter on music beats, white with coloured ghost fringes and a black drop shadow, shaking every music step; fades to **white** at the end |
| the end | 10968–11376 (first frame 10970) | 1293..1342 | 0x2829 / 0x27ae | same effect, horizontal warp scale 1/4 (broader bands), hue starts brown-olive and rotates to teal; "t h e" / "e n d" all visible from the start; fades to white |
| logo | 11377–12234 | 1342..1415 | 0x13d6 / 0x136c | from white: a huge "psychic link" box logo zooms out (10x → 0.5x) over a very blurred "1996", mauve tint; the logo dims away (gone from tick ~11852), the "1996" keeps blurring and sinks to black; then text mode |
| exit | 12235 | | 0x1c8e, main | sound off, text mode 3, message, exit |

There is **no per-frame state** in any of these parts: all animation is advanced in the 70 Hz IRQ (per tick) or on
music-counter changes; the frame loop only renders the current state. They are frame-rate independent.
Measured at the default budget (1.5M instr/tick): title 1065 frames / 973 ticks = 76.6 fps (≈1.37M instr/frame),
the end 77.1 fps (≈1.36M), logo 69.2 fps (≈1.52M).

## 1. Shared data and helpers

### 1.1 Sine table (0x22479, built in 0x5b7d)
`S[k] = round(65536*sin(x_k))`, `x_0 = 0`, `x_{k+1} = x_k + c + c` with `c` = float32 0.003067961661145091
(accumulated in x87 precision). Differs from `round(65536*sin(2πk/1024))` in 2 entries by 1. Use exact sin in the remake.

### 1.2 Font (0xe2e) — static, code32 0x18ed7, 1979 bytes RLE → [0x1ede9] (29 glyphs × 2048 bytes)
Glyph g = 0..28 is 'a'..'z', then '1', '9', '6' (addressed as '{', '|', '}', i.e. char − 0x61).
```
for g in 0..28:
  rec = font + g*2048; rec[0] = *src++;                 // width w (= ink width, ink always starts at column 0)
  for row in 0..31: x = 0                                // mask rows at rec+1+row*60 (60 bytes per row)
     loop: b = *src++; if b == 0: break
           if b == 0xff: skip = *src++, run = *src++ else skip = b & 15, run = b >> 4
           x += skip; repeat run: mask[row][x++] = 0xff
```
Widths: a28 b30 c27 d30 e29 f20 g30 h28 i8 j14 k26 l8 m44 n28 o32 p30 q30 r19 s29 t19 u28 v31 w49 x29 y29 z28,
'1'19 '9'28 '6'28. Heavy Helvetica/Arial-bold-like sans, 1 bit. Vertical metrics in the 32-row cell: ascender/cap top
row 1, x-height top row 7, baseline (last ink row) 21–22, descender bottom 27–28 (g, j, p, q, y).
0xe8a draws a glyph by *saturating add* of the mask at edi with row pitch = the immediate at 0xeb8 (default 0x280 =
640, the title routine patches it to 0x50 = 80), returns edi + w + 1; a char < 'a' advances 10 (0x01 = newline, +0x303e).
0xedc draws a glyph magnified 3x horizontally, 7x vertically, rows 0..24 only (pitch 320), returns edi + 3w + 2.

### 1.3 Blur 0xf47 / 0xf90 ("K4")
`0xf90`: `out[p] = (in[p-pitch] + in[p+pitch] + in[p-1] + in[p+1]) >> 2` over a linear range (x neighbours wrap into
the previous/next row; no centre tap). `0xf47(esi, rows, pitch)` = two passes: buffer → temp ([8], 640 zero bytes
above and below) → buffer. So one 0xf47 call = kernel K4*K4 with truncation after each pass:
```
        1                      variance per axis 1.0 px² per call (0.5 per pass),
      2 . 2                    each pass truncates (mean loss ≈ 0.375, i.e. ≈ 0.75 per call).
    1 . 4 . 1      / 16        Pass 1 reads rows -1 and `rows` of the buffer itself, pass 2 sees zeros.
      2 . 2                    K4 alone has eigenvalue -1 for a checkerboard (never decays) – irrelevant here.
        1
```

### 1.4 Cloud texture (0x10d3) — static 2048 bytes at code32 0x16ca3 → [0x1eddd] (256x256, 64K-aligned)
```
src: 64 rows x 32 bytes, 4-bit pixels, high nibble = left pixel (a dithered 64x64 grey cloud, values 0..14)
for r in 0..63:                                  // top-left quadrant 128x128, each nibble doubled 2x2
   for k in 0..63: v = nibble(r,k)*16;  T[2r][2k] = T[2r][2k+1] = T[2r+1][2k] = T[2r+1][2k+1] = v
   T[2r][255-x] = T[2r][x], T[2r+1][255-x] = T[2r+1][x]   (x < 128)      // horizontal mirror
T[255-y][255-x] = T[y][x] (y < 128)                                     // 180° copy = vertical mirror
repeat 3 times:                                                          // cyclic blur, shifted by (2,2)
   tmp = 260 wide: each row = T row + its first 4 pixels; 512 rows = T stacked twice
   0xf47(tmp, rows 260, pitch 260)
   T[y][x] = tmp[y+2][x+2]
```
Result = 3 calls of K4*K4 (σ ≈ 1.73 px per axis), cyclic, shifted by (+6,+6) (T_new[y][x] = blurred[y+6][x+6] mod 256),
values 0..224. Static after init (identical at tick 11100). For the remake: generate it exactly at 256x256 and sample it
with bilinear filtering (it is smooth; there is nothing sharper to recover).

### 1.5 Shade tables (0x144b) at [0x1eddd]+0x20000 (T1) and +0x30000 (T2), rows ch = 0..64
`T2[ch][v] = min(255, (v*ch) >> 5)`. (T1 is the other parts' table: c = ch<=32 ? ch : ((ch-32)>>1)+((ch-32)>>3);
v<64 → max(0, v+2c-64), else max(0,(v&31)+c-32) + (v&0xe0).) The text routine adds 0x10000 to [0x1eded] for its
duration, so the warp uses **T2**.

### 1.6 Palette builder 0x14dc (angle = imm32 at 0x14dd, shift = imm8 at 0x1505), then 0x15c0 upload
```
s = S[a & 1023] >> sh;  c = S[(a+256) & 1023] >> sh;            // arithmetic shifts; sh = 11 (title, end), 12 (logo)
R = 128 - s;  G0 = 128 + (s >> 1);  d = (c*222) >> 8;  G = G0 + d;  B = G0 - d;   // hue rotation around grey 128
for i in 0..127:  pal[i]     = (C*i) >> 9                         // black → C/4   (6-bit DAC units, per channel C=R,G,B)
for k in 0..127:  pal[128+k] = ((C*(128-k)) >> 9) + (k >> 1)       // C/4 → white 63
```
Amplitude of s, c: 32 for sh=11, 16 for sh=12. Base colours C (and pal[70] = brightest warp colour):
a=0 (128,155,101) green-yellow; 256 (96,144,144) teal; 512 (128,100,156) violet; 768 (160,112,112) red-mauve;
972 (139,148,96) olive; logo a=720 sh=12 (144,115,125) mauve. pal[70] at a=0 = (17,21,13)/63 – the background is dark.

Fade 0x15c0 (also called by the IRQ whenever the level moves), `l = level >> 8` (level [0x1edb5], 0x8000 = normal):
```
l <= 128: out = (pal*l) >> 7
l >  128: m = min(l,255) - 128;  out = (((pal*(127-m)) >> 6) + m) >> 1      // towards white (63) at l = 255
```
The IRQ moves level by step [0x1edb9] towards target [0x1edbd] once per tick (clamped to the target).

### 1.7 Random 0xaa2
Additive lagged Fibonacci: 64 dwords at 0x9a0, byte indices i=[0xaa0] (initially 29), j=[0xaa1] (63):
`x = T[i] + T[j]; T[j] = x; i = (i+1)&63; j = (j+1)&63; return x` (x_n = x_{n-34} + x_{n-64}). The values used here
depend on every earlier call (for "the end" even on how many frames the plane part drew), so the remake should hardcode
the tables in §2.5 and use any RNG for the jitter.

## 2. Text routine 0x2829 (title and "the end")

### 2.1 Caller setup
Title (main 0x7689..0x76d8): 0x15af (static palette, screen still black), step 0x190, back buffer filled with 0x3e,
`0x1943(ebp=[0x1edf5])`, [0x2245d] = 0, esi = 0x463a "psychic linksixty four k", step 0x96 (150/tick; target is
already 0x8000 from 0x5b7d, so the fade-in runs from the end of the init, tick 100 → full at tick 318),
[0x1ea8f] = 0x70.
The end (main 0x79c8): [0x2245d] = 0x18, esi = 0x4653 "   t h e          e n d", byte [0x2efc] = 5 (already 5),
byte [0x2f02] = 5 (was 7), 0x5b6b (clear VGA to 0), target 0x8000, step 0x190 (400/tick: level 0 → 0x8000 in 82 ticks,
10968 → 11049), [0x1ea8f] = 0x53a.

**0x1943 + the 0x3e fill have no visible effect here:** 0x1943 computes `bb[i] = T1[[0x1edf5][i]][bb[i]]` (= T1[28..31][0x3e]
= 54..60, a grey paper grain), but the first title frame overwrites all 64000 back-buffer bytes (0x3095) before
anything is copied to VGA. **0x5b6b** just clears VGA memory to index 0.

### 2.2 0x2829 pseudo-C
```
void text_part(const char *str /*esi*/) {
    make_letters(str);                     // 0x2936: sprites, records, initial jitter (0x29e7)
    init_oscillators(300);                 // 0x2756
    [0x22461] = 0;                         // variable of other parts, not read here
    set_vertex_C(0xa00);                   // 0x2db3: grid[+8] = 0xa00 for all 41*26 vertices  -> shade row 10
    [0x1eded] += 0x10000;                  // warp uses T2 (multiply table)
    pal_shift = 11;  build_palette([0x14dd]);       // 0x14dc + upload with current level
    callback = 0x27ae;                     // per tick
    do {
        build_grid();                      // 0x2dc8
        warp_render(backbuf);              // 0x3095
        draw_letters();                    // 0x28db
        memcpy(VGA, backbuf, 64000);
        esc_check();                       // 0x7a64
        if (counter >= [0x1ea8f]) { target = 0xff00; step = 1000; }   // fade to WHITE
    } while (level != 0xff00);
    callback = 0;  [0x1eded] -= 0x10000;
}
void tick_27ae(void) {                     // IRQ, every tick
    build_palette([0x14dd]); [0x14dd]++;   // hue +1/1024 turn per tick, uploaded with the fade level
    if ((u8)counter != last) {             // last = byte [0x22459]
        last = (u8)counter;
        rejitter();                        // 0x29e7, all 24 records
        if (last >= 0x20) {
            if (counter >= 512)  visible += ((last & 3) == 0);   // only "the end": no visible effect
            else for (e in table_2790) if (e.step == last) visible += e.add;
        }
    }
    for (k = 0; k < 72; k++) osc[k].phase += osc[k].speed;
}
```
`visible` = [0x2245d] = number of string records drawn (spaces count). **[0x2245d] does not select anything in the
warp**: the title starts at 0 and grows on beats; "the end" sets 24 so all letters are there from the first frame
(its string ends with a 0 record that stops the loop).

### 2.3 Letters
**Sprites (0x2936):** pitch patch [0xeb8] = 80; for each of the 26 letters a 80x40 byte sprite (getmem 0xc80, zeroed):
glyph mask saturating-added at column 20, row 4; then 0xf47(sprite+80, rows 38, pitch 80) (one K4*K4 call).
**Drawing samples only the even sprite columns**, so on screen a sprite is 40x40 px and a glyph is **w/2 px wide,
32 px tall** (horizontal squash by 2; the blur happened at the doubled resolution: σ ≈ 0.5 px horizontally, 1 px
vertically in screen pixels). Glyph ink inside the 40x40 sprite starts at (10, 4).

**Records** (24 bytes at [0x1edcd]+0x400 + 24*i, i = 0..23): [0] char, [4] pen address, [8..0x17] 4 offsets.
```
pen = (5, 50);  for i in 0..23: if (i == 12) pen = (5, 110);
    rec[i].pen = pen;  pen.x += (c == ' ' ? 0 : w(c) >> 1) + 15;      // 15 px gap between inks, space = 15
```
Title pens: p5 s35 y64 c93 h121 i150 c169 l212 i231 n250 k279 (y 50); s5 i34 x53 t82 y106 f150 o175 u206 r235 k274 (y 110).
The end: t50 h89 e133 (y 50); e95 n139 d183 (y 110).

**Jitter (0x29e7):** for each record and k = 0..3: `dy = J[rand & 15]; dx = J[rand & 15]` (two calls, dy first),
`J = {0,0,0,1,-1,1,-1,1,-1,2,-2,2,-2,0,1,5}` (static, 0x2a42; P(0)=4/16, P(1)=4/16, P(-1)=3/16, P(±2)=2/16 each,
P(5)=1/16 – the 5 gives the occasional big ghost). offset_k = dy*320 + dx, offset_0 += 3*320 - 3 (shadow 3 down, 3 left).
Re-rolled once at setup and on **every music-counter change** (≈ every 8.4 ticks, 120 ms), all records at once (from
the IRQ, so a frame can mix old and new offsets).

**Draw (0x28db), per frame, in record order, i < visible, stop at char 0, skip spaces** (I = back buffer = palette index):
```
s(x,y) = sprite[c][y][2*x]                  // x,y in 0..39
shadow:   at pen + (dx0-3, dy0+3):  I = max(0, I - s)          // 0x2a91
3 copies: at pen + (dxk, dyk):     I = min(255, I + (s >> 1))  // 0x2a52, k = 1..3
```
Background indices are ≤ 70, one copy adds ≤ 127 (≈ the base colour), two or three copies reach the white end of
the ramp: the letter is white where the jittered copies overlap and tinted at the fringes; the shadow is black.
No fade, no motion other than the jitter: a letter pops in on its beat.

**Appearance schedule** (table 0x2790, 15 pairs (counter, add); reached at tick):
32:+1 p (372), 38:+1 s (423), 42:+1 y (456), 44:+1 c (473), 48:+1 h (507), 52:+1 i (540), 58:+2 "c " (591),
64:+1 l (641), 70:+1 i (691), 74:+1 n (725), 76:+1 k (742), 80:+3 "six" (775), 84:+3 "ty " (809),
90:+5 "four " (859), 96:+1 k (910). Fade to white from counter 112 (tick 1046), white at 1078, next part at 1079.

### 2.4 Warp background (0x2756, 0x2dc8, 0x3095)
**Oscillators (0x2756, ebx = 300):** 72 records (phase, amp, speed) at [0x1edd5]:
`phase = rand; amp = (rand & 0x3ff) + 0x100 (256..1279); speed = (((rand & 0xff) - 0x80) * 300) >> 8 (-150..149)`.
Per tick: `phase += speed`. Only phase bits 3..15 are used.

**Control points (0x2dc8, per frame):** 3x3 points P[i][j] (i = row 0..2, j = column 0..2), oscillators
k = 24i + 8j + 2m (X) and k+1 (Y), m = 0..3:
```
X[i][j] = Σ_m ( (S[(phase_k >> (3+m)) & 1023] * amp_k) >> 16 ) >> (4-m)      // octave m: freq 1/2^(13+m) turn per phase unit, amp/2^(4-m)
```
(code: `((phase << cl) sar 7) & 0x3ff`, cl = 4-m; products 32-bit, arithmetic shifts.) Typical |X|,|Y| ≤ ~850.

**Quadratic (0x2ad0(p0,p1,p2,n)):** Lagrange parabola through p0,p1,p2 at u = 0,1,2:
```
step = (65536/n) >> 7;  B = 4p1 - p2 - 3p0;  A = p0 - 2p1 + p2
for k in 0..n: t = k*step;  v[k] = (( (B*t + ((A*t*t) >> 8)) >> 8 ) + 2*p0) >> 1        // = p0 + (B u + A u²)/2, u = t/256
```
Rows: for each i, 41 samples of X and of Y along x (n = 40, step 12 → u_x = 12c/256, **u ∈ [0, 1.875]**, not 2).
Columns: for each of the 41 columns, 26 samples across the 3 rows (n = 25, step 20 → u_y = 20r/256, **u ∈ [0, 1.953]**).
Vertex (r, c) ↔ screen (8c, 8r). Vertex values: `A = Y << [0x2efc]` (5), `B = X << [0x2f02]` (7 title, 5 the end),
`C = 0xa00`. (0x2efc / 0x2f02 are the immediates of `shl eax,5` / `shl eax,7` at 0x2efa / 0x2f00.)

**Renderer 0x3095 (40x25 blocks of 8x8):** per block, from the 4 corner vertices (V00 top-left, V01 right, V10 below):
```
dAx = (A01-A00) >> 3;  ddA = ((A11-A10) - (A01-A00)) >> 6;  dAy = (A10-A00) >> 3     (same for B and C)
pixel (px,py), 0..7:  a = A00 + py*dAy + px*(dAx + py*ddA);  b = likewise
I = T2[C >> 8][ tex[(a >> 8) & 255][(b >> 8) & 255] ] = (texel * 10) >> 5           // 0..70
```
(8.8 fixed point; A and B are packed with the carry/`adc` trick into edx/bl with a 0x80 guard byte between A and the
B fraction; mathematically exact as above.) So texture **row = Y/8, column = X/2 (title) or X/8 (the end)**, wrapping.

**Remake (exact, per pixel):** with T = ticks since the part's callback started (fractional for smooth motion),
`phase_k(T) = phase0_k + speed_k*T`,
```
X[i][j] = Σ_m amp_{k}/2^(4-m) * sin(2π phase_k / 2^(13+m)),   Y[i][j] likewise with k+1
ux = 3x/512, uy = 5y/512  (x in 0..320, y in 0..200, vertex/pixel centres at integer x, y)
L0(u) = (u-1)(u-2)/2,  L1(u) = u(2-u),  L2(u) = u(u-1)/2
X(x,y) = Σ_i Σ_j L_i(uy) L_j(ux) X[i][j];  Y(x,y) likewise
I_bg = tex_bilinear(row = Y/8, col = X/xs) * 10/32,   xs = 2 (title), 8 (the end)
```
then letters in index space (§2.3, clamps 0..255), then colour = palette ramp(I) (§1.6, interpolate between entries or
use the formula with real numbers) and fade.

### 2.5 Oscillator tables (initial values; phase & 0xffff, index k = 0..71)
Title (dumped right after 0x2756 at tick 106; deterministic on every machine):
```
phase = 0x1880,0x83ef,0xf082,0x60ea,0xdd72,0x58db,0x7197,0x59c0,0x843c,0xfd1a,0xabbb,0x4aa6,0xa035,0x69bb,0xe12c,0xd188,0xbc1b,0xc110,0x5a1c,0x06a9,0x4826,0x9ce7,0xb34b,0xc6b8,0x0587,0x88e6,0xac0e,0x070a,0x82ff,0xbc3f,0xaa0e,0xb5e9,0xa1bf,0xbe96,0xa51f,0x8e0f,0xf28b,0xbef1,0x8b9e,0xf321,0x27bb,0xf69a,0x1f4f,0xc809,0xda04,0x72c7,0xc147,0x4b2d,0xe33a,0x62ce,0x07dd,0xe866,0x7d72,0xdef9,0x0c7b,0x45ab,0x2b09,0x2fe7,0xb8b4,0x40e5,0x58c3,0x3d78,0x45eb,0x9334,0x9e31,0xf07f,0x8cc1,0xb0d9,0x36aa,0xf634,0x9c9d,0x70ff
amp   = 1263,641,842,346,308,1042,521,1215,794,986,682,292,500,795,810,453,689,566,1214,288,454,315,917,439,933,926,867,690,976,856,1219,1249,1165,1152,371,947,878,297,282,539,1000,820,976,522,590,934,1013,921,763,925,1120,713,387,526,927,1059,470,1234,798,984,751,1176,1024,449,1026,895,370,336,1044,1262,1203,416
speed = 113,69,-135,-126,140,-11,-85,103,-66,-105,-26,146,-8,94,-125,22,83,-30,82,77,-51,29,43,-78,-13,-40,101,67,137,103,142,-19,-39,10,96,92,-34,-127,121,16,-10,10,-118,-111,-86,-122,-56,94,106,124,-16,50,-126,-42,11,76,-46,-34,77,-126,65,-66,146,-53,-122,127,85,87,-25,-121,-13,-58
```
The end (tick 10970, as in full.frames, i.e. plane part at 34 fps; on other machines the RNG state differs):
```
phase = 0xf801,0x846f,0xb536,0xd0f1,0x8ad4,0x1a78,0xae71,0x476d,0x283e,0x53aa,0xdc17,0x9e0d,0xbfc7,0xfced,0x8fbb,0x13d9,0x7bfa,0xd4e7,0x418e,0x5bef,0x642e,0xcb51,0x436e,0xecc7,0x26fb,0xa699,0xc190,0x1e77,0xdbe1,0x777d,0x2ecd,0x2880,0x9f17,0xaf5c,0xea00,0x178e,0x7418,0xdc62,0xa37e,0x9d17,0x40e0,0xd7f0,0xc138,0x47ea,0x6b44,0xd262,0xca1a,0x98ca,0x9227,0x64cc,0x8c9b,0x195d,0xc229,0xd865,0xf1ab,0x9f36,0x0861,0x82e8,0xa506,0xca25,0x4293,0x885f,0x1398,0x9421,0xe79d,0xe909,0x5dec,0x3d69,0x98f6,0x615c,0xdb27,0xfde9
amp   = 987,448,806,953,1276,564,345,714,343,1083,710,566,480,1074,632,562,1041,307,345,913,643,280,892,509,734,911,849,875,340,716,1177,325,606,1077,692,950,882,650,1118,623,1132,1019,1233,901,291,507,807,680,985,1119,404,1197,368,335,1094,580,752,818,556,677,739,635,589,1121,335,495,664,891,1202,1069,1009,1070
speed = 78,-136,132,-131,-34,10,85,93,124,-75,50,69,-139,9,-98,128,-122,52,116,-75,-24,38,138,-143,125,-15,-31,-26,146,56,89,-65,-59,-43,-95,-50,76,-36,35,52,-42,-118,-42,105,-92,21,-70,78,137,58,-81,-145,-75,-95,67,101,57,29,-92,-86,91,-150,46,-74,98,-27,-77,-124,105,-145,-146,-11
```
Phase advance starts with the IRQ of the setup tick (title tick 106, the end 10970).

### 2.6 Palette / fade timeline
- Title: angle [0x14dd] starts at 0 (exe value) at tick 106, +1 per tick (angle ≈ tick − 106, 972 at the end);
  level 0 → 0x8000 at +150/tick from tick 100 (end of init, a few ticks before the first frame) to 318;
  counter ≥ 112 (tick 1045): +1000/tick to 0xff00 (white) at 1078.
- The end: angle starts at 2910 (= 0xb5e; left by the greetings part: 0x944 + its counted ticks) → 3316, +1/tick;
  level +400/tick 10968 → 11049; counter ≥ 1338 (tick 11343): +1000/tick to white at 11376.

## 3. Closing logo 0x13d6 (callback 0x136c)

### 3.1 Buffers built at init (0xfb2, part of 0x5b7d)
**"1996"** [0x1ede1] (320x200, rows -1 and 200 zero): glyphs '1','9','9','6' via 0xedc (x3, y7, font rows 0..24)
starting at (2, 9); ink spans x 2..316, y 16..169, values 0/255. Then 0x13af(5) 15 times = **75 blur calls**
(0xf47, 200 rows, pitch 320); the paper part's frame loop calls 0x13af(1) until the budget [0x1edad] (150) is used up:
**75 more** (0x57fc in the "present Paper" part also calls 0x13af(1) while it waits for ticks, so whatever the paper
loop leaves is used there; the total is always 150). At the start of the logo part the buffer is exactly glyphs +
150 blur calls (max value 113).
**"psychic link" logo** [0x1ede5] (640 wide, rows -1..40): text with 0xe8a at pitch 640 from (187, 6): p187 s218 y248
c278 h306 i335 c344, space +10, l382 i391 n400 k429 (advance w+1, full width). Then bitwise NOT of rect
(x 176, y 1, 288x38) and NOT of rect (180, 4, 196x32): white frame (left 4 px, top/bottom 3 px), "psychic" white on
black inside, "link" black on a white block (x 376..463). Then one 0xf47 (rows 0..39, pitch 640).

### 3.2 Part code
```
void logo_part(void) {
    step = 0x200; target = 0x8000;                 // level comes in at 0xff00 (white): 64 ticks to normal (11440)
    [0x14dd] = 0x2d0; pal_shift = 12; build_palette(); // static mauve ramp, C = (144,115,125)
    callback = 0x136c;
    do {
        logo_frame(Z >> 4);                        // 0x129b, straight to VGA
        n = ticks; ticks &= 7;                      // [0x1edc1], counted by the callback (2 at entry)
        for (n >>= 3; n; n--) blur(b1996);          // one 0xf47 per 8 ticks, catch-up -> frame-rate independent
        esc_check();
    } while (Z > 0x500);
}
void tick_136c(void) {                             // per tick
    ticks++;
    Z = (Z * K) >> 16;                             // Z0 = 0xa000, K0 = 0xf230 (static dwords at 0x1368 / 0x1364)
    if (Z < 0x2000) K = ((K - 0xffce) * 240 >> 8) + 0xffce;   // signed; floor -> K settles at 0xffbf, not 0xffce
}
void logo_frame(int s) {                           // 0x129b
    if (s < 0x80) s = 0x80;
    dh = s >= 0x100 ? 255 : (s - 0x80) * 2;  T[v] = (v * dh) >> 8;
    clear tmp ([0x1edfd], rows -1..200);
    zoom(tmp, s);                                   // 0x11c0
    tmp[p] = min(255, b1996[p] + T[tmp[p]]);        // per byte
    VGA = K4(tmp) (one pass 0xf90, zero rows above/below);
}
void zoom(u8 *dst, int s) {                         // 0x11c0, nearest neighbour, 16.16
    step = 0x1000000 / s;                            // source pixels per screen pixel = 256/s
    x0 = 0x1400000 - 160*step;                       // source x of screen x=0: 320 - 160*256/s (centre column 320)
    if (s < 0x500) { row0 = (25600 - 20*s) >> 8; rows = (20*s) >> 7; y0 = 0; }   // band of ~40 source rows
    else           { row0 = 0; rows = 200; y0 = 0x140000 - 100*step; }            // centred on source row 20
    for r in 0..rows-1, x in 0..319: dst[row0+r][x] = logo[(y0 + r*step) >> 16][(x0 + x*step) >> 16]
}                                                   // (fractions accumulated per pixel/row with carries – same thing)
```
Equivalent continuous mapping (both branches): `src_x = 320 + (x-160)*256/s`, `src_y = 20 + (y-100)*256/s`,
outside rows 0..39 → 0. Magnification = s/256.

### 3.3 Logo timeline (Z_n = after n ticks of the part, frame at tick 11377+n uses Z_n; exact integer recurrence)
- n=0: s = 2560 (10x, full-screen band mode until s < 1280 at n=13); screen white, fading to normal by n=63.
- K = 0xf230 (×0.946/tick) until Z < 0x2000 at n=30 (s = 484), then K relaxes: 0xf309, 0xf3d5, ... 0xffbf from n=122.
- s < 256 (logo starts dimming, dh = 2(s-128)) at n=53; s ≤ 128 (logo invisible) at n=475 (tick ~11852).
- Once K = 0xffbf and Z < 0x7e0 (s < 126, n ≈ 490), Z drops by exactly 2 per tick (truncation); Z = 0x500 at n=858 →
  part ends (tick 12234).
- "1996": +1 blur call per 8 ticks (≈107 more calls), brightness 113 → ~48 at n≈523 → nearly black at the end
  (the truncation erodes it; the last remains are bright streaks where the strokes were thickest).
- There is no fade to black: the screen is switched to text mode right after.

### 3.4 Exit (0x7a12 path, also taken by ESC from any part via 0x7a64)
[0x1ea93] = 0 (player off), callback 0, **0x1c8e** (PIT divisor 0 = 18.2 Hz, GUS reset via 0x22f8 if a GUS was
found; nothing on screen), int 10h mode 3 twice, int 21h/9 prints
`"\xfa Thanks for watching - This demo is copyright (c) 1996 Alex "statix" Evans\r\n\xfa Read PAPER.NFO for info"`
(0xfa = CP437 '·'), exit.

## 4. Time bases

| variable | where | rate |
|---|---|---|
| oscillator phases (warp motion) | IRQ 0x27ae | +speed per tick |
| palette hue angle [0x14dd] | IRQ 0x27ae | +1 per tick (1024 ticks = 14.6 s per turn); logo: fixed 720 |
| fade level | IRQ 0x1e45 | title +150/tick in, +1000/tick out; the end +400 in, +1000 out; logo −512/tick |
| letter count [0x2245d] | IRQ, on counter change | per table at 0x2790 (music) |
| letter jitter | IRQ, on counter change | every music step (~8.4 ticks) |
| part end | main loop | counter ≥ 0x70 / 0x53a → white fade; exit when level = 0xff00 |
| logo Z / K | IRQ 0x136c | per tick |
| "1996" blur | main loop, from tick count | 1 call per 8 ticks (catch-up) |
| rendering | main loop | per frame, no state advanced (76.6 / 77.1 / 69.2 fps measured) |

## 5. Rendering shortcuts to evaluate exactly in a high-resolution remake
1. Warp: 41x26 vertex grid + 8x8-block bilinear interpolation (0x3095) of the biquadratic field → evaluate §2.4's
   formula per pixel. Keep the odd parameter ranges (u_x ≤ 1.875, u_y ≤ 1.953) – they shape the field.
2. Integer sines / fixed-point (amp*S >> 16 >> cl, Lagrange in 8.8) → floats.
3. Nearest texel, 256x256 → bilinear (texture is a 6-pass-blurred 64x64 cloud; generate it as in §1.4).
4. Shade T2[10] = texel*10 >> 5 quantises the background to 71 levels; palette entries are 6-bit (>>9) → compute
   colour = ramp(I) in floats.
5. Letters: 32-px 1-bit font, blurred at 2x horizontal resolution then decimated (every 2nd column) → render the glyph
   (bitmap upscaled, or a matching bold sans) at w/2 x 32 screen-px per glyph, blur σ ≈ (0.5, 1.0) screen px; keep the
   index-space arithmetic (subtract full, add half, clamps) so the overlap colours stay right. Jitter offsets are in
   320x200 pixels.
6. Logo: nearest-neighbour zoom of a blurred 640x40 bitmap (blocky at 10x) → bilinear sampling (or a re-drawn vector
   logo blurred by σ ≈ 1 px of the 640 buffer); the final 1-pass K4 blur ≈ σ 0.7 screen px.
7. "1996": 3x7 pixel-replicated glyphs + 150 integer blur calls, then 1 per 8 ticks with truncation. The fading look
   comes from the truncation, so simulate the 320x200 integer buffer exactly (cheap) and upscale it smoothly, rather than
   approximating with Gaussians.
8. The first frame of the logo part is still the last "the end" frame on a white palette (whole-frame capture artefact).

## 6. Not determined / guesses
- The table J's last three entries (0, 1, 5) may be accidental (the 5 looks like a typo), but the code uses all 16.
- Why 0x1943 / the 0x3e fill run before the title (dead for the screen; maybe a leftover of an earlier paper intro).
- The end's oscillator table and jitter depend on the RNG state, i.e. on how many frames the plane part drew; the
  tables in §2.5 are those of the reference capture (34 fps planes). The title's are machine-independent.
- The exact start of the title fade-in on real hardware is "end of init" (0x5b7d), a few ticks before the first frame;
  in the capture 6 ticks.
