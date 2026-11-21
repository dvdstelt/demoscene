# Scope "twirl": a picture wrapped around the screen centre and twisted (29:63..31:63)

Routines: init 0x409ea0 (with 0x40a29c pre-table, 0x40a408 conversion), start 0x40a678, event 0x40a098, tick
0x40a20c, frame 0x40a264 (0x40a5a4 ring table, 0x40a54c pixels), stop 0x40a738; 0x409e90 (see section 8).
Addresses are linear. "Refresh" = 70.086 Hz screen refresh, "record k" = record k of `full.frames`. Screen
coordinates are original 320 x 200 pixels (shown at 4:3); pixel n covers [n, n+1).

Verified against the capture (scripts in `models/twirl/`, see its README):
- the twirl table built by a Python model of 0x40a29c + 0x40a408 equals the table in `snaps/f10574.snap` (all
  64,000 entries);
- a model of the tick and frame routines with the event schedule of section 5 renders **every record 10575..11275
  exactly** (701 of 701 records, all 64,000 pixel indices);
- the model's float phases after 701 refreshes equal the values in `snaps/f11276.snap` bit for bit.

---------------------------------------------------------------------------------------------------------------

## 1. What it draws, and when

A 256 x 256 picture mapped in polar coordinates around the screen centre: the picture's columns go around the
centre (256 columns = one turn), its rows go outwards (one row = 1.72 pixels of radius), so the picture becomes a
tunnel or whirl. Per ring (radius), the picture is shifted radially by a sine wave of the ring index and angularly
by another sine wave of the ring index (the twist), plus a global rotation and a global radial scroll. The picture,
the waves and the speeds change at four script events, each with a flash of the palette (section 6).

