# Scope "parts2d-b": particles, dissolving picture, face (the parts that step per frame)

All addresses are offsets in `unpacked/stars_img.bin` (= near pointers = linear addresses in the emulator). "tick" =
PIT tick (70.064 Hz, the part's callback [0x4ab06] runs once per tick). "frame" = one pass of the part's main loop
(one picture drawn); "record" = a displayed frame of the capture `full.frames` (70.086 Hz). "pos" = music position
order:row in hex (`0x29f80` returns AX = order << 8 | row; `0x1c7a3` returns AL = order, AH = row). `>>` on signed
values is an arithmetic shift (floor), as `sar` in the code. Colours are 6-bit VGA values.

Everything below was checked against the emulator, not only read (models in `models/parts2d-b/`, section 8):

| part | model | frames compared | result |
|---|---|---|---|
| particles 0x133ad (variant 1) | `particles_model.py` | all 546 drawn frames (records 18452..18997) | bit-exact; also with every tick-driven input recomputed from the PIT callbacks and the music rows (`--ticks`); the capture's VGA and palettes match |
| particles 0x133f1 (variant 2) | `particles_model.py` | all 1477 drawn frames (records 21957..23433) | bit-exact, also `--ticks` (random state carried over from variant 1, orbit blobs, words) |
| dissolving picture 0x1535c | `dissolve_model.py` | all 234 complete drawn frames (records 19003..19468) | bit-exact screens (G / R-B dither), also with the zoom offsets and the stamp schedule recomputed (`--ticks`); capture palettes match |
| face 0x1089c | `face_model.py` | all 1727 drawn frames (records 23437..25163) | bit-exact in all three modes, including `script`: the whole main-loop script (parameter blocks, light, zoom oscillator, strobe, per-frame zoom steps) driven only by the PIT callbacks and the music position |
| face with the PIT frozen | `run_hook.py --freeze-pit` | 400 records | 25 screen changes, then still: explained in 6.7 |

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

| part | records | time (s) | pos | drawn fps in the capture | look |
|---|---|---|---|---|---|
| particles, variant 1: 0x13388 set-up (main 0x231), 0x133ad (0x236), 0x1339d free (0x23b) | 18452–18997 | 263.26–271.05 | 65:0a–69:00 | 70.086 (every retrace) | 10 new white or black blobs per frame in the middle of the screen, the whole screen zoomed out from near the centre and blurred every frame: glowing particles with soft trails, a 32-colour dark-blue → white palette; "NOOON" (file 58) stamped into the feedback at rows 0 and 0x1e of each order |
| particles, variant 2: 0x13388 (0x2b0), 0x133f1 (0x2b5), 0x1339d (0x2ba) | 21957–23433 | 313.27–334.34 | 7d:01–87:00 | 70.086 | the same with a beige / mauve → white palette, four extra blobs on Lissajous orbits round the centre (long curved trails), and the words NOOON / BRING / YOUR / BEAUTY / FAMILY one per half pattern |
| dissolving picture: 0x1528c set-up (0x254), 0x1535c (0x264), 0x1530a free (0x269) | 18999–19468 | 271.08–277.79 | 69:01–6b:31 | 35.04 (every 2nd retrace) | three masked true-colour pictures of RA's woman (the intro's pictures 11, 12, 13) stamped every 16 rows into an 18-bit RGB feedback buffer that zooms by 128/119 per frame round a centre that circles twice a second, blurred per frame: the picture explodes into a brown blur; shown through a G / R-B checkerboard dither |
| face: 0x10b26 set-up (0x2d3), 0x1089c (0x2e3), 0x10bd9 free (0x2e8) | 23437–25163 | 334.39–359.03 | 87:01–92:31 | 70.086 | an environment-mapped head (per-pixel texture coordinates, file 34; a marbled sphere map, file 35) drawn into an 8-bit feedback buffer that is warped every frame (zoom about the centre plus sine displacements per column and per row) and darkened through a colour remap table, so ripples and smears flow out of the face; a highlight sweeping across the face; a strobe phase; the face switched off near the end |

Frame loop structure (all three): `0x4a7b0` (fade step result to the DAC, then wait for the vertical retrace), then
draw into a buffer and copy / convert to 0xa0000 straight after the retrace. Every frame is one pass; nothing is
interpolated. Both particle variants and the face draw one frame per retrace in the capture (the particles even at
20 M instructions/s, the face at 40 and 80 M but 35 fps at 20 M); the dissolve needs more than one frame time at
40 M and draws every second retrace (23 / 35 / 70 fps at 20 / 40 / 80 M).

## 2. Data

Static data in the image (read at run time by address) and the overlay files:

| what | where | format |
|---|---|---|
| sine table A | 0xa740, 5120 dwords (cos = +0x1000 bytes = 0xb740) | `round(65528 sin(2 pi i / 4096))`, i = 0..5119 (exactly; amplitude 65528 = 0xfff8) |
| sine table B | 0x14260, 2048 int16 | `round(1023 sin(2 pi i / 2048))` (exact); indexed by a byte offset & 0xfff |
| sine table C | 0xffb8, 512 int32 | `round(1024 sin(2 pi i / 512))` (exact); indexed by a byte offset & 0x7fc |
| particle palettes | 0x13048 (variant 1), 0x130a8 (variant 2): 32 x 3 bytes, 6-bit RGB | v1: (0,5,14) dark blue … cyan-grey … (63,63,63); v2: (0,5,14) … mauve / beige … (63,63,63). The fade scales 256 colours from that address, but only 0..31 occur |
| word table, variant 1 | 0x13178: 13 entries of 4 dwords {dst offset, src offset in file 58, width, height} | entries 0,1,2,4,5,7,9,11 = NOOON: dst 25685 (x 85, y 80), src 0, 150 x 24; entries 3,6,8,10,12 = 1 x 1 at src 0 (file 58 byte 0 is 0: draws nothing) |
| word table, variant 2 | 0x13278: 17 entries, same format | NOOON dst (85,81) src (0,0) 150x24: entries 0,1,13,16; BRING dst (96,85) src (0,24) 128x24: 4, 8; YOUR dst (108,85) src (0,48) 104x24: 5, 9; BEAUTY dst (67,85) src (0,71) 166x24: 6; FAMILY dst (68,85) src (0,93) 162x24: 10; nothing (1x1): 2,3,7,11,12,14,15 |
| file 58 | 0xfa00 bytes = 320 x 200 | hand-pixelled serif lettering NOOON / BRING / YOUR / BEAUTY / FAMILY at the left edge, rows 0..117, values 0 and 31 only |
| files 36, 37, 38 | span lists (1148 / 1284 / 1240 bytes) | u16 skip, u16 count pairs, 0xffff ends: the pixel position advances by skip, then count pixels are copied |
| files 39, 40, 41 | 31677 / 26427 / 26478 bytes | the masked pixels of one picture as three 6-bit planes, R bytes for all spans, then G, then B (10559 / 8809 / 8826 pixels). They are the intro pictures 11, 12, 13 (palette + 320x200, parts2d-a) converted to 6:6:6 at the same position, the figure only (the background, indices 0 and 102..108, is outside the mask); 0 / 1 / 3 pixels differ from the palette colours |
| face parameter blocks | 0xf740 (live, 16 dwords), 0x107dc, 0x1081c, 0x1085c (copied over it) | see 6.6 |
| light angle | dword 0x107ca = 0x180 in the image | |
| strobe accumulator / increment | words 0x10a7e = 0, 0x10a80 = 1600 | |
| file 34 | 55348 bytes: spans {u16 0, u16 skip, u16 count, u16 0, count dwords} | the face: per pixel a dword d with two signed 8-bit texture coordinates in bits 1..8 and 12..19 (bits 0, 9..11 are 0; bits 20..31 hold another value 0..2024, perhaps depth, not used). 13200 pixels, x 115..210, y 12..199. The list continues past the screen (892 bytes); the program stops at pixel position 0xf8c0 |
| file 35 | 0x122ff bytes | 0x000: palette (256 x 3, 6-bit); 0x300..0x102fe: the environment map, 256 bytes per row (255 rows + 255 bytes); 0x102ff: 32 remap tables of 256 bytes (table k at 0x102ff + 256 k) |

