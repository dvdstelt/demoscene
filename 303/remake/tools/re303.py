"""Disassembly helper for 303.EXE: xrefs to strings, function dumps."""
import sys, struct, re
sys.path.insert(0, __import__('os').path.dirname(__file__))
from le_load import load
import capstone

EXE = __import__('os').path.join(__import__('os').path.dirname(__file__), '../../original/303.EXE')
objs, mem, entry, _ = load(EXE)
CODE, CB = mem[1], objs[0]['base']
DATA, DB = mem[2], objs[1]['base']
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = False

def rd32(addr):
    if CB <= addr < CB + len(CODE): return struct.unpack_from('<I', CODE, addr - CB)[0]
    return struct.unpack_from('<I', DATA, addr - DB)[0]

def cstr(addr):
    buf = DATA if addr >= DB else CODE; o = addr - (DB if addr >= DB else CB)
    e = buf.index(b'\0', o); return buf[o:e].decode('latin1')

def find_str(s):
    res = []
    for m in re.finditer(re.escape(s.encode()), DATA): res.append(DB + m.start())
    return res

def xrefs(addr):
    """code locations containing the 32-bit immediate addr"""
    pat = struct.pack('<I', addr); out = []
    i = CODE.find(pat)
    while i >= 0: out.append(CB + i); i = CODE.find(pat, i + 1)
    return out

def dis(start, n=60, stop_ret=True):
    lines = []
    for ins in md.disasm(bytes(CODE[start - CB:start - CB + n * 15]), start):
        txt = f'{ins.address:08x}: {ins.mnemonic:6s} {ins.op_str}'
        for m in re.finditer(r'0x([0-9a-f]{5,8})', ins.op_str):
            v = int(m.group(1), 16)
            if DB <= v < DB + len(DATA):
                try:
                    s = cstr(v)
                    if 2 <= len(s) < 60 and all(32 <= ord(c) < 127 for c in s): txt += f'   ; "{s}"'
                except ValueError: pass
        lines.append(txt)
        if len(lines) >= n or (stop_ret and ins.mnemonic == 'ret'): break
    return '\n'.join(lines)

def func_start(addr):
    """walk back to a likely function start (after a ret / int3 padding)"""
    a = addr
    while a > CB:
        b = CODE[a - CB - 1]
        if b == 0xC3 and CODE[a - CB] in (0x53, 0x55, 0x56, 0x57, 0x51, 0x52, 0x83, 0x89, 0x8b, 0x60, 0x50, 0x68, 0xb8, 0xba): return a
        a -= 1
    return a

def calls_to(target):
    out = []
    for i in range(len(CODE) - 5):
        if CODE[i] == 0xE8 and CB + i + 5 + struct.unpack_from('<i', CODE, i + 1)[0] == target: out.append(CB + i)
    return out

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'str':
        for s in sys.argv[2:]:
            for a in find_str(s):
                print(f'"{s}" @ {a:#x} xrefs:', ' '.join(f'{x:#x}' for x in xrefs(a)))
    elif cmd == 'dis':
        print(dis(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 80, stop_ret=False))
    elif cmd == 'calls':
        print(' '.join(f'{x:#x}' for x in calls_to(int(sys.argv[2], 16))))

def func(start, limit=20000):
    """recursive-descent listing of one function (follows jumps, not calls)"""
    seen, todo, out = set(), [start], {}
    while todo and len(out) < limit:
        a = todo.pop()
        while a not in seen and CB <= a < CB + len(CODE):
            seen.add(a)
            ins = next(md.disasm(bytes(CODE[a - CB:a - CB + 15]), a), None)
            if ins is None: break
            out[a] = ins
            m = ins.mnemonic
            if m.startswith('j') or m == 'loop':
                try: todo.append(int(ins.op_str, 16))
                except ValueError: pass
                if m == 'jmp': break
            if m in ('ret', 'retf', 'iretd'): break
            a += ins.size
    lines = []
    for a in sorted(out):
        ins = out[a]
        txt = f'{a:08x}: {ins.mnemonic:6s} {ins.op_str}'
        for mm in re.finditer(r'0x([0-9a-f]{5,8})', ins.op_str):
            v = int(mm.group(1), 16)
            if DB <= v < DB + len(DATA):
                try:
                    s = cstr(v)
                    if 2 <= len(s) < 60 and all(32 <= ord(c) < 127 for c in s): txt += f'   ; "{s}"'
                except ValueError: pass
                if 'dword ptr [0x' in ins.op_str and ins.mnemonic.startswith('f'):
                    txt += f'   ; ={struct.unpack_from("<f", DATA, v - DB)[0]:.6g}'
                if 'qword ptr [0x' in ins.op_str and ins.mnemonic.startswith('f'):
                    txt += f'   ; ={struct.unpack_from("<d", DATA, v - DB)[0]:.9g}'
        lines.append(txt)
    return '\n'.join(lines)

if __name__ == '__main__' and sys.argv[1] == 'func':
    print(func(int(sys.argv[2], 16)))
