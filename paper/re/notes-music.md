# Paper — music (scope "music": everything that makes the sound)

All addresses are code32 offsets (image offset = code32 + 0xd00 in paper_img.bin). Reference implementation:
`player_ref.py` (same folder). It mirrors the assembly routine by routine and, from paper_img.bin alone,
reproduces the captured run exactly (see "Validation").

## 0. Summary

- **No instrument synthesis.** The intro does not generate its instruments: 8 sampled instruments (8-bit
  unsigned PCM, 34,498 bytes) are stored raw in the exe right after the pattern data, copied to a buffer,
  converted to signed (xor 0x80) and poked into GUS DRAM byte by byte at start-up. DRAM image = 34,944 bytes.
- The tune is a 10-channel tracker song (1472 rows, converted from a MOD/S3M-like module into a compact
  channel-major stream). The player is a small hand-written MOD-style player: periods, portamento, tone
  portamento, note delay, sample offset, speed, tempo (a volume slide exists but is buggy and unused).
- Timing: IRQ0 at 70.0018 Hz; a tempo accumulator calls the player at 125 BPM = 50.0013 Hz; speed 6 →
  one row per exactly 8.4 IRQ ticks (8.3336 rows/s) until row 1407, then speed 31 (43.4 ticks/row).
- GF1 setup: 16 active voices (38,587.5 Hz), channel k on voice 9−k, fixed pans, all volume changes as
  hardware volume ramps (rate 0x30) starting from the voice's *current* volume (read back from reg 0x89).
- Validation: player_ref.py regenerates full.dram (34,944 bytes, identical; rest zero), full.gus
  (104,499 lines, byte-identical) and the music counter of full.ticks (12,235 ticks, all equal).
- **Capture caveat:** run_paper.py's fake GUS answers the volume read with the last *written* reg 9 (always 0),
  so full.gus (and paper_music.wav) always "ramp up from 0": every volume decrease becomes an instant jump.
  Real hardware ramps down. The remake must answer the read from its GF1 model (`--real-volume-read`).
- gf1synth.py has one serious bug (looped instruments 3 and 5 lose everything before the loop start) and
  several minor ones (section 9).

## 1. What runs when (time base: IRQ0 ticks, 70.0018 Hz)

| tick | what |
|---|---|
| 0 (main line) | 0x1c24 sound init: detect, GF1 init (logged at tick 0), unpack patterns, upload samples, IRQ0 = 0x1e45, PIT divisor 0x4295, tempo 125 |
| 104 | main sets [0x1ea93] = 1 after the precalc (0x7682); first player call in that tick's IRQ, counter 0. **Machine dependent** (precalc length); 104 in the 1.5M-instructions-per-tick run |
| 104..11930 | rows 0..1407, speed 6: counter(t) = floor(5·(t − 103) / 42) (exact; checked against full.ticks) |
| 11931.. | row 1408 sets all 10 channel volumes to 0 (→ GF1 0x200, −84 dB: silence); speed 31 from row 1408 on (row 1407 sets it), rows at 11931, 11974, 12017, 12061, 12104, 12148, 12191, 12234 |
| 12235 (main line) | exit: 0x7a12 [0x1ea93] = 0, 0x1c8e sound off (PIT divisor 0 = 18.2 Hz, GF1 re-init → all voices stopped, volume 0) |

The music is entirely IRQ driven: nothing in it depends on the drawn frame rate. The only machine-dependent
quantity is the start tick (first IRQ after the precalc). All parts sync to the music counter [0x1ea8b], so
the remake can pick any start tick; use 104 to line up with paper_reference.mp4 / paper_music.wav.
The greetings callback 0x26aa also reads the sub-row countdown [0x1e4e6] (`cmp byte [0x1e4e6],3; jbe`):
values 6,5,4,3,2,1 within each row (6 on the row tick).

## 2. Start-up (0x1c24)

