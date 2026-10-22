# Published AppleMIDI follow-up, 2026-10-17

The [2026-10-18 layout follow-up](upstream-layout-review-2026-10-18.md)
records subsequent upstream review, updated PR heads and validation.
The inventory and hashes below describe this publication's earlier snapshot.

Publication authorized after the [local preparation](upstream-preparation-2026-10-17.md).
The existing #1587 branch was fast-forwarded, without rewriting history.
Four additional PRs were opened against `aros-development-team/AROS`.
No PR was merged and no unrelated existing PR was changed.

| PR | Published head | Status / scope |
|---|---|---|
| [#1587](https://github.com/aros-development-team/AROS/pull/1587) | `caa66d00b30dc0f6abb1d158bac2901fcab8bfd5` | Existing RTP-MIDI PR updated with per-packet SysEx tracking; original description preserved with correction/test appendix |
| [#1676](https://github.com/aros-development-team/AROS/pull/1676) | `d64eaf76a534c9126d3a8769e326fba0f347c8e1` | Ready for review: CAMD expunge callback success result |
| [#1677](https://github.com/aros-development-team/AROS/pull/1677) | `82ff30a5220f00617867918ac7feb48f6080dcbd` | Draft: netinfo worker lifetime; native UP evidence and outstanding SMP review explicit |
| [#1678](https://github.com/aros-development-team/AROS/pull/1678) | `54256f805fe3bfe937bca779c292ea59cb9a5469` | Draft: N3 AppleMIDI sessions; depends on corrected #1587 |
| [#1679](https://github.com/aros-development-team/AROS/pull/1679) | `6fa4fa51d6d7159af1dd885c8841bfbf0d4d4aab` | Draft: N4 service; depends on #1586, #1587 and #1678 |

N3/N4 target upstream master as dependent drafts, and currently include
their prerequisite commits. Their descriptions link the isolated feature
commits and state the need to rebase after the prerequisites merge.
The N4 description links the separate CAMD/netinfo fixes used for its
unload/restart validation. No discovery or Bonami dependency was added.

The five fork refs were pushed atomically. The original local AROS
checkout branches were not moved. The GitHub CLI's PR-description editor
failed on its deprecated Projects Classic query; updating #1587 through
the REST API succeeded with the original body plus appendix preserved.

Published body snapshots are under `build/upstream-prep-validation/`,
named `*-published-body.md`. Local validation and its limits remain in
the [native review report](applemidi-review-2026-10-17.md).

## Open PR inventory

The paginated REST pull-request API was queried directly after creation,
filtering the author `JJDSNT`, rather than relying on the search index.
It returned nine open PRs:

| PR | Subject | Draft |
|---|---|---|
| [#1507](https://github.com/aros-development-team/AROS/pull/1507) | BLE MIDI class | No |
| [#1586](https://github.com/aros-development-team/AROS/pull/1586) | Network CAMD drivers | No |
| [#1587](https://github.com/aros-development-team/AROS/pull/1587) | RTP-MIDI library, with SysEx correction | No |
| [#1588](https://github.com/aros-development-team/AROS/pull/1588) | UMP / Network MIDI 2.0 library | No |
| [#1591](https://github.com/aros-development-team/AROS/pull/1591) | mDNS / Bonami service registration | No |
| [#1676](https://github.com/aros-development-team/AROS/pull/1676) | CAMD expunge correction | No |
| [#1677](https://github.com/aros-development-team/AROS/pull/1677) | netinfo worker expunge correction | Yes |
| [#1678](https://github.com/aros-development-team/AROS/pull/1678) | AppleMIDI sessions | Yes |
| [#1679](https://github.com/aros-development-team/AROS/pull/1679) | AppleMIDI netmidid service | Yes |

Checks at the first post-publication inspection: #1587, #1676 and #1677
all green; line endings, filenames and mergefreeze green for #1678/#1679,
with Codacy still running. These are check results, not completion of the
protocol, SMP or hardware review.

Codacy subsequently completed successfully for #1678 and flagged one
variable-scope warning in #1679 (`sysex_result`). Commit `6fa4fa51d6`
limits that variable to the SysEx command block. The change was made in
MIDIHub's source and regenerated into the service PR, then fast-forwarded
to its fork branch. The AROS cross-build still produces the exact
native-tested service binary SHA-256 recorded in the preparation report;
the follow-up changes no runtime instructions.
The PR description's aggregate service delta is now 2,261 lines.

Final inspection: all checks green for updated #1587 and new #1676–1679,
including Codacy at #1679 head `6fa4fa51d6`. Draft status is unchanged.

An updated, verified bundle includes the published follow-up and its
local integration copy:

```text
build/upstream-prep-validation/applemidi-published.bundle
SHA-256 e508f1715f424e2d7c04db95b50308d2f549cc2786e70ab892988f66945ebdfd
```
