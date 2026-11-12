# Scope "parts3d-a": the precalc 0x16945, the cloud 0x126ae, the bee 0x11e7f, the spike ball 0x6534

Addresses are offsets in `unpacked/stars_img.bin` (= near pointers). "Tick" = one call of the part's PIT callback
(70.064 Hz); n = ticks since the part installed its callback (each part resets its own counter, see below).
Angles are in 1/4096 turn (the engine masks them with 0xfff). `COS[i]` is the engine's static table at 0x5599c:
4096 int16 = trunc(1023 cos(2 pi i / 4096)); `COS[(a - 0x400) & 0xfff]` is the sine. Pixel coordinates are in the
original 320x200 frame (shown at 4:3). Files are overlay files (`unpacked/files/NN.bin`, read from STARS.EXE at
run time). Music positions are order:row in hex.

Checked against the emulator (scripts in `models/parts3d-a/`, see section 7):

- cloud table 0x1259c: the model equals the snapshot table (all 64,000 words; 4 pixels are never written, see 2.2).
- cloud frames 0x125f4: the model equals full.frames pixel for pixel (rows 1..197) at 16 frames over the whole part
  (5425..5941), with the speed sum of the callback model, including the deceleration; the fade-in levels equal the
  captured DAC palettes.
- bee: camera, bob and environment angles (0x121b4), the 7 animated parts (0x122d7) and the palette (0x11fa1) equal
  the emulator's values in all 430 drawn frames; the fade from white equals full.frames 5949..5965; the text sprite
  intervals as listed.
- spike ball: object path (0x6888), object and environment angles (0x6905), phases and palette (0x6791 / 0x6861)
  equal the emulator's values in all 1,083 drawn frames; the background equals file 19 in full.frames.
- precalc: the placement of the 16 objects (0x16ba1) equals the emulator's in all 64 frames; the RLE stream is
  decoded from a snapshot (64 frames, 1,223,242 bytes).
