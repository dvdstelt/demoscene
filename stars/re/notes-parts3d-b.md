# Scope "parts3d-b": the textured surface (0xa14d / 0xa0ca / 0xa346), the chrome star (0x164dc), the torso with the ring (0x16d86 / 0x16f7c / 0x174bd)

Addresses are offsets in `unpacked/stars_img.bin` (= near pointers = linear addresses in the headless run). Files
are overlay file numbers (`unpacked/files/NN.bin`). "t" is always the part's own PIT tick counter (70.064 Hz),
counted from the moment the part installed its PIT callback; frames, times and music positions are those of the
capture (`full.ticks`). Angles are in 1/4096 of a turn (BARTi's engine), positions in object units unless marked
"1/1024". The engine internals (rasteriser, sort, object format in full) belong to notes-engine.md; this file
describes the engine only as far as these parts use it, with formulas checked against the engine's own results.

What was checked (scripts in `models/parts3d-b/`, section 9):
- **motion and state, every drawn frame of all three parts**: Python models of the three PIT callbacks and frame
  routines reproduce every logged variable (object positions and angles, morph key and tick, palette levels, phase
  flags, beat schedule) exactly: 700 frames (surface), 1103 frames (star), 1055 frames (torso);
- **palettes**: every distinct palette in the capture during the three parts is produced by the formulas below
  (100 / 154 / 85 distinct palettes; 6 star frames are a mix of two consecutive palettes because the PIT callback
  changed the levels while 0x168a3 was running);
- **morph** (0x78ffe): the engine's vertex buffer equals the model bit for bit (star and surface dumps);
- **transform, projection, env coordinates**: an integer model of the engine matrix reproduces the engine's view
  coordinates, screen coordinates and per-vertex env coordinates exactly in dumps of all three parts;
- **light maps** (0x7873a): both parts' 256x256 maps are reproduced byte for byte (including the ring band);
- **texture morph** (0xa5c5 / 0xa57d): the eight precomputed frames and the tiled texture match exactly;
- **pixel formulas**: an approximate rasteriser fed with the engine's per-vertex data reproduces 73–84 % of the
  surface's pixels exactly (89 % of the light part; without the bump offset only 0.6–20 %), 61 % of the star's and
  54–59 % of the torso's (the rest are texel rounding differences of the approximate rasteriser; the images are
  visually identical).

-----------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | routines (callback) | frames | time (s) | music | t = 0 at PIT tick | fps (capture) | on screen |
|---|---|---|---|---|---|---|---|
| surface | 0xa14d set-up (frames 13804–13809), 0xa0ca (0xa4ac), 0xa346 free | 13810–15971 | 197.04–227.89 | 45:02–54:01 | 13771 | 22.6 (3 ticks per frame) | a 23x23 grid surface over a blue cloud picture (file 25); the surface turns and sways, and its vertices morph between a flat wall and nine face reliefs; textured with a stone frieze (map 21) or a Celtic knot pattern (map 23), lit by a light spot that is fixed to the view and embossed by the texture |
| star | 0x164dc (0x16646); precalc 0x16945 at start-up (parts3d-a) | 16643–18450 | 237.46–263.26 | 58:21–65:0a | 16605 | 42.6 | an env-mapped "chrome" star (file 51, three morph shapes, env map file 49) rises from below, spins, pulses darker on an accelerating beat, then flies into the camera while the screen fades out with a bounce; behind it a pre-rendered carousel of 16 small spiky objects (file 50) along the bottom |
| torso | 0x16d86 set-up (19469–19503), 0x16f7c (0x17513), 0x174bd free | 19504–21955 | 278.29–313.27 | 6c:01–7c:2f | 19463 | 29.5 | a stone torso (file 46, light-mapped with a stone bump texture) rises turning in front of a dark green landscape (file 47); a red ring (part of file 46, env map file 29) rises from below and slides up and down it; later a twisted pink mirror object (file 45) flies in and reflects the torso and ring, which are rendered into its env map every frame |

