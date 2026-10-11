# CAMD endpoint ABI draft — gate U01

**Status:** Draft for AROS upstream review; not frozen. The native MIDI 1.0
output slice is implemented as a provisional version 43 (see below).
**Depends on:** [endpoint architecture decision](camd-endpoint-architecture-decision.md).
**Purpose:** Make the ABI questions concrete without assigning public vector,
tag or protocol-bit values prematurely.

The first executable evidence is the
[private endpoint-core model](../prototypes/camd/README.md). It exercises the
records, generations, snapshots, retirement rules and bounded watches on the
host. The same private core is linked into the patched CAMD build, but nothing
in this draft is exported as a public symbol.

## Provisional version 43 slice

`patches/aros-camd-endpoint-client.patch`, `aros-camd-endpoint-input.patch` and
`aros-camd-endpoint-watch.patch` and `aros-camd-endpoint-publish.patch` `aros-camd-endpoint-diagnostics.patch`, `aros-camd-endpoint-time.patch` and
`aros-camd-endpoint-ump.patch` append twenty-seven ordinary vectors behind
`.version 43` and installs `midi/camdendpoint.h`, so that the endpoint core has
a real client before review. Everything in it can still change.

| Function | Registers | Purpose |
|---|---|---|
| `LONG ObtainEndpointSnapshot(snapshot, count)` | A0, A1 | immutable copy of the registry; handle is slot + generation |
| `LONG GetEndpointInfo(snapshot, index, info)` | A0, D0, A1 | copy at most the caller's `Size` bytes of `CAMDEndpointInfoV1` |
| `void ReleaseEndpointSnapshot(snapshot)` | A0 | |
| `LONG OpenEndpointSession(request, session, info)` | A0, A1, A2 | by stable ID; one direction, data format and protocol; no conversion |
| `LONG PutEndpointMidi(session, events, count)` | A0, A1, D0 | atomic batch of `CAMDMIDI1EventV1`; never waits |
| `LONG PutEndpointSysEx(session, bytes, length)` | A0, A1, D0 | one complete copied message; never waits |
| `LONG DrainEndpointSession(session)` | A0 | wait until the queue is handed to the port |
| `LONG GetEndpointMidi(session, event, sysex, size, length)` | A0, A1, A2, D0, A3 | oldest received short message or complete SysEx; never waits |
| `LONG StartEndpointWatch(watch, signal, generation)` | A0, D0, A1 | bounded queue of endpoint changes; signals the calling task |
| `LONG GetEndpointWatchEvent(watch, event)` | A0, A1 | oldest `CAMDEndpointWatchEventV1`; `CAMD_ENDPOINT_EVENT_LOST` after overflow |
| `void EndEndpointWatch(watch)` | A0 | |
| `LONG PublishEndpoint(request, endpoint, id)` | A0, A1, A2 | a program's own endpoint; the name selects the stored ID |
| `void WithdrawEndpoint(endpoint)` | A0 | clients' sessions report retired until they close |
| `LONG SetPublishedEndpointState(endpoint, state)` | A0, D0 | available or offline, same ID |
| `LONG PutPublishedMidi(endpoint, events, count)` | A0, A1, D0 | to every receiving client session |
| `LONG PutPublishedSysEx(endpoint, bytes, length)` | A0, A1, D0 | |
| `LONG GetPublishedMidi(endpoint, event, sysex, size, length)` | A0, A1, A2, D0, A3 | oldest message clients sent, clients served in turn |
| `LONG CancelEndpointSession(session)` | A0 | drop what an output session has not handed over yet |
| `LONG GetEndpointSessionStats(session, stats)` | A0, A1 | records sent, rejected, received and dropped since open |
| `LONG PutEndpointUMP(session, events, count)` | A0, A1, D0 | atomic batch of complete `CAMDUMPEventV1` messages |
| `LONG GetEndpointUMP(session, event)` | A0, A1 | oldest received UMP message |
| `LONG PutPublishedUMP(endpoint, events, count)` | A0, A1, D0 | to every receiving client session |
| `LONG GetPublishedUMP(endpoint, event)` | A0, A1 | oldest UMP message clients sent |
| `LONG SetPublishedEndpointTopology(endpoint, groups, groupcount, blocks, blockcount)` | A0, A1, D0, A2, D1 | replace Groups and Function Blocks as a whole |
| `LONG GetEndpointGroup(snapshot, index, number, info)` | A0, D0, D1, A1 | one `CAMDGroupInfoV1` of an endpoint |
| `LONG GetEndpointFunctionBlock(snapshot, index, number, info)` | A0, D0, D1, A1 | one `CAMDFunctionBlockInfoV1` of an endpoint |
| `LONG CloseEndpointSession(session)` | A0 | |

