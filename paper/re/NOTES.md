# Paper (Statix / Psychic Link, music Vic / Acme, Wired '96 64k intro) — reverse-engineering notes

## Files
- `../PAPER.EXE`: PKLITE-compressed MZ (63845 bytes). `unpklite.py` runs PKLITE's own stub in unicorn
  (real mode, minimal int 21h) and stops at the original entry: 1000:0316 (load segment 0x1000).
  Output: `paper_unp.mem` (memory from the PSP), `paper_img.bin` (image from the load segment, 0x269e0 bytes).
- `rep.py`: capstone helper on the 32-bit segment (func / dis / calls / xref / funcs). Run inside the toolbox.

## Layout (Tran's PMODE 3.x)
- Real-mode entry 0x316 is PMODE's 16-bit kernel (DPMI/VCPI/XMS/raw detection, A20, PIT reset).
- The 32-bit segment `code32` starts at image offset 0xd00 (segment 0x10d0). All 32-bit code and data use
  offsets relative to it; addresses in these notes are code32 offsets unless marked "img".
- PMODE variables at the start of code32: [0x18] = _code32a (linear base, used to reach absolute memory:
  VGA = 0xa0000 - [0x18]), [0x1c] selcode 0x08, [0x1e] seldata 0x10, [0x20] selzero 0x18, [0x26] / [0x2a]
  IRQ-vector get/set functions (0x935 / 0x968: bl = IRQ, edx = handler), 0x240 = getmem (eax = bytes → eax = ptr).
  `int 0x33` = real-mode interrupt call: al = int number, register image at [0x9a] (ax), [0x9b] (ah) ...
- Program: hand-written 32-bit assembly (no C runtime). Strings at code32 0x4530.. and 0x10c6, 0x5929.

## Main (0x7626)
- 0x1c24 sound init: 0x2256 detects a GUS (else asks for the port or space for no sound), 0x22f8 GUS init,
  IRQ0 handler = 0x1e45 (player), 0x1e32 sets the PIT divisor 0x4295 (70.0 Hz).
  Tune: compact pattern data at 0x7a9b (count byte, word, then per event a flag byte: 0x40 → note (7 bits) +
  instrument/volume/effect nibbles, 0x80 → parameter), unpacked into 5-byte events at [0x1e603].
- 0x5b7d init: banner, reciprocal table 65536/n (n = 1..2047) at 0x1ee19, buffers via getmem, random tables
  (0xaa2 = random), 0x10d3, 0x144b, 0x55e2, 0xe2e, 0xfb2 ("psychic link"), 0xc80 (FPU), sine table
  65536*sin at 0x22479 (1024 entries), VGA pointer [0x1edf1] = 0xa0000 - code32a, mode 13h via int 0x33/10h.
- Frame loop of every part: `call 0x7a64` (ESC check: scan code 1 / 0x81 quits), `call 0x603f` (render), copy the
  back buffer [0x1edf9] to VGA [0x1edf1], key check `in al, 0x60` (scan code 0x0a = key '9' jumps to the coloured
  3D scene at 0x78c8 — only in the early loops), until the music
  counter [0x1ea8b] (advanced by the player) reaches [0x1ea8f]. [0x1e5ef] = current background/effect routine.
- Part targets for [0x1ea8f]: 0x70 (title "psychic link / sixty four k" via text routine 0x2829), 0x80,
  then effect pointers 0x61ee, 0x6267, 0x6199, 0x620b, 0x6170 (scene routines 0x603f / 0x726d / 0x72c4 /
  0x73a6 / 0x3c3e / 0x2b55 / 0x2b6c / 0x5623 / 0x572f), 0x280, 0x2c0, 0x380 (0x687a), 0x400 (0x24df),
  0x500 (0x388b), "t h e   e n d" until 0x53a, then 0x13d6 / 0x1c8e and exit.
