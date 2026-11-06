# CAMD 43 MIDI-CI gap audit

Audited draft: AROS PR #2101 at `4c1510f1d08fad7efb1081b9d005734ef2557c28`.
CAMD 42 dependency: `a0ca679f44`. Source links below are immutable commit
links. Required tests that do not yet exist are not presented as validation.

## Source-level matrix

| Area | Existing implementation and behavior | Gap and ownership | Minimum correction, ABI and validation |
| --- | --- | --- | --- |
| UMP | `CAMDUMPEventV1` carries one complete 1–4 word message. [`camd_provider_validate_ump`](https://github.com/aros-development-team/AROS/blob/4c1510f1d08fad7efb1081b9d005734ef2557c28/workbench/libs/camd/provider_contract.c) checks the MT word count; native UMP queues preserve records. | Sufficient generic message boundary for MIDI-CI SysEx7. It deliberately validates size, not every status-specific field. That deeper check belongs to the consuming UMP/MIDI-CI layer. Blocking driver gap: the public resident `MidiDeviceData` path is still MIDI 1.0 and the native provider contract is private, so a real driver cannot yet publish UMP through the generic driver API. | Keep `CAMDUMPEventV1`. Specify and test the smallest optional driver-side UMP callbacks before public exposure. No MIDI-CI state in CAMD. ABI: additive driver extension, not a `MidiDeviceData` layout change. Test all 16 MT size classes, malformed counts, and a resident native-UMP loopback. |
| SysEx | MIDI 1.0 endpoint sessions use `PutEndpointSysEx`/`GetEndpointMidi` with complete bounded messages up to 64 KiB. UMP sessions carry each SysEx7 Data64 packet intact. | No CAMD gap for MIDI 1.0 MIDI-CI. UMP SysEx7 reconstruction spans several UMP records and belongs to `midici.library`, because CAMD must not reinterpret generic UMP. Existing MIDI 1.0 validation checks F0/F7 but does not reject embedded status bytes; the MIDI-CI decoder still validates every data byte. | No CAMD API change. The implemented host test reconstructs byte-exact Discovery through existing UMP helpers. Add CAMD 43 runtime loopback and malformed Start/Continue/End tests. |
| Endpoints | Stable 128-bit CAMD endpoint ID, provider ID, state, data formats, protocol capability/current protocol, product instance and transport are exposed in immutable snapshots. | Sufficient identity handle for coordinator keys. A CAMD ID is not a MIDI-CI MUID and must never be substituted for one. | No change for MUIDs. Test reconnect retains CAMD ID while the coordinator invalidates/re-discovers the correct MUID scope. |
| Function Blocks | `CAMDGroupInfoV1` and `CAMDFunctionBlockInfoV1` expose Group range, direction, active/static/UI and MIDI 1.0 flags; topology replacement is atomic. | Confirmed metadata omission: UMP Function Block Info also conveys MIDI-CI version and maximum SysEx8 streams. The draft explicitly deferred these fields. This is CAMD topology metadata, not MIDI-CI policy. | Before v43 freezes, append `MIDICIVersion` and `MaxSysEx8Streams` (or a reviewed equivalent) through the existing sized-record policy; update size assertions and copy tests. If v43 freezes first, use a V2 record/query, not an incompatible overwrite. Test exact Stream-message projection and unknown/zero values. |
| Metadata | Endpoint name, product instance, transport, protocols, Groups and Function Blocks are available. | Manufacturer/family/model/revision from UMP Endpoint Discovery are not represented. MIDI-CI can still exchange its own Device Identity, so this is not a blocker for initial CI. It is a CAMD/driver gap for a complete UMP endpoint model and for avoiding redundant discovery. | Do not block the engine. Define only after demonstrating consumers and lifetime/source precedence (Stream messages versus USB descriptors). Additive sized endpoint record or separate metadata record. |
| Discovery | A client can enumerate stable endpoints, open bidirectional sessions, send complete SysEx or UMP and watch changes. | MIDI-CI broadcast/reply state is intentionally absent and belongs to the coordinator/engine. Native UMP discovery cannot reach hardware until the driver UMP path exists. | No MIDI-CI CAMD vectors. Coordinator enumerates/open sessions per endpoint; drivers publish UMP capability generically. Test MIDI 1.0 and UMP Discovery independently. |
| Concurrency | Registry and queues use shared `SignalSemaphore` state. Multiple input sessions fan out; output sessions have reserved queues and explicit queue-full counters. | Transport concurrency is adequate. CAMD cannot decide which application owns the endpoint's MUID or serialize conflicting Profile/PE operations. That is a `midici` coordinator responsibility. | No CAMD change. Test two input sessions receive identical complete CI messages and one slow reader reports loss without corrupting the other. |
| Ordering | Per-session queues preserve complete records; equal-time records preserve send order; enqueue batches reserve atomically. | There is no meaningful cross-application MIDI-CI transaction order. Multiple applications can independently send conflicting requests in a scheduler-dependent order. This is not a transport bug. | One coordinator output session per endpoint serializes CI operations. Keep application musical traffic independent. Test conflicting coordinator leases and unrelated application traffic. |
| Lifecycle | Endpoint states include offline/retiring/retired; watches report changes; provider/session references block premature release. `RemoveMidiDevice` refuses while pinned. | Confirmed gap: [`endpoint_owner.c`](https://github.com/aros-development-team/AROS/blob/4c1510f1d08fad7efb1081b9d005734ef2557c28/workbench/libs/camd/endpoint_owner.c) states that a task ending without closing the library is not seen; its objects stay and pin the library. This affects all endpoint clients, not just MIDI-CI. | Do not make `midici.library` authoritative per caller. The service owns CAMD handles, and client requests have deadlines so a dead client cannot pin transport objects. Separately design an Exec-supported task-death cleanup for CAMD only if it benefits all clients. Test forced client termination on real AROS. |
| Routing | Complete SysEx/UMP records are copied per endpoint input session; legacy traffic remains on the existing graph. | CAMD provides no exclusive MIDI-CI claim or filter, so independent applications may all observe CI and may send their own requests. Adding CI routing policy to CAMD would violate the boundary. | Coordinator owns the system MUID/session; direct applications use the public MIDI-CI API or explicitly isolated engines. Test Note/SysEx application traffic remains byte-identical while CI runs. |
| Notifications | Endpoint watch events report added/updated/offline/retired/lost with generations; overflow requires a new snapshot. | Sufficient generic notification. There is no field-level change mask, so the coordinator refreshes the endpoint/topology record on `UPDATED`. | No ABI change required. Test topology update, lost-event resnapshot, offline cancellation and reconnect rediscovery. |
| ABI | V43 appends 33 vectors after the v42 boundary. Records are pointer-free, sized/versioned and compile-time size checked; old CAMD graph structures are unchanged. | The proposal is explicitly provisional and its own U01 checklist is not closed. Function Block fields and the native driver path should be settled before freezing. Host tests do not prove m68k/i386/AArch64/x86-64 ABI or AROS runtime cleanup. | Incorporate justified fields into PR 2101 rather than inventing 43.1. Preserve existing vector order. Required: four-architecture layout builds, old-header/short-record tests, stale handle tests and CAMD 41/42 regression suite. |

## Confirmed blockers

### C43-G1: no generic native-UMP driver publication

Requirement: MIDI-CI SysEx7 over a UMP transport must reach a generic CAMD UMP
session without translating the transport into a MIDI 1.0-only driver.

Evidence: V43 exposes native UMP application publication, but
`AddMidiDevice(struct MidiDeviceData *)` registers the legacy callback-bearing
driver and the loaded-driver adapter declares native MIDI 1.0 paths. The
provider operations capable of UMP remain private.

Owner and correction: CAMD plus individual drivers. Define the smallest
optional driver-side native event block after proving the callback/lifetime
contract with the in-memory driver. Do not expose the whole private provider
registry. ABI is additive and must leave `MidiDeviceData` unchanged.

### C43-G2: Function Block MIDI-CI capability is not queryable

Requirement: UMP Function Block Info identifies whether and at what version a
Function Block supports MIDI-CI; it also reports SysEx8 stream capacity.

Evidence: `CAMDFunctionBlockInfoV1` ends at Group range and Name. Linux's
documented UMP Block interface exposes both fields, independently confirming
their topology role.

Owner and correction: CAMD record populated by the UMP driver. Append reviewed
fields before V43 ABI freeze. A test must decode a known Function Block Info
Notification into the CAMD snapshot and verify zero/unknown behavior.

### C43-G3: abnormal client death leaks endpoint objects

Requirement: sessions and endpoint references must be reclaimable after
application termination.

Evidence: the source comment quoted in the lifecycle row is an explicit
negative behavior, not an inference.

Owner and correction: the MIDI-CI architecture avoids the failure by placing
CAMD handles in a resident coordinator whose requests expire. A generic CAMD
task-death facility is separate work and is not required to put MIDI-CI policy
inside CAMD.

## Midihub integration gaps exposed by the audit

These are not additional CAMD policy requests. The current Network MIDI 2.0
runtime advertises a single static Function Block with no MIDI-CI capability
and translates all UMP through the MIDI 1.0 CAMD driver path. Consequently it
can carry MIDI-CI SysEx7 bytes after translation, but it cannot expose the
peer's native UMP topology or MIDI-CI Function Block version through CAMD 43.
Retiring that conversion boundary depends on C43-G1 and C43-G2. Until then,
MIDI-CI over the transport may be validated as MIDI 1.0 SysEx compatibility,
not claimed as native UMP integration.

## Gaps not demonstrated

- No evidence requires a CAMD MUID field, Profile state, PE transaction table
  or Process Inquiry API.
- No evidence requires CAMD to reassemble UMP SysEx7. Complete UMP message
  boundaries plus ordered delivery are the correct generic primitive.
- No evidence justifies a new CAMD version after 43. The draft PR is the place
  for justified pre-freeze metadata and driver API work.
