# Prepared AppleMIDI changes, 2026-10-17

**Subsequently published:** see the [publication record](upstream-publication-2026-10-17.md)
for the #1587 update, four new PRs and the complete open-PR inventory.
The local-only statements below describe the preparation snapshot.

These are local commits and draft descriptions, following the
[correction and native validation report](applemidi-review-2026-10-17.md).
No push, PR creation, PR-body edit or remote comment was performed. The
original AROS working trees and their branches were left in place.

## Current upstream state

GitHub still reports the same five open PR heads recorded in the audit:
#1507, #1586, #1587, #1588 and #1591. #1587 has no review/comments, and its
existing checks are green for its published head; those checks have not
run on the local correction.

[#1585](https://github.com/aros-development-team/AROS/pull/1585), CAMD driver
alignment, was merged on 2026-10-17 at `b1edfb98664776c8d96f371bd030f16b6d7b6558`.
It is already present in the upstream base fetched for this preparation:

```text
ecdc52080ac8dfc593a27888695b8fba8c836eb2
rust: sockets, poll() and mmap() for std on AROS
```

The CAMD expunge callback and netinfo worker-expunge problems still exist
in that base. Current upstream also contains newer Accounts/netinfo
changes (`206672c498`); the older native image is not a full build of
those changes. The netinfo description keeps that validation boundary
and the unresolved SMP review explicit.

## Local branches

Repository: `/home/jaime/aros-midihub/build/upstream-pr-prep`.
This isolated shared clone contains the following review branches:

| Branch | Head | Change / review base |
|---|---|---|
| `review/rtpmidi-sysex` | `caa66d00b30dc0f6abb1d158bac2901fcab8bfd5` | One correction commit above published #1587 (`2ff4abfe52`) |
| `review/camd-expunge-result` | `d64eaf76a534c9126d3a8769e326fba0f347c8e1` | One independent commit above fetched upstream master |
| `review/netinfo-expunge-task` | `82ff30a5220f00617867918ac7feb48f6080dcbd` | One independent candidate commit above fetched upstream master; SMP review outstanding |
| `review/applemidi-sessions` | `54256f805fe3bfe937bca779c292ea59cb9a5469` | N3 only, above `review/rtpmidi-sysex`; 11 files, 1,278 additions / five removals |
| `review/base-netmidid` | `ac2129efb3465c5dbadbccf1a7d4d417b02f6d92` | Local integration base: current upstream + #1586 + #1587 + its correction + N3 |
| `review/netmidid-service` | `dd61159d16018b382a0ad2876bc6d7c7c09fd8c3` | N4 only, above `review/base-netmidid`; 11 new files / 2,262 lines |
| `review/check-applemidi` | `b219e8c9681bd2afb72897c6f5bbef78263bce8e` | N4 plus the two independent cleanup fixes, for integration review |

N3 has its own narrow parent; the N4 integration base additionally brings
in #1586 because the service needs its adapter header. The cleanup fixes
are not hidden in either transport PR. `local-source` refers to the
original local checkout; `upstream` and `fork` name the two GitHub remotes.
None of these remotes was pushed.

## Draft descriptions

- [#1587 correction](pr-drafts/1587-sysex-update.md).
- [Independent CAMD correction](pr-drafts/camd-expunge-result.md).
- [Independent netinfo candidate](pr-drafts/netinfo-expunge-task.md).
- [N3 sessions](pr-drafts/applemidi-sessions.md).
- [N4 service](pr-drafts/netmidid-service.md).

After corrected #1587 merges, rebase N3 onto actual upstream master. After
#1586 and N3 merge, rebase N4 likewise. Opening dependent drafts earlier
must make their bases explicit; the local integration base is not itself
a proposed upstream PR. #1588 corrections and mDNS registration/browsing
remain separate work and do not gate address-based AppleMIDI transport.

## Validation of the prepared cuts

- The prepared N3 library passed `tests/protocol.c` with ASan/UBSan.
  Configuration/routes and the tests remain MIDIHub host sources.
- N3, N4 and the service-entry mmakefiles expanded successfully with the
  fetched upstream `config/make.tmpl` and the existing genmf tool.
- N3/N4 and the driver cross-compiled with the existing hosted x86-64 SDK,
  `-O2 -Wall -Wextra -Werror`.
- Using the same source link order, both binaries have exactly the SHA-256
  values of the previous native tests:

```text
netmidid 85a655a13d8c8541977ae9e2b778473062310d43e39f48b065f3861f16847c57
applemidi ed325507c3509cd4f0a7621d096cf0db6e163a0e1ddd7863a1e2e4d443139136
```

- The N4 source cut contains no Bonami, discovery include or netmidi2
  reference, and its installed service entry has `Enabled=no`.
- The integration diff passes `git diff --check`; the isolated worktree
  is clean.

Logs and generated makefiles are under `build/upstream-prep-validation/`.
This verifies the cuts and reuses the earlier native evidence through
binary identity. It does not claim a new full current-master AROS build,
native SMP execution or complete RFC 6295 conformance.

## Transfer without publishing

A verified 83 KiB bundle preserves the seven local branch tips:

```text
build/upstream-prep-validation/applemidi-review.bundle
SHA-256 3829d0d01884ab6544c69a05000d8c91ecb63c34405aaa5b77bbdabe56e7895e
```

It requires upstream history through the fetched master and the original
PR base `084dfe66344b67591d34b7adb318d0fc203ddce7`. To import its review
refs into another AROS checkout after fetching upstream master:

```sh
rtk git -C /path/to/AROS fetch /home/jaime/aros-midihub/build/upstream-prep-validation/applemidi-review.bundle \
  'refs/heads/review/*:refs/heads/review/*'
```

This imports local refs; it does not publish them. The three standalone
patches remain under `patches/`, and the N3/N4 cuts can be regenerated
from the repository's source scripts.
