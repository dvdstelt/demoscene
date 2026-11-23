# Scope "sprites + shadow scene": the four photographs over a warp grid or in a lit room

Init 0x404fe8, 0x40506c, 0x405110, 0x4051d4 (the four pictures), 0x405298 (the scene); script routines 0x4057bc,
0x405ba0, 0x40585c, 0x40571c (picture changes), 0x404680 (warp parameters), 0x405f00 (camera drift), 0x404728
(off); tick routine 0x404738, frame routine 0x4047a0; the part's own rasterizer 0x4172e0 (two-texture blend).
Addresses are linear (NOTES.md). Positions are order:row in decimal. **N** = the record of `full.frames` (screen
refresh) that shows a frame; **M** = [0x4248c4], the music clock of notes-core.md (+1 per timer callback, 4 per
refresh; M_ret(k) = 4k − 151 is the retrace callback of record k for k ≥ 3045).

The SurRender calls are described by what goes in and what comes out (notes-engine.md has the engine itself); the
transform, projection, point light and the texel / level conventions the part depends on were checked here
against the emulator.

Checked against the emulator (scripts in `models/sprites/`, README there): runs resumed from snaps/f4255 and
snaps/f9790 (reproducing `full.frames` exactly) with a dump of every drawn frame (616 frames covering all four
pictures), and the models give

- the warp grid's 4 × 64 texture coordinate words exactly (every frame but the first of the part, which the tick
  changed while it was drawn), the screen positions of its vertices to 1/16 pixel (the engine's truncation), and
  the background picture with 85..88 % of the pixels identical, the rest one texel off at texel boundaries;
- the box's per-vertex light levels exactly (all 376 frames of pictures 3 / 4 but the first of a picture), its
  vertices to 1/16 px (more for vertices next to the near plane), the background picture with 55..83 % of the
  pixels identical (texel / light-level boundaries of the affine fixed-point rasterizers; visually the same);
- the light position, intensity and shadow corner from T = 4 N − 155 exactly; the camera drift, the rotation
  counts and the phase resets to the refresh.

---------------------------------------------------------------------------------------------------------------

## 1. What is on screen, and when

Four digitised photographs of people (the "sprites"), each drawn unscaled over a full-screen 3D background. The
part never clears the screen: the background covers it (section 7).

| picture [0x4227fc] | files | records (music) | background | photo, position |
|---|---|---|---|---|
| 2 | nigga/sprite2.raw, nigga2.raw / .shd / .pal | 4256..4611 (11:63..13:00) | **warp**: the swirl texture nigga2.raw read twice through a slowly turning 8 × 8 grid of warped texture coordinates, the two reads blended 50 / 50 (magenta / purple / red swirls) | a man in white reaching his open hand towards the camera, 172 × 200 at (0, 0) |
| 4 | nigga/sprite4.raw, nigga4.raw / .qnt / .pal, shadow4.raw / .sdw | 4612..4962 (13:00..14:00) | **room**: the inside of a box (poksi.sro) turning about the vertical axis, every wall cell showing nigga4.raw (pink / yellow swirls; the two triangles of a cell mirrored against each other), lit by a moving point light; the man's shadow on the floor; from 13:32 the camera rises out of the room | a man with an afro and white sunglasses in a white T-shirt, 170 × 168 at (94, 28) |
| 3 | nigga/sprite9.raw, nigga9.raw / .qnt / .pal, shadow9.raw / .sdw | 9791..10360 (27:48..29:24) | **room** with nigga9.raw (yellow swirls with two elephants), the man's shadow; from 29:00 the camera rises | a man lying on his side in an orange shirt and sunglasses, 274 × 147 at (46, 53) (bottom right) |
| 1 | nigga/sprite1.raw, nigga1.raw / .shd / .pal | 10361..10574 (29:24..29:63) | **warp** with nigga1.raw (yellow / orange / red swirls) | two men, one in a patterned hat looking up, one in a green jacket, 164 × 156 at (0, 44) (bottom left) |

Events (notes-core.md 1.4 has when each ran; "first record" = the first record whose frame shows the change):

| entry | pos | routine | first record | what |
|---|---|---|---|---|
| 27 | 11:63 | 0x4057bc | 4256 | picture 2, warp; tick 0x404738, frame 0x4047a0; palette nigga2.pal |
| 28 | 12:32 | 0x404680 | 4436 | warp parameters: the second set, phases 0 (a jump of the swirls, under the flash of entry 29) |
| 31 | 13:00 | 0x405ba0 | 4612 | picture 4, room; the turning step becomes −0.01; palette nigga4.pal |
| 32 | 13:32 | 0x405f00 | 4787 | camera drift on (30 units per frame) |
| 34 | 14:00 | 0x404728 | 4963 | routines off |
| 75 | 27:48 | 0x40585c | 9791 | picture 3, room; turning step +0.01; palette nigga9.pal |
| 77 | 29:00 | 0x405f00 | 10229 | camera drift on |
| 79 | 29:24 | 0x40571c | 10361 | picture 1, warp; palette nigga1.pal |
| 81 | 29:48 | 0x404680 | 10493 | warp parameters again (same values), phases 0 |
| 82 | 29:63 | 0x404728 | 10575 | routines off |

The flashes and fades around them (0x400498, 0x400438) are separate script entries (notes-core.md). Entries 27
and 75 run while the previous part's frame routine is active (it does not wait for the retrace), 3 and 1 callbacks
before a retrace: the first photo frame misses that retrace, so records 4255 and 9790 still show the previous part's
last frame (under the white flash) and the photos appear at 4256 and 9791. The other entries run right after the
retrace callback the part's frame routine waited for (M_entry = M_ret(first record − 1)).

## 2. Data

### 2.1 INSIDE.DAT (offset in the unpacked block, size)

| file | offset | size | format | what it is |
|---|---|---|---|---|
| nigga/nigga1.pal, nigga2.pal, nigga9.pal, nigga4.pal | 0x192967, 0x1b9057, 0x1e19b7, 0x204a0d | 768 | 6-bit RGB | the pictures' palettes; init sets entry 0 of nigga2 / nigga9 / nigga4 to (0, 0, 0) (it is (0,0,63), (0,17,63), (5,0,0) in the file; nigga1's is already black) |
| nigga/nigga1.raw, nigga2.raw, nigga9.raw, nigga4.raw | 0x192c67, 0x1b9357, 0x1e1cb7, 0x204d0d | 65,536 | 256 × 256, 8 bit | background textures (2.3) |
| nigga/nigga1.shd, nigga2.shd | 0x1a2c67, 0x1c9357 | 65,536 | 256 × 256 table | 50 % blend of two colours (9.1) |
| nigga/nigga9.qnt, nigga4.qnt | 0x1f1cb7, 0x214d0d | 16,384 | 64 × 256 table | light level × colour (9.2) |
| nigga/shadow9.raw | 0x1ffa0d | 16,384 | 128 × 128, values 0..15 | shadow density of picture 3 |
| nigga/shadow4.raw | 0x21fc9d | 65,536 | 256 × 256, values 0..15 | shadow density of picture 4 |
| nigga/shadow9.sdw, shadow4.sdw | 0x203a0d, 0x22fc9d | 4,096 | 16 × 256 table | shadow level × colour (9.3) |
| nigga/sprite1.raw | 0x1b2c67 | 25,584 | 164 × 156, no header | photo, picture 1 |
| nigga/sprite2.raw | 0x1d9357 | 34,400 | 172 × 200 | photo, picture 2 |
| nigga/sprite9.raw | 0x1f5cb7 | 40,278 | 274 × 147 | photo, picture 3 |
| nigga/sprite4.raw | 0x218d0d | 28,560 | 170 × 168 | photo, picture 4 |
| nigga/poksi.sro | 0x296b90 | 2,744 | SurRender object: dword +8 = 98 vertices (12 bytes each from +0x20), +0xc = 1208 (face offset), +0x10 = 192 faces (8 bytes each) | the room: a box x −2688..2696, y −3208..3400, z −2024..4088, each side split 4 × 4 (vertex rows at x −2688, −1344, 0, 1344, 2696; y −3208, −1552, 96, 1752, 3400; z −2024, −488, 1032, 2560, 4088), 2 triangles per cell, normals pointing inwards (as the engine loads them: length 819, averaged at edges (579, 579) and corners (546, 273, 546)) |

