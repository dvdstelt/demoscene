# Scope "generic": GenericInit / GenericEffect and their 3D engine (LANDSCAPE, the second generic scene)

Routines: GenericInit 0x408cec (with the per-object shading set-up 0x4077d4, the bump gradient table 0x4074e4, the
shared-map list 0x40764c / 0x407684 / 0x407698 / 0x4076f4), GenericEffect 0x409d74 (with its retrace routine
0x4073fc), the page buffers 0x403648 / 0x4035b0 / 0x4036ec, and the engine in 0x411000..0x42b000: ini reader
0x412938 / 0x4129ec, spline reader 0x4133f8 / 0x413520 / 0x4137f8, spline 0x413870 (prepare) / 0x413b90
(evaluate), PCX decoder 0x412a68, memory pool 0x4125d0 / 0x412724 / 0x4126f0, object loader 0x412bf0 (free
0x413310, mapping scale 0x413330), screen set-up 0x412400, sine table 0x412560 (fixed cosine 0x422120, integer
square root 0x4221e0), sin / cos 0x413ce4 / 0x413ce0, transforms 0x413c93 / 0x413d98 / 0x413e6a / 0x413f37, group
order 0x413f90, environment coordinates 0x414160, projection 0x414000, back-face test 0x4140e0, face list 0x411980,
face sorts 0x4119f0 / 0x411a30, object sort 0x415480, Phong map 0x413a60, the drawers 0x40b060 (type 9), 0x40b218
(type 10), 0x40b3dc (type 11) and the self-modifying rasterizers 0x414f13 (types 9 and 10, edges 0x414bc0 /
0x414c94) and 0x418313 (type 11, edges 0x417fc0 / 0x418094). Shared helpers by what goes in and out: 0x403740 file
by name (→ 0x40241c; size in [0x82d2f4]) and 0x403760 (free, a no-op for archive files), 0x400104 music position,
0x403738 sets the retrace routine [0x832504]; Watcom's sprintf 0x4159ed, atoi 0x41b12e, strcmp 0x40f870, strncmp
0x4221ad, malloc 0x4117bf, free 0x41173d, realloc 0x41593c, memset 0x411340, atan 0x411234 (x87 fpatan), __CHP
0x41130c (rounding control "chop": truncates toward zero).

Clock names: **refresh** = one screen refresh (70.086 Hz) = one record of a `.frames` capture; refresh numbers are
records of `part2.frames`; add 14,444 for `full.frames` (checked: both scenes' frames are identical in the two
captures, and their script lines are 14,444 records apart in `full.script`). **Effect counter** = [0x832500], +1 per
refresh in the generic retrace routine (below).

**Verified** (scripts in `models/generic/`, README there):
- every table the init builds, against the snapshots at the Run commands (`snaps2/f5951`, `snaps2/f9191`): the six
  spline columns' records of all 11 objects (x, value, second derivative: identical), the 1,025-entry sine table,
  the type 9/10 bump gradient tables (2 × 65,536 bytes), the type 11 bump+texture table, the Phong map (65,536
  bytes), the light table block, the environment map blocks: **all identical**; the environment coordinate table
  at 0x825428 against round(8192 + 16384/π · asin(i/256)): all 513 entries;
- a Python model of the whole frame (`render.py`: splines, rotations, projection, back-face and near tests, face and
  object sorts, both rasterizers with their fixed-point arithmetic) against the page the program finished at every
  draw of runs resumed from those snapshots over the whole of both scenes (`run_vivid.py --gen-dump`, a copy with
  hooks): **0 pixels different** in all sampled draws (20 of the 775 LANDSCAPE draws, 79 of the 1,958 draws of the
  second scene, spread over the whole scenes; every shading type 9, 10, 11);
- the object state of every draw (spline position, X Y Z A B C: 775 + 19,580 object states) against the model's
  spline from t (`check_states.py`): all identical;
- the capture against those pages (`check_dump.py`): every 7th refresh of both scenes (613 records) shows exactly
  the page the retrace routine displayed last.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| scene | ini | GenericInit | GenericEffect | ends (music) | frames drawn at 100 MIPS |
|---|---|---|---|---|---|
| LANDSCAPE | `gen/gen.ini` | refresh 5946 (11:00) | 5951 .. 8101 | 15:00 (8101) | 775 in 2,150 refreshes: one per 3 refreshes (601 times) or 2 (172) |
| second scene | `gen2/gen.ini` (archive 3) | 9177 (17:00) | 9191 .. 11331 | 21:00 (11331) | 1,958 in 2,140 refreshes: one per refresh (1,773) or 2 (183) |

- **LANDSCAPE**: one object, the terrain `maa.m3d` (a 33 × 33 height grid, 2,048 triangles), environment-bump
  mapped (type 9): its colour is purely the environment map (a radial light) looked up through the vertex normals
  and offset by the slopes of `gen/bump.pcx` (a terraced height picture mapped onto the terrain). The terrain turns
  under a fixed camera along `land.spl`: the view circles low over the hills about 1.9 times, pitching between
  -26° and -10°. Black (index 0) behind the terrain's far edge.
- **Second scene**: ten copies of `phong.m3d` (a peanut-shaped blob, 82 vertices, 160 triangles) one after another
  along `path.spl`, tumbling, over the sunset photo `gen2/back.pcx`; shading types 11, 10, 9 repeating (objects 0,
  3, 6, 9: Phong-shaded texture with bumps; 1, 4, 7: floating environment bump; 2, 5, 8: environment bump).

