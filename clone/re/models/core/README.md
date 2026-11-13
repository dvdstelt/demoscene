# Models of the core scope (notes: `../../notes-core.md`)

Run from `clone/re` inside the toolbox: `toolbox run -c acme303 python3 models/core/<script>`. They read
`unpacked/clone_img.bin`, `snaps/*.snap` and `full.*`, and write nothing (a listing of main, `rep.py func
0x4001ab`, is in the ignored `dump/core/main.lst`).

| script | what it checks | result |
|---|---|---|
| `veunpack.py [../original/CLONE.EXE] [base]` | an unpacker written from NOTES.md alone (MZ → LE → VE header, decompressor, fixups) | identical to `unpacked/clone_img.bin` at base 0x400000; FNV-1a 64 `95367b24703a7595` at base 0 |
| `tables.py` | the precalc tables of 0x416806, 0x403f10, 0x40347a, 0x4033ee recomputed | all bytes equal to `snaps/t11.snap` |
| `dtmodel.py` | main's t70 / dt / [0xe9573e] step with the truncating FPU, replayed over all timer values of `full.flips` | t70, dt and the accumulator bit-exact at all 12 snapshots |
| `timecheck.py` | the t70 constant (1/17043) against the snapshots | t70 = timer / 17043, not timer × 70 / 1193182 |
| `partclock.py` | where each part's dt clock starts relative to its event, from `full.flips` | offsets 0.02..1.19 t70 units; events fire when xmpGetTimer ≥ p × 66300 − 22100 |
| `helpers_test.py` | the 2D helpers (0x4043be, 0x404002 / 0x4043ec / 0x40442c, 0x4045b4, 0x404a08, 0x4167b7, 0x4034e7) and the 15-bit conversion 0x4188d4 executed in unicorn on random inputs against Python models | all equal (the log curves differ only for x < −1, where the emulator's fyl2x of a negative number is not an x87's) |
| `heapwalk.py [snap]` | the heap blocks (0x400ff4 / 0x40100c) and their owners | 40 used blocks, free from 0x904d68 |
| `rngstate.py` | the shared random generator 0x420491: numbers drawn at each snapshot | 0 at t11 … 329,145 at t11148 |
