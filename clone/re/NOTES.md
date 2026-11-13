# clone meets clone (Acme & Fudge, Takeover '98 64k intro) — reverse-engineering notes

Design Aap and Echo, effects Echo, music Simon, graphics Aap; texture mapper and grid filler Craft, radial blur
originally Dune, VESA lib Submissive, MXMPlay Pascal (Niklas Beisert), PMODE/W Daredevil and Tran. MS-DOS,
PMODE/W v1.23 with a packed LE program, VBE 2.0 true colour (32 bits per pixel, or 15 bits) through a linear frame
buffer, music by MXMPlay on a Gravis UltraSound. The program is Watcom C (register calling convention) with
hand-written assembler for the player, the fillers and the blits; no C library.

## Files and tools

All tools run inside the toolbox (`toolbox run -c acme303 python3 ...`) from this folder. Generated files are made
from the original and stay out of Git.

| file | what it does |
|---|---|
| `unpack.py ../original/CLONE.EXE` | runs the "veloader" (the LE program PMODE/W starts) in unicorn up to the point where it calls the intro, at two load addresses, and checks an independent Python decompressor + relocator against it. Writes `unpacked/clone_img.bin`, `unpacked/clone_unp.txt`, `unpacked/veloader.bin` |
| `rep.py` | capstone helper on `clone_img.bin`: `func / dis / calls / xref / funcs / str / ints / ports` |
| `run_clone.py --out full` | runs the whole intro headless in unicorn (see "Running the original headless") |
| `sheet.py full.frames out.jpg --every 2 --cols 6 --scale 0.5` | contact sheets of a capture (or `sheet.py full.frames out.jpg T1 T2 ...` for ticks) |
| `frames2mp4.py full.frames out.mp4` | constant frame rate video of a capture (default 60 fps, from the music start) |
| `fpscheck.py A B --tick0b T` | compares two runs at different `--ips`, frame by frame at equal timer value, to find frame-rate dependent state |
| `NOTES.md`, `PARTS.md`, `BRIEF.md` | these notes; the part timeline; the decompilers' brief |
| generated (not in Git) | `unpacked/` (the image), `full.frames/.ticks/.flips/.gus/.dram/.log` (the capture), `snaps/t*.snap` (start of each part), `clone_reference.mp4` (60 fps, 960x720, no sound, from the music start), `overview.jpg` |

Addresses in these notes are linear addresses of the running program. The image is relocated for linear base
0x400000 (where `run_clone.py` loads it), so address A is at offset A − 0x400000 of `clone_img.bin`.

`clone_img.bin`: 164,972 bytes (0x2846c: code and initialised data), sha256
`4931c572ea8b356acdc104e9aebf16b64496d8d88a2532365af98015692d5494`, FNV-1a 64 `4adf7830170c1b95`. The same image
relocated for base 0 (offsets instead of addresses): FNV-1a 64 `95367b24703a7595` (sha256 in `clone_unp.txt`). The BSS (zero, 0xb6d4cb bytes) follows up to 0xf95937.

## File layout (CLONE.EXE, 63,994 bytes)

| file offset | size | what |
|---|---|---|
| 0x0000 | 10,149 (0x27a5) | MZ stub = PMODE/W v1.23 (the extender, self-compressed: its entry 0x5e copies itself and expands; not needed here). Banner "PMODE/W v1.23 DOS extender - Copyright 1994-1996, Daredevil and Tran." at 0x55. e_lfanew (0x3c) = 0x27a8 |
| 0x27a8 | 0x3e1 | LE header (loader section 0x31e bytes, fixup section 0x2c5 bytes): one page of 0x86e bytes, 3 objects, resident name "veloader", entry object 1 + 0x14, stack object 3 + 0x200 |
| 0x2c00 | 0x86e | LE data page = object 1, the veloader's code and tables (starts with the 20 bytes "VP386CHEKSUM" + 8 spaces) |
| 0x346e | 0x44 | "VE" header (below) |
| 0x34b2 | 0xc049 | compressed program image |
| 0xf4fb | 0x4ff | compressed fixup list (ends at the end of the file, 0xf9fa) |

LE objects (as linked): 1 = code, base 0x10000, 0x86e bytes (flags 0x2045); 2 = BSS data, base 0x20000, 0x5c4
bytes (flags 0x2043, no pages); 3 = stack, base 0x30000, 0x200 bytes. 100 internal 32-bit offset fixups (source
type 7; 95 to object 2, 5 to object 1), no imports. PMODE/W loads and fixes this up like any LE program.

