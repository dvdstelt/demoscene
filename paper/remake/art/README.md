# Redrawn picture

A high-resolution redraw of the one hand-drawn picture of *Paper* by Psychic Link (Wired '96) that is shown as it
is: the pencil sketch of the word "Paper" on the logo page ("present the Paper intro"). Code, graphics and design of
Paper are by Statix, so the sketch is Statix's; it carries his small "stx" signature. The remake shows the redraw
by default; `--original-art` shows the original pixels, which it reads from `PAPER.EXE`.

This is a fan redraw of Statix's art, made for this non-commercial tribute and under the same non-commercial terms
as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). It is not Psychic Link's work and not endorsed
by them.

## How it was made

An AI redraw made with ChatGPT image generation from the original pixels, November 2026.

- The original is the 224x83 image at code32 0x1bf03, 4 bits per pixel, n = darkness 0..15 (0 = paper), which
  0x5623 puts on the logo page at (48, 50) as the page value v = 63 - 4 n (`paper/re/notes-logo.md`, section 3).
  It was uploaded as grey, grey = 255 v / 63 (paper white 255, the darkest pencil, n = 11, 77), on a paper-white
  canvas of 240x100 original pixels (the sketch at 8, 8, so that no stroke touches the edge), scaled up with
  nearest-neighbour 10 x 12 per pixel to the 4:3 display shape: 2400x1200, 2:1. `tools/align_art.py --upload
  sketch OUT` makes it.
- The prompt: "This is a 224x83 pixel pencil sketch from a 1996 MS-DOS intro, Paper by Psychic Link, graphics by
  Statix: the word "Paper" in tall, thorny, spiky hand-drawn letters, drawn as thin grey pencil outlines (most
  strokes are double lines) on plain white paper, with a tiny faint "stx" signature at the lower right. It is shown
  here scaled up with nearest-neighbour to the shape it has on screen (the 320x200 screen is shown at 4:3, so every
  original pixel is 1.2 times as tall as wide), on a white margin. Make a faithful high-resolution redraw of this
  pencil sketch: the same strokes, the same composition, proportions and framing, every line at the same position
  and size in the frame, the same thin grey pencil lines with the same darkness, on the same plain pure white
  background; only the resolution and the detail change: remove the pixelation. Do not add, remove or move anything,
  do not fill or shade the letters, do not add paper texture, ink, colour or shadows, do not crop or zoom. Keep the
  tiny "stx" signature. A landscape 2:1 image, as large as you can make it."
- Attempt 2, in the same conversation: "Thank you, that is close. Please make it again from my original picture,
  with the same instructions and two corrections: 1. The signature at the lower right is the lowercase letters
  "stx", tiny and drawn in a very faint, light grey that is barely visible on the white paper (much lighter than the
  pencil lines); keep it lowercase and that faint. 2. The original has a few soft, light grey smudged pencil strokes
  along some lines: on the top bar of the "a", on the crossbars of the "a" and the "e", along the top of the "e" and
  along the top bar of the "r". Keep those soft light grey strokes where they are. Everything else stays as in your
  first redraw: [...]"
- ChatGPT returned 1774x887 (2:1) both times. Chrome did not start downloads in that session (neither ChatGPT's
  download button nor an `<a download>`), so each image was taken from the page instead: drawn at 1:1 in an overlay
  and captured in three 886x888 PNG tiles with the browser extension's zoom capture, then stitched. The tiles agree
  exactly where they overlap, and the channel sums of the stitched image differ from those of the decoded image in
  the page by 0.003 %. The raw images are `paper/re/art-raw/paper-sketch-{1,2}.png` (not in Git; the upload is
  `upload-sketch.png` there).
