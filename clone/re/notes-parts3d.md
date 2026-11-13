# Scope "parts3d": parts 7, 8, 9, 10 (the 3D parts)

Part 7 0x416fce (deform 0x416e37, set-up 0x416cca), part 8 0x417929 (deform 0x4176d0, set-up 0x417563), part 9
0x4185e5 (field 0x418094, voxels 0x418445, cube 0x417c48, buffers 0x418402), part 10 0x41958e (field 0x418fd2,
voxels 0x4193b6, cube 0x418b86, buffers 0x419373). Addresses are linear (NOTES.md). Positions are order:row in
hex. Time is t70 = timer / 17043 (one unit = 17043 PIT clocks = 1/70.01 s, notes-core.md 5.1; `full.flips`
column 5 uses 70 / 1,193,182 instead, 0.0144 % off), dt = the per-frame difference (float [0x420489]).

What goes into the engine (0x4050e9 transform, 0x416b61 vertex normals, 0x416929 cull + sort, 0x4169f8 draw, the
texture mapper and fillers) is described by its inputs and outputs; the engine's internals are notes-engine.md.

Checked against the emulator (scripts in `models/parts3d/`, section 9): over every frame of all four parts
(1282 / 839 / 1550 / 1181 frames, runs resumed from the part snapshots with per-frame dumps), the model gives

- the state variables (angles, phases, fades, Z, light) to float rounding (relative 1e-7, Z to 0.001);
- the matrices (S · Rz · Ry · Rx, below) to 2e-7;
- the vertices handed to the engine: parts 7 / 8 to 0.016 / 0.003 (coordinates up to 3100), parts 9 / 10 the same
  number of cubes, vertices to 1.5e-5, the same faces (vertex indices), vertex shades to 40 / 65536 of a level,
  texture coordinates to 2.4e-7;
- the metaball grids to 0.001 (part 10 including the aliased cells);
- the tile band: every tile whose dominant colour could be checked (263,359 tiles in 4,852 frames) equals the
  model exactly;
- the closed forms (rate × (t70 − origin)) of every linear variable to float accumulation (≤ 3·10⁻⁵ relative),
  the clamped ones (fades, Z) to 0.04, part 9's piecewise angles to 1e-4.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | routine | music | ticks | frames (capture fps) | on screen |
|---|---|---|---|---|---|
| 8 | 0x417929 | 10:00..14:18 | 3084..3924 | 840 (54.0) | the geosphere, deformed into a blob with map B, flies in from z = 9400 to 1400 in 1.52 s while it brightens, turning; a band of 4 × 8 coloured tiles (rows 52..147) appears two columns at a time (801 at 10:04, 10:08, 10:0c, 10:10) and shimmers; from 14:00 (803) the tiles fade out and the blob darkens in three steps |
| 9 | 0x4185e5 | 18:00..20:00 | 4621..6155 | 1550 (54.6) | three metaballs on a 6 × 6 × 6 voxel grid drawn as cubes with map C, grown from nothing from 18:08 (901), turning (the turning axis changes at every 902), cubes shaded by a 3D sine field; band of 2 × 8 tiles (rows 76..123) |
| 7 | 0x416fce | 24:00..29:00 | 6924..7883 | 1282 (72.2) | the geosphere, deformed (larger bumps than part 8) with map A, turning, at the right; a 6 × 5 block of tiles (x 40..239, rows 28..171) at the left; both fade in over 3.2 s; 28:00 (701) freezes the motion, 28:08 the tiles go black, 28:10 the blob goes: black to the end |
| 10 | 0x41958e | 36:00..3a:00 | 10380..11147 | 1181 (83.1) | three metaballs on a 4 × 10 × 4 voxel grid (with aliased cells, 5.4) as cubes with map B, turning slowly; band of 4 × 8 tiles (rows 76..171); 39:00..39:3e the light pulses 1.2 / 1.0 every 2 rows (1011 / 1012) |

All four draw the same way each frame: integrate the state by dt, build a matrix, transform the object, deform it
(7, 8) or build it (9, 10), let the engine cull and sort the faces, fill the tile band (and black bars), then draw
the object over it.

## 2. Shared pieces

### 2.1 Object records (as the parts fill them)