Every part draws into the engine's 320x200 screen buffer and copies rows 1..197 to the VGA after the start of the
vertical sync; rows 0, 198 and 199 stay black (checked in the capture). All three parts end by their own tick
counter, not by the music. After the surface, main frees it, shows picture 57 (black palette) and waits for 0x5500
to fade it in; after the star and the torso, main clears the VGA and goes on at once (the star ends at 65:0a, past
the 0x6100 main tests; the torso's own order test, order > 0x82, is never reached).

-----------------------------------------------------------------------------------------------------------------

## 2. The engine as these parts use it

### 2.1 Objects (`B!` files) and object records

What the parts rely on (full format: notes-engine): `B! 1a xx`, byte 3 = 0 for an object with morph key sets,
0x0f for a static one; word nverts, word nfaces; vertices of 16 bytes (int16 x, y, z, int16 nx, ny, nz with
|n| ≈ 255, uint8 u, v texture coordinates, 2 bytes 0); faces of 16 bytes (word material, words a, b, c vertex
indices, int16 face normal, word 0); then, for morph objects, key vertex sets of nverts x 16 bytes. The first vertex
set (after the header) is the working set the engine draws.

| file | used by | verts | faces | key sets | contents |
|---|---|---|---|---|---|
| 24 | surface | 529 (23 x 23) | 968 | 10 (sets 0..8 used) | grid, x = -1596..1604 (steps 140 / 150), y = -996..1004 (step 90), u = v = round(240 i / 22) (u along x, v along y), all normals (0, 0, -255) in set 0; set 0 = flat (= the working set in the file), sets 1..9 = face reliefs (z -1055..211) |
| 51 | star | 394 | 784 | 3 | the star, three different spike shapes (sets 0..2), working set = set 0 |
| 46 | torso | 242 | 459 | — | vertices 0..7 = bounding-box corners (unused, garbage normals), 8..176 = torso (faces 0..328), 177..241 = the ring (13 x 5 torus, radius 398..513, y 8..116; faces 329..458) |
| 45 | torso | 192 | 384 | — | the twisted mirror object (x -647..734, y -680..636, z -549..389) |
| 50 | (precalc 0x16945) | 158 | 316 | — | the small spiky object of the carousel (parts3d-a) |

Object record (0x20 bytes at the part's address): +0/+4/+8 int32 x, y, z; +0xc/+0xe/+0x10 int16 angles a, b, c;
+0x12 pointer to file + 4 (set by the loader 0x78c62); +0x16 pointer to the morph record; +0x1a bounding radius
(0x79808, for objects added to a scene). Morph record: +0 int32 key k, +4 int32 tick-in-key s, byte +8 shift,
byte +9 ticks per key, +0xa int32 key-set numbers (the loader turns them into pointers), -1 ends.

### 2.2 Morph: 0x78fda (per PIT tick) and 0x78ffe (per frame)

```c
// 0x78fda(EAX = number of keys K, ESI = object), called from the part's PIT callback
if (++s == ticks_per_key) { s = 0; if (++k == K) k = 0; }

// 0x78ffe(ESI = object), called once per drawn frame: every vertex, first 6 words (x, y, z, nx, ny, nz)
A = set[list[k]];  B = set[list[k + 1]];  w = W[s];          // W = 128 words at 0x782d0, copied there by 0x79841
v = A + (int16)(((int32)(int16)(B - A) * w) >> shift);       // imul bp; shrd ax, dx, shift: floor, 16-bit
```
u, v are not interpolated (identical in all sets). Normals are interpolated linearly and not renormalised (|n|
stays 253..255 here). 0x78fda is not in any scope's list; it is described here.

### 2.3 Transform and projection (verified exactly)

Matrix from angles (0x7984e), COS = the 4096-entry int16 table at 0x5599c (static, COS[i] = trunc(1023
cos(2πi/4096)); sin(i) is read as COS[i - 0x400]); every product is `>> 10` (floor):
```
M = | cc·cb            -sc·cb            sb     |      ca = COS[a], sa = COS[a - 0x400], same for b, c
    | sc·ca + sa·sb·cc  cc·ca - sa·sb·sc  -sa·cb |      = Rx(a)·Ry(b)·Rz(c) with the standard rotation matrices
    | sc·sa - ca·sb·cc  cc·sa + ca·sb·sc   ca·cb |
view = [x y z]·M + P            (row vector: X = x·M00 + y·M10 + z·M20 + Px, ...)
```
- Objects drawn directly with 0x789c4 (star, torso, mirror): P = the record's position, in 1/1024 units.
- Objects in a scene drawn by 0x79483 (the surface): P = position << 10 and both M and P are multiplied by the
  camera matrix M(0, 0, 0) = diag(1022/1024) (camera at the origin, angles 0 in all three parts): a uniform scale of
  0.998, negligible.
- Projection (0x79b16): `den = (D - (Z >> 10)) << sh`; `x_s = 160 + trunc(X / den)`, `y_s = 100 + trunc(Y /
  den)` with X, Y, Z in 1/1024 units. So: a pinhole camera at (0, 0, D) looking along -z, screen x right, y down,
  focal length 1024 / 2^sh pixels of the 320x200 frame. Square pixels in this projection; the frame is shown at 4:3.

| part | D [0x57ab6] | sh [0x5594c] | focal length | horizontal field of view |
|---|---|---|---|---|
| surface | 100 (set by 0xa14d) | 3 | 128 px | 102° |
| star | 100 (not set by 0x164dc: left from 0xa14d) | 1 | 512 px | 34.7° |
| torso | 300 (set by 0x16d86) | 1 | 512 px | 34.7° |

- Faces (0x78b08): dropped when the screen-space winding is not positive (back faces) or when the weighted depth
  `(z_a + z_b)/4 + z_c/2` (view z) is >= -D (a near plane at view z = -D, 2D in front of the eye) or <= -86914;
  the rest are sorted by that depth (0x7898d, farthest first) and filled back to front.

### 2.4 Env coordinates and the pixel modes used

Normals are rotated by M of the object's angles alone (0x79a17, same row-vector convention, no camera), giving
n_r with |n_r| ≈ 255·1022 ≈ 260600 for a unit normal. Per vertex (0x4da28 / 0x4d4b4 / 0x4d4e7):
```
eu = (uint8)(128 + (n_r.x >> 12)),  ev = (uint8)(128 + (n_r.y >> 12))     // modes 3, 15, 20, 21: ±63.6 texels
eu = (uint8)(128 + (n_r.x >> 11)),  ev = (uint8)(128 + (n_r.y >> 11))     // mode 2: ±127 texels (wraps)
```
eu/ev and the vertex texture coordinates u/v are interpolated linearly in screen space (engine) and read with
nearest texel. The env coordinate is the normal's component in the screen plane: a map texel at the centre is seen
where the surface faces the viewer (or faces away), the rim of the used disc where the surface is edge-on.

| mode [0x55948] | used for | per-pixel colour index |
|---|---|---|
| 15 | surface | `tex[v][u] + light[(ev + tex[v][u+1]) & 255][(eu + tex[v][u]) & 255]`: the texel plus the light map, whose lookup is shifted by the texel and its right neighbour (the "bump" word is read from selector [0x558dc], which 0xa0ca sets to the texture itself) |
| 3 | star | `env[ev][eu]` (env map = [0x55918], which 0x164dc points at file 49 while drawing) |
| 2 | mirror object | `env[ev][eu]` with the doubled env range |
| 20 | torso + ring on screen | material 0 (torso): `light[(ev + bump[v][2u+1]) & 255][(eu + bump[v][2u]) & 255]` (u doubled mod 256: even column = du, odd = dv; bump = selector [0x558dc]); material 1 (ring): `env[ev][eu]` with env = selector [0x55912] |
| 21 | torso + ring into the reflection map | as mode 20, but drawn into a 256-wide target (stride 256, clipped to x < 256, y < 255) |

Light maps (0x7873a, 256x256 at 0x582d0, selector [0x55918]), verified byte for byte:
```c
// L = 128-byte curve at EBP (0x9df0 for the surface, 0x16c20 for the torso: identical bytes)
dx = x >= 128 ? x - 128 : 127 - x;   dy likewise;     // symmetric about 127.5
d  = min(127, isqrt(dx*dx + dy*dy));                  // 0x7871c's table: floor(sqrt(i)), i < 65536
v  = L[127 - d];                                      // 63 at the centre, 0 from d >= 91
map[y][x] = DL == 4 ? (v >> 1) << 3                   // surface: 32 levels x 8 (light level in bits 3..7)
          : DL == 2 ? v >> 1                          // torso: 0..31, then 0x16f1a adds 0x20 to every byte
```
L (static, 0x9df0): 37 zeros, then 1, 1, 2, 2, 2, 3 ... 42, 43, 44, 45, 47, 48, 49, 50, 52, 53, 55, 58, 63 — a
hand-made curve (no simple formula fits): a sharp peak at the centre and a near-linear slope out to radius 91.

### 2.5 Screen buffer

0x78905 (surface, torso): copy rows 1..197 of the screen buffer [0x558cc] to the VGA and refill them from the
background picture [0x558d2] in the same pass. 0x78948 (star): copy rows 1..197 and clear them to [0x5799d] = 0.
Objects are then drawn into the buffer, so the picture shown is always the previous loop's drawing.

-----------------------------------------------------------------------------------------------------------------

## 3. The textured surface (0xa14d set-up, 0xa0ca loop, 0xa4ac callback, 0xa346 free)

### 3.1 Set-up 0xa14d (music 45:00–45:02)

```c
engine_reset();                                   // 0x787fc
load_object(24, 0x9f80);                          // 0x78c62; morph record 0x9fa0
clear VGA (64000 bytes);  [0x582ac] = 0;  camera position / angles = 0 (0x55836.., 0x55856..)
copy W: 0x9e80 -> 0x782d0 (128 words 0, 1, ..., 127: linear morph weights)
P22 = file 22 -> [0x9e70];  P20 = file 20 -> [0x9e74];  [0x9e78], [0x9e7c] = 768-byte work palettes
A = file 23 -> [0xa06a];  B = file 21 -> [0xa070]  (256x256 maps, values 0..7, with selectors)
[0xa062] = 64 KB: 8 crossfade frames (0xa5c5, below); [0xa066] = 64 KB (never used)
[0xa04e] = 64 KB + selector [0xa052]: the texture the engine reads ([0x582a6] = 0xa04e via 0x79801)
[0x558d8] = 64 KB + selector [0x558dc]; [0x558d8][i] = ([0xa04e][i] - 4) << 2 for i < 0xffff
      // dead: [0xa04e] is still uninitialised here, and 0xa0ca replaces [0x558dc] by [0xa052]
[0x5594c] = 3;  [0x57ab6] = 100;  light map 0x7873a(DL 4, L = 0x9df0)
scene 0xa036 (capacity 100) with the one object 0x9f80 (0x79111, 0x7916b)
background = file 25 -> [0x558d2]
```
Crossfade frames (0xa5c5), each 64 x 128 bytes from the top-left 64 x 128 of the maps (only that corner is used):
```c
F[0] = A;  F[7] = B;
F[k][i] = A[i] + (((int8)(B[i] - A[i]) * k) >> 3)   (k = 1..6, 16-bit arithmetic shift, low byte)
```
So the eight frames step 0, 1/8 ... 6/8 and then jump to B (not 7/8).

### 3.2 Loop 0xa0ca and callback 0xa4ac

```c
set_pit_callback(0xa4ac);
[0x558dc] = [0xa052];               // the "bump" selector of mode 15 = the texture itself
morph_tex(&[0x9e78]);               // 0xa530 with p = 63: palette ~P20, texture frame 7 (= map 21)
[0xa046] = 0;  [0x55948] = 15;
do {
    phase();                        // 0xa3c9, below
    wait_vsync_start();             // 0xa71b
    if ([0xa3c4]) set_palette(0, 256, [0x9e78]);       // 0xa729
    copy_and_clear_to_background(); // 0x78905
    object_state();                 // 0xa656, below
    render_scene(0xa036);           // 0x79483
    morph(0x9f80);                  // 0x78ffe: vertices for the NEXT frame
} while (![0xa3c8]);
remove_pit_callback();

pit 0xa4ac:  t = ++[0xa046]; ++[0xa725];               // both count from the install ([0xa046] reset before any tick)
             p = [0xa076] += dir [0xa07a]; if (p == 63 || p == 0) dir = -dir;
             morph_step(33 keys, 0x9f80);              // 0x78fda
             if (++f [0xa52c] >= 338) { f = 0; if (++key [0xa528] > 5) key = 0; }
```

### 3.3 Motion (0xa656): six keys of 338 ticks, linear

Key table at 0xa082: 6 records of 6 int16 (x, y, z, a, b, c), static:

| key | x | y | z | a | b | c |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | -1310 | 120 | 1960 | 320 |
| 1 | 0 | 0 | -1430 | -240 | 1700 | -60 |
| 2 | 0 | 0 | -1110 | -260 | 2360 | -60 |
| 3 | 20 | 20 | -1290 | 300 | 2220 | 260 |
| 4 | 60 | 80 | -1290 | 380 | 1780 | 0 |
| 5 | 60 | 80 | -1290 | -280 | 2060 | 0 |

```c
key = (t / 338) % 6;  f = t % 338;  A = K[key];  B = K[(key + 1) % 6];
value = A + trunc((B - A) * f / 338)          // idiv: truncation toward zero; 16-bit wrap for angles
```
A 2028-tick loop (28.9 s; the part lasts 2157 ticks, so keys 0 → 1 are seen twice). b ≈ 1700..2360 (150°..207°)
turns the surface's face towards the eye; a and c tilt it by up to ±33° and ±28°; the surface is 1110..1430 units
away (plus D = 100): the grid (3200 x 2000) fills most of the 102° view.

### 3.4 Shape: 33 morph keys of 63 ticks

Morph record 0x9fa0: shift 6, 63 ticks per key, 33 keys, key-set list (static, 0x9faa, 34 entries):
`0 0 0 1 0 0 0 5 1 0 0 0 5 1 2 3 7 1 3 1 5 7 4 2 1 6 4 8 1 4 5 8 5 0`. With W = 0..127 and shift 6 the weight is
s/64 (s = 0..62; the step from 62/64 to the next key is 2/64). One cycle is 2079 ticks (29.7 s); sets 0 = flat
wall, 1..8 = face reliefs (set 9 in the file is unused). The flat phases (0 → 0) give the "turning wall".

```c
k = (t / 63) % 33;  s = t % 63;
vertex = S[seq[k]] + (((S[seq[k+1]] - S[seq[k]]) * s) >> 6)        // positions and normals, 2.2
```
The loop renders before it morphs, so each frame shows the shape of the previous frame's tick (one frame, ~43 ms,
behind the object motion in the capture).

### 3.5 Palette, texture crossfade and fades (0xa3c9 / 0xa530 / 0xa491)

p = [0xa076]: 63 until the first frame with t >= 0x437 (1079) sets dir = -1 (t1 = that frame's t: 1080 in the
capture), then a triangle wave:
```c
p(t) = t <= t1 ? 63 : abs(63 - ((t - t1) % 126))          // 63 -> 0 -> 63, period 126 ticks (1.80 s)
```
0xa530 (per frame, phases 2 and 3): palette and texture follow p:
```c
pal[i] = P22[i] + (((int8)(P20[i] - P22[i]) * p) >> 6);   // i = 0..767, 6-bit values, floor
texture = F[p >> 3] tiled 4 x 2 into 256x256 (0xa57d: 128 rows of 64 bytes, each written at columns 0/64/128/192
          of rows r and r + 128)
```
Phases by t = [0xa046] (all palettes uploaded from colour 0, 256 colours, after the retrace wait):

| t | ticks | music | what |
|---|---|---|---|
| 0..63 | 13771.. | 45:02 | fade in: pal = P20 · t >> 6 (texture F[7] = map 21, the stone frieze) |
| 64..1078 | | 45:17 | no upload: the palette stays at the last fade step drawn (P20 · 63 >> 6 in the capture, frame-rate dependent) |
| 1079..2093 | 14889.. | 4c:19 | crossfade: palette and texture follow p (stone frieze with grey palette 20 ↔ Celtic knots with blue-purple palette 22) |
| 2094..2156 | 15904.. | 53:1b | fade out: pal = P20 · b >> 6, b = 63 - (t - 2094) (the texture keeps following p, the palette is the stone palette whatever p is; the morph palette is computed into [0x9e7c] and not used) |
| 2157 | 15967 | 54:00 | b = 0: [0xa3c8] = 1, the loop ends |

Palette layout (files 20 and 22, static 6-bit ramps): colour = 8·L + texel, L = light level 0..31, texel 0..7;
block 0 (colours 0..7) is the background picture's colours (identical in both palettes; colour 7 = (36, 63, 0)
is not used). Light levels in use are about 4..31 (the env coordinates stay within 64 texels of the centre, plus
the bump offset of up to 7 + 7).

