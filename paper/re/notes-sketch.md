# Paper — "sketch" part: paper, red pencil, pencil sketch (ticks 1079–3186, music counter 116..366)

All addresses are code32 offsets. "Tick" = 70.0018 Hz IRQ0 tick. Everything below was checked against the
emulator: a Python re-implementation of the renderer (scene lines, polygon fills, shadow, paper composite, pencil)
reproduces the back buffer of ticks 1250, 2000 and 2001 **bit-exactly** (frames taken from runs resumed at the
t1250 / t2000 snapshots with a hook at 0x604a / 0x610a / 0x611a), the 3D transform of every scene and pencil vertex
matches exactly, and a simulation of the stroke callback reproduces the snapshot state at t2000 and the part end
at tick 3186 exactly. Scripts in
`paper/re/models/sketch/`
(run_sk.py = run_paper.py copy with `--dump-at` hooks; render.py, cmp1.py, cmp2.py, xf.py, sim.py,
numbering.py, rng.py).

## 1. What is on screen, and the three phases

A grey paper sheet (per-pixel embossed noise) with a 3D red pencil (flat-shaded hexagonal prism, yellow wood,
dark graphite) that casts a hard shadow on the paper. The whole screen fades in from white. The pencil rises and
turns, then draws ("sketches") the silhouette/crease edges of the *same 3D scene that the next part shows in colour*
(flower, house with windows and door, a tree, the ground/path lines), seen from the scene camera, as wobbly
grey pencil lines with hidden-line removal.

| phase | ticks | counter | main loop | callback [0x1e5ef] | what happens |
|---|---|---|---|---|---|
| 1 | 1079–1180 | 116..127 | 0x7a64, 0x13af(1), 0x603f, copy | 0 | static pencil (lying flat, 120 units above the paper) at screen ~(96,121); palette fades from white (level 0xff00) to normal (0x8000) at 400/tick, done at tick 1161 |
| 2 | 1181–1303 | 128..141 | 0x7a64, 0x603f, copy until [0x55d2]==0 | 0x61ee | pencil rises towards the viewer (z −2/tick), moves right (x +1/tick), tilts up (angle a −2/tick) — 123 ticks |
| 3 | 1304–3186 | 142..366 | same, until [0x1ea95]==0x113 | 0x6267 | pencil lands (z +10/tick) and draws 275 strokes; part ends when the 275th stroke *starts* (IRQ of tick 3185) |

Phase 1 ends on the music counter (>= 0x80); phases 2 and 3 end on tick-driven state, so the part length
(3186) is the same on every machine. The 0x7a64 key check runs once per frame (ESC = quit); scan code 0x0a
('9') jumps straight to 0x78c8 (the coloured 3D part, skipping the transition).

## 2. Main sequence (0x76dd..0x7827)

```c
effect_cb = 0;                                    // [0x1e5ef]
p = getmem(0x20000); img_sketch = p;  img_colour = p + 0x10000;   // [0x5063], [0x5067]: 64000-byte images
setup_stroke_table();                              // 0x5e7f (§9.1)
ncam_keys = 23;                                    // [0x4827] = 0x17
build_camera_spline();                             // 0x7554: key 0 := current camera [0x5de8..]
eval_camera_spline();                              // 0x74e8 at t=[0x482b]=0 -> camera = key 0 (§11)
pencil_x = 100;                                    // [0x55d6] (image value 1000 = "no pencil")
stroke_idx = 0;                                    // [0x1ea95]
mode = 2;                                          // [0x1ea94]: 2 = sketch, 1 = pencil, 0 = colour
palette_copy_and_upload();                         // 0x15af: static pal 0x169a3 -> work 0x1eaad, upload at
                                                   //  current level [0x1edb5] = 0xff00 (white, from the title)
render_frame(); copy back buffer -> VGA;           // 0x603f, first frame (all white on screen)
fade_step = 0x190; fade_target = 0x8000;           // [0x1edb9], [0x1edbd]: white -> normal, 400 per tick
part_end = 0x80;                                   // [0x1ea8f]
do { key_check(); blur_precalc(1); render_frame(); copy; '9' -> skip; } while (music_counter < 0x80);   // phase 1
effect_cb = 0x61ee;
do { key_check(); render_frame(); copy; '9' -> skip; } while (pencil_a_cnt != 0);          // [0x55d2], phase 2
effect_cb = 0x6267;
do { key_check(); render_frame(); copy; '9' -> skip; } while (stroke_idx != 0x113);        // phase 3
// 0x7827.. (end of this part, start of the transition handled by "scene3d"):
mode = 0; scene_render();  copy back buffer -> img_colour;   // 0x5eec: coloured scene + all edges, no paper
mode = 2; scene_render();  copy back buffer -> img_sketch;   // 0x5eec: the sketch, no paper, no pencil
cam_t_acc += 0xf0; cam_t = cam_t_acc >> 7; 0x7207 ...         // [0x466b], [0x482b]: next part
```
Note: in the final `img_sketch` the 275th stroke (ground, (200,150)->(191,151), 9 px) is missing: the loop exits
as soon as it starts, and it is never completed (only drawn as a zero-length partial line). The colour image
(mode 0) does not use the stroke flags at all.

## 3. Static data and generated tables

### 3.1 Palette (static): 768 bytes, 6-bit RGB, at code32 0x169a3 (image offset 0x176a3)
Uploaded by 0x15af/0x15c0 (never modified in this part). Layout: 0x00–0x3f grey ramp (level = 0,0,1,2,…; i.e.
idx−1 for idx 2..52, idx for 53..63), 0x40–0x4f misc greys/reds, 0x50–0x5f black, then five 32-entry ramps
black→colour→white: 0x60 green, 0x80 blue, 0xa0 yellow, 0xc0 brown/skin, 0xe0 red (full dump in appendix B).
This part uses the greys (paper, lines, shadow, graphite), yellow (wood) and red (paint).

Fade (0x15c0, called from the IRQ whenever the level moves), per channel `p` of the static palette:
```c
l = level >> 8;                                   // level [0x1edb5], 0x8000 = normal
if (l <= 0x80) out = (p * l) >> 7;                // fade from black
else { b = min(l, 0xff) - 0x80;                   // 0..127, fade to white
       out = (((p * (0x7f - b)) >> 6) + b) >> 1; }
```
level steps by [0x1edb9] towards [0x1edbd] once per tick (0x1e45). Here: 0xff00 → 0x8000 in steps of 400:
first step at the IRQ of tick 1080, reaches 0x8000 at tick 1161 (white → normal in 82 ticks = 1.17 s).

### 3.2 Random generator 0xaa2 (used for all procedural tables of this part)
```c
uint32 X[64];   // static initial contents at code32 0x9a0 (256 bytes); i = [0xaa0] = 29, j = [0xaa1] = 63
uint32 rnd() { X[j] += X[i]; uint32 r = X[j]; i = (i+1) & 63; j = (j+1) & 63; return r; }
```
Calls (from program start): 0..71 discarded (main 0x762d), 72..1095 grey table, 1096..123975 jitter table,
123976..140056 paper texture (verified: regenerating from the static state reproduces all three tables exactly).

### 3.3 Paper texture (0x55e2, at init) — [0x1edf5], 320x200 bytes (buffer 0x10200)
```c
uint8 R[64324];  // 0x3ed1 calls: each rnd() & 0x0f0f0f0f gives 4 bytes (little endian), values 0..15
for (i = 0; i < 64000; i++)
    tex[i] = 0x1e + ((int8)(R[i] - R[i + 321]) >> 3);     // arithmetic shift: floor((a-b)/8) -> 0x1c..0x1f
```
i.e. white noise embossed along the diagonal (pixel minus its lower-right neighbour, 321 = one row + one
column): a grain lit from the upper left. Values: 0x1c 10.9 %, 0x1d 36.2 %, 0x1e 38.8 %, 0x1f 14.1 %. This is the
only "lighting" of the paper; it is static for the whole part.

