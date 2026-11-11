# JIZZ — music (scope "music": the module expander 0x1632b, the MXM module, MXMPlay)

Addresses are offsets in `jizz_img.bin` (the unpacked program, base 0). "IRQ k" = the k-th run of MXMPlay's
IRQ0 handler after xmpPlay (k = 1 is the first, 0x10000 PIT clocks after the timer is installed). P = 19,759 PIT
clocks = the player tick (151 BPM). PIT clock 1,193,182 Hz. The capture's log ticks: IRQ k = `full.gus` /
`full.ticks` tick 433 + k.

Everything below was checked, not only read (scripts in `jizz/re/models/music/`, see its README):

- the expander: `mp4synth.py` (Python, x87 24-bit rounding emulated) reproduces `dump/smokeout.mxm` byte for
  byte (928,657 written bytes + 11 zero bytes); `emu_synth.py` runs the original routine alone in unicorn;
- the player: `mxmplay.py` (a routine-by-routine port of MXMPlay, from the module + `jizz_img.bin`) reproduces
  `full.dram` (1 MB, identical), `full.gus` (685,612 register writes, identical line by line) and the music
  position of `full.ticks` (8,923 ticks, identical). Also when the module comes from `mp4synth.py` instead of the
  dump, i.e. from `jizz_img.bin` alone;
- the song is rendered to a WAV through a GF1 model (section 7); there is no audio capture to compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- The module is **synthesised**, not decompressed. Probe's "mp4 cruncher" (0x1632b) copies 32,625 stored bytes
  (MXM header, 37 instruments, 17 patterns, 14,037 bytes of 8-bit samples for instruments 1–5: drums and two
  short tones) and appends 896,032 bytes of samples it renders itself: 12 synth records × 1..8 passes = 32
  "phrase" samples (MXM instruments 6..37), each 16 notes of an oscillator (saw, x^7-shaped, or noise) through a
  swept 2-pole resonant filter, with glides, fade-outs, a 3-note feedback echo, decimated by 2 to 8-bit. All of it
  in x87 floating point at 24-bit precision; the exact bytes depend on that rounding.
