# Reve — the 3D entries (engine scope)

The script entries that draw with the 3D engine (`notes-engine.md`): 1–4 (INTRO with the credits), 8 (P3D1), 11
(PART2), 14 (P3D3), 22 (PART4) and 26 (FIN). Each loads a world (`3D\<name>.WLD`) and its animation
(`3D\<name>.ANI`), steps the animation once per refresh in its retrace routine and renders it in its frame routine.
Refreshes are the capture's records (`full.log`, `full.ticks`); music positions are order:row.

| entry | init | done | retrace | frame | ends at | init at | retrace set (r0) | next entry | world | 2D after the render |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 0x2048e2 | 0x20483f | 0x25f3ae | 0x2049c4 | 2:00 | 648 | 652 | 1327 | INTRO | AA pass, credits "LCA" |
| 2 | 0x20483f | 0x20483f | 0x25f3ae | 0x204a10 | 3:00 | 1327 | 1327 | 1953 | (INTRO) | AA pass, credits "AJT" |
| 3 | 0x20483f | 0x20483f | 0x25f3ae | 0x204a5c | 3:32 | 1953 | 1953 | 2371 | (INTRO) | AA pass, credits "ANTONY" |
| 4 | 0x20483f | 0x20497a | 0x25f3ae | 0x204aa4 | 4:00 | 2371 | 2371 | 2736 | (INTRO) | AA pass, credits "SHAD" |
| 8 | 0x204d3d | 0x20497a | 0x25f3ae | 0x204dc8 | 20:00 | 4089 | 4097 | 6328 | P3D1 | AA pass |
| 11 | 0x2057b0 | 0x20497a | 0x25f3ae | 0x205850 | 28:00 | 7224 | 7230 | 8119 | PART2 | none |
| 14 | 0x205881 | 0x20497a | 0x25f3ae | 0x20590c | 40:00 | 9911 | 9915 | 10806 | P3D3 | AA pass |
| 22 | 0x205a11 | 0x20497a | 0x25f3ae | 0x205ac0 | 56:00 | 14481 | 14492 | 15377 | PART4 | wireframe boxes, AA pass |
| 26 | 0x205bb0 | 0x204ee9 | 0x205c87 | 0x205c90 | 62:00 | 16272 | 16276 | 16720 | FIN | blend into the previous screen |

(End positions are the script table's: 0x0200, 0x0300, 0x0320, 0x0400, 0x1400, 0x1c00, 0x2800, 0x3800, 0x3e00, as
order << 8 | row.)

## What is common to all of them

**Init.** Every world entry's init (1, 8, 11, 14, 22, 26) does the same steps; entries 8, 11, 14, 22 and 26 first
clear the screen (0xa0000) and the back buffer [0x205e8c]:

```
world_init(name):                     // name = the string the init passes, e.g. 0x205d39 "3D\INTRO.WLD"
  0x21d440(name, 0x217460)            // load the world (notes-engine.md "The WLD file")
  replace the name's extension: the dword ".ANI" is written over ".WLD" in place (in the image's string)
  load the ANI with service 0209h → [0x217468]
  camera block 0x21743c: dwords +0, +4, +8 = 0
  load GFX\ANTIALIA.PAL → 0x20643c (65,536 bytes)
  0x25f3ae()                          // key 0: the absolute start state
```

Entry 1 also loads `GFX\GOURAUD.PAL` → 0x21643c (4,096 bytes), which stays for all later 3D entries, and
`GFX\CREDITS.RAW` → [0x205e80] (pixels only). The textures are the pixels of `GFX\TEXTURE.RAW` in [0x21b140]: loaded
there with its palette by entry 0 (and again by entry 12); entries 11, 22 and 26 load `GFX\TEXTURE.RAW` into
[0x205e80] with EAX = 1 only to set its palette (the pixels there are not used). The palette during every 3D entry is
TEXTURE.RAW's (`v >> 2` of its 8-bit values) and does not change (checked: one palette in the capture over each
entry; no palette effects).

