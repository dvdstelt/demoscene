# Scope "parts2d-a": the title, PULSE, the rotozoomers with WIZ, the ring twirls, the feedback spiral

Script entries (table 0x205ed0, 0x14 bytes each: init, done, retrace routine, frame routine, end position):

| entry | init | done | retrace | frame | ends at | what |
|---|---|---|---|---|---|---|
| 0 | 0x204840 | – | – | 0x20489c | 0x100 | the title `GFX\REVE.RAW` |
| 5 | 0x204aef | – | – | 0x204b2c | 0x530 | `GFX\PULSE.RAW` flashing, plain and negative, between black frames |
| 6 | – | – | – | 0x204bd8 | 0x600 | `GFX\PULSE.RAW` steady |
| 7 | 0x204bed | – | 0x204c5d | 0x204cb4 | 0xa00 | two interleaved rotozoomers, `GFX\WIZ.RAW` pulsing in size |
| 9 | 0x204de0 | – | 0x204ef8 | 0x204fe8 | 0x1600 | `GFX\PLAYMO.RAW` twisted ring by ring |
| 10 | – | 0x204ee9 | 0x204f6f | 0x204fe8 | 0x1800 | the same, wider and faster |
| 12 | 0x20562c | – | 0x2056f3 | 0x20578c | 0x2000 | rotozoomer + zooming feedback blur + a spiral of 30 dots |
| 13 | 0x20569c | – | 0x2056f3 | 0x20578c | 0x2400 | the same, second half of the key list |
| 15 | 0x2053cc | 0x204ee9 | 0x2054c4 | 0x205578 | 0x2c00 | `GFX\BOUCHE.RAW` twisted ring by ring |

("–" = the empty routine 0x20483f.) Shared helpers: the sprite routine 0x25f0be, the picture loader 0x25a1fb, the
code patcher 0x25a1bc, the rotozoom layers 0x25a69c / 0x25a73c with the unrolled code 0x25a7dd..0x25eae9, the
feedback zoom 0x25eaec, the dot spiral 0x25f038 / 0x25f094, and the unused 0x25a275 (section 8).

Conventions. Addresses are linear (NOTES.md). "Record r" = record r of `full.frames` = the screen at the start of
refresh r's vertical retrace (70.086 Hz). VGA coordinates: x 0..320, y 0..200, pixel (x, y) covers [x, x+1) x
[y, y+1). Music positions are order:row in decimal as `full.log` prints them; the end positions in the table are
order << 8 | row. All code in this scope is integer (no FPU), and none of it uses random numbers.