GenericEffect starts right after GenericInit returns (its POS/PAT is the end condition, not a start condition): the
init takes 5 refreshes (LANDSCAPE) / 14 refreshes (second scene) at 100 MIPS (it computes 65,025 × 2 arctangents
per bump table, and the Phong map). During the init the screen is the cleared screen of the line before (index 0,
black in both palettes).

**Where it draws.** Into one of two 64,000-byte page buffers (malloc, 0x403648(2)), never into VGA memory; the
retrace routine 0x4073fc copies a finished page to 0xa0000 inside the retrace interrupt (8-byte moves, 64,000 bytes)
and in the same pass refills that page with the background. **Every displayed frame is a complete page**: the
background (colour or picture) over the whole 320 × 200, then the objects. Until the first page is shown (refresh
5954 / 9192) the screen keeps the cleared black screen (with the new palette from 5951 / 9191). After the effect
ends, the last shown page stays on screen until the next part draws (8101..8105; 11331..11333, at 11333 already
with the next part's palette). A page finished but not yet shown when the effect ends is never shown.

**Quirk (second scene): one frame with a black background.** The second page is zero-filled when allocated and only
gets the background after it has been shown once, so the second drawn frame (t = 1) has background index 0 (black)
around the objects: captured at refreshes 9194 and 9195 (full 23638, 23639), between frames with the sunset. In
LANDSCAPE the background colour is 0, so nothing shows.

## 2. The ini files

Format (0x412938): the key is looked for at the start of a line after any spaces / tabs, case-sensitive,
`Key=value`; lines starting with `#` are skipped; `@end` ends the search; the value runs to the end of the line
(trailing spaces cut), integers through atoi. For a missing required key GenericInit returns −1 with a message
("Missing: <key>=", built in 0x83449c) for the interpreter. For every per-object key the program first reads
`Common<Key>` and, only if it is absent, `<ObjectName><Key>`: a Common key overrides all objects.

Global keys:

| key | default | meaning | gen | gen2 |
|---|---|---|---|---|
| MemoryPoolSize | required (≥ 1) | number of 64 KB blocks of the map pool | 11 | 11 |
| UseSplineLooping | 0 | 0: the effect ends early when object 0 has run one whole lap from its start position | 1 | 1 |
| NumberOfObjects | required (> 0) | | 1 | 10 |
| ObjectName<i> | required (< 32 chars) | the prefix of the object's keys | PhongMaa | Phong0..Phong9 |
| UseBackgroundPic | 0 | | 0 | 1 |
| BackgroundPic | required if UseBackgroundPic | 320 × 200 PCX, the page background | (commented out) | gen2/back.pcx |
| BackgroundColor | 0 | palette index filling the page when there is no picture | 0 | 0 (unused) |
| Palette | required if UseBackgroundPic, else optional | 768 bytes, 6-bit VGA | gen/texture.pal | gen2/texture.pal |

Per object (Common or per name):

| key | used by types | meaning | LANDSCAPE | second scene |
|---|---|---|---|---|
| ShadingType | all | 0..11 (list in the ini; section 12) | 9 | 11, 10, 9 repeating |
| Object | all | the .m3d (each object gets its own copy) | gen/maa.m3d | gen2/phong.m3d |
| SplinePath | all | the .spl | gen/land.spl | Common gen2/path.spl |
| SplineSpeed | all | refreshes per spline key; speed = 0x10000 / value (integer division; 0 = still) | 150 → 436 | Common 65 → 1008 |
| SplineStartPos | all | start position in 16.16 keys, 0 ≤ s < (keys − 1) · 65536 | 0 | 100000 + 70000 · i |
| EnvironmentMap | 3, 9, 10 | 256 × 256 PCX | gen/envmap.pcx | gen2/envmap.pcx |
| BumpMap | 9, 10, 11 | 256 × 256 PCX of heights | gen/bump.pcx | gen2/bump.pcx |
| TextureMap | 4..8, 11 | 256 × 256 PCX | (not read) | gen2/texture.pcx |
| TextureLite | 5..8, 11 | light table, first 64 KB used | (not read) | gen2/texture.lit |
| PhongAmb, PhongDiff, PhongSpec, PhongExp, PhongShades | 2, 7, 8, 11 | Phong map parameters (16.16 fractions; exponent; shades) | (not read) | Common 0, 28672, 36864, 12; PhongShades 128 per object |
| BaseColor | 0, 1, 2, 5, 6 | | (PhongMaaObjBaseColor: not a key the program reads) | |
| GouraudLightShift | 0, 1, 5, 6 | | | |

`PhongMaaPhongShades=128` and the Common Phong / texture keys in `gen/gen.ini` are not read (type 9 needs none).
No key has a default except the global ones marked.

## 3. File formats

### .m3d (loader 0x412bf0)

```
0x00 char[4] "$M3D"
0x04 u32 flags: bit 0 vertices carry mapping coordinates; bit 1 faces carry normal indices; bit 2 normals present
0x08 u32 nverts
0x0c u32 nnormals
0x10 u32 ngroups (face groups)
0x14 u32 offset of the vertices: nverts × { s16 x, y, z; u16 u, v (if bit 0) }
0x18 u32 offset of the group centres: (ngroups > 1 ? ngroups : 1) × { s16 x, y, z }   (both files: one, (0,0,0))
0x1c u32 offset of the normals: nnormals × { s16 nx, ny, nz }   (unit length = 16383)
0x20 u32 offset of the groups: ngroups × { s16 nfaces; u8 colour; u8 pad;
                                           nfaces × { s16 v0, v1, v2; s16 n0, n1, n2 (if bit 1) } }
```
Both files have flags 7 and one group, and trailing bytes after the faces that the loader never reads (maa.m3d
0xa234..0xcc92, phong.m3d 0xcce..0x2254a: more face-like records). maa.m3d: 1,089 vertices (x, z = -3840..3840 in
steps of 240, y = -832..832), 1,002 normals, 2,048 faces; every vertex is used with exactly one normal index (so the
normals are per vertex, shared between vertices with equal normals). phong.m3d: 82 vertices (±423, ±808, ±432), 82
normals, 160 faces. Mapping coordinates u, v are 0..65535 for one turn of the 256-texel texture (the loader divides
them by 4, 0x413330: `u = u * 0x4000 >> 16`, giving 8.6 fixed point, 64 = one texel). Neighbouring vertices of
the terrain have irregular u, v; 178 of its faces span more than half the texture in u or v, so the texture runs
"the long way" through them (section 10).

The loader is asked for: type 0 → 0x102, 1..3 → 0x202, 4 → 1, 5 → 0x103, 6..11 → 0x203 (bit 0 mapping
coordinates, bit 1 normals; 0x100 one normal per face, 0x200 normals per face corner through the indices). For
bit 0 the init then scales u, v by 0x4000 (above).

Engine object (0x412bf0's result): +0x00 group array (12 bytes each: +0 faces, +4 the centre vertex, +8 s16
nfaces), +0x04 vertices (stride +0x24 = 20: +0 u, +2 v (8.6), +4 sx, +6 sy (screen), +8 x, +0xa y, +0xc z
(transformed), +0xe, +0x10, +0x12 x, y, z as loaded), +0x08 normals (stride +0x28 = 12: +0 nx, +2 ny, +4 nz
transformed, +6..+0xa as loaded), +0x10 vertex slots (mesh + centres), +0x14 centre slots, +0x18 normal count,
+0x1c group count, +0x20 group order (dwords index << 16 | z), +0x2c the allocation. Face, 0x1c bytes: +0, +4, +8
vertex pointers, +0xc, +0x10, +0x14 normal pointers, +0x18 colour, +0x1a hidden flag.

### .spl (0x4133f8, 0x413520, 0x4137f8)

Text. Lines starting with `@` are headers, read wherever they are: `@width N` (columns, at most 128), `@length N`
(rows), `@type iiiiii` (one letter per column; `i` = integer, the only kind), `@end`; `#` lines are comments. Data
rows: whitespace-separated integers; the reader skips every line starting with `#` or `@` and any characters before
the first digit / sign. The program takes columns 0..5 of every object's spline:

| column | meaning | unit |
|---|---|---|
| 0, 1, 2 | X, Y, Z: the object's position (added after the rotation) | object units; camera space (section 6) |
| 3 | A: rotation about the x axis | 16-bit angle, 65536 = 360° |
| 4 | B: rotation about the y axis | same |
| 5 | C: rotation about the z axis | same |

`land.spl`: 20 keys; X -1981..428, Y -432..567, Z -5652..-455, A 60817..63657 (-26°..-10°), B 54533 down to
-105803 (2.45 turns over the whole path), C 0. `path.spl`: 41 keys; X 8530 → -4900 → 4589 → ... → -2373, Z from
-1979 out to 15000 and back to -20379 (behind the camera); A = B = 10000 · (key + 1) (55° per key; key 23 has B =
220000 instead of 240000), C 0.

### Pictures, palette, light table

PCX (0x412a68): ZSoft 8-bit RLE; the result is {palette block: u8 first = 0, u16 count = 256, 768 bytes (the
file's 8-bit palette >> 2)}, {u32 width, u32 height, pixels}. Maps take the first 65,536 pixels (a 256 × 256 picture,
row-major: map[y][x] at y·256 + x).

| file | size | what | used as |
|---|---|---|---|
| gen/envmap.pcx, gen2/envmap.pcx (same pixels) | 256 × 256, indices 128..255 | a rendered radial light: index ≈ 255 − r (r = distance from (127, 127) in pixels), 128 beyond r = 127, with ±1 noise; symmetric (map = its transpose) | the pixel's index directly (types 9, 10): read through palette indices 128..255 |
| gen/bump.pcx | 256 × 256, 30 levels 0, 12, 20, ..., 236 | a rendered, terraced relief (scales / cracks) | heights → slope table (only differences matter) |
| gen2/bump.pcx | 256 × 256, 26 levels 0, 12, ..., 204 | rendered small round bumps | same |
| gen2/texture.pcx | 256 × 256, indices 69..127 | a drawn / rendered pink swirl with a light flare | texel index through the light table (type 11) |
| gen/texture.pcx, gen/texture.lit | | | not read by LANDSCAPE |
| gen2/back.pcx | 320 × 200, indices 2..56 | a photograph: sunset over a lake, in the palette's orange ramp 0..63 | the page background, 1:1 |
| gen/texture.pal, gen2/texture.pal | 768 bytes, 6-bit | 0..63 orange ramp (background), 64..127 texture colours, 128..255 the environment ramps | the whole palette |
| gen2/texture.lit | 121,541 bytes | 128 × 256 bytes of table (lit[shade][texel]), then the same table as C source text | first 64 KB copied; rows 0..127 used |

The light table maps (shade 0..127, texel index) to a palette index: shade 0 is black, shade 96 is about the
texel's own colour (lit[96][t] = t for 27 of the 53 texels the texture uses, a neighbour for the others), and above
it the colour runs to the palette's pink-white (rows 124..127 are all index 127 = (63, 51, 63); row 120 is 126 /
127). In between it is roughly the nearest palette colour to colour(t) · s / 96 below 96 and colour(t) + (63 −
colour(t)) · (s − 96) / 31 above (a guess; the nearest entries often have another hue, e.g. texel 127 (63, 51, 63)
at shade 32 gives (21, 8, 0)). The remake reads the table at run time and interpolates between shade rows.

## 4. GenericInit 0x408cec (ini name, archive flag)

```
load the ini (0x403740)
pool_init(MemoryPoolSize)                    // 0x4125d0: malloc(n·65536 + 0xffff), blocks 64 KB-aligned
shared map list = 8 entries of 0x88 bytes    // {void *data; int kind; char name[128]}
looping = UseSplineLooping; nobj = NumberOfObjects
objs = malloc(nobj · 0x74)
for i: name[i] = ObjectName<i>
type[i] = CommonShadingType or <name>ShadingType (0..11)
for i: 0x4077d4(&objs[i].draw, name, type, ini)          // maps and drawer, below
speed[i] = 0x10000 / (CommonSplineSpeed or <name>SplineSpeed)   // idiv; 0 stays 0
splines: CommonSplinePath (one file, six columns shared by all) or <name>SplinePath per object:
    for column c in 0..5: rec = 0x4137f8(file, c)          // n × 16 bytes {x = k << 16, y = value, y2, u}
                          for k: rec[k].y <<= 8
                          0x413870(rec, n)                 // second derivatives, section 5
    keys[i] = n; max[i] = (n - 1) << 16; pos[i] = 0
start[i] = CommonSplineStartPos or <name>SplineStartPos (must be 0 ≤ start < max)
objects: CommonObject (one file) or <name>Object: obj[i] = 0x412bf0(m3d, flags(type)); if (flags & 1) 0x413330(obj, 0x4000, 0x4000)
UseBackgroundPic: pic = 0x412a68(BackgroundPic); palette block 0x83459c = pic's palette (0x303 bytes); bg = pic pixels
else bgcolour = BackgroundColor · 0x01010101
Palette: 0x83459c = {first 0, count 256 (bytes 0, 0, 1)}, 768 bytes of the file   // replaces the picture's palette
sort = malloc(nobj · 8)
0x412400(320, 200)        // row table y·320, clip rectangle 0 ≤ x < 320, 0 ≤ y < 200, pitch 320
0x412560()                // sine table (section 6)
page = 0x403648(2)        // two zero-filled 64,000-byte pages; [0x8233a0] = page 0
```

Object record (0x74 bytes at [0x834478] + 0x74·i): +0x00 engine object, +0x04..+0x18 the six spline columns, +0x1c
X, +0x20 Y, +0x24 Z, +0x28 A, +0x2c B, +0x30 C (this frame), +0x34 keys, +0x38 speed, +0x3c pos, +0x40 max, +0x44
start, +0x48 type, +0x4c t0, +0x50 name, +0x60 drawer, +0x64 / +0x68 / +0x6c its three map pointers, +0x70 Gouraud
light shift.

**Per-object maps, 0x4077d4.** Maps come from the 64 KB pool (0x412724(n blocks): n contiguous free blocks,
64 KB-aligned, zero-filled) and are kept in the shared list by (name, kind), so objects with the same files share
them: kind 1 = one 64 KB block (a picture's first 65,536 pixels, the light table, a Phong map named
"amb+diff+spec+exp+shades"), kind 2 = the bump table (2 blocks), kind 4 = "bump+texture" (4 blocks). The alignment
matters: the inner loops address a map by putting two coordinate bytes into the low 16 bits of a register that holds
the map's base.

- types 9, 10: env = EnvironmentMap's pixels (kind 1); bump = bumptab(BumpMap, stride 2) (kind 2)
- type 11: tb = 4-byte table (kind 4): for every texel k, byte 2 = TextureMap pixel k, bytes 0, 1 =
  bumptab(BumpMap, stride 4), byte 3 = 0; phong = 0x413a60(block + 0x8080, 8, shades, amb, diff, spec, exp) (kind 1);
  lit = the first min(size, 65536) bytes of TextureLite over a zeroed block (kind 1)
- drawer: type 9 → 0x40b060 (env, bump), 10 → 0x40b218 (env, bump), 11 → 0x40b3dc (tb, phong, lit)

**Bump gradient table 0x4074e4(dst, heights, 100.0, stride)** (x87, double precision; verified bit-exact):
```
k = 1.0 / 100.0
for y in 0..254, x in 0..254:
    dst[(y·256 + x)·stride + 0] = (int8) chop(atan((h[y][x+1] - h[y][x]) · k) · 81.528662420382166)   // 256 / 3.14
    dst[(y·256 + x)·stride + 1] = (int8) chop(atan((h[y+1][x] - h[y][x]) · k) · 81.528662420382166)
row 255 and column 255 stay 0 (the pool block is zeroed)
```
Values -97..97: an offset in environment-map texels. A terrace step of 8 gives 6.

**Phong map 0x413a60** (cdecl: centre = block + 0x8080, shift 8, shades, amb, diff, spec, exp), 256 × 256 bytes,
verified bit-exact:
```
R = 128
for y in 0..127, x in 0..127:
    r = isqrt(x·x + y·y)                     // 0x4221e0: Newton b = (b + n/b + 1) >> 1: about round(sqrt)
    if r > R: v = amb
    else:
        c = cosfix(r · 0x19220 / R)          // unsigned division; 0x19220 = π/2 in 16.16; cosfix = 0x422120, a Taylor
                                             // series in 16.16 (c = cos(π/2 · r/128) · 65536, 0..65536)
        v = (c · diff >> 16) + amb
        p = exp == 0 ? 0x80000000 : c << 15  // 1.31
        if exp ≥ 2: repeat exp times: p = p · (c << 15) >> 31      // so c^(exp + 1) for exp ≥ 2 (c^13 here)
        v += p · spec >> 31
    s = v · (shades − 1) >> 16               // 0..127
    map[128 + y][128 + x] = map[128 + y][127 − x] = map[127 − y][128 + x] = map[127 − y][127 − x] = s
```
The second scene's map: s = floor(127 · (0.4375 c + 0.5625 c¹³)), c = cos(π/2 · r/128), 0 outside r = 128; its
centre (the brightest point) is at map (127.5, 127.5).

## 5. GenericEffect 0x409d74 (POS, PAT)

```
clear page: picture → copy bg to [0x8233a0]; else memset(bgcolour)
0x403738(0x4073fc)                         // the retrace routine
0x413b70(0x83459c)                         // the palette straight to the DAC (out 0x3c8 = 0, 768 × out 0x3c9),
                                           // at once, not synchronised with the retrace
for i: t0[i] = counter                     // [0x832500]
loop:
    0x400104()                             // music position
    if (order >= POS && row >= PAT) break  // checked once per drawn frame, before drawing it
    for i (object order):
        pos = start[i] + speed[i] · (counter − t0[i])
        if (pos >= start[i] + max[i]) {     // a whole lap
            if (i == 0 && !looping) end the effect
            t0[i] = counter                 // next frame restarts at start[i] (never happens in these scenes)
        }
        pos = pos % max[i]                  // idiv remainder
        X, Y, Z, A, B, C = spline(column c, pos) >> 8        // section 5.1
        reset vertices and normals; rotY(B); rotX(A); rotZ(C)   // section 6
        types 2, 3, 7..11: 0x414160 (normals → environment coordinates); 0, 1, 5, 6: 0x415a60 (Gouraud light)
        0x414000: project with (X, Y, Z)
        0x4140e0: back-face flags
    sort {Z, i} descending (insertion sort, stable)   // far objects first
    for each object in that order:
        for every face: if any vertex has z + Z <= -6720: hidden = 1          // near plane, depth 280
        drawer(obj, map pointers)                      // section 7
    [0x8233a0] = 0x4035b0()                            // page flip, below
end: 0x403738(0); free pages, tables, maps, objects, splines, pool
```

**Pages and the display (0x4035b0, 0x4073fc).** The flip queues the finished page and returns the next one; if
that one is still waiting to be shown it waits (busy loop on the queue indexes). The retrace routine runs in every
retrace: counter += 1; refreshes since the last shown page += 1; if a page is queued and that number ≥ the minimum
interval [0x821e80]: minimum = that number (so it rises at once to the slowest recent frame), copy the page to VGA
while refilling it with the background (0x414b59 picture / 0x414b36 colour), next queue index, number = 0. The flip
lowers the minimum again: when it had to wait more than one refresh for more than 70 / minimum flips in a row,
minimum −= (wait > 4 ? wait / 2 : 1). So frames are shown at a steady interval of whole refreshes: LANDSCAPE starts
at 3 (left by the water scene), the second scene at 2 and goes down to 1. This decides only which states are shown,
not the motion.

### 5.1 The spline (0x413870, 0x413b90)

A natural cubic spline through the keys (Numerical Recipes `spline` / `splint`) with knots at x = 0, 1, ..., n − 1
(16.16) and values y = value · 256, all in 16.16 fixed point: fmul(a, b) = (a · b) >> 16 (64-bit product, floor),
fdiv(a, b) = (a << 16) / b (truncating idiv).
```
prepare: y2[0] = u[0] = 0
for i = 1..n−2:  sig = fdiv(x[i]−x[i−1], x[i+1]−x[i−1])                      // = 0x8000
                 p = fmul(sig, y2[i−1]) + 0x20000
                 y2[i] = fdiv(sig − 0x10000, p)
                 d = fdiv(y[i+1]−y[i], x[i+1]−x[i]) − fdiv(y[i]−y[i−1], x[i]−x[i−1])
                 u[i] = fdiv((0x60000 · d) / (x[i+1]−x[i−1]) − fmul(sig, u[i−1]), p)    // 64-bit product, idiv
y2[n−1] = 0 (natural end: qn = un = 0)
for k = n−2 .. 0: y2[k] = fmul(y2[k], y2[k+1]) + u[k]

evaluate(pos): binary search from lo = 0, hi = n−1 while hi − lo > 1: k = (lo+hi)/2; pos ≤ x[k] → hi = k, else lo = k
               h = x[hi] − x[lo]; a = fdiv(x[hi] − pos, h); b = fdiv(pos − x[lo], h)
               s = fmul(fmul(fmul(a,a),a) − a, y2[lo]) + fmul(fmul(fmul(b,b),b) − b, y2[hi])
               s = (fmul(s, h) · h) / 0x60000                                         // · h² / 6
               return s + fmul(a, y[lo]) + fmul(b, y[hi])
the object uses evaluate(pos) >> 8 (arithmetic)
```
A double-precision natural cubic spline on the raw values gives the same within 1.06 units for every column of both
files (`spline_float.py`; 1.25 for path.spl's B): the remake can evaluate it in doubles.

## 6. Transforms and projection

**Sine.** Table 0x412560: T[k] = cosfix(k · 0x64880 >> 12) for k = 0..1024, i.e. cos(k · π / 2048) · 65536
(T[0] = 65536). sin(a) (0x413ce4, a = 16-bit angle): i = (a >> 4) & 0x7ff; if i == 0 → 0; v = T[|i − 1024|]; if
bit 15 of a: v = ~v (one's complement, −v − 1). cos(a) = sin(a + 0x4000) (0x413ce0). The remake: sin / cos of
a · 2π / 65536.

**Rotations** (on the 16-bit working copies; each result `(p·c − q·s) >> 16`, `(p·s + q·c) >> 16`, stored as 16
bits), in this order, on the vertices and the normals alike:
```
rotY(B) 0x413d98:  x' = (x·cosB − z·sinB) >> 16;  z' = (x·sinB + z·cosB) >> 16
rotX(A) 0x413e6a:  y' = (y·cosA − z·sinA) >> 16;  z' = (y·sinA + z·cosA) >> 16
rotZ(C) 0x413f37:  y' = (y·cosC − x·sinC) >> 16;  x' = (y·sinC + x·cosC) >> 16
world = (x', y', z') + (X, Y, Z)        i.e. world = Rz(C) · Rx(A) · Ry(B) · v + (X, Y, Z)
```
**Projection 0x414000** (called with centre (160, 100) and 0xd8f0, 0xb4c6, −7000):
```
kx = (0xd8f0 · −7000) >> 20 = −371;   ky = −((0xb4c6 · −7000) >> 20) = 309
for each mesh vertex (not the centres): d = −7000 − (z + Z); if d == 0 skip (keeps the old screen position)
    sy = (y + Y) · ky / d + 100            // truncating idiv, stored as int16
    sx = (x + X) · kx / d + 160
```
So the eye is at (0, 0, −7000) looking along +z, x right, y up: sx = 160 + 371 · x / depth, sy = 100 − 309 · y /
depth with depth = 7000 + z. On the 4:3 screen both focal lengths are the same: vertical field of view
2 · atan(100 / 309) = 35.9°, horizontal 2 · atan(160 / 371) = 46.7°. Positions are whole pixels.

**Environment coordinates 0x414160** (types 2, 3, 7..11), per normal after the rotations: word 0 = E[nx >> 6], word
1 = E[ny >> 6] (nz is not used), E = the static word table at 0x825428 (index −256..256, 0x825228..0x825628):
E[i] = round(8192 + 16384/π · asin(i / 256)), E[256] = 16383. 8.6 fixed point: u_env = 128 + (256/π) · asin(nx)
texels, nx the unit normal's x; the view direction maps to the map's centre.

**Back faces 0x4140e0**, per face from the screen positions (16-bit differences, 32-bit products):
hidden = (x0 − x1)(y2 − y1) < (y0 − y1)(x2 − x1). **Near plane** (in GenericEffect): hidden = 1 if any vertex has
depth ≤ 280 (z + Z ≤ −6720); such faces are dropped whole, not clipped. This is visible in LANDSCAPE: at t = 653..694
(refreshes ≈ 6610..6651) the camera skims the ground, and the ground faces next to the eye, which clipped at depth 1
would cover up to 38,000 pixels (the lower 60 % of the screen at t = 674, `near_mask.py`), are dropped: the hills
behind them show instead (the terrain is closed behind, so no background shows). In the second scene objects leave
the screen before they come that close: dropping changes no pixel there (`near_check4.py`).

**Order.** Objects: by Z, largest first (0x415480, stable insertion sort). Groups (only with more than one group):
by their centre's z, largest first (0x413f90). Faces of a group (0x411980): every face not hidden gets the key
min(z) + ((max(z) − min(z)) >> 1) of its three transformed z (16-bit arithmetic), and is drawn far to near: an
insertion sort for fewer than 200 faces (0x4119f0, stable), else Numerical Recipes' heapsort with the comparisons
reversed (0x411a30, not stable; its exact order is in `render.py` `hpsort_desc`). The terrain (≈ 1,000 visible faces)
uses the heapsort, the blobs (≈ 80) the insertion sort. Painter's algorithm, no depth buffer; it gives the same
visible face as a depth buffer would in all but 0..6 pixels per frame (12 frames of both scenes checked with the
original's coverage, `painter_check.py`).

## 7. The rasterizers and the pixel index

The drawers build, per face in the sorted order, a block of {map pointers; three vertices of 6 dwords (sx, sy, a, b,
c, d)} and call the rasterizer:

| type | drawer | maps | a, b (texture / bump coordinates) | c, d (map coordinates) |
|---|---|---|---|---|
| 9 environment bump | 0x40b060 | env, bump | u, v of the vertex (8.6) | E[nx], E[ny] |
| 10 floating environment bump | 0x40b218 | env, bump | E[nx], E[ny] | E[ny] − 0x4000, E[nx] − 0x4000 |
| 11 Phong texture bump | 0x40b3dc | tb, phong, lit | u, v | E[nx], E[ny] |

(− 0x4000 is 256 texels: no effect, the coordinates wrap at 256.)

**Triangle set-up (0x414f13; 0x418313 is the same).** Sort the vertices by sy (swap (0,1) if y0 > y1, (0,2) if
y0 > y2, (1,2) if y1 > y2): top, mid, bot. Nothing if bot.sy ≤ 0 or top.sy ≥ 200 or bot.sy == top.sy. Then, all in
32-bit integers:
```
t     = ((mid.sy − top.sy) << 16) / (bot.sy − top.sy)                       // 16.16
width = t · (bot.sx − top.sx) + ((top.sx − mid.sx) << 16)                     // long edge minus mid at mid's row, 16.16
if width == 0: nothing;  if |width| ≤ 0x1000: width <<= 12                    // near-degenerate: tiny gradients
for each attribute q in (a, b, c, d):  num = t · (bot.q − top.q) + ((top.q − mid.q) << 16)
                                       g[q] = (num << 10) / width             // 64-bit dividend; per pixel, 8.16
step8[q] = (g[q] >> 8) & 0xffff                                               // patched into the inner loop: 8.8
```
One gradient per triangle (affine). Edges (0x414c94 with attributes, 0x414bc0 x only): if width ≥ 0 the left side
is top→mid then mid→bot and the right side the long edge top→bot; else the long edge is the left side. An edge from
P to Q covers rows P.sy .. Q.sy − 1: x = P.sx << 16, dx = ((Q.sx − P.sx) << 16) / rows (rows == 2: the difference
>> 1; rows == 1: no step); q = P.q << 10, dq = ((Q.q − P.q) << 10) / rows likewise. If the triangle crosses the top
or bottom of the screen, rows are clipped and the start values advanced by the skipped rows (dx from the full
count). Per row: span from x_l >> 16 to (x_r >> 16) − 1; the attributes start at the left edge's values (no
correction for the fraction of x_l); if any vertex has sx < 0 or > 320, the span is clipped to 0..319 and the start
values advanced by g · skipped pixels. Then left edge += steps, right x += dx, next segment when a count runs out.

**Inner loop**, with the four coordinates as 8.8 values (integer byte, fraction byte), each advanced by step8 with
the carry from fraction to integer byte (16-bit wraparound, i.e. modulo 256 texels):
```
type 9:   (dx, dy) = bump[b.int][a.int]                       // signed bytes, row b, column a
          index = env[(d.int + dy) & 255][(c.int + dx) & 255]
type 10:  same code with the coordinates of the table above:
          (dx, dy) = bump[E(ny)][E(nx)];  index = env[(E(nx) + dy) & 255][(E(ny) + dx) & 255]
          (the bump pattern is fixed to the normal, not to the surface, and the environment map is read
           transposed — the same picture here, the map is symmetric)
type 11:  (dx, dy) = tb[b.int][a.int].bytes 0, 1;  texel = tb[b.int][a.int].byte 2
          shade = phong[(d.int + dy) & 255][(c.int + dx) & 255]          // 0..127
          index = lit[shade][texel]
```
The bump offsets move the map lookup by whole texels (they are added to the integer byte only). Every pixel is a
nearest-texel read; the maps and the texture repeat every 256 texels.

So a pixel's colour: **type 9 / 10**: palette[env index] (128..255), the light map index straight; **type 11**:
palette[lit[shade][texel]].

## 8. Time bases

- **Motion: the effect counter.** t = counter − t0, refreshes since GenericEffect started (t0 is read right after
  the start; in the capture t = 0 at refresh 5951 for LANDSCAPE, t = 1 at refresh 9191 for the second scene, i.e.
  t0 belongs to refresh 9190). Each object reads the counter itself, so a retrace during the object loop gives the
  later objects t + 1 (once in the capture: draw 1329 of the second scene, objects 8 and 9).
  pos(t) = start + speed · t in 16.16 keys, speed = trunc(65536 / SplineSpeed) (436 for
  150: 0.2 % slower than 65536/150; 1008 for 65); then pos mod max. X, Y, Z, A, B, C = spline(pos / 65536 keys).
  LANDSCAPE covers keys 0 .. 14.3 of 19 (one lap would take 2,855 refreshes); the second scene's objects run 32.9
  keys of 40, objects 6..9 wrap from the last key to key 0 (both off screen: the last key is behind the camera, key 0
  far right), no object completes a lap, so the restart (t0 = counter) never happens. The remake: t = (T −
  T_start) · 70.086 with T_start the program time of the GenericEffect line (part2 84.9235 s / 131.1479 s, full
  291.0123 s / 337.2367 s); the original's integer t agrees within one refresh.
- **Display latency.** A frame's state is computed at the start of its drawing and shown 2 frame intervals later:
  in the capture the record at refresh R shows t = (R − 5951) − 6 for LANDSCAPE (−4 while frames take 2
  refreshes) and t = (R − 9190) − 2 for the second scene (−4 at 2 refreshes). The remake renders t of the current
  time; to put it next to the capture, shift by those lags (`check_dump.py` lists them).
- **Per drawn frame:** nothing is stepped per frame; the frame rate (25 fps LANDSCAPE, ~64 fps second scene at 100
  MIPS) only decides which states are seen, and how long the black-background frame of the second scene stays
  (2 refreshes in the capture).
- **Music:** only the end condition (order ≥ POS and row ≥ PAT, once per drawn frame): LANDSCAPE 15:00, second
  scene 21:00. No sync values.
- **Random numbers:** none.

## 9. Palette and colours

The ini's Palette (6-bit) goes to the DAC once, at the start of GenericEffect (refresh 5951 / 9191), unchanged
until the effect ends; the effect runs no fades. gen/texture.pal: 0..63 orange ramp (unused), 64..127 texture
colours (unused), 128..255 dark red → cream (the environment ramp the terrain is drawn in). gen2/texture.pal: 0..63
the orange ramp of the sunset photo, 64..127 the pink texture colours, 128..255 the environment ramp (dark
brown/purple → red → orange-yellow). For true colour a pixel's colour is the palette entry of its index (6-bit
v → v / 63).

## 10. What the remake should evaluate exactly (shortcuts) and what stays

- **Spline and angles**: doubles (natural cubic spline, sin / cos), section 5.1 / 6. Keep the integer speed
  65536 / SplineSpeed truncated.
- **Projection**: float positions instead of whole pixels (the truncating division moves vertices up to 1 pixel
  toward the centre lines); same camera: eye at (0, 0, −7000), vertical FOV 35.9°, 4:3.
- **Interpolation**: the original is affine in screen space with one gradient per triangle (taken at the widest
  row, 8.8 steps, no sub-pixel or sub-row correction, ×4096 for near-degenerate triangles). Use perspective-correct
  interpolation and exact coverage.
- **Environment / Phong coordinates**: the original interpolates E[nx], E[ny] linearly between the vertices, with E
  a 512-entry table indexed by the normal's top 9 bits. Exact: interpolate the vertex normals per pixel (they are per
  vertex in both meshes), normalise, and take u = 128 + (256/π) · asin(nx), v = 128 + (256/π) · asin(ny) texels
  (nx, ny of the rotated normal; a normal pointing up has the larger row v, as in the table).
- **Bump offsets**: per bump texel trunc(atan(Δh / 100) · 256 / 3.14) whole environment texels from forward
  differences of 8-bit terraced heights, nearest texel. Exact: offset = atan(∂h/∂u / 100) · 256 / 3.14 and the same
  for v, with ∂h from bilinearly (or bicubically) interpolated heights, in fractional texels. The terraces of the
  height pictures (steps of 8) are the look of the bump patterns (the lines of the terrain) and stay: they come
  from the pictures.
- **Map reads**: nearest texel → bilinear (env map, texture through the palette; Phong map: evaluate the shade
  formula of section 4 continuously; light table: interpolate between shade rows, colour = lerp of
  palette[lit[⌊s⌋][t]] and palette[lit[⌊s⌋ + 1][t]]). All maps wrap at 256 texels (repeat).
- **Texture coordinates** run through the given per-vertex u, v without unwrapping: in 178 terrain faces they span
  most of the texture, which draws the bump pattern squeezed into bands of fine parallel lines: the "zip" bands
  and X-shaped seams visible on the hills in the capture are exactly these faces (`wrap_mask.py`,
  `runs/wrap_1443.png`). A GPU interpolating the same per-vertex values (u / 65536 turns of the texture, repeat)
  reproduces them; keep them, they are part of the picture.
- **Visibility**: painter's order by face mid-depth (and by object Z) → a depth buffer (same result to a few
  pixels). **Keep the near rule**: drop every triangle with a vertex at depth ≤ 280 (7000 + z ≤ 280 in the
  original's units) instead of clipping it, otherwise LANDSCAPE's low pass at t = 653..694 shows a wall of near
  ground where the original shows the hills. Clip only what is left against a near plane closer than 280.
- **Grain**: no noise or dither of its own. The environment picture has ±1 index noise (rendered); its smooth
  version (index ≈ 255 − r) is a fair replacement.

## 11. Pictures for the remake

- gen2/back.pcx is the only picture shown as it is: a landscape photograph (sunset over a lake, no people), 320 ×
  200 at 1:1 under everything, in the palette's single-hue orange ramp (indices 2..56). Candidate for a
  high-resolution version (photo, so an upscale rather than a redraw), same colours (it is read through the palette
  only as colours, not through a table).
- envmap.pcx: a rendered radial light read as palette indices (128..255): stays computed / original (a smooth
  ramp).
- bump.pcx (both): rendered height maps read through the slope table: stay original (4× upscale at most, keeping
  the levels).
- gen2/texture.pcx: a drawn / rendered swirl read through the light table (its indices matter): stays original
  (filtered).

## 12. Other shading types (code shared, not used by these scenes)

Drawers (0x408afb, table 0x4077a4): 0 flat 0x40a2bc (BaseColor), 1 Gouraud 0x40a3f8 (BaseColor, GouraudLightShift;
light 0x415a60 per frame), 2 Phong 0x40a6a4 (BaseColor, Phong map), 3 environment 0x40a544 (env), 4 texture 0x40a818
(texture), 5 flat texture 0x40a980, 6 Gouraud texture 0x40ab10, 7 Phong texture 0x40acc0, 8 "shadow" (tweaked
Phong) 0x40ae84 (texture, Phong map, light table). They use the same object, transform, projection, culling, face
list and sort code.

## 13. Not determined

- How the light table was made (section 3: a guess at the intended colours, nearest palette entries with an
  unknown metric). The remake reads it.
- On real hardware the 64,000-byte copy to VGA inside the retrace takes longer than the vertical blank and may
  tear; the emulator copies instantly. Not reproduced.
- The frame rate and so the display lag and the length of the black-background frame depend on the CPU; the values
  above are the capture's (100 M instructions per second).

---------------------------------------------------------------------------------------------------------------

## Verification

`models/generic/` (README there): `gen.py` (readers, spline, sine table, maps), `render.py` (the frame model),
`check_init.py` / `check_maps.py` (init tables against snapshots), `run_vivid.py --gen-dump` (pages and states of
every draw), `check_states.py` (every draw's object states), `check_dump.py` (frame model against the pages;
capture against the shown pages; frame intervals and lags), `compare_capture.py` (model next to the capture, PNG),
`near_check2.py` / `near_check4.py` / `near_mask.py` (the near rule), `painter_check.py` (painter's order against
a depth buffer), `wrap_mask.py` (the wrapping faces), `stats.py`, `spline_float.py`, `fit_lit.py`, `show_pics.py`.
