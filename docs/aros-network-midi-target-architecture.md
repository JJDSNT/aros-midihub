# AROS Network MIDI target architecture

## Status

This document records the current architectural direction for moving the
network MIDI capabilities incubated in MIDIHub into their natural native AROS
home.

It is a **target direction, not a frozen upstream contract**. Exact names,
installation paths and component boundaries should still be reviewed with AROS
maintainers before an upstream submission. The important goal is to develop
MIDIHub so that its code already follows the responsibilities and conventions
of the AROS tree instead of requiring a second implementation later.

This direction is based on the current AROS tree and maintainer guidance, especially:

- `workbench/devs/midi/`, the native source area for CAMD MIDI device
  interfaces;
- `workbench/libs/camd/`, which loads MIDI drivers from `DEVS:Midi` and
  exposes the application-facing MIDI infrastructure;
- the CAMD driver contract in `<midi/camddevices.h>`, where a device may
  initialize internal resources and expose one or more ports through
  `MidiDeviceData`;
- AROSTCP through `bsdsocket.library`, which owns IP, UDP, multicast,
  interface and routing facilities.

The key architectural correction is that implementation complexity does not
make Network MIDI a system service. Stateful discovery, sessions, multiple
peers and persistent network state can legitimately live behind a MIDI device
interface. The application-facing boundary is what determines the abstraction.

See also [Network MIDI integration](network-midi-integration.md) and
[mDNS and AROSTCP](mdns-arostcp.md).

## Architectural boundary

Network MIDI should be exposed to AROS MIDI applications as a CAMD MIDI
device, not as a standalone system-level Network MIDI service. The device
implementation may internally own long-lived sockets, discovery state,
sessions, timers, peer tables and protocol engines.

The target responsibility split is:

```text
AROS MIDI application
        |
    camd.library
        |
 +------+----------------------+
 |                             |
applemidi.device        networkmidi2.device
 |                             |
AppleMIDI / RTP-MIDI    Network MIDI 2.0 / UMP
 |                             |
 +-------------+---------------+
               |
        peer/session runtime
               |
          system mDNS
               |
         bonami.library
               |
       bsdsocket.library
               |
            AROSTCP
```

AROSTCP supplies network transport. Bonami supplies shared zero-configuration
discovery. The transport devices own their respective MIDI network sessions and present
their endpoints/units through the CAMD device interface.

The current target is two transport-specific devices: `applemidi.device` for
AppleMIDI/RTP-MIDI and `networkmidi2.device` for Network MIDI 2.0/UMP. They
may share portable protocol, networking, timing, discovery and utility code
where appropriate, but they remain separate application-facing device
interfaces.

A separate issue is dynamic peer representation. Current CAMD reads
`MidiDeviceData.NPorts` after driver `Init()`, copies it into the driver's
port count, and allocates the corresponding driver data/clusters. There is no
current mechanism to resize that port set at runtime. This may require a
bounded pool of device ports, an internal peer-to-port mapping, or a future
CAMD enhancement for dynamic ports. It is not, by itself, a reason to turn
Network MIDI into a service.

Neither mDNS nor Network MIDI belongs inside AROSTCP itself.

## Proposed native AROS home

During incubation/upstream preparation, the two devices should live under the
AROS `contrib` area, following the maintainer-approved CAMD device packaging
convention. The intended logical split is:

```text
contrib/
|
+-- applemidi/
|   +-- applemidi.device
|   +-- AppleMIDI / RTP-MIDI adapter code
|   +-- mmakefile.src
|
+-- networkmidi2/
    +-- networkmidi2.device
    +-- Network MIDI 2.0 / UMP adapter code
    +-- mmakefile.src
```

Exact contrib paths and MetaMake target names should follow maintainer guidance.
If these components later graduate from contrib, `workbench/devs/midi/` is
the natural system-tree destination for CAMD MIDI device interfaces.

Portable source files may remain grouped in a subdirectory or static support
library if that produces a cleaner MetaMake definition. There is no
requirement to flatten the current portable source tree merely to match this
sketch.

