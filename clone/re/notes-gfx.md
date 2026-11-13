# Scope "gfx": the generated textures, the grey palettes and the ten stored pictures

Addresses are linear addresses of the running program (address A is at offset A − 0x400000 of
`unpacked/clone_img.bin`; the remake reads them from the unpacked image). Pixel positions are in the 320 × 200
frame (shown at 4:3). "t70" is main's time in 1/70 s and "dt" its per-frame difference (NOTES.md, "Time base").
FPU: control word 0x1f7f (64-bit mantissas, every operation and every store rounded toward zero, `fistp`
truncates).

Checked (scripts in `models/gfx/`, section 11):

- `texgen.py` reproduces map A, map B, map C (0x40404 bytes each), colour table 1 (0xe6542b) and colour table 2
  (0xe35128) (0x30303 bytes each) of `snaps/t11.snap` **byte for byte**, and the grid texture 0xdf02ec byte for
  byte as 0x4033a7(table 1) at t11 and t2316 and as 0x4033a7(table 2) at t6156, t9324 and t11148. The maps and
  tables are unchanged at t11148 (nothing writes them after the precalc).
- `texplain.py`: the same formulas in plain doubles give the same bytes everywhere except 12,676 bytes of map A
  (one radius, off by one level; section 2.5).
- `pictures.py`: the ten pictures decoded from the image equal the 32-bit copies 0x403f53 made in t11 and t11148.
- `overlay_check.py`, `fade_levels.py`: the boxes, captions and glyphs of parts 2, 5, 4 match `full.frames`
  pixel for pixel (opaque blits); in parts 3, 11 and the credits of part 1 every pixel over black matches the blend
  formula at the level replayed from the frame timings (part 3 at levels 58, 43, 29, 0, 16, 50; part 11 at 0, 19,
  40, 63; credits 26, 0). Part 1's title fades over a non-black background; its replayed level is the one that
  fits best (57, 33, 10).

---------------------------------------------------------------------------------------------------------------

## 1. What this scope covers

| routine | when | makes | used by |
|---|---|---|---|
| main 0x400240..0x4008b4 | precalc ("textures [....]", program ticks 0..10) | map A [0xe35108], map B [0xe3510c], map C [0xe35110]; colour tables 1 (0xe6542b) and 2 (0xe35128); [0xe35114] = map B | maps: parts 7 (A), 8 (B), 9 (C), 10 (B through [0xe35114]) via the texture mapper; tables: the grid filler |
| 0x4033a7 | precalc (table 1); event 5 at 20:00 (table 2) | the grid texture [0x4207c8] = 0xdf02ec | grid filler 0x402970: part 2 (table 1), parts 5 and 4 (table 2) |
| main 0x4008d4..0x400946 | precalc | grey palettes A (0xe34dec) and B (0xe34aec), 256 × 3 bytes of 6-bit values | the eight caption / glyph pictures |
| 0x419d5b | precalc (after the part set-ups) | grey palette C (local), the title and credits pictures | parts 1, 11 |
| 0x403f53 | precalc (10 calls) | 32-bit copies of the ten stored pictures | blits 0x404002 in parts 1, 2, 3, 4, 5, 11 |

Everything here is computed once; nothing is animated except the fade levels of the picture overlays (section
7.4), which belong to the parts.

---------------------------------------------------------------------------------------------------------------

## 2. The texture maps and colour tables (main 0x400240..0x4008b4)

### 2.1 Buffers

```
mapA = alloc(0x40404);  [0xe35108] = mapA   // heap 0x54d3f4
mapB = alloc(0x40404);  [0xe3510c] = mapB   // heap 0x58d7fc
mapC = alloc(0x40404);  [0xe35110] = mapC   // heap 0x5cdc04
// tables in the BSS: tab1 = 0xe6542b, tab2 = 0xe35128, 257 * 256 * 3 + 3 = 0x30303 bytes each
```

Maps: 257 rows of 256 texels, dword 0x00RRGGBB (bytes B, G, R, 0), texel (x, y) at dword y·256 + x, then one more
dword = 0. Tables: 257 rows of 256 entries of 3 bytes **R, G, B**, entry (x, y) at byte 3·(y·256 + x), then 3
zero bytes. Row 256 equals row 0 in all five (the formulas repeat every 256 rows; checked on the bytes).

### 2.2 The loop

