# Inside (CNCD, The Gathering 1996) — reverse-engineering notes

Code Wili, Jouni and Juliet; graphics Destop (additional graphics Santtu); music Yolk and Legend ("eyegabo0om",
`EYE.AMF`). MS-DOS, Watcom C/C++ 32-bit under PMODE/W v1.22 (Thomas Pytel / Daredevil and Tran), VGA mode 13h
(320x200, 256 colours), music by DSMI (Otto Chrons, Virtual Visions: AMP 2.00 and a loadable sound driver; the
capture uses the Gravis UltraSound driver `SDIGUS.DSD`), 3D by Hybrid's SurRender ("SurRender P5 version 1.02
(Apr 01 1996)"). Won the PC demo competition at The Gathering 1996.

## Files and tools

All tools run inside the toolbox (`toolbox run -c acme303 python3 ...`) from this folder. Generated files are made
from the original and stay out of Git.

| file | what it does |
|---|---|
| `unpack.py ../original/INSIDE.EXE` | runs PMODE/W's stub and its PMW1 decompressor in unicorn, checks a Python decompressor against it on all six streams, applies the fixups (two loads at different bases cover every relocated dword). Writes `unpacked/inside_img.bin`, `unpacked/inside_unp.txt`, `unpacked/pmodew.bin` |
| `datfile.py ../original/INSIDE.DAT [--extract DIR]` | unpacks INSIDE.DAT and lists its 93 files (`import datfile; datfile.Dat(path).get(name)`) |
| `rep.py` | capstone helper on `inside_img.bin`: `func / dis / calls / xref / funcs / str / ints / ports` |
| `run_inside.py --out full` | runs the whole demo headless in unicorn (see "Running the original headless") |
| `sheet.py full.frames out.jpg ...` | contact sheets (`--every S`, `--pos 3:0 5:32`, or frame numbers) |
| `frames2mp4.py full.frames out.mp4` | the capture as a 70.086 fps video |
| `NOTES.md`, `PARTS.md`, `BRIEF.md` | these notes; the part timeline; the decompilers' brief |
| generated (not in Git) | `unpacked/`, `full.frames/.ticks/.flips/.gus/.dram/.log`, `snaps/f*.snap`, `*.mp4`, `*.jpg` |

Addresses are linear addresses of the running program: object 1 (code) at 0x400000, object 2 (data) at 0x420000,
as `run_inside.py` loads them; address A is at offset A − 0x400000 of `inside_img.bin`.

`inside_img.bin`: 763,552 bytes (0x400000..0x4ba6a0: object 1, 0x1e8be bytes; zero; object 2, 0x9a6a0 bytes, of
which 0x3c45 are initialised data, the rest BSS and stack). The image up to the end of the initialised data
(0x400000..0x423c45, 146,501 bytes) has FNV-1a 64 `717804c4954ab9f3`; the remake checks that.

## Unpacking (INSIDE.EXE, 113,134 bytes)

| file offset | size | what |
|---|---|---|
| 0x0000 | 0x2730 | MZ stub = PMODE/W v1.22 (self-compressed; its entry 0x5e copies itself and expands with the same kind of decompressor as below). Banner at 0x55. e_lfanew (0x3c) = 0x2730 |
| 0x2730 | 0x28 | "PMW1" header |
| 0x2758 | 0x30 | object table (2 × 0x18) |
| 0x2788 | 0x569a | fixup table (compressed blocks) |
| 0x7e22 | 0x1250a | object 1, compressed |
| 0x1a32c | 0x16c2 | object 2, compressed (ends at the end of the file) |

PMW1 header (offsets from 0x2730): +0 'PMW1', +4 version word 0x1601 (1.22), +6 flags 1 (compressed), +8 entry
object 1, +0xc entry offset 0xaa48, +0x10 stack object 2, +0x14 ESP 0x9a6a0, +0x18 object table offset 0x28, +0x1c
objects 2, +0x20 fixup table offset 0x58, +0x24 data offset 0x56f2.

Object table entries (6 dwords): virtual size, compressed size, flags (LE-style: 0x2045 code, 0x2043 data), offset
of its fixups in the fixup table, number of fixup blocks, uncompressed data size (the rest up to the virtual size
is zero). Object 1: 0x1e8be, 0x1250a, 0x2045, 0, 3, 0x1e8be. Object 2: 0x9a6a0, 0x16c2, 0x2043, 0x52ab, 1, 0x3c45.

