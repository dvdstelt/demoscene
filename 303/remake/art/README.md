# Redrawn pictures and restored video

High-resolution versions of *303*'s pictures and video clips (Acme, X'97). The remake shows them by default;
`--original-art` shows the original pixels and the decoded video frames, which it reads from `MUSTARD.PAK` and the
`*.LZW` files. How the remake reads this folder is in [../README.md](../README.md) ("Redrawn pictures and restored
video"); which pictures and clips can be replaced, and what a replacement has to keep, is in
[../../re/notes-pictures.md](../../re/notes-pictures.md).

These are fan versions of Acme's work, made for this non-commercial tribute and under the same non-commercial terms
as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Acme's work and not endorsed by
them.

## Redrawn pictures

`<name>.png` / `<name>.jpg`: high-resolution redraws of `MUSTARD.PAK`'s pictures. The demo credits "graphics and
design" to Aap and does not say who made each picture, so every picture below is credited to Aap.

### How they were made

Every redraw here is an AI redraw made with ChatGPT image generation from the original pixels, 12 November 2026.

- **Originals.** `re/dump/pictures/<name>.png`: the file's pixels with the palette the demo shows them with, grey
  for the files without a palette (see [notes-pictures.md](../../re/notes-pictures.md)).
- **Uploads.** `tools/align_art.py --upload NAME OUT [--part K]` scales the original up with nearest-neighbour so
  the generator sees the real pixels: textures that wrap repeated 2x2 at 3x3 per texel (1536x1536), environment
  maps at 5x5 per texel (1280x1280), screen pictures at 5x6 per pixel (320x200 becomes 1600x1200, the 4:3 display
  shape). Pictures whose shape the generator does not make go on a canvas of their background colour: `final`'s
  lettering and icons (rows 0–149) on 3:2 and its photo (rows 150–349) on its own at 4:3; `opening1`'s rows
  0–199 (the only ones the intro shows) on a square; `303b` and `credits` on 3:1. Grey pictures are uploaded grey.
- **Prompts.** The same for all with the description changed: "a 256x256 texture (environment-map texture,
  320x200 pixel artwork, ... greyscale picture) from a 1997 MS-DOS demo, 303 by Acme, graphics by Aap, scaled up
  with nearest-neighbour" plus "square texels" or "shown at 4:3 (every pixel 1.2 times as tall as wide)"; for the
  textures "shown here repeated 2x2 (four identical copies of one tile)"; a short description of what it shows;
  "make a faithful high-resolution redraw: the same composition and framing, every ... at the same position and
  size in the frame, the same colours and brightness; only the resolution and the detail change: smooth tones and
  fine detail instead of the dithering and the pixel steps; do not add, remove or move anything, do not crop or
  zoom"; for the textures "the same 2x2 repetition (the four copies stay identical and join exactly as they do
  here)"; for the env maps "the ball exactly the same size and position in the frame (its edge touching the same
  pixels)"; for text "exactly the same text, letter for letter, the same fonts" and "greyscale only" for the grey
  ones; "a square (landscape 4:3, 3:2, wide 3:1) image, as large as you can make it". Four conversations: the
  textures (hexagons, threads, tunnel wall, blobs, weird), the lettering (`final` in two parts, `opening1`,
  `303b`, `credits`), the environment maps (gold ball, blue ball, starburst, flower) and `bd2`.
- **Results.** ChatGPT returned 1254x1254 for square, 1448x1086 for 4:3, 1536x1024 for 3:2 and 2172x724 for 3:1,
  and refused nothing. The raw downloads are in `re/art-raw/` (not in Git; `303-NAME-N.png`, N = attempt; the
  uploads in `upload/`, the fitted pictures in `fit/`).
