# Native DNS-SD daemon-core audit — 2026-11-09

## Baseline and decision

The implementation baseline is canonical AROS `upstream/master` commit
`8e32af0a425aea427fb5f85c8e849acea6fb890e`. The mDNS subtree is unchanged
from `d95e177e53a9ef6da78c2209f6a569da739e1e63`. The local branch
`proposal/dnssd-canonical-model` follows that upstream baseline.

The current master daemon is our implementation and upstream contribution:
the subtree history contains twenty Jaime Dias commits and one later build fix
by Nick Andrews. Its retained amigazen notices correctly document the Bonami
origin of derived code. The redesign described here is the next evolution of
our AROS work, not replacement of an external black box.

The baseline supplies history and regression evidence only. It imposes no
requirement to preserve filenames, internal ABI, structures, fixed capacities
or subsystem boundaries. Selective reuse and clean replacement are equally
valid outcomes; acceptance depends on correctness and validation, not textual
similarity to master.

AROS PR #2123 is not the target and is not the branch base. It demonstrates
why extending Bonami with V1/V2 representations does not produce a native
core. Isolated algorithms or fixtures may be reconsidered later, but no
Bonami V2 public structure or IPC version is imported as architecture.

One system daemon still owns sockets, cache and shared publication/discovery
truth. That ownership role does not preserve the current implementation. The
daemon core may be reorganized or redesigned wherever its state is shaped by
Bonami limitations.

## Confirmed structural coupling

| Area | Exact upstream source | Confirmed constraint | Native correction |
|---|---|---|---|
| shared core header | `workbench/network/mdns/mdnsd.h` includes `<bonami/mdns.h>` and `mdns_ipc.h` | Daemon storage, limits and prototypes cannot compile independently of Bonami | Split protocol/core/client headers; no public-library header in the engine. |
| registration state | `MDNSRegisteredService` in `mdnsd.h`; `mdnsRegAdd()` in `mdns_reg.c` | Fixed `MDNS_MAX_*` arrays, escaped TXT, IPv4-only proxy address, pointer-shaped client ref and task ID | Counted DNS names and wire TXT, address sets, daemon object IDs and session ownership. |
| discovery state | `DiscoveryView` and `DiscoverySession` in `mdns_discovery.c` | Cache views and queues store `MDNSDiscoveryEvent`; readiness requires one legacy IPv4 A record | Store canonical service snapshots and address sets independent of public events. |
| record queries | `QuerySession` in `mdns_query.c` | Queue stores public `MDNSRecordEvent` and shares lent-request lifetime rules | Store canonical record events and use the common operation queue/lifecycle. |
| IPC | `MDNSIPCMsg` in `mdns_ipc.h`; dispatcher in `mdns_client.c` | One fixed message embeds every request/output and all Bonami limits; handles are `APTR` | Sized envelope, typed payloads, strict offsets/lengths and daemon incarnation/session/operation IDs. |
| ownership | client, discovery, query and registration modules | Per-feature ownership, stale-task polling and borrowed notification messages | Central session/operation manager using validated D1 termination/cancellation rules. |
| interface state | `MDNSInterface` in `mdnsd.h`; `struct found` and `find_interfaces()` in `mdns_net.c` | At most eight links; one IPv4 and one IPv6 link-local address retained per interface | Separate stable link objects from a counted address inventory; explicit bounds/overflow. |
| socket state | `mdns_net.c`, `mdnsd.c` | Proven per-interface IPv4 sockets and shared IPv6 socket are indexed through the collapsed interface array | Preserve the socket strategy initially, but drive it from canonical links and addresses. |
| names | host, registration and discovery modules | Presentation strings and fixed buffers are authoritative state in several paths | Counted DNS wire names internally; convert at parsing or presentation boundaries. |
| TXT | `txt_data()` in `mdns_reg.c` | Escaped client text is decoded only during packet creation | Store validated counted wire TXT directly; text helpers belong in the library. |
| resource policy | `MDNS_IFS_MAX`, `DISC_*`, `QUERY_*`, `MDNS_MAX_*` | Limits are scattered and several failures collapse to `BUSY` | Central policy, queryable counters and explicit overflow/lost/resync results. |
| parallel IPP discovery | `workbench/devs/ipp/dnssd.c`, added in upstream commit `ca74751ce6` | A second IPv4-only, non-reentrant DNS-SD client owns sockets, codec and a static 96-record cache | Treat as a direct `dnssd.library` migration consumer; retire the private protocol implementation after native coverage. |

