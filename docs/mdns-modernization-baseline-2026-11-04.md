# AROS mDNSd/Bonami modernization baseline — 2026-11-04

## Purpose and evidence boundary

This is the reconciled G0 baseline for the next mDNSd/Bonami work. It records
what is in the canonical AROS tree, what remains outside it, which AROSTCP
facilities are available, and which local artifacts remain useful. It does
not propose code and does not treat a cross-compile or a host fixture as an
AROS runtime result.

The source snapshot audited here is AROS upstream `master`
`6ebbf49bb67b5162996024af747c05361b3b1b0b` under
`workbench/network/mdns/`. Its AROSTCP is `bsdsocket.library` 5.32, kernel
v0.71. The public Bonami implementation is 41.6.

The corresponding chronological backlog is
[`mdns-modernization-backlog-2026-11-04.md`](mdns-modernization-backlog-2026-11-04.md).

## Accepted upstream baseline

The following work is already merged and must not be reimplemented:

| Capability | Accepted work | Current result |
|---|---|---|
| registration and multicast service publication | AROS #1591 | task-owned registrations, probing, update, goodbye and multicast answers |
| queued browse and resolve | AROS #1695 | caller-owned queues and lent requests; no client Hook runs in mdnsd |
| service-type enumeration | AROS #1802 | browse `_services._dns-sd._udp` |
| DNS-SD tools | AROS #1803 | `dnssd`, Nearby, Share and Handshake |
| generic record query and IPv4 lookup | AROS #1836 | `QueryRecordA()`, `ReadMDNSRecordEvent()`, `LookupHostA()` |
| completed query tool modes | AROS #1846 | `dnssd -q`, `-G v4` and faithful SRV target use in `-Z` |
| legacy-unicast aggregation | AROS #1845 | multiple matching services can share one bounded legacy response |
| shared response delay/repeat suppression | AROS #1887 | shared PTR delay and per-link one-second repeat suppression |
| local registration reflection | AROS #1915 | local and remote records use the same discovery/query caches |
| TXT helpers | AROS #1916 | `MakeTXTRecordA()` and `FreeTXTRecord()` implemented |
| status snapshot | AROS #1942 | `GetMDNSStatusA()` implemented through IPC v5 |
| dual-stack core | AROS #1980 and #1991 | shared IPv4/IPv6 daemon, `ff02::fb`, A/AAAA, interface-index link identity and per-family replies |
| IPv6 multicast hop limit | AROS #1986 and #1992 | `IPV6_MULTICAST_HOPS` affects UDP/IPv6 output |
| IPv6 interface/address state | AROS #2053 | IPv6 `SO_BINDTODEVICE`, `SIOCGIFAFLAG_IN6`, address-change notification after DAD |

The accepted #1980 architecture remains the baseline: one daemon, one
registration engine and one per-link discovery cache. IPv6 does not get a
parallel responder or cache.

## Current capability and gap analysis

### Complete enough to preserve

- IPv4 and IPv6 mDNS multicast transport on UDP 5353.
- One shared IPv6 socket, with incoming link identity from `IPV6_PKTINFO`.
- IPv4 and IPv6 multicast membership and scoped `ff02::fb` output.
- Logical links keyed by the AROSTCP interface index rather than an IPv4
  address.
- A and AAAA publication, probing, conflict handling, generic queries and
  shared per-link caching.
- Family- and link-confined immediate and delayed replies.
- One missing-family query after either A or AAAA makes a host reachable,
  without indefinite retries for a family the host does not publish.
- Existing public vector order and the original `MDNSDiscoveryEvent` layout.
- `dnssd -q`, `dnssd -G v4`, `dnssd -Z`, Nearby, Share and Handshake as
  accepted tools. They need regression coverage, not reimplementation.

### Partial or missing

