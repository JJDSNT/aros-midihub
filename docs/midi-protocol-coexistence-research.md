# MIDI 1.0 and MIDI 2.0/UMP coexistence — architecture research

**Status:** Decision support; no public ABI decision is made by this document.  
**Date:** 2026-10-09.  
**Scope:** Message representation and conversion policy. The accepted central
endpoint registry, stable identity and lifecycle model remain useful regardless
of the message-path decision.

## Executive conclusion

The current design is only partly hybrid. Existing CAMD 41.1/42 applications,
drivers and the current MIDIHub Router still exchange native `MidiMsg` and
SysEx directly. However, the proposed v43 client and provider data paths expose
only `CAMDUMPEventV1` batches. If frozen unchanged, every *new* MIDI 1.0
endpoint would effectively become UMP-centric even when both producer and
consumer are native MIDI 1.0.

The evidence does not establish one universally superior model:

- Linux ALSA preserves legacy Sequencer events and UMP events as distinct
  native records, and converts when a connection crosses MIDI versions.
- Windows MIDI Services deliberately converts all traffic to UMP inside the
  service, including MIDI 1.0 traffic.
- CoreMIDI publicly preserves legacy packet lists and protocol-aware UMP event
  lists, with automatic conversion to the protocol requested by a port or
  destination. Apple does not document its internal canonical representation.
- The MIDI specifications define byte-stream and UMP formats plus translation
  rules. They do not require an operating system to use UMP internally for all
  MIDI 1.0 traffic.

**Recommendation:** use a **path-selective hybrid**. Share endpoint identity,
topology, lifecycle, watches, timestamps, scheduling policy and diagnostics,
but retain protocol/data-format-specific payload paths. A MIDI 1.0 producer
connected to a MIDI 1.0 consumer must remain on the existing native CAMD path
with no encode-to-UMP/decode-from-UMP cycle. Native UMP endpoints and clients
use complete UMP events. Insert one explicit conversion stage only when the two
ends require different representations or protocols.

This recommendation is narrower than a fully dual-native implementation: it
does not duplicate endpoint graphs, discovery, routing policy or scheduling.
It is also less UMP-centric than Windows: UMP is the native representation for
UMP transports and v43 UMP sessions, not a mandatory intermediate format for
all MIDI 1.0 traffic.

## Evidence discipline

“Established facts” below are statements made by the platform owner, its
official project documentation, or the MIDI Association. “Architectural
implication”, “interpretation” and the recommendation are conclusions for
AROS. Where a platform does not document an internal detail, this document
says so instead of inferring it from API shape.

Public MIDI Association pages establish the high-level format and translation
model, but do not replace the retained normative M2-104-UM 1.1.2 text for
packet constants, exact translation algorithms or conformance. Those details
remain behind the repository's U02/U06/U08/U09 gates.

## Terminology that affects the decision

The following are different concepts:

- **MIDI 1.0 byte-stream data format:** status/data bytes, running status and
  F0/F7-delimited SysEx.
- **UMP data format:** complete 32/64/96/128-bit packets with a Message Type and,
  where applicable, Group.
- **MIDI 1.0 protocol:** MIDI 1.0 musical messages. These can be carried in a
  byte stream or as UMP MIDI 1.0 messages.
- **MIDI 2.0 protocol:** higher-resolution and additional messages carried in
  UMP.

