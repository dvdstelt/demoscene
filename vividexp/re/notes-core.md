# VIVID.EXE: the core (main, archives, script, pictures, palette, zoom credits, clock)

This file covers main (0x4001a0), the file archives, the script reader and interpreter with every command, the PCX
pictures and the rectangle blitter, the palette and its fades, the zoom credits, and the clock (MIDAS's
screen-synchronised timer, the frame counter, the music position) as the demo uses them. Addresses are linear
(NOTES.md). Records are records of `full.frames` (record k = refresh period k, written at program time (k + 1) /
70.086 s).

**Verified.** `models/core/core_model.py` (decoder, blitter, palette, zoom, in Python, driven by the line times of
`full.script`) reproduces the capture: in part I's records 0..5990 (opening, texts, credits, title picture) the DAC of
all 5,991 records and the VGA memory of 5,138; the other 853 are 852 zoom records that show exactly the model's
previous zoom frame (the emulated CPU had not yet copied the new one), one zoom record caught in the middle of the
copy, and the record before the first name frame was drawn. All 2,856 zoom records are one of the two model frames
(`zoomcheck.py`). The picture sections (slides #1 900 records, slides #2 886, the winged man 441, part II's greetings
802) match in VGA and DAC except one record each where a picture decode or copy spanned the record; part II's white
flash before the first bump matches in all 14 records (DAC). See "Verification" at the end.

## Summary for the remake

- **Script semantics.** A line runs its action first and then, if it is a waiting command, waits until the music
  reaches its POS / PAT. So **what a line draws appears when the previous line's wait ends**; its own POS / PAT is
  when the next line starts. The wait ends when `order >= POS && row >= PAT` (both, every refresh), and always takes
  at least one refresh.
- **Pictures** are drawn straight into VGA memory (no back buffer), at once, without waiting for the retrace. The
  zoom renders into a 64 KB buffer and copies rows 28..171 to VGA.
- **Palette fades** are linear in 16.16 per entry, one step per screen refresh (the frame counter), applied in the
  wait loops; the DAC gets the palette *before* that refresh's step, right after the retrace.
- **Zoom credits**: a 1280 × 200 name picture flies at the camera over the sky: scale s = 100 / z, z = 900 − 3.5 per
  refresh (clamped at 1), centred on screen (160, 100) ↔ picture (640, 100), index 0 transparent, inside the window of
  rows 28..171.
- **Time**: everything in this scope counts screen refreshes (70.086 Hz) or waits for music positions. Nothing in the
  core is stepped per drawn frame, and there are no random numbers. The music sync value is never read.

## 1. Main (0x4001a0)

```c
int main(int argc, char **argv)
{
    if (argc > 2) { printf("Too many parametres!"); exit(1); }
    if (argc == 2 && strcmp(argv[1], "m") && strcmp(argv[1], "2")) { printf("Invalid parametres!"); exit(1); }
    startAtPart2 /*[0x825c44]*/ = (strcmp(argv[1], "2") == 0);
    printf("Use: 'vivid.exe m' for manual config!!!");
    minFree /*[0x82d370]*/ = 64000000;               /* lowest free memory seen, printed at the end */
    archiveInit("demo1.dat", 0, linked=[0x821e70]=1);  /* 0x402200, three times: the linked archives */
    archiveInit("demo2.dat", 1, 1);
    archiveInit("demo3.dat", 2, 1);
    MIDAS set-up 0x40d4fc; if argv[1] == "m": MIDAS's set-up menus 0x40d1f4 (and 0x410410 if no card);
    else 0x410410 (detection);
    tmrGetScrSync(&scrSync /*[0x825c60]*/);           /* 0x410ab6: PIT ticks per refresh */
    MIDAS options [0x844bf4] |= 1, &= ~2;  MIDASinit 0x40d25c;
    printf("MIDAS Sound System v0.5a ..."); printf("Using %s ...");
    puts("Loading music...");                         /* 0x410e45 */
    tmrSyncScr(scrSync, 0x40007c /*before retrace*/, 0, 0x403728 /*in retrace*/);   /* 0x410d23 */
    keyboardInstall();                                /* 0x410ee0: IRQ 1 handler, [0x8232ee] = Esc pressed */
    if (!startAtPart2) {
        scriptRead("script/demo1.scr");               /* 0x40b6e4 */
        scriptLoadModule();                           /* 0x40b5a0: line 0, "LoadXM exprmnt1.xm", from disk */
        int10h(0x13);
        MIDASplayModule(module /*[0x825c68]*/, 0);    /* 0x40d618 */
        setAll(0, 0, 0);                              /* 0x401fbc: palette black, DAC written */
        scriptRun();                                  /* 0x40b760 */
        setAll(0, 0, 0);
        MIDASstopModule(module); freeModule(module);  /* 0x40d728, 0x40dc74 */
        MIDASclose(); MIDASinit();                    /* 0x40d3c4, 0x40d25c */
        tmrSyncScr(scrSync, 0x40007c, 0, 0x403728);   /* restarted: one refresh count is lost here */
    }
    scriptRead("script/demo2.scr"); scriptLoadModule();   /* "LoadXM exprmnt2.xm" */
    if (startAtPart2) int10h(0x13);                   /* no palette is set before part II's script */
    MIDASplayModule(module, 0);
    scriptRun();
    keyboardRemove();                                 /* 0x410f31 */
    int10h(3);
    MIDASstopModule; freeModule; MIDASclose; archiveFree();   /* 0x402354 */
    printf("VIVID v1.68 ... DOOMSDAY!"); printf("Minfree:%d", minFree);
}
```

- Part I ends on the line `ClearSCR` (VGA all 0); main then sets the palette black, so the screen is black from
  record 14447 until part II draws. Part II's script ends with the greetings picture on screen (24:63); main goes to
  text mode at once.
