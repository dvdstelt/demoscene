"""DSMI as INSIDE.EXE uses it, ported routine by routine (notes-music.md):

  Card       the capture's fake GUS (run_inside.py class Gus): logs every register write in full.gus's format,
             answers a voice-register read with the last value written (| 1 for 0x80 / 0x8d), DMA ends at once
  Driver     SDIGUS.DSD as loaded (0x1152bb5..): reset, setupChannels, the CDI functions AMP calls, the DRAM
             allocator and the DMA upload
  Amp        AMP 2.00 (0x40cb34..0x40e03e): ampInit, ampPlayModule, the poll, the player tick, the row reader, all
             24 effect handlers and the 12 per-tick handlers
  Timer      DSMI's timer service (0x40f5a1..0x40f6cc) and the demo's callback 0x400fcc

Tables are read from INSIDE.EXE (../../unpacked/inside_img.bin) by address and from SDIGUS.DSD (the volume table, file
offset 0x700c), as the remake would read them.
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
ORIG = os.path.normpath(os.path.join(RE, '..', 'original'))
IMG = open(os.path.join(RE, 'unpacked', 'inside_img.bin'), 'rb').read()


def img16(a, n):
    return list(struct.unpack_from('<%dH' % n, IMG, a - 0x400000))


SEMITONE = img16(0x40c82e, 132)      # frequency multiplier << 10 per note (note 60 = 1023, 72 = 2048)
PERIOD = img16(0x40c936, 127)        # period per note (note 60 = 0x1ac0 = 6848); freq = rate * 6848 / period
SINE = img16(0x40ca34, 64)           # 4095 * sin(pi * i / 64); vibrato / tremolo use entries 0..31
ARPEGGIO = img16(0x40cab4, 16)       # 1024 * 2^(n / 12)

DSD = open(os.path.join(ORIG, 'SDIGUS.DSD'), 'rb').read()
VOLTAB = list(struct.unpack_from('<65I', DSD, 0x700c))   # driver 0x11557ac: GF1 volume (16 bit) per volume 0..64


def rate_for(voices):
    """Driver table 0x1155878 (entries 14..32): the GF1's frame rate with n active voices."""
    return 617400 // voices


class Card:
    """The capture's fake GUS at 0x220 (run_inside.py class Gus), seen from the driver's port helpers."""

    def __init__(self, base=0x220):
        self.base = base
        self.voice = 0
        self.regs = {}
        self.glob = {}
        self.stamp = 0
        self.log = []          # (stamp, text) per write, as full.gus has them
        self.dram = bytearray(1 << 20)

    def select(self, v):                    # 0x1152c0c: out base+0x102
        self.voice = v & 31

    def port(self, p, val):                 # direct port writes (logged as p<port>)
        self.log.append((self.stamp, 'p%03x %x' % (p, val)))

    def _write(self, reg, val):
        if reg in (0x43, 0x44):
            return
        if reg >= 0x40:
            self.glob[reg] = val
            self.log.append((self.stamp, 'g r%02x %x' % (reg, val)))
        else:
            self.regs[(self.voice, reg)] = val
            self.log.append((self.stamp, 'v%d r%02x %x' % (self.voice, reg, val)))

    def w8(self, reg, val):                 # 0x1152c30: reg -> base+0x103, byte -> base+0x105
        self._write(reg, val & 0xff)

    def w16(self, reg, val):                # 0x1152cb6: reg -> base+0x103, word -> base+0x104
        self._write(reg, val & 0xffff)

    def r8(self, reg):                      # 0x1152d3f
        if 0x80 <= reg < 0x90:
            v = self.regs.get((self.voice, reg - 0x80), 0)
            if reg in (0x80, 0x8d):
                v |= 1
            if reg in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b):
                return (v >> 8) & 0xff
            return v & 0xff
        if reg == 0x41:
            return (self.glob.get(0x41, 0) & ~0x40) | 0x40     # the DMA has always finished
        return 0

    def r16(self, reg):                     # 0x1152d7a
        if 0x80 <= reg < 0x90:
            v = self.regs.get((self.voice, reg - 0x80), 0)
            if reg in (0x80, 0x8d):
                v |= 1
            return v & 0xffff
        return 0


class Voice:
    __slots__ = ('addr', 'freq', 'vol', 'mode', 'muted')

    def __init__(self):
        self.addr = self.freq = self.vol = self.mode = self.muted = 0


