# Scope "temple": the flight through the temple (31:63..36:16)

Routines: init 0x404464 (with 0x403900 camera rig, 0x4039c8 objects, flares and light), start 0x40461c (script entry
93), tick 0x40407c (→ 0x403f10), frame 0x404424 (→ 0x4040b0), stop 0x404668 (entry 95). The only engine routine that
nothing else uses is the rasterizer **0x416dc8** (the glowing pipes, section 7.6); everything else the part calls is
shared and described here by what goes in and what comes out (notes-engine.md has the engine, notes-core.md the
clock, the palette and rand). Addresses are linear (NOTES.md). Time: refresh r = record r of `full.frames`;
**M** = [0x4248c4], the timer callback counter (notes-core.md 1.1), 280.345 per second, **M = 4r − 151** at the
retrace callback of refresh r in this part (notes-core.md 1.2).

Checked against the emulator (section 12, scripts in `models/temple/`; runs resumed from `snaps/f11276.snap`
reproduce `full.frames` 11276..12959 bit for bit):

- the whole animated state (camera rig, camera position, the two swinging pipes, flare positions, light range, pipe
  glow values) over all 1,212 frames of the part: exact (matrices to 3e-14) on the 774 frames in which M did not
  change while 0x4040b0 ran; ±1 (light ±2) on the others, because 0x4040b0 reads M several times;
