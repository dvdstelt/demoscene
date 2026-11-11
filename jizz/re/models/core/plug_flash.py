# plugin for models/core/run_jizz.py: records the colour tables after every 0x1a229 rebuild (flash) with the
# flash variables, into <out>.flash.pickle (checked by colour.py --flash)
from unicorn import UC_HOOK_CODE
import pickle
rec = []
def on(mu_, addr, size, _):
    F = mu.mem_read(0x1a478, 1)[0]; dec = mu.mem_read(0x6386, 1)[0]
    S = struct.unpack('<I', mu.mem_read(0x1a225, 4))[0]; E = struct.unpack('<i', mu.mem_read(0x6387, 4))[0]
    rec.append({'tick': st['tick'], 'flips': st['flips'], 'site': addr, 'F': F, 'dec': dec, 'S': S, 'E': E,
                't768': bytes(mu.mem_read(0x1a479, 0x2400)), 't512': bytes(mu.mem_read(0x1c879, 0x1800))})
for h in (0x63d6, 0x1720c):
    mu.hook_add(UC_HOOK_CODE, on, None, h, h)
def atexit_plugin():
    pickle.dump(rec, open(a.out + '.flash.pickle', 'wb'))
    print('flash records', [(r['tick'], r['flips'], hex(r['site']), r['F'], r['dec']) for r in rec])
