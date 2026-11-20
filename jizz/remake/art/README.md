# Redrawn pictures

High-resolution redraws of the two hand-drawn pictures of *JIZZ* (The Black Lotus, Wired '97): the "jizz" logo
(`logo.png`) and the end picture "~JIZZ~ TBL'97" (`lettering.png`). JIZZ.DOC credits the "freaky "jizz" logo" to
Sjaak/TRP (Sick Sjaak of Trepaan) and the graphics to Jace, so the logo is Sjaak's and the lettering Jace's. The
remake shows these redraws by default; `--original-art` shows the original pixels, which it reads from `JIZZ.EXE`.
Where each picture is used is in [../../re/notes-textures.md](../../re/notes-textures.md) (sections 5, 7 and 8) and
[../../re/notes-parts2d.md](../../re/notes-parts2d.md) (sections 4 and 7).

These are fan redraws of Sjaak's and Jace's work, made for this non-commercial tribute and under the same
non-commercial terms as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Sjaak's,
Jace's or The Black Lotus's work and not endorsed by them.

## How they were made

Both are AI redraws made with ChatGPT image generation from the original pixels, 20 November 2026, in one
conversation: the logo first, then the lettering.

- **Originals.** The two 4-bit run-length pictures of texture script 14 (0x12a7d; op 0x22, decoder 0x13df4), 256x200
  with 16 grey levels n * 17: the logo at 0x27d68 (levels 0 and 136..255, mostly 0 and 255) and the lettering at
  0x275b8 (light grey serif capitals; the solid letters are level 238). `tools/align_art.py` decodes them from the
  unpacked program image (`re/jizz_img.bin`). Both are shown 1:1 on the 320x200 screen, which is shown at 4:3.
- **Uploads.** `tools/align_art.py --upload NAME OUT` puts the bounding box of the picture's non-zero pixels (logo
  183x126 at 68, 33; lettering 131x105 at 89, 32) with a margin of 12 % of its width on a black canvas of square
  display shape (logo 227x189 original pixels, lettering 164x137) and scales it up with nearest-neighbour, 10 x 12
  per pixel (every pixel 1.2 times as tall as wide): 2270x2268 and 1640x1644, the original greys as they are.
- **Prompt, logo.** "This is a 256x200 pixel picture from a 1997 MS-DOS intro, JIZZ by The Black Lotus: its "jizz"
  logo, drawn by Sjaak of Trepaan. It is shown here scaled up with nearest-neighbour to the shape it has on screen
  (the 320x200 screen is shown at 4:3, so every original pixel is 1.2 times as tall as wide), on a black margin. It
  shows white brush-drawn shapes on pure black: wild, splashy brush strokes with round blobs, sharp spikes and
  swashes, and a small hand-written "jizz!" signature at the lower right. Make a faithful high-resolution redraw of
  this picture: the same composition, proportions and framing, every shape, spike, blob, hole and thin black gap at
  the same position and size in the frame, the same signature "jizz!" letter for letter; pure white shapes on a pure
  black background with smooth, crisp, clean brush-drawn edges. Only the resolution and the detail change: remove the
  pixelation, so that the staircase edges become the smooth curves and sharp points they stand for. Do not add,
  remove or move anything, no grey shading, no texture, no glow, no shadow, do not crop or zoom. A square image, as
  large as you can make it."
- **Attempt 2, logo**, in the same conversation: "Thank you, that is close. Please make it again from my original
  picture, with the same instructions and these corrections: 1. Keep every shape exactly as wide as the original's
  white area: your shapes are a little thinner than the original's (about 9 % less white); the white should fill the
  original's pixels right up to their edges. 2. In the brush stroke at the lower right, between its long upper blade
  and its lower blade, the original has four or five long, thin, parallel streaks fanning out towards the upper
  right, separated by thin black lines; keep each of them as a separate thin streak. 3. Keep the thin black lines
  inside the white shapes where the original has them. Everything else stays as in your first redraw: [...]"
- **Prompt, lettering**, in the same conversation: "Now the second picture from the same intro, in the same way: a
  256x200 pixel picture of JIZZ by The Black Lotus, graphics by Jace, the intro's end picture. It is shown here
  scaled up with nearest-neighbour to the shape it has on screen (every original pixel 1.2 times as tall as wide),
  on a black margin. It shows the text "~JIZZ~" above "TBL'97", centred, in light grey serif capitals (a Times-like
  typeface with thin serifs, anti-aliased) on pure black: a tilde before and after JIZZ, a round dot above the I and
  a small round dot below the first Z; "TBL'97" is letter-spaced and has a curly apostrophe. Make a faithful
  high-resolution redraw of this picture: exactly the same text, letter for letter, the same serif typeface, letter
  shapes, widths, spacing, stroke weights and serifs, every letter, dot and tilde at the same position and size in
  the frame, the same light grey letters on a pure black background, with razor-sharp, clean edges like text set in
  a vector font and rendered at high resolution. Only the resolution and the detail change: remove the pixelation.
  Do not add, remove or move anything, no glow, no shadow, no texture, do not crop or zoom. A square image, as large
  as you can make it."
- **Attempt 2, lettering**: "[...] one correction: your letter strokes are a little heavier than the original's
  (about 10 % more white). Keep the original's stroke weights: the thin hairlines and serifs as thin as in the
  original, the thick stems no thicker, the dot above the I and the dot below the first Z as small as in the
  original. Everything else stays exactly as in your redraw: [...]"
