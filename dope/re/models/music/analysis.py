"""Measurements behind ../../notes-music.md that dopeplay.py's check does not print (run inside the toolbox from
this folder: `python3 analysis.py`, about 20 s). Uses dopeplay.py's port; reads only the module, loader.bin and
the capture's full.gus.

  1. quirks the song triggers (4xx past the sine, the unmatched period 906, arpeggio, 9xx, instrument store)
  2. the tick schedule: closed form against the card model, and the capture's lost timer overflows
  3. the ramp path: how long after a tick the updates (0x4299) and the new samples come
  4. the capture's stream against the stream with the interrupts at their hardware times (no CPU latency)
"""
import collections
import os
import sys

import dopeplay as d

IMG = d.Img(os.path.join(d.RE, 'unpacked', 'loader.bin'))
MOD = open(os.path.join(d.RE, '..', 'original', 'DOPE.MOD'), 'rb').read()
IRQS = d.read_irqs(os.path.join(d.RE, 'full.gus'))
NT = sum(1 for x in IRQS if x[1] == 't')


def quirks():
    cnt = collections.Counter()
    where = collections.defaultdict(list)
    P = d.Player
    row0, tick0 = P.row_channel, P.tick_effects

    def row_channel(self, C, ev):
        note, ins, vol, eff, par = ev
        k = self.ch.index(C)
        if note and not ins and getattr(C, 'q0', False):
            cnt['note without instrument after the 0-based instrument store'] += 1
        if note == 0x81:
            where['unmatched period'].append('%02x:%02x ch%d' % (self.order, self.row, k))
        if eff == 9 and not note:
            cnt['9xx without a note (restarts the sample)'] += 1
        if eff == 0 and par:
            cnt['arpeggio rows'] += 1
            if par & 0xf:
                cnt['arpeggio rows with y != 0 (y ignored)'] += 1
            if note and (note & 0xf) - 1 + (par >> 4) > 11:
                cnt['arpeggio crossing an octave (11-step wrap)'] += 1
        if ins and (not note or eff in (3, 5)):
            C.q0 = True
        elif ins:
            C.q0 = False
        if eff in (3, 5) and note and ins:
            cnt['tone porta with note and instrument (instrument stored 0-based)'] += 1
        return row0(self, C, ev)

    def tick_effects(self):
        for C in self.ch[:self.nch]:
            if C.eff == 4:
                i = ((C.vibpos + ((C.par & 0xf0) >> 2)) & 0xff) >> 2
                cnt['4xx ticks'] += 1
                cnt['4xx ticks reading past the sine (0x22c0..0x22df)'] += i >= 32
                cnt['4xx ticks reading the EAX bytes 0x22c1..0x22c3'] += 33 <= i <= 35
        return tick0(self)

    P.row_channel, P.tick_effects = row_channel, tick_effects
    try:
        card = d.Card()
        pl, _ = d.run(IMG, MOD, card, irqs=IRQS)
    finally:
        P.row_channel, P.tick_effects = row0, tick0
    print('1. quirks (capture run, %d note starts)' % pl.notes)
    for k, v in sorted(cnt.items()):
        print('   %6d  %s' % (v, k))
    print('   unmatched period 906 (note byte 0x81 -> period 107 x 4 units): ' + ', '.join(where['unmatched period']))
    # what the unmatched period does: channel 13 frequency writes around 3b:30
    ts = [i + 1 for i, p in enumerate(pl.positions) if p[0] == 0x3b and 0x30 <= p[1] <= 0x31]
    fc = [ln for ln in card.lines if ln.split()[1] == 'v13' and ln.split()[2] == 'r01'
          and ts[0] <= int(ln.split()[0]) <= ts[-1]]
    print('   channel 13 frequency after 3b:30: ' + ', '.join(ln.split()[3] for ln in fc))


def schedule():
    card = d.Card()
    pl, sched = d.run(IMG, MOD, card, irqs=IRQS)
    tt = [te for k, te, td, _ in sched if k == 't']

    def closed(k):                                # k = ticks since the first row tick
        if k <= 2:
            return 0.01984 * k
        if k <= 1730:
            return 0.03968 + (k - 2) * 0.0224
        return 0.03968 + 1728 * 0.0224 + (k - 1730) * 0.02048

    print('2. tick schedule (hardware overflow times of the card model, relative to the first row tick)')
    prev = None
    for n, t in enumerate(tt[2:]):
        dlt = t - tt[2] - closed(n)
        if prev is None or abs(dlt - prev) > 1e-6:
            p = pl.positions[n + 2]
            print('   from tick %5d (k = %5d, position %02x:%02x): closed form + %.2f ms' % (
                n + 3, n, p[0], p[1], dlt * 1e3))
            prev = dlt
    print('   (each step = a timer overflow lost while a part start blocked interrupts; the capture lost %d)' %
          card.lost)
    late = sorted(((td - te) * 1e3, td) for k, te, td, s in sched if s and td - te > 5e-3)
    print('   interrupts served more than 5 ms late: ' + ', '.join('%.1f ms at %.2f s' % x for x in late))


def ramp_path():
    card = d.Card()
    st = {'t': None}
    upd, ramps = [], []
    P = d.Player
    irq0, up0, ramp0 = P.irq, P.update, P.ramp_irq

    def irq(self):
        if card.status & 8 and not card.status & 0x40:
            st['t'] = card.now
        irq0(self)

    def update(self):
        if st['t'] is not None:
            upd.append(card.now - st['t'])
        up0(self)

    def ramp_irq(self):
        ramps.append(card.now - st['t'])
        ramp0(self)

    P.irq, P.update, P.ramp_irq = irq, update, ramp_irq
    try:
        d.run(IMG, MOD, card, irqs=None, ticks=NT)
    finally:
        P.irq, P.update, P.ramp_irq = irq0, up0, ramp0
    late = sorted(x * 1e3 for x in upd if x > 0)
    print('3. ramp path (interrupts at their hardware times): %d ticks, %d with note starts; updates (0x4299) '
          'deferred by %.3f .. %.3f ms (median %.3f); %d ramp IRQs %.3f .. %.3f ms after the tick' % (
              len(upd), len(late), late[0], late[-1], late[len(late) // 2], len(ramps), 1e3 * min(ramps),
              1e3 * max(ramps)))


def latency():
    def load(lines, mask):
        out = []
        for ln in lines:
            if not ln or ln[0] == '#' or ' irq ' in ln:
                continue
            f = ln.split()
            if mask and f[2] in ('r03', 'r05', 'r0b'):
                f[3] = '%x' % (int(f[3], 16) & ~0x100)
            out.append(' '.join(f))
        return out

    ref = open(os.path.join(d.RE, 'full.gus')).read().split('\n')
    print('4. capture against other interrupt schedules (writes only, irq lines left out)')
    for name, irqs, order in (('capture times, ramp ends queued in time order', IRQS, False),
                              ('hardware times (no CPU latency)', None, False)):
        card = d.Card()
        d.run(IMG, MOD, card, irqs=irqs, ticks=NT, use_order=order)
        for mask in (False, True):
            a, b = load(ref, mask), load(card.lines, mask)
            nd = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
            print('   %-48s %s: %d of %d writes differ' % (name, 'bit 8 of reg 3/5/0x0b masked' if mask else
                                                           'exact', nd, len(a)))


if __name__ == '__main__':
    quirks()
    schedule()
    ramp_path()
    latency()
    sys.stdout.flush()