The sprite sizes are code constants (EDX = pitch = EBX = width, ECX = height in the init routines). The file names
are the strings at 0x420cac..0x420f18.

### 2.2 Constants in the program

| what | where |
|---|---|
| warp parameters, 15 floats (phase, speed, amplitude, i-factor, j-factor for A, B, C) | 0x422810..0x42284b, initial values in the image: A (0, 0.07, 425, 0.3, 0.2), B (0, 0.05, 130, 0.4, 0.6), C (0, −0.06, 250, 0.7, 0.5); the second set (immediates in 0x404680): A (0, −0.07, −435, −0.3, −0.2), B (0, −0.08, −150, −0.4, −0.6), C (0, −0.06, −250, −0.7, −0.5) |
| camera angles of pictures 3 / 4 (floats, passed as doubles) | 0x422804..0x42280c: image and 0x40585c (−0.37, 0.51, 0.16); 0x405ba0 writes (−0.89, 0.61, 1.08) (0xbf63d70a, 0x3f1c28f6, 0x3f8a3d71) |
| light and shadow-corner frequencies and amplitudes (doubles) | 0x420c1c 0.007656967840735069, 0x420c24 200, 0x420c2c 0.004805382027871216, 0x420c34 −750, 0x420c3c 0.0056625141562853904, 0x420c44 500, 0x420c4c 0.0088261253309797, 0x420c54 0.0033255736614566014, 0x420c5c 400, 0x420c64 1200, 0x420c6c 1750, 0x420c74 0.00508646998982706, 0x420c7c 0.006648936170212766, 0x420c84 0.005537098560354375, 0x420c8c 512, 0x420c94 2048, 0x420c9c 1024, 0x420ca4 0.0061236987140232705 |
| everything else (grid spacing, positions, UV corners, sprite positions, 30 / 15 camera steps, 0.01 rad turning step) | immediates in the routines, quoted below |

### 2.3 What the pictures are

- **sprite1/2/9/4.raw are digitised photographs of real people** (cut out on index 0). They were first kept as
  they are; at the maintainer's request (23 November 2026) the remake shows high-resolution redraws
  (`remake/art/spriteN.png`), and with `--original-art` the original pixels, filtered, with index 0 as transparent.
