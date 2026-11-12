# DOPE (Complex, The Gathering '95 PC demo) — reverse-engineering notes

Code jmagic, graphics and music jugi, boxes pixelled by reward, "media labs" logo reward; GUS replay "phantom/sonic
pc (gus unit raped by jmagic)" (DOPE.TXT; the player's own strings say "ProTracker for GUS replay routine,
Copyright (C) 1994 Oskari Tammelin"). MS-DOS, a PMODE 32-bit loader that keeps a ProTracker module playing on a
Gravis UltraSound while it runs seven real-mode VGA parts one after the other. DOPE.TXT: "a bit unfinished", the end
scroll is missing; needs 550 KB base memory, 600 KB extended memory, a GUS with 512 KB.

Addresses: `SEG:OFF` in an unpacked image `unpacked/<name>.bin` (relocated for load segment 0: file offset =
SEG*16 + OFF; all part code is in segment 0010), or plain hex offsets in the loader's 32-bit segment ("code32",
file offset 0x17e0 of `loader.bin`; these are the 32-bit code's near pointers). `rep.py <view>` uses the same.

## Files and tools

All tools run inside the toolbox (`toolbox run -c acme303 python3 ...`) from this folder; README.md has the commands.

| file | what it does |
|---|---|
| `unrnc.py ../original/DOPE.EXE` | splits DOPE.EXE into its 9 MZ executables and unpacks the 8 packed ones twice: by running each packer stub in unicorn and with a Python RNC-1 decoder; both must agree (they do, and both CRCs match). Writes `unpacked/<name>.bin` (load module relocated for segment 0), `<name>.exe` (plain MZ of the same program), `<name>.txt` (entry, stack, relocations, hashes), `info.txt` |
| `rep.py <view> <cmd>` | capstone helper: views `loader` (PMODE 16-bit kernel), `cdpi` (segment 00EB), `loader32` (code32), `p1`..`p7`; commands `func / dis / calls / xref / funcs / far / str / ints / ports` |
| `run_dope.py --out full --snap-parts` | runs the whole demo headless (see "Running the original headless") |
| `capture.py --out full` | re-captures every part from its snapshot in parallel and stitches the result |
| `sheet.py`, `frames2mp4.py`, `timeline.py` | contact sheets, 60 fps reference video, per-part summary of a `.ticks` file |
| generated (not in Git) | `unpacked/`, `full.frames/.ticks/.gus/.dram/.log`, `snaps/part_*.snap`, `dope_reference.mp4`, `overview.jpg` (one frame every 4 s) |

## File layout (DOPE.EXE, 584,133 bytes, sha256 0c54549d…6de97d0)

A chain of MZ executables, each starting where the previous one's MZ size (header bytes 2..5) ends:

| name | file offset | MZ size | entry CS:IP | SS:SP | unpacked bytes | what |
|---|---|---|---|---|---|---|
| loader | 0x00000 | 0x2c4a | 0000:03d4 | 7f9f:0100 | 25,570 | PMODE kernel + code32 (main, player) + CDPI |
| p1 | 0x02c4a | 0xca02 | 0010:0bd5 | 0000:0100 | 213,658 | "Complex Media Labs" logo scroller |
| p2 | 0x0f64c | 0xe5ae | 0010:376c | 0000:0100 | 449,862 | credits with 3D discs, the "C" logo, motion blur |
| p3 | 0x1dbfa | 0xd156 | 0010:2bec | 0000:0100 | 374,042 | "7800 PHONG polys" object over a nebula picture |
| p4 | 0x2ad50 | 0x2c1d2 | 0010:1caa | 0000:0100 | 485,072 | "FIRE clouds", "FRACTAL scape" (own code, not the engine; starts on p3's last screen) |
| p5 | 0x56f22 | 0x11e22 | 0010:3089 | 0000:0100 | 386,842 | "ENV mapping", "BUMP glow" over a chequerboard |
| p6 | 0x68d44 | 0x12e9e | 0010:38e8 | 0000:0100 | 446,560 | cat: "SHADOW volume", "material MORPH", "METAL shading" (reads the mouse if there is one) |
| p7 | 0x7bbe2 | 0x12dae | 0010:3194 | 0000:0100 | 457,550 | "multiple LIGHT sources", "WORLD domination", "dope"; sets text mode at the end |
| term | 0x8e990 | 0x35 | 0000:0000 | — | 21 (not packed) | prints "■ TERM" and exits with code 0xFF |

(Part contents: see PARTS.md.) DOPE.MOD (957,334 bytes): a ProTracker module, 31 instruments, tag "28CH" (28
channels), 124 orders, restart 0, 70 patterns (7168 bytes each), loaded at run time by the loader.

## RNC ProPack method 1 (all eight packed executables)

Each packed MZ has no relocations of its own; its load module is `RNC header + bit stream`, followed by the
packer's 16-bit stub at CS:0000 (CS in the MZ header, IP = 000C).

**Header** (18 bytes, big endian): `"RNC" 01`, unpacked size (u32), packed size (u32, bytes after the header),
CRC of the unpacked data (u16), CRC of the packed data (u16), leeway (u8, 2 / 0), chunk count (u8). CRC = CRC-16
with the reflected polynomial 0xA001, initial value 0 ("CRC-16/ARC"), table-driven, over the bytes.

**Bit stream**: read as 16-bit little-endian words, bits taken from the least significant end. The reader holds a
32-bit buffer `buf` with `n` valid bits and a byte position `p`: start `buf = word(18), n = 16, p = 18`. To take k
bits: `v = buf & ((1<<k)-1); buf >>= k; n -= k; if n < 16 { p += 2; buf |= word(p) << n; n += 16 }`. So the buffer
always holds, above the bits still to be used, the whole word at `p` (a look-ahead). First take 2 bits (lock and key
flags, both 0).

Per chunk (chunk count from the header):
1. Three Huffman tables: literal-run lengths, match distances, match lengths. Each: 5 bits N, then N times 4 bits:
   the code length of value 0..N-1 (0 = unused). Codes are canonical: for length L = 1..16, for value v = 0..N-1
   with that length, code = running counter (MSB first), counter += 1; after each L, counter <<= 1. Because the
   stream is read LSB first, a code of length L matches when `buf & ((1<<L)-1)` equals the code bit-reversed in L
   bits. (The stub, 0x2b4d, builds the same table with the reversal done by rcr/rcl.)
2. 16 bits: C, the number of (literal run, match) pairs in this chunk.
3. C times: read a value with the literal table; a value v decodes to v if v < 2, else to `(1 << (v-1)) | (v-1
   more bits)`. Copy that many bytes **from the byte stream at p** (the look-ahead word's position, not through the
   bit buffer), `p += count`, then replace the look-ahead: `n -= 16; buf &= (1<<n)-1; buf |= word(p) << n;
   n += 16`. Unless this was the C-th pair: distance = value(distance table) + 1, length = value(length table) + 2,
   copy byte by byte from `out - distance` (may overlap).

**Stub** (offsets from the stub's CS): header words `[0]` IP, `[2]` CS, `[4]` SP, `[6]` SS of the original program
(relative to the load segment), `[8]` stub length, `[0A]` packed-data length in paragraphs (= stub CS). The stub
(000C) copies itself to the MZ header's SS, moves the packed data up below it in 64 KB steps from the end, decodes
into the load segment (renormalising ES:DI / DS:SI after every chunk, 0x2a42), then applies its relocation table at
CS:028C — groups of `count (u8), segment (u16), count x offset delta (u8, the offset starts at 0 in each group)`,
ended by count 0; each word at segment:offset gets the load segment added — sets SS:SP and jumps to CS:IP. DOS
calls are not used. The decompressed image of a part is its plain real-mode program; `unpacked/<p>.exe` is it as an
ordinary MZ file (header rebuilt from the stub's values and table).

A C++ port needs: the chain walk, the header, the bit reader with its look-ahead / literal quirk, the three tables per
chunk, the relocations (only if it runs the code; the remake only reads data, so it can use the image relocated for
segment 0, as `unpacked/*.bin` is, and add segment bases itself), and the CRC check to identify the right file.

## The loader

### PMODE (image 0x0000..0x0ea0 16-bit, code32 = image 0x17e0..)

Tran's PMODE 3.x (the error strings are PMODE's: "386 or better not detected!!!", "System is already in V86 mode,
and no VCPI found!!!", ...). 16-bit entry 0000:03d4 detects the CPU, VCPI (int 67h DE00), DPMI (int 2Fh 1687), XMS
(int 2Fh 4300), enables A20, allocates its tables (0x1600 bytes) from the top of the program's memory, and enters
code32 at 0x20d with selectors 08h (code32, base code32a), 10h (data, same base), 18h (zero base, `gs`).
Variables at code32 0x00: `[00]` low-memory heap pointer, `[04]` low-memory top, `[08]`/`[0C]` extended-memory
heap pointer / top (XMS), `[10]` PSP (linear), `[14]` code16a, `[18]` code32a = (load + 0x17e) * 16, `[1C]`/`[1E]`/
`[20]` = 08h / 10h / 18h. Services used by the program:

| code32 | what |
|---|---|
| 0x246 / 0x255 / 0x26d | getmem (any / low / extended): EAX = bytes → EAX = code32 pointer, CF on failure |
| `[0x28]` = 0x9b5, `[0x2c]` = 0x9db | get / set protected-mode IRQ vector: BL = IRQ, EDX = offset |
| 0x2b2 | IRQ mask: BL = IRQ, AL = 0 enable / 1 disable (ports 21h / A1h) |
| 0x2fa | build a real-mode stub at EDI that passes IRQ BL up to protected mode (via int 32h) and put it in the IVT |
| int 33h | real-mode interrupt AL, registers in the DPMI-style image at code32 0x84 (EDI 84, ESI 88, EBP 8C, EBX 94, EDX 98, ECX 9C, EAX A0, flags A4, ES A6, DS A8, FS AA, GS AC) |
| int 32h | real-mode far call CX:DX with the same register image |
| int 31h 0900/0901 | disable / enable interrupts (virtual IF) |
| 0x355 | exit: restore int 1Bh / 32h vectors and the PIC masks, back to the 16-bit kernel (int 21h 4Ch) |

Main program (code32 0xa55): `[00]` = (load + 0x63f) * 16 (sic: a linear address used as a code32 offset — the
module's 64 KB file buffer lands load*16 bytes higher than intended, harmless); print "Initializing music replay...";
`call [0x1a7f]` = 0x45b4 GUS init; 0x2d15 load "dope.mod" (0xa4c) and upload the samples; int 10h AX=13h; 0x2c17
reset the 32 channel records and start the player (`call [0x1a77]` = 0x4369); `int 32h` to real mode at
load:0EA0 = `call far CDPI:0131` — **all parts run inside this call**; then 0x2cff stops the player (`[0x1a7b]` =
0x43d6, `[0x1a83]` = 0x4545) and 0x355 exits.

### The player (code32)

A ProTracker player with a GUS driver, all in code32. It is the only code that runs in protected mode while the
parts play; nothing in the parts reads the music position (there is no sync interface).

- Module loader 0x2d15: file through int 33h (0x4aa6 open, 0x4b27 read), 64 KB low buffer, header 0x43c bytes
  (title / instrument copy at 0xc24..; the tag decides the channel count `[0xc7b]`: "M.K." → 4, "xCHN", "xxCH" →
  digits), song length `[0xc7a]`, order table `[0xc7d]`; patterns to extended memory (getmem high); samples to GUS
  DRAM by programmed I/O (0x4450: register 43h/44h address, byte to 3x7, one byte at a time). Instrument records
  0x46 bytes at 0x11fd + (i-1)*0x46 (+0x40 DRAM start, +0x32 length, +0x36/+0x3a loop, +0x31 loop flags, +0x3e
  volume, +0x44 word: the divisor in the note → frequency step computation).
- State: `[0xc76]` speed, `[0xc77]` BPM, `[0x1a9b]` tick countdown, `[0x1a9c]` row, `[0x1aa0]` order (index into the
  order table), `[0x1aa2]` pattern, `[0x1aa9]` pattern delay, `[0x1aa7]` break row; 32 channel records of 0x3f bytes
  at 0x1ac0 (`+0x11` change flags, `+0x13` volume, `+0x21` instrument, `+0x28/+0x29` effect, `+0x2a/+0x30/+0x32`
  periods, `+4/+8/+0xc` DRAM start / loop start / end). Tick 0x22c4 (row parsing, then effects through the jump
  tables at 0x2579 (0xy) and 0x25c1 (Exy)). The song would loop to order 0 after order 0x7b; the demo stops it at
  order 0x6d (PARTS.md).
- GUS driver table at 0x1a77..0x1a93: start 0x4369, stop 0x43d6, init 0x45b4, 0x4545, upload 0x4450, 0x44a8, 0x44f8,
  panning 0x4587. Ports: `[0x3f1c]` base (from ULTRASND, 0x490a; without it an interactive menu 0x498b asks for port
  and IRQ), `[0x3f1e]` 3x2, `[0x3f20]` 3x3, `[0x3f22]` 3x4, `[0x3f24]` 3x7. GUS IRQ = 4th ULTRASND field (`[0x3f26]`).
  Delay 0x401e = 7 reads of port 300h. Active voices = channels (min 14): 28 → register 0Eh = 0xdb, 22.05 kHz.
- **Timing**: GUS timer 2 (320 µs units), not the PIT. The IRQ handler 0x4040 (IRQ 7 here, set with setirqvect)
  checks the GUS IRQ status (2x6): bit 3 (timer 2) → reload register 47h with `[0x3f2a]`, register 45h = 0 then 8,
  `sti`, `call [0x3f0c]` = 0x41e9: the tick 0x22c4, then the timer count for the current BPM
  (`[0x3f2a]` = -((1,000,000 / (BPM*32)) >> 2), remainder bits in `[0x3f29]`: BPM 125 → 62 counts = 19.84 ms; the
  module sets F09 / F6F at the start, BPM 111 → 70 counts = 22.4 ms, 44.6 Hz), then the voice updates (0x421a..).
  Bit 6 (volume-ramp IRQ) → 0x40d8: for each voice whose ramp ended (register 8Fh), stop it and program the next
  sample's addresses; a new note is started by ramping the old one down to volume 0x40 with the ramp IRQ enabled
  (0x423d..: start 0x40, end = current volume, control 0x60) and switching the sample in that IRQ.

### CDPI v0.2 (segment 00EB, image 0x0eb0..0x17e0; offsets below are CDPI offsets)

A 16-bit chain loader: it runs every executable appended to DOPE.EXE, in order, as a real-mode child, while PMODE
keeps the loader resident. Data: `[0000]` PSP segment, `[0002]` saved SS:SP, `[0006]` file offset of the next
executable, `[000A]` the one after, `[000E]` MZ header buffer, `[002E]` the child's memory block, `[0030]` its exit
code, `[042E]` a copy of the whole IVT, `[082E]` the program's path (from the environment).

1. 0131: save DS (PSP), find its own path (0185), copy the IVT to `[042E]`, shrink its DOS block to 0x753
   paragraphs (int 21h 4Ah), open DOPE.EXE and set `[0006]` = the loader's MZ size (01AB).
2. 020E, per executable: open, seek to `[0006]`, read the MZ header, `[000A]` = next; take the largest free DOS block
   (48h with BX=FFFF, then all of it) if it holds image + minalloc + 0x11 paragraphs; int 21h 26h makes a PSP there;
   read the image to PSP+10h in 32 KB pieces; apply the relocations; hook int 21h (IVT) to 03BD; save SS:SP; DS = ES
   = PSP, SS:SP and CS:IP from the header (`retf` at 03BC).
3. 03BD catches int 21h AH=4Ch (everything else goes to the old vector, `[042E]` + 0x84): 03F4 stores the exit code,
   restores SS:SP and int 21h, frees the block, `[0006]` = `[000A]`, and loads the next executable unless the code
   was 0xFF.
4. Exit code 0xFF ends the chain: TERM exits with it, and so does every part when a key is pressed (int 16h AH=1 in
   the frame loops, port 60h = ESC in p4). Then text mode, "CDPI v0.2, Disconnecting life support systems.", the IVT
   is restored from `[042E]`, `retf` back to protected mode.

Nothing is reset between parts: the video mode, the palette and VGA memory carry over (p4 starts by reading p3's
palette and screen), and so do the music and the GUS.

## The parts

Plain real-mode DOS programs (386 instructions, 32-bit registers and addressing, `fs` / `gs`); the only DOS call
is int 21h 4Ch. Load module layout: 0000:0000..00FF stack (SS:SP = load:0100), code and the program's own variables in
segment 0010 (`cs:` addressed), data in segments named by relocated immediates (`push 0x4527; pop es`) and far
pointer tables (0050:0000 in p2, p3, p7; 0010:0000 in p5; 0031:0000 in p6). p2, p3, p5, p6 and p7 are built on the same 3D engine library (the same
routines at different addresses, the "HELLO LAMER !!!" string); p1 (logo scroller) and p4 differ. Per-part routines
and timelines: PARTS.md.

### Timing: everything runs from the vertical retrace

Each part installs its own int 8 handler (the old vector is chained) and turns the PIT into a retrace-synchronised
one-shot timer:

1. calibrate (p2 0x1a5f, p1 00b9): wait for the retrace, PIT channel 0 mode 3 count 0, wait for the next retrace,
   latch: in mode 3 the counter falls by 2 per clock, so `-count` = 2 x the frame period P in PIT clocks; count = 2P *
   1000 / 2048 (p2, p4, p5, p6) or 2P * 990 / 2048 (p1, p3, p7), about 0.98 P / 0.97 P;
2. the handler (p2 0x1a21, p1 0020, p4 1420): EOI, wait for the start of vertical retrace (busy loop on 3DAh bit 3 inside the
   IRQ), PIT mode 0 (one-shot) with that count, `sti`, call the part's per-retrace routine (palette, CRTC start /
   panning, motion of objects, the retrace counter), add the count to an accumulator and chain to the old int 8 on
   each 16-bit overflow (keeps the BIOS clock at 18.2 Hz on average).

So the per-retrace routine runs once per displayed frame, at 70.09 Hz in all modes used (400-line timing). The
main loop of the engine parts is a "run N retraces" routine (p2 0x371e, p3 0x2ba4, p5 0x2b66, p6 0x2de6, p7 0x3070):
reset the retrace counter, then repeat {build the frame, wait for the per-retrace routine's "ready" flag, copy the
buffer to VGA, count the drawn frame, int 16h AH=1 (a key ends the demo)} while counter < N. A part's main script is a
list of such calls with N and parameters in between, so part lengths are fixed numbers of retraces: p2 7296
(104.1 s), p3 3605 (51.4 s), p5 4480 (63.9 s), p6 3990 (56.9 s), p7 2100 (30.0 s) (p1 and p4 count retraces in
their own loops: p1 2688), plus about one drawn frame of overshoot per call (the counter is tested after a frame is drawn), plus loading and set-up. Motion is advanced in
the per-retrace routine (time based); how much is drawn depends on the CPU (frame skipping).

The music is not synchronised to any of this: it runs on the GUS timer at its own tempo, and the parts follow it only
through their fixed lengths. On the original, the time to read each part from disk adds to the gaps between parts
(not emulated; see below).

### Video modes

| part | mode | registers |
|---|---|---|
| p1 | 320x200 unchained ("mode X"), 70 Hz | mode 13h, then SC4 = 06h (chain-4 off), CRTC 14h = 00h, 17h = E3h (byte mode), misc = E3h, CRTC 13h = A8h (virtual width 1344 pixels), line compare 183h (split at line 194: the bottom 6 lines show VGA offset 0), AC 10h bit 5 cleared (panning applies below the split too); scrolls with the start address (0b8b) and pel panning (0ba3) |
| p2..p7 | mode 13h (chain-4), 70 Hz | standard; see PARTS.md for each |

## Running the original headless (run_dope.py)

- No PMODE kernel and no DOS. Two unicorn CPUs share one 16 MB memory: `rm` (real mode: CDPI and the parts) and `pm`
  (32-bit, GDT with 08h/10h based at code32a = 0x98e0 and 18h based at 0). The loader image is loaded at segment
  0810 (PSP 0800, environment 07F0 with `ULTRASND=240,3,3,7,7` and the path `C:\DOPE\DOPE.EXE`) and relocated;
  PMODE's variables are preset (extended memory from 0x110000) and `pm` starts at code32 0x20d. int 33h / 32h / 31h
  and set/getirqvect are emulated (see the table above), the exit 0x355 ends the run. The int 32h call to CDPI
  switches the main line to `rm` until CDPI returns.
- Real mode: every `int n` is dispatched through the IVT. The IVT starts out pointing at fake BIOS stubs
  F000:n*16 (`nop; iret`, int 8: `nop; int 1Ch; iret`); a code hook services them in Python: DOS (files: DOPE.EXE
  and DOPE.MOD from `../original`, memory blocks, 26h create PSP, 4Ch), video BIOS (mode 13h / 3, DAC), keyboard (no
  key), mouse (none), BIOS tick. Hooks installed by CDPI and the parts (int 21h, int 8) run as real code.
- IRQ 0 goes through the IVT on `rm` (the parts' handlers); IRQ 7 (GUS) runs the player's handler on `pm` on its own
  stack, whenever the main line has interrupts enabled and the PIC mask allows it. The handler takes no emulated time.
- Time: emulated seconds; the main line runs `--mips` (default 100) million instructions per second, in quanta of
  20,000 instructions or up to the next event (frame start, retrace start, PIT, GUS timer, end of a volume ramp).
  Reads of 3DAh that see no progress fast-forward to the next change of the status bits, so retrace waits cost no
  instructions (an I/O read sees the time at the start of its quantum plus what was fast-forwarded: error < 0.2 ms).
  The emulated speed (100 M instructions/s, about a Pentium 90-100 running this 16-bit code; the coder's own comment
  in the parts: "WHY OPTIMIZE ANYTHING FOR PENTIUM 100 MHZ?") only changes how many frames get drawn (and the small
  overshoot per script step); the motion and the part lengths are counted in retraces.
- VGA: registers, 256 KB of planes; chain-4 modes use plain RAM at A0000, unchained modes an MMIO region with the
  write modes, map mask, bit mask and latches. Timing from the CRTC and misc registers (mode 13h and p1's mode:
  449 lines of 31.78 µs = 70.09 Hz; retrace at line 412 for 2 lines). Status bit 3 = vertical retrace, bit 0 =
  vertical blank only (no horizontal timing). The CRTC start address is latched at the start of the retrace; each
  frame is captured at its first line from the latched start, the panning, the line compare and the DAC.
- PIT channel 0 in modes 0, 2 and 3 (mode 3 reads back in steps of 2), latches; PIC masks only.
- GUS at 240h, IRQ 7: the register file and 1 MB DRAM (paper/jizz's model), plus timers 1 and 2 with their IRQs
  (AdLib-style control at 2x8/2x9), the IRQ status 2x6, the IRQ source register 8Fh, and a simulation of each voice's
  position and volume ramp at the GF1 mixing rate (1,000,000 / (1.619695497 µs x 28) = 22,050 Hz with 28 voices), so
  ramp-end IRQs come at the right time and reads of 89h / 8Ah / 8Bh return ramped values. It is a timing model, not a
  sound renderer.
- Outputs, per displayed frame (70.09 Hz): `.frames` (32-byte header `DOPEFRM1` + <IIIIII: 320, 240, record bytes,
  32, 0, 0>, then per frame <HHHH width, height, flags (1 = graphics), 0>, 768 bytes of 6-bit DAC, 320x240 bytes of
  indices with the visible area top left), `.ticks` (frame, time, music order, row, tick countdown, music ticks so far,
  part, start address, CRTC start writes so far, key checks so far = frames drawn by the engine parts, mode, main CS:IP),
  `.gus` (`<music tick> v<voice> r<reg> <value>`, port writes, and an `irq` line with the kind and time before every
  handler run), `.dram`. Snapshots (`--snap-parts`: `snaps/part_<p>.snap` where each part starts; `--snap-at`):
  pickles with the 16 MB memory, VGA memory, both CPU contexts and the harness state.
- Speed: about 0.2x real time on one core (unicorn with an instruction count runs this code at 25-35 M
  instructions/s), so the sequential run takes about 40 minutes; `capture.py` re-runs the parts from their snapshots
  in parallel in about 10 minutes. A resumed run reproduces the uninterrupted one exactly (checked: the stitched
  capture equals the sequential run frame for frame, tick for tick and GUS write for GUS write).
- Not modelled: disk time (the parts load instantly; on the original, reading up to 180 KB per part adds a pause
  between parts that the music does not wait for), the time PMODE needs to switch modes for each GUS IRQ, horizontal
  retrace, sound.