This is not PMODE/W's own compression (PMWLITE writes a "PMW1" executable): PMODE/W runs an ordinary LE, and that
LE is a small loader for the real program, which is appended behind it.

### The veloader (object 1; addresses as linked, object 1 at 0x10000, object 2 at 0x20000)

1. 0x10016: ES (PSP selector) → [0x204ec]. Walks the environment (selector at PSP:2C) past the double zero and the
   word count to the program's path, copies it to 0x20518, int 21h AX=3D00 opens it (error → exit 5).
2. Reads the MZ header (0x20 bytes): LE header = MZ size rounded up to 8 ((pages − 1) × 512 + last page bytes,
   or pages × 512 if that is 0, + 7, & ~7) = 0x27a8. Reads 0xac bytes of the LE header. VE header = LE data pages
   offset (LE+0x80) + pages (LE+0x14) × 0x1000, − 0x1000 + last page size (LE+0x2c) if that is not 0 = 0x346e.
   Reads 0x42 bytes there to 0x20518 and checks 'VE' (the check has no failure branch).
3. VE header (offsets in the header; the unused ones are 0 in CLONE.EXE):

   | ofs | value | meaning |
   |---|---|---|
   | 0x00 | 'VE\0\0' | signature |
   | 0x04 | 0x44 | compressed image, offset from the VE header |
   | 0x08 | 0xc08d | compressed fixups, offset from the VE header |
   | 0x14 | 0xc049 | compressed image size |
   | 0x18 | 0x4ff | compressed fixup size |
   | 0x24 | 0x2846c | image size (code + data) |
   | 0x28 | 0xa2f | fixup list size |
   | 0x2c | 0xb6d4cb | BSS size (zeroed behind the image) |
   | 0x34 | 0x10de | entry (image offset) |
   | 0x38 | 0xb93940 | initial stack pointer (image offset; ESP = (base + it) & ~3) |
   | 0x40 | 2 | image compression method (0 = stored; 1 and 2 use the same decompressor; >= 3 = error) |
   | 0x41 | 2 | fixup list compression method |

4. int 31h AX=0501 allocates image size + BSS size (+ 0x12 if the method is not 0 and the BSS is smaller than 0x12)
   = 0xb95937 bytes → base B (linear). The compressed image is read to the END of the block (B + allocation −
   packed size) and decompressed in place to B (0x103d1); the output never overtakes the input. The BSS is zeroed.
5. A second block (fixup list size + 0x13 if compressed = 0xa42 bytes) receives the fixups the same way. They are
   applied (0x10289, below), the block is freed (0502).
6. ESP = (B + 0xb93940) & ~3, ES = the PSP selector again; if the 8 bytes at entry − 0x14 are "VP386CHE" the
   loader's 20-byte signature is copied there (not the case for CLONE: the entry is Watcom's `jmp` + "WATCOM");
   `call B + 0x10de` (0x10374). When the program returns: int 21h AX=4C00.

### The decompressor (0x103d1: ESI = source, EDI = destination, EAX = output size)

LZ77 with a 4 KB window and two canonical Huffman codes, in one block:

- Bits: the source after the first 19 bytes is a stream of big-endian 16-bit words; bits are taken MSB first
  (the loader keeps a left-aligned 32-bit buffer and refills a word when 16 or fewer bits remain; only the order
  of the bits matters).
- Bytes 0..7: 16 nibbles (low nibble of each byte first) = code lengths of the 16-symbol "length code".
  Bytes 8..18: 22 nibbles = code lengths of the 22-symbol LENGTH code.
- Then 280 code lengths of the literal/distance code, each one symbol (0..15, the length itself, no run codes) of
  the length code.
- Codes (0x10414..0x10472, built the same way for all three): canonical; for L = 1..15, the symbols with length
  L in increasing order get consecutive codes, the first code of length L follows the last code of length L − 1
  (left-aligned 32-bit codes: code += 2^(32−L) per symbol). The lengths must fill the code space exactly
  (the sum of 2^(32−L) overflows to exactly 0), otherwise the loader exits with code 5. Decoding finds the
  shortest L whose last code (left-aligned, low bits all ones) is >= the next 32 bits.
- Data, until EAX bytes are out: symbol s of the literal/distance code. s < 256: the byte s. s = 256 + k
  (k = 0..23): distance = DIST_BASE[k] + (DIST_EXTRA[k] bits), then a symbol j of the length code, length =
  LEN_BASE[j] + (LEN_EXTRA[j] bits); copy `length` bytes from `distance` back, byte by byte (overlap allowed).
  Extra bits are read MSB first.
