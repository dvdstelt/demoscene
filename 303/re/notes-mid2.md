# notes-mid2: 0x2e8e4 middle sub-parts (typo / blobs / wireblobs), 0x2ef72 .. 0x2f6ce

Scope: the three sub-parts of the big mid-section routine 0x2e8e4 that run between the facebox loop
(ends 0x2ef62) and the first fish.fad sub-part (0x2f6c9), plus every routine they call.
Times are given as **music seconds** (pos.py) and, in brackets, as capture video time using the
coordinator's visual relation `video = 1.00643*music - 1.953`.
"tick" = one timer callback round (vertical retrace, ~70 Hz). Watcom `rand()` = 0x34974 (LCG, 15 bit).
`sin`/`cos` = 0x32b72 / 0x32b68 (x87 fsin/fcos with range reduction). 0x3235e = __CHP (float->int truncation).

Cross-check images (my numpy re-implementation of the floor mapper + 0x1c490 compositing, for orientation only):
`<scratch>/re/m2/sim_blobfloor.png` (tex3d floor),
`.../re/m2/sim_wireblob.png` (wire mode on bd2 frame 2), `.../re/m2/sim_typo.png` (wire mode on bd2 frame 0, no face, no strobe);
script `.../re/m2/sim.py`, decoded assets in `.../re/m2/assets/`, textures `.../re/m2/tex3d_all.png`, `bd2_0..2.png`, `wire.png`.

---------------------------------------------------------------------------------------------------
## 0. Sub-parts and timing

| what | condition (pos = [0x9b844]) | music s | video s |
|---|---|---|---|
| typo sub-part starts (loop 0x2efd4), [0x9b840] = (pos+0x20)&~0x3f = 0x940 | facebox ends pos >= 0x940 | 120.80 | (119.63) |
| wire mode requested ([0x1499e8]=1), applied on the next tick: white flash, bd2 frame 0 | pos >= 0x9a1 | 125.94 | (124.80) |
| first palette strobe kick (then every 4 half-rows = 2 rows = 0.210 s) | pos = 0x9a4, 0x9a8, ... 0x9fc | 126.05 .. 130.66 | |
| bd2 frame 0 -> 1 | pos = 0x9c0 | 127.52 | (126.38) |
| bd2 frame 1 -> 0 | pos = 0x9e0 | 129.19 | (128.07) |
| **typo ends** -> sub-part 2 (blobs + face sheet), wire off -> white flash | pos >= 0xa00 | **130.87** | **(129.76)** |
| **sub-part 2 ends** ([0x9b840] = exact exit pos of typo, not rounded, +0x30) | pos >= ~0xa30 | **133.39** | **(132.29)** |
| sub-part 3 (blobs only), [0x9b840] = 0xa40 | | 133.39 | |
| wire mode requested (next tick: flash, palette bd2, bd2 frame 2 composite) | pos >= 0xb40 | 147.65 | (146.65) |
| **sub-part 3 ends** -> fish.fad sub-part (0x2f6c9) | pos >= 0xb80 | **151.01** | **(150.03)** |

Mapping to remake scenes (timeline.cpp:20-23, which use video time through V()):
- remake "facebox" V(120.0)=121.24 .. V(124.9)=126.14 music  = typo sub-part, non-wire half (120.80 .. 125.94).
- remake "typo" 126.14 .. V(129.95)=131.18                   = typo sub-part, wire half (125.94 .. 130.87).
- remake "blobs" 131.18 .. V(146.5)=147.71                   = sub-part 2 (130.87 .. 133.39) + sub-part 3 non-wire (133.39 .. 147.65).
- remake "wireblobs" 147.71 .. V(149.9)=151.11               = sub-part 3 wire half (147.65 .. 151.01).
All remake boundaries are 0.05-0.45 s late in music time (facebox start worst: 0.44 s).

Everything animated here is tick-based (callbacks) or position-based; nothing advances per rendered frame
(the camera integrator 0x27444 loops over the pending tick count). Port everything as functions of time.

---------------------------------------------------------------------------------------------------
## 1. Global state used (addresses)

```
camera matrix M      0xa0120  3x3 row-major floats; R0=(m00,m01,m02) screen-right, R1 screen-DOWN, R2 forward
camera position P    0xa018c x, 0xa0190 y, 0xa0194 z
orbit base Bc        0xc1d34/38/3c        (also the blob cluster centre * 4)
look-at target T     0xc1d40/44/48
path source S        0xc1d4c/50/54        (written by 0x278a4 / 0x27804)
spring velocity V    0xc1d58/5c/60
pending ticks        0xc1d64   (++ per tick by 0x2764c, consumed by 0x27444)
roll                 0xc1d68   (0 during these sub-parts; set to 0 at 0x2eb07, only written again later)
tau                  0xc1d6c   (float, += 4.0 per tick by 0x2764c since the 0x2e8e4 setup; never reset)
damp                 0x57d7c   (float, data 0.79)        turn rate  0x57d80 (float, data 0.05)
saved damp/turn      0x1493e0 / 0x1493e4
n                    0x57d78   (int, data 200; +2/tick from facebox start (0x23384), +1/tick from sub-part 2 (0x2685c))
wire_mode            0x57d74   (data 2; = wire_request after the next tick, see 0x2e684)
wire_request         0x1499e8
bd2 frame            0x1499fc  (0..2)       bd2 frames 0x1493b0 (3*64000)   wirecut copy 0x1499f8 (64K)
wire.raw texture     0x1499f4 (256x256)     wire.fad copy 0x1499ec          3d.fad copy 0x1499f0
bd2 palette          0x1493e8 (768; wire.raw's palette was loaded there first and is overwritten by bd2.raw's)
3d palette           0x1496e8 (= tex3d.raw palette, copied from 0x5faa8 at setup)
tex3d textures       0xc1d1c..0xc1d30 (#0..#5)
fade table buffer    [0x5b67c] (current 64-level .fad), ghost table [0x5b678]
palette brightness   0x52920 (float) ; palette level 0x52918 -> target 0x5291c at speed 0x8eba8/tick ; 0x8ebb0 = flash-to-negative
```

