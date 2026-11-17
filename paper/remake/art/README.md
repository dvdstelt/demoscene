# Redrawn pictures

High-resolution redraws of the hand-drawn pictures on the logo page ("present the Paper intro") of *Paper* by
Psychic Link (Wired '96): the pencil sketch of the word "Paper" (`sketch.png`), the text "present the" and "intro"
(`text.png`) and the finished inked "Paper" logo (`ink.png`). Code, graphics and design of Paper are by Statix, so
all three are Statix's; the sketch carries his small "stx" signature. The remake shows the redraws by default;
`--original-art` shows the original pixels (the sketch, the text from the intro's font, the ink image), which it
reads from `PAPER.EXE`.

These are fan redraws of Statix's art, made for this non-commercial tribute and under the same non-commercial terms
as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Psychic Link's work and not
endorsed by them.

## How the sketch was made

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

## How the text was made

An AI redraw made with ChatGPT image generation from the original pixels, November 2026, in a conversation of its own.

- The original is the sharp copy of "present the\x01intro" (code32 0x5929) that 0x5623 draws with the intro's
  1-bit, 32-row font (0xe2e, RLE at 0x18ed7) into its 320x80 text buffer from the pen (16, 1); the code 1 moves the
  pen so that "intro" lands at (175, 40) (`paper/re/notes-logo.md`, section 3). The original blurs this copy once
  (the cross kernel, sigma 1 original pixel), which at 320x200 is its anti-aliasing, and adds a shadow copy at
  (+4, +3), blurred five times and halved. At 2560x1920 that one-pixel blur is about 8 pixels wide and the 32-pixel
  glyphs are too coarse to scale cleanly, so the letters are redrawn as a sharp mask and the remake builds the
  shadow from the same mask.
- It was uploaded as a 1-bit mask, white letters on black, on a black canvas of 324x135 original pixels (the 320x80
  buffer at 2, 27), scaled up with nearest-neighbour 10 x 12 per pixel to the 4:3 display shape: 3240x1620, 2:1.
  `tools/align_art.py --upload text OUT` makes it.
- The prompt: "This picture is the text "present the" and "intro" from the logo page of a 1996 MS-DOS intro, Paper
  by Psychic Link, code, graphics and design by Statix: two lines in a wide, heavy, extended grotesque sans-serif
  bitmap font, 32 pixels high, shown here as a 1-bit mask, white letters on black, scaled up with nearest-neighbour
  to the shape it has on screen (the 320x200 screen is shown at 4:3, so every original pixel is 1.2 times as tall as
  wide). Make a faithful high-resolution redraw of this text as a clean mask: exactly the same text, letter for
  letter, the same wide heavy letterforms, the same letter widths, spacing and stroke weights, every letter at the
  same position and size in the frame, pure white letters on a pure black background with smooth, crisp edges. Only
  the resolution changes: remove the pixelation, so that the staircase edges become the straight lines and curves
  they stand for. Do not change the font, do not add, remove or move anything, no shadow, no grey, no texture, do
  not crop or zoom. A landscape 2:1 image, as large as you can make it."
- ChatGPT returned 1774x887 (2:1). The first attempt was kept: every letter lies on the original's within a
  fraction of an original pixel. It was downloaded from the page with an `<a download>` (Chrome delivered
  downloads in this session). The raw image is `paper/re/art-raw/paper-text-1.png` (not in Git; the upload is
  `upload-text.png` there).
- `tools/align_art.py text RAW.png` fits it onto the original (scale in x and y and offset), stretches its levels
  so that the background is 0 and the solid letters 255 (the download already was) and writes it covering exactly
  the 320x80 text buffer at the redraw's own pixel size.

## How the ink was made

An AI redraw made with ChatGPT image generation from the original pixels, November 2026, in a conversation of its own.

- The original is the ink image at code32 0x196c3: 224x92, 4 bits per pixel, 0 = no ink, n = palette colour
  0x40 + n of the default palette 0x169a3 (a near-black body 0x4b, greys 0x41..0x4a for the highlights and the
  anti-aliasing against the paper, dark reds 0x4c..0x4f for the rims). 0x572f's flood fill colours it pixel by
  pixel at (48, 50) on the page; the finished logo is every ink pixel in its colour (`paper/re/notes-logo.md`,
  section 4). The fill only decides when each pixel appears, so the redraw is of the finished picture and the
  remake keeps the fill's timing from the original's map.
- It was uploaded in those colours on white, on a white canvas of 252x105 original pixels (the image at 14, 6),
  scaled up with nearest-neighbour 10 x 12 per pixel: 2520x1260, 2:1. `tools/align_art.py --upload ink OUT`
  makes it.
- The prompt: "This picture is the inked logo "Paper" from a 1996 MS-DOS intro, Paper by Psychic Link, code,
  graphics and design by Statix: 224x92 pixel art of the word "Paper" in tall, thorny, spiky hand-drawn letters,
  filled with glossy near-black ink (a very dark brown-black), with light grey glossy highlights along some edges
  and dark red rims along others, and a few small ink drops and blobs around the letters, on plain white. It is
  shown here scaled up with nearest-neighbour to the shape it has on screen (the 320x200 screen is shown at 4:3, so
  every original pixel is 1.2 times as tall as wide), on a white margin. Make a faithful high-resolution redraw of
  this inked logo: the same letter shapes, thorns, drops and blobs, the same composition, proportions and framing,
  every part at the same position and size in the frame, the same colours: the glossy near-black ink, the grey
  highlights and the dark red rims exactly where the original has them. Only the resolution and the detail change:
  remove the pixelation, with smooth, clean edges. Do not add, remove or move anything, no pencil lines, no paper
  texture, no shadow, do not crop or zoom. Make the background transparent. A landscape 2:1 image, as large as you
  can make it."
- ChatGPT returned 1774x887 (2:1) as RGBA with a transparent background and clean, dark anti-aliased edges. The
  first attempt was kept: its letters, thorns, drops and red rims lie on the original's. It was downloaded like
  the text; the raw image is `paper/re/art-raw/paper-ink-1.png` (the upload is `upload-ink.png` there).
- `tools/align_art.py ink RAW.png` fits it as it looks over white, gives its ink body the original's body colour
  0x4b (the redraw's body was (24, 9, 6), the original's is (49, 40, 40) in 8 bits: per channel a piecewise-linear
  curve through 0, body -> body and 255, so the highlights stay light and the rims red), and writes it as RGBA
  covering exactly the 224x92 frame, colour and alpha resampled premultiplied.
- Where it differs from the original: its highlights are white where the original's lightest grey is 0x41 (228 of
  255), and its ink has a thin darker outline, so it looks a little glossier; the ink image's holes (pixels 0 inside
  the letters, where the original shows the paper as a light speck) are ink with highlights.

