# Scope "p2": credits, the "C" logo, motion blur (DOPE part 2)

Addresses are `SEG:OFF` in `unpacked/p2.bin` (file offset = SEG*16 + OFF; code and variables in segment 0010, written
`[xxxx]` for `cs:[xxxx]`). At run time the image is loaded at segment 0F64 (CS = 0F74). "Retrace" = one call of the
per-retrace routine 0010:2580 (70.086 Hz); retrace r of p2 is the r-th call since the part started (retrace 1 =
capture frame 2713, 38.71 s). "Buffer row" = a row of the 320-byte-stride work buffers; the screen shows buffer
rows 1..200 (section 3), so screen row = buffer row - 1. Colour indices are p2's DAC palette (section 2.1).

The engine internals (transformation, sort, fillers) are the engine scope's (`notes-engine.md`); the int 8 handler,
the timer calibration and the exit are core's (`notes-core.md`). This file describes p2's script, data, frame
pipeline and how p2 parametrises the engine.

Checked (scripts in `models/p2/`, section 13):

- frame pipeline 360c (credit strip, blend through the 64 KB table, label, copy): a numpy model reproduces the
  buffer 5517 after 360c from the dumped 5517 / 4527 **byte for byte** in all 10 dumped frames (credits, text
  fade-out, disc move, "C" with backdrop, label sliding in, weak blur, heavy blur, white fade), and the capture frame
  equals rows 1..200 of that buffer on the frame of the copy;
- the blend table 3527:0000 built from the formulas below equals the table in memory in all 10 dumps
  (s = 24, 22, 246, ..., 11; before and after the clear at 3a5d);
