# ADR: CAMD endpoint registry and provider architecture

**Status:** Accepted as the target architecture; amended for a path-selective
data plane; public C ABI not yet frozen.
**Date:** 2026-10-09.
**Scope:** CAMD endpoint/provider model, native topology and legacy projection.

## Decision

CAMD will use a **central logical endpoint registry with transport-neutral
providers**. The registry is the authoritative system graph. USB, BLE,
AppleMIDI, Network MIDI 2.0 and software providers publish endpoint state and
topology into it.

The registry is a shared **control plane**, not a mandatory message
representation. MIDI 1.0 and UMP use protocol/data-format-specific data paths
with common identity, lifecycle, topology, watches, timing policy and
diagnostics. A MIDI 1.0 source connected to a MIDI 1.0 destination stays on
the existing native CAMD path. Native UMP sessions carry complete UMP events.
Conversion occurs once, explicitly, only where a route, session or legacy
projection crosses incompatible formats or protocols.

CAMD 41.1 clusters remain a supported MIDI 1.0 execution path. They may also
be projections of a native endpoint, but are not merely a temporary UMP
compatibility shim. This path-selective amendment follows the evidence and
rationale in the
[MIDI protocol coexistence research](midi-protocol-coexistence-research.md).

This selects option B from the architecture review. It rejects both making
`MidiDeviceData.NPorts` dynamically resizable and making each provider's
private graph authoritative. It does not freeze function names, vector order,
structure packing or tag values; those remain gate U01 and require a separate
32/64-bit ABI review and upstream coordination.

That option label belongs to the earlier endpoint-ownership comparison. For
protocol coexistence, the amended decision selects alternative C, the
path-selective hybrid, rather than either an all-UMP or duplicated-graph data
plane.

## Alternatives

| Criterion | A. Resize legacy driver ports | B. Central registry + providers | C. Provider-owned graphs |
|---|---|---|---|
| Legacy applications | Direct, but makes clusters the source of truth | Preserved through a dedicated projection | Requires every provider to reproduce projection rules |
| Hot-plug/discovery | Awkward mutation of fixed arrays and port indices | Native state and transactional snapshot updates | Native locally, inconsistent globally |
| UMP topology | Forces Endpoint/Group/Function Block into MIDI 1.0 ports | Represents each concept independently | Possible, but cross-provider enumeration is fragmented |
| Stable identity | Coupled to driver name/port number | Central collision, confidence and alias policy | Providers can disagree or reuse identities |
| Watches/routes | Cluster churn and polling | One generation model with resynchronization | Lost-update and cross-provider ordering problems |
| Memory/complexity | Smallest initial change, largest later migration | Moderate bounded central state | Duplicated indexes, watches and adapters |
| Upstream maintenance | Preserves old internals but extends their limits | One core contract and thin providers | More provider ABI surface and failure modes |

Option A is unsuitable because a native UMP Endpoint is not a resizable list
of MIDI 1.0 ports. Option C is unsuitable because enumeration, open-by-ID,
routing and watches must agree on a single generation. Option B matches the
central-service/provider pattern used by current MIDI systems while allowing
the existing CAMD graph to remain intact for old applications.

## Authoritative objects

The native graph contains these distinct objects:

1. **Provider:** an owner of transport or software endpoints. It has a stable
   provider identity, operations, context and teardown state.
2. **Endpoint:** a logical bidirectional or unidirectional MIDI object with a
   runtime handle, stable system identity, provider identity, native data
   format, protocol capabilities/current protocol and lifecycle state.
3. **Group:** the 4-bit UMP address and direction/capability state. A Group is
   the unit used to derive a legacy MIDI 1.0 port where projection is valid.
4. **Function Block:** metadata describing one or more Groups: identifier,
   direction, active state, first Group, span, name and capabilities. It is
   not a message address and never becomes a port identifier.
5. **Legacy projection:** a derived source or destination cluster for one
   Endpoint + Group + direction. It is not authoritative native topology.

