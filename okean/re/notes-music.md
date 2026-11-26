# The Sea Robot of Love — music (scope "music": the ProTracker player and its GUS driver)

Addresses are code32 offsets (file offset in `unpacked/korso2.bin` = 0xec0 + offset; `rep.py code32`). "Interrupt
n" is the numbering of `full.gus` (the number before each line) and of the `mtick` column of `full.ticks`: the n-th GUS
timer-2 interrupt served. Interrupts 1..3 come before the player is started; **player tick j = n − 3** (j = 1 reads
row 00:00). Times are emulated seconds of the capture (t = 0: the GUS init in main) unless stated otherwise.

Everything below was checked, not only read: `models/music/okeanplay.py` (a routine-by-routine port, see its README)
builds from `unpacked/korso2.bin` alone (the module is part of the program image)

- the GUS DRAM: identical to `full.dram` (all 1,048,576 bytes);
- the register / port write log from the GUS init to the stop: 70,027 lines, byte-identical to `full.gus`;
- the music position (order index, row, tick countdown) of all 9,107 `full.ticks` records after the start.

The check takes from the capture only the service time of each interrupt (the `irq` lines). With the interrupts at
their hardware times instead (`--ideal`: every interrupt at its event, the capture's 8 held-off timer interrupts at
the part starts kept), the 70,027 lines are again identical except for the times on the `irq` lines: no value written
depends on when an interrupt is served, only on the order of the interrupts, and the handler fixes that order (section
7). The song is rendered to a WAV through a GF1 model (section 12); there is no audio capture to compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- **Player**: a ProTracker MOD player with a GUS driver in code32 0x10414..0x14bd3. It is a version of DOPE's
  "ProTracker for GUS replay routine (C) 1994 Oskari Tammelin" (`dope/re/notes-music.md`): the same channel
  record layout, track format, tick structure and effect handlers, with a different GUS driver; it carries no
  copyright string (the intro's INFOFILE credits "the player by Phantom"). Section 15 lists what is the same and
  what differs.
- **Module**: an 8-channel MOD (`8CHN`) inside the program at code32 0xae0..0x100c4, read in place. 31 orders, 14
  patterns, 8 samples (33,192 bytes), uploaded unchanged to GUS DRAM 0..0x81a7. Samples 11 and 13..20 are named
  "oskari panningit", "1:3", "2:7", "3:6", "4:8", "5:7", "6:8", "7:6", "8:C".
- **The song**: tempo 125 throughout (F7D at 00:00 sets the load's default again), speed 12 for rows 00:00..00:39,
  speed 6 from 00:40 (F0C, F06). Order 0 has 64 rows (624 ticks = 12.38 s), every other order 48 (D00 at row 47:
  288 ticks = 5.71 s). Order 30 ends with B1D (back to order 29), which the intro never reaches: main stops the
  music at 30:37. Effects used: C (volume), A (volume slide), EB (fine volume down), EC2 (note cut), E91 / E92
  (**ignored by this player**), D, B, F. No vibrato, portamento, arpeggio or pan effects.
- **Timing**: GUS timer 2 at count 0xc2: **62 × 320 µs = 19.84 ms per tick** (50.40 Hz) instead of 20 ms. The
  count is computed after every tick from the tempo T: `0x100 − (1000000 / (2T / 5)) / 320` (truncating divisions)
  and written at once. Tick n is the GUS timer overflow n + (overflows lost so far) after the init. Each part's PIT
  calibration runs with interrupts off for 20..40 ms and holds one tick off; the capture loses one overflow at
  each of the 7 part starts after the first (music 139 ms behind the GUS clock at the end). Section 8.
- **Voices**: channel c plays GF1 voice c; 16 voices active (38,587.5 frames per second). A new note first ramps the
  voice's volume down (to 0x040, 63 steps per frame) **with the volume-ramp IRQ enabled, and the timer interrupt
  handler waits for that IRQ**: it sends its EOI early, enables interrupts and busy-waits until the nested ramp
  interrupt has switched the voice to the new sample. The voices are processed one after the other (0..15), so a
  note starts 0.7..1.65 ms after the tick per new note on a lower or equal voice number (up to 5.4 ms in this song),
  and every write for a higher voice waits as long. Section 6, 7, 10.
- **Pitch**: FC = round(Hz / 37), Hz = 14,187,344 / period. The card at 16 voices plays 37.683 Hz per FC unit,
  so **the whole song plays 1.85 % (31.7 cents) sharp** (26..36 cents per note with the rounding).
- **Volume**: 0..63 (not 64: every clamp is 0x3f), through a 64-entry GF1 volume table (0x143ba) indexed by
  `vol × master >> 6`; jumps, no ramps up. **Pan**: written once per voice by the load from the player's table
  0x10575 (3 7 7 7 7 8 6 C for channels 1..8) and never again: a note sets the channel's pan to 7, which equals the
  cached 7, so nothing is written (the song has no E8x).
- **The fade at the end**: the last part's retrace callback lowers the master volume `[0x12b8c]` from 63 by one per
  refresh from 30:32 on (32 refreshes, to 31), and the main loop then leaves. The master only enters on a channel's
  next volume write, so the fade reaches each channel at its next note or slide.
- **Quirks kept in the sound**: E9x (retrigger) does nothing; EC0 never cuts; 8xy does nothing (E8x sets the pan);
  900 restarts the sample; F00 does nothing; arpeggio and vibrato are not used by the song (vibrato would read 32
  bytes of the module instead of its sine: 16-bit addressing, section 9.5).
- **Register read**: the ramp-down reads the current volume with register select 0x09 instead of 0x89; the
  emulator answers 0 (so `r08` is always 0 in the capture), real hardware answers something undefined. It only
  matters for one address bit (section 11).

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when (capture) | interrupt | what |
|---|---|---|
| main 0x1019a, t ≈ 0 | 0 | 0x142d7 detect (`call [0x14310]` = 0x14b4b: `ULTRASND=`); 0x142f8 copies the driver table; `call [0x12b48]` = 0x14935 GUS init (section 3): GF1 reset, 16 voices, latches, DRAM probe, **timer 2 started with count 0xc2** (19.84 ms), IRQ 7 handler 0x1443c with `[0x14385]` = 0x2a0 (a `ret`: no player yet) |
| main 0x101b7 | 0 | 0x13f28 load (ESI = 0xae0, section 4) with interrupts off (`cli` .. `sti`): pan table, tracks, sample upload. Instant in the capture; on a real PC the PIO upload of 33 KB takes on the order of 0.1 s |
| 0.019840, 0.039680, 0.059520 | 1..3 | timer overflows: the handler acknowledges and returns |
| first part's set-up (frame 3, 0.0634 s) | 3 | 0x13eb1 start (section 5): `[0x14385]` = 0x14591 |
| 0.096417 (overflow 4 at 0.079360, held by the part's PIT calibration) | 4 | **first player tick**: row 00:00 (F7D, F0C), first note (voice 3) |
| the parts | 4..9203 | the GUS interrupt plays the music; the parts read the position `[0x12b75]` / `[0x12b73]` |
| last part, from 30:32 (182.117 s) | 9172.. | the last part's callback lowers the master volume `[0x12b8c]` 63 → 31 over 32 refreshes |
| main 0x10241, after interrupt 9203 (position 30:37, 182.74 s) | 9203 | 0x13f18 stop (section 5), `call [0x12b4c]` = 0x148ca GUS shutdown |

Without a GUS (`ULTRASND` missing) `[0x142d6]` = 1, nothing of the player runs and the parts' callbacks count a
fake position (core scope).

---------------------------------------------------------------------------------------------------------------

## 2. Data

### 2.1 Globals (code32)

| address | size | meaning |
|---|---|---|
| 0x10390 | 12 u16 | periods of the base octave: 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907 (note → period) |
| 0x103a8 | 16 u16 | C-2 rates per finetune 0..15: 8363, 8413, 8463, 8529, 8581, 8651, 8723, 8757, 7895, 7941, 7985, 8046, 8107, 8169, 8232, 8280 |
| 0x103c8 / 0x103ca | u16 | period limits: 0x6b00 / 0x70 in the image, set by the load to 0xd60 = 3424 (856 × 4) / 0x1c0 = 448 (112 × 4) |
| 0x103cc | 36 u16 | ProTracker periods 856 .. 113 (period → note, at load) |
| 0x10414 | u32 | 0xd87b50 = 14,187,344 = 4 × 3,546,836 Hz (PAL Amiga clock × 4; also stored by the load) |
| 0x10418 | 4 | "PTM/" |
| 0x1041c | 48 | title: 20 bytes from the module + 28 zero bytes (the module's title is 20 zero bytes) |
| 0x1046e | u8 | speed (ticks per row): 6 after the load |
| 0x1046f | u8 | tempo (BPM): 125 after the load |
| 0x10470 | u8 | patterns: max over all 128 order bytes + 1 (14) |
| 0x10471 | u8 | instruments (31) |
| 0x10472 | u8 | song length (31) |
| 0x10473 | u8 | channels (4; `6CHN` → 6, `8CHN` → 8) |
| 0x10475 | 256 | order table: 128 bytes from the module + 128 zero bytes |
| 0x10575 | 32 u8 | GF1 pan per voice, written by the load: 3 7 7 7 7 8 6 c, then 1 e e 1 six times (static) |
| 0x10c50 | 31 × 0x50 | instrument records (2.2) |
| 0x12b40 | u32 | the load's read pointer in the module |
| 0x12b44 | 45 | copy of the driver table 0x14310 (11 pointers + 1 byte, section 3) |
| 0x12b71 | u8 | tick countdown (1 in the image and at the start) |
| 0x12b72 | u8 | row (0x3f at the start) |
| 0x12b73 | u16 | the same row as a word, for the effects and **for the parts** (0 at the start) |
| 0x12b75 | u8 | order index (0xff at the start): **read by the parts** |
| 0x12b76 | u8 | copy of the order index (written only) |
| 0x12b77 | u8 | "playing" (1 start, 0 stop): nothing reads it |
| 0x12b78 / 0x12b79 | u8 / u32 | current pattern / pattern × 0x140 (the latter written only) |
| 0x12b7d | u8 | break row: when the row equals it, the next row tick starts the next order (0x3f, or Bxx/Dxx/E6x's row) |
| 0x12b7e | u8 | first row of the next order (Dxx) / loop row (E6x) |
| 0x12b7f | u8 | pattern delay (EEx) |
| 0x12b80 / 0x12b81 | u8 | E6x loop row / loop count |
| 0x12b82 | u8 | 1: arpeggio notes above octave 5 become B-5 (9.3) |
| 0x12b83 | u8 | load status (0; written only) |
| 0x12b84 | u32 | DRAM upload pointer |
| 0x12b88 | u32 | pattern × 0x140 during the load |
| 0x12b8c | u32 | **master volume, 63**: lowered by the last part (section 10.5) |
| 0x12b90 | u8 | note without instrument: keep the volume |
| 0x12b91 | u32 | channel loop counter (channels .. 1), read by 0x14284 |
| 0x12b98 | 16 × 0x40 | channel records (2.3) |
| 0x12f98 | 32 u8 | vibrato half sine 0, 24, 49 .. 255 .. 24 (**not read**: 9.5) |
| 0x12fb8 | 8 × 72 u8 | 0x14284: last volume per channel and note (octaves 1..6), written only |
| 0x13438 | u32 | EAX at the tick call = `[0x14381]` (written only) |
| 0x142d6 | u8 | 1 = no GUS: no music (the parts run their fake position clock) |
| 0x1436c | u16 | GUS base port (0x220 in the image; 0x240 from ULTRASND) |
| 0x1436e / 0x14370 / 0x14372 / 0x14374 | u16 | 3x2 voice select, 3x3 register select, 3x4 data low, 3x7 DRAM data |
| 0x14376 | u8 | DRAM banks found by the probe (written only) |
| 0x14377 / 0x1437b | u32 | `((2T / 5) << 32) / 0x411`, 0: written by 0x14714, never read (left from a PIT version) |
| 0x1437f | u16 | 20 (written only) |
| 0x14381 | u32 | +20 per tick (milliseconds of a nominal 20 ms tick), passed to the tick in EAX: unused |
| 0x14385 | u32 | timer callback: 0x2a0 (`ret`) or 0x14591 (the player) |
| 0x14389 | u8 | IRQ (11 in the image; 7 from ULTRASND; 2 is used as 9) |
| 0x1438a | u8 | timer-2 count written by the init: 0xc2 |
| 0x1438c | 16 | real-mode stub (PMODE 0x346) |
| 0x143a1 | u8 | register select restored at the end of every GUS interrupt (0x43 after the upload) |
| 0x143a2 | 2 u16 | `89 c0` (mov eax, eax) / `e6 a0` (out 0xa0, al): patched into 0x14447 (slave PIC EOI for IRQ > 7) |
| 0x143a6 | 16 u8 | IRQ → GF1 IRQ latch: 0 0 41 43 0 42 0 44 0 0 0 45 46 0 0 47 (IRQ 7 → 0x44) |
| 0x143b6 | u32 | old real-mode vector |
| 0x143ba | 64 u16 | volume table (register 9 values), 0x6e00 .. 0xff80 |
| 0x144b1 | u8 | ramp flag: cleared by the timer path before it waits, set by the ramp path |

### 2.2 Instrument record (31 × 0x50 bytes at 0x10c50; the image has "PTS/" at +0 and 8363 at +0x44 of each)

```
+0x11  32 bytes name (22 from the module + 10 zero)   +0x31 u8 flags: 1 loop
+0x32  u32 length (bytes; 0 if <= 2)                    +0x36 u32 loop start
+0x3a  u32 loop end = min(ls + 2 replen - 2, length - 1) + 2      (normally ls + 2 replen)
+0x3e  u8 volume (not clamped)   +0x3f u8 finetune & 15   +0x40 u32 DRAM start   +0x44 u16 C-2 rate = word [0x103a8 + 2 ft]
loop flag:  (replen - 1) * 2 > 2 (signed), i.e. replen >= 3 words. No bidirectional loops.
```

### 2.3 Channel record (16 × 0x40 bytes at 0x12b98; channel c plays on GF1 voice c)

```
+0x00 u32 track: this channel's events (extended memory)   +0x04 u32 DRAM start of the sample
+0x08 u32 loop start (DRAM)   +0x0c u32 end (DRAM): loop end, or start + length - 1
+0x10 u8 pan 0..15 (7 at every note; E8x)   +0x11 u8 change flags: 1 volume, 2 pan, 4 frequency, 8 (re)start the sample
+0x12 u8 GF1 voice control: 0 one-shot, 8 loop   +0x13 u8 volume 0..63
+0x14 u32 loop start written last (0xffff in the image)   +0x18 u32 end written last (0xffff)
+0x1c u16 frequency written last (0x00ff)   +0x20 u8 pan written last (7)   +0x21 u8 instrument (1-based; see 9.5)
+0x22 u16 GF1 frequency (FC)   +0x24 u32 sample offset (9xx; 0 at every row)
+0x28 u8 effect   +0x29 u8 parameter   +0x2a u16 period (4 x Amiga period)   +0x2c u16 vibrato position (low byte used)
+0x2e u8 vibrato depth (low nibble)   +0x2f u8 vibrato speed (high nibble)   +0x30 u16 note period (porta target,
vibrato centre)   +0x32 u16 C-2 rate   +0x34 / +0x36 / +0x38 u16 arpeggio periods   +0x3a u8 arpeggio counter 0..2
+0x3b u8 note: octave << 4 | note 1..12 (octaves 3..5 = ProTracker's 1..3)   +0x3c u8 tone porta speed   +0x3d u8 ticks since the row
```

The start resets only the volumes (+0x13) of the 16 records; everything else keeps the image values above (zero
except the caches) or the load's track pointers.

---------------------------------------------------------------------------------------------------------------

## 3. Detection, driver table, GUS init

- 0x142d7: `call [0x14310]` = 0x14b4b; carry → `[0x142d6]` = 1, AL = 0; else AL = 1.
- 0x14b4b: searches the environment (PSP `[0x10]`, segment at +0x2c) for `ULTRASND`; 0x14ba9 reads numbers (skip
  non-digits, then digits in base EDI): the port in base 16 → `[0x1436c]`, two DMA channels in base 16 (ignored),
  the IRQ in base 10 → `[0x14389]`. Not found: carry. (No menu, unlike DOPE.)
- 0x142f8: copies 11 dwords + 1 byte from `[0x142f0 + 4 AL]` (= 0x14310 for the GUS) to 0x12b44. The driver table
  0x14310: detect 0x14b4b, init 0x14935, shutdown 0x148ca, start 0x14714, stop 0x1475b, upload signed 0x14788,
  upload unsigned 0x1482d (`xor 0x80`), upload delta 0x147d5, download 0x1487d, pan table 0x1490c, name 0x1433d
  ("Gravis UltraSound"), then the byte 7. The MOD path uses init, start, stop, shutdown, upload signed and pan table.

```c
void gus_init(void)                             // 0x14935, main 0x101b1 `call [0x12b48]`
{
    int31(0x0900);                              // virtual interrupts off (restored at the end)
    out(base, 0x0b);                            // mix control: line in off, output off
    ports: 3x2 [0x1436e], 3x3 [0x14370], 3x4 [0x14372], 3x7 [0x14374]
    w8(0x4c, 0); delay; w8(0x4c, 1); delay;     // reset, run (delay: 7 reads of port 0x300)
    w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);
    w8(0x0e, (16 - 1) | 0xc0);                  // 0xcf: 16 active voices, 38,587.5 frames per second
    for (v = 31; v >= 0; v--) repeat 2x {       // bl ^= 0x80: each voice twice
        select(v); w16(0x09, 0); w8(0x00, 0); w8(0x0d, 3); w8(0x06, 0x3f); w8(0x0c, 7); delay;
    }                                           // (voice control 0: the voices run, at address 0 with FC 0, volume 0)
    out(2xF, 5); out(2x0, 8); out(2xB, 0); out(2xF, 0);
    out(2x0, 0x4b); out(2xB, latch[irq]);       // 0x44: IRQ 7 for both GF1 interrupts; no DMA latch
    out(2x0, 9);                                // output on, latches on
    w16(0x43, 0xffff);                          // DRAM probe: byte at bank * 0x40000 + 0x3ffff, banks 0..3:
    for (cl = 3; !(cl & 0x10); cl += 4) {       //   write it + 1, read back; [0x14376] = banks that answer
        w8(0x44, cl); b = in(3x7) + 1; out(3x7, b); if (in(3x7) != b) break; [0x14376]++; }
    w8(0x4c, 7);                                // run, DAC on, GF1 interrupts on
    w8(0x47, [0x1438a] /*0xc2*/); w8(0x45, 8);  // timer 2: count 0xc2 (62 x 320 us), IRQ on
    out(2x8, 4); out(2x9, 2);                   // start timer 2
    if (irq == 2) irq = 9;
    patch word 0x14447 = irq > 7 ? `out 0xa0, al` : `mov eax, eax`;   // self-modifying: slave PIC EOI
    setirqvect(irq, 0x1443c) ([0x2c]); unmask (0x2fe); real-mode stub (0x346, EDI = 0x1438c) -> [0x143b6];
    r8(0x8f);                                   // acknowledge
    int31(old interrupt state);
}
```

The probe leaves bytes 0x3ffff, 0x7ffff, 0xbffff and 0xfffff of the capture's 1 MB DRAM at 1. The timer starts
here: its overflows come every 19.84 ms from the init on, whether the player runs or not.

---------------------------------------------------------------------------------------------------------------

## 4. Module load 0x13f28 (ESI = 0xae0)

### 4.1 Format as read

```
0x000  20 bytes title (all zero here)       0x014  31 x 30-byte sample headers: 22 name, u16be length/2,
                                                   u8 finetune, u8 volume, u16be loop start/2, u16be replen/2
0x3b6  u8 song length (31)   0x3b7 restart (127, ignored)   0x3b8  128 orders
0x438  tag: "6CHN" -> 6, "8CHN" -> 8, anything else -> 4
0x43c  patterns: 64 rows x channels x 4 bytes (Amiga cells), npat = max(all 128 orders) + 1 = 14
then   the samples, in instrument order (8-bit signed)
```

The module is read where it is in the program image (code32 0xae0); it ends at 0x100c4, where main begins.

### 4.2 Pseudo-C

```c
cli;
[0x12b40] = mod + 0x43c;                        // read pointer
copy 20 title bytes -> 0x1041c, then 28 zero bytes;
channels = 4; if (tag == "6CHN") channels = 6; if (tag == "8CHN") channels = 8;
[0x10414] = 0xd87b50;
pan_table(0x10575);                             // 0x1490c: w8(0x0c, pan[v]) for voices 0..31
speed = 6; tempo = 125; [0x10471] = 31; maxp = 0xd60; minp = 0x1c0;
instruments (2.2); orders -> 0x10475 (+128 zeros); [0x10472] = song length; pattern = orders[0];
npat = max(orders[0..127]) + 1;
for (c = 0; c < channels; c++) {                // per channel: npat * 64 events of 5 bytes
    C[c].track = getmem_high(npat * 0x145);     //   (0x140 used per pattern; no memory: PMODE exit 0x3f3)
    fill with {note 0, instrument 0, volume 0xff, effect 0, parameter 0};
}
for (p = 0; p < npat; p++) {
    cells = [0x12b40]; [0x12b40] += channels * 256;
    for (c, row): (b0 b1 b2 b3) = cell; e = C[c].track + p * 0x140 + row * 5;
        period = (b0 & 15) << 8 | b1;
        e.note = period ? (i = first index with per36[i] == period, else 36; (i / 12 + 3) << 4 | i % 12 + 1) : 0;
        e.instrument = (b0 & 0xf0) | b2 >> 4;
        if ((b2 & 15) == 0xc) e.volume = b3;    // Cxx becomes the volume byte (effect stays 0 00)
        else { e.effect = b2 & 15; e.parameter = b3; }
}
dram = 0;
for (i = 0; i < 31; i++) if (I[i].length) {
    if (I[i].length > 0x10000) exit;            // jmp 0x3f3
    I[i].dram = dram; upload(source = [0x12b40], dram, I[i].length);   // [0x12b58] = 0x14788
    [0x12b40] += I[i].length; dram += I[i].length;
}
sti;
```

Notes are matched to the 36 ProTracker periods **exactly**; a period not among them would become note 0x61 (none in
this module). 0x14788 uploads by PIO, one byte at a time: register 0x44 = address >> 16 (again after every 64 KB),
register 0x43 = address & 0xffff (16-bit write to 3x4), the byte to 3x7; `[0x143a1]` keeps the selected register
(0x44, then 0x43) so the IRQ handler can restore it. The samples are uploaded unchanged (DOPE's player zeroed their
first bytes; this one does not).

### 4.3 DRAM layout (`okeanplay.py --info`; hex; loop and end addresses are DRAM addresses)

| ins | name | DRAM start | length | loop start | loop end / end | loop | finetune | C-2 rate | vol |
|---|---|---|---|---|---|---|---|---|---|
| 1 | dune/orange | 0 | 6462 | 3c | 1938 | yes | 14 (−2) | 8232 | 31 |
| 2 | | 193e | 9298 | 29ee | 3d8e | yes | 14 | 8232 | 23 |
| 3 | | 3d90 | 5868 | 4eee | 540a | yes | 0 | 8363 | 64 |
| 4 | | 547c | 148 | | 550f | | 12 (−4) | 8107 | 64 |
| 5 | | 5510 | 794 | | 5829 | | 12 | 8107 | 44 |
| 6 | | 582a | 1482 | | 5df3 | | 0 | 8363 | 64 |
| 7 | | 5df4 | 6616 | 5e50 | 77c6 | yes | 14 | 8232 | 31 |
| 9 | | 77cc | 2524 | 7f18 | 81a6 | yes | 0 | 8363 | 64 |

Instruments 8 and 10..31 are empty. A one-shot's end is `start + length − 1`, its last byte: a finished one-shot
voice parks there (instrument 4: +4, 5: 0, 6: −1). A loop end is `loop start + 2 × replen`, the first byte after the
loop.

---------------------------------------------------------------------------------------------------------------

## 5. Start 0x13eb1, stop 0x13f18, shutdown 0x148ca

```c
void start(void)                    // called by the first part's set-up (0x2c26f)
{
    for (c = 0; c < 16; c++) C[c].vol = 0;
    countdown = 1; row_word = 0; row = 0x3f; break_row = 0x3f; next_row = 0; pattern_delay = 0;
    order -= 1 /* 0 -> 0xff */; playing = 1; loop_count = loop_row = 0;
    // `call [0x12b50]` = 0x14714:
    [0x14377] = ((2 * tempo / 5) << 32) / 0x411; [0x1437b] = 0; [0x1437f] = 20; [0x14381] = 0;   // unused
    [0x14385] = 0x14591;                        // the next timer interrupt runs the player
}
void stop(void)                     // main 0x10241: `call [0x12b54]` = 0x1475b
{
    [0x14385] = 0x2a0;
    for (v = 15; v >= 0; v--) { select(v); w8(0x00, 3); }      // stop the voices (volumes stay)
    playing = 0;
}
void shutdown(void)                 // main 0x10246: `call [0x12b4c]` = 0x148ca
{
    restore the real-mode vector (0x388, [0x143b6]); mask the IRQ (0x2fe); w8(0x4c, 0); out(2x0, 0x0b);
}
```

---------------------------------------------------------------------------------------------------------------

## 6. The GUS interrupt 0x1443c

```c
void gus_irq(void)                                  // IRQ 7, protected mode
{
    pushad; push ds es fs gs;
    out(0x20, 0x20); [patched: out(0xa0, 0x20) for IRQ > 7];   // EOI first ..
    sti;                                            // .. and interrupts on: the handler can nest
    st = in(2x6);
    if (st & 0x40) ramp_irq();                      // 0x144b2
    else if (st & 0x08) {                           // timer 2
        w8(0x45, 0); w8(0x45, 8);                   // acknowledge, re-enable
        [0x14385]();                                // 0x14591 (or 0x2a0: ret)
    }
    out(3x3, [0x143a1]);                            // restore the register select
    pop; iretd;
}

void timer_tick(void)                               // 0x14591
{
    [0x14381] += 20; tick(EAX = [0x14381]);         // 0x1343c, section 9
    hz = tempo * 2 / 5;                             // 50 at 125 BPM (u32 divisions, truncating)
    n = 1000000 / hz / 320;                         // 62
    w8(0x47, 0x100 - n);                            // 0xc2: used from the overflow after the next one (8.1)
    for (v = 0; v < 16; v++) {                      // ecx = 15 .. 0, voice = ecx ^ 15
        select(v);                                  // (every voice, also without changes)
        f = C[v].flags; C[v].flags = 0;
        if (!f) continue;
        if (f & 8) {                                // (re)start the sample: fade the old note first
            bx = r16(0x09);                         // select 0x09 (not 0x89): undefined; the emulator answers 0
            w8(0x07, 0x04);                         // ramp start (low limit) 0x040
            w8(0x08, bx >> 8);                      // ramp end (high limit): irrelevant for a downward ramp
            w8(0x0d, 0x60);                         // ramp down, IRQ at the end
            [0x144b1] = 0;
            while (![0x144b1]) ;                    // 0x14646: interrupts are on; the ramp IRQ nests here
            f = 7;
        }
        update(v, f);                               // 0x14663
    }
}

void ramp_irq(void)                                 // 0x144b2 (only ever runs nested in the wait above)
{
    cli;
    v = r8(0x8f) & 31;                              // the voice whose ramp ended (bit 6 is not checked)
    select(v); w8(0x0d, 3); w8(0x00, 3);            // stop the ramp and the voice
    // bx: EBX of the interrupted code = the value read from select 0x09 in timer_tick (0 in the capture)
    if (C[v].lstart != C[v].cached_ls) { C[v].cached_ls = C[v].lstart; addr(2, 3, C[v].lstart); }
    if (C[v].end != C[v].cached_end)   { C[v].cached_end = C[v].end;   addr(4, 5, C[v].end); }
    addr(0x0a, 0x0b, C[v].start + C[v].offset);
    w8(0x00, C[v].ctl);                             // start: 0 one-shot, 8 loop
    [0x144b1] = 1;
    sti;
}
addr(hi, lo, a): bx = (bx >> 7) | (a << 9);         // shrd bx, si, 7 (16 bit)
                 w16(hi, a >> 7);                   // address bits 7..19
                 w8(lo, bx >> 8);                   // high byte of the low register: address bits 0..6 << 1, and in
                                                    //   bit 0 (= register bit 8, the fraction's 1/2) bit 15 of the
                                                    //   previous BX: bit 6 of the previous address written in this
                                                    //   interrupt, or bit 15 of the select-0x09 read for the first

void update(int v, int f)                           // 0x14663 (the voice is selected)
{
    if (f & 4) {
        if (C[v].period) {
            hz = 0xd87b50 / C[v].period;            // u32 divisions
            C[v].fc = (hz * 100 + 1850) / 3700;     // = round(hz / 37): remainder >= 1850 rounds up
        }
        if (C[v].fc != C[v].cached_fc) { C[v].cached_fc = C[v].fc; w16(0x01, C[v].fc); }
    }
    if ((f & 2) && C[v].pan != C[v].cached_pan) { C[v].cached_pan = C[v].pan; w8(0x0c, C[v].pan); }
    if (f & 1) w16(0x09, voltab[C[v].vol * master >> 6]);     // master [0x12b8c] = 63 until the last part's fade
}
```

The low address register is written with an 8-bit write to 3x5, so on a GF1 its low byte (the fraction bits 7..0)
is the byte 3x4 last latched: the low byte of the 16-bit write just before, `(a >> 7) & 0xff`. The emulator logs
it as 0 (`r03 dc00`); the GF1 model uses the latch, as `dope/remake/src/gf1.cpp` does. So a sample starts up to
255/512 of a byte past its address, and loop start and end carry such fractions too: less than one sample.

The ramp runs at register 6 = 0x3f: 63 steps (of 4096) per GF1 frame, from the current volume down to 0x040:
`(vol12 − 0x40) / 63` frames, 1.65 ms from volume 63 (0xff0), 0.70 ms from volume 0 or 1 (0x6e0). After its own
ramp the voice always gets a volume write (f = 7), so every later ramp starts from a table value (≥ 0x6e0); only
the first note on each voice ramps from the init volume 0, which is at once below 0x040 (section 7).

---------------------------------------------------------------------------------------------------------------

## 7. The interrupts: EOI, nesting and the wait

What happens on a tick with a new note, on real hardware:

1. Timer 2 overflows; the GF1 sets its timer-2 status and raises its IRQ line (ISA, active high). The master 8259
   (edge-triggered) latches IRQ 7 in its request register, the CPU takes the interrupt (PMODE: ring 0, interrupt
   gate, IF cleared), the 8259 sets in-service bit 7.
2. The handler sends a non-specific EOI at once. In-service bit 7 is the only one set (IRQ 0 has the higher
   priority and its handler runs with interrupts off until its own EOI, so a GUS interrupt never finds it in
   service), so the EOI clears it. Then `sti`.
3. It writes register 0x45 = 0 (the GF1 clears its timer status: the IRQ line drops) and 8, runs the tick, writes
   the count, and for the first voice with a new note starts a downward volume ramp with its IRQ enabled and spins
   on `[0x144b1]` with interrupts on.
4. The ramp ends (0.7..1.65 ms later); the GF1 sets the voice's ramp IRQ and raises the line again: a new rising
   edge, so the 8259 requests IRQ 7 again, and since bit 7 is no longer in service and IF = 1, the CPU enters the
   handler a second time, on top of the spinning one. Its EOI clears the new in-service bit; status bit 6 sends it
   to the ramp path, whose read of register 0x8f clears the voice's ramp IRQ (the line drops); it switches the voice
   and sets the flag.
5. The outer handler sees the flag, writes the voice's frequency, pan and volume, and goes on with the next voice
   (another ramp and nested interrupt per new note), then returns.

IRQ 0 (the parts' retrace timer, every refresh) can interrupt the spinning handler at any time; its callback runs
and the ramp interrupt waits for its EOI. Nothing in the callbacks writes to the GUS; the last part's callback
changes the master volume, which the next volume write of the handler uses (section 10.5). The capture shows that
interleaving, and the model reproduces it from the refresh times.

**Does the capture's order follow from this?** Yes. The register order is fixed by the code: per tick
`r45 0, r45 8, r47`, then per voice 0..15 with changes `[r07, r08, r0d 60, (ramp interrupt: r0d 3, r00 3, [r02 r03],
[r04 r05], r0a r0b, r00 ctl)], [r01], [r0c], [r09]`. There is nothing for the timing to reorder: one ramp runs at a
time, the next tick cannot come while a voice waits (the waits of a tick add up to at most 5.4 ms of the 19.84 ms),
and the parts' callbacks write nothing to the card. The emulator (run_okean.py) models the PIC with request bits
on rising edges of the GUS line, in-service bits, priorities and non-specific EOI, and takes interrupts only at
instruction boundaries with IF = 1: the same behaviour. Its 2,494 ramp interrupts come with the ramp end (to 0.1
µs), except the 8 instant ramps of the first note on each voice, which it serves up to one emulation quantum (0.2
ms) late; that moves no write.

Hazards of this design on real hardware (none occurs in the capture):

- **An instant ramp could be lost.** If the ramp ended before the handler clears `[0x144b1]` (between the `out` of
  0x60 and the `mov` two instructions later), the nested interrupt would set the flag first, the outer handler would
  clear it and spin for ever with the main program suspended. Only a ramp that starts at or below 0x040 can end that
  fast: the first note of each voice (from the init volume 0), 8 times at the start of the song. Whether a GF1
  ends such a ramp within a microsecond is not known (guess: it ends it at the voice's next volume update, within
  one frame of 26 µs, which is long enough).
- **A ramp end and a timer overflow pending together** would leave the GUS line high after the handler serves the
  ramp (it tests bit 6 first and serves one source), so no new edge would come and the music would stop. It needs a
  ramp interrupt held off until the next overflow: more than 14 ms of interrupts off inside the wait. Not seen.
- **A tick longer than 19.84 ms** (all waits plus an IRQ 0 callback) would let the next timer interrupt nest into
  the spinning handler and run a tick inside a tick. Not seen; the longest tick here is 5.4 ms.

---------------------------------------------------------------------------------------------------------------

## 8. Tempo, timer and tick times

### 8.1 Tempo → count

| tempo T | 2T / 5 | 1e6 / (2T / 5) µs | / 320 | count `r47` | tick period | nominal 2.5 s / T |
|---|---|---|---|---|---|---|
| 125 (load default, F7D at 00:00) | 50 | 20000 | 62 | 0xc2 | **19.84 ms** (50.40 Hz) | 20.00 ms |

The tempo never changes in this song, so every count written is 0xc2 (also the init's `[0x1438a]`). In general:
the GF1 reloads its counter from register 0x47 at each overflow, so a count written in the handler of overflow k
first sets the period from overflow k + 1 to k + 2 (one tick later than the tempo change; DOPE's player wrote it one
tick later still). 2T / 5 truncates (T = 126 gives 50 too), and 1e6 / (2T / 5) / 320 truncates twice: tempo 125
runs 0.8 % fast.

### 8.2 Speed and rows

Speed 6 after the load. 00:00 sets F7D (tempo 125) and F0C (speed 12, also the countdown, so row 00:00 already
lasts 12 ticks); 00:40 sets F06. Order 0: rows 0..39 at 12 ticks (238.08 ms), rows 40..63 at 6 (119.04 ms): 624
ticks. Orders 1..30: rows 0..47 at 6 ticks (D00 at row 47): 288 ticks = 5.714 s. Player tick j at which row r of
order o is read:

```c
j(0, r) = 1 + 12 r                    r < 40
        = 481 + 6 (r - 40)            40 <= r <= 63
j(o, r) = 625 + 288 (o - 1) + 6 r     1 <= o <= 30, r <= 47
// order 30 row 47: D00 (channel 0) and B1D (channel 5): the next row would be 29:00; the intro stops at 30:37
```

### 8.3 Tick times

```c
// n = timer interrupt served (n = j + 3), L(n) = overflows lost before it, t_init = the GUS init (capture: 0)
overflow(m)   = t_init + m * 0.01984          // the GUS clock: count 0xc2 from the init on
event(n)      = overflow(n + L(n))
served(n)     = event(n), or later if the CPU has interrupts off (or IRQ 0 in service) at that moment
```

The first player tick is overflow 4 (79.36 ms after the init), the first after the start call. In the capture
L(n) = 0 for n < 629, then one more at each part start: 1 from n = 629, 2 from 2357, 3 from 3509, 4 from 4743, 5
from 5813, 6 from 6966, 7 from 8181.

### 8.4 Held and lost ticks at the part starts

Each part's set-up measures one refresh with the PIT (0x14c67 in the first part's copy of the retrace timer) with
interrupts off: `cli`; wait for the end of a retrace if one is on; wait for the next retrace start; start the PIT;
wait for the next retrace start (one frame, 20.06 ms); read the PIT; wait for that retrace's end (2 lines, 64 µs);
load the count; `sti`. The GUS interrupt that falls in this window is served at its end, 2.6..3.5 µs after the end
of that retrace in the capture; a second overflow in the window finds the timer status still set and is lost (the
status is one bit). `analysis.py`:

| part (start frame) | interrupt | held overflow | served | late | lost | music behind the GUS clock from then |
|---|---|---|---|---|---|---|
| 0x2c26f (3) | 4 | 4 at 0.079360 | 0.096417 (end of frame 4's retrace) | 17.06 ms | 0 | 0 |
| 0x2e779 (621) | 629 | 629 at 12.479360 | 12.513299 (frame 623) | 33.94 ms | 1 | 19.84 ms |
| 0x3554b (2331) | 2357 | 2358 at 46.782720 | 46.815186 (frame 2333) | 32.47 ms | 1 | 39.68 ms |
| 0x367e5 (3471) | 3509 | 3511 at 69.658240 | 69.683111 (frame 3473) | 24.87 ms | 1 | 59.52 ms |
| 0x38520 (4693) | 4743 | 4746 at 94.160640 | 94.195921 (frame 4695) | 35.28 ms | 1 | 79.36 ms |
| 0x3c8da (5752) | 5813 | 5817 at 115.409280 | 115.439019 (frame 5754) | 29.74 ms | 1 | 99.20 ms |
| 0x5334f (6893) | 6966 | 6971 at 138.304640 | 138.327003 (frame 6895) | 22.36 ms | 1 | 119.04 ms |
| 0x4d370 (8096) | 8181 | 8187 at 162.430080 | 162.458682 (frame 8098) | 28.60 ms | 1 | 138.88 ms |

(The first part's set-up calls the start before its calibration, so the first player tick is the held one.) Besides
these, 99 interrupts are served 0.04..0.12 ms late (mostly 0.06 ms), when they arrive while a part's retrace IRQ 0
handler waits for the retrace with interrupts off; that only moves the tick.

**On real hardware** the same `cli` runs, so the original loses ticks too. The window is (up to one frame until the
next retrace start) + one frame + 64 µs = 20.1..40.2 ms; with the overflows every 19.84 ms it contains one or two of
them (rarely three), so a part start holds one tick for up to 40 ms and loses 0, 1 or 2, depending on where in the
frame the set-up ends and on the GUS timer's phase; on average about 0.5 per start (the capture: 7 in 8 starts).
The music then stays behind the GUS clock by the lost ticks, and the parts, which follow the music position, end
correspondingly later.

---------------------------------------------------------------------------------------------------------------

## 9. The tick 0x1343c

### 9.1 Row or effects

```c
void tick(void)
{
    [0x13438] = eax;                                      // (written only)
    if (--countdown) return tick_effects();               // 0x136de
    countdown = speed;
    if (pattern_delay) { pattern_delay--; return tick_effects(); }
    if (row == break_row) {                               // next order
        row = row_word = next_row;
        if (++order == song_length) order = 0;
        [0x12b76] = order; pattern = orders[order];
    } else row++;
    next_row = 0; break_row = 0x3f;
    for (c = 0; c < channels; c++) {
        row_word = row;
        row_channel(&C[c], track[c][pattern * 64 + row]);
        if (C[c].flags & 1) vis(c);                       // 0x14284
    }
}
```

On a row tick no tick effect runs. The position the parts read: order `[0x12b75]`, row word `[0x12b73]`, both
changed at the tick that reads the row; the capture's `full.ticks` records `[0x12b75]`, `[0x12b72]`, `[0x12b71]`.
0x14284 (`vis`) stores the channel's volume at `0x12fb8 + 72 × c + 12 × octave + note − 1 − 12` for notes of
octaves 1..6 (index 0..71): a table nothing reads.

### 9.2 Row: one channel (0x134f4..0x136bc)

```c
void row_channel(Chan *C, Event e)
{
    C->offset = 0; C->effect = e.effect; C->param = e.param;
    if (e.note) {
        ins = e.instrument; keep_vol = 0;
        if (!ins) { ins = C->ins; if (!ins) goto noteless; keep_vol = 1; }
        C->ins = ins; I = &instrument[ins - 1];
        C->note = e.note; C->c2spd = I->c2spd;
        C->base = ((oct[(e.note & 15) - 1] * 0x20ab0) >> (e.note >> 4)) / I->c2spd;   // u32, -> u16
        if (e.effect == 3 || e.effect == 5) goto noteless;          // tone porta: only the target
        C->period = C->base; C->flags = 0x0c; C->pan = 7; C->ctl = 0; C->vibpos = 0; C->offset = 0;
        C->start = I->dram; C->end = I->dram + I->length - 1;
        if (I->flags & 1) { C->lstart = I->dram + I->ls; C->end = I->dram + I->le; C->ctl |= 8; }
        if (!keep_vol) { C->vol = min(I->volume, 63); C->flags |= 1; }
        if (e.effect == 0xe && e.param >> 4 == 0xd) C->flags = 0;
        goto volume;
    }
noteless:
    if (e.instrument) { C->ins = e.instrument - 1;                 // 0-based here (see 9.5)
                        C->vol = min(instrument[e.instrument - 1].volume, 63); C->flags |= 1; }
volume:
    if (e.volume != 0xff) { C->flags |= 1; C->vol = min(e.volume, 63); }        // Cxx
    C->tickcount = 0;
    row_effect(C);                                       // tables 0x1374c (effect) / 0x13794 (Exy)
}
```

The note period is in units of a quarter Amiga period. 0x20ab0 = 16 × 8363, so it is `oct[n] × 16 × 8363 >> octave`
divided by the C-2 rate, truncated: 3424 = 4 × 856 for C of octave 3 (ProTracker's C-1) at finetune 0, scaled by
8363 / C-2 rate for other finetunes.

### 9.3 Row effects

| effect | handler | on the row |
|---|---|---|
| 0xy | 0x13da0 | if xy: arp counter = 0; arp[0] = period; arp[1], arp[2] = the note + x, + y semitones: `(oct[n] × 0x20ab0 >> octave) / c2spd` with n = note − 1 + x and, while n > 11, octave++, n −= **11**; if `[0x12b82]` = 1 and octave ≥ 6: octave 5, n = 11 |
| 1, 2, 5, 6, 7, 8, A, C | 0x136bc | nothing (**8xy too**; C is the volume byte) |
| 3xx | 0x13aec | xx = 0 → porta speed; porta speed = parameter = xx |
| 4xy | 0x13a30 | 0 nibbles keep the old depth / speed; parameter = depth \| speed |
| 9xx | 0x138c1 | offset = xx << 8, at most end − start − 1; **flags \|= 8 (restarts the sample, also for 900 and without a note)** |
| Bxx | 0x13ca6 | break_row = row word; order = xx − 1 |
| Dxy | 0x1388a | xy ≥ 0x64 → 0x63; next_row = 10x + y; break_row = row word |
| Fxx | 0x1385c | xx = 0: nothing; xx > 0x1f: tempo = xx; else speed = countdown = xx |
| E1x / E2x | 0x13c1a / 0x13c48 | period ∓= 4x, limited to [448, 3424] (unsigned); flags \|= 4 |
| E5x | 0x13d0a | base = period = the note's period with c2spd[x] (no flag) |
| E6x | 0x13d46 | x = 0: loop_row = row word; else: count = x if 0, else count−− and stop at 0; while looping: break_row = row word, next_row = loop_row, order−− |
| E8x | 0x13cfe | pan = x; flags \|= 2 |
| EAx / EBx | 0x13c76 / 0x13c90 | volume ± x, limited to 0..63; flags \|= 1 |
| EDx | 0x13ce5 | flags = 0 (the note starts at tick x) |
| EEx | 0x13cee | pattern_delay = x |
| E0, E3, E4, E7, **E9**, **EC**, EF | 0x136bc | nothing |

### 9.4 Tick effects (0x136de; tables 0x137d4 / 0x1381c)

```c
for (c = 0; c < channels; c++) {
    C->tickcount++; if (++C->arpcount == 3) C->arpcount = 0;      // every channel, every tick
    switch (C->effect) {
    case 0x0: if (C->param) { C->period = C->arp[C->arpcount]; C->flags = 4; }   // `mov`: other flags dropped
    case 0x1: if (C->period) { C->period -= 4 * xx; if ((s16)C->period < 448) C->period = 448; C->flags |= 4; }
    case 0x2: if (C->period) { C->period += 4 * xx; if (C->period > 3424) C->period = 3424; C->flags |= 4; }
    case 0x3: porta(C, 4 * C->param, C->base); C->flags |= 4;
    case 0x4: C->vibpos += (C->param & 0xf0) >> 2;                // speed x 4 per tick, 256 = one cycle
              d = (vib[(C->vibpos >> 2) & 31] * (C->param & 15)) >> 7;   // vib: 32 bytes at 0x2f98 (9.5)
              C->period = C->base + 4 * (C->vibpos & 0x80 ? d : -d); clamp: >= 448 signed, <= 3424; C->flags |= 4;
    case 0x5: volslide(C, (y - x) & 0xff); porta(C, 4 * C->portaspeed); C->flags |= 5;   // (borrow -> 0, see below)
    case 0x6: volslide(C, (y - x) & 0xff); vibrato with depth/speed of the record, clamp >= 448 unsigned; C->flags |= 5;
    case 0xa: volslide(C, x ? -x : y); C->flags |= 1;            // up x, else down y
    case 0xe: ECx: if (y == C->tickcount) { C->vol = 0; C->flags |= 1; }
              EDx: if (y == C->tickcount) C->flags |= 0x0f;
              // E9x: 0x13cf9 = nothing; all others nothing
    }                                               // 7 (tremolo), 8, 9, B, C, D, F: nothing
    if (C->flags & 1) vis(c);
}
volslide(C, a): v = (u8)(C->vol - a); C->vol = (v & 0x80) ? 0 : (v >= 64 ? 63 : v);
porta(C, s):    if (period < base) { period += s; if (period > base) period = base; }
                else if (period > base) { period -= s; if (borrow) period = (3xx: base, 5xy: 0); if (period < base) period = base; }   (u16)
```

`tickcount` is 0 after the row and 1 on the first tick after it, so a test against 0 never succeeds: EC0 does
nothing, and an ED0 note never starts (the row clears its flags, and tick 0 never comes); EDx with x ≥ speed does
not start either. Neither is in the song.

### 9.5 What the song uses (`okeanplay.py --info`, cells in the played orders)

C (volume byte) 4,485 (183 of them above 0x3f: they play as 63); A (volume slide) 414 (A10 and A0F); EB 452; E91 130
and E92 136 (nothing); EC2 130; D 30 (D00 at row 47 of every order but 0); F 3 (F7D, F0C, F06); B 1 (B1D in order
30). 310 notes without an instrument (they keep the channel's instrument and volume). No arpeggio, portamento,
vibrato, offset, pan or pattern-loop effects; no instrument without a note; every period is in the table. 2,494
notes start in the capture (all with a ramp).

Quirks (part of the sound; keep them):

- **E9x (retrigger) is ignored**: both its row and tick handlers do nothing. The song's 266 E91 / E92 cells were
  written for a player that retriggers every 1 or 2 ticks; here those notes play once.
- **EC2** cuts at tick 2 (volume 0 = table entry 0, −54.7 dB, not silence).
- **Volume 0..63**: Cxx, instrument volumes and slides clamp at 63; C40 plays as 63, the same as C3F.
- **Pan**: see 10.4.
- **Vibrato would read the module.** 4xx and 6xy read `mov al, [bx + 0x2f98]` with an address-size prefix
  (`67 8a 87 98 2f`): 16-bit addressing truncates the sine's address 0x12f98 to 0x2f98, so the "sine" is 32 bytes
  of the module (pattern 4, rows 3..4: 00 00 00 00 02 80 1c 01 01 53 3c 07 ...), and the depth is that byte ×
  depth >> 7 instead of a half sine. The song has no vibrato; a port reads the 32 bytes at code32 0x2f98 if it wants
  the player's behaviour for other modules.
- **Instrument numbers** stored by the noteless path are 0-based (as in DOPE's player); this song never takes that
  path with an instrument.
- **9xx and 900** restart the sample (flag 8) even without a note; not in this song.
- **Arpeggio** uses x and y (DOPE's version played base, +x, +x), wraps octaves by 11 semitones and clamps to B-5;
  not in this song.

---------------------------------------------------------------------------------------------------------------

## 10. What is heard

### 10.1 Note starts (the ramp path)

On a tick with note starts (1,302 of the 9,200 player ticks; 1 to 4 new notes per tick: 502, 456, 296 and 48
ticks), with the interrupts at their hardware times (`analysis.py`):

1. voices 0..15 are handled in order. Voices before the first new-note voice get their frequency / volume writes at
   the tick;
2. a new-note voice fades from its current volume to 0x040 at 63 steps per frame (0.70..1.65 ms, median 1.34 ms)
   while the old sample plays on;
3. at the ramp end the voice is stopped, set to the new sample (loop, end, start + offset) and started, and in the
   same microseconds gets its frequency (if changed) and its volume (a jump, no ramp up);
4. the next voices follow: each later new note waits for the ramps before it, and every write for a higher voice
   (slides, cuts) waits as long. The last switch of a tick comes 0..5.42 ms after the tick (median 2.35 ms).

So a new note is heard 0.7..5.4 ms after its tick, with a fade-out of the old note of up to 1.65 ms just before
it. A remake that wants the original's sound keeps the serial ramp path (it costs nothing in a GF1 model driven by
the same writes).

### 10.2 Pitch

`FC = round((14,187,344 / period) / 37)`, two integer divisions (the first truncates, the second rounds). The GF1
with 16 active voices runs 617,400 / 16 = 38,587.5 frames per second and advances FC / 1024 bytes per frame, i.e.
plays FC × 37.683 Hz; the player divides by 37 (a 37,888 Hz card). So every note plays 1.0185 times its period's
PAL frequency: **+31.7 cents**, between +26.1 and +36.2 cents with the rounding of the 19 note / instrument pairs of
the song. Keep the integer FC and the card's rate.

### 10.3 Volume

`voltab[vol × master >> 6]` (0x143ba, 64 u16, 12-bit GF1 log volume << 4) with master 63: vol 63 → index 62 (0xff00,
−0.3 dB), 48 → −2.7 dB, 32 → −6.6 dB, 24 → −9.3 dB, 16 → −13.2 dB, 8 → −20.6 dB, 4 → −25.2 dB, 2 → −36.1 dB, 1 and 0
→ index 0 (0x6e00, −54.7 dB). About 6.5 dB per halving above 8, steeper below: not ProTracker's linear law. Volume
changes are jumps (register 9 written directly), so slides are steps of one entry per tick. Keep the table.

### 10.4 Pan

The load writes `0x10575[v]` to each voice: channels 0..7 = 3, 7, 7, 7, 7, 8, 6, 12 (0 left .. 15 right; 7 is just
left of centre in the GF1's 16 steps). A note sets the record's pan to 7 without the pan flag, and the update after
a ramp (flags 7) compares 7 with the cached 7 and writes nothing; only E8x would write a pan. So the voices keep the
load's pans for the whole song. The module lists the composer's pans in its sample names 13..20 ("1:3", "2:7",
"3:6", "4:8", "5:7", "6:8", "7:6", "8:C"); the player's table differs from that list in channels 3 and 4 (7 and 7
instead of 6 and 8). The capture has no pan write after the load.

### 10.5 The fade at the end (master volume)

The last part's callback (0x4d6b1..0x4d70b, per refresh, core scope): if `[0x12b75]` = 0x1e and the row word
`[0x12b73]` ≥ 0x20: if its brightness `[0x3ceac]` (32) > 0: `[0x3ceac]`−− and, if `[0x12b8c]` > 0,
`[0x12b8c]`−−; otherwise `[0x3ceac]`++ up to 32. Its main loop leaves when `[0x3ceac]` or `[0x12b8c]` is 0 (or on
Esc), and main stops the music. So from the first refresh at or after 30:32 (182.117 s in the capture: player tick
9169, interrupt 9172) the master drops by one per refresh, 63 → 32 by the last tick the capture plays (31
decrements up to 182.719 s) and 31 at the 32nd refresh; then the music stops (interrupt 9203, 30:37).

The master only enters through `update(…, f & 1)`: a channel's level changes when that channel next writes its
volume (a note, Cxx, A, EB, EC), not when the master changes. During the 0.64 s of the fade the active slides and
notes pick it up; a held note without volume changes keeps its level until the stop. The model takes the refresh
times from the 50 Hz mode (frame k at k × 20.0596 ms, the callback at the retrace start + 16.112 ms) and reproduces
the capture's volume writes exactly; in a remake the fade follows the part's callback, whose timing the core scope
gives.

### 10.6 Stops

The stop writes voice control 3 to the 16 voices; their volumes stay, and the GF1 then outputs each voice's parked
sample (constant) until the shutdown resets the card. With one-shots parked on their last byte (+4, 0, −1) and loops
stopped mid-sample, this is a step to a small DC level.

---------------------------------------------------------------------------------------------------------------

## 11. The register stream (`full.gus`) and what in it comes from the capture

Format: `<n> v<voice> r<reg> <value>` (every register write but the DRAM address 0x43 / 0x44; 8-bit writes to a
16-bit register log `latch | byte << 8` with the emulator's latch 0), `<n> p<port> <value>` (ports other than
3x2..3x5, 3x7, 2x8), `<n> irq <t|r> <time>` before each handler run. 70,027 lines: GUS init and load (interrupt 0),
interrupts 1..3 (`r45 0`, `r45 8`), then per timer interrupt `r45 0`, `r45 8`, `r47 c2`, the voices (section 7), and
the stop. Reads (2x6, 0x8f, select 0x09) are not logged.

What the emulator contributes, and how `okeanplay.py` handles it:

- **service times** of the interrupts: replayed from the `irq` lines (or `--ideal`: hardware times). They decide
  nothing written except through the master volume during the fade (which refresh comes before which volume write);
  the model computes the refreshes itself.
- **the select-0x09 read**: the emulator answers 0 for a register it does not know, so `r08` is always 0 and the BX
  bit that goes into bit 8 of the first address-low register of each switch is 0. A GF1 answers a read of a write
  register select with something undefined (the last data byte, or the bus' 0xff; not known). Effects: register 8
  is the upper limit of the ramp and does not matter for a downward ramp; the BX bit moves the first of the switch's
  address writes (the loop start if it changed, else the end if it changed, else the start position) by half a
  sample. `--other-read` sets the value.
- **lost overflows**: the 7 of the capture (section 8.4) come from the emulated `cli` windows. They change no write,
  only which overflow a tick is.
- **the latch**: the low byte of 8-bit writes to registers 3 / 5 / 0x0b is 0 in the log, the latched 3x4 byte on a
  GF1 (section 6).

---------------------------------------------------------------------------------------------------------------

## 12. GF1 model and render (`okeanplay.py --wav`)

The model of `dope/re/models/music/dopeplay.py` (`jizz/remake/src/gf1.cpp`, paper/re/notes-music.md section 7):
positions in 1/512 byte, increment FC >> 1 per frame, 12-bit log volume with ramps (63 steps per frame), pan 0..15
with the linear law, 617,400 / active voices frames per second (16: **38,587.5 Hz**), 8-bit samples linearly
interpolated, stopped voices mixed at their parked position, forward loops (no bidirectional loops here), 8-bit
writes to the address-low registers combined with the latched low byte. Writes of an interrupt apply at its time
(the frames that start before it are rendered first). The render uses the hardware schedule: timer overflows every
19.84 ms from the init with no lost ticks, ramp interrupts at the ramp ends of the card model, and the master fade
at the refreshes of the 50 Hz mode.

The WAV starts at the first player tick (overflow 4, 79.36 ms after the init) and runs 182.53 s, to one tick period
after interrupt 9203 (30:37, as the capture), 38,588 Hz stereo (the header's integer rate for 38,587.5), normalised
to its peak (1.27 of one voice at full scale and gain 1, hard panned). It is not in Git (`*.wav`).

For the remake (`render(ctx, time)` depends only on time): the player state is a function of the tick count, the
tick times follow 8.3, seek = replay the player from the start (9,200 ticks) and the GF1 voices from the last note
starts. The GF1 model must report ramp ends (to run the ramp path at that frame, then the rest of the tick's voice
loop), answer register 0x8f from its IRQ queue, and take its frame rate from register 0x0e (16 voices from the
init). The master volume comes from the last part.

---------------------------------------------------------------------------------------------------------------

## 13. Time bases

| variable | time base |
|---|---|
| GUS init, load, upload | none (start-up; the load runs with interrupts off) |
| GUS timer 2 | overflow every 19.84 ms from the init (count 0xc2 throughout) |
| player state (row, order, volumes, periods) | player ticks = served timer interrupts, 8.3 |
| position `[0x12b75]` / `[0x12b73]` (read by the parts) | changes at the tick that reads the row |
| voice switch for a new note | the ramp end: 0.7..1.65 ms per new note on the voices 0..v, after the tick |
| frequency, pan, volume of voice v | the tick, plus the ramps of the new notes on voices ≤ v |
| master volume `[0x12b8c]` | the last part's retrace callback (one step per refresh from 30:32) |
| GF1 output | 38,587.5 frames per second |
| lost ticks | at the part starts (PIT calibration with interrupts off): phase dependent, 0..2 each |
| music start | the first timer overflow after the first part's set-up has called the start (overflow 4 in the capture) |
| music stop | main after the last part returns (interrupt 9203, 30:37 in the capture) |

---------------------------------------------------------------------------------------------------------------

## 14. Static data for the remake (read from korso2.bin = file offset 0xec0 + code32; do not copy)

| what | code32 | format |
|---|---|---|
| module | 0xae0..0x100c4 | ProTracker MOD, `8CHN` (section 4) |
| base-octave periods (note → period) | 0x10390 | 12 u16 |
| C-2 rates | 0x103a8 | 16 u16 |
| ProTracker periods (period → note) | 0x103cc | 36 u16 |
| pan per voice | 0x10575 | 32 u8 |
| vibrato bytes as read (unused by the song) | 0x2f98 | 32 u8 |
| arpeggio clamp flag | 0x12b82 | u8 (1) |
| master volume | 0x12b8c | u32 (63) |
| initial timer count | 0x1438a | u8 (0xc2) |
| IRQ latches | 0x143a6 | 16 u8 |
| volume table | 0x143ba | 64 u16 |
| constants in code | 0x13f91 0xd87b50, 0x13585 / 0x13d22 / 0x13e0d / 0x13e79 0x20ab0, 0x145b6 1000000 (0xf4240), 0x145ab 5, 0x145bf 0x140, 0x14689 100, 0x14690 0xe74 (3700), 0x14621 4 (ramp floor), 0x14639 0x60, 0x149da 16 voices | |

The remake needs: the load (4.2) for the tracks, instrument records and DRAM image; the tick (9); the voice loop
and ramp path of 6 driven by a GF1 model with ramp-end interrupts (12); the tick schedule of 8.3; the master volume
from the last part (10.5).

---------------------------------------------------------------------------------------------------------------

## 15. Compared with DOPE's player

Same (code32 here / DOPE loader): the channel record layout (every field from +0x00 to +0x3d, 2.3), the 5-byte track
events and the load's pattern conversion (Cxx → volume byte), the instrument fields +0x31..+0x44 and the loop-end
formula, the tick 0x1343c / 0x22c4 with its four jump tables (row, row Exy, tick, tick Exy), the note period
`(period × 0x20ab0 >> octave) / c2spd` with 0xd87b50, tone portamento, the slides, Dxx / Bxx / E6x / EEx, the
`shrd bx, si, 7` address writes with the stray BX bit, the ramp-down-then-switch scheme with the ramp IRQ, the GUS
init sequence (each voice twice, latches, DRAM probe, timer 2), the PIO upload, the early EOI with the self-modified
slave EOI.

Different:

| | this player | DOPE's |
|---|---|---|
| channels | 4, `6CHN`, `8CHN` | `xCHN`, `xyCH` up to 32 |
| note matching | 36 periods (3 octaves) | 96 periods (8 octaves) |
| voices | 16 (38,587.5 Hz) from the init | 32 during the init, then max(channels, 14) |
| volume | 0..63, table of 64 | 0..64, table of 65 |
| new note | flags 0x0c, pan = 7 (never written: load's pan table stays) | flags 0x0f, pan 6 written |
| one-shot end | start + length − 1; samples uploaded unchanged | start + length; first bytes zeroed |
| bidirectional loops | none | "ping" names |
| note-off | reads select 0x09 (wrong), ramps to 0x040, **one voice at a time, waiting inside the timer interrupt** | reads 0x89, ramps all to 0x400 at once, the last ramp IRQ updates all voices |
| ramp IRQ | one voice per interrupt, sets a flag | all pending voices in a loop |
| frequency | round(Hz / 37): +31.7 cents | Hz × 1024 / 22050 truncated: 0.2..7.3 cents flat |
| volume write | on the volume flag only | also when the master changed |
| timer count | 0x100 − 1e6 / (2T/5) / 320, written after the tick | −(31250 / T >> 2), written at the next interrupt |
| arpeggio | x and y, divided by c2spd, clamp `[0x12b82]` | base, +x, +x |
| vibrato table | 32 module bytes (16-bit address of 0x12f98) | 64 bytes from 0x22a0 incl. code (4xx) |
| 8xy / E8x | nothing / pan = x | pan = x / pan 0 |
| E9x | nothing | retrigger |
| 9xx | 900 restarts too | only xx ≠ 0 |
| F00 | nothing | playing = 0 |
| extras | 0x14284 volume-per-note table, `[0x14381]` ms counter | — |
| detection | `ULTRASND` only | `ULTRASND` or a menu |

---------------------------------------------------------------------------------------------------------------

## 16. What the original approximates (keep for the sound; options for "sharper")

- 8-bit samples, linear interpolation at 38,587.5 frames per second, no band limiting. A sharper option runs the same
  voice model at the output rate with increments and ramp steps scaled by 38,587.5 / rate (pitch, envelopes and
  timing unchanged) — a remake choice, not the original.
- Pitch: integer FC, +31.7 cents against PAL. Keep it: it is how the intro sounds on a GUS.
- Volume: the 64-entry table, jumps on every change, 63-step note-off ramps. Keep.
- Timing: 19.84 ms ticks (320 µs steps). Keep. Lost ticks at the part starts are a property of the machine; the
  remake may play without them (the render does) as long as the parts follow the music position.
- The quirks of 9.5 change what is played; they are the original's sound on any machine.

---------------------------------------------------------------------------------------------------------------

## 17. Open points / guesses

- What a GF1 answers for a read of register select 0x09 (section 11): unknown; one half-sample address bit depends
  on it.
- Whether a GF1 raises the ramp IRQ for a downward ramp that starts below its target, and how fast (section 7,
  first hazard): guessed "at the next volume update of the voice", which is safe.
- Timer semantics (count reloaded at overflow, the status bit as a single flag) follow DOSBox, run_dope.py and
  run_okean.py; the capture's tick times agree with them, real hardware was not checked.
- GF1 details shared with the other notes: pan law, clipping of the voice sum, the volume curve between mantissa
  steps, loop wrap at `pos > end` vs `>=`, the low byte latched for 8-bit writes to 16-bit registers.
- Effects not used by the song (arpeggio, portamento, vibrato, offset, E1/E2/E5/E6/E8/EA/ED/EE) are ported from the
  code but not verified against a capture.
