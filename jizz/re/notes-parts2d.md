# Scope "parts2d": TBL light burst, "jizz" logo over a star warp, radial zoom, field lines, end picture

All addresses are offsets in `jizz_img.bin` (= near pointers = linear addresses in the emulator). "timer" =
xmpGetTimer (0x29f44), PIT clocks (1,193,182 Hz) since the music start. "time" = timer + [0x6542] (the part's start
value; parts set [0x6542] = -timer at their start). "tick" = player tick (60.387 Hz); music time of tick t =
(t - 434) / 60.387 s. Pixel coordinates refer to the original 320x200 frame; pixel (x, y) covers [x, x+1) x [y, y+1),
so its centre is (x + 0.5, y + 0.5). 16-bit pixels are 5:6:5 (the colour code is generic, see 3.5 and 4.2). `round()` =
x87 `fistp` = round half to even. All x87 maths runs with 24-bit precision (float32 is close enough: see 9).

Everything below was checked against the emulator, not only read (models in `jizz/re/models/parts2d/`, section 9):

| part | model | frames compared | result |
|---|---|---|---|
| 0x12013 TBL | `tbl_model.py` (bump, sparks, lines, glow, mask, rays, feedback, box filter, HSV, LUT) | 341 frames (the whole part) recorded with hooks, and the same frames in `full.frames` | every intermediate buffer and every captured page bit-exact |
| 0x1212a logo | `jizz_model.py` (+ `polar.py`) | 220 frames (the whole part) | star table, warp, brightness level, page and captured page bit-exact |
| 0x177bf radial zoom | `zoom_model.py` | 695 frames (the whole part; 625 of them visible in the capture) | bit-exact |
| 0x17c8a field lines | `field_model.py` | 335 frames (ticks 5975..6299, incl. the 171-frame speed ramp and the first flash) | bit-exact; the table formula matches 203,734 of 204,000 entries, the rest by +-1 (x87 rounding) |
| 0x18c25 end loop | `end_model.py` | all 318 captured frames | bit-exact |
| tables | `polar.py`, `rng.py`, `tbl_model.py` | polar table [0x16f4b] (0x16f4f), light table [0x654a] (0x6592), TBL spark tables (0x11dfe) | exact |

The models take the timer values from hooked runs (`run_hook.py`), so they also prove which timer read feeds which
variable.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | ticks | music time | pos (hex order:row) | fps in the capture | look |
|---|---|---|---|---|---|
| 0x12013 "TBL" | 434–1361 | 0.00–15.33 s | 00:00–01:00 | 22.5 (mostly 23.33 = 70/3, sometimes 17.5 = 70/4) | a moving light behind the letters "TBL" (dark silhouettes from a 320x200 mask) on a bumpy, light-lit surface; 40 rays shoot from points around the light to near the screen edges, smeared by a per-frame feedback; tinted azure / blue / violet by a per-frame colour pulse; fades in from black, ends in a white fade at 00:30 |
| 0x1212a "jizz" logo | 4047–4430 | 59.83–66.19 s | 08:00–09:00 | 35 (70/2) | a 16-bit "jizz!" picture (with a 1-pixel black outline) over a point-symmetric star-shaped polar warp of texture map5, darkened by a wobbling level; each of the events 810 / 820 / 830 changes the star (lobes, depth, direction) and gives a 0.88 s brightening pulse |
| 0x177bf radial zoom | 5200–5966 | 78.92–91.61 s | 0b:00–0d:00 | 70 (35 while a flash is applied) | a polar mapping of map5 / map7 that rotates and zooms with a radius-dependent wave; white flashes at 0b:00, 0c:00, 0c:10, 0c:20, 0c:30, each switching texture / zoom direction |
| 0x17c8a field lines | 5967–7502 | 91.63–117.06 s | 0d:00–11:00 | 70 (35 while a flash is applied) | map15 sampled by the signed sums of the angles (field lines of 3 + 3 opposite "charges") and of 3-lobed distances around 6 moving poles; white flash at the start and at 10:00 |
| 0x18c25 end loop | 9039–9356 | 142.50–147.75 s | 15:00–15:1c | 70 | the 16-bit end picture "~JIZZ~ TBL'97" (rainbow letters on black) fading in from white over 1.75 s; ends when the sync table ends |

Every frame of every part is `call 0x638b` (frame start: next page, sync event), drawing into the page
[0x2060], `call 0x651f` (flip at the retrace), `call 0x2058` (key test, never a key) — core scope.

## 2. Data used (read from the exe or generated at run time)