- Tables in the veloader (file offsets = 0x2c00 + object offset): DIST_BASE dword[24] at 0x788 = 1 2 3 4 5 7 9 13
  17 25 33 49 65 97 129 193 257 385 513 769 1025 1537 2049 3073, DIST_EXTRA byte[24] at 0x840 = 0 0 0 0 1 1 2 2
  ... 10 10 (Deflate's distance table, cut at 24 codes: distances 1..4096); LEN_BASE dword[22] at 0x7e8 = 3..11
  then 13 15 17 19 23 27 31 35 43 51 59 67 83, LEN_EXTRA byte[22] at 0x858 = 0 ×8, 1 ×4, 2 ×4, 3 ×4, 4, 0
  (lengths 3..83). They follow from the usual rule, so the remake can compute them (format constants) or read
  them from the file.
- The image stream uses 49,225 bytes, the fixup stream 1,279 (all of the stored data).

### Fixups (0x10289; the decompressed list, 2,607 bytes, all used)

Records: byte type (0 = end; bit 0: add B to the dword at the site, then site += 4; bit 1: a 16-bit selector at
the site, site += 2: the code selector if the byte there is 0, else the data selector; >= 4 = error), word count
(0 = 65536), dword first site (image offset). Then count − 1 deltas, each a big-endian base-128 number (bit 7 of
each byte = another byte follows), added to the site after the previous entry was applied. CLONE.EXE: one kind
only, 2,570 dword offset fixups (type 1), no selector fixups. The list is in `clone_unp.txt`.

A C++ unpacker: parse MZ → LE header (MZ size rounded up to 8) → VE header (LE data pages + last page size);
decompress image and fixup list; add the base to the 2,570 dwords; append the BSS. Check the result by size and
hash (above).

## Runtime model (PMODE/W, as the intro uses it)

- Flat model: CS and DS/SS are 4 GB selectors with base 0, so near pointers are linear addresses; the first MB
  (DOS memory) is reachable directly. ES at the entry is the PSP selector (environment selector at PSP:2C).
- Entry 0x4010de (Watcom style: `jmp` over "WATCOM"): [0x4207b8] = PSP selector, `sti`, `cld`, ES = DS,
  `fninit`, FPU control word 0x1f7f (**rounding = truncate toward zero**, precision 64 bit, all exceptions
  masked), call main 0x4001ab, int 21h AX=4C00. 0x4010dd (called before every `fistp`, 76 sites) is a bare
  `ret` (Watcom's __CHP, empty: the truncation comes from the control word). All float → int conversions
  truncate.
- Memory: no DPMI allocation. The program, its data and a 10,000,000-byte heap (0x400ff4: start 0x42846c, the BSS
  start) all live in the block the veloader allocated. 0x40100c allocates (EAX = size → ESI; first fit, 4-byte
  header = size | 1 when used; 0x401015 would free, nothing calls it). All allocations happen in the precalc in a fixed order, so the
  addresses are constant (heap walk of a snapshot: module 0x428470 (0x124f80 bytes), textures 0x54d3f4,
  0x58d7fc, 0x5cdc04 (0x40404 each), the ten pictures (12-byte header + pixels) 0x60e00c.., the geosphere
  buffers 0x622adc.. and 0x66dc24.., part 9 / 10 buffers 0x6b8d6c.. and 0x7ddcfc.., free from 0x904d68).
  DOS memory: int 31h 0100 for 0x20 and 0x10 paragraphs (VBE info and mode info blocks, [0x4207bc] / [0x4207c0]:
  segment and selector), freed at the exit.
- Services go through Watcom's int386x (0x40110c → 0x401125: `int n` from a table of `int n; ret` stubs at
  0x401180 + 3n) with a REGS block at 0xea5744 and a DPMI real-mode register block at 0xea5780.

## Services used (complete list from the image and a full run)

| service | where | what |
|---|---|---|
| int 21h 09 | main, 0x414aee | text: title, "samples [....]", "] - uploading", "textures [....]", the title again at the exit |
| int 21h 3508 / 2508 | 0x40226f, 0x40228c, 0x4022a5 | get / set the protected-mode vector of IRQ0 (PMODE/W serves these in protected mode) |
| int 21h 4C | 0x40110a | exit |
| int 31h 0100 / 0101 | 0x4012db, 0x401853 | DOS memory for the VBE blocks |
| int 31h 0300, BL=10h | 0x401288 | VBE through real mode: 4F00, 4F01, 4F02, 4F07 (BL=0), 4F05 (bank switch, unused with an LFB) |
| int 31h 0800 | 0x401326 | map the LFB (PhysBasePtr, size = TotalMemory × 64 KB) |
| int 16h 01 | 0x400b1a | once per frame: a key ends the intro |

The intro does not wait for the vertical retrace anywhere (no port 0x3da, no 4F07 BL=0x80).

## Ports

| port | where | use |
|---|---|---|
| 0x43, 0x40 | 0x40228f.. (install), 0x4021f8.. (every IRQ), 0x4022a8.. (restore), 0x4022b3.. (read) | PIT channel 0: mode 2 (0x34) + count; latch (0x00) + 2 reads in xmpGetTimer |
| 0x20 | 0x4021db | EOI in the timer handler |
| 0x240 + 0x000, 0x102..0x105, 0x107 | MXMPlay | GUS mix control, voice / register select, data low / high, DRAM data (upload byte by byte: 985,424 writes) |

## Video (Submissive's VESA lib, 0x401235..0x401853)

- 0x401628 (EAX = width, EDX = height, BL = bits): 4F00 into DOS memory (the request signature written is the
  dword 0x56423532 = bytes "25BV", not "VBE2"), the far pointers of the info block are converted to linear;
  every mode of the list is queried with 4F01; the first with XResolution, YResolution and BitsPerPixel equal
  is returned (no check of the memory model or the masks). 0x4013f3: mode attribute bit 7 (LFB) required.
- main: 320x200x32 ([0x42048d] = 32), else 320x200x15 ([0x42048d] = 15), else "error initializing 32bit and 15bit
  videomode" and exit. 0x401457 sets the mode with bit 14 (LFB), keeps BytesPerScanLine in [0xea5764] (not used
  afterwards) and BitsPerPixel in [0xea57b2]; 0x4016b0 maps PhysBasePtr → [0xe35100]. 0x401809: [0xea5778] =
  0x4016fb (4F05), [0xea577c] = 0x40141f (4F07 BL=0, set display start immediately).
