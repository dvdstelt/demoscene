# Redrawn pictures

High-resolution redraws of the pictures of *Inside* (CNCD, The Gathering 1996) that are drawn as they are. The
graphics are Destop's (additional graphics Santtu); Hybrid's page about the demo credits the photography to Michael
Schwartz. The remake shows these redraws by default; `--original-art` shows the original pixels, which it reads from
`INSIDE.DAT`. The four photos of people in the sprites parts are redrawn too, at my request (see "The photos"
below); the faces in the credits keep their original pixels.

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
| `sprite2.png` | a man in white reaching his open hand towards the camera, his face and a white and green cap to the right (`nigga/sprite2.raw`, 172 × 200, index 0 transparent, cut off at the left and bottom) | ChatGPT, 23 November 2026, first attempt, from the upload on a flat blue key (`tools/align_art.py --upload sprite2`); fitted with `tools/align_art.py sprite2 RAW` | RMS 31.5, detail correlation 0.723 |
| `sprite9.png` | a man with dreadlocks and dark glasses lying on his side in an orange shirt and purple trousers (`nigga/sprite9.raw`, 274 × 147, cut off at the right and bottom) | ChatGPT, 23 November 2026, first attempt, in the same conversation | RMS 15.3, detail correlation 0.922 |
| `sprite4.png` | a man with an afro and white sunglasses in a white T-shirt with a red and black print, seen from above (`nigga/sprite4.raw`, 170 × 168) | as `sprite9.png` | RMS 16.4, detail correlation 0.944 |
| `sprite1.png` | a collage: a man with an afro and dark glasses looking up, a black-and-white face under a flowered bucket hat, and a man with an afro in a green jacket over a T-shirt printed with a face (`nigga/sprite1.raw`, 164 × 156, cut off at the left and bottom) | as `sprite9.png` | RMS 20.7, detail correlation 0.961 |

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

### The photos

The sprites parts draw four digitised photos of people over their backgrounds, unscaled and cut out on index 0. They
were first kept as the original pixels, as photos of real people; at my request they are redrawn as photos. Each
upload has the photo on a flat blue key (none of the four uses pure blue; their own colours reach at most 79 along
the key's direction, so `align_art.py` counts everything up to 100 as opaque, and takes an edge pixel as a mix of the
key and the colour of the opaque pixels around it, which leaves no blue fringe). The four went up in one
conversation, each with the prompt below adapted to its size, contents and cut edges. The fits are not colour
matched: matching the redraw's colours to the original's 256 made the white clothes blotchy, and the redraws' own
colours are close. A photo redraw has more detail than the original can show, so the faces, the hair and the prints
are the generator's reading of a few pixels: the T-shirt print in `sprite4` reads "05" over the lettering, where the
original has an orange and black patch. The original's cut-outs have a dark one-pixel edge; the redraws have clean
edges. The room parts' shadows stay computed from the original's shadow maps and still fall where the redrawn people
are.

Prompt for `sprite2`: "This is a 172x200 pixel digitised photograph from a 1996 MS-DOS demo, Inside by CNCD, in 256
colours, cut out along the person's outline and shown on a flat pure blue background, scaled up with
nearest-neighbour and shown at 4:3 (every pixel 1.2 times as tall as wide). It shows a man in a white shirt reaching
his open hand towards the camera, seen from below, with his bearded face and a white and green cap to the right.
Make a faithful high-resolution version as an image: a sharp, detailed photograph of exactly this picture, with the
same composition and framing, every part at the same position and size in the frame, the same pose, proportions,
hand and fingers, the same face and expression, the same clothes, colours and lighting, and the same cut-out
outline. The photo is cut off by straight edges at the left and at the bottom: keep those straight edges exactly
where they are, with the blue beyond them, and do not complete the body. Only the resolution and the detail change:
no pixels, no colour bands. Do not add, remove or move anything, do not crop or zoom. Keep the flat pure blue
background around the cut-out. An image of the same shape as the upload, as large as you can make it."

## Kept as the original

| picture | why |
|---|---|
| the faces in the credits' names | photos of real people in drawn pictures: only the drawn star and name are redrawn |
| the intro's picture (`intro/intro.raw`) | a photo collage with a nude, so it was not sent to ChatGPT; it waits for an open model (Comfy Cloud's free runs are used up) |
| the water part's background (`water/water.raw`, 320 × 200) | a blurred, blue-tinted photo of an interior (no people), drawn as it is and shifted by the ripple. Two ChatGPT attempts on 23 November 2026, from the nearest-neighbour upload with a prompt that asked for the same picture with the same soft blur, without the pixels and the 143-colour bands: the first fitted well (RMS 7.1, detail correlation 0.930) but came out softer than the original, the second (asked to keep every edge as defined as the original's) fitted worse (RMS 8.2, 0.909); both darkened the left side. Neither is faithful enough, and the original pixels read filtered already look as soft as the photo is |
| the textures (the tunnel, flower, twirl, flight, water, temple and sprites textures, the sun and the lens flares) | read by the effects through texture mapping, shade, fog and blend tables, or warped (the twirl): not drawn as they are, so the remake reads them filtered from the original |
