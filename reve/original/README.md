# Original Reve files go here

This folder is where the remake looks for the files of *Reve* by Pulse (The Party 1995; the final version of May
1996). They are not part of this repository: they belong to their authors. Get
[`pls_revf.zip`](https://files.scene.org/view/demos/groups/pulse/pls_revf.zip) from scene.org and unpack it here, or
let the fetch script do it:

```
scripts/fetch-originals.sh reve      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 reve # Windows
```

The remake reads one of them:

| file | bytes | sha256 |
|---|---|---|
| `REVE.EXE` | 999,999 | `a2053c9c3112977d3593c9f0690d32f10a225471109750b4947af68b22507cd2` |

The release also holds `PULSE!.NFO` and `FILE_ID.DIZ`. The party version (`pls_reve.zip` at The Party 1995) is a
different program and does not work with the remake.

The remake unpacks `REVE.EXE` at start-up and checks it by hash, so it needs this exact release. Everything in this
folder except this README is ignored by Git.
