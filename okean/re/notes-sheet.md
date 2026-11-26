# Scope "sheet": the seventh part 0x5334f (the curling sheet, 23:00..27:10)

A 16 x 16 wireframe grid ("the sheet") whose height is a ring-shaped sine wave of the squared distance from one
point of the grid; the wave runs outwards and its amplitude grows from 0, so the flat sheet starts to ripple and
then curls up into bright folds. The sheet is tilted by 225 degrees and swings on two axes. Its edges are drawn as
rows of 8x8 glowing blobs **added** to per-pixel noise and shown through a black → lavender → white palette. The
captions "float robot float", "if thy degrade yourself", "thy shall be upgraded" and "thy shall not make whores" are
printed at the bottom by the shared text printer. From order 27 on the sheet falls off the bottom of the screen with
growing speed while the palette cross-fades to a grey one; the part ends when the cross-fade is complete, leaving
bright grey noise.

Routines: the part 0x5334f (set-up and main loop), the callback 0x5355a, the projection and edge loop 0x531fe, the
line drawer 0x52fed; it calls the timer copy 0x4d71c / 0x4d797 / 0x4d770 (IRQ 0 handler 0x4d7e8) and the shared text
printer 0x4cacc. The part is the last code in the program (0x52fed..0x53676); its data is 0x4d84c..0x52fec.

Addresses are code32 offsets (file offset in `unpacked/korso2.bin` = 0xec0 + address; linear in the run = 0x8fc0 +
address). `T[i]` is the 32-bit sine table at 0x24a3c, `C(a)` = `T[a + 1025]` (the "cosine" table at 0x25a40).
"Callback n" = the n-th call of the part's callback (n = 1 is the first); "state n" = the variables after n
callbacks (state 0 = the values in the file). "Record k" = record k of `full.frames` = refresh k (49.851 Hz).
Positions are order:row in decimal. Screen coordinates are original 320 x 200 pixels, pixel x covering [x, x + 1).

Verified (models in `models/sheet/`, README there):

