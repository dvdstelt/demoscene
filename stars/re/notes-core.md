# Scope "core": the system, the shared helpers, main's script, the overlay files and the pictures

Addresses are offsets in `unpacked/stars_img.bin` (= the 32-bit program's near pointers = linear addresses in the
headless run). Frames, times and positions are from the capture (`full.ticks`, 40 M instructions per second, minute
0); "pos" is order:row in hex, "AX" the value 0x29f80 returns (order << 8 | row). "Retrace" = one call of 0x4a830
(70.086 Hz). The scripts named in brackets are in `models/core/` (see its README.md).

Checked against the capture and the snapshots:

- fades (`fades.py`): every DAC palette of the picture-57 and credits sequences (frames 15972–16642, 25163–26667)
  equals `min(63, (p * L) >> 8)` for the shown picture's palette p, with L stepping by 4 (by 8 at 0x9a00, by -2 at
  the end) once per retrace, exactly as 0x17e96 / 0x4a7b0 below.
- the random generator (`rng.py`): the state in the snapshots is reached from the seed 0x7ea9 by exact steps: 0
  calls before the first particle part, 16,926 calls in 0x133ad, 0 between, 45,787 in 0x133f1, none after.
- unpacking (`unpack_spec.py`): an unpacker written only from section 9 gives the program image and all 58
  overlay files identical to `unpacked/` (which `unpack.py` checks against the original code in unicorn).
- main's waits (`mainwaits.py`): the frames at which each music position is passed (table in section 2).
- the 3D-part backgrounds (`bgpal.py` and a difference mask): the screen equals files 19, 25, 27 and 47 (+0xe6)
  on rows 1..197 wherever no object covers it; their palette entries only change by uniform fades.
- the dissolving part's pictures (`dissolve_pics.py`): files 36..41 decode to the three women in 18-bit RGB.

---------------------------------------------------------------------------------------------------------------

## 1. Start-up and exit (entry 0x178c3)

```c
entry_178c3() {                         // PMODE has set DS=ES=FS=GS=SS, [0x17620] code32 base, [0x17624/8] DOS mem
    heap_size = 0x2d0000;               // 0x31: [0x17b01]
    if ((err = heap_init_17b56()) != OK) {          // err 0 / 1
        print_dollar(msg_table_17695[err]);         // "Not enough extended memory." / "Memory error."
        dos_exit(al);                               // int 21h AH=4C (AL = whatever is left: not 0)
    }
    [0x1762c] = DS;                     // the data selector every interrupt handler loads
    set_exception_handlers_17978();     // int 0 -> 0x179ec, int 0x0d -> 0x179f8 (int 31h 0204/0205; old ones saved)
    main_4d();
    shutdown_3c();                      // 0x4abb5 (IRQ 0 back, PIT to 18.2 Hz), 0x4a9c6 (close STARS.EXE),
                                        //   unmask IRQ 1 (port 0x21 &= ~2)
    heap_free_17b91();                  // int 31h 0502
    dos_exit(0);                        // int 21h AX=4C00
}
```

The exception handlers are never restored (0x179c3 restores them but is not called). A crash handler and the
memory errors share 0x17a02: text mode (int 10h AX=3), the register dump template at 0x17739.. is filled from the
`pushal` frame (EAX..EDI, DS from the stack, ES, FS, GS, SS, CS:EIP and FLAGS from the interrupt frame; the pointer
table at 0x176cb says where each value goes, hex digits from "0123456789ABCDEF" at 0x17610), the message
[0x178bb] and the dump are printed with int 21h AH=09, then it jumps to 0x178f3 (normal shutdown, exit code 0).
Messages: 0x17727 "Division by zero", 0x17707 "General protecion fault ;) ;)", 0x17b11 "Memory control block list
is corrupted.", 0x17b3b "Memory allocation error.".

Print helper 0x17ad6: EDX = near pointer to a `$`-terminated string; real-mode int 21h AH=09 through 0x1794a with
DS:DX = (EDX + base) >> 4 : (EDX + base) & 15 (the string must lie below 1 MB; all of them are in the image, which
PMODE loads in conventional memory). 0x1794a = int 31h 0300 (simulate real-mode interrupt BL) with the DPMI
register block at 0x17630 (EDI +0, ESI +4, EBP +8, EBX +0x10 = 0x17640, EDX 0x17644, ECX 0x17648, EAX 0x1764c,
flags 0x17650, ES 0x17652, DS 0x17654, SS:SP 0x17660:0x1765e set to 0 = host stack).

## 2. main 0x4d: the script

```c
main_4d() {
    pit_install_4ab7d();                // IRQ 0 -> 0x4ac1f, callbacks 1..3 = ret (0x4abb4)
    pit_rate_4abd4(0x4286);             // 70.0635 Hz
    if (open_self_4a98b() fails) { print_dollar(8 /* "You can't launch stars from floppy disk.$" */); return; }
    if (lca_detect_1ba6c(eax = 0) == 0) nosound_49ff4();   // AL = 0: no card -> [0x49fef] = 1, callback 3 = 0x4a015
    outb(0x21, inb(0x21) | 2);          // mask IRQ 1 (keyboard) for the whole demo
    mda_init_939();                     // second monitor (section 11)
    [4] = dos_time_minute() & 1;        // int 21h AH=2C, CL & 1
    if (!nosound) {
        [0] = alloc(0x177000);
        f = load(1);                    // the module
        lca_load_1c71b(esi = f, edi = [0], eax = -1);      // converts / uploads; EDI returns the end of the data
        resize([0], edi - [0]);  free(f);
    }
    int10(0x13);                        // mode 13h (frame 63)
    precalc_16945();                    // files 50, 49 (frames 63..234 at 40 M instr/s)
    intro_setup_5b84(eax = [4]);        // files 5, 6, 7
    if (!nosound) lca_start_1c759();    // frame 279 (4.009 s): the first GUS player tick
    [0x49ff0] = 0x2492;                 // no-sound clock: 10.0 rows/s
    intro_5a92();  intro_free_5b8f();   // waits for the positions 0x000..0xa00 itself, returns at 0a:39
    [0x49ff0] = 0x7d00;                 // no-sound clock: 34.2 rows/s
    cloud_setup_1241b();      wait(0x0b00);  cloud_126ae();   cloud_free_123d8();
    bee_setup_11d8e();                       bee_11e7f();     bee_free_11f48();
    spikes_setup_65ef();      wait(0x1700);  spikes_6534();   spikes_free_65ad();
    warp_setup_9d17();        wait(0x2700);  warp_975b();     warp_free_9dcf();
    tunnel_setup_1064();      wait(0x3500);  tunnel_11e8();   tunnel_free_110d();
    surfaces_setup_a14d();    wait(0x4500);  surfaces_a0ca(); surfaces_free_a346();
    show_picture_17e50(57);   wait(0x5500);  fade_17e96(+4, 64);
                              wait(0x5800);  fade_out_17eba(-4, 64);  clear_vga();
                              wait(0x5820);  star_164dc();    clear_vga();
                              wait(0x6100);  particles_setup_13388(); particles_133ad(); particles_free_1339d(); clear_vga();
    dissolve_setup_1528c();   wait(0x6900);  dissolve_1535c(); dissolve_free_1530a(); clear_vga();
    torso_setup_16d86();      wait(0x6c00);  torso_16f7c();   torso_free_174bd(); clear_vga();
    particles_setup_13388();  particles_133f1(); particles_free_1339d(); clear_vga();
    face_setup_10b26();       wait(0x8700);  face_1089c();    face_free_10bd9();
    // credits (section 2.2)
    a = 0x36; b = 0x35; if ([4]) swap(a, b);           // even minute: 54 (KARL) first, then 53 (BARTi)
    show_picture_17e50(a);    wait(0x9500);  fade_17e96(+4, 64);  wait(0x9519);  fade_out_17eba(-4, 64);
    show_picture_17e50(b);    wait(0x9600);  fade_17e96(+4, 64);  fade_out_17eba(-4, 64);
    show_picture_17e50(55);   wait(0x9700);  fade_17e96(+4, 64);  fade_out_17eba(-4, 64);
    show_picture_17e50(52);   wait(0x9800);  fade_17e96(+4, 64);  fade_out_17eba(-4, 64);
    show_picture_17e50(10);   wait(0x9a00);  fade_17e96(+8, 32);  fade_out_17eba(-4, 64);
    show_picture_17e50(56);   wait(0x9a20);  fade_17e96(+4, 64);
    retraces_4a843(20);
    if (!nosound) { lca_stop_1c77e(); free([0]); }
    nosound_end_4a006();                // see section 10
    retraces_4a843(180);
    fade_out_17eba(-2, 128);            // "NOOON 50 $" to black, frees the picture
    mda_end_7dd();
    int10(3);                           // text mode (frame 26668, 380.50 s)
}
wait(x)    { while (pos_29f80() <= x) ; }            // `call 0x29f80; cmp ax, x; jbe`: busy loop, no retrace wait
clear_vga(){ memset(0xa0000 - [0x17620], 0, 64000); } // inline `rep stosd` (0x1ee, 0x212, 0x240, 0x26e, 0x29c, 0x2bf)
```