- **32-bit path**: the pixel format is fixed: dword 0x00RRGGBB (bytes B, G, R, 0); nothing reads the masks.
  Four pages of 0x3e800 bytes (320 × 200 × 4, so 1280 bytes per line are assumed) in video memory. The frame
  routine 0x400000 shows the page just drawn (4F07 BL=0, CX = 0, DX = page × 200), advances the page
  [0x4207a9] = (page + 1) & 3 and sets the draw target [0xe350ec] = LFB + page × 0x3e800. A part therefore draws
  over the picture it drew four frames earlier; whatever it does not overwrite stays from then.
- **15-bit path**: the draw target is a fixed 32-bit buffer at 0xdb1aec (main memory); 0x400000 calls 0x4188d4,
  which converts it to 5:5:5 (hard-coded: R bits 10–14, 640 bytes per line) straight into the single visible
  page, and writes 0 to every source pixel it has read, so every frame starts from a black buffer. No page flip,
  no retrace wait (tearing). The 15-bit path also differs where a part relies on what was left in its draw
  target (decompilers: say per part whether it overwrites the whole frame).
- Pictures are generated as 32-bit 0x00RRGGBB; 8-bit intermediate buffers (parts 1, 3, 6) are expanded through
  256-entry dword palettes (0x404a08, 8×8 blocks).

## Music and timer

- The player is Niklas Beisert's MXMPlay; this build calls itself "cubic tiny gus xm player v1.4 (c) '95/96
  Niklas Beisert / pascal" (0x401894). Code 0x4018d4..~0x402680, data block 0xe30aec (0x4000 bytes, in the BSS;
  its pointer is stored in the code at 0x4018d4 and read by 0x4018d8). Entry points: 0x40196a xmpInit (EAX = PSP
  selector (bit 31 set = no sound), ESI = module, EBX = data block, ECX = max timer period 0x10000; checks
  'MXM\0', parses ULTRASND, uploads the samples), 0x402129 xmpPlay(EAX = start order), 0x40219a xmpStop,
  0x4022b3 xmpGetTimer, 0x401bf4 xmpGetPos (AL = row [+6], AH = order [+0x12]), 0x401bde set volume (AL → [+1]; xmpInit
  sets 0x40, main sets 0x20 right after xmpPlay).
