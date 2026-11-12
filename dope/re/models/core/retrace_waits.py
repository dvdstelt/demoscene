"""Finds every busy wait on the vertical retrace (in al, dx / test al, 8 / je|jne back) in the parts and says whether
it waits for the start of a retrace properly (first while bit 3 = 1, then while bit 3 = 0) or only for "bit 3 = 1"
(which returns at once when the retrace is still on: in the emulator, where port writes take no time, a loop around
such a wait can run several times in one 64 us retrace pulse; on a real VGA it cannot, see notes-core.md).

usage: python3 models/core/retrace_waits.py     (from dope/re, inside the toolbox)"""
import os, sys, re
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..')))
import rep

for p in ['p1', 'p2', 'p3', 'p4', 'p5', 'p6', 'p7']:
    v = rep.View(p)
    code = v.img[0x100:0x100 + 0x10000]
    res = []
    for m in re.finditer(rb'\xec\xa8\x08(\x74|\x75|\xe1)\xfb', code):
        off = m.start()
        kind = {0x74: 'until set', 0x75: 'until clear', 0xe1: 'until set (timeout)'}[m.group(1)[0]]
        if kind == 'until clear':
            continue
        prev = code[off - 5:off] == b'\xec\xa8\x08\x75\xfb'
        prev_dx = code[off - 8:off] == b'\xba\xda\x03\xec\xa8\x08\x75\xfb'
        res.append('%04x %s%s' % (off, kind, '' if (prev or prev_dx) else '  <- no wait for the end first'))
    # loope form used in handlers: in al,dx / test al,8 / loope
    print(p, '\n   ' + '\n   '.join(res))
