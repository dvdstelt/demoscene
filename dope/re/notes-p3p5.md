# Scope "p3p5": the "7800 PHONG polys" object (p3), "ENV mapping" and "BUMP glow" (p5)

Addresses are `SEG:OFF` in `unpacked/p3.bin` or `unpacked/p5.bin` (file offset = SEG*16 + OFF; all code in segment
0010). "Retrace" = one run of the part's per-retrace routine (70.09 Hz, NOTES.md); k = number of retraces since the
part installed its timer. Angles are 16-bit, 65536 = one turn. Colours are 6-bit DAC values. "Frame" numbers are
capture frames (`full.frames`). Scripts: `models/p3p5/` (README there).

Both parts draw one object with the shared 3D engine (notes-engine.md documents it from p2) through a part-specific
**two-lookup filler**: per pixel, a texel of a *bump texture* selects an offset, and the colour is read from a 128x128
*environment / Phong map* at (view-space normal x, y) + that offset. p3 uses it as Phong shading with a fixed bump
(the map is a radial highlight gradient, the bump texture is addressed by the object-space normal); p5 uses the same
idea with a picture as environment map (ENV: no bump; BUMP: a red env map, a bump texture addressed by the mesh's
planar UVs, and a bump depth that pulses with |sin|). Nothing reads the music, nothing is random, and nothing is
advanced per drawn frame: all motion is integer state stepped in the per-retrace routine.

What was checked (section 8):
- whole frames: a Python model (`engine_model.py`, `p3model.py`, `p5model.py`) fed only with the camera state
  reproduces the engine's frame buffer **bit for bit** in 6 of 6 p3 frames and 9 of 9 p5 frames (4 ENV, 5 BUMP;
  one pixel differs in one ENV frame); those frame buffers are exactly the capture frames;
- motion: the camera state at every one of the 3605 (p3) and 4480 (p5) drawn frames equals the per-retrace
  integration of the script's velocities; every script step starts exactly at the summed N (no overshoot);
- p5's bump offset table: the formula reproduces the table used in all 4480 frames, with the phase sequence below;
- palettes: the composed palettes equal the captured DAC; p5's cross-fade palettes to p6's picture equal the
  mixing formula in every captured step.

---------------------------------------------------------------------------------------------------------------

## 1. What is on screen, when

| part | frames (capture) | k (retraces) | what |
|---|---|---|---|
| p3 set-up | 10023–10031 | — | CDPI loads p3; mode 13h at 10026 clears the screen to colour 0 = (0,0,3); palette loaded |
| p3 | 10032–13636 | 0–3604 | nebula picture (16 colours) with the grey/teal Phong-shaded lumpy object tumbling and approaching from the distance, then drifting off to the left; label "7800 PHONG polys" slides in at k 1121–1196, out at k 3361–3436 |
| p3 end | 13637 | — | last frame stays (the plain nebula: the object has left, the label is gone); p4 starts on it |
| p5 set-up | 21082–21090 | — | p4's last frame (the chequerboard) stays; p5 masks it to its low nibble (looks the same) |
| p5 ENV | 21091–22840 | 0–1749 | gold env-mapped star-shaped torus flies in, comes very close, recedes, flies up and out; label "phong ENV mapping" in at k 561–635, out at k 1611–1686 |
| p5 BUMP | 22841–25570 | 1750–4479 | the same torus, red, bump mapped with a pulsing bump depth; label "BUMP glow" in at k 2381–2456, out at k 4341–4416 |
| p5 hand-over | 25571–25587 | — | cross-fade from the chequerboard to p6's blue swirl picture (64 palette steps), then p6's picture alone |

The frame drawn with state k is displayed at capture frame 10032 + k (p3) / 21091 + k (p5): it is built between
retraces k and k+1 and copied to VGA right after retrace k+1 (`capmatch.py`), so the screen lags the state by one
retrace. At 100 M instructions/s both parts draw one frame per retrace throughout (69.9 / 69.7 fps in PARTS.md
count the set-up frames).

---------------------------------------------------------------------------------------------------------------

## 2. The engine path both parts use

### 2.1 Routines (p3 / p5 addresses; the p2 equivalents are in notes-engine.md)

