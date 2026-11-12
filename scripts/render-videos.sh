#!/usr/bin/env bash
# Render the YouTube videos of the remakes into videos/ (git-ignored).
#
#   scripts/render-videos.sh [paper|paper-compare|303|jizz|stars|dope ...]     (default: all that can be made)
#   scripts/render-videos.sh 303-original stars-original ...                  the same with --original-art
#
# Needs the remakes built in <demo>/remake/build, the originals in <demo>/original/, and ffmpeg with libsvtav1
# (Fedora's ffmpeg-free has it). paper-compare also needs the emulator capture paper/re/full.frames (see
# paper/re/README.md); its sound is the remake's, because the capture's simplified GUS does not ramp volumes down.
# The remakes with redrawn pictures (Paper, 303, STARS, DOPE) show them by default; the "-original" targets render
# them with the original pictures (and 303's original video clips) into <demo>-remake-original-art-2160p60.mp4.
#
# Output: 2880x2160 (4:3 at 2160p, so YouTube offers 4K), 60 fps, AV1 (CRF 18) + AAC 320k in MP4.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/videos}
SIZE=${SIZE:-2880x2160}
FPS=${FPS:-60}
VCODEC=${VCODEC:--c:v libsvtav1 -crf 18 -preset 5 -g 240}
mkdir -p "$OUT"

ffmpeg_bin() {  # inside a toolbox/flatpak container, use the host's ffmpeg
  if [ -e /run/.containerenv ] || [ -e /run/.toolboxenv ]; then echo "flatpak-spawn --host ffmpeg"; else echo ffmpeg; fi
}

# Variant of a render: "" (the redrawn pictures, where a remake has them) or "original" (--original-art).
variant() { [ "${1:-}" = original ] && echo "--original-art" || true; }
suffix() { [ "${1:-}" = original ] && echo "-original-art" || true; }

paper() {
  local bin=$ROOT/paper/remake/build/paper-remake
  [ -x "$bin" ] || { echo "build paper/remake first"; return 1; }
  # --from/--to are in 70 Hz ticks: the whole intro is 0..12235
  "$bin" $(variant "${1:-}") --video "$OUT/paper-remake$(suffix "${1:-}")-2160p60.mp4" --from 0 --to 12235 \
         --fps "$FPS" --shotsize "$SIZE" --vcodec "$VCODEC"
}

paper_compare() {
  # Original (emulator capture, 320x200 shown at 4:3, nearest-neighbour) left, remake right: 2880x1080.
  local re=$ROOT/paper/re bin=$ROOT/paper/remake/build/paper-remake
  [ -f "$re/full.frames" ] || { echo "paper/re/full.frames missing: make the capture first (paper/re/README.md)"; return 1; }
  local tmp; tmp=$(mktemp -d)
  python3 "$re/frames2mp4.py" "$re/full.frames" "$tmp/original.mkv" --size 1440x1080 --vcodec "-c:v ffv1"
  "$bin" --video "$tmp/remake.mp4" --from 0 --to 12235 --fps "$FPS" --shotsize 1440x1080 \
         --vcodec "-c:v libsvtav1 -crf 12 -preset 6"
  $(ffmpeg_bin) -hide_banner -loglevel error -y -i "$tmp/original.mkv" -i "$tmp/remake.mp4" \
    -filter_complex "[0:v]fps=$FPS[a];[1:v]setsar=1[b];[a][b]hstack=inputs=2[v]" \
    -map "[v]" -map 1:a $VCODEC -pix_fmt yuv420p -c:a aac -b:a 320k -shortest "$OUT/paper-original-vs-remake.mp4"
  rm -rf "$tmp"
}

r303() {
  local bin=$ROOT/303/remake/build/acme303
  [ -x "$bin" ] || { echo "build 303/remake first"; return 1; }
  if ! "$bin" --help 2>&1 | grep -q -- --video; then echo "303/remake has no --video option yet"; return 1; fi
  "$bin" $(variant "${1:-}") --video "$OUT/303-remake$(suffix "${1:-}")-2160p60.mp4" --fps "$FPS" --shotsize "$SIZE" \
         --vcodec "$VCODEC"
}

jizz() {
  local bin=$ROOT/jizz/remake/build/jizz-remake
  [ -x "$bin" ] || { echo "build jizz/remake first"; return 1; }
  # --from/--to are in seconds of music time: the whole intro is 0..147.75
  "$bin" --video "$OUT/jizz-remake-2160p60.mp4" --fps "$FPS" --shotsize "$SIZE" --vcodec "$VCODEC"
}

stars() {
  local bin=$ROOT/stars/remake/build/stars-remake
  [ -x "$bin" ] || { echo "build stars/remake first"; return 1; }
  # --from/--to are in seconds of music time: the whole demo is 0..376.49 (the music is 372.1 s long)
  "$bin" $(variant "${1:-}") --video "$OUT/stars-remake$(suffix "${1:-}")-2160p60.mp4" --fps "$FPS" \
         --shotsize "$SIZE" --vcodec "$VCODEC"
}

dope() {
  local bin=$ROOT/dope/remake/build/dope-remake
  [ -x "$bin" ] || { echo "build dope/remake first"; return 1; }
  # --from/--to are in seconds of music time: the whole demo is 0..461.98
  "$bin" $(variant "${1:-}") --video "$OUT/dope-remake$(suffix "${1:-}")-2160p60.mp4" --fps "$FPS" \
         --shotsize "$SIZE" --vcodec "$VCODEC"
}

targets=("$@"); [ ${#targets[@]} -gt 0 ] || targets=(paper paper-compare 303 jizz stars dope)
for t in "${targets[@]}"; do
  echo "== $t"
  case $t in
    paper) paper ;;
    paper-original) paper original ;;
    paper-compare) paper_compare || true ;;
    303) r303 || true ;;
    303-original) r303 original || true ;;
    jizz) jizz || true ;;
    stars) stars || true ;;
    stars-original) stars original || true ;;
    dope) dope || true ;;
    dope-original) dope original || true ;;
    *) echo "unknown target $t"; exit 2 ;;
  esac
done
ls -la "$OUT"
