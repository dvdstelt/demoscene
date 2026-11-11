# JIZZ — reverse-engineering notes and tools

Everything learned from `JIZZ.EXE`, and the tools that learned it. Addresses are offsets in the unpacked program
image (`jizz_img.bin`), which is relocated for base 0, so they are also the program's near pointers.
[`../docs/making-of.md`](../docs/making-of.md) tells the story.

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: file layout, the DOS32 loader, VESA, MXMPlay and the timer, the main program, the sync table, the dump modes, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every part with its ticks, music positions, sync events, routines and frame rate |
| [notes-core.md](notes-core.md) | buffers, precalc tables, the random generator, the frame loop, colour tables and the inversion, fades, glow lines |
| [notes-textures.md](notes-textures.md) | the texture generator: a byte-code interpreter with 39 ops, its 16 scripts and two RLE pictures |
| [notes-engine.md](notes-engine.md) | Nix's vector engine: object scripts, camera paths, the renderer and its pixel formula |
| [notes-music.md](notes-music.md) | the sample synthesiser, the MXM module, MXMPlay and the Gravis UltraSound |
| [notes-parts2d.md](notes-parts2d.md), [notes-parts3d-a.md](notes-parts3d-a.md), [notes-parts3d-b.md](notes-parts3d-b.md) | the ten parts: what they draw, pseudo-C with every constant, the time base of every variable, the quirks to keep |
| [BRIEF.md](BRIEF.md), [PORT.md](PORT.md) | the briefs the decompiling and the porting agents worked from |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (`pip install unicorn capstone numpy pillow`)
and the original `JIZZ.EXE` in [`../original/`](../original/). Run them from this folder:

```
python3 unadam.py ../original/JIZZ.EXE jizz         # unpack: jizz_img.bin, jizz_unp.txt
python3 rep.py funcs                                # capstone helper: func / dis / calls / xref / funcs / str / ints
python3 run_jizz.py --out full --snap-at 430,440,1365,2515,3285,4050,4435,5205,5970,7506,9045 --snap snaps/t{tick}.snap
                                                    # run the intro headless: full.frames, .ticks, .flips, .gus, .dram
python3 frames2mp4.py full.frames jizz_reference.mp4  # watch the capture (needs ffmpeg)
python3 sheet.py full.frames sheet.jpg 1000 3000 8000 # contact sheet of chosen ticks
python3 run_jizz.py --out d --ticks 440 --cmdline " /pandora"   # the intro's own texture dump (dump/*.tga)
python3 run_jizz.py --out d --ticks 440 --cmdline " /mp4"       # and its music dump (dump/smokeout.mxm)
```

`unadam.py` runs the DOS32 kernel's own loader code in unicorn and checks it against a Python reimplementation.
The full run executes the whole intro instruction by instruction and takes about 10 minutes. `--load --snap file`
resumes from a snapshot, `--freeze-timer` keeps the music timer still, and `--part-budget ADDR=N` changes the CPU
speed inside one part.

The generated files (`jizz_img.bin`, `full.*`, `snaps/`, `dump/`, ...) are ignored by Git. They are made from the
original and stay on your machine. The remake unpacks the same image natively and checks it by hash; its
[`tools/compare.py`](../remake/tools/compare.py) reads `full.frames` from this folder.

## Agent scripts

[`models/`](models/) holds the Python models and verification scripts the decompiling agents wrote, one folder per
scope, each with a README. They reproduce the original's tables, textures, module, GUS register stream and frames,
and are kept as a record of how each finding was checked.
