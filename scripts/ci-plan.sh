#!/usr/bin/env bash
# Decides which projects the build workflow builds, and whether it publishes a release. Prints lines for
# $GITHUB_OUTPUT:
#
#   projects=["paper","jizz"]   the projects to build, as JSON (303, 303-v1, paper, jizz, stars, dope, clone,
#                               inside, reve, okean, vividexp)
#   release=paper               the demo to publish, for a release tag; empty otherwise
#   version=1.0                 the release's version, from the tag; empty otherwise
#
# A tag <demo>-v<version> (paper-v1.0, 303-v2.1) builds and releases that demo. "Run workflow" builds the projects
# named in $INPUT_PROJECTS ("all" for all of them). A push or a pull request builds the projects whose files
# changed since $BEFORE (push) or $BASE (pull request):
#   - a file in <demo>/remake/ (303/remake-v1/ for 303-v1), except its Markdown files other than art/README.md,
#     which goes into the download;
#   - every project when the workflow, these scripts or the fetch scripts changed, or when the changes cannot be
#     told (a new branch with no common history, a force push).
# A change to anything else (the documentation, the reverse-engineering notes) builds nothing; a release tag always
# packages the current files.
#
# To see what a push would build, run it with two commits: scripts/ci-plan.sh <before> [<after>]
set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

all=(303 303-v1 paper jizz stars dope clone inside reve okean vividexp)
demos=" 303 paper jizz stars dope clone inside reve okean vividexp "
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"

event=${GITHUB_EVENT_NAME:-push}
ref_type=${GITHUB_REF_TYPE:-branch}
ref_name=${GITHUB_REF_NAME:-}
head=${GITHUB_SHA:-HEAD}
base=""
if [ $# -ge 1 ]; then base=$1; head=${2:-HEAD}; event=push; ref_type=branch
elif [ "$event" = pull_request ]; then base=${BASE:-}
elif [ "$event" = push ]; then base=${BEFORE:-}
fi

declare -A build=()
release="" version=""

project_of() {  # changed file -> project it belongs to, "all", or nothing
    local top=${1%%/*} rest=${1#*/}
    case "$1" in
    .github/workflows/* | scripts/ci-*.sh | scripts/package.sh | scripts/fetch-originals.*)
        echo all; return 0 ;;
    esac
    case "$demos" in *" $top "*) ;; *) return 0 ;; esac
    case "$rest" in
    remake/art/README.md) echo "$top" ;;
    *.md) ;;
    remake/*) echo "$top" ;;
    remake-v1/*) if [ "$top" = 303 ]; then echo 303-v1; fi ;;
    esac
    return 0
}

is_commit() { [ -n "$1" ] && [ "$1" != 0000000000000000000000000000000000000000 ] && git cat-file -e "$1^{commit}" 2>/dev/null; }

if [ "$ref_type" = tag ]; then
    demo=${ref_name%%-v*}
    version=${ref_name#*-v}
    case "$demos" in *" $demo "*) ;; *) die "tag $ref_name is not <demo>-v<version> with a demo from:$demos" ;; esac
    [ "$version" != "$ref_name" ] && [ -n "$version" ] || die "tag $ref_name has no version after -v"
    build[$demo]=1
    release=$demo
elif [ "$event" = workflow_dispatch ]; then
    for p in ${INPUT_PROJECTS:-all}; do
        if [ "$p" = all ]; then for q in "${all[@]}"; do build[$q]=1; done; continue; fi
        case " ${all[*]} " in *" $p "*) build[$p]=1 ;; *) die "unknown project '$p' (known: ${all[*]})" ;; esac
    done
else
    # a new branch has no "before": compare with where it left the default branch
    if ! is_commit "$base" && [ "$event" = push ] && [ -n "${DEFAULT_BRANCH:-}" ]; then
        base=$(git merge-base "origin/$DEFAULT_BRANCH" "$head" 2>/dev/null || true)
    fi
    if is_commit "$base"; then
        while IFS= read -r file; do
            p=$(project_of "$file")
            if [ "$p" = all ]; then for q in "${all[@]}"; do build[$q]=1; done
            elif [ -n "$p" ]; then build[$p]=1
            fi
        done < <(git diff --name-only "$base" "$head")
    else
        printf 'no commit to compare with (%s): building everything\n' "${base:-none}" >&2
        for q in "${all[@]}"; do build[$q]=1; done
    fi
fi

json=""
for p in "${all[@]}"; do
    if [ -n "${build[$p]:-}" ]; then json="$json${json:+,}\"$p\""; fi
done
printf 'projects=[%s]\n' "$json"
printf 'release=%s\n' "$release"
printf 'version=%s\n' "$version"
