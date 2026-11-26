# Scope "bump": the bump-mapping effect (SUOMI BUMP, ICE BUMP, DOOMSDAY BUMP)

Routines: InitBUMP 0x402ec4 (with 0x402948, 0x4029dc, 0x402ad0, 0x402dd8), RunBUMPEFEK 0x403434, the draw 0x4031a4,
DeinitBUMP 0x40315c, and the assembler drawers 0x40cf55, 0x40ce79, 0x40cdc8 (self-modifying: they patch their own
displacements and immediates at entry). Unused leftovers in the range: 0x402820 (the generator of the ring table, no
caller), 0x402900 (loads a PCX into the height buffer, no caller), 0x403374 (a copy of RunBUMPEFEK's stepping loop, no
caller), the drawers 0x40d017 / 0x40d0c9 (the ini "type 8", which no ini uses). Shared helpers are described by what
goes in and out (notes-core.md): 0x40241c file by name, 0x401884 InitPCX, 0x401c20 DeinitPCX, 0x401ed0 the fade
step, 0x400104 the music position, Watcom's sscanf 0x4112ce, malloc 0x4112f0, free 0x4112f8, memset 0x411340,
__CHP 0x41130c, fsin 0x4116ce, fcos 0x4116c4.

Clock names as in NOTES.md: **refresh** = one screen refresh (70.086 Hz), a record of the `.frames` capture;
**counter** = the frame counter [0x825c5c] (+1 per refresh; in part2.* counter = record - 4). Refresh numbers below
are records of `part2.frames` (part II run on its own); add 14,444 for `full.frames` (checked: every script line of
the three effects is exactly 14,444 records later in `full.script`).

Checked against the emulator (scripts in `models/bump/`, README there):
- the init buffers (height picture, light picture, the per-pixel table of bumptab.val, both planes of the wave
  buffer, the static normals of type 1) against the snapshots at the three Run starts (`snaps2/f2724`, `f8106`,
  `f11334`): **identical**;
- a Python model of the drawers (`bump.py`) against the screen after every draw of a run resumed from those
  snapshots (`run_bump.py`, a copy of run_vivid.py with hooks): **740 of 740 (SUOMI), 1,071 of 1,071 (ICE), 777 of
  777 (DOOMSDAY) screens bit-exact**;
- the model's stepping of the animated variables (from the frame counter) against the state logged at every draw:
  exact in all 738 / 1,069 / 775 steps (both integer offsets, both angles, the float accumulators);
- the ring table of bumpprm1.val against the formula of the unused generator 0x402820: all 4,096 bytes exact;
  bumpwave.val against floor(8 × distance): all 64,000 values exact;
- the capture's records against the complete draws (`check_capture.py`): every record is either a complete draw or
  a draw in progress (new pixels up to a point in raster order, the previous frame below it), apart from the
  first and last records of each Run (the cleared screen).

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

The effect lights a height picture with a moving light: each pixel's slope (differences of its neighbours' heights)
plus a fixed per-column / per-row base plus a moving light offset gives a coordinate into a 256 × 256 "light
picture", whose index is the pixel's colour. Two variants:

- **type 0** (SUOMI, DOOMSDAY): the height picture stands in water. Two sets of circular waves (rings from the top
  left corner and from the bottom right corner) make a water level; wherever the level is above the picture's
  height the pixel is water (its height is the wave level, index from the light picture), elsewhere land (the
  picture's height, light picture index + 128). The palette's lower half colours the water, the upper half the
  land. The coastline moves with the waves.
- **type 1** (ICE): no water; the normals are computed once at init from the height picture and the light picture
  is a coloured texture (lava-like rock) that slides across the bumpy relief as the light offset moves.

| effect | script lines (part II) | refreshes (part2) | music | on screen |
|---|---|---|---|---|
| SUOMI BUMP | InitBUMP bump1.ini 1 (2707), InitFADE watback→white 2 (2710), WaitSYNC 005 001, InitFADE white→phong 50 (2724), RunBUMPEFEK 007 000 (2724..3792) | Run: 2724..3792 (1,068 refreshes) | 5:01..7:00 | a green relief map of Finland in a blue sea, lit by a light-blue spot that circles; rings of waves roll out from the top left and the bottom right corners and wash over the coast; fades in from white over the first 50 refreshes |
| ICE BUMP | InitBUMP bump3.ini 0 (8101), RunBUMPEFEK 017 000 (8106..9177), ClearSCR | Run: 8106..9177 (1,071) | 15:00..17:00 | an orange/brown lava-rock texture (phong3.pcx) seen through a bumpy relief (bumppic3.pcx, with "DSD" in the bottom left corner); the texture slides in a Lissajous path and wraps around |
| DOOMSDAY BUMP | InitBUMP bump2.ini 0 (11331), RunBUMPEFEK 022 055 (11334..12335), InitFADE phong2→white 60 (12335), RunBUMPEFEK 023 000 (12335..12444) | Run 1: 11334..12335 (1,001), Run 2: 12335..12444 (109) | 21:00..23:00 | the word "doomsday" in raised gold letters across the middle, in grey water with a white spot and a dark-and-grey ring pattern around it, waves rolling; at 22:55 it fades to white in 60 refreshes and stays white until 23:00 |

The script names ("SUOMI BUMP", "ICE BUMP", "DOOMSDAY BUMP") are the comments of `script/demo2.scr`.

## 2. Files

All in archive 2 (`unpacked/files/2/bump/`) except `pal/phong2.pal` (archive 3) and `pal/phong.pal`, `pal/white.pal`
(archive 2, `pal/`).

| file | size | format | use |
|---|---|---|---|
| bump/bump1.ini, bump2.ini, bump3.ini | 111, 117, 117 | text, 11 whitespace-separated fields (§3) | the three effects |
| bump/bumppic1.pcx | 17,901 | PCX 320×200, indices 0..86 (grey palette, unused) | SUOMI height picture: Finland, sea = 0, land a plateau ~60..86 with a fine rough texture |
| bump/bumppic2.pcx | 7,669 | PCX 320×200, indices 0..78 | DOOMSDAY height picture: the word "doomsday" in rounded bold lower-case letters, full width, centred vertically, rounded (bevelled) edges; background 0 |
| bump/bumppic3.pcx | 51,803 | PCX 320×200, indices 0..30 | ICE height picture: a rendered bumpy surface (blobs and ridges) with a small "DSD" logo at the bottom left |
| bump/phong.pcx | 44,231 | PCX 256×256, indices 0..127 | SUOMI light picture: a round spot, index ≈ 127 - r (r = distance from texel (128, 128)), 0 beyond r ≈ 127; ±1 irregularities (not an exact formula); own palette not used |
| bump/phong2.pcx | 44,231 | PCX 256×256 | DOOMSDAY light picture: the **same pixels** as phong.pcx; its own palette equals pal/phong2.pal and is loaded (§8) |
| bump/phong3.pcx | 57,443 | PCX 256×256, indices 0..254, own palette | ICE "light picture": a coloured rock/lava texture; seamless when tiled (edge differences as small as neighbour differences); its palette is loaded (§8) |
| bump/bumpprm1.val | 4,136 | 40-byte header + 2,048 × 2 bytes (§3.3) | wave parameters and the ring table (all three ini files) |
| bump/bumpwave.val | 128,000 | 320×200 little-endian uint16 | 8 × distance field (§3.4), type 0 only |
| bump/bumptab.val | 520 | 320 + 200 bytes | base light coordinate per column and per row (§3.5) |
| bump/phong.pal | 768 | 6-bit VGA | identical to pal/phong.pal; not read (the script uses pal/phong.pal) |
| pal/phong.pal | 768 | 6-bit | SUOMI: 0..127 water ramp black → dark blue → light blue (56,60,63); 128..255 land ramp black → green → near white |
| pal/phong2.pal | 768 | 6-bit (archive 3; = phong2.pcx's palette) | DOOMSDAY: 0..19 black, 20..38 grey rising to 13, 39..47 falling to 0, 48..69 black, 70..119 grey ramp to 63, 120..127 white; 128..255 gold ramp black → brown → orange (60,27,1) → yellow → white (255) |

PCX decoding (0x401884): width = xmax + 1, height = ymax + 1 (header bytes 8..11), RLE stream from byte 128 decoded
for width × height bytes (bytes-per-line is not read); the palette follows the 0x0c marker, 8-bit components
shifted right by 2. InitPCX's third argument: **0 = also set the palette from the PCX** (into the 16.16 palette
0x826abc, then 0x401ed0(1), which writes the DAC at once), 1 = pixels only.

## 3. InitBUMP 0x402ec4 (script: `InitBUMP <ini> <flag>`)

### 3.1 The ini file

```
sscanf(file, "%d %d %f %f %f %f %s %s %s %s %s",
       &type,      // [0x82d46c]  0 = water waves (SUOMI, DOOMSDAY), 1 = static normals (ICE), 8 = unused variant
       &add,       // -> byte [0x82d4a4]: added to the wave level (1 in all three)
       &amp_x,     // [0x82d4b8]  light offset amplitude, u    45 / 40 / 100 (bump1 / bump2 / bump3)
       &amp_y,     // [0x82d4bc]  light offset amplitude, v    24 / 24 / 100
       &speed_x,   // [0x82d4b0]  degrees per refresh, u       1 / 0.5 / 0.5
       &speed_y,   // [0x82d4b4]  degrees per refresh, v       1 / 0.75 / 0.75
       light,      // 0x82d3cc  bump/phong.pcx / phong2.pcx / phong3.pcx
       prm,        // 0x82d3ec  bump/bumpprm1.val
       wave,       // 0x82d40c  bump/bumpwave.val
       height,     // 0x82d42c  bump/bumppic1.pcx / bumppic2.pcx / bumppic3.pcx
       tab);       // 0x82d44c  bump/bumptab.val
```

### 3.2 Buffers and the pictures

```
buf_unused = malloc(64000);    // [0x82d4c0] never used (freed by DeinitBUMP)
height     = malloc(64000);    // [0x82d4c8] 320x200 bytes
surf       = malloc(128000);   // [0x82d4c4] 320x200 x {level, flag}
wave       = malloc(256000);   // [0x82d4cc] 320x200 x {uint16 w0, uint16 w1}
base       = malloc(128000);   // [0x82d4d0] 320x200 x {u0, v0}
normals    = malloc(128000);   // [0x82d4d4] 320x200 x {u, v}   (type 1; not initialised for other types)
light      = malloc(0x20000);  // [0x82d4d8] 256x256 used (512x256 for type 8)
if (type != 8) { InitPCX(63, height_name, flag 1); memcpy(height, slot63, 64000); DeinitPCX(63); }
if (type != 8) build_base(tab_name);                          // 0x402948
if (type == 8) { memset(light, 0, 0x20000); load_light(1, ...); } // centred in a 512-wide buffer (unused)
else load_light(0, script_flag, light_name);                   // 0x4029dc: InitPCX(63, light_name, script_flag);
                                                               // memcpy(light, slot63, 65536); DeinitPCX(63)
```

The script's flag goes to InitPCX for the light picture only: SUOMI passes 1 (palette untouched; the script fades
to pal/phong.pal itself), ICE and DOOMSDAY pass 0 (the palette is set from phong3.pcx / phong2.pcx at once).

