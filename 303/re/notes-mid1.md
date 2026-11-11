# notes-mid1: tunnel "rings" (0x16198) and the first sub-parts of 0x2e8e4 (flowers, face, facebox)

Conventions in these notes:
- `pos` = [0x9b844] (half-rows). "music" = music seconds (pos.py's first column; the time when `pos` first reaches the value).
- "t_remake" = VT(music) = music*1.00126 - 1.395, the value to write in the remake's `t = VT(c.T)` code.
  "vis" = 1.00643*music - 1.953, the coordinator's fit for when a visual event appears in the reference capture.
- tick = one call of the timer handler 0x1bc01 (vertical retrace, mode 13h, 70.09 Hz; "70 Hz" below).
- A = buffer 0x5fda8, B = buffer 0x6f7a8. F[row][colour] = current fade table [0x5b67c] (64 rows x 256:
  row 0 black, row 32 ~identity, row 63 white; true for 3d.fad and tunnel.fad).
- Correction to the shared helper description: in 0x1cd4b the element m[3] is **-sb*sa**, not +sb*sa
  (the code negates eax before `imul ebp`). Rows: [cb,0,-sb], [-sb*sa,ca,-cb*sa], [sb*ca,sa,cb*ca] (orthonormal).

---------------------------------------------------------------------------------------------------
## 1. Sub-part table of 0x2e8e4 (runs 0x2e8e4..0x3098e)

The main script registers tick callback 0x2e798 right before calling 0x2e8e4 (0x319c0).
The tunnel ends at pos >= 0x801 (music 104.035), so 0x2e8e4 starts there; `round(p)` = (p+0x20) & ~0x3f.
Every loop shows its result with 0x1bbec(src, screen, 64000) (plain memcpy), then 0x332a4, 0x1259c, ESC check.

| # | loop address | per-frame calls (in order) and composition | shown | end condition | end pos | music | t_remake | vis |
|---|---|---|---|---|---|---|---|---|
| init | 0x2e8e4-0x2eb25 | see 2.1; palette all white during loading; [0x9b840]=round(pos)=0x800 | - | - | - | 104.04 start | 102.77 | 102.75 |
| L1 | 0x2eb2a-0x2ec3d | memset(A,0); 0x2bdb4(A); 0x29ae8(B) flowers; 0x1c490(A->B): B=F[32+min(31,2*(A&127))][B] (A brightens B toward white) | B | pos >= 0x840 | 0x840 | 107.332 | 106.073 | 106.070 |
| L2 | 0x2ece7-0x2edef | 0x2b110(A) (A not cleared); 0x29ae8(B); 0x1c503(A->B): B=F[31-min(31,2*(A&127))][B] (A darkens B, >=16 = black) | B | pos >= 0x878 | 0x878 | 110.311 | 109.055 | 109.067 |
| L3 | 0x2ee57-0x2ef5c | 0x29ae8(B); 0x23540(A) face plate+relief textured with B | A | pos >= [0x9b840]+0xc0, [0x9b840]=round(0x878)=0x880 | 0x940 | 120.803 | 119.560 | 119.627 |
| L4 | 0x2efd4-0x2f221 | 0x278a4(230.0) camera goal; 0x27444(3) camera physics; 0x27670(A) hex floor+ceiling; 0x29ae8(B); 0x23540(A) face plate in 3D; if [0x57d74] (typo mode): B=bd2 frame [0x1499fc], 0x1c490(A->B) with wirecut.fad, show B; [0x1499e8]=(pos>[0x9b840]+0x60) | A (B in typo mode) | pos >= 0x940+0xc0 | 0xa00 | 130.871 | 129.641 | 129.759 |
| L4 typo switch | | [0x1499e8]=1 when pos > 0x9a0; callback 0x2e684 then flips [0x57d74], palette := bd2, fade := wire.fad, white flash 1000/tick | | | 0x9a1 | 125.942 | 124.705 | 124.799 |
| L5 | 0x2f2cb-0x2f3f0 | 0x27804(130.0); 0x27444(3); 0x27670(A); 0x26c14(A); 0x29ae8(B); 0x23540(A) | A | pos >= [0x9b840]+0x30, [0x9b840]=pos at L4 end (not rounded) | ~0xa30 | ~133.39 | ~132.16 | ~132.29 |
| L6 | 0x2f415-0x2f657 | 0x27804(130.0); 0x27444(3); 0x27670(A); 0x26c14(A); typo mode: B=bd2 frame 2, 0x1c490(A->B) wirecut.fad, show B; [0x1499e8]=1 when pos >= [0x9b840]+0x100 | A / B | pos >= [0x9b840]+0x140, [0x9b840]=round(~0xa30)=0xa40 | 0xb80 | 151.007 | 149.802 | 150.025 |
| L6 switch | | wire mode from pos >= 0xb40 | | | 0xb40 | 147.651 | 146.442 | 146.647 |
| L7 | 0x2f768-0x2f881 | memset(A,0); 0x2b908(A); 0x2d81c(B, A); 0x1c490(B->A); callbacks saved+cleared, fish.fad, white flash 1000/tick | A | pos >= round+0x80 (0xb80+0x80) | 0xc00 | 157.719 | 156.523 | 156.780 |
| vic | call at 0x2f8b2 | 0x15734(1) (credits routine; ends when its tick callback sees pos > start+0x80, start = exact pos) | | | ~0xc81 | ~164.54 | ~163.35 | ~163.64 |
| L8 | 0x2f97c-0x2fa95 | as L7 (second fish.fad), flash 500/tick | A | round(~0xc81)=0xc80, +0x80 | 0xd00 | 171.143 | 169.964 | 170.290 |
| L9 | 0x2fbb5-0x2fcd1 | 0x27804(230.0); 0x27444(3); 0x27670(A); 0x26c14(A); 0x28100(A) | A | [0xa1558] <= 0.001; callback 0x2e3ac: counter [0x5809c] from -500, +1/tick; after it passes -100, [0xa1558] (=1.0) -= 0.003/tick -> ~734 ticks (estimate) | - | ~181.6 (est.) | | |
| L10 | 0x2fd3c-0x2fe53 | 0x27804(300.0); 0x27444(3); 0x27670(A); 0x28100(A); 0x28330(A); face+flowers freed before | A | [0x13efa4] >= 1.0; callback 0x2e360: [0x58098] from -0.8, +0.008/tick -> 225 ticks = 3.2 s | - | ~184.8 (est.) | | |
| L11 | 0x2fe96-0x2ffb5 | 0x276f8(); 0x27444(1); 0x27670(A); 0x28100(A); 0x28330(A); if [0x1493d8] 0x2902c(A) | A | byte [0x1493dc] != 0, set by callback 0x2e05c when [0xc1d70] passes 1000 (+7/tick once [0xa0194] > 1500, +2/tick above 900); ~8 s (rough estimate) | must be in 0xe60..0xe9f for L12 to end at 0xf00 | 189.6..192.96 | | |
| L12 | 0x300ca-0x301e4 | 0x16970(A) weird.raw; 0x16b34(B, A) paranoid video; 0x1c3f4(B->A): A=F[32+(B>>3)][A]; 0x29d1c(A, 0x67ac8); pinfx3.fad; callbacks saved+cleared | A | round+0x80 | 0xf00 (if L11 ended 0xe60..0xe9f) | 197.991 | 196.845 | 197.311 |
| L13 | 0x30326-0x304e1 | 0x276f8(); 0x27444(1); 0x27670(A); 0x28100(A); 0x28330(A); opt. 0x2902c(A); if [0x580bc]>0: 0x16b34(B), B=LUT[B] with LUT[i]=trunc(i*[0x580bc]), 0x1c3f4(B->A) | A | [0xa0194] >= 14000 (starts 13500; callback 0x2e05c adds ~12+/tick) -> ~0.5 s (estimate) | | ~198.5 | | |
| L14 | 0x3052e-0x306f2 | 0x276f8(); [0xc1d4c]*=3, [0xc1d50]*=1.5; 0x27444(1); 0x27670(A); 0x2902c(A); paranoid fade overlay as L13 | A | round+0x80 | 0xf80 | 204.703 | 203.566 | 204.066 |
| L15 | 0x3079a-0x308d7 | 0x276f8(); 0x28aa0(); 0x27444(1); cam x,y=0; 0x27670(A); 0x28c64(A); from pos > [0x9b840]+0x28 fade target 0 at 100/tick | A | [0x52918] <= 0 (fade to black finished); start flash: negative mode, 70/tick | ~ | ~209 (est.: 0xfa9 = 206.9 + 3.25 s) | | |
| exit | 0x308dd-0x3098e | frees, unregisters 0x2e684, clears callbacks | | | | | | |

Mapping to the remake timeline (timeline.cpp:18-29): rings = tunnel; face = L1+L2+L3; facebox = L4 before the switch;
typo = L4 after the switch; blobs = L5 + L6 before 0xb40; wireblobs = L6 after 0xb40; spiral = L7; vic = 0x15734(1);
star = L8; plasma = L9+L10+L11; paranoid = L12 (+L13); facefloor = L14+L15.

### 1.1 Timer callbacks 0x2e534, 0x2e684, 0x2e798 (all run once per tick, 70 Hz)

The tick handler first runs the palette engine 0x10124 (level L=[0x52918] steps toward T=[0x5291c] by S=[0x8eba8];
brightness B=[0x52920] = 1+(B-1)*0.98; palette written), then callbacks in slot order (only the first 8 of the 16 slots
are ever called). In this section the slots are: 0 = 0x2e798, 1 = 0x2e534, 2 = 0x2b0ec (later 0x23384), 3 = 0x2bd94,
4 = 0x2764c, 5 = 0x280cc, 6 = 0x29aac, 7 = 0x2e684.

**0x2e798** (registered by the main script before 0x2e8e4, removed after L3 at 0x2ef77) — beat flicker:
```c
static int last = -1;                          // [0x580a8]
if (pos != last) {                             // acts once per half-row
    last = pos;
    if ((pos & 3) == 0 && (rand() & 15) < 7) B = 1.2f;   // [0x52920]
    if ((pos & 3) == 0 && (rand() & 15) < 2) B = 0.9f;
    if ((pos & 0xff) > 0xef) { L = 0x8000; S = 1000; negative = 1; }   // [0x52918],[0x8eba8],[0x8ebb0]
    else negative = 0;
}
```
- The B pulses have **no visible effect while 0x2e534 is registered and [0x57d74]==0** (L1..L4 before the typo
  switch): 0x2e534 runs after it in the same tick and resets B=1.0 before the next palette write.
- The negative segment: for pos 0xf0..0xff of every 256-block the palette is re-set every half-row to the inverted
  palette ((63-pal)*k + pal*(63-k))>>6 with k = (L>>8)-65 decaying 1000/tick. In this section that happens once:
  **pos 0x8f0..0x8ff = music 116.608..117.447** (t_remake 115.36..116.20), during L3. When it ends
  (negative=0), the remaining level ~0x7100 fades out as a short white flash (~13 ticks).
- At every other half-row it sets negative=0, which turns the "negative" start flashes of L2 and L3 into white
  flashes after at most one half-row (~50 ms).

**0x2e534** (registered in the inline bd2 init at 0x2e9f0, removed after L4) — typo-mode flicker and bd2 frame toggle:
```c
if (!typoMode) { B = 1.0f; return; }           // typoMode = [0x57d74]
if (pos != last2) {                             // [0x580a0]
    last2 = pos;
    if ((pos & 3) == 0) B = rand() * 0.000125 + 0.5;          // 0.5 .. 4.6
    if ((pos & 0x1f) == 0 && bd2Frame < 2) bd2Frame ^= 1;     // [0x1499fc]: alternates frame 0/1 every 16 rows
}
B = 1 + (B - 1) * 0.9575;                       // extra decay on top of 0x10124's 0.98
```

**0x2e684** (registered at the end of the init, 0x2eafa; removed at the end of 0x2e8e4) — palette/fade switcher:
```c
if (want != typoMode) {                         // want = [0x1499e8], typoMode = [0x57d74] (data init value 2)
    typoMode = want;
    L = 0x8000; T = 0x4000; S = 1000;           // flash, 16 ticks = 0.23 s (white or negative per [0x8ebb0])
    if (want) { palette = bd2/wire palette (0x1493e8); memcpy(F, wire.fad copy [0x1499ec], 64K); }
    else      { palette = 3d palette (0x1496e8);      memcpy(F, 3d.fad copy [0x1499f0], 64K); }
}
```
Because typoMode starts at 2 (data) and want at 0, this fires on the first tick after registration: it overrides the
init's 300/tick fade, so **the cut into L1 is a 0.23 s white flash** (L=0x8000 -> 0x4000 at 1000/tick), not 0.78 s.
The same trick is used after L8 (typoMode=1, want=0 -> forced flash + 3d palette).

---------------------------------------------------------------------------------------------------
## 2. Tunnel "rings" — 0x16198 (main script 0x31977)

What it draws: a cylinder tunnel (radius 1) textured with tunnel.raw image 0 (wavy horizontal streaks that become
rings around the tube), darkened with distance through tunnel.fad, plus up to 12 sprite copies of the morphing,
tumbling, env-mapped spikey placed every 2 units down the tube axis. Each copy shows the spikey as it was rendered a
different number of frames ago (64-frame history). The lyrics are added on top. In the last part the camera yaws
around by ~167 degrees.

### 2.1 Setup
```c
load tunnel.raw: palette -> 0x5faa8 (live palette), image0 (256x256) -> [0x5b684] (walls),
                 image1 (256x256) -> [0x5b688] = [0x5b674] (spikey env map)
load tunnel.fad -> F;   [0x5d698] = 0x15ffc (the tunnel's own per-tick callback)
camT = (0, 0, 2000)  ([0x52e1c..24]);  morph(1.0, 1.0)   // 0x13e78, sets R = p (unmorphed)
hist[64] = 64 x malloc(128*128)                           // N = [0x52fc8] = 64
histIdx = 0; k = 0; t = 0; ox=oy=0; oz = -1.0f; R = 0;    // [0x9c668],[0x9c478],[0x9c474],[0x9c650/54],[0x9c658],[0x9c47c]
A = 0; vRing = 0.02f; vCam = 0.02f;                       // [0x9c664],[0x9c660],[0x9c65c]
L = 0x8000; T = 0x4000; S = 300;                          // white flash, 0.78 s
0x2a34c(0x17) (lyrics overlay init); [0x1473b8] = 1; frames = 0  // [0x9c670]
[0x9b840] = round(pos at web end 0x740) = 0x740
```
Tick callback 0x15ffc (70 Hz):
```c
R  -= vRing;          oz -= vCam;      t += 4;
if (pos > [0x9b840] + 0x80) {                   // pos >= 0x7c1, music 100.631: phase 2
    if (k == 0) { S = 300; L = 0x8000; }        // white flash 0.78 s at phase start
    vCam -= 0.0001f;  k -= 5;                   // camera decelerates; k drives the camera turn
}
if (pos > [0x9b840] + 0xc0) done = 3;           // pos >= 0x801 -> loop exits after that frame (music 104.035)
if (frames > N + 4 && vRing < 0.05f) { vRing += 0.0001f; vCam += 0.0001f; A += 0.0001f; }  // 300 ticks: 0.02->0.05, A->0.03
```
Note: `frames` counts rendered frames (frame-rate dependent); everything else is per tick.

### 2.2 Per frame
```c
frames++;
// camera orientation, 1024ths of a turn, truncated to int
int pitch = (int)(10 * sin(0.0025 * k));          // k = -5*ticks since 0x7c1: +-10 (3.5 deg), period 7.2 s
int yaw   = (int)(512 * sin(0.001 * k));          // reaches -476 (-167 deg) at the end
int M[9]; rot_1cd4b(pitch, yaw, M);               // rows R0,R1,R2 (16.16): R2 = forward
ox = 15 * A * sin(t / 423.0);                     // t = 4*ticks: period 9.5 s, amplitude <= 0.45
oy = 12 * A * cos(0.0025 * t + 432.0);            // period 9.0 s, amplitude <= 0.36
int uoff = (sintab[(t/3 + 1342) & 1023] >> 2) + 15*t;   // sintab[i] = (int)(65536*sin(2*pi*i/1024))
tunnelGrid_137b8(M, ox, oy, oz, 5000, uoff);       // fills the 41x26 grid at 0x98648
map_1cddc(wallTex /*[0x5b684]*/, A);               // 8x8 bilinear blocks, pixel = F[w>>8][tex[u>>8][v>>8]]
```
Grid 0x137b8 (one ray per grid point i=0..40, j=0..25, all floats):
```c
D(i,j) = (5000/256)*R2 + (i-20)*R0 + (j-12.5)*R1      // in raw 16.16 matrix units: forward 1,280,000, step 65,536
                                                     // => focal length 19.53 grid cells = 156.25 pixels
C = ox*ox + oy*oy - 1;   r2 = Dx*Dx + Dy*Dy (1e-4 if 0);   b = Dx*ox + Dy*oy;
s = (sqrt(|4b^2 - 4*r2*C|) - 2b) / (2*r2);           // far root: hit on the cylinder x^2+y^2 = 1
X = ox + s*Dx;   Z = oz + s*Dz;   (s==0 -> 1e-4)
grid.u (texture ROW, 8.8)    = (int)(7680*Z + 32768)          // row = 30*Z + 128
grid.v (texture COLUMN, 8.8) = (int)(25600*X + 32768 + uoff)  // col = 100*X + 128 + uoff/256
grid.w (fade row, 8.8)       = min((int)(1/(128*s)), 0x2300)  // row = min(35, 39.06/depth), depth along view axis
```
So the wall texture is addressed by the **hit x coordinate, not the angle**: columns 28..228 (+scroll) cover the
tube, the upper and lower halves mirror each other, and each image row (= depth z) becomes a ring. Rows advance
30 per world unit of camera travel (oz decreases 0.02..0.05 per tick = 42..105 rows/s); the column scroll is
15*t/256 = 16.4 columns/s plus a +-64-column sway with period 768 ticks (11 s). Shading: fade row 35 (slightly
brighter than normal) at the screen edges, 32 (normal) at depth 1.22, 16 at depth 2.4, black (row 0/1) beyond ~20-39.

Spikey (spikey.dat, 899 vertices = unit direction + radius p in 93..206, 1794 faces; loaded earlier by 0x13cf8):
```c
float s7 = sin(t/700.0), c7 = cos(t/700.0), s2 = sin(t/243.0), c2 = cos(t/243.0);
Mobj = {c7*c2, s7, -c7*s2,   -s7*c2, c7, s7*s2,   s2, 0, c2};    // = Rz(t/700)*Ry(t/243), v' = Mobj*v
// 0x13e78 morph(m = sin(t/300.0), a2 = sin(0.01*t)):
float Rbase = 50*a2 + 120;                     // 70..170
for each vertex: pos = dir * (Rbase + (p - Rbase) * m);   // m<0 turns spikes into dents
recompute smooth vertex normals (sum of face cross products, normalised)
// 0x1405c transform + project into a 128x128 sprite:
P = Mobj*pos;  N = Mobj*normal;
sx = 64 + (int)(600*P.x/(P.z + 2000));  sy = 64 + (int)(600*P.y/(P.z + 2000));
envU = (int)(120*N.x + 128);  envV = (int)(120*N.y + 128);
bucket = clamp((int)(P.z/2 + 66), 0, 132), face bucket = sum/4 (0..99)
histIdx = (histIdx + 1) % 64;  memset(hist[histIdx], 0, 16384);
0x1438c: buckets 0..99 in increasing order (= nearest first!), back-face culled triangles,
         texel = envTex[envV][envU] (tunnel.raw image 1), affine interpolation, no shading
```
Time bases (t = 4*ticks): m period 6.7 s, Rbase period 2.24 s, rotations 0.4 rad/s (z) and 1.152 rad/s (y).

Ring sprites (0x16198 0x165a2..0x1686f), drawn far to near:
```c
int dir = (M[8] < 0) ? +1 : -1;                // looking toward -z or +z
int i0 = -6*dir, i1 = 6*dir;                   // 12 copies
if (frames < 65) { i0 = 0; i1 = 1; dir = 1; }  // only one copy until the history is filled
int q = (int)(oz - R);                          // C truncation
for (int i = i0; i != i1; i += dir) {
    int ring = q/2 + i;                         // world z of copy = 2*ring + R
    int d = (10*ring) & 127;  if (d >= 64) d = 127 - d;      // N/6 = 10, triangle fold
    int h = (d + histIdx) & 63;                 // d=0 newest; d>0 = the frame rendered (64-d) frames ago
    float dx = -ox, dy = -oy, dz = 2*ring + R - oz;
    float X = (M[0]*dx + M[1]*dy + M[2]*dz)/65536, Y = (M[3]*dx+M[4]*dy+M[5]*dz)/65536, Z = (M[6]*dx+M[7]*dy+M[8]*dz)/65536;
    if (Z <= 0.1) continue;
    float P = 156.25 / Z;
    int size  = (int)(0.8 * P);                 // sprite edge in pixels (128 texels -> size)
    int shade = min(32, (int)(1.2 * size));     // fade row: >= 27 px full brightness
    if (size <= 3) continue;
    blit_1c5e3(hist[h], shade, size, x = (int)(P*X + 160) - size/2, y = (int)(P*Y + 100) - size/2);
    // into A: out = F[shade][texel]; out == 0 -> transparent; x start rounded up to a multiple of 4
}
```
Phase 1 (oz and R move together, oz - R = -1): copies sit at Z = 1, 3, 5, ..., 13 in front of the camera
(sizes 125, 41, 25, 17, 13, 11, 9 px), static in depth while the walls stream past, shifted by the camera wobble
(parallax). Their history ages (frames) are 0, 54, 44, 34, 24, 14, 4 from near to far. Phase 2: vCam drops by
0.0001/tick while vRing stays 0.05, so the copies drift (relative motion -0.0001*n per tick, ~2.9 units in total)
and the camera yaws round; when |yaw| > 90 deg the loop order flips to keep far-to-near.

End of frame: 0x2a34c(B, A) draws the lyrics into B, 0x1c3f4(B -> A): A = F[32 + (B>>3)][A] (brighten toward
white through tunnel.fad), show A. Loop until done==3 (pos >= 0x801) or ESC; then 0x2a34c(0x48).

### 2.3 Times (music s / t_remake)
- start (web loop ends pos >= 0x740): 93.716 / 92.439, plus tunnel.raw loading time. White flash 0.78 s.
- frames > 68: speed-up and wobble start (frame-rate dependent; ~1 s in at 70 fps), complete 4.3 s later.
- phase 2, pos >= 0x7c1: 100.631 / 99.362 (vis 99.325): second white flash 0.78 s, camera turn and slow-down begin.
- yaw passes 90 deg after ~105 ticks (music ~102.1); end yaw -167 deg.
- end pos >= 0x801: 104.035 / 102.771 (vis 102.751). There is no fade at the end: the next part starts on a
  white screen (all palette entries 63 during loading) and fades in over 0.23 s.

### 2.4 Differences from the remake (remake/src/scenes/part2.cpp)
1. part2.cpp:97-110 — the wall mapping. The remake uses angle around the axis + 1/r depth on a squarish tube
   (shapeP 3.2). The original uses a round tube, column = 100*x_hit + 128 + scroll (top/bottom mirrored),
   row = 30*z_hit + 128, so the rings are the image's horizontal streaks; depth darkening via tunnel.fad row
   min(35, 39.06/depth) (slightly brighter than normal at the edges, black down the middle). Wall speed 0.6 -> 1.5
   rows per tick, not `-local*0.3`.
2. part2.cpp:112-125 — the remake has one spikey moving 3 -> 7 -> 3.5. The original has up to 12 sprite copies at
   2-unit spacing on the axis (7 visible, Z = 1..13), each a 128x128 render taken from a 64-frame history. The
   spikey morphs (R = Rbase + (p-Rbase)*sin(t/300), Rbase = 120+50 sin(0.01t)), tumbles Rz(t/700)*Ry(t/243),
   env map = **tunnel.raw image 1** (remake uses "phong"), no specular, copies dim with distance (fade row
   min(32, 1.2*size)) and have black as transparent.
3. part2.cpp:96/118 — camera motion: original has a small xy wobble (<=0.45/0.36, periods 9.5/9 s) and from music
   100.63 a yaw turn to -167 deg plus +-3.5 deg pitch (int 1024ths); the remake only moves the vanishing point.
4. part2.cpp:126-127 — flashes: original white 0.78 s at start and again at music 100.63; no flash-out before the
   cut (the flash belongs to the next part, 0.23 s).
5. part2.cpp:104,107,108 — tint/fog/bright are invented; the original palette is tunnel.raw's, unmodulated.

---------------------------------------------------------------------------------------------------
## 3. Init of 0x2e8e4 (0x2e8e4-0x2eb25)

```c
[0x1499ec],[0x1499f0],[0x1499f4] = get64K();
B = 1.0; S = 0; L = 0x8000; palette = all 63 (white); memset(screen, 0); T = 0x4000;
wire.raw: palette -> 0x1493e8, image -> [0x1499f4]
[0x1499f8] = get64K(); bd2 = alloc(192000); bd2.raw: palette -> 0x1493e8 (overwrites wire's), 3 frames -> [0x1493b0]
register 0x2e534
wirecut.fad -> F, copy -> [0x1499f8];  wire.fad -> F, copy -> [0x1499ec]
0x2b110(0x17); 0x2bdb4(0x17); 0x26f04() (tex3d.raw: palette -> live palette 0x5faa8, 6 images -> [0xc1d1c..30]);
0x268bc(); 0x27670(0x17); 0x28100(0x17); 0x29ae8(0x17); 0x23540(0x17)
3d.fad -> F; 0x10824("3d.gst"); copy live palette (tex3d's) -> 0x1496e8; copy F -> [0x1499f0]
[0x1473b8] = 1; register 0x2e684; S = 300 (overridden to 1000 by 0x2e684 on the next tick); [0xc1d68] = 0
[0x9b840] = round(pos) = 0x800
```
Palette in L1..L4 (before the typo switch) = tex3d.raw palette; F = 3d.fad.

---------------------------------------------------------------------------------------------------
## 4. Flowers — 0x29ae8 (NOT a Julia set)

What it draws: tex3d.raw **image 5** (vertical colour stripes) warped by the sum of three moving 1/z̄ poles
("dipoles"). Each pole turns the stripes into families of circles tangent at the pole (left/right lobes from the
column field, up/down lobes from the row field) — the "flower" petals — and the whole pattern sweeps because the
stripes scroll one texel per tick.

```c
struct Pole { float a, b, da, db, str; } P[3];      // 32-byte records at 0x147334, 0x147354, 0x147374
// 0x29ae8(0x17):
for k in 0..2: P[k].a = rand(); P[k].b = rand();     // radians, i.e. random phases
               P[k].da = (rand() - 16384) / 900000.0; P[k].db = (rand() - 16384) / 900000.0;  // +-0.0182 rad/tick
               P[k].str = 25600.0f;
register tick 0x29aac:  for k: P[k].a += P[k].da; P[k].b += P[k].db;   T++;   // T = [0x147394], starts 0

// 0x29ae8(buf): grid of 41 x 26 points at screen (x = 8i, y = 8j)
for j in 0..25, i in 0..40:
    float x = 8*i, y = 8*j;
    float U = 256.0f * T, V = 0;
    for k in 0..2:
        float cx = 160 + 120*sin(P[k].a), cy = 100 + 80*sin(P[k].b);
        float dx = cx - x, dy = cy - y;
        float f = 100*P[k].str / (1 + dx*dx + dy*dy);      // = 2,560,000/(1+r^2)
        U += dx*f;  V += dy*f;
    grid[j][i].row = (int)V;  grid[j][i].col = (int)U;     // 8.8 fixed; grid.w untouched
map_1d0dd(tex3d[5], buf);    // 8x8 blocks, bilinear in (row,col), pixel = tex[(row>>8)&255][(col>>8)&255], no shading
```
In texels: column = T + sum 10000*(cx-x)/(1+r^2), row = sum 10000*(cy-y)/(1+r^2) (both mod 256). The displacement is
~100 texels at r = 100 px and grows as 1/r toward each pole, so the whole frame is strongly warped; the pattern near
a pole is the inversion image of straight stripes (circles through the pole), which is why it shows broad circular
sweeps rather than fine fractal detail.
- Colouring: direct texel copy, palette = tex3d.raw palette, no fade table. Image 5's colour varies along columns
  (stripes); its brightness varies along rows (dark at the top), which the row field turns into the up/down lobes.
- Time base: everything per tick (70 Hz) from the init call (music ~104.0 + load time): T += 1 (stripes scroll
  70 texels/s), pole phases move at |da|,|db| <= 1.27 rad/s; pole centres sweep x in 40..280, y in 20..180.
  Rendered once per frame into B in L1-L5 (freed after L9).

Differences from the remake (part2.cpp:133-144 and fx.cpp:182-192):
1. Algorithm: the remake iterates z = z^2 + k (24 iterations, external angle/potential). The original is the
   3-pole field above, evaluated on an 8x8 grid with bilinear interpolation (which also softens it).
2. Texture: remake uses "wipeout" (part2.cpp:143); original uses tex3d image 5 (`c.tex("tex3d#5")` style name).
3. Animation: remake drifts centre/zoom/angle slowly; original moves three independent poles with random constant
   angular speeds and scrolls the texture columns at 1 texel/tick. The rand() values depend on the C library seed
   and on how many rand() calls happened before, so the exact speeds are not fixed; any |da|,|db| <= 0.0182 rad/tick is
   faithful.
4. The remake's dark shade for points in the set (fx.cpp:192) has no counterpart.

---------------------------------------------------------------------------------------------------
## 5. L1 and L2 (flowers + Aap overlays)

L1 (music 104.04-107.33, t_remake 102.77-106.07): per frame A cleared to 0, 0x2bdb4(A) draws (line routine 0x2acf0
is among its callees — the "web lines"), flowers into B, then B = F[32 + min(31, 2*(A & 127))][B]: any A pixel >= 16
pushes the flowers fully to white, smaller values brighten partially. Shown B. Start: 0.23 s white flash.

L2 (music 107.33-110.31, t_remake 106.07-109.06): 0x2bdb4 freed; palette re-set white, screen cleared,
L=0x8000, S=200 (1.17 s), negative=1 (becomes white after <= 1 half-row, see 1.1). Per frame 0x2b110(A) (A not
cleared by the loop), flowers into B, B = F[31 - min(31, 2*(A & 127))][B]: A acts as a black mask (A >= 16 = black,
A = 0 leaves the flowers at row 31 = ~97 % brightness). Shown B.

(0x2bdb4 / 0x2b110 internals: other agent.)

Differences from the remake (part2.cpp:146-189): the remake's "light burst" is the phong1 image added at 103.6-105
(part2.cpp:159-164) — phong1.raw is a shading map in the original, not an image; what the original shows there is
the 0x2bdb4 drawing used as white light on the flowers until 107.33, then the 0x2b110 drawing as a black mask until
110.31. Remake flashes at 103.0-103.4, 105.6-106.6, 108.8-109.8 (part2.cpp:184-186) vs original 0.23 s white at
104.04, 1.17 s at 107.33, 2.34 s at 110.31 (all linear in palette space, music s).

---------------------------------------------------------------------------------------------------
## 6. Face — 0x23540 (L3, also drawn in L4 and L5)

What it draws: faceo.dat is a flat plate (101 vertices at z~0, x +-159, y +-99 — the whole screen) with a face relief
in the middle (613 vertices, x +-89, y +-93, z -1.1 .. -131.5, toward the camera). The plate is textured with the
current flowers frame by planar projection, so the screen shows the flowers (1.034x zoom); the relief rises out of
the plate and sinks back, tilts slightly, and acts like a magnifying lens on the flowers, with phong1.raw highlights.

### 6.1 Init 0x2338c (called with 0x17 in the 0x2e8e4 init)
```c
winL = get64K() [0xa12fc]; winR = get64K() [0xa1300]; phong = get64K() [0xa1304] <- phong1.raw (256x256, values 0..63)
faceo.dat: nv=714, nf=1422; vertex records 56 bytes (x,y,z floats), faces 32 bytes (3 vertex ptrs)
face.tex = (v0.x < 0) ? winL : winR;          // by the first vertex only
cam = identity (0x14760(0,0,0xa0120)); camPos = (0, 0, -290) ([0xa018c..94]); [0xa12f8] = 1000
```
0x23540(0x2b) registers tick 0x23384: `k += 2` (k = [0x57d78], data init 200; registered at L3 start 0x2ee4d).
0x23540(0x48) unregisters and releases the three buffers (after L9).

### 6.2 Per frame 0x23540(dst)
```c
split_1c3c4(B, winL, winR);       // winL = B columns 0..255, winR = B columns 64..319 (256x200 each)
[0x5b670] = phong;
Mrel = rot_14760(0.2*cos(k/144.0 + 54), 0.3*sin(k/86.0));   // = Rz(A)*Ry(B): roll +-11.5 deg (period 6.46 s),
                                                             //   yaw +-17.2 deg (period 3.86 s)
D = max(0, 150*sin(0.005*k + 1) + 50);                       // relief push-back, 0..200, period 8.98 s
if (project_22be0()) { shade_22e14(); draw_23170(dst); }
```
0x22be0 (per vertex):
```c
if (v.z >= -1) P = (v.x, v.y, 0);                       // plate: not rotated
else { P = Mrel * v;  P.z = min(P.z + D, 0); }          // relief: rotated, pushed back, clamped to the plate
v.tu = (int)P.x;  v.tv = (int)P.y;                      // planar texture coords (screen-pixel units)
d = P - camPos;  Xc = cam.R0.d; Yc = cam.R1.d; depth = cam.R2.d;
if (depth <= 40) skip projection (vertex keeps stale sx,sy);
sx = (int)(160 + 300*Xc/depth); if (sx > 640 || sx < -320) return 0;   // whole object not drawn this frame
sy = (int)(100 + 300*Yc/depth);
```
0x22e14: light L = normalise(0.1, -0.2, 1.0); vertex normals = normalised sum of face normals (camera space);
faces with all depths > 40 go into bucket (int)((d0+d1+d2-120)/12) clamped 0..99; per-vertex value
`(int)(30*(n.L > 0 ? (n.L)^4 : n.L) + 32)` is stored at +0x28 but **not used** by the textured draw.

0x23170: buckets 99 -> 0 (far to near). Per face, vertex record for the rasteriser 0x1e223:
```c
sx, sy;
eu = (int)(120*n.x + 128), ev = (int)(120*n.y + 128);          // into phong1.raw
tu = v.tu + (face.tex == winR ? 96 : 160), tv = v.tv + 100;    // into the 256x200 window
if (!typoMode) 0x1e223: back-face culled (cross < 0 drawn), affine, per pixel
        dst = F[ phong1[ev][eu] ][ win[tv][tu] ]               // F = 3d.fad
else 0x23118: wireframe edges with line routine 0x2acf0 (typo part)
```
phong1.raw: 31 at the centre (128,128) = plate brightness ~97 %, peak 63 (white) around (165,165), i.e. for normals
tilted toward +x,+y by ~0.3; 0 toward the edges. So the plate looks like the flowers, the relief gets a white
highlight on its lower-right-facing slopes and dark edges.

Because the texture coordinate is the camera-space (x, y) of the rotated point while the screen position is
perspective-projected (300/(290+z)), relief points are magnified up to 300/159 = 1.89x relative to the flowers behind:
the face shows a lens-like enlarged copy of the flowers that slides as it turns.

### 6.3 L3 sequence (music, t = ticks since 110.311; k = 200 + 2t)
- 110.311: L=0x8000, S=100 (2.34 s fade), negative=1 for <= 1 half-row then white. Plate covers the screen; D = 186
  -> relief completely flat (invisible except the 1.034x zoom of the plate).
- 111.12: relief starts to emerge (D < 131.5); 112.43: fully out (D = 0) until 115.94; flat again at 117.24;
  starts again at 120.10; L3 ends at 120.803.
- 116.608-117.447: negative-palette flicker from 0x2e798 (section 1.1).
- k is per tick; geometry is recomputed per frame.

### 6.4 Differences from the remake (part2.cpp:146-189)
1. part2.cpp:165-183: the face does not fade in with alpha and the camera does not dolly. The face is the relief of
   a full-screen plate textured with the flowers; it emerges by the push-back D cycle (times above), and also sinks
   back into the plate at 115.9-117.2 and starts again at 120.1.
2. Motion: roll 0.2*cos(k/144+54) rad and yaw 0.3*sin(k/86) rad (k += 2 per tick), not the remake's
   translate/rotY/rotX (part2.cpp:174-175). No translation at all.
3. Shading: texel = flowers by planar projection of the rotated relief (lens effect), brightness from phong1.raw
   indexed by (120*n.x+128, 120*n.y+128) through 3d.fad; no specular term, no env map of the flowers
   (part2.cpp:172-179 mode 4 is close in spirit but the "pic" must use the rotated x,y and the chrome part must be
   the phong1 brightness of the same picture, not a reflection).
4. The plate itself is the background in L3 (flowers zoomed 1.034x about the centre, ~97 % brightness), so the
   separate background draw (part2.cpp:154-157) should match that.
5. Timing: L1/L2/L3 boundaries at 104.04 / 107.33 / 110.31 / 120.80 music; remake uses one scene 103.0-120.0 (t).

---------------------------------------------------------------------------------------------------
## 7. FaceBox — L4 before the typo switch (music 120.803 - 125.942, t_remake 119.56 - 124.71)

There is **no cube**. The "box" is the same faceo plate (318x198 with the relief, flowers texture, phong1 shading),
now seen from a moving camera between a hex floor and ceiling.

Per frame:
```c
// 0x278a4(230.0): camera goal, s = [0xc1d6c] += 4 per tick since the 0x2e8e4 init (tick 0x2764c)
float Rr = 230 * (1 + sin(0.0005*s + 64)/6);
float p = 4*s/700 + 34, q = 4*s/1100;           // 1.6 rad/s and 1.02 rad/s
goal = (2*Rr*sin(p)*cos(q), Rr*sin(p)*sin(q), Rr*cos(p) - 700);   // [0xc1d4c..54]
// 0x27444(3): for each pending tick ([0xc1d64], incremented by 0x2764c):
dir = target - camPos;   target = [0xc1d40..48] (left over from the web part: (0, 0, webCamZ + 1000))
w = turnRate/|dir| * (dir x camForward);  rotate cam matrix by small angles w (0x2721c, re-orthonormalised 0x18b18)
for x,y,z: vel = damp*vel + 0.002*(goal - camPos); camPos += vel;
// L4 values set at 0x2ef81: damp = [0x57d7c] = 0.6, turnRate = [0x57d80] = 0.01 (were 0.79 / 0.05)
// 0x27670(A): floor/ceiling 0x27014(37.5, 290.0) + map_1cddc(tex3d image 1 = purple hex)
ray = 37.5*R2 - 20.5*R0 - 13*R1 + i*R0 + j*R1  (41x26 grid, focal 300 px);
t = (290 - cam.y)/ray.y; if (t < 0) t = (-290 - cam.y)/ray.y;      // planes y = +290 and y = -290
hit = cam + t*ray;  row = (int)(128*hit.z), col = (int)(128*hit.x)  (8.8: one texel per 2 units, tile 512 units)
light = clamp((int)(307200/t), 0x100, 0x2000)  -> fade row clamp(45000/depth, 1, 32)  (full brightness to depth 1400)
0x29ae8(B); 0x23540(A)   // plate drawn over the world with the world camera
```
- Start state: camPos = (0,0,-290), camera = identity, so the first frames look exactly like the end of L3; the
  spring (damp 0.6, k 0.002: overdamped, time constant ~200 ticks = 2.9 s) pulls the camera back toward z ~ -700
  (+-230 orbit, mostly filtered out), so the plate recedes and the hex floor/ceiling (y = +-290, plate y +-99)
  appear around it. Rotation toward the target with time constant ~1.4 s.
- The face relief keeps its D cycle: fully out from music ~121.4 to ~124.9.
- 0x23540 skips the whole face/plate for a frame if any vertex projects outside x in [-320, 640].
- Typo switch at music 125.942 (pos 0x9a1): palette bd2, F = wire.fad, white flash 0.23 s, the world uses
  wire.raw as its texture ([0x1499f4]), the face is drawn as wireframe, and the bd2 frame is composited (other agent).

Differences from the remake (part2.cpp:192-237):
1. part2.cpp:226-233: no 6-sided box; a single plate (the full face picture with relief) seen in perspective.
2. part2.cpp:215-223: world = two hex planes at y = +-290 (ceiling and floor symmetric), texture tex3d image 1 with
   a 512-unit tile, distance shading via 3d.fad row 45000/depth (1..32); the remake's floorY -1 / ceilY 1.6 / fog 7
   and metal shading are not in the original.
3. Camera: original starts at the L3 view and drifts back on a damped spring toward the orbit goal above while
   turning toward a fixed point; remake flies forward (`eye.z = 4 - 0.6*local`) and moves the box.
4. The plate keeps animating (flowers, relief cycle); remake's picture uses `t - 103` flowers and a fixed face.
5. Flash: remake fades to white at 124.5-124.9 (part2.cpp:234); original has a 0.23 s white flash *after* the
   switch at 125.94 (music).

---------------------------------------------------------------------------------------------------
## 8. 0x14760 (identified)
`void rot_14760(float A, float B, float *M)` (args on the stack, ret 0xc): M = Rz(A)*Ry(B) as 3x3 floats,
rows (cA*cB, sA, -cA*sB), (-sA*cB, cA, sA*sB), (sB, 0, cB); used as v' = M*v (x' = M0 x + M1 y + M2 z ...).
The tunnel builds the same matrix inline for the spikey (A = t/700, B = t/243). 0x14760(0,0,0xa0120) resets the
world camera to identity.

