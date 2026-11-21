# Scope "flower": the green tunnel and the red flowers (14:00..19:63)

Routines: init 0x409730 with the LightWave scene reader 0x408030 (0x408480, 0x408588, 0x408d70, 0x408dfc,
0x408f9c, 0x409050, the spline 0x4090cc), start 0x4098c4 (with 0x408090), tick 0x40940c, frame 0x4095f8 (with the
camera / object placement 0x4080d8), events 0x409358 (show a flower) and 0x4093f0 (hide it), stop 0x409e90; unused
here: 0x409164, 0x409258, 0x409214 (scaled objects). Addresses are linear. "Refresh" = 70.086 Hz screen refresh,
"record k" = record k of `full.frames`, "tick" = one call of the part's tick routine (once per refresh). Screen
coordinates are original 320 x 200 pixels (shown at 4:3). Calls into SurRender are described by what goes in and out
(the engine scope has the internals: notes-engine.md).

Verified against the emulator (scripts in `models/flower/`, see its README):
- the reader's per-frame key table (a Python model of 0x408030 on `flower/tunnel1.lws`) equals the table in
  `snaps/f4963.snap` at 0x426618: both motions, 2 x 269 records of 9 floats; positions bit for bit, 14 of the 2,421
  angle floats differ by 1 ulp (x87 extended vs double rounding);
- the camera model (section 7) gives the emulator's camera position exactly and its matrix to 5e-7 in **all 1,443
  drawn frames** of the part (a resumed run from `snaps/f4963.snap` with dump hooks, `run_flower.py`, which draws
  the same 1,443 frames as the capture);
- the tick recurrence (LightWave frame, face window counter, texture scroll) equals the emulator in all 2,100 ticks,
  including the wrap at tick 1,734;
- the tunnel's per-vertex light levels (formula with rand(), section 4) equal the snapshot for all 732 vertices;
- the morph model equals the flower's vertex positions and normals in all 1,443 frames; the flower's rotation
  matrix equals Rx(angle) Ry(spin) to 2e-9; its camera-space vertices equal R p + (0, 0, 1500) (to the sub-unit
  offset of its integer world position) and its environment-mapping u, v equal the formula of section 9 exactly in
  every frame that no tick interrupted;
- a simple rasterizer model of section 9 fed with the engine's per-vertex results (`render.py`) gives pictures that
  look the same as the captured records; on the pixels that are not black, 44..93 % of the indices are equal in the
  tube-only records 5592..5646 and 6790..6898, the rest differ by a texel or a shade level (the engine's fixed-point
  edge and gradient set-up, engine scope). The texture orientation and the affine interpolation are confirmed by the
  variants: a transposed texture matches 21..89 %, perspective-correct interpolation 21..60 % (`render_variants.py`).

---------------------------------------------------------------------------------------------------------------

## 1. What it draws, and when

A camera flies along a LightWave motion path through a bent tube (61 rings of 12 vertices) textured with
`flower/jeesus2.raw` (a green / yellow / blue liquid swirl, one copy of the whole texture per quad, scrolling
diagonally), shaded with fixed per-vertex light levels that make bright and dark bands along the tube. Only a window
of 348 triangles of the tube exists at any time: it advances with the camera, so the tube is built triangle by
triangle ahead and eaten away behind; everything outside it is black. In front of the camera, fixed in camera space
at distance 1500, hangs a flower (`flower/kukka1.sro`: a ball ringed with petals, wider than the screen) that spins in
its own plane and morphs back and forth between kukka1 and a second shape; its texture is `flower/jeesus1.raw`
(red / white swirl) by environment mapping. Script events hide the flower and show it again with a new second shape
and tilt, each with a palette flash.

| records (event ran at) | music | flower | tilt | second shape (morph target) |
|---|---|---|---|---|
| 4963..5093 | 14:00..14:24 | shown | -100 deg | kukka1-2 |
| 5094..5307 | 14:24..14:63 | shown (0x409358, k = 1) | -70 | kukka1-3 |
| 5308..5312 | 14:63..15:00 | hidden (0x4093f0) | | |
| 5313..5576 | 15:00..15:48 | shown (k = 2) | -130 | kukka1-4 |
| 5577..5663 | 15:48..16:00 | hidden | | |
| 5664..5729 | 16:00..16:12 | shown (k = 3) | -80 | kukka1-5 |
| 5730..5751 | 16:12..16:16 | hidden | | |
| 5752..5970 | 16:16..16:56 | shown (k = 4) | -120 | kukka1-6 |
| 5971..6014 | 16:56..17:00 | hidden | | |
| 6015..6080 | 17:00..17:12 | shown (k = 5) | -90 | kukka1-7 |
| 6081..6190 | 17:12..17:32 | hidden | | |
| 6191..6344 | 17:32..17:60 | shown (k = 6) | -100 | kukka1-8 |
| 6345..6365 | 17:60..18:00 | hidden | | |
| 6366..6519 | 18:00..18:28 | shown (k = 7) | -110 | kukka2 (different topology, see 6.3) |
| 6520..6541 | 18:28..18:32 | hidden | | |
| 6542..6711 | 18:32..18:63 | shown (k = 8) | -80 | kukka2-2 (different topology) |
| 6712..6716 | 18:63..19:00 | hidden | | |
| 6717..6782 | 19:00..19:12 | shown (k = 0) | -100 | kukka1-2 |
| 6783..7062 | 19:12..19:63 | hidden (tunnel only) | | |

