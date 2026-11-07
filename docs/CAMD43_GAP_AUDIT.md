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
| Lifecycle | Endpoint states include offline/retiring/retired; watches report changes; provider/session references block premature release. `RemoveMidiDevice` refuses while pinned. | Confirmed and corrected gap: the baseline [`endpoint_owner.c`](https://github.com/aros-development-team/AROS/blob/4c1510f1d08fad7efb1081b9d005734ef2557c28/workbench/libs/camd/endpoint_owner.c) only observed `CloseLibrary()` and left all endpoint objects plus signal task pointers after abnormal termination. | `aros-camd-endpoint-task-death.patch` uses the baseline task.resource 1.4 notification contract privately: the hook neutralizes signals and marks ownership; a CAMD task releases objects in dependency order. No public ABI. Twenty forced deaths passed on AROS linux-hosted. Exec's separate library open count is intentionally out of scope. |
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

Exact path: `camddevices.h` exposes byte pull/single-`UWORD` receive callbacks;
`addmididevice.c` accepts only that record; `driver_endpoints.c` wraps every
loaded/in-memory driver in `CAMDLegacyDriverOutputBackend` and hard-codes
`CAMD_PROVIDER_PROTOCOL_MIDI1`. Conversely, private `provider_contract` and
`legacy_driver_adapter` already validate and carry complete 1–4-word UMP
records. No new application endpoint/session API is missing.

Network MIDI 2.0 independently demonstrates the failure: its receive path in
`transport_midi2.c` calls `ump_to_midi1()`, its transmit path calls
`ump_from_midi1()`, and `netmididriver.c` publishes only `MidiDeviceData`.
Original Groups, MIDI 2.0 precision/per-note messages, Stream messages,
Data128/SysEx8 and packet boundaries cannot survive that boundary. A future
Bluetooth UMP characteristic needs the same complete-record primitive, while
the merged BLE MIDI 1.0 characteristic must remain unchanged.

Smallest public correction: leave `MidiDeviceData` byte-for-byte unchanged.
Add a flag and a sized/versioned extension immediately after it in a composite
driver registration record. The extension provides per-port query/state/
topology callbacks, complete `CAMDUMPEventV1` send, an attach callback through
which CAMD supplies complete UMP submit and port-change operations, and a
detach boundary. First-version submission is task-context only; interrupt
drivers defer to a resident worker. CAMD continues owning validation, queues,
fan-out and backpressure. `OpenMidiDevice()` validates the flagged tail inside
the seglist; unchanged `AddMidiDevice()` can find the adjacent tail for an
in-memory record. A new library vector is a fallback only if maintainers reject
the adjacent-tail convention.

Compatibility: old drivers and current BLE MIDI 1.0 take the exact legacy
path; old applications and CAMD vectors are unchanged; new drivers should
retain a safe MIDI 1.0 fallback when loaded by CAMD 41/42. Required proof is a
canonical in-memory native driver exercising every MT size, two readers,
isolated overflow, state/protocol/topology change and removal while pinned,
followed by exact Network MIDI 2.0 and Bluetooth UMP transport round trips.

### C43-G2: Function Block MIDI-CI capability is not queryable

Requirement: UMP Function Block Info identifies whether and at what version a
Function Block supports MIDI-CI; it also reports SysEx8 stream capacity.

Evidence: `CAMDFunctionBlockInfoV1` ends at Group range and Name. Linux's
documented UMP Block interface exposes both fields, independently confirming
their topology role.

[Official M2-104-UM v1.1.2](https://midi.org/midi-2-0-core-specification-collection)
defines both fields; the [MIDI Association Function Block example](https://midi.org/building-a-usb-midi-2-0-device-part-2)
states that Max SysEx8 Streams is zero when unsupported. [Linux UAPI](https://github.com/torvalds/linux/blob/master/include/uapi/sound/asound.h)
also exposes `midi_ci_version` and `sysex8_streams` as independent 8-bit block
metadata. They are topology facts, not MIDI-CI session policy.

Owner and implemented proposal: CAMD record populated by the UMP driver.
`patches/aros-camd-function-block-capabilities.patch` appends two `ULONG`
fields at offsets 104 and 108, making the canonical record 112 bytes. Zero is
unknown/no MIDI-CI and no SysEx8 support. `GetEndpointFunctionBlock()` already
uses caller `Size`, so a 104-byte reader remains safe and receives full
`Size == 112`.

Input arrays required an additional ABI correction: existing publication code
indexed with the newly compiled `sizeof`, which would skip through an old
104-byte array incorrectly. The proposal uses the first record's `Size` as a
homogeneous stride, accepts aligned records of at least 104 bytes, zero-fills
missing appended fields, bounds present values to 8 bits and rejects mixed
strides. Canonical PR-source host tests pass for two-record legacy and full
arrays. Stream-message projection, public output canaries, topology watch and
AROS multi-architecture builds remain before upstream submission.

### C43-G3: abnormal client death leaks endpoint objects

Requirement: sessions and endpoint references must be reclaimable after
application termination.

Evidence: the source comment quoted in the lifecycle row is an explicit
negative behavior, not an inference.

Exact path: `TaskOpenedCamd()` records opens and `OwnEndpointObject()` records
objects, but cleanup only enters from `TaskClosedCamd()` via `CloseCamd()`.
The leak can exhaust 32 snapshots, 64 receivers, 16 watches and 32 publishers.
It also leaves `EndpointReceiver`, `EndpointWatcher` and `EndpointPublisher`
task pointers available to later `Signal()` calls. A second bug is that
`OwnEndpointObject()` silently returns on allocation failure, leaving a live
untracked object even after a normal close.

The PR baseline already supplies the generic mechanism: task.resource 1.4
calls `TNA_REMOVED` before Exec removes a task. The hook may allocate and take
semaphores but cannot wait for another task. Therefore no CAMD public API or
Exec change is required. The smallest safe design registers one private hook,
immediately neutralizes signal targets, marks matching owner records dead, and
defers provider/session release to safe CAMD task context. Ownership
registration becomes fallible; each creator rolls its new object back and
returns `CAMD_RESULT_NOMEM` if it cannot be tracked. Session cleanup precedes
publisher cleanup. This does not pretend to repair Exec's separate library
open-count leak when a task omits `CloseLibrary()`.

Implemented correction: `patches/aros-camd-endpoint-task-death.patch` changes
only CAMD-private structures and functions. `EndpointSignalSemaphore` covers
the task/signal pair used by receiver, watcher and publisher callbacks. The
`TNA_REMOVED` hook takes that lock before Exec removes the task, clears every
matching pointer, marks owner records under `EndpointOwnerSemaphore`, and
signals a private reaper. The reaper closes sessions first, then snapshots,
watches and publishers in normal task context. Initialization and expunge
register/remove the hook and synchronously stop the reaper. `OwnEndpointObject`
and `TaskOpenedCamd` now return failure; every public object creator rolls back
and returns `CAMD_RESULT_NOMEM` rather than leaving an untracked object.

API/ABI impact: none. No vector, public record, `MidiDeviceData` field or
application contract changes. Private `CamdBase_intern` grows and the private
ownership helpers change return type. AmigaOS 4 retains last-close cleanup but
does not compile the AROS task.resource hook.

Validation: all changed canonical PR sources compiled and linked into a
linux-x86_64 AROS `camd.library`. `MIDIHubCAMDCompat --v43-task-death`, run by
`tools/camd-task-death-hosted.sh`, forcibly removed 20 independent tasks. Each
task owned a library opener, signalled watch, snapshot, bidirectional published
endpoint and signalled input session. A healthy observer saw every endpoint
departure before the next round; fresh watches and snapshots remained
allocatable. Result: 3 passes, 0 failures on AROS linux-hosted. The patch also
applies cleanly to `4c1510f1d0`. The identical suite against the prior
linux-hosted CAMD 43 reproduced the gap in the first round: after `RemTask`, no
departure event arrived and all three checks failed. Allocation-failure injection, healthy-owner
isolation under simultaneous death and SMP stress remain follow-up tests, not
claimed passes.

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
