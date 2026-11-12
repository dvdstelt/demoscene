# Scope "core": start-up, CDPI, the parts' framework, p1, the hand-overs, the RNC check, the pictures

Addresses: `SEG:OFF` in `unpacked/<name>.bin` (file offset = SEG*16 + OFF; part code in segment 0010 = file offset
0x100), CDPI offsets in the `cdpi` view (segment 00EB of `loader.bin`), code32 offsets in the `loader32` view.
Frames, times and music positions are from the capture (`full.frames` / `full.ticks`, 100 M instructions/s); a
frame record is taken at the END of its frame (VRAM, DAC and the start address latched at that frame's retrace), the
time column is the frame START. "Retrace" = one run of a part's int 8 handler (70.0863 Hz = 25.175 MHz / (800 x
449)). The scripts named in brackets are in `models/core/` (see its README.md).

Checked against the capture and the original files:

- p1 (`p1_model.py`): a model written from the code below reproduces **every** p1 frame of the capture (2690
  frames with the handler running, 15..2704) pixel for pixel, and its DAC 0..63, from the handler count alone.
- RNC-1 (`rnc_from_notes.py`): an unpacker written only from NOTES.md (chain walk, header, CRC, bit reader, tables)
  gives all nine `unpacked/*.bin` byte for byte, both CRCs check. NOTES.md's description is complete (one formula
  to add, section 7).
- exit fades (`exit_fades.py`): every DAC of p6's and p7's final fades equals `(v * L) >> 8` of the palette at the
  start of the fade, L = 256 - 7 n (p6) and 256 - n (p7); the capture runs about 4 steps per frame, a real VGA one
  (section 5.2).
- framework (`framework_scan.py`, `retrace_waits.py`, `runs/first_irq.sh`): the per-part addresses in section 3
  were found by pattern and read one by one; the frame of each part's first handler run was traced from the part
  snapshots.

---------------------------------------------------------------------------------------------------------------

## 1. Start-up (the loader's main, code32 0xa55)

```c
main_a55() {
    [0] = 0x63f << 4;                         // low heap pointer (NOTES.md: a linear address used as an offset)
    print("Initializing music replay...");    // 0xab8, text mode, code32 0xa10
    gus_init();                               // [0x1a7f] = 0x45b4; without ULTRASND an interactive menu (0x498b)
    load_module("dope.mod");                  // 0x2d15: file, patterns to extended memory, samples to GUS DRAM
    rm_int(0x10, AX = 0x13);                  // int 33h AL=10h: mode 13h, black (capture frame 7)
    player_start();                           // 0x2c17: the first GUS timer tick comes at t = 0.1104 s
    rm_far_call(CDPI_entry);                  // int 32h to load:0EA0 = call far 00EB:0131: ALL parts run in here
    player_stop();                            // 0x2cff: the music stops (after TERM)
    pmode_exit();                             // 0x355
}
```

On screen: text mode with "Initializing music replay..." (capture frames 0..6; on a real PC as long as the module
load and the sample upload take: seconds), then mode 13h with the default palette and a cleared screen (black) from
frame 7. The music starts here and is never synchronised with anything after it.

## 2. CDPI v0.2 (00EB:0131..042D)

Data (CDPI offsets): `[0000]` PSP, `[0002]` saved SS:SP, `[0006]` file offset of the executable to load next,
`[000A]` the one after it, `[000E]` 0x1C-byte MZ header buffer (so `[0x10]` last-page bytes, `[0x12]` pages,
`[0x14]` relocation count, `[0x16]` header paragraphs, `[0x18]` minalloc, `[0x1C]` SS, `[0x1E]` SP, `[0x22]` IP,
`[0x24]` CS, `[0x26]` relocation table offset), `[002A]` one relocation entry, `[002E]` the child's DOS block,
`[0030]` its exit code, `[042E]` copy of the whole IVT (1 KB; `[04B2]` = the original int 21h vector), `[082E]` the
program's own path.

