# Reve — music (scope "music": LCA's MOD-Player V3.03, GUS path)

Addresses are linear addresses of the running demo (image at 0x20427c, `unpacked/reve_img.bin`). "Tick k" = the k-th
run of the player's GUS IRQ handler 0x26eecc (k = 0 is the first). `full.gus` stamps every write with the number of
GUS timer interrupts counted so far, so the writes of tick k carry k + 1 and the start-up writes 0; `full.ticks`
column 4 is that count at each record. "Music time" = seconds after 0x26ec3e started the GUS timers (t_s).

The player is V3.02 of STARS (`stars/re/notes-music.md`, whose structure this file follows) with the changes of
section 14. They were found by aligning the two binaries instruction by instruction (`models/music/vs302.py`,
`addrmap.py`, `tables.py`): everything not listed there is the same code with relocated addresses (player globals
STARS + 0x243c04, GUS data and tables STARS + 0x244484).

Everything below was checked, not only read: `models/music/lcaplay.py` (a routine-by-routine port, see its README)
builds from `unpacked/files/ZIC/PULSE.MOD` and `reve_img.bin` alone

- the GUS DRAM: identical to `full.dram` (all 1,048,576 bytes);
- the register-write log from the card detection to main's stop: 111,241 lines, byte-identical to `full.gus`
  (including where main stops the music, section 5.5);
- the GUS timer interrupts before each record's retrace: identical in all 17,729 records of `full.ticks` (the clock
  of section 5.4, the timer switches included);
- the music position at each record: identical in 17,728 records; record 137 shows the position one tick earlier,
  because the emulator held that tick's interrupt (section 5.3);
- the C++ port (`reve/remake/src/lcaplay.cpp`, `gf1.cpp`, `music.cpp`, checked by `reve/remake/tools/test_music.cpp`)
  gives the same log, DRAM, interrupt counts and positions, the 30 script switch refreshes of the capture, and its
  mix at the card's rate is within 1 LSB of `lcaplay.py --wav` in every sample.

There is no audio capture to compare the sound itself with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- **Player**: "LCA's MOD-Player V3.03" (strings 0x25f65c `LCA's Player, (C) Cedric BERMOND (LCA) 1993-95Version
  3.03`, 0x26e14a), GUS and SB paths; the GUS path is used. **8 channels** (`8CHN`), **two GF1 voices per channel**
  (16 active voices → the GF1 runs at 617,400 / 16 = **38,587.5 frames per second**): every note starts on the other
  voice of the channel's pair while the old one is ramped down.
- **Module** `ZIC\PULSE.MOD` (454,614 bytes): a standard 31-sample MOD ("PULSE", signature `8CHN` at 0x438, 128-entry
  order table), 65 orders, 48 patterns, 355,226 bytes of 8-bit samples (21 used samples). Converted by the
  standard-MOD converter 0x26e16c into 6-byte events; samples uploaded by DMA, unchanged, to DRAM 0..0x574df, in
  blocks split at the 64 KB pages of the module's physical address.
- **All timing is the GUS's**: the tick is a GUS timer IRQ. Tempo T → `n = 1000000 / (32 T)` timer-1 steps of
  80 µs, truncated; n ≥ 256 → timer 2 with `n >> 2` steps of 320 µs. The song uses **F50** (tempo 80: timer 2, count
  0x9f, **31.04 ms**, nominal 31.25 ms) and **F96** (tempo 150: timer 1, count 0x30, **16.64 ms**, nominal 16.67 ms),
  and switches between the two timers three times; both run free from the start, so the first tick after a switch
  comes at the other timer's next overflow (5.2). Speed 6 / 3 / 15 / 8; the song ends with F00 at 40:3e (the player
  stops; the voices hold).
- **Position** (0x2602eb, AL = order [0x26042d], AH = row [0x26042c]; the demo's timer interrupt swaps it to
  order << 8 | row) = the *next* row to be read.
- **Clock for the remake**: the timers start at the retrace of **refresh 29** of the capture (startRefresh =
  29.0000000059); tick k is at refresh 29 + 70.0863 × tickTime(k); the timer interrupt at refresh r sees the
  position after every tick before that retrace. The last script entry ends at refresh 17518 (position 40:3f) and
  main stops the music **210 refreshes later, at refresh 17728** (music time 252.5315 s), after 13,535 ticks.
- **V3.03 vs V3.02** (section 14): the standard-MOD converter is the one used; sample records of 0x1c bytes; up to
  extra sound-effect channels with their own API (unused by Reve); a new tick effect EFx (restart the note); a new
  tick-counter test; the song wraps to its restart order at the end instead of stopping; a louder volume table; the SB
  probe only without a GUS; NOSOUND support in the detection; DMA straight from extended memory (bounce buffer only
  above 16 MB, so the blocks split at 64 KB physical pages).
- Effects used by the song: 1xx (215), 2xx (235), 3xx (122), 4xy (132), 5xy (16), 9xx (456), Axy (2,671), Cxx
  (3,866), D00 (6), E1x (211), E2x (134), EAx (1), EBx (2), F03 / F06 / F08 / F0F / F00 (speed), F50 / F96 (tempo).
  All of them are exercised by the capture's register log. No arpeggio, no B, 6, 7, E6, E9, EC, ED, EE, EF.

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when (capture) | log stamp | what |
|---|---|---|
| main 0x204296, frame 12 | 0 | command line: `NOSOUND` / `MIX:nn` / `MONO` / `8BIT` → [0x26e10c] = 45000 (the default MIX rate), AL = 3; 0x25f698 detect with BL = 0 (section 2) |
| frames 12–28 | 0 | 0x2601dc: allocate the pattern buffer (0x802c8 bytes), load `ZIC\PULSE.MOD` (service 0209h → 0x345e30), 0x26e16c convert and upload (27 DMA blocks), free the module |
| main 0x204770, retrace of refresh 29 | 0 | 0x204458 installs the timer interrupt (right after a retrace), 0x2602a1 → 0x26ec3e start: GF1 reset, timers started (t_s), IRQ 7 = 0x26eecc |
| t_s + 20.48 ms (refresh 30.44) | 1 | tick 0 (timer 1): reads row 00:00 (F50: timer 2; F06) — record 31 is the first with position 00:01 |
| t_s + 81.92 ms | 2 | tick 1: timer 2's first overflow, then every 31.04 ms |
| refresh 17518 | 13355 | tick 13354 read 40:3e (F00) at 249.529 s: position 40:3f = the last script entry's end; main leaves the script, draws the title (0x20538d) |
| refresh 17728 (252.5315 s) | 13535 | main: 0x2602c6 → 0x26ecc4 stop, after tick 13534 (the player stopped at 40:3f since tick 13355) |

---------------------------------------------------------------------------------------------------------------

## 2. Detection 0x25f698 (AL = options, BL = extra channels)

```c
int detect(int opts /*al*/, int extra /*bl*/)              // returns EAX = [0x260406]
{
    [0x260409] = opts & 3; [0x26040e] = extra;             // 0x260340 (V3.03): bit 0 16-bit, bit 1 stereo (SB only)
    [0x260404] = ds;                                       // the IRQ handlers load it
    service 0401h (EBX = 0x26e149, "\1LCA's MOD-Player V3.03\1\1");   // 0x271b9c: the player's name to the extender
    if (![0x26e120]) {                                     // V3.03: only once
        0x271bc0: service 0005h -> EBX = a 64 KB DMA buffer, DL = its address >> 16, ECX = 0x1000000
        [0x26e120] = ebx; [0x26e12e] = dl; [0x260424] = ecx;   // V3.03: the DMA limit is 16 MB (V3.02: 640 KB)
        if (ebx + 0xffff > ecx) goto none;
        [0x26e128] = ebx + 0x8000;                         // (SB)
    }
    memset([0x26e120], 0x80, 0x10000);
    if (![0x26e10c]) goto none;                            // V3.03: NOSOUND sets the MIX rate to 0
    [0x26e10a] = [0x26e10c];
    gus_detect();                                          // 0x25f75b: [0x260408] = 0 if found
    if (![0x260408]) return [0x260406] = 0x20;             // GUS; V3.03: no SB probe then
    sb_detect();                                           // 0x25fbc6: [0x260407] = 0 if found, type [0x26e110]
    if (![0x260407]) return [0x260406] = [0x26e110];       // SB 1..4
none: [0x260406] = 0; CF = 1;                              // the timer interrupt then runs 0x271ad3 (section 8)
}
```

