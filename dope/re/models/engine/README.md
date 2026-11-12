# models/engine — checks for notes-engine.md

Run from `dope/re` inside the toolbox (`toolbox run -c acme303 python3 models/engine/<script> ...`).

| file | what it does |
|---|---|
| `engine_model.py` | bit-exact model of the 3D engine of p2, p3, p5, p6, p7 (object loop, matrix, vertex transform, face set-up, radix sort, both triangle fillers and span loops); `PARTS` holds each part's addresses and variants |
| `check_frame.py <part> dumps_<part>/fNNNNN [--png out.png]` | runs the model on the `pre` dump and compares vertex arrays, face list, sorted keys and the render target with the `drawn` dump |
| `check_copy_p2.py dumps_p2/fNNNNN` | p2's frame copy 360c (credit strip, blur blend, label, A000, clear) and the blend tables 1967 / 1998 |
| `check_copy.py <part> dumps_<part>/fNNNNN` | the frame copy of p3, p5, p6, p7 (label, A000, background) |
| `float_check.py <part> <pre dump>` | the float transform of notes section 11.1 against the integer engine |
| `cull_check.py <part> <pre dumps>` | how often the 16-bit facing test is wrong |
| `paths.py`, `objects.py`, `maps.py`, `meshes.py` | filler paths per frame, object lists, env maps / textures, the meshes of each part |
| `match_routines.py`, `diff_routines.py <part> p2addr:addr ... [--imm]`, `find_shld.py` | locating the engine copies in p3..p7 and listing their differences |
| `dumpview.py` | reads the dumps |
| `run_dope.py` | copy of `../../run_dope.py` with `--dump NAME=SEG:OFF,... --dump-frames F,... --dump-dir D --dump-stop` |

Making the dumps again (about 5-10 minutes each, they can run in parallel):

```
python3 models/engine/run_dope.py --load snaps/part_p2.snap --out models/engine/x_p2 \
    --dump pre=0F74:3737,drawn=0F74:3740,post=0F74:3743 --dump-frames 3400,5900,6600,7500,8700,9700 \
    --dump-dir models/engine/dumps_p2 --dump-stop
python3 models/engine/run_dope.py --load snaps/part_p3.snap --out models/engine/x_p3 \
    --dump pre=0F74:2BBD,mid=0F74:2BC3,drawn=0F74:2BC6,post=0F74:2BC9 --dump-frames 10500,12000,13300 \
    --dump-dir models/engine/dumps_p3 --dump-stop
# p5: pre=0F74:2B7F,mid=0F74:2B85,drawn=0F74:2B88,post=0F74:2B8B  frames 21600,22300,23400,24800
# p6: pre=0F74:2DFF,mid=0F74:2E6B,drawn=0F74:2E6E,post=0F74:2E71  frames 26000,27500,28900
# p7: pre=0F74:3089,mid=0F74:3169,drawn=0F74:316C,post=0F74:316F  frames 30000,30900,31400
```

Result (all 19 frames): 0 differing pixels in the target, 0 differences in every array, and the frame copies give
the captured screens exactly.

Generated from the original, not for Git: `dumps_*/` (memory dumps), `cmp_*.png`, `p2_*.png`, `x_*.log` and the
`x_*.frames/.ticks/.gus/.dram` a new run writes.
