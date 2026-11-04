# Bonami dual-stack discovery event design

Date: 2026-11-04  
Status: design complete; public names and layout await review before AROS
implementation

## Decision summary

Add one appended Bonami vector, `ReadMDNSServiceEvent()`, and one start tag,
`MDNS_EventFormat`. Existing browse and resolve entry points remain the only
ways to create operations. A new client requests
`MDNS_EVENT_FORMAT_SERVICE`; omitting the tag retains the exact legacy event
format and completion rule.

The daemon continues to use one cache, one set of browse/resolve sessions and
one refresh/query path. Each discovery view has a legacy projection and a rich
projection. The rich projection completes with SRV, TXT and at least one A or
AAAA record. The legacy projection still completes only when an A record is
available.

This is deliberately not a second discovery API implementation. It is a new
bounded projection and read vector over the existing operation.

## Evidence and demonstrated gap

The audited source is AROS `master` commit
`6ebbf49bb67b5162996024af747c05361b3b1b0b`:

- `include/bonami/mdns.h` defines the pointer-free but IPv4-only
  `MDNSDiscoveryEvent`.
- `library/bonami.conf` exports 21 functions at Bonami 41.6;
  `ReadMDNSRecordEvent()` is the last vector.
- `mdns_discovery.c:snapshot()` follows the SRV target but requires an A
  record, copies one four-byte address and discards the target name.
- `DiscoveryRecord` already stores A and AAAA records by interface index.
- the missing-address logic asks once for the other family after either A or
  AAAA is available, avoiding a permanent missing-family query loop.
- `mdns_ipc.h` uses private IPC version 5 and embeds the legacy discovery
  event in each message.

The canonical-source host fixture was rerun under ASan/UBSan: all 42 discovery
cases passed. Its `single-address-family` case demonstrates both relevant
behaviors: an A-only service completes and does not repeatedly query AAAA;
an AAAA-only service does not produce a legacy event, does not repeatedly
query A, and completes only if an A record later arrives. This is host-fixture
evidence, not an AROS runtime or Avahi interoperability result.

`QueryRecordA()` can expose AAAA records, but it does not return an atomic
DNS-SD resolution: the caller would have to reproduce SRV-target tracking,
per-link association, expiry and address-set updates. It is therefore not a
replacement for the missing discovery projection.

## Public contract

### Selecting the event format

Reserve the currently unused tag value `MDNS_Dummy + 19`:

```c
#define MDNS_EventFormat                (MDNS_Dummy + 19)
#define MDNS_EVENT_FORMAT_LEGACY        0
#define MDNS_EVENT_FORMAT_SERVICE       1
```

The tag is accepted by `BrowseServiceTypeA()` and `ResolveServiceA()` only
when `MDNS_EventMode` is `MDNS_EVENT_QUEUE`.

- absent or `MDNS_EVENT_FORMAT_LEGACY`: the reference is read only with
  `ReadMDNSEvent()` and keeps today's behavior;
- `MDNS_EVENT_FORMAT_SERVICE`: the reference is read only with
  `ReadMDNSServiceEvent()`;
- any other value returns `MDNSERR_BADPARAM` without creating an operation;
- using the wrong reader for a reference returns `MDNSERR_BADPARAM` and does
  not consume an event.

Binding the format when the operation starts prevents an AAAA-only event from
waking a legacy reader that cannot represent it. It also makes the queue have
one unambiguous cursor. `RequestMDNSEvent()` remains valid for either format
and needs no new wakeup API.

### New read vector

Append this function after `ReadMDNSRecordEvent()`:

```c
LONG ReadMDNSServiceEvent(APTR ref,
    struct MDNSServiceEvent *event, ULONG eventSize);
```

The proposed generated register assignment is `(A0,A1,D0)`. No existing
vector moves. Following this component's existing policy for appended vectors,
the minimum library revision becomes 41.7. A caller must verify a version
greater than 41, or version 41 revision 7 or later, before calling the new
vector.

`eventSize` must be at least `MDNS_SERVICE_EVENT_V1_SIZE`. Larger buffers are
accepted; version 1 writes only its defined prefix. The library builds the
result in local memory and copies it only after a successful daemon reply, so
the caller's buffer is unchanged on every error. A NULL or undersized buffer
returns `MDNSERR_BADPARAM` without consuming an event.

### Fixed-width structures

