import numpy as np
from PIL import Image
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
t='8000'
tex=np.load(O+'mem%s_tex.npy'%t)
lm=np.load(O+'mem%s_lightmap.npy'%t)
hm=np.load(O+'mem%s_hmap.npy'%t)
gl=np.load(O+'mem%s_glyph.npy'%t)
vga=np.load(O+'mem%s_vga.npy'%t)
pal=np.load(O+'mem%s_pal.npy'%t)
W=Image.new('L',(1100,700))
W.paste(Image.fromarray(tex),(0,0))        # 256x512 (two banks)
W.paste(Image.fromarray(lm),(260,0))       # 512x256
W.paste(Image.fromarray(hm),(260,260))     # 320x202
gl2=gl.reshape(26,64,52)
strip=np.concatenate([gl2[i] for i in range(13)],axis=1)
strip2=np.concatenate([gl2[i] for i in range(13,26)],axis=1)
W.paste(Image.fromarray(np.concatenate([strip,strip2],0)),(0,520))
W.save(O+'data8000.png')
p=(pal.astype(np.uint16)*255//63).astype(np.uint8)
Image.fromarray(p[vga]).resize((640,400),Image.NEAREST).save(O+'vga8000.png')
print(hm.min(),hm.max(), np.bincount(hm.ravel(),minlength=256)[:40])
