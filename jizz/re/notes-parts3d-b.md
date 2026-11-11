# Scope "parts3d-b": the tube object (0x15c8b), the spiky object over clouds (0x15d20), the blobs (0x10fda)

Addresses are offsets in `jizz_img.bin` (= linear addresses in the emulator). "Timer" = xmpGetTimer (0x29f44), PIT
clocks (1,193,182 Hz). Music positions are order:row in hex, as in NOTES.md. Angles in radians unless marked as
1024ths of a turn. Screen coordinates refer to the original 320x200 frame (shown at 4:3). float = IEEE single; all x87
code here runs with 24-bit precision (main 0x18712, 0x769f, 0x1f5d1), so numpy float32 reproduces it to ~1e-6
relative.

All three parts are made with Nix's vector engine: an object **script** builds static meshes once, a **camera path**
moves through them, and per frame the engine renders every object (texture + "spot" light map through the colour
tables, see notes-engine / notes-core). The tube and spiky parts animate only the camera; the blob part rebuilds a
marching-cubes mesh of six metaballs every frame. No part uses the random generator, and nothing depends on the
frame rate: everything is a function of the timer and the sync event.

What was checked against the emulator (scripts in `models/parts3d-b/`, section 9):
- object scripts (section 2.2): the Python interpreter rebuilds every object of the three scripts — vertex
  positions (max error 2.4e-4 units, float rounding), UVs (exact), face lists (identical), texture/mode bytes,
  vertex normals (1.8e-6 relative) — against the engine buffers at t3285, t4435 and t7506;
- camera (section 2.3): eye, target, roll, the 3x3 matrix and the translation match the snapshots to 5e-7 (matrix)
  and the engine's per-vertex screen coordinates, depths and env-map coordinates to 0.02 px in dumps at ticks 3600,
  4702 and 8100;
- blobs (section 5): ball positions, the field, the polygoniser, edge vertices, normals and UVs reproduce the
  engine's blob mesh **exactly** (same vertices in the same order, same faces, normals to 1.8e-7) in 6 dumps between
  ticks 7523 and 8500; the colour tables match the formula at all three parts and during the inversion;
- whole frames: an approximate rasteriser fed with the models' geometry (camera, scripts, blobs) draws the same
  triangles as the engine and matches the engine's page to a mean of 4.5–7.4/255 per channel at ticks 3300, 3700,
  4702, 7900, 8100 and 8700 (the remainder is the rasteriser: affine span details, texel rounding).

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | main call | music / ticks | starts on | ends when | fps (capture) | look |
|---|---|---|---|---|---|---|
| tube | 0x18bfd | 06:00–08:00, ticks 3279–4046, 47.10–59.81 s | event 600 (06:00) | event != 600 (08:00 → 800) | 23 | camera flies around a static object inside a textured sphere: a yellow bumpy torus (map10), a pink bumpy ball (map14) with six pink cones ("spikes", map14) sticking out of it, two of them twisted; grey/teal swirl background (map2) |
| spiky | 0x18c07 | 09:00–0b:00, ticks 4431–5199, 66.17–78.91 s | event 900 (09:00) | event >= 0xb00 (0b:00 → 2817) | 24 | camera circles a "flower": a sphere with 24 outward lobes and 24 dents (r = 240(1 + 0.36 sin 3φ sin 8θ), map4: purple/gold) inside a sphere of blue clouds (map8) |
| blobs | 0x18c16 | 11:00–15:00, ticks 7503–9038, 117.05–142.48 s | event 0x1100 (11:00) | event >= 0x1500 (15:00) | 23 | six metaballs (map10, gold/chrome) merging and splitting, inside a sphere textured with map5 (green/purple swirl with a white highlight); negative image during 14:00–14:10 |

Every part starts with a fade from white (0x17979 / 0x17999, notes-core 7.3) and the tube part first shows one
white page (0x17737).

---------------------------------------------------------------------------------------------------------------

## 2. The engine as these parts use it

### 2.1 The part loop

```c
// 0x15c8b (tube)                                   // 0x15d20 (spiky)
frame_start(); fill_page(0xffdf); flip();           [0x11303..0x1130b] = 31,31,31; build_fade_tables();  // 0x1131b: fade target white
[0x15c3f] = 0;                                      colour_tables(-40);                                // 0x1f5c5 -> 0x1a336(E=-40)
engine_setup(script 0x157fa, path 1);  // 0x15bb0   [0x15c3f] = 0;
interleave(slot 2 = [0x6552] = map2);  // 0x13eec   interleave(slot 8 = [0x656a] = map8);
[0x6542] = -xmpGetTimer();                          [0x6542] = 1000000 - xmpGetTimer();   // local time starts at 1e6 clocks
viewport 320 x 200 ([0x18d64], [0x18d68]), page offset [0x15c3b] = 0          (same)
fade_start(0x4ac51b);                  // 0.75 s     fade_start(0x4dd47a);            // 0.72 s
for (;;) {                                          for (;;) {
    frame_start();                     // 0x638b        frame_start();
    if (event != 600) return;                           if (event >= 0xb00) return;
    engine_frame();                    // 0x15bef       engine_frame();
    fade_apply();                      // 0x17999       fade_apply();
    flip();                            // 0x651f        flip();
}                                                   }
```
The spiky part does not call 0x15bb0: its scene (script 0x15973, path 3) is built by the logo part 0x1212a
(0x121bb..0x121cc, parts2d scope), which also sets the wobble flag [0x15c3f] = 1; 0x15d20 clears the flag again and
0x1212a never draws the scene, so the wobble 0x15c40 (roll += π sin 0.21T + π sin 0.62T + sin 0.14T) is never seen
in these parts.