```
int prog = 0;                                   // [ebp-0x1c], 0 since "textures [" was printed
for (y = 0; y < 257; y++) {                     // esi
    if (y * 20 / 256 > prog) { print("."); prog = y * 20 / 256; }   // 20 dots over the 257 rows
    for (x = 0; x < 256; x++) {                 // [ebp-4]; ecx = 3·(y·256+x), ebx = 4·(y·256+x)
        double u = x * (1/256.f) * 2.f;         // = x / 128 exactly, [ebp-0x54]
        double v = y * (1/256.f) * 2.f;         // = y / 128, [ebp-0x5c]
        // su = sin(u·pi), sv = sin(v·pi): recomputed for every channel, same value each time
        (block 8)  (block 32)  (block 16)  (block A)   // in this order, see 2.3
    }
}
// after the loop: tab1[ecx..ecx+2] = tab2[ecx..ecx+2] = 0 (ecx = 0x30300), the dword after each map = 0
```

### 2.3 Per texel

`cell(n)`: `dx = x % n − n/2`, `dy = y % n − n/2` (so −n/2 ≤ dx, dy < n/2, the centre is texel n/2),
`r = (int)sqrt(dx² + dy²)` (fsqrt + fistp: **floor** of the distance, an integer).

```
// block 8  (0x4003e6)
r   = floor(sqrt(dx8² + dy8²));                         // n = 8, r = 0..5
c8  = (int)((cos(r * pi * 0.125) + 1) * 48);            // 0..96
tab1[R] = enc23(c8 * 0.7);
tab1[G] = enc23(c8 * (sv * 0.05 + (su * 0.05 + 0.5)));
tab1[B] = enc23(c8 * (sv * 0.15 + (su * 0.15 + 0.7)));
mapC[B] = enc90((c8 * 5) * 0.3);                        // integer c8*5, then * 0.3
mapC[R] = enc90(c8 * (sv * 0.05 + (su * 0.05 + 0.7)));  // byte 2
mapC[G] = enc90(c8 * (sv * 0.15 + (su * 0.15 + 0.7)));  // byte 1
mapC[3] = 0;

// block 32 (0x4005ed); d2 = dx32² + dy32² is kept for block A
r   = floor(sqrt(d2));                                  // n = 32, r = 0..22
c32 = (int)((cos(r * pi * 0.03125) + 1) * 56);          // 0..112
tab2[R] = enc23(c32 * 0.37 * 2);
tab2[G] = enc23(c32 * (sv * 0.05 + (su * 0.05 + 0.42)));
tab2[B] = enc23(c32 * (sv * 0.15 + (su * 0.15 + 0.7)));

// block 16 (0x400706)
r   = floor(sqrt(dx16² + dy16²));                       // n = 16, r = 0..11
c16 = (int)((cos(r * pi * 0.0625) + 1) * 56);           // 0..112
mapB[B] = enc90((c16 * 5) * 0.3);
mapB[G] = enc90(c16 * (sv * 0.05 + (su * 0.05 + 0.7)));
mapB[R] = enc90(c16 * (sv * 0.15 + (su * 0.15 + 0.7)));
mapB[3] = 0;

// block A (0x4002b0, reached by the jmp at 0x40083f)
r   = floor(sqrt(d2));                                  // the 32-cell distance again
cA  = (int)(168 - (cos(r * pi * 0.03125) + 1) * 56);    // 56..168, bright between the cells
mapA[B] = enc90((cA * 5) * 0.3);
mapA[G] = enc90(cA * (sv * 0.05 + (su * 0.05 + 0.7)));
mapA[R] = enc90(cA * (sv * 0.15 + (su * 0.15 + 0.7)));
mapA[3] = 0;
```

Evaluation order inside a channel (it matters only for the exact bytes): `su·k + a0` is stored to a double
(truncated) before `sv·k` is added; the product with c is passed to the encoder as a **float** (truncated to 24
bits). Constants (doubles unless noted): pi 0x41a0a8, 0.125 0x41a0b0, 48 0x41a0b8, 0.7 0x41a0c0, 1/256 (float)
0x41a0c8, 2 (float) 0x41a0cc, 0.05 0x41a0d0, 0.5 0x41a0d8, 0.15 0x41a0e0, 0.3 0x41a0e8, 0.03125 0x41a0f0, 56
0x41a0f8, 0.37 0x41a100, 2 0x41a108, 0.42 0x41a110, 0.0625 0x41a118, 168 0x41a120.