- the random flare positions: Watcom rand() from srand(0x45eafc) (flight init) after 9 calls, all 30 values;
- the projection of every visible vertex (the engine's 1/16 px screen coordinates to truncation), back-face culling,
  the sort keys and the painter's order;
- the flares: all 20 flares of two frames pixel for pixel (position, size, texel stepping, flare.shd lookup);
- the pipes' and the lit shell's pixel lookups on every pixel of three frames (`flare.shd[32 + level][old]`,
  `temple.int[level][texel]`), the light levels and the environment-map coordinates of every vertex exactly;
- which pixels a frame does not set (a run with the back buffer filled with a sentinel before every frame).

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

The camera orbits inside a round hall (a 16-sided room with a sunken bowl in the middle and four translucent pillars),
looking slightly down at the centre and rolling slowly round its view axis. Everything is chrome: the bowl and the
pillars are environment-mapped with `temple/temple.raw` (a chrome sphere), the pillars half transparent, the hall
itself mapped with the same picture through garbage mapping coordinates (streaks) and lit by a pulsing point light in
the centre. Three pink glowing pipes stand in the bowl (one vertical, two swinging like pendulums), and ten pink star
flares (`temple/flare.raw`) float on one side of the hall, moving in and out from the centre.

| records | music | what is on screen |
|---|---|---|
| 11276..11277 | 31:63 | both buffers cleared by 0x40461c: index 0 everywhere, through the white flash (DAC ≈ white) |
| 11278 | 31:63 | first temple frame, palette still fading from white to `temple/temple.pal` (15 refreshes, 11276..11290) |
| 11278..12599 | 31:63..35:48 | the temple, 50.7 frames per second in the capture (1,212 frames drawn in 1,673 refreshes) |
| 12600..12639 | 35:48 | script entry 94 (0x400438): fade to black in 40 refreshes; frames are still drawn |
| 12639..12949 | 35:48..36:16 | black (the part keeps drawing into a black palette) |
| 12950 | 36:16 | entry 95 0x404668: tick and frame routine 0; the screen stays black; 36:24 ends the demo |

## 2. Data

### 2.1 INSIDE.DAT files (offsets in the unpacked block, NOTES.md)

| file (name string in the exe) | offset, size | format, use |
|---|---|---|
| `temple/temple.raw` (0x420b34) | 0x15c767, 65,536 | 256 × 256 8-bit texture in `temple.pal`: a chrome sphere on black (environment map), section 10 |
| `temple/flare.raw` (0x420b48) | 0x16c767, 13,312 | 128 × 104 8-bit flare sprite, values = rows of `flare.shd` (0 = nothing added), section 10 |
| `temple/temple.int` (0x420b5c) | 0x16fb67, 65,536 | 256 × 256 blend table, symmetric: `int[a][b]` ≈ the palette colour nearest to (pal[a] + pal[b]) / 2 |
| `temple/flare.shd` (0x420b70) | 0x17fb67, 65,536 | 256 rows × 256: `shd[row][c]` ≈ pal[c] plus a pink colour that grows with the row (section 9) |
| `temple/temple.pal` (0x420b84) | 0x18fb67, 768 | 6-bit palette of the part |
| `temple/mesta21.sro` (0x420b98) | 0x18fe67, 1,280 | the bowl: 49 vertices, 84 faces |
| `temple/mesta22.sro` (0x420bac) | 0x190367, 4,372 | the four pillars: 192 vertices, 256 faces |
| `temple/mesta23.sro` (0x420bc0) | 0x19147b, 4,240 | the hall: 173 vertices, 268 faces |
| `temple/putki.sro` (0x420bd4) | 0x1927f3, 372 | a pipe: 15 vertices (3 rings of 5), 20 faces; the first of the three entries with this name (NOTES.md), loaded three times |

The `.sro` format is in notes-engine.md ("The .sro object format"): header dwords +4 vertex offset, +8 vertex count,
+0xc face offset, +0x10 face count; vertex = 6 int16 (x, y, z, u, v, unused); face = 4 int16 (flags, a, b, c). The
loader 0x410bb0 keeps `trunc(c · 6553/65536)` of x, y, z (the parts pass 0x1999: ×0.0999908), u = (uint16 u) >> 4,
v = −((uint16 v) >> 4) (1/16 texel). As the engine holds them (`models/temple/meshes.py`, picture `out/meshes.png`):

| object | loaded bounding box (min..max) | shape |
|---|---|---|
| 0 mesta21 | (−184, −196, −184)..(184, 28, 184) | the bowl: 12-sided, open at the bottom ring y = −196, rings at y −149, −109, −82, apex 28 |
| 1 mesta22 | (−575, 0, −575)..(575, 409, 575) | four square pillars at radius ≈ 500..578 on the axes, floor (y 0) to ceiling (y 409), open at both ends |
| 2 mesta23 | (−819, −196, −819)..(819, 409, 819) | the hall: 16-sided wall at r 819 from the floor (y 0) to the ceiling (y 409); floor and ceiling have four square holes where the pillars pass; the floor runs down as a funnel from r ≈ 520 to the bowl's rim (r 184, y −196); faces point inwards |
| 3, 4, 5 putki | (−5, −5, −60)..(5, 6, 60) loaded | a pentagonal tube (radius 6) along z, rings at z −60, 0, 60; no end caps |

The u, v words of mesta21 / 22 / 23 are leftovers of the exporter (they look like x86 code and stack bytes). Objects
0 and 1 overwrite them every frame (environment mapping); **object 2 uses them as they are** (section 7.4): its
texture coordinates are effectively random per vertex. A remake reads them from the file like the engine does.

### 2.2 Variables (BSS) and constants

| what | address |
|---|---|
| file pointers | [0x425e68] temple.raw, [0x425eb8] flare.raw, [0x425ec8] temple.int, [0x425e6c] flare.shd, [0x425ed0] temple.pal, [0x425a50..0x425a64] the six .sro (mesta21, 22, 23, putki ×3) |
| scene, materials | [0x425eb4] scene (0x50 bytes, "temple scene"), [0x425ebc] material 1 ("temple material"), [0x425ec0] material 2 ("temple material 2"), 0x44 bytes each |
| camera rig | [0x425ecc] "dummy camera holder" (0xc8), [0x425e74] "camera" (0xc8) |
| objects | [0x425de0 + 4i], i = 0..5 ("temple scene object"), flares [0x425e78 + 4i], i = 0..9 ("flare", 0x104), light [0x425e5c] ("temple light source", 0xd4) |
| per-object state the tick / frame routines own | positions 0x4259c0 + 0x18i (3 doubles), matrices 0x425ac8 + 0x48i (3 × 3 doubles, row-major); holder: position 0x425df8 (zero, never written), matrix 0x425a68; camera: position 0x425ab0, matrix 0x425e10; flares: base positions 0x425c78 + 0x18i, matrices 0x425588 + 0x48i |
| flare count | [0x425e64] = 10 (0x4045eb) |
| unused | [0x425e58] = 500 and [0x4227f8] (data, 2): the tick routine bounces [0x425e58] by ±2 between 150 and 700; nothing reads it. [0x425e60] memory tag (never freed) |
| doubles | 0x420a94 −0.03, 0x420a9c −0.01, 0x420aa4 1/179, 0x420aac 35, 0x420ab4 0.01745329252 (DEG), 0x420abc 1/231.4, 0x420ac4 33.3, 0x420acc 1.5707963268 (π/2), 0x420ad4 1/271.1, 0x420adc 0.7, 0x420ae4 1/461.1, 0x420af0 16, 0x420af8 0.95, 0x420b00 1/891.1, 0x420b08 0.3, 0x420b10 0.6, 0x420b18 0.15, 0x420b20 971.1 |
| floats | 0x420aec 1000, 0x420b28 256, 0x420b2c 320, 0x420b30 352 |

Doubles the code pushes as immediates (each double = two dword pushes, the high dword first): camera
position (0, 320, −850): 0x403970 `push 0x40740000`, 0x403969 `push 0xc08a9000`; camera angles 0.2617993878 (15°,
0x4039af / 0x4039b4) and 3.1415926536 (0x4039a1 / 0x4039a6); pipe 5 angle 1.5707963268 (0x403c8a / 0x403c8f); pipe
3 / 4 initial angles (overwritten every tick); pipe y 100 (0x403ccf `push 0x40590000`), −150 (0x403d10, 0x403d29
`push 0xc062c000`); tick steps 0.01 (0x403f22 / 0x403f27) and 0.005 (0x403f47 / 0x403f4c). Floats pushed: pipe
scales 1.5, 1.5, 5.3 (0x403b6c, 0x403b62, 0x403b5d; again 0x403c2d, 0x403c23, 0x403c1e) and 3, 3, 4.6 (0x403cad,
0x403ca3, 0x403c9e). The other parameters are code constants (named in the pseudo-C).

## 3. Init 0x404464 (precalc, called by 0x4007c0 after the flight init)

```c
void temple_init(void)                       // 0x404464; [0x4248c0] (busy) held
{
    tag = new_tag(); use_tag(tag);           // 0x40b3c0 / 0x40b4d0 → [0x425e60]
    temple_raw = dat("temple/temple.raw");  flare_raw = dat("temple/flare.raw");
    temple_int = dat("temple/temple.int");  flare_shd = dat("temple/flare.shd");
    temple_pal = dat("temple/temple.pal");
    sro[0..2] = dat("temple/mesta21.sro" .. "mesta23.sro");  sro[3..5] = dat("temple/putki.sro") ×3;
    mat1 = alloc(0x44); mat2 = alloc(0x44);  scene = alloc(0x50);         // 0x40b4d8 zero-fills ([0x4b6ed8] = 1)
    scene_init(scene);                       // 0x410878: pipeline 0x4229c0, focal 256, near 0x2000, far 0x400000
    scene_viewport(scene, 320, 200, 0, 0, 320, 200);   // 0x410618: size, clip window x 0..320, y 0..200
    scene->focal = 170;                      // [scene+0x3c] = 0xaa  (the field of view, section 7.2)
    material(mat1, temple_raw, 0, temple_int,         0, 255, 255, 8, 8);   // 0x4105e4: texture, -, table,
    material(mat2, temple_raw, 0, flare_shd + 0x2000, 0, 255, 255, 8, 8);   //   -, u/v masks, log2 w/h
    nflares = 10;  bounce = 500;             // [0x425e64], [0x425e58] (unused)
    camera_rig();                            // 0x403900
    objects();                               // 0x4039c8
}
```

**0x403900 camera rig.**
```c
    holder = alloc(0xc8, "dummy camera holder");  object_init(holder);   // 0x4108a8: zero, identity matrix
    holderM = R(0, 0, 0);                    // 0x4145f4 into 0x425a68 (identity)
    scene_add(scene, holder);                // 0x411154 (front of the scene list)
    camera = alloc(0xc8, "camera");  camera_init(camera);                 // 0x410520 → 0x4108dc(cam, 0, 0, 0)
    scene_add(scene, camera);
    camera->parent = holder;                 // [camera+0xc]
    scene->camera = camera;                  // [scene+0x38]
    camPos = (0.0, 320.0, -850.0);           // 0x413510 into 0x425ab0
    camM   = R(0.2617993878, 0, 3.1415926536);                           // into 0x425e10
```
R(a, b, c) (0x4145f4: identity, then 0x414270 / 0x414344 / 0x414418) = **Rx(a) · Ry(b) · Rz(c)** with the usual
right-handed matrices (Rx = [[1,0,0],[0,c,−s],[0,s,c]], Ry = [[c,0,s],[0,1,0],[−s,0,c]], Rz = [[c,−s,0],[s,c,0],
[0,0,1]]), column vectors, rows stored as 3 doubles (checked in unicorn, `models/temple/rot.py`). 0x414070(A, B):
A = A · B.

**0x4039c8 objects, flares, light.**
```c
    for (i = 0; i < 6; i++) {
        obj[i] = alloc(0x10c + sro[i].nverts * 0x40 + sro[i].nfaces * 0x34, "temple scene object");
        object_init(obj[i]);
        load_sro(obj[i], sro[i], 0x1999, mat1);    // 0x410bb0: ×6553/65536, every face material 1
        set_raster(obj[i], 0x412db0);             // 0x410850: every face's rasterizer
        scene_add(scene, obj[i]);
        obj[i]->flags |= 0x1000;                  // [+0xc5] |= 0x10: environment-mapped u, v (0x41acc4)
        objPos[i] = (0, 0, 0);  objM[i] = R(0, 0, 0);
    }
    set_raster(obj[0], 0x412db0);                 // bowl: opaque texture
    set_raster(obj[1], 0x41669c);                 // pillars: texture blended with the background (temple.int)
    obj[2]->flags &= ~0x1000;  obj[2]->flags |= 0x40;            // hall: sro u, v; lit (0x41aba4)
    set_raster(obj[2], 0x414e5c);                 // hall: temple.int[light level][texel]

    // pipe 3
    set_raster(obj[3], 0x416dc8);  object_pos(obj[3], 0, 160, 0);  // 0x410868 (overwritten every frame)
    objM[3] = R(1.8325957146, 0.2617993878, 0.5235987756);          // overwritten every tick
    v12 = obj[3]->vertex[12];                     // the loaded (−59, 19, 608) → (−5, 1, 60)
    translate(obj[3], -v12.x, -v12.y, -v12.z);    // 0x41172c (+ bounding box 0x410de8): pivot at vertex 12
    scale(obj[3], 1.5f, 1.5f, 5.3f);              // 0x4116c8: v = trunc(v · s) per axis (box NOT updated)
    face_normals(obj[3]);  vertex_normals(obj[3], 0);               // 0x410918, 0x410a70
    for each face f of obj[3]: f->material = mat2;
    // pipe 4: the same with object_pos (0, 160, 0) and objM[4] = R(1.308996939, −0.3490658504, 0)
    // pipe 5: no translation; objM[5] = R(1.5707963268, 0, 0) (fixed); scale (3.0f, 3.0f, 4.6f); normals; mat2
    objPos[5] = (0, 100, 0);  objPos[3] = objPos[4] = (0, -150, 0);

    for (i = 0; i < 10; i++) {                    // flares
        fl[i] = alloc(0x104, "flare");  billboard_init(fl[i]);       // 0x415790
        fl[i]->pixels = flare_raw;  fl[i]->table = flare_shd;  fl[i]->draw = 0x415dc8;   // through the table
        z = rand() % 1000 - 500;  y = rand() % 300 + 70;  x = rand() % 1000 - 500;       // call order z, y, x
        flareBase[i] = (x, y, z);                 // 0x425c78 + 0x18i
        fl[i]->pitch = 128; fl[i]->w = 128; fl[i]->h = 104; fl[i]->s = 1.0f; fl[i]->size = 0.25f;
        scene_add(scene, fl[i]);
        flareM[i] = R(0, 0, 0);
    }
    light = alloc(0xd4, "temple light source");  object_init(light);
    light->flags |= 0x80;                         // a light
    light->range = 500;  light->intensity = 1600; // [+0xc8] (overwritten every frame), [+0xcc]
    scene_add(scene, light);                      // position (0, 0, 0), never moved
```
The pipes as the engine holds them (`meshes.py`): pipe 3 / 4 vertex = trunc((trunc(sro · 0.0999908) − (−5, 1, 60))
· (1.5, 1.5, 5.3f)): a pentagonal tube of radius ≈ 7.5 from z = 0 (ring 3, the pivot end) over −318 (ring 2) to
−636 (ring 1); pipe 5: trunc(loaded · (3, 3, 4.6f)), radius ≈ 15, z −275, 0, 275. Face normals (length 1024) and
vertex normals (length 819.2) are recomputed after the scaling. The bounding boxes are not: pipes 3 / 4 keep (0, −6, −120)..(10, 5, 0)
(radius 120), pipe 5 the loaded box (radius 60). The engine culls an object only when all 8 box corners are outside
one screen edge; with these small boxes that never happened in the capture (`models/temple/bbox.py`), so it has no
visible effect here.

**Random numbers.** The flare positions are the 10th..39th rand() calls after srand(0x45eafc) at 0x402967 (the
flight init, which runs just before; its 9 calls come first). Watcom's rand(): seed = seed · 0x41c64e6d + 0x3039,
return (seed >> 16) & 0x7fff; `%` on a non-negative int. Result (`models/temple/randseq.py`, equal to snapshot
f11276): (−267, 188, −335), (−203, 345, −25), (−491, 342, −204), (−223, 138, 160), (−478, 86, −423), (−170, 228,
−114), (−480, 152, −137), (−260, 196, −250), (−45, 134, 469), (−83, 118, −374) (check values, the remake replays
the generator). All ten x are negative: the flares stay on one side of the hall.

## 4. Start 0x40461c (31:63, entry 93) and stop 0x404668 (36:16, entry 95)

```c
void temple_start(void)                      // 0x40461c
{
    memset(buffer[0x4248d0], 0, 64000);  memset(buffer[0x4248cc], 0, 64000);   // 0x40b7a4: both screens black
    white_flash();                           // 0x400498 (entry 91 already did one)
    set_palette(temple_pal);                 // 0x400364: base = temple.pal; the running fade now ends there
    set_tick(0x40407c);  set_frame(0x404424);   // 0x400af4 / 0x400b04 (P = 0)
}
void temple_stop(void) { set_tick(0); set_frame(0); }   // 0x404668; nothing is freed
```
Palette: from white to temple.pal in 15 refreshes (records 11276..11290; the DAC ends at temple.pal or one less,
the core's truncated float fade). The part never touches the palette itself; entry 94 (35:48) fades to black in 40
refreshes (12600..12639).

## 5. Tick 0x40407c (every retrace callback, 70.086 Hz) → 0x403f10

```c
void temple_tick(void)                       // 0x40407c
{
    tick_matrices();                         // 0x403f10
    bounce += step;  if (bounce < 150 || bounce > 700) step = -step;   // [0x425e58], [0x4227f8]: unused
}
void tick_matrices(void)                     // 0x403f10, M = [0x4248c4] as the callback sees it (4r − 151)
{
    holderM = holderM · R(0, 0.01, 0);       // 0x425a68: the camera orbits about the vertical axis
    camM    = camM    · R(0, 0, 0.005);      // 0x425e10: the camera rolls about its view axis
    for (i = 0; i < 10; i++)
        flareM[i] = flareM[i] · R(-0.01 * (10 - i), -0.03 * i, 0);   // no visible effect (section 7.7)
    A = sin(M * (1/179.0)) * 35 * DEG;       // double; DEG = 0.01745329252
    B = cos(M * (1/231.4)) * 33.3 * DEG;
    objM[3] = R(A + 1.5707963268,  B, 0);    // 0x425ba0: pipe 3
    objM[4] = R(1.5707963268 - A, -B, 0);    // 0x425be8: pipe 4
}
```
Closed forms after n tick calls (the products of rotations about one axis; checked to 1e-13 over the part):
```
H(n) = Ry(0.01 n)                                           holder
C(n) = Rx(0.2617993878) · Rz(3.1415926536 + 0.005 n)        camera (local)
```
n is the number of tick calls since the part began: the first is the retrace callback of refresh 11276 (snapshot
f11276 already shows one), so **after the retrace of refresh r, n = r − 11275**. A frame routine that starts at M0
uses **n = floor((M0 + 151) / 4) − 11275** (`models/temple/latency.py`, all 1,212 frames). In the capture one tick
is lost: the retrace callback of refresh 11967 hit while the frame routine held [0x4248c0] (notes-core.md 1.6), so
from 11967 on n is one less (0.01 rad of orbit, 0.005 rad of roll; invisible). A remake ignores the loss.

## 6. Frame 0x404424 (once per drawn frame) → 0x4040b0

```c
void temple_frame(void)                      // 0x404424
{
    scene->buffer = [0x4248d0];              // the back buffer ([0x4b6ec0])
    [0x4248c0]++;  update_scene();  [0x4248c0]--;   // 0x4040b0; the busy flag blocks the retrace work (5)
    render(scene);                           // 0x40c6ac: the engine draws everything (section 7)
    debug_overlay();                         // 0x400bec: off ([0x42277c] = 0)
    flip();                                  // 0x400b14
}

void update_scene(void)                      // 0x4040b0; M = [0x4248c4], read anew at each use
{
    for (i = 0; i < 6; i++) {
        obj[i]->pos = (trunc(objPos[i].x), trunc(objPos[i].y), trunc(objPos[i].z));   // ints +0x10..+0x18
        obj[i]->matrix = objM[i];                                                   // 0x413e68 → +0x28
    }
    for (i = 0; i < 10; i++) {
        k = fabs(sin((M + 13 * i) * (1/271.1)) * 0.7) + 0.15;     // 0.15 .. 0.85
        fl[i]->pos = (trunc(flareBase[i].x * k), trunc(flareBase[i].y * k), trunc(flareBase[i].z * k));
        fl[i]->matrix = flareM[i];
    }
    holder->pos = (trunc(0), trunc(0), trunc(0));  holder->matrix = holderM;
    light->range = trunc(fabs(sin(M * (1/461.1))) * 1000);       // 0 .. 1000, [light+0xc8]
    for (j = 0; j < 6; j++) {                // glow values of the pipes: vertices 5..10 (word +0x30)
        w = sin((M + 265 + 53 * j) / (55 + 11 * j + 971.1)) * 16 * 16 * 0.95;   // ±243.2 (fdivp: (M+..)/(..))
        obj[3]->vertex[5 + j].glow = trunc(w + 256.0f);
        obj[4]->vertex[5 + j].glow = trunc(w + 320.0f);
        obj[5]->vertex[5 + j].glow = trunc(w + 352.0f);
    }                                        // vertices 0..4 and 11..14 keep 0 (zero-filled allocation)
    c = sin(M * (1/891.1)) * 0.3 + 0.6;      // 0.3 .. 0.9
    camera->pos = (trunc(0 * c), trunc(320 * c), trunc(-850 * c));
    camera->matrix = camM;
}
```
(The float constants 256, 320, 352 and 1000 are floats; the arithmetic is the FPU's 64-bit precision, every
float → int conversion truncates toward zero.)

So per frame, with M the clock and n the tick count:

| what | value | range / period |
|---|---|---|
| camera orbit | holder Ry(0.01 n): the camera position is rotated about the y axis | 0.701 rad/s, one turn in 8.96 s |
| camera radius / height | (0, trunc(320 c), trunc(−850 c)), c = 0.6 + 0.3 sin(M/891.1) | radius 255..765, height 96..288; period 19.97 s |
| camera roll | Rz(π + 0.005 n) in the camera's own frame | 0.350 rad/s, a full roll in 17.9 s |
| camera pitch | Rx(15°): looks 15° down | fixed |
| pipes 3, 4 | Rx(π/2 ± A) Ry(±B), A = 35° sin(M/179), B = 33.3° cos(M/231.4) (M of the last tick) | periods 4.01 s, 5.19 s |
| pipe 5 | Rx(π/2) at (0, 100, 0): vertical, y −175..375 | fixed |
| flare i | trunc(base_i · k_i), k_i = 0.15 + 0.7 abs(sin((M + 13 i)/271.1)) | in and out along the ray from the origin, period 3.04 s, phases 13 i |
| light range R | trunc(1000 abs(sin(M/461.1))) | 0..1000, period 5.17 s |
| pipe glow (vertex 5 + j) | trunc(243.2 sin((M + 265 + 53 j)/(1026.1 + 11 j)) + 256 / 320 / 352) | 12..499, 76..563, 108..595; periods 23.0..24.2 s |

The camera's world transform (0x40c224, notes-engine.md): position = trunc(H · (0, trunc(320 c), trunc(−850 c))),
rotation = H · C. The pipes 3 and 4 rotate about their pivot (vertex 12, the bowl end) at (0, −150, 0): the tube
(z 0..−636 in object space) points up (Rx(π/2) takes −z to +y) and swings by ±35° and ±33.3°; pipe 4 mirrors pipe 3
(the angles negated, so they cross).

## 7. What the engine does with it

All of this is the engine's (notes-engine.md); here is what it means for this part, checked on the dumps.

### 7.1 Transforms and camera space

World: p = M_obj · v + pos (objects and flares have no parent, so their world transform is their own; the camera's
parent is the holder). Camera space: q = C_w^T · (p − cam_w), with C_w = H · C and cam_w = trunc(H · camPos)
(`models/temple/proj.py`, `state.py`).

### 7.2 Projection

Scene focal 170 ([scene+0x3c]) on a 320 × 200 window: XS = trunc(320 · 32768 · 170 · 1.220703125e−5) = 21760, YS =
trunc(200 · 32768 · 170 · 1.6276041666666666e−5) = 18133 (0x40c4e4), centre (160, 100):
```
x = 160 + 170 · q.x / q.z            y = 100 + 141.6640625 · q.y / q.z        (141.66 = 18133/128)
```
in 1/16 pixels, truncated. On a 4:3 display the pixels are square in this projection: horizontal field of view
2 atan(160/170) = 86.5°, vertical 2 atan(100/141.664) = 70.4°. Near plane z = 64 (scene 0x2000 / 128; triangles are
clipped there, notes-engine.md), objects whose box is entirely nearer than z = 128 are culled. Camera +y is screen
down; with the roll at π the world's +y (up) is screen up. Checked: every visible vertex of 17 frames to the
engine's truncation (`proj.py`).

### 7.3 Visibility and order

Back faces are culled per object (face normal against the camera in object space): the hall's faces all point
inwards and all count as front faces from inside; the pipes and pillars show their near side only (`faces.py`).
Faces and flares go into one list sorted by depth, far first, drawn with no depth buffer (painter's algorithm): mesh
face key trunc((z0 + z1 + z2)/48) with z in 1/128 units (the mean camera depth × 8; no object bias in this part),
flare key trunc(z128)/16 (the centre's depth × 8). Checked on all faces of the dumped frames (the few exceptions
are near-clipped faces).

### 7.4 Texture coordinates

- Objects 0 and 1 (and the pipes, which do not read them): environment mapping 0x41acc4 per vertex, with n the
  vertex normal (length 819.2), Mc = the object-to-camera rotation C_w^T · M_obj, and X20, Y20 the vertex's
  projection numerators (vertex +0x20 = trunc(85 q.x), +0x24 = trunc(70.83 q.y): 8 XS / 2048, 8 YS / 2048):
  ```
  u = −(trunc((2n)·Mc[0] + 2048) & 0xfff) − X20/64          v = (trunc((2n)·Mc[1] + 2048) & 0xfff) + Y20/64
  ```
  in 1/16 texel (exact on every vertex of 17 frames, `envlight.py`; X20/64 is a C division): the camera-space
  normal picks a spot within ±102 texels of the sphere picture's centre (128, 128), shifted by the vertex's
  camera-space position, −q.x/12.05 and +q.y/14.46 texels (not divided by depth).
- Object 2: the sro's u, v (section 2.1), fixed.
- Texture: 256 × 256, wrapping (masks 255), texel = floor((u + 1)/16) etc.; every channel is interpolated affinely
  in screen space (no face in INSIDE enables the perspective path; notes-engine.md).

### 7.5 The light (object 2 only)

Per visible vertex of the hall, 0x41aba4 / 0x41b508 with the light at the origin (exact on every vertex,
`envlight.py`):
```
level = 0 (scene ambient)
if (distance(light, hall) <= hall.radius 915 + R):          // own positions: always
    d = L − p (L = light in object space = (0,0,0) here); dist = |d|
    if (dist <= R && n·d >= 0):
        a = (dist < 1) ? 1600 : 1600 − dist · (1600 / R)     // [light+0xcc] = 1600, R = [light+0xc8]
        c = a · (n·d) / (dist · 512);  if (c > 0) level = trunc(level + c)
    level = min(level, 992)
```
= 1600 (1 − dist/R) · 1.6 cos θ: a point light in the centre whose reach R pulses between 0 and 1000 units every
5.17 s; the funnel round the bowl lights up first, the floor, the ceiling's centre (409 above) and the wall (r 819)
only while R is large. The
level is interpolated over the face; the pixel uses floor((level + 1)/16) (0..62) as a **palette index** (7.6).

### 7.6 Rasterizers and pixel indices

| object | rasterizer | pixel index |
|---|---|---|
| 0 bowl | 0x412db0 | `tex[u, v]` (opaque) |
| 1 pillars | 0x41669c | `temple.int[tex[u, v]][old]`: 50 % of the texel's colour and 50 % of what is behind |
| 2 hall | 0x414e5c | `temple.int[L][tex[u, v]]`, L = floor((level + 1)/16): 50 % texel colour, 50 % **palette entry L** |
| 3, 4, 5 pipes | 0x416dc8 (temple only) | `flare.shd[32 + G][old]`, G = floor((glow + 1)/16) interpolated: pink light added to what is behind |
| flares | 0x415dc8 (billboard through its table) | `flare.shd[texel][old]` (7.7) |

The hall's "lighting" thus mixes the texel with temple.pal[0..62] (black at 0; index 1 = (40, 24, 30) pink, 2 =
(7, 19, 31) blue, 40 = (59, 56, 63) near white, 41 = (17, 15, 54) blue, 57 = (41, 37, 59) ...): as R grows the
lit parts flash through these colours. The table lookups were checked on every pixel of three frames (`lookups.py`).

**0x416dc8** (the pipe rasterizer, used by nothing else): the shared triangle setup (0x41be3c / 0x41be91, near
clipping, notes-engine.md) and one channel, the word at vertex +0x30 (here the glow values of section 6),
interpolated like every engine channel (16.16 accumulator, screen-linear, level = floor((glow + 1)/16) at the
sample point; the gradient arithmetic is in notes-engine.md); the span loop (0x417100..0x41711c) is
```c
for each pixel: level = acc >> 16 & 0xff;  acc += dacc;  *dst = table[level << 8 | *dst];  dst++;
```
with table = material 2's table = flare.shd + 0x2000 (row 32). The subdivided perspective path (0x41717c) exists
but is not used (face bits 10..12 are 0). The pipe's glow is 0 on rings 1 and 3 except vertex 10 (first vertex of
ring 3), so each pipe glows most along its middle ring (rows up to 32 + 37 = 69) and fades to row 32 at both ends,
one corner of the ring-3 end lit as well. Fitting the logged levels against screen-linear interpolation of glow/16:
80 % exact, 99 % within one level on faces larger than 40 px (`levels.py`; the rest is the setup's sub-pixel rule,
modelled in notes-engine.md).

### 7.7 The flares (billboards 0x4157d4 / 0x415878 with edx = 1)

0x4157d4 transforms the flare's position (only its position: the flare matrices the tick routine turns are never
used) to camera space ×128 (q128) and puts it in the list (7.3). 0x415878 (checked pixel for pixel on all 20 flares
of two frames, `flare.py`):
```c
z = q128.z / 128;  if (z < 64 || z > 0x400000) return;          // near = trunc(0x2000/128)
X = q128.x * 1.0 * (XS / 1024) / z;  Y = q128.y * 1.0 * (XS / 1024) / z;   // 1/16 px; XS also for y!
W = 128 * 0.25 * XS / z;  H = 104 * 0.25 * YS / z;               // 1/16 px
y0 = trunc(16*100 + Y - H/2);  y1 = y0 + trunc(H);
x0 = trunc(16*160 + X - W/2);  x1 = x0 + trunc(W);
du = 2048 / trunc(W);  dv = 1664 / trunc(H);                     // texels per pixel (0x40ba78 truncates W in place)
clip to the window (start offsets −(clipped 1/16 px) · d / 16); rows y0/16 .. y1/16 − 1, columns x0/16 .. x1/16 − 1
u0 = (15 − (x0 & 15)) * du / 16;  v = (15 − (y0 & 15)) * dv / 16;
per row: src = flare_raw + trunc(v) * 128;  u = trunc(u0 * 65536), du16 = trunc(du * 65536);
         per pixel: *dst = flare_shd[src[u >> 16] << 8 | *dst];  u += du16;      v += dv;
```
So a flare is a screen-aligned sprite of 128 × 104 texels, 256 × 208 world units (2 units per texel), centred on
its projected position, at depth z covering 43,520/z × 29,466/z pixels. **Quirk:** the vertical offset of the centre
uses the horizontal focal length (170 instead of 141.66): a flare sits 1.2 times as far above or below the screen
centre as a point of the geometry at the same place. A remake projects the flare centre with y = 100 + 170 q.y/q.z
to keep the composition.