**Retrace routine.** 0x25f3ae (FIN: 0x205c87, which calls it): one animation step per refresh. **Everything that
moves is stepped per refresh**: the camera (position, yaw, pitch, roll) and the objects' positions and angles, as
`key 0 + n delta steps` with **n = r − r0** (r0 = the refresh at which the entry's retrace routine was set, table
above). Nothing advances per drawn frame except FIN's blend (below). No random numbers.

**Frame routine.** Renders the state of the last step (0x25f5b5 into [0x205e8c]), then the entry's 2D pass. The frame
shown at capture record R was drawn from the state at n = R − r0 − k: k = 1 or 2 in most records (up to 3–4 in slow
P3D1 frames), k = 0 in FIN. Each frame overwrites the whole screen: the back buffer starts black (cleared after
every use), the AA pass writes all but its border (which stays black), PART2 copies all of it, FIN blends every
pixel.

**Done.** 0x20497a frees the world and the ANI and resets the key index, the counter, the key pointer, the object
count and the object pointer (so the next entry's first step reads key 0). FIN's done is 0x204ee9, the done routine
of the 2D entries 10 and 15: it frees [0x217588] and leaves FIN's world allocated (nothing uses it afterwards).

## Entries 1–4: the INTRO (refreshes 652–2736, music 1:00–4:00)

A chrome surfer (a figure on a surfboard) flies around a large knotted tube (a closed knot like a pretzel,
red with purple bands), the camera circling them; over it, centred, the name of one member of the group: "LCA"
(entry 1), "AJT" (2), "ANTONY" (3), "SHAD" (4).

World `3D\INTRO.WLD` (2 objects): 0 = the surfer (499 vertices, 974 faces, type 8, tile 10 = TEXTURE.RAW (128,
128)), 1 = the knot (888 vertices, 1760 faces, type 6, tile 0 = (0, 0)). `3D\INTRO.ANI`: 25 keys of 25 refreshes; the
delta keys move the camera and the surfer only (the knot stays at (20, 6, −44), unrotated).

- **One animation for all four entries.** Entries 2–4 have no init: the retrace routine stays 0x25f3ae, so the
  INTRO's animation runs on through refreshes 652..2736 with n = r − 652. The ANI loops every 25·25 + 1 = **626
  refreshes** (restarts at refreshes 1278, 1904, 2530): after the 625th step the camera is at (340, −420, 136), yaw
  −5326 (≡ 2866 mod 8192), pitch −301, and the reset puts it back to key 0: (344, −424, 136), yaw 2867, pitch −297;
  the surfer has made exactly one turn and is back at its start. So the loop is seamless except for a camera jump
  of 4 units in x and y, 1/8192 turn of yaw and 4/8192 turn of pitch at each restart. Verified: the snapshots'
  state at refreshes 1400 and 2100 equals the model's at n mod 626.
- **Frame routine** 0x2049c4 / 0x204a10 / 0x204a5c / 0x204aa4: render; AA pass from [0x205e8c] into [0x205e88]
  (which clears [0x205e8c]); the credits sprite into [0x205e88]; copy [0x205e88] to 0xa0000 (64,000 bytes).
- **Credits** (0x25f0be: `ESI` = source, `BL` = width, `BH` = height, `EDX` = stride, `EDI` = destination): the
  sprite is centred: top-left `(160 − (w >> 1), 100 − (h >> 1))`, every non-zero source pixel copied (index 0
  transparent), no scaling, drawn after the AA pass (so sharp). Source `GFX\CREDITS.RAW` (231 × 256, its own palette
  ignored: shown in TEXTURE.RAW's palette); four bands of 64 rows:

  | entry | offset (row) | width × height | top-left on screen | text | ink in the band |
  |---|---|---|---|---|---|
  | 1 | 0x7380 (128) | 120 × 64 | (100, 68) | LCA | x 5..119, rows 135..189 |
  | 2 | 0xad40 (192) | 130 × 64 | (95, 68) | AJT | x 5..129, rows 200..254 |
  | 3 | 0 (0) | 231 × 64 | (45, 68) | ANTONY | x 4..230, rows 12..54 |
  | 4 | 0x39c0 (64) | 151 × 64 | (85, 68) | SHAD | x 4..150, rows 67..109 |

  The letters: a thin, tall art-deco face (40–55 pixels high), strokes three pixels of white (index 1) with a
  two-pixel black outline (index 79: 0, 0, 0, drawn, not transparent) on each side and grey (69: 41, 41, 42; 74:
  20, 21, 21) on diagonals; letters spread wide ("A  N  T  O  N  Y"), mixed shapes (a small "c" with a bar under it
  in LCA, a "j" with a bar over it in AJT, an underlined "o" in ANTONY, a tall "L"). The black outline hides the 3D
  behind the strokes. A redraw keeps the text, the face, the outline, the size, the spacing and the position.
- **Frame rate** in the capture: 51.3 / 51.4 / 50.3 / 51.1 frames per second (0.73 frames per refresh; 1 or 2
  refreshes per frame).

## Entry 8: P3D1 (refreshes 4097–6328, music 10:02–20:00)

The surfer flies through a space of floating textured objects: rows of blue cubes, red ornamental crosses and stars,
long ornamental beams; at the end a large spiky head (a sun-like face with silver and orange spikes and a red
tongue) approaches and turns.

World `3D\P3D1.WLD` (35 objects): 0 surfer (type 8); 1, 2 ornaments (60 vertices, type 6, tile 2); 3–12 and 16–29
cubes (8 vertices, type 6, tiles 5 / 10); 13, 14 (298 vertices, type 8, tile 7); 15 (78 vertices, type 6, tile 6);
30–33 (463 vertices, type 6, tile 2); 34 the head (400 vertices, type 8, tiles 7, 13, 9). `3D\P3D1.ANI`: 212 keys of
11 refreshes (period 2333: no loop within the entry); the delta keys move 28 of the 35 objects (the surfer, the
beams 30–33 and object 15 in most keys).

- **Init** 0x204d3d (8 refreshes of loading: init at 4089, retrace set at 4097; n = r − 4097).
- **Frame routine** 0x204dc8: render; AA pass from [0x205e8c] straight into 0xa0000.
- **Frame rate**: 45.6 fps (0.65 per refresh; 1, 2 or 3 refreshes per frame).
- The capture loses one animation step at refresh ~5140 (its timer interrupt stalls for two refreshes): from there
  the captured P3D1 runs one step behind n = r − 4097. Not part of the demo.

## Entry 11: PART2 (refreshes 7230–8119, music 24:02–28:00)

The surfer rides over a red-and-pink textured landscape (a crumpled terrain close below the camera) toward a blue
spiked tower that floats above it; then the camera pulls back and the surfer is seen alone.

World `3D\PART2.WLD` (3 objects): 0 surfer, 1 the landscape (851 vertices, 1584 faces, type 8, tile 6), 2 the tower
(698 vertices, 1284 faces, type 8, tile 10). All faces Gouraud-textured. `3D\PART2.ANI`: 100 keys of 10 refreshes;
the delta keys move only the surfer, which turns about all three axes (so the lighting point's "negated angles"
rotation differs from the true inverse here: notes-engine.md "Lighting").

- **Init** 0x2057b0 (init 7224, retrace set 7230): also loads TEXTURE.RAW's palette.
- **Frame routine** 0x205850: render; copy [0x205e8c] to 0xa0000; clear [0x205e8c]. **No AA pass**: PART2 is the one
  3D entry whose pixels are not blurred, and it has no black border.
- The landscape crosses the near plane: its clipped faces get u = v = 0 and shade level 0 (white) at the clip points
  (89 such faces in a checked frame), where the terrain passes under the camera at the bottom of the screen
  (notes-engine.md "Near-plane clipping"; I did not isolate how they look).
- **Frame rate**: 53.7 fps (0.77 per refresh).

## Entry 14: P3D3 (refreshes 9915–10806, music 36:01–40:00)

The surfer (unshaded here) glides past a big blue sphere-like object and over a floating checkerboard of blue and
red ornamental cubes with small red "trees" standing on it; the camera turns over the board.

World `3D\P3D3.WLD` (29 objects): 0 surfer (type 6: texture only, no lighting), 1–16 cubes (type 6, tiles 5 / 10),
17–24 trees (130 vertices, type 6, tiles 5 / 6), 25 (78 vertices, type 6, tile 10), 26–28 small objects. All faces
type 6. `3D\P3D3.ANI`: 110 keys of 9 refreshes; the delta keys move the surfer and the eight trees.

- **Init** 0x205881 (init 9911, set 9915).
- **Frame routine** 0x20590c: render; AA pass into 0xa0000.
- **Frame rate**: 59.9 fps (0.85 per refresh).

## Entry 22: PART4 (refreshes 14492–15377, music 52:03–56:00)

The surfer rides along a path of blue tiles through huge flat-shaded 3D letters and numbers (white and beige to
dark red: words and numbers such as "Art", "Culture", "love", "We", "LC93", "01,229", "341539"), chrome-and-pink
rings and pillars; every object is outlined by the pale wireframe of its bounding box. `3D\PART4.ANI` moves only the
surfer and the camera.

World `3D\PART4.WLD` (188 objects): 0 surfer; 1–65 and 97–168 path tiles (8 vertices, type 8, tile 10); 66–74, 76,
78–83, 183–187 the letters and numbers (type 1: flat, lit per face, colour ramp 80..95); 75, 77 (114 vertices,
type 8, tile 13); 84–96 and 169–182 rings (104 vertices, type 8, tiles 13 / 3). `3D\PART4.ANI`: 100 keys of 10
refreshes.

- **Init** 0x205a11 (init 14481, set 14492): TEXTURE.RAW's palette, then the world, and two switches:
  `[0x21d43c] = 0x21dfb8` (the stricter box cull: an object is drawn only when a box corner projects inside the
  screen) and `[0x2174c6] = 1` (after every render, the bounding boxes of all 188 objects are drawn in colour 0x80:
  notes-engine.md "PART4's bounding boxes"). Its done routine does not undo them; FIN's init does.
- **Frame routine** 0x205ac0: render (with the boxes); AA pass into 0xa0000. So the box lines are blurred by the AA
  pass like the rest (one-pixel lines become soft two-to-three-pixel lines).
- **Flat faces** (the letters): colour = 80 + level, level from the light at the object's origin (notes-engine.md
  "Lighting"): all faces of a letter that face the same way have one colour; the ramp 80..95 runs from white
  (63, 63, 63) through beige (62, 57, 52), (61, 50, 42) to dark red and black (0, 0, 0).
- **Frame rate**: 45.1 fps (0.64 per refresh).

## Entry 26: FIN (refreshes 16276–16720, music 60:01–62:00)

A spiked ellipse logo flies in from the distance, turning: a ring with a brown "WAKE UP"-like lettering inside and
blue spikes, with "PULSE" in blue 3D letters arranged around it; everything leaves trails (each frame is blended into
the previous screen).

World `3D\FIN.WLD` (1 object: 653 vertices, 1230 faces, type 6, tiles 5, 10, 9). `3D\FIN.ANI`: 45 keys of 10
refreshes; the object turns three times about its x axis.

- **Init** 0x205bb0 (init 16272, set 16276): clears the screen and back buffer, TEXTURE.RAW's palette, the world,
  ANTIALIA.PAL; copies the 16 × 16 texels at (row 80, col 16) of [0x21b140] to [0x205e90] (for the 2D entries that
  follow; FIN does not use it); `[0x21d43c] = 0x21ddf8`; the key-0 step; `[0x2174c6] = 0` (no boxes).
- **Retrace routine** 0x205c87: `pushal; call 0x25f3ae; popal`.
- **Frame routine** 0x205c90: render; 0x2046c7 blends the back buffer into the screen: `screen = T[new][screen]`
  (≈ the average of the new frame and what was on the screen), then clears the back buffer.
- **Time base of the trail: per drawn frame.** The capture draws a frame every refresh in FIN (70.1 fps, 443 of 443
  frame gaps are one refresh), so in the capture the trail halves per refresh. The remake evaluates it per refresh
  (notes-engine.md "What the remake should evaluate"): displayed = Σ_k 2^−(k+1)·frame(n − k), starting from the black
  screen of FIN's first refresh.
- The frame drawn from state n is on the screen at record r0 + n (k = 0): FIN's render is fast enough to finish
  within the refresh.

## Pictures and textures

- `GFX\TEXTURE.RAW` (256 × 256, 16 tiles of 64 × 64): the textures of all six worlds. Tiles used (index = row/64·4 +
  col/64): 0 (INTRO knot, type 6), 2 (P3D1, 6), 3 (PART4, 8), 5 (P3D1, P3D3, FIN, 6), 6 (P3D1, P3D3: 6; PART2: 8),
  7 (P3D1, 8), 9 (P3D1: 8; FIN: 6), 10 (every part's surfer and more: 6 and 8), 13 (P3D1, PART4, 8). Type-8 faces
  read the texels **through GOURAUD.PAL** (palette indices into a shade table), so these tiles stay the original's
  indices (a redraw would have to keep them; at most an index-preserving upscale). Tiles read only by type-6 faces
  (0, 2, 5) go straight to the palette. They are mapped on 3D geometry with 64-texel repeat.
- `GFX\CREDITS.RAW`: the four names above (a hand-drawn text picture; drawn as it is: a redraw candidate).
- `GFX\GOURAUD.PAL`, `GFX\ANTIALIA.PAL`: tables (notes-engine.md).

## What the remake does per entry

- Evaluate the state continuously at n = r − r0 (fractional between refreshes; INTRO modulo 626 with its reset
  refresh), render (notes-engine.md), then: INTRO: the AA blur with the black border and the sharp credits on top;
  P3D1, P3D3, PART4: the AA blur with the border; PART2: no blur, no border; PART4 also draws the box wireframes
  before the blur (in colour 0x80, as anti-aliased lines of about one original pixel); FIN: the per-refresh trail.
- Known differences to expect against the capture: the capture shows each frame 1–3 refreshes late, some records
  are torn (the AA pass and PART2's copy write straight to the screen while the capture's retrace record is taken),
  and P3D1 is one step late from refresh ~5140.

## Verification

`models/engine/` (README there):

- Memory dumps at the render's start and end in every entry (`run_reve.py --dumps 25f5b5,25f65b` from the snapshots;
  FIN from f16100 through its init): the model's render equals the back buffer in all 37 renders (INTRO 5, P3D1 10,
  PART2 5, P3D3 6, PART4 5, FIN 6: `test_render.py`), and with the 2D passes the captured screen (36 records; one
  torn: `test_frame.py`).
