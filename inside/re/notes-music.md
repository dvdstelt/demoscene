# Inside — music (scope "music": DSMI, AMP 2.00, EYE.AMF, the GUS driver SDIGUS.DSD)

Addresses are linear addresses of the running program (INSIDE.EXE's image at 0x400000: address A is at offset
A − 0x400000 of `unpacked/inside_img.bin`); the GUS driver's addresses are where the capture loaded it (heap,
0x1152b90..; section 7.1). "Interrupt i" = the i-th timer interrupt (the stamp of `full.gus`, the `irqs` column of
`full.ticks`); "callback k" = the k-th time the music part of the timer callback ran. PIT clock 1,193,182 Hz; refresh
= 70.086 Hz (`FRAME` = 17,024.67 PIT clocks); "record f" = record f of `full.frames` / `full.ticks` (line 0 of
refresh f).

Everything below was checked, not only read (scripts in `models/music/`, see its README):

- `player.py` plays EYE.AMF through a routine-by-routine port of AMP 2.00 and of the GUS driver (`dsmi.py`) and
  reproduces **every GUS register write of `full.gus`** (140,095 lines: the 1,822 start-up writes in the same order
  with the same values, the 138,273 writes of the music and of the exit with the same values and the same interrupt
  stamps) and the **order / row columns of all 13,044 records of `full.ticks`** that have a music position;
- `statecheck.py`: AMP's globals, the 4 channel records with their sample records, DSMI time and the driver's 24 voice
  records (214 fields) equal the snapshots' in all 10 snapshots f38 .. f11276;
- the uploaded sample data and its DRAM layout equal `full.dram` (666,132 bytes, all 31 samples; but see 13.1: a real
  GUS stores them with the MSB inverted);
- `timeline.py --check`: the closed form of section 9 gives the interrupt of all 9,510 player ticks of the capture;
- `render.py` renders the song through a GF1 model (copied from clone's `mxmplay.py`) driven by the same port, with
  the reads answered as hardware answers them. There is no audio capture to compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- **Music system**: DSMI/32 by Otto Chrons: AMP 2.00 ("AMP 2.00 - (C) 1992,1994 Otto Chrons" at 0x40c788), its CDI
  channel layer, DSMI's timer service, the dynamic-driver loader (DDS) and the GUS driver from `SDIGUS.DSD` ("Gravis
  UltraSound GF1 sound driver 1.0 (C) 1995 Virtual Visions Finland Oy, Otto Chrons"), named by `SOUND.CFG` (GUS at
  0x220, DMA 1, IRQ 11). The demo drives DSMI from its own retrace timer.
- **EYE.AMF**: DSMI AMF version 14, "eyegabo0om.demo", 4 channels panned L R R L (−63, 63, 63, −63), 37 orders of 64
  rows, 140 tracks (134 stored), 31 instruments with 31 8-bit unsigned samples (666,132 bytes). Speed 6 / tempo 125
  in the header; the first row sets **speed 4, tempo 130**; order 36 sets speed 12. Effects used: instrument,
  speed, volume slide, set volume, tone portamento, sample offset, fine volume slide, note delay, tempo.
- **Time**: the timer callback adds 35 to DSMI time at each of the 4 interrupts per refresh (140 per refresh, 9,812
  per second; DSMI means 10,000); AMP runs a player tick whenever DSMI time − last tick > 10000 / (tempo · 50 / 125)
  = **192 units** at tempo 130 (200 at 125): 1.3714 refreshes, 51.10 ticks per second; a row (4 ticks) is 78.27 ms.
  The song plays 1.9 % slower than DSMI intends, locked to the screen refresh. Closed form in section 9.
