# Redrawn pictures

High-resolution redraws of the pictures of *Inside* (CNCD, The Gathering 1996) that are drawn as they are. The
graphics are Destop's (additional graphics Santtu). The remake shows these redraws by default; `--original-art`
shows the original pixels, which it reads from `INSIDE.DAT`. Photos of real people (the intro's picture, the
sprites, the faces in the credits) are never redrawn.

These are fan redraws made for this non-commercial tribute and under the same non-commercial terms as the rest of
the repository (see [NOTICE.md](../../../NOTICE.md)). They are not CNCD's work and not endorsed by them.

| picture | shows | how it was made | fit |
|---|---|---|---|
| `logo_inside.png` | the title in twisted 3D letters with a painted texture (`logo/inside.raw`, 249 × 139, index 0 transparent) | ChatGPT image generation, 21 November 2026, from the nearest-neighbour upload on a flat blue key (`tools/align_art.py --upload logo_inside`), first attempt; fitted and colour-matched with `tools/align_art.py logo_inside RAW --match` | RMS 20.7, detail correlation 0.967 |
| `credits_destop.png` | the credits' first name: a flat grey star and DESTOP in heavy oblique white capitals with a thin dark edge (`credits/kredu1.raw`, 173 × 75, a level map) | ChatGPT image generation, 22 November 2026, second attempt (see below); fitted with `tools/align_art.py credits_destop RAW --match` | RMS 13.7 (22.9 before the level match), detail correlation 0.974 |
| `credits_wili.png` | WILI with a star to its right (`credits/kredu7.raw`, 129 × 75) | ChatGPT, 23 November 2026, first attempt, in the same conversation; the redraw came back wider than the upload, which the fit's free scale takes up | RMS 9.4, detail correlation 0.987 |
| `credits_jouni.png` | a star and JOUNI (`credits/kredu2.raw`, 146 × 75) | as WILI | RMS 7.9, detail correlation 0.992 |
| `credits_juliet.png` | a star and JULIET (`credits/kredu8.raw`, 154 × 70) | as WILI | RMS 14.0, detail correlation 0.972 |
| `credits_yolk_legend.png` | two stars and YOLK & LEGEND (`credits/kredu3.raw`, 318 × 82) | as WILI | RMS 14.5, detail correlation 0.962 |

### The names in the credits

Each name picture is a level map that the credits add to the tunnel through a shade table: a flat star at level 10, a
scanned face (a photo of a group member) and the name at level 31. Only the star and the letters are redrawn. The
upload has the face replaced by the star's level (the shapes in `FACES` in `tools/align_art.py`, measured on the
level maps), so the redraw has a plain star there, and the remake draws the original's face pixels on top of the
redraw (`src/scenes/credits.cpp`). The redraw is stored as grey, 255 × level / 31. `--match` maps the redraw's
black, star and letters exactly onto levels 0, 10 and 31 (the original's areas are flat) and scales the edges between
them linearly. Each name went up in the conversation of the one before it, with the prompt below adapted to its size,
name and layout (WILI: the name on the left and the star to its right, partly behind the last letters; YOLK & LEGEND:
two stars with the names along the bottom between them). The first DESTOP attempt is not used.

Prompt for DESTOP (the other names use the same words with their own size, name and layout): "This is a 173x75 pixel
credits picture from a 1996 MS-DOS demo, Inside by CNCD, graphics by Destop, grey on a black canvas, scaled up with
nearest-neighbour and shown at 4:3 (every pixel 1.2 times as tall as wide). It shows a flat mid-grey five-pointed star
on the left and, to its right, the name DESTOP in heavy oblique (italic) sans-serif capitals, solid white with a thin
dark edge. Make a faithful high-resolution redraw as an image: the same composition and framing, the star and every
letter at exactly the same position, size and slant in the frame, the same letter shapes, spacing and weight, exactly
the same text, letter for letter, all capitals. Razor-sharp, clean edges like the text set in a vector font and
rendered at high resolution: the letters solid pure white, the star perfectly flat mid-grey with straight edges and
nothing on it, the background pure black, with no shading, texture, gradient, glow or shadow anywhere. Only the
resolution changes. Do not add, remove or move anything, do not crop or zoom. A landscape image of the same shape as
the upload, as large as you can make it."
