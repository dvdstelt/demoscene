# Scope "2D parts B": pictures, interlaced planes and strobes (entries 16-21, 23-25, 27-29, the end)

Script entries (0x14-byte entries of the table 0x205ed0: init, done, retrace routine, frame routine, end position)
and every routine they call:

| entry | init | retrace | frame | ends at | routines called |
|---|---|---|---|---|---|
| 16 | 0x205261 | 0x20483f | 0x20483f | 45:00 | 0x25a1fb (loader), 0x25f0be (centred sprite) |
| 17 | 0x2052a0 | 0x20483f | 0x20483f | 48:00 | 0x25a1fb, 0x25f0be |
| 18 | 0x20508d | 0x2050ce | 0x20510c | 49:00 | 0x25a1fb; 0x259a08, 0x259c98 (with 0x259de4, 0x259ed0, 0x259f70, 0x25a048) |
| 19 | 0x20483f | 0x2050ed | 0x20510c | 50:00 | as 18 |
| 20 | 0x205924 | 0x20483f | 0x205950 | 51:48 | 0x25a1fb, 0x25a5ff (negative table) |
| 21 | 0x20483f | 0x20483f | 0x2059fc | 52:00 | |
| 23 | 0x20516b | 0x2051dd | 0x20520c | 58:00 | 0x25a1fb; 0x259b50, 0x259c98 (same helpers) |
| 24 | 0x2052df | 0x20483f | 0x20483f | 59:00 | 0x25a1fb, 0x25f0be |
| 25 | 0x20531e | 0x20483f | 0x20483f | 60:00 | 0x25a1fb |
| 27 | 0x205ad8 | 0x20483f | 0x205b04 | 63:48 | 0x25a1fb, 0x25a5ff |
| 28 | 0x20483f | 0x20483f | 0x2059fc | 64:00 | |
| 29 | 0x205368 | 0x20483f | 0x20483f | 64:63 | 0x25a1fb |
| end | 0x20538d (main calls it after the last entry) | | | | 0x25a1fb, 0x25f0be; then main waits 210 refreshes |

All done routines of these entries are the empty 0x20483f (`ret`). Addresses are linear (the image at 0x20427c).
"Refresh" = the 70.086 Hz screen refresh, "record k" = record k of `full.frames` (refresh k). Screen coordinates are
original 320 x 200 pixels (shown at 4:3); pixel n covers [n, n+1). No FPU, no random numbers anywhere in this scope;
one piece of self-modifying code (0x25a5ff, section 5.1).

Verified against the capture (scripts in `models/parts2d-b/`, see its README):
- the still screens of entries 16, 17, 24, 25, 29 and the end, modelled from the RAW files, equal **every record**
  they are shown in (all 64,000 indices and the 768 DAC values: 223, 696, 223, 223, 350 and 210 records);
- an integer-exact Python model of the plane routines renders **every record of entries 18, 19 and 23 exactly**
  (618 + 784 + 446 records, all 64,000 indices) with the angles stepped by the retrace routines;
- a model of the strobe counters and of the negative table 0x25a5ff gives **every record of entries 20 and 27**
  (398 + 390 records: black, the picture or its negative, all 64,000 indices), and entries 21 / 28 show the picture
  in every record.

---------------------------------------------------------------------------------------------------------------

## 1. What is on screen, and when

The music positions are order:row (the end positions of the script). Records from the capture (`full.log`,
`full.ticks`); the capture draws **one frame per refresh in every entry of this scope** (70.1 fps: `drawn` in
`full.ticks` advances by one per record).

