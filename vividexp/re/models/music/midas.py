"""Port of MIDAS Sound System 0.5a as VIVID.EXE uses it: the XM loader, the Generic Module Player (GMP) for XM
modules, and the Gravis UltraSound sound device. Routine by routine; addresses in the comments (notes-music.md).

The GUS device does not touch hardware: every port write it would make goes through Gf1Port, which logs the register
writes in the format of run_vivid.py's .gus files and answers the reads either like run_vivid.py's Gus class does
(answers='capture': a read returns the last value written, the voice is always stopped and the ramp always done) or
from a running GF1 model (answers=<object with read(voice, reg)>, see render.py).

Tables are read from the program image (unpacked/vivid_img.bin), never copied.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import rep  # noqa: E402  (vividexp/re/rep.py: the image)

sys.path.insert(0, HERE)
import xm  # noqa: E402


def s8(x):
    x &= 0xff
    return x - 0x100 if x & 0x80 else x


def s16(x):
    x &= 0xffff
    return x - 0x10000 if x & 0x8000 else x


def s32(x):
    x &= 0xffffffff
    return x - 0x100000000 if x & 0x80000000 else x


def tdiv(a, b):
    """idiv: quotient truncated toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ---- tables in the image --------------------------------------------------------------------------------------------
XM_AMIGA = 0x822280        # 96 dwords: 12 notes x 8 finetunes (856 .. 431), FT2's Amiga period table
XM_LINEAR = 0x822430       # 768 dwords: 8363 * 64 * 2^(-k/768)
VIB_SINE = 0x824c30        # 32 bytes: half sine 0..255
GUS_RATES = 0x824452       # 19 words: GF1 output rate for 14..32 active voices
GUS_VOLTAB = 0x824478      # 65 words: GF1 volume (12.4 log) for volume 0..64
XM_FX = 0x8238e4           # XM effect 0x00..0x23 -> GMP command
XM_EFX = 0x823908          # XM Exy x -> GMP command


# ======================================================================================================================
# XM loader (0x418d44) and pattern conversion (0x418918)
# ======================================================================================================================
def convert_pattern(raw, rows, nchan, state):
    """0x418918: FT2 packed pattern -> GMP pattern data (per row: entries flag|channel [note inst] [vol] [cmd info],
    ended by 0). state: dict with 'maxinst' and 'used' (256 flags), updated like [0x846d20] / [0x846d08]."""
    out = bytearray()
    if not raw:
        return bytes(rows)                     # 0x418939: rows zero bytes (every row empty)
    i = 0
    for r in range(rows):
        for c in range(nchan):
            flag = 0
            note = 0xff
            inst = 0xff
            vol = 0
            fx = 0
            fp = 0
            b = raw[i]
            if b & 0x80:
                f = b & 0x7f
                i += 1
            else:
                f = 0x7f
            if f & 1:
                n = raw[i]
                if n > 0x60:
                    note = 0xfe
                else:
                    n -= 1
                    note = ((tdiv(n, 12) << 4) | (n - tdiv(n, 12) * 12)) & 0xff   # note 0 -> 0xff (no note)
                flag |= 0x20
                i += 1
            if f & 2:
                inst = (raw[i] - 1) & 0xff
                i += 1
                if note == 0xfe:
                    inst = 0xff
                if inst != 0xff:
                    state['maxinst'] = max(state['maxinst'], inst)
                    state['used'][inst] = 1
                    flag |= 0x20
            if f & 4:
                vol = raw[i]
                i += 1
                if vol:
                    if 0x10 <= vol <= 0x50:
                        vol -= 0x10
                    flag |= 0x40
            if f & 8:
                fx = raw[i]
                i += 1
            if f & 0x10:
                fp = raw[i]
                i += 1
            cmd = 0
            if fx or fp:
                cmd = rep.rd(XM_FX + fx, 1)[0]
                lo = fp & 0xf
                if fx < 0xf:
                    if fx in (5, 6, 0xa):
                        fp = (fp >> 4) if fp & 0xf0 else (-lo) & 0xff
                    elif fx == 0xe:
                        sub = fp >> 4
                        cmd = rep.rd(XM_EFX + sub, 1)[0]
                        if sub in (1, 0xb):            # jump table 0x4188ec: E1x, EBx -> -x
                            fp = (-lo) & 0xff
                        else:
                            fp = lo
                elif fx == 0xf:
                    cmd = 0x10 if fp >= 0x20 else 0xf
                elif fx < 0x14:
                    if fx == 0x11:
                        fp = (fp >> 4) if fp & 0xf0 else (-lo) & 0xff
                elif fx == 0x14:                       # Kxx: key off now (the tick is ignored), keeps the instrument
                    note = 0xfe
                    flag |= 0x20
                    cmd = 0
                elif fx < 0x19:
                    pass
                elif fx == 0x19:
                    fp = (fp >> 4) if fp & 0xf0 else (-lo) & 0xff
                elif fx == 0x21:
                    h = fp & 0xf0
                    if h == 0x10:
                        fp = (-lo) & 0xff
                    elif h == 0x20:
                        fp = lo
                    else:
                        cmd = 0
                if cmd:
                    flag |= 0x80
            if flag:
                out.append(flag | c)
                if flag & 0x20:
                    out += bytes((note, inst))
                if flag & 0x40:
                    out.append(vol)
                if flag & 0x80:
                    out += bytes((cmd, fp & 0xff))
        out.append(0)
    return bytes(out)


class Sample:
    pass


class Instrument:
    pass


