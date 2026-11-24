# VIVID.EXE: overview

*Vivid Experiment* by Doomsday (Assembly '96, August 1996; the program calls itself "VIVID v1.68"). This file is the
map: how the program is stored, how it runs and where its parts are. The `notes-*.md` files have the routines.

Addresses are linear addresses of the running program (`unpacked/vivid_img.bin` from 0x400000, as `run_vivid.py`
loads it): address A is at offset A - 0x400000 of the file.

## Unpacking

`VIVID.EXE` (1,874,784 bytes) is PMODE/W 1.22 with the demo's Watcom C program in PMODE/W's own compressed "PMW1"
format, like Inside's `INSIDE.EXE`. `unpack.py` (Inside's, with other bases) runs the stub's self-decompression and
the extender's PMW1 decompressor in unicorn on all nine compressed streams and checks an independent Python
decompressor against it; the 8,154 offset fixups and 6 selector fixups are applied at two bases to check that every
relocated dword is found. The program has three objects:

| object | loaded at | size | contents |
|---|---|---|---|
| 1 | 0x400000 | 0x40a4ea | the code (to 0x42b2a8), then the three file archives (to 0x80a4ea) |
| 2 | 0x810000 | 0x57 | a table of names: `setup_files`, `demodata1` .. `demodataend3` |
| 3 | 0x820000 | 0x27fc0 | initialised data (0x5c3f bytes: strings, tables), then the BSS and the stack (top 0x847fc0) |

The image is 4,489,152 bytes, sha256 `fd8b545c51dea936a1d749a448579405826bb6f100d778144add708688156e5a`.

## The file archives

The demo's files are linked into object 1 as three archives (`files.py` lists and extracts them). Each archive is
a table of 128 entries of 40 bytes, `char name[32]; uint32 size; uint32 offset` (the offset counts from the start of
the table), ended by an entry named `eof.eof`; the files follow the table. A fourth file `demoN.dat` on disk
would replace archive N (main 0x40025b: 0x402200 loads `demo1.dat` .. `demo3.dat` if they exist); the release has
none.

| archive | table | files | used by |
|---|---|---|---|
| 1 | 0x42b2a8 | 90 | part I: `script/demo1.scr`, `pics/*.pcx` and `*.raw`, `efek/*.ini` / `*.prm`, `tabs/*.1x1` / `*.2x2`, `pal/*.pal` |
| 2 | 0x60528d | 58 | part II: `script/demo2.scr`, `water/`, `bump/`, `pics/`, `efek/`, `gen/`, `tabs/`, `pal/` |
| 3 | 0x7979fe | 11 | part II: `gen2/` (the second 3D scene), `pal/greets.pal`, `pal/phong2.pal` |

0x40241c looks a name up and returns a pointer to the file's data. File formats:

- `.pcx`: ZSoft PCX 3.0, 8 bits, RLE, with the 256-colour palette at the end (the parts use the `.pal` files).
- `.raw`: Image Alchemy "mhwanh" raw: a 32-byte header (`mhwanh`, version, width and height big-endian, palette
  size), a 768-byte palette (8 bits per component), then the pixels.
- `.pal`: 768 bytes, 6-bit VGA components.
- `.1x1` / `.2x2`: 64,000 / 16,000-byte tables (one byte per screen pixel or per 2 × 2 block) for the bitmap effects.
- `.ini` / `.prm`: text parameter files of the bitmap effects; `water.ini`, `gen.ini`: `key=value` files of the
  3D effects; `.scr`: the scripts (below); `.m3d`, `.spl`, `.val`, `.lit`: the 3D objects, spline paths, bump
  tables and light tables (notes-water.md, notes-generic.md, notes-bump.md).

## Main (0x4001a0)

