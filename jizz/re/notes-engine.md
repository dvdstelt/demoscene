# Scope "engine": Nix's vector engine (objects, camera, transform, sorting, texture + environment filler)

All addresses are offsets in `jizz_img.bin` (= near = linear addresses in the emulator). "t" is the engine's scene
time (section 4.1). Floats are IEEE single; the x87 runs with 24-bit precision (main 0x18712, and 0x1f5d1 sets it
again every frame), so every fadd/fsub/fmul/fdiv/fsqrt result is a float32; fsin/fcos/fpatan/fldpi values are
extended and get rounded by the operation that uses them. `fist` rounds to nearest even. "16.16" = fixed point with
65536 = 1.0. Screen coordinates refer to the original 320x200 page (or the inset window, section 5.2).

What was checked against the emulator (scripts in `jizz/re/models/engine/`, section 10):
- **Objects**: a Python model of the script interpreter and all generators rebuilds every object of the five scenes
  bit-exactly (positions, normals, texture coordinates, faces, per-face seam tables: 0 ulps difference, 7,232
  vertices, 11,888 faces) against the snapshots t1365, t3285, t2515, t4050, t7506.
- **Camera**: the model of 0x159fb + 0x15c40 + 0x154bc reproduces the matrices the renderer received in 23 dumped
  frames of all five scenes to 0..2 ulps (the differences come from extended-precision fsin/fpatan).
- **Renderer**: a Python model of 0x1f5d1 (transform, face list, radix sort, triangle filler with both span
  loops) reproduces the page **bit-exactly (0 differing pixels)** in 23 frames: tube (7), tunnel (3), spiky object
  (3), metaballs (3), ribbons full screen (3) and in both inset windows (175x110 and 160x100, 4). In 15 of them
  the page is also byte-identical to the `full.frames` record shown on screen (the other 8 are later overdrawn or
  faded by their part).
- Shading tables and the environment map: regenerated from the formulas below, 0 differences.

-----------------------------------------------------------------------------------------------------------------

## 1. What the engine draws, and who uses it

The engine draws texture-mapped, environment-mapped ("phong"-highlighted) triangle meshes built procedurally from a
small byte script per scene, seen from a camera that flies along a closed sine-based path. Painter's algorithm
(radix sort of face depth), scanline filler with affine texture coordinates, one 256x256 RGB texture plus one
256x256 8-bit highlight map per face, 16-bit pixel from three lookup tables. No z-buffer, no lighting beyond the
highlight map, no near clipping (faces touching the near limit are dropped).

| scene (eax of 0x15bb0) | script | built by (part) | drawn by | objects |
|---|---|---|---|---|
| 0 | 0x157a4 | 0x17003 (0x17014) | 0x17003 via 0x15bef (0x172b1) | dome sphere R = -4000 (texture 13, hidden at first: object flags 0x43, made visible at 0x170b6) + "ribbons": torus with every other quad removed (texture 14) |
| 1 | 0x157fa | 0x15c8b (0x15cab) | 0x15c8b via 0x15bef (0x15d04) | dome R = -820 (texture 2) twisted and rotated + 6 cones (texture 14) + bumpy sphere R = 100 (texture 14) + bumpy torus (texture 13) |
| 2 | 0x15945 | 0x169e7 (0x16a1b) | 0x169e7 via 0x15bef (0x16b94, 0x16c17, 0x16c41) | the inside of a torus R = -2500, r = 600 (the "tunnel", texture 1) |
| 3 | 0x15973 | 0x1212a (0x121cc) | 0x15d20 via 0x15bef (0x15daa) | dome R = -1000 (texture 8) + spiky sphere R = 240 (texture 4) |
| 4 | 0x159cb | 0x10fda (0x10fe4) | 0x10fda itself: 0x159fb, 0x154bc, 0x1f5d1 (0x1111b..0x1112b) | dome R = -2000 (texture 1); the part adds the metaball mesh as object 1 itself (parts3d-b) |

Every object is static: only the camera moves (verified: vertex data unchanged between the part snapshots and the
dumped frames hundreds of ticks later), except the metaball mesh that its part rewrites.

## 2. Data structures and buffers

### 2.1 Buffers (0x14567, called by 0x15bb0; base [0x145dc] = 0x6db000 set by main at 0x188ad)

| pointer | offset from base | size | contents |
|---|---|---|---|
| [0x18d4c] | 0 | 0xbb800 | vertices, 0x40 bytes each (12,000) |
| [0x18d48] | 0xbb800 | 0x5fb40 | faces, 0x1c bytes each (14,000) |
| [0x18d50] | 0x11b340 | 0xfa0 | objects, 0x50 bytes each (50) |
| [0x18d40] | 0x11c2e0 | 0x1b580 | face list {int key, face *} (14,000), the sorted result (pointers only) |
| [0x18d44] | 0x137860 | 0x1b580 | radix sort temporary |
| [0x1480c] | 0x152de0 | 0xc000 | per-face (u, v) tables for faces across a texture seam, 0x30 bytes per slot (only 24 used) |

0x14567 also zeroes the object, vertex and face counts ([0x18d58], [0x18d5c], [0x18d60]) and sets the script state:
face type [0x14881] = 3, texture [0x1487d] = 1, u / v repeat [0x14885] = [0x14889] = 1.0. Each 0x15bb0 call
replaces the whole previous scene.

### 2.2 Object record (0x50 bytes)

| offset | type | meaning |
|---|---|---|
| +0x00 / +0x04 | int | first face index, face count |
| +0x08 / +0x0c | int | first vertex index, vertex count |
| +0x10..+0x30 | float[9] | camera rotation rows: right, up, forward (written every frame by 0x154bc, the same for all objects) |
| +0x34..+0x3c | float[3] | translation (camera space, relative to the point F in front of the eye) |
| +0x40 | float | F: focal length in pixels of a 320-wide screen, also added to the depth (section 4.4) |
| +0x44 / +0x48 | code * / int | callback and its eax, called before the transform when flag 0x80 is set (never set) |
| +0x4c | int | flags: 0x40 skip the object; 0x80 call +0x44; 2 do not transform; 1 transform but add no faces. Only 0x43 (hidden) / 0 are used (ribbons part) |