| entry | records | music | what is shown | palette (DAC) |
|---|---|---|---|---|
| 16 | init 11702 (black); 11703..11925 | 44:00..45:00 | `GFX\JUNIOR.RAW` (214 x 137, "Junior .born to code") centred on black, at (53, 32) | JUNIOR's |
| 17 | init 11926 (the sprite half drawn in this record); 11927..12622 | 45:00..48:00 | `GFX\MRTOMMY.RAW` (200 x 200, a face with glasses in a dark disc) centred, at (60, 0) | MRTOMMY's |
| 18 | 12623..13240 | 48:00..49:00 | two textured planes interlaced line by line: even lines the 64 x 64 tile 0 of `GFX\TEXTURE.RAW`, odd lines `GFX\DADA.RAW` (a winged pony), same camera; the camera tilts over and over (one turn per 409.6 refreshes) and turns slowly; record 12623 is black (the eye starts in the plane) | DADA's (= TEXTURE's) |
| 19 | 13241..14024 | 49:00..50:00 | the same; the DADA layer now tilts and turns more slowly than the tile layer | DADA's |
| 20 | init 14025, 14026 (black); 14027..14424 | 50:00..51:48 | black, with one-refresh flashes of `GFX\GENE.RAW` ("Pulse Generation", full screen), alternately as it is and as a negative, every 16 frames, then every 15 | GENE's |
| 21 | 14425 (black), 14426..14480 | 51:48..52:00 | GENE, held | GENE's |
| 23 | init 15377, 15378 (black); 15379..15824 | 56:00..58:00 | the interlaced planes again: even lines `GFX\TOM.RAW`, odd lines `GFX\BOZ.RAW` (a clown face); the camera tilts up from 67.5 degrees to 43.9 degrees in 90 refreshes and stays; the two pictures turn at different speeds | TOM's (= BOZ's) |
| 24 | 15825..16047 | 58:00..59:00 | `GFX\DOG.RAW` (187 x 124, "Biscuit Addicted") centred, at (67, 38) | DOG's (= DADA's) |
| 25 | init 16048 (black); 16049..16271 | 59:00..60:00 | `GFX\MRWAAF.RAW` (320 x 200, "Waaf .the dog") full screen | MRWAAF's |
| 27 | init 16720, 16721 (black); 16722..17111 | 62:00..63:48 | as entry 20 with `GFX\BOUGE.RAW` (a puppet, full screen) | BOUGE's |
| 28 | 17112 (black), 17113..17167 | 63:48..64:00 | BOUGE, held | BOUGE's |
| 29 | init 17168 (black); 17169..17518 | 64:00..64:63 | `GFX\YOCOL.RAW` (320 x 200, a boy with pink hair) full screen | YOCOL's |
| end | 17519..17728 | 64:63 | `GFX\REVE.RAW` (227 x 80, the title "RÊVE") centred, at (47, 60); then the program ends (the capture stops at refresh 17729) | REVE's (= DADA's) |

None of these entries runs a palette effect: each palette is set once, by the loader, when the init loads the
picture, and stays.

## 2. Static data, buffers, variables

| what | where | format / content |
|---|---|---|
| file names | 0x205db0 `GFX\JUNIOR.RAW`, 0x205dbf `GFX\MRTOMMY.RAW`, 0x205d8b `GFX\DADA.RAW`, 0x205dea `GFX\GENE.RAW`, 0x205d98 `GFX\TOM.RAW`, 0x205da4 `GFX\BOZ.RAW`, 0x205dcf `GFX\DOG.RAW`, 0x205ddb `GFX\MRWAAF.RAW`, 0x205e5b `GFX\BOUGE.RAW`, 0x205e40 `GFX\YOCOL.RAW`, 0x205cfb `GFX\REVE.RAW` | zero-terminated strings (ESI of the loader calls) |
| pictures | archive files `GFX\*.RAW` | Alchemy "mhwanh": 32-byte header (big-endian width at +8, height at +0xa, palette entries at +0xc), 768 bytes of 8-bit palette, then width x height indices (NOTES.md) |
| texture sheet | `GFX\TEXTURE.RAW` in [0x21b140] (loaded by entry 0 and again by entry 12, used by the 3D engine) | 256 x 256; entry 18 uses its top-left 64 x 64 tile |
| sine table | 0x2176c8, 4096 int16 (index & 0xfff), static in the image | sin(2 pi i / 4096) * 32767, rounded (259 entries one off) |
| plane constants | eye 0x259934 = (0, 0x200, 0); corners 0x25994c = (0x80, 0, 0x80), 0x259958 = (0x80, 0, -0x80), 0x259964 = (-0x80, 0, 0x80), 0x259970 = (-0x80, 0, -0x80); translation 0x259928..0x259930 = 0 (never written) | int32 x 3 each, static |
| plane angles | [0x2598fc] = a (tilt), [0x259900] = b (turn) | int32, 4096 a turn (& 0xfff) |
| plane matrix, points, row state | 0x259904..0x259924 (matrix), 0x259940 / 0x25997c / 0x259988 / 0x259994 / 0x2599a0 (transformed points), 0x2599ac..0x2599f8 (row interpolants), 0x2599fc / 0x259a00 (pixel steps), 0x259a04 (row counter) | int32 |
| plane texture pointer | [0x21b144] | set by the frame routine before each layer |
| entry 18/19/23 angles | [0x217574] a1, [0x217578] b1, [0x21757c] a2, [0x217580] b2 (int32; 0 in the image; [0x217584] is set to 0 by entry 23 and not read) | stepped by the retrace routines (section 4.1) |
| 64 x 64 tile | [0x205e94] (the 4,096 bytes after [0x205e90]) | copied by entry 18's init |
| strobe state, entry 20 | [0x2059f0] flag = 0, [0x2059f4] count = 0x3e80, [0x2059f8] step = 0x3e8 | int32, initial values in the image (each entry runs once) |
| strobe state, entry 27 | [0x205ba4] flag = 0, [0x205ba8] count = 0x3e80, [0x205bac] step = 0x3e8 | same |
| current palette | 0x20613c, 768 bytes, 6-bit | written by the loader; re-read from the DAC by 0x25a5ff |
| negative table | 0x20643c, 256 bytes (the start of a 64 KB area that other entries use for SPIRBLUR.PAL etc.) | computed by 0x25a5ff (section 5.1) |
| buffers | [0x205e80] picture (GENE, BOUGE, DADA, TOM, JUNIOR, MRTOMMY, DOG, REVE), [0x205e84] BOZ, [0x205e8c] the planes' frame | 64 KB each (NOTES.md) |

