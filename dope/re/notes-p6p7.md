# Scope "p6p7": the cat (shadow, material morph, metal) and the tentacle object with moving lights, "dope"

Addresses are `SEG:OFF` in `unpacked/p6.bin` or `unpacked/p7.bin` (file offset = SEG*16 + OFF; code and variables in
segment 0010, written `[xxxx]` for `cs:[xxxx]`). At run time both parts are loaded at segment 0F64 (CS = 0F74), so a
relocated segment value in memory is the image segment + 0F64. "Retrace" = one call of the part's per-retrace routine
(p6 1cc0, p7 1f40; 70.086 Hz). Angles are 16-bit words, 65536 = one turn. Colour values are 6-bit VGA (0..63).
"Env map" = a texture addressed by the camera-space vertex normal.

The engine internals (transformation, sort, polygon set-up, the generic filler table) are the engine scope's
(`notes-engine.md`, which names the packed words the other way round: its tu = my v = high word); the int 8
handler, timer, Cirrus probe, exit fades and the hand-overs between parts are core's (`notes-core.md`, sections 3
and 5). This file gives p6's and p7's scripts, data, per-frame work, the part-specific fillers (bump-env mapping, the
shadow masks, the palette morph, the moving lights) and how they parametrise the engine.

Checked (scripts in `models/p6p7/`, section 9):

- the script models (section 2.2, 3.2): group position, group angles, cat angle, label slide, bump strength and
  palette morph stepped per retrace from the step tables equal the emulator's state in 7 p6 and 4 p7 snapshots;
- p6 per-frame tables: the palette morph (DAC 16..207 from the two ramps), the bump offset table (cs:0010 from the
  bump strength) and the shadow mask written into the env map (index from the group angle) equal memory **exactly**
  in 5 snapshots (bump strength 0x100 and 0, morph 0, 0x8b0, 0x1000; masks 0, 6, 20, 24, 28);
