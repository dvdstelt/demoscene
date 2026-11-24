"""The measurements quoted in ../../notes-music.md, from the player model (okeanplay.py) and the capture.

usage: python3 analysis.py          (inside the toolbox, from this folder; about 3 s)
"""
import collections
import math
import os
import struct

import okeanplay as op

P = 62 * 320e-6                         # tick period at tempo 125


def gf1_gain_db(v16):
    v = v16 >> 4                        # 12-bit log volume: 4-bit exponent, 8-bit mantissa
    e, m = v >> 8, v & 255
    g = (256 + m) * 2.0 ** (e - 24)
    return 20 * math.log10(g)


def main():
    img = op.Img(os.path.join(op.RE, 'unpacked', 'korso2.bin'))
    volt = [img.u16(op.A_VOLT + 2 * i) for i in range(64)]

    print('== volume table 0x143ba (index = vol * master >> 6; master 63: vol 63 -> 62)')
    for vol in (63, 48, 32, 24, 16, 8, 4, 2, 1, 0):
        i = vol * 63 >> 6
        print('  vol %2d -> index %2d: %04x = %+6.1f dB' % (vol, i, volt[i], gf1_gain_db(volt[i])))
    print('  index 0..62: %+.1f .. %+.1f dB' % (gf1_gain_db(volt[0]), gf1_gain_db(volt[62])))

    print('== pitch: FC = round(Hz / 37); the GF1 at 16 voices plays FC x 38587.5 / 1024 Hz')
    card = op.Card()
    pl = op.Player(img, card)
    pl.gus_init()
    pl.load()
    rate = op.GF1_CLOCK / 16
    used = collections.Counter()
    for o in range(pl.songlen):
        p = pl.orders[o]
        for ch in range(pl.nch):
            lastins = 0
            for r in range(64):
                note, ins, vol, eff, par = pl.tracks[ch][p * 64 + r]
                if ins:
                    lastins = ins
                if note:
                    used[(note, ins or lastins)] += 1
    errs = []
    for (note, ins), cnt in sorted(used.items()):
        if not ins:
            continue
        I = pl.ins[ins - 1]
        per = pl.note_period(note, I.c2spd)
        hz = pl.amiga // per
        q, r = divmod(hz * 100, 0xe74)
        fc = q + (r >= 1850)
        played = fc * rate / 1024
        errs.append(1200 * math.log2(played / (pl.amiga / per)))
    print('  over the %d note/instrument pairs of the song: %+.1f .. %+.1f cents against 0xd87b50 / period' % (
        len(errs), min(errs), max(errs)))
    print('  the player assumes 37 Hz per FC unit (a 37,888 Hz card), the card plays %.4f Hz per unit '
          '(38587.5 / 1024): x %.5f = %+.1f cents' % (rate / 1024, rate / 1024 / 37, 1200 * math.log2(rate / 1024 / 37)))

    print('== one-shot samples: the byte a finished voice parks on (start + length - 1)')
    for i, I in enumerate(pl.ins):
        if I.len and not I.flags & 1:
            b = card.dram[I.dram + I.len - 1]
            print('  ins %d: last byte %d (%+d)' % (i + 1, b, b - 256 if b > 127 else b))

    print('== the song: ticks per order (player ticks; tick 1 reads 00:00)')
    card = op.Card()
    pl, sched = op.run(img, card, irqs=None, ticks=op.CAPTURE_TICKS + 400, fade=False)
    starts = {}
    for k, pos in enumerate(pl.positions):
        n = k + 1
        if n <= op.START_AFTER:
            continue
        o, r, cnt = pos
        if (o, r) not in starts:
            starts[(o, r)] = n
    seq = []
    for (o, r), n in sorted(starts.items(), key=lambda x: x[1]):
        if r == 0:
            seq.append((o, n))
    for i, (o, n) in enumerate(seq[:34]):
        nxt = seq[i + 1][1] if i + 1 < len(seq) else None
        print('  order %2d (pattern %2d) from timer interrupt %4d (player tick %4d), %s ticks, hardware %.4f s' % (
            o, pl.orders[o], n, n - op.START_AFTER, (nxt - n) if nxt else '?', n * P))

    print('== the capture\'s timer interrupts held off by more than 1 ms (part starts)')
    irqs = op.read_irqs(os.path.join(op.RE, 'full.gus'))
    frames = op.read_frames(os.path.join(op.RE, 'full.ticks'))
    kprev = 0
    lost_total = 0
    late_small = collections.Counter()
    for tick, kind, t in irqs:
        if kind != 't':
            continue
        k = math.floor(t / P + 1e-6)
        late = t - k * P
        held = (kprev + 1) * P
        if late > 1e-3:
            lost = k - kprev - 1
            lost_total += lost
            fr = int(t / op.FRAME)
            ret_end = fr * op.FRAME + op.RETRACE + 2 * 808 / 25.175e6
            print('  interrupt %4d: overflow %4d (%.6f s) held, served %.6f s (+%.2f ms), %d lost; served %.1f us '
                  'after the end of frame %d\'s retrace; music %.2f ms behind the GUS clock from here' % (
                      tick, kprev + 1, held, t, (t - held) * 1e3, lost, (t - ret_end) * 1e6, fr,
                      lost_total * P * 1e3))
        elif tick > 3 and late > 1.5e-6:
            late_small[round(late * 1e3, 2)] += 1
        kprev = k
    print('  other interrupts served late: %d (by %s ms)' % (
        sum(late_small.values()), ', '.join('%.2f x%d' % kv for kv in sorted(late_small.items())[-6:])))

    print('== the ramp path with hardware times (no holds): per tick with note starts')
    card = op.Card()
    pl, sched = op.run(img, card, irqs=None, ticks=op.CAPTURE_TICKS, fade=True)
    tick_t = None
    per_tick = []
    cur = None
    for kind, te, td, started in sched:
        if kind == 't':
            tick_t = td
            if cur is not None and started:
                per_tick.append(cur)
            cur = []
        else:
            cur.append(td - tick_t)
    if cur is not None:
        per_tick.append(cur)
    with_notes = [x for x in per_tick if x]
    firsts = [x[0] * 1e3 for x in with_notes]
    lasts = [x[-1] * 1e3 for x in with_notes]
    nv = collections.Counter(len(x) for x in with_notes)
    print('  %d of %d ticks start notes; voices per such tick: %s' % (
        len(with_notes), len(per_tick), ', '.join('%d: %d' % kv for kv in sorted(nv.items()))))
    print('  first switch %.3f..%.3f ms after the tick, last %.3f..%.3f ms (median %.3f)' % (
        min(firsts), max(firsts), min(lasts), max(lasts), sorted(lasts)[len(lasts) // 2]))
    d1 = sorted(b - a for x in with_notes for a, b in zip([0] + x, x))
    print('  one ramp: %.3f..%.3f ms (median %.3f)' % (d1[0] * 1e3, d1[-1] * 1e3, d1[len(d1) // 2] * 1e3))
    print('  ramp of index i from volume 0x%03x to 0x040 at 63 per frame: (v - 0x40) / 63 / 38587.5 s;'
          ' index 62 (0xff0): %.3f ms, index 0 (0x6e0): %.3f ms' % (
              0xff0, (0xff0 - 0x40) / 63 / rate * 1e3, (0x6e0 - 0x40) / 63 / rate * 1e3))


if __name__ == '__main__':
    main()