## 9. Buffer helpers used here (as read)
- 0x1c490(src, dst): dst = F[32 + min(31, 2*(src & 127))][dst]   (brighten toward white)
- 0x1c503(src, dst): dst = F[31 - min(31, 2*(src & 127))][dst]   (darken to black)
- 0x1c3f4(src, dst): dst = F[32 + (src >> 3)][dst]                (brighten, 32 levels)
- 0x1c3c4(src, dL, dR): dL = src columns 0..255, dR = src columns 64..319 (256x200 each)
- 0x1c5e3(src128, shade<<16|size, x, y): scaled 128x128 sprite into A (hard-coded 0x5fda8), out = F[shade][texel],
  0 = transparent, x rounded up to a multiple of 4
- 0x1cddc / 0x1d0dd: grid mappers (8x8 bilinear) with / without the fade row; texel = tex[entry0>>8][entry1>>8]
- 0x10bd8 / 0x10c0c: add / remove tick callback (16 slots, only slots 0..7 are called); 0x10c0c(0) clears all
- 0x10ab4 / 0x10bac: get / release a 64 KB buffer from a pool

## 10. Not determined
- rand() state at 0x29ae8(0x17): exact pole speeds/phases (deterministic only if the call count before is known).
- The look-at target during L4 ([0xc1d40..48]) is left over from the web part (z = web camera z + 1000; the web
  camera z is 0.01*[0x148f84] at that time); the leftover camera velocity from the web part is also unknown (small).
- 0x2721c / 0x18b18 internals (camera small-angle rotation and re-orthonormalisation) were not decompiled.
- The tunnel's spikey is drawn nearest-bucket-first (looks like a bug); confirm on the capture whether back spikes
  show through.
- Frame rate of the reference machine: the tunnel's history ring (copies' age in frames) and the "frames > 68" start
  of the speed-up are per rendered frame.
- End times of L9-L11, L13, L15 are estimates (counters whose start values depend on other routines).
- 0x2bdb4 / 0x2b110 / 0x2a34c internals (other agents).
