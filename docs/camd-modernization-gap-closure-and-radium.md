# CAMD modernization — implementation gap closure and Radium feasibility

**Status:** Follow-up implementation guidance; not a replacement for accepted endpoint decisions.  
**Date:** 2026-10-10  
**Scope:** AROS CAMD v42/v43 evolution, native MIDI 1.0/UMP coexistence, DAW readiness, and possible future Radium port.

## Read first

- [CAMD improvements](camd-improvements.md)
- [Endpoint architecture decision](camd-endpoint-architecture-decision.md)
- [Endpoint architecture review](camd-endpoint-architecture-review.md)
- [Endpoint ABI draft / U01](camd-endpoint-abi-draft.md)
- [MIDI protocol coexistence research](midi-protocol-coexistence-research.md)
- [Implementation and integration plan](implementation-integration-plan.md)
- [Executable CAMD prototype](../prototypes/camd/README.md)

Do **not** start another endpoint architecture, invent a second clock or event graph, or force MIDI 1.0 → UMP → MIDI 1.0. The accepted direction is a shared control plane with native format-specific data paths. Keep CAMD 41.1/42 binary, public-structure, source and behavioral compatibility.

## Gap-to-plan matrix

| Priority | Area | Already planned or demonstrated | Specific remaining work / acceptance |
| --- | --- | --- | --- |
| P0 | Native MIDI 1.0 and UMP | Path-selective hybrid selected; legacy MIDI 1.0 is native; private UMP and software-provider models exist | Complete native MIDI 1.0 *new-session* record and data path; prove MIDI 1.0→1.0 does not transcode; test concurrent MIDI 1.0 and UMP clients. Resolve U01 before public ABI freeze. |
| P0 | Endpoint registry integration | Private registry, stable IDs, generations, snapshots, bounded watches, provider ownership and retirement modeled | Finish AROS driver adapter, bidirectional cluster projection and real runtime session/worker wiring; demonstrate plug/unplug/reconnect without stale pointers, duplicate clusters or broken old clients. |
| P0 | Timestamp scheduling | CAMD v42 CamdTime()/MIDI_SystemClock; shared timestamp policy and bounded queues described | Prototype README explicitly lists timestamp eligibility and delayed dispatch as **not implemented**. Implement deadline-aware dispatch, ordering, cancellation and overflow behavior; test timing under load and across clock domains. Preserve old behavior unless opt-in. |
| P1 | MIDI 2.0 topology | Endpoint, Group, Function Block records and precedence/atomic-update rules defined | Validate complete UMP records, dynamic topology and projections against normative specifications and real or reference test vectors; respect U02–U14 gates. |
| P1 | Public ABI | Sized, pointer-free v43 record draft; AROS vector strategy selected | Resolve native MIDI 1.0 session format and other U01 decisions; compile generated interfaces for m68k and 64-bit; verify all existing vectors, structures, tags and semantics remain unchanged. |
| P1 | Runtime robustness | Preallocated bounded queues, backpressure, worker/fan-out prototypes | Complete interrupt-to-task handoff and adapter wiring; stress shutdown, queue-full, SysEx, simultaneous clients, resource bounds and failure recovery. |
| P2 | MIDI-CI | Capability extensibility acknowledged; MIDI-CI and SysEx8 fields intentionally deferred pending normative review | Audit missing discovery/message primitives; define optional MIDI-CI service boundary (Profiles, Property Exchange, protocol negotiation where applicable). Do not embed full MIDI-CI application logic in CAMD without evidence. |
| P2 | DAW-grade audio sync | Shared clock/timestamps planned; CAMD intentionally independent of audio engine | Define clock correlation, latency reporting and audio timestamp conversion contract for AHI/other engines; validate jitter and scheduling against sequencer workloads. Do not make CAMD depend on AHI. |

**Status discipline:** “documented,” “private prototype,” “wired into patched AROS,” “upstream,” and “tested on hardware” are distinct states. Verify each in the current code and report evidence; this matrix is a focused work plan, not a claim that every listed feature is absent or present upstream.