- **Start**: the first music callback is interrupt 91 (refresh 37.64 of the capture); the first row is read at the
  6th tick (AMP's tick counter starts at 0 with speed 6), interrupt 125 = refresh 46.16 = 0.659 s after record 0.
- **One lost callback**: the credits' frame routine 0x406794 sets [0x42278c] around 0x4061fc, and the whole timer
  callback (frame clock, part ticks and music) is skipped while it is set. In the capture this hit one interrupt,
  12117 (between records 3044 and 3045): the music after it is one interrupt later. CPU-speed dependent.
- **GUS**: 24 active voices (GF1 at 617,400 / 24 = **25,725 Hz**), channels 0..3 on voices 0..3. Every volume change
  is a ramp (rate 0x3f: 63 twelve-bit steps per frame, full range in 2.5 ms) to an 8-bit target from a 65-entry
  volume table in the driver; a note start ramps the old note down, stops the voice before the ramp can finish (the
  wait polls at most 20 times), restarts it at volume 0x1770 and ramps up. Pitch = FC = 2 · round(freq · 512 /
  25,725) (steps of 50 Hz). Samples are uploaded by DMA with "invert MSB" (unsigned → signed).
- **For the remake**: port `dsmi.py` (AMP + driver) and drive the clone remake's `gf1::Hardware` / `gf1::Synth` with
  its writes; read EYE.AMF, four AMP tables from INSIDE.EXE and the 65-entry volume table from SDIGUS.DSD (file
  offset 0x700c; not a formula I could find). What gf1 lacks: section 11.

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when (capture) | what |
|---|---|
| init 0x4007c0 (frame 15, before the precalc) | 0x40bd48(4): retrace timer installed (IRQ0 → 0x40becf, callback [0x4b8f04] still 0) |
| after the precalc: 0x4010b4, main line | 0x40e900(0x28), 0x40e9b0 (DDS: function registry "DDSRegisterFunction", "MemAlloc", ..), 0x40f5a1(0x24) (timer service: step 36, time 0; registers GetDSMITime / LockDSMITime / UnlockDSMITime), exports MCPSampleRealAddress / MCPEnableVirtualSamples / MCPDisableVirtualSamples (stubs 0x400eb0 / 0x400eb8), 0x40f6cc (CDI init, registers cdiRegister) |
| | 0x400efc: reads `SOUND.CFG` (13-byte driver name "SDIGUS.DSD", then a 0x5c-byte config to 0x4249fc: +0 device 0x0a (GUS), +8 port 0x220, +0xc DMA 1, +0x10 IRQ 11, +0x3c 0 = use DMA for uploads), exports GetEnvString / SetVector, loads the driver (0x40ef10), looks up SDIInitDevice / SDIStartOutput / SDIStopOutput ([0x4249f4], [0x4249f8], [0x4227a8]) and calls SDIInitDevice(config): **GF1 reset, stamp 86** (sec. 7.3); on failure "Error loading sound driver. Please run 'setup.exe' first" |
| | SDIStartOutput (0x1154a0e: returns 0); ampInit 0x40d907(0); cdi +0x54 setupChannels(24) (stamp 86); 0x4103d0 loads `eye.amf` ("Error in loading module 'eye.amf'. Please configure" otherwise) |
| | ampPlayModule(module, 1) 0x40dbb9: pans (stamp 87), order 0, **sample uploads (stamps 87..90)**, playing; then [0x4227a0] = 1 (music on) |
| 0x401010 (main line) | DSMI step = trunc(4,085 × 2,500 × 3.48092453e−6) = trunc(35.55) = **35** (PIT period [0x4b8f00] = 4,085) |
| end of init | [0x4b8f04] = 0x40070c: the callback runs from interrupt 91 on |
| every interrupt from 91 | 0x40070c → 0x400fcc: DSMI time += 35; AMP's poll runs the ticks that are due (sec. 4, 5.3) |
| record 38 on | the script (core scope) reads the position with 0x40de09 / 0x40de21 after each drawn frame |
| exit 0x400a10 → 0x401054 (stamp 52262) | ampStopModule (4 × stopNote), free the module, SDIStopOutput (24 × clear voice), unload the driver |

The script entry 0:00 runs at the first frame end after ampInit (record 38), because the position reads (0, 0)
before the first row is read: the intro starts 8.5 refreshes before row 0:00 sounds (interrupt 125).

---------------------------------------------------------------------------------------------------------------

## 2. EYE.AMF and the AMF loader

### 2.1 File (677,491 bytes; `models/music/amf.py`)

All little endian. Offsets of EYE.AMF:

| offset | size | what |
|---|---|---|
| 0x00 | 4 | 'AMF', version 0x0e |
| 0x04 | 32 | title "eyegabo0om.demo" |
| 0x24 | 1, 1, 2 | instruments 31, orders 37, tracks 140 |
| 0x28 | 1 | channels 4 |
| 0x29 | 32 | channel pan, signed: −63, 63, 63, −63, 0 … (100 = surround) |
| 0x49 | 1, 1 | tempo 125, speed 6 |
| 0x4b | 37 × 10 | orders: word rows (64), 4 × word track number (1-based) |
| 0x1bd | 31 × 65 | instruments: +0 type (1), +1 name[32], +0x21 file name[13], +0x2e dword sample index (1-based), +0x32 dword length, +0x36 word rate (Hz at note 60), +0x38 byte volume 0..64, +0x39 dword loop start, +0x3d dword loop end (0 = no loop) |
| 0x99c | 140 × 2 | track map: track number − 1 → stored track (1-based) |
| 0xab4 | | 134 stored tracks: word events n, byte type (ignored), n × {byte row, byte command, byte value}; the last event of every track is {0xff, 0xff, 0xff}; stored track 7 has 0 events (used by 7 track numbers: silent) |
| 0x2c5f | 666,132 | sample data, 8-bit **unsigned**, by sample index 1..31 (offsets in section 3) |
| 0xa5673 | | end of file |

Events: command 0x00..0x7f = note (value = volume 0..64, 0xff = keep; note 0 = note off; note 0x7f = keep the
note), 0x80..0x97 = effect (command & 0x7f indexes AMP's jump table, section 5.6; 0x80 = instrument, value = 0-based
instrument).

### 2.2 The loader (0x4103d0 → 0x40fcc8, 0x40fc60; 0x40fb20 frees)

```
int ampLoadAMF(MODULEINFO **out, int opt, char *name)          // 0x4103d0 (pascal: name pushed last)
  mi = calloc(0x5c); f = open(name)                            // -2: no file
  read 4: "AMF" + version, else -3;  mi->type = 3;  read 20 bytes of the title to +9 (+0x1d = 0)
  if (version == 1 || version == 8) { mi->channels = 4; pans from 0x423808 (4 bytes) }
  r = 0x40fcc8(mi, f); if (r) { free everything; return r }
  mi->size(+5) = file length; *out = mi; return 0

0x40fcc8: mi->tempo = 125; mi->speed = 6
  seek 0; read 4; read 32 → title (+9)
  pans = version >= 0x0c ? 32 : 16;  old instrument format if version < 0x0a; version 1: 3 extra bytes per track
  read instruments (+0x4a), orders (+0x4f), tracks word (+0x54)
  if version >= 9: read channels (+0x29), pans bytes (+0x2a); version < 0x0b: + 16 default pans from 0x423808
  if version >= 0x0d: read tempo (+0x5a), speed (+0x5b)
  orders = calloc(orders, 0x82) (+0x50); instruments = calloc(instruments, 0x41) (+0x4b);
  tracks = calloc(tracks + 4, 4) (+0x56)
  for each order: rows = version >= 0x0e ? read word : 64; for each channel: read word track number into +2 + 4 ch
  for each instrument: read 0x41 bytes verbatim (old format: 0x3b bytes converted)
      maxindex = max(index); if (count < index) sizes[count++] = (u16) length    // lengths read as words
  read the track map (tracks words); maxtrack = max(map)
  table[1..tracks] = 0
  for t in 0..maxtrack-1: n = read word; read byte
      p = n ? malloc(3 n + 6) : 0; if p { p[0..1] = n; p[2] = 0; read 3 n (+3 for version 1) bytes to p + 3 }
      for every j with map[j] == t + 1: table[j + 1] = p
  for s in 0..maxindex-1: if sizes[s]: buf = malloc(sizes[s] + 0x10); read sizes[s] bytes;
      every instrument with index == s + 1 (and index < 0x10000) gets +0x2e = buf          // raw, no conversion
  instruments whose +0x2e still is a small number get 0
  0x40fc60: order.track[ch] (dword at +2 + 4 ch) = table[track number]
```

No EMS here ("EMSAlloc" / "EMSCopyTo" are not registered). The sample bytes stay unsigned in memory; the driver's
DMA inverts the MSB on the card (section 7.5).

### 2.3 In memory

MODULEINFO (0x5c bytes; the capture's at 0x11528f0, [0x4227a4]; AMP copies it to 0x4229f0): +0 type 3, +1 size,
+5 file size, +9 title, +0x29 channels, +0x2a pan[32], +0x4a instruments, +0x4b instrument array, +0x4f orders,
+0x50 order array (0x82 bytes each: word rows, 32 track pointers), +0x54 tracks, +0x56 track pointer table, +0x5a
tempo, +0x5b speed. A track pointer points at {word n, byte 0} followed by the events; AMP starts reading at +3.

---------------------------------------------------------------------------------------------------------------

## 3. The song

37 orders, all 64 rows, 4 channels. Orders → track numbers (stored track = the same number, except the empty one):

```
 0: 1 2 3 4      1: 5 6 7 8      2: 9 10 11 12   3: 13 14 15 16  4: 17 18 19 20  5: 21 22 23 24  6: 25 26 27 28
 7: 29 30 31 32  8: 25 26 27 28  9: 33 34 35 36 10: 37 38 39 40 11: 41 42 43 44 12: 105..108   13: 109..112
14: 81..84      15: 85..88      16: 45..48      17: 49..52      18: 45..48      19: 53..56      20: 57..60
21: 125..128    22: 61..64      23: 93..96      24: 65..68      25: 89..92      26: 97..100     27: 69..72
28: 73..76      29: 101..104    30: 77..80      31: 113..116    32: 129..132    33: 133..136    34: 121..124
35: 117..120    36: 137..140
```

Global effects: 0:00 channel 0 speed 4, channel 2 tempo 130; 36:00 channel 0 speed 12; 36:63 tempo 0 (ignored).
Effects (event counts in the file): instrument 789, speed 2, volume slide 205 (values +1, +2, +15, −8, −6, −4,
−2, −1 per tick), set volume 106, tone porta 19 (7, 8, 9, 10, 4: 29:44..47, 31:44..47 and 54..56, 33:44..47,
34:44..47, channel 3), sample offset 43, fine volume slide 155 (−2, −1), note delay 31 (1 or 2 ticks), tempo 2.
Notes 1,252 (notes 66..80; 72 = rate × 2, 79 = rate × 3), plus 2 × note 0x7f; no note-offs.

Instruments (EYE.AMF file offset of the sample data; DRAM address in the capture; loops in sample bytes):

| # | name | data at | length | rate | vol | loop | DRAM |
|---|---|---|---|---|---|---|---|
| 0 | helloAfrica&allniggas | 0x2c5f | 20264 | 8130 | 48 | – | 0x02020 |
| 1 | Africa.niggeria | 0x7b87 | 22936 | 8368 | 48 | – | 0x06f80 |
| 2 | Bass.L | 0xd51f | 45878 | 8368 | 64 | 36210..45878 | 0x0c940 |
| 3 | Bd1.11 | 0x18855 | 10800 | 8368 | 32 | – | 0x17ca0 |
| 4 | Bd1.14 | 0x1b285 | 9238 | 8368 | 32 | – | 0x1a700 |
| 5 | Bd1.15 | 0x1d69b | 10392 | 8368 | 32 | – | 0x1cb40 |
| 6 | Bd1.15 | 0x1ff33 | 9268 | 8368 | 32 | – | 0x1f400 |
| 7 | Bd1.61 | 0x22367 | 10556 | 8368 | 32 | – | 0x21860 |
| 8 | Bd1.4 | 0x24ca3 | 10336 | 8368 | 32 | – | 0x241c0 |
| 9 | niggah | 0x27503 | 21770 | 8368 | 16 | – | 0x26a60 |
| 10 | Iceman.h | 0x2ca0d | 65430 | 8368 | 64 | 61984..65430 | 0x2bfa0 |
| 11 | Klop1 | 0x3c9a3 | 11318 | 8368 | 32 | – | 0x3bf60 |
| 12 | Klop11 | 0x3f5d9 | 10958 | 8368 | 32 | – | 0x40000 |
| 13 | Klop3 | 0x420a7 | 11104 | 8368 | 32 | – | 0x42b00 |
| 14 | Klop6 | 0x44c07 | 11100 | 8368 | 32 | – | 0x456a0 |
| 15 | Klop7 | 0x47763 | 11258 | 8368 | 32 | – | 0x48220 |
| 16 | MBasa3 | 0x4a35d | 24036 | 8368 | 48 | – | 0x4ae40 |
| 17 | MPad.L | 0x50141 | 65116 | 8368 | 43 | 48732..65116 | 0x50c60 |
| 18 | (no name) | 0x5ff9d | 19868 | 8368 | 37 | – | 0x60ae0 |
| 19 | Multiclish | 0x64d39 | 13116 | 8609 | 48 | – | 0x658a0 |
| 20 | Multiklap | 0x68075 | 7556 | 8368 | 48 | – | 0x68c00 |
| 21 | Vox2 | 0x69df9 | 31460 | 8368 | 32 | – | 0x6a9c0 |
| 22 | WetBass.dist | 0x718dd | 65116 | 8368 | 48 | – | 0x80000 |
| 23 | amb_vinyl_e_3 | 0x81739 | 20264 | 8130 | 64 | – | 0x724e0 |
| 24 | c-beatt-4 | 0x86661 | 11324 | 8130 | 64 | – | 0x77440 |
| 25 | amb_vinyl_e_3 | 0x8929d | 7216 | 8130 | 64 | – | 0x7a0a0 |
| 26 | amb_vinyl_e_1 | 0x8aecd | 7554 | 8130 | 64 | – | 0x7bd00 |
| 27 | amb_vinyl_e_3 | 0x8cc4f | 7084 | 8186 | 64 | – | 0x7dac0 |
| 28 | (no name) | 0x8e7fb | 26868 | 8368 | 32 | – | 0x8fe80 |
| 29 | (no name) | 0x950ef | 30180 | 8368 | 24 | 13942..30180 | 0x967a0 |
| 30 | mikko feat jarno-seta | 0x9c6d3 | 36768 | 8368 | 24 | 19710..36768 | 0x9ddc0 |

Song length: 9,989 player ticks (5 lead-in ticks, 36 × 64 rows × 4, 64 × 12); tick 9,990 (DSMI time 1,918,128,
13,700.9 refreshes = 195.5 s after the first callback) would read order 0 again (the song loops: ampPlayModule's
option 1). The demo exits before that, at 36:24 (stamp 52262).

---------------------------------------------------------------------------------------------------------------

## 4. DSMI time and the timer callback

Timer service (0x40f5a1..0x40f6cb; [0x4b9148] time, [0x4b9144] step, [0x4b9140] lock count, [0x4b913c] locked
time):

```
tsInit(step) 0x40f5a1:     step = step; time = 0; locks = 0; export GetDSMITime, LockDSMITime, UnlockDSMITime
tsTick() 0x40f620:         time += step                    (0x40f63d is the same as an interrupt handler: unused)
tsSetStep(s) 0x40f65f:     step = s                        (0x4010b4: 36; 0x401010: 35)
GetDSMITime() 0x40f5f9:    return locks ? locked : time
LockDSMITime() 0x40f67c:   if (!locks) locked = time; locks++
UnlockDSMITime() 0x40f6a7: if (locks) locks--
```

The demo's timer callback 0x40070c (every interrupt; the core scope has the rest):

```
if ([0x42278c]) return;          // set only by the credits' frame routine 0x406794 around 0x4061fc: the whole
                                 // callback is lost (in the capture once: interrupt 12117)
[0x4248c4]++; [0x4248c8]++;      // part ticks
music 0x400fcc:
    if (!musicOn [0x4227a0]) return;
    tsTick();                                        // DSMI time += 35
    if ([0x4227ac] || driverError [0x422784]) return;
    [0x4227ac]++; cdiPoll(0) (CDI +0x5c: the GUS driver's returns 0); ampPoll 0x40d878; [0x4227ac]--;
... frame clock, palette, screen copy, the part's tick routine (only on the retrace interrupt)
```

So DSMI time = 35 × callbacks, 140 per refresh. The step is 35 for any timer period [0x4b8f00] of 4,022..4,136
PIT clocks (0.24 of a 69.2..71.2 Hz refresh).

---------------------------------------------------------------------------------------------------------------

## 5. AMP 2.00 (0x40c788..0x40e03e)

Hand-written assembler. API routines are pascal style (arguments pushed left to right, the callee pops); internal
routines take the channel's record offset in BX/EBX and the event pointer in EDI.

### 5.1 Data

Tables in INSIDE.EXE (read them at run time):

| address | what |
|---|---|
| 0x40c7ae | 32 words: channel record offsets 0x2d · ch |
| 0x40c7ee | 32 words: sample record offsets 0x1a · ch |
| 0x40c82e | **semitone** table, 132 words: frequency multiplier << 10 per note (note 60 = 1023, 72 = 2048, 84 = 4096 .. 120 = 32768, then 32768) |
| 0x40c936 | **period** table, 127 words: note 0..24 = 0xd5ff, 25 = 51709 .. 60 = 6848 (0x1ac0) .. 126 = 151 |
| 0x40ca34 | **sine**, 64 words: floor(4095 · sin(π i / 64)) (vibrato / tremolo use entries 0..31 only) |
| 0x40cab4 | **arpeggio**, 16 words: ≈ 1024 · 2^(n / 12) (rounded, except n = 14, 15: 2298, 2436) |
| 0x40cad4 | effect jump table, 25 dwords (command & 0x7f; index 0x18 points into code: never used) |
| 0x40d70b | per-tick table: 12 × {word command, dword handler}; the row tick uses the last 2 (0x40d747) |

Globals: [0x422a56] init option, [0x422a58] status (bit 0 playing, bit 1 paused), [0x422a59] play option (bit 0 =
loop), [0x422a5b] first order, [0x422a5c] end order (= number of orders), [0x422a5d] **order**, [0x422a5e] current
channel, [0x422a5f] **next row**, [0x422a61] **row** (the row read last), [0x422a63] **tick** in the row,
[0x422a64] order array, [0x422a68] instrument array, [0x422a6c] speed, [0x422a6d] tempo, [0x422a6f] sync byte,
[0x422a70] break flag, [0x422a75] channels, [0x422a77..] pan copy, [0x422a97] rows of the order, [0x423379 + ch]
channel volume (0x40), [0x423399] bit 0 = initialised, [0x4233a6] / [0x4233a7] poll guards, [0x4233a8] tempo
divisor = tempo · 50 / 125, [0x4233aa] DSMI time of the last tick. [0x422a56..0x422a98] and the channel records are
cleared by ampInit.

Channel record (0x422a99 + 0x2d · ch): +0 track pointer, +4 word read offset (3 + 3 · event; 0xffff = end), +6 word
flags (bit 1 muted by ampMuteChannel), +8 note, +9 instrument, +0xa volume 0..64, +0xb word ticks since the note
(saturating), +0xd word period, +0xf command running per tick, +0x10 its value, +0x11 word tone-porta target, +0x13
tone-porta step (signed), +0x14 last tone-porta value, +0x15 last porta value (signed), +0x16 / +0x1a / +0x1e
arpeggio frequencies, +0x23 vibrato position, +0x24 vibrato parameter, +0x25 tremolo parameter, +0x26 tremolo
speed, +0x27 tremolo depth, +0x28 tremolo position, +0x29 last sample offset, +0x2c pan.

Sample record (0x423039 + 0x1a · ch; the structure passed to the driver's setSample): +0 sample handle (the
instrument's +0x2e: its address in memory), +4 length, +8 loop start, +0xc loop end, +0x10 byte 1, +0x11 word 0,
+0x13 word rate, +0x15 dword **frequency** (Hz), +0x19 volume.

### 5.2 API (pascal; only ampInit, ampPlayModule, ampStopModule and the two getters are used)

```
ampInit(opt) 0x40d907:  clear globals and channels; [0x422a56] = opt; initialised = 1; guards = 0; div = 50;
                        last = GetDSMITime(); ampSetMasterVolume(-1, 0x40) 0x40e00f (all 32 channel volumes)
ampPlayModule(mi, opt) 0x40dbb9: return mi->type ? ampPlay(mi, 0, mi->orders, opt) : -1
ampPlay(mi, first, end, opt) 0x40d998:          // -1 / -2 errors: not initialised, bad orders, upload failed
    copy *mi (0x5c bytes) to 0x4229f0; status = 0; clear the 32 channel records
    break = 0; ch = 0; row = 0; sync = 0; next row = 0; speed = mi->speed; tempo = mi->tempo
    div = tempo * 50 / 125 (16-bit); first order = order = first; end order = end; option = opt
    for ch < channels: ampSetPanning(ch, mi->pan[ch])            // 0x40dfd2: record pan, CDI setPanning
    loadOrder()                                                  // 0x40d299
    for ch < channels: ampUnmuteChannel(ch)                      // CDI unmute (nothing), flags &= ~2
    for each instrument i: if (ptr = inst[i].+0x2e)               // in instrument order
        if (ptr >> 16 == 0xffff) virtual sample (MCPSampleRealAddress hook [0x4233ae]; not used)
        else if (CDI download(ptr data, ptr handle, inst[i].length)) { unload all; return -2 }
    status = 1; last = GetDSMITime(); return 0
ampStopModule() 0x40dc21: status &= ~3; for ch < channels: CDI stopNote(ch); unload every instrument's sample
ampGetPattern() 0x40de09: return initialised ? order : -1        (the order, not the pattern)
ampGetRow() 0x40de21:     return initialised ? row : -1          (the row read last)
also present, unused: 0x40d977 close, 0x40dbec play one order, 0x40dc89 pause, 0x40dcc9 resume, 0x40dd09 status,
0x40dd21 channel flags, 0x40dd54 mute, 0x40ddac unmute, 0x40de3a get sync (and clear), 0x40de59 get speed | tempo << 8,
0x40de77 set speed / tempo, 0x40debd channel info (10 bytes at 0x422a4c), 0x40df68 jump ±1 order,
0x40dfd2 set panning, 0x40e00f set channel volume (ch or -1 = all), 0x40d8da an IRQ entry that calls the poll.
```

### 5.3 The poll and the tick period

```
int period() 0x40dfab: return 10000 / div;              // div = (tempo * 50) / 125: 125 → 50 → 200; 130 → 52 → 192
ampPoll() 0x40d878:
    if (!initialised || inPoll [0x4233a6]) return -1;
    inPoll = 1; LockDSMITime();
    p = period(); e = GetDSMITime() - last;
    while ((e -= p) > 0) { tick(); last += p; p = period(); }     // a tick when time - last > period (strictly)
    inPoll = 0; UnlockDSMITime();
```

### 5.4 The tick (0x40d7c0) and the order change (0x40d299)

```
tick():
    if (!(status & 1) || (status & 2)) return;
    if (++tick >= speed) {                          // tick starts at 0 after ampInit, so row 0 is read at tick 6
        tick = 0;
        if (break || nextrow >= rows) { break = 0; order++; if (loadOrder()) return; }
        for ch < channels: readRow(ch);             // 0x40d1ac
        row = nextrow; nextrow++;
    }
    for ch < channels: tickEffect(ch);              // 0x40d753
    for ch < channels: if (++count == 0x10000) count = 0xffff;   // 0x40d7a1
loadOrder():
    nextrow = 0;
    if (order >= end) { if (!(option & 1)) { ampStopModule(); return -1; } order = first; }
    rows = orders[order].rows;
    for ch < channels: track = orders[order].track[ch]; readoffset = 3;
    return 0;
```

### 5.5 Reading a row (0x40d1ac), notes (0x40d0c7), effects (0x40d0a3)

```
readRow(ch):
    cur = ch; old = cmd; cmd = 0; val = 0;
    if (readoffset != 0xffff && track) for (p = track + readoffset;; ) {
        if (p[0] == 0xff) { readoffset = 0xffff; break; }        // end of track
        if (p[0] > nextrow) break;                               // signed 16-bit compare
        if (p[1] & 0x80) effect(p) else note(p);                 // a handler may consume p[3..5] (+3)
        p = track + (readoffset += 3);
    }
    if (old != cmd) CDI setFrequency(ch, sample.freq);           // ends arpeggio / vibrato, see "instrument"
    CDI setVolume(ch, scale(vol, ch));                           // every row, every channel
scale(v, ch) 0x40cb34: return v * chvol[ch] >> 6;               // chvol = 0x40: v

note(p):                                       // p = {row, note, volume}
    if (p[1] == 0) { vol = 0; CDI stopNote(ch); return; }
    if (p[1] != 0x7f) note = p[1];
    if (p[2] != 0xff) vol = p[2];
    count = 0; vibpos = 0;
    trigger: period = target = PERIOD[note];
             sample.freq = sample.rate * SEMITONE[note] >> 10;   // 32-bit product >> 10
             sample.vol = vol;
             if (!(flags & 2)) CDI playNote(ch, sample.freq, scale(vol, ch));

effect(p): k = p[1] & 0x7f; if (k <= 0x18) jump table 0x40cad4[k](p);    // p[3..5] = the next event
```

### 5.6 Row effects (jump table 0x40cad4; `per tick` = sets cmd, run by 5.7)

| cmd | k | handler | what it does on the row |
|---|---|---|---|
| 0x80 | 0 | 0x40cfce instrument | inst = v; readRow's `old` = (next event on this row is a note) ? 0 : 1; sample record = instrument v's {+0x2e handle, +0x32 length, +0x36 rate, +0x39 loop start, +0x3d loop end, +0x38 volume}, +0x10 = 1, +0x11 = 0; vol = instrument volume; CDI setSample(ch, &sample); CDI setVolume(ch, scale(vol)) |
| 0x81 | 1 | 0x40cf8e speed | if (v && v != speed) speed = v |
| 0x82 | 2 | 0x40cf1a volume slide | per tick, v signed |
| 0x83 | 3 | 0x40cf2e volume | vol = v; CDI setVolume(ch, scale(v)) |
| 0x84 | 4 | 0x40cec5 porta | s = v; v == 0x80: s = −\|last\|; v == 0: s = +\|last\|; last = s; per tick (+ = period up = pitch down) |
| 0x85 | 5 | 0x40cb51 | nothing |
| 0x86 | 6 | 0x40ce2a tone porta | s = v ? v : last; last = s; x = p[0] (the row byte: a bug); if the next event is a note on this row: note = p[4] (0x7f: keep), target = PERIOD[note], consume it, x = p[5], if (p[5] != 0xff) vol = p[5]; CDI setVolume(ch, scale(x)) (so the volume is briefly set to the row number, or 64 for 0xff; readRow's final setVolume replaces it in the same tick); step = target < period ? −s : s; per tick |
| 0x87 | 7 | 0x40cd27 tremolo | parameter nibbles replace the old ones if non-zero; speed = hi, depth = lo; per tick |
| 0x88 | 8 | 0x40cdae arpeggio | if v: arp = {f, ARPEGGIO[v >> 4] · f >> 10, ARPEGGIO[v & 15] · f >> 10}, f = sample.freq; per tick (row tick too) |
| 0x89 | 9 | 0x40cd75 vibrato | nibbles replace the old non-zero ones; per tick |
| 0x8a | 10 | 0x40cd13 | tone porta + volume slide, per tick |
| 0x8b | 11 | 0x40ccff | vibrato + volume slide, per tick |
| 0x8c | 12 | 0x40cf86 pattern break | break = 1 (always to row 0) |
| 0x8d | 13 | 0x40cf52 position jump | o = v − 1 (signed byte); ignored if o < order and !(option & 1), or o > end; order = o; break = 1 (plays order v) |
| 0x8e | 14 | 0x40ccf6 sync | sync = v |
| 0x8f | 15 | 0x40ccde retrig | if v: per tick (row tick too) |
| 0x90 | 16 | 0x40cc93 sample offset | cmd = 0x10 (no per-tick handler, but ends arpeggio etc.); h = v ? v : last; last = h; off = h << 8; if (off > sample.length) off = sample.loopstart; CDI setPosition(ch, off) |
| 0x91 | 17 | 0x40cc53 fine volume slide | cmd = 0x11; vol = clamp(s8(vol + v), 0, 64) (byte arithmetic: a sum ≥ 128 gives 0); CDI setVolume |
| 0x92 | 18 | 0x40cbe3 fine porta | cmd = 0x12; period += s8(v) · 16 (16-bit; < 0 or ≤ 0x64 → 0x64); freq = rate · 0x1ac0 / period; CDI setFrequency |
| 0x93 | 19 | 0x40cb9c note delay | val = v ? v : 1; only if the next event is a note on this row: note (≠ 0x7f), vol (≠ 0xff) from it, consume it, cmd = 0x13 |
| 0x94 | 20 | 0x40cb88 note cut | cmd = 0x14, val = v |
| 0x95 | 21 | 0x40cfa3 tempo | if (v && v != tempo) { tempo = v; div = v · 50 / 125 } |
| 0x96 | 22 | 0x40cf02 fine slide | if v: cmd = 0x16 (per tick, period += s8(v)) |
| 0x97 | 23 | 0x40cb52 panning | cmd = 0x17; v ≠ 100: clamp to −63..63; ampSetPanning(ch, v) |

### 5.7 Per-tick effects (0x40d753; on the row tick only arpeggio and retrig)

```
freqOf(period) = sample.rate * 0x1ac0 / period                    // 32-bit
2    volume slide 0x40d312: vol = clamp(s8(vol + val), 0, 64); setVolume(scale(vol))
4    porta 0x40d3a9: d = s8(val) << 4; if (!d) return;
                     d > 0: period += d (16-bit); d < 0: period = (-d > period || period + d <= 0x64) ? 0x64 : period + d;
                     if (period >= 0xd5ff) period = 0xd5ff; sample.freq = freqOf(period); setFrequency
6    tone porta 0x40d429: d = step << 4 (16-bit); dist = target - period (16-bit);
                     move by d unless |d| > |dist| (unsigned compares), then period = target (if target != 0);
                     sample.freq = freqOf(period); setFrequency
9    vibrato 0x40d4dd: a = (vib & 15) * SINE[(vibpos >> 2) & 31] >> 7; p = vibpos & 0x80 ? period - a : period + a;
                     if (p <= 0x64) p = 0x64; setFrequency(freqOf(p)); vibpos += (vib >> 2) & 0x3c
7    tremolo 0x40d56d: a = depth * SINE[(trempos >> 2) & 31] >> 10; v = trempos & 0x80 ? vol + a : max(vol - a, 0);
                     setVolume(scale(min(v, 64))); trempos += (speed << 2) & 0x3c
10   tone porta, then volume slide             11  vibrato, then volume slide
0x13 note delay 0x40d630: if (val == tick) { count = 0; trigger (5.5) }
0x14 note cut 0x40d6e2: if (val == 0 || val == tick) { vol = 0; setVolume(0) }
0x16 fine slide 0x40d345: p = period + s8(val) (16-bit); negative → 0x64; val 0 → nothing; ≤ 0x64 → 0x64; as 4
8    arpeggio 0x40d4b2: setFrequency(arp[tick % 3])
15   retrig 0x40d60d: if (tick % val == 0) setPosition(0)
```

Vibrato and tremolo use only the rising quarter of the sine table (index 0..31 of 64) with a sign from bit 7 of the
position: a "quarter-sine sawtooth" (an AMP quirk). Not used by EYE.AMF. Only the handlers EYE.AMF uses (5.3) are
confirmed by the capture; the others are ported from the code, unchecked.

---------------------------------------------------------------------------------------------------------------

## 6. The CDI layer (0x40f6cc..0x40fb17)

cdiInit 0x40f6cc: channel → device map 0x4233ba (256 bytes), device tables 0x4234ba (0x68 bytes each), count
[0x4237fa], magic [0x4233b8] = 0xda; exports cdiRegister 0x40f709 (copies a driver's 0x68-byte table; the GUS
driver registers 0x1155c1a and gets device 0, so all channels → the GUS). Each wrapper looks up the device of its
channel and calls table entry +n (pascal, channel first):

| wrapper | + | call | GUS driver |
|---|---|---|---|
| 0x40f776 | 0x00 | setSample(ch, sample *) | 0x1153576 |
| 0x40f79b | 0x04 | playNote(ch, freq, vol) | 0x1153716 |
| 0x40f7e7 | 0x08 | setVolume(ch, vol) | 0x1153831 |
| 0x40f80d | 0x0c | setFrequency(ch, freq) | 0x115388e |
| 0x40f833 | 0x10 | (setLinear) | 0x1153908: no write |
| 0x40f859 | 0x14 | setPosition(ch, offset) | 0x1153927 |
| 0x40f87f | 0x18 | setPanning(ch, pan) | 0x11539ef |
| 0x40f8a5 | 0x1c | setMasterVolume(vol) | 0x1153a63 |
| 0x40f8c8 / 0x40f8eb | 0x20 / 0x24 | mute(ch) / unmute(ch) | 0x1153ad2 / 0x1153b26 |
| 0x40f7c4 | 0x28 | stopNote(ch) | 0x1153b7b |
| 0x40f90e / 0x40f92e | 0x2c / 0x30 | mute all / unmute all | 0x1153be8 / 0x1153c2a |
| 0x40f94e | 0x34 | download(data, handle, length) | 0x1153df0 |
| 0x40f997 | 0x38 | unload(handle) | 0x1153ec5 (no GF1 write) |
| 0x40fa69 | 0x54 | setupChannels(n) | 0x115419b |
| 0x40faaf | 0x5c | poll | 0x115429c: returns 0 |
| others | 0x3c..0x50, 0x58, 0x60, 0x64 | unload all, get volume / frequency / position / pan / sample, status, rates | (unused) |

---------------------------------------------------------------------------------------------------------------

## 7. The GUS driver (SDIGUS.DSD)

### 7.1 Where it is

`SDIGUS.DSD` (32,768 bytes) is a DSMI dynamic driver: "DS" header, a 16-bit part, then the 32-bit image from file
offset 0x43f0 with its import names (DDSAccessResource .. GetEnvString, cdiRegister) and exports (SDIDetectDevices,
SDIQueryDeviceCount, SDIQueryDevice, SDIInitDevice, SDIStartOutput, SDIGetBufferPosition, SDIStopOutput,
SDICloseDevice). The DDS loader 0x40ef10 relocates it into a heap block; in the capture it is at 0x1152b90 (memory =
file offset + 0x114e7a0; snapshots: `models/music/snapdis.py`), code 0x1152bb5..0x11556c0, data 0x11557a0..,
config copy 0x11561ec, globals 0x11561a8..0x115628c. Compiled C (Watcom, frame pointers, register calling
convention inside, pascal at the CDI entries: `push esi, edi, ebp`; [ebp+0x10] = the last argument).

Driver globals: [0x11561e0] base 0x220, [0x11561d8] 3X2 voice select, [0x11561a8] 3X3 register select, [0x11561b4]
3X4 data (16-bit), [0x11561e4] 3X5 data high (8-bit), [0x11561c0] master volume 0x100, [0x11561dc] **active voices**,
[0x11561cc] DMA uploads on, [0x11561bc] DMA channel, [0x11561ac] IRQ, [0x1156248] sample table (400 × {handle,
DRAM address}), [0x11561b0] entries used, 0x1155fe8 **voice records** (32 × 14 bytes: +0 DRAM address of the sample,
+4 frequency, +8 volume, +0xc mode (8 = looped, 0), +0xd muted), [0x11557a0] DRAM block list.

### 7.2 Port helpers and the volume ramp

```
sel(v)        0x1152c0c: out 3X2 ← v
w8(r, b)      0x1152c30: cli; out 3X3 ← r; out 3X5 ← b; sti
w8x2(r, b)    0x1152c63: w8(r, b); delay(1); w8(r, b)          // delay(n) 0x1152bb5: 6 n reads of 3X7
w16(r, w)     0x1152cb6: out 3X3 ← r; out16 3X4 ← w
w16x2(r, w)   0x1152cea: w16; delay(1); w16
r8(r) / r16(r) 0x1152d3f / 0x1152d7a
poke / peek   0x1152db6 / 0x1152e09: w16(0x43, a & 0xffff), w8(0x44, a >> 16), 3X7 (DRAM size probe only)
waitRamp()    0x1152f50: up to 20 times: if (r8(0x8d) & 1) break;            // ~40 us: about one GF1 frame
ramp(v)       0x1152e5e:                                       // the selected voice, v = 0..64
    v = min(v, 64); v = master * v >> 8;                       // master 0x100
    target = VOLTAB[v] >> 8;                                   // 8-bit ramp end (VOLTAB: 7.6)
    w8x2(0x0d, 3);                                             // stop the ramp
    cur = r16(0x89) >> 8;                                      // current volume, top 8 bits
    w16(0x09, cur << 8);
    if (cur > target) { w8(0x07, target); w8(0x08, cur); dir = 0x40; } else { w8(0x07, cur); w8(0x08, target); dir = 0; }
    w8(0x06, 0x3f);                                            // rate: 63 per frame
    w8x2(0x0d, dir);                                           // go
setPos(hi, lo, a): w16(hi, (a >> 7) & 0x1fff); w16(lo, (a & 0x7f) << 9)
fc(f) = min(2 * ((RATE[voices] / 2 + (f << 9)) / RATE[voices]), 0xfffe)     // RATE[n] = 617400 / n (7.6)
```

### 7.3 Init, reset, voices, stop

```
SDIInitDevice(cfg) 0x1154881: cfg->device must be 0x0a; copy cfg; ports from cfg->port; DMA, IRQ;
    dma = (cfg+0x3c == 0); master = 0x100; samples = 0; voices = 14; clear the voice records;
    sample table = calloc(400, 8); DMA buffer: 0x2000 bytes of DOS memory (block size [0x1156286] = 0x2000), DMA setup;
    reset(); dramInit(); cdiRegister(0x1155c1a)
reset() 0x11546aa:
    w8(0x4c, 0); delay(10); w8(0x4c, 1); delay(10);            // GF1 reset, run
    out 3X0 ← 3; delay(10); out 3X0 ← 0;                       // MIDI reset ("p320" in full.gus)
    w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);                     // DMA, timers, sampling off
    in 2X6; r8(0x41); r8(0x49); r8(0x8f);                      // clear pending IRQs
    w8(0x0e, 0xdf);                                            // 32 voices
    for v < 32: sel(v); clearVoice(v); w8(6, 0); w8(0x0d, 3); w16(9, 0); w8(0, 3); w8(0x0c, 8);
    in 2X6; r8(0x41); r8(0x49); r8(0x8f);
    w8(0x0e, 0xc0 | (voices - 1)); w8(0x4c, 3);               // run + DAC on
    out 2X0 ← 9                                                // mix control: line in off, output on, latches on
clearVoice(v) 0x1154114: stopNote(v); w16(2, 0); w16(3, 0); w16(4, 0); w16(5, 0); w16(0x0a, 0); w16(0x0b, 0);
    voice[v].addr = voice[v].freq = 0
setupChannels(n) 0x115419b: for v < voices: clearVoice(v); voices = clamp(n, 14, 32);
    w8(0x0e, 0xc0 | (voices - 1)) (on the voice selected last); for v < voices: clearVoice(v)
SDIStartOutput 0x1154a0e: return 0;   SDIStopOutput 0x1154a44: for v < voices: clearVoice(v)
SDICloseDevice 0x1154a85: stop output, free DRAM list, voices = 32, reset(), free (not reached in the capture)
```

### 7.4 The CDI functions (all select the voice first)

```
setSample(ch, s) 0x1153576:
    if (ch >= voices) return -1;  a = DRAM address of s->handle (table lookup 0x1153509)
    voice.mode = s->loopend ? 8 : 0;
    if (voice.addr == a) { ramp(0); return 0; }               // same sample: only fade (setVolume follows)
    voice.addr = a; ramp(0); waitRamp(); w8x2(0, 3);           // stop the voice
    setPos(0x0a, 0x0b, a);
    ls = a + (s->loopend ? s->loopstart - (s->loopstart > 1) : 0);  setPos(2, 3, ls);
    setPos(4, 5, a + (s->loopend ? s->loopend - 1 : s->length - 2));
playNote(ch, f, v) 0x1153716:
    voice.vol = v; voice.freq = f;
    ramp(0); waitRamp(); w8x2(0, 3);                           // old note: ramp starts, voice stopped ~1 frame later
    setPos(0x0a, 0x0b, voice.addr); w16(1, fc(f));
    w16x2(9, 0x1770);                                          // VOLTAB[0]: 12-bit 0x177, -87 dB
    w8x2(0, voice.mode);                                       // start (looped or one-shot)
    if (!voice.muted) ramp(v)
setVolume(ch, v) 0x1153831:  v = min(v, 64) (unsigned); voice.vol = v; if (!voice.muted) ramp(v)
setFrequency(ch, f) 0x115388e: voice.freq = f; w16(1, fc(f))
setPosition(ch, o) 0x1153927:
    ramp(0); waitRamp(); w8x2(0, 3); w8x2(0x0d, 3); setPos(0x0a, 0x0b, voice.addr + o); w8x2(0, voice.mode);
    if (!voice.muted) { ramp(voice.vol); waitRamp(); }
setPanning(ch, p) 0x11539ef: if (p == 100) p = 0; p = p > 63 ? 63 : p <= -63 ? -64 : p;
    w8(0x0c, p / 8 + 8)                                        // division toward zero: -63 → 0, 63 → 15
stopNote(ch) 0x1153b7b: ramp(0); waitRamp(); voice.vol = 0; w8x2(0x0d, 3); w8x2(0, 3); w16(9, 0x1770)
setMasterVolume(v) 0x1153a63: master = min(4 v, 0x100); for each voice: setVolume(voice, voice.vol)
mute(ch): ramp(0); muted = 1      unmute(ch): if (muted) { ramp(voice.vol); muted = 0 }
```

So the four channels sit on voices 0..3, panned 0, 15, 15, 0 (hard left / right). Every call of setVolume — at least
once per row and channel — is a ramp to the target from wherever the volume is.

### 7.5 DRAM allocation and upload

```
dramInit() 0x11530c9: list = { {addr 0x2000, size 0x20}, {addr DRAM size, size 0} };
    DRAM size: poke(0x40400, 0x55) → 256 KB unless it reads back, then poke(0x80400, 0x55) → 512 KB / 1 MB
alloc(n) 0x11532ab:  n = (n + 0x40) & ~0x1f;                   // best fit, never across a 256 KB bank
    for each block b with a successor: gap start s = b.addr + b.size;
        if ((s + n) >> 18 != s >> 18) { s = (s + n) & ~0x3ffff; gap = next.addr - s; } else gap = next.addr - s;
        take the smallest gap with n < gap (strictly)
    insert {s, n} after b; return s
download(data, handle, n) 0x1153df0: a = alloc(n) (0 → -1); upload (DMA 0x1153c6c, or 0x1154d24 by PIO);
    remember {handle, a} in the first free table slot
upload 0x1153c6c: w8(0x41, 0);
    per block of up to 0x2000 bytes: w16(0x42, a >> 4) (16-bit DMA channels: the GF1's 16-bit address form);
        copy to the DOS buffer, program the 8237 (mode, address, count, page); w8(0x41, 0x81 | (dma > 3 ? 4 : 0));
        delay; wait until r8(0x41) & 0x40 (at most 300,000 reads); w8(0x41, 0); a += block
```

Reg 0x41 = 0x81: start, memory → card, 8-bit data, **invert MSB**: the GF1 stores every byte XOR 0x80, so the
unsigned AMF samples become signed in DRAM. EYE.AMF's 31 samples (666,132 bytes, 95 blocks) end up at the addresses
of section 3: in instrument order from 0x2020, each block rounded to (length + 0x40) & ~0x1f, Klop11 and WetBass.dist
moved to the next 256 KB bank (0x40000, 0x80000) and the gap before 0x80000 filled by the later samples.

### 7.6 Tables in the driver

- **Volume table** 0x11557ac = SDIGUS.DSD file offset **0x700c**, 65 dwords (only bits 15..8 matter as ramp targets,
  and entry 0's low 16 bits, 0x1770, as the start / stop volume). Values, as gain relative to entry 64: 0 −80.8 dB,
  1 −31.0, 2 −27.0, 4 −22.7, 8 −17.7, 16 −11.6, 32 −5.8, 48 −2.2, 64 0 dB (= 12-bit 0xef4). From entry 8 up it is
  close to linear amplitude (v / 64), below that boosted (entry 1 = 1.8 / 64, entry 4 = 4.7 / 64); no formula I
  tried (linear amplitude through the GF1 log scale with several full-scale values) reproduces the bytes. All 65
  entries occur in the song. **The remake needs this table**: read SDIGUS.DSD at run time (offset 0x700c).
- Rate table 0x1155878 (file offset 0x70d8), entries 14..32 = 617400 / n truncated (44100, 41160, 38587, .., 25725
  for 24, .., 19293): computed, not needed. Entries 0..13 are unused garbage.

---------------------------------------------------------------------------------------------------------------

## 8. The register stream (for checking a port against `full.gus`)

`full.gus` lines: `stamp v<voice> r<reg> <hex>` (voice registers), `stamp g r<reg> <hex>` (registers ≥ 0x40,
except the DRAM address registers 0x43 / 0x44), `stamp p<port> <hex>` (2X0, 3X0). Stamp = interrupts so far.

| part | stamps | writes |
|---|---|---|
| reset (SDIInitDevice) | 86 | 779: r4c 0, r4c 1, p320 3, p320 0, r41, r45, r49, r0e df, 32 × (clearVoice 19 + 5), r0e cd, r4c 3, p220 9 |
| setupChannels(24) | 86 | 14 × 19 + r0e d7 + 24 × 19 = 723 |
| pans | 87 | v0 r0c 0, v1 r0c f, v2 r0c f, v3 r0c 0 |
| uploads | 87..90 | per sample `g r41 0`, per block `g r42 a>>4`, `g r41 81`, `g r41 0` (95 blocks) |
| music | 125..52262 | per row and channel at least a ramp (8 writes: r0d 3 ×2, r09, r07, r08, r06 3f, r0d dir ×2); an instrument: setSample 8 (same sample) or 16 writes, then a ramp; a note: playNote 25 (ramp, r00 3 ×2, r0a, r0b, r01, r09 1770 ×2, r00 mode ×2, ramp); a sample offset: setPosition 24 |
| exit | 52262 | 4 × stopNote (13) + 24 × clearVoice (19) = 508 |

The stamps of the start-up writes depend on how fast the main line loads the module (the capture's 150 MIPS);
everything from interrupt 91 on is fixed by the music.

The capture's fake GUS answers a read of reg 0x89 with the last value written to reg 9 and 0x8d with bit 0 set, so
the logged ramps start from the last written value and every wait ends at once (reads in the capture: 0x89 14,858,
0x8d 2,362). On hardware the same writes go out (140,095, same registers, same order) but 30,384 values differ (the
r09 / r07 / r08 / r0d of ramps that start from a partly ramped volume), and the waits let the GF1 run 1..10 frames in
924 of the 52,172 music interrupts (`render.py` uses this). A port must answer reg 0x89 from its GF1 model.

---------------------------------------------------------------------------------------------------------------

## 9. Timing: every player tick

```
T(n) = 200 n                  for tick n = 1..6        (tempo 125: period 200; row 0:00 is read at tick 6)
T(n) = 1200 + 192 (n - 6)     for n >= 6               (tempo 130 from 0:00 on: period 192)
callback k(n) = T(n) / 35 + 1                          (integer division: the first k with 35 k > T(n))
interrupt i(n) = 90 + k(n)                             (capture: 91 + k(n) for k(n) >= 12027, after the lost one)
song row R = 64 order + row is read at tick 6 + 4 R (R < 2304), 6 + 9216 + 12 (R - 2304) in order 36
interrupt i = 4 m + j, j = 1..4 (relative to record 0 of full.frames, in refreshes):
    j = 1, 2, 3:  15 + m + (4085 j - 1402.93) / 17024.67      (= +0.1576, +0.3975, +0.6374)
    j = 4:        16 + m - 0.0824                              (the retrace interrupt, just before record 16 + m)
seconds = refreshes / 70.086
```

(The 4 interrupts of a refresh: the one at the retrace (line 412, 37 lines before line 0) runs the frame clock and
the part's tick routine; the other three follow 4,085 PIT clocks apart. "15" is the capture's: the timer was
installed at record 15.) `timeline.py --check` confirms the formula for all 9,510 ticks of the capture
(`timeline.py --rows` lists every row). The tick runs inside the interrupt: the row is read and its notes start on
the GUS at that instant (no extra tick of latency). `full.ticks` shows (order, row) after the interrupts counted in
its `irqs` column.

Order starts in the capture:

| order | interrupt | refresh | s | | order | interrupt | refresh | s |
|---|---|---|---|---|---|---|---|---|
| 0 | 125 | 46.16 | 0.659 | | 19 | 26808 | 6716.92 | 95.838 |
| 1 | 1529 | 397.16 | 5.667 | | 20 | 28213 | 7068.16 | 100.849 |
| 2 | 2933 | 748.16 | 10.675 | | 21 | 29617 | 7419.16 | 105.857 |
| 3 | 4338 | 1099.40 | 15.686 | | 22 | 31021 | 7770.16 | 110.866 |
| 4 | 5742 | 1450.40 | 20.694 | | 23 | 32426 | 8121.40 | 115.877 |
| 5 | 7147 | 1801.64 | 25.706 | | 24 | 33830 | 8472.40 | 120.885 |
| 6 | 8551 | 2152.64 | 30.714 | | 25 | 35234 | 8823.40 | 125.893 |
| 7 | 9955 | 2503.64 | 35.722 | | 26 | 36639 | 9174.64 | 130.905 |
| 8 | 11360 | 2854.92 | 40.734 | | 27 | 38043 | 9525.64 | 135.913 |
| 9 | 12765 | 3206.16 | 45.746 | | 28 | 39447 | 9876.64 | 140.921 |
| 10 | 14169 | 3557.16 | 50.754 | | 29 | 40852 | 10227.92 | 145.933 |
| 11 | 15574 | 3908.40 | 55.766 | | 30 | 42256 | 10578.92 | 150.941 |
| 12 | 16978 | 4259.40 | 60.774 | | 31 | 43660 | 10929.92 | 155.949 |
| 13 | 18382 | 4610.40 | 65.782 | | 32 | 45065 | 11281.16 | 160.961 |
| 14 | 19787 | 4961.64 | 70.793 | | 33 | 46469 | 11632.16 | 165.969 |
| 15 | 21191 | 5312.64 | 75.801 | | 34 | 47873 | 11983.16 | 170.977 |
| 16 | 22595 | 5663.64 | 80.810 | | 35 | 49278 | 12334.40 | 175.989 |
| 17 | 24000 | 6014.92 | 85.822 | | 36 | 50682 | 12685.40 | 180.997 |
| 18 | 25404 | 6365.92 | 90.830 | | | | | |

An order (256 ticks = 49,152 DSMI units) is 351.09 refreshes = 5.0094 s. Without the lost callback (the remake),
every music time after interrupt 12117 is one interrupt (0.24 refresh, 3.4 ms) earlier than in the capture.

Time bases: DSMI time = 35 × music callbacks = 140 per refresh; player state changes only at ticks (above); the GF1
output runs on its own clock (25,725 frames per second) with the register writes at the interrupt instants (+ the
ramp waits). For `render(ctx, time)`: the music state is a function of the callback count; seeking = replay AMP from
ampPlay (≤ 10,000 ticks, cheap) and the GF1 voices from their last note starts.

---------------------------------------------------------------------------------------------------------------

## 10. The Python models (`models/music/`)

| file | what |
|---|---|
| `amf.py` | EYE.AMF reader (layout in its docstring) and a dump of the song |
| `dsmi.py` | the port: fake card (`Card`, as run_inside.py's), GUS driver (`Driver`), AMP (`Amp`), timer + callback (`Dsmi`); tables read from `inside_img.bin` and SDIGUS.DSD |
| `player.py` | runs it against the capture: start-up writes (text), music writes (with stamps), the exit, the positions of `full.ticks` → "ALL MATCH"; `--out` writes the log, `--ticktimes` every tick, `--skip` the lost callbacks |
| `statecheck.py` | compares 214 state fields with every snapshot → "ALL MATCH" |
| `timeline.py` | the closed form of section 9 (`--check`, `--rows`) |
| `gf1.py`, `render.py` | GF1 model and WAV render (section 11) |
| `snapfind.py`, `snapinfo.py`, `snapdis.py` | find the driver in a snapshot, print DSMI state, disassemble snapshot memory |

---------------------------------------------------------------------------------------------------------------

## 11. GF1 model, render, and the clone remake's gf1

`render.py out.wav` (about 20 s): the port drives `gf1.py`, a copy of clone's `mxmplay.py` GF1 model (positions in
1/512 address units, FC >> 1 per frame, 12-bit log volume (256 + mantissa) · 2^(exponent − 24), ramps of
(rate & 63) · 512 >> (3 · (rate >> 6)) per frame, pan L = (15 − p) / 15, R = p / 15, linear interpolation, forward
loops and one-shots) that answers reg 0x89 with the ramped volume and lets the GF1 run one frame for a wait while the
selected voice is still ramping. DRAM holds the samples XOR 0x80. 24 voices: 25,725 frames per second; interrupts at
the instants of section 9 (fraction of a frame accumulated); the WAV starts at record 0 of `full.frames` (silence until
the first note at 0.659 s) so it lines up with `frames2mp4.py`'s video, and ends at the exit (186.64 s, 4,801,214
frames); `--rate 44100` resamples. Level: the mix peaks at 0.87 of one full-scale voice; normalised to 0.9.

What the original sounds like on a GUS, which the remake should keep: 8-bit samples played at 2..3 × their rate
through a 25,725 Hz GF1 with linear interpolation; pitch quantised to FC steps of 1/512 sample per frame (50 Hz:
3.5 cents at the usual 25 kHz playback); hard L R R L panning; the 65-step volume table; every volume change a 63-
steps-per-frame ramp (≤ 2.5 ms); each new note cuts the old one within about one frame (a click) and fades in from
−87 dB over up to 2 ms; one-shot samples stop at length − 2.

Compared with `clone/remake/src/gf1.h` / `gf1.cpp` (the remake's GF1), DSMI's driver uses: 8-bit voices only, forward
loops and one-shots (no bidirectional loops, no backwards play), regs 0x00..0x0e and 0x4c the way gf1 already models
them, 24 active voices via reg 0x0e (gf1 takes any count), reads of 0x89 (16-bit) and 0x8d (8-bit). What gf1 lacks:

1. **A bounded single-voice ramp wait.** `Hardware::waitRamps(voices)` runs the card until the ramps of *all* given
   voices stop (MXMPlay's busy wait); DSMI's 0x1152f50 polls reg 0x8d of the *selected* voice at most 20 times and
   then goes on (about one GF1 frame), so the ramp down is cut short. Add e.g. `waitVoice(maxFrames = 1)`.
2. **The upload.** DSMI uploads by DMA with reg 0x41 = 0x81 (invert MSB). gf1 has only `poke`; the remake should
   poke `byte ^ 0x80` (EYE.AMF's samples are unsigned) at the allocator's addresses (7.5) — the same addresses are
   needed only to reproduce `full.gus`; for the sound any layout without 256 KB bank crossings works.
3. **Logging in this capture's format**, if the remake wants a `full.gus` check: base 0x220 (gf1's `kBase` is 0x240),
   "g r%02x" for registers ≥ 0x40, no lines for 0x43 / 0x44, "p%03x" for the absolute port (p320, p220).
   `Card::r8(0x41)` should return bit 6 set (DMA done) if the upload is run through the card.
4. Nothing else: the reset writes (reg 0x4c, the MIDI port 3X0, mix control 2X0, regs 0x41 / 0x45 / 0x49) need no
   modelling beyond what `received()` does with 0x4c and 0x0e.

---------------------------------------------------------------------------------------------------------------

## 12. Porting notes for the remake

- Read at run time: EYE.AMF (whole file; the loader of 2.2, samples XOR 0x80 into the card's DRAM); from INSIDE.EXE's
  image the semitone table 0x40c82e (132 words), the period table 0x40c936 (127 words), the sine table 0x40ca34 (64
  words) and the arpeggio table 0x40cab4 (16 words) (the last two are only needed for effects EYE.AMF does not use);
  from SDIGUS.DSD the volume table (file offset 0x700c, 65 dwords). Code constants: 0x1ac0, 50 / 125, 10000, 35,
  0x3f, 0x1770 (= VOLTAB[0]), 617400, 0x64, 0xd5ff, 0x2000 / 0x20 / 0x40 (allocator).
- Port `dsmi.py`'s `Amp` and `Driver` routine by routine and feed the driver's writes to `gf1::Hardware` (reads) and
  `gf1::Synth` (output), as the clone remake does with MXMPlay; the timing is section 9: one callback per timer
  interrupt, four per refresh at the given phases, starting at the remake's equivalent of interrupt 91.
- Keep the table-driven pitch (SEMITONE / PERIOD, rate · table >> 10, the GF1's FC rounding): they are what the
  original plays (differences from equal temperament are below 2 cents; FC steps 3.5 cents). The things a
  high-resolution remake may evaluate more exactly are the GF1's 25.7 kHz output (render at 48 kHz with gf1::Synth's
  scaled increments: same pitch and ramp durations, less aliasing) — as the clone remake does.
- **Known differences** to list: the lost callback(s) during the credits (CPU dependent; the capture lost one at
  interrupt 12117; the remake should not lose any, so its music runs 3.4 ms ahead of the capture from 43.4 s on);
  real-hardware details of the GF1 (pan law, interpolation) as in clone's notes.
- AMP's tone-porta `setVolume(row number)` / `setVolume(255)` writes (5.6) have no audible effect (replaced in the same
  interrupt); keep them only if the register stream must match `full.gus`.

---------------------------------------------------------------------------------------------------------------

## 13. Problems found, open points

1. **run_inside.py's GUS DMA swaps bits 6 and 7 of reg 0x41** (inherited from `stars/re/run_stars.py`): it inverts
   the MSB when bit 6 is set; on the GF1 bit 7 is "invert MSB" and bit 6 "16-bit data" (GUS SDK; the Linux driver's
   SNDRV_GF1_DMA_UNSIGNED = 0x80, SNDRV_GF1_DMA_16BIT = 0x40; DOSBox the same). DSMI writes 0x81, so `full.dram`
   holds the samples unsigned, as in EYE.AMF, where a real GUS holds them XOR 0x80. `full.gus` is not affected. (STARS
   writes 0x21: no effect there, but `stars/re/notes-music.md` names bit 6 "invert MSB".) Not fixed (read-only).
2. **NOTES.md, "Clock"**: the callback 0x40070c returns at once while [0x42278c] ≠ 0 (set by the credits' frame
   routine 0x406794 around 0x4061fc); then the part ticks, the frame clock and DSMI time all lose that interrupt. In
   the capture: interrupt 12117 (records 3044 → 3045 show 3 part ticks for 4 interrupts). Worth a line there.
3. NOTES.md "the script": 0x40de09 / 0x40de21 return −1 only before ampInit ([0x423399] bit 0), not "when not
   playing"; [0x422a61] is the row read last, set after all channels of the row are read.
4. The real timing of the 20-poll ramp wait (one GF1 frame assumed) and of `delay(1)` (6 reads of 3X7) is a guess
   about ISA timing; it only changes how much of the old note's ramp-down happens before the cut (≤ 1 frame).
5. The GF1 pan law at 0 / 15 (hard left / right here) and its volume curve are as in clone's / JIZZ's notes, not
   measured. There is no audio capture of Inside to compare the WAV with.
6. AMP effects that EYE.AMF does not use (porta, vibrato, tremolo, arpeggio, retrig, note cut, fine porta, the fine
   slide 0x96, panning, pattern break, position jump, sync) are ported from the code but not checked.
