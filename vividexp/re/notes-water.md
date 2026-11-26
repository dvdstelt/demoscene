# Scope "water": four bump-mapped hearts over the Pleiades (part II's opening)

Routines: WaterInit 0x405bd0 (script line 8 of `script/demo2.scr`), WaterEffect 0x4064b0 (line 9, `004 063
WaterEffect 0`), the retrace routine 0x4059cc, the object drawer 0x40580c, and the engine routines they call:
the page flip 0x4035b0 and frame buffers 0x403648 / 0x4036ec, the screen set-up 0x412400 / 0x4124c8, the sine table
0x412560 (Taylor cosine 0x422120) and sin / cos 0x413ce4 / 0x413ce0, the 64 KB-aligned pool 0x4125d0 / 0x412724, the
engine's PCX decoder 0x412a68, the `.m3d` loader 0x412bf0 and its uv scaling 0x413330, the spline reader
0x413520 / 0x4137f8, the spline set-up 0x413870 and evaluation 0x413b90, the transforms 0x413c93 / 0x413d98 /
0x413e6a / 0x413f37, the group sort 0x413f90, the environment coordinates 0x414160, the projection 0x414000, the
back-face test 0x4140e0, the face lists 0x411980 / 0x4119f0 / 0x411a30, the object sort 0x415480, the rasterizers
0x414f13 (bump-environment) and 0x41448a (affine texture) with their edge routines 0x414bc0 / 0x414c94 and
0x414210 / 0x4142e4, the DAC upload 0x413b70 and the buffer copies 0x414b30 / 0x414b36 / 0x414b59. The ini reader
0x412938 / 0x4129ec (string; integer = atoi 0x41b12e of the string, or the default in EBX) and the file loader
0x403740 / 0x403760 are shared with the generic scope (notes-generic.md); they are described here by what goes in and
out. 0x40580c and 0x4059cc serve only WaterEffect; the engine routines are shared (0x414f13 is also called from
0x40b202 / 0x40b3be, 0x41448a and the spline evaluation from the unused Dog / Tunnel / Earth and the generic scenes).

Clock names: **count** = the water's own refresh counter [0x832500] (+1 per screen refresh in the retrace routine)
minus its value when WaterEffect started; record = record of `part2.frames` (one per refresh, 70.086 Hz).

Checked (models/water/, README there), all exact:
- tables against the image and `snaps2/f21.snap`: the sine table, the asin table, the 128 KB bump table, both
  env-map halves, the palette, the background and all 12 spline channels (knot values and second derivatives);
- a hooked emulator run from `snaps2/f21.snap` to the end of the part (1,743 drawn frames): placement, rotation,
  projection, environment and bump coordinates and back-face flags of **all 6,968 object draws** (vertex and normal
  arrays dumped at 0x40580c);