- **Same player family as JIZZ** (`jizz/re/notes-music.md`, `jizz/remake/src/mxmplay.*`), a different build:
  a normalised instruction comparison of the two player ranges matches 77 %. The IRQ0 handler (0x4021ac), the
  timer, xmpGetTimer, the GUS update and the tick structure are the same code with the data block offsets one or
  two bytes lower (JIZZ +0x36/+0x3a/+0x42 = CLONE +0x35/+0x39/+0x41, the channel blocks at +0x9aa instead of
  +0x9ac, ...); the IRQ vector is set with int 21h 25/35 instead of int 31h 0204/0205; the effect dispatcher
  has more handlers (about 90 more instructions: effects JIZZ's cut-down build leaves out). The music scope must
  list the differences routine by routine.
- ULTRASND parser 0x402621: needs `ULTRASND=2x0,` → port 0x2x0 ([+0x4f]); without it the player runs silent.
- **Module**: MXM, stored in the image at 0x406862 (0xe153 = 57,683 bytes: header, instruments, patterns and the
  4,288 bytes of 8-bit samples), copied by 0x414aee to a heap buffer (0x428470, 0x124f80 bytes) and completed
  with **synthesised 16-bit samples** (490,568 words = 981,136 bytes; "samples [....]" progress, 20 steps):
  0x4149b4 (12 calls: amp × 255 × sin(i × f), where f falls each sample by a step that itself changes by a
  constant: drums and toms) and 0x414a38 (8 calls: amp × 255 × sin(2π i / P1) × sin(2π i / (P2 + P3 sin(2π i /
  P4))): tones with a modulated period), float parameters pushed by 0x414aee; samples clamped to ±0x7e81. Header: 61 orders, restart 0, 18 channels, 63 patterns, 21 instruments,
  speed 3, 135 BPM, linear frequencies, sample data at module offset 0xd093. Order list: 0 1 2 3 5 6 7 8 9 10 11
  13 4 14 12 15 16 … 62 28. The sample upload fills 985,424 bytes of GUS DRAM (`full.dram`).
- Timer: xmpPlay sets the period [+0x41] = 0x2d86bf / BPM = 22,100 PIT clocks (135 BPM: 53.99 Hz, the BPM never
  changes) and installs 0x4021ac on IRQ0 (first period 0x10000). Each interrupt: [+0x39] += [+0x35] (elapsed),
  [+0x31] += [+0x35]; when byte [+0x33] becomes non-zero the old handler is called (BIOS clock); EOI; remaining
  [+0x45] −= [+0x4b], new tick when it runs out; PIT reprogrammed with min(remaining, [+0x4b]); at a new tick
  0x4024ab (GUS update) and 0x401e9e (player tick) run on the player's own stack ([+0x2daa]). One interrupt per
  player tick; speed 3 → a row every 3 ticks (55.6 ms).
- xmpGetTimer (0x4022b3) = [+0x35] + [+0x39] − PIT counter (latched): PIT clocks since the music start,
  interpolated inside the tick (0 at the second interrupt, as in JIZZ).

## Program flow (main = 0x4001ab)

1. Title (int 21h 09), heap 0x400ff4 (10 MB at 0x42846c), **music**: 0x414aee builds the module (samples
   progress), 0x40196a xmpInit uploads it ("] - uploading").
2. **Precalc**: 0x416806 (projection constants 0x414f04(−0.625, −0.3125); 16×16 bilinear weight table at
   0x426d8c; exp table 0x426b8c), 0x403f10 (multiply table 64 × 512 bytes at 0xea740c: (i × j) / 64), 0x4053f2
   (part 1 set-up, again at its event), 0x40347a (exp table 0x420854), 0x4033ee (16×16 bilinear weights at
   0x420a54); "textures [....]" (0x400240..0x400844): three 257×256 32-bit maps ([0xe35108], [0xe3510c],
   [0xe35110], 0x40404 bytes each) and two 256×257 RGB colour tables (0xe6542b, 0xe35128), all from sin / cos of
   the distance to the centre of repeating 8/16/32-pixel cells; 0x4033a7 expands the table 0xe6542b to the grid
   parts' texture [0x4207c8] = 0xdf02ec (0x10101 dwords); the grey palettes and the ten pictures (below); part
   set-ups 0x416cca (part 7: geosphere, texture [0xe35108]), 0x417563 (part 8: geosphere, [0xe3510c]), 0x418402
   (part 9 buffers), 0x419373 (part 10 buffers), 0x419d5b (title and credit pictures), 0x40181e (DOS memory).
   At 150 M instructions per second everything up to here takes 0.65 s (`full.ticks`: ticks 0..10).
3. Video mode (above), first frame 0x400000, xmpPlay(0), volume 0x20.
4. **Frame loop** (0x400b18..0x400fbf), once per frame:
   - int 16h AH=01: a key, or the end flag [ebp−0xc], ends the loop;
   - position = order × 64 + row (xmpGetPos); while it is >= the next sync table entry (0x420499, 8 bytes:
     dword position, dword event; index [0x420495]) the event's handler runs (a Watcom switch: values at
     0x4000c3, jump table at 0x40010f) and the index advances (several events can fire in one frame);
   - the current part [ebp−0x14] draws one frame (switch 0x400e80: part routine, see PARTS.md);
   - 0x400000 shows it;
   - dead code: past position 0x1020 (order 64, row 32) the volume would fade (the intro ends at 3c:3f);
   - **time**: t70 = xmpGetTimer × 70 / 1,193,182 (double [0xe9572e]: time in 1/70 s), **dt** = t70 − previous t70
     (float [0x420489]), [0xe9573e] += 0.03 dt (written, never read).