## 3. The still pictures: entries 16, 17, 24, 25, 29 and the end

```c
// 0x205261 (JUNIOR), 0x2052a0 (MRTOMMY), 0x2052df (DOG), 0x20538d (the end: REVE)
memset(0xa0000, 0, 64000);                       // rep stosd, 0x3e80 dwords
load(name, [0x205e80], EAX = 1);                 // 0x25a1fb: DAC and 0x20613c = palette >> 2, then the pixels
sprite([0x205e80], 0xa0000, w = BL, h = BH, stride = EDX = w);   // 0x25f0be
// 0x20531e (MRWAAF), 0x205368 (YOCOL)
memset(0xa0000, 0, 64000);
load(name, 0xa0000, EAX = 1);                    // 320 x 200: straight onto the screen

// 0x25f0be: a w x h sprite centred on the screen, index 0 not drawn
void sprite(u8 *src, u8 *dst, int w, int h, int stride) {   // w, h are bytes (BL, BH)
    dst += (100 - (h >> 1)) * 320 + 160 - (w >> 1);
    for (y = 0; y < h; y++, dst += 320, src += stride)
        for (x = 0; x < w; x++) if (src[x]) dst[x] = src[x];
}
```

| picture | w x h (BL, BH, EDX) | top-left on screen | transparent |
|---|---|---|---|
| JUNIOR | 214 x 137 (0xd6, 0x89, 0xd6) | (53, 32) | index 0 |
| MRTOMMY | 200 x 200 (0xc8, 0xc8, 0xc8) | (60, 0) | index 0 |
| DOG | 187 x 124 (0xbb, 0x7c, 0xbb) | (67, 38) | index 0 |
| REVE (end) | 227 x 80 (0xe3, 0x50, 0xe3) | (47, 60) | index 0 |
| MRWAAF, YOCOL | 320 x 200, loaded to 0xa0000 | (0, 0) | none (every pixel) |

Index 0 is (0, 0, 0) in all these palettes, and the screen is cleared to 0 first, so the transparency only matters
for anti-aliased edges of a redraw: draw the redraw with its alpha over black. The retrace and frame routines are
empty: the picture stays as the init left it until the next entry's init. **The init overwrites the whole screen**
(the clear); it draws straight into 0xa0000, so the record taken while it runs can show a partial picture (record
11926: MRTOMMY half drawn). The end: main 0x204770 calls 0x20538d after the last entry, then the frame wait 0x2045d0
210 times (records 17519..17728 = 210 refreshes = 3.0 s), stops the music (0x2602c6), restores the timer (0x2044a4)
and returns; the entry then calls int 0FFh AX=0400h BL=1 and leaves.

All are pictures drawn as they are: candidates for high-resolution redraws (section 7).

**`reve/remake/src/scenes/stills.cpp` is right** as it stands: clear to black, the RAW picture through its own
palette, centred at (160 − (w >> 1), 100 − (h >> 1)) with w, h from the `mov bl` / `mov bh` immediates, or the
whole screen for MRWAAF and YOCOL (every pixel drawn there, index 0 included); REVE's palette is byte for byte
TEXTURE.RAW's. Nothing else is on screen and there is no palette change, fade or flash in these entries (all their
records equal the model, DAC included). The only differences in the capture are transients of the init: the record
of the init is black (the clear, the file still loading) and record 11926 shows MRTOMMY half drawn. The end screen
lasts 210 refreshes (17519..17728), then the program exits.