**Verified** (models in `models/parts2d-a/`, see its README): Python models of every entry reproduce **every
record** of the entry exactly (all 64,000 indices; DAC checked separately): entry 0 records 32..651, entries 5/6
2737..3192, entry 7 3194..4088 (from the image's initial state), entries 9/10 6331..7223, entries 12/13
8123..9910 (from the image's state and entry 7's end), entry 15 10810..11701 (ring pixels from its start; all
pixels from snapshot f11200). `GFX\SPIRBLUR.PAL` is exactly what 0x25a275 computes from `TEXTURE.RAW`'s palette, and
`GFX\NEGATIF.PAL` is exactly the nearest-colour negative of `PULSE.RAW`'s palette. Every entry of this scope draws
one frame per refresh in the capture (`rates.py`); none changes the DAC while it runs (`dac.py`).

---------------------------------------------------------------------------------------------------------------

## 0. Timing rule for all entries

Main (NOTES.md) calls init with the empty retrace routine, sets the entry's retrace routine, then loops "wait for a
retrace (0x2045d0), frame routine". The retrace routine is called at every retrace from the refresh `S` at which it
was set (`full.log`: "entry N: retrace routine set at frame S"). With one frame per refresh, **record r shows the
frame drawn after n = r − S calls of the retrace routine** (n ≥ 1; for entries whose init took longer than a
refresh, the first frame is drawn at once with n = 0 and shows in record S if it finishes before that retrace).
Per-frame state (entries 5, 7, 12, 13) advances once per drawn frame = once per refresh here.

| entry | init at | S (retrace set) | records of its frames | music | notes |
|---|---|---|---|---|---|
| 0 | 30 | 32 | 32..651 | 0:00 .. 1:00 | first frame at once (init 2 refreshes: TEXTURE.RAW); static |
| 5 | 2736 | 2737 | 2737..3136 (frame k = record 2737 + k) | 4:00 .. 5:48 | record 2736 is black (init clears 0xa0000) |
| 6 | 3137 | 3137 | 3138..3192 | 5:48 .. 6:00 | record 3137 repeats entry 5's last (black) frame; record 3193 shows PULSE through WIZ's palette (entry 7's init sets the DAC first: a one-refresh loading glitch, drop it) |
| 7 | 3193 | 3193 | 3194..4088 | 6:00 .. 10:00 | entry 8's init at 4089 |
| 9 | 6328 | 6331 | 6331..6776 | 20:00 .. 22:00 | records 6329, 6330: the init has copied PLAYMO to the screen (untwisted) |
| 10 | 6776 | 6776 | 6777..7223 | 22:00 .. 24:00 | entry 11's init at 7224 |
| 12 | 8119 | 8122 | 8123..9015 | 28:00 .. 32:00 | the n = 0 frame is not finished at record 8122 (it shows entry 11's last frame through TEXTURE's palette) |
| 13 | 9015 | 9015 | 9016..9910 | 32:00 .. 36:00 | entry 14's init at 9911 |
| 15 | 10806 | 10809 | 10810..11701 | 40:00 .. 44:00 | records 10807..10809 black (init clears 0xa0000), n = 0 frame not finished in time |

---------------------------------------------------------------------------------------------------------------

## 1. Entry 0: the title (0x204840, 0x20489c)

Black screen with the white "REVE" logo (`GFX\REVE.RAW`, 227 x 80) centred, static from record 32 to 651 (entries
1..4 follow; their frames are the 3D scope's).

```c
void init0(void)                                   // 0x204840
{
    load_picture("GFX\\TEXTURE.RAW" /*0x205ceb*/, buf_21b140, 1);   // 0x25a1fb, palette -> DAC and 0x20613c
    for (row = 0; row < 16; row++)                 // 16 x 16 block at TEXTURE + 0x4020 (x 32..47, y 64..79)
        memcpy(tex16 /*[0x205e90]*/ + 16 * row, buf_21b140 + 0x4020 + 256 * row, 16);   // never shown (below)
    load_picture("GFX\\REVE.RAW" /*0x205cfb*/, buf_205e80, 0);    // pixels only
}

void frame0(void)                                  // 0x20489c, every refresh
{
    sprite(buf_205e8c, buf_205e80, /*w*/ 0xe3, /*h*/ 0x50, /*stride*/ 0xe3);   // 0x25f0be
    memcpy(0xa0000, buf_205e8c, 64000);
    memset(buf_205e8c, 0, 64000);                  // the buffer is left cleared
}
```

`sprite` (0x25f0be, section 8.1) puts the top-left corner at (160 − (227 >> 1), 100 − (80 >> 1)) = **(47, 60)**,
skipping index 0. The frame overwrites every pixel (copy of the whole buffer); everything outside the logo is index
0 = black. Palette: `TEXTURE.RAW`'s (REVE.RAW's own palette is identical); REVE uses indices 1 (63,63,63) white,
69 (41,41,42) and 74 (20,21,21) greys on the letter edges, 79 (0,0,0). No animation, no palette effect.

The 16 x 16 block copied to [0x205e90] is not used: the only readers of [0x205e90] are the rotozoom code (patched
by 0x25a1bc), which runs only in entries 7, 12 and 13, and entry 7's init overwrites the block first.

Remake: draw the REVE.RAW redraw at (47, 60) .. (274, 140) on black. (Entries 1..4 call the same sprite routine
from their frame routines: 0x2049f6, 0x204a42, 0x204a88, 0x204ad6; that is the 3D scope's.)

## 2. Entries 5 and 6: PULSE, flashing negative (0x204aef, 0x204b2c, 0x204bd8)

Black with single-frame flashes of `GFX\PULSE.RAW` (a drawn alien face above the word "PULSE", 320 x 200, full
screen), alternately normal and negative, about 4.4 times a second; then (entry 6, 0.8 s) the picture steadily.

```c
void init5(void)                                   // 0x204aef
{
    memset(0xa0000, 0, 64000);
    load_picture("GFX\\PULSE.RAW" /*0x205d46*/, buf_205e80, 1);   // palette -> DAC
    read_file("GFX\\NEGATIF.PAL" /*0x205d54*/, 0x20643c, 256);    // service 0209h: an index map
}

int toggle = 0, count = 16000, step = 1000;        // dwords 0x204bcc, 0x204bd0, 0x204bd4 (image values)

void frame5(void)                                  // 0x204b2c, per drawn frame
{
    count -= step;
    if (count > 0) { memset(0xa0000, 0, 64000); return; }      // black (index 0 = 0,0,0)
    count = 0x3e80;                                // 16000
    step += 5;
    toggle ^= 1;
    if (toggle) memcpy(0xa0000, buf_205e80, 64000);             // PULSE as it is
    else for (i = 0; i < 64000; i++) vga[i] = negatif[buf_205e80[i]];   // through the map at 0x20643c
}

void frame6(void) { memcpy(0xa0000, buf_205e80, 64000); }     // 0x204bd8
```

Time base: **per drawn frame** (no clock read; frame-rate dependent); the capture draws one frame per refresh, so
frame k = record 2737 + k. Flash m (m = 1, 2, ...) comes ceil(16000 / (1000 + 5 (m − 1))) frames after the previous
one, the first at k = 15: frames 15, 31, 47, ... (every 16 frames for 13 flashes, then every 15); 25 flashes in
entry 5, odd ones plain, even ones negative; each lasts exactly one frame (one refresh, 14.3 ms). Records:
2752p 2768n 2784p 2800n 2816p 2832n 2848p 2864n 2880p 2896n 2912p 2928n 2944p 2960n 2975p 2990n 3005p 3020n 3035p
3050n 3065p 3080n 3095p 3110n 3125p. Entry 6 shows the plain picture in every record 3138..3192.

`GFX\NEGATIF.PAL` (256 bytes) is, for every index i, the index of PULSE's palette nearest (squared RGB distance,
first of equals) to (63 − r, 63 − g, 63 − b) of colour i: verified for all 256 entries. So the negative frames are
the colour negative, quantised to the palette. Both draw directly into VGA memory and overwrite every pixel.

Remake: show PULSE (the redraw) for the plain flashes and entry 6, its **colour negative 1 − c** (per channel, in
display space) for the negative flashes, black otherwise. Step the counter per refresh from the entry start. A flash
lasts one refresh of the original (1/70.086 s): at display rates below 70 Hz a point-sampled time can miss it, so
show a flash in the display frame whose interval contains the flash refresh (or average over the interval).

## 3. Entry 7: two interleaved rotozoomers and WIZ (0x204bed, 0x204c5d, 0x204cb4)

Full screen of two rotating texture layers (a 64 x 64 burst texture, large, and a 16 x 16 speckled texture, small,
turning the other way), mixed pixel by pixel; in the middle the cartoon character `GFX\WIZ.RAW` (87 x 124, a grey
round head on legs) pulsing between 20 % and 96 % of its size, 2.7 times a second.

### 3.1 Init and state

```c
void init7(void)                                   // 0x204bed; [0x21b140] still holds TEXTURE.RAW (entry 0)
{
    copy 16 x 16 from buf_21b140 + 0x6010 (x 16..31, y 96..111) to tex16 [0x205e90];   // stride 256
    copy 64 x 64 from buf_21b140 + 0x4040 (x 64..127, y 64..127) to tex64 [0x205e94];  // = tile 5 (row 1, col 1)
    load_picture("GFX\\WIZ.RAW" /*0x205d64*/, buf_205e80, 1);  // palette -> DAC: identical to TEXTURE.RAW's
}
```

| variable | address | initial (image) | |
|---|---|---|---|
| a | dword 0x2174c8 | 0 | rotation angle, a byte offset into the sine table (2 per table step) |
| p | dword 0x2174cc | 0 | phase of the angle's speed (byte offset) |
| z | byte 0x217570 | 0xc8 = 200 | WIZ's scale, 256 = 1 |
| dz | byte 0x217571 | 0x0f = +15 | z's step (signed) |
| layer cycle | 0x2174d4 / d8 / dc, 0x2174e0 / e4 / e8 | 0, 0, 0x25a7dd; 1, 0x140, 0x25c963 | section 3.3 |

Entry 7 is the first user of all of them, so it starts from the image's values (checked: the model from these
values matches from record 3194 on). Entry 12 later continues a, p and layer 1's cycle from where entry 7 left them.

### 3.2 Retrace routine (per refresh)

```c
void retrace7(void)                                // 0x204c5d
{
    p += 10;
    a += SIN[(p & 0x1ffe) / 2] >> 9;               // signed word >> 9 (arithmetic, = floor(s / 512)): -64..63
    z = (uint8_t)(z + dz);
    if (dz > 0) { if (z == 0xf5) dz = -dz; }       // bounces exactly at 245 and 50 (200 + 15k hits both)
    else        { if (z == 0x32) dz = -dz; }
}
```

`SIN` = 0x2176c8: 5,120 signed words, round(32767 sin(2π i / 4096)) within ±1 (static data in the image; the cosine
table 0x217ec8 is the same table from word 1024 on). After n calls (n = r − 3193):
- p_n = 10 n; a_n = Σ_{i=1..n} floor(SIN[(5 i) mod 4096] / 512): the angle's speed is a sine of period 4096 / 5 =
  819.2 refreshes (11.7 s), up to ±63 byte offsets = ±31.5 table steps = ±2.77° per refresh. Replay this integer
  sum (no closed form is exact: the floor bias is not constant); in the capture a stays between −411 and 16,474 and
  is 975 after entry 7's 896 calls.
- z_n = 50 + 15 tri(n + 10), tri(m) = (m mod 26) if (m mod 26) ≤ 13 else 26 − (m mod 26): a triangle wave between
  50 and 245, period 26 refreshes (checked against the recurrence for 2,000 steps). Linear between steps.

### 3.3 Frame routine (per drawn frame)

```c
void frame7(void)                                  // 0x204cb4
{
    layer1(buf_205e8c, a, tex16);                  // 0x25a69c
    layer2(buf_205e8c, a, tex64);                  // 0x25a73c
    wiz(buf_205e8c, buf_205e80, z);                // inline, below
    memcpy(0xa0000, buf_205e8c, 64000);            // the buffer is NOT cleared
}
```

**Rotozoom layer 1** (0x25a69c, section 8.3 for the unrolled code):

```c
int idx = a & 0x1ffe;                              // even byte offset: table step floor(a / 2) mod 4096
int32 S = SIN[idx/2] << 16 >> 4, C = COS[idx/2] << 16 >> 4;   // = sin, cos words * 4096
uint32 v = C * -80 + S * -50, u = S * -80 - C * -50;          // 4.28 fixed point, wrapping
for (j = 0; j < 100; j++) {                        // [0x2174d0] = 100 lines, two screen rows apart
    uint32 uu = u, vv = v;
    for (k = 0; k < 160; k++) {                    // one texel per written pixel
        texel = tex16[(uu >> 28) << 4 | (vv >> 28)];   // row = top 4 bits of u, column = top 4 bits of v
        buf[(2 * j + row1) * 320 + 2 * k + col1] = texel;
        uu += S; vv += C;
    }
    u -= C; v += S;
}
```

**Layer 2** (0x25a73c) is the same with angle index `(-a) & 0x1ffe` (the opposite rotation), `S, C = word << 16 >> 5`
(× 2048, 6.26 fixed point), texel = `tex64[(uu >> 26) << 6 | (vv >> 26)]`, and its own row / column offsets.

So texel (k, j) of a layer sits at screen pixel (2k + col, 2j + row): each call writes **a quarter of the pixels**
(every other column of every other row). The column parity is the unrolled code variant (0x25a7dd writes bytes 0
and 2 of each dword = even columns, 0x25b8a0 bytes 1 and 3 = odd columns; 0x25c963 / 0x25da26 for layer 2), the
row parity is the dword offset 0 / 0x140 added to the buffer. After each call:

```c
code1 ^= 0x25a7dd ^ 0x25b8a0;  t1 ^= 1;  if (t1) off1 ^= 0x140;     // 0x25a717..: layer 1
code2 ^= 0x25c963 ^ 0x25da26;  t2 ^= 1;  if (t2) off2 ^= 0x140;     // 0x25a7b9..: layer 2
```

From the image's values the two cycles run in lockstep: frame 4m+1: L1 (even x, even y), L2 (even x, odd y); 4m+2:
L1 then L2 both (odd x, odd y); 4m+3: L1 (even x, odd y), L2 (even x, even y); 4m+4: L1 then L2 both (odd x, even
y). Layer 2 overwrites layer 1 on every other frame, so at any time **layer 1 is visible on 25 % of the pixels and
layer 2 on 75 %** (`layers.py`): odd columns always show layer 2 (rewritten every 4 frames), even columns alternate
between the layers every 2 frames. On screen this is a fine flickering checker mix of the two textures.

Mapping, in screen terms: texel coordinates of layer 1 at screen point (x, y) (taking the 2 x 2 block's centre
(2k + 1, 2j + 1) as its sample point) are

    u = (sin θ1 (x − 161) − cos θ1 (y − 101)) / 4,   v = (cos θ1 (x − 161) + sin θ1 (y − 101)) / 4   (× 32767/32768)
    texel = tex16[floor(u) mod 16][floor(v) mod 16]      (row u, column v),   θ1 = 2π floor(a / 2) / 4096

and for layer 2, `θ2 = 2π ((−a) & 0x1ffe) / 2 / 4096` (= −θ1 up to one table step), `/ 2` instead of `/ 4`, and the
64 x 64 block (mod 64). One texel of layer 1 is 4 x 4 VGA pixels, of layer 2 2 x 2 VGA pixels; the 16 x 16 tile
repeats every 64 pixels, the 64 x 64 tile every 128. The texture origin (corner of four texels) is at (161, 101).

**The sprite** (inline in 0x204cb4): WIZ.RAW at [0x205e80] (stride 87), index 0 transparent:

```c
y = 100 - (z >> 2);  x0 = 160 - (z >> 2);          // top-left corner (both use z / 4)
uint8 ch = 0;
for (r = 0; r < 124; r++) {
    if (!carry(ch += z)) continue;                 // a source row is drawn when the 8-bit sum carries
    uint8 cl = 0;  x = x0;
    for (c = 0; c < 87; c++) {
        if (!carry(cl += z)) continue;
        if (wiz[r][c]) buf[y][x] = wiz[r][c];
        x++;
    }
    y++;
}
```

i.e. WIZ scaled by s = z / 256 (nearest neighbour, floor(87 s) x floor(124 s) pixels) with its top-left corner at
(160 − z/4, 100 − z/4): vertically centred (centre y = 100 − 0.008 z), but **not horizontally** (centre x = 160 −
0.08 z: 140.4 at the largest size, 156 at the smallest). Keep that offset: it is the composition.

Whole frame: the buffer is copied whole; at entry 7's start it is all zero (entries 5/6 do not touch it, entry 4
left it cleared), so records 3194..3196 still show black on the pixel classes not yet written (the screen fills in
over four frames). Afterwards every pixel holds a texel of one layer or a WIZ pixel. Because the buffer is not
cleared and each class is rewritten only every 2 or 4 frames, **WIZ leaves ghosts**: where the shrinking sprite was
last frame, its pixels stay until a layer rewrites them (on average 62.5 % of them remain after 1 frame, 25 % after 2,
12.5 % after 3, as a dither).