5. Exit: xmpStop, mode 3 (4F02 BX=3), the title again, DOS memory freed, return → int 21h 4C00.

### Time base: everything is integrated with dt

No part reads the timer. Every animated variable is advanced once per drawn frame by dt (1/70-second units,
float), e.g. part 5: angle += 0.005 dt (0x403538). For a variable that only moves linearly this is the same as a
function of the timer (up to float rounding); anything nonlinear (damping, x += v dt with v changing, fades
clamped per frame, feedback buffers blended once per frame) depends on the frame rate. Order inside a frame:
events → draw with dt of the previous interval → show → read the timer → new dt. So a frame drawn after the timer
read T carries the state integrated up to T. The first two frames of the loop use dt = 6.0 (the initial value of
[0x420489]) and dt = t70 − 0 = t70 of the first read (−4.1, the timer is negative before the second interrupt).
`full.flips` gives the timer value T of every frame.

### The sync table and its events

0x420499, 98 entries (position = order × 64 + row → event), last 3c:3f → −1. Events and what their handlers do
(part numbers: PARTS.md). Who reads the variables: [0x4224f8], [0x4224fc] parts 1, 3, 6 and their helpers;
[0x422500]..[0x422514] part 1; [0x422518] part 3; [0x42252c] part 6; [0x421a54] part 5; [0x421a7c], [0x421a84],
[0x421a88] part 4 ([0x421a80] is never read); [0x427f70] the 3D engine (0x416ae3, 0x416b1f) and parts 7, 8;
[0x427f74] part 7; [0x427fe4], [0x427fe8], [0x427ff0] part 8; [0x428014], [0x428140] part 9; [0x42815c],
[0x428160] part 2; [0x428468] part 11.

| event | handler | event | handler |
|---|---|---|---|
| 1 | 0x4053f2 (part 1 set-up), part = 1 | 101 | [0x4224f8] = [0x4224fc] = 2000, [0x422500] = 1 |
| 2 | part = 2 | 102 | [0x4224f8] = [0x4224fc] = 0 |
| 3 | 0x405b10 (part 3 set-up), part = 3 | 103 | [0x422500] = 0 |
| 4 | part = 4 | 104 | [0x4224f8] = 0, [0x4224fc] = 2000, [0x422500] = 1 |
| 5 | 0x4033a7(0xe35128) (grid texture), part = 5 | 105 | [0x4224f8] = [0x4224fc] = 0, [0x422504] = 255.0 |
| 6 | 0x4065c5 (part 6 set-up), part = 6 | 106 | [0x422510] = 1.0 (title fade) |
| 7 | part = 7, [0x427f70] = 1.0, 0x414f04(−0.625, −0.390625) | 107 | [0x422508] = 1.0 (credits fade) |
| 8 | part = 8 | 111 | part = 1, [0x422510] = 0 |
| 9 | part = 9, [0x427f70] = 1.2 | 201 | [0x428160] = 0.3, [0x42815c] = 1 |
| 10 | part = 10, 0x414f04(−0.625, −0.3125) | 301 | [0x422518] = 1 |
| 11 | part = 11 | 401 | [0x421a7c] = ([0x421a7c] + 1) mod 3 |
| −1 | end flag | 402 / 403 | [0x421a80] = 1 / [0x421a84] = 1, [0x421a88] = 0 |
| (99) | part = 99 (0x400079: black with a blue band; no table entry uses it) | 501 | [0x421a54] = −[0x421a54] |
| 601 | [0x42252c] = 1 | 701 | [0x427f74] += 1 |
| 801 | [0x427ff0] += 2 | 802 | [0x427fe8] = 1.0 (not in the table) |
| 803 | [0x427fe4] −= 1, [0x427f70] −= 0.25 | 901 | [0x428014] = 1.0 |
| 902 | [0x428140] = ([0x428140] + 1) mod 3 | 1011 / 1012 | [0x427f70] = 1.2 / 1.0 |
| 1101 | [0x428468] = 1 | | |

