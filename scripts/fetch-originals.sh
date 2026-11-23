#!/usr/bin/env bash
# Downloads the original MS-DOS releases from scene.org and puts the files the remakes read into place,
# checking every file's SHA-256.
#
#   scripts/fetch-originals.sh          all demos
#   scripts/fetch-originals.sh 303      only 303 by Acme (X'97)
#   scripts/fetch-originals.sh paper    only Paper by Psychic Link (Wired '96)
#   scripts/fetch-originals.sh jizz     only JIZZ by The Black Lotus (Wired '97)
#   scripts/fetch-originals.sh stars    only STARS by NoooN (Assembly '95)
#   scripts/fetch-originals.sh dope     only DOPE by Complex (The Gathering '95)
#   scripts/fetch-originals.sh clone    only clone meets clone by Acme and Fudge (Takeover '98)
#   scripts/fetch-originals.sh inside   only Inside by CNCD (The Gathering '96)
#   scripts/fetch-originals.sh reve     only Reve by Pulse (The Party '95; final version 1996)
#
# In the repository the files go into <repo>/<demo>/original/. Anywhere else (next to
# a released program) they go into an original/ folder next to this script, and without an argument only the
# demo whose program is there is fetched.
#
# A file that is already there and verifies is never touched. A file that is there but differs is renamed to
# <name>.old before the release version takes its place. Needs bash, curl or wget, unzip, and sha256sum or
# shasum. Works on Linux and macOS.
set -euo pipefail

# ---------------------------------------------------------------- the releases
# The files are checked, not the zips, so any mirror or repack with the same files is accepted.
demo_info() {
    case "$1" in
    303)
        title="303 by Acme (X'97, 1997)"
        zip_name=acme_303.zip
        zip_path=demos/groups/acme/demos/acme_303.zip
        program=acme303
        files=(
            "303.EXE      a3f8422a5da12ac114b8306c66577381ecf314cacb3a13bc768415f015081f22"
            "MUSTARD.PAK  8510449684e017107546a66047fccac8e51189bf40836a351a7dec09b4ac40ae"
            "GARLIC.PAK   c864f6057301342926f3771163e00004aabda3c602ddc53c6248ee451fd275e8"
            "KETCHUP.PAK  875f6d5fe9579438000e6bbcf05ea847be861d66ab2c31b8e429fee2ff650d8e"
            "ALEX.LZW     3c5edb084bc3b7d6021e9fbdb7df410ce1fa4150b84ea0a2423b55b48e91b2a6"
            "HARL.LZW     89800ce6ad6b8b142ce703b741790d6e8a7cf405f696c9adb2922386281b6a29"
            "VS.LZW       79094754209309eaa26faf686c8721a1901fb35b2118a6a1905a516b4ce2c730"
            "PARANOID.LZW 2662410562ac779fce3381adb9a696a3c49b330ad6c5e8e84cce37893a196fef"
        )
        ;;
    paper)
        title="Paper by Psychic Link (Wired '96, 1996)"
        zip_name=paper.zip
        zip_path=parties/1996/wired96/in64/paper.zip
        program=paper-remake
        files=(
            "PAPER.EXE    e60c3eccc4b38df0461d78fd884758886de9b4c1fa31505d6556874ec6b8ef4d"
        )
        ;;
    jizz)
        title="JIZZ by The Black Lotus (Wired '97, 1997)"
        zip_name=jizz.zip
        zip_path=demos/groups/tbl/pc/jizz.zip
        program=jizz-remake
        files=(
            "JIZZ.EXE     8bf022ea25fc6c02504e915441cc40ec0a2226d21eda4ffe1a1a46d2dca99593"
        )
        ;;
    stars)
        title="STARS: Wonders of the World by NoooN (Assembly '95, 1995; final release)"
        zip_name=nooonstf.zip
        zip_path=demos/groups/nooon/nooonstf.zip
        program=stars-remake
        files=(
            "STARS.EXE    148aa9240f6a29a5678a0812cf295a38039560f05d26dcfae8c829f8cafa97fc"
        )
        ;;
    dope)
        title="DOPE by Complex (The Gathering '95, 1995)"
        zip_name=clx_dope.zip
        zip_path=demos/groups/complex/clx_dope.zip
        program=dope-remake
        files=(
            "DOPE.EXE     0c54549d741858ecddd23ac4a92d32b0d4823a537a45dcf1722613dcd6de97d0"
            "DOPE.MOD     b2440ad6d713b38d5ca4c21a4a4ab4ab974e2f951175291769191c39ff7d93ce"
        )
        ;;
    clone)
        title="clone meets clone by Acme and Fudge (Takeover '98, 1998)"
        zip_name=acme_cln.zip
        zip_path=demos/groups/fudge/acme_cln.zip
        program=clone-remake
        files=(
            "CLONE.EXE    11ed22222af2cc34dbca2f8c7683354a530d2a062363d7eb298bcaf399942275"
        )
        ;;
    inside)
        title="Inside by CNCD (The Gathering '96, 1996)"
        zip_name=inside.zip
        zip_path=parties/1996/gathering96/demo/inside.zip
        program=inside-remake
        files=(
            "INSIDE.EXE   15ac35f8ea1c4cb32cd70b1fb7ba425af5bb60cb6c81794da26497c8ceeb53ef"
            "INSIDE.DAT   9026946bd6988f3f375559a47982c7483a33817ef89d725f4ea194df84e5a865"
            "EYE.AMF      bf6172d3b1728da542b71f355ef4b58f9573a76fec02fd9ea99a801b5886d01f"
            "SDIGUS.DSD   9eebd39b89135d55039cd7b8de33a032555c0c6859a26efe2ecc07e36737eef1"
        )
        ;;
    reve)
        title="Reve by Pulse (The Party '95, 1995; final version 1996)"
        zip_name=pls_revf.zip
        zip_path=demos/groups/pulse/pls_revf.zip
        program=reve-remake
        files=(
            "REVE.EXE     a2053c9c3112977d3593c9f0690d32f10a225471109750b4947af68b22507cd2"
        )
        ;;
    *) return 1 ;;
    esac
    # scene.org itself, two https mirrors, then the scene.org redirector (picks any mirror)
    urls=(
        "https://ftp.scene.org/pub/$zip_path"
        "https://mirror.netcologne.de/scene.org/$zip_path"
        "https://mirror.scenesat.com/scene.org/$zip_path"
        "https://files.scene.org/get/$zip_path"
    )
}