### 3.6 Time bases (surface)

| variable | base |
|---|---|
| object position / angles | PIT ticks (key every 338, linear by tick) |
| shape | PIT ticks (key every 63, linear by tick), sampled once per drawn frame, one frame late |
| p (palette / texture crossfade) | PIT ticks, start t1 = first frame with t >= 1079 (1080 in the capture) |
| fades | PIT ticks, sampled per drawn frame (fade-in end level frame-rate dependent: 63/64 in the capture) |
| part end | t = 2157 (music 54:00/54:01) |

-----------------------------------------------------------------------------------------------------------------

## 4. The chrome star (0x164dc, callback 0x16646)

### 4.1 Set-up and loop

```c
engine_reset();  camera = 0;
copy W: 0x15860 -> 0x782d0   // 128 words: an ease curve 0, 3, 6 ... 63, 64 (x25), 65 ... 122, 125 (static)
env = file 49 -> [0x5590e] / selector [0x55912]
load_object(51, 0x16360);    // morph record 0x16380: shift 7, 128 ticks per key, keys 0 1 2 0
set_palette(0, 256, 0x15b60);  // static zeros: black
t = [0x163ae] = 0;  [0x5594c] = 1;  [0x55948] = 3;  [0x164ba] = 0;  set_pit_callback(0x16646);
do {
    if ([0x164ba]) palette_levels(0x15e60 -> 0x15b60);  // 0x168a3, 4.4
    wait_vsync_start();
    if ([0x164ba]) set_palette(0, 256, 0x15b60);
    copy_and_clear();                                    // 0x78948
    carousel_frame((t >> 1) & 63);                       // 0x16acc, 4.5
    morph(0x16360);                                      // 0x78ffe (before the draw: no lag)
    [0x55918] = [0x55912];  draw_object(0x16360);  restore [0x55918];   // 0x789c4, mode 3, env map file 49
} while ([0x164d3] != 2);
remove_pit_callback(); free file 49, its selector and the carousel frames [0x163b2]; engine_free();   // 0x788dd
```
D = 100 here only because 0xa14d set it (2.3).