Generated at run time: the zoom maps and the zoom code of the particles (0x13922) and of the dissolve (0x1554d), the
dissolve's palette and R/B lookup (0x79fad), the face's span list (0x11a0a, from file 34) and the patch table of
its unrolled warp (0x10b26). All formulas below.

The random generator 0x4af10 (core) is called by no other part on a normal machine (only the MDA debug display,
0xacc..0xb13, uses it). Its seed dword [0x4af35] = 0x7ea9 in the image is therefore the state at the start of
particle variant 1; variant 2 continues the sequence (section 4.3).

## 3. Shared mechanics

### 3.1 The palette fade (core helpers, as these parts use them)

Variables: [0x4a798] source palette, [0x4a79c] work buffer (0x300 bytes, allocated by the part), [0x4a7a0] level,
[0x4a7a4] step, [0x4a7a8] steps left, [0x4a7ac] dirty.

```c
void fade_tick() {                       // 0x4a80b, called from the part's PIT callback: once per tick
    if (count) { level += step; count--; dirty = 1; }
}
void fade_and_wait() {                   // 0x4a7b0, once per frame
    if (dirty == 1 || count != 0) {
        dirty = 0;
        for (i = 0; i < 768; i++) work[i] = min(63, (src[i] * level) >> 8);   // 0x4a720 (8-bit result)
        wait_retrace();                  // 0x4a830: start of the vertical sync, then its end
        set_dac(0, 256, work);           // 0x4a742
    } else wait_retrace();
}
```
All three parts fade in with level 0, step +4, 64 steps (64 ticks = 0.913 s to level 256) and fade out with step
-4, 64 steps. Level 256 gives the palette unchanged.

### 3.2 The zoom-blur feedback (particles and dissolve)

Both parts generate the same kind of code: for one destination row, 80 groups of 4 bytes, each a packed dword
average of the four neighbours of a source address:

```
mov eax, [esi + X-1]; mov ebp, [esi + X+1]; add eax, ebp
mov ebp, [esi + X-320]; add eax, ebp; mov ebp, [esi + X+320]; add eax, ebp
shr eax, 2; and eax, edx                  ; edx = 0x1f1f1f1f (particles) / 0x3f3f3f3f (dissolve)
[mov [ebx + 4i], eax]                     ; particles only: straight to the screen
stosd                                     ; to the destination buffer
```
X = the column map entry of group i; esi = source + row map entry * 320. The values are at most 31 (63), so four
of them never carry into the next byte, and the mask removes the two bits shifted in from the next byte. Per byte
this is exactly

```c
dst[r][4i + k] = (s[a - 1] + s[a + 1] + s[a - 320] + s[a + 320]) >> 2,   a = row(r) * 320 + col(i) + k   (k = 0..3)
```
(the centre pixel is not used). The maps (`e` runs over -160, -156, .., 156; `j` = 0..199):

| | particles 0x13922 | dissolve 0x1554d |
|---|---|---|
| columns (80 dwords) | 0x12814: `col(i) = ((e * 120 + 64) >> 7) + 160` | 0x13a30: `((e * 119 + 64) >> 7) + 160` |
| rows (200 dwords, used backwards: row(r) = table[199 - r]) | 0x12d14: `((99 - j) * 120 >> 7) + 100` → `row(r) = 100 + ((r - 100) * 120 >> 7)` | 0x13f30: `100 + ((r - 100) * 119 >> 7)` |
| code | [0x13040], 42 bytes per group + `ret` (3361 bytes), the block resized to fit | [0x1425c], 36 bytes per group + `ret` (2881 bytes) |

So every frame the picture is scaled up by 128/120 = 1.0667 (particles) or 128/119 = 1.0756 (dissolve) about the
centre and blurred by a 4-tap kernel of variance 0.5 px² per axis. Horizontally the scale is applied per group of 4
pixels: inside a group the 4 source pixels are adjacent (scale 1), so the zoom moves in steps of 4 columns and the
picture shows 4-pixel-wide blocks. A least-squares fit of the maps (`zoom_fit.py`, pixel centres) gives the
continuous equivalent `src = 0.9375 dst + b`: expansion centre (164.0, 93.1) for the particles and (162.1, 93.4) for
the dissolve before its offsets (section 5.4), not the nominal (160, 100): the floors push the picture up by about
half a pixel per frame, which moves the fixed point of the feedback by 0.47 / (1 - 0.9375) = 7 px. The largest
deviation of the integer map from the fit is 0.5 px (0.6 px).

The truncation (`>> 2`) loses 0..3/4 of a level wherever the four neighbours do not sum to a multiple of 4, and
nothing in flat areas. It is what makes the trails fade: `particles_float_test.py` runs variant 1 with the same
inputs three ways; without truncation the screen saturates (mean level 27.3 of 31 instead of 17.6 at frame 150, and
24.8 instead of 6.0 at frame 200), with a constant -3/8 level per step instead it stays close (17.1 / 3.8 at those
frames, 5.4 vs 5.5 at frame 500; the share of pixels >= 16 within 0.02) and looks alike (`particles_float_test.png`
when generated).

## 4. Particles: 0x13388 / 0x133ad / 0x133f1 / 0x1339d (callbacks 0x1350e / 0x134e3)

### 4.1 Set-up and free

```c
void setup_13388() {                     // main 0x231 and 0x2b0: called before each variant
    build_zoom();                        // 0x13922: maps 0x12814 / 0x12d14 (3.2); bufs = alloc(0x1f400) -> [0x13034],
                                         //   src [0x13038] = bufs, dst [0x1303c] = bufs + 0xfa00, both cleared to 0;
                                         //   code [0x13040] = alloc(0x10000), generated, resized (0x17cd2)
    file58 = load(58);                   // [0x12810]
}
void free_1339d() { free(bufs); free(code); free(file58); }   // 0x13a1b + 0x17c8e
```

