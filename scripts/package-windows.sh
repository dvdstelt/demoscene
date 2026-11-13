#!/usr/bin/env bash
# Packages a MinGW build of a remake as a Windows release zip.
#
#   scripts/package-windows.sh <303|paper|jizz|stars|dope|clone> [BUILD_DIR] [OUT_DIR]
#
# BUILD_DIR defaults to <repo>/<demo>/remake/build-win (as in the remakes' READMEs), OUT_DIR to <repo>/out.
# Writes OUT_DIR/<demo>-remake-windows-x64.zip with one folder of the same name inside: the program, every DLL it
# needs that is not part of Windows, README.txt, fetch-originals.ps1, the licenses, and an empty original/ folder
# for the original files; for STARS, DOPE, clone meets clone, Paper and 303 also the art/ folder with the redrawn
# pictures (from <repo>/<demo>/remake/art), which the program finds next to itself. Fails if the program needs a DLL
# that is neither a Windows system DLL nor found in BUILD_DIR or the MinGW sysroot.
#
# The packaged program and DLLs are stripped (the static MinGW runtime libraries bring debug info along); the
# build folder is not changed.
#
# Needs x86_64-w64-mingw32-objdump and -strip (or $OBJDUMP, $STRIP) and cmake (writes the zip). $VERSION, if set,
# goes into README.txt. $MINGW_BIN overrides the MinGW DLL folder (default /usr/x86_64-w64-mingw32/sys-root/mingw/bin).
set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
demo=${1:-}
case "$demo" in
303)   program=acme303.exe ;;
paper) program=paper-remake.exe ;;
jizz)  program=jizz-remake.exe ;;
stars) program=stars-remake.exe ;;
dope)  program=dope-remake.exe ;;
clone) program=clone-remake.exe ;;
*)     die "usage: $0 <303|paper|jizz|stars|dope|clone> [BUILD_DIR] [OUT_DIR]" ;;
esac
build=${2:-$repo/$demo/remake/build-win}
out=${3:-$repo/out}
objdump=${OBJDUMP:-x86_64-w64-mingw32-objdump}
strip=${STRIP:-x86_64-w64-mingw32-strip}
mingw_bin=${MINGW_BIN:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin}
name=$demo-remake-windows-x64

command -v "$objdump" >/dev/null 2>&1 || die "$objdump not found (install mingw64-binutils or set OBJDUMP)"
command -v "$strip" >/dev/null 2>&1 || die "$strip not found (install mingw64-binutils or set STRIP)"
command -v cmake >/dev/null 2>&1 || die "cmake not found"
build=$(cd "$build" 2>/dev/null && pwd) || die "build folder ${2:-$repo/$demo/remake/build-win} not found"
[ -f "$build/$program" ] || die "$build/$program not found; build it first"
"$objdump" -f "$build/$program" | grep -q 'pei-x86-64' || die "$build/$program is not a 64-bit Windows program"

mkdir -p "$out"
out=$(cd "$out" && pwd)
stage="$out/$name"
rm -rf "$stage" "$out/$name.zip"
mkdir -p "$stage/original" "$stage/licenses"
cp "$build/$program" "$stage/"
"$strip" --strip-all "$stage/$program"

# ---------------------------------------------------------------- DLLs
# DLLs that come with Windows (lower case). Anything else must be shipped.
is_system_dll() {
    case "$1" in
    api-ms-win-*.dll | ext-ms-win-*.dll) return 0 ;;
    advapi32.dll | bcrypt.dll | cfgmgr32.dll | comctl32.dll | comdlg32.dll | crypt32.dll | d3d9.dll | d3d11.dll | \
        d3d12.dll | dbghelp.dll | dinput8.dll | dsound.dll | dwmapi.dll | dxgi.dll | gdi32.dll | hid.dll | \
        imm32.dll | iphlpapi.dll | kernel32.dll | msvcrt.dll | ntdll.dll | ole32.dll | oleaut32.dll | \
        opengl32.dll | powrprof.dll | rpcrt4.dll | secur32.dll | setupapi.dll | shell32.dll | shlwapi.dll | \
        ucrtbase.dll | user32.dll | userenv.dll | uxtheme.dll | version.dll | winmm.dll | ws2_32.dll | \
        xinput1_4.dll) return 0 ;;
    esac
    return 1
}