The extender's loader (in the expanded stub, which runs as a 16-bit protected-mode segment with base = kernel +
0x1b00; offsets below are of `unpacked/pmodew.bin`): 0x20fb reads the header, 0x24f1 per object allocates the
virtual size, 0x1e1c decompresses, 0x220b..0x22cd applies the fixups, 0x2634 one fixup.

**Decompressor (0x1e1c).** A byte stream with interleaved bit groups: the first byte is a literal. Then, per item,
one bit (bits come MSB first from little-endian dwords, read from the stream when the previous 32 bits are used up):
- 0: copy one literal byte.
- 1: read a byte `lo`, then a bit:
  - 1: a long match. A bit: if 1, a bit b and then 2·(1+b) bits (MSB first) give `hi`, else `hi` = 0. Length:
    up to four bits, the first 1 among them at position k gives 3 + k (3..6); after four zeros a bit: 1 = the next
    byte + 15 (15..270), 0 = three bits + 7 (7..14). Distance (hi << 8 | lo) + 1.
  - 0, then a bit: 1 = three bits `hi`, length 2, distance (hi << 8 | lo) + 1; 0 = length 2 at distance `lo`, and
    `lo` = 0 ends the stream.
- Copies run byte by byte (overlap allowed).

**Fixups.** Per object, `blocks` blocks from fixup table offset `fix_idx`: word compressed size, word unpacked size,
then a compressed stream of 10-byte records: byte type (bit 5 = error; low nibble = LE source type), dword source
offset in the object (negative = skipped: the part of a fixup that starts on the previous page), byte target object
(1-based), dword target offset. INSIDE.EXE has type 7 (32-bit offset: target object base + offset; 4,515) and type 2
(16-bit selector: the flat code or data selector of the target object; 2, both data: at 0x40d8df and 0x40f712).

## INSIDE.DAT (1,349,919 bytes)

`datfile.py`. Loaded by 0x4000e8 (fopen, reads the first dword = unpacked size 2,721,868, the rest into a buffer,
allocates size + 0x28 "whole data buffer", decompresses with 0x4011c0 / 0x4011d6). LZSS: a flag byte gives 8 items,
bit 0 first; bit 1 = a literal byte; bit 0 = two bytes b0 b1: distance (b1 & 0xf0) << 4 | b0, length (b1 & 15) + 3.
The block ends with a dword N = 93 and before it N directory entries of 0x30 bytes: the name (zero-terminated, '/'
separators) and at +0x24 the offset in the block. 0x4001c4 keeps the block at [0x424894], N at [0x42489c] and the
directory at [0x4248b0]. 0x40022c finds a name (after turning '\' into '/'; the first entry with the exact name, so
of the duplicates `temple/putki.sro` (3×) and `flower/kukka1.sro` (2×) only the first is ever used) and returns a
pointer into the block; 0x400614 is the front end every part calls (EAX = name → EAX = pointer; with
[0x422780] = 0 it would read loose files instead and log their names to `inside.lst`).

Files (name, offset, size): see `python3 datfile.py ../original/INSIDE.DAT`. Kinds: `.raw` 8-bit pixels (320×200 =
64,000, 256×256 = 65,536 textures, 256×1024 = 262,144, sprites with a header), `.pal` 768-byte 6-bit palettes,
`.shd` shade tables (16,384 = 64 × 256 or 65,536 = 256 × 256), `.int` 65,536-byte tables (blend/intensity), `.qnt`,
`.sdw`, `.fog`, `.sro` SurRender objects, `.lws` a LightWave scene (text).

## Runtime model

- Flat model: CS and DS/SS are 4 GB selectors with base 0. Entry 0x40aa48 is Watcom's start-up (cstart): int 21h
  30h, the DOS/4G check (int 21h AX=FF00 answered with AL ≠ 0, GS = 0: extender type 1), int 31h 0006, heap
  through DPMI 0501 (0x418fa1), clib init, main 0x400010.