```c
// 0x1c24
if (!detect_gus())                       // 0x2256, CF set = not found
{   print("GUS not detected - Port?  Press the middle digit or space for no sound");  // 0x45ee via 0x595d
    key = int16h_read();                  // int 0x33 al=0x16
    if ('0' <= key && key <= '9') { port_lo = (key - '0') << 4; goto have_port; }  // byte [0x1e4e7]
    port = 0; goto no_init;               // dword [0x1e4e7] = 0: no sound, player still runs (counter, speed, tempo)
}
have_port: gf1_init();                    // 0x22f8
no_init:
row = -1;                                 // dec [0x1ea8b] (0 in the image)
set_irq(0, 0x1e45); pit_divisor(0x4295);  // 0x1e32: out 0x43,0x36; out 0x40,lo; out 0x40,hi; EOI
set_tempo(125);                           // 0x1e1e: [0x1e5ff] = 0x2d8426 / 125 = 23863
build_period_table();                     // 0x1ca4
unpack_patterns();                        // 0x1cbe..0x1d4c
if (port == 0) return;
copy_samples_and_upload();                // 0x1d5b..0x1e1d
```

**Detection 0x2256**: environment segment = word [PSP + 0x2c]; scan the environment for "ULTRASND"
(match 'U', then dword "ULTR" at the 'U' and "ASND" 4 bytes later; a NUL followed by NUL ends the scan →
not found); then skip to the first '2' character (NUL → not found) and take the next character:
`byte [0x1e4e7] = (c - '0') << 4`. The high byte of the default 0x0220 stays → port 0x2X0
(ULTRASND=240,... → 0x240). IRQ and DMA are ignored (no GUS IRQ, no DMA: all DRAM access is PIO).

**GF1 init 0x22f8** (also called by 0x1c8e at exit). Port variables: [0x1e5f3] = base+0x102 (voice select),
[0x1e5f7] = base+0x103 (register select; 0 = no GUS). Register writes go through three helpers:
`0x22c1` 8-bit (reg → 0x343, byte → 0x345), `0x22d0` 16-bit (reg → 0x343, word → 0x344),
`0x22da` address pair (reg → value>>7, reg+1 → value<<9, both 16-bit), `0x22b6` = 256 dummy reads of
0x343 then an 8-bit write (reset delay).

```c
out(base+0, 9);                 // mix control: line-in off, line-out on, latches on
w8d(0x4c, 0); w8d(0x4c, 1);     // reset, run (each after the 256-read delay)
w8(0x41, 0); w8(0x45, 0); w8(0x49, 0);   // DMA ctl, timer ctl, sampling ctl
w8(0x0e, 0xcf);                 // 16 active voices -> 617400/16 = 38587.5 Hz
for (v = 31; v >= 0; v--) {
    select(v);
    w8(0x00, 3); w8(0x0d, 3);   // voice stopped, ramp stopped
    w8(0x06, 0x3f); w8(0x09, 0x3f);  // ramp rate; volume high byte (then:)
    w8(0x01, 0);  w16(0x09, 0); // FC high byte 0; volume 0
    w8(0x0c, mem[0x7a8b + v] & 15);  // pan
}
w8(0x4c, 3);                    // run + DAC enable, no GF1 IRQs
```

Pans (voice 0..9): 7, 10, 5, 4, 6, 7, 9, 10, 11, 8, so channel 0..9 (voice 9−k) is panned
8, 11, 10, 9, 7, 6, 4, 5, 10, 7 (0 = left, 15 = right). Voices 10..15 get 3, 12, 3, 12, 3, 12; from voice 16
the table runs into the tune header (10, 0xc0, 5, 0x1f, 2, 2, ... & 15). Voice 19's pan is the
instrument-count byte [0x7a9e], which the upload loop decrements to 0 — so the re-init at exit writes pan 0
there (15 at start). player_ref.py models this; it only shows in the log (voices ≥ 10 are never played).

## 3. Static data

| what | code32 | size / format |
|---|---|---|
| pan table | 0x7a8b | 32 bytes, `& 15` per voice (overlaps the tune header from voice 16) |
| tune header | 0x7a9b | u8 channels = 10, u16 rows = 1472 (0x5c0), u8 instrument slots = 31 |
| pattern stream | 0x7a9f..0xe0e0 | 26,177 bytes, channel-major (below) |
| instrument stream | 0xe0e0..0x169a2 | 35,010 bytes: 31 headers + sample data (below) |
| volume table | 0x1e49c | 65 bytes: channel volume 0..64 → GF1 volume high byte |
| (unreferenced) | 0x1e4dd | 8 bytes 36 0a f6 ca ca f6 0a 36 (no code reads them) |
| speed / countdown | 0x1e4e5 / 0x1e4e6 | u8 6 / u8 1 |
| base port | 0x1e4e7 | u32 0x220 (middle digit patched by detection) |
| period seed | 0x1e4eb | 16 × u16: 6848 6464 6096 5760 5424 5120 4832 4560 4304 4064 3840 3624 0 0 0 0 (= Amiga periods 856..453 × 8) |

