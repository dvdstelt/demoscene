# Scope "core": start-up, main, the frame loop, time, events, video, runtime, shared helpers, the unpacker

Addresses are linear addresses of the running program (image relocated for 0x400000, see NOTES.md). "t70" is
main's time in its own unit: **1/17043 of the PIT clock = 1/70.0101 s** (not 1/70 s, see 5.1). "Timer" = the value
xmpGetTimer (0x4022b3) returns: PIT clocks (1,193,182 Hz) since the music start. Positions are order:row in hex,
`p = order * 64 + row`. Watcom register calling convention (EAX, EDX, EBX, ECX, then the stack) unless a routine
is marked "stack args" (cdecl-like: the caller pushes right to left and pops).

What was checked (scripts in `models/core/`, see its README):

- **unpacker**: an unpacker written from NOTES.md alone gives `unpacked/clone_img.bin` byte for byte (section 13);
- **time step**: a model of 0x400f76..0x400fbf with the truncating FPU, replayed over every timer value of
  `full.flips`, gives t70 [0xe9572e], dt [0x420489] and [0xe9573e] bit for bit at all 12 snapshots (5.1);
- **precalc tables** of 0x416806, 0x403f10, 0x40347a, 0x4033ee: recomputed, all bytes equal to `snaps/t11.snap`;
- **2D helpers and the 15-bit conversion**: executed in unicorn on random inputs, equal to the Python models of
  the pseudo-C below (0 pixels / bytes differ);
- heap layout (heap walk of t11), the part / event switches (decoded tables), the sync table run-off (snapshots).

Corrections and additions to NOTES.md / PARTS.md are collected in section 18.

---------------------------------------------------------------------------------------------------------------

## 1. What the core draws, and when

The core draws nothing of its own in the intro: the parts draw, main shows. The only drawing routines in the
scope are part 99 (0x400079, never selected: black with a dark blue band) and the shared helpers of section 11.

| phase | ticks | what happens |
|---|---|---|
| start-up | 0 | entry 0x4010de → main |
| precalc | 0..10 (program 0.00..0.65 s) | text mode: title, "samples [....] - uploading" (module builder), "textures [....] - uploading" (the second "uploading" uploads nothing) |
| video | 11 | VBE mode 320×200×32 (else ×15) with LFB, first (black) frame shown, xmpPlay(0), volume 0x20 |
| frame loop | 11..11723 | per frame: key check, events, part draws, show, time step |
| exit | 11723 | xmpStop, text mode 3, DOS memory freed, title printed, int 21h 4C00 |

## 2. Entry 0x4010de (Watcom start-up)

```c
void entry(void)                      // ES = PSP selector (PMODE/W), DS = SS = flat
{
    jmp over "WATCOM";                // 0x4010de: jmp 0x4010e9, bytes "WATCOM" at 0x4010e3
    *(u16 *)0x4207b8 = ES;            // PSP selector. The high word 0x4207ba stays 0x2461 from the image, so the
                                      // dword xmpInit gets in EAX has bit 31 clear (= sound on)
    sti(); cld(); ES = DS;
    fninit(); fldcw(0x1f7f);          // all exceptions masked, precision 64 bits, rounding = TRUNCATE
    main();                           // 0x4001ab
    int21(AX = 0x4c00);               // 0x401106: exit code 0 (also on the video error path, which calls
}                                     // 0x401106 with EAX = 1: AX is overwritten)
```

0x4010dd (76 call sites, before every `fistp`) is a bare `ret`. Every float → int conversion and every store of a
float or double truncates toward zero.

## 3. main 0x4001ab

Locals (frame `enter 0x16c`): [ebp−0xc] end flag, [ebp−0x14] part, [ebp−0x1c] progress dots, [ebp−0x34] fade
volume ×16, [ebp−0x16c] a 256-byte mode info buffer (filled, never read).