- End to end from the archive files and n = R − r0 − k (`model_part.py ENTRY`): the table below.

| entry | sampled records (every 37th; FIN every 20th) | exact, by age k (records) | torn | other |
|---|---|---|---|---|
| 1–4 INTRO | 58 | 58 (k = 1: 30, k = 2: 28) | – | – |
| 8 P3D1 | 61 | 51 (k = 1: 6, 2: 31, 3: 13, 4: 1) | 9 | 1 split state (objects 1.. one step newer) |
| 11 PART2 | 24 | 24 (k = 0: 4, 1: 10, 2: 9, 3: 1) | – | – |
| 14 P3D3 | 24 | 17 (k = 0: 4, 1: 10, 2: 3) | 6 | 1 split state |
| 22 PART4 | 24 | 19 (k = 1: 11, 2: 7, 3: 1) | 2 | 3 records differ only in box lines to corners behind the camera (1,491–2,564 pixels, all on two or three stray lines) |
| 26 FIN | 22 | 22 (k = 0) | – | – |

"Torn": the record equals a newer model frame up to a pixel and an older one from there (the AA pass and PART2's
copy write straight to 0xa0000 while the capture's retrace record is taken). "Split": the timer interrupt arrived
during the render's object loop (notes-engine.md). The stale outcode bytes decided 929 (P3D1), 105 (P3D3) and 6
(PART2) object tests in these frames, none of which would have put a pixel on the screen.

## Open

- The exact words of PART4's letter objects and FIN's lettering are only read from the capture by eye (the meshes
  are the authority).
