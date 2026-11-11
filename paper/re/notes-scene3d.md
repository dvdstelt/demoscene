# Scope "scene3d": coloured 3D scene, banner with "Statix + Vic (acme)", page peel — ticks 3186–5611, music counter 366..655

All addresses are code32 offsets. "Tick" = 70.0018 Hz IRQ0 tick, "counter" = music counter [0x1ea8b]. Coordinates
are in the original 320x200 frame unless stated. ">>" on signed values is arithmetic (floor). "16.16" = int32 with
16 fraction bits. sinT[i] = sine table 0x22479 (1024 entries, 65536·sin(2πi/1024)), cosT(i) = sinT[(i+256)&1023];
angles are in 1/1024 turn. recip[n] = 65536/n (truncated) at 0x1ee15 + 4n.

The engine itself (object/vertex/face records, rotation 0xb1f, 0x3fb3 transform/cull/shade/bucket, 0x43df bucket
renderer, triangle fill, pencil line 0x18b7/0x1612, paper composite 0x1943, pencil shadow 0x42e4, the tables
T / paper grain / jitter / grey) is described in detail in **notes-sketch.md §3–§8**. My reading agrees with it
everywhere; below it is restated compactly with what is specific to this scope (mode 0: sky, ground, horizon,
colours, outlines on every silhouette/crease, second draw order, moving camera, banner). The page "present the
Paper intro" built by 0x5623 is detailed in **notes-logo.md §3**.

**Verification.** I wrote an integer-exact Python model of everything below and compared it against memory
captured from the emulator (run_paper.py copy with hooks, runs resumed from the snapshots):
- a full 3D-scene frame (tick 4500: 0x5eec in mode 0 + paper composite) — 0 differing pixels;
- the handwriting pass 0x73a6 on that frame — 0 differing pixels (all 199 line calls identical);
- banner vertices (0x3c3e) and normals (0x3dc0) — identical; camera spline coefficients (0x7554) and evaluation
  (0x74e8) — identical;
- a transition frame (tick 3300: mode-0 render + mode-2 render + noise composite + pencil + pencil shadow + paper
  composite) — 0 differing pixels;
- the page peel 0x2b6c at p = 68, 72, 196, 200, 410, 417 — 0 differing pixels each.
Model and scripts: paper/re/models/scene3d/ (model.py, cmp1.py … cmp5.py, run_cap.py = run_paper.py + `--cap A:B`
memory capture + `--regtrace`). See §12.

---------------------------------------------------------------------------------------------------------------

## 1. What is on screen, timeline

| ticks | counter | main (0x7827..0x797b) | callback | on screen |
|---|---|---|---|---|
| 3186 | 366 | stroke count [0x1ea95]=0x113; 0x5eec mode 0 → copy to [0x5067]; 0x5eec mode 2 → copy to [0x5063]; acc [0x466b] += 240, p [0x482b] = acc>>7; 0x7207 (transition noise) | 0x6267 → 0x6199 | (VGA not updated) |
| 3187–3441 | 367..397 | loop: 0x7a64, 0x726d (noise table), 0x72c4 (two renders + composite + pencil tail of 0x603f), copy to VGA; until threshold byte [0x7324] == 0 | 0x6199 | the pencil sketch turns into the coloured scene through growing, moving colour blobs; the red pencil rotates and flies off towards the viewer; camera starts moving |
| 3442–5479 | 397..640 | [0x1ea8f]=0x280; mode [0x1ea94]=0; pencil x [0x55d6]=2000 (pencil off); 0x7207 (scene noise); loop: 0x726d, 0x3c3e (banner), 0x7a64, 0x603f (scene + paper), 0x73a6 (handwriting), copy; until counter >= 0x280 | 0x620b | camera flight over the flower, house, path, trees; banner between two red poles; from tick 4211 the drawing "Statix + Vic (acme)" with two stick figures is drawn on it, one point per tick |
| 5480–5482 | 640 | 0x5623 builds the "present the / Paper / intro" page into [0x1edc5] (≈3 ticks, screen frozen); 0x2b55(old = back buffer, new = page) | 0x6170 | frozen last frame |
| 5483–5610 | 640..655 | loop: 0x726d, 0x3c3e, 0x7a64, 0x603f, 0x73a6, [0x1ee11]++, 0x2b6c (peel straight to VGA, no back-buffer copy); until peel p [0x1eaa9] >= 700 | 0x6170 | the live 3D scene is flipped up off the page from its bottom edge (grey page back), revealing the logo page |

Objects seen: sun (yellow, far), path (brown, flat), house (grey walls, red roof) with door and 4 windows, three
identical fir trees, a big flower (red petals, yellow centre, green leaves/stem), two red poles with the banner
(grey cloth, 17x11 grid, fluttering), sky gradient, flat green ground, pencil horizon line, black sketchy outlines
on all silhouettes/creases, everything with the paper grain.

## 2. Per-tick callbacks (the only places where anything advances)

```c
// shared state: acc [0x466b] (static 0), p [0x482b] = path parameter 8.8 (segment = p>>8, u = p&255)
void cb_6199(void) {                       // ticks 3187..3441 (255 calls)
    if (pencil_z /*[0x55de]*/ > -1420) pencil_z -= 7;      // 0 -> -1421 in 203 ticks
    noise_advance(0);                      // 0x7244 cl=0: W[k].phase += W[k].speed; if (thr) thr--;  (thr = byte [0x7324])
    camera_eval();                         // 0x74e8 with the current p (before the update below)
    acc += 240;  p = acc >> 7;             // 1.875 per tick
    if (++pencil_a /*[0x55d2]*/ > 123) pencil_a = 123;     // 0 -> 123 in 123 ticks
}
void cb_620b(void) {                       // ticks 3442..5479 (2038 calls)
    noise_advance(2);                      // W[k].phase += W[k].speed << 2
    if ((uint32)p > 0x780) hw_end += 8;    // [0x4837]: reveal one more handwriting point (first at tick 4211)
    if ((p >> 8) < 23) camera_eval();      // always true here
    acc += 240;  p = acc >> 7;
    rot(yaw, pitch) -> 0x217d9;            // 9 dwords, never read (dead; 0x217d9 is reused as a tick counter by the plane part)
}
void cb_6170(void) {                       // ticks 5480..5610
    peel_tick();                           // 0x2b20: p_peel += 4; if (p_peel > 256) p_peel += 3; if (p_peel > 700) p_peel = 700;
    noise_advance(2);  camera_eval();  acc += 240;  p = acc >> 7;
}
```
So at the callback of tick N (N >= 3187), acc before the add = 240·(N − 3186) and the camera of that tick is
evaluated at p = (240·(N − 3186)) >> 7 (1.875/256 segment per tick = 0.513 segments/s). p = 480 at 3442, 1921 at
4211, 4301 at 5480, ≈4546 at 5611 (segment 17). Before 3186 (sketch part) acc = 0, so the sketch is drawn from
key 0.