## Format

- `sketch.png`: 1656x736, 8-bit grey, 240 kB. It covers the sketch's 224x83 pixels at 4:3 (7.39 output pixels per
  original pixel across; at 2560x1920 the sketch is 1792 pixels wide). Grey g is the page value v = 63 g / 255,
  which the remake shades with the paper grain exactly as the original's v (index max(0, v + 2 e - 64) through the
  grey ramp, e = grain 28..31).
- `text.png`: 1752x526, 8-bit grey, 107 kB. Coverage (255 = letter) over the 320x80 text buffer (5.48 output pixels
  per original pixel across). The page pass draws the letters where it is above one half, anti-aliased at the output
  resolution; the shadow is the same mask at (+4, +3), blurred with sigma sqrt(5) original pixels and halved.
- `ink.png`: 1577x777, RGBA (straight alpha), 695 kB. It covers the ink image's 224x92 pixels at 4:3 (7.04 output
  pixels per original pixel across). Shown without the paper grain, as the original draws its ink.

## The pictures

Residual: RMS of the colour difference (0..255) between the original and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is background and the other is not,
and the correlation of the brightness detail (Gaussian 2 minus Gaussian 12 pixels; 1 = every stroke in place),
measured on the upload canvas after the tone or colour match (the ink over white). Scale is relative to "the redraw
fills the canvas" (x by y), offset in original pixels. The text's RMS is mostly the original's 1-bit staircase
against the redraw's smooth edges: its 0.5 contour differs from the original's pixel edges on 4.7 % of the
letters' pixels, all along the edges.

