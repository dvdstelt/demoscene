"""Parse p2's main script (0010:377f .. 0010:43ed) into steps and print the timeline.

Each step = the variable writes made before a `call 371e` ("run CX retraces"). Writes are decoded from the
instructions `mov word/dword ptr cs:[X], imm`, `mov cx, imm`, `dec word ptr cs:[194f]`, `call 1967` (blur table
for the [1965] value set before) and the table clear at 3a5d. Prints one line per step: index, address, N
(retraces), cumulative retrace at the step start (nominal, without the overshoot), and what changed.
python3 script.py [--sim]   (--sim: also the camera state at each step start, stepping 2580's motion per retrace)
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
os.chdir(os.path.join(HERE, '..', '..'))
import rep

NAMES = {0x1816: 'cam.x', 0x181a: 'cam.y', 0x181e: 'cam.z', 0x1822: 'cam.vx', 0x1826: 'cam.vy', 0x182a: 'cam.vz',
         0x182e: 'cam.a', 0x1830: 'cam.b', 0x1832: 'cam.c', 0x1834: 'cam.va', 0x1836: 'cam.vb', 0x1838: 'cam.vc',
         0x1878: 'discs.pivot_x', 0x184e: 'discs.f', 0x1886: 'C.hidden', 0x1888: 'C.f', 0x18ae: 'C.shadesel',
         0x1904: 'strip', 0x3626: 'strip_row_ofs', 0x1965: 'blur_s', 0x1955: 'label_pos', 0x1957: 'label_dir',
         0x1947: 'backdrop', 0x193f: 'white_fade', 0x194f: 'texsel'}


def s16(v): return v - 0x10000 if v >= 0x8000 else v
def s32(v): return v - 0x100000000 if v >= 0x80000000 else v


def parse():
    v = rep.View('p2')
    off, end = 0x377f, 0x43ed
    steps, cur, cx = [], [], None
    while off < end:
        ins = v._one(0x10, off)
        t = ins.mnemonic + ' ' + ins.op_str
        m = re.match(r'mov (word|dword) ptr cs:\[0x([0-9a-f]+)\], (0x[0-9a-f]+|\d+)$', t)
        if m:
            a, val = int(m.group(2), 16), int(m.group(3), 0)
            val = s16(val) if m.group(1) == 'word' else s32(val)
            cur.append((a, val))
        elif t.startswith('mov cx, '):
            cx = int(t.split(', ')[1], 0)
        elif t == 'dec word ptr cs:[0x194f]':
            cur.append((0x194f, 'dec'))
        elif t == 'call 0x1967':
            cur.append(('blur', None))
        elif t == 'mov byte ptr es:[0x80], al':
            cur.append(('clear_text_rows', None))
        elif t == 'call 0x371e':
            steps.append((off, cx, cur))
            cur = []
        off += ins.size
    return steps


def main():
    steps = parse()
    r = 0
    for i, (a, n, w) in enumerate(steps):
        parts = []
        for k, val in w:
            if k == 'blur':
                continue
            name = NAMES.get(k, k if isinstance(k, str) else hex(k))
            parts.append('%s=%s' % (name, val) if val is not None else name)
        print('%3d %04x N=%4d start=%5d  %s' % (i, a, n, r, ' '.join(parts)))
        r += n
    print('total', r, 'retraces in', len(steps), 'steps')


if __name__ == '__main__':
    main()