## 4. The interlaced planes: entries 18, 19 and 23

Two textured planes seen in perspective, drawn into the frame buffer [0x205e8c] on alternate lines: layer A on the
even lines 0, 2, .., 198, layer B on the odd lines 1, 3, .., 199. Each layer is one camera (tilt a, turn b) looking
at an infinite plane covered with its texture, tiled. Every frame draws all 200 lines (black where a row does not
hit the plane) and copies the buffer to 0xa0000 (rep movsd, 0x3e80 dwords): **the whole frame is overwritten**.

### 4.1 Init, retrace and frame routines

```c
// entry 18 init 0x20508d
load("GFX\\DADA.RAW", [0x205e80], EAX = 1);             // DADA's palette (identical to TEXTURE.RAW's)
for (y = 0; y < 64; y++) memcpy([0x205e94] + 64 * y, [0x21b140] + 256 * y, 64);   // TEXTURE.RAW tile (0, 0)
// a1, b1, a2, b2 are not set: they are 0 from the image (nothing else writes them before)

// entry 18 retrace 0x2050ce (every refresh)          // entry 19 retrace 0x2050ed
a1 += 10; b1 += 5; a2 += 10; b2 += 5;                 // a1 += 10; b1 += 5; a2 += 5; b2 += 2;

// entries 18 and 19 frame 0x20510c (every drawn frame)
[0x2598fc] = a1; [0x259900] = b1; [0x21b144] = [0x205e94]; plane64_even();      // 0x259a08
[0x2598fc] = a2; [0x259900] = b2; [0x21b144] = [0x205e80]; plane256_odd();      // 0x259c98: DADA
memcpy(0xa0000, [0x205e8c], 64000);

// entry 23 init 0x20516b
memset(0xa0000, 0, 64000);
load("GFX\\TOM.RAW", [0x205e80], EAX = 1);  load("GFX\\BOZ.RAW", [0x205e84], EAX = 0);   // BOZ's palette = TOM's
a1 = 0x300; b1 = 0; a2 = 0x300; b2 = 0; [0x217584] = 0;
// entry 23 retrace 0x2051dd
b1 += 8; b2 += 12; a1 -= 3; if (a1 <= 0x1f4) a1 = 0x1f4;
// entry 23 frame 0x20520c
[0x2598fc] = a1; [0x259900] = b1; [0x21b144] = [0x205e80]; plane256_even();     // 0x259b50: TOM
                 [0x259900] = b2; [0x21b144] = [0x205e84]; plane256_odd();      // 0x259c98: BOZ, same tilt a1
memcpy(0xa0000, [0x205e8c], 64000);                                             // (a2 is never read in entry 23)
```

Closed forms, with n = the number of calls of the entry's retrace routine so far. In the capture **record s + n
shows the frame drawn with n calls**, s = the record at which the retrace routine was set (`full.log`): s = 12623
(entry 18), 13241 (19), 15379 (23). So record 12623 shows n = 0 (all black: a = 0) and entry 19 starts with
N18 = 618 calls of entry 18's routine behind it.

| entry | layer A (even lines) | layer B (odd lines) |
|---|---|---|
| 18 | tile 0 of TEXTURE, 64 x 64: a = 10 n, b = 5 n | DADA, 256 x 256: a = 10 n, b = 5 n |
| 19 | tile: a = 10 (N18 + n), b = 5 (N18 + n) | DADA: a = 10 N18 + 5 n, b = 5 N18 + 2 n |
| 23 | TOM, 256 x 256: a = max(0x300 - 3 n, 0x1f4), b = 8 n | BOZ, 256 x 256: a = the same, b = 12 n |

In degrees: a full tilt turn takes 409.6 refreshes at 10 a refresh (5.84 s), 819.2 at 5; a full turn of b takes
819.2 refreshes at 5, 2048 at 2, 512 at 8, 341.3 at 12. Entry 23's tilt goes from 67.5 degrees (0x300) to 43.9
degrees (0x1f4 = 500, reached at n = 90: 0x300 - 3 * 89 = 501, then clamped) and stays.

### 4.2 The camera: 0x259de4 and 0x259ed0