Table (order:row event): 00:00 1, 00:20 103, 04:00 101, 08:00 102, 08:00 106, 08:20 11, 0b:00 1101, 0c:00 2,
0e:00 201, 10:00 8, 10:04 801, 10:08 801, 10:0c 801, 10:10 801, 14:00 803, 14:08 803, 14:10 803, 14:18 803,
14:18 6, 17:20 601, 18:00 9, 18:08 901, 19:00 / 19:08 / 19:10 / 1a:00 … 1f:10 902 (21 entries), 20:00 5,
21:00 501, 22:00 501, 23:00 501, 24:00 7, 28:00 701, 28:08 701, 28:10 701, 29:00 3, 30:00 301, 30:20 4, 31:00 403,
32:00 402, 32:00 401, 33:00 401, 34:00 401, 35:00 401, 36:00 10, 39:00..39:3e 1011 / 1012 alternating every 2 rows
(32 entries), 3a:00 104, 3a:00 111, 3c:00 105, 3c:10 107, 3c:3f −1.

The events are checked once per frame, before the part draws, so they lag the music by up to one frame.

## Pictures and textures

Stored pictures (8 bits per pixel, raw, w × h bytes; 0x403f53 makes a 32-bit copy through a 6-bit palette × 4,
index 0, and 0xf0 when its last argument is 0, become transparent 0xffffffff; result = {pixels, w, h}):

| address | w × h | palette | contents | pointer | used by |
|---|---|---|---|---|---|
| 0x41a950 | 42 × 9 | grey A (indices 240..255) | text "meet us" | [0xe35124] | part 2 |
| 0x41aaca | 85 × 12 | grey A | text "get them clones" | [0xe35120] | part 5 |
| 0x41aec6 | 88 × 10 | grey A | text "connect the dots" | [0xe3511c] | part 3 |
| 0x41b236 | 142 × 10 | grey A | text "tunnels meet more tunnels" | [0xe35118] | part 4 |
| 0x41b802 | 77 × 65 | grey B | brush-drawn kanji-like glyph 1 | [0xe350fc] | part 2 |
| 0x41cb8f | 63 × 67 | grey B | glyph 2 | [0xe350f8] | part 5 |
| 0x41dc0c | 68 × 68 | grey B | glyph 3 | [0xe350f4] | part 3 |
| 0x41ee1c | 56 × 64 | grey B | glyph 4 | [0xe350f0] | part 4 |
| 0x41fc5c | 101 × 9 | grey C (indices 0..15) | text "clone meets clone" | [0xf73930] | parts 1, 11 |
| 0x41ffe9 | 37 × 32 | grey C | text ".aap / .echo / .simon" (3 lines) | [0xf7392c] | part 1 |

Grey A / B (main 0x4008d4..0x400946): entries 255 − c (c = 0..15) = dword table[15 − c] × 63 / 100 for R, G, B
(tables of percentages at 0x41b7c2 and 0x41fc1c; i.e. entry 240 + k = table[k] × 63 / 100: 240 is black, 241..254
fall from 94 % to 8 %, 255 is white), all other entries 0. Grey C (0x419d5b): entry c = c × 63 / 15.
The text bitmaps are anti-aliased letters; the glyphs are drawn pictures. Everything else on screen is generated.

Textures: [0xe35108] (map A: part 7), [0xe3510c] (map B: parts 8 and 10), [0xe35110] (map C: part 9), grid texture
[0x4207c8] = 0xdf02ec (from the colour table 0xe6542b; from 0xe35128 from event 5 on: parts 2, 4, 5). Meshes:
a geosphere (octahedron 0x427f9c / 0x427f84 subdivided 4 times by 0x4174c4 / 0x417420 / 0x4172c8) for parts 7
and 8; parts 9 and 10 build their geometry every frame (0x418094 / 0x418445 / 0x417c48 and 0x418fd2 / 0x4193b6 /
0x418b86).

## Frame-rate dependent state