main passes AL = 3 (16-bit and stereo, cleared by `8BIT` / `MONO`) and BL = 0. [0x26e10c] = `MIX:nn` clamped to
8..45, × 1000 (default 45 → 45000), 0 with `NOSOUND`; only the SB path uses the rate (0x25fbc6 limits it to 22,050 /
44,100 / 45,000 Hz by SB type).

**gus_detect 0x25f75b** = V3.02's 0x1bb0a: [0x260408] = 1; 0x25f793 reads `ULTRASND=` (string 0x26e014; 0x25f7f3
searches the first 0x400 bytes of the environment, address from 0x271bf8): numbers parsed by 0x25f7e0
(`v = v * base + (c − '0')` while c − '0' ≤ 9 unsigned; the terminator is consumed): port in base 16 → [0x26dfec],
then base 10: DMA [0x26e006], second DMA [0x26e008], IRQ [0x26e00a], second IRQ [0x26e00c]. Not found → CF (no GUS).
Found → [0x260408] = 0, 0x25f8d7 (ports and GF1 init), 0x25f819 `ULTRA16=` (string 0x26e01e; absent in the capture,
else 0x25f843 writes the codec). No hardware probe: the player trusts the environment.

Port variables (0x25f8d7, base 0x240): [0x26dfee] 0x340 (its reads are the delays: 6 `in` per delay), [0x26dff0]
0x342 voice select, [0x26dff2] 0x343 register select, [0x26dff4] 0x344 data low (16-bit writes), [0x26dff6] 0x345
data high (8-bit writes), [0x26dff8] 0x246 IRQ status, [0x26dffa] 0x247, [0x26dffc] 0x248 / [0x26dffe] 0x249 AdLib
(timer) control, [0x26e000] 0x24d, [0x26e002] 0x24e.

GF1 init 0x25f8d7 (`w8` = register → 3X3, byte → 3X5; `w16` = word → 3X4), as V3.02:

```c
w8(0x4c, 0); delay; w8(0x4c, 1); delay;                    // reset, run
out(0x340, 3); delay; out(0x340, 0);                       // MIDI reset
w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);                     // DMA, timer, sampling control off
w8(0x0e, ([0x26e012] - 1) | 0xc0);                         // [0x26e012] = 14 in the image: 0xcd
in(2X6); r8(0x41); r8(0x49); r8(0x8f);                     // acknowledge
for (v = 31; v >= 0; v--) { select(v); w8(0x00, 3); w8(0x0d, 3); }          // voices and ramps stopped
r8(0x41); r8(0x49); r8(0x8f); w8(0x4c, 7);                 // run, DAC on, GF1 IRQs on
for (v = 31; v >= 0; v--) { select(v); w16(2,0); w16(3,0); w16(4,0); w16(5,0); w8(6, 0x3f);
                            w16(9, 0); w16(0xa, 0); w16(0xb, 0); w8(0xc, 7); }
// IRQ / DMA latches (tables 0x26e027: IRQ 2,3,5,7,11,12,15 -> 1,3,2,4,5,6,7; 0x26e037: DMA 1,3,5,6,7 -> 1,2,3,4,5)
irq = T_irq[IRQ] | (IRQ2 == IRQ ? 0x40 : T_irq[IRQ2] << 3);              // 0x44
dma = T_dma[DMA] | (DMA2 == DMA ? 0x40 : T_dma[DMA2] << 3);              // 0x42
out(2XF, 5); out(2X0, 0xb); out(2XB, 0); out(2XF, 0);
out(2X0, 0xb); out(2XB, dma | 0x80); out(2X0, 0x4b); out(2XB, irq);
out(2X0, 0xb); out(2XB, dma);        out(2X0, 0x4b); out(2XB, irq);
select(0); out(2X0, 0xb); select(0); out(2X0, 8);                         // mix control
```

---------------------------------------------------------------------------------------------------------------

## 3. The module and the conversion 0x26e16c

### 3.1 Load 0x2601dc (main, ESI = "ZIC\PULSE.MOD")

```c
pat = alloc(0x802c8);                                     // 0x271bec (service 0101h): the pattern buffer
mod = load(ESI);                                          // 0x271bd4 (service 0209h, EDI = 0): 0x345e30 in the run
if ([0x260406] == 0x20) convert_gus(EAX = 0, ESI = mod, EDI = pat);   // 0x26e16c
else                    convert_sb(EAX = 0, ESI = mod, EDI = pat);    // 0x26fd7c (also without a card)
free(mod);                                                // 0x271be0 (service 0102h)
```

(V3.02's load entry took the module from the caller. 0x260237 / 0x260263 are entries for a module already in memory,
standard MOD / the 256-order format of 0x26e518; no caller.)

### 3.2 Format (as 0x26e16c reads it)

```
0x000  20 bytes title "PULSE"
0x014  31 x 30 bytes sample headers: 22 name, u16be length/2, u8 finetune (0..15, 8..15 = -8..-1), u8 volume,
       u16be loop start/2, u16be loop length/2    (sample 1 "SHAD/PULSE & LIVE!", sample 2 "18/12/1995")
0x3b6  u8 song length (65), u8 restart (127; > length -> 0)
0x3b8  128 bytes order table (patterns = max entry + 1 = 48)
0x438  signature "8CHN"
0x43c  patterns: 48 x 64 rows x 8 channels x 4 bytes (Amiga MOD cells), then the samples (8-bit signed)
```

Channels from the signature (0x26e188): two digits at 0x438 (either order) → [0x26040b]; `M.K.` → 4; otherwise byte
0x43b `.`/`4` → 4, `6` → 6, `8` → 8 (here: "8C" → al = '8', ah = 'C' is no digit → 0 → **8**), anything else → a
15-sample Soundtracker module (sanity checks; CF). More than 7 channels → GF1 voices [0x26e012] = 2 × channels.

### 3.3 Conversion

