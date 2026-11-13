"""Extract the module buffer (0x428470, 0x124f80 bytes) and MXMPlay's data block (0xe30aec, 0x4000) from a snapshot.
usage: python3 dumpmod.py ../../snaps/t11.snap OUTDIR"""
import os, pickle, sys
import unicorn  # noqa: F401  (the pickle holds a unicorn context)
snap = pickle.load(open(sys.argv[1], 'rb'))
mem = snap['mem']
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, 'module.bin'), 'wb').write(mem[0x428470:0x428470 + 0x124f80])
open(os.path.join(out, 'xmpdata.bin'), 'wb').write(mem[0xe30aec:0xe30aec + 0x4000])
print('ok', snap['st'].get('tick'))
