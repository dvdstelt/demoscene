# Scope "parts3d-a": the ribbon part 0x17003 and the tunnel part 0x169e7

Addresses are offsets in `jizz_img.bin` (= near pointers). "time" = `xmpGetTimer + [0x6542]`: PIT clocks
(1,193,182 Hz) since the part's own start (each part sets `[0x6542] = -xmpGetTimer` once in its init; the capture
has [0x6542] = -18,355,371 for 0x17003 and -41,034,228 for 0x169e7, i.e. 15.38 s and 34.39 s of music time).
"Event" = the sync variable [0x6413]. Angles written "units/16384" use the double at 0x11596
(2*pi/16384 = 0.000383495197). Pixel coordinates are in the original 320x200 frame (shown at 4:3). All x87 maths
runs at 24-bit precision (numpy float32 models reproduce it). Texture names are the `/pandora` dump names
(`dump/*.tga`), buffers as in NOTES.md.

Checked against the emulator (scripts in `models/parts3d-a/`, see section 10):

- floor / ceiling planes 0x17553 + band 0x1770b: model equals full.frames pixel for pixel at t1701 (blue clouds,
  phase 1) and t1830, t2060, t2121 (mirror plane, phase 2); at t2085 two of 60,160 pixels differ (one texel
  rounding edge).
- 2D swirl tunnel (table 0x16f4f + mapper 0x16cd3): the table equals the snapshot table, and the model equals
  full.frames at t2950 (whole frame) and t2620 (outside the inset).
- object generators: vertices of the sphere, the ribbon torus and the tunnel torus equal the snapshot vertex arrays
  (|d| < 0.001), face lists and face bytes as described.
- camera path: eye, target and roll equal the emulator's values in all 169 drawn 3D frames of 0x17003 and 302
  of 0x169e7 (max error 7e-4).
- rings: per-frame table model equal to the emulator for all 157 calls of 0x18009; line rasteriser + overlay chain
  (decay, blur, trail, blend to white) bit-exact for 9 consecutive frames; rectangle corner tables of all four
  configurations, the selection by row, the random jitter (RNG 0x141f6) and the drawing bit-exact for 12 frames.
- white inset frames (both parts, all five positions) and the two sprite blits found in full.frames as described.

The engine internals (object storage, transform, sorting, filling, shading) are the engine scope's; the colour
tables, fades, RNG and 2D helpers are core's. This file describes how the two parts call them.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

Ticks are capture ticks (full.ticks), music time = (tick - 434) * 19759 / 1193182 s.

| part | ticks | music time | pos | event: what is on screen |
|---|---|---|---|---|
| 0x17003 | 1362–1550 | 15.37–18.50 | 1:00 | 100: pink / red / black twisted ribbons (env-mapped, map14) over a floor and a ceiling of blue clouds (map8) that scroll and come closer; black band at the horizon |
| | 1551–1598 | 18.50–19.29 | 1:20 | 120: as 100, plus white rings and spokes (centre of the screen) shrinking and turning, added through the 8-bit glow overlay |
| | 1599–1670 | 19.29–20.48 | 1:28 | 128: rings over the clouds, no ribbons |
| | 1671–1743 | 20.48–21.69 | 1:34 | 130: the rings fade out (overlay decays), clouds only |
| | 1744–1746 | 21.69 | 2:00 | 201: one white frame; textures switch: ceiling map1 (rainbow with a glass logo), floor map1a; event set to 200, then 205 |
| | 1747–1838 | 21.74–23.27 | 2:00 | 205: the rainbow "mirror plane" (ceiling and floor) turning, drifting and racing forwards; fades in from white |
| | 1839–2007 | 23.27–26.07 | 2:10–2:2a | 210 / 215 / 220 every 2 rows: rings restart; on 210 the first half of the glass-letter sprite (weirdtxt) at (50,30), on 220 the second half at (2,85) |
| | 2008–2126 | 26.07–28.04 | 2:2c, 2:2e | 225: rings fade, second sprite still shown; 230: rings fade |
| | 2127–2223 | 28.04–29.64 | 3:00 | 301 → 300: fade from white; white sketchy rectangles appear one per row, turning and growing from the centre to the inset position (130,20) |
| | 2224–2317 | 29.64–31.20 | 3:10 | 311 → 310: ribbons (yellow / pink now, over the map10 sphere backdrop) in a 175x110 inset at (130,20) with a white frame; the inset's colours start inverted and return to normal in 29 frames |
| | 2318–2413 | 31.20–32.79 | 3:20 | 315 → 310: the ribbons full screen |
| | 2414–2510 | 32.79–34.39 | 3:30 | 320 → 310: ribbons in a 160x100 inset at (30,90) |
| | 2511 | 34.39 | 4:00 | 400: return |
| 0x169e7 | 2511–2606, 2632–2704, 2727–2799, 2823–2895 | 34.39–40.77 | 4:00, 4:14, 4:24, 4:34 | 400: inside a textured torus (map5, green / grey), full screen |
| | 2607–2631, 2705–2726, 2800–2822 | | 4:10, 4:20, 4:30 | 410: the 2D swirl (map3) full screen with the torus in a 170x95 inset at (90,77), white frame |
| | 2896–2990 | 40.77–42.34 | 5:00 | 500: the 2D swirl only |
| | 2991–3039, 3088–3134, 3183–3230 | 42.34–43.16, 43.95–44.73, 45.52–46.32 | 5:10, 5:20, 5:30 | 515 / 525 / 535 → 510: white sketchy rectangles over the swirl, turning from a start key to the next inset frame |
| | 3040–3087, 3135–3182, 3231–3278 | 43.16–43.95, 44.73–45.52, 46.32–47.11 | 5:18, 5:28, 5:38 | 520: the torus in a 160x100 inset at (30,20), (70,80), (80,50) with a white frame, over the swirl |
| | 3279 | 47.11 | 6:00 | 600: return (event > 599) |