Variables: [0x1e5fb] tempo accumulator (s32, 0), [0x1e5ff] tempo period (PIT clocks per player tick),
[0x1e603] event buffer pointer (getmem), [0x1e607] row stride (= channels × 5 = 50), [0x1e60b + 0x14·(i−1)]
instrument records, [0x1e88b + 16·k] channel records, [0x1ea8b] row = music counter, [0x1ea93] music on,
[0x1c20] u32 pending sample offset (effect 0xd writes its byte 1, i.e. param << 8).

### 3.1 Period table (0x1ca4)
`period[n]` for n = 0..127: n < 16 → seed; n ≥ 16 → `period[n-16] >> 1` (u16). The note byte is
`octave*16 + semitone` (semitone 0..11; 12..15 give period 0), so `period(note) = seed[note & 15] >> (note >> 4)`.
Notes used: 0x26..0x7b.

### 3.2 Pattern stream → 5-byte events (0x1cbe)
Events are unpacked into a getmem buffer of rows × 50 bytes; event (row r, channel k) at `buf + r*50 + k*5`:
`[0] note (0 = none), [1] instrument (0 = none), [2] volume (0xff = none), [3] effect, [4] parameter`.

```c
p = 0x7a9f;
for (k = 0; k < 10; k++) for (r = 0; r < 1472; r++) {
    e = {0, 0, 0xff, 0, 0};                 // dword 0x00ff0000, byte 4 = 0
    f = mem[p++];
    v = f & 0x3f;  if (v == 0x3f) v = 0x40;  if (v == 2) v = 0xff;   // 2 = "no volume", 63 = 64
    e[2] = v;                               // so volume 2 cannot be expressed
    if (f & 0x40) { b = mem[p++]; c = mem[p++];
        e[0] = b & 0x7f;  e[1] = ((b >> 7) << 4) + (c & 15);  e[3] = c >> 4; }
    if (f & 0x80) e[4] = mem[p++];
}
```

### 3.3 Instruments and DRAM layout (0x1d5b, upload 0x1dd8)
Stream after the patterns, per slot i = 1..31: `s16 L`. L = 0: empty slot (2 bytes). L > 0: one-shot:
`u16 c2spd, L bytes data`. L < 0: looped: `u16 loopofs, u16 len, u16 c2spd, data`, stream advance |L| + 6
(len = |L| − 2, so 2 stream bytes after the data are skipped). The buffer (at the heap start [8]) begins with
8 bytes 0x80; each sample is followed by 2 pad bytes: one-shot: its last byte and the byte after the data
(= low byte of the next header); looped: the 2 bytes at the loop start (seamless interpolation over the loop end).
Upload: DRAM[a] = buf[a] ^ 0x80 for a = 0..34943 via regs 0x43/0x44 and port base+0x107.

Record at 0x1e60b + 0x14·(i−1): `+0 len, +4 DRAM start, +8 DRAM loop start (0 = no loop), +0xc DRAM end
(= start + len, one past the last byte), +0x10 u16 c2spd`.

| ins | type | len | loop at | c2spd | data (code32) | DRAM start..end | used on channels |
|---|---|---|---|---|---|---|---|
| 1 | one-shot | 15584 | – | 8363 | 0xe0e4 | 8..15592 | 0, 1 (with offset 0xd1f) |
| 2 | one-shot | 1250 | – | 16383 | 0x11dc8 | 15594..16844 | 1, 2, 3, 8 |
| 3 | looped | 11082 | 738 (DRAM 17584) | 8529 | 0x122b2 | 16846..27928 | 3..7 |
| 5 | looped | 1206 | 988 (DRAM 28918) | 8943 | 0x14e08 | 27930..29136 | 1, 2, 9 (with offset 0xd03) |
| 6 | one-shot | 2576 | – | 8363 | 0x152c4 | 29138..31714 | 2, 3 |
| 7 | one-shot | 1520 | – | 8363 | 0x15cd8 | 31716..33236 | 2, 3, 8 |
| 8 | one-shot | 1058 | – | 8363 | 0x162cc | 33238..34296 | 0, 6, 7 |
| 9 | one-shot | 644 | – | 8363 | 0x166f2 | 34298..34942 | 2, 3, 6, 7 |