---------------------------------------------------------------------------------------------------
## 2. Control flow (0x2e8e4, relevant setup + 0x2ef72..0x2f6ce)

```c
// ---- setup at 0x2e8e4 entry (~end of rings), only what these sub-parts use ----
wire_tex = alloc64k();                                   // [0x1499f4]
load("wire.raw"): 768 pal -> 0x1493e8, 65536 px -> wire_tex
/* inlined bd2(INIT) */
wirecut = alloc64k();                                    // [0x1499f8]
bd2_frames = alloc(0x2ee00);                             // [0x1493b0]
load("bd2.raw"): 768 pal -> 0x1493e8 (overwrites), 3 x 64000 px -> bd2_frames
add_timer(0x2e534);
load_fad("wirecut.fad"); memcpy(wirecut, [0x5b67c], 0x10000);   // .fad = 64 levels x 256 = 16 KB
load_fad("wire.fad");    memcpy([0x1499ec], [0x5b67c], 0x10000);
0x2b110(0x17); 0x2bdb4(0x17);
0x26f04();          // tex3d.raw: palette -> 0x5faa8, 6 textures
0x268bc();          // blob init (also: M = identity, P = (0,0,-70))
0x27670(0x17);      // Bc = (-1000,0,60), pending = 0, add_timer(0x2764c)   <- tau starts here
0x28100(0x17);
0x29ae8(0x17);      // 3 warp poles, add_timer(0x29aac)
0x23540(0x17);      // 0x2338c: faceo.dat + phong1.raw; M = identity, P = (0,0,-290), push = 1000
load_fad("3d.fad"); load_gst("3d.gst"); memcpy(0x1496e8, 0x5faa8, 0x300); memcpy([0x1499f0], [0x5b67c], 0x10000);
add_timer(0x2e684);
... face sub-parts (0x2eb2a, 0x2ece7), facebox 0x2ee57 (registers 0x23384 via 0x23540(0x2b)) ...

// ---- typo sub-part, 0x2ef72 ----
flash_negative /*0x8ebb0*/ = 0;
remove_timer(0x2e798);
saved_damp = damp;  saved_turn = turn;        // 0.79, 0.05
damp = 0.6f;  turn = turn * 0.2f;             // 0.01
pending = 0;  bd2_frame = 0;
part = (pos + 0x20) & ~0x3f;                  // 0x940
do {
    cam_path_typo(230.0f);                    // 0x278a4
    cam_update(3);                            // 0x27444
    world_render(B0 = 0x5fda8);               // 0x27670: floor + ceiling
    warp_render (B1 = 0x6f7a8);               // 0x29ae8: stripe warp, used as face texture
    face_render (B0);                         // 0x23540: face relief sheet
    if (wire_mode) {                          // 0x2f006
        /* inlined bd2(B1): its INIT branch (the "bd2.raw reload" at 0x2f02e) and FREE branch are
           dead code: they compare the constants 0x17 / 0x48 with the buffer address 0x6f7a8 */
        memcpy(B1, bd2_frames + bd2_frame*64000, 64000);
        swap [0x5b67c] := wirecut;  combine(B0 -> B1) /*0x1c490*/;  restore [0x5b67c];
        show(B1);
    } else show(B0);
    wire_request = (part + 0x60 < pos);       // pos >= 0x9a1
} while (part + 0xc0 > pos);                  // until pos >= 0xa00

// ---- inlined bd2(FREE) + sub-part 2, 0x2f237 ----
wire_request = 0;                             // next tick 0x2e684 switches back (3d palette/fad, white flash)
remove_timer(0x2e534);  free(wirecut);        // NB: wirecut pointer is still used in sub-part 3 (use after free;
                                              //     nothing allocates in between, so the table is intact)
blobs(0x2b);                                  // 0x26c14: add_timer(0x2685c)
Bc = T = (-1000, 0, 60);  S = (-1000, 0, -10);  pending = 0;
part = pos;                                   // not rounded
add_timer(0x2e438);                           // turn-rate ramp
do {
    cam_path_orbit(130.0f);                   // 0x27804
    cam_update(3);
    world_render(B0);
    blobs_render(B0);                         // 0x26c14
    warp_render(B1);
    face_render(B0);                          // face sheet drawn after (over) the blobs, no common z-sort
    show(B0);
} while (part + 0x30 > pos);                  // ~0xa30 (2.5 s)

// ---- sub-part 3, 0x2f406 ----
part = (pos + 0x20) & ~0x3f;                  // 0xa40
do {
    cam_path_orbit(130.0f); cam_update(3); world_render(B0); blobs_render(B0);
    if (wire_mode) {
        bd2_frame = 2;                        // inlined bd2(B1) again, INIT/FREE dead (compare order swapped)
        memcpy(B1, bd2_frames + 2*64000, 64000);
        swap [0x5b67c] := wirecut (freed block); combine(B0 -> B1); restore;
        show(B1);
    } else show(B0);
    if (part + 0x100 <= pos) wire_request = 1; // pos >= 0xb40
} while (part + 0x140 > pos);                  // pos >= 0xb80
remove_timer(0x2e438);  wire_request = 0;
memcpy(stack, timer_list 0x8ec04, 0x40);  remove_timer(0) /* all */;
turn *= 2.0f;                                  // ~0.1 for the following sub-parts
// 0x2f6c9: 0x2b908(0x17) ... fish.fad sub-part (not mine)
```
show() = 0x1bbec plain 64000-byte copy to [0x52914] (no retrace wait), then 0x332a4/0x1259c (music polling).

