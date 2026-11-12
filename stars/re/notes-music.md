# STARS — music (scope "music": LCA's MOD-Player V3.02, GUS path)

Addresses are offsets in `unpacked/stars_img.bin` (= the program's near pointers). "Tick k" = the k-th run of the
player's GUS IRQ handler 0x1abb0 (k = 0 is the first), which is how `full.gus` numbers its lines; `full.ticks`
column 4 counts the ticks run so far. Times "after t_s" are measured from the moment 0x1a91c starts the GUS timers
(program time t_s = 3.98951..3.99001 s in the capture, during frame 279).

Everything below was checked, not only read: `models/music/lcaplay.py` (a routine-by-routine port, see its
README) builds from `unpacked/files/01.bin` and `stars_img.bin` alone

- the GUS DRAM: identical to `full.dram` (all 1,048,576 bytes);
- the register-write log from the card detection to the stop: 144,716 lines, byte-identical to `full.gus`;
- the music position after every tick: identical in all 26,387 `full.ticks` records that follow a tick;
- the tick times (section 5): consistent with every `full.ticks` record (one timer start t_s fits all 26,387
  frames to within the emulator's IRQ latency); the two plausible alternatives (a count written to the timer
  takes effect at once; timer 2 starts with the new count) are each contradicted by the records.

The song is rendered to a WAV through a GF1 model (section 10); there is no audio capture to compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- **Player**: a ProTracker-style MOD player written in assembly ("LCA's MOD-Player V3.02 (C) Cedric BERMOND (LCA)
  1993-95"), GUS and SB paths; the GUS path is used. 16 channels (`16CH`), **two GF1 voices per channel**
  (32 active voices → the GF1 runs at 617,400 / 32 = **19,293.75 frames per second**): every note starts on the
  other voice of the channel's pair while the old one is ramped down (no clicks, a short overlap).
- **Module** (overlay file 1, 780,560 bytes): MOD layout with a 256-byte order table and the signature at 0x4b8;
  31 samples, 155 orders, 73 patterns, 480,340 bytes of 8-bit samples. The patterns are converted into 6-byte
  events; the samples are uploaded by DMA, unchanged, to DRAM 0..0x75d74.
- **All timing is the GUS's**: the tick is GUS timer IRQ 7. Tempo T (Fxx ≥ 0x20) → `n = 1000000 / (32 T)` timer-1
  steps of 80 µs, truncated; n ≥ 256 → timer 2 with `n >> 2` steps of 320 µs, truncated again. Tempo 99 (orders
  0..0x0a) → 78 × 320 µs = **24.96 ms (40.064 Hz)** instead of 2.5 s / 99 = 25.25 ms: 1.17 % fast; tempo 115 (from
  0b:00) → 67 × 320 µs = **21.44 ms (46.642 Hz)** instead of 21.74 ms: 1.39 % fast. Speed 4 then 2: **10.016
  rows/s** in the intro, **23.32 rows/s** after it. Tick k is at **t_s + 81.92 ms + (k − 1) × 24.96 ms** for
  1 ≤ k ≤ 2938 and **t(2938) + (k − 2938) × 21.44 ms** after it (t(2938) = 73.38944 s); tick 0 at t_s + 20.48 ms.
- **Position** (0x1c7a3 / 0x29f80, what main and the parts sync to) = the *next* row to be read: the tick that
  reads row r of order o sets it to o:r+1. `cmp ax, X; jbe` waits therefore end at the tick that plays row X.
- **Register stream**: the player never reads a GF1 register that changes what it writes (no volume or position
  read-back, unlike Paper's and JIZZ's players), so `full.gus` is exactly what a real card receives.
- Effects used by the song: arpeggio (5), 1xx (89), 2xx (180), 3xx (965), 4xy (183), 9xx (112), Axy (3,910),
  Cxx (5,885), D00 (183), F02 / F04 / F0F / F00 (speed), F63 / F73 (tempo). Quirks that are part of the sound:
  arpeggio order +y, +x, base; vibrato phase never reset; volume and pitch changes are GF1 ramps; Exx are
  implemented but unused; 36 notes with periods below the FC table (50, 45) are silent; F00 at 9a:35 would
  freeze the player (main stops the music at 9a:34 first).

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when (capture) | log tick | what |
|---|---|---|
| main 0x6f, frame 46 | 0 | 0x1ba6c detect: bounce buffer, ULTRASND=, GF1 init, IRQ/DMA latches, SB probe (section 2) |
| main 0xa0..0xdb, frames 46–62 | 0 | allocate 0x177000 bytes → [0] (pattern buffer), load file 1, 0x1c71b (EAX = −1, ESI = module, EDI = [0]) → 0x1a1b0: convert, upload (30 DMA blocks), FC table, player state; main shrinks [0] to the patterns (EDI returned = end of the events, 448,512 bytes), frees the module |
| main 0xfe, frame 279 | 0 | 0x1c759 → 0x1a91c start: GF1 reset, timers started (t_s), IRQ 7 = 0x1abb0 |
| t_s + 20.48 ms (frame 281) | 0 | first tick (timer 1): reads row 00:00 (F63: switch to timer 2) — `stars_reference.mp4` starts at frame 281, ~1 ms before |
| t_s + 81.92 ms | 1 | first timer-2 tick, then every 24.96 ms |
| t_s + 73.36448 s | 2937 | reads 0b:00 (F73: tempo 115, from tick 2939 on every 21.44 ms); main leaves the intro |
| frame 26359 (t_s + ≈372.10 s) | 16870 | main 0x43a: 0x1c77e → 0x1a9a2 stop, after tick 16870 (position 9a:34) |

The sound card's start depends on the CPU (the precalc before main calls 0x1c759); from t_s on everything is clocked
by the GUS crystal. Nothing in the music depends on the frame rate or the PIT.

---------------------------------------------------------------------------------------------------------------

## 2. Detection 0x1ba6c (main 0x6f, AL = 0)

```c
int detect(int opts /*al*/)                                // returns AL = [0x1c802]
{
    [0x1c805] = opts & 3;                                  // 0x1c7f8 (SB options; 0 here)
    [0x1c800] = ds;                                        // the IRQ handlers load it
    print("...");                                          // 0x29f88 is a `ret`: every message is disabled
    if (!bounce_buffer()) goto none;                       // 0x29fb4
    [0x29c9c] = bounce; [0x29caa] = bounce_phys >> 16; [0x1c820] = 0xa0000 - [0x17620];   // 640 KB as near ptr
    if (bounce + 0xffff > [0x1c820]) goto none;
    [0x29ca4] = bounce + 0x8000;                            // (SB)
    memset(bounce, 0x80, 0x10000);
    gus_detect();                                          // 0x1bb0a: [0x1c804] = 0 if found
    sb_detect();                                           // 0x1bfda: [0x1c803] = 0 if found, type [0x29c8c]
    if (![0x1c804]) [0x1c802] = 0x20;                      // GUS
    else if (![0x1c803]) [0x1c802] = [0x29c8c];             // SB 1..4
    else none: [0x1c802] = 0, CF = 1;                      // main then runs the no-sound clock (section 8)
}
```

**Bounce buffer** 0x29fb4: `phys = (0x29fe3 + [0x17620] + 0xffff) & ~0xffff` (64 KB aligned physical address),
EBX = phys − [0x17620] (near pointer), DL = phys >> 16, ECX = 0xa0000 − [0x17620]. The 64 KB starting there lie
in the program's own zero-filled area 0x29fe3..0x3a000 (in the headless run at 0x30000). Only the sample upload
uses it: it copies every source at or above 640 KB there first (the module is above 1 MB).

**gus_detect 0x1bb0a**: [0x1c804] = 1; 0x1bb34 → 0x1bba7 reads `ULTRASND=` (string 0x29b90; 0x1bc07 searches the
first 0x400 bytes of the environment, 0x49fe4 → 0x17927 gives its address): numbers parsed by 0x1bbf4
(`v = v * base + (c − '0')` while c − '0' ≤ 0x39 unsigned; the terminator is consumed): port in base 16 →
[0x29b68], then base 10: DMA [0x29b82], second DMA [0x29b84], IRQ [0x29b86], second IRQ [0x29b88]. Not found →
"Gravis Ultrasound card not found." (CF, no GUS). Found → [0x1c804] = 0, the port table 0x1bceb, the GF1 init,
then 0x1bc2d: `ULTRA16=` (string 0x29b9a) → port [0x29b80] = 0x30c | (hex digit after the first character) << 4,
and 0x1bc57 writes words 0x802, 0x803 to it (the GUS MAX / 16-bit daughterboard codec; absent in the capture).
There is **no hardware probe**: the DRAM poke/peek test 0x1bc6b (0xaa / 0x55 at DRAM 0x10 / 0x11) is never
called, so the player trusts the environment variable.

Port variables (0x1bceb, base 0x240): [0x29b6a] 0x340 (MIDI control; its reads are the delays: 6 `in` per delay,
0x1bfc1 = 9 such delays), [0x29b6c] 0x342 voice select, [0x29b6e] 0x343 register select, [0x29b70] 0x344 data low
(16-bit writes), [0x29b72] 0x345 data high (8-bit writes), [0x29b74] 0x246 IRQ status, [0x29b76] 0x247,
[0x29b78] 0x248 / [0x29b7a] 0x249 AdLib (timer) control, [0x29b7c] 0x24d, [0x29b7e] 0x24e.

GF1 init 0x1bceb (logged lines in order; `w8` = register → 3X3, byte → 3X5; `w16` = word → 3X4):

```c
w8(0x4c, 0); delay9; w8(0x4c, 1); delay9;                  // reset, run
out(0x340, 3); delay9; out(0x340, 0);                      // MIDI reset
w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);                     // DMA, timer, sampling control off
w8(0x0e, ([0x29b8e] - 1) | 0xc0);                          // [0x29b8e] = 14 still: 0xcd
in(2X6); r8(0x41); r8(0x49); r8(0x8f);                     // acknowledge
for (v = 31; v >= 0; v--) { select(v); w8(0x00, 3); w8(0x0d, 3); }          // voices and ramps stopped
r8(0x41); r8(0x49); r8(0x8f); w8(0x4c, 7);                 // run, DAC on, GF1 IRQs on
for (v = 31; v >= 0; v--) { select(v); w16(2,0); w16(3,0); w16(4,0); w16(5,0); w8(6, 0x3f);
                            w16(9, 0); w16(0xa, 0); w16(0xb, 0); w8(0xc, 7); }
// IRQ / DMA latches (tables 0x29ba3: IRQ 2,3,5,7,11,12,15 -> 1,3,2,4,5,6,7; 0x29bb3: DMA 1,3,5,6,7 -> 1,2,3,4,5)
irq = T_irq[IRQ] | (IRQ2 == IRQ ? 0x40 : T_irq[IRQ2] << 3);              // 0x44
dma = T_dma[DMA] | (DMA2 == DMA ? 0x40 : T_dma[DMA2] << 3);              // 0x42
out(2XF, 5); out(2X0, 0xb); out(2XB, 0); out(2XF, 0);
out(2X0, 0xb); out(2XB, dma | 0x80); out(2X0, 0x4b); out(2XB, irq);
out(2X0, 0xb); out(2XB, dma);        out(2X0, 0x4b); out(2XB, irq);
select(0); out(2X0, 0xb); select(0); out(2X0, 8);                         // mix control
```

**sb_detect 0x1bfda** runs as well: for base 0x210, 0x220, .. 0x280: DSP command 0xd3 (0x1c618: waits up to
0x200 reads of 2XC for bit 7 clear, else gives up), DSP reset 0x1c21f (out 2X6 = 1, 256 reads, out 2X6 = 0, then
up to 256 × (0x1c63c: up to 0x200 polls of 2XE bit 7, read 2XA) for 0xaa). Nothing answers; `full.gus` shows the
resets of 0x246 .. 0x286 (`p246 1`, `p246 0`, ...), which fall in the GUS port range.

---------------------------------------------------------------------------------------------------------------

## 3. The module (file 1) and the conversion 0x1a1b0

### 3.1 Format (as 0x1a1b0 reads it)

```
0x000  20 bytes title (empty)
0x014  31 x 30 bytes sample headers: 22 name, u16be length/2, u8 finetune (0..15, 8..15 = -8..-1), u8 volume,
       u16be loop start/2, u16be loop length/2    (names carry Groo's message, see NOTES.md)
0x3b6  u8 song length (155), u8 restart (0; > length -> 0)
0x3b8  256 bytes order table (only the first 155 used; patterns = max entry + 1 = 73)
0x4b8  signature "16CH"
0x4bc  patterns: 73 x 64 rows x 16 channels x 4 bytes (Amiga MOD cells), then the samples (8-bit signed)
```

Channels from the signature (0x1a1df): two digits at 0x4b8 (either order) → [0x1c807] = 16; `M...` (M.K.) → 4;
otherwise byte 0x4bb `.`/`4` → 4, `6` → 6, `8` → 8 (and 16 GF1 voices), anything else → a 15-sample Soundtracker
module (sanity checks, error "Module convertion error"). More than 7 channels → GF1 voices [0x29b8e] = 2 ×
channels (here 32; otherwise 14). An unused sibling 0x19dd0 (entry 0x1c6dd, no caller) converts standard MODs
with the 128-entry order table and the signature at 0x438.

### 3.2 Conversion

```c
ESI = module; EDI = [0x1c80c] = pattern buffer; [0x1a1ac] = EAX (-1)
[0x1c806] = 31 samples; [0x29b8e] = 14 (then 32); [0x1c808] = 64 (max volume)
for (n = 1; n <= 31; n++) {                                // records of 0x18 bytes at 0x287c8 + 0x18 (n - 1)
    S.len  = 2 * be16(h + 0x16);                            // +0x00
    S.ft   = h[0x18];  S.vol = h[0x19];                    // +0x04, +0x05 (volume not clamped here)
    S.ls   = 2 * be16(h + 0x1a);                            // +0x08
    rep    = 2 * be16(h + 0x1c);
    S.flag = rep > 2 ? 8 : 0;                              // +0x06: GF1 voice control (bit 3 = loop)
    S.le   = min(S.ls + rep, S.len);                       // +0x0c
}
songlen [0x1c82a], restart [0x1c82b], orders -> 0x28b48 (256 bytes), npat [0x1c82c] = max + 1
for each of npat * 64 * channels cells (b0 b1 b2 b3):      // 0x1a38e: 6-byte events
    ev[0] = (b0 & 0xf0) | (b2 >> 4);                       // instrument
    ev[1] untouched;                                       // (unused)
    *(u16 *)(ev + 2) = (b0 & 0x0f) << 8 | b1;              // period
    *(u16 *)(ev + 4) = (b2 & 0x0f) << 8 | b3;              // effect << 8 | parameter
[0x1c810] = stride = 6 * channels = 96 bytes per row; pattern p at buffer + p * 64 * 96 (pointers 0x28c48[256])
```

Then the upload (3.3), the period table (3.4), and the player state:

```c
memset(0x29048, 0, 32 * 0x34);  for (c = 0; c < 32; c++) C[c].voice = 2 * c;    // channel records (6.1)
order = row = 0; counter = 1; speed = 6; loopcount = 0;     // [0x1c829], [0x1c828], [0x1c82d], [0x1c82e], [0x1c831]
tempo(125);                                                 // stored only: timer 1, count 6 (5.1)
ptr = pattern[orders[0]];                                   // [0x1c814]: the next row's events
return EDI = end of the events (0x1a57e, patched immediate)
```

### 3.3 Sample upload (0x1a3d3..0x1a477) and DRAM layout

```c
out(2X0, 0xb);                                              // mix control: output off during the upload
install IRQ 7 handler 0x1a8e2 (0x1a5f7); [0x29b8a] = 0      // DMA busy flag
edi = 0;                                                    // DRAM address
for (n = 1; n <= 31; n++) {
    S.dram = edi;                                           // +0x10
    if (S.len) {
        if (edi >> 18 != (edi + S.len) >> 18)               // would cross a 256 KB bank: next bank
            S.dram = edi = (edi + S.len) >> 18 << 18;
        upload(esi, S.len);                                 // the sample, as it is (8-bit signed)
        k = (S.le == 0 || S.le > 16) ? 16 : S.le;           // (S.le: loop END, not length)
        upload(sample + S.ls, k);                           // k bytes from the loop start appended at the end
        esi += S.len;
        if (!S.flag) S.le = S.len = S.len - 1;              // += [0x1a1ac]: one-shots end on their last byte
    }
    S.lerel = S.le;                                         // +0x14: the 9xx limit
    S.len += S.dram; S.le += S.dram; S.ls += S.dram;        // DRAM addresses from now on
    edi = (edi + 31) & ~31;
}
restore IRQ 7 (0x1a66c)
```

upload 0x1a699 (ESI source, EDI DRAM address, ECX bytes): ≤ 20 bytes → PIO 0x1a76f (per byte: reg 0x43 = address
low word, reg 0x44 = address bits 16..23, byte → port 3X7; after waiting for [0x29b8a] = 0). Otherwise DMA: a
source at or above 640 KB is first copied in 8-byte steps to the bounce buffer (ESI resumes at source + ECX
afterwards); a block that would cross a 64 KB physical page is split at the page end (then the DRAM address is
32-byte aligned again by PIO-ing up to 31 bytes); 0x1a7a6 waits for [0x29b8a] = 0, sets it, programs the 8237
(mask, clear flip-flop, mode 0x48 + channel = single, memory → card, address, count − 1, page from table 0x29cbd,
unmask; 16-bit channels on 0xd4.. with halved values) and the GF1: `r8(0x41)` (clears a pending DMA IRQ),
`w16(0x42, ((edi + 31) >> 5) * 2)` (DRAM address / 16, rounded up to 32 bytes; 16-bit channels use the GF1's
16-bit address form), `w8(0x41, 0x21)` (start, card ← memory, 8-bit, IRQ at the end; | 4 for 16-bit channels).
The IRQ handler 0x1a8e2 reads 2X6 until two reads agree and clears [0x29b8a] when bit 7 (DMA TC) is set. No byte
is changed on the way (reg 0x41 bit 6 "invert MSB" is off): DRAM = the module's bytes.

In this module every sample goes in one DMA block (none is longer than 64 KB, and the bounce buffer is 64 KB
aligned), and the tails by PIO. Result (`lcaplay.py --info`; addresses hex; "end" = the GF1 end register: the
loop end for looped samples, the last byte for one-shots):

| smp | DRAM start | loop start | end | loop | ft | vol | smp | DRAM start | loop start | end | loop | ft | vol |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | 76b4 | 9466 | yes | 0 | 64 | 17 | 440c0 | – | 451f3 | | 0 | 48 |
| 2 | 9f60 | ba54 | cc12 | yes | 0 | 64 | 18 | 45220 | – | 458c7 | | 0 | 64 |
| 3 | cc40 | 15658 | 1858c | yes | 0 | 64 | 19 | 458e0 | – | 4696f | | 0 | 48 |
| 4 | 185a0 | – | 252e3 | | 0 | 64 | 20 | 46980 | – | 48835 | | 0 | 48 |
| 5 | 25300 | 25300 | 2c12a | yes | 12 | 32 | 21 | 48860 | – | 49b11 | | 0 | 48 |
| 6 | 2c140 | – | 2d483 | | 0 | 64 | 22 | 49b40 | 4c898 | 4d1b0 | yes | 0 | 32 |
| 7 | 2d4a0 | – | 2faf3 | | 0 | 64 | 23 | 4d1c0 | 5a036 | 5b4be | yes | 0 | 64 |
| 8 | 2fb20 | – | 30e55 | | 0 | 32 | 24 | 5b4e0 | 5d9f0 | 5e09e | yes | 0 | 64 |
| 9 | 30e80 | – | 31e7f | | 0 | 29 | 25 | 5e100 | 66bec | 678da | yes | 0 | 16 |
| 10 | 31ea0 | – | 32fb3 | | 0 | 37 | 26 | 67ce0 | 69ab6 | 6c1c2 | yes | 0 | 64 |
| 11 | (empty) | | | | | | 27 | 6c1e0 | – | 6df5b | | 0 | 64 |
| 12 | 32fe0 | 3d61e | 3f052 | yes | 0 | 17 | 28 | 6df80 | – | 6f345 | | 0 | 64 |
| 13 | 3f080 | – | 3fa7d | | 0 | 64 | 29 | 6f360 | – | 70105 | | 0 | 64 |
| 14 | 40000 | – | 40b61 | | 0 | 64 | 30 | 70120 | – | 71861 | | 0 | 64 |
| 15 | 40b80 | – | 427ed | | 0 | 48 | 31 | 71880 | 73656 | 75d62 | yes | 0 | 20 |
| 16 | 42800 | – | 44099 | | 0 | 48 | | | | | | | |

Sample 14 was moved to the 256 KB bank at 0x40000 (gap 0x3faa0..0x3ffff). Where the loop ends at the sample's end
(2, 3, 5, 12, 22, 23) the appended bytes continue the loop for the interpolation; where it ends earlier (1, 24, 25,
26, 31) they are not behind the loop end, and the GF1 interpolates the last loop byte with the sample's own next
byte. A remake reads the samples from the module (file 1 offset 0x4bc + 73 × 4096
= 300,220 onwards, in sample order) and lays them out with these rules.

### 3.4 Period → GF1 frequency table 0x1a594 (dwords at 0x24940, index = Amiga period)

```c
D = u16 [0x29c5e + 2 * (voices - 14)];        // 617400 / voices, truncated: 32 voices -> 19293 (patched into 0x1a5d6)
memset(T, 0, 1000 * 4);                       // indices 0..999
for (p = 55; p <= 4000; p++) {
    q = (0xda7a69e2 / p + D) / D;             // u32 divisions; 0xda7a69e2 = 3,579,545.5 Hz (NTSC Amiga clock) * 1024
    // `add edx, edx; adc eax, 0` meant rounding, but 2 * remainder < 2^32 always: no effect
    T[p] = q & ~1;                            // = floor(K / p / D) rounded UP to even
}
```

FC = T[period] is written to reg 1; the GF1 plays (FC >> 1) / 512 bytes per frame, i.e. FC / 1024 × 19,293.75
bytes per second: period 214 → FC 888 → 16,731 Hz (ideal 3,579,545.5 / 214 = 16,727 Hz). Over periods 113..907
the result is −8.0..+7.4 cents off the ideal (the even rounding, coarse at low FC). Keep the integer FC.

The song goes beyond the Amiga's three octaves: periods 45..1076 reach the table (1076 → FC 176; 60 → FC 3166 =
3.09 bytes per frame). Periods below 55 give FC 0, and FC 0 means "nothing to write" (6.3): 36 note starts with
period 50 or 45 (instrument 16 on channels 8–10, rows 0x10 / 0x14 / 0x18 / 0x24 / 0x28 / 0x2c of orders
0x6c–0x6f, 0x74, 0x75, 0x87–0x8a) set the new voice's position and volume but neither its frequency nor its voice
control, so that voice — a one-shot that has run out — stays stopped: these notes are silent in the original
(the voice outputs the sample's first byte as DC). Periods above 4000 would read past the table (not reached).

---------------------------------------------------------------------------------------------------------------

## 4. Start, GF1 reset, stop

```c
void start(void)                               // 0x1a91c (via 0x1c759 when [0x1c802] = 0x20)
{
    gf1_reset();
    cli; out(2X8, 4); out(2X9, 0xe0);          // AdLib timer control: reset IRQ flags, mask both timers
    in(2X6); install IRQ 7 = 0x1abb0 (0x1a5f7: saves the old vector, unmasks IRQ 7 in the PIC)
    w8(0x4c, 7);
    out(2X8, 4); out(2X9, 3);                  // START timer 1 and timer 2  (= t_s)
    w8(0x45, 0); w8(0x46, 6); w8(0x45, 4);     // timer 1 count 6 (20 ms), timer-1 IRQ only
    out(2X0, 9); sti;                          // mix control: line in off, output on, latches on
}
void gf1_reset(void)                           // 0x1a9ec
{
    w8(0x4c, 0); delay; w8(0x4c, 0); w8(0x4c, 1);
    w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);
    w8(0x0e, (voices - 1) | 0xc0);             // 0xdf: 32 voices -> 19,293.75 Hz
    in(2X6); r8(0x41); r8(0x49); r8(0x8f);
    for (v = 0; v < voices; v++) {
        select(v);
        w8(0x00, 3) x2; w8(0x06, 0x3f) x2;     // stopped; ramp rate 63 per frame (never changed later)
        w16(0x09, voltab[0]);                  // 0x2000: GF1 volume 0x200 = -84 dB
        w8(0x0d, 3) x2; w8(0x0c, pan[v]) x2;   // pan table 0x29bbb (below); "x2" = written, delay, written again
    }
    r8(0x41); r8(0x49); r8(0x8f); w8(0x4c, 7);
}
void stop(void)                                // 0x1a9a2 (via 0x1c77e)
{
    out(2X0, 0xb); delay; out(2X0, 0xb);       // output off
    out(2X8, 4); out(2X9, 0xe0); w8(0x45, 0);  // timers masked, IRQs off
    gf1_reset(); restore IRQ 7 (0x1a66c); sti;
}
```

Pan (0x29bbb, voice 0..31; 0 = left, 15 = right): 3 3 12 12 12 12 3 3 3 3 12 12 12 12 3 3, then 7 × 16. Channel
c plays on voices 2c / 2c + 1: channels 0..7 → 3 12 12 3 3 12 12 3 (the Amiga L R R L layout, not hard panned),
channels 8..15 → 7 (centre, slightly left of 7.5).

---------------------------------------------------------------------------------------------------------------

## 5. Timers and tempo: how the music is clocked

### 5.1 Tempo → timer (0x1a514 at conversion, 0x1b688 for Fxx ≥ 0x20)

```c
void tempo(int T)                              // T = BPM, 0x20..0xff
{
    n = 1000000 / (T * 32);                    // u32 div: 31250 / T timer-1 steps of 80 us, truncated
    if (n >= 256) { n >>= 2; reg = 0x47; ctl = 8; }        // timer 2: 320 us steps, truncated again
    else          {          reg = 0x46; ctl = 4; }        // timer 1
    count = -n & 0xff;                         // [0x29b8b] = reg, [0x29b8c] = ctl, [0x29b8d] = count
}
// Fxx >= 0x20 then writes: w8(0x45, 0); w8(reg, count); w8(0x45, ctl);
```

Period = n × 80 µs (timer 1) or (n >> 2) × 320 µs (timer 2), against the MOD standard 2.5 s / T. Tempos 32..122
go to timer 2. The song: F63 (99, orders 0..0x0a: n = 315 → 78 → count **0xb2**, 24.96 ms) and F73 (115, from
order 0x0b: n = 271 → 67 → count **0xbd**, 21.44 ms). 82 writes of reg 0x47 in the capture (10 × 0xb2 from tick
0, 72 × 0xbd from tick 2937), never anything else. The conversion's tempo(125) (timer 1, count 6) is never used.

### 5.2 Every tick: acknowledge

The IRQ handler starts with `w8(0x45, 0); w8(0x45, [0x29b8c])`: clearing reg 0x45 acknowledges the timer IRQ,
the second write re-enables the timer in use (8 = timer 2 from tick 0's F63 on). Any GUS IRQ runs a tick (the
handler does not look at 2X6); after start only the timers can raise one (voice IRQs are never enabled: reg 0 is
only ever 0, 3 or 8; the DMA is idle).

### 5.3 Tick times

The GUS timers (AdLib-compatible) count from the start (2X9 = 3 at t_s) and at each overflow reload the count
register *as it is then*: a count written in a tick takes effect one period later. That is how run_stars.py and
DOSBox model them, and the capture's tick counts fit it (the two alternatives tested do not, see the top). So:

- tick 0 = timer 1's first overflow. Its count register was still 0 when the timers started (0x46 = 6 is written
  just after): (256 − 0) × 80 µs = **20.48 ms** after t_s (20.00 ms if the card loaded the count later: open
  point). Tick 0 reads 00:00 and switches to timer 2 (0x45 = 8, 0x47 = 0xb2).
- tick 1 = timer 2's first overflow; it started at t_s with count 0: **81.92 ms** after t_s.
- tick k+1 = tick k + (256 − c) × 320 µs, c = reg 0x47 at overflow k (written by tick k − 1 or earlier):
  24.96 ms up to tick 2938, 21.44 ms from tick 2938 → 2939 on (F73 is read by tick 2937).

```c
double tick_time(int k)                         // seconds after t_s
{
    if (k == 0) return 0.02048;
    if (k <= 2938) return 0.08192 + (k - 1) * 0.02496;
    return 73.38944 + (k - 2938) * 0.02144;
}
```

Rows: speed 4 × 24.96 = 99.84 ms (10.016 rows/s) in orders 0..0x0a (speed 15 from 0a:35: 374.4 ms rows), speed 2
× 21.44 = 42.88 ms (23.32 rows/s) from 0b:00 (speed 15 again from 9a:32). The music therefore runs on the
GUS crystal and 1.17 % / 1.39 % faster than the module's nominal tempo; **a remake must use these periods**
(and nothing derived from the PIT or the frame rate). The no-sound path (section 8) runs at other speeds.

In the capture t_s = 3.98951..3.99001 s program time, so tick k is at program time ≈ 3.98976 + tick_time(k) s and
in frame ≈ 70.0863 × that (frames start at line 0). For the remake, t_s is the music start: the time main calls
0x1c759 after its precalc (frame 279 in the capture).

---------------------------------------------------------------------------------------------------------------

## 6. The player tick 0x1abb0

### 6.1 State

Channel record (32 × 0x34 bytes at 0x29048; 16 used):

```
+0x04 u32 start     DRAM address the next note starts at      +0x08 ptr  sample record
+0x10 u32 period    +0x14 u32 tone-porta target (0 = none)    +0x18 u32 FC to write (0 = none)
+0x20 u16 effect    (effect << 8 | param) for the ticks        +0x22 u8 instrument
+0x23 u8 volume 0..64   +0x24 u8 finetune   +0x25 u8 arpeggio counter   +0x26 u8 voice control (8 loop / 0)
+0x27 u8 volume last sent to the GF1   +0x28 u8 porta speed   +0x29 u8 GF1 voice (2c or 2c+1)
+0x2a u8 vibrato (speed << 4 | depth)  +0x2b u8 tremolo  +0x2c u8 vibrato phase  +0x2d u8 tremolo phase
+0x2e u8 waveforms (vibrato bits 0-1, tremolo bits 4-5)  +0x2f u8 tremolo base volume  +0x30 u8 E9x counter
+0x31 u8 glissando  +0x32 u8 notes that still write the loop registers (2 after an instrument number)
```

Globals: row [0x1c828], order [0x1c829], song length [0x1c82a], restart [0x1c82b], counter (ticks left in the
row) [0x1c82d], speed [0x1c82e], loop count [0x1c831], loop row / order / pointer [0x1c832] / [0x1c833] /
[0x1c834], row pointer [0x1c814], stride [0x1c810], channels [0x1c807] (copied to the loop counter [0x1c838]).

Tables (static, read them from the exe): finetune periods 0x296c8 (16 × 36 u16, finetune 0 first: 856 .. 113;
finetunes 8..15 = −8..−1), sine 0x29b48 (32 bytes, 0 24 49 .. 255 .. 24: the half wave), volume 0x29bdc (65 u16:
0x2000, 0x95b5, 0xa4a6 .. 0xefa6 ≈ 0xefa6 + 0xf00 · log2(v / 64)), pan 0x29bbb, voice-count divisors 0x29c5e
(19 u16), latch tables 0x29ba3 / 0x29bb3, DMA page ports 0x29cbd.

### 6.2 Tick

```c
void irq(void)                                            // 0x1abb0
{
    w8(0x45, 0); w8(0x45, tctl);
    old = counter--;                                      // u8
    if (counter == 0 || (s8)counter < 0 != (old == 0x80)) {           // jle
        if ((s8)counter < 0) { counter = 0; goto eoi; }   // js 0x1b95d: stuck (after F00 or the song's end)
        row_tick();
    } else for (c = 0; c < channels; c++) tick_effect(&C[c]);
eoi: out(0x20, 0x20);                                     // (and 0xa0 for IRQs >= 8)
}
```

Once stuck (speed 0, or `counter = 0` at the end of the order list) every later tick only acknowledges the timer:
the voices keep sounding as they are (looped samples forever) until stop.

### 6.3 Row

```c
void row_tick(void)                                       // 0x1b210
{
    counter = speed; e = ptr;
    for (c = 0; c < channels; c++, e += 6) {
        C = &chan[c]; select(C.voice);
        if (e.ins) { S = sample[e.ins]; C.ins = e.ins; C.smp = S; C.vol = min(S.vol, 64); C.ft = S.ft;
                     C.ctl = S.flag; C.start = S.dram; C.newsmp = 2; }
        row_effect(C, period = e.period, eff = e.eff);    // table 0x1b298 (6.5)
    }
    if (++row >= 0x40) {                                  // u8
        row = 0;
        if (++order >= songlen) { counter = 0; order = restart; }   // the song stops at its end
        ptr = pattern[orders[order]];
    } else ptr += stride;
}
// every row handler ends in one of:
note:     /* 0x1b728 */ C.eff = eff;
          if (period) {
              note_start(C);
period_:  /* 0x1b810 */ if (C.ft) period = snap(period, C.ft);
              C.period = period; C.fc = T[period];
          }
volume:   /* 0x1b848 */ vol_update(C); fc_update(C);

void note_start(C)                                        // 0x1b734 (and 0x1afb3)
{
    w16(7, voltab[0]); w16(8, voltab[C.vol]); w8(0x0d, 0x40);   // the old voice ramps down to 0x200 (-84 dB)
    C.gvol = 0; C.voice ^= 1; select(C.voice);                    // the note goes to the other voice
    if (C.newsmp) { C.newsmp--;
        if (C.ctl) { w16(2, S.ls >> 7); w16(3, S.ls << 9); }    // loop start (looped samples only)
        w16(4, S.le >> 7); w16(5, S.le << 9); }                   // end: loop end / last byte
    w16(0x0b, C.start << 9) x2; w16(0x0a, C.start >> 7) x2;       // position (fraction 0)
}
void vol_update(C)                                        // 0x1b848 / 0x1b172
{
    if (C.gvol == C.vol) return;
    a = voltab[C.gvol]; b = voltab[C.vol];
    if (a < b) { w16(7, a); w16(8, b); w8(0x0d, 0x00); }  // ramp up to b
    else       { w16(7, b); w16(8, a); w8(0x0d, 0x40); }  // ramp down to b
    C.gvol = C.vol;
}
void fc_update(C)                                         // 0x1b8a4 / 0x1b1c6
{
    if (C.fc) { w16(1, C.fc); w8(0x00, C.ctl); C.fc = 0; }  // frequency, then (re)start the voice
}
u16 snap(p, ft)        // first i with (u16)p >= ftab[0][i] (or i = 36), return ftab[ft][i] (ftab[ft+1][0] if none)
```

All register writes go to the selected voice; 16-bit writes to the 8-bit ramp registers 7 / 8 put the high byte
in the register (3X5), so a ramp limit is `voltab[v] >> 8`: 4-bit exponent, 4-bit mantissa.

### 6.4 Tick effects (table 0x1ac04; only if C.eff ≠ 0)

```c
void tick_effect(C)
{
    x = C.eff >> 8; p = C.eff & 0xff;
    switch (x) {
    case 0:  a = C.arp++;                                         // arpeggio 0x1ac44
             if (a >= 2) { C.arp = 0; out_freq(C.period); break; }
             n = a == 0 ? p & 15 : p >> 4;                        // counter 0 -> +y, 1 -> +x, 2 -> base
             for (i = 0; i < 36 && (u16)C.period < ftab[C.ft][i]; i++);
             out_freq(ftab[C.ft][min(i, 35) + n]);  break;        // (the table row is C.ft's own here)
    case 1:  C.period -= p; out_freq(C.period); break;            // no limits
    case 2:  C.period += p; out_freq(C.period); break;
    case 3:  if (!C.target) break;                                // nothing is written
             toneporta(C); out_freq(C.period); break;
    case 4:  if (p) C.vib = nibbles(C.vib, p);                    // a 0 nibble keeps the old value
             out_freq(vibrato(C)); break;
    case 5:  if (!C.target) { volslide(C, p); out_vol(); break; }
             toneporta(C); volslide(C, p); out_freq(C.period); break;
    case 6:  q = vibrato(C); volslide(C, p); out_freq(q); break;
    case 7:  if (p) C.trem = nibbles(C.trem, p);
             d = (C.trem & 15) * wave(C.trempos, C.wave >> 4) >> 6; if (C.trempos & 0x80) d = -d;
             v = (u8)(C.tbase + d); C.vol = (s8)v < 0 ? 0 : min(v, 64);
             C.trempos += (C.trem >> 2) & 0x3c; out_vol(); break;
    case 9:  C.start = C.smp->dram; C.eff = 0; break;             // first tick after 9xx: later notes from 0
    case 10: volslide(C, p); out_vol(); break;
    case 14: switch (p >> 4) {
             case 9:  if (--C.retrig) break; C.retrig = p & 15; retrigger(C); break;
             case 12: if (counter != (p & 15)) break;              // compared with the DOWN-counter
                      C.vol = 0; select(C.voice); w16(7, voltab[0]); w16(8, voltab[0]); w8(0x0d, 0x40);
                      C.gvol = 0; break;
             case 13: if (counter == (p & 15)) retrigger(C); break;
             } break;
    }                                                             // 8, B, C, D, F: nothing
}
out_freq(p):  C.fc = T[p]; out_vol();                             // 0x1b15a
out_vol():    select(C.voice); vol_update(C); fc_update(C);       // 0x1b164
retrigger(C): select(C.voice); note_start(C); if (C.ft) C.period = snap(C.period, C.ft);
              C.fc = T[C.period]; vol_update(C); fc_update(C);    // 0x1afb3
int vibrato(C)                                                    // 0x1ad70: the period itself is unchanged
{   d = (C.vib & 15) * wave(C.vibpos, C.wave) >> 7; if (C.vibpos & 0x80) d = -d;
    C.vibpos += (C.vib >> 2) & 0x3c; return C.period + d; }
int wave(pos, w)   { i = (pos >> 2) & 31;  w &= 3;                // phase 0..255 = one cycle
    return w == 0 ? sine[i] : w == 1 ? (u8)(pos & 0x80 ? -(i << 3) : i << 3) : 255; }
void volslide(C, p) { if (!(p & 15)) C.vol = min(C.vol + (p >> 4), 64);
                      else C.vol = max(C.vol - (p & 15), 0); }    // 0x1aeac
void toneporta(C)                                                 // 0x1acd4
{   if (C.period > C.target) { q = C.period - C.pspeed; if (q < 0 || q <= C.target) goto done; C.period = q; }
    else if (C.period < C.target) { q = C.period + C.pspeed; C.period = q; if (q >= C.target) goto done; }
    else goto done;
    if (C.gliss) C.period = snap(C.period, C.ft);  return;        // E3x (unused)
done: C.period = C.target; C.target = 0; }
```

### 6.5 Row effects (table 0x1b298), then `note` unless stated

| effect | handler | on the row |
|---|---|---|
| 0xy | 0x1b2d8 | C.arp = 0 (also for "no effect" cells) |
| 1, 2, 4, 6, 8, A | 0x1b728 | nothing; the effect word is kept for the ticks |
| 3xx | 0x1b2e4 | a period becomes the target (snapped if finetune) and no note starts; xx ≠ 0 → speed |
| 5xy | 0x1b328 | as 3 without the speed |
| 7xy | 0x1b368 | C.tbase = C.vol |
| 9xx | 0x1b374 | if xx·256 ≤ S.lerel: C.start = S.dram + xx·256, else the effect is dropped |
| Bxx | 0x1b39c | row = 0x3f, order = xx − 1 (the row end moves on; xx ≥ song length stops the song) |
| Cxx | 0x1b3b4 | C.vol = min(xx, 64) |
| Dxy | 0x1b3cc | xy = 0 or BCD > 63 → row = 0x3f; else row = BCD − 1, order++ (wraps to restart without stopping), ptr = that row |
| E1x / E2x | 0x1b49c / 0x1b4e8 | with a note: snap, ∓x, start; without: period ∓ x, C.eff = 0, → `period_` (snaps again if finetune ≠ 0) |
| E3x, E4x, E5x, E7x | 0x1b534 .. 0x1b5fc | glissando, vibrato waveform, finetune, tremolo waveform |
| E6x | 0x1b56c | x = 0: remember order, row − 1 (0x3f of the previous order at row 0), ptr − stride; x > 0: count < x → jump back, count++; else count = 0 |
| E9x | 0x1b614 | C.retrig = x (kept for the ticks) |
| EAx / EBx | 0x1b624 / 0x1b644 | fine volume slide |
| EC0..ECF, E0, E8, EF | 0x1b728 | kept for the ticks (EC) / nothing |
| EDx | 0x1b660 | C.period = period, C.eff kept; nothing else (no note, volume or frequency now) |
| EEx | 0x1b66c | counter = x · speed (u8): the row lasts x · speed ticks (EE1 does nothing, EE0 stops the player) |
| Fxx | 0x1b688 | xx ≥ 0x20: tempo (5.1); else speed = counter = xx (this row already; F00 stops the player) |

### 6.6 Behaviour worth knowing (all of it is the sound of the demo)

- **Voice pairs**: a note start ramps the old voice from its volume down to 0x200 (−84 dB, rate 63 steps per frame:
  ≤ 53 frames = 2.7 ms) and starts the note on the other voice, which ramps up from ≈0x200 to its volume in the
  same 2.7 ms (an exponential fade-in in the GF1's log volume). The old voice keeps playing its sample under it.
- Volume changes (Cxx, Axy, instrument volume) are always ramps between `voltab` values, never jumps; a ramp ends
  on `voltab[v] >> 8` (4-bit mantissa: 1/16-octave steps). Volume law: gain(v) ≈ 0.484 · (v / 64)^0.94
  (v = 64 → GF1 0xef0 → 0.484 of full scale, 32 → 0.250, 16 → 0.133, 1 → 0.0103).
- Every frequency write also writes reg 0 = C.ctl, which restarts a one-shot voice that has stopped at its end
  (it stops again in the next frame: no audible effect). Conversely a note whose period has FC 0 (below 55)
  writes neither, and stays silent if its voice had stopped (3.4: 36 notes).
- A one-shot that ends parks on its last byte: the GF1 keeps outputting that byte × volume (DC) until the voice is
  used again.
- Arpeggio steps +y, +x, base (ProTracker: base, +x, +y), the counter is only reset by a row whose effect is 0;
  at speed 2 only base and +y are heard. Vibrato and tremolo phases are never reset by notes. 1xx / 2xx have no
  period limits; the FC table is defined for 55..4000.
- Tone portamento: a target of 0 means "arrived"; from then on the ticks of a 3xx row write nothing (the row tick
  itself still sends a pending volume change).
- 9xx moves only the note of its own row: the next tick resets C.start to the sample start.
- The capture exercises 0, 1, 2, 3, 4, 9, A, C, D, F and finetune 12 (sample 5); B, 5, 6, 7 and all Exx are
  ported from the code but unused by this song, so not verified against the capture.

---------------------------------------------------------------------------------------------------------------

## 7. The song

155 orders (patterns 0..72), 16 channels. Orders 0..0x0a: tempo 99 / speed 4, 64-row patterns, 256 ticks per
order; 0a:35 F0F slows the last 11 rows (15 ticks each). From 0b:00: F73 / F02 (repeated in most patterns), most
patterns end with D00 at row 0x2f (48 rows = 96 ticks = 2.058 s); orders 0x16, 0x69, 0x6a end at row 0x35 (108
ticks), orders 0x6b and 0x7e at 0x3b (120). Order 0x9a (pattern 72): F0F at row 0x32, F00 at 0x35 (the end).
6,305 notes start in the capture.

Position (AX of 0x29f80) passing main's wait values (`ax > X`): the tick that plays row X, its time after t_s and
the capture frame it falls in (t_s = 3.98976 s):

| X | tick | s after t_s | frame | X | tick | s after t_s | frame |
|---|---|---|---|---|---|---|---|
| 0a00 | 2560 | 63.955 | 4762 | 6100 | 11205 | 250.634 | 17845 |
| 0b00 | 2937 | 73.364 | 5421 | 6900 | 11973 | 267.100 | 18999 |
| 1700 | 4101 | 98.324 | 7170 | 6c00 | 12309 | 274.304 | 19504 |
| 2700 | 5637 | 131.256 | 9478 | 8700 | 14925 | 330.391 | 23435 |
| 3500 | 6981 | 160.071 | 11498 | 9500 | 16269 | 359.206 | 25455 |
| 4500 | 8517 | 193.003 | 13806 | 9519 | 16319 | 360.278 | 25530 |
| 5500 | 10053 | 225.935 | 16114 | 9600 | 16365 | 361.264 | 25599 |
| 5800 | 10341 | 232.110 | 16547 | 9700 | 16461 | 363.323 | 25743 |
| 5820 | 10405 | 233.482 | 16643 | 9800 | 16557 | 365.381 | 25887 |
| | | | | 9a00 | 16749 | 369.497 | 26176 |
| | | | | 9a20 | 16813 | 370.869 | 26272 |

(PARTS.md's part starts are in the same frame or the next.) `lcaplay.py --times` lists every tick.

---------------------------------------------------------------------------------------------------------------

## 8. Position, other entry points, no sound

- 0x1c7a3: AL = order [0x1c829], AH = row [0x1c828] (read through CS). 0x29f80: the same with AH / AL swapped
  (AX = order << 8 | row). Both are the next row to be read (6.3). 0x1c7b1 (set position: order, row, pointer)
  has no caller.
- 0x1c71b load (GUS 0x1a1b0, SB 1..4 0x182d5), 0x1c759 start (0x1a91c / 0x1867c), 0x1c77e stop (0x1a9a2 /
  0x1873c); CF = error. 0x1c684 (load with EAX = 0: one-shots keep their full length) and 0x1c6dd (GUS 0x19dd0 /
  SB 0x17fd0: standard MODs with the 128-entry order table and the signature at 0x438) have no caller.
- No sound ([0x1c802] = 0): main calls 0x49ff4 ([0x49fef] = 1, PIT callback 3 = 0x4a015), which per PIT tick
  (70.064 Hz) adds [0x49ff0] to the u16 [0x49ff2] and on carry does row++ (row 0x40 → 0, order++). main sets
  [0x49ff0] = 0x2492 (10.009 rows/s) before the intro and 0x7d00 (34.21 rows/s) after it; no pattern breaks
  (64-row orders), so all syncs after the intro come much earlier than with sound. 0x4a006 at the end: with sound
  ([0x49fef] ≠ 1) 0x4ac14 sets PIT callback 3 back to the `ret` 0x4abb4; without sound the clock keeps running.

---------------------------------------------------------------------------------------------------------------

## 9. The register stream (`full.gus`)

Lines `<tick> v<voice> r<reg> <value>` (voice registers; 8-bit writes log the byte, 16-bit writes the word),
`<tick> g r<reg> <value>` (0x41, 0x42, 0x45..0x47, 0x49, 0x4c), `<tick> p<port> <value>` (2X0, 2X6 (the SB
probe), 2X8 / 2X9, 2XB, 2XF, 3X0). Not logged: voice select, register select, DRAM address 0x43 / 0x44 and data
3X7. Sequence: 751 lines (tick 0) before the first tick (GF1 init and latches 375, SB probe 10, conversion 61,
start 305), then per tick `g r45 0`, `g r45 <4 or 8>` and the row / effect writes in the order of 6.3 / 6.4
(143,664 lines over 16,871 ticks), then after tick 16870 the stop (301 lines). 144,716 lines; reads (0x41, 0x49,
0x8f, 2X6) are acknowledgements whose values are never used, so real hardware gets the same stream.

---------------------------------------------------------------------------------------------------------------

## 10. GF1 model and render (`lcaplay.py --wav`)

The model of `paper/re/notes-music.md` section 7 / `jizz/remake/src/gf1.cpp` (forward loops and one-shots only):
32 active voices → 19,293.75 frames per second; per frame and voice: out += interp(DRAM[a], DRAM[a+1],
fraction / 512) / 128 × gain(volume) × (L = (15 − pan) / 15, R = pan / 15); position += FC >> 1 (1/512 byte); a
looped voice past its end wraps to start + overshoot, a one-shot parks on its end; volume ramps of 63 steps (of
4096) per frame between reg 7 and reg 8 (high bytes << 4); gain(v12) = (256 + (v12 & 255)) · 2^((v12 >> 8) − 24).
Stopped voices are mixed at their parked position. Writes of one tick are applied at the tick time (5.3), with
the fractional frame position carried (a tick every 481.6 or 413.7 frames).

The WAV starts at tick 0 and ends one tick period after tick 16870 (372.09 s); 1.0 = one voice at full scale with
gain 1, hard panned; the mix peaks at 1.047, RMS ≈ 0.1 of the peak, so it is normalised to its peak (×0.9).
The header says 19,294 Hz (13 ppm fast).

For the remake (`render(ctx, time)` depends only on time): the player state is a function of the tick count;
seek = replay the player from the conversion (16,871 ticks are instant in C++) and the GF1 voices from the last
note starts; time ↔ tick by 5.3. The C++ GF1 of JIZZ (`jizz/remake/src/gf1.cpp`) fits once its active-voice
count follows reg 0x0e (32 here).

---------------------------------------------------------------------------------------------------------------

## 11. What the original approximates (keep it for the sound; options for "sharper")

- 8-bit samples, linear interpolation at the GF1 frame rate, no band limiting. With 32 active voices that rate is
  only 19,293.75 Hz: content above 9.6 kHz aliases (FC up to 3,166 = 3.09 bytes per frame). Exact: run the voices at
  19,293.75 Hz and resample the mix. A "sharper" option is to run the same voice model at the output rate with
  increments and ramp steps scaled by 19,293.75 / rate (pitch, envelopes and timing unchanged; only aliasing and
  interpolation differ) — a remake choice, not something the original does.
- Pitch: the FC table (3.4), integer and rounded up to even: ±8 cents. Keep the integer FC.
- Volume: 65 table entries, ramp limits with a 4-bit mantissa, every change a 63-steps-per-frame ramp (≤ 2.7 ms),
  note starts as a 2.7 ms cross-fade between the two voices of a pair. Keep.
- Timing: ticks on the GUS timer with the truncated periods 24.96 / 21.44 ms (5.3). Keep these periods; they set
  where every part of the demo starts.

---------------------------------------------------------------------------------------------------------------

## 12. Time bases

| variable | time base |
|---|---|
| detection, conversion, upload | none (start-up) |
| player state (row, order, volumes, periods, effect phases) | player ticks = GUS timer IRQs (5.3) |
| position 0x29f80 | changes at the tick that reads a row (it then names the next row) |
| GF1 output | GF1 frames at 19,293.75 Hz, register writes at the tick instants |
| music start t_s | machine dependent (main's precalc before 0x1c759) |
| music stop | main (0x43a) after the credits, which wait for retraces: tick 16870 in the capture |
| no-sound position | PIT ticks (70.064 Hz), 0x4a015 |

---------------------------------------------------------------------------------------------------------------

## 13. Static data for the remake (read from STARS.EXE, do not copy)

| what | where | format |
|---|---|---|
| module | overlay file 1 (DIET), 780,560 bytes unpacked | 3.1 |
| finetune periods | image 0x296c8 | 16 × 36 u16 |
| vibrato / tremolo sine | image 0x29b48 | 32 u8 |
| volume table | image 0x29bdc | 65 u16 (high byte = ramp limit) |
| pan table | image 0x29bbb | 32 u8 |
| voice-count divisors (617400 / n) | image 0x29c5e | 19 u16 for n = 14..32 (only the n = 32 entry, 19293, is used) |
| FC table | computed (3.4) from 0xda7a69e2 and the divisor | |
| sample record layout, DRAM layout | computed (3.2, 3.3) | |

---------------------------------------------------------------------------------------------------------------

## 14. The SB path (listed only)

0x1bfda probe (0x1c1fc ports 0x210..0x280, 0x1c21f DSP reset, 0x1c249 DSP E0 test, 0x1c28b version E1 → type
[0x29c8c] 1..4 (SB, SB 2.0, SB Pro, SB16; mixer set-up 0x1c664 / 0x1c674), 0x1c39c, 0x1c402 IRQ / DMA test with
the 0x29cbd page table, 0x1c618 DSP write, 0x1c63c DSP read); 0x182d5 conversion of this module format (0x17fd0:
4-channel variant, no caller); 0x1867c start (0x187af, 0x187c7, 0x187d2: DSP helpers), 0x1873c stop, IRQ
handler with the software mixer (0x18948 ..; mixing loops 0x193e0 / 0x194e8 called from 0x18a18, 0x18c44,
0x18eac, 0x19140), 0x185e4. Not decompiled.

---------------------------------------------------------------------------------------------------------------

## 15. Open points / guesses

- The first timer-1 period (tick 0): 20.48 ms if the timer's count register is still 0 when 2X9 = 3 starts it
  (run_stars.py, DOSBox), 20.00 ms if a card applies the count written right after. Only tick 0 moves.
- Timer semantics in general (count reloaded at overflow, timer 2 starting at t_s with count 0) follow DOSBox and
  fit the capture; not checked on a real GUS.
- The GF1 caveats of the JIZZ and Paper notes: pan law (linear here), clipping of the voice sum (peak 1.047 of one
  voice's full scale), mantissa-linear vs exponential volume curve, ramp overshoot at the limits.
- Whether a GF1 loop wraps at `pos > end` or `pos >= end` (the model uses `>`; one frame of difference per wrap).
- Effects the song does not use (B, 5, 6, 7, Exx, glissando, F00 freeze) are ported from the code and unverified.
- The stop time is main's (credits counted in retraces), not the music's: tick 16870 in the capture.
