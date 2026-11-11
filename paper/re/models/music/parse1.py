import struct
IMG = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
S = IMG[0xd00:]
p = 0x7a9b
nch = S[p]; nrows = struct.unpack_from('<H', S, p+1)[0]; nins = S[p+3]
print('nch', nch, 'rows', nrows, 'ins', nins)
p += 4
cnt = {}
for ch in range(nch):
    st = p
    for r in range(nrows):
        f = S[p]; p += 1
        if f & 0x40: p += 2
        if f & 0x80: p += 1
    print('ch', ch, 'bytes', p-st)
print('pattern end', hex(p))
dr = 8
for i in range(nins):
    L = struct.unpack_from('<h', S, p)[0]; p += 2
    if L == 0:
        print(i+1, 'empty'); continue
    if L > 0:
        fr = struct.unpack_from('<H', S, p)[0]
        print(i+1, 'len', L, 'freq', fr, 'src', hex(p+2), 'dram', dr, 'first', S[p+2:p+10].hex())
        dr += L + 2; p += L + 2
    else:
        lo, cl, fr = struct.unpack_from('<HHH', S, p)
        print(i+1, 'L', -L, 'loopofs', lo, 'copylen', cl, 'freq', fr, 'src', hex(p+6), 'dram', dr, 'first', S[p+6:p+14].hex())
        dr += cl + 2; p += -L + 6
print('end', hex(p), 'dram total', dr)