Endpoint-wide and group-less UMP messages remain endpoint-scoped. They are not
duplicated into every Group projection.

## Identity contract

Four identifiers remain separate:

- the **runtime handle**, valid only while references are held;
- the **stable CAMD system ID**, used by saved routes and preferences;
- provider/device-advertised identity such as product instance ID or serial;
- the physical or network connection path.

Every provider reports the origin and confidence of its identity key:
`authoritative`, `configured`, `path-bound` or `ephemeral`. CAMD owns a
versioned persistent mapping from provider namespace plus provider key to the
stable system ID. Providers submit evidence but cannot write that database or
choose a global ID. User-facing aliases and route policy remain MIDIHub
preferences; CAMD persists only identity and collision-safe legacy projection
names required for compatibility.

Moving a path-bound device does not silently take over the old route. A
collision is exposed as a diagnostic and receives a distinct runtime object;
CAMD never resolves it by rebinding an existing stable ID behind clients.

For a new key, CAMD allocates an opaque 128-bit system ID and commits the
mapping atomically before advertising it as persistent. If storage is
unavailable, the endpoint remains usable but is explicitly `ephemeral`; saved
routes must not silently auto-bind it after restart. A simultaneous duplicate
of an otherwise authoritative key receives a temporary distinct identity and
an ambiguity diagnostic until administrative resolution.

User-visible names and transport metadata may change without changing the
stable ID. A provider cannot choose a global system ID directly.

## Lifecycle and discovery

The lifecycle is:

```text
registered -> discovering -> available <-> offline -> retiring -> retired
                    \-----------> offline
```

- `registered` means the provider and initial identity are accepted.
- `discovering` is observable and nonblocking; partial names and topology are
  valid snapshots.
- `available` means the data path is ready for the advertised current
  protocol. Discovery completeness is a separate property, not a barrier.
- `offline` retains stable identity and topology cache but accepts no new I/O.
- `retiring` rejects opens, cancels discovery and drains/cancels bounded I/O.
- `retired` is no longer enumerable; storage is freed after the final lease.

Protocol readiness, transport presence and discovery completeness are separate
properties. Reconnect may return through `discovering` if cached capabilities
must be revalidated. Updates after discovery completion remain legal.

## Snapshot and watch contract

Each accepted endpoint/topology transaction publishes one monotonically
increasing 64-bit registry generation. Enumeration returns copied, sized
records from one snapshot. Open-by-stable-ID resolves against the same
authoritative registry, never against a provider's independent list.

Endpoint watches report `added`, `updated`, `offline` and `retired`, carrying
the stable ID and generation. Events may coalesce. A bounded queue reports
`lost`; after that the consumer must enumerate a fresh snapshot. ClusterWatch
keeps its version 42 meaning and is not extended with endpoint events.

The private executable core now enforces this contract. Watch registration and
its starting generation are atomic under the registry lock. Overflow replaces
the queued history with one `lost` observation at the latest affected
generation; later events resume only after that marker is read. Retirement is
reported when the endpoint stops accepting acquisitions and is omitted from
new snapshots, independently of when its final lease releases the storage.

Runtime handles use a slot plus generation (or an equivalent non-ABA token)
and resolve under the registry lock. Public queries copy data; they do not
return borrowed pointers into registry storage.

## Topology precedence

Providers submit candidate topology with its source. CAMD normalizes it by
these rules:

1. valid discovered Function Blocks are authoritative;
2. otherwise use valid USB Group Terminal Blocks;
3. otherwise use an explicit provider fallback topology;
4. never union overlapping sources or create duplicate projections.

Blank or late Function Block names do not invalidate their topology. Invalid
ranges, overlaps or unsupported directions produce diagnostics and retain the
last valid snapshot or the documented fallback. A topology replacement and
its legacy-projection changes publish atomically in one registry generation.

## Provider contract

A provider registers once, then publishes, updates, takes offline and retires
owned endpoints. Publication includes normalized transport-independent data;
USB descriptors, GATT objects, sockets and discovery packets remain private to
the provider.