The intro has no retrace wait, so its frame rate is what the CPU gives: at 150 M instructions per second the
capture shows from 26 fps (the end of part 1) and 29 fps (part 6) to 725 fps (part 11). Measured (PARTS.md,
"Frame-rate dependence"): with the timer frozen nothing moves except at sync events and the settling of part 1's
feedback buffer; between runs at 55.5 M, 150 M and 311 M instructions per second, parts 7, 8, 10 and 11 give the
same pictures at equal timer values, 4, 6 and 9 nearly, and parts 1 and 3 (8-bit buffers with per-frame feedback,
particles) and 2 and 5 (camera time and event-timed direction flips, at low rates) differ visibly. The capture's
frame rates are the reference for those.

## Running the original headless (run_clone.py)

- No PMODE/W and no veloader: `clone_img.bin` (relocated for 0x400000) is loaded at 0x400000, the BSS is zero,
  and the registers are set as the veloader leaves them: CS = 0x08 (flat code), DS = SS = FS = GS = 0x10 (flat
  data), ES = 0x18 (PSP, base 0x80000; PSP:2C = 0x20, the environment at 0x80100 with `ULTRASND=240,3,3,7,7`),
  ESP = 0xf93940 − 4 with a return address that does int 21h 4C00. DOS memory from 0x81000, each 0100 block gets
  a GDT selector (0x28, 0x30). 32 MB of RAM.
- Fake VBE 2.0 (2 MB): mode 0x10f = 320x200x32 (masks 8@16, 8@8, 8@0, reserved 8@24, 1280 bytes per line) and
  0x10d = 320x200x15 (5:5:5, 640 bytes per line), LFB at 0xe0000000 (0800 maps it 1:1). The intro picks 0x10f
  (`--bpp 15` offers only 0x10d). 4F07 sets the visible page at once.
- Time base: the PIT clock. Main-line code runs at `--ips` instructions per second (default 150 M: a 150 MHz
  Pentium at one instruction per clock; the original asks for a 133 MHz Pentium) in quanta of `--quantum`
  (default ips / 4000 = 0.25 ms); PIT latch reads return the counter at the start of the current quantum. The IRQ0
  handler runs (instantly, not charged) whenever the programmed period has elapsed; before xmpPlay ticks run at the
  BIOS rate (0x10000) with no handler. Port I/O costs nothing (the GUS upload of 1 MB is instant; on the real
  machine it takes a moment), nor does 4F07, and video memory is as fast as main memory.
- `--freeze-timer` (analysis): xmpGetTimer always returns its first value of the run, so dt = 0 after the first
  frame and only event-driven or per-frame state moves. `--part-ips N=IPS,...` changes the speed while part N is
  active; `--snap-parts` / `--snap-at T,..` save snapshots (pickle: 'mem' 32 MB from linear 0, 'lfb' 2 MB, 'ctx',
  'st' harness state incl. PIT, VBE and the frame counter, 'gus', 'eip'); `--load --snap file` resumes.
- Faked hardware: PIT channel 0, PIC, a GUS at 0x240 (the Gus class of jizz/re/run_jizz.py: register file, 1 MB
  DRAM, word I/O; reads of voice registers return what was written, with the "stopped" bits set in 0x80 / 0x8d;
  volume reads 0x89 return the last value written to 0x09, not the ramped one, so the `.gus` log is a register
  log, not a sound render).
- The full run (`run_clone.py --out full --snap-parts`, about 17 minutes on one core): precalc ticks 0..10 (0.65 s),
  VBE mode 0x410f and xmpPlay in tick 11 (program time 0.70 s; music time 0 at tick 12), then 11,712 player ticks;
  event −1 at 3c:3f, xmpStop and text mode in tick 11723, exit at 217.63 s program time (music time 216.93 s).
  19,230 frames shown; 1,024,244 GUS register writes; 985,424 bytes uploaded to GUS DRAM. A short run with
  `--bpp 15` (400 ticks) takes mode 0x410d and shows the same pictures through the conversion (at a different
  frame rate: part 1 at 59 fps instead of 81 over ticks 12..400; the conversion costs time).
- Outputs: `.frames` (32-byte header `CLONFRM1` + <IIIIII width 320, height 200, bits 24, record 192000, header 32,
  video bits 32>; then per timer tick the picture on screen at the end of the tick as RGB), `.ticks` (tick, PIT
  count, program time at the end of the tick, order, row, player timer, part, sync index, frames shown so far,
  main stage, main EIP), `.flips` (per shown frame: number, tick, program time, the timer value it was drawn with,
  t70, part, order, row), `.gus` (register writes: `tick v<voice> r<reg> value`), `.dram` (final GUS DRAM).
  `--flip-frames` writes one record per shown frame instead (magic `CLONFLP1`).