1. Arguments: `vivid.exe m` asks for the sound card in a menu (MIDAS's setup screens); `vivid.exe 2` starts at part
   II ([0x825c44] = 1). Anything else stops with an error.
2. Prints "Use: 'vivid.exe m' for manual config!!!", sets up the archives, initialises MIDAS 0.5a (detects the GUS
   from `ULTRASND`), prints "MIDAS Sound System v0.5a (c) Sahara Surfers" and the device.
3. Measures the screen refresh (MIDAS `tmrGetScrSync`, 0x410ab6 → [0x825c60]) and starts MIDAS's timer synchronised
   to the screen (0x410d23 with the routines 0x40007c before the retrace and 0x403728 in the retrace).
4. Part I: reads `script/demo1.scr` (0x40b6e4), runs its `LoadXM` line (0x40b5a0: loads `EXPRMNT1.XM` from disk),
   sets mode 13h, starts the module (0x40d618), sets the whole palette to black (0x401fbc), runs the script
   (0x40b760), sets black again, stops and frees the module.
5. Part II: the same with `script/demo2.scr` and `EXPRMNT2.XM`.
6. Text mode, MIDAS closed, prints "VIVID v1.68 ... DOOMSDAY!" and "Minfree:%d".

## Clock

MIDAS's timer (0x410802, IRQ 0) runs in its screen-synchronised mode: it programs the PIT so that an interrupt
comes just before each vertical retrace, waits for the retrace inside the interrupt, and calls the demo's routines;
the music player runs in the same interrupt at the module's tempo. The demo's routine before the retrace (0x40007c)
adds 1 to the frame counter [0x825c5c]: **the demo's clock is the screen refresh, 70.086 Hz**. The routine in the
retrace (0x403728) calls [0x832504] if a part set it.

The music position: 0x400104 asks MIDAS for the play status and copies order position [0x825c48], pattern
[0x825c4c], row [0x825c50] and the sync value [0x825c54]. The main line calls it while it waits.

## The script

The scripts are text, one command per line (at most 512 lines of 128 characters, read to 0x834bac):

```
        POS PAT COMMAND                 arguments
        024 000 RunBITMAPEFEK           efek/1efek1.prm
        xxx xxx InitFADE                pal/plo2.pal   pal/white.pal  80 0 255
```

Lines starting with `#` are comments. POS is the order position, PAT the row (the column names are the scripts'
own). `xxx` means "no wait": the command runs at once. 0x40b760 runs the lines in order; each command waits in its
own loop until the music has reached POS and PAT. WaitSYNC (0x40279c) waits for each new frame counter value, steps
the palette fade by the frames that passed, reads the position, and stops waiting when **order >= POS and row >=
PAT** (both, so a target row is waited for in every order from POS on).

Commands (0x40b760; the handler addresses):

| command | handler | used | what it does |
|---|---|---|---|
| `LoadXM` / `LoadS3M` | 0x40b5a0 | yes | the module to play (main loads it) |
| `ScriptJMP` | in 0x40b760 | no | jump to line POS |
| `WaitSYNC` | 0x40279c | yes | wait for the position |
| `ClearSCR` | 0x402784 | yes | clear the VGA memory |
| `LoadPAL` | 0x402054 | yes | set a palette |
| `InitFADE` | 0x4020b8 | yes | start a fade from one palette to another over N frames, for entries first..last |
| `NegPAL` | 0x402000 | no | invert the palette |
| `InitPCX` / `DeinitPCX` | 0x401884 / .. | yes | decode a picture into slot N |
| `PutPCXSYNC` | .. | yes | copy a rectangle of a picture to the screen, then wait |
| `InitZOOM` / `RunZOOMSYNC` / `DeinitZOOM` | 0x402460 / 0x402644 / 0x402630 | yes | the credits: a zooming name over the sky |
| `InitBITMAPEFEK` / `RunBITMAPEFEK` / `DeinitBITMAPEFEK` / `ClearEFEKPARAMS` | 0x400ae4 / 0x401270 / 0x400ff0 / 0x401588 | yes | the table-driven "bitmap effects" (1 × 1 and 2 × 2) |
| `InitBUMP` / `RunBUMPEFEK` / `DeinitBUMP` | 0x402ec4 / 0x403434 / 0x40315c | yes | bump mapping |
| `WaterInit` / `WaterEffect` | 0x405bd0 / 0x4064b0 | yes | the water scene |
| `GenericInit` / `GenericEffect` | 0x408cec / 0x409d74 | yes | the 3D scenes (landscape, phong objects) |
| `SwapMOD_XM` / `SwapMOD_S3M` | in 0x40b760 | no | change the module |
| `TunnelEffect` | 0x404a64 | no | (left over) |
| `EarthEffect` | 0x406c08 | no | (left over) |
| `DogInit` / `DogEffect` | 0x403ab4 / 0x404220 | no | (left over) |

Part II's script names files that are in none of the archives (`gen2/gen.ini` is in archive 3, but
`pal/greets.pal` and `pal/phong2.pal` are too; check what happens to each "not found" in the capture).

## Code map

| range | what |
|---|---|
| 0x400000 .. 0x400ae4 | main, errors, picture/palette helpers of the bitmap effects |
| 0x400ae4 .. 0x401884 | bitmap effects: init, run, deinit, parameters |
| 0x401884 .. 0x402200 | pictures (PCX) and the palette (fades) |
| 0x402200 .. 0x402460 | the archives |
| 0x402460 .. 0x402784 | the zoom (credits) |
| 0x402784 .. 0x402948 | ClearSCR, WaitSYNC |
| 0x402948 .. 0x403738 | bump mapping |
| 0x403738 .. 0x40b5a0 | the 3D effects: Dog, Tunnel (0x404a64), Water (0x405bd0..), Earth (0x406c08), Generic (0x4074e4..0x409d74..) |
| 0x40b5a0 .. 0x40c22c | the script |
| 0x40c22c .. 0x40d170 | inner loops in assembler (the bitmap effects' 1 × 1 and 2 × 2 drawers from 0x40c3ad) |
| 0x40d170 .. 0x410f00 | MIDAS: the API, the setup menus, the timer (0x410740..) |
| 0x410f00 .. 0x42b2a8 | Watcom's run-time, the ini and spline readers (0x413390..), the 3D engine, the heap, MIDAS's sound devices (GUS 0x41b1a0.., SB 0x41dd00..) and the XM player |

## The headless run

`run_vivid.py` runs the program in unicorn without DOS or the extender (Inside's runner with Vivid's addresses):
fake VGA (DAC, retrace), timer chip, interrupt controller and a GUS at 0x220. The environment holds
`ULTRASND=220,1,1,11,11`; MIDAS finds the GUS and uses it without IRQ and DMA (it writes the samples into the GUS
memory through port 0x327). The CPU runs at 100 million instructions per second.
