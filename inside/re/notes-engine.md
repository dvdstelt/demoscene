# Inside — SurRender P5 1.02, the 3D engine (engine scope)

Hybrid's SurRender ("SurRender P5 version 1.02 (Apr 01 1996)", string at 0x421a7c) as INSIDE links it, and the
LightWave scene reader the flower part uses. This file describes the engine by what goes in and what comes out, with
every constant, so that the parts' notes can refer to it and the remake can render the same geometry with OpenGL.

Verified: a Python model of the whole render path (`models/engine/`) reproduces the original's frames **pixel for
pixel**: the logo part over 327 consecutive rendered frames (refreshes 1446..1728) including its 2D bitmaps, and
sampled frames of the flower (15), flight (42), credits (20), shadow scene (12), temple (25) and water (15) parts — every
rasterizer, the billboards, hierarchies (objects parented to the camera, a camera with a parent), the 0x1000
texture stage, lighting, back-face and bounding-box culling, near clipping and the sort, 0 pixels different.
The model takes from the original only what the parts set (object rotations / positions / flags, vertex data the
parts animate, materials, scene parameters); normals, transforms, culling, projection, lighting, sorting, clipping
and rasterizing are the model's. The LightWave reader's per-frame motion table and its evaluation are reproduced bit
for bit. See "Verification" at the end.

## Where the code is

