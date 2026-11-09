# ADR: native DNS-SD service for AROS

**Status:** Proposed for review; component boundaries are selected, public ABI
and final library name are not frozen.
**Date:** 2026-11-08.
**Scope:** system-wide DNS-SD API, daemon boundary, client concurrency and
migration from `bonami.library`.

## Decision

AROS will have one native, system-wide DNS-SD facility. Applications use a
shared library, provisionally named **`dnssd.library`**, and the library talks
to the existing system `mdnsd`. The daemon remains the only owner of mDNS
sockets, protocol state, caches, probes, registrations and network-interface
state.

```text
AROS applications
        |
        v
  dnssd.library          native asynchronous public API
        |
        v
      mdnsd              mDNS/DNS-SD engine and system-wide state
        |
        v
 bsdsocket.library       AROSTCP IPv4/IPv6 and multicast
```

The native interface is designed from RFC 6762, RFC 6763, AROS task/message
port conventions and current consumer requirements. It is **not** an ABI
extension of Bonami. It does not inherit Bonami's vector order, tag values,
fixed text limits, IPv4-only event layout, hook semantics, public handles or
private IPC format.

Bonami remains useful as evidence of already implemented behavior, a source
of BSD-2-Clause code where technically sound, an interoperability/regression
surface during migration, and a possible compatibility adapter implemented
above the native API. It is not an internal layer of `dnssd.library` and
cannot constrain its public contract. No second mDNS responder will be added
to the library.

The working name `dnssd.library` communicates the intended system service and
is used throughout the design. Naming review must finish before the public
library skeleton is merged; changing the name before that gate has no ABI
cost.

## Evidence behind the decision

The audited AROS source is upstream `master` at
`d95e177e53a9ef6da78c2209f6a569da739e1e63`, under
`workbench/network/mdns/`.

The existing `mdnsd` is not a minimal responder that needs replacement. It
already owns:

- IPv4 and IPv6 multicast transport, per-link identity and AROSTCP address
  change notification (`mdns_net.c`, `mdnsd.c`);
- probing, announcement, conflict handling, goodbyes and local reflection
  (`mdns_host.c`, `mdns_reg.c`);
- daemon-wide browse/resolve and record-query caches, retries, expiry and
  bounded queues (`mdns_discovery.c`, `mdns_query.c`);
- registrations, TXT records, subtypes and proxy-host publication
  (`mdns_reg.c`); and
- task-scoped ownership and stale-client reaping (`mdns_client.c`).

The main legacy coupling is above that engine: `include/bonami/mdns.h` exposes
fixed IPv4-oriented structures and limits; `library/bonami.conf` preserves an
inherited vector table and compatibility stubs; `mdns_ipc.h` names the port
`"BonAmi"`, embeds Bonami structures and passes all commands through one fixed
message; and `library/bonami_*.c` translates inherited calls into that IPC.

This makes a new native facade and a library-neutral daemon protocol smaller
and safer than a protocol rewrite, without making Bonami compatibility a
permanent system contract.

## Responsibility boundaries

### `mdnsd`

`mdnsd` is authoritative for all state shared by applications:

- UDP 5353 sockets, multicast memberships and interface/address lifecycle;
- packet encoding, parsing, retransmission and RFC timing;
- cache records, expirations, known-answer behavior and network changes;
- host/service probing, conflict resolution and final advertised names;
- registrations, browse/resolve/query operations and operation queues;
- ownership, quotas, cancellation, client-death cleanup and daemon
  incarnation; and
- atomic snapshots/counters needed to recover from queue loss or restart.

The daemon does not call application code or retain application pointers.

### `dnssd.library`

The library is a thin, AROS-native client facade. It owns public ABI
validation, task-local session/operation handles, command/reply exchange,
message-port delivery, copying into caller-sized public events, local TXT
helpers and normal-close cleanup. It owns no DNS cache, interface list,
registration truth or network socket.

A shared library is not treated as proof of cross-process shared mutable
state; all authoritative coordination stays in the daemon task.

### Applications and AROSTCP

Applications own their reply ports, event buffers, policy and presentation.
They may run independent operations and never need the Midihub process or GUI.
Midihub is one consumer, not the service owner.

AROSTCP supplies sockets, multicast, scoped IPv6, interface indices and
network-change signals. DNS-SD policy and caches do not move into AROSTCP.
Stack changes require a demonstrated generic socket capability missing from
`mdnsd`.

## Native public contract (design constraints)

This ADR deliberately does not freeze function names or LVOs. The object
model must be proven by an in-tree canary before an ABI is accepted.

### Sessions and operations

- A client opens a task-owned session and supplies an event port.
- Register, browse, resolve and generic record query each create an opaque
  operation handle.
- Operations are independent. Cancelling one cannot drain or invalidate
  another operation's messages.
- Cancellation is explicit, idempotent and acknowledged. Afterwards the
  daemon owns and sends no message for that operation.
- Closing a session cancels all its operations. Abnormal task termination
  produces the same cleanup.
- Handles include a generation/incarnation so a stale handle cannot address
  a reused slot after daemon restart.

Candidate operation families, subject to the ABI gate, are:

```text
OpenSession / CloseSession
RegisterService / UpdateRegistration
BrowseServices / ResolveService / QueryRecord
CancelOperation
GetNextEvent or RequestEvent
BuildTXT / ParseTXT
GetStatus
```

