# Native AROS DNS-SD reuse audit — 2026-11-08

## Scope and evidence boundary

This audit determines what can be reused for a native DNS-SD service without
letting Bonami define the new architecture. It does not implement or freeze a
public API.

Source evidence:

- AROS upstream `master`
  `8e32af0a425aea427fb5f85c8e849acea6fb890e`; the mDNS subtree is unchanged
  from `d95e177e53a9ef6da78c2209f6a569da739e1e63`;
- canonical source `workbench/network/mdns/`;
- Midihub branch `review/mdns-modernization-plan`;
- Midihub consumers under `ports/aros/` and historical local source under
  `network/mdns/aros/`; and
- RFC 6762, RFC 6763, Apple mDNSResponder and Avahi public source.

“Implemented” means source exists in the canonical AROS tree. Runtime claims
are listed separately; host fixtures and cross-builds are not AROS runtime
validation.

## Executive result

The reusable assets are the verified **mDNS/DNS-SD algorithms and AROSTCP
integration** in the AROS daemon, not its current object model and not the
Bonami public contract. Bonami structures and limits reach into registration,
discovery, record-query, ownership, IPC and network-interface state.

This justifies redesigning `mdnsd` around a canonical native model while
reusing algorithms that remain correct. It does not require a blind
clean-sheet protocol rewrite. Bonami API/ABI compatibility is explicitly out
of scope, including transitional compatibility.

The code being redesigned is predominantly our own upstream AROS work. The
canonical subtree history has twenty Jaime Dias commits and one later build
fix by Nick Andrews. Existing amigazen notices identify the original lineage
of derived code; they do not make the present daemon an externally imposed
architecture.

The master tree is an evidence baseline, not a design ceiling. No current
file, data structure, fixed limit, module boundary or control flow is retained
merely because it is merged. Even a full daemon replacement is permissible if
it produces a simpler correct system and passes the protocol, AROSTCP, Exec,
resource and interoperability gates.

The requested four-way disposition is:

| Class | Result |
|---|---|
| Existing | dual-stack mDNS transport, host/service lifecycle, registration, subtypes, browse/resolve cache, record queries, local reflection and bounded queues |
| Reusable | packet codecs, RFC timing/probing/conflict algorithms, AROSTCP socket and service-manager integration, cache algorithms and protocol fixtures after source review |
| Absent | canonical daemon state model, native session/operation ABI, neutral IPC, multi-address interface model, family-neutral service events, binary TXT registration, restart/loss recovery and native consumer integration |
| Obsolete for new development | Bonami-shaped public/event design as the target, the `"BonAmi"` private protocol as the only daemon boundary, and the historical local daemon copy as a runtime source |

## Source inventory and disposition

| Source | Current responsibility | Native disposition |
|---|---|---|
| `mdnsd.c` | daemon loop, IPC port, socket receive, timers and stack lifecycle | **Redesign around canonical state.** Reuse lifecycle sequencing and single-daemon ownership; replace Bonami port/message assumptions. |
| `mdns_net.c` | interface enumeration, multicast sockets and scoped output | **Reuse algorithms, redesign state.** Keep AROSTCP integration; represent multiple addresses independently. |
| `mdns_dns.c`, `mdns_packet.c` | wire codec and packet dispatch | **Reuse.** Add bounds/fuzz cases rather than replace the codec. |
| `mdns_host.c` | host probing, announce, conflict and goodbye | **Reuse.** Preserve packet behavior. |
| `mdns_reg.c` | service registration, subtypes, TXT/SRV/PTR/address publication | **Redesign state/API, reuse protocol behavior.** Use counted wire TXT/names, typed requests and daemon IDs. |
| `mdns_discovery.c` | browse/resolve cache and bounded event queues | **Redesign stored model, reuse cache algorithms.** No public event structure is internal state. |
| `mdns_query.c` | generic long-lived record queries | **Redesign stored model.** Use the common canonical record/event and operation model. |
| `mdns_client.c` | Bonami IPC dispatch and stale-owner reaping | **Replace.** Keep validated lifecycle semantics, not the single-message dispatcher or polling model. |
| `mdns_ipc.h` | fixed library/daemon message layout | **Replace.** Port `"BonAmi"`, IPC versions and embedded public events must not define the native protocol. |
| `library/bonami_*.c`, `bonami.conf` | inherited ABI and thin client | **Retire from production.** Reuse implementation fragments only when independently suitable and with attribution. |
| `include/bonami/*`, `include/libraries/bonamibase.h` | Bonami public types/tags/errors | **Retire from production.** Native public headers include none of them. |
| `tools/*`, commodity | Bonami consumers and test clients | **Migrate or retire.** Protocol fixtures may be retained after removing their Bonami API dependency. |
| `services.d/mdns` | AROSTCP-managed daemon lifecycle | **Reuse.** The library never starts a private responder. |

