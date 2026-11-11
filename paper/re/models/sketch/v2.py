from mm import M
import numpy as np
img = M('img'); s = M(1250)
a = np.frombuffer(img.b[0x169a3:0x169a3+768], np.uint8).reshape(256,3)
b = np.frombuffer(s.b[0x169a3:0x169a3+768], np.uint8).reshape(256,3)
c = np.frombuffer(s.b[0x1eaad:0x1eaad+768], np.uint8).reshape(256,3)
p = np.frombuffer(s.pal, np.uint8).reshape(256,3)
print('img==snap', (a==b).all(), 'work==base', (c==b).all(), 'dac==base', (p==b).all())
for i in range(256):
    print(i, hex(i), b[i].tolist(), end=' | ' if i%4!=3 else '\n')
