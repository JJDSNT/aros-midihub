# Native AROS DNS-SD chronological roadmap — 2026-11-08

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
Bonami regression remains until consumer migration is accepted.

## Dependency order

| Order | Group | Priority | Gate |
|---:|---|---|---|
| 0 | D0 — decision, audit and evidence | P0 | review these documents |
| 1 | D1 — executable lifecycle contract | P0 | task/port prototype passes on hosted AROS |
| 2 | D2 — neutral daemon service boundary | P0 | D1 accepted; Bonami regression green |
| 3 | D3 — native library ABI canary/skeleton | P0 | D1/D2 and final name |
| 4 | D4 — registration and binary TXT | P0 | D3 ABI gate |
| 5 | D5 — browse/resolve/multi-address events | P0 | D3; retain current cache |
| 6 | D6 — generic query/status/network changes | P1 | D3 event model |
| 7 | D7 — lifecycle/resource hardening | P0 | D2–D6 operations exist |
| 8 | D8 — consumers and Bonami decision | P1 | required native operations validated |
| 9 | D9 — interoperability/release gate | P1 | D4–D8 |

Validation is horizontal: every group brings its tests and audit update.

## D0 — decision, audit and evidence

**Priority/objective:** P0; select ownership without freezing API or deleting
legacy code.

**Dependencies/blockers:** none.

**Repository/component/files:** Midihub docs; read-only audit of AROS
`workbench/network/mdns/`; this ADR, audit and roadmap plus the existing
modernization/V2 documents.

**Expected implementation:** documentation only. Mark Bonami API proposals as
compatibility evidence and record canonical source SHA.

**Acceptance/tests:** boundaries prepared for review; explicit
no-Bonami-constraint rule; source matrix and migration plan complete.
`git diff --check` and Midihub `make test` passed on 2026-11-08. This is host
validation only; D0 remains open until architectural review.

**Upstream PR:** none.

## D1 — executable operation, concurrency and lifecycle contract

**Priority/objective:** P0; prove native ownership before public vectors or
daemon restructuring.

**Dependencies/blockers:** D0 review; inspect current `task.resource`
notification ordering and Exec message ownership.

**Repository/component/files:** Midihub isolated prototype under
`tests/review/`; candidate session/operation tokens; `TNA_REMOVED` where safe;
message ports and stable task IDs.

**Existing behavior/gap:** `mdnsd` polls task IDs and Bonami lends one request
per operation. There is no generic session contract proven for independent
native operations.

**Expected implementation:** bounded service-task prototype with two or more
client tasks, generation-safe IDs, queued notifications, serialized
cancellation and task-death cleanup. It contains no DNS implementation.

**Acceptance:** independent operations cannot consume each other's events;
cancellation returns every lent message; forced death leaves no task/port/
signal/handle reference; reused tasks cannot inherit state; restart rejects
old tokens; fallback is defined if termination notification is unsafe.

**Tests:** deterministic host state machine plus Linux-hosted AROS forced
termination, overflow and restart. Cross-build alone is insufficient.

**Upstream PR:** none for prototype; a generic Exec change would be separate.

## D2 — library-neutral `mdnsd` service boundary

**Priority/objective:** P0; decouple daemon operations from Bonami IPC without
changing DNS packets or removing the legacy client.

**Dependencies/blockers:** D1 accepted.

**Repository/component/files:** AROS `mdns_client.c`, `mdns_ipc.h`, `mdnsd.h`,
`mdns_reg.c`, `mdns_discovery.c`, `mdns_query.c`, `mdnsd.c`.

**Existing behavior/gap:** one fixed `MDNSIPCMsg` embeds Bonami events and
pointer-shaped handles. A second public facade cannot safely evolve on it.

**Expected implementation:** typed internal operations; daemon-issued session
and operation IDs; sized command-specific payloads; legacy Bonami adapter;
neutral private service protocol; unchanged packet/cache algorithms.

**Acceptance:** legacy wire behavior unchanged; malformed length/version/
command rejected; old and prototype clients coexist; restart rejects stale
IDs deterministically.

**Tests:** canonical host fixtures; malformed IPC; hosted Bonami registration,
browse, query and restart; D1 contention/termination cases.

**Upstream PR:** one focused daemon-internal refactor after authorization; no
public vectors.

## D3 — native library ABI canary and skeleton

**Priority/objective:** P0; open a session, exchange a neutral command and
receive/cancel an event without committing the full API.

**Dependencies/blockers:** D1/D2; final `dnssd.library` naming and version
policy.

**Repository/component/files:** new AROS shared library, public include,
generated proto/inline files, AutoDocs and build descriptions.

**Existing behavior/gap:** only `bonami.library` exposes the daemon and cannot
represent the selected lifecycle/event model.

**Expected implementation:** minimal session open/close, operation cancel and
version/status; sized request/event header; caller port; neutral IPC client.

**Acceptance:** no Bonami include; extensible public structures; opaque stale
handles documented; 32/64-bit and m68k layout canaries; normal/abnormal close
satisfy D1; precise ownership AutoDocs.

**Tests:** ABI canaries; complete hosted AROS library build/link; two-task
runtime open/cancel/terminate; legacy Bonami client alongside native canary.

**Upstream PR:** separate public-library skeleton after API review and
authorization; do not combine with D2.

## D4 — registration and binary TXT

**Priority/objective:** P0; publish/update/withdraw through the native API.

**Dependencies/blockers:** D3; registration state and rename semantics review.

**Repository/component/files:** native library, neutral adapter,
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

## D8 — consumers and Bonami compatibility decision

**Priority/objective:** P1; make native DNS-SD useful and retire parallel
runtime code without coupling infrastructure to Midihub.

**Dependencies/blockers:** D4/D5 for Midihub, D6 for query tools.

**Repository/component/files:** `ports/aros/midihub/discovery.c`,
`ports/aros/mdns_probe.c`, AROS tools, build descriptions, optional Bonami
adapter.

**Expected implementation:** migrate Midihub in its own commit; add a native
diagnostic tool; migrate tools by capability; inventory dependencies before
deciding frozen Bonami versus adapter. Adapter never lives inside the native
library.

**Acceptance/tests:** Midihub publishes/browses both MIDI types natively;
native-only image needs no Bonami; remaining Bonami consumers are listed;
historical daemon is not production-built. Run rewritten consumer mocks,
hosted publish/browse/resolve, late daemon/restart and legacy regression.

**Upstream PR:** separate Midihub, AROS tool and adapter changes.

## D9 — interoperability, release and deprecation gate

**Priority/objective:** P1; establish release quality and decide Bonami's
future only after native consumers work.

**Dependencies/blockers:** D4–D8.

**Repository/component/files:** AROS validation/AutoDocs/examples/install,
Midihub integration and release policy.

**Expected implementation:** automated Avahi and Bonjour/mDNSResponder matrix
where available, packet RFC regression, lifecycle stress and examples.
Deprecation remains a separate review decision.

**Acceptance/tests:** publish/browse/resolve/TXT/subtypes, IPv4/IPv6,
multi-interface, restart and forced death have reproducible hosted AROS
evidence; no native consumer requires Bonami; limitations are explicit; full
AROS build, ABI canaries, host fuzz/fixtures and packet captures pass.

**Upstream PR:** implementation PRs first; removal/deprecation requires
explicit authorization.

## Immediate next gate

Review D0 and decide the final name. If accepted, implement only D1: the
service/session/operation lifecycle prototype. Do not start public vectors or
rewrite `mdnsd` until concurrent ownership, cancellation, forced termination
and recovery are demonstrated on Linux-hosted AROS.
