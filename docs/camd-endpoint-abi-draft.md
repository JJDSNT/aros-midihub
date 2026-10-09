# CAMD endpoint ABI draft — gate U01

**Status:** Draft for AROS upstream review; not implemented and not frozen.
**Depends on:** [endpoint architecture decision](camd-endpoint-architecture-decision.md).
**Purpose:** Make the ABI questions concrete without assigning public vector,
tag or protocol-bit values prematurely.

The first executable evidence is the
[private endpoint-core model](../prototypes/camd/README.md). It exercises the
records, generations, snapshots and retirement rules on the host, but is not
linked into CAMD and does not make any symbol in this draft public.

## ABI strategy

Expose the client API as a small set of ordinary CAMD library functions,
appended after every existing version 42 vector and described in
`camd.conf`. This is the native AROS library mechanism: function-list order
defines vector order, while genmodule emits public prototypes, inline calls
and link stubs with the target's calling convention. A public table of
ordinary C function pointers would bypass those generated call paths and add
pointer-size and callback-ABI questions without removing the compatibility
obligation.

The next library version is expected to be 43, with the new entries placed
behind a `.version 43` boundary. The number is a release-planning decision,
not frozen until upstream accepts the function list. Clients request an
adequate library version with `OpenLibrary()` and negotiate individual record
versions through `Size` and `Version`; no separate `GetInterface()` vector is
needed.

Only the stable client surface belongs in the first public ABI:

- acquire/release and enumerate an immutable endpoint snapshot;
- start/read/end a bounded endpoint watch;
- open/close an endpoint session;
- send/receive complete UMP events and query/drain/cancel session state.

Exact names, vector numbers and m68k registers remain unassigned until the
prototype header and genmodule output are compiled on the supported targets.
All acquired objects must be released before closing `camd.library`.

The provider API stays private for the first implementation. Its sized
operations table is an internal CAMD contract exercised by the legacy-driver
adapter and a software provider. If later made public for independently built
drivers, it receives its own version gate, callback declarations and ABI
review; it is not smuggled into version 43 through the client surface.

### Why this is a hybrid rather than a literal copy of either model

The AROS vector mechanism is retained where it is strongest: a compact,
stable application ABI generated for every target. It is not used as the
internal object model. Registry, provider and transport operations remain
private C interfaces that can evolve while lifecycle rules are tested.

The alternatives are weaker for this project:

- exposing every provider operation as a library vector would freeze an
  unvalidated driver contract and permanently expand CAMD's public surface;
- returning a public function table would make calls less idiomatic on AROS
  and require a second cross-architecture calling convention;
- creating a separate `camd2.library` would duplicate graph ownership and
  make MIDI 1.0 compatibility a cross-library synchronization problem.

Thus the improvement is not replacing AROS vectors, but keeping them as a
thin public facade over a versioned internal endpoint core.

## Fixed-width public values

Do not expose private pointers as handles. These records use `ULONG` words so
their layout is identical on 32-bit and 64-bit targets and does not depend on
native 64-bit alignment:

```c
struct CAMDHandleV1 {
    ULONG slot;
    ULONG generation;
};                              /* 8 bytes */

struct CAMDEndpointIDV1 {
    ULONG word[4];
};                              /* 16 bytes, opaque stable system ID */

struct CAMDGenerationV1 {
    ULONG high;
    ULONG low;
};                              /* 8 bytes, compared as an unsigned value */
```

The all-zero handle/ID is invalid. Handles are runtime capabilities, passed to
library calls by address, and are never serialized. Each operation validates
the expected handle kind (snapshot, watch, endpoint session, or private
provider lease) as well as slot and generation. Stable IDs are opaque:
clients compare or persist all 16 bytes but do not parse them. CAMD generates
stable IDs and owns the provider-key mapping; providers can supply identity
evidence but cannot assign or rewrite a global system ID.

All public records begin with `Size` and `Version`. Version 1 records contain
no pointers and use 4-byte fields plus fixed UTF-8 byte arrays, giving the same
offsets on every target. Strings must be NUL-terminated; truncated metadata is
flagged rather than silently presented as complete.

## Endpoint snapshot record

