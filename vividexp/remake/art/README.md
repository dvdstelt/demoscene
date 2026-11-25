# Redrawn pictures

High-resolution redraws of *Vivid Experiment*'s hand-drawn pictures, made from the original pictures and faithful to
them: the same composition, letters, proportions and framing; only the resolution and the detail change. The remake
shows them by default; `--original-art` shows the original pixels. They are derived from the work of Doomsday's
graphicians (Dice and Damac) and stay under the same non-commercial terms as the rest of this project (see
[NOTICE.md](../../../NOTICE.md)). The recipe is [docs/redrawing-pictures.md](../../../docs/redrawing-pictures.md).

`tools/align_art.py --upload NAME` makes the picture to upload (the original's pixels scaled up with
nearest-neighbour to the 4:3 screen, 5 × 6 per pixel); `tools/align_art.py NAME REDRAW.png` fits the redraw onto
the original (a scale in x and y and an offset) and writes it here, covering exactly the original's frame; the
numbers below are its residual: the RMS colour difference (0..255) in the original's grid, the share of pixels where
one picture is background and the other is not, and the correlation of the detail (1.000 = every shape in place).

## The pictures

| file | original | shows | made | fit |
|---|---|---|---|---|
| `alkutext.png` | `pics/alkutext.pcx`, 320 × 200 | the opening's white lettering ("a doomsday production", "in the year 1996", "at assembly", the labels "coding", "graphics", "music", and the unused "called"), as a grey coverage mask | ChatGPT, 25 November 2026, first attempt | RMS 11.3, background 0.79 %, correlation 0.994 |
| `doom.png`, `mri.png`, `dice.png` | `pics/doom.pcx`, `mri.pcx`, `dice.pcx`, 1280 × 200 | the credits' names in white serif capitals, as coverage masks covering the whole 1280 × 200 picture | ChatGPT, 25 November 2026, first attempt; the three names in one upload | RMS 7.7 / 6.3 / 6.5, correlation 0.998 each |
| `damac.png`, `swallow.png`, `wode.png` | `pics/damac.pcx`, `swallow.pcx`, `wode.pcx`, 1280 × 200 | the same | ChatGPT, 25 November 2026, first attempt; one upload | RMS 26.9 / 31.7 / 18.8, correlation 0.951 / 0.953 / 0.977 (the letters 3 % lower than the original's: an overlay shows every letter in place within a pixel) |

The prompt (one conversation for all of them, so they share a style): a 320 × 200 artwork (the names: 560 × 233, three
names cropped from their pictures and stacked) from the 1996 MS-DOS demo *Vivid Experiment* by Doomsday, scaled up
with nearest-neighbour; "make a faithful high-resolution redraw: the same composition and framing, every word at
exactly the same position and size, exactly the same text letter for letter, the same typeface, line breaks and
letter spacing; pure white letters with smooth, crisp anti-aliased edges on a pure black background; only the
resolution and the detail change; do not add, remove or move anything, do not crop or zoom; no shadows, glow,
outlines or texture".

The text pictures are drawn by the demo through palette arithmetic (notes-core.md 7: the text's level added to the
sky's index), so they are kept as coverage masks and the remake does the arithmetic in colour.

## Not redrawn

- The photos of people: the title picture `vivid.pcx` and the greetings picture `greets.pcx` (photos of women), and
  the photos of women in the bitmap effects (`nainen1..3.raw`).
- The captions `synktxt1..4.pcx`: they quote a song's lyrics, which this project does not reproduce.
- Everything an effect reads through palette indices or tables: the height pictures and light pictures of the bump
  mapping, the bitmap effects' textures, the 3D scenes' maps and textures.