"Ran at" = the refresh of `full.ticks` at which the script index advanced (the routine ran at the end of the frame
drawn before it; the flash starts at the next refresh, the new flower shows in the next drawn frame, 1..2 records
later). The camera path: LightWave frames 0..260 at 0.15 frame per refresh (1,734 refreshes), then from
frame 0 again at refresh 6696 (a cut: the camera jumps back to the start of the tube) until the part ends at frame
~55. Where no face of the window is in view the background is black: around LightWave frames 6..20 (records
~5000..5090) almost no tube face is in view (the camera looks sideways, forward vector about (-0.9, 0.2, 0.3)) and
the flower is seen over black; in many later records the far end of the window shows as a black hole.

Palette effects in this part (all from the script; NOTES.md "Palette"): 14:00 0x4098c4 sets `flower/jeesus.pal`
(0x400364) and entry 36 flashes white (0x400498: all white, back to the palette in 15 refreshes), 14:04 white; every
0x409358 flashes white when its new k is even and inverse (0x400454: (63 - base) & 63, back in 20 refreshes) when k
is odd; every 0x4093f0 flashes white; further entries: 15:00 inverse (then 0x409358's white flash wins), 15:04, 15:32,
15:36 white, 15:56 inverse, 16:56 and 16:60 white, 17:60 white, 18:32 inverse (after 0x409358's white: inverse
wins), 19:28 inverse, 19:48 fade to black in 40 refreshes (0x400438). When two flashes run in the same frame the
second one replaces the first. The part writes no palette itself. 19:63: 0x409e90 sets the tick and frame routines
to 0 (memory is not freed), the flight part starts.

The frame routine **overwrites every pixel** of the 320 x 200 frame: the engine clears the scene window
(0..319, 0..199) to index 0 (black in `jeesus.pal`) and draws the faces over it. The debug overlay 0x400bec is off
([0x42277c] = 0).

## 2. Static data

From INSIDE.DAT (offsets in the unpacked block, `datfile.py`):

| file | offset, size | format | use |
|---|---|---|---|
| `flower/tunnel1.lws` | 0x296149, 2,631 | LightWave scene, text | camera path (CameraMotion, 19 keys) and one ObjectMotion (the tube, 1 key at the origin); the last byte is overwritten by 0 |
| `flower/tunneli1.sro` | 0x2669c5, 20,324 | SurRender object: 732 vertices, 1,440 faces | the tube |
| `flower/kukka1.sro` | 0x26b929, 2,352 | 111 vertices, 125 faces | the flower and morph target 0 (the second `kukka1.sro` entry at 0x26c259 is never used) |
| `flower/kukka1-2.sro` .. `kukka1-8.sro` | 0x26cb89, 0x26d4b9, 0x26dde9, 0x26e719, 0x26f049, 0x26f979, 0x2702a9; 2,352 each | 111 vertices, 125 faces, same faces as kukka1 | morph targets 1..7 |
| `flower/kukka2.sro`, `kukka2-2.sro` | 0x270bd9, 0x271511; 2,360 each | 109 vertices, 129 faces (other faces) | morph targets 8, 9 |
| `flower/jeesus2.raw` | 0x286149, 65,536 | 256 x 256 8-bit, indices 0..255 | tube texture (through the shade table) |
| `flower/jeesus1.raw` | 0x276149, 65,536 | 256 x 256 8-bit, indices 0..143 | flower environment map (indices direct) |
| `flower/jeesus.shd` | 0x271e49, 16,384 | 64 levels x 256 colours: index = shd[level * 256 + colour]; level 0 = all 0, level 63 = identity | tube shading |
| `flower/jeesus.pal` | 0x275e49, 768 | 6-bit RGB; entry 0 = (0,0,0) | the part's palette |

`.sro` as the loader 0x410bb0 reads it (scale 0x10000 = 1.0): +0 dword file size, +4 dword offset of the vertices
(0x14), +8 dword vertex count, +0xc dword offset of the faces, +0x10 dword face count; vertex 12 bytes: int16 x, y,
z, uint16 u, v (stored >> 4 into the object; overwritten in this part, see 9), 2 more bytes (unused); face 8 bytes:
word (unused, 0), word a, b, c (vertex indices). The engine computes face normals (unit x 1024) and vertex normals
(sum of the face normals, normalised to 0.8 x 1024 = 819) at load (0x410918, 0x410a70).

From INSIDE.EXE:

| what | where | format / value |
|---|---|---|
| morph pairs | 0x4228ac, 9 x 2 dwords | (0, k + 1) for k = 0..8: target indices (taken % 10) |
| phase step | 0x4228f4, 9 floats | 0.02 for every k |
| tilt angle | 0x422918, 9 floats (degrees) | -100, -70, -130, -80, -120, -90, -100, -110, -80 |
| part state | 0x42293c u0 (dword, 0), 0x422940 v0 (dword, 0), 0x422944 window counter (dword, 0), 0x422948 morph phase (float, 0), 0x42294c k (dword, 0), 0x422950 morph active (dword, **1**), 0x422954 spin (float, 0) | initial values in the image |
| spline tension | 0x422868 (dword, set to 0 by 0x409730) | a = 0: Catmull-Rom |
| strings | 0x42286c "LWSC", 0x422871 "FirstFrame", 0x42287c "LastFrame", 0x422886 "ObjectMotion", 0x422893 "CameraMotion", 0x4228a0 "EndBehavior", 0x422958 "flower/tunnel1.lws" | |
| constants | 0x42149d double 0.017452777... (= 3.1415 / 180, LightWave angles), 0x4214a5 double 10.0 (number reader), spline floats 0x4214ad 0.5, 0x4214b1 2.0, 0x4214b5 -3.0, 0x4214b9 3.0, 0x4214bd -2.0; 0x4214d3 / 0x4214f7 double 0.01745329252 (tilt degrees to radians), 0x4214db double 0.75, 0x4214e3 double 0.15, 0x4214eb float 260.0, 0x4214ef double 0.02; light levels 0x4216db 0.05, 0x4216e3 300.0, 0x4216eb 0.04, 0x4216f3 1 / 20.3, 0x4216fb 700.0 (doubles); the LWS scale 3000.0 is an immediate (0x453b8000) | |

BSS: [0x4b6820] scene (0x50 bytes), [0x4b6830] tunnel-mat1, [0x4b6840] tunnel-mat2, [0x4b683c] flower-mat (0x44
bytes each), [0x4b6618 + 4i] objects (i = 0 camera (0xc8 bytes), 1 tube, 2 flower, 3..12 morph targets 0..9;
[0x4b6624 + 4j] = morph target j), [0x4b6698 + 4i] object type (0 camera, 3 others; type 2 would mean "apply the
motion's scale channels", none here), [0x4b6798 + 4i] file pointers, [0x4b6834] jeesus.shd, [0x4b6844] jeesus.pal,
[0x4b682c] jeesus1.raw, [0x4b681c] jeesus2.raw, [0x4b6848] / [0x4b6818] morph targets A / B, 0x426618 + m x 0x4800
the per-frame key table of motion m (up to 512 records of 9 floats), [0x4b6850] text cursor, [0x4b6854] number of
motions (2), [0x4b6858] frames in the table (266), [0x4b685c] the current LightWave frame (float).

## 3. Init 0x409730 (precalc) and the scene reader

```
tag = 0x40b3c0(); [0x4b6838] = tag; 0x40b4d0(tag)          // memory tag for the part
0x40c034()                                                  // resets engine list pointers (0x4b9408.. buffers; engine scope)
scene = alloc(0x50, "flower scene"); mat1 = alloc(0x44, "flower tunnel-mat1");
mat2 = alloc(0x44, "flower tunnel-mat2"); fmat = alloc(0x44, "flower flower-mat"); camera = alloc(0xc8, "flower camera")
file[1] = "flower/tunneli1.sro"; file[2] = file[3] = "flower/kukka1.sro"; file[4..10] = "kukka1-2".."kukka1-8";
file[11] = "kukka2.sro"; file[12] = "kukka2-2.sro"; shd, pal, jeesus1, jeesus2 as above
[0x422868] = 0
if (read_scene("flower/tunnel1.lws", 3000.0) < 0) fatal("fatal shitfucking error")   // 0x408030
```

**read_scene 0x408030(name, scale)**: text = datafile(name) ("scene file not found" if 0); text[0xa46] = 0 (the last
byte, a newline, becomes the terminator); frames = 0x408480(table 0x426618, scale) ("scene read error" if < 0);
[0x4b6858] = frames. Readers (all advance the cursor [0x4b6850]):
- find 0x409050(word): scans for the word's first character, compares the rest; on a match the cursor stays just
  after the first character; -1 at the end of the text.
- int 0x408d70: skips characters until '-' or a digit, reads an optional '-' and decimal digits.
- float 0x408dfc: skips to '-' or a digit; the mantissa digits and the divisor (10 per decimal) accumulate in
  float32 (each step x87 `m * 10.0 + digit` rounded to float); result float32(sign * m / div); an 'e' exponent is
  read as sign and exactly three digits (0x408f9c) and applied with pow(10, e) (not used by this file).

**0x408480(table, scale)**: requires "LWSC"; first = int after "FirstFrame" (1), last = int after "LastFrame"
(265); finds "CameraMotion" and reads motion 0 (0x408588); then rewinds the cursor to the start of the text and
reads every "ObjectMotion" in turn as motions 1, 2, .. (here one: the tube). [0x4b6854] = motions (2). Returns the
camera's frame count. LightMotion, ZoomFactor and the rest of the file are not read (the engine's projection is
fixed, section 9).

**0x408588(table, m, first, last, scale, a)** builds one record of 9 floats per LightWave frame:

```
assert(int() == 9); n = int();
key[1..n] = { x y z h p b sx sy sz (9 floats), frame, linear, tension, continuity, bias (5 floats) }
            // kept per key: x y z h p b frame sx sy sz; linear / TCB are ignored
if ((int)key[1].frame != first) { insert a copy of key 1 as key 2; key[1].frame = first; n++; }
if ((int)key[n].frame != last)  { key[n+1] = key[n], frame = last; n++; }
key[0] = key[1]; key[n+1..n+3] = key[n] with frame = key[n].frame + 1
k = 0
for (i = 1; i <= n; i++) {                       // segment key[i] -> key[i+1]
    span = key[i+1].frame - key[i].frame; steps = (int)span; inv = (float)(1 / span); t = 0.0f
    for (j = 0; j < steps; j++) {                // segments with span <= 0 are skipped
        k++; P(c) = spline(t, a, key[i-1].c, key[i].c, key[i+1].c, key[i+2].c)
        out[k] = { P(x) * scale, P(y) * -scale, P(z) * scale,
                   P(p) * 0.0174527778, P(h) * 0.0174527778, P(b) * 0.0174527778,   // pitch, heading, bank
                   P(sx), P(sy), P(sz) }          // each stored as float
        t = (float)(t + inv)
    }
}
out[0] = out[1]; out[k+1] = out[k+2] = out[k]     // (EndBehavior is read; both of its branches do this)
return k
```

For tunnel1.lws: FirstFrame 1 but the first key is at frame 0, so key 0 is duplicated (frame 1, then frame 0): the
segment 1 -> 0 is skipped and the curve starts at key 0 with p0 = p1. The table holds frames 0..265 at out[1..266]
(k = 266). The ObjectMotion (one key, all 0, scale 1) gives a constant record for the tube.

**spline 0x4090cc(t, a, p0, p1, p2, p3)**: cardinal spline, c = (1 - a) / 2 = 0.5 here (Catmull-Rom), x87
extended precision:

```
p0 * c * (-t^3 + 2t^2 - t) + p1 * ((2 - c) t^3 + (c - 3) t^2 + 1)
  + p2 * ((c - 2) t^3 + (3 - 2c) t^2 + c t) + p3 * c * (t^3 - t^2)
```

## 4. Start 0x4098c4 (script entry 35, 14:00, ran at refresh 4963)

```
0x40b4d0(tag)
0x410878(scene); 0x410520(camera); 0x411154(scene, camera)        // init scene and camera, add the camera
type[0] = 0
tube = alloc(nverts * 64 + 0x10c + nfaces * 52, "flower tunneli"); 0x4108a8(tube)
0x410bb0(tube, file[1], 0x10000, mat1)          // load tunneli1.sro, scale 1.0, every face material mat1
0x410850(tube, 0x414e5c)                        // every face: textured + Gouraud + shade table rasterizer
0x411154(scene, tube)
for (i = 350; i < 1440; i++) face[i].flags |= 1 // hidden; faces 0..349 visible
type[1] = 3; tube.sortbias(+0xcc) = 0
for (i = 0, v = first vertex; v; v = v->next, i++) {   // per-vertex light level, vertex +0x30 (word)
    L = (int)(sin(v.x * 0.05) * 300.0 * cos(v.y * 0.04) + sin(i * (1 / 20.3)) * 700.0);   // x87, trunc
    L += rand() % 600 - 300;                    // 0x4134cd; % of the 0..32767 result
    v.light = clamp(L, 0, 1023);
}
for (j = 0, f = first face; f; f = f->next, j++) if (j & 1) f.material = mat2
flower = alloc(..., "flower jotainpaskaa"); 0x4108a8(flower); 0x410bb0(flower, file[2] = kukka1, 0x10000, fmat)
0x410850(flower, 0x412db0)                      // textured rasterizer, no shading
flower.flags(+0xc5) |= 0x10                     // environment mapping (engine pass 0x41acc4)
type[2] = 3; flower.parent(+0xc) = camera; flower.sortbias = 0x35200; flower.z(+0x18) = 1500   // x, y = 0
0x411154(scene, flower)
for (i = 3; i < 13; i++) {                      // morph targets 0..9, not added to the scene
    obj[i] = alloc(..., "flower morphtarget"); 0x4108a8; 0x410bb0(obj[i], file[i], 0x10000, fmat);
    0x410850(obj[i], 0x412db0); obj[i].flags(+0xc5) |= 0x10; type[i] = 3;
    obj[i].parent = camera; obj[i].sortbias = 0x35200; obj[i].z = 1500
}
0x408090()                                      // type-2 objects (none); [0x4b685c] = 0
scene: +0x38 = camera, +0x20 = 320, +0x24 = 200, window +0x28 x0 = 0, +0x2c x1 = 320, +0x30 y0 = 0,
       +0x34 y1 = 200, +0x3c = 0x100 (focal length 256), +0x44 = 0x2000 (near plane, camera z 64)
mat1, mat2: +0 = +4 = jeesus2.raw, +8 = +0xc = jeesus.shd, +0x18 = 1 (corner coordinates from the material),
            +0x1c = 0, +0x38 = +0x3c = 0xff (u, v masks), +0x40 = +0x41 = 8 (log2 of 256)
fmat:       +0 = +4 = jeesus1.raw, +8 = +0xc = jeesus.shd (unused by 0x412db0), +0x18 = 0, +0x1c = 0,
            +0x38 = +0x3c = 0xff, +0x40 = +0x41 = 8
A = target 0, B = target 1                      // [0x4b6848], [0x4b6818]
0x400af4(0x40940c); 0x400b04(0x4095f8)          // tick and frame routine; part tick counter = 0
0x400364(jeesus.pal)
```

Random numbers: 732 calls of rand() (Watcom LCG, seed at 0x423818) in vertex order. In the capture the seed at the
entry of 0x4098c4 was **0xde09b53b** (0x95a8f93f after; found by running the LCG back 732 steps from the
snapshot). Whether that seed is the same on every run depends on the rand() calls of the parts before (the intro's
tick and event routines; the core scope has the list); the remake can start from this value.

The tube: 61 rings of 12 vertices (ring r = vertices 12r..12r+11), ring radius 192..491 (mean 302), rings 267..617
apart, ring 0 centred at (653, -455, -15340), ring 60 at (2354, -4984, -5925). Segment s (ring s = r0, ring s+1 = r1)
has faces 24s..24s+23: for column c (c' = (c + 1) % 12) face 24s + 2c = (r1[c], r1[c'], r0[c']) (material mat1) and
face 24s + 2c + 1 = (r1[c], r0[c'], r0[c]) (mat2). The light levels make bright and dark bands along the tube (the
1 / 20.3 term has a period of 127.5 vertices = 10.6 rings): 358 of the 732 vertices are 0, 9 are 1023, mean 254.

## 5. Tick 0x40940c (every refresh, from the timer interrupt)

```
count++                                          // [0x422944]
n = (int)(count * 0.75)
if (n + 348 < 1440) { face[n + 348].flags &= ~1; face[n].flags |= 1 }   // show the next, hide the oldest
if (active) {                                    // [0x422950]
    A = target[pair[k].a % 10]; B = target[pair[k].b % 10]
    phase = (float)(phase + step[k])             // 0.02
}
u0 += 0x3b; v0 += 0x3b                           // texture scroll, 1/16 texel units
f = (float)(f + 0.15)                            // [0x4b685c], LightWave frame
if (f > 260.0f) {
    count = 0; f = 0
    faces 0..349: flags &= ~1; faces 350..1439: flags |= 1
}
u0 &= 0xfff; v0 &= 0xfff
spin = (float)(spin + 0.02)                      // [0x422954]
0x4108dc(flower, angle[k] * 0.01745329252, spin, 0)   // rotation: R = Rx(angle) Ry(spin) (section 7)
```

Closed forms (tick i = the i-th tick of the part, i = 1 at refresh 4963): f is a float32 sum of 0.15 (259.94556 at
i = 1733 instead of 259.95); f exceeds 260 at i = 1734 (refresh 6696) and becomes 0; so f = 0.15 (i mod 1734)
within 0.005 (replay the float sum for exactness). count = i mod 1734. Visible tube faces: count = 0: 0..349; else
n = trunc(0.75 count): faces min(n, 1091) + 1 .. max(min(n + 348, 1439), 349) — 348 faces = 14.5 segments; the window
stops at 1092..1439 from count 1456 to the wrap (the camera then flies to the end of the tube with the window
frozen). One triangle appears and one disappears on three ticks of four. u0 = v0 = 59 i mod 4096. spin = 0.02 x
(ticks since the last 0x409358, or since the start); phase = 0.02 x (ticks while active since the last 0x409358).

## 6. Frame 0x4095f8 (once per drawn frame) and the events

```
scene.buffer(+8) = [0x4248d0]                    // the back buffer
0x41127c(scene, 0)                               // clear the window (320 x 200) to index 0
0x4080d8()                                       // camera and objects from [0x4b685c] (section 7)
mat1 corners: (u0, v0), (u0 + 0x1000, v0), (u0 + 0x1000, v0 + 0x1000)          // +0x20..+0x34
mat2 corners: (u0, v0), (u0 + 0x1000, v0 + 0x1000), (u0, v0 + 0x1000)
0x4114c8(flower, A, B, (float)fabs(sin(phase)))  // morph (6.3)
0x40c6ac(scene)                                  // render (section 9)
0x400bec()                                       // debug text, off
0x400b14()                                       // flip
while ([0x422984]) ;                             // wait until the frame is on the screen
```

The wait makes at most one frame per refresh; the capture draws 1,443 frames in 2,100 refreshes (48 fps, frames
1 or 2 refreshes apart). A frame drawn during refresh R uses the state after tick R - 4962 and is visible from
record R or R + 1. Nothing is stepped per drawn frame.

### 6.1 Show 0x409358 (script, 14:24, 15:00, 16:00, 16:16, 17:00, 17:32, 18:00, 18:32, 19:00)

```
phase = 0; flower.flags(+0xc4) &= ~1; active = 1
k = (k + 1) % 9; spin = 0
0x4108dc(flower, angle[k] * 0.01745329252, 0, 0)
if (k % 2 == 0) white flash (0x400498) else inverse flash (0x400454)
```

A and B change at the next tick, so the first frame after the event morphs the old pair with weight |sin 0| = 0,
i.e. sets the vertices both old targets have to kukka1's.

### 6.2 Hide 0x4093f0 (script, 14:63, 15:48, 16:12, 16:56, 17:12, 17:60, 18:28, 18:63, 19:12)

```
flower.flags(+0xc4) |= 1                         // not drawn
active = 0                                       // phase, A, B freeze; spin and the rotation keep running
white flash (0x400498)
```

### 6.3 Morph 0x4114c8(obj, A, B, w) (engine routine used only here)

For the first min(A.faces, B.faces) faces: obj.face.normal = trunc(A.normal + (B.normal - A.normal) w); for the
first min(A.vertices, B.vertices) vertices: obj.v.normal and obj.v.position likewise (integers, x87 trunc). The
faces, their vertex indices and everything else stay those of kukka1. Targets 1..7 share kukka1's faces, so the morph
is a per-vertex blend of two shapes of the same flower. kukka2 / kukka2-2 (k = 7, 8) have 109 vertices and other
faces: kukka1's faces are drawn with kukka2's (kukka2-2's) vertices 0..108 by index, whatever those vertices are in
their own model (at full weight the flower becomes long crossed spikes, record 6600), and vertices 109, 110 are not
morphed: they keep the values the first frame after the event gave them, kukka1's (-500, 45, -300) and
(-151, 46, -303) and their normals. The vertex
normals are blended, not renormalised (shorter between the shapes), and the face normals too (used by the engine's
back-face test). w = |sin(phase)|, phase += 0.02 per tick: the flower opens and closes with a period of 157 ticks
(2.24 s), starting from kukka1 at each show.

## 7. Camera and objects 0x4080d8 (from the frame routine)

```
fi = (int)f; idx = fi % frames + 1; fr = (float)(f - fi)      // frames = 266; idx <= 261 here
for (m = 0; m < motions; m++) {                // m = 0 camera, 1 tube
    T = table + m * 0x4800                     // records of 9 floats
    S(c) = spline(fr, 0, T[idx-1].c, T[idx].c, T[idx+1].c, T[idx+2].c)
    0x410868(obj[m], (int)S(0), (int)S(1), (int)S(2))          // position, trunc
    0x4108dc(obj[m], 0, 0, 0)                  // identity
    0x4145ac(&obj[m].matrix, S(4))             // M = M * Ry(heading)
    0x4145c4(&obj[m].matrix, -S(3))            // M = M * Rx(-pitch)
    0x4145dc(&obj[m].matrix, -S(5))            // M = M * Rz(-bank)
    if (type[m] == 2) 0x409258(obj[m], copy[m], S(6), S(7), S(8))   // scale (not used)
}
```

with Rx(a) = [[1,0,0],[0,cos a,-sin a],[0,sin a,cos a]], Ry(a) = [[cos a,0,sin a],[0,1,0],[-sin a,0,cos a]],
Rz(a) = [[cos a,-sin a,0],[sin a,cos a,0],[0,0,1]] (the matrix at object +0x28, 9 doubles, row-major; verified).
0x4108dc(obj, a, b, c) sets M = Rx(a) Ry(b) Rz(c). The camera's world matrix is M (columns = camera axes in the
world: x right, y down, z forward); a world point P is at Q = M^T (P - C) in camera space. The flower (parent = the
camera, position (0, 0, 1500), rotation R = Rx(angle[k]) Ry(spin)) is at Q = R p + (0, 0, 1500) for its object
point p: it never moves on the screen except for the spin, the tilt and the morph (its world position is rounded to
integers by the engine, a sub-unit offset). The tube's motion is the identity.

So the camera at LightWave frame f is a Catmull-Rom spline over the per-frame table, which is itself a Catmull-Rom
spline over the keys sampled at integer frames: at integer frames the two agree, between them the second spline
smooths the first. LightWave y is negated (world y down), positions x 3000, angles in degrees x 3.1415 / 180.

## 8. Time bases

| variable | time base | formula |
|---|---|---|
| f (LightWave frame, camera) | tick | float32 sum of 0.15 per tick from 0 at the start; 0 at tick 1734 (f > 260); continuous: f = 0.15 t, t = ticks mod 1734 |
| face window | tick | n = trunc(0.75 count), count = ticks mod 1734 (section 5); discrete: keep it per tick |
| u0 = v0 (tube texture scroll) | tick | 59 / 4096 of the texture per tick in u and v (3.69 texels), wraps every 4096 / 59 = 69.4 ticks |
| spin | tick + event | 0.02 rad per tick from 0 at the start and at each 0x409358 |
| phase (morph weight \|sin phase\|) | tick + event | 0.02 per tick while shown, from 0 at each 0x409358; frozen while hidden |
| k, A, B, tilt, visibility | event | section 6; A, B follow k at the next tick |
| light levels, geometry | static | section 4 (rand seeded as found) |

Nothing depends on the frame rate. Events run at the end of the first frame drawn after the music position, so their
refresh can move by one with the frame timing (core scope).

## 9. How a pixel is made

Render 0x40c6ac(scene) runs the engine's passes (engine scope): place objects (world matrix of the flower = M_cam R),
transform vertices to camera space, back-face cull (single-sided faces; face normal test and the screen winding
test), skip hidden faces (flags bit 0), environment-map the flower, sort all visible faces of the scene by a key and
draw them far to near (painter's algorithm, no z-buffer), near-clip at camera z = 64.

- **Projection** (fitted to the engine's results: 98 % of vertices to 1/8 px, the rest near-clipped):
  x = 160 + 256 X / Z, y = 100 + (256 x 27306 / 32768) Y / Z (pixels of the 320 x 200 screen, 1/16 px precision;
  the y factor 213.33 = 256 / 1.2 makes the 4:3 display square). Horizontal field of view 2 atan(160 / 256) = 64 deg.
- **Draw order**: key = max(0, (max of the face's three camera z x 128) - sortbias) / 16, larger first. The tube has
  bias 0; the flower 0x35200 = 1700 units x 128, so a flower face sorts as if 1700 units nearer: it is drawn over the
  tube except where a tube face with max z < (flower face max z - 1700) overlaps it (tube walls within ~350 units of
  the camera against the flower's far petals: 135 of 140 dumped flower frames have such tube faces). A remake can
  get the same picture with a depth test in which the flower's depth is z - 1700 (and a depth buffer instead of the
  painter's order within each object).
- **Tube pixel** (0x414e5c via the material, flags bits 10..12 = 0: the **affine** path, no perspective
  correction): u, v and the light level interpolated linearly over the screen triangle from the corners; corner
  texture coordinates from the face's material (mat1 / mat2, section 6; 1/16 texel units, so 0x1000 = the whole
  256 texels: every quad of the tube shows the whole texture once, u around the ring from r1[c] to r1[c+1], v along
  the tube from ring s+1 back to ring s, shifted by (u0, v0)); texel = jeesus2[(v >> 4) & 255][(u >> 4) & 255]
  (row from v, column from u); pixel = jeesus.shd[((light + 1) >> 4) * 256 + texel] (the engine adds 1 to the
  starting value of every interpolant). Light 1023 at a vertex gives level 64, one row past the table (the bytes of
  `jeesus.pal` follow in the DAT block): a stray pixel at the 9 vertices with 1023.
- **Flower pixel** (0x412db0, affine): texel of jeesus1 at the environment-mapped u, v, written directly (no
  shading). Per vertex (engine pass 0x41acc4, verified): with N = the vertex normal (length ~819) and R the
  flower-to-camera rotation (= R above), X, Y = camera-space x, y of the vertex x 128 (Y also x 27306 / 32768):
  `U = trunc(2 N . R[0] + 2048); V = trunc(2 N . R[1] + 2048); u = -(U & 0xfff) - X / 64; v = (V & 0xfff) + Y / 64`
  (divisions truncate toward zero; R[0], R[1] = the first two rows). In texels: u = -(128 + 102.4 n'x) - x / 8,
  v = 128 + 102.4 n'y + (5/6) y / 8 for the unit camera-space normal n' and camera-space x, y in world units: the
  normal term covers 80 % of the texture's disc (jeesus1 is a round picture on black), and the position term slides
  the texture by up to 125 texels across the flower.
- **Background**: index 0 (the clear) where no face is drawn: outside the face window, beyond the ends of the tube,
  and where the camera looks back.

## 10. Pictures

- `flower/jeesus2.raw` (256 x 256): a swirling liquid texture in greens, yellows and blues; made by the artists
  (not by the code). It is read **through the shade table** (index -> shd[level][index]), so a remake must keep its indices:
  no redraw; the remake can shade in true colour with palette[shd[level][index]], interpolating between levels.
- `flower/jeesus1.raw` (256 x 256): a round red / white swirl on black (indices 0..143), used as the flower's
  environment map, indices straight to the palette. Made by the artists; a redraw would be seen only through the
  environment mapping (and the twirl part shows it as a picture, notes-twirl.md). It is not drawn as it is here.
- `flower/jeesus.pal` (palette of both), `flower/jeesus.shd` (64 x 256 shade table, level 63 = identity).

## 11. Shortcuts a remake should evaluate exactly

- Affine texture mapping of every triangle (both objects): use perspective-correct interpolation (the affine warp
  is visible on the tube's quads, which are seen at grazing angles).
- Painter's order by per-face max z: use a depth buffer (with the flower's -1700 bias, section 9).
- Light levels: 64 shade levels from an integer 0..1023 interpolated per pixel: use the continuous level
  (light + 1) / 16 and blend the shade table's neighbouring rows in colour; the light values themselves (ints) are
  part of the content.
- Integer positions: camera position truncated to integers per frame, flower vertices and normals truncated after the
  morph, vertex normals of length 819 truncated: use floats.
- Environment-map coordinates truncated to integers per vertex: compute them as floats per vertex and interpolate
  them perspective-correct (the 0xfff mask never wraps: with normals of length <= 819, U and V stay in 410..3686;
  the result wraps through the 256-texel texture as before).
- Texture reads nearest-neighbour 256 x 256: filter (both textures tile).
- The near plane at camera z = 64 (faces cut there) and the 1/16 px vertex snapping: render at full precision
  (keep a near plane small enough not to cut the tube walls the original shows).
- The camera's double spline is the content: keep it (cheap to evaluate exactly); also keep pi = 3.1415 for the
  LightWave angles and the float32 sum of f, or at least f = 0.15 t with the wrap at 1734 ticks.

## 12. Not determined / remarks

- The engine's exact rasterization (edges, fixed point, the +1 bias) is the engine scope's; the model of section 9
  matches about half of the non-black pixel indices exactly, the rest by a texel or a shade level.
- Whether the rand() seed at 14:00 is the same on every run (core scope); the capture's value is given in section 4.
- The first frame of the part (drawn before the first tick, record 4964 under the white flash) shows the flower
  with the identity rotation, i.e. edge-on.
- `run_inside.py --load` (shared tool) restarts its refresh clock at 0, so a resumed run records frames without
  emulating; `models/flower/run_flower.py` resumes it at (snapshot frame + 1) x FRAME.
