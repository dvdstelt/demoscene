# Scope "parts2d-a": intro (name cards with the colour-cycling flower, title pictures), picture warp, voxel tunnel

All addresses are offsets in `unpacked/stars_img.bin` (= near pointers = linear addresses in the emulator). "tick" =
one call of the PIT callback (70.064 Hz). "drawn frame" = one pass of a part's frame loop (every loop waits for a
vertical retrace, 70.086 Hz). "frame" = a record of `full.frames`. Music positions are written order:row in hex and
come from 0x29f80 (AX = order << 8 | row) unless 0x1c7a3 (AL = order, AH = row) is named. Pixel coordinates refer to
the original 320x200 frame (shown at 4:3); pixel (x, y) covers [x, x+1) x [y, y+1).

`S[i]` is the sine table at **0xa740**: 5120 signed dwords (0x5000 bytes, static in the image, never written),
`S[i] = round(65528 * sin(2 pi i / 4096))` (checked: all 5120 entries), 4096 entries per turn, `S[i + 1024]` = cosine.
The parts index it either by entry (`S[i & 0xfff]`) or by byte offset (`[0xa740 + (b & 0x3ffc)]` = `S[(b >> 2) & 0xfff]`).
Shared by most parts; listed here because no scope owns it.

Everything below was checked against the emulator (models in `models/parts2d-a/`, section 6):

| what | model | compared | result |
|---|---|---|---|
| name cards (polar map 0x5c97, draw 0x5ece, palette 0x61b1 + 0x615b) | `flower_model.py` | all 1736 drawn frames of the four cards (frames 1183-4301), screen and the 176 DAC colours | bit-exact, every frame |
| warp curves 0x9989 | `warp_check.py` | 915 frames, against the curve arrays dumped at 0x9a62 | 853 exact; the other 62 are frames where a PIT tick fell inside 0x9989 (section 2.9) |
| warp patch fill 0x9a62 | `warp_model.py`, `warp_check.py` | 915 completed frames (9480-11313): from the dumped curves | 914 bit-exact; the 915th record already shows the cleared screen. From the logged parameters alone: 852 (the 62 race frames differ) |
| tunnel polar map 0x136a, projection table 0x10d6 | `tunnel_model.py` | against snapshot f11498 | equal |
| tunnel frame (wiggle 0x1158 / 0x11ce, voxel columns 0x1261, blit) | `tunnel_model.py` | all 1152 drawn frames (11499-13803) | bit-exact |
| sine table 0xa740 | `common.sintab` against the formula | 5120 entries | exact |

The models take the per-frame values (PIT-driven counters) from hooked runs (`run_hook.py`), so they also prove which
counter feeds which variable; sections 1.7, 2.7 and 3.6 give those counters as functions of time.

---------------------------------------------------------------------------------------------------------------

## 1. Intro 0x5a92 (set-up 0x5b84, free 0x5b8f, PIT callback 0x5b91)

### 1.1 What is on screen, and when

frames 279-5211, 3.98-74.35 s, 00:00-0a:39. Seven pictures fade in and out (files 9, 8, 11, 12, 13, 14, 10), and
between them four name cards: a name (file 6) and a word (file 5) over a "flower" -- eight eye shapes (file 7)
mapped through a petal-shaped polar map, turning around the centre, breathing in and out radially and pulsing in
brightness band by band (the "colour cycling").

| frames | time (s) | music | what (routine) |
|---|---|---|---|
| 279 | 3.98 | 00:00 | 0x5a92: black palette, PIT callback 0x5b91 installed (tick counter n = 0 here, 2 frames before the first music tick) |
| 281-409 / 605-733 | 4.01 / 8.63 | 00:01 / 00:2f | picture 9 "Join us" (0x627e): fade in 128 retraces, hold, fade out at row 0x2f, 128 retraces (ends 01:01) |
| 734-862 / 1053-1181 | 10.47 / 15.02 | 01:01 / 01:2f | picture 8 "Bring the family" (0x627e) |
| 1183-1614 | 16.88-23.03 | 02:01-02:3f | name card 0x542a "BARTi / Atomic", 3 petals (0x5c32; with an odd minute 0x5437 "KARL / Powerful") |
| 1627-1691 / 1963-2027 | 23.21 / 28.01 | 03:01 / 03:31 | picture 11 (woman, 0x6251): fade in 64 retraces, fade out at row 0x31, 64 retraces |
| 2076-2509 | 29.62-35.80 | 04:01-04:3f | name card 0x5437 "KARL / Powerful", 13 petals (0x5c47; odd minute: 0x542a) |
| 2523 / 2858 | 36.00 / 40.78 | 05:01 / 05:31 | picture 12 (0x6251) |
| 2971-3405 | 42.39-48.58 | 06:01-06:3f | name card 0x5444 "GROO / Magic", 3 deep petals (0x5c5c) |
| 3419 / 3754 | 48.78 / 53.56 | 07:01 / 07:31 | picture 13 (0x6251) |
| 3867-4301 | 55.18-61.37 | 08:01-08:3f | name card 0x5451 "RA / Nutritive", 4 deep petals (0x5c63) |
| 4314 / 4636 | 61.55 / 66.15 | 09:01 / 09:2f | picture 14 (green poodle, 0x627e) |
| 4765 / 5083-5211 | 67.99 / 72.53 | 0a:01 / 0a:2f | picture 10 "STARS wonders of the world" (0x627e); then black palette, clear screen, return |

Within a card (first drawn frame = 02:01 etc.): fade in over 64 drawn frames (0.91 s), full until 290 ticks after the
card start (4.14 s, around row 0x2b), fade out over 64 drawn frames, then black (palette 0) until row 0x3f, when
the loop ends. The cards draw one frame per retrace in the capture (70.086 fps; `drawn` + 1 every frame).

### 1.2 main's script inside the intro (0x5a92)

