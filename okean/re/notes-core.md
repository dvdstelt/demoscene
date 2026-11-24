# Scope "core": main, the 50 Hz mode, the retrace timer, the clocks, the text printer and its font, the end part

Routines: main 0x100c4 (option test 0x102eb), the retrace timer 0x14bec..0x14d1b and its seven copies, the parts'
common main loop, the fake music clock in every callback, the text printer 0x4cacc with its font (pixels 0x3cec2,
widths 0x4c8c2), the last part 0x4d370 with its timer copy 0x3cd70..0x3ceab (install 0x3cd7c, restore 0x3cdd0,
measure 0x3cdf7, handler 0x3ce48) and its callback 0x4d645.

Addresses are code32 offsets (file offset = 0xec0 + address, linear in the emulator = 0x8fc0 + address).
"Refresh k" = screen refresh k of the 50 Hz mode, "record k" = record k of `full.frames`, "line" = scan line
0..624 of a refresh (vertical retrace at lines 502..503). Positions are order:row in decimal. Scripts in
`models/core/` (its README lists them).

Verified against the capture:
- the eight timer copies are byte-identical except for their callback pointer (`find_timers.py`);
- event logs of the whole intro (`run_core.py`, each part run from its snapshot; their `.ticks` equal `full.ticks`):
  one callback per refresh in every part, at line 502.00..502.13; which callback count every record shows
  (`record_clock.txt`, `clock.py`);
- the time of every music row (`rows.txt`, 1,494 rows), consistent with all 9,105 positions in `full.ticks` and the
  tick times in `full.gus` (`rows.py`, `ticks_gus.py`);
- the palette-timing rule on the title fade (all 48 records of the fade, `title_palette_check.py`);
- every caption of the shared printer: all glyph pixels of the 11 caption pages in records 5184..8034 equal the
  capture (`printer.py`);
- **the last part 0x4d370: a Python model renders records 8099..9108 bit for bit** (all 64,000 indices and the
  768-byte DAC of 1,010 of 1,010 records; `endpart.py`).

---------------------------------------------------------------------------------------------------------------

## 1. main 0x100c4 and the 50 Hz mode

```c
void main(void)                                   // 0x100c4
{
    sti();
    outb(0x21, 0x02);                             // PIC: IRQ 1 (keyboard) masked; Esc is polled at port 60h
    int10(0x0013);                                // mode 13h through PMODE's int 33h (real-mode int AL = 10h)
    if (!option('M') && !option('m'))             // 0x102eb: "/M" or "-M" anywhere in the command tail (>= 3 chars)
        set_50hz_mode();                          // 0x100f0..0x10198
    gus_detect();                                 // 0x142d7: ULTRASND=...; sets no_gus [0x142d6] = 1 if absent
    if (!no_gus) { gus_install_driver();          // 0x142f8: copies the driver table 0x14310 to 0x12b44
                   gus_init();                    // call [0x12b48] = 0x14935
                   mod_load(0xae0); }             // 0x13f28
    part_38469();                                 // tables of the fifth part (black screen)
    part_2c26f();  if (inb(0x60) == 1) goto end;  // Esc (scan code 1) after every part
    part_2e779();  if (inb(0x60) == 1) goto end;
    part_3554b();  if (inb(0x60) == 1) goto end;
    part_367e5();  if (inb(0x60) == 1) goto end;
    part_38520();  if (inb(0x60) == 1) goto end;
    part_3c8da();  if (inb(0x60) == 1) goto end;
    part_5334f();  if (inb(0x60) == 1) goto end;
    part_4d370();                                 // the end texts (no Esc test after it)
end:
    if (!no_gus) { mod_stop();                    // 0x13f18
                   gus_shutdown(); }              // call [0x12b4c] = 0x148ca
    int10(0x0003);
    outb(0x21, 0x00);
    pmode_exit();                                 // 0x3f3
}
```

Because the keyboard IRQ is masked, port 60h keeps the last scan code: Esc ends the running part (every main loop
tests it) and then main skips the remaining parts.

`set_50hz_mode` (0x100f0) changes only the CRT controller timing of mode 13h:

```c
outb(0x3d4, 0x11); outb(0x3d5, inb(0x3d5) & 0x7f);   // unprotect CRTC 0..7
outb(0x3c2, 0x63);                                    // misc output: 25.175 MHz dot clock, -hsync +vsync (= mode 13h)
CRTC[0x00..0x09] = 60 4f 50 82 54 80 6f 3e 00 41;    // word writes to 3D4h (index, value)
CRTC[0x10..0x17] = f6 88 8f 28 40 90 6c a3;          // 0x11 = 88h sets the write protection again
SEQ[1] = 01; SEQ[3] = 00; SEQ[4] = 0e;                // mode 13h's values
outw(0x3c3, 0x4005); outw(0x3c3, 0x0506);             // meant as GC 05h = 40h, 06h = 05h (mode 13h's values);
                                                      // really writes 05h, 06h to port 3C3h and 40h, 05h to the
                                                      // sequencer index 3C4h: no effect on add-in cards (3C3h is
                                                      // the video enable only on some motherboard VGAs, where 06h
                                                      // would switch the VGA off)
```

