# Paper — start-up code and shared code (scope "init")

All addresses are code32 offsets (= emulator linear addresses). "verified" = regenerated in Python from the
description below and compared byte-for-byte with emulator memory (dumps taken right after init at 0x763e and
after main's set-up at 0x76b5, scripts in the scratch dir, see the end). Fixed-point notation: "16.16" = int32
with 16 fraction bits, "8.8" = 16-bit value with 8 fraction bits. Angles: 1024 units per turn everywhere
(sine table index & 0x3ff); cos(a) = sin[(a + 0x100) & 0x3ff].

## 0. Quick index (address -> what, section)

| addr | what | § |
|---|---|---|
| 0x0240 | getmem: eax bytes -> eax pointer (bump allocator) | 4 |
| 0x0aa2 | random: additive lagged Fibonacci, 64 x u32 state at 0x9a0 | 4 |
| 0x0ae2 | 3x3 matrix multiply (16.16) edi = ebx * esi | 4 |
| 0x0b1f | rotation matrix Rx(eax)·Ry(ebx) -> [esi] | 4 |
| 0x0baa / 0x0bed / 0x0c30 | Rx / Ry / Rz (eax) -> [edi] | 4 |
| 0x0c80 | init: 512x256 light map for the bump mapper -> [0x1edd1] | 5.8 |
| 0x0d28 | bump mapper of the water part (uses the light map) — water scope | 5.8 |
| 0x0e2e | init: decode RLE font (29 glyphs) -> [0x1ede9] | 5.9 |
| 0x0e8a | draw glyph (additive, saturating) into a byte buffer, stride self-modified at 0xeb8 | 8.12 |
| 0x0edc | draw glyph scaled 3x horizontally, 7x vertically (stride 320) | 8.12 |
| 0x0f47 | blur in place: 2 passes of the 4-neighbour average (esi buf, ecx rows, ebp stride) | 8.11 |
| 0x0f90 | one 4-neighbour average pass (esi src, edi dst, ecx n, ebp stride, edx = -stride) | 8.11 |
| 0x0fb2 | init: "1996" glow image [0x1ede1] + "psychic link" image [0x1ede5] | 5.10 |
| 0x109a | NOT a rectangle in the [0x1ede5] buffer | 5.10 |
| 0x10d3 | init: 256x256 grey "kaleidoscope" texture -> [0x1eddd] | 5.5 |
| 0x11b0 | draw zero-terminated string with 0xe8a | 8.12 |
| 0x13af / 0x13bb | blur the "1996" image ebx times (0x13af: with a global budget [0x1edad]) | 5.10 |
| 0x144b | init: square table (transient) + shade table [0x1eded] + multiply table [0x1eded]+0x10000 | 5.6 |
| 0x14dc | palette generator (hue angle self-modified at 0x14dd, saturation shift at 0x1505) | 6 |
| 0x1579 | grey palette (unused, no caller) | 6 |
| 0x158e | default palette + 32 colours from esi at 128..159, upload | 6 |
| 0x15af | default palette 0x169a3 -> working palette, upload | 6 |
| 0x15c0 | upload working palette 0x1eaad to the DAC with fade level [0x1edb5] | 6 |
| 0x1612 | "pencil" line, 2 px wide, darkens through the shade table | 8.1 |
| 0x18b7 | sketchy line: 3 x 0x1612 with deterministic per-endpoint jitter | 8.2 |
| 0x1943 / 0x1949 | shade a 320x200 buffer by a per-pixel light map | 8.3 |
| 0x1982 | polygon edge scan into the edge table 0x20e15 | 8.4 |
| 0x1a87 / 0x1ac3 | triangle / quad edges (esi -> 3 / 4 points x,y int32) | 8.4 |
| 0x1b10 / 0x1b62 | flat span fill (store / OR) from the edge table | 8.4 |
| 0x1bae | vertical colour gradient over full rows | 8.5 |
| 0x1e45 | IRQ0 handler: music, palette fade, effect callback | 6 |
| 0x2756 / 0x26d0 | random oscillator set [0x1edd5] / per-tick phase advance | 8.8 |
| 0x2ad0 | quadratic through 3 values, n+1 samples | 8.6 |
| 0x2b20 / 0x2b55 / 0x2b6c | page-peel transition (shared by peel and teal parts) | 8.14 |
| 0x2db3 | set the light field of the 41x26 grid | 8.9 |
| 0x2dc8 | warp grid (title, water) from the oscillators | 8.9 |
| 0x2f24 | canyon grid (teal plane, plane flock): ray / plane intersection per grid point | 8.10 |
| 0x3095 | 8x8-block bilinear texture mapper over the 41x26 grid, 320x200 | 8.7 |
| 0x377f / 0x3793 | random in -mask..mask / three of them | 4 |
| 0x39a4 | object from compact byte data (pencil, paper planes) | 7.2 |
| 0x3a8c / 0x3c3e / 0x3b69 | banner mesh (17x11 grid) / its vertex positions / the vertex warp | 7.4 |
| 0x3cb7 | object from table entry (word data) | 7.2 |
| 0x3dc0 / 0x3eeb / 0x3f37 | face normals / edge neighbours + crease flags | 7.3 |
| 0x3fb3 | object transform, light, cull, depth-bucket | 7.5 |
| 0x42e4 | pencil shadow mask (paper copy at [8]) | 7.7 |
| 0x43df | draw all bucketed faces far-to-near + pencil edges | 7.6 |
| 0x55e2 | init: paper texture -> [0x1edf5] | 5.4 |
| 0x58ea | unpack 4-bit 224-wide image | 8.13 |
| 0x593b / 0x595d | loading progress bar / DOS print string | 1 |
| 0x5979 | init: banner drawing strokes (Statix + Vic (acme)) -> [0x483b] | 5.11 |
| 0x5b6b | clear VGA | 8.15 |
| 0x5b7d | init | 1 |
| 0x5dfc / 0x5e1b | transform a list of objects then draw (0x43df) / record sketch order | 7.8 |
| 0x679b | 16.16 int matrix -> float matrix at 0x23a8 (for 0x2f24) | 8.10 |
| 0x7626 | main | 2 |
| 0x7a64 | ESC check (not a per-frame update) | 8.15 |

## 1. 0x5b7d init (ticks 1..100 in the emulator at 1.5M instructions/tick; black screen)

```
setirqvect(IRQ1, 0x7a6f)            // keyboard: just reads port 0x60 and acks the PIC
print(0x4530)                       // 0x595d: int 21h/9 via PMODE int 0x33 (DS=0x10d0, DX=si), '$'-terminated:
                                    // "Statix and Vic present their Wired '96 64k intro (c) 1996\r\n\nGosh <" + 18 x 0xfa + ">\r"
recip[n-1] at 0x1ee19 = 65536 / n   for n = 1..2048 (u32; [0x1ee15 + 4n] = 65536/n)
progress()                          // 0x593b: writes 0xfe into the bar at 0x4572+[0x24479]++ and reprints 0x456c
[0x2449d] = getmem(0x540), zeroed; [0x24499] = 0   // 28 x 48-byte ring buffer of the teal plane part (camera history)
[0x1ee05] = [0x1ee09] = A = getmem(0x800)
for i in 0..1023: A[i] = shade_byte()            // §5.3
[0x1ee0d] = A + 0x400; for i in 0..1023: A[0x400+i] = jitter_byte()   // §5.3
[0x1edfd] = getmem(0x20000) + 0x800             // 128K scratch
[0x1edd1] = (getmem(0x201ff) + 0x1ff) & ~0x1ff  // 512x256 light map, 512-aligned
[0x1edc5] = getmem(0xfa00), zeroed              // 320x200 work/mask buffer (logo + peel parts)
[0x1edd5] = getmem(0x5a0)                       // oscillator records (§8.8), 120 x 12 bytes (72 used)
p = getmem(0x3f200), all zeroed; [0x1edc9] = p + 0x140; [0x1edcd] = p + 0x140 + 0xfc80
q = getmem(0x44200); [0x1eddd] = (q + 0xffff) & 0xffff0000; [0x1eded] = [0x1eddd] + 0x20000
[0x1edd9] = getmem(0x31f8)                      // 41 x 26 x 12-byte grid
[0x1edf5] = getmem(0x10200)                     // paper
[0x1edf9] = getmem(0xff00) + 0x280; fill 64000 bytes with 0xff   // back buffer
progress(); 0x10d3(); 0x144b(); 0x55e2(); 0xe2e(); progress(); 0xfb2(); 0xc80()
sine table (§4); [0x1edf1] = 0xa0000 - [0x18] (VGA); 0x5979(); progress()
int 10h ax=0x13 (via int 0x33, al=0x10); 0x15af() (level 0 -> black DAC); 0x5b6b() (clear VGA)
[0x1edbd] = 0x8000 (fade target); [0x1edb9] = 0x96 (step)
```
Random calls: main does 72 warm-up calls before init; init consumes 1024 + 1024*120 + 16081 = 140057 calls in
total (counted in the emulator); after init the generator indices are i = 54, j = 24. The title part (0x2829)
then uses the generator too (162929 calls in total by tick 1079), so a remake that wants the exact title/teal
random warps must replay this exact call sequence (or just reseed; the look is the same).

## 2. main set-up 0x7626..0x76d8 (ticks 0..104)

```
0x1c24 sound init (music scope); 72 x rand(); 0x5b7d init
for i in 0..10: [0x5db8 + 4i] = 0x3cb7(table entry 0x174a3 + 12i)   // scene objects, §7.1
0x3a8c()  -> banner mesh, stored in [0x5dd8] = slot 8 (OVERWRITES the pointer of table object 8, a third pole)
[0x5de4] = 0x39a4(esi = 0x1e353, al = 0) = pencil (slot 11); pencil.pos.x = pencil.pos.y = 500 (+0x48, +0x4c)
[0x1ea93] = 1                       // music player enabled in the IRQ from now on
0x15af(); [0x1edb9] = 0x190
back buffer = 0x3e everywhere; 0x1943(ebp = paper)  // paper look (greys 54..60), overwritten by the title anyway
[0x2245d] = 0; esi = 0x463a "psychic link\0sixty four k"; [0x1edb9] = 0x96; [0x1ea8f] = 0x70; 0x2829 (title)
```
Object slots after set-up (12 dwords at 0x5db8): 0 sun, 1 path, 2 house, 3 pole, 4 pole, 5..7 trees, 8 banner
(0x3a8c), 9 house windows/door, 10 flower, 11 (= [0x5de4]) pencil.
Main-loop keys: ESC (scan 1 or 0x81) via 0x7a64 -> exit path 0x7a12; scan 0x0a ('9') inside the pencil/sketch
loops jumps to 0x78c8 = start of the coloured 3D scene part (NOTES.md says "skip to the end"; it is a skip to the
scene). 0x7a64 is only this ESC test, not a per-frame update.

## 3. Memory map (buffers allocated by init; emulator addresses in brackets are for reference only)

| pointer | size / layout | contents | generator | users |
|---|---|---|---|---|
| [0x1edf9] | 320x200 bytes, 640 bytes guard before and after | back buffer (init fills 0xff) | — | every part; copied to VGA with `rep movsd` 16000 dwords, no vsync |
| [0x1edf1] | 0xa0000 - code32a | VGA | — | |
| [0x1edf5] | 320x200 (+0x200 slack) | paper texture, values 28..31 = shade-table rows | 0x55e2 | 0x1943 (main, sketch 0x6127), 0x42e4, logo 0x571d, peel 0x2cb2 (back of the peeled page) |
| [0x1eddd] | 64K-aligned; +0: 256x256 tex; +0x10000: 256x256 canyon texture | init texture (§5.5); canyon tex made by the teal part | 0x10d3; 0x68a6 | 0xc80, 0x3095 (title, water use +0; teal and planes move the pointer to +0x10000 while they run) |
| [0x1eded] | = [0x1eddd]+0x20000; 65 rows x 256 shade table, then at +0x10000 65 rows x 256 multiply table | §5.6 | 0x144b | 0x1612, 0x1943, 0x3095 (title/water add 0x10000 -> multiply table while they run) |
| [0x1edd1] | 512x256, 512-aligned | light map | 0xc80 | 0xd28 (water bump mapper) |
| [0x1edd9] | 41x26 records x 12 bytes (u, v, l int32) | mapping grid | 0x2db3/0x2dc8/0x2f24 | 0x3095 |
| [0x1edd5] | 0x5a0 = 120 x 12 bytes | oscillators {phase, freq, speed} (72 used) | 0x2756 | 0x26d0, 0x2dc8, title/water callbacks, teal 0x67b9 |
| [0x1edc9] | 320x200, guard row before | water part: warped height field + letters | water part | 0xd28 |
| [0x1edcd] | 0x2f440 bytes | glyph cell cache: title (26 pointers to 80x40 cells), water (26 x 0xd00 = 52x64 cells) | 0x2936, 0x24df | title, water |
| [0x1edc5] | 320x200 | logo/peel picture and mask buffer (+0x40 marks in 0x58b5) | logo part | 0x5623..0x58ea, main 0x7942/0x798c |
| [0x1edfd] | 128K (pointer = alloc + 0x800) | scratch: 0x2dc8 column curves (+0, +0x200), end part 320x200 work image, logo flood-fill pointer list (0x58b5) | | |
| [0x1ede1] | 320x200, guard row before/after | "1996" glow image | 0xfb2 + 0x13af | end part |
| [0x1ede5] | 640 x 41 (+guard row) | "psychic link" box logo | 0xfb2 | end part 0x11c0 |
| [0x1ede9] | 29 x 2048 | font glyphs | 0xe2e | 0xe8a, 0xedc, 0x2743, 0x29cf |
| [0x1ee05], [0x1ee09] | 1024 bytes | pencil darkness sequence (values 26..31); [0x1ee05] is reset per line by 0x18b7 | init | 0x1612, 0x18b7 |
| [0x1ee0d] | 1024 bytes | endpoint jitter (-3..2) | init | 0x18b7 |
| [0x483b], [0x4837] | 0x3000 bytes; [0x4833] = point count | banner strokes, (x, y) int32 pairs, (0,0)-terminated strokes | 0x5979 | scene3d 0x73a6, 0x620b |
| [0x2449d], [0x24499] | 0x540 = 28 x 48 bytes, index | teal-part camera ring buffer | — | 0x64f1 |
| [8] | (PMODE himem pointer) | free memory above the heap used as scratch | — | 0xf47, 0x42e4, 0x1336 |

Fixed tables in the data area: working palette 0x1eaad (768), sine 0x22479 (1024 x i32), depth buckets
0x23479 (1024 x u32), recip 0x1ee19 (2048 x u32), edge table 0x20e15 (200 x 8 bytes), temp 0x21455.. (square
table, curve outputs, scratch dwords — reused by many routines), loading counter 0x24479.

Static data in the exe (address, size, format):
| addr | size | format | use |
|---|---|---|---|
| 0x9a0 | 256 | 64 x u32 random state; 0xaa0/0xaa1 = indices 0x1d/0x3f | 0xaa2 |
| 0x169a3 | 768 | default palette, 6-bit RGB (§6) | 0x15af, 0x158e |
| 0x16ca3 | 2048 | 64x64 4-bit image, high nibble = left pixel | 0x10d3 |
| 0x174a3 | 132 | 11 object table entries (§7.1) | main |
| 0x17527..0x18a2f | | object vertex/face data (§7.1), planes 0x1878b, 0x187d5 | |
| 0x18a2f | 1192 | 106 banner strokes (§5.11) | 0x5979 |
| 0x18ed7 | 2027 | RLE font, 29 glyphs (§5.9) | 0xe2e |
| 0x19693 | 48 | looks like a 16-colour 6-bit palette (white->black ramp, reds); no direct reference found (guess: logo part) | ? |
| 0x196c3 | 0x2840 | 224x92 4-bit image (logo part, unpacked raw by 0x58ea) | logo |
| 0x1bf03 | 0x2450 | 224x83 4-bit image (logo part, unpacked as inverted greys) | logo |
| 0x1e353 | 261 | pencil object (byte format) | main |
| 0x3a89 | 3 | colour table of the byte format: 0x10, 0xb0, 0xeb | 0x39a4 |
| 0x3db8 | 8 | colour table of the word format: 0xf0 0x70 0xb0 0xd0 0x30 0xd5 0x10 0x90 | 0x3cb7 |
| 0x681a | 96 | 32 teal colours for palette 128..159 (teal part) | 0x158e |
| 0x4530.. | | strings (banner, progress bar, exit text, GUS prompt, "psychic link\0sixty four k", "t h e   e n d") | |

## 4. Shared helpers

**0x0aa2 random** (no inputs; returns eax; preserves everything else). State T[64] u32 at 0x9a0, byte indices
i = [0xaa0] (initial 0x1d), j = [0xaa1] (initial 0x3f):
```
u32 rand() { u32 v = T[i] + T[j]; T[j] = v; i = (i+1) & 63; j = (j+1) & 63; return v; }
```
i.e. x[n] = x[n-34] + x[n-64] mod 2^32. Initial T (exe, 0x9a0): 193e107a 11842b4d 33feddf9 2aefaab3 01c73dd5
173ae35f 0ee4dce6 19f2d752 32d6cff9 3a92c636 37190815 3126c502 194051b9 23eabd12 1c863517 060b3866 3ad7701f
031663c4 2f986764 1275ea26 1f383883 2fc33c2f 2a0cb7ab 3a2fdd52 3671780f 04a9b9ac 154b1b74 131e275e 124889ed
191780f2 1a187118 2ebf3c70 2f077525 1c817d37 1a159e2f 1a0bda16 159a568a 074c2f9c 0fe714fe 3aac253a 14a30efb
1d076500 1a0a33ca 3addf9fa 25b8e6b3 19ca5118 33ffbda3 299e98b2 03b8210e 26ebc7d4 1a2be527 252947fd 31156867
0e870578 167218ed 1a2bdd7c 25378e01 17c46e32 33667e2b 1f4ef50a 14640336 17b69b45 0e01904a 3acd63de.

**0x377f** (ebx = mask): `r = rand(); return (r & 0x10000) ? -(r & mask) : (r & mask);` (eax; 0 twice as likely).
**0x3793** (ebx = mask): three calls of 0x377f; returns eax = third, and leaves the first and second on the
stack for the caller to pop (first pop = first value); ebx is clobbered (holds the return address).

**0x0240 getmem**: eax = size -> eax = pointer, CF on failure (never checked). Bump allocator: tries low memory
([0] += size while <= [4]), else high memory ([8] += size while <= [0xc]). No free. [8] (the current top) is
used by 0xf47 and 0x42e4 as free scratch memory.

**Matrices**: 3x3, row-major, 9 x int32 16.16 (m[r][c] at +4*(3r+c)).
- 0x0ae2: edi = A * B with ebx = A, esi = B: `out[r][c] = sum_k (A[r][k]*B[k][c] >> 16)` — each 64-bit product
  floor-shifted separately (shld edx,eax,16). Clobbers eax, edx, ecx, ebp, ebx, esi, edi.
- 0x0b1f: esi = out, ebx = angle a, eax = angle b (s = sin, c = cos from the table, 16.16; products >>16):
  ```
  [ ca        0    -sa      ]
  [ -sa*sb    cb   -ca*sb   ]   = Rx(b) * Ry(a),  Ry(a) = [ca 0 -sa; 0 1 0; sa 0 ca], Rx(b) = [1 0 0; 0 cb -sb; 0 sb cb]
  [ sa*cb     sb    ca*cb   ]
  ```
- 0x0baa: edi = Rx(eax) = [1 0 0; 0 c -s; 0 s c].
- 0x0bed: edi = Ry(eax) = [c 0 s; 0 1 0; -s 0 c]   (note: opposite sign convention to the Ry inside 0xb1f).
- 0x0c30: edi = Rz(eax) = [c -s 0; s c 0; 0 0 1].
Callers: 0xae2: planes 0x3667/0x3847/0x386a, 0x3fb3, teal 0x652b/0x656f/0x6583. 0xb1f: planes 0x3651/0x3833/
0x389f, scene 0x5ec0/0x5f35/0x6261, 0x603f (0x60a4 camera = identity, 0x60ee pencil). 0xbaa/0xbed/0xc30: teal
0x64fb/0x6545/0x6516/0x655b, planes 0x3856.

**Sine table** 0x22479: `sin[k] = fistp(65536 * sin(2*k*c))`, c = float32(pi/1024) = 0.0030679616611450911,
x87 accumulation (`fadd c` twice per step, 64-bit precision), round-to-nearest. Equal to
round(65536*sin(2*pi*k/1024)) except sin[661] = -51912 (ideal -51911) and sin[905] = -43712 (ideal -43713).
Range -65536..65536.
**Reciprocal table** 0x1ee19: [0x1ee15 + 4n] = floor(65536/n), n = 1..2048 (verified). Users: 0x2ad0, 0x1612,
0x1982, 0x1bae, 0x5979, peel 0x2d38.

## 5. Tables and textures generated at start-up

### 5.1 Reciprocal, sine: see §4.
### 5.2 Square table (0x144b): dword[0x21455 + 4k] = (k-256)^2, k = 0..511 — transient: 0x21455.. is
scratch for many later routines (it is overwritten before anyone reads it as a square table; no user found).

### 5.3 Pencil darkness bytes and jitter bytes (init, verified)
```
for i in 0..1023: r = rand() & 31; A[i] = r < 5 ? 30 + (r >> 2) : 26 + (r >> 3);   // 26..31: P(26)=3/32 P(27)=8/32 P(28)=8/32 P(29)=8/32 P(30)=4/32 P(31)=1/32
for i in 0..1023: s = 0; repeat 120: s += (rand() & 15) - 7;  A[0x400+i] = (s >> 6) - 1;  // signed byte, -3..2 (mostly -1/0)
```
A[] values are shade-table rows (§5.6): a pencil pixel becomes shade[A[k]][old], i.e. greys get 2..12 levels
darker, ramp colours 1..6 steps darker.

### 5.4 Paper texture 0x55e2 -> [0x1edf5] (verified)
```
fill 0x3ed1 dwords with rand() & 0x0f0f0f0f      // bytes 0..15 (64324 bytes)
for i in 0..63999 (in place, forward): d = (int8)(P[i] - P[i+321]);  P[i] = 30 + (d >> 3)   // arithmetic shift
```
An emboss (difference to the pixel one down-right) of uniform 4-bit noise. Values 28 (10.9%), 29 (36.2%), 30
(38.8%), 31 (14.1%). They are shade-table rows: the scene shows the paper as shade[k][0x3e] = 54, 56, 58, 60
(default palette greys). Hi-res remake: generate the same noise at the target resolution (one noise sample per
output pixel, emboss offset one pixel diagonally) or upscale this one; it is pure random detail.

### 5.5 Init texture 0x10d3 -> [0x1eddd] (256x256, values 0..224, verified)
```
src = 0x16ca3: 64 rows x 32 bytes, each byte = 2 pixels (high nibble left), 4-bit
for r in 0..63: row = for each byte b: [hi*16, hi*16, lo*16, lo*16]  (128 px, 2x horizontal)
    T[2r] = T[2r+1] = row ++ reverse(row)                        // 256 px, mirrored, 2x vertical
T[128 + y][x] = T[127 - y][255 - x]                              // lower half = upper half rotated 180 deg
repeat 3: (toroidal) T = shift(blur5(T), -2, -2)
```
blur5 = one call of 0xf47 = two passes of `p' = (N + S + W + E) >> 2` (truncating, centre not included). It is
done on a copy laid out 260 bytes wide (row + its first 4 pixels) and 512 rows (texture twice), and read back
from offset (2,2), so it is an exact wrap-around blur whose result is shifted by 2 pixels left and up per call
(6 in total). The image is 4-fold symmetric (kaleidoscope). PNG: tex256.png, tex256_unblurred.png,
tex_src64x64.png. Effective blur: variance 0.5 px^2 per axis per pass at 256 res, 6 passes -> sigma ~1.73 px.
Hi-res remake: upsample the 64x64 source (2x nearest equals the original), mirror, blur with a Gaussian of
sigma = 1.73 * (N/256) px (or apply the 6 cross-kernel passes at 256 and upsample bilinearly), shift (-6,-6)*N/256.
Users: title warp and water height field (0x3095 directly), light map (0xc80), teal/planes canyon texture
(quantised copy, §5.12).

### 5.6 Shade table and multiply table 0x144b -> [0x1eded] (verified)
Default-palette layout (§6): colours 0..63 one grey ramp; 64..255 = six 32-entry ramps (black -> colour at +15 ->
white at +31; 64..95 is a special ramp).
```
for ch in 0..64:
    k = ch <= 32 ? ch : ((ch-32) >> 1) + ((ch-32) >> 3)            // rows 33..64 -> k = 0..20
    for c in 0..255:
        SHADE[ch][c] = c < 64 ? max(0, c + 2k - 64)                // [0x1eded + ch*256 + c]
                              : (c & 0xe0) + max(0, (c & 31) + k - 32)
        MUL[ch][c]   = min(255, (c * ch) >> 5)                     // [0x1eded + 0x10000 + ch*256 + c]
```
Row 32 = identity; rows < 32 darken (towards the ramp's black end), never brighten. Rows 33..64 are the shadow
rows: paper k=28..31 with bit 0x20 set (0x42e4) reads rows 60..63 -> k = 17,17,18,18 (greys -30, ramps -15).
Users: pencil lines rows 26..31; paper rows 28..31 (and 60..63 in shadow); canyon rows 8..32 (fog to black);
title MUL row 10 (tex*10/32), water MUL row 20. Rows 65+ of either table do not exist (memory of other buffers).
The 0x44200 allocation is exactly 64K alignment slack + 0x34100 (tex, canyon tex, shade, 65 multiply rows) + 1,
so it always fits; whatever follows the multiply table belongs to the next buffers ([0x1edd9] grid, paper).

### 5.7 (not a table) Back buffer initial 0xff, VGA cleared.

### 5.8 Light map 0xc80 -> [0x1edd1] (512 x 256, verified; light map of the water bump mapper 0xd28)
```
for j in 0..255: y = j - 128; trow = (256 - j) & 255
  for i in 0..511: x = i - 256
    d = round_even(sqrt(sqrt(x*x + y*y)) * 32.0)                  // x87 fistp
    v = clamp(300 - d, 0, 255)
    a = v > 192 ? v - 128 : (v * 0x5555) >> 16                    // ~v/3 below 192
    a = (a << 1) & 255;  a -= a >> 3                              // * 2 * 7/8 (8-bit)
    L[j][i] = min(255, a + (T[trow][(512 - i) & 255] >> 1))       // + half the init texture, mirrored
```
A bright spot (sqrt-of-distance falloff, steep inside r < ~12, "a" jumps at v = 192) centred at (256,128) over
the texture. The bump mapper indexes it with ((128 + h[y-1] - h[y+1]) >> 1) * 512 + ((128 + h[x-1] - h[x+1]) >> 1)
plus a moving light offset (water scope). PNG: lightmap512x256.png. Hi-res: evaluate the formula directly with
real-valued x, y; the texture term at matching resolution.

### 5.9 Font 0xe2e -> [0x1ede9] (verified)
29 glyphs: 'a'..'z', then '{' = "1", '|' = "9", '}' = "6". Each glyph 2048 bytes: [0] = width (8..49), then 32
rows x 60 bytes of 0x00/0xff, rows from +1. RLE source at 0x18ed7: per glyph one width byte, then per row codes:
`0` = end of row; `0xff skip count` (3 bytes); else `skip = b & 15, count = b >> 4`; skip pixels then set count
pixels to 0xff. Widths: a28 b30 c27 d30 e29 f20 g30 h28 i8 j14 k26 l8 m44 n28 o32 p30 q30 r19 s29 t19 u28 v31 w49
x29 y29 z28 1:19 9:28 6:28. A Helvetica/Arial-like sans, 32 px cell (ascender to descender). PNG: font.png.
Hi-res remake: use a matching vector sans font scaled to the same advance widths, or upscale the bitmaps.

### 5.10 "1996" glow and "psychic link" logo 0xfb2 (verified)
```
G = [0x1ede1] (320x200, zero guard rows above and below)
x = 2, y = 9: for ch in "{||}" ("1996"): draw 0xedc(ch) at (x,y); x += 3*w + 2
   // 0xedc: glyph rows 0..24 only, each glyph pixel -> 3x7 block (3 wide, 7 tall), 175 px tall
[0x1edad] = 150; repeat 15: 0x13af(5); progress()      // 75 calls of 0xf47 at init
P = [0x1ede5] (640 wide, 41 rows + guard row): 0x11b0("psychic link") at (187, 6) with 0xe8a (stride 640)
NOT rect (x 176, y 1, w 288, h 38); NOT rect (x 180, y 4, w 196, h 32)   // 0x109a: white frame, "link" inverted
0xf47(P, 40 rows, stride 640)                                              // slight blur
```
0x13af(ebx): `[0x1edad] -= ebx; if negative return; else ebx times 0xf47(G, 200 rows, 320)` (the subtraction is
done even when it fails). After init the budget is 75. The pencil part calls 0x13af(1) once per drawn frame
(0x7779, frame-rate dependent) and the logo part's tick-wait loop calls 0x13af(1) continuously (0x57fc), which
always finishes the budget. Result: the "1996" image has exactly 150 calls of 0xf47 (= 300 four-neighbour
passes) by the end part, on any machine. The end part (0x13d6) then calls 0x13bb (no budget) ebx times with
ebx = ticks/8, i.e. one more 0xf47 every 8 ticks (tick-based). Both images are only used by the end part
("1996" + "psychic link", 0x11c0/0x129b). PNG: logo_glow_75.png (after init), psychic_link_text.png.
Hi-res: the 300 passes approximate a Gaussian with variance 300 * 0.5 = 150 px^2 per axis at 320x200
(sigma ~12.2 px), with zero (black) borders; the end part adds variance 1 px^2 (2 passes) per 8 ticks.

### 5.11 Banner strokes 0x5979 -> [0x483b] (verified; the "Statix + Vic (acme)" drawing with two stick figures)
Source 0x18a2f..0x18ed7, 106 strokes; output (x,y) int32 pairs, each stroke followed by (0,0), plus a final
(0,0); [0x4833] = total pairs written including every terminator and the final one (675). Per stroke:
```
type = byte0; n = byte1 >> 2; if n == 0: n = 1
X(w) = (w * 0x2d000 >> 16) + 300      // = w * 2.8125 + 300   (w unsigned 16-bit)
Y(w) = (w * 0x23000 >> 16) + 100      // = w * 2.1875 + 100
type 1 (line, 58x):  words x0 y0 x2 y2; x1 = (x0+x2)>>1, y1 = (y0+y2)>>1; then as type 0
type 2 (circle, 10x): words cx cy r; n = max(n, 3); step = (65536/n) >> 6;
       for a = 0, step, 2*step, ...: emit; stop after emitting the first a >= 1024:
       px = (sin[a&1023]*r + (cx<<16)) >> 16, py = (cos[a]*r + (cy<<16)) >> 16; emit (X(px), Y(py))
other (here 3, 38x, quadratic): words x0 y0 x1 y1 x2 y2;
       xs = 0x2ad0(X(x0), X(x1), X(x2), n); ys = 0x2ad0(Y(y0), Y(y1), Y(y2), n); emit n+1 points
```
The coordinates are banner parameters: x in 0..2048 along the banner, y in 0..950 down the banner (ranges used:
x 322..1800, y 135..948); scene3d maps them through 0x3b69 onto the banner mesh. PNG: strokes_small.png.
Hi-res: keep the control points and evaluate the curves densely.

### 5.12 Canyon texture (made at the start of the teal part, 0x68a6, verified at t7000/t9500)
`C[i] = 0x8b + ((T[i] >> 5) & 7) + (rand() & 2)` for all 65536 texels of the init texture T, written to
[0x1eddd]+0x10000 (values 139..148 = colour ramp 128..159 positions 11..20). The teal part shows it with the
teal colours from 0x681a at 128..159; the plane-flock part (0x388b) reuses the same texture with the default
palette, whose 128..159 ramp is blue -> "blue canyon". PNG: canyon_tex_t7000.png.

### 5.13 Default palette (static 0x169a3; PNG pal_default.png)
0..63 grey (0,0,0),(0,0,0),(1,1,1)..(63,63,63) (entry i ~ grey i-1); 64..74 grey 63 down to 5 in steps of ~6;
75..79 (12,10,10) (20,11,11) (28,10,10) (36,6,6) (44,0,0); 80..95 black; then 32-entry ramps (black -> full at
+15 -> white at +31): 96 green (12,63,33), 128 blue (11,43,63), 160 yellow (63,63,5), 192 brown (50,31,19)
(then towards a light brown-white), 224 red (63,11,11). Paper and pencil use the grey ramp; objects use
mid-ramp base colours (+12) shifted by -13..+12 for light.

## 6. Palette system

Working palette: 768 bytes 6-bit at 0x1eaad. DAC = f(working palette, level [0x1edb5]).
**0x15c0 upload** (also the tail of 0x14dc/0x1579/0x158e/0x15af): out 0x3c8 = 0, then 768 bytes:
```
L = level >> 8
if L <= 128: out = (p * L) >> 7                                   // 0 = black, 0x8000 = unchanged
else: b = min(L, 255) - 128; out = ((((p * (127 - b)) >> 6) & 255) + b) >> 1   // towards white; 0xff00 = all 63
```
**IRQ0 0x1e45** (70.0018 Hz): (1) if [0x1ea93]: music accumulator [0x1e5fb] -= 0x4295, while <= 0 call 0x1ed6
and add [0x1e5ff] (music scope); (2) fade: if level != target: level += step towards target, clamped at the
target (signed compares); store; upload 0x15c0 — so the DAC is only re-uploaded on ticks where the level moves,
or when a routine calls an upload itself; (3) if [0x1e5ef] != 0 call it (per-tick effect callback); EOI.
Variables: [0x1edb5] level, [0x1edbd] target, [0x1edb9] step per tick.
**0x15af**: working = default palette (0x169a3), upload. **0x158e** (esi): default, then 32 colours from esi to
entries 128..159, upload. **0x1579** (no callers): grey ramp entry i = i>>2. **0x14dc** palette generator:
```
A = [0x14dd] (imm32 of "mov ebx, imm" at 0x14dc); S = [0x1505] (imm8 of "mov cl, imm")
s = sin[A & 1023] >> S;  c = sin[(A+256) & 1023] >> S          // arithmetic shifts
R = (128 - s) & 255; G = B = (128 + (s >> 1)) & 255; d = (c * 0xde) >> 8; G += d; B -= d   (8-bit)
for ch in 0..127:  pal[ch] = (R*ch >> 9, G*ch >> 9, B*ch >> 9)                     // black -> colour/4
for ch in 128..1:  w = (128 - ch) >> 1; pal[256 - ch] = ((R*ch >> 9) + w, ...)    // colour -> white (entry 255 = 63)
upload (0x15c0)
```
(R,G,B) = 128 + amp * (-sin A, sin A/2 + 0.867 cos A, sin A/2 - 0.867 cos A), amp = 65536 >> S (S=0xb: 32,
S=0xc: 16): a hue circle around grey. Verified against every snapshot's working palette.

Per part (palette / fade, all fade steps per tick):
| part | palette | fade |
|---|---|---|
| init/setup | default (DAC black, level 0) | target 0x8000 step 0x96, then 0x190 |
| title 0x2829 (and "the end") | 0x14dc, S = 0xb (set 0x2856, never reset), angle [0x14dd] = 0 at first start; callback 0x27ae rebuilds with angle+1 every tick (full hue turn per 1024 ticks = 14.6 s; the title runs angle 0..~975, colour at entry 127: angle 0 (32,38,25) green-grey, 256 teal, 512 blue-purple, 768 red-grey); "the end" continues from the angle the water part left (2910; 3040 at tick 11100) | in: step 0x96 (219 ticks); at counter end: target 0xff00 step 0x3e8 (white, ~33 ticks), loop ends when level == 0xff00 |
| pencil/sketch/scene/peel/logo | default (0x15af at 0x7734) | from white: target 0x8000 step 0x190 (82 ticks) |
| teal plane 0x687a | 0x158e(0x681a): teal ramp at 128..159 | end: target 0 step 0x12c |
| water 0x24df | 0x14dc angle 0x944, S = 0xb; callback 0x26aa rebuilds with angle+1 per tick while [0x1e4e6] > 3 (music state; water/music scope); sets S back to 0xc at exit | in: 0x8000 step 0x190; out: 0 step 0x12c |
| planes 0x388b | default | in 0x8000 (step 0x12c from water), out 0 step 0x12c (and the skip bug, NOTES.md) |
| "the end" | as title | in 0x8000 step 0x190 (0x79ea) |
| end logo 0x13d6 | 0x14dc angle 0x2d0, S = 0xc, fixed | 0x8000 step 0x200 (from white, 64 ticks) |

Hi-res/true-colour remake: compute colours as palette[index] scaled by the level formula; the palette ramps are
32-step quantisations of linear black->colour->white ramps, the shade tables are index shifts along them.

## 7. 3D objects

### 7.1 Object list (static data)
Word format (0x3cb7, table at 0x174a3, 12 bytes per entry): `u8 nv, u8 nf, i16 ox, i16 oy, i16 oz, u32 ptr`;
at ptr: nv x (i16 x, i16 y, i16 z) then nf x (u8 a, u8 b, u8 c, u8 flags). Vertex = (x+ox, y+oy, z+oz).
Face colour = table0x3db8[flags >> 4] - 4, i.e. 0xec red, 0x6c green, 0xac yellow, 0xcc brown, 0x2c grey, 0xd1
light brown, 0x0c dark grey, 0x8c blue; face +0x1c = 0x20; edge flags (flags & 7) << 3.
Byte format (0x39a4, esi = data, al = colour offset): `u8 nv, u8 nf`, nv x (i8 x, i8 y, i8 z) scaled x8, nf x
(a, b, c, flags); colour = table0x3a89[flags >> 4] + al (0x10, 0xb0, 0xeb; only flags>>4 in 0..2 valid); +0x1c = 0.
Coordinates: y points down (screen convention), the ground is around y = +800..+875.

| slot | entry | data | nv/nf | offset | object | colours |
|---|---|---|---|---|---|---|
| 0 | 0x174a3 | 0x17de3 | 20/36 | (23875,-10830,11604) | sun (icosahedron, r ~600) | yellow |
| 1 | 0x174af | 0x186cb | 20/18 | (668,814,492) | winding path (flat, y = 0) | light brown 0xd1 |
| 2 | 0x174bb | 0x18551 | 17/24 | (1457,-762,3288) | house | red roof, grey walls |
| 3 | 0x174c7 | 0x18617 | 14/24 | (1912,277,-3) | banner pole (hexagonal, 1196 tall) | red |
| 4 | 0x174d3 | 0x18617 | 14/24 | (4115,277,-13) | banner pole | red |
| 5,6,7 | 0x174df/eb/f7 | 0x17eeb | 121/228 | (5258,-593,3081), (4880,-593,-2720), (-2952,-593,2651) | fir trees | green, brown trunk |
| (8) | 0x17503 | 0x18617 | 14/24 | (3013,277,-8) | third pole — created, then its slot is overwritten by the banner (never drawn) | red |
| 9 | 0x1750f | 0x17527 | 23/10 | (1232,-202,2501) | house windows (4) and door | blue, dark grey |
| 10 | 0x1751b | 0x175dd | 177/248 | (-663,509,-724) | flower (5 petals, centre, stem, 2 leaves) | red, yellow, green |
| 8 | — | generated | 187/320 | — | banner (0x3a8c) | 0x30 grey |
| 11 | — | 0x1e353 | 25/46 | z -1016..0, tip at z = 0 | pencil (hexagonal, red body, yellow wood, dark lead) | 0xeb, 0xb0, 0x10 |
| — | — | 0x1878b | 8/12 | x +-200, y -80..0, z -800..0 | simple paper plane (flock, al = 0x1e) | 0x2e |
| — | — | 0x187d5 | 56/108 | same extents | finer paper plane (teal part, al = 0x20) | 0x30 |
(4 unknown bytes 0x175d9..0x175dc: 20 21 22 35.) OBJ files and renders: obj*.obj, pencil.obj, plane_*.obj,
objs_sheet.png.

### 7.2 Runtime records (allocated by getmem: 0x7c + nv*0x24 + nf*0x4c, zeroed)
Object header: +0x00 O (3x3 16.16, init identity) | +0x24 C = Cam*O | +0x48 pos (int x,y,z) | +0x54 pos in
camera space (16.16) | +0x60 light vector in object space (int, length 200) | +0x6c nv | +0x70 nf | +0x74 faces
ptr | +0x78 vertices ptr (= +0x7c).
Vertex (0x24): +0 x, +4 y, +8 z (int, object space) | +0x0c/+0x10/+0x14 camera x,y,z (16.16) | +0x18/+0x1c screen
x,y in 1/32 pixel | +0x20/+0x22 shadow screen x,y (int16, 0x42e4).
Face (0x4c): +0/+4/+8 vertex pointers | +0x0c/+0x10/+0x14 neighbour face across edge v0v1 / v1v2 / v2v0 (self if
none) | +0x18 next face in depth bucket | +0x1c sketch-mode colour offset (0x20 or 0) | +0x1d flags: bits 0..2 =
silhouette (neighbour hidden) per edge, bits 3..5 = crease or data-forced edge, bit 7 = hidden | +0x1e base
colour | +0x1f final colour | +0x20 sketch reveal mask (scene/sketch) | +0x24/+0x28/+0x2c normal * 256.

### 7.3 Normals and edges 0x3dc0 / 0x3eeb / 0x3f37 (ebp = object)
```
for each face: e1 = V1 - V0; e2 = V1 - V2; n = e1 x e2 (int32 products)
    len = max(1, round(sqrt(|n.n|)))  (x87);  normal = (n * 256) / len  (each component, truncating idiv)
0x3eeb: normals, then for each face and edge k (v0v1, v1v2, v2v0): find another face that contains the same two
    vertices in the opposite order (linear search); store it (or self) at +0x0c+4k;
    if found and dot(normal, other.normal) <= 10000 (angle >= ~81 deg): flags |= 8 << k
```
### 7.4 Banner 0x3a8c / 0x3c3e / 0x3b69
0x3a8c: object with nv = 187 (17 columns x 11 rows), nf = 320; faces per quad (i, i+1, i+18) and (i+18, i+17, i)
(vertex i = row*17 + col), colour 0x30, +0x1c = 0; +0x1d gets 0x10 / 0x08 on the triangle edges that lie on the
grid border (edges drawn as outline). Stores the object in [0x5dd8], then 0x3c3e, then 0x3eeb.
0x3c3e (called once at set-up and every frame by the scene parts): `sag = 0x2ad0(0, 90, 0, n=17)` (18 values,
first 17 used); for row r (0..10), col c (0..16): x = 128*c, z = 95*r; vertex = F(x, z) + (0, sag[c], 0); then
normals (0x3dc0). Order of vertices: row r = 0 first (ch counts down from 11, z = (11-ch)*95), col 0 first.
0x3b69 F(x, z) with W = bytes at 0x69b7 (3 x 256 + 256 more, filled per frame by scene3d 0x726d, all zero at
set-up), i = (3x) >> 5, j = (z >> 4) + 0x80:
```
X = 0x7bc + x + ((W[i]-64)*z >> 11)       + ((W[j]-64)*z >> 11)
Y = -0x123 + z + ((W[i+256]-64)*z >> 11)  + ((W[j+256]-64)*z >> 11)
Z = (x >> 9) - 3 + ((W[i+512]-80)*z >> 8) + ((W[j+512]-80)*z >> 8)
```
With W = 0 the banner hangs between the poles (x 1980..4028), top at y = -291, leaning towards -z as it goes down,
sagging 90 units in the middle. Strokes (§5.11) live in the same (x, z) parameter space.

### 7.5 0x3fb3 transform / light / cull / bucket (ebp = object)
Globals: Cam = 3x3 16.16 at 0x1e468 (rows = camera axes); campos int at 0x1ea9d/0x1eaa1/0x1eaa5; light position
int at 0x1e48c/0x1e490/0x1e494; mode byte [0x1ea94]; bucket heads 0x23479 (1024 dwords); bucket shift = imm8 at
0x42ac (6; the teal part patches it to 3).
```
C = Cam * O (0xae2); poscam = Cam * (pos - campos)        // 16.16, no shift
Lo = (O^T * (light - pos)) >> 16; Lo = round(Lo * 200 / |Lo|)   (x87)
for each vertex: cam = C * v + poscam (16.16); +0x14 = cam.z
    if cam.z > 400.0 (0x1900000): inv = 0xe00000 / (cam.z >> 16)       // integer division
        sx32 = ((cam.x * inv) >> 27) + 0x1400; sy32 = ((cam.y * inv) >> 27) + 0xc80
        // = 32 * (160 + 224 * x / z), 32 * (100 + 224 * y / z): focal length 224 px, centre (160,100)
for each face: hidden if any vertex z <= 400.0 or (x1-x0)*(y2-y0) - (y1-y0)*(x2-x0) <= 0 (screen 1/32 px)
    -> flags |= 0x80
    else: flags &= 0x7f; light = (normal . Lo) >> 12   (-13..12)
          a = base + light (8-bit); if mode == 2: a2 = a + [+0x1c] and if (a2 >> 5) != (a >> 5) -> 0x3a else a2
          +0x1f = a                       // modes 0/1: base+light with no clamping (can cross into the next ramp)
for each visible face: flags = (flags & 0xf8) | hidden(neighbour k) << k        // silhouette bits
    b = clamp(((z0 + z1 + z2 (ints)) - 1200) >> shift, 0, 1023); push face on bucket b (LIFO)
```
Callers of 0x3fb3: planes 0x36d3 (in 0x3618), sketch/scene 0x5e09/0x5e28 (0x5dfc/0x5e1b), pencil 0x60f9, teal 0x692d.
In mode 2 (sketch) every 0x3cb7 object face (+0x1c = 0x20) gets colour 0x3a (paper-like grey 58) — the drawing
is shown as white paper with pencil outlines; the banner, pencil and planes keep their colours. Mode 1 = pencil
pass (no outlines), mode 0 = coloured scene with outlines.

### 7.6 0x43df draw buckets (all objects transformed since the last call)
```
for b = 1023 down to 0: for face in bucket b (most recently pushed first; bucket cleared):
    p_k = (sx32 >> 5, sy32 >> 5) of v0, v1, v2 (stored at 0x3f93)
    0x1a87 + 0x1b10 flat fill with +0x1f into the back buffer
    if mode != 1:
        e = flags; if mode == 2: e &= +0x20 (reveal mask) and if face == [0x5047] draw 0x18b7 from
            ([0x55d6],[0x55da]) to ([0x5053],[0x5057]) (the edge being drawn by the pencil right now)
        if e & 0x09: 0x18b7(p0, p1);  if e & 0x12: 0x18b7(p1, p2);  if e & 0x24: 0x18b7(p2, p0)
    [0x1ea99]++ (faces drawn)
```
Crease edges are drawn by both adjacent faces; 0x18b7/0x1612 normalise direction and the jitter/darkness depend
only on the endpoints, so the same pixels are darkened twice: creases look darker than silhouettes.
Users: planes 0x3779, scene 0x5e13, pencil 0x6115, teal 0x6939.

### 7.7 0x42e4 pencil shadow (used by the pencil render 0x603f; camera assumed identity at (0,0,cz))
```
S = [8] (free memory) = copy of the paper (64000 bytes)
inv = 0xe00000 / (-cz)
for each vertex: w = world point = cam/65536 + campos (identity camera assumed)
    t = (-lz << 16) / (wz - lz)               // ray from the light through the vertex hits z = 0
    shadow.x = ((((wx - lx) * t + (lx << 16)) * inv) >> 32) + 160   (high dword of the 64-bit product)
    shadow.y = ((((wy - ly) * t + (ly << 16)) * inv) >> 32) + 100
for each face (no culling): triangle at the shadow points -> 0x1a87 + 0x1b62 (OR 0x20) into S
```
Then 0x603f calls 0x1943(ebp = S): paper rows 28..31 lit, 60..63 in shadow (§5.6).

### 7.8 List helpers (sketch/scene3d) 0x5dfc: esi = list of object pointers, cl = count: 0x3fb3 each, then
0x43df. 0x5e1b: same transform, then walks the buckets far-to-near without drawing and stores face pointers into
the sketch list at 0x5073 (0x113 entries x 5 bytes) where the entry equals the running face index (sketch order).

## 8. Rendering primitives (signature; algorithm; callers)

### 8.1 0x1612 pencil line — eax x0, ebx y0, ecx x1, edx y1 (int pixels); dest [0x1edf9]; darkness bytes from
esi = [0x1ee05] (2 per step, not written back)
```
if |dy| >= |dx| (y-major): order so y0 < y1; slope = dx * recip(dy) (16.16); x = x0 << 16
    for y in y0 .. y1-1 (clipped 0..199; leading rows/columns outside 0..319 skipped, stops when x leaves):
        X = x >> 16; B[y][X] = SHADE[d[2k]][B[y][X]]; B[y][X+1] = SHADE[d[2k+1]][B[y][X+1]]; x += slope
else (x-major): order so x0 < x1; y in 16.16; pixels (x, Y) and (x, Y+1) the same way, x0 .. x1-1
```
Last pixel (y1 resp. x1) not drawn; 2 px thick; multiplicative-like darkening (rows 26..31: greys -12..-2).
Called only by 0x18b7.

### 8.2 0x18b7 sketchy line — eax x0, ebx y0, ecx x1, edx y1 (int pixels)
```
J0 = jitter + ((x0*y0 + y0) & 0x1ff); J1 = jitter + ((x1*y1 + y1) & 0x1ff)     // jitter = [0x1ee0d]
[0x1ee05] = [0x1ee09] + ((x0 + y0 + x1 + y1) & 0xff)                          // darkness start, same for all passes
for pass in 0..2:
    0x1612(x0 + J0[2p+1], y0 + J0[2p], x1 + J1[2p+1], y1 + J1[2p])            // signed bytes -3..2
```
Deterministic (no flicker): the jitter depends only on the endpoint, so lines sharing a vertex stay joined.
Callers: 0x43df (edges), scene 0x5fb9 (horizon), 0x7408 (banner strokes). Hi-res: three slightly offset 2 px
pencil strokes, offsets in -3..2 px (scale with resolution), darkness random 2..12 grey levels along the line.

### 8.3 0x1943 shade-map — ebp = map (row per pixel), dest = [0x1edf9]; 0x1949 = same with esi = dest:
`dest[i] = SHADE[map[i]][dest[i]]` for 64000 bytes. Callers: main 0x76b0 (paper), 0x6110 (paper+shadow), 0x6127
(paper), logo 0x5729 (0x1949, dest [0x1edc5]).

### 8.4 Flat polygons — 0x1982 edge (eax x0, ebx y0, ecx x1, edx y1, int pixels), 0x1a87 triangle /
0x1ac3 quad (esi -> 3 / 4 (x,y) int32 pairs; set ymin = 255, ymax = 0 in bytes 0x21455/0x21456), 0x1b10 fill
(al = colour, edi = buffer base, stride 320), 0x1b62 = same but `|=` al.
```
edge: skip if y0 == y1; slot = y0 < y1 ? +0 : +4 (and swap so y0 < y1); x = x0 << 16; slope = dx * recip(dy)
      rows y0 .. y1-1 clipped to 0..199; x clamped into [0, 320<<16]; ET[y].slot = x; ymin/ymax updated
fill: for y in ymin .. ymax-1: xl = ET[y].+4 >> 16, xr = ET[y].+0 >> 16; if xr > xl: fill [xl, xr)
```
Edges going down the screen are the right side, up the left: polygons wound the other way fill nothing
(implicit back-face cull). Callers: 0x1a87 by 0x43df/0x42e4; 0x1b10 by 0x43df; 0x1b62 by 0x42e4.
No gouraud or textured polygon filler is used by any shared code I traced (objects are flat per face); all
texture mapping goes through 0x3095 (full-screen grid mapper).

### 8.5 0x1bae vertical gradient — esi y0, ebp y1, ebx colour at y0, edx colour at y1: rows y0..y1-1 (clipped
0..199) of [0x1edf9] filled with c(y) = c0 + (c1-c0)*(y-y0)/(y1-y0) (16.16 DDA, truncated). Scene 0x5f92 (sky
0x80 -> 0x9f from horizon-350 to the horizon), 0x5faa (ground 0x6d, horizon .. +1400), then a 0x18b7 horizon line.

### 8.6 0x2ad0 quadratic — eax P0, ebx P1, edx P2, ecx n, edi out: n+1 int32
```
step = recip(n) >> 7;  B = 4*P1 - P2 - 3*P0;  A = P0 - 2*P1 + P2
for k in 0..n: t = k*step; out[k] = ((((B*t + ((A*t*t) >> 8)) >> 8) + 2*P0) >> 1   // int32, arithmetic shifts
```
= P0 + B*s/2 + A*s^2/2 with s = t/256: passes through P0 (s=0), P1 (s=1), P2 (s=2) — an interpolating parabola,
not a Bezier. Because step is truncated, s_max = n*step/256 < 2 (n=17: 1.99, 25: 1.95, 40: 1.875), so the last
sample does not reach P2. Callers: banner sag 0x3c51, strokes 0x59f6/0x5a35/0x5b3c, warp grid 0x2e58/0x2e6e/
0x2eb6/0x2ee0, peel 0x2bc1/0x2c06.

### 8.7 0x3095 8x8-block texture mapper — edi = destination 320x200
Grid [0x1edd9]: 41 x 26 records {u, v, l} (int32, only the low 16 bits used, all 8.8): u = texture row, v =
texture column, l = shade row. Texture [0x1eddd] 256x256 (wraps), table [0x1eded].
```
for by in 0..24, bx in 0..39: A = g[by][bx], B = g[by][bx+1], C = g[by+1][bx], D = g[by+1][bx+1]
  for each q in {u, v, l}: dx0 = (B.q - A.q) >> 3; ddx = ((D.q - C.q) - (B.q - A.q)) >> 6; dy = (C.q - A.q) >> 3
  for r in 0..7: q_r = A.q + r*dy; dq = dx0 + r*ddx
    for c in 0..7: q = q_r + c*dq   (8.8, mod 2^16)
      out[8by + r][8bx + c] = TABLE[l >> 8][ TEX[u >> 8][v >> 8] ]
```
(The packed-register implementation is exact 8.8 arithmetic per component as long as u does not wrap more than
128 times inside a block row.) Bilinear interpolation of (u, v, l) inside each 8x8 block; grid points are exact.
Callers: title 0x2877, water 0x25e6 (into [0x1edc9]), planes 0x3940, teal 0x6922/0x69ab.
Hi-res: evaluate the grid functions (§8.9, §8.10) per pixel instead (for the canyon this also restores correct
perspective, which the original only has at the grid points), sample the texture with filtering, and apply the
shade row as a continuous brightness (TABLE row l>>8 = ramp shift (l>>8) - 32 for SHADE, factor (l>>8)/32 for MUL).

### 8.8 Oscillators 0x2756 (ebx = speed scale) / 0x26d0
```
0x2756: for k in 0..71: osc[k].phase = rand(); osc[k].freq = (rand() & 0x3ff) + 0x100;
        osc[k].speed = (((rand() & 0xff) - 0x80) * ebx) >> 8
0x26d0 (per tick, also inlined in the title callback at 0x2812): osc[k].phase += osc[k].speed
```
Scales: title 0x12c, water 0xc8, teal 0x12c. Time base: per tick (callbacks), so the warp speed is frame-rate
independent; the grid is rebuilt per frame from the current phases.

### 8.9 Warp grid 0x2dc8 (title, water) and 0x2db3 (eax -> l of all 1066 records)
```
o = 0
for row R in 0..2:                    // 3 x 3 control points, 8 oscillators each
  for col K in 0..2:
    U = V = 0
    for cl in 4,3,2,1: U += ((sin[(o.phase >> (7-cl)) & 1023] * o.freq) >> 16) >> cl; o++
                       V += (same with the next oscillator) ; o++
    cp[R][K] = (U, V)
  Urow[R] = 0x2ad0(cp[R][0].U, cp[R][1].U, cp[R][2].U, 40); Vrow[R] likewise   // 41 values
for column j in 0..40:
  Ucol = 0x2ad0(Urow[0][j], Urow[1][j], Urow[2][j], 25); Vcol likewise        // 26 values
  for i in 0..25: grid[i][j].u = Vcol[i] << 5;  grid[i][j].v = Ucol[i] << 7
```
(oscillator angle = (phase >> (7-cl)) & 1023, amplitude freq >> cl: four octaves). The title uses l = 0xa00 with
the MUL table (texel * 10/32), water l = 0x1400 (texel * 20/32, as the bump height field).

### 8.10 Canyon grid 0x2f24 (teal 0x6907, planes 0x3930); 0x679b converts the 16.16 matrix at esi to floats at 0x23a8
Floats: M rows R0 (0x23a8), R1 (0x23b4), R2 (0x23c0) = camera right/down/forward in world space; camera position
P = (0x23cc, 0x23d0, 0x23d4); plane distance D = [0x23e4] (1000; the plane-flock part sets 2000.0); K = -12.51
(float at 0x23dc, used twice), constants -20 (0x23d8), 28 (0x23e0).
```
for row i in 0..25, col j in 0..40:
  d = (-20 + j) * R0 + (K + i) * R1 + 28 * R2          // = camera ray through pixel (8j, 8i); focal 224 px
  t = (D - P.z') / d.z'   where .z' = component 2 (world axis 2); if t < 0: t = (-D - P.z') / d.z'
  l = clamp(8 * (1024 - round(t)), 2048, 8192)          // shade rows 8..32: linear fog to black
  v = round(K * (P.0 + t*d.0));  u = round(K * (P.1 + t*d.1))   // 8.8 texture coords, row u, column v
```
Two parallel planes at +-D along world axis 2 (the canyon walls); texture repeat 256*256/12.51 = 5239 units.

### 8.11 Blur 0xf47 / 0xf90
0xf90: `dst[i] = (src[i-stride] + src[i+stride] + src[i-1] + src[i+1]) >> 2` for i in 0..n-1 (linear memory: the
left neighbour of x = 0 is the last pixel of the row above). 0xf47 (esi buf, ecx rows, ebp stride): pass 1 into
scratch at [8]+640 (reads real memory one row outside the buffer), pass 2 back into the buffer reading the
scratch with 640 zero bytes above and below (stride must be <= 640). Callers: init (0x1094, 0x1182), 0x13cc
("1996" glow), water 0x2561/0x2567 (letters), title 0x2987 (letter cells), logo 0x568b; 0xf90 alone: end part
0x133b (one pass from [0x1edfd] to [8], then copied to VGA).

### 8.12 Text: 0xe8a draw glyph al at edi: lower-case only ('a'..'}'); `dst += glyph` saturating at 255 over 32
rows x width; returns edi += width + 1; codes below 'a' advance 10 px (code 1 additionally adds 0x303e). Row
stride = imm32 at 0xeb8 (self-modified: 0x280 init, 0x140 logo part 0x562d, 0x50 title 0x2937, 0x34 water 0x24f0).
0x11b0: string loop over 0xe8a. 0xedc: glyph scaled 3x7 (rows 0..24) at stride 320, returns edi += 3w + 2.

### 8.13 0x58ea 4-bit image unpack — esi src, edi dst (stride 320), ch rows, bl mode: 224 pixels per row
(112 bytes, high nibble first); bl = 0: pixel = nibble; bl != 0: pixel = ((15 - nibble) << 2) + 3 (grey 3..63).
Logo part 0x5718 (0x1bf03, 83 rows, mode 1), 0x5760 (0x196c3, 92 rows, mode 0).

### 8.14 Page peel (shared by the peel part 0x5623.. and the teal part): 0x2b55 (esi = old picture, edi = new
picture) resets the progress [0x1eaa9] = 0; callback 0x2b20 per tick: += 4 (+7 once above 0x100) up to 0x2bc;
0x2b6c per frame writes straight to VGA: 25 bands from two 0x2ad0 parabolas of the sine of the progress, each
band a horizontally scaled (about x = 160) copy of rows of the old picture (or of the paper texture for the
page's back side, 0x2cb2), and the new picture below the curl. Details: logo/teal scopes.

### 8.15 Misc: 0x5b6b clears VGA (64000 bytes 0). Frame copy (every part, inline): back buffer -> VGA, 16000
dwords, no retrace wait. 0x7a64: ESC check only.

## 9. Self-modified code (a remake must treat these as variables)
0x14dd (imm32, 0x14dc hue angle), 0x1505 (imm8, 0x14dc shift), 0xeb8 (imm32, 0xe8a stride), 0x42ac (imm8,
0x3fb3 bucket shift: 6, teal 3).

## 10. Time bases (scope init)
- Fade level: per tick, +-[0x1edb9] per tick (IRQ). Hue angle [0x14dd]: +1 per tick (title, "the end";
  water when [0x1e4e6] > 3). Oscillator phases: per tick. Page peel progress: per tick.
- Blur budget [0x1edad]: per frame in the pencil part (measured 38 fps there = ~2.7M instructions/frame at the
  default budget, each blur ~1.4M), finished by the logo part's wait loop -> the final image is
  machine-independent (150 calls). End part: +1 blur per 8 ticks.
- Everything else in this scope is static or recomputed per frame from per-tick state (grids, palettes).

## 11. Shortcuts a hi-res remake should evaluate exactly
- 0x3095: bilinear (affine) interpolation over 8x8 blocks of a 41x26 grid -> evaluate warp (§8.9) and canyon ray
  intersection (§8.10) per pixel; 8-bit 256x256 texture -> filtered / higher-res texture (§5.5, §5.12).
- Shade tables (32-step ramps, index shifts) and MUL table -> continuous brightness; palette fade formula on
  RGB.
- Sine table (1024 steps, 16.16), reciprocal table (integer 65536/n) -> float math.
- Projection: integer inv = 0xe00000/z and 1/32-px vertices, integer pixel vertices for filling -> float.
- 0x2ad0 parabola with truncated step (does not reach P2) -> decide whether to keep the slight shortfall.
- Light per face: (n.L) >> 12 -> 26 levels; sqrt via x87 fine.
- Blurs: repeated 4-neighbour box blurs -> Gaussians (variances given in §5.5, §5.10).
- Lines: 2-px pencil lines with per-pixel random darkness -> resolution-scaled strokes.

## 12. Not determined / guesses
- 48 bytes at 0x19693 (16 colours?) — no reference found; guess: logo part data.
- The 4 bytes at 0x175d9 between the window and flower data — unused as far as found.
- Shade-table rows 33..64 (k = 0..20) are only reached as shadow rows (60..63) in what I traced; other rows of
  that range may be unused.
- Who reads face +0x30..+0x4b: nothing found (record is larger than needed).
- The square table at 0x21455 (0x144b) has no reader I could find (overwritten as scratch).

## Scratch files (paper/re/models/init/)
PNGs: tex256.png, tex256_unblurred.png, tex_src64x64.png, lightmap512x256.png, paper_stretched.png,
paper_as_shown.png, font.png, logo_glow_75.png, psychic_link_text.png, strokes_small.png, canyon_tex_t7000.png,
pal_default.png, shade_table_defpal.png, mul_table.png, objs_sheet.png (+ obj*_xy/xz/zy.png), *.obj.
Raw 64 MB memory dumps (linear 0 = code32 0): r_after_init.mem (at 0x763e, tick 100), r_after_setup.mem (at
0x76b5, tick 104), t120.mem; m.py loads them with numpy.memmap.
Scripts: gen.py (random/paper/sine/recip), gen2.py (texture/shade/lightmap), fontchk.py (font, 1996 glow,
psychic link), strokes.py, objs.py/objchk.py, palchk.py, canyon.py, cg.py (call graph by part), xi.py (xref ->
instruction), run_init.py (copy of run_paper.py dumping memory at 0x763e/0x76b5/0x76dd and counting rand calls).