The AROSTCP service model is the principal native reference. At the audited
SHA, `mdnsd_service.c` registers `mdnsd` with `netservices.library` for stop,
reconfigure-begin and reconfigure-end signals. `mdnsd.c` binds its own
`bsdsocket.library` base to the daemon task and subscribes through
`SBTC_SIG_ADDRESS_CHANGE_MASK`. This lifecycle remains authoritative; the new
library does not create sockets or a competing service process.

### Confirmed daemon-core constraints

| Current source behavior | Why it is not the native target | Required direction |
|---|---|---|
| `mdnsd.h` includes `<bonami/mdns.h>` and `mdns_ipc.h` | Public legacy limits/types define daemon storage and prototypes | Split protocol/core/client headers; no Bonami public include in the core. |
| `MDNSRegisteredService` stores fixed character arrays, escaped TXT, `APTR rs_ClientRef` and task ID | Registration truth is shaped by the old call ABI | Counted wire names/TXT, daemon IDs, session ownership and explicit resource bounds. |
| discovery and query sessions store public Bonami events | Internal truth changes whenever a public structure changes | Canonical record/service snapshots; projection only at the native library boundary. |
| one `MDNSIPCMsg` embeds every request and output | Every capability grows one fixed message and couples client/daemon builds | Sized envelope plus typed command/event payloads. |
| `MDNSInterface` holds one IPv4 and one IPv6 link-local address; `MDNS_IFS_MAX` is eight | Multiple addresses are collapsed and the bound is silent policy | Separate link and address objects with explicit limits/overflow. |
| stable task-ID polling and lent client request messages | Cleanup depends on legacy request lifetime rules | D1 sessions/operations, `TNA_REMOVED`, daemon queues and stale-token rejection. |

### Source anchors

At the audited SHA, the behavior is anchored by these exact symbols/locations:

| Requirement | Canonical source location |
|---|---|
| address-change subscription | `mdnsd.c:76-98`, `SBTC_SIG_ADDRESS_CHANGE_MASK` |
| IPv6 receiving-link identity | `mdnsd.c:202`, `IPV6_PKTINFO`; `mdns_net.c` |
| IPC validation and dispatch | `mdns_client.c:24`, `mdnsClientMessage()` |
| abnormal-owner reap | `mdns_client.c:104`, `mdnsClientReap()` and `FindTaskByPID()` |
| browse/resolve operation dispatch | `mdns_discovery.c:1241`, `mdnsDiscoveryMessage()` |
| query operation dispatch | `mdns_query.c:288`, `mdnsQueryMessage()` |
| registration create/update/release | `mdns_reg.c:1441`, `:1541`, `:1603` |
| public legacy structures/limits | `include/bonami/mdns.h`, `MDNSDiscoveryEvent`, `MDNSRecordEvent`, `MDNS_MAX_*` |
| public vector surface | `library/bonami.conf` |
| legacy private protocol | `mdns_ipc.h`, `MDNS_IPC_PORT`, `MDNSIPCMsg` |

## Capability matrix

| Capability | Canonical behavior | Gap for native service | Disposition |
|---|---|---|---|
| Host mDNS IPv4 | UDP 5353, probing, conflict, answers and goodbye | Native status/errors only | Reuse |
| Host mDNS IPv6 | `ff02::fb`, shared socket, scoped send/receive and AAAA | Public discovery must represent IPv6 without a legacy projection | Reuse + new facade |
| Address lifecycle | Filters unusable IPv6 state and consumes AROSTCP address-change signal with fallback scan | Native interface-change status/events | Reuse + generic exposure |
| Service registration | PTR/SRV/TXT/address, update, conflict rename, goodbye | Asynchronous established/final-name/conflict state and typed binary TXT | Refactor boundary |
| Subtypes | Up to eight subtype labels | Do not freeze eight as public ABI; report resource failure explicitly | Reuse algorithm, redesign contract |
| Browse | Long-lived PTR browse, retry, add/remove snapshots | Domain, SRV target, address set, interface and loss semantics | Reuse cache, new event |
| Resolve | SRV/TXT/address correlation | Legacy event completes/projects one IPv4 address | Reuse cache, new event |
| Generic query | Standing RR query with queue | Native binary RDATA/owner/interface and common cancellation | Refactor boundary |
| TXT | Wire records exist; registration uses escaped text | Counted wire TXT plus safe build/parse helpers | New public contract |
| Multiple interfaces | Per-link cache/index, fixed `MDNS_IFS_MAX` 8 | Queryable bound/overflow; demonstrate need before dynamic engine storage | Reuse, evidence gate |
| Multiple addresses | Cache holds A/AAAA per link | Address set with family/scope and truncation/resume | New event |
| Notifications | Bounded queues plus one lent request | General operation notification and explicit lost/resync state | Refactor |
| Multiple clients | Task-ID ownership and per-task operation limits | Native sessions, generation-safe handles and fair documented quotas | Refactor |
| Normal cancellation | Stop/release returns lent request | One idempotent rule for every native operation | Refactor/test |
| Abnormal termination | Stable task-ID polling/reaping | Validate `task.resource` termination notification; retain safe fallback | Investigation gate |
| Daemon restart | `mi_DaemonID`; Midihub probes handles using no-op update | First-class restart state and stale-handle rejection | New service contract |
| Network reconfiguration | Closes/reopens stack, resets cache and reannounces | Deterministic reset/change notification | Refactor event |
| Status | Six daemon counters | Versioned incarnation/link/queue/resource/limit snapshot | Generic extension |
| Wide-area DNS-SD | Engine is primarily multicast `.local` | Demonstrate resolver/consumer requirements before design | Deferred |
| DNSSEC/SRP/proxy | Not present | Do not copy broad Bonjour flags without implementation | Deferred |

