# MIDI Filters / Processing Pipeline — design proposal

**Status:** Future consideration; not implemented. **Dependency:** native CAMD MIDI 2.0/UMP event semantics in [camd-improvements.md](camd-improvements.md), section 7. Filters must not become an implicit CAMD core requirement.

## Purpose

Allow optional, deterministic transformations on a configured MIDIHub Router route between an input and an output. A filter chain is a routing feature, not an intrinsic property of a CAMD endpoint or System MIDI Out.

## Architecture and ownership

- **CAMD** transports and merges MIDI events, preserving packet structure, endpoint capabilities and timing; it does not host a general DSP/plugin engine.
- **MIDIHub Router** owns route attachment, ordered execution, bypass, failure policy and diagnostics.
- **MIDIHub.prefs** exposes only simple enable/disable, profile and chain assignment controls.
- A **separate MIDI processing application** may eventually edit sophisticated chains, graphs and plugin parameters; do not force a complex patchbay into Zune Preferences.
- **System MIDI Out** is a possible route source, not a prerequisite. Other device-to-device routes can have identical processing.
- MIDI event processing is not audio DSP. Audio effects belong to the synthesizer/audio pipeline and AHI-related processing, not this route filter chain.

## Processing contract

A route has zero or more filters, applied in a declared order: input → filter 1 → filter 2 → … → destination. Bypass means lossless pass-through. Each filter declares the message types, UMP Groups, MIDI protocol features and transformations it understands.

A filter may pass, transform, suppress or generate MIDI events. Unknown/unsupported UMP messages **pass unchanged by default** unless a user explicitly chooses blocking; do not silently reinterpret MIDI 2.0 messages as MIDI 1.0. Preserve group-less UMP messages, full message boundaries, original timestamps unless intentionally rescheduled, and deterministic ordering for generated messages.

Processing should be nonblocking and bounded on the real-time path: specify CPU/memory budgets, maximum generated events, queue capacity, backpressure and error reporting. Failure policy is configurable per chain (bypass or stop route); avoid silently dropping entire streams.

## Candidate filter families

- Channel and Group mapping, routing and selection.
- Note transpose, key range and velocity curves, with native MIDI 2.0 resolution where applicable.
- CC/RPN/NRPN filtering or remapping, pitch bend and program mapping.
- Note/controller transforms with awareness of MIDI 2.0 per-note expression and controller resolution.
- SysEx7/SysEx8 and MIDI-CI filtering only with explicit safety and policy; no arbitrary corruption of discovery or property messages.
- Clock/timing operations (delay, quantize, humanize) as a **separate, more demanding class** requiring scheduling and timestamp policy.
- Stateful processors (arpeggiator, chord processor, MPE conversion) as later extensions requiring note tracking and cleanup.
- Audio DSP **excluded**; if ever desired, define a separate synth/audio proposal.

## State, lifecycle and correctness

- Per-route and optionally per-source state; never assume CAMD's shared merge identifies the original application.
- Track note-on/off and sustain when filters transpose, suppress or generate notes, to prevent stuck notes on bypass, chain edit, disconnect or route change.
- Define behavior for MIDI 1.0/UMP conversions at boundaries; avoid lossy conversion in a filter unless its contract requires it.
- Hot-swap chains atomically at a message boundary; in-flight SysEx and scheduled messages require explicit drain/cancel rules.
- Persist chain definitions and versioned parameters in profiles; validate schemas before activation.
- Future plugins need ABI versioning, isolation/trust, CPU limits and failure recovery; V1 can use built-in processors only.

## Incremental delivery

1. Specify a filter ABI and a transparent pass-through chain with diagnostics.
2. Implement stateless Group/channel selection, transpose and velocity mapping.
3. Add bounded stateful note tracking and safe route switching.
4. Add protocol-aware transforms and explicit conversion diagnostics.
5. Consider scheduling filters and third-party plugin hosting only after latency and lifecycle testing.

## Acceptance scenarios

- Unfiltered routes preserve native UMP words and ordering exactly.
- Mixed MIDI 1.0/2.0 traffic is processed only by compatible filters; unsupported messages pass unchanged by default.
- Note transposition maintains matching note-offs through live parameter changes.
- SysEx7/8 and multiword UMP are not split or truncated.
- Overflow, slow filters, plugin failure, route deletion and hot-plug cannot block CAMD indefinitely.
- Filters work on arbitrary routes, not only System MIDI Out.

## Related proposals

- [CAMD improvements](camd-improvements.md) — native MIDI 2.0/UMP contract.
- [System MIDI Out](system-midi-out.md) — optional default output route.
- [MIDIHub architecture](midihub-architecture.md) — Router/prefs ownership.


## Filter execution contract and decision gates

**Proposed V1 architecture:** A Router-owned, ordered, in-process chain of **built-in** MIDI event processors, configured per route. Do not introduce a third-party plugin ABI, audio DSP engine, complex patchbay or a separate resident service in V1. The processing ABI must nonetheless be versioned internally so it can evolve without invalidating saved profiles.

**Processor interface (semantic contract, C signatures TBD):** A processor has immutable configuration, optional private per-route state, capability declaration, initialization, event-processing, flush/reset and destruction operations. Input is a complete timestamped CAMD native UMP event (or an explicitly typed MIDI 1.0 legacy event at the adapter boundary); output is zero or more complete events with a bounded maximum amplification. Return explicit pass/transform/drop/error results and diagnostics. Unsupported valid UMP messages pass through unchanged by default. No unannounced lossy translation.

**Execution guarantees:** Maintain deterministic order for each source and complete multiword UMP atomicity. Distinguish group-addressed from group-less messages. The real-time path must not block on allocations, I/O, user interface, property exchange or external plugins. Preallocate bounded scratch/output buffers, define overflow and failure policy, and measure worst-case processing latency. Generated messages have documented timestamp and ordering rules; scheduling filters are out of initial V1 scope.

**Stateful safety:** Filters that transpose, suppress or generate notes must track matching note-offs, sustain and controller state. A parameter edit, bypass, route change, disconnect or shutdown performs explicit drain/cancel/cleanup. Never assume events already merged by CAMD identify their original application. Preserve SysEx7/8 boundaries and MIDI-CI messages unless a dedicated filter explicitly supports their semantics.

**Profiles and ownership:** Persist versioned chain definitions with stable processor IDs and validated parameters. MIDIHub.prefs manages enable/bypass and chain selection; a separate editor can be considered for advanced configuration later. Router is the single execution owner; CAMD is not extended to host filters.

**Implementation gates:** Freeze processor capability metadata, output amplification limits, queue/backpressure policy, timestamp propagation, error semantics, note-state cleanup, atomic hot-swap and profile migration before implementing stateful processors. Keep unimplemented scheduling, plugin hosting and audio DSP clearly marked as future work.

**Completion tests:** Byte/word-perfect bypass, unknown valid UMP pass-through, 32/64/96/128-bit event preservation, 16 Groups, mixed protocol traffic, transposition across live edits with matched note-offs, bounded event generation, SysEx-in-flight route changes, failure/overflow, no CAMD deadlock and operation on routes unrelated to System MIDI Out.
