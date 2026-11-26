# The original clone meets clone release

This folder holds the original release of *clone meets clone* by Acme and Fudge (Takeover '98), as it is in [`acme_cln.zip`](https://files.scene.org/view/demos/groups/fudge/acme_cln.zip) on scene.org, without the `scene.org` banner that scene.org adds to its downloads. The remake reads its files from here at run time, and the release downloads include them. They belong to their authors ([NOTICE.md](../../NOTICE.md)); the repository's license does not cover them.

The remake reads `CLONE.EXE` (63,994 bytes, sha256 `11ed22222af2cc34dbca2f8c7683354a530d2a062363d7eb298bcaf399942275`). The release also holds `INFO` and `FILE_ID.DIZ`. The copy in the Takeover '98 folder, [`parties/1998/takeover98/in64/acme_cln.zip`](https://files.scene.org/view/parties/1998/takeover98/in64/acme_cln.zip), has the same `CLONE.EXE`.

The remake unpacks `CLONE.EXE` at start-up and checks the unpacked program by hash, so it needs this exact release.
