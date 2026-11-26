# Vivid Experiment — music (scope "music": MIDAS Sound System 0.5a, EXPRMNT1.XM / EXPRMNT2.XM, the GUS device)

Addresses are linear addresses of the running program (`unpacked/vivid_img.bin` from 0x400000). "Interrupt i" = the
i-th timer interrupt (the stamp of `full.gus` / `part2.gus`, the `irqs` column of the `.ticks` files); "record k" =
record k of `full.frames` / `full.ticks`, written at program time (k + 1) · FRAME; "refresh" = one screen refresh,
FRAME = 1,193,182 / 70.086 = 17,024.67 PIT clocks; the retrace of refresh k starts at k · FRAME + 412 · LINE (LINE =
FRAME / 449) in `run_vivid.py`'s timing. PIT clock 1,193,182 Hz.

Everything below was checked, not only read (scripts in `models/music/`, see its README):

- `player.py` loads both modules through a routine-by-routine port of MIDAS's XM loader, Generic Module Player (GMP)
  and GUS sound device (`midas.py`), runs them from a port of MIDAS's screen-synchronised timer and reproduces **every
  GUS register write of `full.gus`** (372,039 of 372,039: start-up, part I, the switch to part II, part II, the exit;
  same values, same order, same interrupt stamps) and of **`part2.gus`** (238,948 of 238,948);
- the sample memory it uploads equals `full.dram` and `part2.dram` byte for byte (1 MB each);
- `statecheck.py`: the GMP's play position, speed, tempo, tick, global volume, the 24 fields of every channel and 11
  fields of every GUS voice record equal the snapshots' in 66 of the 68 snapshots of `snaps/` and 19 of the 21 of
  `snaps2/`; the other two of each are taken before part II's first row and differ only in channel fields MIDAS never
  initialises (heap garbage, section 4.9);
- the order / row columns of the `.ticks` files: in 27,534 of 27,665 records (`full`) the captured position is the
  GMP's position at the record or was until less than one refresh before it; the others are older copies (the main
  line copies the position with 0x400104 only when it calls it; section 6.5);
- `render.py` renders the music through a GF1 model that answers the device's reads as a card does. There is no audio
  capture to compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- **Music system**: MIDAS Sound System 0.5a (Sahara Surfers): `midas.c`'s API (0x40d170..0x40d820), the Generic
  Module Player (GMP, C, 0x40d820..0x40f26a, commands 0x41e930..0x41f437), the XM loader (0x418d44), MIDAS's timer
  (assembler, 0x410740..0x410f00) and the "Gravis Ultrasound Sound Device v4.00" (assembler, structure at 0x824328,
  code 0x41b1a0..0x41cd10). The S3M loader (0x41a24c), the MOD/S3M branches of the GMP and the other sound devices are
  linked in but not used.
- **Mono.** main (0x4002e0) sets the output mode to `sdMono` (mode bit 0 set, bit 1 cleared) before midasInit, so the
  GUS device's SetPanning only stores the panning: no panning register write ever reaches the card, every voice stays
  at the GF1 pan 8 that OpenChannels writes. The songs' panning (sample panning, Cx volume column, panning envelopes)
  is computed but inaudible.
- **EXPRMNT1.XM** (part I): "Vivid", FastTracker 2.00 format 0x104, 16 channels, 61 orders, **Amiga periods**, speed 7 /
  tempo 122 in the header and on row 0; speed 4 / tempo 186 from order 1, tempo 202 from order 22 row 48. Effects
  played: porta down, tone porta, vibrato, sample offset, volume slide, speed, tempo, fine porta, key off, volume
  column set-volume; one instrument with a volume envelope, seven with panning envelopes, three with fadeout.
- **EXPRMNT2.XM** (part II): "Experiment II", 20 channels, 26 orders (orders 24 and 25 name patterns 52 and 53, which
  the file does not have: MIDAS makes them 64 empty rows), **linear periods**, speed 6 / tempo 125; a ritardando on
  order 22 (tempo 123 .. 96, speed 8, 10, 1), then speed 3 / tempo 125. Effects played: porta up / down, tone porta,
  vibrato, vibrato + volume slide, sample offset, volume slide, retrig, note delay, global volume, speed, tempo, key
  off; volume column set-volume, slide down, fine slides, panning; envelopes and fadeout.
- **Time**: one timer interrupt just before each retrace (the refresh: 95 % of a measured refresh after the previous
  retrace) plus the player's interrupts in between, from a count of PIT clocks: tick period **P = 0x71ca5f0 /
  (40 · BPM)** = 2,982,950 / BPM clocks (122: 24,450; 125: 23,863; 186: 16,037; 202: 14,767). The refresh accounting
  counts a refresh as 17,024 clocks, and a tick that falls due in the last 851 clocks before a retrace starts 10 clocks
  after it (its lateness is lost): the ticks run 0.04 % (tempo 125) to 0.25 % (tempo 202) slower than P says. Section
  6 has the exact algorithm; a remake must run it (half a second of drift over part I otherwise).
- **GUS**: 16 active voices in part I (GF1 output 38,587 Hz), 20 in part II (30,870 Hz); channel n on voice n. All
  register writes for a tick are made at the start of the next player interrupt (the device's Play runs before the
  player). Volume changes are ramps (rate 0x3f) to a 65-entry volume table; a new note ramps the old one down, stops
  the voice, **waits until every starting voice's ramp has finished**, sets the sample addresses and position and
  ramps up. Pitch: FC = (rate · 1024 / output rate) & ~1. Samples: 8-bit, uploaded by poking every byte through port
  0x327, signed, each padded to a 32-byte boundary + 32..64 bytes; forward and bidirectional loops.
- **The capture's card** answers reads with the last value written (current volume = 0x0500 from the init, voices
  always stopped, ramps always finished), so `full.gus` has a volume ramp starting at 0x05 for every volume change,
  and the ramp waits end at once. A real card answers with the ramped volume and makes the device wait (section 5.6).
- **Position for the script**: 0x400104 copies the GMP's info (order, pattern, row of the row read last; the sync
  value, always −1: the songs have no W command).

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when | what |
|---|---|
| main 0x40028a | midasSetDefaults 0x40d4fc (mix rate 44100, mode 0, port / IRQ / DMA −1, amplification −1) |
| 0x4002a0.. | midasDetectSD 0x40d1f4 (calls every device's Detect: the GUS's reads `ULTRASND`, resets the card: reg 0x4c ← 0, ← 1, memory test 0x55 / 0xaa at DRAM 0 / 1) |
| 0x4002c6 | tmrGetScrSync 0x410ab6 → [0x825c60] = 34,049 (twice the PIT clocks per refresh, measured in PIT mode 3) |
| 0x4002e0 | mode [0x844bf4] = (mode \| 1) & ~2: **mono** |
| 0x4002fa | midasInit 0x40d25c: Detect again (port etc. are −1), tmrInit 0x410b45 (IRQ 0 → 0x410802), SD Init(44100, mono) 0x41b267 (stamp 0: reset, memory test of 4 × 256 KB, regs of 32 voices, reg 0x4c ← 3), tmrPlaySD 0x410bea (the timer calls the device at 50 Hz), gmpInit 0x40d820 (sets tempo 125 → SD SetUpdRate(5000)) |
| 0x400356 | tmrSyncScr 0x410d23(34049, preVR 0x40007c, immVR 0, inVR 0x403728): waits for a retrace (the one of refresh 4 in both captures), then interrupts come just before each retrace (section 6) |
| script line `LoadXM` 0x40b5a0 | xmLoadModule 0x418d44("exprmnt1.xm" from disk, addSD 1, no callback, &[0x825c68]): converts the patterns, AddSample every sample (uploads, no logged writes) |
| 0x40039f | midasPlayModule 0x40d618(module, 0): **after interrupt 31** (full) / 17 (part2): OpenChannels(16) (reg 0x0e ← 0xcf, reg 0x0c ← 8 for voices 0..15, port 2X0 ← 0), gmpPlaySong 0x40d8b0, tmrAddPlayer 0x410cdb(gmpPlay 0x40de40), gmpSetUpdRateFunct 0x40d87c(tmrSetUpdRate 0x410e21) → the tick period becomes 24,450 clocks |
| interrupt 33 (full) | first GMP tick; first row (order 0 row 0) read at the 7th, interrupt 48, refresh 33.16 (program time 0.473 s) |
| end of part I (after interrupt 30267, record 14447) | midasStopModule 0x40d728 (CloseChannels: per voice reg 0 ← 3, ← 3, reg 0x0c ← 8), free module 0x40dc74, midasClose 0x40d3c4 (… SD Close: reset, port 2X0 ← 0), midasInit again (Detect, Init: stamp 30267), tmrSyncScr (retrace of refresh 14448) |
| part II | `LoadXM exprmnt2.xm`; midasPlayModule after interrupt 30284: OpenChannels(20) (reg 0x0e ← 0xd3); first GMP tick at interrupt 30286; first row at interrupt 30298, refresh 14467.14 |
| exit (after interrupt 52937) | midasStopModule (CloseChannels of 20 voices), midasClose (reset, port 2X0 ← 0) |

