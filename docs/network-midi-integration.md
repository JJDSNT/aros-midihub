# Network MIDI in AROS: RTP-MIDI, AppleMIDI and UMP

A proposal for where MIDIHub's network transports belong in AROS, in the way
BLE MIDI found its place as `btmidi.class` in the Bluetooth stack. It follows
the project's rule: find each capability's native AROS home rather than keep
it inside MIDIHub.

## What exists

| part | file | state |
|---|---|---|
| AppleMIDI session (IN, OK, NO, BY, CK, RS) | `src/applemidi.c`, `src/session.c` | one peer, either role; host tests |
| RTP-MIDI payload, recovery journal | `src/rtpmidi.c`, `src/sender.c`, `src/timing.c` | chapters N, C, P, W, M, E, Q, F, X; host tests |
| mDNS for `_apple-midi._udp` | `src/mdns.c` | advertises, answers browse queries |
| Universal MIDI Packet, MIDI 1.0 translation | `src/ump.c` | host tests |
| Network MIDI 2.0 (UDP), M2-124-UM | `src/netmidi2.c` | Host and Client sessions, FEC; host tests |
| AROS program bridging to CAMD | `ports/aros/midihub/main.c` | AppleMIDI, one peer, `MIDIHub In`/`Out` |

The codecs and session state machines make no OS calls: the caller owns the
sockets, the addresses and the clock. That is what lets them move.

## One network MIDI service

RTP-MIDI and AppleMIDI are one component: AppleMIDI is the session, RTP-MIDI
the payload and journal inside it. Network MIDI 2.0 plays the same role for
MIDI 2.0. All three belong in a single **network MIDI service**.

BLE MIDI became a *class* because the Bluetooth stack has a class mechanism
that loads, binds to devices and reports events. AROSTCP has nothing
equivalent, so the natural form here is a resident service:

- **Process:** one process, started when the network comes up, from
  AROSTCP's startup the way `BTStackLoader` brings up Bluetooth. It owns the
  UDP sockets (AppleMIDI control and data ports, Network MIDI 2.0 port) and
  one event loop.
- **Source location:** for example `workbench/network/midi`, with the
  portable codecs (`applemidi`, `rtpmidi`, `ump`, `netmidi2`) as a static link
  library, as `btcore` is the portable part of the Bluetooth stack.
- **One CAMD node per peer, named after it:** "MacBook In/Out", "iPad (MIDI
  2.0)". This is the model of `btmidi.class`'s central role (a node per bound
  device), not the single `MIDIHub In/Out` of today.
- **Settings:** session name, ports, fixed peers to invite, whether to accept
  invitations. They are stored in `ENVARC:` and edited on the Network page of
  a MIDI Preferences.

A `DEVS:Midi` driver does not fit: its port count is fixed when it loads,
its port names are static, and it runs in callbacks where sockets are not
available. Peers come and go; CAMD client nodes follow them.

### What CAMD gives it

Running as CAMD clients, the peers are ordinary clusters. camd.library 42
already helps:

- MIDIHub.prefs and routes follow peers appearing and leaving through
  cluster watches and `MIDI_PartSignal`.
- The service can stamp received messages with `CamdTime()` once it maps
  RTP timestamps to that clock.

Two further CAMD steps would make network peers first-class:

- `MLINK_Comment`, so applications see "Network MIDI" or "Network MIDI 2.0"
  without knowing the transport ([CAMD improvements](camd-improvements.md),
  step 1).
- Registered virtual ports (step 6), so a peer's ports have an identity of
  their own rather than looking like an application.

## mDNS: the missing system piece

AppleMIDI advertises `_apple-midi._udp` and Network MIDI 2.0 advertises
`_midi2._udp`, with TXT keys `UMPEndpointName` and `ProductInstanceId`.
AROS has no system mDNS responder, which is why MIDIHub carries a minimal
one.

The right home is a **system responder** shared by every service, for example
an `mdns.library` or a resident responder with a register/browse API. If each
service embeds its own, several processes compete for UDP port 5353. That
only works if AROSTCP supports `SO_REUSEADDR` with multicast membership on a
shared port, which should be checked and, if missing, fixed there.

This is worth proposing upstream on its own. Printers and file sharing would
use it too.

## UMP: translate at the edge first

CAMD carries MIDI 1.0: a `MidiMsg` is 32 bits of status and data, while a
MIDI 2.0 Channel Voice message is 64. So, in two steps:

1. **Now: translation at the transport.** A Network MIDI 2.0 peer appears in
   CAMD as a MIDI 1.0 port. The service declares the MIDI 1.0 protocol in its
   Endpoint Info and Stream Configuration, so peers send MIDI 1.0 Channel
   Voice in UMP (message type 2), which maps to CAMD without loss. MIDI 2.0
   Channel Voice that still arrives is translated down by `src/ump.c`:
   - velocities and controllers are scaled;
   - RPN, NRPN and bank select become their Control Change sequences;
   - per-note messages are dropped.
2. **Later: a UMP-aware CAMD.** New calls in a later version (43) would send
   and receive UMP with groups and function blocks. Existing clients would
   keep getting MIDI 1.0 through translation, as CoreMIDI on macOS and
   Windows MIDI Services do. USB MIDI 2.0 in Poseidon (the class's alternate
   setting 1) would feed the same API. This is a large change: it should
   follow a MIDI 2.0 transport that already works through translation.

## Order

1. Finish the RTP-MIDI work in progress.
2. AppleMIDI with several peers (a session manager over `session.c`, which
   already answers invitations) and a CAMD node per peer.
3. The AROS service: AppleMIDI and Network MIDI 2.0 on one event loop and
   one mDNS responder, as a CAMD client per peer, started with the network.
4. Upstream proposals: the system mDNS responder; the network MIDI service in
   `workbench/network`; then the UMP-aware CAMD.

Interoperability targets for step 3: macOS and iOS Network MIDI (AppleMIDI);
rtpMIDI on Windows; for Network MIDI 2.0, Windows MIDI Services and the MIDI
2.0 Workbench.
