# Scope "particles": parts 1, 3 and 6 (8-bit buffers, particles, radial blur, feedback)

All addresses are linear addresses of the running program (= offsets + 0x400000 in `unpacked/clone_img.bin`).
"dt" is main's frame step (float [0x420489], see NOTES.md "Time base"); "unit" = 1/70 s of program time (more
exactly: main computes t70 = xmpGetTimer × [0x41a128], and that double is 5.8675115883353874e-05 = 1/17043, so a
unit is 17043 PIT clocks = 1/70.0101 s, not 1/70 s; core scope). "frame" = one call of the part routine. `>>` on
signed values is an arithmetic shift (floor). `trunc` = toward zero (the FPU runs with RC = truncate: every
`fistp` and every store into a float32 variable truncates toward zero).

Everything below was checked against the original, not only read. Models in `models/particles/` (section 11):

| what | model | compared | result |
|---|---|---|---|
| radial blur 0x403df7 (maps, level table, both in-place passes) | `blur_model.py` | 4 inputs, mode 0 and mode 1 with sin(phase) > 0 and < 0, against the routine run in unicorn | every byte equal |
| part 1 frame 0x405527 (particles, projection, stamps, blur, feedback, flash, expansion, title / credits blits) | `parts_model.py` | 1,500 frames from t11 (growth, blur, title) and 285 from t11148 (flash, credits), against the routine in unicorn on the same state | 8-bit buffers, picture, count, seed: equal in every frame; the particle floats equal except in 2 frames (values near 0, double vs 64-bit extended rounding: 54 ulp of a value ~1e-6, and a velocity 0 vs 3e-20) |
| part 3 frame 0x405c45 | `parts_model.py` | 1,400 frames from t7884 (growth to 5000, event 301) | buffers, picture, count, seed equal in every frame; a velocity differs by 1 ulp in 5 frames |
| part 6 frame 0x406677 | `parts_model.py` | 700 frames from t3925 (with and without event 601) | everything equal, including every float of every particle |
| all three parts against the capture | `capture_check.py` | every frame of the capture that shows parts 1 (first run and second run), 3 and 6, from the snapshots, with dt from `full.flips` (computed as main does) and the events at the capture's frames | the model's pictures equal `full.frames` pixel for pixel (results in section 11) |

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | routine (set-up) | music, ticks, flips | capture fps | what is on screen |
|---|---|---|---|---|
| 1 | 0x405527 (0x4053f2 in the precalc and at event 1) | 00:00..08:20, ticks 11..1644, flips 2..1391 | 93 → 52 (00:00..03:3f, growing with the particle count), 26 (04:00..07:3f, blur), 28 (08:00..) | black until 00:20; from 00:20 a violet swarm of up to 5000 glowing dots strung along a slowly turning 3D Lissajous path, with trails (feedback buffer); the projection's "fisheye" term breathes, so the dots move in and out radially and the trails become radial hairs; 04:00 the swarm is cut to 2000 and the radial blur turns on (streaks from the centre, length pulsing); 08:00 the dots vanish, the trails fade out and the title "clone meets clone" fades in |
| 6 | 0x406677 (0x4065c5 at event 6) | 14:18..18:00, ticks 3925..4620, flips 11815..12187 | 28.4 (34 after 17:30) | 3000 blue dots on the surface of a cube (lattice of 8 units), blinking (short lives, respawned at random lattice points), turning, "breathing" (a sine scale per axis), through a fisheye projection: a ball of dots; radial blur every frame whose vertical direction swings with sin(phase): radial streaks, then horizontal ones; no trails; 17:20 (event 601) the dots die out in about 1.2 s, then black |
| 3 | 0x405c45 (0x405b10 at event 3) | 29:00..30:20, ticks 7884..9323, flips 15617..16999 | 65 → 46 → 63 | a blue cloud: dots flying out of a cube around the centre, each frame blurred and magnified 2× about the centre and darkened to 3/4, so every dot blows up into soft blobs; black bars over rows 0..19 and 180..199, a black box with "connect the dots" and glyph 3 fading in with the dot count; 30:00 (event 301) the dots are cut away in about 1 s, the cloud fades, the text fades out |
| 1 again | 0x405527 (no set-up) | 3a:00..3c:3f, ticks 11148..11723, flips 18947..19230 | 26 → 28 | event 104 spawns 2000 dots in one frame along a short stretch of the Lissajous path, blur on: a dark ring in a violet streak storm; 3c:00 (event 105) the dots vanish and the screen flashes white → pink → violet → black (1.6 s); 3c:10 (event 107) ".aap .echo .simon" fades in on black |

Parts 1 and 6 write rows 20..179 only (section 9); part 3 writes the whole frame.

## 2. Memory

Nothing in these parts is read from a table in the image except the four pictures (section 10); every table they
use is computed. Variables (BSS unless an image value is given):