- object placement and projection (camera record, pivot, per-object scale f, sine table, matrix order): projected
  and rasterised meshes (front faces by the engine's own back-face test) cover the engine's render buffer 4527
  to within edge pixels (12-402 pixels of 17,000-60,000 differ, all on polygon edges) in every dump, with and
  without the "C";
- the camera / pivot / label / fade state stepped per retrace from the script equals the emulator's state at all
  7309 retraces once the measured step starts (section 9.2) are used;
- the bump-map address formula (7.3) against all 86,608 bump reads of the discs' filler in frames 2705-2740;
- env map selection per object (left / right half) by sampling vertices; white fade against the capture DAC;
- the engine's 16-bit back-face test (7.4): with it the projected coverage matches the engine's render buffer at
  retrace 7281 (50 / 66 pixels differ), with a correct 32-bit test 3845 pixels differ.

---------------------------------------------------------------------------------------------------------------------

## 1. What is on screen, and when

Times are p2 retraces (nominal, i.e. the sum of the script's N; section 9.2 gives the few steps that start later
in the capture) and capture frames. Step numbers are the script's `call 371e` in order (section 9.1).

| steps | retraces | capture frames | what is on screen |
|---|---|---|---|
| (set-up) | — | 2705-2712 | p1's last picture, then mode 13h (black, frame 2709), DAC set, timer calibrated |
| 0 | 0-437 | 2713-3150 | two blue pentagonal "discs" (one mesh, top and bottom) swinging around the vertical axis, offset sideways (pivot 6000), the camera backing off (vz 4000); heavy motion blur (s = 24); the credit band rows 87..118 is black |
| 1-2 | 438-1067 | 3151-3780 | "jmagic code" fades in (white, band at screen rows 87..118) |
| 3 | 1068-1557 | 3781-4270 | "jugi music" (each new strip fades in over the old text, section 5) |
| 4 | 1558-2047 | 4271-4760 | "jugi graphics" |
| 5 | 2048-2677 | 4761-5390 | "\"dope\"" (band moved up to screen rows 79..110) |
| 6 | 2678-2817 | 5391-5530 | the text fades out (band forced to 0) |
| 7-21 | 2818-2862 | 5531-5575 | band gone; the discs' pivot steps 6000 → 200 in 15 steps of 3 retraces: the discs slide onto the axis |
| 22-24 | 2863-3122 | 5576-5835 | the camera backs off (vz 6000, then still) |
| 25-26 | 3123-3332 | 5836-6045 | the "C" (Complex logo, its own mesh) appears between the discs; the backdrop picture (the stand: pillars and a ring) appears behind everything; the discs switch to the second env map |
| 27 | 3333-3542 | 6046-6255 | camera flies in (vz -5500) and starts rolling (vc -36) |
| 28 | 3543-3682 | 6256-6395 | label "phong BLUR motion" slides in from the left edge (75 retraces); roll -56 |
| 29-31 | 3683-4522 | 6396-7235 | flight in (vz -3800), then rotation only; close-ups of the "C" |
| 32-87 | 4523-5194 | 7236-7907 | blur weakens: s = 26, 30, ..., 246 every 12 retraces |
| 88 | 5195-5404 | 7908-8117 | label slides out (76 retraces) |
| 89-148 | 5405-6124 | 8118-8849 (capture) | camera backs off (vz 3000); blur strengthens again: s = 256, 252, ..., 20 every 12 retraces |
| 149-150 | 6125-7050 | 8850-9775 (capture, steps 12 late) | pitch starts (va 10, vc -76); from step 150 the camera flies in (vz -2400) |
| 151 | 7051-7260 | 9776-9985 (capture) | s = 11 (very heavy blur), va 20, vc -86 |
| 152 | 7261-7295 | 9986-10021 (capture) | fade to white: every DAC value + 1 per retrace (section 8); then the exit sets the whole DAC to 63 (frames 10023-10025) and p3 starts from black |

Total 7296 retraces nominal; the capture takes 7309 (steps 99-152 start up to 12 retraces late, section 9.2;
the "nominal" retraces in this table are then 12 too small at the end), 104.4 s with set-up and hand-over.
Capture frame of retrace r = 2712 + r.

---------------------------------------------------------------------------------------------------------------------

## 2. Data

### 2.1 Pictures, maps, palette (all read from `unpacked/p2.bin`)

"RIX3" = ColoRIX picture: `"RIX3"`, u16 width, u16 height, u16 0x00AF (palette type AF, storage 0), 768 bytes of
6-bit palette, then width*height bytes, rows top to bottom. All pixel data here is indices into p2's DAC palette.

| what | where (file offset) | format | made how | used how |
|---|---|---|---|---|
| credit strips | RIX3 at 0451:0000 (0x4510), pixels 0451:030A, 320x128 | 4 strips of 320x32: "jmagic code", "jugi music", "jugi graphics", "\"dope\"" (strip n = rows 32(n-1)..32n-1, pixels 0451:030A + (n-1)*0x2800) | hand-made (anti-aliased white text, 13 levels: values 0x80 + {0,5,10,16,21,26,31,37,42,47,52,56,62}; 0x80 = background) | written each drawn frame into the work buffer's band and mixed through the blend table: the value - 0x80 (0..62) is the text intensity x; the fade is in the blend (section 5). A redraw: a 320x128 (or 4 x 320x32) single-channel coverage mask, value 0..62/63 |
| label "phong BLUR motion" | RIX3 at 2BD2:0000 (0x2BD20), pixels 2BD2:030A, 70x44 | indices 240..254 (grey ramp, palette entries 240..255 at 2BD2:02DA, 48 bytes) | hand-drawn (grey box, "phong" small, "BLUR" big, "motion" small) | copied as-is (opaque, all 70x44 pixels) into the output buffer after the blend (section 6). A true-colour redraw replaces it directly |
| backdrop (the stand) | raw 320x200 at 0EC3:0000 (0xEC30) | indices 0..107 of the blue ramp, row 0 = screen row 0 | pre-rendered art (pillars on a floor and a ring, dark blue), made outside the demo like a hand-drawn picture | from step 25 on copied into the render buffer as the background of every frame (instead of clearing to 0), then goes through the blend like the 3D scene. A true-colour redraw replaces it (it must stay a static backdrop that does not move with the camera) |
| env maps | RIX3 at 6507:0006 (0x65076), pixels 6538:0000 (0x65380), 256x128 | two 128x128 maps side by side, stride 256: left half = 6538:0000, right half = 6540:0000 (= 6538:0080); values 1..127 (blue ramp) | generated (radial gradients): left = cone falling about 0.88 per texel from about 95 around (row 53, col 48), with a sharp spot (127 at the centre, radius about 5); right = cone falling about 1.0 per texel from 127 at (row 91.5, col 84.5) | indexed by the rotated vertex normal (section 7.3); a remake can filter the stored map (bilinear / bicubic) or evaluate the two radial profiles; no redraw needed |
| bump map | raw at 23D1:0000 (0x23D10), 128x128 used, stride 256 | one byte per texel: high nibble = column offset index, low nibble = row offset index (offset table 0010:0000, section 7.3); right half (cols 128..255) = 0x88, never read | generated (gradient of a rough stone-like height field; the height field itself is not in the file) | perturbs the discs' env-map lookup. **Rows 0..10 and 16 bytes of row 11 are not bump data**: they overlap the end of the "C" mesh (its normals and map coordinates, 0x23D10..0x2481F); the discs read them near one corner of the pentagon (section 7.3) |
| bump offset table | 0010:0000 (file 0x100), 256 words | signed word = row offset*256 + column offset | generated | see 7.3 |
| sine table | 0010:0410, 512 words (quarter wave, 32767 sin(2 pi i/2048), within 1) | extended by 19CE to 2048+512 entries | generated | engine (angles) |
| DAC palette | 6507:0010 (0x65080), 720 bytes = colours 0..239 (the env-map picture's own palette) + 2BD2:02DA, 48 bytes = colours 240..255 (the label's palette) | 6-bit RGB | — | set once at the start (377F) and only changed by the white fade |

`models/p2/dump_pictures.py` writes the hand-made ones with the DAC palette (credits: the same entries as its own
palette 128..190) to `dump/pictures/p2-credits.png`, `p2-label-blur.png`, `p2-background.png`, and the generated
maps to `models/p2/out/`.

The palette (6-bit): 0..127 a ramp from black through dark blue to white (0 = (0,0,0), 32 = (4,2,17),
64 = (16,16,32), 96 = (36,36,47), 127 = (63,63,63); blue leads, red and green follow); 128..191 a grey ramp
(entry 128 + k = grey k - 1 for k = 1..52, k for k = 0 and 53..63); 192..239 a green-yellow-red ramp (from the env-map picture; not used by p2);
240..255 the label's grey ramp 0, 4, 8, ..., 63. The remake reads the table (6507:0010) instead of fitting it.

### 2.2 Meshes (engine format; see notes-engine.md for the details)

Header words: [0] vertex count, [2] face count, [4] vertex list offset (3 x s16 per vertex), [6] face list offset
(8 bytes: flag word, three vertex indices * 4), [8] vertex normal list (3 x s16, length about 27558), [0A]
per-vertex map coordinates (2 x u16: u, w).

| object | segment (file) | vertices / faces | shape |
|---|---|---|---|
| discs | 1E71:0000 (0x1E710, ends 0x2065F) | 250 / 500 | two pentagonal slabs in the x-z plane (circumradius about 8000, x -7932..7889, z -6862..8191), y = ±3383..±6430, each with a raised oval ring on its inner face (facing the other slab); symmetric in y |
| "C" | 2121:0000 (0x21210, ends 0x2481F) | 432 / 864 | a tube bent into a "C" (x -8191..4863, y ±7580), bowed in z (±2200), open towards +x |

All face flag words are 0. The discs' map coordinates are planar: u follows x, w follows z (the bump map is
projected from above onto both slabs, rows 2*((u>>8)&63), columns (w>>7)&127, section 7.3).

