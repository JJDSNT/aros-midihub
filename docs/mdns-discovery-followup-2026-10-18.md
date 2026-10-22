# DNS-SD browse/resolve: draft preparation

Published as [draft PR #1695](https://github.com/aros-development-team/AROS/pull/1695),
with explicit dependency on #1591 and the #1559 event-API discussion.
Published head confirmed at `1761f492111641819201f52ac8d799ece71e6c33`.
Codacy, filename, line-ending and mergefreeze checks all passed. The PR remains
a draft; automated checks do not resolve the architectural review questions.

Date: 2026-10-18. The owner authorized continuing after #1591's review fixes.
The full implementation in `network/mdns/aros/src/` was reviewed and preserved.
Its daemon-side hooks and cache-only resolve need a different event contract
for upstream. Question 3 of #1559 still has no maintainer decision; this draft
proposes explicitly polled events without claiming architectural acceptance.

Dependency: #1591, `55e7f21ec2ac29f774de27eada5524b8d01c1c54`.
Implementation: `1761f492111641819201f52ac8d799ece71e6c33`, isolated AROS branch
`review/mdns-discovery`, fork target `mdns-discovery`.
The [PR description](pr-drafts/mdns-discovery.md) and AROS `DISCOVERY.md` record
all semantics, bounds and review questions.

## Prepared change

The library keeps task-owned opaque local references, each associated with a
remote handle and daemon Exec task ID. Start explicitly opts into poll mode
and takes a NULL hook. New `ReadMDNSEvent()` copies snapshots in the client's
task; synchronous stop removes operation/queue and local reference. No Hook,
client event buffer or task pointer is retained in the daemon. IPC is version 3,
library 41.2, original vector offsets/signatures unchanged.

The bounded per-link cache automatically resolves browsed services and offers
one-shot asynchronous resolve without a prior PTR. It retains counted labels
and TXT, separate record lifetimes/refresh, goodbye/cache-flush grace and retry
backoff. Overflow is explicit. The API deliberately leaves polling versus
notification/dispatcher delivery open for maintainer review.

## Validation

23 new ASan/UBSan packet/IPC fixtures and 25 parent responder fixtures pass at
the implementation head. They mock Exec lists/allocation, clock and network
output, not scheduling/native IPC. Inputs are independently encoded. Coverage
includes 63-byte dotted/backslash labels, binary TXT, four record expiries,
malformed compression/RDATA, multiple links, clock wrap, overflow, task/daemon
identity, cancellation, asynchronous resolve, refresh and retry backoff.

Tests caught and corrected same-tick address ordering, per-second missing-data
query floods and cross-interface known-answer leakage. Build/analysis also
caught const-pointer refinements before publication. Library, link library and
daemon build for x86-64 and m68k with the generated SDK harness and its
previously documented generated-code warning exclusions. New daemon source
passes local cppcheck checks.

Native hosted tests use actual library vectors with an independently encoded
Linux DNS-SD peer. They verify browse, resolve, 176 wire TXT bytes containing a
98-byte backslash endpoint and 42-byte product ID, stop, timeout, unchanged
output on failed read, unsupported-hook refusal, and an actual mdnsd
exit/restart while the client/library stay alive. Old references fail and a
new subscription succeeds. The first CLI Break command did not stop the
managed daemon; the corrected UP harness signals the public port's owner and
confirms port removal before starting its replacement. This is a test-harness
correction, not a claimed production fix.

Artifacts and SDK scripts: `build/review-discovery-2026-10-18/`; final native
capture/logs: `native-final/`. Runtime used an existing UP image with new
binaries, not a full current-upstream image rebuild. No native SMP/hardware,
Apple Bonjour or Windows results are claimed.

Reproduce host checks with:

```sh
python3 tools/review-mdns-responder.py --aros-tree build/upstream-pr-prep \
  --revision review/mdns-discovery --discovery
python3 tools/review-mdns-responder.py --aros-tree build/upstream-pr-prep \
  --revision review/mdns-discovery
```

Native probe: `tests/review/bonami_discovery_probe.c`; runner:
`tools/review-mdns-discovery-hosted.sh` (prepared image, sudo/veth/tcpdump/Xvfb).
These harnesses remain local companion files, outside the AROS PR.

## Audit relationship and remaining Network MIDI work

Poll-mode delivery avoids MD01's daemon callbacks. Counted names cover the
receive-side BA-007/009 cases. Counted raw TXT avoids BA-008's expansion for
supported MIDI metadata; a 512-byte record limit remains. The parent's
registration dot restriction and broader IPC/daemon-shutdown races remain.

The new daemon-incarnation contract supports recovery from DI-001, but N9
has **not** adopted it. DI-002/003 remain consumer work too: case-insensitive
TXT keys, 98-byte endpoints through configuration/selection, and view reset
and resubscription after restart/overflow. The full local Hook implementation
remains separate and has not acquired these guarantees. Address-based MIDI
transport is independent.

M3 review still needs API acceptance, local-service reflection, multiple-address/
SRV policy and broader IPC/SMP work. IPv6, NSEC and other declared limits remain.
This is a draft for review, not full RFC conformance or merge readiness.