AROS `TagItem` variants may coexist with typed, sized request records where
optional arguments justify them. Tags are not a substitute for versioning
result structures or specifying ownership.

### Event delivery

The preferred contract is message-port notification plus explicit event
read/copy. The daemon queues data in its own storage and sends a small
notification containing a non-pointer session/operation token; the client
then drains bounded events through the library. Direct copied event messages
are acceptable only if cancellation proves that no daemon-owned message or
stale signal reference survives the operation.

The public event header is sized/versioned and includes at least:

- event kind and add/remove/change/error flags;
- session/operation identity and sequence or generation;
- interface index and address family;
- full instance, service type, domain and SRV target;
- port, TTL and counted wire-format TXT data;
- zero or more IPv4/IPv6 addresses, with IPv6 scope; and
- truncation/lost-event state with an explicit resynchronization rule.

Public output is pointer-free or uses offsets within a caller-owned buffer.
No daemon address, `Task *`, `MsgPort *` or borrowed cache pointer is public.
Callers pass structure/buffer sizes and a newer library never writes beyond
them.

### Concurrency and failure rules

- Multiple tasks may register, browse, resolve and query concurrently.
- Registrations and operations use daemon-issued IDs plus daemon incarnation,
  not client pointer values.
- Name conflicts follow explicit auto-rename/no-auto-rename policy and report
  the final name asynchronously.
- Per-session quotas and bounded queues prevent exhaustion. Overflow emits a
  `LOST` state and requires a snapshot or operation restart.
- A daemon restart invalidates old handles and yields a detectable state;
  applications can recreate desired operations without polling a no-op update.
- Normal close is immediate. AROS task-termination notification is preferred
  for abnormal cleanup if its ordering is safe; stable task-ID reaping remains
  a fallback, not public ABI.

## Comparison with established implementations

Apple's `dns_sd.h` uses opaque `DNSServiceRef` objects for register, browse,
resolve, address and record queries. A client library talks to a background
responder and operations may share a connection. This validates daemon-owned
state, opaque operation identity and explicit cancellation. AROS should not
copy its socket/dispatch or callback model; message ports are native.

Primary source:
[Apple mDNSResponder `dns_sd.h`](https://github.com/apple-oss-distributions/mDNSResponder/blob/main/mDNSShared/dns_sd.h).

Avahi separates `avahi-daemon` from `libavahi-client`. Clients, entry groups,
service browsers and resolvers have explicit lifetimes and state callbacks.
This reinforces the service-owned cache and operation-object model. AROS
should not import D-Bus, Avahi's poll abstraction or its public ABI.

Primary sources:
[Avahi client](https://github.com/avahi/avahi/blob/master/avahi-client/client.h),
[lookup operations](https://github.com/avahi/avahi/blob/master/avahi-client/lookup.h),
[publication](https://github.com/avahi/avahi/blob/master/avahi-client/publish.h).

Both references support a resident system service with lightweight client
objects. The AROS-specific difference is Exec integration: tasks, message
ports and library conventions replace descriptors, dispatch, D-Bus and
foreign main loops.

## Alternatives rejected

**Continue extending `bonami.library`:** rejected as the target. It preserves
inherited vectors/stubs, fixed public limits, IPv4-shaped events and a
Bonami-named IPC as permanent contracts.

**Embed or fork Bonami inside `dnssd.library`:** rejected. It would make the
new library a compatibility wrapper and retain the constraints being removed.

**Put the mDNS engine in the shared library:** rejected. It creates per-process
engines or relies on unsupported shared-state assumptions, duplicates port
5353 ownership and cannot coordinate conflicts globally.

**Rewrite `mdnsd`:** rejected without new evidence. Its protocol and
dual-stack state already exist; refactoring the client boundary is smaller.

**Use Midihub as the service process:** rejected. DNS-SD is OS infrastructure
and must work without a MIDI application or GUI.

## Migration policy

1. Keep current `mdnsd` and `bonami.library` behavior operational.
2. Introduce library-neutral daemon operation IDs and IPC behind adapters,
   with regression tests proving no packet behavior changes.
3. Add `dnssd.library` as a second client of that internal protocol.
4. Move Midihub and system tools one consumer at a time.
5. Decide separately whether `bonami.library` becomes a compatibility adapter
   or remains a frozen legacy client.
6. Remove no Bonami source until all dependencies are enumerated and a
   release/deprecation policy is approved.

The historical `network/mdns/aros/` copy is not a second production
implementation. Accepted AROS `workbench/network/mdns/`, or an active proposal
branch based on it, remains canonical.

## Consequences and gates

- Existing IPv6, subtype, record-query and lifecycle work is preserved.
- The Bonami extended-event proposal remains evidence/test material, not the
  native ABI.
- First implementation is a neutral protocol seam and lifecycle prototype,
  not public vectors.
- ABI review includes 32/64-bit and m68k layout canaries, ownership AutoDocs
  and old/new library coexistence.
- Hosted AROS runtime, host fixtures and cross-builds remain separate evidence
  classes.
- No upstream PR follows without explicit authorization.

Protocol requirements are grounded in
[RFC 6762](https://datatracker.ietf.org/doc/html/rfc6762) and
[RFC 6763](https://datatracker.ietf.org/doc/html/rfc6763).