The cell brightness depends only on the integer radius (values computed by the model, exact FPU):

| r | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| c8 | 96 | 92 | 81 | 66 | 48 | 29 | | | | | | | | | | | | | | | | | |
| c16 | 112 | 110 | 107 | 102 | 95 | 87 | 77 | 66 | 56 | 45 | 34 | 24 | | | | | | | | | | | |
| c32 | 112 | 111 | 110 | 109 | 107 | 105 | 102 | 99 | 95 | 91 | 87 | 82 | 77 | 72 | 66 | 61 | 56 | 50 | 45 | 39 | 34 | 29 | 24 |
| cA | 56 | 56 | 57 | 58 | 60 | 62 | 65 | 68 | 72 | 76 | 80 | 85 | 90 | 95 | 101 | 106 | **111** | 117 | 122 | 128 | 133 | 138 | 143 |

The corners of a cell never reach the cos minimum (that is at r = n; the corners are at r ≤ 0.71 n), so every
cell is a bright spot (maps B, C, tables) or a dark spot (map A) on a lighter grid of cell borders. su, sv add a
colour drift with one period over the 256-texel texture in x and in y.

### 2.4 The two encoders: brightness stored as a logarithm

```
// 0x4034e7 (float x), for the colour tables (constants 23 at 0x41a14c, 128 at 0x41a154)
int enc23(float x) { int i = (int)(ln(1 + x) * 23 + 128);                 return clamp(i, 0, 255); }
// 0x4167b7 (float x), for the maps (0.0078125 float 0x41a490, 90.909090... = 1000/11 at 0x41a498, 128 at 0x41a4a0)
int enc90(float x) { int i = (int)(ln((1 + x) * 0.0078125) * 90.9090909 + 128); return clamp(i, 0, 255); }
```

(ln = `fldln2; fyl2x`.) Both store L = 128 + K·ln(value) so that the fillers can scale a colour by adding to L.
The decoders are the exp tables of the core scope (512 bytes each, indexed by L plus a shade offset k):

- 0x420854 (0x40347a), read by the grid filler: `E23[i] = min(255, (int)exp((i − 256) / 23))`. So
  `E23[L + k] = (1 + x) · exp((k − 128) / 23)`: the table entry is the **linear value 1 + x** (0..255 scale),
  and an offset k multiplies it by exp((k − 128)/23).
