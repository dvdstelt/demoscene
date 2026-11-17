# Redrawing pictures

How the high-resolution redraws of STARS's, DOPE's and Paper's hand-drawn pictures were made, as a recipe for the
next one. ChatGPT's image generation made almost all of them. One picture it refused; that one was made with an open
image-edit model on Comfy Cloud. Both routes are described here, with the alternatives that were considered.
Video clips (303's) are restored with a video model instead, also on Comfy Cloud: [section 7](#7-videos).

The rules are in [AGENTS.md](../AGENTS.md) ("Redrawn pictures"): a redraw is faithful to the original (the same
composition, pose, proportions, framing, colours and text; only resolution and detail change), it lives in
`<demo>/remake/art/` with a README that names the original artist, and the remake shows the original pixels with
`--original-art`.

## 1. Decide what gets a redraw

Only pictures that are drawn as they are: title cards, backgrounds, labels, logos, sprites, and textures whose
pixels are shown. Generated textures and maps stay computed from the original, and so does anything an effect
reads through palette indices (colour cycling, index arithmetic). Some pictures can still be redrawn if the
redraw keeps what the effect needs; the decompiling notes say how each picture is used:

- a shade map coloured through palette ramps (DOPE's logo) or shaded with a paper grain (Paper's sketch): redraw it
  as a grey shade map, with the background exactly the value around it;
- a coverage mask (DOPE's credits): redraw it as white on black;
- a map of palette indices that an effect only reveals pixel by pixel (Paper's ink image, which a flood fill
  colours): redraw the finished picture (on a transparent background) and keep the effect's map from the original;
  Paper's remake runs the fill on the original's map and reveals the redraw by its timing;
- text drawn with a bitmap font and blurred as anti-aliasing (Paper's "present the" / "intro"): redraw the 1-bit
  text as a white-on-black mask and draw it sharp, anything built from it (a shadow) from the same mask;
- an env map that is only read inside a disc (DOPE's p5): keep the layout exactly;
- a picture whose parts are coloured separately (STARS's flower eyes in four palette bands): keep each part where
  it is;
- a picture that is also a texture elsewhere (STARS's women in the warp part, their middle 256 columns): keep that
  region exactly in place;
- a sprite with a transparent colour: upload it on a flat key colour and turn that into alpha afterwards.

## 2. Prepare the upload

The decompilers write every picture with the palette the demo shows it with to `<demo>/re/dump/pictures/` (ignored
by Git). `tools/align_art.py --upload` in the remake turns one into the picture to upload: scaled up with
nearest-neighbour to its display shape (a 320×200 screen becomes 1600×1200, 5×6 per pixel, because the original
is shown at 4:3; a texture keeps square texels), so the generator sees the exact pixels. Sprites go into a grid
with gaps on a flat key colour.

```
toolbox run -c acme303 python3 stars/remake/tools/align_art.py --upload 13 upload13.png
```

## 3. First choice: ChatGPT

ChatGPT's image generation, in the browser, one conversation per group of related pictures so they match in style
(the three women; the credits; the labels). The prompt is the same for every picture, with a short description of
what it shows:

- what it is: "a 320×200 pixel artwork from a 1995 MS-DOS demo, *title* by *group*, pixel art by *artist*, scaled
  up with nearest-neighbour, shown at 4:3";
- what to do: "make a faithful high-resolution redraw: the same composition, pose, proportions and framing, every
  part at the same position and size in the frame, the same colours, the same background; only the resolution and
  the detail change; do not add, remove or move anything, do not crop or zoom";
- for text: "exactly the same text, letter for letter, the same fonts";
- for sprites: "the flat magenta stands for transparency, keep it perfectly flat";
- the format: "a landscape 4:3 image, as large as you can make it".

A second or third attempt says exactly what to correct (a moved word, a dropped signature, a wrong letter). ChatGPT
returns about 1450×1088 for 4:3 and 1774×887 for 2:1. Keep the raw downloads in `<demo>/re/art-raw/` (ignored by
Git). The exact prompts and attempts per picture are in [stars/remake/art/README.md](../stars/remake/art/README.md),
[dope/remake/art/README.md](../dope/remake/art/README.md) and
[paper/remake/art/README.md](../paper/remake/art/README.md).

If Chrome does not start a download (it happened for Paper's sketch: neither ChatGPT's download button nor an
`<a download>` did anything), take the image from the page: draw it at 1:1 (`image-rendering: pixelated`) in a
fixed overlay and capture it in tiles with the browser extension's zoom action (`save_to_disk`, scale 1, a region
starting at the page origin so that one image pixel is one output pixel), then stitch the tiles. Overlapping tiles
must agree exactly; compare the stitched image's channel sums with those of the image decoded in the page.

ChatGPT checks its finished images and may refuse one ("the image we created may violate our content policies"):
it refused one of STARS's three cartoon women (picture 13), a nude, and accepted another nude (picture 12). Do not
reword a prompt to get past such a check; use another tool (section 4).

## 4. When ChatGPT refuses or is not good enough

### What was tried or considered

| tool | result |
|---|---|
| Upscayl (Real-ESRGAN models) | Sharpens only what is there. A 320×200 picture in 256 colours has too little detail, so the result stays soft and painted-looking at best; it was clearly worse than ChatGPT's redraws. |
| Stable Diffusion (SDXL) img2img with a ControlNet tile and an IP-Adapter | Can add detail while keeping the shapes. Not tried: Comfy Cloud's image-edit model below did it in one step. |
| Running ComfyUI locally | Needs NVIDIA's own driver (the open nouveau driver has no CUDA; on Fedora Atomic that means layering RPM Fusion's `akmod-nvidia` and rebooting, or booting Windows) and a large GPU: Qwen-Image-Edit has about 20 billion parameters, so an 8 GB laptop GPU needs a quantised version and is slow. On the CPU it is impractical. |
| **Comfy Cloud with Qwen-Image-Edit** | Used for STARS's picture 13. Faithful, in the style of the other redraws, no refusal. |

### Comfy Cloud with Qwen-Image-Edit-2511

[Comfy Cloud](https://www.comfy.org/cloud) runs ComfyUI in the browser on cloud GPUs, with models preinstalled. Its
free tier gave 5 runs; picture 13 took two. One run executes the whole graph, so a graph with several branches
(other seeds, with and without style references) gives several pictures for one run. Comfy's terms of service
apply to what you upload and make (no illegal content; nothing that infringes someone's rights).

The model: **Qwen-Image-Edit-2511**, an open image-edit model that takes several pictures and an instruction. It
redraws "Picture 1" in the style of "Pictures 2 and 3", so the existing redraws of the same subject can serve as
style references and the new picture matches them.

The graph is saved as [comfyui/qwen-image-edit-redraw.json](comfyui/qwen-image-edit-redraw.json): open it in
ComfyUI (Comfy Cloud or local), upload the three pictures into its three "Load Image" nodes, rewrite the prompts,
and run. What it contains:

- models: `qwen_image_edit_2511_fp8mixed` (diffusion model), `qwen_2.5_vl_7b_fp8_scaled` (text encoder, type
  `qwen_image`), `qwen_image_vae`; no LoRA;
- inputs: Picture 1 = the original, made with `--upload` (1600×1200), resized with Lanczos to 1408×1056 (exact 4:3,
  multiples of 16) and blurred with a 1.2-pixel Gaussian so the pixel steps do not read as detail; Pictures 2 and
  3 = two existing redraws, resized to 1024×768;
- `TextEncodeQwenImageEditPlus` gets the prompt and the three pictures but no VAE; three `ReferenceLatent` nodes add
  each picture's VAE latent at its own size, so Picture 1's latent has exactly the output's size and grid;
  `FluxKontextMultiReferenceLatentMethod` `index_timestep_zero` on both conditionings; the negative prompt gets the
  same pictures;
- `ModelSamplingAuraFlow` shift 3.1, `CFGNorm` 1, `KSampler` euler, simple, 30 steps, CFG 4, denoise 1, from an
  empty 1408×1056 latent.

The prompt that worked names everything that must stay, because the model otherwise borrows from the style
references:

- "Redraw Picture 1 as a high-resolution illustration in exactly the same painting style as Picture 2 and Picture
  3:" followed by a description of that style (in STARS's case: polished airbrushed cartoon, glossy shaded skin,
  thin dark ink lines, finely detailed hair, crisp edges, the flat starburst on black);
- "Picture 1 is low-resolution pixel art of the same *subject* as in Picture 2 and Picture 3. Keep everything in
  Picture 1 exactly as it is:" followed by a list: composition and framing, pose, proportions, each distinctive
  detail (eye, hair colour and fall, where each hand is, shoes, the signature), the background shapes, all colours;
- what went wrong in earlier attempts, said positively ("there is no light-coloured border around the figure: her
  edges meet the background directly");
- "Every part stays at the same position and size in the frame. Only the resolution and the detail change: remove
  the pixelation. Do not add, remove or move anything, do not crop or zoom. Do not take *(the other pictures'
  clothes, poses)* from Picture 2 or Picture 3."
- negative prompt: "pixelated, pixel art, jagged edges, dithering, blurry, low resolution, jpeg artifacts", plus
  what the references tempt it to copy and what earlier attempts added (for 13: "clothes, dress, stockings, light
  border around the figure, halo, glow, red hair").

Run two or three seeds, with and without the style references, and look at every result next to the original.
For picture 13, three of five were rejected: one copied a reference's pose and stockings, one changed the hair
colour and moved a hand, one filled the background shape. The full account is in the STARS art README, section
"Picture 13 (Comfy Cloud)".

## 5. Fit and check

`tools/align_art.py NN RAW.png` (STARS: picture number; DOPE: picture name; Paper: `sketch`, `text`, `ink`) fits
the redraw onto the original (scale in x and y, offset), writes it to `art/` covering exactly the original's frame
at the redraw's own resolution, and prints the fit: the scale and offset, and the residual (RMS colour difference
in the original's pixel grid, plain and blurred, the share of pixels where one is background and the other is not,
and the correlation of their detail; 1.00 is every shape in place). DOPE's version has `--match`, which gives the
redraw the original's colours where the generator made them darker or shifted their hue; Paper's does the same for
its grey sketch and gives its ink the original's body colour, by default.

```
toolbox run -c acme303 python3 stars/remake/tools/align_art.py 13 stars/re/art-raw/stars13_r2A_s1303.png
toolbox run -c acme303 python3 stars/remake/tools/align_art.py --sheet compare.jpg 11 12 13
```

Then look: the comparison sheet, and the remake itself in every part that uses the picture (`--shots` at those
times, with and without `--original-art`). Check the regions an effect depends on (section 1). Code that handles a
missing redraw specially may need a change: STARS's dissolve only used the redraws of 11 and 12 until 13 existed.

## 6. Record it

Add the picture to the art folder's README table: what it shows, the original artist, how it was made (tool,
model, date), the prompt in short, the attempts, the fit. Update the remake's README and the making-of if they say
the picture keeps its original pixels. Commit the picture in `art/` (allowed by AGENTS.md); the raw downloads and
uploads stay out of Git.

## 7. Videos

303 plays four video clips (`HARL.LZW`, `VS.LZW`, `ALEX.LZW`, `PARANOID.LZW`): 160×96 frames in Acme's own 4×4 DCT
codec. An image generator would redraw every frame differently, so a clip redrawn frame by frame would flicker. A
video restoration model restores a run of frames together. The 303 remake shows its clips restored with
**SeedVR2** on Comfy Cloud; the account of the run and what to look at is in
[303/remake/art/README.md](../303/remake/art/README.md).

### What was tried

A test on HARL's frames 0–59 put SeedVR2 7B next to Upscayl's Real-ESRGAN models (standard, digital-art and
high-fidelity 4×). Upscayl turned the codec's blocks and the motion blur into painted smears. SeedVR2 drew a
coherent scene: a dancer with a face, hands and folds in his shirt, a sharp floor pattern. SeedVR2 was chosen.

SeedVR2 (ByteDance) is a one-step diffusion model for video restoration; ComfyUI runs it with the
`ComfyUI-SeedVR2_VideoUpscaler` nodes (v2.5.24 on Comfy Cloud). It invents fine detail the original does not have:
faces, eyes and hands at 160×96 come out as the model imagines them, so the README of the art folder says so.

### Recipe

1. **Dump the frames.** Decode every frame as the remake does: `303/remake/tools/dumpvideo.cpp` writes
   `303/re/dump/video/<CLIP>/NNNN.png` (see `303/re/notes-pictures.md`, section 3).
2. **Find the frames that are shown.** The parts show only the start of each clip. Log the frame index
   `Ctx::video()` is asked for while rendering every part that uses a clip in small steps (`--shots` every 0.02 s
   with a temporary log line), and add a margin of a few frames. In 303: HARL 0–116, VS 0–56, ALEX 0–22, PARANOID
   0–24 (and 0–13 again in the face floor).
3. **Make the uploads.** `python3 303/remake/tools/video_art.py inputs 303/re/dump/video OUT` writes one lossless VP9
   MP4 (yuv444p, the clip's frame rate) per clip. Lossless, because a lossy upload adds its own blocks to the
   codec's. SeedVR2 restores a clip in batches (61 frames here), so the uploads are whole batches: HARL 0–121 (two),
   VS 0–60, ALEX and PARANOID whole (one each, padded).
4. **One graph, one run.** Comfy Cloud counts runs, not nodes or clips, so all clips go into one graph:
   [comfyui/seedvr2-video-restore.json](comfyui/seedvr2-video-restore.json), four independent chains of
   `Load Video` → `Get Video Components` → `SeedVR2 Video Upscaler` (with its own `SeedVR2 (Down)Load DiT Model` and
   `VAE Model` nodes) → `Video Combine`. Settings: DiT `seedvr2_ema_7b_sharp_fp16`, attention `sdpa`, no block
   swap; VAE `ema_vae_fp16`, decode tiled (1024, overlap 128); resolution 768 (the short side: 160×96 becomes
   1280×768), batch size 61 (SeedVR2 wants 4n + 1 frames), uniform batch size on, colour correction `lab`, temporal
   overlap 0, seed 42; output a lossless animated WebP. Upload each MP4 into its `Load Video` node and run once.
   A grey clip (PARANOID) goes in as it is; its result is grey to within a fraction of a level. The four 303 clips
   (271 frames) took 4 minutes on Comfy Cloud's free tier.
5. **Download** the four WebPs (24–133 MB each) into a new folder. Chrome lets a page start one download without
   asking and blocks the next ones (the "automatic downloads" permission), so download each output from a freshly
   loaded Comfy Cloud page, one at a time; the outputs stay in the account's history.
6. **Into the remake.** `python3 303/remake/tools/video_art.py frames <CLIP> RESULT.webp 303/remake/art` writes the
   frames the demo shows plus a margin (HARL 0–121, VS 0–60, ALEX 0–26, PARANOID 0–30) as
   `art/video/<CLIP>/NNNN.jpg`: JPEG quality 90 with full-resolution colour (4:4:4), PARANOID as a grey JPEG of the
   luminance (ITU-R 601 weights, the ones the paranoid shaders use). 303's 241 frames take 32 MB.
7. **Check.** Each restored frame should match its own original frame (scaled down, better than the frames before
   and after it), with the original's colours. In the remake: `--shots` in every part that shows a clip, with and
   without `--original-art` (look at faces, hands and text, and at the batch boundary of a longer clip); the same
   frames after seeking forwards and backwards; `--bench T0,T1 --shotsize 2560x1920` over each part, which lists the
   slowest frames, to see that no frame waits for its decode when a clip starts.
8. **Record it** in the art folder's README: the footage's makers, the model and settings, the run, the frames
   included, the sizes, and what the model invented.
