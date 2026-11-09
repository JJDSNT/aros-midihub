# AROS mDNSd/Bonami definitive grouped backlog — 2026-11-04

> **Superseded as target architecture on 2026-11-08.** Completed evidence and
> accepted `mdnsd` work remain valid. New implementation ordering is in the
> [native DNS-SD roadmap](dnssd-native-roadmap-2026-11-08.md), based on the
> [ADR](dnssd-native-architecture-decision.md). Extending the Bonami ABI is no
> longer the default path to a native AROS DNS-SD API.

## Rules

This backlog begins at the reconciled baseline in
[`mdns-modernization-baseline-2026-11-04.md`](mdns-modernization-baseline-2026-11-04.md).
No implementation group starts until this document is reviewed. No upstream
PR is opened automatically.

Each implementation PR must:

- use the canonical AROS source or its active proposal branch;
- preserve IPv4 and the existing Bonami ABI;
- include a failing baseline test for the capability it adds or defect it
  fixes;
- build and link complete AROS targets, not only individual objects;
- distinguish host fixtures, cross-builds, hosted runtime, packet captures
  and hardware evidence;
- carry its G6 interoperability/regression cases instead of deferring them to
  a final testing phase.

## Dependency order

| Order | Group | Priority | Dependencies | May proceed independently of |
|---:|---|---|---|---|
| 0 | G0 — reconciliation and baseline | P0 | none | all code work |
| 1 | G2-D — extended discovery API design review | P0 | G0 | G1a, G1b |
| 2 | G1a — IPv4 receive TTL metadata in AROSTCP (#2105 in review) | P0 | G0 | G1b, G2-D, G3 design |
| 2 | G1b — IPv6 address lifecycle in mdnsd | P0 | G0; AROS #2053 merged | G1a, G2-D |
| 3 | G2 — dual-stack multi-address discovery API | P0 | reviewed G2-D; #1980 baseline | G1a; G1b unless lifecycle tests expose a direct dependency |
| 3 | G3 — DNS-SD subtypes | P0 | G0 and its own public-contract review | G2 behavior; public header/version edits must be serialized |
| 4 | G1c — TTL/hop-limit 255 receive validation | P0 | G1a for IPv4; #1947 for IPv6 | G2 and G3 |
| 5 | G4 — tools and consumers on the new contracts | P0 | relevant G2/G3 APIs merged | G1c |
| 6 | G5 — verified RFC hardening gaps | P1 | a failing baseline fixture per subgroup | unrelated G5 subgroups |
| 7 | G7 — AutoDocs, examples and cleanup | P2 | public G2/G3 ABI stable | protocol-only G5 work |

G6 is a horizontal validation contract applied to every row, not a final PR.

## G0 — repository reconciliation and source-of-truth audit

**Status:** prepared for review; canonical-source fixtures pass 38 responder,
42 discovery and 14 query cases under ASan/UBSan; no functional AROS code or
upstream PR.

**Scope**

- Rebase the committed Midihub history in an isolated branch while leaving
  the dirty `main` untouched.
- Preserve useful IPv6 fixtures and hosted runners.
- Record and retire, after review, the obsolete local #1980 patch rather than
  keeping a parallel implementation.
- Audit current AROS and AROSTCP source and merged PRs.
- Record aros-matter as owner of stack plans/evidence and Midihub as owner of
  mDNS/Bonami integration tests.

**Acceptance**

- Baseline document and this backlog reviewed.
- Canonical-source host fixtures pass.
- Original dirty worktree remains unchanged and recoverable.

**PR boundary:** local Midihub documentation/test commit only; no AROS PR.

## G2-D — extended discovery API design review

This design is intentionally first because it is the most important remaining
functional change and the only near-term public ABI addition.

**Status:** the concrete proposal is recorded in
[`mdns-discovery-api-design-2026-11-04.md`](mdns-discovery-api-design-2026-11-04.md).
The source behavior and missing capability are demonstrated, and candidate
m68k/host layouts have been compiled. Public names and layout still require
review before AROS implementation.

**Existing behavior**

- `MDNSDiscoveryEvent` is pointer-free and IPv4-only.
- `ReadMDNSEvent()` emits a legacy event only after an A record exists.
- The internal cache already keeps per-link A and AAAA records and follows the
  SRV target, but the legacy snapshot discards the target and projects one A.
- `QueryRecordA()` is a workable manual fallback, not complete DNS-SD resolve.

**Contract decisions required before code**

1. Exact appended vector name and minimum Bonami revision.
2. Fixed `V1` record versus caller-sized/tagged output. Either must reject
   undersized buffers without writing beyond them and permit later extension.
3. Fixed-width address entry layout: family, address length, interface index,
   scope and 16 address bytes. Do not expose architecture-dependent
   `sockaddr_storage` layout as ABI.
4. A bounded address count and explicit result when more addresses exist.
5. Whether the same browse/resolve reference may be read through either old
   or new vector, and the rule if a client mixes them.
6. Update identity: instance, service type and interface index; the address
   set and SRV target are snapshot contents, not identity.
7. Legacy projection: old clients retain the existing A-required completion
   rule byte-for-byte; new clients can complete with A, AAAA or both.

**Design acceptance**

- Layout table for m68k and LP64, with compile-time size/offset assertions.
- Lifecycle, ownership, queue overflow, daemon restart and cancellation rules
  written before implementation.
- Examples for IPv4-only, IPv6-only, dual-stack, multiple addresses and two
  links.
- Maintainer review of names and layout.

**PR boundary:** design document in Midihub first; no AROS PR until reviewed.

## G1a — AROSTCP IPv4 receive TTL metadata

**Repository owner:** AROS implementation; aros-matter plan, CUnit and wire
evidence.

**Current status:** implemented in open AROS PR
[#2105](https://github.com/aros-development-team/AROS/pull/2105), head commit
`8f4d7f34b1`. The observed GitHub checks are green; the PR remains blocked by
required review and has not been accepted or merged. Therefore audited AROS
`master` still lacks this facility.

**Scope**

- Preserve the FreeBSD-compatible `IP_RECVTTL` option/cmsg contract selected by
  #2105 (`IP_RECVTTL == 65`, `IPPROTO_IP` control message).
- Let an IPv4 UDP socket request the received IP TTL through `recvmsg()`.
- Reuse the existing ancillary construction/copy path fixed by #1947; do not
  add an mDNS-specific ioctl.

**Published commit**

1. One cohesive commit adds the public header and UDP/IP receive implementation,
   alphabetically registered `cunit-net-cmsg` coverage, documentation, and the
   bsdsocket/kernel revision bump.

**Tests**

- Consecutive datagrams with different TTLs return their own values.
- Ancillary data is complete on LP64 and reports `MSG_CTRUNC` with a short
  control buffer.
- A socket that did not request TTL gets no TTL control message.
- IPv4 UDP behavior without the option is unchanged.

The PR description reports 125 `cunit-net-cmsg` assertions and all 49 socket
suites passing twice, plus warning-free target compilation and static analysis.
Those results were not independently rerun as part of this G0 reconciliation;
only the published commit, PR metadata and green GitHub checks were observed.

**Acceptance**

- Maintainer review and merge without regressing the currently green checks.
- Preserve the reported complete hosted AROSTCP target build/link and CUnit
  evidence during review.
- Keep amiga-m68k target compile/link explicitly distinguished from a hardware
  run.

**PR boundary:** existing AROS PR #2105. Do not duplicate it. It blocks only the
IPv4 half of G1c; G1b and G2 remain independent.

## G1b — mdnsd IPv6 address lifecycle

**Repository owner:** AROS `workbench/network/mdns`; Midihub fixtures/runners.

**Current status:** implemented locally on AROS branch
`review/mdns-ipv6-lifecycle` as two bisectable commits:

1. `0b0c545e69` filters tentative, duplicate, detached and deprecated IPv6
   addresses through `SIOCGIFAFLAG_IN6`;
2. `a916c03fd6` subscribes mdnsd's task-local `SocketBase` to
   `SBTC_SIG_ADDRESS_CHANGE_MASK`, rescans on the signal and unregisters it
   before closing the library.

Both commits compile and link the complete hosted `mdnsd` executable in the
persistent `/home/jaime/aros-build-mdns-codex` tree. An IPv6-only hosted run
with AROSTCP kernel 0.71/bsdsocket 5.32, bonami.library 41.6 and the current
`dnssd` exchanged multicast with the Linux peer: AROS used source `fe80::2`,
sent five AAAA queries, answered 60 AAAA queries and emitted no packet with a
hop limit other than 255. This proves operation after DAD, not the exact
tentative-to-usable signal latency. An amiga-m68k build and a dedicated
address-state transition capture remain outstanding; no upstream PR is open.

**Scope**

- Query each enumerated link-local address with `SIOCGIFAFLAG_IN6`.
- Exclude `IN6_IFF_TENTATIVE`, `IN6_IFF_DUPLICATED`, `IN6_IFF_DETACHED` and
  `IN6_IFF_DEPRECATED`.
- Subscribe the daemon's task-local `SocketBase` to
  `SBTC_SIG_ADDRESS_CHANGE_MASK` and rescan after the signal.
- Keep a slow periodic rescan as fallback and retain netservices begin/end
  handling.
- Release signal subscription, sockets and memberships on every net-down and
  failure path.

**Proposed commits**

1. Filter IPv6 address state during enumeration.
2. Drive rescan from the address-change signal with timer fallback.
3. Focused host mocks and hosted lifecycle runner/documentation.

**Tests**

- tentative address is not joined or announced;
- successful DAD wakes mdnsd and begins probing without waiting three seconds;
- duplicate address never becomes an mDNS source;
- deprecation/removal withdraws old views and memberships;
- interface down/up, SLAAC renewal and in-place stack reload recover;
- IPv4-only operation remains unchanged.

**Acceptance**

- Host fixtures, complete mdnsd/bonami target build/link on hosted x86-64 and
  amiga-m68k, and real hosted lifecycle capture.
- No polling-only claim and no use of an address before DAD completes.

**PR boundary:** one mDNSd PR, independent of G1a.

## G2 — dual-stack, multi-address discovery

**Repository owner:** AROS Bonami/mdnsd; Midihub consumer and interop tests.

**Scope**

- Retain the SRV target and every reachable A/AAAA record in a rich per-link
  snapshot.
- Append the reviewed size-safe public read API and private IPC representation.
- Preserve `MDNSDiscoveryEvent`, `ReadMDNSEvent()` and old completion behavior.
- Make the new path complete on A-only, AAAA-only and dual-stack services.
- Preserve one shared cache, refresh, cache-flush, goodbye and queue model.

**Proposed commits**

1. Internal rich snapshot/address-set model with no public vector.
2. Public fixed-width records, generated binding and layout assertions.
3. Daemon IPC and library read vector, including old-event projection.
4. AutoDoc for the new call and contract documentation.
5. Focused fixtures and hosted/Avahi runner additions.

The commits form one cohesive AROS PR unless review shows the internal model
can be independently useful without leaving unreachable production code.

**Tests**

- A-only, AAAA-only, dual-stack, multiple A, multiple AAAA and mixed sets;
- link-local addresses carry their interface index/scope;
- identical service names on two links remain isolated;
- SRV target differs from instance name and is preserved exactly;
- address addition, expiry, cache flush, goodbye and interface reset update
  the snapshot;
- local registrations and remote Avahi registrations use the same contract;
- old ABI probe sees the exact old sizes/offsets and behavior.

**Acceptance**

- IPv6-only browse and resolve complete through the new API.
- Old binary/API fixtures remain byte-for-byte compatible.
- No repeated missing-family query loop.
- Complete library/daemon link on hosted x86-64 and amiga-m68k.
- Hosted IPv4-only, IPv6-only, dual-stack and two-link Avahi evidence.

**PR boundary:** normally one public-API AROS PR after the design review.

## G1c — receive TTL/hop-limit validation

**Scope**

- Request IPv4 TTL metadata from G1a and IPv6 hop limit metadata from the
  already merged `IPV6_RECVHOPLIMIT` support.
- Receive both families through `recvmsg()` with bounded control buffers.
- Discard a packet whose TTL/hop limit is absent or not 255 before packet
  counters, parsing, discovery or reply state changes.

**Proposed commits**

1. Dual-family receive metadata and validation.
2. Host fixture/mocks plus real hosted packet-injection runner.

**Tests and acceptance**

- query and response, multicast and legacy unicast, both address families;
- 255 accepted; 254, 64 and missing/truncated metadata rejected;
- invalid packets cannot populate cache, trigger conflict or increment the
  accepted-RX status counter;
- complete AROS target builds/links and IPv4/IPv6 hosted captures.

**PR boundary:** one mdnsd PR after G1a. G1b and G2 do not wait for it.

## G3 — DNS-SD subtype support

**Independence rule:** subtype semantics and cache identity do not depend on
the new G2 event layout. Design may proceed in parallel. If G2 and G3 both
touch public headers, library revision or generated bindings, publication is
serialized rather than combining unrelated behavior.

**Scope**

- Parse and validate subtype browse names from RFC 6763 section 7.1.
- Add a backward-compatible registration contract for zero or more subtypes.
- Publish/remove subtype PTR records per link and reflect local registrations.
- Browse, cache, refresh, expire and report subtype membership without
  confusing subtype PTRs with ordinary instance/type enumeration.

**Proposed commits**

1. Name validation and internal subtype representation.
2. Responder publication/goodbye/local reflection.
3. Browse/cache behavior and public registration tag or reviewed equivalent.
4. Fixtures, Avahi/Matter-format interop cases and documentation.

**Acceptance**

- ordinary clients and registrations remain unchanged;
- `_L<discriminator>._sub._matterc._udp` and
  `_I<id>._sub._matter._tcp` style names can be browsed;
- one instance may belong to multiple subtypes; removal of one does not
  remove the base service or another subtype;
- two-link cache isolation, TTL expiry and goodbye work;
- Avahi or another independent implementation observes both directions.

**PR boundary:** one subtype PR unless registration and browsing can be split
without publishing a half-contract.

## G4 — tools and consumer completion

`dnssd -q`, `-G v4` and the SRV-target correction in `-Z` are already merged.
This group adapts consumers; it does not recreate those modes.

**Scope**

- Let tools use G2 when the installed Bonami revision supports it and retain a
  clean legacy fallback.
- Print the real SRV target and all relevant A/AAAA records in `dnssd -Z`.
- Make Nearby and Handshake represent IPv6-only results.
- Extend Share/dnssd registration for subtypes only after G3, without making
  Envoy/NIPC a dependency.
- Decide separately whether `dnssd -G v6` is useful; do not overload
  `LookupHostA()` or silently change `-G v4`.

**Acceptance**

- Existing command syntax/output remains compatible unless a documented new
  mode is selected.
- Old library revisions are refused or use the legacy path without calling
  beyond the vector table.
- Hosted IPv4/IPv6/dual-stack tests against Avahi cover all four tools.

**PR boundary:** one tools PR after the APIs it consumes are accepted.

## G5 — verified RFC hardening only

These entries are candidates, not implementation-ready. Each requires a
fixture that fails current AROS master and a written minimum change.

| Subgroup | Documented current gap | Required baseline evidence | Likely PR boundary |
|---|---|---|---|
| G5a | known answers that continue in a second packet are not waited for; oversized known-answer lists are not continued | independent peer sends continuation and proves an unnecessary current answer or suppression failure | one known-answer continuation PR |
| G5b | negative caching/NSEC is not implemented | fixture proves repeated query/cache behavior and exact RFC record requirement | one NSEC/negative-cache PR if justified |
| G5c | passive duplicate-question suppression is not implemented | two queriers and packet capture reproduce duplicate multicast questions | one suppression PR if the measurable gap warrants it |

Already merged shared-response delay/repeat suppression, legacy-unicast
aggregation, local reflection, probing, conflict handling, cache-flush and
goodbye behavior stay regression cases. They are not presumed gaps.

## G6 — validation contract for every group

Every group records each evidence class separately:

1. host fixture using canonical AROS sources;
2. generated full AROS build and link for hosted x86-64;
3. generated full build and link for amiga-m68k;
4. Linux-hosted AROS runtime with real sockets;
5. packet capture proving family, interface, TTL/hop limit and record set;
6. Avahi interoperability for the group's public behavior;
7. Bonjour/hardware only when actually available, otherwise explicitly not
   verified.

Required regression modes are IPv4-only, IPv6-only, dual-stack, two links,
interface/address lifecycle, local registration, remote registration and old
Bonami ABI consumers. Not every group needs every mode, but omissions must be
stated.

## G7 — AutoDocs, SDK examples and cleanup

**Scope**

- Add AROS source AutoDocs to the ten currently undocumented queued
  discovery/query functions and every new G2/G3 public call.
- Document ownership, task context, lent-request lifetime, cancellation,
  overflow, daemon restart, version requirements and failure outputs.
- Add small register, browse, resolve/query and dual-stack examples that build
  with generated AROS headers.
- Remove obsolete local runtime sources or patches only after proving no test
  reads them.

**Acceptance**

- Every exported vector in `bonami.conf` has accurate source AutoDocs.
- Examples compile for hosted x86-64 and amiga-m68k and at least the hosted
  examples run against the final library.
- Documentation-only behavior changes remain separate from protocol fixes.

**PR boundary:** one documentation/examples PR after G2/G3 ABI stabilization;
repository cleanup remains a Midihub commit unless it changes AROS packaging.

## Expected upstream PR sequence

The present evidence supports this preliminary sequence, subject to review:

1. Review and merge existing AROSTCP PR #2105 for IPv4 receive TTL ancillary
   data (G1a); no replacement PR is planned.
2. mdnsd address flags/signalled lifecycle (G1b).
3. Bonami rich discovery event (G2), after G2-D review.
4. mdnsd TTL/hop-limit validation (G1c), after G1a.
5. DNS-SD subtypes (G3), independently designed but serialized around public
   header/version edits.
6. DNS-SD tools on G2/G3 (G4).
7. Zero to three G5 PRs, only for gaps with reproduced evidence.
8. Bonami AutoDocs/examples (G7).

That is six required implementation PRs plus one stack PR and optional
hardening/documentation cuts: approximately 7–10 PRs, derived from verified
scope rather than used as a target.
