"""What is in CLONE's song: instruments and where their samples come from (the stored 8-bit block or which call of
the builder), the order list with the IRQ / music time at which each order starts, and per instrument the notes,
channels and effects it is played with. Reads the module that modsynth.py builds (or --module).

usage: python3 song.py [--module module.bin]
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import modsynth  # noqa: E402
import mxm  # noqa: E402

NOTES = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-']
PERIOD = 22100
PIT = 1193182


def note_name(n):
    return '%s%d' % (NOTES[(n - 1) % 12], (n - 1) // 12)


def dram_byte(addr, bit16):
    return ((addr & 0xc0000) | ((addr & 0x1ffff) << 1)) if bit16 else addr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--module')
    a = ap.parse_args()
    img = modsynth.load_img()
    data = open(a.module, 'rb').read() if a.module else bytes(modsynth.build(img))
    m = mxm.Module(data)
    calls = modsynth.calls(img)
    src = {}
    for k, (addr, fn, ofs, n, args) in enumerate(calls):
        fl = [struct.unpack('<f', struct.pack('<I', x))[0] for x in args]
        src[ofs - m.smpstart] = ('call %2d at %#x: %s(%s), %d words' % (
            k + 1, addr, 'A' if fn == modsynth.SYNTH_A else 'B', ', '.join('%g' % x for x in fl), n))
    print('instruments (DRAM byte addresses of the sample data):')
    for i in range(1, m.insnum + 1):
        I = m.instrument(i)
        s = I['samples'][0]
        b16 = bool(s['mode'] & 4)
        st, ls, en = (dram_byte(x, b16) for x in (s['start'], s['loop'], s['end']))
        w = 2 if b16 else 1
        loop = ('no loop' if not s['mode'] & 8 else
                '%s loop %d..%d' % ('bidi' if s['mode'] & 16 else 'fwd', (ls - st) // w, (en - st) // w))
        print('  %2d: %-6s %5d samples played (%s), vol %2d, fade %5d, env %s' % (
            i, '16-bit' if b16 else '8-bit', (en - st) // w, loop, s['vol'], I['fade'],
            I['env'][:I['envnum'] + 1] if I['envnum'] else 'none'))
        print('      data at DRAM %#07x: %s' % (st, src.get(st, 'stored 8-bit samples (module %#x)' % (m.smpstart + st))))
    # order list with start ticks (IRQ k reads row 0 of order o; xmpGetTimer = (k - 2) * P at IRQ k)
    print('orders (IRQ that reads row 0; music time = (IRQ - 2) * %d / %d s):' % (PERIOD, PIT))
    irq = 1
    use = {}
    for o in range(m.ordnum):
        pat, _ = m.pattern(m.orders[o])
        speed = 3
        print('  %02x: pattern %2d, IRQ %5d, %7.3f s' % (o, m.orders[o], irq, (irq - 2) * PERIOD / PIT))
        for r, row in enumerate(pat):
            for ch, (note, ins, vol, eff, par, f) in row.items():
                if f & 0x20 and ins:
                    u = use.setdefault(ins, {'notes': {}, 'ch': set(), 'eff': {}, 'n': 0})
                    u['n'] += 1
                    u['notes'][note] = u['notes'].get(note, 0) + 1
                    u['ch'].add(ch)
                    if f & 0x80:
                        k = '%x%02x' % (eff, par) if eff < 0x24 else 'E%x%x' % (eff - 0x24, par)
                        u['eff'][k[0]] = u['eff'].get(k[0], 0) + 1
        irq += 64 * speed
    print('  end of the order list at IRQ %d (%.3f s)' % (irq, (irq - 2) * PERIOD / PIT))
    print('notes per instrument:')
    for i in sorted(use):
        u = use[i]
        print('  %2d: %4d notes on channels %s; notes %s; effects with the note %s' % (
            i, u['n'], ','.join(map(str, sorted(u['ch']))),
            ' '.join('%s x%d' % (note_name(n) if n < 97 else 'off', c) for n, c in sorted(u['notes'].items())),
            u['eff']))


if __name__ == '__main__':
    main()
