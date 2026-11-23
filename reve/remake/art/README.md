# Redrawn pictures

High-resolution redraws of the pictures of *Reve* (Pulse, The Party 1995; final version 1996) that are drawn as they
are. The demo's graphics are credited to Antony and AJT; Hornet's DemoNews (issue 112) names Antony as the artist of
the still pictures. The remake shows these redraws by default; `--original-art` shows the original pixels, which it
reads from `REVE.EXE`. None of the pictures is a photo; all are drawn or rendered. The texture sheet the 3D parts and
the rotozooms read through tables has a 4× upscale instead (see "The upscaled texture").

These are fan redraws made for this non-commercial tribute and under the same non-commercial terms as the rest of
the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Pulse's work and not endorsed by them.

All were made with ChatGPT's image generation on 23 November 2026, from the nearest-neighbour uploads
(`tools/align_art.py --upload NAME`), in three conversations: the pictures, the lettering (the title and the names)
and the two textures. "Fit" is `tools/align_art.py`'s residual against the original (RMS of 0..255 per channel, and
the correlation of the fine detail) after the fit described in "How they were made".

| picture | shows | where | attempts | fit |
|---|---|---|---|---|
| `title.png` | REVE in four tall, narrow white letters spaced far apart, the first a mirrored R, a bar above the first E (`GFX\REVE.RAW`, 227 × 80, index 0 transparent) | the title (entry 0) and the closing title | third: the first two drew the mirrored R from straight diagonals and from round arcs; the third was asked for its straight strokes one by one | RMS 20.6, correlation 0.979 (per letter) |
| `credits_antony.png` | ANTONY in the same lettering, white with a black outline (a band of `GFX\CREDITS.RAW`, 231 × 64) | the credits over the first 3D scene (entries 1–4) | second: the first kept the pixel steps of the curves and diagonals | RMS 17.5, correlation 0.983 (per letter) |
| `credits_shad.png` | SHAD (151 × 64) | as ANTONY | third: the first request got no answer, the second kept the pixel steps; its strokes are a little bolder than the original's | RMS 38.6, correlation 0.871 (per letter) |
| `credits_lca.png` | LCA (120 × 64) | as ANTONY | second: the first copied the pixel staircase | RMS 21.6, correlation 0.974 (per letter) |
| `credits_ajt.png` | AJT (130 × 64) | as ANTONY | first; its stroke ends are round where the original's are square | RMS 22.9, correlation 0.959 (per letter) |
| `pulse.png` | a brown alien head with green goggle eyes and a grin over a large-block mosaic, PULSE in thin letters below (`GFX\PULSE.RAW`, 320 × 200) | the strobes of entries 5 and 6 | first; it came back scaled down in the frame, which the fit takes up | RMS 13.9, correlation 0.932 |
| `wiz.png` | a grey cartoon alien with a lumpy tilted head, two big eyes, three antennas, a red vest and hands on its hips (`GFX\WIZ.RAW`, 87 × 124, index 0 transparent) | pulsing over the rotozoom of entry 7 | first, on a magenta key; the antennas are lighter and the head and vest smoother than the original's | RMS 17.0, correlation 0.957 |
| `playmo.png` | a toy figure with orange hair and a blue suit lying on its side (`GFX\PLAYMO.RAW`, 320 × 200) | twirled ring by ring in entries 9 and 10 | first; placed smaller than the frame, which the fit takes up | RMS 12.1, correlation 0.884 |
| `bouche.png` | an open grey mouth with white teeth and a red tongue on a red stand (`GFX\BOUCHE.RAW`, 320 × 200) | twirled in entry 15 | first | RMS 8.5, correlation 0.900 |
| `junior.png` | "Junior:" in thin white hand lettering with the o as a blue eye, ".born to code" below, over a dark red splash (`GFX\JUNIOR.RAW`, 214 × 137) | entry 16 | first | RMS 15.0, correlation 0.985 |
| `mrtommy.png` | a cartoon head of a man with a long brown face, round glasses and a dark red cap, on a purple glow (`GFX\MRTOMMY.RAW`, 200 × 200) | entry 17 | second: the first ended with a tool error | RMS 7.1, correlation 0.962 |
| `dada.png` | a pink winged pony with brown spots rearing up (`GFX\DADA.RAW`, 256 × 256) | tiled on the planes of entries 18 and 19 | first, in the textures conversation | RMS 8.1, correlation 0.960 |
| `gene.png` | "Pulse" in tall red, white and purple lettering with curls, GENERATION below, over a red speckled pattern (`GFX\GENE.RAW`, 320 × 200) | the strobes of entries 20 and 21 | first | RMS 20.9, correlation 0.975 |
| `tom.png` | MRTOMMY's head as `GFX\TOM.RAW` holds it (256 × 256, the same drawing at (53, 2)) | warped in entry 23 | not redrawn on its own: cut from `mrtommy.png` with `--derive` | — |
| `boz.png` | a grey clown face with goggle eyes, red lips and a purple hat in a speckled cloud (`GFX\BOZ.RAW`, 256 × 256) | warped in entry 23 | first, in the textures conversation | RMS 5.9, correlation 0.978 |
| `dog.png` | "Biscuit Addicted" in white brush and block letters over a dark red sign (`GFX\DOG.RAW`, 187 × 124) | entry 24 | first | RMS 18.7, correlation 0.990 |
| `waaf.png` | a cartoon dog sitting in profile inside brown rings, "Waaf" in purple brush letters and ".the dog" (`GFX\MRWAAF.RAW`, 320 × 200) | entry 25 | first | RMS 11.6, correlation 0.937 |
| `bouge.png` | a puppet mouse with big ears, an orange collar and green trousers hanging from a red string (`GFX\BOUGE.RAW`, 320 × 200) | the strobes of entries 27 and 28 | first | RMS 9.7, correlation 0.950 |
| `yocol.png` | a cartoon boy with spiky pink hair in profile, on a dark red block mosaic framed in black (`GFX\YOCOL.RAW`, 320 × 200) | entry 29, the last picture | first | RMS 10.4, correlation 0.948 |