## Device entry/runtime

The current orchestration responsibility in `ports/aros/midihub/main.c`
should be separated into reusable runtime pieces behind the two native MIDI devices rather than into
a standalone `C:NetworkMIDI` process.

That runtime may legitimately:

1. open required AROS libraries, including `bsdsocket.library`;
2. load Network MIDI configuration;
3. initialize the protocol state owned by that device;
4. register and browse services through Bonami/system mDNS;
5. own the required task/event loop internally;
6. dispatch socket, timer, discovery and device-port events;
7. map active peers/sessions onto that device's exposed CAMD endpoints/units;
8. perform orderly shutdown and release all resources.

The important boundary is that applications use the normal CAMD MIDI-device
interface. Internal tasks and persistent network state are implementation
details, not a separate application-facing service.

Protocol parsing and state machines should stay outside the device entry
glue.

## network.c / network.h

This layer adapts the portable network MIDI engines to AROS networking.

Responsibilities include:

- create, bind and close UDP sockets;
- drive AppleMIDI control and RTP-MIDI data traffic;
- drive Network MIDI 2.0 traffic;
- translate socket addresses into the portable peer/session representation;
- feed received datagrams to the existing protocol code;
- send datagrams produced by the portable code;
- expose deadlines/timers required by session maintenance;
- coordinate discovery results with session creation;
- handle interface/address changes where relevant.

It should not implement CAMD policy and should not contain the mDNS responder.

The portable codecs and state machines should continue to avoid direct OS calls
where practical. That separation is one of the main reasons the existing
MIDIHub code is suitable for upstream migration.

## CAMD device boundary

The current `camd.c/.h` extraction remains useful during incubation, but its
role changes: it is preparation for the native CAMD device interface rather
than a client that creates one `MidiNode` per peer.

Each native target should implement the CAMD MIDI driver contract
(`MidiDeviceData`, `Init`, `Expunge`, `OpenPort`, `ClosePort`) and map
its ports/endpoints to the peers and sessions of its own transport internally.

Current CAMD port allocation is static after device initialization. The design
must therefore explicitly validate how discovered peers are assigned to
ports, how identity is surfaced to users, and what happens when peers appear,
disappear or reconnect. A future dynamic-port CAMD extension may improve this,
but the first upstreamable device should not depend on an unimplemented CAMD
feature.

RTP/network timestamps should eventually be mapped to CAMD timing at this
boundary.

## config.c / config.h

Network MIDI-specific configuration should be separated from the process entry
point and protocol engines.

During incubation MIDIHub already uses:

```text
ENV:MidiHub/Network
ENVARC:MidiHub/Network
```

The native AROS device should eventually own its own settings namespace rather
than depend on the MIDIHub product name. A possible shape is:

```text
ENV:NetworkMIDI/network.prefs
ENVARC:NetworkMIDI/network.prefs
```

The exact native path is deliberately not fixed here.

The configuration layer should cover at least:

- local/session name;
- AppleMIDI control/data port policy;
- Network MIDI 2.0 port policy;
- accepting incoming sessions;
- fixed peers to invite;
- discovery/advertisement enablement;
- protocol enablement;
- future per-peer policy where required.

The AROS Envoy Services Manager also demonstrates a useful pattern: use DOS
notification on the `ENV:` file and reload configuration when it changes.
That would allow MIDI Preferences to apply changes without restarting the
device runtime.

## System mDNS remains separate

Bonami/system mDNS is generic AROS network infrastructure and must not become a
private module of Network MIDI.

The intended relationship is:

```text
                    system mDNS
                    /    |    \
                   /     |     \
          AppleMIDI    MIDI 2.0   Envoy/NIPC
              |           |          |
    _apple-midi._udp  _midi2._udp  _nipc._tcp
```

Network MIDI consumes registration, browse and resolve APIs. It does not own
UDP 5353 once the system responder exists.

The current `network/mdns/embedded.c` remains a useful incubation fallback
until that system service is available.

## Mapping from MIDIHub to the target