Vertex: 0x34 bytes, float x, y, z at +0, +4, +8 (the rest is the engine's). Face: 0x48 bytes:

| offset | contents |
|---|---|
| +0x00 | engine: next face in the sort bucket |
| +0x04, +0x08, +0x0c | pointers to the three (transformed) vertices |
| +0x10, +0x14, +0x18 | pointers to the three vertex normals (parts 7, 8); 0 = no normals (parts 9, 10: +0x10 = 0) |
| +0x1c..+0x30 | u0, v0, u1, v1, u2, v2 (float; the engine multiplies them by 65536 · 256: 16.16 texels, 256 texels per unit) |
| +0x34 | engine: back-face flag |
| +0x38 | texture pointer (257 × 256 × 32-bit map) |
| +0x3c, +0x40, +0x44 | parts 9, 10: per-vertex shade, 16.16 fixed point (used when +0x10 = 0) |

### 2.2 Matrix

All four build `M = I; M = Rx(a) M; M = Ry(b) M; M = Rz(c) M; M = diag(1, sy, 1) M; [M.t += (0, 0, tz)]` with
0x404c9f identity, 0x404f0f Rx, 0x404e36 Ry, 0x404d5c Rz (each left-multiplies), 0x404cd6 scales the rows by
(1, sy, 1), 0x404feb adds to the translation column. The rotations are the usual right-handed ones
(Rx = [[1,0,0],[0,c,−s],[0,s,c]], Ry = [[c,0,s],[0,1,0],[−s,0,c]], Rz = [[c,−s,0],[s,c,0],[0,0,1]]); checked
against the dumped matrices. 0x4050e9 then computes v' = M[:, :3] v + M[:, 3] for every vertex. So a vertex is
rotated about x, then y, then z, then its y is multiplied by sy (projection aspect), then moved along z.

| part | a | b | c | sy | tz |
|---|---|---|---|---|---|
| 7 | A7 | 1.1 A7 | 0.8 A7 | 4/3 (0x3faaaaab) | 0 (z += 2000 in the deform) |
| 8 | A8 | 1.1 A8 | 0.8 A8 | 40/27 = 1.4814814 (0x3fbda12f) | 0 (z += Z in the deform) |
| 9 | A0 + A1 − A2 | 1.1 (A0 − A1 + A2) | 0.8 (A1 − A0 + A2) | 4/3 | 180 |
| 10 | A10 | 1.1 A10 | 0.8 A10 | 4/3 | 150 |

(1.1, 0.8 are doubles; the products are passed as floats.)

### 2.3 The light [0x427f70]

A float the engine multiplies every vertex shade by (0x416ae3 / 0x416b1f): with normals (parts 7, 8) the shade is
(1 − n_z)³ · 2437939.2 · light, without (9, 10) the face's +0x3c.. value · light (notes-engine.md). Initial 1.0
(image). Part 7 writes it every frame (fade-in, 3.2), part 8 while flying in (3.3); event 7 sets 1.0, event 9 1.2,
event 803 subtracts 0.25, events 1011 / 1012 set 1.2 / 1.0. Value per part: part 8 1400 / Z, then 0.75, 0.5, 0.25
(803); part 9 1.2 throughout; part 7 min(2 fade, 1); part 10 1.0 (left by part 7), pulses 1.2 / 1.0 at 39:00..39:3e,
ends at 1.0.

### 2.4 The tile band (background)

Static table 0x428164: 64 × 3 dwords (R, G, B, 0..100: an 8 × 8 mosaic from yellow-orange at the top left to dark
violet at the bottom right; part 11 uses it too, notes-gfx.md 7.5). Each part runs the same loop over an 8 × 8 grid
of 40 × 24 rectangles that covers rows 4..195 completely:

```c
idx = START;                                   // 0 (parts 7, 9) or 24 (parts 8, 10): dword index into 0x428164
for (row = -1; row <= 6; row++)                // y0 = 24 row + 28: 4, 28, ..., 172
  for (col = 0; col <= 7; col++) {             // x0 = 40 col
    r = trunc(T[idx]   * 0.0125f * 255 * F);   // F = part's factor below, float 0.0125f = 0.012500000186
    g = trunc(T[idx+1] * 0.0125f * 255 * F);   // 80 % = 255; values over 80 clamp
    b = trunc(T[idx+2] * 0.0125f * 255 * F);
    colour = min(r,255) << 16 | min(g,255) << 8 | min(b,255);
    if (!VISIBLE(row, col)) colour = 0;
    fill_rect(target + (y0 * 320 + x0) * 4, colour, 40, 24);    // 0x4043be(dest, colour, w, h)
    idx += 3;
  }
```

| part | START | visible tiles | table rows shown | F |
|---|---|---|---|---|
| 7 | 0 | rows 0..5, cols 1..5 (x 40..239, y 28..171), while [0x427f74] < 2 | 1..6, cols 1..5 | fade [0x427f80] (3.1) |
| 8 | 24 | rows 1..4 (y 52..147), cols < [0x427ff0] (0, 2, 4, 6, 8 by event 801) | 3..6 | fade [0x427fec] · w (4.1) |
| 9 | 0 | rows 2..3 (y 76..123), all columns | 3..4 | 1 |
| 10 | 24 | rows 2..5 (y 76..171), all columns | 4..7 | 1 |

(Table row of a tile = row + 1 + START / 24.) The invisible tiles are filled black. With START = 24 the loop reads
past the table for the last row (index up to 215: 0x428464 = 1.0f, 0x428468 = part 11's flag, then the module);
those tiles are always black, so it does not matter.

Then parts 7 and 10 clear rows 0..3 (two fills, 256 × 4 at x = 0 and 64 × 4 at x = 256), part 7 also rows
196..199 (same at 0x3d400 = row 196).

### 2.5 What the parts overwrite

- Part 7: every pixel (rows 0..3 and 196..199 black, rows 4..195 tiles), then the object, which may cover rows
  0..198 (the y window set by event 7, 2.6).
- Part 10: rows 0..195; rows 196..199 are not written. Parts 8 and 9: rows 4..195; rows 0..3 and 196..199 are not
  written. Their objects stay inside rows 20..179 (measured 20..178). In the 32-bit mode those rows show what the
  page held four frames earlier: in the capture always black (the preceding parts 2, 6 and 4 leave rows 196..199
  black, and part 2 and part 6 also rows 0..3; `borders.py`). In the 15-bit mode they are black. **Remake: black.**

### 2.6 Projection window (events 7 and 10)

The precalc calls 0x414f04(−0.625, −0.3125) and event 10 calls it again; event 7 calls 0x414f04(−0.625,
−0.390625). 0x414f04 only stores the two floats in [0x422534], [0x422538] (the engine's x / y view limits,
notes-engine.md). Observed: parts 8, 9, 10 draw nothing outside rows 20..179, part 7 uses the full height. So the
blob of part 7 is cut by the screen edges, the objects of 8, 9 and 10 by a window 160 rows high.

### 2.7 Engine calls

| call | in | out |
|---|---|---|
| 0x4050e9(EAX = M, EDX = src, EBX = dst, ECX = n) | 3 × 4 matrix, n vertices (0x34 stride) | dst = M src |
| 0x416b61(EAX = normals, EDX = faces, EBX = nv, ECX = nf) | deformed vertices via the faces | per-vertex normals: zeroed, every face's normal added to its three vertices, normalised (parts 7, 8) |
| 0x416929(EAX = faces, EDX = nf) | face vertices (camera space, eye at 0) | back faces flagged (+0x34); front faces linked into 4096 buckets by trunc(0.2 (z0 + z1 + z2)), negative → 0, **≥ 4096 dropped** |
| 0x4169f8(EAX = target) | the buckets, far to near | faces drawn (texture, shade · light), buckets emptied |

The bucket limit drops every face whose mean z is ≥ 6826.7. This is visible in part 8 (4.1): the blob is not
drawn until its front comes nearer than that (first object pixels at Z = 7569, the nearest vertex at z = 6820).

### 2.8 Time base: the start of a part and the event frames

Every animated variable is advanced once per frame by rate · dt (sections 3..6). Frame n is drawn with dt_n =
t70_n − t70_{n−1}, where t70_n is the timer value read after frame n−1 was shown (`full.flips` column 4 / 17043).
So a variable that starts at 0 in the part's first frame n0 equals rate · (t70 − origin) with **origin =
t70_{n0−1}, the timer of the previous part's last frame**, which lies between 0 and one frame (of the previous
part) before the music time of the part's event. In the capture:

| part | event (music time, t70) | origin (t70) | first frame drawn with | dt of the first frame |
|---|---|---|---|---|
| 8 | 10:00, ≈ 3982.3 | 3981.1581 | 3982.8924 | 1.734 (part 2's frame interval) |
| 9 | 18:00, ≈ 5973.97 | 5973.9528 | 5975.9861 | 2.033 (part 6) |
| 7 | 24:00, ≈ 8961.6 | 8960.4406 | 8962.1224 | 1.682 (part 5) |
| 10 | 36:00, ≈ 13443.0 | 13442.0276 | 13443.8145 | 1.787 (part 4) |

(Music times of positions from the player timer at the tick the row starts; the music scope has the exact map.)
Checked: value − rate · (t70 − origin) stays within float accumulation (part 9's blob time −0.011 at 398) over the
whole part, for every linear variable.

Events are handled at the start of a frame, before the part draws, at the first frame whose position check sees
the new row. A variable whose rate an event switches therefore switches at **t70_{n−1} of that frame**, 0..1
frame before the event's music time: the frame that spans the event is integrated entirely with the new rule.
This decides part 9's angles (5.3) and the start of the fades of 803 and 901; in parts 7, 8, 10 the shift is at
most one frame (≤ 1.3 t70 units) and invisible.

---------------------------------------------------------------------------------------------------------------

## 3. Part 7: 0x416fce (set-up 0x416cca, deform 0x416e37)

### 3.1 Set-up 0x416cca (precalc)

```c
geosphere(4);                       // 0x4174c4 (engine): octahedron 0x427f9c / 0x427f84 subdivided 4 times
nf = [0xf72d48] = 2048;  nv = [0xf72d44] = 1026;    // vertices int16 x,y,z at 0xf61404 (12-byte records),
                                                    // faces 3 int16 indices at 0xf6cf84 (6-byte records)
[0xf613e0] = nf; [0xf613e4] = nv;
orig  = [0xf613f0] = alloc(nv * 0x34);   // 0x622adc  float copy of the int16 vertices
xform = [0xf613e8] = alloc(nv * 0x34);   // 0x62fb48  transformed + deformed
norm  = [0xf613ec] = alloc(nv * 0x34);   // 0x63cbb4  vertex normals
faces = [0xf613dc] = alloc(nf * 0x48);   // 0x649c20
for i: orig[i] = (float)int16 vertex i
for f: a, b, c = indices of face f
  face.v = &xform[a], &xform[b], &xform[c];  face.n = &norm[a], &norm[b], &norm[c]
  face.uv = 0.004 * (orig[a].x, orig[a].y,  orig[b].x, orig[b].y,  orig[c].x, orig[c].y)   // 0.004 double
  face.tex = [0xe35108]                      // map A
```

The geosphere has its vertices on a sphere of radius 300 (int16, so 298.5..300.5 after rounding). The texture is a
planar projection along z of the undeformed sphere: u = x / 250, v = y / 250 (−1.2..1.2, i.e. −307..307 texels:
2.4 repeats of the 256-texel map across the diameter), the same picture on the front and the back half and
stretched towards the rim. The coordinates stay with the vertices (they turn and deform with the object).

### 3.2 Per frame

```c
if ([0x427f74] < 1) A7 += dt * 0.0075;                          // [0xf61400], 0 at the start
if ((int)fade < 0x3f800000) {                                    // fade [0x427f80], 0 in the image
  fade += dt * 0.0045;  if (fade > 1) fade = 1;                  // (both compares on the float bits as ints)
}
[0x427f70] = fade >= 0.5 ? 1.0 : fade * 2;                       // light
M = matrix(A7, 1.1 A7, 0.8 A7, sy = 4/3)                         // 2.2
xform = M orig                                                   // 0x4050e9
deform7()                                                        // 0x416e37, 3.3
vertex_normals(norm, faces, nv, nf)                              // 0x416b61
if ([0x427f74] < 3) cull_sort(faces, nf)                         // 0x416929
tiles: START 0, visible rows 0..5, cols 1..5 if [0x427f74] < 2, F = fade        // 2.4
black: rows 0..3, rows 196..199
if ([0x427f74] < 3) draw(target)                                 // 0x4169f8
```

### 3.3 The deformation 0x416e37

```c
if ([0x427f74] < 1) P += dt * 0.03f;                // P = [0xf613f4], 0 at the start; 0.03f = [0x427f78]
P8 = (float)(P * 0.8); P13 = (float)(P * 1.3);      // [0xf613f8], [0xf613fc]
for each vertex i:
  ix, iy, iz = (int16)trunc(orig[i].x / .y / .z)    // exact: the vertices are integers
  s = 0.5 sin(ix / 112 + P) + 2/3                   // K = 112.0f [0x427f7c]
    + 0.5 sin(iy / 112 + P8) + 2/3
    + 0.5 sin(iz / 112 + P13) + 2/3;                // = 2 + (sum of the three sines) / 2: 0.5..3.5
  s = (float)s;
  if (s > 2) { k = (float)(1 + 3 (s - 2)); if (k > 1.5) k = 1.5; s = (float)(s * k); }
  xform[i].x *= s;  xform[i].y *= s;  xform[i].z = xform[i].z * s + 2000;
```

The scale is a function of the vertex's position on the undeformed sphere, so the bumps stay on the surface while
it turns; the waves travel because of P. Radially the blob is 0.5..2 times 300 where the sum is ≤ 2 and is
pushed out to s · min(1 + 3 (s − 2), 1.5) (up to 5.25) where it is larger: the bumps rise steeply (the factor
reaches its clamp at s = 2.167, a crease in the slope). Then it is moved 2000 along z (after the rotation).

### 3.4 Events

| event | position | effect |
|---|---|---|
| 7 | 24:00 | part = 7, light = 1.0 (overwritten in the first frame), window 0x414f04(−0.625, −0.390625) (2.6) |
| 701 | 28:00 | [0x427f74] = 1: A7 and P stop (the blob freezes; the fade and the drawing go on) |
| 701 | 28:08 | = 2: all tiles black |
| 701 | 28:10 | = 3: no sort, no draw: the frame is black (checked: 632 black frames to the end of the part) |

In the capture the 701 events are seen at the frames drawn with t70 9957.671, 9989.984, 10020.824; the motion
stops at t70 9956.198 (A7 = 7.4680, P = 29.872).

### 3.5 Time base

| variable | recurrence | closed form (T = t70 − 8960.4406 in the capture) | kind |
|---|---|---|---|
| A7 [0xf61400] | += 0.0075 dt while [0x427f74] < 1 | 0.0075 T, frozen at the 28:00 frame | linear, event stop |
| P [0xf613f4] | += 0.03 dt while [0x427f74] < 1 | 0.03 T, frozen | linear, event stop |
| fade [0x427f80] | += 0.0045 dt, clamped at 1 | min(1, 0.0045 T): 1 at T = 222.2 (3.17 s) | linear + clamp |
| light | = min(2 fade, 1) | min(0.009 T, 1): 1 at T = 111.1 (1.59 s) | |
| tile factor F | = fade | | |

Nothing depends on the frame rate except where the freeze falls (≤ one frame).

## 4. Part 8: 0x417929 (set-up 0x417563, deform 0x4176d0)

### 4.1 Set-up and per frame

0x417563 is 0x416cca with other variables: geosphere(4) again into the same static buffers, [0xf72d50] = nf,
[0xf72d54] = nv, orig [0xf72d5c] (0x66dc24), xform [0xf72d58] (0x67ac90), normals [0xf72d60] (0x687cfc), faces
[0xf72d4c] (0x694d68); uv = 0.004 (x, y) as in 3.1; texture [0xe3510c] (map B).

```c
tt += dt * 0.05;                                   // tile time [0xf72d70], 0 at the start
if (Z < 1400) {                                    // Z = [0x427fe4], 9400 in the image
  fade -= dt * 0.011;  if (fade < 0) fade = 0;     // tile fade [0x427fec], 1.0 in the image
}
if (Z > 1400) { Z -= dt * 75; if (Z < 1400) Z = 1400; }
if (Z >= 1400) [0x427f70] = 1400 / Z;              // light (not written once 803 made Z < 1400)
A8 += dt * 0.0075;                                 // [0xf72d6c]
M = matrix(A8, 1.1 A8, 0.8 A8, sy = 40/27)
xform = M orig
deform8()                                          // 4.2
vertex_normals(...); cull_sort(faces, nf)          // always
tiles: START 24, visible rows 1..4, cols < [0x427ff0], F = fade * w   (no black bars)
draw(target)
```

The tile factor w is per tile, with idx the tile's dword index (24 + 3 · tile number, tile number = 8 (row + 1) +
col) and R = T[idx] its red value:

```c
w = (float)(sin(R * tt * 0.01f + idx * 0.2) * 0.1 + 0.9);  if (w > 1) w = 1;   // 0.8..1.0; the clamp never acts
```

so every tile pulses between 80 and 100 % at its own speed (R · 0.0005 rad per t70 unit: 0.8..3.5 rad/s) and phase.

### 4.2 The deformation 0x4176d0

```c
if ([0x427fe8] != 0) { [0x427fe8] -= dt * 0.03; if (< 0) = 0; }   // event 802: not in the sync table, stays 0
P += dt * 0.03f;                                    // [0x427ffc] (speed 0.03f [0x427ff4]); 0 at the start
P8 = (float)(P * 0.8); P13 = (float)(P * 1.3);      // [0xf72d64], [0xf72d68]
for each vertex i:
  ix, iy, iz = (int16)trunc(orig[i])
  s = (float)(2 + 0.5 (sin(ix / K + P) + sin(iy / K + P8) + sin(iz / K + P13)));  // K = 69.115036f = 22 pi [0x427ff8]
  if (s < 2) s = 2;
  else if (s > 3) { k = min((float)((s - 3) * 2.4), 1.5); s = k * [0x427fe8] + 2.5; if (s > 3.5) s = 3.5; }
  else if (s > 2) { k = min((float)(1 + (s - 2) * 0.1), 1.5); s = (float)(s * k); if (s > 2.5) s = 2.5; }
  xform[i].x *= s;  xform[i].y *= s;  xform[i].z = xform[i].z * s + Z;
```

With [0x427fe8] = 0 the scale is 2 where the sum is ≤ 2, s (1 + 0.1 (s − 2)) for 2 < s ≤ 2.41, and 2.5 above:
a sphere of radius 600 with plateau-topped bumps up to 750 (the "lumpy blob" of PARTS.md), with shorter waves
than part 7 (K = 69.1 against 112).

### 4.3 Events and what they do

| event | positions | effect |
|---|---|---|
| 8 | 10:00 | part = 8 (the light and Z keep their image values 1.0 / 9400 until the first frame) |
| 801 | 10:04, 10:08, 10:0c, 10:10 | [0x427ff0] += 2: 2, 4, 6, 8 tile columns visible, from the left |
| 803 | 14:00, 14:08, 14:10, 14:18 | Z −= 1 (1399, 1398, 1397: the tile fade starts; the blob moves 1 unit nearer, invisible), light −= 0.25 (0.75, 0.5, 0.25; the fourth, 0.0, comes in the frame that already switches to part 6) |
| 802 | none | would set [0x427fe8] = 1: bumps with s > 3 would rise to 2.5 + 1.5 for 0.47 s |

### 4.4 Time base

| variable | closed form (T = t70 − 3981.1581) | kind |
|---|---|---|
| tt [0xf72d70] | 0.05 T | linear |
| A8 [0xf72d6c] | 0.0075 T | linear |
| P [0x427ffc] | 0.03 T | linear |
| Z [0x427fe4] | max(1400, 9400 − 75 T): 1400 at T = 106.67 (1.52 s, t70 4087.825); 803: 1400 − n803 | linear + clamp |
| light | 1400 / Z (0.149 → 1), then 0.75 / 0.5 / 0.25 from the 803 frames | |
| tile fade [0x427fec] | 1 until the first 803 frame, then max(0, 1 − 0.011 (t70 − t803)), t803 = 4977.809 in the capture (the frame before the 14:00 frame): 0 after 90.9 units (1.3 s), just at the part's end | linear + clamp, event start |
| columns [0x427ff0] | event count × 2 | event |

The blob is not drawn while its nearest faces have a mean z ≥ 6826.7 (2.7): in the capture its first pixels appear
at T = 24.4 (Z = 7569, 0.35 s), front faces first; before that the screen is black (plus the tiles from 10:04).
Nothing depends on the frame rate (PARTS.md: equal pictures at equal timer values).

## 5. Part 9: 0x4185e5 (buffers 0x418402, field 0x418094, voxels 0x418445, cube 0x417c48)

### 5.1 Buffers 0x418402 (precalc)

src [0xf73594] (0x6b8d6c), dst [0xf73580] (0x6eb9f0), [0xf73598] (0x71e674, never used): 0x32c80 bytes each (4000
vertices); faces [0xf73584] (0x7512f8): 0x8ca00 bytes (8000 faces). Counts: faces [0x428018], vertices
[0x42801c].

### 5.2 Per frame

```c
k = [0x428140];                                    // 0, 1, 2 (event 902)
A[k] += dt * 0.003 * (k + 1);                      // A0, A1, A2 = [0xf73574], [0xf73578], [0xf7357c], 0 at start
if ((int)fade < 0x3f800000 && [0x428014] != 0) {   // fade [0x428010]; [0x428014] = 1.0 from event 901
  fade += dt * 0.005;  if (fade > 1) fade = 1;
}
C0 += dt * 0.02;  C1 += dt * 0.03;  C2 += dt * 0.04;   // colour phases [0xf73588], [0xf7358c], [0xf73590]
E9 += dt * 0.2;                                    // blob time [0xf735a0]
field9(E9);                                        // 0x418094 -> grid G
voxels9();                                         // 0x418445 -> vertices, faces
M = matrix(A0 + A1 - A2, 1.1 (A0 - A1 + A2), 0.8 (A1 - A0 + A2), sy = 4/3, tz = 180.0)
dst = M src                                        // [0x42801c] vertices
cull_sort(faces, [0x428018])                       // no normals: flat-path shades (2.3)
tiles: START 0, visible rows 2..3, F = 1           // no black bars
draw(target)
```

### 5.3 The rotation and event 902

Only one of the three angles moves at a time: 0.003 per unit (A0, k = 0), 0.006 (A1) or 0.009 (A2). 902 fires at
rows 00, 08, 10 of orders 19..1f (21 times) and sets k = 1, 2, 0 in turn, so in every order from 19 on A1 turns
for 8 rows (31.1 units), A2 for 8 rows, A0 for the remaining 48; 18:00..19:00 is A0 only. The angles are piecewise
linear in time; the frame that sees a 902 integrates its whole dt with the new k (2.8), so the switch happens at the
timer of the previous frame, 0.07..2.6 units (mean 0.99) before the row. In the capture (t70 of the switch = the
timer of the frame before the event frame; `timebase.py`):

| new k | position | row start | switch | | new k | position | row start | switch |
|---|---|---|---|---|---|---|---|---|
| 1 | 19:00 | 6222.960 | 6221.136 | | 1 | 1c:00 | 6969.870 | 6968.555 |
| 2 | 19:08 | 6254.081 | 6252.660 | | 2 | 1c:08 | 7000.992 | 7000.307 |
| 0 | 19:10 | 6285.202 | 6284.763 | | 0 | 1c:10 | 7032.113 | 7031.674 |
| 1 | 1a:00 | 6471.930 | 6470.633 | | 1 | 1d:00 | 7218.841 | 7218.121 |
| 2 | 1a:08 | 6503.051 | 6502.980 | | 2 | 1d:08 | 7249.962 | 7249.855 |
| 0 | 1a:10 | 6534.172 | 6532.472 | | 0 | 1d:10 | 7281.083 | 7279.050 |
| 1 | 1b:00 | 6720.900 | 6720.286 | | 1 | 1e:00 | 7467.811 | 7466.759 |
| 2 | 1b:08 | 6752.021 | 6751.827 | | 2 | 1e:08 | 7498.932 | 7498.406 |
| 0 | 1b:10 | 6783.143 | 6782.598 | | 0 | 1e:10 | 7530.053 | 7529.124 |
| | | | | | 1 | 1f:00 | 7716.781 | 7715.729 |
| | | | | | 2 | 1f:08 | 7747.902 | 7745.307 |
| | | | | | 0 | 1f:10 | 7779.024 | 7777.902 |

A_k(t) = 0.003 (k + 1) · (time spent with index k between the origin 5973.9528 and t), with the switches above:
matches the emulator's angles to 1e-4 over the whole part. **This is part 9's frame-rate dependence** (PARTS.md:
"mostly timer, some frames differ"): switching at the row starts instead gives a rotation that differs by up to
5.1° (2.4° at the end of the part). A remake that wants the capture's poses uses the measured switch times above
(values measured from a capture); one that switches at the row starts is up to 5° off at times.

### 5.4 The field 0x418094 (argument E9, float)

```c
// nine centre coordinates, each 8 * (2.5 sin(...) + 4): 12..52 (2.5 = float [0x428000])
c0 = 8 (2.5 sin(E/23) + 4)        // E/23: E * 0.0434783f (1/23 as float); same for 1/33, 1/43, 1/30, 1/24
c1 = 8 (2.5 sin(E/33 + 1) + 4)
c2 = 8 (2.5 sin(E/43 + 2) + 4)
c3 = 8 (2.5 sin(E/30 + 3) + 4)
c4 = 8 (2.5 sin(E/43 + 4) + 4)    // E/43, E/30, E/24 rounded to float when reused (c4..c8)
c5 = 8 (2.5 sin(E/24 + 5) + 4)
c6 = 8 (2.5 sin(E/30 + 6) + 4)
c7 = 8 (2.5 sin(E/43 + 7) + 4)
c8 = 8 (2.5 sin(E/24 + 8) + 4)
// blob 0 at (x, y, z) = (c2, c1, c0), blob 1 at (c5, c4, c3), blob 2 at (c8, c7, c6); radii R0, R1, R2 =
// ints [0x428004], [0x428008], [0x42800c] = 35, 35, 35
for (x = 1..6) for (y = 1..6) for (z = 1..6) {    // loop order: x outer, z inner
  X = 8x, Y = 8y, Z = 8z;
  d_b = (X - bx)^2 + (Y - by)^2 + (Z - bz)^2       // floats
  v = -135;
  if (R0^2 > d0) v = 270 W(d0 / R0^2) - 135;
  if (R1^2 > d1) v += 270 W(d1 / R1^2);
  if (R1^2 > d2) v += 270 W(d2 / R2^2);           // reach tested with R1, falloff with R2 (all 35 here)
  G[z][y][x] = v * fade;                           // float G[8][8][8] at 0xf72d74; border cells stay 0
}
W(a) = ((-4/9 a + 17/9) a - 22/9) a + 1 = (1 - a)^2 (1 - 4a/9)     // Wyvill's soft-object falloff, doubles
```

So the field is −135 + 270 Σ W (radius 35 grid units = 4.4 cells), times the fade. The blobs move on Lissajous
paths inside the grid (centres 12..52 on a grid of 8..48).

### 5.5 Voxels 0x418445 and cubes 0x417c48

```c
nf = nv = 0;
for (z = 1..6) for (y = 1..6) for (x = 1..6) {    // z outer, x inner
  size = (float)(G[z][y][x] * (1/6.0f));
  if (!(size > 0.5)) continue;
  mask = 0x3f;
  if (size >= 15) {                                // a full cube: hide faces shared with full neighbours
    size = 15;
    if (G[z-1][y][x] >= 90) mask &= ~0x01;         // compares of the float bits with 0x42b40000 (= 90.0)
    if (G[z+1][y][x] >= 90) mask &= ~0x02;
    if (G[z][y-1][x] >= 90) mask &= ~0x04;
    if (G[z][y+1][x] >= 90) mask &= ~0x08;
    if (G[z][y][x-1] >= 90) mask &= ~0x20;
    if (G[z][y][x+2] >= 90) mask &= ~0x10;         // sic: x+2 (address +8), not x+1; x = 6 reads G[z][y+1][0] = 0
  }
  if (mask) cube(30 (x - 4), 30 (y - 4), 30 (z - 4), mask, size);
}
```

`cube(x, y, z, mask, s)` (0x417c48; part 10's 0x418b86 is the same code with other buffers, uv scale 0.009 and
texture [0xe35114] = map B):

```c
n = nv;                                            // 8 vertices, always (also counted when no face is drawn)
src[n+0..7] = (x-s,y-s,z-s) (x+s,y-s,z-s) (x+s,y+s,z-s) (x-s,y+s,z-s)
              (x-s,y-s,z+s) (x+s,y-s,z+s) (x+s,y+s,z+s) (x-s,y+s,z+s)
for (j = 0..11) {                                  // table 0x428020: {a, b, c, bit}, 16 bytes each
  if (!(mask & bit[j])) continue;
  F = faces[nf++];  F.v = &dst[n+a], &dst[n+b], &dst[n+c];  F.n = 0;  F.tex = [0xe35110] (map C)
  for each corner i of (a, b, c):
    p = src[n+i];
    F.shade_i = trunc(2031616 (sin(0.035 p.x + C0) + sin(0.035 p.y + C1) + sin(0.035 p.z + C2)) + 12582912);
    // = 65536 * (192 + 31 * (sum of three sines)): levels 99..285 in 16.16
    q = (x, y, z) + CORNER[i];                     // table 0x4280e0: 8 x float (+-15, +-15, +-15), same order
    F.u_i = 0.003 q.x + 0.003 q.z;   F.v_i = 0.003 q.y - 0.003 q.z;     // 0.003 double (part 10: 0.009)
}
nv += 8;
```

Face table 0x428020 (corner indices, mask bit): (0,1,2) (0,2,3) bit 1 = the z − s side; (2,1,5) (2,5,6) 0x10 =
x + s; (1,0,4) (1,4,5) 4 = y − s; (0,3,7) (0,7,4) 0x20 = x − s; (3,2,6) (3,6,7) 8 = y + s; (7,6,5) (7,5,4) 2 = z + s.

So:
- **Geometry**: cubes of half-size size = field/6 (only drawn above 0.5, full at field ≥ 90: half-size 15 =
  touching neighbours) at the cell centres 30 (i − 4): −90..60, i.e. the block's centre is (−15, −15, −15) and the
  object turns about a point off its middle. Cubes grow and shrink with the field: small separate cubes at the
  edges of the blobs, solid blocks inside (PARTS.md's "boxes / columns").
- **Texture**: one planar projection for all cubes, u = 0.003 (X + Z), v = 0.003 (Y − Z), at the corners of the
  full-size cell (± 15), not at the shrunk corners: every cube shows the part of the texture its full cell would,
  squeezed onto its smaller faces. 30 units = 0.09 · 256 = 23 texels.
- **Shade**: a 3D sine field 192 + 31 Σ sin(0.035 p + C) at the actual (shrunk) corners, drifting with the colour
  phases, Gouraud-interpolated by the engine and multiplied by the light 1.2. It can exceed 255 (white areas in the
  capture; the engine decides how it saturates).
- **The x + 2 test** hides the +x face of a full cube when the cell two steps away is full although the next
  one is not (35 times in the whole part: a missing face for a frame), and keeps the +x face when the next cell is
  full but the one after is not (13,547 times): such a face lies inside the solid, between two cubes, and is only
  covered by the engine's sorting. Guess: these interior faces are the faint lines visible inside large blocks in
  some frames.

### 5.6 Events

| event | position | effect |
|---|---|---|
| 9 | 18:00 | part = 9, light = 1.2 |
| 901 | 18:08 | [0x428014] = 1.0: the fade starts (in the capture at t70 6004.987, the frame before the event frame); before it the field is 0 and only the tiles show |
| 902 | 19:00 .. 1f:10 (21) | k = (k + 1) mod 3 (5.3) |

### 5.7 Time base

| variable | closed form (T = t70 − 5973.9528) | kind |
|---|---|---|
| A0, A1, A2 | piecewise linear, 5.3 | event-switched: frame-rate dependent at the switches |
| fade [0x428010] | min(1, 0.005 (t70 − t901)), t901 = 6004.987: full after 200 units (2.86 s) | linear + clamp, event start |
| C0, C1, C2 | 0.02 T, 0.03 T, 0.04 T (39.81, 59.72, 79.62 at the end; **part 10 continues them**) | linear |
| E9 [0xf735a0] | 0.2 T (398.1 at the end) | linear |

## 6. Part 10: 0x41958e (buffers 0x419373, field 0x418fd2, voxels 0x4193b6, cube 0x418b86)

### 6.1 Buffers 0x419373

src [0xf7391c] (0x7ddcfc), dst [0xf73904] (0x810980), [0xf73918] (0x843604, unused): 0x32c80 bytes; faces
[0xf73908] (0x876288): 0x8ca00. Counts: faces [0x428154], vertices [0x428158].

### 6.2 Per frame

```c
A10 += dt * 0.002;                                 // [0xf73920]
C0 += dt * 0.02; C1 += dt * 0.03; C2 += dt * 0.04; // the same colour phases as part 9, continued
E10 += dt * 0.3;                                   // blob time [0xf73924], 0 at the start
[0xf73914] += dt * 0.0232 * 2;  [0xf73910] += dt * 0.0153 * 2;  [0xf7390c] += dt * 0.0274 * 2;   // never read
field10(E10);                                      // 0x418fd2 -> g
voxels10();                                        // 0x4193b6 -> vertices, faces
M = matrix(A10, 1.1 A10, 0.8 A10, sy = 4/3, tz = 150.0)
dst = M src;  cull_sort(faces, [0x428154])
tiles: START 24, visible rows 2..5, F = 1;  black rows 0..3
draw(target)
```

### 6.3 The field 0x418fd2 (argument E10)

Centres (2.3 = float [0x428144], radii [0x428148], [0x42814c], [0x428150] = 35, 35, 35):

```c
c0 = 8 (2.3 sin(E/23) + 3)          c3 = 8 (2.3 sin(E/30 + 3) + 3)      c6 = 8 (2.3 sin(E/30 + 6) + 3)
c1 = 8 (4.6 sin(E/33 + 1) + 6)      c4 = 8 (4.6 sin(E/43 + 4) + 6)      c7 = 8 (4.6 sin(E/43 + 7) + 6)
c2 = 8 (2.3 sin(E/43 + 2) + 3)      c5 = 8 (2.3 sin(E/24 + 5) + 3)      c8 = 8 (2.3 sin(E/24 + 8) + 3)
// (4.6 = 2.3f * 2.0f; same float rounding of E/43, E/30, E/24 as in part 9)
// grid coordinates: A = 8a (a = 1..4), B = 8b (b = 1..10), Ee = 8e (e = 1..4)
// blob j at (A, B, Ee) = (c[3j], c[3j+1], c[3j+2]): 5.6..42.4 in A and Ee, 11.2..84.8 in B
for (e = 1..4) for (b = 1..10) for (a = 1..4) {    // e outer, a inner
  v = -135 + 270 W(...) as in 5.4 (same reach / falloff quirk, no fade)
  g[36 a + 6 b + e] = v;                           // float array at 0xf735a4
}
```

**Aliasing**: the array is laid out as [6][6][6] (strides 0x90 and 0x18) but b runs to 10, so cell (a, b) and
(a + 1, b − 6) share a slot; the later write (larger b) wins. Every cell (a ≥ 2, b ≤ 4) therefore holds the value
of (a − 1, b + 6): the bottom part of the grid repeats the top part shifted by one in a. 24 % of the drawn cubes
of the part come from such cells. The model `grid10` reproduces it (grid error ≤ 0.0006).

### 6.4 Voxels 0x4193b6

As 5.5, over a = 1..4 (outer), b = 1..10, e = 1..4 (inner), reading k = 36 a + 6 b + e, neighbours at k ∓ 36
(a ∓ 1: bits 1, 2), k ∓ 6 (b ∓ 1: bits 4, 8), k − 1 (bit 0x20) and **k + 2** (bit 0x10), and

```c
cube(x = 30 (e - 3), y = 30 (b - 3), z = 30 (a - 3), mask, size)    // 0x418b86: uv 0.009, texture map B
```

So x, z run −60..30 and y −60..210: a tall 4 × 10 × 4 column (it turns slowly and the window cuts it). The
neighbour reads go outside the written slots: k < 43 and k = 209..215 are zero; k ≥ 216 reads the program's
variables after the array (0xf735a4 + 4k): only the a + 1 test of a = 4 reaches them, and two are ≥ 90:
k = 224 = E10 [0xf73924] (≥ 90 from T = 300, 4.3 s into the part) for the cell (a = 4, b = 7, e = 2), and k = 225 =
part 2's camera time [0xf73928] (994.8, constant) for (4, 7, 3). So those cells' +z faces are hidden whenever they
are full cubes (202 times in the part). The x + 2 test: for e = 3, 4 it reads an unwritten slot (0), so their +x
faces are always kept; for e = 1, 2 it never hid a face whose e + 1 neighbour was not full in the capture, but it
keeps interior +x faces as in part 9 (4,645 times).
A remake: treat k = 225 as always full, k = 224 as full when E10 ≥ 90, everything else outside the written slots as
empty.

The texture scale 0.009 makes a cube span 0.27 · 256 = 69 texels: map B's 16-texel lattice shows about 4 cells per
cube face (part 9: map C's 8-texel lattice, 3 per face).

### 6.5 Events

| event | position | effect |
|---|---|---|
| 10 | 36:00 | part = 10, window 0x414f04(−0.625, −0.3125) (rows 20..179 again) |
| 1011 / 1012 | 39:00, 39:02, ..., 39:3e (32, alternating, 1011 first) | light = 1.2 / 1.0: the cubes flash brighter for 2 rows out of 4; 1.0 after 39:3e |

### 6.6 Time base

| variable | closed form (T = t70 − 13442.0276) | kind |
|---|---|---|
| A10 [0xf73920] | 0.002 T (1.99 at the end) | linear |
| E10 [0xf73924] | 0.3 T (299.0 at the end) | linear |
| C0, C1, C2 | 0.02 (T9 + T), 0.03 (..), 0.04 (..), T9 = 1990.64 = part 9's length (5973.9528 → 7964.595) | linear, continues part 9 |
| light | event values | events |

Nothing depends on the frame rate (PARTS.md: equal pictures at equal timer values).

## 7. Pictures and textures

| what | where | kind | use |
|---|---|---|---|
| map A | [0xe35108] (0x54d3f4), 257 × 256 × 32-bit | generated (notes-gfx.md) | part 7's blob, texture-mapped (uv 3.1), shaded |
| map B | [0xe3510c] (0x58d7fc); part 10 through [0xe35114] = the same pointer | generated | part 8's blob, part 10's cubes |
| map C | [0xe35110] (0x5cdc04) | generated | part 9's cubes |
| tile mosaic | 0x428164, 64 × RGB dwords (percent) | stored data (the only stored image-like data of these parts) | flat 40 × 24 rectangles (2.4): exact at any resolution, no redraw needed |
| geosphere | octahedron 0x427f9c / 0x427f84, radius 300 (0x12c), subdivided by 0x4174c4 | generated mesh (engine) | parts 7, 8 |
| cube tables | 0x428020 (12 × {a, b, c, bit} ints), 0x4280e0 (8 × float corner ± 15) | static tables | parts 9, 10 |

No hand-drawn picture is used by these parts; the maps are read as textures, so they stay computed.

Other static values these parts read (read them from the exe or treat them as code constants): 0x427f70 light
1.0f, 0x427f74 0, 0x427f78 0.03f, 0x427f7c 112.0f, 0x427f80 0, 0x427fe4 9400.0f, 0x427fe8 0, 0x427fec 1.0f,
0x427ff0 0, 0x427ff4 0.03f, 0x427ff8 69.115036f, 0x427ffc 0, 0x428000 2.5f, 0x428004..0x42800c 35 (int),
0x428010 / 0x428014 0, 0x428140 0, 0x428144 2.3f, 0x428148..0x428150 35. The float / double constants in the code
are listed with the pseudo-code above.

## 8. For the remake

- **Matrices, deformations, fields, cube building, tiles**: plain float maths; nothing here is a lookup table or a
  fixed-point shortcut, except the tile colours (truncated to 0..255: use the float value) and the cube shades
  (16.16 ints: use the float 65536 · (192 + 31 Σ sin)).
- **Geosphere** (2048 triangles, int16 vertices, flat facets visible at the silhouette): the deformation is a
  function of the position on the undeformed sphere, so it can be evaluated on a finer sphere of radius 300 with
  float coordinates (the original's int16 truncation only reflects the integer vertices). The texture coordinates
  0.004 (x, y) and the per-vertex normals (recomputed from the deformed mesh) carry over. Optional; the shape a
  viewer describes stays the same. Note the creases where the clamps act (part 7 at s = 2.167, part 8 the plateau at
  2.5 and the floor at 2).
- **Voxel blocks** are the content: keep the 6 × 6 × 6 / 4 × 10 × 4 grids, the half-size field / 6, the 0.5 and 15
  thresholds, the culled faces with the x + 2 rule and part 10's aliasing and its two "full" reads past the array.
  Interior faces (5.5) are drawn by the original and hidden only by the sort; with a depth buffer they disappear,
  which is the intended look.
- **Sorting / culling** is the engine's (bucket sort, faces with mean z ≥ 6826.7 dropped: keep that limit for part
  8's fly-in, 4.4).
- **Time**: use t70 = timer / 17043 and the origins of 2.8; for part 9 the switch times of 5.3.
- **Rows the parts do not write** (2.5): black.

## 9. Models and checks (`models/parts3d/`, README.md there)

| script | what it checks |
|---|---|
| `run_clone.py` | copy of the shared runner with `--p3d FILE`: per frame dt, the state before / after, the matrix, the vertices handed to the engine, part 9 / 10 grids, sources, faces |
| `model.py` | the model (sections 3..6, tiles 2.4) |
| `check_matrix.py` | the rotation conventions against a dumped matrix (2.2) |
| `check78.py` | parts 7, 8: state, vertices, tile band per frame |
| `check910.py` | parts 9, 10: state, grid, cube count, vertices, faces, shades, uv, tile band per frame |
| `timebase.py` | the closed forms, the part origins, part 9's switches and the rotation difference, 803 / 901 starts |
| `quirks.py` | how often the x + 2 test, part 10's aliasing and its reads past the array take effect |
| `borders.py`, `extent.py` | rows never written by parts 8, 9, 10; the rows / columns the objects reach |
| `dump_data.py`, `snapinfo.py`, `events.py` | the static tables and initial values; the geosphere and the state in the snapshots; the events and part starts in the capture |

Runs used (outputs in the session scratchpad, not in the repository): each part from its snapshot to its end with
`--flip-frames --p3d`, at the default 150 M instructions per second; they follow the capture frame by frame.

## 10. Not determined

- How the engine saturates shades above 255 · 65536 (part 9 / 10 vertex shades up to 285 · 1.2) and the exact
  meaning of the two 0x414f04 values (notes-engine.md); the window rows 20..179 above are measured.
- Whether the faint lines inside part 9's blocks are the interior faces of 5.5 (guess).
- The music time of a position is taken from the player timer at the row's tick; the music scope has the exact
  mapping.
