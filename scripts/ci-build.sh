#!/usr/bin/env bash
# Builds one project and packages it as a release download, as the build workflow does for every project it builds.
#
#   scripts/ci-build.sh <303|303-v1|paper|jizz|stars|dope|clone|inside|reve|okean|vividexp> <linux|windows> [OUT_DIR]
#
# Builds in <project folder>/build-ci-<linux|windows> and writes the download to OUT_DIR (default <repo>/out) with
# scripts/package.sh; 303-v1 (303/remake-v1) is only built, never packaged. Linux builds link SDL3 (and 303's
# libopenmpt) in, with the remake's <NAME>_FETCH_DEPS option, so the program runs without them installed. Windows
# builds cross-compile with mingw64-cmake. Uses ccache when it is installed.
#
# $VERSION goes into the README of the download; it defaults to "development build <commit>".
set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
usage="usage: $0 <303|303-v1|paper|jizz|stars|dope|clone|inside|reve|okean|vividexp> <linux|windows> [OUT_DIR]"

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
project=${1:-}
os=${2:-}
out=${3:-$repo/out}
case "$project" in
303-v1)                               src=303/remake-v1 demo="" ;;
303 | paper | jizz | stars | dope | clone | inside | reve | okean | vividexp) src=$project/remake demo=$project ;;
*) die "$usage" ;;
esac
case "$os" in linux | windows) ;; *) die "$usage" ;; esac
build=$repo/$src/build-ci-$os

launcher=()
if command -v ccache >/dev/null 2>&1; then
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

if [ "$os" = windows ]; then
    mingw64-cmake -S "$repo/$src" -B "$build" -G Ninja "${launcher[@]}"
else
    # the option that makes CMake download and link SDL3 (and libopenmpt) in: ACME303_FETCH_DEPS, PAPER_FETCH_DEPS...
    fetch=$(sed -n 's/^option(\([A-Z0-9_]*_FETCH_DEPS\) .*/\1/p' "$repo/$src/CMakeLists.txt")
    [ -n "$fetch" ] || die "$src/CMakeLists.txt has no <NAME>_FETCH_DEPS option"
    cmake -S "$repo/$src" -B "$build" -G Ninja -D"$fetch"=ON "${launcher[@]}"
fi
cmake --build "$build"

if [ -n "$demo" ]; then
    if [ -z "${VERSION:-}" ]; then
        commit=${GITHUB_SHA:-$(git -C "$repo" rev-parse HEAD 2>/dev/null || true)}
        export VERSION="development build ${commit:0:7}"
    fi
    bash "$repo/scripts/package.sh" "$demo" "$os" "$build" "$out"
fi