---------------------------------------------------------------------------------------------------------------------

## 3. Buffers and the frame loop

| segment | file offset | what |
|---|---|---|
| 4527 | 0x45270 | render buffer ("new"): the engine draws the frame here, 320 x 202 rows used |
| 5517 | 0x55170 | output buffer ("old"/accumulator): the blended picture, copied to the screen |
| 3527 | 0x35270 | blend table T, 256 x 256 bytes, T[old*256 + new] (zero in the file) |

Frame loop 371E ("run CX retraces"): `[192c] = 0; [1928] = 0; [371c] = cx; do { 354a; 3585; 3310; 360c;
[192c]++; if key: exit 0xFF } while ([1928] < cx)`. 354a/3585/3310 transform, sort and draw both objects into
4527 (engine). 360c:

```c
// 0010:360C, once per drawn frame
while (retrace_flag[1924] == 0) {}  retrace_flag = 0;            // at most one frame per retrace
// 1. the credit band: 32 rows of the render buffer from byte offset BAND = [3626] (self-modified immediate of
//    "mov di, 6E00" at 3625; 0x6E00 = buffer row 88, 0x6400 = row 80 from step 5)
switch (strip = (s16)[1904]) {
  case -2: break;                                                 // no band: the scene shows
  case -1: memset(new + BAND, 0x00, 0x2800); break;
  case  0: memset(new + BAND, 0x80, 0x2800); break;
  default: memcpy(new + BAND, seg0451 + (u16)(strip*0x2800 + 0xDB0A), 0x2800);  // = 0451:030A + (strip-1)*0x2800
}
// 2. blend, buffer rows 0..199
for (i = 0; i < 64000; i++) old[i] = T[old[i]][new[i]];
// 3. the label, into the output buffer (section 6)
if ((s16)[1955] >= 0) draw_label([1955]);
// 4. show buffer rows 1..201 (64320 bytes) at A000:0000; the screen shows 200 of them
memcpy(A000:0000, old + 320, 64320);
// 5. clear the render buffer rows 1..201 for the next frame, or put the backdrop there
if ([1947] == 1) memcpy(new + 320, seg0EC3, 64320);  else memset(new + 320, 0, 64320);
```

Consequences:

- Screen row y shows buffer row y + 1. The engine projects to buffer rows with the centre at (160, 100) (screen
  row 99) and rejects triangles entirely above row 1 or below row 201 (clipping: engine scope).
- Buffer row 200 of the output buffer is never blended (the blend covers rows 0..199), so **screen row 199 is black
  for the whole part** (checked in the capture). Buffer row 0 is blended but never shown; render-buffer row 0 is
  never cleared or drawn (stays 0).
- The backdrop copy reads 64320 bytes from 0EC3:0000: picture rows 0..199 land on buffer rows 1..200 (screen rows
  0..199), the extra 320 bytes (mesh data) on row 201, which is never shown.

---------------------------------------------------------------------------------------------------------------------

## 4. The blend table (3527:0000) and the motion blur

The table is built in three pieces; it is the whole of p2's colour mixing (motion blur, text fades, text over the
scene).

```c
// 0010:1998, once at the start: entries involving the text / grey ramp (indices >= 128)
for (y = 0; y < 64; y++) for (x = 0; x < 128; x++) {
    v = x*0x60 + y*0x1A0;  v = (v >> 1) + (v & 1);  b = 0x80 | (v >> 8);     // 0x80 + floor(round((96x+416y)/2)/256)
    T[y][0x80 + x] = b;  T[0x80 + y][x] = b;  T[0x80 + y][0x80 + x] = b;
}
// 0010:1967, with s = [1965] (called by the script whenever s changes): the scene's motion blur
for (old = 0; old < 128; old++) for (new = 0; new < 128; new++)
    T[old][new] = (new*s + old*(256 - s)) >> 8;                       // fits 16 bits (s <= 256)
// 0010:3A5D (script, before step 22): T[0][0x80] = 0; T[0x80][0] = 0; T[0x80][0x80] = 0;
//                                      T[0x80..0xFF][*] = 0   (32 KB)
```

Everything else stays 0 (the segment is zero in the file): T[64..127][128..255] and T[192..255][*].

So, per drawn frame and pixel, with old = the previous output and new = the frame just rendered:

- scene over scene (both < 128): `out = floor((s*new + (256-s)*old)/256)`: an exponential moving average with
  weight s/256 for the new frame. s values: 24 from the start to step 31, then 26 → 246 in steps of 4 every 12
  retraces (steps 32-87, the blur dies away while the label is shown), 246 during step 88, then 256 (no blur) →
  20 in steps of 4 every 12 retraces (steps 89-148), 20 (steps 149-150), 11 (steps 151-152).