`engine_setup(esi = script, eax = path)` (0x15bb0): [0x157a0] = path; 0x14567 resets the engine (buffers:
vertices [0x18d4c] 0x40 bytes each, faces [0x18d48] 0x1c bytes each, objects [0x18d50] 0x50 bytes each; counts
[0x18d58] objects, [0x18d5c] vertices, [0x18d60] faces = 0; script state tex = 1, mode = 3, us = vs = 1.0); the
script's first float × 1e-9 → [0x1579c] (time scale, stored as float), the second float → [0x154b4] (camera
distance D); the rest is interpreted by 0x14394.

`engine_frame()` (0x15bef):
```c
T = (float)(int)(xmpGetTimer() + [0x6542]) * [0x1579c];   // [0x159f7]
camera_path(T);                 // 0x159fb: eye [0x1549c], look point [0x154a8], roll [0x154b8]
if ([0x15c3f]) wobble();        // 0x15c40 (off here)
look_at();                      // 0x154bc: one matrix for every object
render(page + [0x15c3b]);       // 0x1f5d1 (engine)
```
The objects never move: every object gets the camera matrix (0x154bc copies it to all objects), so the scenes are
static and only the camera moves.

### 2.2 Object scripts (interpreter 0x14394, opcode table 0x143af)

A script is two floats (time scale × 1e-9, camera distance D) followed by byte opcodes. The scripts are static data
in the exe (read them from there): tube 0x157fa..0x15944, spiky 0x15973..0x159ca, blobs 0x159cb..0x159f6
(`models/parts3d-b/script.py` disassembles them). Formats (after the opcode byte; f = float, b = byte):

| op | handler | operands | effect |
|---|---|---|---|
| 0 | 0x1488d | b n, f cx cy cz, f R, f A1 F1 A2 F2 | new object: sphere (below) |
| 1 | 0x14501 | b t | texture slot for following faces (face+0xe) [0x1487d] |
| 2 | 0x1450f | b us, b vs | texture repeat factors [0x14885], [0x14889] (as floats) |
| 3 | | | end |
| 4 | 0x14b8a | b N, b M, f P0[3], f D[3], f r0, f r1 | new object: open cone/tube along D |
| 5 | 0x151e9 | b obj, f d[3] | translate object (unused here) |
| 6 | 0x15217 | b obj, f k | twist: rotate each vertex about z by k·y |
| 7 | 0x15276 | | merge the last two objects (unused here) |
| 8/9/10 | 0x15316 / 0x1535f / 0x153a5 | b obj, f a | rotate the object's vertices about x / y / z by a |
| 11 | 0x152a0 | b obj | new object = copy of obj; the copy's faces get the current texture |
| 12 | 0x1f5cb → 0x1a089 | | vertex normals for every object |
| 13 | 0x1455b | b m | face mode (face+0xd) and vertex flag m>>1 (vertex+0x3c) [0x14881] (unused here: mode stays 3) |
| 14 | 0x14e7c | b N, b M, f R, f r, f A1 F1 A2 F2, f T | new object: torus |
| 15 | 0x15174 | | drop every other pair of faces of the last object (unused here) |

Vertex record (0x40 bytes): +0 x, +4 y, +8 z (object space, float); +0xc..+0x1c written by the engine each frame
(+0x14 depth, +0x18 screen x 16.16, +0x1c screen y in pixels); +0x20..+0x28 normal (float, length 2^23); +0x2c u,
+0x30 v (texture coordinates, 16.16 texels as float); +0x34/+0x38 env-map u/v (engine); byte +0x3c = mode >> 1 = 1
(env-mapped), +0x3d/+0x3e clip flags (engine). Face record (0x1c bytes): +0/+4/+8 vertex pointers, +0xc flags (0x80 =
"wrapped UVs in the per-face buffer at +0x10"), +0xd mode (3 for every face here: textured + light map), +0xe texture
slot, +0xf light map slot (always 0), +0x14 and +0x18 written by the engine.

Generators (float32, angles accumulated by repeated addition as in the code):

