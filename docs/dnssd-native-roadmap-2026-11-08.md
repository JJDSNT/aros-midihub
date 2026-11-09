# Native AROS DNS-SD chronological roadmap — 2026-11-09

## Rules

This roadmap implements the
[native DNS-SD ADR](dnssd-native-architecture-decision.md) from the
[reuse audit](dnssd-reuse-audit-2026-11-08.md). It supersedes the assumption
that the target is an ever-growing Bonami ABI, without discarding accepted
`mdnsd` work or tests.

No item becomes implementation-ready until existing interfaces/source
behavior are identified, the missing capability is reproduced, and the
minimum change and validation criteria are recorded. No upstream PR is opened
without explicit authorization.

Every implementation must use canonical AROS `workbench/network/mdns/` or an
active proposal, keep daemon and public-library changes independently
reviewable, avoid a second responder/cache, document ownership and sizes, and
separate host fixtures, cross-builds and Linux-hosted AROS runtime evidence.
Bonami compatibility and supported coexistence are not deliverables. Existing
Bonami tests are retained only when they exercise reusable protocol behavior;
consumer tests are migrated directly to `dnssd.library`.

AROSTCP is the network and managed-service reference. A demonstrated generic
gap in socket metadata, multicast, IPv6/interface state or change notification
blocks the dependent DNS-SD item. The correction and regression belong to an
independent AROSTCP proposal; `mdnsd` consumes it only afterwards. No private
DNS-SD workaround satisfies acceptance.

## Dependency order

| Order | Group | Priority | Gate |
|---:|---|---|---|
| 0 | D0 — decision, audit and evidence | P0 | accepted; provenance inventory complete |
| 1 | D1 — executable lifecycle contract | P0 | validated on Linux-hosted AROS |
| 2 | D2 — canonical model and neutral daemon boundary | P0 | D1 accepted; source/consumer inventory reconciled |
| 3 | D3 — `dnssd.library` ABI canary/skeleton | P0 | D1/D2 |
| 4 | D4 — registration and binary TXT | P0 | D3 ABI gate |
| 5 | D5 — browse/resolve/multi-address events | P0 | D3; retain current cache |
| 6 | D6 — generic query/status/network changes | P1 | D3 event model |
| 7 | D7 — lifecycle/resource hardening | P0 | D2–D6 operations exist |
| 8 | D8 — direct consumer migration and Bonami retirement | P1 | required native operations validated |
| 9 | D9 — interoperability/release gate | P1 | D4–D8 |

Validation is horizontal: every group brings its tests and audit update.

## D0 — decision, audit and evidence

**Priority/objective:** P0; select ownership and replacement policy without
freezing the public ABI or deleting unaudited source.

**Dependencies/blockers:** none.

**Repository/component/files:** Midihub docs; read-only audit of AROS
`workbench/network/mdns/`; this ADR, audit and roadmap plus the existing
modernization/V2 documents.

**Expected implementation:** documentation only. Classify Bonami API proposals
as source/protocol evidence, record canonical source SHAs and inventory all
copyright/license notices that derived files must preserve.

**Acceptance/tests:** boundaries prepared for review; explicit
no-Bonami-constraint rule; source matrix and migration plan complete.
The architecture and `dnssd.library` name were accepted on 2026-11-09 with no
Bonami compatibility or transition requirement. D0 closes when the provenance
and local/upstream work inventory are committed and reviewed.

**Upstream PR:** none.

## D1 — executable operation, concurrency and lifecycle contract

**Priority/objective:** P0; prove native ownership before public vectors or
daemon restructuring.

**Dependencies/blockers:** D0 policy; inspect current `task.resource`
notification ordering and Exec message ownership. Completed for the prototype.

**Repository/component/files:** Midihub isolated prototype under
`prototypes/dnssd/`, host test under `tests/` and native probe under
`ports/aros/`; candidate session/operation tokens; `TNA_REMOVED` where safe;
message ports and stable task IDs.

**Existing behavior/gap:** `mdnsd` polls task IDs and Bonami lends one request
per operation. There is no generic session contract proven for independent
native operations.

**Expected implementation:** bounded service-task prototype with two or more
client tasks, generation-safe IDs, queued notifications, serialized
cancellation and task-death cleanup. It contains no DNS implementation.

The prototype may reuse the already validated `TNA_REMOVED` safety pattern,
but its service lifecycle and later network integration are governed by
AROSTCP/netservices, not by MIDI-CI.

**Acceptance:** independent operations cannot consume each other's events;
cancellation returns every lent message; forced death leaves no task/port/
signal/handle reference; reused tasks cannot inherit state; restart rejects
old tokens; fallback is defined if termination notification is unsafe.