| what | where | format / formula |
|---|---|---|
| TBL work + feedback buffer | [0x652a] (main 0x18743), 0x20000 bytes | 0..0xffff: 8-bit work image, 320-byte rows (64000 used, 4-pixel lines may write up to 64640); 0x10000..: feedback history, same layout. 0x11dfe fills both with 0x03 |
| TBL spark speeds | 0x11666 + 4(i-1), i = 1..40 (dwords) | two int16: low = x speed, high = y speed. 0x11dfe: random call 2i-1 (0x141f6, state 0x141ea = 0,0,0 in the image: these are the first 80 calls of the generator) |
| TBL spark angles | 0x115c6 + 4(i-1) | two uint16 angles (16384 = one turn). Initial: random call 2i; rewritten every frame (section 3.3) |
| TBL constants | 0x11582 dword 0x5f0ff0, 0x11586 dword 0x84e770, 0x1158a word 158, 0x1158c word 98, 0x1158e word -90, 0x11590 word 13, 0x11596 double 2*pi/16384, 0x115ae 316.0f, 0x115b2 196.0f, 0x115b6 0.95f, 0x115c2 30.0f | code constants |
| TBL scratch | 0x1157a lx, 0x1157e ly, 0x115ba R, 0x115be 255/R (float), 0x1159e..0x115aa four corner angles, 0x11702/0x11842 + 4i dx/dy (float, later int x/y), 0x117a2/0x118e2 + 4i ray end offsets | |
| TBL mask "TBL'97" | [0x662b] = [0x6576] + 0x10000 (set in 0x12c50 at 0x12d22): the G plane area of the logo2 texture buffer, read as a **320x200 byte image** (stride 320) | 255 = background, 0 = inside the letters "TBL" (rows 71..124, columns 70..241), anti-aliased edges; faint fragments of "'97" at the left and right edges (values >= 209). Produced by the texture build of logo2 (textures scope) |
| TBL bump texture | [0x6572] = texture build 12 ("testshit"), planes G (+0x10000) and B (+0x20000) | 256x256 bytes; used as signed displacements (int8, mean 0, mostly within +-64) |
| light table | [0x654a] (0x10000 bytes), built by 0x6592 (core) | `L[j*256+i] = round((255 - sqrt((i-127)^2 + (j-128)^2))^4 / 2^24)`; peak 252 at (i,j) = (127,128); verified exact |
| HSV table | 0x1403a: 6 x 3 dword pointers into 0x14082..0x14084 (output R, G, B bytes) | sector -> which output gets V, X, P (section 3.5) |
| LUT | 0x11eef: 32 dwords | built by 0x11f73 every frame |
| polar table | [0x16f4b] (main 0x188c3, 0xc8000 bytes), built by 0x16f4f (core precalc) | 340 rows x 600 dwords (2400 bytes per row); for column i, row j: x = i - 299.5, y = j - 169.5; byte 0 = `round(atan2(y,x) / float32(2pi/256) * 2) & 255` (512 units per turn, so the byte repeats every half turn); byte 1 = `round(1.3f * sqrt(x^2+y^2)) & 255`; bytes 2, 3 = 0. Verified exact. **0x17b2f overwrites it** with the field table (section 6) |
| warp texture (logo part) | [0x12425] (main 0x1886f, 0x30000 bytes) = copy of map5 [0x655e] (texture build 7), converted in place to 16 bit by 0x13d73 | 256x256 words, index row*256 + column |
| zoom textures | [0x12425] = map5, [0x12429] = map7 ([0x6566], build 9), both converted to 16 bit | same |
| field texture | [0x12425] = map15 ([0x6586], build 17), converted to 16 bit | same |
| "jizz!" picture | [0x658e] (main 0x18827): 320x200 16-bit pixels, 0 = transparent | copied from [0x1242d] at 0x12d44 during the texture builds (textures scope); picture bounding box rows 33..158, columns 68..250 |
| end picture | [0x657a] (texture build 14 "logo1" buffer), read as 320x200 16-bit pixels | "~JIZZ~" and "TBL'97" in rainbow-shaded serif letters on black (0); bounding box rows 32..136, columns 89..219 |
| fade tables | [0x1130f], [0x11313], [0x11317] (0x2000 bytes each), built by 0x1131b (core) from the target colour [0x11303..0x1130b] | section 4.2 |
| page fill colour | [0x637c] = 0xffdf (main 0x18a93: OR of the three channel masks with green reduced to 5 bits) | used by 0x17737 |

The random generator 0x141f6 (core) is called by the parts in this scope only during the precalc (0x11dfe); the
sequence is fixed (zero seed), see `rng.py`.

## 3. Part 0x12013 "TBL" (0x11986 + 0x11e33 + 0x13f98 + 0x11f73)

### 3.1 Frame loop (0x12013, no set-up: the first frame is drawn at tick 434)

```c
static u8  pulse = 0;          // byte [0x12012]
static s32 fade  = -260;       // dword [0x1210d] = -0x104
for (;;) {
    frame_start();                                   // 0x638b
    draw_light();                                    // 0x11986: fills buf[0..62719] with levels 0..31 (3.2)
    pulse += 2;                                      // per frame, wraps at 256: 128 frames per cycle
    a = pulse < 0x80 ? pulse : 255 - pulse;          // triangle 0..127..0
    hue = ((a >> 2) * 0xe6 >> 8) + 0x9a;             // 154..181
    (cR, cG, cB) = hsv(hue, 0xff, 0xff);             // 0x13f98 -> bytes [0x11f6f], [0x11f70], [0x11f71]
    switch (event) {                                 // [0x6413]
    case 10: case 20: case 30: case 40: fade = 0; break;   // 20/30/40 never occur (not in the sync table)
    case 50:  if (fade < 0x100) fade += 0x17; break;       // fade to white: 0, 23, ..., 253, 276 (12 frames)
    case 100: return;                                       // this frame is not shown
    default:  fade += 9; if (fade >= 0) fade = 0;           // event 0: fade in, -251 ... -8, 0 (29 frames)
    }
    off = fade >> 3;                                 // arithmetic shift -> signed byte [0x11f72]
    build_lut(cR, cG, cB, off);                      // 0x11f73 -> 32 dwords at 0x11eef (3.5)
    for (k = 0; k < 64000; k++) page[k] = lut[buf[k]];          // 0x120b5
    memset(page + 194 * 640, 0, 6 * 640);            // rows 194..199 black (0x120de)
    flip();                                          // 0x651f
}
```
Events seen here: 0 from the start, 10 at 00:08 (tick 665, 3.83 s; the fade is already 0 by then), 50 at 00:30
(tick 1263, 13.73 s), 100 at 01:00 (tick 1360, 15.33 s).