```c
set_palette_black();                       // 0x4a76c
set_pit_callback(0x5b91);                  // 0x4abec
wait_pos_gt(0x000); picture_slow(9);       // wait_pos_gt(p): while (0x29f80() <= p);
wait_pos_gt(0x100); picture_slow(8);
wait_pos_gt(0x200); card_2();              // 0x5c32: card 0x542a if minute even ([0x5426] == 0), else 0x5437
wait_pos_gt(0x300); picture_fast(11);
wait_pos_gt(0x400); card_4();              // 0x5c47: 0x5437 if minute even, else 0x542a
wait_pos_gt(0x500); picture_fast(12);
wait_pos_gt(0x600); card(0x5444);          // 0x5c5c
wait_pos_gt(0x700); picture_fast(13);
wait_pos_gt(0x800); card(0x5451);          // 0x5c63
wait_pos_gt(0x900); picture_slow(14);
wait_pos_gt(0xa00); picture_slow(10);
clear_pit_callback(); set_palette_black(); clear_screen();   // 0x4abfe, 0x4a76c, 0x4a84e

void picture_slow(n) {                     // 0x627e
    show_picture(n);                       // 0x17e50: palette black, copy the 320x200 pixels to the VGA
    fade(+2, 128);                         // 0x17e96: per retrace, level 0, 2, ..., 256 (129 retraces)
    while ((0x29f80() & 0xff) <= 0x2e);    // row > 0x2e
    fade_out(-2, 128);                     // 0x17eba: 256 ... 0, frees the picture
}
void picture_fast(n) {                     // 0x6251: the same with +4 / 64 retraces and row > 0x30
}
```
The fade (core scope) sets DAC = `min(63, (p * level) >> 8)` from the picture's 6-bit palette, `level` 0..256, once
per retrace. Because a picture's fade-out takes 128 retraces (1.83 s), picture 9 and 8 end at row 01 of the next
order; the next step then starts at once.

PIT callback 0x5b91 (every tick, from frame 279):
```c
if (card_active) fade_step();              // [0x5a8e] == 1: 0x4a80b; no picture fade runs then, so it does nothing
card_ticks++;                              // dword [0x5465], reset at each card start
tex_angle += 2;                            // word [0x5410], starts 0 (image); = 2 n, n = ticks since frame 279
```

### 1.3 Data

| what | where | format |
|---|---|---|
| card records | 0x542a, 0x5437, 0x5444, 0x5451 (13 bytes each, static) | word +0 = name image (file 6), word +2 = word image (file 5), byte +4 = shift `cl`, dword +5 = petal step (bytes of the sine table per ray), dword +9 = run list (filled by the set-up) |
| | | 0x542a: name 0 BARTi, word 1 "Atomic", cl 12, step 0x0c (3 petals) |
| | | 0x5437: name 3 KARL, word 3 "Powerful", cl 12, step 0x34 (13 petals) |
| | | 0x5444: name 2 GROO, word 0 "Magic", cl 11, step 0x0c (3 petals) |
| | | 0x5451: name 1 RA, word 2 "Nutritive", cl 11, step 0x10 (4 petals) |
| file 5 (0x512d bytes) | [0x5469] | 0x2d bytes = 15 colours (6-bit RGB) for colours 1..15, then 4 images of 144 x 36 bytes (0x1440): 0 "Magic", 1 "Atomic", 2 "Nutritive", 3 "Powerful"; 0 = transparent, colours 4..12 (a 9-step grey-cyan ramp) and 15 (white) |
| file 6 (0x9430 bytes) | [0x546d] | 0x30 bytes = 16 colours for 0x60..0x6f, then 4 images of 128 x 74 (0x2500): 0 BARTi, 1 RA, 2 GROO, 3 KARL; 0 = transparent, colours 0x61..0x69 (a 9-step blue-grey ramp) |
| file 7 (0x101c0 bytes) | [0x5418]; selector [0x541c] = base file 7 + 0xc0, limit 0x10000 | 0xc0 bytes = 64 colours for 0x10..0x4f, then a 256 x 256 texture (row-major, index row * 256 + column), then 0x100 zero bytes (unused). The texture holds four identical "eye" shapes (concentric ellipses, columns 14..100), one per 64-row band; band j (rows 64 j .. 64 j + 63) uses colours 0x10 + 16 j + 0..5 only. The four palette bands are identical: (27,30,0) olive, (47,34,21) tan, (49,21,11) orange-red, (37,16,15), (32,13,27) purple, (21,9,16) dark purple; entries 6..15 of each band are (0,63,0) and unused. Texel 0 = black |
| palette buffer | 0x5477 (768 bytes, zero in the image) | colours 1..15 from file 5, 0x10..0x4f from file 7, 0x60..0x6f from file 6, all others 0 |
| work palette | 0x5777 (0x210 bytes = colours 0..175) | rebuilt every drawn frame (1.6) |
| fade | byte [0x5a77] level, [0x5a78] step (signed), [0x5a79] steps left | |
| band phases | dwords [0x5a7a], [0x5a7e], [0x5a82], [0x5a86], [0x5a8a] = 0, 0, 0x1000, 0x2000, 0x3000 in the image | byte offsets into the sine table (0x4000 = one turn) |
| radius table | 0x1400..0x53ff (4096 dwords) | a 16 KB zero area of the image used as scratch by several parts |
| scratch screen | [0x545e] (0xfa00 bytes, 0x17c43) + selector [0x5462] | the card's text, used only while the maps are built |
| map | [0x540c] (0x1f400 bytes = 64000 words) | temporary; compressed into the run list [0x5408] |

### 1.4 Set-up (0x5b84 / 0x5bb0, called by main at frame 235, before the music starts)

```c
[0x5426] = minute & 1;                           // EAX from main
load files 5, 6, 7; copy their palettes into 0x5477 (0x6128); DAC 0..255 = 0x5477 (0x613d; the screen is all colour 0)
scratch = alloc_top(0xfa00);                     // 0x60e0
for (card in 0x542a, 0x5437, 0x5444, 0x5451) {   // 0x5e2d
    card.runs = build_map(card);                 // 0x5c97 + 0x62bb (alloc 0x1f400, then shrunk to fit)
}
free scratch;                                    // 0x6101
```