`build_base` 0x402948: bumptab.val's 520 bytes: `base[y][x] = { tab[x], tab[320 + y] }` → u0(x) = tab[x],
v0(y) = tab[320 + y].

### 3.3 bumpprm1.val

```
int32  cx0, cy0, cx1, cy1;   // 0, 0, 320, 200: the two wave centres -> [0x82d480..0x82d48c]
int32  n0, n1;               // 9, 4: periods of each ring profile in 2048 entries -> [0x82d478], [0x82d47c]
float  amp0, speed0;         // 11.0, 0.9  -> [0x82d470], [0x82d49c]
float  amp1, speed1;         // 16.0, 1.2  -> [0x82d474], [0x82d4a0]
uint8  ring[2048][2];        // -> 0x82d4e0 (4096 bytes): ring[i][0] = profile 0, ring[i][1] = profile 1
```

(The four floats are stored in the file in the order amp0, speed0, amp1, speed1 at offsets 0x18..0x24.) The demo
uses only the centres, the speeds and the table; n0, n1, amp0, amp1 are read by the unused generator 0x402820, which
makes exactly the stored table:

```
ring[i][0] = 11 + trunc(11 * sin(i * (9 * pi/128) * (1/8)))     // 11 + trunc(11 sin(2 pi * 9 * i / 2048)), 0..22
ring[i][1] = 16 + trunc(16 * cos(i * (4 * pi/128) * (1/8)))     // 16 + trunc(16 cos(2 pi * 4 * i / 2048)), 0..32
```

