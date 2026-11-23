# Reve — the 3D engine (engine scope)

LCA's 3D engine as REVE.LFL links it: the world loader, the per-refresh animation step, the renderer with its
camera, culling, transforms, near-plane clipping, sorting, lighting and rasterizers, and the two full-screen passes
the 3D entries run after it. This file describes the engine by what goes in and what comes out, with every constant,
so that `notes-parts3d.md` can refer to it and the remake can render the same geometry with OpenGL.

All of it is integer code (no FPU). Addresses are linear addresses of the running image (NOTES.md).

**Verified** (`models/engine/`, README there): a Python model of the loader, the animation and the whole render path
reproduces the original's back buffer **pixel for pixel** in 37 renders taken from six parts (INTRO, P3D1 early and
late, PART2, P3D3, PART4, FIN: textured and textured-Gouraud faces, flat faces, near-plane clipping, screen
clipping, PART4's wireframe boxes), and every transformed and projected vertex of the drawn objects. With the 2D
passes (AA pass, credits sprite, FIN's blend) the model reproduces the captured screen exactly. End to end, from the
archive's files and the refresh number alone (no memory from the run), it reproduces 213 sampled capture records of
all 3D entries: 191 exactly, 17 torn records exactly above and below the tear (the screen was captured while a pass
was writing it), 2 exactly with the state split between two steps (an interrupt during the render's object loop),
and in 3 PART4 records everything but a few box lines that start at corners behind the camera (history; see
"PART4's bounding boxes").

## Where the code is

| address | what |
|---|---|
| 0x21d440 | world loader: loads `3D\*.WLD`, allocates the objects, calls 0x21d538 per object |
| 0x21d538 | object loader: faces, vertices, face normals, vertex normals; 0x21d9ac: bounding box |
| 0x25f3ae | animation step (the 3D entries' retrace routine): reads the ANI stream |
| 0x25f5b5 | render: camera 0x21db9c, per object 0x21e97c (setup and cull through [0x21d43c]), draw 0x21e9bc, PART4's boxes 0x25f26e |
| 0x21db9c | camera matrix and translation |
| 0x21ddf8 / 0x21dfb8 | object setup and bounding-box cull (default / PART4), via [0x21d43c] |
| 0x21e158, 0x21e354 | object matrices: rotation from angles, camera × rotation |
| 0x21e61c | object origin in camera space |
| 0x21e4c4, 0x21e54c | patch the vertex transform's immediates (self-modifying), transform and project a vertex |
| 0x21e9bc | draw: object sort, per object: lighting point, vertices, faces, near clip 0x21e864, back faces, face sort, dispatch |
| 0x21e6ec | the sort (recursive quicksort, descending) used for objects and faces |
| 0x21e72c, 0x21e7c8 | lighting: flat (per face), per vertex |
| 0x21ec24 | face-type jump table (types 0..11) |
| 0x21bccc | polygon dispatch (table 0x21bd1c): flat 0x21bce0 → 0x21bd4c, Gouraud 0x21bcfc → 0x21c0ec (unused), texture 0x21c6b4, texture + shade 0x21cc28 |
| 0x21b14c, 0x21b42c, 0x21b70c, 0x21b9ec | screen clippers: left, right, top, bottom |
| 0x21c05c, 0x21cb3c, 0x21d0fc | span routines: flat, texture, texture + shade (immediates patched per segment) |
| 0x25f26e, 0x25f139, 0x21d22c | PART4: transform every object's bounding box, draw its 12 edges, the line routine |
| 0x2045fa | full-screen pass through `GFX\ANTIALIA.PAL` (the "AA pass"), then clears its source |
| 0x2046c7 | FIN's pass: blends the back buffer into the screen through `GFX\ANTIALIA.PAL`, clears the back buffer |
| 0x2175dc | 32-bit integer square root (floor; checked over the whole range, `check_isqrt.py`) |
| 0x2175bc | fills the square-root byte table 0x219ec8 at start |

Globals: 0x217460 object array, 0x217464 object count, 0x217468 ANI buffer, 0x21746c key index, 0x217470 refresh
counter in the key, 0x2174c2 pointer to the current key, 0x217474..0x2174a0 camera (below), 0x21743c camera block
(below), 0x2174c6 PART4 flag (boxes), 0x21d43c object-setup routine, 0x23c374 / 0x23c378 / 0x23e2b8 object list
(count, keys, pointers; 2000 entries), 0x21eeb0 / 0x21eeb4 / 0x22d914 face list (count, keys, pointers; 15000
entries), 0x240270 near-clip vertex list, 0x24028c near-clip vertex pool (2000 × 0x1e, pointer 0x240284), 0x24ecec
near-clip face pool (pointer 0x240288), 0x2401f8 polygon vertex buffer (3 × 0x1e), 0x21a5c0 / 0x21a6b0 / 0x21a7a0 /
0x21a890 clipper outputs (8 vertices each), 0x21ee54..0x21ee80 camera matrix, 0x21ee90 lighting point, 0x21ee98 /
0x21ee9c / 0x21eea0 / 0x21eea4 clip window 0, 319, 0, 199 (static), 0x21eea8 outcode bytes (8), 0x21b140 texture
pointer, 0x21b144 current texture base, 0x21b148 texture mask 0x3f3f (static), 0x21a980 row offsets y·320 (static,
200 dwords), back buffer [0x205e8c].

## Units and conventions

- **Angles**: 8192 units per turn. A table index is `(a & 0x1ffe) >> 1` into the sine table at **0x2176c8**: 5120
  signed words, 4096 per turn, ≈ 32767·sin(2πi/4096) (within 1 of `round(32767 sin)`; static in the image: read it
  at 0x2176c8, 10240 bytes). Cosine = the entry 1024 further (0x217ec8). `S(a)`, `C(a)` below are these words.
- **Fixed point**: matrices are 1.15 (32767 ≈ 1). `mul(p, q)` = `(p·q) >> 15` of two words, kept as a signed word
  (`imul word` + `shld dx, ax, 1` or `shrd ax, dx, 15`): floor, and sums of two such terms wrap at 16 bits.
- **World**: object positions are int32, mesh coordinates int16 (object space). Camera space comes out as int16
  words (`>> 15` of the 32-bit sums).
- **Camera space**: x right, y up, z forward. **Projection**: for z > 128,
  `sx = trunc(256·x / z) + 160`, `sy = 100 − trunc(256·y / z)` (integer pixels of the 320 × 200 buffer). The same
  focal length (256 pixels) on both axes: horizontal half-angle atan(160/256) = 32.0°, vertical atan(100/256) =
  21.3°; shown on a 4:3 screen, so the original's pixels are 1.2 times as tall as wide and the image is stretched
  vertically by 1.2 relative to square pixels. The remake keeps this projection (x_ndc = 256·x/(160·z), y_ndc =
  256·y/(100·z)) and shows the frame at 4:3.
- **Colours**: everything is palette indices of `GFX\TEXTURE.RAW`'s palette (the 3D entries load or inherit it; it
  never changes during them: checked over the capture). Index 0 is black (the background).

## Data structures in memory

The loader allocates `nv·0x1e + nf·0x16 + no·0x13f` bytes ([0x217460]): `no` objects of 0x12c bytes, then per
object its mesh (0x10-byte header, faces, vertices; 4-aligned).

Object (0x12c bytes):

| offset | type | meaning |
|---|---|---|
| 0x00 | ptr | mesh: dword vertex count, dword face count, ptr vertices, ptr faces |
| 0x04 / 0x08 / 0x0c | int | position (integer; refreshed from 0x1c.. at every render: `pos = pos16 >> 16`) |
| 0x10 / 0x14 / 0x18 | int | angles × 65536 (`ang >> 16` is the angle in 8192ths of a turn); 0 after load and key 0 |
| 0x1c / 0x20 / 0x24 | int | position × 65536 (the ANI's deltas are added here) |
| 0x28 / 0x2c / 0x30 | int | `P` = M·pos + T (0x21e61c) |
| 0x34..0x54 | int[9] | rotation R (after the render's setup: R of the negated angles, used for the lighting point) |
| 0x58..0x78 | int[9] | C = M·R (vertex transform) |
| 0x7c..0x12b | 8 × 0x16 | bounding-box corners: words x, y, z; dwords camera x, y, z; words sx, sy |

Face (0x16 bytes): byte type, byte colour, byte flat colour (written by the flat lighting), byte texture,
words face normal x, y, z (length 32767), ptrs to vertices a, b, c.

Vertex (0x1e bytes): words x, y, z, u, v (from the file); words camera x, y, z, sx, sy (per render); words normal x,
y, z (length 32767); word shade (0xffff = not lit yet this render, else level 0..15); word clip flag (1 = z ≤ 128).

## The WLD file (0x21d440, 0x21d538)

```
byte    1                      (anything else: not loaded)
dword   total vertices, total faces, objects, 0, 0
per object:
  byte    0                    (anything else: the object is not loaded)
  dword   vertices nv, faces nf, x, y, z      (position)
  nf ×    byte type, byte colour, byte texture, word a, word b, word c   (9 bytes; vertex indices into this object)
  nv ×    word x, y, z, u, v                                         (10 bytes, signed)
```

Six files (`3D\INTRO|P3D1|PART2|P3D3|PART4|FIN.WLD`); `notes-parts3d.md` lists their objects. Face types used:
**1** (flat, lit per face: PART4's letters), **6** (texture), **8** (texture + Gouraud). `u`, `v` are 8.8 texels
(texel × 256) and **are read as unsigned words** by everything after the loader: a negative u is u + 65536, i.e.
u/256 + 256 texels. 1,586 faces of the six files have a u or v that crosses 0, and their texture runs across 256
more texels than the signed values say (four more repeats of the 64-texel tile): the remake reads `u = (uint16)U /
256` texels and gets the same mapping.

Face normal (per face, in file order; `a, b, c` the face's vertices, 32-bit products, 64-bit sum of squares):

```
nx = (b.y-a.y)*(c.z-a.z) - (b.z-a.z)*(c.y-a.y)        // each product kept to 32 bits
ny = (b.z-a.z)*(c.x-a.x) - (b.x-a.x)*(c.z-a.z)
nz = (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x)
q  = nx² + ny² + nz²                                  // 64-bit
if (q >> 32) is 0 or -1:  len = isqrt(q & 0xffffffff) or 1;   n = clamp16(trunc((n << 15) / len))
else:                     len = isqrt((q >> 8) & 0xffffffff) or 1; n = clamp16(trunc((n << 11) / len))
face.n = n;  accumulate n into vertex a only (dwords +0x14 x, +0xa y, +0xe z)
```

Vertex normal: `q = ax² + ay² + az²` (64-bit), `len = isqrt((q >> 16) & 0xffffffff) or 1`, `n = clamp16(trunc((acc
<< 7) / len))` (length ≈ 32767). **Only the faces whose first vertex it is contribute** to a vertex's normal; a
vertex that is no face's first vertex keeps the zero normal and is always lit as level 15 (darkest): 1,302 of the
six files' vertices that lie on Gouraud faces (INTRO 127 of the surfer's 499, P3D1 301, PART2 250, PART4 624), and
the vertex normals that do exist average only some of the surrounding faces. This shapes the shading of every
Gouraud object (dark corners in the interpolation); the remake computes the normals the same way.

Bounding box (0x21d9ac): min / max of the vertices' x, y, z (initial 0x7fff / 0x8001); corner k has x = max if
k ≥ 4, y = max if k is 2, 3, 6 or 7, z = max if k is odd.

## The ANI file and the animation step 0x25f3ae

```
byte    0
byte    per        refreshes per key
dword   nkeys      number of delta keys
key 0 (absolute) and nkeys delta keys, each:
  word    (unused: 0x2038 in key 0, 0xffff in the others)
  dword   camera x, y, z, yaw, pitch, roll
  { word object, dword x, y, z, angle0, angle1, angle2 } ...   0x1a bytes each
  word    0xffff
```

In key 0 the camera values are absolute integers and the object records give integer positions (their three angle
dwords are ignored: angles start at 0). In delta keys every value is a 16.16 increment per refresh: the camera's
integer part at 0x217474.. (x, y, z) / 0x21748c.. (yaw, pitch, roll) and fraction at 0x217480.. / 0x217498.. form
48-bit accumulators (`frac += d << 16; int += (d >> 16) + carry`); the objects' `pos16` and `ang` (×65536) get the
increment added.

```
step():                                     // the retrace routine of the 3D entries: once per refresh
  if key == 0:                              // [0x21746c]
      camera = key0.camera, fractions = 0; for each record: pos = (x, y, z), pos16 = pos << 16, ang = 0
      key = 1; ptr = key 1; return
  apply ptr's deltas (camera and the listed objects)
  if ++count == per:                        // [0x217470]
      count = 0; key = key + 1 > nkeys ? 0 : key + 1; ptr = next key
```

The entry's init calls `step()` once (key 0); from the refresh at which its retrace routine is set, the timer
interrupt calls it at every refresh. So after the first refresh r0 of the entry (the refresh its retrace routine is
set, `notes-parts3d.md`), **the state at refresh r is key 0 followed by n = r − r0 delta steps**, a pure function of
n: closed form `value(n) = key0 + Σ deltas` (piecewise linear in n: key k (1-based) applies its deltas during steps
(k−1)·per + 1 .. k·per). After `per·nkeys` steps the next step re-applies key 0: the animation loops with period
`per·nkeys + 1` refreshes (the reset refresh moves nothing; only the INTRO is long enough to loop).

Verified: the model's camera (integers and fractions) and every object's position and angles equal the original's
at 49 dumps and snapshots across all six parts with n = r − r0 (INTRO at refreshes 1400 and 2100 after looping).
The capture itself lost one interrupt in P3D1 (the timer interrupt stalls for two refreshes around refresh 5140),
so from there the capture runs one step behind r − r0; a property of that run, not of the demo.

The render reads the camera at its start and each object's position and angles in its object loop (0x25f619), which
also sets up every object (matrices, box corners) and takes a while: when the timer interrupt arrives during that
loop, the objects after that point are already one step further than the camera and the earlier objects (seen in
2 of 213 sampled records; the remake renders one state). The frame routine draws while the next refreshes pass: in
the capture, record R shows the frame drawn from n = R − r0 − 1 or − 2 (−0 in FIN, up to −4 in the slowest P3D1
frames).

### Camera from the stepped values

0x25f5b5 builds the camera block at 0x21743c: `[+0] = −pitch`, `[+4] = yaw + 0x1000`, `[+8] = −roll`, `[+0xc..0x14]
= h = (−cam) >> 1` (each of x, y, z: negate, then arithmetic shift). 0x21db9c, with `a = (−roll) & 0x1ffe`, `b = (yaw
+ 0x1000) & 0x1ffe`, `c = (−pitch) & 0x1ffe`:

```
M00 = -(mul(Ca,Sb) + mul(mul(Cb,Sc),Sa))    M01 = mul(Ca,Cb) - mul(mul(Sb,Sc),Sa)     M02 = mul(Sa,Cc)
M10 = mul(Sb,Sa) - mul(mul(Cb,Sc),Ca)       M11 = -(mul(Cb,Sa) + mul(mul(Sb,Sc),Ca))  M12 = mul(Ca,Cc)
M20 = -mul(Cb,Cc)                           M21 = -mul(Sb,Cc)                          M22 = -Sc
(16-bit sums, negations of words)
T_r = M_r0·h.x + M_r1·h.y + M_r2·h.z        (32-bit)     -> 0x21ee60 / 0x21ee70 / 0x21ee80
```

In floats (angles θ = value · 2π/8192): the rows are the camera's right, up and forward vectors in world space,
with the world's z axis up:

```
forward = ( cos yaw·cos pitch,  sin yaw·cos pitch,  sin pitch )
right0  = ( sin yaw, −cos yaw, 0 )          up0 = ( −cos yaw·sin pitch, −sin yaw·sin pitch, cos pitch )
right   = cos roll·right0 − sin roll·up0     up  = sin roll·right0 + cos roll·up0
camera position = −2·h = cam rounded up to even (x, y, z each)
```

(The frame right / up / forward has determinant −1: camera space is left-handed, the screen's y is flipped by the
projection.)

## The render 0x25f5b5

```
render():
  camera block, M, T (above)
  for each object i in file order:
      pos = pos16 >> 16
      if setup(object) (through [0x21d43c]):
          append key = (P.z & 0xffff0000) + (number of objects appended so far), object
  sort the object list descending (signed 32-bit keys)
  for each object in that order: draw_object()
  if [0x2174c6]: boxes()                                   // PART4
```

The back buffer [0x205e8c] is not cleared by the render: every frame routine clears it after copying it (the AA pass
and 0x2046c7 clear their source, PART2's frame routine clears it itself). Untouched pixels are index 0 (black).

### Object setup and culling (0x21ddf8; PART4: 0x21dfb8)

```
setup(o):
  a, b, c = (o.ang[k] >> 16) & 0x1ffe  for k = 0, 1, 2
  R = rot(a, b, c);  o.C = (M·R) >> 15  (64-bit sums, kept to 32 bits)        // 0x21e158
  o.R = rot(−a, −b, −c)                                                      // kept for the lighting point
  P = M·pos + T (64-bit); if any component does not fit (high dword not 0 or −1): cull
  if P.z >= 0x1b580000: cull                                                 // far cull
  for k = 0..7 (corner = box corner k, object space, NOT rotated):
      v = (M·corner + (T + P)) >> 15 (64-bit; a component that does not fit: cull the object)
      store v; if v.z > 128:
          sx = trunc(256·v.x / v.z) + 160;  sy = 100 − trunc(256·v.y / v.z)  (stored)
          code = (sx < 0) | (sx > 320) << 1 | (sy < 0) << 2 | (sy > 200) << 3
          if code == 0: return DRAW
          outcodes[j++] = code                                               // 0x21eea8, 8 bytes
  0x21ddf8: return all 8 bytes of outcodes equal ? CULL : DRAW
  0x21dfb8: return CULL
```

`rot(a, b, c)` (0x21e354) is, in floats, **R = Ry(b)·Rz(c)·Rx(a)** with the usual right-handed matrices for column
vectors (`Rx(a) = [[1,0,0],[0,cos,−sin],[0,sin,cos]]`, `Ry(b) = [[cos,0,sin],[0,1,0],[−sin,0,cos]]`, `Rz(c) =
[[cos,−sin,0],[sin,cos,0],[0,0,1]]`): world = R·local + pos. In integers:

```
R00 = mul(Cc,Cb)   R01 = mul(Sa,Sb) − mul(mul(Ca,Sc),Cb)   R02 = mul(Ca,Sb) + mul(mul(Sa,Sc),Cb)
R10 = Sc           R11 = mul(Cc,Ca)                        R12 = −mul(Sa,Cc)
R20 = −mul(Sb,Cc)  R21 = mul(Sa,Cb) + mul(mul(Ca,Sc),Sb)   R22 = mul(Ca,Cb) − mul(mul(Sa,Sb),Sc)
```

Notes for the remake:
- `P` is the camera-space position of `pos + h` = pos − cam/2, not of pos − cam. Its z sorts the objects (the
  order equals the order by the true depth of the object origins, since the added term is the same for all objects)
  and is the **far cull**: an object is not drawn when depth(pos − cam) + depth(cam)/2 ≥ 14000 (0x1b580000 / 32768),
  where depth(v) = forward·v. Keep this rule exactly: it decides when objects appear.
- The box test uses the unrotated box at the object's position, outcodes against the 0..320 × 0..200 rectangle
  (inclusive bounds). An object is drawn if one corner in front of the near plane projects inside the screen, or
  (default routine) the stored outcodes are not all equal. Corners at z ≤ 128 store nothing, so the remaining bytes
  of 0x21eea8 are left from earlier objects or frames: when the stored codes are all equal, those stale bytes decide.
  That happens often (in the sampled frames: 929 object tests in P3D1, 105 in P3D3, 6 in PART2), but **none of
  those objects would put a pixel on the screen** (`model_part.py` draws each of them into an empty buffer): the
  decision is invisible, and the remake can decide such objects either way (or use a proper frustum test).
- PART4's routine draws an object only when a corner projects inside the screen: boxes that span the screen with
  all corners outside are culled (an object filling the view can vanish). Keep it.

### Drawing an object (0x21e9bc)

```
draw_object(o):
  L = lighting point:  e = (−2·h) − pos (32-bit);  L_r = (o.R_r · e) >> 15 as words     // 0x21ee90..94
  transform every vertex (0x21e54c):
      t = P + T
      X = (x·C00 + y·C01 + z·C02 + t0) >> 15  (32-bit wrapping sums), Y, Z likewise; stored as words
      if Z (32-bit) > 128:  sx = trunc((X16·256) / Z) + 160;  sy = 100 − trunc((Y16·256) / Z);  clip = 0
      else clip = 1
      shade = 0xffff
  for each face in file order:
      if a vertex has clip: near_clip(face) → new faces (pool), handled after the loop
      else if front(face): append key = ((a.zc & 0xffff) << 16) + n++, face
  for each new face: if front(face): append key likewise
  sort the face list descending (signed); draw the faces in that order
front(f) = (b.sx−a.sx)·(c.sy−b.sy) − (b.sy−a.sy)·(c.sx−b.sx) < 0    (16-bit differences, 32-bit products)
```

So a face is front-facing when its vertices run counter-clockwise on the screen (y down). **The sort key is the
camera z of the face's first vertex only**, ties broken by the later face first; objects are drawn whole, in
object order, each with its own face sort (painter's algorithm per object). Camera-space coordinates are 16-bit
words (they wrap beyond ±32767, far beyond anything in the parts).

The sort 0x21e6ec (`ebx` first, `ecx` last element, inclusive): pivot = first element; elements greater than the
pivot are moved in front of it (pivot moves right); recurse on [first .. pivot] (it includes the pivot), loop on
[pivot+1 .. last]. Keys are unique (the low word is a running count), so the result is the unique descending order
whatever the algorithm. Its recursion depth reaches the list length for ascending input (12 bytes of stack per
level): sorting a few hundred faces that arrive near-to-far goes below the demo's stack into the extender's
interrupt stacks at 0x140c8..0x148c8 (on a PC an interrupt during such a sort can crash the demo; `run_reve.py`
moves the interrupt stacks). The remake sorts any way it likes, or uses a depth buffer (below).

### Near-plane clipping (0x21e864)

Polygon list from the face, walking the edges (c→a), (a→b), (b→c) with `prev` = the edge's first vertex, `cur` its
second:

```
if prev.clip != cur.clip:
    n = new pool vertex: object-space x, y, z = cur's; camera x, y, z = cur's
    t = trunc(((128 − cur.zc) << 16) / (prev.zc − cur.zc))
    x = cur.xc + ((prev.xc − cur.xc)·t >> 16);   n.sx = 2·x + 160      // projected at z = 128: 256/128 = 2
    y = cur.yc + ((prev.yc − cur.yc)·t >> 16);   n.sy = 100 − 2·y
    append n
if not cur.clip: append cur
fan: (v0, v1, v2), (v0, v2, v3), ...  new faces with the original's type, colours, texture and normal
```

The new vertices' u, v and shade fields are never written: they stay **0**, and a shade
of 0 is not 0xffff, so they are not lit. Every near-clipped textured face therefore gets u = v = 0 at the clip
points and, on Gouraud faces, level 0 (white through GOURAUD.PAL) there: smeared, bright texture where geometry
passes the near plane (P3D1: 3 to 6 such faces in the checked frames; PART2's landscape: 89, at the bottom of the
screen). The remake should decide whether to keep this; a proper clip (interpolated u, v and shade) is what the
code means. (The pool lies in the image, zero bytes of REVE.LFL, so these values are 0 on a PC too; so are the
six bytes at 0x2401f8 that the flat lighting reads, below.) The
new faces' sort key uses their first vertex's camera z, which is the copied (unclipped) z of `cur`.

### Lighting (0x21e72c flat, 0x21e7c8 per vertex)

A point light at L (the camera position, brought into object space through the negated angles), with distance
falloff. For a point p with normal n (words, length 32767):

```
d = L − p                         (words, 16-bit wrap)
dd = d.x² + d.y² + d.z²           (32-bit)
dot = Σ (d_i · n_i) >> 16         (each the high word of a 16×16 product; summed as a word) ≈ (d · n̂) / 2
if dot <= 0: level = 15
else:
    q = trunc((dot² << 10) / dd) & 0xffff            ≈ 256·cos²θ
    al = sqrt8(q) − sqrt8(dd >> D)                   ≈ 16·cosθ − |d| / 2^(D/2)
    vertex (D = 20):  al < 0 → 15;  al > 15 → 0;   else level = 15 − al
    flat   (D = 18):  al < 0 or al > 15 → 15;      else level = 15 − al
sqrt8(i) = the byte table 0x219ec8 = isqrt(i) for i < 1156 (filled by 0x2175bc; beyond it, other data: reached
only for |d| ≥ 34816 (vertex) / 17408 (flat), never in the parts)
```

- **Per vertex** (Gouraud faces): p = the vertex (object space), n = its vertex normal; the level (0..15) is cached
  in the vertex for the render (`shade` = 0xffff until lit; lit when a face using it is drawn, which does not change
  the result).
- **Flat** (types 0, 1): `esi` points at the polygon buffer 0x2401f8 instead of a vertex, whose first 6 bytes
  nothing ever writes: **p = (0, 0, 0)**, the object's origin, for every face; n = the face normal. The colour is
  `face.colour + level` (written to face byte 2). So all faces of an object with the same orientation get the same
  shade, and the shade depends on the direction from the object origin to the camera.
- `al > 15` cannot happen (dot < |d|/2 so q < 256): the flat routine's "> 15 → darkest" is never reached.
- **L** = R(−angles)·(cam' − pos), cam' = −2·h: the code applies the rotation of the negated angles,
  Ry(−b)·Rz(−c)·Rx(−a), not the inverse Rx(−a)·Rz(−c)·Ry(−b). The two differ when an object turns about more than
  one axis (PART2's objects); keep the original's formula, the lighting shows it.
- Continuous form for the remake: `level = clamp(15 − 16·cosθ + |d|/1024, 0, 15)` per vertex (`|d|/512` and the
  face normal at the origin for flat faces), cosθ ≤ 0 → 15, interpolated across the face, looked up as below.

### Face types (jump table 0x21ec24) and the polygon routines

| type | face handler | polygon routine | colour of a pixel |
|---|---|---|---|
| 0, 1 | 0x21ec5c: flat lighting | 0x21bce0 → 0x21bd4c flat n-gon, span 0x21c05c | `colour + level` (PART4: 80..95, a white → beige → red → black ramp) |
| 2..5 | 0x21ecac: vertex lighting | 2: 0x21bcfc Gouraud (span 0x21c50c, from the colour byte); 3..5: 0x21bce0, flat in the colour byte | not used by any file |
| 6, 7 | 0x21ed2c: texture | 0x21c6b4 texture, span 0x21cb3c | `TEX[texel]` |
| 8 | 0x21ed9c: vertex lighting + texture | 0x21cc28 texture + shade, span 0x21d0fc | `GOURAUD[level·256 + TEX[texel]]` |
| 9..11 | as 8 | 0x21bce0 with the low byte of the face address as colour | not used |

Texture: face byte 3 = `t` selects the origin `(col, row) = (16·(t & 15), 16·(t >> 4))` in the 256 × 256 texture
buffer [0x21b140] (TEXTURE.RAW's pixels; 0x21b144 = [0x21b140] + row·256 + col), and the mask 0x3f3f wraps u and v
to 64 texels: `texel = TEX[(row + (v & 63))·256 + col + (u & 63)]`. All `t` in the files are multiples of 4 in
both nibbles: the 16 aligned 64 × 64 tiles of TEXTURE.RAW.

### The rasterizers

All three share one structure (0x21bd4c flat, 0x21c6b4 texture, 0x21cc28 texture + shade). Polygon vertices: integer
`sx, sy`, and `u, v` (unsigned words, 8.8 texels), `s` (shade level word).

**Screen clipping.** Bounding box of the polygon; nothing is drawn if `ymin >= ymax` (zero height) or the box is
outside [0, 319] × [0, 199]. Otherwise, in this order, the first applicable clip runs and the tests restart with its
output: `xmin < 0` → clip at x = 0 (0x21b14c, keep x ≥ 0); `xmax > 320` → clip at x = 320 (0x21b42c, keep x ≤ 320);
`ymin < 0` → clip at y = 0 (0x21b70c); `ymax > 199` → clip at y = 199 (0x21b9ec, keep y ≤ 199). Each clipper walks
the edges (prev → cur, starting with prev = the last vertex) Sutherland–Hodgman style; an intersection is always
interpolated from `prev`, with `num = bound − prev.x`, `den = cur.x − prev.x` (for x; swap x and y for y):

```
y = ((prev.y << 16) + trunc(((cur.y − prev.y)·num << 16) / den)) >> 16       (32-bit; word)
s = the same with the shade words                                           (shade flag)
u = prev.u + ((trunc(((cur.u − prev.u) << 8) / den16) · num16) >> 8)        (unsigned words, 32-bit; word)
```

**Fan.** The textured routines draw a clipped polygon as triangles (0, 1, 2), (0, 2, 3), ... ; the flat routine
draws the whole convex polygon at once. A triangle with all three y equal is skipped.

**Edge walk.** Top vertex = the first with the smallest y. The left edge runs to the next vertices (index + 1), the
right edge to the previous ones (front faces are counter-clockwise). `y` = the top's y.

```
new edge from A to B (skip edges with dy = 0; dy < 0 ends the polygon):
    x  = (A.x << 16) + 0x8000;   dx = trunc(((B.x − A.x) << 16) / dy) | 1
    left only:  U = A.u << 8, dU = trunc(((B.u << 8) − U) / dy); V likewise; S = A.s << 16, dS = trunc(((B.s − A.s) << 16) / dy)
    right only: dU_r, dV_r, dS_r = the same slopes (its U, V, S values are not kept)
segment: cnt = min(yL_end, yR_end) − y;  the edge(s) ending there are replaced next
    per segment gradients: G = trunc(((d_r − d_l) << 16) / (dx_r − dx_l or 0x7fff))   for U, V, S  (64-bit dividend)
    draw rows y .. y + cnt (cnt + 1 rows); step the edges after each row but the last
    y += cnt          // the vertex row is drawn again by the next segment, with the new edge
```

So rows `y_top .. y_bottom` are drawn inclusive, rows through a middle vertex twice (the second time with the new
edge), and pixels `x = (xL >> 16) .. (xR >> 16) − 1` (the right end exclusive). The per-segment gradient from the
edge slopes is the triangle's affine gradient (exact for triangles up to rounding); it can overflow the 32-bit
quotient (#DE) only for degenerate slivers, which never happened in the checked frames.

**Spans.** Per row, with `c = (−xL) & 0xffff` (the distance from xL to the next integer, 16-bit fraction):

```
flat (0x21c05c):     fill [xL >> 16, xR >> 16) with the colour
texture (0x21cb3c):  u = U + (Gu·c >> 16), v = V + (Gv·c >> 16)            // evaluated at ceil(xL), drawn from floor(xL)
                     per pixel: texel at ((v >> 16) & 63, (u >> 16) & 63); u += Gu; v += Gv   (8.16, mod 2^24)
texture + shade (0x21d0fc): u, v as above; the shade has no prestep: s = S;
                     pixel = GOURAUD[((s >> 16) & 0xff)·256 + texel]; s += Gs
                     (the shade's integer part shares a register with v's fraction: its carries can add 2^-16
                     to v on spans longer than ~256 pixels with a falling shade; reproduced by the model)
```

The texture mapping is **affine in screen space** (no perspective), u, v and the shade linear in x and y.

### PART4's bounding boxes (0x25f26e, 0x25f139, 0x21d22c)

With [0x2174c6] = 1, after all objects are drawn: for every object (drawn or not) its 8 box corners are transformed
with C (rotated this time) and `T + P`, `>> 15`, and projected like vertices when z > 128 (a corner at z ≤ 128 keeps
the screen position it had: from the cull test or an earlier frame, so edges to such corners are lines to stale
points, a dependence on history that shows as long stray lines when a box reaches behind the camera; the remake
clips the box edges at the near plane instead). If the **last** corner (7) is in front, the 12 edges (0-1, 2-3, 1-3, 0-2, 4-5, 6-7,
5-7, 4-6, 0-4, 1-5, 2-6, 3-7) are drawn in **colour 0x80** (palette 48, 63, 63: pale cyan) by 0x21d22c: clip the
end points to x 0..319, y 0..199 (moving an end along the line with 16.16 slopes, restarting after each move; a
line with both ends beyond the same side is dropped), then a Bresenham walk of max(|dx|, |dy|) steps plotting the
start point and not the end point.

## The full-screen passes

### 0x2045fa: the AA pass (`GFX\ANTIALIA.PAL` at 0x20643c)

`T[a][b]` = byte `a·256 + b` of the 64 KB table (loaded by every 3D entry's init). From the back buffer `s` into `d`
(the screen, or [0x205e88] in the INTRO), for rows 1..198 and columns 4..315:

```
even x:  d(x,y) = T[ T[s(x,y−1)][s(x,y+1)] ][ T[s(x,y)][s(x−1,y)] ]
odd x:   d(x,y) = T[ T[s(x,y+1)][s(x,y−1)] ][ T[s(x−1,y)][s(x,y)] ]
```

then it clears `s` (64,000 bytes). Rows 0 and 199 and columns 0..3 and 316..319 of `d` are never written: they stay
black (entries 8, 14 and 22 clear the screen in their init; the INTRO's [0x205e88] is never written there). The table is symmetric, so the parity only matters where it
is not: `d ≈ (up + down + centre + left) / 4`. In colour that is a 4-tap blur with weights ¼ at (0, −1), (0, +1),
(0, 0), (−1, 0): horizontally a shift of the image by +¼ pixel to the right with variance 3/16 px², vertically no
shift with variance ½ px² (units: original pixels).

The table is a rough average: over the colours the textures use, `T[a][b]` is the nearest palette colour to the
average of a and b for only about half of the pairs; its colour is on average 6.7 (90th percentile 10.9, at most
21.5) 6-bit units away from the true average, and `T[a][a] ≠ a` for 71 of the 173 colours (a colour maps to a
neighbour of the same shade). (0x25a275, which the 2D parts use, computes a table at the same address from the
current palette: `T[b][a]` = the first nearest colour to `(3·a + b) >> 2` per 6-bit channel — a 3:1 blend; the 3D
parts load the file instead.)

### 0x2046c7: FIN's blend

For every pixel: `screen = T[back][screen]` (≈ (new + old) / 2), then the back buffer is cleared. Applied once per
drawn frame: an exponential trail that halves per frame (per refresh in the capture: FIN draws a frame every refresh).

## Tables and pictures (all read at run time)

| what | where | format |
|---|---|---|
| sine | image 0x2176c8 | 5120 int16, 4096 per turn, cosine at +1024 entries |
| square-root bytes | 0x219ec8, generated | isqrt(i), i < 1156 (0x2175bc writes value n (2n+1) times while below 0x21a30a) |
| squares for 0x2175dc | image 0x21a478 | bytes 0, 1, 4, 9, ... (only through the routine) |
| row offsets | image 0x21a980 | 200 dwords y·320 |
| shade table | `GFX\GOURAUD.PAL` → 0x21643c | 16 levels × 256 indices |
| blend table | `GFX\ANTIALIA.PAL` → 0x20643c | 256 × 256 indices |
| textures | `GFX\TEXTURE.RAW` → [0x21b140] | 256 × 256 pixels, 16 tiles of 64 × 64; its palette is the palette |
| worlds, animations | `3D\*.WLD`, `3D\*.ANI` | above |

GOURAUD.PAL: row k = level k (0 brightest, 15 darkest) of every index. **Level 11 is the index itself** for the
texture colours; levels 1..10 brighten and saturate toward white, level 0 is white for most colours, 12..15
darken. Fitting the nearest palette colour to `clamp(colour · f)` per level explains 94–100 % of the used colours
for levels 1..11 with f ≈ 5.00, 4.61, 4.22, 3.86, 3.48, 3.09, 2.72, 2.32, 1.93, 1.55, 1.00 and 84–90 % for 12..15
with f ≈ 0.86, 0.77, 0.58, 0.28 (`fit_tables.py`); the misses are hue jumps of the 8-bit quantisation (a dark purple
becomes a pink at the bright levels). With the lighting formula (level ≈ 15 − 16·cosθ) a texel shows its own colour
at cosθ ≈ ¼; surfaces facing the camera are blown out to white: the chrome look of the surfer.

## What the remake should evaluate exactly instead

- **Transforms**: floats instead of 1.15 words and the sine table (angles θ = value·2π/8192); the stepped state is
  exactly piecewise linear in n: evaluate key 0 + the deltas at a fractional n (between refreshes) for smooth motion.
  Keep the camera position rounded to even (−2·h) or not: the difference is one world unit.
- **Projection**: float screen coordinates (the original truncates vertices to whole pixels: they jitter).
- **Near plane**: z = 128 world units; clip properly (or keep the u = v = 0 / level-0 artefact: a decision).
- **Texture mapping**: perspective-correct (the original is affine per triangle), texel coordinates = the unsigned
  words / 256 with 64-texel wrap inside the tile, filtered **in colour** (look up each tap's colour, blend).
- **Shading**: the light level as the continuous formula above, interpolated (perspective-correct) across the face,
  then the colour = blend of `GOURAUD[floor(level)][texel]` and `GOURAUD[floor(level)+1][texel]` in colour (or the
  fitted factors applied to the texel's colour, which loses the table's hue jumps). The original interpolates the
  level linearly in screen space without subpixel prestep and truncates it to 16 steps.
- **Flat faces**: `colour + level` with a continuous level, blending the two neighbouring palette entries of the ramp.
- **Painter's order**: objects sorted by origin depth and drawn whole, faces by first-vertex depth within an
  object. Where this order is right a depth buffer gives the same picture; where it is wrong (objects that
  interpenetrate or overlap with origins in the other order, long faces whose first vertex is not representative)
  the original shows the error. I did not measure how often that happens; a depth buffer is the exact evaluation of
  the geometry, keeping the order keeps its errors. The culling rules (far cull, box tests) stay as they are either
  way.
- **Edges**: anti-aliased instead of the fill rule above.
- **AA pass**: the blur it approximates, in colour, at the output resolution: weights ¼ at the four taps in original
  pixel units (or the Gaussian of the same variances, σx² = 3/16, σy² = ½ original px², with or without the ¼-pixel
  shift); keep the black 4-pixel side and 1-pixel top and bottom borders (they are visible).
- **FIN's blend**: (new + old)/2 per refresh in colour, evaluated for seeking as Σ_k 2^−(k+1)·frame(r − k) over the
  last ~12 refreshes (2^−12 ≈ 0.0002), starting from black at FIN's first refresh.

## Things that do not matter

- Overflow paths (0x21e61c, the corner transform, the 16-bit camera coordinates, the sqrt table's end, the gradient
  #DE) are not reached in the parts.
- Face types 2..5, 7, 9..11 and the Gouraud-only rasterizer 0x21c0ec / 0x21c50c are not used by the files.

## Verification

`models/engine/` (README there). All with the Python model `engine.py`:

| check | what | result |
|---|---|---|
| `test_render.py` | 37 renders from memory dumps at the render's start (0x25f5b5) and end (0x25f65b), 6 parts | back buffer 0 pixels different; all transformed vertices of drawn objects equal |
| `test_anim.py` | ANI model vs camera and object state in 36 dumps and 10 snapshots | equal at n = r − r0 (INTRO modulo its 626-refresh loop; P3D1 one behind after the capture's lost interrupt) |
| `test_frame.py` | dump state → render → 2D passes vs `full.frames` | 36 captured screens equal, 1 torn |
| `model_part.py` | archive files + refresh number only, 213 records sampled across entries 1–4, 8, 11, 14, 22, 26 | 191 exact, 17 torn, 2 split states, 3 differ only in stale box lines (`notes-parts3d.md`) |
| `check_isqrt.py` | 0x2175dc in unicorn vs floor(sqrt) | equal on 100,023 values incl. the 32-bit extremes |

## Open

- Whether the INTRO's small camera jump at each loop restart is visible (the loop is otherwise seamless).
- How often the painter's order differs visibly from a depth buffer (not measured).
- Which records of the capture are torn or split depends on the run's timing; it is not part of the demo.
