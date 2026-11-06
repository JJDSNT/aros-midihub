# MIDI-CI chronological backlog

Statuses: **done** means source and reproducible host tests exist;
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

### CI03 — Complete management codec and responder transitions — ready

- Objective: add Endpoint Inquiry/Reply, ACK/NAK, Discovery responder behavior
  and Invalidate receive behavior for MIDI-CI 1.2.1.
- Dependencies: authoritative 1.2.1 tables available during review; CI02.
- Repository/component: aros-midihub portable MIDI-CI engine.
- Existing interface/behavior: `midici_decode_envelope`, fixed caller-owned
  buffers and `midici_context`; only Discovery is typed today.
- Missing capability: typed management bodies and Initiator/Responder event
  transitions.
- Minimum change: append typed body codecs and event-returning `handle`/`poll`
  calls; do not add I/O or CAMD handles.
- Acceptance: every management message and malformed length/value has a
  vector; unknown future minor fields are ignored safely; reserved major bits
  yield an explicit unsupported-version event.
- Tests: spec-derived vectors plus differential tests against at least one
  independent implementation.
- Upstream PR: engine-only.

### CI04 — Function Block MIDI-CI metadata in CAMD 43 — ready for upstream API review

- Objective: expose the MIDI-CI version and SysEx8 stream capacity already
  carried by UMP Function Block Info.
- Dependencies: maintainer agreement on field names/unknown semantics; PR
  #2101 remains provisional.
- Repository/component: AROS CAMD public topology records and UMP drivers.
- Existing interface: `CAMDFunctionBlockInfoV1`,
  `GetEndpointFunctionBlock`, sized output records.
- Demonstrated missing behavior: neither value is representable; Linux and the
  UMP specification expose them as Function Block metadata.
- Minimum change: append two fixed-width fields before ABI freeze, update
  compile-time sizes/copy validation and populate from drivers.
- Acceptance: old shorter record callers work; full record reports exact known
  values; zero/unknown is documented.
- Tests: 32/64-bit record layout, short/equal/long Size, known Stream message,
  topology update watch.
- Upstream PR: incorporate into draft #2101, not 43.1; isolate commit.

### CI05 — Native UMP driver extension — blocked on callback contract proof

- Objective: let a loaded or resident CAMD driver publish/receive native UMP.
- Dependencies: in-memory driver prototype proving callback residence,
  interrupt/task legality, queue ownership, removal and backpressure.
- Repository/component: AROS CAMD driver boundary; then Network MIDI 2.0/USB
  MIDI 2.0 drivers.
- Existing interface: unchanged `MidiDeviceData` byte callbacks;
  `AddMidiDevice`; private provider operations; app-published UMP endpoint.
- Demonstrated missing behavior: no public generic driver route to
  `CAMDUMPEventV1`.
- Minimum change: optional versioned extension beside `MidiDeviceData`; do not
  expose registry internals or change legacy struct layout.
- Acceptance: legacy drivers unchanged; resident UMP loopback survives
  add/remove; complete events/backpressure/lifetime documented.
- Tests: all MT sizes, SysEx7 sequence, concurrent clients, removal while
  pinned, m68k callback ABI.
- Upstream PR: isolated CAMD commit, preferably folded into #2101 after review.

### CI06 — AROS coordinator concurrency prototype — blocked on runtime test environment

- Objective: validate hybrid ownership before freezing `midici.library`.
- Dependencies: CAMD 43 runtime image, supported Exec IPC choice, CI03.
- Repository/component: AROS test program/service prototype, not Midihub GUI.
- Existing behavior: shared CAMD base/semaphores work; task-without-close leak
  is known; no MIDI-CI service exists.
- Minimum change: private resident coordinator with one endpoint context and
  expiring client requests; no public library vectors yet.
- Acceptance: two clients share one MUID; conflicting operation serializes;
  forced client death releases its logical lease; coordinator restart
  rediscoveries.
- Tests: real AROS/QEMU test with forced task termination and captured events.
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
