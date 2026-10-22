# AppleMIDI sessions ready for review, 2026-10-18

The user authorized advancing after the driver and protocol-library merges.

Subsequent [maintainer-review fixes](applemidi-maintainer-review-2026-10-18.md)
update the timeout/unit handling in #1678 and propagate it to #1679/#1693.
The source-equality/build evidence below describes the earlier readiness update.
Upstream master fetched for this update is
`eee73b61566cd27644b8c46efdb90d3a2313bb73`, including #1586, #1587,
#1588 and the earlier CAMD cleanup #1676.

| PR | Published head | Current status |
|---|---|---|
| [#1678](https://github.com/aros-development-team/AROS/pull/1678) | `52a748233870f6622aa0f48ba3d1450bc7df2fdc` | Ready for review; one session commit directly above upstream master |
| [#1679](https://github.com/aros-development-team/AROS/pull/1679) | `232f40489c9037fed3d38b5e7dbe8405c9470dca` | Draft; sessions plus one service commit above master; still depends on #1678 |

The session delta remains 11 files, 1,278 additions and five removals.
The isolated service delta remains 11 new files / 2,261 lines. The two
feature commits were replayed without conflicts; already merged driver,
payload and layout changes are no longer in their PR diffs. The merged
MIDI 2.0 library is preserved without making the AppleMIDI service use it.
Both branches were pushed atomically with explicit expected-head leases.
Descriptions were rewritten around the current scope and dependencies.

Validation after updating the base:

- The 30 files in the prior service/library subtree are byte-identical.
  Driver files and the MIDI 2.0 library match upstream exactly.
- The protocol suite passes under ASan/UBSan, using the proposed session
  library. Validation ran outside the sandbox for LeakSanitizer support.
- Direct SDK cross-builds pass for netmidid and all four CAMD drivers.
  All five binaries have the same SHA-256 values as the preceding
  [layout validation](upstream-layout-review-2026-10-18.md).
- Generated makefiles compile/link both protocol libraries, all four
  drivers and the service in isolated output directories with the
  existing hosted x86-64 SDK. Whitespace checks pass.
- GitHub reports both heads mergeable without conflicts. All checks
  passed for both heads: line endings, Windows filenames, Codacy and
  mergefreeze. #1678 was then marked ready for review.

This is not a fresh full current-master AROS build or new m68k, SMP,
hardware or external recovery-journal interoperability evidence.
Earlier native cleanup tests additionally used #1677, which remains a
separate draft with SMP lifetime review outstanding.

Next: once #1678 merges, replay only the service commit onto actual master,
validate that cut and reassess #1679 readiness. #1677 and #1591 were not
changed by this update.

Local branches are `review/ready-sessions` and `review/ready-service` in
`build/upstream-pr-prep`. Validation scripts, logs, SDK outputs and PR
description payloads are under `build/review-ready-2026-10-18`. The old
published heads are preserved in `before.bundle`. A verified transfer
bundle of the new tips requires upstream `eee73b6156`:

```text
build/review-ready-2026-10-18/published.bundle
SHA-256 318a5cda0ea98a8f9122fe1f59e7b2d577ee25d7a7f000213a3fa48859fe4659
```