### 2.3 Vertex record (0x40 bytes)

| offset | type | meaning |
|---|---|---|
| +0x00..+0x08 | float[3] | position, object = world space (no per-object transform) |
| +0x0c / +0x10 | float | camera x' (scaled by 65536*W/320) / y' (scaled by -H/200), written by the transform |
| +0x14 | float | z' = depth from the eye along the view axis |
| +0x18 | float | screen x in 16.16 units (pixels * 65536), stored as a float |
| +0x1c | float | screen y in pixels |
| +0x20..+0x28 | float[3] | vertex normal, length 2^23 = 8388608 |
| +0x2c / +0x30 | float | texture u / v in texels * 65536 (8.16 in effect; may exceed 2^24, the texture wraps) |
| +0x34 / +0x38 | float | environment u / v in texels * 65536 (written by the transform when +0x3c bit 0 is set) |
| +0x3c | byte | bit 0: environment-map this vertex (= face type >> 1 when generated, so 1 for type 3) |
| +0x3d | byte | 1 = on screen, 0 = in front but off screen, 4 = at or behind the near limit |
| +0x3e | byte | outcode: 1 left (x < 0), 2 right (x >= W), 4 above (y < 0), 8 below (y >= H) |

### 2.4 Face record (0x1c bytes)

| offset | type | meaning |
|---|---|---|
| +0x00..+0x08 | vertex *[3] | absolute addresses of the corners A, B, C |
| +0x0c | byte | 0x80: own (u, v) table at +0x10 (texture seam, section 3.4); 0x40: no back-face culling (never set) |
| +0x0d | byte | type: draw table 0x1926e[type] (256 dwords): 1, 2, 3 -> filler 0x1e07c; 0 and 255 -> 0x1f4f8 (the filler's scratch vertex, data: would crash); all others -> `ret`. Bit 0x80 makes 0x19f78 treat the face as a single point with key 3*z(A) (dead code: no drawer). All generated faces are type 3 (no script uses opcode 13) |
| +0x0e | byte | RGB texture index into the pointer table [0x18d54] = 0x654a (1..17: the 17 texture buffers of 0x30000 bytes) |
| +0x0f | byte | environment map index into the same table: always 0 -> [0x654a] = 0x220000 (section 6.5) |
| +0x10 | float * | 6 floats uA vA uB vB uC vC (when +0x0c & 0x80) |
| +0x14 | float | twice the signed screen area, written every frame by 0x19f78 |
| +0x18 / +0x19 | byte | OR of the corner outcodes if the face needs clipping (0 = fully on screen) / number of off-screen corners |

### 2.5 Engine globals

[0x18d54] texture pointer table (0x654a, main 0x18965); [0x18d58] / [0x18d5c] / [0x18d60] object / vertex / face
counts; [0x18d64] W, [0x18d68] H (320 x 200, parts change them for insets), [0x18d6c] pitch 640; [0x18d70..78]
R/G/B field sizes and [0x18d7c..84] positions (from the VBE mode, 5/6/5 at 11/5/0); [0x18d88] clear mode (main
sets 0xff = none, nothing changes it), [0x18d8c] clear colour / source; [0x18d90] face list bytes, [0x18d94]
entries; [0x18d98] target (0 = transform only), [0x18da4] window start, [0x18da0] window end = start + H*pitch.
Camera: [0x157a0] scene, [0x1579c] time scale, [0x154b4] F, [0x159f7] t, [0x1549c] eye, [0x154a8] target,
[0x154b8] roll, [0x15c3f] roll wobble on/off, [0x15c3b] byte offset of the window in the page.

## 3. Building a scene: 0x15bb0 and the script interpreter 0x14394

```c
void build_scene(int scene /*eax*/, const u8 *s /*esi*/)        // 0x15bb0
{
    cam_scene = scene;                                // [0x157a0]: row of the camera table 0x15afc
    init_engine();                                    // 0x14567 (2.1)
    time_scale = ((float *)s)[0];                     // [0x1579c]
    F          = ((float *)s)[1];                     // [0x154b4]
    run_script(s + 8);                                // 0x14394
    time_scale = (float)(time_scale * 1e-9);          // double constant at 0x15be7
}
```

### 3.1 Script format (0x14394: jump table 0x143af, 16 entries; opcode > 15 or 3 ends the script)

All multi-byte operands are little-endian; "f" = float32.

| op | handler | operands (after the opcode byte) | action |
|---|---|---|---|
| 0 | 0x143ef -> 0x1488d | u8 M, f cx, cy, cz, f R, f A1, F1, A2, F2 (33 bytes) | sphere, M rings, S = 2M segments (3.2) |
| 1 | 0x14501 | u8 index | current texture [0x1487d] |
| 2 | 0x1450f | u8 us, u8 vs | texture repeat [0x14885] = us, [0x14889] = vs (as floats) |
| 3 | 0x14566 | | end |
| 4 | 0x14421 -> 0x14b8a | u8 N, u8 M, f C[3], f A[3], f R0, f R1 (34 bytes) | cylinder / cone (3.2) |
| 5 | 0x1448e -> 0x151e9 | u8 obj, f d[3] | translate object |
| 6 | 0x144a6 -> 0x15217 | u8 obj, f k | per-vertex rotation about z by k*y ("twist") |
| 7 | 0x144b7 -> 0x15276 | | merge the last object into the one before (counts added, object count - 1) |
| 8 / 9 / 10 | 0x144ce / 0x144df / 0x144f0 -> 0x15316 / 0x1535f / 0x153a5 | u8 obj, f angle | rotate object about x / y / z |
| 11 | 0x144c2 -> 0x152a0 | u8 obj | duplicate object (new texture index = current) |
| 12 | 0x14540 -> 0x1f5cb -> 0x1a089 | | vertex normals for every object (3.3) |
| 13 | 0x1455b | u8 type | face type [0x14881] (vertex env flag = type >> 1) |
| 14 | 0x1445b -> 0x14e7c | u8 N, u8 M, f R, r, A1, F1, A2, F2, twist (30 bytes) | torus (3.2) |
| 15 | 0x14483 -> 0x15174 | | keep every other quad of the last object (3.2) |

The five scripts (decoded by `scripts.py`; they are data: read them from JIZZ.EXE at run time):

| scene | address, size | header (time scale, F) | ops |
|---|---|---|---|
| 0 | 0x157a4, 86 | 900, 350 | tex 13, uv 2,2, sphere M=12 R=-4000 (A=0); tex 14, uv 2,4, torus N=20 M=90 R=-2300 r=500 A1=50 F1=5 A2=60 F2=3 twist=3; halve; normals |
| 1 | 0x157fa, 331 | 1300, 160 | tex 2, uv 3,2, sphere M=10 R=-820; twist(0, 0.0007), rot_x(0, 1.0), rot_y(0, 0.3), rot_z(0, 0.7), twist(0, 0.0007); tex 14, uv 1,1, cylinder N=10 M=10 C=(0,-750,0) A=(0,660,0) R0=70 R1=30; duplicate obj 1 five times (objs 2..6); twist 4/5/6 by 8e-5/9e-5/-2e-4; rot_y 2..6 by 0.1; rot_z(2, 2.1), rot_x(3, 2.3), rot_z(4, 1.57), rot_x(5, -1.3), rot_z(6, -1.57); rot_y 3..6 by 1.57; sphere M=9 R=100 A1=0.12 F1=3 A2=0.12 F2=4 (obj 7, still tex 14); tex 13, uv 2,4, torus N=15 M=24 R=300 r=100 A1=20 F1=2 A2=40 F2=3 twist 0 (obj 8); rot_z(8, 1.57), twist(8, 0.002), rot_z(8, -0.57), rot_x(8, 0.74), rot_y(8, 0.6); normals |
| 2 | 0x15945, 46 | 1900, 400 | tex 1, uv 2,2, torus N=30 M=50 R=-2500 r=600 A1=0 F1=6 A2=120 F2=4 twist 0; normals |
| 3 | 0x15973, 88 | 2012, 300 | uv 5,5, tex 8, sphere M=11 R=-1000; uv 3,2, tex 4, sphere M=28 R=240 A1=0.59 F1=3 A2=0.61 F2=8; normals |
| 4 | 0x159cb, 44 | 2200, 230 | sphere M=12 R=-2000 (texture 1 by default); normals |

(Exact float values: `scripts.py` prints them rounded; e.g. 0.0007 is the float nearest to it.) A negative radius
turns the surface inside out (the sphere is mirrored through its centre; the torus becomes the one at angle
th + pi with the cross-section mirrored): the winding and the computed normals then face inwards, so the spheres
with R < 0 are "domes" seen from inside, the scene-2 torus is a tunnel, and the scene-0 ribbons show their inner
side.

### 3.2 Generators

Common to all: vertices are appended to the vertex array, faces to the face array, through 0x145e0 (new object:
first indices = the current counts, flags 0, counts added). Angles are accumulated by float32 addition of the step
(not i*step). Texture coordinates use the constants 128/pi (0x15718), 256/pi (0x15714), 65536 (0x1570c), 256
(0x15720). Each vertex: flag +0x3c = type >> 1, normal = 0. Each face: 0x14810 sets +0x0c = 0, +0x0d = type,
+0x0e = texture, +0x0f = 0; then the seam fix 0x14651 (3.4) where noted.

**Sphere 0x1488d** (op 0). S = 2M, (M+1)*S vertices, 2*(M-1)*S faces.
```c
dphi = (2*pi)/S;  dth = pi/M;
for (j = 0, th = 0; j <= M; j++, th += dth)
  for (i = 0, ph = 0; i < S; i++, ph += dphi) {
    r = R * (1 + (A1*sin(F1*ph)) * (A2*sin(F2*th)));
    P = (sin(th)*cos(ph)*r + cx,  cos(th)*r + cy,  sin(th)*sin(ph)*r + cz);   // y = polar axis
    u = ph * (128/pi) * 65536 * us;      // one texture width per turn * us
    v = th * (256/pi) * 65536 * vs;      // one texture height pole to pole * vs
  }
for (rb = 0; rb < S*M; rb += S) { nb = rb + S;              // vertex index = row * S + i
  for (i = 0; i < S; i++) { i1 = (i+1) % S;
    if (nb < S*M) face(rb+i, nb+i1, nb+i);                 // not for the last ring (pole)
    if (rb != 0)  face(rb+i, rb+i1, nb+i1);                // not for the first ring (pole)
    // both followed by the seam fix when i1 == 0
  } }
```
The S vertices of a pole ring coincide; their u differ, so the texture pinches into a star at the poles (visible on
the domes, e.g. the metaball background).

**Cylinder / cone 0x14b8a** (op 4). (M+1)*N vertices, 2*M*N faces.
```c
U = normalize(cross(A, K));  V = normalize(cross(U, A));   // K = float[3] at 0x15790 = (3.767313, -2.19283, 0.528086)
dphi = (2*pi)/N;  ds = 1.0/M;                               // cross(a,b) = (a1 b2 - a2 b1, a2 b0 - a0 b2, a0 b1 - a1 b0)
for (j = 0, s = 0; j <= M; j++, s += ds) { r = (R1 - R0)*s + R0;
  for (i = 0, ph = 0; i < N; i++, ph += dphi) {
    P[k] = (C[k] + s*A[k]) + (cos(ph)*U[k] + sin(ph)*V[k]) * r;
    u = ph * (128/pi) * 65536 * us;  v = s * 256 * 65536 * vs;
  } }
for each row rb = 0, N, .., N*(M-1) (nb = rb + N), i (i1 = (i+1) % N):
    face(rb+i, nb+i1, rb+i1);  face(rb+i, nb+i, nb+i1);     // seam fix when i1 == 0
```
normalize(v) (0x15401) = v * (1/sqrt(x*x + (y*y + z*z))).

**Torus 0x14e7c** (op 14). N*M vertices, 2*N*M faces, closed in both directions.
```c
dphi = (2*pi)/N;  dth = (2*pi)/M;                 // 2*pi rounded to float first
for (j = 0, th = 0; j < M; j++, th += dth)
  for (i = 0, ph = 0; i < N; i++, ph += dphi) {
    a   = ph + th*twist;                          // the tube cross-section turns twist times per revolution
    rr  = (r + A1*sin(F1*a)) + A2*sin(F2*th);
    rad = cos(a)*rr + R;
    P = (cos(th)*rad,  sin(a)*rr,  sin(th)*rad);   // y = up, ring in the x-z plane
    u = ph * (128/pi) * 65536 * us;  v = th * (128/pi) * 65536 * vs;
  }
for (j = 0; j < M; j++) { rb = j*N; nb = (rb + N) % (N*M);
  for (i = 0; i < N; i++) { i1 = (i+1) % N;
    face(rb+i, rb+i1, nb+i1);  face(rb+i, nb+i1, nb+i);   // seam fix when (i1 & nb) == 0 (a cheap
  } }                                                      // "i1 == 0 || nb == 0"; extra calls change nothing)
```

**Halve 0x15174** (op 15): face count of the last object /= 2; keeps faces 0,1, 4,5, 8,9, ... (every other quad in
creation order). With the scene-0 torus (N = 20 quads around the tube) this leaves 10 strips of one quad width
that spiral around the ring (twist 3): the "ribbons".

**Translate 0x151e9** (op 5): P += d. **Rotations** (ops 8/9/10, matrices built once; 0x1545d applies
`p'[r] = p[0]*m[r][0] + (p[1]*m[r][1] + p[2]*m[r][2])`, positions only, normals are computed afterwards):
x: [[1,0,0],[0,c,-s],[0,s,c]]; y: [[c,0,s],[0,1,0],[-s,0,c]]; z: [[c,-s,0],[s,c,0],[0,0,1]], c/s = cos/sin of
the angle (float32). **Twist 0x15217** (op 6): per vertex the z matrix with angle y*k (y before the rotation).
**Duplicate 0x152a0** (op 11): copies the vertex records verbatim and the face records with the corner pointers
moved to the copy; texture byte = current texture, everything else (including seam flag and its table pointer)
copied. **Merge 0x15276** (op 7): unused by the scripts.

### 3.3 Vertex normals 0x1a089 (op 12, per object; starts with `fninit`, so it runs in 64-bit precision)

```c
for each face (A, B, C):  d1 = B - A;  d2 = C - A;  n = cross(d1, d2);   // area-weighted, not normalized
                          A.n += n;  B.n += n;  C.n += n;               // the generators left n = 0
for each vertex:          n *= 8388608 / sqrt(nx*nx + ny*ny + nz*nz);  // length 2^23
```
Shared vertices (all of them: seams and poles reuse vertex indices) give smooth normals everywhere. After this the
FPU stays in 64-bit precision until the next 0x1f5d1.

### 3.4 Texture seam fix 0x14651

Called for faces that may straddle the u wrap. Takes the three corners' (u, v), reduces each mod 2^24 (fprem,
keeping the sign: 2^24 = 256 texels), then for the edges (A,B), (B,C), (C,A) of u and then of v: if
`|round(first - second)| >= 2^23` (half a texture) the second gets `+2^24` (difference >= 0) or `-2^24`. If any
value moved, the six floats are stored in the next 0x30-byte slot at [0x1480c], the face points to it (+0x10) and
gets flag 0x80 in +0x0c; the filler then uses them instead of the vertices' (u, v). For a GPU: build non-indexed
per-face corners with these (u, v) (they may lie outside [0, 2^24), use GL_REPEAT).