0x5c97, the polar map of one card (pseudo-C of the exact integer code):
```c
u16 map[64000] = { 0xffff, ... };               // 0xffff = not drawn
u8 text[64000] = 0; draw_text(text, card);       // 0x5e63, below
for (i = 0; i < 4096; i++)                       // petal radius, 0x1400 table
    r[i] = (S[(i * card.step / 4) & 0xfff] + 0x10000) >> card.cl;   // cl 12: 0..31, cl 11: 0..63
for (i = 0; i < 4096; i++) {                     // 4096 rays from the centre (160, 100)
    s32 x = 160 << 16, y = 100 << 16;            // 16.16
    u16 code = ((i >> 3) & 0xff) << 8 | (r[i] + 1);   // v = angle (two turns of 256 per circle), u = radius
    for (n = 99 - r[i]; n > 0; n--) {            // steps k = 1 .. 99 - r[i]
        y += S[i];        if ((u32)y >= 200 << 16) break;
        x += S[i + 1024]; if ((u32)x >= 320 << 16) break;
        k = (y >> 16) * 320 + (x >> 16);
        if (text[k] == 0) map[k] = code;         // a later ray overwrites an earlier one
        code++;                                  // u = r[i] + k
    }
}
// 0x62bb: run list = { dword count, dword skip, count words } ..., count 0 = end; skip = pixels from the end of the
// previous run (map[0] is never examined). An equivalent sparse copy of map.

void draw_text(u8 *dst, card) {                  // 0x5e63 (also used on the VGA by 0x5e7a)
    blit_transparent(dst + 150 * 320 + 88, file5_image[card.word], 144, 36);  // 0x5dbb: word at (88, 150)
    blit_transparent(dst +  60 * 320 + 96, file6_image[card.name], 128, 74);  // 0x5df4: name at (96, 60)
}
```
So a pixel at distance k (1..99 - r) from the centre along ray i shows texel u = r(theta) + k, v = theta * 512 / 2pi
(mod 256): the eye pattern is anchored to the petal outline (u = 99 at the edge); the flower is the region
rho <= 99 - r(theta), with r(theta) = A (1 + sin(m theta)), m = step / 4 = 3, 13, 3, 4 petals, A = 2^(16 - cl) = 16
(cards 1, 2: depth 32) or 32 (cards 3, 4: depth 64); the text is cut out of it.

### 1.5 The card loop (0x5c68, 0x5e7a, 0x5ece)

```c
void card(rec) {                                 // 0x5c68
    card_active = 1;                             // [0x5a8e]
    // 0x5e7a: start
    cur = rec; fade_level = 0; fade_step = +2; fade_left = 64;
    work_palette(); output_palette();            // 0x61b1 + 0x615b: level 0 (black), level -> 2
    clear VGA; draw_text(VGA, rec);              // the text is drawn once; the flower never touches it
    card_ticks = 0;
    do {
        wait_retrace();                          // 0x4a830
        work_palette(); output_palette();        // 1.6
        // 0x5ed8: draw the flower straight into the VGA (run list of the card)
        si = (tex_angle & 0xff) << 8 | (u_off & 0xff);        // u_off = word [0x5412], from the previous frame
        for (each map pixel k with map[k] != 0xffff) VGA[k] = tex7[(map[k] + si) & 0xffff];
        u_off = (S[(tex_angle * 32) & 0xfff] + 0x10000) >> 12;   // 0..31, used by the next frame (0x5f5c)
        if (fade_step >= 0 && card_ticks >= 290) { fade_left = 64; fade_step = -2; }   // 0x5f80
    } while ((0x29f80() & 0xff) <= 0x3e);        // until row 0x3f
    card_active = 0;
}
```
With u + u_off <= 99 + 31 the low byte never carries into v. v wraps at 256.

### 1.6 Palette ("colour cycling"): 0x61b1 and 0x615b, once per drawn frame

```c
void work_palette(void) {                        // 0x61b1
    for (j = 0; j < 5; j++) ph[j] += 0x190;      // ph = [0x5a7a], [0x5a7e], [0x5a82], [0x5a86], [0x5a8a]
    W[0..15]   = P[0..15];                       // the words (file 5), unchanged
    W[16..31]  = P[16..31] * band(ph[1]) >> 8;   // eye band 0 (texture rows 0..63)
    W[32..47]  = P[32..47] * band(ph[2]) >> 8;   // band 1 (rows 64..127), +90 degrees
    W[48..63]  = P[48..63] * band(ph[3]) >> 8;   // band 2, +180
    W[64..79]  = P[64..79] * band(ph[4]) >> 8;   // band 3, +270
    W[80..95]  = P[80..95];
    W[96..111] = P[96..111] * band(ph[0]) >> 8;  // the names (file 6), same phase as band 0
}                                                // (per 6-bit component; W[112..175] are never rebuilt)
u8 band(u32 ph) { return (S[(ph & 0x3ffc) >> 2] + 0x30000) >> 10; }   // 128..255 = (3 + sin) / 4 * 256

void output_palette(void) {                      // 0x615b
    bl = fade_level;
    if (fade_left) { fade_level += fade_step; fade_left--; }
    for (b = 0; b < 0x210; b++) W[b] = (u8)((W[b] * bl) >> 7);   // in place; colours 112..175 stay 0
    DAC[0..175] = W;
}
```
Displayed colour c (6-bit) of an eye texel of band j: `((P[c] * band(ph[j+1]) >> 8) * level) >> 7`, level 0..128.
In true colour: `P / 63 * (3 + sin(phi_j)) / 4 * L`, L = level / 128, phi_j = 2 pi ph / 16384.
- The bands pulse between 1/2 and 1 brightness, 90 degrees apart, period 0x4000 / 0x190 = 40.96 drawn frames
  (0.584 s at 70.086 fps). Each band covers two opposite eyes (v repeats twice per turn), so a brightness wave runs
  around the flower. The name pulses with band 0; the word does not pulse.
- The phases are never reset: they advance only while a card draws (0x5e7a once, then once per frame). In the
  capture the first visible frame of the cards has ph[0] = 0x190 x 2, 435, 870, 1306.
- The level: 0 at the start, +2 per drawn frame to 128 (64 frames), -2 per frame from the first frame after 290 ticks,
  then 0.

### 1.7 Time bases (intro)