- the transparent heart's environment map with the hearts drawn into it (0x41448a): 400 of 400 dumps;
- whole frames rendered by the Python model (painter's order, heap sort, both rasterizers, background): **1,741 of
  1,741 frames equal the records of `part2.frames` that show them** (records 23..2706), pixel for pixel, including
  the frames with a stale environment map (8.3) and the 3 frames whose objects read different clock values.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

The ini file calls the objects "the transparent object" (a sphere) and "the dsd letters", but the meshes are
**hearts**: three orange hearts (the same mesh `wat_s.m3d`) follow one spline path one behind the other, and one blue
"transparent" heart (`wat_sphe.m3d`, the same mesh 1.133 times larger) follows its own path. All four tumble (rotation
about X and Y by the same angle), are environment-mapped with a bump map (a rough, scaly relief), and are drawn over a
still picture of a star cluster. The blue heart is opaque; it looks like glass because the orange hearts behind it
are painted into its environment map (8.).

| | record (part2) | music | |
|---|---|---|---|
| WaterInit | 15 | 0:00 | loads, builds the tables (6 refreshes); the screen stays black |
| WaterEffect starts | 21 | 0:00 | first frame (count 0, background only) shown at record 21; record 22 is black (9.3) |
| first heart on screen | 218 (count 196) | 0:23 | the orange hearts enter at the right edge, low |
| blue heart on screen | 1591..2626 | 2:58..4:53 | its path stays at (−9000, 5000) off screen for the first 9 knots |
| last heart pixel | 2654 (count 2632) | 4:56 | |
| end | 2707 | 4:63 | the music position ends the effect (2.); the last frame shown is record 2706 (background only) |

After the effect the script runs InitBUMP (2707), `InitFADE pal/watback.pal pal/white.pal 2 0 255` (2710, the screen
still holds the last water frame: the background) and waits for 5:01 (2724) before fading into the bump part. The
water itself runs no fade. `pal/watback.pal` equals the water palette in the entries the background uses (130..187),
so the fade starts without a jump.

## 2. Files and static data

Archive 2 (part II), `files.py`:

| file | address | size | format, use |
|---|---|---|---|
| water/water.ini | 0x633210 | 966 | `key=value` text (below) |
| water/watenv.pcx | 0x61dd2c | 87,268 | PCX 512 × 256, 8 bit: **two environment maps side by side**. Left 256 × 256: an orange glow (a radial gradient, black at the rim, 128 indices 0..127): the solid hearts' map. Right 256 × 256: a blue glassy sphere with clouds (52 indices in 192..255): the transparent heart's map. Its palette is the scene's palette |
| water/watbump.pcx | 0x60f6ca | 58,978 | PCX 256 × 256, grey, 31 levels (4, 12, .. 244 and 0): the height map of a scaly relief. Only read through the bump table (4.) |
| water/watback.pcx | 0x60712a | 34,208 | PCX 320 × 200: a photograph of the Pleiades star cluster (my identification), tinted magenta, 44 indices in 130..187; the background of every frame, unscaled |
| water/wat_s.spl | 0x6335d6 | 1,164 | spline path of the three solid hearts (5.) |
| water/wat_sphe.spl | 0x633a62 | 1,052 | spline path of the transparent heart |
| water/wat_s.m3d | 0x633e7e | 34,438 | the heart (6.1): 182 vertices, 182 normals, 360 triangles |
| water/wat_sphe.m3d | 0x63c504 | 34,438 | the same triangles and bump coordinates, vertices × 1.1332, normals equal but one component (off by 1) |
| pal/watback.pal | 0x7970fe | 768 | used by the script's fade afterwards, not by the water |

`water.ini` as read (keys at 0x8205cc..0x820687; the object keys through the pointer table 0x821e84):
SplineSpeed=128 (default 70), UseSplineLooping=0, TransparentSplinePath=water/wat_sphe.spl,
SolidSplinePath=water/wat_s.spl, ObjectSpacing=95000 (default 0), TransparentObj=water/wat_sphe.m3d,
SolidObj0..2=water/wat_s.m3d, EnvironmentMap=water/watenv.pcx, BumpMap=water/watbump.pcx, BackgroundColor=0 (used
only without a picture), UseBackgroundPic=1, BackgroundPic=water/watback.pcx; `Palette=` is commented out (it would
be 768 bytes of 8-bit components, each >> 2).

VIVID.EXE: doubles 0.01 (0x820698) and 81.52866242038216 (0x8206a0, = 256 / 3.14) for the bump table; the asin table
(words) at 0x825228..0x825628, indexed through the pointer [0x825638] = 0x825428 with -256..256 (6.3); the names
table 0x821e84 (4 pointers: "TransparentObj", "SolidObj0", "SolidObj1", "SolidObj2"). Immediates in WaterEffect:
500, 10000, 0xd8f0, 0xb4c6, -7000 (0xffffe4a8), 160, 100, 128 (below).

## 3. WaterInit 0x405bd0 (eax = 1: files from the archive, edx = "water/water.ini")

```c
screen_init(320, 200);           // 0x412400: rows[y] = 320 y for y = 0..200 ([0x844d08]), clip x 0..320, y 0..200, pitch 320
sin_init();                      // 0x412560: sintab[k] = cos_fixed(floor(k * 0x64880 / 4096)), k = 0..1024 ([0x844d20])
pool_init(5);                    // 0x4125d0: five 64 KB blocks, 64 KB aligned
bumptab = pool_alloc(2, 0x1ffff);// [0x833dd8], 128 KB, zero-filled
envL    = pool_alloc(1, 0xffff); // [0x833ddc]  64 KB aligned (the rasterizer replaces the low 16 address bits)
envR    = pool_alloc(1, 0xffff); // [0x833de0]  the transparent heart's map, hearts get drawn into it
envR0   = pool_alloc(1, 0xffff); // [0x833de4]  clean copy of envR
buffer  = buffers_init(2);       // 0x403648: two 64,000-byte frame buffers [0x8324e4], [0x8324e8], zero-filled
for (i = 0; i <= 256; i++) rows256[i] = 256 * i;          // 0x8336b8, for drawing into the env map
ini = load("water/water.ini");                             // [0x833ed0]
if ((usepic = ini_int("UseBackgroundPic", 0))) {          // [0x8336ac]
    back = pcx(load(ini_str("BackgroundPic")));            // [0x833dec]; pixels [0x8336b0]
} else fill = ini_int("BackgroundColor", 0) * 0x01010101;  // [0x8336b4]
env = pcx(load(ini_str("EnvironmentMap")));
for (y = 0; y < 256; y++) {
    memcpy(envL + 256 y, env.pixels + 512 y,       256);
    memcpy(envR + 256 y, env.pixels + 512 y + 256, 256);
}
memcpy(dacpal /*0x833abc*/, env.palette, 0x303);  // byte first = 0, word count = 256, 768 bytes (PCX palette >> 2)
memcpy(envR0, envR, 65536);
b = pcx(load(ini_str("BumpMap"))).pixels;          // 256 wide
for (y = 0; y < 255; y++)
    for (x = 0; x < 255; x++) {                    // column 255 and row 255 stay 0
        p = 256 y + x;
        bumptab[y][x].dx = (int8)trunc(atan((b[p + 1]   - b[p]) * 0.01) * 81.52866242038216);
        bumptab[y][x].dy = (int8)trunc(atan((b[p + 256] - b[p]) * 0.01) * 81.52866242038216);
    }                                              // atan 0x411234 (fpatan), trunc: Watcom __CHP 0x41130c (chop)
if (ini_str("Palette")) { first 0, count 256, dacpal[i] = file[i] >> 2 }   // not used
for (k = 0; k < 4; k++) {                          // names 0x821e84: TransparentObj, SolidObj0..2
    obj[k].mesh = m3d_load(load(ini_str(name[k])), flags 3);   // 0x412bf0
    scale_uv(obj[k].mesh, 0x4000, 0x4000);                     // 0x413330: u = u * 0x4000 >> 16 (unsigned)
}
for (path in Solid, Transparent) {                 // 0x833ea0 and 0x833eb8: six channel pointers each
    n = spl_rows(file);                            // 0x413520 -> [0x833dc0] (24 for both files; the last read wins)
    for (c = 0; c < 6; c++) {
        ch[c] = spl_column(file, c);               // 0x4137f8: n keys of 16 bytes {x = i << 16, y = value, y2, u}
        for each key: y <<= 8;
        spline_setup(ch[c], n);                    // 0x413870
    }
}
obj[0].path = transparent channels; obj[0].transparent = 1; obj[0].offset = 0 (BSS)
obj[1..3].path = solid channels;    obj[k].transparent = 0
tend = n << 16;                                    // [0x833dc8]
step = 65536 / ini_int("SplineSpeed", 70);         // [0x833dc4] = 512
spacing = ini_int("ObjectSpacing", 0);             // 95000
looping = ini_int("UseSplineLooping", 0);          // 0
obj[1].offset = 0; obj[2].offset = spacing; obj[3].offset = 2 * spacing;
```

Object records 0x833df0 + 0x2c k: +0 channel table, +4 mesh, +8 transparent flag, +0xc t, +0x10 offset, +0x14 x,
+0x18 y, +0x1c z, +0x20 a, +0x24 b, +0x28 c.

## 4. The bump table

`bumptab` (256 × 256 entries of two signed bytes) holds, per texel of the bump map, the offset in **environment-map
texels** that the texel's slope adds to the environment coordinates:

    dx(x, y) = trunc(atan(0.01 * (h(x + 1, y) - h(x, y))) * 256 / 3.14)
    dy(x, y) = trunc(atan(0.01 * (h(x, y + 1) - h(x, y))) * 256 / 3.14)

with forward differences in bump texels (h in 0..255; the picture's level steps of 8 give dx, dy in -41..41 and
-36..55). Column 255 and row 255 are 0 (not computed), so the map's last column and row have no relief. 3.14 is the
program's constant, not π.

## 5. Splines (`.spl`, 0x413520 / 0x4137f8 / 0x413870 / 0x413b90)

`.spl` is text: rows of six integers, then `@width 6`, `@length 24`, `@type iiiiii`, `@end`. Columns: x, y, z
(world units), a, b, c (angles, 65536 = one turn). Both files have 24 rows; the first row is repeated (rows 0 and 1
equal) and so is the last (rows 22 and 23). In both, column 4 = column 5 (a = b) and column 6 = 0.

Each column becomes a **natural cubic spline** through the rows at the knots x_i = i (16.16: i << 16), values
y_i = value << 8. 0x413870 is Numerical Recipes' `spline()` in 16.16 fixed point with both ends natural
(y2[0] = u[0] = 0; qn = un = 0 because the end slope argument 0x40000001 > 0x40000000):

```c
for (i = 1; i < n - 1; i++) {                 // div16(a, b) = ((int64)a << 16) / b truncated; mul16 = (int64)a * b >> 16 (floor)
    sig   = div16(x[i] - x[i-1], x[i+1] - x[i-1]);                 // 0x8000
    p     = mul16(sig, y2[i-1]) + 0x20000;
    y2[i] = div16(sig - 0x10000, p);
    d     = div16(y[i+1] - y[i], x[i+1] - x[i]) - div16(y[i] - y[i-1], x[i] - x[i-1]);
    u[i]  = div16((int64)0x60000 * d / (x[i+1] - x[i-1]) - mul16(sig, u[i-1]), p);
}
y2[n-1] = 0;
for (k = n - 2; k >= 0; k--) y2[k] = mul16(y2[k], y2[k+1]) + u[k];
```

Evaluation 0x413b90 (t in 16.16 knot units, n = [0x833dc0]):

```c
klo = 0; khi = n - 1;
while (khi - klo > 1) { k = (klo + khi) >> 1; if (t <= x[k]) khi = k; else klo = k; }
h = x[khi] - x[klo];                       // 0x10000
A = div16(x[khi] - t, h);  B = div16(t - x[klo], h);
r = mul16(mul16(mul16(B, B), B) - B, y2[khi]) + mul16(mul16(mul16(A, A), A) - A, y2[klo]);
r = (int64)mul16(r, h) * h / 0x60000;      // (..) h^2 / 6
r += mul16(A, y[klo]) + mul16(B, y[khi]);
value = r >> 8;                            // sar
```

i.e. y(s) = A y_lo + B y_hi + ((A³ − A) y2_lo + (B³ − B) y2_hi) / 6 with s = t / 65536, unit knot spacing. For
s > 23 (only the leading solid heart, from count 2573) the search stays in the last segment and the cubic is
**extrapolated** (B > 1); the remake must do the same, not clamp (the heart is off screen by then, near x = −15000).

The six values per object (x, y, z, a, b, c) are integers (sar 8 of the 16.16 result). The remake evaluates the
same natural spline in double precision from the file's numbers (the fixed point changes the results by at most a
unit or two).

