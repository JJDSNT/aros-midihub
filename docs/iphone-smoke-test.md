# iPhone AppleMIDI smoke test

This checks interoperability with an actual iPhone. MIDI Wrench is useful for
monitoring received notes and sending notes with its on-screen keyboard.
GarageBand can be tried after the connection works; its iPhone MIDI routing
is less useful as an initial protocol diagnostic.

The current WSL installation uses NAT. A connection initiated by the Linux
MIDIHub process toward the iPhone may work over the outbound path. An iPhone
invitation toward the WSL listener is not expected to reach WSL directly in
this mode. The Linux-hosted AROS instance has AROSTCP loopback but no external
TAP interface configured, so this first phone test exercises the portable
AppleMIDI implementation on Linux, not CAMD in AROS.

1. Put the iPhone and Windows PC on the same Wi-Fi network. Find the iPhone's
   IPv4 address under Settings > Wi-Fi > the connected network's information.
2. Open MIDI Wrench and keep it in the foreground. Its CoreMIDI network
   session must be enabled and accept invitations for this test. If the app
   does not expose that control and no invitation is accepted, use an iOS
   app that explicitly enables the Network MIDI session.
3. From WSL, run `build/MIDIHub 5004 IPHONE_IP 5004 --probe-note` in this
   repository, replacing `IPHONE_IP` with the address from step 1. The first
   `5004` is MIDIHub's local control port, and the second is the iPhone's
   assumed network session control port; the actual iPhone port may differ.
4. Look for `session connected` and `clock exchange completed` in the WSL
   terminal, then Note On and Note Off in MIDI Wrench. Play its virtual
   keyboard to send notes back and look for `RTP-MIDI ... bytes=90...` in
   MIDIHub's output. MIDIHub's Linux test binary does not render audio.

If no session connects, verify that the iOS network session is enabled, its
control port, Wi-Fi client isolation, and outbound UDP reachability. AppleMIDI
uses two consecutive UDP ports, so the data port is one above the control
port. AROSTCP plus TAP and host routing are needed before the same phone test
can exercise CAMD inside AROS.