- 0x426b8c (0x416806), read by the texture mapper's fillers: `E90[i] = min(255, (int)(128 · exp((i − 256) ·
  0.011)))`. So `E90[L + k] = (1 + x) · exp((k − 128) · 0.011)`, again the linear value 1 + x.

So a remake can store **linear** textures `V = 1 + x` (0..255, divide by 255 for display-space floats) and
multiply by the fillers' shade. Three things the log form adds:

- enc90 clamps L at 0 when 1 + x < 128 · e^(−128 / 90.909) = 31.31: the maps' dark values are raised to 31.31
  (12 %) instead of going black (map C: 7,160 R and 11,600 G texels; map B: 6,474 R, 3,796 G; map A: 1,464 R).
  enc23 never clamps (the tables' x ≤ 112 gives L = 128..236).
- Truncating c and L lowers the result: L by up to one level (a factor of up to e^(−0.011) for the maps,
  e^(−1/23) = −4.3 % for the tables), c by up to one unit; on average the original is 0.6 (tables) to 1.0 (maps)
  levels darker than the same formula without truncation (section 4).
- The fillers interpolate the stored levels L bilinearly (2 × 2 texels, the 16 × 16 weight tables 0x420a54 /
  0x426d8c) and decode afterwards, so between two texels the linear value follows a geometric, not an arithmetic,
  mean. Filtering linear values instead is brighter halfway between texels by up to 3.4 % (table 1, map C;
  under 1.5 % in the others).

Value ranges (stored levels L): map A B 90..175, G 10..118, R 0..138; map B B 15..153, G 0..96, R 0..116;
map C B 31..139, G 0..102, R 0..82; table 1 R 198..225, G 186..221, B 186..233; table 2 R 195..229, G 177..221,
B 182..236. In words: map A is light violet-blue with dark blue spots every 32 texels, map B a blue / violet
lattice of round spots every 16 texels, map C a fine blue lattice every 8 texels (the darkest of the three), and
the two tables are pale pink-grey (table 1: 8-texel spots; table 2: 32-texel spots, pinker) that the grid filler
darkens with its shading.

### 2.5 Precision

The exact FPU model (truncating 64-bit arithmetic, truncating double / float stores, QEMU's double-precision
fsin / fcos / fyl2x) reproduces the snapshot. Plain doubles (round to nearest) give the same bytes everywhere
except at one radius: block A with r = 16, where cos(pi/2) = 6.1e-17 makes (cos + 1)·56 = 56.0000000000000034
in the truncating 64-bit FPU, 168 minus that truncates to **111**, while doubles round it to 56 and give 112 (12,676
bytes of map A differ by one level). A byte-exact port must special-case that; a remake that uses float textures
does not need to.

---------------------------------------------------------------------------------------------------------------

## 3. The grid texture (0x4033a7)

```
void grid_texture(const u8 *table)              // argument on the stack; [0x4207c8] = 0xdf02ec (BSS, 0x40404 bytes)
{
    u32 *dst = [0x4207c8], eax = 0;             // EAX cleared once, before the loop
    for (i = 0; i < 0x10000; i++, table += 3) { // the first 256 rows of the table
        eax = (eax & 0xffffff00) | table[0];    // al = R
        eax <<= 16;                             // previous G -> bits 24..31, R -> bits 16..23
        eax = (eax & 0xffff0000) | table[1] << 8 | table[2];
        dst[i] = eax;                           // = prevG << 24 | R << 16 | G << 8 | B
    }
    for (i = 0; i < 0x101; i++) dst[0x10000 + i] = dst[i];   // row 256 = row 0, plus row 1's first texel
}
```

- Called in the precalc with table 1 (0x4008c2) and by event 5 (20:00, 0x400bf9) with table 2; nothing restores
  table 1, so part 2 uses table 1 and parts 5 **and 4** use table 2 (checked in the snapshots).
- The top byte of every texel is the previous texel's G (a register left over). The grid filler masks each texel
  with 0xff00ff and reads G separately (0x402b2f..0x402b3e), so it never shows. A remake ignores it.
- Layout: 256 × 256 texels, then row 0 again and one more texel, so the filler's 2 × 2 bilinear reads
  (+0, +4, +0x400, +0x404) never leave the buffer. At u = 255 the "+4" texel is (0, y + 1) instead of (0, y): a
  one-texel seam where the texture wraps in u (grid scope). A remake uses a repeating 256 × 256 texture.

---------------------------------------------------------------------------------------------------------------

## 4. Regenerating the textures at a higher resolution

All five are functions of texel coordinates (u, v) in original texel units, period 256 in both directions
(texel (x, y) of the original = f(x, y); in GL normalised coordinates s = (u + 0.5) / 256, so a texel X of an
N-wide texture is sampled at u = (X + 0.5) · 256 / N − 0.5):

```
su = sin(pi u / 128), sv = sin(pi v / 128)
lo(a0) = a0 + 0.05 (su + sv),  hi = 0.7 + 0.15 (su + sv)
r_n = |((u mod n) − n/2, (v mod n) − n/2)|                         // original: floor(r_n), integer texels
c8  = (cos(pi r_8 / 8) + 1) 48,  c16 = (cos(pi r_16 / 16) + 1) 56
c32 = (cos(pi r_32 / 32) + 1) 56, cA = 168 − c32
linear value V = 1 + x, with x per channel:
  map A  R: cA hi       G: cA lo(0.7)    B: 1.5 cA        V clamped below at 31.31
  map B  R: c16 hi      G: c16 lo(0.7)   B: 1.5 c16       V clamped below at 31.31
  map C  R: c8 lo(0.7)  G: c8 hi         B: 1.5 c8        V clamped below at 31.31
  table1 R: 0.7 c8      G: c8 lo(0.5)    B: c8 hi
  table2 R: 0.74 c32    G: c32 lo(0.42)  B: c32 hi