| what | how | result |
|---|---|---|
| part variables and tables at the start | `snaps/part_5334f.snap` against the file (`snapdump.py`, `check_static.py`) | all equal to the file; only the sine table beyond entry 1024 is generated (by the title part, 0x2c26f) |
| whole frames | `sheet_model.py` (state from the callback count, noise from the number of draws, captions from the printer's script and the music position) against every record 6896..8096 (`compare_all.py`) | **1173 of 1173 drawn records equal to the byte** (all 64,000 indices); every record's DAC equals the palette of the draw it shows or of the next draw (section 9); records 6893..6895 black as described |
| byte carries of the line drawer | the model adds dwords byte-exactly; `stats.py` accumulates without wrapping | the sum never exceeds 217, so the carries never happen |

---------------------------------------------------------------------------------------------------------------

## 1. What it draws, and when

| records | music | what |
|---|---|---|
| 6893..6895 | 23:00 | set-up: video memory cleared (black), the full palette 0x526cd written, the timer installed and one frame measured (0x4d797, interrupts off) |
| 6896..~6960 | 23:00..23:10 | the flat grid fades in from black (palette level 2/64 → 64/64 over 63 refreshes) while the ripple's amplitude starts to grow from 0 |
| ~6960..8033 | 23:10..26:47 | the rippling, curling sheet swinging on two axes; amplitude full from record 7152 (state 256) on; captions (section 6) |
| 8034..~8082 | 27:00..27:08 | the sheet falls downwards with constant acceleration while the palette cross-fades to grey; the last line leaves the screen at state ~1186 (record ~8082) |
| ~8083..8096 | 27:08..27:10 | bright grey noise only; the cross-fade ends; record 8096 is the part's last picture (palette level 1/64), main calls the last part 0x4d370 at 162.4186 s |

The part writes **every pixel of every frame**: the noise fills all 64,000 bytes of the buffer, the lines are added to
it, the text overwrites it, and the whole buffer is copied to video memory.

## 2. Static data, buffers, variables

| what | where | format / content |
|---|---|---|
| palette 1 | 0x526cd, 768 bytes | 6-bit RGB x 256: 0..158 a ramp black → lavender → white (16 = (5,6,9), 31 = (11,13,18), 64 = (23,24,33), 96 = (43,40,49), 128 = (54,52,57), 158 = (62,62,62)), 159..253 = (63,63,63), 254 = (33,33,33), 255 = (63,63,63); 7 = (2,2,3) |
| palette 2 | 0x529cd (= 0x526cd + 0x300; read as `[esi + 0x300]` at 0x534e9), 768 bytes | 0 = black, 1..46 a grey ramp (1 = (5,5,5), 7 = (16,14,16), 16 = (29,29,32), 31 = (45,45,47), 46 = (62,62,62)), 47..253 white except 68..92 a faintly warm white (down to (63,61,59)) and **111 = (63,34,63)**, 254 = (33,33,33), 255 = white |
| grid | 0x4decd, 256 x 12 bytes | int32 x, y, z per vertex k = 16 i + j (i, j = 0..15): x = (i − 8)·256, y = (j − 8)·256 (−2048..1792: the grid is not centred on the origin), z = −512 in the file, overwritten every frame |
| edges | 0x4eacd, 480 x 8 bytes | int32 vertex pairs: 0..239 = (16 i + j, 16 i + j + 1) for j = 0..14 (along y), 240..479 = (k, k + 16) for k = 0..239 (along x) |
| row table | 0x52ccd, 200 dwords | 320·y |
| sine table | 0x24a3c, 5121 dwords | entries 0..1024 in the file (a quarter wave ≈ 1023 sin(2πi/4096) rounded down, peak 1022), the rest written by the title part's set-up 0x2c26f (scope "title + objects"): 1025..2048 = entries 1024..1, 2049..4096 = −(0..2047), 4097..5120 = 0..1023 |
| stamp | immediates of the 16 `add dword [edi + row·320 (+4)], imm32` at 0x53158..0x531e7 (immediates at 0x5315a, 0x53164, 0x5316e, 0x53178, 0x53182, 0x5318c, 0x53196, 0x531a0 for the left dwords and 0x531a7, 0x531b1, 0x531bb, 0x531c5, 0x531cf, 0x531d9, 0x531e3, 0x531ed for the right ones) | the 8x8 kernel of section 5 |
| tilt | immediate 0xa00 at 0x53441 (`mov dword [0x4de40], 0xa00` at 0x5343b) | rotation angle about x, 225 degrees; [0x4de40] = 0x400 in the file is overwritten before use |
| camera distance | [0x4de14] = 2048 | added to depth |
| caption script | 0x4c8e8.. (shared with the other parts; the printer's pointer [0x4c8e4] = 0x4c9cb at the part's start) | pages of text, section 6 |
| font | pixels 0x3cec2 (rows of 320 bytes, 38 rows), widths 0x4c8c2, character map 0x4c861 (scope "core") | indices 0 (transparent), 7, 216, 254 |
| draw buffer | getmem high 64,000 bytes (0x2b9), pointer [0x4dec1]; also given to the printer in [0x4cac4] | never freed (the part has no `push [8]` / `pop [8]`) |
| projected points | 0x4f9cd, 256 x 12 bytes | int32 X, Y, Y (the third dword is a copy of Y, unused) |
| line end points | [0x4deb1] x0, [0x4deb5] y0, [0x4deb9] x1, [0x4debd] y1 | |
| palette buffer | 0x4d854, 768 bytes | the faded palette written to the DAC |

Variables (all initialised in the file; the snapshot shows the file's values, nothing is carried over from earlier
parts):

| variable | file value | meaning | changed by |
|---|---|---|---|
| [0x4db54] b | 1 | palette level / 64 | callback: +1 up to 64 before order 27, −1 down to 0 in order 27 |
| [0x4db58] amp | 0 | ripple amplitude (/512) | callback: +1 up to 256 |
| [0x4db60] phase | 0 | ripple phase | callback: −0x20 (mod 4096) |
| [0x4dbf4] | 0 | phase of the swing about y | callback: +8 (mod 4096) |
| [0x4de6c] | 0 | phase of the swing about z | callback: +7 (mod 4096) |
| [0x4dbf8] | 0 | phase of a third swing | callback: +0x13 (mod 4096); read by the main loop but the result is discarded |
| [0x4d850] | 0 | fall speed | callback in order 27: +2 |
| [0x4d84c] | 0 | fall offset (added to y after rotation) | callback in order 27: + [0x4d850] |
| [0x4db5c] | 0x003513c3 | noise seed | main loop, per draw |
| [0x4de44], [0x4de68] | 0 | swing angles | main loop, from the phases |

Dead code without effect: 0x53391 copies [0x4dbfc] to [0x4dc04] and loads [0x4dbec] into esi (overwritten at once);
0x53423..0x53436 computes the angle of [0x4dbf8] into eax and drops it (the `mov [0x4de40], 0xa00` that follows
probably replaced its store); 0x531fe stores Y twice.

## 3. The part (0x5334f)

```c
void part_sheet(void)                                     // 0x5334f
{
    memset32(video, 0, 16000);                            // black screen
    buf = getmem_high(64000);                             // [0x4dec1]
    dac_write(0, pal1, 768);                              // 0x526cd at full level; the screen is still black
    timer_install();                                      // 0x4d71c: IRQ 0 -> 0x4d7e8, callback [0x4d710] = 0x5355a
    timer_measure();                                      // 0x4d797: PIT count of one refresh - 0xc0
    for (;;) {                                            // 0x53391
        heights();                                        // section 4.1
        for (i = 0; i < 16000; i++) {                     // 0x533f1: noise, section 7
            seed = rol32(seed + 0x9d, 11) ^ 0x9d;
            ((uint32_t *)buf)[i] = seed & 0x1f1f1f1f;
        }
        gamma = 0xa00;                                    // [0x4de40]
        beta  = ((T[ph_y]  * 3) >> 3) & 0xfff;            // [0x4de44], ph_y  = [0x4dbf4]  (>> is sar)
        alpha = ((T[ph_z]  * 3) >> 3) & 0xfff;            // [0x4de68], ph_z  = [0x4de6c]
        project_and_draw();                               // 0x531fe, sections 4.2 and 5
        [0x4cac4] = buf; text_printer();                  // 0x4cacc, section 6
        for (i = 0; i < 768; i++)                         // 0x53493
            pal[i] = (order == 27) ? (pal1[i] * b + pal2[i] * (64 - b)) >> 6
                                   : (pal1[i] * b) >> 6;
        dac_write(0, pal, 768);                           // before the retrace wait
        while (!(inb(0x3da) & 8)) ;                       // retrace start (the callback runs here)
        while (inb(0x3da) & 8) ;                          // retrace end
        memcpy32(video, buf, 16000);
        if (b == 0) break;                                // the end: 64 callbacks into order 27
        if (inb(0x60) == 1) break;                        // Esc
    }
    timer_restore();                                      // 0x4d770: PIT mode 3, 18.2 Hz, old vectors
}
```

`order` is the player's order [0x12b75] read at that moment. The part does not poll for the next part's order like
the others: it ends when its own cross-fade, started by order 27, reaches 0.

The timer copy 0x4d71c..0x4d823 (install, measure 0x4d797, handler 0x4d7e8 with `call [0x4d710]`, real-mode stub
0x4d824, restore 0x4d770) lies between the last part's code and this part and is called only by this part (the
last part 0x4d370 uses another copy, IRQ 0 → 0x3ce48, callback 0x4d645). Whether it is the same code as 0x14bec: see
notes-core.md.

