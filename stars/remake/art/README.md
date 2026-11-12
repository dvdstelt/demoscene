# Redrawn pictures

High-resolution redraws of the hand-drawn pictures of *STARS: Wonders of the World* (NoooN, 1995). The notes credit
the pixels to RA of NoooN ("script and pixels RA"; the credits say "ART RA"), so every picture below is RA's. The
remake shows these redraws by default; `--original-art` shows the original pixels, which it reads from `STARS.EXE`.

These are fan redraws of NoooN's art, made for this non-commercial tribute and under the same non-commercial terms
as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not NoooN's work and not endorsed by
them.

## How they were made

Every redraw here is an AI redraw made with ChatGPT image generation from the original pixels, November 2026.

- The original (`stars/re/dump/pictures/fileNN.png`, the file's pixels with the palette the demo shows them with)
  was scaled up with nearest-neighbour so the image generator sees the real pixels: 5 x 6 per pixel for screen
  pictures (1600x1200, the 4:3 display shape), 4 x 4 for the texture 7, and the sprites 5, 6 and 32 in a 2x2 grid
  with gaps, their transparent pixels shown as flat magenta (green for 32). `tools/align_art.py --upload NN OUT`
  makes these uploads.
- The prompt, the same for all with the description changed: "a 320x200 pixel artwork from a 1995 MS-DOS demo,
  STARS: Wonders of the World by NoooN, pixel art by RA, scaled up with nearest-neighbour, shown at 4:3 (every
  pixel 1.2 times as tall as wide)"; a short description of what it shows; "make a faithful high-resolution redraw:
  the same composition, pose, proportions and framing, every part at the same position and size in the frame, the
  same colours, the same black background; only the resolution and the detail change; do not add, remove or move
  anything, do not crop or zoom"; for text "exactly the same text, letter for letter, the same fonts"; for sprites
  "the flat magenta stands for transparency, keep it perfectly flat"; "a landscape 4:3 image (3:1, 3:2, square),
  as large as you can make it". Related pictures were made in one conversation (the three women; the credits; the
  intro cards; the 3D-part backgrounds). Second and third attempts said what to correct (column "attempt").
- ChatGPT returned 1448x1086 for 4:3, 2172x724 for 3:1, 1536x1024 for 3:2 and 1254x1254 for square. The raw
  downloads are kept in `stars/re/art-raw/` (not in Git).
- `tools/align_art.py NN RAW.png` fits each redraw onto the original (scale in x and y and offset, see the script)
  and writes it covering exactly the original's frame, at the redraw's own pixel size, padded with the original's
  background colour where the redraw does not reach. `tools/align_art.py --sheet OUT.jpg NN ...` makes the
  comparison sheet (`stars/re/dump/art-compare.jpg`: original left, redraw right).

## Format

| files | size | what the remake gets |
|---|---|---|
| 8–12, 14, 52–57, 19, 25, 47 | about 1450x1088 RGB | the whole 320x200 screen at 4:3 |
| 7 | 1263x1263 RGB | the 256x256 texture (square texels), same layout: four eyes, one per 64-row band |
| 5 | 903x1084 RGBA | the four 144x36 words stacked like the file (Magic, Atomic, Nutritive, Powerful), each 903x271 |
| 6 | 638x1772 RGBA | the four 128x74 names stacked like the file (BARTi, RA, GROO, KARL), each 638x443 |
| 32 | 1236x667 RGBA | the 80x36 sprite "23806 faces" |

All are PNG (none is over 2.5 MB). Sprite alpha: 1 where the lettering is, 0 where the original has a transparent
index, anti-aliased at the edges; transparent pixels are black. A sprite pixel of the original covers
(output width / sprite width) output pixels across and 1.2 times that down.

## The pictures

Residual: RMS of the RGB difference (0..255) between the original and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is background and the other is not
(meaningless for the full-frame backgrounds 19, 25, 47, whose "background" colour is everywhere), and the
correlation of the brightness detail (Gaussian 2 minus Gaussian 12 pixels; 1 = every shape in place). Scale is
relative to "the redraw fills the frame" (x by y), offset in original pixels.

