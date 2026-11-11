import sys, struct, pickle, numpy as np
sys.path.insert(0,'/home/dennis/Downloads/acme_303/paper/re')
import rep
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
SEG=rep.SEG
SIN=[struct.unpack_from('<i',SEG,0)[0]]*0
# sine table is generated at init (FPU); read from snapshot dump instead
def load_sin():
    s=pickle.load(open(O+'sin.pkl','rb')); return s
def sar(v,n): return v>>n  # python ints: arithmetic
def i32(v): v&=0xffffffff; return v-(1<<32) if v&0x80000000 else v

def curve(P0,P1,P2,n):
    step=((65536//n))>>7
    b=4*P1-P2-3*P0
    c=P0-2*P1+P2
    out=[]
    e=0
    for k in range(n+1):
        ecx=i32(b*e); esi=i32(i32(c*e)*e)>>8
        ecx=i32(ecx+esi)>>8
        ecx=i32(ecx+2*P0)>>1
        out.append(ecx); e+=step
    return out

def grid_from_waves(W, SIN, sh_a=6, sh_b=6):
    # W: 72x3 (phase, amp, speed)
    tab=[]
    r=0
    for j in range(3):
        pts=[]
        for i in range(3):
            xs=0; ys=0
            for cl in (4,3,2,1):
                ph,amp,_=W[r]; idx=(i32(ph<<cl)>>7)&0x3ff; s=i32(SIN[idx]*amp)>>16; xs+=s>>cl; r+=1
                ph,amp,_=W[r]; idx=(i32(ph<<cl)>>7)&0x3ff; s=i32(SIN[idx]*amp)>>16; ys+=s>>cl; r+=1
            pts.append((i32(xs),i32(ys)))
        X=curve(pts[0][0],pts[1][0],pts[2][0],40)
        Y=curve(pts[0][1],pts[1][1],pts[2][1],40)
        tab.append((X,Y))
    G=np.zeros((26,41,3),np.int64)
    for k in range(41):
        xs=curve(tab[0][0][k],tab[1][0][k],tab[2][0][k],25)
        ys=curve(tab[0][1][k],tab[1][1][k],tab[2][1][k],25)
        for rr in range(26):
            G[rr,k,0]=i32(ys[rr]<<sh_a); G[rr,k,1]=i32(xs[rr]<<sh_b); G[rr,k,2]=0x1400
    return G

def warp(G, tex, T):
    # T: 256x256 shade table page; returns 200x320
    tex=tex.reshape(256,256)
    out=np.zeros((200,320),np.uint8)
    for R in range(25):
        for K in range(40):
            v00=G[R,K]; v10=G[R,K+1]; v01=G[R+1,K]; v11=G[R+1,K+1]
            st={}
            for c in range(3):
                ax=i32(v10[c]-v00[c])>>3
                aax=i32((v11[c]-v01[c])-(v10[c]-v00[c]))>>6
                ay=i32(v01[c]-v00[c])>>3
                st[c]=(ax,aax,ay)
            for y in range(8):
                for x in range(8):
                    vals=[]
                    for c in range(3):
                        ax,aax,ay=st[c]
                        vals.append((int(v00[c])+y*ay+x*(ax+y*aax))&0xffff)
                    a,b,s=vals
                    t=tex[a>>8,b>>8]
                    out[R*8+y,K*8+x]=T[s>>8,t]
    return out

FONT=None
def draw_text(h, slots, glyphs, font, T):
    # h: 202x320 int array (rows 0..201), modified in place
    for s in range(4):
        level=slots[4+5*s]; ptr=struct.unpack_from('<I',slots,4+5*s+1)[0]
        if ptr==0: continue
        x=5; y=46*s
        p=ptr
        while SEG[p]!=0:
            ch=SEG[p]; p+=1
            if ch==0x20: x+=30; continue
            g=ch-0x61
            cell=glyphs[g*64:(g+1)*64]  # 64x52
            v=T[level][cell]
            # linear addressing: rows of 320; x may exceed 320-52 -> wraps into next row (emulate linear)
            flat=h.reshape(-1)
            for r in range(64):
                o=(y+r)*320+x
                seg=flat[o:o+52]
                np.maximum(seg, v[r], out=seg)
            x+=int(font[g*2048])+4
    return h

def bump(h, lm, ang, SIN):
    # h: rows -1..201 as array index 0..202 ; return 200x320
    A=SIN[ang&0x3ff]>>10; E=SIN[(ang+0x100)&0x3ff]>>12
    flat=lm.reshape(-1).astype(np.int64)
    out=np.zeros((200,320),np.uint8)
    H=h.astype(np.int64)
    for y in range(1,199):
        hy=y+1  # index in H (H[0] = row -1)
        for x in range(320):
            dx=((H[hy,x-1] if x>0 else H[hy-1,319]) + 128 - (H[hy,x+1] if x<319 else H[hy+1,0]))&255
            dy=(H[hy-1,x]+128-H[hy+1,x])&255
            dx>>=1; dy>>=1
            addr=(E+14+(y>>1)+dy)*512+(A+112+(x>>1)+dx)
            out[y,x]=flat[addr] if 0<=addr<flat.size else 0
        out[y,0]=0; out[y,319]=0
    return out

if __name__=='__main__':
    SIN=load_sin()
    W=pickle.load(open(O+'x.wrec','rb'))
    tex=np.load(O+'gen_tex.npy'); lm=np.load(O+'gen_lightmap.npy'); font=np.load(O+'gen_font.npy')
    sh=np.load(O+'mem8000_shade.npy')[0]
    glyphs=np.load(O+'mem8000_glyph.npy')
    print('first cb tick', W['ticks'][0][:6], 'init tick', W['init_tick'])
    for n in sorted(W['dumps']):
        d=W['dumps'][n]; f=W['frames'][n]
        wv=np.frombuffer(d['waves'],np.int32).reshape(72,3).astype(np.int64)
        G=grid_from_waves(wv.tolist(), SIN)
        Gs=np.frombuffer(d['grid'],np.int32).reshape(26,41,3)
        gd=(G!=Gs).sum()
        hw=np.frombuffer(d['hwarp'],np.uint8).reshape(203,320)
        Wm=warp(Gs.astype(np.int64), tex, sh)
        wd=(Wm!=hw[1:201]).sum()
        ht=np.frombuffer(d['htext'],np.uint8).reshape(203,320)
        h2=hw.astype(np.int64).copy()
        draw_text(h2[1:], d['slots'], glyphs, font, sh)
        td=(h2!=ht).sum()
        B=bump(ht, lm, d['lang'], SIN)
        vg=np.frombuffer(d['vga'],np.uint8).reshape(200,320)
        bd=(B!=vg).sum()
        print('frame',n,'tick',f['tick'],'lang',d['lang'],'grid diff',gd,'warp diff',wd,'text diff',td,'bump diff',bd)