### 3.4 Paper/darkening composite table T (0x144b, at init) — [0x1eded] (64K-aligned), 256x256 bytes
Row = "texture/darkness" value ch (0..0x40 used), column = colour index cl:
```c
c = (ch <= 0x20) ? ch : ((ch - 0x20) >> 1) + ((ch - 0x20) >> 3);      // shadowed texel 0x3c..0x3f -> 17..18
T[ch][cl] = (cl < 0x40) ? max(0, cl + 2*c - 0x40)                         // grey ramp: subtract (64 - 2c)
                        : (cl & 0xe0) + max(0, (cl & 0x1f) + c - 0x20);   // colour bands: subtract (32 - c), stay in band
// rows 0x41..0xff are 0; a second table at [0x1eded]+0x10000 = min(255, (cl*ch) >> 5) is for the title part.
```
Used two ways: (a) the pencil line darkens a pixel: `pix = T[g][pix]` with g = 0x1a..0x1f (subtract 12..2 grey
levels); (b) the paper composite 0x1943: `pix = T[tex][pix]` (subtract 8..2, or 30..28 in the shadow).
Verified against the snapshot table.

### 3.5 Pencil grey table and jitter table (in 0x5b7d, at init)
```c
uint8 grey[0x400];      // [0x1ee09] (= [0x1ee05] initially)
for k: r = rnd() & 0x1f;  grey[k] = (r < 5) ? (r >> 2) + 0x1e : (r >> 3) + 0x1a;   // 0x1a..0x1f
int8  jit[0x400];       // [0x1ee0d] = grey + 0x400
for k: { s = 0; repeat 120: s += (rnd() & 0xf) - 7;  jit[k] = (s >> 6) - 1; }      // -3..2, mostly -1/0
```
Distribution: grey 0x1a 11 %, 0x1b 24 %, 0x1c 26 %, 0x1d 25 %, 0x1e 11 %, 0x1f 3 % (mean darkening 7.8 levels per
pass); jit −3: 8, −2: 114, −1: 432, 0: 383, 1: 82, 2: 5 (of 1024).

### 3.6 Other tables
- Sine table 0x22479: 1024 dwords, `fistp(sin(i*2*0.0030679617f)*65536)` (FPU, round to nearest); equals
  round(65536*sin(2πi/1024)) except i=661 (−51912 vs −51911) and i=905 (−43712 vs −43713). cos(a) = sin(a+256).
- Reciprocal table: `R[n] = 65536 / n` (truncated) for n = 1..2048, R[n] at 0x1ee15 + 4n.

## 4. Per-frame render 0x603f (shared with the coloured part)

```c
void render_frame() {                                 // 0x603f
    saved = mode;
    scene_render();                                   // 0x5eec (§5): scene in the current mode -> back buffer
    if (pencil_x >= 1000) {                           // [0x55d6] >= 0x3e8: no pencil (coloured part sets 2000)
        composite(back, paper_tex);                   // 0x1943 with ebp = [0x1edf5]
    } else {
        P = pencil_obj;                               // [0x5de4]
        P.pos.x = ((pencil_x - 160) * 0x8edb6) >> 16; // 0x8edb6/65536 = 8.92856 = 2000/224: screen px -> world at z=0
        P.pos.y = ((pencil_y - 100) * 0x8edb6) >> 16; // pencil_y = [0x55da]
        P.pos.z = pencil_z - (travel ? 50 : 0);       // [0x55de]; travel = [0x506b] (pen lifted between strokes)
        CAM = rot(0, 0) /*identity*/; CAMPOS = (0, 0, -2000);       // [0x1e468], [0x1ea9d..0x1eaa5]
        a = 2*pencil_a_cnt + 10;                      // [0x55d2]; angles in 1/1024 turn
        b = a + 90 - ((pencil_x - 160) >> 1);         // >> = arithmetic (floor)
        P.R = rot(b, a);                              // 0xb1f
        transform_and_bucket(P);                      // 0x3fb3 (still in mode 2: no effect for the pencil)
        mode = 1;
        shadow(P);                                    // 0x42e4: [8] = paper_tex with the pencil shadow OR 0x20
        composite(back, shadow_tex);                  // 0x1943 with ebp = [8]
        draw_buckets();                               // 0x43df in mode 1: filled pencil faces, no edges
    }
    mode = saved;
}
```
The paper texture is applied to everything drawn by 0x5eec (lines included) but **not** to the pencil, which is
drawn afterwards on top. Branches for later use: `pencil_x >= 1000` (the coloured part, 0x78d9 sets 2000), and the
mode-0/order branches of 0x5eec below.

## 5. Scene render 0x5eec

```c
void scene_render() {
    memset(back, 0x3a, 64000);                        // 0x3a = paper colour index
    CAMPOS = (cam[2] >> 10, cam[3] >> 10, cam[4] >> 10);   // cam = [0x5de8]: yaw, pitch, x, y, z
    CAM = rot(cam[0] /*yaw b*/, cam[1] /*pitch a*/);       // 0xb1f
    h = 100 - (sin(pitch)*0xe0 / (cos(pitch) ? cos(pitch) : 1));   // ints from the sine table, idiv truncates
    if (mode == 0) {                                  // colour only (not used in this part):
        vgradient(h - 350, h, 0x80, 0x9f);            //  0x1bae: sky rows h-350..h-1, colour 0x80 -> 0x9f (16.16 lerp per row)
        vgradient(h, h + 1400, 0x6d, 0x6d);           //  ground rows, colour 0x6d
        pencil_line(0, h, 320, h);                    //  0x18b7 horizon
    }
    mark_sketch_flags();                              // 0x6133 (§9.3)
    if (cam_t /*[0x482b]*/ < 0x600)                   // this part: groups {0,1} {2..8} {9,10}
        draw_group(0,1); draw_group(2..8); draw_group(9,10);
    else  /* later in the coloured part */            // {0,1} {5,6,7} {2} {9,10} {3,4} {8}
        ...;
}
draw_group(list) = for each object: transform_and_bucket(obj) (0x3fb3); then draw_buckets() (0x43df)
```
Each group is depth-sorted on its own and painted over the previous groups (so the windows/door and the flower
always cover the house, trees and ground).

In this part the camera is constant: yaw 20, pitch −28 (1/1024 turn = 7.03°, −9.84°), position
(−704, 576, −1600); horizon h = 138 (unused in mode 2).

## 6. The 3D engine as used here

### 6.1 Object record (built by 0x3cb7 for the scene, 0x39a4 for the pencil, 0x3a8c for the banner)
```
+0x00 R[9]   object rotation, int32 16.16, row-major      +0x24 M[9] = CAM*R (per frame)
+0x48 pos    int x,y,z (world units)                      +0x54 pc[3] = CAM*(pos-CAMPOS), 16.16
+0x60 lo[3]  light dir in object space, length 200        +0x6c nverts  +0x70 nfaces
+0x74 faces* (0x4c bytes each)                            +0x78 verts* (0x24 bytes each)
vertex: +0 x,+4 y,+8 z (int) | +0xc cx,+0x10 cy,+0x14 cz (16.16 camera space) | +0x18 sx,+0x1c sy (1/32 pixel)
        | +0x20,+0x22 int16 shadow screen x,y (pencil only, 0x42e4)
face:   +0 +4 +8 vertex*  | +0xc +0x10 +0x14 neighbour face* across edge 0 (v0v1), 1 (v1v2), 2 (v2v0) (self if none)
        | +0x18 bucket link | +0x1c colour add for mode 2 (0x20 scene, 0 pencil/banner) | +0x1d flags
        | +0x1e base colour | +0x1f colour this frame | +0x20 sketch flags | +0x24 normal[3] int, length 256
flags +0x1d: bits 0-2 neighbour across edge i hidden this frame (silhouette), bits 3-5 crease edge i (static),
             bit 7 face hidden this frame
```
Scene mesh (0x3cb7): table at 0x174a3, 11 entries of 12 bytes: `u8 nverts, u8 nfaces, s16 ox, oy, oz, u32 mesh`;
mesh = nverts × (s16 x, y, z) [+ (ox,oy,oz)], then nfaces × (u8 v0, v1, v2, attr). base colour =
table0x3db8[attr >> 4] − 4 with table0x3db8 = {f0,70,b0,d0,30,d5,10,90}; crease bits 3-5 = (attr & 7) << 3; +0x1c =
0x20; R = identity, pos = 0 (vertices are absolute world coordinates). Pencil mesh (0x39a4 from 0x1e353): `u8 nv,
nf`, nv × (s8 x,y,z) × 8, nf × 4 bytes; base = table0x3a89[attr>>4] (= {0x10, 0xb0, 0xeb}); +0x1c = 0.
0x3eeb (after creation): 0x3dc0 normal `c = (v1−v0) × (v1−v2)` (ints), `len = round(sqrt(|c|²))` (1 if 0),
`n = (c << 8) / len` (truncating) → length ~256; 0x3f37 neighbour = the face containing the reversed edge (self if
none); crease bit 3+i is ORed in if `n·n_neighbour <= 10000` (angle >= ~81°).