def load_xm(path, sd):
    """0x418d44: returns the module dict; adds every sample to the sound device sd (AddSample, in loader order)."""
    f = xm.load(path)
    d = open(path, 'rb').read()
    m = {'type': 3, 'name': f['name'], 'length': f['length'], 'restart': f['restart'], 'nchan': f['channels'],
         'gv': 0x40, 'speed': f['speed'], 'tempo': f['tempo'], 'linear': bool(f['flags'] & 1)}
    orders = list(f['orders'])
    m['orders'] = orders
    npat = f['npatterns']
    if max(orders) >= npat:                      # 0x419008: patterns the orders use but the file lacks
        npat = max(orders) + 1
    state = {'maxinst': 0, 'used': [0] * 256}
    pats = []
    for i in range(npat):
        if i < f['npatterns']:
            p = f['patterns'][i]
            raw = d[p['offset'] + p['hdr']:p['offset'] + p['hdr'] + p['packed']]
            rows = p['rows']
        else:
            raw, rows = b'', 64                  # 0x4191d1: 64 empty rows
        pats.append({'rows': rows, 'data': convert_pattern(raw, rows, f['channels'], state)})
    m['patterns'] = pats
    ninst = max(f['ninstruments'], state['maxinst'] + 1)
    insts = []
    for i in range(ninst):
        ins = Instrument()
        fi = f['instruments'][i] if i < f['ninstruments'] else None
        nfile = fi['nsamples'] if fi else 0
        nkey = 0
        if nfile:
            nkey = max(fi['keymap']) + 1
        if state['used'][i]:
            n = nkey if nkey > 0 else 1
        else:
            n = 0
        ins.n = n
        ins.used = state['used'][i]
        ins.keymap = fi['keymap'] if (fi and nfile > 1) else None
        ins.volenv = ins.panenv = None
        ins.fadeout = 0
        if nfile:
            if fi['voltype'] & 1:
                ins.volenv = make_env(fi['nvol'], fi['volsus'] if fi['voltype'] & 2 else 0xff,
                                      (fi['volls'], fi['volle']) if fi['voltype'] & 4 else (0xff, 0xff), fi['volenv'])
            if fi['pantype'] & 1:
                ins.panenv = make_env(fi['npan'], fi['pansus'] if fi['pantype'] & 2 else 0xff,
                                      (fi['panls'], fi['panle']) if fi['pantype'] & 4 else (0xff, 0xff), fi['panenv'])
            ins.fadeout = fi['fadeout']
        ins.samples = []
        for s in range(n):
            sm = Sample()
            sm.handle = 0
            if s < nfile:
                h = fi['samples'][s]
                sm.length = h['length']
                sm.loopstart = h['loopstart']
                sm.loopend = h['loopstart'] + h['looplen']
                sm.volume = h['volume']
                sm.panning = h['panning']
                sm.relnote = h['relnote']
                sm.finetune = h['finetune']
                sm.type = 2 if h['type'] & 0x10 else 1
                lt = h['type'] & 3
                sm.loop, sm.looptype = (1, 1) if lt == 1 else (1, 2) if lt == 2 else (0, 0)
                if sm.loopend == sm.loopstart:
                    sm.loop, sm.looptype = 0, 0
                sm.raw = h['raw']
            else:
                sm.length = sm.loopstart = sm.loopend = sm.volume = sm.panning = 0
                sm.relnote = sm.finetune = 0
                sm.type = 0
                sm.loop = sm.looptype = 0
                sm.raw = b''
            ins.samples.append(sm)
        insts.append(ins)
    m['instruments'] = insts
    # sample data and AddSample, in instrument / sample order (0x419b75..)
    for ins in insts:
        for sm in ins.samples:
            if sm.length == 0:
                sm.type = 0
            data = b''
            if sm.type == 2:                      # 16-bit: converted to 8-bit unsigned (high byte + 0x80)
                acc = 0
                out = bytearray()
                for k in range(sm.length >> 1):
                    acc = (acc + struct.unpack_from('<h', sm.raw, 2 * k)[0])
                    out.append(((acc >> 8) + 0x80) & 0xff)
                data = bytes(out)
                sm.type = 1
                sm.length >>= 1
                sm.loopstart >>= 1
                sm.loopend >>= 1
            elif sm.type == 1:
                acc = 0
                out = bytearray()
                for b in sm.raw[:sm.length]:
                    acc = (acc + s8(b)) & 0xff
                    out.append((acc + 0x80) & 0xff)
                data = bytes(out)
            sm.data = data                         # unsigned 8-bit, as MIDAS keeps them
            sm.handle = sd.add_sample(data if sm.type else b'', sm.type, sm.length, sm.loop, sm.loopstart,
                                      sm.loopend, sm.looptype)
    return m


def make_env(n, sus, loop, points):
    """The GMP envelope (0x34 bytes): n, sustain (0xff none), loop start, loop end (0xff none), 12 x (x, y) words."""
    return {'n': n, 'sus': s8(sus), 'ls': s8(loop[0]), 'le': s8(loop[1]),
            'x': [s16(p[0]) for p in points], 'y': [s16(p[1]) for p in points],
            'raw': bytes((n, sus & 0xff, loop[0] & 0xff, loop[1] & 0xff)) + b''.join(
                struct.pack('<HH', p[0], p[1]) for p in points)}


def env_x(env, k):
    """x of point k as the code reads it (k may be -1 or n: the bytes before / after the points)."""
    return s16(struct.unpack_from('<H', env['raw'] + bytes(8), 4 + 4 * k)[0]) if k >= -1 else 0


def env_y(env, k):
    return s16(struct.unpack_from('<H', env['raw'] + bytes(8), 6 + 4 * k)[0]) if k >= -1 else 0


# ======================================================================================================================
# GF1 port access
# ======================================================================================================================
class Gf1Port:
    """Register writes as run_vivid.py logs them; reads answered like its Gus class (or by a GF1 model)."""

    def __init__(self, answers='capture'):
        self.log = []                # (stamp, text)
        self.stamp = 0
        self.voice = 0
        self.regs = {}
        self.answers = answers
        self.listeners = []          # objects with write(voice, reg, value, size)

    def select(self, v):
        self.voice = v & 31

    def w8(self, reg, val):
        val &= 0xff
        self._w(reg, val, 1)

    def w16(self, reg, val):
        val &= 0xffff
        self._w(reg, val, 2)

    def _w(self, reg, val, size):
        if reg in (0x43, 0x44):
            return
        if reg >= 0x40:
            self.log.append((self.stamp, 'g r%02x %x' % (reg, val)))
        else:
            self.regs[(self.voice, reg)] = val
            self.log.append((self.stamp, 'v%d r%02x %x' % (self.voice, reg, val)))
        for l in self.listeners:
            l.write(self.voice, reg, val, size)

    def port(self, p, val):
        self.log.append((self.stamp, 'p%03x %x' % (p, val)))
        for l in self.listeners:
            l.port(p, val)

    def wait(self):
        if self.answers != 'capture':
            self.answers.wait()

    def r16(self, reg):
        if self.answers != 'capture':
            return self.answers.read(self.voice, reg) & 0xffff
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg == 0x80:
            v |= 1
        if reg == 0x8d:
            v |= 1
        return v & 0xffff

    def r8(self, reg):
        if self.answers != 'capture':
            return self.answers.read(self.voice, reg) & 0xff
        v = self.regs.get((self.voice, reg - 0x80), 0)
        if reg == 0x80:
            v |= 1
        if reg == 0x8d:
            v |= 1
        return v & 0xff