## How they were made

Each upload is the original picture through the palette the demo shows it with, scaled up with nearest-neighbour to
its shape on screen (a 320 × 200 picture becomes 1600 × 1200, 5 × 6 per pixel, as the screen is shown at 4:3; a
texture keeps square texels): `tools/align_art.py --upload NAME`. Pictures with a transparent index (the title, the
names and WIZ) go up on a flat magenta key, which `align_art.py` turns back into transparency. Related pictures went
up in one conversation so that they match in style, with the prompt of
[docs/redrawing-pictures.md](../../../docs/redrawing-pictures.md) adapted to each picture, for example YOCOL's:

"This is a 320x200 pixel picture from a 1995 MS-DOS demo, Reve by Pulse, in 256 colours, scaled up with
nearest-neighbour and shown at 4:3 (every pixel 1.2 times as tall as wide). It shows a cartoon boy with spiky pink
hair, seen from the side, with an orange face, a big ear and pink highlights, on a dark red background of large
squares, framed in black. Make a faithful high-resolution redraw as an image: the same composition, pose,
proportions and framing, every part at the same position and size in the frame, the same colours, the same
background with its squares and the black border around it. Only the resolution and the detail change: no pixels,
no colour bands. Do not add, remove or move anything, do not crop or zoom. A landscape 4:3 image, as large as you can
make it."

The block mosaics behind PULSE and YOCOL are large squares in the original as well; the redraws keep them.

`tools/align_art.py NAME RAW` fits a redraw onto the original (scale and offset) and writes it covering exactly the
original's frame; `--match` gives it the original's colour distribution where the generator made the colours lighter
or more saturated (all the pictures). The title and the names are fitted letter by letter (`--segments`): each
letter's own offset, after one fit for the whole, so that a letter the generator drew half a pixel off sits where the
original's is. `--sheet` puts the original and the fitted redraw side by side for checking.

### The lettering

The title and the names are the same lettering: tall, narrow, angular strokes. The title is white on black; the
names are white with a black outline (index 79) on index 0, drawn as sprites over the 3D. The first redraws of the
names kept the pixel steps of the curves and diagonals; at the maintainer's request every name was redrawn as clean
lettering, with this prompt (ANTONY's; the others name their own size and letters):

"That is exactly right, thank you. Now the ANTONY name again in that same clean style: a 231x64 pixel picture on the
flat magenta key, every pixel 1.2 times as tall as wide: an A, an N, a t, an o with a bar under it, an N and a Y,
spaced far apart, white strokes with an even black outline. Make a faithful high-resolution redraw as an image: every
letter at exactly the same position, size and shape in the frame, exactly the same text, spacing and stroke widths,
but as clean vector-like lettering like the last LCA image: smooth round curves, straight clean edges and clean
diagonals, solid white strokes with an even black outline, no visible pixels or steps anywhere, the magenta perfectly
flat. Do not add, remove or move anything, do not crop or zoom. An image of the same shape as the upload, as large as
you can make it."

## The upscaled texture

`GFX\TEXTURE.RAW` is a 256 × 256 sheet of hand-drawn 64 × 64 tiles: the 3D parts map them on their faces through
`GFX\GOURAUD.PAL`'s 16 shade levels, and the rotozoom of entry 7, the dot spirals of entries 12 and 13 and DADA's
planes wrap single tiles or 16 × 16 blocks of it. A redraw would move its pixels. It is upscaled 4× instead, with
Real-ESRGAN's `realesr-general-x4v3` model (BSD-3-Clause, <https://github.com/xinntao/Real-ESRGAN>), which keeps
every pixel where it is: `tools/upscale_art.py` decodes each tile through TEXTURE.RAW's palette (WIZ.RAW's is the
same, and DADA.RAW's matches it on the tile DADA uses), pads it with its own wrapped edges, upscales it on the CPU
and writes the sheet as `tex_texture.jpg`, 1024 × 1024. Made on 23 November 2026.

The tile at (0, 64) is not drawn: it holds 16 × 16 blocks of coloured noise, which the upscaler turns into blobs. It
is written as the original resized smoothly (bicubic, each block wrapped on its own), so it adds no detail.

The parts still compute every colour from the original's indices and tables and add only the upscale's detail:
colour + g × (upscale − the original's texels through the palette), where g is how much of a change in a texel's
colour the shade level passes on (1 for the plain tiles). The 2D parts read the tiles in place of the palette's
colours. `--original-art` turns it off.

## Kept as the original

| picture | why |
|---|---|
| `GFX\SPIRALE.RAW` | the dot of the spirals, 6 × 6 pixels: the remake draws it as an anti-aliased disc with its colours by radius (`src/scenes/dots.cpp`) |
| the noise blocks of `GFX\TEXTURE.RAW` | coloured noise, not a drawing (above) |
