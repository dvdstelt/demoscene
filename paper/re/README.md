# Paper — reverse-engineering notes and tools

Everything learned from `PAPER.EXE`, and the tools that learned it. Addresses are offsets in the intro's 32-bit
segment (code32), which starts at offset 0xd00 of the unpacked image. [`../docs/making-of.md`](../docs/making-of.md)
tells the story.

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: file layout, PMODE, the main program, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every part with its ticks, music counter and routines |
| [notes-init.md](notes-init.md) | start-up and shared code: tables, the random generator, objects, the palette system, the 3D engine |
| [notes-music.md](notes-music.md) | the tracker player and the Gravis UltraSound |
| [notes-title.md](notes-title.md), [notes-sketch.md](notes-sketch.md), [notes-scene3d.md](notes-scene3d.md), [notes-logo.md](notes-logo.md), [notes-planes.md](notes-planes.md), [notes-water.md](notes-water.md) | one per part: what it draws, pseudo-C with every constant, the time base of every variable, the quirks to keep |
| [BRIEF.md](BRIEF.md) | the brief the decompiling agents worked from |
| [REMAKE_PLAN.md](REMAKE_PLAN.md) | the plan the remake was built from |
| `funcs.txt`, `main.s` | the function list and a short listing of the start-up code |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (`pip install unicorn capstone numpy pillow`)
and the original `PAPER.EXE` in [`../original/`](../original/). Run them from this folder:

```
python3 unpklite.py ../original/PAPER.EXE paper_unp # unpack: paper_unp.mem (memory from the PSP), paper_unp.txt
python3 -c "open('paper_img.bin','wb').write(open('paper_unp.mem','rb').read()[0x100:0x100+0x269e0])"
                                                    # the program image the other tools read
python3 rep.py funcs                                # capstone helper: func / dis / calls / xref / funcs
python3 run_paper.py --out full --ticks 12300 --plane-budget 750000
                                                    # run the intro headless: full.frames, full.gus, full.dram, full.ticks
python3 player_ref.py --check . --wav full.wav      # music reference: checks full.gus / full.dram / full.ticks, renders audio
python3 frames2mp4.py full.frames paper_reference.mp4 --audio full.wav   # watch the capture (needs ffmpeg)
python3 sheet.py full.frames sheet.jpg 2000 4500 9000                    # contact sheet of chosen ticks
python3 compare.py out.mp4 --tick0 8700 --from 8800 --to 11000 A.frames:a B.frames:b
```

The run takes a while: it executes the whole intro, about 12,000 ticks of 70 Hz, instruction by instruction.
`--snap-at T --snap file` saves snapshots, and `--load --snap file` resumes from one. `--plane-budget 750000` slows
the PC down during the flock part so it draws about 34 frames per second. Without it, the intro skips the part.

The generated files (`paper_img.bin`, `*.frames`, `*.gus`, ...) are ignored by Git. They are made from the
original and stay on your machine.

`unpklite.py` prints the original entry point (1000:0316). The remake unpacks the same image natively and checks it
by hash. The remake's [`tools/compare.py`](../remake/tools/compare.py) reads `full.frames` from this folder.

## Agent scripts

- [`models/`](models/) holds the Python models and verification scripts the decompiling agents wrote, one folder per
  scope.
- [`port-checks/`](port-checks/) holds the scripts the porting agents used to check their parts against the capture.

They are kept as a record of how each finding was checked. See the README in each folder.