## 3. Static data and buffers used here

| what | where | notes |
|---|---|---|
| palette | 0x169a3, 768 bytes 6-bit | constant over the whole scope (checked all ticks 3180–5614 in full.frames); level 0x8000 = unchanged. 0x00–0x3f grey ramp, 0x60 green, 0x80 blue, 0xa0 yellow, 0xc0 brown, 0xe0 red: 32-entry ramps black (0) → full colour (~15) → white (31). Dump in notes-sketch.md appendix B |
| T (paper/darkening table) | [0x1eded], 256x256 | `T[ch][cl]`, ch = paper/stroke value; formula in §4.6 |
| paper grain | [0x1edf5], 320x200 | values 28..31 (0x55e2), see notes-sketch §3.3 |
| pencil grey / jitter | [0x1ee09], [0x1ee0d], 1024 bytes each | notes-sketch §3.5 |
| back buffer | [0x1edf9] | scene renders here; copied to VGA by main (not during the peel) |
| colour / sketch buffers | [0x5067], [0x5063] | transition: mode-0 and mode-2 renders |
| shadow paper | [8] (PMODE free-heap pointer, used as scratch) | paper grain + pencil shadow (transition only) |
| scene object table | 0x5db8: 12 object pointers | 0..10 from 0x3cb7 (mesh table 0x174a3), entry 8 overwritten by the banner (0x3a8c) at init, entry 11 [0x5de4] = pencil (0x39a4) |
| light position | 0x1e48c: (−1300, 100, −2500) | static, never written |
| camera keys | 0x466f: 22 keys x 5 int32 | §5 |
| camera now | 0x5de8: yaw, pitch (ints), x, y, z (key units = world·1024) | written by 0x74e8 |
| noise records | 0x71b7: 5 x {phase, speed, freq, amp} | §6 |
| noise table | 0x69b7: 0x800 bytes | rebuilt every frame (0x726d) |
| handwriting source | 0x18a2f..0x18ed7, 106 records | §8 |
| handwriting points | [0x483b] .. (0x3000 bytes, built at init by 0x5979) | 674 entries + terminator |

Objects (all with identity rotation and position 0 — 0x3cb7 adds the mesh offset to every vertex, so vertices are
world coordinates; world y points DOWN, +z away from the start camera; ground is at y ≈ 807–838):

| # | mesh | nv/nf | base colours | object | world bbox x / y / z |
|---|---|---|---|---|---|
| 0 | 0x17de3 | 20/36 | 0xac | sun | 23265..24485 / −11375..−10284 / 11067..12142 |
| 1 | 0x186cb | 20/18 | 0xd1 | path (flat, y = 814) | −241..1577 / 814 / −1940..2924 |
| 2 | 0x18551 | 17/24 | 0x2c walls, 0xec roof | house | −325..3240 / −2361..838 / 1947..4628 |
| 3, 4 | 0x18617 | 14/24 | 0xec | left / right banner pole | x 1869..1956 / 4072..4159, y −321..875, z ≈ 0 |
| 5, 6, 7 | 0x17eeb | 121/228 | 0x6c leaves, 0xcc trunk | fir trees | (3874..6642, 1697..4465), (3496..6264, −4104..−1336), (−4336..−1568, 1267..4035); y −1993..806 |
| 8 | (0x3a8c) | 187/320 | 0x30, add 0 | banner cloth (the third pole mesh at x ≈ 3013 that 0x3cb7 built for slot 8 is never drawn) | ≈1973..4056 / −291..759 / −416..48 (varies) |
| 9 | 0x17527 | 23/10 | 0x0c door, 0x8c windows | door + 4 windows (decals on the house front) | 416..2047 / −1241..838 / 2172..2831 |
| 10 | 0x175dd | 177/248 | 0x6c, 0xac, 0xec | flower | −901..−424 / 212..807 / −822..−626 |
| pencil | 0x1e353 | 25/46 | 0x10, 0xb0, 0xeb | red pencil (transition only) | own camera, §4.7 |

