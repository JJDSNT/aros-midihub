# Future investigation: AROS NIPC / Envoy compatibility

## Status

**Future work / exploratory.** This is not a requirement for MIDIHub, AppleMIDI,
Network MIDI 2.0, or the initial AROS port of Bonami.

Bonami's Envoy/NIPC compatibility bridge should remain optional while the core
mDNS/DNS-SD service is brought up on AROS.

## Why this appeared during MIDIHub work

MIDIHub needs a system-wide mDNS/DNS-SD responder for AppleMIDI
(`_apple-midi._udp`) and Network MIDI 2.0 (`_midi2._udp`). Bonami is being
evaluated as the Amiga-native basis for that service.

Bonami also contains an Envoy/NIPC compatibility layer. Its NIPC inquiry code
includes `<envoy/nipc.h>`, understands NIPC inquiry tags such as
`MATCH_ENTITY`, `QUERY_ENTITY`, `QUERY_HOSTNAME`, and `QUERY_IPADDR`,
and maps NIPC discovery onto DNS-SD/mDNS. Bonami also reserves the
`_nipc._tcp` service type for this purpose.

AROS does not currently provide the Envoy/NIPC environment expected by this
optional bridge. That should not block the Bonami port: the bridge can be
disabled initially.

## Two different goals

It is important not to confuse two possible projects.

### 1. Enable Bonami's NIPC discovery bridge

The smallest goal is only to provide enough compatible public definitions and
integration for Bonami's optional NIPC bridge to build and operate on AROS.

This may require only a limited compatibility header/API surface rather than a
complete implementation of Envoy.

Expected effort: **low to medium**, subject to an audit of the exact Bonami
dependencies.

### 2. Implement an AROS `nipc.library`

The larger goal would be an open AROS implementation of the public NIPC API so
software written for Envoy can use entities and transactions on AROS.

Known public API areas include:

- entity creation, lookup, attributes, and lifetime;
- synchronous and asynchronous transactions;
- transaction abort/wait/reply operations;
- NIPC buffers;
- inquiries and entity discovery;
- routing and host/entity information.

A possible modern AROS architecture is:

```text
Envoy/NIPC-aware application
          |
          v
     nipc.library
          |
   +------+-------+
   |              |
entities /     transactions
discovery          |
   |          network transport
   v              |
Bonami             v
mDNS/DNS-SD   bsdsocket.library
   |
_nipc._tcp
```

Bonami could therefore provide the discovery portion while a future
`nipc.library` implements the NIPC programming model and transport.

Expected effort: **medium to high** for useful AROS-to-AROS functionality.

## Compatibility with classic Envoy

API compatibility and wire-protocol compatibility are separate objectives.

An AROS `nipc.library` can reproduce the public programming API without
necessarily being network-compatible with every historical Envoy release.
Full interoperability with classic Amiga Envoy may require reverse engineering
because the wire protocol is not as well documented as the public API and
different Envoy generations may differ.

A future interoperability investigation should:

1. inventory the original public NIPC headers, library vectors, tags, structures,
   autodocs, and example programs;
2. determine which NIPC/Envoy releases should be targeted;
3. capture network traffic between real or emulated Amiga systems running
   Envoy;
4. correlate API operations such as entity discovery and transactions with
   packets on the wire;
5. document the protocol before implementing compatibility;
6. build conformance tests for AROS-to-AROS and, separately, AROS-to-Envoy
   operation.

Expected effort for broad classic Envoy interoperability: **high**.

## Open-source situation

At the time of this investigation, no complete open-source drop-in
implementation of `nipc.library` had been identified. The public API/ABI,
headers, historical documentation, and examples provide useful reference
material, but the original Envoy implementation should not be assumed to be
available as open source.

This makes a clean-room AROS implementation a possible future contribution,
rather than something required by MIDIHub.

## Relationship to the Bonami port

The near-term sequence should remain:

```text
MIDIHub requirement
        |
        v
system mDNS/DNS-SD
        |
        v
Bonami port to AROS
        |
        +--> AppleMIDI / Network MIDI 2.0
        |
        +--> Envoy/NIPC bridge (optional, initially disabled)
                       |
                       v
             future NIPC investigation
```

The Bonami port should therefore be designed so that the Envoy bridge can be
enabled later without making NIPC a dependency of the core mDNS service.

## Potential upstream destination

If this work proceeds, `nipc.library` should be considered general AROS
network infrastructure rather than a MIDIHub-specific component. MIDIHub is
only where the missing capability was discovered.

As with the system mDNS work, this repository can incubate documentation,
experiments, and compatibility tests, while a mature implementation should be
proposed upstream to AROS.

## Future decision points

Before implementation, answer:

- Is enabling Bonami's bridge useful without a complete `nipc.library`?
- What exact subset of `envoy/nipc.h` does Bonami require?
- Is API/source compatibility sufficient, or is binary compatibility desired?
- Which Envoy version defines the interoperability target?
- Can Bonami discovery coexist cleanly with historical NIPC discovery?
- Should network transport preserve the historical protocol or define a
  modern AROS-only transport first?
- Which parts belong in Bonami upstream versus AROS itself?

Until those questions are answered, NIPC/Envoy support remains a documented
future opportunity and must not delay the MIDI networking roadmap.
