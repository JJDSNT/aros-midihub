# Network MIDI in AROS: RTP-MIDI, AppleMIDI and UMP

A proposal for where MIDIHub's network transports belong in AROS, in the way
BLE MIDI found its place as `btmidi.class` in the Bluetooth stack. It follows
the project's rule: find each capability's native AROS home rather than keep
it inside MIDIHub.

## What exists

| part | file | state |
|---|---|---|
| AppleMIDI session (IN, OK, NO, BY, CK, RS) | `src/applemidi.c`, `src/session.c`, `src/peers.c` | up to eight peers, either role; host tests |
| RTP-MIDI payload, recovery journal | `src/rtpmidi.c`, `src/sender.c`, `src/timing.c` | chapters N, C, P, W, M, E, Q, F, X; host tests |
| mDNS for `_apple-midi._udp` | `network/mdns/embedded.c` | interim embedded advertiser and query responder |
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
control and RTP-MIDI carries the MIDI payload and recovery journal. Network
MIDI 2.0/UMP is a second transport family. Whether AROS should expose both
through one `networkmidi.device`-style interface or through separate
`applemidi.device` and `networkmidi2.device` interfaces remains an upstream
design choice.

The expected native source home follows the existing MIDI-driver convention
under `workbench/devs/midi/`, rather than `workbench/network/midi`.

### Dynamic peers versus CAMD ports

The current CAMD driver ABI reads `MidiDeviceData.NPorts` after the driver's
`Init()`, stores that port count, and allocates the corresponding driver
state and clusters. It does not currently resize the device's port set when
network peers later appear or disappear.

That means the first Network MIDI device needs an explicit peer-to-port policy,
for example a bounded pool of ports whose backing peer/session can change.
The existing Poseidon USB MIDI implementation provides a useful precedent: it
exposes a fixed pool of CAMD ports while hotplug and hardware state are handled
behind the driver boundary.

A future CAMD enhancement could provide truly dynamic device ports, which
would also be useful for other hotplug transports. That is a separate CAMD
evolution question, not a reason to expose Network MIDI as a standalone
service.

### Internal runtime

The device implementation may own an internal event loop/task that handles:

- AppleMIDI control and RTP-MIDI data sockets;
- Network MIDI 2.0 traffic;
- Bonami/system mDNS browse and registration events;
- peer/session lifecycle and reconnection;
- timers and clock synchronization;
- mapping between CAMD ports and active network peers.

This persistent state remains an implementation detail behind the MIDI device
interface.

## mDNS: the missing system piece

AppleMIDI advertises `_apple-midi._udp` and Network MIDI 2.0 advertises
`_midi2._udp`, with TXT keys `UMPEndpointName` and `ProductInstanceId`.
AROS has no system mDNS responder, which is why MIDIHub carries a minimal
one.

The right home is a **system responder** shared by every client. The preferred
starting point is Bonami's BSD-licensed Amiga design: `bonami.library` exposes
register, browse, and resolve operations while one engine task owns the socket,
cache, and timers, and is the natural owner for probing and conflict state as
those features are completed. Library calls reach that engine through private
Exec message-port IPC. If each client embeds its own responder, several
processes compete for UDP port 5353 and duplicate all that state.

AROSTCP already supplies the required IPv4 multicast transport,
`IP_ADD_MEMBERSHIP`, `SO_REUSEADDR`, and `SO_REUSEPORT`. Its current
multi-interface support also provides scoped IPv6 link-local/multicast output
and `SO_BINDTODEVICE` for per-interface UDP sockets, giving the future system
responder the transport primitives needed to add IPv6 mDNS without changing
the Network MIDI architecture. Its multicast bind
rules only treat `SO_REUSEADDR` like `SO_REUSEPORT` when the socket is bound
to a multicast address. The current MIDIHub code binds `INADDR_ANY:5353`, so
`SO_REUSEADDR` alone is insufficient for multiple embedded responders. This
is another reason to use one system responder instead of making discovery a
property of each transport process. See [mDNS and AROSTCP](mdns-arostcp.md).

This is worth proposing upstream on its own. Printers and file sharing would
use it too. AROS now also has a clean-room Envoy-compatible `nipc.library`
(commit `fbc2e274d880868c3fe447dffdfc9226fae0bd66`). Bonami already contains
an optional NIPC discovery bridge using `_nipc._tcp`, so the same system mDNS
service can potentially support Envoy/NIPC discovery as well. This is another
reason to keep Bonami generic infrastructure rather than make it MIDI-specific;
AROS has subsequently added `services.library`, a Services Manager,
`accounts.library`, and an Accounts Server, with `nipc.library` answering
service inquiries from the native service list. Bonami's NIPC bridge should
therefore complement rather than replace that native Envoy inquiry path.
NIPC/Envoy remains optional and is not a dependency of Network MIDI.

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
2. Connect the portable multi-peer AppleMIDI manager to the AROS event loop
   and create a CAMD node per connected peer.
3. The AROS device runtime: validate AppleMIDI and Network MIDI 2.0 behind the CAMD device boundary, using the system mDNS registration and browse API.
4. Upstream proposals: the system mDNS responder; the Network MIDI device under
   `workbench/devs/midi`; then the UMP-aware CAMD.

Interoperability targets for step 3: macOS and iOS Network MIDI (AppleMIDI);
rtpMIDI on Windows; for Network MIDI 2.0, Windows MIDI Services and the MIDI
2.0 Workbench.
