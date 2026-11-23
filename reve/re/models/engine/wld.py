"""Readers for Reve's 3D files: 3D\\*.WLD (objects, faces, vertices) and 3D\\*.ANI (camera and object keys).

The formats are described in reve/re/notes-engine.md ("The WLD file", "The ANI file").
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = os.path.join(HERE, '..', '..', 'unpacked', 'files')


def read_file(name):
    return open(os.path.join(FILES, name.replace('\\', '/')), 'rb').read()


class Obj:
    pass


def read_wld(data):
    """-> header dict, list of objects (pos, faces [(type, colour, b2, tex, a, b, c)], verts [(x, y, z, u, v)])."""
    assert data[0] == 1
    nv, nf, no, h4, h5 = struct.unpack_from('<5i', data, 1)
    p = 0x15
    objs = []
    for k in range(no):
        o = Obj()
        o.kind = data[p]
        o.nv, o.nf, x, y, z = struct.unpack_from('<5i', data, p + 1)
        o.pos = (x, y, z)
        p += 0x15
        o.faces = []
        for i in range(o.nf):
            t, c, tex = data[p], data[p + 1], data[p + 2]
            a, b, cc = struct.unpack_from('<3H', data, p + 3)
            o.faces.append((t, c, tex, a, b, cc))
            p += 9
        o.verts = []
        for i in range(o.nv):
            o.verts.append(struct.unpack_from('<5h', data, p))
            p += 10
        objs.append(o)
    return dict(nv=nv, nf=nf, no=no, h4=h4, h5=h5, end=p, size=len(data)), objs


def read_ani(data):
    """-> header (byte0, refreshes per key, number of keys), key 0, list of delta keys.
    key: (word0, camera 6 ints, [(object, 6 ints)])."""
    b0, per = data[0], data[1]
    nkeys = struct.unpack_from('<I', data, 2)[0]
    p = 6
    keys = []
    while p < len(data):
        w0 = struct.unpack_from('<H', data, p)[0]
        cam = struct.unpack_from('<6i', data, p + 2)
        p += 0x1a
        objs = []
        while True:
            i = struct.unpack_from('<H', data, p)[0]
            if i == 0xffff:
                p += 2
                break
            objs.append((i, struct.unpack_from('<6i', data, p + 2)))
            p += 0x1a
        keys.append((w0, cam, objs))
    return dict(b0=b0, per=per, nkeys=nkeys, end=p, size=len(data)), keys


if __name__ == '__main__':
    import collections
    for n in ('INTRO', 'P3D1', 'PART2', 'P3D3', 'PART4', 'FIN'):
        h, objs = read_wld(read_file('3D/%s.WLD' % n))
        print(n, h)
        for k, o in enumerate(objs):
            types = collections.Counter(f[0] for f in o.faces)
            cols = collections.Counter(f[1] for f in o.faces)
            texs = collections.Counter(f[2] for f in o.faces)
            print('  obj %d kind %d nv %d nf %d pos %s types %s cols %s texs %s' % (
                k, o.kind, o.nv, o.nf, o.pos, dict(types), dict(cols), dict(texs)))
        ha, keys = read_ani(read_file('3D/%s.ANI' % n))
        print('  ANI', ha, 'keys in file', len(keys), 'objs per key', collections.Counter(len(k[2]) for k in keys))