## Bonami constraints that must not cross the native boundary

| Bonami constraint | Source | Native decision |
|---|---|---|
| version 41.7 and inherited LVO order | `library/bonami.conf` | New library starts its own ABI/version. |
| fixed name/type/domain/host/TXT limits | `include/bonami/mdns.h` | Use DNS wire rules and caller sizes; quotas remain private/queryable. |
| one IPv4 endpoint/interface in discovery | `MDNSDiscoveryEvent` | Family-neutral address records include scope/interface index. |
| escaped space-separated registration TXT | `MDNS_TXTRecord` | Counted wire-format TXT; optional convenience helpers. |
| Hook parameters and legacy Hook behavior | public vectors/discovery docs | Message-port async delivery; daemon never calls client code. |
| client pointer-shaped handles | `MDNSIPCMsg.mi_Handle`, registration internals | Daemon-issued opaque ID plus generation/incarnation. |
| public port `"BonAmi"` | `mdns_ipc.h` | Neutral private service identity hidden from applications. |
| one fixed IPC structure for all commands/events | `MDNSIPCMsg` | Sized header and command-specific payloads with strict validation. |
| four operations/task and eight snapshots/operation | daemon/docs | Bounds are service policy, not public constants. |

The Bonami `MDNSDiscoveryEvent2` work remains useful because it proves
dual-stack cache extraction, layout canaries and interoperability. It is not
the native event definition, a compatibility requirement or a transitional
public API.

### PR #2123 disposition

AROS PR #2123 was audited at head
`bb650b3859a8cf7774d9a615130974603f28e2ba`. Its implementation commit is
`8715e6a67a` and its documentation commit is `bb650b3859`. It is not treated as
the final native API merely because it is the newest published proposal.

| PR #2123 content | Native disposition |
|---|---|
| richer browse/resolve snapshot including SRV priority, weight and target | Reuse in the canonical internal model after provenance review. |
| multiple A/AAAA collection, deterministic sorting and explicit truncation | Reuse algorithm and fixtures; expose through native counted records. |
| IPv6-only resolution and multiple pending addresses | Reuse behavior and interoperability evidence. |
| public `MDNSDiscoveryEvent2`, `ReadMDNSEvent2()` and Bonami 41.8 vector | Do not carry forward; `dnssd.library` defines its own sized native contract. |
| V1/V2 `eventMode`, `legacyAddress` and `legacyVisible` projection | Remove from the target model; these exist only to preserve Bonami representations. |
| IPC version 7 carrying legacy and V2 forms | Replace with one neutral, sized protocol rather than extending it. |

The current internal `DiscoveryEvent` embeds the public Bonami V2 event, so
the PR has not yet achieved a public-API-independent canonical model. D2 must
extract the reusable cache/address behavior before public native records are
designed. That is a replacement path, not an adapter path.

## Current Midihub integration

`ports/aros/midihub/discovery.c` directly opens `bonami.library` version 40,
calls `OpenMDNSEngine()`, registers MIDI services, starts queued browses and
drains `ReadMDNSEvent()`. It uses a no-op `UpdateServiceA()` as a daemon/handle
liveness probe. `ports/aros/mdns_probe.c` and review probes also compile
against Bonami.

These are direct migration consumers, not reasons to mirror the old ABI. The
native API must cover their required semantics first, then Midihub changes in
a separate consumer commit. There is no intermediate adapter requirement.

The historical `network/mdns/aros/` copy must not become a parallel runtime.
AROS `workbench/network/mdns/` remains canonical.