```

(as stored levels: L = 128 + 23 ln V for the tables, L = 128 + (1000/11) ln(V / 128) for the maps.)

What the original quantises, and what a sharper version changes (`texplain.py`, levels L compared with the
original bytes at the original texels):

| variant | map A | map B | map C | table 1 | table 2 |
|---|---|---|---|---|---|
| floor(r), no truncation of c and L | +1.06 (max 2) | +0.97 (3.4) | +0.96 (2.9) | +0.63 (1.5) | +0.62 (1.6) |
| continuous r | +2.71 (6.2) | −2.93 (20.8) | −4.65 (31) | −1.05 (9.4) | 0.00 (3.0) |

(mean signed difference in levels, max |difference| in brackets; one level is 1.1 % for the maps, 4.3 % for the
tables.) The floor of the radius makes stepped rings, one texel wide; in the 8- and 16-texel cells the steps are
large, so the original's spots are squarer and brighter at the edges than the smooth formula. Since the fillers
read the textures bilinearly, what the original shows is the bilinear interpolation of the 256 × 256 floor(r)
texture: evaluating that texture in floats (first row) and filtering it (best in the log domain, 2.4) keeps the
look; the
continuous radius at a higher resolution gives round, smooth spots (same content, slightly different profile,
map C on average 5 % darker). Either way the cell sizes, the colour drift and the value ranges stay.

---------------------------------------------------------------------------------------------------------------

## 5. Grey palettes

Static data in the image: two tables of 16 dwords (percentages):

| address | values (k = 0..15) |
|---|---|
| 0x41b7c2 (grey A) | 0 94 88 81 75 69 63 53 45 39 33 27 20 14 8 100 |
| 0x41fc1c (grey B) | 0 97 91 81 74 65 58 48 45 36 29 23 13 10 4 100 |

```
// main 0x4008d4..0x400946
memset(0xe34aec, 0, 0x300); memset(0xe34dec, 0, 0x300);       // byte loop, ecx = 1..0x300
for (c = 0; c < 16; c++) {
    e = 255 - c;                                                // entry 240 + k gets table[k], k = 15 - c
    palA[e].r = palA[e].g = palA[e].b = tableA[15 - c] * 63 / 100;   // palA = 0xe34dec
    palB[e].r = palB[e].g = palB[e].b = tableB[15 - c] * 63 / 100;   // palB = 0xe34aec
}
// 0x419d5b: palC[c] = c * 63 / 15 (c = 0..15), a 48-byte local array (entries 16.. are never used)
```

As the 8-bit values the pictures get (6-bit value << 2; white is 252):

| index | 240 | 241 | 242 | 243 | 244 | 245 | 246 | 247 | 248 | 249 | 250 | 251 | 252 | 253 | 254 | 255 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A | 0 (transparent in the texts) | 236 | 220 | 204 | 188 | 172 | 156 | 132 | 112 | 96 | 80 | 68 | 48 | 32 | 20 | 252 |
| B | 0 | 244 | 228 | 204 | 184 | 160 | 144 | 120 | 112 | 88 | 72 | 56 | 32 | 24 | 8 | 252 |

Grey C (index 0..15): 0 (transparent), 16, 32, 48, 64, 84, 100, 116, 132, 148, 168, 184, 200, 216, 232, 252.
All three are greys (R = G = B); the anti-aliasing ramp runs from 241 (lightest) to 254 (darkest), 255 is white.

---------------------------------------------------------------------------------------------------------------

## 6. The picture decoder (0x403f53)

```
typedef struct { u32 *pixels; int w, h; } Picture;            // 12 bytes