| MIDIHub today | Native AROS target | Action |
|---|---|---|
| `ports/aros/midihub/main.c` | shared incubation/runtime glue for `applemidi.device` and `networkmidi2.device` | split transport ownership while retaining reusable AROS adapters |
| `src/applemidi.c` | portable engine used by `applemidi.device` | retain/move with minimal OS coupling |
| `src/rtpmidi.c` | portable engine used by `applemidi.device` | retain/move |
| `src/session.c` | session engine | retain/move |
| `src/peers.c` | peer/session manager | retain/move |
| `src/netmidi2.c` | portable engine used by `networkmidi2.device` | retain/move |
| `src/ump.c` | UMP engine/edge translation used by `networkmidi2.device` | retain/move |
| `network/mdns/embedded.c` | none in Network MIDI | remove after system mDNS migration |
| `network/mdns/bonami/` | generic AROS mDNS subsystem | port/upstream separately |
| current CAMD bridge/runtime | native CAMD device boundary | evolve toward `MidiDeviceData` / port callbacks |
| current socket/event glue in `main.c` | `network.c/.h` | extract |
| current network config handling | `config.c/.h` | consolidate |
| current external build | `mmakefile.src` | add for native upstream component |

## Incubation layout in MIDIHub

We should not prematurely duplicate the complete AROS tree inside this
repository. Instead, evolve the current AROS port toward the target boundaries:

```text
ports/aros/
|
+-- midihub/
|   +-- main.c
|
+-- network.c
+-- network.h
+-- camd.c
+-- camd.h
+-- config.c
+-- config.h
```

Exact placement under `ports/aros` may follow the existing build layout.
The important part is responsibility separation.

### Namespace during incubation

The namespace should make the eventual ownership of each layer explicit:

- `mh_*` remains the namespace of portable or legacy code incubated in
  MIDIHub, including the AppleMIDI, RTP-MIDI, session and UMP components;
- `netmidi_*` is used by new AROS-facing Network MIDI runtime code, including
  the `network.c`, `camd.c` and future `config.c` boundaries;
- `aros_*` is used by adapters shared by more than one AROS subsystem, such as
  the CAMD bridge used by Network MIDI and BLE MIDI;
- `mdns_*` and the Bonami public API remain generic system discovery APIs;
- native CAMD calls retain their AROS API names.

This lets the incubating AROS layer move first into contrib device components and later, if accepted as system components, into `contrib `applemidi.device` / `networkmidi2.device`/` primarily
as a source relocation instead of carrying a permanent MIDIHub identity into
the system component. Shorter ambiguous prefixes such as `nm_*` should be
avoided in new public or cross-file interfaces.

The next refactoring should therefore extract code from `main.c` rather than
rewrite the portable protocol engines.

## Build direction

Each contrib device should have a native `mmakefile.src`, following the
existing CAMD-driver/device conventions so later promotion into
`workbench/devs/midi` does not require an architectural rewrite.

It should express dependencies on the normal AROS include/link infrastructure
and on CAMD, without making Network MIDI part of the AROSTCP binary.

A conceptual dependency graph is:

```text
AROS contrib
       |
 +-----+----------------+
 |                      |
applemidi.device   networkmidi2.device
       |
 +-----+----------------+
 |                      |
CAMD/linklibs      system mDNS API
                       |
                 bsdsocket.library
```

The exact MetaMake dependency names depend on where the system mDNS component
lands upstream.

## What not to create

This direction intentionally avoids several unnecessary abstractions:

- no Network MIDI code inside AROSTCP;
- no standalone Network MIDI service merely because the implementation is stateful;
- no MIDI-specific mDNS implementation in the final architecture;
- no permanent dependency on the MIDIHub application name;
- no second implementation of AppleMIDI/RTP-MIDI for upstream;
- no mandatory Envoy/NIPC dependency;
- no requirement that one discovered peer equal one independently loaded device file.

The device interface should remain small while the existing portable engines
own protocol complexity internally.

## Immediate next work: two parallel tracks

Development should now advance on two parallel tracks rather than making one
block the other.

### Track A - Network MIDI AROS runtime

