# MIDI-CI chronological backlog

Statuses: **done** means source and reproducible host tests exist;
**in progress** means an independently testable subset exists but stated
acceptance criteria remain;
**ready** means public interfaces/current behavior, missing capability,
minimum change, real dependencies and validation are identified;
**blocked** means one of those facts is still missing.

## P0 sequence

### CI01 — Freeze the evidence baseline — done

- Objective: pin CAMD 42, the full CAMD 43 proposal and external reference
  implementations.
- Dependencies/blockers: none.
- Repository/component: aros-midihub documentation.
- Files/APIs: the five `docs/MIDI_CI_*` artifacts; CAMD V43 vectors and records.
- Implementation: record exact revisions and distinguish source evidence from
  claims.
- Acceptance: every architectural claim traces to a specification page,
  immutable source revision or named unvalidated inference.
- Tests: `git diff a0ca679f44..4c1510f1d0`; source searches recorded in the
  research document.
- Upstream PR: none.

### CI02 — Portable management foundation — done

- Objective: establish a C99 sans-I/O core without selecting a service ABI.
- Dependencies: CI01.
- Repository/component: aros-midihub `src/midici.c`.
- Files/APIs: `include/midici/midici.h`; no AROS public vectors yet.
- Implementation: Discovery/Reply and Invalidate codecs, MUID lifecycle,
  bounded transaction correlation/deadlines/cancellation.
- Acceptance: byte-exact known Discovery vector round-trips; malformed frames
  fail; equal MUIDs recover; duplicate operation conflicts; deadlines expire.
- Tests: `make build/midici-test && ./build/midici-test`; sanitizer run.
- Upstream PR: separate Midihub engine commit, no CAMD change.

### CI03 — Complete management codec and responder transitions — in progress

- Objective: add Endpoint Inquiry/Reply, ACK/NAK, Discovery responder behavior
  and Invalidate receive behavior for MIDI-CI 1.2.1.
- Dependencies: authoritative 1.2.1 tables available during review; CI02.
- Repository/component: aros-midihub portable MIDI-CI engine.
- Existing interface/behavior: fixed caller-owned buffers and
  `midici_context`; Discovery, Endpoint Inquiry/Reply, Invalidate MUID,
  ACK and both the 1.1 and 1.2 NAK forms now have typed codecs. The initial
  Responder handles Discovery, both MUID-collision options, Endpoint
  Information, Invalidate receive and structured error NAKs.
- Missing capability: the Initiator peer/discovery-round state machine and
  its event/correlation path; differential execution against a separately
  maintained implementation.
- Minimum remaining change: append bounded Initiator peer state and
  event-returning `handle`/`poll` calls; do not add I/O or CAMD handles.
- Acceptance: every management message and malformed length/value has a
  vector; unknown future minor fields are ignored safely; reserved major bits
  yield an explicit unsupported-version event.
- Tests: the spec-derived Management vectors and malformed cases pass on the
  host; differential tests against at least one independently maintained
  implementation remain required.
- Upstream PR: engine-only.

### CI04 — Function Block MIDI-CI metadata in CAMD 43 — hosted-validated

- Objective: expose the MIDI-CI version and SysEx8 stream capacity already
  carried by UMP Function Block Info.
- Dependencies: maintainer agreement on field names/unknown semantics; PR
  #2101 remains provisional. No implementation dependency remains for the
  requested Linux-hosted validation scope.
- Repository/component: AROS CAMD public topology records and UMP drivers.
- Existing interface: `CAMDFunctionBlockInfoV1`,
  `GetEndpointFunctionBlock`, sized output records.
- Demonstrated missing behavior: neither value is representable; Linux and the
  UMP specification expose them as Function Block metadata.
- Implemented proposal: append two fixed-width fields before ABI freeze and
  make publication arrays use each record's `Size` as their stride. This
  preserves 104-byte publisher arrays as well as short output readers. Patch:
  `patches/aros-camd-function-block-capabilities.patch`.
- Acceptance: old shorter record callers work; full record reports exact known
  values; zero/unknown is documented.