Results are `enum CAMDRegistryResult` values. The native MIDI 1.0 record for
new sessions, left open below, is provisionally `CAMDMIDI1EventV1` plus a
separate SysEx call. An input session names a signal number in its request;
the opening task gets it when something arrives. A full input queue loses
the newest messages and `GetEndpointMidi()` reports that once with
`CAMD_REGISTRY_QUEUE_FULL`. An event sent to a driver's port with
`CAMD_EVENT_TIME_VALID` is handed over when `CamdTime()` reaches its `TimeLow`;
received events carry the time of their arrival. A published endpoint
names its data format and protocol; UMP ones carry complete `CAMDUMPEventV1`
messages unconverted.

A published endpoint also appears as the legacy clusters `<name>.out.0` (what
is sent there reaches `GetPublishedMidi()`) and `<name>.in.0` (which gets what
`PutPublishedMidi()` and `PutPublishedSysEx()` send), so programs written for
CAMD 41 reach it by linking. A legacy sender is never held up: when the
publisher does not read, the newest messages are lost.

The publishing functions are a deliberately small public provider surface:
one endpoint, native MIDI 1.0, with CAMD owning the queues, the sessions and
the object's lifetime. The private operations table that drivers use is not
exposed, so the section below about keeping the provider API private still
holds for everything else.

### Known limits of the provisional slice

These are deliberate or still open; each is what the code does today.

**Scope**

- Nothing converts between MIDI 1.0 and UMP, in either direction or
  protocol. A session has to ask for exactly what its endpoint carries, or
  it gets `CAMD_REGISTRY_UNSUPPORTED`; there is no conversion policy beyond
  `CAMD_CONVERSION_NONE`.
- UMP exists only for endpoints programs publish. No driver or transport
  provides a UMP endpoint, and a UMP endpoint has no legacy clusters, so
  programs written for CAMD 41 cannot reach it.
- CAMD carries UMP words and does not interpret them. The only check is that
  the word count fits the message type (UMP 1.1 table: 32 bits for types 0-2
  and 6-7, 64 for 3-4 and 8-A, 96 for B-C, 128 for 5 and D-F), reserved types
  included. A MIDI 1.0-protocol endpoint is not kept from carrying MIDI 2.0
  channel voice messages or the reverse; SysEx7/SysEx8 and Mixed Data Set
  sequences are not checked for completeness; Stream, Utility, JR Timestamp
  and Flex Data messages pass like any other and start nothing.
- Groups and Function Blocks are what the publisher declared, checked for
  range and duplicates only. CAMD does not route, filter or project by Group,
  does not discover or negotiate anything, and Function Block direction,
  activity and UI hints have no defined flag values yet.
- Scheduling does not apply to UMP: a published endpoint gets the times as
  its clients gave them.
- The UMP word table was written from memory of the specification and agrees
  with `src/ump.c`; it has not been checked against the specification text
  (gate U02), and nothing was tested against another UMP implementation.
- One clock: `CAMD_CLOCK_CAMD`, which is `CamdTime()` in milliseconds and
  wraps after 49.7 days. A time more than 2^31 ms ahead counts as past.
- Scheduling exists for sessions to a driver's port only. A published
  endpoint gets its clients' times as given and has to keep them itself, and
  legacy links carry no time.
- A session is one queue in order: a message that waits for its time holds
  back the messages queued after it, even overdue ones. Nothing is reordered.
- `PutEndpointSysEx()` takes no time; SysEx is always sent at once, in its
  place in the queue.
- A received message's time is when it reached the session's queue, taken in
  the port's receiver process, not when the hardware saw it.
- Scheduling is as exact as timer.device and the session's worker process,
  which runs at priority 0; nothing compensates for latency.