# ======================================================================================================================
# GUS sound device (0x41b1a0..0x41cd10): voice records at 0x8239a4 (0x4c bytes), sample records at [0x824324]
# ======================================================================================================================
class Voice:
    def __init__(self):
        self.flags = 0          # +0x00: 1 stop, 2 start, 4 volume, 8 rate, 0x10 sample, 0x40 pan, 0x80 mute, 0x100 release
        self.sample = 0         # +0x04 handle
        self.playing = 0        # +0x08 handle being played
        self.fc = 0             # +0x0c
        self.rate = 0           # +0x10 Hz
        self.volume = 0         # +0x14 0..64
        self.surround = 0       # +0x18
        self.stype = 0          # +0x1c 1 = 8 bit, 2 = 16 bit
        self.loopmode = 0       # +0x20
        self.loop1 = 0          # +0x24 0 none, 1 forward, 2 bidi
        self.loop2 = 0          # +0x28
        self.start = 0          # +0x2c DRAM
        self.lstart = 0         # +0x30
        self.lend = 0           # +0x34
        self.l2start = 0        # +0x38
        self.l2end = 0          # +0x3c
        self.pan = 0            # +0x40 -64..63 (0x80 surround)
        self.gf1pan = 0         # +0x44
        self.active = 0         # +0x48