---------------------------------------------------------------------------------------------------
## 3. Timer callbacks active here

Order inside one tick (0x1bc01): 0x133b0 (music pos), **0x10124 palette tick**, then the registered callbacks.

```c
void tick_10124(void) {                 // palette (context; described by others)
    level moves toward target by speed; if equal speed = 0;
    bright = (bright - 1.0f) * 0.98 + 1.0f;                // [0x52920]
    set_palette(level >> 8);   // 0x40: out = min(63, trunc(p*bright));
                               // >0x40: k = (level>>8)-0x41, out = min(63, trunc((k + (p*(63-k)>>6)) * bright))  (to white)
}

void cb_2e534(void) {                   // bd2 strobe + frame toggle; registered from setup until typo end
    if (wire_mode) {
        if (last_pos /*0x580a0*/ != pos) {
            last_pos = pos;
            if ((pos & 3) == 0)    bright = rand() * 0.000125 + 0.5;    // 0.5 .. 4.596
            if ((pos & 0x1f) == 0 && bd2_frame < 2) bd2_frame ^= 1;
        }
        bright = (bright - 1.0f) * 0.9575 + 1.0f;
    } else bright = 1.0f;     // NB: holds bright at 1.0 in every non-wire frame while registered
}
// net: the value displayed one tick after a kick r is 1 + 0.9575*0.98*(r-1); afterwards (b-1) *= 0.93835 per tick.
// After typo (callback removed) only the 0.98/tick decay of 0x10124 remains -> a kick at 0x9fc can still
// brighten the first ~0.5 s of sub-part 2.

void cb_2e684(void) {                   // wire-mode switch (registered at face start, active throughout)
    if (wire_request != wire_mode) {
        wire_mode = wire_request;
        pal_level = 0x8000; pal_target = 0x4000; pal_speed = 1000;   // white -> image, linear, 16.4 ticks = 0.23 s
        if (wire_mode) { memcpy(0x5faa8, bd2_pal 0x1493e8, 768); memcpy([0x5b67c], wire.fad,  0x10000); }
        else           { memcpy(0x5faa8, pal3d 0x1496e8,  768); memcpy([0x5b67c], 3d.fad,    0x10000); }
    }
}   // flash_negative is 0 in all three sub-parts -> flashes go to white, not to the negative

void cb_2e438(void) {                   // sub-parts 2-3
    if (turn < saved_turn) turn += 0.0004f;          // 0.01 -> 0.05 in 100 ticks (1.43 s)
    if (saved_damp < saved_damp) damp += 0.0004f;    // original bug: compares 0x1493e0 with itself, never runs
}                                                     // -> damp stays 0.6

void cb_2764c(void) { tau += 4.0f; pending++; }      // from setup
void cb_23384(void) { n += 2; }                      // from facebox start (0x2ee43); never removed before 0x2f6a2
void cb_2685c(void) {                                // from sub-part 2
    n += 1;  [0xa14d8]++;
    for (k = 0; k < count; k++) if (ball[k].d > 0) ball[k].d *= 0.99;   // double constant
}
void cb_29aac(void) { for k<3 { pole[k].a += pole[k].da; pole[k].b += pole[k].db; }  scroll++; }  // [0x147394]
```

---------------------------------------------------------------------------------------------------
## 4. Camera

### 4.1 Paths (write S; per frame from tau, which is tick time)
```c
void cam_path_typo(float R /*230*/) {           // 0x278a4
    float r = R + R/6 * sin(tau*0.0005f + 64);
    float A = 4*tau/1100, Bang = 4*tau/700 + 34;  // = tau/275, tau/175 + 34
    S.x = 2*r*sin(Bang)*cos(A);
    S.y =   r*sin(Bang)*sin(A);
    S.z =   r*cos(Bang) - 700;
}
void cam_path_orbit(float R /*130*/) {          // 0x27804
    float r = R + R/6 * sin(tau*0.0005f + 64);
    float A = tau/1100, Bang = tau/700 + 34;     // NB no factor 4 here
    S = Bc + r * vec3(sin(Bang)*cos(A), sin(Bang)*sin(A), cos(Bang));     // Bc = (-1000, 0, 60)
}
```
tau += 280/s: typo A 1.018 rad/s, B 1.6 rad/s; orbit A 0.2545 rad/s, B 0.4 rad/s; r period 45 s
(typo r in [191.7, 268.3], orbit r in [108.3, 151.7]).
tau's origin is the 0x27670(0x17) call during the 0x2e8e4 setup (after loading wire.raw, bd2.raw, two .fad,
tex3d.raw), i.e. roughly the start of the face section plus load time; tau at typo start is about
280*(120.8 - t_setup) and cannot be fixed statically - take the phase from the capture.