## 4. The camera (per frame)

### 4.1 0x15bef: per-frame entry used by four parts

```c
void engine_frame(void)                                      // 0x15bef
{
    t = (float)(int)(xmpGetTimer() + part_time0 /*[0x6542]*/) * time_scale;   // [0x159f7]
    camera_path();                                           // 0x159fb
    if (wobble /*[0x15c3f]*/) roll_wobble();                 // 0x15c40
    look_at();                                               // 0x154bc: matrices into every object
    render(page /*[0x2060]*/ + window_offset /*[0x15c3b]*/); // 0x1f5d1
}
```
Time base: xmpGetTimer (PIT clocks since the music start) plus the part's start value [0x6542] (set by each part),
times time_scale = header * 1e-9: scene 0: 1.0739 units per second, 1: 1.5511, 2: 2.2670, 3: 2.4007, 4: 2.6250.
The metaball part does the same steps itself (0x110f9..0x1112b) without the wobble. Nothing in the engine depends
on the frame rate.

### 4.2 Camera paths 0x159fb / 0x15a59

Table at **0x15afc: 5 scenes x 9 floats** (data, read it from the exe): p0 Rh, p1 Rv, p2 target scale, p3 target
time offset, p4 a, p5 b, p6 c, p7 d, p8 roll speed.