```c
[0x260410] = EDI (pattern buffer); [0x26040a] = 31 samples; [0x26e012] = 14 (then 16); [0x26040c] = 64 (max volume)
for (n = 1; n <= 31; n++) {                                // records of 0x1c bytes at 0x26c3cc + 0x1c (n - 1)
    S.len  = 2 * be16(h + 0x16);                            // +0x00                (pointer table 0x26ce48 + 4 n)
    S.ft   = h[0x18];  S.vol = h[0x19];                    // +0x04, +0x05 (volume not clamped here)
    S.ls   = 2 * be16(h + 0x1a);                            // +0x08
    rep    = 2 * be16(h + 0x1c);
    S.flag = rep > 2 ? 8 : 0;                              // +0x06: GF1 voice control (bit 3 = loop)
    S.le   = min(S.ls + rep, S.len);                       // +0x0c   (+0x18 u16: V3.03, effect samples only)
}
songlen [0x26042e] = 65, restart [0x26042f] = 0, orders -> 0x26cfcc (128 bytes), npat [0x260430] = max + 1 = 48
for each of npat * 64 * channels cells (b0 b1 b2 b3):      // 0x26e31b: 6-byte events
    ev[0] = (b0 & 0xf0) | (b2 >> 4);                       // instrument
    ev[1] untouched;                                       // (unused)
    *(u16 *)(ev + 2) = (b0 & 0x0f) << 8 | b1;              // period
    *(u16 *)(ev + 4) = (b2 & 0x0f) << 8 | b3;              // effect << 8 | parameter
[0x26e508] = end of the events (patched immediate, returned in EAX)
upload (3.4); [0x26fd65] = [0x26fd69] = DRAM end (V3.03: where effect samples go)
[0x26040f] = [0x26040e] + [0x26040b] = 8;  [0x26e012] = [0x26040f] > 7 ? 2 * [0x26040f] : 14 = 16    // V3.03
FC table (3.5)
memset(0x26d4cc, 0, 32 * 0x34);  for (c = 0; c < 32; c++) C[c].voice = 2 * c;    // channel records (6.1)
[0x260414] = stride = 6 * channels = 48 bytes per row; pattern p at buffer + p * 64 * 48 (pointers 0x26d0cc[128])
order = row = 0; counter = 1; speed = 6; loopcount = 0;     // [0x26042d] [0x26042c] [0x260431] [0x260432] [0x260435]
tempo(125);                                                 // 0x26e4aa, stored only: timer 1, count 6 (5.1)
ptr = pattern[orders[0]];                                   // [0x260418]: the next row's events
```

### 3.4 Sample upload (0x26e36f..0x26e3fc) and DRAM layout

```c
out(2X0, 0xb);                                              // mix control: output off during the upload
install IRQ 7 handler 0x26ec04 (0x26e91d); [0x26e00e] = 0   // DMA busy flag
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
        if (!S.flag) S.le = --S.len;                        // 0x26e3cc: one-shots end on their last byte
    }
    S.lerel = S.le;                                         // +0x14: the 9xx limit
    S.len += S.dram; S.le += S.dram; S.ls += S.dram;        // DRAM addresses from now on
    edi = (edi + 31) & ~31;
}
restore IRQ 7 (0x26e990)
```

upload 0x26e9bb (ESI source, EDI DRAM address, ECX bytes):

```c
if (n <= 20) { pio(src, edi, n); return; }                 // 0x26ea91: per byte reg 0x43 = address low word,
                                                            //   reg 0x44 = bits 16..23, byte -> 3X7 (not logged)
for (;;) {
    if (src + n >= [0x260424] /* 16 MB */) { copy n bytes (8-byte steps) to the bounce buffer; src = bounce; }
    k = min(n, 0x10000 - (src & 0xffff));                  // up to the end of the source's 64 KB page
    if (!k) break;
    dma(src, (edi + 31) >> 5 << 1, k);                      // 0x26eac8; afterwards edi = DRAM start of the block + k
    src += k; n -= k;
    if (!n) break;
    if (edi & 31) { j = min(32 - (edi & 31), n); pio(src, edi, j); src += j; edi += j; n -= j; }   // realign
}
```

dma 0x26eac8: waits for [0x26e00e] = 0, sets it, programs the 8237 (8-bit channels: mask, clear flip-flop, mode
0x48 + channel = single, memory → card, address, count − 1, page from table 0x26e141, unmask; 16-bit channels on
0xd4.. with halved values) and the GF1: `r8(0x41)`, `w16(0x42, ((edi + 31) >> 5) * 2)` (DRAM address / 16, rounded
up to 32 bytes; the GF1's 16-bit form for 16-bit channels), `w8(0x41, 0x21)` (start, card ← memory, 8-bit, IRQ at
the end; | 4 for 16-bit channels). The IRQ handler 0x26ec04 clears [0x26e00e] on the DMA terminal count. No byte is
changed: DRAM = the module's bytes, 8-bit signed as the GF1 plays them.

The module lies in extended memory below 16 MB, so the DMA reads it in place (V3.02 copied everything above 640 KB
to its bounce buffer, which never split a sample): the blocks split at the 64 KB pages of its *physical address*
(0x345e30 in the headless run; samples start at module offset 0x1843c = physical 0x35e26c). That changes only the
log (`g r42` / `g r41` pairs and PIO), not the DRAM: 27 DMA blocks for 21 samples (the tails all by PIO). On another
machine the module would load elsewhere and the blocks would split elsewhere.

Result (`lcaplay.py --info`; addresses hex; "end" = the GF1 end register: the loop end for looped samples, the last
byte for one-shots; all samples volume 64):

| smp | bytes | DRAM start | loop start | end | loop | ft | smp | bytes | DRAM start | end | ft |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 27840 | 0 | 3b22 | 6cbe | yes | 0 | 11 | 21552 | 40000 | 4542f | −6 |
| 2 | 54086 | 6ce0 | dd0c | 13ed0 | yes | 0 | 12 | 3358 | 45440 | 4615d | 0 |
| 3 | 20518 | 14040 | 152fa | 19062 | yes | 0 | 13 | 3462 | 46160 | 46ee5 | 0 |
| 4 | 17778 | 19080 | – | 1d5f1 | | 0 | 14 | 5310 | 46f00 | 483bd | 0 |
| 5 | 11146 | 1d600 | – | 20189 | | 0 | 15 | 46986 | 483c0 | 53b49 | 0 |
| 6 | 65514 | 201a0 | 20234 | 2ff16 | yes | 0 | 16 | 3228 | 53b60 | 547fb | 0 |
| 7 | 25626 | 301a0 | 301a0 | 365ba | yes | 0 | 17 | 3776 | 54800 | 556bf | 0 |
| 8 | 12648 | 365e0 | – | 39747 | | 0 | 20 | 2156 | 556e0 | 55f4b | 0 |
| 9 | 14564 | 39760 | – | 3d043 | | 0 | 21 | 1972 | 55f60 | 56713 | 0 |
| 10 | 10216 | 3d060 | 3ee2e | 3f846 | yes | 0 | 22 | 1698 | 56720 | 56dc1 | 0 |
| | | | | | | | 23 | 1792 | 56de0 | 574df | 0 |

Samples 4, 5, 8, 9 and 12..23 are one-shots; 18, 19, 24..31 are empty. Sample 11 was moved to the 256 KB bank at
0x40000 (gap 0x3f860..0x3ffff). A remake reads the samples from the module (offset 0x43c + 48 × 2048 = 99,388
onwards, in sample order) and lays them out with these rules.

### 3.5 Period → GF1 frequency table 0x26e8ba (dwords at 0x268544, index = Amiga period)

```c
D = u16 [0x26e0e2 + 2 * (voices - 14)];       // 617400 / voices, truncated: 16 voices -> 38587 (patched into 0x26e8fc)
memset(T, 0, 1000 * 4);                       // indices 0..999
for (p = 55; p <= 4000; p++) {                // 0x26e8e6: ebp = 55, ecx = 3946
    q = (0xda7a69e2 / p + D) / D;             // u32 divisions; 0xda7a69e2 (immediate at 0x26e8f3) = 3,579,545.5 * 1024
    T[p] = q & ~1;                            // (`add edx, edx; adc eax, 0` never carries)
}
```

FC = T[period] is written to reg 1; the GF1 plays (FC >> 1) / 512 bytes per frame = FC / 1024 × 38,587.5 bytes per
second: period 214 → FC 444 → 16,731 Hz (ideal 16,727 Hz); ±8 cents over the Amiga range, as in V3.02 (the FC
halves with the frame rate doubled). Keep the integer FC.

---------------------------------------------------------------------------------------------------------------