- the vertex transformation (matrix, projection, env coordinates, p7's normal-based bump coordinates, depth) equals
  the engine buffers **exactly** for every vertex: p6 cat and rings in 3 snapshots, p7 tentacle (1446 vertices) in 3
  memory dumps;
- the texel addressing of both bump-env fillers (bump texel from the UV, offset table, env texel) against 3000
  probed pixels each (p6 1e22 / 1e3c, p7 2052 / 206c): all equal;
- the p7 light map (base picture + 5 spots, 32-bit adds) built from the retrace counter equals the 64 KB buffer in
  memory **exactly** in 4 snapshots; with the light positions of the whole part no byte exceeds 255 in the
  reachable area (max 234), so the 32-bit adds never carry;
- whole frames: an approximate rasteriser fed with the engine buffers of 5 p6 and 3 p7 frames draws the same picture
  (31-51 % of the 3D pixels identical, mean colour difference 2.4-5.4 / 63; the rest is span interpolation and edge
  rounding, the engine scope's).

---------------------------------------------------------------------------------------------------------------------

## 1. What is on screen, and when

Step starts are the frames on which the script's "run N retraces" call starts (traced in the emulator). p6 draws a
frame on every retrace (69.8 fps in the capture) and every step lasts exactly N retraces; p7 drops to 41-56 fps but
its steps still start on their nominal retrace (the snapshots match the nominal sums).

### p6 (frames 25588-29596, 365.09-422.28 s, music 55:3c-64:1f)

| step | N | starts (frame, s) | changes before the step | on screen |
|---|---|---|---|---|
| set-up | — | 25588 | — | p5's last screen (the swirl, DAC 0..15) stays; p6 sets no video mode |
| 1 | 140 | 25596, 365.22 | group pos (-3072000, 0, 4864000), vel (8000, 0, -8600), avel (-4, 128, 0); cat avel (0, -180, 0) | the cat inside two blue rings comes in from the left over the swirl, the rings turning |
| 2 | 140 | 25736, 367.22 | label "SHADOW volume" slides in (`[176a]` = 1) | |
| 3 | 140 | 25876, 369.22 | vel (5000, 0, 0) | |
| 4 | 280 | 26016, 371.21 | vel (0, 0, 6000) | moving away |
| 5 | 210 | 26296, 375.21 | vel 0 | |
| 6 | 490 | 26506, 378.20 | avel (-14, 128, 0), vel (0, 0, -5000) | tilting, flying in to a close-up (ring shadows on the fur) |
| 7 | 280 | 26996, 385.20 | avel (14, 128, 0), vel 0 | close-up |
| 8 | 210 | 27276, 389.19 | label out, avel (-4, 128, 0), vel (0, 0, 6000) | moving away |
| 9 | 210 | 27486, 392.19 | label "material MORPH" slides in | |
| 10 | 210 | 27696, 395.18 | avel (16, 128, 0), vel (0, 0, -3000) | |
| 11 | 420 | 27906, 398.18 | bump -4/retrace, morph +16/retrace, vel (0, 0, -4000), avel (-8, 128, 0) | the fur flattens (64 retraces) while the colours turn from orange fur to blue-grey metal (256 retraces) |
| 12 | 280 | 28326, 404.17 | vel (0, 0, 400), avel (0, 128, 0), bump +6, morph -24 | back to fur (bumps in 42 retraces, colours in 171) |
| 13 | 280 | 28606, 408.17 | vel 0, bump -6, morph +24 | to metal again (42 / 171 retraces) |
| 14 | 140 | 28886, 412.16 | avel (-4, 128, 0), label out | metal cat |
| 15 | 280 | 29026, 414.16 | avel (8, 128, 0), label "METAL shading" slides in | |
| 16 | 280 | 29306, 418.16 | label out, vel (6000, 0, -2600) | the group drifts out to the right |
| exit | — | 29586, 422.15 | timer restored, fade to black (37 steps, notes-core 5) | the last frame fades; black from 29597 |

Totals: 3990 retraces. Group state at each step start (from the model, checked): pos z 4864000, 3660000, 2456000,
2456000, 4136000, 4136000, 1686000, 1686000, 2946000, 4206000, 3576000, 1896000, 2008000, ... (x reaches -132000 at
step 4 and 1548000 at the end); group angle a: 0 → 0xd6fc (-10500, its minimum, step 7) → 0xef0c at the end;
angle b = 128 r (r = retraces since step 1); cat's own b = -180 r.

### p7 (frames 29597-32119, 422.29-458.28 s, music 64:1f-6d:28)

| step | N | starts (frame, s) | changes before the step | on screen |
|---|---|---|---|---|
| set-up | — | 29597 | mode 13h, DAC, border colour 0, timer | black |
| 1 | 210 | 29604, 422.41 | group pos (0, 2304000, 3584000), vel (0, -4000, 0), avel (0, 0, 48), label hidden (`[19c8]` = -1) | horizon picture at the bottom; the flat 8-armed tentacle object comes down from the top, turning about the view axis, lit by 5 moving light spots |
| 2 | 490 | 29814, 425.40 | vel (0, -2000, 0), label "multiple LIGHT sources" slides in | |
| 3 | 70 | 30304, 432.39 | vel (0, -1200, -3000), avel (-18, 7, 58), label out | starts tilting, comes closer |
| 4 | 140 | 30374, 433.39 | vel (700, -500, -3000) | |
| 5 | 420 | 30515, 435.39 | avel (-18, 7, 68), label "1995 WORLD domination" slides in | grows to fill the screen |
| 6 | 560 | 30935, 441.39 | vel (2400, 0, 0), avel (-18, 7, 78) | leaves to the left |
| 7 | 210 | 31494, 449.37 | label out | horizon only |
| end | — | 31704, 452.36 | "dope" picture copied into video memory (rows 50..113) | "dope" above the horizon |
| exit | 350 | 32054, 457.36 | after 350 retraces: timer restored, fade (257 steps), mode 3 | fade to black; text mode at 32120 |

Totals: 2100 retraces of script, then 350 retraces of "dope", then the fade (257 retraces on a real VGA, 66 frames in
the capture, notes-core 5.2).

---------------------------------------------------------------------------------------------------------------------

## 2. The engine as p6 and p7 use it

Both parts carry the shared engine (engine scope: p6 and p7 addresses below). What differs from p2 is listed here.

| routine | p6 | p7 |
|---|---|---|
| object list (0x3A-byte records, record 0 = group) | 1620 (group, cat 165a, rings 1694, end 16ce) | 1810 (group, 184a, 1884, 18be, tentacle 18f8, end 1932) |
| all objects / one object | 2c9c / 2bdd | 2eca / 2e0b |
| matrix / vertices (rotation) / faces | 1b56 / 1a30 (1930) / 2ac7 | 1dd6 / 1cb0 (1b90) / 2cf7 |
| radix sort (ascending, bits of the depth sum) | 2cd9 (bits 4..15, 12 passes) | 2f05 (bits 5..15, 11 passes) |
| draw loop / filler set-up | 2a60 / 1fd1 | 2c90 / 2201 |
| span loops | bump-env 1db0 (unclipped 1e16, clipped 1f36); plain env 2780 → 25f0 (264b / 2721) | bump-env 1fe0 (2046 / ...) |
| label + copy + background restore | 2d5c | 2f8c |
| run N retraces | 2de6 | 3070 |
| per-retrace / int 8 / timer / restore | 1cc0 (+3895) / 17ee / 1834 / 18ec | 1f40 / 1a47 / 1a8a / 1b3e |
| sine table built from the quarter wave | 179b (from cs:0220) | 19f4 (from cs:0410) |
| engine buffers (segment) | 667b: uv +0, env +0c80, screen +1900, depth +2580, sort list +3200, face tables +5140 / +5910 / +60e0 | 3de1: sort list +0, uv +3e80, env +55f0, screen +6d60, depth +84d0, face tables +9c40 / +abe0 / +bb80 |
| frame buffer | 568b:0140 (`[177a]`) | `[19d4]` (4a93, relocated) +0140 |

### 2.1 Object records

```
+00 u16 mesh segment (0 = end of list, 0xFFFF = group record, not drawn)   +02 u16 skip (non-zero: not drawn, not moved)
+04 s16 F, the focal length (p6 3500, p7 2500)      +06/+0A/+0E s32 position     +12/+16/+1A s32 velocity / retrace
+1E/+20/+22 u16 angles a, b, c                       +24/+26/+28 s16 angular velocity / retrace
+2A u8 env-map selector add, u8 bump selector add    +2C u16 flip the back-face test    +2E/+32/+36 s32 vertex offset (0)
```
Every object is drawn with angle = object angle + group angle and position = object position + group position (group
= record 0). The per-retrace routine adds velocity to position and angular velocity to angle for every record with
skip = 0, including the group.

| part | record | mesh | F | +2A | role |
|---|---|---|---|---|---|
| p6 | 165a | 1cc2:0000 (352 vertices, 671 faces) | 3500 | 01 01 | the cat ("found from some cdrom", PARTS.md), own avel b = -180 |
| p6 | 1694 | 202d:0000 (256 vertices, 512 faces) | 3500 | 00 02 | two tori: major radius 7884, tube 459, 16 x 8 vertices each; ring 0 in the yz plane, ring 1 in the xy plane |
| p7 | 18f8 | 23b4:0000 (1446 vertices, 2888 faces) | 2500 | 00 00 | the tentacle object: flat (z ±534), 8 curled arms, xy ±8191 |
| p7 | 184a, 1884, 18be | 23b4 | 400, 800, 1600 | 00 00 | skip = 0xFFFF: never drawn (left-overs) |

### 2.2 Mesh format (engine format, read from the image)

Header words: `[0]` vertex count, `[2]` face count, `[4]` offset of positions (3 x s16 per vertex), `[6]` offset of
faces (8 bytes: u16 material, 3 x u16 vertex index * 4), `[8]` offset of normals (3 x s16, length 27559 = 0.841 x
32767), `[A]` offset of texture coordinates (u16 u, u16 v; 8.8 fixed point). Cat: 160 29f 0010 0850 1d48 2588;
rings: 100 200 0010 0610 1610 1c10; tentacle: 5a6 b48 0010 21f4 7c34 9e18. All face materials are 0.

### 2.3 Transformation (p6 1930/1a30, p7 1b90/1cb0; checked exactly)

```c
SIN[i]: 2048 entries per turn, Q15; built from the quarter wave q[0..511] = round(32767 sin(2 pi i / 2048)) stored
        in the image (p6 cs:0220, p7 cs:0410): [0..511] = q, [512..1023] = q reversed (q[511-k]: one entry late),
        [1024..2047] = -[0..1023], [2048..2559] = q; cos(i) = SIN[i + 512].
for each object:
    A_k = (int16)(obj.ang[k] + group.ang[k]) >> 4;  s_k = SIN[(A_k & 0xFFE) / 2];  c_k = SIN[... + 512]   // a, b, c
    M (Q15, sar 15 after each product):
        m00 = c2 c3 - (s2 s1 >> 15) s3      m01 = c1 s3      m02 = s2 c3 + (c2 s1 >> 15) s3
        m10 = -(c2 s3 + (s2 s1 >> 15) c3)    m11 = c1 c3      m12 = -(s2 s3 - (c2 s1 >> 15) c3)
        m20 = -s2 c1                         m21 = -s1        m22 = c2 c1
    P = obj.pos + group.pos
    for each vertex i (v = V[i] + obj offset, n = N[i]):
        x = M0.v + (P.x << 8);  y = M1.v + (P.y << 8);  z = M2.v           // 32-bit
        depth[i] = (((z >> 16) * F) >> 14) + 0x4000                         // sort key part (word)
        D = z - (P.z << 8)                                                  // negative: P.z is the distance
        q = x * F / D;      sx = (q >> 3) + ((q >> 2) & 1) + 160            // idiv truncates, then round
        q = 3 * (y * F / D); sy = (q >> 5) + ((q >> 4) & 1) + 100
        screen[i] = sy | sx << 16;   drawn on screen row sy - 1 (the buffer starts one row above the screen)
        a = M0.n;  b = M1.n;  E = (a & 0xFFFF0000) | ((b >> 16) & 0xFFFF)
        p6: env[i] = 0x40004000 - E       p7: env[i] = 0x40004000 + E        // u2 = low word, v2 = high word (8.8)
        p6: uv[i] = UV[i] (mesh texture coordinates)
        p7: uv[i] = w15(n.x) | w15(n.y) << 16,  w15(t) = ((t << 1) as int16) >> 1   // object-space normal; the mesh
                                                                                    // UV is loaded and not used
```
In plain terms (vertex units, p = R v + P/128 with R = M/32768, distance d = P.z/128 - (R v).z):
`sx = 160 - (F/8) p.x / d`, `sy = 100 - (3F/32) p.y / d`, so camera x points to the screen's left, y up; the
projection centre is screen pixel (160, 99). Env coordinates with the unit camera-space normal (n_right, n_up)
(53.82 = 27559 * 32767 / 2^24 texels):

- p6: env row = 64 - 53.82 n_up, env column = 64 + 53.82 n_right (the map is oriented like the screen);
- p7: row = 64 + 53.82 n_up, column = 64 - 53.82 n_right in the lookup, plus the 0x4040 base of the light map
  (section 4.4): light-map row = 128 + 53.82 n_up, column = 128 - 53.82 n_right (turned by 180° against the screen).

### 2.4 Faces (p6 2ac7, p7 2cf7)

Back-face test with 16-bit products: `(x1-x0)(y2-y0) - (x2-x0)(y1-y0) >= 0` is drawn (`+2C` = 0); sort key = sum of
the three depths, all objects in one list, sorted ascending = far first (painter's algorithm). Per face the second
and third vertex pointers carry two 2-bit selectors in their low bits:

```
env selector  = (face.material_lo + obj[+2A] + [p6 1762 / p7 19c0]) & 3  -> p6 cs:0210 = {0449, 0451, 0449, 0451}
                                                                          p7 cs:0400 = {17b8 x 4}
bump selector = (face.material_hi + obj[+2B] + [p6 1760 / p7 19be]) & 3  -> p6 cs:0218 = {2398, 23a0, 0, 0}
                                                                          p7 cs:0408 = {2fe9, 2ff1, 2fe9, 2fe9}
```
(segments relocated; 0451 = 0449 + 0x80 bytes = the right half of a 256-byte-wide map; bump segment 0 = plain env
filler). So: p6 cat = env right half 0451, bump 23a0 (fur); p6 rings = env left half 0449, no bump; p7 tentacle =
light map 17b8, bump 2fe9 (left half, "wavy").

### 2.5 The fillers (affine, flat, one texel per pixel; texel addressing checked by probes)

The set-up (p6 1fd1, p7 2201: engine scope) interpolates two packed dwords linearly across the triangle: `ecx` = uv
(v << 16 | u) and `edx` = env (v2 << 16 | u2). Per pixel:

```c
// p6 bump-env (1e16..1e45 / 1f36..1f63): cat
t   = fs:[ ((((u >> 8) & 31) << 2 | (v >> 14)) << 8) | ((v >> 6) & 127) ];   // bump map, 256-byte rows
o   = TAB6[t];                                                               // word table cs:0010
pix = ds:[ (((u2 >> 8) << 8) | (v2 >> 8)) + o ];                             // 16-bit add; env row u2>>8, col v2>>8
// TAB6 (3913, every retrace, k = [176e] = bump strength 0..0x100):
//   B[i] = (((i - 8) * k) & 0xFFFF) >> 8   (i = 0..15, floor((i-8)k/256))
//   TAB6[t] = B[t >> 4] << 8 | B[t & 15]   -> env row += B[hi nibble], column += B[lo nibble]
//   (a negative column offset carries into the row: row += 1 more)

// p6 plain env (264b / 2721): rings
pix = fs:[ ((u2 >> 8) << 8) | (v2 >> 8) ];                                    // span length counted in AL (8 bits)

// p7 bump-env (2046..2075): tentacle
t   = fs:[ (((u >> 8) & 127) << 8) | ((v >> 8) & 127) ];                     // u, v = object normal x, y
o   = TAB7[t];                                                               // word table cs:0000, static data
pix = ds:[ (((u2 >> 8) << 8) | (v2 >> 8)) + o ];                             // ds = 17b8 = light map 13b4 + 0x4040
// TAB7[t] = ((l - 9 + (h >= 9)) << 8) | ((h - 8) & 0xFF), h = t >> 4, l = t & 15: effective
//   row += l - 8, column += h - 8 (for h = 8: row += l - 9)
```
The p6 bump map is therefore read at 4x the UV scale: column = 4 v mod 128 with two fraction bits, row = 4 floor(u)
mod 128 plus v's top two bits (always 01 for the cat's v = 0x4145..0x7FFF): only rows 1, 5, 9, ..., 125 of the fur map
are used, one per u unit; the cat's UVs (u 66..127, v 65..127) span about two tiles. The two parts read the same
fur picture with the nibbles swapped (p6: high nibble = row offset; p7: high nibble = column offset); with p7's
assignment the offsets are close to the gradient of a height field (least-squares residual 2.2 of mean magnitude
8; with p6's assignment 5.3), so p7 uses the map as drawn and p6 transposes it (`maps.py`).

---------------------------------------------------------------------------------------------------------------------

## 3. p6: the cat

### 3.1 Set-up (entry 38e8, script 3951)

```c
2ecf: sine table (179b);  out(0x21, 0) (unmask every IRQ, never restored);  [1758] = int 33h AX=0 (0xFFFF with a mouse)
3951: copy 0c97:0000 (64 KB) -> 568b:0140 (frame buffer = the swirl)
      copy 0419:0000 (768 bytes, palette pal1) -> cs:325d
      38ca (morph ramps for [3259] = 0); wait for the retrace
      DAC 0..15 = 0c66:0010 (48 bytes: the swirl's colours), DAC 16..239 = 0418:0040 (= pal1[16..239]),
      DAC 240..255 = [176c]:02da = 3399:02da (the label palette, 16 greys)
      per-retrace routine = 1cc0; timer 1834
      group: pos (-0x2ee000, 0, 0x4a3800), avel (-4, 0x80, 0); [1684] = 0x0101 (cat +2A), [16be] = 0x0200 (rings +2A)
      [176e] = 0x100 (bump strength), [1770] = 0; cat avel (0, -180, 0); vel (0x1f40, 0, -0x2198); steps (1.)
```

### 3.2 Per retrace (1cc0, then 3895 from the int 8 handler)

```c
w += dw;  if (w < 0 || w >= 0x4B) dw = 0;                         // [1768] label width, [176a] +-1
m += dm;  if (m < 0 || m >= 0x1000) dm = 0;                       // [3259] morph 0..0x1000 (can end at -8)
k += dk;  if (k < 0 || k >= 0x100) { k -= dk; dk = 0; }           // [176e] bump strength, [1770]
TAB6 = build(k);                                                  // 3913
for each record with skip == 0: pos += vel; ang += avel;          // group, cat, rings
[1754]++ (unused), ready++ ([1732]), retraces++ ([1736])
3895: for c in 16..207, ch in r,g,b:                              // the palette morph, every retrace
          DAC = min(63, RA[pal1[c][ch]] + RB[pal2[c][ch]])
// RA, RB (3871, called by 38ca once per drawn frame): R_w[i] = (i * min(max(w,0) >> 4, 0x100)) >> 8, i = 0..63,
//   RA from w = 0x1000 - m, RB from w = m;  pal1 = cs:325d (copy of 0419:0000), pal2 = cs:3565 (static, in the image)
```

### 3.3 Frame loop (2de6) and the shadow masks

```c
do {
    2c9c: transform all objects, collect faces;  2cd9: sort
    // shadow mask into the cat's light map (2e05..2e66):
    ax = -b;  if ((int16)(a - 0x4000) >= 0) ax = b;               // a, b = GROUP angles [163e], [1640]
    mask = 368a:0000 + ((ax & 0x3FFF) >> 9) * 4096;               // 32 masks of 64 x 64 bytes, bit 0 used
    for row in 0..127, col in 0..127:                             // the right half of the env map 0449
        e = env[row][128 + col];
        env[row][128 + col] = ((e - 0x10) & 0x3F) + ((mask[row/2][col/2] & 1) ? 0x50 : 0x90);
    38ca: morph ramps RA, RB from [3259]
    2a60: draw all faces (section 2.5)
    if ([1758]) mouse (section 3.6)
    2d5c: wait for ready; label; copy 568b:0140 (201 rows) -> A000; restore 568b:0140 from the swirl 0c97:0000
} while (N > retraces);  (a key: exit code 0xFF, jump to the exit 3d22)
```
The group angle a never leaves 0xd6fc..0x0000 (-58°..0°), so `ax = -b` throughout and the mask index is
`((-128 r) & 0x3FFF) >> 9` with r = retraces since step 1: masks 0, 31, 30, ... 1, changing every 4 retraces, one
cycle per 128 retraces (a quarter turn of the rings, which have that symmetry).

The masks show the two rings as seen face-on / edge-on while they turn: mask 0 has a 5-pixel vertical band (ring 0
edge-on, x ≈ 28..33) and dark corners (ring 1 face-on, larger than the square); the band opens into an ellipse that
fills the width at mask 16 (both rings at 45°) and closes into a band at x ≈ 37..40 at mask 31 (`look6.py`; 17 %
to 34 % of each mask is shadow). Written into env-map space they put the rings' shadows on the cat as a function of
the surface's normal direction: "SHADOW volume" is this precomputed mask, there is no shadow geometry at run time.
How the masks were made is not known: a parallel projection of the two tori fits them only to 82 % (the all-lit mask
fits 78 %), a shadow onto a sphere worse (`shadow_fit*.py`).

### 3.4 The cat's look: light map, fur, morph

The right half of the env map (0451) is a generated radial ramp: `shade = min(63, 64 - r/2)`, r = distance in texels
from (row 92, column 92) of the half (checked: equal radius gives equal shade within 1), stored as colour 80 + shade.
The frame loop turns every texel into colour 80 + shade (lit) or 144 + shade (shadow) by the mask. The highlight at
(92, 92) means light from the lower right and the front (n_right = 0.52, n_up = -0.52).

Palettes (6-bit): pal1 (0419:0000, the env map's RIX palette) has fur ramps: 80..143 black → brown → orange → white
(e.g. 100 = 17,5,1; 120 = 29,11,5; 143 = 63,63,63), 144..207 black → dark brown (160 = 6,1,0; 207 = 26,18,0). pal2
(cs:3565) has metal ramps: 80..143 black → blue-grey → white (100 = 6,9,13; 120 = 17,17,24), 144..207 black → dark
blue (207 = 7,11,19). Colours 16..79 are equal in both (the rings), 208..239 are unused grey 22.

- fur: bump-env with the fur map at strength k = 0x100 (offsets -8..7 env texels);
- "material MORPH": k falls to 0 (64 retraces, step 11) while the palette goes from pal1 to pal2 over 256 retraces;
  back in step 12 (42 / 171 retraces, k stops at 0xfc), forward again in step 13;
- "METAL shading": k = 0 (smooth surface, plain env lookup through the bump filler) with pal2. Nothing else changes:
  the metal is the same radial light map with the shadow mask, through blue-grey ramps.

### 3.5 Rings

Plain env mapping (no bump, filler 25f0) with the left half of 0449: a blue cloud picture (colours 16..79, pal1 =
pal2 there, so the morph leaves the rings alone except for a -1 rounding in the ramps sum during the morph).

### 3.6 Mouse (only with a mouse driver; the capture had none, the remake follows the no-mouse path)

Once per drawn frame `int 33h AX=3`; when the button state differs from the last one, `[1762] += buttons & 1` and
`[1760] += (buttons >> 1) & 1` (presses count, releases add 0; the position is stored in [175a]/[175c] and not
used). Effect per press:
- left: env selector + 1 for every object: the cat gets the rings' blue cloud map, the rings get the cat's light map
  (with the shadow masks and the fur / metal palette); the next press swaps back;
- right: bump selector + 1: cat 1 (fur) → 2 (none) → 3 (none) → 0 (the flat left half of 2398, all 0x88: zero
  offsets, looks like none) → 1; rings 2 (none) → 3 (none) → 0 (flat) → 1 (fur bumps on the rings) → 2.

### 3.7 Label and frame copy (2d5c; p7 2f8c is the same)

```c
w = [1768];  if (w >= 0) {                                        // label picture [176c]: RIX3 70 x 44, pixels at +030a
    x = 0;  if (w > 70) { x = w - 70; w = 70; }                   // final w = 75: x = 5
    copy rows 0..43, source columns 70-w..69 -> buffer offset 0b40 + x: buffer rows 9..52 = screen rows 8..51  (opaque)
}
```
The label slides in from the left screen edge (1 column per retrace for 70 retraces), then moves 5 more pixels
right; sliding out reverses it (76 retraces). Colours 240..255 (the label palette, loaded once from 3399:02da; the
three labels carry identical palettes).

### 3.8 Exit (3d22)

Timer restored, then the DAC fades to black: `(v * L) >> 8` with L = 256, 249, ..., 4 (37 steps, one per retrace on
a real VGA, about 4 per frame in the capture; notes-core 5.2), then all 0.

---------------------------------------------------------------------------------------------------------------------

## 4. p7: "multiple LIGHT sources", "WORLD domination", "dope"

### 4.1 Set-up (entry 3194 → 318d sine table 19f4, script 31ca)

Mode 13h (`int 10h`), per-retrace routine 1f40, timer 1a8a; wait for the retrace; DAC 0..239 = 5a84:0000 (720 bytes:
the horizon picture's RIX palette), DAC 240..255 = 3beb:02da (label palette); attribute register 11h (border) = 0;
`[19c8]` = -1 (no label), then the steps of section 1.

### 4.2 Per retrace (1f40)

Label slide (as p6), `[19da] += 100` (unused), motion of every record with skip = 0 (the group; the tentacle record
has no velocities), `ready++` ([1996]), `retraces++` ([199a]), `[19b8]++` (16-bit, never reset: retraces since the
timer started; drives the lights).

### 4.3 Frame loop (3070): the light map

```c
copy 03b3:0000 -> 13b4:0000 (65536 bytes: the base picture, 256 x 256)
t = [19b8]
for each of 5 lights: dst = 13b4:(r << 8 | c); for 128 rows: add 37ea (128 x 128 spot) 32 bits at a time
    // light   table index                     (r, c) = top-left of the spot (its peak is at +63, +63)
    // 1       i = (t & 0xFFE)/2   = t/2      (64,                  64 + (SIN[i] >> 9))
    // 2       i = (t << 3 & 0xFFE)/2 = 4t    (64 + (COS[i] >> 9),  64)
    // 3       i = 2t                         (64 + (SIN[i] >> 9),  64 + (COS[i] >> 9))
    // 4       i = t                          (64 + (COS[i] >> 9),  64 + (SIN[i] >> 9))
    // 5       j = 3t/2 (index (3t & 0xFFE)/2); i = (SIN[j] >> 4) & 0x7FF   (64 + (SIN[i] >> 9), 64 + (COS[i] >> 9))
2eca transform, 2f05 sort, 2c90 draw (section 2.5), 2f8c label, copy, restore the horizon 5ab4:0000
```
(indices mod 2048; `>> 9` of Q15 gives -64..63.) As continuous functions of t (retraces): light 1 moves
horizontally, c = 64 + 64 sin(2π t/4096); light 2 vertically, r = 64 + 64 cos(2π t/512); light 3 on a circle,
period 1024; light 4 on a circle, period 2048, other direction; light 5 on the circle at the angle 2π sin(2π t/1365.3)
(swinging a full turn either way). The spot is a smooth radial falloff (63 at the centre, about 46 at 16 texels, 27
at 32, 8 at 56, 0 beyond 64; close to 63 exp(-(r/36)^2), rms 3; not exactly symmetric). The 32-bit adds would
carry into the next pixel above 255; over the whole part the reachable area peaks at 234, so they never do.

The base picture (RIX3 256 x 256, pixels at 03b3:0000) is 32 outside the centre square and a digitised photograph in
rows / columns 63..190 (values 19..63: a person lying on the floor holding a hand-written sign, a can on their head)
— through p7's palette these values are near black, so the photo shows only as a faint texture inside the highlights.

### 4.4 The tentacle's look

Bump-env (section 2.5): light-map texel at (128 + 53.82 n_up + dy, 128 - 53.82 n_right + dx) where (dy, dx) come from
the left half of 2fe9 (a wavy bump picture, 128 x 128, tiled) addressed by the object-space normal (x/256, y/256 mod
128): the bumps are fixed to the object and follow its normals, not a UV map. Palette p7 DAC 16..199: grey ramp
(0 → 63, slightly blue in the middle: 80 = 12,12,15), 200..239 white. The lights therefore show as moving white
highlights on a dark grey, bumpy surface. The 15-bit wrap of the normal before packing (`w15`) shifts values beyond
±16384 by 32768 = one whole bump tile, so a face whose vertex normals straddle ±16384 (±36° from the axis) gets one
extra tile repetition squeezed across it (fine bands along such faces).

### 4.5 Horizon and "dope"

The background of every frame is the horizon picture (5ab4:0000, RIX3 320 x 200: black sky, a blue band and a desert
silhouette with cacti in rows 155..199), restored into the buffer after each copy (201 rows, the last one off
screen). After step 7: `copy 6a86:0000 (320 x 64) -> A000:3e80` (screen rows 50..113, over the black sky; the
horizon stays from the last frame), wait until `ready` counts 350 retraces (the timer still runs), restore the timer,
fade (L = 256, 255, ..., 0, 257 steps), DAC all 0, `int 10h AX=3` (text mode, 3410). "dope" uses the grey ramp
(40..195) and two label colours (247, 252): white anti-aliased letters.

---------------------------------------------------------------------------------------------------------------------

## 5. Pictures and maps (all read from the unpacked images; PNGs in `dump/pictures/`, written by `export_pictures.py`)

RIX3 = ColoRIX file embedded in the image: "RIX3", u16 width, u16 height, AF 00, 768-byte palette, pixels.

| name | where (pixels) | size, format | made | how it is used / what a redraw needs | PNG |
|---|---|---|---|---|---|
| swirl | p6 0c97:0000 (RIX3 at 0c66:0006, palette 0c66:0010) | 320 x 200, colours 0..15 | drawn (a twirled cloud texture, 16 blues) | copied as-is as the background of every frame, static DAC 0..15: a true-colour redraw replaces it directly (also p5's last screen, notes-p3p5) | p6-swirl.png |
| env map, left half | p6 0449:0000, columns 0..127 (RIX3 at 0418:0006, palette pal1 at 0419:0000) | 128 x 128 of a 256 x 128 map, colours 16..79 | drawn (blue clouds) | rings' env map, sampled by normal direction over the disc of radius 53.8 around (64, 64), static palette: a redraw replaces it directly (same layout: normal → texel as in 2.3) | p6-envmap.png |
| env map, right half | p6 0449:0080 (same file) | 128 x 128, colours 80..143 | generated: shade = min(63, 64 - r/2) around (92, 92) | an index ramp, not a picture: overwritten per frame with the shadow mask (80 / 144 + shade) and coloured through four ramps (pal1 / pal2 x lit / shadow) mixed by the morph; compute the shade and the ramps instead of redrawing | p6-envmap.png, p6-envmap-metal.png (with pal2) |
| fur bump map | p6 2398:0080 (rows 0..127 used; columns 0..127 of 2398 are all 0x88, rows 128..255 too) | 128 x 128 bytes, two 4-bit offsets | drawn pattern, encoded as offsets (close to the gradient of a height field, section 2.5) | a map: a redraw would be a height map whose gradient gives the same ±8-texel offsets (p6 transposed, p7 as drawn); or keep the offsets and filter them | — |
| shadow masks | p6 368a:0000 | 32 x 64 x 64 bytes, bit 0 | generated (method unknown, 3.3) | binary masks written into the light map; upsample with a smooth edge and blend neighbouring masks | — |
| labels | p6 3399, 3494, 358f (RIX3, pixels +030a; palette 3399:02da → DAC 240..255) | 70 x 44, 16 greys | drawn (pixelled lettering) | pasted opaque as-is, sliding: a redraw replaces them directly | p6-label-shadow/morph/metal.png |
| pal2 (metal) | p6 cs:3565 (file 0x3665) | 768 bytes | — | the morph target (3.2) | (palette of p6-envmap-metal.png) |
| horizon | p7 5ab4:0000 (RIX3 at 5a83:0006, palette 5a84:0000 → DAC 0..239) | 320 x 200 | drawn (desert silhouette, sky gradient) | background of every frame, static palette: a redraw replaces it directly | p7-horizon.png |
| "dope" | p7 6a86:0000 (RIX3 at 6a55:0006, its palette = the horizon's) | 320 x 64 | drawn (lettering, anti-aliased) | pasted once into video memory at rows 50..113, then the whole DAC fades: a redraw replaces it directly (the fade is a brightness scale) | p7-dope.png |
| labels | p7 3beb, 3ce6 (RIX3, +030a; palette 3beb:02da) | 70 x 44 | drawn | as p6 | p7-label-light.png, p7-label-world.png |
| light-map base | p7 03b3:0000 (RIX3 at 0382:0006, grey palette) | 256 x 256, values 19..63 inside, 32 outside | digitised photograph (not drawn) | added to by the 5 spots, then indexes p7's grey ramp: an intensity map in index units; a redraw must stay a faint greyscale map (values ≈ 19..63) | p7-reflection-photo.png (own grey palette, brighter than in the demo) |
| light spot | p7 37ea:0000 (raw) | 128 x 128, 0..63 | generated radial falloff | added 5 times per frame: evaluate it (or sample it bilinearly) | — |
| wavy bump map | p7 2fe9:0000, columns 0..127 (raw; columns 128..255 = p6's fur map, unused in p7) | 128 x 128, two 4-bit offsets | drawn pattern, encoded | as the fur map | — |
| offset table | p7 cs:0000 (static); p6 cs:0010 (built per retrace) | 256 words | generated | section 2.5 | — |

---------------------------------------------------------------------------------------------------------------------

## 6. Time bases

| variable | where | time base |
|---|---|---|
| group / cat position and angles (p6), group position and angles (p7) | 1cc0 / 1f40 | retraces, linear per step (velocities in section 1) |
| label slide w | 1cc0 / 1f40 | retraces, ±1 per retrace, clamped to -1..75 |
| bump strength k, palette morph m (p6) | 1cc0 | retraces (±4/±6 and ±16/±24 per retrace) |
| DAC 16..207 (p6) | 3895 | uploaded every retrace from ramps recomputed per drawn frame from m (one frame late at most) |
| bump offset table (p6) | 3913 | every retrace from k |
| shadow mask index (p6) | frame loop | per drawn frame from the group angle b (a retrace quantity): mask changes every 4 retraces |
| light positions (p7) | frame loop | per drawn frame from [19b8] = retraces since the timer started |
| "dope" hold / fades | 33a8 / 3d22, 33be | 350 retraces; fades one step per retrace on a real VGA (faster in the capture) |

Nothing reads the music, nothing uses random numbers, and nothing is advanced per drawn frame: at any frame rate the
motion is the same (p7 drops to 41 fps in the close-ups and shows the same positions).

---------------------------------------------------------------------------------------------------------------------

## 7. Shortcuts to evaluate exactly, and quirks

Evaluate exactly:
- angles from the 2048-entry sine table (with its one-entry lag in the 2nd and 4th quarters) and Q15 matrices:
  continuous sin/cos of angle * 2π/65536;
- projection with integer division and rounding to whole pixels; affine texture interpolation: perspective-correct
  interpolation at full resolution (the faces are small, the difference is slight);
- env lookup by the interpolated packed 8.8 coordinates, nearest texel: per-pixel normal → (row, col) as in 2.3,
  filtered reads of the env pictures; the cat's light map from its formula (3.4);
- the morph ramps (`(i*w)>>8` sums, ±1 rounding) and the 64-step shade ramps: mix pal1 / pal2 ramps continuously,
  m = [3259]/4096 clamped to 0..1;
- the bump: the 4-bit offsets (and, in p6, the ±k scaling floored to whole texels) become continuous offsets: filter
  the offset map, scale by k/256; drop the 16-bit carry (+1 row for negative column offsets in p6, -1 row at h = 8 in
  p7) and the -1/256 of the p6 env negation; the p7 15-bit normal wrap (4.4) is a packing artefact (drop it);
- painter's sort of whole faces by the sum of their vertex depths (ignoring the object position) and the 16-bit
  back-face products (see notes-p2 7.4 for where those fail in p2): a depth buffer gives the same picture without
  the sort errors;
- the shadow masks: 64 x 64 bits per 1/128 turn, switched every 4 retraces: upsample with smooth edges and
  cross-fade between masks by the fractional index, or replace by an exact computation once its geometry is known;
- the screen buffer offset: the 3D picture appears one row higher than the projection formula (centre row 99).

Keep (part of the look):
- the p6 fur read at 4x the UV scale with only every 4th row of the map (one row per u unit, continuous along v):
  keep the scale and sample a 32-row version (rows 1, 5, ..., 125) of the map with filtering, rather than all rows;
- the shadow masked by normal direction (the ring shadows slide over the cat with its orientation, not with its
  position), the radial light map, the bump-env "fake bump" (env lookup shifted by the bump offsets, no tangent
  frame);
- p7's light map turned by 180° against the screen, the faint photo in it, the white saturation of the palette above
  index 200, and the five light paths;
- the labels' slide (from the left edge, 1 px per retrace, then 5 px more) and their opaque boxes.

Not visible but in the code: the plain filler (p6 rings) counts span pixels in 8 bits, so a span wider than 255
pixels would be cut to its width mod 256; the widest ring triangle in the checked frames is 225 px (frame 27000),
so this is not seen in the capture.

---------------------------------------------------------------------------------------------------------------------

## 8. Unknowns and guesses

- How the 32 shadow masks were made (section 3.3); guess: an offline shadow of the two rings for 32 turns of a
  quarter turn about the vertical axis, with the light at the light map's highlight.
- The swirl, the env-map clouds and the horizon look drawn / painted; the fur and wavy maps look like hand-drawn
  patterns converted to offsets by a tool (guess).
- On a real VGA the fades take one step per retrace (0.53 s for p6, 3.67 s for p7); the capture's emulator writes the
  DAC in zero time and shows them about 4 times faster (notes-core 5.2).
- The mouse path was read from the code only (the capture had no driver).

---------------------------------------------------------------------------------------------------------------------

## 9. Models (`models/p6p7/`, see its README.md)

| script | checks |
|---|---|
| `timeline.py` | both scripts' step tables and the per-retrace integration against 11 snapshots (all equal) |
| `check_p6_tables.py` | p6 morph ramps / DAC 16..207, bump offset table, shadow mask in the env map against snapshots (exact) |
| `check_p6_transform.py`, `check_p7_transform.py` | vertex transformation, env / bump coordinates, depth (exact for every vertex) |
| `check_p6_probe.py`, `check_p7_probe.py` | filler texel addressing against 3000 probed pixels each (all equal) |
| `check_p7_lightmap.py`, `p7_overflow.py` | the p7 light map (exact) and its maximum over the part |
| `raster6.py`, `raster7.py` | approximate rasteriser from dumped engine buffers vs the drawn frame buffer |
| `maps.py`, `shadow_fit*.py`, `spanwidth.py` | bump maps as gradients, spot profile, mask geometry tries, widest spans |
| `export_pictures.py` | writes the PNGs of section 5 |