- file 27 (the bee's background) is a still of the cloud effect: matched by FFT correlation, not exactly (3.6).

The 3D engine (object format, transforms, projection, sorting, the polygon fillers) is the engine scope's; the
palette helpers, the PIT and the loader are core's. This file says how the parts call them.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| routine (main call site) | frames | time (s) | pos | n (ticks) | on screen |
|---|---|---|---|---|---|
| 0x16945 (0xe6) | 63..234 | 0.9..3.3 | — | — | precalc for part 0x164dc (parts3d-b): renders 64 frames of a ring of 16 small spiky objects (file 50, env map file 49) and stores them as RLE sprites |
| 0x1241b (0x11f) | 5212..5420 | 74.4..77.3 | 0a:39..0b:00 | — | precalc of the cloud: tunnel table, second texture layer; palette black |
| 0x126ae (0x12f) | 5421..5941 | 77.35..84.77 | 0b:01..0e:1e | 0..519 | two layers of a 4-bit cloud texture mapped on a tunnel (angle x depth), moving in opposite directions, through a 16x16 palette (purple / orange / pale violet). Fade in over 64 ticks (to 0b:16), full speed to n = 292 (0d:02), slows to a stop at n = 520 (0e:1e) and returns |
| 0x123d8, 0x11d8e (0x134, 0x139) | 5942..5946 | 84.78 | 0e:1e | — | free the cloud, set up the bee (last cloud frame stays) |
| 0x11e7f (0x13e) | 5947..7015 | 84.85..100.09 | 0e:20..16:03 | 0..1068 | one frame with a garbage palette (3.5), white, fade from white in 16 ticks to the bee scene: a golden env-mapped bee (file 28, 68 parts, wings and antennae animated) seen by a camera that moves through 9 keys (one every 140 ticks), in front of a still of the cloud (file 27); the text "23806 faces" (file 32) blinks at the bottom left during keys 4..6 (n 560..909); fade out from n = 942 to 1068 |
| 0x11f48, 0x65ef (0x143, 0x148) | 7016..7169 | 100.1..102.3 | 16:03..17:00 | — | free the bee, set up the spike ball; black |
| 0x6534 (0x158) | 7170..9475 | 102.30..135.19 | 17:01..27:00 | 0..2304 | a ball with long spikes (file 18) turning in front of a static purple swirl (file 19), shaded through an env map + material texture + bump map (mode 15), fade in over 64 ticks; from n = 624 (1b:10) the ball moves on a closed path; from n = 1114 (1e:24) it turns faster; fade out from n = 2094 (25:1a); the part ends at order 0x27 (n = 2304, fade level 11/64) |

The capture's frame rates: cloud 70.0 fps (one frame per retrace), bee 28.2 fps (25.6 fps during keys 4..6), spike
ball 32.5 fps. Everything that moves is stepped by the PIT callbacks; the exceptions are listed per part (the bee's
text blink, the spike ball's path stall).

PARTS.md says the spike ball's spiral rotates: it does not, the background is the static picture file 19 (every
background pixel of full.frames 7400, 8000, 9000, 9300 equals file 19).

## 2. The cloud: 0x1241b (precalc), 0x126ae (part), 0x1278e (callback), 0x123d8 (free)

### 2.1 Data

| what | where | format |
|---|---|---|
| cloud texture | file 30 (0x1000a bytes) | 256x256 bytes at offset 0, values 0..15, row = angle, column = depth; the 10 bytes after it are not used |
| 16x16 palette | file 31 (0x300, raw) | 6-bit RGB; colour c1 + 16 c2 for layer values c1 (layer 1) and c2 (layer 2) |
| sine table | file 33 (0x1130) | 2200 int16 = round(16383 sin(2 pi k / 2200)) (checked: all entries) |
| state | 0x1269e, 0x126a2, 0x126a6, 0x126aa (dwords, initial 0x14000, 0x23000, 0, -0xa000), 0x1278a (dword 0x400) | offsets in 1/1024 texel, step |
| flags | 0x126f2 (set the palette this frame), 0x126f3 (end), 0x126f4 (decelerate), 0x126f9 (palette copied once), 0x11b7a (tick n), 0x11f9c (fade level) | |
| buffers | [0x11b20] file 30 (selector [0x11b24]); [0x12415] layer 2 texture (0x1000a bytes, selector [0x12419]); [0x11b34] file 31; [0x11b2c] work palette (0x300); [0x124e7] file 33; [0x11d8a] table (0x20000 bytes, 64,000 words used); [0x11b60] 64 KB "pixel written" flags (selector [0x11b64]), freed after the precalc | |

[0x11b58] = the first dword of file 31 (colour 0 and the red of colour 1); the bee uses its first 3 bytes.

### 2.2 Precalc 0x1241b

```
file30 = load(30); sel(file30)
L2 = alloc(0x1000a) + selector
for i in 0..0xffff: L2[i] = (file30[i] << 4) & 0xf0            // dword-wise shl 4 / and 0xf0f0f0f0: = 16 * file30[i]
file31 = load(31); [0x11b58] = dword file31[0]
T = alloc(0x20000)                                              // NOT cleared
sin = load(33)
flags = alloc(0x10000) + selector
0x1259c(): build T (below)
free flags
pal = alloc(0x300) = 0; set DAC 0..255 from pal (black)          // 0x127ec: out 0x3c8 = 0, rep outsb 0x300
[0x11f9c] = 0
```

0x1259c / 0x124eb, the tunnel table (one word per screen pixel: high byte angle, low byte depth):

```
clear flags (64 KB)
for m = 1 .. 199:                         // [0x125ea] before the increment
  depth8 = (0x1400 / (m + 0x13)) & 0xff   // 16-bit: m = 1 gives 256 -> 0
  R = m + 1                                // radius 2 .. 200, passed in EBP
  acc = 0
  for k = 0 .. 2199:
    ang8 = (acc >> 16) & 0xff;  acc += 0x1dca          // 0x1dca * 2200 = 2^24 - 16: 256 steps per turn
    y = ((sin[k] * R) >> 14) + 100                      // shrd: arithmetic shift of the 32-bit product
    if y not in 0..199: continue
    x = ((-sin[(k + 0x226) % 0x898] * R) >> 14) + 160   // 0x226 = 550 = a quarter turn: -cos
    if x not in 0..319: continue
    p = y * 320 + x
    if flags[p]: continue                  // the smallest radius, then the smallest k, wins
    flags[p] = 1
    T[p] = ang8 << 8 | depth8
```

Every pixel except the 2x2 centre (x 159..160, y 99..100) gets a value; those 4 keep whatever the block held (in
the capture the stale words 0x7062, 0x6f61, 0xb660, 0xb760). `cloud_model.py` reproduces the snapshot table.

Continuous form (`cloud_fit.py`): with (x, y) the pixel centre (column + 0.5, row + 0.5) in 320x200 units,
r = hypot(x - 160, y - 100) equals the integer radius that hit the pixel to within -0.04 +- 0.31, and
atan2(y - 100, 160 - x) equals k * 2 pi / 2200 to within -0.005 +- 0.006 rad. So

```
angle(x, y) = 256 / (2 pi) * atan2(y - 100, 160 - x)          (mod 256; 0 at the left, 64 at the bottom)
depth(x, y) = 5120 / (r + 18)                                 (mod 256; 256 at r = 2, 23.5 at r = 200)
```

in texels (texel i covers [i, i + 1)). The circles are circles in 320x200 pixel units, i.e. ellipses 1.2 times
taller than wide on the 4:3 screen: keep the pixel-unit geometry.

### 2.3 Drawing 0x125f4 (directly into VGA rows 1..197)

```
si = ((s16([0x126a2] >> 10) >> 1) << 8) + ([0x1269e] >> 10)        // 16-bit
di = (((s16([0x126aa] >> 10) >> 1) << 8) + ([0x126a6] >> 10)) << 1  // 16-bit, even
for p = 320 .. 63359 (rows 1..197), in pairs (p even):
  c1[p]   = file30[(T[p] + si) & 0xffff]
  c1[p+1] = file30[(T[p+1] + si) & 0xffff]
  b = (T[p+1] & 0xfffe) + di                       // layer 2 is read once per pair, at the odd pixel's word
  vga[p]   = c1[p]   + L2[b & 0xffff]              // = c1 + 16 * c2
  vga[p+1] = c1[p+1] + L2[(b + 1) & 0xffff]
```

So colour index = c1 + 16 c2 with c1 = layer 1, c2 = layer 2, both from the same texture. The 16-bit address sum
means the texture is read as one 65,536-byte strip: with u = depth + D, the row is angle + A + floor(u / 256) (a
one-row step at the circle where u crosses a multiple of 256). Rows 0, 198, 199 are never drawn (colour 0).

Offsets as functions of the speed sum S (1/1024 texel, from the callback) and s = S / 1024:

| | depth offset | angle offset | continuous |
|---|---|---|---|
| layer 1 (si) | D1 = 80 + floor(s) | A1 = floor((140 + floor(s)) / 2) | D1 = 80 + s, A1 = 70 + s / 2 |
| layer 2 (di) | D2 = 2 floor(-s) | A2 = 2 floor((-40 + floor(-s)) / 2) | D2 = -2 s, A2 = -40 - s |

Layer 1 moves outwards (towards the viewer: the texel at a given depth moves to larger radii) at 1 texel per tick and
turns at 1/512 turn per tick; layer 2 moves inwards at 2 texels per tick and turns the other way at 1/256 turn per
tick (in steps of 2 rows).

### 2.4 Time: callback 0x1278e, phases 0x126fa, loop 0x126ae

```
0x126ae: [0x11b7a] = 0; set PIT callback 0x1278e
loop: 0x126fa(); wait retrace (0x4a830); if [0x126f2]: set DAC from pal; 0x125f4(); until [0x126f3]
      clear the callback; return

callback 0x1278e (every tick):
  n = ++[0x11b7a]
  e = 0x400
  if [0x126f4]: e = 0x400 - (0x400 * (n - 0x124)) / 0xe4          // idiv; n - 0x124 >= 0
  [0x1269e] += e; [0x126a2] += e; [0x126a6] -= e; [0x126aa] -= e

0x126fa (once per frame, reads n):
  n < 0x40:        [0x126f4] = 0; level = n; pal = (file31 * level) >> 6 (0x120c4); [0x126f2] = 1
  n < 0x124:       once: pal = file31 (copied, NOT sent to the DAC); [0x126f4] = 0; [0x126f2] = 0
  n < 0x208:       [0x126f4] = 1; [0x126f2] = 0
  else:            [0x126f3] = 1                                       // return
```

So S(n) = 1024 n for n <= 292; then S(n) = 292 * 1024 + sum over m = 1 .. n - 292 of (1024 - floor(1024 m / 228)):
the speed falls linearly to 0 at n = 520 (s(520) = 405.61; continuous: s = 292 + m - m^2 / 456). The deceleration
flag is set by the frame that sees n >= 292, so it takes effect from tick 293 at 70 fps (the capture: S at frames
5715..5941 equals this sum exactly). The remake should switch at n = 292 and interpolate S by the tick fraction.

Palette: the DAC gets file 31 x n / 64 (floor) for n = 0..63, then keeps the last level sent, which is 63 at 70 fps:
the cloud is shown at floor(p * 63 / 64), one step darker than file 31 for most entries (captured palettes: levels
0, 3, 18, 48, 62, 63, then 63 to the end). A remake: brightness 63/64.

Time base: PIT ticks from the callback install (frame 5421, 0b:01). Nothing depends on the frame rate or the
music inside the part (main started it at > 0b:00 and calls the bee as soon as it returns).

### 2.5 Free 0x123d8

Frees file 31, the work palette, file 30 + selector, the layer-2 texture + selector, file 33, the table. The DAC keeps
the cloud palette and VGA the last cloud frame (shown during 5942..5946 and in the bee's first frame).

### 2.6 What a remake should evaluate exactly

- Evaluate angle and depth per output pixel with the continuous formulas (2.2) instead of the table; filtered
  (bilinear or better) reads of file 30 at (u, v) = (depth + D, angle + A + floor(u / 256)) for both layers, with the
  continuous offsets of 2.3; drop the pair shortcut of layer 2 (one read per two pixels, depth rounded to even).
- The 16x16 palette is a smooth 2-D ramp (residual from an additive fit <= 3 levels; red ~ 14 + 1.47 (c1 + c2),
  green ~ 1.27 c1 + 2 c2, blue ~ 11 - 0.33 c1 + 1.47 c2, entry 255 brighter): look up the colour bilinearly in the
  16x16 table with the filtered c1, c2 (both in 0..15), instead of the 8-bit index.
- The centre 2x2 pixels (stale memory) and the undrawn rows 0, 198, 199 (colour 0, dark purple) are artefacts: drop
  them.
- File 30 is a generated texture (a subdivision "plasma" with the typical horizontal / vertical crease lines, tileable),
  not hand-drawn; a redraw would be a larger tileable noise of the same look. It is used as a 4-bit height field read
  twice and combined through the palette, so any redraw must stay a scalar 0..15 field.

## 3. The bee: 0x11d8e (set-up), 0x11e7f (part), 0x12136 (callback), 0x11f48 (free)

### 3.1 Data

| what | where | format |
|---|---|---|
| palette | file 26 (0x300) | 0..63 gold ramp (env map shades: black, red-brown, orange, yellow, white), 64..79 white to black (the text), 80..99 other, 100..255 the colours of the background still. Colour 0 is replaced at set-up by the first 3 bytes of file 31 (14, 0, 11) |
| bee | file 28 (0x22498), `WLDD` | bytes 0..7 `WLDD\0   `; the part reads the object count as the word at offset 3 (= 0x0044 = 68: the 'D' is the count); 68 descriptors of 32 bytes from offset 8: +0 x, +4 y, +8 z (int32), +0xc / +0xe / +0x10 angles (int16, all 0), +0x12 offset of the part's `B!\x1a\x0f` object from the file start (the program adds file + 4: the pointer after the 4-byte magic), rest 0; then the 68 B! objects (3,012 vertices, 5,595 faces in all) |
| env map | file 29 (0x102ff) | 256x256 bytes, values 0..63 = palette 26 entries; then a grey-ramp palette starting at byte 65535 (not used) |
| background | file 27 (0xfa00) | 320x200, palette 26 indices 0 and 100..255 |
| text | file 32 (0xb40) | 80x36 sprite "23806 faces", indices 67..75 (text, anti-aliased) and >= 0x50 (transparent) |
| camera keys | 0x11d12, 9 x 12 bytes | int16 x, y, z, angle x, angle y, angle z |
| wing poses | 0x11bbe, 4 poses x 7 parts x 12 bytes (pose stride 0x54) | int16 x, y, z, ax, ay, az per animated part; pose offsets at 0x11b8e (0, 0x54, 0xa8, 0xfc, 0: the 5th entry wraps to pose 0), base pointer [0x11b82] = 0x11bbe |
| animated parts | 0x11ba2, 7 dwords | WLDD part numbers 65, 34, 35, 66, 37, 38, 42 (two forewings, two hind wings, two antennae, the mouth part) |
| scene list | 0x11b66 (+8 = capacity 100) | engine scene structure |
| state | 0x11b7a n; 0x121ac key 0..8, 0x121b0 tick in key 0..139; 0x11b8a pose 0..3, 0x11b86 tick in pose 0..7; 0x127e8 (counted, not read); 0x11f9a set-palette flag, 0x11f9c level, 0x11f9e end, 0x11f9f blink counter, 0x11fa0 palette-copied flag | |

### 3.2 Set-up 0x11d8e

```
0x787fc()                                   // engine reset: camera, env angles [0x55862..66] = 0, buffers
pal26 = load(26);  pal26[0..2] = 3 bytes of [0x11b58]           // colour 0 = file 31's colour 0
pal = alloc(0x300)                          // NOT initialised (see 3.5)
wldd = load(28); text = load(32)
bg = load(27) -> [0x558d2] (+ selector [0x558d6]): the engine's background (0x78905 copies it into the frame buffer)
env = load(29) -> [0x11b26] (+ selector [0x11b2a])
[0x5594c] = 1; [0x57ab6] = 0x1e             // projection: shift 1, distance 30 (engine)
0x7871c()                                   // integer square root table
0x79111(0x11b66)                            // scene list
for each of the 68 descriptors d: d.+0x12 += wldd + 4; 0x7916b(scene 0x11b66, d)
[0x582ac] = 1                               // engine: no face sort inside an object (0x78c2a)
[0x55948] = 3                               // draw mode 3: env-mapped through the selector in [0x55918]
```

Mode 3 (engine 0x4d4b4 / 0x4d5af / 0x4e710): per vertex, env (u, v) = (rotated normal x >> 12) + 0x80,
(rotated normal y >> 12) + 0x80, interpolated over the triangle; pixel = env map byte = palette 26 index 0..63. The
part puts file 29's selector into [0x55918] only around the render call (0x11f10..0x11f2d).

### 3.3 Per frame (loop 0x11ee4)

```
0x11e7f: [0x11b7a] = 0; set callback 0x12136
         pal[i] = min(2 * pal[i], 63) for 768 bytes; wait retrace; set DAC from pal
         copy bg rows 1.. (0x267a dwords = 39,400 bytes from offset 320) to VGA
         [0x11b7a] = 0
loop:    0x11fa1()                                  // palette phase, text sprite (into the last rendered frame)
         wait retrace; if [0x11f9a]: set DAC from pal
         0x121b4(camera 0x55836); 0x122d7()        // camera, wings
         0x78905()                                  // frame buffer rows 1..197 -> VGA, refill it with bg
         [0x55918] = file 29 selector; 0x79483(0x11b66) (render); restore [0x55918]
         until [0x11f9e] == 1
         clear the callback; clear the screen (0x4a84e)
```

Callback 0x12136 (every tick): `++n; ++[0x127e8]; if ++[0x11b86] == 8 { [0x11b86] = 0; [0x11b8a] = ([0x11b8a] +
1) & 3 }; if ++[0x121b0] == 0x8c { [0x121b0] = 0; if ++[0x121ac] > 8: [0x121ac] = 0 }`. The key and pose counters
are not reset by the part (0 from the image); in the capture key * 140 + tick-in-key = n in every frame (no tick
fell between the install and the second reset of n), so key = n / 140, pose = (n / 8) & 3, sub = n & 7.

Camera 0x121b4 (k = key, t = tick in key, K = the keys at 0x11d12, k + 1 wraps 8 -> 0):

```
for c in x, y, z, ax, ay, az:  cam.c = s16(K[k].c + (s16(K[k+1].c - K[k].c) * t) / 140)    // idiv truncates
s = COS[(64 n - 0x400) & 0xfff]                  // = trunc(1023 sin(2 pi n / 64)): a 64-tick (0.913 s) bob
env.ax [0x55862] = 100 - (s >> 3)                // engine "env angles", added to every object's angles for
env.ay [0x55864] = 400 + (s >> 4) + (cam.ay - 0x122)   //   the normal rotation (0x78a81): moves the reflections
cam.y += s >> 6                                   // +-16 units
```

The keys (x, y, z / ax, ay, az): 0 (230, -110, 340 / -380, 180, 60), 1 (230, -110, 340 / -10, 180, 60), 2 (650,
-120, 600 / 130, 540, 60), 3 (280, -60, 670 / 0, 290, 0), 4 (370, -340, 760 / 300, 340, 0), 5 (120, 10, 330 / 310,
290, 0), 6 (-280, 250, 330 / -250, -470, 0), 7 (-700, -10, 550 / 0, -570, 60), 8 (-570, -20, -120 / 0, -570, 60);
read them from 0x11d12. The part ends during segment 7 -> 8 (n = 980..1068). Linear interpolation, no easing: the
camera changes direction abruptly every 2.0 s, as in the original.

Wings 0x122d7 (p = pose, j = tick in pose, P = the poses, p + 1 wraps 3 -> 0): for each of the 7 parts i,
descriptor (WLDD part [0x11ba2 + 4 i]).c = s16(P[p][i].c + ((s16(P[p+1][i].c - P[p][i].c) * j) >> 3)) for x, y, z
(sign-extended to dwords) and the three angles. Poses 0 and 2 have the wings level, 1 and 3 raised (forewings: position y -32 and angle z
-+261; hind wings: x +-2, y -3, angle z 102 / -68); the antennae (angle y 0, -+100, -+200, -+100) and the mouth part
(angle x 0, 200, 400, 200) move through all four. A full flap is 32 ticks (2.19 Hz).
The remaining 61 parts never move.

### 3.4 Palette and text 0x11fa1

```
n < 0x10:   L = min(4 n, 63); pal = 63 - ceil((63 - p) * L / 64)  (0x120df: (p - 63) * L as 16 bits, shr 6, + 63)
            [0x11f9a] = 1                                  // fade from white in 16 ticks (0.23 s)
n < 0x38e:  first time: pal = pal26, [0x11f9a] = 1 (sent once); afterwards [0x11f9a] = 0 and
            if 3 < key < 7:                                // keys 4, 5, 6 = n 560..979, cut at 910 by the next phase
               if ++[0x11f9f] == 14: [0x11f9f] = 0
               if [0x11f9f] < 7: 0x12102()                 // the text
n >= 0x38e: L = clamp(63 + ((0x3ae - n) >> 1), 0, 63); pal = (pal26 * L) >> 6; [0x11f9a] = 1
            if L <= 0: [0x11f9e] += 1                      // end: fade from n = 942 to 1068
            if order (0x1c7a3: AL) == 0x17: [0x11f9e] = 1  // never reached first in the capture
```

0x12102 copies file 32 (80x36) into the frame buffer at offset 0xcbc0 (x 0..79, y 163..198), skipping bytes >=
0x50. The frame buffer then holds the scene rendered in the previous iteration, so the text is on top of the bee.
Row 198 is never shown (0x78905 copies rows 1..197).

Time base: n, PIT ticks from the callback install (frame 5947, 0e:20). The text blink counts drawn frames: 7 frames
on, 7 off, so it depends on the frame rate. In the capture the text is visible for n = 563..587, 615..643, 665..686,
703..717, 731..745, 759..773, 787..801, 820..841, 861..882, 905..910 (half-periods of 14 to 28 ticks; the bee
draws 25.6 fps here). A remake can use these intervals, or toggle every 7 frames at 25.6 fps (0.273 s).

### 3.5 The first frames (a bug)

The part doubles the work palette [0x11b2c], clamped to 63, expecting the cloud's palette there (a bright flash of
the last cloud frame). But 0x11d8e allocated a new, uninitialised block: in the emulator it is memory of the freed
layer-2 texture (bytes 0, 16, 32, 48, 64 ...), so frame 5948 shows the top of file 27 and the bottom of the last cloud
frame with a garbage palette of 18 colours (black, white, pure red / green / blue / cyan mixes). On a real machine it
is whatever the memory manager hands out. Frame 5949 is all white (L = 0), then the fade. A remake: show white
(or the cloud palette doubled, the evident intent) for that frame.

### 3.6 Background, art and what to evaluate exactly

- File 27 is a still of the cloud effect, colour-reduced into palette 26: an FFT correlation of the layer values
  estimated from its colours against file 30 in (angle, depth) space gives layer 1 at angle offset 86, depth 108 and
  layer 2 at angle 218, depth 228 (si = 0x566c, di = 0xdae4); with these offsets the rendered indices map to file
  27's indices one-to-one for 87 % of the pixels, the rest differ by 5.7 RGB levels on average, spread evenly
  (`bee_bg_*.py`). Index 0 areas (5,031 pixels, where c1 = 0 and c2 <= 2) take the patched colour 0. So it is
  generated, not hand-drawn: a remake can render it with the cloud renderer of section 2 at these offsets (true
  colour, any resolution); whether file 27 had other offsets in its making is not known (guess: an earlier version of
  the cloud with slightly different rounding, or a dithered colour reduction).
- File 29 (env map) is a picture (a bright burst with dark streaks, painted or rendered: hand-made), 64 grey levels
  that index the gold ramp 0..63 of palette 26. A true-colour redraw of it as a 0..63 intensity image works: colour =
  the gold ramp interpolated at the filtered intensity.
- File 32 is hand-drawn anti-aliased text (8 grey levels over a cut-out of the cloud background, which is skipped):
  replaceable by a redraw with alpha.
- The bee's palette fades are whole-palette scalings: evaluate as colour * level / 64 (fade out) and white + (colour -
  white) * level / 64 (fade in) in float.
