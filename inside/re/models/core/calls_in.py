"""Calls and stores to globals made by a routine (recursive descent, jumps followed, calls not)."""
import os
import re
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
for a in sys.argv[1:]:
    lst = rep.func(int(a, 16)).split('\n')
    calls = [l for l in lst if ' call ' in l or ' jmp    0x40' in l]
    stores = [l for l in lst if re.search(r'mov\s+(dword|word|byte) ptr \[0x4[2-9a-b][0-9a-f]{4}\],', l)]
    print('===', a, len(lst), 'instructions')
    for l in calls + stores:
        print('   ', l)