- `tools/align_art.py sketch RAW.png` fits the redraw onto the original (scale in x and y and offset, see the
  script), maps its tones onto the original's (a monotone tone curve from quantile matching in the original's grid;
  greys from 252 up become paper white) and writes it covering exactly the sketch's 224x83 frame at the redraw's own
  pixel size. `tools/align_art.py --sheet OUT.jpg sketch` makes the comparison sheet (`paper/re/dump/art-compare.jpg`:
  the original, the redraw, and the original's strokes in red over the redraw).

## Format

`sketch.png`: 1656x736, 8-bit grey, 240 kB. It covers the sketch's 224x83 pixels at 4:3 (7.39 output pixels per
original pixel across; at 2560x1920 the sketch is 1792 pixels wide). Grey g is the page value v = 63 g / 255, which
the remake shades with the paper grain exactly as the original's v (index max(0, v + 2 e - 64) through the grey
ramp, e = grain 28..31).

## The picture

Residual: RMS of the grey difference (0..255) between the original and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is paper and the other is not, and
the correlation of the brightness detail (Gaussian 2 minus Gaussian 12 pixels; 1 = every stroke in place), measured
on the 240x100 canvas after the tone match. Scale is relative to "the redraw fills the canvas" (x by y), offset in
original pixels.

| file | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / paper / detail) | file size |
|---|---|---|---|---|---|---|---|---|---|
| sketch | the word "Paper" in thorny double-lined pencil letters, the "stx" signature | Statix (Psychic Link) | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | faithful redraw of the pencil sketch, same strokes and darkness, plain white paper; 2nd: the signature lowercase "stx" and very faint, keep the soft light grey strokes on the "a", "e" and "r" | 2 | 0.9997 x 1.0096 | -0.2, -0.5 | 19.1 / 8.0 / 2.8 % / 0.99 | 240 kB |

Attempt 1 fitted a little closer (1.0001 x 1.0044, offset -0.1, -0.2; 14.4 / 6.7 / 1.8 % / 0.99) but wrote the
signature as a clearly visible "StX" in a typeface, where the original's is eight pixels of the lightest grey
(n = 1), barely visible on the page, and it left out the original's soft light grey strokes. Attempt 2 has the
faint lowercase signature and the soft strokes; its lines are a little thinner, which with the soft strokes accounts
for most of the higher residual. Its strokes are as close to the original's (detail correlation 0.99 for both).

## Kept as the original

| data | why |
|---|---|
| the ink image (0x196c3, 224x92, 4 bit) | a map of palette indices 0x41..0x4f that the flood fill of 0x572f colours pixel by pixel: the remake upscales it at start-up (`src/scenes/logo.cpp`, see the remake's README, "The redrawn sketch and the ink image") |
| the cloud texture source (0x16ca3, 64x64, 4 bit) | a dithered grey cloud that 0x10d3 mirrors and blurs into a 256x256 texture: generated |
| the font (0x18ed7) | 1-bit glyphs, rebuilt as outlines and distance fields |
| the sketch strokes, the handwriting, the 3D objects | vector data, drawn at any resolution |

## Notes for the port

- After the fill, the ink covers almost all of the sketch: in the original only 50 of the sketch's 3915 pencil
  pixels stay visible (the signature and single pixels at the edges of the ink). The ink pass treats the ink's light
  greys as partial coverage (they are the art's anti-aliasing against the paper), so with the redraw the logo part
  hides the sketch wherever the fill has reached an ink pixel of any colour; strokes the redraw draws outside the
  original's ink pixels stay visible, as those 50 pixels do.
- The redraw's paper is exactly white (v = 63), the page value around the sketch, so the frame of the picture does
  not show.

## What to look at in the comparison sheet

- The strokes: the red of the original lies on the redraw's lines everywhere; the redraw's lines are cleaner and of
  one weight, where the original has darker and lighter pixels along a stroke.
- The soft grey strokes: the redraw puts them on the same bars (the "a", the crossbar of the "e", the tops of the "e"
  and the "r") as wider smudges along one line; the original's are bands of light pixels between the double lines.
- The signature: "stx", very faint, at the original's place.