Do not expose `sockaddr_storage`: its layout is an unnecessary public ABI
dependency. Use fixed-width fields and network-order address bytes:

```c
#define MDNS_ADDRESS_IPV4                 4
#define MDNS_ADDRESS_IPV6                 6
#define MDNS_SERVICE_ADDRESS_MAX          8
#define MDNS_EVENTF_ADDRESSES_TRUNCATED   (1UL << 0)

struct MDNSServiceAddress
{
    UWORD sa_Family;       /* MDNS_ADDRESS_IPV4 or MDNS_ADDRESS_IPV6 */
    UWORD sa_Length;       /* 4 or 16 */
    ULONG sa_ScopeID;      /* interface index for scoped IPv6, else zero */
    UBYTE sa_Address[16];  /* network order; unused bytes are zero */
};

struct MDNSServiceEvent
{
    ULONG se_Code;
    ULONG se_Flags;
    ULONG se_InterfaceIndex;
    UWORD se_Port;
    UWORD se_TXTLength;
    UWORD se_AddressCount;
    UWORD se_AddressStored;
    char  se_Name[64];
    char  se_Type[MDNS_MAX_TYPE + 1];
    char  se_HostName[256];
    UBYTE se_TXT[MDNS_DISCOVERY_TXT_MAX];
    UWORD se_Reserved;
    struct MDNSServiceAddress
        se_Addresses[MDNS_SERVICE_ADDRESS_MAX];
};

#define MDNS_SERVICE_EVENT_V1_SIZE 1080
```

`se_AddressCount` is the complete number of usable cached A and AAAA records
for the SRV target on this link. `se_AddressStored` is the number copied,
never greater than eight. If the complete set is larger, the stored prefix is
still valid and `MDNS_EVENTF_ADDRESSES_TRUNCATED` is set. The count is bounded
by the daemon's 128-record cache and fits in `UWORD`.

Addresses are de-duplicated and ordered deterministically by family (IPv4
first) and then lexicographically by their network-order bytes. This prevents
record arrival or cache-slot order from producing spurious snapshot updates.
For a link-local IPv6 address `sa_ScopeID` is `se_InterfaceIndex`; it is zero
for IPv4 and unscoped IPv6 addresses. The interface index, not an IPv4
interface address, is the link identity.

`se_HostName` is the expanded SRV target without a trailing dot. It is not
derived from the instance name. `se_TXT` remains counted wire-format TXT and
`se_Port` remains host order, matching the legacy API.

The eight-address bound limits the additional memory in the existing eight
event queue and 32-view table. Truncation is explicit; consumers that truly
need an unusually large RRset may use `QueryRecordA()` after receiving the
atomic service snapshot.

### Verified candidate layout

The candidate definitions were compiled with the host compiler and with the
existing `m68k-amiga-elf-gcc` cross compiler. This verified the following
offsets; it did not build or link bonami.library.

| Structure/field | m68k offset/size | hosted LP64 offset/size |
|---|---:|---:|
| legacy `MDNSDiscoveryEvent` size | 626 | 628 |
| `MDNSServiceAddress.sa_Family` | 0 | 0 |
| `sa_Length` | 2 | 2 |
| `sa_ScopeID` | 4 | 4 |
| `sa_Address` | 8 | 8 |
| `MDNSServiceAddress` size | 24 | 24 |
| `MDNSServiceEvent.se_Code` | 0 | 0 |
| `se_InterfaceIndex` | 8 | 8 |
| `se_Name` | 20 | 20 |
| `se_Type` | 84 | 84 |
| `se_HostName` | 117 | 117 |
| `se_TXT` | 373 | 373 |
| `se_Reserved` | 886 | 886 |
| `se_Addresses` | 888 | 888 |
| `MDNSServiceEvent` size | 1080 | 1080 |

The implementation must place equivalent compile-time assertions beside the
public ABI probe and compile them with the actual AROS header on both targets.

## Snapshot and queue behavior

The identity of a service view remains instance label, service type and
interface index. SRV target, port, TXT and address set are snapshot contents.
A content change follows today's replacement convention: removal of the old
snapshot followed by addition of the new one. Type enumeration remains a
special snapshot with empty name, target, TXT and address set.

Each `DiscoveryView` will track two projections:

1. legacy presence/event, generated by the existing A-required rule;
2. service presence/event, generated by SRV + TXT + nonzero port + at least
   one A or AAAA.