**Time**: in each drawn frame and for each object k (the clock is read again per object, 0x406565):

    t_k = (count) * 512 + offset_k        (16.16 knot units; 512 = 65536 / SplineSpeed)
    s_k = count / 128 + offset_k / 65536  offsets: transparent 0, solid 0, 95000, 190000 (1.4496 and 2.8992 knots)

so the solid hearts follow the same path 185.5 refreshes apart, the later objects ahead.

## 6. Objects

### 6.1 `.m3d` (as 0x412bf0 reads it with flags 3)

Little-endian. Header 36 bytes: `"$M3D"`, dword flags (7: bit 0 vertices carry u, v; bit 1 faces carry normal
indices; bit 2 normals present), dword vertex count (182), dword normal count (182), dword group count (1), dword
offsets of the vertices (0x24), the group centres (0x740), the normals (0x746) and the groups (0xb8a).
Vertices: int16 x, y, z, uint16 u, v (10 bytes). Centres: int16 x, y, z per group (one: (0, 0, 0)). Normals: int16
x, y, z of length 16384 (6 bytes). Group: uint16 face count (360), byte material (0, unused here), byte pad, then per
face uint16 v0, v1, v2 (vertex indices) and n0, n1, n2 (normal indices, a separate numbering) (12 bytes). The file
has 27,160 bytes after the faces that the loader never reads.

