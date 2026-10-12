#!/bin/sh
# Runs the whole UMP path between processes on this host, over UDP on the
# loopback interface: MIDI 1.0 bytes are made into UMPs (src/ump.c), carried
# by Network MIDI 2.0 sessions (src/netmidi2.c) and made into MIDI 1.0 bytes
# again at the other end. No AROS, no CAMD and no other implementation are
# involved: every process is tests/interop/netmidi2_peer.c.
#
#   tools/netmidi2-host.sh [WORK_DIR]
#
#   pair   A Host and a Client. Each sends a Note On, a Note Off, a Control
#          Change and a SysEx message of ten bytes, which takes two UMPs;
#          each has to print the four as the other sent them. The Client's
#          Bye has to free the Host's slot.
#   three  A Host and two Clients at once. The Host has to print what each
#          Client sent, under two different slots, and each Client what the
#          Host sent.
#
# Exit status: 0 when both passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
work=$(realpath "${1:-$(mktemp -d)}")
peer="$work/netmidi2-peer"
mkdir -p "$work"
cc -I"$here/include" -O2 -Wall -Wextra -Werror -std=c99 -o "$peer" \
    "$here/tests/interop/netmidi2_peer.c" "$here/src/netmidi2.c" \
    "$here/src/ump.c"
status=0

# The four messages, as a peer prints them, from the slot given.
received() {
    grep -q "^midi from=$2 903c64\$" "$1" &&
    grep -q "^midi from=$2 803c40\$" "$1" &&
    grep -q "^midi from=$2 b00764\$" "$1" &&
    grep -q "^midi from=$2 f07d0102030405060708090af7\$" "$1"
}

timeout 6 "$peer" 5673 --send > "$work/pair-host.log" 2>&1 &
sleep 0.3
timeout 3 "$peer" 5674 127.0.0.1 5673 --send > "$work/pair-client.log" 2>&1 || true
wait || true
if grep -q '^established slot=0 role=host peer="Peer on 5674"' "$work/pair-host.log" &&
   grep -q '^established slot=0 role=client peer="Peer on 5673"' "$work/pair-client.log" &&
   received "$work/pair-host.log" 0 && received "$work/pair-client.log" 0 &&
   [ "$(grep -c '^midi ' "$work/pair-host.log")" = 4 ] &&
   [ "$(grep -c '^midi ' "$work/pair-client.log")" = 4 ] &&
   grep -q '^closed slot=0$' "$work/pair-host.log"; then
    echo "ok: a Host and a Client exchanged a note, a controller and a SysEx message each way, once each, and the Client's Bye freed the Host's slot"
else
    echo "FAIL: a Host and a Client"
    status=1
fi

timeout 7 "$peer" 5683 --send > "$work/three-host.log" 2>&1 &
sleep 0.3
timeout 4 "$peer" 5684 127.0.0.1 5683 --send > "$work/three-a.log" 2>&1 &
timeout 4 "$peer" 5685 127.0.0.1 5683 --send > "$work/three-b.log" 2>&1 || true
wait || true
if received "$work/three-host.log" 0 && received "$work/three-host.log" 1 &&
   [ "$(grep -c '^midi ' "$work/three-host.log")" = 8 ] &&
   [ "$(grep -c '^established ' "$work/three-host.log")" = 2 ] &&
   received "$work/three-a.log" 0 && received "$work/three-b.log" 0 &&
   [ "$(grep -c '^midi ' "$work/three-a.log")" = 4 ] &&
   [ "$(grep -c '^midi ' "$work/three-b.log")" = 4 ]; then
    echo "ok: a Host with two Clients at once kept them apart, and each Client got what the Host sent"
else
    echo "FAIL: a Host with two Clients"
    status=1
fi
echo "logs: $work"
exit $status
