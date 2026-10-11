# CAMD / System MIDI Out / MIDI Filters — integration and delivery plan

**Status:** Design and implementation guidance. None of the three future features is declared implemented by this document.

## Scope and ownership

| Layer | Owns | Must not own |
|---|---|---|
| CAMD | Legacy MIDI 1.0 and native UMP data paths, endpoint topology, capability/discovery primitives, explicit conversion boundaries and compatibility views | Default-output policy, filter processing, synthesis |
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

## HEAD evidence audit and P0 closure plan

**Audit point:** repository `e17195d`, with CAMD implementation through
`92b7100`. The labels below are deliberately distinct: **implemented** means
present in the patched AROS runtime, **prototype** means executable host design
evidence, **planned** means specified but not implemented, and **blocked** means
an explicit decision or external validation is required.

| Area | Status at audit point | Code-backed evidence | Remaining acceptance / dependency |
|---|---|---|---|
| CAMD 41.1/42 compatibility | Implemented in the patch series; host compatibility checks pass | `patches/aros-camd-v42.patch` through `patches/aros-camd-legacy-output-backend.patch`; legacy entry points remain before the proposed v43 boundary in `workbench/libs/camd/camd.conf`; host suite is driven by `Makefile` | Re-run the unchanged compatibility program and legacy drivers on each supported AROS target and QEMU after every runtime slice; hardware status must be reported separately |
| Shared endpoint control plane | Implemented privately in patched AROS; exercised more deeply as a host prototype | `prototypes/camd/endpoint_registry.[ch]`, `provider_contract.[ch]`, `tests/camd_endpoint_core.c`, `tests/camd_provider_contract.c`; copied into AROS by `patches/aros-camd-endpoint-core.patch` | Native AROS concurrency/retirement stress, provider failure injection and loaded-driver lifecycle integration |
| Native MIDI 1.0 new-session path | Output and input work end to end on hosted AROS | `legacy_driver_adapter.[ch]`, `legacy_output_backend.[ch]`, `legacy_driver_output_backend.[ch]`; tests `camd_legacy_driver_adapter.c` and `camd_legacy_output_backend.c`; commits `dcf8908`, `6ad1dc1`, `ce9521e` | Instantiate from `LoadDriver()`, add the input-side interrupt-to-task bridge, and prove MIDI 1.0→MIDI 1.0 event/SysEx equivalence with a zero conversion count |
| Native UMP path | Prototype only | Separate UMP provider/session callbacks in `provider_contract.[ch]`; complete 32/64/96/128-bit records in `native_event_queue.[ch]`; provider/queue tests | No AROS transport is registered and no end-to-end native UMP runtime test exists; normative UMP/topology work remains behind U02–U14 |
| Bounded queues and backpressure | Implemented as private AROS-capable primitives; host-tested | `native_event_queue`, `native_event_pump`, `native_event_worker`, `native_worker_fanout`; commits `907e9f8` through `8b4258a` | Exercise real loaded sessions under queue-full, SysEx, concurrent clients and shutdown; input enqueue is still not interrupt-safe |
| Stable identity | Host-tested map, codec and recovery transaction; the AROS store creates and reloads IDs on raspi-aarch64 under QEMU | `identity_map.[ch]`, `identity_file.[ch]`, `identity_store_recovery.[ch]`, `legacy_identity_key.[ch]`, their tests, and the identity store/recovery patches | Recovery from damaged and half-replaced files is checked on a hosted filesystem by `tools/camd-compat-hosted.sh`; failing DOS calls mid-write are only covered by the host model. Still to do: handle simultaneous active duplicate evidence in the registry |
| Loaded legacy-driver publication | Both directions implemented in patched AROS; verified on hosted AROS and raspi-aarch64 under QEMU | `driver_endpoints.c` in `patches/aros-camd-driver-endpoints.patch`: `LoadDriver()` resolves the stored provider and per-port IDs, creates the output backend and adapter, and `FreeDriverData()` retires them first | Retirement with live sessions is untested |
| Reverse legacy cluster projection | Planned | Contract in `camd-endpoint-architecture-decision.md` and `camd-endpoint-abi-draft.md`; no implementation file or test exists | Define collision-safe persisted projection names, atomic topology replacement, offline participant behavior and explicit unrepresentable/loss diagnostics |
| Timestamp eligibility and delayed dispatch | Planned | Existing v42 `CamdTime()`/`MIDI_SystemClock` work is documented in `camd-improvements.md`; native records already carry timestamp fields | Decide validity/immediate encoding and clock domain at U01, then implement deadline ordering, late policy, cancellation and bounded wake/timer behavior without changing legacy default timing |
| Public v43 client ABI | Provisional MIDI 1.0 slice implemented; rest blocked at U01 | `patches/aros-camd-endpoint-client.patch`, `-input`, `-watch` and `-publish`: snapshot, session open/close, MIDI 1.0 and SysEx send, drain, receive, endpoint watches and program-published endpoints; `MIDIHubCAMDCompat --v43` passes 92 checks on hosted AROS | Upstream review of names, registers and records; topology, UMP, counters and the multi-target layout checks below |