- Part II started with `vivid.exe 2`: mode 13h is set and the BIOS palette stays until the water sets its own.
- The keyboard handler (0x410e80) stores key states at 0x82326e + scan code and sets [0x8232ee] = 1 for Esc. Every
  wait loop and the interpreter stop when [0x8232ee] != 0; the flag is never cleared, so Esc ends the whole demo.
- 0x400010 (error with two strings: text mode, module freed, MIDAS closed, "DEMO ERROR:%s%s", exit 1) is what every
  "not found" in the core calls. 0x400088 / 0x4000b8 are other error exits.

## 2. The archives

```c
struct Entry { char name[32]; uint32 size; uint32 offset; };   /* 40 bytes, 128 per table, ended by "eof.eof" */
uint8 *base[3];      /* [0x82d2d0 + 4n] */
Entry  table[3][128];/* copied to 0x8282d0 + n * 0x1400 */
uint32 lastSize;     /* [0x82d2f4] */

void archiveInit(const char *diskName, int n, int linked)     /* 0x402200 */
{
    if (!linked) {   /* not taken in the release ([0x821e70] = 1) */
        f = fopen(diskName, "rb"); if (!f) { if (n == 0) { printf("%s not found!"); exit(1); } }
        else { read the whole file into malloc'd memory; base[n] = it; }
    } else {
        0x42b25c(ptr, size): ptr = {0x42b2a8, 0x60528d, 0x7979fe}; base[n] = ptr[n];
    }
    if (base[n]) memcpy(table[n], base[n], 0x1400);
}

uint8 *archiveFind(const char *name, int n)        /* 0x402398 */
{
    uint8 *hit = 0;
    for (Entry *e = table[n]; strcmp(e->name, "eof.eof") != 0; e++)   /* no 128 limit */
        if (strcmp(e->name, name) == 0) { hit = base[n] + e->offset; lastSize = e->size; }   /* no break */
    return hit;
}

uint8 *fileFind(const char *name)                  /* 0x40241c */
{
    uint8 *p = 0, *q;
    if ((q = archiveFind(name, 0))) p = q;
    if ((q = archiveFind(name, 1))) p = q;
    if ((q = archiveFind(name, 2))) p = q;         /* the last archive that has it wins */
    return p;                                      /* 0 = not found; lastSize = size of the last match */
}
```

`strcmp` (0x40f870) is case sensitive. 0x402354 frees the archive memory only when it was read from disk.

**All three archives are searched for every name, in both parts.** Part II's `gen2/gen.ini`, `pal/greets.pal` and
`pal/phong2.pal` are in archive 3 and are found (the capture runs `GenericInit gen2/gen.ini`, `InitFADE phong2.pal
white.pal` and `InitFADE white.pal greets.pal` without error). Names in more than one archive (`pics/plo2.raw`,
`plo5.raw`, `plo6.raw`, `pal/black.pal`, `pal/white.pal`) are byte-identical, so the order does not matter. Nothing in
the release is "not found".

## 3. The script

### Reading (0x40b6e4) and the module line (0x40b5a0)

```c
char line[512][128];   /* 0x834bac */
int  lineCount;        /* [0x844bac] */

void scriptRead(const char *name)                   /* 0x40b6e4 */
{
    uint8 *d = fileFind(name); if (!d) error(name, " SCRIPT not found!");
    lineCount = 0; int col = 0, row = 0;
    for (int i = 0; i < lastSize; i++) {
        if (d[i] != '\r') line[row][col++] = d[i];
        else { line[row][col] = 0; i++; /* skip the '\n' */ lineCount++; row++; col = 0; }
    }   /* a last line without CR is not counted; no bounds checks */
}

void scriptLoadModule(void)                         /* 0x40b5a0 */
{
    sscanf(line[0], "%s %s %s %s", pos, pat, cmd, file);
    if (!strcmp(cmd, "LoadXM"))  xmLoadModule(file, 1, 0, &module);    /* 0x418d44, from disk */
    if (!strcmp(cmd, "LoadS3M")) s3mLoadModule(file, 1, 0, &module);   /* 0x41a24c */
}
```

`demo1.scr` has 219 lines (0..218), `demo2.scr` 80; both end with CR LF and have no empty lines.

### The interpreter (0x40b760)

```c
void scriptRun(void)
{
    char a[64] /*S+0*/, cmd[64] /*S+0x40*/, pal1[96] /*S+0x80*/, posS[32] /*S+0xa0*/, patS[32] /*S+0xc0*/, pal2[32] /*S+0xe0*/;
    for (int i = 1; i < lineCount; i++) {            /* line 0 (the module) is skipped */
        if (line[i][0] == '#') goto next;            /* only column 0 counts */
        sscanf(line[i], "%s %s %s %s", posS, patS, cmd, a);   /* fields not present keep the previous line's text */
        int pos = 999, pat = 999;
        if (strcmp(posS, "xxx")) pos = atoi(posS);   /* atoi 0x41b0c1: decimal, "010" = 10 */
        if (strcmp(patS, "xxx")) pat = atoi(patS);
        /* then every command name is compared in turn (not else-if); the matching one runs: */
        ...table below...
    next:
        minFree = min(minFree, dpmiFreeMemory());    /* 0x402730 (int 31h 0500h) */
        if (escape) break;
    }
}
```

Every command re-parses the line with its own format; **positions passed to the waiting routines are 16 bits** (the
routines compare them zero-extended).