**Tests:** deterministic host state machine plus Linux-hosted AROS forced
termination, overflow and restart. Cross-build alone is insufficient.

**Recorded result:** host tests and sanitizer checks passed. A clean MetaMake
build of `DNSSDCoordProbe` ran inside Linux-hosted AROS and passed all 16
checks, including `TNA_REMOVED` cleanup after forced `RemTask()`, independent
operations, exact cancellation and recovery. See
[`dnssd-native-validation-2026-11-09.md`](dnssd-native-validation-2026-11-09.md).

**Upstream PR:** none for prototype; a generic Exec change would be separate.

## D2 — canonical model and library-neutral `mdnsd` boundary

**Priority/objective:** P0; replace Bonami-shaped daemon operations and IPC
without changing validated DNS packet behavior.

**Dependencies/blockers:** D1 accepted.

**Repository/component/files:** AROS `mdns_client.c`, `mdns_ipc.h`, `mdnsd.h`,
`mdns_reg.c`, `mdns_discovery.c`, `mdns_query.c`, `mdnsd.c`.

**Existing behavior/gap:** one fixed `MDNSIPCMsg` embeds Bonami public events
and pointer-shaped handles. The daemon has no canonical internal service,
address, TXT, session or event model independent of that public API.

**Expected implementation:** canonical family-neutral internal records; typed
operations; daemon-issued session and operation IDs; sized command-specific
payloads; neutral private service protocol; unchanged packet/cache algorithms.
There is no legacy adapter or duplicate V1/V2 representation.

**Acceptance:** packet/RFC behavior remains unchanged; malformed length,
version and command are rejected; no Bonami public type is present in the
canonical model or IPC; restart rejects stale IDs deterministically.

**Tests:** canonical packet/cache fixtures; malformed IPC; native registration,
browse, query and restart canaries; D1 contention/termination cases.

**Upstream PR:** one focused daemon-internal refactor after authorization; no
public vectors.

## D3 — native library ABI canary and skeleton

**Priority/objective:** P0; open a session, exchange a neutral command and
receive/cancel an event without committing the full API.

**Dependencies/blockers:** D1/D2 and native version policy.

**Repository/component/files:** new AROS shared library, public include,
generated proto/inline files, AutoDocs and build descriptions.

**Existing behavior/gap:** only `bonami.library` exposes the daemon and cannot
represent the selected lifecycle/event model.

**Expected implementation:** minimal session open/close, operation cancel and
version/status; sized request/event header; caller port; neutral IPC client.

**Acceptance:** no Bonami include; extensible public structures; opaque stale
handles documented; 32/64-bit and m68k layout canaries; normal/abnormal close
satisfy D1; precise ownership AutoDocs.

**Tests:** ABI canaries; complete Linux-hosted AROS library build/link;
two-task runtime open/cancel/terminate and daemon restart. No coexistence test
is required.

**Upstream PR:** separate public-library skeleton after API review and
authorization; do not combine with D2.

## D4 — registration and binary TXT

**Priority/objective:** P0; publish/update/withdraw through the native API.

**Dependencies/blockers:** D3; registration state and rename semantics review.
Any new interface-selection or network-state need is first checked against
the AROSTCP gap rule.

**Repository/component/files:** native library, neutral daemon protocol,
`mdns_reg.c`, Register/Update/Cancel events and TXT helpers.

**Existing behavior/gap:** engine registration exists, but Bonami uses fixed
escaped text and has no first-class final-name event.

**Expected implementation:** counted instance/type/domain, port, interface,
subtypes and wire TXT; pending, established/final-name, conflict, updated and
removed/error states.

**Acceptance/tests:** binary TXT round-trips; actual auto-renamed name is
reported; cancel confirms goodbye; clients cannot mutate each other; forced
death withdraws. Test empty/key-only/binary/255-byte/malformed TXT, duplicate
policies, subtypes, multi-interface/update/death, and hosted Avahi interop.

**Upstream PR:** native registration separate from discovery.

## D5 — browse, resolve and dual-stack multi-address events

**Priority/objective:** P0; expose the existing cache without IPv4 or Bonami
layout constraints.

**Dependencies/blockers:** D3; review batching, truncation and resync. D4 is
useful for local reflection but not an engine dependency.
Any missing receive/interface capability blocks here for an AROSTCP fix.

**Repository/component/files:** native library, `mdns_discovery.c`, Browse and
Resolve, native service/address events.

