#!/bin/sh
# Applies patches/camd-series to a clean index of the AROS tree's HEAD and
# compares the result with the AROS working tree.
#
#   tools/check-camd-patches.sh [AROS_DIR]
#
# Nothing in AROS_DIR is modified. Exit status: 0 when every patch applies
# and the working tree has no CAMD change the series lacks.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$HOME/AROS}")
paths="workbench/libs/camd workbench/devs/midi rom/usb/classes/camdmidi compiler/include/midi"
index=$(mktemp)
trap 'rm -f "$index"' EXIT

export GIT_INDEX_FILE="$index"
git -C "$aros" read-tree HEAD
grep -v '^#' "$here/patches/camd-series" | while read -r name; do
    [ -n "$name" ] || continue
    git -C "$aros" apply --cached "$here/patches/aros-$name.patch" 2>/dev/null || {
        echo "aros-$name.patch does not apply"
        exit 1
    }
done

status=0
# shellcheck disable=SC2086
drift=$(git -C "$aros" diff --name-only -- $paths)
# shellcheck disable=SC2086
new=$(git -C "$aros" ls-files --others --exclude-standard -- $paths)
if [ -n "$drift$new" ]; then
    echo "the AROS tree differs from the series in:"
    printf '%s\n' $drift $new
    status=1
fi
exit $status
