# Scope "grid": Craft's grid filler 0x402970 and parts 11, 2, 5, 4

Addresses are linear addresses (= offsets in `clone_img.bin` + 0x400000). "timer" = xmpGetTimer (PIT clocks since
the music start), "t70" = timer × 70 / 1,193,182 (time in 1/70 s), "dt" = the float [0x420489] main computes after
each shown frame (NOTES.md, "Time base"). Positions are order:row in hex; row p (= order × 64 + row) becomes current
at timer 66300 p − 22100. All float → int conversions and all float stores **truncate** (FPU control word 0x1f7f);
`f32(x)` below is a float store (truncating), `trunc(x)` a fistp. "u, v" are texture coordinates in 1/256 texel
(16-bit words, the texture repeats every 65536), "light" a 16-bit word whose high byte L selects the brightness.
Pixel (x, y) is the original 320 × 200 frame; the grid entry (c, r) lies at x = 8c, y = 20 + 8r.

What was checked (models in `models/grid/`, see its README; runs resumed from the part snapshots with
`models/grid/run_clone.py --dump`, at the capture's 150 M and at 55.5 M instructions per second):

| what | model | frames | result |
|---|---|---|---|
| grid filler 0x402970 with the tables T, W and the texture | `filler.py` | 445 (parts 2, 5, 4: rows 20..179 after the filler) | bit-exact |
| table T 0x420854 (0x40347a), weights 0x420a54 (0x4033ee) | `filler.py` | snapshots t2316, t6156, t9324 | exact from the formulas |
| part 5 grid (0x403530) | `gridparts.py` | 148 + 86 (two CPU speeds) | every entry bit-exact |
| part 2 grid (0x4198bf) | `gridparts.py` | 144 + 67 | 99.0 % / 98.3 % of the entries exact, the rest ±1..2 (1/256 texel) and light ±1 (fsin / fpatan rounding) |
| part 4 grid (0x4037c0) | `gridparts.py` | 153 | 99.7 % exact, the rest ±4 (one step of its ×4 coordinates) and light ±1 |
| state recurrences of parts 2, 4, 5, 11 | `gridparts.py`, `part11.py` | all of the above | exact (float32 with truncation) |
| part 11 tiles (0x419dc7) | `part11.py` | 150 | every tile pixel bit-exact |
| closed forms of the clocks (time base, event switch times) | `clocks.py`, `event_lag.py` | all of the above | within float accumulation (≤ 0.17 units of 1/70 s over a part) |
| per-pixel functions for the remake | `perpixel.py` | 445 | 320 × 200 renders differ from the original by 3.4..4.9 of 255 on average (the 8 × 8 interpolation and 8-bit steps); pictures side by side in `dump/grid/perpixel_*.png` |

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | routine | music | ticks | music time (s) | fps in the capture | what it draws |
|---|---|---|---|---|---|---|
| 11 | 0x419dc7 | 08:20..0c:00 | 1645..2315 | 30.27..42.67 | 725 | 8 × 6 flat tiles of 40 × 24 pixels (rows 24..167) in orange / pink / violet from a stored colour table, each fading in at its own flickering rate; a black box (tiles 3..6 of rows 2..3) with the title "clone meets clone"; from 0b:00 (event 1101) everything fades to black in 2.2 s |
| 2 | 0x4198bf | 0c:00..10:00 | 2316..3083 | 42.69..56.90 | 40.5 | the camera inside a flat elliptic tube (floor and ceiling receding to a dark horizon), the grid texture on its wall, gliding along the tube and swaying sideways; each scan line looks in a slightly different direction (the rotation grows down the screen), which warps the picture; brightness from 0.3 to 1 in 1 s at the start and again after 0e:00 (event 201, which also switches the camera path); "meet us" in a black box and glyph 1 at the bottom left |
| 5 | 0x403530 | 20:00..24:00 | 6156..6923 | 113.82..128.02 | 41.6 | a 2D rotating zoom of the grid texture whose zoom varies over the screen like a plasma (sum of two products of sines), brighter where it is zoomed out; the rotation reverses at 21:00, 22:00, 23:00 (event 501); "get them clones" in a black box at the top right and glyph 2 at the top |
| 4 | 0x4037c0 | 30:20..36:00 | 9324..10379 | 172.49..192.04 | 39.0 | the inside of three perpendicular tunnels of radius 192 crossing at the origin, the camera floating near the crossing and turning; the walls carry the grid texture projected along (−1, 1, 0), darkening with distance to black at the far ends; dim and pulsing until 31:00 (event 403), then black and fading up in 1.4 s; event 401 at 32:00..35:00 cycles the turning rates; "tunnels meet more tunnels" in a black box and glyph 4 at the top left |

Each part draws one frame per call of main's part switch (0x400e80) into the draw target [0xe350ec] (NOTES.md).

## 2. Data

| what | where | format / formula | made by |
|---|---|---|---|
| grid | 0xea57b4 (BSS), 5166 bytes | 21 rows × 41 entries × 3 words: +0 u, +2 v, +4 light (246 bytes per row). Entry (c, r) belongs to pixel (8c, 20 + 8r); c = 40 and r = 20 are the right and bottom edges (x = 320, y = 180) | parts 2, 4, 5 (each frame) |
| grid texture | [0x4207c8] = 0xdf02ec (set at 0x4001c2), 0x10101 dwords | texel (row u, column v) = dword 0x00RRGGBB at +4 (256 u + v), u, v = 0..255; row 256 (+0x40000) = copy of row 0, plus one more dword (= texel (1, 0)) | 0x4033a7 (below) from a 256 × 256 RGB colour table: 0xe6542b in the precalc (part 2), 0xe35128 from event 5 on (parts 5 and 4) |
| colour decode T | 0x420854, 512 bytes | T[i] = min(255, trunc(e^((i − 256) / 23)))  (0x41a144 = 0.0434782609 = 1/23); T[i] = 0 for i < 256, 1 at 256, 255 from 384 | 0x40347a (precalc) |
| bilinear weights W | 0x420a54, 16 × 16 × 4 floats (4096 bytes) | entry (j, i) at 0x420a54 + 256 j + 16 i (i = u fraction >> 4, j = v fraction >> 4): (16−i)(16−j), (16−i) j, i (16−j), i j — integers, sum 256 | 0x4033ee (precalc) |
| part 4 turning rates | 0x421a58, 3 × 3 floats | mode 0: 0.01, 0.012, 0.008; mode 1: 0.004, −0.01, 0.01; mode 2: −0.013, 0.005, −0.009 (rad per 1/70 s for a1, a2, a3) | static (code parameters) |
| tile colours | 0x428164, dwords | entries 8..55 (from 0x4281c4) = 48 RGB triples (ints 13..91; colour = value × 0.0125 × 255) for part 11's 8 × 6 tiles, row by row; the 3D parts read the same table (0x4171ce, 0x417b65, 0x41885d, 0x419819: parts3d scope) | static data (read it from the exe): a designed 8 × 6 colour mosaic (orange top left to violet bottom right) |
| text and glyph pictures | [0xe35124] "meet us", [0xe35120] "get them clones", [0xe35118] "tunnels meet more tunnels", [0xf73930] "clone meets clone"; glyphs [0xe350fc] 1, [0xe350f8] 2, [0xe350f0] 4 | {pixels, w, h} 32-bit, 0xffffffff = transparent (NOTES.md "Pictures and textures") | stored pictures (hand-drawn), decoded by 0x403f53 (gfx scope) |

### 0x4033a7 (the texture expansion, gfx scope makes the table)

```c
void make_grid_texture(const u8 *rgb /* [ebp+8], 3 bytes per texel */) {
    u32 *t = (u32 *)[0x4207c8];
    for (i = 0; i < 0x10000; i++) t[i] = rgb[3i] << 16 | rgb[3i+1] << 8 | rgb[3i+2];   // R, G, B
    for (i = 0; i < 0x101; i++)   t[0x10000 + i] = t[i];                             // row 256 = row 0, + 1
}
```
The colour bytes are log-encoded intensities: the gfx scope's generator writes them through 0x4034e7,
c = clamp(trunc(23 ln(1 + f) + 128), 0, 255) for a linear intensity f ≥ 0. The filler undoes the log with T, after
adding the light (section 3), so light multiplies the colour.

## 3. The grid filler 0x402970 (Craft; hand-written, cdecl: [ebp+8] = grid, [ebp+0xc] = page)

Fills rows 20..179, columns 0..319 of the page (every pixel there, nothing else) in 20 × 40 blocks of 8 × 8
pixels, interpolating u, v and light linearly inside each block from the block's four corner entries, then samples
the texture bilinearly and decodes the colour with T. The inner loop is unrolled 8 times.

```c
void grid_fill(const s16 *grid /* 41 x 21 x 3 words */, u32 *page) {
    u32 *dst = page + 20 * 320;                                           // edi + 0x6400
    for (by = 0; by < 20; by++, dst += 7 * 320)                           // + 0x2300 after a row of blocks
      for (bx = 0; bx < 40; bx++, dst += 8) {
        const s16 *e = grid + 3 * (41 * by + bx);                         // +6 per block, +6 more per row
        // corners: u, v sign-extended, light zero-extended; per-line steps of the left and right edges
        s32 uL = e[0],   vL = e[1],   lL = (u16)e[2];                     // top left
        s32 uR = e[3],   vR = e[4],   lR = (u16)e[5];                     // top right
        s32 uLs = (s16)(e[123+0] - uL) >> 3, vLs = (s16)(e[123+1] - vL) >> 3, lLs = (s16)((u16)e[123+2] - lL) >> 3;
        s32 uRs = (s16)(e[123+3] - uR) >> 3, vRs = (s16)(e[123+4] - vR) >> 3, lRs = (s16)((u16)e[123+5] - lR) >> 3;
        for (j = 0; j < 8; j++) {                                         // 0x402aa6, one line of the block
            s32 du = (s16)(uR - uL) >> 3;                                 // differences taken mod 65536:
            s32 dv = (s16)(vR - vL) >> 3;                                 // the short way round the texture
            s32 dl = (s16)(lR - lL) >> 3;
            u32 uv   = (u32)vL << 16 | (u16)uL;                           // packed v:u, 8.8 each
            u32 step = (u32)dv << 16 | (u16)du;                           // (no borrow correction: a negative du
            u32 l    = lL;                                                //  adds a carry of 1/256 texel to v)
            for (i = 0; i < 8; i++) {
                u32 tu = (uv >> 8) & 255, tv = uv >> 24;                  // texel row u, column v
                u32 fu = (uv >> 4) & 15,  fv = (uv >> 20) & 15;           // 4-bit fractions
                const float *w = W + 64 * fv + 4 * fu;                    // 0x420a54
                const u32 *t = tex + 256 * tu + tv;                       // [0x4207c8]
                for each channel k (R, G, B):                             // see "the FPU trick" below
                    s[k] = (w[0]*t[0].k + w[1]*t[1].k + w[2]*t[256].k + w[3]*t[257].k) / 256;   // floor
                u32 Li = l >> 8;                                          // 0..255
                dst[j*320 + i] = T[Li + s[R]] << 16 | T[Li + s[G]] << 8 | T[Li + s[B]];
                uv += step;  l += dl;
            }
            uL += uLs; vL += vLs; lL += lLs;  uR += uRs; vR += vRs; lR += lRs;   // 0x4032f7..
        }
      }
}
```

- **The FPU trick** (0x402b26..0x402c19): a texel 0x00RRGGBB is split into the 64-bit pattern G << 32 | R << 16 | B
  and stored over the low six bytes of the double 1.0 at 0x42082c (bytes 6, 7 stay 0xf0, 0x3f), so the double is
  1 + (G·2^32 + R·2^16 + B)·2^−52. The four texels are multiplied by their integer weights and summed on the x87
  (exact in 64-bit precision); stored as a double (truncating) the sum 256 + Σ w m 2^−52 keeps
  floor(Σ w m / 256) in its mantissa, so bytes 0, 2 and 4 of 0x42084c are floor(Σ w B / 256),
  floor(Σ w R / 256), floor(Σ w G / 256): three integer bilinear samples from one FPU pass. Equivalent to the
  integer formula above (checked bit-exact).
- **The wrap of the texture**: u wraps through row 256 (a copy of row 0). v does not: at tv = 255 the right
  neighbours are read from the next row's column 0 (texel (u+1, 0) instead of (u, 0)), a one-texel seam the remake
  should not copy (use a proper wrap).
- **Light**: the page colour is T[L + c] = min(255, trunc(e^((L − 128)/23) · e^((c − 128)/23))). With the log
  encoding of the texture, e^((c − 128)/23) ≈ 1 + f, so the output is min(255, (1 + f) · g(L)) with the gain
  g(L) = e^((L − 128)/23): L = 128 is gain 1, every 23 steps of L multiply the brightness by e. L = light >> 8 is
  quantised to steps of 1/23 in the exponent (4.4 % in brightness).

### The per-pixel function a remake evaluates instead

For each part, section 5..7 gives u(x, y), v(x, y) (texels, unwrapped; the texture repeats every 256) and L(x, y)
(the light word / 256) as continuous functions of the pixel position. The original evaluates them only at the grid
entries and interpolates. The remake evaluates them per pixel and then

```
colour_k = min(255, e^((L + c_k(u, v) − 256) / 23))      k = R, G, B,  c_k = 128 + 23 ln(1 + f_k(u, v))
         = min(255, (1 + f_k(u, v)) · e^((L − 128)/23))
```
with f_k the gfx scope's texture function evaluated at (u, v) (or the stored-byte texture sampled with a proper
bilinear / mipmapped filter in the log domain). Keep the clamp at 255 (the saturation is part of the look); drop
the truncations, the 4-bit weights, the integer L and the 8 × 8 blocks.