- Names, registers, record layouts and result codes are not reviewed upstream
  and can change. `enum CAMDRegistryResult` is the internal registry enum
  exposed as it is.
- `midi/camdendpoint.h` stops the compiler when a record does not have its
  documented size. That check passes with the x86-64 and AArch64 AROS
  compilers, with `m68k-aros-gcc`, and with the x86-64 compiler in 32-bit
  mode standing in for i386. The library itself is built and run on hosted
  x86-64 only: it builds for raspi-aarch64, where nothing after driver
  endpoint publication has been run, and it is not built for m68k or i386, so
  the register assignments are untested there.
- A record CAMD fills in (`CAMDEndpointInfoV1`, `CAMDSessionInfoV1`,
  `CAMDSessionStatsV1`, Group and Function Block records) may be shorter or
  longer than CAMD's: at most the caller's `Size` is written and `Size`
  returns as the full size. A record the caller hands in
  (`CAMDSessionRequestV1`, `CAMDPublishRequestV1`, events) must have exactly
  the current size; there is no older-version path yet.

**Behaviour a client has to know**

- A program that exits without closing its sessions, snapshots and watches or
  withdrawing its endpoints keeps camd.library loaded for good. If it named a
  signal, CAMD goes on signalling a task that no longer exists, as CAMD 41
  does for a node left behind. There is no per-task cleanup.
- Nothing waits except `DrainEndpointSession()`, which waits without a
  timeout: for a driver port as long as the port takes, for a published
  endpoint until its publisher has read everything or withdraws.
- Input is lossy by design. A full input-session queue loses the newest
  messages, reported once by `GetEndpointMidi()` and counted in
  `GetEndpointSessionStats()`. A SysEx message longer than the session's
  `MaxSysExBytes` (4096) is dropped whole.
- A legacy link that sends to a published endpoint is never held up: when the
  publisher does not read, the newest messages are lost, and that loss is
  counted only inside CAMD.
- `PutPublishedMidi()` and `PutPublishedSysEx()` run the receivers' work in
  the publisher's task, including legacy receive hooks.
- Open-by-ID has no minimum generation: a client that kept an ID opens
  whatever endpoint has that ID now.
- Do not call the publishing functions while holding `LockCAMD()`; they take
  the cluster lock exclusively, like `AddMidiLinkA()`.

**Fixed sizes**

- A published endpoint has one data format and one protocol for its
  lifetime.
- 32 snapshots, 64 input sessions, 16 watches of 32 events and 32 published
  endpoints in the whole system; 32 sessions per endpoint; at most 64 records
  per session queue; 256 stored identities.

**Identity**

- A driver port's ID follows the driver's file name and port number, a
  published endpoint's follows its name. Renaming either gives a new ID.
- Published names are compared exactly, driver file names without regard to
  ASCII case.
- Damaged identity files with no complete copy give temporary IDs for that
  run and are left untouched; nothing repairs or reports them beyond a line on
  the debug console. Failing DOS calls in the middle of a write are covered
  only by the host model.
- Stored identities are never removed.

**Lifecycle**

- Drivers are never unloaded, so a driver port's endpoint never retires
  before camd.library is expunged.
- A withdrawn endpoint's name cannot be published again until its clients
  have closed their sessions.
- An endpoint offline leaves its legacy clusters; the links of other programs
  stay and wait.

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
- open/close a format-fixed endpoint session, requesting data format and MIDI
  protocol independently;
- send/receive the record family appropriate to that session and
  query/drain/cancel session state.

Exact names, vector numbers and m68k registers remain unassigned until the
prototype header and genmodule output are compiled on the supported targets.
All acquired objects must be released before closing `camd.library`.

The provider API stays private for the first implementation. Its sized
operations table is an internal CAMD contract. The executable prototype now
validates separate MIDI 1.0 and UMP callbacks, exact native-path selection,
input/output capability and format-filtered receive sinks with a software
provider. Provider identity, endpoint ownership and format-fixed data-session
lifetime are now registry-managed. A private fixed-port adapter proves native
MIDI 1.0 forwarding for legacy drivers; its AROS `DriverData` shim and reverse
cluster projection remain to be proven. If later made public for independently
built drivers, it receives its own version gate, callback declarations and ABI
review; it is not smuggled into version 43 through the client surface.

