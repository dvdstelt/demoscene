"""The ten stored pictures of CLONE.EXE: decode them like 0x403f53 and write PNGs.

usage (inside the toolbox, from clone/re):
  python3 models/gfx/pictures.py [--check snaps/t11.snap] [--out dump/pictures]

Writes per picture <name>.png (RGBA: the 32-bit picture 0x403f53 makes, transparent pixels alpha 0, colours
the 6-bit palette value << 2 as the original shows them) and in extra/: <name>_idx.png (the stored 8-bit indices
as they are, grey), <name>_x8.png (the RGBA one scaled 8x, nearest, on a grey checker so the transparency shows),
<name>_onblack_x8.png (on black, as the parts show them), and mosaic_part11.png (the 8x8 percent-RGB tile table
at 0x428164 that part 11 reads, x32; not one of the ten pictures). With --check it compares the decoded pixels
with the copies 0x403f53 made in the snapshot (pointer table below).
"""
import os
import pickle
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import rep  # noqa: E402

# name, address, w, h, palette, mode (0 = index 0 and 0xf0 transparent, 1 = index 0 only), pointer
PICTURES = [
    ('meet_us', 0x41a950, 42, 9, 'A', 0, 0xe35124),
    ('get_them_clones', 0x41aaca, 85, 12, 'A', 0, 0xe35120),
    ('connect_the_dots', 0x41aec6, 88, 10, 'A', 0, 0xe3511c),
    ('tunnels_meet_more_tunnels', 0x41b236, 142, 10, 'A', 0, 0xe35118),
    ('glyph1', 0x41b802, 77, 65, 'B', 1, 0xe350fc),
    ('glyph2', 0x41cb8f, 63, 67, 'B', 1, 0xe350f8),
    ('glyph3', 0x41dc0c, 68, 68, 'B', 1, 0xe350f4),
    ('glyph4', 0x41ee1c, 56, 64, 'B', 1, 0xe350f0),
    ('title_clone_meets_clone', 0x41fc5c, 101, 9, 'C', 0, 0xf73930),
    ('credits_aap_echo_simon', 0x41ffe9, 37, 32, 'C', 0, 0xf7392c),
]


def palettes():
    """The three 6-bit palettes (256 x RGB): grey A 0xe34dec, grey B 0xe34aec (main 0x4008d4..0x400946),
    grey C (0x419d5b, a local array of 16 entries; entries 16.. are the stack beyond it, never used)."""
    a = [[0, 0, 0] for _ in range(256)]
    b = [[0, 0, 0] for _ in range(256)]
    for c in range(16):
        s = 15 - c
        va = rep.rd32(0x41b7c2 + 4 * s) * 63 // 100
        vb = rep.rd32(0x41fc1c + 4 * s) * 63 // 100
        a[255 - c] = [va] * 3
        b[255 - c] = [vb] * 3
    cpal = [[c * 63 // 15] * 3 for c in range(16)] + [[0, 0, 0] for _ in range(240)]
    return {'A': a, 'B': b, 'C': cpal}


def decode(addr, w, h, pal, mode):
    """0x403f53: 32-bit 0x00RRGGBB = 6-bit palette << 2 (byte shift), index 0 (and 0xf0 when mode == 0)
    become 0xffffffff."""
    src = rep.rd(addr, w * h)
    out = []
    for i in src:
        if i == 0 or (i == 0xf0 and mode == 0):
            out.append(0xffffffff)
        else:
            r, g, b = (x << 2 & 0xff for x in pal[i])
            out.append(r << 16 | g << 8 | b)
    return src, out


def main():
    from PIL import Image
    args = sys.argv[1:]
    outdir = os.path.join(RE, 'dump', 'pictures')
    snap = None
    if '--out' in args: outdir = args[args.index('--out') + 1]
    if '--check' in args:
        import unicorn  # noqa: F401  (the pickle refers to it)
        snap = pickle.load(open(args[args.index('--check') + 1], 'rb'))
    extra = os.path.join(outdir, 'extra')
    os.makedirs(extra, exist_ok=True)
    pals = palettes()
    mosaic = struct.unpack('<192i', rep.rd(0x428164, 768))
    m = Image.new('RGB', (8, 8))
    m.putdata([tuple(min(255, int(v * 2.55)) for v in mosaic[i * 3:i * 3 + 3]) for i in range(64)])
    m.resize((256, 256), Image.NEAREST).save(os.path.join(extra, 'mosaic_part11.png'))
    for name, addr, w, h, pn, mode, ptr in PICTURES:
        src, px = decode(addr, w, h, pals[pn], mode)
        hist = {}
        for i in src: hist[i] = hist.get(i, 0) + 1
        idx = sorted(hist)
        print(f'{name:28s} {addr:#x} {w}x{h} pal {pn} mode {mode}: indices {idx[0]}..{idx[-1]} '
              f'({len(idx)} distinct) transparent {sum(1 for p in px if p == 0xffffffff)}/{w*h}')
        rgba = Image.new('RGBA', (w, h))
        rgba.putdata([(0, 0, 0, 0) if p == 0xffffffff else (p >> 16 & 255, p >> 8 & 255, p & 255, 255) for p in px])
        rgba.save(os.path.join(outdir, f'{name}.png'))
        Image.frombytes('L', (w, h), bytes(src)).save(os.path.join(extra, f'{name}_idx.png'))
        big = Image.new('RGBA', (w * 8, h * 8))
        for y in range(h * 8):
            for x in range(w * 8):
                big.putpixel((x, y), (96, 96, 96, 255) if (x // 8 + y // 8) & 1 else (128, 128, 128, 255))
        big.alpha_composite(rgba.resize((w * 8, h * 8), Image.NEAREST))
        big.save(os.path.join(extra, f'{name}_x8.png'))
        blk = Image.new('RGBA', (w * 8, h * 8), (0, 0, 0, 255))
        blk.alpha_composite(rgba.resize((w * 8, h * 8), Image.NEAREST))
        blk.convert('RGB').save(os.path.join(extra, f'{name}_onblack_x8.png'))
        if snap is not None:
            mem = snap['mem']
            hdr = struct.unpack_from('<I', mem, ptr)[0]
            pp, ww, hh = struct.unpack_from('<III', mem, hdr)
            got = struct.unpack_from(f'<{ww*hh}I', mem, pp)
            print(f'    snapshot: header {hdr:#x} pixels {pp:#x} {ww}x{hh} '
                  f'{"identical" if (ww, hh) == (w, h) and list(got) == px else "DIFFERENT"}')


if __name__ == '__main__':
    main()
