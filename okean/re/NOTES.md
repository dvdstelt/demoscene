# The Sea Robot of Love (Orange, The Party 1995) — reverse-engineering notes

*The Sea Robot of Love* is Orange's 64k intro for The Party 1995 (3rd in the PC 64k intro competition). Credits from
its INFOFILE: programming hoplite and dune, sound effects and music dune, font and graphics der piipo, PMODE by Tran,
the player by Phantom, the 50 Hz mode by liket. The release is `okean.zip` on scene.org; the program is `KORSO2.EXE`.

Addresses below are **code32 offsets**: offsets in PMODE's 32-bit segment, the program's own near pointers (file
offset in `unpacked/korso2.bin` = 0xec0 + offset; linear address in the headless run = 0x8fc0 + offset).

## Files and tools

| file | what |
|---|---|
| `unpack.py` | unpacks KORSO2.EXE (RNC ProPack method 1) twice, by running the packer's own stub in unicorn and with a Python decoder, and checks both give the same image: `unpacked/korso2.bin`, `.exe`, `.txt` |
| `rep.py` | capstone helper: `rep.py code32 func ADDR`, `dis`, `calls`, `xref`, `funcs`, `str`, `ints`, `ports`; view `pmode` for the 16-bit kernel |
| `run_okean.py` | runs the intro headless (below) and records the capture `full.*` and the snapshots `snaps/part_<entry>.snap` |
| `sheet.py`, `frames2mp4.py` | contact sheets and a video of a capture |
| [PARTS.md](PARTS.md) | the timeline |
| [BRIEF.md](BRIEF.md) | the brief for the decompiling agents, with the scopes |
| `notes-*.md` | one per scope |

## Unpacking (KORSO2.EXE, 65,358 bytes)

One MZ executable, packed with Rob Northen's ProPack (method 1, the DOS executable variant): the load module starts
with the 18-byte RNC header (`RNC\x01`, unpacked 345,399 bytes, packed 64,563, CRCs 0xa577 / 0xad43, 30 chunks) and
the packer's 16-bit stub sits at the end (entry CS:IP 0fc5:000c of the packed file). It is the same format and stub
as DOPE's parts, so `unpack.py` is `dope/re/unrnc.py` for one executable. The stub decompresses to the load segment,
applies 20 relocations (all in PMODE's 16-bit kernel) and jumps to 0000:03d9 with SS:SP = 5454:1c00.

sha256 of `korso2.bin`: `d484339fc87d3566e677b2bb65bddf8e8c6d860d02e81d5ae1715a274bd0fa2e`.

## Layout of the unpacked image

- 0x0000..0x0ec0: Tran's PMODE 3.x 16-bit kernel (its error strings: "386 or better not detected!!!", ...). Entry
  0000:03d9. It computes code32a = (load + 0xec) * 16 and enters code32 at **0x259** with selectors 08h (code,
  base code32a), 10h (data, same base) and 18h (base 0, in `gs`).
- code32 0x000..0x429: PMODE's 32-bit part. Its variables at 0x00: `[0]` low heap pointer, `[4]` low heap top,
  `[8]` / `[0xc]` extended heap pointer / top, `[0x10]` PSP (linear), `[0x14]` code16a, `[0x18]` code32a,
  `[0x1c]`/`[0x1e]`/`[0x20]` the selectors 08h/10h/18h, `[0x28]` = 0xa6b get IRQ vector, `[0x2c]` = 0xa91 set IRQ
  vector (BL = IRQ, EDX = handler). Routines: 0x292 getmem (any), 0x2a1 getmem low, 0x2b9 getmem high (EAX bytes ->
  EAX pointer), 0x2fe IRQ mask (BL IRQ, AL 0 enable), 0x346 real-mode IRQ pass-up stub, 0x3f3 exit. The real-mode
  register image for `int 33h` (real-mode interrupt AL) is at 0xc4 (DPMI layout: EDI c4, ESI c8, ..., EAX e0).
  This PMODE build has its routines 0x4c bytes later and its variables 0x40 bytes later than DOPE's.
- code32 0x0ae0..~0x10000: **the music module**, a ProTracker MOD as a file: title "dune/orange", 8 channels
  (`8CHN`), 31 orders (0, 1, 1, 2, 2, 2, 1, 3, 3, 3, 3, 12, 5, 4, 5, 6, 7, 8, 7, 9, 10, 11, 10, 9, 10, 11, 10, 6, 7,
  8, 13), 14 patterns, 8 samples.
- code32 0x100c4: **main**. 0x10270..0x102fc the command-line check.
- code32 0x10414..0x14d1c: **the player** (notes-music.md): variables, the MOD loader 0x13f28, the tick 0x1343c,
  start 0x13eb1 and stop 0x13f18, the device table 0x142f0 (0 = none, 1 = GUS at 0x14310), the GUS driver
  0x1433d..0x14bab (init 0x14935, IRQ handler 0x1443c, detect 0x14b4b reads `ULTRASND=`).
- code32 0x14bec..0x14d1b: the retrace timer of the first part (below); every part has its own copy.
- the parts and their data, in the order main calls them (PARTS.md); the shared text printer 0x4cacc with its font
  (glyph widths 0x4c8c2, character map 0x4c861, pixels from 0x3cec2).
- Most tables are static in the file (for example the 4096-entry sine table at 0x24a3c that the title part uses);
  generated ones are built by the parts' own set-up code. Large zero areas in the file are the parts' buffers.

## Runtime model

Everything after PMODE's start-up is 32-bit code in code32 at ring 0, flat: `ds = es = fs = ss` = data (base
code32a), `gs` = zero base. Video memory is reached as `0xa0000 - [0x18]`. Buffers come from PMODE's getmem.
Interrupts: PMODE's set-IRQ-vector service installs protected-mode handlers; the timer (IRQ 0) is reprogrammed by
every part, the GUS IRQ (from `ULTRASND`) drives the player. Port 60h is read after every part for Esc.