- **Fit.** `tools/align_art.py NAME RAW.png [RAW2.png] [--match] [--match-elements]` fits each redraw onto the
  original (scale in x and y and offset, as DOPE's and STARS's tools do) and writes the picture into this folder,
  covering the file's whole pixel grid. `--match` gives the redraw the original's colour distribution
  (per-channel monotone tone curves; no shape changes), used where the generator shifted the colours or the
  brightness. Special cases:
  - *Textures that wrap* (`tex3d` tile 1, `neuron`, `tunnel` tile 0, `blobby` tile 0, `weird`): the redraw of the
    2x2 repeat is fitted against the repeated original, and the tile is cut out of the middle. Its edges then lie
    where the two copies of the same texels, one period apart, overlap; along each edge the tool takes the path
    where the two copies differ least (a vertical, then a horizontal minimum cut through a band of 1/12 of the
    tile) and feathers across it over a few pixels. The result wraps exactly; tiled 3x3 no seam shows.
  - *Elements* the exe reads by row and column: `final`'s two credit lines (rows 1–31, 32–63), its eight icons
    (columns from 0x57fd4, widths from 0x57ff4, rows 70–109) and its contact line (rows 124–149); `303b`'s eleven
    glyphs (first and last columns at 0x52eb8 and 0x52ee4); `credits`' six words (source columns from 0x52f20).
    After the fit of the whole, each element is fitted again on its own and written exactly into its rectangle, so
    every element is at its pixels even where the generator moved it. `--match-elements` matches each element's
    greys on its own (`303b`'s glyphs are each a different grey).
  - *Stacked files* (`tex3d`, `tunnel`, `blobby`, `bd2`): one picture with all tiles stacked as in the file, at
    one size per tile. Tiles without a redraw (`tex3d` 2–5, `blobby` 1) are the original scaled up bilinearly,
    which is how the remake's texture filter shows the original, so they look the same.
- **Sheet.** `tools/align_art.py --sheet OUT.jpg NAME ...` makes the comparison sheet
  (`re/dump/art-compare.jpg`: original left, redraw right).

### Format

| file | size | covers |
|---|---|---|
| `tex3d.jpg` | 1257x7542 RGB | the six 256x256 tiles stacked, 1257x1257 each: 0 gold ball (redraw), 1 hexagon plates (redraw, fitted at 627 px and scaled up), 2–5 the original |
| `tunnel.jpg` | 1257x2514 RGB | 0 the rings' wall (redraw, fitted at 627 px and scaled up), 1 the flower env map (redraw) |
| `blobby.jpg` | 627x1254 RGB | 0 pastel blobs (redraw), 1 the black and white mask (the original) |
| `neuron.jpg`, `weird.jpg` | 627x627 RGB | the 256x256 tile |
| `sphere.jpg`, `phong.jpg` | 1255x1255, 1221x1221 RGB | the 256x256 env map |
| `bd2.jpg` | 1449x3261 RGB | the three 320x200 screens stacked, at 4:3 |
| `final.png` | 1493x1959 grey | the 320x350 picture at 4:3 |
| `opening1.png` | 1256x1507 grey | the 256x256 picture at 4:3 (rows 200–255, never shown, are the background) |
| `303b.png` | 2179x388 grey | the 256x38 strip at 4:3 |
| `credits.png` | 2177x163 grey | the 512x32 strip at 4:3 |

JPEG quality 95 with full-resolution colour (4:4:4) for the colour pictures, PNG for the grey ones; 12 files,
7.1 MB. Square texels for the textures and env maps, screen pixels (1.2 times as tall as wide) for the rest.

### The pictures

Residual: RMS of the RGB difference (0..255) between the original and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is background and the other is not
(meaningless for the full-frame textures), and the correlation of the brightness detail (Gaussian 2 minus Gaussian
12 pixels; 1 = every shape in place). Scale is relative to "the redraw fills the upload's frame" (x by y), offset in
original pixels. With `--match` the residual is the one after the colour match (before it in brackets). For the
textures that wrap the fit is against the 2x2 repeat.

| picture | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / bg / detail) | file size |
|---|---|---|---|---|---|---|---|---|---|
| `tex3d` tile 1 | purple bevelled hexagon plates: floor and ceiling of the hex corridor, the face-floor tiles | Aap | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | 2x2 repeat; every plate, rim, groove, highlight and glint in place; `--match` | 1 | 1.0001 x 0.9997 | +0.1, +0.2 | 11.9 / 6.1 / – / 0.98 (13.8 / 9.0 / – / 0.98) | `tex3d.jpg` 2437 kB (tiles 0–5) |
| `neuron` | tangled glowing threads in teal, mauve and white on black: the intro's sphere-mapped background | Aap | as above | 2x2 repeat; every thread, loop and sparkle in place, thin smooth glowing lines; `--match` | 1 | 1.0000 x 0.9997 | +0.1, -0.0 | 15.6 / 11.0 / – / 0.97 (18.2 / 13.5 / – / 0.97) | 219 kB |
| `final` | two hand-lettered credit lines ("VIC", a sun, "ACME"; "STATIX", a pistol, "PSYCHIC LINK"), eight hand-drawn icons, the contact line, and a photo of a Roland MC-303 with a dark vignette (grey): the icons part | Aap | as above | lettering and icons (3:2): letter for letter, the same scratchy hand-drawn strokes, white on black; photo (4:3): a faithful restoration, same greys and vignette; every line and icon fitted again on its own | 1, 1 | 1.0001 x 0.9993; photo 0.9990 x 0.9978 | -0.0, +0.0; photo -0.1, +0.1 | lettering 17.7 / 8.9 / 2.5 % / 0.99, photo 15.2 / 9.7 / 10.5 % / 0.98; the lines and icons moved by their own fit at most 0.23 px, scaled 0.997–1.015, each 14–30 / 7–15 / 2–7 % / 0.96–0.99 | 1109 kB |
| `opening1` | "statix", "vic" and "acme" in a bold sans-serif over a grey six-petal flower (grey): the intro credits | Aap | as above | rows 0–199 on a square; letter for letter, the same font, weight and greys; `--match` | 1 | 0.9986 x 0.9999 | +0.1, +0.0 | 4.6 / 2.9 / 0.5 % / 1.00 (6.5 / 5.3 / 0.5 % / 0.99) | 316 kB |
| `tex3d` tile 0 | gold chrome ball reflecting a dark olive landscape: env map of the metaballs and the plasma section's gold ball | Aap | as above | the ball's edge on the same pixels, every band, streak and highlight in place; `--match` | 1 | 0.9973 x 0.9972 | -0.1, -0.0 | 7.8 / 4.7 / 10.1 % / 0.96 (8.2 / 5.3 / 9.3 % / 0.96) | (`tex3d.jpg`) |
| `303b` | the digits 3 0 3 0 3 0 3 0 3 0 3 at different greys, the middle 303 in a frame (grey): the intro from 27.7 s | Aap | as above | on 3:1; the same digits, font, boxes and greys; each glyph fitted again on its own; `--match-elements` | 1 | 0.9966 x 1.0092 | +0.0, -0.3 | 13.3 / 10.9 / 3.0 % / 0.99; the glyphs moved by their own fit at most 0.27 px, scaled 0.96–1.01, each 2.5–10.9 / 1.5–6.0 / 1–4 % / 0.97–1.00 after its tone match | 138 kB |
| `phong` | soft multicoloured starburst: env map of the spiky object in the alex and vic parts | Aap | as above | the centre and every ray and petal in place, smooth gradients; `--match` | 1 | 1.0271 x 1.0187 | -3.5, -2.5 | 7.6 / 4.2 / 10.0 % / 0.94 (8.5 / 5.7 / 10.3 % / 0.93) | 239 kB |
| `tunnel` tile 0 | brown wavy ridges with pale highlights: the rings part's tunnel wall | Aap | as above | 2x2 repeat; every ridge, groove and highlight in place | 1 | 0.9993 x 0.9999 | +0.3, -0.1 | 11.2 / 9.1 / – / 0.99 | `tunnel.jpg` 715 kB (both tiles) |
| `tunnel` tile 1 | glowing flower with pale rings on violet: env map of the spiky sprites in the rings part | Aap | as above | the core, petals, rings and clouds in place; `--match` | 1 | 0.9972 x 0.9975 | +0.5, +0.5 | 10.0 / 7.9 / 11.9 % / 0.92 (11.9 / 10.1 / 14.3 % / 0.91) | (`tunnel.jpg`) |
| `blobby` tile 0 | pastel blobs in yellow, mint, purple and blue: the blobby part's background and env map | Aap | as above | 2x2 repeat; every blob in place, smooth gradients | 1 | 1.0000 x 1.0002 | +0.0, +0.2 | 9.4 / 6.6 / – / 0.99 | `blobby.jpg` 162 kB (both tiles) |
| `bd2` tile 0 | "like you never did before" in serif type over orange halftone dots and two pink bands: the typo part | Aap | as above | letter for letter, the same type sizes, the motion blur on "did", the dots and bands in place | 1 | 1.0006 x 1.0013 | -0.1, -0.1 | 18.0 / 13.9 / 4.6 % / 0.98 | `bd2.jpg` 1277 kB (tiles 0–2) |
| `bd2` tile 1 | "every moment will be lost" in bold sans-serif over ragged vertical colour bands: the typo part | Aap | as above | letter for letter, the bands and the glow around "lost" in place | 1 | 0.9995 x 1.0003 | +0.9, -0.2 | 26.6 / 20.3 / 0.5 % / 0.95 | (`bd2.jpg`) |
| `bd2` tile 2 | dark navy with soft black blotches: the wire blobs part | Aap | as above | every blotch in place, smooth tones; `--match`; a 2nd attempt asked for the original's wider soft halos and was discarded (detail 0.95 against 0.99, the same dark area) | 1 | 1.0014 x 0.9996 | -0.2, -0.1 | 4.4 / 1.6 / 5.4 % / 0.99 (4.8 / 2.3 / 5.2 % / 0.99) | (`bd2.jpg`) |
| `credits` | the words code, music, vocals (heavy) and statix, vic, emma (thin), written together (grey): the alex and vic parts | Aap | as above | on 3:1; letter for letter, the two fonts; each word fitted again on its own; `--match-elements` | 1 | the words 0.970–1.053 of the whole's fit | the words moved by their own fit up to 1.9 px | the words 30–66 / 18–39 / 5–12 % / 0.81–0.98 (the whole: 80.2 / 51.9 / 15.4 % / 0.68, as the generator spaced the words differently) | 86 kB |
| `sphere` | dark blue chrome ball with three highlights: env map of the tentacle star | Aap | as above | the ball's edge on the same pixels, every highlight and reflection in place; `--match` | 1 | 0.9994 x 0.9965 | +0.1, +0.4 | 7.3 / 6.1 / 3.0 % / 0.97 (8.3 / 7.3 / 4.2 % / 0.96) | 249 kB |
| `weird` | pale teal-blue organic shapes with black channels and gold glints: the paranoid part | Aap | as above | 2x2 repeat, including where the tile edges meet; `--match` | 1 | 0.9997 x 0.9992 | +0.3, +0.2 | 18.7 / 12.7 / – / 0.96 (25.2 / 20.8 / – / 0.94) | 177 kB |