| what | p3 | p5 |
|---|---|---|
| entry / init (sine table) / main script | 2bec / 2be5 → 19f0 / 2c22 | 3089 / 2bae → 1584 / 30e2 |
| run N retraces (frame loop) | 2ba4: 2a5a, 2a95, 2820, 2b1c | 2b66: 2a17, 2a52, 27e0, 2adc |
| objects loop / one object | 2a5a / 299b | 2a17 / 2958 |
| matrix / vertices (+ per-vertex) / faces | 1df6 / 1cd0 (+1bb0) / 2887 | 1906 / 17e0 (+16b0) / 2844 |
| radix sort / draw list | 2a95 / 2820 | 2a52 / 27e0 |
| triangle set-up / spans (unclipped, clipped) | 2211 / 1ff0, 20e0 | bump 1d51 / 1b30, 1c20; env 2500 / 2370, 2420 |
| frame copy (label, VGA, background) | 2b1c | 2adc |
| int 8 / per-retrace / timer set-up / exit | 1a43 / 1f60 / 1a86 / 1b63 (2f57) | 15d7 / 1a70 / 161a / 168f (3747) |
| work buffers (gs) | 343d: +0 face list, +0fa0 sort temp, +1f40 attr A, +2ee0 attr B, +3e80 screen dwords, +4e20 depth words, +5dc0/+6590/+6d60 face vertex pointers | 2685: +28a0 attr A, +3840 attr B, +47e0 screen, +5780 depth, +6720/+7148/+7b70 face pointers |
| frame buffer | 3b90 (`[19d0]`), 64 KB, row y at y*320 | 2edf (`[1564]`) |
| map segment tables | 0050:0000 = 0010:0400: env/Phong `[0337 033f 0b37 0b3f]`, 0010:0408: bump `[2341 2349 2b41 2b41]` | 0010:0000: env `[03d7 03df 0bd7 0bdf]`, 0010:0008: texture `[0000 1c96 0000 0000]` |

Note for anyone reading these listings with `rep.py`: capstone prints the bare byte 99h (`cwd` in 16-bit code) as
`cdq`; only `66 99` is `cdq`. The fillers' 16-bit `idiv bx` steps use `cwd` and are correct.

### 2.2 Object list and meshes

Object records of 0x3a bytes from 0010:1810 (p3) / 0010:1412 (p5), ended by a record with word +0 = 0:

| off | meaning |
|---|---|
| +00 | mesh segment; FFFFh = no mesh (object 0: the camera) |
| +02 | nonzero: object skipped (no drawing, no motion) |
| +04 | zoom (projection scale, s16) |
| +06 / +0A / +0E | position x, y, z (s32) — added to object 0's |
| +12 / +16 / +1A | velocity (s32 per retrace) |
| +1E / +20 / +22 | angles x, y, z (u16) — added to object 0's |
| +24 / +26 / +28 | angular velocity (s16 per retrace) |
| +2A | material add: low byte added to the faces' env-map index, high byte to their texture index |
| +2C | back-face sense (0: keep faces with a non-negative screen cross product) |
| +2E / +32 / +36 | pivot (s32) added to the mesh coordinates before rotating |

Object 0 is the camera: its position and angles are added to every object's and the scripts only move object 0.
p3: objects 1–3 (mesh 1362, zoom 400 / 800 / 1600, object 1 material add 0100h) are disabled leftovers; object 4
(1362:0000, zoom 2500, everything else 0) is the one drawn. p5: object 1 (1402:0000, zoom 2500) is drawn, object 2
(same mesh) is disabled; the script sets object 1's material add `[1476]` = 0000h (ENV) or 0101h (BUMP).

