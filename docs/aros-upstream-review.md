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

## Service-side adapter audit — third pass (2026-10-17)

Inspected `ports/aros/midihub/adapters.c` and `ports/aros/netmidi/netmididriver.c` on Midihub main, comparing the driver with the upstream #1586 source.

**Observed safeguards:**
- `attach()` uses `FindSemaphore` under `Forbid()`, checks adapter layout version and port count, detaches a disappeared driver and resets stream/SysEx state before attaching a replacement.
- `wake_driver()` checks `aa_Version` and `ad_IsOpen` with task switching forbidden before `Cause()`, avoiding a call into an unloaded driver's interrupt code when the expunge follows the same synchronization contract.
- `netmidi_camd_deliver_port()` serializes delivery against `ClosePort()` with `ad_Lock`; `netmidi_camd_close()` clears service task/signal references before freeing its signal bit.
- `netmidi_camd_poll()` re-runs `attach_all()` before reading ring buffers. The driver deliberately retains adapter memory on expunge so stale pointers remain allocated until the service notices removal.

**Open verification questions, not confirmed defects:**
1. **Service replacement and stale notifications:** prove no `Signal()` targets a dead task while `netmidi_camd_close()` detaches, including a simultaneous CAMD soft interrupt.
2. **Expunge under active service:** prove no receive callback, ring consumption or soft interrupt runs against unloaded code; validate the assumptions about `Forbid()`, `Disable()` and `Cause()` on relevant AROS targets.
3. **Generation/ABA:** if a driver is replaced, prove that the service cannot confuse a stale adapter with a new generation; version is a *layout* version, not a lifetime generation.
4. **Retained allocation policy:** quantify memory retained per expunge/reload and decide whether a future controlled-unload protocol needs leases/refcounts, quiesce/drain and explicit final reclamation.
5. **Status readers:** independently review the public named `NetMidiStatus` semaphore's removal/free protocol under concurrent readers.

**Conclusion:** service code already implements the expected gone-version and reattachment checks; do not report a proven UAF or memory leak from source inspection alone. The remaining work is concurrency reproduction, lifecycle tests and a separate design decision on administrative driver reload. No tests were executed and no upstream PRs were modified.

## Maintainer-style review finding — #1586 (2026-10-17)

**NM-001 — Unbounded work in software interrupt when service is absent**  
**Location:** `workbench/devs/midi/netmidi/netmididriver.c`, `GetFromCAMD()`, branch `if (!task)` (approximately lines 94–108).  
**Severity:** medium (provisional); **category:** concrete code-level risk, not reproduced.

The driver repeatedly invokes `ad_TXFunc` and discards bytes until the callback returns a value above `0xff`. Unlike the attached-service path, this no-service path has no work budget. If a CAMD producer keeps making bytes available, the software interrupt can execute for an unbounded interval, delaying other work. The intention to drain CAMD output when no service exists is sound; the absence of a processing bound is the issue.

**Suggested change to investigate:** bound the number of bytes drained per invocation and define a safe rescheduling strategy that does not spin or lose the wakeup needed to continue draining. Do not simply stop after N bytes without proving CAMD will re-activate transmission.

**Required test:** producer supplies sustained MIDI bytes while no service is attached; verify bounded interrupt duration, eventual draining, no starvation, and recovery when service attaches. Also test finite SysEx and abrupt service shutdown.

**Status:** source-level finding only. No runtime reproduction, build, or upstream modification performed.

## Maintainer review — #1591 bonami/mdnsd (2026-10-17)

**BA-001 — Task liveness test may mistake a running owner for a dead task**  
**Location:** `workbench/network/mdns/mdns_client.c`, `task_alive()` / `mdnsClientReap()`.  
**Severity:** high (provisional); **status:** source-level risk, scheduler semantics not yet independently validated.

`task_alive()` scans `SysBase->TaskReady` and `SysBase->TaskWait` only. A task executing on a CPU need not appear on either list. If the periodic reaper runs while a registered owner is running, `mdnsRegRelease()` can remove its live services. Check AROS scheduler task states and SMP behavior, and choose an appropriate native lifetime mechanism rather than extending unsafe raw pointer scans. Reproduce with a registered service whose owner remains CPU-active while mdnsd reaps; verify that announcements persist until explicit close or actual owner termination.

**BA-002 — Synchronous IPC can wait indefinitely if daemon stops without replying**  
**Location:** `workbench/network/mdns/library/bonami_ipc.c`, `bonamiSend()`.  
**Severity:** medium (provisional); **status:** failure-mode risk.

`FindPort()` and `PutMsg()` are protected together by `Forbid()`, which addresses port removal between lookup and enqueue, but `WaitPort(replyPort)` has no deadline. The code assumes every enqueued message is eventually answered. Review daemon shutdown/drain and unexpected termination; a crash or failure to reply may strand callers. A timeout alone is not safe if a late reply still targets a freed reply port. Any cancellation protocol must account for message ownership and late replies. Test daemon shutdown and fault injection with pending calls.

**BA-003 — Task identity reuse is explicitly acknowledged**  
**Location:** `mdns_client.c`, `mdnsClientReap()` comment.  
**Severity:** medium (provisional); **status:** design limitation.

Owner identity is a raw `struct Task *`. The comment notes that a new task at a reused address is indistinguishable from the original owner. Explore generation/token or explicit registration ownership that cannot silently transfer to a new task. Test termination/recreation cycles.

