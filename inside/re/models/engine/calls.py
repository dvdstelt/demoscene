"""List SurRender functions (0x410520..0x418000) with their callers, split into part callers and engine callers."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
t = rep.call_targets()
for dst in sorted(t):
    if 0x410500 <= dst < 0x418000:
        parts = [c for c in t[dst] if c < 0x40a800]
        eng = [c for c in t[dst] if 0x410500 <= c < 0x418000]
        other = [c for c in t[dst] if c not in parts and c not in eng]
        print(f'{dst:06x} parts={len(parts):3d} eng={len(eng):3d} other={len(other)}  ' + ' '.join(f'{c:06x}' for c in parts[:12]))