**Existing behavior/gap:** cache follows PTR/SRV/TXT/A/AAAA per link; public
event exposes one IPv4 address and drops domain/SRV-target detail.

**Expected implementation:** full service identity with bounded inline address
set or two-step sized copy; each address includes family and IPv6 scope/index.

**Acceptance/tests:** IPv6-only resolve; every dual-stack/multiple address is
preserved or explicit truncation occurs; link-specific changes; identical
local/remote shape; deterministic overflow recovery. Adapt V2 fixtures and
test IPv4/IPv6/dual-stack, multiple address/interface, RR order, expiry,
goodbye, flush, network change, overflow/restart and hosted peer interop.

**Upstream PR:** native discovery PR; do not append a Bonami V2 vector as a
substitute.

## D6 — generic records, status and network changes

**Priority/objective:** P1; add demonstrated generic facilities without
importing every Bonjour flag.

**Dependencies/blockers:** D3 event model and D5 address representation where
shared; a reproducer/consumer per capability.

**Repository/component/files:** `mdns_query.c`, native QueryRecord event,
status and network events.

**Expected implementation:** binary RDATA with owner/type/class/TTL/interface;
versioned resource/status snapshot; daemon/network reset. Wide-area DNS-SD is
excluded until separately demonstrated.

**Acceptance/tests:** clean cancellation/local reflection, observable bounds
and incarnation, no stale results after reconfiguration, stable unsupported
error. Test common/unknown RR, malformed RDATA, expiry, network removal/re-add,
status size negotiation and restart.

**Upstream PR:** split query and observability if useful.

## D7 — lifecycle and resource hardening

**Priority/objective:** P0; validate cleanup/fairness for every operation.

**Dependencies/blockers:** D2–D6 and D1 mechanism.

**Repository/component/files:** daemon session manager, queue allocation,
task-termination observer, shutdown/restart and AROS runtime tests.

**Expected implementation:** central teardown, bounded per-client/global
quotas, lost-event accounting and safe notification removal. Prefer validated
`TNA_REMOVED`; retain stable-ID reap as recovery if notification can be missed.

**Acceptance/tests:** forced death at every transition leaks no registration,
operation/message/port/signal; one client cannot starve another; shutdown
cancels replies before service-port removal. Repeated kill during create,
update, read and cancel; concurrent operations; exhaustion; ID reuse; daemon
restart; allocation failure; long resource-count loop on hosted AROS.

**Upstream PR:** lifecycle hardening separate from new vectors where possible.

## D8 — direct consumer migration and Bonami retirement

**Priority/objective:** P1; make native DNS-SD useful and retire parallel
runtime code without coupling infrastructure to Midihub.

**Dependencies/blockers:** D4/D5 for Midihub, D6 for query tools.

**Repository/component/files:** `ports/aros/midihub/discovery.c`,
`ports/aros/mdns_probe.c`, AROS tools, public includes and build descriptions.

**Expected implementation:** migrate Midihub in its own commit; add a native
diagnostic tool; migrate useful tools by capability; remove Bonami consumers,
public headers/library and build entries after their native replacements pass.
Preserve notices, history and reusable fixtures, not the compatibility API.

**Acceptance/tests:** Midihub publishes/browses both MIDI types natively; the
production image contains no Bonami requirement or parallel local daemon; no
known consumer includes Bonami headers. Run rewritten consumer mocks and
Linux-hosted publish/browse/resolve, late-daemon and restart tests.

**Upstream PR:** separate Midihub, AROS tool and retirement changes.

## D9 — interoperability and release gate

**Priority/objective:** P1; establish release quality and verify complete
native replacement after consumer migration.

**Dependencies/blockers:** D4–D8.

**Repository/component/files:** AROS validation/AutoDocs/examples/install,
Midihub integration and release policy.

**Expected implementation:** automated Avahi and Bonjour/mDNSResponder matrix
where available, packet RFC regression, lifecycle stress, examples and final
production-build audit for retired Bonami interfaces.

**Acceptance/tests:** publish/browse/resolve/TXT/subtypes, IPv4/IPv6,
multi-interface, restart and forced death have reproducible hosted AROS
evidence; no native consumer requires Bonami; limitations are explicit; full
AROS build, ABI canaries, host fuzz/fixtures and packet captures pass.

**Upstream PR:** implementation PRs first; removal remains independently
reviewable and requires explicit authorization before submission.

## Immediate next gate

D0 policy is accepted and D1 is validated. Complete the provenance/upstream
inventory, then audit and design D2's canonical internal model from the active
AROS proposal branch. Do not freeze public vectors before that model is proven,
and do not create a Bonami adapter while doing so.