- **nigga1/2/4/9.raw** are hand-made swirl textures (a paint program's smudge / twirl look; nigga9.raw also holds
  two cartoon elephants like a fabric print). No people. They are read through palette-index tables (the .shd
  blend, the .qnt light), so they stay as they are (no redraw): the remake samples them filtered and does the
  table arithmetic in RGB (section 9). The warp reads nigga1 / 2 beyond 0..255 (wrapping); the room shows nigga9 / 4
  exactly once per wall cell.
- **shadow4/9.raw** are soft 16-level silhouettes of the photographed people (blurred masks, value 0 outside):
  derived from the photos, used as a density map. Keep, filter (they are already blurred).

### 2.4 Variables (BSS unless noted)

| address | what |
|---|---|
| [0x4227fc] (data) | picture index k = 1..4 (0 in the image) |
| [0x422800] (data) | camera step per frame: 0, or 30 after 0x405f00 |
| [0x425f20 + 4k] | SurRender bitmap of sprite k (0x415e50; +0 type 3, +8 pitch, +0xc width, +0x10 height, +0x18 pixels, +0x24 transparent index 0) |
| [0x425f38..0x425f44] | sprite1 / 2 / 9 / 4 pixels |
| [0x425f48 + 4k], [0x425f5c + 4k] | .sdw table, shadow texture of picture k (0 for k = 1, 2) |
| [0x425f70 + 4k], [0x425f88 + 4k], [0x425f9c + 4k] | palette, background texture, .shd / .qnt table of picture k |
| [0x425f84] | memory tag of the scene |
| [0x425fb0] poksi.sro, [0x425fb4] "grid-sro" (0x10000 bytes), [0x425fb8] "shadowsquare" (0x2000) | object sources |
| [0x425fc4] scene (0x50), [0x425fc0] camera (0xc8), [0x425fd0] light (0xd4) | |
| [0x425fd4] "material", [0x425fdc] "material2", [0x425fbc] "shadowmap" (0x44 each) | materials |
| [0x425fd8] warp grid, [0x425fcc] box ("kuubio"), [0x425fc8] shadow quad ("shadepoly") | objects |
| [0x425ed8] | the turning step: 3 × 3 doubles, Ry(±0.01) |

## 3. Initialisation

### 3.1 The pictures: 0x404fe8 (1), 0x40506c (2), 0x405110 (3), 0x4051d4 (4)

```c
// k = 1, 2, 3 (files nigga9 / sprite9 / shadow9), 4 (nigga4 / sprite4 / shadow4)
select_memory_tag([0x425f84]);                       // 0x40b4d0; still 0 here (0x405298 runs later)
pal[k] = file("nigga/niggaK.pal");  tex[k] = file("nigga/niggaK.raw");
tab[k] = file(k <= 2 ? "nigga/niggaK.shd" : "nigga/niggaK.qnt");
spr = file("nigga/spriteK.raw");
if (k >= 3) { shadow[k] = file("nigga/shadowK.raw"); sdw[k] = file("nigga/shadowK.sdw"); }
if (k != 1) pal[k][0] = pal[k][1] = pal[k][2] = 0;   // palette entry 0 = black
bitmap[k] = make_bitmap(spr, pitch = width, width, height, "nigga spriteK");   // 0x415e50
bitmap[k]->type = 3;                                 // drawn by 0x4164ec: unscaled, index 0 transparent
```

### 3.2 The scene: 0x405298

```c
tag = new_memory_tag(); select_memory_tag(tag);              // [0x425f84]
engine_reset_clip_buffers();                                 // 0x40c034 (engine)
allocate scene, camera, material, material2, shadowmap, light, grid-sro, shadowsquare (2.4)
make_grid_sro(grid_sro, 7, 7, 200, 200);         // 0x410f34: 8 x 8 vertices (x, 0, z) = (-700 + 200 i, 0, -700 + 200 j),
                                                 // vertex j * 8 + i; per cell 2 triangles
                                                 // (a, a + 1, a + 8) and (a + 8, a + 1, a + 9), a = j * 8 + i
make_grid_sro(square_sro, 1, 1, 1400, 800);      // 4 vertices (+-700, 0, +-400), triangles (0, 1, 2), (2, 1, 3)
warpgrid = alloc(0x10c + 64 * 64 + 52 * 98); shadepoly = alloc(0x10c + 64 * 4 + 52 * 2);
init_object(camera);                                         // 0x410520: zero, orientation identity
init_scene(scene);                                           // 0x410878 (notes-engine.md)
set_viewport(scene, 320, 200, window (0, 0)..(320, 200));    // 0x410618
scene->camera = camera; scene->near = 0x2000;                // +0x38, +0x44 (z * 128: 64 units)
set_position(camera, 0, 0, -1300); set_rotation(camera, 0, 0, 0); add(scene, camera);
init_object(warpgrid);                                       // 0x4108a8
load_object(warpgrid, grid_sro, material);                   // 0x410bb0
set_rasterizer(warpgrid, 0x4172e0); add(scene, warpgrid);
set_rotation(warpgrid, 1.5, 3.0, 0);                         // orientation Rx(1.5) Ry(3.0)
init_object(shadepoly); load_object(shadepoly, square_sro, light_material = shadowmap);
set_rasterizer(shadepoly, 0x41669c); add(scene, shadepoly);
set_rotation(shadepoly, 0, 0, 0); set_position(shadepoly, 300, 3000, 600);
shadepoly->flags (+0xc4) = 0x100;                            // no back-face culling
shadepoly->sort_bias (+0xcc) = 666666;                       // 0xa2c2a: drawn as if 5208 units nearer (7.2)
load_object(kuubio, file("nigga/poksi.sro"), material);      // the room
add(scene, kuubio); set_rasterizer(kuubio, 0x412db0);        // replaced by 0x414e5c before it is ever drawn
kuubio->flags |= 0x40;                                       // lit by the scene's lights
for (f = 0; f < kuubio->faces; f += 2) kuubio->face[f].material = material2;   // even faces
init_object(light); light->range (+0xc8) = 5000; light->intensity (+0xcc) = 1600;
light->flags |= 0x80;                                        // a point light
add(scene, light);
shadowmap: +0xc = +0x18 = 0 (its masks and sizes are set by 0x40585c / 0x405ba0)
material, material2: +0xc = +0x18 = 0; +0x38 = +0x3c = 0xff (u, v masks), +0x40 = +0x41 = 8 (log2 size)
turn = rotation_matrix(0, 0.01, 0);                          // 0x4145f4 into [0x425ed8]: Ry(0.01)
```

