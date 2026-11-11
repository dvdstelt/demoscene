import pickle, numpy as np, struct
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
W=pickle.load(open(O+'x.wrec','rb'))
w=np.frombuffer(W['waves0'],np.int32).reshape(72,3)
print('// {phase, amp, speed} x 72 at part start (after 0x2756, before the first tick)')
for i in range(0,72,4):
    print('  '+' '.join('{%d,%d,%d},'%tuple(int(v) for v in w[j]) for j in range(i,i+4)))
f0=np.frombuffer(W['frames'][0]['waves'],np.int32).reshape(72,3)
print('frame0 phases - init', (f0[:,0]-w[:,0])[:8], 'speed', w[:8,2])
rs=W['rand_before_2756']; print('rand idx', rs[256], rs[257])
# check: speed range
print('amp range', w[:,1].min(), w[:,1].max(), 'speed range', w[:,2].min(), w[:,2].max())
# phase at end of part
fl=np.frombuffer(W['frames'][-1]['waves'],np.int32).reshape(72,3)
print('last frame tick', W['frames'][-1]['tick'], 'phase delta/speed', ((fl[:,0]-w[:,0])/np.where(w[:,2]==0,1,w[:,2]))[:6])
