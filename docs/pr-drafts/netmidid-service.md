<!-- Title: netmidid: a network MIDI service, with AppleMIDI as its first transport -->

Add `netmidid`, a service of the AROSTCP service manager that carries MIDI
between the network and CAMD, and its first transport, AppleMIDI
(RTP-MIDI). The service is no client of camd.library: it attaches to the
adapter interface of the drivers in `DEVS:Midi`, a peer to a port.

The PR has three commits:

1. **`netmidid: a network MIDI service and its transport interface`**
   (`aa6ca46a01`). The core has what every protocol needs alike: the
   configuration (`ENV:NetMIDI`, watched and taken anew when it changes),
   numbered UDP sockets, ports of a driver kept by name for a peer, the
   counts published for a status command, and the loop. A protocol is a
   transport, defined in `netmidid.h` (open, start, receive, next_due,
   periodic, send_midi, flush, counts, stop, release, and the size of its
   state, which the core allocates for each run). The core knows no
   protocol. On its own the service has nothing to run: it says so and
   ends.
2. **`netmidid: AppleMIDI transport`** (`60f807ef96`). It accepts
   AppleMIDI invitations, or invites a configured IPv4 peer and does so
   again after a timeout or the peer's departure. It exchanges clocks and
   gives each session a port of `DEVS:Midi/applemidi`: the one kept for
   its session name, or the first free one. The protocol is the merged
   `rtpmidi` library (#1678). The commit adds `transport_applemidi.c`
   and `config_applemidi.c`, plus one line where transports and their
   keys are registered.
3. **`netmidid: expire silent AppleMIDI peers, and log stderr`**
   (`8939c84655`), from review: a peer silent for
   `APPLEMIDI_CLOCK_TIMEOUT` is ended whichever side invited, the peer
   table gets its own 1/10000 s unit, and stderr goes to
   `ENV:netmidid.log` too.

Configuration is read from `ENV:NetMIDI`, for example:

```ini
local_port=5004
peer_ip=192.168.166.1
peer_port=5104
applemidi.port.3=Kept peer
```

The installed service is disabled (`Enabled=no`). Enabling it opens the
AppleMIDI UDP pair. Network MIDI 2.0 (#1693) is a second transport, added
the same way as its own files. Advertisement (#1743) and `NetMIDIStatus`
(#1742) are separate follow-ups. None of them depends on another, only on
this PR.

The service lives in `workbench/network/netmidid`, alongside the merged
`rtpmidi/` library, and uses the adapter header in
`workbench/devs/camd/networkmidi2`. Its mmakefile owns the network build
dependency.

### Base

Upstream master `a1b8aa6fd8b7ed6e0cae1ecbe83c82ecb905c343`. #1678 (the
AppleMIDI session library), the drivers, the payload library and the CAMD
and netinfo cleanup fixes (#1676, #1677) are merged.

### Validation

Each commit compiles and links for hosted x86-64 and amiga-m68k with
warnings treated as errors (the existing m68k SDK volatile-register
exclusion applies), each one on its own. The RTP-MIDI protocol suite
passes under ASan/UBSan against the session sources merged in #1678.

The exact binary of the last commit passes the hosted UP AROS suite
against node-rtpmidi 1.0.0, over three boots:

- both invitation directions and clock exchange;
- notes, controller and SysEx through CAMD;
- separate ports 0 and 3 with a reserved peer;
- an outbound scale and chord;
- NetMIDIStatus counters;
- service stop;
- an initial invitation timeout, and two successive sessions across the
  peer's departure without BY and return, the service expiring it;
- a configuration error written to stderr reaching `netmidid.log`.

Discovery is skipped because this PR has none. The existing hosted image
and support modules are reused with the new service binary.

Validation uses existing SDKs and a hosted image with replacement modules,
not a fresh full current-master AROS build. Physical hardware, SMP, a
native latency bound and complete RFC 6295/external journal
interoperability remain outside that evidence.