- The player is a **cut-down MXMPlay** (Niklas Beisert's GUS XM player, 32-bit asm): 14 channels on 14 GF1
  voices (44,100 Hz), linear frequencies, volume envelopes, fadeout, and only the effects this song needs:
  8xx, Axy, Cxx, Dxx, Fxx, E1x, E2x, EEx, volume column set volume / set panning. Everything else (porta, tone
  porta, vibrato, arpeggio, offset, jumps, retrig (E93 is in the song but does nothing), note delay, global
  volume, ...) points to a `ret`.
- Timing: IRQ0 every P = 0x2d86bf / 151 = 19,759 PIT clocks (60.3868 Hz), one player tick per IRQ, BPM never
  changes. Each IRQ first writes the previous tick's results to the GUS, then computes the next tick: **a row is
  heard one tick (16.56 ms) after xmpGetPos shows it**. Sound starts at xmpGetTimer = 0 (IRQ 2).
- The intro exits when the music reaches 15:1c: IRQ 8,923, 147.748 s after IRQ 1 (= `jizz_reference.mp4`
  frame 0), xmpGetTimer 176,270,039 = 147.731 s. The song itself would wrap to order 0 at IRQ 10,039 (166.2 s);
  order 21's rows 0x17.. are only the tail of the last notes at speed 31.
- The capture's fake GUS returns the last written reg 9 for the volume read; real hardware returns the ramped
  volume. Same code, different register stream (section 6): 685,612 writes in the capture, 433,593 on hardware.

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when | what |
|---|---|
| main 0x18aaa (log tick 1) | copies 0x7f71 = 32,625 bytes from 0x1f644 to the music buffer [0x6546] (1 MB, zeroed by main); [0x15e14] = end of the copy |
| 0x18ac2 → 0x1632b (ticks 1–48) | "extracting music:": synthesises 32 × 28,001 bytes behind the copy (section 2); progress 0x1115c once per pass and once per record (44 steps); `/mp4` writes the module (0xe2b9c = 928,668 bytes) as smokeout.mxm |
| 0x18b35 / 0x18b3a | EE02 (EDI = environment) and 0x2a2cd ULTRASND parser → port 0x2x0 or 0 |
| 0x18b60 (tick 48) | xmpInit(ESI = module, EAX = port, EBX = [0x6546] + 0xfc000 data block, ECX = 0x10000): sample upload. Port 0 or failure: "error! -> switching to nosound", xmpInit again with EAX = 0 (0x18b95; the player then runs without GUS writes) |
| 0x18be9 (main line in tick 434, logged 433) | xmpPlay(0): GF1 reset, player state, timer install (PIT 0 = 65536 clocks) |
| IRQ 1 (log 434), 23.930 s | first handler run: GUS update (nothing playing: 14 × voice off), player tick 0 reads row 00:00. xmpGetTimer = -P after it. `jizz_reference.mp4` starts here |
| IRQ 2 (log 435) | xmpGetTimer = 0: row 00:00 written to the GUS, first sound |
| IRQ k | GUS update with tick k-1's results, then tick k-1... (xmpGetTimer = (k-2)·P at IRQ k) |
| IRQ 8,923 (log 9356) | the position becomes 15:1c; the frame loop sees it at its next frame start, 0x18c59 xmpStop (restores int 8, PIT 0, GF1 reset to 14 voices, mix control 8) |

The music is driven by IRQ0 only. Nothing in it depends on the frame rate; the only machine-dependent quantity
is when xmpPlay is called (after the CPU-bound precalc).

---------------------------------------------------------------------------------------------------------------

## 2. The "mp4 cruncher" 0x1632b: sample synthesis

### 2.1 Data and variables

| what | where | format |
|---|---|---|
| stored module part | 0x1f644, 0x7f71 = 32,625 bytes | MXM bytes 0..0x7f70 as they are (header, instruments, patterns, samples of instruments 1–5) |
| synth records | 0x16423, 12 × 0x7b bytes (to 0x169e7) | below |
| record order | 0x163f3, 12 dwords | record number + 1, in output order: 11 12 9 10 6 7 4 8 1 3 5 2 |
| constants | 0x15dc6 qword 3529.0 (note length − 1); 0x1605f qword 1.02 (detune); 0x16015 qword 32767.0; 0x1628b float 4.0 | |
| work buffer | [0x15dce] = main's buffer of 0x10000 bytes (zeroed by main, 0x188b7) | 8-bit signed samples |
| random generator | 0x141f6 (core scope), state 0x141ea/ee/f2, 0 in the image | the precalc 0x11dfe calls it 80 times before; record 10 (noise) calls it 56,480 times |

Record (copied to 0x15e18 for the duration of one record; offsets in the record / address of the copy):

| ofs | copy | type | meaning |
|---|---|---|---|
| 0x00 | 0x15e18 | double | A0: filter sweep start (multiplied by MUL at every note-on, kept for the next) |
| 0x08 | 0x15e20 | double | B0: filter floor (also multiplied by MUL at every note-on) |
| 0x10 | 0x15e28 | double | R: per-sample decay factor of the sweep |
| 0x18 | 0x15e30 | double | Q: damping |
| 0x20 | 0x15e38 | u8 | oscillator: 0 saw, 2 x^7-shaped, 4 noise |
| 0x21 | 0x15e39 | u8 | passes − 1 |
| 0x22 | 0x15e3a | double | MUL |
| 0x2a | 0x15e42 | double | GAIN (oscillator → filter) |
| 0x32 | 0x15e4a | double | FB: echo feedback |
| 0x3a | 0x15e52 | u8 | HARM: 0 = one oscillator; else a second, detuned one (and, in mode 2, the "partial" of 0x15f55) |
| 0x3b | 0x15e53 | u16[16] | pitch of each note (phase increment per sample, 16-bit phase) |
| 0x5b | 0x15e73 | u8[16] | 1 = this note's filter coefficients × 4 |
| 0x6b | 0x15e83 | u8[16] | TIE: 1 = glide from the previous note, no note-on (filter state continues) |

The code reads these arrays with an index of 16 too: X4[16] = TIE[0], PITCH[16] = X4[0] | X4[1] << 8,
TIE[16] = the code byte at 0x15e93 (0xdd, non-zero). Every record has TIE[0] = 0, so only X4[16] (= 0) and TIE[16]
matter.

Variables: phase [0x15dd6] u16, phase2 [0x15dd8] u16, D [0x15dda] double, A [0x15de2] double, B [0x15dea] double,
pitch [0x15df2] s32, s [0x15df6] s32, y1 [0x15dfa] s32, k [0x15dfe] u16 (samples since note start, for the
glide), seg [0x15e00] u16 (note 0..15, 16 after a pass, 0 in the image), j [0x15e02] u16 (sample in the note),
Qc [0x15e04] double, Rc [0x15e0c] double, cnt [0x15dd2] u32 (write index, 0 in the image), out [0x15e14].

### 2.2 Algorithm

All floating point is x87 with the precision control at 24 bits (main 0x18712) and rounding to nearest even:
every arithmetic result below is rounded to a 24-bit mantissa (written `r(...)`); loads of doubles and integers
are exact; `fist` = fistp, round to nearest even. With 53- or 64-bit precision the output differs (3,716 /
85,395 bytes, checked in unicorn).

```c
void expand(void)                                         // 0x1632b, runs once ([0x163eb] counts 0..11)
{
    for (it = 0; it < 12; it++) {
        rec = order[it] - 1;                              // dword [0x163f3 + 4*it] - 1
        phase = 0; phase2 = 0;
        A = B = Qc = Rc = D = 0.0;
        R = record[rec];                                  // 0x7b bytes -> 0x15e18 (A0, B0 change below)
        for (pass = 0; pass <= R.npass; pass++) {         // [0x163e6]
            note_on();                                    // with seg = 16 (0 for the very first): an extra
                                                          // MUL step of A0 / B0 each pass
            for (seg = 0; seg < 16; seg++) {              // 0x1628f
                if (R.tie[seg] == 0) note_on();
                k = 0;
                for (j = 0; j <= 3529; j++) sample(j);    // 0x161d1 / 0x1601d: 3530 samples per note
            }
            seg = 16;
            progress();                                   // 0x1115c (text only)
            echo();                                       // 0x15ede
            if (rec == 7)                                 // 0x162cd: byte [0x2000] = record number
                for (i = 0; i < 58000; i++) W[i] = (s8)W[i] >> 1;
            for (i = 0; i <= 28000; i++) W[i] = W[2*i];   // decimate by 2, no filter
            cnt = 0;
            append(W, 28000); append(W[27999], 1);        // rep movsd 0x1b58 dwords, then [esi-1] again
        }
        progress();
    }
}

void note_on(void)                                        // 0x161f2
{
    y1 = 0; phase = 0x8002;
    R.B0 = r(R.MUL * R.B0); B = R.B0;                     // MUL and the first B0 / A0 are full doubles
    R.A0 = r(R.MUL * R.A0); A = R.A0;
    Qc = R.Q; Rc = R.R;
    if (R.x4[seg] == 1) { B *= 4; A *= 4; }               // fmul dword 4.0
    pitch = R.pitch[seg];
}

void sample(int j)                                        // 0x1601d
{
    s = (s16)phase;                                       // sawtooth
    if (R.harm) {
        phase2 += fist(r(pitch * 1.02));                  // 16-bit wrap
        s += (s16)phase2;
    }
    if (R.mode == 2) s = shaped();
    else if (R.mode == 4) s = (rand() & 0xffffff) % 64001 - 32000;
    if (seg != 0 && R.tie[seg] == 1) {                    // glide over 0x6e4 = 1764 samples
        if (k < 0x6e4) {
            u = r(r(3529 - 2*k) / 3529.0);  u = r(u * R.pitch[seg-1]);
            v = r(r(2*k / 3529.0) * R.pitch[seg]);
            pitch = fist(r(u + v));
        } else pitch = R.pitch[seg];
    }
    phase += pitch;                                       // 16-bit (add word, low word of pitch)
    s = fist(r(s * R.GAIN));
    s = filter(s);                                        // 0x15e93
    cnt++;
    y1 = s;
    if (R.tie[seg + 1] == 0)                              // next note not tied: linear fade-out
                                                          // (TIE[16] = 0xdd: the 16th note never fades)
        s = fist(r(r(1 - r(j / 3529.0)) * s));
    s = clamp(s, -0x7fff, 0x7fff);
    W[cnt] = (s >> 8) & 0xff;                             // byte 1 of the dword: W[1..56480]
    k++;
}

int filter(int x)                                         // 0x15e93: 2-pole resonator, swept
{
    A = r(A * Rc);                                        // Rc is a full double
    c = r(A + B);
    D = r(r(1 - Qc) * r(D - r(c * r(x + y1))));           // 1 - Qc: Qc a full double
    return fist(r(D + y1));                               // y = y1 + D
}

int shaped(void)                                          // 0x15f55 (mode 2)
{
    for (n = 0; ; ) {
        s = fist(sh(phase));                              // the base is recomputed (and the sum dropped)
        if (R.harm) {                                     //   on every pass of the loop
            e = (u32)((u64)(u32)(cnt * n) * pitch >> 7);  // mul ebx; mul dword [pitch]; shrd eax, edx, 7
            if (!(n & 1)) e = -e;
            phase2 = e + phase;                           // 16 bit
            s = fist(r(sh(phase2) + s));
        }
        if (++n > R.harm) break;                          // signed byte compare
    }
    return s;                                             // = base + partial of the last n (= HARM)
}
float sh(u16 p)                                           // 32767 * t^7, t in [-1, 1)
{
    t = (s16)p > 0 ? 32767 - (s16)p : (s16)p;  t = r(t / 32767);
    t2 = r(t*t); t4 = r(t2*t2); t6 = r(t2*t4);  return r(r(t6 * 32767) * t);
}

void echo(void)                                           // 0x15ede, in place over W[0..56464]
{
    for (e = 0; e < 16; e++) for (bx = 0; bx <= 3529; bx++) {    // index e*3529+bx: note borders done twice
        i = e*3529 + bx;
        d = e >= 3 ? (s8)W[i - 10587] : (s8)W[i + 45877];        // 3 notes back (wet), or 13 notes ahead (dry)
        a = (s8)W[i] + fist(r(d * R.FB));
        W[i] = clamp(a, -127, 127);
    }
}
```

W[0] is never written by `sample`; it keeps the previous pass's decimated W[0] (after its echo), so passes are
not independent. Positions 56,481..57,999 are only touched by the halving of record 7.

Exact rounding for a remake port (C++ with SSE floats, x64): every `r(a op b)` with two 24-bit operands is a
plain `float` operation (`float * float`, `float + float`, int → float for |int| < 2^24, `float / 3529.0f`).
The operations with a full double operand must be rounded once to float, not double-then-float: `MUL * A0`,
`MUL * B0`, `A * Rc`, `s * GAIN`, `pitch * 1.02`, `d * FB` (and `1 - Qc`). Use the fma test of `mp4synth.py`
(`mul24x`): p = x*y; f = (float)p; if p is exactly halfway between f and its float neighbour g, decide with
e = fma(x, y, -p). Rounding these via double gives 2,575 wrong bytes (823 of 2.2 M products); `1 - Qc` via
double happens to be harmless for these records. Compile without FMA contraction / fast-math; `fist` = `nearbyint`.

Time base: none (runs once at start-up). The generator: replay 0x141f6 from state (0,0,0), 80 calls (precalc
0x11dfe), then the 56,480 calls of record 10.

### 2.3 What the records make

The output order and the MXM instruments the passes become (all one-shot, 8-bit, 28,000 bytes + 1 pad byte; each
is a 16-note phrase of 1,765 bytes per note):

| it | record | passes | MXM instruments | oscillator | notes |
|---|---|---|---|---|---|
| 0 | 10 | 1 | 6 | noise | one note, 15 ties (filter sweep, gain 0.4, FB 0.6) |
| 1 | 11 | 2 | 7, 8 | saw | one note + 15 glides (R > 1: the filter opens) |
| 2 | 8 | 1 | 9 | saw + detuned saw | |
| 3 | 9 | 1 | 10 | saw + detuned saw | |
| 4 | 5 | 2 | 11, 12 | saw + detuned saw | 16 separate notes |
| 5 | 6 | 2 | 13, 14 | saw + detuned saw | same notes as record 5, other filter |
| 6 | 3 | 4 | 15–18 | saw | |
| 7 | 7 | 1 | 19 | saw + detuned saw | one note + 15 ties; halved (sar 1) |
| 8 | 0 | 4 | 20–23 | saw + detuned saw | |
| 9 | 2 | 4 | 24–27 | saw + detuned saw | |
| 10 | 4 | 8 | 28–35 | x^7-shaped (HARM 3) | |
| 11 | 1 | 2 | 36, 37 | saw + detuned saw (HARM 6) | one note + 15 glides |

Most phrases are played at note 62 (C#-5, FC 410, 17,657 Hz): a synthesised note of 1,765 bytes then lasts
0.0999 s, one row at speed 6 (0.0994 s). The phrases are pre-rendered bars with one note per row, running 0.6 %
slower than the rows; the song restarts them every 16 rows.

The filter is a resonator: with velocity D and position y, D' = (1−Q)(D − c(x + y)), y' = y + D', i.e.
y'' ≈ −c(y + x) − Q·y': it follows −x through a resonant low-pass at about √c radians per sample, damping Q;
c = A + B with A decaying by R per sample (a cutoff sweep per note, restarted at note-ons) and the floor B.

### 2.4 Shortcuts in the expander

The sample data is part of the module the player plays: a remake that wants the original's sound must generate
the same bytes (8 bit, decimated by 2 without an anti-alias filter, echo computed on 8-bit values with clamping).
A "sharper" variant could keep the 16-bit filter output and skip the decimation (rendering the 16 notes at the
synth rate and playing them at half the pitch increment), but the echo, the clamps and the aliasing are part of
the timbre; that is a choice for the remake, not something the original does.

---------------------------------------------------------------------------------------------------------------

## 3. MXM format (as this player reads it; `models/music/mxm.py`)

```
0x000 'MXM\0'      0x004 u32 orders (22)       0x008 u32 restart (0)        0x00c u32 channels (14)
0x010 u32 patterns (17)  0x014 u32 instruments (37)  0x018 u8 speed (6)  0x019 u8 BPM (151)
0x01a u16 options: bit 0 linear frequencies (the player assumes it), bit 1 notes do not reset panning (0x1: off)
0x01c u32 sample data offset (0x489c)   0x020 u32 8-bit sample bytes (910,080)   0x024 u32 16-bit words (0)
0x028 s32 lowest pitch (-18432)   0x02c s32 highest pitch (24576)       (clamp of E1x / E2x only)
0x030 u8[32] initial channel panning 0..255 (all 128)
0x050 u8[256] order list: 2 3 0 0 4 4 0 0 6 7 7 5 9 10 11 12 13 14 15 14 15 16
0x150 u32[128] instrument offsets (the player indexes instrument i at 0x14c + 4i), 0x350 u32[256] pattern offsets
instrument: +0 u32 samples, +4 u8[96] sample per note, +0x64 u16 fadeout, +0x66 4 bytes (unused here),
  +0x6a u8 index of the last volume envelope point (0 = none), +0x6b sustain point, +0x6c loop start point,
  +0x6d loop end point, +0x6e {u16 ticks to the next point, u16 value 0..64}[], +0x100 sample headers (16 bytes):
  +0 u24 DRAM start, +3 u24 DRAM loop start, +6 u24 DRAM end (byte addresses), +9 u8 GF1 voice control
  (bit 3 loop, bit 4 bidirectional, bit 2 16 bit), +0xa u8 volume, +0xb u8 panning, +0xc s16 relative
  note * 256 + finetune (1/256 semitone), +0xe unused
pattern: u32 rows, then per row entries (flag: bits 0-4 channel, 0x20 note + instrument, 0x40 volume byte,
  0x80 effect + parameter), increasing channels, ending with 0
effects: XM 0..0x23; Exy = 0x24 + x with parameter y; note 97 (0x61) = key off
```

JIZZ's module: instruments 1–5 are the stored samples (DRAM 0..0x36d4: 956, 2,745 (bidirectional loop),
2,274, 6,685 (bidirectional loop), 1,315 bytes; relative notes −24, −12, −12, 0, 0), with volume envelopes on 2,
4 and 5 (to 0), fadeout 1568 (4), 697 (5), 128 (all others; it only acts after a key off, and there is none);
instruments 6–37 are the synthesised passes (DRAM 0x36d5 +
28,001·(i−6), 28,000 bytes, no loop, no envelope). No key-offs and no note delays in the song.

---------------------------------------------------------------------------------------------------------------

## 4. MXMPlay (0x296dc..0x2a316)

### 4.1 Routines

| address | routine |
|---|---|
| 0x296dc | `ebp = [0x296d8]`: the data block (set by xmpInit; every entry point starts with it) |
| 0x296e7 | volume table (4.10) |
| 0x29738 | xmpInit |
| 0x297a8 | pitch → frequency (4.9) |
| 0x29801 | new note (row tick, per channel) |
| 0x299ae / 0x299b9 | xmpSetVolume (byte +1 = AL) / xmpGetSync (AL = byte +2): never called |
| 0x299c4 | xmpGetPos: EAX = order << 8 \| row (bytes +0x12, +6) |
| 0x299d4 | clamp pitch to [lowest, highest] |
| 0x299f1..0x29af6 | effect handlers (4.8); 0x299f1 = `ret` |
| 0x29b10 | dispatch: `jmp [0x2a3f7 + 4*eax] + 0x299f1` |
| 0x29b26 | player tick |
| 0x29db1 / 0x29e22 | xmpPlay / xmpStop |
| 0x29e34 | 4 unreferenced bytes 77 19 09 17 |
| 0x29e38 | IRQ0 handler; 0x29ed1 install, 0x29f2a remove |
| 0x29f44 | xmpGetTimer |
| 0x29f63.. | port I/O: 0x29f63 out(base+dx), 0x29f6b w8 (reg → base+0x103, byte → base+0x105), 0x29f83 w8 + delay + same byte again, 0x29fa5 w16 (word → base+0x104), 0x29fbb r8, 0x29fd2 r16, 0x29fe8 delay (7 reads of base+0x107), 0x29ffb voice select (base+0x102), 0x2a00b address pair |
| 0x2a02a | GF1 reset; 0x2a23f GUS start, 0x2a25e GUS stop |
| 0x2a0b6 / 0x2a11c | set volume (ramp) / voice off (ramp down) |
| 0x2a13c | GUS update (start of every IRQ tick) |
| 0x2a26f | sample upload |
| 0x2a2cd / 0x2a310 | ULTRASND parser / int 31h EE02 |
| data 0x2a317 | 64-byte quarter sine (0..64): vibrato table of the full player, unreferenced here |
| data 0x2a357.. | frequency tables (4.9) |
| data 0x2a3f7 | jump table, 0x88 dwords (the last 8 read past the program end: BSS zeros = `ret`) |

### 4.2 Data block (EBX of xmpInit = [0x6546] + 0xfc000, 0x4000 bytes zeroed by xmpInit)

```
+0x00 u8 global volume (0x40)        +0x01 u8 master volume (0x40)     +0x02 u8 sync (always 0)
+0x03 u8 tick in row                 +0x04 u8 speed                    +0x05 u8 1 on the row tick
+0x06 u32 row                        +0x0a pattern read pointer        +0x0e u32 rows in pattern
+0x12 u32 order                      +0x16 u32 next order (-1 = none)  +0x1a u32 next row
+0x1e u8 pattern delay               +0x1f..0x24 note, ins, vol, eff, par, 0 of the event being read
+0x26 old int 8 (offset, +0x2a selector)   +0x2c saved ss:esp
+0x32 u32 BIOS divider (byte +0x34)  +0x36 u32 current PIT period      +0x3a u32 timer
+0x3e u32 busy                       +0x42 u32 tick period (PIT clocks) +0x46 u32 remaining clocks of the tick
+0x4a ds                             +0x4c u32 max PIT period (0x10000) +0x50 u32 GUS port (0 = nosound)
+0x54 u32 GF1 voices (max(channels, 14))   +0x58 u8 "tone porta" flag  +0x59 u8 key-off flag
+0x5a..0x7a9  module bytes 0..0x74f (pointers fixed up)
+0x7aa u16[257] volume table         +0x9ac 32 channel records of 0x100 bytes   +0x29ac..0x2dac IRQ stack
```

Channel record (+0x9ac + 0x100·ch; channel ch always plays on GF1 voice ch):

```
+0x00 active   +0x01 u24 start   +0x05 u24 end   +0x09 u24 loop start   +0x0d GF1 voice control
+0x0e stop request   +0x0f new sample (write loop/end)   +0x10 s32 restart offset (-1 = none)
+0x14 u16 GF1 frequency   +0x16 u16 GF1 volume   +0x18 u8 GF1 pan 0..15
+0x19 volume 0..64  +0x1a volume this tick  +0x1b panning 0..255  +0x1c panning this tick
+0x1d s32 pitch  +0x21 s32 pitch this tick  +0x25 instrument  +0x26 instrument pointer  +0x2a u16 relnote/fine
+0x2c key on  +0x2d u16 fade (0x8000 = full)  +0x31 u32 envelope point  +0x35 u16 envelope tick
+0x3d sample volume  +0x3e sample panning  +0x3f effect (0xff none)  +0x40 volume-column command
+0x41 s32 porta target (written, never used)  +0x49 volume slide memory  +0x57 / +0x58 E1x / E2x memory
+0x69 retrigger interval (read by E9x, never written)  +0x6d note
(+0x2f, +0x30, +0x37, +0x3b, +0x5c, +0x60, +0x63, +0x64, +0x65, +0x6a, +0x70: cleared, never used)
```

Pitch unit: 1/256 semitone; pitch = 0x3000 − ((note−1)·256 + relnote_finetune) as a signed 16-bit value
(note 49 = C-4 → 0 at relative note 0); a higher pitch value is a lower tone.

### 4.3 xmpInit (0x29738) and the sample upload (0x2a26f)

```c
int xmpInit(u8 *mod /*esi*/, int port /*eax*/, u8 *D /*ebx*/, int maxper /*ecx*/)
{
    [0x296d8] = D; memset(D, 0, 0x4000);
    D.maxper = maxper; D.mastervol = 0x40; D.port = port;
    if (*(u32 *)mod != 'MXM\0') return 0;
    memcpy(D + 0x5a, mod, 0x750);
    for (i = 0; i < 0x180; i++) ((u32 *)(D + 0x1aa))[i] += (u32)mod;     // instrument + pattern offsets
    upload(mod + hdr.smpstart, hdr.smp16 * 2 + hdr.smp8);                   // 910,080 bytes
    voltab();
    return 1;
}
void upload(u8 *src, u32 n)                                                 // 0x2a26f
{
    if (!port) return;
    if (in(port+0x103) == 0x45 && in(port+0x102) == 0x1e && in(port+0x107) == 0x2a) return;  // "already there"
    for (a = 0, blocks = n >> 4; blocks; blocks--) {                        // n & 15 bytes are dropped
        cli; w8(0x44, a >> 16); select reg 0x43;
        do { outw(port+0x104, a & 0xffff); out(port+0x107, *src++); a++; } while (a & 15);
        restore flags;
    }
}
```

DRAM layout: the module's sample block, byte for byte, from DRAM address 0 (0..0xde2ff; the rest zero). The
sample headers hold DRAM addresses directly (the converter laid them out). The "already uploaded" test reads the
register-select and voice-select ports and one DRAM byte (a hook for a loader that pre-loads the GUS; the capture's
fake GUS fails it). No other GUS register is written by xmpInit.

ULTRASND (0x2a2cd, EDI = environment): scans the NUL-separated strings for "ULTRASND", skips an entry whose
14th character is '@', requires "=2" at +8 and "0," at +0xb, port = 0x200 | (char at +0xa) << 4 (low byte):
"ULTRASND=240,…" → 0x240. IRQ / DMA are ignored (no GUS IRQ, no DMA: PIO only).

### 4.4 xmpPlay (0x29db1), GF1 reset (0x2a02a), xmpStop (0x29e22)

```c
void xmpPlay(int ord)
{
    D.nextorder = D.order = ord; D.row = 0;
    if (port) { gf1_reset(channels); select(0); out(port, 9); }             // 0x2a23f; mix control 9
    memset(D + 0x9ac, 0, 0x2000);
    for (ch = 0; ch < 32; ch++) C[ch].pan = hdr.panpos[ch];
    D.globalvol = 0x40; D.nextrow = 0; D.sync = 0;
    D.speed = hdr.speed; D.tick = hdr.speed - 1;                            // first IRQ reads row 0
    D.period = 0x2d86bf / hdr.bpm;                                          // 2,983,615 / 151 = 19,759
    install_timer();
}
void gf1_reset(int n)                                                       // 0x2a02a
{
    D.voices = (u8)n < 14 ? 14 : n;
    w8(0x4c, 0); delay; delay; w8(0x4c, 1); delay; delay;                    // reset, run
    w8(0x0e, (D.voices - 1) | 0xc0);                                        // 14 voices: 617400/14 = 44,100 Hz
    for (v = 0; v < 32; v++) { select(v); w16(0x09, 0); w8d(0x00, 3); w8d(0x0d, 3); w8(0x06, 0x3f); delay; }
    w8(0x4c, 7);                                                            // run, DAC on, GF1 IRQs on
    select(0); out(port, 8);
}
void xmpStop(void) { remove_timer(); if (port) gf1_reset(14); }             // 0x29f2a, 0x2a25e
```

The volume ramp rate (reg 6) is 0x3f for every voice and never changes: 63 volume steps (of 4096) per GF1 frame.

### 4.5 Timer: install, IRQ0, xmpGetTimer, xmpGetPos

```c
void install_timer(void)                                                    // 0x29ed1
{
    D.ds = ds; D.remain = 0; D.biosdiv = 0; D.busy = 0;
    D.cur = 0x10000;
    D.timer = 0xffff0000 - D.period;                                        // = -0x10000 - P
    old = get int 8 vector (int 31h 0204); set int 8 = 0x29e38 (0205);
    out(0x43, 0x34); out(0x40, 0); out(0x40, 0);                             // mode 2, 65536 clocks
}
void irq0(void)                                                             // 0x29e38
{
    D.timer += D.cur;  D.biosdiv += D.cur;
    if (byte(D.biosdiv >> 16)) { clear that byte; pushf; call far old; }   // BIOS clock every 65536 clocks
    out(0x20, 0x20);
    edx = D.remain - D.maxper;  if (!(D.remain > D.maxper)) edx = D.period;  // unsigned; maxper = 0x10000
    D.remain = edx;  D.cur = min(edx, D.maxper);
    out(0x43, 0x34); out(0x40, D.cur & 0xff); out(0x40, D.cur >> 8);
    if (D.remain == D.period && !D.busy) {                                  // a new player tick
        D.busy = 1; switch to the stack at D + 0x2dac; sti;
        gus_update();                                                       // 0x2a13c: last tick's results
        player_tick();                                                      // 0x29b26
        cli; restore the stack; D.busy = 0;
    }
}
int xmpGetTimer(void) { latch PIT 0; return D.timer + D.cur - counter; }  // 0x29f44
int xmpGetPos(void)   { return (u8)D.order << 8 | (u8)D.row; }              // 0x299c4
```

With P = 19,759 < 0x10000 every IRQ starts a tick: IRQ 1 comes 65,536 clocks after the install with timer = −P,
then IRQ k at timer (k−2)·P. xmpGetTimer = (k−2)·P + clocks since IRQ k: PIT clocks since the first sound,
continuous, negative during the first tick. (A latch read after the PIT reloaded but before the IRQ is served would
read P too little; not modelled.) Fxx ≥ 0x20 would change D.period (0x29a24); the song only uses F06 / F0C / F1F
(speed), so the period is 19,759 throughout: 60.3868 player ticks per second. (0x2d86bf = 2.5 × 1,193,446, a
little more than 2.5 × the PIT clock: MXMPlay's 125 BPM = 23,868 clocks = 49.99 Hz.)

### 4.6 GUS update (0x2a13c), at the start of every tick

```c
if (!port) return;
for (ch = 0; ch < channels; ch++)                     // 1. stop voices that get a new note
    if (C.active && C.stop) { select(ch); w8d(0x00, C.mode | 3); voice_off(); C.stop = 0; }
for (ch = 0; ch < channels; ch++) { select(ch); while (!(r8(0x8d) & 1)); }   // 2. wait for all volume ramps
for (ch = 0; ch < channels; ch++) {                   // 3.
    select(ch);
    if (!C.active) voice_off();
    else {
        if (C.newsmp) { waddr(0x02, C.loop); waddr(0x04, C.end); }
        if (C.offset != -1) {
            a = C.start + C.offset;  if (a >= C.end) a = C.end - 1;
            waddr(0x0a, a);
            w8(0x00, (r8(0x80) & 0x40) | C.mode);     // start (keeps the direction bit: 0 after the stop)
        }
        r8(0x80);                                     // the code then tests BL (= 0x80), not AL: never true,
                                                      // so the "voice ended -> voice off" branch is dead
        w8(0x0c, C.gpan); w16(0x01, C.gfreq); setvol(C.gvol);
    }
    C.newsmp = 0; C.offset = -1;
}
waddr(reg, a): w16(reg, (a >> 7) & 0x1fff); w16(reg + 1, (a << 9) & 0xffff);   // 0x2a00b (address << 9)
voice_off():   w8(0x07, 0x04); w8(0x08, 0xfc); w8d(0x0d, 0x40);                // 0x2a11c: ramp down to 0x0400
```

Stopping a voice freezes it at its position (the GF1 keeps outputting that DRAM byte × volume) and ramps the
volume down (≤ 63 frames, 1.4 ms); step 2 busy-waits inside the interrupt until every ramp has ended, so on
hardware the new notes start up to 1.4 ms after the IRQ (in the GF1 model: up to 60 frames, 52 on average over
the 954 IRQs that stop a voice). A channel never becomes inactive again: a finished one-shot keeps its last byte ×
volume (DC) until its next note.

### 4.7 Player tick (0x29b26)

```c
void player_tick(void)
{
    D.newrow = 0;
    for (ch) { C.fvol = C.vol; C.fpan = C.pan; C.fpitch = C.pitch; }
    if (++D.tick == D.speed) {
        D.tick = 0;
        if (D.patdelay) D.patdelay--;                          // EEx: repeat the row's tick effects
        else {
            D.newrow = 1; D.row++;
            if (D.nextorder == -1 && D.row >= D.rows) { D.nextorder = D.order + 1; D.nextrow = 0; }
            if (D.nextorder != -1) {
                (if D.nextorder != D.order: clear +0x63 / +0x64 of every channel)
                D.order = D.nextorder < hdr.orders ? D.nextorder : hdr.restart;
                D.row = D.nextrow; D.nextorder = -1;
                p = pattern[hdr.order[D.order]]; D.rows = *(u32 *)p; p += 4;
                for (; D.nextrow; D.nextrow--) skip one row of p;
                D.patpos = p;
            }
            p = D.patpos;
            for (ch = 0; ch < channels; ch++) {
                note = ins = vol = eff = par = 0; C.eff = 0xff;
                if (*p && (*p & 0x1f) == ch) { f = *p++; if (f & 0x20) note, ins = *p++, *p++;
                    if (f & 0x40) vol = *p++; if (f & 0x80) eff, par = *p++, *p++; }
                new_note(ch);
                C.volcmd = vol >> 4; vol &= 15; fx[0x68 + C.volcmd]();          // volume column, row tick
                if (eff < 0x34) { C.eff = eff; fx[eff](); }                    // effect, row tick
            }
            D.patpos = p + 1;
        }
    }
    for (ch = 0; ch < channels; ch++) {
        fx[0x78 + C.volcmd]();                                                 // all `ret` in this build
        if (C.eff < 0x34) fx[0x34 + C.eff]();                                  // every tick, the row tick too
        if (C.insptr) volume(ch);                                              // 4.10
        C.gvol = voltab[2 * C.fvol];
        C.gpan = C.fpan >> 4;
        C.gfreq = (freq(C.fpitch) * D.voices / 494) & 0xfffe;                  // 4.9
    }
}
```

### 4.8 New note (0x29801) and effects

```c
void new_note(ch)
{
    D.porta = (eff == 3 || eff == 5 || vol >= 0xf0);
    D.keyoff = 0;
    if (eff == 0x14 && par == 0) { D.keyoff = 1; C.keyon = 0; }                // K00
    else if (note == 0x61)       { note = 0; D.keyoff = 1; C.keyon = 0; }       // key off note
    if (ins && ins <= hdr.instruments) C.ins = ins;
    if (!C.ins) return;
    if (note) {
        if (ins) C.keyon = 1;
        C.note = note;
        if (eff == 0x31 && par) return;                     // EDx: the note is dropped, not delayed
        n = note - 1;
        if (D.porta) C.target = (s16)(0x3000 - (n*256 + C.rel));      // tone porta: only the target (no slide)
        else {
            C.stop = 1;
            I = instrument[C.ins]; smp = I.keymap[n];
            if (smp >= I.samples) return;
            S = I.sample[smp];
            C.active = 1; C.newsmp = 1;
            C.start = S.start; C.loop = S.loop; C.end = S.end; C.mode = S.mode;
            if (ins) { C.insptr = I; C.svol = S.vol; C.span = S.pan; }
            C.rel = S.rel;
            C.pitch = C.fpitch = C.target = (s16)(0x3000 - (n*256 + C.rel));
            C.offset = 0;                                   // (no 9xx: always from the start)
        }
    }
    if (C.keyon && ins) {                                   // instrument given: reset volume, pan, envelope
        C.vol = C.fvol = C.svol;
        if (!(hdr.opt & 2)) C.pan = C.fpan = C.span;
        C.fade = 0x8000; C.envpt = 0; C.envtick = 0;
    }
    if (D.keyoff && !ins && C.insptr->envlast == 0) C.fade = 0;   // no envelope: key off cuts
}
```

| index | handler | effect | what it does in this build | used by the song |
|---|---|---|---|---|
| 0x05, 0x06, 0x0a | 0x29ac0 | 5xy, 6xy, Axy (row) | if par: slide memory = par | A0F ×172 |
| 0x08 | 0x29a4a | 8xx | pan = fpan = par | 800 ×87, 8F0 ×84, 8FF ×3 |
| 0x0c | 0x29a3a | Cxx | vol = fvol = min(par, 0x40) | C10, C20, C21 |
| 0x0d | 0x299f2 | Dxx | if next order unset: next = order + 1; next row = 10·(par>>4) + (par&15) | D00 ×1 |
| 0x0f | 0x29a1b | Fxx | par ≥ 0x20: period = 0x2d86bf / par; else if par: speed = par | F06, F0C, F1F |
| 0x25 | 0x29a64 | E1x | if x: memory = x; pitch = clamp(pitch − 16·memory); fpitch = pitch | E11 ×101, E13 ×20 |
| 0x26 | 0x29a86 | E2x | same, pitch + 16·memory | E21 ×1 |
| 0x32 | 0x29a14 | EEx | pattern delay = par | EE0E ×3 (order 0, at speed 12: 180-tick rows) |
| 0x3e | 0x29acb | Axy (tick) | not on the row tick: x ≠ 0: vol = min(vol + x, 64); else vol = max(vol − y, 0) | |
| 0x61 | 0x29af6 | E9x (tick) | if C.retrig (never set) and tick % retrig == 0: offset = 0 → **no-op** | E93 ×8 (order 1, channel 6: the snare roll is not played) |
| 0x69..0x6c | 0x29ab6/b2/ae/aa | volume column 0x10..0x4F | vol = fvol = column − 0x10 | yes |
| 0x6d | 0x29aa6 | volume column 0x5x | vol = fvol = 0x40 | 0x50 |
| 0x74 | 0x29a54 | volume column 0xCx | pan = fpan = x·0x11 | C0, CF |
| all others | 0x299f1 | | `ret` (0, 1, 2, 3/5 slides, 4, 6/7, 9, B, E0/E3–E8/EA–EF, G, H, K (only K00 on the row), L, P, R, T, X, volume-column slides) | |

### 4.9 Pitch → GF1 frequency (0x297a8)

```c
u32 freq(s32 pitch)                       // ~ 6848 * 2^(-pitch/3072): 3072 = 12 * 256 per octave
{
    x = (pitch + 0x8000) & 0xffff;        // only the low 16 bits are used
    f = T3[x >> 12];                      // dword 0x2a3b7: round(6848 * 2^((8 - i) * 4/3))   (16 semitones/step)
    f = f * T2[(x >> 8) & 15] >> 15;      // word 0x2a397: round(32768 * 2^(-i/12))           (semitones)
    f = f * T1[(x >> 4) & 15] >> 15;      // word 0x2a377: round(32768 * 2^(-i/192))          (1/16 semitone)
    f = f * T0[x & 15] >> 15;             // word 0x2a357: round(32768 * 2^(-i/3072))         (1/256 semitone)
    return f;                             // (each step: 64-bit product, shrd 15, low 32 bits)
}
GF1 FC = (f * voices / 494) & 0xfffe      // 14 voices: C-4 at relative note 0 -> 6848 * 14 / 494 = 194
```

All 64 table entries equal those formulas exactly (checked), so a remake may compute or read them. Playback rate =
FC / 1024 × 44,100 Hz (FC bit 0 is unused by the GF1, the player clears it anyway); C-4 → FC 194 = 8,355 Hz
(nominal 8,363). The truncations make every note flat: over the note / instrument pairs of the song by 1 to 35
cents (the low notes of instrument 5, FC 60..90, most). Keep the exact integer FC for fidelity.

### 4.10 Volume (0x29cc4..0x29d7f) and the volume table (0x296e7)

```c
void volume(ch)                                    // after the tick effects, if the channel has an instrument
{
    v = (u8)(C.fvol * D.mastervol >> 6);           // 64
    v = (u16)(v * D.globalvol);                    // 64
    C.fvol = (u8)((v * C.fade >> 16) >> 4);        // 0..128 (64 * 64 * 0x8000 >> 20 = 128)
    if (!C.keyon) C.fade = max(C.fade - I.fadeout, 0);
    pt = C.envpt;
    if (C.envtick && pt == I.looppt_end) C.envpt = pt = I.looppt_start;      // (255 here: never)
    E = &I.env[pt];
    if (pt == I.envlast) C.fvol = (u8)(E.val * C.fvol >> 6);                 // also "no envelope": (0, 64)
    else {
        val = (u8)(E.val + (s16)((s16)(E[1].val - E.val) * (s16)C.envtick / (s16)E.len));   // idiv: truncates
        C.fvol = (u8)(val * C.fvol >> 6);
        if (!(C.envtick == 0 && C.keyon && pt == I.sustain)) {               // (sustain 255 here: never)
            if (++C.envtick >= E.len) { C.envtick = 0; C.envpt++; }
        }
    }
}
voltab[0] = 0, voltab[256] = 0xefff, for i = 1..255:                        // built at init, 257 words at D+0x7aa
    n = bit length of i;  voltab[i] = ((6 + n) << 12) | ((i << (13 - n)) & 0xfff);
```

voltab[i] is the GF1 log volume of i/512 (12-bit volume: 4-bit exponent, 8-bit mantissa; the low 4 bits of the
16-bit value are below the hardware's resolution): GF1 gain(v) = (256 + (v>>4 & 255)) · 2^((v >> 12) − 24) →
gain(voltab[2·fvol]) = fvol/256 (mantissa truncated to 8 bits), at most 0.4990 for fvol = 128.
The envelope is stepped per tick with its own tick counter (the MXM stores the tick length of each segment).

### 4.11 Set volume (0x2a0b6): ramps

```c
void setvol(u16 target)
{
    cur = r16(0x89);                                    // current volume (hardware: the ramped value)
    lo = min(cur, target); hi = max(cur, target); dir = cur > target ? 0x40 : 0;
    if (hi == lo) return;
    if (hi - lo < 0x1000) { w16(0x09, target); return; }        // less than one exponent step: set directly
    lo = max(lo, 0x1000); hi = min(hi, 0xfc00);
    w8(0x07, lo >> 8); w8(0x08, hi >> 8); w8d(0x0d, dir);         // ramp from the current volume, 63 steps/frame
}
```

A ramp ends on the 8-bit limit (e.g. 0xef00 for a target 0xefe0); the next tick then writes the exact target
directly (difference < 0x1000). A change of more than 6 dB thus takes ≤ 60 GF1 frames (≤ 1.4 ms) of ramp.

---------------------------------------------------------------------------------------------------------------

## 5. The song

22 orders (patterns 2 3 0 0 4 4 0 0 6 7 7 5 9 10 11 12 13 14 15 14 15 16), 64 rows each, 14 channels, speed 6
(F0C / EE0E in order 0, F1F at 21:16), 151 BPM throughout. Order starts (IRQ k, xmpGetTimer = (k−2)·P):

| order | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| IRQ | 1 | 925 | 1309 | 1693 | 2077 | 2461 | 2845 | 3229 | 3613 | 3997 | 4381 | 4765 | 5149 | 5533 | 5917 | 6301 | 6685 | 7069 | 7453 | 7837 | 8221 | 8605 |
| timer (s) | −0.017 | 15.285 | 21.644 | 28.003 | 34.362 | 40.721 | 47.080 | 53.439 | 59.798 | 66.157 | 72.516 | 78.875 | 85.234 | 91.593 | 97.952 | 104.311 | 110.670 | 117.029 | 123.388 | 129.747 | 136.106 | 142.465 |

Order 0 takes 924 ticks (rows of 6, 12 and 180 ticks), every other order 384 ticks (6.359 s). 21:16 sets speed
31; 21:1c (the intro's exit) is reached at IRQ 8,923: 147.748 s after IRQ 1, xmpGetTimer 147.731 s. The order list
would end at IRQ 10,039 (166.2 s; restart order 0); after 21:16 only decaying echoes play.
2,061 notes are started. Instruments 1–5 (drums, short tones) play on channels 2–13, the synthesised phrases
(6–37) on channels 0–12; the rows are mostly sparse (a phrase sample lasts 16 notes).

---------------------------------------------------------------------------------------------------------------

## 6. The register stream (for checking a port against `full.gus`)

`full.gus`: `tick vN rXX value` per register write (not 0x43 / 0x44), `tick p240 v` for the mix control; 8-bit
registers log the byte, 16-bit ones the word. Per IRQ, in this order: (1) per channel with a new note: `r00 m|3`,
`r00 m|3`, `r07 4`, `r08 fc`, `r0d 40`, `r0d 40`; (3) per channel: inactive: `r07 4`, `r08 fc`, `r0d 40`, `r0d
40`; active: [new sample: `r02 r03 r04 r05`], [restart: `r0a r0b`, `r00 m`], `r0c pan`, `r01 fc`, then setvol:
nothing / `r09 v` / `r07 lo`, `r08 hi`, `r0d dir`, `r0d dir`. xmpPlay: GF1 reset (196 writes) + `p240 8`,
`p240 9`; xmpStop: the reset again (+ `p240 8`).

Reads: reg 0x89 (one per setvol: 112,188), 0x80 (114,249), 0x8d (124,922 = 14 × 8,923). run_jizz.py's fake GUS
answers 0x89 with the last value written to reg 9, 0x80 / 0x8d with the last write | 1. With that, `mxmplay.py`
reproduces all 685,612 lines. Consequence of the fake read: the "current volume" is always 0 (the resets write
0, and the targets are 0 or at least voltab[1] = 0x7000, so setvol never writes reg 9), so every setvol with a target of 0x1000 or more is logged as a ramp **up** (`r07 10`, `r08 target>>8`)
whatever the voice really plays, ramp-downs never appear and reg 9 is never written during playback (the 64
`r09` lines are the two resets). With the hardware read (`--real-volume-read`): 433,593 lines, 56,397 direct
reg-9 writes, 18,674 setvol ramps (up and down) and 14,795 voice-off ramps. The remake must answer the read from
its GF1 model (as real hardware does).

---------------------------------------------------------------------------------------------------------------

## 7. GF1 model and render (`mxmplay.py --wav`)

Same model as Paper's (paper/re/notes-music.md section 7, with its fixes): 14 active voices → 617,400 / 14 =
44,100 frames per second; per frame and voice: out += interp(DRAM[a], DRAM[a+1], fraction/512) / 128 × gain(vol)
× pan law (L = (15 − p)/15, R = p/15), then position += FC >> 1 (in 1/512 byte), loop / stop, ramp ± 63 steps.
Stopped voices are mixed at their parked position. Additions for JIZZ: bidirectional loops (instruments 2 and 4,
voice control 0x18: reflect at the loop end, direction bit 6), and the busy wait of 4.6 step 2 (the start writes
are applied after the GF1 frames the stop ramps need). Register writes of one IRQ apply at the IRQ instant;
IRQs every 19,759 PIT clocks = 730.29 frames (fraction accumulated).

The WAV starts at IRQ 1 (= `jizz_reference.mp4` frame 0; first sound at frame 730, xmpGetTimer 0):
6,516,400 frames, 147.76 s. Mapping: xmpGetTimer = frame × 1,193,182 / 44,100 − 19,759. Level: 1.0 = one voice
at full scale with gain 1, hard panned; the mix peaks at 2.10 (99.99th percentile 1.65, RMS 0.42; 1.6 % of
the frames above 1.0), the WAV is normalised to its peak. Whether and how the real GUS clips a sum above one
voice's full scale is not known (section 9).

For the remake (`render(ctx, time)` must not depend on how it got there): the player state is a function of the
tick count; seeking = replay the player from xmpPlay (8,923 ticks run in well under a second in C++), and the GF1
voices from the last note starts. Audio and pictures share xmpGetTimer.

### 7.1 Approximations in the original that are its sound (keep them)

- 8-bit samples (synthesised at half rate without anti-aliasing), linear interpolation at 44.1 kHz, no
  band-limiting: render the GF1 way, then resample the mix to the device rate if it is not 44.1 kHz.
- Integer pitch: the four table lookups, the division by 494 and the cleared bit 0 (notes slightly flat).
- Volume: 129 linear steps (fvol 0..128) mapped to the GF1 log scale with an 8-bit mantissa; changes of 6 dB or
  more are ramps of 63 steps per frame; note starts stop the old voice, ramp it down and wait.
- Everything is quantised to the 60.39 Hz tick (and the busy wait); rows are heard one tick after they are read.
- The missing effects: E93 does not retrigger, EDx would drop notes, tone porta would only re-aim. The song as
  the intro plays it is what these rules give, not what the composer's XM might have done in another player.

---------------------------------------------------------------------------------------------------------------

## 8. Time bases

| variable | time base |
|---|---|
| sample synthesis | none (start-up); RNG replayed from the precalc state |
| player state (row, order, volumes, envelopes, pitch) | player ticks = IRQ count; tick k−1's state reaches the GUS at IRQ k |
| xmpGetTimer | PIT clocks: (k − 2)·19,759 + clocks since IRQ k |
| xmpGetPos | changes at the IRQ that reads the row (one tick before the row is heard) |
| GF1 output | GF1 frames at 44,100 Hz, register writes at the IRQ instants (+ busy wait) |

---------------------------------------------------------------------------------------------------------------

## 9. Open points / guesses

- The GF1's behaviour where the docs are vague: pan law (linear vs constant power), clipping of the voice sum
  (the mix exceeds one voice's full scale by up to 6.4 dB), the volume curve (mantissa-linear vs exponential,
  < 0.5 dB), a ramp started below its lower limit (the model clamps up to the limit: inaudible here), and
  bidirectional loops (modelled as DOSBox-X does). Same caveats as Paper's notes section 10.
- The busy-wait length is modelled in whole GF1 frames; the I/O reads take about 1–2 µs each.
- The "already uploaded" check (0x2a27e) is for some loader or a second run; it is never true in the capture.
- The unreferenced bytes 77 19 09 17 at 0x29e34 and the quarter-sine table at 0x2a317 belong to the full
  MXMPlay (or a build stamp); nothing in this build reads them.
- `dump/smokeout.mxm` comes from unicorn's x87 emulation; unicorn honours the 24-bit precision control (other
  settings change the output), so real hardware is expected to give the same bytes, but that was not tested on
  a real FPU.