- FPU: control word 0x127F in main-line code (round to nearest, **53-bit** precision); inside the timer interrupt
  (tick routines, palette steps, music) 0x037F (64-bit), because the handler's `fnsave` resets the FPU. Float → int
  conversions call 0x40ba78 (Watcom's __CHP: `frndint` with rounding = truncate) before `fistp`, so they truncate
  toward zero (notes-core.md section 10).
- main 0x400010: 0x4007c0 (init, precalc of every part), 0x400cb4 (the main loop), then the exit (0x400a10..).
- Memory: 0x40b4d8 (EAX = size, EDX = name) allocates through Watcom's malloc and keeps a named list (tag
  [0x4b6ec8] from 0x40b3c0 / 0x40b4d0, used to free a part's memory: 0x40b6fc frees every block with a tag).
- Random numbers: 0x4134cd is Watcom's rand(): seed = seed × 0x41c64e6d + 0x3039, returns (seed >> 16) & 0x7fff;
  0x4134f1 srand(EAX). The seed is the dword at 0x423818 (initially 1; 0x4134c7 returns its address); srand is called
  once, srand(0x45eafc) in the flight init. Who draws how many numbers: notes-core.md section 9.

## Main flow

0x4007c0 (init): prints "INSIDE by CNCD - please see file 'readme.txt' for more information", checks free memory
(5,500 kB), 0x4001c4 loads INSIDE.DAT, 0x40a83c sets the screen geometry (320 × 200, clip window), allocates the
two screen buffers ([0x4248d0], [0x4248cc], 64,000 bytes each; the draw target [0x4b6ec0]), the palettes (base
[0x424890], current [0x4248a4], 768 bytes; float palette [0x4248b8] and adders [0x424884], 768 floats) and the IRQ
stack; 0x400d00 (debug font, off), 0x40c034, 0x40c6e4 (keyboard handler, IRQ1), 0x40bd48(4) (the retrace timer,
below), int 10h AX=13h, then the precalc of every part in this order: 0x403770 intro, 0x402f9c logo, 0x406d10
credits, 0x402948 flight, 0x404464 temple, 0x404fe8 / 0x40506c / 0x405110 / 0x4051d4 the four sprite pictures,
0x407dec water, 0x409730 flower, 0x405298 the shadow scene, 0x409ea0 twirl; 0x4010b4 starts the music (DSMI:
driver from SOUND.CFG, EYE.AMF, play), 0x401010 tells DSMI's timer the interrupt period, and [0x4b8f04] = 0x40070c
(the timer callback).

0x400cb4 (main loop): until [0x422788] (quit) or Esc ([0x4b9029]): call the part's frame routine [0x424888] (if
any), then call 0x400b3c until it returns 0 (each call runs at most one script entry, so every due entry runs before
the next frame), then the screenshot key ([0x4b9047]: 0x400c40 writes `shot%u.tga`).

**The script** (0x400b3c, table 0x4222e4, 12-byte entries {dword order, dword row, dword routine}): the music
position pos = order × 64 + row (0x40de09 / 0x40de21: DSMI's current order [0x422a5d] and row [0x422a61], −1 when
not playing); while pos ≥ entry.order × 64 + entry.row, the entry's routine runs and the index [0x4248bc] advances.
An entry with routine 0 (1000:0) ends the demo. Entries are checked after every drawn frame, so an entry runs at
the first frame end after the music reached its position. The entries are listed in PARTS.md. Note 30:12 comes
after 30:48 in the table, so it runs right after it.

Script routines set the part's routines with 0x400af4 (tick routine [0x4248b4]) and 0x400b04 (frame routine
[0x424888]); both reset the part tick counter [0x4248c8] to 0.

## Clock: the retrace timer (0x40bd48 .. 0x40bfdf)

0x40bd48(4) measures the screen refresh: PIT channel 0 in mode 3 with count 0 started at a retrace, latched at the
next one, twice until two measurements agree within 4; P2 = the count difference (mode 3 counts by 2: P2 = 2 × the
frame period in PIT clocks). It programs [0x4b8f00] = (P2 × 960 / 1000) / 4 / 2 = 0.24 × the frame period (4,085
PIT clocks for 70.086 Hz), PIT mode 0 (one shot), and installs the IRQ0 handler 0x40becf.