| address | what |
|---|---|
| 0xeaf410, 0xebee10 | the two 8-bit 320 × 200 buffers (64,000 bytes each, adjacent). [0xf57390] is the index of the "current" one (×0xfa00 + 0xeaf410); 0x404ad3 sets it to 0 and nothing changes it, so buffer 0 is always "cur" and buffer 1 "other" |
| 0xed8450 | particle array, 0x34 bytes per particle, room for 10,000 (up to 0xf57390); [0x4224f8] = live count (int), [0x4224fc] = target count (int; 10000 in the image, set by the set-ups and events) |
| 0xece810 | screen offsets (y × 320 + x) of the particles that pass the clipping test, filled every frame by 0x404b84 (room for 10,000) |
| 0x420491 | seed of the random generator (dword, 1 in the image), used by the three spawn routines only |
| 0xea740c | multiplication table of 0x403f10 (64 rows × 512 signed bytes), [0x4224e8] = 0xea750c = row 0, column d = 0 |
| 0xea6f0c, 0xea6bec | the blur's column map (320 dwords) and row map (200 dwords), rebuilt every call |
| 0x4223c4..0x4223e8 | blur work variables: direction (−1 / +1), the two fractions × 512, buffer, map pointers, centre (int), 0x4223e8 level table (256 bytes) |
| 0xeaf40c | blur phase φ (float), += 0.04 dt on every call of 0x403df7, used only by mode 1 |
| 0xf57394 + 0x400 k (k = 0..7) | part 1 palettes; 0xf593b0 + 0x400 k part 3; 0xf5b3c4 + 0x400 k part 6 (256 dwords 0x00RRGGBB each) |
| 0xf5939c | emitter phase (float), part 1 (+= 1.2 dt per spawn call) and part 3 (+= 0.2 dt per spawn call, never read there) |
| 0xf593a0, a4, a8 | part 1 angles a, b, c (float); 0xf593ac part 1 fisheye phase; 0xf59394, 0xf59398 passed in EDX / EBX to 0x404b84, which ignores them (always 0) |
| 0xf5b3b4 | part 3: toggled 0.0 / 1.0 every frame, never read; 0xf5b3b8, bc, c0 part 3 angles |
| 0xf5d3c4, c8, cc | part 6 breathing phase g, g × 0.8, g × 1.3; 0xf5d3d0, d4, d8 part 6 angles |
| 0x422500 | part 1 mode: 0xffffffff in the image (nothing grows, no blur), 0 (event 103: the target grows), 1 (events 101, 104: blur on, no growth) |
| 0x422504 | flash level (float; event 105 = 255.0) |
| 0x422508 / 0x42250c | credits on (event 107) / credits fade level (float, 63.0 in the image) |
| 0x422510 / 0x422514 | title on (event 106 sets 1.0, event 111 sets 0) / title fade level (float, 63.0 in the image) |
| 0x422518 | part 3 end (event 301); 0x42252c part 6 end (event 601) |
| 0x422520, 0x422524 | floats 0.03 and 56.0 in the image (part 6), never written |

Particle record (0x34 bytes, all float32):

| offset | field | parts |
|---|---|---|
| +0x00, +0x04, +0x08 | position x, y, z | 1, 3, 6 |
| +0x0c, +0x10, +0x14 | deformed position | 6 |
| +0x18, +0x1c, +0x20 | transformed (camera) position tx, ty, tz | 1, 3, 6 |
| +0x24, +0x28, +0x2c | velocity vx, vy, vz | 1, 3 (6 leaves them) |
| +0x30 | life (units of the life counter) | 1, 3, 6 |

The array, the count, the seed and the emitter phase are shared: each part continues the generator where the
previous one left it, and part 1's second run continues part 3's emitter phase (section 7).

## 3. Shared mechanics

### 3.1 Random generator

```c
int rnd() {                        // inlined at every call site, seed dword [0x420491]
    seed = seed * 0x41c64e6d + 0x3039;       // mod 2^32
    return (seed >> 10) & 0x7fff;            // 0..32767, used as rnd() % n
}
```
Seed 1 in the image, and nothing calls it before part 1's first spawn (00:20), so part 1 starts the sequence at
seed 1. Calls per spawned particle: 7 in parts 1 and 3, 4 in part 6. The number of spawns depends on the frame rate
(section 7), so the capture's sequence cannot be replayed without its frame steps; seeds at the part starts in the
capture: part 1 at 00:20: 1; part 6 set-up: 0x2dc9b2a7; part 3 first spawn: 0x845351c7; part 1 second run:
0x0d91c17e (from the snapshots t1645, t4621, t9324: no other routine uses the generator).

### 3.2 Particle bookkeeping (0x405442 part 1, 0x405b60 part 3, 0x406615 part 6, 0x404b0e)

```c
void update(spawn_fn, bool move) {           // once per frame
    int spawns = part6 ? 0 : target - count; // parts 1, 3: fill up to the target (negative: none); part 6 only
                                             // replaces the dead (its set-up and event 601 set the count)
    for (i = 0; i < count; i++) {
        P &p = part[i];
        p.life -= 0.6 * dt;                  // (float), all three parts
        if (move) p.pos += 0.3 * p.vel * dt; // parts 1 and 3 (computed as x + (vx * dt) * 0.3)
        if (p.life < 0) {                    // 0x404b0e: the last particle replaces it (x, y, z, v, life only)
            part[i] = part[--count]; spawns++; i--;   // and is updated in this pass at index i
        }
    }
    while (spawns-- > 0) spawn_fn();         // appended at the end; new particles do not move in their first frame
}
```
Lives count down by 0.6 per unit, so a particle with life L lives L / 0.6 units (L / 42 s).

### 3.3 Matrix and transform (0x404c9f, 0x404f0f, 0x404e36, 0x404d5c, 0x404feb, 0x40507a, 0x405162)

All three parts build `M = Rz(c) · Ry(b) · Rx(a)` (3 × 4 float32, row-major): identity 0x404c9f, then 0x404f0f(a),
0x404e36(b), 0x404d5c(c), each multiplying its rotation from the left (it recombines two rows, translation column
included). With s = (float)sin(angle), c = (float)cos(angle) stored first:

```
Rx (0x404f0f): row1' = c row1 − s row2,  row2' = c row2 + s row1
Ry (0x404e36): row2' = c row2 − s row0,  row0' = c row0 + s row2
Rz (0x404d5c): row0' = c row0 − s row1,  row1' = c row1 + s row0
```
Part 6 then calls 0x404feb(M, 0, 0, 0), which adds (0, 0, 0) to the translation column (no effect). 0x40507a
(parts 1, 3) transforms the position +0x00..+0x08, 0x405162 (part 6) the deformed position +0x0c..+0x14:
`t_r = ((m[r][1] y + m[r][0] x) + m[r][2] z) + m[r][3]` → +0x18..+0x20 (no perspective yet).

### 3.4 Projection and stamps (0x404b84 → 0x404772 / 0x40489c)

`0x404b84(k, mode)` (stack: float k, int mode; the register arguments are ignored) projects every live particle
and draws it into buffer 0:

```c
n = 0;
for (i = 0; i < count; i++) {
    float d = tz + 300.0;
    if (!(d > 0)) continue;                          // behind the camera plane
    float q = ((tx*tx + ty*ty) * k + d) * 0.0078125; // = (d + k r^2) / 128, stored as float
    float inv = 1.0 / q;                             // stored as float
    int sy = trunc(ty * inv + 100.0);
    int sx = trunc(tx * inv * 1.2 + 160.0);
    if (sx < 0 || sx > 316 || sy < 20 || sy > 176) continue;   // whole 4x4 stamp inside x 0..319, rows 20..179
    list[n++] = sy * 320 + sx;
}
if (n > 2)                                           // 1 or 2 visible particles: nothing is drawn
    for (j = n - 1; j >= 1; j--) stamp(buf0 + list[j], mode);  // list[0] is never drawn
```
So: screen x = 160 + 153.6 tx / (tz + 300 + k (tx² + ty²)), screen y = 100 + 128 ty / (same); 1.2 is the 4:3
pixel aspect (320 × 200 pixels are 1.2 times taller than wide), y grows downwards, the camera looks along +z from
z = −300. k > 0 pulls the outer points in (a fisheye that depends on the distance from the axis, not on depth):
part 1: k = 0.007 + 0.0065 sin(fisheye phase), part 3: 0, part 6: 0.008 (float 0x3c03126f).

The stamp is 4 × 4 bytes added with saturation at 255 (`add cl, dl; sbb dl, dl; or cl, dl`), top-left at
(sx, sy):

```
mode 0 (0x404772, part 1)     mode 1 (0x40489c, parts 3 and 6)
 21  41  41  21                3  5  5  3
 41  73  73  41                5  6  6  5
 41  73  73  41                5  6  6  5
 21  41  41  21                3  5  5  3
sum 704                        sum 76
```
Because sx, sy are truncated and the stamp spans sx..sx + 3, its centre lies on average 1.5 pixels right of and
below the projected point (in pixel-edge coordinates: centre = trunc(X) + 2, X = the projected coordinate). The
saturating adds commute, so the drawing order does not matter.

### 3.5 Radial blur (0x403df7 → 0x40407c → 0x404137 + 0x404278; Dune's)