| scene | Rh | Rv | p2 | p3 | a | b | c | d | p8 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 2950 | 500 | 0.83 | 0.25 | 0.7 | 0.4 | 0.1 | 0.5 | 0.2 |
| 1 | 530 | 510 | 0.5 | -4.0 | 0.276 | 0.311 | 0.412 | 1.201 | 0.02 |
| 2 | 2700 | 210 | 0.87 | 0.4 | 0.53 | 0.29 | 0.3 | 0.26 | 0.39 |
| 3 | 930 | 210 | 0.5 | 1.0 | 0.316 | 0.291 | 0.412 | 0.831 | 0.09 |
| 4 | 1430 | 1310 | 0.07 | -4.0 | 0.276 | 0.311 | 0.412 | 1.201 | 0.02 |

```c
vec3 path(float t, float s, float *roll)                     // 0x15a59
{
    phi   = t*a + c*sin(t*a);
    theta = d * sin(t*b);
    *roll = t * p8;
    return vec3(Rh*s*cos(theta)*cos(phi),  Rv*s*sin(theta),  Rh*s*cos(theta)*sin(phi));
}
eye    = path(t, 1.0, &roll);                                // [0x1549c]
target = path(t + p3, p2, &roll);                            // [0x154a8]; roll = (t + p3) * p8 (last call wins)
```
(Exact operation order: `((cos(theta)*cos(phi))*Rh)*s`, `(sin(theta)*Rv)*s`, cos/sin(phi) stored as floats first.)

