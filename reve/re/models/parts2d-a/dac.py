"""Check the DAC of every record of the parts2d-a entries against the palette the entry's init loads.

usage: python3 dac.py
None of these entries changes the palette while it runs: the DAC is written only by the picture loader 0x25a1fb.
"""
import numpy as np

from common import frames, indexed, raw

RANGES = [(32, 651, 'GFX\\TEXTURE.RAW', 'entry 0'), (2737, 3192, 'GFX\\PULSE.RAW', 'entries 5, 6'),
          (3193, 4088, 'GFX\\WIZ.RAW', 'entry 7'), (6329, 7223, 'GFX\\PLAYMO.RAW', 'entries 9, 10'),
          (8119, 9910, 'GFX\\TEXTURE.RAW', 'entries 12, 13'), (10807, 11701, 'GFX\\BOUCHE.RAW', 'entry 15')]


def main():
    raw_frames, n = frames()
    for a, b, f, what in RANGES:
        _, pal = raw(f)
        bad = [r for r in range(a, b + 1) if not (indexed(raw_frames, r)[1] == pal >> 2).all()]
        print('%-15s records %5d..%5d: DAC = %s palette >> 2 in all but %d %s' % (what, a, b, f, len(bad), bad[:5]))


if __name__ == '__main__':
    main()