### 3.2 draw_light (0x11986), in drawing order

```c
// light position, from the timer at the frame start ([0x6542] = 0 in this part)
T  = xmpGetTimer() + [0x6542];                                       // u32
a1 = (u16)(((u64)T * 0x5f0ff0) >> 32);   a2 = (u16)(((u64)T * 0x84e770) >> 32);
lx = round(158 + (-90) * sin((s16)a1 * 2pi/16384));                  // 68..248  [0x1157a]
ly = round( 98 +   13  * sin((s16)a2 * 2pi/16384));                  // 85..111  [0x1157e]
R  = (mask[ly*320 + lx] >> 4) + 15;                                  // glow radius 15 (over a letter) .. 30
kR = (float)255 / R;

// 1. bump-lit surface -> buf (0x11e33, buf = [0x11207] = [0x652a])
for (y = 0; y < 200; y++) for (x = 0; x < 320; x++) {
    k = y*256 + x;                         // 256-byte texture rows, 320 columns read: x >= 256 reads row y+1, x-256
    u = x - lx + 128;   v = y - ly + 128;
    if ((unsigned)u > 255 || (unsigned)v > 255) out = 30;            // window around the light (undisplaced)
    else out = L[((v + texB[k]) & 255) * 256 + ((u + texG[k]) & 255)];
    buf[y*320 + x] = max(out, 30);
}

// 2. 40 sparks (i = 40 down to 1): offset (dx, dy) on a Lissajous figure, and the ray end
for (i = 40; i >= 1; i--) {
    dx[i] = (float)(30.0f * sin(angx[i] * 2pi/16384));               // angx/angy: the uint16 pair at 0x115c2+4i
    dy[i] = (float)(30.0f * sin(angy[i] * 2pi/16384));
    ex[i] = 0;
    if (mask[(ly + round(dy[i]))*320 + lx + round(dx[i])] != 0) {    // no ray from a spark inside a letter
        // k = distance factor to the box [0,316] x [0,196] along (dx, dy): the edge is chosen by comparing
        // atan2(dy,dx) mod 2pi with the angles to the corners (316,196), (0,196), (0,0), (316,0) (all mod 2pi,
        // compared as float bit patterns): < BR -> right (316-lx)/dx; < BL -> bottom (196-ly)/dy;
        // < TL -> left -lx/dx; < TR -> top -ly/dy; else right.
        k = 0.95f * k;
        ex[i] = round(k * dx[i]);   ey[i] = round(k * dy[i]);
    }
}
// 3. short lines from the light to each spark point (all 40)
for (i = 40; i >= 1; i--) line(lx, ly, round(dx[i] + lx), round(dy[i] + ly));        // 0x1145c, colour 0xf9

// 4. glow: additive cone, saturating (0x11c73)
for (j = -R; j < R; j++) for (i = -R; i < R; i++) {
    p = &buf[(ly + j)*320 + lx + i];
    v = 255 - round(sqrt(i*i + j*j) * kR);  if (v < 0) v = 0;
    *p = min(255, *p + v);
}
// 5. the letters: multiply by the mask (0x11d12)
for (k = 0; k < 64000; k++) buf[k] = buf[k] * mask[k] >> 8;

// 6. rays from the spark point to 0.95 of the way to the screen edge, on top of the letters (0x11d30)
for (i = 40; i >= 1; i--) {
    if (ex[i] != 0) line(round(dx[i] + lx), round(dy[i] + ly), lx + ex[i], ly + ey[i]);   // ex == 0: no ray
    T_i = xmpGetTimer() + [0x6542];                                   // one read per spark
    angx[i] = (u16)(((s64)(s32)T_i * (s32)(speedx[i] << 8)) >> 32);   // signed imul, edx low word
    angy[i] = (u16)(((s64)(s32)T_i * (s32)(speedy[i] << 8)) >> 32);   // speed = s16 halves of 0x11666+4(i-1)
}
// 7. to 5 bits, feedback, 2x2 box (0x11dc6..)
for (k = 0; k < 64000; k++) buf[k] >>= 3;                             // 0..31
for (k = 0; k < 0x10000; k++) hist[k] = (7*hist[k] + buf[k]) >> 3;    // 0x12111 (esi=buf, edi=buf+0x10000, cl=3)
for (n = 0; n < 62720; n++)                                           // 0x11eb2 (edi=buf, ebp=0x10000): 196 rows
    buf[n] = (hist[n] + hist[n+1] + hist[n+320] + hist[n+321]) >> 2;  // (n = 0: (hist[1] + hist[321]) >> 2)
```
Notes:
- 0x12111 computes `((h << 3) & 0xff) + ((c - h) & 0xff)` in bytes, then `>> 3`; with h, c <= 31 that is exactly
  (7h + c) >> 3.
- The angles used in a frame were computed at the end of the previous frame: about 25,000 PIT clocks after that
  frame's light read, i.e. ~26,000 clocks (22 ms) before the light read of the frame that uses them; the 40 reads
  of one frame spread over ~370 clocks. The first frame uses the random start angles.
- The light is centred at (lx - 1, ly) in the bump (the table peak is at i = 127, j = 128) and at (lx, ly) in the
  glow (the cone covers lx-R .. lx+R-1).