class GusDevice:
    def __init__(self, port, mem=0x100000):
        self.p = port
        self.mem = mem
        self.dram = bytearray(1 << 20)
        self.reset_state()

    def reset_state(self):
        self.voices = [Voice() for _ in range(32)]
        self.samples = {}        # handle -> dict
        self.next_handle = 1     # [0x823964]
        self.max_handle = 0      # [0x823978]
        self.nchan = 0           # [0x823950]
        self.outrate = 0         # [0x82395c]
        self.updrate = 0         # [0x823960]
        self.master = 0x40       # [0x823970]
        self.gvol_changed = 0    # [0x823974]
        self.open = 0            # [0x82399c]
        self.mute = 0            # [0x823968]
        self.paused = 0          # [0x82396c]
        self.mono = 0            # [0x823990]
        self.alloc_next = 0      # the DRAM heap (0x41cb45): one free block from 0, samples taken in order

    # -- 0x41b436 reset, 0x41b1b5 Detect, 0x41b267 Init, 0x41b45d Close --------------------------------------------
    def reset(self):
        self.p.w8(0x4c, 0)
        self.p.w8(0x4c, 1)

    def detect(self):
        self.reset()
        self.dram[0] = 0x55                       # the memory test: 0x55 / 0xaa at the start of bank 0
        self.dram[1] = 0xaa

    def init(self, mode=0):
        self.reset_state()
        self.mono = mode & 1
        self.reset()
        for bank in range(self.mem >> 18):           # the memory test of every 256 KB bank: 0x55 / 0xaa
            self.dram[bank << 18] = 0x55
            self.dram[(bank << 18) + 1] = 0xaa
        self.p.port(0x220, 3)
        self.p.w8(0x0e, 0xdf)
        for v in range(32, 0, -1):
            self.p.select(v)
            self.p.w8(0x00, 3)
            self.p.w16(0x09, 0x500)
            self.p.w8(0x0c, 8)
            self.p.w8(0x0d, 3)
            self.p.w8(0x06, 0x3f)
        self.p.w8(0x4c, 3)
        self.p.port(0x220, 0)

    def close(self):
        self.reset()
        self.p.port(0x220, 0)

    # -- 0x41b4c3 OpenChannels, 0x41b5ce CloseChannels, 0x41b5ea ClearChannels -----------------------------------------
    def open_channels(self, n):
        self.nchan = n
        self.mute = 0
        self.paused = 0
        nv = max(n, 14)
        self.outrate = rep.rd16(GUS_RATES + 2 * (nv - 14))
        self.p.w8(0x0e, (nv - 1) | 0xc0)
        self.voices = [Voice() for _ in range(32)]
        for v in range(n):
            self.p.select(v)
            self.p.w8(0x0c, 8)
            self.voices[v].pan = 0
            self.voices[v].flags = 1
        self.open = 1
        self.p.port(0x220, 0)

    def clear_channels(self):
        for v in range(self.nchan):
            self.p.select(v)
            self.p.w8(0x00, 3)
            self.p.w8(0x00, 3)
            self.p.w8(0x0c, 8)
            r = self.voices[v]
            r.pan = 0
            r.sample = 0
            r.playing = 0
            r.flags = 1

    def close_channels(self):
        self.clear_channels()
        self.open = 0
        self.nchan = 0

    # -- the per-channel calls ------------------------------------------------------------------------------------------
    def set_rate(self, ch, rate):                       # 0x41b99f
        r = self.voices[ch]
        if r.rate != rate:
            r.rate = rate
            r.fc = tdiv(s32(rate << 10), self.outrate) & ~1 & 0xffffffff
            r.flags |= 8

    def play_sound(self, ch, rate):                     # 0x41b80c
        r = self.voices[ch]
        if r.rate != rate:
            r.rate = rate
            r.fc = tdiv(s32(rate << 10), self.outrate) & ~1 & 0xffffffff
            r.flags |= 8
        h = r.sample
        if h == 0 or h > self.max_handle:
            return 0x13
        s = self.samples[h]
        if s['length'] == 0:
            self.stop_sound(ch)
            return 0
        r.start = s['addr']
        r.flags = (r.flags & ~0x101) | 2
        r.active = 1
        return 0

    def release_sound(self, ch):                        # 0x41b92a
        self.voices[ch].flags |= 0x100

    def stop_sound(self, ch):                           # 0x41b951
        r = self.voices[ch]
        r.flags = (r.flags & ~2) | 1

    def set_volume(self, ch, vol):                      # 0x41ba68
        vol &= 0xffffffff
        if vol > 0x40:
            vol = 0x40
        r = self.voices[ch]
        if r.volume != vol:
            r.volume = vol
            r.flags |= 4

    def set_sample(self, ch, h):                        # 0x41baf1
        if h == 0 or h > self.max_handle:
            return 0x13
        r = self.voices[ch]
        s = self.samples[h]
        if r.sample == h:
            return 0
        r.sample = h
        if s['length'] == 0:
            self.stop_sound(ch)
            return 0
        r.stype = s['type']
        r.start = s['addr']
        r.loop1 = s['loop1']
        r.loop2 = s['loop2']
        r.loopmode = s['mode']
        a = s['addr']
        if s['mode'] not in (0, 4):
            r.l2end = a + s['l2end']
            r.l2start = a + s['l2start']
            r.lend = a + s['l1end']
            r.lstart = a + s['l1start']
        else:
            r.lend = a + s['length']
            r.lstart = a
        r.flags |= 0x10
        return 0

    def set_position(self, ch, pos):                    # 0x41bc76
        r = self.voices[ch]
        h = r.sample
        if h == 0:
            return 0
        s = self.samples[h]
        if s['type'] == 2:
            pos *= 2
        if s['length'] < pos:
            if s['mode'] != 5:
                self.stop_sound(ch)
                return 0
            pos = s['l1start']
        r.start = pos + s['addr']
        r.flags = (r.flags & ~1) | 2
        r.active = 1
        return 0

    def set_panning(self, ch, pan):                     # 0x41be67
        self.p.select(ch)
        r = self.voices[ch]
        if r.pan == pan:
            return
        r.pan = pan
        if self.mono:
            return
        if pan == 0x80:
            r.gf1pan = 0                                # surround: never with these modules
            r.flags |= 0x40
            return
        r.surround = 0
        g = pan >> 3
        if g < 0:
            g += 1
        r.gf1pan = g + 7
        r.flags |= 0x40
        v2 = ch + self.nchan
        if v2 < 32:
            self.voices[v2].surround = 0
            self.voices[v2].flags |= 0x80
        self.gvol_changed = 1

    def set_upd_rate(self, rate):                       # 0x41c42b
        self.updrate = rate

    # -- 0x41c001 AddSample, 0x41cb45 DRAM allocation, 0x41c11b upload ---------------------------------------------------
    def add_sample(self, data, stype, length, loop, l1start, l1end, l1type, l2start=0, l2end=0, l2type=0):
        if stype == 0:
            length = 0
        h = self.next_handle
        s = {'type': stype or 1, 'length': length, 'mode': loop, 'loop1': l1type, 'l1start': l1start,
             'l1end': l1end, 'loop2': l2type, 'l2start': l2start, 'l2end': l2end, 'addr': 0}
        if length:
            size = (length & ~0x1f) + 0x40
            addr = self.alloc_next
            self.alloc_next += size
            s['addr'] = addr
            signed = bytes(b ^ 0x80 for b in data[:length]) if stype == 1 else bytes(data[:length])
            self.dram[addr:addr + length] = signed
            a = addr + length
            pad = 0x40 - (a & 0x1f)
            if loop in (0, 2, 4):                       # no loop: the last byte repeated
                self.dram[a:a + pad] = bytes([signed[-1]]) * pad
            else:                                       # loop: the start of the loop again
                ls = l2start if (loop == 3 and l2end) else l1start
                self.dram[a:a + pad] = (signed[ls:] + bytes(pad))[:pad]
        self.samples[h] = s
        nh = h
        while nh in self.samples:
            nh += 1
        self.next_handle = nh
        if self.max_handle < h:
            self.max_handle = h
        return h

    # -- 0x41c43e Play: the register writes, once per player tick before the player runs ------------------------------
    def play(self):
        if self.paused or not self.open:
            return 0
        p = self.p
        for v in range(self.nchan):
            r = self.voices[v]
            if not r.active:
                continue
            p.select(v)
            if r.flags & 3:
                r.flags &= ~1
                p.w8(0x07, 0x15)
                p.w8(0x0d, 0x40)
                p.w8(0x00, 3)
                p.w8(0x00, 3)
                p.w8(0x0d, 0x40)
            elif self.gvol_changed or r.flags & 4:
                r.flags &= ~4
                cur = p.r16(0x89) & 0xff00
                vol = 0 if r.flags & 0x80 else r.volume
                tgt = rep.rd16(GUS_VOLTAB + 2 * (((vol * (self.master & 0xff)) & 0xffff) >> 6))
                if r.surround:
                    tgt = (tgt - 0x1000) & 0xffff
                tgt &= 0xff00
                if cur != tgt:
                    if cur < tgt:
                        lo, hi, d = cur >> 8, tgt >> 8, 0
                    else:
                        lo, hi, d = tgt >> 8, cur >> 8, 0x40
                    p.w8(0x0d, 3)
                    p.w8(0x0d, 3)
                    p.w8(0x07, lo)
                    p.w8(0x08, hi)
                    p.w8(0x0d, d)
                    p.w8(0x0d, d)
            if r.flags & 8:
                r.flags &= ~8
                p.w16(0x01, r.fc)
            if r.flags & 0x40:
                r.flags &= ~0x40
                p.w8(0x0c, r.gf1pan)
            if r.flags & 0x10 and r.loopmode in (4, 5) and not r.flags & 2:
                raise NotImplementedError('Amiga loop emulation (MOD only)')
        while True:
            retry = False
            for v in range(self.nchan):
                r = self.voices[v]
                if not r.active or not r.flags & 2:
                    continue
                p.select(v)
                if not p.r8(0x8d) & 1:
                    retry = True
                    continue
                if r.flags & 0x10:
                    e = self.addr(r, r.lend)
                    p.w16(0x04, e >> 7)
                    p.w16(0x05, e << 9)
                    s = self.addr(r, r.lstart)
                    p.w16(0x02, s >> 7)
                    p.w16(0x03, s << 9)
                    r.flags &= ~0x10
                p.w8(0x00, 3)
                a = self.addr(r, r.start)
                p.w16(0x0a, a >> 7)
                p.w16(0x0b, a << 9)
                p.w16(0x0b, a << 9)
                p.w16(0x0a, a >> 7)
                p.w8(0x00, 3)
                r.playing = r.sample
                ctl = {0: 0, 1: 8}.get(r.loop1, 0x18)
                if r.stype == 2:
                    ctl |= 4
                p.w8(0x00, ctl)
                vol = 0 if r.flags & 0x80 else r.volume
                hi = rep.rd16(GUS_VOLTAB + 2 * (((vol * (self.master & 0xff)) & 0xffff) >> 6)) >> 8
                if r.surround:
                    hi = (hi - 0x10) & 0xff
                p.w8(0x08, hi)
                p.w8(0x0d, 0)
                p.w8(0x0d, 0)
                p.w8(0x00, ctl)
                r.flags &= ~6
            if not retry:
                break
            p.wait()                                    # the loop polls again: time passes on the card
        self.gvol_changed = 0
        return 1

    @staticmethod
    def addr(r, a):
        if r.stype == 2:
            return ((a >> 1) & 0x1ffff) ^ (a & 0xc0000)
        return a