The handler (0x40becf): switches to its own stack (first entry only), saves the FPU; [0x4b8f10] counts the
interrupts of a frame: on the 4th it waits for the start of the vertical retrace (port 0x3da bit 3) and reprograms
the one shot from there, otherwise it reprograms it at once. So there are 4 interrupts per screen refresh, the
first right at the retrace, then every 4,085 PIT clocks; the last interval is the rest of the frame. Then it calls
[0x4b8f04] (the callback 0x40070c), restores, and chains to the old BIOS handler when its 16-bit sum of periods
overflows.

The callback 0x40070c (every interrupt, 280 per second; skipped entirely while [0x42278c] is set, which the credits'
frame routine does around 0x4061fc):
- [0x4248c4] += 1 (**M, the music clock**: callbacks since the music start, never reset; the temple, sprites and water
  read it), [0x4248c8] += 1 (**P**, the part tick counter, reset by 0x400af4 / 0x400b04; the intro and flight read
  it);
- 0x400fcc: the music (DSMI's timer: time += [0x4b9144] = 35 (0x401010: trunc(4,085 × 2,500 × 3.48092453e−6)),
  then AMP's poll 0x40d878 runs the player ticks that are due);
- only on the retrace interrupt ([0x4b8f10] = 0), when not reentered and when the busy counter [0x4248c0] is 0:
  [0x424880] += 1.0 (the frame clock, float, 70.086 Hz; nothing reads it), 0x400540 (one step of a palette fade),
  the screen copy if a frame is ready ([0x422984] = 1: 0x400694 copies [0x4248cc] to VGA memory, [0x42488c] += 1.0,
  [0x422984] = 0), then the part's tick routine [0x4248b4].

notes-core.md section 1 has the exact model: record k follows the retrace callback M = 4k − 150; song row R starts in
callback floor((1200 + 768 R) / 35) + 1 (three times slower in order 36); every script entry with its callback and
the first record that shows its effect.

So everything in the demo runs on the screen refresh: the parts' tick routines at 70.086 Hz, the music player on
DSMI time that advances 35 units per interrupt, 140 per refresh. A finished frame is shown at the next refresh. The
main loop draws as fast as it can into the back buffer and flips with 0x400b14 (swap [0x4248cc] / [0x4248d0], the
draw target [0x4b6ec0] = the new back buffer, [0x422984] = 1); the flip never waits, so a frame drawn twice within
one refresh replaces the first, but the sprites' and the flower's frame routines wait for the copy themselves.

## Palette (0x400364 .. 0x400612)

The base palette [0x424890] (768 bytes, 6 bit), the current one [0x4248a4] (what the DAC shows), the float
palette [0x4248b8] and per-entry adders [0x424884]; [0x4248ac] = steps left, [0x422790] = the level of the fade.
- 0x4004ac(level (float), n): adders = (base × level − current) / n, float palette = current, [0x4248ac] = n
  (n ≤ 0 → 1).
- 0x400540 (per refresh, in the callback): while steps are left: float += adder, clamp to 0..63, current =
  trunc(float), written to the DAC (0x3c8 / 0x3c9).
- 0x400364(EAX = palette): base = min(palette, 63) and a fade with the previous level over [0x4248ac] steps.
- 0x400498: white flash: current = min(base + 63, 63) (all white), fade to base × 1.0 in 15 refreshes.
- 0x400454: inverse flash: current = (63 − base) & 63, fade to base in 20 refreshes.
- 0x400438: fade to black (level 0) in 40 refreshes.
- 0x4003a8(EAX = n): current = (63 − base) & 63, fade to base in n; 0x4003ec(EAX = add, EDX = n): current =
  clamp(base + add, 0, 63), fade to base in n.

## Screen

Two 320×200 8-bit buffers; parts draw into [0x4b6ec0]. 0x40a83c(width, height, x0, y0 | x1, y1) sets the screen
geometry: [0x4b6ec4] = 320 (pitch), [0x4b6eac] = 200, the clip window [0x4b6eb8], [0x4b6ebc], [0x4b6eb0],
[0x4b6eb4]; 0x400694 copies the clip window's rows to VGA memory [0x42297c] (0xa0000). Init sets the full screen and
nothing changes it, so every copy is the whole 320 × 200.

## Running the original headless (run_inside.py)

No DOS, no PMODE/W: the image is loaded flat (object 1 at 0x400000, object 2 at 0x420000), started at 0x40aa48 with
ESP 0x4ba6a0, and PMODE/W's services are answered where the program calls them (int 21h: version, the DOS/4G check,
set / get vector, files from `../original` (INSIDE.DAT, SOUND.CFG, SDIGUS.DSD, EYE.AMF), IOCTL; int 31h: 0006, 0100,
0500, 0501; int 10h: mode 13h). The program loads DSMI's GUS driver from SDIGUS.DSD itself and calls into it, so
the GUS sees exactly what the original writes. Faked hardware: VGA (mode 13h at 0xa0000, the DAC, the input status
0x3da with the retrace at 70.086 Hz: 2 of 449 lines; a read that repeats at the same place with the same value is
a busy wait, so time jumps to the next change), the PIT (modes 0, 2, 3, latch), the PIC mask, the 8237 DMA
controllers and a Gravis UltraSound at 0x220 (from SOUND.CFG; registers, 1 MB DRAM, DMA uploads with their IRQ 11).
Main-line code runs at 150 M instructions per second (a fast Pentium, as the README asks for); interrupt handlers
run instantly except for their retrace wait.