### Kept as the original

| picture | why |
|---|---|
| `alex.raw`, `vic.raw` | photos of real people (Alex, Vic): an image generator would change their faces. They stay the original pixels; the remake reads them as palette indices anyway (notes-pictures.md, section 2) |
| `wipeout.raw`, `tex3d` tile 5 | vertical stripes of dithered colour ramps (Aap's stripes, the flowers, the face plate): a redraw would only remove the dither (notes-pictures.md, candidate 5) |
| `tex3d` tiles 2–4, `blobby` tile 1 | generated textures (plasma, falloff, the blob mask) |
| `phong1.raw`, `wire.raw`, `fontblur.raw`, `fontout.raw` | a shade map, a level mask and the fonts, read by value (the fonts are drawn as distance fields) |
| `frame.raw`, `frame2.raw`, `mustard.raw`, `smalfont.raw`, `stxvic.raw` | never shown |

### Notes for the port

- `bd2`: the typo and wire blobs parts read the picture by palette index through wirecut.fad (`mid_blobs.cpp`
  `kWireFS`). Row L of that table is, for each colour, the palette colour nearest to it blended towards white by
  (L - 32) / 31; with the redraw the shader blends the redraw's colour towards white by that amount instead. With
  bd2's own pixels as the redraw the frames match the table's up to the palette's quantisation (mean 1.5–5.9 of
  255), and the redraw's frames match the capture's colours and whitening.