```c
// op 0, sphere: N = 2n columns, n+1 rows -> 2n(n+1) vertices, 4n(n-1) faces
dphi = 2π/N;  dth = π/n;
for (j = 0, th = 0; j <= n; j++, th += dth)
  for (i = 0, phi = 0; i < N; i++, phi += dphi) {
    r = R * (1 + A1*sin(F1*phi) * A2*sin(F2*th));
    v.pos = (sin(th)*cos(phi)*r + cx,  cos(th)*r + cy,  sin(th)*sin(phi)*r + cz);
    v.u = phi * (256/2π) * 65536 * us;            // [0x15718] = 40.7437 = 256/2π, [0x1570c] = 65536
    v.v = th  * (256/π)  * 65536 * vs;            // [0x15714] = 81.4873 = 256/π
  }
for (j = 0; j < n; j++) for (i = 0; i < N; i++) {  // i1 = (i+1) % N
    if (j < n-1) face(j*N+i, (j+1)*N+i1, (j+1)*N+i);
    if (j > 0)   face(j*N+i, j*N+i1,     (j+1)*N+i1);
}
```
Rows 0 and n are N separate vertices at the same pole (each with its own u): the texture converges to a point at
the poles. A negative R (the background spheres) mirrors every vertex through the centre, which turns the faces
inside out (visible from inside, normals pointing inwards).

```c
// op 4, cone along D: N columns, M+1 rings -> N(M+1) vertices, 2NM faces, open at both ends
K = (3.767313, -2.19283, 0.528086);               // static floats at 0x15790
U = normalize(cross(D, K));  W = normalize(cross(U, D));   // 0x1542e(esi, ebx) = cross(ebx, esi); 0x15401 normalises
for (j = 0, t = 0; j <= M; j++, t += 1.0/M)
  for (i = 0, phi = 0; i < N; i++, phi += 2π/N) {
    r = (r1 - r0)*t + r0;
    v.pos = P0 + t*D + r*(cos(phi)*U + sin(phi)*W);
    v.u = phi * (256/2π) * 65536 * us;  v.v = t * 256 * 65536 * vs;
  }
for (j = 0; j < M; j++) for (i = 0; i < N; i++) {
    face(j*N+i, (j+1)*N+i1, j*N+i1);  face(j*N+i, (j+1)*N+i, (j+1)*N+i1);
}

// op 14, torus: N around the tube, M around the ring -> NM vertices, 2NM faces (closed both ways)
for (j = 0, th = 0; j < M; j++, th += 2π/M)
  for (i = 0, phi = 0; i < N; i++, phi += 2π/N) {
    a   = phi + th*T;
    rr  = r + A1*sin(F1*a) + A2*sin(F2*th);
    rho = rr*cos(a) + R;
    v.pos = (cos(th)*rho, sin(a)*rr, sin(th)*rho);
    v.u = phi * (256/2π) * 65536 * us;  v.v = th * (256/2π) * 65536 * vs;
  }
for (j = 0; j < M; j++) for (i = 0; i < N; i++) {   // nx = ((j+1) % M) * N
    face(j*N+i, j*N+i1, nx+i1);  face(j*N+i, nx+i1, nx+i);
}

// op 6 twist, ops 8/9/10 rotations (applied to positions only, before the normals exist)
twist(k):  a = k*y;  (x, y) = (x*cos a - y*sin a,  x*sin a + y*cos a);          // z unchanged
rotx(a):   (y, z) = (y*cos a - z*sin a,  y*sin a + z*cos a)
roty(a):   (x, z) = (x*cos a + z*sin a, -x*sin a + z*cos a)
rotz(a):   (x, y) = (x*cos a - y*sin a,  x*sin a + y*cos a)

// op 12 normals (0x1a089): n(v) = sum over the object's faces using v of cross(b - a, c - a), then scaled to 2^23
```
Texture seams: faces of the wrap-around column (i1 == 0; for the torus also the last ring, and every face for which
`i1 & next_row_start` is 0) go through 0x14651, which reduces the three UVs modulo 2^24 (256 texels) and, where two
vertices of the face differ by 128 texels or more, shifts the second by ±256 texels; such faces get flag 0x80 and
their own UV triple. The effect is a continuous texture across the seam: a remake gives the last column u = 256·us
instead of 0 (and v likewise for the torus ring).

### 2.3 Camera path (0x159fb / 0x15a59) and look-at (0x154bc)

Path k uses 9 floats at 0x15afc + 36k (static data, read from the exe): A, B, S2, dt, f1, f2, a1, a2, rollk.

| path | used by | A | B | S2 | dt | f1 | f2 | a1 | a2 | rollk |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | tube | 530 | 510 | 0.5 | -4 | 0.276 | 0.311 | 0.412 | 1.201 | 0.02 |
| 3 | spiky | 930 | 210 | 0.5 | 1 | 0.316 | 0.291 | 0.412 | 0.831 | 0.09 |
| 4 | blobs | 1430 | 1310 | 0.07 | -4 | 0.276 | 0.311 | 0.412 | 1.201 | 0.02 |

(Path 0 is the ribbon part's, 2 the tunnel's.)