## 4. The sheet

### 4.1 Heights (0x5339b, per draw)

```c
k = 0;
for (i = 0; i < 16; i++) {                 // outer loop, x
    int X = -64 + 8 * i;                   // = grid x / 32
    for (j = 0; j < 16; j++, k++) {
        int Y = -64 + 8 * j;               // = grid y / 32
        grid[k].z = (T[(X*X + Y*Y + phase) & 0xfff] * amp) >> 9;     // sar
    }
}
```

So z = (amp / 512)·T[(r² / 1024 + phase) mod 4096] with r² = x² + y² in grid units: with S(a) = sin(2πa/4096),
z ≈ 2·amp·S(r²/1024 + phase) (1022/512 = 1.996), up to ±511 at amp = 256. X² + Y² goes up to 8192: two periods of
the sine out to the grid's corner (for phase 0 the zero crossings are at r = 0, 1448, 2048, 2508, 2896). phase decreases
by 32 per callback, so the rings move outwards, one table turn per 128 callbacks (2.57 s). The centre of the rings is
vertex (8, 8) at (0, 0), not the middle of the grid.

### 4.2 Rotation and projection (0x531fe, per draw)

For each vertex (x, y, z) (int32; products fit in 32 bits; `>> 10` is sar, so it rounds down):

```c
s = T[alpha]; c = C(alpha);                            // about z: x, y
x1 = (x * c - y * s) >> 10;   y1 = (y * c + x * s) >> 10;
s = T[beta];  c = C(beta);                             // about y: x, z
x2 = (x1 * c - z * s) >> 10;  z1 = (z * c + x1 * s) >> 10;
s = T[gamma]; c = C(gamma);                            // about x: y, z   (gamma = 0xa00 = 225 degrees)
y2 = (y1 * c - z1 * s) >> 10; z2 = (z1 * c + y1 * s) >> 10;
y2 += fall;                                            // [0x4d84c]
z2 += 2048;                                            // [0x4de14]
if (z2 > 0) { X = (x2 << 8) / z2; Y = (y2 << 8) / z2; }  // cdq; shl 8; idiv: truncation towards zero
else        { X = x2 * (1 - z2); Y = y2 * (1 - z2); }    // never visible, see below
X += 160; Y += 100;                                    // stored at 0x4f9cd + 12 k: X, Y, Y
```

