005b7d: mov    bl, 1
005b7f: mov    edx, 0x7a6f
005b84: call   dword ptr [0x2a]
005b8a: mov    esi, 0x4530   ; "Statix and Vic present their Wired '96 64k intro (c) 1996"
005b8f: call   0x595d
005b94: mov    edi, 0x1ee19
005b99: xor    ecx, ecx
005b9b: mov    eax, 0x10000
005ba0: xor    edx, edx
005ba2: inc    ecx
005ba3: div    ecx
005ba5: stosd  dword ptr es:[edi], eax
005ba6: cmp    ecx, 0x7ff
005bac: jbe    0x5b9b
005bae: call   0x593b
005bb3: mov    eax, 0x540
005bb8: mov    ecx, 0x150
005bbd: call   0x240
005bc2: mov    dword ptr [0x2449d], eax
005bc7: mov    edi, eax
005bc9: xor    eax, eax
005bcb: rep stosd dword ptr es:[edi], eax
005bcd: mov    dword ptr [0x24499], eax
005bd2: mov    eax, 0x800
005bd7: call   0x240
005bdc: mov    dword ptr [0x1ee05], eax
005be1: mov    dword ptr [0x1ee09], eax
005be6: mov    ecx, 0x400
005beb: mov    edi, eax
005bed: call   0xaa2
005bf2: and    al, 0x1f
005bf4: cmp    al, 5
005bf6: jae    0x5bff
005bf8: sar    al, 2
005bfb: add    al, 0x1e
005bfd: jmp    0x5c04
005bff: shr    al, 3
005c02: add    al, 0x1a
005c04: cmp    al, 0x20
005c06: jbe    0x5c0a
005c08: mov    al, 0x1f
005c0a: stosb  byte ptr es:[edi], al
005c0b: loop   0x5bed
005c0d: mov    dword ptr [0x1ee0d], edi
005c13: mov    ecx, 0x400
005c18: xor    ebx, ebx
005c1a: mov    edx, 0x78
005c1f: call   0xaa2
005c24: and    eax, 0xf
005c27: sub    eax, 7
005c2a: add    ebx, eax
005c2c: dec    edx
005c2d: jne    0x5c1f
005c2f: sar    ebx, 6
005c32: dec    bl
005c34: mov    byte ptr [edi], bl
005c36: inc    edi
005c37: loop   0x5c18
005c39: mov    eax, 0x20000
005c3e: call   0x240
005c43: add    eax, 0x800
005c48: mov    dword ptr [0x1edfd], eax
005c4d: mov    eax, 0x201ff
005c52: call   0x240
005c57: add    eax, 0x1ff
005c5c: and    eax, 0xfffffe00
005c61: mov    dword ptr [0x1edd1], eax
005c66: mov    eax, 0xfa00
005c6b: call   0x240
005c70: mov    ecx, 0x3e80
005c75: mov    dword ptr [0x1edc5], eax
005c7a: mov    edi, eax
005c7c: xor    eax, eax
005c7e: rep stosd dword ptr es:[edi], eax
005c80: mov    eax, 0x5a0
005c85: call   0x240
005c8a: mov    dword ptr [0x1edd5], eax
005c8f: mov    eax, 0x3f200
005c94: call   0x240
005c99: mov    edi, eax
005c9b: mov    ecx, 0xfc80
005ca0: add    eax, 0x140
005ca5: mov    dword ptr [0x1edc9], eax
005caa: add    eax, 0xfc80
005caf: mov    dword ptr [0x1edcd], eax
005cb4: xor    eax, eax
005cb6: rep stosd dword ptr es:[edi], eax
005cb8: mov    eax, 0x44200
005cbd: call   0x240
005cc2: add    eax, 0xffff
005cc7: and    eax, 0xffff0000
005ccc: mov    dword ptr [0x1eddd], eax
005cd1: add    eax, 0x20000
005cd6: mov    dword ptr [0x1eded], eax
005cdb: mov    eax, 0x31f8
005ce0: call   0x240
005ce5: mov    dword ptr [0x1edd9], eax
005cea: mov    eax, 0x10200
005cef: call   0x240
005cf4: mov    dword ptr [0x1edf5], eax
005cf9: mov    eax, 0xff00
005cfe: call   0x240
005d03: add    eax, 0x280
005d08: mov    dword ptr [0x1edf9], eax
005d0d: mov    edi, eax
005d0f: mov    ecx, 0x3e80
005d14: mov    eax, 0xffffffff
005d19: rep stosd dword ptr es:[edi], eax
005d1b: call   0x593b
005d20: call   0x10d3
005d25: call   0x144b
005d2a: call   0x55e2
005d2f: call   0xe2e
005d34: call   0x593b
005d39: call   0xfb2
005d3e: call   0xc80
005d43: mov    edi, 0x22479
005d48: mov    ecx, 0x400
005d4d: wait   
005d4e: fninit 
005d50: fldz   
005d52: fld    st(0)
005d54: fsin   
005d56: fmul   dword ptr [0x1e460]   ; =65536
005d5c: fistp  dword ptr [edi]
005d5e: fadd   dword ptr [0x1e464]   ; =0.00306796
005d64: fadd   dword ptr [0x1e464]   ; =0.00306796
005d6a: add    edi, 4
005d6d: loop   0x5d52
005d6f: fstp   st(0)
005d71: mov    edi, 0xa0000
005d76: sub    edi, dword ptr [0x18]
005d7c: mov    dword ptr [0x1edf1], edi
005d82: call   0x5979
005d87: call   0x593b
005d8c: mov    word ptr [0x9a], 0x13
005d95: mov    al, 0x10
005d97: int    0x33
005d99: call   0x15af
005d9e: call   0x5b6b
005da3: mov    dword ptr [0x1edbd], 0x8000
005dad: mov    dword ptr [0x1edb9], 0x96
005db7: ret    