### 4.2 Callback 0x16646 (verified tick for tick)

Initial values (static): position (0, 1500000, -3750000) (1/1024: y = 1464.8, z = -3662.1 units), angles
(0xfc00, 0, 0), b speed B = 9 [0x164c1], interval I = 206 [0x164c3], next beat N = 560 [0x164c7], decrement
Dd = 4 [0x164cf], pulse counter n = 0 [0x164cb], phase [0x164d3] = 0, vz = 0 [0x164d4], fade index [0x164d8] = 0.

```c
upd = 0;  t++;
a = (a + 10) & 0xfff;  b = (b + B) & 0xfff;  c = (c + 6) & 0xfff;
morph_step(3 keys);                                   // 128 ticks per key: 384-tick shape cycle (5.48 s)
if (t < 128)            { upd = 1; level = (t, t, t); shift = 7; }            // fade in from black
else if (t <= 158)      ;
else if (t < 358)       y = 1520000 - (t - 158) * 7500;                       // rises to y = 27500 (26.9 units)
else if (n) {           // pulse
    n--; upd = 1; shift = 6; a = (a + 18) & 0xfff;    // spins faster during a pulse
    level.r = level.g = T2[n] + 42;  level.b = (T1[n] >> 1) + 48;
} else if (phase != 1 && t == N) {                    // beat
    n = 63;  I -= Dd;  Dd += 6;  B += 2;
    if (I < 80) { I = 80; phase = 1; fi = 0; }
    N += I;
}
if (phase == 1) {                                     // flight into the camera
    vz += 185;  z += vz;
    if (vz > 9200) {
        shift = 6;  fi++;  level = (T3[fi & 255],) x 3;  upd = 1;
        if (fi >= 255) { fi = 255; phase = 2; level = (0, 0, 0); }
    }
}
```
T1 = round(32 cos(2πn/64)) at 0x15960, T2 = round(22 cos(2πn/64)) at 0x159e0 (64 int16 each, verified), T3 = 256
int16 at 0x15660: 0, 63, 61, 58 ... 0 (index 24), up to 25 (index ~47), down, 10, ..., 4, ... 1, 0: a bouncing
fade-out (static, no formula). Resulting schedule:

| event | t | frame | music |
|---|---|---|---|
| fade in from black | 0..127 | 16644–16772 | 58:21–59:1c |
| rise from y = 1477 units (centre 200 px below the screen centre, the top of the star in the lower part of the screen) to 26.9 | 159..357 | 16803–17003 | 59:26–5b:08 |
| pulses (63 ticks each); B after the beat | 560 (B 11), 762 (13), 954 (15), 1130 (17), 1284 (19), 1410 (21), 1502 (23) | 17205, 17407, 17599, 17775, 17929, 18055, 18147 | 5c:1c, 5d:2f, 5f:0f, 60:19, 61:1d, 62:16, 63:05 |
| flight: z = z0 + 185·k(k+1)/2 after k = t - 1502 ticks | 1503.. | | |
| bounce fade (T3), the star crosses the near plane | 1552..1805 | 18197– | 63:16– |
| end (black) | 1806 | 18451 | 65:0a |

The beats are tick-timed with shrinking intervals (202, 192, 176, 154, 126, 92), not music-synced. A pulse is a
dip, not a flash: levels below 64 darken (red and green down to 20/64, blue to 32/64 at mid-pulse), so the star
turns dark blue-violet and back within 0.9 s.

### 4.3 Palette 0x168a3

```c
k = 1 << shift;           // shift 7 for the fade in, 6 afterwards
for each colour i, channel ch:  c = SRC[i][ch]  (SRC = 0x15e60, 768 bytes, static, the env map's palette)
    l = level[ch];
    out = l < k ? (c * l) >> shift                    // towards black
                : c + (((63 - c) * (l - k)) >> shift)  // towards white (never used: levels stay < 64 here
                                                       //  except 64 at the pulse ends = c exactly)
```
(8-bit signed `imul bl`: the levels used are < 128, so no sign problem.) After the fade in the palette stays at
level 126/127 of 128 (identical) until the first pulse. The palette is computed in the main loop while the PIT
callback may change the levels: in 6 frames of the capture the first colours use one tick's levels and the rest the
next tick's (frames 17646–17647, 17978–17981).

### 4.4 Shape and env map

Three key shapes of file 51 (sets 0, 1, 2, back to 0), weight W[s]/128 with the static ease table W (0x15860):
fast start, a plateau at exactly one half for s = 52..76, then on to 125/128 and a small jump to the next key. So
the star eases half-way, holds the in-between shape for 25 ticks, and eases on. Env map = file 49 (65535 bytes,
256x256 minus the last byte, indices 0..254), palette 0x15e60.

### 4.5 The carousel layer (prepared by 0x16945, parts3d-a)

0x16945 renders 64 frames of a ring of 16 env-mapped objects (file 50, env map file 49) at start-up and stores each
from screen offset 0x6180 (row 78) as runs: `word skip, word count, count bytes`, `skip = -1` or `count = -1`
ends (the bytes went through the table at 0x15a60 when encoded). 0x16acc draws frame `(t >> 1) & 63` (35 frames a
second, 128-tick loop) into the cleared screen buffer at offset 0x6180 before the star, so the star always covers
it. The object positions repeat every 32 frames (0x16ba1: `ang = 16·frame + 256·i`, x = 256 cos, z = 256 sin -
190, y = 100); details belong to notes-parts3d-a.

### 4.6 Time bases (star)

| variable | base |
|---|---|
| angles, y, z, beats, pulse and fade levels, morph | PIT ticks (state stepped exactly as above) |
| carousel frame | PIT ticks: (t >> 1) & 63 |
| palette | computed and uploaded once per drawn frame from the current levels (race visible in 6 frames) |

Nothing else is per frame. 0x16945's prepared frames are a fixed 64-frame loop (a remake can render the carousel
live with the same parameters).

-----------------------------------------------------------------------------------------------------------------

## 5. The torso, the ring and the mirror (0x16d86, 0x16f7c, callback 0x17513, 0x174bd)

### 5.1 Set-up 0x16d86 (music 6b:31–6c:00)

```c
engine_reset();
load_object(45, 0x16ca8);   // mirror
load_object(46, 0x16cc8);   // torso + ring
[0x16cfa] = [0x16cda];      // object 0x16ce8 = second instance of 0x16cc8 (same vertices): the reflection
load_object(46, 0x16d08);   // untouched reference copy of the ring vertices (never drawn)
P42 = file 42 -> [0x16ca4];  [0x16ca0] = 768-byte work palette
ringenv = file 29 -> [0x5590e] / selector [0x55912];  first 65536 bytes: b = (b >> 1) + 0x60     // 0x60..0x7f
bump = file 43 -> [0x16d72] / selector [0x16d76]
bg = file 47 -> [0x558d2];  every byte += 0xe6;  row 0 = 0                                       // 0xe6..0xed
mirrorenv = file 44 -> [0x16d4a] / selector [0x16d4e]
[0x16d78] (64 KB, selector [0x16d7c]) = (uint8)(bump - 4) >> 1      // dead (only the mode-2 mirror gets it)
bump[i] = (uint8)(bump[i] - 4) << 3                                 // t = 0..7 -> -32, -24 ... 24 (as bytes)
[0x5594c] = 1;  [0x57ab6] = 300;  [0x582ce] = 0
light map 0x7873a(DL 2, L = 0x16c20), then += 0x20 to every byte    // 0x20..0x3f
[0x16d50] = 0x12710 bytes + selector [0x16d54]: the reflection env buffer
faces of 0x16cc8: material 0 for faces 0..328 (torso), 1 for 329..458 (ring)   // 0x16f46
```
File 43 is the same 256x256 map as file 21 (the stone frieze, values 0..7); files 29 and 44 are 0x102ff bytes:
256x256 plus 767 bytes of an unused palette.

### 5.2 Loop 0x16f7c

