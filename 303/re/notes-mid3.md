# 303.EXE mid-section, part 3: spiral, vic, star, plasma (0x2f6c9 .. 0x30060)

Scope: the sub-parts of 0x2e8e4 from the first "fish.fad" part up to the paranoid part, plus 0x15734 (also used for
the earlier "statix / code" part). Scratch tools written for this: `scratchpad/re/sym87.py` (symbolic x87 tracer for
straight-line code), `spinsim.py`, `extract.py` (PAK extraction to `scratchpad/re/pak/`), images in `scratchpad/re/img/`.

Conventions: "tick" = timer callback, 70.086 Hz. "frame" = one pass of the render loop (no vsync, speed depends on CPU).
pos = [0x9b844] (half-rows). Music seconds come from pos.py (rows.txt). "capture" = time seen in the reference mp4
(960x720, 30 fps); where both are given, the capture time was measured (flash/luminance jumps), not predicted.
Camera space in all these routines: x right, y DOWN, z forward; projection sx = 160 + f*X/Z, sy = 100 + f*Y/Z with the
same f for x and y (pixels are 1.2x taller on the 4:3 screen, so everything appears 1.2x stretched vertically).

---------------------------------------------------------------------------------------------------------------------
## 0. Sequence and timing (main routine 0x2f6c9 .. 0x2ffcb)

| sub-part | code | ends when | end pos -> music s | capture (video) |
|---|---|---|---|---|
| spiral (A) | 0x2f6c9 | pos >= S+0x80, S=(pos_in+0x20)&~0x3f = 0xb80 | 0xc00 -> 157.72 | starts 149.93 (white flash), ends 156.63 |
| vic (B) | 0x15734(1) | callback sets n=3 when pos > S+0x80 (S=0xc00) | 0xc81 -> 164.54 | flash 156.63, "vic" 158.33, "music" 160.00, white strobe 162.93-163.40 |
| star (C) | 0x2f8d4 | pos >= S+0x80, S=0xc80 | 0xd00 -> 171.14 | flash 163.43 .. 169.93 |
| plasma D | 0x2fb0c | [0xa1558] <= 0.001 (tick driven) | ~734 ticks = 10.47 s after D start | 169.93 .. ~180.5 (predicted) |
| plasma E | 0x2fce7 | [0x13efa4] >= 1.0 (tick driven) | 225 ticks = 3.21 s | ~180.5 .. ~183.7 |
| plasma F | 0x2fe73 | byte [0x1493dc] set by callback (camera-z driven) | ~550 ticks (see F) | flash 189.00 (cam.z>1500), fade back from white 191.77 |

Music-second equivalents of the capture times with video = 1.00643*music - 1.953: 149.93->150.91, 156.63->157.58,
158.33->159.26, 160.00->160.92, 162.93->163.83, 163.43->164.33, 169.93->170.79, 180.5->181.3, 183.7->184.5,
189.00->189.74, 191.6-191.8->192.3-192.5.
**Observation for the coordinator:** in this section the measured events are increasingly earlier than
pos.py + that formula predict: -0.09 s at the spiral start (0xb80), -0.15 at 0xc00, -0.25 at the vic words
(0xc21/0xc41), -0.28 at the strobe, -0.36 at 0xd00. In the alex part (0x561/0x581, see section 4) the same formula
matches exactly (66.77 / 68.50 measured = predicted). So rows.txt is probably ~1.3% too slow between ~150 and 171 s
(or the module tempo changes there). Tick-driven ends (D, E, F) should be placed relative to the measured D start.

Per-sub-part palette fades (timer 0x10124: level [0x52918] moves to [0x5291c]=0x4000 by [0x8eba8] per tick):
A: 0x8000 -> 0x4000 step 1000 (white, 16 ticks = 0.23 s). B: step 300 (0.78 s). C: step 500 (0.47 s).
D: main sets step 100, but the mode callback 0x2e684 (re-enabled, see D) immediately re-flashes with step 1000 (0.23 s).
The callback list is saved to a local before A (0x2f67c, after removing 0x2e438) and restored at D (0x2fb18), so
0x2e534/0x2e684 from the mid-section init run again in D-F.

---------------------------------------------------------------------------------------------------------------------
## 1. Helpers

### 0x14760(float a, float b, float out[9])  — rotation matrix (stack args, ret 0xc)
```
out = { ca*cb,  sa, -ca*sb,
       -sa*cb,  ca,  sa*sb,
        sb,     0,   cb    }        // = Rz(a)*Ry(b), row-major, used as p' = out * p (column vector)
```
(ca=cos a ...). 0x32b72 = sin, 0x32b68 = cos. 0x15734 inlines the same matrix (0x15e4b).

### 0x2721c(out[9], stack x,y,z)  — axis-angle (Rodrigues) matrix
L = |(x,y,z)|; if L < 1e-8: identity. Else axis = v/L, angle th = asin(L) (0x34cf8 = asin via fpatan),
out = standard right-handed rotation (column-vector convention):
`[X²+c(1-X²), XY(1-c)-sZ, XZ(1-c)+sY; XY(1-c)+sZ, Y²+c(1-Y²), YZ(1-c)-sX; XZ(1-c)-sY, YZ(1-c)+sX, Z²+c(1-Z²)]`.
### 0x18b18(out, A, B): out = A*B (3x3 row-major).   0x34d1f = memmove(dst=eax, src=edx, n=ebx).

