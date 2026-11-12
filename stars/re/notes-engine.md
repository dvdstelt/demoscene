# Scope "engine": BARTi's 3D engine (objects, transform, culling, sorting, env / bump / texture fillers)

All addresses are offsets in `unpacked/stars_img.bin` (= near pointers = linear addresses in the emulator). The
engine is integer-only: "x.10" = fixed point with 1024 = 1.0, "8.8" = 256 = 1.0, "16.16" = 65536 = 1.0. Angles are
12-bit (4096 = one turn). Screen coordinates are pixels of the 320x200 buffer (shown at 4:3).
Code: the engine proper is 0x7871c..0x7a20c; it calls the scanline fillers at 0x4d2ee..0x55835 (same author, same
variables at 0x4d190..0x4d26c). 0x79fad..0x7a20c (the "dissolve" copy routines) belong to parts2d-b.

What was checked (scripts in `models/engine/`, section 13):
- **Renderer**: `engine.py`, an integer port of 0x79483 / 0x789c4 and the five fillers the parts use, reproduces
  the screen buffer **byte for byte (0 differing pixels)** in 27 draw calls dumped from the emulator: spike ball
  (mode 15, 4 frames), textured surfaces (mode 15 with morphing, 4), bee (mode 3, 68 WLDD objects, 6), chrome star
  (mode 3, direct calls, 3), torso (modes 2, 20, 21 incl. the 256-wide target, 10). For the spike ball, the
  textured surfaces and the bee the modelled buffer (rows 1..197) is also identical to the `full.frames` record that
  showed it (10 frames checked, 63040 of 63040 pixels each).
- **Environment maps**: 0x7873a regenerated for the three parts that build one: identical (65536 of 65536 bytes).
- **Cosine table** 0x5599c = `trunc(1023 * cos(2 pi i / 4096))` for all 4096 entries.

-----------------------------------------------------------------------------------------------------------------

## 1. What the engine draws, and who uses it

Triangle meshes (`B!` objects, or a `WLDD` group of them) with per-vertex normals, transformed with 3x3 integer
matrices, projected with a fixed eye distance, back-face culled on the screen, sorted back to front (painter's
algorithm, no depth buffer), and filled with affine scanline fillers that look up 256x256 byte maps: an
environment ("phong") map indexed by the rotated vertex normal, optionally offset by a bump table and added to a
texture. Every pixel is a plain byte store into a 320x200 (or 256x256) buffer: no transparency, no z test.

| part (scope) | draw call | mode [0x55948] | F [0x57ab6] | shift [0x5594c] | face sort | objects | maps |
|---|---|---|---|---|---|---|---|
| spike ball 0x6534 (3d-a) | 0x79483, list 0x6508 | 15 | 80 | 1 | yes | 0x6498: file 18 (685 v, 1366 f) | env 0x7873a dl 0, table 0x6310, then 0 → 8 (0x671c); texture = file 16 (material table 0x6520); bump = file 17, each byte b → (b − 8) * 2 (0x6733) |
| bee 0x11e7f (3d-a) | 0x79483, list 0x11b66 | 3 | 30 | 1 | no ([0x582ac] = 1) | the 68 objects of file 28 (WLDD, 3012 v, 5595 f) | env = file 29 (selector [0x11b2a] put into [0x55918] around the call) |
| textured surfaces 0xa0ca (3d-b) | 0x79483, list 0xa036 | 15 | 100 | 3 | yes | 0x9f80: file 24 (529 v, 968 f, 10 morph frames) | env 0x7873a dl 4, table 0x9df0; texture and bump made by the part (0xa5c5; bump byte b → (b − 4) * 4) |
| chrome star 0x164dc (3d-b) | 0x789c4 direct (ebp = 0x16360, …) | 3 | 100: not set by the part, left by the textured surfaces (0xa2f1) | 1 (0x1656e) | yes | file 51 (394 v, 784 f, 3 morph frames) | the map in [0x55912] put into [0x55918] around the call |
| precalc 0x16945 (3d-a) | 0x79483 (0x16a60) | 3 | 20 (0x16a4c) | 3 (0x169e2) | yes | file 50 (158 v, 316 f), drawn into the part's own buffers ([0x558cc] changed, 0x16a2a..0x16b1e) | see notes-parts3d-a |
| torso and ring 0x16f7c (3d-b) | 0x789c4 direct | 2 (0x16ca8: file 45, 192 v, 384 f); 20 (0x16cc8) and 21 (0x16ce8): file 46 (242 v, 459 f) | 300 | 1 | yes | see mode column | env 0x7873a dl 2, table 0x16c20, then + 0x20 per byte (0x16f1a); mode 21 draws into a 256x256 buffer (the part swaps [0x558d0]); 130 faces of file 46 have type 1 at run time (0 in the file) |

The parts' own set-up (motion, palettes, which tables) is in notes-parts3d-a / -b; this file describes what the
engine does with it.

## 2. Data

### 2.1 `B!` object files (files 18, 24, 45, 46, 50, 51; also each object inside `WLDD`)

File 46 (loaded twice by the torso set-up, NOTES.md lists it as a table) is a `B!` object too. Its vertices 0..7
are the corners of its bounding box (±529, ±648, 516 / −542) with meaningless normals and are used by no face: they
only enlarge the radius 0x79808 would compute.