| records | music | picture (palette) | parameter set | what it looks like |
|---|---|---|---|---|
| 10574 | 29:63 | – | – | 0x40a678 runs after this refresh's tick; the sprites' last frame is still shown |
| 10575..10711 | 29:63..30:24 | `flower/jeesus1.raw` (`flower/jeesus.pal`) | 1 | a red and white swirl texture twisted into spiral arms that sway, pumping in and out radially; black where the picture's black background comes round |
| 10712..10843 | 30:24..30:48 | `nigga/nigga2.raw` (`nigga/nigga2.pal`) | 2 | magenta / red liquid texture flowing outwards, the whole picture swinging round by up to 266 degrees; starts in inverse colours (the script's inverse flash) |
| (never) | 30:48 | `nigga/nigga4.raw` (`nigga/nigga4.pal`) | 3 | loaded and replaced at once by the next event (section 5) |
| 10844..11128 | 30:48..31:36 | `flower/jeesus2.raw` (`flower/jeesus.pal`) | 4 | green / yellow / blue liquid texture streaming into the centre with concentric ripples |
| 11129..11275 | 31:36..31:63 | `flight/river.raw` (`flight/map.pal`) | 0 | the dark river photo turning slowly (counter-clockwise) with a long radial wave |
| 11276 | 31:63 | – | – | temple |

The frame routine writes **every pixel of the frame** (all 64,000 bytes, every pixel has a table entry). Nothing
else is drawn (no letterbox; the debug overlay 0x400bec is off: [0x42277c] = 0).

## 2. Static data, buffers, variables

| what | where | format / content |
|---|---|---|
| picture 0 | `flower/jeesus1.raw`, INSIDE.DAT block offset 0x276149, 65,536 bytes | 256 x 256, 8-bit indices into `flower/jeesus.pal` (0x275e49, 768 bytes, 6-bit) |
| picture 1 | `nigga/nigga2.raw`, 0x1b9357, 65,536 | 256 x 256, `nigga/nigga2.pal` (0x1b9057) |
| picture 2 | `nigga/nigga4.raw`, 0x204d0d, 65,536 | 256 x 256, `nigga/nigga4.pal` (0x204a0d) |
| picture 3 | `flower/jeesus2.raw`, 0x286149, 65,536 | 256 x 256, `flower/jeesus.pal` |
| picture 4 | `flight/river.raw`, 0x14ae6f, 65,536 | 256 x 256, `flight/map.pal` (0x15c467) |
| file names | 0x421704, 0x421718, 0x421748, 0x42175c, 0x421770, 0x421784, 0x421798, 0x4217ac, 0x4217c0 | strings; the pictures are used in place in the INSIDE.DAT block (pointer from 0x400614) |
| double constants | 0x4217cf = 0.006135923 (angle step, about 2π/1024), 0x4217d7 = 0.43 (radius step) | used by 0x40a29c |
| twirl table | [0x4b6d38] = allocate(0x3e800, "twirl table"), zeroed | 64,000 x {word k, word offset} (section 3); freed by 0x40a738 |
| pre-table | [0x4b6d28] = allocate(0x3e800, "twirl pre-table"), zeroed | 64,000 x {word u, word v}; freed at the end of init (0x40b690) |
| ring table | 0x4b6868, 256 words | rebuilt every frame (section 4) |
| current picture | [0x4b6d3c] (pointer), [0x4b6d40] = [0x4b6d3c] + 0x8000 | |
| current palette | [0x4b6d54] (pointer into the INSIDE.DAT block) | |
| memory tag | [0x4b6d30] | 0x40b3c0 new tag, 0x40b4d0 makes it current (init and 0x40a678), 0x40b6fc frees by tag (0x40a738) |
| picture index | [0x42296c] (data, initial 0; set to 0 by 0x40a678) | 0..4, +1 mod 5 per event |
| set index | [0x422970] (data; set to 1 by 0x40a678) | 0..4, +1 mod 5 per event |
| parameter sets | 0x4b6c68 + 0x20 s, 8 floats, s = 0..4 (BSS, written by 0x409ea0) | table below |
| current parameters | 0x4b6d08..0x4b6d24 (floats) | copied from a set by 0x40a678 / 0x40a098 |
| phases | [0x4b6d58] s58, [0x4b6d2c] s2c, [0x4b6d50] p1, [0x4b6d34] p2 (floats, BSS: 0 at program start, never reset) | stepped by 0x40a20c |

The parameter sets (0x409ea0 writes them as immediate float bit patterns: 0x3ca3d70a = 0.02, 0x3df5c28f = 0.12,
0x42340000 = 45, 0x3f800000 = 1, 0xbd23d70a = -0.04, 0x42c60000 = 99, 0x3d23d70a = 0.04, 0x42c40000 = 98,
0x3cf5c28f = 0.03, 0xc0000000 = -2, 0x433d0000 = 189, 0x3ed70a3d = 0.42, 0xbdcccccd = -0.1, 0x41500000 = 13,
0x3eae147b = 0.34, 0x3da3d70a = 0.08, 0x41400000 = 12, 0x40400000 = 3). Each set field is copied to one current
variable:

| set field | → variable | meaning | set 0 | set 1 | set 2 | set 3 | set 4 |
|---|---|---|---|---|---|---|---|
| +0x00 | [0x4b6d18] d18 | radial wave: phase step per ring (rad) | 0.02 | 0 | 0 | 0.42 | 0.34 |
| +0x04 | [0x4b6d20] | radial wave: p1 += per refresh (rad) | 0.12 | -0.04 | 0 | -0.1 | 0.08 |
| +0x08 | [0x4b6d10] A | radial wave amplitude (texel rows) | 45 | 99 | 0 | 13 | 12 |
| +0x0c | [0x4b6d08] | rotation: s58 += per refresh (columns) | 1 | 0 | 0 | 0 | 0 |
| +0x10 | [0x4b6d0c] | radial scroll: s2c += per refresh (rows) | 0 | 0 | -2 | -2 | 3 |
| +0x14 | [0x4b6d1c] | twist: p2 += per refresh (rad) | 0 | 0.04 | 0.02 | 0 | 0 |
| +0x18 | [0x4b6d24] B | twist amplitude (texel columns) | 0 | 98 | 189 | 0 | 0 |
| +0x1c | [0x4b6d14] d14 | twist: phase step per ring (rad) | 0 | 0.03 | 0 | 0 | 0 |

(The copy code loads the eight fields in the order +0, +8, +4, +0xc, +0x10, +0x14, +0x18, +0x1c and stores them
through FPU register exchanges; the table is the result, checked in the snapshot.) Set s is used with picture
s − 1 (mod 5): set 1 with picture 0, set 2 with picture 1, set 3 with picture 2, set 4 with picture 3, set 0 with
picture 4.

## 3. Init: the twirl table (0x409ea0, 0x40a29c, 0x40a408)

```c
void twirl_init(void)                                  // 0x409ea0, called from init (0x4009d3), last of all parts
{
    tag = new_tag(); set_tag(tag);                     // [0x4b6d30]
    pic = datafile("flower/jeesus1.raw"); pic8000 = pic + 0x8000;
    pal = datafile("flower/jeesus.pal");
    table = allocate(0x3e800, "twirl table");          // zero-filled (allocator flag [0x4b6ed8] = 1)
    pre   = allocate(0x3e800, "twirl pre-table");      // zero-filled
    write the five parameter sets (section 2);
    build_pre(160, 100);                               // 0x40a29c
    convert();                                         // 0x40a408
    free(pre);                                         // 0x40b690
}

void build_pre(int cx, int cy)                         // 0x40a29c(EAX = 160, EDX = 100)
{
    double r = 0.0;                                    // qword on the stack
    for (int j = 0; j < 512; j++) {                    // [0x4b6d44]
        double a = 0.0;
        for (int i = 0; i < 1024; i++) {               // [0x4b6d48]
            int x = trunc(cos(a) * r + cx);            // x87, truncation toward zero
            if (x < 320 && x > -1) {
                int y = trunc(sin(a) * r + cy);
                if (y < 200 && y > -1) {
                    pre[y][x].u = (i >> 2) - 128;      // word at pre + 4 (320 y + x)
                    pre[y][x].v = (j >> 2) - 128;      // word at pre + 4 (320 y + x) + 2
                }
            }
            a += 0.006135923;                          // double; 1024 steps = 2π - 1.6e-7
        }
        r += 0.43;                                     // double; j = 511: r = 219.7
    }
    for (int n = 0; n < 128000; n++) table_words[n] += pre_words[n];   // table was zero: table = pre
}

void convert(void)                                     // 0x40a408, in place, 64,000 entries
{
    for (int p = 0; p < 64000; p++) {
        int16 u = table[p].u, v = table[p].v;
        table[p].w0 = v & 0xff;                        // ring index k = 128 + (j >> 2)       (128..255)
        table[p].w1 = ((v << 8) + (u & 0xff)) & 0xffff;// 256 (row - 128) + ((col + 128) & 255), row = j >> 2, col = i >> 2
    }
}
```

So every screen pixel holds the polar sample that was written to it last: j outer, i inner, so the largest radius
that lands in the pixel and, on that circle, the largest angle. All 64,000 pixels are written (checked). Per pixel:
- **ring / row** `row = j >> 2` (0..127): radius 0.43 j, so one row = 4 × 0.43 = 1.72 pixels of radius;
- **column** `col = i >> 2` (0..255): angle a = i × 0.006135923, so 256 columns per turn; a = 0 points right
  (+x) and a grows towards +y (down), i.e. **clockwise on screen**. The table stores (col + 128) & 255.

The centre is (160, 100) in the coordinates where pixel n covers [n, n+1): the corner between pixels 159/160 and
99/100, the exact centre of the screen. The screen corners are at radius 188.7 (row 110).

Continuous form (`fit.py`): with (dx, dy) = pixel centre − (160, 100), ρ = |(dx, dy)|, θ = atan2(dy, dx) in [0, 2π):
the stored row is floor(ρ / 1.72 + 0.315) and the column floor(256 θ / 2π + 0.2) to within one texel (standard
deviation 0.30 and 0.37 texel; the bias comes from the last write coming from the pixel's far side). Near the
centre many columns fall into one pixel and the table holds the last one.

## 4. Frame: 0x40a264 (once per drawn frame)

```c
void twirl_frame(void)                                 // 0x40a264
{
    dest = back buffer [0x4248d0];                     // = the draw target [0x4b6ec0], 320 x 200
    ring_table(s58, s2c, p1, p2);                      // 0x40a5a4 (the four phases, as floats, on the stack)
    draw();                                            // 0x40a54c
    debug_text();                                      // 0x400bec: prints "%02d:%02d" music position and "%05d kB" when [0x42277c] != 0 (it is 0)
    flip();                                            // 0x400b14
}

void ring_table(float s58, float s2c, float p1, float p2)   // 0x40a5a4, ret 0x10
{
    int base = 256 * trunc(s2c) + trunc(s58);
    long double a = p1, b = p2;                        // x87 registers
    for (int k = 0; k < 256; k++) {                    // words at 0x4b6868 + 2k
        T[k] = (int16)(base + 256 * trunc(A * sin(a)) + trunc(B * sin(b)));   // A = [0x4b6d10], B = [0x4b6d24]
        a += d18;                                      // [0x4b6d18]
        b += d14;                                      // [0x4b6d14]
    }
}

void draw(void)                                        // 0x40a54c
{
    for (int p = 0; p < 64000; p++)
        dest[p] = pic8000[(int16)(T[table[p].w0] + table[p].w1)];   // = pic[(T + w1 + 0x8000) & 0xffff]
}
```

Written out, pixel p with table sample (i, j) gets the texel at linear address
```
addr = ( 256 * ( (j>>2) + trunc(s2c) + trunc(A sin(p1 + k d18)) )
       +       ( ((i>>2) + 128) & 255 ) + trunc(s58) + trunc(B sin(p2 + k d14)) )  & 0xffff,     k = 128 + (j>>2)
index = pic[addr]
```
The picture is a 64 KB torus addressed linearly: rows wrap mod 256, and a column that runs past 255 (or below 0)
carries into the next (previous) row. With the shifts at 0, the column wrap is the ray pointing left from the
centre (a = π); it moves with s58 and with the twist. The picture's left and right edges do not match, so a seam
runs along that ray (bent by the twist), with the radius stepping by one row across it.

**For the remake (per pixel, any resolution)**: for an output point (x, y) in original pixel units
(x = 320 X / W, y = 200 Y / H; the frame is shown at 4:3, so the circles are ellipses 1.2 times taller than wide,
as in the original):
```
dx = x - 160;  dy = y - 100;
rho   = sqrt(dx*dx + dy*dy);
theta = atan2(dy, dx);                                 // clockwise on screen, 0 = right; any 2π range
q = rho / 1.72 + 0.315;                                // continuous ring; texel row before the shifts
k = 128 + q;                                           // the original uses the integer ring 128 + floor(q)
R = q + s2c + A * sin(p1 + k * d18);                   // texel row    (texel n covers [n, n+1))
C = 128 + theta * 256 / (2π) + 0.2 + s58 + B * sin(p2 + k * d14);   // texel column
texel(c, r) = pic[(256 * r + c) & 0xffff]              // integer taps; c = 256 carries into row r + 1
colour = palette_in_force[ filtered texel at (C, R) ]  // see section 6
```
The +0.315 and +0.2 put the continuous coordinates where the original's table samples are on average; they are
below half a texel. `continuous.py` renders this next to the capture (`cont_*.png`).

## 5. Start, events, tick, stop

```c
void twirl_start(void)                                 // 0x40a678, script 29:63 (entry 84, record 10574)
{
    set_tag(tag);
    picture_index = 0;  set_index = 1;                 // [0x42296c], [0x422970]
    current parameters = set 1;
    white_flash();                                     // 0x400498
    set_palette(pal);                                  // 0x400364(flower/jeesus.pal), entry 0 not touched (it is black)
    set_tick(0x40a20c);  set_frame(0x40a264);          // both reset the part tick counter (not used by the twirl)
}

void twirl_event(void)                                 // 0x40a098, script 30:24, 30:48, 30:12, 31:36
{
    picture_index = (picture_index + 1) % 5;
    set_index     = (set_index + 1) % 5;
    current parameters = set[set_index];               // the phases s58, s2c, p1, p2 are NOT reset
    switch (picture_index) {                           // jump table 0x40a084
    case 0: pic = "flower/jeesus1.raw"; pal = "flower/jeesus.pal"; break;
    case 1: pic = "nigga/nigga2.raw";   pal = "nigga/nigga2.pal";  break;
    case 2: pic = "nigga/nigga4.raw";   pal = "nigga/nigga4.pal";  break;
    case 3: pic = "flower/jeesus2.raw"; pal = "flower/jeesus.pal"; break;
    case 4: pic = "flight/river.raw";   pal = "flight/map.pal";    break;
    }
    pal[0] = pal[1] = pal[2] = 0;                      // palette entry 0 black (writes into the INSIDE.DAT block)
    white_flash();                                     // 0x400498 (on the old base palette)
    pic8000 = pic + 0x8000;
    set_palette(pal);                                  // 0x400364
}

void twirl_tick(void)                                  // 0x40a20c, every refresh (timer callback, after the frame clock)
{
    s58 += [0x4b6d08];  s2c += [0x4b6d0c];  p1 += [0x4b6d20];  p2 += [0x4b6d1c];   // float32 stores
}

void twirl_stop(void)                                  // 0x40a738, script 31:63 (entry 92)
{
    set_tick(0); set_frame(0); free_tag(tag);          // frees the twirl table
}
```

Events in the capture (n = tick steps since the part start = refresh − 10574; the state after n steps is the one
shown at record 10575 + n):

| script | event | ran | first tick step with the new speeds | first state drawn with the new picture |
|---|---|---|---|---|
| 29:63 (entry 84) | start: picture 0, set 1 | refresh 10574, after its tick | 1 | 0 (record 10575) |
| 30:24 (entry 85) | picture 1, set 2 | refresh 10711, after its tick | E1 = 138 | 137 (record 10712) |
| 30:48 (entry 87) | picture 2, set 3 | refresh 10843, before its tick | – | – (replaced in the same refresh) |
| 30:12 (entry 88, follows 30:48 in the table) | picture 3, set 4 | refresh 10843, before its tick | E2 = 269 | 269 (record 10844) |
| 31:36 (entry 90) | picture 4, set 0 | refresh 11128, before its tick | E3 = 554 | 554 (record 11129) |

Entries 87 and 88 run in consecutive calls of 0x400b3c in the same main-loop pass (0x400cb4 calls it until it
returns 0), so nigga4 and set 3 are never
drawn or stepped; the visible result of the pair is one white flash to `flower/jeesus.pal`. A fifth event would
return to picture 0 / set 1; there is none.

## 6. Colours

The pixel's index is the picture's texel, nothing else: **no shade table, no blend, no index arithmetic**. The colour
is the DAC entry of that index. The palette in force:
- 29:63: script entry 83 (0x400498) and 0x40a678 (0x400498, then 0x400364(jeesus.pal)): the DAC goes from white to
  `flower/jeesus.pal` in 15 refreshes (records 10575..10589).
- 30:24: the event sets `nigga/nigga2.pal` (entry 0 made black; the sprites part had already done so) with a white
  flash, but the script's next entry 86 (0x400454, inverse flash) runs before the next refresh, so the white is
  never shown: the DAC starts at (63 − c) for each 6-bit value c of nigga2.pal and fades to it in 20 refreshes
  (records 10712..10731).
- 30:48 + 30:12: white → `flower/jeesus.pal` (entry 0 made black; it is black in the file) in 15 refreshes
  (records 10843..10857).
- 30:56 (script entry 89, 0x400498): white → jeesus.pal again in 15 refreshes (records 10887..10901).
- 31:36: white → `flight/map.pal` with entry 0 made black (it is (0, 1, 1) in the file; river.raw uses index 0)
  in 15 refreshes (records 11128..11142).
- 31:63: script entry 91 (0x400498) flash, then the temple sets its palette.

The fades are the core palette system's (0x4004ac / 0x400540: linear per 6-bit value, truncated; in the capture
about a third of the values end one step below the palette, e.g. 25 instead of 26, and stay there). The twirl
itself never writes the DAC or cycles colours; its only palette write is entry 0 of the new palette (into the
INSIDE.DAT block: `nigga2.pal`, `nigga4.pal`, `jeesus.pal`, `map.pal` stay changed, but no later part uses them).

## 7. Time bases

| variable | time base | closed form (n = refreshes since the part start; E1 = 138, E2 = 269, E3 = 554 in the capture) |
|---|---|---|
| s58 (rotation, columns) | per refresh, set 0 only | `max(0, n − E3 + 1)` (148 at the end) |
| s2c (radial scroll, rows) | per refresh, sets 2 / 4 | `−2·clamp(n − E1 + 1, 0, E2 − E1) + 3·clamp(n − E2 + 1, 0, E3 − E2)` (−262 at E2 − 1, 593 from E3 − 1 on) |
| p1 (radial-wave phase) | per refresh | `−0.04·min(n, E1 − 1) + 0.08·clamp(n − E2 + 1, 0, E3 − E2) + 0.12·max(0, n − E3 + 1)` |
| p2 (twist phase) | per refresh | `0.04·min(n, E1 − 1) + 0.02·clamp(n − E1 + 1, 0, E2 − E1)` (8.10 from E2 − 1 on) |
| A, B, d18, d14, the speeds, picture, palette | script events (30:24, 30:48 + 30:12, 31:36) | section 5 |

All four phases start at 0 (BSS; the part runs once) and are continuous across the events; only their speeds
change. They are float32 sums (`endstate.py`: the closed form differs by at most 1e-5 rad). The state is linear
between refreshes, so a remake can evaluate it at fractional n for smooth motion. Nothing depends on the number of
frames drawn (the frame routine keeps no state: in the capture 15 records show the previous frame once more, when
a frame took longer than a refresh) and nothing reads the part tick counter or the frame clock. No random numbers.

In words: set 1 (jeesus1): radial shift 99 sin(−0.04 n) rows, the same for every ring (the picture pumps in and
out, period 157 refreshes), twist 98 sin(0.04 n + 0.03 k) columns (up to ±138°, 0.03 rad of phase per ring).
Set 2 (nigga2): no radial wave, the whole picture turned by 189 sin(p2) columns (±266°, p2 from 5.48 to 8.10),
content moving outwards at 2 rows (3.44 px) per refresh. Set 4 (jeesus2): radial ripples 12 sin(p1 + 0.34 k) rows
(wavelength 18.5 rings = 32 px), content moving inwards at 3 rows (5.16 px) per refresh. Set 0 (river): radial
wave 45 sin(p1 + 0.02 k) rows (one wave spans 2.56 rad over the 128 rings), rotation 1 column (1.41°) per refresh,
counter-clockwise on screen.

## 8. 0x409e90 and unused code

0x409e90 sets the tick and frame routines to 0 (0x400af4(0), 0x400b04(0)). It is the flower part's stop routine
(script 19:63, entry 67, through the table at 0x422610); the twirl does not use it.

0x40a460..0x40a54b is not called from anywhere (no call, no pointer): it would write a 256 x 256 spiral unwrap of
the picture (angle += 2π/256 and radius += 0.005039052 per pixel, centre (128, 128)) into the back buffer (65,536
bytes into a 64,000-byte buffer) and copy it over the picture (0x40b784, 0x10000 bytes). A leftover; ignore it.

## 9. Pictures

- `flower/jeesus1.raw`: an abstract red, orange and white swirl texture inside a disc on black. **Drawn**
  (painted), not a photo, no people. In this part it is read **straight through the palette** (texel index →
  palette entry), so a true-colour redraw can replace it here, as long as it keeps the 256 x 256 layout exactly
  (the twirl maps columns to angle and rows to radius, and the 64 KB wrap with the column carry) and the palette
  fades are applied to its colours (each is a per-channel linear fade from white or from 63 − c to the colour c,
  which can be applied to true colour). Note: the flower part uses the same picture through `flower/jeesus.shd`
  (flower scope).
- `flower/jeesus2.raw`, `nigga/nigga2.raw`, `nigga/nigga4.raw` (never shown here): abstract liquid swirl textures in
  posterised colour bands, drawn; here read straight through their palettes. The sprites part uses nigga2 / nigga4
  through `.shd` / `.qnt` tables.
- `flight/river.raw`: dark greens and browns of foliage and rocks around water; it looks like a digitised photo (no
  people). Here read straight through `flight/map.pal` with entry 0 black. The flight scope describes its own use.

## 10. Low-resolution shortcuts to evaluate exactly

| shortcut | what it approximates | remake |
|---|---|---|
| pre-table: one polar sample per screen pixel, the last one written, 256 angles × 128 rings | ρ / 1.72 and 256 θ / 2π at the pixel | atan2 / hypot per output pixel (section 4) |
| ring table: one sine per ring (1.72 px), the ring index an integer | the waves as functions of the radius | continuous q in the sines (removes the staircase rings at large twist) |
| trunc of each shift (s2c, s58, A sin, B sin) separately, toward zero | real-valued shifts | a float sum (the rotation and scroll speeds are whole texels per refresh anyway) |
| nearest texel of a 256 × 256 picture | the picture | filtered read with the address rule `pic[(256 r + c) & 0xffff]` per tap; towards the centre all 256 columns meet, so supersample or pick a coarser level there |
| 6-bit palette, truncated fade steps | the palette colours and linear fades | float colours v / 63 and continuous fades (core) |

Keep: the centre, direction and scale of the mapping, the per-ring waves, the column carry / seam, the event timing,
the inverse-colour start of the nigga2 section.

## 11. Not determined

- The palette fade arithmetic (the values that end one step low) is the core scope's; the numbers above are read
  from the capture.
- Whether event 30:24 runs before or after a refresh's tick depends on where the frame ends (one refresh either way:
  E1 = 138 here, 137 or 138 for the remake); the same holds for the other events.
- Whether `jeesus1.raw` / `jeesus2.raw` / the nigga textures were painted by hand or made with a paint program's
  smear tools cannot be told from the pixels; they are not generated by the code. That `river.raw` is a photo is a
  judgement from its look.