- Rows 0, 198, 199 stay colour 0 (dark purple: the patched colour 0) for the whole part; the remake can draw the full
  frame.
- [0x582ac] = 1: faces are not sorted within each of the 68 parts (the engine sorts the parts by centre). A remake
  with a depth buffer draws the intended picture; whether the original shows sorting errors is the engine scope's
  question.

### 3.7 Free 0x11f48

Frees file 32, clears the callback, frees files 28, 26, the work palette, file 29 + selector, file 27 + selector,
the engine (0x788dd) and the scene list (0x7915b).

## 4. The spike ball: 0x65ef (set-up), 0x6534 (part), 0x6905 (callback), 0x65ad (free)

### 4.1 Data

| what | where | format |
|---|---|---|
| ball | file 18 (0x8038), `B!\x1a\x0f` | 685 vertices, 1,366 faces (engine format) |
| palette | file 15 (0x300) | 0..7 background purples (0, 0, 0) .. (23, 6, 17); 8..255 = 8 L + m: 31 levels L (dark -> colour -> white) of 8 materials m: 0 grey, 1 orange, 2 red, 3 magenta, 4 violet, 5 dark purple, 6 dark green, 7 green |
| material map | file 16 (0x10000) | 256x256, values 0..8 (8 = grey one level up, see 4.3), tileable vein pattern |
| height map | file 17 (0x10000) | 256x256, values 0..15, the same veins; file 16 is a contour banding of it (heights 0..1 -> 0, 2 -> 6, 3..4 -> 7, 5..6 -> 5, 7 -> 4, 8 -> 3, 9 -> 2, 10..15 -> 1, with a few hundred exceptions) |
| background | file 19 (0xfa00) | 320x200, indices 1..6 of palette 15 (a purple swirl) |
| env ramp | 0x6310, 128 bytes | 57 zeros, then 1 .. 63 rising (0x7f - distance -> shade) |
| word table | 0x6398, 128 words | 0..127, copied to the engine's table 0x782d0 (0x79841) |
| object | 0x6498 (32 bytes) | descriptor: +0 x, +4 y, +8 z, +0xc ax, +0xe ay, +0x10 az, +0x12 data, +0x16 = 0x64d8 (a frame list 0, 1, 2, 0, -1 at 0x64e2: not used, file 18 has byte 3 = 0x0f) |
| texture descriptor | 0x6520 | dword file 16 pointer, word selector (0x79801 stores the pointer 0x6520 in [0x582a6]) |
| scene list | 0x6508 (+8 = capacity 100) | |
| state | 0x6518 n; 0x6789 phase 1..5; 0x678a set-palette flag; 0x678b end; 0x678c path flag (a byte counter, see 4.4); 0x678d path angle a (word); 0x678f fade level (word); [0x6394] file 15, [0x6390] work palette | |