Where the original's blocks are wrong: if two neighbouring entries are more than 128 texels apart, the filler
interpolates the short way round the texture. That happens in 0.1..0.6 % of the neighbour pairs (`perpixel.py`):
in parts 2 and 4 at the far ends of the tunnels, where the light is at its floor (L ≈ 55, gain 0.04: nearly black),
in part 5 where Z is small (strong zoom, L ≈ 90, gain 0.2). There the original shows block noise; the per-pixel
version needs texture filtering (mipmaps) to avoid aliasing. Everything else is the same picture, sharper.

## 4. Part 11 (0x419dc7): tiles and title (08:20..0c:00)

```c
static float clock = 0;          // [0xf73934] (BSS)
static float fade  = 1.0f;       // [0x428464] (initialised data)
// event 1101 (0b:00): [0x428468] = 1
void part11(void) {
    clock = f32(clock + dt * 0.05);                                       // 0x41a914 (double)
    if (([0x428468] & 0x7fffffff) && 0 < fade) {
        fade = f32(fade - dt * 0.0065);                                   // 0x41a91c (double)
        if (fade < 0) fade = 0;
    }
    k = 24;                                                               // dword index into 0x428164
    for (row = 0; row < 6; row++) for (col = 0; col < 8; col++, k += 3) {
        int R = tab[k], G = tab[k+1], B = tab[k+2];                       // 0x428164 + 4k
        float b = f32((sin(R + R * clock * 0.01f) * 0.3 + 0.7) * (clock * 0.13));   // 0x41a924 float 0.01
        if (b > 1.0f) b = 1.0f;
        int r = min(255, trunc(R * 0.0125f * 255.0f * b * fade));        // likewise g, b; 0x41a944, 0x41a948
        u32 colour = r << 16 | g << 8 | b;
        if (col > 2 && col < 7 && row > 1 && row < 4) colour = 0;         // the black box
        fill_rect(page + (24 + 24*row) * 320 + 40*col, colour, 40, 24);   // 0x4043be
    }
    level = trunc((1 - fade) * 63.0f);                                    // 0x41a94c
    blit(title [0xf73930], x = 141, y = 104, page, mode 1, level);        // 0x404002
}
```