```c
cdpi_0131() {                                   // called far from the loader (protected mode) through int 32h
    psp = DS;  find_own_path_0185();            // the string after the environment's 00 00 01 00 -> [082E]
    memcpy(cs:042E, 0000:0000, 1024);           // the IVT
    dos_resize(psp, 0x753);                     // int 21h 4Ah
    first_exe_01ab();                           // open self, read the MZ header: [0006] = (pages-1)*512 + last
    sti;
    run_chain_020e();                           // returns after a child exits with code 0xFF
    int10(3);  print("\r\nCDPI v0.2, Disconnecting life support systems.\r\n");
    cli; memcpy(0000:0000, cs:042E, 1024); sti; // every vector back, including any int 8 a part left hooked
    retf;
}
run_chain_020e() {                              // also re-entered by a jump from 03F4 (stack balanced)
    f = open(own path); seek(f, [0006]); read(f, hdr, 0x1C); if (hdr.magic != 'MZ') -> "Invalid executable!";
    size = (hdr.pages - 1) * 512 + hdr.last;   // the executable's MZ size (never a 0 last-page count here)
    [000A] = [0006] + size;
    image = size - hdr.hdrpara * 16;           // the load module (for a packed part: RNC data + stub)
    need = (image >> 4) + hdr.minalloc + 0x11;
    bx = dos_alloc(0xFFFF) -> largest free block; if (bx < need) -> "Insufficient memory.";
    blk = dos_alloc(bx);  [002E] = blk;        // takes all of it
    dos_create_psp(blk);                        // int 21h 26h, DX = blk
    load = blk + 0x10;
    seek(f, [0006] + hdr.hdrpara * 16); read image to load:0000 in 32 KB pieces (DS += 0x800);
    seek(f, [0006] + hdr.reloff);
    for (i = 0; i < hdr.nreloc; i++) { read(f, [002A], 4); word(load + seg : off) += load; }  // packed parts: 0
    close(f);
    ivt[0x21] = 00EB:03BD;
    push ds; push es; [0002] = SS:SP;
    DS = ES = blk;  SS:SP = load + hdr.ss : hdr.sp;  retf to load + hdr.cs : hdr.ip;   // 03BC
}
int21_03bd() { if (AH != 0x4C) jmp far [04B2];          // everything else to the original int 21h
    [0030] = AL;  SS:SP = [0002];  ivt[0x21] = [04B2];  pop es; pop ds;
    dos_free([002E]);  [0006] = [000A];
    if ([0030] != 0xFF) goto run_chain_020e;   else return;      // back to 015F
}
```

