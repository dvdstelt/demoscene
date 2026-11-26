# The original DOPE release

This folder holds the original release of *DOPE* by Complex (The Gathering 1995), as it is in [`clx_dope.zip`](https://files.scene.org/view/demos/groups/complex/clx_dope.zip) on scene.org, without the `scene.org` banner that scene.org adds to its downloads. The remake reads its files from here at run time, and the release downloads include them. They belong to their authors ([NOTICE.md](../../NOTICE.md)); the repository's license does not cover them.

The remake reads two of them:

| file | bytes | sha256 |
|---|---|---|
| `DOPE.EXE` | 584,133 | `0c54549d741858ecddd23ac4a92d32b0d4823a537a45dcf1722613dcd6de97d0` |
| `DOPE.MOD` | 957,334 | `b2440ad6d713b38d5ca4c21a4a4ab4ab974e2f951175291769191c39ff7d93ce` |

The release also holds `DOPE.TXT` and `FILE_ID.DIZ`. The remake unpacks `DOPE.EXE` at start-up and checks every unpacked part by hash, so it needs this exact release.