```c
void main(void)
{
    int end = 0, part = 0;
    *(u32 *)0x4207c8 = 0xdf02ec;                       // grid texture pointer (BSS buffer, 0x10101 dwords)
    print(0x41a000);                                   // "clone meets clone - acme/fudge takeover '98\n\r"
    heap_init(start = 0x42846c, size = 10000000);      // 0x400ff4 (ESI, EAX)
    u8 *mod = build_module();                          // 0x414aee (music scope; prints "samples [" and dots)
    print(0x41a02f);                                   // "] - uploading"
    int vol16 = 0x400;
    xmpInit(EAX = *(u32 *)0x4207b8, ESI = mod, EBX = 0xe30aec, ECX = 0x10000);   // 0x40196a, uploads to the GUS
    int dots = 0;
    print(0x41a03e);                                   // "\n\rtextures [" 20 spaces "]\rtextures ["
    precalc_engine();                                  // 0x416806 (section 10)
    make_multab();                                     // 0x403f10
    part1_setup();                                     // 0x4053f2 (particles; called again by event 1)
    make_exp23();                                      // 0x40347a
    make_weights_grid();                               // 0x4033ee
    for (i = 0; i <= 256; i++) {                       // 0x400240..0x400844: maps and colour tables (gfx)
        if ((i * 20) / 256 > dots) { print(0x41a06c /* "." */); dots = (i * 20) / 256; }
        ...
    }
    print(0x41a02f);                                   // "] - uploading" (nothing is uploaded)
    grid_texture(0xe6542b);                            // 0x4033a7 (gfx), stack arg
    *(u32 *)0xe35114 = *(u32 *)0xe3510c;               // copy of map B's pointer; read only by part 10 (0x418fa9)
    grey palettes, the eight pictures                  // 0x4008d4..0x400a4a (gfx)
    part7_setup(); part8_setup();                      // 0x416cca, 0x417563 (parts3d)
    part9_buffers(); part10_buffers();                 // 0x418402, 0x419373
    title_pictures();                                  // 0x419d5b (gfx / grid)
    dos_alloc();                                       // 0x40181e: VBE info and mode info blocks
    bpp = 32;                                          // [0x42048d]
    mode = vbe_find(320, 200, 32);                     // 0x401628
    if (mode == -1 || !vbe_lfb(mode)) {                // 0x4013f3: ModeAttributes bit 7
        bpp = 15;
        mode = vbe_find(320, 200, 15);
        if (mode == -1 || !vbe_lfb(mode)) {
            print(0x41a06f);                           // "\n\rerror initializing 32bit and 15bit videomode\r\n"
            exit();                                    // 0x401106; xmpInit has run, the GUS is not stopped
        }
    }
    vbe_set_mode(mode, lfb = 1, clear = 1);            // 0x401457: 4F02 BX = mode | 0x4000 (memory cleared)
    *(u32 *)0xe35100 = vbe_map_lfb(mode);              // 0x4016b0
    vbe_mode_info(mode, ebp - 0x16c, DS);              // 0x401388, result unused
    show_frame();                                      // 0x400000: shows page 0 (black), draw target = page 1
    xmpPlay(0);                                        // 0x402129
    xmpSetVolume(0x20);                                // 0x401bde (xmpInit set 0x40)

    for (;;) {                                         // 0x400b18
        if (kbhit() || end) break;                     // int 16h AH=01 (ZF clear = key); the key is not read
        u16 ax = xmpGetPos();                          // 0x401bf4: AH = order, AL = row
        u16 pos = (ax >> 8) * 64 + (ax & 0x3f);
        while ((s32)pos >= sync[sync_index].pos) {     // signed; several events can fire in one frame
            run_event(sync[sync_index].event);         // section 4
            sync_index++;                              // [0x420495]
        }
        draw_part(part);                               // section 4.3
        show_frame();                                  // 0x400000, section 6
        if (pos > 0x1020) {                            // dead code, section 7
            vol16 = (int)(vol16 - dt * 2.0f);
            if (vol16 < 16) { end = 1; vol16 = 16; }
            xmpSetVolume((u8)(vol16 >> 4));
        }
        time_step();                                   // 0x400f76..0x400fbf, section 5
    }
    xmpStop();                                         // 0x40219a
    vbe_set_mode(3, lfb = 0, clear = 1);               // 4F02 BX = 3 (text mode through VBE, not int 10h AH=0)
    dos_free();                                        // 0x401844
    print(0x41a000);                                   // the title again
}                                                      // → entry: int 21h 4C00
```

`pos` stays in DX through the event handlers, the part routine and 0x400000 (Watcom routines preserve every
register but EAX; 0x400000 pushes EDX); the fade test reads it there.

## 4. Events and parts

### 4.1 The sync table (static data, read it from the exe)

0x420499 (image offset 0x20499): 98 entries of 8 bytes, `{s32 pos; s32 event;}`, pos ascending, the last one
3c:3f → −1. The index [0x420495] (dword, 0 in the image) never goes back. NOTES.md lists the entries; checked
against the image: identical.

Run-off: event −1 only sets the end flag, so the loop goes on to entry 98, 99, … in the same frame. These are
main's variables: entry 98 = {[0x4207a9] page (0..3), event 0x6c000000} → fires, no case; entry 99 = {0x800042..
(bytes of the heap start / size; negative), 0x..009896} → fires, no case; entry 100 = {0x00..2461.. , ...}: in the
capture its pos (0x00246100) is above 0xf3f and the loop stops (final index 100). On a real machine entry 100's pos
holds the low byte of the DOS segment of the VBE block in its top byte; if that is ≥ 0x80 the loop would run on
into more variables. Only the very last frame (drawn before the exit) could be affected. Nothing for the remake.

### 4.2 The event switch 0x400b6a

```c
v = event + 1;                                         // unsigned compare: v > 0x44e → no case
// repne scasw over 39 words at 0x4000c3 (38 case values event+1, the 39th word is the low word of the
// default target), then jmp [0x40010f + 4 * remaining_count]: the jump table is in reverse order
```

All 38 cases (handler addresses; "f" = float store, the others are dword integers):