### 0x2acf0(dst, x0, y0, x1, y1) — glow line
Clips to x in [3,317], y in [3,197]; 16.16 DDA; per step adds a "plus" stamp (byte adds on dwords, may carry):
centre +2, left/right/up/down +1 (0x1bfb7 x-major positive slope, 0x1bf7e y-major). The x-major negative-slope
variant adds a 2-row-tall plus (rows 0 and +1 get 1,2,1, row -1 and +2 get +1) — a quirk.
A drawn horizontal line therefore has core value ~4, sides 1.

### 0x23118(verts, dst) — hidden-line wire triangle
`if (0x1f88b(verts, dst)) { line(v0,v1); line(v2,v1); line(v0,v2); }` with 0x2acf0.
0x1f88b: trivially rejects triangles entirely above/below/left of the screen, back-face culls
(`(x1-x0)*(y2-y1) - (y1-y0)*(x2-x1) >= 0` -> rejected), then FILLS the triangle with 0 (rep stosw of 0) and returns 1.
Drawn back-to-front this is hidden-line removal: near faces erase the lines behind them, then draw their edges.
(The x "off right" test compares the int x with 0x1400000, so it never fires — harmless.)

### 0x1ee85(verts, dst) — affine textured triangle
Same reject + back-face test, then affine mapping of [0x5b674] (256x256) with u = word +0xc, v = word +0xe of each
16-byte vertex {int x, int y, word ?, word ?, word u, word v}. Writes texels directly (no shading).

### 0x1c576(src, dst) / 0x1c490(src, dst) — additive combine through the fade table
`dst[i] = fad[(0x20 + min(s, 31))*256 + dst[i]]` with fad = [0x5b67c]; 0x1c576 uses s = src[i],
0x1c490 uses s = 2*(src[i] & 0x7f). fish.fad rows 32..63 add ~1.2/63 luminance per level in the sphere.raw palette;
on black the ramp is a blue-white: v=1 (2,4,4), 2 (6,4,6), 4 (8,8,12), 8 (12,16,20), 16 (24,30,39), 24 (45,47,57),
31 (63,63,63) (6-bit RGB). Row 32 is ~identity (mean RGB error 0.9). Rows 0..31 darken towards black.
3d.fad (tex3d palette) has the same structure (row 32 identity, ±~1/63 luminance per row).
3d.gst ([0x5b678]) is a saturating ADDITIVE colour blend G[new][old] (G[0][x]=x; best fit min(63, a+b) per channel,
mean error 6.5/189 vs 64 for averaging).

---------------------------------------------------------------------------------------------------------------------
## 2. Spiral (A) and star (C): 0x2b908 (icosa.dat + sphere.raw) + 0x2d81c (squares)

Main loop for both (0x2f768 / 0x2f97c), per frame:
```
memset(0x5fda8, 0, 64000);
icosa(0x5fda8);                          // 0x2b908
squares(0x6f7a8);                        // 0x2d81c: clears 0x6f7a8, draws 256 squares
combine_x2(0x6f7a8 -> 0x5fda8);          // 0x1c490 via fish.fad
show(0x5fda8);
```
Before A: [0x57d80] *= 2.0 (camera turn gain); after C: *= 0.5. fish.fad is loaded into [0x5b67c] at both inits.
The only difference between A and C is byte [0x148538] (toggled by every 0x2b908(0x48)): 0 in A (wireframe), 1 in C
(env-mapped). C also sets [0x1473b8]=1 (not used by these routines).

### 2.1 icosa.dat (37244 bytes)
`int nobj=16; int nv=932; int nf=1860; nv x {float x,y,z; int group}; nf x {int a,b,c}`.
Group 0 = 12 icosahedron vertices (r = 126.3); groups 1..15 = 60 vertices each (20 tentacles x 3), radii 201, 283,
368, ..., 1413 (step ~87); group 16 = the 20 tips (r = 1500.4). Coordinates are scaled by 1/120 at load
(core radius 1.05, tips 12.5). The face winding makes N = (v0-v1)x(v2-v1) point INWARD for every face.
(Load bug: the vertex array is allocated nf*56 and the face array nv*28 bytes, i.e. the face array overflows by
26 KB; irrelevant for the remake.)

### 2.2 0x2b908 init (0x17)
work buffer [0x148530] = alloc 64000; texture [0x13f004] = sphere.raw (palette -> 0x5faa8, 256x256 pixels);
reads icosa.dat into 56-byte vertex records {x,y,z, X,Y,Z (camera), int sx, int sy, ..., int group @+0x28,
nx,ny,nz @+0x2c} and 28-byte faces {v0*,v1*,v2*, ..., next* @+0x18}; M[g] = identity for g = 0..16
(0x147f20 + 36*g, via 0x14760(0,0)); registers tick callback 0x2b8e4:
`t[0x14853c] += 10; [0x148534] += 50 (unused)`. t is never reset: it starts at 0 at A's init and keeps counting
(only while the callback is registered, i.e. during A and C).