- The tiles cover rows 24..167 and every column; the box is x 120..279, y 72..119; the title (101 × 9) at
  (141, 104)..(241, 112). 0x404002 with mode 1 and level 0 is a plain transparent copy; with level 1..63 it is
  0x40442c: out = picture + level × (page − picture) / 64 (core scope), over the black box: the title × (64 − level)/64.
- Tile level: b = min(1, 0.13 clock (0.7 + 0.3 sin(p (1 + 0.01 clock)))) with p = the tile's red value (22..91):
  every tile fades in linearly with a flicker of its own frequency (0.035 p rad/s), reaching full brightness
  2.2..5.5 s after the start. Colour = table × 3.1875 × b × fade, clamped at 255 (100 → 318 → 255).
- **Does not overwrite** rows 0..23 and 168..199. They keep what the page held four frames earlier: black in the
  capture (part 1 left them black; checked on every dumped page), black in the 15-bit path. The remake: black.
- Time base: clock = 0.05 (t70 − t70(τ11)), fade = max(0, 1 − 0.0065 (t70 − t70(τ1101))) (section 8). Neither
  depends on the frame rate (b is not integrated). fade reaches 0 at τ1101 + 2.198 s (capture: 41.30 s, ≈ 0b:27).
- Pictures: the tiles are flat rectangles of a stored colour table (no redraw: nothing to sharpen); the title is
  the stored "clone meets clone" picture (grey C, hand-drawn anti-aliased text), 1:1 at a fixed position.