```c
P(t, s):  a = f1*t + a1*sin(f1*t);   b = a2*sin(f2*t);
          return s * (A*cos(a)*cos(b),  B*sin(b),  A*sin(a)*cos(b));
eye  = P(T, 1);                  // [0x1549c]
look = P(T + dt, S2);            // [0x154a8] before 0x154bc
roll = (T + dt) * rollk;         // [0x154b8]

// 0x154bc
d = normalize(look - eye);
C = eye + D*d;                   // [0x154a8]: point at distance D (script's 2nd float) along the view
w = -D*d;                        // [0x15700] = eye - C
yaw   = π - atan2(w.x, w.z);                          c1 = cos(yaw),   s1 = sin(yaw)
pitch = 3π/2 - atan2(-sqrt(w.x² + w.z²), w.y);        c2 = cos(pitch), s2 = sin(pitch)
M = | c1       0    s1     |     // row 0: screen right (horizontal before the roll)
    | s1*s2    c2  -c1*s2  |     // row 1: screen up
    | -s1*c2   s2   c1*c2  |     // row 2: = d, the view direction
row0' = cos(roll)*row0 - sin(roll)*row1;   row1' = sin(roll)*row0 + cos(roll)*row1;
every object: matrix = M, translation = M*(-C), +0x40 = D
```
Projection (engine 0x19118 / 0x19022 / 0x18ea1, viewport 320x200): `c = M (p - C)`, `z = c.z + D`,
`x_screen = 160 + D c.x / z`, `y_screen = 100 - D c.y / z`. Since C = eye + D·d, this is a pinhole camera at `eye`
looking along d with a focal length of D pixels of the 320x200 frame (horizontal field of view 2·atan(160/D): tube
D = 160 → 90°, spiky D = 300 → 56°, blobs D = 230 → 70°), x right, y up, z forward (left-handed world), rolled by
`roll`. Pixels are square in this projection; the 320x200 frame is then shown at 4:3, so a remake scales y by 1.2
relative to x (focal length D·W/320 horizontally and D·H/200 vertically). Faces with a vertex at z <= 16 are dropped
whole (engine).

Time base: T = (xmpGetTimer + [0x6542]) × scale, per part:

| part | [0x6542] | scale (float) | T per second | T at the part start |
|---|---|---|---|---|
| tube | -timer0 | 1300e-9 = 1.3000000e-6 | 1.5511 | 0 |
| spiky | 1,000,000 - timer0 | 2012e-9 = 2.0120001e-6 | 2.4007 | 2.012 |
| blobs | -timer0 | 2200e-9 = 2.2e-6 | 2.6250 | 0 |

timer0 = the timer value read when the part set [0x6542] (just before its first frame). Captured values: tube
56,226,289, spiky 78,958,082, blobs 139,649,690 PIT clocks. Taking xmpGetTimer = (tick − 435) × 19,759 at the
start of a player tick (consistent with every flip in full.flips), the starting rows began at 56,174,837 (06:00),
78,937,205 (09:00) and 139,636,853 (11:00), so the parts start 43 ms, 17.5 ms and 10.8 ms after their row: the
frame-start lag. A remake can use the captured timer0 values or row start + that lag.

### 2.4 How a pixel is made (summary; the rasteriser is in notes-engine, the tables in notes-core 6.1)

Every face here has mode 3 (0x1e07c): texture slot `tex` (interleaved RGB 256x256, wrapping, nearest texel, UVs
interpolated affinely in screen space) plus a light value L from the 256x256 "spot" map in slot 0 ([0x654a] =
0x220000, built by 0x6592), looked up at the env coordinates

    eu = 128 + 128 * dot(M row 0, n/|n|),   ev = 128 + 128 * dot(M row 1, n/|n|)     (texels; vertex +0x34/+0x38)
    spot[y][x] = round(((255 - sqrt((x-127)² + (y-128)²))² / 256)² / 256)            (verified, all 65536 bytes)
    pixel = TR[R + L] + TG[G + L] + TB[B + L]                                         (tables 0x1a479/0x1b079/0x1bc79)

i.e. per channel `clamp((texel + L) * S/256 + E, 0, 255)` (then the inversion F, then 5:6:5), with L ≈ 252·(1 − r/255)^4,
r = 128·|(n·right, n·up)|: a highlight where the normal faces the viewer. The background spheres get the same
treatment (their inward normals give the bright spot in the middle of the background). Parameters per part:

| part | E (bias) | S (gain) | F | tables built by |
|---|---|---|---|---|
| tube | 0 | 0x100 | 0 | left from before (main / the flash) — verified at t3285 and in the dumps |
| spiky | -40 | 0x100 | 0 | 0x15d48 `0x1f5c5(-40)` once |
| blobs | -50 | 0x132 (1.195) | 0, or 0xff during event 0x1400 | 0x11085 every frame: [0x1a225] = 0x132, `0x1f5c5(-50)`, [0x1a225] = 0x100 |

Texture slots (dword pointers at [0x18d54] = 0x654a + 4·slot; contents verified in the snapshots):

| slot | pointer | contents at the time | used by |
|---|---|---|---|
| 0 | [0x654a] | spot light map (8-bit) | every face (light) |
| 1 | [0x654e] | map5, copied there and interleaved by the tunnel part 0x169e7 (0x169f3..0x16a0c) | blob background sphere |
| 2 | [0x6552] | map2, interleaved by the tube part (0x15cb6) | tube background sphere |
| 4 | [0x655a] | map4, interleaved by its texture script | spiky object |
| 8 | [0x656a] | map8, interleaved by the spiky part (0x15d5a) | spiky background sphere |
| 13 | [0x657e] | map10 (interleaved by its script) | tube torus, the blobs |
| 14 | [0x6582] | map14, second build (interleaved by its script) | tube ball and cones |