# ======================================================================================================================
# Generic Module Player for XM (0x40d820..0x40f26a, commands 0x41e930..0x41f437)
# ======================================================================================================================
class Channel:
    def __init__(self, sdch):
        self.period = 0         # +0x00
        self.inst = -1          # +0x04 current instrument
        self.newinst = -1       # +0x08 instrument of the last instrument byte
        self.target = 0         # +0x0c tone portamento target period
        self.offset = 0         # +0x10 sample offset (bytes)
        self.sample = 0xff      # +0x14 sample of the instrument
        self.sdch = sdch        # +0x15
        self.volbyte = 0        # +0x16 volume column (converted)
        self.note = 0xff        # +0x17 last note byte (octave << 4 | note, 0xfe key off)
        self.info = 0           # +0x18 last non-zero command info (all commands share it)
        self.vol = 0x40         # +0x1c volume
        self.cmd = 0            # +0x1d
        self.rawinfo = 0        # +0x1e
        self.portaspeed = 0     # +0x1f
        self.vol2 = 0x40        # +0x20 volume used this tick
        self.pan = 0x80         # +0x21 0..255
        self.vpos = 0           # +0x22 volume envelope position
        self.ppos = 0           # +0x26 panning envelope position
        self.fade = 0x10000     # +0x2a fadeout volume
        self.vdone = 0          # +0x2e volume envelope held (sustain) flag
        self.pdone = 0          # +0x2f
        self.release = 0        # +0x30 key off
        self.retrig = 0         # +0x31
        self.final = 0          # +0x32
        self.lastnote = 0xff    # +0x33
        self.vibspeed = 0       # +0x34
        self.vibdepth = 0       # +0x35
        self.vibpos = 0         # +0x36
        self.offmem = 0         # +0x37 sample offset memory
        self.looprow = 0        # +0x38
        self.loopcnt = 0        # +0x39
        self.flags = 0          # +0x3c 1 note, 2 volume, 4 instrument, 8 command, 0x10 delayed note


# [0x844cb4]: the module type the GMP last worked on (set by the row, tick and update routines, never cleared). It is
# still 0 when part I's gmpPlaySong sets the channels' initial panning, so 0x40f000 takes its MOD/S3M branch there.
CUR_TYPE = [0]