`clear_vga` writes index 0 and leaves the palette alone: the screen shows colour 0 of the palette that is set (black
everywhere it happens). The busy `wait` loops do not touch the screen: whatever the part left stays until the next
part draws.

### 2.1 When each wait ends (capture)

| wait (AX >) | first frame past it | time (s) | what follows | part actually starts |
|---|---|---|---|---|
| 0x0b00 | 5421 | 77.347 | cloud 0x126ae | 5421 |
| 0x1700 | 7170 | 102.302 | spikes 0x6534 | 7170 |
| 0x2700 | 9478 | 135.233 | warp 0x975b | 9478 |
| 0x3500 | 11498 | 164.055 | tunnel 0x11e8 | 11498 |
| 0x4500 | 13806 | 196.986 | surfaces 0xa0ca | 13810 (0xa14d's precalc ends later) |
| 0x5500 | 16114 | 229.917 | fade in picture 57 | 16115 |
| 0x5800 | 16547 | 236.095 | fade out | 16548 |
| 0x5820 | 16643 | 237.464 | star 0x164dc | 16643 |
| 0x6100 | 17845 | 254.615 | particles 0x133ad | 18451 (the star part returns at 65:0a, by itself) |
| 0x6900 | 18999 | 271.080 | dissolve 0x1535c | 18999 |
| 0x6c00 | 19504 | 278.285 | torso 0x16f7c | 19504 |
| (none) | | | particles 0x133f1 | 21956 (right after the torso returns at 7d:00) |
| 0x8700 | 23435 | 334.373 | face 0x1089c | 23436 |
| 0x9500 / 0x9519 | 25455 / 25530 | 363.195 / 364.265 | credit 1 in / out | |
| 0x9600 | 25599 | 365.250 | credit 2 in + out | |
| 0x9700 | 25743 | 367.304 | credit 3 (55) in + out | |
| 0x9800 | 25887 | 369.359 | credit 4 (52) in + out | |
| 0x9a00 | 26176 | 373.482 | picture 10 in (fast) + out | |
| 0x9a20 | 26272 | 374.852 | picture 56 in | |

A part starts at max(its music position, the end of what main did before). The remake should keep both rules:
the particle part 0x133ad does not start at 0x6100 but when the star part ends itself (its own end test), and
0x133f1 has no wait at all.

### 2.2 The picture sequences, frame by frame

Picture 57 (between the surfaces and the star): shown at frame 15972 (227.89 s, right after 0xa346 returns) with a
black palette; the fade writes L = 0, 4, .., 256 at frames 16115..16179 (one per retrace, first at the first
retrace after 55:01); full until 16547; L = 256, 252, .., 0 at 16548..16612 (the first fade-out write repeats 256);
cleared; the star part starts at 58:21 (frame 16643).

Credits (even minute: 54 then 53; odd: 53 then 54): each picture is loaded and shown black right after the previous
fade-out, faded in when its position passes, and (except the first, which waits for 0x9519) faded out immediately:
in 65 writes + out 65 writes, two consecutive frames at L = 256. Picture 10 at 9a:01 fades in by 8 per retrace
(33 writes, full at frame 26209) and out by 4. Picture 56 ("NOOON 50 $") fades in at 9a:21 (full at 26339), then
20 retraces, music stop at frame 26359 (376.09 s), 180 retraces, fade out by 2 per retrace over 128 retraces
(129 writes, from frame 26541), text mode at frame 26668 (380.50 s).

Time base of all of these: the music position for the starts, then one step per retrace (70.086 Hz).

## 3. Screen helpers and the fader (0x4a720..0x4a868, 0x17e50..0x17ec8)

All palettes are 768 bytes of 6-bit RGB (0..63), colour i at 3 i.

```c
// 0x4a720 scale: EAX first colour, ECX count, ESI src palette base, EDI dst palette base, BX = level (16 bit)
for (k = 3*first; k < 3*(first+count); k++) {
    uint32 prod = src[k] * BX;                 // mul bx: DX:AX
    uint8  al   = (prod >> 8) & 0xff;          // shrd ax, dx, 8 (only AL is tested)
    dst[k] = al > 63 ? 63 : al;                // levels used are 0..256: dst = src * L / 256, truncated
}
// 0x4a742 set DAC: AL first colour (also the DAC start index), ECX colours, ESI palette base: outb(0x3c8, AL),
//   then 3*ECX bytes from ESI + 3*EAX to 0x3c9.   0x4a759 (unused): the same without the 3*EAX offset.
// 0x4a76c: all 256 DAC colours black (768 zero writes from index 0).   0x4a781 (unused): all white (63).
// 0x4a830: wait for the vertical sync to start (0x3da bit 3 = 1), then for it to end (bit 3 = 0).
// 0x4a843: ECX times 0x4a830.          0x4a84e: clear the VGA page (64000 zero bytes).

struct Fader {                      // shared by main and the parts that fade with it
    uint8 *src;   // [0x4a798] target palette (768 bytes)
    uint8 *dst;   // [0x4a79c] work palette (768 bytes)
    int32  level; // [0x4a7a0] 0..256
    int32  step;  // [0x4a7a4] added per step
    int32  count; // [0x4a7a8] steps left
    int32  dirty; // [0x4a7ac] set by a step, cleared by apply
};
fader_apply_4a7b0() {               // called once per drawn frame by the parts, per retrace by 0x17e96
    if (dirty == 1 || count != 0) {
        dirty = 0;
        scale_4a720(first 0, 256 colours, src, dst, level);
        wait_retrace_4a830();
        set_dac_4a742(0, 256, dst);   // written just after the sync pulse, during the vertical blank
    } else wait_retrace_4a830();
}
fader_step_4a80b() { if (count) { level += step; count--; dirty = 1; } }

show_picture_17e50(n) {             // n = a 0xfd00 file: 768-byte palette + 320x200 picture
    level = 0;
    dac_black_4a76c();
    p = load(n);
    memcpy(0xa0000 - [0x17620], p + 0x300, 64000);    // straight to the screen, no retrace wait (palette is black)
    p = resize(p, 0x600);           // keeps the palette and 0x300 bytes for the work palette
    src = p; dst = p + 0x300;
}
fade_17e96(EBX step, ECX n) {       // n + 1 palette writes, one per retrace: level, level+step, .., level+n*step
    this.step = EBX; count = ECX;
    do { fader_apply_4a7b0(); fader_step_4a80b(); } while (count);
    fader_apply_4a7b0();            // dirty = 1: writes the final level
}
fade_out_17eba(EBX, ECX) { fade_17e96(EBX, ECX); free(src); }
```

So a main-script fade of n steps takes n + 1 retraces; the k-th write (k = 0..n) happens in the k-th vertical blank
after the call and is shown from the next frame on. Remake: `colour = picture_colour * L(t) / 256` with
L(t) = L0 + step * (retraces since the start), clamped to the end value; the 6-bit truncation (and the 8-bit
level) are 320x200-era quantisation and can be evaluated continuously.

Parts that fade with the fader step it in their PIT callback (0x4a80b at 0xf86 tunnel, 0x5b9b intro, 0x9933 warp,
0x10b1f face, 0x13549 particles, 0x1544e dissolve) and apply it once per drawn frame in their main loop (0x121a,
0x986f, 0x10ac2, 0x1346c / 0x134c0, 0x15422): there the level is a function of PIT ticks (70.064 Hz) and only the
moment it is shown depends on the frame rate. Their settings (level, step, count): tunnel 0, +1, 256 (and -1,
256 from order 0x43, set in its callback); warp 0, +8, 32 (and -8 at the end); face, particles, dissolve 0, +4, 64
and -4, 64 (details in the part notes).

## 4. The PIT and the interrupt handlers

```c
pit_install_4ab7d() {
    [0x4ab06] = [0x4ab0a] = [0x4ab0e] = 0x4abb4;       // the three callbacks = `ret`
    token = irq_get_4a610(0);  [0x4ab77] = DX; [0x4ab79] = token;
    irq_set_4a620(0, 0x4ac1f);
}
pit_rate_4abd4(BX) { [0x4ab02] = BX; outb(0x43, 0x36); outb(0x40, BX & 0xff); outb(0x40, BX >> 8); }
                                    // channel 0, lo/hi byte, mode 3; 0x4286 = 17030: 1193182 / 17030 = 70.0635 Hz
pit_restore_4abb5() { irq_set_4a620(0, token); outb(0x43, 0x36); outb(0x40, 0); outb(0x40, 0); }   // 18.2 Hz
set callback 1: 0x4abec (EAX), clear: 0x4abfe;  callback 2: 0x4abf2 / 0x4ac09;  callback 3: 0x4abf8 / 0x4ac14

irq0_4ac1f() {                      // push eax, ds, es; DS = ES = [cs:0x1762c]
    call [0x4ab06];                 // the part's animation step
    call [0x4ab0a];                 // MDA game (section 11)
    call [0x4ab0e];                 // no-sound music clock (section 10)
    outb(0x20, 0x20);  iretd;       // the BIOS tick is not chained: DOS time stands still
}
```

The callbacks get only EAX, DS and ES saved; they save the rest themselves (`pushal`). Within one tick the part's
callback runs before the no-sound clock.

IRQ handler records (0x4a610 / 0x4a620): one 0x5c-byte record per IRQ at 0x4a050 + 0x5c * irq:

| offset | contents |
|---|---|
| +0x00 | dword: the new protected-mode handler (offset in CS) |
| +0x04 | word: CS |
| +0x06 / +0x08 | old real-mode vector offset / segment (int 31h 0200) |
| +0x0a / +0x0e | old protected-mode vector offset / selector (int 31h 0204) |
| +0x10 | 0x32-byte DPMI real-mode register structure for the callback |
| +0x42 | the interrupt number: 8 + irq, or 0x68 + irq for irq >= 8 |
| +0x43 | real-mode stub: `9a <callback seg:off>` (call far, filled from int 31h 0303) `cf` (iret) |
| +0x49 | protected-mode callback procedure: `cld; lodsw; mov es:[edi+0x2a], ax; add word es:[edi+0x2e], 4; jmp cs:[record+0]` |

`irq_get_4a610(CL)` returns EBX = the record's address and DX = CS (a token). `irq_set_4a620(CL, DX:EBX)`: if EBX
is the token, the old vectors are put back and the callback is freed (0201, 0205, 0304); otherwise EBX is the new
handler: the old vectors are saved, the protected-mode vector set (0205), a real-mode callback allocated (0303)
and the real-mode vector pointed at the stub (0201, segment:offset from the record's linear address). So an IRQ
that arrives in real mode (while DOS reads a file through int 31h 0300) goes stub -> callback -> the same handler:
the PIT callbacks and the music keep running during loading. The callback procedure pops the stub's return IP into
the register structure and drops the far return address, so the real-mode `iret` of the stub ends the interrupt.
The GUS IRQ is installed by the player the same way (0x29fa8 / 0x29fae, music scope).

## 5. Memory: the heap, selectors, buffer lists

### 5.1 KARL's memory manager

One DPMI block (int 31h 0501) of [0x17b01] = 0x2d0000 bytes (0x80000 if that variable were 0); its near address in
[0x17b05], handle in [0x17b0d] / [0x17b0f]. Block headers are 16 bytes:

```c
struct MCB { char tag[3] = "KMM"; char state;   // 'F' free, 'B' busy
             MCB *prev; MCB *next; uint32 size; };   // size = data bytes after the header (multiple of 16)
heap_init_17b56() {
    if (dpmi_alloc_17d3d() fails) return 0;              // "Not enough extended memory."
    for (p in block) { *p = 0x55; if (*p != 0x55) return 1; *p = 0xaa; if (*p != 0xaa) return 1; }  // RAM test
    first = block: {"KMMF", prev 0, next = block + size - 16, size - 0x20};
    last  = block + size - 16: {"KMMB", prev = first, next 0, 0};  [0x17b09] = last;
}
```

**The RAM test leaves every heap byte 0xaa**: memory that a part allocates and reads before writing reads 0xaa
(or whatever an earlier part left there).

```c
check_17d10()  { for (h = first; h; h = h->next) if (h->tag != "KMM") fatal("Memory control block list is corrupted."); }
alloc_17bd7(EDX) -> EAX {            // first fit from the bottom
    check(); n = (EDX + 15) & ~15;
    for (h = first; h; h = h->next) if (h->state != 'B' && h->size >= n) break;
    if (!h) fatal("Memory allocation error.");           // via the crash screen 0x17a02
    rest = h->size - n - 16;
    if (rest > 0) { new = (char*)h + 16 + n; *new = {"KMMF", h, h->next, rest}; h->next->prev = new;
                    h->next = new; h->size = n; }        // otherwise the whole block is taken
    h->state = 'B'; return h + 1;
}
alloc_top_17c43(EDX) -> EAX {        // used for loading files: takes the top of the LAST free block
    check(); n = (EDX + 15) & ~15;
    for (h = last; h->state != 'F'; h = h->prev) ;      // the first free block from the top
    rest = h->size - n - 16;   // `sub eax, edx; sub eax, 16; jb`: only the second borrow is tested, there is no
                               // real size check and no error message (never too small in the demo)
    b = (char*)h + rest;                                 // off by 16: h->size = rest overlaps b's header;
    h->next = b; h->size = rest;                         //   free() recomputes sizes from the pointers
    *b = {"KMMB", h, old next, n}; old next->prev = b; return b + 1;
}
free_17c8e(EAX) {                    // merges with free neighbours, sizes recomputed from the pointers
    check(); h = EAX - 16; prev = h->prev; next = h->next;
    if (prev && prev->state != 'B') lo = prev; else { h->state = 'F'; if (next->state == 'B') return; lo = h; }
    if (next->state != 'B') next = next->next;
    lo->size = (char*)next - (char*)lo - 16; lo->next = next; next->prev = lo;
}
size_17cce(EAX) = ((MCB*)EAX)[-1].size;
resize_17cd2(EAX, EDX) {             // only shrinks safely: new free header right after the new size
    check(); n = (EDX + 15) & ~15; h = EAX - 16; next = h->next; if (next->state != 'B') next = next->next;
    f = (char*)h + 16 + n; *f = {"KMMF", h, next, (char*)next - f - 16};
    h->size = n; h->next = f; next->prev = f;
}
```

The overlay loader reads each file into a block from the top (0x17c43) and unpacks it into a block from the
bottom (0x17bd7), then frees the top block, so loading does not fragment the heap. None of this is visible on
screen; the remake only needs "0xaa in fresh memory" where a part depends on it (to be checked by the parts).

### 5.2 Selectors (int 31h, the engine and some parts address buffers through FS / GS / ES)

```c
make_data_selector_17da3(EAX near, EDX size) -> BX {   // EAX, ECX, EDX preserved
    BX = alloc_ldt(1);                                  // 0000
    set_base(BX, EAX + [0x17620]);                      // 0007
    set_limit(BX, EDX - 1);                             // 0008 (byte granular for < 1 MB)
    set_rights(BX, CL = access byte of DS (lar), CH = 0x40);   // 0009: 32-bit (B/D bit) data
}
make_code_selector_17e04: the same with CS's access byte and CH = 0 (16-bit): unused.
set_selector_base_17de6(BX, EAX near);  free_selector_17dfd(BX);   // 0007 / 0001
```

### 5.3 Buffer lists

```c
struct Buf { void *p; uint16 sel; };                     // 6 bytes
buf_alloc_4a870(ESI = Buf*, EDX size) { ESI->p = alloc(EDX); ESI->sel = make_data_selector(ESI->p, EDX); }
buf_free_4a883(ESI)  { free(ESI->p); free_selector(ESI->sel); }
// a list: 9-byte entries { Buf *slot; uint32 size; uint8 want_selector; }, ended by dword -1
bufs_alloc_4a894(ESI list) { for (e : list) { e.slot->p = alloc(e.size); e.slot->sel = e.want_selector ?
                                              make_data_selector(e.slot->p, e.size) : 0; } }
bufs_free_4a8c7(ESI list)  { for (e : list) { free(e.slot->p); if (e.slot->sel) free_selector(e.slot->sel); } }
                                              // steps with `add si, 9` (16 bit): a list must not cross 64 KB
```

## 6. Overlay loading and DOS file I/O

```c
open_self_4a98b() {                  // CF = 1: refused (floppy)
    env = env_17927();               // PSP (int 21h AH=51) -> [PSP+0x2c] -> int 31h 0006 -> near pointer
    p = env + 1; while (*(uint16*)p != 1) p++;   // the word 0x0001 after the environment's double zero
    strcpy(0x4a90a, p + 2);          // program name, e.g. "C:\STARS\STARS.EXE"
    if (name[0] == 'A' || name[0] == 'B') return CF;
    [0x4a908] = dos_open_4cfc8(0x4a90a);         // int 21h AX=3D02 (read/write); a failure is not checked
}
load_4a9f3(AX n) -> EAX {
    dos_seek_end_4d036(handle, -12 * n);         // ECX = n * -12, int 21h AX=4202 with CX:DX = ECX
    dos_read_4d086(handle, 0x4a8f8, 12);         // [0x4a8f8] n, [0x4a8fc] offset from the end, [0x4a900] size
    p = alloc_top_17c43([0x4a900]);
    dos_seek_end_4d036(handle, [0x4a8fc]);
    dos_read_4d086(handle, p, [0x4a900]);
    return unpack_4ac8b(p);
}
unpack_4ac8b(EAX p) -> EAX {
    if (*(uint16*)(p + 6) == 'dl' && p[8] == 'z') {     // "dl" at +6, "z" at +8
        size = (p[14] << 14) then low 16 bits replaced by *(uint16*)(p + 15);   // = (p[14] >> 2) << 16 | word15
        return diet_4ad47(esi = p, eax = alloc_17bd7(size));   // frees p at the end
    }
    q = alloc_17bd7(size_17cce(p)); memcpy(q, p, size_17cce(p)); free(p); return q;   // raw (26, 31)
}
dos_read_4d086(BX handle, EDI dst, ECX n) -> EAX {     // through the 8 KB real-mode buffer at 0x4afc0 (in the image)
    total = 0;
    do { k = min(n, 0x2000); got = int21_3F(handle, 0x4afc0, k); total += got; if (!got) break;
         n -= got; memcpy(dst, 0x4afc0, got); dst += got; } while (n);
    return total;
}
```

Other I/O helpers: 0x4d00d close (AH=3E), 0x4d047 file size (seek end 0, back to 0), 0x4d14a / 0x4d156 load a
whole file by name into a top / bottom block (open, size, alloc, read, close; 0x4d156 is called only from the
engine's unused 0x78cc4). Unused: 0x4cfe7 create, 0x4d025 seek from start, 0x4d0f2 write, 0x4a9d3 / 0x4aa5e /
0x4ac60 / 0x4acb7 (load variants without unpacking or into a top block).

The DIET routine 0x4ad47 (ESI = stored data with its 17-byte header, EAX = destination) is the one described in
NOTES.md and transcribed in `unpack.py`; I checked every branch of the code against that description (the bit
reader, the byte read after the second bit, the three short forms, the long offsets, the length codes, `cx = byte
+ 0x11` with CH = 0, the 32-bit offset `0xffff0000 | bh << 8 | bl`). It ends with `pop eax; call free` (the stored
block) and returns the destination.

## 7. The random generator 0x4af10

```c
uint32 seed_4af35 = 0x00007ea9;      // in the image; never written by anything else
uint32 random_4af10(uint32 n) {      // EAX = n -> EAX = s % n (unsigned); EBX, EDX preserved
    s = rol32((s ^ 0x78d47e5d) * 0xd413d473, 18);  return s % n;
}
```

Callers: the particle parts only (0x135d0, 0x135e3, 0x135f7, 0x13615, parts2d-b) and the MDA game (0xacc, 0xad8,
0xb07, 0xb13). Without a second monitor the particles get the sequence from the seed (16,926 calls in 0x133ad,
then 45,787 in 0x133f1, `rng.py`). With an MDA monitor the game takes at least two numbers at start-up and more
during the demo, so the particles' sequence differs on such machines: the remake should use the no-MDA sequence.

## 8. Variants: the minute of the clock

`int 21h AH=2C` once at start: [4] = CL & 1 (minute parity). main passes it to the intro (0x5b84 stores it in
[0x5426]: the BARTi / KARL name cards swap, parts2d-a) and swaps the first two credit pictures: even minute 54
"SYSTEM & SPECIAL EFFECTS KARL" then 53 "3D EFFECTS & MOTION BARTI"; odd minute the other way round. The capture
is an even minute.

## 9. Unpacking STARS.EXE: what a C++ unpacker needs

NOTES.md ("File layout") is complete for this: `unpack_spec.py` was written from it alone and reproduces
`unpacked/stars_img.bin` and all 58 files. In short, for the C++ port:

1. Decrypt file bytes 0xa0..0x125a0 word by word: `w = ((((w - 0xbc67 - 0x305e + 0x9af9) & 0xffff) ^ 0xc4f5) -
   0x7b9b) & 0xffff`. Optional check: `ck ^= w; ck = (ck + lo(w) * hi(w)) & 0xffff` over all words must give
   0x0981 (stored in the encrypted stub at 0x2d8; a format constant).
2. PKLITE: bit reader on the decrypted image from offset 0x290 (16-bit LE words, LSB first, the next word is
   fetched right after the 16th bit of the current one is taken, i.e. before any byte read that follows; the first
   word before the first bit). `0` + byte = literal; `1` + length code (prefix codes as in NOTES.md, `011100` +
   byte: 0xfe = skip, 0xff = end, else 25 + byte) + for lengths other than 2 the offset-high code + the offset-low
   byte; copy `length` bytes from `out - (high << 8 | low)` one byte at a time. Result: 0x7c0ab bytes (sha256 of the
   unrelocated image `4051b57215bf2838...`; NOTES.md's `322e3fdd...` is the image relocated for load segment
   0x1000 and padded with zeros to 0x840b0). The 17 relocations are all in the 16-bit kernel (0x7a287..0x7b7fc):
   the remake reads only the 32-bit part (0..0x7a210) and can ignore them.
3. Overlay: directory entry n (1..58) = 12 bytes at `file_size - 12 n` = (dword n, int32 offset from the end of the
   file, dword stored size); DIET when bytes 6..8 are `dlz` (decompress as in NOTES.md, original size from bytes
   14..16), else raw.

The bit reader is the same for PKLITE and DIET; both copy byte by byte (overlapping copies repeat bytes).

## 10. No sound: the PIT music clock (0x49ff4, 0x4a015, 0x4a006)

```c
nosound_49ff4() { [0x49fef] = 1; set_callback3(0x4a015); }
nosound_tick_4a015() {               // PIT callback 3, every tick (70.0635 Hz)
    acc_49ff2 += step_49ff0;         // 16 bit; step is 0 until main sets it (0x2492 before the intro, 0x7d00 after)
    if (carry) { if (++row_1c828 == 0x40) { row_1c828 = 0; order_1c829++; } }
}
nosound_end_4a006() { if ([0x49fef] != 1) clear_callback3(); }   // inverted test: a no-op in both cases
```

Rows per second: 70.0635 * 0x2492 / 65536 = 10.009 (intro), 70.0635 * 0x7d00 / 65536 = 34.211 afterwards; every
order has 64 rows. With the GUS the module plays 10.016 rows/s in the intro (64-row orders) and 23.321 rows/s
afterwards, but most orders from 0x0b on end at row 0x2f (pattern break; 0x16, 0x69, 0x6a at 0x35, 0x6b, 0x7e at
0x3b, 0x9a at 0x34). Per order that is 2.058 s with music against 1.871 s without: the no-sound demo runs about 9 %
faster after the intro, not 47 % (correction to NOTES.md). The remake plays the music, so this path only matters
as a fallback.

## 11. The second monitor: a snake game (0x939, 0x83e, 0x7dd and 0x9a8..0xc91)

NOTES.md calls it a performance display; it is a small game for a monochrome monitor (debug fun, nothing on the VGA
screen depends on it unless it changes the random sequence, section 7):

- 0x939: int 10h AX=1A00; only if BH (the inactive display) = 1 (MDA): [0x7b0] = 1, clear the MDA text page
  (0xb0000, 80x25), rows 0 and 23 filled with bright full blocks (0x0fdb), "SCORE:" + score on row 24 (0xbe2), a
  food item `OO` (0x0f4f twice) at a random free cell (0xac7: x = 2 * random(40), y = 1 + random(22)), PIT
  callback 2 = 0x83e.
- 0x83e every tick: reads port 0x60 directly (IRQ 1 is masked, so the last scan code stays there): space (0x39)
  starts the game, arrows (0x48 / 0x50 / 0x4d / 0x4b) set the direction. Once started, every
  `max(1, 12 - max(score, 0) / 64)` ticks the snake (body arrays at 0x488 (x) / 0x618 (y), length [0x7aa],
  starting at 2, at most 30) moves one 2-character cell, wrapping horizontally (0..39). Eating food: +10
  ([0x7d7]) and a new food, the snake grows; about every seventh food ([0x7ae] += 0x26 carries) a wall block
  appears at a random cell (0xb02). Hitting a block: -100 and the snake restarts at (20, 12) with length 2.
- 0x7dd at the end (only with an MDA): clears the page and shows "SCORE -> MIN : n MAX : n" (lowest / highest
  score seen, [0xc0c] / [0xc10]); the "text page" at 0x7db is just a line feed.

## 12. Other shared or unused routines

- 0x17ed0 event table (used by the tunnel, 0x1229): ESI = entries of 16 bytes {u32 from, u32 to, u32 func, u32 arg},
  ended by from = -1; for each entry with from <= EAX <= to (unsigned) it calls func with EAX = arg (`pushal`
  around). Called once per frame with EAX = the part's tick counter.
- 0x17f09 / 0x17f47: a key-frame interpolator (init: ESI = keys {u32 frames, ECX values}, EDI = output; step: out[i]
  = key[i] + (next[i] - key[i]) * t / frames, next key when t reaches frames, stops at a key with 0 frames). Not
  called anywhere.
- Unused: 0x17905 (PSP + 0x80), 0x179c3 (restore exception vectors), 0x17e04, 0x4a759, 0x4a781, the I/O variants
  of section 6, 0x6052 / 0x6075 (intro: a 64-colour picture fade from [0x5471]), the engine's 0x78cc4; the string
  "bimo.cod" at 0x4ab12 is not referenced.

## 13. The 58 overlay files

Kinds: "picture" = 768-byte palette + 320x200 (0xfd00); "screen" = 320x200 indices without palette (0xfa00);
"map" = 256x256 bytes; "palette" = 768 bytes of 6-bit RGB. Offsets and stored sizes are in `unpacked/files.txt`.
"Loaded by": call site of 0x4a9f3 (or 0x78c62 = the engine's object loader, EAX = file; 0x17e50 = show picture).

| n | size | kind / layout | loaded by | used for | scope |
|---|---|---|---|---|---|
| 1 | 0xbe910 | MOD module, 16 channels (`16CH`) | main 0xb4 | converted by 0x1c71b into [0] (patterns), samples uploaded, then freed | music |
| 2 | 0x10000 | map: heights 3..255 | 0x1064 (0x1078) + selector | tunnel: voxel heights (ES) | parts2d-a |
| 3 | 0x10000 | map: colours 1..222 | 0x1064 (0x1098) + selector | tunnel: voxel colours (GS), palette 4 | parts2d-a |
| 4 | 0x300 | palette | 0x1064 (0x10c7) | tunnel: fader target, faded in by 1 per tick over 256 ticks | parts2d-a |
| 5 | 0x512d | 15 colours (DAC 1..15) + 4 sprites 144x36 | intro 0x5fa9 | the words "Magic", "Atomic", "Nutritive", "Powerful" | parts2d-a |
| 6 | 0x9430 | 16 colours (DAC 0x60..0x6f) + 4 sprites 128x74 | intro 0x5fc9 | the names "BARTi", "RA", "GROO", "KARL" | parts2d-a |
| 7 | 0x101c0 | 64 colours (DAC 0x10..0x4f) + map + 0x100 zeros | intro 0x5fe9 + selector | flower petal texture (colour-cycled) | parts2d-a |
| 8 | 0xfd00 | picture | intro 0x627e (0x17e50) | "BRING THE FAMILY" card (order 0x01) | parts2d-a |
| 9 | 0xfd00 | picture | intro 0x627e | "Join us" card (order 0x00) | parts2d-a |
| 10 | 0xfd00 | picture | intro 0x627e; main (credits) | "STARS wonders of the world" (order 0x0a; credits 9a:01) | parts2d-a, core |
| 11–13 | 0xfd00 | picture (identical palettes) | intro 0x6251; warp 0x9d17 | the woman in three poses (orders 0x03, 0x05, 0x07); warp textures | parts2d-a |
| 14 | 0xfd00 | picture | intro 0x627e | the green poodle (order 0x09) | parts2d-a |
| 15 | 0x300 | palette | spikes 0x6690 | spike ball palette (scaled by the part's fade) | parts3d-a |
| 16 | 0x10000 | map: levels 0..8 | spikes 0x66ae + selector | engine texture (0x79801) of the spike ball | parts3d-a, engine |
| 17 | 0x10000 | map: 0..15 | spikes 0x6701 + selector | becomes `(v - 8) * 2` in place: the engine's offset / bump map [0x558d8] | parts3d-a, engine |
| 18 | 0x8038 | `B!` object | spikes 0x667c (0x78c62) | the spike ball | engine, parts3d-a |
| 19 | 0xfa00 | screen: colours 1..6 | spikes 0x66e1 + selector | background [0x558d2] (the dark spiral) | parts3d-a |
| 20 | 0x300 | palette | surfaces 0xa20c | blended with 22 (0xa530) | parts3d-b |
| 21 | 0x10000 | map: levels 0..7 (4x2 repeats of a 64x128 tile) | surfaces 0xa259 + selector | texture morph end (top-left 64x128 only) | parts3d-b |
| 22 | 0x300 | palette | surfaces 0xa1fd | surfaces palette | parts3d-b |
| 23 | 0x10000 | map: levels 0..7 (4x2 tiles) | surfaces 0xa239 + selector | texture morph start (top-left 64x128 only) | parts3d-b |
| 24 | 0x1a838 | `B!` object | surfaces 0xa15c (0x78c62) | the surfaces | engine, parts3d-b |
| 25 | 0xfa00 | screen: colours 1..6 | surfaces 0xa32a + selector | background (blue-violet clouds) | parts3d-b |
| 26 | 0x300 | palette (raw, not DIET) | bee 0x11d98 | bee palette; colour 0 replaced by the first 3 bytes of 31 | parts3d-a |
| 27 | 0xfa00 | screen: 157 colours | bee 0x11de8 + selector | background (cloud tunnel) | parts3d-a |
| 28 | 0x22498 | `WLDD` object | bee 0x11dca | the bee (36 parts) | engine, parts3d-a |
| 29 | 0x102ff | map 0xffff bytes (0..63) + 0x300 grey ramp palette (not read) | bee 0x11e08 + selector; torso 0x16de7 | bee: env map (selector [0x11b2a] -> [0x55918]); torso: `(v >> 1) + 0x60`, env map [0x5590e] | parts3d-a, parts3d-b |
| 30 | 0x1000a | map: levels 0..15 + 10 bytes | cloud 0x12420 + selector | cloud texture; a copy with `(v & 15) << 4` (two layers combined as 16 a + b) | parts3d-a |
| 31 | 0x300 | palette (raw) | cloud 0x1246d | cloud palette (16 x 16 combinations) | parts3d-a |
| 32 | 0xb40 | sprite 80x36 | bee 0x11dd9 | "23806 faces" (indices < 0x50 drawn) at (0, 163) | parts3d-a |
| 33 | 0x1130 | 2200 int16 | cloud 0x12492 | table for 0x124eb (cloud set-up) | parts3d-a |
| 34 | 0xd834 | runs of (u, v) per pixel | face 0x11a1e | converted to [0x107c2] (env-map coordinates of the face), freed | parts2d-b |
| 35 | 0x122ff | palette 0x300 + map 0xffff + 32 tables of 256 | face 0x119a1 + selector | face palette (fader), env map, remap tables [0x107be] | parts2d-b |
| 36–38 | 0x47c, 0x504, 0x4d8 | run lists (skip, count words, 0xffff ends) | dissolve 0x152b4.. | masks of the three pictures 39..41 | parts2d-b |
| 39–41 | 0x7bbd, 0x673b, 0x676e | 3 planes (R, G, B, 0..63) of the masked pixels | dissolve 0x152e1.. | the three women in RGB, decoded at orders 0x69, 0x6a, 0x6b | parts2d-b |
| 42 | 0x300 | palette | torso 0x16dc9 | torso palette | parts3d-b |
| 43 | 0x10000 | map: levels 0..7 (byte-identical to 21) | torso 0x16e22 + selector | `(v - 4) >> 1` to a new map, `(v - 4) << 3` in place | parts3d-b |
| 44 | 0x102ff | map 0xffff (85 colours) + 0x300 8-bit palette (not read) | torso 0x16e89 + selector | sphere map, copied every frame to [0x16d50] | parts3d-b |
| 45 | 0x2408 | `B!` object | torso 0x16d95 (0x78c62) | the torso | engine, parts3d-b |
| 46 | 0x2bd8 | `B!` object | torso 0x16da4, 0x16dbf (loaded twice) | the ring | engine, parts3d-b |
| 47 | 0xfa00 | screen: colours 0..7 | torso 0x16e42 + selector | background; every byte += 0xe6, row 0 cleared | parts3d-b |
| 48 | 0x300 | palette | never loaded | (unused) | |
| 49 | 0xffff | map 0xffff bytes (253 colours) | star 0x16529, precalc 0x169bd + selector | env map [0x5590e] of the star, palette at exe 0x15e60 | parts3d-b, parts3d-a |
| 50 | 0x1da8 | `B!` object (16 parts) | precalc 0x1699c (0x78c62) | the small spiky shapes, 64 frames pre-rendered | parts3d-a |
| 51 | 0x9388 | `B!` object | star 0x1654e (0x78c62) | the chrome star | engine, parts3d-b |
| 52–55 | 0xfd00 | picture (one shared palette) | main 0x3a2, 0x302/0x33c, 0x36f | credits: "ART RA", "3D EFFECTS & MOTION BARTI", "SYSTEM & SPECIAL EFFECTS KARL", "MUSIC GROO" | core |
| 56 | 0xfd00 | picture | main 0x408 | "NOOON 50 $" (last picture) | core |
| 57 | 0xfd00 | picture | main 0x1b5 | spiky ornament with a glowing knot (55:01–58:20) | core |
| 58 | 0xfa00 | screen: 0 / 31 | particles 0x13392 (both parts) | the words "NOOON BRING YOUR BEAUTY FAMILY" as a mask | parts2d-b |

Order of loading in the capture (`full.log`): 1; 50, 49; 5, 6, 7; 9, 8, 11, 12, 13, 14, 10; 30, 31, 33; 26, 28, 32,
27, 29; 18, 15, 16, 19, 17; 11, 12, 13; 2, 3, 4; 24, 22, 20, 23, 21, 25; 57; 49, 51; 58; 36..41; 45, 46, 46, 42,
29, 43, 47, 44; 58; 35, 34; 54, 53, 55, 52, 10, 56.

Files 29, 44 and 49 (and the map inside 35) are 0xffff bytes: one byte short of 256x256. The program reads them
through 64 KB selectors, so texel (255, 255) reads the byte after the map: 0 in 29 and 44 (the first byte of their
trailing palette), 0xf9 in 35 (the first byte of the remap tables), and in 49 the last byte of its 0x10000-byte
heap block, which DIET does not write (0xaa or older data). Files 29 and 44 carry a 0x300 palette after the map
that the program never reads (a grey ramp in 29, 8-bit values in 44).

## 14. Pictures

The remake will show redrawn high-resolution pictures by default (original pixels with `--original-art`). Below is
every picture-like file: whether it is hand-drawn art, how the program uses it, and what a true-colour redraw has
to respect. All are written to `stars/re/dump/pictures/fileNN.png` by `models/core/pictures.py` (indexed PNG with
the palette named here; level maps as a grey ramp; 39..41 as RGBA).

"As-is" means the program copies the pixels to the screen unchanged and only fades the palette uniformly
(`colour * L`, or towards white in the bee); then a true-colour redraw can replace the picture, with the same fade
applied to the redraw's colours.

### 14.1 Hand-drawn pictures shown as they are

| file | size | contents | palette | shown | use / redraw |
|---|---|---|---|---|---|
| 9 | 320x200 | "Join us" lettering (pink / green) | own | intro, order 0x00 | as-is; fade in / out by 2 per retrace over 128 retraces (0x627e), out after row 0x2e |
| 8 | 320x200 | "BRING THE FAMILY", men walking out of a magenta spiral | own | intro, order 0x01 | as-is, same fades |
| 11 | 320x200 | the woman (dress, green eye) on a magenta star | own (= 12 = 13) | intro, order 0x03; warp | intro: as-is, fade 4 per retrace over 64 (0x6251), out after row 0x30. Warp: texture, see 14.2 |
| 12 | 320x200 | the woman, second pose | own | intro, order 0x05; warp | as 11 |
| 13 | 320x200 | the woman, third pose | own | intro, order 0x07; warp | as 11 |
| 14 | 320x200 | green poodle on a grey star | own | intro, order 0x09 | as-is, fades as 8 |
| 10 | 320x200 | "STARS wonders of the world" | own | intro, order 0x0a; credits 9a:01 | as-is; credits fade in by 8 per retrace (32 steps), out by 4 |
| 57 | 320x200 | spiky purple ornament with a glowing orange knot | own | 55:01–58:20 (main) | as-is, fades 4 / -4 over 64 retraces |
| 54 | 320x200 | "SYSTEM & SPECIAL EFFECTS KARL", "L.C.A. Player" | shared by 52..55 | credits 95:01 (even minute) | as-is, fades 4 / -4 |
| 53 | 320x200 | "3D EFFECTS & MOTION BARTI" | shared | credits 96:01 | as-is |
| 55 | 320x200 | "MUSIC GROO" | shared | credits 97:01 | as-is |
| 52 | 320x200 | "ART RA", "3d Objects: Barti Xteo Imagine Autodesk" | shared | credits 98:01 | as-is |
| 56 | 320x200 | "NOOON 50 $" | own | credits 9a:21 to the end | as-is; final fade out by 2 per retrace over 128 |
| 5 | 4 x 144x36 | the words "Magic", "Atomic", "Nutritive", "Powerful" (sprites) | first 45 bytes -> DAC 1..15 | intro name cards (orders 0x02, 0x04, 0x06, 0x08) | sprite at (88, 150), colour 0 transparent (0x5dbb); drawn over the flower; a redraw needs alpha |
| 6 | 4 x 128x74 | the names "BARTi", "RA", "GROO", "KARL" (sprites) | first 48 bytes -> DAC 0x60..0x6f | intro name cards | sprite at (96, 60), colour 0 transparent (0x5df4); redraw with alpha |
| 32 | 80x36 | "23806 faces" (sprite) | 26 (bee) | bee part, bottom left, blinking | drawn into the frame buffer at (0, 163), indices < 0x50 opaque, >= 0x50 transparent (0x12102); redraw with alpha |
| 19 | 320x200 | dark purple spiral (6 colours) | 15 | spike ball, 17:01–27:00 | background: copied every frame under the objects, rows 1..197 (0x78905); palette 15 scaled by the part's fade; as-is |
| 25 | 320x200 | blue-violet cloud wisps (6 colours) | 22 (= 20 for these colours) | surfaces, 45:02–54:01 | background as 19; the 20 / 22 palette blend does not touch colours 1..6; as-is |
| 27 | 320x200 | purple / orange cloud tunnel (157 colours; looks like a still of the cloud part) | 26 with colour 0 = colour 0 of 31 | bee, 0e:20–16:03 | background as 19; the palette fades from white at the start and to black at the end; as-is |
| 47 | 320x200 | dark green landscape silhouette (8 colours) | 42, at index v + 0xe6 | torso, 6c:01–7d:00 | background as 19, after `v += 0xe6` for every byte (DAC 0xe6..0xed) and row 0 cleared; the PNG's palette is already shifted; as-is |

### 14.2 Hand-drawn pictures used through palette tricks, mapping or index arithmetic

| file | size | contents | palette | shown | use / what a redraw needs |
|---|---|---|---|---|---|
| 11, 12, 13 | 320x200 | the woman (as above) | 13's | warp 0x975b, 27:01–33:23 | converted to 256x256 textures (0x9d95): texture row r = source row floor(r * 25 / 32) (16-bit accumulator += 0xc800 per row), columns 32..287; the warp samples them through moving mappings. A redraw works as the texture if the remake samples the same region (x 32..287, all 200 rows stretched to the texture height) |
| 39, 40, 41 (+ masks 36, 37, 38) | 320x200, 3 planes | the three women again, without the star, in 18-bit RGB | none (RGB); shown through a generated 256-colour palette (0x79fad) | dissolve 0x1535c, 69:01–6b:31 | decoded at rows 0, 0x10, 0x20, 0x30 of orders 0x69 / 0x6a / 0x6b into three 64000-byte planes and blurred / zoomed per channel before the colour reduction (parts2d-b). Already true colour: a redraw must stay a picture of the same figure at the same place and size; the effect works on its R, G, B channels |
| 7 | 256x256 | flower petal: four ellipses of concentric colour bands | first 0xc0 bytes -> DAC 0x10..0x4f | intro name cards | texture drawn through a polar map (parts2d-a) while the 64 petal colours are cycled: the colour of a pixel is its band index shifted in time. A redraw has to keep the band (index) structure: a high-resolution band-index map coloured by the cycling palette, not a true-colour image |
| 58 | 320x200 | "NOOON / BRING / YOUR / BEAUTY / FAMILY" lettering, values 0 / 31 | (mask) | particles 0x133f1, 7d:01–87:00 (and loaded by 0x133ad) | read as a mask: the particles take positions / brightness from the set pixels (0x135c5, parts2d-b). A redraw would be a high-resolution mask at the same place; not shown as a picture |
| 3 (+ height map 2) | 256x256 | terrain colour map (green land, blue water, brown hills); 2 holds the heights (white = high) | 4 | tunnel 0x11e8, 35:01–45:00 | voxel renderer: for each ray step the height from 2 gives a column length, the colour comes from 3 at the same texel (0x1261). The pair looks generated by a landscape program. A redraw of 3 needs a matching height map 2 at the same resolution; palette 4 is only faded in |
| 16 | 256x256, levels 0..8 | stone / crack pattern | (levels; colours come from the shading) | spike ball | engine texture (0x79801), combined with the light by the engine's shading mode [0x5594c] = 1 (notes-engine). A redraw must be an intensity map with the same 9 levels' meaning, not a colour picture |
| 21 (= 43), 23 | 256x256, levels 0..7 (4x2 repeats of a 64x128 tile) | stone bricks with panels (21), Celtic knots (23) | (levels) | surfaces 0xa0ca; 43 in the torso | surfaces: the top-left 64x128 tile of each is copied (0xa5c5) and 6 in-between tiles are made by interpolating the indices, `a + ((b - a) * k >> 3)` for k = 1..6 (a texture morph), tiled to 256x256 (0xa57d) and shaded by the engine (mode 3). Torso: 43 becomes `(v - 4) >> 1` and `(v - 4) << 3` maps for the engine. A redraw must be an intensity map (the morph is a linear blend of levels) |
| 30 | 256x256, levels 0..15 | soft cloud blobs (probably generated) | 31 (16 x 16 combinations) | cloud 0x126ae, 0b:01–0e:1e | two copies: v and `v << 4`; the effect adds a lookup in each so a pixel is `16 a + b` and palette 31 colours every combination of two cloud layers. A redraw needs the same two-layer structure (two intensity maps and the 16 x 16 colour table) |
| 29 | 256x256, levels 0..63 | orange light flare / burst in a square | bee: 26 (colours 0..63, gold); torso: 42 at `(v >> 1) + 0x60` | bee, torso | env / shading map of the bee (direct indices into the gold ramp) and of the ring in the torso part. A redraw is an intensity map (64 levels), coloured by the ramp |
| 49 | 256x256 | glowing sphere with orange / purple filaments | exe 0x15e60 (768 bytes, the star part's palette) | star 0x164dc, 58:21–65:0a; the 64 pre-rendered small stars | engine env map (direct indices). The palette flashes towards white (0x168a3), a uniform blend. A true-colour redraw can serve as the env map if the remake's env mapping uses colours, not indices |
| 35 | 256x256 (+ palette, + 32 tables) | blue chrome sphere with a highlight | own (first 0x300 bytes) | face 0x1089c, 87:01–92:2f | env map read through the face's (u, v) map (file 34) with a scrolling offset ([0x107c6]), then every screen pixel is remapped through one of 32 tables (file 35 + 0x102ff, 0x1178c). A redraw needs special handling: the remap tables act on indices |
| 44 | 256x256 | red-brown sphere with a highlight | 42 | torso | copied every frame into the buffer [0x16d50] that the engine then draws into (0x173a1) and uses for the ring; depends on the engine's index handling (notes-parts3d-b) |

Not pictures: 2 (height map), 17 (offset map made from a 0..15 edge map), 33 (table), 34 (face coordinates), the
objects 18, 24, 28, 45, 46, 50, 51 and the palettes.

Notes for the redraws:
- The fade of every as-is picture is uniform per channel (checked for the backgrounds with `bgpal.py`: only
  `p * L >> 8`, `p * L >> 6` or the bee's fade from white occur on their colours), so a redraw in true colour with
  the same fade function is exact.
- The 3D-part backgrounds cover rows 1..197 only (0x78905 copies 0x3d90 dwords from offset 0x140); rows 0, 198,
  199 stay black.
- 11, 12, 13 and 39, 40, 41 are the same three drawings (with and without the star); one redraw per pose can serve
  the intro, the warp and the dissolving part if it keeps the figure's position.

## 15. What a high-resolution remake should evaluate exactly

- Fades: level steps once per retrace (main) or per PIT tick (parts) and is applied as an 8-bit factor to 6-bit
  colours with truncation. Evaluate `colour * L / 256` in float, with L a continuous function of time (for main:
  70.086 steps per second from the first retrace after the position; for the parts: 70.0635 per second from the
  callback install).
- Palettes: 6-bit values become v / 63.
- The PIT period 1 / 70.0635 s and the retrace period 1 / 70.086 s are different clocks: keep them apart.

## 16. Open points and corrections to NOTES.md

- NOTES.md "0x3c (restore the IRQ handlers...)": it restores IRQ 0 and the PIT rate, closes the file and unmasks the
  keyboard; the GUS IRQ is restored by the player's stop routine; the exception vectors stay.
- NOTES.md "Dual monitor support": a snake game, not a performance display (section 11). It changes the random
  sequence on machines with an MDA.
- NOTES.md "without sound the demo runs faster from there on": by about 9 % per order, because the module's orders
  have 48 rows (section 10).
- NOTES.md "0x4a006 (?)": clears PIT callback 3 when sound is on (an inverted test; no effect).
- The heap starts filled with 0xaa (RAM test). Whether any part reads unwritten memory is for the part scopes.
- Not determined: what the 0x2ff / 0x300 trailing bytes of 29 and 44 were for (they look like the palettes of the
  tool that made the maps); whether 27 was made from the cloud effect (it looks like it).
