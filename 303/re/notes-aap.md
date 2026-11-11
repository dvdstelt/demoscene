# Aap stripes (0x30eb1-0x310ca) and blobby (0x31142-0x31304): notes

Scratch listings and scripts: `re/aap/` (f<addr>.s dumps, pak.py, fad.py, fad2.py, spk.py, sheet*.jpg capture grabs).

Time conventions. `m` is music seconds (pos.py's column). `v` is reference-capture video time, using the
coordinator's relation v = 1.00643*m - 1.953. I checked it against white palette flashes measured in the
capture (30 fps, luminance threshold). Measured flash starts were 41.867, 42.733, 43.567, 44.433, 45.267 and
46.133 s. Predicted 16-half-row boundaries 0x390 to 0x3e0 give 41.834, 42.691, 43.547, 44.404, 45.260 and 46.117.
So every event lands within one capture frame. (pos.py's own "video" column is about 0.3 s late.)

| pos | m | v | event |
|---|---|---|---|
| 0x380 | 42.656 | 40.978 | Aap begins (intro cut at 40.98) |
| 0x3c0 | 46.060 | 44.404 | lines -> webs |
| 0x3f0-0x3ff | 48.613-49.4 | 46.97-47.8 | inverted-palette strobe (0x2e798) |
| 0x400 | 49.464 | 47.829 | webs -> lines |
| 0x440 | 52.868 | 51.255 | lines -> webs |
| 0x480 | 56.272 | 54.681 | Aap ends, blobby begins |
| 0x4a0/4c0/4e0/500/520 | 57.974/59.676/61.378/63.080/64.782 | 56.39/58.11/59.82/61.53/63.25 | next lyric |
| 0x4c0 | 59.676 | 58.107 | in the capture: blobby mode 0 -> 3 ("robot" -> "spoon") |
| 0x4f0-0x4ff | 62.229-63.0 | 60.68-61.5 | inverted-palette strobe |
| 0x540 | 66.484 | 64.959 | blobby ends |

Every 16-half-row boundary (0x10 steps) is a possible event. The m/v values for all of them are 0x390 43.507/41.834,
0x3a0 44.358/42.691, 0x3b0 45.209/43.547, 0x3d0 46.911/45.260, 0x3e0 47.762/46.117, 0x410 50.315/48.686,
0x420 51.166/49.542, 0x430 52.017/50.399, 0x450 53.719/52.112, 0x460 54.570/52.968, 0x470 55.421/53.825,
0x490 57.123/55.538, 0x4b0 58.825/57.251, 0x4d0 60.527/58.964, 0x510 63.931/62.389 and 0x530 65.633/64.102.

**Randomness.** Most "what/where" choices in both parts come from `rand()` (0x34974: Watcom LCG
`s = s*0x41c64e6d + 0x3039; return (s>>16)&0x7fff`). 0x2d458 calls rand() 1066 times per *rendered frame*.
So the sequence depends on frame rate and differs between machines. The capture is one realisation. The
remake can either replay the capture's realisation (listed below where visible) or run the same RNG.

---------------------------------------------------------------------------------------------------------

## 0. Buffer and palette helpers (shared, used by other parts too)

All frame buffers are 320x200 8-bit, with stride 320. `FAD` = the 64x256 fade table at [0x5b67c] (16384 bytes).
It is filled by 0x106a8(name) from the last loaded *.fad. The coordinator's "additive" description is not
quite right. I checked rows against the palettes (`re/aap/fad2.py`):

* `FAD[r][c]` for r <= 32 = nearest palette colour to `pal[c] * r/32`. Row 0 is black and row 32 is the
  identity (about 225 of 256 entries exact; the rest are nearest-colour noise). Example, wipeout grey (57,57,57):
  rows 8/16/24 give 16/28/43.
* `FAD[r][c]` for r >= 32 = nearest colour to `pal[c] + (63-pal[c])*(r-32)/31`, a lerp toward white. Row 63 is
  white. Example: (6,28,39) at row 40 gives (18,39,45); the lerp predicts (20.7,37,45.2), an additive model predicts (24,46,57).

### 0x1c490(eax=src, edx=dst): lighten dst by src ("add light")
```c
for (i = 0; i < 64000; i++) {             // fixed 0x3e80 dwords; ebx ignored
    int s = src[i] & 0x7f;                // NB: values >=128 wrap
    int r = 2*s; if (r > 31) r = 31;
    dst[i] = FAD[32 + r][dst[i]];         // s=0 -> identity, s>=16 -> white
}
```
### 0x1c503(eax=src, edx=dst): darken dst by src
```c
    int r = 2*(src[i] & 0x7f); if (r > 31) r = 31;
    dst[i] = FAD[31 - r][dst[i]];         // s=0 -> row 31 (about 97%, slightly darker than identity!), s>=16 -> black
```
### 0x1c3f4(eax=src, edx=dst): lighten by src/8 (ebx ignored)
```c
    dst[i] = FAD[32 + (src[i] >> 3)][dst[i]];   // 0 -> identity, 248..255 -> row 63 (white)
```
### 0x1bbec(eax=src, edx=dst, ebx=n): `memcpy(dst, src, n)` (rep movsd + rep movsb). Used for buffer -> screen.

### Other helpers in this scope
* **0x1c0d6(eax=src, edx=dst, ebx=tab)** makes a one-third-scale thumbnail through a byte table.
  `for y<67, x<108: dst[y*320+x] = tab[src[(3y)*320 + 3x]]`. tab must be 256-aligned. Nearest sample, no filtering.
* **0x1c117(eax=src, edx=dst, ebx=w, ecx=h)** blits a framed box. If w==0 or h==0 it does nothing. Otherwise it copies a
  w x h block (stride 320 on both sides) and draws a 1-px border of colour 0xFF around it, covering x = -1..w and
  y = -1..h.
* **0x1bf0f(eax=src, edx=dst)** does a full-screen remap through tables:
  `dst[y][x] = T75[src[ytab[y] + xtab[x]]]`, with xtab at 0x1476ec (320 entries), ytab at 0x147bec (200 entries,
  pre-multiplied by 320) and T75 at 0x1475ec. 0x2b110 fills all three (below).
* **0x1c058(eax=add, edx=acc)** is the decaying accumulator used by the lyric renderer.
  `acc[i] = LUT[min(255, T07[acc[i]] + ((add[i]>>1)&0x7f))]`, where T07[i] = trunc(0.7*i) (0x1473bc) and
  LUT = 0x1474bc (rebuilt every frame by 0x2a34c).
* **0x1d0dd(eax=tex256, edx=dst)** is a 41x26 grid (0x98648, 12-byte entries) bilinear texture mapper over
  8x8 pixel blocks. Each entry holds `U = [+0]` (8.8, texture **row**) and `V = [+4]` (8.8, texture **column**).
  Texel = `tex[((U>>8)&255)*256 + ((V>>8)&255)]` (both wrap). Interpolation is linear down the block edges plus a
  per-row delta (second-order term), with +0.5 rounding on V.
* **0x10474(r<<5, g<<5, b<<5)** returns the nearest palette index. It clamps negatives to 0, divides by 32, and minimises
  sq[|dr|]+sq[|dg|]+sq[|db|] over pal at 0x5faa8 (table 0x5f6a8). The first minimum wins.

### Palette system (per tick, 0x10124 called from the retrace handler 0x1bc01, about 70 Hz)
```c
// [0x52918]=level  [0x5291c]=target  [0x8eba8]=speed  [0x52920]=mult (float)  [0x8ebb0]=invert flag
if (level == target) speed = 0;
if (level < target) level = min(level + speed, target);
if (level > target) level = max(level - speed, target);
mult = 1 + (mult - 1) * 0.98;
upload(level):  L = level >> 8; p = pal[0x5faa8][k] (6-bit), out = min(63, trunc(x * mult))
   L == 0x40 : x = p
   L <  0x40 : x = (p*L) >> 6                               // to black
   L >  0x40 : k = L-0x41 (0..63)
               x = invert ? ((63-p)*k + p*(63-k)) >> 6      // toward the negative palette
                          : k + ((p*(63-k)) >> 6)           // toward white
```
The palette is re-uploaded every tick. "Flash" in these notes means `level = 0x8000` (L = 0x80, so k = 63:
fully white, or fully negative when invert is set). The level then falls linearly by `speed` per tick back to
0x4000.

### 0x2e798: timer callback registered by the main script for Aap and blobby (also 0x31369 and others)
```c
if (pos != last) { last = pos;                      // [0x580a8], once per half-row
  if ((pos & 3) == 0 && (rand() & 15) < 7) mult = 1.2f;    // every 2 rows: P = 7/16 brighter kick
  if ((pos & 3) == 0 && (rand() & 15) < 2) mult = 0.9f;    //               P = 2/16 darker kick (overrides)
  if ((pos & 0xff) > 0xef) { level = 0x8000; speed = 1000; invert = 1; }   // last 16 half-rows of each 256
  else invert = 0;
}
```
So the whole palette gets random brightness kicks of x1.2 or x0.9 every 2 rows, decaying with a time constant of
50 ticks (0.71 s). In the last 8 rows of every 128 rows the palette is strobed toward the negative on every half-row,
which keeps it about 75% inverted. A half-row is about 3.7 ticks, so at speed 1000 the level only falls from 0x8000 to about
0x7190 (k from 63 to about 48) before the next reset. A full fade back to 0x4000 would take 16 ticks. On the first half-row
after the window, invert is cleared while the level is still about 0x7190, so the remaining fade (about 12 ticks) goes toward *white*.

---------------------------------------------------------------------------------------------------------

## 1. Aap (main script 0x30eb1-0x310ca)

### Main script
```c
// before: [0x9b840]=(pos+0x20)&~0x3f, 0x10c0c(0) clears all timer callbacks
f2b110(0x17); f2d458(0x17 /*edx=1*/); f29d1c(0x2b, /*edx=*/1); f2bdb4(0x17);
add_timer(0x2e798);
load_fad("wipeout.fad");                    // FAD = wipeout.fad
mult = 1.0f; level = 0x8000;                // the speed write here is overwritten below
VGA palette = all (63,63,63) (direct port writes); memset(screen, 0, 64000);
target = 0x4000; speed = 1000;              // white -> wipeout palette in 16 ticks (0.23 s)
[0x9b840] = 0; [0x149a00] = 0;
do {
    f2d458(A = 0x5fda8);                                   // background
    if ((pos & 0x7f) < 0x40) f2b110(B = 0x6f7a8);         // first 32 rows of each pattern: line burst
    else { memset(B, 0, 64000); f2bdb4(B); }               // last 32 rows: web tunnel
    if ([0x149a00]) f1c503(B, A); else f1c490(B, A);       // darken / lighten by the line buffer
    f29d1c(A, A + winy*320 + winx);                        // inverted thumbnail inset
    memcpy(screen, A, 64000);
} while (pos < 0x480);
f2bdb4(0x48); f2b110(0x48); f2d458(0x48);
```
Timer callbacks active (in order): 0x2b0ec, 0x2d354, 0x29ce4, 0x2bd94, 0x2e798. The retrace handler runs only the first
8 of the 16 slots.

### 0x2d354: timer callback of 0x2d458. This is the composition clock.
```c
if (pos != last57da0) { last = pos;                         // [0x57da0] starts at -1
  if ((pos & 0xf) == 0) {                                   // every 16 half-rows = 8 rows (0.851 s)
    if ((pos & 0x1f) == 0) { winx = rand()%200 + 5;         // [0x5808c] 5..204   (data init 10)
                             winy = rand()%120 + 5; }       // [0x58090] 5..124   (data init 10)
    if ((rand() & 15) < 8) [0x149a00] ^= 1;                 // P=1/2 swap lighten/darken
    level = 0x8000; speed = 600;                            // white flash, linear fade over 27.3 ticks (0.39 s)
    t58088 += 50;
    cx = rand()%320 - 160;  cy = rand()%200 - 100;          // float [0x149394], [0x149398]
    mode = rand() & 3;                                      // [0x14939c]; 0 = S-bands, 1..3 = rings
  }
}
t58088 += 1;                                                // every tick (initial value 354 from data)
```
Init values from 0x2d458(0x17) are cx=100.0, cy=30.0 and mode=1. They are used until the first event. In the capture, the
init ran while the white loading flash covered 0x380, so the first random composition probably starts at 0x380 or 0x390.

### 0x2d458: wipeout background
Init: allocates a 64K texture slot. Loads `wipeout.raw`: the 768-byte palette goes to 0x5faa8 (the live palette), then the 256x256
texture. Registers 0x2d354.
The texture has vertical colour stripes: the **column** selects the stripe and the **row** is a brightness ramp (row 0 dark,
row 255 bright).
```c
// grid point (col c=0..40, row r=0..25): x = -160 + 8c, y = -100 + 8r   (screen pixel = (x+160, y+100))
for each grid point, every rendered frame:
  if (mode != 0) {                                         // "rings", P = 3/4
     d2  = (x - cx)^2 + (y + cy)^2;                        // centre at screen (160+cx, 100-cy)
     V   = trunc(d2 * (sqrt(x + 200) + 1) * (1.0/30) + 100*t58088);
     U   = 0xf000 + (rand() & 0xfff);                      // rows 240..255, new noise every frame
  } else {                                                 // "S-bands", P = 1/4
     V   = trunc(atan(0.03 * (y - 28*atan(0.03*x))) * 100 * 256 + 100*t58088);
     U   = trunc(0xf000 + (rand() & 0xfff) - 6*y*y);       // bright centre band, dark at top/bottom (row ~5..21 at |y|=100)
  }
  // all float->int conversions are trunc (0x3235e sets RC = chop)
f1d0dd(texture, dst);
```
* Rings: texture column = d2*(sqrt(x+200)+1)/7680 + 0.39*t. The phase is quadratic in distance, so rings get denser
  outward. The sqrt factor (7.3 at the left edge, 20 at the right) squeezes the rings on the right, giving an egg shape.
  Rings move inward by about 27 texels/s and jump 19.5 texels at each event.
* S-bands: column = 100*atan(0.03*(y - 28*atan(0.03x))) + 0.39t. The bands are horizontal S-curves of amplitude ±44 px.
  They are narrow at the centre line and wide at top and bottom, and the full height covers about one texture width,
  so each stripe appears once.
* The U noise (16 texture rows, per 8x8 grid point, per frame) gives a shimmering brightness grain.

### 0x2b110: line burst with zoom feedback (into buffer B, first half of each pattern)
Init: writes a grey ramp palette into 0x5faa8, which is overwritten by 0x2d458's init right after. Fills
`T75[i] = trunc(min(i,63)*0.75)` (0x1475ec). Allocates a 64000-byte buffer `L` (0x13eff8). Registers 0x2b0ec:
`t58054 += 5; s58058 += 4;` per tick. These are never reset; data init values are t=0x0085b680=8763008 and s=1.
```c
// frame (B = 0x6f7a8, still holding last frame's output or the web buffer):
xtab[i] = trunc(0.9*(i-160) + 160);  ytab[j] = trunc(0.9*(j-100) + 100) * 320;   // rebuilt every frame
L[j][i] = T75[B[ytab[j] + xtab[i]]];          // zoom in x1.111 about (160,100) and fade x0.75 (trunc), per FRAME
for (k = 0, t = t58054; k < 4; k++, t += s58058) {
    x1 = trunc(160*sin(0.001*t + 23) + 160);   y1 = trunc(100*sin(0.00251*t + 72) + 100);
    x2 = trunc(160*sin(172 - 0.00253*t) + 160); y2 = trunc(100 - 100*sin(0.00351*t + 42));
    f2afb4(L, x1, y1, x2, y2);                  // 12-fold rotated copies
}
memcpy(B, L, 64000);
```
**0x2afb4**: `X=x-160, Y=y-100` for both ends. For `k=0..11`, `a=k*2π/12`, it draws a line between
`(160 + X cos a - Y sin a, 100 + Y cos a + X sin a)` (trunc) for both endpoints. The rotation is in pixel space, so on a
4:3 screen the pattern is stretched vertically by 1.2.
**0x2acf0** is a line drawer (Bresenham-like 16.16 DDA) clipped to x in [3,317] and y in [3,197]. **0x1bfb7 / 0x1bf7e** stamp
*additively* (byte adds, which can carry into the neighbour byte on overflow). Each step adds a plus shape: +2 at
(x-1,y), +1 at (x-2,y), (x,y), (x-1,y-1) and (x-1,y+1). For x-major lines with negative slope the stamp is two rows tall
(+1/+2/+1 on rows y and y+1, +1 at y-1 and y+2). Along a line the overlaps give about 4 on the line and 1 beside it.
Result: 48 bright chords per frame forming a 12-fold star. Old chords expand outward and die in 4 frames
(4 to 3 to 2 to 1 to 0). Composited with lighten, a value of 4 gives about 25% toward white and 16 or more is full white;
with darken it goes toward black.

### 0x2bdb4: spider-web tunnel (into cleared buffer B, second half of each pattern)
Init (random, once per run):
```c
n = 0; for (r = 10; r < 200 && n < 40; r += 12) {          // ring radii ring[1..n] (0x13f00c..)
   b = r + (rand() & 7);
   if ((rand()&31) < 25) ring[++n] = b;
   if ((rand()&31) < 5)  { ring[n+1] = b + (rand()&7) - 3; n++; }
   if ((rand()&31) < 5)  { b += (rand()&7) - 3; ring[n+1] = b; n++; }
}
nrings = n;                                                 // [0x140844]
// segments seg[] = {angle (1024 units per radian), r1, r2}  (0x13f0d4, 12 bytes each)
m = 0;
for (j = 0; j < 15; j++) {                                  // main spokes
   seg[m++] = { trunc(j*2π*1024/15) + (rand()&0xff), ring[1]-1, ring[n]-1 };
   for (p = 8; p > 5; p--) if ((rand()&31) < p)            // up to 3 extra spokes, 60 units (3.4 deg) apart
      { seg[m] = { seg[m-1].angle + 60, ring[2 + (rand()&3)] - 1, ring[n]-1 }; m++; }
   if (m > 500) break;
}
for (k = 0; k < 8; k++) {                                   // runs of short radial "rungs" between adjacent rings
   a0 = seg[rand()%m].angle; a1 = seg[(rand()%4 + idx0) % m].angle; step = 50;   // idx0 = the first index
   if (a1 < a0) a1 += 0x1921;                               // 6433 = 2π*1024
   q = rand() % (n-1);
   if (k == 0) { a0 = 0; a1 = 0x1921; step = 100; q = 1; }  // full circle between ring[2] and ring[3]
   for (a = a0; a < a1 && m <= 500; a += step) seg[m++] = { a, ring[q+1]-1, ring[q+2]-1 };
   if (m > 500) break;
}
nsegs = m; alloc; add_timer(0x2bd94);                       // T148b80 = (T + 4) % 1024 per tick
```
Frame:
```c
for (i = -100; i < 100; i++) {
   z = 1.7*i + 0.01*T;  if (z <= 0.07 || z >= 15.0) continue;    // about 9 layers visible
   s   = 1/z;
   cx  = 160 + 50*sin(T/748.0 + 5342)/z;  cy = 100 + 50*sin(T/638.0 + 342)/z;   // 1/748 = 0.0013368984, 1/638 = 0.0015673981
   rot = 4*sin(10*i + 0.001*T);                               // radians, a pseudo-random fixed angle per layer
   for (k = 1..nrings) circle(B, cx, cy, ring[k]*s);          // 0x2bc1c
   for each seg: a = seg.angle/1024 + rot;
       line(B, cx + sin(a)*seg.r1*s, cy + cos(a)*seg.r1*s, cx + sin(a)*seg.r2*s, cy + cos(a)*seg.r2*s);  // 0x2acf0, trunc
   for (y = 0; y < 200; y++) *(uint32*)&B[y*320] = 0;         // clear columns 0..3 (absorb clamped circle edges)
}
```
**0x2bc1c circle(cx,cy,r)** (float args) works by spans. `cx' = cx-2`. For rows y = trunc(cy-r) .. trunc(cy+r+2)-1, clipped to 0..200:
`h = trunc(sqrt(r² - (y-cy)²))`. sqrt of a negative returns 0 through the Watcom matherr path. It sets
`L = clamp(trunc(cx'-h),0,320)` and `R = clamp(trunc(h+cx'),0,320)`. **0x1bdff** then draws the outline: for each row it
covers the horizontal run between the previous and current L (and the same for R) with a (+1,+2,+1) kernel at x..x+2.
The result is closed, soft, about 2-valued ring outlines. The top and bottom caps come from h=0 rows.
Time base: T is per tick (4/tick, so depth speed is 2.8 units/s with 1.7 between layers: a new layer every 0.61 s). Layers
*recede* (z grows, scale shrinks toward the centre). T wraps at 1024 (every 3.66 s), so z jumps by -10.24 (about 6 layers)
and the layer rotations and centre wobble jump. This pop is visible once per web phase at most.

### 0x29d1c: inverted thumbnail inset (Aap and blobby)
Init (0x17 or 0x2b): `f = (mode==0x2b) ? 1.0 : 0.0` ([0x147398]). It allocates a 64K slot `P` (256-byte table + buffer at P+0x100).
If edx==1 (both parts pass 1): `P[i] = nearest(63-r_i, 63-g_i, 63-b_i)`, the *palette-inverted* index (using the palette
loaded just before). Otherwise it is the identity. Registers 0x29ce4: `if (f < 1) f += 0.01;` per tick, clamped to 1 (100 ticks = 1.43 s).
```c
f1c0d6(src, P+0x100, P);                                    // 108x67 inverted thumbnail (every 3rd pixel/row)
x0 = trunc(53*(1-f)); y0 = trunc(33*(1-f)); w = trunc(106*f); h = trunc(66*f);
f1c117(P+0x100 + x0 + 320*y0, dst + x0 + 320*y0, w, h);     // window opens from its centre; it crops, it does not scale
```
The thumbnail is taken from the composed frame *before* the window is drawn. The full window is 106x66 plus a 1-px white
border (palette entry 255 is (63,63,63) in both wipeout and blobby). It shows source x 0..315 and y 0..195.
In Aap the window is fully open from the start (mode 0x2b). It sits at (winx, winy) from 0x2d354: a new position every
32 half-rows (16 rows, 1.70 s), only on boundaries where pos & 0x1f == 0.

### What is drawn when (Aap)
* Every 8 rows (pos & 0xf == 0): new background mapping (rings P=3/4 or S-bands P=1/4) with a random centre
  (rings only); P=1/2 swap of lighten/darken for the line layer; white flash fading linearly over 0.39 s; and the
  background scroll jumps +50.
* Every 16 rows (pos & 0x1f == 0): the inset moves.
* pos & 0x7f < 0x40 gives the 12-fold line burst with trails: 0x380-0x3c0 (v 40.98-44.40) and 0x400-0x440 (v 47.83-51.26).
  Otherwise the web tunnel: 0x3c0-0x400 (v 44.40-47.83) and 0x440-0x480 (v 51.26-54.68).
* 0x3f0-0x3ff (v 46.97-47.8): negative-palette strobe, with no white flash at 0x3f0. The capture confirms it (no white at 47.0;
  frame 47.2 has inverted colours).
* Throughout: x1.2 / x0.9 palette brightness kicks every 2 rows.

### Differences from remake/src/scenes/aap.cpp (most visible first)
1. **Overlay layer** (aap.cpp:23, `w.lines` procedural web in drawWarp). The original alternates every 32 rows
   between (a) a 12-fold rotated burst of 4 moving chords with zoom-out feedback trails and (b) a receding tunnel
   of about 9 identical random spider webs (rings plus spokes), each layer rotated. Both are additive intensity buffers that either
   *lighten toward white or darken toward black* through wipeout.fad. The lighten/darken choice is random per 8 rows. The remake
   has neither the burst nor the black (darken) variant.
2. **Background mappings** (aap.cpp:8-27, 38-39). The original has only two: off-centre egg-shaped rings (3/4) and
   horizontal S-bands (1/4), with the exact formulas above. There is no funnel (mode 1) and no tunnel (mode 0). Brightness comes from
   the texture's row ramp: near-full in rings mode, dark top/bottom in S mode. Per-frame shimmer comes from the U noise.
   Ring centre: uniform over the screen (cx in [-160,159], cy in [-100,99]).
3. **Inset** (aap.cpp:49-54). The original is a 1/3-scale nearest-sampled thumbnail of the *current composed frame* with
   palette-inverted colours, not a second warp. Size 106x66 px (0.331 x 0.33). It moves every 16 rows (not every
   composition), with x in [5,204] px and y in [5,124] px. Border is 1 px white.
4. **Flash** (aap.cpp:41-47). The original whitens the whole palette (all colours lerped to white, k=63 to 0 linearly over
   27.3 ticks = 0.39 s) at each 8-row event. The remake's phong1 bloom at the vanishing point does not exist in the original.
5. **Palette kicks and strobe** are missing: x1.2/x0.9 multiplier every 2 rows (decay 0.98/tick), and the negative strobe at 0x3f0-0x3ff.
6. **Timing** (aap.cpp:32-34). The remake uses `seg = 0.855` from 40.95. The original events sit on pos multiples of 16:
   m = 42.656 + 0.851*k (row length 0.106375 s before order 17), giving v = 40.978, 41.834, 42.691 and so on. The remake drifts by about 4 ms per
   composition and has the wrong phase. Derive k from the music position instead.
7. The start is a white palette during loading, then a fade to wipeout colours in 16 ticks (0.23 s) (aap.cpp:57 is close enough).
   The original has **no** fade to white at the end (aap.cpp:58). The white at 54.68 is blobby's loading palette, which is instant.

---------------------------------------------------------------------------------------------------------

## 2. Blobby (main script 0x31142-0x31304)

### Main script
```c
level = 0x8000; speed = 0 (eax=0); mult = 1.0; VGA palette = all white; memset(screen,0,64000);
target = 0x4000; add_timer(0x2e798); load_fad("blobby.fad");
f2a34c(0x17);  f182e4(0x17 /*edx=1*/);  f29d1c(0x17 /*edx=1*/);   // inset opens from f=0
speed = 1000;                                       // after loading: white -> palette in 16 ticks
do {
    f182e4(A = 0x5fda8);                            // background + object
    f29d1c(A, A + 10*320 + 193);                    // inset fixed at (193,10)
    f2a34c(B = 0x6f7a8);                            // lyric buffer (never cleared; starts with Aap's last B)
    f1c3f4(B, A);                                   // lighten frame by B>>3
    memcpy(screen, A, 64000);
} while (pos < 0x540);
f182e4(0x48); f2a34c(0x48); f29d1c(0x48);
```
Timer callbacks: 0x2e798, 0x2a0d4 (lyrics), 0x181c4 (blobby), 0x29ce4 (inset).

### 0x181c4: blobby timer callback. This switches the effect *inside* the part.
```c
t9c474 += 3;                                        // per tick; reset to 0 in f182e4(0x17)
if (pos != last52dc0) { last = pos;
  if ((pos & 3) == 0)                               // every 2 rows
     for (i = 0; i < 32; i++) jit[i] = trunc(J[rand()&15] * 0.7);   // signed bytes 0x9c630..0x9c64f
     // J (0x52f10) = {0,0,1,1,-1,-1,2,-2,6,-4,2,1,4,-1,-6,-2} -> jit in {0,±1,±2,4,-4,...}
  if ((pos & 0xf) == 0 && (rand() & 3) == 0) {      // each 8-row boundary, P = 1/4
     level = 0x8000; target = 0x4000; speed = 400;  // white flash, 41 ticks (0.585 s)
     mode = (mode + 3) & 3;                         // [0x9d305]: 0 -> 3 -> 2 -> 1 -> 0 ...
     pa = rand()%400 - 200; pb = rand()%400 - 200; pc = rand()%500 + 500;   // floats 0x53058/5c/60
  }
}
```
Initial mode is 0 (BSS). Initial pa=0, pb=0, pc=1000 (data).
| mode | background texture | object |
|---|---|---|
| 0 | blobby #1 (B/W blobs) | contour (iso-depth bands) |
| 3 | blobby #0 (pastel) | env map, pastel #0 |
| 2 | blobby #1 (B/W) | env map, pastel #0 |
| 1 | blobby #0 (pastel) | contour |

The remake's "robot" is mode 0 and "spoon" is mode 3. The change is random: at each of 0x490..0x530, P=1/4. In the capture it
happened exactly once, at 0x4c0 (v 58.107; white flash measured at 58.13, pastel env object from 58.15 on). The capture
shows no other mode change through 0x530, though 0x510 and 0x530 are not checked frame by frame (the frames at 62.45-63.7 are pastel with an env-mapped object).
Each switch also re-randomises the object's motion phases (pa, pb, pc).

### 0x182e4: blobby render
Init: three 64K slots. `blobby.raw` loads the palette into 0x5faa8, then tex #0 into [0x97e3c] and tex #1 into [0x97e44]. It builds
the contour texture C ([0x97e40]): `v = clamp(trunc(1500*sin(i*π/16) - 1000), 0, 63); C[*][i] = nearest(v,v,v)`,
the same for all 256 rows. That is period-32 bands: i mod 32 in 4..12 is white (60..63) and the rest is black. It sets
`[0x8ebb0] = 1` (which 0x2e798 clears on the next half-row outside its window) and registers 0x181c4.
```c
t = t9c474;
// --- background: sphere-ray grid (0x1cd4b + 0x135dc), then 0x1d0dd ---
a = t/7; b = t/2;                                   // 1024 units per turn, sinT[i] = trunc(65536*sin(i*π/512))
M = { {cb, 0, -sb}, {sb*sa, ca, -cb*sa}, {sb*ca, sa, cb*ca} };   // 16.16, rows R0,R1,R2
d = (sinT[(t/2 + 0x156) & 1023] >> 5) + 0xd48;      // 3400 ± 2048, period 9.75 s
e = (sinT[(t/3 + 0x53e) & 1023] >> 2) + 15*t;       // ±16384 plus scroll 3150/s (12.3 texels/s)
for grid col i 0..40, row j 0..25:
    D = (i - 20.5)*R0 + (j - 13)*R1 + (d/256.0)*R2;  // (as integers: d*R2>>8)
    U = trunc(D.y * 131072/|D| + 32768);             // scale 4.0 * 32768; texture row
    V = trunc(e + D.x * 131072/|D| + 32768);         // texture column
f1d0dd(mode & 1 ? tex0 : tex1, A);
// --- object: spikey.dat (899 verts, 1794 faces), loaded at startup by 0x13cf8 ---
S1 = sin(t/700.0);
pos = { 100*sin(pa + t/300.0), 100*sin(pb + t/542.0), 1200 + 600*sin(pc + t/223.0) };
f13e78(S1, 1.0):  for each vertex (unit dir u, radius r0 in 93..206):
                     p = u * (170 + (r0 - 170)*S1);          // sphere r=170 <-> spikey (S1<0 would invert)
                  smooth vertex normals = normalised sum of face cross products
A_ = t/700.0; B_ = t/243.0;
R = { {cA*cB, sA, -cA*sB}, {-sA*cB, cA, sA*sB}, {sB, 0, cB} };      // float, at 0x52e28
f1405c():  p' = R p, n' = R n;
           sx = 160 + trunc(600*(p'.x + pos.x)/(p'.z + pos.z)); sy = 100 + trunc(600*(p'.y + pos.y)/(p'.z + pos.z));
           env u = trunc(120*n'.x + 128), env v = trunc(120*n'.y + 128);
           bucket = (sum over 3 verts of clamp(trunc(0.5*p'.z + 66), 0, 132)) >> 2   // 0..99
if (mode & 2) { tex = tex0; f14290(A); }   // per vertex (sx, sy, u=env u, v=env v)
else          { tex = C;    f180b8(A); }   // per vertex (sx, sy, u=trunc(p'.z), v=64): bands of constant view depth
// both: polygon filler 0x1ee85, affine (u = texture column, v = row), back faces culled
// (screen cross >= 0 rejected), buckets drawn 0..99
```
* Time base: everything runs on t (3 per tick, 210/s) from blobby init. S1 = sin(0.3 s per second): a sphere
  at the part start (v 54.68), full spikes at about 5.2 s (v ≈ 59.9), back to about 0.08 at the part end. The capture agrees (sphere with
  concentric contour rings at 54.9, spikes from about 56.5). Object rotation is 0.30 and 0.86 rad/s. Background angles
  are 0.18 rad/s (a) and 0.64 rad/s (b).
* Contour look: white bands where (p'.z mod 32) is in [4,12], i.e. slices of constant *view depth*, 9/32 duty, hard edges,
  on black. On the initial sphere they appear as concentric circles.
* Bucket order: as decoded, bucket 0 (smallest p'.z, nearest) is drawn first. That would be a reversed painter's order, with
  culling hiding most of it. **Not verified against the capture**; check before copying it.

### Lyrics: 0x2a34c with 0x2a0d4, 0x29eac and 0x1c058 (also used in later parts)
Init: tmp buffer `S` (0x13efc4, 64000 bytes), text bitmap `TB` (0x14739c, 256x96), T07 table. Registers 0x2a0d4. Sets
"new lyric" = 1 and song-table mode [0x1473b8] = 0 (cycle mode; other parts set 1).
Strings (0x57dc0, cycle order, index [0x1473a8] starts at 0 and persists across parts; wN = entry N, the words
themselves are not copied into these notes): `w0`, `w1`,
`w2`, `w3`, **`w4`**, **`w5`**, `w6`, `w7`, `w8`, `w9`.
```c
// 0x2a0d4, per tick
if (pos != last57d94) { last = pos;
   if (songmode) { /* look up (row [0x9b850], order [0x9b84c] with 0x15/19/1b->0x11, 0x16/1a/1c->0x12)
                     in 41-entry tables 0x57de8/0x57e8c; new index -> new lyric */ }
   if ((pos & 1) == 0) f15494();                    // unknown, also called at program start
   if ((pos & 3) == 0) {                             // every 2 rows
      if ((rand() & 15) == 0) finv = (finv > 0.5) ? 0 : 1;      // P=1/16: invert the lyric buffer
      smear = rand() % 12;                           // [0x57db4]
      zoom = 0;
      if ((rand() & 15) < 2) { ty = rand()%144; tx = rand()%(320 - width); zoom = 1; }   // P=2/16: jump + 3x
   }
   if ((pos & 0x1f) == 0 && !songmode) newlyric = 1;  // every 16 rows
}
if (finv > 0) finv = max(0, finv - 0.021f);           // 48 ticks back to 0
// 0x2a34c frame (dst = B)
if (newlyric) { newlyric = 0; str = list[idx]; idx = (idx+1) % 10;
   width = sum(glyph width + 1) (space = 8+1);       // font = fontblur.raw: 96-row glyphs, stride 560
   ty = rand()%144; tx = rand()%(320 - width);
   memset(TB, 0, 256*96); blit glyphs at TB column 8.. }
j0..j3 = jit[0..3], j4 = jit[7];                      // from 0x181c4 (static in parts without it)
if (!zoom) quad = (tx + 2*j0, ty + 2*j1) .. (tx + width + 4*j2, ty + 56 + j3);   // about 1:1
else       quad = (tx - width, ty - 56) .. (tx + 2*width + 4*j2, ty + 112 + 4*j3);  // 3x, same centre
f29eac(S, quad, width):  // grid for 0x1d0dd with texture TB, full screen:
     column = clamp((X - x0)*width/(x1-x0) + 8, 0, width+16)
     row    = rows inside the quad: (Y - y0)*56/(y1-y0) + 16   (TB rows 16..72)
              grid rows above it: 25 - smear; below it: 61 + smear; grid row 0 forces 25, row 25 forces 61
     // so the letters' top and bottom rows are smeared as vertical streaks up to the top and bottom of the screen
LUT[i] = clamp(trunc((i + 5*j4)*(1-finv) + (255 - (i + 5*j4))*finv), 0, 255);
f1c058(S, B):  B = LUT[min(255, trunc(0.7*B) + (S>>1))];     // per FRAME decay 0.7 (trails), feeds back
// main: f1c3f4(B, A)  -> A = FAD[32 + B/8][A]  (text saturates to white)
```
Consequences seen in the capture:
* Vertical streaks above and below the words. Through blobby.fad's lighten rows, black turns purple-ish. These are the
  "purple bars" at 55-56.
* Huge 3x words with trails.
* Whole-frame whiteouts with the image showing only through the letters when finv is about 1 (58.53, 62.4-62.8, 63.7). The
  inverted buffer holds 255 outside the text, and the feedback through the LUT makes it flicker.
* A faint whole-frame brightening when j4 > 0 (5*j4 accumulates to about 17*j4).
Lyric change times: init (v 54.68) 'w0', 0x4a0 (56.39) 'w1', 0x4c0 (58.11) 'w2',
0x4e0 (59.82) 'w3', 0x500 (61.53) 'w4', 0x520 (63.25) 'w5'.

### Inset in blobby
0x29d1c(0x17) gives an inverted-palette thumbnail at fixed (193,10), opening from its centre over 100 ticks (v 54.68 to about 56.1). It is
drawn before the lyrics, so lyrics lighten over it and the thumbnail never contains lyrics.

### Differences from remake/src/scenes/part1.cpp (Robot 13-62, Spoon 65-125) and timeline.cpp
1. **One continuous effect, not two scenes** (timeline.cpp:13-14). Background, object, inset, time t and motion all continue
   through the mode change. Only the texture choice (mode bit 0), the object shader (bit 1), a 0.585 s white flash and new
   motion phases change. Which change happens, and when, is random (P=1/4 per 8 rows). The capture's realisation is a single
   0 to 3 change at 0x4c0 (m 59.676, v 58.107). The remake's 58.15 is right for the capture.
2. **Background** (part1.cpp:16-24 rotozoom + crisp + wave; 72-81 polar twirl). The original is the sphere-ray grid above (the same
   mapper as the intro, scale 4.0) for both looks. The B/W blobs are the texture itself (no thresholding needed), and the
   pastel streaks are the same mapping's edge stretch. There is **no radial/zoom blur** (part1.cpp:97) and no
   contrast curve.
3. **Inset** (part1.cpp:48-57, 99-119). The original is the palette-inverted 1/3 thumbnail of the frame, which explains the "blue" inset in
   the pastel mode. Fixed at (193,10) with size 106x66 and a 1-px white border, it opens from its centre over 1.43 s from the part start.
   There is no separate object or tex3d environment in the inset, and no size jump at 56.2.
4. **Object** (part1.cpp:35-46, 83-93). Same mesh. Shape morph `r = 170 + (r0-170)*sin(t/700)` (sphere at the start, spiky
   about 5 s later). Rotation R(t/700, t/243). Position (100 sin(pa+t/300), 100 sin(pb+t/542), 1200+600 sin(pc+t/223)) with
   focal 600. There is no jump at 56.4 (part1.cpp:41).
   Contour shader (fx.cpp:57-64) uses `fract(length(vObj)*param)` plus a rim term. The original uses bands of *view-space depth*,
   period 32 object units (sphere radius 170), duty 9/32, no rim.
   Env map (spoon): u,v = 120*n'.xy + 128 on tex #0, no exposure pulse or tint. Its brightness pulsing is the palette
   multiplier.
5. **Purple bars** (part1.cpp:26-33) are the lyric renderer's vertical smear. They should come from the lyric code,
   follow the word and appear for every lyric, not only for "robot".
6. **Lyrics** (timeline.cpp:47-52). Words w4 and w5 of table 0x57dc0 differ from the remake's list (it had misheard them).
   Changes come every 16 rows at the times above. Positions are random (x in [0, 320-width), y in [0,144) px), with jitter, 3x zoom
   jumps, inversion whiteouts and 0.7-per-frame trails. The text is composited by lightening through blobby.fad, not alpha-drawn.
7. **Flashes** (part1.cpp:59, 121-122). At the part start, white during loading fades in 16 ticks. At a mode switch, a 41-tick linear
   fade. The negative strobe at 0x4f0-0x4ff (v 60.68-61.5) is missing in the remake. The capture shows it at 61.4. The x1.2/x0.9
   palette kicks are also missing.

---------------------------------------------------------------------------------------------------------

## 3. Time bases summary
* Per tick (70 Hz): t58088 (+1, +50 per 8 rows), t58054 (+5) and s58058 (+4) (lines), T148b80 (+4 mod 1024, webs),
  inset f (+0.01), t9c474 (+3, blobby), lyric finv (-0.021), and the palette level, speed and multiplier.
* Per rendered frame: background U noise (rand), line feedback (zoom x1/0.9, x0.75 fade), the lyric accumulator
  (x0.7). They are frame-rate dependent. The capture machine's frame rate is unknown; 70 fps is a reasonable assumption.
* Per music position: 8-row composition and mode events, 16-row inset moves and lyrics, 32-row lines/webs halves, 2-row
  palette kicks and jitter, and the 128-row negative strobe window.

## 4. Not determined
* The RNG seed and state at part start, so the exact random compositions cannot be predicted. The capture has to be read frame by frame.
* Whether the near-first bucket order of 0x14290/0x180b8 really draws far faces over near ones (not checked in the capture).
* What 0x15494 does (called every half-row pair from the lyric callback).
* The exact frame rate of the capture machine (it sets the feedback trail lengths).
* Initial value of [0x149a04] and the source of the 0.29 s visual lead. The coordinator's relation covers it.
