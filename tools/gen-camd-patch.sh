#!/bin/sh
# Writes patches/aros-NAME.patch as what the AROS tree has in PATHs beyond
# patches/camd-series, and appends NAME to the series.
#
#   tools/gen-camd-patch.sh NAME PATH...       (AROS_DIR, default ~/AROS)
#
# PATHs are relative to the AROS tree, for example workbench/libs/camd.
# Nothing in the AROS tree is modified.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${AROS_DIR:-$HOME/AROS}")
name=$1
shift
index=$(mktemp)
trap 'rm -f "$index"' EXIT

export GIT_INDEX_FILE="$index"
git -C "$aros" read-tree HEAD
grep -v '^#' "$here/patches/camd-series" | while read -r patch; do
    [ -n "$patch" ] || continue
    git -C "$aros" apply --cached "$here/patches/aros-$patch.patch" 2>/dev/null || {
        echo "aros-$patch.patch does not apply"
        exit 1
    }
done
new=$(git -C "$aros" ls-files --others --exclude-standard -- "$@")
# shellcheck disable=SC2086
[ -z "$new" ] || git -C "$aros" add -N $new
git -C "$aros" diff --no-color -- "$@" | grep -v '^index ' > "$here/patches/aros-$name.patch"
test -s "$here/patches/aros-$name.patch"
echo "$name" >> "$here/patches/camd-series"
grep -c '^diff --git' "$here/patches/aros-$name.patch"
