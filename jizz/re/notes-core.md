# Scope "core": main's set-up, buffers, precalc tables, random generator, frame loop, colour tables, flash, fades and the shared 2D helpers

All addresses are offsets in `jizz_img.bin` (= near pointers = linear addresses in the emulator). "Page" = the
320x200 16-bit page being drawn, `[0x2060]`, 640 bytes per line. "Timer" = xmpGetTimer (0x29f44, PIT clocks of
1,193,182 Hz since the music start). "Frame" = one drawn and flipped page (one pass of a part's loop). Pixel values
are given for the 5:6:5 mode the intro uses (R bits 15..11, G 10..5, B 4..0); the code reads the field sizes and
positions from the VBE mode info, so 5:5:5 works the same way. Music positions are order:row in hexadecimal, as in
NOTES.md's sync table (PARTS.md writes orders in decimal: its "11:00" is 0b:00 here).

What was checked, and how (scripts in `jizz/re/models/core/`, see its README.md):

- precalc tables 0x6592, 0x6b16, 0x6cc0, 0x10fa2, 0x16f4f, 0x11dfe and the marching-cubes case table 0x6cdd:
  Python models reproduce the t430 snapshot exactly (0 differences in every table; `precalc.py`);
- random generator 0x141f6: the model reproduces the 80 numbers of 0x11dfe and every RNG state seen in the
  snapshots (`rng.py`, `precalc.py`);
- colour tables 0x1a336 / 0x1a236 (all five parameter sets used in the intro) and 0x11f73, and the converters
  0x13d73 / 0x13eec: exact against snapshots (`colour.py`); the flash: the tables of all 556 frames of a run through
  the 03:10 flash match the model, F per frame = 255, 246, ..., 3, 0 (`colour.py --flash`);
- fade tables 0x1131b (C = white and C = black) exact; the timer fade 0x17979 / 0x17999 / 0x179f6: the model
  reproduces 130 of 130 captured frames of the end-picture fade (ticks 9040..9169 of `full.frames`) bit for bit
  (`fade.py`);
- the "glow line" pipeline (0x183a9 + 0x1145c lines with the RNG, >> 3, 0x11eb2 blur, 0x12111 trail, 0x11396
  fade-to-white): bit-exact against buffer dumps of 17 frames of 0x17003 and 0x169e7 (`glow.py`);
- buffer layout: all 36 pointers checked against the t430 snapshot (`layout.py`).

---------------------------------------------------------------------------------------------------------------

## 1. The pixel path in one page

```
 planar texture (0x30000: 256x256 R plane, G plane, B plane, 8 bit)          built by 0x12c50 / 0x12e72 (textures)
   |-- 0x13d73 in place --> 256x256 16-bit 5:6:5 texture   (2D parts, warps: the texel is copied as is)
   |-- 0x13eec in place --> 256x256 interleaved R,G,B bytes (engine: env / texture mapping)
                               |
                               v   per channel: index = texel byte + shade byte (0..510)
             768-entry colour tables 0x1a479 / 0x1b079 / 0x1bc79 (0x1a336):
             clamp((index * S >> 8) + E, 0, 255) -> flash (lerp towards 255 - v by F/256) -> 5:6:5 field bits
                               |   pixel = R[...] | G[...] | B[...]
                               v
 8-bit 320x200 buffer [0x652a] --LUT 0x11eef (0x11f73)--> page            (TBL part only)
 8-bit glow buffer [0x652a]+0x10000 --0x11396: lerp page pixel towards C by v/32--> page
                               |
          page (320x200 16-bit) --0x179f6: lerp towards C by level/32 (timer fade)--> page --flip (0x651f)--> screen
```

There is no palette and no global brightness on the final page except these: the colour tables (brightness E,
contrast S, inversion F) apply only to what the vector engine draws (and the unused 0x16e36); the fade tables
(0x1131b) apply to finished pages (0x179f6, 0x11396).

---------------------------------------------------------------------------------------------------------------

## 2. main (0x186bd): set-up and buffer layout

Order: DS/ES = 0x18; EE02 → [0x2024] selector base; banner; 0x56c3 VBE detection; 0x5a50 mode choice; FPU init with
precision control 0 (24-bit single precision, 0x18712); EE42 allocates 0x8137c0 bytes → [0x204e] = base, zeroed
(0x204df0 dwords); the pointers below; precalc 0x6592, 0x6b16, 0x6cdd, 0x10fa2, 0x11dfe, 0x16f4f, 0x180e5, 0x18228
(the last two are parts3d-a's); command tail (dump modes); colour masks and 0x1f5b8; music; GUS; 0x12c50 textures;
0x6305 mode set; xmpPlay(0); the parts 0x12013 .. 0x10fda; 0x17979(0x200b20); end loop 0x18c25 (copy of the end
picture [0x657a], 128000 bytes, to the page, 0x17999, flip) until the sync table ends.

### 2.1 Buffers (offset from the base [0x204e]; in the emulator base = 0x100000)

| pointer | offset | size | what it is (users) |
|---|---|---|---|
| [0x652a] | 0x000000 | 0x20000 | 8-bit work buffer, 320 bytes per line: +0 = line / glow buffer, +0x10000 = trail buffer (§8.6). 0x11dfe fills all of it with 3; 0x17727 clears it (0x169e7 start); TBL (0x11986, 0x120b6) and 0x17003 / 0x169e7 (via [0x11207]) use it |
| [0x6546] | 0x020000 | 0x100000 | music: crunched module copied here and expanded by 0x1632b; MXMPlay data block at +0xfc000 (notes-music) |
| [0x654a] | 0x120000 | 0x10000 | 256x256 bytes "spot" table (0x6592, §3.1). Main sets [0x18d54] = 0x654a (the address of this pointer), presumably the base of the engine's map list 0x654a, 0x654e, ... (notes-engine). Users 0x11e33 (TBL), 0x16dc8 |
| [0x654e] .. [0x658a] | 0x130000 + k*0x30000, k = 0..15 | 0x30000 each | 16 planar 256x256 R,G,B textures, filled by 0x12c50 in the order map14 [0x6582], map1 [0x654e], map1a [0x658a], map2 [0x6552], map3 [0x6556], map4 [0x655a], map5 [0x655e], weirdtxt [0x6562], map7 [0x6566], map8 [0x656a], map9 [0x656e], testshit [0x6572], logo2 [0x6576], logo1 [0x657a], map10 [0x657e], map14 again [0x6582], map15 [0x6586] (the `/pandora` order; which script makes which is notes-textures' subject). [0x657a] ends up holding the 320x200 16-bit end picture. Some are converted in place by 0x13eec / 0x13d73 when a part starts (§8.2) |
| [0x658e] | 0x430000 | 0x20000 | copy of [0x1242d] made after the logo1 script (0x12d44): a 16-bit picture used by 0x1212a / 0x1239b (notes-textures, notes-parts2d) |
| [0x10fd2] | 0x450000 → rounded up to 0x450100 | 0x13000 | marching-cubes case table, 256 x 128 bytes (0x6cdd aligns the pointer to 256 and writes it back; §3.6). Read by the blob code 0x6e74 |
| [0x10fd6] | 0x463000 | 0x82000 | blob field / cell grid (0x6e3d, 0x769f; notes-parts3d-b) |
| [0x1130f], [0x11313], [0x11317] | 0x4e5000, 0x4e7000, 0x4e9000 | 0x2000 each | fade tables R, G, B: 32 rows of 64 dwords (§7.1) |
| [0x12425] .. [0x12435] | 0x4eb000 + k*0x30000, k = 0..4 | 0x30000 each | scratch textures: [0x12425] texture-builder work buffer (cleared by 0x12e72) and the parts' 16-bit texture copies; [0x12429], [0x1242d], [0x12431] more copies (0x177bf, 0x17762, the logo1 script); [0x12435] scratch of the in-place converters (0x13f84) and of the TGA writer 0x14258 |
| [0x145dc] | 0x5db000 | 0x15ede0 | vector-engine work memory, carved up by 0x14567 into [0x18d4c] (+0), [0x18d48] (+0xbb800), [0x18d50] (+0x11b340), [0x18d40] (+0x11c2e0), [0x18d44] (+0x137860), [0x1480c] (+0x152de0) (notes-engine) |
| [0x15dce] | 0x739de0 | 0x10000 | work buffer of the music decruncher 0x1632b (0x15ede, 0x1601d, 0x162cd; notes-music) |
| [0x16f4b] | 0x749de0 | 0xc8000 | polar table 600 x 340 dwords (0x16f4f, §3.5). Users 0x16cd3, 0x16d43, 0x16dc8, 0x17b2f, 0x17f66 |
| [0x179f2] | 0x811de0 | 0x200 | per-angle table rebuilt per frame by 0x123df (0x1212a) and 0x178c4 (0x177bf), read by the warps 0x16dc8 / 0x16d43 (notes-parts2d) |
| [0x18005] | 0x811fe0 | 0x12e0 | objects of 0x18009 / 0x180e5 (notes-parts3d-a) |
| [0x181f0] | 0x8132c0 | 0x500 | 40 quads of 4 corners (int32 x, int32 y), 32 bytes each, built by 0x18228 (notes-parts3d-a), drawn by 0x183a9 (§8.5) |

Total 0x8137c0 = exactly the allocation.

### 2.2 Video and colour variables set in main

| variable | value (5:6:5) | meaning |
|---|---|---|
| [0x18d64], [0x18d68], [0x18d6c] | 320, 200, 640 | viewport width, height, bytes per line (the engine's; parts change width / height / [0x15c3b] offset for insets) |
| [0x18d70], [0x18d74], [0x18d78] | 5, 6, 5 | R, G, B field sizes (from the VBE mode, [0x2082..0x2084]) |
| [0x18d7c], [0x18d80], [0x18d84] | 11, 5, 0 | R, G, B field positions ([0x207f..0x2081]) |
| [0x18d88] | 0xff | engine parameter (read by 0x18dc4) |
| [0x6380..0x6382] | 5, 5, 5 | "5 bits per channel" view of the pixel used by the fades: |
| [0x6383..0x6385] | 11, 6, 0 | position of the top 5 bits of each field (position + 1 when the field has 6 bits) |
| [0x6370], [0x6374], [0x6378] | 0xf800, 0x07c0, 0x001f | masks of those 5-bit fields |
| [0x637c] | 0xffdf | OR of the three = "white" (R 31, G 62, B 31): the colour of 0x17737 and 0x1151c |

0x6305 (called with AL = 0): [0x2064] = 320, [0x2068] = 200, [0x2060] = LFB; a 0x26400-byte system buffer only for
banked or 640-wide modes; 4F02 via 0x5a04; [0x205c] = 1.

---------------------------------------------------------------------------------------------------------------

## 3. Precalc tables

All maths in 24-bit single precision; fistp rounds to nearest (even). The models round after every operation.

### 3.1 0x6592: spot table, [0x654a], 256x256 bytes

```c
for (j = 0; j < 256; j++) {               // row
    float y = -128 + j;                    // [0x661b], starts at -128 (0x6613)
    for (i = 0; i < 256; i++) {
        float x = -127 + i;                // [0x6617] is incremented BEFORE use: columns -127..128
        float v = 255.0f - sqrtf(x*x + y*y);           // 0x661f = 255
        v = (v*v) * (1/256.f); v = (v*v) * (1/256.f);   // 0x6623 = 1/256
        spot[j][i] = (uint8_t)lrintf(v);               // = (255 - r)^4 / 2^24, peak 252 at (i,j) = (127,128)
    }
}
```

A plain double-precision `round((255 - r)^4 / 2^24)` gives the same 65536 bytes. r <= 181, so no wrap. The
centre sits at column 127, row 128 (not 128,128). For the remake: f(r) = 252.02 * (1 - r/255)^4 with r in texels of
a 256-texel map.

### 3.2 0x6b16: 1/sqrt table, 0x671a (dwords)

`R[i] = lrintf(4096 / sqrtf(i))` for i = 1..255, stored at 0x6716 + 4*i; the dword at 0x6716 is the static constant
4096 (= R[0] as the code indexes it). Only user: 0x662f (called once, from the blob normals 0x7a85), an integer
`65536 / sqrt(x)` with range reduction: it picks a shift so that the index x >> 2k lies in 64..255 (or scales small x
up), reads R and shifts the result back:

```c
uint32_t rsqrt16(uint32_t x) {        // 0x662f, ~ 65536 / sqrt(x), 6..8 significant bits of index
    if (x >= 0x400000) return R[x >> 16] >> 4;   if (x >= 0x100000) return R[x >> 14] >> 3;
    if (x >= 0x40000)  return R[x >> 12] >> 2;   if (x >= 0x10000)  return R[x >> 10] >> 1;
    if (x >= 0x4000)   return R[x >> 8];         if (x >= 0x1000)   return R[x >> 6] << 1;
    if (x >= 0x400)    return R[x >> 4] << 2;    if (x >= 0x100)    return R[x >> 2] << 3;
    if (x >= 0x40)     return R[x] << 4;         if (x >= 0x10)     return R[x << 2] << 5;
    if (x >= 4)        return R[x << 4] << 6;    return R[x << 6] << 7;
}
```

0x662f is not in any scope's list; it belongs to the blob surface code (parts3d-b). Remake: exact 1/sqrt.

### 3.3 0x6cc0 (called first by 0x6cdd): square table, 0x7b9e, 0x2000 dwords

`SQ[k] = k*k` (low 32 bits) for k = -4096..4095, entry k at 0xbb9e + 4k. Users: the blob field 0x77d5 (0x7a60..0x7a7e:
dx² + dy² + dz²).

### 3.4 0x10fa2: sine table, 0xfb9e, 0x500 dwords

`SIN[i] = lrint(1024 * sin(float(16*i) * 0.000383495197))`, i = 0..1279 (0.000383495197 = the double at 0x11596 =
2π/16384, so the argument is 2πi/1024; 1024 at 0x10f9e). COS(i) = SIN[i + 256] (0xff9e). A double-precision
`round(1024 sin(2πi/1024))` differs in one entry (i = 889: -754 instead of -755, the exact value is -754.50017).
Users: only 0x7aae (blobs: 0x7b10, 0x7b1d, 0x7b4b, 0x7b74). Remake: sin / cos.

### 3.5 0x16f4f: polar table, [0x16f4b], 340 rows x 600 dwords

```c
for (j = 0; j < 340; j++) {                      // y = -169.5 .. 169.5   ([0x16ff6] = -169.5)
    float y = -169.5f + j;
    for (i = 0; i < 600; i++) {                  // x = -299.5 .. 299.5   ([0x16ff2] = -299.5)
        float x = -299.5f + i;
        int a = lrintf(atan2f(y, x) / 0.0245436933f * 2.0f);  // 0x141e2 = 2π/256 (float), 0x16ffa = 2
        int r = lrintf(sqrtf(x*x + y*y) * 1.3f);             // 0x16ffe = 1.3
        P[j][i] = (a & 0xff) | (r & 0xff) << 8;              // fistp to [0x2004] then to [0x2005]: two low bytes
    }
}
```

Each entry is a 16-bit offset into a 256x256 map: column u = angle with 256 units per half turn (so the map repeats
twice around the centre), row v = 1.3 * radius, both modulo 256. The table is the screen (320x200) plus a margin
of 140 columns and 70 rows on each side: the users start at [0x16f4b] + 0x29270 (= row 70, column 140, the centred
window) plus an offset to move the centre. Exact against the snapshot. Remake: evaluate u = 256 * atan2(y, x) / π
and v = 1.3 * sqrt(x² + y²) per pixel (continuous, filtered texture reads), x, y in 320x200 pixel units from the
centre.

### 3.6 0x6cdd: marching-cubes case table, [0x10fd2] (aligned up to 256), 256 x 128 bytes

Static source: 0x28ed8, 2048 bytes = 256 cases x 8 bytes (in the image just before MXMPlay). Byte 0 bits 0..2 =
number of triangles n; the edge indices are 4-bit nibbles in the order: byte 0 high nibble, byte 1 low, byte 1 high,
byte 2 low, ... (3n of them). Output per case c (32 dwords at [0x10fd2] + 128c):

```c
out[0] = n;
if (n) {
    for (k = 0; k < 3n; k++) out[1 + k] = EDGE[nibble k];       // EDGE = 16 dwords at 0x6e0c: code addresses
    int m = 1 + 3n; out[m] = 0;                                   //   0x7144, 0x71a6, ... (12 used) = edge e
    // neighbour faces to visit: a face is listed when its 4 corners are neither all inside nor all outside
    static const struct { uint8_t mask; uint32_t routine; } F[6] =
        { {0xf0, 0x70e3}, {0x0f, 0x7113}, {0x33, 0x70aa}, {0xcc, 0x7079}, {0x55, 0x701d}, {0xaa, 0x704b} };
    for (f = 0; f < 6; f++) if ((c & F[f].mask) != 0 && (c & F[f].mask) != F[f].mask) out[m + 1 + out[m]++] = F[f].routine;
}
```

c = corner mask (bit k = corner k inside). The edge and face routines (0x701d..0x7618: interpolate the iso level
0x80 along an edge, recurse into a neighbour cell) are the blob code's (notes-parts3d-b). For a remake the table is
the classic triangle table: read the 2048 bytes at 0x28ed8 and keep edge indices 0..11 and face indices 0..5 instead
of code addresses. Exact against the snapshot.

### 3.7 0x11dfe: initial TBL data

```c
memset32([0x652a], 0x03030303, 0x8000);    // the whole 128 KB work buffer = 3
for (i = 0; i < 40; i++) { T11666[i] = rnd(); T115c6[i] = rnd(); }   // 0x141f6 from the image seed (0, 0, 0)
```

0x11666[40] are the TBL sparks' random velocities (TBL reads them as signed words at 0x11662 + 4k / 0x11664 + 4k,
k = 40..1); 0x115c6[40] is overwritten every frame by TBL (positions), so only its first frame sees the random
values. Remake: run the generator from (0, 0, 0): the 80 numbers are reproduced exactly (`precalc.py`).

---------------------------------------------------------------------------------------------------------------

## 4. Random generator 0x141f6

State: three dwords a = [0x141ea], b = [0x141ee], c = [0x141f2], all 0 in the image. Returns EAX (also leaves EBX,
ECX, EDX).

```c
uint32_t rnd(void) {                                   // all uint32 arithmetic, rol = rotate left
    uint32_t eax = a + 0x0e372471, ebx = b + 0x00bc5ea9, ecx = c + 0x00c8ade7, edx;
    eax ^= ebx;
    int64_t p = (int64_t)(int32_t)eax * (int32_t)ecx;  // imul ecx: SIGNED 32x32 -> 64
    eax = (uint32_t)p; edx = (uint32_t)(p >> 32);
    edx = rol(edx, 13) + eax;
    ecx = rol(ecx, 14);
    edx -= ebx;
    eax ^= edx;
    ebx += ecx;
    ecx ^= edx;
    eax -= ebx;
    ebx = rol(ebx, 9);
    uint64_t q = (uint64_t)eax * ecx;                  // mul ecx: UNSIGNED
    eax = (uint32_t)q; edx = (uint32_t)(q >> 32);
    ebx ^= edx;
    eax += 0x01e42ac3; ebx += 0x005edf04; ecx += 0x011a2ec1;
    a = ebx; b = ecx; c = eax;                         // note the rotation of the state words
    return eax;
}
```

Seeds and callers (one sequence through the whole intro):

| when | caller | calls |
|---|---|---|
| precalc | 0x11dfe | 80, from state (0, 0, 0) |
| each texture script | 0x12e72 sets the state to (0x1234abce, 0x03049291, 0x67fcd9a8) first; script ops 0x132f1, 0x1356a (two), 0x13b5c (two), 0x13c5b call it | per texture (notes-textures). After the last script (map15, 0x12bd9 into [0x6586]) the state is (0x44273c73, 0x19e7ee6a, 0xd3a3c270) = the seed advanced 131,070 times (measured from the t430 snapshot) |
| 0x17003, event 300 (03:00..03:10) | 0x183a9 via 0x17355 | 1 per drawn edge = 4 per quad per frame (§8.5); 620 calls in the capture (state at t2515 = seed advanced 131,690 times) |
| 0x169e7, event 510 (05:10..05:18) | 0x183a9 via 0x16b7c | same; 1,076 calls in the capture (state from t3285 to the end = seed advanced 132,766 times) |
| never | 0x16079 in 0x1601d (music decruncher) | only when byte [0x15e38] == 4; [0x15e38] is 0 and never written |

So the only random numbers visible after the precalc are the jitter of the 0x183a9 rectangles, and how many are
drawn depends on the frame rate (per frame). A remake can replay the generator from the state after the textures
(advance the texture seed 131,070 times, or run the texture generator) and draw one set of jittered edges per
remake frame, or per frame of a fixed rate (25.5 fps in event 300 and 34.5 fps in event 510 in the capture).

---------------------------------------------------------------------------------------------------------------

## 5. The frame loop

Every part frame is: `call 0x638b` (frame start) ... draw into [0x2060] ... `call 0x651f` (flip) `call 0x2058`
(key test: `or al, 1; ret`, never a key).

### 5.1 0x638b: frame start

```c
page = ([0x2052] + 1) & 3;  [0x2052] = page;                // 4 pages in video memory
[0x2060] = LFB + page * 0x1f400;                            // 0x5e1d (LFB, 320 wide: [0x2085] >> 1 == 0)
// flash decay (per frame)
if (F < dec) { F = 0; dec = 0; } else F -= dec;             // F = byte [0x1a478], dec = byte [0x6386]
if (F != lastF) { lastF = F; colour_tables(E = [0x6387]); } // lastF = byte [0x63d8]; 0x1a229; [0x6387] is always 0
// sync: at most ONE table entry per frame
pos = xmpGetPos();                                          // 0x299c4: AH = order, AL = row
if (pos >= sync[idx].pos  ||  key == 0x39 /* [0x205b], never written */) {
    event = sync[idx].event;  [0x6413] = event;             // sign-extended word
    if (event == -1) goto 0x18c59;                          // xmpStop, text mode, credits, exit
    idx++;                                                  // [0x651b] += 4
}
```

The sync table: 64 entries of 4 bytes (word order << 8 | row, signed word event) at 0x6417..0x6516, ending with
event -1; [0x651b] = 0x6417 initially. Because one entry is taken per frame, the event can lag the music by one
entry: in the capture the index was one entry behind the position in 91 of 8923 ticks, never two.

0x638b does not clear the page: what a part does not draw keeps the content of four frames earlier (the parts draw
full pages: TBL's LUT copy covers all 64000 pixels, the end loop copies the full picture; for the other parts see
their notes).

### 5.2 0x651f / 0x5e44 / 0x5e71 / 0x5d2e: flip (16-bit LFB path)

0x651f: `0x5e44(page)`; 0x5e44 with [0x2085] >> 1 == 0 goes to 0x5e71: CX = 0 (first pixel), DX = page * 200 (first
line), 0x5d2e: VBE 4F07 with BL = 0x80 - [0x205f] = 0x80 (set display start during vertical retrace) through the
protected-mode interface [0x2086] if present, else int 31h/0300 → int 10h. [0x205f] = 0, so no port-0x3da polling.
The flip waits for the retrace: at most one frame per refresh (70 fps in the capture's end loop).

---------------------------------------------------------------------------------------------------------------

## 6. Colour tables and the flash

### 6.1 0x1a336(E): the 768-entry tables used by the vector engine

Globals: S = [0x1a225] (contrast, 8.8 fixed point, 0x100 = 1.0), F = byte [0x1a478] (inversion), E = EAX
(brightness, signed, in 8-bit units).

```c
for (c = 0; c < 3; c++)                         // R 0x1a479, G 0x1b079, B 0x1bc79: 768 dwords each
    for (i = 0; i < 768; i++) {                 // the code accumulates i*S in EBX
        int v = (i * S + E * 256) >> 8;         // arithmetic shift
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        v = ((255 - F) * v + v + (255 - v) * F) >> 8;   // = ((256 - F) v + F (255 - v)) / 256: towards 255 - v
        T[c][i] = ((v << size[c]) >> 8) << pos[c];     // 5:6:5: (v >> 3) << 11, (v >> 2) << 5, v >> 3
    }
```

Readers: the engine's span fillers 0x1e77c (0x1e816 / 0x1e82e / 0x1e846, 0x1e8fe..) and 0x1f07c (0x1f190..,
0x1f2c4..): `pixel = T_R[shade + texel.r] | T_G[shade + texel.g] | T_B[shade + texel.b]`, texel from an interleaved
RGB texture (0x13eec), shade a byte (≤ 255) from a map (notes-engine). (0x1e77c adds instead of ORs: same, the fields
are disjoint.) So the index is texel + shade, 0..510, and the tables implement additive light with saturation,
contrast, brightness and inversion in one lookup. Also read by 0x16eea, in code (0x16e36..0x16f34) that nothing
calls.

For the remake, per channel (0..1 floats): `x = clamp((texel + shade) * S/256 + E/255, 0, 1);
x = mix(x, 1 - x, F/256)`; the 5/6-bit quantisation is dropped.

### 6.2 0x1a236: the 512-entry tables (unused)

Same without S and E: `T5[c][i] = quantise(flash(min(i, 255)))`, i = 0..511, at 0x1c879 / 0x1d079 / 0x1d879. No
instruction references them and no reads happened in sample runs of every part (hooked runs of 120 ticks from each
snapshot): built, never used. A remake can ignore them.

### 6.3 The entry points and every set of parameters used

0x1f5b8 = 0x1a236 + 0x1a336(0) (main, once); 0x1a229(E) = 0x1a236 + 0x1a336(E) (the flash); 0x1f5c5(E) =
0x1a336(E) (one-line wrapper, listed in the engine's scope).

| when | call site | E | S | F | what the engine parts look like |
|---|---|---|---|---|---|
| precalc | 0x189cb → 0x1f5b8 | 0 | 0x100 | 0 | – |
| 0x17003 start (01:00) | 0x1704e → 0x1f5c5 | -200 | 0x1c8 (1.78) | 0 | dark, contrasty ribbons until 03:10 |
| 0x17003 event 311 (03:10) | 0x17207 → 0x1a229, with dec = 9 | 0 ([0x6387]) | 0x100 | 255 | negative, fading back in 30 frames; afterwards plain tables (E 0, S 1) |
| every frame while F changes | 0x63d1 → 0x1a229 | 0 | 0x100 | F | |
| 0x169e7, 0x15c8b | (none) | 0 | 0x100 | 0 | plain |
| 0x15d20 start (09:00) | 0x15d48 → 0x1f5c5 | -40 | 0x100 | 0 | |
| 0x10fda, every frame | 0x11085 → 0x1f5c5 | -50 | 0x132 (1.195) | 255 while event == 0x1400 (14:00..14:10), else 0 | negative during 14:00..14:10 |

S is set to the value just before the call and set back to 0x100 right after it (0x17053, 0x1108a), so the flash
rebuilds of 0x638b always use S = 0x100 and E = 0. That is why the ribbons of 0x17003 change from the E -200 / S 1.78
look to the plain look at the 03:10 flash and keep it (visible in the capture). Parts that do not use the engine
(TBL, 0x1212a's 2D layers, 0x177bf, 0x17c8a, end loop) are not affected by these tables.

### 6.4 The flash

Not a white flash: an inversion. F = 255 turns every engine pixel into its negative (v → 254 - v roughly); the
capture shows the inset ribbons of 0x17003 turning green / blue at 03:10 (tick 2225) and the whole blob scene
negative during 14:00..14:10 (ticks 8656..8751, where the background is engine-drawn too). The 03:10 flash decays
by `dec` = 9 per frame in 0x638b: F = 255, 246, ..., 12, 3, 0 (30 frames; verified frame by frame against a run).
**Time base: per drawn frame** (35 fps in the capture: 0.85 s). The 14:00 inversion is set every frame from the
event (dec = 0), so it is not frame-rate dependent.

### 6.5 0x11f73: the TBL palette (32 dwords at 0x11eef)

Base colour b = bytes [0x11f6f], [0x11f70], [0x11f71] (R, G, B, 0..255), level L = signed byte [0x11f72].

```c
for (i = 0; i < 32; i++) {
    int l = clamp(i + L, 0, 31);
    uint32_t px = 0;
    for (c = 0; c < 3; c++) {
        int a = min(255, b[c] + 8 * l);                 // add with carry -> 0xff
        px |= ((a * l) >> (13 - size[c])) << pos[c];    // 5-bit: (a*l) >> 8, 6-bit green: (a*l) >> 7
    }
    LUT[i] = px;
}
```

In 0..1 terms: colour(i) ≈ min(1, b/255 + 8l/255) * l/32: black at l = 0, the base colour rising to white at the
top. Used by TBL: page[k] = LUT[[0x652a][k]] for 64000 pixels (0x120b5). Callers: 0x120b0 (TBL, every frame,
L = [0x1210d] >> 3, b from the colour pulse 0x13f98: notes-parts2d) and 0x169ee (0x169e7 start, L = 0; nothing
reads the LUT after TBL, so this call has no effect).

---------------------------------------------------------------------------------------------------------------

## 7. Fades

### 7.1 0x1131b: fade tables towards a colour C

C = ([0x11303], [0x11307], [0x1130b]), 5 bits per channel (0..31). 0x11358 builds one channel:

```c
// tab_c = [0x1130f] (R), [0x11313] (G), [0x11317] (B); shift_c = [0x6383 + c] = 11, 6, 0 (self-modified shl)
for (r = 0; r < 32; r++)                     // the code counts b = 31 - r down from 31
    for (a = 0; a < 32; a++)
        tab_c[r * 64 + a] = ((a * (31 - r) + C_c * r) >> 5) << shift_c;   // entries 32..63 of a row unused
```

Row r maps a 5-bit channel value a to lerp(a, C, r/32), truncated: row 0 is a * 31/32 (one step darker, not the
identity), row 31 is C * 31/32. Green is handled as its top 5 bits (mask 0x07c0, output shifted by 6), so a faded
pixel always has green bit 5 = 0.

| caller | C | used by |
|---|---|---|
| (image) | 31, 31, 31 (white) | 0x1703a (0x17003 start) builds the tables from the image values |
| 0x12157 (0x1212a start, 08:00) | 0, 0, 0 (black) | 0x179f6 calls of 0x1212a |
| 0x15d3e (0x15d20 start) | 31, 31, 31 | |
| 0x177e4 (0x177bf start) | 31, 31, 31 | |

So every timer fade and every glow is towards white, except in 0x1212a (towards black).

### 7.2 0x179f6(level): fade the whole page

```c
// level = EAX & 31; page = [0x2060], 320x200 (two pixels per dword, same per pixel)
for each pixel p:
    p = R[level][(p & 0xf800) >> 11] | G[level][(p & 0x07c0) >> 6] | B[level][p & 0x001f];
```

(Masks, shifts and table addresses are patched into the loop at 0x17aa7..0x17b1a.) Callers: 0x179d0 (0x17999) and
0x1236e (0x1212a's own pulse, level computed by the part: notes-parts2d).

### 7.3 0x17979 / 0x17999: timer fade from C to the picture

```c
void fade_start(uint32_t rate) {            // 0x17979, EAX = rate
    fade_rate = rate;                        // [0x179de]
    fade_t0 = xmpGetTimer() + [0x6542];      // [0x179d6]
    fade_level = 0x3ff;                      // [0x179da]
}
void fade_apply(void) {                      // 0x17999, after the part drew the page, before the flip
    if (fade_level == 0) return;             // idle
    int32_t t = xmpGetTimer() + [0x6542] - fade_t0;
    int lv = t < 0 ? 0 : 0x3ff - (int)(((uint64_t)(uint32_t)t * fade_rate) >> 32);
    fade_level = lv < 0 ? 0 : lv;
    page_fade(fade_level >> 5);              // 0x179f6: row 31 = C ... row 0
}
```

**Time base: the timer** (PIT clocks since fade_start; [0x6542] cancels out). level(t) = 1023 - t * rate / 2^32, a
linear fade from C to the picture in 32 steps lasting 1023 * 2^32 / rate clocks. The frame in which the level
reaches 0 is still drawn with row 0 (a * 31/32); after that nothing is applied. Verified bit for bit on the end
picture (130 captured frames).

| call | part / condition | rate | duration | from |
|---|---|---|---|---|
| 0x17100 | 0x17003, event 201 (02:00), after one white page (0x17095) | 0x2c4020 = 2,900,000 | 1.27 s | white |
| 0x17120 | 0x17003, event 301 (03:00) | 0x2dc6c0 = 3,000,000 | 1.23 s | white |
| 0x15ced | 0x15c8b start (06:00), after one white page | 0x4ac51b = 4,900,123 | 0.75 s | white |
| 0x15d93 | 0x15d20 start (09:00) | 0x4dd47a = 5,100,666 | 0.72 s | white |
| 0x178a7 | 0x177bf, whenever the event is not 0xb00 (the part then sets 0xb00): event 2817 (0b:00) | 0x28c0d0 = 2,670,800 | 1.38 s | white |
| 0x178a7 | 0x177bf, event 3072 = 0xc00 (0c:00, 0c:20) | 0x3d0900 = 4,000,000 | 0.92 s | white |
| 0x178a7 | 0x177bf, event 3088 = 0xc10 (0c:10, 0c:30) | 0x380310 = 3,670,800 | 1.00 s | white |
| 0x17cd6 | 0x17c8a start (0d:00), after one white page | 0x2d9686 = 2,987,654 | 1.23 s | white |
| 0x17cfb | 0x17c8a, event 0x1000 (10:00; the part sets 0x1001) | 0x249f00 = 2,400,000 | 1.54 s | white |
| 0x11030 | 0x10fda start (11:00) | 0x325aa0 = 3,300,000 | 1.12 s | white |
| 0x1104b | 0x10fda, event 0x1301 (13:00; the part sets 0x1300) | 0x36ee80 = 3,600,000 | 1.02 s | white |
| 0x18c20 | end picture (15:00) | 0x200b20 = 2,100,000 | 1.75 s | white |

(Durations = 1024 * 2^32 / rate / 1,193,182.) 0x17999 is called every frame by 0x17003 (0x1746b), 0x15c8b
(0x15d09), 0x15d20 (0x15daf), 0x177bf (0x1795b), 0x17c8a (0x17fdf), 0x10fda (0x11130) and the end loop (0x18c46).
Remake: page = mix(C, picture, 1 - level/1024) with the level from the timer (continuous instead of 32 steps).

---------------------------------------------------------------------------------------------------------------

## 8. Shared 2D helpers

### 8.1 Page fills

- **0x17737** fills the page (320x200) with [0x637c] = 0xffdf (white). Callers: 0x1212f (0x1212a start),
  0x15c90 (0x15c8b start), 0x17095 (0x17003 at event 201), 0x17c8f (0x17c8a start); each is followed directly by a
  flip: one white frame, then the part's first picture (with a fade from white in the three engine cases).
- **0x17727** clears the 128 KB work buffer [0x652a] (line + trail buffers). Caller: 0x16a45 (0x169e7 start).

### 8.2 Texture converters (in place, through the scratch buffer)

- **0x13f84** (EDI = buffer): copies 0x30000 bytes to [0x12435]; returns ESI = [0x12435], EDI unchanged.
- **0x13d73** (EDI = planar texture): 256x256 planar R, G, B bytes → 65536 words in the first 128 KB:
  `px = (r >> (8 - 5)) << 11 | (g >> (8 - 6)) << 5 | (b >> (8 - 5))` (sizes / positions from [0x18d70..],
  patched into the loop). Callers: 0x121ea (0x1212a: [0x12425] = copy of [0x655e]), 0x13277 (texture script op),
  0x16a39 (0x169e7: [0x12425] = copy of [0x6556]), 0x1777e / 0x17789 (0x17762: [0x1242d], [0x12431]), 0x17815 /
  0x17820 (0x177bf: [0x12425], [0x12429] = copy of [0x6566]), 0x17cb8 (0x17c8a: [0x12425] = copy of [0x6586]).
- **0x13eec** (EDI = planar texture): → interleaved R, G, B bytes, 3 per texel, row-major 256x256 (the engine's
  texture format). Callers: 0x132a5 (texture script op), 0x15cb6 (0x15c8b: [0x6552]), 0x15d5a (0x15d20: [0x656a]),
  0x16a0c (0x169e7: [0x654e] = copy of [0x655e]). The planar original is lost, so a part that needs the texture
  twice copies it first.

Both verified exactly. Remake: keep textures as float RGB; these only change the storage.

### 8.3 0x1151c / 0x11566: white frame around the inset viewport

```c
if ([0x15c3b] == 0) return;                              // viewport byte offset in the page; 0 = full screen
uint16_t *p = page + [0x15c3b] - 0x282;                   // one line up, one pixel left of the viewport
uint32_t col = 0xffdf | 0xffdf << 16;
hline(p, col);  p += 320;                                // 0x11566: (w >> 1) + 1 dwords = w + 2 pixels if w even
for (k = 0; k < h; k++, p += 320) { p[0] = 0xffdf; p[w + 1] = 0xffdf; }
hline(p, col);                                           // w = [0x18d64], h = [0x18d68]
```

A 1-pixel white rectangle just outside the w x h viewport. With an odd width the two right corner pixels are not
drawn (dword fill); the 175 x 110 inset of 0x17003 shows this in the capture (tick 2230: corners at column 305 are
scene pixels). Callers: 0x16b99 and 0x16c1c (0x169e7; the second for the 170 x 95 inset at byte offset 0xc134 =
x 90, y 77), 0x172b6 (0x17003). Remake: a full rectangle, 1/320 of the width thick.

### 8.4 0x1122f / 0x1145c: thick lines into an 8-bit buffer

Parameters at 0x11207..: [0x11207] buffer, [0x1120b] bytes per line (320), [0x1120f] bytes per pixel (1),
[0x11213] width (320), [0x11217] height (200) (static except [0x11207] / [0x1120b], which 0x17003 sets to
[0x652a] / 320 and TBL sets before drawing); endpoints [0x1121b] x0, [0x1121f] y0, [0x11223] x1, [0x11227] y1.

```c
void line_f9(int x0, int y0, int x1, int y1) {          // 0x1145c (0x1122f does the set-up)
    if ((unsigned)x0 >= W || (unsigned)x1 >= W || (unsigned)y0 >= H || (unsigned)y1 >= H) return;  // no clipping
    if (y1 < y0) swap(p0, p1);
    int dy = y1 - y0, dx = abs(x1 - x0), sx = x1 < x0 ? -1 : 1;
    if (dx + dy == 0) return;                            // a single point draws nothing
    uint8_t *o = buf + y0 * 320 + x0;
    if (dy <= dx) {                                      // flat: dx + 1 columns, 4 rows thick (y .. y+3)
        uint16_t slope = min((dy << 16) / dx, 0xffff), f = 0;
        for (i = 0; i <= dx; i++) { o[0] = o[320] = o[640] = o[960] = 0xf9;
                                    o += sx; if ((f += slope) overflowed) o += 320; }
    } else {                                             // steep: dy + 1 rows, 4 columns thick (x .. x+3)
        uint16_t slope = min((dx << 16) / dy, 0xffff), f = 0;
        for (i = 0; i <= dy; i++) { *(uint32_t *)o = 0xf9f9f9f9;
                                    if (sx > 0) o += 320 + carry(f += slope);     // self-modified add / adc
                                    else        o += 320 - borrow(f -= slope); }  // sub / sbb
    }
}
```

Pixel i of a flat line is at y0 + floor(i * slope / 65536); steep lines going left step x on the first row already
(x0 - ceil(i * slope / 65536)), going right x0 + floor(...). The value 0xf9 becomes 31 after the >> 3 of §8.6.
Callers: 0x11c6a, 0x11d7a (TBL 0x11986), 0x180d7 (0x18009: the rings of 0x17003), 0x18443 (0x183a9).

### 8.5 0x183a9: jittered quads (EBP = jitter mask)

```c
for (n = [0x1821c]; n <= [0x18220]; n++) {               // quads at [0x181f0] + 32n: 4 x (int32 x, int32 y)
    for (k = 0; k < 4; k++) {
        uint32_t r = rnd();                              // one random number per edge
        int j = (k + 1) & 3;
        int xa = P[n][k].x + (r & m); r = ror(r, 4);     // m = EBP, patched into the four `and`s
        int ya = P[n][k].y + (r & m); r = ror(r, 4);
        int xb = P[n][j].x + (r & m); r = ror(r, 4);
        int yb = P[n][j].y + (r & m);
        line_f9(xa, ya, xb, yb);                         // 0x1145c into [0x11207]
    }
}
```

Callers (both m = 7, i.e. 0..7 pixels of jitter right/down per end point, new every frame):
- 0x17355, 0x17003 during event 300 (03:00..03:10): row = xmpGetPos row; row < 7 → quads 0..row, else quads
  min(row, 14) - 7 .. 7;
- 0x16b7c, 0x169e7 during event 510 (05:10..): k = (row & 15) * 2; k < 6 → quads 0..k, else min(k, 12) - 6 .. 6.

### 8.6 The glow pipeline: >> 3, 0x11eb2, 0x12111, 0x11396

The three users run the same chain on the 8-bit buffers B = [0x652a] (lines) and T = [0x652a] + 0x10000 (trail):

```c
// 1. lines (0xf9) were drawn into B this frame on top of last frame's values
for (k = 0; k < 64000; k++) B[k] >>= 3;                      // new lines -> 31, old content / 8
// 2. 0x11eb2(EDI = dst, EBP = src - dst - ...): 2x2 box, 62720 bytes (196 lines), 8-bit wrapping sums
for (k = 0; k < 0xf500; k++) dst[k] = (s[k] + s[k+1] + s[k+320] + s[k+321]) >> 2;   // s = dst + EBP;
                                                              // k = 0 uses only s[1] + s[321]
// 3. 0x12111(ESI = s, EDI = d, CL = n): 65536 bytes, d += (s - d) >> n in 8-bit arithmetic
for (k = 0; k < 0x10000; k++) d[k] = ((uint8_t)(d[k] << n) + (uint8_t)(s[k] - d[k])) >> n;
                                                              // = floor((d (2^n - 1) + s) / 2^n) while d, s < 32
// 4. 0x11396(ESI = v buffer, EDI = page): fade each pixel towards C by v/32
for (k = 0; k < 64000; k++) if (v[k]) page[k] = R[v[k]][..] | G[v[k]][..] | B[v[k]][..];   // rows of §7.1
```

All values stay below 32 (lines 31, blur and trail are averages), so the wrapping 8-bit arithmetic never wraps and
v is a valid table row. The in-place blur (src = dst) reads only bytes it has not written yet.

| user | lines | >> 3 | 0x11eb2 | 0x12111 | 0x11396 |
|---|---|---|---|---|---|
| TBL 0x11986 (notes-parts2d) | rays / sparks (0x11c6a, 0x11d7a) into [0x652a] | 64000 bytes (bytewise) | dst = [0x652a], src = T (EBP = 0x10000): the displayed buffer is the blurred trail | s = B, d = T, n = 3 (before the blur) | – (B goes through the 0x11eef LUT to the page) |
| 0x17003 (0x1739c..0x17408) | 0x18009 rings (events 130, 225, 230), 0x183a9 quads (event 300) | yes | in place on B, only when [0x17486] != 0 (event 300) | s = B, d = T, n = [0x17485]: 2 (events 130 / 225 / 230), 1 (event 300); skipped when 0 | v = T, C = white |
| 0x169e7 (0x16c51..0x16cab) | 0x183a9 quads (event 510) | yes | in place on B | n = 1 | v = T, C = white |

Order in 0x17003 / 0x169e7: lines, >> 3, blur, trail, fade; TBL: lines, >> 3, trail, blur (out of T into B), LUT.
**Time base: per drawn frame** (the >> 3 decay, the trail and the jitter). Capture rates: TBL 22.5 fps,
0x17003 events 130 / 225 / 230 35.5 fps, event 300 25.5 fps, 0x169e7 event 510 34.5 fps. As time constants of the
trail (frames → seconds, τ = -1 / (fps ln(1 - 2^-n))): TBL n = 3: 0.33 s; 0x17003 n = 2: 0.098 s; event 300 n = 1:
0.057 s; 0x169e7 n = 1: 0.042 s. This is frame-rate dependent state not listed in NOTES.md / PARTS.md (they tested
with the timer frozen from the part's first ticks, before these phases).

What it looks like: white (C) glowing rectangles / rings over the 3D picture, 4 pixels wide, softened by one 2x2 box
and a short afterglow; the rectangles flicker because their corners jump by 0..7 pixels every frame.

---------------------------------------------------------------------------------------------------------------

## 9. Time bases

| variable | where | time base |
|---|---|---|
| page number [0x2052], sync index [0x651b], event [0x6413] | 0x638b | per frame (event follows the music position, ≤ 1 table entry per frame) |
| flash F [0x1a478] (03:10) | 0x638b | per frame: -9 per frame, 30 frames |
| inversion during 14:00..14:10 | 0x10fda | event |
| timer fade level [0x179da] | 0x17999 | timer, from 0x17979's call |
| glow: line decay (>> 3), trail, rectangle jitter | 0x17003, 0x169e7, TBL | per frame |
| random generator state | 0x141f6 | per call: per frame through 0x183a9 |
| colour-table parameters E, S | part starts, 0x10fda per frame | constant per part |

---------------------------------------------------------------------------------------------------------------

## 10. Shortcuts a high-resolution remake should evaluate exactly

- 5-bit / 6-bit quantisation in all tables (colour tables, fade tables, TBL LUT): use float colour. The fade rows
  also drop green's lowest bit and make row 0 one step darker (a * 31/32); both are 16-bit artefacts.
- Timer fade: 32 steps (level >> 5) of a linear ramp → continuous mix(C, picture, level/1024).
- Spot table (§3.1), sine table, square table, 1/sqrt table and 0x662f: evaluate the functions.
- Polar table (§3.5): per pixel atan2 / sqrt at full resolution, with filtered reads of the 256x256 map (the table
  has 0.5-pixel centres and integer-rounded texel offsets).
- Lines of §8.4: integer DDA, 4 pixels thick extending right / down from the line, all-or-nothing rejection at the
  screen border → anti-aliased strokes 4/320 of the width, centred 1.5 pixels right/down of the nominal line
  (keep the offset or centre it: it is under 1% of the width), clipped instead of rejected? Rejection is visible
  only for jittered points pushed past the border (≤ 7 pixels); keep the rejection to match.
- The 2x2 box blur (variance 1/4 pixel² per axis, shifts the image by half a pixel up-left) → a Gaussian of the same
  variance at the remake's resolution; the trail EMA per frame → an exponential in time with the τ of §8.6, or a
  fixed simulation rate.
- Inset frame: full 1-pixel (scaled) rectangle including the corners missing with odd widths.

## 11. Not determined

- The 512-entry tables (0x1a236) look unused; a code path that computes their address at run time was not found
  statically and no read occurred in the sampled runs (120 ticks from each snapshot, not the whole intro).
- Why 0x169e7 calls 0x11f73 (no reader of the LUT after TBL): leftover, as far as can be seen.
- [0x6387] (the E of the flash rebuild) is never written; kept as a constant 0.
- How the texture scripts consume exactly 131,070 random numbers is notes-textures' subject (a measured count here).

## 12. Models (`jizz/re/models/core/`)

See README.md there. Commands (from `jizz/re`, inside the toolbox):

```
python3 models/core/layout.py        # buffer layout vs t430
python3 models/core/precalc.py       # precalc tables + RNG vs t430
python3 models/core/colour.py        # colour tables, TBL LUT, converters vs snapshots
python3 models/core/fade.py          # fade tables vs snapshots, end-picture fade vs full.frames
python3 models/core/run_jizz.py --load --snap snaps/t1365.snap --out models/core/runs/d1365 --ticks 2160 \
        --plugin models/core/plug_dump.py && python3 models/core/glow.py models/core/runs/d1365.dump.pickle
python3 models/core/run_jizz.py --load --snap snaps/t2515.snap --out models/core/runs/d2515 --ticks 3050 \
        --plugin models/core/plug_dump.py && python3 models/core/glow.py models/core/runs/d2515.dump.pickle
python3 models/core/run_jizz.py --load --snap snaps/t1365.snap --out models/core/runs/f1365 --ticks 2275 \
        --plugin models/core/plug_flash.py && python3 models/core/colour.py --flash models/core/runs/f1365.flash.pickle
```