class Gmp:
    MAXPER = 0xd600             # handle +0x34 for XM
    MINPER = 0x39               # handle +0x38
    SHIFT = 2                   # handle +0x3c
    MAXVOL = 0x40               # handle +0x40

    def __init__(self, module, sd, timer_rate=None):
        self.m = module
        self.sd = sd
        self.timer_rate = timer_rate   # tmrSetUpdRate (0x410e21) or None
        self.tempo = 0
        self.set_tempo(125)            # gmpInit (0x40d820)

    # 0x40ebc8
    def set_tempo(self, bpm):
        self.tempo = bpm
        rate = bpm * 40
        self.sd.set_upd_rate(rate)
        if self.timer_rate:
            self.timer_rate(rate)

    # 0x40d8b0 gmpPlaySong(module, -1, -1, -1, -1, channels from 0, &handle)
    def play_song(self):
        m = self.m
        self.pos = 0                                   # +0x0c
        self.pattern = m['orders'][0]                  # +0x10
        self.row = 0                                   # +0x14
        self.loopend = m['length'] - 1                 # +0x18
        self.restart = m['restart']                    # +0x1c
        self.speed = m['speed']                        # +0x24
        self.tick = 0                                  # +0x28
        self.patdelay = 0                              # +0x2c
        self.data = None                               # +0x08 (None = 0: read the pattern from its start + row)
        self.sync = -1                                 # +0x54
        self.info = (0, self.pattern, 0)
        self.set_tempo(m['tempo'])
        self.chans = []
        for i in range(m['nchan']):
            c = Channel(i)
            self.chans.append(c)
            self.cur = c
            self.set_panning(0x80)                     # module panning table: all 0x80

    def setup_rate_callback(self, cb):                 # 0x40d87c
        self.timer_rate = cb
        if cb:
            cb(self.tempo * 40)

    # -- helpers (0x40ec10 .. 0x40f0a4) ----------------------------------------------------------------------------
    def ins(self, i):
        return self.m['instruments'][i]

    def note_period(self, note):                        # 0x40ec10 (XM)
        c = self.cur
        s = self.ins(c.inst).samples[c.sample]
        n = (note & 0xf) + (note >> 4) * 12 + s.relnote
        n = max(0, min(0x76, n))
        if self.m['linear']:
            return 0x1e00 - n * 64 - tdiv(s.finetune, 2)
        ft = s.finetune
        oct_, ni = n // 12, n % 12
        q = tdiv(ft, 16)
        p1 = rep.rd32(XM_AMIGA + 4 * (ni * 8 + q))
        if ft < 0:
            ft = -ft
            i2 = ni * 8 + q - 1
        else:
            i2 = ni * 8 + q + 1
        p2 = rep.rd32(XM_AMIGA + 4 * i2)
        fr = ft & 0xf
        return s32((p1 * (16 - fr) + p2 * fr) * 2) >> oct_

    def period_rate(self, per):                         # 0x40ee6c (XM)
        if per == 0:
            raise RuntimeError('period 0 (error 0x11)')
        per &= 0xffffffff
        if self.m['linear']:
            return rep.rd32(XM_LINEAR + 4 * (per % 0x300)) >> ((per // 0x300) & 31)
        return 0xda7790 // per

    def set_period(self, per):                          # 0x40eef4
        if per == 0:
            return
        per &= 0xffffffff
        if per < self.MINPER:
            per = self.MINPER
        if per > self.MAXPER:
            per = self.MAXPER
        self.cur.period = per
        self.sd.set_rate(self.cur.sdch, self.period_rate(per))

    def change_period(self, per):                       # 0x40ef58 (not stored)
        per &= 0xffffffff
        if per < self.MINPER:
            per = self.MINPER
        if per > self.MAXPER:
            per = self.MAXPER
        self.sd.set_rate(self.cur.sdch, self.period_rate(per))

    def set_volume(self, v):                            # 0x40efb0
        v = s32(v)
        if v < 0:
            v = 0
        if v > self.MAXVOL:
            v = self.MAXVOL
        self.cur.vol = self.cur.vol2 = v

    def set_panning(self, p):                           # 0x40f000
        p = s32(p)
        if CUR_TYPE[0] == 3:
            p = max(0, min(0xff, p))
        else:                                           # MOD / S3M branch: -64..64 (0xa4: surround)
            p = 0x80 if p == 0xa4 else max(-0x40, min(0x40, p - 0x40))
            self.sd.set_panning(self.cur.sdch, p)
        self.cur.pan = p & 0xff

    def new_note(self):                                 # 0x40f0a4
        c = self.cur
        c.vibpos = 0
        if c.inst != -1 and c.sample != 0xff:
            if c.note == 0xfe:
                c.release = 1
                c.vdone = 0
                c.pdone = 0
                if self.ins(c.inst).volenv is None:
                    c.vol = c.vol2 = 0
            else:
                c.release = 0
                c.lastnote = c.note
                c.period = self.note_period(c.note)
                self.sd.play_sound(c.sdch, self.period_rate(c.period))
                if c.offset:
                    self.sd.set_position(c.sdch, c.offset)
        c.flags &= ~1

    # -- the timer callback (0x40de40) -----------------------------------------------------------------------------
    def play(self):
        CUR_TYPE[0] = 3
        self.tick += 1
        if self.tick >= self.speed:
            self.tick = 0
            self.play_row()
        else:
            self.tick_commands()
        self.update()

    # 0x40e3dc
    def play_row(self):
        m = self.m
        self.info = (self.pos, self.pattern, self.row)
        if self.patdelay:
            for c in self.chans:
                self.cur = c
                if c.flags & 8:
                    ROW[c.cmd](self, c.rawinfo)
            self.tick_commands()
            self.patdelay -= 1
            return
        if self.data is None:
            pd = m['patterns'][self.pattern]['data']
            i = 0
            for _ in range(self.row):
                while True:
                    b = pd[i]
                    i += 1
                    if b & 0x20:
                        i += 2
                    if b & 0x40:
                        i += 1
                    if b & 0x80:
                        i += 2
                    if b == 0:
                        break
            self.data = (pd, i)
        for c in self.chans:
            c.flags &= ~0xf
        pd, i = self.data
        n = m['nchan']
        while True:
            b = pd[i]
            i += 1
            ch = b & 0x1f
            if ch >= n:
                if b & 0x20:
                    i += 2
                if b & 0x40:
                    i += 1
                if b & 0x80:
                    i += 2
            else:
                c = self.chans[ch]
                if b & 0x20:
                    nt, inst = pd[i], pd[i + 1]
                    i += 2
                    if nt != 0xff:
                        c.note = nt
                        c.flags |= 1
                    if inst != 0xff:
                        c.flags |= 4
                        c.newinst = inst
                if b & 0x40:
                    c.volbyte = pd[i]
                    c.flags |= 2
                    i += 1
                if b & 0x80:
                    c.cmd = pd[i]
                    c.rawinfo = pd[i + 1]
                    c.flags |= 8
                    i += 2
            if b == 0:
                break
        self.data = (pd, i)
        for c in self.chans:
            self.cur = c
            if c.flags & 4:
                c.vpos = c.vdone = c.ppos = c.pdone = 0
                c.fade = 0x10000
                c.release = 0
                if (c.flags & 8 and c.cmd == 4) or (c.flags & 1 and c.note == 0xfe):
                    ins = self.ins(c.inst)
                else:
                    c.inst = c.newinst
                    ins = self.ins(c.inst)
                    if c.flags & 1 and c.note < 0xfe:
                        k = max(0, min(0x5f, (c.note >> 4) * 12 + (c.note & 0xf)))
                        c.sample = ins.keymap[k] if ins.keymap else 0
                if c.sample != 0xff:
                    s = ins.samples[c.sample]
                    v = s.volume if s.volume >= 0 else 0
                    if v > self.MAXVOL:
                        v = self.MAXVOL
                    c.vol = c.vol2 = v
                    self.set_panning(s.panning)
                    c.offset = 0
                    if c.flags & 1 and s.handle:
                        self.sd.set_sample(c.sdch, s.handle)
            if c.flags & 2:
                v = c.volbyte
                if v <= 0x40:
                    c.vol = c.vol2 = min(v, self.MAXVOL)
                else:
                    vol_row(self, c)
            if c.flags & 8:
                if c.rawinfo:
                    c.info = c.rawinfo
                if c.cmd:
                    if c.cmd >= 0x1f:
                        raise RuntimeError('command %#x' % c.cmd)
                    ROW[c.cmd](self, c.info)
            if c.flags & 1:
                if c.inst != -1:
                    ins = self.ins(c.inst)
                    if c.note < 0xfe:
                        k = max(0, min(0x5f, (c.note >> 4) * 12 + (c.note & 0xf)))
                        c.sample = ins.keymap[k] if ins.keymap else 0
                    if c.sample != 0xff and c.note != 0xfe:
                        c.inst = c.newinst
                        c.offset = 0
                        s = self.ins(c.newinst).samples[c.sample]
                        if s.handle:
                            self.sd.set_sample(c.sdch, s.handle)
                self.new_note()
            else:
                if c.cmd == 5 and c.flags & 8:
                    pass
                elif (c.volbyte & 0xf0) == 0xb0:
                    pass
                else:
                    self.change_period(c.period)
        if self.data is not None:
            self.row += 1
            if self.row >= m['patterns'][self.pattern]['rows']:
                self.row = 0
                self.advance_order()
                self.data = None

    def advance_order(self):
        o = self.m['orders']
        while True:
            self.pos += 1
            if self.pos > self.loopend or o[self.pos] == 0xffff:
                self.pos = self.restart
            if o[self.pos] != 0xfffe:
                break
        self.pattern = o[self.pos]

    # 0x40eaf8
    def tick_commands(self):
        for c in self.chans:
            self.cur = c
            if c.flags & 2 and c.volbyte > 0x40:
                vol_tick(self, c)
            if c.flags & 8:
                TICK[c.cmd](self, c.info)

    # 0x40dec4
    def update(self):
        gv = self.m['gv']
        for c in self.chans:
            self.cur = c
            vol = c.vol2
            ins = self.ins(c.inst) if c.inst != -1 else None
            if ins is not None and ins.volenv is not None:
                env = ins.volenv
                vol = (vol * c.fade) >> 16
                if c.release:
                    c.fade -= ins.fadeout
                    if c.fade < 0:
                        c.fade = 0
                c.vpos, value, c.vdone = self.envelope(env, c.vpos, c.vdone, c.release, -1)
                vol = (value * vol) >> 14
                vol = max(0, min(0x40, vol))
            vol = (vol * gv) >> 6
            c.final = vol & 0xff
            self.sd.set_volume(c.sdch, vol)
            pan = c.pan
            if ins is not None and ins.panenv is not None:
                c.ppos, value, c.pdone = self.envelope(ins.panenv, c.ppos, c.pdone, c.release, 0)
                e = (value >> 8) - 0x20
                pan = pan + tdiv((0x80 - abs(pan - 0x80)) * e, 32)
                if pan < 0:
                    pan = 0xff
                if pan > 0xff:
                    pan = 0xff
            pan = tdiv(pan - 0x80, 2)
            self.sd.set_panning(c.sdch, pan)

    @staticmethod
    def envelope(env, pos, done, release, notfound):
        """One envelope step of 0x40dec4: value (y << 8, interpolated) at pos, then the position advance."""
        n = env['n']
        idx = -2
        for k in range(n):
            if env_x(env, k) >= pos:
                idx = k
                break
        if idx == -2:
            idx = n - 1 if notfound == -1 else n       # volume: last point; panning: one past the last
        if env_x(env, idx) == pos:
            value = env_y(env, idx) << 8
        else:
            y0, y1 = env_y(env, idx - 1), env_y(env, idx)
            x0, x1 = env_x(env, idx - 1), env_x(env, idx)
            value = (y0 << 8) + tdiv((y1 - y0) << 8, x1 - x0) * (pos - x0)
        sus = env['sus']
        if env_x(env, sus) == pos and not release and sus != -1:
            done = 1
        if not done:
            if env_x(env, n - 1) > pos:
                pos += 1
            le = env['le']
            if env_x(env, le) <= pos and le != -1:
                pos = env_x(env, env['ls'])
            if env_x(env, sus) == pos and not release and sus != -1:
                done = 1
        return pos, value, done


# ---- commands (0x41e930..) -------------------------------------------------------------------------------------------
def c_none(g, info):
    pass


def c_speed(g, info):                    # 0x41e934
    c = g.cur
    if c.rawinfo:
        g.speed = c.rawinfo
    else:
        g.pos = g.restart
        g.pattern = g.m['orders'][g.pos]
        g.data = None
        g.row = 0


def c_tempo(g, info):                    # 0x41e994
    g.set_tempo(g.cur.rawinfo)


def c_setvol(g, info):                   # 0x41e9a8
    g.set_volume(g.cur.rawinfo)


def c_globalvol(g, info):                # 0x41e9bc
    g.m['gv'] = min(g.cur.rawinfo, 0x40)


def c_setpan(g, info):                   # 0x41e9dc
    g.set_panning(g.cur.rawinfo)


def c_volslide(g, info):                 # 0x41e9f0 (XM branch: the signed info added once)
    g.set_volume(g.cur.vol + s8(info))


def c_gvolslide(g, info):                # 0x41eaf0
    v = (g.m['gv'] + info) & 0xff
    if v > 0x7f:
        v = 0
    if v > 0x40:
        v = 0x40
    g.m['gv'] = v


def c_panslide(g, info):                 # 0x41eb1c
    g.set_panning(g.cur.pan + s8(info))


def c_portaup(g, info):                  # 0x41eb38
    g.set_period(s32(g.cur.period - (info << g.SHIFT)))


def c_portadown(g, info):                # 0x41ebb0
    g.set_period(s32(g.cur.period + (info << g.SHIFT)))


def c_fineporta(g, info):                # 0x41ec28
    g.set_period(s32(g.cur.period + (s8(info) << g.SHIFT)))


def c_xfineporta(g, info):               # 0x41ec4c
    g.set_period(s32(g.cur.period + s8(info)))


def toneporta_tick(g, speed):            # 0x41ecd4
    c = g.cur
    if c.target == 0:
        return
    if speed:
        c.portaspeed = speed & 0xff
    if c.period == c.target:
        return
    step = c.portaspeed << g.SHIFT
    if c.period < c.target:
        p = c.period + step
        g.set_period(c.target if p >= c.target else p)
    else:
        if c.period - c.target <= step:
            g.set_period(c.target)
        else:
            g.set_period(c.period - step)


def c_toneporta(g, info):                # 0x41ecc4 (tick)
    toneporta_tick(g, g.cur.rawinfo)


def c_toneporta_row(g, info):            # 0x41ecf0 (row, also volume column Fx)
    c = g.cur
    if c.flags & 1 and c.inst != -1 and c.sample != 0xff and c.note != 0xfe:
        c.target = g.note_period(c.note)
        c.flags &= ~1


def c_tpvolslide(g, info):               # 0x41ed38
    c_volslide(g, info)
    toneporta_tick(g, 0)


def vibrato_tick(g, x):                  # 0x41ed6c
    c = g.cur
    if x & 0xf:
        c.vibdepth = x & 0xf
    if x & 0xf0:
        c.vibspeed = (x >> 4) & 0xff
    d = (rep.rd(VIB_SINE + (c.vibpos & 0x1f), 1)[0] * c.vibdepth) >> (7 - g.SHIFT)
    if c.vibpos & 0x20:
        g.change_period(s32(c.period - d))
    else:
        g.change_period(s32(c.period + d))
    c.vibpos = (c.vibpos + c.vibspeed) & 0xff


def c_vibrato(g, info):                  # 0x41ed5c
    vibrato_tick(g, g.cur.rawinfo)


def c_vibvolslide(g, info):              # 0x41ede0
    c_volslide(g, info)
    vibrato_tick(g, 0)


def c_offset(g, info):                   # 0x41edec
    c = g.cur
    if c.rawinfo:
        c.offmem = c.rawinfo
    off = c.offmem << 8
    if c.flags & 1:
        c.offset += off
        g.new_note()


def c_retrig_row(g, info):               # 0x41ee3c
    g.cur.retrig = 1


def c_retrig(g, info):                   # 0x41ee48
    c = g.cur
    if c.rawinfo:
        if c.retrig >= (c.rawinfo & 0xf):
            g.sd.set_position(c.sdch, c.offset)
            c.retrig = 0
    c.retrig = (c.retrig + 1) & 0xff


def c_mretrig(g, info):                  # 0x41eeb8 (not used by the songs)
    raise NotImplementedError('multi retrig')


def c_jump(g, info):                     # 0x41ef60
    g.pos = g.cur.rawinfo
    if g.pos > g.loopend:
        g.pos = g.restart
    g.pattern = g.m['orders'][g.pos]
    g.data = None
    g.row = 0


def c_break(g, info):                    # 0x41efc8
    if g.data is not None:
        g.advance_order()
        g.data = None
    r = g.cur.rawinfo
    g.row = ((r >> 4) * 10 + (r & 0xf)) if r <= 0x63 else 0x3f


def c_notecut(g, info):                  # 0x41f080
    if g.cur.rawinfo == g.tick:
        g.set_volume(0)


def c_notedelay_row(g, info):            # 0x41f0ac
    c = g.cur
    if c.rawinfo and c.flags & 1:
        c.flags = (c.flags | 0x10) & ~1
    else:
        c.flags &= ~0x10


def c_notedelay(g, info):                # 0x41f0ec
    c = g.cur
    if c.rawinfo == g.tick and c.flags & 0x10:
        g.new_note()


def c_patdelay(g, info):                 # 0x41f120
    if g.patdelay == 0:
        g.patdelay = g.cur.rawinfo


def c_e8(g, info):                       # 0x41f144 (S3M-style 16 positions; not used)
    x = g.cur.rawinfo
    if x < 7:
        g.set_panning(x << 3)
    elif x > 8:
        g.set_panning(0x80 - (15 - x) * 8)
    else:
        g.set_panning(0x40)


def c_arpeggio(g, info):                 # 0x41f18c
    c = g.cur
    ni = c.note & 0xf
    oc = (c.note >> 4) & 0xf
    if c.inst == -1 or c.sample == 0xff or c.note == 0xfe:
        return
    t = g.tick % 3
    if t == 1:
        ni += (info >> 4) & 0xf
    elif t == 2:
        ni += info & 0xf
    if ni > 11:
        oc += 1
        ni -= 12
    g.change_period(g.note_period((oc << 4) | ni))


def c_sync(g, info):                     # 0x41f250
    g.sync = g.cur.rawinfo


def c_patloop(g, info):                  # 0x41f28c (XM branch)
    c = g.cur
    x = c.rawinfo
    to = -1
    if x == 0:
        c.looprow = g.row & 0xff
    elif c.loopcnt == 0:
        c.loopcnt = x
        to = c.looprow
    else:
        c.loopcnt -= 1
        if c.loopcnt > 0:
            to = c.looprow
    if to != -1:
        g.data = None
        g.row = to


def vol_row(g, c):                       # 0x41f328
    v = c.volbyte
    x = v & 0xf
    h = v & 0xf0
    if h == 0x80:
        g.set_volume(c.vol - x)
    elif h == 0x90:
        g.set_volume(c.vol + x)
    elif h == 0xa0:
        c.vibspeed = x
    elif h == 0xc0:
        g.set_panning((x << 4) | x)
    elif h == 0xf0:
        c_toneporta_row(g, 0)


def vol_tick(g, c):                      # 0x41f3a4
    v = c.volbyte
    x = v & 0xf
    h = v & 0xf0
    if h == 0x60:
        c_volslide(g, (-x) & 0xff)
    elif h == 0x70:
        c_volslide(g, x)
    elif h == 0xb0:
        vibrato_tick(g, x)
    elif h == 0xd0:
        g.set_panning(c.pan - x)
    elif h == 0xe0:
        g.set_panning(c.pan + x)
    elif h == 0xf0:
        toneporta_tick(g, x << 4)


# GMP command n -> row handler / tick handler (XM tables 0x824d68 / 0x824de4)
ROW = [c_none] * 31
TICK = [c_none] * 31
ROW[4] = ROW[6] = c_toneporta_row
ROW[9] = c_setpan
ROW[0xa] = c_offset
ROW[0xc] = c_jump
ROW[0xd] = c_setvol
ROW[0xe] = c_break
ROW[0xf] = c_speed
ROW[0x10] = c_tempo
ROW[0x11] = c_fineporta
ROW[0x12] = c_patloop
ROW[0x13] = c_e8
ROW[0x14] = c_retrig_row
ROW[0x15] = c_volslide
ROW[0x16] = c_notecut
ROW[0x17] = c_notedelay_row
ROW[0x18] = c_patdelay
ROW[0x19] = c_globalvol
ROW[0x1b] = c_retrig_row
ROW[0x1c] = c_sync
ROW[0x1d] = c_xfineporta
TICK[1] = c_arpeggio
TICK[2] = c_portaup
TICK[3] = c_portadown
TICK[4] = c_toneporta
TICK[5] = c_vibrato
TICK[6] = c_tpvolslide
TICK[7] = c_vibvolslide
TICK[0xb] = c_volslide
TICK[0x14] = c_retrig
TICK[0x16] = c_notecut
TICK[0x17] = c_notedelay
TICK[0x1a] = c_gvolslide
TICK[0x1b] = c_mretrig
TICK[0x1e] = c_panslide

ROW_ADDR = 0x824d68
TICK_ADDR = 0x824de4


def check_tables():
    """The handler tables above against the image (address -> handler)."""
    by_addr = {0x41e930: c_none, 0x41ecf0: c_toneporta_row, 0x41e9dc: c_setpan, 0x41edec: c_offset,
               0x41ef60: c_jump, 0x41e9a8: c_setvol, 0x41efc8: c_break, 0x41e934: c_speed, 0x41e994: c_tempo,
               0x41ec28: c_fineporta, 0x41f28c: c_patloop, 0x41f144: c_e8, 0x41ee3c: c_retrig_row,
               0x41e9f0: c_volslide, 0x41f080: c_notecut, 0x41f0ac: c_notedelay_row, 0x41f120: c_patdelay,
               0x41e9bc: c_globalvol, 0x41f250: c_sync, 0x41ec4c: c_xfineporta, 0x41f18c: c_arpeggio,
               0x41eb38: c_portaup, 0x41ebb0: c_portadown, 0x41ecc4: c_toneporta, 0x41ed5c: c_vibrato,
               0x41ed38: c_tpvolslide, 0x41ede0: c_vibvolslide, 0x41ee48: c_retrig, 0x41f0ec: c_notedelay,
               0x41eaf0: c_gvolslide, 0x41eeb8: c_mretrig, 0x41eb1c: c_panslide}
    for k in range(31):
        assert by_addr[rep.rd32(ROW_ADDR + 4 * k)] is ROW[k], ('row', k)
        assert by_addr[rep.rd32(TICK_ADDR + 4 * k)] is TICK[k], ('tick', k)
