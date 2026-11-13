# clone meets clone — music (scope "music": the module builder 0x414aee, the MXM module, MXMPlay)

Addresses are linear addresses of the running program (image base 0x400000: address A is at offset A − 0x400000 of
`unpacked/clone_img.bin`). "IRQ k" = the k-th run of MXMPlay's IRQ0 handler after xmpPlay (IRQ 1 comes 0x10000 PIT
clocks after the timer is installed). P = 22,100 PIT clocks = the player tick (135 BPM). PIT clock 1,193,182 Hz.
The capture's log ticks (`full.gus`, `full.ticks`): xmpPlay's writes are logged at tick 10, IRQ k at tick 10 + k,
xmpStop at tick 11723 (after IRQ 11713).

The player is the same MXMPlay family as JIZZ's (`jizz/re/notes-music.md`, `jizz/remake/src/mxmplay.*`); section 4
describes it as a diff against JIZZ's build, routine by routine, so the remake can extend JIZZ's port.

Everything below was checked, not only read (scripts in `models/music/`, see its README):

- the module builder: `modsynth.py` (Python with the capture's x87 arithmetic: 64-bit mantissas, rounding toward
  zero, `fsin` = the host's double `sin`) reproduces the module buffer at 0x428470 in `snaps/t11.snap` byte for byte
  (all 1,200,000 bytes of the allocation) and therefore `full.dram` (1 MB, identical);
- the player: `mxmplay.py` (a routine-by-routine port of this build, from the module + `clone_img.bin`) reproduces
  `full.dram`, `full.gus` (1,024,247 register writes, identical line by line) and the order / row / timer columns of
  `full.ticks` (11,713 ticks, identical), with the module built by `modsynth.py` (so from `clone_img.bin` alone);
- `statecheck.py`: the player's whole state (the data block 0xe30aec: header copy with its pointers, volume table,
  vibrato table, player and timer variables, all channel records; 5,613 bytes) equals the snapshot's in all 12
  snapshots t11 .. t11148;
- the song is rendered to a WAV through a GF1 model with 16-bit voices (section 7); there is no audio capture to
  compare it with.

---------------------------------------------------------------------------------------------------------------

## 0. Summary

- The module is **built at start-up**: 0x414aee copies the stored part of an MXM module (57,683 bytes at 0x406862:
  header, 21 instruments, 63 patterns, one 4,282-byte 8-bit sample) into a 1,200,000-byte heap buffer and
  appends 20 **synthesised 16-bit samples** (490,548 words + 20 gap words): 12 drum sounds from 0x4149b4 (a sine
  whose frequency falls: kicks, toms, clicks) and 8 tones from 0x414a38 (a sine with a modulated period times a
  slow sine envelope). All in x87 floating point with the program's control word (64-bit mantissas, **round toward
  zero**): with round-to-nearest 211,193 of the 490,568 samples come out different. A C++ port with plain doubles
  gives the identical module, provided the float stores and the float → int conversions truncate (section 2.4).
- The player is **MXMPlay "cubic tiny gus xm player v1.4"**, the same code as JIZZ's build for everything except
  the effects and the note start: the player tick, the volume / envelope code, the GUS update, the volume ramps,
  the GF1 reset, the timer, the IRQ handler and xmpGetTimer are instruction for instruction the same (with the
  data block offsets 1 or 2 lower). This build has arpeggio, porta up / down, tone porta, vibrato, sample offset,
  multi retrig and, in the volume column, fine volume slides (8x / 9x) and per-tick vibrato / tone porta (Bx / Fx);
  it lacks JIZZ's pattern break, pattern delay, fine porta, E9x and the K00 / "key off cuts" handling.
- 18 channels on 18 GF1 voices: the GF1 runs at 617,400 / 18 = **34,300 Hz**; FC = freq × 18 / 494. Master volume
  0x20 (main sets it right after xmpPlay), so each voice reaches at most a quarter of full scale.