### Small incremental P0 patches

These are ordered dependencies, not a new subsystem:

1. (Done for damaged and half-replaced files on hosted AROS.) Complete native DOS/filesystem fault injection for the host-proven and
   AROS-compiled `uuid.library` plus main/`.new`/`.bak` recovery transaction.
   Identity failure already leaves legacy CAMD initialized; automatic
   publication must remain disabled when the private store is unavailable.
2. Give each loaded `Drivers` object private ownership of its identity,
   fixed-port adapter and output binding. Resolve provider plus per-port keys,
   publish only after all required objects exist, and unwind in reverse order
   on any failure. Do not alter the existing clusters or route their traffic
   through endpoint sessions.
3. Instantiate output sessions against the existing `DriverData` shared-open
   and capacity-relay path. Add fault-injection tests for every allocation,
   attach and physical-open boundary, plus retryable retirement.
4. Add a bounded input handoff from driver/interrupt context to task context
   and format-fixed MIDI 1.0 receive sessions. Quiesce the producer before
   detaching or freeing session state.
5. Implement the reverse projection as a separate registry consumer. Publish
   only representable Endpoint + Group + direction tuples and preserve stable
   cluster names, links and offline state across reconnect.
6. Add timestamp eligibility to the existing queue/pump/worker only after the
   U01 timestamp envelope is decided. Immediate mode must retain current
   behavior; future scheduling is opt-in and bounded.

### P0 acceptance set

- An unchanged legacy sender and fixed driver exchange short messages and
  SysEx through their original cluster path while an endpoint client uses the
  same physical port; the device opens once and no UMP conversion function is
  called.
- A native MIDI 1.0 session sends and receives short messages and SysEx with
  byte-equivalent output, stable ordering and a zero conversion counter.
- Concurrent legacy, native MIDI 1.0 and UMP software sessions retain their
  own record boundaries; pressure or retirement in one path does not reinterpret
  or corrupt another path.
- Queue-full, oversized SysEx, blocked transmitter and producer wake are
  observable and bounded; retry never duplicates an accepted prefix.
- Driver unload and provider retirement race snapshots, opens, callbacks and
  queued work without stale pointers. New opens fail, outstanding leases drain,
  and final storage is released exactly once.
- Restart preserves committed provider/port IDs. Interrupted identity-file
  replacement recovers the last complete snapshot; corrupt and duplicate
  records are rejected. Storage failure publishes only ephemeral identity.
- Projected clusters neither duplicate nor change name across offline/reconnect;
  endpoint-wide or unrepresentable UMP traffic is not silently projected.

### U01 blockers at this audit point

Host prototypes already cover record-size assertions on the host compiler,
stale-handle reuse, snapshot/update races, watch overflow/resynchronization,
topology rollback, provider callback lock exclusion and substantial retirement
behavior. They do **not** close U01. The remaining blockers are:

- exact public function names, vector order, proposed version 43, m68k register
  assignments, numeric namespaces and generated interfaces;
- the final public native MIDI 1.0 record/stream shape and whether it ships in
  the first v43 surface;
- session format/conversion policy and the shared timestamp validity/envelope;
- compile-time layout checks on m68k, i386, AArch64 and x86-64, including every
  shorter/equal/longer `Size` case;
- native AROS races for open/offline/retire, provider shutdown with live
  callbacks/queues/sessions, and unchanged legacy-driver operation;
- real MIDI 1.0→MIDI 1.0 and UMP→UMP zero-conversion tests, plus exactly-once
  boundary conversion with reject/drop/approximate loss policy;
- Function Block versus GTB precedence with normative vectors; and
- final identity-database packaging/recovery policy and administrative tooling.

No public v43 header, tag, vector or numeric constant is frozen until these
items receive upstream review and the acceptance evidence is recorded.