The scene's object list (0x411154 inserts at the head): light, box, shadow quad, warp grid, camera.

Object fields the part touches: +0x10/+0x14/+0x18 position (int), +0x28 orientation (3 × 3 doubles, row-major),
+0xc4 flags (bit 0 hidden, 0x40 lit, 0x80 light, 0x100 two-sided), +0xc8 / +0xcc (light: range / intensity; other
objects: +0xcc depth-sort bias), +0xd0 vertex array (0x40 bytes per vertex: +8/+0xc/+0x10 x, y, z (int), +0x14
/ +0x18 screen x, y (28.4), +0x1c camera z × 128, +0x28/+0x2a/+0x2c/+0x2e texture coordinate words, +0x30 light
level word, +0x34..+0x3c normal), +0xd4 vertex count, +0xd8 face array (0x34 bytes: +8 rasterizer, +0x10
material, +0x14/+0x18/+0x1c vertices, +0x20..+0x28 normal, +0x30 flags), +0xdc face count. Material fields: +0 / +4
textures 1 / 2, +8 table, +0x18 bit 0: the corner texture coordinates come from the material (+0x20, +0x24 for the
face's 1st vertex, +0x28, +0x2c 2nd, +0x30, +0x34 3rd; the rasterizer setup 0x41be3c writes them into the
vertices), +0x38 / +0x3c u / v masks, +0x40 / +0x41 log2 width / height.

**Rotation convention** (checked against every dumped matrix): set_rotation(o, a, b, c) and rotation_matrix give
R = Rx(a) Ry(b) Rz(c) with Rx(a) = [[1,0,0],[0,cos a,−sin a],[0,sin a,cos a]], Ry(b) = [[cos b,0,sin b],[0,1,0],
[−sin b,0,cos b]], Rz(c) = [[cos c,−sin c,0],[sin c,cos c,0],[0,0,1]]; world = R · local + position; camera space =
Rcam^T · (world − camera position).

## 4. Script routines

### 4.1 Warp pictures: 0x4057bc (k = 2) and 0x40571c (k = 1)

```c
select_memory_tag([0x425f84]);
[0x4227fc] = k;  [0x422800] = 0;                   // picture, camera step
material->fixed_uv (+0x18) = 0;  material2->fixed_uv = 0;    // the grid's own vertex coordinates
set_position(camera, 0, 0, -1300);  set_rotation(camera, 0, 0, 0);
scene->focal (+0x3c) = 0x100;
set_tick(0x404738);  set_frame(0x4047a0);          // both reset P
set_palette(pal[k]);                               // 0x400364
```

Nothing else is reset: the warp phases, the grid's orientation and (for k = 1) the scene's ambient 0x70 and near
plane 0x1600 left by 0x40585c carry on (the latter two do nothing to the unlit grid).

### 4.2 Room pictures: 0x40585c (k = 3) and 0x405ba0 (k = 4)