Palette: WIZ.RAW's = TEXTURE.RAW's (identical), constant.

Time bases: a, p, z per refresh (retrace routine); the layer interleave cycle per drawn frame (one per refresh in
the capture).

Shortcuts for the remake to evaluate exactly: 160 x 100 texture samples per layer (one per 2 x 2 block) →
per-pixel mapping; nearest texel reads → filtered reads in colour (tiling); the sine table and the even angle index
→ sin/cos of a continuous angle a(n)/2 (interpolate a between refreshes); the pixel interleave of the layers → a
**colour blend** of what the 70 Hz checker averages to. The classes on screen were drawn at different frames: over
two frames, layer 1 drawn 0 and 1 frames ago (1/8 each) and layer 2 drawn 0, 1, 2, 3 frames ago (1/4, 1/4, 1/8,
1/8), so the average is `1/8 (L1(n) + L1(n−1)) + 1/4 (L2(n) + L2(n−1)) + 1/8 (L2(n−2) + L2(n−3))` with each layer at
the angle of its frame (at fast turns the textures trail towards the edges). Against the capture (4 x 4 box-filtered,
record 3500) this averages to a mean absolute difference of 5.1, a single-time 1/4 + 3/4 blend to 11.6; the port
(`reve/remake/src/scenes/wiz.cpp`) uses the former. WIZ's carry-based nearest
downscale → a filtered scaled draw at continuous z (z/4 for the corner); the ghosts and the four-frame fill-in at
the start may be dropped (or the ghosts approximated by the previous three sizes at 62.5 / 25 / 12.5 % opacity).

