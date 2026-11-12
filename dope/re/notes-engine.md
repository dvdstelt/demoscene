# DOPE — scope "engine": the 3D engine of p2, p3, p5, p6, p7

Documented from p2; section 2 gives every routine's address in each part and section 12 lists where the copies
differ. Addresses are `SEG:OFF` in `unpacked/<part>.bin` (file offset = SEG*16 + OFF); a bare 4-digit address is
in segment 0010 (`cs`). Memory read at run time holds segment values relocated by the load segment (0F64 in the
emulator); the tables below give image segments.

What was checked (scripts in `models/engine/`, section 14):

- **A bit-exact Python model of the whole frame build** (object loop, matrix, vertex transform and projection,
  face set-up, radix sort, both triangle fillers with their clipped and unclipped span loops) reproduces, from a
  memory dump taken where the frame build starts, every per-vertex array (texture, env, screen, depth), the face
  list, the sorted key list and **the render target with 0 differing pixels** in 19 frames: p2 3400, 5900, 6600,
  7500, 8700, 9701; p3 10500, 12000, 13300; p5 21600, 22300, 23400, 24800; p6 26000, 27500, 28900; p7 30000,
  30900, 31400. They cover every texel shift (8, 9, 10), both fillers (texture+env and env only), clipped spans,
  triangles crossing the top edge, triangles thousands of pixels off screen.