### 4.2 Set-up 0x65ef

```
0x787fc(); clear the screen; [0x582ac] = 0 (faces sorted); camera position, angles, env angles, [0x55856..] = 0
0x78c62(file 18, 0x6498)
0x79841(0x6398)                                 // engine table 0x782d0 = 0..127
pal15 = load(15); pal = alloc(0x300)
mat = load(16) + selector -> 0x6520
[0x582ce] = 0; 0x7873a(DL = 0, EBP = 0x6310)    // generate the env map at [0x55914] = 0x582d0 (below)
bg = load(19) -> [0x558d2] (+ selector)
height = load(17) -> [0x558d8] (+ selector [0x558dc])
for each env byte: if 0 -> 8
for each height byte b: b = ((b - 8) << 1) & 0xff        // signed offsets -16 .. +14, even
0x79801(0x6520)                                  // texture = file 16
[0x5594c] = 1; [0x57ab6] = 0x50                  // projection: shift 1, distance 80
0x79111(0x6508); 0x7871c(); 0x7916b(0x6508, 0x6498)
```

Env map (0x7873a with DL = 0, no dithering since [0x582ce] = 0), for dx, dy = 0..127 (and the column dx = 0 at
dy = 128):

```
d = min(isqrt(dx * dx + dy * dy), 127)            // table from 0x7871c: value k repeated 2k + 1 times
v = ((ramp[127 - d] >> 2) << 4)                   // 16 * shade, shade 0..15
env[128 + dy][128 + dx] = env[128 + dy][127 - dx] = env[127 - dy][127 - dx] = env[127 - dy][128 + dx] = v
```

