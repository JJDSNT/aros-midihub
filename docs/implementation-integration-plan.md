# CAMD / System MIDI Out / MIDI Filters — integration and delivery plan

**Status:** Design and implementation guidance. None of the three future features is declared implemented by this document.

## Scope and ownership

| Layer | Owns | Must not own |
|---|---|---|
| CAMD | Legacy ABI, native UMP event transport, endpoint topology, capability/discovery primitives, compatibility adapters | Default-output policy, filter processing, synthesis |
| MIDIHub Router | Persistent routes, System MIDI Out destination policy, optional filter chains, loop prevention | Reimplementation of CAMD protocol/endpoint graph |
| MIDIHub.prefs | Simple user-facing default destination, route/filter selection, profiles and diagnostics | Real-time event processing |
| System SoftSynth | SoundFont rendering and audio output integration | CAMD endpoint semantics and route policy |

## Delivery order and dependencies

1. **CAMD compatibility baseline:** preserve existing 41.1/42 contracts, complete outstanding fixes and pass `MIDIHubCAMDCompat` before and after each step.
2. **CAMD dynamic endpoint design:** stable identities, safe lifecycle and compatible cluster projections. Do not preclude UMP Function Blocks/Groups.
3. **CAMD native MIDI 2.0 milestones M0–M5:** normative audit, additive UMP API, parser/queues, topology/discovery, legacy translation, real transport and interoperability validation. See [CAMD improvements](camd-improvements.md#7-native-midi-20--ump-architecture-future-design-contract).
4. **System MIDI Out:** stable logical default output; Router policy and endpoint registration with effective capabilities, transactional switching and offline behavior. See [System MIDI Out](system-midi-out.md).
5. **MIDI Filters:** begin with Router-owned transparent chain, then stateless processors and stateful safety, preserving UMP semantics. See [MIDI Filters](midi-filters.md).

Steps 4 and 5 may prototype against the existing CAMD MIDI 1.0 interface **only as explicitly bounded prototypes**; neither should hard-code MIDI 1.0 limitations into its lasting contract. They are independent of each other and may progress separately after their required CAMD APIs exist.

## Shared integration acceptance tests

- **Legacy coexistence:** unmodified CAMD applications and legacy MIDI drivers continue working alongside native UMP clients.
- **Native end-to-end:** modern producer → CAMD native UMP → Router (optional filters) → native endpoint, with exact packet boundaries, Groups, timestamps and declared capabilities.
- **Legacy bridge:** MIDI 1.0 sender → compatible projection → MIDI 2.0 destination, and the reverse, with explicit conversion-loss diagnostics.
- **Default output:** multiple producers → System MIDI Out → selected Synth/device, with atomic destination switch, no routing loops and safe disconnect/reconnect.
- **Filter independence:** filters work on arbitrary routes; System MIDI Out works with zero filters.
- **Real-time safety:** stress concurrent senders, filter amplification, overflow, SysEx7/8, hot-plug and teardown; verify bounded resources, ordering and no deadlocks/use-after-free.
- **Portability:** test supported AROS 32/64-bit targets and QEMU, and document unavailable hardware or normative test vectors as blocked rather than passed.

## Agent handoff rules

Read all four documents before changing CAMD or Router interfaces. Treat **open design gates** as decisions requiring explicit resolution, not invitations to use hacks. Record an architecture decision for irreversible public ABI or protocol-mapping choices. Keep changes small and independently reviewable, add tests with each implementation increment, and update status based on observed evidence only.

**Do not claim full MIDI 2.0 support after a basic UMP loopback or Note On test.** Use the M0–M5 and U01–U14 gates from CAMD documentation. Do not declare System MIDI Out or Filters complete without their respective acceptance scenarios.