### 4.2 The parts

```c
static u8  last_order;                   // byte [0x134e2]
static s32 timer;                        // [0x13170]  word timer, in ticks
static u32 word;                         // [0x13174]  word entry
static u32 n;                            // [0x13044]  tick counter: 0 in the image, never reset
static u32 since;                        // [0x13539]  ticks since the word timer was last processed: never reset
static u32 ring;                         // [0x13168]  +0x100 per frame, only for the dead code in 4.3

void part_133ad() / part_133f1() {       // variant 1 / variant 2
    fadebuf = alloc(0x300);              // [0x4a79c]
    last_order = 0x68 / 0x86;
    word = 0; timer = 20;
    fadepal = 0x13048 / 0x130a8;         // [0x4a798]
    loop_1348e() / loop_13435();
    free(fadebuf);
}
void loop() {                            // 0x1348e (variant 1) / 0x13435 (variant 2, which first blacks the DAC, 0x4a76c)
    set_pit_callback(0x1350e / 0x134e3);
    word = 0; level = 0; step = 4; count = 64;       // fade in over 64 ticks
    do {
        fade_and_wait();                 // 0x4a7b0
        draw(src);                       // 0x135c5 (4.3)
        zoom();                          // 0x13579 (4.4)
    } while (order() <= last_order);     // exits at 69:00 / 87:00
    clear_pit_callback();
}
void callback() {                        // 0x1350e and 0x134e3 are identical
    n++; since++;                        // 0x1353d
    fade_tick();
    if (step >= 0 && order() >= last_order) { step = -4; count = 64; }   // fade out from 68:00 / 86:00
    if (row() == 0 || row() == 0x1e) timer = 20;     // every tick of those rows
}
```

### 4.3 The draw step 0x135c5 (into src)