(column = low byte of the address, row = high byte; then 0 -> 8.) A lit disc of radius 70 texels (shade 0 beyond),
brightest in the centre. Generated: a remake evaluates shade = ramp(distance) / 4 continuously (interpolating the
128-byte ramp read from the exe) instead of the 16 levels.

### 4.3 How a pixel is coloured (mode 15: engine 0x4da28 / 0x4dbd7 / 0x51593, span 0x51da8)

```
t = texture (u, v) of the face, interpolated; e = env (u, v) from the vertex normals, interpolated
b = word height'[t]                 // two processed height bytes: at t and at the texel to its right
pixel = mat[t] + env[(e.u + lo(b)) & 0xff, (e.v + hi(b)) & 0xff]       // 8-bit add
```

So the palette index is 16 * shade + material (shade from the env map, displaced by the height map: du = 2 (h(u, v) -
8), dv = 2 (h(u + 1, v) - 8) env texels), i.e. level L = 2 shade, material m; env 8 (the dark outside) gives level
1; material 8 gives grey at level 2 shade + 1. This is the engine scope's to confirm; for this part it means: the
material map and the bump offsets come from files 16 / 17, the shade from the generated env map, and the colour
from palette 15's 8 ramps. A true-colour remake: per material a colour ramp (the 31 entries 8 L + m of palette 15,
interpolated) evaluated at a continuous shade, and filtered maps.

