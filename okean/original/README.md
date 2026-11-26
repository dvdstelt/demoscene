# The original The Sea Robot of Love release

This folder holds the original release of *The Sea Robot of Love* by Orange (The Party 1995, 64k intro), as it is in [`okean.zip`](https://files.scene.org/view/demos/groups/orange/okean.zip) in Orange's folder on scene.org, without the `scene.org` banner that scene.org adds to its downloads. The remake reads its files from here at run time, and the release downloads include them. They belong to their authors ([NOTICE.md](../../NOTICE.md)); the repository's license does not cover them.

The remake reads one of them:

| file | bytes | sha256 |
|---|---|---|
| `KORSO2.EXE` | 65,358 | `a18d2212b1b95eb153a9b7709154da9614698bf4c3d88df74b420befd4fdef64` |

The release also holds `INFOFILE` (the credits) and `FILE_ID.DIZ`. The Party 1995 folder on scene.org (`parties/1995/theparty95/in64/`) has the release as `okean.zip`, `searobot.zip` and `korso2.zip`, without the banner; their listings show the same three files of the release.

The remake unpacks `KORSO2.EXE` at start-up and checks the unpacked program by hash, so it needs this exact release.
