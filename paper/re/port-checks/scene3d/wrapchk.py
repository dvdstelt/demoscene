exec(open('maskalign.py').read().split('for N in list')[0])
mins=[]
for P in range(3186,3442):
    N=table(P); s=N[:320].min()+N[0x207-np.arange(200)].min()
    mins.append((s,P))
    if P%20==0: print(P, N[:320].min(), N[0x207-np.arange(200)].min(), N[:320].max(), N[0x207-np.arange(200)].max())
print(min(mins))