The private bounded-queue primitive is now executable, but is not itself a
public ABI. It allocates all storage before use, fixes one queue to MIDI 1.0 or
UMP, commits multi-record batches atomically, bounds complete SysEx messages
and reports full/oversize rejection with saturating counters. Sessions are
unidirectional. Requested `QueueCapacity` is the minimum number of native
records reserved for that session; successful open returns the effective
capacity and native MIDI 1.0 SysEx limit, both queryable from the registry.
CAMD closes an open whose result is inconsistent. No provider may silently
substitute shared unreserved space or an unbounded queue.

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
    ULONG NativeDataFormats;          /* supported native format bitset */
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

## Native event records

`CAMDUMPEventV1` is the record for native UMP sessions; it is not CAMD's
universal internal representation:

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

The existing CAMD 41/42 `MidiMsg` and SysEx path remains the native record
family for legacy MIDI 1.0 routes. Whether v43 also exposes a new
boundary-preserving MIDI 1.0 event/byte-stream record for new endpoint
sessions is deliberately unresolved at U01. Provider work must prove this
path before any public layout is frozen. A per-event format discriminator and
large payload union are not proposed; session format is fixed at open and
format-specific batch operations keep validation and queue sizing bounded.

## Client interface semantics

The appended library vectors need operations equivalent to:

- acquire the current immutable registry snapshot and its generation;
- enumerate copied Endpoint, Group and Function Block records from it;
- resolve/open by stable endpoint ID, never by list index or display name;
- release the snapshot;
- create/end a bounded endpoint watch and read generation-tagged events;
- open/close a unidirectional endpoint session with requested data format,
  protocol, conversion/loss policy and minimum native-record queue capacity,
  returning the effective format, protocol, reserved capacity and MIDI 1.0
  SysEx limit;
- send/receive format-specific batches; native UMP sessions use complete
  `CAMDUMPEventV1` records, while native MIDI 1.0 sessions use the record
  family selected at U01;
- query session errors/counters and drain/cancel bounded pending work.

The conversion policy must distinguish at least native-only/no-convert,
lossless conversion permitted and lossy conversion permitted. Failure to meet
the requested policy is stable and observable. Data format describes the
container/path (for example MIDI 1.0 byte/message or UMP); protocol describes
the MIDI semantic protocol carried by that path. They are never inferred from
an endpoint name, Group or Function Block.

Enumeration cursors are scoped to a snapshot and are not reusable after it is
released. Open-by-ID may require a minimum generation; if the endpoint changed,
the call returns a stale-snapshot result rather than silently opening a
different topology.

A watch event contains stable endpoint ID, event type and registry generation.
On queue overflow it returns `lost`; the only supported recovery is acquiring
a fresh snapshot. The private model registers a watch and captures its starting
generation under the same registry lock, so mutations cannot fall into a gap
between those operations. A `lost` read discards stale queued events and gives
the latest affected generation. Watches never expose private endpoint handles.

## Private provider interface semantics

A provider descriptor contains `Size`, `Version`, provider stable identity,
declared native data formats, protocol capabilities, flags, caller context and
a sized operations table. Provider callbacks cover:

- open/close an endpoint data path;
- start/stop receive activity;
- send format-specific batches through distinct native MIDI 1.0 and UMP
  callbacks; a provider exposes only the callbacks for formats it declares;
- drain or cancel pending output;
- begin shutdown and report completion.

The private registry provides operations equivalent to:

- register/retire provider;
- publish a new endpoint and obtain a runtime provider lease;
- atomically replace endpoint metadata/topology and state;
- mark available/offline/retiring;
- submit received native MIDI 1.0 or complete UMP batches through the matching
  format-specific entry point;
- publish bounded diagnostics and discovery completion/timeout;
- release the endpoint after all provider work has stopped.

CAMD copies publication records before returning. It never retains provider
stack memory. A publish/update transaction validates all Groups and Function
Blocks before advancing the registry generation. A failed transaction changes
nothing.

