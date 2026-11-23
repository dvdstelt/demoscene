# Scope "flight": the flight over the landscape (refreshes 7063..8467, music 19:63..23:63)

Init 0x402948 (with 0x401330 camera, 0x401450 grid and height field, 0x40136c light, 0x4018d0 ships, 0x4023d4 sun
and flares: 0x402294, 0x402330), start 0x402c28, tick routine 0x4021f8 (0x401dbc ships, 0x4020f8 camera), frame
routine 0x4028e4 (0x40276c objects, 0x402638 camera jump, 0x4024d0 flare visibility, 0x401ab8 grid, 0x401c64 fog),
end 0x402c7c. Addresses are linear (NOTES.md). "Refresh" = screen refresh (70.086 Hz), T = the part tick counter
[0x4248c8]. Calls into SurRender are described by what goes in and what comes out (notes-engine.md describes the
engine); the sprite drawer 0x415878 that draws the sun and the flares is decompiled here (section 7.4); the temple
and the credits use it too.

In short: the landscape is **polygons**, not voxels: a SurRender mesh of 19 × 19 vertices (18 × 18 cells of 512
units, 648 triangles) that the frame routine re-centres on the camera every frame, heights from a 128 × 128 height
field the init **computes** from a sum of sines (map.raw is not a height map but the 512 × 512 texture, an aerial
photo, spanning 18 × 18 cells), drawn by the engine with affine texture mapping, Gouraud light (map.shd) and fog per
vertex (map.fog) towards the flat sky colour 0xdd. river.raw is the environment map of the ships. The sun and the 8
lens flares are 2D sprites at one 3D point 143,360 units in −x from ship 0.

Checked against the emulator (scripts in `models/flight/`, section 11), over the whole part (1404 refreshes, 2024
drawn frames, from snapshot f7063):

- the height field and its light values (128 × 128 × 2 ints): **identical** to the snapshot;
- the motion (3 ships, camera, control point: positions and matrices) at every tick routine call: **bit-exact**
  (x87 extended precision modelled, section 5.4); a plain-double model with the integer rule of 5.4 stays within
  1e-11;
- the random camera jump: the one jump of the capture at the same frame with the same result;
- the landscape grid handed to the engine (object position, 361 vertices: x, y, z, u, v, light, fog; which of the 648
  faces get the fogged rasterizer): **identical** in all 2024 frames;
- the sun's camera-space coordinates and the flare visibility: identical in all 2024 frames;
- the sprite drawer (sun and flares): 2557 draws of refreshes 7063..7300 compared buffer before / after: **all
  identical**;
- the landscape picture: a per-pixel model (section 7.2) gives the same image as the original (side-by-side
  comparisons `cmp_<frame>.png` written by `landrender.py`); 68 % of the land pixels have exactly the same index (21
  frames, every 100th), the rest differ by a neighbouring texel / shade level (the engine's fixed-point
  interpolation, notes-engine.md).

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

A flight over a fogged, textured landscape under a flat blue sky with a sun and a lens flare; three small
environment-mapped ships fly ahead of a chase camera that follows the first ship. Script entries (PARTS.md):

| refresh | music | entry | what |
|---|---|---|---|
| 7063 | 19:63 | 67 0x409e90, 68 **0x402c28** | flower off; flight on: both screen buffers cleared to 0, palette `flight/map.pal` faded in from black over 60 refreshes, tick 0x4021f8, frame 0x4028e4 |
| 8386 | 23:48 | 69 0x400438 | fade to black (40 refreshes; black from 8425) |
| 8468 | 23:63 | 70 0x400498, 71 **0x402c7c**, 72 0x407f90 | white flash; flight off (routines 0, the height field freed); water |

The first flight frame is shown at 7064 (palette at 1/60). 2024 frames are drawn in the 1404 refreshes (1.44 per
refresh at 150 MIPS; one frame shown per refresh). Contents over time (`sheet.py full.frames out.jpg --every 1
--from-time 100 --to-time 121`): the ships far ahead with the sun and flares at the top right (7064..7312),
approaching the ships (7400..7860), a close pass of a ship and the camera jump (7861..7864, section 6.2), again sun
and flares (8022..8052, 8156..8276), the camera close above ship 0 looking down on it over the terrain
(8060..8130), fade out from 8386.

## 2. Data

INSIDE.DAT (offsets in the unpacked block, `datfile.py`):

