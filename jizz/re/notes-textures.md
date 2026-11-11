# Scope "textures": the "decrunching gfx" texture generator (0x12c50, 0x12e72)

All addresses are offsets in `jizz_img.bin` (= near pointers of the running program, loaded at base 0). Scripts and
pictures are read from the image by address. A "buffer" is 0x30000 bytes = three 256x256 byte planes R (+0),
G (+0x10000), B (+0x20000); pixel (x, y) of a plane is at `y*256 + x`. Pixel coordinates wrap at 256 in every op
(the textures tile). `f32(v)` = round to float32 (round to nearest even); `rint` = round to nearest even integer.

Verified against the original, not only read (models in `jizz/re/models/textures/`, section 11):
- `texgen.py` regenerates all 16 files of `dump/*.tga` (17 TGA writes, map14 twice) **byte for byte**, and all 17
  final texture buffers [0x654e]..[0x658e] byte for byte equal to the `snaps/t440.snap` memory (t430 is still
  inside 0x12c50: it stops in map15's blur, before the map15 stripes and the [0x6572] gradients, so use t440;
  the first part does not write the texture buffers);
- `test_ops.py` runs the original interpreter 0x12e72 in unicorn (`emu.py`) on a made-up script that uses the ops
  and argument ranges the 17 scripts do not use (ops 0x0d, 0x16, 0x1e, 0x25, negative scales and matrix entries,
  sine amplitude 200, mix alpha 0, odd masks): identical result.

---------------------------------------------------------------------------------------------------------------

## 1. What it is, when it runs

During the precalc, after "uploading samples to gus", main prints "decrunching gfx:" and calls 0x12c50 (call site
0x18bd8; capture ticks 49..433, about 21 s of the 24 s precalc at 100 M instructions/s; text mode, before the
video mode set and before the music starts). 0x12c50 runs 17 texture scripts through the byte-code interpreter
0x12e72 and does two post-processing passes. Everything is static after that: no texture is animated or rebuilt
later (parts only copy and convert them, section 8). No time base applies; the only "state" is the random
generator, which is reseeded before every script.

The progress display: [0x111ff] = 0, [0x11203] = 0x272 (0x18bb2); 0x1115c is called once per op dispatch
(626 ops in the 17 scripts = 0x272) and prints `count*100/0x272` as a percentage.

All textures are procedural (noise, midpoint-displacement clouds, sine plasmas, cones, an angle field, Hermite
curves, blobs, blurs, warps, emboss, HSV colour) except testshit, logo1 and logo2, which also use the only two
bitmaps: 4-bit run-length greyscale pictures of the "~JIZZ~ TBL'97" lettering and of Sjaak's "jizz" logo
(section 7).

## 2. The driver 0x12c50

```c
void build_textures(void)                    // 0x12c50, called once from main (0x18bd8)
{
    run_script(0x12b5d, [0x6582]);           // map14 (built again below)
    run_script(0x12439, [0x654e]);           // map1
    run_script(0x124c1, [0x658a]);           // map1a
    run_script(0x12599, [0x6552]);           // map2
    run_script(0x12630, [0x6556]);           // map3
    run_script(0x126fc, [0x655a]);           // map4
    run_script(0x1276f, [0x655e]);           // map5
    run_script(0x127f0, [0x6562]);           // weirdtxt
    run_script(0x12919, [0x6566]);           // map7
    run_script(0x12993, [0x656a]);           // map8
    run_script(0x129d6, [0x656e]);           // map9
    run_script(0x129ec, [0x6572]);           // (no TGA) blob height field
    run_script(0x129f7, [0x6576]);           // testshit
    [0x662b] = [0x6576] + 0x10000;           // its G plane, read by the TBL part (0x11986)
    run_script(0x12a7d, [0x657a]);           // logo2 (left in B2) and logo1 (the result)
    memcpy([0x658e], B[2], 0x20000);         // logo2, already 16-bit 320x200 (0x12d44)
    run_script(0x12adc, [0x657e]);           // map10
    run_script(0x12b5d, [0x6582]);           // map14 again: same script, same seed, same bytes
    run_script(0x12bd9, [0x6586]);           // map15

    // 0x12d87: map15 gets horizontal dark stripes, 9 periods over 256 rows
    for (y = 0; y < 256; y++) {
        float a = f32((y * 0x240) * 2pi/16384);        // fild, fmul qword [0x11596] (double 2*pi/16384)
        int   s = rint(f32(sin(a) * 100));              // fsin, fimul [0x2004] = 100, fistp
        u8    m = s + 150;                              // 50..250
        for (x = 0; x < 256; x++) for (p = 0; p < 3; p++)
            map15[p][y][x] = (map15[p][y][x] * m) >> 8;
    }
    // 0x12e08: the blob height field [0x6572] gets its gradients (signed bytes, wrapping)
    for (y = 0; y < 256; y++) for (x = 0; x < 256; x++) {
        H.B[y][x]   = H.R[y+1][x] - H.R[y-1][x];
        H.G[y][x-1] = H.R[y][x+1] - H.R[y][x-1];        // stored one pixel to the left
    }
}
```

The texture buffers are allocated contiguously by main (0x18767..0x18827): [0x654e] = heap + 0x130000, then
0x30000 apart up to [0x658a]; [0x658e] (0x20000 bytes) after it. In the headless run (heap at linear 0x100000):
[0x654e] = 0x230000 ... [0x658e] = 0x530000. The five work buffers B0..B4 of the interpreter are
[0x12425], [0x12429], [0x1242d], [0x12431], [0x12435] (0x30000 each, = 0x5eb000.. in the run).

## 3. The interpreter 0x12e72 and the script format

Input: ESI = script, EDI = destination, AL = 0 (always 0 here; AL = 1 would skip the clearing).

```c
u8 *B[5];       // [0x12425..0x12435]: B0..B3 work buffers, B4 = scratch copy (0x13f84)
u8 *W;          // [0x12e46]: the current buffer; ops work on W
int first, gap, count;   // [0x12e69] = first<<16, [0x12e6d] = gap<<16, [0x12e71] = count

void run_script(const u8 *s, u8 *dest)       // 0x12e72
{
    seed(0x1234abce, 0x03049291, 0x67fcd9a8);       // [0x141ea], [0x141ee], [0x141f2]
    memset(B[0], 0, B[4] - B[0]);                   // B0..B3 (not B4)
    W = B[0];
    for (;;) {
        progress();                                  // 0x1115c
        u8 op = *s++;
        if (op > 0x26) crash();                      // div by zero at 0x12f0a
        u8 m = s[0] & 7;                             // decoded for every op, used by most
        first = table[m].first; gap = table[m].gap; count = table[m].count;   // table 0x12e4a, 4 bytes each
        handler[op](...);                            // jump table 0x12f0f, 39 dwords
    }
}
```

The plane-mask table at 0x12e4a (bytes first, gap, count, 0): masks are bit 0 = R, bit 1 = G, bit 2 = B, and mask
0 means all three:

| m | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| planes | RGB | R | G | RG | B | R,B (gap 1) | GB | RGB |

"For each plane of the mask" below means: plane `first`, then `first + 1 + gap`, ... (`count` planes).
`FIRST(m)` = `table[m & 7].first`. Ops marked "first plane" only use FIRST of their mask.

**Script format**: a byte stream; each op is an opcode byte, followed by a mask byte and arguments (lengths below).
Ops of length 1 do not have a mask byte (the interpreter still decodes the next opcode as one, harmlessly). Op 0x0b
and op 0x23 read their arguments starting at the mask position. Op 0x01 ends the script. Words and dwords are
little endian, `s8` = signed byte.

