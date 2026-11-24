# Original Vivid Experiment files go here

This folder is where the remake looks for the files of *Vivid Experiment* by Doomsday (Assembly 1996, PC demo).
They are not part of this repository: they belong to their authors. Get
[`vividexp.zip`](https://files.scene.org/view/parties/1996/assembly96/demo/vividexp.zip) from the Assembly '96
folder on scene.org and unpack it here, or let the fetch script do it:

```
scripts/fetch-originals.sh vividexp      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 vividexp # Windows
```

The remake reads three of them:

| file | bytes | sha256 |
|---|---|---|
| `VIVID.EXE` | 1,874,784 | `f60040bb29b830482202eed85490e16c858494e7f2b4146dc3255ba3ccf2bf28` |
| `EXPRMNT1.XM` | 663,228 | `1aab58c9bbd8697a86ff80564f5cd95e4918c2ee6f83aa41eaee54ccf2b8080b` |
| `EXPRMNT2.XM` | 391,865 | `b78cef2f737647ce27d9d0636f93aca84e1e1d9376fda3dca2321e6c0cc48362` |

The release also holds `STUPHPH.NFO` and `FILE_ID.DIZ`. Hornet's mirror on scene.org has an earlier pack of the
release (`mirrors/hornet/demos/1996/v/vividexp.zip`) with a different `FILE_ID.DIZ`.

The remake unpacks `VIVID.EXE` at start-up and checks the unpacked program by hash, so it needs this exact release.
Everything in this folder except this README is ignored by Git.