| file | offset | size | format | use |
|---|---|---|---|---|
| flight/map.raw | 0xfee6f | 262,144 | 512 × 512 indices, row-major | landscape texture: a **digitised aerial photograph** (suburbs, roads, fields), rows k and 512 − k nearly equal (mirrored top / bottom so it tiles) |
| flight/map.pal | 0x15c467 | 768 | 6-bit RGB | the part's palette; entry 0xdd = (24, 40, 47) is the sky |
| flight/map.shd | 0x146e6f | 16,384 | 64 rows (light level) × 256 (colour) | light: row 0 all black, row 63 ≈ the colour itself; ≈ nearest(pal[c] × L / 64) (62 % exact, mean error 1.2 / 63) |
| flight/map.fog | 0x13ee6f | 32,768 | 128 rows (fog level) × 256 | fog: row 0 identity (except 163 → 165), rows 63..127 all 0xdd (sky); ≈ nearest(lerp(pal[c], pal[0xdd], F / 63)) (mean error 0.9); rows 64..127 never used |
| flight/river.raw | 0x14ae6f | 65,536 | 256 × 256 indices | the ships' environment map: a **digitised fish-eye photo** (forest, light from above) |
| flight/ship.sro | 0x15ae6f | 5,624 | SurRender object (notes-engine.md): +8 200 vertices (12 bytes: int16 x, y, z, u, v, ?) from +0x20, +0x10 399 faces (8 bytes: flags, 3 × int16 vertex) from +0x980 | the ship |
| flight/sun.raw | 0xfae6f | 16,384 | 128 × 128 indices | the sun: a **drawn** radial glow with rays, its whole border is index 0xdd (the sky) |
| flight/lens1.raw | 0xe9020 | 4,662 | 74 × 63 | flare: hexagon (indices 0, 85, 87, 143..145, 163, 164) |
| flight/lens2.raw | 0xea256 | 2,236 | 52 × 43 | flare: hexagon ring |
| flight/lens3.raw | 0xeab12 | 713 | 31 × 23 | flare: small hexagon |
| flight/lens4.raw | 0xeaddb | 132 | 12 × 11 | flare: round dot |
| flight/lens5.raw | 0xeae5f | 16 | 4 × 4 | flare: 4 × 4 dot, no transparent texel (137, 139, 145, 221) |
| flight/flare.shd | 0xeae6f | 65,536 | [texel][background] | flare blend: row 0 identity (texel 0 transparent); ≈ nearest(min(63, pal[bg] + pal[texel] / 2)) (72 % exact, mean error 1.2 / 63): **adds half the flare's colour** |

All lens files are drawn (hand-made flare shapes); their indices are read through flare.shd (row = texel), so a
redraw would have to keep the indices, or the remake adds half of pal[texel] in RGB (7.4). sun.raw is drawn, copied
opaque by index: a redraw must keep a border that is exactly the sky colour (or the remake draws it blended over
the sky, which looks the same). map.raw and river.raw are photos (not of people); map.raw is read through
map.shd / map.fog by index (a redraw would need its indices kept, or the remake lights and fogs in RGB, 7.2).

INSIDE.EXE: names 0x420734 "map heightfield", 0x420744.. the file names, 0x420830 "flight landscape material",
0x42084c "flight envmap material", 0x420864 "flight scene", 0x420874 "flight control obj", 0x420580 "grid
landscape", 0x4205ec "flight ship 0".., 0x420708 "flight sun flare", 0x42071c "flight flare", 0x420558 "camera".
Constants: doubles 0x420540.. and 0x42063a..0x4206fe (listed where used), the float 0.8 at 0x4227e8 and the double
17.0666… (= 1024 / 60) at 0x4227ec (both only read).

## 3. Init 0x402948 (precalc, main line: 53-bit precision)

```c
tag = new_tag();                                    // 0x40b3c0, 0x40b4d0
srand(0x45eafc);                                    // 0x4134f1 (the program's only srand)
hf = alloc(0x20000, "map heightfield");             // [0x424ff8]: 128 x 128 x {int32 height, int32 light}
lens1..5 = file(...); flare_shd = ...; sun = ...;   // [0x4227bc..0x4227cc], [0x4227d0], [0x4227b8]
map = file("flight/map.raw"); fog = file("flight/map.fog"); shd = file("flight/map.shd");    // [0x4227d8/dc/d4]
river = file("flight/river.raw"); ship_sro = file("flight/ship.sro"); pal = file("flight/map.pal");
mat_land = alloc(0x44); mat_env = alloc(0x44); scene = alloc(0x50);       // [0x42500c], [0x425000], [0x425008]
scene_init(scene);                                  // 0x410878
scene_window(scene, 320, 200, x0 0, y0 0, x1 320, y1 200);   // 0x410618
scene->focal(+0x3c) = 200;                          // (0x410878 set 256) -> engine: F = 25600, G = 21333 (7.1)
material(mat_land, tex map, +4 0, shade shd, fog fog, umask 0x1ff, vmask 0x1ff, ushift 9, vshift 9);  // 0x4105e4
material(mat_env, tex river, +4 0, shade 0, fog fog, 0xff, 0xff, 8, 8);
make_camera();                                      // 0x401330: camera object [0x425004], added, scene+0x38
ctl = alloc(0xc8, "flight control obj"); object_init(ctl); object_pos(ctl, 0, -512, -256); add(scene, ctl);
make_landscape();                                   // 0x401450 (section 4)
light_heightfield();                                // 0x40136c (section 4)
make_ships();                                       // 0x4018d0
make_flares();                                      // 0x4023d4 (section 7.4)
identity(cam_m /*0x424e30*/);
cam_p /*0x424e18*/ = (rand() % 0xa00, 0, rand() % 0x800) + ship_p[0];    // NB: z's rand() is called first
identity(ctl_m /*0x424d38*/);
ctl_p /*0x424fe0*/ = (rand() % 0xa00, 0, rand() % 0x800) + ship_p[0];    // (overwritten by the first tick)
```
`make_ships` (0x4018d0): three objects of 0x10c + 200 × 64 + 399 × 52 bytes ([0x424ea0..0x424ea8]); ship 0 is loaded
from ship.sro with scale 0x1999 / 65536 = 0.0999908 (0x410bb0: every vertex coordinate becomes
trunc(int16 × 0.0999908), so the mesh is quantised to whole units, about ±63 units in size), material mat_env,
rasterizer 0x412db0 for every face, flag 0x10 in byte +0xc5 (environment mapping); ships 1 and 2 are copies
(0x411c80). Positions (doubles, 0x424ec8 + 0x18 i) and matrices (3 × 3 doubles row-major, 0x424a68 + 0x48 i):
```c
x0 = rand() % 0x3c00;  z0 = rand() % 0x3c00;
for (i = 0; i < 3; i++) { ship_p[i] = (x0 + rand() % 0x1400, 0x0a00 * i, z0 - 0x0a00 * i); identity(ship_m[i]); }
```
From srand(0x45eafc): x0 = 9798, z0 = 11101, ships (12374, 0, 11101), (10954, 2560, 8541), (10028, 5120, 5981);
camera ship 0 + (1894, 0, 1420); control point ship 0 + (2500, 0, 1374). All three objects are added to the scene.