- The bump is a displacement lookup, not a gradient: brightness = max(30, (255 - d)^4 / 2^24) with
  d = |(x - lx + 1 + g, y - ly + b)|, g = (s8)texG[k], b = (s8)texB[k] — provided the undisplaced offset is inside
  the 256 window and the displaced one does not wrap. The floor 30 is reached at d = 106 px, so the window cut
  (|x - lx| > 128) is invisible except where a displacement would bring a pixel back within 106 px; the wrap
  (u + g outside 0..255 is taken mod 256) lights about 500–750 pixels per frame on the far side (up to +100 in
  value): small specks of an 8-bit quirk (3.6).
- Lines are clipped all-or-nothing (any endpoint outside 320x200: no line); none of these is ever outside.
- The ray end box is 316 x 196: the screen minus the 4-pixel line thickness.
- Order matters for the look: spark lines and glow are masked by the letters, the rays are not.

### 3.3 Time bases

| variable | time base |
|---|---|
| lx, ly | timer at the frame start; lx period 2^32/0x5f0ff0 * 16384 / 1193182 = 9.466 s, ly 6.771 s |
| spark angles | timer, 40 reads at the end of the previous frame; speeds s * 256 / 2^32 * 1193182 / 16384 turns/s = 0.005..0.14 turns/s (s = int16 from the generator) |
| R | the mask at the light |
| feedback hist | **per frame** (weight 1/8 per frame) |
| pulse (colour) | **per frame**: +2 per frame, 128-frame cycle (5.5 s at 23.3 fps) |
| fade | **per frame** + event: +9 per frame (29 frames) from -260; at event 50 +23 per frame (12 frames) to 276 |

Capture (`full.flips`, part 12013): 345 flips, ticks 434–1360; frame-to-frame timer step 51136 clocks (3 retraces
at 70 Hz = 23.33 fps) 302 times, 68181/68182 clocks (4 retraces, 17.5 fps) 41 times; average 22.5 fps. The first two
frames read negative timer values (-19759, -21531: before the first player tick), which only moves the light in
frames that are still black. A remake should step this part on a virtual frame clock of 70/3 Hz (the capture's
rate) and replay it from the part start when seeking.

### 3.4 The 4-pixel line (0x1145c + 0x1122f, core helpers; parameters 0x11207..0x11227)

[0x11207] buffer, [0x1120b] pitch 320, [0x1120f] 1, [0x11213] width 320, [0x11217] height 200; endpoints
[0x1121b] x0, [0x1121f] y0, [0x11223] x1, [0x11227] y1. Endpoints are swapped so that y1 >= y0; dx = |x1 - x0|,
dy = y1 - y0; nothing is drawn if dx + dy = 0.
- dx >= dy: dx+1 steps along x (+-1); each writes 0xf9 at (x, y..y+3); a 16-bit accumulator adds
  (dy << 16) / dx (0xffff when dx = dy) and y += 1 on carry.
- dy > dx: dy+1 steps along y; each writes 0xf9 at (x..x+3, y); accumulator adds (dx << 16) / dy; x += 1 on carry
  (x1 > x0) or x -= 1 on borrow (x1 < x0; the borrow happens on the first step already).
So a line is 4 pixels thick along the minor axis, offset by +1.5 pixels down (x-major) or right (y-major); its
perpendicular width is 4 cos(angle to the major axis) = 2.8..4 px. Value 0xf9 = 249 becomes level 31 after `>> 3`.

### 3.5 Colours: HSV (0x13f98) and the LUT (0x11f73)

0x13f98 (AL = hue, BL = saturation, CL = value, all 0..255; returns AL, BL, CL = R, G, B):
```c
e = h * 6;  sector = e >> 8;  f = e & 255;  if (!(sector & 1)) f = 255 - f;
X = ((255 - (f * s >> 8)) * (v + 1)) >> 8;     P = ((255 - s) * (v + 1)) >> 8;
// sector 0: R=V G=X B=P   1: G=V R=X B=P   2: G=V B=X R=P   3: B=V G=X R=P   4: B=V R=X G=P   5: R=V B=X G=P
```
With s = v = 255 and hue 154..181 the colour runs (0,100,255) azure -> (0,0,255) blue -> (63,0,255) violet and
back. Pulse value p -> colour: p = 0..2 (0,100,255), 64 (0,16,255), 126..130 (63,0,255), 192 (0,22,255).

0x11f73 (inputs: colour [0x11f6f..0x11f71], signed offset [0x11f72]):
```c
for (l = 0; l < 32; l++) {
    b = clamp(l + off, 0, 31);
    for each channel (c, size, pos):   a = min(255, c + 8*b);   field = (a * b) >> (13 - size);   // size 5/6/5
    lut[l] = (R << 11) | (G << 5) | B;           // positions from the VBE masks (generic 5:5:5 / 5:6:5)
}
```
So level b shows colour min(1, c/255 + 8b/255) * b * 255 / 8192 of full scale per channel: dark levels take the
pulse colour, bright levels go to white. Fully faded to white (b = 31 everywhere): 0xf79e = (30/31, 60/63, 30/31).
The 30 floor of the bump becomes level 3 (a very dark blue background); inside the letters the level is 0.

### 3.6 What a high-resolution remake should evaluate exactly