- **Results.** ChatGPT returned 1254x1254 every time and refused nothing. Chrome delivered downloads in this
  session, so each image was downloaded from the page with an `<a download>` of the image's blob. The raw results
  are in `re/art-raw/` (not in Git; `jizz-logo-N.png` and `jizz-lettering-N.png`, N = attempt; the uploads in
  `upload/`).
- **Fit.** `tools/align_art.py NAME RAW.png` stretches the redraw's levels to a coverage mask (its background, the
  median of the canvas margin, to 0; its solid shapes to 255), fits it onto the original's coverage (the picture
  divided by its solid level) with a scale in x and y and an offset, as clone's tool does, and writes it covering
  exactly the 256x200 picture at the redraw's own pixel size. The lettering's two lines (split at the empty rows
  between them) are then fitted again on their own and written into their rectangles.
- **Sheet.** `tools/align_art.py --sheet OUT.jpg` makes the comparison sheet (`re/dump/art-compare.jpg`: per picture
  the original, the redraw and an overlay, the original's coverage in red and the redraw's in cyan).

## Format

| file | size | contents |
|---|---|---|
| `logo.png` | 1415x1327 grey, 200 kB | coverage (255 = the picture's solid level) over the whole 256x200 picture at 4:3, 5.53 output pixels per original pixel across |
| `lettering.png` | 1956x1834 grey, 119 kB | the same for the lettering, 7.64 output pixels per original pixel across |

The script multiplies each picture with blurred clouds (logo2 = clouds x logo / 256, logo1 = clouds x lettering /
256), and the pastel colours come from those clouds. So only the grey pictures are redrawn: the remake builds the
clouds with the script at 1024x1024 like the other textures (`Tex::LogoClouds`, the script's W just before its first
multiply) and multiplies them in the shader with the redraw's shape (coverage at 1/2, anti-aliased at the output
resolution) at the picture's solid level (the maximum of the decoded picture: 255 for the logo, 238 for the
lettering). The logo's one-pixel dark outline is a distance field of the redraw's shape, built at the redraw's own
resolution and dilated by one original pixel (`src/scenes/logo.cpp`).

## The pictures

Residual: RMS of the difference (0..255) between the original's coverage and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is background (black) and the other
is not, and the correlation of the brightness detail (Gaussian 1 minus Gaussian 4 pixels; 1 = every shape in
place), over the bounding box. For the lettering it is the written picture (after the lines' own fits), with the
whole's fit in brackets. Scale is relative to "the redraw fills the upload's canvas" (x by y), offset in original
pixels from that. White: the redraw's coverage summed over the picture against the original's.

| picture | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / bg / detail) | white |
|---|---|---|---|---|---|---|---|---|---|
| `logo` | the "jizz" logo: white brush shapes (blobs, spikes, swashes) with a small "jizz!" signature at the lower right; the "jizz" part, 8:00-9:00 | Sjaak of Trepaan | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | every shape, spike, blob, hole and thin black gap in place, the signature letter for letter, pure white on pure black, crisp brush-drawn edges, no grey shading; attempt 2: the shapes as wide as the original's, the thin streaks of the lower right stroke and the thin black lines inside the shapes kept | 2 | 0.9994 x 1.0038 | +0.1, -0.3 | 22.0 / 10.4 / 1.8 % / 0.99 | -4.8 % |
| `lettering` | "~JIZZ~" above "TBL'97" in light grey serif capitals, a dot above the I and below the first Z; the end picture, 15:00 to the end | Jace | as above | exactly the same text, letter for letter, the same typeface, widths, spacing, stroke weights, serifs and dots, light grey on pure black, razor-sharp edges | 1 | 1.0005 x 1.0004 | +0.1, -0.1 | 14.6 / 8.2 / 1.6 % / 0.99 (14.8 / 8.4 / 1.4 % / 0.99); the lines moved by their own fit at most 0.02 px, scaled 0.9988-1.0017 vertically | +10.2 % |

Discarded attempts:

- Logo, attempt 1: scale 0.9998 x 1.0032, offset +0.1, -0.3, residual 26.5 / 14.1 / 1.5 % / 0.98, 8.9 % less white
  than the original; it merged the thin streaks of the lower right stroke into two blades. Attempt 2 is closer in
  every number but the background mismatch and keeps one more streak.
- Lettering, attempt 2: scale 1.0019 x 1.0003, residual 18.6 / 11.9 / 2.5 % / 0.99, 18.6 % less white: the
  correction overshot, and its lower line fitted worse (0.13 px off vertically, scaled 1.0105).

### What differs

- The logo's shapes lie on the original's within a fraction of an original pixel, with smooth brush-drawn edges and
  sharp points where the original has stair-stepped pixels. It has 4.8 % less white than the original, a thin band
  along the edges (the original's light grey anti-aliasing pixels count as nearly white). The stroke at the lower
  right has three blades and a thin streak between them where the original has four or five thin streaks separated
  by one-pixel dark lines; the dark lines inside the shapes are thin black gaps, which the remake shows as dark
  lines like the outline.
- The lettering has the original's letter shapes, positions, tildes and dots; its strokes are a little heavier
  (10.2 % more white).

## Kept as the original

| data | why |
|---|---|
| the lettering in the TBL part's letter mask (testshit, script 13 at 0x129f7) | a generated texture: the script cuts the picture to a window with two thresholded ramps (whose dword carries leave the faint "9"), warps, enlarges, blurs and inverts it into the "TBL" mask the light rays read (`src/scenes/tbl.cpp`); generated textures stay computed from the original |
| the clouds of logo1 and logo2 | generated by the script (midpoint displacement from the intro's random generator, shifted, scaled, blurred), built at 1024x1024 like the other textures |
