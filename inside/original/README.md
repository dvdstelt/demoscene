# Original Inside files go here

This folder is where the remake looks for the files of *Inside* by CNCD (The Gathering 1996). They are not part
of this repository: they belong to their authors. Get
[`inside.zip`](https://files.scene.org/view/parties/1996/gathering96/demo/inside.zip) from scene.org and unpack
it here, or let the fetch script do it:

```
scripts/fetch-originals.sh inside      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 inside # Windows
```

The remake reads four of them:

| file | bytes | sha256 |
|---|---|---|
| `INSIDE.EXE` | 113,134 | `15ac35f8ea1c4cb32cd70b1fb7ba425af5bb60cb6c81794da26497c8ceeb53ef` |
| `INSIDE.DAT` | 1,349,919 | `9026946bd6988f3f375559a47982c7483a33817ef89d725f4ea194df84e5a865` |
| `EYE.AMF` | 677,491 | `bf6172d3b1728da542b71f355ef4b58f9573a76fec02fd9ea99a801b5886d01f` |
| `SDIGUS.DSD` | 32,768 | `9eebd39b89135d55039cd7b8de33a032555c0c6859a26efe2ecc07e36737eef1` |

`SDIGUS.DSD` is DSMI's Gravis UltraSound driver: the remake reads its volume table. The release also holds
`README.TXT`, `FILE_ID.DIZ`, `SETUP.EXE`, `SOUND.CFG`, DSMI's other sound drivers (`SDI*.DSD`) and `EYE.MOD`, the
original edit of the tune; the emulator in [`../re/`](../re/) also uses `SOUND.CFG`.

The remake unpacks `INSIDE.EXE` and `INSIDE.DAT` at start-up and checks the unpacked program by hash, so it needs
this exact release. Everything in this folder except this README is ignored by Git.