- The floor makes the average stick below a static value: a constant pixel x converges to x - k with
  k < 256/s, i.e. up to 10 shades darker at s = 24, 12 at s = 20, 23 at s = 11 (of 127). A remake that blends in
  float converges to x: the blurred picture is slightly brighter than the original's (8-bit artefact).
- Time base: **per drawn frame**, not per retrace. At 70 fps (most of the part) that is one blend per retrace;
  in the heavy shots the capture draws fewer frames (section 9.3) and the trails become shorter in time and
  stroboscopic (separate copies of the moving object). A remake should blend per retrace in time:
  `a = 1 - (1 - s/256)^(dt * 70.086)` for a frame of dt seconds, and render enough sub-frames (at least 70 per
  second; more gives smooth trails) for fast motion. Known difference: the capture's frame drops.

---------------------------------------------------------------------------------------------------------------------

## 5. The credits (steps 0-6, retraces 0-2817)

The band (32 render-buffer rows from BAND) gets, every drawn frame, the strip given by [1904] (section 3), so the
3D scene never shows in the band while [1904] != -2 (the discs stay above and below it in the capture). The band
pixels go through the 1998 part of the table:

- the band starts black: the output buffer is 0 at the start; T[0][0x80] = 0x80 (grey 0);
- once a band pixel is in the grey ramp (old = 0x80 + y, y = 0..63) it stays there:
  `y' = floor(round((96*x + 416*y)/2) / 256)` with x = strip pixel - 0x80 (0..62), i.e.
  **y' ≈ y + (x - y)*48/256 per drawn frame**: the text fades in towards x and, when the strip changes, the old
  text fades out while the new one fades in (a cross-fade with the same rate). The integer steps stop it early:
  full-white text (x = 62) settles at y = 59 after about 20 frames; the fade-out with x = 0 (`y' = floor(208y/256)`)
  reaches 0 after about 16 frames.
- Displayed colour: grey ramp entry 0x80 + y, about (y, y, y) / 63 (white text on black).
- Steps: 0 strip 0 (black band, 438 retraces), 1-2 strip 1 "jmagic code" (630), 3 strip 2 "jugi music" (490),
  4 strip 3 "jugi graphics" (490), 5 strip 4 "\"dope\"" with BAND = 0x6400 (buffer rows 80..111, screen 79..110;
  630), 6 strip -1 (zeros: x = 0, fade out; 140), from step 7 strip -2 (band off).
- Band rows on screen: 87..118 for strips 0-3, 79..110 for strip 4. When the band moves up (step 5), rows
  111..118 keep their grey-ramp text and, now getting the scene (0 there), fade to grey 0 with 208/256 per frame;
  rows 79..86 enter the grey ramp from the scene through T[old<64][0x80+x].
- Latent quirk, not visible in the capture: a band pixel in the grey ramp that gets a scene pixel x (< 128)
  becomes `0x80 + floor(round((96x + 416y)/2)/256)` (a dim grey copy of the scene, and indices >= 192 for bright
  scene pixels, which become 0 in the next frame); this would happen in steps 7-21 (band off, table not yet
  cleared) if the scene crossed the band rows. The clear at 3A5D (step 22) turns all grey-ramp pixels into 0 in
  the next frame.

Remake: per pixel, keep the text intensity y (float) and step `y += (x - y) * (1 - (208/256)^(dt*70.086))` per
frame (time base: drawn frames = retraces in this section: the capture draws 70 per second here), x from the strip
mask (0..62/63; a redraw gives a high-resolution mask), and show `grey(y)` in the band rows, black elsewhere in the
band; no stop at 59 (draw at full white). From step 7 the band area is ordinary scene again.

---------------------------------------------------------------------------------------------------------------------

## 6. The label "phong BLUR motion"

Per retrace (2580): `[1955] += [1957]; if ([1955] < 0 || [1955] >= 75) [1957] = 0;` ([1955] starts at -1,
[1957] at 0). The script sets [1957] = 1 at step 28 (retrace 3543): the position runs 0, 1, ..., 75 (75 retraces)
and stops; [1957] = -1 at step 88 (retrace 5195): 74, ..., 0, -1 (76 retraces), then it is not drawn.

Per drawn frame, after the blend (360C, 36A2):

```c
p = (s16)[1955];
if (p >= 0) {
    x0 = 0;  w = p;
    if (p > 70) { x0 = p - 70; w = 70; }          // slides 5 more pixels to the right at the end
    for (k = 0; k < 44; k++)                       // buffer rows 9..52 = screen rows 8..51
        memcpy(old + (9 + k)*320 + x0, label + k*70 + (70 - w), w);   // the rightmost w columns
}
```

So the label comes out of the left screen edge (its right part first), 1 pixel per retrace, comes to rest at
x = 5..74, screen rows 8..51 (checked in the capture), and goes back the same way. It is opaque (all 70x44 pixels,
grey box included).

It is written into the output buffer, so it is also the "old" of the next blend: T[240..254][*] = 0, so wherever
the label was and is not drawn again (its left 5 columns after it moved right, the columns it uncovers while
sliding out) the pixel becomes 0 (black) and the blurred scene grows back from black. The capture shows this dark
strip left of the resting label. Exact equivalent for a remake: where the label was drawn in the previous frame,
the blended value of this frame is black (not a blend); then the label is drawn again on top, so only the pixels
it no longer covers show the black, from which the blur grows back.

