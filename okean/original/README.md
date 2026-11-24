# Original The Sea Robot of Love files go here

This folder is where the remake looks for the files of *The Sea Robot of Love* by Orange (The Party 1995, 64k
intro). They are not part of this repository: they belong to their authors. Get
[`okean.zip`](https://files.scene.org/view/parties/1995/theparty95/in64/okean.zip) from scene.org and unpack it
here, or let the fetch script do it:

```
scripts/fetch-originals.sh okean      # Linux / macOS
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 okean # Windows
```

The remake reads one of them:

| file | bytes | sha256 |
|---|---|---|
| `KORSO2.EXE` | 65,358 | `a18d2212b1b95eb153a9b7709154da9614698bf4c3d88df74b420befd4fdef64` |

The release also holds `INFOFILE` (the credits) and `FILE_ID.DIZ`. The same scene.org folder has the release twice
more, as `searobot.zip` and `korso2.zip`; their listings show the same three files.

The remake unpacks `KORSO2.EXE` at start-up and checks the unpacked program by hash, so it needs this exact
release. Everything in this folder except this README is ignored by Git.