```c
// S(i) = int16 at 0x2176c8 + 2 (i & 0xfff);  mul30(x, y) = (int32)(((int64)x * y) >> 30)   (imul; shld edx, eax, 2)
a = [0x2598fc] & 0xfff;  b = [0x259900] & 0xfff;
sa = S(a) << 15; ca = S(a + 0x400) << 15; sb = S(b) << 15; cb = S(b + 0x400) << 15;   // 2.30 fixed point, ~ +-1
m[0] = { cb,  mul30(sb, ca), mul30(sb, -sa) };   // [0x259904] [0x259910] [0x25991c]
m[1] = { -sb, mul30(cb, ca), mul30(cb, -sa) };   // [0x259908] [0x259914] [0x259920]
m[2] = { 0,   sa,            ca             };   // [0x25990c] [0x259918] [0x259924]
// 0x259ed0: q[r] = mul30(p.x, m[r][0]) + mul30(p.y, m[r][1]) + mul30(p.z, m[r][2]) + 0     (integers: whole units)
E  = M (0, 512, 0)        // 0x259934 -> 0x259940   the eye
P0 = M (128, 0, 128)      // 0x25994c -> 0x25997c   view quad: top left
P1 = M (128, 0, -128)     // 0x259958 -> 0x259988   bottom left
P2 = M (-128, 0, 128)     // 0x259964 -> 0x259994   top right
P3 = M (-128, 0, -128)    // 0x259970 -> 0x2599a0   bottom right
```

M is a tilt by a about the x axis (y toward z) followed by a turn by -b about the vertical z axis:
`M p = (cb x + sb y', -sb x + cb y', sa y + ca z)` with `y' = ca y - sa z`. The world's z is height; the texture
plane is z = 0. The camera looks from E toward the origin through a 256 x 256 view quad at distance 512 (a field of
view of 2 atan(1/4) = 28.1 degrees both ways, shown on a 4:3 screen). a = 0: the eye is in the plane (height 0,
looking along it); a = 1024: 512 above the origin, looking straight down; a = 2048: in the plane again, upside down;
above 2048 the eye is below the plane and sees its underside. Because the quad's rows have the same height for every
x (`m[2][0] = 0`), every screen row is parallel to the plane: one division per row is exact perspective.

### 4.3 The rows: 0x259f70, 0x25a048 and the row drawers

```c
// 0x259f70: the quad's left (x = +128) and right (x = -128) edges, 100 rows from z = +128 down to z = -128
for c in x, y, z:
    dL[c] = ((P1[c] - P0[c]) << 16) / 100;   dR[c] = ((P3[c] - P2[c]) << 16) / 100;   // idiv: toward 0
    L[c]  = P0[c] << 16;                     R[c]  = P2[c] << 16;                     // 16.16

// 0x25a048, called once per row k = 0..99; returns du, dv, u0, v0
Lx = L.x >> 8; Ly = L.y >> 8; Lz = L.z >> 12;          // 24.8, 24.8, 28.4 (sar)
Rx = R.x >> 8; Ry = R.y >> 8; Rz = R.z >> 12;
Ex = E.x << 8; Ey = E.y << 8; num = (E.z << 4) << 16;  // num: 32 bits, then cdq
den = (E.z << 4) - Lz;
if (den == 0) { du = dv = 1; u0 = num < 0 ? -1 : 0; v0 = Lz; goto next; }     // quirk, section 4.6
t = num / den;                                          // idiv: the ray parameter, 16.16
if (t < 0) { du = dv = 0; goto next; }                  // the row looks away from the plane: black
uL = (int32)(((int64)(Lx - Ex) * t) >> 16) + Ex;        // shrd eax, edx, 16: the hit point, 24.8
vL = (int32)(((int64)(Ly - Ey) * t) >> 16) + Ey;
// the same for the right end with Rz, Rx, Ry -> t2, uR, vR (Rz == Lz always, so t2 == t)
du = uR - uL;  dv = vR - vL;  u0 = uL * 320;  v0 = vL * 320;                  // 32-bit
next: L += dL; R += dR;                                                        // also after the early exits

// 0x259b50 (lines 0, 2, ..), 0x259c98 (lines 1, 3, ..): texture 256 x 256 at [0x21b144]
for (k = 0; k < 100; k++) {
    u8 *row = [0x205e8c] + (2 * k + first) * 320;
    (du, dv, u0, v0) = row_0x25a048();
    if (du == 0 && dv == 0) { memset(row, 0, 320); continue; }      // black row (t < 0, or t == 0)
    u32 u = u0 << 8, v = v0 << 8, su = du << 8, sv = dv << 8;
    for (i = 0; i < 320; i++) { row[i] = tex[(u >> 24) << 8 | (v >> 24)]; u += su; v += sv; }
}
// 0x259a08 (lines 0, 2, ..): texture 64 x 64: shifts 10 instead of 8, texel tex[(u >> 26) << 6 | (v >> 26)]
```

