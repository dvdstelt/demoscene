"""Checks the remake's exact texture generator against the original.

    build-x/jizz-texdump --out DIR             (writes DIR/final_<pointer>.bin and DIR/<name>.tga)
    python3 tools/check_textures.py DIR       (inside the toolbox: the snapshot pickle needs unicorn importable)

Compares every DIR/final_<pointer variable>.bin with the buffer that pointer variable points to in
jizz/re/snaps/t440.snap (the emulator's memory after the texture generator 0x12c50; the first part does not write
the texture buffers), and every DIR/<name>.tga with jizz/re/dump/<name>.tga (the TGA files of the /pandora mode).
Prints OK or the number of differing bytes per file; exit code 1 on any difference.
"""
import os
import pickle
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..', 're'))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    import unicorn  # noqa: F401  (the snapshot pickle refers to it)
    mem = pickle.load(open(os.path.join(RE, 'snaps', 't440.snap'), 'rb'))['mem']
    ok = True
    n = 0
    for f in sorted(os.listdir(out)):
        if not (f.startswith('final_') and f.endswith('.bin')):
            continue
        var = int(f[6:-4], 16)
        data = open(os.path.join(out, f), 'rb').read()
        ptr = struct.unpack_from('<I', mem, var)[0]
        ref = bytes(mem[ptr:ptr + len(data)])
        diff = sum(1 for a, b in zip(ref, data) if a != b)
        ok &= diff == 0
        n += 1
        print(f'[{var:#06x}] = {ptr:#x} ({len(data):#x} bytes): {"OK" if diff == 0 else f"{diff} bytes differ"}')
    for f in sorted(os.listdir(out)):
        if not f.endswith('.tga'):
            continue
        ref = open(os.path.join(RE, 'dump', f), 'rb').read()
        data = open(os.path.join(out, f), 'rb').read()
        diff = sum(1 for a, b in zip(ref, data) if a != b) + abs(len(ref) - len(data))
        ok &= diff == 0
        n += 1
        print(f'{f:14s}: {"OK" if diff == 0 else f"{diff} bytes differ"}')
    print(f'{n} files: ' + ('ALL OK' if ok and n else 'MISMATCH'))
    return 0 if ok and n else 1


if __name__ == '__main__':
    sys.exit(main())
