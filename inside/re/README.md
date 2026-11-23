# Inside — reverse-engineering notes and tools

Everything learned from `INSIDE.EXE`, `INSIDE.DAT` and `EYE.AMF`, and the tools that learned it. Addresses are
linear addresses of the running program: `unpacked/inside_img.bin` holds object 1 (code) at 0x400000 and object 2
(data, BSS, stack) at 0x420000, where the headless run loads them, so address A is at offset A − 0x400000 of the file.

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: unpacking (PMODE/W's PMW1 format), INSIDE.DAT, the runtime model, main's flow, the script table, the clock (the retrace timer), the palette, the screen, how the headless run works, the code map |
| [PARTS.md](PARTS.md) | the timeline: every part with its refreshes, music positions, script entries, frame rate and what is on screen |
| [BRIEF.md](BRIEF.md) | the brief for the decompiling agents, with the scopes table |
| `notes-*.md` | one per scope: core, music, engine, intro-logo, credits, sprites, flower, flight, water, twirl, temple |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (in the toolbox: `toolbox run -c acme303 ...`),
ffmpeg for the video (the host's, reached through `flatpak-spawn --host` from the toolbox), and the original files
in [`../original/`](../original/): `INSIDE.EXE`, `INSIDE.DAT`, `EYE.AMF`, and for the headless run also
`SOUND.CFG` and `SDIGUS.DSD` from the release. Run them from this folder:

```
python3 unpack.py ../original/INSIDE.EXE                 # unpacked/inside_img.bin, inside_unp.txt, pmodew.bin
python3 datfile.py ../original/INSIDE.DAT                # the 93 files in INSIDE.DAT (--extract DIR writes them)
python3 rep.py func 0x400cb4                             # capstone helper: func / dis / calls / xref / funcs / str / ints / ports
python3 run_inside.py --out full --snap-parts > full.log # run the demo headless: full.frames, .ticks, .flips, .gus,
                                                         # .dram and snaps/f<frame>.snap at every part (~25 min)
python3 frames2mp4.py full.frames inside_reference.mp4   # watch the capture (70.086 fps, from the music start)
python3 sheet.py full.frames overview.jpg --every 3 --cols 8 --scale 0.5   # one frame every 3 s of music time
python3 sheet.py full.frames sheet.jpg 1446 2148 4255    # frames at chosen refreshes (or --pos 3:63 5:63)
python3 run_inside.py --load --snap snaps/f1446.snap --out x --frames 1600   # resume at the logo part
```

`unpack.py` runs PMODE/W's own code in unicorn: the stub's self-decompression in real mode, then the extender's PMW1
decompressor (a 16-bit routine with 32-bit addressing) on all six compressed streams, and checks an independent
Python decompressor against it; the fixups are applied as the extender does, at two different bases to check that
every relocated dword is covered. The full run executes the demo instruction by instruction at 150 M instructions
per second of emulated time; `--load --snap file` resumes from a snapshot, `--ips` changes the CPU speed.

The generated files (`unpacked/`, `full.*`, `snaps/`, `*.mp4`, `*.jpg`) are ignored by Git. They are made from the
original and stay on your machine.