### Radium feasibility backlog (not a CAMD dependency)

1. Identify and reproduce-build a historical Amiga-capable revision, initially
   limiting scope to the smallest editor/player workload.
2. Trace the current Qt 6/C++20 MIDI, audio, GUI and plugin dependencies and
   compare a reduced native GUI with a faster-target Qt port; promise neither
   before a build proof.
3. Build a small legacy CAMD workload that copies cluster names while locked,
   releases the lock before interaction, sends/receives MIDI 1.0 and survives a
   projected endpoint disconnect/reconnect.
4. Separately exercise endpoint snapshots/watches and native UMP; the historical
   cluster backend is not evidence of MIDI 2.0 support.
5. Record licensing, unavailable dependencies, jitter, CPU and RAM—especially
   on m68k—then make an explicit port/no-port decision.

## Shared integration acceptance tests

- **Legacy coexistence:** unmodified CAMD applications and legacy MIDI drivers continue working alongside native UMP clients.
- **Native MIDI 1.0 path:** MIDI 1.0 producer → CAMD/Router → MIDI 1.0 consumer preserves messages and SysEx with zero UMP conversions.
- **Native end-to-end:** modern producer → CAMD native UMP → Router (optional filters) → native endpoint, with exact packet boundaries, Groups, timestamps and declared capabilities.
- **Legacy bridge:** MIDI 1.0 sender → compatible projection → MIDI 2.0 destination, and the reverse, with explicit conversion-loss diagnostics.
- **Default output:** multiple producers → System MIDI Out → selected Synth/device, with atomic destination switch, no routing loops and safe disconnect/reconnect.
- **Filter independence:** filters work on arbitrary routes; System MIDI Out works with zero filters.
- **Real-time safety:** stress concurrent senders, filter amplification, overflow, SysEx7/8, hot-plug and teardown; verify bounded resources, ordering and no deadlocks/use-after-free.
- **Portability:** test supported AROS 32/64-bit targets and QEMU, and document unavailable hardware or normative test vectors as blocked rather than passed.

## Agent handoff rules

Before changing CAMD or Router interfaces, read the CAMD improvements and
integration documents, the endpoint review, the accepted endpoint decision,
the endpoint ABI draft, and the System MIDI Out and MIDI Filters designs.
Treat **open design gates** as decisions requiring explicit resolution, not
invitations to use hacks. Record an architecture decision for irreversible
public ABI or protocol-mapping choices. Keep changes small and independently
reviewable, add tests with each implementation increment, and update status
based on observed evidence only.

The accepted coexistence model is the path-selective hybrid documented in
[the research decision support](midi-protocol-coexistence-research.md).
Provider callbacks and public event vectors must not be frozen until native
MIDI 1.0 and UMP format-specific paths, plus explicit conversion policy, are
represented in the contract.

**Do not claim full MIDI 2.0 support after a basic UMP loopback or Note On test.** Use the M0–M5 and U01–U14 gates from CAMD documentation. Do not declare System MIDI Out or Filters complete without their respective acceptance scenarios.


## Mandatory pre-M0 endpoint architecture review

Before CAMD dynamic endpoint work, read
[CAMD endpoint architecture review](camd-endpoint-architecture-review.md).
The old USB MIDI driver implementation is replaceable; the legacy CAMD
application ABI and working USB MIDI functionality are not. Compare endpoint
provider models, choose a transport-neutral identity/topology/lifecycle
contract, and record the decision before freezing public APIs. Do not use
fixed `NPorts` pools as the modern architectural model.

The comparison is complete and option B is selected in
[the CAMD endpoint architecture decision](camd-endpoint-architecture-decision.md).
Implementation must follow that central registry/provider model. Gate U01
still blocks public function names, layouts and library vectors pending ABI
and upstream review. The concrete review artifact is the
[CAMD endpoint ABI draft](camd-endpoint-abi-draft.md).

### Final architecture first — no interim dynamic-port implementation

The pre-M0 comparison is for **choosing the final design**, not authorizing a temporary migration architecture. Do not spend implementation effort on provisional `NPorts` pools, USB-specific registration extensions or APIs expected to be discarded. Approve the endpoint registry/provider, identity, lifecycle, UMP topology and legacy-projection contracts first, then implement that architecture directly through small verifiable commits. Legacy application ABI and working USB functionality remain mandatory; old USB internals are replaceable. If unresolved, escalate rather than invent a transitional workaround.
