#!/bin/sh
# Runs our code against Benoit Bouchez's two endpoint classes, which share
# no code with ours and are the engines of his UMP-OX and of KissBox's
# products: RTP-MIDI (AppleMIDI sessions) and NetUMP (Network MIDI 2.0).
# Everything is on this host, over the loopback interface.
#
#   tools/interop-bouchez-host.sh [WORK_DIR]
#
# It needs git, g++ and the network once, to fetch the three repositories
# (MIT licensed) into WORK_DIR at the commits named below. Nothing of them
# is kept in this repository; tests/interop/ has only the small programs
# of ours that drive them.
#
#   applemidi, invite    MIDIHub invites the RTP-MIDI class and sends a
#                        Note On and Note Off; the class has to show them,
#                        and MIDIHub the four messages the class sends.
#   applemidi, invited   The class invites MIDIHub; MIDIHub has to show its
#                        four messages and see its goodbye.
#   netmidi2, host       The NetUMP class invites our endpoint; each has to
#                        show what the other sent, UMP for UMP.
#   netmidi2, client     Our endpoint invites the NetUMP class; the same,
#                        and the class has to be told goodbye.
#
# Neither class implements error recovery that would be exercised here.
# Exit status: 0 when all four passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
work=$(realpath "${1:-$(mktemp -d)}")
hub="$here/build/MIDIHub"
test -x "$hub" || { echo "build/MIDIHub is missing: run make build/MIDIHub"; exit 2; }
mkdir -p "$work"

fetch() {
    if [ ! -d "$work/$1" ]; then
        git clone -q "https://github.com/bbouchez/$1.git" "$work/$1" &&
            git -C "$work/$1" checkout -q "$2" ||
            { echo "cannot fetch $1 at $2"; exit 2; }
    fi
}
fetch BEBSDK 022d0f2
fetch RTP-MIDI 7213cdc
fetch NetUMP f74b0b8

sdk="$work/BEBSDK/network.cpp $work/BEBSDK/SystemSleep.cpp"
g++ -O1 -w -D__TARGET_LINUX__ -I"$work/RTP-MIDI" -I"$work/BEBSDK" \
    -o "$work/bb-rtpmidi-peer" "$here/tests/interop/bb_rtpmidi_peer.cpp" \
    "$work/RTP-MIDI/RTP_MIDI.cpp" "$work/RTP-MIDI/RTP_MIDI_AppleProtocol.cpp" \
    "$work/RTP-MIDI/RTP_MIDI_Input.cpp" $sdk -lpthread
g++ -O1 -w -D__TARGET_LINUX__ -I"$work/NetUMP" -I"$work/BEBSDK" \
    -o "$work/netump-peer" "$here/tests/interop/netump_peer.cpp" \
    "$work/NetUMP/NetUMP.cpp" "$work/NetUMP/NetUMP_SessionProtocol.cpp" \
    $sdk -lpthread
cc -I"$here/include" -O2 -Wall -Wextra -Werror -std=c99 \
    -o "$work/netmidi2-peer" "$here/tests/interop/netmidi2_peer.c" \
    "$here/src/netmidi2.c" "$here/src/ump.c"
status=0

# What the RTP-MIDI class sends, as MIDIHub reports it.
theirs_arrived() {
    grep -q 'RTP-MIDI .*bytes=009045500080454000b0076400f07d01' "$1" &&
    grep -q 'SysEx complete bytes=6' "$1" && ! grep -q 'malformed' "$1"
}

timeout 9 "$work/bb-rtpmidi-peer" 5304 > "$work/am-invite-peer.log" 2>&1 &
sleep 0.5
timeout 7 "$hub" 5404 127.0.0.1 5304 --probe-note > "$work/am-invite-hub.log" 2>&1 || true
wait || true
if grep -q 'session connected' "$work/am-invite-hub.log" &&
   grep -q 'clock exchange completed' "$work/am-invite-hub.log" &&
   grep -q '^midi delta=[0-9]* 903c64$' "$work/am-invite-peer.log" &&
   grep -q '^midi delta=[0-9]* 903c00$' "$work/am-invite-peer.log" &&
   theirs_arrived "$work/am-invite-hub.log"; then
    echo "ok: AppleMIDI, MIDIHub inviting the RTP-MIDI class: notes arrived there, its four messages here"