So map4, map10 and map14 are used (through the face texture byte, not by a direct pointer reference).

---------------------------------------------------------------------------------------------------------------

## 3. The tube object, 0x15c8b (06:00–08:00)

Script 0x157fa (time scale 1300, D = 160, path 1). Objects, in order (all static; "tex" = slot):

| obj | op | what | tex | us, vs | verts / faces |
|---|---|---|---|---|---|
| 0 | sphere n=10, c=0, R=-820, no bumps; then twist 0.0007, rotx 1.0, roty 0.3, rotz 0.7, twist 0.0007 | background (inside-out) | 2 (map2) | 3, 2 | 220 / 360 |
| 1 | cone N=10, M=10, P0=(0,-750,0), D=(0,660,0), r0=70, r1=30 | spike pointing down (-y), outer end open, inner end hidden in the ball | 14 (map14) | 1, 1 | 110 / 200 |
| 2–6 | 5 copies of 1, then: twist obj4 8e-5, obj5 9e-5, obj6 -2e-4; roty 0.1 for 2–6; rotz 2.1 (2), rotx 2.3 (3), rotz 1.57 (4), rotx -1.3 (5), rotz -1.57 (6); roty 1.57 for 3–6 | five more spikes; 4, 5, 6 bent by the twist | 14 | 1, 1 | 110 / 200 each |
| 7 | sphere n=9, c=0, R=100, A1=0.12, F1=3, A2=0.12, F2=4 | the ball at the centre: r = 100(1 + 0.0144 sin 3φ sin 4θ) | 14 | 1, 1 | 180 / 288 |
| 8 | torus N=15, M=24, R=300, r=100, A1=20, F1=2, A2=40, F2=3, T=0; then rotz 1.57, twist 0.002, rotz -0.57, rotx 0.74, roty 0.6 | the yellow bumpy ring, tube radius 100 + 20 sin 2φ + 40 sin 3θ, twisted | 13 (map10) | 2, 4 | 360 / 720 |
| | normals | | | | |

(The textures and repeat factors are the script state when the object was made: op 1 / op 2 before it; copies get
the current texture, 14.) The camera (path 1) stays 510–530 units from the centre: it flies between the spikes
(which reach 750) and around the ring (outer radius ≤ 460), inside the background sphere (radius 820). With D = 160
the field of view is 90°. When a spike passes close to the camera its faces with a vertex at z <= 16 disappear
whole (engine near rule).

Sync: none during the part (it only polls for the end, 08:00). Start: one white page (0x17737 fills the page with
[0x637c] = 0xffdf, flipped; ticks 3280–3283), then the fade from white over 0.75 s (rate 0x4ac51b). Fade tables
(0x1131b) are the white ones left by 0x17003.

---------------------------------------------------------------------------------------------------------------

## 4. The spiky object over clouds, 0x15d20 (09:00–0b:00)

Scene built by 0x1212a (logo part) from script 0x15973 (time scale 2012, D = 300, path 3):

| obj | op | what | tex | us, vs | verts / faces |
|---|---|---|---|---|---|
| 0 | sphere n=11, c=0, R=-1000, no bumps | background clouds (inside-out) | 8 (map8) | 5, 5 | 264 / 440 |
| 1 | sphere n=28, c=0, R=240, A1=0.59, F1=3, A2=0.61, F2=8 | the spiky flower: r = 240(1 + 0.3599 sin 3φ sin 8θ), 56 x 29 vertices | 4 (map4) | 3, 2 | 1624 / 3024 |
| | normals | | | | |

The bumps alternate because sin 3φ · sin 8θ changes sign: 6 x 8 cells (3 periods around, 8 half-periods pole to
pole), 24 lobes outward where the product is positive, 24 dents where it is negative. The camera (path 3) circles at 646–930 units
from the centre (never inside the object, radius ≤ 326) inside the cloud sphere of radius 1000 — at its widest it
passes within 70 units of the clouds, which is why their texture becomes large then. Local time starts at
1,000,000 clocks (T = 2.012 at the first frame).

Sync: none during the part (the 0a:00 event 2560 and 0a:10 → 900 have no effect here); ends at 0b:00. Start: fade
tables to white (targets 31,31,31 via 0x1131b), colour tables with E = -40, fade from white over 0.72 s (rate
0x4dd47a). No white page.

---------------------------------------------------------------------------------------------------------------

## 5. The blobs, 0x10fda (11:00–15:00)

### 5.1 Part loop

