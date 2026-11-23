"""Number of Watcom rand() steps between seeds (0x423818) seen in the snapshots: seed' = seed * 0x41c64e6d + 0x3039."""
def steps(a, b, limit=10_000_000):
    s, n = a, 0
    while s != b:
        s = (s * 0x41c64e6d + 0x3039) & 0xffffffff
        n += 1
        if n > limit:
            return None
    return n
print('seed 1 -> after intro init (260) -> credits init start: %#x' % 0x8e60e23d, steps(1, 0x8e60e23d))
print('srand 0x45eafc -> end of init 0xf83d63af:', steps(0x45eafc, 0xf83d63af))
print('end of init 0xf83d63af -> f38 0xc7ddea69 (snapshot f38):', steps(0xf83d63af, 0xc7ddea69))
print('end of init -> f1446/f2148/f4255 0xde09b53b (after the intro):', steps(0xf83d63af, 0xde09b53b))
print('0xde09b53b -> f4963/f7063 0x95a8f93f (flower start 0x4098c4):', steps(0xde09b53b, 0x95a8f93f))
