## 3. Paranoid ... end of 0x2e8e4 (0x2ffcb - 0x3098e)

Scratch: `scratchpad/re/late3/` (listings `f*.s`, `chain.py` tick simulation, `dump.py` texture/fade dumps in `img/`,
capture frames in `frames/`, `frames2/` with contact sheets `sheet1.png`, `sheet2.png`).

### 0x14760 = rotation matrix from two angles (shared helper, also used by the Web part)

`void rot2(float a, float b, float m[9])` - three **stack** args (pushed b-first order: `push m; push b; push a`),
callee pops 12 bytes (`ret 0xc`). Uses fsin (0x32b72) / fcos (0x32b68). Writes a row-major 3x3:

```
m[0]= cos a*cos b   m[1]= sin a   m[2]= -cos a*sin b
m[3]=-sin a*cos b   m[4]= cos a   m[5]=  sin a*sin b
m[6]= sin b         m[7]= 0       m[8]=  cos b
```
i.e. M = Rz(a)*Ry(b) with Rz=[[c,s,0],[-s,c,0],[0,0,1]], Ry=[[cb,0,-sb],[0,1,0],[sb,0,cb]].
`rot2(0,0,0xa0120)` resets the camera matrix to identity (0x302f9). The camera matrix at 0xa0120 is three rows:
right (0xa0120..28), up (0xa012c..34), forward (0xa0138..40); camera position is 0xa018c/0xa0190/0xa0194 (x,y,z).

Other small helpers met here: 0x3235e = truncate st0 toward zero (sets RC=chop, frndint); 0x1bbec(src,dst,n) = memcpy;
0x187b4(n) = zeroed bump alloc, 0x186e8(n) = non-zeroed bump alloc ("3d engine tempalloc"); 0x10ab4 = 64K alloc.

### Global mechanisms needed to read these parts

**Palette fade (per 70 Hz tick, 0x10124 called from the tick handler before the callbacks).**
L=[0x52918] level, T=[0x5291c] target, S=[0x8eba8] speed, F=[0x52920] float brightness, X=[0x8ebb0] invert flag,
P = 6-bit palette at 0x5faa8:
```
if (L==T) S=0;  if (L<T) L=min(L+S,T);  if (L>T) L=max(L-S,T);
F = (F-1)*0.98 + 1;                          // brightness flashes decay 2%/tick toward 1
c = L>>8;                                    // 0..0x80
c==0x40 : out = min(63, trunc(P*F))
c< 0x40 : out = min(63, trunc((P*c>>6)*F))                 // fade to black
c> 0x40 : w=c-0x41 (0..63), iw=63-w;
          X==0 : out = min(63, trunc((w + (P*iw>>6))*F))     // fade to white
          X!=0 : out = min(63, trunc((((63-P)*w + P*iw)>>6)*F))  // fade to the INVERTED palette
```
0x4000 = normal, 0x8000 = white (or fully inverted when X=1), 0 = black. Speed 100 => 0x8000->0x4000 in 164 ticks (2.34 s).

**Fade tables (*.fad, 16384 bytes = 64 rows x 256, loaded by 0x106a8 into [0x5b67c]).** Rows 0..31 multiply the
colour by r/32 (darken), row 32 is identity, rows 33..63 lighten toward white by (r-32)/31 (row 63 = white).
Checked numerically for pinfx3.fad (weird palette) and 3d.fad (tex3d palette).

**0x1c3f4(src=eax, dst=edx)** (other agent decompiles it): for all 64000 bytes (fixed count, ebx ignored)
`dst = fad[(32 + (src>>3))*256 + dst]` -> "lighten dst toward white by src/256" (screen-like blend, 32 levels).

