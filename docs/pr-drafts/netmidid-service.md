Add an AROSTCP-managed service that accepts AppleMIDI invitations or invites a configured IPv4 peer, exchanges clocks, and connects each session to a port of `DEVS:Midi/applemidi`. The daemon attaches through the driver's adapter interface rather than creating CAMD application links. It resumes invitations after timeout or peer departure.

Configuration is read from `ENV:NetMIDI`, for example:

```ini
local_port=5004
peer_ip=192.168.166.1
peer_port=5104
```

The installed service is disabled (`Enabled=no`). Enabling it opens its UDP
pair. This cut is AppleMIDI-only; discovery and Network MIDI 2.0 are separate
changes. `NetMIDIStatus` remains a separate follow-up.

The service lives in `workbench/network/netmidid`, alongside the merged
`rtpmidi/` library, and uses the adapter header in
`workbench/devs/camd/networkmidi2`. Its mmakefile owns the network build dependency.

### Prerequisites and base (2026-10-20)

#1678 is now merged, as are the drivers, payload library and CAMD cleanup
prerequisites. This branch is rebased onto upstream master
`a1b8aa6fd8b7ed6e0cae1ecbe83c82ecb905c343` and contains only the two service
commits. The already merged session implementation is no longer duplicated
in this PR's diff. The service source is unchanged by the rebase.

The exact service revision compiles and links for hosted x86-64 and
amiga-m68k with warnings treated as errors (the existing m68k SDK
volatile-register exclusion applies). The nine RTP-MIDI library translation
units compile for both targets, and the protocol suite passes under
ASan/UBSan against the session sources merged in #1678.

Earlier hosted UP AROS tests against node-rtpmidi covered both invitation
directions, clocks, notes/controllers/SysEx through CAMD, two peers on
separate ports, configured port reservation, stop and reconnect. Driver
unload/replacement and service restart used the separate CAMD/netinfo
cleanup fixes: #1676 is merged and #1677 remains open for review.

Validation uses existing SDKs and a hosted image with replacement modules,
not a fresh full current-master AROS build. Physical hardware, SMP, a native
latency bound and complete RFC 6295/external journal interoperability remain
outside that evidence.

### Fresh hosted service run (2026-10-20)

The exact rebased AppleMIDI-only binary passes the three-boot node-rtpmidi
1.0.0 suite on hosted UP AROS: both invitation directions and clock exchange,
notes/controller/SysEx through CAMD, separate ports 0 and 3 with a reserved
peer, outbound scale and chord, NetMIDIStatus counters, service stop, initial
invitation timeout and two successive sessions across peer departure/return.
Discovery is skipped because this cut does not implement it. The existing
hosted image and support modules are reused with the new service binary.
