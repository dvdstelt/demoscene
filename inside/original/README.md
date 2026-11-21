# Original Inside files go here

This folder is where the remake looks for the files of *Inside* by CNCD (The Gathering 1996). They are not part
of this repository: they belong to their authors. Get
[`inside.zip`](https://files.scene.org/view/parties/1996/gathering96/demo/inside.zip) from scene.org and unpack
it here, or let the fetch script do it:

```
scripts/fetch-originals.sh inside      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 inside # Windows
```

The remake reads three of them:

| file | bytes | sha256 |
|---|---|---|
| `INSIDE.EXE` | 113,134 | `15ac35f8ea1c4cb32cd70b1fb7ba425af5bb60cb6c81794da26497c8ceeb53ef` |
| `INSIDE.DAT` | 1,349,919 | `9026946bd6988f3f375559a47982c7483a33817ef89d725f4ea194df84e5a865` |
| `EYE.AMF` | 677,491 | `bf6172d3b1728da542b71f355ef4b58f9573a76fec02fd9ea99a801b5886d01f` |

The release also holds `README.TXT`, `FILE_ID.DIZ`, `SETUP.EXE`, `SOUND.CFG`, DSMI's sound drivers (`SDI*.DSD`) and
`EYE.MOD`, the original edit of the tune; the emulator in [`../re/`](../re/) uses `SOUND.CFG` and `SDIGUS.DSD`.

The remake unpacks `INSIDE.EXE` and `INSIDE.DAT` at start-up and checks the unpacked program by hash, so it needs
this exact release. Everything in this folder except this README is ignored by Git.
