Title: mdnsd: reflect local registrations into discovery

## Scope

This is an internal behavior change with no public API, ABI, IPC or message
format change. The prepared branch is `review/mdns-local-discovery`, commit
`fb8981e861`, directly on upstream master `adff8323a1`. It does not depend on
AROS #1887; the two branches only touch some of the same source files.

## Existing interfaces and reproduced gap

The accepted Bonami interfaces already expose queued local-domain discovery:

- `BrowseServiceTypeA()` and `ResolveServiceA()`, with
  `ReadMDNSEvent()`;
- `QueryRecordA()` and `ReadMDNSRecordEvent()`;
- service-type enumeration through a browse of
  `_services._dns-sd._udp`.

Remote response records enter `mdnsDiscoveryRecord()` and
`mdnsQueryRecord()` from `mdnsHandlePacket()`. That packet path deliberately
drops packets sourced by this host. The generic query code has one explicit
exception for the host's own A record, but registered local PTR, SRV, TXT,
address and service-type records have no equivalent path.

Two focused cases demonstrate the missing capability on unmodified master:

- `local-registration`: a browse and type enumeration cannot see an already
  announced local registration;
- `query-local-service`: a generic PTR query cannot see it either.

Master passes the other 38 discovery and 12 query cases in the same runs.

## Minimum change

- Keep multicast loopback filtering unchanged.
- Reuse the responder's canonical service packet builder and feed those
  records internally to the existing discovery cache and query queues, once
  per interface.
- Replay current announced registrations when an operation starts or
  refreshes.
- Reflect later announcements, updates and goodbyes, including service-type
  add/remove events.
- Target initial generic-query replay only to the new query session, so
  starting another query cannot duplicate events in an older one.

The existing cache, validation, coalescing, queue bounds, notification and
TTL-zero removal behavior remain the single implementations used for both
remote and local records.

## Validation

- Baseline: discovery 38/39 and query 12/13; only the two local cases fail.
- Candidate: discovery 39/39 and query 13/13 under AddressSanitizer and
  UndefinedBehaviorSanitizer. The local cases cover initial replay, a second
  query not duplicating the first, withdrawal, and a later registration.
- Responder results are unchanged at 31/34 on this master-based branch: the
  three failures are the RFC 6762 timing cases fixed independently by #1887.
- `cppcheck --enable=warning,performance,portability` passes on the changed C
  sources.
- Complete mdnsd compile and link pass for hosted linux-x86_64 and
  amiga-m68k.
- Companion repository `make test` passes.

No native hosted packet/client run is claimed yet. The behavior is internal
to mdnsd and the focused fixtures compile the canonical daemon sources, but a
version-consistent hosted image remains useful follow-up validation.

Not tested on physical m68k or big-endian hardware.