### 4.3 Roll wobble 0x15c40 (when [0x15c3f] != 0)

`roll = ((roll + pi*sin(0.21*t)) + pi*sin(0.62*t)) + sin(0.14*t)` (constants 0x15c7f, 0x15c83, 0x15c87; the last
term has no pi). Set by the tunnel part (0x16a3e) and by the ribbons part from its inset section on (0x170bd);
0x1212a sets it for scene 3 but the spiky part 0x15d20 clears it before drawing; the tube clears it.

### 4.4 Look-at 0x154bc (+ 0x15401 normalize, 0x1545d matrix * vector)

```c
dir = normalize(target - eye);
T   = eye + F*dir;                 // the camera-space origin: the point F in front of the eye
D   = eye - T;                     // = -F*dir
yaw   = pi - atan2(D.x, D.z);
pitch = 4.712389 /*0x15710, 3pi/2*/ - atan2(-sqrt(D.x*D.x + D.z*D.z), D.y);
cy, sy = cos, sin(yaw);  cp, sp = cos, sin(pitch);
right = (cy, 0, sy);  up = (sp*sy, cp, -(cy*sp));  fwd = (-(sy*cp), sp, cy*cp);
// roll (cr, sr = cos, sin(roll)):
right' = right*cr - up*sr;  up' = right*sr + up*cr;
M = rows (right', up', fwd);  tr = M * (-T)   // tr[r] = -T.x*M[r][0] + (-T.y*M[r][1] + -T.z*M[r][2])
for every object: rows -> +0x10..+0x30, tr -> +0x34..+0x3c, F -> +0x40.
```
In plain terms: fwd = normalize(target - eye), right = normalize((fwd.z, 0, -fwd.x)) (world up = +y),
up = (-fwd.x*fwd.y, fwd.x^2 + fwd.z^2, -fwd.y*fwd.z)/sqrt(fwd.x^2 + fwd.z^2); then (right, up) rotated by roll so
that camera x' = cr*x - sr*y, y' = sr*x + cr*y (the picture turns counter-clockwise by roll). Camera space is
left-handed: x right, y up, z forward. Because the translation is relative to T and the projection adds F to z
(5.2), z' is exactly the distance from the eye along fwd, and F only acts as the focal length.

## 5. The renderer 0x1f5d1

```c
void render(u16 *target /*edi*/)                    // 0x1f5d1
{
    fninit; control word &= 0xfcff;                  // 24-bit precision, round to nearest
    win = target;  win_end = target + H*pitch;       // [0x18d98], [0x18da4], [0x18da0]
    build_face_list();                               // 0x18e3b: transform every object, collect visible faces
    n = list_bytes / 8;                              // [0x18d94]
    radix_sort();                                    // 0x1966e
    if (target) { clear(); draw_list(); }            // 0x18dc4, 0x19240
}
```

### 5.1 0x18e3b: per object (in order)

`if (flags & 0x40) skip; if (flags & 0x80) callback(+0x48); if (!(flags & 2)) transform(obj) /*0x18ea1*/;
if (!(flags & 1)) add_faces(obj) /*0x19f78*/;`

### 5.2 Transform and projection 0x18ea1 (+ 0x19118 constants, 0x19022 scaled matrix)

0x19118 writes the screen size into three immediates of 0x18ea1 (self-modifying code) and computes the constants;
0x19022 divides the object's matrix by them:
```c
kx = (1/65536 * 320) / W;   ky = -(200 / H);   cx = (W >> 1) * 65536.0;   cy = H >> 1;   NEAR = 16;
for each vertex v (object space x, y, z):
    z' = (x*m20 + tz) + (y*m21 + z*m22);           // tz = t.z + F (m2x / 1.0)
    v.z = z';
    if (fist(z') <= NEAR) { v.inside = 4; continue; }   // rounded to nearest: z' < 16.5 (16.5 itself too)
    q  = F / z';
    x' = (x*(m00/kx) + tx/kx) + (y*(m01/kx) + z*(m02/kx));   // = 65536*W/320 * camera x
    sx = x'*q + cx;                                    // 16.16 pixels: W/2 + (W/320)*F*x/z'
    y' = (x*(m10/ky) + ty/ky) + (y*(m11/ky) + z*(m12/ky));   // = -H/200 * camera y
    sy = y'*q + cy;                                    // pixels: H/2 - (H/200)*F*y/z'
    inside = 1, out = 0;
    if (!(bits(sx) <u bits(W*65536.0))) { inside = 0; out = (int)bits(sx) < bits(W*65536.0) ? 1 : 2; }
    if (!(bits(sy) <u bits((float)H)))  { inside = 0; out += (int)bits(sy) < bits((float)H) ? 4 : 8; }
    if (v.flag & 1) {                                  // environment coordinates
        v.eu = (nx*m00 + (ny*m01 + nz*m02)) + 8388608.0;   // unscaled rows: right, up
        v.ev = (nx*m10 + (ny*m11 + nz*m12)) + 8388608.0;
    }
```
(The compares use the float bit patterns as integers: unsigned "below" = 0 <= value < limit, the signed compare
then separates negative values. +0.0 is inside, -0.0 is not.)