class Driver:
    """SDIGUS.DSD: GUS driver of DSMI (addresses as loaded in the capture: 0x1152bb5..0x11556c0)."""

    def __init__(self, card, port=0x220):
        self.c = card
        self.port = port
        self.voices = 14                    # [0x11561dc]
        self.master = 0x100                 # [0x11561c0]
        self.v = [Voice() for _ in range(32)]   # 0x1155fe8, 14 bytes each
        self.samples = []                   # 0x1156248: (handle, DRAM address), [0x11561b0] used
        self.blocks = []                    # DRAM allocator list 0x11557a0: [addr, size]
        self.dram_size = 0

    # -- helpers ---------------------------------------------------------------------------------------------
    def w8x2(self, reg, val):               # 0x1152c63: write, delay (0x1152bb5(1): 6 reads of base+0x107), write
        self.c.w8(reg, val)
        self.c.w8(reg, val)

    def w16x2(self, reg, val):              # 0x1152cea
        self.c.w16(reg, val)
        self.c.w16(reg, val)

    def ramp(self, vol):
        """0x1152e5e: ramp the selected voice's volume to vol (0..64) from where it is now."""
        vol = min(vol, 0x40)
        vol = (self.master * vol) >> 8
        target = VOLTAB[vol] >> 8
        self.w8x2(0x0d, 3)
        cur = (self.c.r16(0x89) & 0xffff) >> 8
        self.c.w16(0x09, cur << 8)
        if cur > target:
            lo, hi, down = target, cur, 1
        else:
            lo, hi, down = cur, target, 0
        self.c.w8(0x07, lo)
        self.c.w8(0x08, hi)
        self.c.w8(0x06, 0x3f)
        self.w8x2(0x0d, down << 6)

    def wait_ramp(self):                    # 0x1152f50: up to 20 reads of 0x8d until bit 0
        if hasattr(self.c, 'wait_voice'):   # a card model: the 20 reads take about one GF1 frame
            self.c.wait_voice()
            return
        for _ in range(20):
            if self.c.r8(0x8d) & 1:
                break

    def set_pos(self, regs, a):             # address -> GF1 (bits 19..7 to the high register, 6..0 << 9 low)
        self.c.w16(regs[0], (a >> 7) & 0x1fff)
        self.c.w16(regs[1], (a & 0x7f) << 9)

    def fc(self, freq):
        r = rate_for(self.voices)
        f = (((r >> 1) + ((freq << 9) & 0xffffffff)) // r) * 2
        return min(f, 0xfffe)

    def sample_addr(self, handle):          # 0x1153509
        for h, a in self.samples:
            if h == handle:
                return a
        return 0

    # -- init ------------------------------------------------------------------------------------------------
    def reset(self):
        """0x11546aa: GF1 reset with [0x11561dc] voices."""
        c = self.c
        c.w8(0x4c, 0)
        c.w8(0x4c, 1)
        c.port(self.port + 0x100, 3)
        c.port(self.port + 0x100, 0)
        c.w8(0x41, 0)
        c.w8(0x45, 0)
        c.w8(0x49, 0)
        c.w8(0x0e, 0xdf)                    # 32 voices while clearing
        for i in range(32):
            c.select(i)
            self.clear_voice(i)
            c.w8(0x06, 0)
            c.w8(0x0d, 3)
            c.w16(0x09, 0)
            c.w8(0x00, 3)
            c.w8(0x0c, 8)
        c.w8(0x0e, (self.voices - 1) | 0xc0)
        c.w8(0x4c, 3)
        c.port(self.port, 9)                # mix control: line in off, output on, latches on

    def clear_voice(self, i):               # 0x1154114
        self.stop_note(i)
        for r in (2, 3, 4, 5, 0x0a, 0x0b):
            self.c.w16(r, 0)
        self.v[i].addr = 0
        self.v[i].freq = 0

    def init_device(self, dram_size=1 << 20):
        """SDIInitDevice 0x1154881 (GUS, port 0x220, DMA 1, IRQ 11 from SOUND.CFG)."""
        self.voices = 14
        self.master = 0x100
        self.v = [Voice() for _ in range(32)]
        self.reset()
        # 0x11530c9: allocator: a 0x20-byte block at 0x2000 and the end of DRAM (size probed at 0x40400 / 0x80400)
        self.dram_size = dram_size
        self.blocks = [[0x2000, 0x20], [dram_size, 0]]

    # -- CDI (table 0x1155c1a as registered by cdiRegister) ---------------------------------------------------
    def setup_channels(self, n):            # +0x54  0x115419b
        for i in range(self.voices):
            self.clear_voice(i)
        self.voices = min(max(n, 14), 32)
        self.c.w8(0x0e, (self.voices - 1) | 0xc0)
        for i in range(self.voices):
            self.clear_voice(i)
        return 0

    def set_sample(self, ch, s):            # +0x00  0x1153576   s = AMP's channel sample record
        if ch >= self.voices:
            return -1
        addr = self.sample_addr(s['handle'])
        self.c.select(ch)
        v = self.v[ch]
        v.mode = 8 if s['loopend'] else 0
        if v.addr == addr:
            self.ramp(0)
            return 0
        v.addr = addr
        self.ramp(0)
        self.wait_ramp()
        self.w8x2(0x00, 3)
        self.set_pos((0x0a, 0x0b), addr)
        ls = addr
        if s['loopend']:
            ls += s['loopstart']
            if s['loopstart'] > 1:
                ls -= 1
        self.set_pos((0x02, 0x03), ls)
        end = addr + (s['loopend'] - 1 if s['loopend'] else s['length'] - 2)
        self.set_pos((0x04, 0x05), end & 0xffffffff)
        return 0

    def play_note(self, ch, freq, vol):     # +0x04  0x1153716
        v = self.v[ch]
        v.vol = vol
        v.freq = freq
        addr = v.addr
        self.c.select(ch)
        self.ramp(0)
        self.wait_ramp()
        self.w8x2(0x00, 3)
        self.set_pos((0x0a, 0x0b), addr)
        self.c.w16(0x01, self.fc(freq))
        self.w16x2(0x09, VOLTAB[0] & 0xffff)
        self.w8x2(0x00, v.mode)
        if not v.muted:
            self.ramp(vol)
        return 0

    def set_volume(self, ch, vol):          # +0x08  0x1153831
        vol = min(vol & 0xffffffff, 0x40)
        self.v[ch].vol = vol
        if self.v[ch].muted:
            return 0
        self.c.select(ch)
        self.ramp(vol)
        return 0

    def set_frequency(self, ch, freq):      # +0x0c  0x115388e
        f = self.fc(freq)
        self.v[ch].freq = freq
        self.c.select(ch)
        self.c.w16(0x01, f)
        return 0

    def set_position(self, ch, pos):        # +0x14  0x1153927
        v = self.v[ch]
        a = v.addr + pos
        self.c.select(ch)
        self.ramp(0)
        self.wait_ramp()
        self.w8x2(0x00, 3)
        self.w8x2(0x0d, 3)
        self.set_pos((0x0a, 0x0b), a)
        self.w8x2(0x00, v.mode)
        if not v.muted:
            self.ramp(v.vol)
            self.wait_ramp()
        return 0

    def set_panning(self, ch, pan):         # +0x18  0x11539ef: -64..63 -> 0..15
        if pan == 100:
            pan = 0
        if pan > 63:
            pan = 63
        elif pan <= -63:
            pan = -64
        pan = int(pan / 8) + 8              # signed division truncating toward zero
        self.c.select(ch)
        self.c.w8(0x0c, pan)
        return 0

    def set_master(self, vol):              # +0x1c  0x1153a63
        self.master = min(vol * 4, 0x100)
        for i in range(self.voices):
            self.set_volume(i, self.v[i].vol)
        return 0

    def mute(self, ch):                     # +0x20  0x1153ad2
        self.c.select(ch)
        self.ramp(0)
        self.v[ch].muted = 1
        return 0

    def unmute(self, ch):                   # +0x24  0x1153b26
        if self.v[ch].muted:
            self.c.select(ch)
            self.ramp(self.v[ch].vol)
            self.v[ch].muted = 0
        return 0

    def stop_note(self, ch):                # +0x28  0x1153b7b
        self.c.select(ch)
        self.ramp(0)
        self.wait_ramp()
        self.v[ch].vol = 0
        self.w8x2(0x0d, 3)
        self.w8x2(0x00, 3)
        self.c.w16(0x09, VOLTAB[0] & 0xffff)
        return 0

    def alloc(self, size):
        """0x11532ab: best fit among the gaps of the block list; a sample does not cross a 256 KB bank."""
        size = (size + 0x40) & ~0x1f
        best, bi, cross = 0x2000000, None, 0
        for k in range(len(self.blocks) - 1):
            a, s = self.blocks[k]
            start = a + s
            nxt = self.blocks[k + 1][0]
            gap = nxt - start
            if (start + size) >> 18 != start >> 18:
                gap = nxt - ((start + size) & ~0x3ffff)
                if size < gap < best:
                    best, bi, cross = gap, k, 1
            elif size < gap < best:
                best, bi, cross = gap, k, 0
        if bi is None:
            return 0
        a, s = self.blocks[bi]
        addr = ((a + s + size) & ~0x3ffff) if cross else a + s
        self.blocks.insert(bi + 1, [addr, size])
        return addr

    def download(self, data, handle, size):  # +0x34  0x1153df0, DMA upload 0x1153c6c in 0x2000-byte blocks
        addr = self.alloc(size)
        if not addr:
            return -1
        c = self.c
        c.w8(0x41, 0)
        left, a, p = size, addr, 0
        chunk = 0x2000
        while left > 0:
            c.w16(0x42, (a >> 4) & 0xffff)
            if left < chunk:
                chunk = left
            block = bytes(data[p:p + chunk])
            for i, b in enumerate(block):         # what a real GF1 stores: reg 0x41 bit 7 inverts the MSB
                c.dram[(a + i) & 0xfffff] = b ^ 0x80
            c.w8(0x41, 0x81)
            c.w8(0x41, 0)
            left -= chunk
            a += chunk
            p += chunk
        self.samples.append((handle, addr))
        return 0

    def poll(self):                         # +0x5c  0x115429c: nothing
        return 0

    def stop_output(self):                  # SDIStopOutput 0x1154a44
        for i in range(self.voices):
            self.clear_voice(i)
        return 0


# ------------------------------------------------------------------------------------------------- AMP 2.00

class Channel:
    """AMP's 45-byte channel record (0x422a99 + 0x2d * ch) and its 26-byte sample record (0x423039 + 0x1a * ch)."""

    def __init__(self):
        self.track = None        # +0x00 events (list of (row, cmd, val)) of the current order
        self.pos = 0             # +0x04 event index (AMP: byte offset 3 + 3 * index; -1 = end)
        self.flags = 0           # +0x06 bit 1: muted by ampMuteChannel
        self.note = 0            # +0x08
        self.inst = 0            # +0x09
        self.vol = 0             # +0x0a
        self.count = 0           # +0x0b ticks since the note (saturating word)
        self.period = 0          # +0x0d
        self.cmd = 0             # +0x0f per-tick command
        self.val = 0             # +0x10
        self.target = 0          # +0x11 tone porta target period
        self.tpspeed = 0         # +0x13 signed tone porta step
        self.tplast = 0          # +0x14
        self.portalast = 0       # +0x15 signed
        self.arp = [0, 0, 0]     # +0x16, +0x1a, +0x1e
        self.vibpos = 0          # +0x23
        self.vibparam = 0        # +0x24
        self.tremparam = 0       # +0x25
        self.tremspeed = 0       # +0x26
        self.tremdepth = 0       # +0x27
        self.trempos = 0         # +0x28
        self.offlast = 0         # +0x29
        self.pan = 0             # +0x2c
        # sample record (MCP sample structure passed to cdiSetInstrument)
        self.s = {'handle': 0, 'length': 0, 'loopstart': 0, 'loopend': 0, 'rate': 0, 'freq': 0, 'vol': 0}


def sbyte(x):
    x &= 0xff
    return x - 256 if x & 0x80 else x


class Amp:
    def __init__(self, module, drv):
        self.m = module
        self.d = drv
        self.chvol = [0x40] * 32           # [0x423379 + ch] (ampInit: 0x40e00f(-1, 0x40))
        self.playing = 0                   # [0x422a58] bit 0 playing, bit 1 paused
        self.tick_ = 0                     # [0x422a63] (ampInit clears it; ampPlayModule does not)
        self.speed = 6
        self.tempo = 125
        self.div = 50                      # [0x4233a8] = tempo * 50 / 125 (ampInit: 0x32)
        self.last = 0                      # [0x4233aa] DSMI time of the last tick
        self.order = 0                     # [0x422a5d]
        self.row = 0                       # [0x422a61] the row last read
        self.rowc = 0                      # [0x422a5f] the next row
        self.rows = 64                     # [0x422a97]
        self.brk = 0                       # [0x422a70]
        self.sync = 0                      # [0x422a6f]
        self.ch = [Channel() for _ in range(32)]
        self.cur = 0                       # [0x422a5e]
        self.handles = {}                  # instrument -> handle (its sample's address in memory)
        self.stats = {}

    # ---- helpers
    def scale(self, vol, ch):              # 0x40cb34
        return ((vol & 0xffffffff) * self.chvol[ch] >> 6) & 0xffffffff

    def period(self):                      # 0x40dfab: the tick period in DSMI time units
        return 10000 // self.div

    def freq_of_period(self, c, period):
        return (c.s['rate'] * 0x1ac0) // period

    def note_freq(self, c):
        return ((c.s['rate'] * SEMITONE[c.note]) >> 10) & 0xffffffff

    # ---- ampPlayModule 0x40dbb9 -> 0x40d998
    def play(self, start=0, opts=1):
        m = self.m
        self.playing = 0
        self.ch = [Channel() for _ in range(32)]
        self.brk = 0
        self.cur = 0
        self.row = 0
        self.sync = 0
        self.rowc = 0
        self.speed = m.speed
        self.tempo = m.tempo
        self.div = (self.tempo * 50) // 125
        self.start = start
        self.order = start
        self.end = m.norders
        self.opts = opts
        self.nch = m.channels
        for i in range(self.nch):
            self.set_pan(i, m.pan[i])
        self.load_order()
        for i in range(self.nch):          # 0x40ddac: ampUnmuteChannel
            self.d.unmute(i)
            self.ch[i].flags &= ~2
        for ins in m.instruments:          # download every instrument that has sample data
            if ins.data:
                self.handles[ins.i] = 0x1000000 + ins.data_offset     # stands for the sample's heap address
                self.d.download(ins.data, self.handles[ins.i], ins.length)
        self.playing = 1

    def set_pan(self, ch, pan):            # 0x40dfd2
        self.ch[ch].pan = pan
        self.d.set_panning(ch, pan)

    def load_order(self):                  # 0x40d299
        self.rowc = 0
        if self.order >= self.end:
            if not self.opts & 1:
                self.stop()
                return -1
            self.order = self.start
        rows, tracks = self.m.orders[self.order]
        self.rows = rows
        for i in range(self.nch):
            c = self.ch[i]
            c.track = self.m.track(tracks[i])
            c.pos = 0
        return 0

    def stop(self):                        # 0x40dc21
        self.playing &= ~3
        for i in range(self.nch):
            self.d.stop_note(i)

    # ---- the poll 0x40d878 (from the timer callback) and the tick 0x40d7c0
    def poll(self, now):
        p = self.period()
        e = now - self.last
        while True:
            e -= p
            if e <= 0:
                break
            self.tick()
            self.last += p
            p = self.period()

    def tick(self):
        if not self.playing & 1 or self.playing & 2:
            return
        self.tick_ += 1
        if self.tick_ >= self.speed:
            self.tick_ = 0
            if self.brk or self.rowc >= self.rows:
                self.brk = 0
                self.order += 1
                if self.load_order():
                    return
            for i in range(self.nch):
                self.read_row(i)
            self.row = self.rowc
            self.rowc += 1
        for i in range(self.nch):
            self.tick_effect(i)
        for i in range(self.nch):
            c = self.ch[i]
            c.count = min(c.count + 1, 0xffff)

    # ---- the row reader 0x40d1ac
    def read_row(self, i):
        self.cur = i
        c = self.ch[i]
        old = c.cmd
        c.cmd = 0
        c.val = 0
        self.oldcmd = old
        if c.pos != -1 and c.track is not None:
            ev = c.track
            while True:
                row, cmd, val = ev[c.pos]
                if row == 0xff:
                    c.pos = -1
                    break
                if row > self.rowc:
                    break
                nxt = ev[c.pos + 1] if c.pos + 1 < len(ev) else (0xff, 0xff, 0xff)
                if cmd & 0x80:
                    self.effect(i, c, (row, cmd, val) + tuple(nxt))
                else:
                    self.note_event(i, c, (row, cmd, val))
                c.pos += 1
        if self.oldcmd != c.cmd:
            self.d.set_frequency(i, c.s['freq'])
        self.d.set_volume(i, self.scale(c.vol, i))

    def note_event(self, i, c, p):         # 0x40d0c7
        note, val = p[1], p[2]
        self.stats['note'] = self.stats.get('note', 0) + 1
        if note == 0:
            c.vol = 0
            self.d.stop_note(i)
            return
        if note != 0x7f:
            c.note = note
        if val != 0xff:
            c.vol = val
        c.count = 0
        c.vibpos = 0
        self.trigger(i, c)

    def trigger(self, i, c):
        c.period = c.target = PERIOD[c.note]
        f = self.note_freq(c)
        c.s['freq'] = f
        c.s['vol'] = c.vol
        v = self.scale(c.vol, i)
        if not c.flags & 2:
            self.d.play_note(i, f, v)

    def set_freq(self, i, c, period):
        c.period = period
        f = self.freq_of_period(c, period)
        c.s['freq'] = f
        self.d.set_frequency(i, f)

    # ---- effects at the row (jump table 0x40cad4, index = command & 0x7f)
    def effect(self, i, c, p):
        k = p[1] & 0x7f
        self.stats[k] = self.stats.get(k, 0) + 1
        v = p[2]
        if k > 0x18:
            return
        if k == 0x00:                      # 0x40cfce instrument
            c.inst = v
            self.oldcmd = 0 if (p[0] == p[3] and not p[4] & 0x80) else 1
            ins = self.m.instruments[v]
            s = c.s
            s['handle'] = self.handles.get(v, 0)
            s['length'] = ins.length
            s['rate'] = ins.rate
            s['loopstart'] = ins.loopstart
            s['loopend'] = ins.loopend
            s['vol'] = ins.volume
            c.vol = ins.volume
            self.d.set_sample(i, s)
            self.d.set_volume(i, self.scale(s['vol'], i))
        elif k == 0x01:                    # 0x40cf8e speed
            if v != self.speed and v:
                self.speed = v
        elif k == 0x02:                    # 0x40cf1a volume slide (per tick)
            c.cmd, c.val = 2, v
        elif k == 0x03:                    # 0x40cf2e volume
            c.vol = v
            self.d.set_volume(i, self.scale(v, i))
        elif k == 0x04:                    # 0x40cec5 portamento (per tick; + = period up = pitch down)
            cl = sbyte(v)
            if v == 0x80:
                cl = c.portalast
                if cl >= 0:
                    cl = -cl
            elif v == 0:
                cl = c.portalast
                if cl <= 0:
                    cl = -cl
            c.portalast = sbyte(cl)
            c.cmd, c.val = 4, cl & 0xff
        elif k == 0x05:                    # 0x40cb51 nothing
            pass
        elif k == 0x06:                    # 0x40ce2a tone portamento
            cl = v or c.tplast
            c.tplast = cl
            vol = p[0] & 0xff              # EAX still holds the row byte unless a note follows on this row
            if p[0] == p[3] and not p[4] & 0x80:
                n = p[4]
                if n == 0x7f:
                    n = c.note
                c.note = n
                c.target = PERIOD[n]
                c.pos += 1
                vol = p[5]
                if p[5] != 0xff:
                    c.vol = p[5]
            self.d.set_volume(i, self.scale(vol, i))
            if c.target < c.period:
                cl = -sbyte(cl)
            c.cmd, c.tpspeed = 6, sbyte(cl)
        elif k == 0x07:                    # 0x40cd27 tremolo
            c.val = v
            dl = c.tremparam
            if v:
                if v & 0x0f:
                    dl = (dl & 0xf0) | (v & 0x0f)
                if v & 0xf0:
                    dl = (dl & 0x0f) | (v & 0xf0)
            c.tremparam = dl
            c.cmd = 7
            c.tremspeed, c.tremdepth = dl >> 4, dl & 0x0f
        elif k == 0x08:                    # 0x40cdae arpeggio
            if v:
                c.cmd, c.val = 8, v
                base = c.s['freq']
                c.arp = [base, (ARPEGGIO[v >> 4] * base >> 10) & 0xffffffff,
                         (ARPEGGIO[v & 15] * base >> 10) & 0xffffffff]
        elif k == 0x09:                    # 0x40cd75 vibrato
            c.cmd = 9
            dl = c.vibparam
            if v:
                if v & 0x0f:
                    dl = (dl & 0xf0) | (v & 0x0f)
                if v & 0xf0:
                    dl = (dl & 0x0f) | (v & 0xf0)
            c.vibparam = dl
            c.val = dl
        elif k == 0x0a:                    # 0x40cd13 tone porta + volume slide
            c.cmd, c.val = 0x0a, v
        elif k == 0x0b:                    # 0x40ccff vibrato + volume slide
            c.cmd, c.val = 0x0b, v
        elif k == 0x0c:                    # 0x40cf86 pattern break (always to row 0)
            self.brk = 1
        elif k == 0x0d:                    # 0x40cf52 position jump (to order v - 1 + 1)
            o = sbyte(v - 1)
            if o < sbyte(self.order) and not self.opts & 1:
                return
            if o > sbyte(self.end):
                return
            self.order = o & 0xff
            self.brk = 1
        elif k == 0x0e:                    # 0x40ccf6 sync byte (ampGetSync 0x40de3a)
            self.sync = v
        elif k == 0x0f:                    # 0x40ccde retrig
            if v:
                c.cmd, c.val = 0x0f, v
        elif k == 0x10:                    # 0x40cc93 sample offset
            c.cmd = 0x10
            h = v or c.offlast
            c.offlast = h
            off = h << 8
            if off > c.s['length']:
                off = c.s['loopstart']
            self.d.set_position(i, off)
        elif k == 0x11:                    # 0x40cc53 fine volume slide (once)
            c.cmd, c.val = 0x11, v
            nv = sbyte(v + c.vol)
            if nv < 0:
                nv = 0
            if nv > 0x40:
                nv = 0x40
            c.vol = nv
            self.d.set_volume(i, self.scale(nv, i))
        elif k == 0x12:                    # 0x40cbe3 fine porta (once)
            c.cmd, c.val = 0x12, v
            cx = (c.period + (sbyte(v) << 4)) & 0xffff
            if cx & 0x8000 or cx <= 0x64:
                cx = 0x64
            self.set_freq(i, c, cx)
        elif k == 0x13:                    # 0x40cb9c note delay
            c.val = v or 1
            if p[0] == p[3] and not p[4] & 0x80:
                if p[4] != 0x7f:
                    c.note = p[4]
                if p[5] != 0xff:
                    c.vol = p[5]
                c.cmd = 0x13
                c.pos += 1
        elif k == 0x14:                    # 0x40cb88 note cut
            c.cmd, c.val = 0x14, v
        elif k == 0x15:                    # 0x40cfa3 tempo
            if v != self.tempo and v:
                self.tempo = v
                self.div = (v * 50) // 125
        elif k == 0x16:                    # 0x40cf02 fine slide per tick
            if v:
                c.cmd, c.val = 0x16, v
        elif k == 0x17:                    # 0x40cb52 panning
            c.cmd, c.val = 0x17, v
            pan = sbyte(v)
            if v != 0x64:
                pan = max(pan, -63)
                pan = min(pan, 63)
            self.set_pan(i, pan)

    # ---- per-tick effects 0x40d753 (table 0x40d70b; on the row tick only arpeggio and retrig, 0x40d747)
    def tick_effect(self, i):
        self.cur = i
        c = self.ch[i]
        k = c.cmd
        if not k:
            return
        if self.tick_ == 0 and k not in (8, 0x0f):
            return
        if k == 2:
            self.vol_slide(i, c)
        elif k == 4:
            self.porta(i, c)
        elif k == 6:
            self.tone_porta(i, c)
        elif k == 9:
            self.vibrato(i, c)
        elif k == 7:
            self.tremolo(i, c)
        elif k == 0x0a:
            self.tone_porta(i, c)
            self.vol_slide(i, c)
        elif k == 0x0b:
            self.vibrato(i, c)
            self.vol_slide(i, c)
        elif k == 0x13:                    # 0x40d630 note delay
            if c.val == self.tick_:
                c.count = 0
                self.trigger(i, c)
        elif k == 0x14:                    # 0x40d6e2 note cut
            if c.val == 0 or c.val == self.tick_:
                c.vol = 0
                self.d.set_volume(i, 0)
        elif k == 0x16:                    # 0x40d345
            cx = c.period + sbyte(c.val)
            if cx & 0x8000 or cx < 0:
                cx = 0x64
            elif sbyte(c.val) == 0:
                return
            elif cx <= 0x64:
                cx = 0x64
            self.set_freq(i, c, cx & 0xffff)
        elif k == 8:                       # 0x40d4b2 arpeggio
            self.d.set_frequency(i, c.arp[self.tick_ % 3])
        elif k == 0x0f:                    # 0x40d60d retrig
            if self.tick_ % c.val == 0:
                self.d.set_position(i, 0)

    def vol_slide(self, i, c):             # 0x40d312
        nv = sbyte(c.val + c.vol)
        if nv < 0:
            nv = 0
        if nv > 0x40:
            nv = 0x40
        c.vol = nv
        self.d.set_volume(i, self.scale(nv, i))

    def porta(self, i, c):                 # 0x40d3a9
        cx = c.period
        ax = (sbyte(c.val) << 4)
        if ax == 0:
            return
        if ax > 0:
            cx = (cx + ax) & 0xffff
        else:
            ax = -ax
            if ax > cx:
                cx = 0x64
            else:
                cx -= ax
                if cx <= 0x64:
                    cx = 0x64
        if cx >= 0xd5ff:
            cx = 0xd5ff
        self.set_freq(i, c, cx)

    def tone_porta(self, i, c):            # 0x40d429 (16-bit arithmetic, unsigned compares)
        cx = c.period
        ax = (c.tpspeed << 4) & 0xffff
        if ax == 0:
            return
        dx = (c.target - cx) & 0xffff
        to_target = False
        if dx & 0x8000:
            ax, dx = -ax & 0xffff, -dx & 0xffff
            if ax > dx:
                to_target = True
            else:
                ax = -ax & 0xffff
        elif ax > dx:
            to_target = True
        if to_target:
            cx = c.target
            if cx == 0:
                return
        else:
            cx = (cx + ax) & 0xffff
        self.set_freq(i, c, cx)

    def vibrato(self, i, c):               # 0x40d4dd
        s = SINE[(c.vibpos >> 2) & 0x1f]
        ax = ((c.vibparam & 0x0f) * s >> 7) & 0xffff
        cx = c.period
        if c.vibpos & 0x80:
            cx = (cx - ax) & 0xffff
        else:
            cx = (cx + ax) & 0xffff
        if cx <= 0x64:
            cx = 0x64
        self.d.set_frequency(i, self.freq_of_period(c, cx))
        c.vibpos = (c.vibpos + ((c.vibparam >> 2) & 0x3c)) & 0xff

    def tremolo(self, i, c):               # 0x40d56d
        s = SINE[(c.trempos >> 2) & 0x1f]
        dx = (c.tremdepth * s >> 10) & 0xffff
        cx = c.vol
        if c.trempos & 0x80:
            cx += dx
        else:
            cx -= dx
            if cx < 0:
                cx = 0
        cx = min(max(cx, 0), 0x40)
        self.d.set_volume(i, self.scale(cx, i))
        c.trempos = (c.trempos + ((c.tremspeed << 2) & 0x3c)) & 0xff


class Dsmi:
    """The music as INSIDE.EXE runs it: 0x4010b4 (driver, ampInit, setupChannels(24), load, play), 0x401010
    (time step 35) and the timer callback 0x400fcc per timer interrupt."""

    def __init__(self, module, card=None, step=35):
        self.card = card or Card()
        self.drv = Driver(self.card)
        self.amp = Amp(module, self.drv)
        self.time = 0                      # [0x4b9148] DSMI time
        self.step = step                   # [0x4b9144]

    def start(self):
        d, a = self.drv, self.amp
        d.init_device()                    # SDIInitDevice (reset with 14 voices)
        # ampInit 0x40d907(0): clears AMP's state, channel volumes 0x40, last = GetDSMITime() = 0
        d.setup_channels(24)               # 0x40fa69(0x18) -> CDI +0x54
        a.play(0, 1)                       # ampPlayModule(module, 1): pans, order 0, downloads
        a.last = self.time

    def interrupt(self):
        """The timer callback's music part 0x400fcc: DSMI time += step, AMP's poll."""
        self.time += self.step
        self.amp.poll(self.time)

    def exit(self):
        """0x401054 at the demo's exit: ampStop 0x40dc21 (stop the channels, unload the samples: no GUS writes),
        free the module, SDIStopOutput (clear every voice), unload the driver."""
        self.amp.stop()
        self.drv.stop_output()