// EAX = 8-bit pixels, EDX = 6-bit palette (3 bytes per entry), EBX = w, ECX = h, stack: keep_f0; ret 4
Picture *make_picture(const u8 *src, const u8 *pal, int w, int h, int keep_f0)
{
    Picture *p = alloc(12);  p->w = w;  p->h = h;
    p->pixels = alloc(w * 4 * h);
    for (i = 0; i < w * h; i++) {
        u8 c = src[i], *e = pal + 3 * c;
        p->pixels[i] = (u8)(e[0] << 2) << 16 | (u8)(e[1] << 2) << 8 | (u8)(e[2] << 2);
        if (c == 0 || (c == 0xf0 && keep_f0 == 0)) p->pixels[i] = 0xffffffff;   // transparent
    }
    return p;
}
```

The pictures are raw 8 bits per pixel, row by row, no header, stored back to back from 0x41a950 to 0x420489
(with the two percentage tables between them). Each 0x403f53 result is a heap block; the blits skip 0xffffffff.

---------------------------------------------------------------------------------------------------------------

## 7. Pictures

PNG: `dump/pictures/<name>.png` (RGBA as the original shows them: the palette value << 2, transparent pixels
alpha 0; `dump/pictures/extra/` has the raw indices, 8× enlargements and the pictures on black).

### 7.1 The ten pictures

| name (PNG) | address | w × h | palette, transparent | pointer → heap | kind | drawn by |
|---|---|---|---|---|---|---|
| meet_us | 0x41a950 | 42 × 9 | A, 240 | [0xe35124] → 0x60e00c | text, sans serif | part 2 |
| get_them_clones | 0x41aaca | 85 × 12 | A, 240 | [0xe35120] → 0x60e608 | text | part 5 |
| connect_the_dots | 0x41aec6 | 88 × 10 | A, 240 | [0xe3511c] → 0x60f60c | text | part 3 |
| tunnels_meet_more_tunnels | 0x41b236 | 142 × 10 | A, 240 | [0xe35118] → 0x6103e0 | text, "meet" bold | part 4 |
| glyph1 | 0x41b802 | 77 × 65 | B, 0 | [0xe350fc] → 0x611a24 | hand-drawn | part 2 |
| glyph2 | 0x41cb8f | 63 × 67 | B, 0 | [0xe350f8] → 0x61686c | hand-drawn | part 5 |
| glyph3 | 0x41dc0c | 68 × 68 | B, 0 | [0xe350f4] → 0x61aa74 | hand-drawn | part 3 |
| glyph4 | 0x41ee1c | 56 × 64 | B, 0 | [0xe350f0] → 0x61f2c8 | hand-drawn | part 4 |
| title_clone_meets_clone | 0x41fc5c | 101 × 9 | C, 0 | [0xf73930] → 0x902c8c | text, "meets" bold | parts 1, 11 |
| credits_aap_echo_simon | 0x41ffe9 | 37 × 32 | C, 0 | [0xf7392c] → 0x903ad4 | text, 3 lines | part 1 |

(heap = the 12-byte header; the pixels follow 16 bytes later.) All ten are stored pixels, so all are Aap's work;
none is generated. The four glyphs are drawn pictures; the six texts are anti-aliased renderings of a font.

**Glyphs.** Four cartoonish brush-style characters that look like kanji (my readings, guesses: 長, 系, 隼, 宜),
each a white shape (index 255) with a thick black outline (index 240, about 3–4 pixels), grey anti-aliasing
(241..254) between the white and the black, inner details in black (glyph 1: three small marks and a curved
stroke in its lower part; glyph 2: two slanted slits; glyphs 3 and 4: short rungs). Outside the outline index 0
(transparent, a hard 1-bit edge: the black outline meets the background without anti-aliasing). No enclosed
transparent holes. The outline is part of the picture (with palette B index 240 is opaque black).

**Texts.** White letters (255 = 252/255 grey) anti-aliased with 14 grey levels **towards black**, on index 240
(transparent). Lower case, a Helvetica / Arial-like sans serif, x-height 7 pixels, ascenders 2 more, descenders 3
(the g of "get"). "tunnels **meet** more tunnels" and "clone **meets** clone" have the middle word in bold.
The credits are three right-aligned lines ".aap", ".echo", ".simon" (baselines 13 and 12 pixels apart, a dot
before each name). The title and credits use palette C (0 transparent, 1..15 an even grey ramp) and are
anti-aliased the same way. The dark anti-aliasing pixels are opaque, so the texts need a black backdrop, and in
every use but one they have it (the black boxes, black screen; part 1's title lies on the dark particle field).

### 7.2 How they are drawn

Every use is a 1:1 blit at a fixed position (no scaling, warping, colouring, masking or texture use; no palette
effect reads the indices). The blit 0x404002 (EAX = picture, EDX = x, EBX = y, ECX = target [0xe350ec]; stack:
mode, level) skips 0xffffffff pixels and

- mode 0, or mode 1 with level ≤ 0: copies the pixel (opaque);
- mode 1 with level 1..63 (clamped to 63): `out = src + trunc((dst − src) · level / 64)` per channel (0x40442c
  with the table 0x403f10: [0x4224e8] + level · 512 + (dst − src), entries trunc(d · level / 64)). So the
  picture's opacity is 1 − level/64; **level 63 still leaves 1/64 of the picture** (white over black: 4/255).

Each caption part first fills a black box with 0x4043be (dword 0 into a w × h rectangle), then the text, then the
glyph, after its own picture (grid filler or 8-bit expansion), so the overlays are on top:

| part (code) | box (x, y, w × h) | text at | glyph at | mode, level | on screen |
|---|---|---|---|---|---|
| 2 (0x419cfa..0x419d4a) | (10, 130) 100 × 40 | meet_us (20, 151) | glyph1 (80, 111) | 0: opaque | ticks 2316..3083 (0c:00..10:00), every frame |
| 5 (0x403765..0x4037b5) | (180, 30) 130 × 40 | get_them_clones (215, 53) | glyph2 (140, 0) | 0: opaque | 6156..6923 (20:00..24:00) |
| 3 (0x405ef9..0x405f81) | (162, 135) 148 × 40 | connect_the_dots (212, 160) | glyph3 (212, 87) | 1, level L3 (7.4) | 7884..9323 (29:00..30:20) |
| 4 (0x403d9c..0x403dec) | (10, 45) 170 × 40 | tunnels_meet_more_tunnels (20, 70) | glyph4 (40, 0) | 1, level 0: opaque | 9324..10379 (30:20..36:00) |
| 1 (0x40587e..0x4058e5) | – | title (141, 104) if [0x422510] ≠ 0; credits (141, 74) if [0x422508] ≠ 0 | – | 1, levels trunc([0x422514]), trunc([0x42250c]) | title 08:00..08:20, credits 3c:10..end |
| 11 (0x419fbe..0x419fee) | (the black tiles, grid scope) | title (141, 104) | – | 1, level trunc((1 − [0x428464]) · 63) | 1645..2315 (08:20..0c:00) |

The glyphs overlap their boxes (glyph 1 the right end of the box, x 80..109; glyph 2 its top-left corner; glyph 3
its top, y 135..154; glyph 4 its top, y 45..63): the composition is a glyph standing on the corner of a black
caption box. The texts lie inside the boxes. In part 1 the title lies on the expanded particle picture (rows
16..175), the credits on black.

### 7.3 What a redraw must keep

- **Glyphs** (redraw them): the silhouette and its position exactly (the transparent area shows the moving
  background, and the overlap with the box must stay), the thick black outline, the white fill, the inner black
  details and the stroke shapes; grey only (no colour). The outer edge may get anti-aliased alpha (the original
  edge is a hard 1-bit mask). They are always drawn whole and upright, opaque except part 3's uniform fade.
- **Texts** (redraw, or set in a matching font): the exact strings, lower case, weight per word (bold middle
  word in two of them), left edge and baseline position, height (x-height 7 of 200 lines), white anti-aliased on
  black; the credits right-aligned. White text with alpha coverage over black gives the original's grey edges.
  The one text on a non-black background is part 1's title (on the dark particle field, while it fades in): there
  the original's dark edge pixels are opaque and leave a dark rim of at most one pixel, which alpha coverage would
  not.
- Colours: white is 63/63 in the remake's display-space convention (the original shows 252/255).

### 7.4 The fade levels (time bases; the variables belong to the part scopes)

- **Part 1 title**: [0x422514] (float, 63.0 in the image) −= 0.45 · dt per frame while [0x422510] ≠ 0 (event 106
  at 08:00 sets it, 111 at 3a:00 clears it) and it is > 0, clamped at 0 (0x405539..0x405575); level = trunc.
  Linear in dt: level ≈ 63 − 0.45 · 70 · (t − t(08:00)) with t in seconds of music time (the original starts it
  with the dt of the event's frame, so up to a frame early), 0 after 140 units = 2.0 s. Part 1 ends at 08:20
  (1.78 s), so the title reaches about level 7 there; part 11 then shows it opaque.
- **Part 1 credits**: [0x42250c] (63.0) −= 0.75 · dt from event 107 (3c:10), ≥ 0: opaque after 1.2 s.
- **Part 11 title**: [0x428464] (1.0) −= 0.0065 · dt from event 1101 (0b:00), ≥ 0 (0x419df6..0x419e2f); level =
  trunc((1 − f) · 63): opaque until 0b:00, then 63 after 153.8 units = 2.2 s (tick 2243), and a 1/64 ghost
  until the part ends.
- **Part 3 caption and glyph**: level = max(0, trunc(63 − 0.05 · v)) (0x405f12..0x405f4a; 0.05 at 0x41a3c0, 63 at
  0x41a3c8), v = [0x4224fc] (int), 0 at event 3 (0x405b3f). Per frame (0x405c5b..0x405cf0): while v < 5000 and
  [0x422518] = 0: v = trunc(v + 5 · dt), ≤ 5000; after event 301 (30:00, [0x422518] = 1): v = trunc(v − 50 · dt),
  ≥ 0. **Not linear in dt**: v is an integer, so each frame loses the fraction (the fade-in advances by floor(5
  dt) per frame: at the capture's 52 fps 6 instead of 6.73, so it takes about 4.0 s instead of 3.6 s; the fade-out
  starts about 1.07 s after 30:00 and takes about 0.36 s). The capture's frame rate (PARTS.md: 52 fps) is the
  reference; `fade_levels.py` replays it from `full.flips` and matches the captured levels. v is the particles
  scope's variable ([0x4224f8] follows it; it also drives the dot field).

The levels are integers 0..63 and the blend truncates per channel; a remake uses opacity 1 − level/64 with the
level as a float (from the same formulas without trunc), and keeps (or deliberately drops) the 1/64 ghost.

### 7.5 Another stored colour picture: part 11's mosaic (not one of the ten)

0x428164: 64 entries of three dwords (R, G, B in percent, 0..100), an 8 × 8 mosaic that runs from yellow /
orange at the top left to dark violet at the bottom right (`dump/pictures/extra/mosaic_part11.png`, previewed as
percent × 2.55). Part 11 reads entries 8..55 (rows 1..6, from index 0x18 dwords, 3 per tile) for its 8 × 6 tiles,
scaled by 0.0125 · 255 and a per-tile pulse (grid scope). It is stored data (Aap's, or a downscaled image); as
coloured squares it needs no redraw. Not in NOTES.md's list: the core scope may add it.

---------------------------------------------------------------------------------------------------------------

## 8. Time bases

Nothing in this scope is animated: the textures, tables, palettes and pictures are made once in the precalc
(0x4033a7 runs once more at event 5, a copy). The only time-dependent values are the overlay fade levels of
section 7.4: the part 1 title, the credits and the part 11 title are linear in dt (closed forms given); part 3's
level comes from an integer that truncates every frame (frame-rate dependent).

## 9. Shortcuts a high-resolution remake should evaluate exactly

- Pictures: 320 × 200 pixels blitted 1:1 → redraws (or font text) at the output resolution; 6-bit grey palettes
  → display-space floats; 64 blend levels, truncated per channel → float opacity.
- Textures: 256 × 256 texels with integer radii (floor of sqrt), integer cell brightness, 8-bit log-encoded
  levels decoded through 512-entry exp tables (with clamps) → float linear values (section 2.4), either the
  floor(r) texture filtered linearly (the original's look) or the continuous formula at a higher resolution
  (section 4). The top byte of the grid texels and the u-wrap seam (section 3) are artefacts to drop.

## 10. Frame coverage

The overlays draw over the part's own picture and never clear anything; whether a part overwrites the whole frame
is the part scopes' question. In the 15-bit mode the same overlays are drawn into the conversion buffer.

## 11. Models (`models/gfx/`, run inside the toolbox from `clone/re`)

| file | what it checks |
|---|---|
| `texgen.py [--snap S] [--png DIR]` | the exact model of 2.2..2.4 and 3 (a truncating soft float in Python ints, QEMU's double-precision fsin / fcos / fyl2x): byte for byte against t11 / t2316 (maps, tables, grid texture from table 1) and t6156 / t9324 / t11148 (grid texture from table 2). `--fyl2x new` (a full-precision log) gives the same bytes |
| `texplain.py [--png N]` | the formulas in numpy doubles (the port's version: identical except map A r = 16), and the variants of section 4; `--png 4` writes `dump/textures/hires_*.png` |
| `pictures.py [--check S]` | decodes the ten pictures with palettes A, B, C, compares them with the snapshot copies, writes `dump/pictures/` |
| `overlay_check.py [ticks]` | boxes, texts and glyphs against `full.frames` (opaque: exact compare; blended: the level that fits) |
| `fade_levels.py` | replays the four fade levels from `full.flips` and prints them next to `overlay_check`'s fit |

## 12. Open points

- The glyphs' readings as kanji are guesses (they are stylised; the remake does not need them).
- The font of the texts is not named anywhere; "Helvetica / Arial-like" is a description, not a finding.
- How the texture mapper and the grid filler choose the shade offset k (and so the final brightness of a texel)
  is the engine and grid scopes' part; section 2.4 only gives the encoding.