### 4.2 Integrator 0x27444(flags) — runs once per pending tick
```c
while (pending) {
    vec3 d = T - P;  float len = sqrt(dot(d,d));  pending--;
    if (len > 0.01) {
        d *= turn / len;                                  // |d| = turn ([0x57d80])
        vec3 ax = cross(d, M.R2);                         // (d.y*m22-d.z*m21, d.z*m20-d.x*m22, d.x*m21-d.y*m20)
        if (!(flags & 1)) ax = 0;
        mat3 Rot = axis_angle(ax);                        // 0x2721c
        mat3 N = M * Rot;                                 // 0x18b18 (row-major product): each row rotated by Rot^T
        if (roll == 0) M = N; else M = N * axis_angle(0,0,roll);
    }
    int nc = (flags & 2) ? 3 : 2;                         // flags = 3 here: x,y,z
    for (i < nc) { V[i] = V[i]*damp + (S[i] - P[i])*0.002; P[i] += V[i]; }
}
mat3 axis_angle(vec3 a) {                                 // 0x2721c(dst, ax, ay, az), ret 0xc
    float L = |a|;  if (L < 1e-8) return I;
    float th = asin(L);  n = a/L;  c = cos th, s = sin th;   // 0x34cf8 = asin
    return { c+nx*nx*(1-c),   nx*ny*(1-c)-nz*s, nx*nz*(1-c)+ny*s,
             nx*ny*(1-c)+nz*s, c+ny*ny*(1-c),   ny*nz*(1-c)-nx*s,
             nx*nz*(1-c)-ny*s, ny*nz*(1-c)+nx*s, c+nz*nz*(1-c) };     // standard Rodrigues
}
```
Behaviour: the forward row R2 turns toward T by asin(turn*sin(err)) per tick about the minimal axis (no
up-vector constraint, the camera may roll), i.e. an exponential look-at with rate turn per tick
(typo: 0.01/tick = time constant 1.43 s; sub-parts 2-3: ramps 0.01 -> 0.05 (0.29 s) over 1.43 s).
Position: second-order spring, poles 0.995 and 0.603 per tick: P follows S like a first-order lag with time
constant ~198 ticks = 2.8 s (typo path amplitudes are attenuated to ~1/3 (A) and ~1/5 (B) and lag ~70 deg;
orbit path to ~0.8 / 0.67).

### 4.3 State at the typo start
M = identity, P = (0,0,-290) (set by 0x2338c at setup; nothing touches them during the face sub-parts),
V = leftovers of the web part (small, decays x0.6/tick), T = value left by the web part (0x2cd4b):
(0, 0, 1000 + 0.01*q_web) — far ahead on +z. With P=(0,0,-290) and focal 300 the 320x200 face sheet at z=0
exactly fills the screen, so the typo sub-part begins as a seamless continuation of the full-screen face and
the camera then dollies back (2.8 s lag) toward the typo path around (0,0,-700), x up to +-2r, looking down +z.
In sub-part 2, S and T jump to the blob site: the camera turns left and flies ~1000 units toward x = -1000
(~3-6 s), passing the face sheet, which is still drawn until 133.39.

---------------------------------------------------------------------------------------------------
## 5. Corridor: 0x27670 -> 0x27014 + 0x1cddc

```c
void world_render(u8 *buf) {        // 0x27670; INIT: Bc=(-1000,0,60), pending=0, add_timer(0x2764c); FREE: remove it
    grid_floor(37.5f, 290.0f);       // 0x27014 (two stack floats, ret 8)
    map_lit(wire_mode ? wire_tex : tex3d[1], buf);   // 0x1cddc
}
void grid_floor(float D, float H) { // fills the 41x26 grid at 0x98648, 12-byte entries
    for (j = 0; j < 26; j++) for (i = 0; i < 41; i++) {
        vec3 d = D*M.R2 + (i - 20.5f)*M.R0 + (j - 13.0f)*M.R1;      // one grid step = 8 pixels, focal 300 px
        float t = (d.y != 0) ? (H - P.y) / d.y : 1.7e24f;           // plane y = +290 (screen-down side)
        if (t < 0) t = (-H - P.y) / d.y;                            // else plane y = -290
        g.row   = (int)((P.z + t*d.z) * 128);    // entry0, 8.8 texel row    = world z / 2
        g.col   = (int)((P.x + t*d.x) * 128);    // entry1, 8.8 texel column = world x / 2
        g.light = clamp((int)(307200.0f / t), 256, 8192);           // entry2, 8.8 fade level
    }
}
// 0x1cddc(tex, dst): for each 8x8 block, bilinear interpolation of (row, col, light) from the 4 grid corners,
// pixel = fad[light >> 8][ tex[(row>>8)&255][(col>>8)&255] ],  fad = [0x5b67c] (3d.fad, or wire.fad in wire mode)
```
- Two infinite planes at y = +-290 (no walls), both mapped identically: column <- world x, row <- world z,
  1 texel = 2 units, texture repeat 512 units. Normal mode texture = tex3d #1 (purple bevelled hexagons, palette
  tex3d / 3d.fad); wire mode texture = wire.raw (grey hexagon outlines, indices 0..31).
