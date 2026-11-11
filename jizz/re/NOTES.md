# JIZZ (The Black Lotus, Wired '97 64k intro) — reverse-engineering notes

Code/design Jace, music Probe (".mp4 cruncher"), vector engine and blobs Nix, VESA code Ballein. MS-DOS, DOS32
v3.5 extender, VBE 2.0 hicolor with a linear frame buffer, music by MXMPlay on a Gravis UltraSound.

## Files and tools

All tools run inside the toolbox (`toolbox run -c acme303 python3 ...`) from this folder. Generated files are made
from the original and stay out of Git.

| file | what it does |
|---|---|
| `unadam.py ../original/JIZZ.EXE jizz` | unpacks the DOS32 "Adam" executable exactly as the DOS32 kernel loads it: runs the kernel's own loader routine (stub 0x115b: decompress + relocate + zero BSS) in unicorn, twice (base 0 and base 0x100000, to check the relocations), and checks an independent Python reimplementation gives the same bytes. Writes `jizz_img.bin` and `jizz_unp.txt`. |
| `rep.py` | capstone helper on `jizz_img.bin`: `func / dis / calls / xref / funcs / str / ints` |
| `run_jizz.py --out full` | runs the whole intro headless in unicorn (see "Running the original headless") |
| `sheet.py full.frames out.jpg --every 2 --cols 6 --scale 0.5` | contact sheets of a capture (or `sheet.py full.frames out.jpg T1 T2 ...`) |
| `frames2mp4.py full.frames out.mp4` | constant frame rate video of a capture (default 60 fps, from the music start) |
| `fpscheck.py A B --tick0a TA --tick0b TB` | compares two runs at different `--budget`, frame by frame at equal xmpGetTimer value, to find frame-rate dependent state |
| `NOTES.md`, `PARTS.md` | these notes; the part timeline |
| generated (not in Git) | `jizz_img.bin`, `jizz_unp.txt` (header values, hashes, fixup list), `full.frames/.ticks/.flips/.gus/.dram/.log` (the capture), `snaps/t*.snap` (start of each part), `overview.jpg` (one frame every 2 s of music time), `jizz_reference.mp4` (60 fps, 960x720, no sound, from the music start), `dump/` (the files the two dump modes write) |

`jizz_img.bin`: 208,896 bytes (0x33000 = the memory size: program 0x2a617 bytes + zeroed BSS),
sha256 `f9ab8054e07fd5fc6b167e34c4848738c6a8de45793bc2ed8411ae6db9eed559`; the program part alone (0x2a617 bytes)
sha256 `e56381edbc159d946edf040e2f21dadb4e3470a81334da7b95434a81447d92f9`.
Addresses in these notes are offsets in this image = near addresses of the running program (loaded at base 0).

## File layout (JIZZ.EXE, 63,960 bytes)

- 0x0000..0x2330: MZ stub = DOS32 v3.5 rev 6 by Adam Seychell (header 0x20 bytes, image 0x2310 bytes, entry
  0000:13f8). "unregistered" banner text at stub image 0x20..0x7f.
- 0x2330: Adam header (0x2c bytes, the kernel reads it to [0x4760]): 'Adam', dlink version 0x0350, min DOS32
  0x0200, contents 0xd67c, image size 0xd6a8 (= header + contents), header size 0x2c, EIP 0x186bd, memory size
  0x33000, ESP 0x32614, program size 0x2a617, flags 0x00050000 (byte [0x4786] = 5: bit 0 compressed, bit 2 no
  banner), 4 bytes 0.
- 0x235c..0xf9d8: contents, 54,908 bytes, compressed. They decompress to 0x2b351 bytes: the program (0x2a617)
  followed by the relocation stream (0xd3a bytes).

### The DOS32 v3.5 loader (stub image offsets, 16-bit code with 32-bit registers, DS = CS)

- 0x0f2b reads the MZ header, seeks to the MZ image end, 0x110a reads the Adam header; 'Adam' check 0x0f58.
- 0x0f82: flags bit 2 set ([0x4786] & 4): no banner. Clear: `mov byte [0x60], '$'` terminates the banner
  string (stub 0x20..0x5f) and the banner is printed. [0x4785] (flags byte 1) is a delay after loading in units
  of 0xe9/128 BIOS ticks (0x1001; 0 here).