```c
torso z = -0x23f000 (-2300 units), x = 0, y = 0x113000 (1100 units);  ringy [0x16d7e] = 200;
set_pit_callback(0x17513);  t = [0x16d56] = 0;
do {
    phase();                               // 0x17084, 5.3
    wait_vsync_start();  if ([0x1707e]) set_palette(0, 256, [0x16ca0]);
    copy_and_clear_to_background();        // 0x78905 (file 47)
    ring_vertices();                       // 0x17342: v[177..241] = ref + (ringx, ringy << 4, ringz), normals kept
    band(+0x20);                           // 0x17459: if -100 < ringy < 100: light map rows 0x88 + ringy .. +15 += 0x20
    if (mirror [0x1707f]) {
        reflection();                      // 0x173a1, 5.4
        [0x55948] = 2;  [0x55918] = [0x16d54];  draw_object(0x16ca8 mirror);  restore [0x55912], [0x55918];
    }
    [0x55948] = 20;  [0x558dc] = [0x16d76] (bump);  draw_object(0x16cc8 torso + ring);
    band(-0x20);                           // 0x1748b
} while (![0x17081] && music_order <= 0x82);    // ends by the fade (t = 2447, 7c:2f); 0x82 is never reached
remove_pit_callback();
```
Draw order: mirror first, torso and ring over it (each object sorted internally only).

### 5.3 Phases (0x17084, per drawn frame) and the callback (0x17513, per tick); verified frame by frame

COS as in 2.3; `sin(i)` = COS[(i - 0x400) & 0xfff]; `>>` is an arithmetic shift (floor).
```c
// 0x17253 "wobble"
k = t - 496;
torso.a = sin(k << 6) >> 2;                  // ±255 (±22.4°), period 64 ticks
ringx   = sin(k << 7) >> 3;                  // ±127 units, period 32 ticks
ringz   = COS[(k << 7) & 0xfff] >> 3;        // ±127: the ring circles about the torso axis
ringy   = sin((t - 304) << 4) >> 5;          // ±31 (x 16 = ±496 units), period 256 ticks

// 0x17084
if (t < 64)          { upload = 1; pal = P42 * t >> 6; }                                    // fade in
else if (t < 160)    { if (!copied) { upload = 1; pal = P42; copied = 1; }                  // that frame only
                       else { upload = 0;
                              torso.b = 0x800 + trunc(-0x800 * (t - 64) / 96);             // half a turn
                              torso.y = 0x113000 + trunc(-0x113000 * (t - 64) / 96); } }  // rises 1100 -> 0
else if (t < 304)    { spin = 1;  ringy = 200 + trunc(-200 * (t - 160) / 144); }           // ring rises 3200 -> 0
else if (t < 496)    { ringx = ringz = 0;  ringy = sin((t - 304) << 4) >> 5; }
else if (t < 736)    wobble();
else if (t < 1600)   { wobble(); mirror = 1; }
else if (t < 2320)   { mirror = 2; wobble(); }
else                 { upload = 1;  f = 2447 - t;
                       if (f < 127) { if (f <= 0) { end = 1; f = 0; }                        // (t = 2320: nothing)
                                      pal = P42 * f >> 7;  mirror = 2;  wobble(); } }        // fade out

// 0x17513, every tick
t++;
if (spin) torso.b -= 12;                                     // 0.205 turns a second
if (mirror == 1) { mirror.b -= 7; mirror.a += 1; mirror.c += 3;
                   torso.x  = min(torso.x  + 0x307,  0x64000);   // 0 -> 400 units, 0.757 per tick
                   mirror.x = min(mirror.x + 0xda2, -0x4b000);   // -2000 -> -300, 3.41 per tick
                   mirror.z = min(mirror.z + 0x1e4d, -0x271000); } // -6000 -> -2500, 7.58 per tick
if (mirror == 2) { mirror.b -= 8; mirror.a += 1; mirror.c += 4;
                   torso.x -= 0x5c1;  mirror.x += 0x2ba; }       // -1.44 and +0.68 units per tick, no limit
```
Initial mirror record (static, 0x16ca8): position (-0x1f4000, 0, -0x5dc000) = (-2000, 0, -6000) units, angles 0.
Positions are 1/1024 units (direct draws). When the mirror phase starts, 0x173a1 also sets the torso z to
-0x258000 (-2400 units), every frame: a jump of 100 units away at t = 736.

| event | t | frame | music |
|---|---|---|---|
| fade in | 0..63 | 19503–19566 | 6c:00–6c:16 |
| torso rises and turns half a turn | 64..159 | 19567– | 6c:16 |
| torso spins; ring rises from y = 3200 | 160..303 | 19663– | 6d:06 |
| ring band in the light map appears (ringy < 100) | 232 | 19735 | 6d:1d |
| ring slides up and down | 304.. | 19807– | 6e:05 |
| wobble (torso tilt, ring circling) | 496.. | 20000– | 6f:16 |
| mirror flies in, torso moves right, reflection drawn | 736.. | 20240– | 71:06 |
| mirror drifts right, torso left | 1600.. | 21104– | 77:05 |
| fade out over 127 ticks | 2321..2447 | 21825–21951 | 7c:05–7c:2f |

Frame-rate dependent values in the capture: the rise is updated per frame, and the last frame before t = 160 had
t = 158, so the torso stays at y = 23467 (22.9 units) instead of 0 for the rest of the part, and its angle b is 43
(not 0) when the spin starts; the spin starts at the first frame with t >= 160 (t = 160 in the capture):
b(t) = 43 - 12 (t - 160).

### 5.4 The reflection (0x173a1) and the mirror

```c
memcpy(envbuf [0x16d50], file 44, 65536);          // pink shaded sphere with a highlight (values 0..31)
torso.z = -0x258000;
inst (0x16ce8): x = torso.x >> 1 (arithmetic), y = torso.y, z = torso.z,
                a = torso.a, b = 0x800 - torso.b, c = torso.c      // mirrored about the y axis
[0x55948] = 21; [0x558dc] = bump; draw inst into envbuf (target selector [0x558d0] = [0x16d54])
```
Mode 21 draws the torso (light map + bump) and the ring (env map 29) with the normal projection (centre 160, 100 of
a 320x200 frame) into the 256-wide buffer, clipped to x < 256, y < 255: the reflected torso lands right of the
map's centre and is cut at the right edge. The mirror (file 45) is then env-mapped with this buffer in mode 2
(normal >> 11: the whole 256x256 map is used and wraps), so it shows the pink sphere picture with the live
torso and ring in it.