| op | len | bytes after the op | routine | what |
|---|---|---|---|---|
| 0x00 | 2 | n | 0x12fab | W = B[n & 7] |
| 0x01 | 1 | | 0x12fbf | end: copy W (0x30000 bytes) to the destination, return |
| 0x02 | 3 | m, v | 0x132c9 | fill planes(m) with v |
| 0x03 | 1 | | 0x132f1 | noise: W as 0xc000 dwords = rand() (all three planes) |
| 0x04 | 2 | m | 0x13300 (AX=0x0202) | box blur 2x2 of planes(m) |
| 0x05 | 2 | m | 0x13300 (AX=0x0404) | box blur 4x4 |
| 0x06 | 2 | m | 0x13300 (AX=0x0608) | box blur 8x8 |
| 0x07 | 1 | | 0x13378 → 0x13f98 | HSV → RGB (H, S, V in planes R, G, B) |
| 0x08 | 1 | | 0x133ad → 0x14088 | RGB → HSV |
| 0x09 | 3 | m, v | 0x133e2 | planes(m) += v, as dword adds |
| 0x0a | 4 | m, word n | 0x1356a | n random blobs, first plane |
| 0x0b | 3 | dx, dy | 0x13410 | move all three planes by (dx, dy), wrapping |
| 0x0c | 4 | m, s16 k | 0x13444 (ECX=0xff) | planes(m) = (v*k) >> 8, values above 255 → 255 |
| 0x0d | 4 | m, s16 k | 0x13444 (ECX=0x7fffffff) | planes(m) = (v*k) >> 8, low byte (not used by the scripts) |
| 0x0e | 3 | m, n | 0x13473 | planes(m) = (B[n] * W) >> 8 |
| 0x0f | 3 | m, n | 0x134a4 | planes(m) += B[n], as dword adds |
| 0x10 | 4 | m, n, a | 0x134d7 | planes(m) = (B[n]*a + W*(256-a)) >> 8 |
| 0x11 | 5 | m, n, mm, k | 0x13514 | planes(m) = mix with B[n] by alpha plane FIRST(mm) of B[k] |
| 0x12 | 6 | m, ang, s8 s, b, n | 0x139e9 | emboss plane FIRST(m) of W into plane FIRST(b) of B[n]; W = B[n] |
| 0x13 | 4 | m, b, n | 0x13ab9 | planes(m) = min(255, W + plane FIRST(b) of B[n]) |
| 0x14 | 9 | m, 6 x s8, amp | 0x137ea | sine plasma, first plane |
| 0x15 | 3 | m, n | 0x137aa | displace planes(m) by the (U, V) = (R, G) planes of B[n] |
| 0x16, 0x17 | 20 | m, cnt, size, 8 x s16 | 0x13182 → 0x1360b | Hermite curve of squares, first plane (scripts use 0x17 only) |
| 0x18 | 2 | m | 0x135cf | planes(m) = 255 - v |
| 0x19 | 3 | m, n | 0x131b9 | copy planes(m) of W to the same planes of B[n] |
| 0x1a | 4 | m, b, n | 0x135f1 | copy plane FIRST(m) of W to plane FIRST(b) of B[n]; W = B[n] |
| 0x1b | 1 | | 0x13799 | R += x, G += y (wrapping): identity displacement |
| 0x1c | 3 | m, r | 0x13af4 | cone of radius r around pixel (0,0), added, first plane |
| 0x1d | 3 | m, k | 0x13b5c | midpoint-displacement clouds, first plane |
| 0x1e | 5 | m, dx, dy, n | 0x13c7a | directional blur (not used by the scripts) |
| 0x1f | 2 | m | 0x13cfe | angle field atan2(y, x) around pixel (0,0), first plane |
| 0x20 | 1 | | 0x13d3d | 320x200 tiled copy of each plane (for screen backgrounds) |
| 0x21 | 1 | | 0x13d73 | planar → 16-bit pixels (VBE field layout) |
| 0x22 | 6 | m, dword addr | 0x13df4 | decode a 4-bit RLE picture (256x200), first plane |
| 0x23 | 10 | 9 x s8 | 0x13e72 | 3x3 colour matrix (/128, clamped) |
| 0x24 | 1 | | 0x13eec | planar → interleaved R, G, B bytes |
| 0x25 | 1 | | 0x132af | no-op |
| 0x26 | 1 + name + 1 | zero-terminated name | 0x14258 | `/pandora`: write W as a TGA under that name |

The scratch copy 0x13f84 (`memcpy(B[4], W, 0x30000)`, then ESI = B[4], EDI = W) is used by every op that reads
neighbouring pixels (blur, shift, warp, tile320, to16bit, matrix, interleave, directional blur), so they read the
unmodified image.

### 3.1 The random generator 0x141f6

0x141f6 is the program's generator (also used by 0x11dfe, 0x16079, 0x183e2; described in the core scope). The
texture scripts reset it to the seed above at the start of each script, so each texture is reproducible on its
own and the order of the scripts does not matter.

```c
u32 s0, s1, s2;                      // [0x141ea], [0x141ee], [0x141f2]
u32 rand(void)                       // returns EAX; EBX = the new s0 is used by the blobs
{
    u32 a = s0 + 0x0e372471, b = s1 + 0x00bc5ea9, c = s2 + 0x00c8ade7;
    a ^= b;
    s64 p = (s64)(s32)a * (s32)c;  a = (u32)p;  u32 d = (u32)(p >> 32);      // imul ecx
    d = rol(d, 13) + a;  c = rol(c, 14);  d -= b;  a ^= d;  b += c;  c ^= d;  a -= b;  b = rol(b, 9);
    u64 q = (u64)a * c;  a = (u32)q;  b ^= (u32)(q >> 32);                     // mul ecx
    a += 0x01e42ac3;  b += 0x005edf04;  c += 0x011a2ec1;
    s0 = b;  s1 = c;  s2 = a;
    return a;                        // EBX = b
}
```

Calls per op: noise 0xc000 (one per dword), blobs 2 per blob, clouds 1 + 1 + one per edge midpoint (section 4.11).

### 3.2 x87 precision