## 5. Part 2 (0x4198bf): the flat tube (0c:00..10:00)

```c
static float TC = 0;             // [0xf73928] (BSS): camera clock
static float Br = 0.3f;          // [0x428160] (initialised data): brightness
static int   flag = 0;           // [0x42815c]
// event 201 (0e:00): Br = 0.3f, flag = 1
void part2(void) {
    if (Br < 1.0f) { Br = f32(dt * 0.01 + Br); if (Br > 1.0f) Br = 1.0f; }
    if (flag) { Ox = f32(110 * sin(TC * 0.013 + 2)); Oz = f32(TC * 3.0f); }     // TC before this frame's dt
    else      { Ox = f32(150 * sin(TC * 0.01));      Oz = f32(TC * 5.0f); }
    Oy = 0;
    TC = f32(dt + TC);
    r1 = f32(TC * 0.01);  r2 = f32(TC * 0.012);  r3 = f32(TC * 0.008);       // TC after
    for (r = 0; r < 21; r++) {
        for (c = 0; c < 41; c++) {
            xs = f32((c * 8.0 - 160) * (1/120.0));  ys = f32((8.0 * r - 80) * (1/120.0));   // (x−160)/120, (y−100)/120
            (x2, y3, z3) = rotate(xs, ys, r1, r2, r3);                    // below, shared with part 4
            n  = sqrt(x2*x2 + y3*y3 + z3*z3);
            dx = f32(1/n * 0.17 * x2);  dy = f32(y3 / n);  dz = f32(z3 / n);
            t  = cylinder(dx, dy, Ox, Oy);                                // radius 192 around the z axis
            u     = trunc((dz*t + Oz) * 0.6 * 256 * 0.5 * 0.25);          // = 19.2 hz
            v     = trunc(atan2(dx*t + Ox, dy*t + Oy) * 256 * (1/π) * 256);   // fpatan(st1 = hx, st0 = hy)
            Lf = f32(53 - t * 0.024 * (1/256.0) * 192);  if (!(Lf >= 16)) Lf = 16;   // 53 − 0.018 t
            Lf = f32(Lf * 878.0f);  if (Lf > 64535) Lf = 64535;
            grid[r][c] = { u - 0x8000, v - 0x8000, trunc(Lf * Br) };       // 16-bit words
        }
        r1 = f32(r1 + 0.02);  r2 = f32(r2 + 0.024);  r3 = f32(r3 + 0.016);   // per grid row
    }
    grid_fill(grid, page);
    fill_rect(page + 130*320 + 10, 0, 100, 40);                           // black box x 10..109, y 130..169
    blit("meet us" [0xe35124], 20, 151, page, mode 0);                    // 42 × 9
    blit(glyph 1 [0xe350fc], 80, 111, page, mode 0);                      // 77 × 65, over the box
}

// shared by parts 2 and 4 (0x419b05.. / 0x403b8b..): rotate the screen ray (xs, ys, 1)
rotate(xs, ys, a1, a2, a3):
    x1 = f32(sin a3 · ys + cos a3 · xs);   y1 = f32(cos a3 · ys − sin a3 · xs);      // roll (x-y)
    z2 = f32(sin a2 · x1 + cos a2);        x2 = f32(cos a2 · x1 − sin a2);           // x-z, z = 1
    y3 = f32(sin a1 · z2 + cos a1 · y1);   z3 = f32(cos a1 · z2 − sin a1 · y1);      // y-z
    return (x2, y3, z3);
// forward distance from (pa, pb) along (da, db) to the circle of radius 192 (−36864 at 0x41a248 / 0x41a898)
cylinder(da, db, pa, pb):
    a = f32(da*da + db*db);  b = f32((pa*da + pb*db) * 2);
    D = b*b − (da*da + db*db) * 4 * (pa*pa + pb*pb − 36864);
    t1 = f32((sqrt(D) − b) / (a*2 + 0.001));  t2 = f32((−b − sqrt(D)) / (a*2 + 0.001));
    return t1 > 0 ? t1 : t2;
```

