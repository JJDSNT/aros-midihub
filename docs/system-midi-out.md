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


## Implementation contract and decision gates

**Proposed ownership decision:** Reuse the resident MIDIHub Router as the default-output policy owner. CAMD owns endpoint identity and graph semantics. Do not add a separate always-running MIDIHub core unless a demonstrated requirement cannot be met by the Router. Prefer the versioned CAMD virtual endpoint API once available; until then a Router-owned stable CAMD node/cluster may serve as a *documented temporary implementation*, not a new permanent parallel endpoint framework.

**Stable identity and startup:** Reserve a canonical system-owned identifier (human-readable label: `System MIDI Out`); enforce collision detection and single active owner. The logical output remains discoverable while no destination is selected, while the selected destination is offline, and while the synth is stopped. Persist the chosen destination by stable endpoint ID, not transient cluster name alone. Startup is nonblocking; endpoint state distinguishes ready, degraded, unavailable and switching.

**Switching and delivery:** Route configuration changes are transactional. Validate the new target and capabilities, stop accepting new events at the cutover boundary, resolve or cancel queued events using a bounded policy, and activate the new target atomically. Prevent feedback loops. Track notes/controllers where possible; issue targeted cleanup on the old destination without affecting unrelated traffic. When source identity is unavailable, do not promise perfect per-application cleanup. Offline delivery defaults to explicit drop-with-counter rather than indefinite buffering; any bounded queue mode must be opt-in with expiration and documented note safety.

**Protocol capabilities:** The endpoint is protocol-neutral, but each route advertises *effective* capabilities of the chosen destination and translation path. UMP support does not imply MIDI 2.0 Channel Voice. Native endpoint-wide messages, SysEx7/8 and MIDI-CI may need destination-specific policies; unsupported or lossy conversions must be observable. No hidden down-conversion.

**API and UI boundary:** MIDIHub.prefs chooses one primary destination, shows availability and effective MIDI capabilities, and offers diagnostics; Router handles forwarding and persistent route policy. Advanced fan-out and filters remain separate Router features. Isolated sessions are explicitly out of V1.

**Implementation gates:** Before coding, freeze canonical identity/namespace and collision behavior; CAMD virtual-endpoint registration/lifetime API; capability and status query/watch API; switch transaction, SysEx-in-flight, active-note cleanup, offline/drop policy and startup/shutdown tests. These are open design gates, not claims of existing implementation.

**Completion tests:** Two concurrent legacy producers; native UMP producer with all packet lengths; loss reporting to a MIDI 1.0 destination; destination switch during sustained notes and SysEx; restart, offline/reconnect, loop detection, queue overflow and no dangling CAMD references.