Notification compares and queues only the projection selected by each
session. Consequently, adding or removing only an AAAA record does not create
a legacy remove/add pair when its selected A, SRV and TXT are unchanged.
Legacy A selection retains the present `find_record()` behavior rather than
being silently changed to the first member of the new sorted set.

Resolve uses the same format-specific completion rules. A legacy AAAA-only
resolve still reaches `MEVENT_RESOLVE_FAILED` at the existing deadline. A
service-format resolve returns `MEVENT_RESOLVED` as soon as either address
family completes the snapshot.

Existing ownership, per-task limits, queue coalescing, terminal overflow,
lent-request wakeup, cancellation, daemon-incarnation checks and network-reset
behavior do not change. A rich snapshot is stored in the same bounded queue;
no caller-owned event buffer reaches mdnsd.

## Private implementation boundary

The private IPC version increments from 5 to 6. Add an event-format field, a
fixed `MDNSServiceEvent` payload and a `MDNS_IPC_READSERVICE` command. The
library-side discovery record also stores the selected format so a wrong read
can fail before IPC. The daemon validates it again.

This private structure may grow because mdnsd and bonami.library are installed
together and already reject different IPC versions. It must not contain a
client pointer to the output event.

Expected files in the eventual AROS change:

- `include/bonami/mdns.h`: tag, constants and fixed-width structures;
- `library/bonami.conf`: appended vector and 41.7 revision;
- `library/bonami_discovery.c`: format validation and new read vector AutoDoc;
- `mdns_ipc.h` and `library/bonami_ipc.c`: private IPC payload/copy;
- `mdns_discovery.c`: dual projections and address-set construction;
- `DISCOVERY.md` and `README.md`: contract and example;
- canonical host fixtures plus an AROS ABI probe.

## Proposed commit sequence

Keep one cohesive AROS PR, with bisectable commits:

1. Refactor the internal snapshot builder to produce legacy and rich
   projections, with host fixtures but no public vector.
2. Add public fixed-width definitions, layout assertions and generated binding.
3. Add IPC version 6 and `ReadMDNSServiceEvent()`, including format mismatch,
   buffer-size and unchanged-on-error tests.
4. Add source AutoDoc, user documentation, an example and hosted Avahi runners.

Commit 1 must leave legacy behavior and all existing tests unchanged. Commits
2 and 3 must build together if the generated binding cannot safely reference
an unimplemented vector at an intermediate commit; in that case combine them
rather than leave a link-broken revision.

## Validation and acceptance

Host fixtures, target builds and real runtime evidence must remain distinct.

Required canonical-source fixtures:

- legacy A-only, dual-stack and AAAA-before-A behavior unchanged;
- rich A-only, AAAA-only, dual-stack, multiple A, multiple AAAA and mixed sets;
- deterministic ordering, duplicate removal and explicit truncation above
  eight addresses;
- different SRV target preserved exactly;
- identical instance/type on two links remains isolated by interface index;
- address add, expiry, goodbye, cache flush and network reset update only the
  affected projection;
- missing-family query occurs once after reachability, never indefinitely;
- wrong reader, NULL/short buffer, queue overflow, stop, task exit and daemon
  restart preserve their documented results;
- old structure size/offset assertions and old source/binary API probe remain
  unchanged.

Required builds:

- complete hosted x86-64 mdnsd and bonami.library build and link;
- complete amiga-m68k target build and link;
- generated headers/stubs produced from `bonami.conf` at the new vector;
- no claim of runtime success from either cross-build alone.

Required runtime/interoperability evidence:

- IPv4-only Avahi registration browsed and resolved by both readers;
- IPv6-only Avahi registration resolved by the service reader while the
  legacy behavior remains documented;
- dual-stack and multiple-address updates;
- two links carrying the same instance without cross-link address mixing;
- local Bonami registration and remote Avahi registration use the same event
  contract.

## Explicit non-goals

- No new daemon, cache or parallel IPv6 discovery path.
- No change to `MDNSDiscoveryEvent`, `ReadMDNSEvent()` or their default start
  behavior.
- No dynamic unbounded event allocation or caller pointer retained by mdnsd.
- No subtype API, DNS-SD tool conversion or RFC-hardening change in this PR.
- No claim that the current host fixture is an AROS runtime or Avahi test.