| command | format after POS PAT CMD | calls | meaning |
|---|---|---|---|
| `ScriptJMP` | `%s` | — | `i = atoi(arg) - 1`: continue at line `arg` (line numbers count from 0, comments included). Unused |
| `WaitSYNC` | (ignored) | 0x40279c(pos, pat) | wait for the position (below). `xxx` would wait forever. The trailing `0` is ignored |
| `ClearSCR` | — | 0x402784 | VGA 0xa0000..0xaf9ff = 0 (64,000 bytes) |
| `LoadPAL` | `%s file` | 0x402054(file) | current palette = file, DAC written at once (palette section) |
| `InitFADE` | `%s %s %d %d %d` pal1 pal2 steps first last | 0x4020b8 | set up a fade; never waits, whatever POS / PAT say |
| `NegPAL` | — | 0x402000 | palette = 63 − palette, DAC written. Unused |
| `InitPCX` | `%d slot %s file %d flag` | 0x401884(slot, file, flag) | decode a PCX into slot; flag 0 = also set the palette from the file |
| `DeinitPCX` | `%d slot` | 0x401c20(slot) | free the slot's picture |
| `PutPCXSYNC` | `%d slot` + 7 × `%d` | 0x401c2c | draw a rectangle of the slot's picture, then wait unless POS is `xxx` |
| `InitZOOM` | `%s file %d flag` | 0x402460(file, flag) | the zoom buffer; the sky picture into slot 0 |
| `RunZOOMSYNC` | `%s file %f %f %f %d` | 0x402644 | the zooming name, until the position |
| `DeinitZOOM` | — | 0x402630 | free slot 0 and the buffer |
| `InitBITMAPEFEK` | `%s file %d` | 0x400ae4(file, int) | notes-bitmap.md |
| `RunBITMAPEFEK` | `%s file` | 0x401270(pos, pat, file) | notes-bitmap.md |
| `DeinitBITMAPEFEK`, `ClearEFEKPARAMS` | — | 0x400ff0, 0x401588 | notes-bitmap.md |
| `InitBUMP` | `%s file %d` | 0x402ec4(file, int) | notes-bump.md |
| `RunBUMPEFEK` | (ignored) | 0x403434(pos, pat) | the script's `1` / `0` after it is not read |
| `DeinitBUMP` | — | 0x40315c | |
| `WaterInit` | `%d int %s file` | 0x405bd0(int, file) | non-zero result: error " WaterINIT ERROR!" |
| `WaterEffect`, `GenericEffect` | `%s %s` (pos, pat only) | 0x4064b0 / 0x409d74(atoi(pos), atoi(pat)) | positions through 0x41b12e (atoi; `xxx` would give 0); the trailing `0` is not read |
| `GenericInit` | `%d int %s file` | 0x408cec(eax = &errorText, edx = int, ebx = file) | non-zero result: exits with the text it returns |
| `SwapMOD_XM` / `SwapMOD_S3M` | `%s file` | in line | stop and free the module, load `file` from disk, MIDASplayModule(module, 0). Unused |
| `TunnelEffect`, `EarthEffect` | `%d %s` | 0x404a64 / 0x406c08(pos, pat, int, file) | left over, unused |
| `DogInit` / `DogEffect` | `%d %s` / pos pat | 0x403ab4 / 0x404220 | left over, unused |

#### PutPCXSYNC's numbers (0x401c2c)

`POS PAT PutPCXSYNC slot dx dy sx sy w h mode`, e.g. `031 023 PutPCXSYNC 011 137 26 137 26 112 24 0`:

| number | meaning |
|---|---|
| slot | picture slot (10, 11 or 12 in the scripts; `%d`, so `010` = 10) |
| dx, dy | destination: top-left screen pixel |
| sx, sy | source: top-left pixel in the picture |
| w, h | rectangle size |
| mode | 0 copy; 1 copy except index 0 (index 0 transparent); 2 `dst += src` (8-bit wrap); 10..16 `dst += src << (mode − 9)` (8-bit wrap; the shifted value is truncated to 8 bits). Other values draw nothing |

```c
void PutPCXSYNC(int slot, uint16 pos, uint16 pat, int dx, int dy, int sx, int sy, int w, int h, int mode)
{
    if (dx + w > 320) w = 320 - dx;                 /* clipped right and bottom only */
    if (dy + h > 200) h = 200 - dy;
    if (h > 0)
        for (int r = 0; r < h; r++) {
            uint8 *s = pic[slot] + (sy + r) * picWidth + sx;   /* picWidth [0x826ab4]: ONE global, see InitPCX */
            uint8 *d = (uint8 *)0xa0000 + (dy + r) * 320 + dx;
            per byte by mode (above)
        }
    if (pos != 999) wait as WaitSYNC (but the reference counter is read after drawing)
}
```

The same blitter without the wait exists at 0x401a04 and is never called.

#### The waits

```c
void WaitSYNC(uint16 pos, uint16 pat)             /* 0x40279c; PutPCXSYNC and RunZOOMSYNC end the same way */
{
    int last = frames;                            /* [0x825c5c] */
    readPosition();                               /* 0x400104 */
    do {
        while (frames == last) ;                  /* at least one refresh, even if the position is already past */
        if (fadeActive) fadeStep(frames - last);  /* 0x401ed0 */
        last = frames;
        readPosition();                           /* [0x825c48] order, [0x825c50] row (MIDAS's live values) */
        if (escape) return;
    } while (pos > order || pat > row);           /* unsigned compares */
}
```

- The condition is `order >= POS && row >= PAT`, tested once per refresh. A target row is therefore waited for in
  every order from POS on: both scripts' `034 063 WaitSYNC` after the slides (part I lines 144 and 184) end at row
  63 of the current order (34:63 the first time, **46:63** the second time, 45 refreshes after 46:50), not at once.
- POS `xxx` (999): PutPCXSYNC does not wait; WaitSYNC would wait forever.
- Commands that do not wait (`InitFADE`, `LoadPAL`, `InitPCX`, ...) run back to back in the same refresh; only the
  real work (a PCX decode takes most of a refresh in the capture) lets time pass.

#### Timing quirks in the scripts that the remake keeps

- `InitFADE pal/taivas.pal pal/white.pal 45` before `021 000 PutPCXSYNC` (part I line 93): the wait ends after 6
  refreshes, so only 6 of the 45 steps happen; the next line restarts from `white.pal` (an abrupt jump to white,
  record 5514), then fades to the title over 150 steps.
- `021 000 PutPCXSYNC 011 5 10 0 170 320 14 0` copies rows 170..183 of `alkutext.pcx`, which are empty: it erases the
  "music" label. The word "called" in `alkutext.pcx` (rows 53..71) is never shown.