Errors (text mode + message, back to the loader): "Unable to read file." (03C7), "Invalid executable!" (03D7),
"Insufficient memory." (03E4). 0123 / 0128 ("Passed.") and 017D ("Failed to find required equipment, unable to
run.") are not reached.

What CDPI means for the screen: nothing. It never touches the video mode, the palette, VGA memory, the PIT or the
GUS; while it reads and unpacks the next part, the screen keeps the last frame of the previous part and the music
plays on. In the capture that is 3..4 frames per boundary (disk and unpacking at 100 M instructions/s); on a real
PC it is the disk time plus the RNC stub's time (up to 180 KB packed, 485 KB unpacked).

### 2.1 Keys and exit codes (correction to NOTES.md)

| part | key test | on a key | exit code |
|---|---|---|---|
| p1 | port 60h after every main-loop pass: `(in(0x60) & 0x7F) == 1` (ESC pressed **or released**) | 0d29: restore timer, exit | 0xFF |
| p2 | int 16h AH=1 once per drawn frame (371e) | `[1930]` = 0xFF, jump to the exit 43ed (DAC white) | 0xFF |
| p3 | int 16h AH=1 per drawn frame (2ba4) | jump to the exit 2f57 | **0** (2c12 `mov ax, 4C00h`) |
| p4 | port 60h ESC test in every wait loop (1d1c...) | 1e9b: restore timer, **text mode**, exit | 0xFF |
| p5 | int 16h AH=1 per drawn frame (2b66) | `[2bad]` = 0xFF, exit 3747 | 0xFF |
| p6 | int 16h AH=1 per drawn frame (2de6) | `[3907]` = 0xFF, exit 3d22 (fade to black) | 0xFF |
| p7 | int 16h AH=1 per drawn frame (3070) | exit 33be (fade, text mode) | **0** (31ba) |

int 16h AH=1 only looks at the key buffer; nobody reads the key. So a key pressed in p3 ends p3, p4 then runs in
full (unless the key was ESC: port 60h still holds its scan code and p4 quits at once), and p5 quits at its first
frame; a key in p7 still plays p7's 257-retrace fade, then TERM ends the chain. A non-ESC key in p1 does not stop
p1 but ends the demo at p2's first frame. For the remake: ESC or any key quits.

## 3. The framework every part repeats

### 3.1 Addresses (segment 0010 of each part)

| | p1 | p2 | p3 | p4 | p5 | p6 | p7 |
|---|---|---|---|---|---|---|---|
| wait for retrace start (bit 3 = 0, then 1) | 0010 | 1a11 | 1a33 | 1410 | 15c7 | 17de | 1a37 |
| PIT ch. 0 mode 3 count 0, between two waits | 00d6 | 1abf | 1ae6 | 1486 | 1637 | 1894 | 1aea |
| install + Cirrus probe + calibration | 0076 | 1a5f | 1a86 | 1469 | 161a | 1834 | 1a8a |
| calibration code | 00eb | 1ad0 | 1af7 | 1497 | 1648 | 18a5 | 1afb |
| factor (count = 2P * f / 2048) | 990 | 1000 | 990 (repeated) | 1000 | 1000 | 1000 | 990 |
| Cirrus probe | yes, CR1F = 27h | 27h | 27h | no | no | 25h | 27h |
| int 8 handler | 0020 | 1a21 | 1a43 | 1420 | 15d7 | 17ee | 1a47 |
| registers it saves | all | ax, dx | ax, dx | all | ax, dx | ax, dx | ax, dx |
| retrace wait in the handler | no limit | no limit | 1000 reads | no limit | 1000 reads | 5000 reads | 1000 reads |
| per-retrace routine | 0b8b, 0ba3, 0b24 (direct) | `[1a09]` = 2580 | `[1a2b]` = 1f60 | 1a20 (direct) | `[15bf]` = 1a70 | `[17d6]` = 1cc0, then 3895 | `[1a2f]` = 1f40 |
| old int 8 / count / accumulator | [0] / [4] / [6] | [1a01] / [1a05] / [1a07] | [1a23] / [1a27] / [1a29] | [1400] / [1404] / [1406] | [15b7] / [15bb] / [15bd] | [17ce] / [17d2] / [17d4] | [1a27] / [1a2b] / [1a2d] |
| handler run counter (unused) | [1e] | [1a1f] | [1a41] | [141e] | [15d5] | [17ec] | [1a45] |
| restore (Cirrus, int 8, PIT) | 0132 | 1b17 | 1b63 | 14da | 168f | 18ec | 1b3e |
| "run N retraces" loop | (main loop 0ce3) | 371e | 2ba4 | (own waits) | 2b66 | 2de6 | 3070 |
| N / retrace counter / ready flag / drawn frames | 0x1500 on [16a] | [371c] / [1928] / [1924] / [192c] | [2ba2] / [199a] / [1996] / [199e] | [1969] (word) | [2b64] / [1528] / [1524] / [152c] | [2de4] / [1736] / [1732] / [173a] | [306e] / [199a] / [1996] / [199e] |
| frame copy (waits for the ready flag) | — | 360c | 2b1c | — | 2adc | 2d5c | 2f8c |
| exit routine | 0d21 / 0d29 | 43ed | 2f57 | 1e81 / 1e9b | 3747 | 3d22 | 33be |

The handler is installed where the script sets the routine pointer: p2 37df..37e7, p3 2c88..2c96, p5 3152..315a,
p6 39c1..39c9, p7 31db..31e3, p4 1d16, p1 0ce0 (after the first palette tables).

### 3.2 Install and calibration

```c
install_1a5f() {                                // p2; the others are the same up to the table above
    cirrus_probe();                             // 3.6 (not in p4, p5)
    old_int8 = ivt[8];  ivt[8] = cs:handler;    // the vector is set BEFORE the calibration
    cli;
    wait_retrace_start(); pit(0x36, 0);  wait_retrace_start();     // mode 3, count 0 (65536), started at a retrace
    out(0x43, 0); c = in(0x40) | in(0x40) << 8; // latch
    count = (uint16)(-c);                       // mode 3 counts down by 2 per clock: -c = 2P, P = frame in clocks
    count = (count * 1000) >> 11;               // p1, p3, p7: * 990 (eax, 32 bit)
    wait_retrace_start();
    pit(0x30, count);                           // mode 0 = one-shot, started at a retrace start
    acc = 0;  sti;
}
```

P = 1193182 / 70.0863 = 17024.6 clocks, so 2P = 34049, count = 16625 (x1000: 13.93 ms, the IRQ comes 0.33 ms
before the next retrace) or 16459 (x990: 13.79 ms, 0.47 ms before). p3 (1af7) repeats the measurement up to 20
times until two successive values differ by -4..+3 (`(new - old + 4) >> 3 == 0`) and uses the last one. p7 (1b12)
does an `sti` before the multiplication (harmless). p1 (00eb) is p2's code with 990.

### 3.3 The int 8 handler

```c
handler_1a21() {                                // p2
    cli; push ax, dx;  out(0x20, 0x20);  irqs++;
    while (!(in(0x3DA) & 8)) ;                  // wait for the retrace start (p3, p5, p7: at most 1000 reads,
                                                //   p6: 5000; on a time-out the routine just runs early)
    pit(0x30, count);                           // the next one-shot starts now, at the retrace start
    sti;
    call far per_retrace;                       // [1a09]
    acc += count; pop dx, ax;
    if (carry) jmp far old_int8;  else iret;    // the BIOS clock gets one tick per 65536 PIT clocks (18.2 Hz)
}
```

The one-shot is re-armed at every retrace start, so the handler runs exactly once per displayed frame, 0.3-0.5 ms
before the retrace, and waits for it. p1 (0020) and p4 (1420) save all registers; p1 writes the CRTC start address
before the wait and the panning and DAC after it (section 4.5); p6 (17ee) calls 3895 (a two-palette mix, p6's
"material MORPH", notes-p6p7) after the routine.

### 3.4 Per-retrace routine skeleton

Engine parts (p2 2580, p3 1f60, p5 1a70, p6 1cc0, p7 1f40): palette fade / label slide / motion of the objects
(engine and part scopes), then `ready++` and `retraces++` (dwords; p6 also `[1754]++`, p7 `[19b8]++` word), `retf`.
p4 (1a20): `[2113] += 2`, `[1969]++` (word), fades, palette mix through 1975 / 1999. p1: section 4.

### 3.5 "Run N retraces" (p2 371e; p3 2ba4, p5 2b66, p6 2de6, p7 3070 are the same)

```c
run_371e(cx = N) {
    drawn = 0; retraces = 0; Nvar = N;
    do {
        set_up_354a(); transform_3585(); draw_3310();
        copy_360c();               // while (ready == 0) ;  ready = 0;  copy / blend the buffer to A000
        drawn++;
        if (int16(AH=1) has a key) { exitcode = 0xFF; drop the return address; goto exit_43ed; }
    } while (Nvar > retraces);     // tested after the frame
}
```

Time base: the step ends at the first frame finished with `retraces >= N`; retraces counted during that last frame
beyond N are lost because the next call resets the counter, while the motion in the per-retrace routine runs on.
So the script's parameter changes lag the per-retrace motion by up to one drawn frame per step (frame-rate
dependent; the capture's values are one realisation at 100 M instructions/s; per-part details in the part notes).
At most one frame is copied per retrace (the ready flag).

### 3.6 The Cirrus probe (p1 0076; p2 1a5f, p3 1a86, p6 1834, p7 1a8a)

```c
sr6 = SR[6];  SR[6] = 0;
if (SR[6] read back == 0x0F) {                  // Cirrus Logic GD54xx: extensions locked
    SR[6] = 0x12;                               // unlock
    chip = CR[0x27];                            // chip ID
    if (chip >= 0x90) { saved = CR[0x1F]; CR[0x1F] = 0x27; }   // p6: 0x25. dx is 3D5h here: a CRTC register
} else SR[6] = sr6;
// restore (0132 ...): if (chip >= 0x90) { SR[6] = 0x12; SR[0x1F] = saved; SR[6] = 0x1F; }
```

On the GD54xx, SR1F (sequencer) selects the memory clock (value x 14.318 MHz / 8: 0x27 = 69.8 MHz, 0x25 = 66.2
MHz); the probe looks like a memory-clock speed-up for faster VGA writes that writes the CRTC register 1Fh instead
(the restore writes the saved byte to SR1F) — the intent is a guess. It changes nothing on screen; the remake drops
it.

### 3.7 What the remake takes from the framework

Every per-retrace quantity advances once per 1/70.0863 s, counted from the part's first handler run (section 6).
Within a part, "retrace r" means r handler runs after the first (r = 0 is the first). The drawn-frame counters and
the step overshoot are the only CPU-dependent parts.

## 4. p1: the "Complex Media Labs" logo scroller

The chrome logo "Complex" / "Media Labs" (1024 x 195 pixels, 64 shades) slides in from the right and out to the left
at half a pixel per retrace while its palette cross-fades from violet to orange; nothing else is on screen. Capture
frames 7..2704 (0.10..38.58 s, music 00:3f..02:3e); first handler run frame 15; the logo is visible from frame 61
to 2664, the colours change from frame 265 to 2313; exit after 2689 handler runs (frame 2703).

### 4.1 Set-up (0bd5..0cac)

```c
p1_0bd5() {
    wait_retrace_start(); int10(0x13);            // frame 10
    in(0x3DA); out(0x3C0, 0x00); out(0x3C0, 0x00); // AC index 0 without bit 5: screen off; AC[0] = 0
    wait_retrace_start();
    SR[4] = 0x06;                                // chain-4 off (unchained, "mode X")
    CR[0x14] = 0x00;  CR[0x17] = 0xE3;           // no dword mode, byte mode
    SR[0] = 0x01;  misc = 0xE3;  SR[0] = 0x03;   // misc E3: 25 MHz clock, -/- sync polarity (see 4.7)
    SR[2] = 0x0F;
    line_compare(0x183);                         // CR18 = 83h, CR7 bit 4 = 1, CR9 bit 6 = 0 (bit 9 = 0)
    in(0x3DA); out(0x3C0, 0x30); AC[0x10] &= ~0x20; // index 10h with bit 5: screen on; panning also below the split
    memset32(A000:0000, 0, 0x4000 dwords);       // all four planes cleared
    CR[0x13] = 0xA8;                             // 168 words: a line is 336 bytes = 1344 pixels
    for (plane = 0, mask = 0x11; plane < 4; plane++, mask = rol(mask)) {   // 0x11, 0x22, 0x44, 0x88
        SR[2] = mask;
        blit_0b5c(ds:si = 00E4:070A + plane, es:di = A000:0050);
    }
    pos = 0;                                    // [016A], u16, 1/4 pixel units
    mix_tables(pos);  install_timer_0076();      // first handler run at frame 15
    do {
        mix_tables(pos);                         // 0cb5 / 0ce3
        if (pos > 0x1500) { restore_0132(); exit(0); }
        if ((in(0x60) & 0x7F) == 1) { restore_0132(); exit(0xFF); }
    } while (1);
}
mix_tables(pos) {                                // the weights are recomputed continuously by the main loop
    d = pos - 500;                               // [04EC], 16 bit
    weight_table_0b00(ax = 0, dx = 0x1000 - d, bx = 0x016C);   // tA
    weight_table_0b00(ax = 0, dx = d,          bx = 0x01AC);   // tB
}
blit_0b5c(src, di) {                             // 195 rows x 64 dwords per plane
    for (row = 0; row < 195; row++) {
        for (k = 0; k < 64; k++, src += 16, di += 4)   // ds advances one paragraph per dword
            *(u32 *)(A000:di) = src[0] | src[4] << 8 | src[8] << 16 | src[12] << 24;
        di += 0x50;
    }
}
```

The source is the linear 8-bit picture: a paragraph holds 16 consecutive pixels, byte `4k + p` is pixel `4k + p`, so
plane p receives pixels x = p (mod 4) of each row. Logo pixel (x, y) (x 0..1023, y 0..194) = picture row y + 1
(section 8) and lands in VGA memory at byte `0x50 + y * 336 + x / 4` of plane `x & 3`: the virtual screen is 1344 x
195, the logo sits at virtual x 320..1343, x 0..319 stay black. The last byte written is 0xFFEF (195 rows * 336 =
65520 bytes: rows 195..199 of the virtual screen would wrap around the 64 KB plane, hence the split, 4.5).

### 4.2 The palettes and the cross-fade (0b00, 0b24)

Palette A at 0010:04F0 (file 0x5F0, violet) and palette B at 0010:07F8 (file 0x8F8, red / orange; identical to the
RIX3 picture's own palette): 768 bytes of 6-bit RGB each, followed by 8 zero bytes; only colours 0..63 are used (the
rest is 0). Both are ramps from black (colour 0) to white (colour 63), monotonic in luminance.

```c
weight_table_0b00(ax, dx, bx) {                  // also in p4 1975, p5 2f99, p6 3871 (byte-identical)
    if ((int16)dx < 0) dx = 0;
    dx >>= 4;  if (dx >= 0x100) dx = 0x100;      // weight w = 0..256
    for (i = 0; i < 64; i++) {
        cs:[bx + i] = ax >> 8;                    // table[i] = (i * w) >> 8 (ax starts at 0)
        if (ax <= 0x3F00) ax += dx;              // never limits: 63 * 256 = 0x3F00
    }
}
dac_0b24() {                                     // in the handler, after the retrace start
    out(0x3C8, 0);
    for (j = 0; j < 192; j++) {                  // 64 colours x RGB
        c = tA[palA[j]] + tB[palB[j]];  if (c >= 63) c = 63;   // the clamp never acts (wA + wB <= 256)
        cs:[0x1EC + j] = c;                      // a copy nobody reads
        out(0x3C9, c);
    }
}
```

With d = pos - 500: wB = clamp(d >> 4, 0, 256) and wA = clamp((4096 - d) >> 4, 0, 256), so wA + wB = 256 (or 255
where 4096 - d is not a multiple of 16). Colour v = `(A[v] * wA >> 8) + (B[v] * wB >> 8)`, mixed in 6-bit DAC
values (display space, not linear light).

### 4.3 The handler (0020) and what each frame shows

```c
handler_0020() {
    cli; push all; out(0x20, 0x20); irqs++;
    crtc_start_0b8b();                           // CR0C/0D = pos >> 4   (byte address: 4 pixels)
    while (!(in(0x3DA) & 8)) ;                   // retrace start
    pit(0x30, count);
    panning_0ba3();                              // AC[0x13] = ((pos >> 2) & 3) * 2   (256-colour pel panning)
    pos += 2;
    sti;
    dac_0b24();                                  // with the tables of the main loop (built from pos - 2)
    acc += count; pop all; if (carry) jmp far old_int8; iret;
}
```

After the k-th handler run the screen shows position `pos = 2 (k - 1)`: start address and panning from the same
value (scroll = pos >> 2 whole pixels, 0.5 pixel per retrace), and the DAC from the weights of that value (the main
loop rebuilds the tables many times per retrace; checked for all frames). Screen rows 0..193 show virtual rows
0..193 from the start address; scan line 387 (= 0x183) is the line compare, so rows 194..199 show VGA offset 0 (with
the same panning): rows 0..5 at virtual x 0..322 = black. So the logo's last row (picture row 195: 14 dark pixels,
index <= 5) is never shown, and the bottom 6 rows are black. Without the split, rows 195..199 would show the wrapped
top of the plane.