then every edge (a, b) of the list is drawn with 0x52fed from (X[a], Y[a]) to (X[b], Y[b]).

- The angle unit is 1/4096 turn. alpha = 3·T[7n mod 4096] / 8 ≈ ±383 units (±33.7°) with period 4096/7 = 585.1
  callbacks (11.74 s); beta = 3·T[8n mod 4096] / 8, ±33.7° with period 512 callbacks (10.27 s) (n = callbacks,
  section 8; negative values wrap to 4096 − |v|, the same angle).
- The tilt of 225° turns the sheet so that (flat, unswung) its row y = 1792 is near the camera (depth ≈ 784,
  projected at Y ≈ −313, far above the screen) and y = −2048 far away (depth ≈ 3492, Y ≈ 206, just below the bottom
  edge); the screen shows the rows from y ≈ 816 (top) down to the far edge. Most of the sheet lies outside the screen
  (on average 146 of the 256 vertices). The focal length is 256 screen pixels in x and in y (320 x 200 shown at 4:3, so
  the picture is stretched vertically by 1.2 like everything in this mode).
- Vertices come close to the camera: the depth z2 goes down to 1 (screen coordinates up to ±450,000) and in 397 of the
  1138 states before order 27 one or two vertices are behind it (z2 down to −353). The z2 ≤ 0 branch scales instead of
  dividing (so a vertex just behind the camera jumps to scale 1); checked over all states, no edge with such an end
  ever puts a blob on the screen (`near.py`), so a remake can clip at a near plane instead.

## 5. The line drawer (0x52fed): blobs every 2 pixels, added