### 2.3 0x2b908 render (per FRAME)
```
memmove(&M[1], &M[0], 17*36);                 // M[g] = last frame's M[g-1]  -> delay line of orientations
memset(work, 0, 64000);
v = 0.12 * ( sin(t/700), sin(t*0.00107411386 /* =1/931 */), sin(t*0.002) );   // t = [0x14853c], +10 per tick
R = axisangle(v);                              // 0x2721c: angle asin(|v|) about v/|v|, |v| <= 0.208
M[0] = M[1] * R;                               // 0x18b18 (object-space increment)
transform();  normals_and_sort();  draw(dst);  // 0x2b394, 0x2b570, 0x2b780
if (mode == 0) combine(work -> dst);           // 0x1c576 (also called in mode 1; work is empty -> fish row 32)
```
In seconds (tau = ticks/70): v = 0.12*(sin(1.0*tau), sin(0.752*tau), sin(1.4*tau)).
**The tentacle twist is exactly this:** every vertex of group g is transformed with M[g], the object orientation from
g frames ago. Shell 0 (core) uses the newest orientation, the tips the one from 16 frames ago, so each shell lags
the next by one frame's rotation asin(|v|) (up to 11.9°) about the axis v. Total curl = 16 frames of rotation.
The curl shape is frame-rate independent; the spin speed (rad/s) is proportional to the frame rate. A crude
rotation measurement on the capture (~8°/capture frame at 151-152.7 s, ~4° at 153.2-154.2, ~0 at 154.6) matches a
simulation of this formula at about 30-35 rendered frames/s (spinsim.py); 70 fps would spin ~2x faster.
(Guess: the remake should step M at a fixed ~33 Hz, or 35 Hz = every 2nd tick.)

0x2b394 (transform):
```
P = C * campos                // C = camera matrix 0xa0120 (rows = camera axes), campos = 0xa018c (set by 0x2d81c)
for each vertex: Mg = M[group];
   Z = dot(Mg.row2, p) - P.z; if (Z <= 20) { sx = sy = 0; continue; }       // +0x14
   X = dot(Mg.row0, p) - P.x;  Y = dot(Mg.row1, p) - P.y;   k = 300/Z;
   sx = trunc(160 + X*k); if (sx > 640 || sx < -320) return 0;            // early out, result unused
   sy = trunc(100 + Y*k);
   n_acc = 0;
```
So the icosa is placed at the world origin (-P = origin in camera space) but its ORIENTATION is M[g] directly in
camera space: the camera orbit only shifts it when the lazy camera is not yet looking at the origin.
At distance 30: core radius ~10 px, tentacles up to ~125 px (320x200). Faces with any vertex Z <= 20 are skipped,
i.e. the parts of tentacles that come within 10 units towards the camera disappear.

0x2b570 (normals + depth sort): for each face: N = (v0-v1) x (v2-v1) in camera space (inward), added to the
three vertices; if all three Z > 20: bucket = clamp(trunc((Z0+Z1+Z2-60)/90), 0, 99), push on list 0x1483a0[bucket].
Then every vertex normal n = acc / max(|acc|, 0.001).

0x2b780 (draw): buckets 99 -> 0 (far to near). Vertex {sx, sy, u = trunc(nx*120+128), v = trunc(ny*120+128)}
(n = inward camera-space normal, i.e. u = 128 - 120*nout.x, v = 128 - 120*nout.y with y down).
mode 1 ([0x148538]=1, star): 0x1ee85 textured with sphere.raw into dst. mode 0 (spiral): 0x23118 hidden-line
wire into the WORK buffer. Lists are cleared as they are drawn.
Spiral result: dst = fish[32 + min(work,31)][0] = the blue-white ramp above; shared edges are drawn twice, so a
typical edge core is ~8 (12,16,20)/63 and only dense overlaps saturate to white.

### 2.4 0x2d81c — 256 squares and the orbiting camera
Init (0x17): camera matrix C = identity (0x14760(0,0,0xa0120)); 256 particles p = (rand()%41-20, ..., ...)
(integers in [-20,20]^3, 0x146384 + 12*i); [0x1493a0]=0; callback 0x2d680: `[0x1493a0]++ ; T[0x58094] += 6`.
T starts at 2354 (data) and is not reset; it advances only during A and C.
Render (dst): 
```
A = 5*sin(T/1500 + 435);  B = 5*sin(T*0.000613496933 /* 1/1630 */ + 1435);
campos = 30*( cos A*sin B, cos A*cos B, sin A );          // sphere of radius 30 around the origin
lazy_lookat();                                            // 0x2d6a4, runs once per elapsed tick
memset(dst,0,64000);
for each particle p: d = p - campos;  Z = d.C2; if (Z <= 5) skip;
    corners in camera space: A=(X,Y,Z), B=A+(C0,C3,C6), C=B+(C1,C4,C7), D=A+(C1,C4,C7)
    // = the world points p, p+(1,0,0), p+(1,1,0), p+(0,1,0): UNIT SQUARES IN WORLD XY PLANES, not billboards
    project each with 160+300X/Z, 100+300Y/Z (only corner A is depth-tested);
    glow lines A-B, A-D, C-B, C-D (0x2acf0)
```
0x2d6a4 (per tick, [0x1493a0] times): d = -campos; if |d| > 0.01: d' = d*[0x57d80]/|d|;
w = d' x F (F = C row 2); R = axisangle(w) (angle asin(k*sin(angle between F and d))); C = C*R; if roll [0xc1d68] != 0:
C = C*axisangle(0,0,roll) (roll is 0 here, reset by the mid-section init). Net: the camera turns towards the origin
with a first-order lag of gain k = [0x57d80] per tick (0.05*2 = 0.1 if the earlier parts left it at 0.05 — not
verified). Because C is reset to identity at each init, at the start of A and C the camera looks along +z from
wherever it is on the sphere and swings to the origin over a fraction of a second: the icosa enters from off-centre
(capture 164: only squares and the object at the bottom edge).
Squares are combined with 0x1c490 (value x2) through fish.fad onto the icosa.