## 4. Entries 9, 10, 15: the ring twirl (0x204de0 / 0x2053cc, 0x204ef8 / 0x204f6f / 0x2054c4, 0x204fe8 / 0x205578)

A picture turned about the screen centre ring by ring: every ring of radius r = 1..120 is rotated by its own angle,
which grows linearly with r (a twist) and swings back and forth with time, so the picture winds into a spiral and
unwinds. Entry 9: `GFX\PLAYMO.RAW` (a rendered blue toy figure lying on its side, 320 x 200, black background);
entry 10: the same rings, twice the swing; entry 15: `GFX\BOUCHE.RAW` (a grey mouth with a red tongue), with an
extra global spin of up to ±2.4 turns.

### 4.1 Init: the ring lists

```c
bool init9(void)                                   // 0x204de0 (init15 0x2053cc: the same with BOUCHE, below)
{
    memset(0xa0000, 0, 64000);
    load_picture("GFX\\PLAYMO.RAW" /*0x205d7c*/, buf_205e80, 1);  // palette -> DAC
    memcpy(0xa0000, buf_205e80, 64000);            // shown at once (records 6329, 6330)
    memcpy(buf_205e8c, buf_205e80, 64000);
    rings = alloc(0x78000);                        // [0x217588]; freed by done 0x204ee9 (entries 10, 15)
    uint32 *out = rings;
    for (r = 120; r >= 1; r--) {                   // ecx
        ring_ptr[r - 1] = out;                     // dword at [0x205e80] + 0xfffc + 4 r = [0x205e84] + 4 (r - 1)
        uint32 *cnt = out++;  *cnt = 0;
        for (b = 0x2000; b >= 0; b -= 8) {         // byte offset: table step 4096, 4092, ..., 0 (1025 angles)
            int y = -((SIN_b(b) * r) >> 15);       // signed word * r, arithmetic >> 15
            if (y <= -100 || y >= 100) continue;
            int x = (COS_b(b) * r) >> 15;          // COS_b(b) = word at 0x217ec8 + b
            if (x <= -160 || x >= 160) continue;
            int o = (y + 100) * 320 + x + 160;
            uint8 c = buf_205e80[o];
            if (c == 0xff) continue;               // taken (or a picture pixel of index 255: none in PLAYMO/BOUCHE)
            buf_205e80[o] = 0xff;                  // mark: each pixel belongs to the first (largest) ring hitting it
            *out++ = o << 16 | c;  (*cnt)++;
        }
    }
}
```

Notes on the lists:
- Order within a ring: decreasing angle from 360° (screen: starting right of the centre, going clockwise); x =
  floor(r cos φ), y = −floor(r sin φ) put the rings' centre at **(160.0, 101.0)** in VGA coordinates (ring 1 =
  pixels (160,100), (160,101), (159,100)).
- At b = 0x2000 the cosine read runs one word past the table into the square-root table 0x219ec8 (filled at start
  by 0x2175bc; that word is 0x0100 = 256), so x = 0, y = 0: **the centre pixel (160, 100) becomes the first entry of
  ring 120**. It then shows ring 120's colours (mostly black). Entry 15 paints over it (below).
