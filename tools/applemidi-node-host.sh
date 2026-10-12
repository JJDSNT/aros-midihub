#!/bin/sh
# Runs the Linux build of MIDIHub against node-rtpmidi, an AppleMIDI
# implementation that shares no code with ours, over the loopback
# interface. No AROS and no hardware are involved: this is the cheapest
# check that another implementation understands us and we it.
#
#   tools/applemidi-node-host.sh [WORK_DIR]
#
# It needs node and npm, and the network once, to fetch the package into
# WORK_DIR (kept there between runs). The package's optional mDNS parts are
# left out, so nothing is advertised or browsed; addresses are given.
#
#   invite   MIDIHub invites the other side and sends a Note On and a
#            Note Off. The other side has to show both.
#   invited  The other side invites MIDIHub and sends a Note On, a Note
#            Off, a Control Change and a SysEx message in one packet, with
#            delta times and the RTP marker bit clear. MIDIHub has to take
#            the packet, complete the SysEx and acknowledge.
#
# node-rtpmidi does not implement the recovery journal, so no loss is
# repaired here in either direction.
# Exit status: 0 when both passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
work=$(realpath "${1:-$(mktemp -d)}")
hub="$here/build/MIDIHub"
peer="$here/tests/interop/node_rtpmidi_peer.js"

test -x "$hub" || { echo "build/MIDIHub is missing: run make build/MIDIHub"; exit 2; }
mkdir -p "$work/node"
if [ ! -d "$work/node/node_modules/rtpmidi" ]; then
    (cd "$work/node" && npm init -y > /dev/null 2>&1 &&
     npm install --omit=optional --ignore-scripts --no-audit --no-fund \
         rtpmidi@1.0.0 > "$work/npm.log" 2>&1) ||
        { echo "cannot install node-rtpmidi (see $work/npm.log)"; exit 2; }
fi
export NODE_PATH="$work/node/node_modules"
# Whether the sequence number the other side says we acknowledged is the
# one of the packet it sent.
acknowledged() {
    sent=$(sed -n 's/.*RTP-MIDI seq=\([0-9]*\) .*/\1/p' "$work/invited-hub.log" 2> /dev/null | tail -1)
    test -n "$sent" && grep -q "is at $sent\\." "$work/invited-peer.log"
}

status=0

# invite: the other side on 5104, MIDIHub on 5204
timeout 8 node "$peer" 5104 > "$work/invite-peer.log" 2>&1 &
sleep 1
timeout 6 "$hub" 5204 127.0.0.1 5104 --probe-note > "$work/invite-hub.log" 2>&1 || true
wait || true
if grep -q 'session connected' "$work/invite-hub.log" &&
   grep -q 'clock exchange completed' "$work/invite-hub.log" &&
   grep -q '^message 903c64$' "$work/invite-peer.log" &&
   grep -q '^message 903c00$' "$work/invite-peer.log"; then
    echo "ok: MIDIHub invited node-rtpmidi, exchanged clocks, and its Note On and Note Off arrived"
else
    echo "FAIL: MIDIHub inviting node-rtpmidi"
    status=1
fi

# invited: MIDIHub on 5214, the other side on 5114
timeout 12 "$hub" 5214 > "$work/invited-hub.log" 2>&1 &
sleep 0.5
timeout 9 node "$peer" 5114 127.0.0.1 5214 > "$work/invited-peer.log" 2>&1 || true
wait || true
if grep -q 'session connected' "$work/invited-hub.log" &&
   grep -q 'clock exchange completed' "$work/invited-hub.log" &&
   grep -q 'RTP-MIDI .*bytes=009045500080454000b0076400f07d01' "$work/invited-hub.log" &&
   grep -q 'SysEx complete bytes=6' "$work/invited-hub.log" &&
   ! grep -q 'malformed' "$work/invited-hub.log" &&
   grep -q '^control receiver_feedback$' "$work/invited-peer.log" &&
   acknowledged &&
   grep -q 'peer disconnected' "$work/invited-hub.log"; then
    echo "ok: node-rtpmidi invited MIDIHub; its packet of four commands was taken, the SysEx completed and acknowledged, and its goodbye ended the session"
else
    echo "FAIL: node-rtpmidi inviting MIDIHub"
    status=1
fi
echo "logs: $work"
exit $status