### 2.5 Star = same object env-mapped
Mode 1: faces drawn back-to-front (painter, buckets as above), back-face culled, affine-textured with sphere.raw
(blue chrome ball, palette = sphere.raw palette). Note M[g] were reset to identity by the re-init, so the arms are
straight in the first frame and curl within 16 frames; t keeps counting from A.

### 2.6 Differences vs remake (part3.cpp)
Spiral (214-239):
1. Geometry: remake twists by fixed angles per shell (twistZ 0.32, twistY 0.02, fx.cpp:22-28) plus a sine wobble (:228-231) and
   flattens the mesh (scale 1.6,1.6,0.5, :224). Original: no flattening/wobble; shell g uses the orientation of g
   frames ago; orientation integrates R(v(t)) per frame (2.3). This is the main reason the spiral looks wrong.
2. Shading: remake mode 2 fresnel glow on solid faces (:225). Original: hidden-line wireframe (front faces only,
   painter order, interiors erased) with additive soft 3-px lines through fish.fad -> dim blue-grey lines that
   brighten to white where edges pile up; no surfaces.
3. Spin: remake rotZ(0.6 t) + rotX wobble (:224). Original: per-frame axis-angle with v above; ~30-35 frames/s.
4. Squares: remake flyingSquares (:97-113, called :220/:280: random tilted squares streaming at the camera, alpha). Original: 256 static
   unit squares on the integer lattice [-20,20]^3, all parallel to the world XY plane, seen from a camera orbiting at
   radius 30 (2.4) that lazily looks at the origin; additive glow lines x2; regenerated (new rand values) at C's init.
5. Projection: f = 300 px on a 320x200 frame (vertical fov 2*atan(100/300) = 0.644 rad, horizontal 0.98 rad) and
   1.2 vertical stretch; remake fovy 1.0 (:218).
6. Object centred at the world origin at distance 30 (core 10 px, tips up to ~125 px); near-camera parts (Z<=20)
   are culled per face.
Star (274-299):
1. Texture: remake tex3d#2 + tint (:287-288). Original sphere.raw (blue chrome), raw sphere map
   u=128+120*n_in.x, v=128+120*n_in.y, no lighting/specular, palette straight from sphere.raw.
2. Twist/wobble (:291-294) -> same delay-line mechanism, restarting from straight arms at the part start.
3. Motion: remake translate drift + spin (:285-286). Original: object at origin, orientation M (continuing t from A);
   screen offset only from the lagging camera at the start (enters from the frame edge).
4. Squares as in the spiral; start flash 0.47 s (remake 0.35 s, :296); start at pos 0xc81 (capture 163.43, remake
   162.95).

---------------------------------------------------------------------------------------------------------------------
## 3. Vic (B): 0x15734(sel) — photo + spikey + credit words

Used twice: sel 0 at 0x31374 ("statix / code", alex.raw), sel 1 at 0x2f8b2 ("vic / music", vic.raw).
Parameterisation (tables copied to locals at entry):
* file name table 0x52f80[sel]: "alex.raw", "vic.raw", "jo.raw" (jo.raw is not in MUSTARD.PAK; sel 2 unused).
* object table 0x52f8c[sel] = {a1, a2, ox, oy, oz}: sel0 {1.0, 1.0, 140, 100, 900}, sel1 {-1.0, 0.6, -240, -90, 1300},
  sel2 {0.2, 1.1, -150, 180, 1100}.
* word table 0x52f20[k], k = sel + 3*j (j = 0,1), each {x, y, srcx, width} in credits.raw (512x32, words
  "code"0-72 "music"73-168 "vocals"169-264 "statix"265-355 "vic"356-399 "emma"400-505):
  k0 statix (8,8,265,91), k3 code (40,35,0,73); k1 vic (8,158,356,44), k4 music (120,160,73,96);
  k2 emma (8,8,400,106), k5 vocals (210,155,169,96).

