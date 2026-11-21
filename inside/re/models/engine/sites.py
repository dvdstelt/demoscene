"""Print the instructions before each call site of the given function (default 0x410850)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
fn = int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 4
for c in rep.call_targets().get(fn, []):
    seq = rep._before(c - rep.BASE, n)
    print(f'--- {c:06x}: ' + ' | '.join(f'{s.mnemonic} {s.op_str}' for s in seq))