| obj | mesh | nv/nf | colours (base) | what | in the sketch view |
|---|---|---|---|---|---|
| 0 | 0x17de3 | 20/36 | 0xac | sun (far away) | off screen (x >= 435) |
| 1 | 0x186cb | 20/18 | 0xd1 | ground / path (y = 814) | 12 strokes |
| 2 | 0x18551 | 17/24 | 0x2c walls, 0xec roof | house | 15 strokes |
| 3, 4 | 0x18617 | 14/24 | 0xec | banner poles | off screen (x >= 418) |
| 5, 6, 7 | 0x17eeb | 121/228 | 0x6c leaves, 0xcc trunk | trees | 5: 6 strokes (right edge), 6: behind camera, 7: 79 strokes (left) |
| 8 | (0x3a8c) | 187/320 | 0x30, +0x1c = 0 | banner cloth (17x11 grid) | drawn but off screen (x >= 432) |
| 9 | 0x17527 | 23/10 | 0x0c, 0x8c | 4 windows + door | 20 strokes |
| 10 | 0x175dd | 177/248 | 0x6c, 0xac, 0xec | flower | 143 strokes |
| pencil | 0x1e353 | 25/46 | 0x10, 0xb0, 0xeb | pencil | [0x5de4] |

### 6.2 Rotation 0xb1f(ebx = b, eax = a) — angles in 1/1024 turn, S(x) = sin table, C(x) = S(x+256)
```c
R = { C(b),                 0,     -S(b),
      ((-S(b))*S(a)) >> 16, C(a),  -((C(b)*S(a)) >> 16),
      (S(b)*C(a)) >> 16,    S(a),  (C(b)*C(a)) >> 16 };      // 64-bit products, >> = floor
```
world = R·v + pos; camera space = CAM·(world − CAMPOS). (b = yaw about y, a = pitch about x.)
0xae2: `M[r][c] = Σk (CAM[r][k] * R[k][c]) >> 16` (each 64-bit product floored, summed in 32 bits).

