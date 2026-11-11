# Reverse-engineering tools

The tools used to take the originals apart, and how to run them. They are written in Python 3, and some need
`capstone` (disassembler), `unicorn` (CPU emulator), `numpy` and `pillow`:

```
pip install capstone unicorn numpy pillow
```

All of them read the original files from `<demo>/original/` (see `scripts/fetch-originals.sh`).

## 303 (Watcom C/C++, DOS/4GW, LE executable)

| tool | what it does |
|---|---|
| [`303/remake/tools/le_load.py`](../303/remake/tools/le_load.py) | loads the LE executable like DOS/4GW: objects at their linear addresses (code at 0x10000, data at 0x50000), fixups applied |
| [`303/remake/tools/re303.py`](../303/remake/tools/re303.py) | capstone helper: `func ADDR` (recursive-descent listing, x87 constants annotated with their values), `dis ADDR N`, `calls ADDR`, `str NAME` (string address and the code that uses it) |
| [`303/remake/tools/lzw.py`](../303/remake/tools/lzw.py) | reads the PAK archives (LZW15V variant) |
| [`303/remake/tools/dumpvideo.cpp`](../303/remake/tools/dumpvideo.cpp) | decodes the `.LZW` video clips to images |
| [`303/remake/tools/rowtime.cpp`](../303/remake/tools/rowtime.cpp) | times the module's rows, to convert the music position counter to seconds |
| [`303/re/pos.py`](../303/re/pos.py) | music position counter → music time and capture time |
| [`303/re/compare.sh`](../303/re/compare.sh) | contact sheet: frames of a video capture (`CAPTURE=...`) next to the remake's frames at the same moments |

```
cd 303/remake/tools
python3 re303.py str tunnel.raw       # where the code loads a texture
python3 re303.py func 0x2d81c         # list a routine
```

## Paper (PKLITE, PMODE, hand-written 32-bit assembly)

| tool | what it does |
|---|---|
| [`paper/re/unpklite.py`](../paper/re/unpklite.py) | runs PKLITE's own decompression stub in unicorn (real mode, minimal DOS) and dumps memory at the original entry point |
| [`paper/re/rep.py`](../paper/re/rep.py) | capstone helper on the 32-bit segment: `func`, `dis`, `calls`, `xref`, `funcs` |
| [`paper/re/run_paper.py`](../paper/re/run_paper.py) | runs the whole intro headless in unicorn with fake VGA, timer, keyboard and Gravis UltraSound. Records per 70 Hz tick: screen, palette, GUS writes, key variables. Snapshots let you start at any part |
| [`paper/re/player_ref.py`](../paper/re/player_ref.py) | the intro's music player in Python, routine by routine, plus a GF1 model to render audio. Regenerates the captured GUS writes byte for byte |
| [`paper/re/gf1synth.py`](../paper/re/gf1synth.py) | an earlier, simpler GF1 renderer (has known bugs, see notes-music.md) |
| [`paper/re/frames2mp4.py`](../paper/re/frames2mp4.py), [`sheet.py`](../paper/re/sheet.py), [`compare.py`](../paper/re/compare.py) | turn a capture into a video, a contact sheet, or a side-by-side video |
| [`paper/remake/tools/compare.py`](../paper/remake/tools/compare.py) | the remake's frames next to the capture's, with an optional difference score |

[`paper/re/README.md`](../paper/re/README.md) lists the commands in order.

## JIZZ (DOS32, VESA 2.0 hicolor, hand-written 32-bit assembly)

| tool | what it does |
|---|---|
| [`jizz/re/unadam.py`](../jizz/re/unadam.py) | runs the DOS32 kernel's own loader routine (decompress, relocate, clear the BSS) in unicorn, twice at different bases, and checks a Python reimplementation against it |
| [`jizz/re/rep.py`](../jizz/re/rep.py) | capstone helper on the program image: `func`, `dis`, `calls`, `xref`, `funcs`, `str`, `ints` |
| [`jizz/re/run_jizz.py`](../jizz/re/run_jizz.py) | runs the whole intro headless in unicorn with fake DOS32 services, a VESA 2.0 BIOS with a linear frame buffer, the timer chip and a Gravis UltraSound. Records per timer tick: the visible page, the music position and sync event, the page flips, GUS writes. Snapshots, a frozen timer and per-part CPU speeds help find frame-rate dependent state |
| [`jizz/re/fpscheck.py`](../jizz/re/fpscheck.py) | compares two runs made at different CPU speeds at equal timer values |
| [`jizz/re/frames2mp4.py`](../jizz/re/frames2mp4.py), [`sheet.py`](../jizz/re/sheet.py) | turn a capture into a video or a contact sheet |
| [`jizz/re/models/`](../jizz/re/models/) | the agents' Python models: textures, the music synthesiser and MXMPlay, the engine, every part |
| [`jizz/remake/tools/compare.py`](../jizz/remake/tools/compare.py) | the remake's frames next to the capture's, with an optional difference score |

[`jizz/re/README.md`](../jizz/re/README.md) lists the commands in order.

## The emulator harness in short

`run_paper.py` doesn't emulate a PC. It emulates just enough for this one program:

- **Memory.** The 32-bit segment is loaded at linear address 0 with flat segment selectors, so the intro's own
  "linear base" variable is 0 and VGA memory is at 0xa0000.
- **The DOS extender.** PMODE's variables are preset. Its interrupt-vector service is intercepted, so the intro's
  timer and keyboard handlers are known. Its real-mode interrupt call (`int 0x33`) is emulated for the few
  services the intro uses (set the video mode, DOS print, exit).
- **Ports.** Reads and writes are hooked: VGA palette and retrace, the timer chip, the interrupt controller, the
  keyboard and the GUS (registers, DRAM and voices).
- **Time.** The timer interrupt is delivered every N instructions of main-line code. N sets the speed of the
  emulated PC, which matters for anything the intro does per drawn frame.

That is a few hundred lines of Python. It gives an exact, repeatable run of the original that can be stopped,
inspected and resumed at any tick. `run_jizz.py` does the same for JIZZ, with DOS32's services and a VESA BIOS
instead of PMODE and VGA.

## In the remakes

The remakes do their own loading at run time, in C++:

- `paper/remake/src/pklite.cpp` unpacks PAPER.EXE. Its output is byte-identical to the stub's.
- `paper/remake/src/exe.*` reads by address from the unpacked image.
- `303/remake/src/assets.*` reads the PAK archives, the LZW codec, the video codec and the meshes.
- `jizz/remake/src/adam.cpp` unpacks JIZZ.EXE like the DOS32 loader; `jizz/remake/src/exe.*` reads by address.
