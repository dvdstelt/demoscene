# Scope "credits": the tunnel, the smoke, the spiked ball and the name pictures

Init 0x406d10 (0x40684c flight path, 0x406c70 smoke sprites), script 0x4073c8 (5:63, start), tick 0x405f30, frame
0x406794 (0x4061fc camera and spike ball, 0x406470 tunnel, 0x4063fc seam vertices), events 0x405f1c (7:00..9:32, next
name) and 0x405f10 (9:48), 0x4073e8 (11:63, end). 0x407400 (called from 0x4078e5) belongs to the water part.
Addresses are linear (NOTES.md). Refresh = screen refresh (70.086 Hz) = record of `full.frames`; "tick" = one call
of the tick routine (one per refresh). Calls into SurRender are described by what goes in and what comes out
(notes-engine.md has the engine).

Checked against the emulator (scripts in `models/credits/`, section 12), with a run resumed from `snaps/f2148.snap`
through the whole part (its 2,114 records equal `full.frames` byte for byte, pixels and DAC) and hooks that dump
the state at every tick and every drawn frame:

- the flight path table (1,024 entries: ring centres, 12 light levels, 12 radius factors) equals the model exactly,
  every int and every float bit, from rand() calls 261..13,572 of the program;
- every tick (2,106): distance, roll angle and spin matrix exactly, the smoothed camera and ball positions to
  1.5e-11 (absolute, values up to 80,000);
- every drawn frame (1,970): the camera position, the camera matrix (3e-16), all 132 tunnel vertices (x, y, z,
  u, v, light), the 12 smoke positions and the ball position and orientation exactly;
- the water mask and the name pictures: 56 dumped frames (both before and after the overlays) equal byte for byte;
- the engine's camera-space coordinates and 28.4 screen coordinates of the tunnel vertices follow the projection
  in 6.2 to the rounding of the engine (1 unit).

---------------------------------------------------------------------------------------------------------------

## 1. What is on screen

Refreshes 2148..4254 (music 5:63..11:63; about 1,970 frames drawn, 1,921 of them shown: 63.9 per second). Each
frame: clear to index 0, a tunnel of 11 rings with 12 vertices each (credits/tuubi.sro reshaped every frame) along a
precomputed wavy flight path, the camera flying through it at 47 units per refresh and rolling 1° per refresh; 12
additive smoke puffs (credits/sauhu.raw), one beside each ring centre; from 9:48 a spiked ball (credits/piikkis.sro
with an environment map) that floats ahead and closes in. Before 9:48 the whole frame is darkened at the edges by a
mask (credits/wtrmask.raw); from 7:00 to 9:32 a name picture with a star and a portrait is added at the bottom left.

