# Scope "bitmap": the table-driven bitmap effects (part I's "1x1 Efekti" #1..#3 and the 2 × 2, part II's efek4 / efek5)

Routines: InitBITMAPEFEK 0x400ae4 (with 0x4007fc, which builds the coordinate words), RunBITMAPEFEK 0x401270 (with
0x401024, which reads the `.prm` speeds), DeinitBITMAPEFEK 0x400ff0, ClearEFEKPARAMS 0x401588 (→ 0x40c284), the step
0x40c22c and the self-modifying assembler drawers 0x40c3ad, 0x40c455, 0x40c543, 0x40c626, 0x40c74f, 0x40c86d,
0x40cab5, 0x40cb33, 0x40cbe7 (they patch their own immediates and displacements at entry). Unused leftovers in the
range: 0x4004e0 (stores 4 / 180 · 360 / 256 into [0x825ca0], read nowhere), 0x400506 (loads TAUSTA), 0x4005e2
(loads the pictures and the palette), 0x4008d0 (the draw dispatch of RunBITMAPEFEK as a function), 0x4011e4 (the step
loop as a function), the drawers 0x40c2b9 (2 × 2, three planes combined by nibbles through a 4-pixel table) and
0x40c9d5 (1 × 1 with a displacement table): none has a caller. Shared helpers by what goes in and out
(notes-core.md): 0x40241c file by name → data, 0x401ed0 the fade step (writes the DAC, then steps an active fade),
0x400104 the music position, Watcom's sscanf 0x4112ce, atof 0x411301, malloc 0x4112f0, free 0x4112f8, and __CHP
0x41130c (sets the FPU's rounding control to **truncate** for the `fistp` that follows).

Clock names as in NOTES.md / notes-core.md: **refresh** = one screen refresh (70.086 Hz) = one record of the
`.frames` capture; **counter** F = the frame counter [0x825c5c] (+1 per refresh; record − 4 in part I of `full`,
record − 5 in part II of `full`, record − 4 in `part2`). Records are `full.frames` records unless noted
(`part2` = `full` − 14,444 for the part II lines).

**Verified** (scripts in `models/bitmap/`, README there):
- a Python model of Init, the step and the drawers (`bitmap.py`), driven only by the files, the script lines and the
  frame counter, against **every record of every RunBITMAPEFEK line**: 8,362 of 8,362 records of `full` and 2,146
  of 2,146 of `part2` are exact in all 64,000 VGA bytes (`verify.py`). Each record is the draw for its counter in
  its top rows over the previous draw below (the drawers write straight into VGA memory and are part way down the
  screen when the record is taken, 10.);
- the offsets the model steps against the eight offset words in every snapshot (`snapoffs.py`): exact;
- the DAC in every Run record: the effect's palette in all records outside the three fades, and the fades as
  notes-core.md describes them (`palcheck.py`);
- the tables (`fittabs.py`): **all eighteen `.1x1` tables but one regenerate byte for byte from a formula** (6.);
  `tab14.1x1` matches 63,659 of 64,000 bytes, the rest ±1 on two lines; the three `.2x2` tables match smooth formulas
  within ±1..2 (60..68 % of the bytes exact; they carry noise, 6.4).

---------------------------------------------------------------------------------------------------------------

## Summary for the remake

- **What it is.** Up to four *planes*. Each plane is a 256 × 256 texture (one byte per texel) read through two
  per-pixel tables: the texture column u(x, y) and row v(x, y), plus the plane's scroll offset (Ox, Oy). The tables
  are polar maps around a point (distance, angle, distance or angle with sine wobbles), at screen resolution
  (1 × 1) or one entry per 2 × 2 block (2 × 2). The planes are combined into the pixel's index:
  - part I's "1x1 Efekti" (one plane): index = texel; a photo of a woman (`nainen*.raw`) is drawn over it, its index
    0 transparent;
  - part II (two planes): index = DTABLE[texel2][texel1], a colour table that is a **dithered 50 % blend of the two
    textures' colours** (7.3): the remake draws the two textures half transparent over each other;
  - part I's 2 × 2 (three planes): index = texel1 + texel2 + texel3 (mod 256) into a ramp palette (black → purple →
    red → yellow → white): additive index arithmetic.
- **Tables are formulas** (6.). The remake evaluates them per output pixel in float (no truncation), at continuous
  original-pixel coordinates, and samples the textures with filtering and wrap-around.
- **Time base: the frame counter.** While a RunBITMAPEFEK line runs, each plane's offset moves by its `.prm` speed
  (1/256 texel units) once per refresh (70.086 Hz), however many frames are drawn: O(F) = O(t0) + S · (F − t0).
  The offsets are never reset between runs or sections (only ClearEFEKPARAMS, before the 2 × 2, zeroes them), so
  every section continues where the previous run left the offsets; part II (full demo) starts with the 2 × 2's final
  offsets, part II alone (`vivid.exe 2`) with zero (4.3).
- **Where it draws:** straight into VGA memory (0xa0000), no back buffer, no retrace wait: the whole 320 × 200 frame
  is overwritten each frame (both the 1 × 1 and the 2 × 2 drawers write every pixel). The capture's records show
  the tearing line at row ~28 (1 × 1) / row ~90 (2 × 2) (10.). One frame per refresh in the capture.