(pi/128 is the double at 0x8203f4, 1/8 is 1 / [0x821e74]; trunc is __CHP, toward zero.)

### 3.4 The wave buffer (0x402ad0, type 0 only)

bumpwave.val is a quarter distance field: `src[y][x] = floor(8 * sqrt((320 - x)^2 + (200 - y)^2))` (exact for all
64,000 values; 0 would be at the corner (320, 200), the stored corner (319, 199) is 11). 0x402ad0(name, cx, cy, k)
mirrors it about (cx, cy) into the 16-bit plane k of the wave buffer:

```
for (j = 0; j < 200; j++) for (i = 0; i < 320; i++) {
    sx = i < cx ? 320 - cx + i : 319 + cx - i;
    sy = j < cy ? 200 - cy + j : 199 + cy - j;
    wave[j][i].w[k] = src[sy][sx];
}
```

i.e. `w = floor(8 * hypot(|i - cx + 0.5| + 0.5, |j - cy + 0.5| + 0.5))` (8 × the distance in pixels from the centre,
with the centre column / row doubled). InitBUMP calls it twice: plane 0 with (cx0, cy0) = (0, 0), plane 1 with
(cx1, cy1). **Quirk**: the first call clamps all four centre variables to 4..316 / 4..196 before the second call
reads them, so plane 1 is centred on (316, 196), not (320, 200), while plane 0 keeps (0, 0).

### 3.5 bumptab.val

u0(x) rises from 21 to 233 over the 320 columns, v0(y) from 57 to 198 over the 200 rows, in steps of 0 or 1, 128 at
the screen centre (u0(160) = v0(100) = 128), steepest (≈ 0.75 per pixel) in the middle and flatter (≈ 0.55) at the
sides: within ±1 of `128 + A·d / sqrt(d² + D²)` (d = x - 160 or y - 100, A ≈ 236, D ≈ 316; v0 exact with A = 233,
D = 314), the direction to a light above the screen centre. A flat surface therefore shows the light picture's
centre where u0 = 128 - lx and v0 = 128 - ly.

### 3.6 The rest

```
angle_x = angle_y = 0;               // [0x82d4a8], [0x82d4ac] floats
if (type == 1) static_normals();     // 0x402dd8
// NOT reset: b [0x82d494], a [0x82d498] (floats) and the offsets [0x82d490], [0x82d492] (uint16):
// they carry on from the previous bump effect (§5).
```

`static_normals` 0x402dd8 (type 1): `surf[y][x] = { height[y][x], 0 }` for all pixels; for y = 2..197,
x = 2..317 (8-bit wrap-around arithmetic):

```
normals[y][x].u = u0(x) + height[y][x-1] - height[y][x+1];
normals[y][x].v = v0(y) + height[y-1][x] - height[y+1][x];
```

InitBUMP takes 3 refreshes (SUOMI), 5 (ICE) and 3 (DOOMSDAY) in the capture; the screen keeps the previous part's
last frame meanwhile (§9).

## 4. RunBUMPEFEK 0x403434 (script: `POS PAT RunBUMPEFEK`)

```
memset(0xa0000, 0, 64000);                       // clears the screen to index 0
c0 = counter;  read_position();
do {
    draw();                                      // 0x4031a4 (§6), straight into VGA memory
    while (counter == c0) ;                      // wait for the next refresh
    n = counter - c0;                            // refreshes since the last wake (1 or 2 in the capture)
    for (k = 0; k < n; k++) {                    // b, a stay in the FPU (64-bit mantissa) in this loop
        b += 0.9f * 8;                           // [0x82d49c] * [0x821e74] = 7.1999998...
        a += 1.2f * 8;                           // [0x82d4a0] * 8 = 9.6000004...
        off_b = (uint16)trunc(b);                // [0x82d490]
        off_a = (uint16)trunc(a);                // [0x82d492]
        angle_x = (float)(angle_x + speed_x);    // stored as float every step
        angle_y = (float)(angle_y + speed_y);
    }
    b = (float)b;  a = (float)a;                 // stored as floats once per wake
    if (fade_active) fade_step(counter - c0);    // 0x401ed0: DAC from the current palette, then n fade steps
    c0 = counter;  read_position();
} while (!escape && (POS > order || PAT > row)); // ends when order >= POS and row >= PAT
```