| music | refresh | script | what happens |
|---|---|---|---|
| 5:63 | 2148 | 8 0x400498, 9 0x403184, 10 0x4073c8, 11 0x400498 | white flash; logo off; credits: tick 0x405f30, frame 0x406794, base palette credits/tunnel.pal (0x400364); white flash (15 refreshes, so the part fades in from white) |
| 6:32 | 2329 | 12 0x400498 | white flash |
| 7:00 | 2505 | 13 0x405f1c | white flash; name 0: DESTOP |
| 7:32 | 2680 | 14 0x405f1c | white flash; name 1: WILI |
| 8:00 | 2856 | 15 0x405f1c | white flash; name 2: JOUNI |
| 8:32 | 3032 | 16 0x405f1c | white flash; name 3: JULIET |
| 9:00 | 3207 | 17 0x405f1c | white flash; name 4: YOLK & LEGEND |
| 9:32 | 3382 | 18 0x405f1c | white flash; name index 5: no name is drawn (SANTTU's picture is never shown, 4.2) |
| 9:48 | 3470 | 19 0x400498, 20 0x405f10 | white flash; [0x426430] = 1: the ball appears, the mask goes, the ball starts closing in |
| 10:32 | 3734 | 21 0x400454 | inverse flash |
| 11:00 | 3909 | 22 0x400498 | white flash |
| 11:32 | 4085 | 23 0x400454 | inverse flash |
| 11:48 | 4173 | 24 0x400438 | fade to black (40 refreshes): black from 4213 |
| 11:63 | 4255 | 25 0x400498, 26 0x4073e8 | flash; routines off, frees every block of tag [0x426480] (then the sprites part) |

The flashes and fades are the core's palette routines (notes-core.md); the part runs no palette effect of its own.

## 2. Data

| what | where | size, format | used as |
|---|---|---|---|
| palette | credits/tunnel.pal | 768, 6-bit | base palette of the whole part (0x400364) |
| tunnel texture | credits/tunnel.raw | 256 × 1024 bytes, indices 0..220 | tunnel faces, through tunnel.shd |
| tunnel shade | credits/tunnel.shd | 64 rows × 256: [row][index] | light level of the tunnel (5.1) |
| smoke sprite | credits/sauhu.raw | 64 × 64, levels 0..30 | smoke puffs, through smoke.shd |
| smoke blend | credits/smoke.shd | 256 × 256: [level][background], rows 32..255 all 0 | additive smoke (5.2) |
| name blend | credits/tunnel.int | 256 × 256: [level][background], rows 32..255 all 0 | name pictures (5.3) |
| mask | credits/wtrmask.raw | 320 × 200, levels 0..31 | the water mask (5.4) |
| mask blend | credits/wtrmask.int | 256 × 256: [level][background], rows 32..255 all 0 | the water mask (5.4) |
| ball texture | credits/piikkis.raw | 256 × 256, indices 1..255 | environment map of the ball, direct |
| names | credits/kredu1, 7, 2, 8, 3, 6.raw | see 4.2 | name pictures, through tunnel.int |
| tunnel object | credits/tuubi.sro | 132 vertices (11 rings × 12), 240 faces | loaded at scale 7.0; only ring 0's x, y and the ring spacing are used (3.2) |
| ball object | credits/piikkis.sro | 252 vertices, 260 faces | loaded at scale 0.8 (0xcccc); radii 220.7..408 after scaling |
| path constants | 0x420fa4..0x4210d4 | doubles (and the float 1008.0 at 0x4210d4) | 3.1 |
| tick constants | 0x420f38..0x420f88 | doubles | 3.3 |
| ring v table | built at init in [0x426338 + 4k], k = 0..21 | int (k · 0x1000) / 10 | 3.5 |

State (BSS unless noted): 0x426434 float pos (distance flown), 0x42284c float θ (roll, data, 0.0), 0x422850 float
s (ball lead, data, 9.0), 0x426390 3 doubles C (camera), 0x426320 3 doubles S (ball), 0x4262d8 3 × 3 doubles
(ball spin), 0x426430 event (9:48), 0x426438 name index (−1 after init), 0x426470 D (ring spacing = 686),
0x426424 the path (0x1b000 bytes), 0x425fe8 11 seam vertex records (0x40 each), 0x4262a8 12 smoke objects.
Engine records: [0x426460] the scene / viewport (0x50 bytes, "credit scene"), [0x42645c] the camera (0xc8),
[0x42647c] the tunnel object, [0x426468] an untouched copy of it (the template), [0x426464] the ball, [0x426474]
the tunnel material (0x44), [0x426450] the ball material, [0x4263c0 + 4i] the six name bitmaps, [0x426458] the mask
bitmap. 0x4263d8 is set to an identity matrix and never read.

## 3. Algorithms

### 3.1 The flight path (0x40684c, at init)

1,024 entries of 0x6c bytes: `int x, y, z` (+0), `int light[12]` (+0xc), `float radius[12]` (+0x3c). All x87
arithmetic is at 53-bit precision (control word 0x127f, read from the snapshot; with 64-bit intermediates one radius
of 12,288 comes out one ulp off, with 53 bits all match). The divisions are multiplications by double constants:
1/7.1 is the double at 0x420fa4 and so on; three of them are one ulp off the correctly rounded 1/x: 0x420ff4
(1/6.3) = 0x1.4514514514515p−3, 0x42104c (1/1200) = 0x1.b4e81b4e81b4ep−11, 0x42108c (1/41.3) =
0x1.8cb4ff6151337p−6. A remake reads all constants from the image.

```c
D = 686;                                    // [0x426470], 3.2
float X = 0, Y = 0, Z = 0;
for (i = 0; i < 1024; i++) {
    a = i + 111; b = i + 15; c = i - 10; d = i + 30; e = i + 60; g = i + 20; h = i + 10;
    double P = fabs(5530 * cos(g / 7.1)) + fabs(1500 * sin(i / 10.3)) - 1650 * cos(c / 14.4)
             + 2757 * cos((i + 20.4) / 16.5) + fabs(3130 * sin(b / 14.3));
    double Q = fabs(1250 * sin(a / 6.3)) - fabs(4240 * cos(g / 10.1)) + 2363 * cos(e / 28.4)
             - 2653 * cos(i / 14.5) - sin(d / 21.3) * 1100 * 1.2;
    double v[3] = { P, Q, D };              // 0x413510
    k = D / sqrt(v[1]*v[1] + v[0]*v[0] + v[2]*v[2]);   // 0x413750: scale to length D (k stored as double)
    X += v[0] * k; Y += v[1] * k; Z += v[2] * k;       // float accumulators
    int r1 = rand() % 900;
    float base = r1 / 1200.0 + 0.65;        // 0.65 .. 1.399
    for (j = 0; j < 12; j++) {
        int k1 = i + j, k2 = i - j;
        int r2 = rand() % 200;
        double rr = fabs(sin(j / 3.1) * 0.4) + base;               // stored as double
        path[i].radius[j] = r2 * 0.001 + rr;                        // float, 0.65 .. 1.998
        double s3 = cos(k2 / 17.1) * 0.5 + sin(i / 10.3) * 0.5 + sin(h / 13.1) * 0.8 + cos(h / 41.3) * 0.2;
        double t = fabs(s3 / 3 + sin(k1 / 2.23)) + sin((i + j * 0.8) / 3.05) * 0.4;
        float L = t * 64 * 16;
        if (0 > L) L = 0; if (L > 1008.0) L = 1008.0;              // 1008 = 63 * 16: shade row 63
        path[i].light[j] = trunc(L);
    }
    path[i].x = trunc(X); path[i].y = trunc(Y); path[i].z = trunc(Z);
}
```

(The order of the terms is the order of the code; the model `path.py` spells every rounding.) The steps are D long
and point along (P, Q, D) normalised; P and Q are thousands, so the path runs mostly in the x-y plane (entry 0
(547, −410, 49), entry 1023 (580803, −189352, 81331)); the part uses entries 0..~150 (pos reaches 99,000).

**Random numbers.** Watcom rand() 0x4134cd. The intro's init makes the program's first 260 calls (seed 1); this
precalc makes calls 261..13,572 (the generator state is 0x8e60e23d when it starts), in the order r1 then the 12 r2
per entry. Nothing in the part calls rand() at run time.

### 3.2 Engine set-up (0x406d10)

```c
scene  = alloc(0x50, "credit scene");   0x410878(scene);                 // defaults: +0x3c 0x100, +0x44 0x2000, +0x48 0x400000
0x410618(scene, 320, 200, 0, 0, 320, 200);  // width, height, clip x0, y0, x1, y1
scene->camera = camera; scene->+0x3c = 0x100; scene->+0x44 = 0x4000;      // focal 256, near plane 0x4000 / 128 = 128
camera = alloc(0xc8, "credit camera"); 0x410520(camera); camera.pos = (0,0,0); angles (0,0,0); link into scene
tunnel = object(tuubi.sro, scale 0x70000 = 7.0, material tunnel_mat); faces' renderer 0x414e5c; link
ball   = object(piikkis.sro, scale 0xcccc = 0.8, material ball_mat); faces' renderer 0x412db0;
         ball.flags(+0xc4) |= 0x1001 (bit 0 = hidden; 0x1000 = environment-mapped u, v, notes-engine.md); link
spin (0x4262d8) = identity;  tunnel.flags |= 0x100;
0x41172c(tunnel, 0, 0, -v0.z): moves every vertex so that ring 0 is at z = 0
template = copy of tunnel (0x411c80)
tunnel_mat: texture tunnel.raw, shade tunnel.shd, +0xc 0, +0x18 0, u mask 0xff, v mask 0x3ff, sizes 8 / 10 bits
ball_mat:   texture piikkis.raw, no shade table, u / v masks 0xff, sizes 8 / 8 bits
D = tunnel.vertex[13].z - tunnel.vertex[0].z;     // = 686 (ring spacing of tuubi.sro at scale 7)
bitmaps (0x415e50: pixels, pitch, width, height, name; type +0, table +0x20, key +0x24 = 0):
   [0x4263c0] kredu1 173 x 75 "destop"   [0x4263c4] kredu7 129 x 75 "wili"   [0x4263c8] kredu2 146 x 75 "jouni"
   [0x4263cc] kredu8 154 x 70 "jussi"    [0x4263d0] kredu3 318 x 82 "yolk+legend"  [0x4263d4] kredu6 294 x 75 "santtu"
   all type 4 with table tunnel.int;  mask = wtrmask.raw 320 x 200, type 2, table wtrmask.int
v table: [0x426338 + 4k] = (k * 0x1000) / 10, k = 0..21 (0, 409, 819, 1228, 1638, 2048, ...)
seam: the last template vertex's next = 0x425fe8; for k = 0..8: face[24k+22].v0 = S(k), .v1 = S(k+1), face[24k+23].v1 = S(k+1)
smoke: 0x406c70, 12 sprite objects (3.6), linked into the scene
C = S = (0, 0, 0); event = 0; name = -1
```

The template's ring (the 12-gon the part uses; the remake reads it from tuubi.sro through the engine's .sro loader at
scale 7): (0, 483), (−245, 420), (−420, 238), (−483, 0), (−420, −238), (−245, −413), (0, −476), (238, −413),
(413, −238), (476, 0), (413, 238), (238, 420) — all eleven rings of the file have the same x, y, at z = 0, 686,
1365, 2051, … Of the file only these 12 pairs, D and the face list (two triangles per quad, 3.5) matter.