| file | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / background / detail) | file size |
|---|---|---|---|---|---|---|---|---|---|
| sketch | the word "Paper" in thorny double-lined pencil letters, the "stx" signature | Statix (Psychic Link) | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | faithful redraw of the pencil sketch, same strokes and darkness, plain white paper; 2nd: the signature lowercase "stx" and very faint, keep the soft light grey strokes on the "a", "e" and "r" | 2 | 0.9997 x 1.0096 | -0.2, -0.5 | 19.1 / 8.0 / 2.8 % / 0.99 | 240 kB |
| text | "present the" and "intro" in a wide, heavy grotesque, as a white-on-black mask | Statix (Psychic Link) | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | faithful redraw of the 1-bit text as a clean mask, letter for letter, the same letterforms, widths and spacing, smooth crisp edges, no shadow | 1 | 1.0001 x 1.0003 | -0.1, -0.0 | 14.9 / 6.0 / 1.9 % / 1.00 | 107 kB |
| ink | the inked "Paper": glossy near-black thorny letters with grey highlights, dark red rims and ink drops, on transparency | Statix (Psychic Link) | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | faithful redraw of the inked logo, same shapes, drops and colours, highlights and red rims where the original has them, transparent background | 1 | 0.9997 x 0.9963 | -0.0, +0.1 | 13.2 / 4.7 / 1.3 % / 1.00 (15.7 / 9.7 / 1.4 % / 1.00 before the colour match) | 695 kB |

Attempt 1 fitted a little closer (1.0001 x 1.0044, offset -0.1, -0.2; 14.4 / 6.7 / 1.8 % / 0.99) but wrote the
signature as a clearly visible "StX" in a typeface, where the original's is eight pixels of the lightest grey
(n = 1), barely visible on the page, and it left out the original's soft light grey strokes. Attempt 2 has the
faint lowercase signature and the soft strokes; its lines are a little thinner, which with the soft strokes accounts
for most of the higher residual. Its strokes are as close to the original's (detail correlation 0.99 for both).

## Kept as the original

| data | why |
|---|---|
| the ink image's map (0x196c3) | the flood fill's walls and seeds: the remake runs the fill on the original's map for the timing that reveals the redraw, and upscales the image itself with `--original-art` (`src/scenes/logo.cpp`, see the remake's README, "The redrawn pictures on the logo page") |
| the cloud texture source (0x16ca3, 64x64, 4 bit) | a dithered grey cloud that 0x10d3 mirrors and blurs into a 256x256 texture: generated |
| the font (0x18ed7) | 1-bit glyphs, rebuilt as outlines and distance fields for the other parts' text (the title, "the end", the "1996"); the logo page's text is the redraw |
| the sketch strokes, the handwriting, the 3D objects | vector data, drawn at any resolution |

## Notes for the port

- The ink redraw is revealed by the original's fill timing: the remake runs 0x572f's fill on the original's map for
  every ink pixel's arrival tick R, extends R to a field of 4 x 4 samples per original pixel (each sample takes the
  region, one per seed, of its nearest original ink pixel and the Gaussian-weighted mean, sigma 0.6 px, of that
  region's ticks around it) and shows the redraw where R < tick + 0.5, with an anti-aliased front. At whole ticks
  that is the original's pixel set; between ticks the front moves on smoothly along the original's diamond-shaped
  fronts.
- After the fill, the ink covers almost all of the sketch: in the original only 50 of the sketch's 3915 pencil
  pixels stay visible (the signature and single pixels at the edges of the ink). The two redraws' edges do not meet
  exactly; every stroke of the sketch redraw except the signature lies within 1 original pixel of the ink redraw,
  so the page pass hides the sketch within 1 px of the ink wherever the ink has arrived. The signature, 6 px from
  the nearest ink, stays.
- The sketch redraw's paper is exactly white (v = 63), the page value around the sketch, so the frame of the
  picture does not show.
- The text mask holds both lines in the text buffer's layout; the page shows the buffer's rows 0..39 at page rows
  10..49 and rows 40..79 at 158..197, so each half takes only its own line from the mask. The shadow keeps the
  original's split, including a faint trace of the shadow of the "i" dot of "intro" above the logo.

## What to look at in the comparison sheet

`tools/align_art.py --sheet OUT.jpg sketch text ink` (`paper/re/dump/art-compare.jpg`): per picture the original,
the redraw and an overlay.

- The strokes: the red of the original lies on the redraw's lines everywhere; the redraw's lines are cleaner and of
  one weight, where the original has darker and lighter pixels along a stroke.
- The soft grey strokes: the redraw puts them on the same bars (the "a", the crossbar of the "e", the tops of the "e"
  and the "r") as wider smudges along one line; the original's are bands of light pixels between the double lines.
- The signature: "stx", very faint, at the original's place.
- The text: the overlay shows the original's pixels in red and the redraw in cyan; they agree (white) everywhere
  except thin fringes along the staircases.
- The ink: the same overlay with the original's ink pixels; the redraw follows the original's outline, a little
  inside the light grey anti-aliasing pixels, and fills the holes inside the letters (cyan specks).