- Row 0 and column 0 are never in a ring (−100 < y, −160 < x); the rings of radius 100..120 lose the angles where
  they leave rows 1..199 (|r sin φ| ≥ ~99.5): their lists cover only the left and right arcs.
- PLAYMO: 41,476 ring pixels (every pixel within ~120.5 of the centre, rows 1..199); no ring is empty; no picture
  pixel outside the rings is non-zero; no picture pixel has index 255. The same holds for BOUCHE.

`init15` (0x2053cc) differs: it clears 0xa0000 and loads `GFX\BOUCHE.RAW` (0x205df7) with its palette but does **not**
copy the picture to the screen or to [0x205e8c] (non-ring pixels of [0x205e8c] keep what entry 14 left: all zero in
the capture), and it ends with `phase = 0x78` (120), `twist = 0x5c` (92).

### 4.2 Retrace routines (per refresh)

```c
// variables: phase [0x217598], dphase [0x21758c], twist [0x2175a0], dtwist [0x217594], B [0x2175b0], dB [0x217590]
// image values: phase 0, dphase 4, twist 0, dtwist 1, B 0, dB 50
void retrace9(void)                                // 0x204ef8: L = 800, S = 4, T = 40
void retrace10(void)                               // 0x204f6f: L = 1000, S = 8, T = 80
{
    phase += dphase;
    if (dphase >= 0) { if (phase >= L) dphase = -S; } else if (phase <= -L) dphase = S;
    twist += dtwist;
    if (dtwist >= 0) { if (twist >= T) dtwist = -1; } else if (twist <= -T) dtwist = 1;
}
void retrace15(void)                               // 0x2054c4: retrace9, then
{
    B += dB;
    if (dB >= 0) { if (B >= 10000) dB = -50; } else if (B <= -10000) dB = 50;
}
```

Note the step's size only changes at a bounce, so a step carried over from the previous entry keeps its size until
then. **The state carries over 9 → 10 → 15** (only init15 resets phase and twist; B and dB are only used by 15):
- entry 9 (n = r − 6331 calls, from the image's values): phase_n = 4 (200 − |((n + 200) mod 800) − 400|),
  twist_n = 40 − |((n + 40) mod 160) − 80| (both checked). After its 445 calls: phase −180 (dphase −4), twist −35
  (dtwist +1).
- entry 10 (n = r − 6776): phase −180 − 4n down to −1000 (n = 205), then +8 per refresh (reaches 944 at n = 448);
  twist −35 + n up to 80, then down to −80, up again (67, falling, at n = 448). Entry 10 got 448 calls in the capture.
- entry 15 (n = r − 10809): phase from 120 with entry 10's **+8** until 800 (n = 85), then ±4 between ±800; twist
  from 92 with entry 10's −1 down to −40 (n = 132), then ±1 between ±40; B = 50 n up to 10,000 (n = 200), then ±50
  between ±10,000.

The remake should replay these integer recurrences per refresh with the capture's call counts (445 for entry 9, 448
for entry 10: measured values) and interpolate between refreshes; the sign of dphase at entry 15's start depends on
entry 10's length (7 more refreshes would flip it).

### 4.3 Frame routine (per drawn frame; its state comes only from the retrace routine)

```c
void frame9(void)                                  // 0x204fe8 (frame15 0x205578: acc starts at B, and the dot)
{
    int acc = 0;                                   // [0x2175a8]; frame15: acc = B
    for (i = 0; i < 120; i++) {                    // ring index i = radius i + 1, innermost first
        uint32 *l = ring_ptr[i];  int n = l[0];
        if (n == 0) continue;                      // (never: no empty rings)
        int shift = ((phase + acc) * n) >> 12;     // 32-bit imul, arithmetic shift: floor
        acc += twist;
        for (k = 0; k < n; k++) {
            int d = (shift + k) mod n;             // mathematical modulo (loops of +n / -n)
            buf_205e8c[l[1 + d] >> 16] = (uint8)l[1 + k];   // colour of entry k goes to entry k + shift
        }
    }
    // frame15 only: *(uint16 *)(buf_205e8c + 0x7da0) = 0x1818;   pixels (160,100), (161,100) = index 0x18
    memcpy(0xa0000, buf_205e8c, 64000);
}
```

So ring i (radius R = i + 1) is turned by `shift / n` of its list, about (phase + B + i · twist) / 4096 of its
walk, clockwise on screen for positive values (the colour at list position k moves to k + shift, the list running
clockwise). Every ring pixel is rewritten every frame; non-ring pixels never change (PLAYMO's are its black
background, entry 15's are zero), so the whole frame is defined. Index 0x18 in BOUCHE's palette is (25, 0, 3), the
dark red of BOUCHE's centre area: the two-pixel dot hides the centre-pixel artefact of the lists.

Palette: PLAYMO.RAW's (entries 9, 10), BOUCHE.RAW's (entry 15); constant.

### 4.4 Size of the motion

Entry 9: the whole picture swings ±800/4096 turn (±70°) with a period of 800 refreshes (11.4 s), and the outer
rim turns up to 119 · 40 / 4096 = 1.16 turns more than the centre (period 160 refreshes, 2.3 s), so the twist winds
and unwinds about 5 times per swing. Entry 10: ±1000/4096 turn (±88°) at twice the speed, twist up to 119 · 80 /
4096 = 2.3 turns. Entry 15: B adds ±10,000/4096 = ±2.44 turns over 800 refreshes.

### 4.5 Continuous form for the remake

The rings are pixel-quantised and each is shifted by whole list entries: neighbouring rings tear against each
other, and the original's picture shows ragged edges along the rings. Evaluate instead, for every output point p at
distance ρ from c = (160, 101) (VGA pixel units, not aspect corrected: the rings are circles in pixel space and
ellipses on the 4:3 screen):

    R = ρ + 0.5 (the ring that owns p),  f = (phase + B + (R − 1) · twist) / 4096   (B = 0 in entries 9, 10)
    ρ ≤ ~99:  source = c + Rot(f) (p − c): angle ψ_src = ψ + 2π f (maths angle, y up), i.e. the picture turned
              clockwise on screen by 2π f;
    ρ > ~99:  the ring only exists on the left and right arcs |sin ψ| < 99.5 / R (half-angle α each); walk them as
              the list does (from ψ = 2π downwards: the right arc's lower half, the left arc, the right arc's upper
              half), length L = 4α, and take the colour from position s − f L (mod L) of that walk: content that
              leaves an arc at the top or bottom of the screen jumps across to the other arc instead of leaving;
    ρ > 120.5 or row 0: the picture as it is (black for PLAYMO and BOUCHE).

`twirl_continuous.py` renders this next to the exact model: the shapes agree (with the arc rule; a plain rotation
for ρ > 99 does not, 1 % vs 18..26 % of pixels equal there), the per-pixel agreement is about 50 % because the
original tears. Read the picture with filtered reads (the redraw), interpolate phase, twist and B between refreshes.
Drop the centre-pixel artefact and entry 15's dot (the continuous form shows the picture's own centre there,
which in BOUCHE is the same colour 0x18).

