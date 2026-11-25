#!/usr/bin/env bash
# Render the YouTube videos of the remakes into videos/ (git-ignored).
#
#   scripts/render-videos.sh                      every remake, and every original-vs-remake video that can be made
#   scripts/render-videos.sh <demo> ...           one remake (the folder name: paper, 303, inside, ...)
#   scripts/render-videos.sh <demo>-original ...  the same with --original-art: <demo>-remake-original-art-2160p60.mp4
#   scripts/render-videos.sh <demo>-compare ...   the original next to the remake: <demo>-original-vs-remake.mp4
#
# A demo is any folder with remake/CMakeLists.txt; new ones need no change here if they follow the conventions in
# AGENTS.md ("Adding a new demo"): the program is <demo>/remake/build/<demo>-remake, its --video renders the whole
# production by default, and <demo>/re/frames2mp4.py turns the emulator capture <demo>/re/full.frames into a video
# that starts at the remake's time 0 (frames2mp4.py full.frames OUT --size WxH --vcodec "..."). The tables below
# hold the exceptions.
#
# Needs the remakes built in <demo>/remake/build, the originals in <demo>/original/, and ffmpeg with libsvtav1
# (Fedora's ffmpeg-free has it). The comparisons also need the emulator captures (see <demo>/re/README.md) and
# python3 with numpy (the toolbox); their sound is the remake's (the captures' sound cards are simplified or not
# recorded with the frames). Their intermediate files go to $TMPDIR (a lossless copy of the capture: about 9 MB per
# second).
# The remakes with redrawn pictures show them by default; the "-original" targets render them with the original
# pictures (and 303's original video clips).
#
# Output: 2880x2160 (4:3 at 2160p, so YouTube offers 4K), 60 fps, AV1 (CRF 18) + AAC 320k in MP4; the comparisons
# 2880x1080 (two 4:3 halves of 1440x1080).
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

# ---------------------------------------------------------------- the exceptions to the conventions
remake_bin() {
  case $1 in
    303) echo "$ROOT/303/remake/build/acme303" ;;
    *) echo "$ROOT/$1/remake/build/$1-remake" ;;
  esac
}
# --from / --to of the remake's --video when its default is not the whole production
remake_range() {
  case $1 in
    paper) echo "--from 0 --to 12235" ;;   # 70 Hz ticks: the whole intro
  esac
}
# options of the remake's --video for a video that is to be published
remake_options() {
  case $1 in
    vividexp) echo "--censor" ;;   # the nude parts of the pictures as a mosaic (the comparison's capture is not)
  esac
}
# frames2mp4.py options that make the capture's video start at the remake's time 0
capture_args() {
  case $1 in
    dope) echo "--from 0.1104" ;;   # program time; the music (the remake's 0) starts 0.1104 s in
  esac
}

# ---------------------------------------------------------------- the videos
remake() {  # <demo> [original]
  local d=$1 bin; bin=$(remake_bin "$1")
  [ -x "$bin" ] || { echo "build $d/remake first"; return 1; }
  # (the help text is read in full first: grep -q stops reading at the first match, which pipefail counts as a
  # failure of the program writing to it)
  local help; help=$("$bin" --help 2>&1 || true)
  if ! grep -q -- --video <<<"$help"; then echo "$d/remake has no --video option"; return 1; fi
  # shellcheck disable=SC2046
  "$bin" $(variant "${2:-}") $(remake_options "$d") --video "$OUT/$d-remake$(suffix "${2:-}")-2160p60.mp4" \
         $(remake_range "$d") \
         --fps "$FPS" --shotsize "$SIZE" --vcodec "$VCODEC"
}

compare() {  # <demo>: the original (emulator capture, 320x200 shown at 4:3, nearest-neighbour) left, remake right
  local d=$1 re=$ROOT/$1/re bin; bin=$(remake_bin "$1")
  if [ ! -f "$re/full.frames" ] || [ ! -f "$re/frames2mp4.py" ]; then
    echo "$d: no emulator capture to compare with ($d/re/full.frames and $d/re/frames2mp4.py; see $d/re/README.md)"
    return 1
  fi
  [ -x "$bin" ] || { echo "build $d/remake first"; return 1; }
  if ! python3 -c 'import numpy' 2>/dev/null; then
    echo "$d-compare needs python3 with numpy for $d/re/frames2mp4.py: run it in the toolbox"
    echo "  (toolbox run -c acme303 scripts/render-videos.sh $d-compare)"
    return 1
  fi
  # Each step is checked by hand: the targets run as `compare ... || true`, where bash ignores set -e.
  local tmp; tmp=$(mktemp -d)
  # shellcheck disable=SC2046
  python3 "$re/frames2mp4.py" "$re/full.frames" "$tmp/original.mkv" $(capture_args "$d") --size 1440x1080 \
          --vcodec "-c:v ffv1" && [ -s "$tmp/original.mkv" ] ||
    { echo "$d: the capture's video failed"; rm -rf "$tmp"; return 1; }
  # shellcheck disable=SC2046
  "$bin" --video "$tmp/remake.mp4" $(remake_range "$d") --fps "$FPS" --shotsize 1440x1080 \
         --vcodec "-c:v libsvtav1 -crf 12 -preset 6" && [ -s "$tmp/remake.mp4" ] ||
    { echo "$d: the remake's video failed"; rm -rf "$tmp"; return 1; }
  $(ffmpeg_bin) -hide_banner -loglevel error -y -i "$tmp/original.mkv" -i "$tmp/remake.mp4" \
    -filter_complex "[0:v]fps=$FPS[a];[1:v]setsar=1[b];[a][b]hstack=inputs=2[v]" \
    -map "[v]" -map 1:a $VCODEC -pix_fmt yuv420p -c:a aac -b:a 320k -shortest "$OUT/$d-original-vs-remake.mp4" ||
    { echo "$d: putting the two side by side failed"; rm -rf "$tmp"; return 1; }
  rm -rf "$tmp"
  echo "wrote $OUT/$d-original-vs-remake.mp4"
}

demos() {  # every folder with a remake
  local f
  for f in "$ROOT"/*/remake/CMakeLists.txt; do f=${f%/remake/CMakeLists.txt}; echo "${f##*/}"; done
}

targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
  for d in $(demos); do
    targets+=("$d")
    if [ -f "$ROOT/$d/re/full.frames" ] && [ -f "$ROOT/$d/re/frames2mp4.py" ]; then targets+=("$d-compare"); fi
  done
fi
for t in "${targets[@]}"; do
  echo "== $t"
  d=${t%-original}; d=${d%-compare}
  [ -f "$ROOT/$d/remake/CMakeLists.txt" ] || { echo "unknown target $t (no $d/remake)"; exit 2; }
  case $t in
    *-original) remake "$d" original || true ;;
    *-compare) compare "$d" || true ;;
    *) remake "$d" || true ;;
  esac
done
