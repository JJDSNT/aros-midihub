# DNS-SD discovery: queue plus lent-request wakeup

Date: 2026-10-19. #1591 was squash-merged into AROS `master` as `f19edd39eb`
the same day; `workbench/network/mdns` there is identical to its old head
`55e7f21ec2`, so the #1695 commits rebased without conflicts.

Worktree: `build/mdns-notify` (sparse, from `build/upstream-pr-prep`), branch
`review/mdns-discovery-v2`. The old head stays on `review/mdns-discovery`
(`1761f49211`).

```
777411e922 mdnsd, bonami.library: wake a discovery client through a lent request
85d4f40bbe mdnsd: name the discovery event mode after its queue
ed6c5c1869 .. 5c9e0731d0  the five #1695 commits, rebased
f19edd39eb #1591 on master
```

## Decision

Agreed in conversation: keep `ReadMDNSEvent()` as the one way to consume
events, add an optional wakeup modelled on device I/O, no dispatcher, no
direct `Signal()`. Polling is not a separate mode: a caller that never lends
a request just reads. `MDNS_EVENT_POLL` was renamed `MDNS_EVENT_QUEUE`
(still unpublished ABI).

`RequestMDNSEvent(ref, struct MDNSEventRequest *)`, LVO 21. mdnsd replies
exactly once: `MDNS_OK` events waiting, `MDNSERR_BUSY` overflow,
`MDNSERR_CANCELLED` stop, `MDNSERR_NOENGINE` daemon exit.

Contract points and why:

- Stop replies the request before the stop's own reply (AbortIO+WaitIO).
- mdnsd cancels lent requests **before** `RemPort()`. Cancelling after left a
  window where a stop found no port, returned `NOENGINE` and the request was
  still out.
- Release (last library close) and a vanished owner drop the request unread.
  Exec cannot validate a port; this is best effort and documented as such,
  like a device. No promise beyond the device contract.
- The library writes nothing to the request: it may still be lent.

## Validation

- Host fixtures (`tools/review-mdns-responder.py --aros-tree build/mdns-notify
  --revision HEAD --discovery`): 33/33, ten new `notify-*` cases, guarded by
  `#ifdef MDNS_IPC_NOTIFY` so older revisions still build (23/23 there).
  Parent responder suite 25/25. Mutations caught: no cancel on stop, no owner
  check, no immediate reply.
- Builds: `build/review-notify-2026-10-19/build-sdk.py` and `build-m68k.py`
  (copies of the 10-18 scripts pointed at the new worktree).
- Native: `tests/review/bonami_discovery_probe.c` extended (Wait-based wakeup,
  second request refused, CANCELLED already on the port after StopBrowse,
  NOENGINE already on the port after mdnsd exits). Image
  `build/review-notify-2026-10-19/AROS-native` = the 10-18 image plus new
  library, daemon and probe. Run with
  `sh tools/review-mdns-discovery-hosted.sh AROS-native native-N`.
  The probe first failed because a local `port` shadowed the client's port;
  it is now built with `-Wshadow`.

Uniprocessor hosted only; nothing on SMP.

## Publication

Done 2026-10-19 with the owner's go: `review/mdns-discovery-v2` force-pushed
(with lease on `1761f49211`) to fork branch `mdns-discovery`; #1695 head is
`777411e922`, mergeable, still draft. Title and body replaced via the REST
API (`gh pr edit` fails on the Projects-classic deprecation). Comment posted
on #1559: issuecomment-6278490337. Native hosted run passed four times
(`native`, `native-1..3`).

Later the same day: in-tree DISCOVERY.md/README.md/mdns.h wording changed from
"draft/proposal" to describing the code, folded into the wakeup commit (now
`34e151b6d9`, code identical to `777411e922`). #1695 marked ready for review;
all four checks pass, Codacy 0 new issues.