```c
engine_setup(script 0x159cb, path 4);       // obj 0: sphere n=12, R=-2000, tex 1 (map5), us = vs = 1; normals
faces0 = [0x18d60]; verts0 = [0x18d60];     // 0x10fe9..0x10ff9: [0x11146] = faces, [0x1114a] = faces too (bug:
                                            // meant [0x18d5c]); harmless, see below
viewport 320x200, page offset 0, [0x6542] = -xmpGetTimer();  fade_start(0x325aa0);   // 1.12 s from white
do {
    frame_start();                                           // 0x638b
    if (event == 0x1301) { fade_start(0x36ee80); event = 0x1300; }   // 13:00: fade from white again, 1.02 s
    [0x1a478] = (event == 0x1400) ? 0xff : 0;                 // 14:00..14:10: inversion
    [0x1a225] = 0x132; colour_tables(-50); [0x1a225] = 0x100; // 0x1f5c5, every frame
    if (event >= 0x1500) return;                              // 15:00
    tb = xmpGetTimer() + [0x6542];
    balls((tb * 710000) >> 32);                               // 0x7aae (mul 0xad570, high dword)
    [0x18d58] = 2; build_blob(1);                             // 0x769f: object 1, written at vertex [0x18d5c], face [0x18d60]
    [0x18d5c] = obj0.nverts + verts0;  [0x18d60] = obj0.nfaces + faces0;   // = 840 / 1056 from frame 2 on
    T = (float)(xmpGetTimer() + [0x6542]) * 2.2e-6;           // read again: ~12-14 ms after tb (the polygonisation)
    camera_path(T); look_at(); render(page);  fade_apply(); flip();
} while (no key);
```
The two counters only decide where in the buffers the blob is written (vertex 312 / face 528 in the first frame,
840 / 1056 afterwards, far from the buffer ends at 12000 vertices / 14000 faces); nothing visible.

### 5.2 Ball paths (0x7aae) — time base: the timer

```c
t = (uint32)(tb * 710000) >> 32;            // 197.24 steps per second (integer)
for (i = 0; i < 6; i++) {
    a = ((k1[i] * t * 3) >> 5) & 1023;      // 1024ths of a turn (sar; the & keeps bits 5..14 of 3·k·t)
    b = ((k2[i] * t * 3) >> 5) & 1023;
    c = ((k3[i] * t * 3) >> 5) & 1023;
    r = (SIN[c] >> 2) + 490;                // 490 + 256 sin c: 234..746
    X[i] = (SIN[a] * r) >> 10;              // r sin a
    Y[i] = (SIN[a + 256] * r) >> 11;        // r cos a / 2
    Z[i] = SIN[b] >> 1;                     // 512 sin b
}   // ints at 0x7918/0x7930/0x7948, floats at 0x7960/0x7978/0x7990
```
SIN = the 1280-entry table at 0xfb9e (round(1024 sin(2πi/1024)), 0x10fa2, core). k1, k2, k3 = int32[6] at 0x78a0,
0x78d0, 0x7900 (static data, read from the exe): k1 = 13, 7, -10, 5, -8, 8; k2 = 15, -11, -10, 7, -5, -6;
k3 = 4, -9, 11, 10, 7, 5. Continuous form: with t = tb·710000/2^32, the angle a_i = 2π·(3 k1_i t/32)/1024 rad =
0.11346·k1_i rad/s·(tb in s), likewise b, c; X = r sin a, Y = (r/2) cos a, Z = 512 sin b, r = 490 + 256 sin c.
At t = 0 all six balls are at (0, 245, 0) (one big sphere in the first frame, under the white fade).

### 5.3 Field (0x7a4e) and threshold

```c
int field(x, y, z) {                        // grid point, integer coordinates (multiples of 64)
    s = 0;
    for (i = 5; i >= 0; i--)
        s += 21 * g(SQ[X[i] - x] + SQ[Y[i] - y] + SQ[Z[i] - z]);    // SQ[k] = k², table 0x7b9e (0x6cc0), k = -4096..4095
    s >>= 7;  return s > 255 ? 255 : s;     // byte; "inside" = bit 7 set (>= 128)
}
int g(d2) {                                 // 0x662f: ~ 65536 / sqrt(d2) from RSQ[0..255]
    // RSQ[0] = 4096 (dword at 0x6716), RSQ[i] = round(4096 / sqrt(i)) (0x6b16, table 0x671a)
    // d2 is brought into [0x40, 0x100) by shifting 2 bits at a time, the result scaled by 2 per step:
    d2 >= 0x400000: RSQ[d2 >> 16] >> 4      0x100000: RSQ[d2 >> 14] >> 3    0x40000: RSQ[d2 >> 12] >> 2
    0x10000: RSQ[d2 >> 10] >> 1             0x4000: RSQ[d2 >> 8]             0x1000: RSQ[d2 >> 6] << 1
    0x400: RSQ[d2 >> 4] << 2                0x100: RSQ[d2 >> 2] << 3         0x40: RSQ[d2] << 4
    0x10: RSQ[d2 << 2] << 5                 4: RSQ[d2 << 4] << 6             else RSQ[d2 << 6] << 7
}
```
So field ≈ 21·65536/128 · Σ 1/d_i = 10752 Σ 1/d_i (truncated, clamped to 255), and the surface is
**Σ_i 1/|p − c_i| = 1/84**: a single ball has radius 84, six coincident balls 504. The table quantises 1/d to
8 bits of mantissa (steps up to ~0.8 %).

### 5.4 Polygonisation (0x769f → [0x6b6d] = 0x77ad → 0x77d5 / 0x6e74)

Grid: points at -1024 + 64·i, i = 0..31 per axis; cell cache [0x10fd6], 32³ records of 16 bytes at
((x+1024)/64·1024 + (y+1024)/64·32 + (z+1024)/64)·16, set to 0xff at the start of every frame: byte +0 field
value, byte +3 visited (0 = visited), dwords +4 / +8 / +0xc the vertex made on the x / y / z edge that starts at
this point (-1 = none). Cells (origin x, y, z) are polygonised for origins in [-1024, 896] per axis.