Per pixel (x, y), with row = (y − 20)/8:
```
r1 = 0.01 TC + 0.02 row,  r2 = 0.012 TC + 0.024 row,  r3 = 0.008 TC + 0.016 row
(x2, y3, z3) = rotate((x − 160)/120, (y − 100)/120, r1, r2, r3);  d = (0.17 x2, y3, z3) / |(x2, y3, z3)|
t = the positive root of (Ox + t dx)² + (t dy)² = 192²;   h = (Ox, 0, Oz) + t d
u = 0.075 hz + 128   (texels; texture row)       v = 256 atan2(hx, hy) / π + 128   (texels; column)
L = Br · 878 · max(16, 53 − 0.018 t) / 256       (= Br · (181.77 − 0.06173 t), at least 54.875 Br)
```
- Geometry: the camera at (Ox, 0, Oz) inside a cylinder of radius 192 around z, but the ray's x component is scaled
  by 0.17: the tube is an ellipse 1129 wide and 192 high (floor and ceiling). v goes twice round the texture per
  turn. Oz = 5 TC moves the texture by 0.375 texel per 1/70 s (26 texels/s); after event 201 Oz = 3 TC and Ox
  follows the other sine (a jump, hidden because Br drops to 0.3 at the same frame: gain ≤ e^(−74/23) = 0.04).
- The per-row increments make the view direction turn by (0.4, 0.48, 0.32) rad from the top to the bottom row:
  evaluate them per pixel row with the continuous row = (y − 20)/8.
- Overwrites rows 20..179 completely (the filler); rows 0..19 and 180..199 are never written: black in the capture
  (every dumped page) and in the 15-bit path. Overlays: box, text, glyph 1 every frame at fixed positions.
- Pictures: texture = generated (0xe6542b version, gfx scope; read as a texture: stays computed). "meet us"
  (grey A) and glyph 1 (grey B): stored, hand-drawn, 1:1, transparent where 0xffffffff.

## 6. Part 5 (0x403530): the warped rotating zoom (20:00..24:00)

```c
static float T = 0, R = 0;      // [0xea6be4], [0xea6be8] (BSS)
static float dir = 1.0f;        // [0x421a54] (initialised data); event 501: dir *= -1.0f (0x400dad)
// event 5 (20:00): make_grid_texture(0xe35128), part = 5
void part5(void) {
    a = dt * 0.005;
    T = f32(a + T);
    R = f32(a * 2.0 * dir + R);
    for (r = 0; r < 21; r++) for (c = 0; c < 41; c++) {
        X = (c - 20) * 0x83400;  Y = (r - 10) * 0x83400;                  // 537600 = 2100 × 256
        Xr = f32(cos R · X − sin R · Y);   Yr = f32(sin R · X + cos R · Y);
        S  = sin(0.296 r + 7T) · (130 sin(0.141 c + 4T)) + sin(0.176 r + 8T) · (55 sin(0.261 c + 6T));
        Z  = f32(S · sin(5T) + 200);                                      // 15..385
        u     = trunc(Xr · (1/Z) + 32768);
        v     = trunc(f32((1/Z) · Yr) + 32768);
        light = trunc(Z · 32768 · 0.0025 + 18432);                        // 18432 + 81.92 Z
        grid[r][c] = { u, v, light };                                     // low 16 bits
    }
    grid_fill(grid, page);
    fill_rect(page + 30*320 + 180, 0, 130, 40);                           // black box x 180..309, y 30..69
    blit("get them clones" [0xe35120], 215, 53, page, mode 0);            // 85 × 12
    blit(glyph 2 [0xe350f8], 140, 0, page, mode 0);                       // 63 × 67: x 140..202, y 0..66
}
```
Constants: 0.005 (0x41a15c), 2.0, 0.141, 4.0f, 130.0, 0.296, 7.0f, 0.261, 6.0f, 55.0, 0.176, 8.0f, 5.0f, 200.0,
32768.0f, 32768.0, 0.0025, 18432.0 (0x41a164..0x41a1dc in this order).