```c
void line(int x0, int y0, int x1, int y1)                    // [0x4deb1], [0x4deb5], [0x4deb9], [0x4debd]
{
    if (!(y0 > 0)     && y1 < 0)     return;                 // both above (one end may be on row 0)
    if (!(x0 > 0)     && x1 < 0)     return;
    if (!(x0 < 319)   && x1 > 319)   return;
    if (!(y0 < 199)   && y1 > 199)   return;
    if (!((unsigned)y0 < (unsigned)y1)) swap(end 0, end 1);  // start at the smaller y; a negative y counts as huge
    dx = x1 - x0; nx = dx >= 0 ? dx + 1 : -dx;               // cdq; xor; inc
    dy = y1 - y0; ny = dy >= 0 ? dy + 1 : -dy;
    n = max(nx, ny) >> 1;                                     // one blob per 2 pixels of the major axis
    if (n == 0) return;
    sx = (dx << 8) / n; sy = (dy << 8) / n;                   // 8.8, truncated towards zero
    // 0x530e8: "if (y0 < 0) clip": never reached (a start with y0 < 0 has y1 < 0 too and was rejected); it would
    // add (−y0)·sx to the integer x0, a bug
    fx = x0 << 8; fy = y0 << 8;
    for (cnt = n & 0xff ? n & 0xff : 256; cnt; cnt--) {       // dec cl: the count is n mod 256
        px = fx >> 8; py = fy >> 8;                           // sar
        if (0 <= py && py <= 191 && 0 <= px && px <= 311)
            for (r = 0; r < 8; r++) {                         // two dword adds per row (carries between bytes)
                *(uint32_t *)(buf + (py + r) * 320 + px)     += stamp_left[r];
                *(uint32_t *)(buf + (py + r) * 320 + px + 4) += stamp_right[r];
            }
        fx += sx; fy += sy;
    }
}
```

The stamp (rows top to bottom, columns left to right, bytes of the immediates in memory order):

```
 0  1  1  1  1  1  1  0
 1  1  2  2  2  2  1  1
 1  2  4  8  8  4  2  1
 1  2  8 15 15  8  2  1
 1  2  8 15 15  8  2  1
 1  2  4  8  8  4  2  1
 1  1  2  2  2  2  1  1
 0  1  1  1  1  1  1  0        sum 200, symmetric in x, y and the diagonals
```

- The blobs are placed at i = 0..n − 1 steps from the start; the far end itself gets none (the last blob is one step
  short of it). Edges meet at the grid's vertices, and a vertex that is the start (smaller y) of its edges gets their
  first blobs exactly on it, so the **vertices show as bright knots** (visible in every frame).
- The blob's top-left corner is the truncated line position, so its centre is 4 pixels right of and below that
  position (between pixels px + 3 and px + 4): the whole picture is shifted by about (+3.5, +3.5) pixels relative to
  the projection centre (160, 100).
- An axis-aligned line gets the cross-section 3, 6, 15, 26, 26, 15, 6, 3 (row sums / 2); a diagonal line has its
  blobs 2.83 pixels apart and a lower, wider profile (peak 20 per row-crossing, the same sum 100 per row): diagonal
  lines are dimmer than horizontal and vertical ones.
- Blobs whose corner is outside [0, 311] x [0, 191] are skipped whole, so the light never reaches column 319 or row
  199, and lines end up to 4 pixels before the left and top edges (an artefact of the bounds check).
- The adds are dword adds at any alignment: a byte passing 255 would wrap and carry 1 into the pixel to its right
  (within the dword). Over the whole part the highest value is 217 (`stats.py`), so this never happens.
- The loop counts only cl, so an edge with n > 255 gets n mod 256 blobs. That happens only for edges to vertices
  very close to the camera, and never cuts a line that is still on the screen (`near.py`).
- 2252..3211 blobs per frame (mean 2640) before order 27; at most 48 on one edge.

Every part of the intro has its own copy of this drawer with its own stamp (notes-morph-star.md lists them).

## 6. The captions (0x4cacc, the shared printer)