**Callback slots.** 0x10bd8 accepts 16 slots (dedupes), but the tick handler 0x1bc01 only calls slots 0..7.
I replayed every register/unregister in 0x2e8e4 (and the inits it calls; note 0x23540 registers 0x23384 only with
arg 0x2b, not 0x17). Result: every callback below lands in slots 0..7, so all run. Important: **0x2e798 (the main
script's random F=1.2/0.9 flicker + inverted window on (pos&0xff)>=0xf0) is unregistered at 0x2ef7c and is NOT
active in any sub-part covered here.** 0x2e684 (palette/fade-table swap on [0x1499e8] change) is in slot 7 and active,
but [0x1499e8] stays 0, so it does nothing after the plasma start (palette = 3d palette 0x1496e8, [0x5b67c] = 3d.fad
+ 3d.gst copy, corridor texture = tex3d#1 because [0x57d74]=0).

Before paranoid (0x2ffcb) the callback table (0x40 bytes at 0x8ec04) is saved to the stack and cleared, the 64K
at [0x5b67c] is saved to a temp buffer; after paranoid (0x30246..0x302cb) both are restored and the 3d palette
0x1496e8 is copied back to 0x5faa8. So the plasma state machine (0x2e05c) is frozen during paranoid and resumes in A.

Active callbacks per sub-part (slot order):
- paranoid: 0x16968 (weird time), 0x16b00 (video frame flag), 0x29ce4 (inset size).
- A: 0x28a10 (sheet time [0x57d8c]+=3), 0x2e05c (plasma state machine), 0x16b00, 0x2e838 (overlay fade), 0x2764c (cam ticks), 0x280cc, 0x2e684.
- B: 0x28a10, 0x2e2fc, 0x16b00, 0x2e838, 0x2764c, 0x2e684.
- C: 0x28a10, 0x28a80 (tile ticks), 0x2e240, 0x2764c, 0x2e684.

### Timeline (end positions, verified against the capture)

| sub-part | code | start condition | end test | end pos | end video t |
|---|---|---|---|---|---|
| plasma chain (not mine) | 0x2fbb5/0x2fd3c/0x2fe96 | after star (0xd00) | tick-driven: [0x1493dc] set by 0x2e05c | ~0xe94..0xe9c | ~191.7 (capture: first faint face 191.8) |
| **paranoid** | 0x30088-0x301ea | [0x9b840]=(pos+0x20)&~0x3f = **0xe80** | pos >= [0x9b840]+0x80 | **0xf00** | **196.845** (capture: white at 196.85-196.95) |
| **A** (transition) | 0x302d0-0x304e7 | - | tick-driven: camera z [0xa0194] >= 15000-1000 | ~0xf0a | ~197.38 (37 ticks after 196.85) |
| **B** (sheet chase) | 0x30515-0x306f8 | [0x9b840]=round64 = **0xf00** | pos >= [0x9b840]+0x80 | **0xf80** | **203.566** (capture: inverted at 203.65) |
| **C** (tiles) | 0x3077f-0x308dd | [0x9b840]=round64 = **0xf80** | fade level [0x52918] <= 0 | ~0xfe5 | **~208.9** (capture: black 208.7, VS at 209.0) |
| end of 0x2e8e4 | 0x308ed-0x3098e | frees, 0x26fd8, sets [0x9b840]=round64 | - | ~0xfe5 | ~208.9 |

The dance part then sets [0x9b840]=round64(pos) = **0x1000** (0x31af7), so VS ends at 0x1060 (215.33) and ALEX at
0x1090 (217.85), matching the remake's icons start.

Plasma-chain length (for the other agent / paranoid start): 0x2e3ac waits 400 ticks ([0x5809c] = -500 -> -100) then
[0xa1558] 1.0 -= 0.003/tick -> 333 ticks; 0x2e360 [0x58098] -0.8 += 0.008/tick -> 226 ticks; 0x2e05c ~578 ticks with
camera z0 = 0 (it depends on the camera z at its start: +-50 ticks per +-300 units). Total ~1537 ticks = 21.96 s after
0xd00 -> ~191.9 video at z0=0. The capture shows the paranoid fade-in starting ~191.7, i.e. before the rounding
boundary 0xea0 (191.805): [0x9b840]=0xe80 is confirmed by the end at 196.85. **This rounding is fragile** (a few
tenths of a second): the remake should hard-code 0xe80/0xf00, not derive it.

---

### 3a. Paranoid (0x30088 - 0x301ea; routines 0x16970, 0x16b34, 0x29d1c, 0x1c3f4)

**What it draws.** weird.raw (256x256, teal/blue organic texture) mapped full screen through a moving radial ripple;
the PARANOID.LZW greyscale face (160x96, also rippled, scaled to full screen) "lightens" it through pinfx3.fad (bright
face areas go white, dark areas show the texture); a 1/3-scale colour-inverted thumbnail of the whole frame opens as a
growing window at (32,100)-(138,166) with a white 1-px frame. Fades in from white.

**Setup (0x2ffcb-0x300c5).**
```
save callbacks, clear; save 64K [0x5b67c]
F=1.0; L=0x8000; S=0; VGA palette := all 63 (immediately white); memset(screen,0); T=0x4000
weird_init():  tex=alloc64K; load "weird.raw": palette(768) -> 0x5faa8, 65536 -> tex; add_cb(0x16968)
video_init():  vtex=alloc64K; vbuf=alloc(0x7800); open "paranoid.lzw" (0x12ccc); frame=0; add_cb(0x16b00); newframe=0xff
[0x9b840] = (pos+0x20)&~0x3f            // = 0xe80
load_fad("pinfx3.fad") -> [0x5b67c]
S = 100                                 // fade from white over 164 ticks = 2.34 s
inset_init(mode 0x17, edx=1)            // see 0x29d1c
```
**Frame loop.**
```
weird_render(A=0x5fda8); video_render(B=0x6f7a8);
lighten(B -> A)            // 0x1c3f4: A = pinfx3[(32+(B>>3))*256 + A]
inset_render(A, dst=0x67ac8 /* = A + 100*320 + 32 */);
copy A -> screen;  until pos >= 0xe80+0x80
```
**End (0x301ea).** inset/weird/video freed; F=1, L=0x8000, S=0, VGA palette all white, screen cleared (hard cut to
white at 196.85); restore callbacks, 3d palette, saved [0x5b67c]; T=0x4000.

**Grid warp (0x14920) + 8x8 grid mapper (0x1d0dd)**, shared by weird and video:
```
grid[26][41] of {v (8.8), u (8.8)}, screen point (8i, 8j), i=0..40, j=0..25 (8x8 blocks, 40x25 cells)
warp(cx, cy, U0, V0, A, Fr, Ph):
  r = sqrt((8i-cx)^2 + (8j-cy)^2)                 // screen pixels
  k = 1 + A*sin(Fr*r + Ph)
  v = trunc((v - V0*256)*k + V0*256);  u = trunc((u - U0*256)*k + U0*256)
mapper: bilinear interpolation of (u,v) inside each 8x8 block, nearest texel,
        texel = tex[(v>>8 & 255)*256 + (u>>8 & 255)] (both axes wrap at 256), u fraction rounded with +0x80.
```
- weird (0x16970 render): `u0 = i*65536/41, v0 = j*51200/26` (texels u 0..249.8, v 0..192.3, i.e. ~0.78 texel/px
  horizontally, 0.96 vertically); t1 = [0x9c9b4] (+= 7 per tick in 0x16968, starts at 0, only used here);
  `cx = trunc(80*sin(0.01*t1 + 24) + 160)`, `cy = trunc(50*cos(t1/143 + 34) + 100)` (0.00699300699 = 1/143),
  `warp(cx, cy, U0=128, V0=100, A=0.3, Fr=0.05, Ph=t1/72)`.
  Per second: centre x phase 4.9 rad/s, centre y 3.43 rad/s, ripple phase 6.8 rad/s.
- video (0x16b34 render): when newframe: if frame==0 rewind stream (0x11308(stream,1)); decode luma 160x96
  (0x13174, 15360 bytes, 8-bit) and copy into the 256-stride texture; `frame = (frame+1) % 44` (only frames 0..43 of
  the 45 in the file are ever shown). newframe is set by callback 0x16b00 whenever pos changes to a multiple of 4
  half-rows, i.e. **one video frame every 2 rows = 0.20975 s (4.77 fps), locked to the music**.
  Grid: `u0 = i*0xa000/41, v0 = j*0x6000/26` (the 160x96 frame stretched to full screen);
  t2 = [0x9c9b0] (+= 2 per tick in 0x16b00, starts 0); `warp(160, 100, U0=80, V0=48, A=0.07, Fr=0.02, Ph=t2/172)`
  (0.00581395349 = 1/172 -> 0.81 rad/s). At the ripple maximum it samples a few texels outside the 160x96 area.

**Inset 0x29d1c** (mode 0x17: size f=[0x147398]=0; mode 0x2b would start at 1.0; edx: 1 = inverting LUT, else identity).
```
init: buf = alloc64K; lut = buf[0..255]:
      lut[i] = nearest palette index (sum of squared 6-bit RGB differences, 0x10474) to (63-R_i, 63-G_i, 63-B_i)
      using the palette in 0x5faa8 at init time (= weird.raw palette); add_cb(0x29ce4): f = min(1, f + 0.01) per tick
render(src=A, dst):
  thumb = buf+0x100 (stride 320): for y<67, x<108: thumb[y][x] = lut[src[3y][3x]]   // 1/3 nearest downscale (0x1c0d6)
  W = trunc(106 f); H = trunc(66 f); ox = trunc(53 (1-f)); oy = trunc(33 (1-f))
  blit W x H from thumb(ox,oy) to dst + oy*320 + ox  (dst = screen (32,100)),
  plus a 1-px border of colour 255 (= white in weird.raw) around it (0x1c117); nothing if W or H = 0.
```
So the window opens from its centre (85,133) to the full (32..138, 100..166) = (0.100, 0.500, 0.331 w, 0.330 h of
the frame) in 100 ticks = **1.43 s, linear**, and always shows the matching part of a whole-frame thumbnail
(full face, colours inverted: the teal/blue picture becomes orange/brown).

**Time bases.** t1, t2, f: per 70 Hz tick (callbacks). Video frame: from [0x9b844] (every 4 half-rows).
Palette fade: per tick. Grids are recomputed per rendered frame from those values.

**Differences from the remake (remake/src/scenes/part3.cpp).**
1. :430 the base layer is a duotone of the clip; original = weird.raw rippled texture (u,v formulas above), with the
   clip only lightening it via pinfx3.fad (`out = c + (63-c)*level/31`, level = luma>>3). The face's dark areas show
   the texture, not dark blue. Most visible difference.
2. :428 / :435 video rate 12 fps over all frames; original 1 frame per 2 rows (4.77 fps) synced to the music, 44-frame
   loop restarting at frame 0 at the part start.
3. :431-437 inset: original is an inverted-colour 1/3 thumbnail of the composed frame (same frame, not +20 frames;
   colours from the nearest-inverse LUT, not a brown duotone), rect (0.100, 0.500, 0.331, 0.330) instead of
   (0.21, 0.5, 0.22, 0.26), growing linearly from its centre (85/320, 133/200) during the first 1.43 s of the part
   (from 0 size), not 192.9-193.5 smoothstep from a small rect; the original crops (window opens) rather than scales.
   Border: 1 px white, outside the image (remake :434 is close).
4. :426 / :439 start: original fades in from white starting ~191.7 with L linear over 2.34 s (palette mix
   P*(63-w)/64 + w); remake 191.3 smoothstep over 1 s.
5. :440 end: original hard cut to full white at 196.845 (0xf00), not a 0.5 s fade from 196.9.
6. Both layers ripple (weird strongly: +-30 % zoom around a wandering centre; video +-7 %); remake has none.

---

### 3b. Sub-part A (0x302d0 - 0x304e7): transition, ~0.53 s

**What it draws.** The hex corridor and the waving blue sheet (both continuing from the plasma chain), with the
paranoid clip lightening the whole frame at full strength, all fading in from white.

```
setup: z=[0xa0194] = 15000 + (-1500) = 13500; cam x=y=0; camera matrix = identity (rot2(0,0,0xa0120));
       S=100 (but 0x2e05c forces T=0x4000, S=150 every tick -> white fade takes 109 ticks = 1.56 s);
       video_init() again (stream restarts at frame 0; t2 keeps counting); add_cb(0x2e838)
loop:  cam_targets_276f8(); camera_27444(1); corridor_27670(A); spheres_28100(A); flare_28330(A);
       if ([0x1493d8]) sheet_2902c(A);          // set to 1 by 0x2e05c, so the sheet is drawn
       overlay(A);                               // see below
       copy A -> screen
until !(14000 > z)
```
`overlay(A)` (also in B): if k=[0x580bc] > 0: video_render(B); `lut[i]=trunc(i*k)`, i<256; `B[n]=lut[B[n]]` for
65536 bytes (overruns the 64000-byte buffer by 1536); lighten(B -> A) with 3d.fad. k = 1.0 at A start (data init,
only written by 0x2e838), `k -= 0.005` per tick, clamped at 0 -> the face fades out linearly over 200 ticks = 2.86 s
(gone at ~199.7 video, verified: still visible at 198.5, gone at 199.5).

0x2e05c keeps running in its last phase ([0xc1d70] > 1000): every tick `z += w; w += 0.1` (w = [0x580b4] = 5.9 at A
start), `z += v` (v = [0x1493d4] = 6), look target = (0, 0, [0xc1d70] + 15000) (≈16000+), roll rate [0xc1d68] =
0.04*sin(p) rad/tick (p = [0x1493d0] += 0.01/tick, ≈6.15 here), orbit offsets [0xc1d34..44] zeroed, [0xc1d70] += 2,
T=0x4000, S=150. z reaches 14000 after 37 ticks; w ends at ≈9.6. The 0x28100 spheres (radius [0xc1d70]+60.. at the
origin) and the 0x28330 flare are behind the camera and are culled (0x27958 / 0x28330 test camera-space z).

### 3c. Sub-part B (0x30515 - 0x306f8): chasing the waving sheet, 197.38 -> 203.566

**What it draws.** Flight along the hex corridor (floor and ceiling) behind a waving 480x300 sheet textured with the
blue tex3d#2 texture and phong-lit; the camera orbits sideways around the sheet's axis while keeping a constant
distance; the face overlay fades out during the first ~2.3 s.

```
[0x9b840] = round64(pos) = 0xf00; unregister 0x2e05c; spheres_free; add_cb(0x2e2fc)
loop: cam_targets_276f8(); tx=[0xc1d4c]*=3; ty=[0xc1d50]*=1.5;     // per frame, on freshly computed targets
      camera_27444(1); corridor_27670(A); sheet_2902c(A); overlay(A); copy -> screen
until pos >= 0xf80
callback 0x2e2fc per tick: p += 0.01; d = v + w (= 6 + 9.6 = 15.6);
      Z=[0x57d88] += d; z=[0xa0194] += d; look target z [0xc1d48] = Z; roll [0xc1d68] = 0.004*sin(p)
```
Z starts at 15000 (data) and z at ~14007 -> the sheet stays ~990 units ahead; camera speed 15.6 units/tick =
1092 units/s = 3.77 corridor half-heights/s. Total roll over B ≈ 0.4*(cos 6.15 - cos 10.49) ≈ 0.6 rad.

**cam_targets_276f8** (t = [0xc1d6c], += 4.0 per tick by 0x2764c, never reset):
```
R  = 290 + 50*sin(0.0005 t + 64)
tx = cos(3t/1100) * sin(3t/700 + 34) * R + [0xc1d34]      // [0xc1d34], [0xc1d38] are 0 here
ty = sin(3t/1100) * sin(3t/700 + 34) * R + [0xc1d38]
```
(0.000909090909 = 1/1100, 0.00142857143 = 1/700). In B: x amplitude up to ±1020, y ±510 (after *3, *1.5).

**camera_27444(mode)** runs once per pending tick (counter [0xc1d64] incremented by 0x2764c), so it is frame-rate
independent:
```
d = look - campos; dist = |d|
if dist > 0.01:
   n = d * (turn/dist)  (turn = [0x57d80] = 0.05)
   if (mode & 1) axis = n x forward  else axis = 0
   camM = camM * rot3(axis) (0x2721c + 0x18b18)        // turns toward the target, ~0.05 rad/tick * sin(angle)
   if roll != 0: camM = camM * rot3(0, 0, roll)         // roll = [0xc1d68] rad per tick about forward
for k in 0 .. (mode&2 ? 2 : 1):                         // x,y (mode 1) or x,y,z (mode 3)
   vel[k] = damp*vel[k] + 0.002*(target[k] - pos[k]); pos[k] += vel[k]   // damp = [0x57d7c] = 0.79 (0.87 in C)
```
Targets: pos targets [0xc1d4c..54], look target [0xc1d40..48], velocities [0xc1d58..60].

**corridor_27670** (0x27014 grid + 0x1cddc mapper; also used by other parts): 41x26 rays, ray(i,j) = 37.5*forward +
(i-20.5)*right + (j-13)*up (focal 300 px); hit the plane y=+290, or y=-290 if that t is negative; texel = hit (x,z)/2
(tex3d#1 purple hex, 256 texels per 512 units, wraps); shade = clamp(307200/t, 0x100, 0x2000) -> 3d.fad row
shade>>8 (1..32): full brightness up to ~1406 units ahead, then ∝ 1/distance. Interpolated per 8x8 block.

**sheet_2902c** (render; init loads phong1.raw (256x256 light map) to [0x5b670], add_cb(0x28a10): T=[0x57d8c] += 3
per tick, data init 523):
```
for j=-10..10, i=-16..16 (21 x 33 = 693 vertices, 1280 triangles):
   Ta = 0.0164375*T;  Tb = 0.013*T
   h  = 60 * sin(i/7 + Ta) * cos(0.2 j - Tb + 34)
   pos = (15 i, 15 j, Z + h)                       // 480 x 300 sheet at depth Z=[0x57d88]
   gx = 60*cos(0.2j - Tb + 34)*sin((10i+1)/70 + Ta) - h
   gy = 60*sin(i/7 + Ta)*cos(0.02(10j+1) - Tb + 34) - h
   nrm = (gx, gy, 1)/sqrt(1 + gx^2 + gy^2)         // forward-difference normal
project (0x26524): screen = (160 + 300 x/z, 100 + 300 y/z); env coords = 128 + 126 * (camera-space normal x, y);
                   triangles bucket-sorted back to front, bucket = clamp((z-120)/12, 0, 99)
draw (0x285d8 -> 0x1e223): texture tex3d#2 ([0xc1d24], blue plasma), u = 128 + 0.390625 x, v = 128 + 0.625 y
     (central 188x188 texels, no wrap), lit by the env coords through phong1 / 3d tables
```
Wave speeds: 3.45 rad/s along x (wavelength 44 columns = 660 units), 2.73 rad/s along y (wavelength 31 rows = 471).

### 3d. Sub-part C (0x30708 - 0x308dd): the sheet bursts into tiles, 203.566 -> ~208.9

**What it draws.** Same corridor, looking straight ahead; the sheet is replaced by 640 flat 15x15 tiles (hex
texture) that swirl outward, stretch horizontally, tumble and then rush back past the camera. Palette starts fully
INVERTED (purple/blue -> green/yellow) and fades back toward normal, then everything fades to black.

```
unregister 0x2e2fc; video_free; unregister 0x2e838
L = 0x8000; X=[0x8ebb0] = 1 (inverted mode); S = 70; T = 0x4000; [0x1473b8] = 1
tiles_init_28c64(); roll/orbit offsets [0xc1d34..3c] = 0; add_cb(0x2e240)
[0x9b840] = round64(pos) = 0xf80
loop: cam_targets_276f8();   // immediately overwritten by:
      cam_targets_28aa0();   // tx=cos(t/1100) sin(t/700+34) R2, ty=sin(t/1100) sin(t/700+34) R2,
                             // tz=Z + 2 R2 cos(t/700+34), R2 = 300 + 50 sin(0.0005t+64);
                             // look = (0, 0, Z); damp [0x57d7c] = 0.87
      camera_27444(1); cam x = cam y = 0 (forced every frame -> tx/ty/tz have no visible effect)
      corridor_27670(A); tiles_render_28c64(A)
      if (pos > 0xf80 + 0x28) { T = 0; S = 100; }      // fade to black, checked per frame
      copy -> screen
until L <= 0
callback 0x2e240 per tick: p += 0.01; Z += 15; z += 14; look z = Z; roll = 0.004*sin(p);
      if (pos > 0xf80 + 0x20) { I = trunc(I + a); a += 0.4; }   // I=[0x13efbc], a=[0x1493ac], both start 0
```
X is never cleared during C (0x2e798 is not active), so the first ~2.15 s show the inverted palette mixing back
toward normal (L 0x8000 -> 22268, i.e. still 1/3 inverted, looks grey at 205.0), then L falls 100/tick through
normal (0x4000 at ~206.6) to black. Fade-to-black trigger at pos 0xfa9 ≈ 205.7 video; L reaches 0 ≈ 373 ticks after
the C start -> ~208.9 video (pos ~0xfe5). Tile pull-back starts at pos 0xfa1 ≈ 205.25.

**tiles_init (0x28c64(0x17))** - 640 records of 44 bytes in a zeroed buffer:
```
for j=-10..9, i=-16..15:              // 20 x 32 tiles covering the sheet exactly
   p = (15 i, 15 j, 60 * cos(0.2 j - 0.013 T + 34) * sin(i/7 + 0.0164375 T))   // same T as the sheet: seamless
   angles a = b = 0
   wa = (rand()-0x4000) * 3.3333e-6;  wb = (rand()-0x4000) * 3.3333e-6       // ±0.0546 rad/tick
   phase = rand() * 3.3333e-4 (0..10.9);  freq = 0.1 / (30 + 0.001*rand())   // 0.0016..0.0033 rad/tick
   swirl s = sin(1e-4 (i^2+j^2)), c = cos(1e-4 (i^2+j^2))
vertex/triangle buffers: 4 vertices, 2 triangles per tile (2560 / 1280); add_cb(0x28a80): n=[0x57d90]++ (data
init 45), pending++.
```
**tiles_update (0x28b54)**, run once per pending tick:
```
for each tile:
   (x, y) = (c x - s y, s x + c y)      // rotate the anchor about the sheet axis; corner tiles 0.0356 rad/tick
   x *= 1.007                           // horizontal spread only (y is not scaled)
   z += 3 + 0.3 sin(n*freq + phase)
   a += wa; b += wb
if [0xa12b8] <= 0: [0xa12b8] = 50      // purpose unknown
```
**tiles_render**: M = rot2(a, b); v0 = (x, y, z + Z - I), v1 = v0 + 15*M.row0, v2 = v0 + 15*(row0+row1),
v3 = v0 + 15*row1; normal = M.row2 (only stored on v0); project (0x26524); draw (0x287c4 -> 0x1e223) each triangle
with texture tex3d#1 ([0xc1d20], hex) addressed **planar from the current world position** u = 128 + 0.78125 x,
v = 128 + 1.25 y (so the texture slides across tiles as they move), and **flat** env shading (all three corners get
v0's env coords).
Relative motion: tiles move +18 - a per tick, camera +14 -> they recede 4/tick until 205.25, then a grows 0.4/tick
and the tiles (≈1460 units ahead) pass the camera ~96 ticks (1.4 s) later, ≈206.6 video.

### 3e. Differences: FaceFloor (part3.cpp:445-480) vs A+B+C

1. :461-475 the "card" is the paranoid clip on a spinning quad until 204. Original: the card is the waving
   480x300 sheet with the **blue tex3d#2 texture** (height field and phong as above, no face); the face is only a
   full-screen lighten overlay that fades out linearly from 196.85 to ~199.7.
2. Missing tile burst (203.57 onward): 640 hex-textured tiles swirling (anchor rotation 1e-4(i²+j²) rad/tick),
   stretching x by 1.007/tick, tumbling, receding, then rushing back past the camera from 205.25.
3. :476 the green-white flash is really the **inverted palette** ([0x8ebb0]=1, L=0x8000) at 203.566 fading at
   70/tick toward normal (only to 1/3), then :458 fade to black from ≈205.7 at 100/tick, black at ≈208.9 (remake
   dims to 0.2 between 205.5 and 208.0; timeline.cpp:30 ends the scene at 208.4, should be ~208.9).
4. :449/:477 start: original A starts at 196.845 fully white and fades at 150/tick (1.56 s); remake 197.4 + 0.4 s.
5. :451-454 camera: original B speed 1092 units/s (3.77 corridor half-heights/s; C 980 u/s = 3.4 hh/s) vs remake
   1.5 hh/s; original keeps the sheet ~990 units (3.4 hh) ahead at screen centre while the camera position
   spring-follows a sideways orbit (x ±1020, y ±510 units, period ~8 s) - the sheet is seen strongly obliquely;
   roll is small (≈0.6 rad over B, ≈0.17 rad in C, rate 0.004 sin(p) rad/tick) vs remake 0.6 + 0.25 t (2.75 rad).
   In C the camera is centred (x=y=0) and looks straight ahead.
6. :455-457 corridor: planes at y=±290, hex texture 512 units per repeat (1.77 half-heights), brightness 1 up to
   1406 units (4.85 hh) then ∝ 1/distance through 3d.fad (remake: fogDist 5, texScale 0.35 in its own units).

### What I could not determine
- Exact paranoid start: the plasma chain is tick-driven (~1537 ticks after 0xd00, ±50 depending on the camera z
  when 0x2e05c starts); capture says ~191.7. Only matters for the white hold before the fade-in.
- Internals of the polygon fillers 0x1e223 / 0x285d8 (how phong1.raw and 3d.gst combine with the texture) and of
  0x1cddc's shading; 0x287c4 issues a second fill per triangle with permuted vertices (0x289e5 loop) that I did not
  fully decode. [0xa12b8] (forced to >= 50 by the tile update) is read elsewhere; purpose unknown.
- Output range of the video decoder clamp table 0x978d0 (assumed 0..255 luma, as in the remake's decoder).