- Light: L = floor(45000/depth) clamped to [1,32] (depth = view-space z; 307200/t/256 = 1200/t, t = z/37.5).
  The .fad tables are standard: level L = colour*L/32 for L<32, identity at 32, toward white above (63 = white).
  So brightness = min(1, 1406/z) in 1/32 steps, never below 1/32 (no fog colour), affine within 8x8 blocks.
- Grid pixel (0,0) is at (-20.5, -13)*8 = (-164, -104) px from the axis while polygons use centre (160,100):
  the floor is offset 4 px right/down relative to objects (harmless).

---------------------------------------------------------------------------------------------------
## 6. Metaballs: 0x26c14 and helpers

### 6.1 Ball record (64 bytes at 0xa14e4 + 64*k, 16 initialised, at most 4 used)
`+00 pos.xyz (grid units) | +0c dir.xyz | +18 d | +1c f0,f1,f2 | +28 ph0,ph1,ph2 | +34 w`

### 6.2 Init 0x268bc (setup, and 0x26c14(0x17))
```c
for (m = 0; m < 256; m++) {            // face-crossing table 0xa18e4 (dword per corner mask)
    cross[m] = 0;
    for each (bit, faceMask) in {(1,0x0f),(2,0xf0),(4,0x33),(8,0xcc),(0x10,0x55),(0x20,0xaa)}
        if ((m & faceMask) != 0 && (m & faceMask) != faceMask) cross[m] |= bit;
    // also rewrites the case table at 0x532a4 (256 x 38 bytes: nEdges, edge[12], nTris, tri[3*8]) so that
    // triangle corners index the case's edge list instead of edge numbers
}
vtx = alloc(0x2673c0);  tri = alloc(0x75300);  env = tex3d[0];  count = 1;
for (k = 0; k < 16; k++) {             // rand() call order: a, b, f0, f1, f2, ph0, ph1, ph2
    a = rand()*PI/32768;  b = rand()*PI/65536;                       // a in [0,pi), b in [0,pi/2)
    ball[k].dir = (cos(a)*sin(b), sin(a)*sin(b), cos(b));            // y >= 0, z >= 0 hemisphere quadrant
    ball[k].d   = 50.0f;
    ball[k].f[i] = abs(rand())/250 + 48;                             // integers 48..179
    ball[k].ph[i] = rand();                                          // 0..32767 rad
    ball[k].w   = 1.0f;
}
M = rot(0,0) = identity;  P = (0, 0, -70);  count = 1;  [0xa14d8] = 0;
```
0x26c14(0x2b) registers 0x2685c, 0x26c14(0x48) and 0x26c08 remove it (0x26c08 is used later at 0x2fcfc).

### 6.3 Frame 0x26c14(buf)
```c
if (count < 4 && ball[count-1].d < 2.0f) count++;      // next ball starts when the previous has arrived
vec3 c = Bc * 0.25f;                                    // grid unit = 4 world units -> (-250, 0, 15)
for (k < count) {
    float amp = 3.8f * w / (1 + d);                     // 0.075 at d=50 .. 3.8 at d=0
    pos = c + d*dir + amp * vec3(sin(n/f0 + ph0), sin(n/f1 + ph1), sin(n/f2 + ph2));   // n = (float)(int)[0x57d78]
}
memset(hash 0xa1ce4, 0, 0x20000);  nv = nt = 0;         // counters shared with the face object (0xa12bc/0xa12c0)
for (k < count) {
    ix,iy,iz = trunc(pos[k]);
    while (field(ix+1, iy, iz) > 0.2f) ix++;            // walk +x to the surface
    polygonize(ix, iy, iz);                             // 0x25d00
}
project_and_bucket();  [0x5b674] = env;  draw_buckets(buf);   // 0x26524, 0x26784
float field(vec3 p) { float s = 0; for (k < count) s += w_k / (|p - pos_k|^2 + 1e-5); return s; }  // iso 0.2
```
Single ball surface radius sqrt(5) = 2.236 grid = 8.9 world units; 4 coincident balls 17.9 units.
Arrival: d = 50*0.99^ticks < 2 after 321 ticks = 4.58 s. Ball 0 exists from 130.87 (d starts decaying then),
ball 1 starts ~135.45, ball 2 ~140.03, ball 3 ~144.61 (arrives ~149.2); max 4 balls. They come from 200 world
units out along dir, then wobble around the centre with amplitude up to 15.2 world units; n advances 3/tick
(210/s) -> wobble rates 210/f = 1.17 .. 4.4 rad/s.