| event | handler | does |
|---|---|---|
| −1 | 0x400e6e | end = 1 |
| 0 | 0x400b90 | part = 0 (draws nothing; not in the table) |
| 1 | 0x400b9c | part1_setup() 0x4053f2; part = 1 |
| 2 | 0x400bc3 | part = 2 |
| 3 | 0x400bcf | part3_setup() 0x405b10; part = 3 |
| 4 | 0x400be0 | part = 4 |
| 5 | 0x400bec | part = 5; grid_texture(0xe35128) 0x4033a7 (the colour table B replaces A in the grid texture) |
| 6 | 0x400c06 | part6_setup() 0x4065c5; part = 6 |
| 7 | 0x400c17 | part = 7; [0x427f70] = 1.0f; set_proj(−0.625f, −0.390625f) 0x414f04 |
| 8 | 0x400c3f | part = 8 |
| 9 | 0x400c4b | part = 9; [0x427f70] = 1.2f (0x3f99999a) |
| 10 | 0x400c61 | part = 10; set_proj(−0.625f, −0.3125f) |
| 11 | 0x400c74 | part = 11 |
| 99 | 0x400c80 | part = 99 (not in the table) |
| 101 | 0x400c8c | [0x4224f8] = 2000; [0x4224fc] = 2000; [0x422500] = 1 |
| 102 | 0x400caf | [0x4224fc] = 0; [0x4224f8] = 0 |
| 103 | 0x400cc8 | [0x422500] = 0 |
| 104 | 0x400cd7 | [0x4224f8] = 0; [0x4224fc] = 2000; [0x422500] = 1 |
| 105 | 0x400cfa | [0x4224f8] = 0; [0x4224fc] = 0; [0x422504] = 255.0f |
| 106 | 0x400d1d | [0x422510] = 1.0f |
| 107 | 0x400d2c | [0x422508] = 1.0f |
| 111 | 0x400bad | part = 1 (**no set-up**: part 1's state goes on from where its first run left it); [0x422510] = 0 |
| 201 | 0x400d3b | [0x428160] = 0.3f (0x3e99999a); [0x42815c] = 1 |
| 301 | 0x400d54 | [0x422518] = 1 |
| 401 | 0x400d63 | if (++[0x421a7c] == 3) [0x421a7c] = 0 |
| 402 | 0x400d85 | [0x421a80] = 1 |
| 403 | 0x400d94 | [0x421a84] = 1; [0x421a88] = 0 |
| 501 | 0x400dad | [0x421a54] = [0x421a54] × −1.0f (f) |
| 601 | 0x400dc4 | [0x42252c] = 1 |
| 701 | 0x400dd3 | [0x427f74]++ |
| 801 | 0x400dde | [0x427ff0] += 2 |
| 802 | 0x400dea | [0x427fe8] = 1.0f (not in the table) |
| 803 | 0x400df9 | [0x427f70] = (float)([0x427f70] + (−0.25)) and [0x427fe4] = (float)([0x427fe4] + (−1.0f)) (f, both computed in 64-bit precision and stored truncated; [0x427fe4] is a float, 9400.0 in the image) |
| 901 | 0x400e23 | [0x428014] = 1.0f |
| 902 | 0x400e2f | if (++[0x428140] == 3) [0x428140] = 0 |
| 1011 | 0x400e4a | [0x427f70] = 1.2f |
| 1012 | 0x400e56 | [0x427f70] = 1.0f |
| 1101 | 0x400e62 | [0x428468] = 1 |

`set_proj(a, b)` = 0x414f04 (stack args, caller pops): [0x422534] = a, [0x422538] = b (floats; read by Craft's
texture mapper 0x415127, engine scope). The precalc sets (−0.625, −0.3125) (0x416806); event 7 changes the second
to −0.390625 for part 7, event 10 restores it for part 10.

### 4.3 The part switch 0x400e80

A compare tree on [ebp−0x14]: 1 → 0x405527, 2 → 0x4198bf, 3 → 0x405c45, 4 → 0x4037c0, 5 → 0x403530,
6 → 0x406677, 7 → 0x416fce, 8 → 0x417929, 9 → 0x4185e5, 10 → 0x41958e, 11 → 0x419dc7, 99 → 0x400079; any
other value (0 before event 1) draws nothing. The part routines take no arguments; everything goes through
globals (dt [0x420489], the draw target [0xe350ec], the event variables).

## 5. Time: t70 and dt (0x400f76..0x400fbf), exactly

```c
// globals: double t70 [0xe9572e], double t70_prev [0xe95736] (0 in the image), float dt [0x420489] (6.0f in the
// image), float acc [0xe9573e] (written, never read)
s32 timer = xmpGetTimer();                         // 0x4022b3
long double t = (long double)timer * C;            // C = double at 0x41a128 = 5.8675115883353874e-05 = 1/17043
t70 = (double)t;                                   // fst qword, truncated to 53 bits
long double d = t - t70_prev;                      // 64-bit precision, t not yet rounded to double
dt = (float)d;                                     // fst dword, truncated to 24 bits
acc = (float)(d * 0.03 + acc);                     // 0x41a130 = 0.03 (double)
t70_prev = t70;                                    // the double
```

### 5.1 The unit: 1/17043 of the PIT clock

The constant at 0x41a128 is 1/17043 to the last bit (17043 = 1,193,010 / 70: the authors used 1,193,010 Hz for the
PIT). t70 = timer / 17043, so **one t70 unit = 17043 PIT clocks = 1/70.0101 s**, 0.0144 % shorter than 1/70 s
(at the end of the intro t70 = 15,187 instead of 15,185: 2.3 units = 33 ms). `full.flips` and NOTES.md compute
t70 with 70 / 1,193,182; the program does not. For the remake: `t70 = seconds_of_timer × 1193182 / 17043`.

Checked: `models/core/dtmodel.py` replays the step over every timer value in `full.flips` (prev = 0, acc = 0 at
the start) with exact rational arithmetic truncated to 64 / 53 / 24 bits as above; t70, dt and acc equal the
snapshot values bit for bit at all 12 snapshots.

Precision: dt is truncated to float once per frame; the sum of the dt of a part telescopes to t70(m) − t70(start)
up to ~6·10⁻⁸ relative (truncation toward zero, always the same sign): negligible. What does accumulate rounding is
the parts' own float32 accumulators (part scopes).

### 5.2 Order in a frame, and the first frames

Frame n: events (positions read at about timer(n)) → the part draws with dt_n = t70(n) − t70(n−1) → shown →
timer read = timer(n+1). So **frame n shows the state integrated up to t70(n)**, the timer value read at the end
of frame n−1 (`full.flips` column 4 is exactly this value: "the timer the frame was drawn with").

- Flip 1 (before the loop, before xmpPlay): nothing drawn, page 0 (black after the mode set) shown.
- Flip 2 = the first frame of part 1: dt = **6.0** (the image's initial value of [0x420489]).
- Flip 3: dt = t70(3) − 0 = **−4.1263** (timer −70,335: xmpGetTimer is negative before the second player interrupt).
- From flip 4 on dt = t70(n) − t70(n−1) > 0 (0.72 at flip 4).

So part 1's integrated time at frame m ≥ 3 is 6.0 + t70(m) (and 6.0 at frame 2); every dt-driven variable of
part 1 sees one negative dt at its second frame.

### 5.3 When an event fires, and where a part's clock starts

**xmpGetPos is one player tick ahead of xmpGetTimer.** At the interrupt that starts position p the timer is
p × 66300 − 22100 (66300 = 3 ticks × 22100 PIT clocks; speed 3, 135 BPM, no speed or tempo changes): e.g. tick
2315 sets 0c:00, and the timer read 1.25 ms later (after flip 10396) is 50,897,791 = 768 × 66300 − 22100 + 1,491.
(Probably because the player tick prepares the row and the GUS update of the next interrupt starts its notes, so
the timer is the one aligned with the sound; the music scope has the details.) Hence:

- an event at position p fires at the first frame n with timer(n) ≥ p × 66300 − 22100 (to within the few
  instructions between the timer read and xmpGetPos), i.e. up to 18.5 ms before its row is heard plus up to one
  frame late;
- a part entered at frame n draws its first frame with dt = t70(n) − t70(n−1). After frame m its integrated time is
  **t70(m) − t70(n−1)**: it starts at the timer of the previous part's last frame, slightly before the event.

Measured in the capture (`models/core/partclock.py`; T_event = (p × 66300 − 22100) / 17043; offset =
T_event − t70(n−1), always in (0, first dt]):

| part | position | first flip n | first dt | t70(n−1) | T_event | offset (t70 units) |
|---|---|---|---|---|---|---|
| 1 | 00:00 | 2 | 6.0 | (none) | −1.2967 | special: clock = 6.0 + t70 (5.2) |
| 11 | 08:20 | 1392 | 2.4709 | 2114.1087 | 2114.9504 | 0.8417 |
| 2 | 0c:00 | 10397 | 0.1066 | 2986.3272 | 2986.3463 | 0.0191 |
| 8 | 10:00 | 10974 | 1.7343 | 3981.1581 | 3982.2273 | 1.0692 |
| 6 | 14:18 | 11815 | 1.4542 | 5071.0155 | 5071.4722 | 0.4566 |
| 9 | 18:00 | 12188 | 2.0333 | 5973.9528 | 5973.9893 | 0.0366 |
| 5 | 20:00 | 13742 | 1.9968 | 7964.5946 | 7965.7513 | 1.1567 |
| 7 | 24:00 | 14334 | 1.6818 | 8960.4406 | 8961.6323 | 1.1918 |
| 3 | 29:00 | 15617 | 0.2991 | 10206.2020 | 10206.4836 | 0.2816 |
| 4 | 30:20 | 17000 | 1.0692 | 12073.6714 | 12073.7605 | 0.0891 |
| 10 | 36:00 | 17764 | 1.7868 | 13442.0276 | 13443.0969 | 1.0692 |
| 1 again | 3a:00 | 18947 | 0.3166 | 14438.8363 | 14438.9779 | 0.1416 (no set-up: part 1's variables continue from frame 1391) |

### 5.4 Time base summary for the remake

- A variable advanced `v += k * dt` in every frame of part P from v0 at the part's start (set-up or static value)
  is, at the frame drawn with timer value t: **v = v0 + k × (t70(t) − T_event + offset_P)**, with
  t70(t) = t × 1193182 / 17043 for t in seconds of music timer, T_event = (p × 66300 − 22100) / 17043 and offset_P
  from the table (between 0 and one original frame interval; ≤ 1.2 units = 17 ms). Using offset 0 shifts a part by
  at most 17 ms.
- Events: fire them when the music timer reaches (p × 66300 − 22100) PIT clocks; in the original they land on the
  first frame after that, so a remake at a higher frame rate is up to one original frame earlier (the capture's
  frame rates are in PARTS.md).
- Everything nonlinear in dt, or per frame, depends on the frame rate (part scopes; the shared helpers that do so
  are 0x4045b4 and the random generator, section 9).

## 6. The frame routine 0x400000

```c
void show_frame(void)                      // keeps EDX
{
    if (bpp == 32) {                       // [0x42048d]
        set_display_start(0, (s16)(page * 200));   // [0xea577c] = 0x40141f: 4F07 BX=0 CX=0 DX=page*200, no retrace wait
        if (++page == 4) page = 0;         // [0x4207a9] (dword, 0 in the image)
        draw_target = lfb + page * 0x3e800;        // [0xe350ec]; lfb = [0xe35100]
    } else if (bpp == 15) {
        draw_target = (u32 *)0xdb1aec;     // fixed 320x200 buffer in main memory (just after the heap's end)
        convert15(0xdb1aec, lfb);          // 0x4188d4 (stack args), section 12
    }
}
```

32-bit: four pages of 0x3e800 bytes (320 × 200 dwords, 1280 bytes per line assumed: the BytesPerScanLine of the
mode is stored in [0xea5764] and never used). It shows the page that was just drawn; the next frame draws into the
page shown four frames earlier, so whatever a part leaves untouched shows its picture of four frames ago. The mode
set clears video memory (bit 15 of the mode number is clear), so the first frames of part 1 draw over black.
15-bit: the buffer is cleared by the conversion, so a part that leaves pixels untouched shows black there instead.
Snapshots agree: target = 0xe0000000 (the emulator's LFB) + page × 0x3e800 at all 12 snapshots.

## 7. The dead fade (0x400f2e..0x400f71)

Runs only while pos > 0x1020 (order 0x40, row 0x20). The song has orders 0..0x3c and ends at 3c:3f (pos 0xf3f), so
it never runs. If it did: vol16 starts at 0x400, so the volume would jump from 0x20 to 0x40, then fall by 2 × dt / 16
per frame (to 1 after 504 t70 units, 7.2 s) and set the end flag.

## 8. Exit

The loop ends at the top of the frame after event −1 (the last frame is drawn and shown; in the capture tick
11723). xmpStop (0x40219a), 4F02 BX=3 (VBE call, clears the screen), the two DOS blocks freed (0x401844: int 31h
0101 for [0x4207c0], then [0x4207bc]), the title printed, return → int 21h 4C00. A key press ends the intro the same
way (the key stays in the BIOS buffer).

## 9. The shared random generator 0x420491 (not in any scope's list)

```c
static u32 seed = 1;                       // [0x420491], 1 in the image
int rnd(void) { seed = seed * 0x41c64e6d + 0x3039; return (seed >> 10) & 0x7fff; }   // inlined
```

Inlined at 31 sites: 7 in 0x4051d5 (part 1), 7 in 0x40590c (part 3), 3 in 0x40600f and 14 in 0x40628f (part 6);
the particles scope describes the calls. The state is one global, never reseeded, so the numbers a part gets depend
on how many were drawn before, by all parts, and those that draw per frame make the count frame-rate dependent.
Numbers drawn since the start, at the snapshots (`models/core/rngstate.py`; each snapshot is the first tick of a
part, after its set-up and first frame or frames): t11 0; t1645, t2316, t3084 84,546 (all drawn by part 1's first
run); t3925 96,670 (part 6 started); t4621, t6156, t6924 216,354; t7884 216,361 (part 3 started); t9324, t10380
315,145; t11148 329,145 (part 1 again). Parts 2, 4, 5, 7..11 draw none.

## 10. Precalc tables (all verified byte for byte against t11.snap)

### 10.1 0x416806 (engine precalc)

```c
set_proj(-0.625f, -0.3125f);                       // 0x414f04 → [0x422534], [0x422538]
for (i = 1; i <= 4096; i++) ((u32 *)0xf5d3d8)[i] = 0;   // 0xf5d3dc..0xf613db; the BSS is zero already
                                                   // (read by the engine at 0x4169df..; 0xf5d3d8 also by part 6)
for (i = 0; i < 16; i++) for (j = 0; j < 16; j++) {   // bilinear weights, 16x16 sub-positions, sum 256
    float *w = (float *)0x426d8c + (j * 16 + i) * 4;
    w[0] = (16-i)*(16-j); w[1] = (16-i)*j; w[2] = i*(16-j); w[3] = i*j;
}
for (k = 0; k < 512; k++) {                        // exp table 0x426b8c (bytes)
    int v = (int)(128.0 * exp((k - 256) * 0.011));  // 0.011 = double 0x41a4a8, 128.0 = 0x41a4b0; e^x as 2^(x·log2 e)
    ((u8 *)0x426b8c)[k] = v > 255 ? 255 : v < 0 ? 0 : v;    // 1 at k = 0 … 128 at 256 … 255 from k = 319
}
```

The weights and the exp table are read by Craft's texture mapper (0x415a1d.., 0x415aae..; engine scope). The exp
table is the inverse of 0x4167b7 (11.6): 0x426b8c[L(a) + L(b)] = (1+a)(1+b)/128 (before truncation) for
L = 0x4167b7, a multiplication in an 8-bit log domain (steps of 1.1 %). The maps hold values made by 0x4167b7 and
the mapper indexes the table with a texel plus a shade value (0x415a9e: `ebp = shade >> 16; … + 0x426b8c`), so it
multiplies texel and light this way (reading only; the engine scope has the details). The remake multiplies
directly.

### 10.2 0x403f10: signed multiply table

```c
u8 *t = (u8 *)0xea740c;                            // 64 rows of 512 bytes (0x8000), ends at 0xeaf40c
for (i = 0; i < 64; i++) for (j = -256; j < 256; j++) *t++ = (u8)((i * j) / 64);   // C division, toward zero
*(u8 **)0x4224e8 = (u8 *)0xea750c;                 // = row 0, column j = 0: mul(i, j) = ptr[i * 512 + j]
```

Users: the blend blit 0x40442c (11.3) and the radial blur 0x4041d9.. (particles). It is `trunc(i × j / 64)`; the
remake computes `j × i / 64` in float.

### 10.3 0x40347a: exp table 0x420854 (grid)

```c
for (k = 0; k < 512; k++) {
    int v = (int)exp((k - 256) * 0.043478260869565216);     // 1/23, double at 0x41a144
    ((u8 *)0x420854)[k] = v > 255 ? 255 : v < 0 ? 0 : v;    // 0 for k < 256, 1 at 256, 255 from k = 384
}
```

Inverse of 0x4034e7 (11.7): 0x420854[L(a) + L(b)] = (1+a)(1+b) (before truncation) for L = 0x4034e7: a
log-domain multiplication with steps of 4.4 %. The colour tables behind the grid texture are made with 0x4034e7,
and the grid filler indexes this table with a shade value plus a texel (0x402bdc..0x402bea; grid scope).

### 10.4 0x4033ee: bilinear weights 0x420a54 (grid)

The same 16 × 16 × 4 floats as 0x426d8c (same index (j·16 + i)·4, same values), a second copy for the grid filler
(0x402b5b..). They are the weights of 16×16-pixel bilinear interpolation; the remake interpolates per pixel.

## 11. Shared 2D helpers (all checked in unicorn against these models)

Pixels are dwords 0x00RRGGBB in a 320-wide buffer (stride 1280 bytes).

### 11.1 0x4043be fill rectangle (stack args: dst, colour, w, h)

```c
for (y = 0; y < h; y++) for (x = 0; x < w; x++) dst[y * 320 + x] = colour;   // w, h used as bytes: 0 means 256
```

19 callers (parts 3, 4, 5, 7, 8, 9, 10, 2, 11: black boxes behind the text pictures, bands, tiles).

### 11.2 0x404002 picture blit (EAX = picture {u32 *pixels; int w, h;}, EDX = x, EBX = y, ECX = buffer; stack: mode, level; callee pops 8)

```c
u32 *d = buf + y * 320 + x;
if (mode == 0 || (mode == 1 && level <= 0)) key_blit(pic->pixels, d, pic->w, pic->h);          // 0x4043ec
else if (mode == 1) blend_blit(pic->pixels, d, pic->w, pic->h, level > 63 ? 63 : level);        // 0x40442c
// any other mode: nothing
```

0x4043ec (stack args src, dst, w, h): copies every source pixel that is not 0xffffffff (the transparent value the
picture decoder 0x403f53 writes for index 0 / 0xf0). No clipping.

### 11.3 0x40442c blend blit (stack args src, dst, w, h, level)

```c
for each source pixel s != 0xffffffff, destination d, channel c in B, G, R:
    out.c = s.c + mul[level][d.c - s.c];          // = s.c + trunc(level * (d.c - s.c) / 64), table 10.2
out byte 3 = s byte 3 (0)
```

**`level` is the weight of the background**: 0 = the picture opaque (and 0 or negative takes the key blit),
63 = 63/64 background. Above 63 it is clamped to 63, so the blit never makes a picture fully invisible (white on
black leaves 255 + trunc(63 × −255 / 64) = 4). Remake: `out = mix(src, dst, level / 64)` from the caller's
continuous level.

### 11.4 0x4045b4 feedback (stack args src, dst; 64,000 bytes)

```c
for (i = 0; i < 64000; i++) dst[i] = (15 * dst[i] + src[i]) >> 4;    // 16-bit arithmetic, no overflow
```

Called once per frame by part 1 (0x40577f): per-frame, frame-rate dependent. The floor makes it asymmetric: a
value falling towards src converges to it exactly, a value rising towards src stops when it is 1..15 below it
(fixed points src − 15 ≤ dst ≤ src). Continuous equivalent: dst → src with factor (15/16) per original frame.

### 11.5 0x404a08 8×8 block through a palette (stack args src8, dst32, pal)

```c
for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) dst32[y * 320 + x] = pal[src8[y * 320 + x]];
```

Callers (0x40586f part 3, 0x405eba part 3, 0x406832 part 6, and part 1 through them) loop over the blocks and
choose a palette per block (the per-band palettes). The 8×8 grouping only exists for the per-block palette choice;
the remake evaluates the palette per pixel.

### 11.6 0x4167b7 log curve A (stack arg float x; callee pops 4) → EAX

```c
int v = (int)(log((1.0L + x) * 0.0078125) * 90.9090909090909 + 128.0);   // 0x41a490 1/128 (float), 0x41a498 (double)
return v > 255 ? 255 : v < 0 ? 0 : v;       // x <= -1 on an x87: invalid → 0x80000000 → 0
```

90.9090909090909 = 1000/11 = 1/0.011: the inverse of the exp table 0x426b8c. Callers: main's map generation
(gfx). Checked on 4,210 values; equal except x < −1, where unicorn's fyl2x returns a number instead of an invalid
result (the callers pass x ≥ 0, gfx scope).

### 11.7 0x4034e7 log curve B (stack arg float x; callee pops 4) → EAX

```c
int v = (int)(log(1.0L + x) * 23.0 + 128.0);       // 0x41a14c 23.0, 0x41a154 128.0
return v > 255 ? 255 : v < 0 ? 0 : v;
```

Inverse of the exp table 0x420854. Callers: main's colour tables (gfx).

## 12. The 15-bit conversion 0x4188d4 (stack args src32, dst16)

```c
for (i = 8; i < 63992; i++) {                // 3999 iterations of 16 pixels; offsets 0x3e7e0 / 0x1f3f0, i = 63992 + eax
    u32 s = src32[i];
    dst16[i] = (((s >> 16) & 0xff) >> 3) << 10 | (((s >> 8) & 0xff) >> 3) << 5 | ((s & 0xff) >> 3);   // 5:5:5, truncated
    src32[i] = 0;                            // clears what it read
}
```

Two pixels per dword write; two dummy reads of the buffer's end first (cache warm-up). **Pixels 0..7 and
63992..63999 (the first 8 of the top line, the last 8 of the bottom line) are never converted or cleared**: they stay
as the mode set left them (black). Hard-coded 5:5:5 and 640 bytes per line; the masks of the mode are not read. Only
in the 15-bit mode; the capture is 32-bit.

## 13. The packed program: NOTES.md checked, with additions

`models/core/veunpack.py` was written from NOTES.md alone (MZ size → LE header → VE header → decompressor → fixups)
and gives `unpacked/clone_img.bin` exactly (sha256 4931c572…, FNV-1a 64 4adf7830170c1b95 at base 0x400000;
95367b24703a7595 at base 0), 2,570 dword fixups, all 2,607 bytes of the fixup list used. The description is
enough for a C++ unpacker. Details that the notes leave open, from the veloader's code (0x10289):

- A fixup record of type 3 does both: add the base to the dword at the site, site += 4, then the selector word at
  the (new) site, site += 2. The selector is chosen by the low byte of that word **before** it is overwritten
  (0 → CS, else DS). After each entry the next delta is added to the site as it stands after the entry, so for
  type 1 the next site = site + 4 + delta. Deltas: big-endian base-128 (first byte = most significant 7 bits).
- The decoder only stops by output size; the packed sizes (VE+0x14 / +0x18) are only needed to read the data. The
  bit reader refills a 16-bit word when 16 or fewer bits remain, so it reads up to 2 bytes past the end of the
  image stream (49,225 bytes, odd): a C++ decoder must pad the input with zero bytes (the bits are peeked, never
  used).
- Errors: unknown fixup type or incomplete code lengths → exit code 5 (int 21h 4C05).
- The base added is the linear address of the block (EDI = [0x20500]); the program's ESP = (base + 0xb93940) & ~3
  and it is called at base + 0x10de with ES = PSP selector.

C++ outline: read CLONE.EXE; `mz = (e_cblp ? (e_cp − 1) × 512 + e_cblp : e_cp × 512)`; `le = (mz + 7) & ~7`;
`ve = le.datapages + le.pages × 0x1000 − (le.lastpage ? 0x1000 − le.lastpage : 0)`; check "VE"; decompress image
(VE+0x24 bytes, method byte VE+0x40) and fixups (VE+0x28 bytes, method VE+0x41) from VE + VE[4] / VE + VE[8];
apply the fixups with the base 0x400000 (so the remake can use the addresses of these notes as offsets
`a − 0x400000`), append VE+0x2c zero bytes for the BSS; check size 0x2846c and the hash.

## 14. Runtime (Watcom C and PMODE/W)

### 14.1 Heap 0x400ff4 / 0x40100c / 0x401015 / 0x401083

```c
void heap_init(u8 *start /*ESI*/, u32 size /*EAX*/)  // 0x400ff4
{ heap_size = size; heap_start = start; *(u32 *)start = size - 4; }   // [0x4207b4], [0x4207b0]: one free block

u8 *alloc(u32 n /*EAX*/)                             // 0x40100c → ESI, CF = 0; CF = 1 (ESI unchanged) if no block
{
    n = (n + 1) & ~1;                                // rounded up to even (all requests here are multiples of 4)
    for (u32 *h = heap_start; h != heap_start + heap_size; h = (u8 *)(h + 1) + (*h & ~3)) {
        if (*h & 1) continue;                        // used
        u32 s = *h;
        if (s < n) continue;
        if (s != n) {
            if (s - n < 4) continue;                 // a remainder too small for a header: block skipped
            *(u32 *)((u8 *)(h + 1) + n) = s - n - 4; // split: free remainder
        }
        *h = n | 1;
        return (u8 *)(h + 1);                        // not cleared (nothing is ever freed, so it is BSS zero)
    }
}
```

0x401015 (free: clear bit 0 of the header, then 0x401083 merges each free block with free followers over the
whole heap) is never called. The allocation order is fixed, so the addresses are constant (heap walk of t11,
`models/core/heapwalk.py`): module 0x428470 (0x124f80); maps 0x54d3f4, 0x58d7fc, 0x5cdc04 (0x40404 each); the ten
pictures, each a 12-byte header {pixels, w, h} followed by its pixel block: 0x60e00c "meet us", 0x60e608 "get
them clones", 0x60f60c "connect the dots", 0x6103e0 "tunnels…", 0x611a24 / 0x61686c / 0x61aa74 / 0x61f2c8 the
glyphs, 0x902c8c title, 0x903ad4 credits; part 7 buffers 0x622adc, 0x62fb48, 0x63cbb4 (0xd068 each) and 0x649c20
(0x24000); part 8 the same at 0x66dc24.. 0x694d68; part 9 0x6b8d6c, 0x6eb9f0, 0x71e674 (0x32c80 each), 0x7512f8
(0x8ca00); part 10 0x7ddcfc, 0x810980, 0x843604, 0x876288; free from 0x904d68 (4.9 MB). The heap ends at
0xdb1aec, where the 15-bit draw buffer starts.

### 14.2 Small helpers

- 0x4010cc memset(EAX = dest, DL = byte, EBX = count), keeps all registers; count 0 = nothing.
- 0x4010dd: empty (Watcom __CHP).
- 0x40110c int386x(EAX = interrupt, EDX = in REGS, EBX = out REGS, ECX = SREGS) → EAX = out.eax. REGS = {eax, ebx,
  ecx, edx, esi, edi, cflag} dwords; SREGS = {es, cs, ss, ds} words. 0x401125 / 0x40115c load ES and DS from
  SREGS and the registers, `ret` into a stub `int n; ret` at 0x401180 + 3n, store the results, cflag = −CF, and
  write the returned ES and DS back to SREGS. Used only by the VESA lib (REGS 0xea5744, SREGS 0xea5768, the DPMI
  real-mode register block 0xea5780).

## 15. Submissive's VESA lib 0x401235..0x401853

All VBE calls go through 0x401288: int 31h AX=0300 (simulate real-mode interrupt) BL = 0x10, BH = 0, CX = 0,
ES:EDI = the real-mode register block 0xea5780 (EDI +0, ESI +4, EBP +8, EBX +0x10, EDX +0x14, ECX +0x18, EAX
+0x1c, flags +0x20, ES +0x22, DS +0x24, …), cleared by 0x401250 before each call. Results are not checked.

| routine | in → out | does |
|---|---|---|
| 0x401235 | EAX = 512-byte buffer | zeroes 4 bytes, writes the dword 0x56423532 = bytes "25BV" (meant "VBE2"; a VBE 2.0 BIOS treats it as a 1.x request) |
| 0x401250 | – | clears 0xea5780 (0x32 bytes), 0xea5768 (12), 0xea5744 (0x1c) |
| 0x4012db | EAX = paragraphs, EDX = &result | int 31h 0100 → [EDX] = segment, [EDX+2] = selector |
| 0x401326 | EAX = physical, EDX = size → EAX | int 31h 0800 (map physical memory) → linear |
| 0x401388 | EAX = mode, EBX = buffer, ECX = selector | 4F01 into the DOS block [0x4207c0]:0, then 256 bytes copied to ECX:EBX (`repne movsd`: REPNE acts as REP on movs) |
| 0x4013f3 | AX = mode → EAX | 1 if ModeAttributes bit 7 (LFB available) |
| 0x40141f | EAX = x, EDX = y | 4F07 BX = 0 (set display start, no retrace wait), CX = x, DX = y; [0xea577c] |
| 0x401457 | EAX = mode, EDX = lfb, EBX = clear | 4F02 BX = (mode & 0x1ff) \| (lfb ? 0x4000 : 0) \| (clear ? 0 : 0x8000); then 4F01: [0xea5764] = BytesPerScanLine (unused), [0xea57b2] = BitsPerPixel; 0x401809 |
| 0x4014c4 | EAX = 512-byte buffer, EDX = selector | copies the request to the DOS block [0x4207bc], 4F00 (ES:DI = segment:0), copies 512 bytes back, converts the far pointers at +6, +0xe (VideoModePtr), +0x16, +0x1a, +0x1e to linear (seg × 16 + off) |
| 0x401628 | EAX = width, EDX = height, BL = bpp → EAX | walks VideoModePtr until 0xffff; the first mode whose XResolution (+0x12), YResolution (+0x14) and BitsPerPixel (+0x19) match; −1 if none. No check of the memory model, masks, pitch or attributes |
| 0x4016b0 | EAX = mode → EAX | maps PhysBasePtr (+0x28 of the mode info) with size TotalMemory (+0x12 of the VBE info) × 64 KB |
| 0x401809 | – | [0xea5778] = 0x4016fb, [0xea577c] = 0x40141f |
| 0x40181e | – | DOS blocks: 0x20 paragraphs → [0x4207bc] (segment) / [0x4207be] (selector), 0x10 → [0x4207c0] / [0x4207c2]; 0x401809 |
| 0x401844 / 0x401853 | – / EAX = &block | int 31h 0101 (DX = selector) for [0x4207c0], then [0x4207bc] |
| 0x4015c9, 0x4015fe, 0x4016fb, 0x401742, 0x40176c, 0x4017b7, 0x4017e0 | | not called: version ≥ 2.0, memory size, 4F05 bank switch with cache [0x4207c4] (through [0xea5778], never called), 4F06 get max scan line (returns the wrong register), 4F06 set scan line length, 4F08 DAC width, DAC capability |

## 16. Part 99 (0x400079) — never selected

```c
for (i = 0; i < 6400; i++) target[i] = 0;              // rows 0..19
for (i = 6400; i < 57600; i++) target[i] = 0x000040;   // rows 20..179: blue 64/255
for (i = 57600; i < 64000; i++) target[i] = 0;         // rows 180..199
```

Overwrites the whole frame. No event selects it (it would be event 99).

## 17. Rendering shortcuts in the core's code

- 8-bit buffers expanded in 8×8 blocks through per-block palettes (0x404a08): per pixel in the remake.
- Log / exp tables with 8-bit indices (10.1, 10.3, 11.6, 11.7): multiplications in an 8-bit log domain
  (1.1 % and 4.4 % steps); the remake multiplies in float.
- Multiply table in 64ths (10.2) and the blend levels in 64ths (11.3): float weights.
- Bilinear weight tables for 16×16 cells (10.1, 10.4): per-pixel interpolation.
- 15-bit output with truncation to 5 bits per channel (12): the remake is true colour; only the 32-bit mode is the
  reference.

## 18. Corrections and additions to NOTES.md / PARTS.md

1. **t70 = timer / 17043** (constant 0x41a128), not timer × 70 / 1,193,182; `full.flips` column 5 and NOTES.md
   ("time in 1/70 s") use the latter. The difference is 0.0144 % (33 ms at the end).
2. xmpGetPos is one player tick (22,100 PIT clocks) ahead of xmpGetTimer: position p is first returned at timer
   p × 66300 − 22100. The part clocks start at the timer of the previous part's last frame (table 5.3).
3. The second loop frame runs with dt = −4.1263 (NOTES says so; worth stressing for part 1's nonlinear state).
4. The event switch also has case 0 (part = 0); with 99 and 802 three cases no table entry uses.
5. Event 111 does not call part 1's set-up: the second run of part 1 continues its state.
6. Event 803: [0x427f70] += −0.25 and [0x427fe4] += −1.0 are float adds rounded by truncation.
7. [0xe35114] = [0xe3510c] (map B) is set in the precalc and read by part 10 (0x418fa9).
8. The random generator at 0x420491 (section 9) is shared by parts 1, 3, 6 and never reseeded.
9. The 15-bit conversion leaves the first 8 and last 8 pixels of the screen untouched.
10. 0x404002: level ≤ 0 means opaque, > 63 is clamped to 63 (never invisible).
11. The xmpInit argument EAX is the dword at 0x4207b8: PSP selector | 0x2461 << 16 (image), bit 31 clear.
12. The VE fixup details of section 13 (type 3 order, selector choice, read-ahead padding).
13. After event −1 the event loop reads main's variables as table entries 98..100 (4.1); harmless.

## 19. Not determined

- Whether xmpGetTimer's one-tick offset to xmpGetPos is exactly what is heard on a real GUS (the timer is aligned to
  the GUS update of the next tick; the music scope models the player).
- How a 1998 machine's BIOS reacts to the "25BV" signature and whether its VideoModePtr list contains a 320×200×32
  mode with an LFB (many cards of the time had none; then the 15-bit path runs). Not needed for the remake.
- The run-off past the sync table on a real machine (4.1) depends on the DOS segment values; only the last frame.