Nothing reads the music position, the sync value or a random generator: every animated value is a function of the
number of refreshes stepped.

## 5. Time bases

| variable | time base | formula | origin |
|---|---|---|---|
| angle_x, angle_y | counter | speed × n, n = refreshes since the effect's first RunBUMPEFEK started (DOOMSDAY's second Run continues; no refresh was lost between the two Runs in the capture) | 0 at InitBUMP |
| light offset lx, ly | counter | §6.1 | from the angles |
| b, a (wave phases) | counter | b = 7.2 × N, a = 9.6 × N, N = refreshes stepped in **all** RunBUMPEFEK loops so far in the program | 0 at program start (BSS); never reset |
| fade | counter | one fade step per refresh while a fade is active (core's fade) | the InitFADE line |

N per effect in the capture (same in part2 and full): SUOMI steps 1,067 times (b goes 0 → 7682.408, a → 10243.224);
ICE steps 1,072 times (it does not show the wave, but steps b and a: b → 15401.018, a → 20534.006); DOOMSDAY starts
with those values and adds 7.2 / 9.6 per refresh (1,108 more by its end: b = 23,378 at the last draw). The stored
values drift from 7.2 × N because b and a are rounded to float after every drawn frame (DOOMSDAY start: b is 0.22
above 7.2 × 2139 = 15400.80, a is 0.39 below 9.6 × 2139 = 20534.40); the drift is less than 0.05 pixel of wave
movement and can be ignored.

For the remake: SUOMI's wave time is the refreshes since its Run started; DOOMSDAY's is 2,139 + the refreshes since
its first Run started (or SUOMI's and ICE's Run durations in the remake's own timeline); the light angles restart at
0 in each effect. The offsets b, a of a refresh only change in the loop, so the frame shown at refresh k used the
state of the latest wake (≈ one refresh earlier than the time it appears; take the state at the refresh time).

**Frame rate** (100 M instructions per second): type 0 draws 0.69 frames per refresh (shown frames 1 refresh apart
411 / 443 times, 2 apart 327 / 332 times in SUOMI / DOOMSDAY), type 1 draws every refresh. The only frame-rate
dependent things are which refreshes get a new frame, the tearing (§9) and the float drift of b, a above.

## 6. The draw 0x4031a4

### 6.1 Light offset (types 0 and 1)

```
lx = (uint8)trunc(amp_x * sin(0.01745 * angle_x));    // [0x82d4dc]; 0.01745 is the double at 0x820476 (not pi/180)
ly = (uint8)trunc(amp_y * cos(0.01745 * angle_y));    // [0x82d4dd]; trunc toward zero (__CHP), two's complement byte
```

Periods: 2π / (0.01745 × speed) refreshes: 360.07 (speed 1), 720.13 (0.5), 480.09 (0.75). SUOMI: lx = 45 sin,
ly = 24 cos of the same angle (an ellipse once per 5.14 s); DOOMSDAY: lx = 40 sin(0.008725 n), ly = 24 cos(0.0130875 n);
ICE: lx = 100 sin(0.008725 n), ly = 100 cos(0.0130875 n) (Lissajous paths). At n = 0: lx = 0, ly = amp_y.

### 6.2 Type 0: water surface 0x40cf55, then shading 0x40ce79

```
// pass 1, all 64,000 pixels (p = y*320 + x)
i0 = (wave[p].w0 - off_b) & 2047;  i1 = (wave[p].w1 - off_a) & 2047;
level = (uint8)(ring[i0][0] + ring[i1][1] + add);      // 1..55
if (level > height[p]) surf[p] = { level, 0x00 };      // water
else                   surf[p] = { height[p], 0x80 };  // land (height >= level)

// pass 2, y = 2..197, x = 2..317, two pixels per iteration; uint8 arithmetic
u = u0(x) - surf[y][x+1].level + surf[y][x-1].level + lx;
v = v0(y) - surf[y+1][x].level + surf[y-1][x].level + ly;
screen[y][x] = light[v][u] + surf[y][x].flag;          // index; light[] is 0..127 for phong.pcx / phong2.pcx
```

So the slope is the central difference over two pixels, without scaling, with the neighbours' levels (water or land
height, whichever is on top there); the pixel itself is not used. Both differences, u0 / v0 and the light offset
add up modulo 256: **the light picture wraps around** (at the left edge u0 = 21; a slope of -47 there with
lx = -40 gives -66 → 190, for example). The wave index is modulo 2048 and the offsets modulo 65,536; both tables hold whole
periods, so the wrap is seamless. The wave: ring 0 has 9 periods per 2048 index units = per 256 pixels of distance
(wavelength 28.4 px) and moves outward from the top left corner at 0.9 px per refresh; ring 1 has a wavelength of
64 px and moves outward from (316, 196) at 1.2 px per refresh.

### 6.3 Type 1: 0x40cdc8

```
// y = 2..197, x = 2..317
screen[y][x] = light[(uint8)(normals[y][x].v + ly)][(uint8)(normals[y][x].u + lx)] + surf[y][x].flag;   // flag 0
```

The index is phong3.pcx's own index: a texture lookup, shown through phong3's palette.

### 6.4 Type 8 (not used)

0x40d0c9 computes the wave level alone (ring sums, no height picture) into a byte buffer; 0x40d017 shades it with
signed (movsx) differences as an offset into a 512 × 256 light buffer, in which 0x4029dc mode 1 has centred the light
picture. No ini selects it, and the light offset is not applied. Not needed for the remake.

## 7. Where it draws; what is overwritten

Straight into VGA memory (0xa0000), no back buffer, no retrace wait. Each Run first clears the whole screen to index
0; the draws then write rows 2..197, columns 2..317 only, so **a 2-pixel border stays index 0** all through the
effect (black in all three palettes; during the fades index 0 fades like the rest: white at SUOMI's start, white
at DOOMSDAY's end). Every pixel inside the border is rewritten by every draw.

## 8. Palettes and fades

- **SUOMI**: InitBUMP leaves the palette alone (flag 1). The script fades the water part's palette (pal/watback.pal)
  to white in 2 refreshes (2710..2712) while waiting for 5:01 — the water part's last frame turns white — then starts
  a fade white → pal/phong.pal over 50 refreshes at the Run start; RunBUMPEFEK steps it (1 step per refresh) — the
  DAC equals phong.pal from record 2778.
- **ICE**: InitPCX with flag 0 sets phong3.pcx's palette directly (the DAC changes in record 8104, two refreshes
  before the Run starts); no fade. After the Run, ClearSCR.
- **DOOMSDAY**: InitPCX flag 0 sets phong2.pcx's palette (= pal/phong2.pal; the DAC changes in record 11333). Run 1
  without fade; at 22:55 InitFADE pal/phong2.pal → pal/white.pal over 60 refreshes (12335..~12395), stepped by Run 2,
  which then keeps drawing on an all-white palette until 23:00 (12444). DeinitBUMP has no ClearSCR after it: the
  bump's last frame stays in VGA memory under the greetings' InitPCX / InitFADE white → greets.pal until PutPCXSYNC
  draws the greetings (notes-core.md).

The pixel's colour is the palette entry of its index; for type 0 the index is L + 128 × land with L = 0..127 from
the light picture, so each palette half is a 128-step ramp: water ramp (indices 0..127) and land ramp
(128..255).

## 9. Visible quirks and artefacts (what the remake keeps or drops)

- **Tearing** (drop): the draw writes the visible screen while it is scanned; in the capture 40 % of SUOMI's and
  DOOMSDAY's records and 89 % of ICE's show the new frame down to some row and the previous one below (ICE: always
  row 45). Only complete frames belong to the effect. (full.frames shows the same frames; its records differ from
  part2.frames only in where the tear is, by a few pixels.)
- **Loading glitch** (drop; depends on load time): between InitBUMP setting the palette and the Run clearing the
  screen, the previous part's last frame shows in the new palette: 2 refreshes for ICE (records 8104, 8105: the
  landscape in phong3's colours), 1 for DOOMSDAY (11333: the second 3D scene in phong2's colours).