```c
for (i = 5; i >= 0; i--) {                       // 0x77ad: [0x6b71] = normal 0x79a8, [0x6b75] = field 0x7a4e
    (x, y, z) = (X[i] & ~63, Y[i] & ~63, Z[i] & ~63);
    if (any coordinate <= -1024 or >= 896) continue;
    for (x1 = x + 64; x1 < 896; x1 += 64)        // march +x until the field is outside
        if (!(field(x1, y, z) & 0x80)) { cell(x1 - 64, y, z); goto next; }
    for (x1 = x - 64; x1 > -1024; x1 -= 64)      // reached the edge: march -x
        if (!(field(x1, y, z) & 0x80)) { cell(x1, y, z); goto next; }
  next:;
}
void cell(x, y, z) {                             // 0x6e74, recursive
    compute the 8 corner fields (cached); visited = 1;
    case = Σ (field(corner k) >= 128) << k,  corner k = (x + 64(k&1), y + 64(k>>1&1), z + 64(k>>2&1));
    for each triangle (e0, e1, e2) of TRI[case]: face(vertex(e2), vertex(e1), vertex(e0));   // reversed order
    // then recurse over the cell faces that the surface crosses, in this order, if the neighbour is inside the
    // box and not yet visited:  z+1 (case & 0xf0 mixed), z-1 (0x0f), y-1 (0x33), y+1 (0xcc), x-1 (0x55), x+1 (0xaa)
}
vertex(e): edge e from corner A to corner B (fa = field(A), fb = field(B), one of them >= 128);
    cached per edge; else position = A + ((128 - fa) * 64 / (fb - fa)) along the edge axis   // C division, truncated
    (an integer, stored as float), normal and UV from 0x79a8, appended at [0x6b48] (+0x40)
```
The seed call does not check "visited": when a ball's seed cell lies on a surface already traced from another ball,
its triangles are emitted a second time (duplicate faces, invisible). Every face: flags 0, mode 3, texture slot
[0x6b6c] = 13 (map10, static byte), light slot 0.

TRI: the triangle table, 256 records of 8 bytes at 0x28ed8 (static, read from the exe), turned into a code table at
[0x10fd2] by 0x6cdd (core): low 3 bits of byte 0 = number of triangles (0..4), then 3 nibbles per triangle (high
nibble of byte 0, then low/high nibbles of bytes 1, 2, ...) = edge numbers:

| edge | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| from → to (corner offsets xyz) | 000→100 | 000→010 | 100→110 | 010→110 | 000→001 | 100→101 | 010→011 | 110→111 | 001→101 | 001→011 | 101→111 | 011→111 |

(Edge routines 0x7144.. via the table 0x6e0c.) Over the whole part the mesh has 754–1240 vertices and 1496–2489
faces, and its extent stays within -923..877 (x), -593..752 (y), -826..800 (z), so the surface never reaches the
box edges (sampled every 0.05 s, `blob_extent.py`).

### 5.5 Normals and texture coordinates (0x79a8)

```c
a = Σ_i (p - c_i) / |p - c_i|³;                  // c_i = float ball centres (0x7960..); = −∇ Σ 1/|p − c_i|
n = a * (2^23 / |a|);                            // +0x20..+0x28
u = n.x;  v = n.y;                               // +0x2c / +0x30: 16.16 texels → u = 128 n̂.x, v = 128 n̂.y (mod 256)
flag +0x3c = 1                                   // env-mapped like everything else
```
The normal is the exact gradient direction of Σ 1/d, i.e. of the field the surface is cut from. The texture is
attached by the world-space normal (the blob object has no transform of its own): a gold "reflection" fixed to the
world, plus the spot highlight fixed to the camera.

### 5.6 Sync reactions

| music | event (as the part sees it) | reaction |
|---|---|---|
| 11:00 | 0x1100 | part start, fade from white 1.12 s (rate 3,300,000) |
| 13:00 | 0x1301 → set to 0x1300 | fade from white 1.02 s (rate 3,600,000) |
| 14:00 | 0x1400 | F = 0xff in the colour tables: each channel becomes ((v + 255·(255 − v)) >> 8) ≈ 255 − v, a **negative** image (blue blobs on a pale background), not white (ticks 8656–8750) |
| 14:10 | 0x1300 | F = 0 again: normal colours |
| 15:00 | 0x1500 | part ends; main starts the end picture |

