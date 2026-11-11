"""Load a DOS/4GW LE executable into a flat memory image with fixups applied."""
import struct, sys, json

def load(path):
    d = open(path, 'rb').read()
    le = struct.unpack_from('<I', d, 0x3C)[0]
    # bound DOS/4GW executables chain several MZ stubs; find the 'LE' signature
    if d[le:le+2] != b'LE':
        le = d.find(b'LE\x00\x00')
    assert d[le:le+2] == b'LE', 'no LE header'
    h = lambda off, fmt='<I': struct.unpack_from(fmt, d, le + off)[0]
    npages, eip_obj, eip, esp_obj, esp, pgsize, lastpg = h(0x14), h(0x18), h(0x1C), h(0x20), h(0x24), h(0x28), h(0x2C)
    objtab, nobj, pgtab = le + h(0x40), h(0x44), le + h(0x48)
    fxpg, fxrec = le + h(0x68), le + h(0x6C)
    datapg = h(0x80)
    objs = []
    for i in range(nobj):
        vsize, base, flags, pidx, pcnt, _ = struct.unpack_from('<6I', d, objtab + i*24)
        objs.append(dict(vsize=vsize, base=base, flags=flags, pidx=pidx, pcnt=pcnt))
    # page table: LE uses 3-byte big-endian page number + flags
    pages = []
    for i in range(npages):
        b = d[pgtab + i*4: pgtab + i*4 + 4]
        pages.append(((b[0] << 16) | (b[1] << 8) | b[2], b[3]))
    mem = {}
    for oi, o in enumerate(objs):
        buf = bytearray(max(o['vsize'], o['pcnt']*pgsize))
        for k in range(o['pcnt']):
            pn, fl = pages[o['pidx']-1+k]
            src = datapg + (pn-1)*pgsize
            n = lastpg if (pn == npages) else pgsize
            buf[k*pgsize:k*pgsize+n] = d[src:src+n]
        mem[oi+1] = buf
    # fixups
    fxoffs = [struct.unpack_from('<I', d, fxpg + 4*i)[0] for i in range(npages+1)]
    nfix = 0
    for oi, o in enumerate(objs):
        for k in range(o['pcnt']):
            gpage = o['pidx']-1+k
            p, end = fxrec + fxoffs[gpage], fxrec + fxoffs[gpage+1]
            while p < end:
                src, flg = d[p], d[p+1]; p += 2
                if src & 0x20:
                    cnt = d[p]; p += 1; srcoffs = None
                else:
                    so = struct.unpack_from('<h', d, p)[0]; p += 2
                assert flg & 3 == 0, f'non-internal fixup {flg:#x}'
                if flg & 0x40: tobj = struct.unpack_from('<H', d, p)[0]; p += 2
                else: tobj = d[p]; p += 1
                stype = src & 0x0F
                if stype == 2: toff = 0
                elif flg & 0x10: toff = struct.unpack_from('<I', d, p)[0]; p += 4
                else: toff = struct.unpack_from('<H', d, p)[0]; p += 2
                if src & 0x20:
                    srcoffs = [struct.unpack_from('<h', d, p + 2*j)[0] for j in range(cnt)]; p += 2*cnt
                else:
                    srcoffs = [so]
                buf = mem[oi+1]
                tgt = objs[tobj-1]['base'] + toff
                for so in srcoffs:
                    at = k*pgsize + so
                    if at < 0 or at + 4 > len(buf): continue
                    if stype == 7:
                        struct.pack_into('<I', buf, at, tgt & 0xFFFFFFFF)
                    elif stype == 8:
                        rel = tgt - (o['base'] + at + 4)
                        struct.pack_into('<i', buf, at, rel)
                    elif stype == 2:
                        struct.pack_into('<H', buf, at, tobj)
                    elif stype == 5:
                        struct.pack_into('<H', buf, at, tgt & 0xFFFF)
                    elif stype == 6:
                        struct.pack_into('<IH', buf, at, tgt, tobj)
                    nfix += 1
    entry = objs[eip_obj-1]['base'] + eip
    return objs, mem, entry, nfix

if __name__ == '__main__':
    objs, mem, entry, nfix = load(sys.argv[1])
    for i, o in enumerate(objs):
        print(f"obj{i+1}: base={o['base']:#x} vsize={o['vsize']:#x} flags={o['flags']:#x} pages={o['pcnt']}")
    print(f'entry={entry:#x} fixups={nfix}')
    if len(sys.argv) > 2:
        for i, o in enumerate(objs):
            open(f"{sys.argv[2]}/obj{i+1}_{o['base']:08x}.bin", 'wb').write(mem[i+1])