Providers declare their native data formats independently of their supported
MIDI protocols. Provider operations cover open/close, format-specific
send/receive, start/stop, drain/cancel and capability/discovery updates. A
session's effective data format is fixed when it opens. MIDI 1.0 providers
must be able to exchange native MIDI 1.0 data without manufacturing UMP;
UMP providers transfer complete 32/64/96/128-bit messages.

Calls into provider code run without registry or legacy-graph locks held.
Reference leases keep provider and endpoint storage alive across calls.
Provider shutdown is two-phase: mark retiring and detach new users, then wait
for bounded in-flight work before release.

Registration and metadata paths may allocate; every real-time data path uses
bounded preallocated queues. Shared timestamp, ordering, cancellation and
overflow semantics apply across formats. Unsupported conversion and any lossy
policy are explicit and observable.

There is exactly one required transport-facing queue, owned by the provider or
by its CAMD adapter; the registry does not impose a redundant second queue.
An existing legacy driver buffer may satisfy this role only when its shim maps
full, oversize, drain and cancel behavior to the common contract. The private
queue model now proves preallocation, format separation, atomic batches,
bounded SysEx, saturating counters and explicit backpressure. It uses a
task-context lock; interrupt ingress still requires a reviewed handoff.
Its transactional single-consumer pump copies a complete head item, calls the
downstream with no queue or registry lock held, and commits only after
acceptance. A full or failing downstream leaves the item queued, which avoids
both loss and duplicate delivery when a legacy driver buffer temporarily
cannot accept more data. Task wakeup and timestamp eligibility remain separate
runtime policies.

Endpoint sessions are unidirectional. Their requested `QueueCapacity` is a
minimum reservation of native records, not a hint and not shared unreserved
space. A successful open reports effective capacity at least as large as the
request and separately reports the native MIDI 1.0 SysEx limit. CAMD rejects
and closes a provider session that returns an inconsistent result. The host software provider now
exercises the real queue through registry sends, drain and cancel.

## Legacy CAMD adapter

The adapter preserves all CAMD 41.1/42 vectors, structures, tags and behavior.
Legacy drivers remain supported by an adapter that publishes their fixed ports
and identity metadata into the native registry without intercepting a
MIDI 1.0-to-MIDI 1.0 data path. They do not define the endpoint control-plane
model, but their native message path remains first-class.

The executable private model now proves the ingress half of this boundary:
caller-supplied stable IDs, fixed-port publication, per-port direction checks,
native MIDI 1.0 event/SysEx forwarding, retryable queue pumping and two-phase
retirement. The remaining AROS shim must provide task/signal wakeup and share
`DriverData` open/reference state with the existing cluster path. It must not
open the same hardware port twice or redirect legacy cluster traffic through
endpoint sessions.

For each projectable Endpoint + Group + direction, the adapter owns a stable
cluster identity independent of mutable display names. While an endpoint is
offline, client links and saved cluster names remain, but the provider-side
participant detaches so `MidiLinkConnected()` and `MIDI_PartSignal` reflect
availability. Reconnect attaches to the same cluster.

MIDI 1.0 byte stream, UMP MIDI 1.0 and MIDI 2.0 translation follows explicit
normative policy. SysEx8 and other unrepresentable data are never silently
truncated. Loss, unsupported messages and overflow have counters. Native and
legacy views of one endpoint have loop/duplication protection.

## Locking and lifetime

The lock order is registry -> endpoint -> legacy graph when an operation
cannot be split. Provider callbacks and notification delivery occur with none
of those locks held. Data queues have their own bounded synchronization and do
not take discovery/metadata locks.

Retirement removes the endpoint from new snapshot generations before waiting
for leases. Existing opens receive an offline/closing indication, queued work
is drained or cancelled by declared policy, and only the final reference frees
the object. This contract applies to concurrent open/remove and provider
failure, not only orderly shutdown.

## Evidence

- Linux creates native UMP endpoints and separate legacy rawmidi projections,
  represents Function Blocks over Group ranges, and keeps group-less messages
  on the endpoint-wide port:
  https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html