No builds or runtime tests executed. These are actionable review candidates, not proven failures.

## Maintainer review — #1587 RTP-MIDI and #1588 Network MIDI 2.0 (2026-10-17)

**NM2-001 — Zero-sized output buffer writes a terminator out of bounds**  
**PR:** #1588. **Location:** `workbench/network/netmidi/netmidi2/netmidi2.c`, static `copy_text()`, invoked by public `netmidi2_identity()`.  
**Severity:** medium; **category:** source-confirmed bounds defect for the zero-capacity input, runtime test pending.

`copy_text(to, size, from, length)` terminates with `to[i] = 0` even when `size == 0`. The loop executes zero times and `i == 0`, so `to[0]` is written despite no capacity. `netmidi2_identity()` accepts caller-supplied `name_size` and `id_size` without rejecting zero; either may reach the helper. Guard `size == 0` before writing; document whether null output pointers with zero capacity are allowed and validate accordingly. Test both output sizes independently at 0, 1 and normal capacity, including malformed/empty identity packets and sanitizer builds.

**#1587 sampled checks:** `rtpmidi_sender_record()` validates supported MIDI messages before copying into history; `rtpmidi_sender_packet()` rejects zero and oversized messages before copying to a fixed local event. These checks are positive observations, **not approval of the full 3,000+ line PR**. Continue reviewing RTP framing, recovery journal, SysEx and integer wraparound.

No tests run and no upstream PR code changed.

## Remaining review work — actionable checklist (2026-10-17)

This checklist tracks **review work still to do**, not implemented fixes. Mark an item complete only after attaching evidence (commit, test output, reviewed diff or upstream source reference). The audit is **not complete**.

### P0 — resolve before recommending upstream merge

- [ ] **#1588 / NM2-001:** fix the zero-capacity output write in `netmidi2_identity()` / `copy_text()`; define NULL/zero-capacity contract; add 0/1/normal-size regression tests and run sanitizer tests on host.
- [ ] **#1591 / BA-001:** inspect the actual AROS task scheduler on supported targets and reproduce/discount false owner death when the owner is executing. Replace raw task-list liveness detection with an AROS-supported lifecycle strategy; test live owner, sleeping owner, terminated owner and task address reuse.
- [ ] **#1591 / BA-002:** trace `mdnsd` shutdown, message-port removal, queue draining and all reply paths. Prove that `bonamiSend()` cannot wait forever on normal shutdown; design cancellation/timeout with late-reply ownership safety if necessary.
- [ ] **#1591 / BA-003:** assess raw `struct Task *` reuse as owner identity; document severity and test task termination/recreation; use generation-safe ownership if required.

### P1 — targeted risk validation and full review

- [ ] **#1586 / NM-001:** measure work in `GetFromCAMD()` without attached service under sustained MIDI production. If unbounded, introduce a safe work budget and reactivation strategy; verify no starvation or stranded MIDI.
- [ ] **#1586:** exercise service stop/restart, concurrent `Cause()`/`Signal()`, `ClosePort()` and driver expunge. Confirm no stale task signals or callback into unloaded code; verify adapter reattachment and layout checks.
- [ ] **#1507:** review complete BLE MIDI diff and all previous review concerns against current code: binding-start failure, use-after-free, pairing consent, GATT busy/failure retry, notification queue saturation, lock ordering, callback reentrancy, unplug/shutdown.
- [ ] **#1587:** finish complete sender/receiver/packet/timing audit, including RTP framing, recovery journal, SysEx boundaries, malformed and reordered packets, timestamp and sequence wraparound, integer overflow, interoperability and fuzz tests.
- [ ] **#1588:** finish full UMP/Network MIDI 2.0 audit: command parsing, count/size validation, FEC/history bounds, malformed packet handling, endian/alignment, MIDI 1.0↔UMP conversion and native MIDI 1.0 coexistence; check against available authoritative MIDI specifications.
- [ ] **#1591:** review remaining daemon registration, DNS encoding, packet construction, name conflict resolution, mDNS TTL/goodbye, IPC ABI and build integration beyond the three initial findings.

### P2 — architecture and release readiness

- [ ] **CAMD driver unload/reload:** revisit the *previously deferred* administrative unload/hot-replacement feature separately from device hot-plug. Inventory CAMD loader/`RethinkCAMD()`, references, leases, callback drain, rollback, identity preservation and eventual memory reclamation. Do **not** misclassify deliberate retained allocations in #1586 as a proven leak.
- [ ] **AROS native-first pass for each PR:** compare to master APIs/macros, naming/build conventions, error paths, 68k/x86/x86-64 ABI and size impact; distinguish maintainer-style improvements from functional defects.
- [ ] **Midihub-only code:** review endpoint registry/provider, legacy CAMD projection, service/router/synth and integration tests that are not part of the five upstream PRs.
- [ ] **Evidence and sign-off:** run relevant target builds, host sanitizers, negative/fault-injection tests, compatibility and interoperability suites; re-review amended diffs and record one verdict per PR: **block / changes requested / ready**, with remaining risks.

### Execution rules

1. Prioritize **NM2-001** and **BA-001**, then IPC lifecycle and concurrency findings; continue full coverage of all five PRs.
2. For every finding, record PR, path/symbol, severity, reproduction or source proof, native AROS precedent, fix and regression test.
3. A checked item requires actual verification. Source inspection alone is not a test pass.
4. Review first; do not push upstream changes or post PR review comments without explicit approval.