```c
select_memory_tag([0x425f84]);
[0x4227fc] = k;  [0x422800] = 0;
// the shadow quad's 4 vertices (local x, y, z; texture words u, v):
k == 3: v0 (0, 0, 0) uv (0xff0, 0);      v1 (2048, 0, 0) uv (0, 0);
        v2 (400, -2400, 0) uv (0xff0, 0xff0);  v3 (1948, -1600, 0) uv (0, 0xff0)
k == 4: v0 (900, 700, 500) uv (0xff0, 0); v1 (-750, 700, 500) uv (0, 0);
        v2 (900, 700, -700) uv (0xff0, 0xff0); v3 (-750, 700, -700) uv (0, 0xff0)
shadowmap: k == 3: masks 0x7f, 0x7f, log2 7, 7 (shadow9.raw 128 x 128); k == 4: 0xff, 0xff, 8, 8
material->fixed_uv = 1;  material2->fixed_uv = 1;   // every box triangle gets the corners below
k == 3: material  corners (0, 0), (0, 0xff0), (0xff0, 0);  material2 (0, 0xff0), (0xff0, 0xff0), (0xff0, 0)
k == 4: material  corners (0, 0), (0xff0, 0), (0, 0xff0);  material2 (0, 0xff0), (0xff0, 0xff0), (0xff0, 0)
k == 3: set_rotation(shadepoly, 1.5, 3.0, 0);   k == 4: set_rotation(shadepoly, 0, 1.0, 0)
scene->ambient (+0x40) = 0x70;  scene->near (+0x44) = 0x1600;      // level 7.0; 44 units
k == 3: set_position(camera, -1600, 2000, -2200); camera angles [0x422804..] = (-0.37, 0.51, 0.16)
k == 4: set_position(camera, -550, 2000, -500);   camera angles = (-0.89, 0.61, 1.08)
set_rotation(camera, angles);
set_tick(0x404738);  set_frame(0x4047a0);  set_palette(pal[k]);
set_rotation(kuubio, 0, 0, 0);  set_rasterizer(kuubio, 0x414e5c);  // textured, Gouraud through the .qnt
turn = rotation_matrix(0, k == 3 ? 0.01 : -0.01, 0);
```

Texture words are 12.4 fixed point texels, 0xff0 = texel 255 (the whole texture whatever its size, 7.4).

Where the shadow quad lies (world = position (300, 3000, 600) + R · local): for picture 3, Rx(1.5) Ry(3.0) lays it
almost flat at y ≈ 2830..3290, 100..570 units above the floor (y = 3400); for picture 4, Ry(1.0) keeps it flat at
y = 3700, 300 units below the floor, with the moving corner v1 at y = 3500..4500. Either way it is drawn over the
floor as a decal (7.2), so only its projection matters.