Time base: retraces (position), drawn frames (when it is drawn; at 70 fps identical).

---------------------------------------------------------------------------------------------------------------------

## 7. The objects

### 7.1 Object records (0010:1810, 0x3A bytes each; list ends with a record whose word [0] = 0)

| offset | meaning | camera 1810 | discs 184A | "C" 1884 |
|---|---|---|---|---|
| +00 w | mesh segment (FFFF = camera record, not drawn) | FFFF | 1E71 | 2121 |
| +02 w | non-zero: not moved by 2580 and not drawn | 0 | 0 | FFFF until step 25, then 0 |
| +04 w | projection scale f (s16, see 7.2) | — | 4000 (script; 3200 in the file) | 1300 (script; 1200 in the file) |
| +06/+0A/+0E dw | position x, y, z | 1816/181A/181E | 0 | 0 |
| +12/+16/+1A dw | velocity per retrace | 1822/1826/182A | 0 | 0 |
| +1E/+20/+22 w | angles a, b, c (65536 = one turn) | 182E/1830/1832 | 0 | 0 |
| +24/+26/+28 w | angular velocity per retrace | 1834/1836/1838 | 0 | 0 |
| +2A w | low byte added to [194F] (env map select), high byte to [194D] (bump map select) while the object is drawn | — | 0 | 0x0101 (18AE, set by the script) |
| +2C w | [1935]: back-face cull sign (0 = keep faces with non-negative 2D cross product) | — | 0 | 0 |
| +2E/+32/+36 dw | pivot: added to every vertex before the rotation | — | x = [1878] (6000, then 5800 ... 0), 0, 0 | 0 |

Per retrace (2580) every record with [+2] == 0 gets `pos += vel; angle += angular velocity` (32-bit and 16-bit
wrapping adds). Only the camera has non-zero velocities in p2, so all motion is the camera record's.

### 7.2 Placement and projection (engine 348B, 1D86, 1C60, with p2's values)

For each drawn object, with cam = the camera record:

```c
ang  = (s16)(obj.ang + cam.ang) >> 4;     // per axis; sine index (ang >> 1) & 0x7FF: 2048 steps per turn
M    = Rz(c) * Rx(a) * Ry(b);             // 15-bit entries; rows:
       // ( cb cc - sb sa sc,   ca sc,   sb cc + cb sa sc )
       // (-cb sc - sb sa cc,   ca cc,  -sb sc + cb sa cc )
       // (-sb ca,             -sa,      cb ca            )
p    = v + pivot;                          // vertex (s16) + pivot
X = M0.p + (obj.x + cam.x) << 8;   Y = M1.p + (obj.y + cam.y) << 8;   Z = M2.p - (obj.z + cam.z) << 8;
sx = 160 + round((X*f/Z) / 8);    sy = 100 + round(3*(Y*f/Z) / 32);    // buffer coordinates, Z < 0
key = ((Z >> 16) * f >> 14) + 0x4000;      // per vertex; a face's sort key is the sum of its 3 vertices'
```

So in vertex units the camera sits at distance cam.z/128 from the origin on the view axis (cam.x = cam.y = 0 in
p2), the whole scene turns with the camera angles (yaw b about the y axis first, then pitch a about x, then roll c
about the view axis), and the screen aspect factor is y = 0.75 x. f only scales the projection:
**the "C" is drawn with f = 1300 against the discs' 4000**, i.e. at 0.325 of its modelled size around the screen
centre (equivalent to scaling it by 0.325 in view-space x and y, not z), and its sort keys are scaled by the same
factor: faces are drawn in increasing key order (far first), and the C's keys are 0.325 x its true depth, so the C
sorts as if three times nearer and is drawn over the discs unless a disc face is closer than 0.325 x the C's depth.
A remake with a depth buffer gets the same picture by giving the C a projection with x, y scaled by 1300/4000 and
its depth scaled by the same factor (or by drawing the C after the discs where they overlap).

The discs' pivot x (record +2E = [1878]) moves them sideways by 6000 vertex units before the rotation, so while
the yaw b turns they swing around the axis; steps 7-21 step it 5800, 5400, ..., 200 (every 3 retraces) and step
22 sets 0.

### 7.3 Textures (engine fillers 2660 / 2750 / 2EA0 / 2F50 with p2's data)