### 4.4 Time: callback 0x6905, phases 0x6791, loop 0x6534

```
0x6534: set callback 0x6905; [0x678c] = 0; a = 0; 0x6888() (position for a = 0); n = 0; [0x55948] = 0xf
loop:   wait for the start of the vertical retrace (0x695d: in 0x3da until bit 3)
        0x6791()                                         // phase
        if [0x678a]: pal = (pal15 * level) >> 6 (0x6861); set DAC (0x696b)
        0x78905(); 0x79483(0x6508)                       // show the last frame, clear with bg, render
        until [0x678b] == 1 or order (0x1c7a3: AL) == 0x27
        clear the callback

callback 0x6905 (every tick):
  ++n
  env.ax [0x55862] += 0x21
  obj.ax += 2; obj.ay += 3; obj.az += 4                  // words
  if phase >= 4: obj.ay += 7; env.ax += 0x14
  if [0x678c] != 0: a += 4; 0x6888()

0x6888 (path, a in 1/4096 turn):
  obj.x = (COS[(a - 0x400) & 0xfff] * 0x46) >> 10        //  70 sin a   (64-bit product, arithmetic shift)
  obj.y = (COS[(2 a) & 0xfff] * 0xb4) >> 10              // 180 cos 2a
  obj.z = ((COS[a & 0xfff] * 0x17c) >> 10) - 0x32a       // 380 cos a - 810

0x6791 (once per frame, reads n):
  n < 0x40:   phase 1, level = n, [0x678a] = 1, [0x678c] = 0     // fade in, 64 ticks
  n < 0x270:  phase 2, [0x678a] = 0                              // DAC stays at the last level sent (62 here)
  n < 0x45a:  phase 3, [0x678a] = 0, ++[0x678c]                 // the ball moves on its path
  n < 0x644:  phase 4, [0x678a] = 0, ++[0x678c]                 // and turns faster (callback)
  n < 0x932:  phase 5, ++[0x678c], [0x678a] = 1,
              level = clamp(63 - ((n - 0x82e) >> 2), 0, 63); if level <= 0: [0x678b] = 1   // fade out from n = 2094
```