0bbc (pos += 3 while pos <= 0xA80) is never called; [001E] (handler runs) and [01EC] (DAC copy) are never read.

### 4.4 Time base and remake formulas

Everything is counted in handler runs (retraces), nothing per drawn frame (p1 draws nothing after the set-up), no
music, no random numbers. With r = 70.0863 * (t - t0) retraces since the first handler run (t0, section 6):

- scroll: logo column c (0..1023) is at screen x = 320 + c - r / 2 (320-pixel screen), logo row y at screen row y;
  rows 194..199 black; everything outside the logo black.
- colour of a logo pixel with index v: w = clamp((2 r - 500) / 4096, 0, 1),
  `rgb = (A[v] * (1 - w) + B[v] * w) / 63`; w goes 0 -> 1 between r = 250 and r = 2298 (3.57 s .. 32.79 s).
- the logo's first columns (c = 22) appear at r = 46, the last (c = 1004) leave at r = 2649; p1 ends after r =
  2688 (the 2689th handler run makes pos 0x1502 > 0x1500), 38.35 s after t0.

### 4.5 Shortcuts to evaluate exactly, and quirks

- Scroll in whole pixels every second retrace (pos >> 2 with pos += 2): scroll continuously at 35.04 pixels (of
  320) per second.
- Weights: integer steps of 1/256 every 8 retraces and two truncations (`A * wA >> 8`, `B * wB >> 8`, up to 2/63
  darker): a continuous w(t) and a float mix.