Slots 4 and 10..31 are empty. For a high-quality remake the samples are what they are (8-bit, ~8–16 kHz
material); there is nothing procedural to regenerate at higher resolution.

## 4. IRQ and tempo (0x1e45 music part, 0x1e1e)

```c
// in the IRQ0 handler, before the palette fade and the effect callback [0x1e5ef]
if (music_on) {                       // byte [0x1ea93]
    acc -= 0x4295;                    // s32 [0x1e5fb], starts at 0
    while (acc <= 0) { player(); acc += tempo; }   // tempo = [0x1e5ff]
}
set_tempo(bpm) { tempo = 0x2d8426 / bpm; }   // u32 div; 0x2d8426 = 2.5 * 1193180; bpm 125 -> 23863
```
Player ticks at 1193182/23863 = 50.0013 Hz; with speed 6 a row is 143,178 PIT clocks = exactly 8.4 IRQ
ticks. Player call k (k = 0, 1, ...) runs in IRQ number `max(0, ceil(k·23863/17045) − 1)` after the start
(pattern 1,1,1,0,1,1,0,...: never two calls in one IRQ at 125 BPM). The tune only ever sets bpm 125.

## 5. Player (0x1ed6)

Channel record (16 bytes at 0x1e88b + 16k): `+0 s32 period, +4 s32 target period, +8 instrument record
pointer, +0xc u8 volume, +0xe u8 portamento speed memory, +0xf u8 volume-slide memory`. Channel k always uses
GF1 voice 9 − k (`out base+0x102, cl−1` with cl counting 10..1); voice-select writes are skipped if no GUS.

