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
#   timed  The program MIDIHub (the Linux build, build/MIDIHub) as Host and
#          a Client that sends twelve notes meant 50 ms apart, each up to
#          20 ms late, with JR Timestamps. With no latency the notes are
#          played as they come; with 40 ms of it they have to be played
#          50 ms apart. Skipped when build/MIDIHub is not there.
#   client The program MIDIHub as Client: its configuration names a Host
#          by address, which it has to invite, and whose four messages it
#          has to take. Skipped when build/MIDIHub is not there.
#
# Exit status: 0 when all passed.
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
# The spacing of the notes MIDIHub delivered, as "worst late" where worst
# is the furthest any interval is from 50 ms, in tenths of a millisecond.
timed() {
    timeout -s INT 6 "$here/build/MIDIHub" --midi2 "$2" --latency "$1" \
        --trace "$3" > "$work/timed-$1-hub.log" 2>&1 &
    sleep 0.5
    timeout 5 "$peer" "$4" 127.0.0.1 "$2" --jr > "$work/timed-$1-peer.log" 2>&1 || true
    wait || true
    awk -F'[= ]' '/MIDI 2.0 delivered at=.*bytes=90/ {
            if (n++) { d = $6 - last - 500; if (d < 0) d = -d; if (d > worst) worst = d }
            last = $6
        }
        / timed MIDI 2.0 events came after/ { late = $2 }
        END { print (n == 12 ? worst + 0 : "none"), late }' "$work/timed-$1-hub.log"
}
if [ -x "$here/build/MIDIHub" ]; then
    set -- $(timed 0 5693 5690 5694)
    plain_worst=$1; plain_late=${2:-}
    set -- $(timed 40 5703 5700 5704)
    held_worst=$1; held_late=${2:-}
    if [ "$plain_worst" != none ] && [ "$held_worst" != none ] &&
       [ "$plain_late" = 12 ] && [ "$plain_worst" -gt 50 ] &&
       [ "$held_late" = 0 ] && [ "$held_worst" -le 30 ]; then
        echo "ok: twelve timestamped notes sent unevenly: played as they came with no latency (off by up to $plain_worst tenths of a ms), 50 ms apart with 40 ms of it (off by at most $held_worst)"
    else
        echo "FAIL: timed delivery: no latency worst=$plain_worst late=$plain_late; 40 ms worst=$held_worst late=$held_late"
        status=1
    fi
    printf 'local_port=5710\nmidi2_port=5713\nmidi2_peer_ip=127.0.0.1\nmidi2_peer_port=5714\n' \
        > "$work/client.conf"
    timeout 5 "$peer" 5714 --send > "$work/client-host.log" 2>&1 &
    sleep 0.3
    timeout -s INT 4 "$here/build/MIDIHub" --config "$work/client.conf" \
        > "$work/client-hub.log" 2>&1 || true
    wait || true
    if grep -q 'inviting a MIDI 2.0 host' "$work/client-hub.log" &&
       grep -q 'MIDI 2.0 session established peer="Peer on 5714"' "$work/client-hub.log" &&
       grep -q 'MIDI 2.0 bytes=903c64' "$work/client-hub.log" &&
       grep -q 'MIDI 2.0 bytes=f07d0102030405060708090af7' "$work/client-hub.log" &&
       grep -q '^established slot=0 role=host peer="AROS MIDIHub"' "$work/client-host.log"; then
        echo "ok: MIDIHub as Client invited the Host its configuration names and took its four messages"
    else
        echo "FAIL: MIDIHub as Client"
        status=1
    fi
else
    echo "skipped: timed delivery and the Client role, build/MIDIHub is not built"
fi
echo "logs: $work"
exit $status