## Reuse decision by responsibility

| Responsibility | Initial decision | Required proof |
|---|---|---|
| DNS codec and compression bounds | Reuse/refactor | Existing packet fixtures plus malformed-length and compression tests. |
| probing, conflict resolution, announcements and goodbyes | Reuse state-machine behavior | Deterministic timing/conflict fixtures against the canonical name/service model. |
| multicast socket setup and AROSTCP lifecycle | Reuse/refactor | IPv4-only, IPv6-only, dual-stack, reconfigure and interface removal on Linux-hosted AROS. |
| interface/address enumeration | Redesign storage and rescan diff | Multiple addresses on one link, address-state changes and explicit capacity tests. Any missing generic facility becomes an AROSTCP issue. |
| registration, discovery and query storage | Redesign | No Bonami include/type; binary TXT; multiple addresses; independent operations and forced-owner death. |
| client IPC and notification | Replace | Malformed envelope tests, exact cancellation, restart invalidation, queue loss/resync and task termination. |
| service-manager integration | Reuse | Stop/reconfigure ordering and socket-base lifetime on Linux-hosted AROS. |

## Canonical model gate

Before daemon integration, a private canary must prove:

- fixed-width, pointer-free same-host messages with total size and version;
- offsets and counted binary fields validated against received length;
- counted DNS wire names and wire-format TXT;
- separate instance, type and domain identity;
- SRV priority, weight, port and target;
- multiple IPv4/IPv6 addresses with stable interface index and IPv6 scope;
- deterministic address ordering/deduplication without legacy projection;
- daemon incarnation, session, operation and sequence correlation; and
- rejection of overlapping, truncated or malformed fields.

The canary source now lives in AROS `proposal/dnssd-canonical-model` at
commit `e812bd5be9`, under `workbench/network/mdns/dnssd_model.*`; its test is under
`developer/debug/test/dnssd/`. The earlier Midihub implementation copy was
removed. It remains a private model canary, not daemon integration or public
ABI.

## Link/address inventory gate

AROS proposal commit `86a8e7cdc9` adds the next private D2 canary in
`workbench/network/mdns/dnssd_link.*`, with its regression test beside the
model test under `developer/debug/test/dnssd/`. It is still daemon-engine
state, not a public `dnssd.library` structure.

The inventory establishes these minimum native properties:

- stable interface indexes and explicit per-observation generations;
- multiple independent IPv4/IPv6 addresses per link, including IPv6 scope;
- one deterministic ordering and deduplication path for both families;
- explicit up, multicast, loopback and address-usability state;
- validation before publication, using caller-supplied scratch storage so a
  bad or oversized rescan cannot partially replace active state;
- explicit required counts when policy capacity is insufficient; and
- service-task ownership instead of embedded locks or public-library state.

It does not enumerate interfaces, open sockets or infer AROSTCP behavior.
Consequently it proves no AROSTCP gap and implements no DNS-SD workaround for
one. The integration gate must feed this model from the actual AROSTCP
interface/address facilities and demonstrate every address on every relevant
link. If that generic input is missing or lossy, the minimum fix belongs in
AROSTCP and precedes daemon integration.

## Next implementation slice

1. Connect the canonical inventory to the daemon rescan boundary and audit
   the exact AROSTCP enumeration path without collapsing addresses.
2. Migrate one read-only discovery snapshot path to the canonical service
   model.
3. Only then define the first `dnssd.library` canary vectors.

No upstream PR is opened or updated without explicit authorization.