```c
void player(void)                                   // 0x1ed6
{
    if ((s8)--count > 0) goto ticks;                // byte [0x1e4e6]
    count = speed;                                  // [0x1e4e5]
    if ((u16)++row >= 1472) row = 0;                // [0x1ea8b] (16-bit compare)
    for (k = 0; k < 10; k++) {                      // 0x1f28
        ev = &events[row][k]; ch = &chan[k]; select_voice(9 - k);
        trig = 0;
        if (ev->note) { trig = 1; ch->target = period[ev->note]; }
        if (ev->ins)  { trig = 1; ch->ins = &ins_rec[ev->ins]; }
        switch (ev->eff) {                          // row effects (all tested, in this order)
            case 0xa: speed = ev->par; break;       // takes effect at the next row
            case 0x9: set_tempo(ev->par); break;
            case 0xd: sample_offset = ev->par << 8; break;   // [0x1c20], consumed by the next start_sample
        }                                           //        on ANY channel, then cleared
        if (ev->eff == 3 || ev->eff == 8) trig = 0; // tone porta / note delay: no trigger on the row
        if (trig) { ch->period = ch->target; trigger(ch); }
        if (ev->vol != 0xff) { ch->vol = ev->vol; set_volume(ch->vol); }
    }
ticks:                                              // 0x1fbf: every player tick, the row tick included
    if (no_gus) return;
    for (k = 0; k < 10; k++) {
        ev = &events[row][k]; ch = &chan[k]; select_voice(9 - k);
        switch (ev->eff) {
        case 1: ch->target = 0;       goto porta;   // portamento up (period -> 0)
        case 2: ch->target = 0x7ffff; goto porta;   // portamento down
        case 3: porta:                              // tone portamento
            if (count == 3) break;                  // quirk: no slide while the countdown is 3
            s = ev->par ? ev->par : ch->pspeed;  ch->pspeed = s;
            if (ch->period == ch->target) break;
            if (ch->period > ch->target) ch->period = max(ch->period - s, ch->target);   // signed
            else                         ch->period = min(ch->period + s, ch->target);
            set_freq(ch, ch->period);
            break;
        case 4:                                     // volume slide (unused by the tune; buggy)
            s = ev->par ? ev->par : ch->vslide;  ch->vslide = s;
            v = (s8)(ch->vol - (s & 15)); if (v < 0) v = 0;
            if ((s & 15) + (s >> 4) > 0x40) v = 0x40;   // never true: the up nibble is ignored
            ch->vol = v; set_volume(v);
            break;
        case 8:                                     // note delay
            if ((u8)(count - 1) == ev->par) { ch->period = ch->target; trigger(ch); }
            break;
        }                                           // 0, 5..7, 9..15: nothing per tick
    }
}

void trigger(ch)       { set_freq(ch, ch->period); start_sample(ch->ins); set_volume(ch->vol); }   // 0x20f2

void set_freq(ch, per)                              // 0x21d9
{   if (per == 0) per = 1;
    fc = ((u32)ch->ins->c2spd * 0x5adc / per) >> 11;     // signed idiv, operands positive here
    w16(0x01, fc & 0xffff); }

void start_sample(r)                                // 0x2205
{   w8(0x00, 3); w8(0x00, 3);                       // stop (twice)
    a = r->start + sample_offset; sample_offset = 0;
    waddr(0x0a, a); waddr(0x02, r->loop); waddr(0x04, r->end);    // waddr(reg,x): w16(reg,x>>7); w16(reg+1,x<<9)
    w8(0x00, r->loop ? 8 : 0); }                    // forward, 8-bit, loop on/off, no IRQ

void set_volume(v)                                  // 0x2112
{   bh = voltab[v]; if (bh <= 5) bh = 5; else if (bh >= 0xfa) bh = 0xfa;   // clamps never bite here
    w8(0x06, 0); w8(0x0d, 3);                       // ramp rate 0, ramp stopped
    bl = r16(0x89) >> 8;                            // CURRENT volume, bits 11..4 of the 12-bit value
    if (bl == bh) return;
    if (bl < bh) { w8(0x07, bl); w8(0x08, bh); w8(0x0d, 0x00); }    // ramp up to bh
    else         { w8(0x07, bh); w8(0x08, bh); w8(0x0d, 0x40); }    // ramp down to bh
    w8(0x06, 0x30); }                               // 48 volume steps per frame
```

Notes on the effects as used by the tune (effect: params × count): 1 porta up (1, 2, 3: 550), 2 porta
down (1, 4, 15, 16, 0x90: 373), 3 tone porta (0xdf: 206), 8 note delay (3: 130), 9 tempo (125: 5),
0xa speed (6: 5, 31: 1), 0xd sample offset (3: 122 → 768 bytes; 31: 226 → 7936 bytes), 0xf (0x11, 0x31,
0x41, 0x81, 0x91, 0xa1, 0xc1: 29) — **effect 0xf is ignored by the player** (they look like the module's
Exy extended commands — fine slides, retrigger, note cut — kept by the converter but never executed).
Quirks to keep (they are the sound of the intro): slides run on the row tick but skip the tick where the
countdown is 3; note delay `8 3` triggers on the 3rd tick of the row (countdown 4), i.e. 2 player ticks late,
not 3; an instrument without a note would retrigger at the current target period (does not occur); the
pending sample offset is global (in this tune every 0xd event carries a note and instrument — ins 1 with
0xd1f, ins 5 with 0xd03 — so it is consumed on the same channel). Tone portamento (0x3df) always comes with a
note and instrument 5: the note only sets the target, the instrument pointer changes without a retrigger.
Note delay (0x803) always comes with a note and instrument 2.

**Pitch**: GF1 increment = (FC >> 1) / 512 samples per frame (FC bit 0 unused), so the playback rate is
`(fc >> 1) / 512 * 38587.5 Hz`, ideally `c2spd * 428 / period` (note 0x40 = c2spd). The two truncations
flatten low notes by up to ~15 cents (note 0x40 at 8363: FC 221 → 8290 Hz); keep the exact integer FC.

