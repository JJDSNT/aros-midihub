# Upstream layout review addressed, 2026-10-18

Subsequent merges and the [session-readiness update](upstream-sessions-ready-2026-10-18.md)
supersede the PR statuses and dependency statements in this layout snapshot.

Kalamatee requested a CAMD subsystem layout in
[#1586](https://github.com/aros-development-team/AROS/pull/1586#issuecomment-6250422067)
and consumer-owned protocol libraries in
[#1587](https://github.com/aros-development-team/AROS/pull/1587#issuecomment-6250425507)
and [#1588](https://github.com/aros-development-team/AROS/pull/1588#issuecomment-6250428207).
The user authorized implementing and publishing these changes.

## Published changes

| PR | Published head | Layout |
|---|---|---|
| [#1586](https://github.com/aros-development-team/AROS/pull/1586) | `2a902bcbb19de0bc99432e9e4dd9584f6433d064` | CAMD subsystem, separate driver directories |
| [#1587](https://github.com/aros-development-team/AROS/pull/1587) | `0905adf91db85a2bd528caf8d166da894d1db2a3` | `workbench/network/netmidid/rtpmidi` |
| [#1588](https://github.com/aros-development-team/AROS/pull/1588) | `e89557bcc24b660ffe0895296322f1d7d7e928ad` | `workbench/network/netmidid/netmidi2` |
| [#1678](https://github.com/aros-development-team/AROS/pull/1678) | `bb3d3f5496e3e4d9433740b26d4e94c22c6bbd24` | Sessions directly above relocated #1587; remains draft |
| [#1679](https://github.com/aros-development-team/AROS/pull/1679) | `d5338c3889b35df4cad7fb118c237f6bdeaf65fd` | Service at `workbench/network/netmidid`; updated prerequisites; remains draft |

#1586 was rebuilt on upstream `e179dbe1f6`, which includes the merged
BLE MIDI #1507 and CAMD expunge #1676. Its first commit, `5306c55409`,
moves debugdriver to `workbench/devs/camd/debugdriver` and Unix hostmidi
to `arch/all-unix/devs/camd/hostmidi`, with their build metadata. Its
second commit adds the network drivers, with a directory and build
target for each. Their unchanged shared C source and adapter header
live in `workbench/devs/camd/common`. The parent mmakefile aggregates
the subsystem; it compiles no driver itself. Installation remains
under `DEVS:Midi`.

Both libraries drop the two network metatarget additions. The service
mmakefile owns the `workbench-network` connection and the RTP-MIDI
dependency, and uses the relocated adapter include directory.
The isolated session and service commits are `bb3d3f5496` and
`d5338c3889`, respectively. Their PRs still target master as dependent
drafts and should be rebased after prerequisites merge.

All five fork refs were pushed atomically with explicit expected-head
leases. #1587 and #1588 advanced without rewriting history; #1586,
#1678 and #1679 were rebuilt. The old tips are preserved in
`build/upstream-prep-validation/pre-layout-2026-10-18.bundle`.
PR descriptions retain original provenance, explain the requested layout
and dependencies, and distinguish earlier native results from this validation.

## Validation

- All 42 compared C/header files match their prior cuts byte for byte,
  including the existing debugdriver and hostmidi sources.
- The RTP-MIDI protocol suite compiled and passed with ASan/UBSan.
  LeakSanitizer required running this validation outside the sandbox.
- Direct SDK cross-compilation passed for the service and all four
  drivers. The new drivers/service used `-O2 -Wall -Wextra -Werror`;
  the unchanged legacy drivers used `-O2 -Wall` because their existing
  unused parameters trigger `-Wextra`.
- Generated makefiles compiled and linked both protocol libraries, all
  four drivers, and netmidid using the existing hosted x86-64 SDK in an
  isolated output directory. The harness uses the current upstream
  template with existing build configuration and an isolated SDK view,
  so netmidid links the new archive instead of the SDK's older archive.
  It is not a full current-master AROS build.
- The updated generators reproduce 42 proposed files exactly. Repeated
  N7/N8/N9/N4 service generation preserves both libraries and the status
  tool; returning to N4 removes stale discovery sources. Shell syntax
  and diff whitespace checks pass.
- At final GitHub inspection, all checks were successful for the five
  published heads: source line endings, Windows filenames, Codacy and
  mergefreeze. All five were reported mergeable without conflicts.

The driver binary for applemidi still has the previously native-tested
SHA-256 `ed325507c3509cd4f0a7621d096cf0db6e163a0e1ddd7863a1e2e4d443139136`.
This update adds no new m68k build, radio, SMP or external journal
interoperability evidence, and does not claim full RFC conformance.

## Merge coordination and local tooling

Coordinate #1587 and #1678 with their service consumer #1679, as requested.
#1588 is relocated and its independent build works, but #1679 remains
AppleMIDI-only. Network MIDI 2.0 still needs its own service extension
before coordinating that library's merge. The local N7/N8 templates have
the corresponding library dependency and new paths; no new service
feature or PR was introduced by this layout update. Previously documented
protocol findings remain separate from the layout review.

The driver/library/service/status cut scripts and N1–N8 templates now
use the requested paths. The driver cut expects the initial CAMD
subsystem move to be present. Service regeneration no longer removes
the whole service directory, since that would also remove its libraries.
The pinned Network MIDI 2.0 audit runner accepts both old and new paths.

Validation scripts, logs, SDK outputs and before/after PR descriptions
are under `build/review-layout-2026-10-18`. Local preparation branches
are `review/layout-{drivers,rtpmidi,netmidi2,sessions,service}` in
`build/upstream-pr-prep`.
A verified bundle of their published tips is:

```text
build/review-layout-2026-10-18/published.bundle
SHA-256 74889f9381f58afead996598c67972f9579f88fd34366f6937fb64316404bab0
```

It requires upstream commits `084dfe66344b67591d34b7adb318d0fc203ddce7`
and `e179dbe1f6ada1fce981bb68aa8ae11d4c66b7a9`.
