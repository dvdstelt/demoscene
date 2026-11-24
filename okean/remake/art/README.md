# The Sea Robot of Love — redrawn pictures

The intro draws one hand-made picture as it is: **der piipo's font** (credited "font and graphics" in the intro's
info file), 26 lowercase letters on a 285 × 38 sheet at code32 0x3cec2 of `KORSO2.EXE`, used for the captions and
the end texts ([`../../re/notes-core.md`](../../re/notes-core.md), section 6). Each letter has three layers: the
white body (index 216), single grey edge pixels at the steps of curves (254) and a 1-pixel outline (7) that takes
each part's own dark colour. The remake shows the redraw by default; `--original-art` shows the original pixels,
which it reads from `KORSO2.EXE`.

This is a fan redraw made for this non-commercial tribute and under the same non-commercial terms as the rest of the
repository (see [NOTICE.md](../../../NOTICE.md)). It is not Orange's work and not endorsed by them.

| picture | shows | where | attempts | fit |
|---|---|---|---|---|
| `font.png` | the font sheet, 26 tall, narrow lowercase letters, white with a thin red outline on black, in the sheet's layout at the redraw's resolution (1986 × 318) | the captions of the landscape, star and sheet parts and the end texts | third: the first added a grey band inside the right and bottom edges, like a bevel; the second removed it but drew the edges as pixel steps; the third went back to the first with the band removed | RMS 22.9, correlation 0.994 (whole sheet); per letter RMS 17.1 (r) to 27.9 (e) |

## How it was made

Following [docs/redrawing-pictures.md](../../../docs/redrawing-pictures.md), with ChatGPT's image generation on
24 November 2026, in one conversation:

1. `python3 tools/align_art.py --upload font upload_font.png` wrote the upload (900 × 900): the 26 glyphs in three
   rows with gaps, at 5 × 6 per original pixel (the screen's 4:3), body white, edge pixels grey, outline pure red,
   on black. It holds only letters.
2. The recipe's prompt (a 320×200 pixel artwork from a 1995 MS-DOS demo, The Sea Robot of Love by Orange, pixel art
   by der piipo, scaled up with nearest-neighbour, shown at 4:3; a faithful high-resolution redraw, every letter at
   the same position and size; exactly the same letters, the same font), plus: keep the thin pure red outline around
   every letter and the pure black background. It returned 1254 × 1254.
3. Two corrections, each saying what to change: the grey is only single anti-aliasing pixels at the steps of curves
   and diagonals, not a band along the right and bottom edges; then, as the second attempt had pixel steps on its
   edges, the first image with only that band removed.
4. `python3 tools/align_art.py font font_r3.png` fitted the redraw onto the original, first as a whole (scale
   0.9999 × 0.9991 of the canvas, offset −0.07, 0.14 original pixels) and then each glyph in its own cell, and wrote
   `font.png` in the sheet's layout. `--sheet OUT.jpg font` makes the comparison sheet. Much of the residual is the
   original's grey edge pixels, which the redraw replaces with smooth edges.

The remake takes alpha = max(R, G, B), the body from green and the outline from red minus green
(`src/okean/text.cpp`), so the outline keeps each part's colour and fades with it. The raw downloads and the upload
are in `okean/re/art-raw/` (ignored by Git).

The title's lettering ("the sea ROBOT of love", a 1-bit 320 × 200 mask at 0x1502c) is not redrawn: it is never drawn
as a picture. It decides which stamps of the light beams are left out, so it only shows blurred by the 8-pixel
stamps, and the remake reads it as it is (`tools/align_art.py --upload title_mask` would make its upload).