- `PutPCXSYNC 011 5 88 0 0 310 21 14` takes rows 0..20 of "a doomsday production", whose letters reach row 23: the
  descenders of y and p are cut off 3 rows short on screen.
- `PutPCXSYNC 011 5 10 0 105 320 14 0`: 320 wide at x = 5 is clipped to 315.

## 4. Pictures (0x401884 InitPCX, 0x401c20 DeinitPCX)

```c
uint8 *pcxData[64] /*0x8268b0*/, *pic[64] /*0x8269b0*/;
int picWidth /*[0x826ab4]*/, picHeight /*[0x826ab8]*/;      /* of the LAST picture decoded, for all slots */

void InitPCX(int slot, const char *name, int flag)
{
    uint8 *d = pcxData[slot] = fileFind(name); if (!d) error(name, " not found! (PCX)");
    int w = picWidth  = u16(d + 8)  + 1;            /* xmax + 1; xmin, ymin and bytes-per-line are ignored */
    int h = picHeight = u16(d + 10) + 1;
    pic[slot] = malloc((w + 2) * (h + 2));          /* "Out of mem! (PCX)" */
    int p = 128, i = 0;
    while (i < w * h) {
        uint8 b = d[p++];
        if ((b & 0xc0) == 0xc0) { memset(pic[slot] + i, d[p++], b & 0x3f); i += b & 0x3f; }   /* may run past w*h */
        else pic[slot][i++] = b;
    }
    if (flag == 0) {                                /* the file's palette: 8-bit components >> 2 */
        uint8 *q = d + p + 1;                       /* skips the 0x0c marker after the image data */
        for (k = 0..255) cur[k] = { (q[3k] >> 2) << 16, (q[3k+1] >> 2) << 16, (q[3k+2] >> 2) << 16 };
        fadeStep(1);                                /* DAC written now; an active fade also steps once */
    }
}
```

- **The flag**: 0 = set the palette from the PCX file at once; 1 = leave the palette alone (the scripts use .pal
  files and fades). Only `synknai2.pcx` and `synknai4.pcx` are loaded with 0; their PCX palettes equal
  `synknai2.pal` / `synknai4.pal`. The palette switches when the picture is decoded, one line before it is drawn: in
  the capture both land in the same record (8215, 10886); on another CPU the old picture could show for one refresh
  in the new colours. The remake switches both together.
- All PCX files of the demo are 8-bit, one plane, bytes-per-line = width (all widths even), so the decoder's
  shortcuts do not matter; the picture's own palette follows the data directly (checked for every file).
- **picWidth is one global**: PutPCXSYNC addresses every slot with the width of the last decoded picture. The
  scripts always decode a 320-wide picture last before a PutPCXSYNC (the 1280-wide names are decoded inside
  RunZOOMSYNC, and every later PutPCXSYNC follows an `InitPCX` of a 320-wide picture), so it has no effect.
- DeinitPCX 0x401c20: `free(pic[slot])`.

## 5. The palette (0x401ed0, 0x401fbc, 0x402000, 0x402054, 0x4020b8)

```c
int32 cur[256][3];    /* 0x826abc, 16.16: 6-bit VGA value << 16 */
int32 delta[256][3];  /* 0x8276bc */
int fadeActive /*[0x8282bc]*/, count /*[0x8282c0]*/, steps /*[0x8282c4]*/, first /*[0x8282c8]*/, last /*[0x8282cc]*/;

void fadeStep(int n)                               /* 0x401ed0 */
{
    outb(0x3c8, 0);
    for (k = 0..255) for (c = 0..2) outb(0x3c9, cur[k][c] >> 16);   /* all 256 entries, BEFORE stepping */
    for (int i = 0; i < n; i++) {
        if (count < steps) count++; else fadeActive = 0;
        if (fadeActive == 1)
            for (k = first; k < last - first + 1; k++)   /* bound bug: right only for first == 0 (all scripts) */
                cur[k] += delta[k];                      /* R, G, B */
    }
}

void InitFADE(pal1, pal2, int steps_, int first_, int last_)   /* 0x4020b8 */
{
    p1 = fileFind(pal1) ("not found! (PAL1)"); p2 = fileFind(pal2) ("not found! (PAL2)");
    first = first_; last = last_;
    for (k = first; k < last - first + 1; k++) for (c = 0..2) {
        delta[k][c] = ((p2[k][c] - p1[k][c]) << 16) / steps_;   /* idiv: truncates toward zero */
        cur[k][c]   = p1[k][c] << 16;
    }
    steps = steps_; count = 0; fadeActive = 1;     /* the DAC is NOT written here */
}

void LoadPAL(file)    /* 0x402054 */ { cur = file << 16; fadeStep(1); }      /* "not found! (PAL)" */
void setAll(r, g, b)  /* 0x401fbc */ { cur[all] = {r, g, b} << 16; fadeStep(1); }
void NegPAL(void)     /* 0x402000 */ { cur = 0x3f0000 - cur; fadeStep(1); }
```

**What the DAC shows.** The fade is stepped by the wait loops (WaitSYNC, PutPCXSYNC, RunZOOMSYNC; the bitmap and
bump effects call fadeStep themselves; water and generic do not) with the number of refreshes since their previous
call, right after the retrace. Each call first writes the palette as it was, then steps. So after `InitFADE` the
DAC shows, at the j-th retrace a wait loop sees (j = 1, 2, ...):

    DAC(j) = (p1 << 16 + min(j − 1, steps) · delta) >> 16          (6-bit, arithmetic shift)

The first refresh shows p1, the fade reaches its end at retrace steps + 1, and the next call clears `fadeActive`
(from then on the wait loops no longer write the DAC). Retraces that pass while the main line is busy (a PCX decode)
are lost for the fade when the following command reads its reference counter after that work: in the capture this
delays a fade's start by one or two refreshes (the winged man's fade starts at record 14007, two refreshes after its
`InitFADE`).

