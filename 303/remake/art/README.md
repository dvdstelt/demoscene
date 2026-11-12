# Redrawn pictures and restored video

High-resolution versions of *303*'s pictures and video clips (Acme, X'97). The remake shows them by default;
`--original-art` shows the original pixels and the decoded video frames, which it reads from `MUSTARD.PAK` and the
`*.LZW` files. How the remake reads this folder is in [../README.md](../README.md) ("Redrawn pictures and restored
video"); which pictures and clips can be replaced, and what a replacement has to keep, is in
[../../re/notes-pictures.md](../../re/notes-pictures.md).

These are fan versions of Acme's work, made for this non-commercial tribute and under the same non-commercial terms
as the rest of the repository (see [NOTICE.md](../../../NOTICE.md)). They are not Acme's work and not endorsed by
them.

## Restored video

`video/<CLIP>/NNNN.jpg`: AI restorations of the four video clips, made with SeedVR2 on Comfy Cloud from the original
frames, November 2026. One file per frame of `<CLIP>.LZW` (NNNN = the frame's index from 0000), 1280x768, the 5:3
shape of the 160x96 frames.

The footage is Acme's, shot and digitised for the demo in the four days before X'97 (April 1997). The NFO names
Harlequin as "the breakdancer in the demo" (HARL), says the demo features Vic "with simstim behind", and thanks
Ravian "for sorting out the video capture card". The clips: Harlequin breakdancing in a party hall (HARL), people
dancing and clowning (VS), people at computers (ALEX), a man's face looking around, in grey (PARANOID).

### How they were made

- **Frames.** Every frame decoded as the remake decodes it (`tools/dumpvideo.cpp`, see
  [notes-pictures.md](../../re/notes-pictures.md) section 3): 160x96 RGB, PARANOID grey (R = G = B = luma). The
  4x4 DCT blocks and the quantisation of Acme's codec are in these frames.
- **Uploads.** `tools/video_art.py inputs ../re/dump/video OUT` writes one MP4 per clip, VP9 lossless (yuv444p, at
  most 2/255 from the PNGs), at the clip's frame rate: HARL 0–121, VS 0–60, ALEX 0–42 and PARANOID 0–44 (all).
- **Model.** SeedVR2 (ByteDance's one-step diffusion model for video restoration) through the
  `ComfyUI-SeedVR2_VideoUpscaler` nodes, v2.5.24, on Comfy Cloud. A test on HARL's frames 0–59 compared it with
  Upscayl's Real-ESRGAN models; Dennis chose SeedVR2.
- **Settings**, the same as the test: DiT `seedvr2_ema_7b_sharp_fp16` (7B, the "sharp" variant), device `cuda:0`,
  attention `sdpa`, no block swap, `cache_model` off; VAE `ema_vae_fp16`, encode not tiled, decode tiled (1024,
  overlap 128); upscaler resolution 768 (the short side, so 160x96 becomes 1280x768), max resolution 0, batch size
  61, uniform batch size on (a short last batch is padded to 61), colour correction `lab`, temporal overlap 0,
  prepend frames 0, input and latent noise 0, offload `cpu`, seed 42. Output: Video Combine, lossless animated WebP
  (and an H.264 CRF 8 copy, not used).
- **Graph and run.** One graph with four independent chains, one per clip (Load Video → Get Video Components →
  SeedVR2 Video Upscaler with its own DiT and VAE loaders → Video Combine), saved as
  [docs/comfyui/seedvr2-video-restore.json](../../../docs/comfyui/seedvr2-video-restore.json). It ran once, on
  12 November 2026, on the free tier: the account's last of its five free runs, no credits. Queued 26 s, executed
  in 243 s for 271 frames in five batches of 61 (HARL two, the other clips one each). The test (60 frames, one
  batch) had taken 52 s.
- **Into this folder.** `tools/video_art.py frames <CLIP> RESULT.webp .` writes the frames listed below as JPEG
  quality 90 with full-resolution colour (4:4:4). PARANOID is written as a grey JPEG of the luminance (ITU-R 601
  weights, the ones the remake's paranoid shaders use); SeedVR2's output of the grey clip was grey to within 0.03
  of 255 on average.
- **Checks.** Every restored frame, scaled down to 160x96, matches its own original frame better than the frames
  before and after it (all 271). The mean colour difference to the original is under 0.6 of 255 per channel (the
  `lab` correction). In the remake: `--shots` in every part that shows a clip, with and without `--original-art`
  (the restored frames sit exactly where the decoded ones do: best shift 0, correlation 0.997–1.0 after a blur),
  seeking forwards and backwards gives identical frames, and `--bench` at 2560x1920 shows no waits for a decode
  when a clip starts (see the remake README).

### Frames

The frames the remake shows were found by logging the frame index `Ctx::video()` is asked for while rendering
every part that uses a clip in steps of 0.02 s. The folder holds those plus a margin of four to six frames.

| clip | frames in the file | shown | restored | in this folder | size |
|---|---|---|---|---|---|
| HARL | 200 | 0–116 (harl part, 11.68 fps) | 0–121 | 0–121 (122 files) | 17.7 MB, 98–204 kB per frame |
| VS | 147 | 0–56 (dance part, 8.76 fps) | 0–60 | 0–60 (61 files) | 8.7 MB, 132–158 kB |
| ALEX | 43 | 0–22 (end of the dance part, 8.76 fps) | 0–42 | 0–26 (27 files) | 3.5 MB, 107–141 kB |
| PARANOID | 45 | 0–24 (paranoid part, 4.77 fps), 0–13 again (face floor A/B) | 0–44 | 0–30 (31 files) | 2.2 MB, 63–80 kB (grey) |

241 files, 32.1 MB. In the face floor's phase C the part still steps through PARANOID's frames 0–43 but draws them
with strength 0; frames 31–43 there come from the decoded original, which is not visible either.

### What to look at

- SeedVR2 invents fine detail that the 160x96 original does not have. Faces, eyes, teeth, hands, the prints on
  shirts and the fronts of the monitors are the model's guesses. The people in these clips are real (Harlequin, Vic
  and the others), and the restored faces are not their faces, only plausible ones. Examples: in VS frame 5 the
  face is a blur of a few pixels in the original and a smiling face with eyes and teeth in the restoration; in
  ALEX the red-haired person at the monitors gets a face where the original has a few skin-coloured pixels;
  PARANOID's face gets stubble, eyebrow hairs and skin texture.
- PARANOID frame 20, where the eyes close and the original is smeared, has horizontal streaks across the cheek in
  the restoration.
- The codec's 4x4 blocks are gone; the floor of the party hall, the ceiling lights and the monitors are sharp. The
  colours are the original's, including the pink cast.
- HARL was restored in two batches, frames 0–60 and 61–121, with no overlap; there is no visible jump between 60
  and 61.
- The motion is the clips' own (11.68, 8.76 and 4.77 frames per second); nothing is interpolated between frames.