- Tests: canonical-source registry tests pass for 104-byte arrays, 112-byte
  values, offsets 104/108, invalid values and inconsistent strides. The public
  header canary compiles with `x86_64-aros-gcc`. On AROS Linux-hosted, the
  known `f0118037 05010205 00000000 00000000` Stream fixture, legacy and full
  publication, zero extension and guarded 104-byte output pass 8/8. The
  unpatched library fails the four new capability checks. CAMD contract/v42/v43
  sections pass 71/38/229; the aggregate debug-driver reload check fails
  identically in the baseline. No AArch64 result is claimed per current scope;
  a dedicated topology-watch assertion is a non-blocking follow-up.
- Upstream PR: incorporate into draft #2101, not 43.1; isolate commit.

### CI05 — Native UMP driver extension — ready for API review

- Objective: let a loaded or resident CAMD driver publish/receive native UMP.
- Dependencies: maintainer review of the optional adjacent-tail extension;
  then an in-memory driver prototype proving callback residence, task-context
  submission, queue ownership, removal and backpressure.
- Repository/component: AROS CAMD driver boundary; then Network MIDI 2.0/USB
  MIDI 2.0 drivers.
- Existing interface: unchanged `MidiDeviceData` byte callbacks;
  `AddMidiDevice`; private provider operations; app-published UMP endpoint.
- Demonstrated missing behavior: no public generic driver route to
  `CAMDUMPEventV1`.
- Minimum change: leave `MidiDeviceData` byte-for-byte unchanged and add a
  flagged, sized extension in an adjacent composite registration. It supplies
  complete UMP send/submit, per-port query and change callbacks and adapts to
  the existing private provider core. No new application vector is required.
- Acceptance: legacy drivers unchanged; resident UMP loopback survives
  add/remove; complete events/backpressure/lifetime documented.
- Tests: all MT sizes, SysEx7 sequence, concurrent clients, removal while
  pinned, m68k callback ABI.
- Upstream PR: isolated CAMD commit, preferably folded into #2101 after review.

### CI05A — Reclaim endpoint objects after task death — implemented; upstream review pending

- Objective: prevent dead CAMD clients from exhausting endpoint object slots
  or leaving stale task pointers as signal targets.
- Dependencies: satisfied by task.resource 1.4 in the PR #2101 baseline and
  the AROS linux-hosted task-removal environment.
- Repository/component: AROS CAMD private endpoint owner/client lifecycle.
- Existing interface/behavior: `endpoint_owner.c` only cleans on a task's last
  `CloseLibrary()` and explicitly leaves objects when the task terminates;
  `OwnEndpointObject()` also silently loses ownership on allocation failure.
- Implemented minimum change: no public API. Register one `TNA_REMOVED` hook, immediately
  neutralize that task's signal targets, mark its owned objects dead and defer
  provider/session cleanup to safe task context. Make ownership registration
  fallible and roll back a newly created object on `NOMEM`.
- Acceptance: repeated dead owners do not exhaust 16 watches, 32 snapshots,
  64 receivers or 32 publishers; statistics return to baseline; stale handles
  fail; healthy owners survive; ownership-allocation failure rolls back.
- Test result: `tools/camd-task-death-hosted.sh` removed 20 tasks, each holding
  an opener, signalled watch, snapshot, publisher and signalled input session.
  The observer received every departure and capacity recovered after every
  death: 3 passes, 0 failures on AROS linux-hosted. Canonical PR source builds
  and clean patch application pass. The same test against the prior CAMD 43
  failed in round zero because the dead endpoint produced no departure event,
  reproducing the missing cleanup. Allocation-failure injection,
  healthy-owner isolation and SMP stress remain P1 validation work; host
  registry tests cannot validate task-resource ordering.
- Upstream PR: private CAMD lifecycle commit, separate from Function Block and
  native-driver API patches. Patch:
  `patches/aros-camd-endpoint-task-death.patch`.

### CI06 — AROS coordinator concurrency prototype — in progress

- Objective: validate hybrid ownership before freezing `midici.library`.
- Dependencies: CAMD 43 runtime image, supported Exec IPC choice, CI03.
- Repository/component: AROS test program/service prototype, not Midihub GUI.
- Existing implementation: `MIDICICoordProbe` is a private resident Exec task
  with one named message port, task-ID plus task-pointer ownership and exact
  transaction tokens. It is deliberately not a service ABI.
