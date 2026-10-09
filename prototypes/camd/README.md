# CAMD private endpoint-core executable model

**Status:** Host-tested design evidence and source for
`patches/aros-camd-endpoint-core.patch`. The patch initializes the core inside
`camd.library`, but nothing consumes it and it exposes no public ABI.

This directory converts the accepted endpoint architecture into C before any
version 43 vectors are frozen. The same source selects `AllocVec`/`FreeVec`
when built for AROS, while the host build uses the C allocator. The opaque
registry owns an Exec semaphore on AROS and a pthread mutex in the host model;
it is not a MIDI 1.0 dynamic-port implementation.

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
  alive until the final lease is released.

It deliberately does not yet implement:

- stable-ID storage or key derivation;
- concurrent retirement/failure-path stress on native AROS;
- provider callbacks or native MIDI 1.0/UMP queues;
- legacy cluster projection;
- any public CAMD vector, tag, header or normative UMP wire constant.

Run it with the normal host suite:

```sh
make test
```

The next slice is the private format-specific provider contract, followed by a
software provider and native AROS retirement stress. Public vectors remain
blocked until native MIDI 1.0 and UMP paths are both proven and the U01 review
passes.
