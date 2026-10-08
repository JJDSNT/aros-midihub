# CAMD endpoint architecture — comparative research and agent decision gate

**Status:** Research-backed architecture recommendation, not implemented or a frozen ABI.
**Priority:** Mandatory review before CAMD dynamic endpoint API or UMP API freeze (M0).
**Principle:** Preserve the *legacy CAMD application contract*, not the internal implementation of the legacy USB MIDI driver.

## Why revisit the original proposal?

The original dynamic-endpoint requirement arose from fixed `MidiDeviceData.NPorts` and USB/network hot-plug. That is a useful symptom, not an appropriate universal abstraction. A design that merely adds/removes driver ports risks making USB 1.0 assumptions permanent in CAMD's modern public API. AROS is free to replace or refactor its legacy USB MIDI implementation if a new endpoint-provider model is demonstrably better.

**Compatibility levels, explicitly separated:**
- **Required:** Unmodified legacy CAMD 41.1 applications, ABI vectors, public structure layouts, tags and documented behavior; existing 42 additions and compatibility regression suite.
- **Transition requirement:** Legacy MIDI drivers should continue working through an adapter when feasible. If a driver ABI migration is proposed, document the compatibility impact and obtain upstream review. Do not confuse preserving legacy driver ABI with retaining the old USB driver's internal architecture.
- **Not required:** Retaining USB driver's old fixed-port allocation algorithm, artificial `NPorts` pools, physical-port-oriented internal topology or implementation details.
- **Required functionality:** Existing USB MIDI devices must still work, including hot-plug and multiple devices, after migration. Refactoring is not permission for user-visible regression.

## Findings from contemporary systems

### Linux ALSA MIDI 2.0

ALSA distinguishes native UMP endpoints from legacy rawmidi projections. The USB driver queries Endpoint and Function Block information and can fall back to USB Group Terminal Blocks. The sequencer provides dynamic clients/ports and announces changes. Lesson: the native model must not be derived from MIDI 1.0 port counts, and legacy views should be projections rather than the source of truth.

References:
- https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html
- https://www.alsa-project.org/alsa-doc/alsa-lib/seq.html

### Windows MIDI Services

Windows explicitly separates transport-created endpoints, asynchronous discovery and subsequent property updates. Function Blocks are authoritative when present; USB Group Terminal Blocks are fallback, **not an additional union of ports**. A MIDI 1.0-style port is an endpoint plus Group plus direction, not a Function Block identifier. The endpoint may be visible before discovery completes; metadata and protocol can change later. USB serial numbers improve persistent identity; device-provided IDs are not universally reliable.

References:
- https://microsoft.github.io/MIDI/kb/porting-midi-libraries/
- https://microsoft.github.io/MIDI/kb/endpoint-arrival-and-update-ordering/
- https://microsoft.github.io/MIDI/kb/midi2-implementation-details/

### Apple CoreMIDI

Applications can create protocol-aware virtual endpoints and restore unique identifiers so other clients can retain persistent references. The model is not limited to physical USB ports. Lesson: support first-class software providers and stable identities without forcing them to fabricate device files.

Reference:
- https://developer.apple.com/documentation/coremidi/mididestinationcreatewithprotocol%28_%3A_%3A_%3A_%3A_%3A%29

## Recommended CAMD architecture (to be validated)