### 3.3 The tick routine (0x405f30, once per refresh)

```c
pos += 47.0;                                          // float [0x426434]
n = trunc(pos / D); while (n < 0) n += 1024; n %= 1024;
float f = (trunc(pos) % D) / (double)D;               // stored as float
theta += 0.01745329252;                               // float [0x42284c]; 0x420f38, not exactly pi/180
C = C * 0.8 + lerp(path[n], path[n + 1], f) * 0.2;   // doubles; lerp(a, b, f) = a * (1 - f) + b * f; n + 1 not wrapped
spin = spin * Rx(0.01) * Ry(0.008675) * Rz(0.0123);   // 0x4145f4 + 0x414070 (right-multiply), radians per refresh;
                                                      // the ball's vertex v is at spin * v + S (6.2)
if (event) s -= 0.015;                                // float [0x422850], 9.0 at the start
m = (n + trunc(s) + 3) % 1024;
L = lerp(path[m], path[(m + 1) % 1024], f);  L.x = (float)L.x;
S = S * 0.96 + L * 0.04;                              // doubles
```

with Rx(a) = [[1,0,0],[0,cos a,−sin a],[0,sin a,cos a]], Ry(b) = [[cos b,0,sin b],[0,1,0],[−sin b,0,cos b]],
Rz(c) = [[cos c,−sin c,0],[sin c,cos c,0],[0,0,1]] (0x4145f4(a, b, c) builds Rx(a) Ry(b) Rz(c); checked on the
dumped spin matrix). The camera's point is n + f rings along the path; C trails it (exponential, factor 0.8 per
refresh: about 188 units = 0.27 ring behind at 47 units per refresh). The ball's point is 3 + trunc(s) rings
further: 12 rings until 9:48, then 11 from the first decrement, one ring less every 66.7 refreshes, down to
3 + (−2) = 1 ring at the end (s = 9 − 0.015 × 784 = −2.76 at refresh 4254); S trails its point with factor 0.96
(about 1,130 units = 1.6 rings behind), so at the end the ball's centre reaches the camera.