- 64 shade indices and 6-bit ramps: the redraw / filtered reads (section 8).
- Keep: the composition (logo at the top, filling rows 0..193, 3.2 screen widths long, entering from the right and
  leaving to the left on black), the constant speed, the violet-to-orange cross-fade timing.
- Drop: misc register E3h (the 480-line sync polarity of the Mode X recipe on a 400-line, 70 Hz timing: a monitor that
  sizes by polarity would show the picture squashed to 5/6 of the height); the hidden last logo row (1 dark row).

## 5. Hand-overs between parts

### 5.1 First and last frames (capture)

| boundary | last frames of the old part | CDPI launch | new part, before its first handler run | first handler run |
|---|---|---|---|---|
| loader -> p1 | text (0..6), mode 13h black (7) | 7 | int 10h 13h at 10, mode X at 11, black (logo off-screen) | 15 |
| p1 -> p2 | black since 2665 (logo gone), DAC = palette B | 2705 | mode 13h at 2709: black, default DAC, AC 11h (border) = 0 | 2713 |
| p2 -> p3 | p2's last step (43e0: fade flag, 35 retraces) brightens to white; exit 43ed: restore the timer, wait for the retrace start, all 768 DAC values = 63 | 10023 | white (10023..10025), then p3 clears to its colour 0 = (0, 0, 3) (10026) | 10032 |
| p3 -> p4 | the nebula picture alone (16 colours), exit 2f57 changes nothing | 13638 | p4 reads p3's DAC (first 16 colours) and screen and starts on them: no visible change | 13651 |
| p4 -> p5 | normal exit 1e81: the chequerboard picture prepared at 4f1e:0000 (64000 bytes) is copied to A000, then the timer is restored | 21082 | the chequerboard (15 colours) stays | 21091 |
| p5 -> p6 | p5's last script step (36ba..3746, after restoring the timer): screen = chequerboard (low nibble) + swirl (high nibble, from 4ed1:0000), 64 calls of 3038 cross-fade the two 16-colour palettes (the weight tables of 4.2) by 0x40 per call; then the swirl alone (4ed1:0000 -> A000, its 16 colours from 2be5 to DAC 0..15) | 25588 | the blue swirl picture (indices 0..15) stays | 25597 |
| p6 -> p7 | exit 3d22: restore the timer, read the DAC, then per retrace DAC = (v * L) >> 8 with L = 256, 249, ..., 4 (37 steps), then all black | 29597 | black | 29605 |
| p7 -> end | exit 33be: restore the timer, DAC = (v * L) >> 8 with L = 256, 255, ..., 0 (257 steps), all black, int 10h mode 3 | 32120 (TERM) | text mode: TERM prints "■ TERM" and exits with 0xFF, CDPI prints "CDPI v0.2, Disconnecting life support systems.", the loader stops the music | — |

