# Redrawn pictures

High-resolution redraws of the ten stored pictures of *clone meets clone* (Acme and Fudge, Takeover '98): the four
caption texts, the four brush-drawn glyphs, the title and the credits. The intro credits its graphics to Aap
(design Aap and Echo), so every picture below is Aap's. The remake shows these redraws by default; `--original-art`
shows the original pixels, which it reads from `CLONE.EXE`. Where each picture is drawn and what a replacement has
to keep is in [../../re/notes-gfx.md](../../re/notes-gfx.md), section 7.

These are fan redraws of Aap's work, made for this non-commercial tribute and under the same non-commercial terms as
the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Aap's, Acme's or Fudge's work and not
endorsed by them.

## How they were made

Every redraw here is an AI redraw made with ChatGPT image generation from the original pixels, 13 November 2026.

- **Originals.** `re/dump/pictures/<name>.png` (written by `re/models/gfx/pictures.py`): the 6-bit greys as the
  intro shows them, the transparent pixels with alpha 0. A grey v is read as 255 * v / 63, so white is 255.
- **Uploads.** `tools/align_art.py --upload NAME OUT` scales the original up with nearest-neighbour at 4:3 (5k x 6k
  per pixel, every pixel 1.2 times as tall as wide, about 2000 pixels wide) on a canvas of a shape the generator
  makes, with a margin of at least 15 % (texts) or 12 % (glyphs) of the picture's width: the five one-line texts
  on 3:1, the credits and the glyphs on a square. Texts are uploaded grey on black (the intro draws them on black
  boxes, the title in part 1 on the dark particle field). The glyphs are sprites: their transparent pixels are
  uploaded as flat magenta (255, 0, 255).
- **Prompts.** The same for all with the description changed: "a WxH pixel text label (title, picture) from a 1998
  MS-DOS demo, clone meets clone by Acme and Fudge, graphics by Aap, on a black canvas (on a flat magenta background
  that stands for transparency), scaled up with nearest-neighbour and shown at 4:3 (every pixel 1.2 times as tall as
  wide)"; what it shows (the text, lower case, "Helvetica-like sans serif", which word is bold; for the glyphs "a
  cartoonish brush-drawn character that looks like a kanji: a white shape with a thick black outline" and its
  strokes and inner marks); "make a faithful high-resolution redraw as an image: the same composition and framing,
  ... at exactly the same position and size in the frame, the same letter shapes, spacing and weight (outline,
  fill); only the resolution and the detail change"; for the texts "razor-sharp, clean edges like the text set in
  a vector font and rendered at high resolution, solid pure white strokes with no streaks, blur or glow, on pure
  black" and "exactly the same text, letter for letter, all lower case"; for the glyphs "smooth, clean brush-drawn
  edges", "keep the background perfectly flat pure magenta right up to the black outline, with no shadow, glow,
  light border or texture; the character itself only black, white and grey"; "do not add, remove or move anything,
  do not crop or zoom"; "a wide 3:1 (square) image, as large as you can make it". Three conversations: the four
  captions; the four glyphs ("matching your first one in style"); the title and the credits.
- **Results.** ChatGPT returned 2172x724 for 3:1 and 1254x1254 for square, and refused nothing. Its flat magenta
  came out as about (250, 4, 250). The raw results are in `re/art-raw/` (not in Git; `clone-NAME-N.png`, N =
  attempt; the uploads in `upload/`).
