# DOPE — music (scope "music": the loader's ProTracker player and GUS driver)

Addresses are code32 offsets (file offset in `unpacked/loader.bin` = 0x17e0 + offset; `rep.py loader32`). "Tick n"
is the log numbering of `full.gus` / `full.ticks`: the n-th GUS timer-2 interrupt served (ticks 1 and 2 come before
the player is started; tick 3 reads row 00:00). "k" counts ticks from the first row tick (k = n − 3). Times are
emulated seconds of the capture unless stated otherwise.

Everything below was checked, not only read: `models/music/dopeplay.py` (a routine-by-routine port, see its
README) builds from `../original/DOPE.MOD` and `unpacked/loader.bin` alone

- the GUS DRAM: identical to `full.dram` (all 1,048,576 bytes);
- the register / port write log from the GUS init to the stop: 281,388 lines, byte-identical to `full.gus`;
- the music position (order index, row, tick countdown) of all 32,113 `full.ticks` records after the first tick.

The check takes from the capture only the interrupt *schedule*: the service time of each interrupt (the `irq`
lines) and, for 7 of the 7,543 ramp interrupts, the order in which the emulator queued simultaneous ramp ends
(section 10). Every value written is computed. With the interrupts at their hardware times instead, the same
251,640 register writes come out with 169 differences, all inside those 7 interrupts or in one address bit that
depends on how ramp ends are grouped (section 10). The song is rendered to a WAV through a GF1 model (section 11);
there is no audio capture to compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- **Player**: "ProTracker for GUS replay routine, Copyright (C) 1994 Oskari Tammelin" (strings at code32 0xaf2),
  "gus unit raped by jmagic". MOD loader 0x2d15 (an S3M loader 0x38b2 is present, unused), 28 channels on GF1
  voices 0..27 (28 active voices: **22,050 frames per second**), samples uploaded unchanged by programmed I/O to
  DRAM 0..0x6ef5a, one after the other. No per-voice double buffering: a new note first **ramps the old note down
  with the volume-ramp IRQ enabled**, and the new sample is switched in from that IRQ.
- **Timing**: GUS timer 2 (320 µs steps). Tempo T → `n = 1,000,000 / (32 T)` (80 µs units, truncated) → reload
  count `256 − (n >> 2)`; the dropped quarter steps `−(n & 3)` go to `[0x3f29]`, **which nothing reads**. A count
  computed at tick k reaches the timer at the overflow after tick k+1: the period after tick k is set by the tempo
  after tick k − 2. The song: speed 9 / tempo 111 for orders 0..2 (70 × 320 µs = **22.40 ms** per tick instead of
  22.52), then speed 3 / tempo 122 (64 × 320 µs = **20.48 ms** instead of 20.49). Closed form in section 7.
- **What the ramp path does to the sound**: the old voice fades from its volume to GF1 0x400 in up to 1.84 ms;
  the new sample starts at that moment, nearly silent and still at the old frequency, and **every update of that
  tick** (frequencies, pans and volumes of all 28 channels) waits until the last ramp of the tick has ended:
  0.16..1.84 ms (median 1.77) after the tick on the 2,969 of 22,203 ticks that start notes. Volumes are then set
  by a jump (register 9 written directly, no ramp up); volume slides are steps.
- **Quirks that are the sound of the demo** (section 8.6): 4xx vibrato reads 64 bytes from a 32-byte sine table —
  the positive half of every 4xx cycle uses the dword `[0x22c0]` and the code bytes of the tick routine (594 of
  1,218 vibrato ticks); the period 906 (8 cells, channel 13, 3xx F0) is not in the note table and becomes a tone
  portamento to a ~132 kHz pitch; arpeggio plays base, +x, +x (y ignored); every channel's pan becomes 6 at its
  first note (the pan table written at load is overridden); 9xx always restarts the sample.
- **Capture artefacts** (section 10): the emulator serves the GUS interrupt only when the interrupted code has
  interrupts enabled. Each part's PIT calibration runs with `cli` for 20–55 ms: the tick is late, and when the
  wait exceeds a tick period the timer overflow is lost (the IRQ status is a flag). The capture lost 7 overflows
  (one at each part start p2, p4..p7, two at p3): the music is 145 ms behind the GUS clock at the end. Real hardware
  does the same thing (same `cli`), with counts that depend on the phase.
- Nothing in the parts reads the music position, and the player reads nothing from the parts.

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when (capture) | log tick | what |
|---|---|---|
| main 0xa6d, t ≈ 0 | 0 | `call [0x1a7f]` = 0x45b4 GUS init (section 3): ULTRASND, GF1 reset, 32 voices, latches, DRAM probe, **timer 2 started with count 0xff** (overflow every 320 µs), IRQ 7 handler 0x4040 with `[0x3f0c]` = 0x254 (`ret`: no player yet) |
| main 0xa78 | 0 | 0x2d15 load "dope.mod" (section 4) with interrupts off (`cli` .. `sti`): pan table, tracks, sample upload. On a real PC this takes seconds; instant here |
| 0.110398 / 0.110400 | 1, 2 | the overflows pending since the load: the handler reloads count 0xc2 (the image value of `[0x3f2a]`) and returns |
| main 0xa8a | 2 | int 10h mode 13h, then 0x2c17 start (section 5): channel reset, 28 voices (reg 0x0e = 0xdb), `[0x3f0c]` = 0x41e9 |
| 0.130240 (frame 9) | 3 | first player tick: row 00:00 (F09, F6F), first notes |
| main 0xaac | — | int 32h → CDPI: all parts run while the GUS interrupt plays the music |
| frame 32120, after tick 22205 (position 6d:28) | 22205 | main 0xaae: 0x2cff stop (section 5) |

The song would go on to order 0x7b and loop to order 0 (the restart byte is ignored); the demo stops at 6d:28.

---------------------------------------------------------------------------------------------------------------

## 2. Data

### 2.1 Globals (code32)

