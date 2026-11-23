# Scope "water": the underwater scene (twisted metal object, glass fish, rippling background)

Routines: init 0x407dec (and 0x4079f4), start 0x407f90 (script entry 72, 23:63), tick 0x407c78, frame 0x407d78
(0x407cac, 0x4078d4, 0x407400, the ripple 0x401234), stop 0x408014 (entry 73, 27:48). 0x407c44 is an unused copy of
0x407c78 (no reference). Everything else the part calls is shared: the core helpers and the SurRender engine
(notes-core.md, notes-engine.md), described here by what goes in and out.

Clock names as in notes-core.md: **M** = [0x4248c4] (timer callbacks since the music start, 280.345 per second, 4 per
refresh, never reset), refresh = record of `full.frames`. "Frame" = one call of the frame routine (one drawn frame).

Checked against the emulator (scripts in `models/water/`, README there):
- the ripple 0x401234 (Python model): new height field and background rows 2..197 **bit-exact** in 12 full frames
  spread over the whole part;
- the fish 0x407400 and the drops (Python model, run from each frame's state): positions, matrices, tail vertices
  and normals, projected positions, rand seed and every height-field write **exact in all 1,170 frame steps** of the
  part, given the M each fish read (6.1), except 4 fish updates in which M changed between two of its own reads;
- the init's fish placement replayed from the seed 0xef012bc7 and the first frame's update: the 8 fish of
  `snaps/f8468` exactly;
- the obuke's rotation: exactly 0.0185 rad in each of the 1,321 tick routine calls;
- the scene geometry (camera, positions, projection) against the vertices the engine projected (within its 1/16
  pixel truncation) and the environment mapping 0x41acc4 against the texture coordinates the rasterizers receive
  (0 of ~770 vertices differ, in each of 3 frames);
- a run resumed from `snaps/f8468` reproduces `full.frames` (92 of 92 records identical).
The rasterizers themselves are the engine's (not re-modelled here): the final pixels were checked only for their
kind (obuke pixels are texels of waterenv.raw, fish pixels are water.int[texel][background], 95..98 %).

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| | refreshes | music | |
|---|---|---|---|
| start | entry 72 (0x407f90) runs at the end of the last flight frame, refresh 8467 (M = 33719); the part's first frame is drawn right after it; the first tick routine call is at refresh 8468 (M = 33721, P = 2) | 23:63 | same refresh as entries 70 (white flash 0x400498) and 71 (flight off) |
| part | 8468..9789: 1,172 calls of the frame routine in 1,322 refreshes (62.2 per second; shown frames 1 refresh apart 1,020 times, 2 apart 150 times) | 23:63..27:48 | |
| end | entry 73 (0x408014) at 27:48, refresh 9790, then a white flash and the second sprite part | 27:48 | |

On screen: a blurred blue underwater picture (water/water.raw) that ripples sideways: a 320×200 height field (a damped
wave simulation) shifts each pixel of the picture horizontally by an amount proportional to the height under it.
Eight small glass fish (water/fish.sro, environment-mapped with water/waterenv.raw, blended 50/50 with the picture)
swim in planes parallel to the screen, wander, turn around at the screen edges and stir the water: each leaves a
small triangle of height in the field every frame, so wakes and rings spread behind them; three times per frame a
1/200 chance drops a 7×7 square somewhere ("rain"). On the left a tall twisted metal spiral (water/obuke.sro,
environment-mapped, opaque) turns about its vertical axis, cut by the top and bottom of the screen, always in front
of the fish. Rows 0, 1, 198 and 199 are black except where the spiral covers them. The part begins under the white
flash of entry 70, which fades into the water palette in 14 refreshes.

## 2. Static data and variables

INSIDE.DAT (offsets in the unpacked block, `datfile.py`):

| file | offset | size | format, use |
|---|---|---|---|
| water/waterenv.raw | 0x230c9d | 65,536 | 256×256 8-bit, the environment map of both objects (a shiny sphere on black, 135 of the palette's indices) |
| water/water.shd | 0x240c9d | 16,384 | 64 levels × 256 colours shade table; put in the obuke's material but **not read** by its rasterizer (6.2) |
| water/water.pal | 0x244c9d | 768 | the palette, 6 bit |
| water/water.raw | 0x244f9d | 64,000 | 320×200 8-bit, the background picture (143 indices; shares only 22 with waterenv) |
| water/fish.sro | 0x25499d | 208 | the fish: 8 vertices, 10 faces |
| water/water.int | 0x254a6d | 65,536 | 256×256 blend table int[a][b] (row a = env texel, column b = background) |
| water/obuke.sro | 0x264a6d | 8,024 | the spiral: 287 vertices, 570 faces |

`.sro` as the loader 0x410bb0 reads it (engine scope for the rest): header dwords +4 vertex offset, +8 vertex count,
+0xc face offset, +0x10 face count (+0 is the file size for obuke.sro, 0x1df020 for fish.sro; unused). Vertex 12
bytes: int16 x, y, z, then u, v words (the loader keeps u >> 4 and −(v >> 4); both files hold garbage there,
overwritten every frame by the environment mapping), one unused word. Face 8 bytes: word 0 (0 in both files), words
1..3 = vertex indices. Vertex coordinates become trunc(c × scale / 65536) with the scale the part passes (6.2).

INSIDE.EXE:

| address | value | use |
|---|---|---|
| 0x4212e4, 0x4212ec, 0x4212f4 | 0.001584786053882726, 0.0027019724398811133, 0.008130081300813007 (doubles; 1/631, 1/370.1, 1/123) | fish turn rate frequencies (per M) |
| 0x4212fc | 0.1 | weight of the third turn term |
| 0x421304, 0x421396 | 0.01745329252 | degrees → radians |
| 0x42130c, 0x42131c, 0x42132c | 1/45, 1/37, 1/61 (0.0222…, 0.0270…, 0.0163…) | tail frequencies (per M) |
| 0x421314, 0x421324, 0x421334 | 15, 20, 25 | tail swing (object units) |
| 0x42133c | 800 | tail normal swing |
| 0x421344, 0x42134c, 0x421314 | 30, 10, 15 | speed = max(30 a + 10, 15) |
| 0x421354, 0x42135c, 0x421364 | −70, 390, 270 | turn-around window (screen pixels) |
| 0x42136c | 3 | wake window margin |
| immediates | 0x3ff921fb544486e0 = 1.5707963268, 0x400921fb544486e0 = 3.1415926536, 0x3fc657184ae79455 = 0.1745329252, 0x3f92f1a9fbe76c8b = 0.0185, 0xc0990000… = −1600.0, 0xc082c000… = −600.0, 0x4082c000… = 600.0 | (the code's "π/2" and "π" are 10-digit decimals: cos(1.5707963268) = −5.10e−12, which matters, 6.1) |
| 0x422854 | 0 in the file, set to 3 by the init | drop tries per frame |
| 0x422858 | 200 (kept if > 0, else 1) | 1 / drop chance |
| 0x422860, 0x422864 | 320, 200 | ripple width, height |
| 0x421374.. | "camera", "water object", "waterfish", the file names, "water material", "fish material", "water scene", "water frame 1", "water frame 2" | allocation names |

BSS: 0x4265dc waterenv, 0x42660c water.shd, 0x426614 water.pal, 0x426608 water.raw, 0x4265f8 fish.sro, 0x426610
water.int, 0x4264b8 obuke.sro; 0x426568 obuke material, 0x42656c fish material (0x44 bytes each); 0x4265ec scene (0x50);
0x4265e8 camera object (0xc8); 0x4265fc obuke object; 0x426570 + 4i fish object i (i = 0..7); doubles: 0x4264c0
camera position, 0x426590 camera matrix, 0x426488 obuke position, 0x4264d8 obuke matrix (3×3 row-major);
0x4265e4 / 0x4265d8 the two height fields "water frame 1" A / "water frame 2" B (int16, 320×200, swapped every
frame); 0x4265f0 the part's memory tag.

## 3. Init 0x407dec (precalc, before the music)

```c
[0x4248c0]++;                                    // busy (no retrace work meanwhile)
tag = 0x40b3c0(); [0x4265f0] = tag; 0x40b4d0(tag);   // new memory tag, current
load the 7 files (0x400614) into the pointers above;
mat_obuke = alloc(0x44, "water material"); mat_fish = alloc(0x44, "fish material");    // zero-filled
0x40c034();                                      // engine helper (shared): resets a few of its list pointers
scene = alloc(0x50, "water scene"); 0x410878(scene);   // defaults
0x410618(scene, 320, 200, 0, 0, 320, 200);       // viewport 320x200, clip window the whole screen
0x4079f4();                                      // camera and objects, below
scene->target (+8) = [0x4248d0];                 // overwritten by every frame
scene+0x3c = 0x100; scene+0x44 = 0x1c00; scene+0x40 = 0;   // engine fields (0x410878 sets 0x100, 0x2000, 0)
[0x422854] = 3; if ([0x422858] <= 0) [0x422858] = 1;     // 3 tries, chance 1/200
0x4105e4(mat_obuke, tex = waterenv, +4 = 0, table = water.shd, +0xc = 0, umask 0xff, vmask 0xff, 8, 8);
0x4105e4(mat_fish,  tex = waterenv, +4 = 0, table = water.int, +0xc = 0, umask 0xff, vmask 0xff, 8, 8);
[0x4248c0]--;
```

0x4079f4 (all positions in world units; the camera looks along +z, x right, **y down**):
```c
cam = alloc(0xc8, "camera"); 0x410520(cam); 0x411154(scene, cam); scene+0x38 = cam;
cam_pos = (0, 0, -1600);                         // [0x4264c0], never changes
cam_mat = euler(0, 0, 0) = identity;             // [0x426590], never changes
obj = alloc(0x10c + 64 * nverts + 52 * nfaces, "water object");   // from the .sro header
0x4108a8(obj);                                   // clear, identity matrix
0x410bb0(obj, obuke.sro, scale 0x9999 (= 0.5999908447 in 16.16), mat_obuke);
0x410850(obj, 0x412db0);                         // every face's renderer: the opaque texture mapper
0x411154(scene, obj);  obj+0xc4 = 0x1000;        // bit 0x10 of byte +0xc5: environment mapping on (0x41acc4)
obuke_pos = (-600, 0, 0);                        // [0x426488], never changes
obuke_mat = euler(0, 0.1745329252, 0);           // [0x4264d8] = Ry(10 deg)
for (i = 0; i < 8; i++) {
    f = alloc(0x10c + 64 * 8 + 52 * 10, "waterfish"); 0x4108a8(f);
    0x410bb0(f, fish.sro, scale 0x2666 (= 0.1499938965), mat_fish);
    0x410850(f, 0x41669c);                       // every face's renderer: the blending texture mapper
    f+0xc4 = 0x1000;                             // environment mapping on
    z = rand() % 1000;  x = rand() % 3000 - 1500;
    0x410868(f, x, 0, z);                        // integer position (+0x10, +0x14, +0x18) = (x, 0, z)
    0x4108dc(f, 1.5707963268, 0, 0);             // matrix (+0x28) = euler(...) = Rx(1.5707963268)
    0x4145ac(f+0x28, (rand() % 300) * 0.01745329252);   // matrix = matrix * Ry(angle)
    0x411154(scene, f);
    f+0xcc = -256000;                            // depth-sort bias: every fish face sorts 2000 units further away
}
```
euler(a, b, c) = 0x4145f4 = I·Rx(a)·Ry(b)·Rz(c); 0x414270 X = X·Rx(a), Rx = [[1,0,0],[0,c,−s],[0,s,c]]; 0x414344
(and 0x4145ac) X = X·Ry(a), Ry = [[c,0,s],[0,1,0],[−s,0,c]]; 0x4144ec v' = X v (column vectors); matrices row-major
doubles. **Random numbers**: the 24 rand() calls (z, x, angle per fish) are calls 39..62 after the flight init's
srand(0x45eafc), seed 0xef012bc7 before them, 0xf83d63af after (notes-core.md section 9 agrees). Replaying them
gives the fish (x, z, heading): (6, 448, 61°), (1164, 210, 47°), (−691, 551, 292°), (−123, 385, 44°),
(−524, 13, 79°), (−45, 494, 127°), (−239, 996, 18°), (−820, 491, 181°).

Allocations are zero-filled ([0x4b6ed8] = 1), so both height fields start flat.

## 4. Start 0x407f90 and stop 0x408014

```c
// 0x407f90 (entry 72, 23:63)
memset([0x4248d0], 0, 64000); memset([0x4248cc], 0, 64000);    // both screen buffers black
0x400364(water.pal);                    // base palette; the fade of the preceding white flash now runs to it
A = alloc(2 * 320 * 200, "water frame 1");  B = alloc(2 * 320 * 200, "water frame 2");   // zero
0x400af4(0x407c78); 0x400b04(0x407d78); // tick and frame routines (P = 0)
// 0x408014 (entry 73, 27:48)
0x400af4(0); 0x400b04(0); 0x40b6fc([0x4265f0]);   // routines off, free the init's blocks (A and B were allocated
                                                  // under whatever tag was current then)
```
Palette: entry 70's white flash (0x400498) sets the DAC to white and fades over 15 refreshes; 0x400364 changes the
target to water.pal. In the capture the DAC is constant from refresh 8482 on, with 346 of the 768 components one
step below water.pal (the fade's float steps truncate; notes-core.md section 6). The part runs no palette effect of
its own.

## 5. Tick routine 0x407c78 (once per refresh): the obuke's rotation

```c
obuke_mat = obuke_mat * euler(0, 0.0185, 0);      // 0x414070: = obuke_mat * Ry(0.0185), doubles
```
Rotation about the obuke's vertical axis, 0.0185 rad (1.06°) per refresh, 74.3° per second, one turn per 340
refreshes; the near side moves to the left. Closed form: after n calls the matrix is Ry(0.1745329252 + 0.0185 n);
the first call is at refresh 8468 (M = 33721), so a frame that copies the matrix during refresh R shows
**θ = 0.1745329252 + 0.0185 (R − 8467)**, i.e. n = (M_retrace − 33721) / 4 + 1 with M of the last retrace callback.
(The copy in the frame routine is guarded by [0x4248c0], section 6; a retrace callback that falls into it skips
its tick routine call: never happened in the capture.)

## 6. Frame routine 0x407d78

```c
scene->target = [0x4248d0];                      // the back buffer
[0x4248c0]++;  0x407cac();  [0x4248c0]--;        // copy the doubles into the engine objects:
    //   cam  (+0x10..0x18) = trunc(cam_pos);    cam+0x28  = cam_mat   (0x413e68: copy 3x3)
    //   obj  (+0x10..0x18) = trunc(obuke_pos);  obj+0x28  = obuke_mat
memset(back, 0, 640);  memset(back + 63360, 0, 640);   // rows 0, 1 and 198, 199 black
0x4078d4(back);                                  // fish, drops, ripple: writes rows 2..197 (6.1)
0x40c6ac(scene);                                 // engine: render the camera's view of all objects into back (6.2)
0x400bec();                                      // debug overlay (music position, memory), off
0x400b14();                                      // flip
```
**Every pixel of the frame is written** each frame: rows 0..1 and 198..199 are cleared, rows 2..197 are the
displaced picture, the 3D objects are drawn on top (they may cover rows 0..1 and 198..199).

### 6.1 0x4078d4: fish, drops, ripple (once per drawn frame)

```c
void water_frame(u8 *screen) {
    for (i = 0; i < 8; i++) fish_update(i);      // 0x407400
    for (k = 0; k < [0x422854] /* 3 */; k++)     // drops
        if (rand() % [0x422858] /* 200 */ == 0) {
            x = rand() % (320 - 20) + 10;        // 10..309
            y = rand() % (200 - 20);             // 0..179
            h = rand() % 20000;
            for (dx = -3; dx < 4; dx++)
                for (yy = y + 7; yy < y + 14; yy++) A[yy * 320 + x + dx] = h;   // 7x7, rows 7..192
        }
    ripple(screen, water.raw, A /* cur */, B /* prev */);   // 0x401234
    swap(A, B);                                  // [0x4265e4] <-> [0x4265d8]
}
```
A holds the field of two frames ago; the wakes and drops are written into it just before the ripple overwrites it.

**Fish 0x407400(i)** (fish matrix Mf = object +0x28, integer position pf = +0x10..+0x18, vertex array vtx = +0xd0,
64 bytes per vertex: +8 x, +0x34/+0x38/+0x3c normal x/y/z; each read of M below is a separate read of [0x4248c4]):
```c
a = sin((M + 171*i) * 0.001584786053882726) + cos((M - 57*i) * 0.0027019724398811133)
    + cos(-M * 0.008130081300813007) * 0.1;             // degrees per frame, |a| <= 2.1
Mf = Mf * Ry(a * 0.01745329252);                        // 0x4145ac: turn (a > 0: clockwise on screen)
vtx[4].x  = trunc(-sin(M / 45.0) * 15.0);               // the tail (same for all 8 fish: no i)
vtx[5].x  = trunc(-sin(M / 37.0) * 20.0);               //   (constants are 0.0222.., 0.0270.., 0.0163.. doubles)
vtx[6].x  = trunc( sin(M / 61.0) * 25.0);
vtx[4].nx = trunc( sin(M / 45.0) * 800.0);              // and its normals (env mapping shimmer)
vtx[5].ny = trunc( sin(M / 37.0) * 800.0);
vtx[6].nz = trunc( sin(M / 61.0) * 800.0);
s = a * 30.0 + 10.0;  if (s < 15.0) s = 15.0;           // units per frame, 15..73
p = (double)pf;                                         // 0x413510
d = (0, 0, s);  p += Mf * (d * (|d| / |d|));            // 0x4136fc length, 0x413d58: move along local +z
pf = trunc(p);                                          // integer position, truncated toward zero every frame
(sx, sy) = project(p);                                  // 0x411e80, of the double position (6.2)
if (sx < -70.0) Mf = Mf * Ry(3.1415926536);             // turn around when the centre leaves
if (sx > 390.0) Mf = Mf * Ry(3.1415926536);             //   [-70, 390] x [-70, 270]
if (sy < -70.0) Mf = Mf * Ry(3.1415926536);             //   (two at once cancel)
if (sy > 270.0) Mf = Mf * Ry(3.1415926536);
r = rand();  h = 600.0;
if (r % 300 == 0) h = rand() % 40000;                   // rare big wake (32 times in the part)
if (sx > 3.0 && sx < 320 - 3 && sy > 3.0 && sy < 200 - 3) {
    X = trunc(sx); Y = trunc(sy);
    for (dx = -3; dx < 3; dx++)                         // a 15-cell right triangle:
        for (dy = -3; dy < dx; dy++)                    //   columns X-2..X+2, rows Y-3..Y+1
            A[(Y + dy) * 320 + X + dx] = (int16)trunc(h);   // 32768..39999 wrap to negative
}
```
With Mf = Rx(α)·Ry(θ) (α = 1.5707963268, θ the heading) the fish's local axes are: length (local z) in the screen
plane, height (local y) along the view direction (local +y points away from the camera), so the fish are seen
along their height axis, swimming in planes z = const. The motion per frame is
s·(sin θ, −sin α cos θ, cos α cos θ) = s·(sin θ, −cos θ, −5.1e−12 cos θ): up the screen for cos θ > 0.
**Quirk to keep:** because cos α is −5.1e−12 and the position is truncated each frame, z drops by 1 per frame
whenever the fish swims upwards (cos θ > 0) and z > 0, and never rises: the fish drift towards the camera during the
part (in the capture 5 of the 8 reach z = 0, fish 2 goes 550 → 107, fish 6 995 → 533), so they grow by up to about
30 % (distance 1600 + z). The truncation of x and y also loses up to one unit per frame towards 0.

M per fish: the 8 updates read M themselves; in the capture 76 fish updates (of 9,360) saw M + 1 or M + 2 because a
timer interrupt came during the frame, 4 had M change inside one update. A remake uses one time per frame.

First frame: the engine sets the scene's projection fields during its first render, after the fish of frame 1 have
been updated, so in frame 1 every fish projects to (0, 0): no wake, no turn.

**Ripple 0x401234** (hand-written assembler, self-modifying: the picture and screen bases and the loop count are
patched into the loop; the neighbour offsets ±0x280 bytes = ±320 words are fixed):
```c
void ripple(u8 *screen, const u8 *pic, int16 *A, const int16 *B) {
    for (p = 320; p < 320 + (200 - 4) * 320; p++) {     // height rows 1..196, all columns, linear
        v = (B[p - 1] + B[p + 1] + B[p - 320] + B[p + 320]) >> 1;   // arithmetic shifts (floor)
        v -= A[p];
        v -= v >> 4;                                     // damping 1/16
        if (v > 0x3fff) v = 0x3fff;  if (v < -0x3fff) v = -0x3fff;
        A[p] = v;
        screen[p + 320] = pic[p + 320 - (v >> 5)];       // picture and screen one row below the height row
    }
}
```
So screen pixel (x, y), y = 2..197, shows water.raw at the linear index y·320 + x + d with d = −(h(x, y−1) >> 5),
h = the new height: a purely horizontal shift by −h/32 pixels (proportional to the height, not to its slope). A
shift past the row's end continues on the next or previous row (linear addressing): x' = x + d, y' = y +
floor(x'/320), x' mod 320. In the capture about 60 % of the pixels are shifted, 20..25 % by 4 or more, up to 6 % by
16 or more, the largest shifts are ~300 pixels (big wakes). The x neighbours of column 0 and 319 are the previous /
next row's last / first cell (the edges are coupled). Rows 0 and 197..199 of the field are never updated by the
ripple; wakes can write rows 0 and 197, and those cells then keep their value for the rest of the part (values up to
30,822 were seen) and act as fixed sources for rows 1 and 196.

### 6.2 The 3D scene (engine calls)

- **Camera**: position (0, 0, −1600), identity rotation. Projection of a camera-space point (x, y, z) (0x411e80 and
  the engine's vertex projection; scene fields set by the render: centre (160, 100), scales [scene+0x18] = 32768,
  [scene+0x1c] = 27306, /16384): **sx = 160 + 256 x / z, sy = 100 + 213.33 y / z** (square pixels on a 4:3 display,
  horizontal field of view 2 atan(160/256) = 64.0°). 0x411e80 returns nothing (and the caller uses stale values)
  when z < [scene+0x44] / 256 = 28; the fish are always at z ≥ 1600.
- **Objects**: world = Mobj · v + pos (v the scaled integer vertex). The obuke: 287 vertices, 570 faces, scaled
  coordinates x −563..364, y −983..1153, z −445..458, at (−600, 0, 0): its axis at screen x = 64, taller than the
  screen (cut at top and bottom). Each fish: 8 vertices, 10 faces, x −22..22, y −31..33, z −123..145 (length 268,
  about 35 pixels at z = 2000): a double pyramid body (apexes 0 = the nose at +z and 4, around the triangle 1, 2,
  3) and a tetrahedron tail (4..7) whose vertices 4, 5, 6 the tail wag moves sideways.
- **Environment mapping** (engine 0x41acc4, for objects with byte +0xc5 & 0x10, both here), per vertex after
  projection; verified exactly:
  ```c
  n2 = 2 * (nx, ny, nz);                     // integer vertex normal (+0x34..+0x3c), length ~819 (= 4096/5)
  a = (n2.y * R[0][1] + n2.x * R[0][0]) + n2.z * R[0][2];   // R = object-to-camera rotation (here Mobj)
  b = (n2.y * R[1][1] + n2.x * R[1][0]) + n2.z * R[1][2];
  u = -(trunc(a + 2048.0) & 0xfff) - px / 64;   // px = vertex +0x20 = 128 x_cam (x scale 32768/32768)
  v =  (trunc(b + 2048.0) & 0xfff) + py / 64;   // py = vertex +0x24 = 106.67 y_cam; C division (toward 0)
  vertex.u = (int16)u; vertex.v = (int16)v;     // 1/16 texel: 4096 = the 256-texel width, the map repeats
  ```
  In texels: u = −((128 + 102.4 n̂·r0) mod 256) − x_cam/8, v = ((128 + 102.4 n̂·r1) mod 256) + y_cam/9.6: a sphere
  map over the inner 80 % of waterenv.raw that also slides with the vertex's camera-space position. The fish tail's
  modulated normals (|n| up to ~1,150) can exceed the ±2048 range and wrap.
- **Rasterizers** (engine; per face [face+8]): both map affinely (the subdivision field, bits 10..12 of the face
  flags, is 0) with u, v masked to 0..255 (material masks 0xff, shifts 8):
  - obuke 0x412db0: `pixel = waterenv[(v & 255) * 256 + (u & 255)]`, opaque; no shade table (water.shd is never
    read);
  - fish 0x41669c: `pixel = water.int[waterenv[(v & 255) * 256 + (u & 255)] * 256 + pixel]`, a read-modify-write
    blend with whatever is under it.
- **Order**: back faces culled, the remaining faces sorted far to near by their average camera z minus the object's
  bias (painter's algorithm, no z-buffer; engine). The fish's bias −256000 (= 2000 units) puts every fish face
  behind every obuke face, so the fish are drawn first (blending with the picture and with each other) and the
  spiral always covers them.

## 7. Colours

- Palette: water/water.pal (0x400364 at the start, after a white flash), no palette effects; index 0 is black.
- Background: index straight from water.raw (shifted, nearest pixel).
- Obuke: index straight from waterenv.raw.
- Fish: water.int[texel][background]. The table is a **50 % blend**: int[a][b] is the palette entry nearest to
  (pal[a] + pal[b]) / 2 (least-squares fit: 0.501 pal[a] + 0.501 pal[b] − 0.23, mean error 1.2 of 63; symmetric,
  int[a][a] = a). A true-colour remake blends the two colours 50/50, so neither picture's indices need to be kept.
- water.shd (unused): level l (0..63) gives about l/40 × the colour (0 black, 40 the colour itself, 63 ≈ 1.56×,
  saturating), 256 columns per level.

## 8. Pictures

| file | kind | use |
|---|---|---|
| water/water.raw | looks like a digitised photograph (a blurred, blue-tinted view through water of an interior with beams and a lamp; guess: Destop's or Santtu's scan, possibly a render) | full screen background, rows 2..197, shifted sideways per pixel by the ripple; read through no table, but the fish blend over it |
| water/waterenv.raw | an environment map: a shiny sphere (a face-like reflection with a bright highlight) on black, 256×256; drawn or a rendered/scanned sphere (guess) | mapped on both objects through the sphere-map coordinates above; the fish blend it 50/50 |

Both are read by index only through the palette (and the 50 % table, which a remake does in colour), so redraws in
true colour would work; neither shows a real person. Whether to redraw them is the maintainer's call (they are
blurred photo-like pictures). The env map's black parts are part of the objects' look: the position term of the
mapping (x_cam/8, y_cam/9.6 texels) moves the sampled area well away from the sphere's centre.

## 9. Time bases

| variable | time base | how |
|---|---|---|
| obuke rotation | refresh tick (tick routine) | θ = 0.1745329252 + 0.0185 n, n = tick calls (first at refresh 8468) |
| fish heading θ_i | **per drawn frame** | θ += a_i(M)·0.01745329252 per frame, + 3.1415926536 when off screen |
| fish speed / position | **per drawn frame** | s(M) units per frame along the heading, integer truncation, z drift |
| tail wag, tail normals | M (280.345 Hz), closed form | sin(M/45), sin(M/37), sin(M/61): periods 1.01 s, 0.83 s, 1.37 s |
| turn rate a_i, speed s | M, evaluated once per frame | periods 14.1 s, 8.3 s, 2.8 s (3965, 2325, 773 M) |
| wakes, drops | **per drawn frame**, rand() | 8 fish × (1 or 2) calls, then 3 × (1 or 4) calls per frame |
| ripple | **per drawn frame** | one step of the recurrence per frame |
| random numbers | 0x4134cd | init: seed 0xef012bc7 (exact on any machine); run time: per frame, seed 0xc249a56a before the first frame in the capture (11 calls in it), frame-rate dependent |

M at the part's first frame: 33719 (the M_reset of notes-core.md 1.5); in the capture M = 4 × refresh − 153 ± 2 in
the frame routine. **Frame-rate dependent**: the ripple's wave speed and decay, the fish's speed, turning, wakes and
z drift, and the random drops all advance once per drawn frame (62.2 per second in the capture, 1,172 frames). A
remake has to choose a step rate: stepping at the capture's frame times (`models/water/frame_times.py --list`:
refresh and M of every frame, measured values) with the capture's run-time seed reproduces the capture exactly,
rand sequence included; a fixed 62.2 Hz gives the same look. Say which under "Known differences". After a backwards
seek the simulation is replayed from the part start (≤ 1,172 steps of 320×200 cells).

## 10. Shortcuts a high-resolution remake should evaluate exactly, and what to keep

- **Ripple displacement**: the shift is an integer number of original pixels (h >> 5) and the picture read is
  nearest-pixel. Evaluate d = −h/32 continuously: keep the 320×200 integer simulation (its wave pattern, speed and
  decay are the content and are exact), interpolate h bilinearly at each output pixel (one original row lower), and
  read the picture filtered at x + d (with the row continuation of the linear addressing, or clamped at the edges:
  the coupling of the left and right edges and the stuck border rows are 320×200 artefacts that may be dropped).
- Keep the 2-row black bands at the top and bottom (rows 0..1, 198..199 of 200; the 3D objects cover them where
  they reach).
- **Fish and obuke**: floats instead of the integer vertex coordinates (trunc(c × 0.15) / trunc(c × 0.6)), the
  per-frame truncated fish positions may stay integer (their drift is part of the motion; the z drift is visible
  and must be kept), anti-aliased edges, perspective-correct and filtered texture reads of the sphere map (the
  original is affine and nearest-texel), the env coordinates evaluated per vertex as above (or per pixel from the
  interpolated normal), the 50 % blend in true colour instead of water.int, a z-buffer (or the same painter's order)
  for the opaque spiral, the fish drawn before it.
- The tail normals' 800·sin modulation (a shimmer of the reflection) and their wrap are part of the look.

## 11. Not determined / guesses

- The pictures' origin (photo, render or drawing) is a guess from their look.
- The engine internals used here (vertex normals of the .sro, the projection's exact integer steps, the sort keys,
  the rasterizers' sub-pixel rules) are described in notes-engine.md; they were not re-modelled in this scope.
- Why the code sets scene+0x44 = 0x1c00 instead of 0x2000 (the near plane) is not visible in its effect here
  (nothing comes that close).