The part calls the printer once per draw, after the lines, with [0x4cac4] = the draw buffer. The printer (scope
"core") draws the current page of its script and then, at the page's end mark `@`, compares the player's order
[0x12b75] and bit 4 of the row word [0x12b73] with the values it stored at its last page change ([0x3ceb8] byte,
[0x3cebe]): if both are equal it rewinds to the page's start (the same page is printed again next time), otherwise it
stores them and leaves its pointer after the `@`, so the **next** draw prints the next page. A page therefore stays for
16 rows (the patterns of these orders are 48 rows long: rows 0..15, 16..31, 32..47) and changes one draw after the row
0, 16 or 32 is reached.

Each page in this part is `\r` + text + `@`. `\r` starts a line: its width W = the sum of the character widths up
to the next `\r` (a space counts 4, `@` counts 0, a letter its width from 0x4c861[c] = 0x4c8c2[c − 'a']), the line
starts at x = 160 − (W >> 1), y = 162 (offset 0xca80 = 162·320; the glyphs are 38 rows high, their pixels in rows
1..36, so rows 163..198). Letter c is the glyph at column Σ widths of the letters before it in the font sheet
(0x3cec2), drawn opaque where its pixel is not 0 (indices 7 = the dark outline, 216 = white, 254 = the grey edge),
then x advances by its width + [0x3ceb0] (= 0).