Main sets the precision control to 24 bits (0x18712), so every fadd/fsub/fmul/fdiv/fsqrt/fiadd/fisub/fimul/fidiv
result is rounded to a 24-bit mantissa: float32 arithmetic (no overflow or denormals occur). fild is exact, fsin /
fcos / fpatan are not affected by the precision control (the model uses the C library's double sin/cos/atan2,
which is what unicorn's x87 returns; a real FPU differs in the last bits, which changes a few pixels at most),
fldpi is the 64-bit pi, fistp rounds to nearest even. A product of a double (sin) and a float is rounded once to
24 bits.

## 4. The ops in pseudo-C

`P` = the 256x256 plane being processed, `T` = the same plane in the scratch copy B[4], `S` = a plane of B[n].
Indices wrap at 256.

### 4.1 Fill, add, scale, invert, copies (0x02, 0x09, 0x0c, 0x0d, 0x18, 0x19, 0x1a)

```c
fill:     P[i] = v;
add:      ((u32 *)P)[j] += v * 0x01010101;            // 0x4000 dwords: a byte that wraps carries +1 into the
                                                      // next pixel to the right (pixel 4j+1, 4j+2, 4j+3)
scale:    t = (P[i] * k) >> 8;                         // signed k, arithmetic shift
          P[i] = (t > 255 ? 255 : t) & 0xff;           // 0x0c (negative t keeps its low byte); 0x0d: t & 0xff
invert:   P[i] = ~P[i];
copy:     B[n].plane(p) = W.plane(p) for p in planes(m);
copyplane: B[n].plane(FIRST(b)) = W.plane(FIRST(m)); W = B[n];
```

### 4.2 Combining two buffers (0x0e, 0x0f, 0x10, 0x11, 0x13)

```c
mul:      P[i] = (S[i] * P[i]) >> 8;
addbuf:   ((u32 *)P)[j] += ((u32 *)S)[j];             // same byte carries as "add"
mix:      P[i] = (S[i]*a + P[i]*((256 - a) & 255)) >> 8;        // a = 0 gives 0
mixmask:  c = A[i];  if (c) P[i] = (S[i]*c + P[i]*(256 - c)) >> 8;   // A = plane FIRST(mm) of B[k]; c = 0 keeps P
addsat:   P[i] = min(255, P[i] + S1[i]);              // S1 = plane FIRST(b) of B[n], the same for every plane of m
```

### 4.3 Box blur 0x13300 (ops 0x04/0x05/0x06: s = 2/4/8, shift = 2/4/6)

```c
for (y..) for (x..) {
    u16 sum = 0;
    for (j = 0; j < s; j++) for (i = 0; i < s; i++) sum += T[y - s/2 + j][x - s/2 + i];
    P[y][x] = sum >> shift;                   // = floor(sum / s^2)
}
```
The box covers offsets -s/2 .. s/2-1, so every blur also moves the image by +0.5 pixel right and down. (The code
steps the 16-bit index ECX = row<<8 | column with byte adds; at the last pixel of a row, `ebp & 0xff == 1`, the row
byte is incremented.)

### 4.4 HSV → RGB 0x13f98 (op 0x07, per pixel: H = R, S = G, V = B)

```c
int h6 = H * 6, sector = h6 >> 8, f = h6 & 255;
if ((sector & 1) == 0) f = 255 - f;
int p1 = ((255 - ((f * S) >> 8)) * (V + 1)) >> 8;     // [0x14086]
int p2 = ((255 - S) * (V + 1)) >> 8;                   // [0x14087]
// table 0x1403a: per sector the destinations of V, p1, p2
//   0: R=V G=p1 B=p2   1: G=V R=p1 B=p2   2: G=V B=p1 R=p2   3: B=V G=p1 R=p2   4: B=V R=p1 G=p2   5: R=V B=p1 G=p2
```
0x13f98 is also the TBL part's colour routine (call 0x1203e).

### 4.5 RGB → HSV 0x14088 (op 0x08)

```c
int e = (R > G) | (R > B) << 1 | (G > B) << 2;         // EBP = e * 16, table 0x14156 (16 bytes per entry)
// entry: where R, G, B go among {max [0x141df], mid [0x141e0], min [0x141e1]} and the sector (dword << 8)
//   e=0: R=min G=mid B=max sector 3     e=1: R=mid G=min B=max sector 4     e=3: R=max G=min B=mid sector 5
//   e=4: R=min G=max B=mid sector 2     e=6: R=mid G=max B=min sector 1     e=7: R=max G=mid B=min sector 0
//   (e=2 and e=5 cannot happen)
if (max == min) { H = 0; S = 0; V = max; }
else {
    int q   = 0xff00 / max;
    int mid2 = 255 - ((mid * q) >> 8);
    S       = 255 - ((min * q) >> 8);                  // 1..255
    int f   = 255 - ((mid2 * (0xff00 / S)) >> 8);
    if (sector & 1) f = 255 - f;
    H = (sector * 256 + f) / 6;  V = max;
}
R = H; G = S; B = V;
```

### 4.6 Blobs 0x1356a (op 0x0a, first plane)

```c
for (k = 0; k < n; k++) {
    rand();  u32 pos = EBX_after_that_call;            // = the new s0
    u8 v = rand();                                      // low byte of EAX of the second call
    int x0 = pos & 255, y0 = (pos >> 8) & 255;
    // 0x1358b: an 8x8 disc; row j covers x0+dx .. x0+dx+w-1
    static const int dx[8] = {0,-1,-2,-2,-2,-2,-1,0}, w[8] = {4,6,8,8,8,8,6,4};
    for (j = 0; j < 8; j++) for (i = 0; i < w[j]; i++) P[y0 + j][x0 + dx[j] + i] = v;
}
```
The disc is centred on (x0 + 2, y0 + 4) in continuous pixel coordinates (pixel x covers [x, x+1)), radius 4.

### 4.7 Shift 0x13410 (op 0x0b; the mask byte is dx)

`W.plane(p)[(y + dy) & 255][(x + dx) & 255] = T[y][x]` for all three planes.

### 4.8 Emboss 0x139e9 (op 0x12)

```c
// src = plane FIRST(m) of W, dst = plane FIRST(b) of B[n]; afterwards W = B[n]
float t  = f32(ang * C);                    // C = float [0x141e2] = 2*pi/256
double sn = sin(t), cs = -cos(t);
float k  = f32((s * 64) / f32(fabs(sn) + fabs(cs)));
float cx = f32(k * f32(cs)), cy = f32(k * f32(sn));
for (y..) for (x..) {
    int dx = src[y][x+1] - src[y][x], dy = src[y+1][x] - src[y][x];
    int v  = rint(f32(f32(dx * cx) + f32(dy * cy)));
    dst[y][x] = v < 0 ? 0 : v >= 0xffff ? 255 : v >> 8;
}
```
So `dst = clamp(s/4 * (-dx*cos t + dy*sin t) / (|sin t| + |cos t|), 0, 255)`: a directional slope, light from
angle t, s = strength (signed). There is no 128 offset: slopes facing away give 0.

### 4.9 Sine plasma 0x137ea (op 0x14, first plane)

Six signed byte frequencies p0..p5 (in 1/256 turns per pixel) and an amplitude `amp` (0..255):

```c
float c[6] = { f32(p0*C), f32(p1*C), f32(p2*C), f32(p3*C), f32(p4*C), f32(p5*C) };   // [0x139d1] [0x139d5]
float A = 0, Bv = 0, Cv = 0;                         // [0x139c5] [0x139c9] [0x139bd]   [0x139dd] [0x139e1]
for (y = 0; y < 256; y++) {                          //                                 [0x139d9] [0x139e5]
    A  = f32(A + c[2]);
    Bv = f32(Bv + c[3]);
    float R = f32(sin(f32(f32(f32(sin(A) * PI) + Bv) + Cv)) * amp);   // PI = fldpi
    Cv = f32(Cv + c[4]);
    float X = Cv, Y = 0, Z = A;                      // [0x139b5] [0x139b9] [0x139cd]
    for (x = 0; x < 256; x++) {
        X = f32(X + c[0]);  Y = f32(Y + c[1]);  Z = f32(Z + c[5]);
        P[y][x] = rint(f32(sin(f32(f32(f32(sin(X) * PI) + Y) + Z)) * R)) & 0xff;   // signed result, low byte
    }
}
```
Without the float32 accumulation: with u = x+1, v = y+1 and w = 2*pi/256,
`P = round(R(y) * sin(pi*sin((p4*v + p0*u)*w) + p1*u*w + (p2*v + p5*u)*w))`,
`R(y) = amp * sin(pi*sin(p2*v*w) + p3*v*w + p4*y*w)`. All frequencies are integers per 256 pixels, so the plasma
tiles. The byte is a signed value; the scripts follow it with "add 128" (to unsigned) or use it as a signed
displacement (with op 0x1b).

### 4.10 Cone 0x13af4 (op 0x1c, first plane)

```c
float K = -f32(255.0 / r);                           // float [0x141e6] = 255
for (y..) for (x..) {
    int X = (s8)x, Y = (s8)y;                        // -128..127: distance to pixel (0,0) on the torus
    float d = f32(sqrt(X*X + Y*Y));
    int v = rint(f32(f32(d - r) * K));               // = round(255 * (r - d) / r)
    if (v >= 0) P[y][x] += v;                        // byte add, wraps
}
```
Followed by "shift" ops to place several cones (the shift moves the whole image, so cones drawn earlier move too).

### 4.11 Clouds 0x13b5c (op 0x1d, first plane): midpoint displacement

```c
void clouds(u8 *P, int k)                            // [0x2004] = k
{
    memset(P, 0, 65536);
    P[0] = rand();
    quad(0, 0, 256);
}
void quad(int x, int y, int size)                    // 0x13b8b (EBX = y<<8|x, ECX = size), recursive
{
    if (size <= 1) return;
    int half = size / 2, v;
    if (size == 256) v = (u8)rand();
    else v = (edge(x, y, half, 0, size, 0, half)              // top    (x+half, y)
            + edge(x, y, 0, half, 0, size, half)              // left   (x, y+half)
            + edge(x + size, y, 0, half, 0, size, half)       // right  (x+size, y+half)
            + edge(x, y + size, half, 0, size, 0, half)) >> 2;   // bottom (x+half, y+size)
    P[y + half][x + half] = v;
    quad(x, y, half);  quad(x + half, y, half);  quad(x + half, y + half, half);  quad(x, y + half, half);
}
int edge(int x, int y, int ox, int oy, int ex, int ey, int half)   // 0x13c03
{
    u8 *e = &P[y + oy][x + ox];
    if (*e) return *e;                               // 0 = not set yet; set values are 1..255
    int d = (int)(rand() % (2*k + 1)) - k;           // 0x13c5b: unsigned modulo
    int v = (((d * half) >> 1) + P[y][x] + P[y + ey][x + ex]) >> 1;   // arithmetic shifts
    v = (v < 0 ? 0 : v > 254 ? 254 : v) + 1;
    return *e = v;
}
```
Quirks that are part of the result: the top level only sets the centre (128,128), so the points (128, 0) and
(0, 128) are never set and stay 0 (their neighbourhoods are pulled dark); the centre and corner values come from
plain random bytes and may be 0. The texture tiles (all indices wrap).

### 4.12 Displacement 0x137aa (op 0x15) and identity offsets 0x13799 (op 0x1b)

```c
addxy (0x1b): W.R[y][x] += x;  W.G[y][x] += y;       // bytes, wrapping; all of W, no mask
warp  (0x15): for p in planes(m): P[i] = T[ S.G[i] * 256 + S.R[i] ];   // S = B[n]; nearest-neighbour read
```
The scripts build displacement maps as `B.RG = offset field (sines, clouds, scaled); addxy;` then warp a buffer
with it: `out(x, y) = in(x + U(x,y), y + V(x,y))` mod 256.

### 4.13 Hermite curve 0x1360b (ops 0x16/0x17, first plane)

The 8 words are (P0, P1, T0, T1) for the curve's first coordinate r and then for its second coordinate a.
n = cnt * 16 points, squares of size x size pixels of 255.

```c
float inv = f32(1.0 / n);                            // [0x2008]
// per coordinate (0x1363a, exact integers): F0 = P0, F1 = T0, F2 = -(3*(P0-P1) + 2*T0 + T1), F3 = 2*(P0-P1) + T0 + T1
float t = 0;
for (i = 0; i < n; i++) {
    t = f32(t + inv);
    float t2 = f32(t*t), t3 = f32(t2*t);
    float r = f32(f32(f32(t*F1r) + F0r) + f32(f32(t2*F2r) + f32(t3*F3r)));
    float a = f32(f32(f32(t*F1a) + F0a) + f32(f32(t2*F2a) + f32(t3*F3a)));
    float ang = f32(a * C);
    int py = rint(-f32(sin(ang) * r)), px = rint(f32(cos(ang) * r));      // polar: a in 1/256 turns
    for (j = 0; j < size; j++) for (k = 0; k < size; k++) P[py - size + j][px - size + k] = 255;
}
```
The curve is in polar coordinates around pixel (0,0), r up to 132 (wrapping). The squares cover
[px - size, px) x [py - size, py). The scripts draw one curve, then "shift" (which moves what is already drawn),
then the next. Sample spacing is at most 1.9 pixels against pen sizes 6..16, so the strokes are continuous.

### 4.14 Angle field 0x13cfe (op 0x1f, first plane)

`P[y][x] = rint(f32(atan2((s8)y, (s8)x) / C)) & 0xff` (fpatan, fdiv [0x141e2]): the angle around pixel (0,0) in
1/256 turns (0 along +x, 64 along +y = down). Not tileable: it jumps at the torus seam x = -128 / y = -128.

### 4.15 Directional blur 0x13c7a (op 0x1e, unused)

```c
int st = -(n >> 1);  u16 o = (u8)((s8)dy*st) << 8 | (u8)((s8)dx*st);   // [0x200c], 8-bit products
for (i = 0; i < 65536; i++) {                        // per plane of m
    u16 p = o + i;                                    // the start index carries from x into y
    u16 sum = 0;
    for (k = 0; k < n; k++) sum += T[(u8)((p >> 8) + k*dy)][(u8)(p + k*dx)];
    P[i] = sum / n;
}
```

### 4.16 Format conversions (0x20, 0x21, 0x24, 0x23)

```c
tile320 (0x13d3d):  for p in 0..2: W.plane(p)[y*320 + x] = T.plane(p)[y][x & 255]  (y < 200, x < 320);
                    bytes 64000..65535 of each plane keep their old contents
to16bit (0x13d73):  for i in 0..65535: ((u16 *)W)[i] = (T.R[i] >> (8-rs)) << rp | (T.G[i] >> (8-gs)) << gp
                                                      | (T.B[i] >> (8-bs)) << bp;
                    // sizes/positions from 0x1f5b8 ([0x18d70..0x18d84], self-modified shift counts): 5:6:5
                    // gives (R>>3)<<11 | (G>>2)<<5 | B>>3; 5:5:5 works the same way. Writes 0x20000 bytes;
                    // after tile320 the first 64000 pixels are the 320x200 screen image
interleave (0x13eec): W[3i] = T.R[i]; W[3i+1] = T.G[i]; W[3i+2] = T.B[i];
matrix (0x13e72):   for c in 0..2: W.plane(c)[i] = clamp((T.R[i]*M[3c] + T.G[i]*M[3c+1] + T.B[i]*M[3c+2]) >> 7, 0, 255)
                    // M = 9 signed bytes starting right after the opcode
```

### 4.17 The TGA writer 0x14258 (op 0x26, only with `/pandora`, [0x12e45] = 1)

Interleaves W into B[4] as B, G, R bytes per pixel (top row first), writes the 18-byte header at 0x14349
(image type 2, width/height words patched to 256 at 0x14355/0x14357, 24 bits, descriptor 0x20 = top-left origin),
then the 196608 bytes in 0x8000-byte writes (0x652e create, 0x653c write, 0x6537 close). So the TGA is the
state of W at that point of the script, not the final texture (see section 6).

## 5. Script locations

The 17 runs use 16 scripts, stored back to back at 0x12439..0x12c32 (2041 bytes). The two short scripts at
0x12c32 (`clouds RGB 20; end`) and 0x12c3f (`clouds RGB 10; rgb2hsv; rgb2hsv; clouds R 30; scale R 41; hsv2rgb;
end`, 0x12c3f..0x12c50) are never run. The two pictures are at 0x275b8 (1966 bytes, to 0x27d66) and 0x27d68
(4463 bytes, to 0x28ed7).

| # | name (TGA) | script | bytes | ops | destination | final format |
|---|---|---|---|---|---|---|
| 1, 16 | map14 | 0x12b5d..0x12bd9 | 124 | 40 | [0x6582] | interleaved RGB 256x256 |
| 2 | map1 | 0x12439..0x124c1 | 136 | 35 | [0x654e] | planar 256x256 |
| 3 | map1a | 0x124c1..0x12599 | 216 | 55 | [0x658a] | planar, moved by (64, 64) after the TGA |
| 4 | map2 | 0x12599..0x12630 | 151 | 39 | [0x6552] | planar |
| 5 | map3 | 0x12630..0x126fc | 204 | 65 | [0x6556] | planar |
| 6 | map4 | 0x126fc..0x1276f | 115 | 40 | [0x655a] | interleaved RGB |
| 7 | map5 | 0x1276f..0x127f0 | 129 | 43 | [0x655e] | planar |
| 8 | weirdtxt | 0x127f0..0x12919 | 297 | 52 | [0x6562] | 16-bit 256x256 (0x20000 bytes; B plane left behind) |
| 9 | map7 | 0x12919..0x12993 | 122 | 42 | [0x6566] | planar |
| 10 | map8 | 0x12993..0x129d6 | 67 | 23 | [0x656a] | planar |
| 11 | map9 | 0x129d6..0x129ec | 22 | 6 | [0x656e] | planar |
| 12 | (none) | 0x129ec..0x129f7 | 11 | 5 | [0x6572] | R = blob heights, G/B = gradients (0x12e08) |
| 13 | testshit | 0x129f7..0x12a7d | 134 | 43 | [0x6576] | planar 320x200 (tile320), [0x662b] = G plane |
| 14 | logo2, logo1 | 0x12a7d..0x12adc | 95 | 28 | [0x657a] (logo1), [0x658e] (logo2 from B2) | 16-bit 320x200 each |
| 15 | map10 | 0x12adc..0x12b5d | 129 | 46 | [0x657e] | interleaved RGB |
| 17 | map15 | 0x12bd9..0x12c32 | 89 | 24 | [0x6586] | planar, with the stripes of 0x12d87 |

`models/textures/scripts.py` prints every script op by op with addresses. Short form (`sel` = op 0x00, plane
letters = mask, `B2.R` = plane R of B2; `emboss R ang a s s ->B2.G` reads W's R plane and writes B2's G plane):

- **map14** (0x12b5d): clouds R 25; clouds G 25; clouds B 25; scale R 70; add R 128; blur4 RGB; add R 93; copy R->B1;
  mul R B0; mix R B1 128; hsv2rgb; sel B1; sines R (3,2,1,-2,2,2) 127; sines G (1,-3,2,2,1,3) 127; add RG 128;
  blur4 RG; copy RG->B2; scale RG 33; addxy; sel B0; warp RGB B1; blur2 RGB; sel B2; emboss R ang 100 s 19 ->B2.G;
  blur2 G; blur2 G; sel B0; copy RG->B1; sel B1; scale R 50; addxy; sel B2; warp G B1; blur2 G; blur2 G; sel B0;
  addsat RGB B2.G; save "map14.tga"; interleave; end
- **map1** (0x12439): curve R n=480 size 16 (80,30,-100,128,50,470,40,-40); shift 58,0; curve R n=480 size 16
  (60,-100,-300,300,100,220,400,-350); shift 160,120; blur2 R; blur4 R; emboss R ang 90 s 10 ->B2.R; blur2 R;
  sel B0; blur8 R; copyplane R->B3.B; sel B0; scale R 70; addxy; sel B1; clouds R 30; clouds G 30; clouds B 30;
  blur8 RGB; rgb2hsv; sel B2; clouds B 30; blur8 B; scale B 180; add B 60; sel B1; copyplane B->B0.B; sel B1;
  mul B B2; mixmask B B0 by B3.B; hsv2rgb; warp RGB B0; addsat RGB B2.R; save "map1.tga"; end
- **map1a** (0x124c1): clouds R/G/B 27; blur8 RGB; scale RGB 210; add RGB 45; copy RGB->B1; rgb2hsv; clouds R 27;
  blur8 R; scale R 45; copy RGB->B2; hsv2rgb; sel B2; add R 170; hsv2rgb; mixmask RGB B0 by B1.R; mixmask RGB B1
  by B1.B; sel B0; fill RGB 0; curve R n=480 size 15 (60,-90,-50,-500,50,210,-100,200); shift 65,20; curve R
  n=480 size 15 (60,-50,-200,0,100,190,400,-500); shift 100,251; curve R n=480 size 15
  (-50,0,400,-150,180,250,-50,50); shift 40,115; blur4 R x3; emboss R ang 160 s 19 ->B1.R; blur2 R; blur4 R;
  sel B3; sines R (4,2,-3,1,3,-2) 20; add R 230; mul R B0; scale R 130; addxy; sel B2; warp RGB B3; addsat RGB
  B1.R; sel B0; copyplane R->B0.B; scale R 20; scale B 90; add B 165; sel B2; rgb2hsv; mul B B0; addbuf R B0;
  hsv2rgb; blur2 RGB; save "map1a.tga"; shift 64,64; end
- **map2** (0x12599): noise; sel B1; sines R (2,1,1,2,-3,1) 100; sines G (3,2,-3,4,-1,1) 100; sines B
  (-2,3,-2,2,3,-1) 100; add RGB 100; mix RGB B0 99; copy RGB->B0; blur2 RGB; blur4 RGB; emboss G ang 220 s 45
  ->B2.R; blur2 R; sel B0; addsat RGB B2.R; blur2 RGB; sel B2; sines R (3,2,4,5,1,3) 3; sines G (0,4,3,1,5,2) 3;
  addxy; sel B3; cone R 90; shift 80,80; cone G 130; scale R 150; scale G 200; addbuf RGB B2; sel B0; warp RGB
  B3; blur2 RGB; sel B1; sines G (2,-1,1,-2,3,3) 80; sines B (1,3,-1,1,2,1) 80; add RGB 160; sel B0; rgb2hsv;
  mul GB B1; hsv2rgb; save "map2.tga"; end
- **map3** (0x12630): noise; blur2 RGB; blur4 RGB; blur4 RGB; sel B1; sines R (-2,3,2,5,-4,2) 127; sines G
  (1,5,2,-1,3,1) 127; sines B (2,-2,1,2,-2,3) 127; add RGB 128; copy RGB->B2; mix RGB B0 130; sel B0; scale RGB
  30; sel B3; cone R 100; shift 130,70; cone G 110; shift 150,100; cone B 120; scale RGB 130; addbuf RGB B0; sel
  B0; fill RGB 0; cone R 90; shift 60,87; cone G 80; shift 100,112; cone B 110; shift 130,30; scale RGB 130;
  addbuf RGB B3; addxy; sel B2; warp RGB B0; scale G 160; add G 84; scale B 190; add B 64; sel B1; rgb2hsv;
  emboss G ang 40 s 13 ->B0.R; blur2 RGB; sel B1; mul GB B2; hsv2rgb; addsat RGB B0.R; copy RGB->B2; sel B0; fill
  RGB 0; angle R; cone G 200; shift 128,128; sel B1; warp RGB B0; blur2 RGB; sel B0; fill RGB 0; cone R 120;
  invert R; mul R B0; shift 128,128; sel B1; mixmask RGB B2 by B0.R; save "map3.tga"; end
- **map4** (0x126fc): noise; blur2 RGB; blur4 RGB; blur4 RGB; sel B1; clouds R/G/B 17; blur4 RGB; sel B0; mix RGB B1
  113; rgb2hsv; copy RGB->B1; blur2 RGB; mix RGB B1 128; sel B2; clouds R 23; clouds G 23; blur8 RG; shift
  128,128; scale RGB 50; addxy; sel B0; warp RGB B2; emboss R ang 160 s 7 ->B1.R; sel B0; emboss G ang 160 s 8
  ->B1.G; blur2 RGB; sel B0; addsat RGB B1.R; shift 128,128; addsat RGB B1.G; blur2 RGB; copy B->B1; copyplane
  R->B0.B; sel B1; copyplane B->B0.R; save "map4.tga"; interleave; end
- **map5** (0x1276f): noise; blur2 RGB; blur4 RGB; blur4 RGB; sel B1; clouds R/G/B 20; blur8 RGB; sel B0; mix RGB B1
  128; sel B3; clouds R 28; clouds G 28; blur4 RG; blur4 RG; scale RG 25; sel B2; sines R (4,3,2,3,3,4) 10; sines
  G (-3,-2,1,-3,4,1) 11; addbuf RG B3; addxy; sel B1; clouds B 50; blur8 B; blur8 B; scale B 175; add B 80; warp
  B B2; blur2 B; sel B0; rgb2hsv; emboss B ang 30 s 57 ->B2.R; fill GB 0; copy RGB->B3; blur4 R; mix R B3 128;
  sel B0; mul B B1; hsv2rgb; addsat RGB B2.R; save "map5.tga"; end
- **weirdtxt** (0x127f0): 3 curves G (n=240 size 6) with shifts 85,0 / 110,0 / 98,100 between them, then 5 curves R
  (n=240 size 6) with shifts 60,9 / 74,0 / 50,53 (parameters in scripts.py); blur2 RG; copy RG->B2; copy G->B3;
  copyplane R->B2.G; copyplane G->B2.B; sel B3; copyplane G->B3.R; copyplane G->B3.B; addbuf RGB B2; blur4 RGB;
  sel B0; blur4 RG; emboss R ang 30 s 12 ->B2.R; sel B0; emboss G ang 30 s 12 ->B2.G; blur2 RG; sel B0; scale RG
  128; addxy; sel B1; clouds R/G/B 20; blur2 RGB x2; scale RGB 200; add RGB 55; warp RGB B0; mul RGB B3; sel B2;
  copyplane G->B3.R; addbuf R B2; sel B1; addsat RGB B3.R; blur2 RGB; save "weirdtxt.tga"; to16bit; end
- **map7** (0x12919): clouds R/G/B 20; scale RGB 200; add RGB 55; blur8 RGB; sel B1; noise; blur2 RGB x2; blur4 RGB
  x2; copy RGB->B3; sel B2; sines R (1,2,2,-3,-1,-2) 127; add R 128; sel B0; mixmask RGB B1 by B2.R; rgb2hsv; sel
  B3; rgb2hsv; copy B->B0; sel B0; hsv2rgb; sel B1; blobs R 5120; blur2 RGB x2; blur4 RGB; emboss R ang 100 s 8
  ->B2.R; blur2 R; sel B1; scale R 70; add R 185; copyplane R->B1.G; copyplane R->B1.B; sel B0; mul RGB B1;
  addsat RGB B2.R; blur2 RGB; save "map7.tga"; end
- **map8** (0x12993): clouds R/G/B 30; blur8 RGB; rgb2hsv; fill R 162; sel B1; blobs B 5120; sel B2; clouds B 40;
  mix B B1 128; blur2 B; blur4 B x2; scale B 90; add B 165; sel B0; mul B B2; hsv2rgb; scale B 235; add B 20;
  save "map8.tga"; end
- **map9** (0x129d6): clouds R/G/B 30; blur8 RGB; save "map9.tga"; end
- **blob heights** (0x129ec): blobs R 5120; blur2 RGB x2; blur4 RGB; end (only R is non-zero; G/B are replaced by
  0x12e08)
- **testshit** (0x129f7): rle4 R @0x275b8; shift 170,150; sel B1; addxy; scale R 783; add R 1; scale R 10270; sel
  B0; mul R B1; sel B1; copyplane G->B1.R; scale R 1280; add R 1; scale R 10270; sel B0; mul R B1 (= the picture
  cut to a window by two thresholded ramps); sel B1; fill RGB 0; addxy; scale RG 115; sel B0; warp RGB B1; scale R
  336; shift 61,55; blur2 R x2; blur4 R; invert R; copy R->B1; fill RGB 0; cone R 61; shift 120,30; scale R 200;
  cone R 38; shift 60,50; scale R 220; cone R 47; shift 150,80; sel B1; copyplane R->B0.G; save "testshit.tga";
  tile320; end (R = three soft cones, G = the inverted, enlarged "TBL" letters)
- **logo1 / logo2** (0x12a7d): rle4 R @0x275b8; copyplane R->B0.G; copyplane R->B0.B; sel B1; clouds R/G/B 40; shift
  0,10; scale RGB 160; add RGB 95; blur2 RGB; blur8 RGB; copy RGB->B2; mul RGB B0 (B1 = clouds x lettering);
  sel B3; rle4 R @0x27d68; copyplane R->B3.G; copyplane R->B3.B; sel B2; mul RGB B3 (B2 = clouds x jizz logo);
  save "logo2.tga"; tile320; to16bit; sel B1; save "logo1.tga"; tile320; to16bit; end
- **map10** (0x12adc): clouds R 23; scale R 50; add R 10; clouds G 23; clouds B 33; scale GB 200; add GB 55; blur4
  RGB; hsv2rgb; blur2 RGB; sel B1; fill RGB 0; blobs R 5120; blur2 RGB; copy R->B2; sel B3; addxy; scale RG 105;
  sel B2; warp R B3; blur2 R; blur4 R; sel B3; fill RGB 0; cone R 126; shift 128,128; invert R; mul R B3 x2; sel
  B2; mixmask R B1 by B3.R; blur2 RGB; blur4 RGB; emboss R ang 0 s 16 ->B1.R; blur2 R x2; sel B2; scale R 51;
  addxy; sel B0; warp RGB B2; addsat RGB B1.R; blur2 RGB; save "map10.tga"; interleave; end
- **map15** (0x12bd9): sines R (1,1,0,1,0,1) 126; add R 128 (both overwritten: clouds clears the plane); clouds
  R/G/B 30; blur4 RGB; copy RGB->B1; matrix (65,30,40, 30,30,55, 50,80,80); mix RGB B1 80; sel B1; blur4 B;
  emboss B ang 100 s 49 ->B2.R; blur2 R; sel B1; scale RG 60; shift 128,128; addxy; sel B0; warp RGB B1; scale
  RGB 140; addsat RGB B2.R; blur2 RGB; save "map15.tga"; end

(`clouds R/G/B k` = three clouds ops, one per plane; `x2` = op repeated.)

## 6. TGA versus final buffer

The TGA is written by op 0x26 in the middle of the script; what the parts get differs for:
map14, map4, map10 (interleaved after the TGA), map1a (moved by 64,64), weirdtxt (16-bit), testshit (tiled to
320x200), logo1/logo2 (tiled and 16-bit), map15 (stripes added by the driver). The blob height field has no TGA.

## 7. The pictures: 4-bit run-length greyscale (op 0x22, 0x13df4)

JIZZ.DOC credits Jace with a ".jpg (JacePeG) cruncher". There is no transform-coding (DCT / JPEG-like) decoder in
the program: the only picture decoder is this nibble run-length format, and everything else is the procedural
script system. "JacePeG" is most likely Jace's joke name for this graphics packer (scripts + RLE pictures), as
".mp4 cruncher" is for Probe's music packer. (Interpretation, not provable from the code.)

```c
void rle4(u8 *P, const u8 *src)                  // P = plane FIRST(m) of W, filled linearly: 51200 = 256 x 200
{
    int left = 0xc800, half = 0;                  // nibbles: low nibble first, then high nibble
    while (left > 0) {
        int n = nib(), cnt = 1; u8 v = n * 0x11;
        if (n == 0)       { cnt = nib() | nib() << 4 | nib() << 8 | nib() << 12; v = 0;   }   // run of 0
        else if (n == 15) { cnt = nib() | nib() << 4;                            v = 255; }   // run of 255
        while (cnt--) { *P++ = v; left--; }        // a count of 0 would run 2^32 times (none in the data)
    }
}
```
16 grey levels (n * 17). Rows 200..255 of the plane are untouched (0 after the script's clear). Picture A
(0x275b8, 1966 bytes): "~JIZZ~" above "TBL'97" in serif capitals, white on black, placed for a 320-wide screen
(columns 0..63 black, they repeat at the right after tile320). Picture B (0x27d68, 4463 bytes): Sjaak's "jizz" logo
(white brush shapes with a small "jizz!" signature), mostly levels 0 and 255.

Both are true bitmaps at 256x200 with 4-bit levels: a remake can only scale them up (a good filter, or redraw /
trace them as vectors by hand), it cannot evaluate them at a higher resolution. The scripts multiply them with
blurred clouds (logo1, logo2) or use picture A as a mask (testshit).

## 8. Where each texture goes and who reads it

The pointer variables [0x654a..0x658e] (written by main 0x1875b..0x18827) form the **texture table of the vector
engine**: main sets [0x18d54] = 0x654a (0x18965), and the polygon fillers take a face's texture numbers from the
face bytes +0x0e and +0x0f (`[[0x18d54] + 4*n]`, 0x1e1d3 / 0x1e208 and 0x1ea95 / 0x1eaca) and patch them into
their inner loops (0x1e7fa.., 0x1e8fa.., 0x1f18c.., 0x1f2c0..). Byte +0x0e selects an **interleaved RGB** texture
(3 bytes per texel, read as `tex[3*(v*256+u) + 0/1/2]`), byte +0x0f a 256x256 byte map (always number 0 =
[0x654a], the light map built by 0x6592; core scope). That is why map4, map10 and map14 end with op 0x24 and why
the parts interleave map1, map2 and map8 in place before their 3D objects use them. The objects' texture numbers
are the engine / part scopes' business; the numbers seen at run time are listed below.

| n | texture | buffer var | final format | used by (code xrefs, and engine faces seen in a run from t440 to the end) |
|---|---|---|---|---|
| 0 | light map (0x6592, not a script) | [0x654a] | 256x256 bytes | every textured face's second texture (all 3D parts) |
| 1 | map1 | [0x654e] | planar | 0x17003 ribbons: `esi=[0x654e], ebx=[0x658a]` → 0x17762 (0x1709f). 0x169e7 tunnel copies **map5** over it (0x169f3) and interleaves it (0x16a06); from then on number 1 = map5: engine faces of 0x169e7 and of the metaballs 0x10fda |
| 2 | map2 | [0x6552] | planar | 0x15c8b tube: interleaved in place (0x15cb0), engine faces of 0x15c8b |
| 3 | map3 | [0x6556] | planar | 0x169e7 tunnel: 16-bit copy in B0 (0x16a20, 0x16a35); also in the block at 0x16e36 (dead, see below) |
| 4 | map4 | [0x655a] | interleaved | engine faces of 0x15d20 (spiky object) |
| 5 | map5 | [0x655e] | planar | 0x1212a "jizz" part: 16-bit copy in B0 (0x121d1); 0x169e7: copied over map1 (0x169f3); 0x177bf radial zoom: 16-bit copy in B0 (0x177e9) |
| 6 | weirdtxt | [0x6562] | 16-bit 256x256 | 0x17003 ribbons (0x17419, 0x1744b) |
| 7 | map7 | [0x6566] | planar | 0x177bf radial zoom: 16-bit copy in B1 (0x177fc) |
| 8 | map8 | [0x656a] | planar | 0x15d20: interleaved in place (0x15d54), engine faces of 0x15d20; 0x17003: `esi=ebx=[0x656a]` → 0x17762 (0x1705d) |
| 9 | map9 | [0x656e] | planar | nobody (no xref, never read in the run) |
| 10 | blob heights | [0x6572] | R height, G/B gradients | TBL helper 0x11e33 (0x11e3d): bump-mapped light, the G and B gradient planes offset the light map [0x654a] (320 columns per row read from 256-wide rows, `esi += 320 - 64`); also in the dead `mov eax` chain at 0x16e40 |
| 11 | testshit | [0x6576]; [0x662b] = G plane | planar 320x200 | TBL rays 0x11986 (0x11a07, 0x11b41, 0x11d12: G plane, 320 wide); the block at 0x17487 (dead) |
| 12 | logo1 | [0x657a] | 16-bit 320x200 | end loop 0x18c25 (0x18c32): copied to the page, 0x7d00 dwords |
| 13 | map10 | [0x657e] | interleaved | engine faces of 0x17003 (from tick 2224), 0x15c8b and 0x10fda |
| 14 | map14 | [0x6582] | interleaved | engine faces of 0x17003 (from tick 1365) and 0x15c8b |
| 15 | map15 | [0x6586] | planar + stripes | 0x17c8a field lines: 16-bit copy in B0 (0x17c99) |
| 16 | map1a | [0x658a] | planar | 0x17003 ribbons (0x170a5, with map1 → 0x17762) |
| 17 | logo2 | [0x658e] | 16-bit 320x200 | 0x1212a "jizz" part: copied to B4 (0x1215c), then marks black pixels next to non-black ones with 1 in place (0x1216f..0x121b9, an outline mask; parts2d scope), and 0x1239b |

Only map9 is built and never used; map14 is built twice for nothing. Writes to the texture buffers after the
precalc (run from t440 to the end with write hooks on 0x230000..0x55ffff): map1 by 0x169e7 (tick 2511, the map5
copy + interleave), map2 by 0x15c8b (3281, interleave), logo2 by 0x1212a (4048, outline marks), map8 by 0x15d20
(4431, interleave); nothing else. So a remake can treat every texture as fixed after the precalc, plus these
four conversions.

The blocks at 0x16e36 (map3 planes into 0x16ee6/0x16ef3/0x16f00, after a dead chain of `mov eax` loads of [0x6572],
[0x654a]) and 0x17487 (testshit R plane as a threshold mask) have no direct call or jump, and the capture's
per-tick EIP samples never fall into them (0x16dc8 next to them: 85 samples): dead code, most likely.

The work buffers B0..B4 are reused by the parts as scratch (16-bit copies); every xref site writes them before
reading them, so the scripts' leftovers in B0..B4 are not used.

## 9. Higher resolution: what can be evaluated exactly, and how

A remake should keep the 256x256 byte-exact generator (it is cheap and it is the reference: replaying the random
generator needs the same call order) and add a float path at N x N (N = 256 * scale) where each op is evaluated
in texture coordinates u = x/256 (period 1, everything tiles). Per op:

| op | at N x N | notes |
|---|---|---|
| fill, add, scale, mul, mix, mixmask, addsat, addbuf, invert, copies, matrix, HSV↔RGB | per pixel, in float (v/255) | point ops, resolution independent. Drop the integer truncations; keep the modulo-256 wrap of add/addbuf/cone where it happens (signed plasma bytes + 128; testshit builds its window masks from it). The dword-add carry into the right-hand neighbour is a packed-add artefact (±1, at most a few warped texels change), except in testshit, see below |
| noise (0x03) | generate the 256² bytes by the generator, interpolate | it is always blurred next (blur2+blur4(+blur4)): evaluate the blurred field at 256² and upsample bicubically, or treat the samples as impulses and apply the blur kernel at N² |
| clouds (0x1d) | generate the 256² field exactly, interpolate | random midpoint displacement; every use is followed by blurs (>= 2 x blur2). Continuing the subdivision below one texel would add detail that is not in the original. Keep the two zero points (128,0), (0,128) |
| blobs (0x0a) | anti-aliased discs, radius 4/256, centre ((x0+2)/256, (y0+4)/256), drawn in order | positions and values from the generator; later blobs overwrite earlier ones |
| box blur s | box filter of width s/256 (or a Gaussian of variance s²/12 texels²) centred 0.5 texel up-left | keep the +0.5 texel shift per blur: it moves content |
| sines (0x14) | analytic formula of 4.9 at continuous (u, v) | exact; the result is a signed value, wrap/offset as the script does |
| cone (0x1c) | `255*(r - d)/r` with d the torus distance to texel (0,0) in texel units | exact |
| angle (0x1f) | `atan2(y, x)/2pi*256` mod 256 around texel (0,0) | exact; keep the seam at x/y = -128 (it is in the original) |
| curve (0x16/0x17) | thick stroke: the Minkowski sum of the continuous Hermite curve (polar, 4.13) with an axis-aligned square of side `size` texels, offset by -size/2 | samples are dense (spacing < 2 texels vs. pen >= 6), so a continuous stroke is what is visible; anti-alias the edges |
| shift (0x0b) | translation by (dx, dy)/256 | exact |
| addxy + warp (0x1b, 0x15) | continuous coordinates: `in(u + U(u,v), v + V(u,v))` with a filtered (bicubic) read | the original reads nearest-neighbour at integer offsets (U, V quantised to whole texels): the main 8-bit artefact |
| emboss (0x12) | gradient of the continuous source (forward differences are derivatives at +0.5 texel) times the light vector, `clamp(s/4 * (...), 0, 255)` | per texel units: dx = ∂f/∂x * 1 texel |
| rle4 pictures | true bitmaps (256x200, 16 levels): scale up with a good filter or redraw | section 7 |
| map15 stripes | `(150 + 100 sin(2pi * 9 * v)) / 256`, v = y/256 | exact (9 periods over the texture) |
| blob gradients (0x12e08) | analytic gradient of the (smooth) height field; the G plane is stored one texel left | central differences of the original |
| tile320 / to16bit / interleave | format changes only | a remake samples the texture with wrap instead (tile320 = a 320x200 view of the texture, x mod 256) |

The add carry matters once: testshit makes a window mask with `addxy; scale R 783; add R 1; scale R 10270` (x*783/256
clamped to 255, +1, then x40): the add wraps 255 to 0 outside the window, but in each dword the carry turns the
next three 255s into 1, which the last scale makes 40. So outside the window 3 of 4 columns keep 16 % of the
picture: in testshit.tga this is the faint "9" left of "TBL" and the start of the apostrophe right of it (gone
when the adds are done per byte: 1633 pixels differ by up to 46). For such mask textures the simplest faithful
route is the exact 256x256 result, filtered up. In the other textures the carry changes values by ±1 before
warps (map14: 32605 pixels differ by up to 64 after the warps, map2: 43054 by up to 28: warped detail moves by a texel
here and there).

Measured with the remake's float path (`jizz/remake/src/jizz/texgen_hires.cpp`, 1024x1024, checked with
`tools/texdump`): dropping the integer truncations outright moves every warp by about half a texel against the
original, because the displacement maps come out of `scale` (`>> 8`, a floor). Replacing each truncation by its mean
(a floor averages x - 1/2; a sum divided by n in the box blurs: x - (n-1)/(2n)) keeps the textures in register: the
area-downsampled mean |difference| to the exact textures drops from at most 6.4 to at most 3.0 (map10 6.4 → 3.0, map14
5.5 → 2.1, map4 4.8 → 2.8). The remaining differences are blob outlines (round discs against the 8x8 shapes, where
5120 random greys overlap), the warps' nearest-texel reads, and the dword carries of map2 / map14, which the float
path leaves out except in testshit (there they make the faint "9", which it keeps).

## 10. Unused code and data

- Ops 0x0d, 0x16 (same handler as 0x17), 0x1e, 0x25 are never used by the 17 scripts (verified with test_ops.py).
- 0x13f0a: a polar-to-cartesian remap (`P[r][a] = T[(r*sin)][(r*cos)]` with fsin/fcos of a*2pi/256), not in the
  jump table and not called.
- Scripts at 0x12c32 and 0x12c3f (section 5), never run.
- The strings "you think THIS is boring??", "wait till you see the intro!", "almost done" (0x184c7..) are not
  referenced (NOTES.md).

## 11. Models and checks (`jizz/re/models/textures/`)

- `texgen.py`: the generator. `python3 models/textures/texgen.py` builds everything (8 s) and compares the 16 TGA
  files with `dump/*.tga` and the 17 final buffers with `snaps/t440.snap`: all byte-identical.
- `scripts.py`: lists the scripts op by op (addresses, bytes, decoded arguments).
- `emu.py`: runs the original 0x12e72 on one script in unicorn (memory from t430.snap, progress print patched out,
  fninit + precision control 24 bit as main does) and records B0..B3 before every op.
- `test_ops.py`: compares texgen.py with emu.py on a made-up script that covers the unused ops and argument ranges;
  `all` also compares the 17 scripts op by op (they already match through texgen.py).
- Run-time accesses (section 8): two runs of the intro from t440 to the end with a scratch copy of run_jizz.py: one
  with memory hooks (writes to all texture buffers, reads of map4/map9/map10/map14), one with code hooks on the
  engine's texture-table lookups (0x1e1e0, 0x1e215, 0x1eaa2, 0x1ead7) logging the texture number per part.
- The dword-add carry effect (section 9) was measured by running texgen.py with per-byte adds.

## 12. Open points

- fsin/fcos/fpatan: the model (and the dump, made in unicorn) uses the C library's double results; a real x87
  returns slightly different low bits, which can flip a few rounded pixels of the sines, curves, emboss and
  angle ops. Not checked against a real machine (not needed for the remake).
- The blocks at 0x16e36 and 0x17487 are taken as dead code from the missing references and the EIP samples (not
  from a code hook).
- Which object faces carry which texture numbers (bytes +0x0e/+0x0f) is left to the engine and part scopes.
- "JacePeG": interpretation only (section 7).
