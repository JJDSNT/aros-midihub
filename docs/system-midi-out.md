# System MIDI Out — design proposal

**Status:** Proposed; not implemented. **Dependency:** future native CAMD MIDI 2.0/UMP architecture in [camd-improvements.md](camd-improvements.md), section 7. This document specifies a feature, not changes to the CAMD ABI.

## Purpose and contract

System MIDI Out is the stable, user-selected **default logical MIDI output** for AROS applications. It is not a particular synthesizer, MIDI 1.0 port, physical device or new CAMD transport. It is protocol-neutral: modern clients may use native UMP/MIDI 2.0 when available, while legacy CAMD clients retain MIDI 1.0 access through a compatible projection.

The system-wide identity remains stable when the chosen destination disconnects or changes. Identity, availability, capabilities and routing policy are separate properties. Never advertise capabilities the selected destination or conversion path cannot deliver.

## Ownership and architectural boundaries

- **CAMD** owns graph/endpoint identity, message transport, multi-producer merging, UMP capabilities and legacy compatibility. CAMD does not own a user's default destination policy.
- **MIDIHub Router** owns persistent default-destination selection and forwarding from the logical System MIDI Out to the selected endpoint. Reuse the existing router where feasible; do not create a separate resident core without demonstrated need.
- **MIDIHub.prefs** configures the default and shows current destination, availability and capabilities.
- **System SoftSynth** is one possible destination, not synonymous with System MIDI Out. AHI owns audio output; MIDI merging and audio mixing are different operations.
- **MIDI Filters** may optionally run on the route, but are not required for System MIDI Out to function.

## V1 functional behavior

1. A single discoverable logical System MIDI Out identity; a stable legacy CAMD cluster projection for old applications. The public name `System MIDI Out` is proposed; confirm namespace collisions and naming rules before implementation.
2. Multiple senders may publish simultaneously. Existing CAMD merge semantics apply; this does **not** isolate channel/controller/note state between applications.
3. One configured **primary destination** at a time (software synth or an external endpoint). General fan-out remains a Router feature, not implicit behavior of the default endpoint.
4. If the destination is offline, the logical identity remains available and reports a distinct unavailable/degraded state. Policy for drop, bounded queue and retry must be explicit; never accumulate unbounded notes while offline.
5. Destination changes are atomic from the route's perspective: avoid splitting multi-packet messages, flush or terminate in-flight SysEx safely, and apply an explicit stuck-note policy (including sustain and controllers) to the old destination. Do not blindly broadcast All Notes Off to unrelated endpoints.
6. Preserve message order per source, timestamps and UMP packet boundaries where the underlying CAMD API supports them.
7. Capability presentation reflects the effective path: MIDI 1.0 legacy projection, UMP transport support and actual MIDI 2.0 Channel Voice support are distinct.
8. Settings persist across restarts. Router startup, shutdown and missing synth/device behavior must be deterministic.

## Future isolated sessions

Isolated sessions are **not V1**. A future client may explicitly request a private logical endpoint/session, with independent channel state and optionally a separate synth context. Session identity and ownership must be available at the source; legacy events already merged into one cluster cannot reliably be attributed to applications afterward. A physical MIDI device may not support independent sessions. This is distinct from CAMD dynamic endpoint registration.

## Open implementation decisions

- Should the stable logical endpoint be registered as a CAMD virtual endpoint or implemented initially as a resident CAMD node/cluster owned by the Router? Select after section 6 endpoint API is settled.
- What exact API advertises effective destination capabilities and state, and what signals notify clients of default changes?
- What is the deterministic policy for offline events, destination switching, MIDI-CI/Stream messages, SysEx7/8 and unsupported UMP message types?
- What happens when a user routes System MIDI Out back into itself or a route cycle? Require loop detection.
- Which destination is selected on first boot, and is it permissible to have no default configured?

## Acceptance scenarios

- Two legacy CAMD producers share the default endpoint; no promise of per-app isolation.
- Native UMP producer sends all supported packet sizes through an appropriate MIDI 2.0-capable destination without loss.
- A MIDI 1.0-only destination receives compatible translated events, with conversion loss visible in diagnostics.
- Change default while notes are active and while SysEx is in progress; no use-after-free, split messages or persistent stuck notes.
- Disconnect/reconnect default destination without destroying System MIDI Out identity.
- Start Router without synth and with no external device; identity/status and startup behavior are deterministic.
- Validate loop protection, simultaneous senders, queue overflow and restart behavior.

## Related proposals

- [CAMD improvements](camd-improvements.md) — native MIDI 2.0/UMP architecture and compatibility.
- [MIDIHub architecture](midihub-architecture.md) — runtime ownership.
- [MIDI Filters](midi-filters.md) — optional route processing.