0x402c28 (script 19:63): memset both screen buffers to 0 (0x40b7a4, 64,000), 0x400364(map.pal), 0x4004ac(1.0, 60),
tick 0x4021f8, frame 0x4028e4. 0x402c7c (23:63): tick and frame routines 0, free(hf) (0x40b690).

## 4. The landscape: grid object and height field (0x401450, 0x40136c)

```c
land = alloc(0xdeec, "grid landscape");             // [0x425014]
tmp = alloc(400000, 0);
make_grid_sro(tmp, nx 18, nz 18, dx 1, dz 1);       // 0x410f34: 19 x 19 vertices, 18 x 18 cells, 2 triangles each
object_init(land); load_object(land, tmp, scale 0x2000000 /*512.0*/, mat_land);   // 0x4108a8, 0x410bb0
set_rasterizer(land, 0x412264); add(scene, land); free(tmp);
for (k = 0; k < 38; k++) utab[k] = (int)(k * 4096.0 * (1.0 / 18));     // 0x424d80: 0, 227, 455, .. 8419
```
Grid object: vertex (row r, column c), r, c = 0..18, at list index 19 r + c, object-local x = (c − 9) × 512,
z = (r − 9) × 512, y set per frame. Cell (r, c) is split along the diagonal from (r, c + 1) to (r + 1, c): faces
(r,c)(r,c+1)(r+1,c) and (r+1,c)(r,c+1)(r+1,c+1) (face 0 = vertices 0, 1, 19; face 1 = 19, 1, 20).

**Heights** (0x401450, verified identical). With s(n, f) = sin(n · 1.5707963268 · f / 128) and c(n, f) the same
with cos (0x4012e0 / 0x401308: n an int, f a float, the constant 1.5707963268 as written in the source, double at
0x420540), for row j (z) = 0..63 and column i (x) = 0..63:
```c
h = 179.2*s(i-11, 4.0) + 256*s(j+64, 2.0) + 179.2*c(j-13, 4.3) + 256*c(i+17, 6.0)
  + 435.2*s((int)(0.3*i*j), 9.0) + 486.4*c(i+10, 16.0) + 128*c(j-7, 45.0) + 204.8*s(i+3, 39.0)
  + 230.4*s(j*(i+1), 64.0) + 204.8*c(i*j, 90.0) + 153.6*s(i*(j+1), 72.0);    // summed in this order, doubles
h = (float)h;  if (h > 256) h = 0;  H[j][i] = (int)h;                         // truncate toward zero
for (j = 0; j < 63; j++) for (i = 0; i < 63; i++)                              // smoothing, int division toward 0
    H[j][i] = (H[j][i] + H[j][i+1] + 2*H[j+1][i+1]) / 4;                       // (reads only unchanged entries)
for (j = 0; j < 64; j++) for (i = 0; i < 64; i++) H[j][64+i] = H[j][63-i];      // mirror in x
for (k = 0; k < 64; k++) for (i = 0; i < 128; i++) H[64+k][i] = H[63-k][i];     // mirror in z
```
(int)(0.3·i·j) is computed in 53-bit precision: 0.3 × 10 gives 3, not 2 (with 64-bit precision it would be 2 and
530 of the 4096 heights would change). Range −1448..246; y points down, so −1448 is a peak 1448 units high. The
field is periodic (128 cells) and mirror-symmetric, so it tiles. Rows / columns 63 are unsmoothed. The high terms
(arguments i·j, j(i+1), i(j+1)) are pseudo-random noise per vertex: the height field is a lattice of values, not a
sampled continuous function; a high-resolution remake keeps the lattice and its flat triangles (7.2, 8).

