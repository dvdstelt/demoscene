import numpy as np
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
FR=64768
a=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',np.uint8,'r')
b=np.memmap(O+'x.frames',np.uint8,'r')
bad=0
for t in range(7741,8818):
    if (a[t*FR:(t+1)*FR]!=b[(t-7000)*FR:(t-6999)*FR]).any(): bad+=1; print(t, (a[t*FR:t*FR+64000]!=b[(t-7000)*FR:(t-7000)*FR+64000]).sum(), (a[t*FR+64000:(t+1)*FR]!=b[(t-7000)*FR+64000:(t-6999)*FR]).sum())
print('ticks differing from full.frames:', bad)