Mesh format (all little-endian): header words `vcount, fcount, voff, foff, noff, uvoff` (offsets from the
segment); vertices `3 x s16`; faces `u16 material, 3 x u16 vertex index * 4` (material low byte: env-map index,
high byte: texture index, both & 3 after adding the object's material add); normals `3 x s16`, length 27559 +- 1;
UVs `2 x u16` (U, V).

| mesh | where | size | shape |
|---|---|---|---|
| p3 | 1362:0000 (file 0x13620) | 0x4030 bytes: 514 vertices, 1024 faces (all material 0) | a tetrahedron subdivided 16 times per edge (4 vertices of valence 3, the rest 6), deformed into a lumpy blob, radius 4649–8947; UVs = planar x/y (U = 1.0272 x + 25262, V = 0.9856 y + 24990), not used by p3 |
| p5 | 1402:0000 (file 0x14020) | 0x3610 bytes: 432 vertices, 864 faces (all material 0) | a torus (Euler 0, valence 6) bent into a six-pointed star, radius 2176–8703, y extent -4875..4428; UVs = planar x/z (U = 1.1102 x + 24915, V = 1.0068 z + 24520, range 16491..32767) |

The label of p3 says 7800 polygons; the mesh has 1024 triangles (no other mesh is drawn).

### 2.3 Per frame (engine; verified bit for bit by `engine_model.py`)

```c
// sine table (p3 19f0, p5 1584): T[0..511] = quarter wave at 0010:0410 (p3) / 0010:0012 (p5),
// = round(32767 sin(i*2pi/2048)); T[512+j] = T[511-j] (sic: one entry early), T[1024..2047] = -T[0..1023],
// T[2048..2559] = T[0..511]. sin(a) = T[i], cos(a) = T[i+512] with i = (a >> 5) & 2047 (a u16).
a = obj.ang + cam.ang (u16, per axis);  s1,c1 / s2,c2 / s3,c3 = sin,cos of the x / y / z angles
m00 = (c2*c3 - ((s2*s1)>>15)*s3) >> 15;   m01 = (c1*s3) >> 15;   m02 = (s2*c3 + ((c2*s1)>>15)*s3) >> 15;
m10 = -(c2*s3 + ((s2*s1)>>15)*c3) >> 15;  m11 = (c1*c3) >> 15;   m12 = -(s2*s3 - ((c2*s1)>>15)*c3) >> 15;
m20 = (-s2*c1) >> 15;                     m21 = -s1;             m22 = (c2*c1) >> 15;
// = Rz(c) Rx(a) Ry(b) with Rx = [1 0 0; 0 c s; 0 -s c], Ry = [c 0 s; 0 1 0; -s 0 c], Rz = [c s 0; -s c 0; 0 0 1]
P = obj.pos + cam.pos;  zoom = obj.zoom
for each vertex v (+ pivot), normal n:
    X = m0.v + (P.x << 8);  Y = m1.v + (P.y << 8);  Z = m2.v          // 32-bit, R.v scaled by 32768
    depth = (((Z >> 16) * zoom) >> 14) + 0x4000                        // u16, ignores P
    Zd = Z - (P.z << 8)                                                // always negative here (in front)
    q = trunc(X*zoom / Zd);  sx = (q >> 3) + bit2(q) + 160            // rounded
    q = 3*trunc(Y*zoom / Zd); sy = (q >> 5) + bit4(q) + 100
    attrB = ((m0.n) & 0xffff0000 | (m1.n) >> 16) + 0x40004000          // env coords, two 8.8 words
    attrA = p3: (wrap15(n.y) << 16) | wrap15(n.x)  (wrap15(a) = ((a+16384) mod 32768) - 16384, as u16)
            p5: the mesh UV dword (V << 16 | U)
for each face (a, b, c):
    keep if s16(s16((sxb-sxa)*(syc-sya)) - s16((sxc-sxa)*(syb-sya))) >= 0     // 16-bit products
    key = depth_a + depth_b + depth_c (u16)
stable sort ascending by key >> 5 (binary LSD radix sort on bits 21..31 of key<<16|slot); draw in that order
```

So view x (row 0) points to the screen's left and view y to the screen's top (Zd < 0); the projection is
`x = 160 + 312.5 X/|Zd|`, `y = 100 - 234.4 Y/|Zd|` in X, Y, Zd vertex units (horizontal field of view 54.2 deg; the
vertical scale is 0.75 of the horizontal, i.e. 0.9 of round on a 4:3 screen). The filler draws rows y = 1..200 at
frame-buffer row y, and the frame buffer is copied to the screen from row 1: **screen row = y - 1**, so the
projection centre is at screen (160, 99).

Filler (2211 = p5 1d51; env-only 2500): vertices sorted by sy; triangles entirely right of x = 320 (unsigned
compare of `sx<<16|sy` with 1400000h) or outside rows 1..201 skipped; any vertex outside → clipped span loop. Edges
are 16.16 DDAs starting at the top vertex (left edge at x, right edge at x + 0x7fff, env-only: + 0x8000), stepped
with a packed add/adc pair; rows from the top vertex's row up to (not including) the bottom vertex's; a span covers
columns floor(xL) .. floor(xR) - 1. The two attributes are interpolated **affinely**, both 16-bit halves of each dword
in one 32-bit add (carries cross from the low into the high half: at most 1/256 texel per pixel, invisible); the
left-edge value is stepped per row, the horizontal step per pixel is computed once per triangle at the middle
vertex's row (`((A_a - A_c)*dy_ab/dy_ac - A_a + A_b) << 16) / width`, 16-bit integer divisions). Degenerate triangles
reuse the previous triangle's edge steps.

---------------------------------------------------------------------------------------------------------------

## 3. p3: "7800 PHONG polys"

### 3.1 Set-up (2c22)

```c
mode 13h; fill A000 with 0;                               // colour 0 = (0,0,3)
wait for retrace;
DAC[0..239]   = 0306:0010 (720 bytes)                      // palette of the Phong-map RIX file
DAC[240..255] = 3342:02DA (48 bytes)                      // label RIX palette entries 240..255
DAC[0..15]    = 4B81:0000 (48 bytes)                      // nebula RIX palette entries 0..15
attribute 11h (overscan) = 0;
per-retrace routine = 1f60; timer set-up 1a86 (count = 2P*990/2048, core scope)
label pos [19C4] = -1, label speed [19C6] = 0 (image)
camera: pos (0, 0, 8192000), ang (0, 0, 0), vel (0, 0, -2600), avel (-110, 20, 10)
```

### 3.2 Script (21 steps, 3605 retraces; k = retrace at which the step starts)

| step | k | N | changes before the step |
|---|---|---|---|
| 1–8 | 0, 140, ..., 980 | 140 each | avel = (-110, 20+i, 10+2i) for i = 0..7 |
| 9–15 | 1120, ..., 1960 | 140 each | avel = (-110, 28+i, 26+2i) for i = 0..6; before step 9: label speed `[19C6]` = 1 |
| 16 | 2100 | 140 | vel = (0, 0, -1100) |
| 17 | 2240 | 140 | vel = (0, 0, -600) |
| 18 | 2380 | 420 | vel = (0, 0, 0) |
| 19 | 2800 | 140 | vel = (1250, 0, -800) |
| 20 | 2940 | 420 | vel = (2650, 0, -1600) |
| 21 | 3360 | 245 | label speed = -1 |
| exit | 3605 | | 1b63: restore int 8 and the PIT, `retf` (exit code 0) |

Camera state after k retraces: `ang = sum(avel * retraces)` (mod 65536), `pos = (0,0,8192000) + sum(vel * retraces)`,
piecewise linear over the steps (step i covers retraces k_i+1 .. k_i+N_i with that step's velocities). The last
drawn state (k = 3604): ang (-3224, -23236, -23520), pos (1934600, 0, 1319600). In vertex units the camera distance
goes from 64000 to 10300 (nearest vertex 3302 at the end), then the object drifts left (positive x is screen-left).

### 3.3 Per retrace (1f60)

```c
label += label_speed; if (label < 0 || label >= 75) label_speed = 0;   // [19C4] += [19C6]
[19D6] += 100;                                                          // unused
for each object with +02 == 0 and +00 != 0: ang += avel; pos += vel;    // object 0 and object 4 (zero velocities)
ready [1996]++; retraces [199A]++;
```

### 3.4 Frame (2ba4 → 2a5a, 2a95, 2820, 2b1c)

Transform and sort (2.3), draw every sorted face with the Phong+bump filler, then 2b1c: wait for the ready flag,
draw the label into the frame buffer, copy frame-buffer rows 1..201 (0x3ED0 dwords) to A000, copy the nebula
(4BB1:0000, 0x3ED0 dwords) back into the frame buffer from row 1 for the next frame.

Label: a 70x44 opaque box (every pixel copied, indices 240..254), top row on screen row 8. With `a` = label pos:
`a < 0`: not drawn; `0 <= a <= 70`: label columns 70-a..69 at screen x 0..a-1 (slides in from the left edge, one
pixel per retrace); `70 < a <= 75`: whole label at x = a - 70. It rests at x = 5 (a = 75).

### 3.5 The Phong + bump pixel (1ff0 inner loop; per triangle, mat = 0 → Phong map 0337, bump 2341)

```c
// A = (V<<16 | U) interpolated attrA = wrapped object-space normal (U = wrap15(n.x), V = wrap15(n.y))
// B = interpolated attrB: high word = 0x4000 + (view x of n)>>16, low word = 0x4000 + (view y of n)>>16
t   = BUMP[ ((U >> 8) & 127) * 256 + ((V >> 8) & 127) ];        // 2341:0000, 128x128 used, rows of 256
off = TAB[t];                                                    // 256 words at 0010:0000 (file 0x100)
idx = PHONG[ ((B.lo >> 8) * 256 + (B.hi >> 8) + off) & 0xffff ]; // 0337:0000, rows of 256
pixel = idx;                                                     // palette 128..239
```

TAB holds `off(t) = 256*dy + dx` with **dy = (t & 15) - 8** (Phong-map rows) and **dx = (t >> 4) - 8** (columns),
except for t = 128..143 (dx = 0), where dy is one less: `(t & 15) - 9` (a table quirk; 27% of the texture's
texels have dx = 0). So the bump moves the Phong lookup by -8..+7 texels in each direction (up to 8.5 deg of normal
tilt at a map radius of 53.8).

Phong-map coordinates: column = 64 + (view x of n) * 53.82, row = 64 + (view y of n) * 53.82, where 53.82 =
27559 * 32766 / 2^24 (normal length times the matrix row length). With the bump the lookup stays within columns /
rows 2..124 of the 128x128 map. Because view x points left and view y up, the map is seen point-mirrored: a normal
facing screen-right reads the map's left part, one facing screen-down its top.

### 3.6 Data

| item | where | format | notes |
|---|---|---|---|
| Phong map | ColoRIX file "RIX3" at 0306:0006 (file 0x3066): 256x128, palette 0306:0010, pixels 0337:0000 (file 0x3370) | left 128x128 = the map, right half all 255 (never read) | **generated**: a radial gradient centred on (55, 40): index = clamp(floor(240.5 - d), 128, 239), 239 inside d < 4.4 (white highlight disc); this fits within +-2 indices (15% of texels off by 1-2: drawn with a paint program's gradient, `phongfit.py`). Indices read: 153..239 |
| object palette | RIX palette entries 128..239 = DAC 128..239 | 6-bit RGB ramp | 239 white, down through teal-grey to black at ~182–186, then dark violet (~160) to violet-grey (128 = 2d383b); 16..127 are unused green placeholders (0,63,0). Distance from the highlight: 0–4 white, ~55 black, beyond: the violet rim |
| bump texture | 2341:0000 (file 0x23410), 64 KB, rows of 256 | 128x128 in the top-left quadrant, rest 88h (= no offset) | **generated**: each byte packs two clamped differences of a height field (`hi` nibble = horizontal, `lo` = vertical difference, values 0,2,4..14 and 15); the height field (`bumpmaps.py` reconstruction) is a soft fractal noise ("plasma"). Not a picture |
| bump table | 0010:0000 (file 0x100), 256 words | 256*dy + dx | see 3.5 |
| nebula | RIX3 at 4B80:0006 (file 0x4b806): 320x200, palette 4B81:0000, pixels 4BB1:0000 (file 0x4bb10) | 16 colours (0..15) | hand-drawn violet nebula with stars; the same file is in p4 at 4EED:0006 |
| label | RIX3 at 3342:0000 (file 0x33420): 70x44, palette 3342:000A, pixels 3342:030A | indices 240..254, grey ramp DAC 240..255 = round(4.2 i) | "7800 PHONG polys", hand-pixelled |
| mesh | 1362:0000 | 2.2 | |
| sine quarter | 0010:0410 (file 0x510), 512 words | 2.3 | |

### 3.7 Time bases

All in retraces: camera angles and position (integer per-retrace steps, script table above), label position
(1 pixel per retrace). Nothing per drawn frame, no random numbers, no music.

### 3.8 For the remake

Evaluate exactly:
- angles: continuous `a * 2pi/65536` (the engine quantises to 2048 steps through the sine table, and its mirrored
  quarter is one entry early); matrix and projection in float, motion interpolated between retraces;
- the Phong lookup: per pixel, interpolate the view-space normal (perspective-correct) and look up the map at
  (64 + 53.82 n_x + dx, 64 + 53.82 n_y + dy), with the map either read from the RIX and filtered, or computed from the
  fitted gradient (continuous `240 - d`, white disc d < 4.4) and the colour interpolated along the palette ramp
  128..239. The original interpolates the projected normal components linearly per triangle (not renormalised);
  with 1024 faces renormalising makes little difference;
- the bump: a filtered (dx, dy) field: convert each texel through TAB (keeps the dx = 0 quirk, a 1-texel shift) and
  filter bilinearly instead of nearest-neighbour; a smooth alternative is the reconstructed height field;
- the label slide: interpolate the position (sub-pixel slide), x = a - 70 clamped as above;
- depth: a z-buffer instead of the 11-bit painter's sort; exact back-face culling.

Keep (part of the look):
- **the 15-bit wrap of the bump coordinates** (`shl eax,1; sar ax,1` in 1bb0): on the 280 of 1024 faces whose
  vertices straddle |n.x| or |n.y| = 16384, the texture is interpolated "the long way round" and squeezed about 30
  times, which draws the dense zig-zag contour bands across the object (`quirk_p3.py`: 11506 pixels change at frame
  12500 without it). Reproduce it by giving each vertex u = wrap15(n.x)/256, v = wrap15(n.y)/256 (texels), interpolating
  them linearly and sampling with wrap-around (period 128 texels);
- the bump addressing by object-space normal (the pattern is fixed to the object), the palette ramp, the
  point-mirrored map orientation, the projection's 0.75 vertical scale and centre (160, 99);
- the one-retrace latency between state and display only matters for frame-exact comparisons.

Drop: carries between the packed halves (at most 1/256 texel per pixel), nearest-neighbour reads, integer
spans.

---------------------------------------------------------------------------------------------------------------

## 4. p5: "phong ENV mapping" and "BUMP glow"

### 4.1 Set-up (30e2)

```c
for i in 0..63999: A000[i] &= 0x0F; 2EDF:0140[i] = A000[i]; 3ECF:0310 (= 3F00:0000)[i] = A000[i];
        // the background is p4's last frame (the chequerboard, indices 0..14) taken from VGA memory
3ECF:0010 = DAC[0..15]                       // read back: p4's chequerboard colours (kept for the hand-over)
wait for retrace;
DAC[16..239]  = 03A6:0040 (672 bytes)        // env-map RIX palette entries 16..239
DAC[240..255] = 248F:02DA (48 bytes)         // label palette (same as p3's)
                                             // DAC[0..15] stays as p4 left it
per-retrace routine = 1a70; timer 161a (count = 2P*1000/2048)
```

The chequerboard itself is p4's (IFF PBM at p4 177A:0006, notes-p4.md); p5 has no copy. The DAC p4 leaves is one
6-bit step darker than the CMAP in every non-zero component (notes-p4.md): (2,0,0), (6,0,0), ... (62,49,0).

### 4.2 Script (28 steps, 4480 retraces)

Initial (ENV): material add `[1476]` = 0, camera pos (0, -1024000, 2304000), ang (0,0,0), avel (-80, -30, 140),
label pos `[1558]` = 0 (draws nothing), label segment `[1556]` = 248F, bump speed `[1548]` = 0.

| step | k | N | changes before the step |
|---|---|---|---|
| 1 | 0 | 140 | vel = (0, 6200, 14000) |
| 2 | 140 | 140 | vel = (0, 3200, 13000) |
| 3 | 280 | 280 | vel = (0, 0, -18000) |
| 4 | 560 | 35 | label speed `[155A]` = 1; vel = (0, 0, -10000) |
| 5 | 595 | 35 | vel = (0, 0, 10000) |
| 6 | 630 | 420 | vel = (0, 0, 13000) |
| 7 | 1050 | 420 | vel = (0, 0, -13000) |
| 8 | 1470 | 35 | vel = (0, 0, -10000) |
| 9 | 1505 | 35 | vel = (0, 3000, 12000) |
| 10 | 1540 | 70 | vel = (0, 7000, 12000) |
| 11 | 1610 | 70 | label speed = -1; vel = (0, 15000, 12000) |
| 12 | 1680 | 70 | vel = (0, 27000, 12000) |
| 13 | 1750 | 140 | **BUMP**: material add = 0101h; camera pos (0, -1024000, 2048000), ang (0,0,0), avel (-20, 10, 90); bump speed = 24; vel = (0, 5200, 16000) |
| 14 | 1890 | 140 | avel (-20, 20, 90); vel = (0, 2200, 13000) |
| 15 | 2030 | 350 | avel (-20, 30, 90); label segment = 258A ("BUMP glow"); vel = (0, 0, -11000) |
| 16 | 2380 | 35 | avel (-30, 40, 80); label speed = 1; vel = (0, 0, -10000) |
| 17 | 2415 | 35 | vel = (0, 0, 10000) |
| 18 | 2450 | 420 | avel (-40, 40, 80); vel = (0, 0, 10000) |
| 19 | 2870 | 420 | avel (-50, 40, 80); vel = (0, 0, -10000) |
| 20 | 3290 | 35 | avel (-60, 40, 80) |
| 21 | 3325 | 35 | vel = (0, 0, 10000) |
| 22 | 3360 | 420 | vel = (0, 0, 13000) |
| 23 | 3780 | 420 | vel = (0, 0, -13000) |
| 24 | 4200 | 35 | vel = (0, 0, -10000) |
| 25 | 4235 | 35 | vel = (0, 3000, 12000) |
| 26 | 4270 | 70 | bump speed = 20; vel = (0, 7000, 12000) |
| 27 | 4340 | 70 | label speed = -1; vel = (0, 15000, 12000) |
| 28 | 4410 | 70 | vel = (0, 27000, 12000) |
| hand-over | 4480 | | 4.7 |

The camera state is the same piecewise-linear integral as in p3 (restarting from the set values at step 13). The
torus comes as close as 926 vertex units (k = 1505, radius ~8700): it fills the screen at frames ~21590–21700 and
~22580–22610.

### 4.3 Per retrace (1a70) and the bump depth (3099)

```c
label += label_speed; if (label < 0 || label >= 75) label_speed = 0;          // [1558] += [155A]
phase += bump_speed;                                                          // [1546] += [1548], u16
s  = T[(phase & 0x7fe) >> 1];                     // index 0..1023: the non-negative half of the sine table
A  = ((((s >> 4) * 3) & 0xffff) >> 4);            // 0..383 (unsigned shifts; s >= 0 here)
for k in 0..15: b[k] = high byte of (u16)(-8*A + k*A)   // = floor((k-8)*A/256), -12..+10 at A = 383
for i, j in 0..15: TAB5[16*i + j] = (b[i] << 8) | (u8)b[j];                   // 0010:375B, 256 words
[156A] += 100;                                                                // unused
objects' motion as in p3; ready [1524]++; retraces [1528]++
```

phase(k) = 0 for k <= 1750, 24 (k - 1750) up to k = 4270, then 60480 + 20 (k - 4270) (verified for all 4480 frames).
So the bump depth A/256 = 1.496 |sin(pi * phase / 2048)| (to the integer truncations): a pulse every 85.3 retraces
(1.22 s), from k = 4270 every 102.4 retraces (1.46 s) — the "glow". The table is rebuilt in the interrupt every
retrace.

### 4.4 Frame (2b66 → 2a17, 2a52, 27e0, 2adc)

As p3 (3.4): the background is 3F00:0000 (the masked chequerboard), the label comes from segment `[1556]` (248F or
258A), 70x44 at 030A, same slide rule and position. The face filler depends on the texture segment of the face's
material: 0 → env-only filler (ENV), else the bump filler (BUMP).

### 4.5 ENV filler (2500 set-up, 2370 / 2420 spans)

```c
idx = ENV[ (B.lo >> 8) * 256 + (B.hi >> 8) ];       // 03D7:0000 (left half of the env RIX), indices 16..79
```
Same attribute B as p3 (map radius 53.82 around (64, 64), point-mirrored), no bump. Differences from the bump
filler: the right edge starts at x + 0x8000 (+0x7fff when the top half is skipped), the per-pixel steps are one less
in each 16-bit half (`dec ax` after each division), the span counter is 8 bits (`dec al`: spans longer than 255
pixels would wrap; no triangle span is that long in p5), and clipped spans stop at x = 318 (column 319 keeps the
background where a clipped triangle covers it, in 394 ENV frames).

### 4.6 BUMP filler (1d51 set-up = p3's 2211; 1b30 / 1c20 spans)

Identical to p3's filler except the two lines that address the bump texture and the table:

```c
// A = interpolated mesh UV (V << 16 | U); `shld ebx, ecx, 10` instead of 8:
row = (((U >> 8) & 31) << 2) | ((V >> 14) & 3);     // V >= 16384 always: (V >> 14) & 3 == 1
col = (V >> 6) & 127;
t   = TEX[row * 256 + col];                          // 1C96:0000, 128x128 used, rows of 256
off = TAB5[t];
idx = ENV[ ((B.lo >> 8) * 256 + (B.hi >> 8) + off) & 0xffff ];   // 03DF:0000 = right half of the env RIX
```

So the texture is effectively **128 columns x 32 rows (rows 1, 5, 9, ..., 125 of the bump texture)**, repeated every
8192 units of U (vertical texel rows, each stretched 4x) and of V (columns, 64 units each): about two repeats across
the torus in each direction. The offset: rows `floor((hi-8)A/256)`, columns `floor((lo-8)A/256)` (hi/lo = nibbles of
t; note the opposite nibble order to p3), and because the column byte is OR-ed in unsigned, the row is one more
whenever the column offset is negative. Lookups: columns -2..127, rows -2..128 of the red map at full depth: at the
silhouette the read can leave the 128x128 map (left: the gold map's last columns; above row 0: the RIX palette
bytes; below row 127: the data after the RIX).

### 4.7 Hand-over to p6's picture (369c..3746)

```c
pal1 = saved DAC[0..15] (chequerboard, 3ED0:0000 -> cs:2BB5); pal2 = 4EA1:0000 (p6 picture palette -> cs:2BE5)
168f: restore int 8 and the PIT (the per-retrace routine stops)
w = 0; mix(w);                                   // 3038: palette for w, one retrace wait, w += step, count++
A000[i] = (bg[i] & 15) + ((SWIRL[i] & 15) << 4)  // SWIRL = 4ED1:0000: lo nibble chequerboard, hi nibble p6 picture
count = 0; step = 64;
do mix(w) while (count < 64);                    // w = 0, 64, ..., 4032 (stops adding at >= 4096)
DAC[0..15] = pal2; A000 = SWIRL;                 // p6's picture alone, exit (retf)

mix(w): w1 = (4096 - w) >> 4, w2 = w >> 4 (0..256);  A[i] = floor(i*w1/256), B[i] = floor(i*w2/256)
        DAC[16*hi + lo] = min(63, A[pal1[lo]] + B[pal2[hi]])   // per channel, all 256 entries
        wait for the start of a retrace (3DAh bit 3), write the DAC
```

A linear cross-fade (alpha = w/4096) of the chequerboard to p6's picture in 64 palette steps. Each step waits for
the retrace bit, so on real hardware (768 DAC writes take far longer than the 2-line retrace pulse) it advances one
step per retrace: **64 retraces = 0.91 s**. The emulator's DAC writes cost almost nothing, so the capture advances 4
steps per frame (frames 25572–25587, 0.23 s; `handover_p5.py`: every captured palette equals mix(w)). This is the
only CPU-dependent timing in p3/p5; the remake should use 64 retraces (it shifts p6's start, core scope).

### 4.8 Data

| item | where | format | notes |
|---|---|---|---|
| env maps | RIX3 at 03A6:0006 (file 0x3a66): 256x128, palette 03A6:0010, pixels 03D7:0000 (file 0x3d70) | two 128x128 maps side by side | **hand-drawn / rendered pictures**. Left (03D7, ENV): a liquid-gold reflection of a chequerboard, gold ramp 16..79 (black, red-brown, orange, yellow, white); column 127 holds 80..105 (never read). Right (03DF, BUMP): a glossy red scene with a bright highlight at the top, a plant silhouette, two small light spots and a curved reflection; red ramp 80..142 and 79 (white) for the highlight |
| palette | RIX entries 16..239 → DAC 16..239 | 16..79 gold ramp, 80..143 red ramp, 144..239 unused greens | |
| bump texture | 1C96:0000 (file 0x1c960), rows of 256 | 128x128 top-left | **generated** like p3's (nibble differences of a height field, heavily clamped: a quarter of the nibbles are 0, a quarter 15); the height field is an organic cell / vein pattern. The other quadrants hold other data, not used by p5 |
| bump table | built at run time (4.3) | | |
| labels | RIX3 70x44 at 248F:0000 ("phong ENV mapping") and 258A:0000 ("BUMP glow"), pixels at +030A | indices 240..254 | hand-pixelled; palettes equal p3's label palette |
| p6 picture | RIX3 at 4EA0:0006 (file 0x4ea06): 320x200, palette 4EA1:0000, pixels 4ED1:0000 | 16 colours | hand-drawn blue swirl; byte-identical to p6's RIX at 0C66:0006 |
| mesh / sine quarter | 1402:0000 / 0010:0012 (file 0x112) | 2.2 / 2.3 | |

### 4.9 Time bases

Retraces: camera motion, label, bump phase (24 / 20 per retrace). Per palette step (intended: per retrace, see
4.7): the hand-over cross-fade. Nothing per drawn frame, no random numbers, no music.

### 4.10 For the remake

Evaluate exactly: as p3 (continuous angles, float transform, z-buffer, perspective-correct per-pixel normals,
filtered env-map reads, interpolated label), plus:
- the bump depth as the continuous `A/256 = 1.4965 * |sin(pi * phase / 2048)|` with phase interpolated between
  retraces; offsets `(k - 8) * A/256` texels (drop the floor and the +1 row for negative column offsets);
- the bump field filtered: build the effective 128x32 texture (rows 4j+1), convert to (dx, dy) and filter; map
  u = U/8192, v = V/8192 with wrap-around;
- clamp the red-map lookup to its 128x128 map (the original reads neighbouring memory at the extreme silhouette);
- exact culling: the 16-bit back-face products overflow when the torus is very close (sampled frames 21685–21690
  and 22580–22605: up to 49 faces per frame culled or kept wrongly); column 319 drawn.
Keep: the 4x-stretched, 1-in-4-rows bump pattern (it is the visible texture), the gold / red pictures, the pulse,
the label timing, the 64-step linear cross-fade.

---------------------------------------------------------------------------------------------------------------

## 5. Pictures and textures for the redraws

PNGs with their palettes in `dope/re/dump/pictures/` (`models/p3p5/pictures.py`):

| PNG | hand-drawn? | where | how it is used | what a redraw needs |
|---|---|---|---|---|
| `p3-nebula.png` | yes | p3 RIX3 4B80:0006, 320x200, 16 colours, palette = RIX entries 0..15 | drawn as-is behind the object every frame | a true-colour redraw replaces it directly; p4 also starts on it and blends its fire over it (notes-p4.md) |
| `p3-label-phong.png` | yes | p3 RIX3 3342:0000, 70x44, indices 240..254 (grey ramp) | opaque box over the frame, sliding | replace directly (same 70x44 box at 4:3) |
| `p5-envmaps.png` | yes (artwork, maybe rendered) | p5 RIX3 03A6:0006, 256x128 = two 128x128 env maps | env maps read at (64 + 53.8 n_x, 64 + 53.8 n_y) of the view-space normal, seen point-mirrored; only the disc of radius ~54 (gold) / ~66 (red, with bump offsets) around each half's centre is read; the red half is read through bump offsets | a redraw at any resolution with the same layout; the lookup is a plain texel fetch, so true colour works unchanged; keep the content inside the read discs where it is |
| `p5-label-env.png`, `p5-label-bump.png` | yes | p5 RIX3 248F:0000, 258A:0000 | as p3's label | replace directly |
| `p5-chequerboard.png` | yes (p4's IFF PBM, Deluxe Paint) | p4 177A:0006 (notes-p4.md); p5 takes it from VGA memory & 0Fh with p4's darker DAC | drawn as-is behind the torus; low nibble in the hand-over mix | replace directly; must equal p4's redraw; the cross-fade becomes a true-colour blend |
| `p5-swirl.png` | yes | p5 RIX3 4EA0:0006 (= p6's 0C66:0006) | high nibble in the hand-over mix, then shown alone (p6's background) | the same redraw as p6's; true-colour blend |

Not pictures (computed or derived data, no PNG): p3's Phong map (a radial gradient, compute it, 3.6); the two bump
textures (packed clamped height differences; a redraw would need a height map: `bumpmaps.py` reconstructs one by
least squares, or filter the (dx, dy) field directly); the bump offset tables.

---------------------------------------------------------------------------------------------------------------

## 6. Not determined

- One pixel of one ENV frame (21600, x 192, screen row 27) differs from the model; not investigated (probably a
  degenerate triangle using a stale per-triangle variable).
- Whether the env-map pictures were painted or rendered, and what the bump height maps looked like before
  differencing (guess: p3's is generated plasma noise).
- The hand-over speed on real hardware (4.7) is reasoned, not measured.
- Why the label says 7800 polygons (the drawn mesh has 1024).

---------------------------------------------------------------------------------------------------------------

## 7. Shared routines

Nothing outside the engine (notes-engine.md) and the core framework (notes-core.md) is shared. The part-specific
fillers are documented here: p3 2211/1ff0/20e0 (Phong + bump, texture addressed `shld 8`, table at 0010:0000), p5
1d51/1b30/1c20 (same with `shld 10` and the run-time table at 0010:375B) and p5 2500/2370/2420 (env only). The
"two-lookup" filler shape (texel → offset into a 256-wide map) may also exist in p6 / p7 ("METAL shading", "multiple
LIGHT sources"); not checked.

## 8. Checks (`models/p3p5/`)

| check | result |
|---|---|
| `verify_p3.py` on frame buffers dumped by `hookrun.py` at frames 10300, 11000, 11800, 12500, 13000, 13300 | 0 of 64000 pixels differ in all six (object up to 43000 pixels) |
| `verify_p5.py` at 21300, 21600, 22000, 22400 (ENV) and 23300, 23600, 24200, 24800, 25200 (BUMP) | 0 pixels differ, except 1 pixel at 21600 |
| `verify_p5.py` / phase check: bump table formula | 4480 of 4480 tables, phase(k) as in 4.3 |
| `capmatch.py`: dumped frame buffers vs `full.frames` | identical to capture frame = copy frame (10032 + k, 21091 + k) |
| `steps.py`: camera state | equals the per-retrace integration at all 3605 / 4480 drawn frames; step calls exactly at the summed N; one drawn frame per retrace |
| captured DAC vs composed palettes | equal (p3 frames 10040, 13600; p5 16..255 at 21200, 25500) |
| `handover_p5.py` | every captured hand-over palette equals mix(w), w = 0, 0xC0, 0x1C0, ..., 0xEC0 |
| `quirk_p3.py`, `cull16.py`, `spans_p5*.py`, `depth_range.py` | the quirk measurements quoted above |
