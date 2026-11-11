import struct, collections
IMG = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
S = IMG[0xd00:]
p = 0x7a9b
nch = S[p]; nrows = struct.unpack_from('<H', S, p+1)[0]
p += 4
ev = [[None]*nch for _ in range(nrows)]
for ch in range(nch):
    for r in range(nrows):
        f = S[p]; p += 1
        note=ins=eff=0; par=0
        vol = f & 0x3f
        if vol == 0x3f: vol = 0x40
        if vol == 2: vol = 0xff
        if f & 0x40:
            b = S[p]; note = b & 0x7f; hi = b >> 7
            b2 = S[p+1]; ins = (hi << 4) + (b2 & 0xf); eff = b2 >> 4; p += 2
        if f & 0x80:
            par = S[p]; p += 1
        ev[r][ch] = (note, ins, vol, eff, par)
effs = collections.Counter(); notes=collections.Counter(); inss=collections.Counter(); vols=collections.Counter()
for r in range(nrows):
    for ch in range(nch):
        n,i,v,e,pa = ev[r][ch]
        if e or pa: effs[(e,pa)] += 1
        if n: notes[n]+=1
        if i: inss[(ch,i)]+=1
        if v!=0xff: vols[v]+=1
print('effects', sorted(effs.items()))
print('notes', sorted(notes.items()))
print('ins', sorted(inss.items()))
print('vols', sorted(vols.items()))
# first instrument per channel vs first note
for ch in range(nch):
    fi = next((r for r in range(nrows) if ev[r][ch][1]), None)
    fn = next((r for r in range(nrows) if ev[r][ch][0]), None)
    fe = next((r for r in range(nrows) if ev[r][ch][3]), None)
    print('ch', ch, 'first ins row', fi, 'first note', fn, 'first eff', fe)
# print rows 0..40
def fmt(e):
    n,i,v,ef,pa = e
    return ('%02x'%n if n else '..') + ('%02d'%i if i else '..') + ('%02x'%v if v!=0xff else '..') + ('%x%02x'%(ef,pa) if ef or pa else '...')
for r in list(range(0,24)):
    print('%4d'%r, ' '.join(fmt(ev[r][c]) for c in range(nch)))
