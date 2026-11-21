"""Reader for DSMI's AMF module format as DSMI 2.x's loader 0x40fcc8 / 0x4103d0 reads it (version 14: EYE.AMF).

Layout of version 14 (all little endian), with the in-memory form AMP plays from:
  0x00  'AMF', byte version (0x0e)
  0x04  title[32]                                      -> MODULEINFO +0x09
  0x24  byte instruments (31), byte orders (37), word tracks (140)   -> +0x4a, +0x4f, +0x54
  0x28  byte channels (4)                              -> +0x29
  0x29  pan[32] (signed, -64..63, 100 = surround)      -> +0x2a   (16 bytes for versions < 12)
  0x49  byte tempo (125), byte speed (6)               -> +0x5a, +0x5b   (versions >= 13)
  0x4b  orders: per order word rows, then per channel word track number (1-based; 0 = empty)
        -> 0x82-byte records {word rows; dword track pointer[32]}
  then  instruments: 65 bytes each, kept verbatim as AMP's 0x41-byte instrument record:
        +0 type, +1 name[32], +0x21 file name[13], +0x2e dword sample index (1-based; replaced by the sample's
        address), +0x32 dword length, +0x36 word rate (Hz at note 60), +0x38 byte volume (0..64),
        +0x39 dword loop start, +0x3d dword loop end (0 = no loop)
  then  track map: tracks words (track number -> stored track, 1-based)
  then  stored tracks: word events, byte (type, ignored), events x 3 bytes {row, command, value};
        in memory: 3-byte header {word events, byte 0} then the events
  then  sample data: 8-bit signed PCM, for sample index 1, 2, ... (length taken from the first instrument that
        uses the index; the loader reads it as a word)
"""
import struct


class Instrument:
    def __init__(self, i, raw):
        self.i = i
        self.raw = raw
        self.type = raw[0]
        self.name = raw[1:0x21].split(b'\0')[0].decode('latin1')
        self.filename = raw[0x21:0x2e].split(b'\0')[0].decode('latin1')
        self.index, self.length, self.rate, self.volume, self.loopstart, self.loopend = \
            struct.unpack_from('<IIHBII', raw, 0x2e)
        self.data = b''


class AMF:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.d = d
        assert d[:3] == b'AMF'
        self.version = v = d[3]
        self.title = d[4:0x24].split(b'\0')[0].decode('latin1')
        p = 0x24
        self.ninstr, self.norders, self.ntracks = struct.unpack_from('<BBH', d, p)
        p += 4
        self.channels = 4
        self.pan = [0] * 32
        if v >= 9:
            self.channels = d[p]
            p += 1
            n = 32 if v >= 12 else 16
            self.pan = [b - 256 if b > 127 else b for b in d[p:p + n]] + [0] * (32 - n)
            p += n
        self.tempo, self.speed = 125, 6
        if v >= 13:
            self.tempo, self.speed = d[p], d[p + 1]
            p += 2
        self.orders = []                         # (rows, [track number per channel])
        for _ in range(self.norders):
            rows = 64
            if v >= 14:
                rows = struct.unpack_from('<H', d, p)[0]
                p += 2
            tr = list(struct.unpack_from('<%dH' % self.channels, d, p))
            p += 2 * self.channels
            self.orders.append((rows, tr))
        assert v >= 10
        self.instruments = []
        self.instr_offset = p
        for i in range(self.ninstr):
            self.instruments.append(Instrument(i, d[p:p + 65]))
            p += 65
        self.trackmap = list(struct.unpack_from('<%dH' % self.ntracks, d, p))
        p += 2 * self.ntracks
        nstored = max(self.trackmap)
        self.stored = []                          # (file offset of the events, [(row, cmd, val)])
        self.stored_offset = p
        for _ in range(nstored):
            n, _type = struct.unpack_from('<HB', d, p)
            p += 3
            ev = [tuple(d[p + 3 * k:p + 3 * k + 3]) for k in range(n)]
            self.stored.append((p, ev))
            p += 3 * n
        # sample data, in index order; the length of index k comes from the first instrument with index > count
        self.sample_offset = p
        sizes = []
        for ins in self.instruments:
            if len(sizes) < ins.index:
                sizes.append(ins.length & 0xffff)
        self.samples = []                         # (file offset, bytes) per sample index 1..
        for s in sizes:
            self.samples.append((p, d[p:p + s]))
            p += s
        self.end = p
        for ins in self.instruments:
            if 1 <= ins.index <= len(self.samples):
                ins.data = self.samples[ins.index - 1][1]
                ins.data_offset = self.samples[ins.index - 1][0]
            else:
                ins.data, ins.data_offset = b'', None

    def track(self, number):
        """Events of track number (1-based as in the orders; 0 = none)."""
        if number == 0:
            return None
        s = self.trackmap[number - 1]
        return self.stored[s - 1][1] if s and self.stored[s - 1][1] else None    # 0 events: pointer 0


def effect_name(cmd):
    names = ['instrument', 'speed', 'volume slide', 'volume', 'porta', 'nothing (5)', 'tone porta', 'tremolo',
             'arpeggio', 'vibrato', 'tone porta + vol slide', 'vibrato + vol slide', 'pattern break',
             'position jump', 'sync', 'retrig', 'sample offset', 'fine volume slide', 'fine porta', 'note delay',
             'note cut', 'tempo', 'extra fine porta', 'panning']
    i = cmd & 0x7f
    return names[i] if i < len(names) else 'bad %02x' % cmd


if __name__ == '__main__':
    import sys
    import collections
    m = AMF(sys.argv[1] if len(sys.argv) > 1 else '../../../original/EYE.AMF')
    print('AMF v%d %r: %d instruments, %d orders, %d tracks (%d stored), %d channels, tempo %d speed %d' % (
        m.version, m.title, m.ninstr, m.norders, m.ntracks, len(m.stored), m.channels, m.tempo, m.speed))
    print('pan', m.pan[:m.channels])
    print('offsets: instruments %#x, track map %#x, tracks %#x, samples %#x, end %#x (file %#x)' % (
        m.instr_offset, m.instr_offset + 65 * m.ninstr, m.stored_offset, m.sample_offset, m.end, len(m.d)))
    for ins in m.instruments:
        print('%2d %-32r %-13r idx %2d len %6d rate %5d vol %2d loop %6d..%6d  data@%s' % (
            ins.i, ins.name, ins.filename, ins.index, ins.length, ins.rate, ins.volume, ins.loopstart, ins.loopend,
            '%#x' % ins.data_offset if ins.data_offset is not None else '-'))
    for i, (rows, tr) in enumerate(m.orders):
        print('order %2d rows %d tracks %s' % (i, rows, tr))
    cnt = collections.Counter()
    for _, ev in m.stored:
        for r, c, v in ev:
            if c >= 0x80:
                cnt[(effect_name(c))] += 1
            elif c == 0:
                cnt['note off' if v == 0 else 'note 0'] += 1
            elif c == 0x7f:
                cnt['0x7f (volume only)'] += 1
            else:
                cnt['note'] += 1
    print(dict(cnt))