- 0x0fcc version must be 0x350; 0x0fe5 int 31h AX=EE42 allocates [memory size] → [0x475c] = base (near).
- 0x115b load: flags bit 0 clear → read contents raw; set → 0x119f decompressor (needs min version >= 0x140).
  Input through an 8 KB buffer at 0x2420 refilled by 0x1080 (0x2000 bytes per int 21h AH=3F via the real-mode
  call helper 0x1ba2 = int 31h AX=0300; also polls Ctrl-Break at BIOS 0x471). Output to es:[edi], es = [0x13a8].
  Bit buffer BP, 16 bits, LSB first; a new little-endian word is loaded as soon as the 16th bit has been taken
  (so the word comes before the bytes that follow it in the stream, LZEXE style). The first bit is a dummy that
  loads the first word. Codes (offsets are negative, copy byte by byte so they may overlap):

  | bits | meaning |
  |---|---|
  | `1` | literal: next byte |
  | `0 0 0`, byte b != 0xff | copy 2 from offset b - 0x100 |
  | `0 0 0`, byte 0xff, `1` | no-op (continue) |
  | `0 0 0`, byte 0xff, `0` | end of data |
  | `0 0 1`, byte b, 3 bits h (MSB first) | copy 2 from offset ((0xf8 \| h) - 1) * 0x100 + b - 0x10000 |
  | `0 1`, byte b, high part, length | high byte bh starts at 0xff: bh = bh<<1 \| bit; if the next bit is 0: dh = 2 and up to 3 times { bit 1 → stop; else bh = bh<<1 \| bit, dh <<= 1 }, bh -= dh. Offset (bh<<8 \| b) - 0x10000. Length: `1`=3, `01`=4, `001`=5, `0001`=6, `00001` then `0`=7 / `1`=8; `00000` then `1` + byte = byte + 17 (17..272), `00000 0` + 3 bits (MSB first) = 9..16. |

  The stream also ends when the file data runs out (0x1080 with nothing left jumps to 0x12f4).
