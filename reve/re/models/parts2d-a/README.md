# Models of scope "parts2d-a" (script entries 0, 5, 6, 7, 9, 10, 12, 13, 15)

Python models that reproduce the capture (`reve/re/full.frames`) from the original's algorithms, as described in
`reve/re/notes-parts2d-a.md`. Run them inside the toolbox from this folder:
`toolbox run -c acme303 python3 <script> ...`. They read the image (`../../unpacked/reve_img.bin` through `rep.py`),
the archive's files (`../../unpacked/files/`), snapshots (`../../snaps/`) and the capture; they write nothing into
the repository except `pictures.py` (into `../../dump/pictures/`, ignored by Git).

| script | what it checks | result |
|---|---|---|
| `common.py` | helpers: RAW pictures, snapshots, the capture | |
| `rates.py` | drawn frames per refresh in each entry (`full.ticks`) | one frame per refresh in every entry |
| `dac.py` | the DAC of every record against the palette the entry's init loads | constant in every entry |
| `title.py` | entry 0: REVE.RAW through the sprite routine 0x25f0be | records 32..651: all exact |
| `negflash.py` | entries 5, 6: the flash counter of 0x204b2c, NEGATIF.PAL | records 2737..3192: all exact (pixels and DAC) |
| `roto.py` | the rotozoom layers 0x25a69c / 0x25a73c and their four-call cycle (used by `wizroto.py`, `spiral.py`) | |
| `unrolled.py` | the structure of the four unrolled variants between 0x25a7dd and 0x25eae9 | 160 texels, 80 dword stores each |
| `layers.py` | which rotozoom layer each pixel class shows in entry 7 | layer 1 on 25 %, layer 2 on 75 % |
| `wizroto.py` | entry 7: both layers and the zooming WIZ.RAW (`--start`: from the image's state) | records 3194..4088: all exact |
| `twirl.py` | entries 9, 10, 15: the ring lists and the ring rotation | 6331..6776, 6777..7223 exact; 15: 10810..11701 exact on the ring pixels from the start, 11201..11701 exact on all pixels from `f11200.snap` |
| `twirl_continuous.py` | the continuous form of the twirl (notes 5.5) against the exact model | same shapes; per-pixel agreement about 50 % (the original tears along the rings) |
| `spiral.py` | entries 12, 13: layer 1, the feedback zoom through SPIRBLUR.PAL, the 30 dots (`--start`: from the image's state and entry 7's end) | records 8123..9910: all exact |
| `inplace.py` | the feedback blend in place (as the original) against double-buffered | after 120 frames 9 % of the pixels differ, not visibly |
| `blendtables.py` | SPIRBLUR.PAL = 0x25a275's table for TEXTURE.RAW's palette | 65,536 of 65,536 entries |
| `pictures.py` | writes the pictures drawn as they are to `../../dump/pictures/` | REVE, PULSE, WIZ, PLAYMO, BOUCHE, SPIRALE-dot, TEXTURE-tile5, TEXTURE-6010, TEXTURE-5010 |

Typical calls:

```
python3 title.py
python3 negflash.py
python3 wizroto.py --start --to 4088
python3 twirl.py 9 ; python3 twirl.py 10 ; python3 twirl.py 15 ; python3 twirl.py 15 --snap snaps/f11200.snap
python3 spiral.py --start --quiet
python3 twirl_continuous.py --refresh 6531 --out /tmp/x.png
python3 inplace.py /tmp/y.png
```