Projection in plain terms, full screen: `x_s = 160 + F*x/z`, `y_s = 100 - F*y/z` with (x, y, z) in camera space,
F in pixels of the 320x200 page (shown at 4:3, so the pixels are 1.2 times taller than wide). For a W x H window
the same view is scaled to it: `x_s = (W>>1) + (W/320)*F*x/z`, `y_s = (H>>1) - (H/200)*F*y/z` (the centre is
truncated: 87 for the 175-wide inset). Camera angles, horizontal 2*atan(160/F) x vertical 2*atan(100/F): scene 0
F=350: 49.1 x 31.9 degrees; 1 F=160: 90.0 x 64.0; 2 F=400: 43.6 x 28.1; 3 F=300: 56.1 x 36.9; 4 F=230: 69.6 x
47.0. Shown at 4:3 the 200 rows take the height of 240 square pixels, so the picture is 1.2 times taller than an
undistorted perspective with the same horizontal angle; that is the original's look. For OpenGL:
`x_ndc = F*x/(160*z)`, `y_ndc = F*y/(100*z)` in the 4:3 viewport (or the window's viewport), at any resolution.

Environment coordinates: with the normal of length 2^23, `eu/65536 = 128 + 128*(n̂·right)`,
`ev/65536 = 128 + 128*(n̂·up)` in texels of the 256x256 highlight map. The normals use the camera rotation only
(no perspective: a view-space "normal x/y" mapping, not a reflection vector).

### 5.3 Face list 0x19f78 (per object)

```c
for each face f of the object:
    if (f.type & 0x80) {                                       // dead path (no such faces)
        if (fist(A.z) - 17 < 0) continue;  key = fist((A.z + A.z) + A.z);  add;  continue; }
    f.clip = 0;
    al = 3 - A.inside - B.inside - C.inside;
    if (al < 0) continue;                                      // a corner at/behind the near limit: face dropped
    f.area = (A.sx - C.sx)*(B.sy - C.sy) - (B.sx - C.sx)*(A.sy - C.sy);   // 16.16 * px
    if (!(f.flags & 0x40) && signbit(f.area)) continue;        // back face (also -0.0); front = clockwise on screen
    z = (A.z + B.z) + C.z;
    if (al == 3 && (A.out & B.out & C.out)) continue;          // all off screen on the same side
    if (al != 0 && (A.out | B.out | C.out)) { f.clip = A.out | B.out | C.out; f.nout = al; }
    list[n++] = { fist(z), &f };                              // key: rounded sum of the three depths
```
No near-plane clipping: a face with any corner at z' < 16.5 disappears completely. (In the 23 checked frames none
of the dropped faces would have covered a pixel if it had been clipped instead; it can only show when geometry
passes within 16 units of the eye inside the view.)

### 5.4 Radix sort 0x1966e

Two counting passes over the low 16 bits of the key (low byte, then the inverted high byte), each placing entries
at decrementing positions (`pos = --count[bucket]`, buckets prefix-summed). Result: descending key (farthest
first); entries with equal 16-bit keys end up in the order of the list (second pass reverses the first pass's
reversal). Keys above 65535 would wrap; the keys in the 23 checked frames are 194..18,668 (largest: the scene-0
dome seen from the far side of the eye path). Only the face pointers are written back to the list (+4 of each
entry).

### 5.5 Clear 0x18dc4 and draw 0x19240

Clear mode [0x18d88]: 0 = black, 2 = fill with colour [0x18d8c] (16 bit, doubled into a dword), 1 = copy rows of
W pixels from the packed buffer [0x18d8c], anything else = nothing. Main sets 0xff and nothing changes it: the
parts draw their background into the page first (or the domes cover everything). 0x19240 then calls
`draw_type[f.type](f)` for the list from index 0 (farthest) to the end: painter's algorithm, later faces overwrite.

## 6. The triangle filler 0x1e07c (types 1..3)

### 6.1 Set-up (unclipped path; the clipped path 0x1e9dc is the same apart from 6.3)

```c
// corners: A, B, C = face corners (if face.flags & 0x80, copies in scratch records 0x1f4f8 / 0x1f538 / 0x1f578
// with (u, v) from the face's table).  Each corner: X = sx (16.16), Y = sy, u, v, eu, ev.
dyA = A.Y - C.Y;  dyB = B.Y - C.Y;
for q in (u, v, eu, ev):  N_q = (B.q - C.q)*dyA - (A.q - C.q)*dyB;
inv = 65536.0 / face.area;
g_q = fist(inv * N_q);          // = -dq/dx per pixel (16.16 / 8.16 integer), constant over the triangle
sort A, B, C by Y (A top, C bottom): compares of the float bits as unsigned (clipped path with vertical
    clipping: bits mapped to a monotonic key so negative Y sort correctly);
yA = fist(A.Y + 0.5);  yB = fist(B.Y + 0.5);  yC = fist(C.Y + 0.5);    // first scanline of each part
if (!clip) { if (H <= yA) return;  if (H <= yB) yB = yC = H; else if (H <= yC) yC = H; }
invL = 1 / (C.Y - A.Y);
sub_u[i] = i * (g_u >> 3), sub_v[i] = i * (g_v >> 3), i = 0..7;      // 1/8-pixel x pre-step, u and v only
if (yB != yA) {
    dxAB = (B.X - A.X) / (B.Y - A.Y);  step_s = fist(dxAB);
    dy0 = yA - A.Y;  xs = fist(dy0*dxAB + A.X);  t0 = dy0*invL;
    for q in (X, u, v, eu, ev): step_q = fist((C.q - A.q)*invL);  q_long = fist((C.q - A.q)*t0 + A.q);
    spans(line yA, count yB - yA, xs, step_s, long edge values, steps);
    if (yC == yB) return;
    dxBC = (C.X - B.X)/(C.Y - B.Y);  step_s = fist(dxBC);  xs = fist((yB - B.Y)*dxBC + B.X);
    spans(next line, count yC - yB, ...);          // long-edge values continue from the first part
} else {                                            // flat top
    if (yC == yB) return;
    dxBC = (C.X - B.X)/(C.Y - B.Y);  step_s = fist(dxBC);
    t0 = (yA - A.Y)*invL;  xs = fist((yB - B.Y)*dxBC + B.X);
    long edge values / steps from A..C as above;  spans(line yA, count yC - yB, ...);
}
```
So all attributes (x, u, v, eu, ev) are interpolated along the long edge A..C only, rows are sampled at integer y
(with an exact float pre-step), and inside a row the attributes change by the constant per-pixel gradient:
classic affine (screen-linear) mapping, no perspective correction. All DDAs are 32-bit integers with rounded
steps.

### 6.2 Span loop 0x1e77c (per scanline)