In memory (object header after the arrays): +0 groups (12 bytes: faces, centre vertex, word count), +4 vertices
(20 bytes: +0 bump u, +2 bump v, +4 screen x, +6 screen y, +8 x, +0xa y, +0xc z rotated, +0xe..+0x12 x, y, z from the
file), +8 normals (12 bytes: +0 nx → env u, +2 ny → env v, +4 nz, +6..+0xa from the file), +0xc centre, +0x10
vertices + centres (183), +0x14 centres (1), +0x18 normals, +0x1c groups, +0x20 group order, +0x24 / +0x28 strides,
faces 0x1c bytes (+0/+4/+8 vertex pointers, +0xc/+0x10/+0x14 normal pointers, +0x18 material, +0x1a back-face flag).

Bump coordinates: file u, v (0..65535 = 0..256 texels, 8.8) × 1/4 → 10.6 fixed point (64 = one texel): the heart's
u, v span texels 7.75..255.95 and 1.1..250.4: each heart carries the bump map once.

### 6.2 Rotation (0x413c93, 0x413d98, 0x413e6a, 0x413f37)

Every frame the working coordinates are reset from the file and rotated in this order, vertices (including the
centre) and normals alike, each step storing 16-bit words (sar 16 of the 32-bit products: floor):

    about Y by b:  x' = (x cos b − z sin b) >> 16,  z' = (z cos b + x sin b) >> 16
    about X by a:  y' = (y cos a − z sin a) >> 16,  z' = (z cos a + y sin a) >> 16
    about Z by c:  y' = (y cos c − x sin c) >> 16,  x' = (x cos c + y sin c) >> 16