### 6.4 Polygoniser 0x25d00(x,y,z) (surface-following marching cubes, recursive)
```c
h = (x ^ (y<<4) ^ (z<<4)) & 0x1fff;                   // 16-byte slots {used,x,y,z}, linear probing
if (cell already in hash) return;  insert;
mask = 0;  for corner (dx,dy,dz): if (field(x+dx,y+dy,z+dz) > 0.2) mask |= 1 << (4*dx + 2*dy + dz);
emit_cell(x,y,z);                                      // 0x23a3c
if (cross[mask] & 1)    0x244ec(x-1,y,z);   if (cross[mask] & 2)    0x248f0(x+1,y,z);
if (cross[mask] & 4)    0x24cf0(x,y-1,z);   if (cross[mask] & 8)    0x250f4(x,y+1,z);
if (cross[mask] & 0x10) 0x254f8(x,y,z-1);   if (cross[mask] & 0x20) 0x25900(x,y,z+1);
// 0x244ec..0x25900 are per-direction copies of the same routine (not separately decoded)
```
### 6.5 Emit 0x23a3c / 0x236c4
```c
g[i] = field(corner i) - 0.2f   (stored 0xc1ce4..0xc1d00; corner i = (i>>2, (i>>1)&1, i&1))
for (e in case[mask].edges) {                          // 0x236c4: jump table of 12 edges
    off = edge endpoints: along the edge axis 4*g_a/(g_a - g_b) (unchanged if g_a == g_b), others 0 or 4;
    clamp each component to [0,4];                     // e.g. edge0 = corners 0-4: (4*g0/(g0-g4), 0, 0)
    v = 4*(x,y,z) + off;                               // world units
    N = sum_k (4*pos_k - v) / max(|4*pos_k - v|^3, 1e-5);   // weights ignored, points INWARD
    v.n = N / max(|N|, 1e-5);
}
append case[mask].tris (3 vertex pointers each); abort demo if nv > 30000 or nt > 10000
```
Vertices are not shared between cells.

### 6.6 Projection and sort 0x26524
```c
for each vertex: q = v - P;  z = dot(q, R2);  v.depth = z;
    if (z > 40) { s = 300/z;  v.sx = (int)(160 + s*dot(q,R0));  v.sy = (int)(100 + s*dot(q,R1));
                  v.eu = (int)(128 + 126*dot(n,R0));  v.ev = (int)(128 + 126*dot(n,R1)); }
for each tri with all three depths > 40:                 // otherwise dropped (no clipping)
    b = clamp((int)((z0+z1+z2 - 120) * (1/12.0)), 0, 99);   // 100 buckets of 4 units, depth 40..440
    push_front(bucket[b], tri);
```
### 6.7 Draw 0x26784
```c
for (b = 99; b >= 0; b--) { for tri in bucket[b]: wire_mode ? hidden_line_tri(tri, buf) /*0x23118*/
                                                            : tex_tri(tri, buf) /*0x1ee85*/;  bucket[b] = 0; }
```
- tex_tri 0x1ee85: affine (constant-gradient) texture mapper, no shading, texture [0x5b674] = tex3d #0
  (gold sphere env map). Texel = env[ev][eu] (row from n.R1, column from n.R0 — most likely; the packing
  goes through the filler's 8.8 edge tables). Because N points inward, the env map appears point-mirrored
  compared with an outward-normal sphere map.
- hidden_line_tri 0x23118: `if (0x1f88b(tri, buf)) { glow(v0,v1); glow(v2,v1); glow(v0,v2); }`
  0x1f88b rejects triangles fully off-screen and one winding (cross product >= 0, i.e. back faces) and FILLS
  the accepted triangle with index 0 (rep stosw of 0). glow = 0x2acf0: clip to x in [3,317], y in [3,197],
  DDA along the major axis; per step adds (palette-index addition, dword adds, carries may leak):
  y-major or x-major with slope >= 0: (x-1,y)+=2, (x-2,y)+=1, (x,y)+=1, (x-1,y-1)+=1, (x-1,y+1)+=1;
  x-major with negative slope: (x-1,y-1)+1; (x-2..x,y) +1,+2,+1; (x-2..x,y+1) +1,+2,+1; (x-1,y+2)+1.
  Back-to-front + black fill = hidden-line removal.

The face relief (0x23540 -> 0x23170) uses the same two paths: textured in normal mode, hidden-line wireframe
in wire mode (0x23353 tests wire_mode).

---------------------------------------------------------------------------------------------------
## 7. Face sheet 0x23540 and its texture 0x29ae8 (as used here)

### 7.1 0x29ae8 (into B1 = 0x6f7a8): stripe warp, the "flowers"
```c
// INIT: for k<3: a=rand(); b=rand(); da=(rand()-16384)*1.111e-6; db=(same); s=25600.0;  add_timer(0x29aac)
for (j = 0; j < 26; j++) for (i = 0; i < 41; i++) {      // grid at 0x98648, x = 8i, y = 8j
    float U = scroll * 256, Vv = 0;                        // scroll += 1 per tick
    for (k < 3) {
        float px = 160 + 120*sin(a_k), py = 100 + 80*sin(b_k);
        float dx = px - 8*i, dy = py - 8*j;
        float f = 100*s_k / (1 + dx*dx + dy*dy);           // = 2.56e6/(1+r^2)
        U += dx*f;  Vv += dy*f;
    }
    g.row = (int)Vv;  g.col = (int)U;                       // 8.8
}
0x1d0dd(tex3d[5], B1);                                      // unlit 8x8 bilinear grid mapper
```
tex3d #5 = vertical colour stripes, so only the column matters: stripes bent around three wandering poles
(displacement ~10000*dx/r^2 texels) and scrolling one texel per tick.

### 7.2 0x23540(buf): face relief (faceo.dat: 714 vertices, 1422 triangles; a 318x198 rectangle at z=0 with a
face relief down to z=-131 toward the camera)
```c
nv = saved_nv; nt = saved_nt;  [0x5b670] = phong1.raw;
split(B1): texL = columns 0..255, texR = columns 64..319 (0x1c3c4, 256x200 each); faces with v0.x<0 use texL
Mf = rot(0.2*cos(n*0.00694444 + 54), 0.3*sin(n*0.011627907));   // 0x14760(a,b): [[ca*cb, sa, -ca*sb],
                                                                  //  [-sa*cb, ca, sa*sb], [sb, 0, cb]]
push = max(0, 150*sin(n*0.005 + 1) + 50);
for each vertex: if (v.z < -1) { w = Mf*v; w.z = min(w.z + push, 0); } else w = (v.x, v.y, 0);  // rim pinned
    project like 6.6 (near reject z <= 40); if any vertex has sx outside [-320,640] the object is not drawn;
0x22e14 sort, 0x23170 draw (normal: textured + phong env 128+120*n, 0x1e223 - not decoded; wire: 0x23118)
```
n += 2/tick in typo, 3/tick afterwards: push period 9.0 s (6.0 s), rotation periods 3.9 s / 6.5 s.
The relief is drawn into B0 after the blobs: it always covers them (no common depth sort).

