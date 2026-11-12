# STARS: Wonders of the World (NoooN, 1995) — reverse-engineering notes

Noise GROO (music, module dated "CNCD July 1995"), 3D motion and vertices BARTi, system and special effects KARL,
script and pixels RA, sound LCA (LCA's MOD-Player V3.02, "(C) Cedric BERMOND (LCA) 1993-95"), PMODE 3.07 by TRAN.
MS-DOS, 32-bit protected mode, VGA mode 13h only, music on a Gravis UltraSound (SB and no-sound paths exist).

## Files and tools

All tools run inside the toolbox (`toolbox run -c acme303 python3 ...`) from this folder. Generated files are made
from the original and stay out of Git (`unpacked/`, `snaps/`, `full.*`, `*.mp4`).

| file | what it does |
|---|---|
| `unpack.py ../original/STARS.EXE` | runs the loader chain (Infiny Loader + PKLITE) in unicorn and checks it against a Python reimplementation, extracts the overlay's 58 files and checks the Python DIET decompressor against the program's own routine; writes `unpacked/` |
| `rep.py` | capstone helper on `unpacked/stars_img.bin`: `func / dis / dis16 / calls / xref / funcs / str / ints` |
| `run_stars.py --out full` | runs the whole demo headless in unicorn (see "Running the original headless") |
| `sheet.py full.frames out.jpg --every 2 --cols 6 --scale 0.5` | contact sheets of a capture (or `sheet.py full.frames out.jpg F1 F2 ...`) |
| `frames2mp4.py full.frames out.mp4` | constant frame rate video of a capture (60 fps, from the music start) |
| `NOTES.md`, `PARTS.md`, `BRIEF.md` | these notes, the part timeline, the brief for the decompilers |

Generated: `unpacked/stars_img.bin` (the program image as loaded, 0x840b0 bytes), `unpacked/stars_unp.txt` (entry,
stack, relocations, hashes), `unpacked/stars_pklite.exe` (the decrypted PKLITE executable), `unpacked/files/NN.bin`
(the overlay files, unpacked), `unpacked/files.txt` (the overlay directory), `full.frames / .ticks / .gus / .dram /
.log` (the capture), `snaps/f<frame>_<routine>.snap` (snapshots), `stars_reference.mp4`.

`stars_img.bin`: the first 0x7c0ab bytes are the decompressed image, sha256
`322e3fdd9bdc3fac98628145a1899ddd18e8f67c397f22d7aa94a3ab3874e44b` (relocated for load segment 0x1000; the 17
relocations are all in the 16-bit kernel, the 32-bit part is position independent); the rest up to 0x840b0 (the end of
the stack, SS:SP = 7c0b:8000) is zero. **Addresses in these notes are offsets in this image.** The 32-bit code and
data selectors have their base at the load address, so an image offset is also the 32-bit program's near pointer
("code32 offset"); in the headless run the image is at linear 0, so it is the linear address too.

## File layout (STARS.EXE, 1,224,140 bytes)

| file offset | size | contents |
|---|---|---|
| 0x00000 | 0xa0 | MZ header: 76,154 bytes of MZ image (e_cblp 0x17a, e_cp 0x95), header 10 paragraphs, no relocations, minalloc 0x71cb, maxalloc 0xffff, SS:SP 1250:07da, CS:IP 1250:0000. Bytes 0x1c..0x9f are not used by DOS |
| 0x000a0 | 0x12500 | the load image of a PKLITE executable (its MZ header removed), encrypted by Infiny Loader |
| 0x125a0 | 0x3da | Infiny Loader stub (entry), self-decrypting |
| 0x1297a | 0x11819a | overlay: 58 data files, 56 of them DIET-compressed (the demo's pictures, textures, objects, tables, music) |
| 0x12ab14 | 0x2b8 | overlay directory: 58 entries of 12 bytes, read backwards from the end of the file |

### Stage 1: Infiny Loader (stub offsets; the stub runs at load segment + 0x1250)

"Infiny Loader -> file integrity error / Get a new copy of this demo on A.C.E : +33-1-45 88 75 48 ..." (the
decrypted stub's strings; the PKLITE copyright string is kept there too).

1. Layer 1 (stub 0x2e..0x44) decrypts 0x1ca words from stub 0x46: per word `xchg al, ah; xor ax, 0xb649; rol ah, 1;
   neg al; xor ax, 0xa32b; rol ah, 1; inc ax`.
2. Layer 2 (stub 0x9c..): `int 3` / `int 1` with the vectors pointed into the stub decrypt 0x163 words from 0x112
   (anti-debugging: a debugger that owns int 1 / int 3 breaks it).
3. 0x112: masks IRQ 0 and 1 in the PIC, saves the int 1 / int 3 / int 8 vectors and points them into the stub
   (int 3 = the decrypt step at 0x258, int 8 = the set-up at 0x186, which is run with an `int 8` instruction and
   rewrites the loop at 0x1b3 to `lodsw; int 3; loop`).
4. Decryption of the PKLITE image (file 0xa0..0x125a0, in memory at the load segment), 0x1250 paragraphs of 8 words,
   in place: `w = ((((w - 0xbc67 - 0x305e + 0x9af9) & 0xffff) ^ 0xc4f5) - 0x7b9b) & 0xffff`. Checksum over the
   decrypted words: `ck ^= w; ck = (ck + lo(w) * hi(w)) & 0xffff` (start 0); it must equal the word at stub 0x2d8
   (0x0981), otherwise the integrity error is printed and the program exits with code 0xf0.
5. Restores the vectors and the PIC mask, applies the PKLITE image's relocation table (one entry: image offset 7, the
   immediate of the PKLITE stub's `add ax, <load segment>`), sets SS:SP = load + 0x125a : 0x0200 and jumps to
   load - 0x10 : 0x0100 (= PSP:0100, PKLITE's entry) with DS = ES = PSP. The stored PKLITE MZ fields are at stub
   0x2d0: SS 0x125a, SP 0x0200, CS 0xfff0, IP 0x0100, checksum 0x0981; relocation count 1 at 0x2f6.

A C++ unpacker needs only step 4: decrypt file[0xa0 : 0x125a0] word by word, optionally check the checksum.

### Stage 2: PKLITE 1.1x ("PKLITE Copr. 1990-91 PKWARE Inc.", no "extra" compression)

In the decrypted image: 0x00..0x44 the stub header (`mov ax, 0x83fc` paragraphs of memory needed, `mov dx, 0x1250`
image paragraphs, "Not enough memory$"), 0x44..0x290 the decompressor (copied with the data to the top of memory;
its tables at decompressor offsets 0x206 / 0x211 / 0x21c / 0x22c), compressed data from image offset 0x290.

- Bit reader: 16-bit little-endian words, least significant bit first; the next word is loaded as soon as the 16th
  bit of the current one has been taken (so it precedes the bytes that follow it in the stream). The first word is
  loaded before the first bit.
- `0` + byte: literal. `1` + length code + (for lengths other than 2) offset-high code + byte (offset low): copy
  `length` bytes from `out_pos - (high << 8 | low)`, byte by byte (overlaps allowed). Length 2 always has high 0.
- Length codes (after the `1`): `10`=2 `11`=3 `000`=4 `0010`=5 `0011`=6 `0100`=7 `01010`=8 `01011`=9 `01100`=10
  `011010`=11 `011011`=12 `0111010`=13 `0111011`=14 `0111100`=15 `01111010`=16 `01111011`=17 `01111100`=18
  `011111010`=19 `011111011`=20 `011111100`=21 `011111101`=22 `011111110`=23 `011111111`=24, and `011100` = read a
  byte b: b = 0xfe → no output (segment normalisation), continue; b = 0xff → end of data; else length 25 + b.
- Offset-high codes: `1`=0 `0000`=1 `0001`=2 `00100`=3 `00101`=4 `00110`=5 `00111`=6 `010000`..`010110`=7..13,
  `0101110`..`0111111` = 14..31 (the 7-bit code read as a binary number, minus 32; the stub computes `bx & 0xdf`).
- After the end code: relocation groups (count byte, segment word, count offset words; count 0 ends), then SS, SP,
  CS, IP words (relative to the load segment).
- Result: 0x7c0ab bytes (0x12235 bytes of compressed data), 17 relocations (all in the 16-bit kernel, image
  0x7a287..0x7b7fc), entry 7a21:0000, SS:SP 7c0b:8000 (32 KB of stack after the image).

`unpack.py` runs both stages in unicorn (real mode, IVT dispatch so the stub's own int 1 / int 3 / int 8 handlers
run, minimal DOS) at load segments 0x1000 and 0x1700, and checks that a Python implementation (`py_decrypt`,
`pklite_decompress`) gives the same image and relocation list.

### The overlay (read at run time by the program)

- Directory: entry n (n = 1..58) is the 12 bytes at `file_size - 12 * n`: dword n, int32 offset from the end of the
  file (negative), dword stored size. The program (0x4a9f3, file number in AX) seeks with `int 21h AX=4202` to
  `-12 n` from the end, reads the entry, allocates the stored size, seeks to the offset (again from the end) and reads
  the data (0x4d086: 0x2000-byte pieces through a real-mode buffer at 0x4afc0), then 0x4ac8b: if the data has `dl`
  at +6 and `z` at +8 it is DIET-compressed and is unpacked into a new block (the stored block is freed), otherwise it
  is copied to a block of the exact size. The files are contiguous from 0x1297a to 0x12ab14; `files.txt` lists them.
- The program opens itself by the name after the environment (0x4a98b, `int 21h AX=3D02` via real-mode int);
  if that name starts with `A` or `B` it prints "You can't launch stars from floppy disk." and exits.
- DIET data format (17-byte header): `b4 4c cd 21 9d 89 'dlz'`; byte 9 = compressed size bits 16..23; word 10 = bits
  0..15 (size after the header); word 12 = CRC-16 (polynomial 0xa001 reflected, initial 0) of the compressed bytes;
  byte 14 bits 2..7 = original size bits 16..21 (0x4ac9a: `movzx edx, byte [+0xe]; shl edx, 14; mov dx, [+0xf]`);
  word 15 = original size bits 0..15. All 56 headers check (sizes and CRCs).
- DIET bit stream (0x4ad47; the same code as the DOS32 loader, see `jizz/re/NOTES.md`): bit reader as PKLITE's.
  `1` + byte: literal. `0 0` + byte b: `0` → b != 0xff: copy 2 from -0x100 + b; b = 0xff then `1` = nothing, `0` =
  end. `0 0` + byte b + `1` + 3 bits h (MSB first): copy 2 from `((0xf8 | h) - 1) << 8 | b` (as a negative 16-bit
  offset). `0 1` + byte b + high part + length: high byte starts at 0xff, `hb = hb << 1 | bit`; if the next bit is
  0: d = 2 and up to 3 times {bit 1 → stop; else hb = hb << 1 | bit, d <<= 1}, hb -= d. Offset `(hb << 8 | b) -
  0x10000`. Length: `1`=3, `01`=4, `001`=5, `0001`=6, `00001x` = 7 + x, `000001` + byte = byte + 17, `000000` +
  3 bits (MSB first) = 9..16. Copies go byte by byte.
- What the files are (by size and use; the part scopes describe each in detail): 1 = the music module (780,560
  bytes, see "Music"); 0xfd00-byte files = 768-byte 6-bit palette + 320x200 picture (8..14 title cards, 52..56
  credits, 57); 0xfa00 = 320x200 screens without palette (19, 25, 27, 47, 58); 0x10000 = 256x256 maps (2, 3, 16, 17,
  21, 23, 43); 0x300 = palettes (4, 15, 20, 22, 42, 48, raw 26, 31); objects starting `B!\x1a` (18, 24, 45, 50, 51,
  BARTi's 3D format) and `WLDD` (28); the rest tables (5, 6, 7, 29, 30, 32..41, 44, 46, 49).

## The program

### PMODE 3.07 and the hand-over (16-bit kernel at image 0x7a210, segment 0x7a21)

Tran's PMODE 3.07 linked as a separate 16-bit segment at the end of the image, entered at 7a21:0000. It calls
`_pm_info` (0x496: BX = paragraphs of DOS memory needed, CL = processor (3/4/5), CH = system type 0 raw, 1 XMS,
2 VCPI, 3 DPMI) and prints "> 80486 processor." / "> protected mode type: RAW." etc. (message table at segment
offset 0x11c; the error texts "<!> ..." are at 0x7a426..), then `_pm_init` (0x5d9) with ES =
SS + 0x800 (the DOS memory after the 32 KB stack). In protected mode it creates one 32-bit code and one data
selector with base = load segment * 16 and limit 4 GB (int 31h 0000/0007/0008/0009), loads DS = ES = FS = GS = SS,
ESP = SP + (SS - load) * 16, and stores:

| variable | value |
|---|---|
| [0x17620] | code32 base (linear address of image offset 0); the program converts linear → near by subtracting it (VGA = 0xa0000 - [0x17620], MDA = 0xb0000 - [0x17620]) |
| [0x17624] | size of the free DOS memory after the stack |
| [0x17628] | its linear start |

then `retf` to the 32-bit entry 0x178c3. PMODE's int 31h (a DPMI 0.9 subset, the host's own under DPMI) is the
only interface the program uses besides plain `int 21h` / `int 10h` (reflected to real mode).

### Services used (complete: `rep.py ints` and the full run)

| service | where | what |
|---|---|---|
| int 31h 0501 / 0502 | 0x17d62 / 0x17da0 | allocate / free one memory block of [0x17b01] = 0x2d0000 bytes (2.9 MB, set at 0x31; "2.9Mb of free high memory" in the NFO); KARL's memory manager (alloc 0x17bd7, free 0x17c8e, resize 0x17cd2, size 0x17cce) works inside it ("Not enough extended memory.", "Memory error.") |
| int 31h 0000 / 0007 / 0008 / 0009 / 0001 | 0x17da3, 0x17e04, 0x17de6, 0x17dfd | data / code selectors for buffers (alloc + base + limit + access rights of DS / CS), set base, free; used by the 3D engine and some parts (selector in [0x55918] etc.) |
| int 31h 0006 | 0x17937 | base of the environment segment from PSP:2C |
| int 31h 0204 / 0205 | 0x17978 (int 0 → 0x179ec "Division by zero", int 0xd → 0x179f8 "General protecion fault ;) ;)" with a register dump), 0x4a620 (IRQ handlers: IRQ n → int 8 + n, or 0x68 + n for n >= 8) | protected-mode vectors |
| int 31h 0200 / 0201 / 0303 / 0304 | 0x4a620.. | real-mode vectors and real-mode callbacks: every IRQ handler the program installs also gets a real-mode stub that reflects the IRQ to protected mode |
| int 31h 0300 | 0x1794a (register block at 0x17630) | real-mode int 21h: 3D02 open, 3F read, 3E close, 09 print |
| int 21h | 0x8a 2C (time: [4] = minute & 1), 0x17915 51 (PSP), 0x4d031.. 42 (seek), 0x29f9b 02, 0x178e0 4C | reflected by PMODE |
| int 10h | 0xe4 AX=13 (mode 13h), 0x93d AX=1A00 (display combination: is a monochrome adapter the second display?), 0x470 / 0x17a0f AX=3 (text mode) | |

### Ports

| port | where | use |
|---|---|---|
| 0x40, 0x43 | 0x4abd4 | PIT channel 0, mode 3, count 0x4286 (17030: 70.064 Hz) |
| 0x20, 0x21, 0xa0, 0xa1 | handlers, 0x7d / 0x3c, player | EOI; IRQ 1 (keyboard) is masked for the whole demo; the player unmasks the GUS IRQ |
| 0x3c8, 0x3c9 | 0x4a742.., 0x6052.., parts | DAC writes (also `rep outsb` of 0x300 bytes) |
| 0x3da | 0x4a830 and 6 more | vertical retrace waits |
| 0x0a, 0x0b, 0x0c, 0x06, 0x07, 0x82 (0xd4.. for 16-bit channels) | 0x1a7a6 | 8237 DMA for sample uploads to the GUS |
| 0x240..0x24f, 0x340..0x347 | LCA's player | GUS (ULTRASND=240,3,3,7,7): mix control 0x240, IRQ status 0x246, timer control 0x248 / 0x249, IRQ/DMA control 0x24b, register control 0x24f, voice select 0x342, register select 0x343, data 0x344 / 0x345, DRAM 0x347; reads of 0x340 are delays |
| 0x2x6, 0x2xa, 0x2xc, 0x2xe | the player's card detection | Sound Blaster probe: reset / read / write / status at every base 0x210..0x280 (also 0x246, inside the GUS's range; nothing answers in the headless run) |

No CRTC, sequencer or graphics controller writes: the demo uses plain BIOS mode 13h (320x200, 256 colours, chain-4,
one page at 0xa0000). Everything is drawn into buffers and copied to 0xa0000 (or drawn there directly) after a
retrace wait.

### "Dual monitor support"

0x939: `int 10h AX=1A00`; if BH (the inactive display) = 1 (MDA), [0x7b0] = 1: the MDA text screen at 0xb0000 is
cleared and framed with lines of 0x0fdb blocks, a performance display ("SCORE:", "SCORE -> MIN : MAX :", 0xbe2,
0xc5e) is drawn and the PIT callback 0x83e ([0x4ab0a]) updates it every tick (a moving bar from [0x7d3] and two
history arrays at 0x488 / 0x618); at the end 0x7dd shows a text page from 0x7db (`%` and `#` switch the attribute).
Debug output only; nothing on the VGA screen depends on it. The headless run answers BH = 0 (no second display).

### Program flow (entry 0x178c3)

1. 0x31: [0x17b01] = 0x2d0000; 0x17b56: allocate the 2.9 MB block, set up the memory manager (error → message, exit).
2. [0x1762c] = DS (the data selector the interrupt handlers load); 0x17978: exception handlers (int 0, int 0xd).
3. main 0x4d (below), then 0x3c (restore the IRQ handlers, unmask the keyboard), 0x17b91 (free), exit 0.

main 0x4d:
- 0x4ab7d: the PIT handler 0x4ac1f on IRQ 0 with three empty callbacks; 0x4abd4: PIT count 0x4286 (70.064 Hz).
- 0x4a98b: open STARS.EXE (floppy check, error 8 = the floppy message at offset 8).
- 0x1ba6c: LCA's player: detect the card (GUS: ULTRASND= or hard detection; else SB; [0x1c802] = 0x20 for a GUS,
  1..4 for SB types, 0 none) and set it up. No card: 0x49ff4, [0x49fef] = 1 (no sound), PIT callback 3 = 0x4a015,
  which advances the music position from the PIT instead (see "Music").
- mask IRQ 1 (keyboard); 0x939 (dual monitor); `int 21h AH=2C` → [4] = current minute & 1 (see "Variants").
- With sound: allocate 0x177000 bytes → [0], load file 1 (the module) and convert it (0x1c71b with EAX = -1: GUS →
  0x1a1b0 converts the patterns and uploads the samples by DMA; SB → 0x182d5), shrink the block, free the file.
- mode 13h; 0x16945 (precalc for later parts, files 50 and 49), 0x5b84 ([0x5426] = [4]; intro set-up, files 5, 6,
  7); 0x1c759 start the music (GUS → 0x1a91c); then the parts, see PARTS.md. Every part is a routine called from
  main; main waits between them with `call 0x29f80; cmp ax, <order:row>; jbe` (music position, order in AH).
- At the end: 0x4a843 delays (retrace counts), 0x1c77e stop the music, free the module, 0x4a006, fade out, 0x7dd
  (MDA end page), text mode, return.

### Timing

Three clocks, all hardware-driven:

1. **The PIT** (IRQ 0, 70.064 Hz, handler 0x4ac1f): calls three callbacks and sends EOI (the BIOS clock is not
   chained, DOS time stands still during the demo). Callback 1 [0x4ab06] is the current part's animation step
   (set by 0x4abec, reset to `ret` 0x4abb4 by 0x4abfe); callback 2 [0x4ab0a] the MDA display (0x4abf2); callback
   3 [0x4ab0e] the no-sound position counter (0x4abf8). Parts advance their animation counters there (example: part
   0x11e7f's callback 0x12136 counts ticks, the camera key [0x121ac] changes every 0x8c ticks and is interpolated by
   the tick count [0x121b0] in 0x121b4), so their motion is frame-rate independent.
2. **The vertical retrace** (70.086 Hz): 0x4a830 waits for the start of the vertical sync and then for its end.
   Parts call it once per drawn frame; the picture fades (0x17e96) step once per retrace.
3. **The music** (GUS timer IRQ, see below): main sequences the parts by the music position (0x29f80), and parts
   test it to end or to switch phases.

Frame rates (frames drawn per second) therefore depend on the CPU, while motion uses the PIT and the music. What
moves per drawn frame instead is listed per part in PARTS.md (measured with `--freeze-pit`).

### Music: LCA's MOD-Player V3.02 (GUS path)

- Module (file 1): a 16-channel MOD variant: 20-byte title (empty), 31 instruments of 30 bytes (names carry a
  message: "Groo/CNCD July 1995", "Hi Olivier. So, this / is part 1 and the / second part should / begin right
  away after / this one - so no / break between them. / Hopefully everything"), song length 155 (byte 950), restart
  0 (951), **256-byte order table** (952..1207, patterns 0..0x48 = 73 patterns), signature `16CH` at 1208 (0x4b8),
  patterns from 1212 (16 channels x 64 rows x 4 bytes), then 8-bit signed samples (480,340 bytes uploaded).
- 0x1a1b0 (GUS conversion, "Converting module..." — the player's print routine 0x29f88 is a `ret`): channel count
  from the signature ([0x1c807] = 16), GUS voices [0x29b8e] = 2 x channels = 32 (register 0x0e = 0xdf: 19.3 kHz
  output rate), patterns converted to 6-byte events, samples uploaded to DRAM by DMA channel 3 (0x1a699 / 0x1a7a6,
  64 KB bounce buffer below 640 KB found by 0x29fb4) with IRQ 7 (handler 0x1a8e2) signalling the end of each block.
- 0x1a91c (start): GUS IRQ 7 handler 0x1abb0; timer 1 count (reg 0x46) = 6 (125 BPM), but order 0 row 0 sets
  speed 4 (`F04`) and tempo 99 (`F63`) and the player switches to **timer 2** (reg 0x47) with only its IRQ enabled
  (reg 0x45 = 8; the handler writes 0x45 = 0, then 8, every tick). Timer 2 counts in 320-us steps:
  - orders 0x00..0x0a (the intro): tempo 99, count 0xb2: (256 - 0xb2) x 320 us = 24.96 ms = **40.064 Hz** (2.5 s / 99
    = 25.25 ms, truncated to the timer's steps: 1.2 % fast); speed 4 (10.0 rows/s); order 0x0a row 53 sets speed 15;
  - from order 0x0b (`F73` + `F02`, repeated in most patterns): tempo 115, count 0xbd: 67 x 320 us = 21.44 ms =
    **46.64 Hz** (2.5 s / 115 = 21.74 ms: 1.4 % fast); speed 2 (23.3 rows/s).
  The player rewrites reg 0x47 at every tempo command (82 writes, two values). AdLib timer control 0x248 / 0x249 =
  4 / 3 starts the timers. The music therefore runs on the GUS crystal, not the PIT: a remake must use these periods.
- Player tick 0x1abb0: acknowledges (reg 0x45 = 0, then [0x29b8c]), `dec [0x1c82d]` (tick in row), per channel
  (structures of 0x34 bytes at 0x29048) effects via the jump table 0x1ac04, and on a new row (0x1b210) reads the
  next events (0x1b298 table); EOI.
- Position: [0x1c829] = order, [0x1c828] = row; 0x1c7a3 returns AL = order, AH = row; 0x29f80 swaps them (AX =
  order << 8 | row) — this is what main and the parts compare against.
- No sound: callback 0x4a015 adds [0x49ff0] to [0x49ff2] every PIT tick; each carry is a row (64 rows per order).
  main sets [0x49ff0] = 0x2492 (70.064 x 0x2492 / 0x10000 = 10.0 rows/s, as the module's intro) and 0x7d00 after
  the intro (34.2 rows/s, against the module's 23.3: without sound the demo runs faster from there on).
- Other entry points: 0x1c71b (load / convert), 0x1c759 (start: GUS 0x1a91c, SB 0x1867c), 0x1c77e (stop: GUS
  0x1a9a2, SB 0x1873c), 0x4a006 (?).

### Variants: the minute of the clock

`int 21h AH=2C` at start: [4] = minute & 1 (also [0x5426]). It swaps two title cards of the intro (0x5c32 / 0x5c47:
the cards at 0x542a / 0x5437, "BARTi Atomic" and "KARL Powerful") and the order of two credit pictures at the end
(files 0x35 BARTI and 0x36 KARL, 0x2ed..0x302). The capture uses minute 0 (even): intro BARTi first, credits KARL
first. The remake should pick one (or use the clock like the original).

## Running the original headless (run_stars.py)

- No PMODE kernel: `stars_img.bin` is loaded at linear 0 with flat selectors 0x08 (code) and 0x10 (data), so the
  code32 base [0x17620] = 0 and near = linear. Preset like the kernel does: [0x17620] = 0, [0x17624] / [0x17628]
  = free DOS memory 0x840b0..0x90000, ESP = 0x840b0, entry 0x178c3. PSP at 0x90000 (environment segment 0x9020 with
  `ULTRASND=240,3,3,7,7` and the program name `C:\STARS\STARS.EXE`), GDT at 0x91000, IRQ return sentinel at 0x92000
  (with `iretd` stubs below it as the "old" vectors). 8 MB of RAM; the 0501 block is at 0x100000.
- PMODE services emulated: int 31h 0000/0001/0006/0007/0008/0009 (descriptors are written to the GDT),
  0200/0201/0204/0205, 0300 (int 21h on STARS.EXE: open / read / seek / close; int 10h), 0303/0304 (fake callbacks,
  never called), 0501/0502; int 21h 02/09/2C/30/42/4C/51; int 10h 00/0F/1A00 (BH = 0: no MDA); int 16h (no key).
- Faked hardware: PIT channel 0, PIC masks, VGA DAC (write and read index), 0x3da, keyboard (0x60 = 0), the 8237
  DMA controllers, and a GUS at 0x240 (`Gus`, from jizz/re/run_jizz.py: register file, 1 MB DRAM via 0x43 / 0x44 +
  0x347, voice register reads return what was written with the "stopped" bits set; plus the timers: reg 0x45 / 0x46
  / 0x47, AdLib control 0x248 / 0x249, IRQ status 0x246; DMA: reg 0x41 bit 0 copies the programmed 8237 transfer to
  DRAM at reg 0x42 << 4 at once, sets the DMA IRQ bit (cleared by reading reg 0x41) and raises IRQ 7 if reg 0x41 bit
  5 is set).
- Time base: the PIT clock (1,193,182 Hz). Main-line code runs at `--ips` instructions per second (default 40 M, see
  below) in quanta of ips/70/32 instructions; interrupt handlers run instantly (not charged). Events: PIT IRQ 0 every
  0x4286 clocks; GUS timer 1 every (256 - reg 0x46) x 80 us and timer 2 every (256 - reg 0x47) x 320 us while
  started, IRQ 7 if enabled in reg 0x45; the VGA frame of 449 lines at 70.086 Hz (0x3da bit 3 = vertical sync during
  lines 412..413, bit 0 = display disabled during lines 400..448). Interrupts are delivered between quanta when IF
  is set and the PIC does not mask them. Polling 0x3da: each read within a quantum advances the time by 4
  instructions; 16 equal reads in a row end the quantum (half of it is charged) and the clock jumps to the next change
  of the value.
- Records (`.frames`, `.ticks`): one per displayed frame, taken at the start of the vertical sync. The program draws
  or copies its next picture after waiting for the sync, so VGA memory at that moment holds what the monitor showed
  during the frame's 400 lines; the record's time (`.ticks` column 2) is that frame's line 0, 412/449 of a frame
  earlier. Per record: 64000 bytes of VGA memory + 768 bytes of DAC palette (6-bit); `.ticks`: frame, time, PIT
  ticks, GUS player ticks, order, row, main routine, PIT callback 1, frames drawn so far (vertical syncs the program
  waited for), main EIP. `.gus`: register writes `<player tick> v<voice> r<reg> <value>`, global registers as `g r<reg>`,
  ports as `p<port>`; `.dram`: final DRAM.
- `--snap-at parts --snap 'snaps/f{frame}_{part}.snap'`: a snapshot at the first frame after each call in main
  (pickle: 'mem' 8 MB from linear 0 = near pointers, 'ctx', 'st' (harness state, incl. PIT/GUS timer times), 'gus',
  'dma', 'pal', 'eip'); `--load --snap file` resumes. `--freeze-pit` (IRQ 0 not delivered: the PIT callbacks stop) and
  `--part-ips ADDR=IPS` are for frame-rate analysis.
- Speed: 40 M instructions per second, chosen as a 486DX2/66 (the NFO's recommendation) or a little faster: a
  486 needs one to two clocks for most of these instructions, and unicorn counts each iteration of a `rep movsd` as
  one instruction, which is about what a 486 spends per dword on a VLB/PCI card. See PARTS.md for the frame rates
  this gives and which parts depend on them.