- Increasing components with a remainder (`(p2 − p1) · 65536` not divisible by steps) end one 6-bit step short:
  black → `taivas.pal` over 400 steps leaves 63 at 62. Decreasing components reach p2 exactly. Steps 2 and 4 are
  exact. The fade stays where it ended until the next `LoadPAL` / `InitFADE`.
- The DAC writes happen in the vertical blank (the wait loops run right after the retrace), so a palette change
  takes effect from the start of a refresh: a fade is "the palette of refresh j".
- `LoadPAL`, `InitPCX` with flag 0 and `setAll` write the DAC at once and also step an active fade once.

## 6. The zoom credits (0x402460, 0x402644, 0x402494, 0x40ccb8, 0x402630)

What it draws: over the sky (`taivas.pcx`), a name from a 1280 × 200 picture comes from the distance and flies past
the camera, getting larger hyperbolically, between the letterbox bands. Six names: `doom.pcx` and `mri.pcx` (code),
`dice.pcx` and `damac.pcx` (graphics), `swallow.pcx` and `wode.pcx` (music), part I lines 58, 62, 71, 75, 84, 88,
records 2611..5505.

```c
void InitZOOM(const char *file, int flag)           /* 0x402460 */
{
    zbuf = malloc(65536);                           /* [0x82d308], "Out of mem! (ZOOM)" */
    InitPCX(0, file, flag);                         /* the sky into slot 0 */
}   /* the script gives no flag ("InitZOOM pics/taivas.pcx"): %d fails and the flag is stale stack data,
       non-zero in the capture (0x847f54), so the palette is not set; taivas.pcx's palette equals taivas.pal anyway */

void RunZOOMSYNC(uint16 pos, uint16 pat, const char *file, float z0, float a, float dz, int b)   /* 0x402644 */
{                                                   /* script: 900 200 -3.5 28 */
    InitPCX(1, file, 1);                            /* the name into slot 1 (1280 x 200) */
    z /*[0x82d32c], float*/ = z0;
    int last = frames; readPosition();
    do {
        zoomFrame(a, b);                            /* 0x402494: draw with the current z */
        while (frames == last) ;
        for (int i = 0; i < frames - last; i++) { z += dz; if (z < 1.0f) z = 1.0f; }   /* per refresh */
        if (fadeActive) fadeStep(frames - last);
        last = frames; readPosition();
        if (escape) break;
    } while (pos > order || pat > row);
    DeinitPCX(1);
}

void zoomFrame(float a, int b)                      /* 0x402494, x87 (53-bit precision, CW 0x127f) */
{
    x0 = trunc(160 - (a * 160 * 2) * (1 / z));      /* left edge;  trunc = 0x41130c, chop */
    w  = 2 * (160 - x0);
    y0 = trunc(100 - (a * 100 * 0.5) * (1 / z));    /* top edge */
    h  = 2 * (100 - y0);
    du = trunc((1280.0 / w) * 65536);               /* 16.16 source step per screen pixel */
    dv = trunc((200.0 / h) * 65536);
    u0 = v0 = 0;
    if (x0 < 0) { u0 = (-x0 * du) >> 16; x0 = 0; w = 320; }          /* first visible source column */
    if (b > y0) { v0 = ((b - y0) * dv) >> 16; y0 = b; h = 200 - 2 * b; }   /* clipped to the window */
    memcpy(zbuf, pic[0], 64000);                    /* the sky, every frame */
    zoomBlit(zbuf, pic[1], {x0, y0, w, h, u0, v0, du, dv});    /* 0x40ccb8 */
    memcpy(0xa0000 + b * 320, zbuf + b * 320, (200 - 2 * b) * 320);   /* rows b .. 199 - b only */
}

void zoomBlit(uint8 *dst, uint8 *src, P p)          /* 0x40ccb8, assembler, self-modifying addresses */
{
    uint8 *o = dst + 320 * p.y0 + p.x0;
    uint32 V = p.v0 << 16;
    for (int r = 0; r < p.h; r++) {
        uint32 U = p.u0 << 16;                      /* the fraction of the start is dropped */
        uint8 *row = src + (V >> 16) * 1280;        /* stride 1280 hard-coded */
        for (int c = 0; c < p.w; c++) { uint8 t = row[U >> 16]; if (t) *o = t; o++; U += p.du; }
        V += p.dv; o += 320 - p.w;
    }   /* h <= 0 would loop 2^32 times; it never happens with these arguments */
}

void DeinitZOOM(void) { DeinitPCX(0); free(zbuf); }   /* 0x402630 */
```