Init: credits.raw -> B3, each byte >>= 3 (0..31). phong.raw: palette -> 0x5faa8 (all 256), texture -> [0x5b674].
B1 (32 KB) = noise: B1[i] = 2*N[rand()&15], N = {0,0,1,1,-1,-1,2,-2,6,-4,2,1,4,-1,-6,-2} (signed).
Photo palette: entries 0..127 = photo palette entries 0,2,4,...,254 (phong.raw keeps 128..255).
Photo pixels: B0+320 = file pixels >> 1 (0..127). Spikey: 0x13e78(a1, a2) (below). [0x52e1c..24] = (ox,oy,oz).
Spring: v[0x9c464]=5, z[0x9c468]=4000. n[0x9c480]=0. Palette level 0x8000 (white) -> 0x4000 at 300/tick.
S = [0x9b840] = (pos+0x20)&~0x3f. Per-tick callback [0x5d698] = 0x154d4.

Tick callback 0x154d4:
```
z1 = z + v;  v -= trunc(z1/300);  z = trunc(z1*252/256);            // damped spring, period ~109 ticks (1.55 s)
if ((rand()&255) < 5) v += 150 + (rand()&63);                         // random kicks, ~1.4 per second
spin_t[0x9c474] += 4;  [0x9c470] += 0.00125 (unused);
if ((tickcount & 7) == 0) for i in 1..32: J[i] = trunc(0.7*N[rand()&15]);   // word jitter bytes 0x9c630..
n = pos > S+0x20 ? 1 : 0;  if (pos > S+0x40) n = 2;  if (pos > S+0x80) n = 3 (end);
if (pos > S+0x78 && !(pos & 1)) { level = 0x8000; step = 1800; }     // white strobe in the last 4 rows
```
Render loop per frame:
```
s = 1 + min(|z/128|, 60);                                   // horizontal box width 1..61
step = trunc(0.8 * trunc((65535 - 1100*s)/s));
T[i] = min(255, (i*step)>>16) for i < 16384;               // 0x1c91d builds this table every frame
for each row y, x: dst = T[ sum_{k<s} photo[y][x - s/2 + k] ]   // running box sum, = avg*0.8*(1-0.0168*s)
for 4 strips of 16000 bytes: off = rand()&0x3fff; dst = clamp(dst + (int8)B1[off+i], 0, 127)   // 0x1c789 grain
words: for j while 3*(n%3) > sel+3j:                        // n=1: first word, n=2: both
    for p in 0..3:  shadow: dst[x+3+dx_p][y+3+dy_p] = max(0, dst - word)       (dx,dy = J[2p], J[2p+1])
    for p in 4..7:  dst[x+dx_p][y+dy_p] = min(127, dst + word)
    once more with jitter p=4 (additive)                     // 5 additive passes -> white core, saturates at 127
spikey (below), show
```
So the photo is darkened (x0.79 at s=1, x0.37 at s=30) — with vic.raw's dark-blue -> mauve -> gold palette that is
what makes it look purple/blue; it is not a duotone grade. Blur is horizontal only and pumps with the spring
(starts at s=32, very blurred, settles, re-kicks randomly). Grain is a fixed noise texture at random offsets.
Word pixel extents (no shadow): vic x 9-51, y 158-186 (centre 0.094, 0.86 of the frame); music x 121-215, y 168-184
(0.525, 0.88); statix x 10-97, y 8-36 (0.167, 0.11); code x 40-112, y 36-59 (0.238, 0.238). Jitter ±4 px, changes every
8 ticks; words appear instantly (no fade).

