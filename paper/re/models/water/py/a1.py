import numpy as np
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
sh=np.load(O+'mem8000_shade.npy')[0]  # table at 0x2e0000
np.set_printoptions(linewidth=200)
for r in [0,1,2,8,16,20,24,31,32,33,40,63,64,100,128,200,255]:
    print(r, sh[r,::16], sh[r,255])