```c
s  = (x_long >> 13) & 7;                     // eighths of a pixel of the long edge's x
pu = u + sub_u[s];  pv = v + sub_v[s];       // u, v moved back to x = floor(x_long); eu, ev are not
xi = x_long >> 16;  n = (xs >> 16) - xi;     // arithmetic shifts
if (n > 0)  for (k = 0; k < n; k++) { put(xi + k, pu, pv, peu, pev);  pu -= g_u; pv -= g_v; peu -= g_eu; pev -= g_ev; }
if (n < 0)  for (x = xi - 1; x >= xi + n; x--) { pu += g_u; pv += g_v; peu += g_eu; pev += g_ev;  put(x, ...); }
line += pitch;  xs += step_s;  x_long += step_X;  u += step_u;  v += step_v;  eu += step_eu;  ev += step_ev;

put(x, u, v, eu, ev):
    e   = envmap[((ev >> 16) & 255)*256 + ((eu >> 16) & 255)];        // 8-bit, nearest texel, wraps
    t   = 3 * (((v >> 16) & 255)*256 + ((u >> 16) & 255));             // nearest texel, wraps
    pixel = TR[e + tex[t]] + TG[e + tex[t+1]] + TB[e + tex[t+2]];      // 16-bit store
```
Coverage: pixel i of a row is drawn when floor(x_left) <= i < floor(x_right), i.e. the edges are sampled at
x = i + 1 while u, v belong to x = i; rows at integer y (the top edge of the pixel). A half-pixel offset of the
whole image against pixel-centre sampling; irrelevant at high resolution.

### 6.3 Clipped faces 0x1e9dc / 0x1f07c

Only for faces with f.clip != 0. Identical arithmetic, plus: vertical clipping per line (stop when the line
pointer reaches the window end, skip lines above the window start by advancing every edge value by
n*step), horizontal clipping per span (left: advance pu, pv, peu, pev by -xi pixels and start at 0; right: shorten
at W). The three table values are combined with OR instead of ADD (same result: the fields do not overlap). The
result equals rasterising the whole triangle and keeping the pixels inside the W x H window.

### 6.4 Textures

[0x18d54] = 0x654a: dword pointer table, entry 0 = 0x220000 (the 64 KB highlight map), entries 1..17 = the 17
texture buffers (0x30000 bytes each, 0x230000..0x530000). The filler reads them as **interleaved** 24-bit
R, G, B (byte 0 = red) with texel index v*256 + u (row = v). The decrunched textures start planar; the parts call
the core helper 0x13eec (planar -> interleaved, in place) on the ones they use (tube: [0x6552] = texture 2, spiky:
[0x656a] = 8, tunnel: [0x654e] = 1); 13 and 14 are already interleaved in the snapshots (textures scope).

### 6.5 The highlight map 0x220000 (core precalc 0x6592, used only by the engine)

```c
for (r = 0, y = -128; r < 256; r++, y++)
  for (c = 0, x = -127; c < 256; c++, x++)              // x is incremented before use: -127..128
    env[r*256 + c] = fist(sqr(sqr(255 - sqrt(x*x + y*y)) / 256) / 256);   // float32 steps, low byte stored
```
Peak 252 at (c, r) = (127, 128), 16 at a distance of 127 texels; regenerated bit-exactly. For a unit normal the
distance from the centre is 128*sqrt(n_r^2 + n_u^2) = 128*sin(angle between the normal and the view axis), so the
highlight term is (to half a texel) **E = (255 - 128*sin α)^4 / 2^24**, from 252 (normal along the view axis) to
15.5 (normal across it). It is the same whether the normal points at the camera or away from it.

### 6.6 The 16-bit pixel: tables 0x1a479 / 0x1b079 / 0x1bc79 (768 dwords each, core 0x1a336)

Indexed by `e + texel channel` (0..510). Built by 0x1a336 (eax = offset; 0x1f5c5 is just `call 0x1a336`) with
gain k = [0x1a225] (0x100 = 1.0; the parts set it only around their call) and the flash level f = [0x1a478]
(core, frame start 0x638b):
```c
c  = clamp((i*k + offset*256) >> 8, 0, 255);
c2 = (c + (255 - c)*f + (255 - f)*c) >> 8;          // blend towards white by f/255 (f = 0: c2 = c)
TR[i] = ((c2 << 5) >> 8) << 11;  TG[i] = ((c2 << 6) >> 8) << 5;  TB[i] = (c2 << 5) >> 8;   // 5:6:5 from the VBE masks
```
Values used with the engine: ribbons k = 0x1c8 (1.78), offset -200 (a hard, high-contrast look: only
e + texel > 112 is visible); tube offset 0; tunnel 0; spiky offset -40; metaballs k = 0x132 (1.195), offset -50,
f = 0xff while the part shows white. So per channel: **out = sat(sat(e + texel)·k/256 + offset)**, the sum
e + texel saturating only through the clamp after gain and offset (verified for every table entry).

## 7. Time bases

