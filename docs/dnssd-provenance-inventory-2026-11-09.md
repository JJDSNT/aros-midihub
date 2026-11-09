# DNS-SD provenance and attribution inventory — 2026-11-09

## Purpose

This inventory preserves the legal and historical lineage of the native AROS
DNS-SD work while keeping compatibility policy separate. `dnssd.library` does
not preserve the Bonami API, ABI, vectors, structures, IPC or runtime. Source
that is reused or derived still preserves its applicable notices and history.

This is an engineering inventory of source evidence, not a new license or a
legal opinion.

## Audited baselines

| Source | Audited identity | Evidence |
|---|---|---|
| Original Bonami source used by Midihub | `amigazen/Bonami` commit `540e93b2eae5bc35cef8fa05ac9a254174a29364` | Midihub gitlink `network/mdns/bonami` and `.gitmodules` URL |
| Canonical AROS baseline | AROS `master` commit `8e32af0a425aea427fb5f85c8e849acea6fb890e`; mDNS subtree unchanged since `d95e177e53a9ef6da78c2209f6a569da739e1e63` | `workbench/network/mdns/` |
| Rejected Bonami V2 proposal retained as evidence | branch `review/mdns-discovery-v2`, commit `bb650b3859a8cf7774d9a615130974603f28e2ba` | AROS PR #2123; not an implementation base |
| Native canonical-model proposal | branch `proposal/dnssd-canonical-model`, commits `e812bd5be9`, `86a8e7cdc9`, `0b64c38bb0` and `bb6b138b31` | Starts from AROS master `8e32af0a42`; no PR opened |
| Native architecture work | Midihub branch `review/mdns-modernization-plan` | ADR, reuse audit, roadmap and D1 prototype |

The original Bonami `LICENSE.md` and canonical AROS
`workbench/network/mdns/LICENSE` contain the BSD 2-Clause license and the
notice `Copyright 2026 amigazen project`. Source redistribution must retain
that notice, license conditions and disclaimer; binary redistribution must
reproduce them in its documentation or other materials as specified there.

The AROS subtree history at the audited baseline contains twenty Jaime Dias
commits and one later Nick Andrews build fix. This is material engineering
evidence that the current master implementation is our AROS evolution. The
amigazen notices remain mandatory for derived source and record its historical
origin; they do not transfer ownership of the later AROS architecture back to
the original API.

## Lineage statement

The final README and technical documentation should retain this historical
statement in addition to, not instead of, the legal notices:

> `dnssd.library` is an AROS-native evolution of the mDNS/DNS-SD
> implementation originally developed as part of the amigazen Bonami project.
> It preserves the original project's attribution and applicable license
> notices while evolving the architecture into a unified, native dual-stack
> DNS-SD subsystem for AROS.

## Source-family disposition

| Family | Observed provenance | Native disposition |
|---|---|---|
| `mdns_dns.c`, `mdns_net.c`, `mdns_reg.c`, `mdnsd.c`, `mdnsd.h`, `mdns_client.c`, `mdns_ipc.h` | Current headers preserve amigazen and later Jaime Dias notices | Reuse/refactor where technically suitable; retain all applicable notices in derived files. Replace Bonami-shaped types and IPC without erasing provenance. |
| `mdns_host.c`, `mdns_packet.c` | Current headers preserve amigazen and later Jaime Dias notices | Reuse mature protocol algorithms and tests; preserve notices. |
| library startup/IPC/API files and inherited public headers | Predominantly amigazen plus later Jaime Dias notices | Public Bonami API is retired, not ported. If implementation fragments are reused in `dnssd.library`, derived files retain the notices and BSD license. |
| tools such as Nearby, Share, Handshake and `dnssd` | Current headers include amigazen; several also include later Jaime Dias notices | Migrate useful behavior to native clients or retire. Reused code retains notices; compatibility binaries are not required. |
| `mdns_discovery.c`, `library/bonami_discovery.c`, `mdnsd_service.c` | Current headers identify later AROS work and do not uniformly contain an amigazen notice | Review file history before moving code. Do not add or remove authorship based only on filename or architectural lineage. |
| `mdns_query.c`, `library/bonami_query.c` | Current headers use an AROS Development Team notice | Preserve their existing notice and review source history before refactoring. |
| PR #2123 dual-stack event/cache changes | Jaime Dias commits `8715e6a67a` and `bb650b3859` on top of canonical AROS source | Reuse family-neutral cache extraction, deterministic sorting, multiple A/AAAA handling and tests. Do not carry `MDNSDiscoveryEvent2`, V1/V2 mode projection or public Bonami vectors into the native contract. |
| canonical-model canary | New AROS proposal source `workbench/network/mdns/dnssd_model.*`, based on native requirements and not copied from PR #2123 or Bonami structures | BSD-2-Clause implementation at `e812bd5be9`; the temporary Midihub implementation copy was removed. |
| canonical link/address inventory canary | New AROS proposal source `workbench/network/mdns/dnssd_link.*`, independently implemented from the native requirements rather than copied from Bonami or PR #2123 structures | BSD-2-Clause implementation at `86a8e7cdc9`; private daemon-engine state with no public compatibility contract. |
| AROSTCP inventory collector | New AROS proposal source `workbench/network/mdns/dnssd_link_aros.*`, independently implemented against existing public AROSTCP ioctl/tag contracts | BSD-2-Clause implementation at `0b64c38bb0`; no copied Bonami collector and no new public ABI. |
| canonical daemon socket/rescan integration | Refactor of existing AROS/Bonami-derived `mdns_net.c`, host/registration/query/discovery paths and daemon state, preserving every existing file notice | Commit `bb6b138b31`; removes the collapsed legacy internal interface projection while retaining mature protocol code and its provenance. No AROSTCP or public ABI change. |

The table intentionally distinguishes current header evidence from inferred
ancestry. A file lacking the original Bonami notice is not automatically
reclassified; its Git history must be checked before source is moved,
consolidated or relicensed.

## Mandatory preservation rules

1. Copying, renaming or substantially deriving a Bonami file does not remove
   its existing copyright notice or BSD-2-Clause obligations.
2. New contributor notices are additive and must not replace prior notices.
3. Keep the applicable BSD license in the source distribution and reproduce
   its binary-distribution notice as required.
4. Prefer history-preserving moves and focused commits so `git log --follow`
   remains useful.
5. Record whether each new native file is derived, refactored or newly
   implemented before upstream submission.
6. Protocol concepts, independent designs and test scenarios are not claimed
   as wholly original when their source evidence came from Bonami or later
   AROS work.
7. Removing the Bonami runtime/API from production does not authorize removal
   of notices from retained or derived code.

## Pre-PR attribution gate

For every upstream implementation patch:

- compare changed/new files with the audited Bonami commit and AROS history;
- list copied or materially derived functions in the patch notes;
- verify SPDX identifiers, copyright lines and the packaged license;
- confirm new source does not claim exclusive authorship over inherited code;
- confirm no compatibility-only Bonami V1/V2 representation entered the
  canonical internal model; and
- run a clean source-distribution inspection in addition to build/tests.

This gate applies independently of whether Bonami is still buildable. The
project preserves origin, not compatibility.
