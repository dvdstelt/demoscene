# Vivid Experiment: reverse-engineering notes and tools

Everything learned from `VIVID.EXE`, `EXPRMNT1.XM` and `EXPRMNT2.XM`, and the tools that learned it. Addresses are
linear addresses of the running program: `unpacked/vivid_img.bin` holds object 1 (code and the file archives) at
0x400000, object 2 at 0x810000 and object 3 (data, BSS, stack) at 0x820000, where the headless run loads them, so
address A is at offset A - 0x400000 of the file.

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: unpacking (PMODE/W's PMW1 format), the file archives, main's flow, the clock, the script and its commands, the code map, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every section with its refreshes, music positions, script lines and what is on screen |
| [BRIEF.md](BRIEF.md) | the brief for the decompiling agents, with the scopes table |
| [PORT.md](PORT.md) | the brief for the porting agents |
| `notes-*.md` | one per scope: core, music, bitmap, bump, water, generic |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (in the toolbox: `toolbox run -c acme303 ...`),
ffmpeg for the video (the host's, reached through `flatpak-spawn --host` from the toolbox), and the original files
in [`../original/`](../original/): `VIVID.EXE`, `EXPRMNT1.XM` and `EXPRMNT2.XM`. Run them from this folder:

```
python3 unpack.py ../original/VIVID.EXE                # unpacked/vivid_img.bin, vivid_unp.txt, pmodew.bin
python3 files.py                                       # the files in the three archives
python3 files.py --extract unpacked/files              # ... extracted to unpacked/files/1, 2 and 3
python3 rep.py func 0x40b760                           # capstone helper: func / dis / calls / xref / funcs / str / ints / ports
python3 run_vivid.py --out full --snap-parts > full.log    # run the demo headless: full.frames, .ticks, .script,
                                                       # .gus, .dram and snaps/f<frame>.snap at every effect (~2 h)
python3 run_vivid.py --out part2 --args 2 --snap-parts --snapdir snaps2 > part2.log   # part II on its own
python3 frames2mp4.py full.frames vivid_reference.mp4  # watch the capture (70.086 fps, from the script start)
python3 sheet.py full.frames overview.jpg --every 4 --cols 8 --scale 0.5   # one frame every 4 s
python3 sheet.py full.frames sheet.jpg 5000 9000 14465 # frames at chosen refreshes (or --pos 3:0 5:32)
python3 run_vivid.py --load --snap snaps/f5992.snap --out x --frames 6300   # resume at the first bitmap effect
```

`unpack.py` runs PMODE/W's own code in unicorn: the stub's self-decompression in real mode, then the extender's PMW1
decompressor on all nine compressed streams, and checks an independent Python decompressor against it; the fixups
are applied as the extender does, at two different bases, to check that every relocated dword is covered. The full
run executes the demo instruction by instruction at 100 M instructions per second of emulated time;
`--load --snap file` resumes from a snapshot, `--ips` changes the CPU speed, `--args 2` passes the demo's own switch
that starts at part II.

The generated files (`unpacked/`, `full.*`, `part2.*`, `snaps/`, `snaps2/`, `*.mp4`, `*.jpg`) are ignored by Git.
They are made from the original and stay on your machine.