1. **Central endpoint registry:** CAMD owns registered logical endpoint objects, opaque runtime handles, identity, capabilities, connection/discovery state, topology snapshots, change generation and watches. Registry lifetime is distinct from transport session lifetime.
2. **Provider model:** USB, BLE, AppleMIDI, Network MIDI 2.0 and software providers register/update/retire endpoints through one versioned provider API. Provider operations must have explicit ownership, failure and teardown contracts. The provider model must not require an artificial `NPorts` pool.
3. **Native MIDI topology:** UMP Endpoint, Function Blocks and Groups are different objects/concepts. Group is the message address component; Function Block describes group ranges and capabilities. Group-less UMP messages remain endpoint-scoped. A legacy port is an endpoint + Group + direction *projection*, not a Function Block ID.
4. **Legacy compatibility adapter:** The CAMD 41.1 cluster/node/link interface continues to work. Map native endpoints/groups into compatible clusters where meaningful; document naming, direction, subscription, offline state and conversion-loss policy. Do not leak legacy `MidiMsg` into native UMP representation.
5. **Identity model:** Distinguish (a) ephemeral runtime handle, (b) stable system identity for persistent routing, (c) provider/device-advertised identifiers and (d) physical connection path. A device without reliable serial identity may not be uniquely recognizable across ports; never claim otherwise. Resolve collisions and stale aliases explicitly.
6. **Asynchronous discovery:** `registered`, `discovering`, `available`, `offline` and `retired` are conceptual lifecycle states; define precise transition rules and separate connectivity from discovery completeness and protocol readiness. Property updates must be observable without unregister/re-register churn.
7. **Topology precedence:** Prefer valid discovered Function Blocks; fall back to USB Group Terminal Blocks or explicitly documented provider topology when Function Blocks are unavailable. Never merge overlapping sources blindly or duplicate projected legacy ports. Treat late, partial and changed discovery data safely.
8. **Snapshot consistency:** Enumeration, open-by-ID, watch updates and route resolution must agree on one authoritative topology generation. Prevent stale references, ABA reuse, lost wakeups and use-after-free on removal. Watch overflow triggers a complete resync.
9. **Transport independence:** Keep USB descriptors, BLE GATT, network discovery, AppleMIDI sessions and UMP transport negotiation within providers/adapters. CAMD core stores normalized capabilities, not transport-specific wire details.
10. **Real-time behavior:** Registration/discovery/metadata updates cannot block event delivery. Define bounded queues, lock ordering, atomic UMP messages, cancellation, and safe provider shutdown while messages or clients remain outstanding.

## Required design comparison before implementing

Compare at least:
- **A. Legacy driver port resizing:** minimal change, but binds new architecture to `NPorts` and MIDI 1.0 port semantics.
- **B. Central logical endpoint registry + providers (recommended):** universal across transports, enables native UMP topology, requires a careful lifecycle and adapter design.
- **C. Distributed provider-owned endpoint graph:** reduces CAMD central state but complicates consistent discovery, stable identity and legacy projection.

Document tradeoffs in complexity, memory footprint, hot-plug behavior, compatibility, UMP support and upstream maintainability. Choose on evidence; the recommendation is not permission to skip comparison.

## Required implementation-agent instructions

- **STOP before freezing dynamic endpoint public APIs** until the alternatives and identity/topology/lifecycle contracts have been reviewed. Update sections 6.1–6.5 of [CAMD improvements](camd-improvements.md) if the chosen model differs.
- **Do not preserve the old USB implementation for its own sake.** Replace/refactor it when the new model is demonstrably better, while maintaining USB MIDI device functionality and legacy application compatibility.
- **Do not break legacy CAMD application ABI.** Old `NPorts` semantics remain for existing drivers if their ABI is retained; new providers must not be designed around it.
- **Do not mistake Function Blocks for MIDI port addresses.** Use endpoint, Group and direction; use blocks for metadata/capability mapping.
- **Do not treat registration as discovery completion.** Support incremental updates, timeouts, incomplete names, reconnect and watch resynchronization.
- **Do not infer globally stable identity from an untrustworthy serial/product string.** Explicitly represent confidence, collision and path dependence.
- **Do not implement USB-specific workarounds in the CAMD core.** Keep the native graph and provider API transport-neutral.
- **Do not declare completion** until old applications, migrated USB MIDI, a software provider and at least one dynamically appearing/disappearing provider pass the same lifecycle and data-integrity tests.

## Acceptance scenarios

1. Existing 41.1/42 clients run unmodified against migrated USB MIDI and legacy drivers.
2. Two identical USB MIDI devices coexist; disconnecting one never rebinds the other's routes.
3. A serial-less USB device moving ports follows the documented identity policy without silently hijacking saved routes.
4. Network peer appears, partially discovers, becomes available, disconnects, reconnects and retires without stale pointers.
5. Virtual software endpoint is created, discovered and retired without a fake physical driver file.
6. Native UMP endpoint exposes several Groups and changing Function Blocks; no duplicate legacy ports and no Group/Block confusion.
7. Endpoint watch overflow, concurrent open/remove and delayed metadata updates trigger correct resynchronization and teardown.
8. MIDI 1.0/UMP interop reports unsupported or lossy conversion rather than silently degrading messages.

## Normative and evidence boundary

This document synthesizes public OS implementation documentation. It is **not** a line-by-line audit of MIDI Association normative specifications or a finalized AROS C API. Before implementation freeze, verify applicable UMP v1.1.2 and MIDI-CI v1.2.1 requirements and coordinate public API changes with AROS maintainers:
https://midi.org/midi-2-0-core-specification-collection
