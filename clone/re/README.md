# clone meets clone — reverse-engineering notes and tools

Everything learned from `CLONE.EXE`, and the tools that learned it. Addresses are linear addresses of the running
program: the unpacked image (`unpacked/clone_img.bin`) is relocated for base 0x400000, where the headless run loads
it, so address A is at offset A − 0x400000 of the file.

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: file layout, the "veloader" and its packed program (format for a C++ unpacker), PMODE/W as the intro uses it, VESA (32-bit pages, the 15-bit path), MXMPlay and the timer, main's flow, the time base (dt), the sync table and its events, pictures and textures, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every part with its ticks, music time and positions, events, routines, frame rate, what is on screen, what depends on the frame rate |
| [BRIEF.md](BRIEF.md) | the brief for the decompiling agents, with the scopes table |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (in the toolbox: `toolbox run -c acme303 ...`),
ffmpeg for the video (the host's, reached through `flatpak-spawn --host` from the toolbox), and the original
`CLONE.EXE` in [`../original/`](../original/). Run them from this folder:

```
python3 unpack.py ../original/CLONE.EXE                 # unpacked/clone_img.bin, clone_unp.txt, veloader.bin
python3 rep.py func 0x4001ab                            # capstone helper: func / dis / calls / xref / funcs / str / ints / ports
python3 run_clone.py --out full --snap-parts > full.log # run the intro headless: full.frames, .ticks, .flips, .gus,
                                                        # .dram and snaps/t<tick>.snap at every part start (~17 min)
python3 frames2mp4.py full.frames clone_reference.mp4   # watch the capture (60 fps, from the music start)
python3 sheet.py full.frames overview.jpg --every 2 --cols 8 --scale 0.5   # one frame every 2 s of music time
python3 sheet.py full.frames sheet.jpg 1000 3000 8000   # frames at chosen ticks
python3 run_clone.py --load --snap snaps/t6156.snap --out x --ticks 6556 --ips 55.5e6   # part 5 on a slower CPU
python3 fpscheck.py full x --tick0b 6156                # compare at equal timer values: frame-rate dependence
```

`unpack.py` runs the veloader's own code in unicorn (at two load addresses, to see every relocated dword) and checks
it against a Python reimplementation of its decompressor and fixups. The full run executes the whole intro
instruction by instruction at 150 M instructions per second of emulated time and takes about 17 minutes (one
core). `--load --snap file` resumes from a snapshot, `--freeze-timer` keeps the music timer still (dt = 0),
`--ips` / `--part-ips PART=N` change the CPU speed, `--flip-frames` records every shown frame instead of one per
timer tick, `--bpp 15` offers only the 15-bit video mode.

The generated files (`unpacked/`, `full.*`, `snaps/`, `*.mp4`, `overview.jpg`) are ignored by Git. They are made
from the original and stay on your machine.