## 5. Entries 12 and 13: rotozoomer, feedback zoom, dot spiral (0x20562c, 0x20569c, 0x2056f3, 0x20578c)

A dim purple texture smeared outwards from the centre by a zooming feedback blur, with 30 bright pink dots on a
spiral (radius 25 to 97 pixels) turning at changing speeds; the dots leave fading copies of themselves that drift
outwards. The zoom and the spiral's speed follow a key list (one key every 3 s).

### 5.1 Init

```c
void init12(void)                                  // 0x20562c
{
    load_picture("GFX\\TEXTURE.RAW", buf_21b140, 1);            // palette -> DAC (TEXTURE's, for both entries)
    copy 16 x 16 from buf_21b140 + 0x5010 (x 16..31, y 80..95) to tex16 [0x205e90];
    load_picture("GFX\\SPIRALE.RAW" /*0x205e17*/, buf_205e84, 0); // 24 x 8: three dots; only the first is used
    read_file("GFX\\SPIRBLUR.PAL" /*0x205e06*/, 0x20643c, 0x10000);
}
void init13(void) { cnt = 1; ptr = 0x217540; /* returns with CF set; main ignores it */ }   // 0x20569c
```

State (image values; entry 12 is their first user except a, p and layer 1's cycle, which continue from entry 7's
end: a = 975, p = 8960 and the cycle back at its image state after entry 7's 896 calls):

| variable | address | initial | |
|---|---|---|---|
| a, p | 0x2174c8, 0x2174cc | (entry 7's end) | layer 1's angle and its speed's phase |
| q | 0x2174ec | 0 | spiral angle (4096 = a turn) |
| qprev | 0x2174f0 | 0 | q before this refresh's step (the dots use it) |
| Z | 0x2174f4 | 0x331 = 817 | feedback zoom (1024 = 1) |
| spd | 0x2174f8 | 0xa0000 | spiral speed, 16.16 (10.0 = 0.88° per refresh) |
| ptr, cnt | 0x217508, 0x21750c | 0x217518, 1 | key list position, refreshes until the next key |
| inc1, inc2 | 0x217510, 0x217514 | 0, 0 | per-refresh increments of spd and Z |
| keys | 0x217518..0x21756f | 11 x {int32 inc1, int32 inc2} | static data: (−1523, 0), (0, −1), (7047, 0), (−15047, 0), (8571, 1), (−46666, −1), (−28571, 0), (−19047, 2), (94285, 0), (624, −2), (−312, 3) |

### 5.2 Retrace routine (per refresh)

```c
void retrace12(void)                               // 0x2056f3 (entries 12 and 13)
{
    p += 10;  a += SIN[(p & 0x1ffe) / 2] >> 8;     // as entry 7, twice the speed (±5.5° per refresh)
    qprev = q;  q += spd >> 16;                    // arithmetic shift
    if (ptr > 0x217570) return;                    // never true
    if (--cnt == 0) {
        cnt = 0xd2;                                // 210 refreshes (3.0 s) per key
        if (ptr == 0x217570) return;               // after the last key: skip this one refresh's increments
        inc1 = ptr[0];  inc2 = ptr[1];  ptr += 8;
    }
    spd += inc1;  Z += inc2;
}
```

Keys are taken at entry-local calls 1, 211, 421, 631, 841 (entry 12: keys 0..4; entry 13 restarts at key 5:
keys 5..9; key 10 is never reached before entry 13 ends). In the capture: Z 817 → 607 (816 at call 211, then −1 per
refresh) → 608..660 (entry 12's end); entry 13: 659 → 450 → 452..870 (+2 per refresh) → 868..758; spd ranges from
+27.5 to −314 per refresh in q (the spiral spins up to 28° per refresh, which strobes). Replay the integer
recurrences per refresh (exact; `spiral.py`), interpolate within a refresh.

### 5.3 Frame routine (per drawn frame)

```c
void frame12(void)                                 // 0x20578c
{
    layer1(buf_205e8c, a, tex16);                  // 0x25a69c: 1/4 of the pixels get a fresh texel (section 3.3)
    feedback(buf_205e8c, Z);                       // 0x25eaec
    dots(buf_205e8c, qprev);                       // 0x25f038
    memcpy(0xa0000, buf_205e8c, 64000);
}
```

**Feedback** (0x25eaec), in place:

```c
uint32 step = Z << 22;                             // zoom step as a 0.32 fraction: (Z mod 1024) / 1024 = s
uint32 h = step >> 16;                             // = s * 65536
int32 ex = 0x1400000 - 320 * h;                    // 320 (1 - s) * 65536
int imm = (ex >> 17);                              // floor(160 (1 - s)): source column offset
uint32 x0 = (uint32)ex >> 1;                       // 160 (1 - s) as 16.16, used as a 0.32 start fraction (~0)
int32 ey = 0xc80000 - 200 * h;
imm += (ey >> 17) * 320;                           // + floor(100 (1 - s)) rows; stored into the code at 0x25eb56
uint32 y0 = (uint32)ey >> 1;
uint8 *src_row = buf + imm;  uint32 fy = y0;
for (j = 0; j < 200; j++) {
    uint8 *s = src_row;  uint32 fx = x0;
    for (i = 0; i < 320; i += 4) {                 // four source / dest reads, then one dword write
        for (t = 0; t < 4; t++) {
            out[t] = SPIRBLUR[*s << 8 | buf[j*320 + i + t]];  // table[zoomed source][current]
            s += carry(fx += step);
        }
        write out[0..3] to buf[j*320 + i .. +3];
    }
    if (carry(fy += step)) src_row += 320;
}
```

The source for pixel (i, j) is (imm_x + floor((x0 + i·step)/2^32), imm_y + floor((y0 + j·step)/2^32)) ≈ (160 + (i
− 160) s, 100 + (j − 100) s): **a zoom by 1/s about the centre (160, 100)** (s = 0.44..0.85: magnification 1.18..2.3
per frame), so content flows outwards. Because it works in place from the top, rows below the centre (and the right
half of the centre rows) read sources that this pass has already blended; a double-buffered version differs on 9 %
of the pixels after 120 frames, invisibly (`inplace.py`): the remake can double-buffer.

**`GFX\SPIRBLUR.PAL`** (65,536 bytes) = `T[a][b]` = the index of TEXTURE.RAW's palette nearest (squared distance,
first of equals) to `(pal[a] + 3 pal[b]) >> 2` per 6-bit channel: exactly 0x25a275's output (section 8.5), verified
for all 65,536 entries. With a = the zoomed source and b = the current pixel: **new = 3/4 current + 1/4 zoomed**.

**Dots** (0x25f038, 0x25f094):

```c
for (k = 0; k < 30; k++) {
    int b = (qprev + 300 * k) & 0xfff;  int R = 50 + 5 * k;
    int dy = (SIN[b] * R) >> 16, dx = (COS[b] * R) >> 16;      // ≈ R/2 · (sin, cos): radius 25 .. 97 pixels
    draw the top-left 6 x 6 of SPIRALE (stride 24, index 0 transparent) with its top-left corner at
        (160 + dx, 100 + dy);                      // no clipping: rows past 199 land in the buffer's slack
}
```

So the dot centres lie on a spiral about (163, 103), 300/4096 turn apart (2.2 turns for the 30 dots), turning
clockwise on screen as q grows; the dots use q of the previous refresh. The dot (SPIRALE's first 6 x 6) is round,
indices 97 (63,49,49) core, 98 (63,34,34), 103 (41,8,30), 106 (23,5,21) rim, through TEXTURE's palette
(SPIRALE's own palette has the same colours at these indices).

Whole frame: the buffer is copied whole; every pixel is rewritten by the feedback. At entry 12's start [0x205e8c] is
all zero (entry 11 leaves it cleared; the model from zero matches from record 8123).

Time bases: a, p, q, Z, spd, keys per refresh. The texel refresh pattern, the feedback blend and the dots are **per
drawn frame** (frame-rate dependent: how far the smear reaches and how fast echoes fade depend on the frame rate;
the capture draws one frame per refresh).

### 5.4 Remake

Keep the feedback in colour, double-buffered, stepped once per original frame. A single averaged field
(B ← 3/4 B + 1/4 T, then the zoom blend) is **not** enough: the texture rewrites cycle through four pixel classes,
so a pixel is rewritten every fourth frame and a dot's echo leaves it after at most four frames, while a single
field lets echoes decay as (9/16)^k and shows them far too long and bright (mean luminance 35 against the capture's
28). Keep one field per class c = 0..3 and show their mean:

    step g:  M_c = T_g if c = g mod 4 (the class rewritten, T_g = layer 1 at a_g) else B_c;   M = mean of the M_c
             B_c ← 3/4 M_c + 1/4 M(zoom(x)) − bias;   then the dots into every B_c

with zoom(x) = imm + frac + (x − ½) s per axis (the mean source of 0x25eaec's carry steps) and bias = 3/8 / 63 per
channel, the mean loss of the table's truncation `(a + 3b) >> 2` (without it the background and echoes are about 15 %
brighter than the capture's; with it the mean luminance is within 1.5 of the capture at records 8600..9700). The
in-place order is not visible. The fields forget their start fast (the old buffer's weight is 1.2e-3 after 12 steps,
8e-5 after 16): replay the last 14 steps from black (from the entry start when nearer). The quantisation of every
blend to the palette (colours that stick instead of decaying) is an 8-bit artefact to drop. The dots' motion is
stroboscopic (up to 28° per refresh, 26° apart): the port shows the frames as they are, one per refresh. All this was
modelled in Python against the exact model and the capture, then ported (`reve/remake/src/scenes/dots.cpp`).

---------------------------------------------------------------------------------------------------------------

## 6. Pictures

| file | size | used in | shown through | how | redraw? |
|---|---|---|---|---|---|
| `GFX\REVE.RAW` | 227 x 80 | entry 0 (and entries 1..4, 3D scope) | TEXTURE.RAW's palette (= its own) | as it is, top-left (47, 60), index 0 transparent | yes: `dump/pictures/REVE.png` |
| `GFX\PULSE.RAW` | 320 x 200 | entries 5, 6 | its own palette | as it is, full screen; also its colour negative (NEGATIF.PAL) | yes: `PULSE.png` (the remake inverts it in colour) |
| `GFX\WIZ.RAW` | 87 x 124 | entry 7 | its own (= TEXTURE's) | as it is, scaled 0.2..0.96, index 0 transparent | yes: `WIZ.png` |
| `GFX\PLAYMO.RAW` | 320 x 200 | entries 9, 10 | its own | pixels moved by the rings, never recoloured | yes: `PLAYMO.png` (resampled by the twirl) |
| `GFX\BOUCHE.RAW` | 320 x 200 | entry 15 | its own | as PLAYMO | yes: `BOUCHE.png` |
| `GFX\SPIRALE.RAW` (top-left 6 x 6) | 6 x 6 | entries 12, 13 | TEXTURE's | a dot, index 0 transparent, then blended by the feedback | a smooth disc with its four colours is enough; `SPIRALE-dot.png` for reference |
| `GFX\TEXTURE.RAW` 0x4040 (tile 5) | 64 x 64 | entry 7 layer 2 | TEXTURE's | rotozoom texture, tiling | a tileable 4x upscale at most: `TEXTURE-tile5.png` |
| `GFX\TEXTURE.RAW` 0x6010 | 16 x 16 | entry 7 layer 1 | TEXTURE's | rotozoom texture, tiling | as above: `TEXTURE-6010.png` |
| `GFX\TEXTURE.RAW` 0x5010 | 16 x 16 | entries 12, 13 | TEXTURE's | rotozoom texture, then colour-blended | as above: `TEXTURE-5010.png` |

The PNGs (palette as the DAC shows it, v = 8-bit >> 2 → v/63, transparent index as alpha) are written by
`models/parts2d-a/pictures.py` to `reve/re/dump/pictures/` (ignored by Git). None is a photo of a real person.
No picture in this scope is read through palette indices by an effect except through tables the remake replaces by
colour operations (NEGATIF → 1 − c, SPIRBLUR → 3/4 + 1/4 blend), so the redraws can be used everywhere they are drawn.
The file names are strings in the image: 0x205ceb TEXTURE, 0x205cfb REVE, 0x205d46 PULSE, 0x205d54 NEGATIF.PAL,
0x205d64 WIZ, 0x205d7c PLAYMO, 0x205df7 BOUCHE, 0x205e06 SPIRBLUR.PAL, 0x205e17 SPIRALE.

## 7. Buffers used

[0x205e80] picture (REVE, PULSE, WIZ, PLAYMO, BOUCHE; in entries 9/10/15 marked with 0xff by the ring builder);
[0x205e84] SPIRALE (12/13) or the ring pointer table (9/10/15, 120 dwords); [0x205e8c] the frame buffer of entries
0, 7, 9, 10, 12, 13, 15 (copied whole to 0xa0000 each frame; entries 5/6 draw into 0xa0000 directly);
[0x21b140] TEXTURE.RAW (loaded by entries 0 and 12); [0x205e90] 16 x 16 and [0x205e94] 64 x 64 textures; 0x20643c
the 256-byte NEGATIF map (5) / the 64 KB SPIRBLUR table (12, 13); [0x217588] the ring lists (0x78000 bytes).

## 8. Shared helpers

### 8.1 Sprite 0x25f0be

ESI source, BL width, BH height (8-bit), EDX source stride, EDI destination buffer (320 wide): draws with the
top-left corner at (160 − (BL >> 1), 100 − (BH >> 1)), skipping index 0, no clipping. Callers: entries 0..4 (frame
routines 0x20489c, 0x2049c4, 0x204a10, 0x204a5c, 0x204aa4) and the inits 0x205261, 0x2052a0, 0x2052df, 0x205368
(entries 16, 17, 24, 29; other scopes).

### 8.2 Picture loader 0x25a1fb

ESI file name, EDI destination (kept at 0x25a271), EAX = 1 to also set the palette. Loads the file with service
0209h into a new buffer; palette entries = big-endian word at +0xc (256); with EAX = 1 writes 3 x 256 bytes `v >> 2`
to the DAC (from index 0) and to 0x20613c; copies ceil(w · h / 4) dwords of pixels (w, h = big-endian words at +8,
+0xa) from +0x20 + 768; frees the file buffer (0102h). CF set if the file is missing.

### 8.3 The rotozoom code and its patcher (0x25a1bc, 0x25a7dd..0x25eae9)

The unrolled code is static in the image (not generated): four variants of 80 dword blocks (160 texels) each, ending
with `jmp` back to the line loop (0x25a703 for layer 1, 0x25a7a5 for layer 2):

| variant | texel load | texture | coordinates |
|---|---|---|---|
| 0x25a7dd..0x25b8a0 | `mov al` (bytes 0, 2: even columns) | [ebx + 0x12345678] → [0x205e90] | `shr ebx, 28; shld ebx, edx, 4` |
| 0x25b8a0..0x25c963 | `mov ah` (bytes 1, 3: odd columns) | as above | as above |
| 0x25c963..0x25da26 | `mov al` | [ebx + 0x87654321] → [0x205e94] | `shr ebx, 26; shld ebx, edx, 6` |
| 0x25da26..0x25eae9 | `mov ah` | as above | as above |

Each block: `mov eax, [edi]`, then twice (`add ecx, esi; add edx, ebp; mov al/ah, [ebx + tex]; recompute ebx; ror
eax, 16`), `mov [edi + 4m], eax`: the texel is read with the coordinates from before the step, and the two untouched
bytes of the dword keep the buffer's old pixels. 0x25a1bc (called once at start) scans 0x25a7dd..0x25eae9 byte by
byte for the dwords 0x12345678 / 0x87654321 and replaces them by [0x205e90] / [0x205e94] (320 each; checked against
snapshot f3500: nothing else differs). The remake does not need any of this.

### 8.4 Feedback zoom 0x25eaec and dots 0x25f038 / 0x25f094

Section 5.3. 0x25eaec patches its own `add esi, imm32` at 0x25eb54 (immediate at 0x25eb56) every call. 0x25f094 is
the 6 x 6 transparent blit (source [0x205e84], stride 24, destination EDI + EAX).

### 8.5 Blend table 0x25a275 (no call site)

```c
for (a = 0; a < 256; a++) for (b = 0; b < 256; b++) {          // pal = 0x20613c (6-bit)
    target = ((3 * pal[b] + pal[a]) >> 2) per channel;          // 8-bit adds, no overflow (max 252)
    T[a * 256 + b] = first index i minimising Σ (pal[i] - target)^2;   // into 0x20643c
}
```

Nothing calls it or holds its address; SPIRBLUR.PAL is its output for TEXTURE.RAW's palette, precomputed and shipped
as a file. Also unused: 0x2056b4, an earlier version of the retrace routine 0x2056f3 without the key list.

## 9. Open points

- Whether the first frame of an entry whose init takes more than a refresh appears in record S depends on how long
  the frame routine takes (entries 0, 5, 9: yes; 12, 15: no). The remake switches at its cue anyway.
- The continuous twirl (4.5) and the layer blend (3.3) are my reading of what the pixel effects show; both are
  checked visually and by box-filtered differences against the exact models, not pixel for pixel (the originals tear
  and dither).
- The four-class feedback (5.4) replaces a per-pixel-class rewrite by the classes' mean and the zoom's carry steps
  by their mean position; checked by mean luminance and visually against the capture.
- The music positions in the timing table are the capture's; the remake's cues derive from the music and may start
  a few refreshes earlier (inits are instant there).