| Area | Current source behavior | Minimum remaining capability |
|---|---|---|
| IPv6 address lifecycle | `mdns_net.c` selects the first link-local returned by `SIOCGLIFADDR`; it does not call `SIOCGIFAFLAG_IN6`. `mdnsd.c` polls every three seconds. | Skip tentative, duplicate, detached and deprecated addresses; subscribe to `SBTC_SIG_ADDRESS_CHANGE_MASK`; retain a slow fallback scan. |
| receive TTL/hop limit | IPv4 uses `recvfrom()`. IPv6 `recvmsg()` requests only packet info. | Obtain IPv4 TTL and IPv6 hop limit and reject mDNS traffic whose value is not 255 before parsing or counting it. |
| IPv4 receive metadata | Audited upstream `master` (`6ebbf49bb6`, bsdsocket 5.32) exposes `IP_TTL` for output but no receive equivalent. Open AROS PR [#2105](https://github.com/aros-development-team/AROS/pull/2105), head `8f4d7f34b1`, proposes FreeBSD-compatible `IP_RECVTTL` and CUnit coverage. | Review and merge #2105; do not create a second stack implementation. mDNSd may consume it in G1c only after it is upstream. |
| IPv6-only browse/resolve | `MDNSDiscoveryEvent` contains only one IPv4 address and one IPv4 interface address. Legacy completion still requires A. | An appended, size-safe discovery read API. Do not grow or reinterpret the old structure. |
| multiple addresses | The cache can hold A and AAAA records, but one selected IPv4 address is projected into the legacy snapshot. | A versioned snapshot that can return multiple A/AAAA addresses per link, with explicit truncation/overflow behavior. |
| SRV target in discovery | The daemon follows the SRV target internally but the legacy event discards it. | Carry the actual target hostname in the new event. |
| DNS-SD subtypes | No Bonami registration or browse contract represents RFC 6763 section 7.1 subtypes. | Independent subtype registration, publication, browse, expiry and removal. |
| ingress RFC protection | Source-port/ID rules exist, but no receive TTL/hop-limit gate exists. | G1c after the IPv4 stack metadata exists. |
| remaining RFC work | Current documentation explicitly lists multi-packet known-answer continuation, passive duplicate-question suppression and negative caching/NSEC as absent. | Reproduce each independently before calling it implementation-ready. |
| AutoDocs | 21 functions are exported. The five registration/engine functions and six TXT/status/NIPC functions have source AutoDocs; the six queued discovery and four generic-query functions do not. | Document the ten missing functions and every later appended API after its contract stabilizes. |

The fixed limit of eight links and other bounded queues are documented
limits, not automatically defects. They need a demonstrated consumer failure
before a dynamic-allocation refactor is proposed.

## AROSTCP integration matrix

| AROSTCP facility | Upstream status | mDNSd use/status | Remaining action |
|---|---|---|---|
| IPv4 multicast and SANA-II group registration | merged (#1543, #1558) | consumed | driver-specific filters remain hardware validation, not a daemon replacement |
| IPv6 membership and local multicast fan-out | merged (#1924, #1945) | consumed by the shared IPv6 socket | regression only |
| complete ancillary chains, `IPV6_PKTINFO`, `IPV6_RECVHOPLIMIT` | merged (#1947) | packet info consumed; hop limit not requested | consume hop limit in G1c |
| link-local source selection and scoped datagrams | merged (#1970, #1973, #1974) | consumed | regression only |
| `IPV6_MULTICAST_IF` and `IPV6_MULTICAST_HOPS` | merged (#1975, #1986) | consumed | packet-capture regression |
| IPv6 `SO_BINDTODEVICE` | merged (#2053) | not required by the correct shared-socket design | do not refactor solely to use it |
| `SIOCGLIFADDR` and `SIOCGIFINDEX` | available | consumed | retain interface index as link identity |
| `SIOCGIFAFLAG_IN6` | merged (#2053) | not consumed | G1b |
| `SBTC_SIG_ADDRESS_CHANGE_MASK`, including DAD completion | merged (#2053) | not consumed; three-second polling remains | G1b |
| netservices reconfigure begin/end | available | consumed; sockets and `SocketBase` are released/reopened | retain and test with address signals |
| IPv4 received TTL ancillary data | absent from audited `master`; proposed by open PR #2105 | cannot be an upstream dependency until #2105 merges | G1a is in review and blocks only the IPv4 half of G1c |

## Public compatibility boundary

- `MDNSDiscoveryEvent` and `ReadMDNSEvent()` are frozen compatibility
  surfaces. Their size, offsets, IPv4 completion rule and behavior must not
  change for an old binary.
- New discovery data requires an appended vector and a versioned or
  caller-sized output contract. Pointer size, alignment and record layout
  must be checked for hosted x86-64 and amiga-m68k.
- `MDNSRecordEvent.re_InterfaceIndex` already provides a 32-bit link/scope
  identity without changing its structure size.
- Original Bonami vectors remain in place. AROS's queued API is the accepted
  implementation; Hook dispatch must not be reintroduced.
- Envoy/NIPC vectors remain compatibility stubs unless separately requested.

## Local repository reconciliation

### aros-midihub

The original `main` worktree was deliberately left untouched. At the start
of G0 it was 13 commits ahead and 8 behind `origin/main`, with seven modified
and four untracked mDNS files. Branch `g0/mdns-reconcile` rebased the 13 local
commits onto current `origin/main` (`6c6a4b68b42f6753bb640c115a783a1dfbead5e6`)
without conflict. The reconciled pre-G0-doc head is
`3c76c71df76fed08e6d9a6220258a01b21c90210`.

The useful uncommitted IPv6 fixtures and runners were copied into the G0
branch. The original worktree and the following SHA-256 identities were not
changed:

| Original uncommitted artifact | SHA-256 | G0 treatment |
|---|---|---|
| `docs/mdns-backlog.md` | `fb3a1dae2f2b60be7f3d5c728520dfe3cae2ddb506ef2a2a0ccfe4a540825740` | superseded by this baseline/backlog; original retained |
| `network/mdns/README.md` | `631528734042b1933140ebcceecfed61e9439c60ddaf03ba5446288aa03da75c` | replace stale wording with canonical-source notice |
| `patches/README.md` | `66db2dc1f9a00ff4f9fdb36dcc8b94d981aa237dde6f54c8b987843cad2d751b` | do not promote the obsolete #1980 patch |
| `tests/review/mdns_discovery.c` | `c2bad585fa8e3b796d2b751f922bb95318091b9fe93525d39c92cb99c57a738f` | migrated |
| `tests/review/mdns_query.c` | `f0364abea33965587076028fa80a20d970f9384e210510743cfab8d1b3001dee` | migrated |
| `tests/review/mdns_responder_rfc.c` | `77a4e33de6345a290703e66bf0456d655c7a7c7debe8b936d73ecb6f90310c25` | migrated |
| `tools/review-mdns-responder.py` | `b9255eff9ded995cd45efaba15c7976084b53af28cd4a5d9f6f2133936c385ed` | migrated |
| `docs/mdns-ipv6-investigation-2026-10-30.md` | `d4a578cb50b465ff763f170d19a8db42d82114a408ac5dce3cd4a87f9915c4af` | migrated as historical evidence |
| `patches/aros-mdns-ipv6-core.patch` | `f74ef23589468d65ef073b0ded3cb98175f7145517a2df36d57a83aeece0f81f` | retained only in original dirty worktree; obsolete implementation snapshot |
| `tools/review-mdns-ipv6-hosted.sh` | `b0ccd2504a93ee87035b057525ff438b82ba239ccb7da7386a010be00216ec94` | migrated |
| `tools/review-mdns-ipv6-links-hosted.sh` | `aa5d50f4011c3257c24bbce690408edf68a312ff971ee9d0a307ac5e0d4786dc` | migrated |

The historical `network/mdns/aros/` source is not a runtime implementation.
Tests must compile AROS upstream sources or an active proposal worktree. It
can be removed only after confirming no build/test target still reads it.

### aros-matter

`main` is clean and 25 commits ahead of its `origin/main` at
`85c39cd0028cc773aa378e2edd0310283a05427a`. Its head is
`deaf5a46b0e959c361a5cf4890ef2775f3cf6e30`. Those commits primarily record
AROSTCP groups 5b, 6a and 6b and their benches; the corresponding AROS work
has already been merged. They remain the owner of stack planning/evidence,
not a place for a second mdnsd implementation. Publishing or squashing that
local documentation is a repository-maintenance decision, not a prerequisite
for G1b or G2.

### AROS

`/tmp/AROS-mdns-g0` is a detached worktree at the audited upstream commit.
No AROS source was changed. Future proposals must start from a fresh current
master worktree, not from the historical #1980 patch or a CAMD worktree.

## G0 exit criteria

The migrated fixtures were run against the canonical AROS snapshot above,
compiling its current daemon sources directly with ASan/UBSan:

- responder: 38/38;
- discovery/cache/API: 42/42, including A-before-AAAA ordering,
  single-address-family completion and independent links;
- generic query: 14/14, including scoped AAAA on an IPv6-only link.

These are host fixtures. They do not by themselves claim an AROS executable
link, hosted socket runtime, Avahi interoperability or hardware behavior.

G0 is complete when:

1. the reconciled branch retains the useful fixtures/runners and documents
   every original dirty artifact;
2. those fixtures compile the canonical AROS `workbench/network/mdns/`
   sources and pass;
3. the definitive backlog has been reviewed;
4. no functional AROS implementation or upstream PR has started.