| variable | time base |
|---|---|
| t (camera, roll, wobble) | xmpGetTimer + [0x6542] (the part's start value) times the scene's scale (4.1); float32 |
| object geometry | static after the script (built once at the part start) |
| object visibility / window size / offset / gain / flash | set by the parts (sync events, part scopes) |
| anything per frame | nothing in the engine |
| random numbers | none |

## 8. For the GPU remake

What to keep (content), straight from the formulas above:
- Geometry from the scripts at 0x157a4 / 0x157fa / 0x15945 / 0x15973 / 0x159cb (read the bytes from JIZZ.EXE and
  run the generators; the vertex counts are small enough to rebuild exactly or with more segments, see below),
  smooth area-weighted normals, per-face seam (u, v).
- Camera paths from the table at 0x15afc, the look-at with F (in 320-pixel units) and roll (+ wobble), the
  projection `x_ndc = F*x/(160 z)`, `y_ndc = F*y/(100 z)` in a 4:3 (or inset) viewport.
- Back-face culling: front faces are clockwise on screen (y down positive area); domes are seen from inside.
- Shading: `out_c = clamp(floor((E + tex_c)*k/256) + offset, 0, 255)`, blended to white by the flash, with E from
  6.5 and tex_c the 8-bit texel; then /255 (no 5:6:5 truncation).

Shortcuts to evaluate exactly instead:
1. **Affine texture and highlight interpolation** (6.1): screen-linear u, v, eu, ev per triangle. Big triangles
   (the domes: 24x12 or 22x11 segments around the camera; the tunnel) show bent texture and kinks along triangle
   edges. Use perspective-correct interpolation (the GPU default).
2. **Highlight per vertex**: eu, ev are computed at the vertices and interpolated. Evaluate E per pixel from the
   interpolated, renormalised camera-space normal: `E = (255 - 128*sqrt(1 - (n̂·fwd)^2))^4 / 2^24`
   (the map's peak is half a texel off the n = 0 point: texel (127, 128), centre (127.5, 128.5) instead of
   (128, 128); this can be dropped).
3. **Nearest-texel reads** of the 256x256 textures (8.16 coordinates truncated, wrapping): use bilinear filtering
   with mipmaps and GL_REPEAT (texel k covers u in [k, k+1) texels, same as GL's convention). The domes minify
   their textures strongly near the poles and edges: the original aliases there.
4. **16-bit output** (5:6:5 by truncation) and the integer table steps: compute in float.
5. **Painter's algorithm** with a 16-bit centroid key (5.4): use a depth buffer. Differences only where faces
   intersect or overlap cyclically, e.g. scene 1, where the six cone tips end 90 units from the centre, inside the
   bumpy sphere of radius about 100: the original shows whole triangles of one or the other in front there, a
   depth buffer shows the true intersection curve.
6. **No near clipping** (5.3): faces with a corner within 16.5 units of the eye vanish. A GPU clips them at the
   near plane instead (use near = 16 or less). Not visible in any checked frame; a faithful option is to drop such
   triangles before drawing.
7. **Rounding of vertex positions** (x to 1/65536 px, edges evaluated by rounded integer DDAs, coverage at the
   pixel's right/top edge): sub-pixel errors and occasional one-pixel cracks or overlaps between neighbouring
   triangles (the long edge of one triangle and the short edge of its neighbour are stepped differently). Use
   watertight rasterisation with MSAA; place the projection centre at the viewport centre.
8. **Geometry resolution**: the spheres / tori have 9..90 segments; at high resolution their silhouettes show the
   polygons. The generators are analytic, so a remake may evaluate them with more segments (keeping the same
   parametrisation, u/v and the "halve" pattern of the ribbons: every other quad in the phi direction of a
   20-segment tube cross-section, i.e. 10 strips) — a choice for the remake; the original's polygon outline is
   part of its look at 320x200 only.

## 9. Other details

- 0x1f5c5 (`call 0x1a336`) and 0x1f5cb (`call 0x1a089`) are thin wrappers; 0x1f5b8 (core) builds both table sets
  at start-up.
- Self-modified immediates: 0x18f0b (near 16), 0x18f58 (W*65536 as float bits), 0x18fa3 (H as float bits), the
  filler's texture bases (0x1e7f3 env, 0x1e7fa / 0x1e820 / 0x1e838 R/G/B and their backwards-loop and clipped
  copies) and gradient immediates (0x1e806 .. 0x1e8d8, 0x1f1bf .. 0x1f320), the radix sort's buffer addresses.
  All are written before use every frame / face; they are not state.
- The FPU precision: generators run at 24 bits (the FPU state left by main or by the previous part's last render);
  0x1a089 (normals) switches to 64 bits with fninit; 0x1f5d1 switches back every frame. The first frame after a
  script therefore computes its camera in 64-bit precision (only matters for bit-exact models).
- Engine time is independent of the frame rate; at the capture's speed the parts render at 23..39 fps (PARTS.md).

## 10. Models and how they were checked (`jizz/re/models/engine/`, see its README.md)

- `scripts.py`: decodes the five scripts (section 3.1 table).
- `model_objects.py`: interpreter + generators + normals + seam fix; 0 ulps against all objects in t1365, t3285,
  t2515, t4050 and object 0 of t7506 (the metaball mesh is built by its part).
- `run_engine.py`: copy of run_jizz.py with `--dump-render DIR` (state at the call of 0x1f5d1 and the window after
  it), `--dump-tick0`, `--list-renders` (tick, W, H, offset of every render).
- `model_render.py DIR N`: full renderer model on a dump; 0 differing pixels in all 23 dumped frames (tube ticks
  3290..3301 and 3704..3709, tunnel 2601..2606, spiky 4704..4709, metaballs 7802..7808, ribbons 1502..1505, inset
  175x110 at 2230/2232, inset 160x100 at 2426/2427). `match_capture.py`: 15 of these pages are byte-identical to
  `full.frames` records (the tube frames before tick 3700 are still faded by 0x17999 afterwards, the inset frames get
  their frame and fades).
- `model_camera.py DIR N`: camera chain from t; 0..2 ulps on the 13 matrix, translation and F values (23 frames).
- `check_tables.py DIR N`: shading-table formula (0 of 2304 entries differ, with the gain each part used) and the
  highlight map formula (0 of 65536 bytes differ).
- `stats.py DIR N`: near-dropped / clipped face counts, key range, and the area the near-dropped faces would cover
  if clipped at z = 16 instead (0 pixels in all 23 frames).

## 11. Not determined / notes for other scopes

- Draw types 0 and 255 point at data (0x1f4f8) and the single-point faces (type bit 0x80) have no drawer: dead code
  in this intro (maybe used by other productions of the engine). No face of those types is ever created.
- The object callback (flag 0x80, +0x44) and merge (op 7) are never used.
- Which picture each texture index holds and when textures 13/14 become interleaved: textures / part scopes. The
  ribbons part changes textures 13/14 later (the inset shows green/blue ribbons) — parts3d-a.
- The metaball mesh (object 1 in scene 4) and its per-frame counts [0x18d5c] / [0x18d60] (0x110d7..0x110f4; note
  0x10ff9 stores eax instead of ebx into [0x1114a], so the vertex count bookkeeping uses the face count): parts3d-b.