| variable | time base | formula |
|---|---|---|
| texture angle `tex_angle` [0x5410] | PIT | 2 n, n = ticks since 0x5a92 installed the callback (frame 279, about 0.02 s before the first music tick); read when the frame is drawn |
| radial offset `u_off` [0x5412] | PIT, one drawn frame late | `(S[(2 n' * 32) & 0xfff] + 0x10000) >> 12` = 16 (1 + sin(2 pi n' / 64)), n' = n at the previous drawn frame; period 64 ticks (0.913 s) |
| fade-out start | PIT | first drawn frame with card_ticks >= 290 (ticks since 0x5e7a) |
| card fade level | per drawn frame | +-2 per frame (64 frames each way) |
| band phases | per drawn frame | +0x190 per frame |
| card start / end, picture fades start | music | 0x29f80 > order:00, row > 0x2e / 0x30 / 0x3e |
| picture fades | per retrace | 0x17e96 (core) |

On screen: the texture turns by -2 v per tick = one turn (512 v) in 256 ticks = 3.65 s, counter-clockwise on the
screen (a texel moves towards smaller theta; theta grows clockwise because y points down). The eye rings move in and
out by 0..31 texels with a 0.913 s period. Everything per drawn frame ran at 70.086 Hz in the capture (the card
loop needs well under one frame at 40 M instructions per second), so a remake can step it with the retrace clock.

### 1.8 For the remake

- Evaluate the polar map per pixel instead of 4096 integer rays: for the pixel centre (dx, dy) = (x + 0.5 - 160,
  y + 0.5 - 100) in original pixels (the 4:3 stretch makes the flower 1.2 times taller than wide, as in the
  original), rho = |(dx, dy)|, theta = atan2(dy, dx) in [0, 2 pi), r = A (1 + sin(m theta)); draw where
  rho <= 99 - r (anti-aliased edge), texel u = r + rho + u_off(t), v = 256 theta / pi + 2 n(t) (mod 256), with
  u_off(t) = 16 (1 + sin(2 pi n(t) / 64)) continuous (the one-frame lag of u_off is 14 ms: drop it). Integer r
  (>> cl truncation), integer radius steps, the ray quantisation and nearest texel reads are 320x200 shortcuts.
- The eye texture (file 7) is hand-pixelled flat-colour art; a redraw can replace it: one eye shape, sampled with a
  filter, and multiplied by the brightness of its band, band = floor(v / 64) (the four copies differ only in their
  palette band). Texel 0 is drawn black over the black background (no visible difference with transparency).
- Files 5 and 6 (lettering) are hand-drawn; a redraw can replace them, drawn at (88, 150) and (96, 60) over the
  flower (the original cuts the text pixels out of the flower, i.e. the text is opaque); the name is multiplied by
  the band-0 brightness, everything by the fade L.
- Brightness: replace the 6-bit integer products with `P / 63 * (3 + sin phi) / 4 * L`; L = min(1, frames / 64)
  rising, then falling, 2 / 128 per retrace (70.086 Hz).
- Pictures 8..14 are hand-drawn pictures shown as they are with a palette fade: true-colour redraws can replace them,
  multiplied by level / 256 (the core's fade formula).

### 1.9 Not used

0x5c20 (draws the "next" card from [0x5422]), 0x5db0 (frees [0x540c]), 0x6052 / 0x6075 (show a picture from [0x5471] + 0xc0 and fade in
0x30-byte palette groups from colour 0xb0 over 129 retraces), [0x5422] (set to 0x542a, never read). No callers.

---------------------------------------------------------------------------------------------------------------

## 2. Picture warp 0x975b (set-up 0x9d17, free 0x9dcf, PIT callback 0x9932)

### 2.1 What is on screen, and when

frames 9478-11312, 135.23-161.40 s, 27:01-33:23, 35.04 fps (one frame per two retraces). The three women pictures
(files 11, 12, 13) as a wrapping 256 x 256 texture, mapped onto the screen through a patch whose four edges are the
four quarters of a closed wavy curve r(theta) (a "star" or blob around the texture point (160, 100)). Five phases,
each fading in over 32 ticks and out over its last 32 ticks:

| phase | block | first drawn frame / time / music | ticks | look |
|---|---|---|---|---|
| 1 | 0x9488 | 9479, 135.25 s, 27:01 | 261 | negative radius: a part of picture 11 about 4.5 times magnified, rippling with 16 lobes |
| 2 | 0x94b4 | 9741, 138.99 s, 28:28 | 391 | picture 11 large, turning, with 4 lobes whose depth oscillates (per drawn frame) |
| 3 | 0x9564 | 10133, 144.58 s, 2b:1a | 391 | radius 251..573 texels: many small wrapped copies; pictures 11 -> 12 -> 13 change every 65.5 drawn frames |
| 4 | 0x95e8 | 10525, 150.17 s, 2e:0d | 391 | 8 lobes, fast ripple, picture cycling |
| 5 | 0x9538 | 10922, 155.84 s, 31:01 | 391 | an off-centre circle (1 lobe) whose radius oscillates 84 +- 64..256, picture cycling |

A phase starts when the previous one has ended *and* the music is past its position (0x2800, 0x2b00, 0x2e00,
0x3100): in the capture phases 2-4 start at the end of the previous one (the music position was already passed)
and phase 5 waits for 31:01 (7 frames of black). After phase 5 the part clears the screen and returns (33:23);
main waits for 0x3500.

### 2.2 Set-up 0x9d17 (frames 9476-9477)

```c
set_palette_black();
pal_src = alloc(0x300); pal_dst = alloc(0x300);            // [0x4a798], [0x4a79c]: the core fade's palettes
alloc_buffers(0x973c);   // 0x4a894: three 0x10000 blocks [0x9722], [0x9728], [0x972e], each with a data selector at +4
tex[0] = make_texture(load(11));
tex[1] = make_texture(load(12));
f = load(13); memcpy(pal_src, f, 0x300);                     // the palette of 13 (11, 12 and 13 have the same palette)
tex[2] = make_texture(f);

void make_texture(u8 *file) {                                // 0x9d95, frees the file afterwards
    for (k = 0; k < 256; k++)                                // row k = picture row floor(k * 0xc800 / 0x10000)
        memcpy(tex + 256 k, file + 0x300 + 320 * ((k * 0xc800) >> 16) + 32, 256);   // = floor(25 k / 32)
}
```
Texel (u, v) = picture pixel (32 + u, floor(25 v / 32)): the middle 256 columns, the 200 rows stretched to 256.

### 2.3 Parameter blocks (static, 0x9488 + 0x2c k, 11 dwords each, copied to 0x9458 by 0x982c)

| offset | 0x9458+ | meaning |
|---|---|---|
| 0 | u0 | texture u offset, 8.8 (texels * 256), added to every pixel |
| 4 | v0 | v offset, 8.8 |
| 8 | base | curve radius (texels) |
| 12 | d1 | ph1 += d1 per tick |
| 16 | d2 | ph2 += d2 per tick |
| 20 | shift | low byte patched into `shl ebp, imm8` at 0x9a0f (immediate 0x9a11); the CPU uses it mod 32 |
| 24 | amp | radius wobble amplitude (texels) |
| 28 | ph1 | curve rotation (angle units, 4096 per turn) |
| 32 | ph2 | wobble phase |
| 36 | du | u0 += du per tick (0 in every block) |
| 40 | dv | v0 += dv per tick (0 in every block) |

| block | u0 | v0 | base | d1 | d2 | shift (mod 32) | amp | ph1 | used in |
|---|---|---|---|---|---|---|---|---|---|
| 0x9488 | 0xd740 (215.25) | 0 | -36 | -11 | 16 | 4 | 6 | 2560 | phase 1 |
| 0x94b4 | 0xd600 (214.0) | 0 | 84 | 32 | -148 | 2 | 64 (oscillator: 0..64) | 2560 | phase 2 |
| 0x9564 | 0xd600 | 0 | 412 | 1 | 12 | 34 (2) | -161 | 2560 | phase 3 |
| 0x95e8 | 0xd600 | 0x2380 (35.5) | 108 | -22 | 128 | 163 (3) | -44 | 2560 | phase 4 |
| 0x9538 | 0xd600 | 0 | 84 | 0 | 16 | 192 (0) | 256 (oscillator: 64..256) | 2560 | phase 5 |

ph2 = 0 and du = dv = 0 in all of them. Blocks 0x94e0, 0x950c, 0x9590, 0x95bc are not used.

### 2.4 The part (0x975b, 0x982c)

```c
set_pit_callback(0x9932);
run_block(0x9488, 0x104);
wait_pos_gt(0x2800);
osc_on(&amp, 0x00, 0x40, 0x64);          // 0x98b6: half = (hi - lo) / 2 = 0x20, centre = lo + half = 0x20, speed 0x64
run_block(0x94b4, 0x186);
wait_pos_gt(0x2b00);
cycling = 1;                               // word [0x9738]
osc_off();                                 // 0x98de
run_block(0x9564, 0x186);
wait_pos_gt(0x2e00);
osc_off();
run_block(0x95e8, 0x186);
wait_pos_gt(0x3100);
osc_on(&amp, 0x40, 0x100, 0x64);         // half 0x60, centre 0xa0
run_block(0x9538, 0x186);
clear_pit_callback(); clear_screen();

void run_block(block, count) {             // 0x982c
    left = count;                          // [0x9484]
    pic = 0; cyc = 0;                      // [0x9734], word [0x973a]
    memcpy(0x9458, block, 0x2c);
    fade_level = 0; fade_left = 32; fade_step = 8;          // core fade, stepped by the PIT callback
    do {
        apply_fade_and_wait_retrace();     // 0x4a7b0: DAC = min(63, pal_src * level >> 8) if it changed; retrace
        if (osc_enabled) {                 // 0x98e9
            osc += osc_speed;              // dword [0x9618], never reset
            *osc_target = (S[osc & 0xfff] * half >> 16) + centre;
        }
        build_curves();                    // 0x9989
        draw_patch(tex[pic]);              // 0x9a62, straight into the VGA
        if (cycling) {                     // word add with carry
            cyc += 1000; if (cyc >= 0x10000) { cyc -= 0x10000; if (++pic == 3) pic = 0; }
        }
    } while (left >= 0);
}

void pit_9932(void) {
    fade_step_tick();                      // 0x4a80b: if (fade_left) { level += step; fade_left--; }
    ph1 += d1; ph2 += d2; u0 += du; v0 += dv;
    if (--left == 32) { fade_left = 32; fade_step = -8; }
}
```
A block lasts count + 1 ticks (261 / 391); level = 8 m for the first 32 ticks m = 1..32, 256, then 256 - 8 (m -
(count - 32)) down to 0 at m = count; the loop ends at m = count + 1.

### 2.5 The curves (0x9989, 0x9a02)

Four arrays of (x, y) in 16.16 texture coordinates: top[320] at 0x6990, bottom[320] at 0x7390, left[200] at
0x7d90, right[200] at 0x83d0 (0x6990..0x9457 is a zero area of the image used as scratch).
```c
void curve(pt *out, int n, int start, u32 step) {          // 0x9a02
    u32 e = (ph1 + start) << 20;                            // angle in the top 12 bits
    for (j = 0; j < n; j++) {
        a = e >> 20; e += step;
        w = ((a << (shift & 31)) + ph2) & 0xfff;
        r = ((s32)(S[w] * amp) >> 16) + base;               // texels (integer)
        out[j].x = S[a + 1024] * r + (160 << 16);
        out[j].y = S[a] * r + (100 << 16);
    }
}
curve(top,    320, 0,      0x333333);    // a = ph1 + 3.2 j          (0 .. 1/4 turn)
curve(bottom, 320, 0xc00,  0xffcccccd);  // a = ph1 + 3072 - 3.2 j   (3/4 .. 1/2 turn, left to right)
curve(right,  200, 0x400,  0x51eb85);    // a = ph1 + 1024 + 5.12 j  (1/4 .. 1/2)
curve(left,   200, 0,      0xffae147b);  // a = ph1 - 5.12 j         (1 .. 3/4, top to bottom)
```
Going round the screen border clockwise (top, right, bottom right-to-left, left bottom-to-top) runs once round the
curve C(theta) = (160, 100) + R(theta) (cos theta, sin theta), R(theta) = base + amp sin(2^shift theta +
2 pi ph2 / 4096), theta = 2 pi a / 4096; the top-left corner is at a = ph1 (2560 = 225 degrees at each block's
start).

### 2.6 The patch (0x9a62)

The texture point of screen pixel (x, y) is a Coons-like blend of the four edges, in packed 8.8 fixed point:
```c
TL = (top[0] + left[0]) / 2;     TR = (top[319] + right[0]) / 2;        // per coordinate, sar 1
BL = (bottom[0] + left[199]) / 2; BR = (bottom[319] + right[199]) / 2;
// packed: high word = u (texture column, 8.8), low word = v (row, 8.8)
pack(x, y)   = (x << 8) & 0xffff0000 | ((u32)y >> 8) & 0xffff;
pstep(dx, dy) = { p = (idiv(dx, 200) << 8) & 0xffff0000 | ((u32)idiv(dy, 200) >> 8) & 0xffff;
                  if (p & 0x8000) p += 0x10000; return p; }             // idiv truncates towards 0
for (x = 0; x < 320; x++) { col[x] = pack(top[x]); colstep[x] = pstep(bottom[x] - top[x]); }   // 0x8a30, 0x8f30
Lp = TL; dL = (BL - TL) / 200;  Rp = TR; dR = (BR - TR) / 200;            // per coordinate, idiv
for (y = 0; y < 200; y++) {
    Dl = left[y] - Lp;  Dr = right[y] - Rp;
    e0 = pack(Dl) + (u0 << 16 | v0 & 0xffff);
    h  = pstep(Dr - Dl);                         // NB: divided by 200, not 320
    for (x = 0; x < 320; x++) {
        col[x] += colstep[x];                    // row y uses top + (y + 1) / 200 (bottom - top)
        e = col[x] + e0 + x * h;                 // all 32-bit wrapping adds
        VGA[y * 320 + x] = tex[((e >> 8) & 0xff) * 256 + (e >> 24)];   // row = v integer, column = u integer
    }
    Lp += dL; Rp += dR;
}
```
In real numbers (what the remake should evaluate, per pixel, with filtered texture reads):
```
P(x, y) = T(x) + (y + 1) / 200 (B(x) - T(x)) + Dl(y) + x / 200 (Dr(y) - Dl(y)) + (u0, v0) / 256      (mod 256)
Dl(y) = L(y) - (TL + y / 200 (BL - TL)),  Dr(y) = R(y) - (TR + y / 200 (BR - TR))
```
with T, B, L, R the curves at the pixel's position along each edge (continuous angle, see 2.5). The `x / 200` (not
x / 319) is in the original: at the right edge the left/right correction is extrapolated 1.6 times; it is part of
the look (keep it). The packing costs precision: steps truncated to 1/256 texel, and a carry from v into u (plus the
`+0x10000` for negative v steps) adds up to 1/128 texel per step to u; against the float formula the integer
result is off by up to 2.5-3.6 texels in u and 1.9 in v (mean 0.5-0.8; `warp_float.py`), so 70-80 % of the pixels
read a neighbouring texel. That is smooth and not content: evaluate in floats.

### 2.7 Time bases (warp)

| variable | time base | formula |
|---|---|---|
| ph1, ph2 | PIT | block value + d1 m, + d2 m, m = ticks since the block's copy (they restart at each block) |
| fade level | PIT | 8 m up to 256; from tick count - 31 down by 8 per tick to 0; applied to the DAC once per drawn frame |
| block length | PIT | count + 1 ticks |
| block start | PIT and music | the previous block's end, but not before 0x2801 / 0x2b01 / 0x2e01 / 0x3101 |
| amp (phases 2, 5) | **per drawn frame** | osc = 0x64 j (j = drawn frames of phase 2, then 196 + j in phase 5 in the capture); amp = centre + (S[osc & 0xfff] * half >> 16): period 40.96 drawn frames |
| picture (phases 3-5) | **per drawn frame** | the j-th drawn frame of a block (j = 0, 1, ...) shows picture floor(1000 j / 65536) mod 3 (0 = file 11): a change every 65.5 frames (1.87 s at 35 fps) |

The capture draws exactly one frame per two retraces (35.04 fps, `drawn` advances 196 in 392 frames): a remake should
step the oscillator and the picture change at 35.04 Hz.

### 2.8 Art

Pictures 11, 12, 13 are hand-drawn (painted women on magenta stars, black background). They are used as warped,
wrapping textures: a true-colour redraw can replace them if the remake samples the redrawn picture at
(32 + u, v * 200 / 256) with u, v wrapping at 256 (the left and right parts of the picture, columns 0..31 and 288..319,
are never shown; the wrap seams in u at columns 32 / 287 and in v at rows 0 / 199 are in the original too, mostly on
black). The palette fade becomes a brightness multiplier level / 256.

### 2.9 Quirks and shortcuts

- **Tearing**: the patch is drawn straight into the VGA memory and takes about 1.5 frames, so the beam shows half old,
  half new images (visible in `full.frames`, e.g. frame 10980). Drop it: render whole frames.
- **Race**: ph1 / ph2 are read inside 0x9989 while the PIT may change them; in 62 of 915 frames a tick fell inside
  the curve computation and the curves mix two phase values (verified: the curve model with the phase changing at the
  point where the dump starts to differ matches). Not visible; drop.
- Integer curve angles (a = e >> 20), integer radii (>> 16), 8.8 packing, nearest-neighbour texels of a nearest-
  resampled (200 -> 256 rows) copy: evaluate continuously, read the picture with a filter.
- `shift` is the raw block byte; the CPU uses it mod 32 (34 -> 2, 163 -> 3, 192 -> 0). The remake must use the
  same values.

### 2.10 Not used

The blocks 0x94e0, 0x950c, 0x9590, 0x95bc; and a table at 0x9630..0x971d, 18 entries of 13 bytes ended by -1 {word scan code
(| 0x100 = add, else set), byte 1, dword address, dword value, word size (2 = dword, 0 = byte)}: arrows (left /
right: du +0x40 / -0x40, up / down: dv +0x40 / -0x40), Q/A (base +-8), W/S (d1 +-1), C (d1 = 0), E/D (d2 +-4), R/F
(shift +-1), T/G (amp +-1), F1-F3 (picture = 0, 1, 2): the key bindings of the tool the blocks were tuned with. Nothing reads it (the keyboard is masked during the demo).

---------------------------------------------------------------------------------------------------------------

## 3. Tunnel 0x11e8 (set-up 0x1064, free 0x110d, PIT callback 0xf85)

### 3.1 What is on screen, and when

frames 11498-13803, 164.05-196.96 s, 35:01-45:00, 35.04 fps. A tunnel whose wall is a voxel landscape (height map
file 2, colour map file 3, palette file 4) wrapped round the inside of a cylinder: the camera sits on the axis and
flies forward; mountains (low height values) reach towards the axis, water (255) is the outermost wall; black hole
in the centre where the view ends. From 3b:00 the tunnel twists (the map rows shift with depth), from 3d:00 the twist
travels.

| frames | time (s) | music | event |
|---|---|---|---|
| 11498 | 164.06 | 35:01 | start; fade in by 1 per tick (256 ticks, 3.65 s); forward speed ramps up per drawn frame for 257 ticks |
| 12363 | 176.40 | 3b:00 | twist frequency f: +1 every 4 ticks, 0 -> 20 (tick 864..940) |
| 12651 | 180.51 | 3d:00 | twist speed: +1 every 4 ticks, 0 -> 30 (tick 1152..1268) |
| 13517 | 192.86 | 43:00 | fade out by 1 per tick (256 ticks; black from frame 13773) |
| 13804 | 196.96 | 45:00 | loop ends (order > 0x44), clear screen |

### 3.2 Set-up (0x1064, frames 11313-11497)

```c
alloc_buffers(0xf4e);          // 0x4a894: [0xcc0] 0x10000 + selector [0xcc4] (projection table),
                               //          [0xca6] 0x10000 + selector [0xcaa] (256 x 256 column buffer),
                               //          [0xca0] 0x1f400 (polar map, 64000 words)
set_palette_black();
hmap = load(2) [0xcac], selector [0xcb4];  cmap = load(3) [0xcb0], selector [0xcb6];   // 256 x 256 each
pal_dst = alloc(0x300) [0x4a79c];  pal_src = load(4) [0x4a798];                       // the fade's palettes
build_polar_map();             // 0x136a
for (R = 0; R < 211; R++)      // projection table [0xcc0], rows of 256 bytes (0x10d6)
    for (h = 0; h < 256; h++)
        proj[R][h] = 190 - min(189, (756 + 14 * h) / (R + 9));    // integer division (16-bit quotient compared)

void build_polar_map(void) {   // 0x136a; word = angle << 8 | radius code, 0 = free
    memset(map, 0, 128000);
    for (i = 0; i < 4096; i++) {
        s32 x = 160 << 16, y = 100 << 16;  int code = 190;  u8 a = i >> 4;
        for (;;) {
            k = (y >> 16) * 320 + (x >> 16);
            if (map[k] == 0) map[k] = a << 8 | code;               // the first ray wins
            x -= S[i + 1024]; if ((u32)x >= 320 << 16) break;     // NB: x goes left for angle 0
            y += S[i];        if ((u32)y >= 200 << 16) break;
            if (--code < 0) break;                                // 191 points, code 190 .. 0
        }
    }
}
```
Screen pixel at distance K (ray steps) and angle a = theta * 256 / 2pi, theta = atan2(y - 100, 160 - x), reads
buffer[a][190 - K].

### 3.3 PIT callback 0xf85 (every tick)

```c
fade_step_tick();                                   // 0x4a80b
phase += phase_speed;                               // dword [0xec6] += [0xeca]
col_pos += col_speed;                               // word [0xcba] += word [0xcbe]  (8.8, wraps at 65536)
row_pos += row_speed;                               // word [0xcb8] += word [0xcbc]  (row_speed is always 0)
ticks++;                                            // [0xf6d], 0 at the part start
(order, row) = 0x1c7a3();
if (fade_step != -1 && order >= 0x43) { fade_step = -1; fade_left = 0x100; }    // fade out once
if (order == 0x3b && row == 0 && freq_left == 0)  { freq_left = 20;  freq_inc = 1; }     // [0xeda], [0xede]
if (order == 0x3d && row == 0 && pspd_left == 0)  { pspd_left = 30;  pspd_inc = 1; }     // [0xece], [0xed2]
if ((ticks & 3) == 0) {
    if (pspd_left) { pspd_left--; phase_speed += pspd_inc; }
    if (freq_left) { freq_left--; freq += freq_inc; }                                    // [0xed6]
}
```
Initial values (image): col_speed = 0x40, everything else 0.

### 3.4 The frame loop (0x11e8)

```c
set_pit_callback(0xf85);
fade_level = 0; fade_step = 1; fade_left = 0x100; ticks = 0;
do {
    apply_fade_and_wait_retrace();                 // 0x4a7b0
    run_script(ticks, 0xf71);                      // 0x17ed0, below: col_speed++ while ticks <= 256
    wiggle_table();                                // 0x1158 + 0x11ce
    render();                                      // 0x1261
} while (0x1c7a3().order <= 0x44);
clear_pit_callback(); clear_screen();

// 0x17ed0: time-window script. Entries of 16 bytes {u32 first, u32 last, func, u32 arg}, ended by first = -1;
// calls func(EAX = arg) for every entry with first <= t <= last. Only the tunnel uses it; its script at 0xf71 is
// one entry {0, 0x100, 0x1251, 1}: 0x1251 adds AX to word [0xcbe] (col_speed). 0x1259 (adds to row_speed) is unused.

void wiggle_table(void) {                          // 0x1158, 0x11ce
    e = phase;
    for (b = 0; b < 256; b++) { e &= 0xfff; T[b] = ((S[e] >> 10) << 8) + b; e += freq; }  // u16, [0xcc6]
    for (b = 255; b >= 1; b--) T[b] -= T[b - 1];                                      // differences D[b]
}
// W(b) = S[(phase + b freq) & 0xfff] >> 10 = floor(63.99 sin): -64..63 map rows

void render(void) {                                // 0x1261: 256 voxel columns, one per angle
    for (a = 0; a < 256; a++) memset(&buf[a][169], 0, 24);       // clear radius codes 169..192
    for (a = 0; a < 256; a++) {
        u16 p = (((row_pos >> 8) + a) & 0xff) << 8 | (col_pos >> 8);  // map address: row << 8 | column
        u8 ch = 0, *o = buf[a];
        for (s = 0; s < 105; s++) {
            cl = proj[2 * s][hmap[p]];             // fs:[bh = 2s, bl = height]
            if (cl > ch) { memset(o, cmap[p], cl - ch); o += cl - ch; ch = cl; }
            p += D[s];                             // 16-bit: step s + 1 reads at start + T[s]
        }
    }
    for (k = 0; k < 64000; k++) VGA[k] = buf[map[k] >> 8][map[k] & 0xff];   // 0x1320, 4 pixels per dword
}
```
Every row is filled at least to 171 (the last step gives cl >= 171), so 169..192 hold black where the view ends
and nothing stale is shown; codes 193..255 are never read.

### 3.5 The same in continuous form (for the remake)

Step s (0..104) is depth z_s = 2 s + 9. It reads the maps at
```
column X_s = c0 + max(0, s - 1),   row Y_s = r0 + a + W(s - 1)  (W(-1) = 0),   c0 = col_pos / 256, r0 = row_pos / 256 = 0
```
(map coordinates wrap at 256; in the original only the integer parts of col_pos / row_pos are used, and a column
wrap carries one row: past the depth where X_s passes 255 the rows are shifted by +1, i.e. 1/256 turn -- an
addressing artefact, drop it). Its projected screen radius is
```
k_s = min(189, 14 (54 + h(X_s, Y_s)) / z_s)        (756 = 14 * 54; original: integer division)
```
and the pixel at radius rho (original pixels from (160, 100); the 4:3 display makes the tunnel 1.2 times taller
than wide, as in the original) and angle a shows the colour cmap(X_s, Y_s) of the **first** step with k_s < rho
(original: K > k_s with integer K), black if there is none (only within 3..19 pixels of the centre).
Geometrically: a camera on the axis of a cylinder whose wall lies at a distance proportional to 54 + h from the
axis, one map column per 2 depth units, one map row per 1/256 turn; the twist W shifts the rows with depth.

For high resolution: one column per output angle (e.g. 2048), continuous depth (z from 9 to 217, X = c0 + max(0,
(z - 11) / 2)), bilinear height and colour reads (colour through palette file 4 into RGB first), a continuous W,
continuous k and an anti-aliased boundary between steps; the polar map evaluated per pixel. The 256 angles, 105
steps, integer radii, whole-texel forward motion (only col_pos >> 8 is used: the landscape jumps 1.5 texels per drawn
frame) and nearest reads are the original's shortcuts.

### 3.6 Time bases (tunnel)

| variable | time base | formula (n = ticks since the part start, frame 11498) |
|---|---|---|
| col_pos (forward) | PIT, speed **per drawn frame** | col_pos(n) = sum_{k=1..n} speed(k) mod 65536 (8.8 texels); speed rises by 1 at every drawn frame while ticks <= 256 (script), from 0x40. In the capture (35.04 fps): speed(k) = min(193, 64 + ceil(k / 2)), final 193 / 256 = 0.754 texel per tick (52.8 texels/s); this sum matches the logged col_pos (e.g. 32896 at n = 256, 19168 at n = 864). A machine drawing 70 fps would end at 321 |
| row_pos | PIT | 0 (row_speed 0) |
| freq (twist frequency) | PIT + music | 0; from the first tick at 3b:00, +1 at every tick with n % 4 == 0, 20 times (n = 864..940 in the capture) |
| phase_speed | PIT + music | 0; from 3d:00, +1 at every tick with n % 4 == 0, 30 times (n = 1152..1268) |
| phase | PIT | sum of phase_speed per tick (mod 4096 when used) |
| fade | PIT | level = n (n <= 256); from the first tick with order >= 0x43: -1 per tick to 0 (n = 2018..2274 in the capture) |
| end | music | order > 0x44 (0x1c7a3) |

### 3.7 Art

- File 2 (height) and file 3 (colour), 256 x 256 bytes each: generated terrain, not hand-drawn: a smooth fractal
  height map (values 3..255; 250..255 = water, 19 % of the map) and a colour map shaded from it (its brightness
  correlates -0.72 with d/dx - d/dy of the height: light from one diagonal; water blue, land green / brown). Both
  tile (row 255 = row 0; the columns wrap smoothly). Used as voxel height and colour maps. A "redraw" would have to be
  a matching pair at a higher resolution (for example the original maps upsampled with a smooth filter, or a
  re-generated terrain with the same shapes); otherwise the remake reads the original maps with bilinear filtering.
- File 4: the 256-colour palette (6-bit) of the colour map (indices 1..222 used), faded by level / 256.

### 3.8 Not used

0x118f (an alternative wiggle table: row offset `(S[phase] >> 7) * b >> 8`, a linear bend) and 0x1259 (row_speed
script function): no callers.

---------------------------------------------------------------------------------------------------------------

## 4. Pictures and textures of this scope

| file | content | made | how it is used | true-colour redraw |
|---|---|---|---|---|
| 9, 8, 14, 10 | "Join us", "Bring the family", green poodle, "STARS wonders of the world" (palette + 320x200) | hand-drawn (8 with a halftone-like group of men) | shown as-is, palette fade 128 retraces each way | yes, as-is, brightness = level / 256 |
| 11, 12, 13 | women on magenta stars (one shared palette) | hand-drawn (painted) | intro: as-is with a 64-retrace fade; warp: middle 256 columns, rows stretched to 256, wrapping texture through the patch | yes; the warp samples it at (32 + u, v * 200/256) with wrap |
| 5 | four words, 144 x 36, 9-step ramp + white | hand-drawn lettering | drawn once at (88, 150), opaque over the flower | yes, with alpha; faded with the card |
| 6 | four names, 128 x 74, 9-step ramp | hand-drawn lettering | drawn once at (96, 60), opaque; brightness pulsed with band 0 and faded | yes, multiply by (3 + sin phi_0) / 4 * L |
| 7 | 256 x 256: four identical eye shapes (6 flat colours) in four palette bands | hand-pixelled | texture of the polar map; colour cycling = per-band brightness pulse in the palette | yes: one eye drawn smoothly, the band brightness chosen by v / 64 |
| 2, 3, 4 | height map, colour map, palette | generated terrain | voxel tunnel | only as a consistent higher-resolution pair; else filter the originals |

---------------------------------------------------------------------------------------------------------------

## 5. Open points

- The `x / 200` horizontal step of the warp patch (2.6) looks like a slip (x / 320 would make the patch meet the
  right curve), but the capture is exact with it; it stays.
- The 9-byte buffer list records at 0x973c / 0xf4e ({dword variable, dword size, byte make selector}; the selector is
  stored at variable + 4) are read by 0x4a894 / 0x4a8c7 (core scope).
- On a real machine the PIT tick phase against the music (here the callback starts 0.02 s before the first music
  tick) and the frame rate vary; the capture's values (70.086 fps cards, 35.04 fps warp and tunnel) are the reference
  for everything stepped per drawn frame (1.7, 2.7, 3.6).
- With an odd minute the cards at 02:01 and 04:01 swap, including their petal shapes (0x542a has 3 petals, 0x5437
  13).

## 6. Models (`models/parts2d-a/`)

See its README.md. The hooked runs (`run_hook.py`, a copy of `run_stars.py` with `--hook ADDR:MEM/LEN,...`) are
started from the snapshots f279_5a92, f9478_975b and f11498_11e8 and take 20-40 s each; their outputs (`runs/`)
and images (`out/`) are generated from the original and are not kept.