```c
#define CAMD_ENDPOINT_NAME_BYTES       128
#define CAMD_ENDPOINT_PRODUCT_BYTES    128
#define CAMD_ENDPOINT_TRANSPORT_BYTES   32

struct CAMDEndpointInfoV1 {
    ULONG Size;                       /* caller sets; CAMD returns full size */
    ULONG Version;                    /* 1 */
    struct CAMDEndpointIDV1 ID;
    struct CAMDEndpointIDV1 ProviderID;
    ULONG State;                      /* registered/discovering/available/... */
    ULONG Flags;                      /* presence, discovery, directions */
    ULONG IdentityKind;               /* authoritative/configured/path/ephemeral */
    ULONG NativeDataFormat;           /* byte stream or UMP */
    ULONG ProtocolCapabilities;       /* semantic CAMD flags, not raw wire bits */
    ULONG CurrentProtocol;
    struct CAMDGenerationV1 Generation;
    char Name[CAMD_ENDPOINT_NAME_BYTES];
    char ProductInstance[CAMD_ENDPOINT_PRODUCT_BYTES];
    char Transport[CAMD_ENDPOINT_TRANSPORT_BYTES];
};                                    /* 360 bytes */
```

`State` and `Flags` are intentionally separate: transport presence,
discovery completion, protocol readiness, direction and metadata completeness
cannot be collapsed into one enum. Exact flag values remain part of the U01
review, not this draft.

Provider-advertised IDs and physical paths are queryable metadata, not the
stable `ID`. Sensitive or arbitrarily large provider properties are not stored
in this fixed record.

## Group and Function Block records

```c
#define CAMD_TOPOLOGY_NAME_BYTES 64

struct CAMDGroupInfoV1 {
    ULONG Size;
    ULONG Version;
    struct CAMDEndpointIDV1 EndpointID;
    ULONG Group;                      /* 0..15 */
    ULONG Flags;                      /* direction, active, projectable */
    ULONG Protocol;
    char Name[CAMD_TOPOLOGY_NAME_BYTES];
};                                    /* 100 bytes */

struct CAMDFunctionBlockInfoV1 {
    ULONG Size;
    ULONG Version;
    struct CAMDEndpointIDV1 EndpointID;
    ULONG Number;
    ULONG Flags;                      /* direction, active, static/dynamic */
    ULONG FirstGroup;                 /* 0..15 */
    ULONG GroupCount;                 /* validated within 16 Groups */
    char Name[CAMD_TOPOLOGY_NAME_BYTES];
};                                    /* 104 bytes */
```

A Function Block record never serves as a message address. Group-less UMP
messages use the endpoint ID/session itself. Exact MIDI-CI and SysEx8
capability fields may be appended in a later sized version after normative
review; they are not encoded into unverified generic flag bits now.

## Native event record

```c
struct CAMDUMPEventV1 {
    ULONG Size;
    ULONG Version;
    ULONG WordCount;                  /* 1..4, one complete message */
    ULONG Flags;                      /* timestamp validity, scope, loss */
    ULONG Words[4];                   /* host-endian UMP words */
    ULONG TimeHigh;
    ULONG TimeLow;
    ULONG ClockDomain;
};                                    /* 44 bytes */
```

The queue transports complete events only. Message-Type length validation,
reserved types and timestamp/JR semantics stay blocked on gates U02/U09 and
the retained UMP 1.1.2 text. Providers perform transport byte-order
conversion; public words are host-endian, matching the native model used by
ALSA.

## Client interface semantics

The appended library vectors need operations equivalent to:

- acquire the current immutable registry snapshot and its generation;
- enumerate copied Endpoint, Group and Function Block records from it;
- resolve/open by stable endpoint ID, never by list index or display name;
- release the snapshot;
- create/end a bounded endpoint watch and read generation-tagged events;
- open/close an endpoint session with direction, protocol and queue policy;
- send/receive batches of complete `CAMDUMPEventV1` records;
- query session errors/counters and drain/cancel bounded pending work.

Enumeration cursors are scoped to a snapshot and are not reusable after it is
released. Open-by-ID may require a minimum generation; if the endpoint changed,
the call returns a stale-snapshot result rather than silently opening a
different topology.

A watch event contains stable endpoint ID, event type and registry generation.
On queue overflow it returns `lost`; the only supported recovery is acquiring
a fresh snapshot. Watches never expose private endpoint handles.

## Private provider interface semantics

A provider descriptor contains `Size`, `Version`, provider stable identity,
flags, caller context and a sized operations table. Provider callbacks cover:

- open/close an endpoint data path;
- start/stop receive activity;
- send a batch of complete UMP events;
- drain or cancel pending output;
- begin shutdown and report completion.

The private registry provides operations equivalent to:

- register/retire provider;
- publish a new endpoint and obtain a runtime provider lease;
- atomically replace endpoint metadata/topology and state;
- mark available/offline/retiring;
- submit received UMP event batches;
- publish bounded diagnostics and discovery completion/timeout;
- release the endpoint after all provider work has stopped.

CAMD copies publication records before returning. It never retains provider
stack memory. A publish/update transaction validates all Groups and Function
Blocks before advancing the registry generation. A failed transaction changes
nothing.

Provider callbacks execute without registry, endpoint or legacy graph locks.
Their context remains valid until the shutdown callback has completed and the
last lease is released. Callback reentrancy rules and task/interrupt legality
must be explicit in the final header.

## Legacy adapter boundary

The legacy adapter consumes the same immutable snapshots as native clients.
It creates stable cluster projections only for projectable Endpoint + Group +
direction tuples. Collision-safe projection names are registry-owned and
persisted; they are not recomputed from mutable endpoint names. User-facing
aliases remain MIDIHub preference data and do not change endpoint identity.

Offline removes the provider participant, not client links. Topology changes
replace projections atomically with the registry generation. Native
endpoint-wide/group-less traffic has no implicit legacy projection. All
translation loss and unsupported data are counted and queryable.

Legacy fixed-`NPorts` drivers enter through an internal provider adapter. No
existing `MidiDeviceData`, `MidiPortData`, cluster, node, link or library
vector layout changes.

## Error model

Every fallible public vector or private provider operation returns one stable
CAMD endpoint result code;
output records are unchanged on failure. Required result classes are:

- invalid version/size/argument;
- invalid or stale handle/snapshot;
- endpoint offline/retiring/retired;
- identity or projection collision;
- unsupported protocol/message/conversion;
- bounded queue full or event too large;
- discovery incomplete/timeout;
- provider failure/cancelled;
- no memory/resource limit.

Per-session and per-endpoint counters preserve repeated failures without
requiring clients to consume every transient notification.

## Lifetime and lock tests required before freeze

U01 cannot close until tests cover:

1. compile-time offsets/sizes for all pointer-free records on m68k/i386 and
   AArch64/x86-64;
2. shorter/equal/longer `Size` values for every versioned public record;
3. stale handle slot reuse and generation wrap policy;
4. snapshot enumeration racing update/retire without mixed generations;
5. open racing offline and retire;
6. provider shutdown with callbacks, queued events and client sessions active;
7. watch overflow followed by complete resynchronization;
8. topology transaction rollback and Function Block/GTB precedence;
9. unchanged CAMD 41.1/42 compatibility suite and legacy driver operation;
10. no provider callback or notification while registry/graph locks are held;
11. identical lifecycle behavior through the legacy-driver adapter and a
    software provider before provider registration can become public.

## Decisions from the platform review

- Public client operations use appended genmodule vectors, not a returned
  function table.
- The first public release does not expose provider registration. Provider
  callbacks remain private until at least the legacy adapter and software
  provider validate shutdown, reentrancy and ownership.
- CAMD owns stable system IDs and their identity-key mapping. Providers own
  discovery evidence; MIDIHub preferences own user-facing aliases and route
  policy. A generated collision-safe legacy cluster name is CAMD state, not a
  user alias.
- Persistent mappings use a versioned, atomically replaceable CAMD-owned
  database. Its final `ENV:`/`ENVARC:` location is packaging policy and does
  not appear in the public ABI.

These decisions follow the current `workbench/libs/camd/camd.conf`, where the
ordered function list and `.version 42` boundary define the existing ABI, and
the AROS library documentation:
https://developers.aros.org/documentation/sys-dev/libraries.html

## Open upstream decisions

- Exact function names, vector order, m68k register assignments and final
  version number (43 is the proposed boundary).
- Final numeric values and namespace for flags, result codes and feature bits.
- Exact provider callback declarations if and when the private provider API
  becomes public; cross-module register callbacks then require the appropriate
  `AROS_UFP`/`AROS_UFH` declarations and target builds.
- Stable-ID database file location, upgrade/recovery policy and administrative
  tooling.

Until these are reviewed, no header, vector or tag from this document is a
public compatibility promise.
