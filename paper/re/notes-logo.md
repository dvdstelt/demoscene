# Scope "logo": "present the Paper intro" page — ticks 5611–6018, music counter 655..704

All addresses are code32 offsets (file offset in paper_img.bin = code32 + 0xd00). Coordinates are in the
original 320x200 frame. "Tick" = 70.0018 Hz IRQ0 tick; "counter" = music counter [0x1ea8b].

## 1. What it draws

A grey, finely grained paper page. At the top "present the" and at the bottom right "intro" in a wide, heavy
sans font with a soft drop shadow (offset +4,+3, half strength). In the middle is the "Paper" logo, drawn as a grey
hand-drawn sketch of thorny letters (a static 224x83 bitmap from the exe). During the part the letters are filled
with ink (near-black body, grey highlights, dark-red/red edges) by a flood fill that grows from 8 seed points,
one 4-connected pixel ring per tick. The seeds start one at a time, every 4 music-counter steps. **The sketch
itself is not animated.** The only thing that changes in the part is the ink fill. Checked: frames 5611→6017 do
not differ outside the logo rectangle, and the fill matches an exact BFS simulation at every tick I sampled.

- Page built by 0x5623 at tick 5480 (counter 640), during the 3D-scene part, before the peel into this page.
- Page fully on screen from tick ~5600 (end of the peel, scene3d scope). 0x572f runs from tick 5611 (counter 655,
  when the peel's [0x1eaa9] reaches 0x2bc) until counter >= [0x1ea8f] = 0x2c0 = 704 (tick 6017). Then 0x2b55
  hands the finished page to the next part's peel (0x687a, from tick 6018).
- Ink fill: first pixel at counter 664 (tick 5681/5682), all 5003 ink pixels done at tick 5962. The complete logo
  then stays still for ~55 ticks before the peel starts.

Main-line sequence (main 0x792b..0x799b):
```
callback = 0x6170 (peel stepper, scene3d); logo_setup()  /*0x5623*/; peel_set(bb, page) /*0x2b55*/
loop 3D scene + peel 0x2b6c until [0x1eaa9] >= 0x2bc        (scene3d scope)
[0x1ea8f] = 0x2c0; logo_part() /*0x572f*/; peel_set(page, bb) /*0x2b55*/; [0x1ea8f] = 0x380; 0x687a ...
```

## 2. Buffers and static data

| what | where | format |
|---|---|---|
| back buffer `bb` | [0x1edf9] | 320x200 bytes, copied to VGA [0x1edf1] |
| work buffer `page` | [0x1edc5] | 320x200 bytes: page values, then palette indices, then the fill map/state during 0x572f, then the final page |
| paper grain `tex` | [0x1edf5] | 320x200 bytes, values 28..31 (0x55e2, at init) |
| LUT | [0x1eded] (64 KB aligned) | 256x256, `LUT[tex<<8 | value]` (0x144b, at init) |
| font | [0x1ede9] | 29 glyphs x 2048 bytes (0xe2e, at init) |
| BFS list | [0x1edfd] (getmem 0x20000 + 0x800) | u32 offsets; count [0x1edb1] (exe data 0, never reset; only this part uses it) |
| string | 0x5929 | `"present the\x01intro\0"` |
| seed table | 0x585f | 9 dwords: 0x438d, 0x2317, 0x5030, 0x60a2, 0x5466, 0x1e93, 0x3191, 0x4bd4, 0 |
| font RLE data | 0x18ed7..0x19692 | see 0xe2e |
| ink/fill image | 0x196c3, 92 rows x 112 bytes (10304) | 224x92, 4 bit/pixel, high nibble first; 0 = no ink, 1..15 = colour 0x40+n |
| sketch image | 0x1bf03, 83 rows x 112 bytes (9296), ends 0x1e353 | 224x83, 4 bit/pixel, high nibble first; n = darkness (0 = paper) |
| palette | 0x169a3 (768 bytes, 6-bit) | entries 0..127 used as they are (see section 6) |

## 3. Page construction — 0x5623 (tick 5480)

```c
void logo_setup(void)                         // 0x5623
{
    tickc = 0;                                // [0x1edc1]
    *(u32 *)0xeb8 = 320;                      // SELF-MODIFYING: row stride of glyph blitter 0xe8a ("add edi,0x280"
                                              // at 0xeb6, was 640 for the init-time "psychic link" buffer)
    memset(page, 0x3f, 64000);
    memset(bb, 0, 64000);                     // bb is scratch: an 80-row text buffer T (stride 320)
    draw_text(bb + 4*320 + 20, "present the\x01intro");          // shadow copy, at (20,4)
    for (k = 5; k != 0; k--) {                // byte [0x21455]
        blur(bb + 320, 78, 320);              // rows 1..78
        if (k == 2) {                         // i.e. after the 4th blur
            for (i = 0; i < 80*320; i++) bb[i] >>= 1;            // shr dword + and 0x7f7f7f7f
            draw_text(bb + 1*320 + 16, "present the\x01intro");  // sharp copy, at (16,1)
        }
    }                                         // => shadow: 5 blurs, x0.5;  sharp text: 1 blur
    // text buffer rows 0..39 -> page rows 10..49, rows 40..79 -> page rows 158..197, x + 4
    u8 *d = page + 10*320 + 4;                // 0xc84
    for (i = 0; i < 80*320; i++) {
        if (i == 40*320) d += 0x8700;         // skip 108 rows
        *d++ = 63 - (bb[i] >> 2);             // 63 = white paper .. 0 = black
    }
    unpack4(page + 50*320 + 48, 0x1bf03, 83, /*invert*/1);       // the "Paper" sketch, overwrites
    for (i = 0; i < 64000; i++) page[i] = LUT[(tex[i] << 8) | page[i]];   // 0x1949 (esi=page, ebp=tex)
}
```

### Text blitter (0x11b0 -> 0xe8a)
```c
void draw_text(u8 *dst, const u8 *s) {
    for (; *s; s++) {
        s8 c = *s - 'a';
        if (c < 0) {                          // every character below 'a'
            dst += 10;                        // space = 10 px
            if ((u8)c == 0xa0) dst += 0x303e; // '\x01': total +0x3048 = 38 rows + 200 px (x wraps into the next row)
            continue;
        }
        u8 *g = font + c*2048; int w = g[0];
        for (y = 0; y < 32; y++) for (x = 0; x < w; x++)
            dst[y*320 + x] = min(255, dst[y*320 + x] + g[1 + y*60 + x]);   // saturating add
        dst += w + 1;
    }
}
```
Glyph widths a..z: 28 30 27 30 29 20 30 28 8 14 26 8 44 28 32 30 30 19 29 19 28 31 49 29 29 28; '{' '|' '}' (drawn
as "1" "9" "6", used for "1996" elsewhere) 19 28 28. "present the" advances 279 px, "intro" 111 px.
Resulting positions (top-left of the 32-row glyph boxes, final page coordinates):

| | sharp | shadow |
|---|---|---|
| "present the" | (20, 11) | (24, 14) |
| "intro" | (179, 158) | (183, 161) |

(text buffer: sharp (16,1)/(175,40), shadow (20,4)/(179,43); "intro" = 279 + 200 - 320 = +159 px and +39 rows from
the start of "present the").

### Font generator 0xe2e (init; data 0x18ed7)
```c
font = getmem(0xe800); memset(font, 0, 0xe800); src = 0x18ed7;
for (g = 0; g < 29; g++) {                    // 'a'..'z','{','|','}'
    u8 *d = font + g*2048;  d[0] = *src++;    // width
    for (y = 0; y < 32; y++) {
        u8 *p = d + 1 + y*60;
        for (;;) {
            u8 b = *src++;  if (b == 0) break;            // end of row
            if (b == 0xff) { skip = src[0]; run = src[1]; src += 2; }
            else           { skip = b & 15; run = b >> 4; }
            p += skip;  while (run--) *p++ = 0xff;        // 1-bit glyph, 0 / 0xff
        }
    }
}
```
1-bit, 32 rows high, max 60 wide. A wide, heavy grotesque (looks like Helvetica Black Extended; that is a guess).

### Blur 0xf47 / 0xf90 (shared with 0x13af)
```c
void blur(u8 *p, int rows, int stride) {      // temp at the free-heap pointer [8] (not allocated)
    int n = rows*stride;  u8 tmp[640 + n + 640] = {0};   // 640 zero bytes before and after
    for (i = 0; i < n; i++) tmp[640+i] = (p[i-stride] + p[i+stride] + p[i-1] + p[i+1]) >> 2;
    for (i = 0; i < n; i++) p[i]       = (tmp[640+i-stride] + tmp[640+i+stride] + tmp[640+i-1] + tmp[640+i+1]) >> 2;
}
```
Pass 1 reads the real rows just outside the range (bb rows 0 and 79, both 0 here). Pass 2 sees zeros there.
x-1/x+1 wrap into the neighbouring row, but nothing is near the edges here. One call = the 4-neighbour cross
applied twice, i.e. the 5x5 kernel {(0,0):4, (+-1,+-1):2, (+-2,0),(0,+-2):1}/16, variance 1.0 px^2 per axis,
truncated after each pass.

### 4-bit image unpacker 0x58ea (esi = data, edi = dst, ch = rows, bl = invert)
```c
for (r = 0; r < rows; r++, dst += 320 - 224)
    for (i = 0; i < 112; i++) { u8 b = *src++;
        u8 n0 = b >> 4, n1 = b & 15;          // high nibble = left pixel
        if (invert) { n0 = (15-n0)*4 + 3; n1 = (15-n1)*4 + 3; }   // 3..63 page value
        *dst++ = n0; *dst++ = n1; }
```

### Value-to-index LUT (0x144b, only the part used here)
For LUT row `ch` <= 32 and column `cl` < 64: `LUT[ch][cl] = max(0, cl + 2*ch - 64)`. Here `ch` = tex = 28..31,
so **index = max(0, v - 8 + 2*(tex-28))**: the grain darkens by 8/6/4/2 levels. Paper (v = 63) becomes index
55/57/59/61. (Other parts of the LUT: for cl >= 64, `(cl & 0xe0) + max(0, (cl&31) + ch' - 32)`, with ch' = ch for ch <= 32,
else `((ch-32)>>1) + ((ch-32)>>3)`. Rows 0..64 only. At LUT+0x10000: `min(255, cl*ch >> 5)`. None of these is used here.
Verified against the snapshot.)

### Paper grain 0x55e2 (init; also used by other parts)
```c
for (i = 0; i < 0x3ed1; i++) ((u32 *)tex)[i] = random() & 0x0f0f0f0f;   // 64324 bytes of noise 0..15
for (i = 0; i < 64000; i++) {                                         // in place, forward
    s8 d = tex[i] - tex[i + 321];             // (+1,+1) neighbour, still the raw noise
    u8 a = (d >> 3) + 30;                     // arithmetic shift: -2..+1
    tex[i] = a > 31 ? 31 : a;                 // the clamp never triggers
}                                             // -> 28 (10.9%), 29 (35.9%), 30 (39.1%), 31 (14.1%): embossed noise
```
`random()` = 0xaa2, an additive lagged Fibonacci generator: `x[k] += x[j]; return x[k]; j = (j+1)&63; k = (k+1)&63`,
with 64 dwords at 0x9a0 and j = [0xaa0], k = [0xaa1] (exe initial values j = 29, k = 63), i.e. x_n = x_{n-34} + x_{n-64} mod 2^32.
Measured by emulation: 0x55e2 starts after **123976** calls from program start (tick 6). With the exe state, my Python
regeneration gives exactly the texture in the t5500 snapshot (script `tex.py`, see section 9).

### Verification
A Python reimplementation of all of section 3 (page.py) reproduces the converted page in snapshot t5500 at
[0x1edc5] byte for byte (64000/64000).

Quirk: the 80-row text buffer is split at row 40. The blurred 'i' dot of "intro" reaches up into buffer row 39
(values <= 17), so it ends up as a faint smudge on page row 49, x 179..189, instead of above "intro". It is barely
visible.

## 4. The part — 0x572f + callback 0x561c (ticks 5611..6018)

```c
void tick_cb(void) { tickc++; }               // 0x561c, from IRQ0 every 70 Hz tick

void logo_part(void)                          // 0x572f
{
    memcpy(bb, page, 64000);                  // page (palette indices) -> back buffer
    memset(page, 0, 64000);
    unpack4(page, 0x196c3, 92, 0);            // fill map at page[0..91][0..223], stride 320: 0 = wall, 1..15
    callback = tick_cb;
    u8 seed_i = 0, last_q = 0; s8 delay = 2;  // dword [0x21455] = 0x20000 -> bytes 0x21455/56/57 = 0,0,2
    for (;;) {
        do {                                  // one BFS layer per elapsed tick (catch-up loop)
            int n = count;                    // entries appended now are handled next layer
            for (j = 0; j < n; j++) {         // re-scans the whole list every layer
                u32 p = list[j];  u8 v = page[p];
                if (v <= 0x40) continue;      // done, or a wall (0 + 0x40)
                bb[50*320 + 48 + p] = v;      // ink colour 0x41..0x4f, drawn straight (no grain)
                page[p] = 0x40;
                push(p - 1); push(p + 1); push(p + 320); push(p - 320);
            }
        } while (--tickc > 0);
        memcpy(VGA, bb, 64000);
        do { esc_check(); hidden_blur_1996(1); } while (tickc <= 0);   // 0x7a64, 0x13af: wait for the next tick
        if (music >= 704) break;
        if ((music & 3) == 0 && (music >> 2) != last_q) {               // once per 4 counter steps
            last_q = music >> 2;
            if (--delay < 0 && seeds[seed_i] != 0) push(seeds[seed_i++]);   // table 0x585f
        }
    }
    callback = 0;
    memcpy(page, bb, 64000);                  // finished page, used by the next peel
}

void push(s32 p) {                            // 0x58b5
    if (p <= 0 || page[p] >= 0x40) return;
    page[p] += 0x40;  list[count++] = p;
}
```
So pixel p of the fill map gets ink at tick `R(p) = min_k (T_k + d_k(p))`, where d_k is the **4-connected geodesic
(L1) distance** from seed k through the nonzero pixels of the fill map, and T_k is the tick at which seed k is pushed.
Its colour is `0x40 + fillmap[p]`. Map pixel (x, y) is drawn at screen (x + 48, y + 50). Map pixels that are 0 keep the
page (paper and sketch), so the thin sketch thorns around the ink stay visible.

Seed schedule: the delay byte skips the 4-step boundaries 656 and 660. Seed k is pushed in the first frame after
the counter becomes **664 + 4k** (k = 0..7). Entry 8 is 0, so 696 and 700 do nothing.

| k | offset | map (x,y) | screen | region (screen bbox) | pixels | depth | counter | tick | done at tick |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x438d | (13,54) | (61,104) | "P" 48..109 x 50..141 | 1283 | 156 | 664 | 5681 | 5837 |
| 1 | 0x2317 | (23,28) | (71,78) | dot inside P, 69..77 x 72..80 | 53 | 9 | 668 | 5715 | 5724 |
| 2 | 0x5030 | (48,64) | (96,114) | "a" 90..141 x 83..128 | 933 | 106 | 672 | 5748 | 5854 |
| 3 | 0x60a2 | (98,77) | (146,127) | "p" 132..181 x 73..130 | 934 | 81 | 676 | 5782 | 5863 |
| 4 | 0x5466 | (166,67) | (214,117) | "e" 172..227 x 86..131 | 923 | 119 | 680 | 5815 | 5934 |
| 5 | 0x1e93 | (147,24) | (195,74) | blob above "e", 187..203 x 70..77 | 85 | 13 | 684 | 5849 | 5862 |
| 6 | 0x3191 | (209,39) | (257,89) | "r" 218..268 x 74..125 | 620 | 79 | 688 | 5883 | 5962 |
| 7 | 0x4bd4 | (212,60) | (260,110) | drops after "r", 254..268 x 102..124 | 172 | 17 | 692 | 5916 | 5933 |

(Ticks are the full.ticks ticks at which the counter changes. The capture shows the new pixels one record later,
because it is grabbed before that tick's frame. Together the 8 regions cover all 5003 nonzero map pixels.)
Fill map values: 0 x15605, 1 x312, 2 x267, 3 x116, 4 x140, 5 x303, 6 x297, 7 x74, 8 x279, 9 x450, 11 x2270, 12 x186,
13 x128, 14 x169, 15 x12 (10 is unused). The body is colour 0x4b (dark brown-black), the highlights are greys 0x41..0x49, and the
"red edge pixels" are 0x4c..0x4f. They are all pre-painted in the fill image; nothing is random.

The sketch image uses nibbles 0..9 and 11. There is a tiny "stx" signature (nibble 1) at sketch (198..205, 66..69) =
screen (246..253, 116..119).

Verification: bfs.py simulates R(p) and matches the captured frames exactly (0 mismatching pixels) at ticks 5700,
5750, ..., 6017, including every colour.

### Hidden side work: 0x13af (called in the wait loop)
```c
void hidden_blur_1996(int n) { if ((s32)([0x1edad] -= n) < 0) return; while (n--) blur([0x1ede1], 200, 320); }
```
[0x1ede1] is the big "1996" picture of the end part (0xfb2 draws '{','|','|','}' with 0xedc at init). [0x1edad]
starts at 150: 75 blurs at init (15 x 0x13af(5)), 55 in the pencil part (one per frame), and the **last 20 here**,
one per wait at ticks 5611..5634. Each costs about a tick at the 1.5M budget, before the first seed. Nothing is visible.
After that, [0x1edad] keeps falling (about -17M by t5800) and every call returns at once. The remake can simply
precompute the full 150 blurs for the end part.

### 0x2b55 (peel hand-over)
```c
void peel_set(u8 *a /*esi*/, u8 *b /*edi*/) { [0x1eaa9] = 0; [0x23a0] = a; [0x23a4] = b; }
```
The calls are at 0x7946 (a = bb with the live 3D scene, b = this page) and at 0x7996 (a = [0x1edc5] = the finished,
inked logo page, b = bb, which the next part draws into). In 0x2b6c, [0x23a4] is copied straight to VGA below the
peel line (0x2c99), and [0x23a0] / the grain [0x1edf5] are read at 0x2ca2. Everything else about the peel is in the
scene3d / teal-plane scope.

## 5. Geometry summary (320x200)

- Page: paper everywhere (page value 63) except the following.
- Text: rows 10..49 and 158..197 (x from 4); glyphs as in the table in section 3.
- Sketch: page (48..271, 50..132), overwritten (not blended), values (15-n)*4+3.
- Ink fill: (48..271, 50..141), drawn over the page in palette indices 0x41..0x4f.

## 6. Palette and fades

- No fade in this part: [0x1edb5] = target [0x1edbd] = 0x8000 (step [0x1edb9] = 0x190), and 0x15c0 uploads the
  working palette 0x1eaad unchanged (level 0x80 -> `c*128>>7`). Entries 0..127 are the static exe palette at 0x169a3
  (copied by 0x15af at init). Entries 128..159 belong to the 3D scene and are unused here. The next part replaces 129..158 at tick 6018.
- Entries 0..63, the page greys: `pal[0] = 0; pal[i] = i-1 for 1 <= i <= 52; pal[i] = i for 53 <= i <= 63` (6-bit, R=G=B).
- Entries 0x40..0x4f, the ink (6-bit RGB): 40 (63,63,63) unused, 41 (57,57,57), 42 (51,51,51), 43 (45,45,45),
  44 (39,39,39), 45 (34,34,34), 46 (28,28,28), 47 (22,22,22), 48 (16,16,16), 49 (10,10,10), 4a (5,5,5),
  4b (12,10,10), 4c (20,11,11), 4d (28,10,10), 4e (36,6,6), 4f (44,0,0).
- Resulting page tones: paper = grey 55/57/59/61 (8-bit about 223/231/239/247, by grain). Text value
  v = 63 - T/4 (T = text buffer 0..255), then index max(0, v - 8 + 2g) and grey pal[index]. The solid shadow (T = 127)
  comes out as grey ~23..29 of 63; solid text is 0.

## 7. Time bases

| variable | time base | rate / values |
|---|---|---|
| page, text, shadow, sketch, grain | static | built once at tick 5480 (0x5623) |
| ink BFS layer | 70 Hz tick: [0x1edc1] counted by the callback 0x561c, consumed by the catch-up loop | 1 layer (1 px of L1 geodesic distance) per tick, independent of frame rate |
| seed k | music counter | pushed in the first frame with counter == 664 + 4k (k = 0..7; every ~33.6 ticks) |
| part end | music counter | counter >= 704 ([0x1ea8f] = 0x2c0), tick 6017 |
| palette | static | level 0x8000, no fade |
| hidden "1996" blurs | per wait-loop call | 20 remaining blurs, at ticks 5611..5634, not visible |

Frame rate: at most one frame per tick, because the loop waits for [0x1edc1] > 0, so 70 fps here. There is **no
per-frame variable**. Work per frame (1.5M-instruction budget): ~16K instructions with an empty list (the 16000-dword
VGA copy), up to ~56K at the end (the scan of a list that grows to 6634 entries: 5003 ink + 1631 wall pixels, ~6
instructions per finished entry). In full.ticks, the 5th column for this part (~103K..106K) is **not frames**. It counts
calls to 0x7a64, which here run inside the wait loop: 14 instructions per iteration, ~1.5M/14 when idle. For ticks
5611..5634 it is 1 because each iteration does a ~1-tick hidden blur.

## 8. For the high-resolution remake (what the 320x200 shortcuts approximate)

- **Text**: the font is a 1-bit 32-px bitmap, so a sharp remake needs vector outlines (trace the 29 glyphs, or use a
  matching extended heavy grotesque). Build both lines in one 320x80-unit buffer as described. Shadow = text mask at
  +4,+3 px, blurred 5 times with the cross kernel, which is close to a Gaussian with **sigma = sqrt(5) ~ 2.24 px**, then
  x0.5. Sharp text = mask blurred once (**sigma = 1 px**, mainly anti-aliasing). Composite T = min(255, shadow + text),
  then v = 63 - T/4. The original's per-pass truncation (>>2, a loss of <= 0.75/255 per pass) and the cross kernel's
  parity (it only mixes pixels with the same (x+y) parity, so 1-px features show checkerboard) are artefacts and need
  not be copied.
- **Grain**: 1-px embossed noise (difference to the (+1,+1) neighbour, quantised to 4 levels), subtracted as
  2..8 grey levels via the LUT. For "the same but sharper", generate the noise on the 320x200 lattice (the exact RNG
  sequence is given above), interpolate it at high resolution, and emboss in original-pixel units. Using fresh 1-pixel noise at
  high resolution would look finer than the original.
- **Logo**: hand-painted bitmaps (sketch 224x83, 16 greys; ink 224x92, 15 colours). They are not procedural, so they need
  upscaling or redrawing. Keep the ink-region topology (the 8 regions and their seeds) or recompute it.
- **Fill**: precompute R(p) = min_k(T_k + d_k(p)) with d_k the 4-connected BFS distance (diamond-shaped fronts,
  1 original pixel per tick) on the 224x92 map. At high resolution, interpolate R and compare it with the
  continuous tick time (including the fraction) for a smooth front, or compute L1 geodesic distance on the upscaled mask divided by the scale.
  Ink colours are drawn as-is, with no grain.
- The palette's grey ramp has a 1-level step at index 53 (i-1 below, i above). True colour can use pal[] directly.

## 9. Scripts (scratch, paper/re/models/logo/)

- page.py: reimplements 0x5623 (font, blur, LUT, unpack). Matches t5500 exactly. It also writes outline.png, fillmap.png
  and font.png.
- bfs.py: reimplements the fill and its timing. Matches full.frames exactly.
- tex.py + rng.rng: the RNG state at 0x55e2 (from run_logo.py, a copy of run_paper.py with hooks on 0xaa2/0x55e2) and
  the regenerated grain. Matches t5500 exactly.

## 10. Not determined / guesses

- The font's real-world model (Helvetica Black Extended?) and the origin of the logo art are guesses. The art is clearly
  hand-drawn and stored as bitmaps.
- Peel details (0x2b6c/0x2ad0) and which picture is "on top" are left to the scene3d / teal-plane notes. Here I only
  established which pointer is copied flat (0x2c99).
- On a slow 1996 PC, the 20 hidden blurs at the start could delay seed 0 by at most one blur's duration. Fill timing
  is otherwise tick-locked.