Per face, the engine picks two segments: env map = word [0010:0400 + 2*((face.flags + [194F]) & 3)] and bump map
= word [0010:0408 + 2*(((face.flags >> 8) + [194D]) & 3)]; the table at 0050:0000 holds 6538, 6540, 6D38, 6540 and
23D1, 0, 0, 0. [194F] and [194D] start at 0, the "C" adds 1 to both while it is drawn, and the script decrements
[194F] at step 25 (0xFFFF; the "C"'s byte add makes it 0xFF00). Result:

| object | until step 25 | from step 25 |
|---|---|---|
| discs | env map left half (6538), bump map 23D1 | env map right half (6540), bump map 23D1 |
| "C" | (hidden) | env map left half (6538), no bump map |

(6D38 and the other table entries are never selected in p2.) Checked by sampling the render buffer at projected
vertices (`texsel_check.py`).

- Env map coordinates per vertex (1C60): `col = 64 + ((N.M0) >> 24)`, `row = 64 + ((N.M1) >> 24)` with N the
  vertex normal (length about 27558, so the lookups stay within about 54 texels of (64, 64)); interpolated
  linearly across the polygon in 8.8 (packed into one 32-bit register: row in the low word, column in the high
  word), texel = map[row*256 + col] (nearest).
- Bump (discs only): the per-vertex map coordinates (u, w) from the mesh are interpolated the same way; per pixel
  `b = bump[((u >> 7) & 0x7E | w >> 15) * 256 + ((w >> 7) & 0x7F)]` — the row uses only bits 8..13 of u, shifted
  left by 1 (a `shld ebx, ecx, 9` on the packed pair), so only the even rows 0, 2, ..., 126 are read and u has half
  the resolution of w; then `texel = env[(row*256 + col + offs[b]) & 0xFFFF]` with `offs[b]` = the signed word at
  0010:0000 + 2b. Decoded: column offset = (-7, -6, -5, -4, -3, -2, -1, 0, 0, 0, 1, ..., 6)[b >> 4], row offset =
  the same list [b & 15], except that for b >> 4 = 7 or 8 the row offset is one less (-8 ... 5): a quirk of how
  the words were made (0x88, the "flat" value, is offset (0, -1)).
- The bump map's rows 0..10 hold the "C" mesh's normals and map coordinates (2.1); they are read for u >> 8 & 63
  <= 5, i.e. near the pentagon corner at x ≈ -8000 of both slabs: noisy shading there. A remake reading the map
  from 23D1:0000 reproduces it; it is part of the original's look (not a resolution artefact), but harmless to
  replace by the flat value 0x99 (offset (0, 0)) if it looks wrong at high resolution — my choice.
- A remake: env map by the interpolated view-space normal (per pixel) with bilinear filtering, plus the bump
  offset (column, row) from the bilinearly filtered decoded bump map, added to the env coordinates in texels. Bump
  map position: row = (u & 0x3FFF) / 128, column = (w & 0x3FFF) / 128 (0..128, wrapping; the original reads row
  2*floor(row/2) and column floor(column)).

### 7.4 The 16-bit back-face test in p2's close-ups (engine 3377)

The engine's visibility test is the 2D cross product of the projected triangle,
`(x1-x0)*(y2-y0) - (x2-x0)*(y1-y0) >= 0` (record +2C = 0), computed with 16-bit `imul` and `sub`: for triangles
whose doubled screen area exceeds 32767 px² (including parts off screen) the sign can be wrong, so big front
faces vanish and big back faces are drawn. p2 is the part where this shows: in the close-ups (camera near the
discs and the "C") large disc faces are dropped, and the faces behind them, the backdrop or black show through.
`cull_frames.py` counts 721 of the 6836 drawn frames (from retrace 3924 on: steps 29-31, 89-148, 150-152) with
more than 500 px² of wrongly dropped or wrongly drawn faces; at retrace 7281 (`cull_check.py`) two front faces of
together 21160 px² are dropped and 3845 pixels show what is behind them instead (the model with the 16-bit test
matches the engine's render buffer there, a correct test does not). It is an integer-overflow artefact (a remake
renders correctly and lists it under "Known differences"); reproducing it would mean evaluating the test in 16
bits on the 320x200 vertex positions. (The engine scope documents the routine; this is its effect in p2.)

The filler's trivial reject (all three vertices with x outside 0..319, or the triangle entirely above row 1 or
below row 201) dropped no visible face in the 10 dumps (`reject_check.py`).

---------------------------------------------------------------------------------------------------------------------

## 8. Palette and the fade to white

The DAC is written once at the start (377F: after `int 10h AX=13h` and a wait for the vertical retrace, 768 bytes
from the copy at cs:1F10 = 6507:0010 (720 bytes) + 2BD2:02DA (48 bytes)); the overscan colour is set to 0
(attribute register 11h). Nothing changes it until the last step.

Step 152 sets [193F] = 1 (retrace 7261 nominal, 7273 in the capture). Per retrace (2580) while [193F] == 1:

```c
ax = [1941];                          // starts at 0
if (ax >= 0x3F00) { [193F] = 0; [1941] = 0x3E7F; }   // not reached in p2 (35 retraces)
[1941] += 0xFF;
// 2550(ax, dx = 0x100, bx = 2510): ramp[i] for i = 0..63 = high byte of ax + i*0x100, adding only while <= 0x3F00
for (j = 0; j < 768; j++) DAC[j] = ramp[pal[j]];       // pal = cs:1F10
```

With ax = (n-1)*0xFF at the n-th fade retrace, `DAC = min(63, pal + max(0, n-2))`: every component rises by one
per retrace (checked against the capture DAC: +1 per frame from frame 9988, +34 at frame 10022). After the step
(35 retraces + overshoot) the exit 43ED restores the timer (1B17, core), waits for the vertical retrace and writes
63 to all 768 DAC entries: white frames 10023-10025 before p3 sets its mode (notes-core.md). Time base: retraces
(step 152 starts at retrace 7273 = capture frame 9985; the n-th fade retrace is frame 9985 + n).

Remake: `c' = min(1, c + k/63)` per component with k = n - 2 (clamped at 0) after n retraces of step 152, then
white at the end of the part.

Also in 2580: `[1963] += 0x64` per retrace — nothing in p2 reads [1963].

---------------------------------------------------------------------------------------------------------------------

## 9. The script (0010:377F) and its timing