## 4. Start, GF1 reset, stop (identical to V3.02)

```c
void start(void)                               // 0x26ec3e (via 0x2602a1 when [0x260406] = 0x20)
{
    gf1_reset();
    cli; out(2X8, 4); out(2X9, 0xe0);          // AdLib timer control: reset IRQ flags, mask both timers
    in(2X6); install IRQ 7 = 0x26eecc (0x26e91d: saves the old vector, unmasks IRQ 7 in the PIC)
    w8(0x4c, 7);
    out(2X8, 4); out(2X9, 3);                  // START timer 1 and timer 2  (= t_s)
    w8(0x45, 0); w8(0x46, 6); w8(0x45, 4);     // timer 1 count 6 (20 ms), timer-1 IRQ only
    out(2X0, 9); sti;                          // mix control: line in off, output on, latches on
}
void gf1_reset(void)                           // 0x26ed0e
{
    w8(0x4c, 0); delay; w8(0x4c, 0); w8(0x4c, 1);
    w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);
    w8(0x0e, (voices - 1) | 0xc0);             // 0xcf: 16 voices -> 38,587.5 Hz
    in(2X6); r8(0x41); r8(0x49); r8(0x8f);
    for (v = 0; v < voices; v++) {
        select(v);
        w8(0x00, 3) x2; w8(0x06, 0x3f) x2;     // stopped; ramp rate 63 per frame (never changed later)
        w16(0x09, voltab[0]);                  // 0x2000: GF1 volume 0x200 = -84 dB
        w8(0x0d, 3) x2; w8(0x0c, pan[v]) x2;   // pan table 0x26e03f; "x2" = written, delay, written again
    }
    r8(0x41); r8(0x49); r8(0x8f); w8(0x4c, 7);
}
void stop(void)                                // 0x26ecc4 (via 0x2602c6)
{
    out(2X0, 0xb); delay; out(2X0, 0xb);       // output off
    out(2X8, 4); out(2X9, 0xe0); w8(0x45, 0);  // timers masked, IRQs off
    gf1_reset(); restore IRQ 7 (0x26e990); sti;
}
```

Pan (0x26e03f, voice 0..31; 0 = left, 15 = right): 3 3 12 12 12 12 3 3 3 3 12 12 12 12 3 3, then 7 × 16. Channel c
plays on voices 2c / 2c + 1: channels 0..7 → 3 12 12 3 3 12 12 3 (L R R L L R R L, not hard panned).

---------------------------------------------------------------------------------------------------------------

## 5. Timers and tempo: how the music is clocked

### 5.1 Tempo → timer (0x26e4aa at conversion, 0x26f9c8 for Fxx ≥ 0x20)

```c
void tempo(int T)                              // T = BPM, 0x20..0xff
{
    n = 1000000 / (T * 32);                    // u32 div: 31250 / T timer-1 steps of 80 us, truncated
    if (n >= 256) { n >>= 2; reg = 0x47; ctl = 8; }        // timer 2: 320 us steps, truncated again
    else          {          reg = 0x46; ctl = 4; }        // timer 1
    count = -n & 0xff;                         // [0x26e00f] = reg, [0x26e010] = ctl, [0x26e011] = count
}
// Fxx >= 0x20 then writes: w8(0x45, 0); w8(reg, count); w8(0x45, ctl);
```

The song: **F50** (80 BPM: n = 390 → 97 → count **0x9f**, timer 2, **31.04 ms**; nominal 31.25 ms, 0.67 % fast) at
00:00, 02:00 and 30:00; **F96** (150 BPM: n = 208 → count **0x30**, timer 1, **16.64 ms**; nominal 16.667 ms, 0.16 %
fast) at 04:00 and in 29 later rows. The capture writes reg 0x46 31 times (6 at the start, then 0x30) and reg 0x47
three times (0x9f).

### 5.2 Tick times

The handler starts with `w8(0x45, 0); w8(0x45, [0x26e010])` (acknowledge, re-enable the timer in use). The GUS timers
(AdLib-compatible) count from the start (2X9 = 3 at t_s), **both of them, whether their IRQ is enabled or not**, and
at each overflow reload the count register as it is then; an overflow raises the IRQ (= a tick) when reg 0x45 enables
that timer at that moment. That is how run_reve.py and DOSBox model them; the capture's interrupt counts fit it in
every record. So:

- tick 0 = timer 1's first overflow, (256 − 0) × 80 µs = **20.48 ms** (its count register was 0 when the timers
  started; 0x46 = 6 is written just after). Tick 0 reads 00:00: F50 → timer 2 (0x45 = 8, 0x47 = 0x9f).
- tick 1 = timer 2's first overflow; it started at t_s with count 0: **81.92 ms**; then every 31.04 ms.
- tick 1248 (38.78880 s) reads 04:00: F96 → timer 1 (0x46 = 0x30, 0x45 = 4). Timer 1 has kept running with count 6
  (20 ms) since 20.48 ms: tick 1249 is its next overflow, **38.80048 s** (11.68 ms later), which reloads 0x30: from
  then on every 16.64 ms.
- tick 9720 (179.75792 s) reads 30:00: F50 → timer 2 again: tick 9721 at its next overflow, **179.77248 s**
  (14.56 ms later), then every 31.04 ms.
- tick 10368 (199.85536 s) reads 32:00: F96 → timer 1: tick 10369 at **199.85904 s** (3.68 ms later), then every
  16.64 ms to the end.

Periods in the capture: 11,636 × 16.64 ms, 1,894 × 31.04 ms and the four above. If the other timer overflows at the
same instant as the enabled one, it does so before the handler can change reg 0x45 (not the case in this song).
`lcaplay.py --times` lists every tick; the C++ port gets the same times from `gus::Timers`.

Rows: speed 6 × 31.04 = 186.24 ms (5.37 rows/s) in orders 0..3 and 0x30..0x31; speed 3 × 16.64 = 49.92 ms
(20.03 rows/s) elsewhere; 40:30 F0F (249.6 ms rows), 40:36 F08 (133.1 ms rows), 40:3e F00.

### 5.3 Position

0x2602eb: `AL = cs:[0x26042d]` (order), `AH = cs:[0x26042c]` (row); the demo's timer interrupt (0x204565) does
`and eax, 0xffff; xchg ah, al` → **order << 8 | row**, compares it with the entry's end position [0x205ea0] and sets
[0x205e9a] when ≥. The position is the next row to be read: the tick that reads row r of order o sets o:r+1
(o+1:00 after row 0x3f or a D00).

The demo's timer interrupt fires just before each retrace, waits for it, sends EOI, `sti`, and then reads the
position. A GUS tick whose overflow comes while it waits is held by the PIC (IRQ 7 below IRQ 0) and on hardware
runs right after the `sti` (one instruction later), before 0x2602eb: **the position at the retrace of refresh r is
the position after every tick whose overflow comes before that retrace**. Soundtrack::positionAt implements this.
The emulator delivers interrupts only between its slices, so in the capture such a held tick runs after the
position is read: record 137 shows 00:08 where hardware would read 00:09 (tick 48 overflowed 157 µs before the
retrace, while the timer interrupt was still waiting: its PIT count settles from 0x40d8 for about 420 refreshes after
the install, so in the first 6 s it fires up to 0.36 ms early). It is the only such record of the 17,729 and none of
the script switches is affected: all 30 entries end at the refresh where Soundtrack::refreshOfPosition puts them.

