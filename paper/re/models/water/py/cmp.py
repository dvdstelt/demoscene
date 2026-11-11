import numpy as np
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
a=np.load(O+'mem120_tex.npy'); b=np.load(O+'mem8000_tex.npy'); g=np.load(O+'gen_tex.npy').reshape(256,256)
print('t120 vs t8000 bank0 diff', (a[:256]!=b[:256]).sum())
print('bank1 at t120 nonzero', (a[256:]!=0).sum())
d=np.nonzero(g!=a[:256]); print('gen vs t120', len(d[0]))
np.set_printoptions(linewidth=220)
print(g[240:256,:16]); print(a[240:256,:16])
l1=np.load(O+'mem120_lightmap.npy'); l2=np.load(O+'mem8000_lightmap.npy'); print('lightmap t120 vs t8000', (l1!=l2).sum())