**Light** (0x40136c, verified identical), per entry from the slope towards x + 1 only:
```c
for (z = 0; z < 128; z++) for (x = 0; x < 128; x++) {
    a = atan2((H[z][x] - H[z][(x+1) & 127]) / 512.0, 1);              // fpatan
    v = (float)(cos(a + -1.0995574287564276) * 64 * 16);             // -0.35 pi = -63 degrees
    if (v < 160) v = 160;
    L[z][x] = (int)v;                                                 // 160..1023 = light level * 16
}
```
This is the Lambert term of the (x, y) profile only: with s = (H[x] − H[x+1]) / 512, L = 1024 · (s sin 63° + cos 63°) /
√(1 + s²) = 1024 · dot(profile normal, light), light from −x and 27° above the horizon (the sun is in the −x
direction too), floor 160 (level 10). The z slope is ignored (a visible property of the original's shading).

## 5. Per refresh: the tick routine 0x4021f8

Runs in the retrace callback once per refresh. The n-th call (n = 1, 2, ...) is at refresh 7063 + n with
**T = [0x4248c8] = 4n** (all 1404 calls of the capture; notes-core.md 1.5).

### 5.1 The routine
```c
const double S = 17.066666666666666;               // 0x4227ec (1024 / 60)
ship_step(0, (float)(S * 0.4), T);                  // 6.826667 units per refresh
ship_step(1, (float)(S * 0.3), T);                  // 5.12
ship_step(2, (float)(S * 0.2), T);                  // 3.413333
ctl_p = (0,0,0) + ship_p[0];  identity(ctl_m);      // the control point is ship 0
camera_step((float)(S * 0.3));                      // 5.12
```
Matrices are 3 × 3 row-major doubles; a vector is transformed as M·v (0x4144ec). Rotations post-multiply: rot_x
0x414270 M = M·[[1,0,0],[0,c,−s],[0,s,c]], rot_y 0x414344 M = M·[[c,0,s],[0,1,0],[−s,0,c]], rot_z 0x414418
M = M·[[c,−s,0],[s,c,0],[0,0,1]], with c = cos(angle), s = sin(angle).

### 5.2 Ships 0x401dbc(i, s)
```c
ship_p[i] += ship_m[i] * (0, 0, s);                 // 0x413d58: forward along local +z (column 2 of the matrix)
t  = T * 0.12;
n1 = (int)((t + i * 30.6) * 1.1);
a  = (float)sin(n1 * 1.5707963268 * (float)(i * 0.25 + 1.7) / 128);
n2 = (int)((t + 91) - 17 * i);
r  = c(n2, 1.2f) * 0.020943951024 + a * 0.010471975512;           // 1.2 and 0.6 degrees
n3 = (int)((t - 42) + 91 * i);
r  = c(n3, -1.71f) * -0.013962634016 + r;                          // -0.8 degrees
n4 = (int)((t - 81) - i * 13.5);
r  = r - s(n4, 2.31f) * -0.006981317008;                           // -0.4 degrees
if (fabs(r) > 0.006981317008) ship_m[i] = rot_y(ship_m[i], r);     // yaw only: the ships stay level
ship_p[i].y *= 0.99;
if (i > 0) for (k in x, z) {                                       // follow ship 0, both tests in turn
    if (ship_p[i].k > ship_p[0].k) ship_p[i].k -= s * 0.65;
    if (ship_p[i].k < ship_p[0].k) ship_p[i].k += s * 0.65;
}
h = i * 0.3 + 1.7;
if (h * -512 < ship_p[i].y) ship_p[i].y = -(h * 512);              // never lower than 512 h above y = 0
```
In effect the heights are constant from the first tick: ship 0 at y = −870.4, ship 1 −1024, ship 2 −1177.6 (the
0.99 decay only pulls them onto the limit). The yaw rate r is a sum of four stepped sines of T (each argument n
changes every few refreshes, so r is piecewise constant); ships 1 and 2 also slide towards ship 0's x and z by
0.65 s per refresh per axis.

### 5.3 Camera 0x4020f8(s)
```c
cam_p += cam_m * (0, 0, s);                         // along column 2 of the previous camera matrix
cam_p.y *= 0.995;
if (cam_p.y > -1024) cam_p.y = -1024;               // in the capture always exactly -1024
d = ctl_p - cam_p;
cam_m = look(d);                                    // 0x414cec
cam_m = rot_z(cam_m, 3.1415926536);                 // double 0x400921fb544486e0 (not exactly pi)
cam_m = rot_x(cam_m, -0.20943951024);               // -12 degrees
```
`look(d)` builds **rows** f = d / |d|, a = (−f.z, 0, f.x) / |..|, u = f × a / |..| (0x4135c4 cross product), matrix
rows (a, u, f). The engine uses a camera matrix C as **camera-to-world** (columns = camera axes): camera space
v = Cᵀ (p − cam_p) (0x40c158, 0x411e80, verified). So the camera's axes are the columns of
C = L · Rz(π') · Rx(−12°), L the look matrix: for a level f (f.y = 0) column 2 of L is f, Rz(π') turns x and y over
(y points down on screen) and Rx tilts the view by 12°; for f.y ≠ 0 the columns of L are (−f.z / n, f.x f.y / n,
f.x), (0, −n, f.y) and (f.x / n, f.y f.z / n, f.z) with n = √(f.x² + f.z²), so the third column is not f. The
result: ship 0 stays near the screen centre (x: median 161, 90 % of the refreshes within 146..195; y: median 108,
about 83 while it is far ahead, about 115 when the camera is within 600 units, 65..209 for 90 % of the refreshes).
Keep this exact matrix construction.

Speeds: camera 5.12 / refresh (359 units/s) along its own view, ship 0 6.83 (478 units/s); the camera-to-ship-0
distance goes from 2371 (start) to about 700..850, then the jump (6.2) sets it back to 2157, later down to 177.

### 5.4 Precision and the integer arguments n1..n4

The tick routine runs inside the timer interrupt; its handler does `fnsave`, which leaves the x87 in its reset
state, **control word 0x037F: 64-bit precision** (notes-core.md 10). So the tick is evaluated in extended precision
(main-line code: 53-bit). Only the truncations to n1..n4 are sensitive to this: 0.12 is stored as
0.11999999999999999556, so T·0.12 is a hair below 3T/25, and where 3T/25 + constant is a whole positive number the
original truncates one lower (T = 100: n2 = 102, not 103; a plain double evaluation is wrong in 717 of 18000
cases). Exact integer form (checked against the x87 model for T = 0..19,999, i = 0..2):
```c
int below(int v100) { /* 100 * value, an integer; the original's value is just below it when T > 0 */
    if (T > 0 && v100 > 0) return (v100 - 1) / 100;
    return v100 >= 0 ? v100 / 100 : -((-v100) / 100);             // toward zero
}
n1 = (132*T + 33660*i) / 1000;                     // (3T/25 + 30.6 i) * 1.1, exact, no adjustment
n2 = below(12*T + 9100 - 1700*i);
n3 = below(12*T - 4200 + 9100*i);
n4 = below(12*T - 8100 - 1350*i);
```
With these n, everything else in plain doubles stays within 1e-11 of the original over the whole part
(`remake_check.py`); the float32 roundings shown above (speeds, a, the factors 1.2f, −1.71f, 2.31f, i·0.25 + 1.7)
are part of the formulas.

### 5.5 Time base

All motion is stepped per refresh (n = refresh − 7063); T = 4n is used only for the yaw arguments. The state the
frame routine renders is the state after the last tick, converted to ints (6.1); the frame routine does not wait
for the copy, so the frame shown at a retrace was drawn from the state of the previous tick or the one before
(notes-core.md 1.5, "Frame latency"). A remake steps the ticks exactly (replaying from n = 0 after a backwards
seek, which is cheap: 1404 steps) and interpolates positions between tick n and n + 1 by the fraction of the
refresh; the camera matrix is a function of the positions, so it can be rebuilt from the interpolated positions
(or slerped).

## 6. Per drawn frame: the frame routine 0x4028e4 (main line)

```c
scene->target = [0x4248d0];          // the back buffer
objects_from_state();                // 0x40276c
camera_jump();                       // 0x402638
flares_visibility();                 // 0x4024d0
land_grid(land, camera);             // 0x401ab8
land_fog(land, camera);              // 0x401c64
scene_clear(scene, 0xdd);            // 0x41127c: memset of the scene window (all 320 x 200) with the sky index
render(scene);                       // 0x40c6ac (engine)
debug_text();                        // 0x400bec: order:row and free memory when [0x42277c] (off)
flip();                              // 0x400b14
```
Every pixel is overwritten each frame (clear, then the scene).

### 6.1 0x40276c: state to engine objects
Object positions are ints (truncated toward zero): ship i object = trunc(ship_p[i]), matrix ship_m[i]; camera object
= trunc(cam_p), matrix cam_m; control object = trunc(ctl_p), matrix ctl_m; **sun** object position =
(ctl.x − 0x23000, −0x5000, ctl.z) with ctl the control object's int position: 143,360 units in −x from ship 0 and
20,480 up (elevation about 8° seen from the camera). A tick that interrupts the frame routine later does not change
this frame's objects.

### 6.2 0x402638: random camera jump (per drawn frame)
```c
for (i = 0; i < 3; i++) {
    d = ship_p[i] - cam_p;
    if (sqrt(d.x*d.x + d.y*d.y + d.z*d.z) < 153.6) {
        n1 = (int)((rand() % 10 + 5) * d.x);
        n2 = (int)((rand() % 5 + 2) * d.y);
        n3 = trunc((rand() % 10 + 5) * d.z);
        cam_p -= (n1, n2, n3);       // jump away from the ship by 5..14 times the offset (2..6 in y)
    }
}
```
In the capture it fires once, in the frame after the tick with T = 3196 (refresh 7862): camera (13859.5, −1024,
9731.4) → (14910.5, −1024, 11500.4). That frame and the others drawn before the next tick use the new position with
the old matrix (refresh 7863 shows mostly sky), the next tick turns the camera back to ship 0 (7864). rand() is the
shared Watcom generator (seed at 0x423818): its state at the flight start is **0x95a8f93f** in the capture (left by
the flower start, notes-core.md 9; no other rand() call happens during the flight before the jump). The check runs
per drawn frame, but only the first frame after a tick can see a distance below 153.6 (the jump makes it large), so
a remake checks once per refresh after the tick and, for the frames of that refresh, renders the new position with
the old camera matrix.

### 6.3 0x4024d0: flare visibility
```c
for (k = 0; k < 8; k++) flare[k]->pos = sun->pos;
p = (double)sun->pos;
if (project(scene, &sx, &sy, &p) != 0                       // 0x411e80: -1 when camera z < 32 (scene+0x44 / 256)
    || sx < x0 - 12 || sx >= x1 + 12 || sy < y0 - 12 || sy >= y1 + 12)   // window 0..320 x 0..200
    for each flare: flags(+0xc4) |= 1;                      // hidden
else
    for each flare: flags(+0xc4) &= ~1;
```
0x411e80 is the 3D projection: v = Cᵀ (p − camera object position), sx = 160 + 200 v.x / v.z, sy = 100 +
166.666 v.y / v.z (exactly 25600 · 128 / 16384 and 21333 · 128 / 16384). There is no occlusion test: the flares stay
when the sun is behind a mountain. Shown in refreshes 7064..7312, 8022..8052, 8156..8276 (503 of the drawn
frames). The sun itself is not tested (it is drawn whenever it is in front of the camera, clipped to the screen).

### 6.4 0x401ab8: the grid around the camera (verified identical)
```c
cx = cam.x / 512;  cz = cam.z / 512;                 // camera object ints, C division (toward 0)
land->pos = (cx * 512, 0, cz * 512);
hx = cx mod 128, hz = cz mod 128, ux = cx mod 18, uz = cz mod 18;   // non-negative remainders
for (r = 0; r < 19; r++) for (c = 0; c < 19; c++) {
    v = &land->vertex[19*r + c];
    v->y     = H[(hz + r) & 127][(hx + c) & 127];     // +0x0c
    v->light = L[(hz + r) & 127][(hx + c) & 127];     // +0x30 (word)
    v->u     = utab[ux + c];                          // +0x28 (word)
    v->v     = utab[uz + r];                          // +0x2a (word)
}
```
So the world vertex at lattice point (gx, gz) = (cx + c − 9, cz + r − 9) is (512 gx, H[(gz+9) mod 128][(gx+9) mod 128],
512 gz) with light L at the same entry, and texture coordinates u = ((gx + 9) mod 18) · 4096 / 18 (+ 4096 for the
columns past the wrap; truncated, `utab`), v the same with gz. The grid is world-anchored: it moves in whole cells
with the camera, there is no popping (the edge is fully fogged, 6.5).

### 6.5 0x401c64: fog per vertex (verified identical)
```c
p = ((float)(cam.x % 512), (float)cam.y, (float)(cam.z % 512));   // camera in the grid object's space
f0 = (int)(0.8f * 3328.0);                                        // 2662
for each vertex v: {
    e = sqrt((p.x - v.x)^2 + (p.y - v.y)^2 + (p.z - v.z)^2) - f0;
    fog = e >= 0 ? (int)(e * 0.00078125 * 63 * 16) : 0;          // e * 0.7875
    v->fog(+0x32) = clamp((short)fog, 0, 0x3f0);                  // fog level * 16, 0..63
}
for each face: rasterizer = (all three fog == 0) ? 0x414e5c : 0x412264;
```
Fog level F = min(63, (d − 2662) / 20.3175) for the camera distance d: none up to 2662 units, full (the sky
colour) from 3942. The grid edge is at least 4096 units away horizontally, so its vertices are always fully fogged.
On average 528 of the 648 faces have some fog.

## 7. How the pixels are made

### 7.1 Projection (engine)
Camera space v = Cᵀ (p − cam), C = the camera matrix (5.3), cam = the camera object's int position. Screen x =
160 + 200 · v.x / v.z, y = 100 + 166.667 · v.y / v.z (focal 200 from scene+0x3c; F = [0x4b9014] = 25600 = 200 · 128,
G = [0x4b8fec] = 21333 = F · 5 / 6 for the 4:3 display; horizontal field of view 77.3°, vertical 61.9°). y points
down on screen and in the world (up is −y). Faces and sprites are drawn in depth order (painter's algorithm: the
engine's 24-bit radix sort of its draw list [0x4b8fc0] (0x40c384), far to near; the sprite keys are in 7.4);
notes-engine.md has the face keys, the clipping (near plane) and the rasterizer details. The camera (1024) and the
ships (870.4, 1024, 1177.6) fly at fixed heights while the peaks reach 1448: they sometimes pass through mountains
(the near plane cuts them open, e.g. around 8090..8130).

### 7.2 Landscape faces (rasterizers 0x412264 / 0x414e5c, material mat_land)
Per pixel (the faces' +0x30 is 0, so the engine takes its **affine** path: u, v, light and fog are interpolated
linearly in screen space across each triangle; the perspective-correct path is only used for faces with bits 10..12
of +0x30 set):
```c
texel = map[(v >> 3) & 511][(u >> 3) & 511];      // u, v vertex units: 8 per texel (4096 = the 512-texel width)
idx   = fog_tab[fog >> 4][ shd[light >> 4][texel] ];    // 0x414e5c (no fog): shd[light >> 4][texel]
```
(fixed-point details, the +1 bias, the sample point (column + 1, row + 15/16) and the clipping: notes-engine.md,
"rasterizers"; the landscape is the only user of 0x412264). So the texture spans 18 × 18 cells
= 9216 × 9216 units (28.44 texels per cell, 18 world units per texel), u along +x, v along +z, repeating; the height
field repeats every 128 cells (65,536 units). A per-pixel Python model with these rules (float interpolation,
sampled at (column + 1, row + 15/16) with the +1/16 bias) reproduces the original's landscape (`landrender.py`: 68 %
identical indices, mean colour difference 4 / 189, visually the same; sampling at pixel centres gives 56 %, and
perspective-correct texturing matches 7..9 % fewer, which confirms the affine path).

For the remake, the same mapping with continuous inputs: colour = lerp(lerp-in-L(pal[shd[⌊L⌋][t]],
pal[shd[⌊L⌋+1][t]]), sky, F / 63), or with the fitted formulas colour = lerp(pal[t] · L / 64, pal[0xdd], F / 63), L =
light / 16 interpolated from the vertices (10..64), F the fog level computed per pixel from the true distance, t the
bilinearly filtered map.raw texel colour at (u, v) = ((X / 512 + 9) · 512 / 18, (Z / 512 + 9) · 512 / 18) texels
(X, Z world coordinates of the surface point).

### 7.3 Ships (rasterizer 0x412db0, material mat_env)
ship.sro scaled by 0.0999908 and truncated to ints (3), positioned at trunc(ship_p[i]) with ship_m[i] (yaw only),
local +z forward. The engine computes environment-map coordinates u, v per vertex every frame (object flag 0x1000 of
+0xc4: stage 0x41acc4 in notes-engine.md, u, v from the camera-space normal plus the vertex's camera-space x, y; a
fit to the normal alone leaves 200..550 units of error, which that screen-position term explains) and the
rasterizer copies river.raw[v][u] (256 × 256, masks 0xff) straight to the screen: no light, no fog (the material
names map.fog but 0x412db0 does not read it). Affine (faces' +0x30 = 0x100000 / 0x100002: bits 10..12 clear).

### 7.4 Sun and lens flares (0x402294, 0x402330, sprite drawer 0x415878; verified identical)
Sprite objects (0x104 bytes, type 0x42381c whose method 0x4157d4 queues them): +0xd0 draw routine, +0xd8 texture,
+0xdc blend table, +0xe0 stride, +0xe4 width W, +0xe8 height H, +0xec k (float), +0xf0 size (float), +0xf4 depth
bias (int).

| object | texture (W × H) | k | size | draw |
|---|---|---|---|---|
| sun [0x4227e4] | sun.raw (128 × 128) | 1.0 | 70 | 0x415dbc: opaque copy |
| flare 0 [0x424e78] | lens4 (12 × 11) | 0 | 64 | 0x415dc8: flare.shd |
| flare 1 [0x424e7c] | lens5 (4 × 4) | −0.2 | 64 | " |
| flare 2 [0x424e80] | lens5 | 0.5 | 64 | " |
| flare 3 [0x424e84] | lens4 | 0.3 | 64 | " |
| flare 4 [0x424e88] | lens5 | −1.3 | 64 | " |
| flare 5 [0x424e8c] | lens1 (74 × 63) | −0.9 | 64 | " |
| flare 6 [0x424e90] | lens2 (52 × 43) | −0.47 | 64 | " |
| flare 7 [0x424e94] | lens3 (31 × 23) | −2.0 | 64 | " |

All nine are at the sun's position (6.1, 6.3). The engine transforms the position to camera space as ints ×128
(x, y, z = trunc(128 · Cᵀ(p − cam)), 0x40c158), skips the sprite when hidden or z < 0, and queues it with key
max(0, trunc(bias + z)) / 16: the sun (bias 0) far behind everything, the flares (bias 0x9c000000 =
−1,677,721,600) at key 0, drawn last. 0x415878 (EAX item = object + 0xc8, EDX mode):
```c
zz = z / 128.0;  if (64 > zz || 4194304 < zz) return;               // [0x4b9020] / 128, [0x4b9018]
SX = k * x * (25600 / 1024.0) / zz;   SY = k * y * (25600 / 1024.0) / zz;   // 1/16 pixel; NB y uses F, not G
SW = W * size * 25600 / zz;           SH = H * size * 21333 / zz;
y0 = (int)(1600 + SY - SH * 0.5);  y1 = y0 + (int)SH;
x0 = (int)(2560 + SX - SW * 0.5);  x1 = x0 + (int)SW;
du = 16.0 * W / (int)SW;  dv = 16.0 * H / (int)SH;                  // texels per pixel (integer sizes)
uoff = voff = 0;
if (y0 < 0) { voff = -y0 * dv / 16; y0 = 0; }     if (x0 < 0) { uoff = -x0 * du / 16; x0 = 0; }
fy = y0 & 15; fx = x0 & 15;  y0 /= 16; y1 /= 16; x0 /= 16; x1 /= 16;     // toward 0
x1 = min(x1, 320); y1 = min(y1, 200);  if (x1 <= x0 || y0 >= y1) return;
u16 = (int)((uoff + (15 - fx) * du / 16) * 65536);  du16 = (int)(du * 65536);
v = voff + (15 - fy) * dv / 16;
for (y = y0; y < y1; y++, v += dv) {
    row = (int)v * stride;  u = u16;
    for (x = x0; x < x1; x++, u += du16) {
        t = tex[row + (u >> 16)];
        dst[y][x] = mode == 0 ? t : flare_shd[t][dst[y][x]];
    }
}
```
In pixels: a sprite's centre is (160 + 200 k v.x / v.z, 100 + **200** k v.y / v.z) and its size W · size · 1600 / v.z ×
H · size · 1333.3 / v.z (round on the 4:3 display). Note the vertical offset uses the horizontal focal length: the
sun and the flares are 1.2 times farther from the horizontal centre line than the sun's 3D direction (the 3D
projection uses 166.67); the visibility test (6.3) uses the 3D projection. The flares lie on the line from the
screen centre (k = 0) through the sun (k = 1) and beyond to the opposite side (k < 0). With the sun about 143,000
units away the sun sprite is about 100 × 83 pixels, lens1 53 × 38.

Sampling: nearest texel at about +15/16 of each pixel (not the centre). The sun is copied opaque: its square is
invisible only because its border is the sky index and it is drawn first (behind the landscape, ships and flares).

### 7.5 Palette
map.pal, set by 0x402c28 with a fade from black (level 0 after the flower's fade out) to 1.0 over 60 refreshes
(full from 7123; the float steps end at 23/63 for some entries instead of 24 — the core's fade, notes-core.md);
fade to black over 40 refreshes from 8386. The part writes no palette entries itself.

## 8. Rendering shortcuts a high-resolution remake should evaluate exactly

- 320 × 200 → any resolution; the projection as in 7.1 (keep the 4:3 field of view).
- Positions truncated to ints for the engine (6.1), the ship mesh quantised to whole units after ×0.1 (3), the
  sprites' camera coordinates as ints ×128: use the doubles (interpolated between ticks, 5.5).
- Landscape: the 19 × 19 grid around the camera with affine texturing → render the same lattice (or a larger area;
  beyond 3942 units it is pure sky colour) with perspective-correct, bilinearly filtered map.raw; heights stay a
  lattice of 512-unit cells with flat triangles split along the (r, c+1)–(r+1, c) diagonal (the faceted shape is the
  original's look; a smoother surface would change the content).
- Light: Gouraud between vertex values of a 64-level table → interpolate L continuously, colour from the table rows
  (interpolated) or pal[t] · L / 64.
- Fog: per-vertex distance interpolated linearly in screen space and quantised to 64 levels → per-pixel distance,
  continuous level, colour lerp towards the sky (pal[0xdd]).
- Painter's sort by face centroid → depth buffer (avoids the original's sorting errors where ships cross the
  terrain; keep the sun behind everything and the flares in front of everything).
- Sprites: nearest-neighbour scaling with 1/16-pixel positions and the +15/16 sampling offset → smooth textured
  quads at the exact float positions and sizes of 7.4 (keeping the vertical factor 200); flare.shd → add
  pal[texel] / 2 in RGB (clamped); the sun opaque over the sky, or with its glow blended.
- The flare blend and the shade / fog tables are palette quantisations of the formulas in section 2.

## 9. Whole frame
Yes: 0x41127c fills the whole scene window (320 × 200) with index 0xdd every frame before drawing. What the remake
shows before the first frame (refresh 7063) is black.

## 10. Not determined here
- The ships' environment mapping and the landscape rasterizer were not modelled pixel for pixel here (notes-engine.md
  describes both; the 32 % of land pixels that differ by a neighbouring texel / level from my float model are the
  fixed-point detail described there).
- Near-plane clipping when the camera or a ship is inside a mountain (engine).
- The rand() state at the flight start depends on how many frames earlier parts drew (notes-core.md 9); 0x95a8f93f
  is the capture's. With another seed the jump of 6.2 would have other factors (5..14, 2..6).

## 11. Models (`inside/re/models/flight/`, see its README.md)

| script | checks |
|---|---|
| `heightmap.py` | heights and light vs. snapshot f7063: identical |
| `flightmodel.py` | ships, camera, control point at all 1404 ticks (x87 extended precision): bit-exact; the camera jump |
| `remake_check.py` | the integer rule for n1..n4 (T = 0..19,999) and a plain-double model (error ≤ 1e-11) |
| `landmodel.py` | grid, texture coordinates, light, fog per vertex and face rasterizers in all 2024 frames: identical |
| `sunmodel.py` | the sun's camera coordinates and the flare visibility in all 2024 frames: identical |
| `spritemodel.py` | sprite drawer: 2557 draws (refreshes 7063..7300) identical |
| `landrender.py` | per-pixel landscape vs. the drawn frames (68 % identical indices, visually equal) |
| `run_inside.py` | copy of `inside/re/run_inside.py` with the dump hooks (`run1.dump`, `run1.sprites`) |
