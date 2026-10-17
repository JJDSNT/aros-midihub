# AROS upstream maintainer review — audit plan

**Status:** Planned; read-only audit not yet completed.  
**Scope:** [JJDSNT/aros-midihub](https://github.com/JJDSNT/aros-midihub) and open contributions to [aros-development-team/AROS](https://github.com/aros-development-team/AROS).  
**Date recorded:** 2026-10-17.

## Motivation

The upstream CAMD fix [b1edfb9](https://github.com/aros-development-team/AROS/commit/b1edfb98664776c8d96f371bd030f16b6d7b6558) corrected a real x86-64 driver-discovery bug by aligning the scan for `MidiDeviceData`. The follow-up [1ce6fd9](https://github.com/aros-development-team/AROS/commit/1ce6fd9a984d9e9543b99e544a9a28cfdf7527a1) replaced hand-written alignment with `AROS_ROUNDUP2`, simplified the loop, and clarified the header layout. This was a maintainability refinement, **not evidence of another functional bug**. Future reviews should catch existing AROS idioms before submission.

## Audit order

1. [#1507 — BLE MIDI class](https://github.com/aros-development-team/AROS/pull/1507): task and callback lifetime, memory safety, GATT concurrency, CAMD bridge, shutdown.
2. [#1586 — Network MIDI CAMD drivers](https://github.com/aros-development-team/AROS/pull/1586): adapter ownership, cross-task lifetime, ABI/alignment, legacy compatibility, service restarts.
3. [#1591 — bonami.library / mdnsd](https://github.com/aros-development-team/AROS/pull/1591): message-port IPC, cancellation, late replies, resource cleanup, native Exec conventions.
4. [#1587 — RTP-MIDI library](https://github.com/aros-development-team/AROS/pull/1587): bounds, SysEx, journal recovery, timestamp arithmetic, loss/reordering, interoperability.
5. [#1588 — UMP / Network MIDI 2.0](https://github.com/aros-development-team/AROS/pull/1588): specification compliance, bit scaling, endian conversion, malformed packets, native MIDI 1.0 and 2.0 paths.

Then audit code in this repository not yet upstreamed, notably dynamic CAMD endpoints, network services, router and synthesizer integration.

## Required review lenses

- **Native-first:** search the AROS master tree for an existing macro, helper, API, build convention or analogous driver before adding custom code. Record the alternative considered.
- **Portability/ABI:** explicitly review m68k, x86 and x86-64; `BPTR`, `IPTR`, `APTR`, structure packing, alignment, pointer arithmetic, byte order and library ABI.
- **Lifetime/concurrency:** resource ownership, task termination, semaphore ordering, message ownership, callbacks, timeout/cancellation, hot-unplug, service restart and failure paths.
- **Compatibility:** legacy CAMD 1.0 clients and drivers, endpoint identities, native MIDI 1.0 versus UMP, old Bluetooth and network behavior.
- **Validation:** compile/test on relevant targets, targeted regressions, negative/fault-injection tests and interoperability evidence. Never label an unexecuted test as passed.

## Findings format

For each finding record: **PR / path / line or symbol / severity / category / observed evidence / upstream precedent / suggested fix / regression test / verification status**.

Categories: **confirmed bug**, **plausible risk requiring reproduction**, **portability/ABI concern**, **maintainability/idiom**, **documentation/style**. Do not conflate stylistic cleanups with bugs. Severity: critical / high / medium / low. Clearly separate hypotheses from validated defects.

## Workflow / decision gate

1. Inspect the complete PR diff and surrounding implementation; compare equivalent AROS master implementations and existing project tests.
2. Produce a **read-only findings report first**. Do not silently change branches, open issues or push PR updates during the audit.
3. Review and prioritize findings with the project owner. Fix only approved items, with targeted tests, on the appropriate source branch.
4. Re-run compatibility tests and review the final diff as an AROS maintainer would before requesting upstream review.

Related: [upstream PR tracker](upstream-prs.md), [PR dependency graph](upstream-pr-graph.md), [CAMD endpoint architecture review](camd-endpoint-architecture-review.md).

## Initial read-only audit notes (2026-10-17)

These are **observations and hypotheses**, not confirmed defects. No upstream branches were changed.

### #1507 — BLE MIDI

- The earlier upstream review correctly rejected publishing hardware as a CAMD application node; the current PR now uses a `MidiDeviceData` driver in `DEVS:Midi`. Do not reopen that resolved architecture issue.
- `rom/bluetooth/classes/btmidi/btmidi_central.c`, `btmidi_force_binding` and `btmidi_binding_task`: the failed-start path calls `cleanup(binding)` **before** signalling the parent, explicitly addressing the previous use-after-free risk. Verify the parent/child handshake under rapid start/failure/stop and with the AROS task-switching model.
- `btmidi_release_binding` waits for `binding->task` to clear; check all early exits, signal delivery and potential task context restrictions. Compare with existing AROS Bluetooth class shutdown patterns.
- `btmidi_camd.c` uses `Forbid()` for owner reservation and a semaphore for receive/close exclusion; review lock ordering and callback reentrancy, especially whether CAMD receive hooks can re-enter close.

### #1586 — Network CAMD drivers

- `workbench/devs/midi/netmidi/netmididriver.c`, `Init`: allocates public adapter state and per-port TX buffers, publishes a named semaphore.
- `Expunge`: removes the named semaphore, marks `aa_Version` as gone and deliberately leaves adapter allocations alive because a service may retain references. **Potential bounded/unbounded retention concern** depending on real driver reload and service shutdown behavior; establish lifecycle and compare with upstream conventions before calling it a leak.
- `OpenPort` / `ClosePort` serialize via `ad_Lock`; examine whether all service callbacks, interrupt paths and expunge use compatible synchronization and whether stale adapter pointers can be dereferenced after code unload.
- Verify driver module format, alignment and `MidiDeviceData` discovery on hosted x86-64, explicitly covering the recently fixed CAMD scan.

### Next verification steps

1. Read complete implementations and corresponding AROS master references (Bluetooth classes, USB CAMD driver, Exec semaphores and CAMD loader).
2. Trace state transitions and ownership across all tasks and service attachments.
3. Run focused tests for failure injection, rapid reconnect, unload/reload and 32/64-bit builds.
4. Record line-precise, reproducible findings separately from stylistic suggestions before proposing fixes.

## Upstream reference comparison — second pass (2026-10-17)

Read `rom/usb/classes/camdmidi/camd/poseidonusb.c` from AROS master alongside `btmidi_camd.c`, `btmidi_central.c` and `netmididriver.c` from the respective PR heads.

- **Confirmed design consistency:** both new drivers publish a `MidiDeviceData` record with `__attribute__((used))`, like the upstream USB CAMD driver. This is the correct hardware-driver layer, not an application-side `CreateMidiA` shortcut.
- **#1507 startup cleanup:** `btmidi_binding_task` cleans up before signalling a failed start. The shutdown path aborts/waits for Bluetooth channels, detaches the CAMD task, releases signals and closes timer resources. This narrows earlier lifetime concerns, but concurrent callbacks and cleanup still require execution tests.
- **#1586 expunge policy:** `netmidi_camd.h` explicitly says CAMD normally loads the driver once; `Expunge` marks adapters gone and deliberately retains allocations to protect references held by the network service. **Do not assert a leak from this alone.** Verify if expunge/reload is reachable and whether stale service references are checked before *every* access, including callbacks.
- **#1586 TX behavior:** `GetFromCAMD` drains pending CAMD bytes when no service is attached; when attached, it fills a power-of-two ring and sets `ad_TXMore` on backpressure. Review the service-side ring consumer and signal/task detachment under concurrent restart.
- **Legacy USB caveat:** `poseidonusb.c` provides a valid CAMD ABI reference but contains its own historical implementation limitations; do not copy its locking or buffer handling unquestioningly.

**Verification status:** code inspection only; no cross-target builds, race reproduction or runtime tests executed. No confirmed new defect at this stage.

## Driver unload/reload — revisit an existing deferred decision (2026-10-17)

**Existing decision, not a new discovery:** `docs/camd-improvements.md` §1.3 explicitly rejects unloading drivers merely because a USB or network device disconnects, and says runtime driver-file replacement/unload is not planned. `docs/camd-backlog.md` references that decision; `docs/camd-endpoint-abi-draft.md` assumes loaded drivers persist until `camd.library` expunges.

**Reopen the scope distinction, not the hot-plug architecture:**
- **Device disconnect / network peer offline:** retain loaded transport driver; publish endpoint offline/disconnected state. Do not tie driver unload to hot-plug.
- **Administrative driver upgrade/replacement / recovery:** investigate controlled quiesce, unload and reload (or safe replacement without unload) as a separate future capability.

**Research gate:** inventory CAMD's actual driver-load and `RethinkCAMD()` paths; establish how open ports, callbacks/soft interrupts, pending TX/RX, legacy links, provider leases, network service pointers, named semaphores and `MidiDeviceData` references are drained; define failure/rollback and identity preservation; compare AROS Exec/DOS `UnLoadSeg` conventions and 68k/x86/x86-64 ABI. No public API or code change before proving safety and backward compatibility.

**Priority:** architectural backlog / follow-up investigation, **not a confirmed defect in #1586**. The PR's current deliberate retained allocation is consistent with the documented no-unload model; revisit that policy independently.