Mesh format (0x3cb7 / 0x39a4) and the record layouts: notes-sketch §6.1. Face base colour = byte
table 0x3db8[attr>>4] − 4 = {ec, 6c, ac, cc, 2c, d1, 0c, 8c}[attr>>4]; crease flags (attr & 7) << 3; colour add
for mode 2 (+0x1c) = 0x20 for 0x3cb7 objects, 0 for the banner and the pencil. Normals (length 256) and
neighbour faces / angle creases (n·n' <= 10000, >= ~81°) are computed once at init (0x3eeb); the banner's
normals every frame (0x3dc0); its creases only from 0x3a8c's border flags (no angle creases, verified).

## 4. Frame render 0x603f (as used here)

### 4.1 Top level
```c
void render_frame(void) {                  // 0x603f
    saved = mode;                          // byte [0x1ea94]: 0 = colour, 2 = sketch, 1 = no edges
    scene_render();                        // 0x5eec -> back buffer (raw palette indices, no paper yet)
    if (pencil_x /*[0x55d6]*/ >= 1000)     // 620b and peel loops: [0x55d6] = 2000
        composite(back, paper);            // 0x1943: back[i] = T[paper[i]][back[i]]
    else
        pencil_pass();                     // transition only, §4.7 (also entered from 0x72c4 via jmp 0x604a)
    mode = saved;
}
```

### 4.2 Scene render 0x5eec
```c
void scene_render(void) {
    memset(back, 0x3a, 64000);                              // 0x3a = paper grey (58)
    CAMPOS = (cam.x >> 10, cam.y >> 10, cam.z >> 10);       // [0x5df0..0x5df8] -> [0x1ea9d..0x1eaa5]
    CAM = rot(yaw /*[0x5de8]*/, pitch /*[0x5dec]*/);        // 0xb1f -> 0x1e468
    c = cosT(pitch); if (c == 0) c = 1;
    h = 100 - (sinT[pitch & 1023] * 224) / c;               // idiv truncates; horizon row
    if (mode == 0) {
        vgradient(h - 350, h, 0x80, 0x9f);                  // sky, 0x1bae
        vgradient(h, h + 1400, 0x6d, 0x6d);                 // ground
        pencil_line(0, h, 320, h);                          // 0x18b7 horizon line
    }
    mark_sketch_flags();                                    // 0x6133 (only matters in mode 2; [0x1ea95] = 0x113 = all 275 strokes)
    if ((uint32)p < 0x600) groups = {0,1} {2,3,4,5,6,7,8} {9,10};          // until tick ≈ 4004
    else                   groups = {0,1} {5,6,7} {2} {9,10} {3,4} {8};    // banner part
    for each group: { for each obj: transform_and_bucket(obj); draw_buckets(); }   // 0x5dfc: 0x3fb3 ..., 0x43df
}
void vgradient(r0, r1, c0, c1) {                            // 0x1bae, whole rows
    if (r1 <= 0 || r0 >= 200) return;
    step = (c1 - c0) * recip[r1 - r0];  c = c0 << 16;       // recip of the UNclipped height
    if (r1 > 200) r1 = 200;
    if (r0 < 0) { c -= r0 * step; r0 = 0; }
    for (y = r0; y < r1; y++, c += step) memset(back + 320*y, (c >> 16) & 0xff, 320);
}
```
Sky: index 0x80 (black end of the blue ramp) 350 rows above the horizon to 0x9e at the horizon row (31·187/65536
= 0.0885 index per row); only the part below row 0 is seen, i.e. light blue at the top fading to near-white at the
horizon. Ground: constant 0x6d (10,54,28). The group order is what makes the door/windows (coplanar decals) and the
flower always cover the house, and the banner always cover everything in the banner part.

`rot(b = yaw, a = pitch)` (0xb1f, row-major 16.16):
`{cb, 0, −sb;  (−sb·sa)>>16, ca, −((cb·sa)>>16);  (sb·ca)>>16, sa, (cb·ca)>>16}`, camera space = CAM·(world − CAMPOS),
x right, y down, z forward. View direction = third row = (sb·ca, sa, cb·ca): positive pitch looks down.

### 4.3 Transform, cull, shade, sort (0x3fb3) — per object
```c
M = CAM*R (0xae2, each product >>16, 64-bit);  T = CAM*(pos - CAMPOS) (32-bit, 16.16);
l = LIGHT - pos;  lo[c] = (Σk R[k][c]*l[k]) >> 16;  lo = rint(lo * 200 / |lo|);   // FPU, round to nearest
//   all scene objects have pos = 0, R = I  ->  lo = (-92, 7, -177) for every scene face: a directional light
for each vertex v (int x,y,z):
    cz = M[2]·v + T.z;                                   // 16.16
    if (cz > 25.0) { e = 0xe00000 / (cz >> 16);          // 224·65536 / z_int (integer division)
        cx = M[0]·v + T.x;  sx = (int32)(((int64)cx * e) >> 27) + 160*32;   // screen x in 1/32 px
        cy = M[1]·v + T.y;  sy = (int32)(((int64)cy * e) >> 27) + 100*32; }
for each face f (v0,v1,v2):
    hidden = any cz <= 25.0  ||  !((sy2-sy0)*(sx1-sx0) > (sx2-sx0)*(sy1-sy0));    // no clipping: whole face dropped
    if (!hidden) { s = (n·lo) >> 12;                     // |n| = 256, |lo| = 200: s = floor(12.5 cos θ), -13..12
                   col = (base + s) & 0xff;  band = col >> 5;
                   if (mode == 2) col = (col + add) & 0xff;                     // add = 0x20 for scene objects
                   f.col = ((col >> 5) != band) ? 0x3a : col; }               // mode 2: scene faces -> paper 0x3a
for each visible f: f.flags = (f.flags & 0xf8) | hidden(nb0) | hidden(nb1)<<1 | hidden(nb2)<<2;
                    key = clamp(((cz0>>16) + (cz1>>16) + (cz2>>16) - 1200) >> 6, 0, 1023);
                    push_front(bucket[key], f);
```
Projection: screen = (160 + 224·X/Z, 100 + 224·Y/Z) with the same focal length in x and y in 320x200 pixel units
(so on a 4:3 monitor the picture is stretched 1.2x vertically — a remake at 4:3 must scale x by W/320 and y by
H/200 to look the same). Horizontal FOV 71.1°, vertical 48.2°.

### 4.4 Draw buckets 0x43df
```c
for (key = 1023; key >= 0; key--) for (f in bucket[key], most recently inserted first) {
    p_i = (v_i.sx >> 5, v_i.sy >> 5);                    // whole pixels
    fill_triangle(back, p0, p1, p2, f.col);              // 0x1a87/0x1982 + 0x1b10
    if (mode == 1) continue;
    m = f.flags;  if (mode == 2) { m &= f.sketch;  if (f == cur_face) pencil_line(pen, stroke_start); }
    if (m & 0x09) pencil_line(p0, p1);                   // silhouette (neighbour hidden) or crease
    if (m & 0x12) pencil_line(p1, p2);
    if (m & 0x24) pencil_line(p2, p0);
}
```
Fill (notes-sketch §6.5): edges from integer vertices; rows y0..y1−1, x(y) = (x0<<16) + (y−y0)·recip[dy]·dx,
clamped to [0, 320<<16]; downward edges → right slot, upward → left slot; span x = left>>16 .. right>>16 − 1.
In mode 0 every visible face is outlined on each edge that is a crease or whose neighbour face is hidden (back
facing or behind the camera) — so contours and folds get black pencil lines, open mesh borders only if flagged.

### 4.5 Pencil line 0x18b7 (outlines, horizon, handwriting)
3 passes of a 2-pixel-wide DDA (0x1612) from (x0,y0) to (x1,y1); pass k adds jitter
`(jit[h0+2k+1], jit[h0+2k])` to the start and `(jit[h1+2k+1], jit[h1+2k])` to the end (x, y), h = (x·y + y) & 0x1ff
of that endpoint; every drawn pixel becomes `T[g][pixel]` with g = grey[((x0+y0+x1+y1) & 0xff) + 2·step + 0/1]
(0x1a..0x1f: darken 2..12 grey levels / 1..6 shades in a colour ramp). Full DDA details: notes-sketch §7 (my model
implements exactly that and is pixel-exact). **Specific to this scope:** the camera moves, so the integer endpoints
change and with them the jitter hash and the grey offset — the outlines "boil" (re-randomise) whenever an endpoint
crosses a pixel. That is part of the look at 320x200.

### 4.6 Paper composite 0x1943 and the colour arithmetic
```c
// T[ch][cl] (0x144b): c = ch <= 32 ? ch : ((ch-32)>>1) + ((ch-32)>>3);
//   cl <  0x40 (grey ramp):   T = max(0, cl + 2c - 64)
//   cl >= 0x40 (32-ramps):    T = (cl & 0xe0) + max(0, (cl & 0x1f) + c - 32)
back[i] = T[paper[i]][back[i]];          // paper 28..31: grey -8..-2 levels, colour ramps -4..-1 shades
```
So the final colour = palette[ramp_base + max(0, shade_in_ramp − (32 − paper))]. Colours used by the scene:
sky 0x80–0x9e, ground 0x6d, path 0xd1±12, house walls 0x2c±12 (grey), roof/poles/petals 0xec±12, leaves/stem
0x6c±12, trunks 0xcc±12, centre/sun 0xac±12, door 0x0c±12, windows 0x8c±12, banner 0x30±12 (grey), lines darker.
The handwriting (0x73a6) is drawn AFTER the composite, so its pixels are darkened only once (T[g][final]).

### 4.7 Pencil pass (transition only, ticks 3187–3441)
Exactly as notes-sketch §4/§8 (pencil object with its own identity camera at (0,0,−2000), shadow from the light
(−1300,100,−2500) onto the paper plane z = 0 OR-ed (0x20) into a copy of the paper grain at [8], composite with
that copy, then the pencil faces drawn on top without outlines and without paper). Inputs during the transition:
pen x = 195, y = 150 (fixed, from the end of the sketch), z [0x55de] = 0 − 7/tick (to −1421), a_cnt [0x55d2] = 0 +
1/tick (to 123), travel [0x506b] = 0. Hence position = ((195−160)·8.929, (150−100)·8.929, z) = (312, 446, z),
pitch a = 2·a_cnt + 10 (10 → 256 = lying flat), yaw b = a + 90 − 17. The pencil rises off the page towards the
viewer while tilting over; it leaves the screen around tick 3370. Its shadow is the only shadow in this scope.
From tick 3442 [0x55d6] = 2000 and the pencil is never drawn again.

## 5. Camera path (0x7554 at tick 1079, 0x74e8 per tick)

Keys at 0x466f, 5 int32 per key: yaw, pitch (1/1024 turn), x, y, z (world·1024). Key 0 is overwritten by 0x7554
with the current camera (identical to the static key 0). Keys used here (world = K/1024, rounded):

| k | yaw | pitch | x | y | z | | k | yaw | pitch | x | y | z |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 20 | −28 | −679 | 600 | −1589 | | 10 | 20 | 4 | 2668 | 344 | −1780 |
| 1 | 20 | −48 | −670 | 620 | −1519 | | 11 | 20 | −4 | 2692 | 348 | −1590 |
| 2 | 88 | −32 | −1331 | 547 | −2124 | | 12 | 20 | −4 | 2739 | 339 | −1209 |
| 3 | 20 | 25 | −768 | 0 | −2432 | | 13 | 20 | −4 | 2739 | 339 | −1209 |
| 4 | −4 | 36 | 526 | −1363 | −3825 | | 14 | −76 | −4 | 3853 | 227 | −1122 |
| 5 | 31 | 175 | 1408 | −7743 | −2816 | | 15 | 68 | −4 | 589 | 396 | −3080 |
| 6 | 52 | 176 | 1698 | −6304 | −2074 | | 16 | 84 | 28 | −454 | 74 | −2240 |
| 7 | 52 | 32 | 1242 | −1181 | −4269 | | 17 | 48 | 92 | −1099 | −441 | −1832 |
| 8 | 52 | 4 | 1823 | 317 | −2511 | | 18 | 112 | 164 | −1132 | −955 | −1470 |
| 9 | 20 | 4 | 2621 | 334 | −2161 | | 19 | 112 | 32 | −3170 | −515 | −3751 |

Raw int32 values: `rep.SEG[0x466f + 20k + 4c]` (keys 20, 21 = key 19 / (112,32,−6100092,−1421906,−7318605)).
"Key" 22 is the variable block [0x4827..0x4837] (count 0x17, p, coefficient pointer, 675, handwriting end) and
key 23 starts at [0x483b] — garbage that only affects segments 20–22, never reached (max segment here: 17).

```c
// 0x7554 (built once, tick 1079): per channel c (0 yaw, 1 pitch, 2..4 x,y,z), Q(v) = (c >= 2) ? v >> 16 : v
Tg[0] = 0;  Tg[j] = Q(K[j+1][c] - K[j-1][c]) (j = 1..22);  Tg[23] = 0;     // NOT halved (2x Catmull-Rom)
for segment j: d = Q(K[j][c]); e = Q(K[j+1][c]); c1 = Tg[j];
               a = 2d - 2e + Tg[j] + Tg[j+1];  b = e - a - c1 - d;          // Hermite: a u^3 + b u^2 + c1 u + d
// 0x74e8 (per tick, p = [0x482b]): u = p & 255, seg = p >> 8
v = (d << 16) + a*((u*u*u) >> 8) + b*u*u + c1*(u << 8);     // 32-bit = 65536 * H(u/256)
yaw, pitch = v >> 16;     x, y, z = v  (stored), CAMPOS = v >> 10 in 0x5eec   // world = 64 * H(u/256)
```
So positions are a Hermite spline through floor(K/65536)·64 (key positions quantised to 64 world units) with
tangents (K[j+1]−K[j−1])>>16 ·64; angles are integer-valued (0.35° steps). A remake: H(s) in float with
s = 1.875·(t_tick − 3186)/256 (continuous), P_j = floor(K_j/65536)·64 for exact key positions (or K_j/1024 —
up to 64 units different), angles unfloored. T0 = 0 at key 0 means the flight eases out of the sketch view.

## 6. Noise (0x7207 / 0x7244 / 0x726d) — drives the colour transition and the banner flutter

```c
struct { int32 phase, speed, freq, amp; } W[5];             // 0x71b7
void noise_init(void) {                                     // 0x7207 (ticks 3186 and 3442)
    for (k = 0; k < 5; k++) { W[k].phase = rnd(); W[k].speed = (rnd() & 0xff) - 0x80;
                              W[k].freq  = (rnd() & 0x3ff) - 0x200; W[k].amp = rnd() & 0xff; } }
void noise_advance(cl) { for k: W[k].phase += W[k].speed << cl;  if (thr) thr--; }  // 0x7244, per tick
uint8 N[0x800];                                             // 0x69b7, rebuilt at the start of EVERY frame (0x726d)
void noise_table(void) {
    for (i = 0; i < 0x800; i++) { x = 0x800 - i;  s = 0;
        for k: s += sinT[((uint32)(W[k].freq * x + W[k].phase) >> 5) & 0x3ff] * W[k].amp;   // 32-bit
        v = (s >> 18) + 0x80;  N[i] = clamp(v, 0, 0xfa) >> 1; } }                         // 0..125, mean ~64
```
Continuous form: N(i) = clamp(128 + Σk amp_k/4 · sin(2π·((freq_k·(2048−i) + phase_k) mod 32768)/32768), 0, 250)/2,
i.e. only phase mod 32768 matters; freq_k/32 table steps per index (wavelength 32768/|freq| indices).
rnd() = 0xaa2 (notes-sketch §3.2); the values depend on the global call count, so hard-code them
(verified from snapshots t3300 / t4500 / t5500; phase = value right after 0x7207, before the first callback, mod 32768):

| set | k | phase mod 32768 | speed | freq | amp |
|---|---|---|---|---|---|
| transition (0x7207 at 3186; phase += speed per tick from tick 3187) | 0 | 30092 | 104 | −17 | 146 |
| | 1 | 16390 | 34 | −9 | 107 |
| | 2 | 30185 | −84 | −117 | 14 |
| | 3 | 571 | 27 | 178 | 90 |
| | 4 | 20265 | −19 | −462 | 121 |
| scene (0x7207 at 3442; phase += 4·speed per tick from tick 3442) | 0 | 21306 | 1 | 364 | 195 |
| | 1 | 29401 | −43 | 348 | 176 |
| | 2 | 13249 | −12 | 152 | 50 |
| | 3 | 5818 | −8 | −267 | 203 |
| | 4 | 19801 | −3 | 91 | 98 |

(Full 32-bit phases at those points: transition −1975454324, −660127738, −289573399, 392987195, −786641111;
scene 1395249978, −1000279335, 843002817, 859739834, −781890215.) The table used in a frame is built from the
phases of the last tick before that frame (checked: snapshot table = formula with the phases one tick back).

## 7. The colour transition (0x72c4, ticks 3187–3441)

```c
void transition_frame(void) {                               // 0x72c4, after noise_table()
    strokes = 0x113;
    mode = 0; scene_render(); memcpy(colour /*[0x5067]*/, back, 64000);
    mode = 2; scene_render(); memcpy(sketch /*[0x5063]*/, back, 64000);
    thr = byte [0x7324];          // SELF-MODIFIED: immediate of "mov ch,0xff" at 0x7323, decremented per tick by
                                  // 0x7244: 255 at tick 3187 ... 0 at tick 3442 (loop exits when it is 0)
    for (r = 0; r < 200; r++) {
        rowv = (N[0x207 - r] - 0x12) & 0xff;                // row noise: N[0x140..0x207]
        for (x = 0; x < 320; x++)
            back[320*r + x] = (((N[x] + rowv) & 0xff) < thr) ? sketch[..] : colour[..];   // column noise N[0..319]
    }
    // then continue in 0x603f at 0x604a with mode 2 saved: pencil_pass() (pencil, shadow, paper composite, pencil faces)
}
```
- Mode 2 render ("sketch"): background 0x3a, no sky/ground/horizon, every scene face filled with 0x3a (so it hides
  the lines behind it), only the 275 sketched edges drawn and only where they are a silhouette/crease from the
  current camera (so the sketch follows the moving camera); the banner keeps its shaded grey (add = 0). It is the
  same drawing the sketch part produced, now re-rendered every frame.
- The mask is separable: value(x, r) = N[x] + N[0x207−r] − 18 (mod 256). Colour appears where value >= thr; thr
  falls linearly 255 → 0 over 255 ticks (3.64 s), so the colour regions are the high-sum areas of two 1D sine sums
  (soft-edged, roundish blobs on a grid-like pattern) and they grow and drift because the phases move per tick.
  Quirk: where N[x] + N[y] < 18 the 8-bit subtraction wraps to 238..255, so those few deepest-minimum spots turn
  colour first (from thr = 255). Values N are 0..125, so the normal sum − 18 is at most 232: nothing else changes
  before tick ≈ 3210, and thr = 0 shows everything in colour.
- Main also renders both buffers once at tick 3186 (0x7838..0x786d); the first transition frame overwrites them, so
  that work is invisible.
- The banner's vertices are not updated during the transition (0x3c3e is not called): it keeps the shape 0x3a8c
  gave it at init (noise table all 0); it only enters the right screen edge at the very end. At tick 3442 it jumps to
  the noise-driven shape (barely visible).
- Cost: two scene renders + composite per frame → 0.52 frames per tick (≈2.9 M instructions per frame, 36 fps at
  the 1.5 M/tick budget). Everything visible is tick-driven, so this only affects smoothness.

## 8. The banner and the "Statix + Vic (acme)" drawing

### 8.1 Cloth mesh (0x3a8c at init, vertices per frame by 0x3c3e, normals 0x3dc0)
Grid 17 columns x 11 rows of vertices, index = row·17 + col; faces per cell (v = row·17 + col, 16x10 cells):
A = (v, v+1, v+18), B = (v+18, v+17, v); base colour 0x30 (grey ramp), add 0; crease flags: A in the top row 0x08
(edge v→v+1), A in the last column 0x10 (v+1→v+18), B in the first column 0x10 (v+17→v), B in the bottom row 0x08
(v+18→v+17): the four borders are always outlined, interior edges only on fold silhouettes. Object pos 0,
identity: vertices are world coordinates, the light vector is (−92, 7, −177).
```c
sag[0..17] = curve(0, 90, 0, 17);   // 0x2ad0: 0,19,37,52,64,74,82,87,89,89,87,82,75,65,53,38,21,(1 unused); +y = down
for (row = 0; row < 11; row++) for (col = 0; col < 17; col++) {
    (X, Y, Z) = warp(col * 128, row * 95);            // 0x3b69, u = 0..2048, v = 0..950
    vertex = (X, Y + sag[col], Z);
}
for each face: n = (v1-v0) x (v1-v2);  len = rint(sqrt(|n|^2)) (1 if 0);  n = (n << 8) / len (trunc)   // 0x3dc0
(X, Y, Z) warp(u, v) {                                // 0x3b69, N = noise table of this frame
    i = (3*u) >> 5;  j = (v >> 4) + 0x80;             // i 0..192, j 128..187
    X = 0x7bc + u        + (((N[i]      - 64) * v) >> 11) + (((N[j]      - 64) * v) >> 11);
    Y = -0x123 + v       + (((N[i+256]  - 64) * v) >> 11) + (((N[j+256]  - 64) * v) >> 11);
    Z = -3 + (u >> 9)    + (((N[i+512]  - 80) * v) >> 8)  + (((N[j+512]  - 80) * v) >> 8);
}
```
So the top edge (v = 0) hangs straight from x = 1980 to 4028 at y = −291 (pole tops at y = −321, poles at
x ≈ 1912 and 4115), the cloth is 950 units high, sags 90 units in the middle (parabola) and flutters with an
amplitude growing linearly downwards: ±~60 units in x and y at the bottom edge, and in z between −594 and +334 at
the bottom edge (N 0..125 against the offset 80, not the centre 64: on average the bottom bulges ~120 units towards
the camera). Each of X, Y, Z is the sum
of one term varying across the cloth (i, from u) and one down the cloth (j, from v), so the flutter is two
travelling 1D waves. Flat-shaded facets (±12 shades of grey 0x30) with outlines on fold silhouettes — the facets
are clearly visible in the frames.

0x2ad0 `curve(a, m, c, n)` (also used for the peel and for the drawing): n+1 values of the parabola through
(0,a), (1,m), (2,c):
```c
step = (65536 / n) >> 7;  B = 4m - c - 3a;  C = 2m - 2a - B;   // = a + c - 2m
for (i = 0, t = 0; i <= n; i++, t += step) out[i] = ((((B*t + ((C*t*t) >> 8)) >> 8) + 2*a) >> 1);
// s = t/256 in [0, 2); out = a·(s-1)(s-2)/2 + m·s(2-s) + c·s(s-1)/2.  Because step is truncated, the last
// sample is at s = n·floor(512/n)/256 < 2 unless 512 % n == 0 (n=17: s=1.992; n=25: 1.953; n=54: 1.898).
```

### 8.2 The drawing: data (0x5979 at init) and reveal
106 records at 0x18a2f (1192 bytes, until 0x18ed7). Record = `u8 type, u8 cnt` then data; n = cnt >> 2 (0 → 1);
coordinates are u16 in drawing units, converted with `U(x) = ((x*0x2d000) >> 16) + 300` (×2.8125) and
`V(y) = ((y*0x23000) >> 16) + 100` (×2.1875) into banner (u, v):
- type 2 (10 records, circles): `cx, cy, r`; n = max(n, 3); step = (65536/n) >> 6 (≈1024/n); for a = 0, step, …
  (emit, stop after the first a >= 1024): x = (sinT[a]·r + (cx<<16)) >> 16, y = (cosT(a)·r + (cy<<16)) >> 16 → (U(x), V(y)).
- type 1 (58 records, straight lines): `x0, y0, x1, y1`; points = curve(U(x0), U((x0+x1)>>1), U(x1), n) and the
  same for y (midpoint taken in drawing units) → n+1 points, stopping slightly short of the end point (see above).
- any other type (38 records, all type 3: arcs): `x0, y0, x1, y1, x2, y2`; points = curve(U(x0), U(x1), U(x2), n),
  curve(V(y0), V(y1), V(y2), n): a parabola THROUGH the middle point, n+1 points.
- after every record a (0, 0) entry = pen up. After the last one another (0,0) → "end". [0x4833] = 675 entries.
Point list: 674 entries (int32 u, int32 v), u 322..1800, v 135..948 (lower right 3/4 of the 2048x950 cloth).
Content (plotted from the data): "Statix" in big handwriting at the top left, a stick figure with long hair and a
smile sitting at a computer to its right, "+" below, a stick figure with spiky hair at two turntables/speakers at
the lower left, "Vic" and "(acme)" at the lower right. The first records are the right figure's head.

### 8.3 Drawing it (0x73a6, every frame after 0x603f, in the banner part and the peel)
```c
void handwriting(void) {
    q = start /*[0x483b]*/;  P = project(q[0]); q++;
    for (;;) {
        if (q >= end /*[0x4837]*/) return;
        prev = P;
        if (q->u == 0) { q++; if (q->u == 0) return; P = project(*q); q++; continue; }   // pen up / end
        P = project(*q); q++;
        pencil_line(prev.x, prev.y, P.x, P.y);                  // 0x18b7 into the composited back buffer
    }
}
project(u, v) {                                                 // 0x7418
    (X, Y, Z) = warp(u, v) - CAMPOS;                            // the same warp as the cloth, but WITHOUT the sag
    z = CAM[2]·(X,Y,Z);  if (z <= 25.0) return previous P;      // (keeps the old value)
    e = 0xe00000 / (z >> 16);
    return ( (int)(((int64)(CAM[0]·(X,Y,Z)) * e) >> 32) + 160,  (int)(((int64)(CAM[1]·(X,Y,Z)) * e) >> 32) + 100 );
}
```
- Reveal: end = start + 8·k with k = N − 4210 after the callback of tick N (first point at tick 4211, p > 0x780;
  checked: k = 289 at tick 4500). One entry per tick, pen-up entries included, so the visible pen speed varies with
  the spacing of the curve points; complete at tick ≈ 4885 (674 entries, 9.6 s). Then it stays (including during
  the peel, where it keeps being drawn on the live scene).
- It is drawn after everything else, with no depth test and no back-face test: it shows through anything in front
  of the banner and from any angle. It follows the cloth's flutter (same warp, same noise table) but not its sag,
  so it is up to 90 units "above" the cloth position in the middle columns (barely noticeable, guess: oversight).
- Endpoints are whole pixels, each segment is a 3-pass pencil line: when the banner is seen at a grazing angle
  (ticks ~5200–5400) the strokes pile up into dark blobs.

## 9. Page peel (0x2b55 / 0x2b20 / 0x2b6c / 0x2ca2 / 0x2ad0) — complete

Used twice: here (old = the live 3D scene in the back buffer, new = the static logo page) and at the start of the
teal plane part (0x687a: old = the finished logo page [0x1edc5], new = the back buffer with the live canyon,
callback 0x6434 → 0x2b20, ticks 6018–6146; notes-planes.md). The peel writes straight into VGA and is the only thing
updating the screen during its loop.
```c
void peel_set(u8 *old, u8 *new) { p = 0; OLD = old; NEW = new; }          // 0x2b55: [0x1eaa9], [0x23a0], [0x23a4]
void peel_tick(void) { p += 4; if (p > 256) p += 3; if (p > 700) p = 700; } // 0x2b20, per tick (callback)
// p: 4, 8, ..., 256 (64 ticks), 263, 270, ..., 697, 700 (64 ticks): 128 ticks = 1.83 s; loop ends at p >= 700

void peel_draw(void) {                                                       // 0x2b6c, per frame
    A[0..25] = curve((cosT(p) * 200) >> 16, (cosT(p >> 1) * 100) >> 16, 0, 25);          // table 0x21455: screen rows
    B[0..25] = curve((sinT[-p & 1023] * 1600) >> 16, (sinT[(-p >> 1) & 1023] * 800) >> 16, 0, 25);  // 0x21655: zoom
    ymax = 0;  ymin = 200;  sy = 200 << 16;                                  // [0x2183d], [0x21841], [0x2245d]
    for (i = 0; i < 25; i++) {
        ymax = max(ymax, A[i]);  ymin = min(ymin, A[i]);                     // running, A[0..i]
        sy -= 8 << 16;                                                       // strip i = source rows 192-8i .. 199-8i
        strip(A[i+1], A[i], B[i], B[i+1], sy);
    }
    if (ymax < 200) memcpy(VGA + 320*ymax, NEW + 320*ymax, 320*(200 - ymax));   // below the fold: the new page
}
void strip(y1, y0, b0, b1, sy) {                                             // 0x2ca2
    if (y1 == y0) return;
    if (y1 > y0) {                       // strip upside down = back of the page
        src = PAPER /*[0x1edf5], raw grain 28..31 used as colour indices = dark grey*/;
        top = y0; bot = y1; bt = b0; bb = b1;
        if (bot <= 0 || top >= 200) return;  top = max(top, 0);  bot = min(bot, 200);
    } else {                             // front side
        src = OLD;  top = y1; bot = y0; bt = b1; bb = b0;
        if (bot <= 0 || top >= ymin) return;  top = max(top, 0);  bot = min(bot, ymin);   // hidden under the folded part
    }
    h = bot - top;  if (h <= 0) return;
    sc  = (bt << 5) + 0x10000;                                  // 16.16 source pixels per screen pixel = 1 + b/2048
    dsc = ((((bb << 5) + 0x10000) - sc) * recip[h]) >> 16;
    dsy = recip[h] << 3;                                        // 8 source rows over h screen rows
    for (r = 0; r < h; r++, sc += dsc, sy += dsy) {             // NB: no correction when top was clipped to 0
        row = src + 320 * (sy >> 16);
        x0 = sc * -160;  s = row + 160 + (x0 >> 16);  f = x0 << 16;    // 32-bit fraction accumulator
        for (x = 0; x < 320; x++) { VGA[320*(top+r) + x] = *s;  f += sc << 16 (carry into s);  s += sc >> 16; }
        // i.e. source column = 160 + floor((x - 160) * sc / 65536); no bounds check (reads neighbouring rows when sc > 1)
    }
}
```
Geometry: A[i] is the screen row of the page's horizontal line at source row 200 − 8i. At p = 0, A is the straight
line 200 → 4 (identity mapping, shifted down ~4 rows), B = 0 (scale 1). As p grows, the bottom edge (A[0] =
200·cos p) rises to the top of the screen (p = 256), past it (−200 at p = 512) and comes back to −82 at p = 700,
while the middle line A[13] follows 100·cos(p/2): the page folds up from the bottom like a calendar sheet. Where A
increases with i the strip is upside down and shows the page back (grey grain); front strips are clipped to rows
above min(A[0..i]), the highest screen row reached by the boundaries nearer the bottom edge (correct occlusion by
the fold). B gives the
perspective: scale 1 − 1600·sin(p)/2048 at the bottom edge (0.22 = 4.5x magnified, "close to the viewer", at
p = 256), 1 at the top edge. Rows below max(A[0..24]) show the new page. Phases (tick of p): 5520 (p≈148) scene
squashed at the top, new page below; 5540 (≈230) grey back band appears; 5560–5590 (≈350–560) grey band shrinking
upward; 5611 (700) done. (p = 4·(N − 5483) for N <= 5547, then 256 + 7·(N − 5547).)

Quirks to decide on in a remake: (1) the curve never reaches s = 2, so A[25] ≈ 4–6 for p < 512 and rows 0..A[25]−1
are never written — they keep the VGA content of the last frame before the peel (stale image strip at the top);
(2) clipped strips are not re-parameterised (an 8-row source strip is squeezed into its visible part);
(3) sc > 1 (only on back strips, p > 512) reads past the row ends — invisible because the grain is noise;
(4) the back is the raw grain (28..31 → grey 27..30 of 63), much darker than the paper front (~50–56), and not
re-composited; (5) nearest-neighbour sampling, 25 strips with linear interpolation of zoom and source row.

0x5623 (setup, tick 5480–5482, ≈3 ticks during which the screen is frozen): builds the target page in [0x1edc5]:
white 0x3f, "present the" (rows 10–49) and "intro" (rows 158–197) as 63 − (5x blurred text)/4 with a sharp copy
added before the last blur, the 224x83 4-bit "Paper" sketch logo from 0x1bf03 at (48, 50), then composited with
the paper grain (0x1949). Self-modifies the glyph row stride at 0xeb8 to 320 and resets [0x1edc1]. Full details:
notes-logo.md §3.

## 10. Time bases

Everything that moves is advanced in the 70 Hz callbacks; the main loop only draws. Nothing depends on the frame
rate.

| variable | where | time base | rate / range |
|---|---|---|---|
| path accumulator [0x466b], p = acc >> 7 [0x482b] | 0x6199/0x620b/0x6170 (+ once in main at 3186) | per tick | +240/tick; p +1.875/tick; camera from p of the tick (before its update) |
| camera yaw/pitch/x/y/z [0x5de8..] | 0x74e8 in the callbacks | per tick | spline §5; key 0 (3186) → segment 17.8 (5611) |
| transition threshold, byte [0x7324] | 0x7244 | per tick | 255 → 0, −1/tick, ticks 3187–3441 |
| noise phases 0x71b7 | 0x7244 | per tick | += speed (transition), += 4·speed (scene, peel) |
| noise table 0x69b7 | 0x726d | per frame, from the per-tick phases | — |
| banner vertices / normals | 0x3c3e / 0x3dc0 | per frame, from the noise table | — |
| pencil z [0x55de], pitch counter [0x55d2] | 0x6199 | per tick | −7/tick (0 → −1421), +1/tick (0 → 123) |
| handwriting end [0x4837] | 0x620b | per tick | +1 entry/tick while p > 0x780 (ticks 4211..≈4885) |
| peel p [0x1eaa9] | 0x2b20 from 0x6170 | per tick | +4/tick to 256, then +7/tick to 700 (128 ticks) |
| [0x1ee11] | main peel loop | per frame | +1, never read anywhere (dead) |
| end of transition | [0x7324] == 0, tested per frame | tick 3442 | |
| end of 3D part | music counter >= 0x280 | tick 5480 (counter 640) | |
| end of peel | p >= 700, tested per frame | tick 5611 (counter 655) | |

Measured from full.ticks (1.5 M instructions per tick): transition 133 frames / 255 ticks = 0.52 frames per tick
(36 fps, ≈2.9 M instructions per frame); 3D scene 2083 / 2038 = 1.02 (71.5 fps, ≈1.47 M per frame); peel 139 / 131
= 1.06 (74 fps, ≈1.41 M per frame). A remake can render at display rate and evaluate all variables at fractional
tick time (the original shows per-tick steps: camera p in steps of 1–2/256 segment, angles in 1/1024 turns).

## 11. Rendering shortcuts a high-resolution remake should evaluate exactly instead

- Projection with integer z (z >> 16), integer division 0xe00000 / z, 1/32-px screen coordinates that are then
  floored to whole pixels for fills and lines → project in float; keep the 320x200 anisotropy (fx = 224·W/320,
  fy = 224·H/200 for a 4:3 picture).
- No near clipping: a face with any vertex at z <= 25 disappears entirely (affects faces reaching behind the
  camera, e.g. the long path quads). Proper clipping would show more geometry than the original — a look choice.
- Painter's algorithm with 1024 buckets (sum of 3 integer depths, 64-unit steps, faces nearer than ~400 all in
  bucket 0) per object group; groups always paint over earlier groups. With a z-buffer, keep the decals (door,
  windows) drawn after the house with a depth bias, and the outlines depth-tested against the faces drawn after them.