`0x403df7(buf = EAX; float cx, float cy, float level, float zoom, int mode)` (stack, `ret 0x14`). Parts 1 and 6
call it on buffer 0 with cx = 160.0, cy = 100.0, zoom = −0.025 (0xbccccccd); part 1: mode 0, level = 0.8 + 0.19
sin(13 a); part 6: mode 1, level = 0.85 + 0.14 sin(6 a) (a = the part's first angle).

```c
phase += 0.04 * dt;                                  // [0xeaf40c], every call
float zy = zoom * 0.8333333333333333;                // [ebp-0x10], stored as float (5/6: see below)
for (x = 0; x < 320; x++) colmap[x] = trunc((x - cx) * 64.0 * zoom);          // 1/64 pixel
for (y = 0; y < 200; y++) {
    float t = y - cy;
    rowmap[y] = mode == 0 ? trunc(t * 64.0 * zy) : trunc(sin(phase) * (t * 64.0 * zy));
}
// 0x40407c(buf, (int)cx, (int)cy, &level, colmap, rowmap)
icx = clamp((int)cx, 0, 319); icy = clamp((int)cy, 0, 199);
for (i = 0, acc = 0; i < 256; i++, acc += level) lev[i] = (int)acc & 0xff;     // = trunc(i * level)
pass(-1, icy);  pass(+1, icy + 1);                   // rows upwards from cy, then downwards from cy + 1

void pass(int dir, int y0) {
    for half in (left: x = icx-1 down to 0 [0x404137], right: x = icx up to 319 [0x404278])
        for (y = y0; 0 <= y && y < 200; y += dir)
            for x in half (in that order) {
                int r = rowmap[y], c = colmap[x];
                int fy = r & 63, fx = c & 63;
                byte *s = buf + (y + (r >> 6)) * 320 + x + (c >> 6);       // floor
                byte top  = s[0] + MUL[fy][s[320] - s[0]];                 // vertical first
                byte top2 = s[1] + MUL[fy][s[321] - s[1]];
                byte v    = top + MUL[fx][top2 - top];                     // then horizontal
                buf[y * 320 + x] = min(255, buf[y * 320 + x] + lev[v]);
            }
}
MUL[f][d] = (f * d) / 64, truncated toward zero, f = 0..63, d = -256..255       // 0x403f10, signed bytes
```
The left half is processed for all rows of a direction before the right half. Each pixel p adds, scaled by the
level, the bilinear sample of the same buffer at q = c + S (p − c) with S = diag(1 + zoom, 1 + zoom · 5/6 · σ),
σ = 1 (mode 0) or sin(phase) (mode 1); with zoom = −0.025 that is 2.5 % (horizontally) and 2.08 % · σ
(vertically) closer to the centre. The 5/6 (= 200 / 240, perhaps meant as an aspect correction) is not needed for
a radial step: a uniform scale about the centre needs the same factor in pixel units whatever the pixel aspect,
so with 5/6 the sample lies slightly off the ray to the centre (vertical offsets shrink by 0.979 per step,
horizontal by 0.975): traced inwards a streak follows y ∝ x^0.83, a slight curve towards the horizontal axis.

Because the pass runs in place from the centre outwards, a sample that lies towards the centre reads pixels that
were already brightened in this pass: out(p) = in(p) + L · out(q), i.e. along every ray out(p) ≈ Σ_k L^k in(c +
S^k (p − c)), a streak that runs outwards from every bright point and falls off as (distance ratio)^(ln L / ln
0.975) — at L = 0.61 a short glow, at L = 0.99 streaks to the screen edge. Exceptions, all part of the look:
- within 40 columns of cx and 48 rows of cy (mode 0) the sample is less than a pixel away in both directions, so
  one corner of its bilinear footprint is the pixel itself, not yet processed (its old value), and the left half's
  column cx − 1 reads column cx, which the right half processes later: the recursion is weaker near the centre;
- every step truncates (lev[], and MUL towards the first sample), which removes about half a level per step;
- mode 1 with sin(phase) < 0: the vertical sample lies further out, in rows the pass has not reached, so there is
  no recursion through rows: each pixel gets one echo of the original buffer (from 2.08 % · |σ| further out
  vertically), plus some recursion along its row for rows within 48 / |σ| of the centre row (the sample row is
  then partly the current row); with σ ≈ 0 the sample stays in the pixel's own row: pure horizontal streaks across
  the screen (the "bow-tie" frames of part 6, t4400 in the capture). In this case the top rows read above the
  buffer (into the multiplication table) and the bottom rows into buffer 1 (zero in part 6); only rows 0..2 and
  197..199 are affected, outside the visible rows.

`blur_study.py` compares the in-place pass with order-free formulations on typical part 1 and part 6 inputs (mean
absolute difference over rows 20..179, levels 0..255; full table in the script's output):

| input, level L, σ | original mean | fixed point of min(255, in + floor(L · bilinear(out at q))) | single echo min(255, in + L · in(q)) |
|---|---|---|---|
| part 1, 0.62, +1 | 20.9 | 0.8 | 7.4 |
| part 1, 0.80, +1 | 38.7 | 2.7 | 23.7 |
| part 1, 0.99, +1 | 167.9 | 41 | 151 |
| part 1, 0.80, −1 | 18.2 | 18.3 | 3.5 |
| part 6, 0.80, +1 / 0 / −1 | 16.3 / 14.9 / 7.7 | 1.7 / 2.3 / 7.5 | 9.5 / 8.2 / 1.5 |
| part 6, 0.99, +1 / −1 | 102.8 / 11.9 | 127 / 92 | 95 / 5.3 |

So the order-free recursion matches the original for σ ≥ 0 and L up to about 0.8 (and is closest with the floor
per step), a single echo matches for σ < 0, and at L near 0.99 (the peaks of part 1's and part 6's pulses) neither
does: there the original's weaker centre and per-step losses hold the streaks far below the ideal series, which
saturates. A remake that wants the original's look at the pulse peaks has to run the sequential pass (it is cheap
on the CPU: one pass over the 8-bit-equivalent buffer, at 320 × 200 or a small multiple) or tune a loss per step
against it.

### 3.6 Feedback, expansion, palettes, set-up

```c
void feedback(byte *dst, byte *src) {         // 0x4045b4 (dst = buffer 1, src = buffer 0), part 1 only
    for (i = 0; i < 64000; i++) dst[i] = (15 * dst[i] + src[i]) >> 4;
}
void expand(byte *src, uint *tgt, uint *pal_for_band(b)) {   // 20 bands x 40 calls of 0x404a08 (8 x 8 blocks)
    for (y = 20; y < 180; y++) for (x = 0; x < 320; x++) tgt[y*320 + x] = pal[src[y*320 + x]];
}
void make_palette(uint *pal, int R, int G, int B) {          // 0x404a3e (EAX, EDX, EBX, ECX)
    for (i = 0; i < 256; i++)
        pal[i] = min(255, R*i/92) << 16 | min(255, G*i/92) << 8 | min(255, B*i/92);   // integer division
}
void particles_setup() {                       // 0x404ad3, called by all three set-ups
    count = 0; memset(buf0, 0, 64000); memset(buf1, 0, 64000); cur = 0;
}
```
0x404a08 is a plain lookup per pixel (no interpolation); the 8 × 8 blocks are only its loop structure.

Palettes (all eight copies of a set are made with the same arguments, so the per-band selection below has no
effect; the colour at i = 255 is white in all three):

| set | address | (R, G, B) | look | made by |
|---|---|---|---|---|
| part 1 | 0xf57394 + 0x400 k | (127, 95, 274) | violet; B saturates at i = 86, R at 185, G at 247 | 0x4053f2 |
| part 3 | 0xf593b0 + 0x400 k | (127, 159, 255) | light blue; B at 92, G at 148, R at 185 | 0x405b10 |
| part 6 | 0xf5b3c4 + 0x400 k | (95, 127, 274) | blue; B at 86, G at 185, R at 247 | 0x4065c5 |

Part 1 picks a palette per band of 8 rows: band b = 2..21 (rows 8b + 4 .. 8b + 11) uses k = b − 2 for b < 9, 7
for b = 9..16, 23 − b for b > 16 (0 at the top and bottom, 7 in the middle), apparently meant for a vertical fade
that the identical palettes do not make. Parts 3 and 6 use k = 7 for all bands. (PARTS.md's "rows 16..175 through
per-band palettes that darken towards the top and bottom" is not what the code does: rows 20..179, one palette.)

### 3.7 Pictures and boxes on the 32-bit frame

`0x404002(pic, x, y, target, mode 1, level)`: the picture variable holds a pointer to a 12-byte header {pixels,
w, h}; pixels 0xffffffff are skipped; level <= 0 copies, 1..63 blends per channel `p + trunc(level (d − p) / 64)`
(p = picture, d = frame; level 63 leaves 1/64 of the picture, 0 shows it fully: the opacity is (64 − level) / 64).
`0x4043be(target + offset, colour, w, h)` fills a rectangle; its width is a byte counter (0 = 256), which is why
part 3 fills 320 columns as 256 + 64.

## 4. Part 1 (0x405527)

### 4.1 Set-up and events

0x4053f2 (in the precalc and again at event 1): the eight part 1 palettes, 0x404ad3, target = 0, then spawns
`target` particles (none). It does not reset the angles, the phases or the generator.

| event (position) | handler | effect in part 1 |
|---|---|---|
| 103 (00:20) | mode = 0 | the target count grows |
| 101 (04:00) | count = target = 2000, mode = 1 | the first 2000 array entries stay, blur on, growth off |
| 102 (08:00) | count = target = 0 | all particles vanish at once; the trails fade |
| 106 (08:00) | title = 1.0 | the title fades in |
| 104 (3a:00) | count = 0, target = 2000, mode = 1 | 2000 particles are spawned in the next frame, blur on |
| 111 (3a:00) | part = 1, title = 0 | |
| 105 (3c:00) | count = target = 0, flash = 255.0 | flash |
| 107 (3c:10) | credits = 1.0 | the credits fade in |

### 4.2 Frame

```c
void part1() {
    if (title  && title_level   > 0) title_level   = max(0, title_level   - 0.45 * dt);   // [0x422514]
    if (credits && credits_level > 0) credits_level = max(0, credits_level - 0.75 * dt);  // [0x42250c]
    fisheye += 0.02 * dt;                                                   // [0xf593ac]
    if (mode == 0 && target < 5000) target = min(5000, (int)(target + 10.0 * dt));        // truncates
    a += 0.001 * dt;  b += 0.002 * dt;  c += 0.003 * dt;                    // [0xf593a0], a4, a8
    update(spawn1, move = true);                                            // 3.2
    M = Rz(c) Ry(b) Rx(a);  t = M pos;                                      // 3.3
    project_and_stamp(k = 0.007 + 0.0065 * sin(fisheye), mode 0);           // 3.4, into buffer 0
    if (mode == 1) radial_blur(buf0, 160, 100, 0.8 + 0.19 * sin(13 * a), -0.025, mode 0);  // 3.5
    feedback(buf1, buf0);                                                   // buf1 = (15 buf1 + buf0) >> 4
    if (flash != ±0) { flash = max(0, flash - 2.3 * dt); memset(buf1, (int)flash, 64000); }
    expand(buf1, rows 20..179, part 1 palettes);
    if (title)   blit(title picture,   141, 104, mode 1, (int)title_level);
    if (credits) blit(credits picture, 141,  74, mode 1, (int)credits_level);
    memset(buf0, 0, 64000);
}

void spawn1() {                                                             // 0x4051d5
    emitter += 1.2 * dt;                                                    // [0xf5939c], on EVERY call
    if (count >= target) return;
    P &p = part[count++];
    p.x = sin(emitter * 0.002 ) * 200.0 + (rnd() % 48 - 24);
    p.y = sin(emitter * 0.0042) * 200.0 + (rnd() % 48 - 24);
    p.z = sin(emitter * 0.0067) * 200.0 + (rnd() % 48 - 24);
    p.vx = (rnd() * 5.439512619669277e-05 - 1.0) * 0.03;                    // 5.4395e-5 = 1/18384
    p.vy = (rnd() * 5.439512619669277e-05 - 1.0) * 0.03;
    p.vz = (rnd() * 5.439512619669277e-05 - 1.0) * 0.03;
    p.life = rnd() % 696;
}
```
The particles are born on a Lissajous curve (200 units, frequencies 0.002 : 0.0042 : 0.0067 per emitter unit) with
a ±24 jitter per axis, and nearly stay there: |v| ≤ 0.03 per axis (the range is −0.03..+0.0235, mean −0.0033:
rnd() / 18384 reaches 1.78, not 2), so they drift at most 0.009 units per unit, at most 10 units over a life of up
to 1160 units. The swarm is therefore the stretch of the curve the emitter covered during the last lifetime, turned
by M. Its frames show:
- trails: buffer 1 keeps 15/16 of itself per frame; with the truncation a constant input c settles at c − 15
  (for c ≥ 15) and an abandoned pixel loses about 1/16 per frame, then 1 level per frame below 16;
- the fisheye k = 0.0005..0.0135 cycles every 2π / (0.02 · 70) = 4.5 s, moving every dot in and out along its
  radius: the radial hairs of the trails before the blur;
- the blur level 0.8 + 0.19 sin(13 a) = 0.61..0.99 cycles every 2π / (0.013 · 70) = 6.9 s (pulsing streak length).

Flash: while flash > 0 the whole of buffer 1 is set to (int)flash after the feedback, every frame (255 →
0 in 255 / 2.3 = 111 units = 1.58 s): the screen shows palette[flash]: white down to 247, then light pink as G
falls, violet below 185 as R falls, darker below 86 as B falls, black; the frame where flash reaches 0 clears the
buffer.

The title (0x41fc5c, 101 × 9) at (141, 104) and the credits (0x41ffe9, 37 × 32) at (141, 74) are drawn on the
32-bit frame after the expansion, with the blend level falling 63 → 0 by 0.45 and 0.75 per unit (140 and 84 units:
2.0 s and 1.2 s). In the capture the part ends (08:20) with the title level at 7.28, not yet 0; part 11 then
draws the title itself.

### 4.3 In the capture

| | flip (tick) | dt of that frame | state |
|---|---|---|---|
| start | 2 (11) | 6.0, then −4.13 (NOTES.md) | count 0, target 0, mode −1: black |
| event 103 | 177 (108) | 0.737 | growth +floor(10 dt) per frame (7 at dt 0.74); 5000 reached at flip 708 (02:27, 9.27 s; 8.9 s without the truncation) |
| event 101 | 972 (783) | 1.33 | 2000, blur on; fps 52 → 26 |
| events 102, 106 | 1342 (1551) | 2.70 | particles gone, title fading in |
| end | 1391 (1644) | 2.47 | angles a, b, c = 2.12007, 4.24013, 6.36018, fisheye 42.4011, emitter 22805.37, blur phase 44.78262, title level 7.28 |
| second run, events 104, 111 | 18947 (11150) | 0.3165 (part 10's last frames ran at 220 fps) | 2000 spawns in this frame: the emitter advances 1.2 × 0.3165 = 0.38 per particle, from 26746.80 (left by part 3) to 27504.6: x turns 1.5 rad, y 3.2 rad, z 5.1 rad of the curve: the ring of part 1's second run |
| event 105 | 19133 (11535) | 2.70 | flash |
| event 107 | 19157 (11583) | 2.58 | credits |

## 5. Part 6 (0x406677)

### 5.1 Set-up (0x4065c5, event 6)

Part 6 palettes, 0x404ad3, target = 3000 (0xbb8), then 3000 calls of spawn6: count = 3000. The angles and g start
at 0 (BSS; nothing else writes them).

```c
void spawn6() {                                       // 0x40628f
    if (count >= target) return;
    P &p = part[count++];
    int face = rnd() % 6, u = rnd() % 20 - 10, v = rnd() % 20 - 10;   // -10..9
    (X, Y, Z) = face 0: (-10, u, v)   1: (10, u, v)   2: (u, -10, v)
                face 3: (u, 10, v)    4: (v, u, -10)  5: (v, u, 10);   // jump table at 0x406277
    p.pos = 8.0 * (X, Y, Z);           // lattice points on the cube [-80, 80]^3 (free axes -80..72)
    p.life = rnd() % 96;               // velocity not set (not used)
}
```

### 5.2 Frame

```c
void part6() {
    a += 0.002 * dt;  b += 0.003 * dt;  c += 0.004 * dt;       // [0xf5d3d0], d4, d8
    if (target != 0 && end601) { target = max(0, (int)(target - 35.0 * dt)); count = target; }
    update(spawn6, move = false);                              // life -= 0.6 dt, respawn
    // 0x40600f: breathing
    g += 0.03 * dt;                                            // [0xf5d3c4]  (0.03 = float [0x422520])
    g8 = g * 0.8; g13 = g * 1.3;                               // from the unrounded sum, stored as floats
    for each particle {
        ix = trunc(x); iy = trunc(y); iz = trunc(z);           // as int16 (fild word): lattice values anyway
        S = (sin(ix/56 + g) * k6 + k23) + (sin(iy/56 + g8) * k6 + k23) + (sin(iz/56 + g13) * k6 + k23);
        deformed = (ix, iy, iz) * S;                           // k6 = 0.1666666666666666, k23 = 0.6666666666666666
    }
    M = Rz(c) Ry(b) Rx(a);  translate(M, 0, 0, 0);  t = M deformed;
    project_and_stamp(k = 0.008, mode 1);                      // into buffer 0
    radial_blur(buf0, 160, 100, 0.85 + 0.14 * sin(6 * a), -0.025, mode 1);
    expand(buf0, rows 20..179, part 6 palette 7);
    memset(buf0, 0, 64000);                                    // no trails
}
```
S = 2 + (sin + sin + sin) / 6 = 1.5..2.5 (the cube is scaled to ±120..±200 and bulges where the waves meet). A
particle lives 0..95 / 0.6 = 0..160 units (2.3 s) and is replaced the same frame by one at a random lattice point:
the cube's dots blink. The blur's vertical factor sin(φ), φ += 0.04 per unit, turns every 2π / (0.04 · 70) =
2.24 s; its level 0.71..0.99 every 2π / (0.012 · 70) = 7.5 s. φ continues from part 1 (44.78262 at the start of
part 6 in the capture: 0.04 × the units part 1 ran with the blur on).

After event 601 (17:20, flip 12130) the count falls by ceil(35 dt) per frame (87 at dt 2.47): 0 at flip 12167
(17:36); the remaining frames are black (no stamps, nothing to blur, no feedback).

## 6. Part 3 (0x405c45)

### 6.1 Set-up (0x405b10, event 3)

Part 3 palettes, 0x404ad3, target = 0 (spawns none). The angles start at 0.

```c
void spawn3() {                                     // 0x40590c
    emitter += 0.2 * dt;                             // [0xf5939c], every call, never read in part 3
    if (count >= target) return;
    P &p = part[count++];
    float a = rnd() % 200 - 100, b = rnd() % 200 - 100, c = rnd() % 200 - 100;   // -100..99
    float len = sqrt(a*a + b*b + c*c);                                           // stored as float
    p.x = rnd() % 96 - 48;  p.y = rnd() % 96 - 48;  p.z = rnd() % 96 - 48;       // -48..47
    p.v = (a, b, c) * (1.0 / len) * 3.0;            // speed 3 (0.9 units per unit), direction from a cube
    p.life = rnd() % 696;
}
```
(a = b = c = 0, one chance in 8 million, would give an infinite velocity.)

### 6.2 Frame

```c
void part3() {
    toggle = 1.0 - toggle;                                     // [0xf5b3b4], unused
    if (target < 5000 && !end301) target = min(5000, (int)(target + 5.0 * dt));
    else if (target != 0 && end301) { target = max(0, (int)(target - 50.0 * dt)); count = target; }
    a += 0.003 * 1.0051 * dt;  b += 0.004 * 1.0051 * dt;  c += 0.005 * 1.0051 * dt;   // [0xf5b3b8], bc, c0
    update(spawn3, move = true);
    M = Rz(c) Ry(b) Rx(a);  t = M pos;
    project_and_stamp(k = 0, mode 1);                          // into buffer 0, which holds last frame's cloud
    fill(target rows 0..19 and 180..199, black);               // 4 x 0x4043be (256 + 64 wide)
    for (p = 320; p < 63680; p++)                              // 0x404621: rows 1..198 of buffer 1
        buf1[p] = (buf0[p-320] + buf0[p-1] + buf0[p+1] + buf0[p+320]) >> 2;   // no centre tap; wraps at row ends
    expand(buf1, rows 20..179, part 3 palette 7);
    for (y = 0; y < 200; y++) for (x = 0; x < 320; x++)        // 0x405f8c(buf1, buf0, 32000)
        buf0[y*320 + x] = 3 * buf1[(((y-100)*32000 >> 16) + 100) * 320 + ((x-160)*32000 >> 16) + 160] >> 2;
    fill(target at (162, 135), 148 x 40, black);
    level = max(0, 63.0 - target * 0.05);                      // stored as float, then (int)
    blit("connect the dots", 212, 160, mode 1, level);
    blit(glyph 3,            212,  87, mode 1, level);
}
```
So the visible buffer is blur4(stamps + ¾ · magnify(previous visible buffer)), magnified by 65536 / 32000 = 2.048
per frame about the centre of pixel (159, 99) (least-squares fit of the integer maps: source = 0.4882 dest + 81.64
horizontally, 0.4880 dest + 50.96 vertically; fixed points 159.52 and 99.53 in pixel-edge coordinates; reads rows
51..148, columns 81..237), nearest pixel, and darkened to 3/4 (truncated). A dot's image doubles in size and loses a
quarter every frame and is blurred by the 4-tap kernel (variance 0.5 pixel² per axis per frame): a dot of frame n
appears as soft blobs of 2, 4, 8.. times its size in the following frames, mostly gone after 10 frames (0.75^10 =
0.06). The dots themselves fly out of a 96-unit cube at 0.9 units per unit for up to 1160 units, so they spread over
a growing ball, seen through the plain perspective (k = 0).

The text level falls as the count grows: 63 − 0.05 target, 0 from target 1260 on (in the capture flip 15865,
2a:09); after
event 301 it rises again with the falling count (1/64 of the picture remains visible at level 63: the glyph stays
faintly visible on black to the end of the part).

In the capture: growth +floor(5 dt) per frame from flip 15617 (29:00), 5000 at flip 16462 (2d:17); event 301 at
flip 16897 (30:00), count −ceil(50 dt) per frame, 0 at flip 16975 (30:19); then the cloud decays (¾ per frame).

## 7. Time bases

"T" below is a part's own clock: the sum of the dt it has seen = program t70 (= xmpGetTimer / 17043) of the timer
read before its current frame minus the one before its first frame (part 1's first run: plus 6.0, the initial dt;
NOTES.md). In music seconds, T = 70.0101 s.

| variable | recurrence | kind |
|---|---|---|
| part 1 angles a, b, c | += (0.001, 0.002, 0.003) dt | linear: (0.001, 0.002, 0.003) T1, where T1 counts both runs of part 1 (the second run continues at a = 2.12007, b = 4.24013, c = 6.36018 in the capture: T1 = 2120.07 at the end of the first run) |
| part 1 fisheye phase | += 0.02 dt | linear, 0.02 T1 (42.4011 at the start of the second run) |
| part 1 title / credits level | −= 0.45 dt / 0.75 dt, clamped at 0, from the frame of event 106 / 107 | linear with a clamp: max(0, 63 − 0.45 (T − T_event)), where T_event = T of the frame before the event frame |
| part 1 flash | −= 2.3 dt, clamped, from 255 at event 105 | the same; overrides buffer 1 while > 0 |
| part 1 target | `target = (int)(target + 10 dt)` per frame, up to 5000 | **per frame**: loses the fraction of 10 dt every frame (7 per frame at dt 0.74 instead of 7.4) |
| part 3 target | `(int)(target + 5 dt)` per frame; after 301 `(int)(target − 50 dt)` | **per frame** (truncation) |
| part 6 target after 601 | `(int)(target − 35 dt)` per frame | **per frame** (truncation) |
| life (all parts) | −= 0.6 dt; removal and respawn when < 0 | linear in T, checked per frame: a particle dies in the first frame after its time, its replacement is born in that frame |
| position (parts 1, 3) | += 0.3 v dt | linear (v constant) |
| emitter phase (part 1) | += 1.2 dt per call of spawn1 | **per frame and per spawn**: calls per frame = deaths + (target − count). With the count at the target, deaths per frame ∝ dt, so the emitter advances ∝ dt² per frame, ∝ dt per unit: `rate_study.py` (from t11, 00:20..04:00) gives 8.0 dt emitter units per unit at 5000 particles (6.0 at dt 0.75, 10.6 at 1.33, 21.6 at 2.7). At the capture's 52 fps the x component turns 1.5 rad/s; at 93 fps it would turn 0.8 rad/s. This sets how much of the curve the swarm covers and how fast it moves: **the look of part 1 depends on the frame step** |
| emitter phase in part 3 | += 0.2 dt per call of spawn3 | per spawn; not used by part 3, but part 1's second run starts from it (26746.80 in the capture) |
| part 1 feedback buffer | (15 o + c) >> 4 per frame | **per frame**: trails decay by 15/16 per frame (time constant 16 frames: 0.31 s at 52 fps, 0.62 s at 26 fps) |
| blur phase φ | += 0.04 dt per blur call (part 1 with mode 1, part 6) | linear in the time the blur ran: 44.78262 at the start of part 6, 80.89889 at the start of part 1's second run (capture) |
| blur pass | in place, once per frame on the frame's stamps | per frame, but on a buffer cleared every frame: no accumulation across frames (part 1's accumulation is the feedback) |
| part 6 angles a, b, c | += (0.002, 0.003, 0.004) dt | linear, from 0 at the set-up |
| part 6 g | += 0.03 dt | linear, from 0 |
| part 3 angles | += (0.003, 0.004, 0.005) · 1.0051 dt | linear, from 0 |
| part 3 buffers | buf1 = blur4(buf0 + stamps), buf0 = ¾ magnify(buf1) per frame | **per frame**: the ×2.048 magnification and the ¾ decay happen once per frame |
| part 3 text level | 63 − 0.05 target | follows the per-frame target |
| random generator | 7 / 7 / 4 calls per spawn | per spawn (see 3.1 for the capture's seeds) |

No events are frame-rate dependent beyond the usual lag (they run at the first frame after their position).

**What the remake should step per frame**: parts 1 and 3 completely (spawns, emitter, targets, the feedback and
magnification), part 6's spawns and target. Their look depends on the step, so the reference is the capture's
frame steps (dt in units, from `full.flips`, `fps_table.py`):

| part | music | dt per frame in the capture |
|---|---|---|
| 1 | 00:00..00:1f | 0.73 (no particles) |
| 1 | 00:20..01:3f | 0.77 → 1.12, rising with the count (≈ 0.73 + 0.00012 per particle) |
| 1 | 02:00..03:3f | 1.14 → 1.33 (5000 particles from 02:27) |
| 1 | 04:00..07:3f | 2.68..2.70 (2000 particles + blur) |
| 1 | 08:00..08:20 | 2.47 |
| 6 | 14:18..17:1f | 2.45..2.47 |
| 6 | 17:20..18:00 | 2.47 → 2.03 (count falling to 0) |
| 3 | 29:00..2d:0f | 1.08 → 1.50 (count rising) |
| 3 | 2d:10..2f:3f | 1.50..1.52 |
| 3 | 30:00..30:20 | 1.36 → 1.07 |
| 1 | 3a:00 | 0.3165 for the first frame (the 2000-particle spawn), then 2.70 |
| 1 | 3a:01..3b:3f | 2.65..2.70 |
| 1 | 3c:00..3c:3f | 2.59 → 2.47 |

(per 16 rows: `fps_table.py`). Stepping at these rates (TickStepper with the rate taken from this table, or the
capture's own frame times, 3,430 timer values for these parts) reproduces the capture; `capture_check.py` shows that
with the capture's exact steps the model gives the capture's pictures.

## 8. Shortcuts a high-resolution remake should evaluate exactly

- **8-bit buffers through palettes**: the buffers hold intensities 0..255 and each palette is linear with a clamp,
  colour = min(255, C · v / 92) per channel: keep one float intensity buffer per part and map it through that
  formula (the clamp to white is part of the look). The intensities saturate at 255 when stamps and streaks add up;
  keep that clamp too.
- **4 × 4 stamps** with saturating adds: a soft blob of 4 × 4 pixels (1/80 of the screen width) whose kernel sums to
  704 (mode 0) or 76 (mode 1), centred 1.5 pixels right of and below the projected point on average (trunc(X) + 2
  in pixel-edge coordinates: the truncation and the top-left anchor); at higher resolution a smooth sprite of the same footprint and integral. The clipping drops a
  stamp that is not completely inside x 0..319, rows 20..179 (a sharp edge in the original; drawing it clipped is
  the evaluated-exactly version). Dropping list entry 0 and drawing nothing for 1 or 2 visible particles are
  invisible quirks.
- **Projection**: trunc to whole pixels → sub-pixel positions.
- **Feedback** (15 o + c) >> 4: in float, o = (15 o + c) / 16 loses the truncation, which is what ends the trails
  (a level below 16 drops by 1 per frame instead of fading forever) and lowers bright plateaus by up to 15 levels;
  use floor((15 o + c) / 16) on a float buffer in 0..255 units, or subtract the average loss 15/32 per frame.
- **Radial blur**: bilinear with 6-bit weights and truncating products, a level table trunc(L v), in-place
  recursion; see 3.5 for what an order-free version gets right and wrong.
- **Part 3**: nearest-pixel ×2.048 magnification → bilinear; the 4-tap blur → a Gaussian of variance 0.5 px² per
  axis (in 320-pixel units) per frame; the two truncations (>> 2 and 3 v >> 2) lose 3/8 of a level each on average.
- **Part 6**: the integer truncation of the positions before the breathing scale changes nothing (lattice points).
- Matrices and transforms in float32 with truncation: plain float maths.

## 9. Whole frame or not

Parts 1 and 6 write only rows 20..179 of the draw target (the expansion; part 1's title and credits lie inside
them). Rows 0..19 and 180..199 keep what was drawn into that page four frames earlier (32-bit mode) or are black
(15-bit mode, the conversion clears its buffer). In the capture they are black at every entry: before part 1 the
pages are empty, and parts 8 (before part 6) and 10 (before part 1's second run) leave those rows black. Part 3
fills rows 0..19 and 180..199 black itself and expands rows 20..179: it writes every pixel.

## 10. Pictures

All four are stored pictures (hand-drawn by Aap; NOTES.md "Pictures and textures", gfx scope), drawn on the 32-bit
frame with the level blend of 3.7; transparent pixels (index 0 → 0xffffffff) are skipped:

| picture | stored at | size, palette | variable | where | fade |
|---|---|---|---|---|---|
| title "clone meets clone" (anti-aliased lettering, "meets" bold) | 0x41fc5c | 101 × 9, grey C | [0xf73930] | part 1: (141, 104) | level 63 → 0 by 0.45 / unit from event 106 |
| credits ".aap", ".echo", ".simon" (three lines) | 0x41ffe9 | 37 × 32, grey C | [0xf7392c] | part 1: (141, 74) | 63 → 0 by 0.75 / unit from event 107 |
| "connect the dots" | 0x41aec6 | 88 × 10, grey A | [0xe3511c] | part 3: (212, 160), in the black box (162, 135) 148 × 40 | level = max(0, 63 − 0.05 target) |
| glyph 3 (brush-drawn) | 0x41dc0c | 68 × 68, grey B | [0xe350f4] | part 3: (212, 87), its lower 20 rows over the box | the same level |

Everything else (dots, streaks, clouds, palettes) is generated. No effect reads a picture through palette indices.

## 11. Models (`models/particles/`) and what they showed

- `blur_model.py`: the radial blur in Python, byte-exact against 0x403df7 run in unicorn (4 inputs, both modes,
  sin(phase) of both signs).
- `parts_model.py`: parts 1, 3 and 6 frame by frame (particles in doubles with float32 stores truncated toward zero;
  the scalar fade level of part 3 in exact x87 arithmetic). Against the routines in unicorn on the same state:
  part 1, 1,500 frames from t11 with the events poked at chosen frames and 285 from t11148 (flash, credits); part 3,
  1,400 frames (growth to 5000 and event 301); part 6, 700 frames (with and without event 601): buffers, pictures,
  counts and seeds equal in every frame; particle floats equal except in 7 frames (a value near zero, or 1 ulp of a
  velocity: Python doubles against the 64-bit extended FPU).
- `capture_check.py`: from the snapshots, finishes the frame in progress in unicorn, then steps with the capture's
  dt and events and compares model and original with `full.frames` at the tick each frame was shown. Result for
  every frame of the parts (section 11 table below).
- `emu.py` (unicorn harness: call one routine on a snapshot), `fpu.py` (float32 truncation, x87 64-bit truncation,
  main's t70 / dt), `fps_table.py` (capture frame steps), `blur_study.py` (3.5), `rate_study.py` (section 7).

| run | snapshot | frames stepped | frames on screen at a tick end, compared | pixels differing (model and original) |
|---|---|---|---|---|
| part 1, first run (events 103, 101, 102 + 106) | t11 | flips 6..1391 | 1,184 | 0 in every frame |
| part 6 (event 601) | t3925 | flips 11815..12187 | 371 | 0 |
| part 3 (event 301) | t7884 | flips 15617..16999 | 1,334 | 0 |
| part 1, second run (events 105, 107) | t11148 | flips 18947..19230 | 282 | 0 |

(Frames drawn and replaced within one tick are stepped but not in `full.frames`.) The events were applied before the
first frame whose predecessor was shown at or after the event's position; that matches the capture everywhere.

## 12. Not determined / remarks

- Why the eight band palettes are identical: the code makes them with one set of arguments; whether a fade was
  planned cannot be told.
- The fixed point of the order-free blur at L ≈ 0.99 does not match the original (3.5); I give the sequential
  algorithm exactly but no cheaper equivalent for those frames.
- Main's t70 factor is 1/17043 (0x41a128), not 70/1193182 as NOTES.md says (core scope): the program's clock runs
  0.0144 % fast against the music timer; `full.flips`' t70 column uses 70/1193182. `fpu.t70_dt` reproduces main.
- 0x42251c (42.0) and 0x422528 are in the particle variables' block but nothing reads them.
