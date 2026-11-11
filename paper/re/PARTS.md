# Paper — part timeline (from a traced run, `run_paper.py --trace`, plane part at 34 fps)

Ticks are 70.0018 Hz IRQ0 ticks from program start; "counter" is the music counter [0x1ea8b] (-1 before the
music starts). Video time = tick / 70. The intro exits at tick 12235.

| ticks | counter | main code (code32) | callback [0x1e5ef] | what is on screen |
|---|---|---|---|---|
| 0–104 | -1 | 0x7626 main: 0x1c24 sound init, 72x 0xaa2, 0x5b7d init, 11x 0x3cb7 (table at 0x174a3 -> 0x5db8), 0x3a8c, 0x39a4 (object from 0x1e353 -> [0x5de4]), 0x15af, back buffer = 0x3e, 0x1943 | 0 | black (precalc) |
| 104–1079 | -1..116 | 0x2829("psychic link", "sixty four k"), [0x2245d]=0, target 0x70 | 0x27ae | letters over a dark blue swirling warp |
| 1079–1181 | 116..128 | getmem 0x20000 -> [0x5063]/[0x5067], 0x5e7f, [0x4827]=0x17, 0x7554, 0x74e8, loop 0x13af(1) + 0x603f until 0x80 | 0 | white paper texture, a red pencil appears |
| 1181–1304 | 128..142 | loop 0x603f until [0x55d2] == 0 | 0x61ee | the pencil moves in |
| 1304–3186 | 142..366 | loop 0x603f until [0x1ea95] == 0x113 | 0x6267 | the pencil sketches a flower and a house in grey lines |
| 3186–3187 | 366 | 0x5eec twice (rendered into [0x5067] and [0x5063]), [0x466b] += 0xf0, [0x482b] = [0x466b] >> 7, 0x7207 | | |
| 3187–3442 | 367..397 | loop 0x726d + 0x72c4 until [0x7324] == 0 | 0x6199 | the sketch turns into a coloured 3D scene (colour blobs eat the paper) |
| 3442–5480 | 397..640 | 0x7207, loop 0x726d, 0x3c3e, 0x603f, 0x73a6 until 0x280 | 0x620b | coloured 3D scene: flower, house, trees, pencil; camera moves; a banner between two poles with "Statix"/"Vic" drawn on it |
| 5480–5483 | 640 | 0x5623 (setup), 0x2b55 | 0x6170 | |
| 5483–5611 | 640..655 | loop 0x726d, 0x3c3e, 0x603f, 0x73a6, [0x1ee11]++, 0x2b6c until [0x1eaa9] >= 0x2bc | 0x6170 | page-peel transition from the 3D scene to the logo page |
| 5611–6018 | 655..704 | 0x572f, then 0x2b55 | 0x561c | "present the Paper intro" on paper; "Paper" in sketchy letters |
| 6018–7741 | 704..909 | 0x687a, target 0x380 | 0x6434 -> 0x6439 | page-peel from the logo, then a single paper plane over a teal textured canyon |
| 7741–8818 | 909..1037 | 0x24df, target 0x400 | 0x26aa | greetings ("bullfrog", "jamm", "halcyon", "orange", ...) bump-mapped on a purple metallic/water surface |
| 8818–10968 | 1037..1293 | 0x388b, target 0x500 | 0x3884 | a flock of 32 paper planes in a blue canyon (frame-rate dependent: ~34 fps) |
| 10968–11377 | 1293..1342 | 0x2829("t h e   e n d"), [0x2245d]=0x18, [0x2efc]=[0x2f02]=5, 0x5b6b, target 0x53a | 0x27ae | letters over an olive swirling warp |
| 11377–12235 | 1342..1415 | 0x13d6 | 0x136c | "1996" and "psychic link" logo, fading out |
| 12235 | | 0x1c8e (sound off), text mode, int 21h/9 message, exit | | |

Snapshots (start of each part) for dynamic analysis: see BRIEF.md.
