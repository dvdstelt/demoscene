# MUSTARD.PAK pictures and the video clips: inventory for redraws and restored video

What each of the 45 files in `MUSTARD.PAK` and the four `*.LZW` clips is, which part uses it and how, and whether a
high-resolution redraw (or a restored video frame) can replace it in the remake's `art/` folder (see
[../remake/README.md](../remake/README.md), "Redrawn pictures and restored video", and [AGENTS.md](../../AGENTS.md),
"Redrawn pictures"). Times are video seconds (the remake's cues, `remake/src/timeline.cpp`), rounded.

Dumps (not in Git, made from the originals):

- `re/dump/pictures/<name>.png`: every `.raw` picture with its own palette (6-bit VGA value v shown as v * 255 / 63,
  the same 8-bit colours the remake's `Ctx::tex()` uploads); files without a palette as 8-bit grey (their values).
  Stacked files (`bd2`, `blobby`, `tex3d`, `tunnel`) are one PNG with the tiles stacked as in the file.
- `re/dump/video/<CLIP>/NNNN.png`: every frame of every clip, decoded as the remake decodes it (section 3).
- `re/dump/video/<CLIP>.mp4`: the same frames at the clip's playback rate.

Made with a short script (PAK reader `remake/tools/lzw.py`, PIL) and `remake/tools/dumpvideo.cpp`. A check: an `art/`
folder holding the 1x picture dumps and the 1x video dumps gives the same frames as `--original-art` (41 shots over
the whole demo, maximum difference 0), so the dumps are exactly the remake's colours and layouts.

## Redraw candidates

Ordered by how long they are on screen. "As it is" = the remake reads the picture as colours (or grey intensity)
through `Ctx::tex()`, so `art/<name>.png` replaces it with no code change; "needs code" = the remake reads palette
indices and a redraw first needs a conversion (section 2). Redrawn so far (`remake/art/README.md`): 1, 2, 3, 4, 6,
7, 8, 9, 10, 11, 12, 14, 15; kept: 5 (gradients), 13 (photos of real people).

| # | file | shows | on screen | how it is used | redraw |
|---|---|---|---|---|---|
| 1 | `tex3d.raw` tile 1 | purple bevelled hexagon plates | ~60 s (typo, blobs, plasma, face floor) | floor and ceiling of the hex corridor, the face-floor tiles; darkened with depth | as it is; must tile seamlessly (wraps every 256 texels) |
| 2 | `neuron.raw` | tangled glowing threads (teal, mauve, white) on black | ~41 s (intro) | sphere-mapped full-screen background, ghosted | as it is; must tile seamlessly |
| 3 | `final.raw` | three hand-lettered credit lines, a strip of 8 hand-drawn icons, a photo of a Roland MC-303 with a dark vignette (grey) | ~35 s (icons part to the end) | dim 7x5 icon grid all the time; each line and then the photo blurred in and out | as it is, grey; every line and icon at its rows and columns (exe tables) |
| 4 | `opening1.raw` | "statix", "vic", "acme" over a grey flower (grey) | ~27 s (intro) | intensity added through intro.fad, through a ripple and a repeating swirl | as it is, grey; layout in place |
| 5 | `tex3d.raw` tile 5 / `wipeout.raw` | vertical colour stripes, each a dark-to-bright ramp | ~29 s / ~14 s | warped full screen (flowers, face plate; Aap's stripes) | not worth it: dithered gradients; a smooth (de-dithered) version at most |
| 6 | `tex3d.raw` tile 0 | gold chrome ball | ~25 s (metaballs, plasma) | environment map, read inside the disc u, v = 128 +- 126 (metaballs) | as it is; keep the disc layout exactly |
| 7 | `303b.raw` | the "303" glyph strip (grey) | ~13 s (intro, 27.7-40.9) | glyphs spread apart, blurred horizontally, added through intro.fad | as it is, grey; the 11 glyph column ranges (0x52eb8 / 0x52ee4) exactly in place |
| 8 | `phong.raw` | soft multicoloured starburst | ~13 s (alex and vic parts) | environment map of the spikey object (disc) | as it is; little to gain (soft) |
| 9 | `tunnel.raw` tiles 0, 1 | brown wavy streaks; a glowing flower on violet | ~10 s (rings) | 0: tunnel wall (darkened with depth); 1: env map of the spikey sprites | as it is; 0 must wrap, 1 keeps the disc layout |
| 10 | `blobby.raw` tile 0 | pastel blobs | ~10 s (blobby) | sphere-mapped background and env map | as it is; must tile seamlessly |
| 11 | `bd2.raw` tiles 0, 1, 2 | "like you never did before" and "every moment will be lost" typographic pictures; dark navy blotches | ~5 s + ~3 s (typo, wire blobs) | full screen, whitened by the wire render through wirecut.fad | needs code: wirecut.fad as a blend towards white in true colour |
| 12 | `credits.raw` | the words code, music, vocals, statix, vic, emma (grey) | ~8 s (alex and vic parts) | coverage (>> 3) added to the photo's index, jittered, with a shadow | as it is, grey; every word at its source columns (table 0x52f20) |
| 13 | `alex.raw`, `vic.raw` | photos (a blurred sepia portrait; Vic in a cap, black-blue-mauve-gold) | ~6.5 s each | box-blurred, darkened, grained, as palette indices | needs code: convert to ramp positions; photos of real people, so restore rather than reinvent |
| 14 | `sphere.raw` | blue chrome ball | ~6.5 s (star) | environment map of the tentacle star (disc) | as it is; keep the disc layout |
| 15 | `weird.raw` | teal-blue organic shapes | ~5 s (paranoid) | rippled full screen, lightened by the face clip | as it is; must tile seamlessly |

Not candidates: the shade and level maps (`phong1.raw`, `wire.raw`), the generated textures (`blobby.raw` tile 1,
`tex3d.raw` tiles 2-4), the fonts (`fontblur.raw`, `fontout.raw`: the remake already draws them as distance fields
from the coverage bytes) and the files the demo never shows (`frame.raw`, `frame2.raw`, `mustard.raw`,
`smalfont.raw`, `stxvic.raw`).

## 1. All files

24 pictures, 10 fade tables, 3 ghost tables, 3 meshes, 5 other tables. Loader addresses are the code that references
the file name (`re303.py str`). "Made" is what the picture looks like; the demo credits all graphics and design to
Aap and does not say who made each picture.

### Pictures (`.raw`)

| file | pixels | palette | made | used by | how the original reads it | remake now | redraw |
|---|---|---|---|---|---|---|---|
| `303b.raw` | 256x38 | none (values 0..255) | the digits 3 0 3 at different greys, one boxed | intro 27.7-40.9 (0x14f04, loaded at 0x14f84) | 11 glyphs by column (first columns 0x52eb8, last 0x52ee4), spread apart, blurred horizontally by a damped random spring, added through intro.fad | `c.tex("303b")` grey, sampled level 0 | as it is, grey |
| `alex.raw` | 320x200 | own (sepia ramp) | photo, motion-blurred portrait | alex part 65.05-71.65 (0x15734(0), name table 0x52f80) | pixel >> 1 = index into 128 colours (the file's even entries); box blur, darkening and grain on the index, credit words added to it | indices + 128-colour ramp texture (`mid_spiral.cpp` `CreditPart`) | needs code (section 2) |
| `bd2.raw` | 3 x 320x200 | own (one for all three) | 0, 1: typographic compositions (words over halftone dots / colour bands); 2: dark navy blotches | typo 124.8-129.6 (0, 1 toggled every 16 rows), wire blobs 146.6-149.9 (2) (0x2e8e4) | `B = wirecut[32 + min(2*(render & 0x7f), 31)][bd2]`: the 3D render whitens the picture through wirecut.fad | indices + a 256x64 colour table from wirecut.fad (`mid_blobs.cpp` `kWireFS`) | needs code (section 2) |
| `blobby.raw` | 2 x 256x256 | own | 0: pastel blobs (looks rendered); 1: black and white blob mask | blobby 54.7-65.3 (0x182e4) | sphere-ray grid background (tile by mode), env map u, v = 120 n.xy + 128 on tile 0; lyrics lightened through blobby.fad | `c.tex("blobby#0/#1")` colours | 0 as it is (tiles); 1 no (generated) |
| `credits.raw` | 512x32 | none (17 values) | anti-aliased words: code, music, vocals, statix, vic, emma | alex and vic parts (0x15734, 0x15775) | value >> 3 added to / subtracted from the photo index at the exe's word rectangles | `c.tex("credits")` grey (photo shader) | as it is, grey |
| `final.raw` | 320x350 | none (shown through the grey ramp c/4) | rows 1-31, 32-63: hand-lettered credit lines; 70-109: 8 hand-drawn icons; 124-149: hand-lettered contact line; 150-349: MC-303 photo with vignette | icons 217.85-253 (0x2a840) | icons at value >> 3 into a 7x5 grid; one line at a time box-blurred (lighten), feedback 0.65 per frame | `c.tex("final")` grey, sampled level 0 (`late.cpp` `kIconsFS`) | as it is, grey |
| `fontblur.raw` | 560x96 | none | capitals with vertical blur streaks | lyric overlay 0x2a34c (blobby, harl, dance, mid parts) | glyph runs found in the coverage bytes (0x29599), copied into a word buffer | coverage bytes (`exefx.cpp` `LyricLayer`) | no (font) |
| `fontout.raw` | 1024x64 | none | lowercase outline letters | web words 81.95-92.4 (0x2d038, loaded at start by 0x29860) | glyph runs found in the bytes >> 3 | coverage bytes (`late.cpp` `buildWords`); SDF font "outline" | no (font) |
| `frame.raw` | 320x200 | own (2 colours) | four sketchy hand-drawn frames | none: loaded only by 0x2dc7e, which nothing calls | - | - | no (unused) |
| `frame2.raw` | 320x200 | own | four screenshots of the demo's parts | none (0x2dc7e) | - | - | no (unused) |
| `mustard.raw` | 256x48 | none | the "[mustard]" logo | none (no reference in the exe) | - | - | no (unused) |
| `neuron.raw` | 256x256 | own | tangled glowing threads on black (looks generated or rendered) | intro 0-40.95 (0x14f04) | 41x26 grid of view rays through a rotating camera, texel = 128 + 128 ray.xy / abs(ray) (+ scroll), point-sampled, ghosted 1/4 new + 3/4 previous through intro.gst | `c.tex("neuron")` colours | as it is; tileable |
| `opening1.raw` | 256x256 | none (values 13..246) | "statix / vic / acme" in a sans-serif over a grey flower | intro to 27.7 (credits 0x14a34) | (value / 8) * light / 64, / 4, added through intro.fad; ripple + swirl grid; rows 200+ unused | `c.tex("opening1")` grey | as it is, grey |
| `phong.raw` | 256x256 | own, entries 128..255 (the photo keeps 0..127) | soft starburst | alex and vic parts (0x15734) | spikey env map u, v = 120 n.xy + 128 | `c.tex("phong")` colours | as it is (soft; little gain) |
| `phong1.raw` | 256x256 | none (values 0..63) | radial highlight (generated) | face plate, face sheet, waving sheet (0x23540, 0x2902c) | value = 3d.fad row: `dst = F[phong1[ev][eu]][texel]` | `c.tex("phong1")` grey and raw bytes (`mid_blobs.cpp`) | no (shade map) |
| `smalfont.raw` | 768x12 | none (inverted at load) | small debug font | loaded at start (0x1342c) but only drawn by 0x195e6 ("memory used"), which nothing calls | - | - | no (unused) |
| `sphere.raw` | 256x256 | own | blue chrome ball (looks rendered) | star 162.95-169.95 (0x2b908); its palette also for fish.fad in the spiral | env map u, v = 128 + 120 n.xy, no lighting | `c.tex("sphere")` colours | as it is; disc layout |
| `stxvic.raw` | 256x256 | none | black "statix vic acme" on white | none (no reference in the exe) | - | - | no (unused) |
| `tex3d.raw` | 6 x 256x256 | own (one for all six) | 0 gold chrome ball; 1 purple bevelled hexagons; 2 blue plasma; 3 the same in pink; 4 gold falloff; 5 colour stripe ramps | 0x2e8e4 (0x26f04): 0 metaball and gold ball env map, 1 corridor floor/ceiling and face-floor tiles, 2 sheet and plasma ball 1, 3 plasma ball 2, 4 polar ray sprite (row = angle, column = radius), 5 the flowers (0x29ae8) | texel copies, darkened through 3d.fad rows, ghosted through 3d.gst | `c.tex("tex3d#k")` colours | 0, 1 as it is (1 tileable, 0 disc layout); 2-5 no (generated) |
| `tunnel.raw` | 2 x 256x256 | own | 0 brown wavy streaks (looks rendered); 1 glowing flower on violet | rings 92.4-102.7 (0x16198) | 0: wall, row = 30 z + 128, column = 100 x + 128 (+ scroll), darkened through tunnel.fad; 1: env map of the spikey sprites | `c.tex("tunnel#0/#1")` colours | as it is |
| `vic.raw` | 320x200 | own (black-blue-mauve-gold ramp) | photo of Vic in a cap, duotone | vic part 156.6-163.4 (0x15734(1)) | as `alex.raw` | as `alex.raw` | needs code (section 2) |
| `weird.raw` | 256x256 | own | teal-blue organic shapes, dithered | paranoid 191.3-196.9 (0x16970) | rippled grid mapper, texel copy, wraps; lightened by the face through pinfx3.fad; inverted in the inset | `c.tex("weird")` colours | as it is; tileable |
| `wipeout.raw` | 256x256 | own | vertical colour stripes, each a dark-to-bright ramp (dithered) | Aap's stripes 40.95-54.7 (0x2d458) | grid mapper (egg rings, S bands), texel copy; lines lighten / darken the frame through wipeout.fad | `c.tex("wipeout")` colours | possible, little gain (gradient) |
| `wire.raw` | 256x256 | own, but overwritten by bd2's before use | hexagon outlines, levels 0..31 | wire mode of the typo / wire blobs corridor | level through wire.fad, then only a whitening mask for bd2 (never shown in colour) | raw bytes (`mid_blobs.cpp`) | no (mask) |

### Tables and meshes

| file | what | used by |
|---|---|---|
| `3d.fad`, `blobby.fad`, `fish.fad`, `intro.fad`, `pinfx3.fad`, `truecol.fad`, `tunnel.fad`, `wipeout.fad`, `wire.fad`, `wirecut.fad` | 64 x 256 fade tables: row 32 about identity, rows below towards black, rows above towards white, each entry the nearest colour of one palette (truecol.fad: of the YIQ video palette; wire.fad row 32 is i -> 4 (i >> 2) + 4) | the remake uses them as blends in true colour (`fadDarkTint`, shaders), except wirecut.fad (bd2) which it reads as a colour table for the original's pixels and as a blend towards white for a redraw |
| `3d.gst`, `intro.gst` | 256 x 256 ghost tables G[new][previous] (blends) | 0x2e8e4, intro (1/4 new + 3/4 previous) |
| `pinfx3.gst` | ghost table | none (no reference in the exe) |
| `faceo.dat`, `icosa.dat`, `spikey.dat` | meshes: relief face plate, tentacle-star object, spiky ball (unit direction + radius per vertex) | 0x23540, 0x2b908, 0x13e78 / 0x16198 |
| `bittab0.tab`..`bittab3.tab`, `window.tab` | probably the MPEG audio layer II tables (four bit-allocation tables, the 512-coefficient synthesis window, 8 bytes each) of the decoder for KETCHUP.PAK (loaded at 0x116f2 and 0x12336) | the remake decodes KETCHUP.PAK with minimp3 instead |

`alex.raw`, `vic.raw` and a third name `jo.raw` are in the file name table at 0x52f80; `jo.raw` is not in the PAK and
the third entry is never used.

## 2. Pictures read through palette indices

Where a scene reads the file's bytes (`c.pak.get`) instead of `c.tex`, an `art/` redraw is not picked up; the code
says so at each place. Two of them are worth a redraw with a conversion (bd2's is written; the photos are kept as
the original, see `remake/art/README.md`):

- **`alex.raw`, `vic.raw`** (`mid_spiral.cpp` `CreditPart`, routine 0x15734). The photo's index p >> 1 (0..127) is
  box-blurred over 1..61 pixels, multiplied by 0.8 (1 - 0.0168 s), gets a signed grain added, and the credit words
  are added to and subtracted from it; the result picks one of the 128 even palette entries. Both palettes are
  ramps whose luminance rises with the index (alex: black - sepia - white; vic: black - dark blue - mauve - gold -
  white; vic has 9 dips of under 0.6 / 63), so darkening moves along the ramp (vic's gold turns mauve, then blue),
  which a colour multiply would not do. A redraw has to be turned into ramp positions: for each pixel the nearest
  point on the piecewise-linear ramp of the 128 colours, as a float 0..127; the shader already filters the index
  in float (`photoAt`) and looks the colour up in a 128x1 ramp texture, so such a map replaces the index texture
  directly (`c.redraw("vic")` gives the redraw; the conversion is not written yet). Colours off the ramp are lost,
  as they are in the original. Both are photos of real people: restore and sharpen them, do not reinvent faces.
- **`bd2.raw`** (`mid_blobs.cpp` `kWireFS`, routine 0x1c490). Every pixel is `wirecut[L][index]` with
  L = 32 + min(2 (render & 0x7f), 31): a blend of the picture's colour towards white by (L - 32) / 31 (wirecut.fad
  row 32 is about the identity, row 63 white). The remake reads the index and a 256x64 colour table built from
  wirecut.fad; with a redraw (`art/bd2`) the shader samples the redraw's colour and mixes it towards white by
  (L - 32) / 31 instead. Checked against the table: weighted by how often bd2's pixels use each index, 95.8 % of
  row 32 is the identity, and in every row L the table's colour lies 0.1..4.7 (6-bit units) from bd2's colour
  blended towards white by (L - 32) / 31, as close as the palette's nearest colour to that blend gets
  (0.0..4.6); a least-squares fit of the blend fraction per row gives (L - 32) / 31 within 0.02. With bd2's own
  pixels as the redraw, the blend gives the same frames as the table up to that quantisation (mean difference
  1.5..5.9 of 255, `--shots` in the typo and wire blobs parts).

The others read through indices or levels are not pictures to redraw: `wire.raw` (levels into wire.fad, a mask),
`phong1.raw` (rows of 3d.fad), `fontblur.raw`, `fontout.raw` (coverage bytes, glyph runs found in them).

What redraws read as colours must keep (the remake samples them in 0..1 over the original's pixel grid):

- tileable textures (`neuron`, `weird`, `blobby` tile 0, `tunnel` tile 0, `tex3d` tile 1): the edges must join,
  since the mappers wrap at 256 texels;
- environment maps (`tex3d` tile 0, `sphere`, `phong`, `tunnel` tile 1, `blobby` tile 0): only the disc of radius 120
  texels around (128, 128) is read (u, v = 128 + 120 n.xy; 126 for the metaballs on `tex3d` tile 0); keep it
  exactly in place;
- grey pictures (`final`, `opening1`, `303b`, `credits`): the remake takes the redraw's luminance as the value; the
  exe addresses icons, lines, glyphs and words by column and row, so every element must stay at its pixels;
- stacked files: one PNG for the whole file, tiles stacked as in the file, each tile drawn on its own (the remake
  cuts tile k out as rows k/n .. (k+1)/n; nothing may bleed across a tile boundary);
- `final`, `303b` and `credits` are sampled with an explicit level (`textureLod`) inside per-pixel loops: level 0
  for the original, and for a redraw the level its texels per screen pixel give (`mipLevel()` in `fx.h`), so a
  redraw larger than the output does not alias. The metaballs read `tex3d` tile 0 at level 0 too (in a ray
  marcher, no derivatives); the redraw is minified there only near the balls' silhouettes, which get 4 rays.

## 3. The video clips

All four are `*.LZW` streams (Nelson's LZW15V) of 160x96 frames: a 1-byte quality, then per frame a 160x96 luma plane
and two 80x48 chroma planes (I, Q) of 4x4 DCT blocks (23040 bytes); PARANOID has the luma plane only (15360 bytes).
The original decodes into a 256-colour YIQ palette (16 Y x 4 I x 4 Q, 0x13244) with a 4x4 ordered dither, each luma
sample as 2x2 screen pixels (0x1b819). The dumps are the remake's decode (`src/assets.cpp`): the DCT in floating
point, chroma upsampled bilinearly (centre aligned), YIQ to RGB with the original's matrix in 6-bit units, rounded
to 8 bits, no dither; PARANOID as grey (R = G = B = luma).

| clip | frames in file | frames shown | rate | shown as |
|---|---|---|---|---|
| `HARL.LZW` | 200 | 0 .. about 118 (harl 71.65-82.09) | one per 6 ticks = 11.68 fps (tick callback 0x30990, `[0x580c0]` = 5) | Harlequin breakdancing in a party hall. 2x, 320x192 at rows 2..193 (2 black rows above, 6 below), not rippled or cropped; the lyric overlay (0x2a34c, fontblur words) lightens it through truecol.fad; random white flashes |
| `VS.LZW` | 147 | 0 .. about 55 (dance 208.9-215.33) | one per 8 ticks = 8.76 fps (`[0x580c0]` = 7) | people dancing and clowning. As HARL, with the sung-lyric overlay |
| `ALEX.LZW` | 43 | 0 .. about 21 (dance 215.33-217.85) | 8.76 fps (the tick counter runs on from VS) | people at computers. As VS |
| `PARANOID.LZW` | 45 (luma only) | loop of 44 (frame 44 never shown); about 0..25 in the paranoid part, 0..13 again in 0x2e8e4 A/B | one per 2 rows (pos multiple of 4 half-rows, callback 0x16b00) = 0.20975 s = 4.77 fps, locked to the music | a man's face looking around, grey. Stretched to 320x200 through the ripple grid warp(160, 100, A 0.07, Fr 0.02, phase t2 / 172) (at the ripple maximum it reads a few texels outside the 160x96 frame in its 256-wide buffer; the remake takes 0 there); its luma >> 3 lightens weird.raw through pinfx3.fad (191.7-196.85); in 0x2e8e4 A/B (197-199.7) it lightens the 3D frame through 3d.fad with a strength fading from 1 to 0 over 2.86 s |

The frame counts are (decompressed size - 1) / frame size. "Frames shown" come from the part lengths in
notes-late.md (sections 2, 3a, 3b, 4.1); the remake picks the same frames (`late.cpp`: `tk / 6`, `tk / 8`, the
4-half-row counter). Measured in the remake (the index `Ctx::video()` is asked for, rendering the parts every
0.02 s): HARL 0..116, VS 0..56, ALEX 0..22, PARANOID 0..24 in the paranoid part. In 0x2e8e4 the PARANOID counter
runs on through C (0..43, looping), but in C the strength is 0 and nothing of it is visible.

Display shape: a 160x96 frame is shown over 320x192 screen pixels, which are 1.2 times as tall as wide on the 4:3
screen, so the picture is seen at about 25:18 (1.39:1), slightly wider than the 4:3 video it was digitised from.
Restored frames keep the stored 5:3 shape (e.g. 640x384); the remake stretches them like the decoded frames. The
remake's `art/video/` holds SeedVR2 restorations at 1280x768 (`remake/art/README.md`).

Dumps: `re/dump/video/<CLIP>/NNNN.png` (all frames, 160x96 RGB) and `re/dump/video/<CLIP>.mp4`: VP9 lossless in
MP4, yuv444p, at 70.086/6 (HARL), 70.086/8 (VS, ALEX) and 4000/839 (PARANOID) frames per second. The only loss is
the RGB to YUV conversion: maximum difference 2/255 against the PNGs (PSNR 52-60 dB). The 4x4 DCT blocks and the
quantisation are part of the original data; an upscaler should treat them as compression artefacts.

```
g++ -std=c++20 -O2 -I src -I third_party tools/dumpvideo.cpp src/assets.cpp -o dumpvideo   # in 303/remake
./dumpvideo ../original ../re/dump/video
ffmpeg -framerate 70086/6000 -i HARL/%04d.png -c:v libvpx-vp9 -lossless 1 -pix_fmt yuv444p HARL.mp4
```