- Flat shading with 25 integer levels: shade = floor(12.5·cos θ) against one directional light (−1300,100,−2500)
  (normalised to length 200, rounded) and integer normals of length 256 → continuous cos θ, fractional ramp index.
- 32-entry palette ramps + paper subtraction (T table) → ramp colour at index max(0, base + shade − (32 − paper));
  for greys (< 0x40) max(0, c + 2·paper − 64). Interpolate the ramp for fractional indices.
- Sky: 32 colour steps over 350 rows → continuous gradient from ramp index 0 (350 original rows above the horizon)
  to 31 at the horizon.
- Pencil outlines: 2-px-wide DDA, 3 passes, ±1–3 px jitter hashed on the integer endpoints, per-pixel darkening
  from a 1024-byte random table. Scale width and jitter with the resolution; decide whether the jitter hash uses the
  320x200-quantised endpoints (same "boiling" rate as the original) or something stable.
- Banner: 17x11 vertex grid, flat facets, noise from an 8-bit 1D table (N 0..125) → may evaluate warp() on a finer
  grid with the continuous 5-sine sums (the facets are part of the original look, though).
- Handwriting: integer-pixel endpoints, curves sampled with n+1 points (truncated step, short of the end) — keep the
  same point list (its index drives the reveal timing) but draw the segments at full resolution; no depth test.