- **Black frames at a Run start** (drop): the Run clears the screen and the first pass-2 rows appear about a refresh
  later: records 2724..2725 (under white), 11334 (black with a strip of rows), and **12335, a whole black refresh**
  between DOOMSDAY's two Runs (the screen is cleared again; the palette is still phong2.pal, index 0 black).
- **The 2-pixel black border** (keep: it is in every frame).
- **Light picture wrap-around** (keep): the coordinate wraps modulo 256 (sample with repeat).
- **The land/water switch** at the coast and the slope across it, including the bright/dark rim where the levels
  jump (keep: it is the coastline's look).

## 10. Pictures: redraw or not

| picture | kind | read how | redraw? |
|---|---|---|---|
| bumppic1.pcx, bumppic2.pcx, bumppic3.pcx | drawn / rendered height maps (grey) | through their indices as heights (differences) | **no**: computed from the original indices (interpolated, §11) |
| phong.pcx, phong2.pcx | generated-looking light spot (≈ 127 - r) | through their indices: brightness level, + 128 for land, then the palette | **no**: index data |
| phong3.pcx | drawn/rendered colour texture (rock/lava), tileable | as colours: index → its own palette, no arithmetic on the index | can be shown as a true-colour texture and may get a 4× upscale (tileable, sampled with repeat); it is mapped through the bump offsets, not shown as it is |

## 11. Rendering shortcuts and how to evaluate them exactly

| original | approximates | remake |
|---|---|---|
| 8-bit height picture, read per pixel | a smooth relief | interpolate the heights (bicubic or bilinear) at continuous positions in original-pixel units |
| slope = h(x-1) - h(x+1), h(y-1) - h(y+1) of the combined water/land field | -2 ∂H/∂x, -2 ∂H/∂y | evaluate H = max(h, level) at (x ± 1, y) and (x, y ± 1) in original-pixel units (keeps the original's values at its pixel centres, including the rim at the coast); u, v as float, wrapped mod 256 |
| ring table (11 + trunc(11 sin ...), 16 + trunc(16 cos ...)), index & 2047 | level = 11 + 11 sin(2π·9·(8r0 - b)/2048) + 16 + 16 cos(2π·4·(8r1 - a)/2048) + 1 | continuous formula with amp, n from bumpprm1.val's header (or linear interpolation of the table), r0, r1 the float distances from (0, 0) and (316, 196) (§3.4), b = 7.2 × refreshes, continuous |
| integer offsets trunc(b), trunc(a) | continuous phase | b, a as floats from continuous time |
| trunc(amp·sin), trunc(amp·cos) light offsets | smooth light path | lx, ly as floats |
| bumptab.val (integer steps 0/1) | a smooth function of x, y | linear interpolation of the 320 / 200 values (or the fit in §3.5 from the file's values) |
| land = level ≤ h, per pixel | the coastline | per sample with the continuous values; anti-alias the land/water boundary |
| light[v][u] nearest texel | the light spot | bilinear read of phong.pcx / phong2.pcx with wrap (or the cone 127 - r; the picture's ±1 irregularities are invisible) |
| palette index = L (+128) | a 128-step colour ramp | colour = lerp(pal[base + floor(L)], pal[base + floor(L) + 1], frac(L)) within the half (base 0 water, 128 land), from the faded palette |
| phong3 index → palette | a colour texture | bilinear read of the true-colour texture with repeat |

Grain that must stay: the fine roughness of the height pictures (Finland's plateau texture, bumppic3's blobs) is part
of the picture; interpolating the heights keeps it at its original scale (one feature per original pixel). There is
no dither or noise generated by the effect itself.

`models/bump/bump.py` has `frame_hd()`, a sketch of these suggestions; `runs/hd_*.png` puts it next to the
original frame (§12) and looks the same at 4× apart from smoothness.

## 12. Python model

`models/bump/bump.py`: `Bump(ini, pcx_flag)` builds the init buffers, `State.step(n, fx)` the stepping loop,
`Bump.frame(state)` the 320×200 indices (bit-exact: see the checks at the top). `frame_hd()` is the continuous
sketch.

## 13. Not determined

- Whether the light spot phong.pcx was generated or drawn with a gradient tool (it is ≈ 127 - r with irregular ±1
  steps, not symmetric); bumptab.val's exact generator (§3.5 is a fit within ±1).
- phong.pcx's own palette is never loaded (flag 1); it differs from pal/phong.pal. Not needed.
- The name "ICE BUMP" in the script does not match what is drawn (warm lava-rock colours); nothing in the code refers
  to ice.