- [0x1edb9] / [0x1edbd]: palette fade speed / level (0x8000 used like 303's white level).

## Running the original headless (run_paper.py)
- The 32-bit segment is loaded at linear 0 with flat GDT selectors 0x08/0x10/0x18 (PMODE's _code32a = 0, so
  VGA memory is at 0xa0000). PMODE variables preset: [0]/[4] low memory (none), [8]/[0xc] high memory heap,
  [0x10] PSP (environment with ULTRASND=240,3,3,7,7 — that is how the intro finds the GUS).
- setirqvect (0x968, via [0x2a]) is intercepted: IRQ0 → 0x1e45 (player + palette fade + per-tick effect
  callback [0x1e5ef]), IRQ1 → 0x7a6f (keyboard). int 0x33 (real-mode int) emulated for int 10h/16h/21h.
- The 70 Hz IRQ0 is delivered after every `--budget` main-line instructions (1.5M ≈ a 100 MHz Pentium).
- Faked hardware: VGA DAC (0x3c8/0x3c9), retrace (0x3da), PIT/PIC, keyboard (0x60 = 0), GUS at 0x240
  (register file, DRAM via regs 0x43/0x44 + port 0x347, no auto-increment). Outputs: .frames (VGA + palette per
  tick), .gus (register log per tick), .dram (sample memory), .ticks (music counter, effect pointer, main EIP).
- frames2mp4.py turns .frames into a 70 fps reference video. The music is rendered by player_ref.py --wav (see
  notes-music.md): run_paper.py's fake GUS answers the current-volume read (reg 0x89) with 0, so its .gus log
  ramps every volume change up from silence; gf1synth.py also mishandles loops. Both are kept only for reference.
- Music counter [0x1ea8b] runs at ~8.3 per second; the intro ends at 0x53a (~161 s).

## The skipped paper-plane flock (bug in the original)
- Main calls 0x388b with target 0x500 after the greetings part (0x24df). 0x388b sets the effect callback 0x3884
  and the palette target [0x1edbd] = 0x8000, renders, and loops `while ([0x1edb5] != 0)` (0x397e), fading out
  once the music counter passes the target.
- [0x1edb5] (current palette level) only moves in the 70 Hz IRQ. On a fast machine the precalc (0x33d0, 0x342f)
  and the first frame finish within one tick, the level is still 0 at the first check, and the whole part
  (~30 s of flocking planes) is skipped — which is why many captures miss it. On a slow PC it plays.
- run_paper.py hooks the first loop test (0x397e) and gives the level one fade step if it is still 0. Measured from
  a snapshot: at 1.5M instructions per tick (68 fps in this part) the level is 0 there and the part would be skipped;
  at 1.0M / 0.75M / 0.5M (45 / 34 / 23 fps) it is already 300-600 and the part plays without help. So the skip is a
  fast-machine effect. With it, the part runs ticks 8818-10966 (music counter 1037-1293), then "the end" (0x2829 text
  over the 0x27ae warp) until 0x53a.
- The remake must play this part.

## The flock's speed depends on the frame rate
- The part has no fixed time step: the per-tick callback 0x3884 only counts ticks into [0x1edc1], which the part
  resets but never reads. Everything moves once per drawn frame:
  - 0x342f, the point the flock follows ([0x3390] position, [0x33a8] velocity): velocity += random(-1..1) per axis,
    with a 1-in-64 chance of a new random velocity (-7..7), clamped to -7..7; bounces at +-450 (x, y) and +-14 (z).
    Then per plane (32, 0x20-byte records at [0x33c0]): turn angles [+0x10] / [+0x14] = steering towards the point
    plus random(-63..63) jitter, speed [+0x18] += 12 + random(-3..3). Camera sway angles [0x33c4] += 0x64,
    [0x33cc] += 0x8f, [0x33c8] -= 0x48 (sine table, 0x3719..).
  - 0x3618 applies each plane's turn (0xb1f rotation, 0xae2 matrix multiply), moves it along its forward axis, sums
    the flock centre for the camera and draws it (0x3fb3). 0x3095 draws the canyon (8x8-block interpolated texture
    mapping, 40x25 blocks), 0x2f24 sets up the camera.
- A frame costs ~1.5M instructions here, so the emulator at the default budget draws ~68 fps; a 1996 PC (VGA copy of
  64000 bytes per frame included) would have drawn far fewer, and the target point then wanders more slowly and
  changes direction less often per second. `--budget` sets the machine speed; paper_planes_speeds.mp4 compares
  68 / 45 / 34 / 23 fps (runs resumed from a snapshot at tick 8700, `--snap-at` / `--load`).
- Chosen speed: 34 fps (`--plane-budget 750000`, applied from the tick after the part starts), which matches how
  the scene is remembered from real hardware. paper_reference.mp4 = a `--whole-frames` run of ticks 0-8699 at the
  default budget joined with a run resumed from the tick-8700 snapshot with `--plane-budget 750000` (full.log has
  both logs). The remake should step the flock (0x342f + 0x3618) at ~34 Hz, not per display frame.
- `--whole-frames` records the last fully copied frame per tick (grabbed at 0x7a64, which every part loop calls once
  per frame), so captures of slower runs never show a half-copied frame.
- Measured frame rates per part at the default budget (frames / ticks * 70): title warp 77, pencil sketch 96, 620b 72,
  6439 100, greetings 47, planes 68, the end 77. The part at 0x5623 (callback 0x561c) steps its animation per tick
  ([0x1edc1]) and waits for the next tick, so it is frame-rate independent.

## The grey band at the start of the teal plane part (tick ~6060-6140) is a transition, not a glitch
- 0x68fe sets the callback 0x6434, which calls 0x2b20 every tick: [0x1eaa9] += 4 (+7 once past 0x100) up to 0x2bc.
  While it is below 0x2bc the part draws the canyon (0x3095) and then 0x2b6c instead of the plane: 0x2b6c pulls the
  previous picture (the "present the Paper" sheet) off the screen along sine-shaped edges (0x2ad0 builds the edge
  tables at 0x21455 / 0x21655 from the sine table), over the new canyon. It is tick-driven (~1 s), so it looks the
  same on any machine. Whole-frame captures show it too.
