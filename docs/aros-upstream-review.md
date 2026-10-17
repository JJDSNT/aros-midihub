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
