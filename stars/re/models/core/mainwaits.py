"""main 0x4d waits with `call 0x29f80; cmp ax, X; jbe` (continue when order << 8 | row > X). For each X, the first
captured frame (full.ticks) with a position above it, its time and the GUS player tick."""
import cap

WAITS = [0x0b00, 0x1700, 0x2700, 0x3500, 0x4500, 0x5500, 0x5800, 0x5820, 0x6100, 0x6900, 0x6c00, 0x8700,
         0x9500, 0x9519, 0x9600, 0x9700, 0x9800, 0x9a00, 0x9a20]
t = cap.ticks()
for x in WAITS:
    for r in t:
        if (r[4] << 8 | r[5]) > x:
            print(f'pos > {x:#06x}: frame {r[0]:6d}  time {r[1]:8.3f} s  pos {r[4]:02x}:{r[5]:02x}  gus tick {r[3]}')
            break