The script's end positions, the tick that reaches each, its music time and the refresh at which the timer interrupt
sees it (= the capture's first record with that position; `test_music` checks all 30):

| entry | end | tick | s | refresh | entry | end | tick | s | refresh |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 01:00 | 282 | 8.80416 | 647 | 15 | 2c:00 | 8925 | 166.52912 | 11701 |
| 1 | 02:00 | 594 | 18.48864 | 1325 | 16 | 2d:00 | 9117 | 169.72400 | 11925 |
| 2 | 03:00 | 882 | 27.42816 | 1952 | 17 | 30:00 | 9714 | 179.65808 | 12621 |
| 3 | 03:20 | 1074 | 33.38784 | 2370 | 18 | 31:00 | 10002 | 188.49472 | 13240 |
| 4 | 04:00 | 1242 | 38.60256 | 2735 | 19 | 32:00 | 10362 | 199.66912 | 14024 |
| 5 | 05:30 | 1581 | 44.32496 | 3136 | 20 | 33:30 | 10701 | 205.38352 | 14424 |
| 6 | 06:00 | 1629 | 45.12368 | 3192 | 21 | 34:00 | 10749 | 206.18224 | 14480 |
| 7 | 0a:00 | 2397 | 57.90320 | 4088 | 22 | 38:00 | 11517 | 218.96176 | 15376 |
| 8 | 14:00 | 4317 | 89.85200 | 6327 | 23 | 3a:00 | 11901 | 225.35152 | 15824 |
| 9 | 16:00 | 4701 | 96.24176 | 6775 | 24 | 3b:00 | 12093 | 228.54640 | 16047 |
| 10 | 18:00 | 5085 | 102.63152 | 7223 | 25 | 3c:00 | 12285 | 231.74128 | 16271 |
| 11 | 1c:00 | 5853 | 115.41104 | 8118 | 26 | 3e:00 | 12669 | 238.13104 | 16719 |
| 12 | 20:00 | 6621 | 128.19056 | 9014 | 27 | 3f:30 | 13005 | 243.72208 | 17111 |
| 13 | 24:00 | 7389 | 140.97008 | 9910 | 28 | 40:00 | 13053 | 244.52080 | 17167 |
| 14 | 28:00 | 8157 | 153.74960 | 10805 | 29 | 40:3f | 13354 | 249.52944 | 17518 |

### 5.4 Music time ↔ refreshes (the remake's clock)

run_reve.py runs everything on the PIT clock (1,193,182 Hz): record k is taken at the start of the vertical retrace
of refresh k, program time (k + 412/449) / 70.0863 s (70.0863 = 25,175,000 / (800 × 449)), and the GUS timers
overflow at t_s + their periods (`Gus.period`: (256 − count) × 80 µs or × 320 µs, in PIT ticks). With refresh r as a
continuous variable (r = k at the retrace of record k):

```
startRefresh = t_s[PIT ticks] / (1193182 / 70.0863) - 412 / 449 = 29.0000000059
refresh(t)   = startRefresh + 70.0863 * t          // t: music time (s after t_s)
tick k at    refresh(tickTime(k))                   // tickTime from 5.2
positionAt(r) = position after the ticks with tickTime < (r - startRefresh) / 70.0863
```

t_s = 509,331.124025 PIT ticks (`models/music/run_reve.py --music-log`: the `out 2X9, 3` of 0x26ec3e) = the retrace
of refresh 29 to within 0.0001 PIT tick: main's timer install 0x204458 waits for a retrace (0x2045b8, 0x2045a6),
programs the PIT and returns, and main calls 0x2602a1 at once; the emulator runs that slice at the retrace's time.
On hardware the start comes some microseconds after that retrace; which refresh it is depends on the loading time
before main. Nothing in the music depends on the frame rate.

### 5.5 The stop

After the last script entry (end position 0x403f, table entry 0x206114) the main loop leaves at refresh 17518 (the
timer interrupt sees 40:3f there), calls 0x20538d (clears the screen, loads `GFX\REVE.RAW` and draws the title:
0.44 refresh in the capture), then 210 × 0x2045d0 (each waits for a retrace; the first one for refresh 17519) and
0x2602c6 right after the retrace of **refresh 17728 = 17518 + 210** (`--trace 0x204826`: refresh 17728.000000006).
That is music time 252.531511 s, after tick 13534: the stop's writes carry stamp 13535. If 0x20538d took j ≥ 1
refreshes, the stop would come at 17518 + 209 + j. The player has been stopped since tick 13355 (F00 at 40:3e: speed
0, every later tick only acknowledges), so the last 3 s are what the voices hold: the song has faded out, and the
render is below −60 dBFS from 248.78 s on.

---------------------------------------------------------------------------------------------------------------

## 6. The player tick 0x26eecc

### 6.1 State

Channel record (32 × 0x34 bytes at 0x26d4cc; 8 used), as V3.02:

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

Globals: row [0x26042c], order [0x26042d], song length [0x26042e], restart [0x26042f], counter (ticks left in the
row) [0x260431], speed [0x260432], loop count [0x260435], loop row / order / pointer [0x260436] / [0x260437] /
[0x260438], row pointer [0x260418], stride [0x260414], module channels [0x26040b], all channels [0x26040f] (V3.03),
channel loop counter [0x26043c].

Tables (static, read them from the image): finetune periods 0x26db4c (16 × 36 u16, finetune 0 first: 856 .. 113;
finetunes 8..15 = −8..−1), sine 0x26dfcc (32 bytes, 0 24 49 .. 255 .. 24: the half wave), volume 0x26e060 (65 u16,
section 14), pan 0x26e03f, voice-count divisors 0x26e0e2 (19 u16), latch tables 0x26e027 / 0x26e037, DMA page ports
0x26e141. All but the volume table are byte-identical to V3.02's.

### 6.2 Tick

```c
void irq(void)                                            // 0x26eecc
{
    w8(0x45, 0); w8(0x45, tctl);
    if (--counter == 0) row_tick();                       // u8; rows: the module's channels [0x26040b]
    else if (counter == 0xff) counter = 0;                // 0x26fc89: stuck (after F00)
    else for (c = 0; c < [0x26040f]; c++) tick_effect(&C[c]);   // all channels (module + extra)
    out(0x20, 0x20);                                      // (and 0xa0 for IRQs >= 8)
}
```

(V3.02: `dec; jle row` and inside `js stuck`. The two differ only for counters ≥ 0x80, which only EEx pattern
delays with x · speed ≥ 128 could make; the song has none.) Once stuck, every tick only acknowledges the timer: the
voices keep sounding as they are (looped samples forever) until the stop.

### 6.3 Row

```c
void row_tick(void)                                       // 0x26f544
{
    counter = speed; e = ptr;
    for (c = 0; c < channels; c++, e += 6) {
        C = &chan[c]; select(C.voice);
        if (e.ins) { S = sample[e.ins]; C.ins = e.ins; C.smp = S; C.vol = min(S.vol, 64); C.ft = S.ft;
                     C.ctl = S.flag; C.start = S.dram; C.newsmp = 2; }
        row_effect(C, period = e.period, eff = e.eff);    // table 0x26f5cc (6.5)
    }
    if (++row >= 0x40) {                                  // u8
        row = 0;
        if (++order >= songlen) order = restart;          // V3.03: the song goes on (V3.02: counter = 0, it stopped)
        ptr = pattern[orders[order]];
    } else ptr += stride;
}
// every row handler ends in one of:
note:     /* 0x26fa5c */ C.eff = eff;
          if (period) {
              note_start(C);
period_:  /* 0x26fb44 */ if (C.ft) period = snap(period, C.ft);
              C.period = period; C.fc = T[period];
          }
volume:   /* 0x26fb7c */ vol_update(C); fc_update(C);

void note_start(C)                                        // 0x26fa68 (and 0x26f2e3)
{
    w16(7, voltab[0]); w16(8, voltab[C.vol]); w8(0x0d, 0x40);   // the old voice ramps down to 0x200 (-84 dB)
    C.gvol = 0; C.voice ^= 1; select(C.voice);                    // the note goes to the other voice
    if (C.newsmp) { C.newsmp--;
        if (C.ctl) { w16(2, S.ls >> 7); w16(3, S.ls << 9); }    // loop start (looped samples only)
        w16(4, S.le >> 7); w16(5, S.le << 9); }                   // end: loop end / last byte
    w16(0x0b, C.start << 9) x2; w16(0x0a, C.start >> 7) x2;       // position (fraction 0)
}
void vol_update(C)                                        // 0x26fb7c / 0x26f4ae
{
    if (C.gvol == C.vol) return;
    a = voltab[C.gvol]; b = voltab[C.vol];
    if (a < b) { w16(7, a); w16(8, b); w8(0x0d, 0x00); }  // ramp up to b
    else       { w16(7, b); w16(8, a); w8(0x0d, 0x40); }  // ramp down to b
    C.gvol = C.vol;
}
void fc_update(C)                                         // 0x26fbd8 / 0x26f502
{
    if (C.fc) { w16(1, C.fc); w8(0x00, C.ctl); C.fc = 0; }  // frequency, then (re)start the voice
}
u16 snap(p, ft)        // first i with (u16)p >= ftab[0][i] (or i = 36), return ftab[ft][i] (ftab[ft+1][0] if none)
```

All register writes go to the selected voice; 16-bit writes to the 8-bit ramp registers 7 / 8 put the high byte in
the register (3X5), so a ramp limit is `voltab[v] >> 8`.

### 6.4 Tick effects (table 0x26ef34; only if C.eff ≠ 0)

```c
void tick_effect(C)
{
    x = C.eff >> 8; p = C.eff & 0xff;
    switch (x) {
    case 0:  a = C.arp++;                                         // arpeggio 0x26ef74 (not used by the song)
             if (a >= 2) { C.arp = 0; out_freq(C.period); break; }
             n = a == 0 ? p & 15 : p >> 4;                        // counter 0 -> +y, 1 -> +x, 2 -> base
             for (i = 0; i < 36 && (u16)C.period < ftab[C.ft][i]; i++);
             out_freq(ftab[C.ft][min(i, 35) + n]);  break;
    case 1:  C.period -= p; out_freq(C.period); break;            // 0x26efd8, no limits
    case 2:  C.period += p; out_freq(C.period); break;            // 0x26efe4
    case 3:  if (!C.target) break;                                // 0x26efec: nothing is written
             toneporta(C); out_freq(C.period); break;
    case 4:  if (p) C.vib = nibbles(C.vib, p);                    // 0x26f078: a 0 nibble keeps the old value
             out_freq(vibrato(C)); break;
    case 5:  if (!C.target) { volslide(C, p); out_vol(); break; }  // 0x26f0f0
             toneporta(C); volslide(C, p); out_freq(C.period); break;
    case 6:  q = vibrato(C); volslide(C, p); out_freq(q); break;  // 0x26f114
    case 7:  if (p) C.trem = nibbles(C.trem, p);                  // 0x26f130
             d = (C.trem & 15) * wave(C.trempos, C.wave >> 4) >> 6; if (C.trempos & 0x80) d = -d;
             v = (u8)(C.tbase + d); C.vol = (s8)v < 0 ? 0 : min(v, 64);
             C.trempos += (C.trem >> 2) & 0x3c; out_vol(); break;
    case 9:  C.start = C.smp->dram; C.eff = 0; break;             // 0x26f1c0: first tick after 9xx
    case 10: volslide(C, p); out_vol(); break;                    // 0x26f1d4
    case 14: switch (p >> 4) {                                    // 0x26f214, table 0x26f224
             case 9:  if (--C.retrig) break; C.retrig = p & 15; retrigger(C); break;
             case 12: if (counter != (p & 15)) break;              // compared with the DOWN-counter
                      C.vol = 0; select(C.voice); w16(7, voltab[0]); w16(8, voltab[0]); w8(0x0d, 0x40);
                      C.gvol = 0; break;
             case 13: if (counter == (p & 15)) retrigger(C); break;
             case 15: C.eff = 0; retrigger(C); break;             // V3.03 0x26f484 (V3.02: nothing)
             } break;
    }                                                             // 8, B, C, D, F: nothing
}
out_freq(p):  C.fc = T[p]; out_vol();                             // 0x26f496
out_vol():    select(C.voice); vol_update(C); fc_update(C);       // 0x26f4a0
retrigger(C): select(C.voice); note_start(C); if (C.ft) C.period = snap(C.period, C.ft);
              C.fc = T[C.period]; vol_update(C); fc_update(C);    // 0x26f2e3
int vibrato(C)                                                    // 0x26f0a0: the period itself is unchanged
{   d = (C.vib & 15) * wave(C.vibpos, C.wave) >> 7; if (C.vibpos & 0x80) d = -d;
    C.vibpos += (C.vib >> 2) & 0x3c; return C.period + d; }
int wave(pos, w)   { i = (pos >> 2) & 31;  w &= 3;                // phase 0..255 = one cycle
    return w == 0 ? sine[i] : w == 1 ? (u8)(pos & 0x80 ? -(i << 3) : i << 3) : 255; }
void volslide(C, p) { if (!(p & 15)) C.vol = min(C.vol + (p >> 4), 64);
                      else C.vol = max(C.vol - (p & 15), 0); }    // 0x26f1dc
void toneporta(C)                                                 // 0x26f004
{   if (C.period > C.target) { q = C.period - C.pspeed; if (q < 0 || q <= C.target) goto done; C.period = q; }
    else if (C.period < C.target) { q = C.period + C.pspeed; C.period = q; if (q >= C.target) goto done; }
    else goto done;
    if (C.gliss) C.period = snap(C.period, C.ft);  return;        // 0x26f044, E3x (unused)
done: C.period = C.target; C.target = 0; }
```

EFx is what the sound-effect call 0x26034e puts on its channel (section 14): the note it sets up starts at the next
tick, in the player's own interrupt.

### 6.5 Row effects (table 0x26f5cc, E table 0x26f790), then `note` unless stated — identical to V3.02

| effect | handler | on the row |
|---|---|---|
| 0xy | 0x26f60c | C.arp = 0 (also for "no effect" cells) |
| 1, 2, 4, 6, 8, A | 0x26fa5c | nothing; the effect word is kept for the ticks |
| 3xx | 0x26f618 | a period becomes the target (snapped if finetune) and no note starts; xx ≠ 0 → speed |
| 5xy | 0x26f65c | as 3 without the speed |
| 7xy | 0x26f69c | C.tbase = C.vol |
| 9xx | 0x26f6a8 | if xx·256 ≤ S.lerel: C.start = S.dram + xx·256, else the effect is dropped |
| Bxx | 0x26f6d0 | row = 0x3f, order = xx − 1 (the row end moves on) |
| Cxx | 0x26f6e8 | C.vol = min(xx, 64) |
| Dxy | 0x26f700 | xy = 0 or BCD > 63 → row = 0x3f; else row = BCD − 1, order++ (wraps to restart), ptr = that row |
| E1x / E2x | 0x26f7d0 / 0x26f81c | with a note: snap, ∓x, start; without: period ∓ x, C.eff = 0, → `period_` |
| E3x, E4x, E5x, E7x | 0x26f868 .. 0x26f930 | glissando, vibrato waveform, finetune, tremolo waveform |
| E6x | 0x26f8a0 | x = 0: remember order, row − 1, ptr − stride; x > 0: count < x → jump back, count++; else count = 0 |
| E9x | 0x26f948 | C.retrig = x (kept for the ticks) |
| EAx / EBx | 0x26f958 / 0x26f978 | fine volume slide |
| EC, E0, E8, EF | 0x26fa5c | kept for the ticks (EC; EF: V3.03 restarts the note at the next tick) / nothing |
| EDx | 0x26f994 | C.period = period, C.eff kept; nothing else |
| EEx | 0x26f9a0 | counter = x · speed (u8) |
| Fxx | 0x26f9bc | xx ≥ 0x20: tempo (5.1); else speed = counter = xx (this row already; F00 stops the player) |

### 6.6 Behaviour worth knowing (all of it is the sound of the demo)

- **Voice pairs**: a note start ramps the old voice from its volume down to 0x200 (−84 dB, 63 steps per frame:
  ≤ 56 frames = 1.5 ms at 38.6 kHz) and starts the note on the other voice, which ramps up from ≈0x200 to its volume
  in the same time. The old voice keeps playing its sample under it.
- Volume changes (Cxx, Axy, EAx / EBx, instrument volume) are always ramps between `voltab` values, never jumps.
- Every frequency write also writes reg 0 = C.ctl, which restarts a one-shot voice that has stopped at its end.
- A one-shot that ends parks on its last byte: the GF1 keeps outputting that byte × volume (DC) until the voice is
  used again (the remake's 5 Hz high-pass removes it, as the card's AC-coupled output does).
- Vibrato and tremolo phases are never reset by notes. 1xx / 2xx have no period limits; the song stays inside the
  FC table (periods 55..4000; `lcaplay.py` asserts it).
- 9xx moves only the note of its own row: the next tick resets C.start to the sample start.

---------------------------------------------------------------------------------------------------------------

## 7. The song

65 orders: 0 1 2 3 4 5 6 7 42 7 8 9 8 9 18 5 10 11 19 11 20 22 42 7 8 9 8 21 12 13 23 13 35 9 25 26 27 29 28 14
15 16 15 30 12 13 23 34 31 3 43 44 37 39 40 41 45 24 27 29 15 30 47 46 33 (patterns 0..47), 8 channels. Breaks
(D00): 00:2f, 01:33, 02:2f, 03:3b, 30:2f, 31:3b; all other orders play 64 rows. Speed / tempo: 00:00 F50 F06, 02:00
F50, 04:00 F03 F96 (and F96 / F03 again in most patterns from 05:00), 2f:38 F06, 30:00 F50, 32:00 F03 F96,
40:30 F0F, 40:36 F08, 40:3e F00 (the end: the player stops with the position at 40:3f, the last script entry's end
position). About 3,900 notes start (with a frequency write) before the stop. The demo ends before the song could wrap.

---------------------------------------------------------------------------------------------------------------

## 8. Other entry points, no sound

- 0x2601dc load (3.1), 0x2602a1 start (GUS 0x26ec3e / SB 0x2703c0), 0x2602c6 stop (0x26ecc4 / 0x270480), CF =
  error; 0x2602eb position (5.3).
- 0x2602f9 set position (AL = order, AH = row): [0x26042d] = AL, [0x26042c] = AH, ptr = pattern[orders[order]] +
  row × stride; counter, speed and the channels are left alone. main calls it when Enter is pressed (skip to the
  current entry's end position: `mov eax, [esi + 0x10]; xchg ah, al`).
- **No sound** ([0x260406] = 0: NOSOUND or no card): the module is converted for the SB mixer (0x26fd7c) and the
  demo's timer interrupt calls **0x271ad3 with EAX = 70** at every retrace: `acc [0x271b38] += ([0x271b34] << 16) / 70`
  (idiv, truncated), and for every 0x10000 in it `call 0x271108` (the SB path's player tick: the same counter, row and
  effect logic on the channel records, with the SB mixer's parameters instead of GF1 writes; not decompiled
  further) and acc −= 0x10000. [0x271b34] is the SB tick rate, `round(2 T / 5)` Hz (0x2719c4; 50 in the image for
  tempo 125), so without sound the ticks come at rate × 70.0863 / 70 Hz: tempo 80 → 32.04 Hz (GUS: 32.22 Hz), tempo
  150 → 60.07 Hz (GUS: 60.10 Hz). The syncs then drift by about 0.6 % in the tempo-80 parts.
- V3.03's sound-effect API (no caller in Reve): 0x26034e play, 0x2603dd → 0x26fca2 add a sample, 0x2603b8 →
  0x26fd02 voice position, 0x26fd58 forget the added samples (section 14).

---------------------------------------------------------------------------------------------------------------

## 9. The register stream (`full.gus`)

Lines `<stamp> v<voice> r<reg> <value>` (voice registers; 8-bit writes log the byte, 16-bit writes the word),
`<stamp> g r<reg> <value>` (0x41, 0x42, 0x45..0x47, 0x49, 0x4c), `<stamp> p<port> <value>` (2X0, 2X8 / 2X9, 2XB,
2XF, 3X0). Not logged: voice select, register select, DRAM address 0x43 / 0x44 and data 3X7. 591 lines stamped 0
(GF1 init and latches, conversion and upload: mix control and 27 `g r42` / `g r41 21` pairs, start), then per tick
`g r45 0`, `g r45 <4 or 8>` and the row / effect writes in the order of 6.3 / 6.4, then main's stop (157 lines, stamp
13535 together with tick 13534's 2). 111,241 lines. There is no SB probe in the log (V3.02 probed after finding the
GUS). Reads (0x41, 0x49, 0x8f, 2X6) are acknowledgements whose values are never used, so real hardware gets the same
stream.

---------------------------------------------------------------------------------------------------------------

## 10. GF1 model and render (`lcaplay.py --wav`, `reve/remake/src/gf1.cpp`, `music.cpp`)

STARS's model (paper/re/notes-music.md section 7): 16 active voices → 38,587.5 frames per second; per frame and
voice: out += interp(DRAM[a], DRAM[a+1], fraction / 512) / 128 × gain(volume) × (L = (15 − pan) / 15, R = pan / 15);
position += FC >> 1 (1/512 byte); a looped voice past its end wraps to start + overshoot, a one-shot parks on its end;
volume ramps of 63 steps (of 4096) per frame between reg 7 and reg 8 (high bytes << 4); gain(v12) = (256 + (v12 &
255)) · 2^((v12 >> 8) − 24). Writes of one tick are applied at the tick time (5.2), at the first frame at or after
it. The render runs from t_s to main's stop (252.531511 s).

The C++ port evaluates the same voices at the output rate (increments and ramp steps scaled by 38,587.5 / rate), mixes
on worker threads from snapshots (about 0.1 s for the whole song at 48 kHz), applies a 5 Hz one-pole high-pass (the
card's AC-coupled output; one-shots park on a non-zero byte) and normalises the peak to **−1 dBFS** (Inside's choice;
LCA's player sets no master volume). The mix peaks at 2.01 one-voice full scales (1.98 at the card's own rate).

---------------------------------------------------------------------------------------------------------------

## 11. What the original approximates (keep it for the sound; options for "sharper")

- 8-bit samples, linear interpolation at the GF1 frame rate (38,587.5 Hz with 16 voices: twice STARS's), no band
  limiting. The C++ port evaluates the voices at the output rate: only aliasing and interpolation differ.
- Pitch: the FC table (3.5), integer and rounded up to even: ±8 cents. Keep the integer FC.
- Volume: 65 table entries, ramp limits with a 4-bit mantissa, every change a ≤ 1.5 ms ramp, note starts as a cross-
  fade between the two voices of a pair. Keep.
- Timing: ticks on the GUS timers with the truncated periods 31.04 / 16.64 ms and the free-running phase at the
  switches (5.2). Keep them; they set where every part of the demo starts.

---------------------------------------------------------------------------------------------------------------

## 12. Time bases

| variable | time base |
|---|---|
| detection, conversion, upload | none (start-up) |
| player state (row, order, volumes, periods, effect phases) | player ticks = GUS timer IRQs (5.2) |
| position 0x2602eb | changes at the tick that reads a row (it then names the next row); the timer interrupt reads it at each retrace (5.3) |
| GF1 output | GF1 frames at 38,587.5 Hz, register writes at the tick instants |
| music start t_s | the retrace after main's timer install (refresh 29 in the capture; machine dependent) |
| music stop | main: 210 refreshes after the last entry ends (refresh 17728 in the capture, 5.5) |
| no-sound position | retraces (70.086 Hz), 0x271ad3 |

---------------------------------------------------------------------------------------------------------------

## 13. Static data for the remake (read from REVE.EXE, do not copy)

| what | where | format |
|---|---|---|
| module | `ZIC\PULSE.MOD` in the archive, 454,614 bytes | 3.2 |
| finetune periods | image 0x26db4c | 16 × 36 u16 |
| vibrato / tremolo sine | image 0x26dfcc | 32 u8 |
| volume table | image 0x26e060 | 65 u16 (high byte = ramp limit) |
| pan table | image 0x26e03f | 32 u8 |
| voice-count divisors (617400 / n) | image 0x26e0e2 | 19 u16 for n = 14..32 (only n = 16, 38587, is used) |
| voices before the conversion | image 0x26e012 | u8 (14) |
| FC constant | image 0x26e8f3 (immediate) | u32 0xda7a69e2 |
| IRQ / DMA latch codes | image 0x26e027 / 0x26e037 | u8 tables |
| script table (last end position) | image 0x205ed0 | 0x14-byte entries, end position at +0x10 |
| FC table, sample records, DRAM layout | computed (3.3 .. 3.5) | |
| timer start, stop | measured: t_s = 509,331.124025 PIT ticks (refresh 29), stop = last entry's end + 210 refreshes | 5.4, 5.5 |
| module address | measured: 0x345e30 (only the log's DMA blocks depend on it) | 3.4 |

---------------------------------------------------------------------------------------------------------------

## 14. V3.03 against V3.02 (STARS), exactly

From `models/music/vs302.py` (instruction alignment of REVE.LFL 0x25f698..0x2603ff and 0x26e16c..0x26fd7c against
STARS.EXE 0x19dd0..0x1c7f8), `addrmap.py` (every data address the aligned code uses: one consistent offset per
block) and `tables.py` (the four jump tables). The GUS player is otherwise the same code.

1. **Converter in use**: Reve's load entry 0x2601dc calls the standard-MOD converter 0x26e16c (31 samples, 128-entry
   order table, signature at 0x438, 128 pattern pointers); STARS used the 256-order "16CH" converter 0x1a1b0 (Reve's
   copy is 0x26e518, unused). Both versions contain both. The load entry allocates the pattern buffer and loads and
   frees the module itself (V3.02's took them from the caller); the conversion returns the events' end in EAX (EDI).
2. **Sample records** are 0x1c bytes (V3.02: 0x18) at 0x26c3cc; +0x18 (u16) is the rate of a sound-effect sample.
3. **Extra channels** (sound effects): 0x260340, called from the detection, stores BL in [0x26040e]; the conversion
   sets [0x26040f] = extra + module channels and the GF1 voices to twice that (> 7); tick effects run over
   [0x26040f] channels, rows over the module's [0x26040b]. API: 0x26034e (AL = extra channel, EBX = sample 0x20 + n,
   EDX = pitch factor: period = (rate × EDX) >> 12, CL = volume ≤ [0x26040d]): sets the channel's sample, period,
   volume, finetune, control and C.eff = 0xEF0, so the next tick starts the note (EFx, item 4); 0x2603dd → 0x26fca2
   (ESI = {u32 length, u16 rate, u8 volume, u8, data}): uploads a one-shot by PIO after the module's samples (DRAM
   [0x26fd65], 32-byte aligned) as sample 0x20 + EBX; 0x2603b8 → 0x26fd02: AL = extra channel → its voice's DRAM
   position relative to the note's start (regs 0x8a / 0x8b read back); 0x26fd58: forget the added samples. Reve
   passes BL = 0 and calls none of them: 8 channels, 16 voices.
4. **EFx tick effect** (E-tick table entry 15 → 0x26f484): `C.eff = 0; retrigger(C)` (V3.02: nothing).
5. **Tick counter test** (0x26eef5): `dec counter; je row; cmp 0xff; je stuck; else effects` (V3.02: `dec; jle row`,
   `js stuck`); different only for counters ≥ 0x80.
6. **End of the order list** (0x26fc2d): order = restart and play on (V3.02 also set counter = 0: the song stopped).
7. **Volume table** 0x26e060: 0x2000, 0x9fe0, 0xaef8, 0xb7cd, .. 0xeb5d (32), .. 0xfa76 (64) ≈ 0xfa76 + 0xf19 ·
   log2(v / 64) (V3.02: .. 0xefa6 ≈ 0xefa6 + 0xf00 · log2(v / 64)): v = 64 ramps to GF1 volume 0xfa0 (the high
   byte), gain 0.8125 instead of 0.484 (+4.5 dB); gain(v) ≈ 0.81 · (v / 64)^0.95. The other tables are
   byte-identical.
8. **Detection**: the SB probe 0x25fbc6 only when no GUS was found (V3.02 always probed: STARS's log has the SB resets
   of 0x246..0x286); [0x26e10c] = 0 (NOSOUND) → no card; the DMA buffer is allocated once (`[0x26e120] != 0`) through
   the extender's service 0005h, which also sets the DMA limit [0x260424] = 16 MB (V3.02: 640 KB); the SB path checks
   the MIX rate against the SB type (0x25fbc8, 0x25fdb1, 0x25fe17, 0x25fe36, 0x25fed0: 22,050 / 44,100 / 45,000 Hz);
   the player's name goes to the extender (service 0401h) instead of V3.02's disabled messages.
9. **Upload**: same routine; with the 16 MB limit the module is DMA'd in place and the blocks split at its 64 KB
   physical pages (3.4).

---------------------------------------------------------------------------------------------------------------

## 15. Open points / guesses

- The first timer-1 period (tick 0): 20.48 ms if the count register is still 0 when 2X9 = 3 starts it (run_reve.py,
  DOSBox), 20.00 ms if a card applies the count written right after. Only tick 0 moves.
- Timer semantics in general (count reloaded at overflow, both timers free-running from t_s, the phase at a switch)
  follow DOSBox and fit every record of the capture; not checked on a real GUS. The switches make the song's timing
  depend on them more than STARS's did (11.68, 14.56 and 3.68 ms first periods).
- t_s and the stop are the capture's: on hardware the timer start comes microseconds after the retrace of refresh 29,
  and that the timers start at that refresh depends on the loading time before main.
- Record 137 (5.3) is an emulator artefact; on hardware the timer interrupt would read 00:09 there. A remake that
  wants the capture's record exactly would have to model the timer interrupt's PIT settling.
- The GF1 caveats of the JIZZ and Paper notes: pan law (linear here), clipping of the voice sum (peak 2.0 of one
  voice's full scale), mantissa-linear vs exponential volume curve, ramp overshoot at the limits, whether a loop
  wraps at `pos > end` or `pos >= end`.
- Effects the song does not use (arpeggio, B, 6, 7, E3..E9, EC..EF, glissando) are ported from the code and
  unverified; the no-sound tick 0x271108 is not decompiled.