- Demonstrated on raspi-aarch64 AROS/QEMU: two independent tasks race one
  operation (one owner/one busy); exact cancellation releases it; a task exits
  without cancellation; the next request detects the dead owner and recovers;
  shutdown drops remaining ownership. Ten checks passed.
- Acceptance: two clients share one MUID; conflicting operation serializes;
  forced client death releases its logical lease; coordinator restart
  rediscoveries.
- Remaining acceptance: bind the prototype to one engine MUID, exercise
  coordinator restart/fresh MUID/rediscovery, and kill a task rather than only
  letting it return abnormally.
- Tests: `tools/midici-coordinator-qemu.sh`; real AROS/QEMU log recorded in
  `MIDI_CI_VALIDATION.md`.
- Upstream PR: none until the prototype selects a public contract.

### CI07 — Profile Configuration — blocked on CI03 and service gate

- Objective: typed Profile messages and state transitions.
- Dependencies: CI03, CI06, Profiles 1.1 vectors.
- Repository/component: engine then coordinator.
- Minimum change: bounded Profile IDs/state cache and initiator/responder
  events; no Profile state in CAMD.
- Acceptance/tests: inquiry, enable/disable, added/removed, details, specific
  data; unsupported target; disconnect invalidation; two clients conflict.
- Upstream PR: engine/library only.

### CI08 — Property Exchange 1.2 — blocked on CI03 and resource limits

- Objective: PE capabilities, bounded transactions, chunking/reassembly,
  Mcoded7, subscriptions and error behavior.
- Dependencies: exact M2-103 v1.2 review; CI06 ownership; agreed AROS maximums.
- Existing interface: generic transaction IDs/deadlines only.
- Minimum change: streaming header codec, caller-provided body storage and
  bounded assembler; no DOM requirement and no unbounded allocation.
- Acceptance/tests: out-of-order/duplicate/missing chunks, 3 s timeout,
  simultaneous-request limit, cancellation, malformed JSON headers, max body,
  subscription cleanup and independent interop transcript.
- Upstream PR: engine/library only.

### CI09 — Process Inquiry — blocked on CI03

- Objective: capabilities and MIDI Message Report sequence.
- Dependencies: Process Inquiry 1.2.1 review and application callback model.
- Minimum change: typed messages/events; CAMD continues transporting report
  MIDI as ordinary traffic.
- Acceptance/tests: report begin/selected messages/end, unsupported and
  interrupted report.
- Upstream PR: engine/library only.

## P1 integration and validation

### CI10 — Public `midici.library` and coordinator API

- Dependencies: CI06 passes; ABI review on every pointer/lifetime/ownership
  rule.
- Expected implementation: sized pointer-free records, appended vectors,
  asynchronous result events and explicit cancellation. GUI-independent.
- Acceptance: old clients are not applicable because this is a new library;
  cross-architecture layouts and service restart behavior are mandatory.
- Tests: m68k/i386/AArch64/x86-64 compile, two-process runtime suite, low-memory
  and stale-handle tests.
- Upstream PR: new AROS library/service series separate from CAMD changes.

### CI11 — MIDI-CI Workbench and independent interop

- Dependencies: accessible named Workbench release or another independently
  verified peer; CI07–CI09 as applicable.
- Expected implementation: captured, redacted byte transcripts and exact tool
  version/configuration.
- Acceptance: Discovery plus each supported category passes both Initiator and
  Responder scenarios; failures remain recorded.
- Tests: repeatable procedure in `MIDI_CI_VALIDATION.md`.

### CI12 — Midihub management integration

- Dependencies: CI10 stable public API.
- Expected implementation: endpoint status, profile/property management and
  persistent desired configuration through the public library only.
- Acceptance: closing/killing the GUI does not stop MIDI-CI or invalidate
  application access.
- Upstream PR: Midihub-only, separate from CAMD and engine commits.

## P2 follow-ups

- Full UMP Endpoint Device Identity metadata in CAMD after a consumer and
  descriptor/Stream precedence rules are demonstrated.
- SysEx8 MIDI-CI adaptation only if a normative MIDI-CI use and transport need
  is demonstrated; MIDI-CI 1.2 Discovery remains SysEx7-compatible.
- Performance/stress tuning after correctness: long-running reconnect,
  thousands of PE transactions, queue pressure and memory fault injection.