| file | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / bg / detail) | file size |
|---|---|---|---|---|---|---|---|---|---|
| 08 | "BRING THE FAMILY", men in hats walking out of a magenta spiral | RA (NoooN) | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | faithful redraw, lettering letter for letter, men stay a black-and-white photo look | 1 | 0.9991 x 0.9997 | +0.1, -0.0 | 11.6 / 8.0 / 2.1 % / 0.99 | 1065 kB |
| 09 | "join us", pink and green psychedelic lettering on a red-brown blob | RA (NoooN) | as above | faithful redraw, the same letter shapes | 1 | 1.0043 x 1.0040 | +2.6, +2.9 | 17.7 / 12.4 / 1.7 % / 0.95 | 663 kB |
| 10 | "STARS wonders of the World" title | RA (NoooN) | as above | text letter for letter; 2nd: the layout in per cent of the frame (the 1st was squashed vertically and too high) | 2 | 0.9998 x 1.0018 | -0.1, -0.2 | 7.6 / 4.8 / 0.9 % / 1.00 | 342 kB |
| 11 | the woman in the sequined dress on a magenta starburst | RA (NoooN) | as above | faithful redraw, same face, pose and clothes, glamorous caricature pin-up | 1 | 0.9989 x 1.0029 | -0.2, -0.3 | 13.8 / 10.2 / 1.8 % / 0.96 | 1013 kB |
| 12 | the woman, second pose (cartoon nude) on the starburst | RA (NoooN) | as above | as 11; 2nd: keep the small "RA" signature (the 1st left it out) | 2 | 0.9979 x 0.9959 | +0.4, +0.6 | 14.0 / 10.1 / 2.2 % / 0.97 | 902 kB |
| 14 | green poodle on a grey star, "BRING YOUR BEAUTY" in its fur | RA (NoooN) | as above | faithful redraw, faint words in the fur; 2nd: the middle word is YOUR (the 1st drew dots) | 2 | 0.9984 x 0.9983 | +0.5, +0.4 | 10.9 / 8.5 / 0.5 % / 0.97 | 1021 kB |
| 52 | "ART RA", "3d Objects : Barti Xteo Imagine Autodesk" | RA (NoooN) | as above | text letter for letter, same fonts and greys | 1 | 1.0013 x 0.9988 | -0.3, +0.2 | 6.5 / 3.1 / 0.5 % / 1.00 | 288 kB |
| 53 | "3D EFFECTS & MOTION BARTI" | RA (NoooN) | as above | as 52 | 1 | 1.0016 x 0.9987 | -0.4, +0.1 | 8.4 / 4.1 / 1.0 % / 1.00 | 294 kB |
| 54 | "SYSTEM & SPECIAL EFFECTS KARL", "L.C.A. Player" | RA (NoooN) | as above | as 52; 2nd: the layout in per cent (the 1st had "L.C.A. Player" too high and too big) | 2 | 1.0089 x 0.9972 | -1.0, +0.7 | 22.5 / 11.9 / 2.2 % / 0.98 | 338 kB |
| 55 | "MUSIC GROO" | RA (NoooN) | as above | as 52 | 1 | 1.0008 x 0.9995 | -0.1, -0.0 | 6.3 / 2.7 / 0.4 % / 1.00 | 242 kB |
| 56 | "NOOON 50 $" | RA (NoooN) | as above | as 52, with the layout in per cent | 1 | 0.9866 x 1.0044 | +3.7, +0.2 | 14.9 / 8.6 / 1.1 % / 0.98 | 274 kB |
| 57 | spiky violet ornament with a glowing knot of threads | RA (NoooN) | as above | faithful redraw; 2nd: the knot irregular and dimmer, not regular bright orbits | 2 | 0.9995 x 0.9981 | +0.1, +0.2 | 12.5 / 8.1 / 0.7 % / 0.97 | 1481 kB |
| 05 | the words Magic, Atomic, Nutritive, Powerful (sprites, 4 x 144x36) | RA (NoooN) | as above | 2x2 grid on magenta, letter for letter, same thin dark outline | 1 | 1.0003–1.0017 x 1.005–1.025 | per sprite, within 1 px of the grid | 35–43 / 20–26 / 2.4–3.5 % / 0.92–0.94 | 588 kB |
| 06 | the names BARTi, RA, GROO, KARL (sprites, 4 x 128x74) | RA (NoooN) | as above | 2x2 grid on magenta, letter for letter, same shapes and black edges (a 2nd attempt asked by mistake for edges on two sides only and was discarded) | 1 | 0.991–0.996 x 0.987–0.991 | per sprite, within 0.7 px of the grid | 19–28 / 9–13 / 2.1–6.3 % / 0.96–0.98 | 697 kB |
| 07 | flower petal texture: four identical eyes of nested ellipses in six flat colours | RA (NoooN) | as above | square, the four shapes in place, flat colours, crisp edges, no shading | 1 | 0.9925 x 0.9998 | +0.4, +0.1 | 6.7 / 4.4 / 0.4 % / 0.99 | 620 kB |
| 19 | dark purple spiral (spike ball background) | RA (NoooN) | as above | faithful redraw, smooth tones instead of dithering, no brighter or more colourful | 1 | 0.9984 x 1.0024 | +0.3, -0.2 | 5.9 / 5.1 / – / 0.96 | 1329 kB |
| 25 | blue-violet cloud wisps (surfaces background) | RA (NoooN) | as above | as 19 | 1 | 1.0007 x 1.0023 | +0.1, -0.2 | 5.2 / 3.4 / – / 0.95 | 1193 kB |
| 47 | dark grey-green landscape silhouette (torso background) | RA (NoooN) | as above | as 19 | 1 | 1.0050 x 1.0037 | +0.2, +1.3 | 5.4 / 3.6 / – / 0.92 | 835 kB |
| 32 | "23806 faces" (sprite, 80x36, blinking in the bee part) | RA (NoooN) | as above | on green, same text and serif font; 2nd: lighter weight, no outline (it copied the pixel blocks: discarded); 3rd: smooth again with those corrections | 3 | 1.0359 x 0.9812 | -8.6, -8.2 (the grid cell is at 8, 8) | 34 / 15.5 / 7.5 % / 0.97 | 409 kB |