Per pixel (x, y), with c = x/8, r = (y − 20)/8:
```
Z = 200 + sin 5T · (130 sin(0.141 c + 4T) sin(0.296 r + 7T) + 55 sin(0.261 c + 6T) sin(0.176 r + 8T))
u = 128 + 262.5 ((x − 160) cos R − (y − 100) sin R) / Z         (texels; 262.5 = 67200 / 256)
v = 128 + 262.5 ((x − 160) sin R + (y − 100) cos R) / Z
L = 72 + 0.32 Z                                                  (gain e^((L−128)/23): 0.11 at Z = 15, 18 at Z = 385)
```
- A 2D mapping (no 3D): rotation R about the screen centre (160, 100), zoom 2100/Z texels per 8 pixels; the zoom
  field Z swells and flattens with sin 5T (period 3.6 s ≈ one order), and the brightness follows it.
- Overwrites rows 20..179; rows 0..19 / 180..199 not written (black, except glyph 2's pixels in rows 0..19).
- Pictures: texture generated (0xe35128 version); "get them clones" (grey A), glyph 2 (grey B): stored, 1:1.

## 7. Part 4 (0x4037c0): three crossing tunnels (30:20..36:00)

```c
static int   mode = 0;           // [0x421a7c]; event 401: mode = (mode + 1) % 3
static int   lit = 0;            // [0x421a84]; event 403: lit = 1, B = 0  ([0x421a80], event 402, is never read)
static float B = 0, T4 = 0, a1 = 0, a2 = 0, a3 = 0;   // [0x421a88], [0x421a8c], [0x421a90], [0x421a94], [0x421a98]
void part4(void) {
    if (lit == 0) B = f32(sin(T4 * 0.07) * 0.3 + 0.5);                  // T4 before this frame's dt
    else if (lit == 1 && B < 1.0f) { B = f32(dt * 0.01 + B); if (B > 1.0f) B = 1.0f; }
    T4 = f32(dt + T4);
    P0 = f32(100 sin(T4 * 0.03));  P1 = f32(100 sin(T4 * 0.043));  P2 = f32(100 sin(T4 * 0.053));
    a1 = f32(dt * rate[mode][0] + a1);  a2 = f32(dt * rate[mode][1] + a2);  a3 = f32(dt * rate[mode][2] + a3);  // 0x421a58
    for (r = 0; r < 21; r++) for (c = 0; c < 41; c++) {
        xs, ys as in part 2;
        (x2, y3, z3) = rotate(xs, ys, a1, a2, a3);
        n = sqrt(x2*x2 + y3*y3 + z3*z3);  dx = f32(x2 * (1/n));  dy = f32(y3 * (1/n));  dz = f32(1/n * z3);
        t = max(cylinder(dx, dy, P0, P1),                                 // tunnel along z
                cylinder(dz, dy, P2, P1),                                 // tunnel along x
                cylinder(dz, dx, P2, P0));                                // tunnel along y
        hx = dx t + P0;  hy = dy t + P1;  hz = f32(t dz + P2);
        u = trunc((hx + hy + hz) * 8) << 2;  v = trunc(8 * (hx + hy - hz)) << 2;
        L = f32(46534 - t * 878 * 0.026 * (1/256.0) * 192);              // 46534 − 17.121 t
        if (!(L >= 14048)) L = 14048; else if (L > 64535) L = 64535;
        grid[r][c] = { u - 0x8000, v - 0x8000, trunc(L * B) };
    }
    grid_fill(grid, page);
    fill_rect(page + 45*320 + 10, 0, 170, 40);                            // black box x 10..179, y 45..84
    blit("tunnels meet more tunnels" [0xe35118], 20, 70, page, mode 1, level 0);   // = plain transparent copy
    blit(glyph 4 [0xe350f0], 40, 0, page, mode 1, level 0);                        // 56 × 64: x 40..95, y 0..63
}
```
Constants: 0.07, 0.3, 0.5, 0.01, 0.03, 100.0, 0.043, 0.053 (0x41a1e4..0x41a21c), 8.0, −160, 1/120, −80, 2.0f,
−36864.0f, 4.0f, 0.001, 8.0f, 878.0f, 0.026, 1/256, 192.0, 46534.0, 14048.0f, 64535.0f (0x41a224..0x41a288).

Per pixel:
```
P = 100 (sin 0.03 T4, sin 0.043 T4, sin 0.053 T4);   d = rotate((x − 160)/120, (y − 100)/120, a1, a2, a3), normalised
t = max over the three tunnels of the forward distance from P along d to the wall (radius 192; axes z, x, y)
h = P + t d;   u = (hx + hy + hz)/8 + 128,   v = (hx + hy − hz)/8 + 128   (texels)
L = B · clamp(46534 − 17.121 t, 14048, 64535) / 256  = B · max(181.77 − 0.06688 t, 54.875)
```
- The camera is always inside all three tunnels (|P| per axis pair ≤ 141 < 192), so the visible wall is where the
  ray leaves the last of them: the union of three orthogonal cylinders. Along a tunnel's axis the distance grows
  without bound and L sinks to its floor: the black centre. The texture is a planar projection along (−1, 1, 0),
  one texel per 8 units of (hx + hy ± hz).
- B: before 403 a pulse 0.5 + 0.3 sin(0.07 T4) (1.28 s period, gain at the near wall e^((0.5·181.8 − 128)/23) =
  0.2: dim); from 403 (31:00) black, then up to 1 in 100 units (1.43 s).
- Overwrites rows 20..179; rows 0..19 / 180..199 not written (black, except glyph 4's pixels in rows 0..19).
- Pictures: texture generated (0xe35128 version); "tunnels meet more tunnels" (grey A), glyph 4 (grey B): stored,
  1:1.

## 8. Time base and frame-rate dependence

### How the integrated state relates to time

Every animated variable of these parts is advanced once per frame by dt (a sum of dt times constants, or a clamp),
and main reads the timer T after each shown frame. Frame k is drawn with dt = t70(T_{k−1}) − t70(T_{k−2}) and shows
the state integrated up to T_{k−1} (the `.flips` timer of frame k). The sync check runs right after the timer read,
so an event for row p fires in the first frame whose timer is ≥ the row start, and the change it makes (a new part,
a new rate, a reset) is integrated from **τ = the timer of the frame before** (the last frame timer below the row
start): 0 to one frame interval before the music position. So, exactly up to float rounding (`clocks.py`):

```
T(t)  = 0.005 · (t70 − t70(τ5))                              part 5   (t70 = t70 of the frame's timer)
R(t)  = 0.01 · ∫ from τ5 to t of dir, dir = +1, −1, +1, −1 switching at τ1, τ2, τ3 (21:00, 22:00, 23:00);
        e.g. after the third flip R = 0.01 · ((τ1 − τ5) − (τ2 − τ1) + (τ3 − τ2) − (t − τ3))   (t70 units)
TC    = t70 − t70(τ2)                                        part 2 camera clock
Br    = min(1, 0.3 + 0.01 (t70 − t70(τ2)))  before 0e:00,  min(1, 0.3 + 0.01 (t70 − t70(τ201)))  after
T4    = t70 − t70(τ4);  a_i = Σ over the mode intervals of rate[mode][i] · (length)   (mode switches at τ401's)
B     = 0.5 + 0.3 sin(0.07 · (T4 − dt))  before 31:00;  min(1, 0.01 (t70 − t70(τ403)))  after
clock = 0.05 (t70 − t70(τ11));  fade = max(0, 1 − 0.0065 (t70 − t70(τ1101)))     part 11
```
The capture's τ (`event_lag.py`; lag = row start − τ):

| event | position | row start (s) | τ (s) | lag (ms) | frame interval there (ms) |
|---|---|---|---|---|---|
| 11 (part 11) | 08:20 | 30.2092 | 30.1972 | 12.0 | 35.3 |
| 1101 | 0b:00 | 39.0997 | 39.0995 | 0.3 | 1.5 |
| 2 (part 2) | 0c:00 | 42.6559 | 42.6557 | 0.3 | 1.5 |
| 201 | 0e:00 | 49.7684 | 49.7613 | 7.0 | 24.5 |
| 5 (part 5) | 20:00 | 113.7800 | 113.7635 | 16.5 | 28.5 |
| 501 | 21:00 / 22:00 / 23:00 | 117.3362 / 120.8925 / 124.4487 | 117.3260 / 120.8799 / 124.4339 | 10.3 / 12.5 / 14.8 | 24.0 |
| 4 (part 4) | 30:20 | 172.4574 | 172.4562 | 1.3 | 15.3 |
| 403 | 31:00 | 174.2355 | 174.2115 | 24.0 | 25.8 |
| 402 + 401 / 401 / 401 / 401 | 32:00 / 33:00 / 34:00 / 35:00 | 177.7917 / 181.3479 / 184.9041 / 188.4604 | 177.7692 / 181.3274 / 184.8851 / 188.4431 | 22.5 / 20.5 / 19.0 / 17.3 | 25.5 |

The clocks also drift by float rounding of dt (core scope) by up to +0.14 (part 2) and +0.17 (part 4) units of
1/70 s over the whole part: negligible.

### What depends on the frame rate

- **Part 2**: the camera **position** (Ox, Oz) is computed from the camera clock *before* this frame's dt is added,
  the **rotation** from the clock after it. So the picture at time t shows the position of the previous frame's
  time, t − (frame interval), with the current view direction. This is the frame-rate dependence PARTS.md measured
  (13 / 4.7 at 15 / 84 fps against the capture): the model gives 19.7 / 6.5 over the whole part
  (`lag_effect.py`; PARTS.md measured the first 400 ticks, which begin with the dim first second, probably why its
  numbers are lower; the ratio is similar), and 11.9 between no lag and the
  capture's lag; the change between two capture frames is 12. Not the start value or the float accumulation
  (phase 1's guess): those are below 0.3 ms and 2 ms. Remake: Ox and Oz at TC − Δ with Δ = 1.73 units (24.7 ms,
  the capture's frame interval in this part) reproduces the capture; Δ = 0 is the original at an infinite frame
  rate. Also frame-rate dependent, slightly: the switch at event 201 (from τ201, 7 ms early in the capture).
- **Part 5**: only the direction flips (event 501). R integrates the new direction from τ (the frame before the
  event frame), up to one frame interval before 21:00 / 22:00 / 23:00; after the three flips R differs from
  exact flips at the row starts by 0.01 (l0 − 2 l1 + 2 l2 − 2 l3) (l = lags in 1/70 s, l0 the part start's): in the
  capture −0.006 rad (0.34°), at 15 fps up to ±0.09 rad per flip. T is linear. Remake: flip at the row starts (or
  at the capture's τ for frame-by-frame comparisons).
- **Part 4** (nearly timer-only, as measured): B before 403 uses T4 before this frame's dt (one frame interval
  behind, 1.79 units in the capture: the pulse lags by 0.125 rad); the 403 ramp and the 401 rate switches start at
  τ (17..24 ms early in the capture).
- **Part 11**: none (timer only, as measured).
- Nothing in these parts is stepped per frame without dt (no feedback, no counters).

## 9. Rendering shortcuts to evaluate exactly in the remake

| original | approximates | remake |
|---|---|---|
| 41 × 21 grid, 8 × 8 blocks interpolated linearly (u, v, light; steps floor((b − a)/8)) | the per-pixel functions of sections 5..7 | evaluate u, v, L per pixel (continuous c, r) |
| per-row rotation steps in part 2 (+0.02 / 0.024 / 0.016 per grid row) | a rotation that grows down the screen | r_k = rate_k TC + inc_k (y − 20)/8 per pixel |
| u, v as 8.8 fixed point, 16-bit wrap, packed v:u add without borrow fix | continuous texture coordinates | float coordinates, wrap at 256 texels |
| 4-bit bilinear weights, floor of the byte sum, the v = 255 seam | bilinear sampling of the log-encoded texture | the texture function itself (gfx scope) or a proper filtered lookup |
| L = light >> 8 (integer), T table (integer, truncated) | gain e^((L − 128)/23) on (1 + f) | continuous L and exp; keep the clamp at 255 |
| float32 state and truncating rounding | the closed forms of section 8 | evaluate from time |
| tiles of part 11 (40 × 24) | flat rectangles | flat rectangles at the scaled positions |
| 1:1 pictures (texts, glyphs) at fixed positions | — | redraws at the same place and size (scaled) |

## 10. Shared routines used here (other scopes describe them)

- 0x4043be fill_rect(dst, colour, w, h) (cdecl; w and h are bytes): fills w × h pixels.
- 0x404002 blit(pic EAX, x EDX, y EBX, page ECX; stack: mode, level): mode 0 → 0x4043ec (copy, skip 0xffffffff);
  mode 1 with level ≤ 0 → the same copy; level 1..63 (clamped at 63) → 0x40442c (out = picture + level × (page −
  picture) / 64 through the multiply table [0x4224e8]); other modes draw nothing.
- 0x4034e7 (float → log byte) is used by the gfx scope's texture tables, decoded here by T.

No routine outside the scopes' lists was found in these parts.

## 11. Open points

- The grid models of parts 2 and 4 differ from the original in 1 % / 0.3 % of the entries by one unit (1/256
  texel, one light step): x87 fsin / fcos / fpatan / fsqrt rounding in the emulator (and on a real FPU) versus
  Python's double functions. Not a reading uncertainty: the state and the formulas are exact.
- The look of the texture (lines, colours) is the gfx scope's: this file only uses it through (u, v).
- The capture's τ lags are those of the emulator at 150 M instructions per second; a real 1998 machine would have
  other lags and a lower frame rate (larger position lag in part 2).