| original | exact version |
|---|---|
| light position rounded to whole pixels | float lx, ly |
| bump: 256x256 table lookup, integer pixel positions, int8 displacements read nearest | brightness = max(30, (255 - d)^4 / 2^24) with d from bilinear-filtered displacements g, b (in 320-pixel units) at continuous positions; drop or keep the wrap/window specks (3.2 notes) |
| glow: integer cone over a 2R square, R an integer from the mask at the light pixel | cone max(0, 255 (1 - d/R)) over a disc; R from the filtered mask (or keep the integer steps: they are visible as a sudden change of glow size when the light crosses a letter edge) |
| 4-pixel lines with a 16-bit DDA, value 249, overwrite | anti-aliased strokes 4 px (of 320) along the minor axis (3.4), endpoints and spark offsets unrounded |
| work image `>> 3` (32 levels), feedback (7h + c) >> 3 with truncation | float levels; the truncation is a visible darkening: on the recorded frames the integer feedback averages 2.5 levels (of 31) below a float EMA of the same input; a float EMA with a bias of -1/3 level per frame (h = max(0, (7h + c)/8 - 1/3)) matches the mean (mean abs difference 1.2 levels; `ema_bias.py`) |
| 2x2 box of the history (shifts the picture by half a pixel up-left) | a 1-pixel (of 320) box/Gaussian blur, sampled at (x + 0.5, y + 0.5) |
| 32-entry LUT, 5/6-bit channels | continuous level b in [0, 31]: channel = min(1, c/255 + 8b/255) * b * 255/8192 |
| fade offset (fade >> 3) | fade / 8 (continuous), still stepped per frame |
| mask: 320x200 bytes, nearest | bilinear (it is anti-aliased already) |
| rows 194..199 black, rows 196..199 never box-filtered | keep the black band (6 rows of 200 = 3 %) as composition |

## 4. Part 0x1212a "jizz" logo (0x123df, 0x16dc8, 0x179f6, 0x1239b)

### 4.1 Algorithm

```c
frame_start(); fill_page(0xffdf); flip();         // 0x17737 with [0x637c]: one white frame (ticks 4047..4052)
fade_tables(0, 0, 0);                              // 0x1131b with [0x11303..0x1130b] = 0: target black
// 1-pixel outline around the picture P = [0x658e] (320x200 words), Q = copy of P in [0x12435]
for (n = 320; n < 320 + 60800; n++)                // rows 1..190
    if (P[n] == 0 && (Q[n-320] | Q[n-1] | Q[n+1] | Q[n+320])) P[n] = 0x0001;    // byte write: 0x0001
[0x15c3f] = 1;  engine_setup(esi = 0x15973, eax = 3);   // 0x15bb0 (engine scope): nothing is drawn with it here
copy map5 [0x655e] -> [0x12425];  to16(edi = [0x12425]);  // 0x13d73: (R>>3)<<11 | (G>>2)<<5 | B>>3, in place
start = xmpGetTimer();  [0x6542] = -start;         // time() = timer - start
A = 7; B = 25; S = 0; pulse_on = 0;                // dwords 0x1238e, 0x12392, 0x12396; byte 0x1239a
for (;;) {
    frame_start();
    if (event >= 900) return;
    if (event != 800) {                            // 810 / 820 / 830 (08:10, 08:20, 08:30)
        if (event == 810)      { A = 4; B = 0x23; S = -1; }
        else if (event == 820) { A = 7; B = 0x17; S = 0; }
        else                   { A = 5; B = 0x26; S = -1; }      // 830 (and any other value)
        event = 800;  t_ev = time();  pulse_on = 1;
    }
    t = time();
    C = (((u32)(5 * t)) >> 13) ^ S;                 // S = -1: C = -C - 1 (the star turns the other way)
    for (i = 0; i < 256; i++)                       // 0x123df -> [0x179f2]; fistp word at byte steps:
        T[i] = (s8) round(B * sin((float)(i*A + C) * 0.0245437f));    // only the low byte survives
    warp_add(page, entry0 = 0x29270, T, add = 0, tex = [0x12425]);  // 0x16dc8 (4.3)
    t = time();
    w = 5 * sin((t >> 6) * 2pi/16384) + 6 * cos(((((t >> 6) * 5) >> 2)) * 2pi/16384);
    lv = round(w) + 13;                             // 2..24
    if (pulse_on) { k = 0x3ff - (int)((u32)(t - t_ev) >> 10); if (k < 0) k = 0;
                    lv -= k >> 5; if (lv < 0) lv = 0; }               // 31..0 over 0.878 s after the event
    darken(page, lv);                               // 0x179f6 with the black tables (4.2)
    draw_picture(page);                             // 0x1239b (4.4)
    flip();
}
```

### 4.2 The colour fade helpers (core: 0x1131b, 0x179f6, 0x17979, 0x17999), as these parts use them

