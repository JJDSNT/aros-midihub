# AppleMIDI #1678: maintainer-review corrections

Review: [Kalamatee, 2026-10-18](https://github.com/aros-development-team/AROS/pull/1678#issuecomment-6250940105).
The comment has two pre-merge fixes, two maintenance nits and a conditional
CK0 interoperability suggestion. It is an issue comment, not a formal GitHub
review/approval. #1679 had no discussion comments when checked.

The owner's correction redirected the active work from mDNS discovery to this
existing review. #1695 remains a draft; no additional discovery work was done.

## Changes

- `applemidi_clock_expired()` requires `now >= heard` before subtracting.
  Clock polling also ignores samples older than `sent`.
- Peer expiry checks that the unsigned elapsed difference is at most
  `INT32_MAX` before comparing the timeout. This rejects backdated samples
  and preserves forward progress across the 32-bit wrap. The header states
  one monotonic 10 kHz clock and the half-range interval requirement.
- `now_ms`, `last_seen_ms` and `timeout_ms` become `now`, `last_seen` and
  `timeout`. Member sizes/layout and function types are unchanged. No caller
  outside the library accessed the renamed member in the checked sources.
- The redundant peer LEFT/NONE/LEFT sequence becomes one LEFT assignment.
- Both `ports.c` sites explain the byte at `text[limit]`; the external name
  contract is repeated in `ports.h`.

The optional proposal to answer CK0 in either role remains an interoperability
follow-up. No such failing peer was reproduced; this update retains the
established role behavior and existing tests for it.

Canonical local sources and the narrow PR were both updated. The isolated
review branches preserve the published parent commits and add/merge the fix;
the dependent drafts carry the new library so they cannot restore its old
timeout implementation when merged later.

| PR | Before | Updated head |
|---|---|---|
| #1678 | `52a748233870f6622aa0f48ba3d1450bc7df2fdc` | `31d1ffdde5d4af3aa4139ac071d407afb6961645` |
| #1679 | `70d61fb1d2a6c6f406101637cbe6144137d2ff94` | `db9e3073c3a0645a50dabf388092cece9902e344` |
| #1693 | `7ddf7e46a82523a4700369bf2b6bd03f26db14d9` | `32351d7bab73140bf3dc018e56741438e6123ed1` |

## Validation

The new protocol regression fails against the original #1678 sources at
`now=994`, `heard=1000`. The complete protocol suite passes with ASan/UBSan
against the exact corrected PR source snapshot and against the local sources.
The added cases cover six/one tick backward differences, equality, the exact
timeout, a renewed last-seen value, 32-bit wrap and old samples from the
previous counter cycle. They also exercise polling with a backdated sample.

All nine RTP-MIDI library source files compile for x86-64 and m68k. Both
dependent service snapshots compile/link for both architectures. These checks
use `-Wall -Wextra -Werror`; m68k retains the SDK's volatile-register warning
exclusion. They are direct compiler builds, not fresh generated-make/full
upstream rebuilds. Earlier generated-make and native interop evidence remains
historical; no native, hardware, SMP or interop rerun is claimed for this change.

Artifacts, before/update PR descriptions, exact exported source snapshots,
logs and `validate.py`: `build/review-applemidi-2026-10-18/`.
Regression code: `applemidi_timeout_ordering()` in `tests/protocol.c`.
PR descriptions link the review and state what changed, what was tested and
what remains conditional. No separate public comment is posted.

Published heads confirmed on all three PRs. #1678 remains ready for review;
#1679 and #1693 remain drafts. All automated checks passed on all three PRs,
including Codacy. Maintainer re-review is still pending.