The camera stays at the origin with angles 0. Position: (0, 179, -431) until n = 624, then the closed path above:
one lap in 1024 ticks (14.6 s), x +-70, y 180 cos 2a (two up-down cycles per lap), z -810 +- 380. Rotation: 2, 3, 4
units per tick about x, y, z (y: 10 from n = 1114); env angle x: 33 per tick (53 from n = 1114), so the highlights
sweep around the ball.

Quirks, all visible in the capture's state:

- [0x678c] is a byte incremented every frame from phase 3: every 256th drawn frame it is 0 and the callback skips
  the path step for the ticks of that frame. In the capture the path angle lags 4 n - 2496 by 8, 20, 28 units after
  the 1st, 2nd, 3rd wrap (frames drawn at 32.5 fps: a wrap every ~7.9 s). Frame-rate dependent; a remake can drop it
  (a = 4 (n - 624)) or replay the capture's lag.
- The DAC stays at level 62 (the last fade-in frame drawn saw n = 62) during phases 2..4, level 63 from phase 5
  (captured: 62 at frames 7234..8000, 63 at 8800, 9000). Remake: 62/64 then 63/64, or just 63/64.
- The phase is set per frame, so the faster rotation starts one tick late (-7 on ay once).
- Main reaches order 0x27 at n = 2304 (frame 9475): the part ends with the fade at level 11 (the fade would end at n
  = 2346), and the screen cuts to the next part. Keep the music cut.

Time base: n, PIT ticks from the callback install (frame 7170, 17:01); music only at the end. Phases: n 64 (17:16),
624 (1b:10), 1114 (1e:24), 1604 (22:07), 2094 (25:1a), end 2304 (27:00).

### 4.5 Art

- File 19 (background) is a static 6-level picture of a swirl (a paint-program twirl, hand-made), drawn as-is each
  frame (0x78905 refills the frame buffer from it) and faded with the palette: a true-colour redraw can replace it
  (6 levels of palette 15 entries 1..6).
- Files 16 / 17 are generated-looking textures (a vein pattern; the material map is a contour banding of the
  height map), used as material texture (palette offset) and bump offsets into the env map. A redraw must keep
  both as data (a material id per texel, a height 0..15), not as colours.
- The env map is generated (4.2).
- Rows 0, 198, 199: colour 0 (black here).

### 4.6 Free 0x65ad

Frees file 15, the work palette, the scene list, file 17 + selector, file 19 + selector, file 16 + selector, the
engine (0x788dd).

## 5. The precalc 0x16945 (for the chrome star part 0x164dc)

Called by main at 0xe6 (frame 63, before the music starts). It renders 64 frames of a ring of 16 copies of the
small spiky object (file 50: B!, 158 vertices, 316 faces) with env map file 49, and stores rows 78..199 of each as
an RLE sprite remapped to darker colours. Part 0x164dc (parts3d-b) plays them with 0x16acc at frame
([0x163ae] >> 1) & 0x3f (the row of small spiky shapes along the bottom).

```
blk = alloc(0x142710); [0x163b2] = blk
0x787fc(); camera position and angles = 0
0x78c62(file 50, 0x16160)                       // 16 descriptors of 32 bytes at 0x16160
for i in 1..15: desc[i].+0x12 = desc[0].+0x12   // the same object data
envsel = selector(load(49), 0x10000)            // [0x5590e], [0x55912]
[0x55948] = 3; [0x5594c] = 3                    // mode 3 (env-mapped), projection shift 3
0x79111(0x1639e); 0x7871c(); for i in 0..15: 0x7916b(0x1639e, 0x16160 + 32 i)
out = blk; f = 0 ([0x163ae])
clear the frame buffer (64,000 bytes); [0x55918] = envsel (saved / restored); [0x57ab6] = 0x14
for f = 0..63:
  0x16ba1()                                      // placement, below
  0x79483(0x1639e)                               // render into the frame buffer
  0x16b1e()                                      // RLE, below
  clear the frame buffer
resize blk to out - blk (1,223,242 bytes in the capture); free the scene list, file 49, its selector; 0x788dd()
```

0x16ba1, the placement (checked against the emulator for all 64 frames):

```
for i in 0..15:
  ang = 16 * (f & 31) + 256 * i
  desc[i].ay = previous ebx            // i > 0: (ang - 0x500) & 0xfff (the previous object's z angle index)
                                       // i = 0: whatever EBX held: frame 0 the selector of file 49 (56 in the
                                       //   emulator), frames 1..63 the previous frame number f - 1
  desc[i].ax = 0x400; desc[i].y = 100
  desc[i].x = (COS[ang & 0xfff] << 8) >> 10                 // 256 cos / 1024: ~ 255.75 cos(ang)
  desc[i].z = ((COS[(ang - 0x400) & 0xfff] << 8) >> 10) - 0xbe     // ~ 255.75 sin(ang) - 190
```

A ring of radius 256 in the x-z plane at y = 100 around (0, 100, -190), the camera at the origin; each object
turned a quarter turn about x and, about y, tangentially (ang - 0x500). The ring advances 16 units (1/256 turn) per
frame and one object spacing per 16 frames; positions repeat after 32 frames (f & 31), so frames 32..63 differ from
0..31 only by object 0, whose y angle is not the tangential one (a register left over: frame 0 vs 32 differ in 1,214
pixels, frame 31 vs 63 in none). On a real machine frame 0's angle is PMODE's selector number (unknown); a remake
can use 56 or f - 1 = -1.

