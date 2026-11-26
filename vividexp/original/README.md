# The original Vivid Experiment release

This folder holds the original release of *Vivid Experiment* by Doomsday (Assembly 1996, PC demo), as it is in [`vividexp.zip`](https://files.scene.org/view/parties/1996/assembly96/demo/vividexp.zip) in the Assembly '96 folder on scene.org. The remake reads its files from here at run time, and the release downloads include them. They belong to their authors ([NOTICE.md](../../NOTICE.md)); the repository's license does not cover them.

The remake reads three of them:

| file | bytes | sha256 |
|---|---|---|
| `VIVID.EXE` | 1,874,784 | `f60040bb29b830482202eed85490e16c858494e7f2b4146dc3255ba3ccf2bf28` |
| `EXPRMNT1.XM` | 663,228 | `1aab58c9bbd8697a86ff80564f5cd95e4918c2ee6f83aa41eaee54ccf2b8080b` |
| `EXPRMNT2.XM` | 391,865 | `b78cef2f737647ce27d9d0636f93aca84e1e1d9376fda3dca2321e6c0cc48362` |

The release also holds `STUPHPH.NFO` and `FILE_ID.DIZ`. Hornet's mirror on scene.org has an earlier pack of the release (`mirrors/hornet/demos/1996/v/vividexp.zip`) with a different `FILE_ID.DIZ`.

The remake unpacks `VIVID.EXE` at start-up and checks the unpacked program by hash, so it needs this exact release.