### 9.1 Steps

Set-up before step 0: mode 13h; palette (section 8); per-retrace routine = 0010:2580 ([1A09]); timer calibration
1A5F; blend table with s = 24; [1955] = -1; C record +2A = 0x101, +2 = -1 (hidden), +4 = 1300; discs record
+4 = 4000, +2E = 6000; camera: position (0, 0, 1152000), velocity (0, 0, 4000), angles 0, angular velocity
(0, 50, 0); [1904] = 0. Then (N = retraces; values are the camera record unless named; unchanged values carry
over; `models/p2/script.py` prints all 153):

| step | addr of call | N | nominal start | sets before the call |
|---|---|---|---|---|
| 0 | 3893 | 438 | 0 | (set-up above), strip 0 |
| 1 | 38BE | 280 | 438 | strip 1, vz 1500 |
| 2 | 38E2 | 350 | 718 | vz 0 |
| 3 | 38EF | 490 | 1068 | strip 2 |
| 4 | 38FC | 490 | 1558 | strip 3 |
| 5 | 3910 | 630 | 2048 | [3626] = 0x6400 (band at buffer row 80), strip 4 |
| 6 | 391D | 140 | 2678 | strip -1 |
| 7 | 3952 | 3 | 2818 | strip -2, vz 10000, pivot 5800 |
| 8-21 | 3962..3A32 | 3 each | 2821..2860 | pivot 5400, 5000, ..., 200 (-400 per step) |
| 22 | 3A7C | 140 | 2863 | vz 6000, pivot 0, table clear (3A5D) |
| 23 | 3A82 | 50 | 3003 | — |
| 24 | 3AA6 | 70 | 3053 | vz 0 |
| 25 | 3ADD | 70 | 3123 | "C" shown (+2 = 0), [194F]-- (env maps), backdrop on ([1947] = 1) |
| 26 | 3AED | 140 | 3193 | s = 24 (table rebuilt, same) |
| 27 | 3B26 | 210 | 3333 | vz -5500, va 0, vb 50, vc -36 |
| 28 | 3B48 | 140 | 3543 | label in ([1957] = 1), vc -56 |
| 29 | 3B6C | 490 | 3683 | vz -3800 |
| 30 | 3B90 | 210 | 4173 | vz 0 |
| 31 | 3BB4 | 140 | 4383 | — |
| 32-87 | 3BC4..3F34 | 12 each | 4523..5183 | s = 26, 30, ..., 246 (+4 per step) |
| 88 | 3F41 | 210 | 5195 | label out ([1957] = -1) |
| 89 | 3F6F | 12 | 5405 | vz 3000, s = 256 |
| 90-148 | 3F7F..431F | 12 each | 5417..6113 | s = 252, 248, ..., 20 (-4 per step) |
| 149 | 4358 | 350 | 6125 | va 10, vb 50, vc -76, vz 0 |
| 150 | 437C | 576 | 6475 | vz -2400 |
| 151 | 43BF | 210 | 7051 | s = 11, va 20, vb 50, vc -86, vz 0 |
| 152 | 43EA | 35 | 7261 | vz 0, white fade on ([193F] = 1) |

Then 43ED: exit (timer restored, DAC white), exit code [1930] (0, or 0xFF after a key). Most of the 153 steps are
the 15 pivot steps (7-21) and the 116 blur-strength steps (32-87 and 89-148).

Camera state at selected step starts (nominal; `camera.py` prints every step): cam.z 1152000 (step 0), 2904000
(1), 3324000 (2-7), 3774000 (22), 4914000 (24-27), 3759000 (28), 2989000 (29), 1127000 (30-89), 3287000 (149-150),
1904600 (151-152); in vertex units that is a distance of 9000 → 25970 → 38390 → 8805 → 25680 → 14880. The yaw
runs at 50/retrace the whole part (one turn in 1310.7 retraces = 18.7 s); the roll starts at step 27.

### 9.2 Step starts in the capture (overshoot)

The frame loop tests the retrace counter after each drawn frame, so a step ends at the first frame boundary at or
after its N-th retrace. Because 360C waits for a retrace before copying, the next step still starts within the
same retrace count unless the frame in progress at retrace N needs more than one more retrace to finish; then the
step starts one retrace late, and the delay carries over to all later steps. Measured (`stats.py`, 100 M
instructions/s): steps 0-98 start exactly at the nominal retrace; then the 12-retrace blur steps in the slow
close-ups accumulate delay:

| steps | delay (retraces) |
|---|---|
| 0-98 | 0 |
| 99-100 | 1 |
| 101-103 | 2 |
| 104-118 | 3 |
| 119-135 | 4 |
| 136 | 5 |
| 137 | 6 |
| 138-139 | 7 |
| 140-141 | 8 |
| 142-144 | 9 |
| 145 | 10 |
| 146-147 | 11 |
| 148-152 | 12 |

So step 149 starts at retrace 6137 (nominal 6125), 150 at 6487, 151 at 7063, 152 at 7273, and the last step ends
after retrace 7309 (35 + 1 overshoot); retrace 7309 is capture frame 10021, the exit's white DAC follows
(frames 10023-10025, notes-core.md section 5). Capture frame of retrace r = 2712 + r throughout.