0x16b1e, the RLE (frame table at 0x163ba, 64 dwords; stream from [0x163b6]): scan frame buffer bytes 0x6180 (row
78) .. 0xfa00: `word skip` (zero bytes), `word length`, length bytes mapped through the 256-byte table at 0x15a60,
repeated; the stream ends with `0xffff` (after a skip, or after a run's bytes). The table maps every colour of the
star palette (static at 0x15e60, 6-bit) to a darker one (median 0.62 of the brightness): the shapes are a dimmed
copy of what the env map gives. 0x16acc draws a frame at row 78 of the frame buffer, skipping the zero runs.

For a remake: render the 16 objects directly (env-mapped with file 49, colours multiplied by about 0.62, or
palette[map[i]] for exact colours) at the frame's placement; file 49 is a picture of a sphere with a net pattern (a
hand-made env map, 65,535 bytes: the last texel is missing), indices into the star palette.

## 6. Engine calls used here (by what goes in and out; the engine scope has the internals)

| call | in | out / effect |
|---|---|---|
| 0x787fc | — | resets camera 0x55836 (dwords x, y, z; words ax 0x55842, ay 0x55844, az 0x55846), env angles 0x55862..66, vector 0x55856..5e, [0x582ac], [0x5799c]; frame buffer [0x558cc] = [0x558de] = 0x682d0 (+ selector [0x558d0]); env-map buffer [0x55914] = 0x582d0 (+ selector [0x55918]); object file list 0x57ac2 = -1 |
| 0x78c62 | EAX file, ESI descriptor | loads a B! object; desc.+0x12 = file + 4; if byte 3 of the file is 0, converts the frame list at desc.+0x16 (+0xa..) to pointers |
| 0x79111 / 0x7915b | ESI scene list | allocates / frees the object pointer array (capacity [+8]) and 16 bytes per object |
| 0x7916b | ESI scene, EAX descriptor | prepares the object (0x79808) and appends it |
| 0x7871c | — | isqrt table at [0x558de] (byte k repeated 2k + 1 times, 64 KB) |
| 0x7873a | DL mode, EBP 128-byte ramp, [0x582ce] dither | radial env map at [0x55914] (4.2) |
| 0x79801 / 0x79841 | ESI | texture descriptor pointer [0x582a6] / 128 words to 0x782d0 |
| 0x78905 | — | frame buffer rows 1..197 to VGA and refilled from [0x558d2] (the background) in the same pass |
| 0x79483 | ESI scene | renders all objects; per object: camera-space centre, cull, sort, faces in draw mode [0x55948] |
| 0x788dd | — | frees the engine's selectors |

Projection parameters set here: [0x5594c] (shift) and [0x57ab6] (distance): bee 1 / 30, spike ball 1 / 80,
precalc 3 / 20. In 0x79483 an object centre at camera-space (x, y, z) (matrix in 1.10 fixed point) goes to
x' = 160 + 1024 x / ((D - z) << shift), y' = 100 + ... (focal length about 1023 / 2^shift = 512 or 128 pixels).
Normals are rotated by the object's angles plus the env angles [0x55862..66] (0x78a81), which is how the bee's bob
and the spike ball's 33-per-tick sweep move the reflections without moving the objects.

## 7. Models and checks (`models/parts3d-a/`, run inside the toolbox from that folder)

| script | what it shows |
|---|---|
| `common.py`, `hooklib.py` | capture / snapshot / file readers, the COS table; parser for the hook dumps |
| `run_hook.py` | copy of `run_stars.py` with `--hook ADDR --hook-dump A:N,... --hook-out F`: dumps memory ranges every time ADDR runs |
| `cloud_model.py` | table 0x1259c and draw 0x125f4; checks the table against snapshot f5421 (equal) |
| `cloud_check.py` | finds the speed sum per captured frame: 16 frames equal full.frames exactly; S matches the callback model |
| `cloud_fit.py` | continuous radius / angle of the table (2.2) |
| `bee_bg_check.py`, `bee_bg_fft.py`, `bee_bg_refine.py`, `bee_bg_where.py` | file 27 against the cloud effect (3.6) |
| `bee_check.py` | camera, env angles, wings, palette against a hook dump at 0x11f23 (`--hook-dump 55836:64,11b7a:4,121ac:8,11b86:8,11f9a:8,2b6d88:2176,127e8:4`, from snapshot f5947 to frame 7020): 430 frames, 0 mismatches (palette within one level: n advances during the retrace wait) |
| `bee_pal_start.py` | the fade from white in full.frames |
| `spike_check.py` | path, angles, env angle, palette against a hook dump at 0x6584 (`--hook-dump 55836:64,6498:32,6518:4,6789:8`, from f7170 to 9480): 1,083 frames, 0 mismatches |
| `precalc_check.py`, `precalc_rle.py` | placement against a hook dump at 0x16a5b (`--hook-dump 16160:512,163ae:4,55836:64,57ab6:4,5594c:1,55948:4`, from f63 to 236); RLE frames decoded from snapshot f235 |

## 8. Not determined

- The bee's first frame palette is uninitialised memory (3.5); the original's intent (doubled cloud palette) is a
  guess from the code.
- How file 27 was made exactly (3.6: 87 % index consistency with the found offsets).
- Object 0's y angle in precalc frame 0 on real hardware (a PMODE selector number).
- The engine's per-pixel maths for modes 3 and 15 (4.3) is summarised from the span code, not modelled; the 3D
  frames were not reproduced pixel for pixel (the engine scope's models can do that with the parameters above).
- On a real machine the PIT tick between the bee's callback install and its second reset of n could make the camera
  run one tick ahead of n (0 in the capture).
