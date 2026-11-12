# Original DOPE files go here

This folder is where the remake looks for the files of *DOPE* by Complex (The Gathering 1995):

| file | bytes | sha256 |
|---|---|---|
| `DOPE.EXE` | 584,133 | `0c54549d741858ecddd23ac4a92d32b0d4823a537a45dcf1722613dcd6de97d0` |
| `DOPE.MOD` | 957,334 | `b2440ad6d713b38d5ca4c21a4a4ab4ab974e2f951175291769191c39ff7d93ce` |

They are not part of this repository: they belong to their authors. Get
[`clx_dope.zip`](https://files.scene.org/view/demos/groups/complex/clx_dope.zip) from scene.org and unpack it here
(it also holds `DOPE.TXT` and `FILE_ID.DIZ`), or let the fetch script do it:

```
scripts/fetch-originals.sh dope      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 dope # Windows
```

The remake unpacks `DOPE.EXE` at start-up and checks every unpacked part by hash, so it needs this exact release.
Everything in this folder except this README is ignored by Git.