The sprite residuals are higher because each sprite is small and its edges are sharp against the key colour: a
fraction of a pixel of difference in an edge counts fully.

## Kept as the original

| file | why |
|---|---|
| 13 | the woman, third pose (cartoon nude): ChatGPT refused ("the image we created may violate our content policies"). Not retried |
| 27 | the cloud-tunnel background of the bee part: the remake renders it from the cloud effect |
| 36–41 | the women in 18-bit RGB for the dissolving part: the port decides whether to reuse the redraws of 11 and 12 |
| 58 | the "NOOON BRING YOUR BEAUTY FAMILY" mask of the particle parts: not shown as a picture |
| 2, 3, 16, 21, 23, 29, 30, 35, 43, 44, 49 | maps, level textures and env maps the effects read by index or level, not pictures (notes-core.md, "Pictures") |

## Notes for the port

- 11, 12 (and 13) are also the warp part's textures: columns 32..287 of the screen, all 200 rows stretched to 256.
  The redraws keep the figures where the originals have them (offsets under 0.6 original pixels), so the same
  region of a redraw works as the texture. 13 stays original pixels in both places.
- 7: band j is v / 64 (texels 64 j .. 64 j + 63); each eye spans rows 64 j .. 64 j + 62 in the original. The
  redraw's smooth edges reach up to 0.4 texel above 64 j, i.e. into the band above (at 1263 pixels that is 2 rows);
  sample the band from the eye's centre row rather than from v there if it shows.
- 19, 25, 47: the 3D parts copy rows 1..197 only; rows 0, 198 and 199 stay black.
- 5, 6, 32 are opaque lettering over the flower / the bee: use their alpha as coverage; their colours are the
  lettering's colours (unmixed from the key), not premultiplied.

## What to look at in the comparison sheet

- 08: the front man's stride differs a little and the men are sharper and more photographic than the soft pixels.
- 09 and 56: drawn about 3 original pixels to the left (09 also up); the fit corrects that.
- 10, 54, 56: ChatGPT moved text until given the layout in per cent; check that nothing is still off.
- 14: the poodle's fur is more regular; the faint words now read BRING / YOUR / BEAUTY as in file 58's mask.
- 05: the words' outline is an even thin line; in the original it is thicker on the bottom and right.
- 19, 25, 47: the wisps, streaks and rocks follow the original's light and dark areas (detail correlation
  0.92–0.96), but the fine detail is the generator's.
- 57: the knot of threads is a new tangle in the original's place and colours.
- 32: the letters have a soft darker shading inside their edges where the original has single darker pixels.
