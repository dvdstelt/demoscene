"""Snapshot helpers for the engine scope: load a run_jizz.py snapshot and disassemble / read from its memory.
Run from jizz/re or anywhere (paths are resolved from this file)."""
import os, sys, pickle, struct
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import unicorn  # noqa: F401  (needed to unpickle)
import capstone
import numpy as np

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


class Snap:
    def __init__(self, name):
        p = name if os.path.exists(name) else os.path.join(RE, 'snaps', name if name.endswith('.snap') else f't{name}.snap')
        with open(p, 'rb') as f:
            self.d = pickle.load(f)
        self.mem = self.d['mem']

    def u32(self, a): return struct.unpack_from('<I', self.mem, a)[0]
    def i32(self, a): return struct.unpack_from('<i', self.mem, a)[0]
    def f32(self, a): return struct.unpack_from('<f', self.mem, a)[0]
    def u8(self, a): return self.mem[a]
    def u16(self, a): return struct.unpack_from('<H', self.mem, a)[0]
    def arr(self, a, n, dt): return np.frombuffer(bytes(self.mem[a:a + n * np.dtype(dt).itemsize]), dtype=dt)

    def dis(self, a, n=40):
        out = []
        for ins in md.disasm(bytes(self.mem[a:a + n * 15]), a):
            out.append(f'{ins.address:06x}: {ins.mnemonic:6s} {ins.op_str}')
            if len(out) >= n: break
        return '\n'.join(out)