**The arguments** (`RunZOOMSYNC pics/doom.pcx 900 200 -3.5 28`): z0 = 900 the starting distance, a = 200 the size,
dz = −3.5 the change of z per refresh, b = 28 the rows of each letterbox band (the window is rows 28..171).
**Continuous form for the remake** (the original's integer rectangle and 16.16 stepping approximate exactly this):

    n = refreshes since the line started (frame counter − its value at the line's start)
    z(n) = max(900 − 3.5 n, 1)                      (exact in float32)
    s(n) = a / (2 z) = 100 / z                      (screen pixels per picture pixel)
    picture texel (u, v) covers screen [160 + (u − 640) s, 160 + (u + 1 − 640) s) × [100 + (v − 100) s, ...)

The sky fills rows 28..171 of the window; name pixels with index 0 are transparent; other indices are drawn
opaquely over the sky; the window is clipped to rows 28..171 and columns 0..319; rows 0..27 and 172..199 are not
touched (they keep the sky picture's black bands and the label). z reaches 1 after 257 refreshes (3.67 s); s goes
from 0.111 (the name 142 × 22 pixels) through 1 (z = 100, after 229 refreshes) to 100. The six names lie off the
picture's centre row (doom rows 99..134, mri 58..93, dice 102..138, damac 55..91, swallow 108..144, wode 56..92; all
centred around column 640), so each one flies past below or above the camera and leaves the window; after that the
window shows the sky only until the next flash. The picture's centre texels are transparent in all six.

**Frame by frame** (verified, `zoomcheck.py`): the first frame (z = 900) is drawn when the line starts, each later
frame after a retrace with z for that refresh. The frame drawn after retrace k is in VGA by record k when the CPU is
fast enough; in the capture 2,003 of 2,856 records show it, 852 still show the previous frame (from record 2726 of
the first zoom, as the name grows: the emulated 100 MIPS CPU needs more than the 37 lines between retrace and
display to draw), one shows the copy half done. The remake draws z(n) for the refresh shown.

**The credits sequence** (identical for each pair of names):

| line (part I) | records | what happens |
|---|---|---|
| `InitPCX 011 alkutext.pcx 1`, `InitZOOM pics/taivas.pcx` | 2603, 2604 | the label picture; sky into slot 0 |
| `PutPCXSYNC 011 5 10 0 89 49 14 1` | 2604 | label "coding" at (5, 10), index 0 transparent (graphics: `0 10 0 74 320 14 0` puts "graphics" at x = 248..308; music: `5 10 0 105 320 14 0`) |
| `InitFADE taivas.pal taivas4.pal 4`, `009 000 WaitSYNC` | 2604..2611 | white flash: DAC steps 0..4 at records 2604..2608 |
| `InitFADE taivas4.pal taivas.pal 30`, `010 062 RunZOOMSYNC doom.pcx ...` | 2611..3087 | the flash fades back over 30 refreshes while DOOM flies in |
| `InitFADE taivas.pal taivas4.pal 4`, `011 000 WaitSYNC`, `InitFADE ... 30`, `012 062 RunZOOMSYNC mri.pcx` | 3087..3571 | flash, MRI |

The previous text section's sky stays outside the window; the zoom's first frame overwrites the window (the "at
assembly" text is not seen again: during the flash all indices but 0 are white).

## 7. Core-drawn sections and what they draw

All of these draw **straight into VGA memory** with no back buffer and no retrace wait before drawing (in practice
they run right after a wait ended, i.e. in the vertical blank; a full-screen copy takes longer than the blank on a
1996 PC, so the first refresh of a new picture may tear). The zoom renders into its buffer and copies 46,080 bytes.

| section (part I unless noted) | records | positions | drawn | overwrites |
|---|---|---|---|---|
| sky fade-in | 23..918 | 0:00..2:00 | `taivas.pcx` full screen (`PutPCXSYNC 010 0 0 0 0 320 200 0`, again at 1:15); `LoadPAL black`, fade black → taivas 400 steps; flash taivas → taivas4 2 steps at 1:00, back over 30 at 1:02 | every pixel |
| three texts | 919..2602 | 2:00..8:62 | sky again, then a text rectangle added with mode 14 (`dst += src << 5`); fade taivas → taivas2 200 steps (text in), at the next order taivas2 → taivas 200 (text out) | full screen (sky), then the rectangle |
| credits | 2603..5505 | 8:62..20:62 | label rectangle in the top band; zoom window rows 28..171 | the window every refresh; the bands keep their content |
| end of credits | 5505..5511 | 20:62..21:00 | label band cleared (empty rectangle); fade taivas → white cut after 6 steps | the band only |
| title | 5512..5990 | 21:00..23:00 | `vivid.pcx` full screen; fade white → vivid.pal 150 steps | every pixel |
| slides #1 | 7758..8658 | 30:60..34:63 | `synknai1.pcx` full screen at once (fade white → synknai1 20 steps); captions from `synktxt1.pcx` copied opaquely to the same place (mode 0, source = destination) at 31:15, 31:23, 31:39, 31:47; at 32:63 `synknai2.pcx` (palette from the file) and its captions at 33:08, 33:15, 33:23, 33:39; at 34:50 fade synknai2 → black 25 steps; wait to 34:63 | every pixel, then rectangles |
| slides #2 | 10443..11329 | 43:00..46:63 | the same with `synknai3` / `synktxt3` (43:15 ...), `synknai4` / `synktxt4` (45:08 ...); fade to black at 46:50; the copied `034 063 WaitSYNC` waits to 46:63 | as above |
| winged man | 14005..14446 | 59:00..60:63 | `ClearSCR`, `siipiman.pcx` full screen, fade white → siipiman 250 steps; at 60:63 `ClearSCR` | every pixel |
| part II: flash before SUOMI BUMP | 17151..17168 | 4:63..5:01 | nothing drawn (the water's last frame stays); fade watback → white 2 steps, then `InitFADE white phong 50` and the bump | — |
| part II: greetings | 26888..27689 | 23:00..24:63 | `greets.pcx` (slot 12) full screen, fade white → greets.pal 130 steps; the demo ends at 24:63 with it on screen | every pixel |

The caption rectangles (slides), in screen = picture coordinates, are the 7 numbers of each PutPCXSYNC line in
`demo1.scr` lines 128..141 and 168..181 (read them from the script at run time).

**How the text over the sky is made (mode 14).** `taivas.pcx` uses indices 0..31 (0 only in the bands: rows 0..27
and 172..199 are all 0, no 0 inside the sky). `alkutext.pcx` uses 0 and 129, 130, 131 (three anti-aliasing levels of
white text). `(129..131) << 5` in 8 bits = 32, 64, 96, so a text pixel becomes `sky + 32 · level`. In `taivas.pal`
entries 32..127 repeat 0..31 (the text is invisible); in `taivas2.pal` entries 32..63 are grey (20, 20, 20), 64..95
(41, 41, 41), 96..127 white (63, 63, 63). The 200-step fade taivas → taivas2 is therefore a cross-fade of each text
pixel from the sky colour under it to its grey level:

    colour = lerp(sky, grey(level), f),  grey = 20/63, 41/63, 63/63 for level 1, 2, 3;  f = fade progress

Outside the text both palettes are equal. `taivas4.pal` is white everywhere except entry 0 (black): the flash whitens
the sky and the text but keeps the bands black. `taivas2.pal` also turns 128..130 white (no effect: nothing shows
those indices during its fades).

## 8. The pictures

| file (archive) | size | kind | palette | use |
|---|---|---|---|---|
| `pics/taivas.pcx` (1) | 320 × 200, indices 0..31 | a cloudy evening sky in purples (looks like a photo, 32 colours), with black bands (index 0) in rows 0..27 and 172..199 | `taivas.pal` (= its PCX palette), `taivas2.pal`, `taivas4.pal` | background of the opening, the texts and the zoom; its indices are read through the palette tricks above, so a redraw has to keep "sky colour + text level" separable |
| `pics/alkutext.pcx` (1) | 320 × 200, indices 0, 129..131 | drawn text, white serif bold letters, anti-aliased against black in 3 levels: "a doomsday production" (rows 0..23), "in the year 1996" (26..49), "called" (53..71, unused), "graphics" (75..87, x 248..308), "coding" (90..102), "music" (106..115), "at assembly" (125..148) | through taivas*.pal | the three texts (mode 14) and the labels (modes 0 / 1) |
| `pics/doom.pcx`, `mri.pcx`, `dice.pcx`, `damac.pcx`, `swallow.pcx`, `wode.pcx` (1) | 1280 × 200, indices 0, 129..131 | drawn text: DOOM, MRI, DICE, DAMAC, SWALLOW, WODE in white serif capitals (the same typeface as `alkutext.pcx`) with 2 darker anti-aliasing levels, each centred on column 640 and lying above or below the centre row | taivas.pal: 129 = grey 20, 130 = grey 41, 131 = white | the zoom; index 0 transparent; the grey edge pixels are drawn opaquely (a dark fringe against the sky, which grows into large grey blocks at the end of each zoom) |
| `pics/vivid.pcx` (1) | 320 × 200, indices 6..93 | photo-like picture of a person (a woman, kneeling) on a chequered floor, with the title "Vivid Experiment" | `vivid.pal` (= its PCX palette) | the title, full screen |
| `pics/synknai1..4.pcx` (1) | 320 × 200, ~200..250 indices, background 255 | drawn (airbrushed) pictures of women | `synknai1..4.pal` (= their PCX palettes) | the slides, full screen |
| `pics/synktxt1..4.pcx` (1) | 320 × 200, 13..22 indices, background 255 | drawn captions: black calligraphic script on white, four lines each; they quote song lyrics (not transcribed here) | the synknai palettes | copied rectangle by rectangle (mode 0) at the positions the script gives |
| `pics/siipiman.pcx` (1) | 320 × 200, 254 indices | painting of a winged man against a yellow sky | `siipiman.pal` | end of part I, full screen |
| `pics/greets.pcx` (2) | 320 × 200, indices 0..63 | photo of two women (left) and a list of group names (right, drawn text) | `pal/greets.pal` (3) | end of part II, full screen |

None is read through tables or mapped on geometry. Photos of people (vivid, greets) are not to be redrawn unless the
maintainer asks; the sky looks like a photo of clouds (no people).

## 9. The clock

### MIDAS's timer as the demo uses it (0x410740..0x410f00)

- `tmrGetScrSync` 0x410ab6: measures the PIT count between two retrace starts (PIT mode 3, counted twice until two
  measurements agree within ±2) → [0x825c60].
- `tmrSyncScr` 0x410d23(sync, preVR = 0x40007c, immVR = 0, inVR = 0x403728): the screen interrupt comes after
  `(sync · 950 / 1000) >> 1` PIT clocks (mode 0), i.e. at 95 % of the refresh, about 22 lines before the retrace; the
  remaining 5 % is added to the chained BIOS clock. The music player's ticks are scheduled on the same PIT
  (0x41074d picks whichever comes first; notes-music.md).
- The interrupt 0x410802, screen branch: wait until the retrace is *not* active; call **preVR 0x40007c**
  (`frames++`, [0x825c5c]); wait for the **retrace start**; reprogram the PIT; EOI; `sti`; call **inVR 0x403728**
  (calls [0x832504] if a part set it with 0x403738: water, generic, tunnel, dog, earth do; the core does not).
- So the frame counter changes about 22 lines before the retrace, and the main line resumes at the retrace start.
  Every wait loop and the zoom wake up there: their DAC writes and the first drawing happen in the vertical blank.

### Program time, refreshes, the frame counter, the music position (from `full.ticks`, `full.script`)

- Refresh period p = program time [p, p + 1) / 70.086 s; in the emulator line 0 (first displayed line) is at p /
  70.086, the retrace at line 412 of 449. Record p is the screen at (p + 1) / 70.086 s, i.e. what is displayed
  during period p + 1. Script lines that run after a wait ends are at line ~412..423 of their period (`full.script`
  times), so their drawing and DAC writes are in record p.
- Frame counter: 0 until the timer starts (record 4); after the retrace of period p it is **p − 4** in part I, and
  **p − 5** in part II of the full run (the timer restarted between the parts misses one count, at record 14448). In
  `part2.*` alone it is p − 4. Records with a refresh but no count: none other.
- Music position (the demo's copy [0x825c48] / [0x825c50]) changes only when a wait loop or effect calls 0x400104;
  MIDAS's own values (the player's info struct, `[[0x844bfc] + 0x4c]` + 0 / + 8) change at the player's tick. In a
  wait loop the demo sees a new row at the first retrace after the player's tick: **0.115 .. 0.999 refreshes later
  (median 0.58)** over 76 row changes measured in the first zoom (`rowdelay.py` on a run logging the live position
  after every interrupt, `run_vivid_pos.py`). The phase cycles because the player's tick (13.44 ms in orders 1..22)
  and the refresh (14.27 ms) are independent.
- Hence: **a line after a wait starts at the first retrace after the player reaches its position**, and draws in that
  refresh's vertical blank. Effects poll once per drawn frame, so lines after them can start later (their notes give
  the frame rates). `WaitSYNC` and the other waits always last at least one refresh.
- Measured order starts (first record whose polled position is order:0): part I order 1 at 677, then every 241..242
  refreshes up to order 22 (5754); orders 23..60 every 222..223 (23 at 5991, 60 at 14228). Part II in the full run:
  order 0 polled at 14465, order 1 at 15005, then 537..540 refreshes per order up to 24 at 27425 (part2.*: order 1
  at 561, order 24 at 12981; the same spacing, part II is reproducible on its own). Order 0 of part I is longer
  (rows every ~10.06 refreshes, row 1 at record 43). The player's exact tick times are in notes-music.md.
- Part I's module starts during period 23 (program time 0.336 s; mode 13h, `MIDASplayModule`, then the first line
  `LoadPAL` at 0.3358 s); part II's at period 14459 (12 refreshes after part I's last line: module freed, MIDAS
  closed and reopened, EXPRMNT2.XM loaded from the (instant) emulated disk).
- [0x825c54] (MIDAS's sync value) is written by 0x400104 and read nowhere.

### What the remake should take as time

- One clock per part: the music player's time t since the module start; the music position comes from the player.
- A virtual 70.086 Hz refresh grid R(t) = floor(t · 70.086 + φ) for everything the original counts in refreshes
  (fade steps, the zoom's z, the bitmap / bump effects' timers). φ (the retrace phase at the module start) is
  arbitrary on real hardware; take the capture's for frame-exact comparison: the retrace of period p is at program
  time (p + 412/449) / 70.086 s.
- A line with POS / PAT ends at the first grid point after the player reaches the position (order ≥ POS and row ≥
  PAT); the following non-waiting lines run at that same instant (decode times are not modelled), and their pictures
  are on screen from that refresh on.
- Fades: after `InitFADE` at grid point R0, the palette shown at refresh R is p1 + (p2 − p1) · min(R − R0 − 1, steps)
  / steps per component (6-bit values / 63); for more than 70 frames per second use the fractional refresh. The
  zoom: z = max(900 − 3.5 (R − R0), 1), fractional R allowed. Neither depends on how fast frames are drawn.

## 10. Rendering shortcuts and what to evaluate exactly

- **Fades**: 16.16 truncated deltas and the 6-bit DAC: evaluate `lerp(p1, p2, j / steps)` in float (the original's
  one-step shortfall of increasing fades is a truncation artefact, drop it). The steps are per refresh (70 Hz);
  interpolating between refreshes is allowed.
- **Text over the sky (mode 14)**: index arithmetic `sky + 32 · level` read through two palettes = an alpha blend of
  the text's grey level over the sky colour with weight f (section 7). With redrawn pictures: colour = lerp(sky_rgb,
  text_grey, f · coverage) where the redraw's coverage replaces the 3 anti-aliasing levels (at f = 1 the original
  shows grey 20 / 41 at the letters' edges, i.e. a dark edge, not the sky).
- **Flashes** (taivas4, white): lerp of every sky / picture colour to white; with taivas4 the bands (index 0) stay
  black.
- **Zoom**: integer rectangle (truncated x0 / y0), 16.16 nearest-neighbour stepping with the start fraction dropped
  (a jump of up to one texel = s screen pixels when the edge is clipped; at the end of each zoom s is 10..100, so
  letter edges move in visible blocks) → evaluate the continuous transform of section 6 with a filtered name texture
  (alpha from index 0 / the redraw's coverage). Keep the window rows 28..171 (of 200) and the transparent index.
  The names' grey anti-aliasing pixels are drawn opaquely in the original; with a redraw use its coverage.
- **PCX pictures**: shown 1:1 at 320 × 200, nearest; the remake shows the redraws / the original pixels scaled to the
  4:3 screen. Rectangles (captions, labels, texts) keep their source and destination rectangles in 320 × 200 units.
- No grain or dither in this scope.

## 11. Verification

`models/core/` (README.md there lists the scripts):

- `core_model.py full.frames full.script FIRST LAST`: replays part I's lines at the logged times (before or after
  the retrace of their period) through the Python ports; the DAC and VGA of each record are compared. Records
  0..5990: DAC 5,991 / 5,991; VGA 5,138 (the rest are zoom records, next item, and record 2611 where the 1280 × 200
  decode was still running). Slides #1 7759..8659: VGA 900 / 900, DAC 899 (8214: the flag-0 palette reached the DAC
  after the record, the decode spanned it). Slides #2 10444..11330: 886 / 886, DAC 885 (10885, same). Winged man
  14006..14446: 440 / 441 (14006: the copy was running at the record).
