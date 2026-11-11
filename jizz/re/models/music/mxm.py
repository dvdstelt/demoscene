"""MXM module reader (the format MXMPlay 0x29738.. reads), and a dump of what is in JIZZ's module.

Layout (all little endian; offsets are file offsets; the player copies bytes 0..0x74f to its data block + 0x5a and
turns the 0x180 offsets at 0x150.. into pointers by adding the module address):
  0x000 char[4]  'MXM\\0'
  0x004 u32      order count            (22)
  0x008 u32      restart order          (0)
  0x00c u32      channels               (14)
  0x010 u32      patterns               (17)
  0x014 u32      instruments            (37)
  0x018 u8       initial speed (ticks per row)   (6)
  0x019 u8       initial tempo (BPM)             (151)
  0x01a u16      options: bit 0 linear frequencies (always assumed), bit 1 = notes do not reset the panning
  0x01c u32      sample data offset     (18588)
  0x020 u32      8-bit sample bytes     (910080)
  0x024 u32      16-bit sample words    (0)
  0x028 s32      lowest pitch  (for fine portamento)  (-18432)
  0x02c s32      highest pitch                         (24576)
  0x030 u8[32]   initial panning per channel (0..255)
  0x050 u8[256]  order list (pattern numbers)
  0x150 u32[128] instrument offsets (index 1..127 used: entry i at 0x14c + 4i; entry 0 = 0x150 is instrument 1)
  0x350 u32[256] pattern offsets
Instrument (at its offset):
  +0x00 u32      number of samples
  +0x04 u8[96]   sample index for notes 1..96
  +0x64 u16      fadeout (subtracted from a 0x8000 fade level per tick after key off)
  +0x66 u8[4]    (not read by this player: panning envelope fields)
  +0x6a u8       volume envelope: index of the last point (0 = no envelope: key off cuts the note)
  +0x6b u8       sustain point
  +0x6c u8       loop start point
  +0x6d u8       loop end point
  +0x6e {u16 ticks to the next point, u16 value 0..64}[...]   volume envelope
  +0x100 16-byte sample headers:
     +0 u24 GUS DRAM start, +3 u24 loop start, +6 u24 end (all byte addresses: the sample data block is uploaded
     to DRAM 0), +9 u8 GF1 voice control (bit 3 loop, bit 4 bidirectional, bit 2 16 bit), +0xa u8 volume 0..64,
     +0xb u8 panning 0..255, +0xc s16 relative note * 256 + finetune (1/256 semitones), +0xe u16 unused
Pattern (at its offset): u32 rows, then per row a list of entries ending with a 0 byte; entry = flag byte
  (bits 0..4 channel, 0x20: note byte + instrument byte follow, 0x40: volume column byte, 0x80: effect + parameter),
  in increasing channel order.
Effects: XM's 0..0x23 (0..9, A..Z = 10..35), Exy = 0x24 + x with parameter y.

usage: python3 mxm.py [module.mxm]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))

EFFNAMES = {0: 'arpeggio', 1: 'porta up', 2: 'porta down', 3: 'tone porta', 4: 'vibrato', 5: 'tone porta+vol slide',
            6: 'vibrato+vol slide', 7: 'tremolo', 8: 'set panning', 9: 'sample offset', 10: 'volume slide',
            11: 'position jump', 12: 'set volume', 13: 'pattern break', 15: 'speed/tempo', 16: 'global volume',
            17: 'global vol slide', 20: 'key off', 21: 'set env pos', 25: 'pan slide', 27: 'multi retrig',
            29: 'tremor', 33: 'extra fine porta'}
for x, n in enumerate(['E0 filter', 'E1 fine porta up', 'E2 fine porta down', 'E3 gliss', 'E4 vib wave',
                       'E5 finetune', 'E6 pattern loop', 'E7 trem wave', 'E8 set pan', 'E9 retrig',
                       'EA fine vol up', 'EB fine vol down', 'EC note cut', 'ED note delay', 'EE pattern delay',
                       'EF']):
    EFFNAMES[0x24 + x] = n


class Module:
    def __init__(self, data):
        self.data = data
        d = data
        assert d[:4] == b'MXM\0'
        (self.ordnum, self.restart, self.channels, self.patnum, self.insnum) = struct.unpack_from('<5I', d, 4)
        self.speed, self.bpm = d[0x18], d[0x19]
        self.opt = struct.unpack_from('<H', d, 0x1a)[0]
        self.smpstart, self.smp8, self.smp16 = struct.unpack_from('<3I', d, 0x1c)
        self.lowpitch, self.highpitch = struct.unpack_from('<2i', d, 0x28)
        self.panpos = list(d[0x30:0x50])
        self.orders = list(d[0x50:0x50 + 256])
        self.insofs = struct.unpack_from('<128I', d, 0x150)
        self.patofs = struct.unpack_from('<256I', d, 0x350)

    def ins_ptr(self, i):           # instrument i (1..), as the player indexes it: [D + 0x1a6 + 4i]
        return struct.unpack_from('<I', self.data, 0x14c + 4 * i)[0]

    def instrument(self, i):
        d, p = self.data, self.ins_ptr(i)
        nsmp = struct.unpack_from('<I', d, p)[0]
        keymap = list(d[p + 4:p + 0x64])
        fade = struct.unpack_from('<H', d, p + 0x64)[0]
        envnum, sus, lps, lpe = d[p + 0x6a], d[p + 0x6b], d[p + 0x6c], d[p + 0x6d]
        env = [struct.unpack_from('<HH', d, p + 0x6e + 4 * k) for k in range(envnum + 1)]
        smps = []
        for s in range(nsmp):
            h = p + 0x100 + 16 * s
            st = int.from_bytes(d[h:h + 3], 'little')
            ls = int.from_bytes(d[h + 3:h + 6], 'little')
            en = int.from_bytes(d[h + 6:h + 9], 'little')
            smps.append({'start': st, 'loop': ls, 'end': en, 'mode': d[h + 9], 'vol': d[h + 10], 'pan': d[h + 11],
                         'rel': struct.unpack_from('<h', d, h + 12)[0], 'x': struct.unpack_from('<H', d, h + 14)[0]})
        return {'ptr': p, 'nsmp': nsmp, 'keymap': keymap, 'fade': fade, 'envnum': envnum, 'sus': sus,
                'loopstart': lps, 'loopend': lpe, 'env': env, 'samples': smps,
                'unread': d[p + 0x66:p + 0x6a]}

    def pattern(self, n):
        """list of rows; row = {channel: (note, ins, vol, eff, par)} (missing fields 0, as the player reads)"""
        d = self.data
        p = struct.unpack_from('<I', d, 0x350 + 4 * n)[0]
        rows = struct.unpack_from('<I', d, p)[0]
        p += 4
        out = []
        for r in range(rows):
            row = {}
            while True:
                f = d[p]
                p += 1
                if f == 0:
                    break
                note = ins = vol = eff = par = 0
                if f & 0x20:
                    note, ins = d[p], d[p + 1]
                    p += 2
                if f & 0x40:
                    vol = d[p]
                    p += 1
                if f & 0x80:
                    eff, par = d[p], d[p + 1]
                    p += 2
                row[f & 0x1f] = (note, ins, vol, eff, par, f)
            out.append(row)
        return out, p


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, 'dump', 'smokeout.mxm')
    m = Module(open(path, 'rb').read())
    print('orders %d restart %d channels %d patterns %d instruments %d speed %d bpm %d opt %#x' % (
        m.ordnum, m.restart, m.channels, m.patnum, m.insnum, m.speed, m.bpm, m.opt))
    print('samples at %#x: %d 8-bit bytes, %d 16-bit words; pitch range %d..%d' % (m.smpstart, m.smp8, m.smp16,
                                                                                   m.lowpitch, m.highpitch))
    print('panning', m.panpos[:m.channels])
    print('orders', m.orders[:m.ordnum])
    end = 0
    for i in range(1, m.insnum + 1):
        I = m.instrument(i)
        used = sorted(set(I['keymap']))
        print('ins %2d @%#06x nsmp %d fade %d env last %d sus %d loop %d..%d env %s unread %s keymap uses %s' % (
            i, I['ptr'], I['nsmp'], I['fade'], I['envnum'], I['sus'], I['loopstart'], I['loopend'],
            I['env'], I['unread'].hex(), used))
        for s in I['samples']:
            print('     smp start %#07x loop %#07x end %#07x len %6d mode %#04x vol %2d pan %3d rel %6d (%+.2f st) x %d'
                  % (s['start'], s['loop'], s['end'], s['end'] - s['start'], s['mode'], s['vol'], s['pan'], s['rel'],
                     s['rel'] / 256, s['x']))
    effs = {}
    vols = {}
    rows_total = 0
    for o in range(m.ordnum):
        pat, pend = m.pattern(m.orders[o])
        end = max(end, pend)
        rows_total += len(pat)
        for row in pat:
            for ch, (note, ins, vol, eff, par, f) in row.items():
                if f & 0x80:
                    effs.setdefault(eff, set()).add(par)
                if f & 0x40:
                    vols.setdefault(vol >> 4, set()).add(vol)
    for n in range(m.patnum):
        pat, pend = m.pattern(n)
        print('pattern %2d: %3d rows, offset %#x..%#x' % (n, len(pat), m.patofs[n], pend))
    print('rows in the order list: %d' % rows_total)
    for e in sorted(effs):
        print('effect %#04x %-22s params %s' % (e, EFFNAMES.get(e, '?'), ' '.join('%02x' % p for p in sorted(effs[e]))))
    for v in sorted(vols):
        print('volume column %x_: %s' % (v, ' '.join('%02x' % p for p in sorted(vols[v]))))


if __name__ == '__main__':
    main()