**Volume**: voltab (vol 0..64): 20 90 98 a0 a8 b0 b4 b8 bc c0 c2 c4 c6 c8 ca cc ce d0 d1 .. df e0 e1 e1 e2 e2
.. ef ef f0. With the GF1 curve (section 7) this is ≈ linear: gain ≈ vol/64 × 0.5 (vol 64 → 0xf00 → 0.5 =
−6.0 dB, 32 → 0.242, 16 → 0.117, 1 → 0.0078; vol 0 → 0x200 → −84 dB, effectively silent).
A volume ramp at 48 steps/frame takes ≤ (0xf00−0x200)/48 = 70 frames = 1.8 ms.

## 6. Register-write sequences (for a C++ port that wants to diff against full.gus)
full.gus lines are `tick vN rXX val` for every register write except 0x43/0x44 (N = currently selected voice,
val = the byte for 0x345 writes, the word for 0x344 writes) and `tick pPPP val` for other ports (only
`p240 9`). Voice-select and DRAM-data writes are not logged. Per event:
- trigger: `r01 fc`, `r00 3`, `r00 3`, `r0a a>>7`, `r0b (a<<9)&0xffff`, `r02`, `r03`, `r04`, `r05`, `r00 8|0`,
  then the volume sequence;
- volume: `r06 0`, `r0d 3`, [`r07 lo`, `r08 bh`, `r0d 0|40`, `r06 30`] (bracket omitted if unchanged);
- porta step: `r01 fc`.
To reproduce full.gus, answer the 0x89 read with the last value written to reg 9 (always 0 after init): every
volume call then logs `r07 0, r08 bh, r0d 0, r06 30`. That is the emulator's behaviour, not the hardware's.

## 7. GF1 model the remake needs (what the hardware does with these writes)

