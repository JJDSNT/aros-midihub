Title: camd: add version 42 timing and cluster observation APIs

AROS PR: [#1972](https://github.com/aros-development-team/AROS/pull/1972),
branch `review/camd-v42`, commit `7b14f148af`, based directly on upstream
master `62bd8216b1`. The prerequisite `CamdWait()` fix is already merged as
AROS PR #1956; this proposal has no dependency on an unaccepted pull request.

## Scope

CAMD 41.1 has no library clock. `MIDI_TimeStamp` is a pointer to a value the
client must update, and the default remains zero. Its global cluster
notification reports only cluster creation and removal. Participant signals,
hooks and counts now work, but a program that does not link to every cluster
cannot observe participant changes. `MLINK_Comment` is a public tag and
`MidiLink` already has `ml_ClusterComment`, but the setter ignores it and the
getter does not return it. Cluster receive failures are visible only as
per-node error bits.

Introduce CAMD 42.0 as one additive public API level:

- append `GetClusterAttrsA()`, `CamdTime()`, `StartClusterWatchA()`,
  `EndClusterWatch()` and `GetClusterWatchEvent()` after every 41.1 vector;
- add the opt-in `MIDI_SystemClock` node tag, leaving legacy timestamps
  unchanged unless requested;
- expose cluster name, comment, participant counts, driver presence and
  atomic diagnostic drop/error counters through `MCLA_` tags;
- implement the existing `MLINK_Comment` contract, copying the
  highest-priority link comment into private cluster storage;
- add bounded cluster-watch handles for added, removed and participant
  events. Overflow discards incomplete history, reports `CWE_Lost`, and
  resumes event delivery only after that marker is read.

All new state is private or opaque. No public 41.1 structure grows, no existing
tag or vector moves, and `StartClusterNotify()` keeps its existing event
frequency. `struct ClusterWatchEvent` is pointer-free and fixed at 68 bytes,
with its name at offset 4 on both tested architectures. The modified public
header's legacy replacement character is also normalized to the standard
ASCII `Copyright (C) 1995-2026` notice.

The version bump is required by the five new callable vectors: clients can
now request `OpenLibrary("camd.library", 42)`. It is not a mechanical bump for
an internal fix. The timing, attributes and watches are submitted together
because they define one CAMD 42 public boundary; splitting them would either
change the contents of the same API level between dependent PRs or require
artificial intermediate version levels. The change remains confined to the
CAMD component and its public header. No provisional version 43 endpoint code
is included.

## Reproduction and validation

The external compatibility fixture demonstrates the missing API against the
unmodified 41.1 library: `MIDIHubCAMDCompat --v42` reports that the library is
older and skips the complete v42 section.

With the candidate library on hosted x86-64 AROS:

- the CAMD 41.1 contract passes 60 checks, with no failures and the optional
  loopback test skipped;
- the CAMD 42 section passes 38 checks with no failures, covering participant
  notifications/counts, cluster attributes and counters, link comments,
  monotonic clock/timestamps, watch events, overflow recovery and watch calls
  while `LockCAMD()` is held;
- the merged legacy wait regression passes 5 checks with no failures.

Complete generated `camd.library` builds and links pass for:

- hosted x86-64: 64-bit little-endian;
- amiga-m68k: 32-bit big-endian.

A standalone consumer of all five new vectors compiles for both targets. ABI
assertions verify 32-bit `ULONG`, the 68-byte watch event, its offset 4 name,
and the new tag bases on both targets. Generated headers place the new vectors
at indices 41 through 45 and gate them on library API version 42.

The four compiler warnings in each complete build are pre-existing signedness
warnings in CAMD; none comes from a new expression. The companion repository's
host `make test` suite and `git diff --check` pass.

No 32-bit little-endian or 64-bit big-endian build was available, and no
physical MIDI hardware was used.