## 8. Time bases

| variable | time base | how the remake evaluates it at refresh r |
|---|---|---|
| holder angle, camera roll | per refresh (tick routine), n calls | n = r − 11275 (continuous between steps); H = Ry(0.01 n), C = Rx(15°) Rz(π + 0.005 n) |
| pipes 3, 4 | per refresh, from M at the tick (M = 4r − 151) | A, B from M = 4r − 151 (continuous) |
| flare matrices, bounce counter | per refresh | not needed (no effect) |
| camera position, flare positions, light range, pipe glow | per drawn frame, from M | M = 4r − 151 (core: the frame's clock with latency L = 0); keep the original's truncations to integers only where they matter (they don't visibly: positions in world units) |
| palette | script events (white flash 31:63, fade to black 35:48), per refresh | the core's palette system |

There is no other state: the part is a pure function of n and M, so seeking is trivial. In the capture a frame is
drawn with the M it reads at its start and shown 5..12 callbacks later (median 8 = 2 refreshes; `latency.py`), and
frames take 4..8 callbacks (50.7 per second).

## 9. Colours

- Palette: `temple/temple.pal` throughout (0x400364), white flash at the start, fade to black at 35:48 (section 4).
  The part writes no palette entries and runs no palette effects.
- `temple.int[a][b]`: symmetric; the palette colour of the average of colours a and b (mean error 1.4 of 63 per
  channel against the exact average, 59 % of entries are exactly the nearest palette colour; the table was built
  with some other distance or rounding). In true colour: (colour(a) + colour(b)) / 2.
- `flare.shd[row][c]`: c's colour with a pink colour added. Fitted per row as clamp(colour(c) + A_row, 63)
  (`shdfit.py`): for the rows flare.raw uses, A grows with the texel value from (0, 0, 0) at 0 over (10, 3, 5) at
  32, (20..27, 8..12, 9..15) at 79..175, (40, 19, 23) at 223, (56, 24..27, 28..31) at 234..239, (63, 43..46,
  48..50) at 244..248 to white at 253..254 (mean error 0.0..2.9 of 63 per channel), with a few rows out of order
  (63 → (0, 0, 4), 204 → (14, 6, 9), 226 → nothing): the sprite's values are colour indices of a flare palette, not
  a clean intensity ramp. Rows 32..69 (the pipes) are irregular: mostly
  (10..24, 3..9, 5..10) with bright rows 56 (56, 26, 29), 60 (35, 16, 21), 62 (42, 18, 25), 65 (33, 15, 19) and a
  dark row 63 (0, 0, 4); as the interpolated glow crosses those levels the pipes show bright and dark bands.
  A remake derives an additive colour per row from flare.shd at run time (least squares over the 256 background
  colours, as `shdfit.py` does) and adds it in true colour with clamping; a continuous level blends the two
  neighbouring rows' colours.

## 10. Pictures

| picture | kind | use | redraw? |
|---|---|---|---|
| `temple/temple.raw`, 256 × 256, temple.pal | a chrome sphere on black (a rendered or digitised environment map; no artist credit in the file) | texture of the bowl and the pillars (environment-mapped: only the disc of ±102 texels around the centre, plus the camera-space shift), and of the hall through random coordinates (any part of the picture, black included); read as colours (the blend tables are colour averages) | possible in principle (keep the 256 × 256 layout: the hall's coordinates address it); it is a texture, so filtered reads of the original are the natural choice |
| `temple/flare.raw`, 128 × 104, values = flare.shd rows | a white star burst with eight rays, a glow and a dotted ring | additive sprite through flare.shd | no: an effect reads it through the shade table; convert it to an RGB glow with the per-row colours of section 9 |

## 11. Does the part overwrite the whole frame?

No (`models/temple/sentcmp.py`: the back buffer filled with index 255 before each frame's faces, compared with the
capture): in every frame some pixels keep, or blend over, what the buffer held from two frames earlier:
- cracks along shared edges between the hall's and the bowl's faces (the engine's fill rule): median 150 pixels per
  refresh;
- **the inside of the pillars**: the hall's floor and ceiling have square holes where the pillars pass, and the
  pillars are translucent, so a view into a pillar shows no opaque surface (rays cast through these pixels hit
  nothing opaque, `nearclip.py`): up to 1,550 pixels per refresh when the camera looks along a pillar
  (records 11417..11767, 12780..12949). In the capture these spots show black or smeared remains of older frames,
  with the pillar texture blended over them every frame.

Records 11276..11277 show the cleared buffers (index 0 through the white flash).

## 12. Verification (`models/temple/`, README.md there)

| check | result |
|---|---|
| run from f11276 vs `full.frames` (hooks only read) | 0 differing pixels / palette entries in 1,684 refreshes |
| state model (`state.py`) vs 1,212 dumped frames | exact on 774 frames, ±1..2 on the others (M read several times) |
| rand sequence (`randseq.py`) | 30 of 30 values |
| projection (`proj.py`) | every visible vertex within the engine's truncation (1/16 px) |
| culling and sort (`faces.py`) | drawn faces = front faces on screen; keys as stated (near-clipped faces excepted) |
| env map and light (`envlight.py`) | u, v and light levels of every vertex exact |
| table lookups (`lookups.py`) | 187,574 of 187,574 pixels (pipes and hall, three frames) |
| flares (`flare.py`) | 20 of 20 flares pixel-exact |
| uncovered pixels (`sentcmp.py`, `nearclip.py`) | section 11 |
| bounding boxes (`bbox.py`) | the stale pipe boxes never cull a visible pipe |

## 13. Shortcuts a remake evaluates exactly

- Integer world positions (trunc of the camera, flare and object positions and of the loaded and scaled vertices,
  0.1 world-unit steps of the sro coordinates) → floats from the same formulas (keep the formulas: e.g. the pipe
  pivot is the truncated vertex 12).
- 1/16-pixel screen coordinates and the engine's span rule → exact projection (centre (159.5, 99.5625) for
  pixel-centre sampling, notes-engine.md), anti-aliased edges; no cracks.
- Affine interpolation of u, v, light level and glow → perspective-correct.
- Nearest texels → filtered reads in colour; the 64 light levels and the glow levels → continuous (blend the two
  neighbouring table rows in colour).
- temple.int → the true average of the two colours; flare.shd → an additive colour per row (section 9).
- Flare sprite: nearest texels stepped in 16.16 → a filtered textured quad of the same size and position.
- M read several times per frame, the lost tick at 11967, the render latency: one clock per frame.

## 14. Quirks to keep

- The hall's texture coordinates are the sro's leftover bytes: streaks of the sphere picture across the hall.
- The hall's light mixes texels with palette entries 0..62 (colour flashes, not brightness); the light pulses
  between reach 0 and 1000 every 5.17 s.
- The pipes' glow profile (rings 1 and 3 dark except vertex 10) and flare.shd's irregular rows (bands).
- Flares positioned with the horizontal focal length vertically (7.7); all flares on the −x side of the hall.
- Painter's order: the translucent pillars, the pipes and the flares blend in the original's back-to-front order
  (temple.int is not commutative over several layers in order of drawing: draw translucent things sorted).
- Not to keep: the cracks and the stale pixels inside the pillars (section 11): clear to black before drawing (the
  buffers start black, 0x40461c); under "Known differences" in the remake's README.

## 15. Not determined

- Who made temple.raw and flare.raw, and whether temple.raw is rendered or digitised (guess: rendered by Destop).
- The exact metric with which temple.int and flare.shd were built; the per-row additive colours are a fit.
- The 20 % of pipe pixels whose level differs by one from the screen-linear fit come from the engine's sub-pixel
  rules (notes-engine.md has the exact gradient arithmetic); not modelled here.
