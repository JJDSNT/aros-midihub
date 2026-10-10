# CAMD private endpoint/provider executable model

**Status:** Host-tested design evidence and source for
`patches/aros-camd-endpoint-core.patch`. The patch initializes the registry
inside `camd.library` and compiles the private provider contract, but nothing
consumes either data path and no public ABI is exposed.

This directory converts the accepted endpoint architecture into C before any
version 43 vectors are frozen. The same source selects `AllocVec`/`FreeVec`
when built for AROS, while the host build uses the C allocator. The opaque
registry owns an Exec semaphore on AROS and a pthread mutex in the host model;
it is not a MIDI 1.0 dynamic-port implementation.

`provider_contract.c` separately defines the path-selective data-plane shape.
Provider registration, endpoint ownership and format-fixed data sessions are
now coupled to the registry.

`legacy_driver_adapter.c` is the private fixed-port ingress adapter. It
publishes legacy driver ports as native MIDI 1.0 endpoints for new clients,
while existing CAMD 41/42 applications continue to use their original cluster
and `DriverData` path without passing through the adapter.

`native_event_queue.c` is the bounded provider-queue primitive. Queue storage
is fully allocated at creation, is fixed to MIDI 1.0 or UMP, and counts native
records rather than bytes or converted packets.

The current slice proves:

- the proposed pointer-free record sizes on the host compiler;
- slot-plus-generation handles and permanent quarantine on generation wrap;
- duplicate stable-ID rejection;
- validated atomic Endpoint/Group/Function Block publication and replacement;
- immutable copied snapshots with one registry generation;
- explicit lifecycle transitions;
- retirement that rejects new acquisitions and waits for outstanding leases;
- stale-handle rejection after slot reuse;
- internal serialization at every registry entry point, exercised with
  concurrent snapshots, lease churn and lifecycle state changes;
- bounded endpoint watches with generation-tagged `added`, `updated`,
  `offline` and `retired` events;
- deterministic overflow: queued stale events collapse into one `lost` marker,
  after which the consumer acquires a fresh snapshot;
- retirement disappears from new snapshots immediately while storage remains
  alive until the final lease is released;
- providers declare exact native paths independently: MIDI 1.0 bytes/events,
  UMP carrying MIDI 1.0 protocol, and UMP carrying MIDI 2.0 protocol;
- input-only and output-only providers require only direction-relevant
  callbacks;
- each session fixes one data format and protocol at open time; unsupported
  combinations fail before a provider callback runs;
- native MIDI 1.0 short messages and complete SysEx use MIDI 1.0 callbacks,
  while complete UMP events use a separate callback, with no implicit
  conversion or cross-format dispatch;
- receive sinks expose only callbacks matching the session format;
- generation-safe sessions pin both endpoint and provider storage, and all
  open/send/receive/drain/cancel/close provider callbacks run outside the
  registry lock;
- asynchronous receive uses a stable registry bridge that filters by session
  format and keeps callback storage alive until stop and in-flight delivery
  have both completed;
- provider retirement rejects new opens and sends while allowing explicit
  drain, cancel, receive stop and close before final release;
- endpoint publication requires a live owning provider with a matching stable
  provider ID and compatible declared native formats;
- provider handles use independent slot generations and reject stale reuse;
- provider retirement atomically removes all owned endpoints from snapshots,
  rejects new acquisitions, and waits for their final leases;
- `BeginShutdown` and `ShutdownReady` execute without the registry lock held,
  proven by callbacks that reenter snapshot enumeration.
- fixed legacy ports publish as MIDI 1.0-only endpoints using caller-supplied
  stable IDs, enforce direction per port and forward exact MIDI 1.0 events and
  complete SysEx without manufacturing UMP;
- adapter retirement is two-phase and remains retryable while sessions pin a
  legacy endpoint; direct legacy cluster traffic remains independent.
- format-fixed preallocated queues preserve complete MIDI 1.0 events, SysEx
  messages and 32/64/96/128-bit UMP records without conversion;
- multi-record enqueue is atomic, full and oversized operations change no
  queue content, and explicit saturating counters retain accepted, dequeued,
  cancelled, full-rejection and oversize-rejection history;
- concurrent producers are serialized with a task-context lock and receive
  deterministic bounded backpressure rather than unbounded allocation.

It deliberately does not yet implement:

- stable-ID storage or key derivation;
- concurrent retirement/failure-path stress on native AROS;
- session/provider queue negotiation, scheduling or timestamp dispatch;
- an interrupt-safe ingress handoff; the current queue lock is task-context
  only;
- any format/protocol converter or lossy policy;
- the AROS `DriverData` backend shim and its open/reference integration;
- projection of native UMP endpoints back into legacy clusters;
- any public CAMD vector, tag, header or normative UMP wire constant.

Run it with the normal host suite:

```sh
make test
```

The next slice integrates the queue primitive with a software provider and
defines effective-capacity negotiation, then connects the adapter backend to
AROS `DriverData` without intercepting existing cluster traffic. Public
vectors remain blocked until native AROS retirement stress, the AROS shim,
legacy projection policy and U01 review pass.