| register | mode 13h | 50 Hz mode | meaning |
|---|---|---|---|
| CRTC 00 horizontal total | 5f | 60 | 101 character clocks x 8 = **808 dots** per line (mode 13h: 800) |
| CRTC 06, 07 vertical total | bf, 1f | 6f, 3e | 0x26f + 2 = **625 lines** (mode 13h: 449) |
| CRTC 10 retrace start | 9c (412) | f6 + 100h = **502** | |
| CRTC 11 retrace end | 8e | 88 | ends when the line's low nibble is 8: lines 502..503 (2 lines) |
| CRTC 15, 16 vertical blank | 96, b9 | 90 (400), 6c | |
| CRTC 01..05, 08, 09, 12, 13, 14, 17 | | unchanged | 320 x 200, each row shown twice (400 lines, display end 0x18f) |

So: line rate 25.175 MHz / 808 = 31.157 kHz, line = 32.0953 µs, **refresh T = 808 x 625 / 25.175 MHz =
20.059583 ms = 49.8515 Hz**; lines 0..399 show the picture, the vertical retrace starts 502 lines = 16.112 ms after
line 0 and lasts 64.2 µs. One refresh is 1,193,182 x T = 23,934.7 PIT ticks. The picture is the ordinary 320 x 200
frame (shown at 4:3); only the rate changes. With `-M` the screen stays in mode 13h (70.086 Hz) and everything that
advances per callback runs 40 % faster while the music keeps its speed (NOTES.md); the remake follows the 50 Hz mode.

## 2. The retrace timer (0x14bec..0x14d1b and its copies)

