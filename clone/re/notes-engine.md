# Scope "engine": the 3D engine, Craft's texture mapper, matrices, the geosphere

All addresses are linear addresses of the running program (= offsets + 0x400000 in `unpacked/clone_img.bin`, = near
pointers, = addresses in the emulator and in snapshots). The engine works in float (x87, **truncating**: control
word 0x1f7f, 64-bit mantissa) up to the screen; the texture mapper is Craft's hand-written assembler with 16.16 and
8.8 fixed point inside. "16.16" = 65536 is 1.0. Screen coordinates are pixels of the 320 × 200 target (shown at
4:3). Colours are dwords 0x00RRGGBB.

What was checked (scripts in `models/engine/`, section 13):
- **Renderer**: `engine.py` (a port of 0x4169f8, the clipper and the mapper with the x87's truncating arithmetic,
  `fpu.py`) reproduces the target page **byte for byte (0 differing pixels of 64000)** in 28 draw calls dumped from
  the emulator at the entry of 0x4169f8: part 7 (6 frames), part 8 (7, 4 of them with nothing drawn: the far cut,
  section 6), part 9 (8, 4 empty), part 10 (7); 20 non-empty frames with 54..1104 triangles. They include clipped
  triangles (part 10: 336 faces → 350 triangles), the full-height and the band projection, the normal shading and
  the per-vertex shade. For the 17 of them whose frame is on screen at the end of a timer tick, the drawn page is
  also **identical to the `full.frames` record** (64000 of 64000 pixels).
- **Normals, back-face test and depth buckets**: recomputed from the transformed vertices in every dump: vertex
  normals identical (1026 of 1026, parts 7 and 8), hidden flags identical (all faces, parts 7..10), the 4096 bucket
  lists identical (same faces in the same order).
- **Matrix routines**: `matrix.py` runs the seven routines in unicorn on 300 random matrices / angles / point sets
  and compares them with the model: 0 differences.
- **Geosphere**: `geosphere.py` regenerates the level-4 geosphere: identical to the snapshot (1026 vertices, 2048
  faces, same order).
- **Tables**: `tables.py`: the shade table 0x426b8c and the weight table 0x426d8c equal their formulas (512 / 1024
  entries).

-----------------------------------------------------------------------------------------------------------------

## 1. What the engine draws, and who uses it