Nothing is reset between parts. p2, p3 and p7 set mode 13h (int 10h: cleared screen, default DAC, then their own
palette); p4, p5 and p6 do not set a mode: they start on the mode, screen and DAC the previous part left.

### 5.2 Three fades run too fast in the capture

p5's final cross-fade (3038: 64 calls), p6's exit fade (3d22: 37 steps) and p7's exit fade (33be: 257 steps) run
after the timer is restored and pace themselves with `while (!(in(0x3DA) & 8));` only, without first waiting for
the retrace to end (list: `retrace_waits.py`). On a VGA the vertical retrace pulse lasts 2 lines (63.6 us) and the
768 DAC writes of one step take longer than that even on PCI (about 0.12 us per OUT at least), so the next test sees
the retrace over and the loop does exactly one step per retrace. The emulator gives an I/O read the time of the start
of its 20,000-instruction quantum (0.2 ms), so it does about 4 steps per captured frame:

| fade | steps | capture | real VGA |
|---|---|---|---|
| p5 end, chequerboard -> swirl | 64 | frames 25572..25588 (16 frames) | 64 retraces (0.91 s) |
| p6 exit to black | 37 (+ black) | frames 29588..29597 (9 frames) | 37 retraces (0.53 s) |
| p7 exit to black | 257 (+ black) | frames 32056..32119 (64 frames) | 257 retraces (3.67 s) |

