# DOPE — reverse-engineering notes and tools

Everything learned from `DOPE.EXE`, and the tools that learned it. DOPE.EXE is a chain of executables: a loader
(Tran's PMODE + a 32-bit GUS ProTracker player + CDPI, a 16-bit chain loader) and seven real-mode parts appended to
it, each packed with Rob Northen's ProPack. Addresses are `SEG:OFF` in an unpacked image (`unpacked/<name>.bin`,
relocated for load segment 0, so the file offset is SEG*16 + OFF) or, for the loader's 32-bit code, offsets in
PMODE's code32 segment (file offset 0x17e0 of `unpacked/loader.bin`).

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: file layout, the RNC-1 packer (exact format), PMODE, the loader's main program, the player and the GUS, CDPI, the parts' common structure, video modes, timing, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every part with its frames, time, music order:row, routines, frame rate and what is on screen |
| [BRIEF.md](BRIEF.md) | the shared brief for the decompiling agents, with the scopes table |
| `notes-<scope>.md` | written by the decompilers (see BRIEF.md) |

## Tools

You need Python 3 with `unicorn` (2.x), `capstone`, `numpy` and `pillow` and the original `DOPE.EXE` and
`DOPE.MOD` in [`../original/`](../original/). On the maintainer's machine they run inside the toolbox
(`toolbox run -c acme303 python3 ...`). Run them from this folder, in this order:

```
python3 unrnc.py ../original/DOPE.EXE              # split + unpack: unpacked/{loader,p1..p7,term}.{bin,exe,txt}, info.txt
                                                   # (runs every packer stub in unicorn AND a Python RNC-1 decoder,
                                                   #  checks both give the same image and both CRCs; ~40 s)
python3 rep.py p2 func 371e                        # capstone helper, per image: func / dis / calls / xref / funcs /
python3 rep.py loader32 func 4040                  #   far / str / ints / ports (views: loader, cdpi, loader32, p1..p7)
python3 run_dope.py --out full --snap-parts        # the whole demo headless: full.frames/.ticks/.gus/.dram and
                                                   #   snaps/part_<p>.snap where each part starts (about 40 minutes)
python3 capture.py --out full                      # optional, after one run with --snap-parts: re-captures every part
                                                   #   from its snapshot in parallel (about 10 minutes)
python3 timeline.py full.ticks                     # per-part table: frames, times, music, drawn fps (PARTS.md)
python3 sheet.py full.frames overview.jpg --every 4 --cols 8 --scale 0.5    # contact sheet (or list frame numbers)
python3 frames2mp4.py full.frames dope_reference.mp4   # 60 fps reference video (ffmpeg; from a toolbox without
                                                       #   ffmpeg it runs the host's through flatpak-spawn)
python3 run_dope.py --load snaps/part_p3.snap --out x --stop-part p4  # one part, from its snapshot
```

`run_dope.py --help` lists the options (`--mips` speed, `--frames`, `--snap-at`, `--trace`, `--debug`). Runs are
deterministic: a run resumed from a snapshot reproduces the uninterrupted run bit for bit.

The generated files (`unpacked/`, `full.*`, `snaps/`, `*.mp4`, `overview.jpg`) are ignored by Git. They are made
from the original and stay on your machine.

## Agent scripts

`models/<scope>/` holds the Python models and verification scripts of the decompiling agents, one folder per scope,
each with a README (see BRIEF.md).