- 0x12f4 relocation: the stream after [program size] in the output. Per entry: bytes rotated left by 5 (by 0 when
  the banner was printed, i.e. [0x60] == '$'), bit 0 = more bytes follow, bits 1..7 = 7 bits of the displacement,
  most significant group first; the displacement is added to the previous site (starting at base). If the next
  byte is 0 (consumed) the site gets the 16-bit data selector (`mov es:[edi], es`), otherwise base is added to the
  dword at the site. JIZZ: 3347 offset fixups, 1 selector fixup at 0x186bf (the `mov ax, 0x18` at the entry).
  (RetroLinker's dosexe.cc reads the same stream from an uncompressed v3.5 file, as nibble-swapped bytes.)
- 0x1350 zero-fills from program size to memory size (this also wipes the relocation stream).
- 0x1028 jumps to the program: ds = es = fs = gs = ss = [0x13a8] (data selector, 0x18 in the stub), esp = esp +
  base, `jmp far [0x13aa]:(eip + base)` (code selector 0x20). Both selectors have the same base and a 4 GB limit.

## DOS32 runtime model (as the intro uses it)

- One code and one data selector with the same base ("selector base"); near pointers are offsets from it. The
  program sits at near [EE42 result]; unadam.py relocates it for near 0, which is what the VESA code expects
  ("VBEPMEXT buffer ... has GOT to be at offset 0": 0x5941 copies the VBE 2.0 protected-mode interface table to
  near 0, over the start of the image).
- int 31h AX=EE02 returns EBX = selector base (linear address of near 0), ESI = near pointer to the PSP,
  EDI = near pointer to the environment. The intro converts linear → near by subtracting EBX:
  [0x2024] = EBX, [0x201c] = [0x208e] = 0xa0000 - EBX (VGA window, used only by the banked paths),
  DOS memory segment * 16 - EBX, LFB linear - EBX → [0x56bf].
- Memory: EE42 (0x2036; EDX = size → EDX = near pointer, CF on error) for one 8,468,416-byte block (0x8137c0,
  "couldn't alloc 7900Kb XMS") carved up at 0x18738.. into textures (0x30000 each = 256x256 planar R, G, B),
  the music buffer [0x6546] (1 MB: module + MXMPlay data at +0xfc000), screen buffers etc.; a second EE42 of
  0x26400 bytes (0x6337) only when there is no LFB or the mode is 640 wide. EE40 (0x203e) exists but is not called.
  int 21h AH=48 BX=0x20 (0x56d3): 512 bytes of DOS memory for the VBE info / mode info blocks → [0x5683].
- The command tail is read from PSP+0x80 (see "Dump modes"), ULTRASND from the environment.
- No DPMI interrupt reflection is needed beyond int 31h AX=0300 (simulate real-mode interrupt, BL = 0x10 for
  all VBE calls) and plain `int 10h` (DOS32 passes it through): 4F07 BL=1 and 4F05 BH=1 (probes, 0x5976 / 0x5992),
  AH=3 / AH=2 (cursor for the percentage display, 0x1114e / 0x1115c), AX=3 (text mode at exit, 0x18c5e).

## Services used (complete list from the image and a full run)

| service | where | what |
|---|---|---|
| int 31h EE02 | 0x186d0, 0x1891f, 0x2a314 | selector base (EBX), PSP (ESI), environment (EDI) |
| int 31h EE42 | 0x203b (via 0x2036) | allocate (0x18727: 0x8137c0; 0x633c: 0x26400 without LFB) |
| int 31h 0300, BL=10h | 0x5720 4F00, 0x57fe / 0x5ba8 / 0x5bd6 4F01, 0x5916 4F0A, 0x5a3c 4F02, 0x5d89 4F07, 0x5dcd / 0x5deb 4F05 | VBE through real mode; register block at 0x568d (ES = [0x5683], DI = 0) |
| int 31h 0800 | 0x58b8 | map the LFB (PhysBasePtr, size = video memory KB * 1024, capped at 2 MB) |
| int 31h 0204 / 0205 | 0x29efc / 0x29f1a, restore 0x29f37 | get / set the protected-mode vector of int 8 (IRQ0) |
| int 21h 09 | many | text output (banner, progress, credits at exit) |
| int 21h 48 | 0x56d9 | DOS memory for VBE blocks |
| int 21h 3C / 40 / 3E | 0x652e / 0x653c / 0x6537 | create / write / close, only in the dump modes |
| int 21h 4C | 0x2046, 0x59a7, ... | exit |
| int 10h | 0x5976 4F07 BL=1, 0x5992 4F05 BH=1, 0x11152 AH=3, 0x1116f AH=2, 0x18c63 AX=3 | see above |

No int 16h, int 33h, no keyboard handler. The key test 0x2058 is `or al, 1; ret` (never "pressed"), so the
party version cannot be left with a key; [0x205b] (space = skip to the next sync event, 0x63e9) is never written.

## Ports

| port | where | use |
|---|---|---|
| 0x43, 0x40 | 0x29f1c.. (install), 0x29e84.. (every IRQ), 0x29f39.. (restore), 0x29f44.. (read) | PIT channel 0: mode 2 (0x34) + count; latch (0x00) + 2 reads in xmpGetTimer |
| 0x20 | 0x29e67 | EOI in the timer handler |
| ULTRASND base + 0x000 | MXMPlay init | GUS mix control (3 writes) |
| base + 0x102 / 0x103 / 0x104 / 0x105 | MXMPlay | voice select, register select, data low (word = 16-bit register), data high |
| base + 0x107 | 0x2a2be (upload), delays | DRAM data (DRAM address via registers 0x43 / 0x44) |
| 0x3da | 0x5d94, 0x5e0d | retrace wait, only when [0x205f] != 0 (never: the flips use 4F07 BL=0x80 instead) |

The GUS is never probed for its port: only the ULTRASND variable gives it. Reads during a full run: 0x40 (PIT
latch), base+0x104 / 0x105 (voice registers 0x80 voice control, 0x89 current volume, 0x8d ramp control),
base+0x107 (790k reads: 0x29fe8 is MXMPlay's GUS delay, 7 reads of the DRAM port), base+0x103 once (0x2a26f: an
"already uploaded" check, 0x343 == 0x45 and 0x342 == 0x1e and 0x347 == 0x2a would skip the sample upload).

## Video

- 0x56c3 VBE detection: 4F00 into DOS memory ('VESA', version >= 1.02, video memory >= 1024 KB — the message
  says 512kb), copies 0x1400 bytes of the mode list to 0x209e and queries every mode with 4F01; keeps modes with
  attributes 1+8+0x10 in a table at 0x34cd (8 bytes: width, height, bits (sum of the mask sizes above 8 bpp),
  mode number, attributes; 640-wide hicolor modes without LFB are dropped when WinBSegment & 3 != 0). The last
  PhysBasePtr found is mapped with 0800 → [0x56bf]. 4F0A (protected-mode interface) is copied to near 0 if
  available: [0x2086] = set display start, [0x208a] = set window.
- 0x5a50 mode choice, first available of 320x200x16, 320x200x15, 640x400x16, 640x400x15, 640x480x16, 640x480x15
  ("no 320x200xhicol, 640x400xhicol or 640x480xhicol found!!"). A 320x200 hicolor mode keeps its LFB only if
  BytesPerScanLine = 640. Results: [0x207c] mode (| 0x4000 with LFB), [0x2070] / [0x2074] width / height,
  [0x206c] bytes per line, [0x207f..0x2084] red/green/blue field positions and sizes, [0x2085] flags (bit 0 =
  15 bit, bit 1 = banked, bit 2 = 640 wide: the intro then draws 320x200 into a buffer and scales it; JIZZ.DOC's
  "fake the shit on 640*400 or 640*480").
- 16-bit 5:6:5 is preferred and works. The pixel format is not hard-coded: 0x1f5b8 (called at 0x189cb) builds
  the colour tables from the mask positions, so 5:5:5 works the same way.
- 0x6305 (at 0x18bdf): [0x2064] = 320, [0x2068] = 200, [0x2060] = LFB, 4F02 via 0x5a04.
- Double buffering with 4 pages of 128000 bytes (0x1f400) in video memory (512 KB): every frame starts with
  0x638b (page = ([0x2052] + 1) & 3, [0x2060] = LFB + page * 0x1f400 in 0x5e1d), draws into [0x2060], and ends
  with 0x651f → 0x5e44 → 0x5e71: 4F07 BL=0x80 (set display start during vertical retrace) CX = 0, DX = page * 200.
  So every frame waits for a retrace inside the VBE BIOS (frame rate <= refresh rate).

## Music and timer

- The player is MXMPlay (Niklas Beisert's XM player for the GUS, "couldn't init mxmplay"), code 0x296dc..0x2a316,
  data block at [0x6546] + 0xfc000 (found by 0x296dc through [0x296d8]). Entry points used: 0x29738 xmpInit
  (ESI = module, EAX = GUS port or 0 = nosound, EBX = data block, ECX = max timer period 0x10000), 0x29db1
  xmpPlay (EAX = start order), 0x29e22 xmpStop, 0x29f44 xmpGetTimer, 0x299c4 xmpGetPos (AL = row [+6],
  AH = order [+0x12]).
- Module: 32,625 bytes of crunched data at 0x1f644 ("mp4" cruncher by Probe) are copied to [0x6546] and
  expanded by 0x1632b to 928,668 bytes of MXM ("extracting music"): 22 orders, 14 channels, 17 patterns,
  37 instruments, speed 6, 151 BPM, linear frequencies, 909,056 bytes of 8-bit samples (fits a 1 MB GUS).
  Patterns use pattern delays (order 0 has rows of 6 and 180 ticks), so rows are not evenly spaced.
- GUS detection: 0x2a2cd scans the environment for `ULTRASND=2x0,` (needs "=2" and "0,"; skipped if the
  character after the comma is '@') → port 0x2x0. No variable → "error! -> switching to nosound", xmpInit with
  port 0. "uploading samples to gus" = xmpInit (0x2a26f uploads byte by byte through 0x347).
- Timer: xmpPlay sets the period [+0x42] = 0x2d86bf / BPM PIT clocks (0x29e0b; Fxx >= 0x20 recomputes it at
  0x29a24) and installs the IRQ0 handler 0x29e38 on int 8 (0x29ed1; first period 0x10000). Each interrupt:
  [+0x3a] += [+0x36] (elapsed), [+0x32] += [+0x36]; when byte [+0x34] becomes non-zero (64K clocks elapsed) the old
  int 8 handler is called (BIOS clock, 18.2 Hz on average); EOI; remaining [+0x46] -= 0x10000, if that is not
  positive a new player tick starts with remaining = period; the PIT is reprogrammed (mode 2) with
  min(remaining, 0x10000); at the start of a player tick it runs 0x2a13c + 0x29b26 (player) on its own stack
  unless re-entered. With 151 BPM (constant through the whole song) the period is 19,759 clocks: 60.39 Hz, one
  interrupt per player tick.
- xmpGetTimer (0x29f44) = [+0x3a] + [+0x36] - PIT counter (latched): PIT clocks (1,193,182 Hz) since the
  start, interpolated inside the current period. All animation that uses time reads it (24 call sites); parts
  keep a start value in [0x6542] (time = xmpGetTimer + [0x6542]).

## Program flow (main = 0x186bd)

1. Entry: DS = ES = 0x18, EE02 → [0x2024] = selector base, [0x201c] = VGA; banner (0x1853c); 0x56c3 VBE
   detection; 0x5a50 mode choice; FPU init with the control word's precision field cleared (0x18712: `and cw,
   0xfcff` → 24-bit single precision for all x87 maths); EE42 8.1 MB, zeroed, divided into buffers (0x18738..).
2. Precalc (text mode): 0x6592, 0x6b16 (1/sqrt table), 0x6cdd, 0x10fa2 (1024*sin table at 0xfb9e), 0x11dfe
   (uses the random generator 0x141f6), 0x16f4f, 0x180e5, 0x18228; command tail check (dump modes); 0x1f5b8 colour
   tables from the VBE masks.
3. "extracting music:" 0x1114e (remember the cursor) + copy of the crunched module + 0x1632b; "/mp4": save.
4. "uploading samples to gus...": EE02, ULTRASND, xmpInit.
5. "decrunching gfx:" 0x12c50: 17 texture builds (16 names, map14 twice) of 256x256 planar RGB, each by 0x12e72
   from a script at ESI into a 0x30000 buffer (procedural maps and the two logo pictures; JIZZ.DOC credits Jace's
   ".jpg (JacePeG) cruncher" — which builds use it is not checked yet); progress 0..0x272 steps through 0x1115c;
   "/pandora": each is written as a TGA (0x14258). The strings "you think THIS is boring??", "wait till you see
   the intro!", "almost done" (0x184c7..) are not referenced by any code.
6. 0x6305 sets the video mode, xmpPlay(0) at 0x18be9 starts the music and the timer (about 24 s after start at
   100 M instructions/s: the precalc is CPU bound).
7. The parts, called one after the other from main: 0x12013, 0x17003, 0x169e7, 0x15c8b, 0x1212a, 0x15d20,
   0x177bf, 0x17c8a, 0x10fda; then 0x17979 (start a timer-based fade) and the end loop 0x18c25 (copies the end
   picture [0x657a] to the page, 0x17999 fade, flip) — see PARTS.md.
8. When the sync table reaches its end (-1), 0x638b jumps to 0x18c59: xmpStop (restores int 8 and the PIT),
   text mode, banner + credits, exit 0.

### How parts end: the sync table

0x638b (start of every frame) compares xmpGetPos (order << 8 | row) with the table at [0x651b] (initially 0x6417,
4-byte entries: word position, signed word event). Each time the position reaches the next entry, [0x6413] = its
event and the pointer advances; event -1 exits the intro. Parts poll [0x6413] once per frame and return when it
reaches their end value (0x12013: == 100; 0x17003: >= 400; ...); some parts overwrite [0x6413] themselves to mark
sub-steps. Table (order:row → event):

```
00:00 0    00:08 10   00:30 50   01:00 100  01:20 120  01:28 128  01:34 130  02:00 201  02:10 210  02:12 215
02:14 210  02:16 215  02:18 210  02:1a 215  02:1c 210  02:1e 215  02:20 220  02:22 215  02:24 220  02:26 215
02:28 220  02:2a 215  02:2c 225  02:2e 230  03:00 301  03:10 311  03:20 315  03:30 320  04:00 400  04:10 410
04:14 400  04:20 410  04:24 400  04:30 410  04:34 400  05:00 500  05:10 515  05:18 520  05:20 525  05:28 520
05:30 535  05:38 520  06:00 600  08:00 800  08:10 810  08:20 820  08:30 830  09:00 900  0a:00 2560 0a:10 900
0b:00 2817 0c:00 3072 0c:10 3088 0c:20 3072 0c:30 3088 0d:00 3328 0f:00 3840 10:00 4096 11:00 4352 13:00 4865
14:00 5120 14:10 4864 15:00 5376 15:1c -1
```

Because the check runs once per drawn frame (and parts test the event after drawing part of the frame), part
changes lag the music position by up to about two frames: part 0x17003 starts 4 ticks after the music reaches 1:00
at 22 fps, 17 ticks after it at 5.7 fps (25 M instructions/s).

### Frame-rate dependent state

Measured with `run_jizz.py --freeze-timer` (xmpGetTimer returns a constant, so only per-frame or event-driven
state still moves) from a snapshot in every part, and with `fpscheck.py` between runs at 25 / 100 / 200 M
instructions/s. Everything that moves in parts 0x17003 .. 0x10fda is a function of xmpGetTimer and the sync event,
except:

- Part 0x12013 ("TBL"): [0x1210d] fade level starts at -0x104 and steps +9 per frame up to 0 (fade in from black,
  29 frames), at event 50 it steps +0x17 per frame up to 0x100 (fade to white, 12 frames); [0x12012] += 2 per frame
  drives the colour pulse (0x13f98; a triangle wave of 128 frames). At the capture's 22 fps that is 1.3 s, 0.5 s
  and 5.8 s; at 25 M instructions/s (5.7 fps) 5.1 s, 2.1 s and 22 s. The level ([0x1210d] >> 3 → [0x11f72]) is
  the brightness given to 0x11f73.
- The flash in 0x638b: [0x1a478] (brightness of the colour tables, 0x1a229) -= [0x6386] per frame. Part 0x17003
  sets 0xff / 9 at event 311 (03:10): a white flash that fades in 29 frames (0.8 s at 35 fps).
- All part changes and sync reactions happen once per frame (0x638b), so they lag the music by up to two frames.

The music sync, all 3D objects, textures, tunnels, warps and the timer fades (0x17979 / 0x17999) use the timer.
No part is skipped or broken at any of the three speeds.

## Dump modes ("a way to get the intro writing all the gfx and music to disk")

Main compares the command tail at PSP+0x80 (length byte included, characters 3..10 lower-cased with OR 0x20)
with two length-prefixed strings at 0x18d2f and 0x18d39:

- `JIZZ /pandora` (tail exactly " /pandora") → [0x12e45] = 1: while "decrunching gfx", 0x14258 writes every
  generated texture as a 256x256 24-bit TGA (18-byte header at 0x14349, planes interleaved to BGR) under its name:
  map14, map1, map1a, map2, map3, map4, map5, weirdtxt, map7, map8, map9, testshit, logo2, logo1, map10, map14
  (again), map15 (.tga). Write errors: "* File error while writing texture *".
- `JIZZ /mp4` (tail exactly " /mp4") → [0x6541] = 1: after "extracting music" it prints "saving music..." and
  writes the expanded module (928,668 bytes, 'MXM\0') as smokeout.mxm (0x18ad4).

Only one of the two per run. `run_jizz.py --cmdline " /pandora"` (or `" /mp4"`) does the same into `--dumpdir`
(default `dump/`); both were run and produce the files above.

## Running the original headless (run_jizz.py)

- No DOS32 kernel: `jizz_img.bin` (relocated for near 0) is loaded at linear 0 with flat selectors 0x18 (data)
  and 0x20 (code), so near = linear and EE02 returns EBX = 0. PSP at 0x80000 (command tail from `--cmdline`),
  environment at 0x80100 with `ULTRASND=240,3,3,7,7`, DOS memory from 0x81000, EE42 heap from 0x100000
  (32 MB of RAM), old int 8 vector = an `iretd` at 0x7f000.
- Fake VBE 2.0 (2 MB): mode 0x10e 320x200x16 (5:6:5) and 0x10d 320x200x15 (5:5:5), BytesPerScanLine 640, LFB at
  0xe0000000 (0800 maps it 1:1), no 4F0A interface (so the intro uses int 31h/0300 4F07). The intro picks 0x10e
  (16 bit); `--bpp 15` offers only 0x10d. 4F07 BL=0/0x80 sets the visible page; BL=0x80 waits for the next
  vertical retrace (`--refresh 70`, `--no-vsync`).
- Time base: the PIT clock. Main-line code runs at `--budget` instructions per 125-BPM tick (23,868 clocks), i.e.
  budget * 50 instructions per second (default 2,000,000 = 100 M/s, the speed Paper's capture used). The IRQ0
  handler runs (instantly, not charged) whenever the programmed period has elapsed; before xmpPlay installs it,
  ticks run at the BIOS rate (0x10000) with no handler. PIT latch reads return the counter at the start of the
  current quantum (`--quantum`, default budget/64 instructions). A flip with 4F07 BL=0x80 ends the quantum
  (charged as half a quantum, unicorn cannot say how far into it the flip was) and the main line waits for the
  next 70 Hz retrace (`--refresh`, `--no-vsync`), so no part draws more than 70 fps.
- `--freeze-timer` (analysis): xmpGetTimer always returns its first value of the run.
- `--part-budget ADDR=N,...` changes the speed while main is in a part routine (ADDR as in PARTS.md);
  `--snap-at T,.. --snap file{tick}` / `--load --snap file` save / resume snapshots (pickle: 'mem' 32 MB from
  linear 0, 'lfb' 2 MB, 'ctx', 'st' harness state incl. PIT and VBE state, 'gus', 'pal', 'eip').
- Faked hardware: PIT channel 0, PIC, retrace bit, a GUS at 0x240 (the Gus class of paper/re/run_paper.py, plus
  word writes to 0x342 = voice + register select; register file, 1 MB DRAM; reads of voice registers return what
  was written, with "stopped" bits set in 0x80 / 0x8d). MXMPlay reads 0x89 (current volume) before volume ramps;
  the fake answers with the last value written to 0x09, not the ramped one, so the `.gus` log is a register log,
  not a sound render. For the music, `dump/smokeout.mxm` (from `--cmdline " /mp4"`) is the module itself.
- Outputs per tick: `.frames` (32-byte header `JIZZFRM1` + <IIIIII width 320, height 200, bits 16, record 128000,
  header 32, 0>; then the visible page, 16-bit little-endian 5:6:5 pixels), `.ticks` (tick, PIT count, time at the
  end of the tick, order, row, event [0x6413], sync index, part, flips so far, main EIP), `.flips` (per page
  flip: number, tick, time, the first xmpGetTimer value of that frame, part), `.gus` (register writes:
  `tick v<voice> r<reg> value`), `.dram` (final GUS DRAM).
- The full run (`run_jizz.py --out full --snap-at 430,440,1365,2515,3285,4050,4435,5205,5970,7506,9045 --snap
  snaps/t{tick}.snap`): the music starts at tick 434 (23.93 s: the precalc took 434 BIOS ticks), then 8923 ticks
  of 19,759 clocks (the PIT count never changes); the end picture loop starts at tick 9039, the sync table ends
  and the program exits in tick 9357 = 171.69 s program time, 147.75 s after the music start. 5662 frames flipped
  (38 fps on average). The emulation takes about 10 minutes (one core).