The remake should run these at one step per retrace (the formula itself is checked: `exit_fades.py`). That shifts
p6 by 48 retraces and p7 by 76 retraces later relative to the music than in the capture, and the end of p7 by 269.
Every other retrace wait in the parts either waits for the end of the retrace first or runs inside the timer
handler.

## 6. The global schedule

The parts follow each other with no reference to the music. In the capture (no disk time):

| part | CDPI launch | first handler run: frame, time (s) | after the first music tick (s) | frames to the next part's first handler run |
|---|---|---|---|---|
| p1 | 7 | 15, 0.2266 | 0.1162 | 2698 |
| p2 | 2705 | 2713, 38.7222 | 38.6118 | 7319 |
| p3 | 10023 | 10032, 143.1504 | 143.0400 | 3619 |
| p4 | 13638 | 13651, 194.7869 | 194.6765 | 7440 |
| p5 | 21082 | 21091, 300.9417 | 300.8313 | 4506 (+48 on a VGA, 5.2) |
| p6 | 25588 | 25597, 365.2339 | 365.1235 | 4008 (+28) |
| p7 | 29597 | 29605, 422.4204 | 422.3100 | 2514 to the last graphics frame (+193) |

(The first music tick is at 0.1104 s. Each handler run comes 0.3-0.5 ms before the retrace at line 412 of its
frame.) Each gap = the old part's retraces + its exit + CDPI + the new part's set-up (8-13 frames here). On a real
PC every gap grows by the disk and unpacking time of the next part, so the original drifts later against the music
by an amount that depends on the machine; the capture (zero disk time, 100 M instructions/s) is the reference the
remake can use, with the fades of 5.2 at their hardware speed.

## 7. RNC-1: NOTES.md checked