The box's triangles come in pairs per wall cell: face 2n (material2) and face 2n + 1 (material) of poksi.sro in
file order, each gets its material's three corners in its own vertex order. For picture 3 the two triangles of a
cell agree on their shared vertices (one copy of the texture per cell). For picture 4 they do not: on the shared
diagonal one triangle has (255, 0) → (0, 255) and the other (0, 255) → (255, 0), so the second half of each cell
is the texture mirrored about its diagonal u = v: every cell shows a seam along its diagonal (the kaleidoscope look
of picture 4's room). This is content, not an artefact: keep the per-triangle corners.

### 4.3 0x404680: warp parameters (12:32 and 29:48)

Writes the second set (2.2) into 0x422810..0x42284b: phases A = B = C = 0, speeds, amplitudes and i / j factors
negative. With n ticks since the reset, a = −(0.3 i + 0.2 j + 0.07 n), b = −(0.4 i + 0.6 j + 0.08 n), c = −(0.7 i +
0.5 j + 0.06 n), and since sin(−x) = −sin x, cos(−x) = cos x the formulas of 6.2 become the first set's with the
amplitudes 435 / 150 / 250 (were 425 / 130 / 250), B's speed 0.08 (was 0.05), C's phase advancing (+0.06 per tick,
was −0.06), the sine terms unchanged in sign and every cosine term negated. So the swirls jump at the reset. At
29:48 the values are the same as already set; only the phases are reset.

### 4.4 0x405f00: camera drift (13:32, 29:00)

`[0x422800] = 30`. Each drawn frame then moves the camera (6.1).

### 4.5 0x404728: off (14:00, 29:63)

Tick and frame routine 0. Nothing is freed.

## 5. The tick routine 0x404738 (every refresh, in the retrace callback)

```c
A += sA;  B += sB;  C += sC;                       // float32 sums: 0x422810 += 0x422814, 0x422824 += 0x422828,
                                                   // 0x422838 += 0x42283c
warpgrid->orientation = warpgrid->orientation * turn;   // 0x414070, doubles
kuubio->orientation   = kuubio->orientation * turn;
```

With turn = Ry(s · 0.01), s = ±1: the grid's orientation is Rx(1.5) Ry(3.0 + 0.01 m) and the box's Ry(0.01 m'),
m, m' the signed tick counts (8.2): the grid turns in its own plane, the box about the vertical axis through the
origin (not its centre, which is at (4, 96, 1032)).

## 6. The frame routine 0x4047a0

### 6.1 Common start

```c
scene->buffer (+8) = [0x4248d0];                   // the back buffer = the draw target
material->tex1 = material->tex2 = tex[k];  material->table = tab[k];
material2->tex1 = material2->tex2 = tex[k]; material2->table = tab[k];
shadowmap->tex1 = shadow[k];  shadowmap->table = sdw[k];
camera->y -= [0x422800];                           // drift: 30 per drawn frame (camera rises)
if (k != 3) camera->x -= [0x422800] / 2;           // 15 (picture 4; for k = 1, 2 the step is always 0)
```

### 6.2 Warp pictures (k = 1, 2)

```c
warpgrid->hidden = 0;  kuubio->hidden = 1;
for (j = 0; j < 8; j++) for (i = 0; i < 8; i++) {
    v = &warpgrid->vertex[j * 8 + i];
    a = iA * i + A + jA * j;   b = iB * i + B + jB * j;   c = iC * i + C + jC * j;    // floats, x87
    v->u1   (+0x28) = trunc(512 i + kA sin a + kB sin b + kC sin c);
    v->v1   (+0x2a) = trunc(512 j + kA cos a + kB cos b + kC cos c);
    v->row2 (+0x2e) = trunc(512 i + kC cos a + kB cos b + kA sin c);
    v->col2 (+0x2c) = trunc(512 j + kC sin a + kB cos b + kA sin c);
    v->x = -1400 + 350 i;  v->y = 0;  v->z = -1400 + 350 j;
}
render(scene);                                     // 0x40c6ac
draw_bitmap(bitmap[k], x = 0, y = 200 - height);   // 0x415df0: (0, 0) for picture 2, (0, 44) for picture 1
```

(A, sA, kA, iA, jA) = the floats at 0x422810.., (B, ..) at 0x422824.., (C, ..) at 0x422838.. (2.2). Words are
12.4 fixed point texels (truncated toward zero, stored as 16 bits). The grid is 2450 × 2450 units (centred on
(−175, −175), not on its origin) in the object's x-z plane; Rx(1.5) stands it up facing the camera, tilted 4.05°
(its normal (0, cos 1.5, sin 1.5)), at distance 1300: one grid cell is about 69 pixels wide, 512 words = 32
texels per cell, so the grid spans 224 × 224 texels (7/8 of the texture in each direction) plus the warp of up to
±805 / 16 = ±50 texels.

### 6.3 Room pictures (k = 3, 4)

```c
kuubio->hidden = 0;  warpgrid->hidden = 1;
T = M;                                             // [0x4248c4], read once per variable (same value in practice)
if (k == 3) {
    shadepoly->v1.x = trunc(2048 - 512 cos(T * 0.005537098560354375) sin(T * 0.004805382027871216));
    shadepoly->v1.y = trunc(1024 cos(T * 0.0056625141562853904) sin(T * 0.0061236987140232705));
    light->intensity = trunc(400 sin(T * 0.0033255736614566014) + 1200);
    light->x = trunc(1750 cos(T * 0.005537098560354375) sin(T * 0.004805382027871216));
    light->y = trunc(1750 cos(T * 0.0056625141562853904) sin(T * 0.0061236987140232705));
} else {
    shadepoly->v1.x = trunc(200 cos(T * 0.007656967840735069) sin(T * 0.004805382027871216) - 750);
    shadepoly->v1.y = trunc(500 cos(T * 0.0056625141562853904) sin(T * 0.0088261253309797) + 500);
    light->intensity = trunc(400 sin(T * 0.0033255736614566014) + 1200);
    light->x = trunc(1750 cos(T * 0.007656967840735069) sin(T * 0.004805382027871216));
    light->y = trunc(1750 cos(T * 0.0056625141562853904) sin(T * 0.0088261253309797));
}
light->z = trunc(1750 cos(T * 0.00508646998982706) sin(T * 0.006648936170212766));
// dead code: the hidden warp grid is re-laid as a flat 700-unit grid with words (512 i, 512 j)
render(scene);
draw_bitmap(bitmap[3], 320 - 274, 200 - 147) = (46, 53)   or   draw_bitmap(bitmap[4], 94.0, 28.0)
```

The light orbits inside the room (|x|, |y|, |z| ≤ 1750 around the origin) and pulses 800..1600 (period
2π / 0.0033256 = 1889 callbacks = 6.74 s). The shadow quad's corner v1 follows the light's x / y products (k = 3:
v1.x = 2048 − 512 / 1750 · light.x, v1.y = 1024 / 1750 · light.y; k = 4: v1.x = −750 + 200 / 1750 · light.x, v1.y
(the quad's height there) = 500 + 500 / 1750 · light.y): the shadow stretches and swings with the light (a fake: it
is not projected from the light).

### 6.4 End of the frame

```c
debug_text();                                      // 0x400bec, off
flip();                                            // 0x400b14
while ([0x422984]) ;                               // wait until the retrace callback has copied the frame
```

So the part draws at most one frame per refresh; in the capture exactly one (706 frames in 707 refreshes, 783 in
784).

## 7. The engine as this part uses it (in → out)

### 7.1 Transform and projection

camera space p = Rcam^T (R · v + position − camera position) (section 3, verified); screen x = 160 + 256 p.x / p.z,
y = 100 + 256 · 5/6 · p.y / p.z (scene +0x18 = 0x8000, +0x1c = 0x6aaa = 0x8000 · 5/6, both / 128; the 5/6 makes
the 320 × 200 picture square on a 4:3 screen: field of view 64.0° × 50.2°); positions in 1/16 pixel (truncated);
+y is down on screen. Near plane z = scene +0x44 / 128 (64 units for the warp pictures, 44 for the rooms): faces
crossing it are clipped (0x41c0e0) and drawn in pieces.

### 7.2 Culling, sorting, coverage

Back faces (normal · (camera − vertex) < 0) are skipped, except on the two-sided shadow quad. Faces are drawn back
to front by a 3-pass radix sort (largest key first) on max(0, trunc((trunc((z0 + z1 + z2) / 3) − bias) / 16))
(0x41a9d4; z = camera z × 128; bias = the object's +0xcc: 0 for the box, 666666 for the shadow quad), so the shadow quad is drawn after every box face except those
whose average depth is about 0 (faces cut by the near plane), which can overpaint it.

Picture 3's camera is 2720 units from the room's turning axis, farther than the nearest walls (2024, 2688), so
for records 9791..9926, 9997..10023 and 10309..10339 it is outside the room: the wall between it and the room
faces away and is culled, and the view looks through it into the room (the remake must cull the room's back faces
too). The room is convex and seen from inside: each pixel shows one wall, and the screen is always covered (all
376 dumped frames of pictures 3 / 4: 100 %). At the end of picture 4 the drifting camera leaves the room through
the wall x = 2696 (object space) from record 4929, after the fade to black (13:48, black from record 4914).

The warp grid covers the screen at every rotation the part shows, but only just: because the grid is 4° off
facing the camera and not centred, a screen corner lies outside it by up to 0.253 px for m (8.2) in 73.5..83.5,
230.5..241, 259..269 (picture 2) and 701.8..711.8 (picture 1); the corner pixels' centres stay inside, so no pixel
is left undrawn (`uncovered.py`).

### 7.3 Point light 0x41b508 (the box, checked exactly)

```c
for each vertex v of the box (object space, normal n of length 819):
    level = scene->ambient;                          // 112 = 7 levels × 16
    L = R_box^T (light position − box position);    // light in object space
    d = L − v;  dist = |d|;
    if (dist <= range && dot(n, d) >= 0) {
        c = (dist < 1 ? I : I − dist * I / range) * dot(n, d) / (512 * dist);   // I (1 − dist / range) · 1.6 cos θ
        if (c > 0) level = trunc(level + c);
    }
    v->light (+0x30) = min(level, 992);              // 62.0 levels; table row = level >> 4
```

(range 5000, I = the animated intensity; the box's light is only rebuilt for vertices of visible faces.)

### 7.4 Rasterizers

All three map affinely: the face flags (+0x30 bits 10..12 = 0) select no perspective subdivision (the rasterizers
have a path that divides every 2^(n−1) pixels, unused here). Interpolants are plane equations in screen space
(16.16), evaluated per pixel along spans clipped to the window.

**0x4172e0 (warp grid; only this part uses it):** the same setup as the engine's textured Gouraud mapper 0x414e5c
(0x41be3c, 0x41be91 edge setup) but with four interpolants: the words +0x28, +0x2a, +0x2c, +0x2e (12.4 texels,
<< 12 into 16.16 with a +1 bias). Inner loop:

```c
t1 = tex1[((V1 >> 16) & vmask) << log2w | ((U1 >> 16) & umask)];   // material +0, masks 0xff, sizes 8 (256 x 256)
t2 = tex2[((ROW2 >> 16) & 255) << 8 | ((COL2 >> 16) & 255)];       // material +4; always 256 x 256
*dst++ = table[t2 * 256 + t1];                                       // material +8: the .shd
U1 += dU1; V1 += dV1; COL2 += dCOL2; ROW2 += dROW2;
```

So pixel = shd[tex[row2 / 16][col2 / 16] · 256 + tex[v1 / 16][u1 / 16]] with wrap-around at 256 texels: the
second read is the texture transposed (row from the i-based word), with the amplitudes kA, kC exchanged.

**0x414e5c (the box):** pixel = qnt[(light >> 4) · 256 + tex[v][u]], light Gouraud-interpolated from the vertex
words +0x30 (1/16 levels), u, v from the material corners (12.4).

**0x41669c (the shadow quad):** pixel = sdw[shadow[v][u] · 256 + dst] (reads the pixel under it), shadow texel
index = coordinate >> (24 − log2 size) in 16.16, i.e. 0..0xff0 maps the whole 128 or 256 texture. Its two
triangles (v0, v1, v2) and (v2, v1, v3) are mapped separately, so the moving corner v1 shears them differently.

**0x4164ec (the photos):** unscaled copy at (trunc x, trunc y), skipping index 0, clipped to the window.

## 8. Time bases

### 8.1 Which state a frame uses

The frame routine starts right after the retrace callback that copied the previous frame (it waits for it, 6.4),
and the script entries due at that moment run just before it. So **the frame shown at record N uses the state after
the retrace callback of record N − 1**: T = M_ret(N − 1) = **4 N − 155** (all dumped frames but the first after an
event), the tick counts below, and the events whose first record (table in 1) is ≤ N. The first frame after a
picture change was started before that retrace (it is shown at 4256 / 9791 with the state before it; under a white
flash).

### 8.2 Per variable

| variable | time base | closed form for the frame shown at record N |
|---|---|---|
| warp phases A, B, C | refresh tick, float32 sums | (A, B, C) = n · (sA, sB, sC), n = number of ticks since the last reset: n = N − 4255 (4257..4435, first set), N − 4436 (4436..4962), 527 + N − 9790 (9792..10492), N − 10493 (10493..10574); the speeds are the first set before record 4436, the second set from 4436 on. The first frame of a picture change (4256, 9791) has one tick less (n = 0, 527) |
| warp grid orientation | refresh tick | Rx(1.5) Ry(3.0 + 0.01 m): m = N − 4255 (4257..4612; 4256: 0), 357 − (N − 4612) (4612..4962; 6 after the last tick), 6 + (N − 9790) (9792..10574) |
| box orientation | refresh tick | Ry(0.01 m'): m' = −(N − 4612) for picture 4, N − 9790 for picture 3 |
| light position, intensity, shadow corner | music clock M (280.345 Hz) | T = 4 N − 155 (6.3) |
| camera drift | per drawn frame (frame-rate dependent) | picture 4: (x, y) = (−550 − 15 s, 2000 − 30 s), picture 3: y = 2000 − 30 s, s = N − 4786 (from 4787) / N − 10228 (from 10229); the part runs at one frame per refresh, so 30 units per refresh (2103 / s) |
| camera, shadow quad, materials, palettes, picture | script events | section 4 |

Random numbers: none.

## 9. Pixel index → colour

### 9.1 .shd (pictures 1, 2): a 50 % blend

nigga1.shd and nigga2.shd are symmetric (table[a][b] = table[b][a]) with table[a][a] = a, and table[a][b] is the
palette colour closest to (pal[a] + pal[b]) / 2 for 67 / 71 % of the colour pairs the texture uses (mean error 4.5 /
6.8 against 4.2 / 6.4 for the best possible nearest colour; `blend.py`). The remake: colour = (texel colour 1 +
texel colour 2) / 2 in RGB.

### 9.2 .qnt (pictures 3, 4): light level × colour

Row L (0..63; the part reaches 7..62) of nigga9.qnt / nigga4.qnt: row 32 = the identity, rows below are colour ×
L / 32 (to the nearest palette colour: mean error 5..6, the sum of the three 6-bit channel errors, which is the
palette's quantisation), rows above are colour × L / 32 clamped to 63 per channel (error 5..8 for the colours that
saturate, up to 15 for the few dark colours that do not: the palette has no such colours). `shades.py`. The remake: colour · L / 32 per channel, clamped to 1, with
the continuous level L = 7 + c / 16 (7.3).

### 9.3 .sdw (pictures 3, 4): shadow density × colour

Row s (0..15) of shadow9.sdw / shadow4.sdw equals .qnt row 32 − 4 s / 3 exactly where that is an integer
(s = 0, 3, 6, 9, 12, 15): colour × (1 − s / 24), so the darkest shadow keeps 37.5 %. The remake: dst colour ·
(1 − s / 24) with s the filtered shadow texture (0..15).

### 9.4 Palette

0x400364(pal[k]) at each picture change (the base palette; the flash entries fade the DAC to it). The part runs no
palette effects of its own.

## 10. Shortcuts for the remake to evaluate exactly

- **Warp grid**: the texture coordinates exist only at the 8 × 8 vertices and are interpolated linearly over 98
  triangles. Evaluate the formulas of 6.2 per pixel instead, with continuous grid coordinates (i, j) = ((x + 1400)
  / 350, (z + 1400) / 350) of the point of the plane seen through the pixel (invert the projection of the turning,
  tilted plane). The formulas are smooth (the linear interpolation is off by at most a few texels inside a cell),
  and they also exist outside 0..7, which removes the sub-pixel corner gap of 7.2.
- Texture reads are nearest texel of 12.4 coordinates: bilinear (wrap at 256) on the swirl textures; the 50 % table
  becomes an RGB average (9.1).
- **Box**: affine mapping over large triangles seen in perspective (visible kinks along the cell diagonals) →
  perspective-correct mapping with the same per-triangle corners (keep picture 4's mirrored halves, 4.2).
- **Light**: per-vertex levels on a 4 × 4 grid per wall, Gouraud, truncated to 1/16 and read as 64 table rows →
  either the same Gouraud with continuous levels, or (sharper) the formula of 7.3 per pixel with the interpolated
  object-space position and the interpolated smooth normal (the corner and edge normals are averaged, which is
  what rounds the light into the corners); colour × L / 32 clamped (9.2).
- **Shadow**: a 16-level density texture through 16 table rows → continuous density, filtered; draw it after the
  room without a depth test (as the sort bias does; the remake need not reproduce the near faces that can paint
  over it, 7.2).
- Integer light position, shadow corner, camera position (trunc) → floats; the turning step accumulated in doubles
  → the closed forms of 8.2; float32 phase sums → n · speed (differs by < 1e-4 rad).
- The photos: unscaled 8-bit blits with colour key 0 → the original pixels scaled with filtering and a matching
  alpha (index ≠ 0), at the positions of section 1 in the 320 × 200 frame.
- The camera drift is per drawn frame: step it per refresh (30 units, 15 in x for picture 4) as the capture shows.

## 11. Does it overwrite the whole frame?

Yes, in every frame: the warp grid or the room covers all 320 × 200 pixels (7.2), then the photo is drawn on top.
The part never clears the buffer; if the background missed a pixel it would show the frame drawn two frames
earlier (the back buffer), which does not happen in the capture.

## 12. Open questions

- The exact sub-pixel / fixed-point stepping of the three rasterizers (models match 55..88 % of the pixels; the
  rest one texel or one light level off) is the engine scope's (notes-engine.md); the remake does not need it.
- Whether the .shd / .qnt / .sdw tables were made with exactly "nearest palette colour" (RGB distance) is not
  certain (about 70 % of the blend entries are the nearest colour; the rest are close): irrelevant for a true-colour
  remake.
- The first frame after each picture change (4256, 9791) is drawn with the state from before the first tick; it is
  under a white flash.