`vivid.exe 2` (part2 capture): the same with part II only: midasPlayModule after interrupt 17; first row at interrupt
31, refresh 23.14; stop after interrupt 22670.

Order starts (row 0 read, refresh = program time / FRAME, full run; `models/music` `player.py --ticks`):

```
part I : 0:33.16 1:677.15 2:918.92 3:1160.69 4:1402.45 5:1644.22 6:1885.99 7:2127.80 8:2369.57 9:2611.34 10:2853.11
         11:3094.92 12:3336.69 13:3578.45 14:3820.22 15:4061.99 16:4303.80 17:4545.57 18:4787.34 19:5029.11 20:5270.92
         21:5512.69 22:5754.45 23:5991.46 24:6214.06 25:6436.66 26:6659.27 27:6881.92 28:7104.52 29:7327.12
         30:7549.72 31:7772.33 32:7994.93 33:8217.53 34:8440.13 35:8662.79 36:8885.39 37:9107.99 38:9330.59
         39:9553.19 40:9775.80 41:9998.40 42:10221.00 43:10443.65 44:10666.26 45:10888.86 46:11111.46 47:11334.06
         48:11556.66 49:11779.27 50:12001.92 51:12224.52 52:12447.12 53:12669.72 54:12892.33 55:13114.93 56:13337.53
         57:13560.13 58:13782.79 59:14005.39 60:14227.99          (stopped at order 60 row 63, interrupt 30267)
part II: 0:14467.14 1:15005.60 2:15544.06 3:16082.58 4:16621.04 5:17159.55 6:17698.01 7:18236.53 8:18774.99
         9:19313.45 10:19851.96 11:20390.42 12:20928.94 13:21467.40 14:22005.86 15:22544.37 16:23082.83 17:23621.35
         18:24159.81 19:24698.32 20:25236.78 21:25775.24 22:26313.76 23:26886.74 24:27425.20   (stopped at 24:63)
```

---------------------------------------------------------------------------------------------------------------

## 2. The songs