`models/core/rnc_from_notes.py` implements NOTES.md's "File layout" and "RNC ProPack method 1" sections literally
(header, CRC-16/ARC, the bit reader with its look-ahead word and the literal-run quirk, three canonical Huffman
tables per chunk, the value coding `(1 << (v-1)) | bits`, distance + 1, length + 2) and walks the MZ chain of
DOPE.EXE. All nine load modules come out equal to `unpacked/<name>.bin` (packed CRCs and unpacked CRCs check;
TERM is not packed, its .bin is its load module after the 0x20-byte header). The relocations are not needed:
`unpacked/*.bin` are the plain decoder output. Missing from NOTES.md, needed by a port: the MZ size is
`pages * 512 - (512 - last)` when `last` (header bytes 2..3) is not 0, else `pages * 512`; the load module starts at
`header paragraphs (bytes 8..9) * 16`.

## 8. Pictures

The loader (text only), CDPI, TERM and p1 hold one picture: p1's logo.

| picture | where | format | palette | hand-drawn? |
|---|---|---|---|---|
| "Complex Media Labs" logo | p1 00E4:0000 (file 0xE40): a ColoRIX "RIX3" file: "RIX3", width 1024, height 200 (u16 LE), palette type 0xAF, storage 0 (uncompressed), 768-byte 6-bit palette at 00E4:000A, then 1024 x 200 bytes at 00E4:030A (file 0x114A), linear, row r at 0x114A + 1024 r | indices 0..63 only (0 = background); content in rows 1..195, columns 22..1004; p1 copies rows 1..195 (from 00E4:070A) | the file's own (= palette B, 0010:07F8, orange); shown through the A/B cross-fade (palette A 0010:04F0, violet) | yes: DOPE.TXT "media labs logo reward". Chrome / gold extruded lettering in 64 smooth shades; the shading may come from a rendered or gradient-filled base, then pixelled (guess) |

How it is used: the indices go to the screen unchanged (no masks, no height map, no cycling) and are scrolled by
the hardware, but they are **shade levels**: both palettes are ramps from black (0) to white (63), and the colour of
the whole picture moves from palette A to palette B (section 4.2). A true-colour redraw therefore has to come either
as
- a high-resolution shade map (grey 0..63 as a float, 0 = black background) coloured per pixel by the cross-faded
  ramps `A(s) * (1 - w) + B(s) * w` (A, B interpolated between their 64 entries), or
- two colourings of the same redraw, a violet one (palette A) and an orange one (palette B), blended per pixel with
  `(1 - w)` and `w` — exact, because the original mix is linear in the two palette colours.

It must keep the extent (1024 x 195 logo pixels, logo row 0 = picture row 1, columns 22..1004 used) and the black
background. Written by `models/core/pictures.py`:

- `dump/pictures/p1-logo.png`: the whole 1024 x 200 RIX3 picture with its own palette (= B, the end colours);
- `dump/pictures/p1-logo-violet.png`: the same with palette A (the start colours);
- `dump/pictures/p1-logo-shown.png`: rows 1..195, the part p1 copies to the VGA, palette B.

## 9. Open points and corrections

- NOTES.md "Exit code 0xFF ends the chain: ... every part when a key is pressed": p3 and p7 exit with 0 on a key, p1
  and p4 test only ESC through port 60h (press or release), and the key stays in the BIOS buffer (2.1).
- NOTES.md "the handler (p2 0x1a21, p1 0020, p4 1420): EOI, wait for the start of vertical retrace ...": p1 writes the
  start address before the wait; p3, p5, p6 and p7 limit the wait (1000 / 5000 reads); p3 repeats the calibration
  until two measurements agree; p6's handler also calls 3895 (3.1).
- NOTES.md "Video modes", p1: misc E3h is the 480-line sync polarity on the 400-line timing (4.5); the logo is a
  linear RIX3 picture split into planes by the blit, not a planar picture.
- PARTS.md p1 "position 0..0x1500": the last shown position is 0x1500, p1 stops at 0x1502 after 2689 handler runs.
- PARTS.md "p6 fades out", "p7 ... dope": the capture's fades at the ends of p5, p6, p7 are about 4 times too fast
  (5.2); the part start frames after p5 are earlier than on hardware.
- The weight-table routine 0b00 is byte-identical in p4 (1975), p5 (2f99) and p6 (3871); p5's end cross-fade
  (2fbd, 3038) and p6's 3895 mix two palettes with it like p1's 0b24 (described here once; the part notes cover how
  their parts use it).
- Not determined: what CR1F means on the Cirrus chips (3.6); how long the disk reads and the RNC stubs take on a
  real PC (they only add to the gaps of section 6).