| offset | type | meaning |
|---|---|---|
| +0 | char[3] | `B!` 0x1a |
| +3 | byte | 0x0f = static mesh; 0x00 = morphing mesh (vertex frames follow, 2.2). Only tested for == 0 |
| +4 | u16 | nv, number of vertices (≤ 2000: the engine's buffers) |
| +6 | u16 | nf, number of faces (≤ 2000) |
| +8 | vertex[nv] | 16 bytes each, below |
| +8 + 16 nv | face[nf] | 16 bytes each, below |
| +8 + 16 (nv + nf) | frame[] | morphing meshes only: complete vertex arrays (16 nv bytes each); file 24: 10 frames, file 51: 3 |

The engine keeps `mesh = file + 4` (the pointer at object +0x12 points at nv).

Vertex (16 bytes): `int16 x, y, z` (model units; the files use about ±1600); `int16 nx, ny, nz` (unit normal times
255: lengths 253.5..255.0 in all files); `u8 u, v` (+0xc, +0xd: texture coordinates in texels of a 256x256 map,
read as one word by the modes with a texture; 0 for objects that have none); `u16` +0xe (0 everywhere, unused).

Face (16 bytes): `u16 type` (+0: material index for modes 4/5/10/11/12/13/22/23; for modes 20/21 `1` = plain
environment map, anything else = bump-mapped; 0 in all files, see 7.4); `u16 a, b, c` (+2, +4, +6: vertex indices);
`int16 nx, ny, nz` (+8: face normal * 255, used only by the face-normal modes 0/10/11/24/25 — none of the parts);
`u16` +0xe (0, unused).

Coordinate system: x right, y down, z towards the viewer (the eye looks along −z, section 4.3). Front faces are the
ones that appear clockwise on the screen (y down): vertex order a, b, c with
`(xb − xa)(yc − ya) − (xc − xa)(yb − ya) > 0`.

### 2.2 Morphing meshes and the animation record

0x78c62 (load) converts the frame list of the animation record that the part provides at object +0x16:

| offset | type | meaning |
|---|---|---|
| +0 | int | key: index into the frame list (0x78fda steps it) |
| +4 | int | step within the key, 0 .. steps − 1 |
| +8 | byte | shift of the blend (6 in the textured part) |
| +9 | byte | steps per key (63 in the textured part) |
| +0xa | int[] | frame list, −1 terminated: in the exe image frame *indices* (0 = the first frame after the base mesh), replaced by pointers at load: `mesh + 4 + 16 (nv + nf) + index * 16 nv` |

The blend weights are a table of 128 words at 0x782d0, copied there from the part's data by 0x79841 (esi = source,
256 bytes; the textured part's table 0x9e80 is w[i] = i).

```c
void anim_tick(Object *o /*esi*/, int keys /*eax*/)          // 0x78fda, called from the parts' PIT callbacks
{
    Anim *a = o->anim;
    if (++a->step == a->steps_per_key) {                     // byte compare
        a->step = 0;
        if (++a->key == keys) a->key = 0;
    }
}

void anim_blend(Object *o /*esi*/)                           // 0x78ffe, called once per drawn frame (after the draw)
{
    Vertex *out = (Vertex *)(o->mesh + 4);                   // the base mesh is overwritten
    Anim *a = o->anim;
    int16 w = blend_weights[a->step];                        // words at 0x782d0
    int16 *p = a->frames[a->key], *q = a->frames[a->key + 1];   // so the list holds keys + 1 pointers
    for (i = 0; i < nv; i++, p += 8, q += 8, out++)
        for (k = 0; k < 6; k++)                              // x, y, z, nx, ny, nz; u, v and +0xe are kept
            out->w[k] = p[k] + (int16)(((int32)(int16)(q[k] - p[k]) * w) >> a->shift);   // 16-bit results
}
```
The blended normals are not renormalised (shorter between keys). Time base: the key and step advance per PIT tick
(70.064 Hz) in the part's callback; the mesh is blended once per drawn frame from the state at that moment, for the
next frame.

### 2.3 `WLDD` (file 28, the bee)

| offset | size | contents |
|---|---|---|
| 0 | 8 | `WLDD` 0x00 `   `; the program reads the object count as the word at +3 = 0x0044 = 68 (the last `D` and the 0) |
| 8 | 68 × 0x20 | object records: the object struct of 2.4 (0x1e bytes) + 2 bytes padding; +0x12 = offset of the object's `B!` header from the file start |
| 0x888.. | | 68 complete `B!` files (static, byte 3 = 0x0f), each starting at its record's offset |

The bee part (0x11e43..0x11e6b) adds `file + 4` to every +0x12 (→ the mesh) and appends all 68 records to its
scene list. Angles, animation pointer and radius are 0 in the file (0x7916b computes the radius).

### 2.4 Object struct (0x1e bytes)

| offset | type | meaning |
|---|---|---|
| +0x00 | int[3] | position x, y, z. In a scene list: world units (same scale as the vertices). For a direct 0x789c4 call: camera-space x.10 (vertex units * 1024) |
| +0x0c | int16[3] | angles a, b, c (12-bit, only `& 0xfff` used) |
| +0x12 | ptr | mesh (file + 4) |
| +0x16 | ptr | animation record (2.2), morphing meshes only |
| +0x1a | int | bounding radius (0x79808, scene lists only) |

### 2.5 Scene list (0x79111 .. 0x79184)

`{ Object **objects; Work *work; int capacity; int used_bytes; }` — `objects` is −1 terminated, `work` holds 16
bytes per object (camera-space centre X, Y, Z in x.10, visibility flag). 0x79111 (esi = list, capacity at +8):
allocates both arrays and fills `objects` with −1; 0x7916b (esi = list, eax = object): 0x79808 radius, append;
0x7915b: free; 0x79148 (reset) and 0x79184 (remove last) have no callers.

Radius 0x79808: `r = max over vertices of isqrt(x² + y² + z²)` with the table square root 0x79cbf (8.2).

### 2.6 Material table (0x79801: [0x582a6] = esi)

16 bytes per material: `+0 ptr` (unused by the engine), `+4 u16` selector of a 256x256 texture. Mode 15 always uses
material 0 (0x4dc1f clears the index); the per-face materials of modes 4/5/10..13/22/23 are unused.

### 2.7 Engine state and buffers

Static areas in the image (zero in the file):

| address | size | contents |
|---|---|---|
| 0x582d0 | 0x10000 | environment map ([0x55914], selector [0x55918]) |
| 0x682d0 | 0x10000 | screen buffer 320x200 ([0x558cc], selector [0x558d0]); during set-up also the square-root table ([0x558de], 0x7871c), so parts build it before anything is drawn |
| 0x782d0 | 0x100 | blend weights (0x79841) |
| 0x5599c | 0x2000 | cosine table, 4096 int16 = trunc(1023 cos(2 pi i / 4096)) (static data; 0x79da0 would build a slightly different one by recurrence and is never called) |
| 0x784d1 | 28 × 0x15 | the mode table (6) |

Allocated by 0x787fc through the buffer list 0x5594d (9-byte entries: variable, size, make a selector):

| variable | size | contents |
|---|---|---|
| [0x558f0] (selector [0x558f4]) | 0x1f40 | face list: 2000 dwords `face index << 16 | depth key` |
| [0x5591a] (selector [0x5591e]) | 0x7d0 | object list of 0x79483: 500 dwords `object index << 16 | depth key` |
| [0x558e4] | 0x1f40 | projected vertices: int16 sx, sy |
| [0x558ea] | 0x7d00 | rotated vertices: int32 X, Y, Z (x.10), 16 bytes each |
| [0x558fc] | 0x7d00 | rotated vertex normals (int32 × 3, 16 bytes each) |
| [0x55902] | 0x7d00 | rotated face normals |
| [0x55908] | 0x1f40 | per-vertex filler parameters (section 7.1) |
| [0x558f6] | 0x7d0 | allocated, unused |

Globals: camera struct 0x55836 (int x, y, z; int16 a, b, c at 0x55842); environment angle offsets int16[3] 0x55862;
normal-matrix struct 0x55876 (position unused, angles 0x55882 = object angles + 0x55862); matrices (4 rows × 4
int32, 16-byte rows, row 3 = translation) camera 0x5799e, combined 0x579de, object 0x57a1e, normal 0x57a5e; F
[0x57ab6]; F2 [0x57aba] (alternative projection); projection shift byte [0x5594c]; mode [0x55948]; no-sort flag
byte [0x582ac]; alternative-projection flag byte [0x5799c]; "drawing a list" byte [0x582cd]; texture-uv shift
byte [0x582cf] (0 in all parts); env-map dither flag byte [0x582ce]; texture selector [0x55912]; bump table [0x558d8]
/ selector [0x558dc]; background picture [0x558d2]; clear colour byte [0x5799d]; loaded-file list 0x57ac2 (500
dwords, [0x57abe] = bytes used); the current object struct 0x582ad (filled by 0x79483).

## 3. Call sequence (what a part does)

```
set-up:   0x787fc  init: flags / camera / offsets = 0, screen and env map pointers + selectors, buffers, file list
          0x78c62  load object: eax = file, esi = object struct (records the block, sets +0x12, converts +0x16)
          0x79841  blend weights (esi = 256 bytes)          0x79801  material table (esi)
          0x7873a  env map: dl = mode, ebp = 256-byte colour table (8.1)
          [0x5594c] = shift, [0x57ab6] = F, [0x582ac], [0x558d8]/[0x558dc] bump, [0x558d2] background
          0x79111  scene list; 0x7871c sqrt table; 0x7916b add object(s)
per frame: camera 0x55836 / 0x55842, offsets 0x55862, object positions / angles; [0x55948] = mode
          0x78905  present the previous frame and clear (9)
          0x79483  draw a scene list (esi = list)   or   0x789c4 draw one object (ebp = object, [0x582cd] = 0)
          0x78ffe  blend the morphing mesh for the next frame
per tick: 0x78fda  morph key / step (in the PIT callback)
end:      0x7915b  free the list; 0x788dd free the objects (0x78d26), the buffers and the two selectors
```
0x78cc4 loads an object with the DOS file loader 0x4d156 instead of the overlay (no callers).

## 4. Transform and projection

### 4.1 Rotation matrix 0x7984e (esi = struct {int x, y, z; int16 a, b, c}, edi = matrix)

```c
#define COS(t) cos_table[(t) & 0xfff]                 // 0x5599c, 1023 = 1.0
#define SIN(t) cos_table[((t) - 0x400) & 0xfff]
#define MUL(p, q) (int32)(((int64)(p) * (q)) >> 10)   // imul + shrd 10 (floor)
ca = COS(a); sa = SIN(a); cb = COS(b); sb = SIN(b); cc = COS(c); sc = SIN(c);
m[0][0] = MUL(cc, cb);   m[0][1] = MUL(-sc, cb);   m[0][2] = sb;
m[1][0] = MUL(sc, ca) + MUL(MUL(sb, cc), sa);      m[1][1] = MUL(cc, ca) - MUL(MUL(sa, sb), sc);   m[1][2] = MUL(-sa, cb);
m[2][0] = MUL(sc, sa) - MUL(MUL(sb, cc), ca);      m[2][1] = MUL(cc, sa) + MUL(MUL(ca, sb), sc);   m[2][2] = MUL(ca, cb);
m[3][0..2] = x, y, z;                                // column 3 and m[3][3] are not written
```
As a column-vector matrix this is Rx(a)·Ry(b)·Rz(c); the engine applies it to row vectors (`v' = v · M`, below),
i.e. `v' = (Rx(a) Ry(b) Rz(c))^T v + t`. The table's 1023 instead of 1024 scales every matrix by
(1023/1024)^1..3 (the objects are 0.1–0.3 % smaller, slightly anisotropic): a shortcut to evaluate exactly.

### 4.2 Per-object set-up 0x78a35 (first entry of every mode row; ebp = object)

```c
mesh = obj->mesh;  nv = mesh[0]; nf = mesh[1];  verts = mesh + 4;  faces = verts + 16*nv;
transform_fn = (alt_proj == 1) ? 0x79df3 : 0x79b16;          // [0x5799c]
normal_angles = obj->angles + env_offset;                     // 0x55882 = obj +0xc.. + 0x55862.. (16-bit adds)
OBJ  = rotation(obj->pos, obj->angles);                       // 0x57a1e
NORM = rotation(0x55876 struct, normal_angles);               // 0x57a5e: only its 3x3 part is used
COMB = OBJ x CAM;   // 0x79d25: out[i][j] = sum_k MUL(OBJ[i][k], CAM[k][j]) over all 4x4 entries
M = drawing_list /*[0x582cd]*/ ? COMB : OBJ;
```
The normals are rotated by the object's own angles plus the offsets 0x55862, never by the camera: the env map
follows the object, and the parts move the highlights by changing 0x55862 (bee: 0x12296.., spike ball: 0x690c).
0x79483 zeroes the camera matrix's row 3 (0x579ce..0x579da) before its objects, so COMB's translation is the
object's relative position rotated by the camera.

### 4.3 Vertices 0x79b16 (patched copy of the matrix into the code)

```c
for (i = 0; i < nv; i++) {
    x = verts[i].x; y = verts[i].y; z = verts[i].z;                       // int16
    X = x*M[0][0] + y*M[1][0] + M[3][0] + z*M[2][0];                     // int32 wrap, x.10
    Y = x*M[0][1] + y*M[1][1] + M[3][1] + z*M[2][1];
    Z = x*M[0][2] + y*M[1][2] + M[3][2] + z*M[2][2];
    rot[i] = (X, Y, Z);
    D = (F - (Z >> 10)) << shift;                                        // F = [0x57ab6], shift = [0x5594c]
    if (D <= 0) { sx = X >> 10; sy = Y >> 10; }                          // not projected (garbage on screen)
    else        { sx = (int16)(X / D + 160); sy = (int16)(Y / D + 100); } // idiv: truncation toward zero
    proj[i] = (sx, sy);
}
```
So with z = Z/1024 etc.: `sx = 160 + (1024 / 2^shift) * x / (F - z)`, `sy = 100 + (1024 / 2^shift) * y / (F - z)`:
the eye is at z = +F looking along −z, the focal length is 512 pixels (shift 1) or 128 (shift 3) in the 320-wide
buffer, the same in y (no aspect correction: on the 4:3 screen the picture is stretched vertically by 1.2 like
everything in mode 13h). Horizontal field of view 34.7° (shift 1) / 102.7° (shift 3). Screen positions are
integers (no sub-pixel precision).

The alternative 0x79df3 ([0x5799c] = 1, never set: 0x787fc clears it) uses `D = F2 - (Z >> 10)` without the shift
and `sx = (X >> 1) / D + 160` (the same as shift 1 with F2).

### 4.4 Normals 0x79a17

```c
// 0x78a15 (vertex normals, the parts' modes):  src = verts + 6, n = nv, out = [0x558fc]
// 0x789f5 (face normals):                     src = faces + 8, n = nf, out = [0x55902]
for (i = 0; i < n; i++) {
    (nx, ny, nz) = src[i];                                               // int16, length ~255
    out[i] = (nx*NORM[0][0] + ny*NORM[1][0] + nz*NORM[2][0],            // no shift: length ~255*1023 = 2^18
              nx*NORM[0][1] + ny*NORM[1][1] + nz*NORM[2][1],
              nx*NORM[0][2] + ny*NORM[1][2] + nz*NORM[2][2]);
}
```

## 5. Scene lists: 0x79483

```c
void draw_list(List *l /*esi*/)
{
    drawing_list = 0;
    CAM = rotation(camera.pos, camera.angles);                 // 0x5799e (its translation row is not used)
    for (i = 0; l->objects[i] != -1; i++) {
        Object *o = l->objects[i];
        d = o->pos - camera.pos;                               // int32, world units
        X = d.x*CAM[0][0] + d.y*CAM[1][0] + d.z*CAM[2][0];     // x.10
        Y = d.x*CAM[0][1] + d.y*CAM[1][1] + d.z*CAM[2][1];
        Z = d.x*CAM[0][2] + d.y*CAM[1][2] + d.z*CAM[2][2];
        D = (F - (Z >> 10)) << shift;
        if (D <= 0) { sx = X >> 10; sy = Y >> 10; sr = o->radius; }
        else        { sx = X / D + 160; sy = Y / D + 100; sr = (o->radius << 10) / D; }   // int16 results
        visible = sx + sr > 0 && sx - sr < 320 && sy + sr > 0 && sy - sr < 200          // int16 compares
               && (Z >> 10) - o->radius < -F                   // the whole bounding sphere beyond z = -F
               && Z > -0x7d0000;                               // and the centre nearer than z = -8000
        l->work[i] = (X, Y, Z, visible ? 0 : 0xff);
    }
    n = 0;
    for each i with work flag 0:  key[n++] = i << 16 | (uint16)(work[i].Z >> 10);
    if (n == 0) return;
    if (n > 1) quicksort(key, n);                              // 0x7898d, ascending signed 16-bit key
    CAM[3][0..3] = 0;                                          // 0x579ce..0x579da
    drawing_list = 1;
    for (k = 0; k < n; k++) {                                  // farthest centre first
        Object *o = l->objects[key[k] >> 16];
        cur.pos = (o->pos - camera.pos) << 10;                 // 0x582ad: x.10, camera-relative
        cur.angles = o->angles; cur.mesh = o->mesh; cur.anim = o->anim; cur.radius = o->radius;
        draw_object(&cur);                                     // 0x789c4 with ebp = 0x582ad
    }
    drawing_list = 0;
}
```
An object is dropped as a whole as soon as its bounding sphere reaches z ≥ −F (camera space), i.e. comes closer than
2F to the eye, or when its projected bounding square is off screen. 0x79192 is the same routine for the alternative
projection (no callers).

## 6. One object: 0x789c4 and the mode table

```c
void draw_object(Object *o /*ebp*/)               // 0x789c4: five calls through the row of the current mode
{
    Row *r = (Row *)(0x784d1 + 0x15 * mode);     // 5 code pointers + 1 byte (mode + 1)
    r->setup();        // 0x78a35 (4.2) and transform_fn (4.3)
    r->normals();      // 0x789f5 face normals / 0x78a15 vertex normals / 0x789f4 none (4.4)
    r->faces();        // 0x78b08 (section 7.0)
    r->prep();         // per-vertex filler parameters (7.1)
    r->draw();         // per sorted face: corner parameters + filler (7.2 .. 7.4)
}
```

| mode | normals | prep | draw → filler | what (modes in **bold** are used) |
|---|---|---|---|---|
| 0 | face | — | 0x4d2ee → 0x4ef40 | flat, colour from the face normal's z and the face type |
| 1 | vertex | 0x4d3c5 (nZ >> 11, 0..127, through the byte table 0x4d26c) | 0x4d40b → 0x4e04c | Gouraud |
| **2** | vertex | 0x4d4e7 env (n >> 11) | 0x4d5af → 0x4e710 | environment map, radius 128 (bee-style, torso) |
| **3** | vertex | 0x4d4b4 env (n >> 12) | 0x4d5af → 0x4e710 | environment map, radius 64 (bee, star) |
| 4 / 5 | none | 0x4d51a / 0x4d53c texture uv | 0x4d64f → 0x4e710 | texture of the face's material |
| 6 / 7 | vertex | 0x4d566 / 0x4da28 | 0x4d700 → 0x4f47c | bump-mapped env |
| 8 / 9 | vertex | 0x4d4e7 / 0x4d4b4 | 0x534f3 → 0x5203a / 0x5345f → 0x5277b | (not analysed: env map read through ds) |
| 10 / 11 | face | 0x4d51a / 0x4d53c | 0x4d8e3 | (not analysed) |
| 12 / 13 | vertex | 0x4d566 / 0x4da28 | 0x4db26 → 0x50097 | texture of the face's material + env |
| 14 / **15** | vertex | 0x4d566 / 0x4da28 | 0x4dbd7 → 0x51593 | texture (material 0) + bump-mapped env (spike ball, textured surfaces) |
| 16 | vertex | 0x4d3a3 (−Z >> 10 per vertex) | 0x4d40b → 0x4e04c | (not analysed: depth as the shade) |
| 17 | vertex | 0x4d4b4 | 0x4dc8b → 0x52e96 | env map into a 256-wide buffer |
| 18 | vertex | 0x4d9d1 | 0x4dbd7 → 0x51593 | as 15, texture uv + [0x4d2ec] |
| 19 | vertex | 0x4da28 | 0x4dd2b → 0x54548 | bump env into a 256-wide buffer |
| **20** | vertex | 0x4da28 | 0x4ddbf → 0x4e710 / 0x4f47c | per face: type 1 plain env, else bump env (torso) |
| **21** | vertex | 0x4da28 | 0x4deb1 → 0x52e96 / 0x54548 | as 20 into a 256x256 buffer (torso) |
| 22 / 23 | vertex | 0x4da28 / 0x4d566 | 0x4da75 → 0x50af5 | texture shaded through a table (ds = [0x55924]) |
| 24 / 25 | face | 0x4d51a / 0x4d53c | 0x4d7f5 | (not analysed) |
| 26 / 27 | vertex | 0x4d4e7 / 0x4d4b4 | 0x4dfa3 | (not analysed) |

## 7. Faces, parameters and fillers

### 7.0 Visibility and painter's order: 0x78b08 and 0x7898d

```c
n = 0;
for (i = 0; i < nf; i++) {
    (a, b, c) = faces[i].v;
    z = (((rot[a].Z + rot[b].Z) >> 1) + rot[c].Z) >> 1;        // weights 1/4, 1/4, 1/2 (not the centroid)
    cross = (int16)(sx[b]-sx[a]) * (int16)(sy[c]-sy[a]) - (int16)(sx[c]-sx[a]) * (int16)(sy[b]-sy[a]);
    if (cross <= 0) continue;                                   // back face or zero area (screen, integers)
    if (z >= -(F << 10) || z <= -90000000) continue;            // near: z < -F; far: z > -87890.6 (units)
    list[n++] = i << 16 | (uint16)(z >> 10);                    // (z << 6) >> 16: key = z in units, 16 bits
}
count = n;                                                      // [0x55928]
if (no_sort /*[0x582ac]*/ != 1 && n > 1 && !(no_sort & 1)) quicksort(list, n);

void quicksort(uint32 *a, int lo, int hi)        // 0x7898d (byte offsets; called with 0 .. (n-1)*4)
{
    while (hi > lo) {
        p = lo; pivot = a[lo];
        for (i = lo + 1; i <= hi; i++)           // signed 16-bit keys (the low words)
            if ((int16)pivot > (int16)a[i]) { a[p] = a[i]; p++; a[i] = a[p]; a[p] = pivot; }
        quicksort(a, lo, p);
        lo = p + 1;
    }
}
```
Faces are then drawn in list order: ascending key = farthest first (z grows towards the eye). The sort is not
stable; the order of faces with equal keys follows from this exact algorithm (the model replays it). The bee draws
its faces in file order ([0x582ac] = 1): within one of its 68 small objects only back-face culling hides faces, and
the objects are ordered by their centres (section 5). Note the near rule: a face is dropped as soon as its key depth
is ≥ −F, i.e. it comes within 2F of the eye (no clipping); faces with a vertex at D ≤ 0 but a key beyond −F would be
drawn with unprojected corners (not seen in the dumps).

### 7.1 Per-vertex filler parameters ([0x55908])

```c
// 0x4d4b4 (mode 3, 9, 17, 27) / 0x4d4e7 (mode 2, 8, 26): one word per vertex, s = 12 / 11
envu = (uint8)((vn[i].X >> s) + 0x80);  envv = (uint8)((vn[i].Y >> s) + 0x80);  param[i] = envv << 8 | envu;
// 0x4da28 (modes 7, 13, 15, 19..22) / 0x4d566 (s = 11; modes 6, 12, 14, 23): one dword per vertex
param[i] = (envv << 8 | envu) << 16 | (uint16)(verts[i].uv_word << [0x582cf]);   // texture u = byte 0, v = byte 1
```
With the normal of length ~255·1023: `envu = 128 + floor(63.7 · n̂x)` (s = 12) or `128 + floor(127.4 · n̂x)` (s = 11),
the same for v with n̂y (screen down): a sphere map seen along the view axis. With s = 11 the rim value 256 wraps to 0.

The draw routines pass, per sorted face, the projected corners `A = proj[a], B = proj[b], C = proj[c]` (dwords: x
low word, y high word) and the three parameter dwords to the filler (for 0x4e710 from words: the dword read at the
vertex's word address, only its low word is used). They set the texture selector [0x55912]: 0x4d5af (modes 2/3) =
the env map [0x55918]; 0x4dbd7 (mode 15) = material 0's selector; 0x4ddbf / 0x4deb1 (modes 20/21) do not set it
(their type-1 faces use whatever map the last mode 2/3 draw put there), and for type-1 faces they swap the
parameter's halves (`rol 16`) so that the env (u, v) is in the low word.

### 7.2 The scanline filler (common to 0x4e710, 0x51593, 0x4f47c, 0x52e96, 0x54548)

Parameters are bytes: 2 (env u, v) or 4 (texture u, v, env u, v), interpolated as 8.8. State lives in globals
(0x4d190..0x4d256) and partly survives from one triangle to the next.

```c
fill(A, B, C, pA, pB, pC)
{
    du_dx[all] = 0;                                         // [0x4d240..]; edge steps are NOT reset
    inside = count of x in {A.x, B.x, C.x} with (uint16)x < W;      // W = 320 or 256
    if (inside == 0) return;
    span = (inside == 3) ? span_noclip : span_xclip;
    // the top vertex P0 = smallest dword (y high word, then unsigned x); ties: C before B before A
    // P1 = the next one in the order A, B, C, A..., P2 = the one after (the face's winding is kept)
    if (P0.y >= YTOP) return;
    if (P1.y < YLIM && P2.y < YLIM) return;
    edge(P0, P2) -> e02;  edge(P0, P1) -> e01;  edge(P1, P2) -> e12;
    // edge: dy = Pb.y - Pa.y; if (dy != 0) { if dy != -1 (and for 4 parameters dy != 1):
    //     for each parameter k: d[k] = (int16)(diff_k << 8) / dy   (16-bit idiv, truncation),
    //   dx = (int32)((int16)(Pb.x - Pa.x) << 16) / dy }        // otherwise the old values stay
    if (e12.dy >= 0) {                                      // P1 is the middle vertex: long edge P0-P2 on the left
        mid(P1, e02);
        span(xl = xr = P0.x << 16, p = P0 << 8, y = P0.y, rows = e01.dy, left = e02, right = e01.dx);
        span(xl = xlong, xr = P1.x << 16, p = plong, y = P1.y, rows = e12.dy, left = e02, right = e12.dx);
    } else {                                                // P2 is the middle vertex: long edge P0-P1 on the right
        mid(P2, e01);
        span(xl = xr = P0.x << 16, p = P0 << 8, y = P0.y, rows = e02.dy, left = e02, right = e01.dx);
        span(xl = P2.x << 16, xr = xlong, p = P2 << 8, y = P2.y, rows = -e12.dy, left = e12, right = e01.dx);
    }
}
mid(Pm, long)                                               // 0x4eac9 (2 parameters) / 0x4ff31 (4)
{
    t = Pm.y - P0.y;
    xlong = P0.x << 16 + long.dx * t;                       // [0x4d1b0]
    plong[k] = (uint16)((P0[k] << 8) + long.d[k] * t);      // 16-bit
    w = Pm.x - (xlong >> 16);
    if (w != 0 && w != -1 && !(4 parameters && w == 1))
        du_dx[k] = (int16)((Pm[k] << 8) - plong[k]) / w;    // one gradient per triangle, at the middle row
}
span(xl, xr, p, y, rows, left, dxr)                         // 0x4eb93 / 0x4ed30, 0x51da8 / 0x51a62, ...
{
    if (rows <= 0 || y >= YRET) return;
    if (y < YLO) { n = YLO - y; if (n >= rows) return; xl += left.dx*n; xr += dxr*n; p += left.d*n (16-bit);
                   rows -= n; y = YLO; }
    rows = min(rows, YLAST + 1 - y);  if (rows <= 0) return;
    for (; rows; rows--, y++) {
        x0 = xl >> 16; x1 = xr >> 16;                       // floor of 16.16 (the code keeps y*W + x there)
        q = p;
        if (clipped) { if (x0 < 0) { q -= x0 * du_dx (16-bit); x0 = 0; }  x1 = min(x1, W); }
        for (x = x0; x < x1; x++, q += du_dx) put(x, y, q);   // pixels x0 .. x1-1
        xl += left.dx; xr += dxr;  p += left.d;              // 0x51593: as two dword adds (carry u -> v)
    }
}
```

| filler | used by | params | W | YTOP | YLIM | YRET | YLO | YLAST | pixel |
|---|---|---|---|---|---|---|---|---|---|
| 0x4e710 | modes 2, 3, 20 type 1 | 2 | 320 | 199 | 1 | 199 | 1 | 197 | `c = MAP[v][u]`, MAP = [0x55912] |
| 0x51593 | mode 15 | 4 | 320 | 199 | 1 | 199 | 1 | 197 | 7.3 |
| 0x4f47c | mode 20 other faces | 4 | 320 | 200 | 0 | 200 | 0 | 198 | 7.4 |
| 0x52e96 | mode 21 type 1 | 2 | 256 | 200 | 0 | 255 | 0 | 254 | as 0x4e710 |
| 0x54548 | mode 21 other faces | 4 | 256 | 255 | 0 | 255 | 0 | 253 | as 0x4f47c |

Rasterisation rules that follow: vertices are integer pixels; rows from the top vertex's y up to (not including)
the bottom vertex's y; the middle vertex's row belongs to the lower half; pixels from floor(left x) up to (not
including) floor(right x); no sub-pixel pre-step, parameters start at the vertex value and step from there; one
d/dx per triangle (from the middle row); texel = integer parts of the 8.8 values (nearest). All parameter
differences are taken modulo 256 and read as signed (−128..127): every triangle interpolates its 8-bit texture /
env coordinates the short way round the wrap.

Exact-model details (they change only isolated pixels; `engine.py` has them): the 4-parameter edge differences
come from one 32-bit subtraction of the parameter dwords, so a borrow from byte k makes byte k+1's difference one
texel smaller; edges with dy = ±1 (4 parameters) or −1 (2 parameters) keep the previous triangle's d/dy; in the
second half of 0x4e710's else branch the start values get the low byte of the long edge's 16.16 x as their
fraction; 0x51593 steps texture u→v and env u→v per row with 32-bit adds (carry) and per pixel with 32-bit adds in
alternating register orientation (env u → texture u carries in the first pixel of each pair, texture u → env u in
the others).

Buffer rows: the fillers write rows 1..197 (0x4e710 / 0x51593) or 0..198 (0x4f47c), and 0x78905 shows rows 1..197
only, so rows 0, 198 and 199 of the 3D parts stay black on screen (checked in `full.frames`).

### 7.3 Mode 15 pixel (0x51593): texture + bump-offset environment

```c
// tu, tv, eu, ev = integer parts (bytes); T = texture (material 0, [0x55912]), E = env map ([0x55918]),
// Bm = bump table ([0x558dc]); maps are 256x256 bytes, row = v, column = u
i  = tv << 8 | tu;
du = Bm[i];  dv = Bm[i + 1];                      // the byte to the right (u = 255: first byte of the next row)
c  = T[i] + E[(uint8)(ev + dv) << 8 | (uint8)(eu + du)];          // 8-bit sum
// second pixel of each pair (odd x, not the first pixel of the span): the env index is the 16-bit sum
// (ev << 8 | eu) + (dv << 8 | du), i.e. a carry from u into v
screen[y][x] = c;
```
The palette is laid out shade-major: the spike ball's env map holds multiples of 16 (16 shades, 8 → for the
darkest) and its texture (file 16) values 0..8; the textured surfaces' env map multiples of 8, texture 0..7. So the
colour index = shade band + texture colour. Bump offsets: spike ball −16..+14 texels, textured surfaces multiples
of 4 in −128..+124 (bytes, the env coordinates wrap).

### 7.4 Modes 20 / 21 (torso): bump-offset environment without texture

```c
i  = tv << 8 | (uint8)(2 * tu);                  // Bm is read as 128 x 256 pairs: u's top bit is lost
du = Bm[i];  dv = Bm[i + 1];
c  = E[(uint8)(ev + dv) << 8 | (uint8)(eu + du)];   // second pixel of a pair: 16-bit sum as in 7.3
```
Faces with type (+0) == 1 use the plain env filler (7.2) instead. All faces in the files have type 0; in the torso
part 130 of file 46's 459 faces have type 1 when it draws (set by the part, notes-parts3d-b).

## 8. Tables and maps

### 8.1 Environment map 0x7873a (dl = mode, ebp = colour table)

```c
build_sqrt_table();                                   // 0x7871c
for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) {   // (runs cx = 0x8000 down to 0 in steps of 2)
    r = min(isqrt(x*x + y*y), 127);                   // sqrt table: floor
    c = colours[127 - r];
    switch (dl) {
      case 0: c = (c >> 2) << 4;   break;             // with [0x582ce] != 0 a dither adds 1 to every 4th odd c
      case 2: c = c >> 1;          break;             //   ((c & 0x11) << 6 into an 8-bit accumulator; never on)
      case 3: c = (c >> 3) << 5;   break;
      case 4: c = (c >> 1) << 3;   break;
      default: break;
    }
    E[0x80 + y][0x80 + x] = E[0x80 + y][0x7f - x] = E[0x7f - y][0x7f - x] = E[0x7f - y][0x80 + x] = c;
}
```
A radially symmetric map centred between texels 127 and 128: at texel (u, v) the radius is
`isqrt(du² + dv²)` with `du = u − 128` (u ≥ 128) or `127 − u`, likewise dv. The colour tables are part data in
the exe image (128 bytes used each, index 127 − r): 0x6310 (spike ball: 0 for r ≥ 71, rising to 63 at the centre),
0x9df0 and 0x16c20 (textured surfaces, torso: identical, 0 for r ≥ 91, rising to 63). With the env radius 64 of
modes 3 / 15 / 20 / 21 only r ≤ 64 (+ bump offsets) is reached. The parts post-process the map
(spike ball 0 → 8, torso + 0x20). Bee and star use other maps (file 29, a part's map).

### 8.2 Square roots

0x7871c fills [0x558de] (= 0x682d0, the screen buffer) with 64 KB: value v repeated 2v + 1 times = floor(sqrt(n)).
0x79cbf (ebx = n): `n ≤ 0xffff: T[n]; ≤ 0x3fffc: 2 T[n >> 2]; ≤ 0xffff0: 4 T[n >> 4]; ≤ 0xfeff01: 16 T[n >> 8];
else 256 T[n >> 16]` (used for the radii and by 0x78d39, which would compute face and vertex normals and has no
callers: the files carry their normals).

## 9. Presenting: 0x78905 / 0x78948

```c
void present(void)            // 0x78905: rows 1..197 (offset 0x140, 0x3d90 dwords)
{
    for (i = 0x140; i < 0x140 + 4*0x3d90; i += 4) {
        vga[i] = screen[i];           // the frame drawn in the previous loop iteration
        screen[i] = background[i];    // [0x558d2]: a 320x200 picture
    }
}
// 0x78948: the same, but the buffer is filled with the byte [0x5799d] (chrome star)
```
The parts call it right before drawing, so the screen shows the previous iteration's picture. No retrace wait in
these routines.

## 10. Time bases

The engine has no clock. Everything animated comes from the parts (object / camera angles and positions, env
offsets 0x55862: PIT-tick driven, see the part notes) except the morph: key and step advance per PIT tick
(0x78fda in the callbacks 0xa4ac and 0x16646), the blend (0x78ffe) runs once per drawn frame. Its step granularity
(63 steps per key in the textured part, 64ths of the frame difference) is part of the motion; a remake can blend
continuously with `t = (step + fraction of the tick) / 64` within the key (and the same jump at the key change).
Random numbers: none.

## 11. Shortcuts a high-resolution remake should evaluate exactly

- Integer screen vertices, no sub-pixel stepping: transform and project in float (same F, focal 1024 / 2^shift
  pixels at 320x200, no aspect correction, centre (160, 100)), let the GPU rasterise. The jitter of the original's
  integer vertices is not part of the look.
- Cosine table with amplitude 1023 and 12-bit angles: use exact sin / cos of `angle * 2 pi / 4096` (fractional
  angles from the tick interpolation).
- Affine (screen-linear) interpolation of the env and texture coordinates: perspective-correct interpolation is the
  better evaluation (the triangles are small, the difference is slight). Keep the original's short-way wrap: per
  triangle, unwrap the three 8-bit (u, v) relative to one corner into −128..127 (texture coordinates; env
  coordinates can wrap only in mode 2, whose range is 0..255: 4 faces of the mirror, file 45, could).
- Environment map: instead of the 256x256 byte map with `isqrt` rings and the 32 / 16 / 8 shade steps, read the
  colour table at the continuous radius `r = |e − 128|` of the env coordinate e (interpolated between its entries)
  and blend between neighbouring palette shade bands (index = band * N + texture colour, N = 16 or 8, 7.3) in true
  colour. The bee and star read their maps from files: sample them bilinearly in true colour (palette applied
  first). The env coordinate itself is the original's design and stays: the per-vertex coordinates `128 + R n̂_xy`
  (R = 63.75, 127.5 for mode 2, without the truncation) interpolated across the face, not a coordinate computed per
  pixel from the interpolated normal.
  Found by the port (`remake/tools/enginetest`, then the whole parts against the capture): on coarse meshes the two
  differ visibly. With five vertices round the ring's section (torso) the per-vertex coordinates of a face cut
  across the map, nearer its bright centre, where the direction of the interpolated normal follows the arc; the
  per-pixel version makes the ring and the wall's face dome darker than in the capture. Mean absolute difference to
  the capture over 15 frames per part (640x480 area-averaged to 320x200, the capture taken 50–70 ms later to
  compensate the remake's lead), per-vertex / per-pixel: bee 11.37 / 11.69, spike ball 9.96 / 9.79, wall 8.00 /
  7.94, star 9.50 / 9.96, torso 10.10 / 10.24; the 95th percentile of the brightness is closer to the capture with
  the per-vertex coordinates for the bee, the spike ball, the wall and the torso (the star's is 2.4 / 255 above it
  instead of 1.9 below), and the spike ball's frames are about 1.7 / 255 brighter on average. The
  remake interpolates the per-vertex coordinates (perspective-correct) by default.
- Bump offsets: the table is read per texel (nearest) and adds whole texels; filter the offsets (as signed values)
  at the higher resolution.
- Texture maps: filter in true colour after the palette lookup of `texture + shade`; nearest only for the index sum.
- Painter's order with the 1/4-1/4-1/2 depth key, whole-object culling by bounding sphere, the near rules (faces
  closer than 2F dropped, objects dropped when their sphere reaches z ≥ −F) and the bee's unsorted faces: a depth
  buffer gives the geometrically right picture; where the original's order produced visible errors (none were
  noticed in the dumps) they would differ. The near rules decide what disappears when it comes close to the camera:
  keep them (they are composition, not resolution).
  Found by the port (`remake/tools/enginetest`): the bee needs its file order. Its objects are not single skins:
  WLDD object 8 (the abdomen, 116 vertices, 228 faces, closed) has front-facing faces lying just inside and partly
  in front of the outer surface, with dark env colours; with a depth buffer they show through as dark zigzag
  stripes over the abdomen (capture frame 6747), drawn in file order ([0x582ac] = 1) the outer surface covers them
  as in the capture. 24 vertices of the bee have zero normals (objects 20, 34, 35, 44, 57, 65, 66: mostly the
  wings): their env coordinate is the map centre (128, 128), the brightest texel, which a remake that renormalises
  the normal per pixel loses (interpolating the per-vertex coordinates keeps it).
- The blended normals are not renormalised; the morph uses 64 steps per key.
- Black rows 0, 198, 199 (7.2): a 320x200 artefact of the copy routine.
- The filler details listed at the end of 7.2 (borrows, stale gradients, carries) are invisible at this scale and
  should be dropped.

## 12. Unknowns, unused code

- Modes 0, 1, 4..14, 16..19, 22..27 and their fillers (0x4ef40, 0x4e04c, 0x50097, 0x50af5, 0x5203a, 0x5277b,
  0x4d7f5, 0x4d8e3, 0x4dfa3, 0x4dc8b, 0x4dd2b) are unused by STARS; only briefly identified.
- 0x79786 draws a dotted circle (colour 0x1e) of radius [0x79780] around ([0x79782], [0x79784]) — the bounding
  circle of the last object of 0x79483; no callers (debug). Its table access uses a 16-bit displacement 0x599c.
- 0x79da0 (cosine table by recurrence), 0x78d39 (normals), 0x78cc4 (DOS loader), 0x79148, 0x79184, 0x79192: no
  callers. [0x558f6] is allocated and never used. 0x55856..0x5585e are cleared by 0x787fc and the parts, never read.
- The chrome star part never sets F: it draws with the F = 100 that the textured surfaces left (the precalc 0x16945
  at the start used F = 20, shift 3). A remake should use 100 for the star.

## 13. Models (`models/engine/`, run inside the toolbox)

| file | what |
|---|---|
| `engine.py` | the integer model: matrices, transform, scene list, face list and quicksort, prep, the five fillers with their exact quirks |
| `run_engine.py` | copy of `run_stars.py` with `--engine-dump DIR --engine-every N --engine-max K`: memory before and after a part's 0x79483 / 0x789c4 call |
| `test_dump.py` | runs `engine.py` on a dump's memory and compares the screen buffer (and the 256x256 target) with the original's; prints which modes / fillers were used |
| `match_capture.py` | finds the `full.frames` record that shows a dumped draw and compares rows 1..197 |
| `envmap.py` | 0x7873a / 0x7871c, checked against three snapshots; the cosine table formula |
| `objects.py` | parses and summarises the `B!` and `WLDD` files |
| `xins.py` | prints the instructions behind `rep.py xref` hits |

Reproduce: `run_engine.py --load --snap ../../snaps/f7170_6534.snap --out <dir>/x --frames 7260 --engine-dump <dir>
--engine-every 10 --engine-max 4`, then `test_dump.py <dir>/*.npz` (spike ball; the other parts: snapshots
f5947_11e7f, f13810_a0ca, f16643_164dc, f19504_16f7c).