- `final`, `303b`, `credits` and the metaballs' env map (`tex3d` tile 0) are read at an explicit mip level in
  per-pixel loops; those reads now take the level the redraw's size needs (`mipLevel()` in `fx.h`; level 0 for the
  original). For the metaballs the level comes from how far the env coordinate moves between output pixels,
  which needed the ray offsets' derivatives out of the silhouette branch: that also removed a row of stray bright
  pixels along the balls' silhouettes that the original-art frames had.
- `weird.raw` does not wrap in the original: its edges do not match (RMS 98 across the left-right edge against 22
  between neighbouring columns). The redraw keeps those edges as the generator drew them (RMS 56), so the seam the
  paranoid part shows in the original is still there, a little softer.
- `opening1`: the intro reads rows 0–199 only; the redraw covers them, rows 200–255 are the background grey.
- `final`, `303b`, `credits`: the parts read nothing outside the element rectangles except `final`'s photo (rows
  150–349, read as one block); the gaps between the rectangles come from the fit of the whole.

### What to look at in the comparison sheet

- `final`'s photo: where the original's printed labels are a few grey pixels, the redraw has legible ones (Roland,
  MC-303, VOLUME, CUTOFF, RESONANCE, ATTACK, DECAY, RELEASE, LFO, RATE, PART SELECT, VALUE, CURRENT, NEXT, STEP
  REC, BEAT, SONG, PATTERN, FUNC, SHIFT, EXIT, ENTER). Most are the MC-303's own labels; a few are garbled (a
  second "ATTACK" under the first, a "CUTOF"). It has a little more contrast in the knobs. The part shows the photo
  blurred sideways and dimmed, so the labels are only legible for moments.
