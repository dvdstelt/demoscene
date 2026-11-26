# The original Reve release

This folder holds the original release of *Reve* by Pulse (The Party 1995; the final version of May 1996), as it is in [`pls_revf.zip`](https://files.scene.org/view/demos/groups/pulse/pls_revf.zip) on scene.org. The remake reads its files from here at run time, and the release downloads include them. They belong to their authors ([NOTICE.md](../../NOTICE.md)); the repository's license does not cover them.

The remake reads one of them:

| file | bytes | sha256 |
|---|---|---|
| `REVE.EXE` | 999,999 | `a2053c9c3112977d3593c9f0690d32f10a225471109750b4947af68b22507cd2` |

The release also holds `PULSE!.NFO` and `FILE_ID.DIZ`. The party version (`pls_reve.zip` at The Party 1995) is a different program and does not work with the remake.

The remake unpacks `REVE.EXE` at start-up and checks it by hash, so it needs this exact release.