Sources: GUS SDK 2.10 (ULTRADOC.TXT, sections 2.3, 2.6.2 and 2.16:
https://discmaster.textfiles.com/view/556/Experimental%20BBS%20Explossion%20III.iso/gus/gusdk210.zip/ULTRADOC.TXT),
DOSBox-X src/hardware/gus.cpp (https://github.com/joncampbell123/dosbox-x), DOSBox-staging
src/hardware/audio/gus.cpp (https://github.com/dosbox-staging/dosbox-staging), fetched 2026-11-10.
player_ref.py class `GF1` implements exactly this (`--wav out.wav`).

```c
// frame rate: 617400 / active voices (SDK: 1 / (1.619695497 us * voices)); reg 0x0e = 0xcf -> 16 -> 38587.5 Hz
// IRQ0 every 17045/1193182 s = 551.2352 frames: apply all register writes of one IRQ at that instant,
// carry the fractional frames (accumulator), then render frames until the next IRQ.
struct Voice {
    u8  ctl;        // reg 0: bit0 stopped, bit1 stop, bit3 loop, (bit2 16-bit, bit4 bidir, bit6 backwards: unused)
    u16 fc;         // reg 1
    s32 start, end, pos;   // regs 2/3, 4/5, a/b: ((hi & 0x1fff) << 16) | lo  = address * 512 + fraction
    u8  vctl, rate; // reg 0xd (bit0 stopped, bit1 stop, bit6 down), reg 6 (bits 7-6 bank, 5-0 increment)
    s32 vol, lo, hi;// reg 9: (data >> 4) * 512; reg 7/8: ((data & 0xff) << 4) * 512   (12-bit volume * 512)
    u8  pan;        // reg 0xc & 15
};
float gain(int v12) { return (256 + (v12 & 255)) * ldexp(1.0, (v12 >> 8) - 24); }   // 4-bit exp, 8-bit mantissa
// reads: reg 0x89 returns (vol >> 9) << 4 (the ramped current volume)

for each frame:
    L = R = 0;
    for (i = 0; i < active; i++) {              // all active voices, stopped ones too
        Voice *v = &voice[i];
        a = (v->pos >> 9) & 0xfffff;  f = v->pos & 511;
        s = (dram[a] + (dram[a+1] - dram[a]) * f / 512.0) / 128.0;   // signed 8-bit, linear interpolation always
        g = s * gain(v->vol >> 9);
        L += g * (15 - v->pan) / 15.0;  R += g * v->pan / 15.0;
        if (!(v->ctl & 3)) {                     // wave running
            v->pos += v->fc >> 1;
            if (v->pos > v->end) {
                if (v->ctl & 8) v->pos = v->start + (v->pos - v->end);   // loop (overshoot kept)
                else { v->pos = v->end; v->ctl |= 1; }                  // one-shot: stop, keeps playing DRAM[end]
            }
        }
        if (!(v->vctl & 3)) {                    // ramp running
            inc = ((v->rate & 63) * 512) >> (3 * (v->rate >> 6));      // bank 0: whole steps per frame
            if (v->vctl & 0x40) { v->vol -= inc; if (v->vol <= v->lo) { v->vol = v->lo; v->vctl |= 1; } }
            else                { v->vol += inc; if (v->vol >= v->hi) { v->vol = v->hi; v->vctl |= 1; } }
        }
    }
    output(L, R);       // 1.0 = one voice at full scale; this tune peaks at 0.962 -> map 1.0 to full scale
```
Register writes: 8-bit registers take the byte written to 0x345; a reset (reg 0x4c bit 0 = 0) stops all
voices and sets 14 active voices; reg 0x4c bit 1 = DAC enable. The remake can render at 38,587.5 Hz and
resample to the device rate (recommended: ramps, FC quantisation and interpolation are defined per GF1 frame),
or scale the per-frame increments to the device rate (small deviations).
DC: a one-shot voice ends parked on DRAM[end] (= its last sample byte) and keeps outputting it × volume
(instrument 6 ends at −22/128). The card's output is AC-coupled (guess), so a gentle DC blocker (~5–10 Hz
high-pass) on the final mix is reasonable; the DC itself is inaudible.

### 7.1 Approximations in the original (keep them; they are the sound)
- 8-bit samples, linear interpolation at 38,587.5 Hz with no band-limiting: high notes skip samples and
  alias (e.g. note 0x7b on instrument 2, the hi-hat-like hits on channel 8: FC 6645 → 6.49 DRAM bytes per
  frame, a 5 ms burst). A "cleaner" resampler would change the timbre; render the GF1 way, then resample the
  finished mix.
- Integer FC: `(c2spd·23260 / period) >> 11`, then the hardware drops FC bit 0 → notes up to ~15 cents flat.
- Volume steps: the channel volume (0..64) only reaches the GF1 through the 65-entry table (top 8 of the 12
  volume bits), and every change is a 48-steps-per-frame ramp (≤ 1.8 ms), never an instant jump.
- Event timing is quantised to the 70 Hz IRQ (player ticks at 50 Hz land on IRQ boundaries: jitter up to one
  IRQ, 14.3 ms). To be exact, apply the player's writes at the IRQ instants (every 551.2352 frames), not at
  ideal 50 Hz instants.

## 8. Validation (player_ref.py)

```
python3 player_ref.py                       # compares with full.dram / full.gus / full.ticks in this folder
  DRAM: first 34944 bytes identical; rest of the 1 MB all zero
  GUS log: 104499 lines, identical
  music counter: 12235 ticks compared, 0 differ
python3 player_ref.py --real-volume-read --out x   # hardware-faithful register stream: 83,735 lines
python3 player_ref.py --wav music.wav       # GF1 model render (numpy), real volume reads
```
Assumptions: music starts at tick 104 and stops at tick 12235 (both from the capture; `--music-start`,
`--music-stop`). Everything else is derived from paper_img.bin. With real volume reads the stream differs
from full.gus: 2,423 volume changes are ramp-downs (in full.gus: instant drops, because they are logged as
ramps up from 0 with the current volume already above the target) and unchanged volumes write nothing.
The render (`--wav`) correlates ≥ 0.99 per second with paper_music.wav for most of the tune (same notes,
timing, pitches); the differences are the ramp-downs and the pre-loop parts of instruments 3 and 5.

## 9. Review of gf1synth.py (do not edit; fixes described)

Correct: frame rate 617400 / max(14, active) with active = (reg 0x0e & 31) + 1 (16 → 38,587.5 Hz) and only
voices < active mixed; increment (FC >> 1)/512 (SDK: FC bits 15-1, bit 0 unused); 9-bit position fraction,
4-bit start/end fraction (SDK); 12-bit volume curve (256 + m)/512 · 2^(e−15) — this is the hardware's
exponent/mantissa curve (DOSBox-X's "original amplification routine"); DOSBox uses a pure exponential
(0.0235 dB steps), which differs by < 0.5 dB; ramp units (rate & 63)/8^(rate >> 6) 12-bit steps per frame and
ramp start/end << 4 (SDK: 63 steps/frame = full scale in 1.4 ms at 14 voices); ramps start from the current
volume; linear interpolation on every frame (SDK: always; DOSBox-staging skips it when the increment ≥ 1,
which is not hardware); linear pan law L = (15−p)/15, R = p/15 (= DOSBox-X's "accurate" pan table, attenuation
20·log10(15/(15−p)); DOSBox-staging uses constant power cos/sin — the SDK does not specify the law, so this
stays an open point, see section 10); 70.0018 Hz tick with a fractional frame accumulator.

Wrong, most important first:
1. **Loops wrap positions below the loop start** (`under = idx < s` in the loop branch, regardless of
   direction). A forward voice starts at the sample start, which is *below* the loop start, so instruments 3
   (738 bytes before the loop) and 5 (988 of 1206 bytes before the loop, also with offset 768) play loop
   material from the first frame and lose their attack. Verified with a one-note log: gf1synth plays the loop
   body from offset 102 instead of the sample start. Fix: for a forward voice only test `idx > e` (wrap to
   `s + (idx − e) mod L`); test `idx < s` only when bit 6 (decreasing) is set.
2. **Stopped voices are muted** (`if v.ctrl & 3: continue`, `alive` mask). The GF1 keeps mixing every active
   voice at its parked position (DOSBox-X: "the GUS is always rendering the audio sample at the current
   position, even if the voice is stopped"): a finished one-shot outputs DRAM[end] × volume. Fix: when stopped,
   mix the constant DRAM[pos] × vol; at the end of a one-shot clamp the position to `e` instead of zeroing.
   (Affects only DC and clicks.)
3. **Input log, not gf1synth itself**: full.gus never contains ramp-downs (run_paper.py's Gus returns the last
   written reg 9 for the 0x89 read, i.e. 0). gf1synth renders each volume decrease as an instant jump (ramp
   "up" with current > end → clamp). Fix: render from a log made with real volume reads
   (`player_ref.py --real-volume-read --out x` → x.gus), or make run_paper.py's Gus track ramps for reads.
4. Bidirectional loops (ctrl bit 4) fold positions without keeping the direction state (bit 6 is never
   toggled, so the next tick restarts forwards). Unused by this tune. Fix: per-frame stepping with
   direction flip at the boundaries (DOSBox: `pos = end − overshoot` and toggle bit 6).
5. The volume is advanced before it is used: frame 0 of a ramp uses vol + step (DOSBox: sample uses the
   current volume, then the ramp advances). Fix: `vol + step*k` for k = 0..n−1, clamp from k = 1. One frame.
6. 8-bit writes to 16-bit registers are taken as the low byte (the log's `r09 3f` at init means 0x3fxx).
   Harmless here (immediately overwritten). Fix: needs the access width in the log.
7. WAV header rate `round(38587.5)` = 38588 (13 ppm fast, 2 ms over the tune); the output is normalised to
   its peak (absolute level lost: the true mix peaks at 0.962 of one full-scale voice).
8. Volume ramp loops (reg 0xd bits 3/4) and 16-bit samples are not modelled (unused here).

## 10. Open points / guesses

- Exact GF1 hardware behaviour at ramp limits: the SDK warns of overshoot/oscillation near the rails; DOSBox
  clamps to the limit. The intro's ramps end at 0x20..0xf0 (far from the rails), so the clamp model is fine.
- Pan law (linear vs constant power): DOSBox-X's table from documentation says linear (what gf1synth and
  player_ref.py use); not verified on hardware.
- Volume curve: mantissa-linear (hardware formula quoted by DOSBox-X) vs exponential: < 0.5 dB difference.
- Interrupt-internal timing: writes within one IRQ are treated as simultaneous (a few µs on hardware; at most
  one GF1 frame of ramp progress between the two volume calls of a triggered note with a volume).
- The output stage (analog filtering, AC coupling, clipping) of a GUS is not modelled; the mix never exceeds
  one voice's full scale (peak 0.962), so clipping does not occur with unit scaling.
- Instrument record pointer 0 (a note before any instrument on a channel) would read garbage from code32 0;
  never happens in this tune. bpm 0 would divide by zero; never happens.