i.e. v' = Rz(c) Rx(a) Ry(b) v with these signs. sin / cos (0x413ce4, cos = sin(angle + 0x4000)) of a 16-bit angle
(65536 = one turn; only the low 16 bits of the spline value count):

```c
k = (a >> 4) & 2047;                       // a & 0x7ff0 == 0 -> 0
s = sintab[abs(k - 1024)];                 // sintab[j] = cos(j pi / 2048) in 16.16, sintab[0] = 65536
if (a & 0x8000) s = ~s;                    // one's complement: -s - 1
```

The table comes from 0x422120, a 16.16 Taylor series cos(x) = 1 − x²/2! + x⁴/4! − ... (each term = previous ×
(x² / ((2j−1) 2j)), integer division first, until a term ≤ 1). The remake: sin and cos of 2π a / 65536.

### 6.3 Environment coordinates (0x414160)

From the rotated normal (length 16384), per normal:

    eu = T[nx >> 6],  ev = T[ny >> 6]       T[i] = round(8192 + asin(i / 256) * 16384 / π), T[256] = 16383

(the table in the image, i = −256..256, values 0..16383 in 10.6 fixed point). In texels: **u = 128 + (256/π)
asin(nx / 16384), v = 128 + (256/π) asin(ny / 16384)**, using the object's rotation only (the camera never turns).
A normal facing the viewer samples the map's centre; v grows with ny, so upward normals sample the lower half of the
picture. nz is not used.

### 6.4 Projection (0x414000) and the camera

Arguments: scale x, scale y, camera z, object position x, y, z; centre in ESI, EDI.
```c
kx =  (int32)(sx * camz) >> 20;  ky = -((int32)(sy * camz) >> 20);
for each vertex (not the centres): d = camz - (z + pz); if (d == 0) keep the old screen position;
    screen_y = (y + py) * ky / d + cy;  screen_x = (x + px) * kx / d + cx;   // idiv: truncated; stored as words
```
Screen: sx = 0xd8f0, sy = 0xb4c6, camz = −7000, centre (160, 100): kx = −371, ky = 309:

    X = 160 + trunc(371 (x + px) / (7000 + z + pz)),   Y = 100 − trunc(309 (y + py) / (7000 + z + pz))

The camera sits at z = −7000 looking along +z, y up; 371 / 309 = 1.2 is the 320 × 200 pixel aspect, so in a 4:3
picture the focal length is 371 / 160 of the half width (horizontal field of view 46.6°). The remake projects with
these focal lengths in float (scaled to its resolution).

### 6.5 Back faces, face order (0x4140e0, 0x411980, 0x4119f0, 0x411a30)

A face is skipped when (x0 − x1)(y2 − y1) < (y0 − y1)(x2 − x1) (screen coordinates, 16-bit differences, 32-bit
products). The remaining faces get the key zmin + (zmax − zmin) / 2 of their three rotated z (object space, 16-bit,
sar 1) and are drawn **farthest first**: fewer than 200 faces by a stable insertion sort (descending), 200 or more
(most frames: up to 245 faces pass the test, the heart is concave) by Numerical Recipes' heap sort with reversed
comparisons (descending, not stable). The four objects are drawn in the order of their spline z, farthest first
(0x415480, stable insertion sort of (z, k)), each completely over the ones before.

