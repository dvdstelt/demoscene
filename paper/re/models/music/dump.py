import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import player_ref
seg = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
t = player_ref.Tune(seg)
def fmt(e):
    n,i,v,ef,pa = e
    nn = '%x-%x' % (n & 15, n >> 4) if n else '...'
    return nn + (' %02d'%i if i else ' ..') + (' %02x'%v if v!=0xff else ' ..') + (' %x%02x'%(ef,pa) if ef or pa else ' ...')
with open('tune.txt','w') as f:
    for r in range(t.nrows):
        f.write('%4d | ' % r + ' | '.join(fmt(t.events[r][c]) for c in range(t.nch)) + '\n')
for r in range(t.nrows):
    for c in range(t.nch):
        n,i,v,ef,pa = t.events[r][c]
        if ef in (9,10,13,15) : print(r, c, hex(ef), pa, end='; ')
print()
# last row with any event
last = max(r for r in range(t.nrows) if any(t.events[r][c] != (0,0,0xff,0,0) for c in range(t.nch)))
print('last row with an event', last)
print('instruments', t.ins)