---------------------------------------------------------------------------------------------------
## 8. Wire mode compositing (the actual "typo" and "wireblobs" look)

```c
void combine(u8 *src /*B0, 3D render*/, u8 *dst /*B1, bd2 frame*/) {   // 0x1c490, table [0x5b67c] = wirecut.fad
    for (i < 64000) dst[i] = wirecut[32 + min(2*(src[i] & 0x7f), 31)][dst[i]];
}
```
wirecut.fad is a fade table for the bd2 palette: level 32 = identity (95 %), levels 33..63 blend toward white
(63 = (63,63,63) for every colour). So the 3D render only acts as a "whiten" mask on the bd2 picture:
render index 0 -> unchanged, 4 -> level 40, 8 -> 48, 12 -> 56, >=16 -> pure white.

What the render contains in wire mode (palette indices of the grey wire palette, the bd2 palette is on screen):
- floor/ceiling = wire.fad[L][wire.raw texel]; wire.fad row 32 is special: i -> 4*(i>>2)+4, so every floor
  pixel nearer than depth 1406 is >= 4 (bd2 lifted to level 40 = +25 % toward white) and the hexagon line
  cores (texel 12..31 -> 16..32) become white; beyond depth 1406 the background drops to 0 (unmodified bd2)
  with a hard edge, and the lines dim with 1/z.
- blob mesh / face relief: triangle interiors 0 (bd2 shows through unmodified, hiding lines behind them),
  edges +1/+2 per stroke (level 34..40, faint lightening; dense far meshes brighter).
- typo: bd2 frame 0 ("like you never did before") / 1 ("every moment will be lost") per the toggle times in
  section 0; plus the palette strobe of 0x2e534 (whole palette x0.5..x4.6, clamped at 63, every 2 rows).
- wireblobs: bd2 frame 2 (dark navy with black spots, 6 colours) — no strobe (0x2e534 removed), no toggling.
Both wire switches and the switch back flash white for 0.23 s (linear).
There is no slice displacement, zoom or line overlay anywhere in these sub-parts. Horizontal tearing in a
capture would come from the unsynchronised copy in 0x1bbec (guess).

---------------------------------------------------------------------------------------------------
## 9. Differences from the remake (most visible first)

### Typo — remake/src/scenes/part2.cpp:240-272 (original: 125.94-130.87 music)
1. part2.cpp:245-247: bd2 drawn directly with a zoom pulse. Original: no zoom; the picture is the bd2 frame
   whitened by the live 3D render (perspective hexagon lines of both planes from wire.raw, near-floor lift to
   level 40, the face relief as a hidden-line wireframe moving with the camera) — see section 8 / sim_typo.png.
2. part2.cpp:249-255: horizontal slice displacement does not exist in the exe. The beat-synced "glitch" is the
   palette strobe: at every pos%4==0 (2 rows, 0.21 s, first 126.05, last 130.66) brightness = U[0.5,4.596],
   then (b-1)*=0.9575*0.98 on the first tick and *0.93835 per tick; per channel min(63, trunc(p*b)) — mostly
   blow-outs toward white/flat colours, sometimes a dim frame.
3. part2.cpp:257-267: no white "web strands"; the only line work is the floor hexagons and the face wireframe.
4. part2.cpp:244: frame 1 is shown 127.52-129.19 music (video 126.38-128.07), frame 0 before and after;
   remake shows #1 only for video 126.85-127.4.
5. part2.cpp:268-269: start flash is right (0.23 s linear from white at 125.94); the end flash belongs to the
   next sub-part and starts at the cut (130.87), there is no fade-to-white before the cut.
6. timeline.cpp:21: starts 0.2 s late (126.14 vs 125.94 music).