- Windows transport plugins publish endpoints into a central service; endpoint
  discovery updates properties asynchronously and legacy ports are derived:
  https://microsoft.github.io/MIDI/kb/service-transport-plugin-development/
  https://microsoft.github.io/MIDI/kb/endpoint-arrival-and-update-ordering/
- Windows uses Function Blocks in preference to Group Terminal Blocks rather
  than merging both sources:
  https://microsoft.github.io/MIDI/kb/porting-midi-libraries/
- Windows explicitly warns that names are not stable identifiers and keeps
  system endpoint IDs distinct from device-declared and transport identity:
  https://microsoft.github.io/MIDI/kb/identifiers/
- CoreMIDI supports driver and non-driver device creation and protocol-aware
  virtual endpoints with persistent unique IDs:
  https://developer.apple.com/documentation/coremidi/mididevicecreate(_:_:_:_:_:)
  https://developer.apple.com/documentation/coremidi/mididestinationcreatewithprotocol(_:_:_:_:_:)
- The normative baseline is UMP/MIDI 2.0 Protocol 1.1.2 and MIDI-CI 1.2.1 in
  the 2025-12-18 Core Specification Collection:
  https://midi.org/midi-2-0-core-specification-collection
- AROS libraries expose ordered function lists through genmodule, which emits
  target calling stubs and register-aware entry points:
  https://developers.aros.org/documentation/sys-dev/libraries.html

## Consequences and next gate

The earlier MIDI-1.0-only endpoint registry sketch is superseded and must not
be implemented. The first code slice may build private registry/provider and
snapshot machinery only if its structures implement this final model and are
not a disposable transition architecture.

Before adding public vectors, gate U01 must produce:

- exact sized C records and opaque handle ownership;
- 32/64-bit alignment and register assignments;
- appended public client vectors plus a private sized provider callback table;
- vector ordering and library version policy;
- compatibility and concurrent-lifecycle tests;
- a format-fixed session contract that separates data format from protocol;
- native MIDI 1.0 and UMP provider paths, with explicit conversion policy;
- upstream review of the ABI sketch.

Private registry/provider work may precede closure of U01 because it creates
no public compatibility promise. Its purpose is to validate the records,
ownership and lifecycle using both the legacy-driver adapter and a software
provider. Provider data callbacks must not be frozen until that private
contract supports native MIDI 1.0 and native UMP separately. The public client
vectors are frozen only after that proof; provider registration remains
private in the first release.

The host-tested
[private endpoint-core model](../prototypes/camd/README.md) begins that proof
with transactional topology, immutable snapshots, lifecycle transitions and
generation-safe leases. Its AROS patch uses Exec allocation and initializes
the core inside `camd.library`. The registry now owns and enforces its Exec
semaphore, while the host model races snapshots and lease operations against
lifecycle changes. Bounded watches and an executable format-specific provider
contract are implemented privately. Providers now register with generation-safe
handles, own compatible published endpoints and retire them transactionally;
shutdown callbacks reenter the registry in tests to prove they run outside its
lock. The separate provider contract proves exact native-path selection,
direction-specific operations and format-filtered receive sinks without
inserting conversion. Registry-owned format-fixed sessions now pin provider
and endpoint lifetime, invoke provider code outside registry locks and drain
asynchronous receive callbacks safely. The fixed-port legacy-driver ingress
adapter now proves MIDI 1.0-only publication and forwarding without UMP.
The AROS `DriverData` shim, scheduling policy, native AROS retirement stress
and the reverse legacy-cluster projection are still required before this
becomes an operational public path.

The current proposal is documented in
[the endpoint ABI draft](camd-endpoint-abi-draft.md). It is a review artifact,
not a public compatibility promise.

Normative packet constants, discovery state machines and translation tables
remain gated on the retained UMP 1.1.2/MIDI-CI 1.2.1 texts and applicable
errata. This ADR approves the object model, not unverified wire semantics.