Current AROS master also contains `workbench/devs/ipp/dnssd.c`, added by
`ca74751ce6`, with its own IPv4-only DNS codec, multicast query sockets,
static 96-record cache and serialized/non-reentrant calls. It is a known
native-library migration consumer, not a second architectural reference. Its
printer scenarios should become interoperability tests; the duplicate
implementation is retired after `dnssd.library` covers them.

## Comparison matrix

| Concern | Bonjour/mDNSResponder | Avahi | Native AROS direction |
|---|---|---|---|
| shared authority | background daemon | `avahi-daemon` | existing `mdnsd` |
| client objects | opaque refs, optional shared connection | client, entry group, browser/resolver objects | session plus opaque operation IDs |
| async readiness | callback after socket/dispatch processing | callbacks through poll/main-loop and daemon IPC | Exec message notification plus explicit drain |
| publication lifecycle | deallocate reference/connection | entry-group reset/free and disconnect | explicit cancel/close plus task-death recovery |
| interface identity | 32-bit interface index | interface index + protocol family | AROSTCP index + family/scope |
| batching/loss | `MoreComing`; connection semantics | daemon/client state callbacks | bounded queue, `MORE` hint and mandatory `LOST`/resync |
| transport | client-stub/daemon IPC | D-Bus | private Exec messages |
| not copied | dispatch/socket and callback ABI | D-Bus, poll abstraction and callback ABI | native tasks, ports and library ABI |

The useful lesson is separation and lifecycle, not API imitation.

## Licensing and portability

- Current AROS mDNS source is BSD-2-Clause; its provenance is recorded in
  `workbench/network/mdns/LICENSE`.
- Bonjour and Avahi are architecture references only. No source import is
  proposed, so they add no platform or license dependency.
- Public ABI must be checked on 32-bit, 64-bit and m68k alignment before
  freeze. Linux-hosted AROS is the initial runtime platform; cross-builds are
  only layout/build evidence.

## Existing evidence worth preserving

| Evidence | Proves | Does not prove |
|---|---|---|
| canonical-source responder/discovery/query host fixtures | codec/cache/state behavior under mocks | AROS Exec/IPC/socket runtime |
| recorded Linux-hosted AROS + Avahi runs | daemon/library interoperability for tested cases | hardware or future native API |
| Bonami discovery/query/status/TXT probes | reusable scenarios and current behavior before replacement | native session contract or a compatibility obligation |
| V2 layout canaries | a pointer-free dual-stack event compiles on selected architectures | that it is the right native ABI |
| Midihub discovery mocks | consumer retry/register/browse behavior | network interoperability |

All tests should compile canonical upstream source or an active AROS proposal.
None justifies keeping a second local daemon.

On 2026-11-08, the Midihub `make test` suite passed after this documentation
change, including 27 mocked advertisement lifecycle checks, 24 mocked browse
checks and the existing protocol/component tests. This is host validation of
the unchanged consumer code, not AROS runtime evidence for the proposed API.

## Confirmed gaps and ownership

| Gap | Owner | Minimum correction | Validation gate |
|---|---|---|---|
| Bonami-coupled IPC | `mdnsd` internal boundary | replace it with a sized/versioned command protocol and daemon IDs | native clients pass; malformed lengths rejected; no Bonami public type crosses the boundary |
| native API absent | new AROS library | session/operation/event canary with AutoDocs | 32/64/m68k layouts; hosted two-task lifecycle |
| IPv6/multi-address discovery absent publicly | event projection + library | full address set and SRV target | Avahi dual-stack and IPv6-only runtime |
| binary TXT registration absent | library + daemon request | counted wire TXT and helpers | empty, binary, 255-byte and malformed cases |
| restart/overflow recovery absent | session contract | incarnation and lost/resnapshot events | forced restart and overflow |
| abnormal cleanup only polled | daemon lifecycle | validate termination notification, keep task-ID fallback | forced death during register/browse/query |
| native consumer migration absent | Midihub/tools | migrate after API gate | fixtures and Avahi interoperability |

### Network-stack gap rule

No missing AROSTCP facility is classified as a `mdnsd` or `dnssd.library`
implementation detail. A generic stack gap gets its own reproducer, AROSTCP
owner, minimum correction and stack regression before DNS-SD integration.
This is how receive metadata, IPv6 address-state visibility, multicast and
address-change notifications must continue to be handled. The DNS-SD backlog
may be blocked on such work; it may not bypass it with a private parallel
mechanism.

## Conclusion

Proceed with a canonical daemon-core design, not a facade-only refactor. Reuse
packet, timing, conflict and AROSTCP code only after mapping each algorithm to
the new link/address, record, service and operation models. Do not begin by
renaming Bonami symbols, wrapping its vectors or copying the daemon into
`dnssd.library`. Public API work remains gated on the redesigned core and the
already validated D1 lifecycle contract.
