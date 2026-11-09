# Native DNS-SD validation record — 2026-11-09

## D1 coordinator lifecycle prototype

The D1 prototype validates the native ownership model before any public
`dnssd.library` vectors or `mdnsd` IPC changes. It contains no DNS codec,
sockets or public ABI.

### Implemented behavior

- bounded sessions, operations and per-operation event queues;
- daemon-incarnation and slot-generation handles;
- exclusive ownership for operations sharing one conflict key;
- concurrent progress for independent keys;
- exact operation cancellation;
- owner cleanup and notification-target removal;
- restart invalidation and recovery with fresh sessions; and
- an AROS service-task probe using Exec ports, a semaphore and
  `task.resource` `TNA_REMOVED` notification.

### Host validation

The Midihub host suite builds and runs `tests/dnssd_coordinator.c` against the
portable state engine in `prototypes/dnssd/`. Eight pthread clients exercise
simultaneous independent and conflicting operations, queue isolation,
cancellation, owner removal, overflow/lost accounting and restart recovery.

Result: `make test` passed, including `DNS-SD coordinator prototype: 8
clients, 0 failures`. An AddressSanitizer/UndefinedBehaviorSanitizer run also
passed. LeakSanitizer was disabled because it is unsupported under the traced
test environment; this is not AROS runtime evidence.

### Clean AROS build

The probe was built through the AROS MetaMake target
`contrib-aros-midihub-dnssdcoordprobe` from a clean temporary build based on
AROS commit `d95e177e53a9ef6da78c2209f6a569da739e1e63`. The resulting program was
`DNSSDCoordProbe`. This validates the declared AROS build integration; it does
not by itself validate runtime behavior.

### Linux-hosted AROS runtime

`DNSSDCoordProbe` ran inside Linux-hosted AROS. The runner captured evidence
under `/tmp/dnssd-coordinator-metamake-runtime-20261108` and required these
results, among the complete 16-check run:

- `task.resource` opened and the notification hook registered before clients;
- one service task and public Exec port started;
- three independent client tasks started;
- exactly one conflicting exclusive transaction was accepted while an
  unrelated transaction proceeded;
- forced `RemTask()` caused `TNA_REMOVED` cleanup of the active operation,
  session and stale notification target;
- live cancellation was acknowledged exactly;
- normal client exit left no session, operation or notification target;
- a new client recovered the released conflict key;
- restart invalidated the old operation token;
- a fresh session operated after restart; and
- shutdown removed the service port and notification hook safely.

Result: `DNSSD-COORDINATOR 16 checks, 0 failures` and the runner reported
`PASS DNS-SD coordinator prototype on AROS linux-hosted`.

### Reproduction

From the Midihub source tree:

```sh
make test
```

Build `contrib-aros-midihub-dnssdcoordprobe` with the normal AROS MetaMake
workflow, then run:

```sh
tools/dnssd-coordinator-hosted.sh AROS_BUILD \
  AROS_BUILD/Extras/aros-midihub/Tests/DNSSDCoordProbe EVIDENCE_DIR
```

`AROS_BUILD` must be a runnable Linux-hosted AROS tree. The script copies the
tree to its evidence directory before installing and running the probe.

### Limits

- This validates the coordinator/lifecycle design, not `mdnsd`, DNS-SD wire
  interoperability or a public library ABI.
- The prototype does not establish that every daemon operation can execute
  inside the task-resource callback. Production integration must keep the
  callback bounded and synchronize teardown using the proven service-owned
  state model.
- No hardware AROS runtime was tested; Linux-hosted AROS is the required D1
  runtime platform.
- No Bonami compatibility or coexistence behavior was tested or is required.

## D2 canonical-model exploratory canary

The private canary at AROS commit `e812bd5be9` on branch
`proposal/dnssd-canonical-model` tests a
canonical representation before daemon integration or public ABI selection.
Its source is `workbench/network/mdns/dnssd_model.*`, with the runtime test in
`developer/debug/test/dnssd/`. It is independent of the current `mdnsd`
structures and of PR #2123.

The host test validates:

- a 96-byte fixed-width, pointer-free envelope and 24-byte address records;
- size negotiation without partial output;
- daemon incarnation, session, operation and sequence correlation;
- counted instance data preserving dots and backslashes;
- DNS wire names for type, domain and SRV target;
- counted binary TXT including zero and non-ASCII bytes;
- deterministic IPv4/IPv6 ordering and duplicate elimination;
- stable interface index carried with every address; and
- rejection of unknown version, header references, overlapping fields,
  malformed DNS names/TXT, inconsistent address extents and out-of-range
  address access.

Results:

- normal host build: `DNS-SD canonical model: 0 failures`;
- AddressSanitizer/UndefinedBehaviorSanitizer: passed with leak detection
  disabled for the traced environment; and
- AROS MetaMake target `test-dnssd-model-quick`: built successfully; and
- Linux-hosted AROS runtime: `DNS-SD canonical model: 0 failures`, with
  evidence under `/tmp/dnssd-model-runtime-20261109`.

The Midihub implementation copy was removed after the source moved to AROS;
only the Linux-hosted runner remains here. This validates model parsing and
layout on AROS, but not daemon IPC, network interoperability or public ABI.