- **Palettes:** Init sets the whole palette from the files (texture palette, the photo's 64 colours at 192..255, or
  DTABLE's palette) and writes the DAC once. Three fades run inside the sections (to white, to white, to black);
  nothing else touches the palette (8.).
- **Drop**: integer-texel offsets (only the offsets' high bytes are used), the table truncation, the carry from the
  column into the row in the 16-bit address add (5.2), the 2 × 2 blocks, DTABLE's dither, the tearing. **Keep**: the
  y-speed quirk of RunBITMAPEFEK (Oy moves by S2 − 1 when S1 < 0, 4.2), the tile mirroring of `2tab4` / `4tab4`
  (its 1-pixel asymmetry included), the centres and constants of the tables.

---------------------------------------------------------------------------------------------------------------

## 1. What is drawn, and when

Script semantics (notes-core.md): a `RunBITMAPEFEK` line starts when the previous line's wait ends and runs until
the music reaches its own POS:PAT (order ≥ POS and row ≥ PAT). Init / Deinit / ClearSCR / ClearEFEKPARAMS lines run
at once.

| section | Init (TYPE PLANES BPIC) | texture / overlay | tables (column u / row v) | Run lines: prm → until | records |
|---|---|---|---|---|---|
| part I, 1x1 #1 a | `efek/efek1.ini` (1 1 1), after ClearSCR | `plo2.raw` / photo `nainen1.raw` (left) | `tab10` / `tab11` | 1efek1 → 24:00, 1efek2 → 25:00, 1efek3 → 26:00, 1efek4 → 27:00 | 5992..6881 |
| #1 b | `efek/4efek1.ini` (1 1 1) | same | `4tab10` / `4tab11` (2 × 2 tiles) | 1efek5 → 28:00, 1efek6 → 29:00 | 6883..7326 |
| #1 c | `efek/16efek1.ini` (1 1 1) | same | `16tab10` / `16tab11` (4 × 4 tiles) | 1efek7 → 30:00, 1efek8 → 30:40, InitFADE plo2→white 80, 1efek8 → 30:60 | 7328..7757 |
| part I, 1x1 #2 a | `efek/efek2.ini` (1 1 1), after ClearSCR | `plo6.raw` / photo `nainen2.raw` (right) | `tab2` / `tab4` | 1 → 36:00, 2 → 37:00, 3 → 38:00, 4 → 39:00 | 8660..9552 |
| #2 b | `efek/4efek2.ini` | same | `4tab2` / `4tab4` (2 × 2 tiles, mirrored) | 5 → 40:00, 6 → 41:00 | 9554..9997 |
| #2 c | `efek/16efek2.ini` | same | `16tab2` / `16tab4` (4 × 4 tiles) | 7 → 42:00, 8 → 42:32, InitFADE plo6→white 60, 8 → 43:00 | 9999..10442 |
| part I, 1x1 #3 a | `efek/efek3.ini`, after ClearSCR | `plo5.raw` / photo `nainen3.raw` (left) | `tab2` / `tab4` | 1 → 48:00, 2 → 49:00, 3 → 50:00, 4 → 51:00 | 11331..12223 |
| #3 b | `efek/2efek3.ini` | same | `2tab2` / `2tab4` (2 tiles stacked, the lower mirrored) | 5 → 52:00, 6 → 53:00 | 12225..12668 |
| #3 c | `efek/4efek3.ini` | same | `4tab2` / `4tab4` | 7 → 54:00, 8 → 54:32, InitFADE plo5→black 60, 8 → 55:00 | 12670..13114 |
| part I, 2 × 2 | ClearSCR, ClearEFEKPARAMS, `efek2x2/efek2.ini` (2 3 0) | planes `2x2_3.raw`, `2x2_4.raw`, `2x2_4.raw` | `tab4.2x2` / `tab5.2x2`; `tab6.2x2` / `tab6.2x2` (×2) | `efek2x2/2.prm` → 59:00 | 13116..14004 |
| part II, efek4 | ClearSCR, `efek/efek4.ini` (1 2 0) | `plo3.raw` + `plo6.raw` through `dtable.raw` | `tab13_1` / `tab13_2`; `tab13_3` / `tab13_4` | 4efek1 → 8:00 | 18238..18774 (part2 3794..4330) |
| efek5 | ClearSCR, `efek/efek5.ini` (1 2 0) | `plo2.raw` + `plo5.raw` through `dtable2.raw` | `tab13_1` / `tab14`; `tab13_3` / `tab13_4` | 4efek3 → 9:00 | 18777..19312 |
| efek4 | ClearSCR, `efek/efek4.ini` | as above | as above | 4efek2 → 10:00 | 19315..19851 |
| efek5 | `efek/efek5.ini` (no Deinit before it), ClearSCR | as above | as above | 4efek2 → 11:00 | 19854..20389 |

A DeinitBITMAPEFEK follows each section (the last of part II after 11:00). Directory prefixes: part I's files are in
archive 1 (`efek/`, `efek2x2/`, `tabs/`, `pics/`, `pal/`), part II's in archive 2.

On screen:
- **1x1 #1** (`tab10` / `tab11`): a ten-pointed star / kaleidoscope of the greenish caustic texture `plo2` around the
  screen centre, flowing outwards and turning as the offsets scroll; then the same in 2 × 2 and 4 × 4 tiles; the
  photo stands at the left over everything. Fades to white over the last part.
- **1x1 #2 / #3** (`tab2` / `tab4`): the texture (purple embossed relief `plo6`, then the yellow lightning `plo5`)
  wrapped around the centre (one texture height per turn) and twisted by a radial sine: a swirl that scrolls
  outward / turns; then in tiles: #2 in 2 × 2 tiles whose right and lower copies are mirrored (two "8"-shaped
  pairs) and 4 × 4 tiles; #3 in two stacked copies (the lower mirrored) and in 2 × 2 mirrored tiles. Photo right
  (#2) / left (#3). #2 fades to white, #3 to black.
- **2 × 2**: a floor in perspective covered with a lattice of glowing dots (`2x2_3`), lit by a swirling glow
  (`2x2_4` sampled through the swirl table, counted twice), in black / purple / red / yellow / white; 2 × 2 pixel
  blocks.
- **Part II**: two swirling patterns of rings and tongues (both coordinates are angles about two different points,
  with a fast radial ripple) half transparent over each other: fire (`plo3`) with purple relief (`plo6`), then
  caustic (`plo2`) with lightning (`plo5`) where the second plane's row is a distance map with 100 ripples.

## 2. Files

| file | size | format, use |
|---|---|---|
| `efek/*.ini`, `efek2x2/efek2.ini` (archive 1), `efek/efek4.ini`, `efek5.ini` (archive 2) | 111..442 | text: `TYPE PLANES BPIC 0` / `BPICNAME DTABNAME x x` / `P1 P2 P3 P4` / `T1 .. T8` (comment lines after them are ignored); `0` = no file |
| `efek/*.prm`, `efek2x2/*.prm` | 27..137 | text: eight decimal speeds (texels per refresh) S1..S8; the rest ignored |
| `pics/plo2.raw`, `plo5.raw`, `plo6.raw` (archive 1); `plo2`, `plo3`, `plo5`, `plo6.raw` (archive 2; plo2 / 5 / 6 are the same files) | 66,336 | Image Alchemy raw ("mhwanh", 32-byte header, 256 × 256, 768-byte 8-bit palette, then 65,536 indices); indices 0..127 only |
| `pics/2x2_3.raw`, `2x2_4.raw` | 66,336 | the same format, indices 0..31; one palette (identical in both files, = `pal/2x2_4.pal`) |
| `pics/nainen1.raw`, `nainen2.raw`, `nainen3.raw` | 64,800 | raw, 320 × 200, palette entries 0..63 used; indices 0..63 (one pixel of `nainen3` is 65); 0 = transparent (76..80 % of the pixels) |
| `pics/dtable.raw`, `dtable2.raw` (archive 2) | 33,568 | raw, 256 × 128: DTABLE (7.3) and its palette (= `pal/efek4.pal`, `efek5.pal`) |
| `tabs/*.1x1` | 64,000 | one byte per screen pixel, row-major 320 × 200 |
| `tabs/*.2x2` | 16,000 | one byte per 2 × 2 block, row-major 160 × 100 |
| `pal/plo2.pal`, `plo5.pal`, `plo6.pal` | 768 | the effects' palettes as Init builds them (texture palette with the photo's 64 colours at 192..255: identical, checked); the fades start from them |

Unused by the script: `efek2x2/1.prm`, `efek/4efek4..8.prm` (archive 2), `pics/2x2_1.raw`, `pics/2x2_2.raw`,
`pal/2x2_4.pal`, `pal/efek4.pal`, `pal/efek5.pal`.

The `.ini` names that the code reads (sscanf "%d %d %d %d  %s %s %s %s  %s %s %s %s  %s %s %s %s %s %s %s "): four
ints, then 15 strings: BPICNAME [0x8264ac], DTABNAME [0x8264ec], two unused [0x82652c], [0x82656c], P1..P4
[0x8265ac + 0x40 k], T1..T7 [0x8266ac + 0x40 k]; T8 [0x82686c] is never read (only a 2 × 2 with four planes would
use it). Ints: TYPE [0x825c7c], PLANES [0x825c80], BPIC [0x825c84], [0x825c88] unused.

## 3. InitBITMAPEFEK 0x400ae4 (script: `InitBITMAPEFEK <ini> 0`)

```c
// buffers (malloc; never freed if Init runs twice without Deinit, as part II's 4th section does)
u8  *DTABLE;   // [0x825c9c] 0x8000   colour table of the two-plane 1 x 1 drawer
u8  *BUFF;     // [0x825c8c] 64000    allocated and freed, never used
u8  *PICRGB;   // [0x825c94] 0x40000  planes 1..4: four 256 x 256 textures, plane p at PICRGB + 0x10000 p
u8  *TAUSTA;   // [0x825c98] 64000    the background (overlay) picture, 320 x 200
u16 *PREC;     // [0x825c90] 0x3e800  per plane one word per pixel (1 x 1) or per block (2 x 2): row << 8 | column

void InitBITMAPEFEK(const char *ini, int keepdac)
{
    sscanf(FindFile(ini), ...);                          // 2.
    DTABLE = malloc(0x8000); BUFF = malloc(64000); PICRGB = malloc(0x40000);
    TAUSTA = malloc(64000);  PREC = malloc(0x3e800);
    if (TYPE == 1) {                                     // 1 x 1: 64,000 words per plane
        MakePrec(T1, T2, 0);
        if (PLANES >= 2) MakePrec(T3, T4, 64000);
    }
    if (TYPE == 2) {                                     // 2 x 2: 16,000 words per plane
        MakePrec(T1, T2, 0);
        if (PLANES >= 2) MakePrec(T3, T4, 16000);
        if (PLANES >= 3) MakePrec(T5, T6, 32000);
        if (PLANES == 4) MakePrec(T7, T8, 48000);
    }
    u8 *raw = FindFile(P1);  pal = raw + 32;  memcpy(PICRGB, raw + 800, 0x10000);
    if (TYPE == 1 && PLANES == 2) { raw = FindFile(DTABNAME); pal = raw + 32; memcpy(DTABLE, raw + 800, 0x8000); }
    for (p = 2; p <= PLANES; p++) memcpy(PICRGB + 0x10000 (p - 1), FindFile(Pp) + 800, 0x10000);
    for (i = 0; i < 256; i++) palette[i] = (pal[3i + c] >> 2) << 16;      // 16.16 at 0x826abc, 6-bit values
    if (BPIC == 1) {
        raw = FindFile(BPICNAME);
        memcpy(TAUSTA, raw + 800, 64000);
        for (each little-endian dword d of TAUSTA) d -= 0x3f3f3f40;        // 0x400f79
        for (i = 0; i < 64; i++) palette[192 + i] = (raw[32 + 3i + c] >> 2) << 16;
    }
    if (keepdac == 0) FadeStep(1);   // 0x401ed0: the DAC := palette (an active fade would also step once; none is)
}

void MakePrec(name lo, name hi, int first)       // 0x4007fc
{
    n = TYPE == 1 ? 64000 : 16000;
    for (i = 0; i < n; i++) PREC[first + i] = lo[i] | hi[i] << 8;   // lo = texture column, hi = texture row
}
```

The dword subtraction maps indices 0..63 to 192..255 (index + 0xc0 mod 256): byte 0 loses 0x40, each higher byte
loses 0x3f plus the borrow from the byte below, which every byte below 0x40 produces; so every byte loses 0x40 and
index 0 becomes 0xc0, the drawers' "transparent" value.
(The one pixel 65 of `nainen3` at (37, 36) becomes index 1, and the byte after it 245 instead of 244; harmless.)

TYPE 3 loads no pictures. The "PICRGB" texture planes hold indices, not RGB.

DeinitBITMAPEFEK 0x400ff0: free(TAUSTA), free(BUFF), free(PICRGB), free(PREC), free(DTABLE).

## 4. RunBITMAPEFEK 0x401270 (script: `POS PAT RunBITMAPEFEK <prm>`) and the offsets

### 4.1 The loop

```c
s16 S[8];                    // [0x825c6c..0x825c7a]
s16 O[8];                    // [0x40c21c..0x40c22a] in the code segment: Ox1 Oy1 Ox2 Oy2 Ox3 Oy3 Ox4 Oy4,
                             // 8.8 fixed point texels; 0 in the image, zeroed only by ClearEFEKPARAMS

void ReadPrm(const char *prm)        // 0x401024
{
    char s[8][32];
    sscanf(FindFile(prm), "%s %s %s %s %s %s %s %s", s[0], ..., s[7]);
    for (i = 0; i < 8; i++) S[i] = (s16)(int)trunc(atof(s[i]) * 256.0);   // __CHP: truncation toward zero
}

void RunBITMAPEFEK(u16 pos, u16 pat, const char *prm)
{
    ReadPrm(prm);
    int t = frames;                                  // [0x825c5c]
    do {
        Draw();                                      // 5. (by TYPE, PLANES, BPIC)
        while (frames == t) ;                        // busy wait for the next refresh
        int n = frames - t;
        for (i = 0; i < n; i++)
            Step((S[1] << 16) + S[0], (S[3] << 16) + S[2], (S[5] << 16) + S[4], (S[7] << 16) + S[6]);
        if (fade_active) FadeStep(frames - t);       // 0x401ed0: DAC := palette, then frames - t fade steps
        t = frames;
        GetPosition();                               // 0x400104: order [0x825c48], row [0x825c50]
        if (quit) break;                             // [0x8232ee] (set elsewhere; never in the capture)
    } while (pos > order || pat > row);              // until order >= POS and row >= PAT
}

void Step(s32 a, s32 b, s32 c, s32 d)                // 0x40c22c (d is never used)
{
    O[0] += (u16)a;  O[1] += (u16)(a >> 16);
    O[2] += (u16)b;  O[3] += (u16)(b >> 16);
    O[4] += (u16)c;  O[5] += (u16)(c >> 16);
    O[6] += (u16)c;  O[7] += (u16)(c >> 16);         // plane 4 gets plane 3's speeds (a bug; no effect here)
}
```

### 4.2 Time base and the y quirk

- **Frame counter.** The offsets move once per refresh (the loop steps as many times as refreshes have passed since
  the last draw), so the motion does not depend on the frame rate. Each drawn frame uses the offsets of the counter
  at which it starts. With t0 = the counter when the Run line started:
  `O(F) = O(t0) + n · (Sx, Sy')` for F = t0 + n, all in 16-bit wrap-around (texels · 256),
  where **Sy' = S[2k+1] − 1 if S[2k] < 0, else S[2k+1]**: the caller packs each pair as (S[2k+1] << 16) +
  S[2k] with S[2k] sign-extended, so a negative x speed borrows 1 from the y speed. Measured: 1efek1 (S = −256,
  −179) moves Oy1 by −180 per refresh (snapshot `f6214`: 223 steps → Oy1 = −40,140 mod 65,536 = 25,396). The remake
  keeps it: it is a real difference in speed (1/256 texel per refresh, 0.27 texels per second).
- Speeds of the used `.prm` files (S1..S6; S7, S8 are 0 everywhere and never used), in texels / 256 per refresh:

  | prm | S1 Ox1 | S2 Oy1 | S3 Ox2 | S4 Oy2 | S5, S6 |
  |---|---|---|---|---|---|
  | efek/1efek1 / 1efek2 / 1efek3 / 1efek4 | −256 / −256 / −384 / −384 | −179 / 204 / −256 / 256 | 128 / 128 / 76 / 128 | −128 / 128 / −76 / 128 | 0 |
  | 1efek5 / 1efek6 / 1efek7 / 1efek8 | −384 / 384 / −384 / 384 | 256 / −256 / 256 / −256 | −128 / 128 / −128 / 128 | 128 / −128 / 128 / −128 | 0 |
  | efek/1 / 2 / 3 / 4 | −153 / 204 / −256 / −256 | −153 / −179 / −179 / 204 | −128 / −128 / 128 / 128 | 128 / −128 / −128 / 128 | 0 |
  | 5 / 6 / 7 / 8 | −384 / −384 / −460 / 384 | 256 / −256 / 256 / −409 | −128 / 128 / −128 / −128 | 128 / −128 / 128 / −128 | 0 |
  | efek2x2/2 | −102 | −102 | 153 | 153 | 153, 153 |
  | efek/4efek1 / 4efek2 / 4efek3 (part II) | −179 / −230 / −179 | 102 / 204 / −204 | 128 / 256 / 153 | 153 / −358 / −153 | 0 |

  (Part I's 1 × 1 drawers use plane 1 only; S3, S4 still step Ox2, Oy2.)
- The capture draws **one frame per refresh** in every bitmap section (each record shows a new draw), so the
  catch-up loop runs once per frame there.

### 4.3 Run boundaries and the carried offsets

The loop leaves right after a step, so a run's last step is at the counter value of the refresh in which the
position was reached; the next line starts in that same refresh. A Run that follows a Run starts at that counter
(no refresh lost). A Run that follows Deinit / ClearSCR / Init starts at the counter when Init has finished: in part
I the Inits finish within the refresh (no step lost), in part II each Init (two planes of PREC) runs past the next
increment and **one refresh is lost** (the offsets stand still for one refresh). Measured (`verify.py`), counter
values:

| run | t0 → end | run | t0 → end | run | t0 → end |
|---|---|---|---|---|---|
| 1efek1 | 5987 → 6210 | 1 | 8655 → 8881 | 1 | 11326 → 11552 |
| 1efek2 | 6210 → 6432 | 2 | 8881 → 9104 | 2 | 11552 → 11775 |
| 1efek3 | 6432 → 6655 | 3 | 9104 → 9326 | 3 | 11775 → 11998 |
| 1efek4 | 6655 → 6878 | 4 | 9326 → 9549 | 4 | 11998 → 12220 |
| 1efek5 | 6878 → 7100 | 5 | 9549 → 9771 | 5 | 12220 → 12443 |
| 1efek6 | 7100 → 7323 | 6 | 9771 → 9994 | 6 | 12443 → 12665 |
| 1efek7 | 7323 → 7545 | 7 | 9994 → 10217 | 7 | 12665 → 12888 |
| 1efek8 | 7545 → 7684 | 8 | 10217 → 10328 | 8 | 12888 → 12999 |
| 1efek8 | 7684 → 7754 | 8 | 10328 → 10439 | 8 | 12999 → 13111 |
| 2x2 2 | 13111 → 14001 (after ClearEFEKPARAMS) | 4efek1 | 18232 → 18770 | 4efek3 | 18771 → 19308 |
| 4efek2 | 19309 → 19847 | 4efek2 | 19848 → 20385 | part2: | 3789 → 4327, 4328 → 4865, 4866 → 5404, 5405 → 5942 |

So the offsets are a sum over all runs so far: each section begins with the offsets the previous section ended
with (sections 1, 2, 3 of part I chain through the gaps of ~890 refreshes with the "synkki" pictures, in which they
stand still). In the **full demo part II starts with the 2 × 2's final offsets** (890 steps of efek2x2/2.prm:
Ox1 = −25,244, Oy1 = −26,134, Ox2 = Oy2 = 5,098); `vivid.exe 2` starts part II with zeros, so `part2.*` shows
different texture positions than `full.*` in part II's bitmap sections (both reproduce exactly). For the remake:
take each run's t0 / end from the music positions (end = the refresh in which the position is reached, notes-core.md;
t0 = the previous run's end, + 1 refresh after a part II Init) and accumulate. Between refreshes, interpolate:
O(t) = O(t0) + S · (R(t) − t0) with the fractional refresh R(t).

ClearEFEKPARAMS 0x401588 → 0x40c284: O[0..7] = 0.

## 5. The drawers

### 5.1 Dispatch (in RunBITMAPEFEK, also 0x4008d0)

| TYPE | PLANES | BPIC | drawer | used |
|---|---|---|---|---|
| 1 | 1 | 0 | 0x40cab5 (PREC, PICRGB, 0, 200) | no |
| 1 | 1 | 1 | 0x40cb33 (PREC, PICRGB, 0, TAUSTA, 200) | part I 1x1 #1..#3 |
| 1 | 2 | 0 | 0x40cbe7 (PREC, PICRGB, DTABLE, 200) | part II |
| 2 | 2 | 0 / 1 | 0x40c3ad / 0x40c455 (…, 100) | no |
| 2 | 3 | 0 / 1 | 0x40c543 / 0x40c626 (…, 100) | 0x40c543: the 2 × 2 |
| 2 | 4 | 0 / 1 | 0x40c74f / 0x40c86d (…, 100) | no |

Any other combination draws nothing. All are `__cdecl`, write to 0xa0000 directly, and patch their inner loop's
immediates first (texture plane addresses, the 16-bit offset words, TAUSTA, DTABLE).

### 5.2 How a texel is addressed

For plane k (0-based) the drawer adds one 16-bit word to the table word:

```c
u16 add_k = (O[2k+1] & 0xff00) | ((u16)O[2k] >> 8);            // whole texels: the high bytes of Oy, Ox
u8  texel(k, i) = PICRGB[0x10000 k + (u16)(PREC_k[i] + add_k)];  // 16-bit add, wraps at 65,536
```

PREC_k[i] = v << 8 | u (u = texture column from the odd table T(2k+1), v = row from T(2k+2)); the texture byte at
row r, column c is at r · 256 + c. So: column = (u + Ox_int) mod 256, row = (v + Oy_int + carry) mod 256 with
**carry = 1 when u + Ox_int ≥ 256**. Exception: the 2 × 2 four-plane drawers read plane 4 from plane 3's texture
(PICRGB + 0x20000, a bug; unused).

### 5.3 The pixel's index

```c
// 0x40cb33: 1 x 1, one plane, overlay (two pixels per dword, both with the same offset)
for (i = 0; i < 64000; i++) {
    u8 e = texel(0, i);                               // PREC_0 = PREC
    VGA[i] = TAUSTA[i] == 0xc0 ? e : TAUSTA[i];       // the photo except where it is index 0
}
// 0x40cab5: the same without TAUSTA: VGA[i] = texel(0, i)

// 0x40cbe7: 1 x 1, two planes (PREC_1 = PREC + 64000 words, plane 1 texture at PICRGB + 0x10000)
for (i = 0; i < 64000; i++)
    VGA[i] = DTABLE[texel(1, i) << 8 | texel(0, i)];  // texel(1, i) < 128 in plo5 / plo6, so inside the 32 KB

// 0x40c543: 2 x 2, three planes (PREC_k = PREC + 16000 k words)
for (by = 0; by < 100; by++) for (bx = 0; bx < 160; bx++) {
    j = by * 160 + bx;
    u8 c = texel(0, j) + texel(1, j) + texel(2, j);    // 8-bit sum (max 31 + 31 + 31 = 93 here: no wrap)
    VGA[(2 by) * 320 + 2 bx] = VGA[.. + 1] = VGA[(2 by + 1) * 320 + 2 bx] = VGA[.. + 1] = c;   // one 2 x 2 block
}
// 0x40c3ad / 0x40c74f: the same with 2 / 4 planes; the BPIC variants 0x40c455 / 0x40c626 / 0x40c86d test each of
// the block's four TAUSTA bytes against 0xc0 like 0x40cb33.
```

The 2 × 2 mode is plain block duplication: one table entry and one index per 2 × 2 block, no interpolation.

In the 2 × 2 section planes 2 and 3 are the same texture (`2x2_4`) with the same table (`tab6.2x2` for both column
and row) and the same offsets (S3..S6 all 153, so Ox2 = Oy2 = Ox3 = Oy3 always): index = P1 + 2 · P2. With column =
row = tab6 + o, plane 2 reads **the diagonal of `2x2_4`** (texel (s, s), s = tab6 + o, and (s, s + 1) after a
carry): a 1-D profile of the noise, swept around the swirl centre.

## 6. The tables

Notation for the `.1x1` tables: pixel (x, y), dx = x − cx, dy = y − cy, r = sqrt(dx² + dy²), a = atan2(dy, dx) in
radians (−π..π, y down, a = 0 to the right, +π/2 down). The generator stored `(int)v & 255` (C conversion:
truncation toward zero, then the low byte, so negative values wrap: the white "petals" of `tab11`). `fittabs.py`
regenerates them; "exact" = all 64,000 bytes. (Precision detail, only for byte-exact regeneration: r and a rounded to
float32, the rest in double, except `tab13` whose angle is double.)

### 6.1 The base maps

| table | centre | v (texels) | used as | bytes |
|---|---|---|---|---|
| `tab2.1x1` | (160, 100) | r | column, sections #2, #3 | exact |
| `tab4.1x1` | (160, 100) | a · 128/π + 41 · sin(r · π/128) | row, sections #2, #3 | exact |
| `tab10.1x1` | (160, 100) | r + 4 · sin(40 a) | column, section #1 | exact |
| `tab11.1x1` | (160, 100) | r + 52 · sin(10 a) | row, section #1 | exact |
| `tab13_1.1x1` = B | (220, 130) | a · 128/π − 15 · sin(r · π/12.8) | part II column plane 1 | exact |
| `tab14.1x1` | (179, 40) | r + 8 · sin(100 a) | part II efek5 row plane 1 | 63,659 (the rest ±1 on two lines, 12.) |

The angle maps run 256 texels per turn (the texture wraps exactly once around the centre); the distance maps reach
188 at the corners (no wrap without the offset).

### 6.2 Derived 1 × 1 tables (all exact)

- **Mirror copies of B** (part II): `tab13_2`(x, y) = B(319 − x, 199 − y) (centre (99, 69)), `tab13_3`(x, y) =
  B(319 − x, y) (centre (99, 130)), `tab13_4`(x, y) = B(x, 199 − y) (centre (220, 69)).
- **Tiled copies** (part I b / c parts): table(x, y) = base(sx · xl, sy · yl) with the tile size tw = 320 / sx,
  th = 200 / sy, xl = x mod tw, yl = y mod th, i.e. dx = sx · xl − 160, dy = sy · yl − 100:

  | tables | sx × sy | mirrored tiles |
  |---|---|---|
  | `4tab2`, `4tab10`, `4tab11` | 2 × 2 | none |
  | `4tab4` | 2 × 2 | the right column: xl → 159 − xl; the bottom row: yl → 99 − yl |
  | `16tab2`, `16tab4`, `16tab10`, `16tab11` | 4 × 4 | none |
  | `2tab2` | 1 × 2 | none |
  | `2tab4` | 1 × 2 | the bottom tile: yl → 99 − yl |

  The mirrored copies are pixel mirrors of the tile (about xl = 79.5), so the mirrored kidney shapes of `4tab4` have
  their centres at x = 80 and x = 239 (not symmetric about 159.5). Keep that. The tile seams are discontinuities
  (part of the look).

### 6.3 Pairs (column u / row v) and what they look like

- #1: u = `tab10` (distance with 40 small waves), v = `tab11` (distance with 10 big waves): both coordinates grow
  with the distance, so the texture is stretched along rays and the wobbles make a 10-pointed star; near the
  centre v is negative in ten petals (the white petals in the table's picture), where the row wraps around the
  texture.
- #2, #3: u = distance, v = angle + 41 · sin(πr/128): the texture wrapped once around the centre, twisted.
- Part II: plane 1 u = angle about (220, 130), v = angle about (99, 69) (or, in efek5, the distance map `tab14`);
  plane 2 u = mirrored angle about (99, 130), v = mirrored angle about (220, 69); each with the fast radial ripple
  (period 25.6 pixels).

### 6.4 The 2 × 2 tables (block coordinates bx 0..159, by 0..99; block (bx, by) covers pixels 2bx..2bx+1, 2by..2by+1)

These are not exact formulas: each has a random-looking ±1..2 noise (in the contour lines of `tab4.2x2` it shows as
ragged steps, e.g. row 60 runs 109 110 109 108 108 109 107 …); perhaps made with an image program, a guess. The
smooth fits (`fit2x2.py`, value ≈ floor(fit)):

| table | fit | bytes exact, error |
|---|---|---|
| `tab4.2x2` (column, plane 1) | (−0.974152 bx − 6.98946 by + 114.351) / (−7.38868e−6 bx + 0.0182636 by + 1) | 9,652 / 16,000, ±2 |
| `tab5.2x2` (row, plane 1) | (−3.39436 bx + 4.3322 by + 120.507) / (0.013228 bx + 0.0611961 by + 1) | 10,938 / 16,000, ±1 |
| `tab6.2x2` (planes 2, 3) | a · 128/π + 16 · cos(r · π/64) about block (80, 50), truncated | 9,505 / 16,000, ±2 |

(the first two values are mod 256: the fits are of the unwrapped tables.) `tab4.2x2` and `tab5.2x2` are each a
projective (perspective) map: the dot lattice of `2x2_3` appears as a floor; they do not share one denominator, so
they are not one plane in 3D (a joint fit leaves 4.7 texels rms), but each alone fits to the noise level (0.5..0.6
rms). The remake uses the fits (or the tables unwrapped and filtered).

## 7. Textures and pictures

### 7.1 Textures (256 × 256, read through the tables, wrap-around)

| file | look | indices | used |
|---|---|---|---|
| `plo2.raw` | greenish / cyan caustic streaks on dark | 0..127, palette in no order | #1; part II efek5 plane 1 |
| `plo6.raw` | purple / pink embossed relief | 0..127, dark → light ramp | #2; part II efek4 plane 2 |
| `plo5.raw` | yellow lightning lines on dark (cells) | 2..127, black → yellow ramp | #3; part II efek5 plane 2 |
| `plo3.raw` | fire: orange flames on dark red | 4..127, black → red → yellow ramp | part II efek4 plane 1 |
| `2x2_3.raw` | a 16 × 16 tile repeated 16 × 16 times: a lattice of soft squares | 0..31 | 2 × 2 plane 1 |
| `2x2_4.raw` | smooth noise | 0..31 | 2 × 2 planes 2, 3 (its diagonal) |

All are **generated** (texture-generator / filter output, not hand-drawn; a judgement from their look) and tile
seamlessly. Part I reads `plo*` as plain colour (index → palette); part II reads them through DTABLE by index; the
2 × 2 adds `2x2_*` indices. So: no redraws; the remake samples the original textures with filtering (part I and II:
as colour images, the palette applied first; 2 × 2: as index values, the sum looked up afterwards, 9.).

### 7.2 The photos (`nainen1..3.raw`)

Photos of women, 320 × 200 full-screen, drawn at 1:1 over the effect (#1 and #3 at the left, #2 at the right); index
0 is transparent, 64 colours at palette 192..255. The remake shows their redraws (`art/nainen1..3.png`, RGBA) with the palette's fade fitted per refresh as a gain and offset of their colours; with `--original-art` the original pixels with the index-0 mask.

### 7.3 DTABLE (part II)

`dtable.raw` / `dtable2.raw`: 256 × 128 indices, DTABLE[b][a] for plane 1 texel a (only a < 128 occur) and plane 2
texel b, with its own palette (the DAC palette during the section). Its colours are a **50 % blend of the two
textures' own palette colours**, quantised to 256 colours with dithering (`dtable.py`):

| | rms error of DTABLE's colour vs (pal1[a] + pal2[b]) / 2 | 8 × 8-entry means | weights 0.4 / 0.6 |
|---|---|---|---|
| `dtable` (plo3 / plo6) | 8.9 (8-bit units) | 3.1 | 13.8 / 12.2 |
| `dtable2` (plo2 / plo5) | 6.6 | 0.8 | 10.2 / 10.6 |

(only 36..41 % of the entries are the nearest palette colour of the blend: the error is dither noise that averages
out.) The remake draws colour = (colour1 + colour2) / 2 with the textures' own palettes (the raw files' 8-bit
palettes, or >> 2 for the 6-bit values) and drops the dither.

## 8. Palettes and fades

- 1x1 #1..#3: palette[0..191] = the texture's raw palette >> 2 (only 0..127 are used), palette[192..255] = the
  photo's 64 colours >> 2. Identical to `pal/plo2.pal`, `plo6.pal`, `plo5.pal`.
- 2 × 2: `2x2_3.raw`'s palette (= `pal/2x2_4.pal`; 8-bit values before the >> 2): black at 0..4, a slow dark
  purple ramp to (47, 0, 31) at 55, then faster to purple-red (79, 0, 35) at 64, red (135, 23, 11) at 80, orange
  (223, 179, 51) at 88, yellow (255, 255, 111) at 92, white from 95. The sums 0..93 use it as a glow ramp.
- Part II: DTABLE's palette (= `pal/efek4.pal`, `efek5.pal`).
- Init writes the DAC once (FadeStep(1); the script's argument 0). Then the DAC is the effect's palette in every Run
  record (checked: 222 / 222 .. 889 / 889 per run) except during the three fades in the script, which the Run loop
  steps once per refresh (FadeStep with the refreshes passed; notes-core.md for the fade arithmetic):
  - `InitFADE pal/plo2.pal pal/white.pal 80 0 255` at 30:40 (record 7688): the run lasts 70 refreshes, so the fade
    reaches step ~69 of 80; the synkki section then starts `white → synknai1` from full white (a jump of 11/80);
  - `InitFADE pal/plo6.pal pal/white.pal 60 0 255` at 42:32 (10332): complete after 60 of the run's 111 refreshes,
    then white;
  - `InitFADE pal/plo5.pal pal/black.pal 60 0 255` at 54:32 (13003): complete, then black.
  In the records the fade shows step (counter − counter at InitFADE − 1).
- No other palette writes, no palette cycling, no random numbers.

## 9. For the remake: shortcuts to evaluate exactly, and what stays

Per output pixel, with continuous original coordinates (x, y) ∈ [−0.5, 319.5) × [−0.5, 199.5):

1. **Tables → formulas** (6.): u(x, y), v(x, y) in float, without truncation; tiled tables with xl, yl taken as
   the continuous position inside the tile (the seam between pixels 159 and 160 is at x = 159.5; mirrored tiles
   xl → tw − 1 − xl); `.2x2` tables with block coordinates bx = (x − 0.5) / 2, by = (y − 0.5) / 2 and the fits of
   6.4 (no 2 × 2 blocks).
2. **Offsets continuous**: column = u + Ox / 256, row = v + Oy / 256 (mod 256), O from 4.2 / 4.3 with the
   fractional refresh. The original uses whole texels (the offsets' high bytes): the texture jumps in 1-texel
   steps every 256 / |S| refreshes; drop that. Drop the carry from the column into the row (5.2: where u + Ox
   passes 256 the row is one texel off, along one contour of the u table).
3. **Texture reads**: filtered (bilinear + mipmaps or anisotropic), wrap-around 256. The angle maps have a branch
   cut (a = ±π) where v jumps by 256: compute derivatives modulo 256 (or use textureGrad with the analytic
   gradient), otherwise the mip level explodes along the cut. Near the polar centres the original aliases (many
   texels per pixel); filtering turns that into a smooth blur, which is right.
4. **Index combinations**:
   - part I: colour = texture's palette colour (filter the colour texture, i.e. the palette applied before
     filtering); the photo on top where its index ≠ 0 (mask from the original pixels; scale the mask without
     blurring its edge into the photo's colours: nearest or a filtered alpha);
   - part II: colour = (texture1 colour + texture2 colour) / 2 (7.3);
   - 2 × 2: s = P1 + 2 · P2 with P1, P2 the filtered index values (continuous), colour = the palette ramp at s,
     interpolated between entries (the entries are 6-bit steps of a smooth ramp).
5. **What stays as in the original**: the table formulas and centres (including the tiles' 1-pixel asymmetry),
   the speeds with the y quirk, the run boundaries, the photos' pixels, the textures' texels (one texel per
   texture pixel, filtered: these are the effect's grain).
6. **Known difference**: none of the drawing depends on the frame rate. The original's tearing (records show
   the new frame down to row ~28 over the previous one, 10.) is a property of drawing into VGA memory without
   waiting for the retrace; the remake shows whole frames.

## 10. Where it draws; tearing; speed

Everything goes straight into VGA memory at 0xa0000 while the screen is displayed; there is no back buffer and no
retrace wait. Every drawer writes all 64,000 pixels (1 × 1) or all 16,000 blocks (2 × 2), so nothing of the previous
part stays once the first frame is complete (the sections start after ClearSCR, so the first record shows the new
frame's top over black; the b / c parts start over the previous part's last frame). The draw starts when the main line
resumes after the counter's increment (at the retrace start, notes-core.md) and, at the capture's 100 M
instructions per second, takes about 0.55 of a refresh (1 × 1: roughly 25 instructions per two pixels) or about a
quarter (2 × 2): when the record is taken at the first displayed line the drawer has reached row ~28 (1 × 1; 14..23
when a fade writes the DAC first) or row ~90 (2 × 2). Every record is
"new frame above, previous frame below" in drawing order (`verify.py` checks exactly this); in part I's b / c parts the
Init takes about half a refresh, so the first draw is still in progress (row ~119, over the previous part's last
frame) at the next record. One frame is drawn per refresh
throughout (70.086 frames per second); on a slower PC fewer frames are drawn but the motion is the same (4.2).

## 11. Python model

`models/bitmap/bitmap.py` (Init, step, drawers), `verify.py` (every Run record of `full` and `part2`: all exact),
`palcheck.py` (DAC), `fittabs.py` (the `.1x1` formulas), `fit2x2.py` (the `.2x2` fits), `dtable.py` (the blend).
The model reads the archive files from `unpacked/files/` and nothing from the capture except the counter values of
the records.

## 12. Not determined

- How the `.2x2` tables were made (the noise; perhaps resampled or retouched in an image program); the fits of 6.4
  are the best smooth description, not the generator.
- `tab14.1x1`'s 341 differing bytes lie on the two axis lines through its centre (x = 179 above and below, y = 40
  to the left), where sin(100 a) is 0 in exact arithmetic and the generator's rounding left it ≥ 0 (the float32 /
  double variants tried all give a tiny negative value there). Irrelevant for the remake.
- What `2x2_3.raw`'s 16 × 16 tile is as a formula (it is not a product or sum of simple cosines; a guess: a
  filtered or hand-made lattice). The remake reads it.
- Whether the textures are generated or hand-made is judged from their look only.
- [0x8232ee] (the loop's quit flag) is set outside this scope (keyboard / core); it is 0 throughout the capture.
