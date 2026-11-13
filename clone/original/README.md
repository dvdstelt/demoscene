# Original clone meets clone files go here

This folder is where the remake looks for `CLONE.EXE` (63,994 bytes, sha256
`11ed22222af2cc34dbca2f8c7683354a530d2a062363d7eb298bcaf399942275`) of *clone meets clone* by Acme and Fudge
(Takeover '98). It is not part of this repository: it belongs to its authors. Get
[`acme_cln.zip`](https://files.scene.org/view/demos/groups/fudge/acme_cln.zip) from scene.org and unpack it here
(it also holds `INFO`, `FILE_ID.DIZ` and scene.org's notice; the copy in the Takeover '98 folder,
[`parties/1998/takeover98/in64/acme_cln.zip`](https://files.scene.org/view/parties/1998/takeover98/in64/acme_cln.zip),
has the same `CLONE.EXE`), or let the fetch script do it:

```
scripts/fetch-originals.sh clone      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 clone # Windows
```

The remake unpacks `CLONE.EXE` at start-up and checks the unpacked program by hash, so it needs this exact release.
Everything in this folder except this README is ignored by Git.