- **Taking the images.** The Chrome that had ChatGPT open did not save downloads on this machine (neither ChatGPT's
  download button nor an `<a download>` of the image's blob arrived in `~/Downloads`), so every image was taken
  from the page as the recipe ([docs/redrawing-pictures.md](../../../docs/redrawing-pictures.md), section 3)
  describes: the image drawn at 1:1 into a fixed canvas overlay and captured with the browser extension's zoom
  (scale 1, the region always starting at the page origin, the overlay moved by whole pixels for the second tile),
  two overlapping tiles stitched. The overlaps agreed exactly; the stitched images' channel sums are 0.02–0.04 %
  below those of the image decoded in the page, all in the non-black pixels (the capture's colour handling, about
  one level in some pixels).
- **Fit.** `tools/align_art.py NAME RAW.png` fits each redraw onto the original (scale in x and y and offset, as
  DOPE's and 303's tools do) and writes it covering exactly the original's frame at the redraw's own pixel size.
  Special cases:
  - *Words and lines.* After the fit of the whole, each word of a text (split at runs of three or more empty
    columns) and each line of the credits (split at empty rows) is fitted again on its own (horizontal scale and
    offset; the vertical scale stays the whole's, so all words keep one letter height) and written exactly into its
    rectangle; the rectangles meet in the middle of the gaps. This puts every word on its pixels where the generator
    spaced the words a little differently.
  - *Glyphs.* The magenta is turned into alpha: a pixel is a * grey + (1 - a) * key, so a follows from how far it is
    from grey towards the key, which is measured on the redraw's own border. The fit compares the pictures on the
    key, so the silhouette counts.
- **Sheet.** `tools/align_art.py --sheet OUT.jpg NAME ...` makes the comparison sheet (`re/dump/art-compare.jpg`:
  the texts original above, redraw below; the glyphs and the credits original left, redraw right, the glyphs on
  blue-grey so the black outline shows).

## Format

| file | size | contents |
|---|---|---|
| `meet_us.png` | 1210x311 grey | the 42x9 text at 4:3, white on black (the grey is the coverage of white) |
| `get_them_clones.png` | 1349x229 grey | the 85x12 text |
| `connect_the_dots.png` | 1366x186 grey | the 88x10 text |
| `tunnels_meet_more_tunnels.png` | 1633x138 grey | the 142x10 text |
| `title_clone_meets_clone.png` | 1570x168 grey | the 101x9 title |
| `credits_aap_echo_simon.png` | 883x917 grey | the 37x32 credits |
| `glyph1.png` .. `glyph4.png` | 967x980, 791x1009, 833x1000, 748x1026 grey + alpha | the 77x65, 63x67, 68x68 and 56x64 glyphs; alpha 0 where the original is transparent, the outer edge anti-aliased |

8-bit PNG, 10 files, 1.3 MB. Every file covers exactly the original picture's pixel grid at 4:3 (screen pixels 1.2
times as tall as wide), at 11 to 29 output pixels per original pixel horizontally.

## The pictures

Residual: RMS of the difference (0..255) between the original and the aligned redraw area-averaged to the
original's pixels, the same after a 1-pixel blur, the share of pixels where one is background (black for the texts,
transparent for the glyphs) and the other is not, and the correlation of the brightness detail (Gaussian 1 minus
Gaussian 4 pixels; 1 = every shape in place). For the texts it is the written picture (after the words' own fits),
with the whole's fit in brackets. For the glyphs it is measured on the magenta key, so a silhouette edge that is off
by a fraction of a pixel counts with the full key-to-black difference; the RMS of the glyphs is not comparable with
the texts'. Scale is relative to "the redraw fills the upload's canvas" (x by y), offset in original pixels from
that.

| picture | shows | artist | made | prompt in short | attempt | scale | offset | residual (RMS / blurred / bg / detail) |
|---|---|---|---|---|---|---|---|---|
| `meet_us` | "meet us", part 2's caption | Aap | AI redraw made with ChatGPT image generation from the original pixels, November 2026 | on 3:1; letter for letter, the same font and weight, white on black; attempt 2: "too soft: the letter edges are blurry and the strokes have lighter vertical streaks; razor-sharp, clean edges like a vector font" (attempt 1 was a faithful but blurred redraw: 25.6 / 9.6 / 11.4 % / 0.97) | 2 | 0.9919 x 1.0477 | +0.3, +0.7 | 21.6 / 7.3 / 6.1 % / 0.98 (22.0 / 7.1 / 6.6 % / 0.98); the words moved by their own fit at most 0.03 px, scaled 1.007–1.012 |
| `get_them_clones` | "get them clones", part 5's caption | Aap | as above | as `meet_us` attempt 2, the g's descender | 1 | 0.9991 x 0.9678 | -0.0, +0.6 | 17.5 / 5.4 / 6.9 % / 0.99 (18.3 / 5.9 / 6.6 % / 0.99); words moved at most 0.06 px, scaled 0.993–0.999 |
| `connect_the_dots` | "connect the dots", part 3's caption | Aap | as above | as `get_them_clones` | 1 | 1.0763 x 1.0489 | -5.1, +0.6 | 25.0 / 7.1 / 7.5 % / 0.98 (37.5 / 8.8 / 11.9 % / 0.97: the generator set "the" 0.4 px too far right); words moved at most 0.44 px, scaled 1.000–1.004 |
| `tunnels_meet_more_tunnels` | "tunnels **meet** more tunnels", part 4's caption | Aap | as above | as `get_them_clones`, only "meet" bold | 1 | 1.0102 x 1.0594 | -2.0, -0.3 | 19.5 / 6.4 / 9.8 % / 0.99 (49.7 / 12.4 / 15.9 % / 0.94: the generator made "meet" about 4 % wider than the whole's fit and the other words 2–3 % narrower); words moved at most 0.65 px, scaled 0.958–1.028 |
| `title_clone_meets_clone` | "clone **meets** clone", the title in parts 1 and 11 | Aap | as above | as `get_them_clones`, only "meets" bold | 1 | 0.9977 x 0.9959 | +0.1, +0.3 | 19.5 / 9.0 / 6.6 % / 0.98 (22.9 / 10.2 / 7.7 % / 0.98); words moved at most 0.07 px, scaled 0.987–0.999 |
| `credits_aap_echo_simon` | ".aap", ".echo", ".simon", right-aligned, the end credits in part 1 | Aap | as above | on a square; the three lines with their dots, the same right edge, baselines and line spacing | 1 | 0.9912 x 1.0039 | +0.3, +0.1 | 22.8 / 9.8 / 8.9 % / 0.97 (23.6 / 10.3 / 10.1 % / 0.98); lines moved at most 0.1 px, scaled 1.000–1.018 |
| `glyph1` | brush-drawn character like 長 (a window with two bars, three spots and a curved stroke), part 2 | Aap | as above | on a square, on flat magenta; every stroke, bump and inner mark in place, the same thick black outline, white fill | 1 | 0.9985 x 0.9975 | +0.1, -0.0 | 19.4 / 7.9 / 2.2 % / 0.99 |
| `glyph2` | character like 系 (two slanted slits, a crossbar, three legs), part 5 | Aap | as above | as `glyph1` | 1 | 0.9990 x 0.9951 | +0.2, +0.1 | 33.7 / 16.7 / 3.1 % / 0.98 |
| `glyph3` | character like 隼 (a column with a slanted mark and three rungs, a curved stroke on the right, a wide base), part 3 | Aap | as above | as `glyph1`; attempt 2 asked for a wider column, thinner rungs and a dash instead of the oval mark, and was discarded (43.6 / 23.7 / 3.6 % / 0.95) | 1 | 1.0133 x 0.9978 | -0.5, -0.6 | 41.0 / 21.7 / 3.8 % / 0.96 |
| `glyph4` | character like 宜 (a round dot, a roof, a box with two rungs, a wide base), part 4 | Aap | as above | as `glyph1`; attempt 2 asked for smaller rungs and a thicker roof, and was discarded (50.7 / 29.1 / 3.7 % / 0.92) | 1 | 0.9981 x 1.0041 | +0.3, -0.4 | 41.2 / 21.5 / 3.6 % / 0.96 |

### What differs

- The texts are crisp vector-like letters of about the original's weight: the redraws have 2–7 % more white than
  the originals (area-averaged), the credits 15 % (a slightly heavier stroke). The letter shapes are a generic
  Helvetica-like sans serif, as the original's are.
- The glyphs keep their silhouettes to within about one original pixel (2–4 % of the pixels differ in being opaque
  or transparent, nearly all along the edge) and their coverage to within about 1 %. The redraws draw a thin grey line
  where the black outline meets the white fill, as the original's grey anti-aliasing does at 320x200. Glyphs 3 and
  4 differ most: glyph 3's column is about a pixel narrower on its left and its top mark is an oval where the
  original has a short slanted dash; glyph 4's two rungs are a little larger and its roof a little thinner.