At the part's start the printer's pointer is 0x4c9cb, its stored position 22 / bit 0 (from the sixth part). Pages
(`captions.py`, from the model on the capture's schedule):

| address | text | x range | records | position when the page changed .. when it was left |
|---|---|---|---|---|
| 0x4c9cb | (empty) | | 6896 | 23:00 (the order differs from 22: advance at the first draw) |
| 0x4c9cd, 0x4c9cf, 0x4c9d1 | (empty) | | 6897..7179 | 23:00 .. 24:00 |
| 0x4c9d3 | float robot float | 82..237 | 7180..7274 | 24:00 .. 24:16 |
| 0x4c9e6 | (empty) | | 7275..7369 | 24:16 .. 24:32 |
| 0x4c9e8 | if thy degrade yourself | 52..268 | 7370..7464 | 24:32 .. 25:00 |
| 0x4ca01 | thy shall be upgraded | 59..260 | 7465..7559 | 25:00 .. 25:16 |
| 0x4ca18, 0x4ca1a | (empty) | | 7560..7749 | 25:16 .. 26:00 |
| 0x4ca1c, 0x4ca37, 0x4ca52 | thy shall not make whores (three pages with the same text) | 37..283 | 7750..8034 | 26:00 .. 27:00 |
| 0x4ca6d | (empty) | | 8035..8096 | 27:00 .. (the last part continues from 0x4ca6f) |

The strings are in the file at those addresses + 1 (after the `\r`). In the ending the text's indices go through the
cross-fade like everything else (the outline 7 turns grey: pal2[7] = (16,14,16)).

## 7. The noise (0x533f1, per draw)

Generator: x = rol32(x + 0x9d, 11) ^ 0x9d, seed [0x4db5c] = 0x003513c3 from the file (this part's own variable; not
continued from another part; the first part's generator uses ror and [0x14d1c]). 16,000 calls per draw, each
giving four pixels (bytes of x & 0x1f1f1f1f, lowest byte leftmost). The seed is stored back after each draw, so the
noise is a function of the number of draws, not of the callbacks: **frame-rate dependent** (one new field per drawn
frame; on a slow machine the noise changes less often, everything else keeps its speed).

Statistics: indices 0..31, uniform (mean 15.5), neighbours independent (correlation −0.03 at lag 1, < 0.001 at lags
2, 4, 320), no repeated generator state over the 18.8 million calls of the part. In palette 1 that is a dark
blue-grey speckle from black to (11,13,18); in palette 2 a bright grey one from black to (45,45,47).

## 8. The callback (0x5355a) and the time bases

Called by the IRQ 0 handler 0x4d7e8 at the start of every vertical retrace (49.851 Hz). Without a GUS it first runs
the fake music clock (as in the other parts, notes-core.md; its counter is the byte 0x5356d inside the skipped bytes
of `jmp 0x5356e` at 0x53568).

```c
void callback(void)                                       // 0x5355a
{
    if (order != 27 && b != 64) b++;
    if (order == 27) {
        fall_v += 2; fall += fall_v;                      // [0x4d850], [0x4d84c]
        if (b != 0) b--;
    }
    if (amp < 256) amp++;
    ph_y   = (ph_y + 8)    & 0xfff;                       // [0x4dbf4]
    ph_3   = (ph_3 + 0x13) & 0xfff;                       // [0x4dbf8], unused
    ph_z   = (ph_z + 7)    & 0xfff;                       // [0x4de6c]
    phase  = (phase - 0x20) & 0xfff;                      // [0x4db60]
}
```

Closed forms after n callbacks, of which m ran in order 27 (m = max(0, n − 1137) in the capture):

| variable | state n | time base |
|---|---|---|
| b | min(1 + n − m, 64) − m, at least 0 | callback; the part ends at b = 0 |
| amp | min(n, 256) | callback |
| phase | −32 n mod 4096 | callback |
| ph_y, ph_z | 8 n, 7 n mod 4096 | callback |
| fall_v, fall | 2 m, m (m + 1) | callback, order 27 |
| noise seed | 16,000 steps per draw | per drawn frame |
| caption page | section 6 | music position, checked per draw |

The heights, angles, fall and palette level all come from the callback's counters, so the animation's speed does
not depend on the frame rate (in the 70 Hz mode, `-M`, all of it runs 40 % faster, the fade-out in 0.91 s).

Music-position events:

| position | event |
|---|---|
| 23:00 | the part starts (main calls it after the sixth part left at order 23); the order-23 tick was at 138.2650 s, callback 1 runs at 138.3470 s (refresh 6896) |
| 24:00, 24:32, 25:00, 26:00 (..26:47) | captions (section 6), each for 16 rows, one draw after the row |
| 27:00 | from the first callback in order 27 (callback 1138, refresh 8033, 161.1547 s; the order-27 tick was at 161.1405 s): the fall starts and b counts down; the main loop cross-fades to palette 2 from the first draw that sees order 27 |
| 27:10 | b reaches 0 at callback 1201 (refresh 8096, 162.4185 s), 64 callbacks after the first: the main loop copies its last picture and returns; main calls 0x4d370 |

## 9. Which frame shows what

- Callback n runs at the retrace of refresh 6895 + n (the timer counts 6879 interrupts at the end of 6895, 6880 at
  the end of 6896).
- Draw j starts after the copy of draw j − 1 (in refresh P, after the callback of that retrace), so it uses state
  P − 6895: draw 0 is made in refresh 6895 after the timer set-up with state 0, and **record 6896 + n shows state n**
  in general.
- The palette is written at the end of the draw, before the retrace wait: a record shows the picture of state n with
  the palette of the next draw (state n + 1), which is visible during the fades only (record 6896 has level 2/64).
  When a draw runs past the end of its refresh (the captions are printed byte by byte, and the music interrupt runs
  inside the draw at rows with new notes), the record still has the palette of its own draw and the slow draw's
  palette is replaced by the next one within the following refresh: records 8033, 8044, 8050, 8056 show the level
  of their own draw, and the levels 63, 52, 46, 40 are never on screen.
- 28 records repeat the picture before (6976, 6982, 7065, 7071, 7166, 7249, 7261, 7338, 7344, 7427, 7433, 7516, 7522,
  7528, 7611, 7623, 7700, 7706, 7712, 7789, 7795, 7801, 7878, 7890, 7973, 7979, 8062, 8074): the GUS interrupt (which
  waits inside its handler for a volume-ramp interrupt, notes-music.md) held the main loop across the 2-line
  retrace, so the copy came one refresh later and the next draw skipped one state. Once (draw for refresh 7516) the
  interrupt held it between the retrace's start and end until refresh 7517 had begun: that draw (state 620) was
  copied early in 7517 and replaced by another draw (state 621) within the same refresh, so it is in no record. These
  are timing effects of the emulated machine (on real hardware they would fall elsewhere); the remake draws every
  refresh.
- Records 6893, 6894 show the cleared screen with palette 1 at full level (black: index 0 is black), record 6895
  the cleared screen with level 1/64.

## 10. Colours: what an index means

Every pixel's index is an **intensity on a ramp**: index = noise (0..31) + the sum of the blobs over it (noise and
blobs together reach 217 at most over the part), except the caption pixels, which overwrite it with the font's indices 7, 216, 254. Palette 1 maps
intensity 0..158 to black → lavender (blue first, then red and green catch up) → white, and everything from 159 up to
white. The DAC gets palette 1 times b/64 (fade in over the first 63 callbacks; b is 64 from state 63 to state 1137);
in order 27 it gets (palette 1·b + palette 2·(64 − b)) / 64 with b falling from 63 to 1 over the last 63 draws (the
`>> 6` truncates). Palette 2 is a steeper grey ramp (white from intensity 47) with a slightly warm white at 68..92 and
a single pink entry 111 = (63,34,63): in the cross-fade a few pixels of the falling lines (up to 10 per frame) whose
intensity is exactly 111 turn pink. Index 254 is a fixed grey in both palettes (the font's edge), 255 white.

## 11. What a high-resolution remake should evaluate exactly

- **Projection**: float vertices (no truncation of X, Y; no `>> 10` rounding), exact sin / cos (the table has
  amplitude 1022 and is one step late after its first quarter; each rotation scales by 1022/1024: 0.6 % over the
  three, keep or ignore), the heights z = amp/512 · 1022 sin(2π (r²/1024 + phase)/4096) with amp, phase interpolated
  between callbacks. Vertices behind the camera never show; a near-plane clip at depth 1 is enough.
- **Lines**: keep the blob structure, it is the look: per edge the same blob positions (start at the end with the
  smaller y, n = floor((max(|dx|, |dy|) + 1) / 2) blobs at start + i·d/n, i = 0..n − 1, computed from the float end
  points) and a continuous version of the 8x8 stamp scaled by the resolution factor, centred at the position + (3.5, 3.5)
  (the original centres it 4 pixels right of and below the truncated position), summed into a float intensity. A
  bilinear reading of the 8x8 table centred there, or a Gaussian σ = 1.18 pixels with peak 17.2 plus a floor of 0.78
  inside the 8x8 square (rms error 0.32), reproduces it. This keeps the bright knots at the vertices, the dimmer
  diagonals and the brightness of the folds. Drop the bounds artefacts: blobs partly outside the screen are drawn
  (the light reaches the edges), and the count is not taken mod 256.
- **Intensity → colour**: look up the palette ramp with the continuous intensity (noise + blobs), clamped at the top;
  the byte wrap never occurs, so no wrap. The fade is (b/64)·palette 1, the cross-fade the linear mix, with b
  continuous (interpolated between callbacks).
- **Noise**: a new field of independent, uniform intensities 0..31 per drawn frame (one grain = one original pixel),
  under the lines, not on top of them; replaying the generator only makes sense at 320 x 200.
- **Captions**: the font (redrawn, scope "core") at the positions of section 6, over everything, through the same
  palette (they fade with it).

## 12. Open points

- Whether the timer copy 0x4d71c..0x4d823 is byte-identical to the first part's 0x14bec..0x14cf3 is left to the
  core scope (the part only needs it to call 0x5355a once per retrace).
- The meaning of the dead code (the copy of [0x4dbfc], the discarded third swing angle) is a guess: left-overs of an
  earlier version in which the tilt also swung.
