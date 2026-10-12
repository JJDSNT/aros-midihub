# Network MIDI in AROS: RTP-MIDI, AppleMIDI and UMP

A proposal for where MIDIHub's network transports belong in AROS, in the way
BLE MIDI found its place as `btmidi.class` in the Bluetooth stack. It follows
the project's rule: find each capability's native AROS home rather than keep
it inside MIDIHub.

## What exists

| part | file | state |
|---|---|---|
| AppleMIDI session (IN, OK, NO, BY, CK, RS) | `network/rtpmidi/applemidi.c`, `network/rtpmidi/session.c`, `network/rtpmidi/peers.c` | up to eight peers, either role; host tests |
| RTP-MIDI payload, recovery journal | `network/rtpmidi/rtpmidi.c`, `network/rtpmidi/sender.c`, `network/rtpmidi/timing.c` | chapters N, C, P, W, M, E, Q, F, X; host tests |
| mDNS for `_apple-midi._udp` | `ports/aros/midihub/discovery.c` | registered through the system service (`bonami.library`, `mdnsd`); the embedded responder is gone; peers are not browsed for yet |
| Universal MIDI Packet, MIDI 1.0 translation | `src/ump.c` | host tests |
| Network MIDI 2.0 (UDP), M2-124-UM | `src/netmidi2.c` | Host and Client sessions, FEC; host tests |
| AROS program bridging to CAMD | `ports/aros/midihub/main.c` | AppleMIDI, one active peer, `MIDIHub In`/`Out` |

The codecs and session state machines make no OS calls: the caller owns the
sockets, the addresses and the clock. That is what lets them move.

## Network MIDI as a CAMD device interface

The native AROS abstraction should be a CAMD MIDI device interface rather than
a standalone system-level Network MIDI service. Applications that use CAMD
should continue to see and open a MIDI device; the implementation behind that
device may internally manage sockets, long-lived tasks, discovery, multiple
peers, sessions and transports.

RTP-MIDI and AppleMIDI remain one protocol family: AppleMIDI provides session
control and RTP-MIDI carries the MIDI payload and recovery journal. They are
exposed through `applemidi.device`. Network MIDI 2.0/UMP is a second
transport family and is exposed through `networkmidi2.device`.

The devices should initially be packaged under AROS `contrib` while their
interfaces, interoperability and peer lifecycle are validated. If they later
graduate into the system tree, the native MIDI-device area under
`workbench/devs/midi/` is the natural destination.

### Dynamic peers versus CAMD ports

The current CAMD driver ABI reads `MidiDeviceData.NPorts` after the driver's
`Init()`, stores that port count, and allocates the corresponding driver
state and clusters. It does not currently resize the device's port set when
network peers later appear or disappear.

That means each network MIDI device needs an explicit peer-to-port policy,
for example a bounded pool of ports whose backing peer/session can change.
The existing Poseidon USB MIDI implementation provides a useful precedent: it
exposes a fixed pool of CAMD ports while hotplug and hardware state are handled
behind the driver boundary.

A future CAMD enhancement could provide truly dynamic device ports, which
would also be useful for other hotplug transports. That is a separate CAMD
evolution question, not a reason to expose Network MIDI as a standalone
service.

### Internal runtime

Each device implementation may own an internal event loop/task that handles its transport responsibilities, including:

- AppleMIDI control and RTP-MIDI data sockets in `applemidi.device`;
- Network MIDI 2.0 traffic in `networkmidi2.device`;
- Bonami/system mDNS browse and registration events;
- peer/session lifecycle and reconnection;
- timers and clock synchronization;
- mapping between CAMD ports and active network peers.

This persistent state remains an implementation detail behind each MIDI device
interface. Applications use CAMD to discover/select the exposed endpoints or
units and decide how to bind or use them; they do not own transport discovery
or session management.

## mDNS: the missing system piece

AppleMIDI advertises `_apple-midi._udp` and Network MIDI 2.0 advertises
`_midi2._udp`, with TXT keys `UMPEndpointName` and `ProductInstanceId`.
AROS had no system mDNS responder, which is why MIDIHub carries a minimal
one.

The right home is a **system responder** shared by every client, and it now
exists in this repository: `mdnsd`, a daemon managed by the AROSTCP service
manager, with `bonami.library` as its client library, ported from Bonami. It
runs and is tested on hosted AROS; see
[`network/mdns`](../network/mdns/README.md). If each client embedded its own
responder, several processes would compete for UDP port 5353 and each keep
its own cache, names and conflict state.

AppleMIDI uses it since 2026-10-12: the session is registered as
`_apple-midi._udp` through `bonami.library`, and the embedded responder,
which could not run beside mdnsd, is gone. What remains is to browse for
peers and to register `_midi2._udp`. See
[mDNS and AROSTCP](mdns-arostcp.md).

The service is generic infrastructure: printers and file sharing can use it,
and it carries Bonami's bridge for Envoy discovery, which AROS's
`nipc.library` does not call yet
([notes](nipc-envoy-future.md)). NIPC/Envoy remains optional and is not a
dependency of Network MIDI.

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

1. Finish the RTP-MIDI work in progress. What is owed is listed, with
   priorities, in four backlogs: [RTP-MIDI](rtp-midi-backlog.md) (the
   payload and its journal), [AppleMIDI](applemidi-backlog.md) (sessions,
   peers, discovery, the runtime and the driver), [UMP](ump-backlog.md) and
   [Network MIDI 2.0](network-midi2-backlog.md); beside them,
   [BLE MIDI](ble-midi-backlog.md), [mDNS](mdns-backlog.md) and
   [CAMD](camd-backlog.md) have theirs. How their items depend
   on one another, and the order that follows, is in
   [the dependency graph](backlog-dependencies.md).
2. Connect the portable multi-peer AppleMIDI manager to the AROS event loop
   and give each connected peer a port of a `DEVS:Midi` driver, as
   `btmidi.class` does since its upstream review; a CAMD node per peer is
   what that review turned down (see
   [the CAMD integration design](camd-integration.md)).
3. The AROS device runtime: validate AppleMIDI and Network MIDI 2.0 behind the CAMD device boundary, using the system mDNS registration and browse API.
4. Upstream proposals: the system mDNS responder; the Network MIDI device under
   `workbench/devs/midi`; then the UMP-aware CAMD.

Interoperability targets for step 3: macOS and iOS Network MIDI (AppleMIDI);
rtpMIDI on Windows; for Network MIDI 2.0, Windows MIDI Services and the MIDI
2.0 Workbench.
