"""Make a synthetic PKLITE 1.15 file: PAPER.EXE's header and stub with a random compressed stream that uses every
length and distance code, length escapes, FEh renormalizations and a multi-group relocation table.
usage: gen.py PAPER.EXE seed variant out.exe     variant: normal | early (one FEh while DI < 2000h) | loose"""
import random, struct, sys

LEN = {2: '10', 3: '11', 4: '000', 5: '0010', 6: '0011', 7: '0100', 8: '01010', 9: '01011', 10: '01100',
       11: '011010', 12: '011011', 13: '0111010', 14: '0111011', 15: '0111100', 16: '01111010', 17: '01111011',
       18: '01111100', 19: '011111010', 20: '011111011', 21: '011111100', 22: '011111101', 23: '011111110',
       24: '011111111'}
ESC = '011100'
DIST = {0: '1', 1: '0000', 2: '0001', 3: '00100', 4: '00101', 5: '00110', 6: '00111'}
DIST.update({v: '01' + format(v - 7, '04b') for v in range(7, 14)})
DIST.update({v: '01' + format(v, '05b') for v in range(14, 32)})

class Enc:
    def __init__(s):
        s.out = bytearray(2); s.wpos = 0; s.word = 0; s.n = 0
    def bit(s, b):
        s.word |= b << s.n; s.n += 1
        if s.n == 16:  # the stub fetches the next word at once
            s.out[s.wpos:s.wpos + 2] = struct.pack('<H', s.word)
            s.wpos = len(s.out); s.out += b'\0\0'; s.word = 0; s.n = 0
    def bits(s, t):
        for c in t: s.bit(int(c))
    def byte(s, v): s.out.append(v)
    def word16(s, v): s.out += struct.pack('<H', v & 0xffff)
    def flush(s): s.out[s.wpos:s.wpos + 2] = struct.pack('<H', s.word)

def main(paper, seed, variant, dst):
    rnd = random.Random(seed)
    d = bytearray(open(paper, 'rb').read())
    hdr = d[:0xa0 + 0x290]          # MZ header + PKLITE stub (compressed data starts at module offset 290h)
    e = Enc()
    PSP = 0x0ff0
    es, di = PSP, 0x100            # stub's ES:DI
    w = 0                          # linear output position relative to L:0000
    need = 0                       # max(w_end - bytes read): the data block must start at least this high
    target = rnd.randint(0x14000, 0x30000)
    early_done = variant != 'early'
    lit_p = 0.35 if variant != 'loose' else 0.6
    count_len = {}; count_dist = {}
    ds_rel, si = 0, 0              # bytes read before DS's base, SI
    def read_total(): return len(e.out)
    while w < target:
        r = rnd.random()
        if not early_done and di >= 0x900 and r < 0.05:
            e.bits('1' + ESC); e.byte(0xfe)
            es = (es + (((di - 0x2000) & 0xffff) >> 4)) & 0xffff; di = (di & 15) + 0x2000
            w = es * 16 + di - 0x10000
            early_done = True
            continue
        if (di > 0xe000 or (read_total() - ds_rel) > 0xe000 or (di >= 0x2000 and r < 0.002)):
            e.bits('1' + ESC); e.byte(0xfe)
            es = (es + (((di - 0x2000) & 0xffff) >> 4)) & 0xffff; di = (di & 15) + 0x2000
            w = es * 16 + di - 0x10000
            si = read_total() - ds_rel; ds_rel += si & ~15
            continue
        if r < lit_p or w < 2:
            e.bit(0); e.byte(rnd.randrange(256)); di += 1; w += 1
        else:
            length = rnd.choice(list(range(2, 25)) + [rnd.randint(25, 25 + 253)])
            maxd = min(di, w, 0xff if length == 2 else 0x1fff)
            dist = rnd.randint(0, maxd) if rnd.random() < 0.01 else rnd.randint(1, maxd)
            if length != 2 and rnd.random() < 0.5:  # spread over all high codes
                hi = rnd.randint(0, maxd >> 8); dist = (hi << 8) | rnd.randint(0, 255)
                if dist > maxd: dist = maxd
            e.bit(1)
            if length <= 24: e.bits(LEN[length])
            else: e.bits(ESC); e.byte(length - 25)
            if length != 2: e.bits(DIST[dist >> 8]); count_dist[dist >> 8] = 1
            e.byte(dist & 0xff)
            count_len[min(length, 25)] = 1
            di += length; w += length
        need = max(need, w - read_total())
    e.bits('1' + ESC); e.byte(0xff)
    out_len = w
    # relocation table: groups { count, segment, offsets }
    for _ in range(rnd.randint(1, 6)):
        n = rnd.randint(1, 255)
        seg = rnd.randint(0, max(0, (out_len - 0x10000) >> 4)) if out_len > 0x10000 else 0
        e.byte(n); e.word16(seg)
        for _ in range(n):
            hi = min(0xfffe, out_len - 2 - seg * 16)
            e.word16(rnd.randint(0, hi))
    e.byte(0)
    stream_len = len(e.out) + 8
    comp_paras = (stream_len + 15) // 16
    slack = rnd.choice([0, 1, 7]) if variant != 'loose' else 0x100
    data_para = (need + 15) // 16 + slack      # data block start, in paragraphs from L
    mem_paras = data_para + comp_paras + 0x25 + 0x20
    # program SS:SP somewhere in the image, CS:IP anything but T
    ss = rnd.randint(0, mem_paras - 0x1000) if mem_paras > 0x1000 else 0
    sp = rnd.randint(8, 0xfff0) & ~1
    ss = min(ss, (mem_paras * 16 - sp) // 16)
    e.word16(ss); e.word16(sp); e.word16(rnd.randint(0, out_len >> 4)); e.word16(rnd.randint(0, 0xffff))
    e.flush()
    stream = bytes(e.out)
    out = bytearray(hdr) + stream
    struct.pack_into('<H', out, 0xa1, mem_paras)
    struct.pack_into('<H', out, 0xa4, comp_paras)
    size = len(out)
    struct.pack_into('<HH', out, 2, size % 512, (size + 511) // 512)
    open(dst, 'wb').write(out)
    print('%s seed %d: out %05x, stream %05x, memParas %04x, lengths %d/24, dist hi %d/32' % (
        variant, seed, out_len, len(stream), mem_paras, len(count_len), len(count_dist)))

main(sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4])
