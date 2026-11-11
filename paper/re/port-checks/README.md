# Porting agents' checks

The scripts the porting agents used while writing the remake's parts. They compare the remake's frames, internal
state dumps and tables with the original's capture and with the decompiling agents' models.

| folder | remake code it checked |
|---|---|
| `core2d/` | `paper/remake/src/paper/` 2D core: palettes and fade (exact to the emulator at every tick), font, textures, oscillators, warp, blur, page peel |
| `core3d/` | the 3D core: 16.16 matrices, objects, renderer, pencil lines (objects compared with the original's) |
| `title/` | `src/scenes/title.cpp`, `title_closing.cpp` |
| `sketch/` | `src/scenes/sketch.cpp` |
| `scene3d/` | `src/scenes/scene3d.cpp` |
| `logo/` | `src/scenes/logo.cpp` |
| `planes/` | `src/scenes/teal.cpp`, `flock.cpp`, `planes_canyon.cpp` |
| `greetings/` | `src/scenes/greetings.cpp` |

As in [`../models/`](../models/), the scripts are kept as they were left. Paths point at the machine they ran on,
and the data they read was generated from the original and is not included.