The MIDI Association explicitly says UMP can contain both MIDI 1.0 and MIDI
2.0 protocol messages, and treats translation compatibility as an important
design goal. That is a format capability, not a requirement that every system
internally convert MIDI 1.0 byte traffic to UMP
([UMP and MIDI 2.0 Protocol 1.1.2 landing page](https://midi.org/universal-midi-packet-ump-and-midi-2-0-protocol-specification),
[MIDI 2.0 overview](https://midi.org/details-about-midi-2-0-midi-ci-profiles-and-property-exchange-updated-june-2023)).

## Comparative architecture matrix

| System | Documented message model | MIDI 1.0 → MIDI 1.0 path | Conversion boundary | Legacy view of UMP/MIDI 2.0 | Classification for this study |
|---|---|---|---|---|---|
| Linux ALSA RawMIDI | Separate legacy `/dev/snd/midi*` byte streams and `/dev/snd/ump*` packet devices; optional legacy substreams for a UMP endpoint | Native legacy RawMIDI remains available; no UMP is required | UMP driver/projection boundary | Optional legacy RawMIDI substream per Group | Dual at raw transport boundary |
| Linux ALSA Sequencer | Legacy `snd_seq_event_t` and UMP-capable extended events coexist; clients declare legacy, UMP MIDI 1.0 or UMP MIDI 2.0 | Matching legacy clients retain legacy events | Sequencer converts when connected clients have different MIDI versions; conversion can be suppressed | UMP Endpoint port plus Group ports; legacy clients receive translated legacy events | Path-selective hybrid |
| Windows MIDI Services | Every message inside the service and modern API is UMP, including traffic for byte-stream MIDI 1.0 devices | Under the new service, byte format is translated to UMP and back at required API/device boundaries | Driver, service transport or legacy API transform | MIDI 1.0 ports are derived children of one parent UMP endpoint | UMP-centric |
| Apple CoreMIDI | Public legacy `MIDIPacketList` API coexists with protocol-tagged `MIDIEventList`; endpoints/ports declare a protocol | Delivery to legacy APIs/endpoints remains byte-packet based; internal representation is undocumented | CoreMIDI automatically converts to the receiving port/destination protocol | Same endpoint model is consumed through protocol-specific ports/callbacks | Public hybrid; internals unknown |
| MIDI specifications | Defines two data formats and two protocol generations; UMP carries MIDI 1.0 or MIDI 2.0 protocol | Permitted and still normative | A translator is required only where formats/protocols differ | Group roughly maps to one MIDI 1.0 connection where translation is valid | No mandated OS architecture |
| Current AROS CAMD 41/42 | `MidiMsg` plus a separate SysEx ring and cluster graph | Direct native path | No UMP conversion in CAMD or current Router | None yet | MIDI 1.0-native |
| Proposed AROS v43 draft | Shared endpoint graph, but client/provider event operations are UMP-only | Existing 41/42 path remains direct; new endpoint path is unclear and tends toward mandatory UMP | Intended legacy projection adapter | Cluster per projectable Endpoint + Group + direction | Mixed architecture with an unresolved UMP-centric new path |

## Findings from actual systems

### Linux ALSA

#### Established facts

The Linux kernel exposes UMP and legacy RawMIDI under different device names so
legacy applications do not accidentally open packet data as a byte stream.
With `CONFIG_SND_UMP_LEGACY_RAWMIDI`, it additionally creates a legacy RawMIDI
device with one substream per UMP Group. Endpoint and Function Block metadata
remain associated with the UMP endpoint
([Linux MIDI 2.0 design, RawMIDI section](https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html#rawmidi-device-with-usb-midi-2-0)).

The ALSA Sequencer extends rather than replaces its event representation.
Legacy events retain the existing record and payload; UMP events set
`SNDRV_SEQ_EVENT_UMP` and use an extended 16-byte payload. Clients declare
legacy, UMP MIDI 1.0 or UMP MIDI 2.0. The Sequencer converts when connected
clients use different MIDI versions, and provides `SNDRV_SEQ_FILTER_NO_CONVERT`
to suppress conversion
([kernel design](https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html#alsa-sequencer-with-usb-midi-2-0),
[ALSA Sequencer API](https://alsa-project.org/alsa-doc/alsa-lib/seq.html#seq_midi2)).

UMP endpoints have an endpoint-wide port and Group ports. Group-less messages
go only to the endpoint port, avoiding duplicate delivery across Group views.
Function Blocks map Group ranges and can dynamically update port properties.
The UMP and legacy device records are explicitly tied to each other, which
provides a single topology relationship rather than independent identities
([kernel design](https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html)).

Sequencer timestamps, source/destination addresses and scheduling fields are
shared around either payload form; the UMP record keeps those fields and
extends the payload. Legacy SysEx remains a variable-length Sequencer event,
while UMP carries packetized SysEx7
([Sequencer event model](https://alsa-project.org/alsa-doc/alsa-lib/seq.html#seq_events),
[UMP extension](https://alsa-project.org/alsa-doc/alsa-lib/seq.html#seq_midi2)).

#### Architectural implication

ALSA is the clearest precedent for AROS constraints. It preserves an old ABI
and native legacy traffic while sharing routing/scheduling infrastructure and
performing conversion only across incompatible client versions. It does not
require a legacy-to-legacy route to round-trip through UMP.

### Windows MIDI Services

#### Established facts

Microsoft documents an intentionally UMP-centric service: every message inside
the service is UMP, even when it originated in a MIDI 1.0 byte-stream device or
classic API. Translation to/from byte format occurs in the new driver, the
service, or a legacy API boundary depending on the device/driver combination
([data translation](https://microsoft.github.io/MIDI/kb/data-translation/),
[implementation details](https://microsoft.github.io/MIDI/kb/midi2-implementation-details/)).

Windows does not automatically promote incoming MIDI 1.0 protocol messages to
MIDI 2.0 protocol merely because they are in UMP. The modern API returns the
UMP protocol it received. It downscales MIDI 2.0 when the negotiated endpoint
requires MIDI 1.0, while first-release upscaling is deliberately limited.
Applications are expected to inspect endpoint/Function Block protocol
information and send the correct Message Type
([data translation](https://microsoft.github.io/MIDI/kb/data-translation/)).

Windows creates a UMP endpoint even for a native MIDI 1.0 device. Groups map to
the device's pins/cables, and derived WinMM/WinRT ports have that UMP endpoint
as their parent. This parent relationship prevents the modern endpoint and its
legacy ports from becoming unrelated identities
([WinMM migration mapping](https://microsoft.github.io/MIDI/kb/moving-from-winmm-to-wms/),
[legacy/new API integration](https://microsoft.github.io/MIDI/kb/integrating-midi1-and-midi2-apis/)).

Legacy port creation can be asynchronous and optional for transports. Microsoft
recommends deterministic configuration when a transport exposes both UMP and
legacy ports
([transport plugin guidance](https://microsoft.github.io/MIDI/kb/service-transport-plugin-development/)).

The service uses a shared timestamp-aware transform for scheduling. Windows
currently tells applications not to supply JR timestamps; scheduling time and
JR messages are separate concerns
([Windows MIDI Services overview](https://microsoft.github.io/MIDI/overview/),
[implementation details](https://microsoft.github.io/MIDI/kb/midi2-implementation-details/)).

#### Architectural implication

Windows proves that a UMP-centric core is viable and simplifies scheduling and
processing. It does **not** prove that it is free: a classic MIDI 1.0 API talking
to a byte-stream MIDI 1.0 device still crosses format translators inside the
new service. Windows accepts that cost to obtain one service format and a new
cross-process architecture. AROS already has a small, working in-process CAMD
legacy graph, so the same trade-off need not be copied.

### Apple CoreMIDI

#### Established facts

CoreMIDI retains the legacy `MIDIPacketList` API and adds UMP-based
`MIDIEventList`, whose list declares `kMIDIProtocol_1_0` or
`kMIDIProtocol_2_0`. Protocol-aware input ports and virtual destinations ask
CoreMIDI to deliver a selected protocol, and Apple documents automatic
conversion as needed. Legacy destinations receive `MIDIPacketList`; MIDI 2
destinations receive `MIDIEventList`
([Apple MIDI 2 sample](https://developer.apple.com/documentation/coremidi/incorporating-midi-2-into-your-apps),
[`MIDIInputPortCreateWithProtocol`](https://developer.apple.com/documentation/coremidi/midiinputportcreatewithprotocol(_:_:_:_:_:)),
[`MIDIDestinationCreateWithProtocol`](https://developer.apple.com/documentation/coremidi/mididestinationcreatewithprotocol(_:_:_:_:_:))).

Both packet-list families carry `MIDITimeStamp`; CoreMIDI schedules future
timestamps and performs needed merging
([`MIDISendEventList`](https://developer.apple.com/documentation/coremidi/midisendeventlist(_:_:_:)),
[`MIDIPacketList`](https://developer.apple.com/documentation/coremidi/midipacketlist)).

An endpoint has a native protocol property. Drivers can change it after
protocol negotiation, and clients can observe the property change
([`kMIDIPropertyProtocolID`](https://developer.apple.com/documentation/coremidi/kmidipropertyprotocolid)).

#### Documented limit

Apple documents observable API behavior, not whether CoreMIDI uses byte
messages, UMP, or another representation internally. Therefore CoreMIDI is
evidence for protocol-aware endpoints, two public record families and automatic
boundary conversion. It is **not** evidence that a unified UMP core or a
dual-native core is used internally. The public material reviewed here also
does not specify a conversion-loss reporting contract comparable to the one
AROS needs before freezing v43.

### MIDI Association specifications

#### Established facts

UMP is a common packet container for MIDI 1.0 and MIDI 2.0 protocol messages.
One Group is approximately one MIDI 1.0 connection for translation purposes;
some UMP messages are group-less and endpoint-scoped. MIDI 1.0 is not replaced
by MIDI 2.0
([MIDI 2.0 overview](https://midi.org/details-about-midi-2-0-midi-ci-profiles-and-property-exchange-updated-june-2023),
[specifications index](https://midi.org/specs)).

Translation is deterministic only where an equivalent exists. Scaling high
resolution values down necessarily loses precision. MIDI 2.0 adds per-note
controllers, per-note pitch, relative registered/assignable controllers,
attributes, SysEx8 and other data with no general faithful MIDI 1.0 form. The
Association explicitly notes, for example, that relative registered and
assignable controllers cannot be translated to MIDI 1.0
([relative controller guidance](https://midi.org/midi-2-0-protocol-using-relative-registered-assignable-controls),
[MIDI 2.0 state/translation summary](https://midi.org/the-state-of-midi-2-0-high-resolution-performance-and-the-rise-of-profiles-update-feb-2026)).

Current UMP 1.1 protocol selection uses Endpoint/Stream Configuration
mechanisms; the older MIDI-CI Protocol Negotiation mechanism is deprecated
([MIDI 2.0 update summary](https://midi.org/details-about-midi-2-0-midi-ci-profiles-and-property-exchange-updated-june-2023)).

#### Architectural implication

The specification supports either a UMP-centric service or a path-selective
system. It requires correct translation behavior at an actual boundary; it
does not justify silently translating traffic when both ends already share a
native representation.

## Assessment of the current AROS design

### Established repository facts

1. CAMD 41/42 uses `MidiMsg` for short messages and a separate bounded SysEx
   ring. Its legacy application and driver ABI is explicitly preserved.
2. `MIDIHubRouter` currently receives with `GetMidi()`, forwards short messages
   with `PutMidi()`, and copies SysEx with `GetSysEx()`/`PutSysEx()`. A current
   MIDI 1.0 route therefore performs no UMP conversion.
3. `src/ump.c` is a portable boundary converter. It maintains byte-stream
   running-status/SysEx state, packetizes MIDI 1.0 into UMP, and downconverts
   supported UMP messages. It currently drops several unrepresentable MIDI 2.0
   classes without returning a detailed loss result; that is acceptable for an
   experimental codec but insufficient as a public CAMD conversion contract.
4. The endpoint ADR correctly separates stable Endpoint, Group, Function Block
   and legacy projection objects, and protects group-less messages from being
   duplicated into every Group projection.
5. The ABI draft records `NativeDataFormat`, protocol capabilities and current
   protocol, but its only new event record is `CAMDUMPEventV1`. Client and
   provider operations send/receive only complete UMP events.
6. The integration plan calls CAMD the owner of “native UMP event transport”
   and tests legacy coexistence, native UMP and a legacy bridge, but it has no
   explicit acceptance test proving a zero-conversion MIDI 1.0 route.

### Interpretation

The accepted endpoint **object model is not the problem**. A central registry,
one stable identity, Groups/Function Blocks where applicable, lifecycle and
watches are compatible with every architecture evaluated.

The unresolved issue is the **event/provider contract**. Existing legacy
traffic remains first-class only outside the new endpoint/session API. New
MIDI 1.0 transports published through the provider model would have to produce
UMP, because the provider callback contract names only UMP batches. Calling a
cluster a “legacy projection” also risks treating every MIDI 1.0 device as if
its native path were merely a view of a UMP source.

Thus the draft preserves old binaries but does not yet guarantee MIDI 1.0 as a
first-class representation for new providers, new clients or Router evolution.

## Alternatives for AROS

### A. UMP-centric core

All providers normalize into UMP. Legacy drivers and APIs translate at the
service boundary. MIDI 1.0 protocol travels as UMP Message Types 1/2/3.

**Advantages**

- One queue record, scheduler, router payload and transform surface.
- Natural representation for Groups, endpoint-scoped messages, SysEx7/8 and
  future UMP transports.
- Closely follows Windows MIDI Services.

**Disadvantages and risks**

- Mandatory encode/decode and state machines for byte-stream MIDI 1.0 even on
  MIDI 1.0-only routes.
- Running status and SysEx fragmentation become adapter state; failures and
  reconnects can alter behavior that CAMD currently passes directly.
- A latent converter defect can regress every legacy route.
- Additional queue footprint and CPU work matter more on classic/embedded AROS
  targets than on the Windows service architecture.
- Tempts the implementation to treat MIDI 1.0 protocol in UMP as “equivalent”
  to the existing CAMD message/SysEx contract when their framing is different.

**Assessment:** viable, but not justified as the mandatory AROS path without
measurements and a stronger compatibility reason.

### B. Fully dual-native cores

Maintain separate MIDI 1.0 and UMP graphs, queues, scheduling and routing
engines, joining them only through explicit bridge objects.

**Advantages**

- Maximum isolation and no conversion for same-protocol traffic.
- Each engine can optimize its native framing independently.

**Disadvantages and risks**

- Duplicate identity, lifecycle, watches, route state and reconnect logic.
- Cross-core ordering and timestamp consistency become difficult.
- Applications can see two unrelated objects for one device.
- Every transport and Router feature tends to be implemented twice.

**Assessment:** rejects the strongest part of the accepted central-registry
decision and creates more maintenance risk than value.

### C. Path-selective hybrid — recommended

Use one endpoint registry and shared control plane, but let an opened session
or legacy link use a fixed native data format. The payload path is MIDI 1.0 or
UMP; it is not a per-message untagged union. A converter is inserted once when
source and destination/session formats or protocols differ.

**Advantages**

- Zero conversion for CAMD MIDI 1.0 → MIDI 1.0 and UMP → UMP.
- One identity, lifecycle, topology, watch and reconnect model.
- Conversion cost and loss are attributable to a specific route/session.
- Closely matches ALSA's proven compatibility pattern while retaining the
  central endpoint model seen in Windows.
- Allows gradual adoption: old Router routes remain direct; new Router routes
  can select native or converted paths.

**Disadvantages and risks**

- Two payload record families and two provider capabilities must be tested.
- Shared ordering, timestamp and queue semantics must be specified once and
  applied consistently to both paths.
- Mixed routes require stateful conversion objects, especially for byte
  streams, RPN/NRPN expansion and SysEx.
- A provider that supports both formats needs deterministic selection rules.

**Assessment:** best fit for AROS because it preserves the working CAMD ABI and
runtime while avoiding duplicate control planes.

### D. Separate `camd2.library`

A new UMP-only library could bridge to CAMD 41/42.

**Assessment:** not recommended. It makes endpoint identity, enumeration,
reconnect and routing a cross-library synchronization problem and duplicates
the system graph. The current additive-library-vector direction is stronger.

## Recommended architecture

### Shared control plane

Keep one authoritative registry for:

- stable provider and endpoint identity;
- transport presence, discovery, capabilities and current protocol;
- Group and Function Block topology where it exists;
- snapshots, watches, leases and retirement;
- clock-domain metadata, queue policy and diagnostics;
- the relationship between a native endpoint and its compatibility views.

MIDI 1.0-only endpoints need not invent Function Blocks. They may publish
simple port/direction topology that maps to one or more Groups only when a UMP
view is requested.

### Protocol-specific data plane

Use fixed-format sessions/paths:

1. **Legacy CAMD path:** existing `MidiMsg` and SysEx semantics, unchanged.
2. **Native MIDI 1.0 endpoint path:** boundary-preserving MIDI 1.0 events or
   byte streams for new providers that are natively MIDI 1.0.
3. **Native UMP path:** complete 32/64/96/128-bit UMP events with the existing
   proposed timestamp envelope.
4. **Conversion path:** an explicit stateful transform owned by the
   route/session/projection boundary, never hidden inside the registry.

The exact new MIDI 1.0 record is an open U01 decision. It should not be solved
by placing a format discriminator and a large union in every event. Prefer a
session whose effective format is fixed at open time and separate batch
operations/record layouts. This keeps real-time validation and queue sizing
simple and prevents accidental mixed-format ordering.

### Conversion rules

- If source and destination native formats/protocols match, do not convert.
- MIDI 1.0 byte/message → UMP MIDI 1.0 occurs at the UMP destination boundary,
  with one explicit Group mapping.
- UMP MIDI 1.0 → MIDI 1.0 occurs at the legacy destination boundary.
- MIDI 1.0 ↔ MIDI 2.0 protocol translation occurs only when requested and
  supported by endpoint/session policy.
- Never upscale MIDI 1.0 protocol merely because its container is UMP.
- Never silently downscale an unsupported MIDI 2.0 message. Apply a declared
  reject/drop/approximate policy and increment inspectable counters.
- SysEx7 can bridge with bounded reassembly/fragmentation. SysEx8 and Mixed Data
  Set require an explicit unsupported or application-defined policy; they are
  not truncated into MIDI 1.0 SysEx.
- Preserve source-specific converter state. Do not share running status,
  RPN/NRPN expansion or partial SysEx state across producers.
- Expanded translations that produce several MIDI 1.0 messages must be atomic
  relative to other producers or carry an explicit interleaving rule.

### Identity and projections

Represent one physical/logical endpoint once. Legacy clusters and UMP Group
ports are **views with parent identity**, not separately rediscovered
endpoints. Persist routes against the stable endpoint ID plus view attributes
(direction, Group, requested format), not mutable display names.

The registry should publish projection/view relationships so discovery tools
can deduplicate them. Group-less UMP traffic remains endpoint-scoped and is
never echoed through every legacy cluster. A loop-prevention token or internal
origin identity must stop a translated event from returning through the other
view of the same endpoint.

## Implications for the proposed CAMD v43 ABI

Do not freeze the current data-path portion of the ABI draft unchanged.

1. Keep `CAMDUMPEventV1`; it is appropriate for native UMP sessions.
2. Define whether v43 exposes a new native MIDI 1.0 session record or formally
   relies on CAMD 41/42 nodes/links for all native MIDI 1.0 access. New providers
   need a non-UMP native option if MIDI 1.0 is to remain first-class.
3. Make session open negotiate or request **data format** separately from
   **protocol**. Return effective values; do not infer them from the endpoint
   name, Group or Function Block alone.
4. Add policy flags equivalent to `native-only`/`no-convert`, `allow-lossless`
   and `allow-lossy`, with a stable failure when the request cannot be met.
5. Use separate batch vectors or a format-fixed session rather than a public
   per-event payload union. Exact names and layouts remain U01 work.
6. Extend the private provider descriptor with declared native data formats
   and format-specific callbacks. Do not require a byte-stream provider to
   manufacture UMP before CAMD can route it to another byte-stream consumer.
7. Define shared timestamp, ordering, cancellation and queue semantics across
   both record families. A timestamp belongs to the CAMD event envelope, not
   solely to JR Timestamp UMP messages.
8. Add per-session/endpoint counters for format conversions, protocol scaling,
   unrepresentable drops/rejections, malformed input, SysEx overflow and queue
   overflow.
9. Make legacy projection creation a view policy, not evidence that the
   underlying endpoint is UMP-native.
10. Keep all existing 41/42 structures and vectors unchanged. Their direct path
    is a supported architecture component, not a temporary shim scheduled for
    removal.

## Router implications

The current Router is correctly MIDI 1.0-native. Its `GetMidi` → `PutMidi` and
`GetSysEx` → `PutSysEx` paths should remain the implementation for a route whose
ends are legacy/native MIDI 1.0.

A future endpoint-aware Router should plan a route once:

```text
source native format
        -> zero or one required converter
        -> optional format-compatible processors
        -> destination native format
```

It must not bounce a route through both the native endpoint and its legacy
projection, and it must not run MIDI 1.0 traffic through UMP merely because
the Router itself understands UMP. Processor chains should declare accepted
input/output formats; inserting a processor may legitimately introduce a
conversion that the unprocessed route did not need, and diagnostics should say
so.

## Proposed acceptance tests

### Mandatory zero-conversion tests

1. **Legacy short messages:** unmodified CAMD sender → Router → legacy CAMD
   receiver. Verify byte/message identity, ordering and timestamps, and assert
   converter invocation counters remain zero.
2. **Legacy SysEx:** send boundary sizes, multi-kilobyte SysEx, real-time
   interleaving where supported, and back-to-back messages. Verify exact bytes
   and zero UMP packetization calls.
3. **New MIDI 1.0 provider:** native MIDI 1.0 provider → native MIDI 1.0 v43
   session/consumer. Verify the negotiated effective format is MIDI 1.0 and no
   UMP conversion occurs.
4. **UMP MIDI 1.0:** UMP MIDI 1.0 source → UMP MIDI 1.0 destination. Verify
   exact UMP words and no promotion to MIDI 2.0 Channel Voice.

### Boundary-conversion tests

5. MIDI 1.0 → UMP MIDI 1.0 converts exactly once, applies the selected Group,
   preserves order/time, and packetizes SysEx7 correctly.
6. UMP MIDI 1.0 → MIDI 1.0 converts exactly once and preserves all faithfully
   representable data.
7. MIDI 1.0 ↔ MIDI 2.0 tests normative min/center/max scaling, velocity-zero,
   CC, RPN/NRPN, bank/program and pitch-bend rules.
8. Per-note controllers, per-note pitch, attributes, relative controllers,
   SysEx8, Mixed Data Set, Flex Data and group-less messages exercise each
   declared reject/drop/approximation policy. No case may report lossless
   success after discarding information.
9. Expanded MIDI 2.0 → MIDI 1.0 sequences remain atomic against a concurrent
   producer, or demonstrate the documented interleaving policy.

### Topology, identity and lifecycle tests

10. Enumeration reports one stable endpoint with linked native/legacy views,
    not duplicate independent endpoints.
11. Saved routes survive offline/reconnect and mutable name changes without
    rebinding to another identity.
12. Group views map once; group-less UMP messages appear only on the endpoint
    path and are not duplicated to 16 clusters.
13. Routing between a native view and compatibility view of the same endpoint
    cannot loop or duplicate delivery.
14. Protocol/format changes while sessions are open follow an explicit
    drain/cancel/reopen rule and never silently reinterpret queued payloads.
15. Disconnect during partial byte-stream SysEx or UMP SysEx7 resets only the
    affected converter state.

### Performance and compatibility tests

16. Compare CPU time, allocations, latency and queue footprint for direct
    MIDI 1.0 routing versus a forced UMP round trip on m68k and hosted builds.
17. Run the complete CAMD 41.1/42 compatibility suite with v43 installed and
    converter counters enabled; existing same-format tests must remain zero.
18. Stress multiple producers, queue overflow, teardown and retirement in all
    four combinations: MIDI1→MIDI1, MIDI1→UMP, UMP→MIDI1 and UMP→UMP.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Two payload APIs diverge | Share lifecycle, timestamp, queue result codes and conformance tests; keep payload codecs separate |
| Converter state leaks across producers | Allocate conversion context per source/session/route |
| Duplicate endpoints/views | One stable parent endpoint ID with explicit view metadata |
| Silent fidelity loss | Policy flags, failure results and monotonic counters |
| Extra code on small AROS targets | Keep conversion optional and link/provider scoped; direct legacy path remains minimal |
| Router loops through projections | Preserve internal origin identity and reject self-view cycles |
| Format changes reorder queued events | Format fixed for session lifetime; drain/cancel before reopening |
| Public ABI overfits current codecs | Freeze semantics and sized records only after tests; keep provider API private initially |

## Open architectural decisions

1. What is the v43 native MIDI 1.0 record: complete logical messages plus a
   separate bounded SysEx payload, or a byte-stream batch with parser state?
2. Can v43 clients request MIDI 1.0 format from UMP-native endpoints, or must
   they use legacy clusters for that view?
3. Are conversion permissions session flags, route policy, endpoint policy, or
   a combination with a strict precedence order?
4. Which lossy conversions are reject-by-default, and which may be enabled as
   explicit approximations?
5. Who owns stateful RPN/NRPN, bank/program and partial SysEx translation when
   several producers share one destination?
6. What atomicity guarantee applies when one source event expands to several
   destination messages?
7. How are CAMD monotonic timestamps mapped across legacy events, native UMP
   events, transport clocks and optional JR messages?
8. Does a MIDI 1.0-only endpoint publish synthetic Group metadata eagerly, or
   only when a UMP/legacy projection is requested?
9. How are view IDs represented without exposing mutable cluster names as
   persistent identity?
10. Can providers offer both native formats, and how is the preferred format
    selected without changing during an active session?
11. What diagnostics are public per session, per endpoint and per route?
12. Which normative translation vectors and licensed specification fixtures
    are available for closing U02/U06/U08/U09?

## Decision gate

Before U01 freezes v43, the project should explicitly accept or reject the
path-selective hybrid recommendation and answer open questions 1–6. At minimum,
the ABI must guarantee:

- direct MIDI 1.0 → MIDI 1.0 operation without mandatory UMP conversion;
- lossless native UMP → UMP operation;
- conversion only at a declared boundary;
- observable loss and unsupported data;
- one endpoint identity across native and compatibility views.

Until that decision is recorded, the endpoint registry may continue as private
control-plane work, but provider data callbacks, public event vectors and
Router UMP integration should not be frozen.

