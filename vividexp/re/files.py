"""List or extract the files VIVID.EXE carries in its image.

The unpacked image (unpack.py) holds two archives, one per half of the demo: part I's at 0x42b2a8 and part II's at
0x60528d. Each is a table of 128 entries of 40 bytes (a 32-byte name, the size, the offset from the start of the
table), ended by an entry named "eof.eof", and followed by the files themselves. See NOTES.md, "The file archives".

usage: python3 files.py                 # list
       python3 files.py --extract DIR   # write every file under DIR/1/ and DIR/2/, with its own folder names
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, 'unpacked', 'vivid_img.bin')
BASE = 0x400000
TABLES = (0x42b2a8, 0x60528d, 0x7979fe)   # part I, part II, part II's second landscape
ENTRIES = 128


def entries(img, TABLE):
    out = []
    for k in range(ENTRIES):
        e = img[TABLE - BASE + k * 40:TABLE - BASE + k * 40 + 40]
        name = e[:32].split(b'\0')[0].decode('ascii')
        size, ofs = struct.unpack_from('<II', e, 32)
        if not name or name == 'eof.eof':
            break                            # the table ends with an "eof.eof" entry
        out.append((name, TABLE + ofs, size))
    return out


def main():
    img = open(IMG, 'rb').read()
    for part, table in enumerate(TABLES, 1):
        ents = entries(img, table)
        if len(sys.argv) > 2 and sys.argv[1] == '--extract':
            root = os.path.join(sys.argv[2], str(part))
            for name, addr, size in ents:
                p = os.path.join(root, name)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                open(p, 'wb').write(img[addr - BASE:addr - BASE + size])
            print('wrote %d files under %s' % (len(ents), root))
            continue
        print('part %d: archive at %#x' % (part, table))
        for name, addr, size in ents:
            print('  %-24s %#9x %8d' % (name, addr, size))
        print('  %d files' % len(ents))


if __name__ == '__main__':
    main()
