# models/core — scripts of the core scope (notes-core.md)

Run from `okean/re` inside the toolbox (`toolbox run -c acme303 python3 models/core/<script>`).

| script | what it does / checks |
|---|---|
| `find_timers.py` | finds the eight copies of the retrace timer (0x14be0..0x14d1b) and compares them byte by byte with the first after relocation: they differ only in their callback pointer |
| `run_core.py` | copy of `run_okean.py` with an event log (`--events FILE`, `--no-frames`): callback entries, IRQ 0 handler entries (retrace bit, count), main-loop tops, copies to video memory, DAC writes, player ticks, printer calls, the end part's page select, the timer routines, records. Snapshots it takes go to `models/core/snaps/` |
| `runs/` | the event logs `p1.ev` .. `p8.ev` (with the runs' `.ticks`, `.gus`, `.log`): each part run from its snapshot until about 60 refreshes into the next part (`p8` to the exit), e.g. `run_core.py --load snaps/part_2e779.snap --out models/core/runs/p2 --frames 2391 --events models/core/runs/p2.ev --no-frames`; their `.ticks` equal `full.ticks` |
| `clock.py` | per part: first callback, callbacks per refresh, which callback count each record shows, drops, exit; `--table` writes `record_clock.txt` (every record of `full.frames`: the part and callback count it shows) and lists the drops with the music tick that caused them |
| `rows.py` | `rows.txt`: the start tick, time, refresh and line of every music row (1,494 rows); checks the tick times against `full.gus` and the positions against all 9,105 records of `full.ticks` (0 differences) |
| `ticks_gus.py` | the timer-2 ticks in `full.gus`: grid, late ticks, lost ticks at the part starts |
| `title_palette_check.py` | checks the palette-timing rule on the title fade (DAC of record k = state after n + 1 callbacks; 48 of 48 records) |
| `font_check.py`, `font_sheet.py` | the font sheet 0x3cec2 (static, the same in every snapshot); writes `font_sheet.png` (as stored, end palette, x4), `font_sheet_indexed.png` (1:1, the stored indices), `font_sheet_false.png` (index 7 red, 216 green, 254 white, glyph starts marked), `font_glyphs.png` (glyphs cut out as the printer cuts them) |
| `printer.py` | which page the shared printer 0x4cacc prints in every record of parts 1..7, and a check of every caption against `full.frames` (all glyph pixels of all 11 caption pages equal the capture) |
| `endpart.py` | model of the last part 0x4d370 (noise, page select, text, fade): **records 8099..9108 reproduced bit for bit** (indices and DAC, 1010 of 1010) |
| `endnoise.py` | statistics of the end part's noise generator (uniform over 0..31, no repeat within the part) |
| `captions.jpg`, `caption_5300.png`, `endpage_8400.png`, `font_zoom_a_k.png` | contact sheet / crops of the captured captions and end pages; the glyphs a..k at x8 (outline red, body white, edge pixels green) |

Images and `runs/` are git-ignored (made from the original); the scripts, `rows.txt` and `record_clock.txt` are not.
