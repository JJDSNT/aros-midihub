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

This direction is based on the current AROS tree, especially:

- `workbench/network/envoy/`, where NIPC, Services and Accounts are split into
  coherent network subsystem components with their own MetaMake definitions;
- `workbench/network/envoy/services/tools/ServicesManager.c`, which provides a
  useful precedent for a resident network-facing process with a Wait-based
  event loop and live configuration;
- `workbench/libs/camd/`, whose `CreateMidiA()` and `AddMidiLinkA()` APIs
  provide the native MIDI integration point;
- AROSTCP through `bsdsocket.library`, which owns IP, UDP, multicast,
  interface and routing facilities.

See also [Network MIDI integration](network-midi-integration.md) and
[mDNS and AROSTCP](mdns-arostcp.md).

## Architectural boundary

Network MIDI should be an AROS network subsystem using CAMD, not an AROSTCP
protocol implementation and not a permanent MIDIHub-owned application.

The target responsibility split is:

```text
                         NetworkMIDI
                              |
              +---------------+---------------+
              |               |               |
           network          CAMD           config
              |
      +-------+--------+
      |       |        |
 AppleMIDI RTP-MIDI Network MIDI 2.0
      |                  |
      +--------+---------+
               |
          system mDNS
               |
             Bonami
               |
       bsdsocket.library
               |
            AROSTCP
```

AROSTCP supplies transport. A system mDNS service supplies shared
zero-configuration discovery. Network MIDI owns MIDI network sessions and
their lifecycle. CAMD exposes those sessions to MIDI applications.

Neither mDNS nor Network MIDI should be built into AROSTCP itself.

## Proposed native AROS home

The current target is a component under `workbench/network`, following the
same broad placement principle as Envoy:

```text
workbench/network/midi/
|
+-- NetworkMIDI.c
+-- network.c
+-- network.h
+-- camd.c
+-- camd.h
+-- config.c
+-- config.h
+-- applemidi.c
+-- applemidi.h
+-- rtpmidi.c
+-- rtpmidi.h
+-- session.c
+-- session.h
+-- peers.c
+-- peers.h
+-- netmidi2.c
+-- netmidi2.h
+-- ump.c
+-- ump.h
+-- [sender/timing and other portable support files]
+-- mmakefile.src
```

This is illustrative. Portable source files may remain grouped in a subdirectory
or static support library if that produces a cleaner MetaMake definition.
There is no requirement to flatten the current portable source tree merely to
match this sketch.

## NetworkMIDI.c

`NetworkMIDI.c` is the target resident process entry point. During incubation,
the equivalent responsibility remains in `ports/aros/midihub/main.c`.

Its job should be orchestration rather than protocol implementation:

1. open required AROS libraries, including `bsdsocket.library` and
   `camd.library`;
2. load Network MIDI configuration;
3. initialize AppleMIDI/RTP-MIDI and Network MIDI 2.0 state;
4. register and browse services through the system mDNS API when available;
5. own the AROS event loop;
6. dispatch socket, CAMD, timer, discovery and configuration events;
7. create/remove peer integration as sessions appear and disappear;
8. perform orderly shutdown and release all resources.

The AROS Envoy Services Manager is a useful structural precedent: a small
program opens its subsystem libraries, creates its resources, watches
configuration, then spends most of its lifetime in `Wait()` processing
signals.

A conceptual loop is:

```text
Wait()
 |
 +-- AppleMIDI control socket
 +-- RTP-MIDI data socket
 +-- Network MIDI 2.0 socket
 +-- mDNS/discovery event
 +-- CAMD event
 +-- timer/session maintenance
 +-- configuration change
 +-- shutdown
```

Protocol parsing and state machines should stay outside this file.

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

## camd.c / camd.h

This is the AROS MIDI-facing adapter.

Its central responsibility is to make network peers ordinary CAMD-visible MIDI
participants.

For each active peer it should own the necessary `MidiNode` and links using
the native CAMD API, including `CreateMidiA()`, `AddMidiLinkA()`,
`RemoveMidiLink()` and `DeleteMidi()`.

The target lifecycle is:

```text
peer discovered / configured
          |
session established
          |
create CAMD representation
          |
 +--------+--------+
 |                 |
network RX      CAMD TX
 -> CAMD        -> network
 |                 |
 +--------+--------+
          |
session ends
          |
remove CAMD representation
```

The current fixed `MIDIHub In` / `MIDIHub Out` bridge is an incubation
mechanism. The target is a useful identity per connected peer, with names
derived from the peer/session where possible.

This layer should also be the place where RTP/network timestamps are eventually
mapped to CAMD timing.

## config.c / config.h

Network MIDI-specific configuration should be separated from the process entry
point and protocol engines.

During incubation MIDIHub already uses:

```text
ENV:MidiHub/Network
ENVARC:MidiHub/Network
```

The native AROS service should eventually own its own settings namespace rather
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
service.

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
| `ports/aros/midihub/main.c` | `NetworkMIDI.c` | progressively reduce to orchestration/event loop |
| `src/applemidi.c` | AppleMIDI portable component | retain/move with minimal OS coupling |
| `src/rtpmidi.c` | RTP-MIDI portable component | retain/move |
| `src/session.c` | session engine | retain/move |
| `src/peers.c` | peer/session manager | retain/move |
| `src/netmidi2.c` | Network MIDI 2.0 engine | retain/move |
| `src/ump.c` | UMP edge translation | retain/move |
| `network/mdns/embedded.c` | none in Network MIDI | remove after system mDNS migration |
| `network/mdns/bonami/` | generic AROS mDNS subsystem | port/upstream separately |
| current CAMD bridge in `main.c` | `camd.c/.h` | extract |
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

This lets the incubating AROS layer move to `workbench/network/midi` primarily
as a source relocation instead of carrying a permanent MIDIHub identity into
the system component. Shorter ambiguous prefixes such as `nm_*` should be
avoided in new public or cross-file interfaces.

The next refactoring should therefore extract code from `main.c` rather than
rewrite the portable protocol engines.

## Build direction

The eventual AROS tree component should have a native `mmakefile.src`, in the
same style as other `workbench/network` components.

It should express dependencies on the normal AROS include/link infrastructure
and on CAMD, without making Network MIDI part of the AROSTCP binary.

A conceptual dependency graph is:

```text
workbench-network
       |
workbench-network-midi
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
- no `DEVS:Midi` driver for dynamic network peers;
- no MIDI-specific mDNS implementation in the final architecture;
- no permanent dependency on the MIDIHub application name;
- no second implementation of AppleMIDI/RTP-MIDI for upstream;
- no mandatory Envoy/NIPC dependency;
- no elaborate service framework unless AROS gains one that Network MIDI can
  naturally use.

The resident process plus small AROS adapters should be sufficient.

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

### Phase 2 - prove the service

6. Validate bidirectional AppleMIDI with macOS/iOS and rtpMIDI peers.
7. Validate peer arrival, departure and reconnection without stale CAMD nodes.
8. Validate Network MIDI 2.0 through the same event loop.
9. Validate configuration reload.
10. Replace embedded discovery with the system mDNS API when available.

### Phase 3 - prepare upstream

11. Re-home the portable and AROS adapter sources under the agreed
    `workbench/network` location.
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
- the resident process survives peer and interface churn;
- settings can be loaded and changed without MIDIHub-specific assumptions;
- discovery is behind a replaceable API so the embedded responder can be
  removed cleanly;
- Network MIDI 2.0 shares the same service lifecycle rather than becoming a
  second daemon;
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
workbench/network/midi
```