## 7. The rasterizers

Both take integer screen vertices, sort them by y, and walk a long edge and two short edges (left edge with the
attributes, right edge x only), with the 16.16 edge set-up: x = x0 << 16, dx = ((x1 − x0) << 16) / dy (idiv), for
dy = 2: (x1 << 16 − x0 << 16) >> 1, dy = 1: no step; attributes (10.6 values) A = A0 << 10 with steps
((A1 − A0) << 10) / dy, so attributes are 16.16 texels. Rows y0 .. y2 − 1. Gradients along x are constant per
triangle: with t = ((ym − y0) << 16) / (y2 − y0) and the width w = t (x2 − x0) + ((x0 − xm) << 16) (16.16; if
|w| ≤ 0x1000, w <<= 12), dA/dx = ((t (A2 − A0) + ((A0 − Am) << 16)) << 10) / w. w < 0 puts the long edge on the left.
Triangles crossing the clip window (0..xmax, 0..ymax) use the clipping variants: rows above ymin are skipped with
the edges pre-stepped by the skipped rows (then the steps use plain idiv also for dy ≤ 2), spans are cut at xmin /
xmax with the attributes pre-stepped by the skipped pixels.

### 7.1 Bump-environment rasterizer 0x414f13 (all four hearts on screen)

Input per vertex: screen x, y; bump u, v (vertex +0, +2); env u, v (normal +0, +2); the env map (64 KB aligned)
and the bump table (its address patched into the instruction at 0x41535b; the four steps patched into the adds at
0x415369 / 0x41536c, 0x415371 / 0x415374, 0x41537e / 0x415381, 0x415384 / 0x415387).

Span of a row: pixels floor(xl) .. floor(xr) − 1 (no sub-pixel correction), starting with the left edge's
attributes. In the span all four coordinates are **8.8** (integer byte and the top byte of the fraction), stepped
by the 8.8 steps, integer parts wrapping at 256:

```c
for (i = 0; i < n; i++) {
    (dx, dy) = bumptab[bv >> 8][bu >> 8];              // nearest bump texel
    index    = env[((ev >> 8) + dy) & 255][((eu >> 8) + dx) & 255];   // nearest env texel
    *dst++   = index;
    bu += dbu; bv += dbv; eu += deu; ev += dev;        // 16-bit wrap
}
```

The pixel's index is **a texel of the environment map** (left half for the solid hearts, right half with the hearts
drawn in for the transparent one), looked up at the interpolated environment coordinates shifted by the bump
offsets. No shading, no table, no index arithmetic: the remake looks up the env map's RGB (filtered). Offsets and
coordinates wrap inside the 256 × 256 half (GL_REPEAT on each half as its own texture; the rims of both maps are
dark, so the wrap at silhouettes shows little).

### 7.2 Affine texture rasterizer 0x41448a (hearts into the env map)

Input per vertex: x, y, u, v (u, v = the normal's env coordinates); texture = envL. Spans run from round(xl) to
round(xr) − 1 (round half up: the bit shifted out is added). The texture coordinates are pre-stepped to the pixel
centre: by the left edge's fraction in eighths (table 0x823450: 0, d/8, d/4, 3d/8, subtracted when the fraction is
< 0.5, else added with (1 − fraction)) and by + d/2. In the loop u, v are 8.16 (integer byte, 16-bit fraction);
texel = tex[v >> 16 & 255][u >> 16 & 255], nearest.

## 8. The transparent heart

### 8.1 Hearts behind it painted into its environment map (0x40674b..0x4069a8)

In the object loop the transparent heart (object 0) comes first and leaves its z in zt. For each solid heart with
**z > zt + 500** ("behind"): for the first such heart of the frame envR is reset from envR0; then the heart is
projected for the env map and drawn into envR (256 × 256, pitch 256, rows 0x8336b8, clip 0..256 × 0..256) with
0x41448a, textured with the orange envL by its own environment coordinates (no bump):

    camz = −D with D = z − zt + 10000,  kx = floor(camz / 16) (= (0x10000 camz) >> 20),  ky = −kx
    u_px = 128 + trunc((x_v + px) kx / (camz − z_v)),   v_px = 128 + trunc((y_v + py) ky / (camz − z_v))
    i.e.  u ≈ 128 + (x_v + px) / 16 · D / (D + z_v),     v ≈ 128 − (y_v + py) / 16 · D / (D + z_v)