So pixel i of row k samples the hit point of the ray from E through `L(k) + (i / 320) (R(k) - L(k))`, and the texel
is **row = floor(1.25 Hx) mod N, column = floor(1.25 Hy) mod N** (H = the hit point in world units, N = 256 or 64):
both drawers have the same scale, 1.25 texels per world unit (u0 = 256 * 320 * Hx, texel = u0 * 2^8 / 2^24), so the
64 x 64 tile repeats four times as often as a 256 x 256 picture. Looking straight down (a = 1024) one texel is one
pixel across and 1.6 lines down. The texture's rows run across the screen (world x is the screen's horizontal axis
at b = 0, pointing left), so the pictures appear turned on their side; at b = 0 texel (0, 0) is below the eye, i.e.
at the screen centre when looking straight down. The index is written as it is: **no shade or blend table**; index
0 (the pictures' black background) is drawn black.

### 4.4 The same in floating point (what the remake should evaluate)

For a layer with angle words (a, b): α = 2π a / 4096, β = 2π b / 4096, and

```
R(x, y, z) = (cos β · x + sin β · y',  −sin β · x + cos β · y',  sin α · y + cos α · z),   y' = cos α · y − sin α · z
E = R(0, 512, 0)
Q = R(128 − 0.8 sx, 0, 128 − 2.56 k)      sx = screen x in [0, 320) (the original samples pixel i at sx = i),
                                          k = plane row in [0, 100): the original's row k is lines 2k (A), 2k+1 (B)
t = E.z / (E.z − Q.z);   t <= 0 (or no intersection): black (index 0)
H = E + t (Q − E);       texel coordinates (row, column) = (1.25 H.x, 1.25 H.y), wrapped mod N
```

`models/parts2d-b/plane.py` `draw_float` is this form; it picks the same texel as the integer code for 56-62 % of
the pixels of entries 18/19 (the 64 x 64 tile is a fine noise texture) and 81-87 % for entry 23; the texel
coordinates differ by 1 to 4 texels (more toward the horizon) because the original rounds E and the corners down to
whole world units (E = (104, 446, 227) instead of (104.6, 446.6, 227.4) at a = 300, b = 150), which also makes the
motion jitter slightly from frame to frame. The remake should use the float form with continuous angles.

### 4.5 Time base

The angles are stepped **per refresh** by the retrace routines (closed forms in 4.1, linear in n: evaluate them at
the fractional n = r − start). The frame routine only reads them, so nothing is frame-rate dependent; the capture
draws every refresh (618 frames in entry 18's 618 refreshes).

### 4.6 Shortcuts to evaluate exactly, quirks

- Integer camera: E and the corners are whole world units (floor of 512 sin, 128 cos ...); the rows' t in 16.16,
  the heights in 1/16, x and y in 1/256 units. Use floats (section 4.4).
- 100 plane rows per layer, each shown on one line: in the remake compute every output pixel's own ray (continuous
  vertical position) rather than 100 rows.
- Nearest-texel reads with severe minification toward the horizon (the rows near t → ∞ alias into noise): filter
  in colour, with mip-maps or anisotropic filtering, wrap (repeat) addressing.
- The texture sheet's 64 x 64 tile on the even lines of entries 18/19 is read through DADA's palette, which is the
  same as TEXTURE.RAW's own (all 768 bytes equal), so it shows in its own colours.
- **The line interlace** is the effect's way of showing two layers at once. On the VGA (mode 13h is double-scanned)
  it looks like two images mixed with fine horizontal stripes, each layer on alternate lines. Recommended: keep the
  alternation in bands of one original line (1/200 of the screen height; layer A on the bands 2k, B on 2k + 1),
  each band rendered at full resolution from its own continuous rays; a 50 % blend of the two layers would be the
  alternative if the stripes are counted as a 320 x 200 artefact. (The coordinator's choice; the content is "two
  planes overlaid".)
- Black rows: a row is black when t < 0 (it looks away from the plane) and also when t = 0 (the eye exactly in the
  plane, du = dv = 0: the whole layer is black at a = 0 and a = 2048). The horizon appears only while the eye passes
  through the plane: in entry 18 around n = 0, 205, 410 and 615 (rows go black from one side for about 30
  refreshes); the rest of the time the plane fills the screen. In entry 23 the tilt stays between 43.9 and 67.5
  degrees and no row is black.
- Quirk not worth porting: when the eye's height equals a row's height exactly (den = 0) the row is drawn with
  steps of 1/65536 texel from a meaningless start: a single-colour row. It does not occur in the capture.
- The texel index is not filtered or shaded: index 0 is black both as the pictures' background and as "no plane".

## 5. The strobes: entries 20 / 21 (GENE) and 27 / 28 (BOUGE)

A full-screen picture flashed for one frame at a time on black, alternately as it is and as its negative, at
intervals that shorten from 16 frames to 15 (and would go on shortening); then the next entry holds the picture.

### 5.1 The negative table: 0x25a5ff (called by both inits after loading the picture)

```c
// read the DAC: outb(0x3c7, 0); rep insb 768 bytes from 0x3c9 to 0x20613c  (the palette just set, 6-bit)
for (c = 0; c < 256; c++) {
    tr = 63 - pal[c].r; tg = 63 - pal[c].g; tb = 63 - pal[c].b;     // patched into the code as the immediates of
                                                                    // 'sub al, imm' at 0x25a659, 0x25a666, 0x25a676
    best = 0xffffffff;
    for (j = 0; j < 256; j++) {                                     // signed 8-bit differences, exact for 0..63
        d = sq(pal[j].r - tr) + sq(pal[j].g - tg) + sq(pal[j].b - tb);
        if (d < best) { best = d; neg[c] = j; }                     // strict: the lowest index wins a tie
    }
}   // neg = 0x20643c, 256 bytes
```

Each index maps to the palette entry nearest to its inverse colour (63 − r, 63 − g, 63 − b). Black (index 0) maps to
white exactly in both palettes. The rest is close: per channel the table's colour is 2.2 six-bit units from the
true inverse on average for GENE and 1.0 for BOUGE (pixel-weighted), at most 18 / 15 (a few hundred pixels with an
error of 8 or more: GENE index 55, (35, 6, 33), maps to (43, 58, 38) instead of (28, 57, 30)). The capture's negative
frames equal the model table's mapping in every pixel (the entries of the used indices are verified; the table in
memory is overwritten by the following 3D entries before the next snapshot).

**Remake:** the table approximates the inverse colour with the palette's own colours (a palette quantisation).
Show the negative as the exact inverse in colour, 1 − c per channel (c = the displayed colour, v / 63), of the
picture or of its redraw.

### 5.2 The frame routines

```c
// entry 20 init 0x205924 / entry 27 init 0x205ad8
memset(0xa0000, 0, 64000);
load("GFX\\GENE.RAW" / "GFX\\BOUGE.RAW", [0x205e80], EAX = 1);
negative_table();                                               // 0x25a5ff

// entry 20 frame 0x205950 (variables 0x2059f0..f8) / entry 27 frame 0x205b04 (0x205ba4..ac)
count -= step;                                                  // count = 16000, step = 1000, flag = 0 initially
if (count > 0) memset(0xa0000, 0, 64000);                       // black
else {
    count = 16000; step += 5; flag ^= 1;
    if (flag) memcpy(0xa0000, [0x205e80], 64000);              // the picture
    else for (i = 0; i < 64000; i++) ((u8 *)0xa0000)[i] = neg[[0x205e80][i]];   // the negative
}

// entries 21 and 28 frame 0x2059fc
memcpy(0xa0000, [0x205e80], 64000);                             // the picture (GENE / BOUGE), every frame
```

After flash j the step is 1000 + 5 j and the next flash comes ceil(16000 / (1000 + 5 j)) frames later. So the
F-th frame of the entry is a flash at F = 16, 32, .., 224 (flashes 1..14, 16 frames apart), then 239, 254, .., 449
(flashes 15..29, 15 apart), then 14 apart (from step 1145) ... Odd flashes show the picture, even flashes the
negative. Each flash lasts one frame;
every other frame is black. The palette is the picture's own throughout (the negative is made by indices, not by
the palette). Every frame writes all 64,000 bytes of 0xa0000 directly.