- Transition: hard threshold of an 8-bit separable noise sampled per 320x200 pixel → evaluate N(x·320/W), N(row)
  continuously and anti-alias the edge; keep the mod-256 wrap if the early "holes" are wanted.
- Peel: 25 strips, nearest-neighbour row/column sampling, zoom linearly interpolated per strip, stale top rows —
  evaluate A(s), B(s) per output row (parabola through the same three points, s ∈ [0, 2]) and sample the old page
  bilinearly; clip the source columns.
- 1024-entry sine table and integer angles everywhere (camera, noise) → float trig.

## 12. Verification scripts (paper/re/models/scene3d/)

- `run_cap.py`: run_paper.py + `--cap A:B` (dump the first 4 MB at code address A and again at B within the same
  tick → `<out>.capN.pkl`) and `--regtrace addr` (register log). E.g.
  `run_cap.py --load --snap ../snaps/t4500.snap --out c4500 --ticks 4504 --budget 6000000 --cap 7901:7906`
  (a large budget keeps a whole frame inside one tick).
- `model.py`: integer-exact model (scene_render, transform, buckets, fill, 0x18b7/0x1612, composite, pencil pass +
  shadow, transition composite, noise table, curve 0x2ad0, warp 0x3b69, banner vertices, camera eval,
  handwriting 0x73a6/0x7418, peel 0x2b6c/0x2ca2).
- `cmp1.py` (scene, 0 px diff), `cmp2.py`/`cmp3.py` (banner, normals, handwriting, camera), `cmp4.py` (transition
  frame, 0 px), `cmp5.py` (peel, 0 px at 6 values of p), `hw.py` (decodes and plots the drawing → hw_uv.png).

## 13. Not determined / guesses

- Whether the handwriting's missing sag (§8.3) and the stale top rows of the peel (§9) are intended — guess: not.
- [0x1ee11] (incremented per peel frame) and the matrix written to 0x217d9 every 0x620b tick are never read in
  this scope or elsewhere (xref) — dead code.
- The pre-render of both transition buffers at tick 3186 is overwritten before it is ever shown (dead work).
- Whether a 1996 machine showed the outline "boiling" as strongly: it depends only on camera motion per drawn frame
  (fewer frames → bigger jumps but the same hash), so it should look similar at any frame rate.
- Noise/random values are taken from this emulator run (they depend on the global random-call count, which is
  deterministic for the intro, so the original shows the same values).