## Agent execution order

1. Audit actual HEAD, prototypes, patches and tests against the matrix. Cite source paths, tests and commits for each status; avoid repeating already-completed work.
2. Finish the real CAMD adapter / native MIDI 1.0 new-session path and reverse cluster projection without breaking the native legacy data path.
3. Implement timestamp-based eligibility and delayed dispatch on the existing clock/queue infrastructure. Define clock domain, timestamp validity, immediate-vs-future events, tie-breaking, cancellation, late-event policy and bounded resource behavior.
4. Validate P0 with the CAMD compatibility suite and targeted concurrency, timing, reconnection and m68k footprint tests.
5. Resolve U01 and test generated ABI on supported architectures **before** freezing public vectors or layouts.
6. Advance MIDI 2.0 topology, MIDI-CI extension hooks and audio clock correlation only behind their existing review gates.

**Deliverable:** Update the existing implementation plan with an evidence-backed matrix (implemented / prototype / planned / missing / blocked), code locations, dependency gates, acceptance tests and small incremental tasks. Implement P0 items that do not require unresolved public ABI decisions. No parallel subsystem or wholesale redesign.

## Future application feasibility: Radium for AROS

Upstream: https://github.com/kmatheussen/radium  
Project: https://www.radium.dog/

Radium is a useful **future compatibility/DAW workload**, not a current CAMD prerequisite or a committed port.

### Findings from source inspection

- `midi/camd/` contains a historical CAMD backend using `CreateMidi()`, `AddMidiLink()`, `LockMIDI()`, and `NextCluster()`.
- `amiga/` contains historical Amiga player, graphics, memory and semaphore code. Presence of these files **does not establish current buildability**.
- The modern build uses Qt 6 and C++20 (`Makefile.Qt`) plus substantial audio/plugin infrastructure; a complete m68k port is therefore much harder than CAMD integration.
- The historical cluster picker in `midi/camd/midi_get_clustername.c` holds `LockMIDI(CD_Linkages)` while presenting an interactive selection menu; investigate releasing the lock after copying names, especially with dynamic endpoints.
- Historical backend selects cluster names, not modern stable endpoint IDs; add a compatibility layer or port enhancement for endpoint watches, persistent identity, reconnection and topology changes.
- The old CAMD backend must not be assumed to be the MIDI backend of the current Qt application without tracing the active build configuration.

### Feasibility investigation (no port yet)

1. Identify a stable historical Amiga-capable Radium revision and its build requirements; attempt a reproducible AROS build of the smallest editor/player subset.
2. Independently trace the **current** Radium MIDI/audio/GUI dependencies and identify which features can be isolated or replaced. Compare reduced native GUI vs full Qt port; do not promise either.
3. Build a minimal CAMD integration smoke test: enumerate legacy clusters, send/receive MIDI 1.0, attach to a dynamic endpoint's legacy projection, disconnect/reconnect, recover selection, and measure timing/jitter.
4. If a modern API is used, test endpoint snapshots/watches and native UMP separately. Never claim that historical CAMD integration gives Radium MIDI 2.0 automatically.
5. Document licensing, dependency availability, performance and RAM footprint, particularly for m68k.
6. Only propose a port milestone after a successful build proof-of-concept and a written feasibility decision.

### Decision criteria

A future Radium port should **validate** that CAMD can serve a sophisticated sequencer without forcing transport-specific knowledge into the application. It should not drive speculative CAMD APIs, break legacy compatibility or pull GUI/audio/plugin code into CAMD. Prefer a lightweight tracker/editor proof-of-concept first; consider full modern Radium on faster AROS targets separately.

## Next review checkpoint

The agent should return: (1) code-backed status matrix, (2) P0 patch/test plan, (3) U01 blockers, and (4) a short Radium feasibility backlog with no premature implementation commitment.
