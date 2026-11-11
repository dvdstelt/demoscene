"""Check texgen.py op by op against the original interpreter 0x12e72 (in unicorn, emu.py), on the 17 scripts and on
a made-up script that uses the ops and argument ranges the 17 scripts do not (0x0d, 0x16, 0x1e, 0x25, negative
scales and matrix entries, amplitudes above 127, alpha 0, odd mask combinations).

usage (inside the toolbox, from jizz/re):  python3 models/textures/test_ops.py [all]
"""
import struct
import sys

import numpy as np

import emu
import texgen

TEST_AT = 0x32000          # zero BSS in the image, free in the emulator run (stack is elsewhere)
TEST = bytes.fromhex(
    '03'                        # noise
    '1e0703fe05'                # mblur RGB dx=3 dy=-2 n=5
    '1e02fb0704'                # mblur G dx=-5 dy=7 n=4
    '0d01d4fe'                  # scale (wrap) R by -300
    '0c049cff'                  # scale (sat) B by -100
    '16020a05' + struct.pack('<8h', 40, 90, -60, 30, 10, 200, 300, -100).hex() +   # curve G
    '25'                        # nop
    '1f04'                      # angle -> B
    '1c0530'                    # cone R,B r=48
    '1103000200'                # mixmask RG from B0 by B0.G
    '120633f00501'              # emboss G (mask 6 -> first plane G) angle 0x33 s=-16 -> B1.R, W = B1
    '0000'                      # select B0
    '239c4010f02030a07f80'      # matrix
    '0a064000'                  # blobs on G, 64
    '0807'                      # rgb2hsv, hsv2rgb
    '140405fb03fd7f81c8'        # sines B, amp 200
    '0e0701'                    # mul RGB by B1
    '0f0501'                    # addbuf R,B with B1
    '10070100'                  # mix alpha 0
    '13060501'                  # addsat G,B += B1.R
    '150501'                    # warp R,B by B1
    '1b'                        # addxy
    '0b0d0e'                    # shift 13, 14
    '1803'                      # invert R,G
    '190702'                    # copy RGB -> B2
    '1a020403'                  # copy plane G -> B3.B, W = B3
    '1d03c8'                    # clouds R,G k=200 (only R: first plane of the mask)
    '040705070607'              # blurs
    '24'                        # interleave
    '0000' '20' '21'            # select B0, tile320, to16bit
    '01')


def compare(script, img, dest=0x6582, setup=None):
    """Returns None when the model's final buffer equals the emulator's, else (op address, buffer, bytes that
    differ) for the first op after which a work buffer differs."""
    states, final, bufs, uc = emu.run_script(script, dest, extra=setup)
    tg = texgen.TexGen()
    if np.array_equal(np.frombuffer(final, np.uint8), tg.run(script, img)):
        return None
    ops = [s[0] for s in states]
    for k in range(1, len(states)):          # rerun the model with an end op (1) in place of op k
        cut = bytearray(img)
        cut[ops[k]] = 0x01
        tg = texgen.TexGen()
        tg.run(script, bytes(cut))
        for b in range(4):
            ref = np.frombuffer(states[k][2][f'B{b}'], np.uint8)
            if not np.array_equal(ref, tg.B[b]):
                return ops[k - 1], b, int((ref != tg.B[b]).sum())
    return 'final copy', None, None


def main():
    img = bytearray(texgen.IMG)
    img[TEST_AT:TEST_AT + len(TEST)] = TEST
    img = bytes(img)

    def setup(uc):
        uc.mem_write(TEST_AT, TEST)

    bad = compare(TEST_AT, img, setup=setup)
    print('test script:', 'OK' if bad is None else f'first difference after the op at {bad}')
    if 'all' in sys.argv:
        for s, dst in texgen.BUILDS:
            bad = compare(s, texgen.IMG, dst)
            print(f'script {s:#x}:', 'OK' if bad is None else bad)


if __name__ == '__main__':
    main()