Frame rates in the capture (100 M instructions/s, 70 Hz retrace): 35 fps for most of both parts; 23 fps at
event 120 and 300 (and some 310 frames); 70 fps at 205 and 500.

## 2. Static data, buffers and textures used

Read from JIZZ.EXE at run time (addresses in the image):

| what | where | format / value |
|---|---|---|
| object header + script, part 0x17003 | 0x157a4, 0x56 bytes (script 0x157ac..0x157f9) | float speed 900.0, float distance 350.0, then the object script (section 3.4) |
| object header + script, part 0x169e7 | 0x15945, 0x2e bytes (script 0x1594d..0x15972) | float 1900.0, float 400.0, script (section 4.4) |
| camera path parameter sets | 0x15afc + 36*set, 9 floats each; set 0 (part 0x17003), set 2 (part 0x169e7) | P0..P8 (section 3.5) |
| roll wobble frequencies | 0x15c7f, 0x15c83, 0x15c87 | floats 0.21, 0.62, 0.14 |
| plane constants | 0x17531 (dword 2500), 0x17535 (dword, initial 5700, overwritten in the first frame), 0x17539 / 0x1753b / 0x1753f (words, initially 0), 0x17543 (dword 2600), 0x17547 (float 0.8), 0x1754b (float 256.0) | section 3.3 |
| ring projection | 0x17ff9 (float 160), 0x17ffd (float 100), 0x18001 (float 0.78) | section 3.7 |
| rectangle key frames | 0x181f4 (9 dwords, the event-300 set), 0x18218 (float 0.625), 0x18224 (float 2048.0); 0x18325, 0x18351, 0x1837d (11 dwords each) | section 4.5 |
| swirl table constants | 0x16ff2 (float -299.5), 0x16ff6 (float -169.5), 0x16ffa (float 2.0), 0x16ffe (float 1.3), 0x141e2 (float pi/128) | section 4.3 |
| swirl window | 0x16cc5 (dword 0 = page offset), 0x16cc9 (320 columns), 0x16ccd (200 rows) | |

The ring and spoke parameters (counts, radii, start angles, steps) are immediates in the code of 0x180e5 (code
constants, listed in section 3.7). Sprite positions, inset rectangles and the event numbers are immediates too.

Textures (256x256, built by "decrunching gfx"; buffer ← name, as 0x12c50 assigns them):

| buffer | texture | used here as |
|---|---|---|
| [0x656a] (0x380000) | map8 (blue clouds) | ceiling and floor, events 100..130 (16-bit via 0x17762) |
| [0x654e] (0x230000) | map1 | ceiling from event 201 (16-bit via 0x17762); later overwritten by part 0x169e7 with map5 (see below) |
| [0x658a] (0x500000) | map1a | floor from event 201. Note: the buffer holds map1a.tga cyclically shifted by 64 rows and 64 columns (buffer[y][x] = tga[(y-64)&255][(x-64)&255]); the textures scope should say which one is right |
| [0x6562] (0x320000) | weirdtxt, already converted to 16-bit 5:6:5 (256x256 words) | the glass-letter sprites |
| [0x657e] (0x470000) | map10, interleaved RGB | engine material 13 (the sphere backdrop) |
| [0x6582] (0x4a0000) | map14, interleaved RGB | engine material 14 (the ribbons) |
| [0x655e] (0x2f0000) | map5 | copied into [0x654e] and interleaved (0x13eec) by 0x169e7: engine material 1 (the tunnel torus) |
| [0x6556] (0x290000) | map3 | copied into [0x12425] and converted to 16-bit (0x13d73) by 0x169e7: the 2D swirl |
| [0x654a] (0x220000) | 8-bit 256x256 shade map (white radial highlight) | engine shade map 0 of every face here |

Engine material index i means texture pointer [0x654a + 4*i] (table pointer [0x18d54] = 0x654a).

Work buffers: [0x12425] (0x5eb000, 256K: interleaved texel pairs for the planes, or the swirl texture), [0x1242d] /
[0x12431] (two 16-bit 256x256 copies made by 0x17762), [0x652a] = [0x11207] (0x100000: 8-bit 320x200 overlay
"raw" buffer, pitch [0x1120b] = 320; +0x10000: the "trail" buffer), [0x18005] (0x911fe0, 150 ring records of 32
bytes), [0x181f0] (0x9132c0, up to 8 rectangles of 32 bytes), [0x16f4b] (0x849de0, swirl table 600x340 dwords).

## 3. Part 0x17003 (ribbons, planes, rings, insets)

### 3.1 Init (0x17003..0x17078)

```
byte [0x15c3f] = 0                         // no inset / no roll wobble
0x15bb0(esi=0x157a4, eax=0)                // engine: path set 0, speed 900e-9, distance 350, run the object script
object[0].flags (+0x4c) = 0x43             // object 0 = the sphere backdrop: hidden (bits 0x40|2|1)
[0x11207] = [0x652a]; [0x1120b] = 320      // 8-bit overlay buffer (NOT cleared here, see section 8)
0x1131b()                                  // core: overlay blend tables, colour (31,31,31) = white
[0x1a225] = 0x1c8; 0x1f5c5(eax=-200); [0x1a225] = 0x100   // engine colour table: offset -200, slope 456/256
0x17762(esi=[0x656a], ebx=[0x656a])        // planes: map8 on both
[0x6542] = -xmpGetTimer()
```

### 3.2 Frame loop (0x1707d)