Every part has its own copy of the same 316 bytes (0x14be0..0x14d1b: callback pointer, saved vectors, install,
restore, measure, handler, real-mode stub). The copies are byte-identical except for the callback pointer
(`find_timers.py`; their absolute addresses are relocated by the copy's offset):

| copy (callback pointer) | used by part | callback | install | restore | measure | IRQ 0 handler | real-mode stub | count word |
|---|---|---|---|---|---|---|---|---|
| 0x14be0 | 0x2c26f (title) | 0x2c47b | 0x14bec | 0x14c40 | 0x14c67 | 0x14cb8 | 0x14cf4 | 0x14d0d |
| 0x2c570 | 0x2e779 | 0x2eb81 | 0x2c57c | 0x2c5d0 | 0x2c5f7 | 0x2c648 | 0x2c684 | 0x2c69d |
| 0x2ede0 | 0x3554b | 0x357b3 | 0x2edec | 0x2ee40 | 0x2ee67 | 0x2eeb8 | 0x2eef4 | 0x2ef0d |
| 0x35900 | 0x367e5 | 0x36af4 | 0x3590c | 0x35960 | 0x35987 | 0x359d8 | 0x35a14 | 0x35a2d |
| 0x36cf0 | 0x38520 | 0x38985 | 0x36cfc | 0x36d50 | 0x36d77 | 0x36dc8 | 0x36e04 | 0x36e1d |
| 0x38be0 | 0x3c8da | 0x3cad3 | 0x38bec | 0x38c40 | 0x38c67 | 0x38cb8 | 0x38cf4 | 0x38d0d |
| 0x4d710 | 0x5334f | 0x5355a | 0x4d71c | 0x4d770 | 0x4d797 | 0x4d7e8 | 0x4d824 | 0x4d83d |
| 0x3cd70 | 0x4d370 (end) | 0x4d645 | 0x3cd7c | 0x3cdd0 | 0x3cdf7 | 0x3ce48 | 0x3ce84 | 0x3ce9d |

Addresses below are those of the first copy.

```c
void (*callback)(void);   // [0x14be0]
u32  saved_rm_vector;     // [0x14be4]  real-mode IVT entry of INT 8
u32  saved_pm_handler;    // [0x14be8]
u16  count;               // [0x14d0d]  = the immediate of the real-mode stub's "mov ax, imm16" (0x4100 in the file)

void timer_install(void)                       // 0x14bec
{
    cli();
    u32 lin = 0x14cf4 + code32_base;           // the real-mode stub
    *(u16 *)0x14d03 = (lin & 15) + 0x19;       // self-modifying: the stub's "add cs:[....], ax" addresses 0x14d0d
    saved_rm_vector = xchg(ivt[8], (lin >> 4) << 16 | (lin & 15));   // gs:[0x20]
    saved_pm_handler = pmode_get_irq(0);       // call [0x28]
    pmode_set_irq(0, 0x14cb8);                 // call [0x2c]
    outb(0x43, 0x30); outb(0x40, 0x00); outb(0x40, 0x41);   // PIT 0: lo/hi, mode 0 (one-shot), 0x4100 = 13.9 ms
    sti();
}

void timer_measure(void)                       // 0x14c67: interrupts off from here to the end (about 2 refreshes)
{
    cli();
    outb(0x43, 0x30);                          // PIT 0 mode 0: OUT low, counting stops until a count is written
    while (inb(0x3da) & 8) ;  while (!(inb(0x3da) & 8)) ;   // the next retrace start
    outb(0x40, 0); outb(0x40, 0);              // count 65536
    while (inb(0x3da) & 8) ;  while (!(inb(0x3da) & 8)) ;   // one refresh later
    u16 left = inb(0x40) | inb(0x40) << 8;     // the live count (no latch command)
    count = -left - 0xc0;                      // the refresh in PIT ticks (23,934 in the capture) - 192 (161 µs)
    while (!(inb(0x3da) & 8)) ;  while (inb(0x3da) & 8) ;   // the end of this retrace
    outb(0x40, count & 0xff); outb(0x40, count >> 8);      // first IRQ about 5 lines before the next retrace
    sti();
}

void irq0_handler(void)                        // 0x14cb8 (saves eax, edx, ds; runs with interrupts off)
{
    count += (inb(0x3da) & 8) ? -7 : +1;       // "and eax,8; dec eax; neg eax": already in the retrace -> earlier
    while (!(inb(0x3da) & 8)) ;                // wait for the retrace start
    outb(0x40, count & 0xff); outb(0x40, count >> 8);      // restart the one-shot
    callback();                                // the part's retrace callback
    outb(0x20, 0x20);                          // EOI, sti, iretd
}
// 0x14cf4: the same for an IRQ 0 taken in real mode (16-bit, count read from its own code), without the callback.
// The parts make no real-mode calls, so it never runs in the capture.

void timer_restore(void)                       // 0x14c40
{
    cli();
    ivt[8] = saved_rm_vector;
    pmode_set_irq(0, saved_pm_handler);
    outb(0x43, 0x36); outb(0x40, 0); outb(0x40, 0);         // PIT 0 mode 3, 65536: 18.2 Hz
    sti();
}
```

The count starts at the measured refresh minus 192 PIT ticks; the IRQ arrives about 5 lines before the retrace
(line 497..499 in the capture), and the count grows by 1 tick (0.84 µs) per refresh until the IRQ falls into the
retrace, where it loses 7. After about 190 refreshes the IRQ hovers just before line 502. In every case the handler
calls the callback **at the start of the vertical retrace, line 502**, once per refresh (checked in every refresh of
every part: line 502.00..502.13). Every part calls install and measure back to back, so the 0x4100
one-shot never fires.

## 3. The parts' common structure

```c
void part(void)
{
    set-up: tables, the first palette (DAC), for 0x2e779 / 0x38520 / 0x5334f also clear A0000 to index 0;
    push [8]; buf = getmem_high(64000);        // the draw buffer (the end part does not free its buffer)
    timer_install(); timer_measure();          // the part's own copy (section 2)
    do {                                       // main loop
        draw the frame into buf;               // from state the callback advanced: n callbacks so far
        [0x4cac4] = buf; print_page();         // the shared printer 0x4cacc (parts 1..7, section 6)
        (palette computed here: parts 0x3554b, 0x3c8da, 0x5334f; 0x5334f also writes it here)
        while (!(inb(0x3da) & 8)) ;            // retrace start: the IRQ 0 handler has just run callback n + 1
        while (inb(0x3da) & 8) ;               // retrace end (line 504)
        (palette computed here: parts 0x2c26f, 0x2e779, 0x367e5, 0x38520)
        write the DAC (when the part changes it);
        copy buf to A0000 (rep movsd, 16,000 dwords);
    } while (!exit_condition() && inb(0x60) != 1);
    timer_restore();
    pop [8];
}
```

| part | loop top | printer call (buffer) | retrace wait | palette | copy (buffer) | exit test after the copy |
|---|---|---|---|---|---|---|
| 0x2c26f | 0x2c314 | 0x2c3bc ([0x29a58]) | 0x2c3c1 | after the wait, only in order 0 from row 56 (0x2c3cf..0x2c43e) | 0x2c440 ([0x29a58]) | order [0x12b75] == 1 |
| 0x2e779 | 0x2e9ca | 0x2eada ([0x2cae0]) | 0x2eadf | after the wait, from order 4 (0x2eaed) | 0x2eb46 ([0x2cae0]) | order == 7 |
| 0x3554b | 0x35590 | 0x356e3 ([0x2ff9c]) | 0x3573a | computed before (0x356e8), written after (0x35748), order 10 from row 16 | 0x35778 ([0x2ff9c]) | order == 11 |
| 0x367e5 | 0x36943 | 0x36a61 ([0x35d94]) | 0x36a66 | after the wait, in order 15 (0x36a74) | 0x36ab9 ([0x35d94]) | fade [0x35d3c] == 0 |
| 0x38520 | 0x3872e | 0x388ff ([0x371b4]) | 0x38904 | after the wait, every frame (0x38912) | 0x3894a ([0x371b4]) | order == 19 |
| 0x3c8da | 0x3c94b | 0x3ca6b ([0x38e00]); then 0x3c128 | 0x3ca75 | buffer 0x3be28 filled by 0x3c128 before the wait, written after (0x3ca83) | 0x3ca98 ([0x38e00]) | order == 23 |
| 0x5334f | 0x53391 | 0x5348e ([0x4dec1]) | 0x53517 | computed and written **before** the wait (0x53493..0x53515) | 0x53525 ([0x4dec1]) | [0x4db54] == 0 |
| 0x4d370 | 0x4d389 | (own inline printer, section 7) | 0x4d454 | computed before, written after (0x4d462) | 0x4d57f ([0x4d368]) | before the copy: fade [0x3ceac] == 0, volume [0x12b8c] == 0 |

0x5334f writes its DAC while the previous frame is still being scanned out (a palette change part-way down the
screen on a real monitor); in the capture the DAC is recorded with the image copied in the same refresh, which is
the pairing the remake should use.

### The fake music clock (no GUS)

Every callback starts with the same code (part 0x38985 uses byte instead of word operations on the row; the same
effect). Each has its own counter byte inside its code, skipped by a `jmp` (0x2c48e, 0x2eb94, 0x357c6, 0x36b07,
0x38998, 0x3cae6, 0x5356d, 0x4d657; all 0 in the file):

```c
if (no_gus) {                                  // [0x142d6] == 1
    if (++counter == 4) {                      // every 4th callback of the part
        counter = 0;
        row_word++;                            // [0x12b73] (the player's byte [0x12b72] is not touched)
        if ((order != 0 && row_word == 48) || row_word == 64) { row_word = 0; order++; }   // [0x12b75]
    }
}
```

So without a GUS the position runs at 49.85 / 4 = 12.46 rows per second (the music runs at 8.40 rows per second
from 0:40, 4.20 before), each part's first row step comes at its 4th callback, and nothing waits for a late or lost
tick. The remake follows the GUS case.

## 4. The clocks

### Refreshes, callbacks and records

- Program time 0 = the start of refresh 0; refresh k starts at k x T (T = 20.059583 ms); its retrace (and the
  callback) at k x T + 16.112 ms. (In the capture the 50 Hz mode is set within refresh 0.)
- **Record k of `full.frames` is video memory and the DAC at the end of refresh k**, i.e. at (k + 1) x T: the image
  copied in refresh k's retrace (line 504..560: the copy follows the retrace wait), which the monitor scans out
  during refresh k + 1. It is not the buffer copied in refresh k - 1 (event logs: `copy` at refresh k line 504, then
  `rec k`). The time column of `full.ticks` (k x T) is the start of refresh k; its other columns (position, ticks,
  IRQ counts) are read at the end of refresh k.
- Part p's callbacks: callback c (c = 1, 2, ...) runs in refresh F_p + c - 1 at line 502, exactly one per refresh
  until the part ends.
- **Record k shows the frame drawn with n = k - F_p callbacks done** (record F_p shows n = 0: the first frame is
  drawn before the first callback). The draw of record k starts right after the copy in refresh k - 1, so the music
  position a draw reads is the position after line ~504 of refresh k - 1 (plus the drawing time).
- **The palette of record k** is computed from n callbacks where the part computes it before the retrace wait
  (0x3554b, 0x3c8da, 0x5334f, 0x4d370) and from **n + 1** callbacks where it computes it after the wait (0x2c26f,
  0x2e779, 0x367e5, 0x38520): the callback of refresh k has already run. Checked on the title fade: all 48 records
  with the fade under way match n + 1, and n only where both give the finished fade (`title_palette_check.py`); the
  end part (n) is reproduced exactly.
- `models/core/record_clock.txt` gives, for every record 0..9108, the part and the callback count n of the draw it
  shows.

| part | F_p (first callback) | records of the part | records before F_p (set-up and calibration) |
|---|---|---|---|
| 0x38469 | – | 0..2 | black |
| 0x2c26f | 5 | 5..620 | 0..4 black |
| 0x2e779 | 624 | 624..2330 | 621..623 black (its set-up clears video memory; the DAC already has its palette) |
| 0x3554b | 2334 | 2334..3470 | 2331..2333: part 2's last frame (copied at 2331) in part 3's palette |
| 0x367e5 | 3474 | 3474..4692 | 3471..3473: part 3's last frame in part 4's palette |
| 0x38520 | 4696 | 4696..5751 | 4693..4695 black (cleared; DAC still all 0 from part 4's fade) |
| 0x3c8da | 5755 | 5755..6892 | 5752..5754: part 5's last frame (DAC all 0: black) |
| 0x5334f | 6896 | 6896..8095 | 6893..6895 black (cleared; DAC set to part 7's palette at 6893, all 0 at 6895) |
| 0x4d370 | 8099 | 8099..9108 | 8096..8098: part 7's last frame with its last DAC |

The program sets mode 3 in refresh 9109 (182.739 s) and exits; `full.frames` has records 0..9108.

### Part changes

The main loop tests its exit condition right after the copy, i.e. at line ~504 of a refresh X. Then main runs the
next part's set-up (in the capture it ends at line 504..530 of the same refresh X), and its `timer_measure` waits
for the retrace of X + 1, measures to the retrace of X + 2, loads the count at the end of that retrace, and the first
IRQ comes just before the retrace of X + 3. So **F = X + 3** for every part change, and the new part's first frame
(n = 0) is drawn in refresh X + 2 from line 504 and is record X + 3. (General rule: a measure that starts before
line 502 of refresh r gives F = r + 2, one that starts in or after the retrace F = r + 3; the title's starts in
refresh 3, line 106: F = 5. A slow machine whose set-up takes longer moves F by whole refreshes.)

| part | event | its tick, time, refresh:line | exit refresh X | F | first callback minus the row's tick |
|---|---|---|---|---|---|
| 0x2c26f | song row 0:00 (first tick of the player) | 4, 0.096417 s, 4:504 | (start) | 5 | 19.99 ms = 1.00 refresh |
| 0x2e779 | 1:00 | 628, 12.459520 s, 621:78 | 621 | 624 | 73.8 ms = 3.68 refreshes |
| 0x3554b | 7:00 | 2356, 46.762880 s, 2331:124 | 2331 | 2334 | 72.3 ms = 3.60 |
| 0x367e5 | 11:00 | 3508, 69.638400 s, 3471:361 | 3471 | 3474 | 64.7 ms = 3.23 |
| 0x38520 | part 4's fade [0x35d3c] reaches 0 (15:13 began at tick 4738, 94.061 s, 4689:64) | | 4693 | 4696 | 154.5 ms = 7.70 after 15:13 |
| 0x3c8da | 19:00 | 5812, 115.389440 s, 5752:209 | 5752 | 5755 | 69.6 ms = 3.47 |
| 0x5334f | 23:00 | 6964, 138.264960 s, 6892:446 | **6893** (see drops: the frame of 6892 was dropped) | 6896 | 82.0 ms = 4.09 |
| 0x4d370 | part 7's fade [0x4db54] reaches 0 (27:10 began at tick 8176, 162.331 s, 8092:272) | | 8096 | 8099 | 147.8 ms = 7.37 after 27:10 |
| exit | the end part's fade reaches 0 (30:32 at tick 9172, 182.111 s, 9078:326) | | 9109 | – | mode 3 at 182.739 s, music at 30:37 |

So a part whose exit waits for an order normally leaves in the refresh of the order's first tick (if the tick comes
before line ~502), and the next part's first callback is the third retrace after that.

### Music ticks and rows

- GUS timer 2 runs at 62 x 320 µs = **19.84 ms per tick** on its own grid, which in the capture starts at program
  time 0 (expiry j at j x 19.84 ms). The player's tick count i (`mtick`, the timer IRQs served) is served at
  **t_i = (i + L_i) x 19.84 ms**, L_i = ticks lost so far.
- **At every part start from 0x2e779 on, one tick is lost**: `timer_measure` keeps interrupts off from line ~504 of
  X to line 504 of X + 2 (about 40 ms), two timer expiries fall into that window, the GUS latches only one IRQ, and it
  is served late at the `sti` (refresh F - 1, line 504). The title's measure (refresh 3 line 106 to refresh 4
  line 504, 33 ms) delays one tick and loses none.

  | part start | late tick (served at F-1:504) | L from the next tick on |
  |---|---|---|
  | 0x2c26f | 4 (song row 0:00; grid 79.36 ms, served 96.417 ms) | 0 |
  | 0x2e779 | 629 (12.513 s) | 1 |
  | 0x3554b | 2357 (46.815 s) | 2 |
  | 0x367e5 | 3509 (69.683 s) | 3 |
  | 0x38520 | 4743 (94.196 s) | 4 |
  | 0x3c8da | 5813 (115.439 s) | 5 |
  | 0x5334f | 6966 (138.327 s) | 6 |
  | 0x4d370 | 8181 (162.459 s) | 7 |

  On real hardware the window is 20..40 ms depending on where in the refresh the measure starts, so a part start
  loses 0 or 1 tick (2 only in a rare phase); in the capture it is always 1. Besides that, a tick that comes while
  the IRQ 0 handler polls for the retrace (interrupts off) is served up to 0.16 ms late, about every 90 ticks (the
  beat of 19.84 ms against 20.06 ms).
- The song in ticks: row 0:00 at tick 4; **speed 12 for rows 0:00..0:39, speed 6 from 0:40**; order 0 has 64 rows,
  orders 1..30 have 48. Start tick of row (o, r): 4 + 12 r (o = 0, r <= 40); 484 + 6 (r - 40) (o = 0, r >= 40);
  628 + 288 (o - 1) + 6 r (o >= 1). Its time is t_i above. `models/core/rows.txt` lists all 1,494 rows (tick, time,
  refresh, line); they agree with every record of `full.ticks`.
- In refreshes: a row of speed 6 lasts 119.04 ms = 5.934 refreshes, 16 rows = 94.95 refreshes, an order of 48 rows
  284.85 refreshes; the music runs 0.8 % faster than 20 ms ticks would.

### Dropped frames in the capture

151 records repeat the previous record although the part's callbacks continue: a row-start tick (new notes) that is
served within about 4 ms (up to 130 lines) before the retrace keeps the CPU in the GUS interrupt handler (it waits
inside the interrupt for the volume-ramp IRQ) across the 2-line retrace, the main loop misses it and copies one
refresh later. If the draw started in refresh a is copied in a + 2: record a + 1 repeats record a, record a + 2 shows
n = a + 1 - F (one less than the rule), from a + 3 the rule holds again, and the state n = a + 2 - F is never shown.
They come every ~90 ticks (1.8 s, the beat of tick and refresh) where the music has a new row at that moment: 30 in
0x2e779, 25 in 0x3554b, 29 in 0x367e5, 9 in 0x38520, 22 in 0x3c8da, 28 in 0x5334f, 8 in 0x4d370, none in the title.
One of them moved a part change (0x3c8da's last frame, 23:00 at 6892:446: exit at 6893 instead of 6892).
`record_clock.txt` marks them (`repeat`, `late`); `clock.py --table` lists them with their tick. They are a
frame-rate effect of the original (a slower CPU drops more frames); **the remake draws every refresh** and uses
n = k - F_p.

## 5. Text: the strings and when they are shown

All text is lowercase a..z and spaces in the font of section 6. `\r` starts a line, `@` ends a page.

Shared printer pages (0x4c8e8.., shown on row 162, centred), in the music (section 6 says how):

| page address | text | rows | records | part |
|---|---|---|---|---|
| 0x4c952 | "hmmm" | 17:00..17:15 | 5184..5278 | 0x38520 |
| 0x4c958 | "moving lightsource and shadow" | 17:16..17:31 | 5279..5373 | 0x38520 |
| 0x4c977 | "moving lightsource and shadow" | 17:32..17:47 | 5374..5468 | 0x38520 |
| 0x4c996 | "woww" | 18:00..18:15 | 5469..5563 | 0x38520 |
| 0x4c9b2 | "the sea robot of love" | 22:00..22:15 | 6609..6703 | 0x3c8da |
| 0x4c9d3 | "float robot float" | 24:00..24:15 | 7180..7274 | 0x5334f |
| 0x4c9e8 | "if thy degrade yourself" | 24:32..24:47 | 7370..7464 | 0x5334f |
| 0x4ca01 | "thy shall be upgraded" | 25:00..25:15 | 7465..7559 | 0x5334f |
| 0x4ca1c, 0x4ca37, 0x4ca52 | "thy shall not make whores" (three pages) | 26:00..26:47 | 7750..8034 | 0x5334f |
| 0x4ca8d | "take hash man" (ends with NUL, not `@`) | never reached | – | – |

All other pages are empty (`"\r@"`). The end part's pages (section 7): "forever loving robot" / "its the" / "human
robot of love"; "hoplite" / "dune" / "der piipo" / "the robots in their teens"; "nooon" / "zuul" / "live" /
"halcyon" / "complex"; "melon" / "polka brothers" / "are of this great love". (PARTS.md's "forever lowing robot"
is a typo: the string is "loving".)

Crude: "thy shall not make whores" (on screen for 5.7 s); "take hash man" (a drug reference) is in the data but
never shown.

## 6. The shared text printer 0x4cacc and the font

### The font (static, hand-made pixel art)

| what | where | format |
|---|---|---|
| font sheet | 0x3cec2, 320 x 38 bytes (12,160) | 8-bit indices, 320 bytes per row; the 26 glyphs a..z side by side in columns 0..284, columns 285..319 empty; rows 0 and 37 empty in every glyph |
| widths | 0x4c8c2, 26 bytes | a..z: 13 12 12 12 12 9 12 11 4 6 11 4 17 11 13 12 12 8 11 8 11 12 17 12 12 11 |
| "character map" 0x4c861 | = the widths seen from the character code: [c + 0x4c861] = width[c - 'a'] (0x4c861 + 0x61 = 0x4c8c2) | 0 for '@' and other codes below 'a' (zeros); digits would be mapped to 0x7b..0x84 and read the printer's variables at 0x4c8dc.. (never used) |

Glyph k starts at column sum(width[0..k-1]) (a 0, b 13, c 25, d 37, e 49, f 61, g 70, h 82, i 93, j 97, k 103,
l 114, m 118, n 135, o 146, p 159, q 171, r 183, s 191, t 202, u 210, v 221, w 233, x 250, y 262, z 274) and is
width[k] columns wide; the code computes it as k + sum(width[j] - 1). Every glyph cell is 38 rows high; ascenders
start at row 1, the baseline is the bottom of row 28, descenders reach row 36; x-height about 20 rows.

Indices: **0 = transparent** (not written), **216 = the letter body** (2-pixel strokes), **254 = single edge pixels
at the steps of curves and diagonals** (anti-aliasing), **7 = a 1-pixel outline** around the whole letter. Glyphs
touch outline to outline (the width includes the outline; no extra gap; [0x3ceb0], added after every glyph and
space, is 0 and never written). Space = 4 pixels.

The letters are hand-made pixel art, credited to der piipo ("font and graphics"): a condensed Helvetica-like
lowercase with consistently placed single anti-aliasing pixels and a hand-closed outline (whether he drew them from
scratch or retouched a rendered typeface cannot be told from the pixels). `models/core/font_sheet.png` (x4, through
the end part's palette), `font_sheet_indexed.png` (1:1, the stored indices), `font_sheet_false.png` (outline red,
body green, edge pixels white) and `font_glyphs.png` (the glyphs cut out as the printer cuts them) show it. For the
remake it counts as a hand-drawn picture (a redraw of the 285 x 38 sheet, glyph boxes scaled), but it must keep the
three layers apart: the outline takes the current part's colour 7 (below).

Colours on screen: every part's palette has **216 = (63, 63, 63) white** and **254 = (33, 33, 33) grey**; **7 is the
part's own dark colour**: (3, 7, 9) dark blue in 0x38520, (5, 0, 6) in 0x3c8da, (2, 2, 3) in 0x5334f, (16, 14, 16)
in the end part (where 7 is also noise level 7). The captions fade with the part's palette (e.g. part 0x38520's
fade: 216 = 54, 254 = 28 at record 5500).

### The printer 0x4cacc

```c
u8  *page        = 0x4c8e8;   // [0x4c8e4] the current page
u8  *buf;                     // [0x4cac4] set by the caller to its draw buffer
u32  txt_off;                 // [0x4c8dc] write offset y * 320 + x
u32  lines;                   // [0x4c8e0] newline counter, never read
u32  spacing     = 0;         // [0x3ceb0]
u8   last_order  = 3;         // [0x3ceb8] (file value: the first call always counts as a new order)
u32  last_rowbit = 0x22;      // [0x3cebe]

static int adv(u8 c) { return c == ' ' ? 4 : *(u8 *)(0x4c861 + c); }   // newline measurement ('@' gives 0)

void print_page(void)         // 0x4cacc, called once per drawn frame by parts 1..7
{
    u8 *start = page;
    for (;;) {
        u8 c = *page;
        if (c == '@') { page++; break; }
        if (c == '\r') {                                       // 0x4cc1d: a new line, centred, on row 162
            page++;
            int w = 0;
            for (u8 *q = page; *q != '\r'; q++) w += adv(*q);  // to the next CR, through the '@'
            lines++;
            txt_off = 0xa0 - (w >> 1) + 0xca80;                // x = 160 - floor(w / 2), y = 162 (0xca80 = 162 * 320)
            continue;
        }
        if (c == 0) continue;                                  // would hang (never reached)
        if (c == ' ') { txt_off += 4 + spacing; page++; continue; }
        if (c >= '0' && c <= '9') c += 0x7b - '0';             // digits: never used
        int k = c - 'a', w = width[k];
        u8 *src = font + sum(width[0..k-1]), *dst = buf + txt_off;
        for (int y = 0; y < 38; y++, src += 320, dst += 320)
            for (int x = 0; x < w; x++) if (src[x]) dst[x] = src[x];   // index 0 transparent, others copied
        txt_off += adv(c) + spacing;
        page++;
    }
    // '@': next page when the order or bit 4 of the row changed since the last check
    u32 bit = row_word & 0x10;                                 // [0x12b73]
    if (order != last_order) { last_order = order; last_rowbit = bit; return; }   // keep the advanced pointer
    if (bit != last_rowbit)  { last_rowbit = bit; return; }
    page = start;                                              // the same page again next frame
}
```

The text is drawn into the buffer over everything the part drew before (only 0x3c8da draws more afterwards, 0x3c128;
its caption is intact in the capture), on rows 162..199: the cell fits the screen bottom exactly. The longest caption
("moving lightsource and shadow", 300 pixels) spans x = 10..309.

**When**: one page per 16 rows: the pointer advances at the first call that sees a new order or a new bit 4 of the
row, i.e. at rows 0, 16, 32 (and 48 in order 0). Page number for row (o, r): 1 + r / 16 in order 0 (page 0 is
printed once, by the very first call), 5 + 3 (o - 1) + r / 16 for o >= 1, which puts the 99 pages of 0x4c8e8.. on
the music as in section 5. The call that sees the new position still prints the old page (it advances after
printing), so **a caption appears in the second frame drawn after its row's tick and disappears likewise**: in the
capture, record B + 2 where B is the refresh in which the row's tick comes (before the copy at line 504); every
caption stays 95 records (16 rows = 94.95 refreshes). The page counter only advances while parts 1..7 call the
printer; the end part does not, so pages 83.. (order 27 on) are never printed.

For the remake: the caption shown in a frame is the page of the music position at the previous drawn frame
(one-frame delay), on row 162 of the 320 x 200 frame, centred on x = 160 - floor(w / 2) with w = the sum of the
advances; draw the three layers with the part's DAC entries 7, 216, 254 (as they change with its fades).

## 7. The last part 0x4d370: the end texts

What it draws (records 8099..9108, 27:11..30:37): grey noise over the whole screen, new every frame, with white
centred text pages (credits and greetings) at 28-row line spacing, each page shown while rows 0..32 of an order play
and blank for rows 33..47; from 30:32 the palette fades to black in 32 refreshes and the program exits.

```c
u32 x       = 0x5c17;         // [0x4d36c] noise state (file value; not touched by other parts)
u8 *page_ptr = 0x4cca3;       // [0x4d364] page for the NEXT frame
u8 *text;                     // [0x4cc9c] page of this frame
u32 line_y;                   // [0x4cc98] offset of the next line (0, 0x2300 = 28 rows, ...)
u32 fade    = 32;             // [0x3ceac] (file value; only this part uses it)
u32 volume;                   // [0x12b8c] the player's master volume, 63 (the GUS driver scales channel volumes by it / 64)

void end_part(void)           // 0x4d370
{
    timer_install(); timer_measure();          // 0x3cd7c, 0x3cdf7: callback 0x4d645
    u8 *buf = getmem_high(64000);              // [0x4d368]
    for (;;) {                                 // 0x4d389
        text = page_ptr;                       // the page chosen by the previous frame
        for (int i = 0; i < 16000; i++) {      // noise: 4 pixels per step
            ((u32 *)buf)[i] = x & 0x1f1f1f1f;
            x = rol32(x + 0x9d, 11) ^ 0x9d;
        }
        if (order == 28) page_ptr = 0x4ccd9;   // positions read here, after the noise (0x4d3c8)
        if (order == 29) page_ptr = 0x4cd0f;
        if (order == 30) page_ptr = 0x4cd33;
        if ((s16)row_word > 32) page_ptr = 0x4cca0;   // "\r@": blank page
        line_y = 0;
        for (int i = 0; i < 768; i++) dac[i] = (pal_end[i] * fade) >> 5;   // pal_end = 0x4cd64, 768 bytes, 6-bit
        while (!(inb(0x3da) & 8)) ;  while (inb(0x3da) & 8) ;
        write_dac(0, dac, 768);
        // the printer of section 6, inline (0x4d481..0x4d63f), with one difference:
        // '\r' sets txt_off = 0xa0 - (w >> 1) + line_y; line_y += 0x2300  (lines at y = 0, 28, 56, ...)
        draw_page(buf, text);                  // up to the '@'
        if (fade == 0 || volume == 0 || inb(0x60) == 1) break;   // checked before the copy
        copy buf to A0000;
    }
    timer_restore();                           // 0x3cdd0
}

void end_callback(void)       // 0x4d645, per refresh at the retrace
{
    fake_clock();                              // section 3 (no GUS only; counter byte 0x4d657)
    if ((s16)row_word >= 32 && order == 30) {
        if (fade != 0) { fade--; if ((s32)volume > 0) volume--; }
    } else if (fade != 32) fade++;             // never: fade is 32 until 30:32
}
```

The pages (each line centred on x = 160 - floor(w / 2); line j at y = 28 j; cells 38 rows high, so a line's
descenders and the next line's ascenders share rows; later lines are drawn over earlier ones):

| page | y = 0 | 28 | 56 | 84 | 112 | 140 | chosen by | records |
|---|---|---|---|---|---|---|---|---|
| 0x4cca3 | – | forever loving robot | – | its the | human robot of love | – | initial value | 8099..8230 |
| 0x4ccd9 | – | hoplite | dune | der piipo | – | the robots in their teens | order 28, row <= 32 | 8321..8516 |
| 0x4cd0f | – | nooon | zuul | live | halcyon | complex | order 29, row <= 32 | 8606..8800 |
| 0x4cd33 | – | – | melon | polka brothers | – | are of this great love | order 30, row <= 32 | 8890..9085 |
| 0x4cca0 | (blank) | | | | | | row > 32 (any order) | 8231..8320, 8517..8605, 8801..8889, 9086..9108 |

The page a frame shows is the one chosen by the previous frame's draw (from the position read after its noise
fill), so pages change one drawn frame after the position: e.g. 28:00 comes at refresh 8318 line 580, after that
refresh's page select; the select of 8319 sees it, the draw of 8320 prints it, record 8321 shows it.

The noise: indices 0..31, uniform (each within 0.7 % of 1/32 over 50 frames), independent per pixel (horizontal
neighbour correlation -0.03, vertical 0.00), a new field of 64,000 values for every drawn frame (16,000 generator
steps per frame, so it is frame-rate dependent); the state continues from frame to frame and does not repeat within
the part. The palette 0x4cd64 maps 0..31 to a grey ramp from (0, 0, 0) through (16, 14, 16) at 7 to (45, 45, 47) at
31 (slightly violet in the middle); 216 = (63, 63, 63), 254 = (33, 33, 33).

The fade: from the first callback at 30:32 (refresh 9078; tick 9172 at 182.111 s) fade drops by 1 per callback,
reaching 0 at the callback of refresh 9109; the DAC of record k is pal_end x fade(n) / 32 with n = k - 8099 (the
palette is computed before the wait). At the same time the master volume [0x12b8c] drops by 1 per callback, 63 to 31
(whether it is audible depends on the player's volume writes: notes-music.md). The loop leaves before copying when it
sees fade = 0 after the retrace of 9109: the screen keeps the last image with the DAC at fade 1/32, then main stops
the music (at 30:37, half volume) and sets text mode at 182.739 s.

Verified: `endpart.py` renders this from the file (font, widths, strings, palette, seed) plus the clock of the event
log (callback count per draw, position at each page select, position at each callback) and reproduces every record
8099..9108 exactly, indices and DAC.

## 8. Shortcuts for the remake to evaluate exactly

- Font: hand-drawn 38-row pixel letters with three indices; redraw at high resolution (glyph boxes scaled, advances
  scaled) and keep the outline / body / edge layers so the outline takes the part's colour 7; `--original-art` shows
  the 285 x 38 sheet. Positions are integer pixels of the 320 x 200 frame (x = 160 - floor(w / 2)); keep them.
- End-part noise: per-pixel white noise of 32 grey levels, new every frame: draw noise of the same statistics at the
  output resolution (uniform level 0..31 on the ramp of 0x4cd64), not upscaled 320 x 200 noise; one new field per
  refresh (the original draws one per frame; in the capture every refresh).
- Fades: (pal x fade) >> 5 with fade 0..32 per refresh: evaluate pal / 63 x fade / 32 continuously, interpolated by
  the fraction of a refresh (the fade is stepped per callback, from 30:32).
- Timing: the remake should not reproduce the dropped frames, nor the 3-refresh gaps unless they are wanted for
  fidelity (they show the previous part's last frame in the next palette for 60 ms, or black).

## 9. Open points

- The real-mode stub (0x14cf4) never runs in the capture; on a system where IRQ 0 arrives in V86/real mode it would
  skip that refresh's callback (a guess at the consequence; not observed).
- Real hardware: the set-up time of each part (and of 0x38469 before the title) depends on the CPU; it moves F by
  whole refreshes and decides whether a part start loses a music tick. The values above are the capture's (100 M
  instructions per second, set-ups within 1 ms).
- How long the GUS handler waits for its ramp IRQ (which decides the dropped frames) comes from the emulator's GUS
  model; real cards may differ (notes-music.md).