```c
for (i = 0; i < 10; i++) {                       // random blobs
    y = 50 + rand(100);  x = 80 + rand(160);
    c = (rand(256) & 1) ? 0 : 31;                // black or full white, 50 / 50
    small_blob(src, y * 320 + x, c);             // 0x138ff
}
if (rand(4000) < 1000) {                         // dead code: positions of 10 points on a circle of radius 16 round
    ...                                          // (160, 100) at angle index (ring & 0xfff) + k * 0x400 are computed
}                                                // with colour 0x08, but no call draws them
ring += 0x100;
if (timer != 0) {                                // the word
    timer -= since;
    if (timer < 0) { timer = 0; if (++word >= (variant == 2 ? 17 : 13)) word = 0; }
    since = 0;
    e = &table[word];                            // 0x13178 / 0x13278: {dst, src, w, h}
    for (r = 0; r < e->h; r++) for (k = 0; k < e->w; k++)
        if (b = file58[e->src + 320 r + k]) src[e->dst + 320 r + k] = b;   // b = 31
}
if (variant == 2)                                // ([0x4a798] == 0x130a8): four orbiting blobs, colour 31, at n as read
    for each (s, ys, xs, neg) in {(4, 10, 10, no), (6, 12, 10, no), (2, 11, 10, yes), (7, 11, 12, no)} {
        a = ((neg ? -n : n) << s) & 0xfff;
        y = 100 + (SINA[a] >> ys);  x = 160 + (SINA[a + 1024] >> xs);
        big_blob(src, y * 320 + x, 31);          // 0x138be
    }
```
`rand(m)` = 0x4af10: `s = rol32((s ^ 0x78d47e5d) * 0xd413d473, 18); return s % m` (unsigned). Exactly 31 calls per
frame, in the order above. Variant 1 starts with s = 0x7ea9; variant 2 starts where variant 1 stopped (after
31 x 546 calls in the capture: checked, s = 0x156fde44 at the first frame of variant 2). The remake
replays it with one step per drawn frame of variant 1 (one per retrace from the part's first frame to 69:00).

Blob shapes, (x, y) = the position computed above:

| | rows (columns) |
|---|---|
| small 0x138ff | y-3 (x+2..x+4), y-2 (x+1..x+5), y-1 (x+1..x+5), y (x+2..x+4): 5 x 4, centre (x+3, y-1.5) |
| big 0x138be | y-3 (x+2..x+4), y-2 (x+1..x+5), y-1, y, y+1 (x..x+6), y+2 (x+1..x+5), y+3 (x+1..x+3): 7 x 7 (the last row is one pixel left of the first) |

Orbits (SINA amplitude 65528: `>> 10` = radius 64, `>> 11` = 32, `>> 12` = 16; big-blob centre = (x + 3, y)):

| blob | x radius | y radius | period | direction |
|---|---|---|---|---|
| 1 | 64 | 64 | 256 ticks (3.65 s) | y = sin, x = cos of +16 n / 4096 turns |
| 2 | 64 | 16 | 64 ticks (0.91 s) | +64 n |
| 3 | 64 | 32 | 1024 ticks (14.6 s) | -4 n (the other way round) |
| 4 | 16 | 32 | 32 ticks (0.46 s) | +128 n |

n is not reset: it starts at 0 at variant 1 (where it is not used) and at 545 (the ticks of variant 1) at variant 2.

### 4.4 Zoom 0x13579

```c
for (r = 0; r < 200; r++) zoom_row(dst + 320 r, vga + 320 r, src + row(r) * 320);   // generated code, 3.2
swap(src, dst);                          // [0x13038] <-> [0x1303c]
```
The screen gets the new frame directly (written right after the retrace wait). The reads stay inside the buffer
(rows 5..193, columns 9..310), so the outer border never feeds back.

### 4.5 Words in the capture (`particles_text_timeline.py`)

A word stays while the timer counts: rows 0 and 0x1e set it to 20 on every tick of the row (about 3 ticks), so it
is drawn on the ~22 frames until 20 ticks after the row ended. The word index advances when the timer is found
negative: at the first frame after the next trigger (`since` has grown to ~70 by then), so normally once per
trigger. When a frame sees two ticks at the end of a countdown the timer goes from 1 to -1 and the next entry is
drawn for one frame (once in the capture, entry 0 at record 23112). Variant 2 starts with `since` = 33 (ticks from
variant 1's last word to its end), so its first frame advances to entry 1 and draws NOOON for that one frame only.
The patterns here have 48 rows, so the triggers alternate 30 and 18 rows apart (90 and 54 frames, 1.29 and 0.77 s).

| variant | words, one per trigger (rows 0 and 0x1e), from the part start |
|---|---|
| 1 (65:0a..) | NOOON (entry 0, from the start, timer set by the part), NOOON (65:1e), NOOON (66:00), – (66:1e), NOOON, NOOON, –, NOOON (68:1e) |
| 2 (7d:01..) | NOOON (1 frame), –, –, BRING (7e:1e), YOUR, BEAUTY, –, BRING, YOUR, FAMILY, –, –, NOOON, –, –, NOOON (84:1e) (+ entry 0 for 1 frame), NOOON (85:00), –, –, BRING (86:1e) |

### 4.6 Time bases

| variable | time base |
|---|---|
| random blobs, zoom-blur, words and orbit blobs being drawn | per drawn frame = per retrace (70.086 Hz in the capture and on any machine fast enough; the part needs well under a frame) |
| n (orbit phase), word timer, fade level | PIT ticks since the callback was installed (n and `since` carried over from variant 1 into variant 2) |
| word triggers, fade-out, end | music: rows 0 / 0x1e; order >= 0x68 / 0x86 fade out; order > 0x68 / 0x86 end |

The remake steps the whole feedback at 70.086 Hz (one step per retrace) and reads the tick-driven values at each
step; it cannot render this part independently per time without replaying the steps from the part start.

### 4.7 Shortcuts and what to evaluate instead

- 5-bit levels, 32-colour palette: keep a float level 0..31 per pixel and interpolate between the 32 palette
  entries (they are smooth ramps).
- The zoom moves 4-pixel groups (horizontal scale 1 inside a group): use the continuous map of 3.2 (scale 15/16 on
  both axes about the fitted centre (164.0, 93.1) in 320x200 pixel units, or (160, 100) if the 7-px offset is not
  wanted), sampled with bilinear filtering.
- The 4-neighbour blur of variance 0.5 px² per step: a Gaussian (or the same 4 taps) of variance 0.5 original
  pixels² per step, scaled to the output resolution.
- The truncation: subtract 3/8 of a level (0.375 / 31 of full scale) per step after the blur (3.2), clamped at 0.
- Blobs: the small blob is a 5 x 4 rounded square, the big one 7 x 7 (its asymmetric last row is an artefact): draw
  anti-aliased discs of the same area at the same centres. The words: see section 7.

## 5. Dissolving picture: 0x1528c / 0x1535c / 0x1530a (callback 0x1544d)

### 5.1 Set-up and free

```c
void setup_1528c() {                     // main 0x254
    build_zoom3();                       // 0x1554d: maps 0x13a30 / 0x13f30 (3.2); block [0x14250] = alloc(0x5dc00),
                                         //   cleared; src [0x14254] = block, dst [0x14258] = block + 0x2ee00 (each:
                                         //   three planes R, G, B of 0xfa00 bytes); code [0x1425c] (no screen store)
    colour_setup();                      // 0x79fad (5.5)
    fadepal = palette [0x79fa8]; fadebuf = alloc(0x300);
    for (k = 0; k < 6; k++) file[k] = load(36 + k);   // [0x15260..0x15274]
}
void free_1530a() { free files, fadebuf; free_colour(); /* 0x7a04c */ free(block); free(code); /* 0x1563d */ }
```

### 5.2 The part

```c
static u32 phase;                        // [0x15278], 0 in the image
static s32 dx, dy;                       // [0x1527c], [0x15280], 0 in the image

void part_1535c() {
    set_pit_callback(0x1544d);
    level = 0; step = 4; count = 64;
    for (;;) {
        pos = music_pos();
        if (pos > 0x6b30) break;                                      // ends at 6b:31
        if (step >= 0 && pos > 0x6b19) { step = -4; count = 64; }   // fade out from 6b:1a
        pos = music_pos();
        if ((row & 0xf) == 0) {                                       // every frame while the row is 0, 16, 32, 48
            if (order == 0x69) stamp(36, 39);
            else if (order == 0x6a) stamp(37, 40);
            else if (order == 0x6b) stamp(38, 41);
        }
        zoom3();                         // 0x1549b: src -> dst, 3 planes
        fade_and_wait();                 // 0x4a7b0
        convert(dst);                    // 0x7a061 -> 0xa0000
        swap(src, dst);
    }
    clear_pit_callback();
}
void callback_1544d() {
    fade_tick();
    phase += 0x78;
    b = phase & 0xfff;                                  // byte offset into the 2048-word table B
    dx = ((SINB[b / 2] * 4) >> 10) + 1;                 // floor(1023 sin / 256) + 1: -3..4
    dy =  (SINB[((b + 0x400) & 0xfff) / 2] * 4) >> 10;  // floor(1023 cos / 256):     -4..3
}
```
The offset turns by 60/2048 of a circle per tick: once per 34.13 ticks (0.487 s, 2.05 Hz).

### 5.3 Stamp 0x15500

```c
void stamp(spans, data) {                // into all three planes of src
    u8 *d = data;
    for (p = 0; p < 3; p++) {            // R, G, B: the same span list, the data continues
        u8 *o = src + p * 0xfa00;
        for (s = spans; s[0] != 0xffff; s += 2) {   // u16 skip, u16 count (0xffff in either ends)
            o += s[0]; if (s[1] == 0xffff) break;
            memcpy(o, d, s[1]); o += s[1]; d += s[1];
        }
    }
}
```
In the capture each stamp happens on the two frames that fall into the row (rows last 3 ticks, frames 2
retraces): records 19045/47 (69:10), 19093/95 (69:20), 19141/43 (69:30), 19159/61 (6a:00), 19207/09, 19255/57,
19303/05 (6a:30), 19321/23 (6b:00), 19369/71, 19417/19, and once at 19467 (6b:30, nearly black by then). The part
starts at 69:01, so the 69:00 stamp is missed. How many frames stamp per row depends on the frame rate.

### 5.4 Zoom 0x1549b

```c
for (p = 0; p < 3; p++)
    for (r = 0; r < 200; r++) {
        o = dy * 320 + dx;               // read again for every row: a tick in the middle of the zoom changes it
        zoom_row(dst_p + 320 r, src_p + row(r) * 320 + o);   // 3.2 with mask 0x3f
    }
```
The reads stay inside each plane (rows 2..196, columns 7..313). The offset moves the sampling point, so the fixed
point of the feedback is `centre + offset / (1 - 119/128)`: the expansion centre circles with radius
3.81 / 0.0703 = 54 px round about (169, 86) (the floors give dx a mean of +0.50 and dy a mean of -0.50; fit
`dx = 3.81 sin + 0.50`): the "zooming blur" swirls. In the capture the offset changed inside a frame's zoom 182
times (the rows below the change use the new offset).

### 5.5 Colour set-up 0x79fad and conversion 0x7a061

Palette [0x79fa8] (768 bytes):
```c
for (g = 0; g < 16; g++) pal[g] = (0, (g * 0x434) >> 8, 0);                              // green ramp 0..63
for (r = 0; r < 15; r++) for (b = 0; b < 16; b++)
    pal[16 + 16 r + b] = ((r * 0x492) >> 8, 0, (b * 0x434) >> 8);                       // red x blue grid
```
R/B lookup [0x79fa0] (the first 64 KB boundary inside a block [0x79fa4] of 0x14000 bytes; 64 rows of 256, 64 used):
`RB[R][B] = 16 * ((0x100 + R * 0x3c) >> 8) + (B >> 2)` = `16 + 16 * (60 R >> 8) + (B >> 2)` for 6-bit R, B.

```c
void convert(planes) {                   // 0x7a061: 100 row pairs
    flag ^= 1;                           // byte [0x79fac], 0 in the image: toggles every frame
    for (y = 0; y < 200; y++) for (x = 0; x < 320; x++) {
        rb_here = ((x + y) & 1) == (flag ? 0 : 1);   // flag 1: even rows 0x7a0dd, odd rows 0x7a120; flag 0: swapped
        vga[y][x] = rb_here ? RB[R[y][x]][B[y][x]] : (G[y][x] >> 2);
    }
}
```
So every pixel shows either its red + blue or its green, in a checkerboard that changes phase every drawn frame:
spatial and temporal dithering of an 18-bit picture through 256 colours (R in 15 levels, G and B in 16). The eye
mixes (R, 0, B) and (0, G, 0) to the colour at half the light. 0x7a09e (with 0x7a164 / 0x7a1b8) is the same
conversion that also clears the source planes; there is no call to it.

### 5.6 Time bases

| variable | time base |
|---|---|
| zoom-blur step, stamps (while the row is a multiple of 16), the dither phase | per drawn frame: 35.04 Hz in the capture (every second retrace); 23 / 70 fps at 20 / 80 M instructions/s, so the speed of the explosion depends on the machine. The capture's 35 is the reference |
| zoom offset (dx, dy), fade | PIT ticks since 0x1535c installed the callback: phase = 0x78 t |
| stamps, fade-out, end | music: orders 0x69 / 0x6a / 0x6b, rows & 0xf == 0; > 0x6b19 fade out; > 0x6b30 end |

### 5.7 Shortcuts and what to evaluate instead

- The 6:6:6 picture is shown through the G / R-B checkerboard with R quantised to 15 levels and G, B to 16: show
  the planes as true colour instead. To match the brightness a viewer saw, scale by the mixing of two pixels: half
  the light, i.e. `v * 0.5^(1/2.2) = 0.73 v` in display values (0.5 v if matched to a box-filtered capture). Say
  which in the README.
- Zoom (scale 119/128 per step about the fitted centre, plus the offset, sub-pixel: `dx = 1023 sin(a)/256 + 0.5`,
  `dy = 1023 cos(a)/256 - 0.5`, one offset per step), blur and truncation (-3/8 of a level of 63 per step) as for
  the particles (4.7), in float RGB with bilinear reads. Step at 35.04 Hz.
- The stamp writes the masked picture at its 320x200 position (no scaling): with a redrawn picture, stamp the
  redraw (colour and coverage) at the same place.

## 6. Face: 0x10b26 / 0x1089c / 0x10bd9 (callback 0x10ae2)

### 6.1 Set-up and free

```c
void setup_10b26() {                     // main 0x2d3
    // 0x11979
    set_dac_black();                     // 0x4a76c
    fadebuf = alloc(0x300); fadepal = alloc(0x300);  // [0x4a79c], [0x4a798]
    f35 = load(35);                      // [0x107b8]
    memcpy(fadepal, f35, 0x300);         // the palette
    sel = data_selector(f35 + 0x300, 0x10000);       // [0x107bc] (its base is moved every frame, 6.3)
    tables = f35 + 0x300 + 0xffff;       // [0x107be]: 32 remap tables of 256 bytes
    // 0x11a0a: the face span list
    list = alloc(0x40000);               // [0x107c2]
    f34 = load(34); pos = 0;
    do {
        skip = f34[+2]; count = f34[+4]; f34 += 8; emit16(skip); emit16(count); pos += skip;
        while (count--) {
            d = *f34++;                                  // dword
            a = (s8)((d >> 1) & 0xff) >> 1;              // bits 1..8, signed, halved: -64..63
            b = (s8)((d >> 12) & 0xff) >> 1;             // bits 12..19
            w = ((b & 0xff) << 8 | (a & 0xff)) + 0x8080; // add ax, 0x8080: a's carry goes into the high byte
            emit16(((w & 0xff) << 8) | (w >> 8 & 0xff)); // xchg: word = (a + 128) << 8 | (b + 128 + (a < 0))
            pos++;
        }
    } while (pos < 0xf8c0);
    emit32(0xffffffff); resize(list); free(f34);
    // buffers
    tab = align256(alloc(0x200));                    // [0xffb0] / [0xffb4]: the current remap table
    blk = alloc(0x2ee00), cleared;                    // [0xffa0]
    A = blk + 0x3e80; B = blk + 0x17700;              // [0xffa4] current, [0xffa8] other (selectors [0xffac],
                                                      //  [0xffae] are made but not used)
    // the warp code: find the 320 displacements 0x1234 in 0x10cfe..0x11765 and store their addresses at 0xfaa0 +
    // 4 x for pixel x (the scan meets them in the order pixel 3, 2, 1, 0, 7, 6, ..: strides -4, -4, -4, +28)
}
void free_10bd9() { free(blk); free(tab block); /* 0x119df */ free(f35); free_selector; free(fadepal, fadebuf);
                    /* 0x11a82 */ free(list); }
```
A and B are 320x200 frames inside a zero block: 50 rows of zeros above A, 50 between A and B, 100 below B. Only
the 64000 bytes of A and B are ever written, so the margins stay 0 (black).

### 6.2 The frame 0x10a86

```c
void frame() {
    a = angle & 0x7fc;                                   // [0x107ca]
    s = SINC[a / 4] >> 5;                                // computed, then overwritten (eax = ebx): unused
    c = SINC[((a + 0x200) & 0x7fc) / 4] >> 5;            // cos: -32..32
    light = c * 256 + 0x2000;                            // [0x107c6]
    fade_and_wait();                                     // 0x4a7b0
    remap_and_face(cur);                                 // 0x1178c (6.3)
    warp(cur -> other, and to 0xa0000);                  // 0x10bf3 (6.4)
    swap(cur, other);                                    // [0xffa4] <-> [0xffa8]
}
```

### 6.3 Remap and face 0x1178c (in place on the current frame)

```c
memcpy(tab, tables + 256 * P.table, 256);                // P = the parameter block 0xf740 (6.6)
if (P.face <= 0) {
    for (i = 0; i < 64000; i++) cur[i] = tab[cur[i]];
} else {
    base = f35 + light;                                  // data selector base moved (0x17de6)
    i = 0;
    for (each span of the list) {                        // skip, count, count words
        repeat skip:  cur[i] = tab[cur[i]], i++;         // the background between the face spans
        repeat count: cur[i] = base[word], i++;          // the face: 16-bit offset into file 35 + light
    }                                                    // the last 132 pixels (after 63868) are not remapped
}
```
The face reads file 35 as a 256-wide byte picture from offset 0 (rows 0..2 are the palette): row
`(a + 128) + 32 + c`, column `(b + 128 + (a < 0))`, where a comes from bits 1..8 and b from bits 12..19 of the
file 34 dword: a is the texture row and follows the normal's x on screen, so moving the light offset c (rows)
moves the highlight across the face from side to side (checked by rendering the face with c = -32, 0, 32). Rows used: 65..254. The env map uses
colours 3..31 only (the first ramp, 6.5); the face pixels 5..31.

### 6.4 Warp 0x10bf3

```c
// columns: a vertical displacement and the zoomed x; read: phases P.ph[0], P.ph[1] once, z and the steps per column
e1 = P.ph[0]; e2 = P.ph[1]; edx = ((256 - z) * 320) >> 1;
for (x = 0; x < 320; x++) {
    e1 &= 0x7fc; e2 &= 0x7fc;
    v = ((SINC[e1 / 4] + SINC[e2 / 4]) * z) >> 16;       // rows: up to +-2048 z / 65536 (+-7 at z 236)
    col[x] = v * 320 + (edx >> 8);                       // patched into the code (displacement for pixel x)
    edx += z; e1 += P.step[0]; e2 += P.step[1];
}
// rows: a horizontal displacement and the zoomed y; phases P.ph[2], P.ph[3] read once here, after the columns
e3 = P.ph[2]; e4 = P.ph[3]; edx = ((256 - z) * 200) >> 1;
for (y = 0; y < 200; y++) {
    e3 &= 0x7fc; e4 &= 0x7fc;
    v = ((SINC[e3 / 4] + SINC[e4 / 4]) * z) >> 16;
    rowoff[y] = (edx >> 8) * 320 + v;                    // 0xf780
    edx += z; e3 += P.step[2]; e4 += P.step[3];
}
// the unrolled code (0x10cdf..0x11775): for every row y and pixel x
other[y * 320 + x] = vga[y * 320 + x] = cur[rowoff[y] + col[x]];
```
So `x_src = floor(160 + (x - 160) z / 256) + dx(y)`, `y_src = floor(100 + (y - 100) z / 256) + dy(x)` with
`dy(x) = floor((S(i1 + x s1) + S(i2 + x s2)) z / 65536)`, `dx(y)` likewise, S = 1024 sin(2 pi i / 512),
i = phase / 4. Two details of the index arithmetic: the `& 0x7fc` drops the low two bits of every sum, so the
effective step is `step & ~3` (step 18 acts as 16, 5 as 4, ...), while the phases themselves keep their low bits.
z < 256 zooms in (content flows outwards), z > 256 zooms out (content flows to the centre). Nothing is clamped:
a row above 0 or below 199 reads the zero margins (black flows in), a column left of 0 or right of 319 reads the end
of the neighbouring row.

### 6.5 Palette and remap tables (file 35)

The palette is 8 blocks of 32 colours: block 0 (0..31) a blue-grey ramp from (0,0,0) over (39,39,60) to white;
block 1 the ramp lifted to grey 21; block 2 lifted to grey 42; block 3 white; blocks 4..7 the ramp scaled to about
0.78, 0.59, 0.39, 0.2. The 32 remap tables map each colour to another one; table 16 is the identity up to duplicate
colours, table k < 16 darkens (nearest palette colour to about k/16 of the colour; table 15 fits 0.94, but no
simple nearest-colour rule reproduces it exactly: 176..220 of 256 entries depending on the metric, `remap_fit*.py`),
tables > 16 shift to other colours. The part uses only 15 (until 0x9000) and 16 (from 0x9000).

Table 15 as a process: repeated, every colour reaches a fixed point within 23 steps (iterating the table from
file 35), and the fixed points are not black: white → (42,42,42) in 7 steps (block 2), bright blue → (21,21,21) in
14..18 (block 1), mid and dark ramp colours → (10,10,16) (colour 6) or stay (0..4, 6). So the trails fade to a
grey-blue background, not to black, and bright smears keep a grey colour. Table 16 freezes everything.

### 6.6 The callback, the oscillator and the script

```c
void callback_10ae2() {                  // per tick
    angle += aspeed;                     // [0x107ca] += [0x107ce]
    P.ph[0] += P.spd[0]; P.ph[1] += P.spd[1]; P.ph[2] += P.spd[2]; P.ph[3] += P.spd[3];   // 0xf770.. += 0xf744 ..
    if (osc.on) {                        // 0x11ad8
        osc.ph += osc.speed;             // [0x11a8d] (0 in the image) += [0x11a91]
        *osc.target = ((SINA[osc.ph & 0xfff] * osc.amp) >> 16) + osc.mid;
    }
    fade_tick();
}
void osc_start(lo, hi, speed, target) {  // 0x11aa5: amp = (hi - lo) >> 1 (unsigned), mid = lo + amp, on = 1
}                                        // 0x11acd: on = 0 (the target keeps its last value)
```
Parameter block P at 0xf740 (16 dwords): [0] unused, then {spd, step} for the four phases (0xf744/48, 4c/50, 54/58,
5c/60), z (0xf764), table (0xf768), face (0xf76c), the four phases (0xf770..0xf77c). The block copies replace all
64 bytes, so they also reset the phases to 0.

| block | spd 1..4 (per tick) | step 1..4 (effective, entries) | z | table | face |
|---|---|---|---|---|---|
| 0xf740 (image) | 0 0 0 0 | 0 0 0 0 | 256 | 15 | 1 |
| 0x107dc | 46 10 3 20 | 32 18 20 5 (8 4 5 1) | 221 | 15 | 1 |
| 0x1081c ([0] = 70) | 46 10 17 42 | 38 19 28 5 (9 4 7 1) | 236 | 15 | 1 |
| 0x1085c | 38 30 23 20 | 15 21 22 6 (3 5 5 1) | 236 | 16 | 1 |

A phase advancing by spd per tick turns by spd / 2048 per tick (46: once per 44.5 ticks, 0.64 s).

The script (main loop of 0x1089c; each "frame" is 6.2; a check happens after every frame; capture frames from
`face_check_capture.py`):

```c
set_pit_callback(0x10ae2);
osc_start(0xd0, 0x130, 0x32, &P.z);      // z = 256 + (SINA[50 t] * 48 >> 16): 208..304, period 81.92 ticks (1.17 s)
level = 0; step = 4; count = 64;         // fade in
do frame(); while (pos < 0x8820);        // 1. pulsing zoom, no ripple, face on, table 15        (frames 1..237)
osc_stop(); aspeed = 0x50;               //    z keeps its value (229 in the capture)
do frame(); while (angle < 0x980);       // 2. the light sweeps across once: 26 ticks (0x180 -> 0x9a0)   (..263)
aspeed = 0; P = block 0x107dc;           // 3. ripples, z 221
do frame(); while (pos <= 0x8920);
do frame(); while (pos <= 0x8b00);       //                                                       (..576)
aspeed = 0x1e;                           // 4. light sweeps continuously: one cycle per 68.3 ticks (0.97 s)
do frame(); while (pos <= 0x8b20);       //                                                       (..673)
P = block 0x1081c;                       // 5. other ripples, z 236
do frame(); while (pos <= 0x8d00);       //                                                       (..865)
P.face = 0;                              // 6. strobe
do {
    frame();                             //    without the face
    acc += inc;                          //    16-bit [0x10a7e] += [0x10a80] (1600 at first)
    if (carry) { P.face = 1; frame(); P.face = 0; }      // an extra frame with the face
    inc += 0x32;                         //    per frame: the flashes come faster (31 in the capture)
} while (pos <= 0x8f00);                 //                                                       (..1153)
P.face = 1;                              // 7.
do frame(); while (pos <= 0x9000);       //                                                       (..1298)
P = block 0x1085c; cnt = 0x30;           // 8. table 16 (no fading), z 236 rising
do {
    if (cnt) { cnt--; if ((cnt & 3) == 0) P.z++; }       //    +1 every 4th frame, 12 times: 236 -> 248
    frame();
} while (pos <= 0x9200);                 //                                                       (..1586)
P.face = 0; P.z += 0x16;                 // 9. no face, z 270 (zooming out, black flows in)
do frame(); while (pos <= 0x9210);       //                                                       (..1634)
count = 64; step = -4;                   // 10. fade out
do frame(); while (pos <= 0x9230);       //                                                       (..1727, 92:31)
clear_pit_callback();
```
The patterns of these orders have 48 rows.

### 6.7 Why the face changes while the PIT is frozen

Because the feedback itself is stepped per drawn frame: every frame remaps the previous frame through table 15
and warps it into the other buffer, whatever the PIT does. With IRQ 0 not delivered (`--freeze-pit` from the
part's snapshot) the callback never runs, so: the fade level stays 0 (the DAC stays black: 0 palette changes; the
VGA memory changes but nothing is visible), the phases stay 0 (no ripple: dx = dy = 0), z stays at the 259 the
oscillator had reached in the snapshot, the light does not move, and the script cannot leave step 2 (it waits for
the light angle to reach 0x980, which only the callback advances; the music still moves since it runs on the GUS
timer). What remains is a fixed map applied once per frame: zoom out by 256/259, remap by table 15, redraw the
static face. That converges: every colour reaches a fixed point of table 15 within 23 applications, the face is
the same every frame and the black margins have flowed in, so after 24 drawn frames the frame no longer changes
(the frozen run: records 1..25 change, the remaining 375 are identical). The phase-1 run's "25 changes in 400
frames" is this convergence, not a hidden animation.

With the PIT running, the per-frame steps are: the feedback (zoom, ripple displacement and remap applied once per
frame), the strobe accumulator, and the z ramp of step 8. At 35 fps the trails would flow half as fast and the
strobe would take twice as long per flash.

### 6.8 Time bases

| variable | time base |
|---|---|
| warp + remap + face redraw (the feedback), strobe (acc, inc), z ramp at 0x9000 | per drawn frame: 70.086 Hz in the capture (one per retrace; 35 at 20 M) |
| phases P.ph, light angle, zoom oscillator (until 0x8820), fade | PIT ticks since 0x1089c installed the callback (the oscillator phase 50 t; the light angle starts at 0x180 and only moves from 0x8820 on) |
| script steps | music position (above) and the light angle (step 2 → 3) |

### 6.9 Shortcuts and what to evaluate instead

- The feedback works on palette indices, and its look depends on the remap table's fixed points (trails end in
  grey-blue, white smears in grey 42). Two ways to keep it: (a) run the same index feedback at the output resolution
  (nearest reads are natural for indices; the remap is a 256-byte lookup) and convert through the palette at the
  end; (b) work in true colour with a colour-to-colour function built at start-up from table 15 / 16 and the
  palette (for any RGB: the nearest palette colour i, then pal[T[i]]; as a 3D lookup table), which gives exactly
  the original's dynamics for palette colours. A plain multiply by 15/16 would fade to black and is not the look.
- The warp: x_src / y_src as in 6.4 with exact (float) zoom and displacements `(S1 + S2) z / 65536` instead of the
  floors, at output resolution. Bilinear reads in a feedback loop blur cumulatively, which the original does not:
  use nearest reads at the output resolution (the integer steps then shrink to one output pixel). The effective steps
  are `step & ~3`; keep them (they decide the wavelength of the ripples). Out of range: black above / below, the
  neighbouring row at the left / right edges (wrap with a one-row shift); treat both as black if the wrap is not
  wanted (it only shows while z > 256).
- The face: interpolate the two texture coordinates (a, b) bilinearly between the 96 x 188 face pixels (they are
  smooth) and sample the env map bilinearly: the head comes out sharp. The mask (the span list) needs an
  anti-aliased edge; eye holes are part of the mask.
- The light offset only has a vertical texture component (cos >> 5, the sine is computed and dropped): keep that
  (the highlight sweeps sideways across the face); evaluate `32 cos(2 pi angle / 2048)` in float.

## 7. Pictures and textures: hand-drawn or generated, and what a redraw needs

| picture | made how | used how | true-colour redraw? |
|---|---|---|---|
| file 58, the words | hand-pixelled serif lettering, 1 bit (0 / 31) | as a mask: its non-zero pixels set level 31 in the particle feedback buffer, inside the rectangles of the word tables | yes, as a high-resolution coverage mask (one channel, anti-aliased) with the same layout: stamp `level = max(level, 31 * coverage)` at the scaled rectangles. Colour comes from the palette ramp, not from the picture |
| particle blobs | generated (code shapes) | stamps of level 31 or 0 | draw as discs |
| particle palettes 0x13048 / 0x130a8 | hand-made 32-entry ramps | map the feedback level to colour, faded | keep (interpolate between entries) |
| files 36..41 (= intro pictures 11, 12, 13) | hand-drawn by RA (the same drawings as the intro's women; here 6:6:6 per pixel, masked to the figure) | drawn as-is (stamped) into an RGB feedback buffer, then dissolved by the zoom-blur | yes, directly: a redraw of pictures 11..13 serves both parts. The remake stamps the redraw's RGB with its figure mask (the mask of files 36..38, or the redraw's own alpha) at the same position and scale (320x200 frame) |
| dissolve palette / R-B table | generated (5.5) | display only | not needed in true colour |
| file 34, the head | generated: per-pixel environment-map coordinates of a rendered 3D head (a Nefertiti-like bust), 96 x 188 px | coordinates into file 35's env map | not a picture: interpolate (6.9); a sharper head needs a re-render of the normals at higher resolution, not a redraw |
| file 35 env map | a painted / rendered marbled blue sphere with a highlight, 256 x 255, 8-bit indices into the 32-colour ramp | sampled by the face, then fed through the remap tables (palette tricks) | a redraw must either stay in the 32-level ramp (indices) or come with a true-colour version of the remap (6.9 (b)); the highlight position matters (the light moves the map vertically by up to 32 rows) |
| file 35 palette and remap tables | generated offline from the palette (nearest-colour search) | the decay of the feedback | keep as data (6.9) |

## 8. Models and how they were checked (`models/parts2d-b/`)

The models need the hooked runs, which come from a copy of the emulator: `run_hook.py` = `run_stars.py` with
`--hookmod FILE` (a Python file with `install(mu, st, args)` adding unicorn code hooks and `finish()`). The hook
files record an event log (PIT callbacks with the music position, frame starts, the values at the instructions that
read them, stamps, palette levels) and every drawn frame. Run from `models/parts2d-b` inside the toolbox:

```
python3 run_hook.py --load --snap ../../snaps/f18451_133ad.snap --out runs/p1 --frames 18998 --hookmod hook_particles.py
python3 run_hook.py --load --snap ../../snaps/f21956_133f1.snap --out runs/p2 --frames 23434 --hookmod hook_particles.py
python3 run_hook.py --load --snap ../../snaps/f18999_1535c.snap --out runs/d --frames 19470 --hookmod hook_dissolve.py
python3 run_hook.py --load --snap ../../snaps/f23436_1089c.snap --out runs/f --frames 25164 --hookmod hook_face.py
python3 particles_model.py runs/p1.pkl 1 f18451_133ad.snap --ticks     # 546 frames, 0 differ
python3 particles_model.py runs/p2.pkl 2 f21956_133f1.snap --ticks     # 1477 frames, 0 differ
python3 dissolve_model.py runs/d.pkl --ticks                            # 234 screens, 0 differ
python3 face_model.py runs/f.pkl script                                 # 1727 frames, 0 differ
python3 particles_check_capture.py runs/p1.pkl 1 f18451_133ad.snap      # against full.frames (VGA + palette)
python3 dissolve_check_capture.py; python3 face_check_capture.py
```
The runs take 5..30 s each. Results:

- Particles: both variants bit-exact on every drawn frame, with the recorded inputs and with `--ticks` (n, the word
  timer and index recomputed from the callbacks; the random generator from the image's seed, carried over to
  variant 2). Against `full.frames`: every record of the part matches the last drawn frame (except the very last
  record of each part, where the next part has started), and every palette is `min(63, (pal * L) >> 8)` for a
  level L in 0, 4, .., 256.
- Dissolve: bit-exact on all 234 complete frames, in both modes (`--ticks`: every offset (dx, dy) recomputed from
  the callback phase and compared with the value the zoom read; the stamps recomputed from the music position).
  The zoom uses the per-row offsets where the hook saw them change (the mid-frame changes of 5.4). Records
  19003..19468 of `full.frames` match, palettes too.
- Face: bit-exact on all 1727 frames with the recorded warp tables, with the tables from the formula (6.4), and
  in `script` mode where only the callbacks, the music position after each frame, and the points where the warp
  read its phases (`W0` / `W1` events; a tick between the column and the row loop changes the row phases) come from
  the run. The script state (all 16 parameter dwords and the light angle) matches at every frame. Records
  23437..25162 match; palettes are file 35's palette times the fade level.
- The frozen-PIT face run: `runs/ffrozen` (400 records, 25 changes, palette black throughout).
- `particles_float_test.py`: integer vs float zoom-blur (3.2). `zoom_fit.py`: the continuous zoom maps.
  `pics.py`: decodes files 36..41. `face_data.py`: decodes files 34 and 35. `remap_fit.py`, `remap_fit2.py`: the
  remap tables vs nearest-colour rules.

## 9. Not determined / guesses

- How the remap tables of file 35 were computed (nearest palette colour to a scaled colour fits table 15 only for
  176..220 of 256 entries depending on the metric); they are data, so the remake reads them.
- What bits 20..31 of the file 34 dwords hold (0..2024; a guess: depth or intensity of the render). Unused.
- The light offset: the code computes the sine and then overwrites it with the cosine term (`mov eax, ebx` after
  `imul ebx, 0x100`); probably meant `add eax, ebx` (a 2D light movement). The demo shows the 1D sweep; keep it.
- The particle ring (0x13610..0x1366c) is computed on a quarter of the frames but never drawn (the call is
  missing); only its random number matters (it is part of the 31 per frame).
- Whether the remake should keep the expansion centres of the fitted maps (164, 93) / (169, 86) (they come from
  floors) or use the screen centre: the shift is 4..14 px of 320, visible as the point the trails stream from.
  These notes recommend the fitted values.

## 10. Timing details measured for the remake

Found while porting (`src/scenes/zoomblur.h`, `particles.cpp`, `dissolve.cpp`, `face.cpp`); measured in the
snapshots and with a timing hook (PIT callback 0x1353d, draw start 0x135c5, word read 0x13676 with the emulator's
clock) over 0x133f1.

- **PIT phase.** Every snapshot (f15972 .. f25163) gives the next PIT tick after its vertical sync; in music time all
  ticks fall at `t * 70.0635 = 0.924 (mod 1)`. A part sees the ticks after it installs its callback: 0x133ad /
  0x133f1 just before their first retrace wait (no callback yet at the sync of f18451 / f21956), 0x1535c and
  0x1089c at their start (one callback before the sync of f23436).
- **When the capture draws.** The emulator lets a loop polling 0x3da jump to the next change of the retrace bit after
  half a quantum (1/64 of a 70 Hz frame), so the wait of 0x4a830 normally ends at line 449 (= line 0 of the next
  record), not at the end of the sync (line 414). An interrupt inside the sync ends it earlier: at the interrupt when
  it falls 7 or more lines after line 412, otherwise at line 414. Interrupts there: the PIT ticks, the GUS timer 2
  (the player's ticks; the emulator raises them 8 lines after the music's tick times) and the GUS timer 1, which keeps
  running every 20 ms (phase 0.000735 s) after the player has switched its IRQ off. With this rule 1473 of the 1477
  draws of 0x133f1 come out within 0.3 lines of the timing run (2 within a line, 2 at line 414 instead of 449), and
  every frame sees the same PIT ticks as there. That decides the words where a tick falls close to a draw: the
  trigger of 84:1e is seen by frame 1133, and frame 1155 sees two ticks (timer 1 -> -1, entry 0 for one frame), so
  BRING appears at 86:1e. Both variants' word frames (546 + 1477) then match the capture exactly. On a real machine
  the draw follows line 414 and the GUS handler delays it by its run time instead; the outcome near such
  coincidences is a matter of the machine.
- **Dissolve.** The frames are on screen from records 19001 + 2j (1.35 retraces after the part's start, then every
  second retrace). An iteration reads the position 1.50..1.76 retraces before its frame is shown (the stamp records of
  5.3 are those reads; the bounds come from 6b:00 and 6b:30); the remake uses 1.6. The last frame (stamped at 6b:30)
  is shown from record 19469, after the cue's end at 6b:31.
- **Face.** The first frame is on screen from record 23437 (1.53 retraces after the start), then one per retrace;
  1727 frames.
- **Truncation in float.** A float step with the loss `0.375 * clamp(spread / 0.5, 0, 1)` levels, spread = max - min
  of the four neighbours, keeps the mean level of variant 1 within 1..4 % of the integer model at every 50th frame
  (the integer maps and blobs as the original, with the words); a constant 3/8 is 5..30 % darker where large areas
  are flat (the original's flat areas are equal integers and lose nothing). A single blob's trail sums agree within
  1 % over its 10 frames on screen.
- **The zoom maps' centre.** Both maps leave a 16-pixel band unmoved (columns 156..171, rows 85..100 for the
  particles): the 4-pixel staircase rounds the shift to 0 there. In the mean the maps are the linear zoom about
  (164.0, 93.1); the band keeps content near the centre longer and shows as a vertical seam.
