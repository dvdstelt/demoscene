import sys, numpy as np
sys.path.insert(0,'/home/dennis/Downloads/acme_303/paper/re')
import rep
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
SEG=rep.SEG

def blur_f90(buf, src, dst, count, stride):
    # dst[i] = (buf[src+i-stride] + buf[src+i+stride] + buf[src+i-1] + buf[src+i+1]) >> 2, sequential (in-place safe? src != dst)
    for i in range(count):
        s=src+i
        buf[dst+i]=(int(buf[s-stride])+buf[s+stride]+buf[s-1]+buf[s+1])>>2

def blur_f47_np(img_rows_padded):
    pass

# ---- texture 0x10d3
def gen_texture():
    tex=np.zeros(0x20000,np.uint8)  # two banks; bank1 assumed zero at init (emulator)
    src=SEG[0x16ca3:0x16ca3+64*32]
    for r in range(64):
        for c in range(32):
            b=src[r*32+c]; hi=(b>>4)*16; lo=(b&15)*16
            row=2*r
            for rr in (row,row+1):
                tex[rr*256+4*c:rr*256+4*c+4]=[hi,hi,lo,lo]
                tex[rr*256+256-4*c-4:rr*256+256-4*c]=[lo,lo,hi,hi]
    t=tex[:0x10000].copy()
    tex[:0x10000]=t[::-1]*0  # placeholder
    # reversal: tex[0xffff-k]=tex[k] processed dword-wise sequentially
    a=t.copy()
    for i in range(0x4000):
        d=a[4*i:4*i+4].copy()
        a[0x10000-4-4*i:0x10000-4*i]=d[::-1]
    tex[:0x10000]=a
    # blur 3x: copy 512 rows to scratch stride 260 (+4 wrap bytes), blur rows 0..259 (ecx=260 rows starting at scratch+0), copy back from +0x20a
    for it in range(3):
        sc=np.zeros(0x20800+2000,np.int32); base=1000
        for r in range(512):
            row=tex[(r&255)*256:((r&255)+1)*256]
            sc[base+r*260:base+r*260+256]=row
            sc[base+r*260+256:base+r*260+260]=row[:4]
        n=260*260
        temp=np.zeros(n+1280+10,np.int32); tb=640
        # pass1 src=sc[base..] -> temp[tb..]
        s=base; 
        idx=np.arange(n)
        # sequential semantics irrelevant since src!=dst
        temp[tb:tb+n]=(sc[s+idx-260]+sc[s+idx+260]+sc[s+idx-1]+sc[s+idx+1])>>2
        sc[s:s+n]=(temp[tb+idx-260]+temp[tb+idx+260]+temp[tb+idx-1]+temp[tb+idx+1])>>2
        for r in range(256):
            tex[r*256:(r+1)*256]=sc[base+0x20a+r*260:base+0x20a+r*260+256]
    return tex

def gen_font():
    font=np.zeros(0xe800,np.uint8)
    si=0x18ed7; di=0
    for g in range(29):
        font[di]=SEG[si]; si+=1; di+=1
        for row in range(32):
            p=di
            while True:
                b=SEG[si]; si+=1
                if b==0: break
                if b==0xff:
                    skip=SEG[si]; cnt=SEG[si+1]; si+=2
                else:
                    skip=b&15; cnt=b>>4
                p+=skip
                for k in range(cnt if cnt else 256):
                    font[p]=0xff; p+=1
            di+=0x3c
        di+=0x7f
    return font, si

if __name__=='__main__':
    t='8000'
    tex=gen_texture()
    snap=np.load(O+'mem%s_tex.npy'%t).ravel()
    d=tex[:0x10000].astype(int)-snap[:0x10000]
    print('tex diff bank0: max',abs(d).max(),'count',(d!=0).sum())
    print('rows with diffs', np.unique(np.nonzero(d.reshape(256,256))[0])[:50])
    font,end=gen_font()
    sf=np.load(O+'mem%s_font.npy'%t).ravel()
    print('font diff', (font[:26*2048]!=sf).sum(), 'rle end', hex(end), 'widths', [font[g*2048] for g in range(29)])
    np.save(O+'gen_tex.npy',tex[:0x10000]); np.save(O+'gen_font.npy',font)

def gen_lightmap(tex):
    tex=tex.reshape(256,256)
    lm=np.zeros((256,512),np.uint8)
    for j in range(256):
        y=j-128
        trow=(-j)&255
        for i in range(512):
            x=i-256
            d=int(np.rint(np.sqrt(np.sqrt(float(x*x+y*y)))*32.0))
            v=300-d
            v=max(0,min(255,v))
            if v>192: v=(v-128)&255
            else: v=(v*0x5555)>>16
            a=(v<<1)&255
            a=(a-(a>>3))&255
            t=int(tex[trow,(-i)&255])>>1
            a=a+t
            lm[j,i]=min(a,255)
    return lm

def gen_glyphs(font):
    # 26 glyphs a..z: 52x32 cell (glyph at x=2), rows doubled -> 52x64, then 4 blur passes (0xf47 twice)
    N=26*64*52
    buf=np.zeros(N+4000,np.int32); B=2000  # strip at buf[B:B+N]
    for g in range(26):
        w=font[g*2048]
        cell=np.zeros(52*32+52,np.int32)
        for r in range(32):
            for c in range(w):
                p=2+r*52+c
                cell[p]=min(255,cell[p]+font[g*2048+1+r*60+c])
        cell=cell[:52*32].reshape(32,52)
        for r in range(32):
            buf[B+(g*64+2*r)*52:B+(g*64+2*r+1)*52]=cell[r]
            buf[B+(g*64+2*r+1)*52:B+(g*64+2*r+2)*52]=cell[r]
    n=1662*52; s=B+52
    idx=np.arange(n)
    for it in range(2):
        temp=np.zeros(n+1280,np.int32); tb=640
        temp[tb:tb+n]=(buf[s+idx-52]+buf[s+idx+52]+buf[s+idx-1]+buf[s+idx+1])>>2
        buf[s:s+n]=(temp[tb+idx-52]+temp[tb+idx+52]+temp[tb+idx-1]+temp[tb+idx+1])>>2
    return buf[B:B+N].astype(np.uint8).reshape(26*64,52)

if __name__=='__main__':
    lm=gen_lightmap(tex[:0x10000])
    sl=np.load(O+'mem%s_lightmap.npy'%t)
    print('lightmap diff', (lm!=sl).sum(), np.argwhere(lm!=sl)[:10])
    np.save(O+'gen_lightmap.npy',lm)
    gs=gen_glyphs(font)
    sg=np.load(O+'mem%s_glyph.npy'%t)
    dd=np.argwhere(gs!=sg); print('glyph diff', len(dd), dd[:10])