0x1131b builds, for each channel, 32 rows (fade levels k = 0..31) of 64 dwords (32 used):
`value(c, k) = ((c * (31 - k) + target * k) >> 5) << position`, c = 0..31 (5 bits). 0x179f6(EAX = level & 31)
rewrites every page pixel: each channel is reduced to 5 bits (red >> 11, green: bits 6..10 only, blue: bits 0..4),
mapped through row `level`, and recombined (green's lowest bit becomes 0). So: c' = (c * (31 - k) + T * k) >> 5.
Level 0 is not the identity (c * 31/32).

0x17979(EAX = K): t0 = time(); state = 0x3ff. 0x17999 each frame, while state != 0:
`d = time() - t0; state = (d < 0) ? 0 : max(0, 0x3ff - (u32)(((u64)d * K) >> 32)); darken(page, state >> 5)` —
the call with the final state 0 still darkens by 31/32 once, later frames are untouched.
With target white (31,31,31) this is a white flash that fades linearly over 1023 * 2^32 / K clocks.

### 4.3 The polar warp (0x16dc8, and its variant 0x16d43)

```c
// 0x16dc8(eax = tex16, ebx = byte offset of the window in the polar table, ecx = T (256 bytes), word [0x16cd1] = add)
for (y = 0; y < 200; y++) for (x = 0; x < 320; x++) {
    e = table[(ebx/4) + y*600 + x];               // ebx = 0x29270: row 70, column 140 = centred window
    a8 = e & 255;  r8 = (e >> 8) & 255;
    r8 = (r8 + T[a8]) & 255;                      // add bh, [ecx + bl]
    page[y][x] = tex16[((r8 << 8 | a8) + add) & 0xffff];     // add bx, imm16: angle + add_lo carries into the row
}
```
With the window at 0x29270, pixel (x, y) reads table (140 + x, 70 + y), i.e. X = x + 0.5 - 160, Y = y + 0.5 - 100
from the screen centre (continuous pixel coordinates). Per pixel, exactly:
```
theta = atan2(Y, X);   rho = sqrt(X^2 + Y^2)
col   = theta * 256 / pi            (mod 256; one texture width per half turn -> the picture is point symmetric)
row   = 1.3 * rho + B * sin(2pi * (A * col + C) / 256)        (mod 256)
pixel = map5(row, col)              (texel (row, col) = byte row*256 + col of each plane)
```
A lobes per half turn (2A around the centre); the star turns at 728.26 / A units of col per second
(C = 5t/8192: 728.26 units/s), i.e. 1.42/A turns/s; S = -1 reverses it.

### 4.4 The picture (0x1239b)

Words 9600..51199 of [0x658e] (picture rows 30..159) are copied to page rows 50..179 (+20 rows), skipping words
equal to 0. The outline pixels (0x0001, almost black) are opaque, so the picture has a 1-pixel dark outline
(4-connected dilation of its non-zero area). The picture itself sits at picture rows 33..158, so on screen at rows
53..178, columns 68..250.

### 4.5 Time bases and shortcuts

| variable | time base |
|---|---|
| C (star rotation) | timer: 5t/8192 units (t = timer - start) |
| A, B, S | event 810 / 820 / 830 (ticks 4144, 4241, 4336 = 61.44, 63.04, 64.62 s) |
| lv | timer: 5 sin(2pi t / 1048576) + 6 cos(2pi t / 838860.8) + 13 (periods 0.879 s and 0.703 s); minus a pulse of 31 -> 0 levels over 1,047,552 clocks (0.878 s) after each event |
| picture, outline | static |

Shortcuts: integer angle/radius bytes from the table and integer star offsets (T rounded per angle byte) ->
continuous col/row as above; nearest texel -> bilinear (wrapping); darkening in 5-bit channels -> float
c * (31 - lv)/32 (lv itself may stay an integer: it is a step function of time in the original, but a continuous
level is closer to the intent); the picture is a 320x200 bitmap (scale it filtered); the 1-pixel outline becomes a
dark ring of one original pixel width.

## 5. Part 0x177bf radial zoom (0x16d43)

### 5.1 Algorithm

```c
byte [0x16d42] = 4;                         // makes 0x16d43 index its table by the radius byte (5.2)
fade_tables(0x1f, 0x1f, 0x1f);              // 0x1131b: target white
copy map5 [0x655e] -> [0x12425]; copy map7 [0x6566] -> [0x12429]; to16 both (0x13d73)
start = xmpGetTimer(); [0x6542] = -start;
for (;;) {
    frame_start();
    if (event >= 0xcff) return;                                 // 0d:00 (3328)
    if (event != 0xb00) {
        old = event;  event = 0xb00;  flip = 0;
        if (old == 0xc00)      { tex = [0x12425] /*map5*/; K = 0x3d0900; }          // 0c:00, 0c:20
        else if (old == 0xc10) { tex = [0x12429] /*map7*/; K = 0x380310; flip = -1; } // 0c:10, 0c:30
        else                   { tex = [0x12425] /*map5*/; K = 0x28c0d0; }          // 2817 at 0b:00 (part start)
        [0x17971] = tex; [0x17975] = flip;
        timer_fade_start(K);                                    // 0x17979: white flash
    }
    t = time();
    p = (s32)(((s64)(s32)t * (s32)0xfdf99209) >> 32);           // imul: p = floor(-0.0079106 t)
    ph = p * 2pi/16384;                                         // float, then +0.008 per entry (accumulated)
    for (r = 0; r < 256; r++) { ph += 0.008f;                   // [0x179ee]
        Tz[r] = (u8) round(60.0f * (sin(ph) + 0.7f) + r); }     // [0x179e2] 0.7, [0x179ea] 60.0; byte table
    t = time();
    e = (u32)(3 * t);
    ro = ((e >> 15) & 255) ^ (flip & 255);   ao = (e >> 16) & 255;
    word [0x16cd1] = ro << 8 | ao;
    [0x16cc5] = 0; [0x16cc9] = 320; [0x16ccd] = 200;             // page offset, width, height
    warp_r(page, entry0 = 0x29270, Tz, [0x16cd1], tex);         // 0x16d43 (5.2)
    timer_fade_apply();                                          // 0x17999 (4.2)
    flip();
}
```

### 5.2 0x16d43

Same loop as 0x16dc8 (window [0x16cc9] x [0x16ccd] at page offset [0x16cc5], 2400-byte table rows) but the
instruction at 0x16d98 is patched: `mov cl, bl` (byte [0x16d42] = 0: table indexed by the angle) or `mov cl, bh`
(byte [0x16d42] = 4, the only use), and the table value **replaces** the radius byte (`mov bh, [ecx + T]`):
```c
row = Tz[r8];  index = ((row << 8 | a8) + [0x16cd1]) & 0xffff;  page = tex16[index];
```
Per pixel, exactly (X, Y, theta, rho as in 4.3, r = 1.3 rho):
```
col = theta * 256/pi + ao                                    (mod 256; ao = 3t/65536: 54.62 units/s = 0.107 turns/s)
row = r + 60 * (sin(phi + 0.008 * (r + 1)) + 0.7) + ro       (mod 256)
      phi = -t * 33975799 / 2^32 * 2pi/16384  (-3.62 rad/s)
      ro  = 3t/32768 (109.24 texels/s), or 255 - that when flip = -1 (zoom direction reversed)
```
The original adds `ao` to the angle byte with a carry into the row byte (row + 1 where a8 + ao >= 256): a one-texel
radial step along a line through the centre (the angle byte wraps every half turn) — drop it.

### 5.3 Time bases

| variable | time base |
|---|---|
| phi, ro, ao | timer (t = timer - part start) |
| texture, flip, flash | events: part start (2817 at 0b:00, tick 5200), 0c:00 (5584, 85.28 s), 0c:10 (5680), 0c:20 (5776), 0c:30 (5872, 90.05 s) |
| flash | timer: 31 -> 0 levels (white target) over 1.379 s (K = 0x28c0d0), 0.921 s (0x3d0900), 1.003 s (0x380310) |

Shortcuts: integer radius byte indexing Tz (the wave is evaluated per whole radius unit) -> continuous r; nearest
texel -> bilinear; 5-bit fade -> float.

## 6. Part 0x17c8a field lines (0x17b2f)

### 6.1 The field table (0x17b2f, once at the part start; overwrites the polar table [0x16f4b])

```c
// 600 x 340 dwords; x0 = -159.5 - 280*0.5 = -299.5, y0 = -99.5 - 140*0.5 = -169.5 (the stores at 0x17c3a /
// 0x17c3e modify those two static floats)
for (j = 0; j < 340; j++) for (i = 0; i < 600; i++) {
    x = i - 299.5;  y = j - 169.5;
    th  = atan2(y, x);
    ang = round(th * 10430.4f * 2);                               // [0x17c1a] 10430.4, [0x17c2e] 2: 65536 per half turn
    rad = round(45.6f * sqrt(x*x + y*y) / ((sin(3*th) + 1) * 0.5f + 0.4f));   // [0x17c2a], [0x17c22], [0x17c26]
    table[j][i] = (ang & 0xffff) | (rad << 16);                   // fistp of rad at [0x2006] overwrites the high word
}
```
rad is a 3-lobed distance: 45.6 rho / (0.9 + 0.5 sin 3 theta) (range 26..39215 in the table).

### 6.2 Frame loop

```c
frame_start(); fill_page(0xffdf); flip();                 // one white frame
copy map15 [0x6586] -> [0x12425]; to16; [0x17fc7] = [0x12425];     // texture address patched into the loop
build_field_table();                                       // 0x17b2f (ticks 5967..~5972 stay white)
start = xmpGetTimer(); [0x6542] = -start;
timer_fade_start(0x2d9686);                                // white flash, 1.233 s (fade tables still white from 0x177bf)
static u32 S = 0x400;                                       // [0x17ff5]
for (;;) {
    frame_start();
    if (event == 0x1000) { event = 0x1001; timer_fade_start(0x249f00); }   // 10:00 (tick 7119): 1.534 s flash
    t = time();
    if (S != 0x200) { S -= 3; if (S < 0x200) S = 0x200; }  // per frame: 1021, 1018, ..., 514, 512 (171 frames)
    tau = (u32)(3 * (u32)((t >> 4) * S)) >> 20;            // [0x2000]
    if (event >= 0x1100) return;                           // 11:00
    for (k = 0; k < 6; k++) {                              // poles
        X = round((sin(tau * fx[k]) + 1) * 124);           // [0x17c4e] = 124
        Y = round((sin(tau * fy[k]) + 1) * 67);            // [0x17c52] = 67
        off[k] = (Y * 600 + X + c[k]) * 4;                 // [0x17c56] = 600; patched into 0x17f8c + 6k
    }
    for (y = 0; y < 200; y++) for (x = 0; x < 320; x++) {
        n = y*600 + x;
        D = E1 + E3 + E5 - (0x12345678 + E2 + E4 + E6);    // Ek = table dword at n + off[k]/4; u32 wrap
        page[y][x] = tex16[((D >> 8) & 255) << 8 | (D >> 24)];   // shr bx,8 / rol ebx,8 / and 0xffff
    }
    timer_fade_apply();                                    // 0x17999
    flip();
}
```
Pole constants (float32 at 0x17c5a.., immediates in the code):

| k | sign | fx | fy | c |
|---|---|---|---|---|
| 1 | + | 0.01 | 0.011 | 2 |
| 2 | − | 0.017 | 0.013 | 10 |
| 3 | + | 0.012 | −0.0128 | 13 |
| 4 | − | −0.014 | 0.018 | 19 |
| 5 | + | 0.0187 | 0.016 | 12 |
| 6 | − | 0.0136 | 0.0155 | 15 |

The `mov eax, 0x12345678` at 0x17f85 is never patched: the placeholder is a real constant offset
(−0x1234 in the radius sum, −0x5678 in the angle sum).

### 6.3 Per-pixel formula

Screen pixel (x, y) reads table column X_k + c_k + x and row Y_k + y, i.e. the table point
(x + 0.5 − (300 − X_k − c_k), y + 0.5 − (170 − Y_k)): pole k sits at Q_k = (300 − X_k − c_k, 170 − Y_k) in
continuous screen coordinates (33..298, 36..170). For a pixel centre P = (x + 0.5, y + 0.5) let
(dx_k, dy_k) = P − Q_k, th_k = atan2(dy_k, dx_k), rho_k = |(dx_k, dy_k)|, s_k = +1 for k = 1, 3, 5 and −1 for
k = 2, 4, 6. Then
```
row (texture v) = ( sum_k s_k * th_k * 256/pi  - 0x5678/256 )              mod 256    // field lines of 3 + 3 charges
col (texture u) = ( sum_k s_k * 45.6 rho_k / (0.9 + 0.5 sin 3 th_k) - 0x1234 ) / 256   mod 256
pixel = map15(row, col)
```
(The borrow out of the angle words adds −4..+2 to the radius sum, at most 0.016 texel: ignore.) The angle sum is continuous
everywhere (each angle word wraps by a whole 65536 at the branch cut), so the "row" bands are the field lines of
three positive and three negative charges; the "col" coordinate changes slowly across them and gives the colour
variation along a line. Poles move on Lissajous paths: X_k = 124 (1 + sin(fx_k tau)), Y_k = 67 (1 + sin(fy_k tau)).

### 6.4 Time bases

| variable | time base |
|---|---|
| tau | timer, scaled by S: tau = floor(3 * floor(t/16) * S / 2^20). With S = 512: 109.24 tau/s (pole periods 2pi/f / 109.24 = 3.1–5.8 s) |
| S | **per frame**: 1021 in the first loop frame, −3 per frame, 512 from loop frame 170 on. The capture draws the loop at 35 fps while the start flash is being applied (0x179f6 over the page costs a second retrace; up to t = 1.25 s, S = 889 there), then at 70 fps: S reaches 512 at t = 3.05 s. tau = 0.21336 t S(t) (t in s) therefore grows at 218 tau/s at first, 237 at 1.25 s, 321 at 2.15 s, peaks at about 2.7 s (tau ≈ 337) and is 333 at 3.05 s, then runs at 109 tau/s. At 25 M instructions/s (17 fps) the ramp lasts 10 s. A remake should replay S on the capture's frame clock (35 Hz until the start flash is over, 70 Hz after) |
| flashes | timer: part start (1.233 s), 10:00 = tick 7119, 110.70 s (1.534 s) |

NOTES.md / PARTS.md list this part as not frame-rate dependent; it is, during the first 171 frames (S).

Shortcuts: the pole positions are rounded to whole pixels (X, Y integers) -> use the unrounded sin values; the
angle and distance come from a table sampled at integer offsets -> evaluate per pixel as above; the radius byte is
truncated (>> 24 = floor) and the angle byte too (>> 8) -> continuous u, v with bilinear wrapping texture reads.

## 7. End loop 0x18c25

```c
timer_fade_start(0x200b20);            // at 0x18c1b, after 0x10fda returns: 1.754 s; tables still white (0x177bf)
for (;;) {
    frame_start();
    if (event == -1) goto exit;         // 0x18c59 (sync table end, 15:1c)
    memcpy(page, [0x657a], 128000);     // the end picture
    timer_fade_apply();                 // 0x17999: white -> picture
    flip();
    if (key()) goto exit;               // 0x2058 never reports a key
}
```
Time base: the timer (fade from white, 1023 * 2^32 / 2,100,000 = 2,092,253 clocks); 70 fps. In the capture the
loop starts at tick 9039 (142.50 s) and the program exits at tick 9356. Shortcut: the 5-bit fade -> float.

## 8. Notes for the other scopes

- 0x16e36..: an unreferenced routine (no call, no pointer to it) that draws a polar bump effect from map3 [0x6556],
  [0x654a] and the TBL light variables [0x1157a]/[0x1157e] through the polar table at 0x29270. Dead code.
- 0x17b2f overwrites the polar table [0x16f4b] at 0x17cbd. The only readers of [0x16f4b] are 0x16cd3 (tunnel
  part, earlier), 0x16d43, 0x16dc8, the dead 0x16e36 and 0x17c8a, so nothing reads the polar table after that.
- 0x1212a calls the engine set-up 0x15bb0 (esi = 0x15973, eax = 3, [0x15c3f] = 1) but draws no object.
- 0x13d73 copies the planar texture to [0x12435] first (0x13f84) and converts from that copy: [0x12435] is
  clobbered (0x1212a uses it before, for the outline copy).
- The flips' "timer" column is the first timer read of a frame: TBL the light read, logo the star read (or the
  event read at 0x12285), zoom the radius-table read, field lines the tau read, end loop the fade read (none after
  the fade ends).

## 9. Models (`jizz/re/models/parts2d/`, see its README.md)

Hooked runs (`run_hook.py` = a copy of `run_jizz.py` with a `--hooks` module; `hook_tbl.py`, `hook_generic.py`)
record per frame the timer reads (with their return addresses) and the buffers at chosen code addresses; the models
recompute everything from the timer values and the snapshot tables and compare. The x87 work is modelled with
float32 rounding of products and sums, float64 sin/atan2 and round-half-even; this reproduced every integer
result in the five parts (only the 600x340 field table has 266 entries off by one).

Open points:
- The pictures [0x658e], [0x657a] and the mask [0x662b] come out of the texture builder; how (and where their data
  is in the exe) is the textures scope's job.
- The remake's frame clocks for the per-frame state are a choice: TBL 70/3 Hz, field-line ramp 35 Hz during the
  start flash and 70 Hz after it are what the capture (100 M instructions/s) shows.