Refactor `ports/aros/midihub/main.c` toward the native target boundaries:

1. extract CAMD peer lifecycle into `camd.c/.h`;
2. extract AROS socket/event-loop adaptation into `network.c/.h`;
3. consolidate network settings in `config.c/.h`;
4. keep `main.c` primarily as orchestration;
5. connect the existing multi-peer engines so established peers can obtain
   independent CAMD representations.

This work can continue using `network/mdns/embedded.c` and therefore does
not depend on completion of the Bonami port.

### Track B - Bonami / system mDNS

In parallel, continue the Bonami port as generic AROS infrastructure:

1. make the Bonami core build and run correctly on current AROS;
2. complete interface enumeration and address-change handling;
3. use current AROSTCP multi-interface/scoped multicast facilities;
4. add/validate IPv6 mDNS;
5. validate register, browse and resolve behavior needed by
   `_apple-midi._udp` and `_midi2._udp`;
6. keep the optional Envoy/NIPC bridge separate from the Network MIDI
   dependency.

### Convergence point

The tracks converge only at the discovery boundary:

```text
Track A                              Track B
Network MIDI runtime                Bonami/system mDNS
        |                                   |
        +------------- discovery API -------+
                           |
             replace embedded.c fallback
```

The Network MIDI runtime should therefore avoid depending on details of the
embedded responder. Discovery should have a small replaceable boundary so the
working fallback can later be exchanged for the system mDNS API without
rewriting AppleMIDI, RTP-MIDI, Network MIDI 2.0 or CAMD integration.

This parallel plan is the immediate next development direction.

## Implementation sequence

### Phase 1 - establish native boundaries in MIDIHub

1. Extract CAMD peer lifecycle from `ports/aros/midihub/main.c` into
   `camd.c/.h`.
2. Extract AROS socket/event-loop adaptation into `network.c/.h`.
3. Consolidate settings parsing and reload behavior in `config.c/.h`.
4. Keep `main.c` as the small process orchestrator.
5. Connect the existing portable multi-peer manager so every established peer
   can receive its own CAMD representation.

### Phase 2 - prove the device model

6. Validate bidirectional AppleMIDI with macOS/iOS and rtpMIDI peers.
7. Validate peer arrival, departure and reconnection without stale CAMD nodes.
8. Validate Network MIDI 2.0 through the same event loop.
9. Validate configuration reload.
10. Replace embedded discovery with the system mDNS API when available.

### Phase 3 - prepare upstream

11. Package the transport-specific adapters as `applemidi.device` and
    `networkmidi2.device` under the agreed AROS contrib locations.
12. Add native `mmakefile.src`.
13. Rename the settings namespace away from MIDIHub.
14. Ensure startup ordering waits for a `bsdsocket.library` provider without
    depending specifically on AROSTCP.
15. Submit Network MIDI independently from generic mDNS where practical, with
    the dependency/interface between the two explicit.

## Upstream-readiness criteria

The component is a strong upstream candidate when:

- AppleMIDI/RTP-MIDI works bidirectionally with more than one peer;
- peers appear and disappear cleanly in CAMD;
- no portable protocol module depends directly on AROS APIs unnecessarily;
- the device runtime survives peer and interface churn;
- settings can be loaded and changed without MIDIHub-specific assumptions;
- discovery is behind a replaceable API so the embedded responder can be
  removed cleanly;
- AppleMIDI/RTP-MIDI is exposed through `applemidi.device` and Network MIDI 2.0/UMP through `networkmidi2.device`;
- build and startup integration can be expressed with normal AROS MetaMake and
  system conventions.

## Guiding principle

MIDIHub is the incubator, not the permanent owner.

The goal is to make each refactoring step useful in the current repository
while steadily making the resulting source files look like code that can be
moved into AROS with minimal architectural change:

```text
MIDIHub incubation
       |
       | prove + separate responsibilities
       v
native-shaped AROS port
       |
       | MetaMake + system mDNS + native settings
       v
contrib `applemidi.device` / `networkmidi2.device`
```