(x_v, y_v, z_v rotated vertex, px, py the heart's position, its z left out). Back faces and face order as on
screen. So a heart behind appears in the blue map at 1/16 scale, shifted from the map's centre by its own world
position / 16 (not by its position relative to the blue heart), slightly smaller the farther behind it is. Then
the heart is projected again for the screen and drawn normally. The blue heart, drawn with envR, shows these
images wherever its normals sample them: a fake refraction.

### 8.2 Order

Objects are drawn farthest first, so a heart behind the blue heart is drawn before it and the blue heart covers it;
its image inside the blue heart replaces the view through it. A heart in front is not drawn into the map.

### 8.3 Stale map (keep)

envR is only reset when a heart is behind. In a frame where no heart is behind, envR still holds the hearts of the
last drawn frame that had one, and the blue heart keeps showing them. Hearts are behind (z > zt + 500) in the frames
shown at records 547..558, 919, 1051..1585, 1719..2539 and 2671..2706 (the blue heart is off screen before 1591).
`stale_scan.py` renders each frame without a heart behind with the stale and with a clean map: the stale map is
**visible in records 1591..1717**, the blue heart's first 1.8 s on screen: it enters showing the orange hearts
painted into its map for record 1585 (up to 10,876 pixels differ from a clean map; record 1653 in
`runs/stale_1200.png`). In 2541..2669 the stale image lies where the blue heart's normals do not sample it. The
remake: when no heart is behind at count c, use the map of the latest count c' < c that had one (scan back over
integer counts), so the result depends on time only.

## 9. Frames, buffers, timing

### 9.1 Main loop (WaterEffect 0x4064b0, eax = POS 4, edx = PAT 63: the script line's position is the end)

```c
memcpy(buffer, back, 64000);          // only the first buffer
set_retrace(0x4059cc);                // [0x832504]
dac_upload(dacpal);                   // 0x413b70: out 0x3c8 = 0, 768 bytes to 0x3c9, at once (no retrace wait)
start = [0x832500];
do {
    if (key [0x8232ee]) quit = 1;
    music_pos();                      // 0x400104
    if (order >= 4 && row >= 63) break;            // unsigned: the first drawn frame at or after 4:63
    behind = 0;
    for (k = 0; k < 4; k++) {
        t = ([0x832500] - start) * 512 + obj[k].offset;
        if (t >= tend /* 24 << 16 */) { if (!looping) goto done; start = [0x832500]; }
        x, y, z, a, b, c = spline(obj[k], t) >> 8;
        rotate (6.2), env coordinates (6.3);
        if (k == 0) zt = z;
        else if (zt + 500 < z) draw into envR (8.1);
        project for the screen (6.4); back faces (6.5);
    }
    sort objects by z; draw each with 0x40580c (env = k ? envL : envR, bumptab);
    buffer = flip();                  // 0x4035b0
} while (!quit);
done: set_retrace(0); free everything;    // VGA keeps the last copied frame
```

The spline end (t ≥ 24 << 16 for the leading heart at count 2701) would end the effect too; the music (4:63, count
≈ 2685) comes first.

### 9.2 Where it draws

Into one of two 320 × 200 buffers. The retrace routine 0x4059cc, called by MIDAS's timer interrupt in every
vertical retrace:

```c
[0x832500]++; since++;                               // the water's clock: +1 per refresh
if (drawn != shown && since >= minint) {             // a finished frame waits
    minint = since;                                  // [0x821e80], starts at 1
    copy buffers[shown] to 0xa0000 and refill it with the background (0x414b59), 64,000 bytes, in the retrace;
    shown = (shown + 1) % 2; since = 0;
}
```

flip 0x4035b0: drawn = (drawn + 1) % 2 after waiting while it equals shown; if it waited more than one refresh in
more than 70 / minint flips in a row, minint is lowered (by waited / 2 if waited > 4, else by 1). So the frame rate
locks to 70 / minint frames per second.

**The whole frame is overwritten**: every buffer starts as the background picture and the full 64,000 bytes are
copied each time.

### 9.3 Time base and what the capture shows