The private registry now copies and validates provider descriptors, gives them
generation-safe handles, and requires every endpoint publication to name its
live owner. Provider retirement changes all owned endpoints to retiring in one
registry generation, rejects new acquisitions and keeps provider storage until
the last endpoint or session lease releases. `BeginShutdown`, `ShutdownReady`
and data-path callbacks run outside the registry lock and may reenter snapshot
queries. Format-fixed sessions use independent generation-safe handles and pin
their endpoint and provider. Their receive bridge remains stable across
asynchronous callbacks and is released only after stop plus callback drain.
This proves lifetime, dispatch and capacity negotiation. The software provider
uses the bounded primitive through registry send/drain/cancel operations and
returns explicit queue-full and oversized-message results. Scheduling and the
native AROS driver/runtime integration are not yet complete. The private queue
now also has transactional checkout/commit/release and a bounded-work pump:
downstream callbacks run unlocked, acceptance commits exactly once, and
backpressure leaves the native item queued. These are implementation details,
not additional public v43 operations.
A private bounded-work worker now runs the pump from a host thread or AROS
process/Exec signal, coalesces wakes and synchronously stops after active work.
Its lifecycle and counters likewise remain provider implementation details.

Provider callbacks execute without registry, endpoint or legacy graph locks.
Their context remains valid until the shutdown callback has completed and the
last lease is released. Callback reentrancy rules and task/interrupt legality
must be explicit in the final header.

No provider callback may require a MIDI 1.0-only device to encode UMP merely
to reach a MIDI 1.0 consumer. CAMD inserts a stateful converter only at a
declared incompatible boundary and counts format conversions, protocol
scaling, unrepresentable drops/rejections, malformed input, SysEx overflow and
queue overflow.

The private executable contract currently identifies three exact native
paths: MIDI 1.0 events/bytes, UMP carrying MIDI 1.0 protocol, and UMP carrying
MIDI 2.0 protocol. A provider may declare any combination. This is private
design evidence, not a commitment to those numeric values or to the provisional
32-byte MIDI 1.0 event envelope. The existing public `MidiMsg`/SysEx ABI remains
unchanged, and the exact optional v43 MIDI 1.0 record stays an open U01 choice.

## Legacy adapter boundary

There are two distinct adapter directions. The fixed-port ingress adapter
publishes an existing CAMD driver into the endpoint registry. Existing CAMD
41/42 applications still reach that driver through the original clusters and
MIDI 1.0 queues; endpoint clients use the private MIDI 1.0 provider callbacks.
The executable adapter requires stable IDs as input, rejects UMP, checks each
port's direction and forwards complete native messages without conversion. A
separate host-tested private identity map now derives legacy evidence from the
case-folded `DEVS:Midi` module leaf plus port index and maps it to CAMD-generated
opaque IDs. Direction is excluded from that path-bound key. Persistence failure
keeps the ID usable only as explicitly ephemeral. The compiled private AROS
binding now uses `uuid.library` plus a bounded portable CRC-checked IFF snapshot
with recoverable main/`.new`/`.bak` rotation; this remains private packaging,
not a public ABI promise.
The private AROS `DriverData` state now combines legacy direction presence and
endpoint direction reference counts into one first-open/last-close lifecycle.
The compiled output backend is connected to those endpoint helpers but is not
yet instantiated from loaded drivers.

The reverse projection described below exposes suitable native endpoints to
legacy applications and is a separate component.

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
    software provider before provider registration can become public;
12. MIDI 1.0 producer to MIDI 1.0 consumer routing with a zero conversion
    count and byte/SysEx-equivalent output;
13. native UMP producer to UMP consumer routing without translation;
14. incompatible-format routing converts exactly once at the declared
    boundary and obeys reject/drop/approximate loss policy.

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
- The accepted data plane is path-selective: endpoint control state is shared,
  while native MIDI 1.0 and UMP payload paths remain distinct. Existing CAMD
  41/42 MIDI 1.0 traffic is not required to traverse UMP.

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
- Exact v43 native MIDI 1.0 event/stream record and whether it is exposed in
  the first v43 client surface or initially limited to CAMD 41/42 plus the
  private provider adapter.
- Exact session format negotiation, conversion-policy flags and shared
  timestamp envelope across the MIDI 1.0 and UMP record families.

Until these are reviewed, no header, vector or tag from this document is a
public compatibility promise.
