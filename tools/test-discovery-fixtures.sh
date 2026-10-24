#!/bin/sh
# The AROS half of ports/aros/midihub/discovery.c on the host: its includes
# taken off and the bonami.library calls it makes mocked
# (tests/review/mdns_advertise_mock.h), under ASan and UBSan.
#   advertise  registrations, as the AppleMIDI-only cut (#1743) has them
#   browse     the same with NETMIDI_BROWSE: queued searches
# Usage: DISCOVERY_SOURCE=/path/to/discovery.c \
#        DISCOVERY_MARKS=-DNETMIDI_BROWSE \
#        DISCOVERY_CFLAGS=-Wno-unused-function \
#        tools/test-discovery-fixtures.sh [output-dir]
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$here/build/discovery-fixtures}
source=${DISCOVERY_SOURCE:-$here/ports/aros/midihub/discovery.c}
source_include=$(dirname "$source")
extra_marks=${DISCOVERY_MARKS:-}
extra_cflags=${DISCOVERY_CFLAGS:-}
mkdir -p "$out"
for case in advertise browse; do
    marks="-D__AROS__ -DNETMIDI_SERVICE"
    test "$case" = browse && marks="$marks -DNETMIDI_BROWSE"
    marks="$marks $extra_marks"
    {
        echo '#include "mdns_advertise_mock.h"'
        sed -E '/^#include [<"].*[>"]/d' "$source"
        cat "$here/tests/review/mdns_${case}_cases.c"
    } > "$out/$case.c"
    # shellcheck disable=SC2086
    ${CC:-cc} $marks $extra_cflags -std=c99 -g -O1 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -I"$source_include" -I"$here/tests/review" \
        "$out/$case.c" -o "$out/$case"
    ASAN_OPTIONS=detect_leaks=0 "$out/$case"
done
