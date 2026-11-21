#!/usr/bin/env bash
# Publishes a demo's downloads as a GitHub Release, for a tag <demo>-v<version> such as paper-v1.0.
#
#   scripts/ci-release.sh <tag> <DIR> [--dry-run]
#
# DIR holds <demo>-remake-windows-x64.zip and <demo>-remake-linux-x64.tar.gz (scripts/ci-build.sh writes them).
# The release is called "<demo> remake <version>"; its notes say what to download and list the commits that
# changed the demo's remake since the demo's previous release tag. --dry-run prints the title and notes instead.
# Needs git with the history and tags, and gh with $GH_TOKEN.
set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tag=${1:-}
dir=${2:-}
dry=${3:-}
[ -n "$tag" ] && [ -n "$dir" ] || die "usage: $0 <demo>-v<version> <DIR> [--dry-run]"
demo=${tag%%-v*}
version=${tag#*-v}
case "$demo" in
303)   name="303";               about="*303* by Acme, the PC demo that won the X'97 demo competition in 1997" ;;
paper) name="Paper";             about="*Paper*, the 64k intro by Psychic Link released at Wired '96" ;;
jizz)  name="JIZZ";              about="*JIZZ*, the 64k intro by The Black Lotus that won at Wired '97" ;;
stars) name="STARS";             about="*STARS: Wonders of the World* by NoooN, the PC demo that won at Assembly '95" ;;
dope)  name="DOPE";              about="*DOPE* by Complex, the PC demo that won at The Gathering '95" ;;
clone) name="clone meets clone"; about="*clone meets clone*, the 64k intro by Acme and Fudge that won at Takeover '98" ;;
*) die "$tag is not <demo>-v<version> for one of 303, paper, jizz, stars, dope, clone" ;;
esac
[ "$version" != "$tag" ] && [ -n "$version" ] || die "$tag has no version after -v"

files=("$dir/$demo-remake-windows-x64.zip" "$dir/$demo-remake-linux-x64.tar.gz")
for f in "${files[@]}"; do [ -f "$f" ] || die "$f not found"; done

# the demo's previous release: the next lower version among its tags
prev=""
seen=0
while IFS= read -r t; do
    if [ "$seen" = 1 ]; then prev=$t; break; fi
    if [ "$t" = "$tag" ]; then seen=1; fi
done < <(git -C "$repo" tag -l "$demo-v*" --sort=-version:refname)

notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
{
    printf 'A high-resolution remake of %s, version %s.\n\n' "$about" "$version"
    printf -- '- **Windows** (64-bit): `%s`\n' "$(basename "${files[0]}")"
    printf -- '- **Linux** (64-bit): `%s`\n\n' "$(basename "${files[1]}")"
    printf 'The original files are not included; they belong to their authors. Unpack the download and run '
    printf '`fetch-originals.ps1` (Windows) or `./fetch-originals.sh` (Linux) in its folder to download them from '
    printf 'scene.org, then start the program. README.txt in the download has the details and the options.\n'
    if [ -n "$prev" ]; then
        printf '\n## Changes since %s\n\n' "$prev"
        changes=$(git -C "$repo" log --no-merges --format='- %s' "$prev..$tag" -- \
            "$demo/remake" scripts/package.sh scripts/fetch-originals.sh scripts/fetch-originals.ps1)
        printf '%s\n' "${changes:-- No changes to the remake itself.}"
    fi
} >"$notes"

title="$name remake $version"
if [ "$dry" = --dry-run ]; then
    printf '%s\n\n' "$title"
    cat "$notes"
    exit 0
fi
gh release create "$tag" "${files[@]}" --title "$title" --notes-file "$notes" --verify-tag