dll_imports() {  # file -> imported DLL names, one per line
    "$objdump" -p "$1" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr -d '\r'
}

queue=("$program")
shipped=" "
system=" "
i=0
while [ $i -lt ${#queue[@]} ]; do
    file=${queue[$i]}; i=$((i + 1))
    while IFS= read -r dll; do
        [ -n "$dll" ] || continue
        lc=$(printf '%s' "$dll" | tr '[:upper:]' '[:lower:]')
        case "$shipped$system" in *" $lc "*) continue ;; esac
        if is_system_dll "$lc"; then
            system="$system$lc "
            continue
        fi
        src=""
        for dir in "$build" "$mingw_bin"; do
            for cand in "$dir"/*; do
                if [ -f "$cand" ] && [ "$(basename "$cand" | tr '[:upper:]' '[:lower:]')" = "$lc" ]; then src=$cand; break 2; fi
            done
        done
        [ -n "$src" ] || die "$file needs $dll, which is not a Windows system DLL and was not found in $build or $mingw_bin"
        cp "$src" "$stage/"
        "$strip" --strip-unneeded "$stage/$(basename "$src")"
        shipped="$shipped$lc "
        queue+=("$(basename "$src")")
    done < <(dll_imports "$stage/$file")
done

# ---------------------------------------------------------------- licenses
copy_license() {  # label required candidates...
    local label=$1 required=$2 f
    shift 2
    for f in "$@"; do
        if [ -f "$f" ]; then cp "$f" "$stage/licenses/$label.txt"; return 0; fi
    done
    if [ "$required" = 1 ]; then die "license of $label not found (looked for: $*)"; fi
    printf 'warning: license of %s not found, not included\n' "$label" >&2
}
crlf() { sed 's/\r*$/\r/' "$1" >"$2"; }  # text files with Windows line ends
crlf "$repo/LICENSE.md" "$stage/LICENSE.txt"
crlf "$repo/NOTICE.md" "$stage/NOTICE.txt"
case " $shipped " in *" sdl3.dll "*) sdl_required=1 ;; *) sdl_required=0 ;; esac
copy_license SDL3 "$sdl_required" "$build/_deps/sdl3-src/LICENSE.txt" /usr/share/licenses/mingw64-SDL3/LICENSE.txt
copy_license winpthreads 0 /usr/share/licenses/mingw64-winpthreads/COPYING
if [ "$demo" = 303 ]; then
    # libopenmpt is linked statically; its BSD license asks for the notice in binary distributions
    copy_license libopenmpt 1 "$build/_deps/libopenmpt_src-src/LICENSE" /usr/share/licenses/mingw64-libopenmpt/LICENSE
fi

# ---------------------------------------------------------------- redrawn pictures
# STARS, DOPE, clone meets clone, Paper and 303 look for an art folder next to the program (findArt in
# src/main.cpp); 303's also has art/video/<CLIP>/ with the restored video frames.
art_note=""
case "$demo" in
stars | dope | clone | paper | 303)
    art=$repo/$demo/remake/art
    [ -f "$art/README.md" ] || die "$art/README.md not found"
    mkdir -p "$stage/art"
    n=0
    for f in "$art"/*.png "$art"/*.jpg; do
        [ -f "$f" ] || continue
        cp "$f" "$stage/art/"; n=$((n + 1))
    done
    [ "$n" -gt 0 ] || die "no pictures in $art"
    if [ -d "$art/video" ]; then
        cp -r "$art/video" "$stage/art/"
    fi
    crlf "$art/README.md" "$stage/art/README.md"
    art_note='
The pictures
------------
The "art" folder holds high-resolution redraws of the original'"'"'s hand-drawn pictures: AI redraws made from the
original pictures and faithful to them (art\README.md says how each was made and names its artist). The remake
shows them by default; start it with --original-art to see the original pixels instead.
'
    if [ -d "$art/video" ]; then
        art_note="$art_note"'The "art\video" folder holds the video clips restored at high resolution with an AI video model; --original-art
shows the original frames as well.
'
    fi
    ;;
esac

# ---------------------------------------------------------------- fetch script and README
crlf "$repo/scripts/fetch-originals.ps1" "$stage/fetch-originals.ps1"

source_url="${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-dvdstelt/demoscene}"
version_line=""
if [ -n "${VERSION:-}" ]; then version_line="Version: $VERSION"; fi

if [ "$demo" = 303 ]; then
    title="303 by Acme - high resolution remake for Windows (x64)"
    about='A re-implementation of "303" by Acme, the PC demo that won the X'"'"'97 demo competition in April 1997
(code Statix, music and vocals Vic, graphics and design Aap). It plays the original'"'"'s effects, timing and
soundtrack, rendered at any resolution in true colour instead of 320x200 in 256 colours.'
    zipfile=acme_303.zip
    needed="303.EXE, MUSTARD.PAK, GARLIC.PAK, KETCHUP.PAK, ALEX.LZW, HARL.LZW, VS.LZW and PARANOID.LZW"
    options="  --window          run in a window instead of fullscreen
  --size 1280x960   window size
  --start SECONDS   start position
  --ss 2            render at twice the resolution and downsample (antialiasing)
  --loop            restart at the end
  --data DIR        the folder with the original files"
elif [ "$demo" = jizz ]; then
    title="JIZZ by The Black Lotus - high resolution remake for Windows (x64)"
    about='A re-implementation of "JIZZ", the 64k intro by The Black Lotus (code and design Jace, music Probe,
vector engine Nix) that won the PC 64k intro competition at Wired '"'"'97. It plays the original'"'"'s
effects, timing and music, rendered at any resolution in true colour instead of 320x200 in hicolor.'
    zipfile=jizz.zip
    needed="JIZZ.EXE (63,960 bytes)"
    options="  --window          run in a window instead of fullscreen
  --size 1280x960   window size
  --start SECONDS   start position
  --ss 2            render at twice the resolution and downsample (antialiasing)
  --loop            restart at the end
  --info            show the position and part in the window title
  --exe FILE        the original JIZZ.EXE"
elif [ "$demo" = stars ]; then
    title="STARS by NoooN - high resolution remake for Windows (x64)"
    about='A re-implementation of "STARS: Wonders of the World" by NoooN (music Groo, 3D BARTi, system and effects
KARL, script and pixels RA, sound code LCA), the PC demo that won the demo competition at Assembly '"'"'95. It plays
the original'"'"'s effects, timing and music, rendered at any resolution in true colour instead of 320x200 in 256
colours.'
    zipfile=nooonstf.zip
    needed="STARS.EXE (1,224,140 bytes, from the final release)"
    options="  --window          run in a window instead of fullscreen
  --size 1280x960   window size
  --start SECONDS   start position
  --ss 2            render at twice the resolution and downsample (antialiasing)
  --loop            restart at the end
  --info            show the time, music position and part in the window title
  --original-art    show the original pixels instead of the redrawn pictures
  --exe FILE        the original STARS.EXE
  --art DIR         the folder with the redrawn pictures"
elif [ "$demo" = dope ]; then
    title="DOPE by Complex - high resolution remake for Windows (x64)"
    about='A re-implementation of "DOPE" by Complex (code jmagic, graphics and music jugi), the PC demo that won
the demo competition at The Gathering '"'"'95. It plays the original'"'"'s effects, timing and music, rendered at
any resolution in true colour instead of 320x200 in 256 colours.'
    zipfile=clx_dope.zip
    needed="DOPE.EXE (584,133 bytes) and DOPE.MOD (957,334 bytes)"
    options="  --window          run in a window instead of fullscreen
  --size 1280x960   window size
  --start SECONDS   start position
  --ss 2            render at twice the resolution and downsample (antialiasing)
  --loop            restart at the end
  --info            show the time, music position and part in the window title
  --original-art    show the original pixels instead of the redrawn pictures
  --exe FILE        the original DOPE.EXE
  --mod FILE        the original DOPE.MOD (default: next to DOPE.EXE)
  --art DIR         the folder with the redrawn pictures"
elif [ "$demo" = clone ]; then
    title="clone meets clone by Acme and Fudge - high resolution remake for Windows (x64)"
    about='A re-implementation of "clone meets clone", the 64k intro by Acme and Fudge (design Aap and Echo, effects
Echo, music Simon, graphics Aap) that won the PC 64k intro competition at Takeover '"'"'98. It plays the
original'"'"'s effects, timing and music, rendered at any resolution instead of 320x200 (the original already
draws in true colour).'
    zipfile=acme_cln.zip
    needed="CLONE.EXE (63,994 bytes)"
    options="  --window          run in a window instead of fullscreen
  --size 1280x960   window size
  --start SECONDS   start position
  --ss 2            render at twice the resolution and downsample (antialiasing)
  --loop            restart at the end
  --info            show the time, music position and part in the window title
  --original-art    show the original pixels instead of the redrawn pictures
  --exe FILE        the original CLONE.EXE
  --art DIR         the folder with the redrawn pictures"
else
    title="Paper by Psychic Link - high resolution remake for Windows (x64)"
    about='A re-implementation of "Paper", the 64k intro by Psychic Link (code and graphics Statix, music Vic of
Acme) that was released at Wired '"'"'96 in November 1996. It plays the original'"'"'s effects, timing and music,
rendered at any resolution in true colour instead of 320x200 in 256 colours.'
    zipfile=paper.zip
    needed="PAPER.EXE (63,845 bytes)"
    options="  --window          run in a window instead of fullscreen
  --size 1280x960   window size
  --start SECONDS   start position
  --ss 2            render at twice the resolution and downsample (antialiasing)
  --loop            restart at the end
  --info            show the position and part in the window title
  --original-art    show the original pixels instead of the redrawn sketch
  --exe FILE        the original PAPER.EXE
  --art DIR         the folder with the redrawn pictures"
fi

{
    printf '%s\n' "$title" "$(printf '%s' "$title" | tr -c '\n' '=')" ""
    printf '%s\n' "$about"
    if [ -n "$version_line" ]; then printf '\n%s\n' "$version_line"; fi
    cat <<EOF

The original files
------------------
The remake reads the files of the original MS-DOS release ($zipfile on scene.org) at run time. They are
not included: they belong to their authors. Put

    $needed

in the "original" folder next to $program (or next to $program itself).

To download them from scene.org and check them, open PowerShell in this folder and run

    powershell -ExecutionPolicy Bypass -File fetch-originals.ps1

Then start $program.
$art_note
Keys
----
Esc quit, Space pause, Left/Right seek 5 s, Up/Down seek 30 s, F or F11 toggle fullscreen.

Options (from a command prompt or a shortcut)
----------------------------------------------
$options

More
----
Source code, how it was made, and builds for other systems: $source_url
License: LICENSE.txt (PolyForm Noncommercial 1.0.0); the original productions: NOTICE.txt;
the libraries used: licenses\\.
EOF
} | sed 's/$/\r/' >"$stage/README.txt"

# ---------------------------------------------------------------- zip
(cd "$out" && cmake -E tar cf "$name.zip" --format=zip "$name")
rm -rf "$stage"

printf 'wrote %s\n' "$out/$name.zip"
printf '  program      %s\n' "$program"
printf '  DLLs         %s\n' "$(echo $shipped)"
printf '  Windows DLLs %s\n' "$(echo $system)"