- count = [0x832500] − start = the frame counter [0x825c5c] − its value when WaterEffect started (16: the command ran
  in record 20's refresh, frame 21 in `part2.script`). Both counters step in the same retrace; during record k the
  count is k − 20. The frame drawn with count c is finished before the next retrace, but that retrace copies the
  other buffer (the previous frame), so it is visible **from record 22 + c**.
- In the capture at 100 M instructions per second: 1,743 frames in 2,686 refreshes (798 shown 1 refresh after the
  previous one, 943 after 2). minint is 1 (up to 70 frames per second) to record 792, 2 (35 per second: each frame
  shown at record 24 + c, every other record) from record 793, and 1 again from record 2678.
- **The remake: count = r − 22** at display refresh r (r − (f + 1) with f = 21 the WaterEffect line's frame in
  `part2.script`), evaluated every displayed frame; continuous between refreshes.
- Record 21 shows the first frame (count 0, drawn at once; background only). Record 22 is **black**: the second
  frame went into the second buffer, which was never filled with the background (only zeroed by 0x403648), and the
  objects are still off screen. A start-up artefact: drop it. From record 23 on, record r shows count r − 22.
- The palette is set once (0x413b70) at the start, before the first frame is copied.

## 10. Time bases

| variable | time base | formula |
|---|---|---|
| every object's x, y, z, a, b, c | refresh counter | s = count / 128 + offset / 65536, natural cubic spline (5.) |
| objects in one frame | per object clock read | the clock is read once per object: 3 of 1,743 frames have the transparent heart one refresh behind the rest. Use one count per frame |
| transparent env map with hearts | drawn frames (8.3) | stale map from the last drawn frame with a heart behind: use the last count with one |
| frame pacing, display latency | drawn frames | 70 or 35 frames per second (9.3); the remake draws every refresh at count = record − 22 |
| end of the effect | music | first drawn frame at order ≥ 4 and row ≥ 63 (the program also stops at a key press) |
| random numbers | none | |

Nothing is stepped per drawn frame: the motion does not depend on the frame rate.

## 11. For the remake: shortcuts to evaluate exactly, and what stays

- Spline in 16.16 with values × 256 → doubles. Positions and angles truncated to integers (sar 8) → continuous.
- Integer object-space vertices and three successive rotations with 16.16 sines from a 2048-step table, each
  result truncated to 16 bits → float rotation matrices. Integer, truncated screen vertices → float vertices,
  anti-aliased edges.
- Environment coordinates from a 513-entry asin table indexed by n >> 6, interpolated linearly in screen space per
  triangle → per pixel from the interpolated normal: u = 128 + (256/π) asin(n̂x), v = 128 + (256/π) asin(n̂y)
  (in texels of the 256 × 256 map). Bump coordinates: affine in the original (constant gradients, 8.8 in the span,
  spans from floor(xl) without sub-pixel correction) → perspective-correct interpolation of the mesh's u, v.
- Bump offsets from the byte table (trunc, forward differences of a 31-level height map, texel-nearest) → computed
  per pixel from the bilinearly filtered height map's gradient: offset = (256 / 3.14) atan(0.01 ∂h/∂texel), in env
  texels; env and bump maps read with filtering. Keep the wrap (GL_REPEAT per half) and the zero slope in the last
  row and column if the seam matters (it is where u, v wrap on the heart).
- Painter's sort of faces inside a heart (keys from integer z; an unstable heap sort) → a depth buffer cleared per
  object; keep the object order (farthest centre first, each object over the previous ones).
- The hearts drawn into the blue map at 256 × 256 with integer vertices → render that map at a higher resolution
  with the same projection (8.1), from the left env half.
- Grain that stays: none. The scaly relief is the bump map's own pattern (magnified, filtered).
- Pictures: watback.pcx is a photograph of stars (no people), drawn as it is: a candidate for a redraw at the same
  composition. The two env maps are read through the bump offsets and mapped on the hearts, not shown as pictures:
  keep them (an upscale of the smooth orange glow is harmless). watbump.pcx is a height map read through the table:
  keep its pixels.
- The palette: watenv.pcx's (6-bit, PCX bytes >> 2), set once; entries 0..127 orange ramp, 130..187 the background's
  magentas, 192..255 the blue map. No palette effects in the part.

## 12. Not determined

- Whether watback.pcx is a scanned photograph or a rendered starfield (it looks like an astrophoto of M45).
- The trailing 27,160 bytes of both `.m3d` files (never read; they look like more face indices).