### 3.4 The camera (0x4061fc, at the start of each drawn frame)

Ticks are blocked while it runs ([0x42278c] guard; an interrupt that comes then is lost entirely, notes-core.md 1.6).

```c
n = trunc(pos / D) wrapped to 0..1023;  fi = trunc(pos) % D;
camera.pos = (trunc(C.x), trunc(C.y), trunc(C.z));                   // ints
m = (n + 3) % 1024;  f = fi / D (53-bit, not rounded to float);
T = lerp(path[m], path[(m + 1) % 1024], f);
target = (trunc(0.8 * camera.x + 0.2 * T.x), trunc(T.y), trunc(T.z));  // (sic) only x is mixed with the camera
0x41077c(camera, target):        // look-at, 0x414cec
    d = normalize(target - camera.pos);
    r = normalize(-d.z, 0, d.x);
    u = normalize(cross(d, r));
    camera.M = rows (r; u; d);   camera.flags |= 8;
ball.M = spin;  ball.pos = (trunc(S.x), trunc(S.y), trunc(S.z));
```

After the tunnel update (3.5) the frame multiplies `camera.M = camera.M · Rz(θ)` (0x4145f4(0, 0, θ) and 0x414070),
with θ as read at the end of 0x406470.

### 3.5 The tunnel (0x406470) and the seam (0x4063fc)

```c
tunnel.pos = camera.pos;                       // vertices are relative to the camera
q = trunc(pos) / D;  r10 = q % 10;             // q is not wrapped (it stays < 150)
for (k = 0; k < 11; k++) {                     // ring k = path entry e = (q + k) % 1024
    e = (q + k) % 1024;
    v = [0x426338 + 4 * (r10 + k)];            // ((r10 + k) * 4096) / 10
    for (j = 0; j < 12; j++) {
        rad = path[e].radius[j];
        w = camera.M · (template[j].x * rad, template[j].y * rad, 0);   // 0x4144ec: M times column vector, look-at M (before Rz)
        vert.x = trunc(path[e].x + w.x - camera.x);  vert.y, vert.z likewise;
        dist = sqrt(x*x + y*y + z*z);           // int32 sum
        ex = trunc(dist - 7 * D);               // 4802
        L = path[e].light[j];  if (ex > 0) L -= ex;  if (L < 0x70) L = 0x70;
        vert.u (+0x28, word) = trunc(j * 256 * 16.0 / 12);         // 0, 341, 682, 1024, ..., 3754
        vert.v (+0x2a, word) = v;
        vert.light (+0x30, word) = L;           // 112 .. 1008
    }
}
smoke[k].pos = (path[(q + k) % 1024].x + 180, .y + 180, .z), k = 0..11
// 0x4063fc: seam copies S(k) = vertex 12k (first vertex of ring k), k = 0..10, with u = 0x1000 + 0x1000 / D = 4101
```