- `zoomcheck.py`: each of the 2,856 zoom records equals the model frame for that refresh (2,003) or the previous one
  (852), except record 4659 which is the new frame up to byte 34,667 and the previous one after it (copy under way).
  The model uses 53-bit precision (the start-up 0x4210ad loads CW 0x127f) and truncation; 64-bit gives the same
  integers for every z of the sequence.
- `part2check.py`: part II's flash 17155..17168 (DAC, 14 / 14) and the greetings 26888..27689 (VGA and DAC, 802 /
  802).
- `fadefit.py`: which fade step each record's DAC is (used to find the DAC timing).
- `clock.py`, `rowdelay.py`, `run_vivid_pos.py`: the clock relations above.

## 12. Not determined / guesses

- The exact player tick times (and so the exact refresh at which each line starts in music time) are the music
  scope's; the delays above are measured, not derived.
- The InitZOOM flag is uninitialised stack data; in the capture it is non-zero (no palette load). If it were 0 on
  some machine, the DAC would get `taivas.pcx`'s palette, which is identical to `taivas.pal`: no visible change.
- Which MIDAS routine 0x410410 is (detection when no set-up was asked for) and 0x40dc74 (frees the module) are named
  by their use only.
- On real hardware the one-refresh effects of decode times (a picture or a palette one refresh late, a lost fade
  step) depend on the CPU; the remake ignores them.