all_demos=(303 paper jizz stars dope clone inside reve)

# ---------------------------------------------------------------- tools
usage() {  # the comment at the top of this file
    awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "${BASH_SOURCE[0]}"
}

say()  { printf '%s\n' "$*"; }
fail() { printf 'error: %s\n' "$*" >&2; }

if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum <"$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 <"$1" | awk '{print $1}'; }
else
    fail "need sha256sum or shasum"; exit 1
fi

download() {  # url file
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --connect-timeout 20 --max-time 900 --retry 2 -o "$2" "$1"
    else
        wget -q --timeout=20 --tries=3 -O "$2" "$1"
    fi
}

check_download_tools() {
    if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
        fail "need curl or wget to download"; return 1
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        fail "need unzip"; return 1
    fi
}

# ---------------------------------------------------------------- where the files go
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
in_repo=0
for d in "${all_demos[@]}"; do
    if [ -d "$repo_root/$d/remake" ]; then in_repo=1; fi
done

dest_dir() {  # demo
    if [ "$in_repo" = 1 ]; then printf '%s/%s/original' "$repo_root" "$1"; else printf '%s/original' "$script_dir"; fi
}

work=""
cleanup() { if [ -n "$work" ]; then rm -rf "$work"; fi; }
trap cleanup EXIT