With these starts, `camera.py` reproduces the emulator's camera record, pivot, label position / direction and
fade state at **all 7309 retraces exactly**. On a faster machine the delays shrink (to 0 on one fast enough to
draw every frame within a retrace); they shift the camera after step 149 by up to 12 retraces of motion (e.g.
vz -2400 starting 12 retraces later moves the end position by 28800, 1.6 % of the distance). A remake that is
compared with the capture uses the measured starts; the nominal ones are what the script asks for.

### 9.3 Drawn frames

Drawn frames per second in the capture (F lines of the trace):

| steps | retraces | drawn fps |
|---|---|---|
| 0-6 (credits) | 0-2817 | 70.1 (every retrace) |
| 7-24 | 2818-3122 | 70.1 |
| 25-31 ("C" appears, flight in) | 3123-4522 | 66.0 (53-60 at retraces 3570-3850) |
| 32-88 (blur fades, label) | 4523-5404 | 70.1 |
| 89-148 (blur grows) | 5405-6136 | 62.0 |
| 149-150 | 6137-7062 | 53.2 |
| 151-152 | 7063-7309 | 46.2 (39 in the slowest second, retraces 7000-7069) |

Only the blend and the credit band are advanced per drawn frame (sections 4, 5); everything in 2580 is per
retrace.

---------------------------------------------------------------------------------------------------------------------

## 10. Time bases (summary)

| variable | where | time base |
|---|---|---|
| camera position, angles, discs' pivot | 2580 (velocities set by the script) | retraces; piecewise linear, step starts as in 9.2 |
| label position [1955] | 2580 | retraces (1 pixel per retrace) |
| white fade [1941] | 2580 | retraces (+1 DAC step per retrace) |
| blend (motion blur), credit text fade-in / cross-fade / fade-out | 360C | per drawn frame (70 per second except in the heavy shots, 9.3) |
| blur strength s, strip choice, band row, "C" shown, backdrop, textures | script | script steps |
| music | — | not read by p2 |
| random numbers | — | none in p2 |

A remake evaluates the per-retrace state at r = t*70.086 retraces since the part start with the step starts of
9.2 (positions and angles are linear between retraces: interpolate by the fraction), and the per-frame blends as
continuous exponential decays per retrace (sections 4, 5).

---------------------------------------------------------------------------------------------------------------------

## 11. Shortcuts to evaluate exactly, and quirks to keep

Evaluate exactly:

- motion blur through an 8-bit index table (floor of a weighted sum of palette indices): blend true colours in
  float with the weight per retrace (section 4); the palette ramps are linear enough that blending colours equals
  blending indices; the darkening by the floor (up to 256/s shades) is an 8-bit artefact;
- credit text fade through the grey part of the table: a float intensity per pixel (section 5), stopping at full
  white instead of 59/63;
- env map and bump map: nearest texel reads in 8.8 coordinates packed in one register, the half-resolution bump
  rows, the linear interpolation of normals across polygons: per-pixel normals, filtered map reads;
- sine table (32767 sin, 2048 steps, the second quarter mirrored about 511.5 instead of 512) and 15-bit matrix:
  float trigonometry;
- integer projection (truncating idiv, rounding shifts): floats;
- the 16-bit back-face cross product (7.4): a correct test; this changes the close-ups (faces no longer vanish),
  so list it under "Known differences" (or reproduce it, my choice);
- the 70 Hz blend: blend per retrace in time (sub-frames for fast motion) instead of per drawn frame;
- the black screen row 199 (output-buffer row 200 is never blended): a buffer-layout artefact, drop it.

Keep (part of the look):

- the y = 0.75 x projection aspect; the "C" drawn at f = 1300 against 4000 and sorted with depth*0.325;
- the label: opaque, comes from the left edge 1 px per retrace, rests at x = 5 with the black trail left of it,
  covers the blur;
- the credit band being black (no scene) while it is active, the cross-fade between consecutive strips;
- the backdrop being a static picture behind the moving scene, and going through the blur like the scene (it and
  the "C" fade in through the blur when they appear at step 25);
- the bump noise from the overlapping mesh data at one corner of each slab (see 7.3, my choice);
- the white fade (+1 per retrace for 35 retraces, then white).

---------------------------------------------------------------------------------------------------------------------

## 12. Unknowns and guesses

- Why the bump map overlaps the "C" mesh (file layout); its first 11 rows are what the original reads.
- The offset table's row offsets for b >> 4 = 7, 8 (one less): read from the image; it looks like an error in
  whatever made the table (a guess).
- Whether the even-row bump read is intended (it looks like a shift by 9 meant for the other coordinate): a guess;
  the remake should sample the full map continuously.

---------------------------------------------------------------------------------------------------------------------

## 13. Models (`models/p2/`, details in its README.md)

- `script.py`: the 153 steps; `camera.py`: per-retrace state from the script, `--trace` against the emulator
  (7309 of 7309 retraces equal); `stats.py`: step delays and drawn fps (9.2, 9.3).
- `p2trace.py`: the hooked p2 run (per-retrace / per-step / per-frame log and 10 buffer dumps in `out/runs/`).
- `blend_check.py`: 360C + the table builders, byte-exact in all 10 dumps and equal to full.frames.
- `project.py`, `reject_check.py`, `texsel_check.py`, `cull_check.py`, `cull_frames.py`: placement / projection,
  env-map selection and the 16-bit back-face test against the engine's render buffer.
- `p2bump.py`: the bump-map address formula against every bump read in frames 2705-2740.
- `mesh.py`, `mesh_view.py`, `p2data.py`, `dump_pictures.py`: data parsers and the PNGs.
