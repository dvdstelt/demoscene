# The Sea Robot of Love — redrawn pictures

The intro draws one hand-made picture as it is: **der piipo's font** (credited "font and graphics" in the intro's
info file), 26 lowercase letters on a 285 × 38 sheet at code32 0x3cec2 of `KORSO2.EXE`, used for the captions and
the end texts ([`../../re/notes-core.md`](../../re/notes-core.md), section 6). Each letter has three layers: the
white body (index 216), single grey edge pixels at the steps of curves (254) and a 1-pixel outline (7) that takes
each part's own dark colour.

| picture | what | status |
|---|---|---|
| `font.png` | the font sheet, 285 × 38 at the redraw's resolution: white body and red outline on black | **not redrawn yet**: the remake shows the original pixels |

The redraw is prepared but has not been made: it needs a ChatGPT session, which this run did not have. The
procedure, following [docs/redrawing-pictures.md](../../../docs/redrawing-pictures.md):

1. `python3 tools/align_art.py --upload font upload_font.png` writes the upload (900 × 900): the 26 glyphs in three
   rows with gaps, at 5 × 6 per original pixel (the screen's 4:3), body white, edge pixels grey, outline pure red,
   on black. It holds only letters.
2. ChatGPT's image generation, the recipe's prompt ("a faithful high-resolution redraw ... exactly the same letters,
   the same font ..."), plus: keep the thin pure red outline around every letter and the black background.
3. `python3 tools/align_art.py font REDRAW.png` fits the redraw onto the original, first as a whole and then each
   glyph in its own cell, and writes `art/font.png` in the sheet's layout; `--sheet OUT.jpg font` makes the
   comparison sheet. The remake takes alpha = max(R, G, B), the body from green and the outline from red minus
   green (`src/okean/text.cpp`), so the outline keeps the part's colour.

The title's lettering ("the sea ROBOT of love", a 1-bit 320 × 200 mask at 0x1502c) is not redrawn: it is never drawn
as a picture. It decides which stamps of the light beams are left out, so it only shows blurred by the 8-pixel
stamps, and the remake reads it as it is (`tools/align_art.py --upload title_mask` would make its upload).