- `final`'s lettering and icons: the same handwriting with clean strokes; the redraw's strokes are a little more
  even than the original's.
- `credits`: the thin words (statix, vic, emma) are a little thinner than the original's; each word is in its
  place, but the redraw's spacing between the words differed, so the fit of the whole is poor (see above).
- `tex3d` tile 1: the plates lost the original's grainy surface and are smoother; colours matched.
- `neuron`: the threads are thinner and crisper; the tangle is the original's.
- `weird`: smooth where the original is mottled and dithered; its unmatched tile edges are kept (see above).
- `blobby` tile 0: softer than the original.
- `tex3d` tile 0 and `tunnel` tile 1: the bands of the gold ball and the flower's background clouds follow the
  original's light and dark areas (detail 0.96 and 0.92), but the fine detail is the generator's.
- `phong`: drawn 3 % smaller and 3 px off-centre; the fit corrects it.
- `bd2` tile 1: in the original the "t" of "lost" is cut by the yellow-green band; the redraw draws the whole
  letter on top of it.
- `bd2` tile 2: the blotches have harder, narrower edges than the original's dithered halos.

## Restored video

`video/<CLIP>/NNNN.jpg`: AI restorations of the four video clips, made with SeedVR2 on Comfy Cloud from the original
frames, November 2026. One file per frame of `<CLIP>.LZW` (NNNN = the frame's index from 0000), 1280x768, the 5:3
shape of the 160x96 frames.

The footage is Acme's, shot and digitised for the demo in the four days before X'97 (April 1997). The NFO names
Harlequin as "the breakdancer in the demo" (HARL), says the demo features Vic "with simstim behind", and thanks
Ravian "for sorting out the video capture card". The clips: Harlequin breakdancing in a party hall (HARL), people
dancing and clowning (VS), people at computers (ALEX), a man's face looking around, in grey (PARANOID).

### How they were made

- **Frames.** Every frame decoded as the remake decodes it (`tools/dumpvideo.cpp`, see
  [notes-pictures.md](../../re/notes-pictures.md) section 3): 160x96 RGB, PARANOID grey (R = G = B = luma). The
  4x4 DCT blocks and the quantisation of Acme's codec are in these frames.
- **Uploads.** `tools/video_art.py inputs ../re/dump/video OUT` writes one MP4 per clip, VP9 lossless (yuv444p, at
  most 2/255 from the PNGs), at the clip's frame rate: HARL 0–121, VS 0–60, ALEX 0–42 and PARANOID 0–44 (all).
- **Model.** SeedVR2 (ByteDance's one-step diffusion model for video restoration) through the
  `ComfyUI-SeedVR2_VideoUpscaler` nodes, v2.5.24, on Comfy Cloud. A test on HARL's frames 0–59 compared it with
  Upscayl's Real-ESRGAN models; Dennis chose SeedVR2.
- **Settings**, the same as the test: DiT `seedvr2_ema_7b_sharp_fp16` (7B, the "sharp" variant), device `cuda:0`,
  attention `sdpa`, no block swap, `cache_model` off; VAE `ema_vae_fp16`, encode not tiled, decode tiled (1024,
  overlap 128); upscaler resolution 768 (the short side, so 160x96 becomes 1280x768), max resolution 0, batch size
  61, uniform batch size on (a short last batch is padded to 61), colour correction `lab`, temporal overlap 0,
  prepend frames 0, input and latent noise 0, offload `cpu`, seed 42. Output: Video Combine, lossless animated WebP
  (and an H.264 CRF 8 copy, not used).
- **Graph and run.** One graph with four independent chains, one per clip (Load Video → Get Video Components →
  SeedVR2 Video Upscaler with its own DiT and VAE loaders → Video Combine), saved as
  [docs/comfyui/seedvr2-video-restore.json](../../../docs/comfyui/seedvr2-video-restore.json). It ran once, on
  12 November 2026, on the free tier: the account's last of its five free runs, no credits. Queued 26 s, executed
  in 243 s for 271 frames in five batches of 61 (HARL two, the other clips one each). The test (60 frames, one
  batch) had taken 52 s.
- **Into this folder.** `tools/video_art.py frames <CLIP> RESULT.webp .` writes the frames listed below as JPEG
  quality 90 with full-resolution colour (4:4:4). PARANOID is written as a grey JPEG of the luminance (ITU-R 601
  weights, the ones the remake's paranoid shaders use); SeedVR2's output of the grey clip was grey to within 0.03
  of 255 on average.
- **Checks.** Every restored frame, scaled down to 160x96, matches its own original frame better than the frames
  before and after it (all 271). The mean colour difference to the original is under 0.6 of 255 per channel (the
  `lab` correction). In the remake: `--shots` in every part that shows a clip, with and without `--original-art`
  (the restored frames sit exactly where the decoded ones do: best shift 0, correlation 0.997–1.0 after a blur),
  seeking forwards and backwards gives identical frames, and `--bench` at 2560x1920 shows no waits for a decode
  when a clip starts (see the remake README).

### Frames

The frames the remake shows were found by logging the frame index `Ctx::video()` is asked for while rendering
every part that uses a clip in steps of 0.02 s. The folder holds those plus a margin of four to six frames.

| clip | frames in the file | shown | restored | in this folder | size |
|---|---|---|---|---|---|
| HARL | 200 | 0–116 (harl part, 11.68 fps) | 0–121 | 0–121 (122 files) | 17.7 MB, 98–204 kB per frame |
| VS | 147 | 0–56 (dance part, 8.76 fps) | 0–60 | 0–60 (61 files) | 8.7 MB, 132–158 kB |
| ALEX | 43 | 0–22 (end of the dance part, 8.76 fps) | 0–42 | 0–26 (27 files) | 3.5 MB, 107–141 kB |
| PARANOID | 45 | 0–24 (paranoid part, 4.77 fps), 0–13 again (face floor A/B) | 0–44 | 0–30 (31 files) | 2.2 MB, 63–80 kB (grey) |

241 files, 32.1 MB. In the face floor's phase C the part still steps through PARANOID's frames 0–43 but draws them
with strength 0; frames 31–43 there come from the decoded original, which is not visible either.

### What to look at

- SeedVR2 invents fine detail that the 160x96 original does not have. Faces, eyes, teeth, hands, the prints on
  shirts and the fronts of the monitors are the model's guesses. The people in these clips are real (Harlequin, Vic
  and the others), and the restored faces are not their faces, only plausible ones. Examples: in VS frame 5 the
  face is a blur of a few pixels in the original and a smiling face with eyes and teeth in the restoration; in
  ALEX the red-haired person at the monitors gets a face where the original has a few skin-coloured pixels;
  PARANOID's face gets stubble, eyebrow hairs and skin texture.
- PARANOID frame 20, where the eyes close and the original is smeared, has horizontal streaks across the cheek in
  the restoration.
- The codec's 4x4 blocks are gone; the floor of the party hall, the ceiling lights and the monitors are sharp. The
  colours are the original's, including the pink cast.
- HARL was restored in two batches, frames 0–60 and 61–121, with no overlap; there is no visible jump between 60
  and 61.
- The motion is the clips' own (11.68, 8.76 and 4.77 frames per second); nothing is interpolated between frames.
