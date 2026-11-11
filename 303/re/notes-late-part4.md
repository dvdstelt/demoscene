## 4. Dance, Icons, Outro

Scratch: `scratchpad/re/late4/` (function listings `f*.s`, `lyrics.txt`, capture frames `tile*.png`,
`final.png` = final.raw as an image, `an.py`/`an.txt` = capture brightness per frame).
All times below are video times (VT) computed with pos.py, and checked against the capture
(`Acme - 303 ... .mp4`, 30 fps) where stated. Ticks = vertical-retrace timer callbacks (MIDAS `tmrSyncScr`,
0x371ec/0x374d0), 70.086 Hz in mode 13h.

### 4.0 Shared machinery used by these parts (only what matters here)

**Palette fade (per tick, 0x10124 called from the timer 0x1bc01):**
```c
// level=[0x52918], target=[0x5291c], speed=[0x8eba8], bright=[0x52920] (float), whiteMode=[0x8ebb0]
if (level == target) speed = 0;
if (level < target) level = min(level + speed, target);
if (level > target) level = max(level - speed, target);
bright = (bright - 1.0f) * 0.98 + 1.0f;          // flash multiplier decays to 1
setpal(level):                                    // 0x1019c, base palette at 0x5faa8 (6-bit)
  L = level >> 8;
  L == 64: out = min(63, trunc(p * bright));
  L <  64: out = min(63, trunc(((p * L) >> 6) * bright));        // fade to black, linear in DAC units
  L >  64: k = L - 65; out = min(63, trunc((k + ((p * (63 - k)) >> 6)) * bright));  // to white (whiteMode==0)
```
So `level 0x8000 -> 0x4000` = fade from white, `0x4000 -> 0` = fade to black; duration = 0x4000/speed ticks.

**Part position:** `[0x9b844] = 2*(order*64+row) - 2 + [0x149a04]`. Nothing in the code writes `[0x149a04]`
(only read at 0x133e4), so the position is always even and changes once per row.
`[0x9b840] = ([0x9b844] + 0x20) & ~0x3f` (low byte only, i.e. round to 32 rows).