`models/music/xm.py` (FT2 format) and `songinfo.py` (after MIDAS's conversion).

| | EXPRMNT1.XM (663,228 bytes) | EXPRMNT2.XM (391,865 bytes) |
|---|---|---|
| name, tracker | "Vivid", "FastTracker v2.00", format 0x104 | "Experiment II", same |
| channels, orders, restart | 16, 61, 0 | 20, 26, 0 |
| patterns in the file / after loading | 62 / 62 (all 64 rows) | 52 / 54 (pattern 5: 32 rows, 33 and 50: 128, 49: 68; 52 and 53 made empty, 64 rows) |
| instruments in the file / after loading | 46 / 111 (patterns name instruments up to 111; the missing ones become empty) | 28 / 28 |
| frequencies | Amiga (flags bit 0 clear) | linear |
| speed, tempo | 7, 122 | 6, 125 |
| samples (non-empty) uploaded | 48 handles (39), 568,694 bytes, DRAM 0 .. 0x8b4e0 | 26 handles (18), 261,572 bytes, DRAM 0 .. 0x40140 |
| loops | 32 none, 6 forward, 1 bidirectional | 8 none, 5 forward, 5 bidirectional |
| envelopes (instruments that get samples) | volume 1 (ins. 13), panning 7, fadeout 3 | volume 3, panning 7, fadeout 17 |
| 16-bit samples, auto-vibrato | none, none | none, none |

Orders: EXPRMNT1 `17 8 9 10 11 12 13 14 15 43 44 45 46 16 48 45 46 47 44 49 50 18 19 57 5 6 7 4 5 6 29 0 1 2 3 21 22
23 24 30 31 32 33 20 1 2 3 35 36 37 38 39 40 41 42 51 52 53 54 55 56`; EXPRMNT2 `39 10 11 12 13 14 15 45 3 6 48 27 28
31 32 46 47 16 17 18 19 20 49 50 52 53`.

Speed / tempo changes: EXPRMNT1: order 0 row 0 speed 7 tempo 122; orders 1, 9, 13 row 0 tempo 186 speed 4; order 22
row 48 tempo 202 speed 4; order 23 row 0 tempo 202; order 50 row 48 tempo 202; speed 4 on row 0 of almost every
pattern. EXPRMNT2: speed 6 tempo 125 at order 0; speed 6 at orders 1, 12, 14, 17; order 22 rows 48..63 tempo 123, 122,
121, 120, 119, 118, 117, 116, 115, 112, 108, 106, 103, 101, 99, 96, speed 8 (row 62), 10 (63), 1 (64); order 23
speed 3 tempo 125.

GMP commands played (after conversion; count, parameters):

| song | command | count | parameters |
|---|---|---|---|
| 1 | porta down (XM 2) | 8 | 0x0e |
| 1 | tone porta (3) | 8 | 0xe0 |
| 1 | vibrato (4) | 164 | 0x94, 0xa1, 0xe0 |
| 1 | sample offset (9) | 328 | 0x04 .. 0x5a |
| 1 | volume slide (A) | 1012 | −3, −2, −1, and 0 twice (shared memory) |
| 1 | speed, tempo (F) | 49, 7 | 4, 7; 122, 186, 202 |
| 1 | fine porta (E1x / E2x) | 2 | −3, −2 |
| 1 | key off (note 97) | 2 | |
| 2 | porta up / down (1 / 2) | 12 / 22 | 1, 3, 4 / 1, 0x0a |
| 2 | tone porta (3) | 1494 | 0 (2×), 3 .. 0xf0; 1316 of them with an instrument |
| 2 | vibrato (4) | 911 | 0 (682×), 0x71, 0x82, 0xb2, 0xb4 |
| 2 | vibrato + volume slide (6) | 406 | −1 |
| 2 | sample offset (9) | 508 | 0 .. 0x3f (every value 4 to 12 times) |
| 2 | volume slide (A) | 416 | −15 (320×), −6 (64×), −3, −1 |
| 2 | retrig (E9x) | 12 | 3 |
| 2 | note delay (EDx) | 28 | 2, 3 |
| 2 | global volume (G) | 9 | 0x10 .. 0x40 |
| 2 | speed, tempo | 10, 18 | |
| 2 | key off | 249 | |
| 2 | volume column | 6x 18, 8x 1037, 9x 3216, Cx 271 | |

Not played: arpeggio, tone porta + volume slide, tremolo, set panning 8xx, position jump, set volume Cxx, pattern
break, pattern loop, E8x, note cut, pattern delay, global volume slide, multi retrig, sync, extra fine porta, panning
slide, volume column 7x, Ax, Bx, Dx, Ex, Fx. They are ported from the code in `midas.py` (except multi retrig) but not
checked against a capture.

---------------------------------------------------------------------------------------------------------------

## 3. The XM loader (0x418d44) and MIDAS's data structures

`xmLoadModule(char *name, int addSD, callback, gmpModule **out)`, Watcom C, cdecl. The file is read with MIDAS's file
routines (0x41fdd0 open, 0x41fe7c read, 0x41febc seek, 0x41fe38 close); memAlloc 0x41e8d0 / memFree 0x41e90c
(not zeroed: `malloc`). Errors (0xf invalid module, 3 heap corrupted via the heap check 0x4188a0) free everything.

### 3.1 Module (0x4c bytes)

```
+0x00 char  name[20], +0x14 = 0     XM header +0x11
+0x20 int   type = 3 (XM; 1 MOD, 2 S3M)
+0x24 int   songLength              XM +0x40
+0x28 int   restart                 XM +0x42
+0x2c int   numInsts                max(XM +0x48, highest instrument number in the patterns + 1)
+0x30 int   numPatts                XM +0x46, raised to highest order entry + 1
+0x34 byte  numChans                XM +0x44
+0x35 byte  globalVolume = 0x40     (Gxx writes it here: it is the module's, kept across a restart)
+0x36 byte  speed                   XM +0x4c
+0x37 byte  tempo                   XM +0x4e
+0x38 int  *panning                 numChans dwords, all 0x80
+0x3c u16  *orders                  songLength words = the XM order bytes (0xfffe skip / 0xffff end are never made)
+0x40 inst **instruments
+0x44 patt **patterns
+0x48 int   flags                   bit 3: linear frequencies (XM flags bit 0)
```

Loading order: 0x150 bytes of header (ID check "Extended Module: " and version ≥ 0x104, else error 0xf), seek to
header size − 0x114; the orders; then every pattern **converted** (3.2): patterns `numPatts` and above that the file
lacks get packed size 0 and 64 rows (64 empty rows); a table of "instrument used" flags (256 bytes) and the highest
instrument number are collected during the conversion; then every instrument (3.3) and its sample headers; then the
sample data (3.4).

### 3.2 Pattern data (0x418918)

Converted pattern: `int size` (bytes incl. this header), `int rows`, then per row a list of entries ended by a 0 byte:

```
byte flag:  bits 0..4 channel, bit 5: note + instrument follow, bit 6: volume follows, bit 7: command + info follow
[byte note, byte instrument]   note: octave << 4 | semitone (0..11), 0xfe key off, 0xff none; instrument 0-based
                               (0xff none)
[byte volume]                  0x00..0x40 set volume, else the XM volume column command (0x60..0xff) as it is
[byte command, byte info]      GMP command 1..0x1e (table below), info (converted)
```

Per XM cell (packed with bit 7 of the first byte, else all five fields):
- note n: n > 0x60 → 0xfe; else n − 1 → ((n−1) / 12) << 4 | (n−1) % 12 (C division: XM note 0 gives 0xff, "no note",
  but still sets bit 5);
- instrument i: i − 1 (0 → 0xff); **dropped when the note is key off**; else marked used and bit 5 set;
- volume v: 0 → no volume; 0x10..0x50 → v − 0x10; any other non-zero value kept (0x01..0x0f would then be "set volume
  1..15": a MIDAS quirk; the songs have none);
- effect e / parameter p (if either is non-zero): command = table 0x8238e4[e] (36 bytes: XM 0..9, A..D → 1..0xe,
  E → table 0x823908[p >> 4], F → speed 0xf if p < 0x20 else tempo 0x10, G → 0x19, H → 0x1a, K → see below,
  P → 0x1e, R → 0x1b, W (0x20) → 0x1c sync, X → 0x1d; L, T and the rest → 0 = dropped). Parameter conversions:
  - 5xy, 6xy, Axy, Hxy, Pxy: x ≠ 0 → +x, else −y (one signed byte; when both nibbles are set only x counts);
  - Exy: E1y → cmd 0x11 info −y, E2y → 0x11 +y, E6y → 0x12, E8y → 0x13, E9y → 0x14, EAy → 0x15 +y, EBy → 0x15 −y,
    ECy → 0x16, EDy → 0x17, EEy → 0x18, all with info y; E0, E3, E4, E5, E7, EF → dropped (no glissando, no vibrato
    or tremolo waveforms, no finetune command);
  - Kxx: the effect is dropped and the note becomes 0xfe (key off **at once**, xx ignored; the instrument byte stays);
  - X1y → −y, X2y → +y, other X → dropped.
Entry flag 0 (nothing in the cell) writes nothing.

### 3.3 Instruments (0x3c + n · 0x54 bytes) and envelopes

Per instrument: 29 bytes of header; if it has samples, the next 0xd6 bytes (sample header size, keymap, envelopes,
…, fadeout). n = number of GMP samples = **0 if no pattern uses the instrument**, else max(keymap) + 1 (at least 1;
the keymap is read only if the file says ≥ 1 sample). Unused instruments get no samples and nothing is uploaded for
them. An instrument the file does not have (number ≥ the file's count) is named "S2 loves Triton!" and gets one empty
sample.

```
+0x00 char  name[22], +0x16 = 0
+0x20 int   numSamples (n)
+0x24 byte *keymap      96 bytes, only if the file has more than one sample; else 0 (sample 0 for every note)
+0x28 env  *volEnv      0 unless XM volume type bit 0
+0x2c env  *panEnv      0 unless XM panning type bit 0
+0x30 int   fadeout     XM word
+0x34 byte  vibType, vibSweep, vibDepth, vibRate  (loaded, never used: MIDAS 0.5 has no auto-vibrato)
+0x38 int   used flag
+0x3c       samples[n], 0x54 bytes each:
   +0x00 char name[22]   +0x20 int sdHandle   +0x24 data (0 when the device keeps it)   +0x28 int length
   +0x2c byte type (0 none, 1 8-bit; 16-bit is converted to 8-bit)   +0x2d byte loop (0/1)   +0x2e byte loopType
   (0 none, 1 forward, 2 bidirectional; no loop when loop end = loop start)   +0x30 loopStart   +0x34 loopEnd
   (start + length)   +0x40 int volume   +0x44 int panning (0..255)   +0x48 int relativeNote (signed)
   +0x4c int finetune (signed)   +0x50 int file offset of the data
env (0x34 bytes): byte numPoints, byte sustain (0xff none: XM type bit 1 clear), byte loopStart, byte loopEnd (both 0xff
   unless type bit 2), then 12 × {u16 x, u16 y} as in the file
```

### 3.4 Samples and AddSample

Every sample of every instrument, in instrument and sample order, is decoded (8-bit deltas, then **+0x80**: MIDAS
keeps 8-bit samples unsigned; 16-bit deltas are summed and converted to `(sum >> 8) + 0x80`, length and loop halved)
and given to the device with `AddSample(sdSample *, copy 1, &sample->sdHandle)` (SD +0xa4 → 0x41c001):

```
sdSample: +0 data, +4 1, +8 type (1 = 8-bit unsigned; 0 = none for an empty sample), +0xc length,
          +0x10 loopMode (0 none, 1 sdLoop1), +0x14 loop1Start, +0x18 loop1End, +0x1c loop1Type (0, 1 fwd, 2 bidi),
          +0x20 loop2Start, +0x24 loop2End, +0x28 loop2Type (all 0)
```

Every sample gets a handle (1, 2, … in order), empty ones too (their record has length 0). The GUS upload is in
section 5.5. Free (0x40dc74): RemoveSample for every handle, free everything.

---------------------------------------------------------------------------------------------------------------

## 4. The Generic Module Player (XM branch)

Globals: [0x844cbc] sound device, [0x844cb8] current play handle, [0x844cb0] its module, [0x844cb4] module type
(set by the row, tick and update routines, never cleared), [0x844cac] current channel, [0x844cc0] tempo (BPM),
[0x844ca8] the timer's rate function, [0x844c68] 16 handle slots.

### 4.1 Play handle (0x58 bytes) and channel (0x40 bytes)

```
handle: +0x0c position   +0x10 pattern   +0x14 row   +0x08 pattern data pointer (0: read from row +0x14 of the
        pattern)   +0x18 loopEnd = songLength − 1   +0x1c restart   +0x20 tempo   +0x24 speed   +0x28 tick
        +0x2c pattern delay   +0x30 numChans   +0x34 max period 0xd600   +0x38 min period 0x39   +0x3c period
        shift 2   +0x40 max volume 0x40   +0x48 channels   +0x4c info (position, pattern, row, tempo, speed, sync)
        +0x50 sync callback 0   +0x54 sync value −1
channel: +0x00 period (stored)   +0x04 instrument (−1)   +0x08 instrument of the last instrument byte   +0x0c tone
        porta target   +0x10 sample offset   +0x14 sample (0xff)   +0x15 device channel   +0x16 volume column
        +0x17 note (last note byte, kept)   +0x18 info memory (last non-zero info of any command)   +0x1c volume
        +0x1d command   +0x1e info (this row)   +0x1f tone porta speed   +0x20 volume used (= +0x1c)   +0x21 panning
        0..255   +0x22 volume envelope position   +0x26 panning envelope position   +0x2a fadeout volume (0x10000)
        +0x2e volume envelope held   +0x2f panning envelope held   +0x30 key off   +0x31 retrig count   +0x32 final
        volume   +0x33 last played note   +0x34 vibrato speed   +0x35 vibrato depth   +0x36 vibrato position
        +0x37 sample offset memory   +0x38 loop row   +0x39 loop count   +0x3c flags: 1 note, 2 volume,
        4 instrument, 8 command, 0x10 delayed note
```

gmpPlaySong (0x40d8b0) initialises only: period 0, instrument / last instrument −1, target 0, offset 0, sample 0xff,
note 0xff, volume 0x40 (both), command 0, porta speed 0, +0x30, +0x34..+0x39 0, +0x33 0xff, and the panning from the
module's table through 0x40f000 — **which at part I's start takes the MOD/S3M branch** ([0x844cb4] is still 0):
panning 0x80 − 0x40 = 0x40 is stored (and SetPanning(0x40) called); a channel keeps 0x40 until a sample sets its
panning (inaudible: mono). In part II [0x844cb4] is 3 and the panning is 0x80. All other channel fields (info memory,
envelope state, flags, volume column, command) are heap garbage until written (4.9). Position 0, row 0, pattern =
orders[0], tick 0, speed and tempo from the module (tempo through 0x40ebc8: SD SetUpdRate(40 · BPM), and once
midasPlayModule has installed it, the timer's rate).

### 4.2 The tick: gmpPlay (0x40de40)

```
tick += 1
if (tick >= speed) { tick = 0; playRow(); } else tickCommands();
update();
```
So the first row is read at the speed-th call (7th in part I, 6th in part II); the tick counter runs 1 .. speed − 1
between rows. Every call ends with update(), which sets every channel's volume and panning.

### 4.3 playRow (0x40e3dc)

```
info = (position, pattern, row)                       // what the demo's position reads return
if (patternDelay) { for ch with flags & 8: rowCommand[ch.cmd](ch.info_byte_of_this_row /*+0x1e*/);
                    tickCommands(); patternDelay--; return; }
if (!data) { data = patterns[pattern] + 8; skip `row` rows; }
for every channel: flags &= ~0xf
parse one row: entries for channels ≥ numChans are skipped;
   bit 5: note != 0xff → ch.note = note, flags |= 1;   instrument != 0xff → ch.lastInst = instrument, flags |= 4
   bit 6: ch.volCol = byte, flags |= 2;   bit 7: ch.cmd, ch.info8 (+0x1e), flags |= 8
for every channel (in order):
  if (flags & 4) {                                    // instrument
      volEnvPos = panEnvPos = 0; volEnvHeld = panEnvHeld = 0; fade = 0x10000; keyOff = 0;
      if ((flags & 8 && cmd == 4 /*tone porta, not 6*/) || (flags & 1 && note == 0xfe))
          ins = instruments[instrument];              // keep the current instrument (instrument −1: garbage read)
      else { instrument = lastInst; ins = instruments[instrument];
             if (flags & 1 && note < 0xfe) sample = ins.keymap ? ins.keymap[clamp(oct*12+semi, 0, 95)] : 0; }
      if (sample != 0xff) { s = ins.samples[sample];
          volume = volume2 = min(max(s.volume, 0), 64);  setPanning(s.panning);  offset = 0;
          if (flags & 1 && s.sdHandle) SD.SetSample(ch, s.sdHandle); }
  }
  if (flags & 2) { if (volCol <= 0x40) volume = volume2 = min(volCol, 64); else volumeColumnRow(); }   // 4.6
  if (flags & 8) { if (info8) info = info8;          // ONE memory for all commands
                   if (cmd) rowCommand[cmd](info); }  // 4.5 (most handlers use info8 instead)
  if (flags & 1) {                                    // a note (tone porta, sample offset and note delay clear it)
      if (instrument != -1) {
          ins = instruments[instrument];
          if (note < 0xfe) sample = ins.keymap ? ins.keymap[...] : 0;
          if (sample != 0xff && note != 0xfe) { instrument = lastInst; offset = 0;
              if (instruments[lastInst].samples[sample].sdHandle) SD.SetSample(ch, that handle); }
      }
      newNote();                                      // 4.4
  } else if (!(flags & 8 && cmd == 5 /*vibrato*/) && (volCol & 0xf0) != 0xb0 /*stale value too*/)
      changePeriod(period);                           // undoes vibrato / arpeggio offsets on every other row
}
if (data) {                                           // no jump or break this row
  if (++row >= pattern.rows) { row = 0;
     do { if (++position > loopEnd || orders[position] == 0xffff) position = restart; }
     while (orders[position] == 0xfffe);
     pattern = orders[position]; data = 0; }
}
```

### 4.4 Notes, periods and frequencies

newNote (0x40f0a4):
```
vibratoPos = 0
if (instrument != -1 && sample != 0xff) {
  if (note == 0xfe) { keyOff = 1; volEnvHeld = panEnvHeld = 0;
                      if (!ins.volEnv) volume = volume2 = 0; }      // no envelope: key off cuts at once
  else { keyOff = 0; lastNote = note; period = notePeriod(note);   // not clamped
         SD.PlaySound(ch, periodToRate(period));
         if (offset) SD.SetPosition(ch, offset); }
}
flags &= ~1
```

notePeriod (0x40ec10, XM), s = the channel's sample:
```
n = semi + oct * 12 + s.relativeNote, clamped to 0..118
linear: period = 7680 - n * 64 - trunc(s.finetune / 2)                       (0x1e00)
Amiga:  o = n / 12, k = n % 12, q = trunc(finetune / 16), f = |finetune| & 15
        p1 = T[k*8 + q], p2 = T[k*8 + q + (finetune < 0 ? -1 : 1)]          T = 0x822280 (dwords, 856 .. 431)
        period = ((p1 * (16 - f) + p2 * f) * 2) >> o
```
The Amiga table index can leave the table (k = 0 with a negative finetune reads 0x82227c.., k = 11 with finetune ≥
112 reads 0x822400 = 1712): those are what the program reads; take them from the image too.

periodToRate (0x40ee6c, XM; period 0 → error 0x11, never reached):
```
linear: rate = L[period % 768] >> ((period / 768) & 31)       L = 0x822430, 768 dwords (L[0] = 535,232 = 8363 · 64)
Amiga:  rate = 14,317,456 / period                            (0xda7790)
```
setPeriod (0x40eef4: period 0 → nothing; clamp to 0x39 .. 0xd600 **unsigned**, so a negative period becomes 0xd600;
store; SD.SetRate(ch, rate)) and changePeriod (0x40ef58: the same without the zero test and without storing).

### 4.5 Commands (row handlers table 0x824d68, tick handlers 0x824de4)

`info` is the shared memory (+0x18), `info8` the row's own byte (+0x1e). Tick handlers run only on rows whose entry
has the command (flags & 8 is cleared at every row), on ticks 1 .. speed − 1.

| GMP | XM | row (tick 0) | ticks 1 .. speed−1 |
|---|---|---|---|
| 1 | 0xy arpeggio | — | changePeriod(notePeriod(note + [0, x, y][tick % 3])) with info; semitone > 11 → next octave; nothing without instrument / sample / on key off |
| 2 | 1xx porta up | — | setPeriod(period − (info << 2)) |
| 3 | 2xx porta down | — | setPeriod(period + (info << 2)) |
| 4 | 3xx tone porta | if a note (flags & 1), instrument, sample and not key off: target = notePeriod(note); flags &= ~1 (the note is not played) | if target: if info8 → portaSpeed = info8; step = portaSpeed << 2; move period toward target by step without passing it (setPeriod) |
| 5 | 4xy vibrato | — (and the row's period is not reset) | if y: depth = y; if x: speed = x; d = (S[pos & 31] · depth) >> 5, S = 0x824c30 (32 bytes 0..255..24); changePeriod(period ± d, − when pos & 32); pos += speed (byte) |
| 6 | 5xy tone porta + vol slide | as 4 | volume slide(info), then tone porta with speed 0 (the stored speed) |
| 7 | 6xy vibrato + vol slide | — (period reset on the row) | volume slide(info), then vibrato with 0 (stored speed / depth) |
| 8 | 7xy tremolo | nothing | nothing (not implemented) |
| 9 | 8xx panning | setPanning(info8) | |
| 0xa | 9xx sample offset | if info8: mem = info8; if a note: **offset += mem << 8**; newNote() | |
| 0xb | Axy volume slide | — | volume = clamp(volume + (signed char)info, 0, 64) |
| 0xc | Bxx jump | position = info8 (> loopEnd → restart); pattern = orders[position]; data = 0; row = 0 | |
| 0xd | Cxx volume | volume = clamp(info8, 0, 64) | |
| 0xe | Dxx break | if data: position + 1 as at the pattern end; data = 0; row = BCD(info8) (> 0x63 → 63) | |
| 0xf | Fxx speed | xx ? speed = xx : jump to restart, row 0 (not stop) | |
| 0x10 | Fxx tempo | tempo(xx): SD SetUpdRate(40 · xx), timer rate (section 6) | |
| 0x11 | E1y / E2y fine porta | setPeriod(period + ((signed char)info << 2)) (E1 −y, E2 +y) | |
| 0x12 | E6y loop | y = 0: loopRow = row; else first time count = y and jump, then count−− and jump while > 0 (data = 0, row = loopRow) | |
| 0x13 | E8y | panning (S3M style: y·8, 0x40, 0x80 − (15 − y)·8) | |
| 0x14 | E9y retrig | retrigCount = 1 | if info8: if count ≥ y: SD.SetPosition(ch, offset), count = 0; count++ |
| 0x15 | EAy / EBy fine vol slide | volume slide(info) once | |
| 0x16 | ECy note cut | if y == tick (0): volume 0 | if y == tick: volume 0 |
| 0x17 | EDy note delay | if y and a note: flags = (flags \| 0x10) & ~1; else flags &= ~0x10 | if y == tick and flags & 0x10: newNote() |
| 0x18 | EEy pattern delay | if no delay running: delay = y | |
| 0x19 | Gxx global volume | module +0x35 = min(xx, 64) | |
| 0x1a | Hxy global vol slide | — | gv = (gv + info) & 0xff; > 0x7f → 0; > 0x40 → 0x40 |
| 0x1b | Rxy multi retrig | retrigCount = 1 | 0x41eeb8 (tables 0x824c4d / 0x824c50 / 0x824c5d; not ported) |
| 0x1c | Wxx sync | sync = xx; call the sync callback (none) | |
| 0x1d | X1y / X2y extra fine porta | setPeriod(period + (signed char)info) | |
| 0x1e | Pxy panning slide | — | setPanning(panning + (signed char)info) |

Quirks the songs reach (all reproduced by the port):
- **Shared info memory**: Axx with 00 slides by the last non-zero info of *any* command on the channel (EXPRMNT1 twice);
  4xx, 3xx and 9xx use their own memories (info8), so EXPRMNT2's 682 "400" keep the vibrato's speed and depth.
- **Sample offset adds up**: the offset is cleared only by an instrument byte (or a normal note start), so 9xx on a note
  without instrument byte adds to an offset left by an earlier 9xx note.
- **Tone porta with an instrument** (EXPRMNT2: 1,316 times): the *current* instrument's sample gives the volume and
  panning; the new instrument number is not taken (but 5xy = GMP 6 takes it).
- **Vibrato row**: on a row with 4xy the period is not reset (the previous tick's vibrato period stays on the card
  through tick 0); on every other row without a note it is (changePeriod(period)), including 6xy rows. A Bx volume
  column (never played) also blocks the reset, and the test looks at the channel's volume column byte even when the
  row has none.
- **Note delay**: an instrument byte's volume, panning and sample (keymap lookup, SetSample) take effect on tick 0
  (the old note keeps sounding at the new volume: the card gets the new sample's addresses only when a voice starts);
  the note starts on tick y. Without an instrument byte the keymap is not consulted: the delayed note plays the
  channel's current sample.
- **Key off** (note 97 or Kxx at once): with a volume envelope the release starts (fadeout, sustain released); without
  one the volume becomes 0. The instrument byte is dropped with note 97.
- F00 restarts at the restart position instead of stopping; Kxx's tick is ignored; L, T, E3, E4, E5, E7, tremolo and
  auto-vibrato do nothing.

### 4.6 Volume column (XM only; > 0x40 after loading)

Row (0x41f328): 8x volume − x, 9x volume + x (clamped), Ax vibrato speed = x, Cx panning = x · 17 (0xf → 0xff),
Fx as the tone porta row handler. Ticks (0x41f3a4, every tick 1 .. speed−1 of the row): 6x slide −x, 7x slide +x,
Bx vibrato with x (depth), Dx panning − x, Ex panning + x, Fx tone porta with speed x << 4.

### 4.7 update (0x40dec4, XM branch), every tick after the row / tick commands

```
for every channel:
  vol = volume2
  if (instrument != -1 && (e = ins.volEnv)) {
      vol = (vol * fade) >> 16;  if (keyOff) { fade -= ins.fadeout; if (fade < 0) fade = 0; }   // fade after use
      k = first point with x[k] >= pos, else numPoints − 1
      value = x[k] == pos ? y[k] << 8
                          : (y[k−1] << 8) + trunc(((y[k] − y[k−1]) << 8) / (x[k] − x[k−1])) * (pos − x[k−1])
                          //  the division comes first: the interpolation steps are truncated
      vol = clamp((value * vol) >> 14, 0, 64)
      advance(volEnvPos, volEnvHeld)
  }
  vol = (vol * module.globalVolume) >> 6;  finalVol = vol;  SD.SetVolume(ch, vol)
  pan = panning
  if (instrument != -1 && (e = ins.panEnv)) {
      the same with panEnvPos (not found: k = numPoints, one past the last point; not reached)
      d = (value >> 8) - 32
      pan += trunc((128 - |pan - 128|) * d / 32);  if (pan < 0) pan = 255;  if (pan > 255) pan = 255
      advance(panEnvPos, panEnvHeld)
  }
  SD.SetPanning(ch, trunc((pan - 128) / 2))          // −64..63; ignored by the mono device

advance(pos, held):   (sus, ls, le are the envelope's signed bytes; x[−1] reads the header bytes)
  if (x[sus] == pos && !keyOff && sus != -1) held = 1
  if (!held) { if (x[numPoints-1] > pos) pos++;
               if (x[le] <= pos && le != -1) pos = x[ls];
               if (x[sus] == pos && !keyOff && sus != -1) held = 1; }
```

The envelope position counts ticks from the instrument byte (an instrument byte resets it, a note without one does
not). Fadeout acts only with a volume envelope. Panning envelopes and sample panning are computed but never reach the
card (mono).

### 4.8 What the remake keeps / evaluates exactly

Keep: everything above as integer arithmetic, the shared memory, the truncated envelope interpolation (it is the
volume the card gets), the period / frequency tables and the GF1's FC rounding (5.3). No low-resolution shortcut to
undo: the music's grain is the card's (5.7).

### 4.9 Uninitialised channel fields

The channel array comes from the heap without being cleared. Part I's ticks before the first row and part II's run
tick commands for channels whose garbage flags have bit 3 (or bit 1 with a garbage volume column byte > 0x40): in the
part II snapshots (`snaps/f14465`) the garbage commands are 0 and one channel would run a volume-column tone porta
on a voice that is not playing. None of it reaches the card (the captures match a port that starts from zeros). The
remake starts from zeros.

---------------------------------------------------------------------------------------------------------------

## 5. The GUS sound device ("Gravis Ultrasound Sound Device v4.00")

### 5.1 Structure 0x824328 and globals

SoundDevice: tempoPoll 1, configBits 0x11, port 0x220 (from `ULTRASND`), IRQ 0, DMA 0, card type 1, modes 0xb,
names, 6 port addresses, then the functions: +0x30 Detect 0x41b1b5, Init 0x41b267, Close 0x41b45d, GetMixRate,
GetMode, OpenChannels 0x41b4c3, CloseChannels 0x41b5ce, ClearChannels 0x41b5ea, Mute 0x41b69f, Pause 0x41b6d0,
SetMasterVolume 0x41b7b6, GetMasterVolume, SetAmplification 0x41b7ea, GetAmplification, PlaySound 0x41b80c,
ReleaseSound 0x41b92a, StopSound 0x41b951, SetRate 0x41b99f, GetRate, SetVolume 0x41ba68, GetVolume, SetSample
0x41baf1, GetSample, SetPosition 0x41bc76, GetPosition 0x41bd3f, GetDirection, SetPanning 0x41be67, GetPanning,
MuteChannel 0x41bf89, AddSample 0x41c001, RemoveSample 0x41c3a0, SetUpdRate 0x41c42b, StartPlay 0x41c43b (nothing),
Play 0x41c43e. cdecl; return 0 or an error number (0x12 bad channel, 0x13 bad sample).

Globals: [0x823958] = port + 0x103 (register select), [0x823954] = port + 0x102 (voice select), [0x823950] channels,
[0x82395c] GF1 output rate, [0x823960] update rate (40 · BPM), [0x823964] next free handle, [0x823978] highest
handle, [0x82397c] DRAM size (1 MB in the capture), [0x823980] free DRAM, [0x823970] master volume 64, [0x823974]
"recompute every volume", [0x823990] **mono**, [0x82399c] channels open, [0x823998] uploading, [0x824324] sample
records (256 × 0x30), voice records 0x8239a4 + 0x4c · v (32).

Voice record: +0 flags (1 stop, 2 start, 4 volume, 8 rate, 0x10 sample addresses, 0x20 paused, 0x40 panning, 0x80
muted, 0x100 released), +4 sample handle, +8 handle playing, +0xc FC, +0x10 rate (Hz), +0x14 volume 0..64, +0x18
surround, +0x1c sample type, +0x20 loop mode, +0x24 loop 1 type, +0x28 loop 2 type, +0x2c start address, +0x30 loop
start, +0x34 loop end, +0x38 / +0x3c loop 2, +0x40 panning, +0x44 GF1 pan, +0x48 active (set by PlaySound /
SetPosition, cleared only by OpenChannels / ClearChannels). Sample record: +0 type (0 = free slot; 1 for 8-bit and for
empty samples), +4 DRAM address, +8 second copy (surround), +0xc length, +0x10 loop mode, +0x18 loop 1 type, +0x1c /
+0x20 loop 1 start / end, +0x24 loop 2 type, +0x28 / +0x2c loop 2.

Port helpers: a "delay" 0x41b1a0 = 8 reads of port 0x300 (about 8 µs on the ISA bus); register writes: select (3X3),
byte to 3X5 or word to 3X4. Several writes are repeated after the delay (reg 0 when stopping, reg 0x0d, the position
registers 0x0b / 0x0a): they appear twice in `full.gus`.

### 5.2 Init, OpenChannels, Close

```
Detect 0x41b1b5: port from ULTRASND (3 hex digits); reset; DRAM[0] = 0x55, DRAM[1] = 0xaa (poke through 3X7), read back
reset 0x41b436: reg 0x4c ← 0, delay ×2, reg 0x4c ← 1, delay ×2
Init 0x41b267(rate, mode): mono = mode & 1; reset; memory test per 256 KB bank b (0x55 at b·0x40000, 0xaa at +1) while
  it reads back → DRAM size; port 2X0 ← 3; reg 0x0e ← 0xdf; for voice 32, 31 .. 1 (voice 32 selects voice 0):
  reg 0 ← 3, reg 9 ← 0x0500, reg 0x0c ← 8, reg 0x0d ← 3, reg 6 ← 0x3f (ramp rate: 63 per frame); reg 0x4c ← 3;
  port 2X0 ← 0; DRAM heap: one free block [0, size); sample table cleared
OpenChannels 0x41b4c3(n): voices = max(n, 14); output rate = W[voices − 14] (0x824452: 44100, 41160, 38587, 36317,
  34300, 32494, 30870, …, 19293); reg 0x0e ← (voices − 1) | 0xc0; voice records cleared; for v < n: reg 0x0c ← 8,
  pan 0, flags = 1; port 2X0 ← 0
ClearChannels 0x41b5ea / CloseChannels: per voice reg 0 ← 3, delay, reg 0 ← 3, reg 0x0c ← 8; records: sample 0,
  playing 0, panning 0, flags 1 (active stays)
Close 0x41b45d: reset; port 2X0 ← 0; heap and table freed
```

### 5.3 The calls (they only change the voice record; Play writes the registers)

```
SetRate(ch, rate):  if (rate != r.rate) { r.rate = rate; r.fc = ((rate << 10) / outputRate) & ~1; flags |= 8; }
PlaySound(ch, rate): SetRate part; s = samples[r.sample] (none → error 0x13); length 0 → StopSound;
                     r.start = s.address; flags = flags & ~0x101 | 2; active = 1
StopSound(ch):      flags = flags & ~2 | 1
SetVolume(ch, v):   v = min(v, 64) (unsigned); if changed: r.volume = v, flags |= 4
SetSample(ch, h):   same handle → nothing; else r.sample = h; length 0 → StopSound; type, start = address, loop types,
                    mode; mode 0 / 4: loop start = address, loop end = address + length; else loop start / end =
                    address + loop1Start / loop1End (loop 2 likewise); flags |= 0x10
SetPosition(ch, p): no sample → nothing; p > length → StopSound (mode 5: loop start); r.start = address + p;
                    flags = flags & ~1 | 2; active = 1                  (sample offset and retrig restart the voice)
SetPanning(ch, p):  selects the voice; if changed: r.pan = p; **mono: return**; (stereo: GF1 pan = (p >> 3, + 1 if
                    negative) + 7, flags |= 0x40, the "every volume" flag set)
SetUpdRate(r):      [0x823960] = r (used only by the MOD sample-change logic of Play)
```

The pitch: the GF1 adds FC / 1024 samples per output frame (FC bit 0 ignored), so the played rate is
(FC / 1024) · output rate, FC truncated to an even number: at 38,587 Hz a C-4 at 8,363 Hz gives FC 220 (8,290 Hz,
−15 cents), and FC steps of 2 are 8 to 16 cents for the usual notes. That detuning is part of the original's sound.

### 5.4 Play 0x41c43e (called by the timer before the player, every player interrupt)

```
if (paused || !channelsOpen) { *callMP = 0; return; }
save the voice and register selects (register 0x43 while uploading)
for v < channels, active:
  select v
  if (flags & 3) {                               // stop or start: ramp the old note down, stop the wave
      flags &= ~1; reg 7 ← 0x15; reg 0x0d ← 0x40; reg 0 ← 3; delay; reg 0 ← 3; reg 0x0d ← 0x40
  } else if (everyVolume || flags & 4) {
      flags &= ~4
      cur = read16(reg 0x89) & 0xff00
      tgt = V[(muted ? 0 : volume) * master >> 6] & 0xff00          V = 0x824478: 65 words, 0x1500, 0x8f10 .. 0xef00
      if (cur != tgt) { lo, hi = sorted(cur, tgt) >> 8; dir = cur < tgt ? 0 : 0x40
          reg 0x0d ← 3; delay; reg 0x0d ← 3; reg 7 ← lo; reg 8 ← hi; reg 0x0d ← dir; delay; reg 0x0d ← dir }
  }
  if (flags & 8)    { flags &= ~8;    reg 1 ← FC (16 bit) }
  if (flags & 0x40) { flags &= ~0x40; reg 0x0c ← GF1 pan }     // never (mono)
  if (flags & 0x10 && loopMode in (4, 5) && !(flags & 2)) { MOD sample change at the loop end; never }
do {                                              // start the voices: wait for their ramps first
  retry = 0
  for v < channels, active, flags & 2:
    select v
    if (!(read8(reg 0x8d) & 1)) { retry = 1; continue; }        // ramp still running: next voice, then again
    if (flags & 0x10) { reg 4/5 ← loop end; reg 2/3 ← loop start; flags &= ~0x10 }   // (a >> 7, a << 9)
    reg 0 ← 3; reg 0x0a/0x0b ← start; delay; reg 0x0b again; reg 0x0a again; reg 0 ← 3
    playing = sample
    c = loop1Type 0 → 0, 1 → 8, 2 → 0x18 (| 4 for 16-bit); reg 0 ← c
    reg 8 ← V[volume * master >> 6] >> 8; reg 0x0d ← 0; delay; reg 0x0d ← 0; reg 0 ← c
    flags &= ~6
} while (retry)
restore the selects; everyVolume = 0; *callMP = 1
```

So a new note: the old note ramps down from where it is to 0x15xx at 63 steps per frame (a full-volume note: 55
frames, 1.4 ms at 38.6 kHz), the wave is stopped at once, the device **busy-waits until every starting voice's ramp has
ended**, then the voice starts at its start address and ramps from 0x15xx up to the target. Loop modes: none (reg 0
← 0: one-shot, the voice stops at the end address), forward (8), bidirectional (0x18). Sample addresses are byte
addresses written as (a >> 7) and (a << 9) & 0xffff; the end address is start + length (one-shot) or the loop end.

### 5.5 Sample memory (AddSample 0x41c001, heap 0x41cb45)

```
handle = next free; type 0 → length 0
if (length) {
   size = (length & ~0x1f) + 0x40;   address = best-fitting free block (here: the next free byte, 32-aligned)
   8-bit: every byte ^= 0x80 (unsigned → signed), poked to DRAM through reg 0x44 / 0x43 and port 3X7, byte by byte;
   then 0x40 − (end & 0x1f) bytes: the loop's first bytes again (looping sample) or the last byte repeated (one-shot)
   (the source bytes are XOR-ed back)
}
record: type (0 → 1), address, length, loop fields; next free handle = next empty slot
```
No register writes are logged for an upload (0x43 / 0x44 and 3X7 are not in `.gus`); `full.dram` equals the port's
DRAM. A 256 KB bank crossing matters only for 16-bit samples (none).

### 5.6 The capture's card and a real one

`run_vivid.py`'s fake GUS answers reg 0x89 with the last value written to reg 9 (0x0500 after Init: nothing else
writes reg 9) and reg 0x8d / 0x80 with bit 0 set. So in `full.gus` every volume change ramps between 0x05 and the
target (`r07 5`, `r08 target`, or the reverse), the voice starts are never delayed, and any panning change would make
every voice rewrite its volume (none happens: mono). On a card: reg 0x89 returns the ramped volume (the ramp starts
where the voice is, and no ramp is written when the volume is already there), and the start loop waits up to
~1.4 ms in the interrupt for the ramp-downs; with that, the render writes 320,835 registers instead of 372,039 and
spends 137,313 passes in the wait. `render.py` models this (one card frame per pass of the wait loop; a pass with 16
voices takes about 2..3 frames of ISA time, a guess).

### 5.7 Mix (what it sounds like)

8-bit signed samples, linear interpolation, 16 voices at 38,587 Hz (part I) / 20 at 30,870 Hz (part II), ramped
volume changes (≤ 1.4 ms), new notes cut the old one after a ≤ 1.4 ms fade (click-free on a card), mono at GF1 pan 8
(slightly right of centre: L 7/15, R 8/15 in the linear pan model), FC-quantised pitch, and the 65-step volume table:
V[v] (12-bit: exponent e = V >> 12, mantissa m = (V >> 4) & 255) is built so that 2^e · m grows linearly with v
(v = 2^k → m = 0xf1 with e = k + 9, mantissa linear in v within each octave; V[64] = 0xef00, V[0] = 0x1500). On a GF1,
whose volume is logarithmic (about 0.0235 dB per 12-bit step; the clone / Inside model's (256 + m) · 2^(e − 24) is the
same within 6 %), doublings of v are exact 6 dB steps but the values between are louder than linear, by up to
1.1 dB (log law) / 1.4 dB (the model) at v = 3 · 2^k (24 sounds like 27..28). V[64] is −6.4 dB of the card's full
scale. The remake should take V from the image and the GF1's law, not a linear v / 64.

---------------------------------------------------------------------------------------------------------------

## 6. The timer: tempo and screen sync (0x410740..0x410f00)

### 6.1 State

```
[0x8231d6] system count   [0x8231da] playCount   [0x8231de] P (player period)   [0x823236] scrCount
[0x82323a] S = (950 * sync / 1000) >> 1 = 16,173   [0x82323e] Rm = (sync >> 1) - S = 851   [0x82325a] mode (2 refresh,
1 player)   [0x823226] device playing   [0x82322a] in player   [0x823256] tempo flag (0 for the GUS)
sync = [0x825c60] = 34,049 (tmrGetScrSync: PIT mode 3 counts by 2, so sync = 2 x clocks per refresh)
```
P: tmrPlaySD sets 0x5d37 (23,863: 50 Hz); tmrSetUpdRate 0x410e21(rate) sets P = 0x71ca5f0 / rate (119,318,000 /
(40 · BPM)) for a tempo-polled device; the GMP calls it on every tempo command and at midasPlayModule.

### 6.2 The algorithm (PIT one-shot in mode 0; handlers 0x410843 and 0x41092c)

```
setNext(t):  if (playCount < 0) { playCount = 10; mode = 1; PIT = 10 }
             else if (playCount < scrCount) { mode = 1; PIT = playCount }
             else { if (scrCount < 0) scrCount = 10; mode = 2; PIT = scrCount }       // fires at t + PIT
tmrSyncScr:  scrCount = S; mode = 2; wait for a retrace; PIT = S (playCount keeps its value, 23,863)
refresh interrupt (mode 2): wait for the end of a retrace if in one; preVR (frame counter + 1);
             playCount -= scrCount + Rm;  scrCount = S;  wait for the retrace start → t;  setNext(t);  EOI;  inVR
player interrupt (mode 1) at t: scrCount -= playCount; playCount = P; setNext(t); EOI (interrupts on);
             SD.StartPlay(); SD.Play(&callMP) (5.4); if (callMP) gmpPlay();
             if (P changed during the tick) { playCount = P; if (mode == 1) setNext(t); }
```
In program time: the refresh interrupt comes S clocks after each retrace start, i.e. at line 389.5 of the next refresh
(always before the retrace, never coinciding with a player interrupt: the PIT is not armed while the handler waits);
player interrupts are counted from the last retrace or the last player interrupt. Every refresh is accounted as
scrCount + Rm = 17,024 clocks (the real refresh is 17,024.67), and a tick whose count runs out between a refresh
interrupt and the retrace after it (an 851-clock window) is taken 10 clocks after that retrace: its lateness is not
made up. In the full run 875 of the 25,252 player interrupts start late (by 847 clocks at most). Mean tick periods:
tempo 122: 24,472.2 clocks (P 24,450), 186: 16,078.6 (16,037), 202: 14,803.9 (14,767), 125: 23,873.3 (23,863).

The emulator runs handlers in no time except their retrace waits, so these times are exact for the capture; on real
hardware the GUS device's ramp waits (5.6) run inside the player interrupt with interrupts enabled: the refresh
interrupt can nest there (it does not look at the "in player" flag), a player interrupt nested in the player only does
the accounting (0x410935) and its tick is lost. Neither happens in the capture.

### 6.3 Start of each part

Both runs: tmrSyncScr waits for the retrace of refresh 4 (part II of the full run: refresh 14448) with playCount =
23,863, and the first refresh interrupt is interrupt 1 (30268). midasPlayModule runs after interrupt 31 (full) /
17 (part2) / 30284 (full, part II): from then on the GMP ticks at every player interrupt; P = 24,450 (part I) /
23,863 (part II) from the next tick. The first player interrupts of part I come at refresh 5 + 6,839 clocks, refresh
6 + 13,678, refresh 8 + 3,493, … (`timeline.py`). The GMP's first tick: interrupt 33 (refresh 24.542, program time
0.350 s); its first row: interrupt 48 (refresh 33.159, 0.473 s). Part II: first tick interrupt 30286 (refresh
14460.131), first row interrupt 30298 (refresh 14467.140, 206.419 s); part2 run: interrupt 19 / 31 (refresh 16.131 /
23.140).

### 6.4 For the remake

Run this state machine in integers (PIT clocks within a refresh, plus the refresh index) from the remake's
equivalent of tmrSyncScr and of the interrupt that starts the module, run a GMP tick at each player interrupt, and
convert to seconds with the remake's refresh clock (70.086 Hz, FRAME = 17,024.67 clocks). The tick times are then
those of the capture to the PIT clock. Using P alone would run part I about 0.5 s ahead by its end.

### 6.5 The position the demo reads

0x400104 (called by WaitSYNC and the effects) calls 0x40de94, which fills the info: +0 position, +4 pattern, +8 row
(the values at the start of the last playRow, i.e. the row now playing), +0xc tempo, +0x10 speed, +0x14 sync (−1);
it copies them to [0x825c48], [0x825c4c], [0x825c50], [0x825c54]. The `.ticks` order / row columns are those copies:
they lag the GMP when the main line has not called 0x400104 since the row changed: 131 records of the full run,
mostly by 1 to 2 refreshes (a frame that took longer to draw), up to 5.2 refreshes while an effect initialises (records
23631..23634), and at part II's start the copy keeps part I's 60:63 for 5 records.

---------------------------------------------------------------------------------------------------------------

## 7. The register stream (for checking a port against `full.gus`)

| stamps | writes |
|---|---|
| 0 | Detect ×2 and Init: r4c 0, r4c 1 (×3), p220 3, r0e df, 32 × (r00 3, r09 500, r0c 8, r0d 3, r06 3f) for voices 0, 31 .. 1, g r4c 3, p220 0 |
| 31 | OpenChannels(16): v1 r0e cf (voice 1 is still selected), v0 .. v15 r0c 8, p220 0 |
| 84 .. 29796 | part I's music (first note: voice 8, pattern 17 row 2) |
| 30267 | CloseChannels (16 × r00 3, r00 3, r0c 8), Close (r4c 0, r4c 1, p220 0), Detect, Init (as at stamp 0, r0e logged with voice 15) |
| 30284 | OpenChannels(20): v1 r0e d3, v0 .. v19 r0c 8, p220 0 |
| 30300 .. | part II's music |
| 52937 | CloseChannels (20 voices), Close |

Every music write is made by Play (5.4) at the player interrupt after the tick that changed the voice; the stamp is
that interrupt's number.

---------------------------------------------------------------------------------------------------------------

## 8. GF1 model, render, and the remake's gf1

`render.py full runs/full.wav` (394.75 s at 44,100 Hz from the record where frames2mp4.py's video starts; `--all`
from program time 0): the port with a GF1 model that answers reads as a card (5.6), rendered at the card's frame rate
per part (38,587.5 / 30,870 Hz, from 617,400 / voices) and resampled linearly. GF1 model:
`inside/re/models/music/gf1.py`'s (clone's mxmplay.py) with bidirectional loops as `inside/remake/src/gf1.cpp` does
them. The mix peaks at 1.03 of one
full-scale voice. Levels per second look like the music (silence only at the part switch, 203..206 s).

`inside/remake/src/gf1.h` / `gf1.cpp` (`gus::Card`, `gus::Hardware`, `gus::Synth`) cover what MIDAS's device uses:
8-bit voices, one-shots, forward and **bidirectional** loops, ramps, regs 0x00..0x0e, 0x4c, the 0x89 / 0x8d reads, the
log format of this capture (base 0x220, "g r%02x", no 0x43 / 0x44 lines, "p220"). What differs from Inside's use:

1. **The ramp wait**: MIDAS waits for the ramps of *all* starting voices, without bound (Inside's DSMI polls the
   selected voice 20 times; `gus::Hardware::waitVoice`): each pass of 5.4's loop reads reg 0x8d of every voice still
   to start and starts those whose ramp has ended, so voices start one pass apart; the card runs during a pass (one
   frame in `render.py`). Clone's `gf1::Hardware::waitRamps` (run until every ramp has stopped) is the nearest
   existing primitive; a per-pass `run(1)` between the device's polls is what MIDAS does.
2. **The upload**: poke `byte ^ 0x80` (the GMP's unsigned bytes) at the heap's addresses, plus the padding (5.5); the
   memory test bytes 0x55 / 0xaa at 0, 1, 0x40000, 0x40001, … only matter for a DRAM check.
3. Reg 0x0e with 16 and 20 voices; the frame rate follows it (gf1 already does).
4. Mono: no reg 0x0c writes after OpenChannels (pan 8 for every voice).

`inside/remake/src/amp.*` (AMP 2.00) and `gusdrv.*` (DSMI's SDIGUS.DSD) do not apply: MIDAS needs its own player (the
GMP of section 4, with the XM loader of section 3) and its own device port (section 5) that writes through
`gus::Card`'s `select` / `w8` / `w16` / `r8` / `r16` and pokes the DRAM; the timer of section 6 calls the device's Play
and then the GMP at each player interrupt. Only the gf1 layer is shared.

---------------------------------------------------------------------------------------------------------------

## 9. Porting notes for the remake

- Read at run time: EXPRMNT1.XM, EXPRMNT2.XM (the original directory, as the demo does); from VIVID.EXE's image the
  XM effect table 0x8238e4 (36 bytes) and the Exy table 0x823908 (16 bytes), the Amiga period table 0x822280 (96
  dwords; the index k · 8 + trunc(finetune / 16) ± 1 reaches −9 .. 96, i.e. 0x82225c .. 0x822403), the linear
  frequency table 0x822430 (768 dwords), the vibrato sine 0x824c30 (32 bytes), the GUS output rates 0x824452 (19
  words) and the GUS volume table 0x824478 (65 words). Code constants: 0x1e00, 64, 768, 0xda7790, 0x39 / 0xd600,
  0x40, 0x10000, 40, 0x71ca5f0, 0x3b6 / 1000, 0x5d37, 10, 0x15, 0x3f, 0x500, 0x40 (heap), 1024.
- Port `midas.py`'s loader, GMP (XM branch) and GUS device routine by routine; feed the device's writes to
  `gus::Hardware` (reads, the ramp wait) and `gus::Synth` (output); drive it from the timer of section 6.
- Keep: the table-driven periods and frequencies, the FC rounding, the shared info memory and the other quirks of
  4.5, the truncated envelope interpolation, mono, the 0x15xx ramps. Render at 48 kHz with gus::Synth's scaled
  increments (same pitch and ramp durations, less aliasing than the card's 30.9 / 38.6 kHz).
- Start the parts as the demo does: the module plays from the interrupt after its loading; the remake picks that
  instant (in the capture: after 18 refresh and 13 player interrupts from tmrSyncScr's retrace in part I, 10 and 7 in
  part II); everything after follows from section 6.
- **Known differences** to list: the capture's card answers (5.6) give different register streams from a real card
  (the remake should model the real card: ramps from the actual volume, the ramp wait); the wait's duration per pass
  is a guess.

---------------------------------------------------------------------------------------------------------------

## 10. Problems found, open points

1. **The capture's card does not model volume reads or ramps** (`run_vivid.py`'s Gus: reg 0x89 = last reg 9 write),
   so every volume change in `full.gus` ramps from 0x0500. That is what the capture's program saw; the port's
   `answers='capture'` reproduces it, `render.py` uses a card model. Not fixed (read-only).
2. The GUS device's busy wait for ramps (5.4) has no bound: on a slow card or with many simultaneous note starts the
   player interrupt can run long enough for a refresh interrupt to nest in it (handled) or for a player interrupt to
   be swallowed (its tick lost). Not in the capture; a real-hardware question.
3. Multi retrig (0x41eeb8) is not ported (no song uses R). The other unplayed commands are ported but unchecked.
4. The pan law and volume curve of the GF1 model are clone's / JIZZ's (not measured); with the mono output only the
   pan-8 balance matters.
5. NOTES.md "Main" could say that main forces mono output (0x4002e0) and that the music position read by 0x400104
   also returns MIDAS's sync value, always −1 here (no W commands).
