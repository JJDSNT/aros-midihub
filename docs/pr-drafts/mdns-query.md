AROS PR: [#1836](https://github.com/aros-development-team/AROS/pull/1836),
ready for review, branch `review/mdns-query` in `build/mdns-query`, opened
2026-10-24. One commit `141450804f` directly over upstream master
`8ded7e1869`; it does not depend on #1803.

The PR appends the BonAmi operations omitted from queued discovery:

- `LookupHostA()` performs a bounded IPv4 A lookup, with `MDNS_Timeout` from
  1 to 60 seconds and no change to the caller's output on failure;
- queue-native `QueryRecordA(name, type, tags)` and `StopQuery()` own a
  standing mDNS question;
- `ReadMDNSRecordEvent()` reads self-contained owner/type/class/TTL/interface
  and RDATA snapshots;
- `RequestMDNSEvent()` also wakes a query reference.

The unmerged prototype's Hook argument and LVO positions are deliberately not
preserved. It ran client code in mdnsd, and its positions are now occupied by
`ReadMDNSEvent()` and `RequestMDNSEvent()`. The new functions use LVOs 22–25.
`MDNSDiscoveryEvent` remains unchanged for 41.2 callers; record answers use a
separate fixed `MDNSRecordEvent`. PTR and SRV targets are decompressed before
delivery. The library becomes 41.3 and private IPC version 4.

Bounds and lifecycle follow queued discovery: task-owned references, daemon
incarnation checks, synchronous stop/cancellation, one lent request, explicit
terminal overflow, eight query operations (four per task) and eight pending
records per query. Repeated unread copies of one record coalesce with the
newest TTL. Bare lookup/query names are placed in `.local`. The host's own A
record is answered locally.

Validation at `141450804f`:

- 12/12 query fixtures under ASan/UBSan: A/PTR, own host, compressed RDATA,
  coalescing, overflow, timeout, cancellation, task/daemon ownership, network
  reset, wrong class/source port and invalid parameters;
- 38/38 discovery and 26/26 responder/registration regressions;
- generated x86-64 and amiga-m68k builds of `bonami.library` and `mdnsd`, plus
  a public-ABI probe linked for both targets, with no compiler warnings;
- hosted x86-64 AROS against an independently encoded Linux peer: queued PTR
  read/wakeup/cancel, IPv4 lookup and timeout; packet capture retained under
  `build/review-2026-10-24/mdns-query-hosted-2`;
- root `make test` passes.

Not included: reflection of registered local services, a retained query cache
or known-answer lists, IPv6, TXT/status helpers, the commodity and Envoy. The
next upstream BonAmi proposal is local reflection, but it must remain local
until #1836 merges. AutoDocs remain deliberately later, after the public API
surface has stopped changing.