## Main flow (0x100c4)

1. `sti`, PIC mask 02h (IRQ 1, the keyboard, off; Esc is read from port 60h), mode 13h via `int 33h` (int 10h
   AX=0013h).
2. If the command line has no `-M` / `-m` (0x102eb looks for `/M` or `-M`, both cases), **the 50 Hz mode**:
   CRTC 11h write protect off, misc output 63h, CRTC 00..17h = 60 4f 50 82 54 80 6f 3e 00 41 .. f6 88 8f 28 40 90 6c
   a3, sequencer 01 = 01, 03 = 00, 04 = 0e (and two word writes to port 3C3h that were meant for the graphics
   controller: GC 05 = 40h, 06 = 05h, mode 13h's values anyway). That is mode 13h with a horizontal total of 808
   dots (101 characters) and 625 lines (vertical total 0x26f + 2), 400 of them shown (200 doubled), vertical retrace
   at line 502 for 2 lines: **25.175 MHz / (808 x 625) = 49.851 Hz**. With `-M` the screen stays in mode 13h at
   70.086 Hz.
3. 0x142d7: detect a GUS (`ULTRASND=` in the environment: port, two DMA channels, IRQ). Found: 0x142f8 copies the
   GUS driver table 0x14310 to 0x12b44.., `call [0x12b48]` = 0x14935 initialises the card (16 voices) and the IRQ,
   0x13f28 loads the module at 0xae0 (samples to GUS memory). Not found: `[0x142d6]` = 1 and no music.
4. The parts, each called once, with an Esc check (port 60h = 1) after each: 0x38469 (the set-up of the fifth part's
   table generator), 0x2c26f, 0x2e779, 0x3554b, 0x367e5, 0x38520, 0x3c8da, 0x5334f, 0x4d370 (PARTS.md).
5. 0x13f18 stops the player and `call [0x12b4c]` = 0x148ca shuts the GUS down, mode 3, PIC mask 0, PMODE's exit.

The music starts inside the first part (0x2c26f calls 0x13eb1 in its set-up).

## The parts' common structure

Each part (example: 0x2c26f):

1. set-up: tables, a 64,000-byte draw buffer from getmem high (`push [8]` ... `pop [8]` frees it at the end);
2. **the retrace timer** (0x14bec .. 0x14cf3 for the first part; each part has a copy right before its own code):
   0x14bec installs the protected-mode IRQ 0 handler 0x14cb8 (and a real-mode stub 0x14cf4 in the IVT for IRQs that
   arrive in V86/real mode) and starts PIT channel 0 in mode 0 (one-shot); 0x14c67 measures one frame with the PIT
   (from one retrace start to the next) and loads that count minus 0xc0 (161 µs, about 5 lines early). The handler
   0x14cb8: if the retrace has already started, the stored count is lowered by 7, otherwise raised by 1 (so the
   interrupt keeps arriving just before the retrace); then it waits for the retrace, reloads the count, **calls the
   part's retrace callback** `[0x14be0]` (0x2c47b for the first part), sends EOI and returns. 0x14c40 restores the
   timer (mode 3, 18.2 Hz) and the vectors at the end.
3. the main loop: compute and draw the frame into the buffer, wait for the retrace start and its end (port 3DAh bit
   3), write the palette if it changed, copy the buffer to video memory (16,000 `movsd`), check whether the music
   has reached the next part's order (`[0x12b75]`), and Esc.

So the animation runs on the **retrace callback, 49.85 times per second** (counters advanced there), and the main
loop draws at most one frame per retrace. Part changes and events follow the music position: order `[0x12b75]` and
row `[0x12b73]` (a word; the player's row is `[0x12b72]`).

Without a GUS the callbacks run a fake music clock (title: 0x2c47b): every 4th retrace the row word `[0x12b73]`
counts up, and at row 64 (48 in orders other than 0) the order `[0x12b75]` counts up.

## Music (notes-music.md)

A ProTracker player with a GUS driver, string "oskari panningit" ("Oskari's pannings") in its data. 16 GUS voices
(register 0Eh = 0Fh: 38,587 Hz mixing). The player tick runs in the GUS IRQ from timer 2 (0x1443c: status bit 3 ->
`call [0x14385]` = 0x14591 -> the tick 0x1343c); at tempo 125 the timer count gives 62 x 320 µs = **19.84 ms per
tick** (50.40 Hz) instead of 20 ms. A new note ramps the old voice's volume down with the ramp IRQ enabled, and the
timer path **waits inside the interrupt** (`sti` after an early EOI) for the ramp interrupt to set a flag, so the
GUS interrupt nests. Position: order index `[0x12b75]`, row `[0x12b72]` (and the word `[0x12b73]` the parts read),
tick countdown `[0x12b71]`, speed `[0x1046e]`, tempo `[0x1046f]`, pattern `[0x12b78]`; 16 channel records of 0x40
bytes at 0x12b98.

## Running the original headless (run_okean.py)

- No PMODE kernel and no DOS: one unicorn CPU in 32-bit protected mode, 16 MB, GDT with 08h/10h at code32a = 0x8fc0
  (load segment 0810, PSP 0800, environment 07F0 with `ULTRASND=240,3,3,7,7`) and 18h at 0. PMODE's variables are
  preset (low heap after the program's stack, extended memory from 0x110000); the CPU starts at code32 0x259. int 33h
  (only int 10h mode sets are used), int 31h 090xh and the get/set IRQ vector services are emulated; the exit 0x3f3
  ends the run. The command line is empty (the 50 Hz mode) unless `--args=-M`.
- Interrupts are taken like the CPU does (EFLAGS, CS, EIP pushed, IF cleared) and the handlers run on the main line
  at the same speed; the PIC is modelled with request bits (rising edges: the PIT's OUT, the GUS IRQ line), in-service
  bits, priorities and EOI. This matters: the player's handler nests.
- Time: the main line runs `--mips` (default 100) million instructions per second in quanta; polling port 3DAh
  without progress fast-forwards to the next change of the retrace bits.
- Hardware models from `dope/re/run_dope.py`: the VGA (timing from the CRTC and misc registers), the PIT (modes 0, 2,
  3), the GUS (registers, 1 MB DRAM, timers, voice positions and volume ramps with their IRQs; a timing model, not a
  sound renderer). Keyboard: nothing pressed.
- Outputs per displayed frame (49.85 Hz): `full.frames` (header `OKEANFR1`, per frame <HHHH w, h, flags, 0>, the 768
  bytes of the DAC, 320x240 bytes of indices with the 320x200 picture top left), `full.ticks` (frame, time, order,
  row, tick countdown, music ticks, part, start address, start writes, timer IRQs, mode, EIP), `full.gus` (every GUS
  register write, stamped with the timer-2 ticks served, and an `irq` line per GUS interrupt), `full.dram`.
  Snapshots `snaps/part_<entry>.snap` where main calls each part (pickle: `mem` = 16 MB from linear 0, `pm` the CPU
  context, `st`, `vga`, `pit`, `gus`, `voices`).
- The whole intro takes about 5 minutes; the program exits at frame 9109 (182.74 s).
- Not modelled: sound, disk, the time of real video memory writes (the copy to video memory takes no visible time,
  so the capture has no tearing).

## The 70 Hz mode (`-M`)

A run with `--args=-M` (`video mode 13`, 70.086 Hz): the music and the part changes keep their times (the first part
ends at 12.47 s in both), but the animation, which advances once per retrace, runs 40 % faster. The 50 Hz mode is the
intended one; the remake follows it.