Spikey (0x13e78 init, 0x1405c transform, 0x14290 draw): spikey.dat vertex = unit dir d and radius r (93..206).
R = 50*a2 + 120; position = d * (R + (r - R)*a1). sel1: R=150, a1=-1 -> radius 300 - r: the spikes become DENTS in a
ball of radius ~190-207 (the "bumpy" object); sel0: radius r (normal spikey). Normals: per face (v2-v1)x(v0-v1)
(inward) accumulated and normalised once at init.
Per frame: A = t/700, B = t/243 (t = [0x9c474], +4/tick, never reset: 0.4 rad/s and 1.152 rad/s; the vic part
continues from where the alex part stopped), Rm = 0x14760 matrix (Rz(A)*Ry(B)), p' = Rm p, n' = Rm n,
k = 600/(p'.z + oz), sx = 160 + k*(p'.x+ox), sy = 100 + k*(p'.y+oy), env u = trunc(n'.x*120+128),
v = trunc(n'.y*120+128) into phong.raw (pixel values 128..255). Faces bucketed by (b0+b1+b2)>>2 with
b = clamp(trunc(p'.z*0.5+66), 0, 132) and drawn bucket 0 first (near-to-far: likely a bug, mostly hidden by back-face
culling). Screen placement: sel1 centre (49, 58), radius ~95 px (top-left, partly off screen); sel0 centre (253, 167),
radius 67 px body / 137 px spike tips.

Timing (sel 1): S = 0xc00. "vic" at pos >= 0xc21 (music 159.50; capture 158.33), "music" at pos >= 0xc41 (161.18;
capture 160.00), strobe from pos >= 0xc7a (164.12; capture 162.93), end at pos >= 0xc81 (164.54; capture 163.40).

Differences vs remake Vic (part3.cpp:242-271):
1. Photo: remake zooms the photo (z = 1+0.03 t, :246) with a duotone (:248). Original: no zoom/motion; horizontal box
   blur of width s = 1+min(|z|/128,60) driven by the damped spring + random kicks (starts very blurred), darkening
   0.8*(1-0.0168 s), additive grain, palette = vic.raw even entries (dark blue -> mauve -> gold).
2. Object: remake spikey with spikeScale 0.22 (:257) — original is the spikey turned inside out (radius 300 - r,
   dents), much larger (radius ~95 of 200 px) at screen (49,58), env phong.raw raw lookup, no specular.
3. Rotation: Rz(t/700)*Ry(t/243) with t in ticks*4 (0.4 and 1.15 rad/s), continuing from the alex part; remake
   spin(0.35, 0.5, 0.15) (:254).
4. Words: instant pop-in at 0xc21/0xc41 (remake fades 157.5-158.0 / 159.6-160.1 (:260), ~0.6-0.8 s early vs the capture);
   shadow offset (3,3) px, 4 jittered subtractive + 5 jittered additive passes, jitter changes every 8 ticks.
5. End: white strobe (alternating half-rows) from 162.93 capture, cut at 163.40; remake flash ramp 162.5-162.95 (:268) and
   cut at 162.95 (timeline.cpp:25).

---------------------------------------------------------------------------------------------------------------------
## 4. 0x15734(0) in the alex part (remake AlexVideo, part1.cpp:127-158)
Same routine: alex.raw (palette even entries), normal spikey (a1=1, a2=1 -> radius r) at screen centre (253,167),
k = 600/(z+900), words "statix" (8,8) at pos >= 0x561 (music 68.29; capture 66.77) and "code" (40,35) at pos >= 0x581
(70.00; capture 68.50), strobe from pos >= 0x5ba (72.97), end pos >= 0x5c1 (73.40) [S = 0x540].
Differences: remake zoom/drift + dim ramp + tint (:133-135) -> original static photo with the spring box blur,
darkening and grain; remake word fades (:150) -> instant pop; remake spikey at (0.75,-0.62) with spin (:142) -> screen
(253,167)/320x200, Rz(t/700)Ry(t/243) starting at t=0 at this part; the part ends with the white strobe
(remake cuts at 71.65 = strobe start).

---------------------------------------------------------------------------------------------------------------------
## 5. Plasma section D/E/F (corridor, two translucent balls, gold ray sprite)

Shared state: ball centre [0xc1d34..3c] = (-1000, 0, 60) (set by the blobs init 0x2f266), look-at target
[0xc1d40..48] = same point. tex3d.raw pages: 0 gold env, 1 purple hex (corridor), 2 blue plasma ([0xc1d24], ball 1),
3 pink plasma ([0xc1d28], ball 2), 4 gold ray falloff ([0xc1d2c], sprite), 5 stripes. Palette = tex3d palette
(0x1496e8), fade table 3d.fad, blend 3d.gst (additive).
D start: palette restored from 0x1496e8, [0x57d74]=1, [0x1499e8]=0; the restored callback 0x2e684 sees the mismatch
on the first tick, sets [0x57d74]=0 (solid mode: corridor texture page 1, balls textured, grid textured), reloads
3d.fad into [0x5b67c] and flashes white (step 1000) — that is the white flash at 169.93.

Per-frame calls: D: 0x27804(230), 0x27444(3), corridor 0x27670, metaballs 0x26c14, balls 0x28100.
E: 0x27804(300), 0x27444(3), corridor, balls, sprite 0x28330. F: 0x276f8, 0x27444(1), corridor, balls, sprite,
grid 0x2902c (only once [0x1493d8]=1). (0x27444/0x27670/0x26c14/0x27804 belong to the blobs agent; summary:
0x27804(R) puts the camera goal on an orbit around the ball, Rorb = R*(1+sin(0.0005T+64)/6), theta = T/700+34,
phi = 0.000909T, goal = ball + Rorb*(sin th cos ph, sin th sin ph, cos th), T=[0xc1d6c] +4/tick; 0x27444 per tick:
camera position spring vel = vel*[0x57d7c](0.79) + (goal-cam)*0.002 on x,y (and z if flag 2), lazy look-at to the
target with gain [0x57d80] (flag 1), roll [0xc1d68]. 0x276f8 (F) sets only goal x,y:
ball.xy + (50 sin(0.0005T+64)+290)*sin(3T/700+34)*(cos, sin)(3*0.000909T).)

### 5.1 0x28100 — two ray-traced translucent balls (D, E, F)
Init (0x17, mid-section init): span buffer [0xc1d74] (64000), texture scratch [0x13efa8]; tick callback 0x280cc:
`spriteT[0x13efa0] += 0.23; noiseoff[0x13ef9c]++; T[0x57d84] += 7` (T starts at 1200).
Render:
```
c = [0xc1d70];                                             // radius offset (see callbacks)
r1 = max(0, c + 60 - 6*sin(25*9.43e-5*T + 43));            // 0.0023575*T
r2 = max(0, c + 68 + 6*cos(25*5.3e-5*T + 23));             // 0.001325*T
tex1 = (lvl==32) ? page2 : fade(page2, lvl);               // lvl = [0x13efac]; fade = 3d.fad row lvl (0x1c18a)
M = 0x14760(0.001*T, 0.000732*T);   sphere(ball, r1);  spans(tex1);   // 0x27958, 0x1c302 (0x1c37f in wire mode)
tex2 = page3 (faded likewise); M = 0x14760(4 + 0.001124*T, 7 - 0.000532*T); sphere(ball, r2); spans(tex2);
```
0x27958(centre, R) ray-traces the sphere: centre to camera space (o = -centre_cam); rows y and x-ranges solved
analytically and clipped to the screen; every 8th pixel the ray d = (x, y, 300) is intersected with the sphere taking
the FAR root λ = (-(o·d) + sqrt((o·d)² - |d|²(|o|²-R²)))/|d|², i.e. the BACK inner surface; n = (o + λd)*300/R
(hit point on a radius-300 sphere); u = trunc(dot(n, M.row0) + 0.1*T) & 255, v = trunc(dot(n, M.row1)) & 255.
0x1c302 interpolates u,v linearly over each 8-pixel run and writes dst = G3d[tex[v][u]][dst] (additive).
(Wire mode 0x1c37f would just add 8 to every covered pixel; not used here after the first tick.)
Both balls share the centre: an inner blue and an outer pink translucent ball, both showing their far wall with a
3D-planar projected texture (wraps ~2.3x across the ball), scrolling 49 texels/s in u; additive over the corridor.

### 5.2 Callbacks driving D/E/F
D, 0x2e3ac (per tick): s[0x5809c] starts -500 (data), s++; c = min(trunc(s/10), 0) -> [0xc1d70];
lvl = clamp(trunc(s/10)+32, 0, 32) -> [0x13efac]; if s > -100: the 4 metaball strengths at 0xa1518+64*i decrease
0.003/tick to 0. Main sets [0xa1558] (strength #1) = 1.0 and [0xc1d70] = -100 at D start; D ends when
[0xa1558] <= 0.001.
=> balls radius r1 ~ 11±6 / r2 ~ 19±6 at the start growing linearly to 60/68 at tick ~500 (7.1 s); invisible
(texture black) until tick 180 (2.6 s), fade in to full at tick 490 (7.0 s); metaballs shrink from tick 400 (5.7 s);
D lasts ~400+333 = 733-734 ticks = 10.47 s. After D: [0x13efac]=32, metaball callback removed (0x26c08 =
remove 0x2685c), 0x23540/0x29ae8 freed.
E, 0x2e360: g[0x58098] starts -0.8 (data), += 0.008/tick capped 1.0; [0x13efa4] = max(g, 0). E ends at
[0x13efa4] >= 1.0: 225 ticks = 3.21 s; the sprite scale is 0 for the first 100 ticks (1.43 s) then ramps to 1.
F, 0x2e05c (per tick; e = [0xc1d70] starts 0, speed [0x1493d4] = 0, w [0x580b4] = 1.0, ph [0x1493d0] = 0):
```
ph += 0.01;  if (roll > -0.02) roll -= 0.0001;                       // [0xc1d68]: camera roll rate ramps to -0.02/tick
if (e > 900) {                                                        // phase 2 (last 50 ticks)
    [0x1493d8] = 1 (draw grid); [0x57d80] = 0.05; ball = (0,0,0); target.xy = 0;
    cam.z += w; w += 0.1; roll = 0.04*sin(ph); target.z = e + 15000;
    e += 2; if (e > 1000) { level target 0x4000 step 150; [0x1493dc] = 1 (end) }
} else if (cam.z > 1500) {
    if (first) { level = 0x8000 -> 0x4000 step 400 (flash) }          // capture 189.00
    sprite_scale[0x13efa4] += 0.2;  e += 7;                           // balls (radius e+60/68) and rays explode
    if (cam.z > 1700) { level target 0x8000 (white) step 100 }        // fade to white, 2.3 s
}
speed = min(speed + 0.02, 6);  cam.z += speed;  if (cam.z > 500) sprite_scale += 0.01;
[0xc1d70] = e;
```
The camera flies +z (away from the ball at z=60, still looking back at it, rolling) -> the balls/sprite recede down
the corridor (capture 186: small sphere, walls rolled ~90°), then at cam.z > 1500 the white flash, the sprite scale
grows 0.2/tick and the balls' radius grows 7/tick: the gold burst (188-191), fading to white, then phase 2 (camera
turns to +z; ball moved behind it; grid at z~15000 is ~11 px wide, under the white -> probably invisible) and the cut.
Measured: F flash 189.00, white plateau 191.2-191.77, fade back after 191.77. If D+E = 959 ticks from 169.93, F starts
~183.70 (capture) and cam.z reaches 1500 after ~370 ticks, consistent with cam.z ~190 at F start; F lasts
~370+179 = ~550 ticks (7.8 s) -> end ~191.6 capture.

### 5.3 0x28330 — gold ray sprite (E, F)
```
d = ball - campos (camera space); if (Z <= 40) return; k = 300/Z;
sx = 160 + X*k; sy = 100 + Y*k;  s = [0x13efa4]*k;  w = 88*s; h = min(w,200); w = min(w,320);
rays(s);                       // 0x27f58: builds remap[256]
draw rect [sx-w, sx+w] x [sy-h, sy+h] (x rounded up to /4, clipped) with 0x1c1bf, texture page 4, blend 3d.gst
```
0x27f58 (T = [0x13efa0], +0.23/tick = 16.1/s):
```
for i in 0..255:          // i = angle in 1/256 turn
   A = max(0, 30*sin²(0.0453*T + i*pi/8) - 15 + 7*sin(0.142*T + 43));   // 32 lobes, slow drift, width pulsing
   B = max(0, 100*sin²(i*pi/32 + 12 - 0.1345*T) - 70);                  // 8 narrow lobes rotating ~31°/s
   N = noise[(i + noiseoff) & 255];                                     // noise = rand()&15 (init 0x284f4), +1/tick
   remap[i] = min(255, trunc((A + B + 30 + N) * s));
```
0x1c1bf: for each pixel in the rect, look up the precomputed polar table (0xc1e9c, 640x400 x 2 bytes, centred on the
rect centre; built by 0x284f4: byte0 = angle = atan2 in 1/256 turns (0x10328, full turn 0x20000, x=0,y<0 -> half
turn), byte1 = min(255, round(sqrt(x²+y²))) pixels), then texel = page4[remap[angle]][radius] and
dst = G3d[texel][dst] (additive). Page 4 is a gold falloff: row r stays gold out to a radius of ~1.25*r px.
So: a gold glow whose radius per angle is ~1.25*(30+A+B+N)*s px: a disc of ~37*s px plus 32 soft spikes, 8 long
thin rays and flicker; in pixels, not scaled with distance except through s.

### 5.4 0x2902c — wavy grid (F phase 2 only), phong1.raw
Init: phong1.raw (64 KB, values 0..63, a soft light blob) -> [0x5b670]; callback 0x28a10: Tg[0x57d8c] += 3 (starts 523).
Render: 21x33 vertices (693), 1280 triangles: x = 15j (j=-16..16), y = 15i (i=-10..10),
z = 15000 + h, h(i,j) = 60*sin(j/7 + 0.0164375*Tg)*cos(0.2*i - 0.013*Tg + 34); normal = normalize(h(j+0.1)-h,
h(i+0.1)-h, 1). 0x26524 projects with the camera (Z > 40, f = 300) and env coords 126*n_cam+128; 0x285d8 draws
far-to-near with 0x1e223 (solid mode): texture page 2 at planar u = 0.390625x+128, v = 0.625y+128, lit through
3d.fad by phong1 at the normal coords (wire mode would use 0x23118). Seen from cam.z ~2300-2500 it is ~11 px wide.

### 5.5 Differences vs remake Plasma (part3.cpp:302-381)
1. Object: remake has one ray-marched plasma sphere approaching the camera (dist 16 -> 2.1, :316-320) with spherical
   uv, fresnel and halo (fx.cpp:307-331). Original: two concentric balls centred at a fixed point, radii growing
   from ~0 to 60/68 world units (corridor scale; floor/ceiling are the blobs agent's), inner blue (page 2), outer
   pink (page 3), each textured on its far inner wall with a planar projection of the rotated hit point, additive
   over the corridor, fading in from black between 2.6 s and 7 s into D. Camera orbits the balls (radius 230 in D,
   300 in E, spring-followed, lazy look-at), then in F flies away along +z while rolling.
2. Rays: remake = radialBlur + scaled phong1 image + procedural sin rays + golden multiply (:333-376). Original = the
   polar-table sprite 5.3 (32 + 8 rotating lobes + noise, gold falloff page 4, additive), appearing 1.43 s into E,
   scale ramp 0->1 over E, then +0.01/tick (cam.z>500) and +0.2/tick after the flash. No radial blur, no multiply
   pass; phong1.raw is only the light map of the (invisible) grid.
3. Golden pair: the remake's two small gold balls (t<174, :322-326) correspond to the metaballs (0x26c14, shrinking
   from 5.7 s into D) and the balls starting small; the original keeps them all at one place.
4. Timing: D/E/F are tick driven: D 10.47 s, E 3.21 s, F ~7.8 s; flash at F's cam.z>1500 (capture 189.00),
   white fade 189.5-191.8, cut ~191.6-191.8 (remake flash 189.8-191.0 :377, cut 191.3; burst 186.5-190 :333).
5. Start flash 0.23 s at 169.93 (remake 169.95-170.4 ✓).

---------------------------------------------------------------------------------------------------------------------
## 6. Not determined / caveats
* Original frame rate (affects spin speed of A/C, not the curl): estimated 30-35 fps from the capture only.
* [0x57d80] value on entry to A (camera turn gain): modified by earlier callbacks (0x2e438) — not traced.
* D/E/F corridor camera, metaballs (0x27444, 0x27670, 0x27804, 0x26c14) only summarised; floor geometry not checked.
* Exact F duration depends on cam.z at F start (from E's spring); estimated from the capture.
* 0x1e223 (lit textured triangle) not decompiled in detail; 0x28aa0 is not used in this range (only at 0x3079f).
* The 0x2b394 early-out (vertex sx outside [-320,640]) leaves the rest of the vertices stale for that frame; harmless.