| range | what |
|---|---|
| 0x40c034..0x40c6e4 | the scene render pipeline: 0x40c6ac render, stages 0x40c4e4 (view setup), 0x40c2b8 (world transforms), 0x40c0e0 (run each object's pipeline), 0x40c384 (sort), 0x40c13c (draw); 0x40c158 object → camera; 0x40c224 hierarchy; 0x40c034 clipper scratch setup |
| 0x410520..0x41209c | API: scene, camera, material, object creation, .sro loader, normals, hierarchy, vertex edits, morph, projection of a point, line drawing |
| 0x4120a0..0x4134c2 | vertex lerp (0x4120a0, unused) and the rasterizers 0x412264, 0x412db0 |
| 0x4134c7..0x413500 | Watcom rand() / srand() (part of the C library, linked in between) |
| 0x413510..0x414e5a | vector / 3x3 matrix library (doubles), Euler rotations, look-at matrix 0x414cec |
| 0x414e5c..0x415783 | rasterizer 0x414e5c (texture + Gouraud shade table) |
| 0x415790..0x415df0 | billboards (scaled 2D sprites placed in 3D) |
| 0x415df0..0x41669b | 2D bitmaps (0x415e50 make, 0x415df0 draw: copy / table / transparent / transparent table) |
| 0x41669c..0x417e27 | rasterizers 0x41669c, 0x416dc8, 0x4172e0 |
| 0x417e28 | pow (x87), used by the LWS number reader |
| 0x41a440..0x41b752 | the mesh object pipeline (table 0x423a84): culling, projection, face list, lighting, environment mapping |
| 0x41be3c..0x41c99f | triangle setup shared by all rasterizers (0x41be91) and the near-plane clipper (0x41c0e0) |
| 0x408030..0x409358 | the LightWave .lws reader and evaluator (inside the flower part's code; the flower scope uses it) |

All of it is reached from the parts through about 40 API routines (table below). Pipelines are linked lists of
`{function, next}` pairs in the data object: the scene's 0x4229c0 → 0x40c4e4, 0x40c2b8, 0x40c0e0, 0x40c384,
0x40c13c; the mesh object's 0x423a84 → 0x41a440, 0x41a5b8, 0x41a458, 0x41af78, 0x41ae9c, 0x41adcc, 0x41acc4,
0x41aba4, 0x41ac04; the billboard's 0x42381c → 0x4157d4; a prebuilt polygon list 0x423a7c → 0x41b4d4 (unused).

**Floating point.** The FPU runs with precision control = 53 bits (control word 0x127f, loaded by the C library's
start-up 0x41d4b8 from AX), not 64 bits as NOTES.md says: every x87 add, multiply, divide and square root rounds to a
double. The model reproduces the integer results only with double arithmetic in the original operation order
(with 64-bit precision the projection scale comes out 4479 instead of 4480). Float → int conversions truncate toward
zero (0x40ba78). All engine maths is in doubles; the LWS reader stores floats (float32).

**Time, randomness, pictures.** The engine has no clock and draws nothing on its own: everything animates through
what the parts set before each render (positions, rotations, vertex data, flags), once per drawn frame; the LWS
evaluator reads the frame number from the float [0x4b685c] that the flower part advances. It calls no random
numbers. It does not clear the frame: the parts clear the viewport (0x41127c) or draw a background first, and the
render only writes the pixels its triangles, billboards and bitmaps cover. Its textures, tables and bitmaps are
the parts' (their notes say what they are).

## Units and conventions

- **Object space**: vertex positions are 32-bit ints (the .sro's int16 × a load scale). Positions of objects and
  cameras are ints too. Rotations are 3x3 double matrices, row-major (`M[r][c]` at byte 8·(3r + c)), acting on
  column vectors: `world = M · local + position`.
- **Camera space**: x right, y down, z forward (into the screen); stored ×128 (7 fractional bits) as ints:
  vertex `zc` = trunc(128·z).
- **Screen**: 1/16 pixel (4 fractional bits), origin at the top-left of the 320×200 buffer; the projection centre
  (cx, cy) is the middle of the viewport (160, 100). Screen x = cx + Fx·X/Z, y = cy + Fy·Y/Z with Fx = fov·W/320,
  Fy = fov·H/240 (W, H = 320, 200) — square pixels on a 4:3 screen.
- **Texture coordinates**: 16-bit words, 12-bit fractions of the texture: u = 4096 is the texture's full width,
  whatever its size (2^wlog × 2^hlog texels: 256 × 256 → 16 units per texel, the flight's 512 × 512 map → 8), with
  wrap-around (masks); the texel is `(u_acc >> (24 − wlog)) & umask` + 2^wlog · `((v_acc >> (24 − hlog)) & vmask)`.
- **Shade**: a 16-bit word per vertex; the light level used by the tables is shade / 16 (0..62; the lighting
  clamps shade to 992).
- **Normals**: face normals ints of length 1024, vertex normals ints of length 819.2 (0.8 × 1024), truncated.
- Matrices for Euler angles: `Rx(a) = [[1,0,0],[0,c,-s],[0,s,c]]`, `Ry(a) = [[c,0,s],[0,1,0],[-s,0,c]]`,
  `Rz(a) = [[c,-s,0],[s,c,0],[0,0,1]]` (c = cos a, s = sin a, radians; computed with fsin / fcos).

## Structures

### Scene (0x50 bytes, the parts allocate it)

| offset | type | meaning |
|---|---|---|
| 0x00 | ptr | pipeline list (0x4229c0, set by 0x410878) |
| 0x04 | ptr | first object of the scene's object list (objects are linked through obj+4 / obj+8) |
| 0x08 | ptr | the 8-bit target buffer (pitch = width); parts set it to the back buffer [0x4248d0] every frame |
| 0x10, 0x14 | int | projection centre cx, cy in pixels (computed unless flags bit 1) |
| 0x18, 0x1c | int | projection scales XS, YS (computed unless flags bit 0): XS = trunc(W·32768·fov·(1/81920)), YS = trunc(H·32768·fov·(1/61440)) (= 128·fov·W/320, 128·fov·H/240) |
| 0x20, 0x24 | int | width W, height H of the buffer (pitch = W) |
| 0x28, 0x2c | int | viewport x0, x1 (columns x0 ≤ c < x1) |
| 0x30, 0x34 | int | viewport y0, y1 (rows y0 ≤ r < y1) |
| 0x38 | ptr | the camera object |
| 0x3c | int | fov (default 0x100 = 256: horizontal half angle atan(160/256) = 32.0°) |
| 0x40 | word | ambient shade (copied to every lit vertex before the lights) |
| 0x44 | int | near plane, camera z ×128 (default 0x2000 = 64 units) |
| 0x48 | int | far plane in units, used only by billboards (default 0x400000) |
| 0x4c | byte | flags: 1 = keep XS/YS, 2 = keep cx/cy, 4 = scale from the viewport size (x1-x0, y1-y0) instead of W, H |

0x40c4e4 latches these into globals at the start of a render: [0x4b8fc8] ambient, [0x4b8fcc] object list,
[0x4b8ff8] fov, [0x4b900c] W (= pitch), [0x4b8fd0] H, [0x4b8fe8]/[0x4b9000] viewport x0/x1 (x1 clamped to ≤ W, x0
to ≥ 0), [0x4b8fe4]/[0x4b8ff4] y0/y1 (same), [0x4b9010]/[0x4b8fd4]/[0x4b8fe0]/[0x4b8fd8] the same ×16,
[0x4b9014]/[0x4b8fec] XS/YS, [0x4b8ffc]/[0x4b9004] cx/cy, [0x4b9020] near, [0x4b9018] far, [0x4b8ff0] buffer.
cx = x0 + (x1 − x0)/2 and cy = y0 + (y1 − y0)/2 (C division) are computed from the scene's own (unclamped) values.

### Object (mesh, camera, light): 0x10c-byte header, then vertices and faces

Cameras and lights are objects without geometry (the parts allocate 0xc8 or 0xd4 bytes for them).

| offset | type | meaning |
|---|---|---|
| 0x00 | ptr | pipeline list: 0x423a84 for meshes (set by the .sro loader), 0 for cameras / lights, 0x42381c for billboards |
| 0x04, 0x08 | ptr | next / previous object in the scene list |
| 0x0c | ptr | parent object (0 = none) |
| 0x10, 0x14, 0x18 | int | position (in the parent's space) |
| 0x1c, 0x20, 0x24 | int | world position (computed every render) |
| 0x28..0x6f | double[9] | rotation matrix (in the parent's space) |
| 0x70..0xb7 | double[9] | world rotation (computed every render) |
| 0xb8, 0xbc, 0xc0 | int | the object's origin in camera space ×128 (computed) |
| 0xc4 | dword | flags (below); bits 16..20 = the bounding box's clip flags of the last render |
| 0xc8 | int | light: range R (logo 550). Billboard: start of its 0x3c-byte render item (0xc8..0x103) |
| 0xcc | int | mesh: sort bias (subtracted from the face keys, camera z ×128). Light: intensity I (1600 in every part) |
| 0xd0 | ptr | first vertex (vertices are linked through vertex+0) |
| 0xd4 | int | number of vertices |
| 0xd8 | ptr | first face (linked through face+0) |
| 0xdc | int | number of faces |
| 0xe0..0xf4 | int[6] | bounding box: min x, y, z, max x, y, z of the vertices (0x410de8) |
| 0xf8 | int | radius: trunc(max |vertex|) |
| 0xfc..0x108 | int[4] | screen bounding box of the last render (min x, min y, max x, max y, pixels) |
| 0x10c.. | | vertices (0x40 each), then faces (0x34 each) — the loader's layout; 0x411c80 copies put the faces first |

Flags [0xc4]:

| bit | meaning |
|---|---|
| 0x0001 | hidden: the pipeline stops (the parts toggle it to show / hide objects); a hidden light is skipped |
| 0x0004 | marks children during 0x4111e0 (object removal with its children; unused by INSIDE) |
| 0x0008 | do not inherit the parent's rotation (world rotation = own rotation); set by 0x41077c / 0x410800 |
| 0x0010 | do not inherit the parent's position |
| 0x0040 | lit: Gouraud shade from the scene's lights (stage 0x41aba4) |
| 0x0080 | this object is a point light |
| 0x0100 | no back-face culling: every face and vertex is used (flight landscape, credits tunnel, water, the shadow scene's grid) |
| 0x0200 | environment map in u, v (stage 0x41ac04; unused by INSIDE) |
| 0x0400 | flat environment index per face into face+0x2c (stage 0x41ae9c; unused) |
| 0x0800 | second (environment) coordinates eu, ev (stage 0x41adcc; unused) |
| 0x1000 | u, v from the camera-space normal plus the screen position (stage 0x41acc4; flower petals, flight ships, credits spike ball, water, temple) |
| 0x2000 | skip the bounding-box test (flight) |

### Vertex (0x40 bytes)

| offset | type | meaning |
|---|---|---|
| 0x00 | ptr | next vertex |
| 0x04 | dword | flags: bit 1 = used this frame; bits 16..20 = clip flags 0x10000 x < x0, 0x20000 x ≥ x1, 0x40000 y < y0, 0x80000 y ≥ y1, 0x100000 behind the near plane |
| 0x08, 0x0c, 0x10 | int | position x, y, z (object space) |
| 0x14, 0x18 | int | screen x, y in 1/16 pixel (truncated) |
| 0x1c | int | camera z ×128 (truncated) |
| 0x20, 0x24 | int | projection numerators X'/2048, Y'/2048 (used by the near clipper and stage 0x41acc4) |
| 0x28, 0x2a | int16 | u, v (12-bit fractions of the texture, see Units) |
| 0x2c, 0x2e | int16 | second coordinates eu, ev for rasterizer 0x4172e0 (its second texture is indexed with eu >> 4, ev >> 4: 256 × 256) |
| 0x30 | int16 | shade (Gouraud channel 1) |
| 0x32 | int16 | second channel (rasterizer 0x412264: fog level) |
| 0x34, 0x38, 0x3c | int | vertex normal (length 819.2) |

### Face (0x34 bytes)

| offset | type | meaning |
|---|---|---|
| 0x00 | ptr | next face of the object |
| 0x04 | int | sort key (computed) |
| 0x08 | ptr | draw function (rasterizer), set for all faces by 0x410850 |
| 0x0c | ptr | link in the frame's polygon list (computed) |
| 0x10 | ptr | material (the loader's ECX; -1 → 0) |
| 0x14, 0x18, 0x1c | ptr | its three vertices |
| 0x20, 0x24, 0x28 | int | face normal (length 1024) |
| 0x2c | int16 | flat environment index (stage 0x41ae9c, unused) |
| 0x30 | dword | flags: bit 0 = hidden (a double-sided face is drawn anyway), bit 1 = front-facing this frame, bit 8 (byte 0x31 bit 0) = double-sided, bits 10..12 = perspective subdivision 2^(n-1) pixels (0 = affine; never set in INSIDE: only 0x411e68 sets it and nothing calls it), bit 20 = needs near clipping (set on every listed face) |

### Material (0x44 bytes, 0x4105e4)

| offset | type | meaning |
|---|---|---|
| 0x00 | ptr | texture (8-bit, 2^wlog × 2^hlog) |
| 0x04 | ptr | second texture (environment map of rasterizer 0x4172e0) |
| 0x08 | ptr | table 1: shade table (.shd: [level·256 + colour]) or blend table (.int: [colour·256 + background]) |
| 0x0c | ptr | table 2 (rasterizer 0x412264: [level2·256 + colour]) |
| 0x18 | dword | flags: bit 0 = fixed texture coordinates: the rasterizer copies 0x20..0x34 to the face's three vertices' u, v before drawing (in vertex order v0, v1, v2) |
| 0x20..0x34 | int[6] | u0, v0, u1, v1, u2, v2 for flag bit 0 (texture fractions, stored to the int16 fields; the flower animates them every frame) |
| 0x38 | int | u mask (texture width − 1) |
| 0x3c | int | v mask (height − 1) |
| 0x40, 0x41 | byte | log2 width, log2 height |

### Billboard (0x104 bytes, 0x415790) and bitmap (0x28 bytes, 0x415e50): see their sections.

## API routines (as the parts call them; Watcom register convention EAX, EDX, EBX, ECX, then the stack)

| address | call | effect |
|---|---|---|
| 0x410878 | scene_init(EAX scene) | pipeline 0x4229c0, object list 0, fov 0x100, ambient 0, near 0x2000, far 0x400000 |
| 0x410618 | scene_viewport(EAX scene, EDX W, EBX H, ECX x0, stack: y0, x1, y1) | [0x20] W, [0x24] H, [0x28] x0, [0x30] y0, [0x2c] x1, [0x34] y1 (every part: 320, 200, 0, 0, 320, 200) |
| 0x410520 | camera_init(EAX obj) | zero 0xc8 bytes, rotation = identity (0x4108dc with zeros) |
| 0x4108a8 | object_init(EAX obj) | the same (cameras, lights, object headers) |
| 0x4108dc | object_euler(EAX obj, stack: double a, b, c) | rotation = Rx(a)·Ry(b)·Rz(c) (0x4145f4); position unchanged |
| 0x410868 | object_position(EAX obj, EDX x, EBX y, ECX z) | [0x10..0x18] = x, y, z |
| 0x411154 | scene_add(EAX scene, EDX obj) | puts the object at the head of the scene list (only if it is not in a list) |
| 0x410bb0 | load_sro(EAX obj, EDX sro data, EBX scale 16.16, ECX material) | builds the object in place (memory: 0x10c + 0x40·nv + 0x34·nf bytes): header zeroed, identity rotation, position 0, pipeline 0x423a84, vertices and faces (below), face normals 0x410918, vertex normals 0x410a70, bounding box and radius 0x410de8 |
| 0x410850 | object_draw_function(EAX obj, EDX rasterizer) | face+8 = EDX for every face |
| 0x4105e4 | material_init(EAX mat, EDX texture, EBX texture 2, ECX table, stack: table 2, umask, vmask, byte wlog, byte hlog) | material fields 0x00, 0x04, 0x08, 0x0c, 0x38, 0x3c, 0x40, 0x41 (flags 0x18 untouched) |
| 0x410918 | face_normals(EAX obj) | face normal = (p1 − p0) × (p2 − p0) scaled to length 1024 (factor 1024 when the length is ≤ 1), truncated |
| 0x410a70 | vertex_normals(EAX obj) | vertex normal = Σ normals of its faces, scaled to length 1024·0.8, truncated |
| 0x41172c | object_translate(EAX obj, EDX dx, EBX dy, ECX dz) | adds to every vertex, recomputes the bounding box |
| 0x4116c8 | object_scale(EAX obj, stack: float sx, sy, sz) | vertex = trunc(vertex·s) per axis (bounding box not updated) |
| 0x4114c8 | object_morph(EAX dst, EDX a, EBX b, stack: float t) | for min(face counts) faces: normal = trunc(a + (b − a)·t); for min(vertex counts) vertices: normal and position the same (bounding box not updated) |
| 0x411c80 | object_copy(EAX dst, EDX src) | copies header, faces and vertices (faces first in the copy), remaps the pointers, unlinks it from any list |
| 0x410f34 | make_grid(EAX buffer, EDX nx, EBX nz, ECX sx, stack: sz) | writes an .sro image of a grid in the XZ plane: (nx+1)(nz+1) vertices at x = j·sx − (nx·sx)/2, y = 0, z = i·sz − (nz·sz)/2 (u, v left as they are in the buffer), 2·nx·nz faces (v[i][j], v[i][j+1], v[i+1][j]) and (v[i+1][j], v[i][j+1], v[i+1][j+1]); load it with 0x410bb0 |
| 0x41077c | object_look_at(EAX obj, EDX x, EBX y, ECX z) | rotation = 0x414cec(target − the object's world position), flag 0x08 |
| 0x40c6ac | render(EAX scene) | the render (below); the parts clear the viewport first |
| 0x41127c | scene_clear(EAX scene, EDX colour) | fills the viewport rows y0..y1−1, columns x0..x1−1 of the target with the colour |
| 0x411e80 | project_point(EAX scene, EDX out double[2], EBX world point double[3]) → 0 / −1 | the camera's world transform; p = R^T (point − camera); −1 if near/256 > p.z; else out = (cx + XS·p.x/(128·p.z), cy + YS·p.y/(128·p.z)) in pixels, not truncated (flight sun, water) |
| 0x412070 | vertex_copy(EAX dst, EDX src) | copies 0x40 bytes, dst.next = 0 |
| 0x415790 | billboard_init(EAX obj) | zero 0x104, identity, pipeline 0x42381c, draw 0x415dbc, [0xec] = [0xf0] = 1.0f |
| 0x415e50 | make_bitmap(EAX pixels, EDX pitch, EBX width, ECX height, stack: name) → bitmap | allocates 0x28 bytes: type 1, [4] = 1.0f, [8] pitch, [0xc] width, [0x10] height, [0x18] pixels, [0x24] key 0 |
| 0x415df0 | draw_bitmap(EAX bitmap, stack: float x, float y) | by type (below) into [0x4b6ec0] |
| 0x40c034 | clip_init() | points the clipper's two scratch faces at its four scratch vertices (init, flower, water, shadow scene) |
| 0x413510 | vec_set(EAX v, stack: double x, y, z) | |
| 0x413540 / 0x41355c | vec_add / vec_sub(EAX a, EDX b) | a += b / a −= b |
| 0x4136fc | vec_length(EAX v) → st0 | sqrt((y² + x²) + z²) |
| 0x413750 | vec_set_length(EAX v, stack: double l) | v·= l/|v| |
| 0x4137b4 | vec_copy(EAX dst, EDX src) | |
| 0x413d58 | vec_add_scaled(EAX dst, EDX v, stack: double s, ptr M) | dst += M·(v·s/|v|) |
| 0x413e68 | mat_copy(EAX dst, EDX src) | |
| 0x413f44 | mat_identity(EAX m) | |
| 0x414070 | mat_mul(EAX a, EDX b) | a = a·b, each element (a[i][1]b[1][j] + a[i][0]b[0][j]) + a[i][2]b[2][j] |
| 0x4144ec | mat_apply(EAX v, EDX M) | v = M·v, each component (v0·M[i][0] + v1·M[i][1]) + v2·M[i][2] |
| 0x4145ac / 0x4145c4 / 0x4145dc | rotate_y / rotate_x / rotate_z(EAX M, stack: double a) | M = M·Ry(a) / M·Rx(a) / M·Rz(a) |
| 0x414418 | rotate_z (the routine behind 0x4145dc; flight calls it directly) | |
| 0x4145f4 | mat_euler(EAX M, stack: double a, b, c) | M = Rx(a)·Ry(b)·Rz(c) |
| 0x414cec | mat_look(EAX M, EDX direction double[3]) | d = dir/|dir|; r0 = normalize(−d.z, 0, d.x); r1 = normalize(d × r0) (cross 0x4135c4: (a.y b.z − a.z b.y, a.z b.x − a.x b.z, a.x b.y − a.y b.x)); M rows = r0, r1, d. Note: as an object's rotation this maps local (0,0,1) to the third column, not to d, and a camera with this rotation looks along (r0.z, r1.z, d.z) — it is the transpose of a look-at rotation; the credits use it (0x406385). Checked by calling the original routine in unicorn for 200 random directions (`call_engine.py`): equal to 1e−12 |
| 0x417e28 | pow(st1 = x, st0 = y) | x^y (LWS exponents) |

## The render: 0x40c6ac(scene)

[0x4b8fdc] = scene, [0x4b9008] = scene+0x38 (camera), then the scene pipeline, each stage called with EAX = 0:

1. **0x40c4e4** latches the view (Scene above).
2. **0x40c2b8 world transforms**, for every object in the list (the camera must be in the list for its world
   fields to be current):
   ```
   if (parent == 0 || (flags & 8 && flags & 0x10)) { world_rot = rot; world_pos = pos; }
   else {
       (R, P) = chain(obj)          // 0x40c224, recursive over the parents' *own* rot/pos (their flags ignored):
                                    // chain(o) = o.parent ? (Rp·o.rot, Pp + Rp·o.pos) with (Rp, Pp) = chain(o.parent)
                                    //                     : (o.rot, o.pos)        (doubles)
       world_pos = (flags & 0x10) ? pos : trunc(P);
       world_rot = (flags & 8) ? rot : R;
   }
   ```
3. **0x40c0e0**: [0x4b8fc0] (the frame's polygon list) = 0; for every object in list order its pipeline's stages run
   with EAX = object while they return 0 (a non-zero return ends that object).
4. **0x40c384 sort** (below).
5. **0x40c13c draw**: for every item of the sorted list: call item+8 (the face's rasterizer, or the billboard's
   draw function) with EAX = item.

### The mesh pipeline 0x423a84 (per object)

**0x41a440** — return 2 if hidden. Else 0x40c158: object → camera:
```
C = camera world rotation, Ct = transpose(C)
T = trunc(128 · Ct·(obj.world_pos − cam.world_pos))     → obj+0xb8..0xc0   (ints, camera space ×128)
M = Ct · obj.world_rot                                     → [0x4b8f78] (9 doubles, "object to camera")
```

**0x41a5b8 bounding box** — unless flag 0x2000 (then [0xc6] |= 0x10, continue). The 8 corners of the box
(0xe0..0xf4) are transformed: `p = M·(128·corner) + T` (0x4137b4, ×128, 0x4144ec); p.z ≤ 16384 → flag
0x100000; else projected `x = XS·p.x/(128·p.z) + cx`, `y = YS·p.y/(128·p.z) + cy` (pixels) and flagged 0x10000
x < x0, 0x20000 x ≥ x1, 0x40000 y < y0, 0x80000 y ≥ y1; the screen box 0xfc..0x108 is the min / max (truncated).
If every corner shares a flag the object is culled (return 1). The flags are added into [0xc4] bits 16..20.

**0x41a458 back faces** — with flag 0x100 every vertex gets bit 1 and every face bit 1 unless the face is hidden
(face bit 0: then its bit 1 is cleared). Otherwise all vertices' bit 1 is cleared and the camera is taken into
object space, `eye = obj.world_rot^T · (cam.world_pos − obj.world_pos)` (doubles), and per face:
```
if (double-sided)                visible
else if (hidden)                 not visible
else visible = ((eye.y − p0.y)·n.y + (eye.x − p0.x)·n.x) + (eye.z − p0.z)·n.z >= 0     (p0 = the face's first vertex)
visible faces mark their three vertices (bit 1)
```
There is no screen-space orientation test in the path INSIDE uses: a front face draws whatever its winding on
screen.

**0x41af78** — 0x41afa0 projects the marked vertices, then the face list: 0x41a9d4 (the clipping variant). The
non-clipping variant 0x41aacc (screen-space back-face test and the face's *farthest* vertex as key) is never used,
because 0x41afa0 always sets [0xc6] bit 4 (near clipping) before testing it.

0x41afa0, for every vertex with bit 1 (the operation order matters for the truncations; `engine.project`):
```
c = 1/256;  xsc = XS·c;  ysc = YS·c;  P = 2048·xsc;  Q = 2048·ysc (stored as a double)
m0..m2 = M row 0 · P;   m3..m5 = M row 1 · Q;   m6..m8 = M row 2 · 128            (doubles)
Cz = −m6·m7 + T.z;   Cx = 16·T.x·xsc + (−m0·m1);   Cy = 16·T.y·ysc + (−m3·m4)
per vertex (x, y, z ints):
  Z  = (((x + m7)·(y + m6) + Cz) − x·y) + z·m8          = 128·zc        (Winograd's form of the dot product)
  X' = (((x + m1)·(y + m0) + Cx) − x·y) + z·m2          = 8·XS·xc
  Y' = (((y + m3)·(x + m4) + Cy) − x·y) + z·m5          = 8·YS·yc
  v.z = trunc(Z);  v[0x20] = trunc(X') / 2048;  v[0x24] = trunc(Y') / 2048        (C division)
  if (near > v.z) flags = 0x100000
  else { q = 2 / Z;  v.sx = trunc(X'·q + 16·cx);  v.sy = trunc(Y'·q + 16·cy);  clip flags against 16·viewport }
  vertex flags byte 2 = flags, bit 1 cleared
```
So `sx = 16·(cx + (XS/128)·xc/zc)`, `sy = 16·(cy + (YS/128)·yc/zc)` with xc, yc, zc the camera-space coordinates in
object units: the projection of the "Units" section, truncated to 1/16 pixel.

0x41a9d4, for every face with bit 1, in face order:
```
or  = (f0 | f1 | f2) & 0xff0000;  and = (f0 & f1 & f2) & 0xff0000     (vertex clip flags)
if (or & and) skip                                   // all three outside one edge, or all behind the near plane
mark the three vertices (bit 1) and the face (bit 20: near clipping check in the rasterizer)
key = trunc((z0 + z1 + z2) · 0.333333333333) − obj[0xcc];  key /= 16 (C division);  if (key < 0) key = 0
put the face at the head of [0x4b8fc0]
```
The key is the face's **average camera z** in 1/8 object units minus the object's bias. Faces nearer than the
bias all get key 0.

**0x41ae9c** (flag 0x400, unused): per front face `iu = trunc(n·M0/16 + 128)`, `iv = trunc(n·M1/16 + 128)` (n = face
normal, M0, M1 = rows 0 and 1 of [0x4b8f78], products summed (nx·m + ny·m) + nz·m), face+0x2c = (iu & 255) +
256·(iv & 255).

**0x41adcc** (flag 0x800, unused): per used vertex, with U = n·M0, V = n·M1 (vertex normal, same summation order):
eu = (−trunc(2U + 2048)) & 0xfff, ev = trunc(2V + 2048) & 0xfff.

**0x41acc4** (flag 0x1000; flower, flight, credits, water, temple): per used vertex, with U = (2n)·M0, V = (2n)·M1
(2n = the normal doubled as ints):
```
u = −(trunc(U + 2048) & 0xfff) − v[0x20]/64          (C division)
v =  (trunc(V + 2048) & 0xfff) + v[0x24]/64
```
In texture fractions (4096 = one texture): the normal term is 2048 ± 1638 (the centre ± 0.4 of the texture: the
camera-space normal's x, y, the x term mirrored), and v[0x20]/64 = trunc(X')/2048/64 ≈ XS·xc/16384 = fov·xc/128
(W = 320; likewise YS·yc/16384 for v) adds the vertex's camera-space x, y — not divided by z — so the texture slides
as the object moves across the view. The words wrap (the texture tiles).

**0x41aba4 lighting** (flag 0x40): every used vertex's shade = the scene's ambient word; then for every object of
the scene list with flag 0x80 and without flag 1 (a light), 0x41b508:
```
if (trunc(|obj.pos − light.pos|) > obj.radius + light.R) skip     // the *own* positions 0x10..0x18, 0x41063c
L = obj.world_rot^T · (light.world_pos − obj.world_pos)            // light in object space
for each used vertex p (normal n):
    d = L − p;  dist = sqrt((d.y² + d.x²) + d.z²)
    if (dist > R) continue
    dot = (n.y·d.y + n.x·d.x) + n.z·d.z;   if (dot < 0) continue
    q = dot / (dist·512)
    a = (dist < 1) ? I : I − dist·(I/R)
    c = a·q;   if (c > 0) shade = trunc(shade + c)                 (16-bit)
    if (shade > 992) shade = 992
```
With |n| = 819.2, `c = I·(1 − dist/R)·1.6·cos θ`: a point light with linear falloff to 0 at R and Lambert's
cosine. Lights are applied in list order with a truncation after each. Light levels: shade/16, 0..62.

**0x41ac04** (flag 0x200, unused): u = (−trunc(U + 2048)) & 0xfff, v = trunc(V + 2048) & 0xfff (U, V as for
0x41adcc without the factor 2).

### Sorting: 0x40c384

A three-pass LSD radix sort of the frame's list over key bytes 0, 1, 2 with 256 linked-list buckets per pass; every
item is put at the head of its bucket's list; the first pass uses bucket 255 − byte 0, the others the byte itself;
passes walk the buckets from 0 to 255; at the end the buckets of the last pass are joined from 255 down to 0. The
result: **key descending (far first)**; equal keys in the order objects come in the scene list (first object
first), and within an object in ascending face index. Items are drawn in this order with no depth buffer
(painter's algorithm); the per-object bias [0xcc] moves whole objects forward or back (flower petals 0x35200 =
1700 units in front of the tunnel, water −0x3e800, temple, shadow scene ...).

### Near clipping and the triangle setup (every rasterizer starts with 0x41be3c, 0x41be91)

0x41be3c: EDX = material, ESI/EDI/EBX = the face's three vertices. If material flag bit 0: the material's
0x20..0x34 are stored into the three vertices' u, v. If face bit 20 (always): **0x41c0e0** counts vertices with
z < near:
- 0: draw normally; 3: draw nothing.
- 2 behind: sort by z descending (A in front, B, C behind); the points on A→B and A→C at z = near are made (below);
  draw (A, AB, AC).
- 1 behind (C): A, B in front; draw (A, B, AC), then (B, AC, BC).

A clipped point between s (in front) and e (behind), 4.12 fixed weights:
```
t = (s.z − near)·trunc(2^30/(s.z − e.z)) >> 18           (0 if s.z == e.z; t·(1/4096) is the parameter from s)
K = trunc(2^30 / (near >> 8))
sx = ((((t·e.X20) >> 12) + (((4096 − t)·s.X20) >> 12))·K >> 26) + 16·cx       (X20 = vertex+0x20; same for sy)
u, v, eu, ev, shade, shade2 = (t·e + (4096 − t)·s) >> 12;   z likewise
```
i.e. the projection of the point at z = near. The two pieces are drawn by the face's own rasterizer through two
scratch faces (0x4b953c, 0x4b9508; scratch vertices 0x4b9408..0x4b9507) with bit 20 cleared and material bit 0
masked.

**0x41be91 setup** (sx, sy in 1/16 px; returns ECX = −1 to reject):
```
sort the vertices by sy: A (top), B, C (bottom);  H = C.sy − A.sy;  reject if H <= 0 or H >= 0x8000
reject if (C.sy >> 4) < vy0, (A.sy >> 4) > vy1, (min sx >> 4) > vx1, (max sx >> 4) <= vx0, x extent >= 0x8000 px
dl = trunc(((C.sx − A.sx) << 12) / H)                   long edge, 16.16 px per 1/16 px
dd = trunc(((C.sx − B.sx) << 12) / (C.sy − B.sy))       lower short edge (0 if B.sy == C.sy)
du = trunc(((A.sx − B.sx) << 12) / (A.sy − B.sy))       upper short edge (0 if A.sy == B.sy)
W  = ((dl·(B.sy − A.sy)) >> 8) + ((A.sx − B.sx) << 4)   long edge minus B at B's height, 1/256 px
     (if A.sy == B.sy: W = (A.sx − B.sx) << 4);   reject if W == 0
tmid = trunc(((B.sy − A.sy) << 16) / H);   rW = trunc(2^30 / W);   rH = trunc(2^30 / H)
P = 15 − (A.sy & 15)  (+ 16·(vy0 − (A.sy >> 4)) if the top is above the viewport)
row r runs from max(A.sy >> 4, vy0) to min(C.sy >> 4, vy1) − 1; rows r < (B.sy >> 4) use the upper edge
xl = (A.sx << 12) + dl·P;  xu = (A.sx << 12) + du·P;  xd = (B.sx << 12) + dd·(A.sy − B.sy + P);  steps ·16 per row
span: left = (W > 0 ? short : long) >> 16, right = (W > 0 ? long : short) >> 16 (floor), clamped to vx0 / vx1;
      pixels left .. right − 1
```
**Sub-pixel convention**: row r samples the edges at y = 16r + 15 (r + 15/16 px) and covers column c when
`left_edge < c + 1 <= right_edge`, i.e. the pixel (c, r) shows the point (c + 1, r + 15/16) of the engine's screen
(whose projection centre is (160, 100)). Rows are r = floor(A.sy) .. floor(C.sy) − 1 (top-left fill rule in that
shifted grid). A renderer that samples at pixel centres reproduces this by moving the projection centre by
(−0.5, −0.4375) original pixels: cx = 159.5, cy = 99.5625.

**Attribute gradients** (all rasterizers, all channels; `engine.gradients`), for an int16 attribute a with values
a0 (A), a1 (B), a2 (C), 32-bit wrap-around except where noted:
```
e  = (a2 − a0)·tmid + ((a0 − a1) << 16)                 a(long edge at B's height) − a1, ×65536
gx = (e·rW) >> 26  (64-bit product)                     da/dx ×4096 per pixel
t  = ((a2 − a0) << 8) − ((gx·(C.sx − A.sx)) >> 8)
gy = (t·rH) >> 22                                       da/dy ×4096 per row
row0 = (gy·P − gx·(A.sx − 16) + ((a0 + 1) << 16)) >> 4  (64-bit)     value at x = 0 of the first row
pixel (c, r): acc = row0 + gy·(r − r_first) + gx·c;  the integer part is acc >> 16 (& 0xff)
```
So every channel is interpolated **linearly in screen space** (affine), evaluated at the sample point
(c + 1, r + 15/16), with a bias of +1 unit (1/4096 of the texture for u, v — 1/16 texel of a 256-texel texture —
and 1/16 level for shades): texel = floor((u + 1)·2^wlog/4096), level = floor((shade + 1)/16). The perspective path (face bits 10..12: exact 1/z at every 2^(n−1)-th pixel, linear
in between, using 2^28/z per vertex) exists in every textured rasterizer but no face in INSIDE enables it.

### The six rasterizers (face+8)

Texture addressing: `texel = tex[((u_acc >> (24 − wlog)) & umask) + (((v_acc & (vmask << (24 − hlog))) >> (24 − hlog − wlog)))]`
= tex[(u & umask) + (v & vmask)·2^wlog] with u, v = the integer texel coordinates (acc >> 16 for 256 × 256,
acc >> 15 for the flight's 512 × 512 map); u_acc = u × 4096 (the 12-bit fraction × 4096).

| rasterizer | channels | pixel index | used by |
|---|---|---|---|
| 0x412db0 | u, v | tex[u, v] | flight (ship), flower, shadow scene, temple, credits, water |
| 0x414e5c | u, v, shade | table1[level·256 + tex[u, v]] | logo, temple, credits, shadow scene, flower |
| 0x412264 | u, v, shade, shade2 | table2[level2·256 + table1[level·256 + tex[u, v]]] | flight landscape (map.shd, map.fog) |
| 0x41669c | u, v | table1[tex[u, v]·256 + background] (transparency, .int) | temple, shadow scene, water |
| 0x416dc8 | shade | table1[level·256 + background] (darkens / lightens what is there) | temple |
| 0x4172e0 | u, v, eu, ev | table1[tex2[eu, ev]·256 + tex[u, v]] (tex2 = material+4, indexed (eu & 255) + 256·(ev & 255)) | shadow scene (0x4054ab) |

level = (shade_acc >> 16) & 255, level2 likewise from channel 0x32. The tables are 256-entry rows: a .shd row per
light level (e.g. logo.shd: 64 rows), an .int row per source colour.

## Billboards (0x415790; pipeline 0x42381c → 0x4157d4; draw 0x415dbc / 0x415dc8)

A billboard is an object (0x104 bytes) whose geometry is a bitmap: [0xd8] pixels, [0xdc] blend table, [0xe0] pitch,
[0xe4] width w, [0xe8] height h (texels), [0xec] float s (scales the position only, 1.0), [0xf0] float size (units
per texel), [0xf4] int depth bias, [0xd0] draw function: 0x415dbc opaque, 0x415dc8 through the table. Its render
item is at obj+0xc8 (item+8 = [0xd0]).

0x4157d4 (per render, unless hidden): 0x40c158 → T; floats [0xf8..0x100] = T; skipped if T.z < 0; key =
trunc([0xf4] + T.z)/16 (≥ 0) — the centre's depth plus the bias; the item joins the polygon list.

0x415878 (draw):
```
z = T.z/128;  return if trunc(near/128) > z or far < z
X = s·T.x·XS/1024/z;  Y = s·T.y·XS/1024/z        (both with XS: the vertical position ignores the 0.833 aspect)
Wd = w·size·XS/z;  Hd = h·size·YS/z              (1/16 px; return if <= 0)
y0 = trunc(16·cy + Y − Hd/2), y1 = y0 + trunc(Hd);  x0 = trunc(16·cx + X − Wd/2), x1 = x0 + trunc(Wd)
du = 16w / trunc(Wd), dv = 16h / trunc(Hd)       texels per pixel ×16 / 16
clip to the viewport (start offsets −(clipped 1/16 px)·d/16), rows y0/16 .. y1/16 − 1, columns x0/16 .. x1/16 − 1
first column samples at 16c + 15 (as the triangles): u0 = (15 − (x0 & 15))·du/16, v0 = (15 − (y0 & 15))·dv/16
per row: src row = trunc(v), u stepped in 16.16 (trunc(u·65536), trunc(du·65536)); v += dv per row
pixel = src (opaque, index 0 included) or table[src·256 + background]
```
Note the vertical offset uses XS where the size uses YS: a billboard away from the screen centre sits 1.2× too far
up or down relative to the triangles' projection (a quirk of the original: keep it). Verified: the billboards of
the flight (sun), credits (smoke) and temple (flares) frames come out pixel-exact with this formula.

## 2D bitmaps (0x415e50, 0x415df0)

Bitmap (0x28 bytes): [0] type 1..4, [4] 1.0f (unused), [8] pitch, [0xc] width, [0x10] height, [0x18] pixels,
[0x20] table, [0x24] transparent key (0). 0x415df0(bitmap, float x, float y) draws into [0x4b6ec0] (pitch
[0x4b6ec4]) at X0 = trunc(x), Y0 = trunc(y), X1 = trunc(width + x), Y1 = trunc(height + y), clipped to the screen
clip window of 0x40a83c ([0x4b6eb8] x0, [0x4b6ebc] y0, [0x4b6eb0] x1, [0x4b6eb4] y1), no scaling:

| type | routine | pixel |
|---|---|---|
| 1 | 0x415fe8 | src (row copies) |
| 2 | 0x416174 | table[src·256 + dst] |
| 3 | 0x4164ec | src where src ≠ key |
| 4 | 0x416324 | table[src·256 + dst] where src ≠ key |

The logo draws logo/inmask.raw (253 × 147) as type 4 with logo/inmask.shd at (31, 9) and logo/inside.raw
(249 × 139) as type 3 at (34, 13) (verified).

## The .sro object format (INSIDE.DAT, 0x410bb0)

```
+0x00 dword   (ignored: the file size, or 0x1df020 in files with a 0x20-byte header)
+0x04 dword   offset of the vertices (0x14 or 0x20)
+0x08 dword   number of vertices nv
+0x0c dword   offset of the faces (= +0x04 + 12·nv; the loader ignores it: faces follow the vertices)
+0x10 dword   number of faces nf
vertices, 12 bytes: int16 x, y, z; uint16 u, v; uint16 (unused, 0)
faces, 8 bytes: uint16 (ignored, 0); int16 a, b, c (vertex indices)
```
Loaded: x = trunc(x·scale/65536) (likewise y, z); u = (u >> 4) (u_file/65536 of the texture's width); v = −(v >> 4)
(so the texture is flipped vertically: v_file/65536 of its height measured upward, wrapping); face = (vertex a, b, c) with the scene's material and draw function 0;
face flags 0; vertex shade not initialised. Files: logo/obuke.sro (130 v, 256 f), credits/tuubi.sro,
credits/piikkis.sro, flight/ship.sro, temple/mesta21..23.sro, temple/putki.sro, nigga/poksi.sro, water/fish.sro,
water/obuke.sro, flower/tunneli1.sro, flower/kukka1*.sro, flower/kukka2*.sro.

## The LightWave scene reader (0x408030..0x409358; flower/tunnel1.lws)

0x408030(EAX name, stack: float scale = 3000.0) with [0x422868] = c = 0.0 (spline tension): loads the text (the
byte at offset 0xa46, the last of the file's 2631, is set to 0 to end the text), parses it with 0x408480 into the
table 0x426618 (0x4800 bytes = 512 rows of 9 floats per motion), [0x4b6854] = number of motions, [0x4b6858] = N
(the camera motion's row count). Errors print "scene file not found" / "not a scene file" / "scene read error".

**Parsing** (0x408480): the text must contain "LWSC"; FirstFrame and LastFrame are read (integers), then
"CameraMotion" → motion 0, then the cursor goes back to just after LastFrame and every "ObjectMotion" in file order
→ motions 1, 2, ... (the light's motion is not read; neither are ZoomFactor, the object names, ShowObject, ...).
Numbers (0x408d70 integers, 0x408dfc floats): skip to the next digit or '-', read digits (float32 arithmetic:
value·10 + digit; fraction digits counted in a float32 divisor; 'e' takes a sign and exactly three digits:
10^e by 0x417e28).

**A motion** (0x408588): the channel count must be 9, then the key count n and per key 14 numbers: x y z H P B
sx sy sz frame, and linear tension continuity bias, which are **ignored** (every key is a plain Catmull-Rom point).
Keys are slots 1..n; if the first key's frame ≠ FirstFrame a copy of it at FirstFrame is inserted before it; if the
last key's frame ≠ LastFrame a copy at LastFrame is appended; three copies of the last key with frame + 1 are
appended; slot 0 = slot 1. Then every segment (slot j, slot j+1), j = 1..n, is sampled at
`steps = trunc(frame[j+1] − frame[j])` points t = 0, 1/steps, 2/steps ... (float32 accumulation; segments with
steps ≤ 0 give nothing) with the spline 0x4090cc through slots j−1, j, j+1, j+2:
```
f(t) = a·p0·(−t³ + 2t² − t) + p1·((2 − a)t³ + (a − 3)t² + 1) + p2·((a − 2)t³ + (3 − 2a)t² + a·t) + a·p3·(t³ − t²)
a = (1 − c)/2 = 0.5  (Catmull-Rom)
```
Each sample row (float32): x·scale, −y·scale, z·scale, P·k, H·k, B·k, sx, sy, sz with k = 0.017452777777777779
([0x42149d], a slightly wrong π/180 = 0.0174532925: angles are 0.003% small). Row 0 = row 1; two more rows repeat
the last. For tunnel1.lws: FirstFrame 1, LastFrame 265, the camera's first key is at frame 0, so a copy is put at
frame 1 before it (segment 1 → 0 has no steps) and N = 266 rows (frames 0..265).
EndBehavior is read but both branches do the same.

**Evaluation** (0x4080d8, every frame of the flower part, at the float [0x4b685c]):
```
i = trunc(f);  k = i % N;  t = f − i
value = spline(t, c, row[k], row[k+1], row[k+2], row[k+3])     (each channel, stored as float32)
object (table 0x4b6618[motion]): position = trunc(x, y, z) (0x410868);
rotation = I · Ry(H) · Rx(−P) · Rz(−B)  (0x4108dc zeros, then 0x4145ac H, 0x4145c4 −P, 0x4145dc −B)
motion types [0x4b6698 + 4·m] == 2 also scale: 0x409258(obj, saved geometry 0x409164, sx, sy, sz): vertices and
bounding box = trunc(saved·s) per axis, radius = trunc(saved radius · max(sx, sy, sz)) (0x409214)
```
Row 1 is the sample at the motion's first key frame (frame 0 in tunnel1.lws, whose first keys are at 0), so the
part's frame f is LightWave frame f for this file; the motion repeats every N frames, and between whole frames the
sampled rows are interpolated with the same spline. The y axis is negated (LightWave y up,
the engine's y down) and the rotations are applied heading, pitch, bank with pitch and bank negated.

## What the remake should do (shortcuts to evaluate exactly)

- **Geometry**: use the projection above (centre (160, 100) minus the sampling offset (0.5, 0.4375), scales
  fov·W/320 and fov·H/240 per unit of x/z, y/z) on the 320×200 frame shown at 4:3; positions are ints in
  object space, transforms in doubles: the remake can use floats without the int truncations (vertex positions
  after load scaling, camera-space ×128, screen 1/16 px, normals as ints of length 1024 / 819.2 are all
  quantisation).
- **Near plane**: triangles are clipped at camera z = near/128 (logo 96 units, default 64); there is no far plane.
  The new vertices are projected at exactly z = near (a GL near plane at the same distance does the same).
- **Visibility**: painter's order by average depth minus the object's bias, back-face culling in object space
  (dot ≥ 0 is visible), no depth buffer. Interpenetrating faces and the biases (flowers in front of the tunnel,
  etc.) decide what is visible, so sort the triangles the same way (keys as above, descending, stable in face
  order) rather than using a depth test; a depth test would change the image wherever the bias or the average
  depth puts a farther face in front.
- **Interpolation**: u, v and the shade are interpolated linearly in screen space per triangle (affine; INSIDE never
  enables the perspective subdivision). On large triangles near the camera (the logo object surrounds the
  camera) the texture visibly bends at the triangle edges; this is part of the look of the original. A remake that
  wants the same image uses `noperspective` interpolation of u, v and shade (and clips at the same near plane, so
  the split triangles are the same); perspective-correct mapping would be "sharper" but changes the warping.
- **Texels and levels**: nearest texel (u, v as fractions of the texture, +1/4096) with wrap-around, and
  floor((shade+1)/16) as the table row:
  evaluate continuously: filter the texture after mapping indices to colours (look up each texel's colour through
  the shade table at the continuous level, interpolating between the two nearest levels).
- **Light**: per-vertex point light with linear falloff (`I·(1 − d/R)·1.6·cos θ` + ambient, clamp 992, level =
  shade/16), Gouraud; the remake can evaluate it per pixel with the same formula only if it accepts a different
  look on big triangles — Gouraud on the original vertices matches the original.
- **Edges**: 1/16 px vertex positions and integer spans; draw anti-aliased geometry instead.

## Verification

All checks run the original in `models/engine/run_engine.py` (a copy of run_inside.py with code hooks and the
snapshot-resume fix; its frames are identical to full.frames) and compare the model with the frame buffer right after
the original's render call. Commands in `models/engine/README.md`.

1. **Logo, every frame** (`hooks_logo.py`, `logo_model.py --final`): from logo/obuke.sro, the object's rotation
   matrix, the camera, the light (R 550, I 1600 at the object's centre) and the scene (fov 42, near 0x3000) the
   model's vertex screen positions, camera z, projection numerators, shades and sort keys equal the original's for
   every used vertex and face, and 327 rendered frames (refreshes 1446..1728, before and after the text appears)
   are identical, 0 pixels differ, also in the finished frames with the mask (type 4) and text (type 3) bitmaps.
2. **Other parts, sampled frames** (`hooks_scene.py`, `scene_model.py`; the model gets each object's own position,
   rotation, parent, flags and bias, the vertex data the parts animate, the materials and textures and the buffer
   before the render; it computes everything else), 0 pixels differ in every frame:
   | part | frames | what it exercises |
   |---|---|---|
   | flower (4964..5188) | 15 | objects parented to the camera, the 0x1000 stage, 0x412db0, 0x414e5c with animated fixed-u, v materials, biases between objects, bounding-box culling |
   | flight (7064..7398) | 42 | 0x412264 with two tables on a 512 × 512 texture, 0x414e5c, 0x412db0 + 0x1000 (ships), flags 0x100 / 0x2000, the sun billboard |
   | credits (2150..2684) | 20 | 0x414e5c (flag 0x100), blended billboards (smoke), a camera turned by 0x41077c |
   | shadow scene (4256..4586) | 12 | 0x4172e0 (two textures), 0x41669c, 0x412db0 lit by a light (0x40), hidden objects |
   | temple (11290..11691) | 25 | 0x416dc8, 0x41669c, 0x414e5c lit, 0x412db0, 0x1000, flare billboards, a camera with a parent |
   | water (8470..8787) | 15 | 0x412db0 + 0x1000 (water/obuke.sro), fish with 0x41669c + 0x1000 and bias −256000 |
3. **LightWave reader**: `lws.py ../../snaps/f38.snap`: the sampled motion table of flower/tunnel1.lws (2 motions,
   N = 266) equals 0x426618 in memory, all 538 rows bit for bit; `lws_check.py`: the camera's and the tunnel's
   positions and rotation matrices evaluated at the part's frame variable [0x4b685c] equal the original's (30 object
   states, bit for bit).
4. `sro.py`: vertices (positions, u, v), face normals and vertex normals of logo/obuke.sro equal the loaded object's.

## Problems found in the shared files

- NOTES.md, "Runtime model" / BRIEF: the FPU precision is 53 bits (control word 0x127f), not 64.
- run_inside.py: a run resumed with `--load --snap` never advances time: `next_frame` starts at FRAME (frame 0)
  instead of the snapshot's frame, so the main loop only records the same refresh over and over. The engine copy
  sets `next_frame = FRAME·(st['frame'] + 1)` after loading; with that, the resumed logo run matches full.frames.
- BRIEF.md: the engine code is not only 0x410520..0x417f00: the render pipeline is at 0x40c034..0x40c6e4 and the
  object pipeline, triangle setup and near clipper at 0x41a440..0x41c9a0 (between C library modules).

## Not determined / not modelled

- The perspective-correct path of the rasterizers (face bits 10..12) is decoded only in outline: INSIDE never
  enables it.
- 0x41077c itself was not run against the capture (the credits' camera rotation was taken from the dump; the
  matrix routine 0x414cec it uses was checked by calling it). The credits' camera rotation in the dumps has a
  non-zero r0.y, so the credits part changes the rotation after 0x41077c (credits scope).
- Unused by INSIDE and only outlined: stages 0x41ac04, 0x41ae9c, 0x41adcc (flags 0x200, 0x400, 0x800), the
  non-clipping face list 0x41aacc, 0x4111e0 (removing an object with its children), 0x410714 / 0x410800 (flatten a
  transform, look at an object), 0x411f68 (a line between two projected points), 0x4120a0 (vertex lerp), 0x411764 /
  0x41188c / 0x411988 / 0x4119f4 / 0x411a08 / 0x411a1c / 0x411ab4 / 0x411ae8 (centre, unit size, flip normals, list
  helpers, nearest vertex, size, copy), 0x41b4d4 (a prebuilt polygon list).
