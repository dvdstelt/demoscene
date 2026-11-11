import sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
d = open(sys.argv[1],'rb').read()
start = int(sys.argv[2],0); end = int(sys.argv[3],0); base = int(sys.argv[4],0)
md = Cs(CS_ARCH_X86, CS_MODE_16)
for ins in md.disasm(d[start:end], base):
    print('%04x: %-20s %s %s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
