"""Checks mxmplay.py's internal state against MXMPlay's data block (0xe30aec, 0x4000 bytes) in the snapshots: the
header copy with its fixed-up pointers, the volume table (+0x7a8), the vibrato sine (+0x2daa), the player and timer
variables and the 18 channel records (+0x9aa), byte for byte (the IRQ stack below +0x2daa and channel bytes nothing
writes are left out). A snapshot snaps/t<T>.snap is taken before the IRQ of log tick T: it holds the state after
the IRQ of tick T - 1 (t11: after xmpPlay, before the first IRQ).

usage: python3 statecheck.py [../../snaps/t*.snap ...]      (default: all snapshots)
"""
import glob
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401  (the pickles hold a unicorn context)

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import modsynth  # noqa: E402
import mxm  # noqa: E402
import mxmplay  # noqa: E402

DATA, MOD = 0xe30aec, 0x428470


def pack_chan(c, mod_addr):
    b = bytearray(0x100)
    b[0] = c.active
    struct.pack_into('<III', b, 1, c.start, c.end, c.loop)
    b[0xd], b[0xe], b[0xf] = c.mode, c.stop, c.newsmp
    struct.pack_into('<IHHB', b, 0x10, c.offset & 0xffffffff, c.gfreq, c.gvol, c.gpan)
    b[0x19], b[0x1a], b[0x1b], b[0x1c] = c.vol, c.fvol, c.pan, c.fpan
    struct.pack_into('<ii', b, 0x1d, c.pitch, c.fpitch)
    b[0x25] = c.ins
    struct.pack_into('<IHBH', b, 0x26, (c.insp + mod_addr) if c.insp else 0, c.rel, c.keyon, c.fade)
    struct.pack_into('<IH', b, 0x31, c.envpt, c.envpos)
    b[0x3d], b[0x3e], b[0x3f], b[0x40] = c.svol, c.span, c.eff, c.volcmd
    struct.pack_into('<ii', b, 0x41, c.target, c.tpspeed)
    b[0x49] = c.vslide
    struct.pack_into('<ii', b, 0x4f, c.pupspeed, c.pdnspeed)
    b[0x5b], b[0x5c], b[0x5e] = c.vibspeed, c.vibpos, c.vibdepth
    b[0x65] = c.arpidx
    b[0x66:0x69] = bytes(c.arp)
    b[0x6a], b[0x6b], b[0x6c], b[0x6d], b[0x6e] = c.rtcount, c.rtint, c.rtvol, c.note, c.ofsmem
    return b


def pack_block(pl, mod):
    b = bytearray(0x4000)
    b[0], b[1], b[2], b[3], b[4], b[5] = pl.globalvol, pl.mastervol, pl.sync, pl.tick, pl.speed, pl.newrow
    struct.pack_into('<IIIIII', b, 6, pl.row, pl.patpos + MOD if pl.patpos else 0, pl.patrows, pl.order,
                     pl.jumporder, pl.jumprow)
    b[0x1e] = pl.patdelay
    struct.pack_into('<IIIII', b, 0x35, pl.cur, pl.timer, 0, pl.period, pl.remain)
    struct.pack_into('<II', b, 0x4b, 0x10000, pl.port)
    struct.pack_into('<I', b, 0x53, pl.voices)
    b[0x58:0x58 + 0x750] = mod.data[:0x750]
    for i in range(0x180):
        v = struct.unpack_from('<I', b, 0x1a8 + 4 * i)[0]
        struct.pack_into('<I', b, 0x1a8 + 4 * i, (v + MOD) & 0xffffffff)
    struct.pack_into('<257H', b, 0x7a8, *pl.volt)
    for i, c in enumerate(pl.ch):
        b[0x9aa + 0x100 * i:0x9aa + 0x100 * (i + 1)] = pack_chan(c, MOD)
    b[0x2daa:0x2daa + 256] = bytes(x & 0xff for x in pl.vibtab)
    return b


# bytes compared: everything the model keeps (the rest of the block is the IRQ stack, the old vector, the saved
# stack pointer, the BIOS divider, ds, the porta flag +0x57, the event bytes +0x1f..+0x24 and unused channel bytes)
def mask():
    m = bytearray(0x4000)
    for a, n in ((0, 0x1f), (0x35, 8), (0x41, 8), (0x4b, 8), (0x53, 4), (0x58, 0x750), (0x7a8, 514),
                 (0x2daa, 256)):
        m[a:a + n] = b'\1' * n
    chan = [(0, 0x31), (0x31, 6), (0x3d, 0x10), (0x4f, 8), (0x5b, 2), (0x5e, 1), (0x65, 4), (0x6a, 5)]
    for i in range(32):
        for a, n in chan:
            o = 0x9aa + 0x100 * i + a
            m[o:o + n] = b'\1' * n
    return m


def main():
    snaps = sys.argv[1:] or sorted(glob.glob(os.path.join(RE, 'snaps', 't*.snap')),
                                   key=lambda p: int(os.path.basename(p)[1:-5]))
    img = modsynth.load_img()
    mod = mxm.Module(bytes(modsynth.build(img)))
    gus = mxmplay.GusLogger()
    pl = mxmplay.Player(mod, img, gus)
    pl.init()
    pl.play(0)
    t = 10
    m = mask()
    ok = True
    for path in snaps:
        T = int(os.path.basename(path)[1:-5])
        while t < T - 1:
            t += 1
            pl.irq()
        mem = pickle.load(open(path, 'rb'))['mem']
        ref = mem[DATA:DATA + 0x4000]
        mine = pack_block(pl, mod)
        bad = [i for i in range(0x4000) if m[i] and ref[i] != mine[i]]
        ok &= not bad
        print('%-8s (state after IRQ of tick %5d, %02x:%02x): %s' % (
            os.path.basename(path), T - 1, pl.order, pl.row,
            'data block identical (%d bytes compared)' % sum(m) if not bad else '%d bytes differ: %s' % (
                len(bad), ', '.join('+%#x (%02x vs %02x)' % (i, ref[i], mine[i]) for i in bad[:12]))), flush=True)
    print('ALL MATCH' if ok else 'MISMATCH')


if __name__ == '__main__':
    main()