```
for (;;) {
  byte [0x17002] = 0
  if (event == 201) {                      // 2:00
    0x638b(); 0x17737();  0x651f();        // one frame of the white page ([0x637c] = 0xffdf)
    0x17762([0x654e] /*map1, ceiling*/, [0x658a] /*map1a, floor*/)
    object[0].flags = 0                    // sphere backdrop visible from now on
    byte [0x15c3f] = 1                     // roll wobble on; insets get a frame
    inset = (130, 20), 175 x 110           // [0x15c3b] = 20*640 + 130*2, [0x18d64] = 175, [0x18d68] = 110
    event = 200
    0x17979(0x2c4020)                      // start a fade from white (section 3.11)
    continue
  }
  0x638b()                                 // frame start: page, flash decay, sync
  if (event == 301) { 0x17979(0x2dc6c0); event = 300; }
  if (event == 200) { event = 205; 0x180e5(); }       // re-initialise the rings
  if (event >= 400) return
  time = xmpGetTimer() + [0x6542]          // [0x2000]
  set_plane_params(time, event)            // section 3.3
  0x17553(); 0x1770b()                     // ceiling + floor, black band
  if (event == 311) { [0x1a478] = 255; [0x6386] = 9; 0x1a229(eax=[0x6387]=0); event = 310; }  // section 3.6
  if (event == 315) { inset = (0,0), 320 x 200; event = 310; }
  if (event == 320) { inset = (30,90), 160 x 100; event = 310; }
  if (event <= 120 || event == 310) { 0x15bef(); 0x1151c(); }   // 3D objects into the inset, white frame
  if (event in {120, 128, 210, 215, 220}) {
    [0x17002] = 1; [0x17486] = 0 /*no blur*/; [0x17485] = 2 /*trail shift*/; 0x18009()  // rings, section 3.7
  }
  if (event == 300) {
    row = xmpGetPos() & 0xff
    if (row >= 7) { first = min(row, 14) - 7; last = 7; } else { first = 0; last = row; }
    0x183a9(ebp=7, [0x1821c]=first, [0x18220]=last)            // rectangles, section 3.9
    [0x17002] = 1; [0x17486] = 1 /*blur*/; [0x17485] = 1 /*trail shift*/
  }
  if (event in {225, 230, 130} || [0x17002] == 1) overlay([0x17486], [0x17485])   // section 3.8
  if (event == 210) 0x174e5(src=[0x6562],          x=50, y=30, w=254, h=110)
  if (event == 220 || event == 225) 0x174e5(src=[0x6562]+0xd000, x=2, y=85, w=254, h=110)
  0x17999()                                // timer fade (section 3.11)
  0x651f()                                 // flip
}
```

Order inside a frame: planes, band, 3D objects, white frame, overlay (rings / rectangles glow), sprite, fade.

### 3.3 Ceiling and floor planes (0x17762, 0x17553, 0x1770b)

0x17762(esi = ceiling texture, ebx = floor texture), both planar 256x256 RGB: copies them to [0x1242d] and
[0x12431], converts both to 16-bit in place (0x13d73, core: 5:6:5 = r>>3<<11 | g>>2<<5 | b>>3) and interleaves
them into [0x12425]: dword per texel = ceiling word (low) | floor word (high), index v*256 + u. [0x1754f] =
[0x12425] (patched into 0x176b3).

Parameters, set every frame by the part loop (0x17167..0x171d8) from time:

```
if (event <= 199) {                                   // phase 1 (map8)
  angle = 0; X0 = 0                                   // static zeros
  Y0 = (time >> 8) & 0xffff
  if (H > 600) H = max(600, 3660 - ((((time >> 4) * 3)) >> 9))   // H = [0x17535], signed compare
} else {                                              // phase 2 (map1 / map1a)
  H = 600
  e = time >> 4
  Y0 = (-e) & 0xffff;  X0 = (e >> 3) & 0xffff;  angle = (e >> 7) & 0xffff   // words
}
```

(H only decreases; as a function of time it is max(600, 3660 - 3*time/8192): 3660 at the part start, 600 after
7.0 s; event 201 comes at 6.3 s, when H is about 894, and sets 600.) The words at 0x1753d and 0x17541 are never
written (0), so `fiadd dword` reads X0 and Y0 as unsigned 16-bit values.

0x17553, for each d = 100, 99, ..., 6 (95 rows; ceiling row 100 - d, floor row 99 + d):

```
t  = H / d                                   // float
A  = t * 2600                                // half width   ([0x17543])
B  = (t - 1) * 2500                          // forward       ([0x17531])
a  = angle * 2pi/16384;  s = sin a, c = cos a
u0 = B*s + X0;          v0 = -B*c + Y0       // row centre
us = u0 - A*c;  ue = u0 + A*c                // row ends
vs = v0 - A*s;  ve = v0 + A*s
U  = round(us * 256), V = round(vs * 256)    // 16.16 texel coordinates (u, v are in 1/256 texel)
dU = round((ue - us) * 0.8), dV = round((ve - vs) * 0.8)   // per pixel; 0.8 * 320 = 256: the row spans us..ue
for x in 0..319:
  texel = [0x12425][((V >> 16) & 255) * 256 + ((U >> 16) & 255)]     // nearest
  page[100 - d][x] = texel & 0xffff          // ceiling
  page[ 99 + d][x] = texel >> 16             // floor
  U += dU; V += dV                           // 8.16 stepping (rotated registers + adc)
```