### FaceBox — part2.cpp:191-237 (original: non-wire half of the typo sub-part, 120.80-125.94)
1. part2.cpp:226-230: there is no box. The object is the faceo.dat relief sheet (318x198 units, face relief
   toward the camera, rim pinned at z=0) at the world origin with the 0x29ae8 stripe warp as texture; it only
   wobbles slightly (Mf) and the face pushes in/out (push, section 7.2).
2. part2.cpp:216-217: camera starts at (0,0,-290) with the sheet exactly filling the screen (continuing the face
   sub-parts) and dollies back on the spring toward the typo path (section 4.1/4.3), looking at (0,0,>=1000).
3. part2.cpp:218-220 / fx.cpp:276-278: planes at y=+-290 symmetric about the start height, tex3d #1, brightness
   min(1,1406/z) in 1/32 steps (no fog colour), no glint (fx.cpp:262-267).
4. part2.cpp:234: the flash at the end is the wire-mode switch flash (from white, after 125.94), not a fade-in.
5. timeline.cpp:20: starts 0.44 s late (121.24 vs 120.80 music).

### Blobs — part3.cpp:112-136 + fx.cpp:243-305 (original: 130.87-147.65)
1. fx.cpp:243-249,281-305: real metaballs, field sum w/r^2 (w=1), iso 0.2, polygonised by marching cubes with
   4-unit cells and env-mapped flat-interpolated triangles, painter-sorted; not a smooth-min SDF ray march
   (shape differs: inverse-square blending, visible facets, triangles closer than depth 40 vanish).
2. part3.cpp:123-130: 1 to 4 balls, not 3 bouncing ones: ball k flies in from 200 units along a random
   direction (y>=0, z>=0 quadrant) with d*=0.99/tick, the next starts when d<2 (new balls at ~135.45, 140.03,
   144.61), then all wobble around (-1000,0,60) with amplitude 3.8/(1+d) grid units (15.2 units at rest),
   sin(n/f+ph), n += 3/tick, f in 48..179.
3. part3.cpp:117-118: camera orbits the cluster (section 4.1, R=130, spring-lagged, look-at with turn rate
   0.01->0.05/tick); remake eye is fixed.
4. Scale: ball radius 8.9 units vs planes 290 units from the orbit plane (camera within +-152 of mid-height);
   remake balls 0.32-0.44 vs floor 1.0/ceiling 1.1 (part3.cpp:119-120,129) are ~10x larger relative to the
   corridor, and the remake's corridor is asymmetric.
5. fx.cpp:256,299-301: env coordinates 128+126*dot(n_inward,R0/R1) on tex3d #0 (point-mirrored vs the remake's
   sphereMap), no extra specular, no fog on the balls (fx.cpp:303).
6. fx.cpp:258-278: floor/ceiling as in FaceBox item 3; texture repeat 512 units = 1.77x the plane distance
   (remake 2.86 units at texScale 0.35 = 2.9-4.4x its plane distances), column <- x, row <- z.
7. 130.87-133.39: the face relief sheet is still drawn (over the blobs) while the camera swings ~1000 units
   from the typo position to the blob site; the remake has no transition.
8. part3.cpp:133: flash is 0.23 s linear from white (remake 0.65 s); possible leftover strobe brightness from
   the last typo kick decays x0.98/tick.
9. timeline.cpp:22: starts 0.31 s late (131.18 vs 130.87).

### WireBlobs — part3.cpp:139-211 (original: 147.65-151.01)
1. part3.cpp:144-170: background is not a flat rotating hex texture; it is the same perspective corridor with
   wire.raw on both planes, same orbit camera continuing, composited onto bd2 frame 2 through wirecut
   (sim_wireblob.png): near cells lifted grey-blue, line cores white, far floor dark navy with black spots,
   hard change at depth 1406.
2. part3.cpp:172-190, fx.cpp:292-297: blobs are the same 4-ball marching-cubes mesh at the same place,
   drawn hidden-line (black-filled faces -> unmodified bd2 inside the silhouette, glow edges adding +1/+2),
   not two separate balls with an object-space grid at a fixed camera.
3. part3.cpp:191: flash 0.23 s from white at 147.65 (remake 146.5-146.8 video, roughly right). No strobe here.
4. timeline.cpp:23: starts 147.71 vs 147.65, ends 151.11 vs 151.01.

---------------------------------------------------------------------------------------------------
## 10. Not determined / caveats
- tau phase (load time between the end of rings and the 0x27670(0x17) call) — take from the capture.
- rand() sequence: ball directions/f/ph and strobe values depend on all earlier rand() calls (not reproducible
  statically; Watcom LCG seed 1 unless srand is called elsewhere).
- T.z in typo = 1000 + 0.01*[0x148f84] at the end of the web part (web-part variable, unknown value; only
  matters for how strongly the camera yaws).
- Exact contents of the MC case/edge tables (data at 0x532a4, extractable) and which winding 0x1f88b treats as
  front; the per-direction recursion copies 0x244ec..0x25900 were not decoded individually (assumed identical).
- Env map axis assignment in 0x1ee85 (row <- n.R1 assumed). Face texturing/shading (0x22e14, 0x1e223, 0x23170
  normal path) left to the face/facebox notes.
- The freed wirecut block is assumed intact in sub-part 3 (no allocation happens in between).