- Timing: IRQ0 every P = 0x2d86bf / 135 = 22,100 PIT clocks (53.990 Hz), one player tick per IRQ, speed 3 and
  135 BPM throughout (the song's only Fxx is F00, which does nothing here). Row R = order × 64 + row is read at
  IRQ 1 + 3R and heard one IRQ later, at xmpGetTimer = 3R × P: **row R sounds at music time R × 0.0555657 s**.
- The song: 61 orders, 3,904 rows, 216.91 s; event −1 at 3c:3f ends the intro (xmpStop after IRQ 11713 in the
  capture). 5,881 note starts, 725 key-offs.

---------------------------------------------------------------------------------------------------------------

## 1. What runs when

| when (capture) | what |
|---|---|
| main 0x4001f5 (log ticks 0–2) | "samples  [                    ]" (0x41a460), 0x414aee builds the module: heap 0x400ff4 → 0x40100c(0x124f80) = 0x428470, copy, 20 synth calls, a "." (0x41a48c) after each; returns the buffer in EAX → ESI |
| main 0x40020e (tick 2) | "] - uploading" (0x41a02f), xmpInit(EAX = [0x4207b8] = PSP selector, ESI = module, EBX = 0xe30aec (data block, in the BSS), ECX = 0x10000): ULTRASND parser, header copy, sample upload (985,424 bytes), tables |
| main 0x400b09 (main line between the timer ticks 10 and 11; logged as tick 10) | xmpPlay(0): GF1 reset (18 voices) + mix control 8, 9; channels cleared; timer installed (int 21h 3508 / 2508, PIT 0 = 65536 clocks) |
| main 0x400b13 | xmpSetVolume(0x20): master volume [+1] = 0x20 (xmpInit set 0x40); before the first IRQ |
| IRQ 1 (log 11) | first handler run: GUS update (nothing playing: 18 × voice off), player tick 0 reads row 00:00; xmpGetTimer = −P after it |
| IRQ 2 (log 12) | xmpGetTimer = 0: row 00:00 written to the GUS, first sound |
| IRQ k | GUS update with tick k−1's results, then tick k−1 ... (xmpGetTimer = (k−2)·P at IRQ k) |
| IRQ 11710 (log 11720) | row 3c:3f read (the song's last row; event −1 in the sync table); main sees it at its next frame and sets the end flag |
| IRQ 11713 (log 11723) | 3c:3f has been heard for two ticks; the player wraps to order 0 row 0 (restart 0) |
| main 0x400fc4 (after IRQ 11713) | xmpStop: timer removed (int 21h 2508, PIT 0 = 65536), GF1 reset to 14 voices + mix control 8 (the sound stops at once) |

The music is driven by IRQ0 only; nothing in it depends on the frame rate. The only machine-dependent instants are
xmpPlay (after the CPU-bound precalc) and xmpStop (at the first frame start after the end flag; frame-rate
dependent: in the capture 3 ticks after 3c:3f is read).

The dead fade in main (0x400f2e..0x400f71: past position 0x1020, v = trunc(v − 2·dt) per frame from 0x400, master
volume = v >> 4, end flag when v < 0x10) never runs: the song ends at 0xf3f. It would first set the master volume to
0x3f, about twice the volume of the rest of the song.

---------------------------------------------------------------------------------------------------------------

## 2. The module builder 0x414aee and the synthesisers

### 2.1 The builder

```c
u8 *build_module(void)                                     // 0x414aee
{
    print("samples  [                    ]");              // 0x41a460
    u8 *m = alloc(0x124f80);                                // 0x428470 in every run (first heap block)
    memcpy(m, (u8 *)0x406862, 0xe153);                      // stored module part (byte loop 0x414b0b)
    u16 *p = (u16 *)(m + 0xe153);
    for (k = 0; k < 20; k++) {                              // the calls at 0x414b3d .. 0x414eed
        if (k > 0) p += n[k-1] + 1;                         // add ecx, 2*n + 2: one word gap after each sample
        synth[k](p, n[k], args[k]);                         // 0x4149b4 (A) or 0x414a38 (B)
        print(".");                                         // 0x41a48c
    }
    return m;                                               // EAX
}
```

The gap words stay zero (heap = zeroed BSS). Used: 0xfd9e3 = 1,038,819 bytes of the buffer (module offset 0xd093
+ 985,424 sample bytes); the rest of the allocation is zero. The copy's last byte is 0x53, the first code byte of
0x4149b4 (`push ebx`): the module's 8-bit block (0xd093..0xe152, 4,288 bytes) ends with it; only bytes 0..4281 are
used by an instrument.

The parameters are immediates in the code (the remake reads them from the image, e.g. by decoding 0x414b1f..
0x414efd: `6a ib` / `68 id` push (the first push is the last argument), `ba id` mov edx = n (skip the string pointer
0x41a48c), `8d 8b id` lea ecx, [ebx + 0xe153], `81 c1 id` add ecx, `e8 rel32` call; pushal / mov ah, 9 / int 21h /
popal between the calls; `modsynth.calls()` does this):

| k | call at | synth | module offset | n (words) | arguments (stack order) | MXM instrument |
|---|---|---|---|---|---|---|
| 1 | 0x414b3d | A | 0x0e153 | 1380 | amp 127, f 0.11, df 4e-5, ddf 0 | 1 |
| 2 | 0x414b6e | A | 0x0ec1d | 256 | 127, 0.1, 0.03, −0.0013 | 2 |
| 3 | 0x414ba4 | B | 0x0ee1f | 65536 | P 256, M 256, D 1, E 132000, amp 110 | 3 |
| 4 | 0x414bda | B | 0x2ee21 | 65536 | 92, 64, 1, 1024, 110 | 4 |
| 5 | 0x414c10 | B | 0x4ee23 | 65536 | 128, 128, 1, 2000, 110 | 5 |
| 6 | 0x414c46 | B | 0x6ee25 | 65536 | 128, 128, 1, 132000, 3110 | 6 |
| 7 | 0x414c7c | B | 0x8ee27 | 65536 | 64, 128, 0.1, 132000, 110 | 8 |
| 8–11 | 0x414caa, 0x414cd8, 0x414d06, 0x414d34 | A | 0xaee29 + 0xaca·j | 1380 | amp 527 / 427 / 327 / 227, 0.11, 4e-5, 0 | 9–12 |
| 12–16 | 0x414d62, 0x414d90, 0x414dbe, 0x414dec, 0x414e1a | A | 0xb1951 + 0xc82·j | 1600 | amp 527 / 427 / 327 / 227 / 127, 0.17, 4e-5, 0 | 13–17 |
| 17 | 0x414e4b | A | 0xb57db | 256 | 127, 0.1, 0.0003, −1.3e-5 | 18 |
| 18 | 0x414e81 | B | 0xb59dd | 16384 | 128, 128, 33, 132000, 210 | 19 |
| 19 | 0x414eb7 | B | 0xbd9df | 65536 | 1024, 32, 1, 16384, 110 | 20 |
| 20 | 0x414eed | B | 0xdd9e1 | 65536 | 128, 128, 0.005, 228192, 110 | 21 |

(The values are float32 bit patterns: 0.11 = 0x3de147ae, 4e-5 = 0x3827c5ac, ...; the table prints them rounded.
Instrument 7 is the stored 8-bit sample.)

### 2.2 Synth A 0x4149b4 (EAX = destination, EDX = n; stack: amp, f, df, ddf as floats; `ret 0x10`)

```c
void synth_a(s16 *dst, int n, float amp, float f, float df, float ddf)
{
    amp = (float)(amp * 255.0f);                 // float 255.0 at 0x41a450; the store truncates
    for (int i = 0; i < n; i++) {
        f  = (float)(f - df);                    // stored as float (truncated) BEFORE the sample uses it
        df = (float)(df - ddf);                  // (old df in the line above)
        long double y = sinl((double)(i * f)) * amp;    // i * f exact (fild i, fmul dword [f])
        int v = (int)y;                          // fistp, truncating
        dst[i] = clamp(v, -0x7e81, 0x7e81);      // ±32385 (cmp with 0x7e81 / 0xffff817f)
    }
}
```

The phase is i · f_i, not a sum of frequencies: with df constant (f_i = f_0 − (i + 1)·df) the phase advances by
f_0 − 2(i + 1)·df per sample, a linear fall to zero at i ≈ f_0 / (2 df) (1,375 for f 0.11 / df 4e-5: the sample ends there; 2,125
for the toms' 0.17, which stop at 1,600 at 0.042 rad per sample). amp × 255 > 32385 for amp > 127: calls 8–16 are
clipped (harder kicks / toms). Calls 2 and 17 (ddf < 0: df grows) are 256-sample chirps whose frequency rises past
the Nyquist limit: clicks / hi-hats.

### 2.3 Synth B 0x414a38 (EAX = destination, EDX = n; stack: P, M, D, E, amp as floats; `ret 0x14`)

```c
void synth_b(s16 *dst, int n, float P, float M, float D, float E, float amp)
{
    amp = (float)(amp * 255.0f);                 // float 255.0 at 0x41a454
    for (int i = 0; i < n; i++) {                // TWO_PI = qword 6.283185307179586 at 0x41a458 (= 2*M_PI)
        long double s1 = sin(TWO_PI / M * i);
        long double per = s1 * D + P + i / 999999999;   // idiv by 0x3b9ac9ff: always 0 here (n <= 65536)
        double s2 = sin(TWO_PI / per * i);       // stored as qword (truncated) and reloaded
        long double y = sin(TWO_PI / E * i) * s2 * amp;
        dst[i] = clamp((int)y, -0x7e81, 0x7e81); // fistp truncating, clamp ±32385
    }
}
```

x[i] = 255·amp · sin(2πi / E) · sin(2πi / (P + D·sin(2πi / M))): a sine of period P samples whose period is
modulated by ±D samples once every M samples, times a sine envelope of period E. E = 132000 / 228192: one slow
swell over the sample (half a period ends at i = 66,000 / 114,096); E = 1024, 2000: ring modulation; E = 16384: four
swells. Call 6 (amp 3110) is clipped almost to a square wave; call 18 (D = 33) is a strong FM sweep.

### 2.4 Rounding (checked with `modsynth.py --variant`)

The FPU runs with control word 0x1f7f (set at 0x4010f6): 64-bit mantissas, **every result truncated toward zero**,
`fistp` truncates. The emulator (unicorn = QEMU 5) computes `fsin` by converting the operand to a double (with the
FPU's rounding: truncation) and calling the host's `sin` (glibc), whose double result is exact in the 80-bit
register. With truncation every intermediate result can be computed exactly and truncated once to its destination
precision (truncating twice gives the same as truncating once). Results, samples differing from the capture:

| arithmetic | samples that differ (of 490,568) |
|---|---|
| x87 64-bit, truncate (the program; `modsynth.py`) | 0 |
| x87 64-bit, round to nearest, `fistp` to nearest (the default control word) | 211,193 |
| doubles rounded to nearest; float stores (amp × 255, f, df), the double for `sin` and float → int truncated | **0** |
| the same, but the float stores rounded to nearest | 4,288 (synth A) |
| floats (24-bit) throughout | 241,584 |

So a C++ port can use doubles (SSE2, x64; no FMA contraction, no fast-math): compute amp × 255, f − df and df − ddf
in double (exact here) and convert to float **toward zero** (`float r = (float)d; if (fabs(r) > fabs(d)) r =
nextafterf(r, 0)`), use `std::sin` on doubles and convert with a plain `(int)` cast. A libm other than glibc may
return a different last bit of `sin` for some arguments; that changes a sample only when the product lands within
about 1e−11 of an integer (not seen; expected well under one sample). A real Pentium's `fsin` (66-bit π, extended
result) would differ in the same rare way; the capture is the reference.

### 2.5 What is stored where (for the remake: read, do not copy)

| what | where | format |
|---|---|---|
| stored module part | image 0x406862, 0xe153 bytes (CLONE.EXE is packed: unpack first, see NOTES.md) | MXM bytes 0..0xe152 as they are (section 3); the last byte is code |
| synth parameters | code 0x414b1f..0x414efd (table above) | push immediates (float32), mov edx = word count |
| constants | 0x41a450 / 0x41a454 float 255.0, 0x41a458 double 2π, 999,999,999 = immediate 0x3b9ac9ff | |

---------------------------------------------------------------------------------------------------------------

## 3. The MXM module (`models/music/mxm.py` reads it; format as in JIZZ's notes section 3)

```
0x000 'MXM\0'   orders 61   restart 0   channels 18   patterns 63   instruments 21   speed 3   BPM 135
0x01a options 0x0001 (linear frequencies; notes reset the panning)
0x01c sample data offset 0xd093   0x020 8-bit sample bytes 4,288   0x024 16-bit sample words 490,568
0x028 lowest pitch -18432, highest pitch 24576 (clamps of porta / arpeggio / vibrato)
0x030 panning: all 128     0x050 order list: 0 1 2 3 5 6 7 8 9 10 11 13 4 14 12 15 16 17 18 19 21 22 25 24 26
      27 29 30 31 32 33 34 45 46 47 48 49 50 51 52 20 35 36 37 38 39 40 41 42 43 44 53 54 55 56 57 58 59 60 62 28
```

New against JIZZ's module: **16-bit samples** (sample header voice control bit 2 = 0x04). Their DRAM addresses are
GF1 16-bit addresses: bits 18–19 = the 256 KB bank, bits 0–16 = the word in the bank, and **bit 17 is set in every
one** (the GF1 ignores it: byte address = (a & 0xc0000) | (a & 0x1ffff) << 1). Because bit 17 is 1, a carry out of
bit 16 runs into the bank bits: instrument 20 (0xb84a6..0xc84a6) plays across the bank 2 / bank 3 boundary as one
contiguous sample. The upload writes the sample block byte for byte from DRAM 0: the 8-bit block at 0..0x10bf, the
16-bit words from 0x10c0 (little endian).

Instruments (one sample each, keymap all 0; volume envelope = (ticks to next point, value) pairs; 16-bit lengths in
samples; "played" = start..end of the sample header, which can be shorter than the synthesised buffer):

| ins | data | played | loop | vol | fadeout | volume envelope | role (guess from the sound / use) |
|---|---|---|---|---|---|---|---|
| 1 | call 1 (A) | 1380 | – | 64 | 128 | (1,64) (11,64) (0,0) | kick |
| 2 | call 2 (A) | 256 | – | 13 | 65535 | – | hi-hat click (934 notes, C-5, channels 4/5) |
| 3 | call 3 (B) | 41829 | forward 38755..41829 | 64 | 128 | (2,64) (14,3) (15,8) (0,3) | bass / pad (with 9xx, 3xx) |
| 4 | call 4 (B) | 51447 | bidi 933..51447 | 32 | 128 | (2,64) (12,5) (11,9) (0,5) | (4 notes) |
| 5 | call 5 (B) | 65121 | bidi 0..65121 | 64 | 65535 | – | (1,024 notes, always with 9xx) |
| 6 | call 6 (B, clipped) | 64544 | bidi 1282..64544 | 32 | 128 | (2,64) (10,2) (11,5) (0,2) | distorted bass (with 9xx, 3xx) |
| 7 | stored 8-bit, module 0xd093 | 4282 | bidi 1744..4282 | 64 | 148 | (1,64) (21,64) (8,5) (9,7) (0,5), sustain 1, loop 2..4 | the only stored sample (period about 44 samples); relative note −3 |
| 8 | call 7 (B) | 65536 | – | 64 | 128 | (2,64) (11,2) (12,3) (0,2) | lead (with 9xx) |
| 9–12 | calls 8–11 (A, clipped) | 1380 | – | 64 | 128 | (324,48) (0,48) | kicks |
| 13–17 | calls 12–16 (A) | 1600 | – | 64 | 65535 | – | toms |
| 18 | call 17 (A) | 256 | – | 64 | 65535 | – | click |
| 19 | call 18 (B) | 16383 | bidi 2618..16383 | 64 | 0 | (6,64) (9,37) (18,21) (27,9) (264,3) (0,0) | FM sweep |
| 20 | call 19 (B) | 65536 | bidi 0..65536 | 64 | 0 | (35,64) (49,30) (218,15) (0,0) | (4 notes) |
| 21 | call 20 (B) | 60032 | forward 57856..60032 | 64 | 128 | (6,64) (4,25) (8,16) (9,9) (60,6) (0,0) | plucked lead (with 9xx, 3xx) |

Sustain / loop points are 255 (none) unless given. Instruments 3, 4, 6, 8, 19, 20, 21 have sustain point 0 (and
3, 4, 6, 8 a loop over points 1..3): while the key is on the envelope holds at point 0 (value 64); a key-off (note
97: 725 in the song, mostly on instruments 7, 21, 3, 8, 6, 19) releases it and starts the fadeout (fade −= fadeout
per tick from 0x8000). Effects and volume-column commands in the patterns:

| effect | parameters | handler runs (rows / ticks) |
|---|---|---|
| 0xy arpeggio | 0C (5 rows) | |
| 2xx porta down | 02 | |
| 3xx tone porta | 00 10 20 30 40 50 70 98 | 974 row, 2,922 tick |
| 4xy vibrato | 83, then 00 | 539 row, 1,617 tick (always speed 8 → +32 per tick, depth 3 → 12) |
| 8xx pan | 00, FF | |
| 9xx sample offset | 00 .. F8 (90 values) | in new note |
| Axy volume slide | 07 0A 0F | |
| Cxx volume | 00 05 08 10 20 | |
| Fxx | F00 once, at 3c:3f | no effect (F00 would stop an XM in FT2) |
| Rxy multi retrig | 01 02 03 (x = 0: no volume change) | R01: restart on tick 1 of the row, R02: on tick 2, R03: never (section 4.9) |
| volume column | 10–50 (set), 81 82 (fine down), 91 (fine up), C0 CF (pan) | |

---------------------------------------------------------------------------------------------------------------

## 4. MXMPlay, this build against JIZZ's

`models/music/playerdiff.py` disassembles both builds, maps JIZZ's data-block offsets onto CLONE's and diffs
routine by routine. Result (instructions JIZZ / CLONE, similarity after normalisation):

| routine | JIZZ | CLONE | same? |
|---|---|---|---|
| data block pointer | 0x296dc | 0x4018d8 | same (pointer stored in the code at 0x4018d4; xmpInit writes it via [ebp − 0x9c]) |
| tables | 0x296e7 | 0x4018e3 | **+ vibrato table** (4.3), volume table same |
| xmpInit | 0x29738 | 0x40196a | port from the ULTRASND parser unless EAX bit 31 (4.3); header copy to +0x58 |
| pitch → frequency | 0x297a8 | 0x4019ee | same (tables at 0x4026ad.., values identical to JIZZ's) |
| new note | 0x29801 | 0x401a47 | **differs** (4.8) |
| xmpSetVolume / xmpGetSync / xmpGetPos | 0x299ae / b9 / c4 | 0x401bde / be9 / bf4 | same (getpos does not clear EAX first: AL = row, AH = order, upper half undefined) |
| pitch clamp | 0x299d4 | 0x401c02 | same |
| effect handlers | 0x299f1.. | 0x401c21..0x401e87 | **differ** (4.9) |
| effect dispatch | 0x29b10 | 0x401e88 | same mechanism: `jmp [0x40274d + 4·idx] + 0x401c21` (0x88 entries) |
| player tick (+ volume / envelope) | 0x29b26 | 0x401e9e | **same** (207 instructions) |
| xmpPlay / xmpStop / IRQ0 / xmpGetTimer | 0x29db1 / e22 / e38 / f44 | 0x402129 / 19a / 1ac / 2b3 | same |
| timer install / remove | 0x29ed1 / 0x29f2a | 0x402245 / 0x40229d | int 21h 3508 / 2508 instead of int 31h 0204 / 0205 |
| port I/O, GF1 reset, set volume, voice off, GUS update, GUS start / stop | 0x29f63.. | 0x4022d2.. | same |
| sample upload | 0x2a26f | 0x4025de | no "already uploaded" check; a second `cli` per byte |
| ULTRASND parser | 0x2a2cd (+ int 31h EE02) | 0x402621 | reads the environment through ES = PSP:2C itself; no '@' check |

### 4.1 Routines

| address | routine |
|---|---|
| 0x401894 | "cubic tiny gus xm player v1.4 (c) '95/96 Niklas Beisert / pascal" |
| 0x4018d4 | dword: the data block pointer (0xe30aec) |
| 0x4018d8 | `ebp = [0x4018d4]` (every entry point starts with it) |
| 0x4018e3 | vibrato table (+0x2daa) and volume table (+0x7a8) |
| 0x40196a | xmpInit |
| 0x4019ee | pitch → frequency |
| 0x401a47 | new note |
| 0x401bde / 0x401be9 / 0x401bf4 | xmpSetVolume ([+1] = AL) / xmpGetSync (AL = [+2], never called) / xmpGetPos (AL = row [+6], AH = order [+0x12]) |
| 0x401c02 | clamp pitch to [lowest, highest] |
| 0x401c21..0x401e87 | effect handlers (0x401c21 = `ret`) |
| 0x401e88 | dispatch |
| 0x401e9e | player tick |
| 0x402129 / 0x40219a | xmpPlay / xmpStop |
| 0x4021ac | IRQ0 handler; 0x402245 install, 0x40229d remove |
| 0x4022b3 | xmpGetTimer |
| 0x4022d2.. | port I/O: 0x4022d2 out(base+dx), 0x4022da w8 (reg → base+0x103, byte → base+0x105), 0x4022f2 w8 + delay + same byte again, 0x402314 w16 (word → base+0x104), 0x40232a r8, 0x402341 r16, 0x402357 delay (7 reads of base+0x107), 0x40236a voice select (base+0x102), 0x40237a address pair |
| 0x402399 | GF1 reset; 0x4025ae GUS start, 0x4025cd GUS stop |
| 0x402425 / 0x40248b | set volume (ramp) / voice off (ramp down) |
| 0x4024ab | GUS update (start of every IRQ tick) |
| 0x4025de | sample upload |
| 0x402621 | ULTRASND parser |
| data 0x40266d | 64 bytes: quarter sine round(64 sin(πi/128)), i = 0..63 (the same bytes as JIZZ's unreferenced 0x2a317) |
| data 0x4026ad / 0x4026cd / 0x4026ed / 0x40270d | frequency tables T0, T1, T2 (16 words each), T3 (16 dwords): identical to JIZZ's |
| data 0x40274d | jump table, 0x88 dwords (handler − 0x401c21), up to 0x40296d |

### 4.2 Data block (EBX of xmpInit = 0xe30aec, 0x4000 bytes zeroed by xmpInit)

Offsets +0x00..+0x24 as in JIZZ (globalvol, mastervol, sync, tick, speed, newrow, row, pattern pointer, rows,
order, next order, next row, pattern delay, the event being read: note +0x1f, instrument +0x20, volume +0x21,
effect +0x22, parameter +0x23, +0x24 = a flag new note tests, always 0). From +0x25 on, JIZZ's offsets − 1:

```
+0x25 old int 8 (offset, +0x29 selector)   +0x2b saved esp, +0x2f ss   +0x31 u32 BIOS divider (byte +0x33)
+0x35 u32 current PIT period   +0x39 u32 timer   +0x3d u32 busy   +0x41 u32 tick period   +0x45 u32 remaining
+0x49 ds   +0x4b u32 max PIT period (0x10000)   +0x4f u32 GUS port (0 = no sound)   +0x53 u32 GF1 voices
+0x57 u8 "tone porta" flag of the new note        (JIZZ's +0x59 key-off flag does not exist)
+0x58..+0x7a7 module bytes 0..0x74f (pointers fixed up: +0x1a8 + 4·(i−1) = instrument i, +0x3a8 + 4·p = pattern p)
+0x7a8 u16[257] volume table   +0x9aa 32 channel records of 0x100 bytes   +0x2daa s8[256] vibrato table
(the IRQ stack grows down from +0x2daa, below the table)
```

Channel record: the same layout as JIZZ's (section 4.2 there), with the fields this build's effects use:

```
+0x41 s32 tone porta target   +0x45 s32 tone porta speed (3xx: xx·16)   +0x49 volume slide memory
+0x4f s32 porta up speed (1xx: xx·16)   +0x53 s32 porta down speed (2xx: xx·16)
+0x5b vibrato speed (x·4)   +0x5c vibrato position (byte, read as a word with +0x5d = 0)   +0x5e vibrato depth (y·4)
+0x65 arpeggio step 0..2   +0x66 / +0x67 / +0x68 arpeggio offsets 0, x, y (semitones)
+0x6a retrigger counter   +0x6b retrigger interval (y)   +0x6c retrigger volume change (x)
+0x6d note   +0x6e sample offset memory (9xx)
(+0x2f, +0x30, +0x37, +0x3b, +0x60, +0x63, +0x64, +0x70: cleared, never used)
```

xmpPlay clears every record (rep stosd), so the effect byte +0x3f is 0 until the first row sets it.

### 4.3 xmpInit (0x40196a), tables (0x4018e3), upload (0x4025de), ULTRASND (0x402621)

```c
int xmpInit(u32 psp /*eax*/, u8 *mod /*esi*/, u8 *D /*ebx*/, u32 maxper /*ecx*/)
{
    *(u8 **)0x4018d4 = D; memset(D, 0, 0x4000);
    D.maxper = maxper; D.mastervol = 0x40;
    port = (psp & 0x80000000) ? psp : ultrasnd(psp);   // bit 31 set: EAX is the port itself (0 = no sound)
    D.port = port & 0x7fffffff;                         // ultrasnd: 0x2x0, or 0 without "ULTRASND=2x0,"
    if (*(u32 *)mod != 'MXM\0') return 0;
    memcpy(D + 0x58, mod, 0x750);
    for (i = 0; i < 0x180; i++) ((u32 *)(D + 0x1a8))[i] += (u32)mod;
    upload(mod + hdr.smpstart, hdr.smp16 * 2 + hdr.smp8);       // 985,424 bytes
    tables();                                           // 0x4018e3
    return 1;
}
void tables(void)
{
    s8 *v = D + 0x2daa;                                 // v[i] = round(64 sin(2 pi i / 256)), i = 0..255
    memcpy(v, (u8 *)0x40266d, 64);  v[64] = 64;  for (i = 1; i < 64; i++) v[64 + i] = v[64 - i];
    for (i = 0; i < 128; i++) v[128 + i] = -v[i];
    voltab[0] = 0; voltab[256] = 0xefff; for (i = 1..255) voltab[i] = (as JIZZ)
}
void upload(u8 *src, u32 n)                             // as JIZZ's, without the "already uploaded" test
{
    if (!port || !(n >> 4)) return;
    for (a = 0, blocks = n >> 4; blocks; blocks--) {
        pushf; cli; w8(0x44, a >> 16); select reg 0x43;
        do { cli; outw(port+0x104, a & 0xffff); out(port+0x107, *src++); a++; } while (a & 15);
        popf;
    }
}
```

ULTRASND (ES = the PSP selector from EAX, environment selector at PSP:2C): scans the NUL-separated strings for
"ULTRASND", requires "=2" at +8 and "0," at +0xb, returns 0x200 | (char at +0xa − '0') << 4 (low byte):
"ULTRASND=240,…" → 0x240. JIZZ's version gets the environment from int 31h EE02 and skips entries whose 14th
character is '@'. IRQ / DMA settings are ignored (no GUS IRQ, no DMA, PIO upload).

### 4.4–4.7 Unchanged routines

xmpPlay, GF1 reset, xmpStop, the timer (IRQ0, xmpGetTimer, xmpGetPos), the GUS update, set volume, voice off and
the player tick with the volume / envelope code are JIZZ's (its notes sections 4.4–4.7, 4.10, 4.11; its
pseudo-code applies with the offsets of 4.2). With this song:

- GF1 reset with 18 channels: voices = 18, reg 0x0e = 0xd1, 617,400 / 18 = 34,300 GF1 frames per second.
- xmpPlay: period = 0x2d86bf / 135 = 22,100 (53.990 Hz); first period 0x10000; timer = 0xffff0000 − 22,100.
  IRQ 1: timer = −P; IRQ k: timer = (k − 2)·P; xmpGetTimer = timer + clocks since IRQ k.
- Player tick: the row is read on the tick where ++tick == speed; with speed 3 always, row R = order × 64 + row
  (0-based count over the song) is read at IRQ 1 + 3R, so after IRQ k: order = ((k − 1) / 192) mod 61, row =
  ((k − 1) / 3) mod 64. The row's GUS writes happen at IRQ 2 + 3R, xmpGetTimer = 3R·P.
- **Correction for JIZZ's notes 4.7 and its ports** (`jizz/re/models/music/mxmplay.py`, `jizz/remake/src/mxmplay.cpp`):
  in both builds the row reader skips a channel that has no entry in the row (`je` at 0x29c33 / 0x401fab):
  new_note, the volume-column handler and the effect handler do not run for it, and its volume-column command
  (+0x40) keeps the previous row's value (only the effect byte +0x3f is reset to 0xff). The JIZZ ports call
  new_note for every channel; that is harmless there (new_note with an empty entry does nothing, JIZZ's per-tick
  volume-column handlers are all `ret`) and with this song too (the volume-column tick handlers this song can
  reach are `ret`), but a shared port should mirror the skip.
- Volume: with master 0x20, fvol = vol·0x20 >> 6 = vol / 2 (truncated) before global volume, fade and envelope:
  at most 64, gvol = voltab[2·fvol] ≤ voltab[128] = 0xe000 = gain 0.25.
- GUS update (unchanged): stop voices with a new note (`r00 mode|3` twice + voice off), busy-wait until every
  channel's ramp has stopped (reads reg 0x8d of 18 voices), then per channel: inactive → voice off; else [loop /
  end], [restart: address 0x0a/0x0b = start + offset (clamped to end − 1), `r00 (r80 & 0x40) | mode`], read 0x80
  (unused: the code tests BL), pan, FC, set volume.

### 4.8 New note (0x401a47) — differences

```c
void new_note(ch)                       // runs only for channels with an entry in the row
{
    D.porta = (eff == 3 || eff == 5 || vol >= 0xf0);       // +0x57
    if (note == 0x61) { C.keyon = 0; note = 0; }           // key off. JIZZ also: K00 = key off, and a key-off
                                                           // flag that cuts (fade = 0) notes without envelope
    if (ins && ins <= hdr.instruments) C.ins = ins;
    if (!C.ins) return;
    if (note) {
        if (ins) C.keyon = 1;
        C.note = note;
        if (eff == 0x31 && par) return;                     // EDx: the note is dropped (as JIZZ; not in the song)
        n = note - 1;
        if (D.porta) C.target = (s16)(0x3000 - (n*256 + C.rel));   // tone porta: only the target
        else {
            C.stop = 1;
            I = instrument[C.ins];                          // [D + 0x1a8 + 4*(C.ins-1)] (JIZZ: + 0x1a6 + 4*C.ins)
            smp = I.keymap[n]; if (smp >= I.samples) return;
            S = I.sample[smp];
            C.active = 1; C.newsmp = 1;
            C.start = S.start; C.loop = S.loop; C.end = S.end; C.mode = S.mode;
            if (ins) { C.insptr = I; C.svol = S.vol; C.span = S.pan; }
            C.rel = S.rel;
            C.pitch = C.fpitch = C.target = (s16)(0x3000 - (n*256 + C.rel));
            ofs = 0;                                        // NEW: 9xx sample offset with memory
            if (eff == 9) { if (par) C.ofsmem = par; ofs = C.ofsmem << 8; }
            C.offset = ofs;                                 // JIZZ: always 0
            C.vibpos = 0; C.arpidx = 0; C.rtcount = 0;      // NEW (+0x5c, +0x65, +0x6a; also +0x60, +0x70)
        }
    }
    if (C.keyon && ins) {
        if (D.flag24 != 1) {                                // NEW test of [+0x24]: always 0 here (no setter)
            C.vol = C.fvol = C.svol;
            if (!(hdr.opt & 2)) C.pan = C.fpan = C.span;
        }
        C.fade = 0x8000; C.envpt = 0; C.envtick = 0;
    }
}
```

The sample offset is in address units: samples for 8- and 16-bit samples alike (256 per 9xx step), and the GUS update
clamps start + offset to end − 1. The song uses 9xx on 2,818 notes (instruments 3, 5, 6, 7, 8, 16, 21).

Key off: without JIZZ's cut, a key-off on an instrument without envelope (here 2, 5, 13, 14, 16, 18: fadeout 65535,
25 key-offs) silences the note one tick later (the tick's volume still uses the old fade, then fade = 0) instead of
at once; instruments with an envelope release it and fade out by their fadeout per tick (19 and 20: fadeout 0, the
envelope's last point is 0).

### 4.9 Effects (jump table 0x40274d; index 0x00..0x33 row tick, 0x34..0x67 every tick including the row tick,
0x68..0x77 volume column on the row, 0x78..0x87 volume column every tick)

| index | handler | effect | what it does in this build | JIZZ's build |
|---|---|---|---|---|
| 0x00 | 0x401c6b | 0xy (row) | par = 0 → C.eff = 0xff (also the "no effect" case); arp = {0, x, y} | ret |
| 0x34 | 0x401c88 | 0xy (tick) | fpitch = clamp(fpitch − arp[C.arpidx]·256); arpidx = (arpidx + 1) mod 3 (a counter of its own, reset only by a note start: not tick mod 3) | ret |
| 0x01 / 0x02 | 0x401cec / 0x401cfb | 1xx / 2xx (row) | if par: speed memory = par·16 (separate up / down) | ret |
| 0x35 / 0x36 | 0x401d0a / 0x401d22 | 1xx / 2xx (tick) | not on the row tick: pitch = fpitch = clamp(pitch ∓ speed) | ret |
| 0x03 | 0x401d3a | 3xx (row) | if par: tone porta speed = par·16 | ret |
| 0x37, 0x87 | 0x401d49 | 3xx (tick), volume column Fx (tick) | not on the row tick and pitch ≠ target: pitch moves by speed towards the target, stops on it (signed compares); pitch = fpitch (on the row tick too) | ret |
| 0x04 | 0x401d78 | 4xy (row) | if y: depth = 4y; if x: speed = 4x | ret |
| 0x38, 0x83 | 0x401d93 | 4xy (tick), volume column Bx (tick) | t = vib[pos] (s8); fpitch = clamp(fpitch + ((t·depth) >> 3)); not on the row tick: pos += speed (byte) | ret |
| 0x05, 0x06, 0x0a | 0x401dbd | 5xy / 6xy / Axy (row) | if par: slide memory = par | same |
| 0x3e | 0x401dc8 | Axy (tick) | not on the row tick: x ≠ 0: vol = min(vol + x, 64), else vol = max(vol − y, 0); vol = fvol | same |
| 0x08 | 0x401c51 | 8xx | pan = fpan = par | same |
| 0x0c | 0x401c41 | Cxx | vol = fvol = min(par, 0x40) | same |
| 0x0f | 0x401c22 | Fxx | par ≥ 0x20: period = 0x2d86bf / par; else if par: speed = par | same |
| 0x1b | 0x401df3 | Rxy (row) | if par: interval = y, volume change = x, counter = 0 | ret |
| 0x4f | 0x401e0c | Rxy (tick) | c = counter++; if (s8)c < (s8)interval: return; counter = 0; offset = 0 (the GUS update restarts the voice at the sample start, without the stop and ramp of a new note); volume change x (below) | ret |
| 0x69..0x6c | 0x401cbe, 0x401cba, 0x401cb6, 0x401cb2 | volume column 1x..4x | vol = fvol = column − 0x10 (code: x + 0x10 per step down the chain) | same |
| 0x6d | 0x401cae | volume column 50 | vol = fvol = 0x40 | same |
| 0x70 | 0x401cdb | volume column 8x (row) | vol = fvol = max(vol − x, 0) | ret |
| 0x71 | 0x401cc8 | volume column 9x (row) | vol = fvol = min(vol + x, 64) | ret |
| 0x74 | 0x401c5b | volume column Cx | pan = fpan = x·0x11 | same |
| 0x09 | – | 9xx | `ret` here; done in new note (4.8) | – |
| all others | 0x401c21 | | `ret`: 5xy / 6xy ticks, 7, B, D (JIZZ: pattern break), E (JIZZ: E1x, E2x, EEx pattern delay, E9x), G, H, K, L, P, T, X, volume-column slides 6x / 7x, Ax, Dx, Ex, the row part of Bx / Fx | |

Vibrato detail (0x401d93): `movsx eax, table byte; imul byte [depth]; sar eax, 3` multiplies in AX only, so the
upper half of EAX is the sign of the table byte. With depth 0 and a negative table value the result would be
0xffff0000 >> 3 = −8192 (a jump of 32 semitones); the song always has depth 12 when vibrato runs (`mxmplay.py`
counts the case: never). Range: ±64·12 / 8 = ±96 pitch units = ±0.375 semitone, one cycle per 8 non-row ticks.

Retrig volume change (0x401e1b): ah = rol8(0x80, x); x = 0 or 8: none; 1–5: vol − 1, 2, 4, 8, 16; 6: (vol +
vol/4) / 2; 7: vol / 2; 9–13: vol + 1, 2, 4, 8, 16; 14: vol + vol/2; 15: 2·vol (127 if ≥ 128); then clamped to
0..64 (signed test for < 0). The song only has x = 0. Because the counter is compared before it counts and is reset
on every row with an R command, Ry retriggers every y + 1 ticks counted from the row start: at speed 3 R01 restarts
the sample on tick 1, R02 on tick 2, R03 never (all three are in the song: 20, 24 and 32 rows).

### 4.10 Pitch → frequency, volume

As JIZZ (its sections 4.9 and 4.10; the tables at 0x4026ad.. hold the same 64 values). FC = (freq(fpitch) × 18
/ 494) & 0xfffe; C-4 at relative note 0: freq 6848 → FC 248 = 248 / 1024 × 34,300 = 8,307 samples per second
(nominal 8,363: −11.6 cents; JIZZ's 14 voices give FC 194 = 8,355).

### 4.11 Porting: what to change in `jizz/remake/src/mxmplay.cpp` / `gf1.cpp`

1. Module: replace mp4synth by the builder of section 2 (copy image 0x406862..+0xe153, 20 synth calls with the
   parameters decoded from 0x414b1f..0x414efd). The module has 16-bit samples (smp16 = 490,568 words).
2. Tables from CLONE.EXE: frequency tables 0x4026ad / 0x4026cd / 0x4026ed / 0x40270d (or the formulas), vibrato
   quarter sine 0x40266d (or round(64 sin(πi/128))). The effect dispatch can stay a switch (code), indexed as 4.9.
3. Player: master volume 0x20 before the first IRQ; new note as 4.8 (drop K00 / key-off cut, add 9xx, the clears,
   the [+0x24] test); the effect handlers of 4.9 with the new channel fields; the row reader must skip channels
   without an entry (4.4–4.7). Everything else (tick, envelope, volume, GUS update, setvol, timer) is unchanged.
4. GF1 model: 18 voices (34,300 Hz); 16-bit voices (voice control bit 2): the address counts words, the DRAM word
   is at byte (a & 0xc0000) | (a & 0x1ffff) << 1, signed little-endian, 1.0 = 32768; interpolation between word
   a and word a + 1 (translated separately: instrument 20 crosses a bank); bidirectional loops on 16-bit data
   (instruments 4, 5, 6, 19, 20) as on 8-bit data.
5. Timeline: sync table at 0x420499 (8-byte entries: dword position = order × 64 + row, dword event; event −1 at
   0xf3f); xmpPlay's log tick 10 (IRQ k = log tick 10 + k); the song ends at IRQ 11713 (the order list wraps; the
   capture's xmpStop follows it).

---------------------------------------------------------------------------------------------------------------

## 5. The song

61 orders of 64 rows, speed 3, 135 BPM throughout: every order is 192 ticks = 3.55620 s, row R (0-based over the
song) is heard from music time 3R × 22,100 / 1,193,182 s = R × 55.5657 ms. Order o starts at music time
o × 3.55620 s (IRQ 192·o + 2 writes its first row; 00 at 0.000 s, 0c at 42.674 s, 10 at 56.899 s, 20 at 113.799 s,
24 at 128.023 s, 3a at 206.260 s, 3c at 213.372 s; `song.py` prints the whole list with the IRQ that reads each
order). The last row 3c:3f is heard from 216.873 s; IRQ 11713 (music time 216.910 s, xmpGetTimer 258,813,100)
wraps to 00:00; the capture's xmpStop comes after it (program time 217.63 s), cutting all voices.

Instruments by channel (from `song.py`): 0–2 the long tonal samples 3, 5, 6 (bass and pads), 2–5 and 8 the kick
(1), 4–5 the hi-hat (2), 4–8 and 10 the stored sample (7), 6–17 the leads 8 and 21, the toms 13–17, the clicks 18,
the sweeps 19 (and 6 on 10–17). The tonal phrases use sample offsets heavily (9xx with 90 different values): the
long 65,536-word synth samples are played from the middle.

---------------------------------------------------------------------------------------------------------------

## 6. The register stream (for checking a port against `full.gus`)

Format and order of writes as JIZZ's section 6 (the same GUS update): per IRQ (1) per channel with a new note
`r00 m|3` ×2, `r07 4`, `r08 fc`, `r0d 40` ×2; (3) per channel: inactive: `r07 4`, `r08 fc`, `r0d 40` ×2; active:
[new sample: `r02 r03 r04 r05`], [restart: `r0a r0b`, `r00 m`], `r0c pan`, `r01 fc`, setvol: nothing / `r09 v` /
`r07 lo`, `r08 hi`, `r0d dir` ×2. xmpPlay: GF1 reset (196 writes: 3 + 32 × 6 + 1) + `p240 8`, `p240 9`; xmpStop:
the reset again + `p240 8`.

Reads in the capture: reg 0x89 171,462 (one per setvol), 0x80 177,382, 0x8d 210,834 (= 18 × 11,713). The fake GUS
answers 0x89 with the last value written to reg 9 (always 0: the resets write 0, the player never writes reg 9
during playback), so every setvol with a target ≥ 0x1000 is logged as a ramp up from 0x10: 111,730 ramps, no
direct reg-9 writes, 1,024,247 lines. With the GF1 model answering the reads (`--real-volume-read`, as hardware
would): 862,725 lines, 68,100 ramps, 12,998 direct reg-9 writes; voice-off ramps 45,248 in both. The busy wait of the
GUS update (stop ramps of the voices that get a new note) takes up to 56 GF1 frames (1.6 ms), 44 on average, in
1,894 of the 11,713 IRQs. The remake must answer the read from its GF1 model.

---------------------------------------------------------------------------------------------------------------

## 7. GF1 model and render (`mxmplay.py --wav`)

JIZZ's model (its section 7: 8-bit linear interpolation, (256 + mantissa) · 2^(exponent − 24) gain, pan law
L = (15 − p) / 15, R = p / 15, ramps of 63 steps per frame, bidirectional loops as DOSBox-X, the busy wait) with
16-bit voices (section 4.11 item 4) and 18 active voices: 34,300 frames per second, IRQs every 22,100 PIT clocks =
635.29 frames (fraction accumulated). The WAV starts at IRQ 1 (music time −22,100 / 1,193,182 = −0.0185 s; first
sound at frame 635) and ends one period after IRQ 11713: 7,441,283 frames, 216.95 s. Level: 1.0 = one voice at full
scale with gain 1, hard panned; the mix peaks at 0.664 (master volume 0x20: one voice reaches at most 0.25), RMS
0.07–0.14 per 8 s; the WAV is normalised to 0.9 of its peak. All channels are panned to 128 (pan 8: L 7/15,
R 8/15) except where 8xx / volume column Cx pan them (12 times).

Approximations of the original that are its sound (keep them): the synthesised samples as built (truncating x87
arithmetic, clipping at ±32385), 16-bit samples at 34.3 kHz with linear interpolation, integer FC (notes 12 cents
flat at C-4), the 129-step volume scale, ramps, the 54 Hz tick quantisation, rows heard one tick after they are
read, the retrig and arpeggio counters as in 4.9.

---------------------------------------------------------------------------------------------------------------

## 8. Time bases

| variable | time base |
|---|---|
| module / sample synthesis | none (start-up) |
| player state (row, order, volumes, envelopes, pitch, vibrato / arpeggio / retrig counters) | player ticks = IRQ count; tick k−1's state reaches the GUS at IRQ k |
| xmpGetTimer | PIT clocks: (k − 2)·22,100 + clocks since IRQ k (the frames' t70 and dt come from it) |
| xmpGetPos | changes at the IRQ that reads the row: row R at IRQ 1 + 3R (one tick before it is heard) |
| GF1 output | GF1 frames at 34,300 Hz, register writes at the IRQ instants (+ busy wait) |

For the remake (`render(ctx, time)` must not depend on how it got there): the player state is a function of the
tick count; seeking = replay the player from xmpPlay (11,713 ticks) and the GF1 voices from the last note starts,
as JIZZ's port does.

---------------------------------------------------------------------------------------------------------------

## 9. Open points / guesses

- The GF1 points of JIZZ's section 9 (pan law, clipping of the sum, volume curve, bidirectional loops), plus the
  16-bit path: the address translation and the bank carry through bit 17 follow the GUS SDK's description of the
  translation (bit 17 ignored); that a real GF1 increments the untranslated 20-bit address (so instrument 20 really
  crosses into bank 3) is inferred from the converter's addresses, not tested on hardware. The interpolation of
  16-bit data is modelled like the 8-bit one.
- The [+0x24] test in new note (keep the volume on a new note) and the `i / 999999999` term of synth B have no
  effect here; both look like leftovers of features this build or this song does not use.
- `sin` precision: the module was checked against unicorn (host glibc `sin`); a real FPU may differ in rare samples
  (section 2.4).
- Instrument roles in section 3 are descriptions from the parameters and the patterns, not from listening.