| address | size | meaning |
|---|---|---|
| 0xc24 | 48 | title (20 bytes) + 28 zero bytes |
| 0xc76 | u8 | speed (ticks per row), 6 after load |
| 0xc77 | u8 | tempo (BPM), 125 after load |
| 0xc78 | u8 | number of patterns (max over all 128 order entries + 1 = 70) |
| 0xc79 | u8 | instruments (31) |
| 0xc7a | u8 | song length (124) |
| 0xc7b | u8 | channels (28, from the tag) |
| 0xc7d | 256 | order table: 128 bytes from the module + 128 zero bytes |
| 0xd7d | 32 | initial GF1 pan per voice: 5 10 10 5, repeated (static) |
| 0xb57 / 0xb59 | u16 | period limits: 0xd60 = 3424 (856 × 4) / 0x1c0 = 448 (112 × 4), set by the MOD loader |
| 0xc1b | u32 | 0xd87b50 = 14,187,344 = 4 × 3,546,836 Hz (PAL Amiga clock × 4), set by the MOD loader |
| 0x1a97 | u32 | copy of the order index (unused) |
| 0x1a9b | u8 | tick countdown (1 at start) |
| 0x1a9c | u8 | row (0x3f at start); 0x1a9d u16: the same row, for the effects |
| 0x1a9f | u8 | order index (0xff at start) |
| 0x1aa0 | u8 | order index as shown (position) |
| 0x1aa1 | u8 | "playing" (1 start, 0 stop / F00): nothing reads it |
| 0x1aa2 / 0x1aa3 | u8 / u32 | current pattern / pattern × 0x140 (offset in the tracks) |
| 0x1aa7 | u8 | break row: when the row equals it, the next row tick starts a new order (0x3f, or Bxx/Dxx/E6x's row) |
| 0x1aa8 | u8 | first row of the next order (Dxx) / loop row (E6x) |
| 0x1aa9 | u8 | pattern delay (EEx) |
| 0x1aaa / 0x1aab | u8 | E6x loop row / loop count |
| 0x1aad | u8 | load error (1 open, 2 memory, 3 read) |
| 0x1aae | u32 | DRAM upload pointer |
| 0x1ab6 | u32 | master volume 0x40 (never changed) |
| 0x22c0 | u32 | EAX at the tick call (written by 0x22c4; read by 4xx, section 8.6) |
| 0x3f04 / 0x3f08 | u32 | `((2 T / 5) << 32) / 0x411`, 0: written by 0x4369 and Fxx, never read (left over from a PIT version) |
| 0x3f0c | u32 | timer callback: 0x254 (`ret`) or 0x41e9 (the player) |
| 0x3f10 | u32 | old IRQ vector |
| 0x3f14 | u32 | master volume last applied (0 at start) |
| 0x3f18 | u32 | GF1 frame rate for the active voices: 22,050 |
| 0x3f1c..0x3f24 | u16 | GUS ports: base 0x240, 3x2 voice, 3x3 register, 3x4 data low, 3x7 DRAM |
| 0x3f26 / 0x3f27 | u8 | IRQ (7) / DRAM banks found by the probe (written only) |
| 0x3f29 | u8 | `−(n & 3)`: the tempo remainder, **written only** |
| 0x3f2a | u8 | timer-2 reload count (0xc2 in the image) |
| 0x3f2b | u8 | active voices (28) |
| 0x3f2c | 32 u8 | per voice: 1 = idle, 0 = ramping down for a new note |
| 0x3f61 | u8 | register select restored at the end of the IRQ (0x43 / 0x44 during uploads) |

### 2.2 Instrument record (31 × 0x46 bytes at 0x11fd)

```
+0x11  32 bytes name (22 from the module + 10 zero)      +0x31 u8 flags: 1 loop, 2 bidirectional
+0x32  u32 length (bytes; 0 if <= 2)                       +0x36 u32 loop start
+0x3a  u32 loop end = min(ls + 2 replen - 2, length - 1) + 2      (normally ls + 2 replen)
+0x3e  u8 volume (not clamped)   +0x3f u8 finetune & 15   +0x40 u32 DRAM start   +0x44 u16 C-2 rate = word [0xb37 + 2 ft]
loop flag:  (replen - 1) * 2 > 2 (signed), i.e. replen >= 3 words; bidirectional: the name starts with "ping"
```

### 2.3 Channel record (32 × 0x3f bytes at 0x1ac0; channel c plays on GF1 voice c)

```
+0x00 u32 track: this channel's events (extended memory)   +0x04 u32 DRAM start of the sample
+0x08 u32 loop start (DRAM)   +0x0c u32 end (DRAM): loop end, or start + length
+0x10 u8 pan 0..15 (6 at start)   +0x11 u8 change flags: 1 volume, 2 pan, 4 frequency, 8 (re)start the sample
+0x12 u8 GF1 voice control: 8 loop, 0x10 bidirectional   +0x13 u8 volume 0..64
+0x14 u32 loop start written last (0xffff at start)   +0x18 u32 end written last (0xffff)
+0x1c u16 frequency written last (0x00ff)   +0x20 u8 pan written last (7)   +0x21 u8 instrument (1-based; see 8.6)
+0x22 u16 GF1 frequency (FC)   +0x24 u32 sample offset (9xx; 0 at every row)
+0x28 u8 effect   +0x29 u8 parameter   +0x2a u16 period (4 x Amiga period)   +0x2c u8 vibrato position (+0x2d: 0)
+0x2e u8 vibrato depth (low nibble)   +0x2f u8 vibrato speed (high nibble)   +0x30 u16 note period (porta target,
vibrato centre)   +0x32 u16 C-2 rate   +0x34 / +0x36 / +0x38 u16 arpeggio periods   +0x3a u8 arpeggio counter 0..2
+0x3b u8 note: octave << 4 | note 1..12   +0x3c u8 tone porta speed   +0x3d u8 ticks since the row
```

### 2.4 Tables (static, read them from loader.bin; do not copy)

| what | code32 | format |
|---|---|---|
| C-2 rates per finetune | 0xb37 | 16 u16: 8363, 8413, 8463, 8529, 8581, 8651, 8723, 8757, 7895, 7941, 7985, 8046, 8107, 8169, 8232, 8280 |
| Amiga periods, 8 octaves | 0xb5b | 96 u16: 6848 .. 28 (ProTracker's values extended; entries 4 and 6 read 3424 and 2832 instead of 5424 and 4832) |
| note → period base octave | 0xb8b | = 0xb5b entries 24..35: 1712 .. 907 |
| vibrato sine | 0x22a0 | 32 u8 (0, 24, 49 .. 255 .. 24), followed by `[0x22c0]` and the code at 0x22c4 (read by 4xx) |
| IRQ → GF1 latch | 0x3f66 | 16 u8 (IRQ 7 → 0x44: 4 | "same for both") |
| voices → frame rate | 0x3f76 | 19 u16: 617400 / n, n = 14..32 (28 → 22050) |
| volume 0..64 → GF1 | 0x3f9c | 65 u16: 0x4e20, 0x98d0, 0xa1a0 .. 0xdf80, 0xdf80 (register 9 values) |
| pan per voice | 0xd7d | 32 u8 |
| self-modified EOI word | 0x3f62 | `89 c0` (mov eax, eax) / `e6 a0` (out 0xa0, al), patched into 0x404b |

---------------------------------------------------------------------------------------------------------------

## 3. GUS init 0x45b4 (main 0xa6d, `call [0x1a7f]`)

```c
void gus_init(void)
{
    int31(0x0900);                              // virtual interrupts off (restored at the end)
    if (!ultrasnd()) if (!menu()) return;       // 0x490a / 0x498b
    out(base, 0x0b);                            // mix control: line in off, output off
    ports: 3x2 [0x3f1e], 3x3 [0x3f20], 3x4 [0x3f22], 3x7 [0x3f24]
    w8(0x4c, 0); delay; w8(0x4c, 1); delay;     // reset, run (delay 0x401e: 7 reads of port 0x300)
    w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);
    w8(0x0e, 0xdf);                             // 32 voices for now
    for (v = 31; v >= 0; v--) repeat 2x {       // bl ^= 0x80: each voice twice
        select(v); w8(0x00, 3); w8(0x0d, 3); w16(0x09, voltab[0] /*0x4e20*/); w8(0x06, 0x3f); w8(0x0c, 7); delay;
    }
    out(2xF, 5); out(2x0, 8); out(2xB, 0); out(2xF, 0);
    out(2x0, 0x4b); out(2xB, latch[irq]);       // 0x44: IRQ 7 for both GF1 interrupts; no DMA latch
    out(2x0, 9);                                // output on, latches on
    w16(0x43, 0xffff);                          // DRAM probe: byte at bank * 0x40000 + 0x3ffff, banks 0..3:
    for (cl = 3; cl < 16; cl += 4) {            //   write it + 1, read back; [0x3f27] = banks that answer
        w8(0x44, cl); b = in(3x7) + 1; out(3x7, b); if (in(3x7) != b) break; [0x3f27]++; }
    w8(0x4c, 7);                                // run, DAC on, GF1 interrupts on
    w8(0x47, 0xff); w8(0x45, 8);                // timer 2: count 0xff (1 x 320 us), IRQ on
    out(2x8, 4); out(2x9, 2);                   // start timer 2
    patch word 0x404b = irq > 7 ? `out 0xa0, al` : `mov eax, eax`;   // self-modifying: slave PIC EOI
    setirqvect(irq, 0x4040); unmask; real-mode stub (0x2fa) -> [0x3f10];
    r8(0x8f);
}
```

The probe leaves bytes 0x7ffff, 0xbffff and 0xfffff of the capture's 1 MB DRAM at 1 (0x3ffff is overwritten by
sample 12). `ultrasnd` 0x490a finds `ULTRASND=` in the environment and reads four numbers with 0x4968 (skip
non-digits, then digits): the port in base 16 → `[0x3f1c]`, two DMA channels (ignored), the IRQ in base 10 →
`[0x3f26]` (IRQ 2 is used as 9). Without it 0x498b prints "No ULTRASND variable found." and asks for the port
(220h..260h, ENTER = 220h) and the IRQ (2, 3, 5, 7, 11, 12, 15; ENTER = 11) through int 16h.

---------------------------------------------------------------------------------------------------------------

## 4. Module load 0x2d15 (EDX = "dope.mod")

### 4.1 Format as read

```
0x000  20 bytes title                      0x014  31 x 30-byte sample headers: 22 name, u16be length/2,
                                                  u8 finetune, u8 volume, u16be loop start/2, u16be replen/2
0x3b6  u8 song length (124)   0x3b7 restart (ignored)   0x3b8  128 orders
0x438  tag: "xCHN" -> x;  "M.K." -> 4;  "xyCH" -> 10 x + (y & 15)  ("28CH" -> 28);  else 4
0x43c  patterns: 64 rows x channels x 4 bytes (Amiga cells), npat = max(all 128 orders) + 1 = 70
then   the samples, in instrument order (8-bit signed)
```

### 4.2 Pseudo-C

```c
cli; buffer = getmem_low(0x10000);              // (main's [0] makes this land elsewhere: harmless)
read(0x43c); title -> 0xc24; channels -> [0xc7b];
[0xc1b] = 0xd87b50; pan_table(0xd7d);           // 0x4587: w8(0x0c, pan[v]) for voices 0..31
speed = 6; tempo = 125; [0xc79] = 31; maxp = 0xd60; minp = 0x1c0;
instruments (2.2); orders -> 0xc7d (+128 zeros); [0xc7a] = song length; pattern = orders[0];
npat = max(orders[0..127]) + 1;
for (c = 0; c < channels; c++) {                // per channel: npat * 64 events of 5 bytes
    C[c].track = getmem_high(npat * 0x145);     //   (0x140 used per pattern)
    fill with {note 0, instrument 0, volume 0xff, effect 0, parameter 0};
}
for (p = 0; p < npat; p++) {
    read(channels * 256);
    for (c, row): (b0 b1 b2 b3) = cell; e = C[c].track + p * 0x140 + row * 5;
        period = (b0 & 15) << 8 | b1;
        e.note = period ? (i = first index with periods[i] == period, else 0x60, i / 12 << 4 | i % 12 + 1) : 0;
        e.instrument = (b0 & 0xf0) | b2 >> 4;
        if ((b2 & 15) == 0xc) e.volume = b3;    // Cxx becomes the volume byte (effect stays 0 00)
        else { e.effect = b2 & 15; e.parameter = b3; }
}
dram = 0;
for (i = 0; i < 31; i++) if (I[i].length) {
    if (I[i].length > 0x10000) exit;            // jmp 0x355
    read(I[i].length) -> buffer; I[i].dram = dram; dram += I[i].length;
    buffer[0] = 0; if (!(I[i].flags & 1)) buffer[1] = 0;
    upload(buffer, I[i].dram, I[i].length);     // [0x1a87] = 0x4450
}
close; sti;
```

Notes are matched to the period table **exactly**: a period not in it becomes note 0x81 (octave 8, note 1). In
DOPE.MOD that is period 906 (8 cells, see 8.6). 0x4450 uploads by PIO, one byte at a time: register 0x44 =
address >> 16 (again after every 64 KB), register 0x43 = address & 0xffff (16-bit write to 3x4), the byte to 3x7;
`[0x3f61]` keeps the selected register so the IRQ handler can restore it. 0x44a8 (the same with `xor al, 0x80`,
for unsigned samples) and 0x44f8 (DRAM → memory) are not used by the MOD path.

### 4.3 DRAM layout (`dopeplay.py --info`; hex; loop addresses are DRAM addresses)

| ins | name | DRAM start | length | loop start | loop end | flags | vol |
|---|---|---|---|---|---|---|---|
| 1 | loop2.1 | 0 | 11420 | | | | 64 |
| 2 | loop2.2 | 2c9c | 11430 | | | | 64 |
| 3 | loop2.4 | 5942 | 11406 | | | | 64 |
| 4 | ping.Leadi | 85d0 | 20170 | b20c | d470 | loop, bidi | 64 |
| 5 | ping.leadkaiku | d49a | 27310 | 13590 | 13ed0 | loop, bidi | 64 |
| 6 | loop2.3 | 13f48 | 11366 | | | | 64 |
| 8 | Melokick.pat | 16bae | 6480 | | | | 64 |
| 9 | Basso Sinidist | 184fe | 13580 | 19bac | 1b85e | loop | 64 |
| 10 | ping.HuminaLore | 1ba0a | 56542 | 1ba0a | 29456 | loop, bidi | 64 |
| 11 | ping.Hulina | 296e8 | 49072 | 2bbe2 | 35690 | loop, bidi | 64 |
| 12 | Orgaani | 35698 | 59744 | 3d4f2 | 43ff8 | loop | 64 |
| 14 | ping.bassosini | 43ff8 | 34538 | 44034 | 4c206 | loop, bidi | 64 |
| 17 | 808clap2.pat | 4c6e2 | 3822 | | | | 46 |
| 18 | 909rcym1.pat | 4d5d0 | 22502 | | | | 48 |
| 19 | 909sd2.pat | 52db6 | 7998 | | | | 64 |
| 20 | 909hh3.pat | 54cf4 | 1918 | | | | 50 |
| 21 | GrooveLoop | 55472 | 41676 | | | | 64 |
| 22 | HardPlate | 5f73e | 9494 | | | | 64 |
| 23 | hardlonghiha | 61c54 | 8474 | | | | 56 |
| 25..28 | loop1..loop4 | 63d6e, 669c4, 6960a, 6c2e8 | 11350, 11334, 11486, 11378 | | | | 64 |

Instruments 7, 13, 15, 16, 24, 29–31 are empty (DRAM start 0, length 0). A one-shot's end is `start + length`:
the first byte of the next sample, which the load zeroed, so a finished one-shot parks on a 0. Orgaani's loop end
is also the next sample's first byte. There are no bytes appended for the interpolation at loop ends.

---------------------------------------------------------------------------------------------------------------

## 5. Start 0x2c17, stop 0x2cff

```c
void start(void)                    // main 0xa8a
{
    for (c = 0; c < 32; c++) C[c] = { vol 0, start/loop/end 0, pan 6, flags 0, ctl 0, fc 0, cached loop start /
        end 0xffff, cached fc 0xff, cached pan 7, instrument 0, offset 0, effect 0 00, period 0, vibrato 0, note
        period 0, note 0, porta speed 0, tick count 0 };          // +0x32..+0x3a (C-2 rate, arpeggio) are kept
    countdown = 1; row = 0x3f; break_row = 0x3f; next_row = 0; pattern_delay = 0; order_index -= 1; playing = 1;
    loop_count = loop_row = 0;
    // 0x4369 ([0x1a77]):
    voices = max(channels, 14); [0x3f18] = framerate_table[voices - 14];    // 22050
    w8(0x0e, (voices - 1) | 0xc0);                                         // 0xdb
    [0x3f04] = ..., [0x3f08] = 0;  [0x3f0c] = 0x41e9;                      // the next timer IRQ runs the player
}
void stop(void)                     // main 0xaae: 0x43d6 ([0x1a7b]) then 0x4545 ([0x1a83])
{
    [0x3f0c] = 0x254;
    for (v = 31; v >= 0; v--) { select(v); repeat 2x { w8(0x00, 3); w8(0x0d, 3); w8(0x09, 0); delay; } }
    restore the IRQ vector, mask the IRQ; w8(0x4c, 0); out(2x0, 0x0b);
}
```

---------------------------------------------------------------------------------------------------------------

## 6. The GUS interrupt 0x4040

```c
void gus_irq(void)                                  // IRQ 7, protected mode
{
    out(0x20, 0x20); [patched: out(0xa0, 0x20) for IRQ >= 8];   // EOI first
    st = in(2x6);
    if (st & 0x40) ramp_irq();                      // 0x40d8, section 9.2
    else if (st & 0x08) {                           // timer 2
        w8(0x47, [0x3f2a]);                         // the count computed after the previous tick
        w8(0x45, 0); delay; w8(0x45, 8);            // acknowledge, re-enable
        sti; [0x3f0c]();                            // 0x41e9 or `ret`
    }
    out(3x3, [0x3f61]);                             // restore the register select
}
void timer_tick(void)                               // 0x41e9
{
    sti; tick();                                    // 0x22c4, section 8
    n = 1000000 / (tempo * 32);                     // idiv: 31250 / tempo, in 80 us units, truncated
    [0x3f2a] = -(n >> 2);                           // timer-2 count: n / 4 steps of 320 us, truncated again
    [0x3f29] = -(n & 3);                            // the remainder: never read
    note_off();                                     // 0x421a, then 0x4299 if no ramp is pending (section 9)
}
```

Only bits 6 (volume ramp) and 3 (timer 2) of the status are tested, ramp first. If a ramp end and a timer
overflow were pending together, the handler would serve the ramp and leave the timer pending (it never happened in
the capture: no interrupt found both bits set).

---------------------------------------------------------------------------------------------------------------

## 7. Tempo, timer and tick times

### 7.1 Tempo → count

| tempo | n = 1e6 / 32T | n >> 2 | count `[0x3f2a]` | `[0x3f29]` | tick period | nominal 2.5 s / T |
|---|---|---|---|---|---|---|
| 125 (load, image) | 250 | 62 | 0xc2 | 0xfe | 19.84 ms | 20.00 ms |
| 111 (F6F, 00:00) | 281 | 70 | 0xba | 0xff | **22.40 ms** (44.64 Hz) | 22.52 ms |
| 122 (F7A, 03:00) | 256 | 64 | 0xc0 | 0x00 | **20.48 ms** (48.83 Hz) | 20.49 ms |

The remainder `[0x3f29]` = −(n & 3) is what an accumulator would need to distribute the dropped quarter steps
(80 µs each); it is stored at 0x4216 and read nowhere (no instruction refers to 0x3f28..0x3f29 otherwise). So the
period is exactly `(n >> 2) × 320 µs`: tempo 111 runs 0.54 % fast, tempo 122 0.06 % fast.

### 7.2 When the count takes effect

The GUS timer reloads its counter from the count register at each overflow (DOSBox and run_dope.py model it so,
and the capture's 22,203 tick times agree to the microsecond). The handler of overflow k writes the count
computed after tick k − 1, so the counter reloaded at overflow k holds the count computed after tick k − 2:

```c
// k = ticks since the first row tick (k = 0 reads 00:00), T(k) in seconds after it, hardware times
c(j) = count after tick j's row/effects (c(-2) = c(-1) = 0xc2)
T(k + 1) = T(k) + (256 - c(k - 2)) * 320e-6
// DOPE.MOD: F6F at k = 0, F7A at k = 1728 (order 3 row 0)
T(k) = 0.01984 k                                  k <= 2
     = 0.03968 + (k - 2) * 0.0224                 2 <= k <= 1730
     = 38.74688 + (k - 1730) * 0.02048            k >= 1730
```

Rows: speed 9 (F09) for orders 0..2: 1,728 ticks = 38.70 s; speed 3 (F03, repeated at the start of orders 0f..22
and 59..5e) from order 3: 61.44 ms per row, 3.932 s per 64-row order. The capture's tick 3 is at 0.130240 s
(frame 9); tick 22205 (6d:28, k = 22202) is at T = 458.013 s by the closed form, 458.159 s on the GUS clock of
the capture (7 lost overflows, 7.3).

### 7.3 Lost overflows (capture and hardware)

The timer-2 IRQ status is one bit. If the interrupt is not served before the next overflow, that overflow is lost:
no tick, and the music falls one tick period behind the GUS clock. In the capture this happens at each part
start, where the part's PIT calibration (p3: 0010:1af7 `cli` .. 0010:1b61 `sti`, waiting for two to four retraces)
holds the interrupt off:

| part start | tick served late by | lost | music behind the closed form from tick |
|---|---|---|---|
| p1 (0.21 s) | 20.5 ms | 0 | — |
| p2 (38.71 s) | 32.7 ms | 1 | 1725 (02:3f): 22.40 ms |
| p3 (143.14 s) | 55.4 ms | 2 | 6821 (1d:20): 63.36 ms |
| p4 (194.77 s) | 20.7 ms | 1 | 9342 (2a:29): 83.84 ms |
| p5 (300.93 s) | 27.7 ms | 1 | 14524 (45:28): 104.32 ms |
| p6 (365.22 s) | 33.1 ms | 1 | 17662 (55:3e): 124.80 ms |
| p7 (422.39 s) | 25.3 ms | 1 | 20453 (64:20): 145.28 ms |

(p4's 20.7 ms delay at tempo 122 exceeds one 20.48 ms period by 0.2 ms.) The same code with `cli` runs on real
hardware, so the original loses ticks there too, but how many depends on where the calibration falls between two
overflows. Besides these, 2,942 interrupts are served late by 2 µs to 0.51 ms (1,943 of them by 0.04 ms; the
longest when the parts' retrace-synchronised int 8 handler busy-waits for the retrace with interrupts off); that
only moves the tick and its writes.

---------------------------------------------------------------------------------------------------------------

## 8. The tick 0x22c4

### 8.1 Row or effects

```c
void tick(void)
{
    [0x22c0] = eax;                                       // (read by 4xx: section 8.6)
    if (--countdown) return tick_effects();               // 0x251f
    countdown = speed;
    if (pattern_delay) { pattern_delay--; return tick_effects(); }
    if (row == break_row) {                               // next order
        row = next_row;
        if (++order_index == song_length) order_index = 0;
        shown_order = order_index; pattern = orders[order_index];
    } else row++;
    next_row = 0; break_row = 0x3f;
    for (c = 0; c < channels; c++) row_channel(&C[c], track[c][pattern * 64 + row]);
}
```

On a row tick no tick effect runs. The position the capture records is (`[0x1aa0]`, `[0x1a9c]`): the row just read.

### 8.2 Row: one channel (0x2379..0x2505)

```c
void row_channel(Chan *C, Event e)
{
    C->offset = 0; C->effect = e.effect; C->param = e.param;
    if (e.note) {
        ins = e.instrument; keep_vol = 0;
        if (!ins) { ins = C->ins; if (!ins) goto noteless; keep_vol = 1; }
        C->ins = ins; I = &instrument[ins - 1];
        C->note = e.note; C->c2spd = I->c2spd;
        C->base = ((period_oct1[(e.note & 15) - 1] * 0x20ab0) >> (e.note >> 4)) / I->c2spd;   // u32, -> u16
        if (e.effect == 3 || e.effect == 5) goto noteless;          // tone porta: only the target
        C->period = C->base; C->flags = 0x0f; C->ctl = 0; C->vibpos = 0; C->offset = 0;
        C->start = I->dram; C->end = I->dram + I->length;
        if (I->flags & 1) { C->lstart = I->dram + I->ls; C->end = I->dram + I->le; C->ctl |= 8;
                            if (I->flags & 2) C->ctl |= 0x10; }
        if (!keep_vol) { C->vol = min(I->volume, 64); C->flags |= 1; }
        if (e.effect == 0xe && e.param >> 4 == 0xd) C->flags = 0;   // (undone by the EDx row handler)
        goto volume;
    }
noteless:
    if (e.instrument) { C->ins = e.instrument - 1;                 // 0-based here (see 8.6)
                        C->vol = min(instrument[e.instrument - 1].volume, 64); C->flags |= 1; }
volume:
    if (e.volume != 0xff) { C->flags |= 1; C->vol = min(e.volume, 64); }        // Cxx
    C->tickcount = 0;
    row_effect(C);                                       // tables 0x2579 (effect) / 0x25c1 (Exy)
}
```

The note period is in units of a quarter Amiga period: `1712 × 16 >> octave` for finetune 0 (note 0x31 = C of
octave 3 → 3424 = 4 × 856). With finetune f it is scaled by 8363 / c2spd[f] (exactly, before truncation).

### 8.3 Row effects

| effect | handler | on the row |
|---|---|---|
| 0xy | 0x2ba3 | if xy: arp[0] = period; arp[1] = arp[2] = `period_oct1[n] << 4 >> oct` with n = note − 1 + x, and while n > 11: oct++, n −= **11** (no finetune) |
| 1, 2, 5, 6, 7, A, C | 0x250c | nothing (C is the volume byte) |
| 3xx | 0x28e8 | xx = 0 → xx = porta speed; porta speed = parameter = xx |
| 4xy | 0x2854 | 0 nibbles keep the old depth / speed; parameter = depth \| speed |
| 8xy | 0x2b06 | pan = x (0..15); flags \|= 2 |
| 9xx | 0x270a | if xx: offset = xx << 8, at most end − start − 1; **flags \|= 8 (restarts the sample, with or without a note)** |
| Bxx | 0x2a57 | break_row = row; order_index = xx − 1 |
| Dxy | 0x26d7 | xy ≥ 0x64 → 0x63; next_row = 10x + y; break_row = row |
| Fxx | 0x2689 | xx = 0: playing = 0 (no effect); xx > 0x1f: tempo = xx; else speed = countdown = xx |
| E1x / E2x | 0x29db / 0x2a05 | period ∓= 4x, limited to [448, 3424] (unsigned); flags \|= 4 |
| E5x | 0x2b15 | base = period = the note's period with c2spd[x] (no flag) |
| E6x | 0x2b51 | x = 0: loop_row = row; else: count = x if 0, else count−− and stop at 0; while looping: break_row = row, next_row = loop_row, order_index−− |
| E8x | 0x2b06 | pan = x >> 4 = 0 (the 8xy handler with the low nibble) |
| EAx / EBx | 0x2a2f / 0x2a45 | volume ± x, limited to 0..64; flags \|= 1 |
| EDx | 0x2a96 | flags \|= 0x0f |
| EEx | 0x2a9f | pattern_delay = x |
| E0, E3, E4, E7, E9, EC, EF | 0x250c | nothing |

### 8.4 Tick effects (0x251f; tables 0x2601 / 0x2649)

```c
for (c = 0; c < channels; c++) {
    C->tickcount++; if (++C->arpcount >= 3) C->arpcount = 0;      // every channel, every tick
    switch (C->effect) {
    case 0x0: if (C->param) { C->period = C->arp[C->arpcount]; C->flags = 4; }   // `mov`: other flags dropped
    case 0x1: if (C->period) { C->period -= 4 * xx; if ((s16)C->period < 448) C->period = 448; C->flags |= 4; }
    case 0x2: if (C->period) { C->period += 4 * xx; if (C->period > 3424) C->period = 3424; C->flags |= 4; }
    case 0x3: porta(C, 4 * C->param); C->flags |= 4;
    case 0x4: C->vibpos += (C->param & 0xf0) >> 2;                // speed x 4 per tick, 256 = one cycle
              d = vib64[C->vibpos >> 2] * (C->param & 15) >> 5;    // 64 entries: see 8.6
              C->period = clamp_s(C->base + (C->vibpos & 0x80 ? d : -d));   // >= 448 signed, <= 3424
              C->flags |= 4;
    case 0x5: volslide(C, (y - x) & 0xff); porta(C, 4 * C->portaspeed); C->flags |= 5;
    case 0x6: volslide(C, (y - x) & 0xff);
              C->vibpos += (C->vspeed) >> 2; d = sine[(C->vibpos >> 2) & 31] * C->vdepth >> 5;
              C->period = clamp_u(C->base + (C->vibpos & 0x80 ? d : -d)); C->flags |= 5;
    case 0xa: volslide(C, x ? -x : y); C->flags |= 1;
    case 0xe: E9x: if (y == C->tickcount) { retrigger from instrument [C->ins - 1] at C->offset; volume reset;
                                            C->tickcount = 0; C->flags |= 0x0d; }
              ECx: if (y == C->tickcount) { C->vol = 0; C->flags |= 1; }
              EDx: if (y == C->tickcount) C->flags |= 0x0f;
    }                                               // 7 (tremolo), 8, 9, B, C, D, F: nothing
}
volslide(C, a): v = (u8)(C->vol - a); C->vol = (v & 0x80) ? 0 : min(v, 64);
porta(C, s):    if (period < base) { period += s; if (period > base) period = base; }
                else if (period > base) { period -= s; if (borrow || period < base) period = base; }   (u16)
```

### 8.5 What the song uses (`dopeplay.py --info`, cells in the played orders)

A (volume slide) 32,639; C 13,963; 3 (tone porta) 3,323; EB (fine volume down) 1,759; 4 (vibrato) 672; 9 (offset)
407; 8 (pan) 103; 0 (arpeggio, nonzero) 53 rows; 1 (porta up) 63; E0 34 (filter: nothing); F 30 (F09, F6F at
00:00, F03 + F7A at 03:00, F03 at orders 0f..22 and 59..5e). No B, D, 2, 5, 6, 7, E1–E9, EC–EF. 11,521 notes start
in the capture.

### 8.6 Quirks (all part of the sound; keep them)

- **4xx reads past the sine.** 0x27f9 indexes `0x22a0 + (vibpos >> 2)` with vibpos 0..255 and no `and 0x1f` (6xy
  has it). The negative half (vibpos < 128) uses the 32-byte sine; the positive half reads 0x22c0..0x22df: the
  dword `[0x22c0]` (EAX at the tick call: AL = 8 from the handler, bits 8..31 from the interrupted code: 0x808 in
  the capture) and the first 28 code bytes of 0x22c4 (a3 c0 22 00 00 fe 0d 9b 1a 00 00 0f 85 4a 02 00 00 a0 76 0c
  00 00 a2 9b 1a 00 00 80). So every 4xx cycle is a sine dip followed by a jagged positive half. In the song 594 of
  1,218 vibrato ticks read there, 13 of them byte 0x22c0 (= 8 always) and none the machine-dependent bytes 0x22c1..3:
  the result does not depend on the machine. A port reads these 64 bytes from loader.bin (code32 0x22a0) with bytes
  0x22c0..0x22c3 replaced by 08 08 00 00.
- **Period 906.** The module has period 906 at row 0x30 of orders 3b, 3d, 43, 45, 60, 62, 68, 6a (pattern cells
  with instrument 12 and 3F0, channel 13). It is not in the table (907 is), so the note becomes 0x81: period
  `1712 × 16 >> 8` = 107 (Amiga 26.75), and the tone portamento slides channel 13 to it at 960 per tick: FC 0x10b,
  0x1b6, 0x4bb, 0x180d (6157: 6 bytes per frame, ≈ 132 kHz) — a short upward squeal that stays until the next note.
- **Arpeggio** plays base, +x, +x (y is ignored; 28 of the 53 rows have y ≠ 0), and an octave wrap subtracts 11
  instead of 12 (not reached in the song). The counter runs on every non-row tick of every channel and is never
  reset, and the tick handler leaves the last arpeggio period in place after the effect ends.
- **Pan**: the load writes 5 10 10 5 ... to the voices, but each channel record starts with pan 6 and a cached pan
  of 7, so the first note of every channel writes pan 6. All 28 channels play at 6 (slightly left) except where
  8xy sets x (800 → 0, 8F0 / 8FF → 15).
- **9xx** sets the "start sample" flag even without a note (none in this song) and clips the offset to the sample.
- **Instrument numbers** stored by the noteless path (instrument without note, or with 3xx / 5xy) are 0-based; a
  later note without an instrument then plays the previous instrument (and none if it was instrument 1). The song
  has 2,032 tone-porta notes with instruments, but no note without an instrument follows one: no audible effect.
- **Fine tune** only enters through c2spd (all instruments here are finetune 0). EDx starts the note on the row
  *and* at tick x (the row handler sets all flags again). E8x sets pan 0.

---------------------------------------------------------------------------------------------------------------

## 9. Voices: note-off ramps, the ramp interrupt and the update

### 9.1 After every tick: 0x421a

```c
for (v = 0; v < 32; v++) {
    idle[v] = 1;
    if (C[v].flags & 8) {                       // (re)start the sample: fade the old note first
        cli; select(v); idle[v] = 0;
        cur = r16(0x89);                        // current volume (16-bit, 12-bit value << 4)
        w8(0x07, 0x40); w8(0x08, cur >> 8); w8(0x0d, 0x60);    // ramp down to 0x400, IRQ at the end
        sti;
    }
}
if (all 32 idle) update();                      // 0x4299 now; else the last ramp IRQ calls it
```

The ramp runs at register 6 = 0x3f: 63 steps (of 4096) per GF1 frame, from the current volume down to 0x400:
`(vol12 − 0x400) / 63` frames, 1.84 ms from volume 64 (0xdf8), 0.16 ms from volume 0 (0x4e2).

### 9.2 Ramp end: 0x40d8

```c
void ramp_irq(void)
{
    bx = EBX of the interrupted code;           // (0 in the capture)
    al = r8(0x8f);
    do {
        v = al & 31; select(v); idle[v] = 1;
        w8(0x0d, 3); w8(0x00, 3);               // stop the ramp and the voice
        if (C[v].lstart != C[v].cached_ls) { C[v].cached_ls = C[v].lstart; addr(2, 3, C[v].lstart); }
        if (C[v].end != C[v].cached_end)   { C[v].cached_end = C[v].end;   addr(4, 5, C[v].end); }
        addr(0x0a, 0x0b, C[v].start + C[v].offset);
        w8(0x00, C[v].ctl);                     // start: 0 one-shot, 8 loop, 0x18 bidirectional loop
        al = r8(0x8f);
    } while (!(al & 0x40));                     // more voices with a ramp IRQ pending
    if (all 32 idle) update();
}
addr(hi, lo, a): bx = (bx >> 7) | (a << 9);    // shrd bx, si, 7 (16 bit)
                 w16(hi, a >> 7);               // address bits 7..19
                 w8(lo, bx >> 8);               // high byte of the low register: address bits 0..6 << 1, and in
                                                //   bit 0 (= register bit 8, the fraction's 1/2) bit 15 of the
                                                //   previous BX: bit 6 of the previous address written in this
                                                //   interrupt, or of the interrupted code's EBX for the first
```

The low register is written with an 8-bit write to 3x5, so its low byte is whatever 3x4 last latched (the run_dope
log shows it as 0; the GF1 model takes the low byte of the last 16-bit write, like `jizz/remake/src/gf1.cpp`).

### 9.3 The update: 0x4299

```c
void update(void)
{
    for (v = 0; v < 32; v++) {
        select(v); f = C[v].flags; C[v].flags = 0;
        if (!f) continue;
        if (f & 8) f = 7;
        if (f & 4) {
            if (C[v].period) C[v].fc = (14187344 / C[v].period) * 1024 / 22050;   // two truncating divisions
            if (C[v].fc != C[v].cached_fc) { C[v].cached_fc = C[v].fc; w16(0x01, C[v].fc); }
        }
        if ((f & 2) && C[v].pan != C[v].cached_pan) { C[v].cached_pan = C[v].pan; w8(0x0c, C[v].pan); }
        if (master != master_applied || (f & 1)) {   // master_applied is global: true once, for the first channel
            master_applied = master; w16(0x09, voltab[C[v].vol * master >> 6]);
        }
    }
}
```

FC: the GF1 plays FC / 1024 bytes per frame at 22,050 frames per second. Against the PAL frequency of the computed
period the result is 0.2..7.3 cents flat over the three ProTracker octaves. Volume: `voltab` gives
gain(64) = 0.246 of full scale and about 5 dB per halving of the volume (−5.0 dB at 32, −10.0 at 16, −26.2 at 1;
volume 0 = GF1 0x4e2, −54.6 dB): a compressed law, not ProTracker's linear one. Keep the table.

### 9.4 What is heard (the ramp path)

On a tick with note starts (2,969 of 22,203; `analysis.py` with the interrupts at their hardware times):

1. at the tick: each voice that gets a new note starts fading from its current volume to 0x400 (≈ −60 dB below
   volume 64); the old sample keeps playing during the fade (0.16..1.84 ms);
2. at each ramp end: the voice is stopped and restarted on the new sample (position, loop) — still at the volume
   0x400 and at the **old** frequency;
3. when the last ramp of the tick has ended (0.16..1.84 ms after the tick, median 1.77 ms): frequency, pan and
   volume of **every** channel with changes are written; the new notes jump to their volume (no ramp up; the
   sample's zeroed first byte softens the step), and the slides / vibrato / volume changes of the other channels
   of that tick are late by the same amount.

On ticks without note starts the update runs at the tick. Volume changes are always jumps (register 9), so volume
slides are steps of one table entry per tick. A new note is therefore heard 0.16..1.84 ms after its tick, with its
first samples (those played at volume 0x400 between its own ramp end and the last one) inaudible. A port that runs
the same events through a GF1 model (section 11) reproduces all of this; a remake that wants the original's sound
must keep the deferred update, not start notes at the tick.

---------------------------------------------------------------------------------------------------------------

## 10. The register stream (`full.gus`) and what in it comes from the capture

Format: `<tick> v<voice> r<reg> <value>` (every register write but the DRAM address 0x43 / 0x44; 8-bit writes to a
16-bit register log `latch | byte << 8` with the latch 0), `<tick> p<port> <value>` (ports other than 3x2..3x5,
3x7, 2x8), `<tick> irq <t|r> <time>` before each handler run. 281,388 lines: GUS init and load (tick 0), ticks 1..2
(reload only), start (`r0e db`), then per timer tick `r47 count`, `r45 0`, `r45 8`, the note-off ramps (`r07 40`,
`r08 vol>>8`, `r0d 60`), [the update]; per ramp IRQ the voice switch(es) and, after the last, the update; the stop.
Reads: 0x8f (IRQ source), 0x89 (volume, one per note start), 2x6.

What the emulator contributes, and how `dopeplay.py` handles it:

- **service times** of the interrupts: replayed from the `irq` lines. 2,949 of the 29,746 interrupts after the
  start are served more than 1.5 µs after their event (section 7.3), none before.
- **which ramp ends one interrupt serves**: all those that ended before it is served. Served at once, they share
  one 0x40d8 run, so the address bit carried in BX (9.2) differs from separate runs: 35 writes differ from the
  hardware-time stream for this reason.
- **the queue order of simultaneous ramp ends**: the emulator queues ramp ends in time order, or in voice order when
  its main loop stepped over several at once. In 7 interrupts (at 206.53, 224.59, 236.64, 281.00, 291.44, 405.51 and
  455.42 s; the first in tick 9915) it used voice order; `dopeplay.py` takes that order from the log (`--time-order` shows the 7 interrupts, 134 writes, that
  differ otherwise).
- **registers of the interrupted code**: EAX at the tick (`[0x22c0]`, 4xx) = 0x808 and EBX at the ramp IRQ (BX
  bit 15 → the first address bit) = 0 in the capture: the main program's protected-mode registers at the int 32h
  call, which the emulator restores for every handler run. On a real machine they are whatever PMODE's real-mode
  IRQ reflection leaves; for this song only EBX bit 15 matters (one half-sample address bit, sometimes).
- **reads**: the fake GUS answers 0x89 with its simulated ramp volume; the player only reads it when the voice is
  not ramping, where it equals the last register 9 value (or 0x400 after a ramp): the same as real hardware.

With the interrupts at their hardware times (`--ideal`), the 251,640 register writes differ from the capture in 169
places: 134 in the 7 reordered interrupts and 35 in register 3 / 5 / 0x0b bit 8.

---------------------------------------------------------------------------------------------------------------

## 11. GF1 model and render (`dopeplay.py --wav`)

The model of `jizz/remake/src/gf1.cpp` (paper/re/notes-music.md section 7): positions in 1/512 byte, increment
FC >> 1 per frame, 12-bit log volume with ramps (63 steps per frame), pan 0..15 with the linear law, 617,400 /
active voices frames per second (28: 22,050 Hz), 8-bit samples linearly interpolated, stopped voices mixed at their
parked position, forward loops, and **bidirectional loops** (voice control 0x18: the five "ping" instruments) as in
gf1.cpp. Writes of an interrupt apply at its time (the frames that start before it are rendered first). The render
uses the hardware schedule: timer overflows at the times of 7.2 (no lost ticks) and ramp interrupts at the ramp
ends of the card model.

The WAV starts at the first row tick and runs 458.03 s (22205 timer interrupts, as the capture, plus one tick
period), 22,050 Hz stereo, normalised to its peak (the mix peaks at 0.947 of one voice at full scale and gain 1, hard
panned; RMS about 0.13 left, 0.10 right: left-heavy because of pan 6).

For the remake (`render(ctx, time)` depends only on time): the player state is a function of the tick count;
the tick times follow 7.2; seek = replay the player from the start (22,205 ticks) and the GF1 voices from the last
note starts. The GF1 model must report ramp ends (to run 0x40d8 at that frame), answer register 0x89 with the
current volume and 0x8f from its IRQ queue, and take its frame rate from register 0x0e (32 voices during the init,
28 from the start).

---------------------------------------------------------------------------------------------------------------

## 12. Time bases

| variable | time base |
|---|---|
| GUS init, load, upload | none (start-up; the load runs with interrupts off) |
| player state (row, order, volumes, periods, vibrato, arpeggio) | player ticks = timer-2 interrupts, T(k) of 7.2 |
| tick period | the tempo after tick k − 2 (7.2); `[0x3f29]` unused |
| position `[0x1aa0]` / `[0x1a9c]` | changes at the tick that reads the row |
| voice switch for a new note | the ramp end, 0.16..1.84 ms after the tick (GF1 frames) |
| frequencies, pans, volumes | the tick, or the last ramp end of the tick if it starts notes |
| GF1 output | 22,050 frames per second |
| lost ticks | interrupts held off longer than a tick (part start-up `cli`): machine and phase dependent |
| music start | the first timer overflow after main calls 0x2c17 (after the load: seconds on a real PC) |
| music stop | main after CDPI returns (tick 22205 in the capture) |

---------------------------------------------------------------------------------------------------------------

## 13. Static data for the remake (read from loader.bin = file offset 0x17e0 + code32; do not copy)

| what | code32 | format |
|---|---|---|
| module | `DOPE.MOD` | section 4 |
| C-2 rates | 0xb37 | 16 u16 |
| periods (note matching), octave-1 periods | 0xb5b, 0xb8b | 96 u16, 12 u16 (inside the first) |
| vibrato table incl. the code bytes | 0x22a0 | 64 bytes; bytes 32..35 = EAX at the tick (08 08 00 00) |
| volume table | 0x3f9c | 65 u16 |
| frame rates per voice count | 0x3f76 | 19 u16 |
| initial pan | 0xd7d | 32 u8 |
| initial count | 0x3f2a | u8 0xc2 |
| constants in code | 0x2dc4 0xd87b50, 0x23ff / 0x2b2d 0x20ab0, 0x41fc 1000000, 0x4259 0x40 (ramp floor), 0x4275 0x60 | |

The remake needs: the load (4.2) to build the tracks, instrument records and DRAM image; the tick (8); the voice
logic of 9 driven by a GF1 model with ramp-end interrupts (11); the tick schedule of 7.2.

---------------------------------------------------------------------------------------------------------------

## 14. What the original approximates (keep for the sound; options for "sharper")

- 8-bit samples, linear interpolation at 22,050 frames per second, no band limiting: content above 11 kHz aliases.
  A sharper option runs the same voice model at the output rate with increments and ramp steps scaled by
  22,050 / rate (pitch, envelopes and timing unchanged) — a remake choice, not the original.
- Pitch: integer FC from two truncating divisions (0.2..7.3 cents flat). Keep the integer FC.
- Volume: the 65-entry table (compressed law), jumps on every change, 63-step note-off ramps. Keep.
- Timing: 320 µs timer steps with the remainder dropped (22.40 / 20.48 ms ticks). Keep.
- The quirks of 8.6 change what is played; they are the original's sound in any machine.

---------------------------------------------------------------------------------------------------------------

## 15. Open points / guesses

- Timer semantics (count reloaded at overflow, the status bit as a single flag) follow DOSBox and run_dope.py; the
  capture's tick times agree with them, real hardware was not checked.
- The handler sends the PIC EOI before reading the GUS status and serves one source per run. With an
  edge-triggered PIC, a GUS IRQ line that stays active because a second source is pending would not raise a new
  interrupt on real hardware; the emulator re-delivers it. It never happened in the capture (no interrupt with both
  a ramp end and a timer overflow pending), so it is not known whether the original can stall there.
- How many ticks the original loses at part starts on real hardware (section 7.3), and the registers PMODE passes
  to the handler (section 10): machine dependent.
- GF1 details shared with the JIZZ / Paper notes: pan law, clipping of the voice sum, the volume curve between
  mantissa steps, loop wrap at `pos > end` vs `>=`, the low byte latched for 8-bit writes to 16-bit registers.
- Effects not used by the song (B, D, 2, 5, 6, E1–E9, EC–EF, glissando) are ported from the code but not verified.
- The S3M loader 0x38b2 (with its effect converter 0x3cd6.., NTSC clock 0xda7790, limits 0x6b00 / 0x70) is not
  used by DOPE and was not decompiled.