else
    echo "FAIL: AppleMIDI, MIDIHub inviting the RTP-MIDI class"
    status=1
fi

timeout 9 "$hub" 5414 > "$work/am-invited-hub.log" 2>&1 &
sleep 0.5
timeout 6 "$work/bb-rtpmidi-peer" 5314 127.0.0.1 5414 > "$work/am-invited-peer.log" 2>&1 || true
wait || true
if grep -q 'session connected' "$work/am-invited-hub.log" &&
   grep -q 'clock exchange completed' "$work/am-invited-hub.log" &&
   grep -q '^open$' "$work/am-invited-peer.log" &&
   theirs_arrived "$work/am-invited-hub.log" &&
   grep -q 'peer disconnected' "$work/am-invited-hub.log"; then
    echo "ok: AppleMIDI, the RTP-MIDI class inviting MIDIHub: its four messages arrived, and its goodbye"
else
    echo "FAIL: AppleMIDI, the RTP-MIDI class inviting MIDIHub"
    status=1
fi

# What our endpoint sends, as the NetUMP class prints it, and the reverse.
ours_arrived() {
    grep -q '^ump 20903c64$' "$1" && grep -q '^ump 20803c40$' "$1" &&
    grep -q '^ump 20b00764$' "$1" && grep -q '^ump 30167d01 02030405$' "$1" &&
    grep -q '^ump 30350607 08090a00$' "$1"
}
netump_arrived() {
    grep -q '^midi from=0 903c64$' "$1" && grep -q '^midi from=0 803c40$' "$1" &&
    grep -q '^midi from=0 b00764$' "$1" &&
    grep -q '^midi from=0 f07d0102030405060708090af7$' "$1" &&
    [ "$(grep -c '^midi ' "$1")" = 4 ]
}

timeout 8 "$work/netmidi2-peer" 5873 --send > "$work/nm-host-ours.log" 2>&1 &
sleep 0.4
timeout 5 "$work/netump-peer" 5874 127.0.0.1 5873 > "$work/nm-host-peer.log" 2>&1 || true
wait || true
if grep -q '^established slot=0 role=host peer="NetUMP peer" id="netump-1"' "$work/nm-host-ours.log" &&
   netump_arrived "$work/nm-host-ours.log" && ours_arrived "$work/nm-host-peer.log" &&
   grep -q '^closed slot=0$' "$work/nm-host-ours.log"; then
    echo "ok: Network MIDI 2.0, the NetUMP class as Client of our Host: UMPs both ways, and its Bye freed the slot"
else
    echo "FAIL: Network MIDI 2.0, the NetUMP class as Client of our Host"
    status=1
fi

timeout 9 "$work/netump-peer" 5883 > "$work/nm-client-peer.log" 2>&1 &
sleep 0.5
timeout 5 "$work/netmidi2-peer" 5884 127.0.0.1 5883 --send > "$work/nm-client-ours.log" 2>&1 || true
wait || true
if grep -q '^established slot=0 role=client peer="NetUMP peer" id="netump-1"' "$work/nm-client-ours.log" &&
   netump_arrived "$work/nm-client-ours.log" && ours_arrived "$work/nm-client-peer.log" &&
   grep -q '^bye reason=1$' "$work/nm-client-peer.log"; then
    echo "ok: Network MIDI 2.0, our Client of the NetUMP class as Host: UMPs both ways, and our Bye understood"
else
    echo "FAIL: Network MIDI 2.0, our Client of the NetUMP class as Host"
    status=1
fi
echo "logs: $work"
exit $status
