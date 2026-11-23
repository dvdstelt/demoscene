# Scope "intro + logo": the light drops and the logo part

Intro: init 0x403770, script 0x4038c8 (0:00), tick 0x403214, frame 0x403748 (0x40337c drops, 0x403674 blur,
0x4036e8 shade), 0x4031a0 (3:00), 0x4038dc (3:63, end). Logo: init 0x402f9c (0x402ca0 camera, 0x402d0c object and
light), script 0x403164 (3:63), tick 0x402e30, frame 0x402f1c (0x402e44), 0x402f10 (4:32), 0x403184 (5:63, end).
Addresses are linear (NOTES.md). Refresh = screen refresh (70.086 Hz) = record of `full.frames`. The SurRender calls
are described by what goes in and what comes out (notes-engine.md has the engine's internals).

Checked against the emulator (scripts in `models/intro-logo/`, section 7), with runs resumed from `snaps/f38.snap`
and `snaps/f1446.snap` (their `.ticks` equal `full.ticks` line for line):

- intro: every tick routine call (1407) gives X, Y and the random seed exactly; every frame (974) gives the drops,
  the seed and the light buffer exactly; all 352 dumped light buffers and 176 dumped finished frames are equal
  byte for byte; **the whole intro replays exactly** from its second frame to its last (events in the captured
  order), and the first frame is reconstructed exactly from the init and the generator's state (2.7);
- logo: in all 768 drawn frames the rotation matrix is S^k exactly (k = refreshes), and the engine's camera-space
  z, the 28.4 screen coordinates, the outcodes, the back-face set, the face set and the drawing order equal the
  model exactly; the per-vertex light equals the model in every vertex; the 2D overlays (mask, text) are exact in
  all 130 dumped frames; an affine rasteriser model gives 80..86 % of the 3D pixels identical (the rest are one
  shade level or one texel off along boundaries: the engine's fixed-point steps), coverage equal to 0.2 %.

---------------------------------------------------------------------------------------------------------------

## 1. What is on screen

| part | refreshes | music | frames drawn | on screen |
|---|---|---|---|---|
| intro | 38..1445 | 0:00..3:63 | 975 (0.6925 per refresh, 48.5 fps) | intro/intro.raw (a woman's body and a tube of "Amazing GOOP" glue on a swirled background) appears where spots of light ("drops") land near a point that zips across the screen; the light spreads and the picture fills in; a dark disc (centre (80, 80), radius 60 pixels) never gets light. The palette fades in from black over the first 120 refreshes. At 3:00 (refresh 1101) all drops jump to random places over the whole screen and their number grows from 22 to 128 |
| logo | 1446..2147 | 3:63..5:63 | 769 (about 1.1 per refresh; at most one is shown per refresh) | the inside of a spiky sphere (logo/obuke.sro, textured with logo/twlogo.raw) turning around the viewer: red and dark-red radial streaks with bright pink/white tips, black wedges at the screen edges; over it a dark "ghost" of the CNCD / INSIDE lettering (the mask logo/inmask.raw), and from 4:32 (refresh 1627) the coloured lettering itself (logo/inside.raw). The script flashes the palette white at 4:32, 5:00, 5:63 and inverts it at 5:32 (core palette routines). Refresh 1446 still shows the intro's last frame through the logo's palette (the palette is set before the first logo frame is finished) |

---------------------------------------------------------------------------------------------------------------

## 2. Intro

### 2.1 Data

| address | contents |
|---|---|
| [0x425558] | intro/intro.raw (INSIDE.DAT, 64,000 bytes = 320 × 200, 8 bit, intro.pal) |
| [0x425578] | intro/intro.pal (768 bytes, 6 bit) |
| [0x42556c] | intro/intro.shd (16,384 bytes = 64 levels × 256 colours, row = level) |
| [0x425560] | SurRender bitmap "intro pic" = 0x415e50(intro.raw, stride 320, width 320, height 200), type set to 3 (copy, index 0 transparent) |
| [0x425564] | light buffer "intro lightbuf", 0x40b4d8(0x1f400): 320 × 200 int16 (row-major, pitch 640 bytes), cleared |
| [0x425568] | drops "intro light drops", 0x40b4d8(0x820): 130 records of 16 bytes: float x, float y, int32 intensity, dword unused (cleared, never used) |
| [0x425548], [0x425550] | X, Y (double): the light source |
| [0x425574] | count (int): drops in use |
| [0x425570] | grow flag (int): 0, set to 1 at 3:00 |
| [0x42555c] | the memory tag of the intro (0x40b3c0 / 0x40b4d0; freed at 3:63 by 0x4038dc → 0x40b6fc) |
| 0x42095c.. | constants (doubles): 0.02, 10.0, 4.7, 0.02320185614849188 (= 1/43.1), 0.03690036900369004 (= 1/27.1), 4.4, 0.03184713375796178 (= 1/31.4), 12.0, 3.7, 0.027700831024930747 (= 1/36.1), 0.024330900243309004 (= 1/41.1), 2.6, 319.0, 199.0, -40.0 (0x4209cc); floats 319.0 (0x4209d4), 199.0 (0x4209d8), 80.0 (0x4209dc); double 60.0 (0x4209e0) |

The allocator clears what it allocates ([0x4b6ed8] = 1), so the light buffer starts at 0 and the drops' unused
dword is 0.

### 2.2 Init 0x403770 (in the precalc)

```c
tag = new_tag();                                    // 0x40b3c0 / 0x40b4d0
drops = alloc(0x820, "intro light drops");
shd = file("intro/intro.shd"); pal = file("intro/intro.pal"); pic = file("intro/intro.raw");
light = alloc(0x1f400, "intro lightbuf");           // int16[200][320] = 0
bmp = make_bitmap(pic, 320, 320, 200, "intro pic"); bmp->type = 3;
set_palette(pal);                                   // 0x400364
fade(1.0f, 120);                                    // 0x4004ac: to pal x 1.0 in 120 steps (see 2.8)
for (i = 0; i < 130; i++) {                         // the first rand() calls of the program: seed 1
    drop[i].x = (float)(rand() % 320);
    drop[i].y = (float)(rand() % 200);
    drop[i].intensity = 0;
}
Y = 150.0; X = 200.0; flag = 0; count = 22;
```

With intensity 0 these 130 positions have no effect until a drop is respawned; they only matter as the state of the
generator and as positions of drops that are never respawned before 3:00 (they add 0).

### 2.3 Tick routine 0x403214 (once per refresh)

T = the part tick counter [0x4248c8] (int). In the capture the tick routine always sees T = 4k + 2, k = refreshes
since the intro's first tick (k = 0 at refresh 38, T = 2: the script reset the counter between the third and the
fourth timer interrupt of refresh 37, and the retrace interrupt that runs the tick routine is the second one after
that; notes-core.md has the general rule).

```c
X += sin(T * 0.02) * 10.0 * 4.7 + cos(T * 0.02320185614849188) + sin(T * 0.03690036900369004) * 4.4;
Y += cos(T * 0.03184713375796178) * 12.0 * 3.7 + cos(T * 0.027700831024930747) + sin(T * 0.024330900243309004) * 2.6;
if (X > 319.0) X = rand() % 40 + 30;                // re-enters at 30..69
if (0.0 > X)   X = rand() % 50 + 250;               // at 250..299
if (Y > 199.0) Y = rand() % 20 + 30;                // at 30..49
if (0.0 > Y)   Y = rand() % 20 + 160;               // at 160..179
```

(x87 extended precision; the model in doubles stays exact over the whole intro.) The steps are large (up to 53
pixels per refresh in X and 48 in Y), so the source sweeps across the screen in a few refreshes and re-enters at a
random place: 330 of the 1407 tick calls call rand(). There is no closed form (random resets); it is a recurrence
per refresh.

### 2.4 Frame routine 0x403748 (once per drawn frame)

```c
drops_step();          // 0x40337c
blur();                // 0x403674
draw_bitmap(bmp, 0.0f, 0.0f);   // 0x415df0, type 3: dst = src unless src == 0 (then dst keeps its old value)
shade();               // 0x4036e8
debug_overlay();       // 0x400bec: prints the music position and memory when [0x42277c] != 0 (it is 0)
flip();                // 0x400b14
```

**Drops 0x40337c.**

```c
if (flag > 0 && count < 128) count++;
for (i = 0; i < count; i++) {
    d = &drop[i];
    if (rand() % 20 == 0) {                                     // respawn near the source
        int xi = trunc(rand() % 90 + X + -40.0);                // x in X-40 .. X+49
        d->y = (float)trunc(rand() % 80 + Y + -40.0);           // y in Y-40 .. Y+39
        d->x = (float)xi;
        d->intensity = rand() % 6500 + count * 100;
        if ((!rand()) % 3) d->intensity = -d->intensity;        // (sic) true only when rand() returns 0
        d->intensity += (count - 22) * 30;
    }
    if (0.0 > d->x)   d->x = rand() % 50 + 260;
    if (0.0 > d->y)   d->y = rand() % 30 + 160;
    if (d->x > 319.0) d->x = rand() % 30 + 50;
    if (d->y > 199.0) d->x = rand() % 30 + 45;                  // (sic) sets x: the drop stays below the screen
    if (d->x < 1 || d->x >= 319 || d->y < 1 || d->y >= 199) continue;
    if (sqrt((80 - d->y)^2 + (80 - d->x)^2) < 60.0) continue;   // the dark disc
    s = &light[trunc(d->y)][trunc(d->x)];
    *s = clamp(*s + d->intensity, -30000, 30000);
}
```

Every active drop adds its intensity at its pixel **every frame**, not only when it respawns (it respawns with
probability 1/20 per frame: a mean life of 20 frames, 0.4 s). Positions are whole
pixels. Before 3:00 the intensity is 2200..8699 (count 22); after 3:00 it is rand() % 6500 + 100 count + 30 (count
− 22), up to 22,479 at count 128. The negation (meant as one drop in three, `!rand() % 3` parsed as `(!rand()) %
3`) happens only when rand() returns 0 (1 in 32768); it never happened in the capture. A drop whose y leaves the
bottom (y > 199 from a respawn near a source low on the screen) gets a new x instead and is not drawn until it
respawns.

**Blur 0x403674**, in place, rows 1..197, columns 1..318, in raster order:

```c
for (y = 1; y < 198; y++)
    for (x = 1; x < 319; x++)
        L[y][x] = (4 * L[y][x] + L[y][x-1] + L[y][x+1] + L[y-1][x] + L[y+1][x]) / 8;   // C division (toward 0)
```

L[y][x−1] and L[y−1][x] are already this frame's values (Gauss-Seidel order). Row 0, row 198, row 199, column 0 and
column 319 are never blurred (row 198 can still receive deposits: y < 199). Measured on an impulse of 30,000: the
variance grows by 0.33 pixel² per frame per axis (0.25 for a symmetric Jacobi step; the in-place order spreads a
bit more), the centre of mass drifts −0.003 pixel per frame toward the upper left, and the truncation removes
0.1..0.5 % of the light per frame at these levels; small values die out, which is what keeps the dark disc dark
and the edges of lit regions sharp.

**Shade 0x4036e8**: for every pixel of the draw target (it reads [0x4248d0], which the flip keeps equal to the draw
target [0x4b6ec0]):

```c
lev = clamp(L[y][x], 0, 63);
dst[y][x] = shd[lev * 256 + dst[y][x]];          // intro.shd, row = level
```

dst holds intro.raw's index after the blit, except where intro.raw is 0 (784 pixels, all in dark areas): there
the blit leaves the pixel of the frame drawn two frames earlier, which is always black: intro.pal 0 and 76 are
(0, 0, 0) and intro.shd maps 0 and 76 to 76 at every level (the buffers start at 0). So index 0 is black.

**The whole frame is overwritten** (the blit writes every non-zero pixel, the shade writes all 64,000).

### 2.5 Script events

- 0:00, 0x4038c8: tick routine 0x403214, frame routine 0x403748 (refresh 38).
- 3:00, 0x4031a0 (ran after intro frame 737, refresh 1101, 1063 tick calls into the intro):

  ```c
  flag = 1;
  for (i = 0; i < 130; i++) { drop[i].x = rand() % 320; drop[i].y = rand() % 200; drop[i].intensity = 7000; }
  ```

  so the next frames light 23, 24, .. 128 random places over the whole screen (count + 1 per drawn frame: 128 after
  106 frames, about 2.2 s), and drops that respawn come back near the source with the larger intensities.
- 3:63, 0x4038dc: routines off, frees the intro's memory.

### 2.6 Time bases

| variable | time base |
|---|---|
| X, Y | per refresh (recurrence above, T = 4k + 2), random resets |
| palette fade-in | per refresh, 120 refreshes from the intro's start (2.8) |
| count, the drops, the deposits, the blur | **per drawn frame**: frame-rate dependent. A faster machine spreads the light faster and deposits more per second |
| the 3:00 event | script, music position 3:00 |
| random numbers | Watcom rand() 0x4134cd shared by the tick routine (in the timer interrupt) and the frame routine, so the sequence depends on how ticks and frames interleave |

In the capture (150 M instructions per second) 975 frames are drawn in 1408 refreshes: 0.6925 frames per
refresh, steady over the whole intro (68..70 per 100 refreshes; the gap between two frames' drop loops is 1 tick
542 times and 2 ticks 432 times). Two ticks fell inside a drop loop (intro frame 558 during tick 802, which made no
rand() call; frame 854 during tick 1231, between drops 111 and 126).

### 2.7 Exact replay (what the remake needs)

The light field is content (where the picture appears), and it can be replayed exactly; the model does it
(`intro_model.py --chain`, `intro_start.py`):

- start state at the intro's first frame: drops from the init (seed 1, 2.2), X = 200, Y = 150, count 22, flag 0,
  light 0; **the generator's state at the first frame is 0xf83d63af**, the state 63 rand() calls after
  srand(0x45eafc) (the flight's init 0x402967 calls srand with that address; the 63 calls are the later parts'
  inits). The first frame's drop loop runs before the first tick (X, Y still 200, 150) and makes 30 rand() calls;
- then the events in the captured order: tick k (k = 0, 1, ...; T = 4k + 2) and the frames, frame i's drop loop
  after a measured number of ticks (`intro_schedule.py` writes the table: frame, ticks before its drop loop,
  refresh; frame 0 after 0 ticks, then gaps of 1 or 2 ticks), the 3:00 event after frame 737 (1063 ticks), the
  two ticks inside a drop loop (frame 558: no rand() call, any place gives the same result; frame 854: between
  drops 111 and 126).

Stepping the frames at a fixed 0.6925 per refresh instead gives the same look with a different random sequence
(the rand() calls of the tick routine land between other frames). Either way the picture shown at time t is the
last frame finished by t; the remake can blend the level field between two consecutive frames for smooth motion.

### 2.8 Colours

Palette intro.pal through the fade 0x4004ac(1.0, 120) started in the init: the fade steps run in the timer callback,
which is installed only after the precalc, so the DAC is black up to refresh 38 and reaches intro.pal (some entries
one lower: the core's float steps, notes-core.md) at refresh 157. The remake: intro.pal × min(1, (refreshes since
the intro started) / 120).

Each pixel's index is intro.shd[level][picture index] with level = clamp(L, 0, 63). intro.shd is a darkening
table: row 0 is black, rows 61..63 are mostly the identity; a row's colours are close to the palette colour
scaled by about level / 64 (least-squares factor of row L: 0.048 at 4, 0.231 at 16, 0.490 at 32, 0.733 at 48, 0.936
at 60, 0.957, 0.976, 0.997 at 61..63; mean colour error of that fit 0.5..1.5 of 63; `shades.py`). It is the
nearest palette colour to the scaled colour, so it has the palette's quantisation steps.

### 2.9 Shortcuts a high-resolution remake should evaluate exactly

- The light field is a 320 × 200 int16 grid stepped per drawn frame with integer deposits at whole pixels and a
  truncating blur. Keep the grid simulation as it is (it defines where the light goes, and the truncation decides
  how far it spreads); sample it at high resolution with a smooth filter (bicubic of clamp(L, 0, 63) or of a
  slightly wider clamp) instead of one level per original pixel.
- 64 shade levels through a table → a continuous level: colour = picture colour × g(level) with g from intro.shd
  (the per-level factor above, or interpolate the table's colours between floor and ceil of the level).
- The picture is shown with nearest-neighbour pixels → a filtered read (it is a photo, not redrawn; 6.1).
- The palette fade in 120 integer steps → continuous.

---------------------------------------------------------------------------------------------------------------

## 3. Logo: set-up

### 3.1 Data

| address | contents |
|---|---|
| [0x4254fc] | logo/twlogo.raw: 65,536 bytes = 256 × 256 texture, logo.pal |
| [0x425530] | logo/logo.shd: 16,384 bytes = 64 levels × 256 (row = level, 32 = identity) |
| [0x425538] | logo/logo.pal (768 bytes) |
| [0x4254f8] | logo/inside.raw: 34,611 bytes = 249 × 139, no header |
| [0x425150] | logo/obuke.sro: 3,640 bytes (3.2) |
| [0x425508] | logo/inmask.raw: 37,191 bytes = 253 × 147, values 0..15 |
| [0x42552c] | logo/inmask.shd: 4,096 bytes = 16 × 256 |
| [0x425460] | material, 0x44 bytes "logo material" |
| [0x425510] | scene, 0x50 bytes "logo scene" |
| [0x42550c] | camera, 0xc8 bytes "camera" |
| [0x425488] | object, 0x10c + 130 × 0x40 + 256 × 0x34 bytes "logo scene object" |
| [0x425518] | light, 0xd4 bytes |
| [0x425534] | bitmap "logo text" (inside.raw), [0x425514] bitmap "logo mask" (inmask.raw) |
| 0x425018 | the step matrix S (3 × 3 doubles, row-major) |
| 0x425190 | the object's rotation matrix R (3 × 3 doubles), copied into the object every frame |
| 0x4254b0 | the camera's matrix (identity) |
| 0x425178 | camera position (3 doubles): (0, 0, 100) |
| 0x425060 | object position (3 doubles): (0, 0, 0) |
| [0x42553c] | text flag (0; 1 from 4:32) |
| [0x425500] | the logo's memory tag (freed at 5:63 by 0x403184) |

### 3.2 logo/obuke.sro (INSIDE.DAT offset 0x30733, 3,640 bytes), as the loader 0x410bb0 reads it

| offset | contents |
|---|---|
| 0x00 | dword 0x001df020 (magic) |
| 0x04 | dword 0x20: offset of the vertices |
| 0x08 | dword 130: vertices |
| 0x0c | dword 0x638: offset of the faces (= 0x20 + 130 × 12; the loader just continues after the vertices) |
| 0x10 | dword 256: faces |
| 0x14 | six int16 −503, −512, −494, 503, 512, 503 (a bounding box; not read) |
| 0x20 | 130 vertices of 12 bytes: int16 x, y, z; uint16 u, v; uint16 0 |
| 0x638 | 256 faces of 8 bytes: uint16 0; uint16 a, b, c (vertex indices) |

The loader multiplies the coordinates by its scale (EBX = 0x10000 → × 1/65536 · 65536 = 1.0) and truncates them to
ints; it keeps u' = u >> 4 and v' = −(v >> 4) as 16-bit values (12-bit texture coordinates). The object is a sphere
of radius ~512 (112 vertices at 500..517 from the centre) with 18 vertices pulled in to 218..246 (vertices 7, 12,
28, 42, 47, 54, 56, 62, 65, 75, 81, 83, 91, 95, 104, 108, 111, 126): seen from inside, those make the spikes
pointing at the viewer. The faces are wound so that the normals point inward.

Normals (engine, at load; checked exactly): face normal = trunc(1024 × unit(cross(b − a, c − a))); vertex normal =
trunc(819.2 × unit(sum of the integer face normals of the faces using it)).

### 3.3 Init 0x402f9c, 0x402ca0, 0x402d0c (in the precalc)

```c
tag = new_tag();
rotation(0x425018, -0.01, 0.0085, 0.012345);     // 0x4145f4: S = Rx(-0.01) Ry(0.0085) Rz(0.012345) (3.4)
load files (3.1);
material = alloc(0x44);  scene = alloc(0x50);
scene_defaults(scene);                            // 0x410878: ambient +0x40 = 0, +0x3c = 256, +0x44 = 0x2000,
                                                  //   +0x48 = 0x400000, render stages +0 = 0x4229c0
viewport(scene, 320, 200, 0, 0, 320, 200);        // 0x410618: +0x20 width, +0x24 height, window +0x28..+0x34
text = make_bitmap(inside.raw, 249, 249, 139); text->type = 3;            // copy, 0 transparent
mask = make_bitmap(inmask.raw, 253, 253, 147); mask->table = inmask.shd; mask->type = 4;   // shade by level
scene->buffer = draw target; scene->fov = 42 (+0x3c); scene->near = 0x3000 (+0x44);
material(material, texture twlogo.raw, 0, shade table logo.shd, 0, umask 0xff, vmask 0xff, log2 w 8, log2 h 8);
// 0x402ca0
camera = alloc(0xc8); camera_init(camera); add(scene, camera); scene->camera (+0x38) = camera;
vec3(0x425178, 0.0, 0.0, 100.0);                  // camera position
rotation(0x4254b0, 0, 0, 0);                      // camera orientation: identity
// 0x402d0c
object = alloc(0x10c + nv * 64 + nf * 52); object_init(object);
load_sro(object, obuke.sro, scale 1.0, material); // 0x410bb0
set_face_routine(object, 0x414e5c);               // every face drawn by the textured Gouraud rasteriser
add(scene, object); object->flags |= 0x40;        // receives point lights
vec3(0x425060, 0, 0, 0);                          // object position
rotation(0x425190, 0, 0, 0);                      // R = identity
light = alloc(0xd4); object_init(light);
light->range (+0xc8) = 550; light->intensity (+0xcc) = 1600; light->flags |= 0x80;   // point light
add(scene, light);                                // position (0, 0, 0): never set
text_flag = 0;
```

### 3.4 Script and time base

- 3:63, 0x403164 (refresh 1446): set_palette(logo.pal) (0x400364; the DAC equals logo.pal from refresh 1446 on),
  tick routine 0x402e30, frame routine 0x402f1c.
- 4:32, 0x402f10 (refresh 1627): text_flag = 1 (with the white flash 0x400498 of the same entry).
- 5:63, 0x403184 (refresh 2148): routines off, frees the logo's memory.

Tick routine 0x402e30, once per refresh: R = R × S (0x414070: R[i][j] = Σk R[i][k] S[k][j], then copied back),
so R = S^k after k tick calls; the first runs at the retrace before record 1446, so a frame started after the
retrace before record r (and before the next one) uses S^(r − 1445) (checked in all 768 frames: the matrix is S^k
with k = the tick calls so far, never a torn copy). The part tick counter is not used (it is 4k + 3 at the
ticks).

S = Rx(−0.01) · Ry(0.0085) · Rz(0.012345) with Rx(a) = [[1,0,0],[0,c,−s],[0,s,c]], Ry(a) = [[c,0,s],[0,1,0],
[−s,0,c]], Rz(a) = [[c,−s,0],[s,c,0],[0,0,1]] (checked against the matrix at 0x425018 to 3e−16). A vertex turns as
p = R v. S is a rotation by θ = 0.0179887996 rad (1.0307°) per refresh about the unit axis
(−0.5529745, 0.4759376, 0.6838879): 1.2608 rad/s, one turn in 349.28 refreshes (4.984 s). Closed form for the
remake: R(t) = rotation about that axis by θ · n(t), n(t) = refreshes since the retrace that starts refresh 1445,
one refresh before the part's first (continuous; the original steps once per refresh, at the retraces). Nothing
else in the logo moves; nothing depends on the frame rate.

---------------------------------------------------------------------------------------------------------------

## 4. Logo: a frame (0x402f1c)

```c
scene->buffer = draw target;
copy_positions();        // 0x402e44: camera position = trunc(0x425178..) as ints (0, 0, 100), camera matrix =
                         //   0x4254b0; object position = trunc(0x425060..) (0, 0, 0), object matrix = R (0x425190)
clear(scene, 0);         // 0x41127c: the viewport (all 320 x 200) to index 0 (black)
render(scene);           // 0x40c6ac: the engine's stages (4.1..4.5)
draw_bitmap(mask, 31.0f, 9.0f);                  // 0x415df0 type 4 (4.6)
if (text_flag) draw_bitmap(text, 34.0f, 13.0f);  // type 3
debug_overlay(); flip();
```

**The whole frame is overwritten** (cleared to 0 first). Pixels no face covers stay index 0 = black (logo.pal 0 =
(0, 0, 0)): the black wedges at the edges come from the near plane (4.1, 4.2).

What goes into the engine and what comes out (all checked against the engine's own records in every frame):

### 4.1 Transform and projection

Camera at (0, 0, 100), identity orientation, looking along +z; screen x to the right, y down.

```c
p = R * v - (0, 0, 100);                        // camera space, world units
zc = trunc(128 * p.z);                          // the engine keeps z x 128 as an int (vertex +0x1c)
sx = trunc(16 * 160 + 16 * 42 * p.x / p.z);     // 28.4 fixed point (vertex +0x14)
sy = trunc(16 * 100 + 16 * 35 * p.y / p.z);     // (vertex +0x18)
```

The scale 42 is the scene's +0x3c: horizontal focal length 320 · 42 / 320 = 42 pixels, vertical 200 · 42 / 240 = 35
(so pixels come out with the 4:3 aspect). That is a very wide view: 150.6° horizontally, 141.4° vertically. Only
vertices of front-facing faces are transformed.

Outcodes per vertex: 0x10000 sx < 0, 0x20000 sx ≥ 5120, 0x40000 sy < 0, 0x80000 sy ≥ 3200; 0x100000 (alone) when
zc < 12288 = scene +0x44, i.e. **near plane z = 96** world units.

### 4.2 Faces

1. Back faces (0x41a458, in object space): the camera in object space c = Rᵀ (0, 0, 100); a face is drawn when
   (c − a) · n ≥ 0 (a its first vertex, n its integer face normal). From inside the sphere almost every face faces
   the viewer; 0..3 faces on the flanks of the spikes are dropped per frame.
2. Clip test (0x41a9d4): a face is dropped when its three vertices share an outcode bit (all left, all right, all
   above, all below, or all behind the near plane).
3. Sort key = max(0, trunc(trunc((zc_a + zc_b + zc_c) × (1/3)) / 16)); 0x40c384 sorts with three 8-bit radix
   passes and the faces are drawn with the largest key first (painter's order). Ties keep the order the radix
   passes leave (`radix_order` in `logo_model.py` reproduces the drawn order in all 768 frames). A depth buffer
   gives the same picture (no two faces overlap from this viewpoint; 0 pixels differ in the frames tried).
4. A face with a vertex behind the near plane is clipped at z = 96 (0x41be3c: the attributes are interpolated
   along the edges in camera space), giving one or two triangles. Toward the screen edges the view rays are up to
   78° off the axis and the sphere's surface there is near or inside z = 96, so the clipped faces leave the black
   wedges (index 0 from the clear).

### 4.3 Light (0x41aba4 → 0x41b508), per vertex

Ambient 0 (scene +0x40) plus the point light at the object's centre (its position in object space is Rᵀ (light −
object) = 0), for each vertex v with its normal n (length 819.2):

```c
d = |0 - v|;  dot = (0 - v) . n;
if (d <= 550 && dot >= 0)
    light = trunc(0 + (d < 1 ? 1600 : 1600 - d * (1600.0 / 550)) * dot / (d * 512));
light = min(light, 992);                        // vertex +0x30
```

Because the light is at the centre the object turns about, the result is the same in every frame: the outer
vertices get 134..196, the 18 inner spike vertices saturate at 992 (checked for every vertex). As shade level (4.4):
8.4..12.3 outside (≈ 26..38 % of the texture's colour) and 62.06 at the spike tips (about twice the colour, towards
white). Gouraud interpolation between them makes the radial streaks.

### 4.4 Rasteriser 0x414e5c

Bits 10..12 of the face's +0x30 flags select the perspective-correction span; they are 0 for this object (the
flags are 2 or 0x100002), so **u, v and the light are interpolated linearly in screen space** (affine). Per
vertex the 16.16 values are u = (u' + 1) / 16 texels, v = (v' + 1) / 16 texels, level = (light + 1) / 16 (each ×
65536); per pixel:

```c
texel = twlogo[((v >> 16) & 255) * 256 + ((u >> 16) & 255)];   // wraps (material masks 0xff, 256 x 256)
dst   = logo_shd[((level >> 16) & 255) * 256 + texel];
```

(the light accumulator's bits 16..23 select the row). Sub-pixel rules and the exact gradient set-up are the
engine's (notes-engine.md); the model with pixel centres at +0.5 and exact float interpolation matches 80..86 % of
the pixels, the rest one level or one texel off.

### 4.5 logo.shd

Row 32 is the identity. Rows below 32 darken (row L ≈ the nearest palette colour to colour × L / 32), rows above
32 brighten towards white with clamping (row L ≈ nearest to min(colour × L / 32, 63)): the scaled-colour fit
matches 67 % of rows 0..32 exactly (`shades2.py`), mean colour error 1.5..2 of 63 above 32. So level 62 ≈ colour ×
1.94 clamped.

### 4.6 The 2D overlays (checked exactly in all 130 dumped frames)

- Mask, bitmap type 4 (0x416324) at (trunc(31.0), trunc(9.0)): for each of the 253 × 147 pixels with m =
  inmask.raw ≠ 0: dst = inmask.shd[m × 256 + dst]. inmask.shd row m is exactly logo.shd row 30 − 2m, so the
  mask multiplies the colour by about (15 − m) / 16: m = 15 is black, m = 1 keeps 87.5 %. It is the lettering's
  shape in grey with a dark outline; before 4:32 it shows as a dark see-through ghost of the logo, after 4:32 its
  outline (3 pixels left, 4 above, 1 right, 4 below the text) is the text's black border.
- Text, type 3 (0x4164ec) at (34, 13), from 4:32: dst = src for every inside.raw pixel ≠ 0.

Both are clipped to the window (they fit inside 320 × 200).

### 4.7 Shortcuts a high-resolution remake should evaluate exactly

- 28.4 screen coordinates, z × 128 integers, integer normals and integer light → floats.
- Affine interpolation of u, v and light → perspective-correct (it changes 33..39 % of the pixels by a level or a
  texel in the original's resolution, the composition stays the same; `logo_compare.py` shows them side by side).
  Keep the light per vertex with Gouraud interpolation: per-pixel evaluation of 4.3 would remove the streaks.
- 64 shade levels → a continuous level; nearest texture reads → filtered: shade each texel's index at the
  continuous level (interpolating logo.shd's rows) and filter the colours, or take texture colour × level / 32
  clamped per channel to 1 (logo.shd's rows are the nearest palette colours to that, 4.5).
- The mask's 16 levels → a continuous factor (15 − m) / 16; the overlays at integer positions → scaled.
- Painter's order → a depth buffer (same result).
- The rotation stepped once per refresh → continuous angle.
- Keep: the near plane at z = 96 (the black wedges are part of the look), the camera inside the sphere, the 150°
  view.
- The first logo refresh shows the intro's last frame through logo.pal (an 8-bit palette artefact lasting 1/70 s);
  the remake can show black or the first logo frame there.

---------------------------------------------------------------------------------------------------------------

## 5. Time bases (summary)

| part | variable | time base |
|---|---|---|
| intro | X, Y | per refresh, T = 4k + 2 (k = refreshes since refresh 38), random resets |
| intro | drops, count, deposits, blur | per drawn frame (0.6925 per refresh in the capture) — frame-rate dependent |
| intro | grow flag, the 130 random drops | script 3:00 |
| intro | palette fade-in | per refresh, 120 refreshes from refresh 38 |
| logo | R | per refresh: S^(refreshes since refresh 1445); continuous form θ n(t) about a fixed axis |
| logo | text flag | script 4:32 |
| logo | everything else (light, mask, camera) | static |
| both | flashes | script (core palette routines) |

## 6. Pictures

### 6.1 intro/intro.raw (INSIDE.DAT offset 0x4300, 64,000 bytes, 320 × 200, intro.pal)

A **digitised photo** collage (by Destop or Santtu; not computed): a nude woman's torso and legs, kneeling, on the
left, a tube of "AMAZING GOOP" glue (1 FL.OZ. (29.7 ml), MADE IN U.S.A.; PARTS.md reads it as "Amazing Good") on
the right, a swirled grey-blue and red marbled background. It is a photo of a real
person, so it is **not redrawn**; it is also read through a shade table (2.4) by palette index. Shown full screen at
(0, 0), 1:1, index 0 transparent (black in practice).

### 6.2 logo/twlogo.raw (offset 0x13d00, 65,536 bytes, 256 × 256, logo.pal)

A hand-made abstract texture (by the graphics artists, not computed): smeared, marbled reds and pinks with yellow and
white highlights and a few spirals, like the lettering's colours twirled. Mapped on the 3D object (u, v from the
.sro, wrapping) and read through logo.shd by index: it stays the original's (no redraw); the remake filters its
shaded colours.

### 6.3 logo/inside.raw (offset 0x28000, 34,611 bytes, 249 × 139, no header, logo.pal)

**Hand-drawn** lettering (by Destop or Santtu; painted or rendered and retouched, not computed): twisted, swirled
3D letters with a chrome-like rim and a colourful organic texture (pink, orange, yellow, green), in two interlocked
rows on a black (index 0, transparent) background. It reads as the group's and the demo's names, probably "CNCD"
above and "INSIDE" below (a guess: the letters are strongly distorted). Drawn as it is at (34, 13) without scaling or blending: **a redraw can replace it** (same position and
size 249 × 139 at 320 × 200, transparent outside the letters).

### 6.4 logo/inmask.raw (offset 0x3156b, 37,191 bytes, 253 × 147, values 0..15)

The lettering's shadow and outline as a 16-level map (drawn by the artist from the lettering: grey letters with a
dark outline, 0 = untouched). It is read as shade levels (4.6), so it stays a level map; if inside.raw is redrawn,
the mask has to follow the redraw's outline (an upscaled level map of the same shape: a darkening alpha with an
outline about 3..4 original pixels wide around the letters).

### 6.5 Palettes and tables

intro.pal (offset 0x4000), intro.shd (offset 0, 16,384), logo.pal (0x27d00), logo.shd (0x23d00, 16,384),
inmask.shd (0x3a6b2, 4,096 = logo.shd rows 30, 28, .., 0) are read at run time.

## 7. Models (`models/intro-logo/`, README.md there)

- `run_inside.py` (a copy of the shared one with two additions: `--hooks FILE` installs hooks, and a resumed run
  starts its refresh clock at the snapshot's refresh; the shared copy starts it at 0, so a resumed run first writes
  that many records without running the program, and the `--load` example in BRIEF.md labels its records too
  high),
  `hooks_intro.py`, `hooks_logo.py`: the instrumented runs.
- `intro_model.py`: ticks, drops, blur, shade; `--chain` replays the whole intro. `intro_start.py`: the start state
  and seed. `intro_schedule.py`: the frame schedule.
- `logo_model.py`: S, normals, light, transform, back faces, outcodes, sort, near clipping, an affine rasteriser;
  `--geom` checks every frame's geometry. `logo_overlay.py`: mask and text. `logo_light.py`, `logo_normals.py`:
  the light and the normals. `logo_compare.py`: affine vs perspective vs depth buffer.
- `sro.py` reads the .sro; `pics.py` writes the pictures and tables as PNGs; `shades.py`, `shades2.py`: the shade
  tables against the palettes; `palette_check.py`: the DACs in the capture.

## 8. Not determined

- The engine's exact fixed-point rasterisation (sub-pixel rules, gradient rounding): 14..20 % of the logo's 3D
  pixels differ by a level or a texel in the model (notes-engine.md).
- Who drew which picture (the README credits graphics to Destop and additional graphics to Santtu), and the exact
  words of the lettering (6.3, a guess).
- How the 63 rand() calls between srand(0x45eafc) and the intro's first frame split between the later parts' inits
  (only their number is measured; the remake can start the intro from the state 0xf83d63af).
