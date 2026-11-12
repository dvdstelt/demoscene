"""Lists the shared framework routines of every part (int 8 handler, calibration, Cirrus probe, exit) by pattern.
usage: python3 models/core/framework_scan.py   (from dope/re, inside the toolbox)"""
import os, sys, re
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..')))
import rep

for p in ['p1', 'p2', 'p3', 'p4', 'p5', 'p6', 'p7']:
    v = rep.View(p)
    img = v.img
    seg = 0x100
    code = img[seg:seg + 0x10000]
    out = []
    # int 8 install: 26 66 a3 20 00 (mov es:[0x20], eax) preceded by mov ax, imm16 (b8 lo hi)
    for m in re.finditer(rb'\xb8(..)\x26\x66\xa3\x20\x00', code, re.S):
        h = int.from_bytes(m.group(1), 'little')
        out.append('install@%04x handler=%04x' % (m.start(), h))
    # imul eax, eax, imm32 (66 69 c0 imm32)
    for m in re.finditer(rb'\x66\x69\xc0(....)', code, re.S):
        k = int.from_bytes(m.group(1), 'little')
        if k in (990, 1000):
            out.append('calib@%04x factor=%d' % (m.start(), k))
    # cirrus: mov al, 0x1f / out / inc dx / in / mov cs:[x], al / mov al, imm / out dx, al
    for m in re.finditer(rb'\xb0\x1f\xee\x42\xec\x2e\xa2(..)\xb0(.)\xee', code, re.S):
        out.append('cirrus@%04x CR1F<-%02x' % (m.start(), m.group(2)[0]))
    # retrace wait timeout: mov cx, imm; mov dx, 3da; in; test al, 8; loope
    for m in re.finditer(rb'\xb9(..)\xba\xda\x03\xec\xa8\x08\xe1', code, re.S):
        out.append('irqwait@%04x timeout=%d' % (m.start(), int.from_bytes(m.group(1), 'little')))
    # far call cs:[x] in handler
    for m in re.finditer(rb'\x2e\xff\x1e(..)', code, re.S):
        out.append('lcall cs:[%04x]@%04x' % (int.from_bytes(m.group(1), 'little'), m.start()))
    # int 21h 4c
    for m in re.finditer(rb'\xb8(.)\x4c\xcd\x21', code, re.S):
        out.append('exit@%04x code=%02x' % (m.start(), m.group(1)[0]))
    # int 16h
    for m in re.finditer(rb'\xcd\x16', code):
        out.append('int16@%04x' % m.start())
    for m in re.finditer(rb'\xe4\x60', code):
        out.append('in60@%04x' % m.start())
    print(p, '\n   ' + '\n   '.join(out))
