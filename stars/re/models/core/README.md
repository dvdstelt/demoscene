# models/core: scripts behind notes-core.md

Run inside the toolbox from this folder (`toolbox run -c acme303 python3 <script>`). They read `../../unpacked/`,
`../../full.frames` / `full.ticks` and `../../snaps/`.

| script | what it does / checks |
|---|---|
| `cap.py` | helpers: capture records (screen, palette), `full.ticks` rows, overlay files |
| `loads.py` | every call of the overlay loader 0x4a9f3 and of 0x17e50 with the file number loaded into EAX |
| `uses.py [n ...]` | where each loaded file's pointer is stored and which instructions read it |
| `fades.py [first last file...]` | finds the fade level L with DAC = min(63, pal * L >> 8) for every captured frame of main's picture sequences (picture 57, credits) and prints the ramps |
| `mainwaits.py` | the first captured frame past each music position main waits for |
| `rng.py` | the random generator 0x4af10; counts the calls between snapshots |
| `unpack_spec.py` | an unpacker written only from the notes (Infiny decryption, PKLITE, overlay directory, DIET); compares with `unpacked/` |
| `palruns.py` | per part: number of distinct DAC palettes and which file palettes appear unscaled |
| `bgpal.py` | how the palette entries of the 3D-part backgrounds (19, 25, 27, 47) change during their parts |
| `dissolve_pics.py OUT.png` | decodes files 36..41 (run masks + R, G, B planes) like 0x15500 |
| `gallery.py OUTDIR` | renders every overlay file as an image (for the catalogue) |
| `pictures.py` | writes the pictures and drawn textures to `../../dump/pictures/fileNN.png` (see notes-core.md, "Pictures") |