So both planes use the same (u, v) at mirrored rows: pixel row r of the ceiling (r = 0..94) has d = 100 - r, row
r of the floor (r = 105..199) has d = r - 99; at x the texture point is
`u = u0 + A*c*(x/160 - 1)`, `v = v0 + A*s*(x/160 - 1)` (x at the pixel's left edge), in units of 1/256 texel,
texture repeating every 256 texels. One register quirk: the integer part of dU also accumulates in the low half
of the rotated register and its overflow carries into the fraction (a 1/65536 texel error at most).

0x1770b then clears rows 95..106 (12 rows, 0x1e00 bytes) to black: the visible ceiling is d = 6..100, the floor
d = 8..100 (rows 105, 106 are drawn and cleared again).

Time base: the timer (time read at 0x17157, the first timer read of the frame). Rates: phase 1 Y0 +18.2
texels/s, H -437/s; phase 2 X0 +36.4 texels/s, Y0 -291.3 texels/s, angle +12.8 degrees/s (a turn in 28.1 s).

### 3.4 The 3D objects (script at 0x157ac)

The engine (0x15bb0 → 0x14394) runs a byte script; the part 0x17003 script decodes as:

```
01 0d                 material = 13           ([0x1487d], face byte +0xe)
02 02 02              texture repeat su = 2, sv = 2   ([0x14885], [0x14889])
00 0c 00  0,0,0  -4000.0  0,0,0,0    op 0 (0x1488d) "sphere": n = 12, centre (0,0,0), R0 = -4000, A1 f1 A2 f2 = 0
01 0e                 material = 14
02 02 04              su = 2, sv = 4
0e 14 5a  -2300.0 500.0 50.0 5.0 60.0 3.0 3.0   op 14 (0x14e7c) "torus": N = 20, M = 90, R0, r0, A1, f1, A2, f2, g
0f                    op 15 (0x15174): keep every other quad of the last object
0c                    op 12: per object 0x1f5cb (engine: normals etc.)
03                    end
```

Draw type byte (face +0xd) is [0x14881] = 3 (default) for both; shade map byte (+0xf) = 0. Vertex byte +0x3c =
3 >> 1 = 1.

Object 0, op 0 (0x1488d), n = 12: N = 2n = 24 around, M = n = 12; vertices for j = 0..M, i = 0..N-1:

```
u = i * 2pi/N,  v = j * pi/M          (accumulated in float: u += du, v += dv)
R = R0 * (1 + A1 sin(f1 u) * A2 sin(f2 v))        // = R0 = -4000 here
P = (R sin v cos u + cx, R cos v + cy, R sin v sin u + cz)
tex u = u * (256/2pi) * 65536 * su,  tex v = v * (256/pi) * 65536 * sv   (16.16; constants 40.7437 at 0x15718,
                                                                          81.4873 at 0x15714)
```

Faces: for each row j = 0..M-1 and i (i1 = (i+1) mod N): (j*N+i, (j+1)*N+i1, (j+1)*N+i) unless j = M-1 and
(j*N+i, j*N+i1, (j+1)*N+i1) unless j = 0: 528 triangles (the pole rows have one per quad). 312 vertices. A
sphere of radius 4000 around the origin, mirrored by the negative radius: the backdrop, seen from inside, with
map10 (material 13) repeated twice around and twice pole to pole. Hidden (flags 0x43) until event 201.

Object 1, op 14 (0x14e7c), N = 20 around the tube, M = 90 along the ring; for j = 0..M-1, i = 0..N-1:

```
u = i * 2pi/N,  v = j * 2pi/M
a  = u + v*g                                // g = 3: the cross-section turns 3 times per lap
rr = r0 + A1 sin(f1 a) + A2 sin(f2 v)       // 500 + 50 sin(5a) + 60 sin(3v)
RR = rr cos a + R0                          // R0 = -2300
P  = (RR cos v, rr sin a, RR sin v)
tex u = u * (256/2pi) * 65536 * su,  tex v = v * (256/2pi) * 65536 * sv     // su = 2, sv = 4
```

Faces per quad (i1 = (i+1) mod N, next row (j+1) mod M): (j,i)-(j,i1)-(j+1,i1) and (j,i)-(j+1,i1)-(j+1,i). Op 15
(0x15174) then halves the face count and keeps quads 0, 2, 4, ... of the list, i.e. the quads with even i: the
torus surface becomes 10 strips, each twisted 3 times around the ring — the "ribbons". 1800 vertices, 1800
triangles, material 14 (map14).

To regenerate with more segments keep N even (op 15 keeps alternate quads in i), and keep the strip width: a
remake can generate ribbon strips of angular width 2pi/20 directly (u in [2k*2pi/20, (2k+1)*2pi/20]).

### 3.5 Camera path and projection (0x15bef → 0x159fb, 0x15a59, 0x15c40; shared by both parts)

0x15bb0 stores `speed = float(header.speed * 1e-9)` (900e-9 here, 1900e-9 in 0x169e7) and the distance
[0x154b4] (350 / 400). 0x15bef, every frame the objects are drawn, reads the timer again:

```
t = float(time) * speed                      // [0x159f7]; time = xmpGetTimer + [0x6542] (a second timer read)
pos(a, s) = s * (P0 cos(phi) cos(th),  P1 sin(phi),  P0 cos(phi) sin(th))
            th = P4 a + P6 sin(P4 a),  phi = P7 sin(P5 a)
eye    = pos(t, 1)                           // [0x1549c]
target = pos(t + P3, P2)                     // [0x154a8]
roll   = P8 (t + P3)                         // [0x154b8]
if (byte [0x15c3f]) roll += pi sin(0.21 t) + pi sin(0.62 t) + sin(0.14 t)    // 0x15c40
```

| set | P0 | P1 | P2 | P3 | P4 | P5 | P6 | P7 | P8 | speed (t per second) |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 (0x17003) | 2950 | 500 | 0.83 | 0.25 | 0.7 | 0.4 | 0.1 | 0.5 | 0.2 | 900e-9 (1.074/s) |
| 2 (0x169e7) | 2700 | 210 | 0.87 | 0.4 | 0.53 | 0.29 | 0.3 | 0.26 | 0.39 | 1900e-9 (2.267/s) |

In 0x17003 the wobble is off until event 201 ([0x15c3f] = 1 from then on); in 0x169e7 it is on.
Part 0x17003: the eye circles the ribbon torus (ring radius 2300, tube about 500) on a radius about 2950,
height up to 500, a lap in about 8.4 s; it looks at a point a little ahead on the same path scaled by 0.83.
Part 0x169e7: the eye runs inside the tube of the tunnel torus (ring 2500, tube 600 + 120 sin 4v), a lap in
about 5.2 s, looking 0.4 ahead (scaled by 0.87).

0x154bc (engine) turns this into a look-at camera: view direction = normalize(target - eye), yaw / pitch from it,
then the roll angle around the view axis; depth Z = (p - eye) . dir. Projection (0x19118 / 0x18ea1) into the
current viewport of w x h pixels at the inset offset:
`x = w/2 + (w/320) * dist * X/Z`, `y = h/2 - (h/200) * dist * Y/Z`, dist = [0x154b4] = 350 (0x17003) or 400
(0x169e7), vertices need Z > 16. So an inset shows the full-screen view scaled to w x h (horizontal field of view
2 atan(160/350) = 49 degrees in 0x17003, 2 atan(160/400) = 43.6 degrees in 0x169e7; on a 4:3 display the
vertical one is 2 atan(1.2 * 100/350)).

Viewport: [0x15c3b] = page offset y*640 + 2x, [0x18d64] = w, [0x18d68] = h (0x1f5d1 renders at page +
[0x15c3b]). Initially (0, 320x200). 0x1151c draws the white frame only when [0x15c3b] != 0.

Time base: the timer (second read of the frame, in 0x15bef).

### 3.6 Engine colour tables and the event-311 "flash" (calls into core 0x1a336 / 0x1a229)

The engine fills a pixel as R[k] | G[k] | B[k] with k = texel channel + shade (shade map byte), tables at
0x1a479 / 0x1b079 / 0x1bc79 (768 entries). 0x1a336(eax = offset) builds them with slope [0x1a225] (/256):
`v = clamp((k * slope + offset*256) >> 8, 0, 255)`, mixed with the flash level f = [0x1a478]:
`v' = ((255-f) v + f (255-v) + v) >> 8` (f = 255 inverts), then reduced to the channel's bits.

- Init of 0x17003: offset -200, slope 0x1c8 (456): v = 1.78 k - 200 (hard contrast: the pink / red / black
  ribbons of events 100..120).
- Event 311: f = 255, decay [0x6386] = 9, 0x1a229(eax = [0x6387] = 0, never written) with slope 0x100: from
  now on v = k (the brighter yellow / pink look of events 310..), starting inverted. 0x638b subtracts 9 from f
  at the start of every frame and rebuilds the tables (when f changed) until it would go negative: 29 frames,
  0.83 s at 35 fps. Only the engine's polygons use these tables: the planes are not affected.

Time base: per drawn frame (frame-rate dependent, already listed in NOTES.md).

### 3.7 Rings and spokes (0x180e5, 0x18181, 0x181bc, 0x18009)

Record of 32 bytes at [0x18005] + 32 i (i = 0..149): +0 x1, +4 y1, +8 x2, +0xc y2 (int32, projected),
+0x10 a1, +0x12 a2 (u16 angles, units/16384), +0x14 r1, +0x16 r2 (u16 radii, only the low byte changes),
+0x18 da1, +0x1a da2 (u16), +0x1c dr1, +0x1d dr2 (u8).

0x180e5 (called in the precalc and at event 200) fills them:

| records | routine | count | r1 | r2 | dr (byte) | a1 | a2 | da |
|---|---|---|---|---|---|---|---|---|
| 0..29 | 0x18181 ring | 30 | 0x50 | 0x50 | 0xfe (-2) | i*546 | (i+1)*546 | 0x28 |
| 30..59 | ring | 30 | 0x70 | 0x70 | 0xfe | i*546 | (i+1)*546 | 0x28 |
| 60..79 | ring | 20 | 0xa0 | 0xa0 | 0xff (-1) | i*819 | (i+1)*819 | 0x28 |
| 80..99 | ring | 20 | 0xb3 | 0xb3 | 0xfd (-3) | i*819 | (i+1)*819 | 0x28 |
| 100..112 | 0x181bc spoke | 13 | 0x73 | 0x4d | 0xfe | 500 + 444 i | = a1 | 0x50 |
| 113..129 | spoke | 17 | 0x78 | 0x60 | 0xfe | 5000 + 764 i | = a1 | 0xff40 (-192) |
| 130..149 | spoke | 20 | 0x79 | 0x10 | 0x03 | 10000 + 672 i | = a1 | 0x90 |

(0x18181: segment angle step = 0x4000 / count, integer, so each ring polygon closes to within 4 units; 0x181bc:
a1 = a2, the spoke goes radially from r1 to r2; first to last spoke of the three fans: 117, 269 and 281 degrees.)

0x18009, once per call (= once per drawn frame while the event is 120, 128, 210, 215 or 220):

```
for each record:
  a1 += da1; a2 += da2                              // 16-bit wrap
  r1 = (r1 & 0xff00) | ((r1 + dr1) & 0xff)           // radius 0..255, wraps: rings shrink to the centre and
  r2 = (r2 & 0xff00) | ((r2 + dr2) & 0xff)           // come back from radius 255 (spokes of dr=+3 the other way)
  x_k = round(cos(a_k) * r_k * 0.78 + 160)           // a_k as signed word * 2pi/16384
  y_k = round(sin(a_k) * r_k * 0.78 * 0.78 + 100)
  line(x1, y1, x2, y2) into the overlay buffer       // 0x1145c, section 3.8
```

Ellipses of radii 0.78 r by 0.608 r pixels around (160, 100) (0.94 aspect on a 4:3 screen). The table is stepped
from the precalc state through events 120 and 128 (60 calls in the capture, ticks 1552..1670: about 17 at 23 fps,
the rest at 35 fps), reset at event 200, and stepped again through 210..220 (98 calls, ticks 1839..2006, 35 fps).

Time base: per drawn frame (NEW: not listed in NOTES.md; the rotation and shrinking speed follow the frame rate).

### 3.8 The 8-bit glow overlay (both parts; core helpers with these parameters)

Buffers: raw = [0x11207] (320x200 bytes), trail = raw + 0x10000. Per frame where it runs:

```
draw lines / rectangles into raw          // 0x1145c: value 0xf9; x-major lines 4 rows tall (y..y+3), y-major
                                          // lines 4 columns wide (x..x+3); a segment is dropped completely when
                                          // an endpoint is outside 0<=x<320, 0<=y<200; no zero-length lines
raw[i] >>= 3                              // inline in the part, all 64000 bytes
if (blur) raw = 0x11eb2(ebp=0)            // raw[i] = (raw[i] + raw[i+1] + raw[i+320] + raw[i+321]) >> 2, in place,
                                          // first 0xf500 bytes (rows 0..195)
if (shift) trail = trail + ((raw - trail) >> shift) in 8-bit arithmetic   // 0x12111(cl = shift)
else       trail = raw (the blend reads raw)
page = 0x11396(trail)                     // for v != 0: each 5-bit channel c -> (c (31 - v) + 31 v) >> 5
                                          // (green: its top 5 bits, low bit cleared): lerp to white by v/32
```

0x17003: rings (events 120, 128, 210, 215, 220) without blur, shift 2; rectangles (300) with blur, shift 1;
events 130, 225, 230 only decay (no new lines; [0x17485] / [0x17486] keep their last values). 0x169e7:
rectangles (510) with blur, shift 1. A line pixel is 0xf9 >> 3 = 31 in the frame it is drawn and 3 the frame
after; the trail buffer then approaches it by 1/4 (or 1/2) per frame. The blend tables are those of 0x1131b with
colour (31,31,31).

Time base: per drawn frame (decay, blur and trail are per-frame recurrences).

### 3.9 Rectangles at event 300 (0x183a9 on the precalc table)

0x18228 runs in the precalc on the configuration at 0x181f4 (section 4.5): x 110 → 218, y 130 → 75, R 80 → 124,
angle 0 → -4096, n = 7, i.e. 8 rectangles from a 114x70 one around (110,130) to the 176x110 one around (218,75) =
the inset (130..306, 20..130) of event 310. Each frame of event 300 draws rectangles first..last (selection in
3.2: rows 0..6 grow the trail 0..row, rows 7..14 slide the window of 8 towards the last one; row 14 and 15 show
only the final frame) with jitter (4.5), through the overlay with blur and shift 1.

### 3.10 Sprites (0x174e5)

`0x174e5(esi = src, eax = x, ebx = y, cl = w, ch = h)`: copies h rows of w 16-bit pixels from a 256-wide image
(stride 512 bytes) to the page at (x, y), skipping pixels equal to 0. Event 210: weirdtxt rows 0..109,
columns 0..253 at (50, 30) (the first word of glass letters). Events 220 and 225: rows 104..213 at (2, 85) (the
second word). Drawn after the overlay, before the fade.

### 3.11 White page and fades (core 0x17737, 0x17979 / 0x17999 / 0x179f6)

Event 201: one frame filled with [0x637c] (0xffdf). 0x17979(rate) stores start = time and level = 0x3ff;
0x17999 (every frame, after everything else): `level = 0x3ff - ((time - start) * rate >> 32)`, 0 when negative
or when time < start; while it was non-zero, 0x179f6(level >> 5) blends the whole page towards white with the
0x1131b table row level>>5 (row 31 = almost white). Event 201: rate 0x2c4020 (fade length 1,515,128 clocks =
1.27 s); event 301: rate 0x2dc6c0 (1.23 s). The start is the time of the frame that sees the event.

## 4. Part 0x169e7 (torus tunnel, 2D swirl, rectangles)

### 4.1 Init (0x169e7..0x16a54)

```
byte [0x11f72] = 0; 0x11f73()                    // core: rebuilds TBL's LUT at 0x11eef (read only by 0x120d1
                                                 // in 0x12013): no effect here
copy [0x655e] (map5, planar) -> [0x654e]; 0x13eec([0x654e])  // interleaved RGB: engine material 1
0x15bb0(esi=0x15945, eax=2)                      // path set 2, speed 1900e-9, distance 400, object script
copy [0x6556] (map3) -> [0x12425]; 0x13d73([0x12425])        // 16-bit swirl texture
byte [0x15c3f] = 1                               // roll wobble on
0x17727()                                        // clear [0x652a], 128 KB: raw and trail overlay buffers
[0x6542] = -xmpGetTimer()
```

### 4.2 Frame loop (0x16a59)

```
for (;;) {
  0x638b(); byte [0x17002] = 0
  if (event > 599) return
  if (event > 499) {
    [0x18d64] = 160; [0x18d68] = 100
    cfg = event == 515 ? 0x18325 : event == 525 ? 0x18351 : event == 535 ? 0x1837d : 0
    if (cfg) { copy 9 dwords cfg -> 0x181f4; inset offset = cfg[10]*640 + cfg[9]*2; event = 510; 0x18228(); }
    swirl(time, window = 0x3aa84)                       // section 4.3
    if (event == 510) {
      a = (xmpGetPos() & 15) * 2                        // row & 15, doubled
      if (a >= 6) { first = min(a, 12) - 6; last = 6; } else { first = 0; last = a; }
      0x183a9(ebp=7); [0x17002] = 1
    }
    if (event == 520) { 0x15bef(); 0x1151c(); }         // torus in the 160x100 inset, white frame
  } else if (event != 400) {                            // 410
    swirl(time, window = 0x29220)
    inset = (90, 77), 170 x 95; 0x15bef(); 0x1151c()
  } else {                                              // 400
    inset = (0, 0), 320 x 200; 0x15bef()
  }
  if ([0x17002] == 1) overlay(blur = 1, shift = 1)      // section 3.8
  0x651f()
}
```

There is no timer fade in this part.

### 4.3 The 2D swirl (table 0x16f4f, precalc; mapper 0x16cd3)

0x16f4f (precalc, core's list; described here because it is the mapping) fills [0x16f4b] with 340 rows of 600
dwords:

```
for j in 0..339: y = -169.5 + j            // float, accumulated += 1
  for i in 0..599: x = -299.5 + i
    a = round(atan2(y, x) / (pi/128) * 2)  // 512 units per turn
    r = round(sqrt(x*x + y*y) * 1.3)
    T[j][i] = ((r & 255) << 8) | (a & 255) // only the low bytes are kept
```

0x16cd3(eax = texture [0x12425], ebx = window byte offset into the table): for the 320x200 screen
(destination [0x2060] + [0x16cc5] = 0, [0x16cc9] = 320 columns, [0x16ccd] = 200 rows, table stride 0x960):

```
off = (dv << 8) | du                       // patched into "add bx, imm16" at 0x16d15
page[y][x] = tex16[(T[row0 + y][col0 + x] + off) & 0xffff]     // 16-bit add: angle byte carries into radius
du = ((time * 0x59d4b) >> 32) & 255        // byte [0x16cd1]: dl of the 32x32 product
dv = ((time * 0x723eb) >> 40) & 255        // byte [0x16cd2]: dh of the product
```

Windows: 0x3aa84 = row 100, column 65 (events 500..599: swirl centre on screen at (234.5, 69.5));
0x29220 = row 70, column 120 (event 410: centre at (179.5, 99.5)). Per pixel, with the screen pixel (px, py) and
centre (cx, cy): `u = 2*atan2(py - cy, px - cx)*128/pi + du`, `v = 1.3*hypot(px - cx, py - cy) + dv`,
colour = map3[v mod 256][u mod 256] (texture row = radius, column = angle; the texture repeats twice per turn).
No aspect correction (circles in pixel units, ellipses on the 4:3 screen). Rates: du 102.2 units/s (the pattern
turns at 0.2 turns/s), dv 0.51 texels/s. Time base: the timer (read at 0x16b04 / 0x16bb9, first read of the
frame).

### 4.4 The torus tunnel object (script at 0x1594d)

```
01 01                 material = 1   ([0x654e] = map5, interleaved by the init)
02 02 02              su = 2, sv = 2
0e 1e 32  -2500.0 600.0 0.0 6.0 120.0 4.0 0.0    op 14 torus: N = 30, M = 50, R0 = -2500, r0 = 600, A1 = 0,
                                                 f1 = 6, A2 = 120, f2 = 4, g = 0
0c                    op 12
03                    end
```

Same generator as 3.4 (no op 15): a torus of ring radius 2500 whose tube radius swells as 600 + 120 sin(4v),
1500 vertices, 3000 triangles, map5 repeated twice around the tube and twice around the ring. The camera
(path set 2) runs inside the tube. Object flags 0 (always drawn). Viewport per event as in 4.2.

### 4.5 Rectangles (0x18228, 0x182e1, 0x183a9)

Configuration (dwords at 0x181f4): xA, xB, yA, yB, RA, RB, angA, angB, n; [0x18218] = 0.625; [0x18224] = 2048.0.
0x18228 writes n+1 rectangles of 4 corners (8 int32) to [0x181f0]; rectangle k (k = 0..n) uses i = n - k:

```
lerp(A, B) = (A*i + B*(n-i)) / n               // 0x182e1: imul, idiv (truncates toward zero)
X, Y, R, ang = lerp(x), lerp(y), lerp(R), lerp(angle)
th = (ang + 2048.0) * 2pi/16384;  c = R cos th, s = R sin th          // c, s rounded to float
corners: (X + c, Y + 0.625 s), (X - s, Y + 0.625 c), (X - c, Y - 0.625 s), (X + s, Y - 0.625 c)   // fistp
```

A square of half-diagonal R turned by th, squashed vertically by 0.625 (0.75 on the 4:3 screen: a parallelogram
when turned). Rectangle 0 is the A key, rectangle n the B key.

| config | x | y | R | angle | n | inset (x, y) | last rectangle |
|---|---|---|---|---|---|---|---|
| 0x181f4 (precalc, event 300 of 0x17003) | 110 → 218 | 130 → 75 | 80 → 124 | 0 → -4096 | 7 | (130, 20) 175x110 (set by the part) | 130..306 x 20..130 |
| 0x18325 (515) | 150 → 110 | 55 → 70 | 70 → 113 | 0 → 4096 | 6 | (30, 20) | 30..190 x 20..120 |
| 0x18351 (525) | 70 → 155 | 85 → 130 | 70 → 113 | 0 → -4096 | 6 | (70, 80) | 75..235 x 80..180 (5 px right of the inset) |
| 0x1837d (535) | 70 → 160 | 85 → 100 | 70 → 113 | 0 → 4096 | 6 | (80, 50) | 80..240 x 50..150 |

0x183a9(ebp = 7): for k = [0x1821c] .. [0x18220] (inclusive), for the four edges c = 0..3 (corner c to corner
(c+1) & 3): one call of the RNG 0x141f6, value v; the endpoints get `x1 += v & 7`, `y1 += (v ror 4) & 7`,
`x2 += (v ror 8) & 7`, `y2 += (v ror 12) & 7`; then line(x1, y1, x2, y2) (0x1145c, 3.8). So every edge is redrawn
each frame shifted by 0..7 pixels right / down independently at both ends: the sketchy, trembling look.

Time base: the key frames are static; which rectangles show is the music row (xmpGetPos); the jitter is per
drawn frame (RNG).

## 5. Time bases (summary)

| variable | base |
|---|---|
| plane H, Y0, X0, angle (0x17003) | timer (first read of the frame) |
| ribbon / sphere / tunnel camera: eye, target, roll | timer (second read, in 0x15bef) * speed |
| swirl offsets du, dv | timer |
| timer fades (events 201, 301) | timer, started at the frame that sees the event |
| sprites, insets, white frames, rectangle selection, ring reset, textures, sphere visibility | sync event / music row |
| ring angles and radii | per drawn frame (35 fps in the capture, 23 at event 120) |
| overlay decay, blur, trail | per drawn frame |
| colour inversion after event 311 | per drawn frame (-9 of 255 per frame, 29 frames) |
| rectangle jitter | per drawn frame (one RNG call per edge) |

## 6. Random numbers

Only 0x183a9 uses the RNG here: 0x141f6 (core: three-dword state at 0x141ea / 0x141ee / 0x141f2, returns the new
[0x141f2]), 4 calls per rectangle per frame. Its state at a given frame depends on every earlier call in the
program (precalc and other parts) and on the frame count, so it cannot be replayed from a time. Observed states
in the capture: (0x44273c73, 0x19e7ee6a, 0xd3a3c270) at tick 1365, (0x296a9e46, 0x815e2d10, 0x768000e0) at tick
2515 (snapshots). A remake can step the same generator with 4 calls per rectangle per simulated frame (fixed
frame rate) from such a state, or use any uniform 0..7 jitter that changes every 1/35 s; only the distribution
(non-negative, 0..7 original pixels, independent per endpoint and axis) is visible.

## 7. Shortcuts a high-resolution remake should evaluate exactly

- Planes (0x17553): one row per d, 8.16 affine stepping, nearest texel, d an integer per row. Exactly:
  per pixel, continuous row position (top: d = 100 - y, bottom: d = y - 99 with y the row coordinate of the
  pixel's top edge in original units; use the pixel centre consistently), u, v from the formulas of 3.3 with
  x continuous, filtered (mipmapped) texture reads. Keep the black band rows 95..106 (it hides the horizon);
  keep the 16-bit wrap of X0 / Y0 / angle (seamless, period 256 texels).
- Objects: 24x12 sphere, 20x90 ribbon torus, 30x50 tunnel torus can be generated with more segments from the
  formulas (keep the ribbon strips' angular width 2pi/20).
- Rings: 4-pixel-thick integer lines, segment polygons of 20 / 30 edges, integer endpoints: draw as smooth
  ellipses / radial strokes 4 original pixels thick (resolution scaled), positions as floats. The rings are
  polygons in the original (546 or 819 units per segment); a remake may draw true ellipses.
- Overlay: the per-frame >>3 and 8-bit trail are a frame-step recurrence; evaluate it at a fixed simulated rate
  (35 fps, the capture's) with float values; the 2x2 box blur (half a pixel towards the top left) becomes a
  Gaussian of sigma 0.5 original pixels; the blend to white is v/32 (31/32 at most) per channel.
- Lines dropped when an endpoint leaves the screen: a 320x200 limitation of 0x1122f (no clipping). Visible as
  ring segments and long spokes vanishing near the edges; clip instead (or keep, as a choice).
- Swirl: angle quantised to 512 steps per turn and radius to 1/1.3 texel, 8-bit texture coordinates, nearest
  texel: evaluate atan2 / hypot per pixel with filtered reads; the carry of the angle byte into the radius byte
  (a one-texel radial step along the spiral where u wraps) is an artefact of the packed word and can be dropped.
- Rectangles: integer corners plus integer jitter, 4-pixel lines: float corners, jitter in original pixel units,
  strokes 4 original pixels wide.
- Inset frames: 1-pixel white outline; for odd widths (175) the top and bottom lines stop one pixel short, so the
  two right corners are missing (0x1151c stores (w>>1)+1 dwords from x-1). Draw a full outline, 1 original
  pixel wide.

## 8. Quirks, frame-rate dependence and things to decide in the remake

- Rings move per frame (3.7): the capture's look is 35 fps (23 fps at event 120). The NOTES.md "static with
  the timer frozen" check for 0x17003 did not cover the ring events.
- The overlay buffers are not cleared when 0x17003 starts: they still hold the TBL part's last 8-bit buffer
  (values up to 24 in both raw and trail at tick 1365). The first overlay frames of event 120 (tick 1553..)
  therefore show a white haze over the whole screen that fades in about 10 frames. 0x169e7 clears them; but
  between its rectangle phases (510 → 520 → 510) the buffers keep their last state, so the first frames of the
  next 510 show the previous rectangles fading out under the new ones.
- Event 201 shows one completely white frame, then the planes fade in from white (1.27 s).
- The colour change of the ribbons between events 100..120 and 310.. is the engine colour table (offset -200,
  slope 1.78 before the event-311 flash, identity after).
- The 525 rectangles end 5 pixels right of their inset; jitter only shifts right / down.
- Part changes and event reactions happen at frame starts (lag up to two frames, see NOTES.md).

## 9. Not determined

- The engine's rasterisation, sorting, normals and how material / shade map combine beyond `k = texel + shade`
  (engine scope); the 3D frames were not reproduced pixel by pixel here, only the camera inputs.
- map1a: whether the 64/64 cyclic shift between buffer [0x658a] and `dump/map1a.tga` comes from the generator
  or from the dump (textures scope). The plane model uses the buffer contents (= tga rolled by 64, 64).
- Two pixels at t2085 differ from the plane model (x87 sin / cos or a .5 rounding case).

## 10. Models (`models/parts3d-a/`, run inside the toolbox from that directory)

See `models/parts3d-a/README.md`. `clouds.py`, `tunnel2d.py`, `objects.py`, `insets.py` work on the snapshots and
full.frames; `rings.py`, `lines.py`, `overlay.py`, `rects.py`, `camera.py` need the hook dumps
`p1_hooks.pkl` / `p2_hooks.pkl` made by `run_hook.py` (a copy of run_jizz.py with a `--hookmod` option) with
`hooks_p1.py` / `hooks_p2.py`.