### 5.5 Colours

Palette 42 (static file): 0x00–0x1f pink ramp (the mirror env picture, file 44), 0x20–0x3f blue-grey stone ramp
(torso light 0x20 + L/2), 0x40–0x5f dark-to-light blue ramp (torso pixels in the ring band: +0x20), 0x60–0x7f red
ramp (ring env map 29), 0xe6–0xed background (file 47: 0xe6 black, 0xe7..0xec dark green-greys, 0xed black);
0x80–0xdf and the rest are unused by this part. Fades: in P42 · t >> 6 (t < 64), out P42 · f >> 7 (f = 126..0).

The band (0x17459 / 0x1748b): +0x20 on 16 rows of the light map starting at row 0x88 + ringy, i.e. for surface
points whose screen-plane normal component (plus the bump offset) points 8..23 texels below the map centre plus
ringy: those torso pixels switch to the blue ramp. The band moves with the ring's height, so the blue streaks on
the torso follow the ring's up-and-down motion. It is added before the frame is drawn and removed after.

### 5.6 Time bases (torso)

| variable | base |
|---|---|
| spin, mirror motion, torso x | PIT ticks (callback) |
| rise (y, b), ring height, wobble, phases, fades | PIT ticks, evaluated once per drawn frame (the rise's end value is frame-rate dependent, 5.3) |
| reflection map | rendered every drawn frame from the current state (no own time) |
| end | t = 2447 (fade), music 7c:2f |

### 5.7 Free 0x174bd

Frees the palettes, the background, the reflection buffer, the bump maps, file 44 and file 29 (with selectors) and
the engine (0x788dd). 0xa346 does the same for the surface part (its two work palettes, files 20, 22, 21, 23, the
frame and texture buffers, [0x558d8], the scene, the background).

-----------------------------------------------------------------------------------------------------------------

## 6. Pictures and textures: hand-drawn or generated, and how they are used

| data | what | made how (assessment) | used how | true-colour redraw |
|---|---|---|---|---|
| file 25 (320x200, values 1..6) | blue-violet clouds | painted, dithered to 6 colours | drawn as-is (copied into the screen buffer every frame) with colours 1..6 of palettes 20/22 | yes: a redrawn background picture |
| file 21 (= file 43), 256x256, values 0..7 | stone frieze (Greek key bands), 64 x 128 tile repeated | hand-drawn grey-level pixel art | surface: texel added to the light level (palette ramp 8·L + texel) and used as the bump offset of the light lookup; torso: only as a bump map ((t - 4)·8 offsets), never as colour | yes, as a grey-level (height / brightness) map 0..7, not as colours: the same value drives the colour ramp and the bump. Only the top-left 64 x 128 is used |
| file 23, 256x256, values 0..7 | Celtic knot pattern (top-left 64 x 128 used) | hand-drawn grey-level pixel art | surface texture crossfaded with file 21 by index (8 frames) | yes, grey-level as file 21 |
| palettes 20, 22 | 32 light levels x 8 texel levels (grey; blue-purple) + the cloud colours | hand-made ramps | 2D colour table, crossfaded with p, faded | keep as data: colour(L, texel) table, interpolate bilinearly |
| file 49 (256x256, 255 colours) + palette 0x15e60 (in the exe) | glowing sphere with a star / knot pattern and coloured halos | painted or rendered picture | env map of the star (and the carousel objects), palette levels pulse and fade it | yes: a true-colour env map; apply the level transform (4.3) per channel |
| file 44 (256x256, values 0..31) | pink shaded sphere with a highlight | painted or rendered picture | base of the mirror's env map; the reflected torso and ring are rendered into it each frame | yes, but the live reflection must be rendered by the remake on top |
| file 29 (256x256, values 0..63) | red "explosion" glow | painted or rendered picture | env map of the ring through the red ramp 0x60–0x7f ((b >> 1) + 0x60) | yes (grey → red ramp, or true colour) |
| file 47 (320x200, values 0..7) | dark green landscape / foliage | painted, dithered | drawn as-is (colours 0xe6..0xed) | yes |
| palette 42 | ramps listed in 5.5 | hand-made | direct colour of every index | keep as data |
| light curve 0x9df0 / 0x16c20 | 128-byte radial profile | hand-made curve | generates the light maps (0x7873a) | generated: evaluate L(r) continuously |
| object files 24, 45, 46, 51 (50) | 3D models and morph shapes (BARTi) | modelled | geometry | — |

No colour cycling; all palette changes are fades, crossfades and level transforms of whole palettes.

-----------------------------------------------------------------------------------------------------------------

## 7. Static data (read from STARS.EXE or the overlay at run time)

| what | where | size / format |
|---|---|---|
| surface object | file 24 | `B!`, 529 verts, 968 faces, 10 key sets |
| surface key-set sequence | 0x9faa | 34 int32 (morph record at 0x9fa0: shift 6, 63 ticks) |
| surface motion keys | 0xa082 | 6 x 6 int16 (x, y, z, a, b, c) |
| linear morph weights | 0x9e80 | 128 int16 (0..127) |
| light curves | 0x9df0, 0x16c20 | 128 bytes each (identical) |
| surface palettes | files 20 (stone), 22 (knots) | 768 bytes, 6-bit |
| surface textures | files 21, 23 | 256x256 bytes, values 0..7 |
| surface background | file 25 | 320x200 bytes |
| star object | file 51 | `B!`, 394 verts, 784 faces, 3 key sets; morph record 0x16380 (shift 7, 128 ticks, keys 0 1 2 0) |
| star object record | 0x16360 | position (0, 1500000, -3750000), angles (0xfc00, 0, 0) |
| star ease weights | 0x15860 | 128 int16 |
| star env map / palette | file 49 / 0x15e60 | 65535 bytes / 768 bytes |
| star beat parameters | 0x164c1 (B 9), 0x164c3 (I 206), 0x164c7 (N 560), 0x164cf (Dd 4) | int16 / int32 |
| pulse tables | 0x15960 (T1), 0x159e0 (T2) | 64 int16, = round(32 / 22 cos(2πn/64)) |
| bounce fade | 0x15660 (T3) | 256 int16 |
| carousel | 0x16945's output (parts3d-a); objects file 50; encode table 0x15a60 | |
| torso / ring object | file 46 | `B!`, 242 verts, 459 faces (ring = verts 177..241, faces 329..458) |
| mirror object | file 45 | `B!`, 192 verts, 384 faces; record 0x16ca8: (-0x1f4000, 0, -0x5dc000), angles 0 |
| torso palette | file 42 | 768 bytes |
| torso maps | files 43 (bump, = 21), 29 (ring env), 44 (mirror env), 47 (background) | see section 6 |
| COS table (engine) | 0x5599c | 4096 int16, trunc(1023 cos) |

Code constants (allowed in source): 338, 63, 33, 6, 0x437, 0x82e, 126; 7500, 0x173180, 158, 358, 185, 9200, 80,
6, 2, 18, 42, 48; 0x113000, 0x800, 96, 200, 144, 304, 496, 736, 1600, 2320, 2447, 12, 7, 3, 8, 4, 0x307, 0x64000,
0xda2, -0x4b000, 0x1e4d, -0x271000, 0x5c1, 0x2ba, -0x23f000, -0x258000, 0x88, 0x20, 0xe6, 0x60; D and sh per part.

-----------------------------------------------------------------------------------------------------------------

## 8. Shortcuts a high-resolution remake should evaluate exactly

- **Morph and motion steps**: evaluate the key interpolations (surface motion 338 ticks, surface shape 63 ticks,
  star shape with W) with the fractional tick instead of integer ticks; truncating divisions become exact ones. Use
  one time for shape and motion (the surface's one-frame shape lag is a loop-order artefact). Interpolate the star's
  W table linearly between entries.
- **Normals**: the morph interpolates per-vertex normals linearly from 8-bit-scale integers; a remake can
  interpolate them and renormalise per pixel (the key shapes are hand-made, so keep their stored normals as the
  source, not recomputed ones, unless they differ visibly).
- **Env / light coordinates**: per-vertex, integer-truncated (`>> 12`), interpolated affinely in screen space, nearest
  texel. Evaluate per pixel from the interpolated normal: env offset = 63.6·n̂ (modes 3, 15, 20, 21) or 127·n̂
  (mode 2, wrapping) texels from the map centre (127.5, 127.5), with a filtered (redrawn) env map.
- **Light map**: L[127 - floor(sqrt(dx² + dy²))] with integer dx, dy, then quantised to 32 levels: use L(r)
  interpolated from the 128-entry curve at the continuous radius, and keep the level continuous. The colour then
  comes from the palette ramp at a fractional level (interpolate between rows of palettes 20/22, or between entries
  of the 0x20..0x3f / 0x40..0x5f ramps of palette 42).
- **Bump offsets**: the light lookup is shifted by (texel(u, v), texel(u + 1, v)) on the surface and by
  (bump[2u], bump[2u + 1]) on the torso, in light-map texels; evaluate them with filtered texture values (the texel
  neighbour is one texel of the 256-wide texture: scale with the redraw's resolution so the offset stays the same
  size in texture space). The torso's offsets alternate between the even and odd columns of the stone map: keep
  that pairing (it is part of the look) but sample it filtered.
- **Texture crossfade**: 8 frames by index (weights 0..6/8, then 1); a remake can blend the grey-level maps
  continuously (weight p/64, or the original's steps) and look the colour up afterwards.
- **Palette maths**: `>> 6` / `>> 7` integer fades and level transforms, 6-bit values: evaluate in full precision.
- **The reflection map**: rendered at 256x256 with the 320x200 projection; render it at a higher resolution with the
  same mapping (map texel = screen pixel of the 320x200 projection, x < 256, y < 255) and keep the clipping.
- **Near plane**: faces whose weighted depth reaches -D are dropped whole (visible when the star flies into the
  camera); clip per pixel instead.
- **Pre-rendered carousel**: 64 frames at 320x200 played at 35 frames a second; render it live.

-----------------------------------------------------------------------------------------------------------------

## 9. Models (`models/parts3d-b/`, see its README)

| script | checks | result |
|---|---|---|
| model_tex.py LOG | surface: motion keys, morph key/tick, crossfade triangle | 700 / 700 frames exact |
| model_star.py LOG | star: angles, y, z, morph, beat schedule, levels | 1103 / 1103 frames exact |
| model_torso.py LOG | torso: phases, callback, frame-rate dependent rise | 1055 / 1055 frames exact |
| palette_check.py | palette formulas of the three parts vs the capture | every distinct palette produced (star: 6 race frames explained) |
| morph_check.py DUMP OBJ FILE | 0x78ffe vertex morph | bit-exact |
| transform_check.py DUMP OBJ ... | matrix, projection, env coordinates | exact (0 error) |
| lightmap_check.py | 0x7873a light maps (DL 4 and DL 2 + 0x20 + band) | byte-exact |
| texture_check.py DUMP | 0xa5c5 crossfade frames and 0xa57d tiling | exact |
| render_check.py DUMP MODE | per-pixel formulas of modes 15, 3, 20 with an approximate rasteriser | 73–84 % / 61 % / 54–59 % exact pixels, visually identical |
| obj24.py, textures_a0ca.py, pictures.py | object and picture viewers (write PNGs to out/) | |
| run_stars_b.py | copy of run_stars.py with `--log-at` and `--dump-at` | |

## 10. Not determined / guesses

- The exact rasteriser rules (span start, rounding of the interpolants): engine scope; my approximate rasteriser
  explains the pixels only up to texel rounding.
- Whether the surface's self-bump (bump word read from the texture itself) was intended: the prepared bump map
  [0x558d8] is computed from uninitialised memory and then never used (0xa0d4 replaces its selector).
- The light curve (0x9df0), the ease table W (0x15860) and the bounce table T3 (0x15660) have no simple formula
  that I found; they are read as data.
- "Painted or rendered" for files 25, 29, 44, 47, 49 is an assessment from the pictures, not from the code.
- The carousel's frame content and its encode table 0x15a60 are parts3d-a's (0x16945, 0x16ba1, 0x16b1e).