### 6.3 Transform, cull, shade, bucket: 0x3fb3(obj)
```c
M = CAM*R;  d = pos - CAMPOS;  pc[r] = Σ CAM[r][k]*d[k];                      // 16.16
l = LIGHT - pos; lo[c] = (Σk R[k][c]*l[k]) >> 16;                              // LIGHT = (-1300, 100, -2500) (0x1e48c, static)
lo = round(lo * 200 / |lo|);                                                   // float, round to nearest
for v: cz = Σ M[2][k]*v[k] + pc[2];
       if (cz > 0x1900000) {                                                   // 25.0
           e = 0xe00000 / (cz >> 16);                                          // 224*65536 / z
           cx = Σ M[0][k]*v[k] + pc[0];  sx = (int32)(((int64)cx*e) >> 27) + 160*32;
           cy = Σ M[1][k]*v[k] + pc[1];  sy = (int32)(((int64)cy*e) >> 27) + 100*32; }
           // => screen = (160 + 224*X/Z, 100 + 224*Y/Z), 5 fractional bits; cx,cy,sx,sy kept stale if z <= 25
for f: if (any cz <= 25.0 || !((sy2-sy0)*(sx1-sx0) > (sx2-sx0)*(sy1-sy0))) f.flags |= 0x80;   // hidden / back face
       else { f.flags &= 0x7f;
              col = ((n · lo) >> 12) + f.base;  band = (col & 0xff) >> 5;      // shade = floor(12.5*cos) in -13..12
              if (mode == 2) col += f.add;                                     // +0x20 for scene faces
              f.col = ((col & 0xff) >> 5 != band) ? 0x3a : col; }              // => every scene face is 0x3a in mode 2
for visible f: f.flags = (f.flags & 0xf8) | hidden(nb0) | hidden(nb1) << 1 | hidden(nb2) << 2;
               key = clamp(((cz0 >> 16) + (cz1 >> 16) + (cz2 >> 16) - 1200) >> 6, 0, 1023);
               push_front(bucket[key], f);                                     // heads at 0x23479 + 4*key
```
For the pencil (mode 1) the colour is simply `(base + shade) & 0xff` (can leave its 32-band: e.g. red 0xeb−13 =
0xde would show the brown band's near-white; observed pencil colours 0x15–0x1b, 0xad–0xbb, 0xe9–0xf5).

### 6.4 Bucket renderer 0x43df
```c
for (key = 1023; key >= 0; key--)                     // far to near
  for (f = bucket[key]; f; f = f->link) {             // last inserted first
      p[i] = (v_i.sx >> 5, v_i.sy >> 5);              // integer pixels (floor)
      fill_triangle(back, p0, p1, p2, f.col);         // 0x1a87 + 0x1b10
      if (mode == 1) continue;                        // pencil: no edges
      m = f.flags;  if (mode == 2) m &= f.sketch;     // +0x20
      if (mode == 2 && f == cur_face)                 // [0x5047]: stroke being drawn
          pencil_line(pencil_x, pencil_y, stroke_x0, stroke_y0);   // from the pen to the stroke start [0x5053],[0x5057]
      if (m & 0x09) pencil_line(p0, p1);
      if (m & 0x12) pencil_line(p1, p2);
      if (m & 0x24) pencil_line(p2, p0);
  }  // buckets are emptied while walked
```
Hidden-line removal is the painter's algorithm: each face is filled with the paper colour 0x3a before its edges
are drawn, so nearer faces erase the lines of farther ones (within a group; later groups cover earlier groups).
In mode 0 (coloured part) all silhouette + crease edges are drawn over the coloured faces.

### 6.5 Triangle fill (0x1a87 edges via 0x1982, spans 0x1b10 = set, 0x1b62 = OR)
```c
ymin = 0xff; ymax = 0;
edge(x0,y0,x1,y1) for (p0,p1), (p1,p2), (p2,p0):
    if (y0 == y1) return;  slot = RIGHT;  if (y0 > y1) { swap; slot = LEFT; }   // downward edges = right side
    if (y0 >= 200 || y1 <= 0) return;
    s = R[y1-y0] * (x1-x0);  x = x0 << 16;  if (y1 > 200) y1 = 200;
    if (y0 < 0) { x -= y0*s; y0 = 0; }
    ymin = min(ymin, y0); ymax = max(ymax, y1);
    for (y = y0; y < y1; y++, x += s) tab[y][slot] = clamp(x, 0, 320 << 16);
for (y = ymin; y < ymax; y++) for (x = tab[y][LEFT] >> 16; x < tab[y][RIGHT] >> 16; x++) put(x, y);
```
Triangles with the other winding produce empty spans (that is how the unculled shadow triangles of 0x42e4 work).

## 7. The pencil line 0x18b7 and rasteriser 0x1612

```c
void pencil_line(x0, y0, x1, y1) {                    // 0x18b7, integer pixel endpoints
    int8 *j0 = jit + ((x0*y0 + y0) & 0x1ff);          // per-endpoint jitter, a hash of the endpoint
    int8 *j1 = jit + ((x1*y1 + y1) & 0x1ff);
    uint8 *g = grey + ((x0 + y0 + x1 + y1) & 0xff);   // same grey sequence for all 3 passes
    for (pass = 0; pass < 3; pass++) {
        Y1 = y1 + j1[0]; X1 = x1 + j1[1]; j1 += 2;
        Y0 = y0 + j0[0]; X0 = x0 + j0[1]; j0 += 2;
        dda(X0, Y0, X1, Y1, g);                       // 0x1612
    }
}
void dda(x0, y0, x1, y1, g) {                         // 0x1612: 2-pixel-wide DDA, 16.16
    order so that y0 <= y1;  dx = x1 - x0;  dy = y1 - y0;
    if (|dx| <= dy) {                                 // y-major: rows y0 .. y1-1 (last row not drawn)
        s = R[dy] * dx;  x = x0 << 16;  clip rows to [0,200) (x -= s*y0 if y0 < 0);
        skip rows while x is outside [0, 320<<16) (stop when it leaves);
        for each row y: X = x >> 16;
            back[y][X]   = T[g[0]][back[y][X]];       // darken
            back[y][X+1] = T[g[1]][back[y][X+1]];     // second pixel to the right
            g += 2;  x += s;
    } else {                                          // x-major: from the left end, columns x .. x+dx-1
        s = R[|dx|] * dy_signed;  y = y_left << 16;  clip columns to [0,320) (y -= s*x if x < 0);
        skip columns while y is outside [0, 200<<16);
        row = y >> 16; frac = (y << 16) as uint32; step = (s << 16) as uint32 (negated for upward lines);
        for each column X: back[row][X]   = T[g[0]][...];
                           back[row+1][X] = T[g[1]][...];   // second pixel below
                           g += 2;  frac += step; on carry: row += (down ? 1 : -1), stop when leaving [0,200)
    }
}
```
Details that matter for an exact port: no rounding (x0<<16 start, truncating reciprocal slope); for upward x-major
lines the fractional accumulator counts up, so their y is effectively rounded up instead of down; when the start is
off screen, the skip loops advance k rows/columns but reduce the count by only k−1, so such a line gets one extra
row/column at its end (all four cases; mirrored in render.py). The second pixel of a row at
x = 319 lands on x = 0 of the next row; row 199's second pixel lands below the buffer (in its slack).

Look: each edge = 3 overlapping 2-px-wide DDA lines whose endpoints are offset by the jitter table (−3..2 px,
mostly −1/0, so on average ~0.5 px up-left), each pixel darkened by a pseudo-random 2..12 grey levels (grey
table walked 2 entries per step from an offset that depends on the endpoints). Because jitter and grey offset
depend only on the endpoint coordinates, a finished line is identical in every frame (no flicker). The partial
line of the stroke in progress is drawn from the pen to the stroke start, so its jitter/grey pattern changes
each tick and differs from the finished edge (drawn v_i→v_i+1 in face order) — the stroke visibly "settles" when
completed. 35 edges are stroked twice (once for each of the two adjacent faces, often back to back) and so are drawn twice
(darker).

## 8. Paper composite 0x1943 and pencil shadow 0x42e4

```c
void composite(uint8 *back, uint8 *tex) {             // 0x1943, whole screen
    for (i = 0; i < 64000; i++) back[i] = T[tex[i]][back[i]];
}
// paper:      0x3a + 2*tex - 64  -> index 50/52/54/56 -> grey 49/51/54/56 of 63 for tex 0x1c..0x1f
// in shadow:  tex|0x20 -> c = 17/18 -> 0x3a - 30/-28 -> index 28..30 (grey 27..29)
// line pixels are darkened further by the same (64 - 2c), clamped at 0

void shadow(Object *P) {                              // 0x42e4
    memcpy(stex /*[8]*/, paper_tex, 64000);
    e = 0xe00000 / 2000;                              // = 7340 (224*65536/2000, -CAMPOS.z)
    for v: wz = (v.cz >> 16) - 2000;                  // world z (camera at z=-2000, identity rotation)
           t  = (2500 << 16) / (wz + 2500);           // light ray L->v hits z=0 at t (16.16), int division trunc.
           X  = ((v.cx >> 16) + 1300) * t + (-1300 << 16);       // 32-bit; world x of the shadow point, 16.16
           Y  = ((v.cy >> 16) - 100) * t + (100 << 16);
           v.shx = (int)(((int64)X * e) >> 32) + 160;  v.shy = (int)(((int64)Y * e) >> 32) + 100;
    for all 46 faces: fill_triangle_or(stex, sh0, sh1, sh2, 0x20);   // 0x1a87 + 0x1b62, no culling
}
```
So the shadow is the pencil projected from the point light (−1300, 100, −2500) (left, slightly below centre,
behind the camera) onto the paper plane z = 0, then seen through the camera; hard-edged, one level (OR makes
overlaps idempotent), darkening paper and lines alike by ~22 grey levels. It falls to the right of the pencil.

## 9. Strokes: storage, order, speed

### 9.1 Static stroke table (code32 0x5073, 0x113 = 275 entries × 5 bytes) and 0x5e7f
Entry = `u32 ordinal, u8 flags`; flags bit0 = edge v0v1, bit1 = v1v2, bit2 = v2v0, bit3 = draw reversed. The
ordinal is the index of a face in the painter's order of a reference render: 0x5e7f renders the objects with the
camera given by the *static initial* values at 0x5de8 (yaw 20, pitch −28, pos = (−694972, 614576, −1627222) >> 10
= (−679, 600, −1590) — slightly different from the camera actually used later) in the groups {0,1}, {2..7},
{9,10} (object 8 skipped), walks the buckets far→near after each group (0x5e1b) numbering the visible faces
0,1,2,… (547 in total), and replaces each table ordinal by the face pointer. Re-implemented in numbering.py:
reproduces all 275 entries. A port can skip this and use the resolved list in appendix A (object, face, from
vertex → to vertex). Raw entries: `rawtable.txt` in the scratch dir.

### 9.2 Stroke sequencer = per-tick callback 0x6267 (phase 3)
State: pen (x,y) [0x55d6],[0x55da] (screen pixels, 320x200); z [0x55de]; travel [0x506b]; step [0x504b];
nsteps [0x504f]; segment start [0x5053],[0x5057]; end [0x505b],[0x505f]; cur_face [0x5047]; stroke_idx [0x1ea95].
```c
void tick_6267() {
    if (z < 0) { z += 10; if (z > 0) z = 0; }         // land: -366 -> 0 in 37 ticks
    for (;;) {
        if (step < nsteps) {                          // move the pen one step
            r = R[nsteps];
            x = sx + (((ex - sx) * step * r) >> 16);  // 32-bit, arithmetic shift
            y = sy + (((ey - sy) * step * r) >> 16);
            step++; return;
        }
        if (stroke_idx >= 0x113) return;
        e = table[stroke_idx]; A,B = the flagged edge's vertices (swapped if bit3);
        cur_face = e.face; step = 0; travel ^= 1;
        if (travel) { cur_face = 0; (sx,sy) = (x,y); (ex,ey) = (A.sx>>5, A.sy>>5); }   // pen up, go to the start
        else { (sx,sy) = (A.sx>>5, A.sy>>5); (ex,ey) = (B.sx>>5, B.sy>>5); stroke_idx++; } // pen down, draw A->B
        L = sqrt((sx-ex)^2 + (sy-ey)^2);
        nsteps = max(0, (int)rint(sqrt(L) * 1.73f) - 3) << travel;   // FPU: sqrt(sqrt(d2))*1.73, fistp (nearest)
    }
}
```
So a stroke of length L pixels takes `n = max(0, round(1.73*sqrt(L)) − 3)` ticks to draw and the pen move to
the next stroke takes `2·n(L_move)` ticks (e.g. 100 px: 14 ticks; 10 px: 2; < 4.1 px: 0 = instant, several strokes
can start in one tick). The pen visits positions start + k/n·(end−start), k = 0..n−1, then the next segment starts
from there (it never quite reaches the end point). Pen speed ≈ 0.58·sqrt(L) px/tick, so long lines are drawn
faster. First pen-up move: from (223,120) to (137,51), 30 ticks; stroke 0 starts at tick 1334; stroke 127 at 2000;
stroke 274 at 3185 (start tick of every stroke in appendix A).

### 9.3 What is shown (0x6133, every scene render)
```c
n_done = stroke_idx + travel - 1;                     // completed strokes
for (k = 0; k < n_done; k++) table[k].face->sketch |= (fl & 7) | (fl & 7) << 3;   // fl = table[k].flags
if (!travel) table[n_done].face->sketch |= 0x80;      // stroke in progress (bit never tested: no effect)
```
A completed edge is drawn when `face.flags & face.sketch` has bit i or i+3, i.e. the edge is a silhouette or
crease edge in the current view and the face is visible. With the fixed camera all 275 strokes stay visible once
done. A stroke becomes "complete" (and is drawn as the full edge) at the tick its pen-up move begins.

Stroke order by object: flower 0–114 (ticks 1334–1817), house 115–123, flower 124–135, ground 136–137,
windows 138–141, ground 142–147, tree5 148–152, ground 153–154, flower 155–161, tree7 162–240 (2267–2751),
flower 241–249, windows/door 250–265, house 266–271, tree5 272, ground 273–274 (3141–3185).

## 10. Pencil model and motion

Model (0x1e353, coordinates ×8): tip at the origin, body along −z. Graphite cone 0..−40 (radius 16, faces
40–45, 0x10 dark grey), sharpened wood cone −40..−168 (radius 16 → 48, faces 28–39, 0xb0 yellow), hexagonal body
−168..−1008 (radius ~48: (0,56),(±48,±24),(0,−48); faces 16–27, 0xeb red), bevel ring −1008..−1016 (faces 4–15,
yellow), end cap at −1016 (radius 16, faces 0–3, 0x10). Vertices 22/23 have x = 8 instead of 16 (data quirk,
keep). Length 1016 units = ~114 px at z = 0.

Pencil camera: identity, at (0,0,−2000), focal 224, so a point on the paper (z=0) at screen (x,y) is world
((x−160)·2000/224, (y−100)·2000/224, 0): the tip is exactly at the pen position when z = 0.
World direction of the pencil body (tip → end) = (S(b), C(b)·S(a), −C(b)·C(a)) / 65536.

| variable | phase 1 (1079–1180) | phase 2 (1181–1303), 0x61ee per tick | phase 3 (1304–3185), 0x6267 per tick |
|---|---|---|---|
| x [0x55d6] | 100 | +1 per tick → 223 | stroke sequencer |
| y [0x55da] | 120 (static) | 120 | stroke sequencer |
| z [0x55de] | −120 (static) | −2 per tick → −366 | +10 per tick → 0 at tick 1340; drawn −50 while travel |
| a_cnt [0x55d2] | 123 (static) | −1 per tick → 0 (ends phase 2) | 0 |
| a = 2·a_cnt+10 | 256 (90°: lying flat) | 256 → 10 | 10 (3.5°: pointing at the viewer) |
| b = a+90−((x−160)>>1) | 376 | from 376 to 69 | 100 − ((x−160)>>1): yaw follows screen x |

In phase 1 the pencil lies parallel to the paper, 120 units up, pointing up-right; in phase 2 it tilts towards
the viewer while rising and sliding right; in phase 3 it stands nearly on end (end cap visible), leaning right
(more to the right on the left of the screen: b = 180 at x=0, 20 at x=320).
Pencil faces: shaded flat colours `(base + floor(12.5·cos θ)) & 0xff` with θ between the face normal and the light
direction (object space, per object); painter-sorted alone (its own bucket pass); not textured; no outlines.

## 11. Camera of this part (0x7554 / 0x74e8 at t = 0)

0x7554 copies the 5 current values at 0x5de8 (yaw, pitch, x, y, z; positions in 16.16) into key 0 of the key
table at 0x466f (23 keys × 5 dwords, 0x14 bytes per key) and builds per segment k and channel c a cubic
(coefficients at [0x482f] + k·0x50 + c·16: a, b, c1, d):
`d = K[k]; e = K[k+1]; c1 = T[k]; a = 2d − 2e + T[k] + T[k+1]; b = e − a − c1 − d`, with tangents
`T[0] = T[n] = 0, T[k] = K[k+1] − K[k−1]` (not halved), positions (c ≥ 2) taken >> 16 first.
0x74e8 evaluates at t = [0x482b] (8.8: segment t>>8, u = t&255):
`v = (d << 16) + a·((u³) >> 8) + b·u² + c1·(u << 8)`, angles >> 16. At t = 0 (this whole part) the camera is key
0 quantised: yaw 20, pitch −28, position ((key >> 16) << 16) >> 10 = (−704, 576, −1600). The path itself belongs
to the coloured part ("scene3d").

## 12. 0x13af — background blur precalc (invisible here)

```c
void blur_precalc(n) {                               // 0x13af, n = 1 in phase 1
    blur_left -= n;  if (blur_left < 0) return;      // [0x1edad]: 150 at init, 75 used by init (0xfb2)
    repeat n: blur2(img = [0x1ede1], rows 200, stride 320);   // 0xf47
}
// 0xf47: two passes of out[i] = (in[i-320] + in[i+320] + in[i-1] + in[i+1]) >> 2 over 64000 bytes:
//   img -> tmp ([8]+0x280, zero rows above/below) -> img (one zero padding row above/below; i±1 wraps rows)
```
[0x1ede1] is the glow image of the end logo ("psychic link", built in 0xfb2, used by 0x12de in the end part).
Phase 1 does one pass per frame (55 frames here: 75 → 20 left); the logo part 0x572f (0x57fc) calls it in its
wait loop and finishes the rest, so the end result is always 150 passes. Frame-rate independent in effect;
a remake can do all 150 passes at load time. It only slows phase 1 down (2.7 M instructions/frame).

## 13. Time bases

| variable | time base | rate |
|---|---|---|
| palette level [0x1edb5] | per tick (IRQ 0x1e45) | −400/tick, 0xff00 → 0x8000, ticks 1080–1161 |
| phase 1 → 2 | music counter ≥ 0x80 (tested per frame) | tick 1181 |
| x, z, a_cnt in phase 2 (0x61ee) | per tick | +1, −2, −1 per tick for 123 ticks |
| phase 2 → 3 | a_cnt == 0 (tested per frame) | tick 1304 |
| pen z in phase 3 | per tick | +10/tick until 0 |
| pen x,y, step, travel, stroke_idx | per tick (0x6267) | §9.2 |
| end of part | stroke_idx == 0x113 (tested per frame) | tick 3186 (counter 366/367) |
| blur precalc | per frame (phase 1 only) | invisible, see §12 |
| everything drawn | per frame, from the state above | — |

Nothing visible depends on the frame rate. Measured (1.5 M instr/tick): phase 1 56 frames/102 ticks (38 fps,
2.73 M instr/frame incl. the blur), phase 2 192/123 (109 fps, 0.96 M), phase 3 2571/1883 (96 fps, 0.98 M at the
start rising to 1.21 M at the end as strokes accumulate). A remake should run 0x61ee/0x6267 at 70.0018 Hz and may
interpolate the pen position/angles between ticks for display (the original shows per-tick steps).

## 14. Shortcuts a high-resolution remake should evaluate exactly instead

- Vertex positions are floored to 1/32 px (and to whole pixels for fills, lines, the pen and the stroke
  endpoints); shadow vertices to whole pixels. Project in float at the target resolution. The stroke sequencer
  works in 320x200 pixels (its step counts depend on integer lengths) — keep its timing in 320x200 units
  (n from the original integer endpoints) but place the pen/lines at the exact projected positions.
- Pencil line: 3 jittered 2-px-wide DDA passes with per-pixel random darkening — the "pencil look". At high res
  draw 3 strokes of width 2 original px with endpoint jitter scaled by the resolution factor, and per-pixel
  darkening (2..12 of 63 levels, distribution §3.5) — either per original pixel or per target pixel (finer grain);
  this is a look decision.
- Hidden lines: painter's algorithm with 1024 buckets of 64 units of summed depth, per object group; a z-buffer
  would differ only where the bucket sort is wrong. Keep the group order (later groups always on top).
- Shading: integer `floor(12.5·cos)` steps (25 levels) on 32-entry palette ramps; can be evaluated continuously.
- Paper: 320x200 embossed nibble noise; regenerate at the target resolution (same formula, neighbour at
  (+1,+1) pixel) or upscale the original 320x200 texture for the same grain size. Composite is a subtraction in
  grey-index space: `index = max(0, scene_index + 2·tex − 64)`.
- Shadow: binary mask, one darkness level (index −28..−30), hard edges; integer projection.
- Sine table quantised to 1/65536 and 1024 steps per turn (pencil angles change in steps of 2/1024 per tick).
- Palette fade uses the 8-bit level (>>8) and 6-bit DAC values.

## 15. Not determined / caveats

- The 0x80 bit set in face +0x20 for the stroke in progress (0x6133) is never tested (0x43df masks it out);
  presumed leftover.
- The camera used to number the stroke faces (static 0x5de8 values >> 10) differs from the one drawn with
  (quantised key 0); harmless since the resolved list is what matters, but a port that renumbers must use the
  numbering camera.
- Colour-wrap of pencil faces at the darkest shade (base 0xeb − 13 = 0xde) is possible in principle; not observed.
- Mode 0 / later draw order / horizon / sky gradient branches of 0x5eec and 0x603f's no-pencil branch are only
  described as far as read here (used by the coloured part).

## Appendix A — resolved stroke list

`k obj/face from>to (x0,y0)>(x1,y1) n t`: stroke k draws the edge from vertex `from` to vertex `to` of face `face` of
object `obj` (indices into the object's mesh as built by 0x3cb7, objects numbered as in 0x5db8, §6.1). Screen
coordinates = integer 320x200 projections with this part's camera (what 0x6267 uses), n = draw ticks, t = tick at
which the stroke starts (its IRQ). The pen-up move before each stroke takes 2·n(move length) ticks.

```
  0 10/109  96>94  (137, 51)>(127, 56) n3  t1334  |    1 10/107  94>92  (127, 56)>(124, 64) n2  t1337
  2 10/105  92>90  (124, 64)>(126, 72) n2  t1341  |    3 10/103  90>88  (126, 72)>(129, 77) n1  t1345
  4 10/102  88>89  (129, 77)>(139, 83) n3  t1348  |    5 10/102  89>91  (139, 83)>(145, 83) n1  t1353
  6 10/104  91>93  (145, 83)>(154, 81) n2  t1356  |    7 10/106  93>95  (154, 81)>(160, 74) n2  t1360
  8 10/108  95>97  (160, 74)>(160, 64) n2  t1364  |    9 10/110  97>99  (160, 64)>(159, 56) n2  t1368
 10 10/111  99>98  (159, 56)>(147, 49) n3  t1370  |   11 10/111  98>96  (147, 49)>(137, 51) n3  t1375
 12 10/6     7>9   (124, 58)>(114, 54) n3  t1386  |   13 10/8     9>11  (114, 54)>(102, 53) n3  t1389
 14 10/9    11>10  (102, 53)>( 91, 60) n3  t1392  |   15 10/9    10>8   ( 91, 60)>( 90, 70) n2  t1397
 16 10/7     8>6   ( 90, 70)>( 93, 79) n2  t1401  |   17 10/5     6>4   ( 93, 79)>(100, 86) n2  t1405
 18 10/3     4>2   (100, 86)>(110, 87) n2  t1409  |   19 10/1     2>0   (110, 87)>(116, 86) n1  t1413
 20 10/0     0>1   (116, 86)>(126, 79) n3  t1416  |   21 10/0     1>3   (126, 79)>(128, 74) n1  t1421
 22 10/2     3>5   (128, 74)>(129, 65) n2  t1424  |   23 10/4     5>7   (129, 65)>(124, 58) n2  t1428
 24 10/188 154>155 (123, 73)>(118, 76) n1  t1436  |   25 10/190 155>156 (118, 76)>(114, 81) n1  t1439
 26 10/224 156>172 (114, 81)>(113, 82) n0  t1442  |   27 10/224 172>173 (113, 82)>(111, 88) n1  t1444
 28 10/226 173>174 (111, 88)>(112, 94) n1  t1447  |   29 10/228 174>175 (112, 94)>(116,100) n2  t1450
 30 10/230 175>176 (116,100)>(122,103) n1  t1452  |   31 10/232 176>161 (122,103)>(129,105) n2  t1455
 32 10/202 161>162 (129,105)>(136,104) n2  t1459  |   33 10/204 162>163 (136,104)>(142,100) n2  t1461
 34 10/206 163>164 (142,100)>(146, 95) n1  t1463  |   35 10/208 164>165 (146, 95)>(147, 89) n1  t1466
 36 10/210 165>166 (147, 89)>(145, 83) n1  t1469  |   37 10/211 166>150 (145, 83)>(145, 81) n0  t1472
 38 10/213 150>167 (145, 81)>(142, 78) n1  t1474  |   39 10/213 167>151 (142, 78)>(141, 77) n0  t1477
 40 10/182 151>152 (141, 77)>(136, 74) n1  t1477  |   41 10/184 152>153 (136, 74)>(130, 72) n1  t1480
 42 10/186 153>154 (130, 72)>(123, 73) n2  t1483  |   43 10/94   79>81  (113, 85)>(103, 82) n3  t1493
 44 10/96   81>83  (103, 82)>( 93, 84) n3  t1496  |   45 10/98   83>85  ( 93, 84)>( 85, 92) n3  t1499
 46 10/100  85>87  ( 85, 92)>( 77,103) n3  t1502  |   47 10/101  87>86  ( 77,103)>( 83,117) n4  t1507
 48 10/101  86>84  ( 83,117)>( 95,119) n3  t1513  |   49 10/99   84>82  ( 95,119)>(108,119) n3  t1518
 50 10/97   82>80  (108,119)>(117,114) n3  t1523  |   51 10/95   80>78  (117,114)>(121,106) n2  t1528
 52 10/93   78>76  (121,106)>(122,100) n1  t1532  |   53 10/92   76>77  (122,100)>(117, 89) n3  t1535
 54 10/92   77>79  (117, 89)>(113, 85) n1  t1538  |   55 10/196 158>159 (113, 92)>(117, 97) n1  t1541
 56 10/229 159>158 (117, 97)>(113, 92) n1  t1544  |   57 10/198 159>160 (117, 97)>(122,101) n1  t1545
 58 10/231 160>159 (122,101)>(117, 97) n1  t1548  |   59 10/200 160>145 (122,101)>(129,102) n2  t1549
 60 10/233 145>160 (129,102)>(122,101) n2  t1553  |   61 10/84   67>69  (120,103)>(113,110) n2  t1557
 62 10/86   69>71  (113,110)>(113,119) n2  t1561  |   63 10/88   71>73  (113,119)>(119,130) n3  t1565
 64 10/90   73>75  (119,130)>(128,140) n3  t1570  |   65 10/91   75>74  (128,140)>(143,139) n4  t1575
 66 10/91   74>72  (143,139)>(150,127) n3  t1579  |   67 10/89   72>70  (150,127)>(154,116) n3  t1584
 68 10/87   70>68  (154,116)>(151,107) n2  t1587  |   69 10/85   68>66  (151,107)>(144,101) n2  t1591
 70 10/83   66>64  (144,101)>(138, 99) n1  t1595  |   71 10/82   64>65  (138, 99)>(126,100) n3  t1598
 72 10/82   65>67  (126,100)>(120,103) n1  t1603  |   73 10/203 145>146 (129,102)>(136,101) n2  t1604
 74 10/172 146>147 (136,101)>(141, 98) n1  t1606  |   75 10/207 147>148 (141, 98)>(145, 93) n1  t1609
 76 10/174 148>147 (145, 93)>(141, 98) n1  t1612  |   77 10/205 147>146 (141, 98)>(136,101) n1  t1615
 78 10/170 146>145 (136,101)>(129,102) n2  t1618  |   79 10/47   33>29  (133,111)>(141,126) n4  t1624
 80 10/39   29>25  (141,126)>(144,144) n4  t1630  |   81 10/31   25>21  (144,144)>(142,171) n6  t1636
 82 10/23   21>17  (142,171)>(137,193) n5  t1644  |   83 10/15   17>13  (137,193)>(135,202) n2  t1651
 84 10/14   13>14  (135,202)>(137,204) n0  t1655  |   85 10/16   14>15  (137,204)>(139,203) n0  t1657
 86 10/16   15>19  (139,203)>(141,194) n2  t1657  |   87 10/24   19>23  (141,194)>(146,171) n5  t1661
 88 10/32   23>27  (146,171)>(148,144) n6  t1668  |   89 10/40   27>31  (148,144)>(144,124) n5  t1676
 90 10/48   31>35  (144,124)>(136,110) n4  t1681  |   91 10/56   35>39  (136,110)>(132, 91) n5  t1685
 92 10/57   39>38  (132, 91)>(131, 91) n0  t1690  |   93 10/57   38>34  (131, 91)>(134,110) n5  t1690
 94 10/54   34>38  (134,110)>(131, 91) n5  t1697  |   95 10/55   38>37  (131, 91)>(128, 91) n0  t1702
 96 10/55   37>33  (128, 91)>(133,111) n5  t1704  |   97 10/49   34>30  (134,110)>(142,125) n4  t1711
 98 10/46   30>34  (142,125)>(134,110) n4  t1717  |   99 10/112 103>101 (142,102)>(140, 96) n1  t1727
100 10/112 101>100 (140, 96)>(142, 85) n3  t1730  |  101 10/114 103>105 (142,102)>(148,109) n2  t1739
102 10/116 105>107 (148,109)>(157,112) n2  t1743  |  103 10/118 107>109 (157,112)>(167,110) n3  t1747
104 10/120 109>111 (167,110)>(177,107) n3  t1750  |  105 10/121 111>110 (177,107)>(178, 94) n3  t1755
106 10/121 110>108 (178, 94)>(171, 85) n3  t1760  |  107 10/119 108>106 (171, 85)>(163, 78) n3  t1763
108 10/117 106>104 (163, 78)>(154, 77) n2  t1766  |  109 10/115 104>102 (154, 77)>(146, 81) n2  t1768
110 10/113 102>100 (146, 81)>(142, 85) n1  t1772  |  111 10/41   30>26  (142,125)>(146,144) n5  t1789
112 10/38   26>30  (146,144)>(142,125) n5  t1796  |  113 10/33   26>22  (146,144)>(144,171) n6  t1809
114 10/30   22>26  (144,171)>(146,144) n6  t1817  |  115  2/1     1>2   (150,151)>(328,154) n20 t1825
116  2/1     2>6   (328,154)>(312, 24) n17 t1849  |  117  2/0     6>5   (312, 24)>(150, 42) n19 t1870
118  2/13    5>12  (150, 42)>(208, 36) n10 t1893  |  119  2/20   12>13  (208, 36)>(293, 26) n13 t1905
120  2/11   13>11  (293, 26)>(243, 15) n9  t1922  |  121  2/12   11>9   (243, 15)>(190, 22) n10 t1933
122  2/13    9>5   (190, 22)>(150, 42) n9  t1945  |  123  2/0     5>1   (150, 42)>(150,151) n15 t1956
124 10/65   44>42  (164,165)>(151,180) n5  t1983  |  125 10/63   42>40  (151,180)>(144,189) n3  t1990
126 10/62   40>41  (144,189)>(139,202) n3  t1995  |  127 10/62   41>43  (139,202)>(141,206) n1  t2000
128 10/64   43>45  (141,206)>(148,205) n2  t2003  |  129 10/66   45>47  (148,205)>(163,194) n4  t2005
130 10/68   47>49  (163,194)>(183,176) n6  t2011  |  131 10/70   49>51  (183,176)>(205,155) n7  t2019
132 10/71   51>50  (205,155)>(210,140) n4  t2028  |  133 10/71   50>48  (210,140)>(195,144) n4  t2032
134 10/69   48>46  (195,144)>(180,152) n4  t2036  |  135 10/67   46>44  (180,152)>(164,165) n5  t2040
136  1/13   17>18  (168,158)>(173,153) n2  t2047  |  137  1/15   18>19  (173,153)>(191,151) n4  t2049
138  9/1     3>2   (188,152)>(222,152) n7  t2053  |  139  9/0     2>1   (222,152)>(220,102) n9  t2062
140  9/0     1>0   (220,102)>(187,104) n7  t2073  |  141  9/1     0>3   (187,104)>(188,152) n9  t2082
142  1/1     0>1   (200,150)>(224,150) n5  t2097  |  143  1/1     1>2   (224,150)>(216,151) n2  t2104
144  1/16    2>3   (216,151)>(204,153) n3  t2108  |  145  1/14    3>4   (204,153)>(213,158) n3  t2113
146  1/11    4>5   (213,158)>(268,162) n10 t2118  |  147  1/9     5>6   (268,162)>(356,166) n13 t2130
148  5/77   51>50  (332,131)>(315,132) n4  t2159  |  149  5/77   50>38  (315,132)>(317,133) n0  t2165
150  5/51   38>37  (317,133)>(318,134) n0  t2165  |  151  5/49   37>36  (318,134)>(332,134) n3  t2165
152  5/100  51>50  (332,131)>(315,132) n4  t2170  |  153  1/10   15>16  (289,168)>(209,163) n12 t2192
154  1/12   16>17  (209,163)>(168,158) n8  t2206  |  155 10/25   22>18  (144,171)>(139,194) n5  t2228
156 10/22   18>22  (139,194)>(144,171) n5  t2235  |  157 10/73   52>54  (138,178)>(132,168) n3  t2242
158 10/75   54>56  (132,168)>(120,153) n5  t2245  |  159 10/77   56>58  (120,153)>(107,140) n4  t2250
160 10/79   58>60  (107,140)>( 95,133) n3  t2256  |  161 10/81   60>62  ( 95,133)>( 88,130) n2  t2261
162  7/91   57>58  ( 84,129)>( 81,131) n0  t2267  |  163  7/90   58>46  ( 81,131)>( 64,132) n4  t2269
164  7/69   46>47  ( 64,132)>( 50,132) n3  t2275  |  165  7/68   47>35  ( 50,132)>( 30,133) n5  t2278
166  7/68   35>34  ( 30,133)>( 35,133) n1  t2285  |  167  7/42   34>22  ( 35,133)>( 18,134) n4  t2288
168  7/42   22>21  ( 18,134)>( 16,134) n0  t2294  |  169  7/19   21>22  ( 16,134)>( 18,134) n0  t2296
170  7/18   22>10  ( 18,134)>( 22,151) n4  t2296  |  171  7/18   10>9   ( 22,151)>( 20,152) n0  t2302
172  7/16    9>8   ( 20,152)>( 12,152) n2  t2304  |  173  7/14    8>7   ( 12,152)>( -1,153) n3  t2306
174  7/38   19>20  (  1,134)>( 10,134) n2  t2317  |  175  7/40   20>21  ( 10,134)>( 16,134) n1  t2321
176  7/17   21>20  ( 16,134)>( 10,134) n1  t2324  |  177  7/15   20>19  ( 10,134)>(  1,134) n2  t2327
178  7/108  55>54  (  8,125)>(-66,124) n12 t2333  |  179  7/85   54>55  (-66,124)>(  8,125) n12 t2347
180  7/87   55>56  (  8,125)>( 61,128) n10 t2363  |  181  7/112  56>57  ( 61,128)>( 84,129) n5  t2375
182  7/112  57>69  ( 84,129)>( 19, 88) n12 t2382  |  183  7/138  69>70  ( 19, 88)>( 20, 89) n0  t2396
184  7/140  70>71  ( 20, 89)>( 18, 90) n0  t2396  |  185  7/142  71>60  ( 18, 90)>( 13, 90) n1  t2396
186  7/136  68>69  ( 15, 87)>( 19, 88) n1  t2399  |  187  7/113  69>68  ( 19, 88)>( 15, 87) n1  t2402
188  7/134  68>67  ( 15, 87)>(  8, 86) n2  t2405  |  189  7/132  67>66  (  8, 86)>(  0, 85) n2  t2407
190  7/109  66>67  (  0, 85)>(  8, 86) n2  t2409  |  191  7/111  67>68  (  8, 86)>( 15, 87) n2  t2413
192  7/120  61>60  (  6, 90)>( 13, 90) n2  t2417  |  193  7/122  61>62  (  6, 90)>( -1, 89) n2  t2419
194  7/145  85>84  (  4, 94)>( 32, 96) n6  t2423  |  195  7/167  84>95  ( 32, 96)>( 55, 95) n5  t2431
196  7/165  95>94  ( 55, 95)>( 67, 92) n3  t2438  |  197  7/163  94>93  ( 67, 92)>( 67, 87) n1  t2443
198  7/184  93>105 ( 67, 87)>( 36, 48) n9  t2446  |  199  7/210 105>106 ( 36, 48)>( 37, 51) n0  t2457
200  7/212 106>107 ( 37, 51)>( 34, 53) n0  t2457  |  201  7/214 107>96  ( 34, 53)>( 26, 53) n2  t2459
202  7/192  96>97  ( 26, 53)>( 16, 52) n2  t2461  |  203  7/194  97>98  ( 16, 52)>(  6, 50) n3  t2465
204  7/196  98>99  (  6, 50)>( -1, 46) n2  t2468  |  205  7/202 101>102 ( -1, 40)>(  8, 39) n2  t2474
206  7/204 102>103 (  8, 39)>( 20, 41) n3  t2478  |  207  7/206 103>104 ( 20, 41)>( 30, 44) n3  t2483
208  7/208 104>105 ( 30, 44)>( 36, 48) n2  t2488  |  209  7/185 105>104 ( 36, 48)>( 30, 44) n2  t2490
210  7/183 104>103 ( 30, 44)>( 20, 41) n3  t2492  |  211  7/181 103>102 ( 20, 41)>(  8, 39) n3  t2495
212  7/179 102>101 (  8, 39)>( -1, 40) n2  t2498  |  213  7/195 110>109 (-14, 54)>( 10, 60) n6  t2510
214  7/193 109>108 ( 10, 60)>( 33, 62) n5  t2518  |  215  7/215 108>119 ( 33, 62)>( 51, 61) n4  t2525
216  7/213 119>118 ( 51, 61)>( 61, 57) n3  t2531  |  217  7/211 118>117 ( 61, 57)>( 61, 50) n2  t2536
218  7/224 117>120 ( 61, 50)>( 21, -1) n11 t2538  |  219  7/221 120>113 ( 21, -1)>(-40, 27) n11 t2551
220  7/222 114>115 (-14, 25)>( 19, 31) n7  t2572  |  221  7/223 115>116 ( 19, 31)>( 46, 40) n6  t2581
222  7/224 116>117 ( 46, 40)>( 61, 50) n4  t2589  |  223  7/209 117>116 ( 61, 50)>( 46, 40) n4  t2595
224  7/207 116>115 ( 46, 40)>( 19, 31) n6  t2599  |  225  7/205 115>114 ( 19, 31)>(-14, 25) n7  t2607
226  7/180  90>91  (-34, 67)>( 13, 72) n9  t2632  |  227  7/159  91>79  ( 13, 72)>( 19, 75) n1  t2643
228  7/135  79>80  ( 19, 75)>( 43, 80) n6  t2646  |  229  7/158  80>92  ( 43, 80)>( 49, 79) n1  t2654
230  7/184  92>93  ( 49, 79)>( 67, 87) n5  t2657  |  231  7/161  93>92  ( 67, 87)>( 49, 79) n5  t2664
232  7/161  92>80  ( 49, 79)>( 43, 80) n1  t2669  |  233  7/158  80>79  ( 43, 80)>( 19, 75) n6  t2672
234  7/156  79>91  ( 19, 75)>( 13, 72) n1  t2678  |  235  7/159  91>92  ( 13, 72)>( 49, 79) n7  t2681
236  7/182  92>91  ( 49, 79)>( 13, 72) n7  t2690  |  237  7/157  91>90  ( 13, 72)>(-34, 67) n9  t2699
238  7/147  86>85  (-27, 90)>(  4, 94) n7  t2718  |  239  7/110  55>56  (  8,125)>( 61,128) n10 t2739
240  7/89   56>57  ( 61,128)>( 84,129) n5  t2751  |  241 10/81   63>62  ( 90,143)>( 88,130) n3  t2764
242 10/80   63>61  ( 90,143)>(101,161) n5  t2771  |  243 10/78   59>61  (115,180)>(101,161) n5  t2788
244 10/76   59>57  (115,180)>(129,191) n4  t2803  |  245 10/74   57>55  (129,191)>(138,193) n2  t2809
246 10/72   55>53  (138,193)>(141,191) n0  t2813  |  247 10/72   53>52  (141,191)>(138,178) n3  t2815
248 10/17   18>14  (139,194)>(137,204) n3  t2824  |  249 10/14   14>18  (137,204)>(139,194) n3  t2827
250  9/3     7>6   (236, 86)>(253, 85) n4  t2866  |  251  9/2     6>5   (253, 85)>(252, 69) n4  t2872
252  9/2     5>4   (252, 69)>(235, 70) n4  t2876  |  253  9/3     4>7   (235, 70)>(236, 86) n4  t2882
254  9/9    19>18  (234, 62)>(251, 61) n4  t2896  |  255  9/8    18>17  (251, 61)>(250, 46) n4  t2902
256  9/8    17>16  (250, 46)>(233, 47) n4  t2906  |  257  9/9    16>19  (233, 47)>(234, 62) n4  t2912
258  9/7    15>14  (260, 60)>(278, 59) n4  t2928  |  259  9/6    14>13  (278, 59)>(277, 43) n4  t2934
260  9/6    13>12  (277, 43)>(259, 45) n4  t2938  |  261  9/7    12>15  (259, 45)>(260, 60) n4  t2944
262  9/5     8>11  (261, 68)>(262, 84) n4  t2954  |  263  9/5    11>10  (262, 84)>(281, 83) n5  t2960
264  9/4    10>9   (281, 83)>(279, 67) n4  t2965  |  265  9/4     9>8   (279, 67)>(261, 68) n4  t2969
266  2/20   13>16  (293, 26)>(246, -5) n10 t2991  |  267  2/20   16>12  (246, -5)>(208, 36) n10 t3003
268  2/12   11>12  (243, 15)>(208, 36) n8  t3027  |  269  2/11   11>10  (243, 15)>(257, 13) n4  t3049
270  2/10   10>6   (257, 13)>(312, 24) n10 t3053  |  271  2/10    6>13  (312, 24)>(293, 26) n5  t3065
272  5/101  62>50  (368,101)>(315,132) n11 t3100  |  273  1/8    14>15  (410,181)>(289,168) n16 t3141
274  1/17    0>19  (200,150)>(191,151) n2  t3185
```

## Appendix B — palette at code32 0x169a3 (6-bit r g b per entry, hex)

```
00: 000000 000000 010101 020202 030303 040404 050505 060606 070707 080808 090909 0a0a0a 0b0b0b 0c0c0c 0d0d0d 0e0e0e
10: 0f0f0f 101010 111111 121212 131313 141414 151515 161616 171717 181818 191919 1a1a1a 1b1b1b 1c1c1c 1d1d1d 1e1e1e
20: 1f1f1f 202020 212121 222222 232323 242424 252525 262626 272727 282828 292929 2a2a2a 2b2b2b 2c2c2c 2d2d2d 2e2e2e
30: 2f2f2f 303030 313131 323232 333333 353535 363636 373737 383838 393939 3a3a3a 3b3b3b 3c3c3c 3d3d3d 3e3e3e 3f3f3f
40: 3f3f3f 393939 333333 2d2d2d 272727 222222 1c1c1c 161616 101010 0a0a0a 050505 0c0a0a 140b0b 1c0a0a 240606 2c0000
50: 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000
60: 000000 000301 010703 010c06 021008 03140a 04180c 051d0f 062111 062513 072915 082e18 09321a 0a361c 0b3a1e 0c3f21
70: 0f3f22 123f24 153f26 183f28 1b3f2a 1f3f2c 223f2e 253f30 283f31 2b3f33 2f3f35 323f37 353f39 383f3b 3b3f3d 3f3f3f
80: 000000 000203 010507 01080c 020b10 030d14 031018 04131d 051621 061925 071c29 071f2e 082232 092536 0a283a 0b2b3f
90: 0e2c3f 112d3f 142e3f 18303f 1b313f 1e323f 21333f 25353f 28363f 2b373f 2e383f 313a3f 353b3f 383c3f 3b3d3f 3f3f3f
a0: 000000 030300 070700 0c0c00 101001 141401 181801 1d1d01 212102 252502 292903 2e2e03 323203 363604 3a3a04 3f3f05
b0: 3f3f08 3f3f0c 3f3f0f 3f3f13 3f3f17 3f3f1a 3f3f1e 3f3f22 3f3f25 3f3f29 3f3f2c 3f3f30 3f3f34 3f3f37 3f3f3b 3f3f3f
c0: 000000 020101 060301 090503 0c0704 100905 130c07 170e08 1a1009 1d120b 21140c 24160d 27180f 2b1a10 2e1c11 321f13
d0: 322015 332217 34241a 35261c 36281f 372a21 372b24 382d27 392f29 3a322c 3b342f 3b3632 3c3835 3d3a38 3e3c3b 3f3f3f
e0: 000000 030000 070101 0b0101 0f0202 130202 170303 1b0404 1f0505 230505 270606 2b0707 2f0808 330808 370909 3b0a0a
f0: 3f0b0b 3f0e0e 3f1212 3f1515 3f1818 3f1c1c 3f1f1f 3f2323 3f2626 3f2a2a 3f2d2d 3f3131 3f3434 3f3737 3f3b3b 3f3f3f
```