### 5.3 Time base

**Per drawn frame**: the counter does not read a clock, so the strobe is frame-rate dependent. The capture draws one
frame per refresh in both entries (398 frames in entry 20's 398 refreshes, 390 in entry 27's 390), and frame F is
shown in record s + F − 1 (s = 14027 for entry 20, 16722 for entry 27): the flashes are at records 14042 (picture),
14058 (negative), 14074, .., 14250, 14265, .., 14415, i.e. 25 flashes in entry 20 (the 25th, at frame 389, shows the
picture), and likewise at 16737, 16753, .. in entry 27 (25 flashes). Entry 21 shows GENE from record 14426, entry 28
BOUGE from 17113 (the first frame of an entry whose init is empty is drawn after the next retrace).

**Remake:** step the counter once per refresh (as the capture does) from the entry's start: frame F at refresh
start + F − 1, a flash lasting exactly one refresh (1/70 s). A display running slower than 70 Hz can miss a
one-refresh flash entirely (at 60 Hz about one in seven); to keep every flash visible, show a flash in any displayed
frame whose interval of refreshes contains a flash refresh (the porter's choice).

## 6. What each entry leaves on screen

| entry | buffer drawn into | reaches 0xa0000 | whole frame |
|---|---|---|---|
| 16, 17, 24 | 0xa0000 (clear + sprite, once, in the init) | directly | yes (the clear) |
| 25, 29 | 0xa0000 (clear + the picture loaded onto it, once) | directly | yes |
| 18, 19, 23 | [0x205e8c], all 200 lines | rep movsd of 64,000 bytes at the end of each frame | yes |
| 20, 27 | 0xa0000, each frame | directly (clear, copy or table copy) | yes |
| 21, 28 | [0x205e80] (the picture) | rep movsd each frame | yes |
| end | 0xa0000 (clear + sprite, once) | directly | yes |

## 7. Pictures for the redraws

Written by `models/parts2d-b/pictures.py dump` to `reve/re/dump/pictures/` (ignored by Git): RGBA PNGs of the
original pixels through the palette the demo shows them with (8-bit palette >> 2 as the DAC gets it, scaled by
255/63), alpha 0 at the transparent index. None is a photo; all are drawn.

| file | size | palette shown with | used as | transparent | redraw? |
|---|---|---|---|---|---|
| JUNIOR.png | 214 x 137 | its own | still, centred (entry 16) | index 0 | yes |
| MRTOMMY.png | 200 x 200 | its own | still, centred (17) | index 0 | yes |
| DOG.png | 187 x 124 | its own (= DADA's) | still, centred (24) | index 0 | yes |
| MRWAAF.png | 320 x 200 | its own | full screen (25) | none | yes |
| YOCOL.png | 320 x 200 | its own | full screen (29) | none | yes |
| REVE.png | 227 x 80 | its own (= DADA's, = TEXTURE's) | the end screen, centred (also entry 0's title, another scope) | index 0 | yes |
| GENE.png | 320 x 200 | its own | full screen, flashed as it is and as a negative (20, 21) | none | yes: the negative is the redraw's inverse colour (5.1) |
| BOUGE.png | 320 x 200 | its own | the same (27, 28) | none | yes, as GENE |
| DADA.png | 256 x 256 | its own | texture of the odd-line plane (18, 19), tiled | none (index 0 drawn black) | yes, as a texture: keep the 256 x 256 frame (the tiling period) and the black border (all four edges are black in all three pictures, so the tiling has no seams) |
| TOM.png | 256 x 256 | its own | texture of the even-line plane (23) | none | yes, as DADA (the same face as MRTOMMY, framed differently) |
| BOZ.png | 256 x 256 | TOM's (the files' palettes are identical) | texture of the odd-line plane (23) | none | yes, as DADA |
| TEXTURE-tile0.png | 64 x 64 | TEXTURE's (= DADA's) | texture of the even-line plane (18, 19): the top-left tile of `GFX\TEXTURE.RAW`, the 3D engine's texture sheet | none | follow the 3D scope's decision for TEXTURE.RAW (a 4x upscale at most); it is a fine noise texture |

The planes read DADA, TOM, BOZ and the tile as they are (no tables), so a redraw at a higher resolution can replace
them directly; it must keep the texture's frame because the plane maps texel (row, column) = (1.25 H.x, 1.25 H.y)
mod 256 (mod 64 for the tile).

## 8. What I could not determine

- Nothing open in the algorithms: every record of every entry in the scope is reproduced exactly.
- The negative table's 256 entries are verified only through the indices the pictures use (100 for GENE, 65 for
  BOUGE); the other entries do not matter.
- The den = 0 quirk (4.6) is read from the code only; it does not occur in the capture.