- The frame copies (p2's blur blend, label, credit strip; the plain copy of the other parts) reproduce the screen
  (A000) and the cleared target with 0 differences in all 19 frames, and those screens equal the `full.frames`
  records of the same frames (checked for p2 7501, p5 23401, p7 30001: 0 differing pixels).
- The float form of the transform (section 11.1) agrees with the integer results to rounding (mean 0.35 px,
  max 1.9 px; env coordinates within 0.22 texel).

-----------------------------------------------------------------------------------------------------------------

## 1. What the engine draws

Polygon meshes (triangles) with **environment-mapped ("phong") shading**: every vertex carries a normal; the
normal rotated into view space, x and y only, picks a point in a 128 x 128 map of 8-bit colour indices (the
"env map", which holds the whole lighting: a highlight on a ramp). Optionally a second map (the "texture", in
practice a bump map) is read with per-vertex texture coordinates and its texel, through a 256-entry table, shifts
the env-map lookup by up to about +-8 texels ("BUMP"). There is no other lighting, no shade table, no z-buffer:
faces are back-face culled, radix-sorted by depth and drawn back to front into a 320 x 201 byte buffer, which the
frame copy puts on screen. Objects are rigid meshes from the part's image (never changed at run time, checked);
they move by per-retrace velocities.

| part | meshes (image segment: vertices / faces) | objects seen in the dumps |
|---|---|---|
| p2 | 1e71: 250 / 500 (the two discs), 2121: 432 / 864 (the "C" and its stand) | discs f = 4000, C f = 1300 |
| p3 | 1362: 514 / 1024 (the lumpy object: 1024 triangles, not 7800) | one object, f = 2500 |
| p5 | 1402: 432 / 864 (the star / torus shape) | one object, f = 2500 |
| p6 | 1cc2: 352 / 671 (the cat), 202d: 256 / 512 (the rings) | both, f = 3500 |
| p7 | 23b4: 1446 / 2888 (the tentacle object) | one object, f = 2500 |

## 2. Routines per part

All in segment 0010. "—" = not present.

| routine | p2 | p3 | p5 | p6 | p7 |
|---|---|---|---|---|---|
| "run N retraces" frame loop (CX = N) | 371e | 2ba4 | 2b66 | 2de6 | 3070 |
| object loop | 354a | 2a5a | 2a17 | 2c9c | 2eca |
| per object | 348b | 299b | 2958 | 2bdd | 2e0b |
| rotation matrix | 1d86 | 1df6 | 1906 | 1b56 | 1dd6 |
| vertex loop (transform, projection) | 1c60 | 1cd0 | 17e0 | 1a30 | 1cb0 |
| vertex rotate | 1b60 | 1bb0 | 16b0 | 1930 | 1b90 |
| face set-up (culling, keys, materials) | 3377 | 2887 | 2844 | 2ac7 | 2cf7 |
| radix sort | 3585 | 2a95 | 2a52 | 2cd9 | 2f05 |
| draw loop | 3310 | 2820 | 27e0 | 2a60 | 2c90 |
| triangle set-up, texture+env | 2881 | 2211 | 1d51 | 1fd1 | 2201 |
| span loop, texture+env (clipped variant at +f0) | 2660 | 1ff0 | 1b30 | 1db0 | 1fe0 |
| triangle set-up, env only | 3030 | — | 2500 | 2780 | 29b0 |
| span loop, env only (clipped variant at +b0) | 2ea0 | — | 2370 | 25f0 | 2820 |
| frame copy | 360c | 2b1c | 2adc | 2d5c | 2f8c |
| per-retrace routine (motion) | 2580 | 1f60 | 1a70 | 1cc0 | 1f40 |
| sine table | 19ce (from 3765) | 19f0 | 1584 | 179b | 19f4 |
| palette scale table | 2550 | — | 2f99 | 3871 | — |
| bump table generator | — (data) | — (data) | 3099 | 3913 | — (data) |
| blend tables | 1967, 1998 | — | — | — | — |

Frame loop calls: p2 354a, 3585, 3310, 360c; p3 2a5a, 2a95, 2820, 2b1c; p5 2a17, 2a52, 27e0, 2adc; p6 2c9c,
2cd9, then part code (2e05..2e66 rewrites half of an env map per frame, 38ca palette morph), 2a60, 2d5c, mouse;
p7 2eca, 2f05, then part code (env map rebuilt from 03b3 into 13b4 and five light spots by 3046), 2c90, 2f8c. After
the copy each loop counts a drawn frame and checks the keyboard (core scope).

Per-part variables and buffers (cs offsets unless a segment is named):

| | p2 | p3 | p5 | p6 | p7 |
|---|---|---|---|---|---|
| object list, first .. last drawable record | 1810..18be | 1810..1932 | 1412..14c0 | 1620..16ce | 1810..1932 |
| work segment (per-frame arrays, 2.5) | 2ccd | 343d | 2685 | 667b | 3de1 |
| render target | 4527 (fixed) | [19d0] | [1564] | [177a] | [19d4] |
| env / texture segment tables (4 words each) | 0400 / 0408 | 0400 / 0408 | 0000 / 0008 | 0210 / 0218 | 0400 / 0408 |
| bump table (256 words) | 0000 | 0000 | 375b | 0010 | 0000 |
| sine table (2560 words) | 0410 | 0410 | 0012 | 0220 | 0410 |
| material adders (env / texture, bytes) | 194f / 194d | 19be / 19bc | 1550 / 154e | 1762 / 1760 | 19c0 / 19be |
| vertex / visible-face counters | 1931 / 1933 | 19a2 / 19a4 | 1530 / 1532 | 173e / 1740 | 19a2 / 19a4 |
| ready flag / retrace counter / drawn frames | 1924 / 1928 / 192c | 1996 / 199a / 199e | 1524 / 1528 / 152c | 1732 / 1736 / 173a | 1996 / 199a / 199e |
| label x / label speed / label picture | 1955 / 1957 / 2bd2 | 19c4 / 19c6 / 3342 | 1558 / 155a / [1556] | 1768 / 176a / [176c] | 19c8 / 19ca / [19c6] |
| background picture (restored after each copy) | 0ec3 when [1947] = 1, else zeros | 4bb1 | 3f00 | 0c97 | 5ab4 |

The segment tables at p2/p3/p7 0050:0000, p6 0031:0000, p5 0010:0000 are the "far pointer tables" of NOTES.md;
their initial values (relocations) are in section 7.1.

## 3. Data structures

### 3.1 Object record (0x3a bytes; list at `objs`, slot 0 = camera)

| offset | type | meaning |
|---|---|---|
| +00 | u16 | mesh segment; 0 ends the list; FFFF = no mesh (slot 0, the camera) |
| +02 | s16 | non-zero = object off (neither drawn nor moved) |
| +04 | s16 | f: projection factor of this object (section 5.3); also scales its depth keys |
| +06, +0a, +0e | s32 | position T (x, y, z), units of 1/128 mesh unit (section 5.3) |
| +12, +16, +1a | s32 | velocity added to the position every retrace |
| +1e, +20, +22 | u16 | angles a1, a2, a3 (65536 = 360 degrees) |
| +24, +26, +28 | s16 | angular velocity added every retrace (16-bit wrap) |
| +2a | u16 | material adders: low byte added to the env selector, high byte to the texture selector |
| +2c | s16 | culling: 0 = draw faces with non-negative screen cross product, else draw the negative ones |
| +2e, +32, +36 | s32 | pivot p, mesh units, added to every vertex before the rotation |

The camera slot (p2 1810: position 1816/181a/181e, velocity 1822/1826/182a, angles 182e/1830/1832, angular
velocity 1834/1836/1838 — the "camera variables" the scripts set) is not a camera transform: its position is
**added** to every object's position and its angles are **added** to every object's angles before the matrix is
built (Euler angles summed, 16-bit wrap). Its f, material, culling and pivot fields are unused.

The object loop (354a) walks from slot 0 until a record whose +00 is 0, skipping FFFF; the per-object routine
(348b) ignores records beyond `objlast` and records with +02 != 0 or +00 = 0. Drawable objects: p2, p5, p6 three
(slots 1..3), p3, p7 five (slots 1..5).

### 3.2 Mesh (at segment S of the part's image; all words little-endian)

```
S:0000  u16 nv          vertex count
S:0002  u16 nf          face count (0: the object is skipped entirely)
S:0004  u16 offPos      nv x {s16 x, y, z}              positions, mesh units (|xyz| <= 8191 in all meshes)
S:0006  u16 offFace     nf x {u16 attr, u16 A, B, C}    corners as vertex index * 4; attr low byte = env
                                                         selector, high byte = texture selector (0 in all meshes)
S:0008  u16 offNorm     nv x {s16 nx, ny, nz}           vertex normals, length 27558 (+-1) in every mesh
S:000a  u16 offTex      nv x {u16 v, u16 u}             texture coordinates (low word v, high word u; all
                                                         values 0x2500..0x7fff); p3 and p7 ignore them (5.2)
```

The header is 16 bytes and the arrays follow in the order positions, faces, normals, texture (offsets 0010,
0010 + 6 nv, ...). Meshes, with file offsets in `unpacked/<part>.bin`:

| part | segment (file offset) | bytes | nv | nf | offsets pos / faces / normals / tex |
|---|---|---|---|---|---|
| p2 | 1e71 (0x1e710) | 8016 | 250 | 500 | 0010 / 05ec / 158c / 1b68 |
| p2 | 2121 (0x21210) | 13840 | 432 | 864 | 0010 / 0a30 / 2530 / 2f50 |
| p3 | 1362 (0x13620) | 16432 | 514 | 1024 | 0010 / 0c1c / 2c1c / 3828 |
| p5 | 1402 (0x14020) | 13840 | 432 | 864 | 0010 / 0a30 / 2530 / 2f50 |
| p6 | 1cc2 (0x1cc20) | 11016 | 352 | 671 | 0010 / 0850 / 1d48 / 2588 |
| p6 | 202d (0x202d0) | 8208 | 256 | 512 | 0010 / 0610 / 1610 / 1c10 |
| p7 | 23b4 (0x23b40) | 46256 | 1446 | 2888 | 0010 / 21f4 / 7c34 / 9e18 |

(`models/engine/meshes.py` finds them: the initial object records and the scripts' `mov word cs:[slot], seg`.
p3 and p7 put the same mesh in several slots and switch them with +02.)

### 3.3 Work segment (one per part, rebuilt every frame)

Indices: g = global vertex number (vertices of all drawn objects in object order), k = visible-face slot.

| array | p2, p5 | p3 | p6 | p7 | contents |
|---|---|---|---|---|---|
| keys | 0000 | 0000 | 3200 | 0000 | u32 per visible face: depth sum << 16 \| k; FFFFFFFF ends the list; sorted in place |
| sort temp | 1450 | 0fa0 | 41a0 | 1f40 | the radix sort's second buffer |
| tex[g] | 28a0 | 1f40 | 0000 | 3e80 | u32 texture coordinates (high word u, low word v) |
| env[g] | 3840 | 2ee0 | 0c80 | 55f0 | u32 env coordinates (high word u, low word v, 8.8 texels) |
| scr[g] | 47e0 | 3e80 | 1900 | 6d60 | s16 y at +0, s16 x at +2 (so the dword is x << 16 \| y) |
| dep[g] | 5780 | 4e20 | 2580 | 84d0 | s16 depth word (stride 4) |
| A[k], B[k], C[k] | 6720, 7148, 7b70 | 5dc0, 6590, 6d60 | 5140, 5910, 60e0 | 9c40, abe0, bb80 | u16 4*g of the corners; B and C carry the texture / env selector in bits 0..1 |

Capacities: p2/p5 1000 vertices, 1300 faces; p3 1000 / 1000; p6 800 / 1000; p7 1500 / 2000. Nothing checks them.

## 4. Per retrace (2580; p3 1f60, p5 1a70, p6 1cc0, p7 1f40)

Called once per displayed frame (70.09 Hz) from the int 8 handler (core scope). Engine part, identical in all:

```c
label_x += label_dx;                                   // p2 [1955] += [1957]
if (label_x < 0 || label_x >= 0x4b) label_dx = 0;      // label slides until it leaves 0..74
/* part-specific here: p2 palette fade (8.1), p5 bump strength (7.4), p6 morph / bump strength, p7 [19b8]++ */
timer += 100;                                          // p2 [1963], p3 [19d6], p5 [156a]: never read
for (o = objs; o->seg != 0; o++)                       // camera slot included
    if (o->off == 0) {
        o->ang[0..2] += o->dang[0..2];                 // u16 wrap
        o->pos[0..2] += o->vel[0..2];                  // s32
    }
ready++;  retraces++;                                  // p2 [1924], [1928]
```

The scripts set positions, angles and velocities between "run N retraces" calls (part scopes).

## 5. The frame build

### 5.1 Object loop and matrix (354a, 348b, 1d86)

```c
nvert = nface = 0;                                     // p2 [1931], [1933]
for each record o from slot 0 while o->seg != 0:
    if (o->seg == 0xffff || o > objlast || o->off || mesh(o).nf == 0) continue;
    cli;                                               // copy the object's state atomically
    A[i] = (s16)(o->ang[i] + cam->ang[i]) >> 4;        // [1d80..1d84]
    T[i] = o->pos[i] + cam->pos[i];                    // [1910..1918]
    piv[i] = o->piv[i];  f = o->f;  cull = o->cull;    // [18f8..1900], [191c], [1935]
    selC += o->attr & 0xff;  selB += o->attr >> 8;     // byte adds to [194f], [194d], undone afterwards
    sti;
    matrix(); vertices(); faces();
keys[nface] = 0xffffffff;
```

Sine: S(a) = sin[(A & 0xffe) / 2], C(a) = sin[(A & 0xffe) / 2 + 512] (word table, 2048 entries per turn, Q15:
32767 = 1.0; so the angle is used to 2048 steps, angle >> 5). With s1/c1 from A[0], s2/c2 from A[1], s3/c3 from
A[2] and r(x) = x >> 15 (arithmetic; **p2 rounds**: `sar 15; adc 0` = round half up, except the one marked t()):

```
m0 = r(c2*c3 - r(s2*s1)*s3)    m1 = r(c1*s3)    m2 = r(s2*c3 + r(c2*s1)*s3)
m3 = r(-(c2*s3 + t(s2*s1)*c3)) m4 = r(c1*c3)    m5 = r(-(s2*s3 - r(c2*s1)*c3))
m6 = r(-s2*c1)                 m7 = -s1         m8 = r(c2*c1)            (t = truncating sar in p2 too)
```

All products 32-bit. This is R = Rz(-a3) * Rx(-a1) * Ry(a2) with the standard right-handed rotation matrices
(applied to column vectors: Ry first), scaled by 32768.

### 5.2 Vertices (1c60 calling 1b60, per vertex i of the object, g = nvert + i)

```c
N = normal[i];                                          // s16 x3
ex = m0*Nx + m1*Ny + m2*Nz;  ey = m3*Nx + m4*Ny + m5*Nz;  // s32
e  = (ex & 0xffff0000) | ((u32)ey >> 16);               // high words of both
if (p6) e = -e;                                         // p6 1a5c: neg edx (u = 0x4000 - ex - 1, v = 0x4000 - ey)
env[g] = e + 0x40004000;                                // u = high word, v = low word, 8.8 texels, 64.0 = centre
tex[g] = texsrc == mesh ? texcoord[i]                   // p2, p5, p6
       : (sext15(Ny) << 16) | (u16)sext15(Nx);          // p3, p7: object-space normal; sext15(n) = (s16)(n<<1)>>1
x = pos[i].x + piv.x; y = ...; z = ...;                 // s32
Px = m0*x + m1*y + m2*z;  Py = m3*x + ...;  Pz = m6*x + m7*y + m8*z;   // s32, = 32768 * R (v + p)
if (p5) { Px += piv.x; Py += piv.y; Pz += piv.z; }      // p5 16b0: pivot added again, unscaled (negligible)
Px += T.x << 8;  Py += T.y << 8;
dep[g] = (s16)((((Pz >> 16) * f) >> 14) + 0x4000);      // depth word (translation not included!)
D  = Pz - (T.z << 8);                                   // s32 divisor (negative in front of the eye)
q  = (s64)Px * f / D;                                   // idiv: truncated
sx = (q >> 3) + ((q >> 2) & 1) + 160;                   // sar 3 + adc: round half up
q  = 3 * ((s64)Py * f / D);
sy = (q >> 5) + ((q >> 4) & 1) + 100;
scr[g] = (sx << 16) | (u16)sy;
```

p5's vertex rotate also builds the normal-derived texture dword like p3/p7 but its vertex loop stores the mesh
texture coordinates (dead code). There is no near-plane test: D = 0 would divide by zero, D > 0 (behind the eye)
flips the vertex through the centre; neither happens in the demo's camera paths as far as the dumps show.

### 5.3 What the projection means

With R the float rotation and v the vertex (mesh units): V = R (v + p) (+ p / 32768 in p5), distance in front of
the eye **d = T.z / 128 - V.z** (T in object position units: 128 = one mesh unit), and

```
x  = 160 - (f / 8)     * (V.x + T.x / 128) / d          screen column
yr = 100 - (3 f / 32)  * (V.y + T.y / 128) / d          row of the 201-row target; screen row = yr - 1
```

So the projection centre is screen pixel (160, 99); f / 8 pixels per unit at distance 1 horizontally and 0.75 of
that vertically (a 320 x 200 picture shown at 4:3 would need 0.833: objects look 10 % flatter than round — part of
the look). x grows with -V.x (the image is mirrored relative to a right-handed camera looking down -z, y up), so
front faces end up clockwise on screen. **f is per object**: an object with a smaller f is drawn smaller about the
screen centre (in p2 the "C" has f = 1300, the discs f = 4000), equivalent to scaling its view-space x and y by
f / f_ref. The camera z position (e.g. p2 starts at 0x119400 / 128 = 9000 units) is just T.z.

### 5.4 Faces (3377), per face j of the object in mesh order

```c
(xa,ya), (xb,yb), (xc,yc) = scr[A], scr[B], scr[C];           // A, B, C = word + 4*nvert
ax = (s16)((s16)((xb-xa)*(yc-ya)) - (s16)((xc-xa)*(yb-ya)));  // 16-bit products (overflow for edges > ~181 px)
if (cull == 0 ? ax < 0 : ax >= 0) continue;                   // ax == 0 is drawn with cull 0
keys[nface] = (u16)(dep[A] + dep[B] + dep[C]) << 16 | nface;
Aarr[nface] = A;
Barr[nface] = B | ((attr >> 8) + selB) & 3;                   // texture selector
Carr[nface] = C | ((attr & 0xff) + selC) & 3;                 // env selector
nface++;
nvert += nv;    // after the object
```

The 16-bit overflow made the facing test wrong for 15 of 1364 faces in the p2 close-up 7500 and 2 in 9701 (all
huge, mostly off screen), 0 in every other dumped frame (`cull_check.py`).

## 6. Sort (3585)

A binary LSD radix sort of the u32 keys, one stable partition per bit, bits **18..31 in p2, 21..31 in p3, p5, p7,
20..31 in p6** (`mov edx, 0x40000 / 0x200000 / 0x100000`): zeros keep their order, ones are written backwards from
the end of the temp buffer and read back reversed. The list ends at the first entry whose low word is negative
(the FFFFFFFF terminator). Result: faces ascending by `depth_sum >> 2` (p2), `>> 5` (p3, p5, p7), `>> 4` (p6),
unsigned, ties in mesh / object order. Drawn in that order, so the smallest key (farthest) first.

depth_sum = sum over the corners of 0x4000 + R(v+p).z * f / 32768 (mod 65536): the face centroid's depth **in the
object's own rotated frame, scaled by f, without the translation**. Within one object this is the painter's order
by centroid depth (coarsened to 4, 32 or 16 units of the sum). Between objects it is not a depth order at all:
faces of different objects interleave by local depth times f (in p2, disc faces by 4000 z, "C" faces by 1300 z,
both centred on the same T).

## 7. Drawing (3310 and the fillers)

### 7.1 Materials

For each sorted entry: `tseg = texseg[B & 3]`, `eseg = envseg[C & 3]` (the 4-word segment tables, patched into
the filler: p2 [2e8e], [2e8c]). tseg = 0 selects the env-only filler (p2, p5, p6, p7); **p3 has no env-only
filler** and always uses the texture path. Initial table contents (image segments; scripts and part code may
change them):

| part | env segments [0..3] | texture segments [0..3] |
|---|---|---|
| p2 | 6538, 6540, 6d38, 6540 | 23d1, 0, 0, 0 |
| p3 | 0337, 033f, 0b37, 0b3f | 2341, 2349, 2b41, 2b41 |
| p5 | 03d7, 03df, 0bd7, 0bdf | 0, 1c96, 0, 0 |
| p6 | 0449, 0451, 0449, 0451 | 2398, 23a0, 0, 0 |
| p7 | 17b8 x 4 | 2fe9, 2ff1, 2fe9, 2fe9 |

An env map is a 128 x 128 window of 8-bit colour indices at stride 256: segment S and S + 8 (128 bytes on) are the
left and right halves of one 256-byte-wide block, S + 0x800 the lower half. Contents seen in the dumps (from the
image unless noted): p2 6538 values 1..127 (dark blue ramp, small highlight), 6540 1..127 (large highlight), 6d38
lies past the end of the image (rows 128..137 of the block are the image's last bytes, the rest whatever memory
held: not used as far as the dumps show); p3 0337 128..239 (033f all 255, 0b37/0b3f all 0); p5 03d7 16..105, 03df
79..142; p6 0449 16..79, 0451 80..189 (rewritten every frame by p6's loop); p7 17b8 22..200 = the centre of the
256 x 256 map 13b4 that p7 rebuilds every frame (13b4 + 0x404 paragraphs = offset row 64, column 64). Textures are
bump maps, 8-bit codes (136 = 0x88 = no displacement with the generated tables): p2 23d1, p3 2341, p5 1c96, p6
23a0, p7 2fe9 / 2ff1; p3 2349 / 2b41 and p6 2398 are all 136. Which palette ranges these index is in the part
scopes' palettes; the engine adds no shading of its own.

### 7.2 Triangle set-up (2881; the env-only 3030 is the same with the differences noted)

```c
P = scr[] dwords (x << 16 | (u16)y) of A, B, C
outside = count of P > 0x01400000 (unsigned: x < 0, x > 320, or x == 320 with y > 0)
if (outside == 3) return;              // dropped even if it spans the screen
clip = outside != 0;                   // [2e88]: horizontal clipping needed
sort A, B, C by (s16)y: if !(yA<yB) swap(A,B); if !(yB<yC) swap(B,C); if !(yA<yB) swap(A,B);
if (yA > 201 || yC < 1) return;
if (yA == yB && (s32)PA >= (s32)PB) swap(A,B);       // flat top: A is the left one
dy1 = (u16)(PB-PA), dy2 = (u16)(PC-PA), dy3 = (u16)(PC-PB)
s1 = (s32)(PB-PA) / dy1, s2 = (PC-PA) / dy2, s3 = (PC-PB) / dy3     // 32-bit idiv; dword differences, so the
                       // slope is ((dx << 16) + dy) / dy and a negative y borrows from x (top-edge quirk)
if (dy2 == 0) return;
L = PA; R = PA + 0x7fff (env-only: + 0x8000);       // 16.16 x, the "fraction" is y; R starts half a pixel right
```

Left-edge gradient per line of the two interpolated dwords (env e, texture t), each as two 16-bit halves divided
by 16-bit idiv (`cwd; idiv bx`): long edge (A->C over dy2) if s1 >= s2 (B is right), else A->B over dy1. Width at
B's row: W = (s1 - s2) * dy1 (32-bit); if s1 == s2 or dy1 == 0: W = PB - PA and the left-edge gradient is the
previous triangle's (self-modified immediates keep their old value; only matters for degenerate triangles).
Per-pixel gradient of each 16-bit half h:

```
q        = (s16)(hA - hC) * dy1 / dy2                  // imul word, idiv word: truncated
ax       = (u16)(q - hA + hB)                          // = hB - (hA + (hC - hA) * dy1 / dy2): width of h at B's row
hstep(h) = (s32)(ax << 16) / W                         // cdq; idiv ebp; low word kept
```

The env-only filler subtracts 1 from both per-pixel halves (`dec ax`, 314c / 3170). Halves are stored as one dword
(high | low) and added as a dword everywhere, so a negative low step carries +1 into the high half on most
additions (an error of 1/256 texel per step in the high coordinate).

Top half (rows yA .. yB-1) unless yB <= 1 or flat top: left / right slopes = min / max(s1, s2); yB clamped to 201;
if yA < 1 the edges and the left-edge values advance by 1 - yA lines and yA = 1; the row address yA*320 << 16 is
added to L and R (the edge accumulators are screen addresses in 16.16); span loop for yB - yA lines. Skipping the
top half (yB <= 1 or flat): left-edge values advance by dy1 lines, L = min(PA + s2*dy1, PB), R = max(...) + 0x7fff,
row max(yA, 1)*320 added.

Bottom half (rows yB .. yC-1) if dy3 != 0: new left-edge gradient (C - current value) / dy3 per half; slopes:
left = max(s2, s3), right = min; yC clamped to 201 (unsigned compare); yB < 1 advanced as above; span loop.

### 7.3 Span loops (2660 texture+env, 2ea0 env only)

Edge stepping: both slopes get + 320 << 16 (next row) and are packed crosswise, `ebp = Rfrac:Lint`,
`edx = Lfrac:Rint`, stepped by `add ebp, Rsfrac:Lsint; adc edx, Lsfrac:Rsint; adc ebp, 0` (self-modified
immediates). The fraction carries are right; a carry out of a 16-bit integer address also leaks into the other
edge's fraction (1/65536 px). Per line: pixels Lint .. Rint-1 (16-bit addresses: writes wrap inside the
segment), values start at the left-edge values (no sub-pixel correction), then the left-edge values add their
per-line gradient. Clipped variant (some vertex outside 0..320): per row, a span starting at or right of
rowbase+320 is skipped; a span starting left of the row is advanced by (rowbase - L) per-pixel steps (a 32-bit
multiply of the packed step) and starts at rowbase; R is clamped to rowbase .. rowbase+320 (**env-only: +319, so
column 319 is never drawn by a clipped env-only span**). The env-only loops count pixels in AL: **spans wider than
255 pixels draw (width mod 256) pixels** (256 draws 256).

Pixel, texture + env path (u = high word, v = low word of each dword; k = texel shift, section 12):

```c
bx    = ((((tex >> 8) & 0xff) << k) | (tex >> (32 - k))) & 0x7f7f;   // mov bl,ch; shld ebx,ecx,k; and bx,7f7f
code  = texseg:[bx];
bx    = ((env >> 8) & 0xff) << 8 | (env >> 24);                       // row = v int, column = u int
pixel = envseg:[(u16)(bx + bump[code])];                               // 16-bit add: column carry goes to the row
tex  += tstep; env += estep;
```

Env-only path: `pixel = envseg:[((env >> 8) & 0xff) << 8 | env >> 24]`.

Texel address per shift (tu = high word, tv = low word of the texture dword):

| k | parts | row (0..127) | column (0..127) | effective texture |
|---|---|---|---|---|
| 8 | p3, p7 | (tv >> 8) & 127 | (tu >> 8) & 127 | the 128 x 128 map, 8.8 coordinates |
| 9 | p2 | ((tv >> 8) & 63) * 2 + (tu >> 15) | (tu >> 7) & 127 | even rows only while tu < 0x8000 (always: tu <= 0x7fff); 9.7 in u |
| 10 | p5, p6 | ((tv >> 8) & 31) * 4 + (tu >> 14) | (tu >> 6) & 127 | every 4th row, phase tu >> 14 (1 for the meshes' 0x4000..0x7fff); 10.6 in u, two repeats over that range |

The mesh texture coordinates lie in 0x4000..0x7fff (p6's rings from 0x2527), so each mesh covers one (k = 10: two
in u) repeat of its map. In p3 and p7 the coordinates are the object-space normal: column (Ny >> 8) & 127, row
(Nx >> 8) & 127, a pattern fixed to the object; sext15 makes a vertex with |n| >= 16384 jump by 32768 against its
neighbour, so faces where a normal component crosses +-16384 (0.59 of the length) interpolate across the whole
map: streaks (a visible quirk of p3 and p7).

### 7.4 Bump tables (256 words, indexed by the texel code t)

The word is added to the env address, so as a displacement: dx = ((o + 128) & 255) - 128, dy = (o - dx) / 256
(o = the word as s16). p2 (data at 0010:0000 = file 0x100; cs:0200 holds a second copy, unused): dy = D(t & 15),
dx = D(t >> 4) with D = -7 -6 -5 -4 -3 -2 -1 0 0 0 1 2 3 4 5 6, except rows t >> 4 = 7, 8 where dy is one less (the
generator's borrow). p3 / p7 (data at their 0010:0000, different from p2's): dy = (t & 15) - 8, dx = (t >> 4) - 8,
with a borrow quirk for t >> 4 = 8 (dy one less). p5 (3099, cs:375b) and p6 (3913, cs:0010) regenerate theirs
every retrace from a strength s (p5: s = ((sin[[1546] & 0x7fe at cs:0012] >> 4) * 3) >> 4 with [1546] += [1548]
per retrace, 0..383; p6: s = [176e], += [1770] per retrace, kept in 0..255):

```c
d[i] = (u16)((i - 8) * s) >> 8;              // high byte of -8s + i*s, i = 0..15
word[16*r + c] = d[r] << 8 | (u8)d[c];       // r = t >> 4 -> row (dy), c = t & 15 -> column (dx)
```

so dy = d[t >> 4] + (1 if d[t & 15] < 0), dx = d[t & 15] (the byte packing adds 1 to dy when dx is negative).
Note p2/p3/p7 put the low nibble on the row axis and p5/p6 the high nibble.

## 8. Frame copies

All wait first until the per-retrace routine has run since the last copy (`ready != 0`, then clear it): at most one
drawn frame per retrace, and the frame shows the object state of the moment its build started (the retrace before).

### 8.1 p2 (360c): credit strip, motion blur, label

```c
switch ([1904]):  -2: nothing;  -1: target 4527:6e00 (rows 88..119) = 0;  0: = 0x80;
                  n >= 1: copy 0x2800 bytes from 0451:(u16)(n*0x2800 + 0xdb0a) = 0451:030a + (n-1)*0x2800
for (i = 0; i < 64000; i++) acc[i] = blend[acc[i]][target[i]];        // acc = 5517, blend = 3527:0000
if (label_x >= 0) copy the 70 x 44 label from 2bd2:030a (stride 70), its rightmost min(label_x, 70) columns, to
    acc at row 9, column max(label_x - 70, 0)      // into the blur buffer: the label gets a trail too
A000:0000 = acc + 0x140, 64320 bytes;            // acc rows 1..201 -> screen rows 0..200
target + 0x140 = [1947] == 1 ? 0ec3:0000 : 0, 64320 bytes
```

The blend covers acc rows 0..199 only, so acc row 200 = **screen row 199 is never written: colour index 0 during
all of p2** (checked in every 7th frame of the capture). blend[old][new] (64 KB, old = accumulator, new = this frame):

- old, new < 0x80 (1967, weight w = [1965], 24 at the start, the script changes it, e.g. 114, 64):
  `(new * w + old * (256 - w)) >> 8` (truncated; the generator accumulates exactly, `lea ax,[ecx+edx]`);
- old < 0x40 or 0x80..0xbf with new >= 0x80, and old 0x80..0xbf with new < 0x80 (1998, run once at start):
  `0x80 | (ceil((n7 * 96 + o7 * 416) / 2) >> 8)` with n7, o7 = the values without bit 7 (3/16 new + 13/16 old,
  rounded); other entries are never generated. The p2 script later zeroes rows 0x80..0xbf and entries
  [0][0x80], [0x80][0], [0x80][0x80] (0010:3a5d), see notes-p2.md.

So the blur is an exponential moving average of colour indices, which works because p2's palette is a ramp; a
remake blends in RGB with the same weights. The palette fade (p2 per-retrace, when [193f] = 1): [1941] += 0xff per
retrace from its start value until >= 0x3f00 (then [193f] = 0), table = 2550(start = [1941] before the add, step
0x100), DAC[i] = table[palette[i]] for the 768 bytes at cs:1f10 (720 from 6507:0010 + 48 from 2bd2:02da): every
component + [1941] / 256, saturating at 63 = the fade to white.

### 8.2 p3, p5, p6, p7

```c
if (label_x >= 0) label (70 x 44 at labelseg:030a) into the target at row 9 as above;   // no trail
A000:0000 = target + 0x140, 64320 bytes;
target + 0x140 = background:0000, 64320 bytes;      // the picture behind the objects, restored every frame
```

## 9. Tables

- **Sine** (19ce): the first quarter, 512 words, is data (p2 cs:0410 = file 0x510; the same words in every part at
  its sine address): round(32767 sin(2 pi i / 2048)) except entry 510 (32767 for 32766). 19ce mirrors it to entries
  512..1023 (`[1023 - i] = [i]`, so entry 512 = entry 511), negates 0..1023 into 1024..2047 and copies 0..511 to
  2048..2559 (the cosine overrun).
- **Palette scale** (p2 2550; p5 2f99 and p6 3871 first do `dx >>= 4`, so their input is 0..0x1000): AX = start
  (8.8), DX = step clamped to 0..0x100, 64 bytes at BX: `t[i] = ah; if (ax <= 0x3f00) ax += dx;` = floor((start +
  i*step) / 256) until the first value past 0x3f00, then constant (63, or 64 if it hits 0x3f00 exactly with step
  0x100, which the demo's start / step values never do on an entry that is used). start 0 = brightness
  step / 256; step 0x100 = add start / 256 (fade to white).
- **Blend tables** (1967, 1998): section 8.1.

## 10. Time bases

- Object and camera motion, angles, label slide, p2's fade, p5/p6 bump strength, p6 morph counter, p7 light
  counter: per retrace (70.09 Hz), section 4. A remake evaluates n = t * 70.086 retraces since the script step set
  the value and interpolates the fraction (angles: unwrapped sum, then mod 65536).
- The engine itself keeps no per-drawn-frame state, apart from the stale gradient of degenerate triangles (7.2).
- Script steps: the "run N retraces" loop (core / part scopes). Music: not read. Random numbers: none.

## 11. For the remake

### 11.1 Float form (checked against the integer engine, `float_check.py`)

```
theta_i = (o.ang[i] + cam.ang[i]) mod 65536 * 2 pi / 65536;  R = Rz(-theta3) Rx(-theta1) Ry(theta2)
V = R (v + p);  d = T.z / 128 - V.z;  T = o.pos + cam.pos
x = 160 - f/8 * (V.x + T.x/128) / d;   screen row = 99 - 3f/32 * (V.y + T.y/128) / d
env: u = 64 + (R N).x / 512,  v = 64 + (R N).y / 512   (texels of the 128 map; p6: 64 - ..., 64 - ...)
     sample env[row = v][column = u] (+ bump displacement); |N| = 27558 -> radius 53.8 texels around (64, 64)
tex: per-vertex (tu, tv) from the mesh (p2, p5, p6) or (Ny, Nx) of the object-space normal (p3, p7), decoded with
     the part's texel shift (7.3)
front faces: clockwise on screen (screen cross product >= 0) for cull = 0, counter-clockwise for cull != 0
```

### 11.2 Shortcuts to evaluate exactly

- 2048-step Q15 sine table, truncated (p2: rounded) Q15 matrix products -> float sin / cos and matrix.
- Integer vertex positions (rounded to whole pixels), integer edge stepping, affine (screen-linear) interpolation of
  env and texture coordinates in packed 16-bit halves -> float vertices, MSAA, perspective-correct interpolation.
  The env coordinates are a linear function of the unnormalised normal, so interpolating (u, v) (or the normal
  without renormalising) keeps the original's look; renormalising per pixel would be true Phong and slightly
  different.
- Nearest-neighbour env-map reads -> filtered reads, after converting the indices through the part's current
  palette (indices are not linear values; p2's blur, 8.1, likewise blends RGB).
- Bump: decode each texel code through the bump table to (dx, dy) env texels and filter the displacement, not the
  codes. The +1 row on negative column displacements (p5, p6) and the p2/p3/p7 borrow rows are byte-packing
  artefacts.
- 16-bit facing test, dropped triangles with all corners left/right of the screen, the 8-bit span counter, the
  missing column 319 of clipped env-only spans, the one-pixel snapping at the top edge (negative y borrows into x):
  320 x 200 artefacts, drop them.
- Painter's order -> depth buffer. Within an object the result is the same apart from intersecting or nearly
  coplanar faces (the sort uses centroid depth at 1/4..1/32 granularity). Between objects the original order is by
  local depth times f, not by distance (6); a depth buffer gives the geometrically correct overlap of p2's discs and
  "C" instead. If that changes what a viewer sees in some shot, the remake must reproduce the original order for
  those objects (draw order per object, or a depth bias); p2's notes decide.
- Different f per object = a per-object projection (scale about the screen centre); keep it, and give depth from d.

### 11.3 Quirks to keep (part of the look)

- Vertical scale 0.75 of horizontal (objects 10 % flatter than round on a 4:3 screen), projection centre at screen
  pixel (160, 99), objects with their own f.
- p2: screen row 199 black throughout; the label inside the blur; the credit strip replacing the 3D in rows
  87..118 when [1904] != -2.
- p3, p7: texture streaks where a normal component crosses +-16384 (7.3).
- Low-resolution texture sampling of p2 (64 rows of its 128) and p5/p6 (32 rows): that is the bump pattern the
  viewer sees; decode the same rows.

## 12. Where the copies differ (from `diff_routines.py`, aligned listings)

| | p2 | p3 | p5 | p6 | p7 |
|---|---|---|---|---|---|
| matrix products | rounded (adc), one truncated | truncated | truncated | truncated | truncated |
| texture dword | mesh | object normal (sext15) | mesh (rotate builds the normal one, unused) | mesh | object normal |
| env coordinates | | | | negated (`neg edx`) | |
| pivot | | | added again after rotation, unscaled | | |
| first sort bit | 18 | 21 | 21 | 20 | 21 |
| texel shift k | 9 | 8 | 10 | 10 | 8 |
| env-only filler | yes | **no** | yes | yes | yes |
| bump table | data cs:0000 | data cs:0000 | 3099 -> cs:375b per retrace | 3913 -> cs:0010 per retrace | data cs:0000 |
| frame copy | blur 360c | copy + background | copy + background | copy + background | copy + background |
| per retrace extra | palette fade | — | bump strength | morph [3259], bump strength [176e] | [19b8]++ (lights) |

Everything else (object loop, per object, vertex loop, face set-up, sort, draw loop, both fillers and span
loops) is the same code with other addresses (section 2 and the work-segment table).

## 13. Not determined

- Whether p2's env map 6d38 (selector 2) is ever selected: it lies past the end of p2's image, its content is
  whatever memory held (in the emulator: zeros below the image tail). Not used in the dumped frames.
- Why the normals have length 27558 (= 0.841 * 32768): probably chosen so that 64 +- 53.8 texels plus the bump
  displacement stays inside the 128 map (guess).
- The engine's behaviour with vertices behind the eye or exceeding the work arrays: not reached in the dumps.

## 14. Models and checks (`models/engine/`, README.md there)

- `engine_model.py`: the bit-exact model (all five parts, configuration table `PARTS`).
- `check_frame.py <part> dumps_<part>/fNNNNN`: model vs emulator for one frame (arrays, keys, target).
- `check_copy_p2.py`, `check_copy.py`: the frame copies. `float_check.py`: section 11.1. `cull_check.py`: 5.4.
- `run_dope.py`: copy of the shared runner with `--dump NAME=CS:IP,...` (memory at code addresses);
  dumps were taken at the frame loops' calls: p2 pre=0F74:3737, drawn=3740, post=3743; p3 2bbd, 2bc3, 2bc6, 2bc9;
  p5 2b7f, 2b85, 2b88, 2b8b; p6 2dff, 2e6b, 2e6e, 2e71; p7 3089, 3169, 316c, 316f (pre, mid = before the draw loop,
  drawn = before the copy, post).
- `match_routines.py`, `diff_routines.py`, `find_shld.py`, `meshes.py`, `objects.py`, `maps.py`, `paths.py`:
  finding the copies, their differences, the meshes, object lists, maps and filler paths per frame.
