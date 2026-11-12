# Redrawn pictures

High-resolution redraws of the hand-drawn pictures of *DOPE* by Complex (The Gathering 1995). DOPE.TXT credits the
graphics to jugi, the "routine boxes" (the 70x44 labels) to jugi (sketch) and Reward (pixels), and the "media labs"
logo to Reward. The remake shows these redraws by default; `--original-art` shows the original pixels, which it
reads from `DOPE.EXE`.

These are fan redraws of Complex's art, made for this non-commercial tribute and under the same non-commercial terms
as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Complex's work and not endorsed by
them.

## How they were made

Every redraw here is an AI redraw made with ChatGPT image generation from the original pixels, November 2026.

- The original (`dope/re/dump/pictures/NAME.png`, the file's indices with the palette the demo shows them with) was
  scaled up with nearest-neighbour so the image generator sees the real pixels: 5 x 6 per pixel for the 320x200
  screens (1600x1200, the 4:3 display shape), 15 x 18 for the 70x44 labels (1050x792, also screen pixels at 4:3),
  10 x 10 for the env maps (square texels, 1280x1280, each 128x128 map on its own). Pictures whose shape the
  generator does not make were put on a black canvas: the credits (320x128) on 3:2, "dope" (320x64) and the logo
  (1024x195) on 3:1. The credits are a coverage mask and the logo a shade map, so they were uploaded as grey
  (grey = 255 * level / 63). `tools/align_art.py --upload NAME OUT [--part K]` makes these uploads.
- The prompt, the same for all with the description changed: "a ... pixel picture from a 1995 MS-DOS demo, DOPE by
  Complex, graphics by jugi (labels: sketched by jugi and pixelled by Reward), scaled up with nearest-neighbour and
  shown at 4:3 (every pixel 1.2 times as tall as wide)" (env maps: "square texels"); a short description of what it
  shows; "make a faithful high-resolution redraw: the same composition and framing, every ... at the same position
  and size in the frame, the same colours and brightness; only the resolution and the detail change; do not add,
  remove or move anything, do not crop or zoom"; for text "exactly the same text, letter for letter, the same
  fonts"; for the credits, "dope" and the logo "greyscale only"; "a landscape 4:3 image (square, 3:2, 3:1), as large
  as you can make it". Four conversations: the eleven labels (one per message, "matching your first one in
  style"); the backgrounds (nebula, chequerboard, swirl, stand, horizon); the three env maps; the credits, "dope"
  and the logo. A second attempt said what to correct (column "attempt").
- ChatGPT returned 1448x1086 for 4:3, 1254x1254 for square, 1536x1024 for 3:2 and 2172x724 for 3:1. The raw
  downloads are kept in `dope/re/art-raw/` (not in Git; `dope-NAME-N.png`, N = attempt; the uploads in `upload/`).
- `tools/align_art.py NAME RAW.png [RAW2.png] [--match]` fits each redraw onto the original (scale in x and y and
  offset, see the script) and writes it covering exactly the original's frame, at the redraw's own pixel size,
  padded with the original's background colour where the redraw does not reach. `--match` then gives the redraw the
  original's colour distribution: per-channel monotone tone curves from quantile matching against the original in
  the original's grid (both blurred by one pixel). It was used for the colour backgrounds, the env maps and the logo,
  which came out up to 10 % darker or lighter, or shifted in hue (the nebula lost its navy background, the swirl its
  lavender); it changes no shape. `tools/align_art.py --sheet OUT.jpg NAME ...` makes the comparison sheet
  (`dope/re/dump/art-compare.jpg`: original left, redraw right; the logo coloured through palette B).

## Format

| files | size | covers | what the remake gets |
|---|---|---|---|
| p2-background, p3-nebula, p4-chequerboard, p5-swirl, p7-horizon | about 1450x1088 RGB | the whole 320x200 screen at 4:3 | the picture |
| the eleven `*-label-*` | about 1450x1095 RGB, R = G = B | the 70x44 box at 4:3 (screen pixels 1.2 times as tall as wide) | the opaque box |
| p7-dope | 2186x525 RGB, R = G = B | the 320x64 strip (screen rows 50..113) at 4:3 | the picture, black around the letters |
| p2-credits | 1520x730 8-bit grey | the 320x128 strips at 4:3 | coverage: grey g is text intensity x = 63 g / 255 (the original's 0..62) |
| p1-logo | 2177x498 8-bit grey | the logo's 1024x195 pixels (picture rows 1..195) at 4:3 | shade s = 63 g / 255 (the original's indices 0..63, 0 = black) |
| p5-envmaps | 2504x1252 RGB | both 128x128 maps side by side, as in the file (gold left, red right) | texel (u, v) of a map = pixels 1252 u / 128 ... |
| p6-envmap | 1255x1255 RGB | the drawn left half only (texels 0..127 of the 256x128 file) | the rings' blue cloud map |

All are PNG, none over 2.5 MB (16 MB together). The redraws have about 4.5 output pixels per original pixel across
for the screens, 21 for the labels, 6.8 for "dope", 4.75 for the credits, 2.13 for the logo and 9.8 for the env
maps.

## The pictures

Residual: RMS of the RGB difference (0..255) between the original and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is background and the other is not
(for the labels the "background" is the border colour of the box, so this is not meaningful), and the correlation of
the brightness detail (Gaussian 2 minus Gaussian 12 pixels; 1 = every shape in place). Scale is relative to "the
redraw fills the upload's frame" (x by y), offset in original pixels relative to that. With `--match` the residual
is the one after the colour match (before it in brackets).

| picture | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / bg / detail) | file size |
|---|---|---|---|---|---|---|---|---|---|
| p2-label-blur | "phong BLUR motion" | sketch jugi, pixels Reward | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | grey box with bevel, lettering letter for letter, pure greyscale, smooth tones instead of the dither | 1 | 0.9989 x 0.9475 | -0.4, -0.2 | 28.4 / 17.4 / 12.5 % / 0.95 | 850 kB |
| p3-label-phong | "7800 PHONG polys" | sketch jugi, pixels Reward | as above | as blur, "matching your first one"; a 2nd attempt with the layout in per cent moved PHONG right and was discarded | 1 | 1.0163 x 0.9754 | -1.5, +0.0 | 51.2 / 25.9 / 17.1 % / 0.88 | 730 kB |
| p4-label-fire-clouds | "larusse FIRE clouds" | sketch jugi, pixels Reward | as above | as phong | 1 | 0.9888 x 0.9848 | -0.1, +0.0 | 18.2 / 8.2 / 7.9 % / 0.99 | 654 kB |
| p4-label-fractal-scape | "larusse FRACTAL scape" | sketch jugi, pixels Reward | as above | as phong | 1 | 0.9908 x 0.9807 | -0.3, +0.1 | 34.4 / 16.0 / 11.7 % / 0.96 | 637 kB |
| p5-label-env | "phong ENV mapping" | sketch jugi, pixels Reward | as above | as phong | 1 | 0.9851 x 0.9889 | -0.2, +0.1 | 30.4 / 16.0 / 10.0 % / 0.97 | 590 kB |
| p5-label-bump | "BUMP glow" | sketch jugi, pixels Reward | as above | as phong | 1 | 0.9911 x 0.9551 | +0.5, +0.1 | 46.3 / 24.1 / 10.7 % / 0.95 | 562 kB |
| p6-label-shadow | "SHADOW volume" | sketch jugi, pixels Reward | as above | as phong | 1 | 1.0176 x 0.9357 | -1.1, +0.1 | 46.5 / 24.5 / 13.4 % / 0.93 | 566 kB |
| p6-label-morph | "material MORPH" (empty bottom fifth) | sketch jugi, pixels Reward | as above | as phong | 1 | 0.9918 x 1.0445 | +0.2, +0.7 | 46.0 / 24.2 / 14.7 % / 0.94 | 603 kB |
| p6-label-metal | "METAL shading" | sketch jugi, pixels Reward | as above | as phong, "do not make the letters bigger or smaller" | 1 | 0.9919 x 0.9348 | +0.1, +0.2 | 46.0 / 23.0 / 15.9 % / 0.92 | 520 kB |
| p7-label-light | "multiple LIGHT sources" | sketch jugi, pixels Reward | as above | as metal | 1 | 1.0080 x 0.9654 | -0.5, +0.7 | 54.2 / 26.9 / 13.7 % / 0.91 | 570 kB |
| p7-label-world | "1995 WORLD domination" | sketch jugi, pixels Reward | as above | as metal; a 2nd attempt with the layout in per cent shortened "domination" (my measurement missed its darker right letters) and was discarded | 1 | 1.0127 x 0.9030 | -0.5, +1.2 | 52.6 / 26.1 / 21.0 % / 0.88 | 584 kB |
| p2-credits | "jmagic code", "jugi music", "jugi graphics", "\"dope\"" (four strips, white on black) | jugi | as above | white on pure black, letter for letter with the quotation marks, same geometric font, no shading | 1 | 1.0106 x 1.0140 | -0.1, -0.3 | 10.0 / 5.0 / 0.6 % / 1.00 | 132 kB |
| p7-dope | the ending: "dope" in white | jugi | as above | as the credits | 1 | 0.9935 x 0.9722 | +0.2, +2.0 | 7.2 / 4.4 / 0.3 % / 1.00 | 104 kB |
| p1-logo | "Complex Media Labs", chrome extruded lettering (shade map) | Reward | as above | greyscale shade map, every letter, bevel and reflection in place, pure black background; `--match` | 1 | 0.9975 x 1.0110 | +0.4, -1.2 | 19.1 / 16.2 / 1.8 % / 0.96 (23.1 / 20.3 / 1.7 % / 0.96) | 296 kB |
| p2-background | the stand: three blue metal tubes, rails, a ring (pre-rendered) | jugi | as above | same tubes, ring and rails, dark blue-violet, no extra lights; `--match` | 1 | 1.0236 x 1.0147 | -2.2, -0.6 | 7.4 / 5.2 / 4.0 % / 0.95 (8.6 / 6.5 / 4.5 % / 0.94) | 415 kB |
| p3-nebula | violet nebula with stars (p3 and p4) | jugi | as above | every star, cloud and lane in place, no new stars, no brighter; `--match` | 1 | 1.0001 x 1.0004 | -0.1, -0.0 | 6.0 / 4.2 / 5.6 % / 0.89 (8.8 / 7.7 / 7.9 % / 0.90) | 1758 kB |
| p4-chequerboard | red / orange marble chequerboard in perspective (p4 and p5) | jugi | as above | every square and streak in place, straight square edges; `--match` | 1 | 0.9922 x 0.9953 | +0.6, +0.4 | 8.0 / 5.7 / 5.8 % / 0.98 (11.7 / 9.8 / 5.7 % / 0.98) | 998 kB |
| p5-swirl | blue cloud texture with a dark C-shaped swirl (p5 and p6) | jugi | as above | 2nd: one thick C-shaped band with a single light tongue, fine vertical streaks, lavender lights (the 1st was a hurricane with many arms); `--match` | 2 | 0.9985 x 1.0001 | +0.4, +0.2 | 7.4 / 5.5 / 12.1 % / 0.94 (9.7 / 8.3 / 17.3 % / 0.93) | 1500 kB |
| p7-horizon | night desert horizon with cacti under a black sky | jugi | as above | horizon, hills and cacti given in per cent of the frame, black sky without stars; `--match` | 1 | 1.0006 x 0.9916 | -0.1, +1.6 | 2.5 / 1.4 / 2.0 % / 1.00 (3.4 / 2.6 / 1.6 % / 0.99) | 500 kB |
| p5-envmaps, left | gold env map: a liquid-gold reflection of a chequerboard | jugi | as above | same layout, every square and highlight in place, no border, not round; `--match` | 1 | 0.9973 x 0.9982 | +0.2, +0.0 | 11.7 / 7.3 / 4.1 % / 0.99 (15.3 / 11.8 / 7.2 % / 0.99) | 2427 kB (both) |
| p5-envmaps, right | red env map: glossy red scene, highlight, plant, light spots | jugi | as above | as gold; `--match` | 1 | 1.0056 x 1.0022 | -0.2, -0.1 | 8.9 / 6.7 / 0.0 % / 0.98 (12.4 / 10.5 / 0.0 % / 0.97) | (above) |
| p6-envmap | blue clouds (the rings' env map, left half) | jugi | as above | as gold; `--match` | 1 | 0.9994 x 1.0026 | +0.0, -0.3 | 13.2 / 8.0 / 2.8 % / 0.99 (14.0 / 9.2 / 2.8 % / 0.99) | 1657 kB |

The label residuals are higher because each label is small and nearly all of it is sharp letter edges, and because
the generator drew the letters a little bolder (PHONG, FRACTAL, METAL, LIGHT) or larger (SHADOW, MORPH, WORLD,
BUMP glow) than the pixelled ones; the fit then scales the redraw in y to a compromise (0.90–1.04 of
frame-filling).

## Kept as the original

| picture | why |
|---|---|
| p7-reflection-photo | a digitised photograph (the p7 light map's centre), not drawn; not to be redrawn |
| p4-cloud, p4-landscape-colour, p4-landscape-height | the voxel landscape's cloud, colour and height maps: data the effect reads by value |
| p6-envmap-metal | the same pixels as p6-envmap with the metal palette (pal2); colours 16..79 (the clouds) are the same in both palettes, so the p6-envmap redraw serves both; the right half is generated |
| p6-envmap, right half; the p2 env maps, the bump and fur maps, the Phong map | not drawn pictures: generated by the demo (radial ramps, height fields); the remake computes them or filters the stored maps |

## Notes for the port

- One redraw serves both parts where the demo shows the same picture twice: p3-nebula is also p4's nebula,
  p4-chequerboard also p5's, p5-swirl also p6's. The cross-fades between them become true-colour blends.
- The screen pictures, labels, "dope" and the credits are screen pixels at 4:3; the env maps have square texels.
- p2-credits: strip n (n = 0..3) is rows n/4 .. (n+1)/4 of the height (182.5 output rows each). As in the
  original, each strip's first row is black and only the descender of "dope" in strip 3 reaches the strip's last
  row. The redraw's text core is 251..255 grey where the original's is 62/63 (251).
- p1-logo: it covers the logo pixels p1 shows (picture rows 1..195; the unused picture row 0 and rows 196..199 are
  not included), all 1024 columns; columns outside 22..1004 are black (under 8 of 255). Colour a pixel with shade
  s = 63 g / 255 by the cross-faded ramps A(s) (1 - w) + B(s) w, interpolating between the ramps' 64 entries
  (notes-core.md, section 8).
- p5-envmaps: the gold map's column 127 holds red-ramp indices in the original (never read); the redraw is gold
  there. The env maps are read only inside discs of radius about 54 (gold, p6 clouds) and 66 (red) texels around
  each map's centre; the redraws keep the content there in place (offsets under 0.3 texel, scales within
  0.6 %).
- p7-dope is pasted over the black sky of p7-horizon at screen rows 50..113; it is opaque with a black background,
  as in the original.

## What to look at in the comparison sheet

- The labels: the box is smooth where the original has a coarse checker dither; the letters are a little bolder or
  larger (see above). The box shading (lighter top left, bevel) and every letter are there.
- p3-nebula: the stars and dust lanes are in place, but the original's faint bluish glow at the right edge is
  weaker and there are fewer of the faintest stars.
- p5-swirl: the second attempt; the cloud grain is the generator's, the C-shaped band and the tongue are in place.
- p5-envmaps: the gold squares have straight edges where the original's are slightly irregular; in the red map the
  plant is a cleaner flowering branch and the mottled dark lower part is smooth.
- p6-envmap: more contrast in the cloud wisps than the soft original.
- p1-logo: only 2.13 times the original resolution (one 3:1 image holds the whole 1024x195 logo at that size); the
  reflections inside the letters follow the original's light and dark areas, the fine streaks are new.
- p2-background: the redraw was drawn 2 % smaller; the fit scales it up, which slightly softens it.