All reactions happen at the first frame start after the row (frame lag ≤ 2 frames at the capture's 23 fps).

---------------------------------------------------------------------------------------------------------------

## 6. Static data used (read from JIZZ.EXE at run time)

| what | address | size / format |
|---|---|---|
| tube script | 0x157fa | 0x14b bytes, format 2.2 |
| spiky script | 0x15973 | 0x58 bytes |
| blob scene script | 0x159cb | 0x2c bytes |
| camera paths | 0x15afc | 5 x 9 floats (A, B, S2, dt, f1, f2, a1, a2, rollk) |
| cone reference vector K | 0x15790 | 3 floats |
| generator constants | 0x15718 (256/2π), 0x15714 (256/π), 0x1570c (65536), 0x15720 (256), 0x15710 (3π/2) | floats (code constants) |
| ball speeds k1, k2, k3 | 0x78a0, 0x78d0, 0x7900 | 6 int32 each |
| marching cubes triangles | 0x28ed8 | 256 x 8 bytes (5.4) |
| blob texture slot | 0x6b6c | byte 13 |
| RSQ[0] | 0x6716 | int32 4096 (the rest of the table is computed) |
| wobble constants (unused here) | 0x15c7f / 0x15c83 / 0x15c87 | 0.21, 0.62, 0.14 |

Computed at start-up (core): SIN 0xfb9e, SQ 0x7b9e, RSQ 0x671a, the spot map (slot 0), the case table [0x10fd2].

---------------------------------------------------------------------------------------------------------------

## 7. Time bases

| variable | base |
|---|---|
| camera T (all parts) | timer: (xmpGetTimer + [0x6542]) × scale, table in 2.3 |
| ball positions | timer: t = ((xmpGetTimer − timer0) × 710000) >> 32, read ~12–17 ms (14,000–17,000 clocks at the capture's speed) before the camera's timer value of the same frame |
| fades | timer (notes-core 7.3) |
| inversion, 13:00 fade, part ends | sync event, checked once per frame |
| anything per frame | nothing (the blob mesh is rebuilt from scratch every frame; the scenes are static) |

---------------------------------------------------------------------------------------------------------------

## 8. Shortcuts a high-resolution remake should evaluate exactly

- **Low-poly generated objects** (spheres n = 9..28, cones 10x10, torus 15x24, background spheres n = 10..12):
  the generators are parametric (2.2), so generate them with many more segments (same formulas, same twist and
  rotations, same UV scales) and analytic or averaged normals. Keep the pole pinch of the sphere mapping (the
  texture really converges there; e.g. the swirl centre at the top left of the blob background) but not the
  asymmetric per-column pole normals.
- **Affine, nearest-texel texture mapping, 8-bit wrap** (engine): perspective-correct, filtered, wrapping.
- **Per-vertex env coordinates interpolated across faces**: evaluate the spot light per pixel from the interpolated
  normal: L = 255^4/2^24 · (1 − r/255)^4 with r = 128·|(n̂·right, n̂·up)| (the map's 1-texel centre offset,
  x − 127 vs y − 128, is a table artefact).
- **Colour tables, 5:6:5 and the 32-step fades**: per channel clamp((texel + L)·S/256 + E, 0, 255), the inversion
  mix, full precision; fades continuous (notes-core).
- **Faces dropped whole at z <= 16**: clip at a near plane instead (visible when the tube camera grazes a spike).
- **Blobs**: integer ball times/angles/positions → continuous formulas (5.2); the 64-unit marching-cubes grid with
  byte-quantised field values, table-based 1/d and truncated edge interpolation → evaluate Σ 1/|p − c_i| = 1/84
  exactly (fine grid, or ray marching), normal = normalize(Σ (p − c_i)/|p − c_i|³) per pixel, texture coordinates
  128·n̂.x, 128·n̂.y per pixel. Use one time for balls and camera (the original reads the timer twice).
- The pages are never cleared: the background sphere always covers the viewport (checked every 0.25 s for all
  three parts, `coverage.py`), so nothing depends on stale pages.

## 9. Models (`models/parts3d-b/`, see its README)

| script | checks | result |
|---|---|---|
| script.py | disassembles the object scripts | — |
| objects.py SCRIPT TICK | rebuilds the scripts' objects, compares with the snapshot buffers | all objects of all three scripts: faces identical, UVs exact, positions ≤ 2.4e-4, normals ≤ 3.4e-6 (relative) |
| camera.py TICK | camera path + look-at vs snapshot | matrix ≤ 4.8e-7, translation ≤ 6e-4 |
| project.py DUMP | projection, depth, env coordinates vs the engine's vertex results | ≤ 0.02 px, env exact |
| blobs.py DUMP | balls, field, marching cubes, normals vs the engine's blob mesh | identical vertex/face lists in 6 dumps (ticks 7523–8500) |
| tables.py DUMP E S F | colour tables 0x1a336 | identical for all three parts and the inversion |
| render.py DUMP OUT [--model] | approximate rasteriser vs the page the engine drew | mean 4.5–7.4/255; same result from engine data and from the models |
| blob_extent.py, coverage.py | blob box edges, background coverage | never reached / always covered |
| run_jizz_b.py | copy of run_jizz.py with `--dump-at ADDR:T1,..` (memory + drawn page after a given instruction) | dumps at 0x15c3a (after the render, tube/spiky) and 0x11130 (blobs) |

## 10. Not determined / guesses

- The exact row start times in xmpGetTimer units (2.3) rest on xmpGetTimer = (tick − 435)·19,759 at a player
  tick, inferred from full.flips; notes-music has the authoritative relation.
- Texture contents and the rasteriser's exact span rules belong to notes-textures / notes-engine; the frame
  comparison here only shows that the parameters given (objects, camera, slots, light map, tables) are the ones
  the engine used.