Outputs: `full.frames` (per refresh, after its retrace callback, at its first displayed line: VGA memory and the
DAC; record k = frame k, written at program time (k + 1) / 70.086 s),
`full.ticks` (per refresh: time, music order and row, the frame clock [0x424880], the part tick counter, the script
index, the frame and tick routines, frames shown, timer interrupts, main EIP), `full.flips` (every copy to the
screen), `full.gus` (GUS writes stamped with the number of timer interrupts), `full.dram`, `full.log`.
Snapshots `snaps/f<frame>.snap` at the first refresh after the frame routine changes (pickle: 'mem' = 64 MB from
linear 0, 'ctx', 'st', 'gus', 'dma', 'eip'); `--load --snap` resumes from one.

## Parts

See PARTS.md for the timeline. Code by part (init routine, then the tick / frame routines the script sets):

| part | init | routines | data (INSIDE.DAT) |
|---|---|---|---|
| intro | 0x403770 | 0:00 0x4038c8 → tick 0x403214, frame 0x403748; 3:00 0x4031a0 | intro/intro.raw, .pal, .shd |
| logo | 0x402f9c (0x402ca0, 0x402d0c) | 3:63 0x403164 → tick 0x402e30, frame 0x402f1c; 4:32 0x402f10 | logo/* |
| credits | 0x406d10 (0x40684c, 0x406c70) | 5:63 0x4073c8 → tick 0x405f30, frame 0x406794; 0x405f1c, 0x405f10 | credits/* |
| sprite pictures and the shadow scene | 0x404fe8, 0x40506c, 0x405110, 0x4051d4, 0x405298 | 11:63 0x4057bc → tick 0x404738, frame 0x4047a0; 0x404680, 0x405ba0, 0x405f00, 0x404728; 27:48 0x40585c, 29:24 0x40571c | nigga/* |
| flower | 0x409730 (0x408030) | 14:00 0x4098c4 → tick 0x40940c, frame 0x4095f8; 0x409358, 0x4093f0 | flower/* |
| flight | 0x402948 (0x401330, 0x40136c, 0x401450, 0x4018d0, 0x4023d4) | 19:63 0x402c28 → tick 0x4021f8, frame 0x4028e4 | flight/* |
| water | 0x407dec (0x4079f4) | 23:63 0x407f90 → tick 0x407c78, frame 0x407d78; 27:48 0x408014 ends it | water/* |
| twirl | 0x409ea0 (0x40a29c, 0x40a408) | 29:63 0x40a678; 0x40a098 | flower/jeesus1.raw, .pal |
| temple | 0x404464 (0x403900, 0x4039c8) | 31:63 0x40461c → tick 0x40407c, frame 0x404424 | temple/* |

The code between parts: 0x40a7c4.. interrupt helpers, 0x40aa48 Watcom start-up, 0x40b10c.. memory, 0x40bd48..
the retrace timer, 0x40bff2.. printf, 0x40c034 / 0x40c6e4 keyboard; 0x40c788..~0x410500 DSMI (AMP 2.00, its MCP /
CDI layer, the timer service 0x40f5a1.., the driver loader "DDS" 0x40eae0.. / 0x40ef10, the AMF loader 0x4103d0);
0x410520..~0x417f00 SurRender; 0x417fb4.. Watcom's C library.
