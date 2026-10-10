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

`native_event_pump.c` is its single-consumer delivery bridge. It checks out a
complete head item, invokes the downstream without holding the queue lock and
commits only after acceptance. Backpressure or callback failure releases the
checkout and leaves the identical native item queued for retry.

`native_event_worker.c` runs that pump in a private task. The AROS build uses a
`CreateNewProcTags()` process plus an allocated Exec signal; the host model
uses a condition variable. Wake requests coalesce, each pump run has a fixed
item budget, and synchronous stop waits until no callback is active.

`legacy_output_backend.c` composes those primitives into the output half of a
legacy-driver provider. Each session owns a bounded native MIDI 1.0 queue,
pump and worker; open acquires the physical port and attaches the worker to its
capacity fan-out transactionally, while close detaches before stopping the
worker and releasing the port. Producer enqueue wakes the worker directly.

`legacy_port_refs.c` models the logical owners of one fixed legacy port. The
AROS `DriverData` path now uses it instead of two isolated booleans: legacy
input/output presence and endpoint input/output reference counts produce one
physical open transition on the first owner and one close on the last.

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
- adapter callback validation follows the union of published directions, so
  output and input backends can land as separate reviewed steps without
  advertising callbacks they do not implement;
- adapter retirement is two-phase and remains retryable while sessions pin a
  legacy endpoint; direct legacy cluster traffic remains independent.
- format-fixed preallocated queues preserve complete MIDI 1.0 events, SysEx
  messages and 32/64/96/128-bit UMP records without conversion;
- multi-record enqueue is atomic, full and oversized operations change no
  queue content, and explicit saturating counters retain accepted, dequeued,
  cancelled, full-rejection and oversize-rejection history;
- a legacy-output enqueue mode keeps that all-or-nothing admission while
  making each short MIDI 1.0 message an independent dispatch item, so partial
  driver capacity never forces replay of an earlier message from the call;
- concurrent producers are serialized with a task-context lock and receive
  deterministic bounded backpressure rather than unbounded allocation.
- transactional checkout prevents drain/cancel from removing an item while a
  downstream decision is in flight; accepted items commit exactly once and a
  blocked downstream leaves them at the head;
- a bounded-work pump delivers MIDI 1.0 batches, complete SysEx or UMP without
  conversion, stops deterministically on downstream backpressure and exposes
  callback failures separately from a full destination;
- a task/signal worker resumes the pump on explicit producer or downstream
  capacity notifications, records work/block/failure counters and stops after
  at most the current bounded pump run;
- a bounded task-context fan-out attaches workers by generation-checked handles
  and synchronizes detach against wake traversal; this lets one stable physical
  port task relay capacity to multiple session workers without exposing their
  mutable pointers to its interrupt callback;
- existing cluster attachment/removal uses the shared port-reference state,
  while private endpoint acquire/release helpers can join the same physical
  open without a second driver `OpenPort()`; failed opens roll back ownership;
- sessions are unidirectional; `QueueCapacity` is a minimum reserved native
  record count, and successful opens expose effective capacity and the native
  MIDI 1.0 SysEx limit through registry-owned session information;
- the software provider uses the real queue primitive and proves observable
  `QUEUE_FULL`/`TOO_LARGE`, drain, cancel and cleanup when a provider reports
  an invalid reservation;
- the legacy output backend connects the adapter to per-session native queues
  and workers, preserves atomic producer admission, retries a blocked physical
  port through its fan-out without replay, and rolls back failed attach/open
  or retryable close paths.

It deliberately does not yet implement:

- stable-ID storage or key derivation;
- concurrent retirement/failure-path stress on native AROS;
- instantiating the AROS output binding for loaded drivers after stable-ID
  derivation/storage is decided;
- timestamp-based eligibility and delayed dispatch;
- an interrupt-safe ingress handoff; the current queue lock is task-context
  only (the AROS worker now has a lock-free `Signal()`-only capacity wake for
  an already-quiesced lifetime, but no interrupt producer may enqueue);
- any format/protocol converter or lossy policy;
- projection of native UMP endpoints back into legacy clusters;
- any public CAMD vector, tag, header or normative UMP wire constant.

Run it with the normal host suite:

```sh
make test
```

The AROS runtime now embeds the bounded fan-out in `DriverData`. A transmitter
capacity change signals only the stable receiver process; that task performs
the fan-out, and shutdown frees its relay signal before destroying the fan-out.
The output backend now has an AROS binding for its four physical-port
callbacks. It invokes the implemented `DriverData` reference helpers, submits
short messages directly to the native MIDI 1.0 ring and holds the legacy SysEx
borrowed buffer until final transmission. The next slice instantiates this
binding from loaded drivers using caller-supplied stable IDs; key derivation or
persistent identity must be decided before automatic publication. Existing
cluster traffic remains untouched. Public vectors remain blocked until the
data shim, timestamp policy, legacy projection policy and U01 review pass.