**Faces.** Ring gap k (rings k, k+1; k = 0..9) has 24 triangles: quad j (vertices j, j+1 of both rings) is
(b[j+1], a[j+1], a[j]) and (b[j], b[j+1], a[j]) with a = ring k, b = ring k+1. For the closing quad j = 11 the
init replaced, in gaps 0..8, a[0] and b[0] by the seam copies S(k), S(k+1) (u = 4101 instead of 0), so the texture
wraps once around the ring (u = 4096 is the texture width, 5.1). Gap 9 keeps u = 0 at its closing quad, so there the
whole texture width is squeezed backwards into one 30° segment (sic); gap 9 is the farthest (rings 9..10, about
6,200..6,900 units ahead) and nearly black (the distance fade above), so it hardly shows. In gaps 0..8 the first
triangle of the closing quad also has its winding reversed; it is drawn all the same (in the dumps every face
beyond the nearest gaps carries the engine's visible bit 0x100000 at face +0x30, the reversed ones included).

**Where the rings are.** In camera space (6.2) a ring vertex is R^T (path[e] − c) + Rz(θ)^T (x·rad, y·rad, 0) with
R = M · Rz(θ) (up to the integer truncations), because R^T M = Rz(θ)^T: every ring is a 12-gon parallel to the image
plane, centred on its path point, turned by −θ. The tunnel's texture is fixed to the path: path entry e always gets
v ≡ (e mod 10) · 409.6 (mod 4096), vertex j always u = j · 4096 / 12; the rings turn on screen with −θ (1° per
refresh) and every ring has its own radii and lights.

### 3.6 Smoke (0x406c70)

12 sprite objects (0x415790, 0x104 bytes "tunnel smoke"): draw routine 0x415dc8 (mode 1: blended), pixels sauhu.raw,
table smoke.shd, pitch / width / height 64, +0xec 1.0, +0xf0 0.35 (size factor), +0xf4 0 (depth bias). The engine
(0x4157d4, 0x415878; notes-engine.md "Billboards") draws a sprite as a screen-aligned square, nearest-neighbour:
width 64 × 0.35 × F / z and height 64 × 0.35 × G / z in 1/16 pixel (F = 32768, G = 27306 from the scene, z =
camera-space depth), i.e. a square of 64 × 0.35 × 8 = 179.2 world units; its centre is at (160 + 256 X / Z, 100 +
256 Y / Z) pixels — the vertical offset without the 5/6 of the faces' projection (6.2), so a puff above or below the
screen centre sits 1.2 times further out than its point (sic; keep). Sprites with depth below 128 are skipped; they
are depth-sorted with the faces (key 8 · z). Each pixel: dst = smoke.shd[level][dst] (no transparent key; row 0 is
the identity). The 12th puff (path entry q + 11) is beyond the tunnel's last ring.

### 3.7 The frame routine (0x406794)

```c
scene.buffer (+8) = [0x4248d0];            // the draw target
guard++; 0x4061fc(); guard--;               // 3.4
0x406470();                                 // 3.5, ends with camera.M = camera.M * Rz(theta)
ball.flags = event ? flags & ~1 : flags | 1; // the ball is drawn only from 9:48
0x41127c(scene, 0);                         // clear the clip window (all 320 x 200) to index 0
0x40c6ac(scene);                            // render: tunnel, ball, smoke (engine)
if (!event) 0x415df0(mask, 0.0, 0.0);       // 5.4
if (0 <= name && name < 5) 0x415df0(names[name], 0.0, 117.0);   // 5.3
0x400bec();                                 // debug text, off ([0x42277c] = 0)
0x400b14();                                 // flip
```

## 4. Pictures and textures

### 4.1 Overview

| file | kind | how it is used |
|---|---|---|
| tunnel.raw 256 × 1024 | drawn (a painted swirl texture in orange, yellow, violet, purple and black; not made by the code) | mapped on the tunnel (affine per triangle), one width around a ring, the height over 10 rings; read through tunnel.shd (palette indices 0..220): a redraw would have to keep its indices, or the remake shades its true colour (5.1) |
| piikkis.raw 256 × 256 | drawn (a painted swirl in violet, white, yellow, orange on dark) | environment map of the ball, texel index straight to the screen (no table): a true-colour redraw works |
| sauhu.raw 64 × 64 | drawn (a soft round puff with a flower-like centre), levels 0..30 | smoke sprite, read through smoke.shd as brightness levels: a redraw must stay a level map |
| wtrmask.raw 320 × 200 | drawn (a full-screen vignette: bright mottled centre with round bubbles, dark swirled fur-like edges), levels 0..31 | read through wtrmask.int as levels: a redraw must stay a level map |
| kredu*.raw | name pictures (4.2), levels 0..31 | read through tunnel.int as levels |

### 4.2 The name pictures

Each is a level map (index = brightness level 0..31, 0 transparent): a flat five-pointed star at level 10, a
cut-out greyscale portrait on the star (levels 1..31), and the name in heavy oblique (italic) sans-serif capitals at
level 31 with a thin anti-aliased edge (levels 1..7). The portraits are **digitised photos of real people** (scanned
faces; DESTOP's and WILI's show face paint): not redrawn. The star and the letters are drawn shapes: a redraw is
possible but has to stay a level map (levels 10 and 31), because the picture is added to the tunnel through
tunnel.int. Drawn at x = 0, y = 117 (bottom left; the bottom of the screen for the 82-row YOLK & LEGEND).

| index (event) | file | bitmap | text | layout |
|---|---|---|---|---|
| 0 (7:00) | kredu1.raw | 173 × 75 | DESTOP | star with face at the left, name right of it |
| 1 (7:32) | kredu7.raw | 129 × 75 | WILI | name at the left, star with face at the right |
| 2 (8:00) | kredu2.raw | 146 × 75 | JOUNI | star with face at the left, name right |
| 3 (8:32) | kredu8.raw | 154 × 70 of the file's 154 × 72 | JULIET | star with face, name right; the bitmap is created with height 70 (0x407237), so the file's last two rows (5 pixels of the star's tip) are never drawn |
| 4 (9:00) | kredu3.raw | 318 × 82 | YOLK & LEGEND | two stars with faces (left and right), the name below between them |
| 5 (9:32) | kredu6.raw | 294 × 75 | SANTTU of UNIQUE ("of" in lower case) | prepared but never shown: the frame routine draws only indices 0..4 |

## 5. How each pixel's index is made

The palette is credits/tunnel.pal (an unordered 221-colour set for the tunnel texture, 0..220, then 221..255 a ramp
from black through dark reds and violets to white; the ball's texture uses indices of both, 1..255). Every table
below is a blend in RGB with the palette, quantised to the palette; the measurements are least-squares fits over all
256 colours of a row (models/credits/tables.py); the remake can derive the per-row factor / offset from the table at
load time instead of copying numbers.

### 5.1 Tunnel: texture through tunnel.shd

pixel = tunnel.shd[row][tunnel.raw[v][u]], row = floor((light + 1) / 16) (the vertex light 112..1008, interpolated
linearly in screen space like u and v by 0x414e5c: the tunnel's faces have bits 10..12 of face +0x30 clear, so the
rasterizer's perspective path is off; notes-engine.md, the rasterizers). Row r is the colour times r / 63 (row 0 =
index 221, black; row 63 the identity; fit k(r) · 63 / r = 0.99..1.02 for r ≥ 18, lower below from quantisation). So
in true colour: pal[texel] × (light / 1008). Texture coordinates: u, v words with 0x1000 = the whole texture (u: 256
texels per 4096, one texture width per ring; v: 1024 texels per 4096 = 10 rings): the rasterizer takes the texel row
as v_acc >> (24 − hlog) with hlog = 10 for this material, so a v word is 1/4 row here (notes-engine.md takes every
INSIDE material as 256 × 256; this one is 256 × 1024: material +0x3c v mask 0x3ff, +0x41 hlog 10).

### 5.2 Smoke: additive through smoke.shd

dst = smoke.shd[level][dst]; row L adds about 1.13 L (6-bit units) to each channel and saturates at 63: measured
offsets for L = 0..31: 0 0 1 4 5 6 7 8 9 10 11 12 14 15 16 17 18 19 20 22 23 24 25 26 27 28 30 31 32 33 34 35.

### 5.3 Names: additive through tunnel.int

dst = tunnel.int[level][dst] where level ≠ 0 (0x416324, key 0). Row L adds k(L) and saturates; k for L = 0..31:
0 0 3 5 7 8 10 11 14 16 17 18 20 23 25 26 28 30 32 34 36 37 39 41 44 45 47 49 51 52 52 52 (about 1.75 L). So the
star (level 10) brightens the background by 17/63, the letters (31) nearly saturate to white, the face is a
brightening by its grey level.

### 5.4 The water mask: signed additive through wtrmask.int

dst = wtrmask.int[mask][dst] over the whole screen (0x416174, no key), before 9:48. Row L adds k(L) (negative =
darker) and clamps to 0..63: k for L = 0..31: −31 −29 −29 −27 −26 −24 −23 −22 −20 −19 −17 −16 −15 −13 −12 −9 −8 −8 −7
−6 −3 0 0 0 0 4 6 7 8 8 10 12 (rows 21..24 neutral). The mask's centre (levels 25..31) brightens a little, the edges
darken by up to half.

### 5.5 Ball

pixel = piikkis.raw[v][u] (0x412db0: affine texture, no shading); u, v are written by the engine per frame (they are
0 in the object after loading): the environment mapping of flag 0x1000 (u, v from the camera-space vertex normal and
the screen position, notes-engine.md stage 0x41acc4).

## 6. Engine calls (what goes in, what comes out)

### 6.1 Calls

| call | in | out / effect |
|---|---|---|
| 0x410520(obj) | | zero 0xc8 bytes, identity orientation (0x4108dc → 0x4145f4(0,0,0) at +0x28) |
| 0x410878(scene) | | +0 0x4229c0 (render stages), +0x3c 0x100, +0x44 0x2000, +0x48 0x400000 |
| 0x410618(scene, w, h, x0, y0, x1, y1) | 320, 200, 0, 0, 320, 200 | size +0x20 / +0x24, clip +0x28 / +0x2c / +0x30 / +0x34 |
| 0x410868(obj, x, y, z) | 0, 0, 0 | position +0x10 / +0x14 / +0x18 |
| 0x4108a8(obj) | | zero, identity orientation |
| 0x410bb0(obj, sro, scale 16.16, material) | tuubi 7.0, piikkis 0.8 | vertices (+0xd0 list, 0x40 each: +8/+0xc/+0x10 x, y, z), faces (+0xd8 list, 0x34 each: +8 renderer, +0x14/+0x18/+0x1c vertex pointers), counts +0xd4 / +0xdc |
| 0x410850(obj, renderer) | 0x414e5c / 0x412db0 | sets every face's renderer |
| 0x411154(scene, obj) | | links the object into the scene's list (+4) |
| 0x41172c(obj, dx, dy, dz) | | moves every vertex |
| 0x411c80(dst, src) | | copies an object with its vertices and faces |
| 0x412070(dst, src) | | copies one vertex record, next = 0 |
| 0x41077c(cam, x, y, z) | target | look-at (3.4) |
| 0x4145f4(M, a, b, c) | radians | M = Rx(a) Ry(b) Rz(c) |
| 0x414070(A, B) | | A = A · B |
| 0x4144ec(v, M) | | v = M · v |
| 0x413e68(dst, src) | | copies a 3 × 3 matrix |
| 0x413510(v, a, b, c) / 0x413750(v, len) | | set a vector / scale it to length len |
| 0x415e50(pixels, pitch, w, h, name) | | bitmap (0x28 bytes: +0 type, +4 1.0f, +8 pitch, +0xc width, +0x10 height, +0x18 pixels, +0x20 table, +0x24 key) |
| 0x415df0(bitmap, float x, float y) | | 2D blit by type (5.3, 5.4); x, y truncated, clipped to the clip window |
| 0x415790(obj) | | sprite object (3.6) |
| 0x41127c(scene, colour) | 0 | fills the clip window with the colour |
| 0x40c6ac(scene) | | renders the scene (runs the stage list at scene +0) |

### 6.2 Camera convention and projection (checked on the dumped vertices)

The engine maps a point p to camera space with the transpose of the stored matrix: (X, Y, Z) = R^T (p − c),
X_j = Σ_i R[i][j] (p_i − c_i), R = the camera matrix (rows r, u, d from the look-at, times Rz(θ)). So the camera's
axes in world space are the columns of R, and the view direction is the third column (r.z, u.z, d.z) — not d, the
direction to the target (the look-at is used transposed; with d = (0.23, −0.97, 0.10) at the start the view looks
along (0.92, −0.39, 0.10), about 15° off the path's direction). Rz(θ) on the right turns the first two columns: a
roll about the view axis. Vertex fields after the transform: +0x20 = 128 X, +0x24 ≈ 128 · (5/6) Y, +0x1c = 128 Z;
screen +0x14 = 16 · (160 + 256 X / Z), +0x18 = 16 · (100 + 256 · (5/6) Y / Z) (28.4 fixed point); y grows downwards.
So: focal length 256 pixels horizontally, 213.3 vertically (square pixels on a 4:3 screen), horizontal field of view
2 atan(160 / 256) = 64.0°, vertical 50.2°; near plane z = 128 (scene +0x44 = 0x4000, the default is 0x2000).
An object's own matrix is applied the other way round: the ball's vertex v is at world point spin · v + S (column
vector), so its camera-space point is R^T (spin · v + S − c) (checked on the 148 vertices the engine transformed in
each of two dumped frames after 9:48, to 0.1 unit; the object centre R^T (S − c) is at +0xb8 of the object, × 128).
Clipping, sorting and the rasterisers are the engine's (notes-engine.md).

## 7. Time bases

| variable | time base | formula |
|---|---|---|
| pos | per refresh | 47 T (T = ticks since the part started; exact in float) |
| θ (roll) | per refresh | float sum of T × 0.01745329252 rad (≈ T degrees) |
| C (camera) | per refresh, recurrence | C_T = 0.8 C_{T−1} + 0.2 P(T), C_0 = 0, P(T) = path point at pos_T (3.3); no closed form (the path is a table) |
| S (ball) | per refresh, recurrence | S_T = 0.96 S_{T−1} + 0.04 Q(T), S_0 = 0, Q at m = n + 3 + trunc(s) |
| spin | per refresh | E multiplied T times (E · E · … · E), E = Rx(0.01) Ry(0.008675) Rz(0.0123) |
| s | per refresh after 9:48 | 9.0 − 0.015 × (ticks since the event), float steps |
| name | script events | −1, then +1 at 7:00, 7:32, 8:00, 8:32, 9:00, 9:32 |
| event | script event 9:48 | ball shown, mask off, s decreasing |
| everything else | per drawn frame, from the tick state | camera, tunnel, smoke, ball position: no frame-rate dependence |

The first tick runs at the retrace of record 2148 (notes-core.md 1.5: M = 8442), so T = record − 2147 at the
records' retrace. In the capture a frame shown at record k was drawn with T = k − 2149 (k − 2148 .. k − 2150): the
drawing takes about a refresh and the copy comes at the next retrace. A remake renders refresh k with
T = k − 2147 (plus the fraction of the refresh), interpolating C, S and the spin between ticks, and replays the
recurrences from the part's start after a backwards seek (2,106 steps at most). The 9:48 entry runs at the end of a
frame drawn in refresh 3470: the next drawn frame (shown at record 3471) has the ball and no mask, and s first falls
at the next tick, the retrace of record 3471 (tick 1,324). The name events likewise change the frame drawn right
after them (first frames with names 0..5 drawn in refreshes 2505, 2680, 2856, 3032, 3207, 3382).

Races in the original (ignore in a remake): a tick can come after 0x4061fc and before 0x406470 reads pos (24 of
1,970 frames in the capture), or during 0x406470 (3 frames), so the tunnel can use a newer pos than the camera; an
interrupt that comes during 0x4061fc is dropped (once in the part, record 3045).

## 8. Shortcuts a high-resolution remake should evaluate exactly

- Integer positions: the camera position, the target, the vertex coordinates (relative to the camera) and the ball
  position are truncated to ints; evaluate in float (keep the formulas).
- pos and the ring index: the camera and the rings step with trunc(pos) in units; with pos continuous (47 per refresh
  interpolated) the motion is smooth.
- The ring v values (k · 4096) / 10 are integer-rounded: use k · 409.6.
- The seam u = 4101 (0x1000 + 0x1000 / D, sic): 4096 closes the ring exactly; the 5/4096 offset is 0.3 texel.
- Shade rows: the light is used in 64 steps through tunnel.shd: use pal[texel] · light / 1008 continuously, with
  a filtered texture.
- The blend tables (smoke, names, mask) are additive with saturation quantised to the palette: add the measured
  offset k(level) in RGB (derived from the table), with the level maps filtered.
- The smoke sprite is drawn nearest-neighbour with 1/16-pixel positions: draw a filtered 179.2-unit square.
- The engine's fixed-point projection (28.4) and rasterisers: float, anti-aliased edges; the textures (tunnel,
  ball) and the tunnel's light are interpolated affinely per triangle: interpolate them perspective-correct.
- The ring geometry (camera-facing 12-gons, the transposed look-at, the x-only mixing of the target, the distance
  fade beyond 4,802 units, the minimum light 112) is what the original shows: keep it.

## 9. Whole frame

Yes: 0x41127c clears the whole 320 × 200 clip window to index 0 every frame, then the 3D render, then (before 9:48)
the mask over the whole screen, then the name (checked with runs that fill the buffer with a marker before 0x41127c:
no pixel keeps it). Where no face is drawn (beyond the tunnel's open far end) the screen shows index 0
(RGB 1, 0, 0: black).

## 10. Quirks that are part of the look (keep) and bugs

- The view axis is the look-at matrix's third column, not the direction to the target (6.2), and the target's x is
  0.8 camera + 0.2 path (3.4): together they decide where the tunnel's far end sits on screen. Keep.
- SANTTU of UNIQUE (kredu6.raw) is loaded and prepared but never shown (the sixth event sets index 5; only 0..4 are
  drawn).
- JULIET's bitmap is 70 rows of the file's 72 (two rows with 5 star pixels never shown).
- Gap 9's closing quad has the texture width squeezed backwards (3.5); it is in the dark far end.
- The 12th smoke puff floats beyond the tunnel's last ring, in the black.
- The ball's position is computed (and trails its point) from the part's start although it is hidden until 9:48;
  at 9:48 it appears about 12 rings ahead, beyond the tunnel's end.

## 11. Not determined here

- The engine's sorting (painter's order of the faces, the ball and the sprites), its clipping at the near plane and
  the ball's environment-map coordinates: notes-engine.md. The rasterised pixels of this part were not compared
  with a model (the vertex data that goes into the rasterizers is exact).
- Whether the portraits' face paint is make-up or retouching (they are photos either way).

## 12. Models (`models/credits/`, see its README.md)

| file | what it checks |
|---|---|
| `path.py` | 0x40684c: the 1,024-entry path, exact against snaps/f2148.snap |
| `seed.py` | the number of rand() calls before the path (260) |
| `model.py` | the tick routine and the per-frame camera, tunnel, smoke and ball against a dump |
| `blit.py` | the mask and name overlays against dumped buffers |
| `coverage.py` | which pixels the clear and render leave untouched (marker runs) |
| `latency.py` | at which record a drawn frame is shown |
| `tables.py` | the fits of the shade and blend tables (section 5) |
| `run_inside.py` | a copy of the runner with dump hooks (`--dump`, `--dump-buf`, `--mark`, `--sprites`) |