Triangle meshes with float vertices, transformed by 3 × 4 float matrices, back-face tested in camera space, sorted
back to front into 4096 depth buckets (painter's algorithm, no depth buffer), clipped against a 90° pyramid, then
drawn by Craft's perspective texture mapper: bilinear-filtered 32-bit texture, Gouraud shade applied through a
log-space table. It writes only the pixels of the triangles: no clear, no transparency, no z test.

| part (parts3d) | frame routine → draw | geometry | texture ([face+0x38]) | projection ([0x422538]) | shade source | scale [0x427f70] |
|---|---|---|---|---|---|---|
| 7 (24:00..29:00) | 0x416fce → 0x4169f8 at 0x4172bd | geosphere object 0xf613dc (1026 v, 2048 f), deformed by 0x416e37, z += 2000 | map A [0xe35108] | −0.390625 (event 7): full height | vertex normals | 2·[0x427f80] up to 1 (the part's fade-in) |
| 8 (10:00..14:18) | 0x417929 → 0x417c3d | geosphere object 0xf72d4c, deformed by 0x4176d0 | map B [0xe3510c] | −0.3125 (precalc): rows 20..180 | vertex normals | 1400 / [0x427fe4] (distance 9400 → 1400), event 803 |
| 9 (18:00..20:00) | 0x4185e5 → 0x4188c8 | faces [0xf73584] (count [0x428018]) built per frame (0x418094, 0x418445, 0x417c48) | map C [0xe35110] | −0.3125 | per-vertex ints (+0x3c) | 1.2 (event 9) |
| 10 (36:00..3a:00) | 0x41958e → 0x4198b4 | faces [0xf73908] (count [0x428154]) built per frame (0x418fd2, 0x4193b6, 0x418b86) | map B | −0.3125 (event 10) | per-vertex ints | 1.0; 1.2 / 1.0 at events 1011 / 1012 |

Each part does: identity matrix, rotations, scale (and translation in 9 / 10), transform its vertices (0x4050e9),
its own deformation (7, 8), vertex normals (0x416b61; 7 and 8), back-face test and bucket insertion (0x416929),
background, then the draw (0x4169f8, EAX = the draw target [0xe350ec]). The parts' motion is in notes-parts3d.

## 2. Data

### 2.1 Vertex arrays (stride 0x34)

All vertex arrays (model positions, transformed positions, normals) have a stride of 0x34 bytes, but the engine
uses only three floats: x, y, z at +0, +4, +8 (so do parts 7 and 8; what parts 9 / 10 keep in the other 0x28 bytes,
if anything, is in notes-parts3d).

### 2.2 Face (0x48 bytes)

| offset | type | meaning |
|---|---|---|
| +0x00 | ptr | next face in the same depth bucket (written by 0x416929) |
| +0x04, +0x08, +0x0c | ptr | camera-space positions of the vertices a, b, c (an entry of the transformed vertex array) |
| +0x10, +0x14, +0x18 | ptr | the vertex normals of a, b, c (only +8 = nz is read by the draw); **0 at +0x10 = no normals: use +0x3c** |
| +0x1c, +0x20 | float | u, v of a (1.0 = one texture width = 256 texels) |
| +0x24, +0x28 | float | u, v of b |
| +0x2c, +0x30 | float | u, v of c |
| +0x34 | int | 1 = back face (hidden), 0 = front (written by 0x416929) |
| +0x38 | ptr | texture: 256 × 257 dwords + 1 dword (the maps of notes-gfx) |
| +0x3c, +0x40, +0x44 | int | shade of a, b, c in 16.16 when +0x10 is 0 (parts 9, 10) |

### 2.3 The geosphere objects (parts 7 and 8, built in the precalc)

Part 7 (0x416cca): `[0xf613dc]` faces, `[0xf613e0]` face count, `[0xf613e4]` vertex count, `[0xf613e8]`
transformed positions, `[0xf613ec]` normals, `[0xf613f0]` model positions (heap 0x622adc..). Part 8 (0x417563):
`[0xf72d4c]` faces, `[0xf72d50]` face count, `[0xf72d54]` vertex count, `[0xf72d58]` transformed positions,
`[0xf72d5c]` model positions, `[0xf72d60]` normals (heap 0x66dc24..). Both set-ups:

```c
geosphere(4);                                    // 0x4174c4: 1026 vertices (int16) at 0xf61404, 2048 faces at 0xf6cf84
for (i = 0; i < nv; i++) model[i] = (float)geo_v[i].{x, y, z};
for (k = 0; k < nf; k++) {                       // geo_f[k] = (a, b, c)
    f = &faces[k];
    f->p[0..2] = &xformed[a], &xformed[b], &xformed[c];
    f->n[0..2] = &normals[a], &normals[b], &normals[c];
    f->uv[j] = ((float)(model[v_j].x * 0.004), (float)(model[v_j].y * 0.004));   // double 0.004 at 0x41a4d0 / 0x41a554
    f->tex = map;                                // part 7: [0xe35108] (A), part 8: [0xe3510c] (B)
}
```
The texture coordinates are a planar projection of the undeformed sphere along z: u = x / 250, v = y / 250 texture
widths (the radius 300 spans 1.2 widths each way, so the map repeats 2.4 times across the sphere; the back half
shows the same pattern as the front). The hidden flag +0x34 is only written (0x416993); nothing reads it.

### 2.4 Depth buckets

`bucket[4096]` at 0xf5d3dc (dwords, face pointers; 0 = empty). The precalc 0x416806 zeroes entries 0..4094 (4095 is
zero from the BSS); 0x4169f8 sets every entry back to 0 as it takes the list, so the buckets are empty between
frames.

## 3. Matrices and transforms (0x404c9f .. 0x4050e9)

A matrix is 12 floats, 3 rows of 4: row r = (m[4r], m[4r+1], m[4r+2], m[4r+3]); a point transforms as
p' = M · (x, y, z, 1). The matrix routines change the matrix in place (EAX = matrix) and **pre-multiply**: the last
call is applied last to the points. Floats are passed on the stack (`ret 4` / `ret 0xc`).

```c
void identity(float m[12]);                      // 0x404c9f: m0 = m5 = m10 = 1, the rest 0
void scale(float m[12], float sx, float sy, float sz)   // 0x404cd6 (stack sx, sy, sz): M = diag(sx, sy, sz) M
{   for (k = 0; k < 4; k++) m[k] *= sx, m[4 + k] *= sy, m[8 + k] *= sz;  }   // the translation column too
void rot(float m[12], float ang, int a, int b)   // shared pattern of the three rotations
{
    float s = sin(ang), c = cos(ang);            // fsin / fcos of the float angle, stored as floats first
    for (k = 0; k < 4; k++) {
        float ra = m[4*a + k], rb = m[4*b + k];
        m[4*a + k] = ra * c - rb * s;
        m[4*b + k] = rb * c + ra * s;
    }
}
void rot_z(float m[12], float ang) { rot(m, ang, 0, 1); }   // 0x404d5c: M = [[c,-s,0],[s,c,0],[0,0,1]] M
void rot_y(float m[12], float ang) { rot(m, ang, 2, 0); }   // 0x404e36: M = [[c,0,s],[0,1,0],[-s,0,c]] M
void rot_x(float m[12], float ang) { rot(m, ang, 1, 2); }   // 0x404f0f: M = [[1,0,0],[0,c,-s],[0,s,c]] M
void translate(float m[12], float tx, float ty, float tz)   // 0x404feb: m3 += tx, m7 += ty, m11 += tz
void transform(float m[12] /*eax*/, Vec34 *src /*edx*/, Vec34 *dst /*ebx*/, int n /*ecx*/)   // 0x4050e9
{   for (i = 0; i < n; i++) for (r = 0; r < 3; r++)
        dst[i].v[r] = ((m[4r+1] * src[i].y + m[4r] * src[i].x) + m[4r+2] * src[i].z) + m[4r+3];   }
```
Every intermediate is extended precision, every store a truncated float. Part 7, for example, builds
M = S(1, 4/3, 1) · Rz(0.8 a) · Ry(1.1 a) · Rx(a) (calls in the order rot_x, rot_y, rot_z, scale; 4/3 = 0x3faaaaab).

## 4. The geosphere generator (0x4174c4 / 0x417420 / 0x4172c8)

An octahedron subdivided `levels` times, each new vertex pushed out to radius 300, all in int16. Static data in the
exe (read it from CLONE.EXE at run time): **0x427f9c**: 6 vertices × 3 int32 (only the low 16 bits are used):
(0,0,300), (0,300,0), (300,0,0), (0,−300,0), (−300,0,0), (0,0,−300); **0x427f84**: 8 faces × 3 bytes (vertex
indices). Buffers: vertices at 0xf61404 (stride 12 bytes, int16 x, y, z at +0, +2, +4; room for 4000), count
[0xf72d44]; faces at 0xf6cf84 (3 int16 per face; room for 4000), count [0xf72d48].

```c
void geosphere(int levels)                       // 0x4174c4 (EAX = levels; parts 7 and 8 use 4)
{
    nv = 6;  copy the 6 vertices of 0x427f9c;  nf = 0;
    for (k = 0; k < 8; k++) subdivide(oct_f[k][0], oct_f[k][1], oct_f[k][2], levels);
}
void subdivide(int a, int b, int c, int n)       // 0x417420 (EAX, EDX, EBX, ECX)
{
    while (n) {
        int ab = midpoint(a, b), bc = midpoint(b, c);
        n--;
        int ca = midpoint(c, a);
        subdivide(a, ab, ca, n);
        subdivide(ab, b, bc, n);
        subdivide(ca, bc, c, n);
        a = ab; b = bc; c = ca;                  // the middle triangle continues in the loop
    }
    face[nf++] = (a, b, c);
}
int midpoint(int i, int j)                       // 0x4172c8
{
    int mx = (v[i].x + v[j].x) / 2, my = ..., mz = ...;          // C division: truncates toward zero
    int len = (int)sqrt((double)(my*my) + mx*mx + mz*mz);        // fsqrt, fistp: truncated
    long double r = 1.0L / len;
    int16 x = (int)(mx * 300.0 * r), y = (int)(my * 300.0 * r), z = (int)(300.0 * mz * r);   // truncated
    for (k = 0; k < nv; k++) if (v[k] == (x, y, z)) return k;  // shared edges give the same vertex
    v[nv] = (x, y, z); return nv++;
}
```
Level 4: 1026 vertices, 2048 faces (8 · 4^4; the vertex count is that of an exact subdivision, 4·16² + 2, so the
deduplication never fails). Radius 298.5..300.5 (mean 299.6) because of the integer truncations (midpoint, length,
coordinates). The x87 details do not matter here: the same algorithm in exact rational arithmetic with C's integer
truncations gives the same 1026 vertices. Faces are wound so that (p0 − p1) × (p2 − p1) points outwards. Face order (depth-first, the middle triangle last) only matters for the painter's order within a
bucket.

For the remake: generate it the same way (the integer algorithm, or the same topology with exact floats on the
sphere of radius 300: the positions move by < 1.5 units, invisible); the texture coordinates come from the model
positions (2.3), and parts 7 / 8 deform per vertex from them. A finer subdivision would round the silhouette at
high resolution but would also change the Gouraud shading slightly; level 4 is the original.

## 5. Vertex normals (0x416b61)

```c
void vertex_normals(Vec34 *n /*eax*/, Face *f /*edx*/, int nv /*ebx*/, int nf /*ecx*/)
{
    for (i = 0; i < nv; i++) n[i] = (0, 0, 0);
    for (k = 0; k < nf; k++) {
        Vec a = *f[k].p[0] - *f[k].p[1], b = *f[k].p[2] - *f[k].p[1];
        Vec N = a × b;                           // (a.y b.z − a.z b.y, a.z b.x − a.x b.z, a.x b.y − a.y b.x), not normalised
        *f[k].n[0] += N; *f[k].n[1] += N; *f[k].n[2] += N;
    }
    for (i = 0; i < nv; i++) n[i] *= 1 / sqrt(n·n);
}
```
Called every frame after the part's deformation, from the **camera-space** positions: the normals are those of the
displayed (scaled, deformed) shape, area-weighted, pointing outwards (towards the viewer on the visible side:
nz < 0). x87 detail of the model: a.y and b.z enter some products as stored floats, Nx is stored as a float before
the additions (`engine.py`).

## 6. Back-face test and depth sort (0x416929)

```c
void cull_and_sort(Face *f /*eax*/, int nf /*edx*/)    // parts 9 / 10 also pass ECX = −1, unused
{
    for (k = 0; k < nf; k++) {
        Vec v0 = *f[k].p[0], v1 = *f[k].p[1], v2 = *f[k].p[2];   // camera space
        f[k].hidden = dot(v0, cross(v1, v2)) < 0;                 // exactly: (v1.y v2.x − v1.x v2.y) v0.z >
                                                                  //   v0.x (v1.y v2.z − v1.z v2.y) − v0.y (v1.x v2.z − v1.z v2.x)
        if (f[k].hidden) continue;
        int key = (int)((v0.z + v1.z + v2.z) * 0.2);              // double 0.2 at 0x41a4b8, truncated
        if (key < 0) key = 0;
        else if (key >= 4096) continue;                           // too far: not drawn at all
        f[k].next = bucket[key]; bucket[key] = &f[k];             // push at the head
    }
}
```
The test needs no projection: det(v0, v1, v2) ≥ 0 means the face is turned towards the eye at the origin
(det = 0 counts as visible). On the screen (y down) the vertices a, b, c of a visible face run clockwise.

**Draw order** (0x4169f8): buckets 4095 down to 0 (far to near; 1 bucket = 5/3 units of mean z), within a bucket the
face inserted last first (reverse face order). With back faces removed this is the painter's algorithm on the mean
depth. Measured in the 20 non-empty dumps (parts 7..10), a depth buffer would give the same picture: 0 pixels in 19
of them and 1 pixel in one are drawn by a face that is farther (1/z smaller by more than 0.1 %) than another face
covering the same pixel. So the remake can use a depth buffer and back-face culling.

**The far cut matters in part 8**: faces whose mean z is ≥ 6826.7 (key ≥ 4096) are not drawn. Part 8's blob starts
at distance 9400 ([0x427fe4], parts3d) and approaches: at ticks 3085..3095 all 1019..1055 front faces are cut and
nothing is drawn; the nearest faces appear first (from tick 3102 the capture changes). Faces with key < 0 (behind
the eye) go to bucket 0 and are removed by the clipper. Parts 7, 9, 10 stay well inside (keys up to 1632, 185, 176).

## 7. The draw (0x4169f8) and the shade

```c
void draw(uint32 *target /*eax*/)
{
    for (b = 0xfff; b >= 0; b--)
        for (f = bucket[b], bucket[b] = 0; f; f = f->next) {
            Vtx v[3];                            // 8 floats: x, y, z, (sx, sy: filled by the mapper), u, v, shade
            for (i = 0; i < 3; i++) {
                v[i].xyz = *f->p[i];
                v[i].u = f->uv[i].u * 65536 * 256;          // 2^24 per texture width
                v[i].v = f->uv[i].v * 65536 * 256;
                if (f->n[0]) {
                    float c = 1 - f->n[i]->z;
                    v[i].shade = c*c*c * 2437939.2 * [0x427f70];   // 2437939.2 = 37.2 * 65536 (double 0x41a4c8)
                } else
                    v[i].shade = f->shade_int[i] * [0x427f70];
            }
            clip_and_fill(&v[0], &v[1], &v[2], 0 /*filler index*/, f->tex, target);   // 0x41674b
        }
}
```
So the shade is 16.16; its integer part s = shade >> 16 is the offset in the shade table (section 9.5).
- **Normals (parts 7, 8)**: s = 37.2 × scale × (1 − nz)³ with nz the camera-space normal's z: 297.6 × scale where
  the surface faces the eye (nz = −1), 37.2 × scale at the silhouette (nz = 0), 0 facing away. With the table
  (9.5) that multiplies the texture's linear value by exp(0.011 (s − 128)): ×6.5 (white) in the middle,
  ×0.37 at the rim (scale 1). This is a view-direction ("facing ratio") light, not a light source.
- **Ints (parts 9, 10)**: the parts compute 16.16 values per vertex (part 9: 192 + 31 × (three sines), 101..277 in
  the dumps; notes-parts3d).
- [0x427f70] multiplies both: a brightness offset in the log domain.

## 8. Frustum clipping (0x41674b → 0x4166dc → 0x416669 → 0x416472, plane routine 0x416297)

Camera space of the engine: **x right, y down, z forward** (into the screen). The visible region is the pyramid
|x| < z, |y| < z (no near or far plane at this stage). 0x41674b rejects a triangle whose three z all have the sign
bit set, then clips against the four planes in turn; each stage passes 1 or 2 triangles to the next. The plane
routine always tests field0 − field2 < 0 (inside), so the stages rewrite the vertices in place first and undo it
afterwards: stage 1 tests x < z; stage 2 negates field 0 (−x < z); stage 3 swaps fields 0 and 1 (y < z); stage 4
negates field 0 (−y < z) and calls the filler with field 0 = −y, field 1 = −x.

```c
int clip_plane(Vtx *a /*esi*/, Vtx *b /*edi*/, Vtx *c /*edx*/, Vtx *A /*ebx*/, Vtx *B /*ecx*/)   // 0x416297
{
    float d0 = a->f0 - a->z, d1 = b->f0 - b->z, d2 = c->f0 - c->z;        // stored floats; d < 0 = inside
    int code = (d0 < 0) | (d1 < 0) << 1 | (d2 < 0) << 2;                   // sign bits
    if (code == 0) return 0;                                               // all outside
    if (code == 7) return 1;                                               // all inside: (a, b, c) as they are
    // rotate so that the odd vertex (alone inside or alone outside) is esi:
    //   codes 1, 6: esi = a, edi = b, edx = c;  codes 2, 5: esi = b, edi = a, edx = c;  codes 3, 4: esi = c, edi = b, edx = a
    t1 = d_esi / (d_esi − d_edi);  t2 = d_esi / (d_esi − d_edx);
    *A = edi * t1 + esi * (1 − t1);  *B = edx * t2 + esi * (1 − t2);      // fields x, y, z, u, v, shade (floats)
    if (code is 1, 2 or 4) { triangle (esi, A, B); return 1; }            // one vertex inside
    else                   { triangles (A, edi, edx) and (A, B, edx); return 2; }   // a quad as two triangles
}
```
Output buffers per stage: 0x427f30 / 0x427f50, 0x427ef0 / 0x427f10, 0x427eb0 / 0x427ed0, 0x427e04 / 0x427e24. The
last stage copies the three vertices to 0x427e44, 0x427e64, 0x427e84 (fields x, y, z, u, v, shade) and calls the
filler `[0x427d8c + 4 × [0x427dfc]]` with the stack arguments (0x427e84, 0x427e64, 0x427e44, index, target); in the
two-triangle case its first triangle goes to 0x415127 directly. The index is always 0 (pushed at 0x416b3f), so the
filler is always **0x415127**. The other eight entries of the "filler table" 0x427d8c (0x4160c0, 0x415fc7, …,
0x4159f1) are not fillers: 0x427d90.. is the entry table of the mapper's unrolled 8-pixel loop (section 9.3), which
happens to follow entry 0.

The shade is interpolated linearly in camera space at the clip points (t from the plane distances); the mapper then
interpolates it linearly on the screen.

## 9. Craft's texture mapper (0x415127)

Arguments on the stack: [ebp+8] = 0x427e84, [ebp+0xc] = 0x427e64, [ebp+0x10] = 0x427e44 (vertex records of 8 dwords:
+0 field0 (= −y), +4 field1 (= −x), +8 z, +0xc sx, +0x10 sy, +0x14 u, +0x18 v, +0x1c shade), [ebp+0x18] = target;
the texture pointer is in [0x4226d4] (set by 0x41674b).

### 9.1 Projection (per vertex)

```c
long double k = 16777216.0 / z;                                           // 2^24 (0x422584)
v->sx = 160*65536 + floor(k * field1 * [0x422534]);    // magic 1.5*2^52 + 160*65536 (double 0x422564); 16.16 column
v->sy = 100*65536 + floor(k * field0 * [0x422538]);    // magic 1.5*2^52 + 100*65536 (double 0x42256c); 16.16 row
long double iz = 1 / z;  v->z = (float)iz;  v->u = (float)(u * iz);  v->v = (float)(v * iz);   // u/z, v/z, 1/z
```
In camera terms, with [0x422534] = A = −0.625 and [0x422538] = B (set by 0x414f04(A, B): precalc 0x416806 and
event 10: (−0.625, −0.3125); event 7: (−0.625, −0.390625)):

    column = 160 + 160 · x / z          row = 100 + 80 · y / z  (B = −0.3125)  or  100 + 100 · y / z  (B = −0.390625)

(256 × 0.625 = 160, 256 × 0.3125 = 80, 256 × 0.390625 = 100.) The clip pyramid |x|, |y| < z therefore maps exactly to
columns 0..320 and rows 20..180 (B = −0.3125: parts 8, 9, 10 never draw outside that band) or rows 0..200 (part 7).
On a 4:3 screen one unit of x/z is 2/3 of the screen height, one unit of y/z is 0.4 (or 0.5) of it, so a part that
wants round objects scales its y by 4/3 with B = −0.390625 (part 7 does: S(1, 4/3, 1)).

For a GPU: x_ndc = x / z, y_ndc = −y / z (y down), viewport = columns 0..320 × rows 20..180 (or 0..200) of the 320 × 200
frame, any small near plane (the original has none; triangles end at the pyramid's apex), and the far cut of
section 6 per face.

### 9.2 Set-up (rasterisation)

```c
sort the three by sy (esi = 0x427e84, edi = 0x427e64, ebx = 0x427e44; swap esi/edi if esi.sy > edi.sy; swap edi/ebx
    if edi.sy > ebx.sy, then esi/edi again if needed) → top, mid, bot;
row_ptr = target + (top.sy >> 16) * 1280;                 // 0x4225d0 (sar: floor)
e = mid − top, b = bot − top                              // ints for sx, sy; floats for z, u, v, shade
D = e.sx * b.sy − e.sy * b.sx;   r = 65536 / D;           // sign of (float)D decides the side
for each attribute a in {u, v, z, shade}:                 // constant gradients of the triangle (per pixel)
    a_x = e.a * (b.sy r) − b.a * (e.sy r);                // [0x422650] u, [0x42265c] v, [0x422674] z, [0x422668] shade
    a_y = b.a * (e.sx r) − e.a * (b.sx r);                // [0x422658], [0x422664], [0x42267c], [0x422670]
shade_x_int = (int)shade_x;                               // [0x42266c] (16.16 step per pixel, truncated)
a_x8 = 8 * a_x  for u, v, z;                              // [0x422720..0x422728]
pre_top = ((~top.sy) & 0xffff) / 65536.0;                 // distance from the vertex to y = floor(sy) + 1 − 2^-16
pre_mid = ((~mid.sy) & 0xffff) / 65536.0;
if (D >= 0)  long edge top→bot = LEFT (with attributes, 0x414f2f), short edges top→mid, mid→bot = RIGHT (0x4150f7)
else         long edge = RIGHT, short edges = LEFT;
rows floor(top.sy) .. min(floor(mid.sy), 200) − 1 with the first short edge, then (the second short edge set up from
mid with pre_mid) rows floor(mid.sy) .. min(floor(bot.sy), 200) − 1; a zero-height half is skipped.
```
Edge records: right 0x422610 = {slope (float), slope_i (16.16, truncated), x (16.16)}; left 0x4225d4 = the same plus,
for u, v, z (floats) and shade (int): value at the row start, step when the integer x advances by floor(slope) + 1
("carry"), step when it advances by floor(slope):

```c
Edge edge_pos(Vtx *s, Delta *d, float pre)                // 0x4150f7
{   slope = d->sx / (long double)d->sy * 65536;  e.slope_i = (int)slope;  e.x = (int)(slope * pre + s->sx);   }
Edge edge_attr(Vtx *s, Delta *d, float pre)               // 0x414f2f
{
    slope, slope_i, x as above;  int Si = slope_i >> 16;
    float q = ((float)slope * pre − (x & 0xffff)) / 65536;          // pixels from the vertex to floor(x)
    for a in u, v, z (floats) and shade (16.16 int via magic 1.5*2^52 = floor):
        e.a        = a(s) + pre * a_y + q * a_x + a_x;               // value at (floor(x) + 1, row + 1 − 2^-16)
        e.a_nc     = Si * a_x + a_y;                                 // step to the next row, no carry
        e.a_c      = Si * a_x + a_y + a_x;                           // with carry
    (shade: e.shade = floor(shade(s) + pre*shade_y + q*shade_x) + shade_x_int)
}
per row (0x4161d5):
    left.x += left.slope_i;  carry = (left.x & 0xffff) < (left.slope_i & 0xffff);   // the fraction wrapped
    right.x += right.slope_i;
    left.{u, v, z} += carry ? left.*_c : left.*_nc;   (floats)   left.shade += carry ? shade_c : shade_nc;  (int)
    row_ptr += 1280; row++;
```
**Sample points**: row r is drawn with the edges at y = r + 1 − 2^-16, pixels floor(xl) .. floor(xr) − 1, and every
attribute is evaluated at x = column + 1. So pixel (i, r) shows the point (i + 1, r + 1) of the projected triangle
(its lower right corner) and is drawn if xl < i + 1 ≤ xr there. A remake that samples pixel centres matches it with
the projection moved by half a pixel: column = 159.5 + 160 x/z, row = 99.5 + 80 (100) y/z.

Clamps: rows < 0 are skipped (the pointer still advances), the bottom is clamped to row 200, the right end to
column 320. A span whose left end is < 0 starts at column 0, but the values advanced to column 0 (computed into
0x422684) are not used: the span keeps the attributes of its true start (a bug; never happens in the dumps: the
clipper keeps x ≥ 0). D == 0 does not occur either.

### 9.3 Spans: perspective every 8 pixels

```c
span(row): xl = left.x >> 16 (≥ 0), xr = min(right.x >> 16, 320), n = xr − xl; if (n <= 0) return;
    e = (n − 1) & 7;  blocks = (n − 1) >> 3;              // a first block of e + 1 pixels, then blocks of 8
    U0 = floor(left.u / left.z / 256);  V0 = ...          // 8.8 texels (u carries 2^24 per width), magic 1.5*2^60 (0x422588)
    at pixel e: Ue, Ve the same from (left.u + e u_x) / (left.z + e z_x);
    first block step: dU = floor(((u_e − (float)u_0) * (1/e)) / 256)   (table 0x4225b0 = 0, 1, 1/2 .. 1/7; 0 if e = 0)
    then every 8 pixels: U = floor(u'/z' / 256) at the block's last pixel, dU = (U − Uprev) >> 3, and the block
        starts at Uprev + dU (the division for the next block runs on the FPU while the integers draw).
    uv = U << 16 | (V & 0xffff), step = dU << 16 | (dV & 0xffff): one 32-bit add per pixel (a carry out of V's low
        16 bits adds 1/256 texel to U).
    shade per pixel: s16 = left.shade + k * shade_x_int (16.16, affine on the screen; s = s16 >> 16, logical shift)
```
The pixel loop is unrolled 8 times (0x4159f1, 0x415aea, …, 0x4160c0, 0xf9 bytes each, storing to [p − 0x1c] ..
[p − 0]); a span enters it at the copy given by the table 0x427d90[e] (Duff's device), p = 0x422730 advances by 0x20 per
block.

### 9.4 The texel: bilinear filter in the FPU

```c
Ui = uv >> 24; Vi = (uv >> 8) & 0xff; fu = (uv >> 20) & 15; fv = (uv >> 4) & 15;   // 4-bit fractions
t = tex + (Vi * 256 + Ui);
c00 = t[0], c10 = t[1], c01 = t[256], c11 = t[257];        // U+1 at Ui = 255 reads the next row's column 0;
                                                           // row 256 of the map is a copy of row 0, then 1 dword 0
w = weights[fu][fv];                                       // 0x426d8c: {(16−fv)(16−fu), (16−fv) fu, fv (16−fu), fv fu}
for each channel ch (B, G, R): L_ch = floor((w0 c00 + w1 c10 + w2 c01 + w3 c11) / 256);
```
The code does all three channels with four double multiplies: each texel's bytes are spread into a double's mantissa
(B at bits 0..7, R at 16..23, G at 32..39 under the exponent of 1.0: qwords 0x426b64, 0x426b6c, 0x426b74, 0x426b7c),
multiplied by the float weights (sum 256), added, stored (truncating) to 0x426b84, and the bytes 0x426b84 (B),
0x426b86 (R), 0x426b88 (G) read back. The sums never overlap, so this is exactly the floor above.

### 9.5 The pixel: the log-space shade table

```c
T[d] = min(255, (int)(128 * exp(0.011 * (d − 256))))      // 0x426b8c, 512 bytes, made by 0x416806 (f2xm1 / fscale)
pixel = T[s + L_R] << 16 | T[s + L_G] << 8 | T[s + L_B];  // s = shade >> 16; bits 24..31 = 0; one dword store
```
T[0] = 7, T[256] = 128, T[319..511] = 255. Indices ≥ 512 would read the weight table (bytes 00 00 80 43 00 …: black
dots); in the dumps the largest index s + L is 476, so this never shows.

The maps hold **log-encoded** channels: notes-gfx generates them through 0x4167b7,
L = clamp(trunc(128 + 90.909 · ln((1 + x) / 128)), 0, 255) with x the linear value (1/0.011 = 90.909). The table
undoes it: T[L + s] = (1 + x) · exp(0.011 (s − 128)) up to the truncations. So **the displayed channel is the
texture's linear value (1 + x) times exp(0.011 (s − 128)), clipped at 255**: s = 128 is ×1, each +63 doubles it.
The maps use L ≤ 175 (A: B channel 90..175; B: up to 153; C: up to 139).

The three maps are generated pictures (notes-gfx), read only as textures: they stay computed in the remake, in their
log form. The engine uses no stored picture.

## 10. The colour of a pixel, as a closed form (for the GPU)

    L     = bilinear sample of the log-encoded map at (u, v) · 256 texels, wrapping (texel centres at integer
            coordinates: GPU coordinate (256 u + 0.5) / 256), per channel, in 0..255
    s     = the shade, interpolated linearly on the screen (GLSL `noperspective`), in table units (16.16 / 65536)
    out   = min(255, 128 · exp(0.011 · (L + s − 256))) / 255     (display-space value)

with u, v perspective-correct and s from section 7 times [0x427f70]. Evaluated this way (per-pixel perspective,
exact bilinear weights, exp instead of the table, same coverage) the four checked frames differ from the original by
0.6 (part 7), 1.1 (8), 1.7 (9), 1.9 (10) levels on average over the drawn pixels (99th percentile 4..14, maximum
10..98 at a few pixels: the 8-pixel affine steps on steep faces and the seam column), which is the size of the
quantisation the remake drops. Filtering the log values (not linear ones) is part of the look: keep the map in its
log form and exponentiate after the filter.

## 11. Shortcuts to evaluate exactly in the remake

| original | approximates | remake |
|---|---|---|
| perspective division every 8 pixels, linear in between; first block from the span start | perspective-correct u, v | per pixel (GPU default) |
| u, v in 8.8 fixed, packed into one dword (V's carry leaks into U) | continuous texture coordinates | float |
| bilinear filter with 4-bit weights, floored channel sums; texel centres at integer coordinates; U + 1 at the seam column reads the next row; the 257th row and the extra dword | bilinear filtering of a repeating map | GPU linear filter, GL_REPEAT, +0.5 texel offset |
| shade table T (integer shade, truncated bytes) | (1 + x) · exp(0.011 (s − 128)), clamped | the exp in the shader |
| shade in 16.16 stepped per pixel (affine on the screen) | Gouraud | `noperspective` interpolation (it is affine in the original too) |
| sample point = the pixel's lower right corner | pixel centre | shift the projection by −0.5 pixel (9.2) |
| edge slopes truncated to 1/65536 px per row; float32 attribute stepping along the left edge | exact edges | the GPU rasteriser with MSAA |
| painter's order by mean z in 4096 buckets | visibility | depth buffer (identical in the checked frames, section 6) |
| geosphere in int16, radius 300 with truncations | a sphere | the same mesh (or exact floats on the sphere) |
| per-face far cut at mean z ≥ 6826.7 | — (a limit of the bucket table, visible at the start of part 8) | keep it: drop such faces |

## 12. Time base, frame coverage, 15-bit mode

- The engine has no state that lives across frames: the buckets are filled and emptied within the frame, the
  normals are recomputed every frame. Its inputs that change are [0x427f70] (events 7 = 1.0, 9 = 1.2, 803 −= 0.25,
  1011 = 1.2, 1012 = 1.0; part 7 and part 8 overwrite it every frame, see the table in section 1) and the projection
  constants (events 7 and 10). Nothing depends on the frame rate.
- It draws only the triangles: every other pixel of [0xe350ec] keeps what the part put there (its background) or,
  in the 32-bit mode, what was on that page four frames earlier. Whether the frame is fully covered is the parts'
  matter (notes-parts3d).
- The engine writes 32-bit pixels into whatever [0xe350ec] is; in the 15-bit mode that is the buffer 0xdb1aec, and the
  conversion makes 5:5:5 out of it. No difference in the engine.

## 13. Scripts (models/engine/)

| file | what it does |
|---|---|
| `fpu.py` | x87 arithmetic for the models: numpy longdouble with the host FPU set to truncate (fesetround), float stores, the magic-number roundings, fsin / fcos as the emulator does them |
| `run_engine.py` | copy of run_clone.py with `--dump-draws N,..`: at those calls of 0x4169f8 it saves the memory 0x400000..0xf96000 and the target page before and after the draw (`dump/engine/draw_t<tick>_c<call>.npz`, git-ignored) |
| `engine.py DUMP.npz ... [--smooth]` | the engine model: normals, cull flags and buckets recomputed and compared; the draw redone into the 'before' page and compared with 'after'; `--smooth` also draws the closed form of section 10 and reports the difference |
| `capture_check.py DUMP.npz ...` | the drawn page (and the model's page) against the `full.frames` record that shows that frame |
| `matrix.py` | the matrix routines in unicorn (via `emu.py`) against the model, random inputs |
| `geosphere.py` | regenerates the geosphere and compares it with a snapshot |
| `tables.py` | the shade and weight tables against their formulas; the maps' extra row |
| `emu.py` | minimal unicorn harness: call one routine with registers and stack arguments, FPU as the intro sets it |

The dumps used (`run_engine.py --load --snap snaps/t<part start>.snap --no-frames --dump-draws ...`): part 7 from t6924 (calls 0,
1, 20, 40, 300, 600), part 8 from t3084 (0, 1, 20, 40, 250, 500, 750), part 9 from t4621 (0, 1, 20, 40, 300, 700,
1100, 1500), part 10 from t10380 (0, 1, 20, 40, 300, 700, 1100). A run of 800 ticks takes a few minutes.

## 14. Unused code and open points

- 0x414f1d (stores the pointer 0x422b64 into its argument) has no caller. Filler indices other than 0 are never used
  (and would jump into the middle of the pixel loop).
- 0x416929's hidden flag (+0x34) is written but nothing reads it (no other access with offset 0x34 to a face).
- Not seen in the checked frames, so not verified against the original's output: the left-clip bug (9.2), D == 0,
  shade-table indices ≥ 512. They are modelled as the code reads.
- The model of the clipper / mapper is checked on frames with up to 350 triangles per frame; frames of the parts not
  dumped (other times) are expected to behave the same, as nothing in the engine depends on the part.