**Start of this section (end of 0x2e8e4, brief):** the last sub-part of 0x2e8e4 (loop 0x3079a–0x308d7) starts at
`[0x9b840]=0xf80` (VT 203.57) with level 0x8000, target 0x4000, speed 0x46 (white fade-in); once
`[0x9b844] > [0x9b840]+0x28` (pos 0xfaa, VT 205.77) every frame sets target 0, speed 100; the loop runs while
`level > 0`. Level at 205.77 is about 0x8000 - 70*154 = ~22000, so the loop ends ~220 ticks (3.1 s) later,
**VT ~208.9**. Capture: black at 208.8–209.0, dance flash at ~209.1. (Exact value depends on load time;
see the parent's notes for 0x2e8e4.)

---

### 4.1 Dance: vs.lzw then alex.lzw (main script 0x31a13–0x31ffb)

**What it draws:** full-screen YIQ video clips (VS: the dancer; ALEX: people at PCs), 160x96 decoded, shown 2x
with a 4x4 ordered dither into a 256-colour YIQ palette, with the 0x2a34c lyric overlay combined on top.

**Pseudo-C (inlined video "effect"; the `==0x17`/`==0x48` branches in the loop are dead copies):**
```c
// ---- setup VS (0x31a13)
fx2a34c(0x17);                     // overlay init (clears [0x1473b8])
[0x1473b8] = 1;                    // overlay uses the *sung lyric* table (see below)
[0x580c0]  = 7;                    // video period: 7+1 ticks per frame
open_video("vs.lzw");              // 0x12ccc: open LZW stream, build dequant/IDCT + dither tables
yiq_palette();                     // 0x13244 -> 0x5faa8 and DAC (formula below)
Ybuf  = tempalloc(0x3c00);         // [0x1493b8]  (0x186e8 = bump allocator)
IQbuf = tempalloc(0x3c00);         // [0x1493bc]
frame = 0;                         // [0x1493c0]
add_tick_callback(0x30990);
want = 1;                          // [0x149a08]
load_fad("truecol.fad");           // used by the overlay combine
bright = 1.0; speed = 0; level = 0x8000; DAC[all] = 63;     // white
memset(screen, 0, 64000);
speed = 1000; target = 0x4000;     // white -> normal in 17 ticks (0.24 s)
memset(B = 0x6f7a8, 0, 64000);
[0x9b840] = ([0x9b844] + 0x20) & ~0x3f;                      // = 0x1000 in practice (see times)
do {
    fx2a34c(A = 0x5fda8);          // overlay layer (other agent)
    if (want) {
        want = 0;
        if (frame == 0) lzw_restart(stream, 1);              // 0x11308: rewind to first frame
        frame = (frame + 1) % 200;
        decode_plane(Ybuf, 160x96);                          // 0x13174: 0x3c00 bytes LZW -> 4x4 DCT (0x1b648)
        decode_planes(IQbuf, 2 x 80x48);                     // 0x131ac: I at +0, Q at +0xf00
    }
    dither_yiq(Ybuf, IQbuf, B + 2*320);                      // 0x1b819, every frame
    fx1c3f4(A, B, 64000);          // combine overlay A into B (other agent)
    memcpy(screen, B, 64000);      // 0x1bbec
    music_service();               // 0x332a4, 0x1259c
    if (kbhit()) quit_demo();      // prints [0x9b844] with "%x"
} while ([0x9b840] + 0x60 > [0x9b844]);

// ---- switch to ALEX (0x31d03): same setup without the overlay init and without recomputing [0x9b840]
remove_tick_callback(0x30990);
[0x580c4] = "alex.lzw"; [0x580c0] = 7; open_video; yiq_palette; 2 new tempallocs; frame = 0;
add_tick_callback(0x30990); want = 1; load_fad("truecol.fad");
bright = 1.0; level = 0x8000; DAC = white; speed = 1000; target = 0x4000;   // second 0.24 s white flash
memset(screen,0,64000); memset(B,0,64000);
do { ...same body... } while ([0x9b840] + 0x90 > [0x9b844]);
remove_tick_callback(0x30990); fx2a34c(0x48);

// ---- tick callback 0x30990 (70 Hz)
if (cnt == 0) { cnt = [0x580c0]; want = 1; } else cnt--;    // cnt=[0x1493b4], never reset between clips
```
**Frame rate:** one decode every `[0x580c0]+1 = 8` ticks = **8.76 fps** (decode only when `want`, no catching
up). HARL uses the data default `[0x580c0]=5` -> 6 ticks = 11.68 fps (note for the Harl owner). Verified on the
capture: picture changes every 3–4 capture frames (0.10–0.13 s) in both clips.

**Frame counts (decompressed size - 1) / 23040:** VS.LZW 147, ALEX.LZW 43, HARL.LZW 200. The counter wraps at 200
(rewind at 0), irrelevant here: VS shows only frames 0..~55, ALEX frames 0..~21.

**Dither/output (0x1b819):** 80x48 chroma blocks -> 4x4 output pixels; each Y sample -> 2x2 pixels.
Output 320x192 written at rows 2..193 of the 320x200 buffer (2 black rows on top, 6 at the bottom).
```c
for (by = 0; by < 48; by++) for (bx = 0; bx < 80; bx++)
  for (r = 0; r < 4; r++) {                       // output row 4*by+r, source Y row 2*by + (r>>1)
    uint32 c = tabIQ[r][I[by][bx] >> 2] >> 2 | tabIQ[r][Q[by][bx] >> 2];  // tables 0x979d0 + 0x100*r
    for (x = 0; x < 4; x++) {                     // source Y col 2*bx + (x>>1)
      v = Y + D[r][x]; Y4 = v > 255 ? 15 : v >> 4;
      out = Y4 + byte x of c;                     // index = Y4 | I2<<4 | Q2<<6
    }
  }
D = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}}   // 4x4 Bayer
```
The 2-bit chroma dither tables at 0x979d0..0x97dcf are built by 0x12ccc (not decoded).
**Palette (0x13244):** `Y=(c&15)*4, I=((c>>4)&3)*16, Q=((c>>6)&3)*16;
R=1.003Y+0.9548I+0.6178Q, G=0.9967Y-0.27I-0.644Q, B=1.0084Y-1.11I+1.6995Q`, truncated, clamped 0..63
(the remake's assets.cpp matrix matches).

**Overlay text in the dance:** 0x2a34c with `[0x1473b8]=1` takes the word from table 0x57f30, chosen when the
current (order,row) equals an entry of the pair tables 0x57e8c (order) / 0x57de8 (row) (orders 21/25/27 are
treated as 17, 22/26/28 as 18). Entries hit in this part (sN = the N-th word hit, from table 0x57f30; the words themselves are not copied into
these notes; order/row, VT of the trigger):
`s0` 31/58 209.55, `s1` 31/62 209.97, `s2` 32/7 210.92, `s3` 32/16 211.86, `s4` 32/24 212.70,
`s5` 32/28 213.12, `s6` 32/38 214.17, `s7` 32/50 215.43, `s8` 32/58 216.27.
Capture confirms s0 (209.7), s1 (210.7), s2 (211.1–211.6), s3 (212.2), s4 (213.0).
(The full table is at 0x57f30. How the word is drawn is 0x2a34c's business.)

**Time base:** frame advance = tick callback (8 ticks); dither/combine = per rendered frame; fades = tick;
part end = position.

**Times:** VS from the end of 0x2e8e4 (~208.9; capture ~209.1) to `[0x9b844] >= 0x1060` = **215.33**;
ALEX to `>= 0x1090` = **217.85** (capture: white flash at 215.4, icons white at 218.0).
`[0x9b840]` must be 0x1000: the dance starts after pos 0xfe0 (208.61), and only 0x1000 gives the observed ends
(0xfc0 would end ALEX at 214.49).

**Differences from the remake (part3.cpp Dance, timeline.cpp):**
1. part3.cpp:488–491 plays both clips at 19.5 fps and all 147 VS frames before switching. Original: 8.76 fps
   (8 ticks/frame), VS frames 0..~55 then ALEX frames 0..~21; switch at 215.33, not 208.4+147/19.5 = 215.94.
2. timeline.cpp:29–30 dance starts 208.4; original ~208.9–209.1 (after facefloor's fade-to-black loop).
3. part3.cpp:492 fades in from black over 0.5 s; original is a 0.24 s white flash (level 0x8000->0x4000 at
   1000/tick) at the VS start **and again at the switch to ALEX** (missing in the remake).
4. part3.cpp:493 white flash at 217.4–217.9 does not exist in the dance; the white is the icons part's
   2.34 s fade from white starting at 217.85.
5. Picture geometry: original 320x192 at y=2 (black rows 0–1 and 194–199); remake stretches to full frame.
   Original has the 4x4 ordered dither and 16 luma / 4x4 chroma levels (README already notes no dithering).
6. Lyrics (timeline.cpp:65–70): missing s0 (209.55) and s1 (209.97); s2 should start 210.92 (not
   210.10); s4 (212.70) and s5 (213.12) are separate words; s6 214.17 (not 213.80);
   s7 215.43; s8 216.27.

---

### 4.2 Icons + MC-303 (main loop 0x320be, routine 0x2a840 "final.raw", callback 0x2a6d8)

**What it draws:** a static 7x5 grid of dim hand-drawn icons that are randomly re-shuffled, over which four
images from final.raw are blurred in and out one after another, centred vertically: "VIC ☼ ACME",
"STATIX ☞ PSYCHIC LINK", "vic@luna.nl ☎ ajbe2@cam.ac.uk", and finally the full-screen MC-303 photo.
Everything goes through a feedback/echo pass with random brightness pumping and random negative flashes.
The remake's separate "outro" is just the fourth image of this routine.

**final.raw** = 320x350, 8-bit grey (no palette; palette is a grey ramp). Layout (see late4/final.png):
rows 1–31 credit line 0, rows 32–63 line 1, rows 70–109 icon strip, rows 124–149 line 2 (e-mails),
rows 150–349 MC-303 photo (the tilt and the dark rounded vignette are in the photo itself).

**Tables (data):**
```
iconX[8] @0x57fd4 = {3, 48, 99, 140, 177, 228, 272, 293}   // source x in rows 70..109
iconW[8] @0x57ff4 = {37, 43, 34, 29, 42, 37, 14, 25}
iconOff[k] = (45 - iconW[k]) / 2 = {4, 1, 5, 8, 1, 4, 15, 10}   // built at init into 0x13efd8
line[4]  @0x58014 = {row0,h} = {1,31}, {32,32}, {124,26}, {150,200}
```
**Init (0x2a840(0x17)):**
```c
for i in 0..255: T1[i] = trunc(i * 0.65);                 // 0x1473bc (shared with 0x2a34c)
img  = tempalloc(0x1b580); tmp = tempalloc(0x1b580);      // [0x13efc8], [0x13efd0]
btab = tempalloc(0x4000);  comp = tempalloc(0xfa00);      // [0x13efd4], [0x13efcc]
for c in 0..255: pal[c] = (c/4, c/4, c/4);                // 0x5faa8, grey ramp
load "final.raw" into img;
idx = -1;                                                 // [0x1475e0]
for i in 0..33: cell[i] = rand() & 7;                     // 0x1475bc; cell[34] keeps its BSS value 0
blurDstStride = 320;                                      // 0x1c9d4 patches 0x1c9c6
e = 0xc0; eTarget = 0;                                    // [0x1475e4], [0x1475e8]
add_tick_callback(0x2a6d8);
```
**Main script around it (0x3203f–0x321cc):**
```c
fx2a840(0x17);
level = 0x8000; speed = 0; bright = 1.0; DAC = white; memset(screen,0,64000);
[0x9b840] = 0; target = 0x4000; speed = 100;              // white -> normal over 164 ticks = 2.34 s
B = 0x6f7a8;                                              // NOT cleared: last dance frame decays in the feedback
do { fx2a840(B); memcpy(screen, B, 64000); music_service(); if (kbhit()) quit_demo(); }
while (level > 0);
fx2a840(0x48);                                            // removes callback 0x2a6d8
```
**Render (0x2a840(dst)):**
```c
memset(comp, 0, 64000);
for (r = 0; r < 5; r++) for (c = 0; c < 7; c++) {         // cells 45x40, x = 45c, y = 40r (5 px right margin)
    k = cell[r*7 + c];
    // 0x1c01e: 40 rows, width rounded up to 4, each byte p>>3 (0..31)
    copy_dim(img + 70*320 + iconX[k], comp + r*40*320 + 45*c + iconOff[k], iconW[k]);
}
if (idx >= 0) {
    s = e / 3;                                            // C division
    if (s < 10) s += 3 * (int8)J[0];                      // J = 0x9c630.., random, see callback
    if (s < 64) {
        n = abs(s) + 1;
        step = (0x10000 - 1000*n) / n;
        for (i = 0; i < 0x4000; i++) btab[i] = min(i*step, 0xffffff) >> 16;   // = min(255, i*step>>16)
        // 0x1c91d: horizontal box blur, n taps, window starts at x - n/2 (reads across row ends)
        for (y = 0; y < h[idx]; y++) { sum = Σ src[x-n/2 .. x-n/2+n-1]; tmp[y][x] = btab[sum]; }
        y0 = 100 - h[idx]/2;                              // 85, 84, 87, 0
        for (i = 0; i < h[idx]*320; i++) comp[y0*320+i] = max(comp[y0*320+i], tmp[i]);   // lighten
    }
}
// brightness/negative map, rebuilt every frame
g = 1 + 0.2 * abs((int8)J[7]);                            // byte 0x9c637
for (i = 0; i < 256; i++) {
    v = min(255, trunc(i * g));
    w = trunc(v*(1-f) + (255-v)*f);                       // f = [0x1473b0]
    map2[i] = clamp(w, 0, 255);                           // 0x1474bc
}
if (idx > 2) { speed = 10; target = 0; }                  // MC-303 shown: start the long fade to black
// 0x1c058: feedback into the persistent frame buffer
for (i = 0; i < 64000; i++) dst[i] = map2[min(255, T1[dst[i]] + (comp[i] >> 1))];
```
Effect of the blur table: output = floor(sum*step/65536) ~ average * (1 - 0.01526*n), so a wide blur is also
dark (n=64 -> 2%, n=10 -> 85%, n=1 -> 98.5%). Feedback steady state = comp/(2*0.35) = 1.43*comp
(saturates for comp >= 178); icons end up at ~18% of their source grey. With gain g=1.4 the steady state is
7.8*comp, with g=1.8 the loop is unstable and the screen saturates to white within a few frames.

**Callback 0x2a6d8 (every tick):**
```c
if ([0x9b844] != last) {                                  // once per row
    last = [0x9b844];
    reroll_jitter();                                      // 0x15494: J[0..31] (0x9c630..) = trunc(0.7*tab[rand()&15])
                                                          // tab@0x52f10 = {0,0,1,1,-1,-1,2,-2,6,-4,2,1,4,-1,-6,-2}
                                                          // -> each J: 0 (p 8/16), +1 (2/16), -1 (2/16),
                                                          //    +4, -2, +2, -4 (1/16 each)
    if ((last & 3) == 0) {                                // every 2 rows
        if ((rand() & 0x1f) == 0) f = (f > 0.5) ? 0 : 1;  // random negative flash, p = 1/32
        for (i = 0; i < 16; i++) { v = rand() & 7; cell[(rand()%5)*7 + rand()%7] = v; }
    }
    if ((last & 0x3f) == 0 && idx < 3) { idx++; eTarget = 0; e = 0xc0; }   // next image, starts at s=63
    if ((last & 0x3f) > 0x32 && idx < 3) eTarget = 0x400;                    // blur out
}
if (e < eTarget) e += 3;  if (e > eTarget) e -= 2;
if (f > 0) f -= 0.021;  if (f < 0) f = 0;                 // flash decays over 48 ticks (0.68 s)
```
rand() is Watcom's LCG (`x = x*0x41c64e6d + 0x3039; (x>>16)&0x7fff`, 0x34974), shared with everything else.

**Time base:** e/eTarget, f decay, palette fade = per tick; idx, blur-out trigger, shuffle, flash trigger,
jitter J = per position change (row); icon copy, blur, feedback 0.65/half-add, map2 = per rendered frame
(no vsync wait in this loop, so the trail length depends on render speed).

**Times (VT):** part start 217.85 (white, fades to normal by 220.19; capture 218.0 -> 220.2).
Blur-in = 95 ticks (1.36 s) from n=64 to sharp, jitter active once s<10 (after 81 ticks);
blur-out starts at pos&0x3f = 0x34 and is invisible after 64 ticks (0.91 s).
| image | rows | y0 | appears (pos) | sharp | blur-out | gone/replaced |
|---|---|---|---|---|---|---|
| VIC ☼ ACME | 1–31 | 85 | 220.37 (0x10c0) | 221.72 | 223.10 (0x10f4) | 224.01 (line 1 takes over at 223.73) |
| STATIX ☞ PSYCHIC LINK | 32–63 | 84 | 223.73 (0x1100) | 225.08 | 226.46 (0x1134) | 227.37 (replaced 227.09) |
| e-mail line | 124–149 | 87 | 227.09 (0x1140) | 228.44 | 229.82 (0x1174) | replaced 230.45 |
| MC-303 photo | 150–349 | 0 | 230.45 (0x1180) | 231.80 | never | fades with the palette |

From 230.45 the palette fades linearly from L=64 to 0 at 10/tick (1639 ticks, one 1/64 step every 25.6 ticks =
0.365 s): screen black (L=0) at **253.46**, loop exits at **253.83**. The song ends at music 244.87 = VT 243.78;
after that `[0x9b844]` stops changing, so shuffle/jitter/flash triggers stop and the image freezes while it fades.
Capture check: frozen image from 243.9, brightness steps every 0.33–0.40 s, black from ~252.4 (the last ~4
levels are below the capture's black level).

**Differences from the remake (part3.cpp Icons/Outro, timeline.cpp):**
1. part3.cpp:508–518: scrolling, tiled rows of the whole icon strip (rows 71–107). Original: static 7x5 grid,
   one icon per 45x40 cell (table above, source rows 70–109), 16 random cells re-assigned every 2 rows; no
   scrolling; icons at p/8 then ~1.43x feedback (~18% grey), no tint (part3.cpp:517 tints 0.75/0.75/0.8).
2. part3.cpp:510 brightness ramp 224–226 does not exist; brightness instead pumps per row by gain
   1+0.2|J[7]| (1, 1.2, 1.4, 1.8) through the feedback, which saturates the whole screen on 1.8 rows.
3. part3.cpp:521 source rows wrong: {1,26},{35,61},{125,148} -> {1..31},{32..63},{124..149}; images are drawn
   1:1 at full width (320 px), centred at y = 100 - h/2.
4. part3.cpp:524–530: slide-in from the right with 7 smeared copies and a black band (part3.cpp:527). Original:
   no motion, no band; a horizontal box blur shrinking from 64 px to 1 px over 1.36 s while brightening
   (factor 1-0.0153n), jitter of 3*J[0] px when sharp, and blur-out 0.91 s before the next image.
5. part3.cpp:521 times {221.0–224.3},{224.4–227.6},{227.6–229.6} -> table above (220.37 / 223.73 / 227.09 starts).
6. part3.cpp:505–506: one fixed negative flash at 229.6–230.4. Original: random negative flashes (p=1/32 every
   2 rows, f=1 decaying over 0.68 s, v' = v(1-f)+(255-v)f) during the whole part until the song ends.
7. No feedback in the remake: original `dst = map2[min(255, trunc(0.65*dst) + comp/2)]` every frame, starting
   from the last dance frame.
8. part3.cpp:532 0.7 s white flash; original 2.34 s linear (DAC) fade from white, 217.85–220.19.
9. timeline.cpp:31–32: icons/outro split at 230.6 -> 230.45; whole part ends 253.46 (black) / 253.83 (loop end).
10. Outro (part3.cpp:543–546): zoom 1.12+0.012*local and source from row 155 -> original rows 150–349 at 1:1,
    no zoom, no rotation, no tint (part3.cpp:546 tints 0.82/0.86/0.95); the dim icon grid stays visible
    through dark parts of the photo (lighten), and the photo is blurred in like the text lines.
11. part3.cpp:547–561 vignette shader: not in the code; the dark rounded corners are part of final.raw.
12. part3.cpp:562–564 flashes at 230.6–231.6 (white), 235.6–236.2, 237.6–238.4: original has the blur-in at
    230.45–231.80 and random negative flashes / gain pumping instead of fixed ones.
13. part3.cpp:565 smoothstep fade 242.0–252.5 -> linear palette fade 230.45–253.46 in 64 steps; also freeze
    (no jitter/flicker) from 243.78.

---

### 4.3 "Outro" 0x10dc4 and the end of the main script (0x32215–0x3232f)

0x10dc4 is **not** a visual: it closes the data library (`if ([0x8ec50]) { [0x8ec50]=0; fclose([0x8ec48]);
free([0x8ec4c]); [0x8ec4c]=0; }`, same code as the start of 0x10e00 "open mustard.pak").
```c
// after the icons loop
part_cleanup();  part_cleanup();   // 0x332a4, 0x133b0, [0x9b840]=round, tempalloc reset 0x18898,
                                   // 0x10bac(0), clear all tick callbacks 0x10c0c(0), [0x5d698]=0, flush keys
close_library();                   // 0x10dc4
while (kbhit()) getch();
while (!kbhit()) { music_service(); }      // 0x332a4 + 0x1259c: black screen (palette level 0), music
                                           // (already finished at 243.78) until a key is pressed
while (kbhit()) getch();
stop module (0x34874), timer sync off (0x374d0(rate,0,0,0)), text mode (0x10c34(3)),
printf "midas free module...\n", free module (0x37b00), "midas close...\n", MIDAS close (0x344dc),
"midas done\n", "stop...\n", "nothing...\n"; return.
```
So after VT 253.83 the original shows a black screen until a key is pressed (the capture is cut at 253.33).
Remake difference: timeline.cpp:32 outro scene 230.6–253.4 should be part of the icons routine (see 4.2);
no separate visual outro exists.

### 4.4 Not determined
- The exact dance start (depends on 0x2e8e4's final fade level and load time; ~208.9 computed, ~209.1 on capture).
- The 2-bit chroma dither tables 0x979d0.. (built in 0x12ccc) and the 4x4 DCT details (0x1b648) — the remake's
  float decoder already covers the codec.
- What 0x2a34c draws and how 0x1c3f4 combines it (other agents); only the lyric selection is listed above.
- Render frame rate of the original machine, which sets the per-frame feedback trail length in the icons part.