# ---------------------------------------------------------------- one demo
fetch_demo() {  # demo
    demo_info "$1"
    local dest name want entry url ok n old
    dest=$(dest_dir "$1")
    say "== $title"
    say "   into $dest"

    local need=()
    for entry in "${files[@]}"; do
        read -r name want <<<"$entry"
        if [ -f "$dest/$name" ] && [ "$(sha256 "$dest/$name")" = "$want" ]; then
            say "   ok         $name (already there)"
        else
            need+=("$name")
        fi
    done
    if [ ${#need[@]} -eq 0 ]; then
        say "   all files present and verified, nothing to download"
        return 0
    fi

    check_download_tools || return 1
    if [ -z "$work" ]; then work=$(mktemp -d "${TMPDIR:-/tmp}/fetch-originals.XXXXXX") || return 1; fi
    local zip="$work/$1-$zip_name" x="$work/$1-files"

    # download, unpack and verify; on any failure try the next mirror
    ok=0
    for url in "${urls[@]}"; do
        say "   download   $url"
        rm -rf "$zip" "$x"
        if ! download "$url" "$zip"; then
            say "              failed, trying the next mirror"; continue
        fi
        mkdir -p "$x"
        if ! unzip -tqq "$zip" >/dev/null 2>&1; then
            say "              not a zip file, trying the next mirror"; continue
        fi
        unzip -qq -o -j -C "$zip" "${need[@]}" -d "$x" >/dev/null 2>&1
        # a release split into zips inside the zip (Inside: inside1.zip and inside2.zip): look in those too
        for inner in $(unzip -Z1 "$zip" 2>/dev/null | grep -i '\.zip$'); do
            mkdir -p "$x/inner"
            if unzip -qq -o -j "$zip" "$inner" -d "$x/inner" >/dev/null 2>&1; then
                unzip -qq -o -j -C "$x/inner/$(basename "$inner")" "${need[@]}" -d "$x" >/dev/null 2>&1
            fi
        done
        ok=1
        for entry in "${files[@]}"; do
            read -r name want <<<"$entry"
            case " ${need[*]} " in *" $name "*) ;; *) continue ;; esac
            if [ ! -f "$x/$name" ] || [ "$(sha256 "$x/$name")" != "$want" ]; then
                say "              $name in this download does not match the known release"
                ok=0
            fi
        done
        if [ "$ok" = 1 ]; then break; fi
        say "              trying the next mirror"
    done
    if [ "$ok" = 0 ]; then
        fail "could not get a verified copy of $zip_name from any mirror; nothing was changed in $dest"
        return 1
    fi

    # install the verified files
    mkdir -p "$dest" || return 1
    for entry in "${files[@]}"; do
        read -r name want <<<"$entry"
        case " ${need[*]} " in *" $name "*) ;; *) continue ;; esac
        if [ -e "$dest/$name" ]; then
            old="$dest/$name.old"; n=1
            while [ -e "$old" ]; do n=$((n + 1)); old="$dest/$name.old$n"; done
            mv "$dest/$name" "$old" || return 1
            say "   replaced   $name (it differed from the release; the old file is now $(basename "$old"))"
        else
            say "   added      $name"
        fi
        cp "$x/$name" "$dest/$name.part" && mv "$dest/$name.part" "$dest/$name" || return 1
        if [ "$(sha256 "$dest/$name")" != "$want" ]; then
            fail "$dest/$name does not verify after copying"; return 1
        fi
    done
    say "   all files present and verified"
}

# ---------------------------------------------------------------- arguments
demos=()
for arg in "$@"; do
    case "$arg" in
    303 | paper | jizz | stars | dope | clone | inside | reve) demos+=("$arg") ;;
    all) demos+=("${all_demos[@]}") ;;
    -h | --help) usage; exit 0 ;;
    *) fail "unknown argument '$arg'"; usage >&2; exit 2 ;;
    esac
done
if [ ${#demos[@]} -eq 0 ]; then
    if [ "$in_repo" = 0 ]; then
        # next to a released program: fetch what that program needs
        for d in "${all_demos[@]}"; do
            demo_info "$d"
            if [ -e "$script_dir/$program" ] || [ -e "$script_dir/$program.exe" ]; then demos+=("$d"); fi
        done
    fi
    if [ ${#demos[@]} -eq 0 ]; then demos=("${all_demos[@]}"); fi
fi

failed=0
for d in "${demos[@]}"; do
    fetch_demo "$d" || failed=1
done
if [ "$failed" = 1 ]; then
    fail "not all originals are in place (see above)"
    exit 1
fi
say "done"
