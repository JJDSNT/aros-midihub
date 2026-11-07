# MIDI-CI architecture for AROS

Status: Gate 1 evidence supports the component decision. The first Gate 2
AROS runtime ownership prototype passes; the service ABI remains open pending
engine-MUID binding, forced termination and coordinator-restart tests.

## Decision

Use a hybrid model:

1. A portable, sans-I/O C engine implements codecs and MIDI-CI state
   transitions. It owns no task, CAMD endpoint or clock.
2. A resident system MIDI-CI coordinator owns the authoritative MUID and peer
   state for each CAMD endpoint/Function Block, owns the CAMD sessions and
   serializes conflicting operations.
3. `midici.library` is the public API boundary. Its ordinary application calls
   marshal operations to the coordinator; the same engine source can also be
   instantiated privately for an application-owned virtual endpoint.
4. Midihub configures and observes the coordinator but is not the coordinator
   process and is not required for applications to use MIDI-CI.

This retains the proposed division: CAMD transports generic MIDI, MIDI-CI owns
protocol state, and Midihub owns management/persistence UI.

## Evidence about AROS shared state

AROS libraries do not inherently have per-process state. The
[AROS library guide](https://developers.aros.org/documentation/sys-dev/libraries.html)
says the default is one base shared by all openers; `options peropenerbase`
must be requested for separate bases. CAMD's `camd.conf` does not request that
option. Its `CamdBase_intern` contains the registry and `SignalSemaphore`
instances, and `InitCamd()` initializes them once. Exec documents that these
semaphores are shared between tasks and must live in shared memory.

Therefore design A is technically capable of shared mutable state. The
problem is lifecycle, not visibility: CAMD 43's own owner code says a task
that exits without its final `CloseLibrary()` is not observed and its endpoint
objects remain. A MIDI-CI library that pins authoritative sessions per caller
would inherit this failure and make recovery depend on every application
terminating correctly.

## Compared designs

| Criterion | A. library-managed | B. resident service | C. hybrid (selected) |
| --- | --- | --- | --- |
| Shared correctness | Possible with one shared base and semaphores | Natural single owner | Natural single owner for authority; reusable engine remains separate |
| Abnormal client exit | Weak without a proven task-death hook; leaked caller-owned CAMD handles are demonstrated | Client request can expire; service-owned CAMD handle survives | Same service recovery, with no transport in the library engine |
| MUID collision scope | Easy to accidentally allocate per opener | One table per endpoint/Function Block | One authoritative table; isolated engines only for explicitly app-owned endpoints |
| Conflicting PE/Profile operations | Requires global locks inside arbitrary caller tasks | Serialized message loop | Serialized service, engine itself remains deterministic/sans-I/O |
| Memory | Lowest code count, but global state grows in library base | One task stack plus bounded tables | One task plus shared engine code; bounded tables |
| Latency | Direct call | One IPC round trip for control traffic | Same; MIDI-CI is control-plane traffic, not note-path traffic |
| Compatibility | Public library ABI only | Needs service protocol and library API | Library hides service protocol; direct engine tests remain hostable |
| Failure recovery | Library expunge/reopen is hard while leaked handles pin it | Restart coordinator, allocate fresh MUIDs, rediscover | Same, with protocol state rebuilt from deterministic engine events |

Design B alone tends to embed codecs and policy in the service. Design C keeps
the engine independently testable and allows a device application to reuse it
without making the Midihub GUI mandatory.

## Ownership model

| State | Sole owner | Persistence |
| --- | --- | --- |
| CAMD stable endpoint ID/topology | CAMD/driver | CAMD identity store where supported |
| Local MUID for a system endpoint/Function Block | coordinator | runtime only; fresh after coordinator/system restart |
| Remote MUID and Discovery data | coordinator | runtime cache, invalidated on collision/offline |
| CAMD input/output sessions used for MIDI-CI | coordinator | runtime |
| PE request IDs, chunks, deadlines | coordinator engine instance | runtime until completion/timeout/disconnect |
| Profile state learned from peer | coordinator cache | runtime; rediscovered after reconnect |
| User aliases, routes and desired policy | Midihub configuration | persistent |
| UMP protocol selection | UMP endpoint/driver through generic CAMD transport metadata | transport lifecycle |

A MUID is never derived from or persisted as a CAMD endpoint ID. The CAMD ID
keys the coordinator's runtime record; MIDI-CI collision rules determine the
MUID.

## Runtime flow

1. Coordinator takes a CAMD snapshot and watch without a gap.
2. For a bidirectional MIDI 1.0 endpoint it opens complete-message MIDI 1.0
   input/output sessions. For UMP it opens native UMP sessions and feeds
   Data64 packets to a bounded SysEx7 assembler.
3. It creates one engine instance for the appropriate MIDI-CI scope, generates
   an assignable random MUID, then sends Discovery.
4. Incoming MIDI-CI is decoded and correlated by source/destination MUID and,
   for PE, request ID. Non-CI messages are ignored by the coordinator; CAMD's
   independent application sessions continue to receive their traffic.
5. An application request receives a coordinator lease. Operations that
   mutate the same Profile/resource or exceed the peer's simultaneous PE
   capacity are queued or rejected as busy. Read-only operations may overlap
   only where the peer capability and request-ID table permit it.
6. Each request has a deadline independent of the client task. Timeout,
   disconnect or endpoint retirement cancels it and releases the lease.

## Collision and lifecycle rules

- If the engine observes its local MUID as another source, it emits Invalidate
  MUID, cancels in-flight transactions, obtains fresh entropy, chooses another
  assignable MUID and restarts Discovery.
- Endpoint `OFFLINE`, `RETIRED` or provider loss cancels all transactions and
  discards remote MUID/Profile/PE state. The stable CAMD endpoint ID remains a
  reconnect key only.
- Reconnect never assumes that a previous remote MUID is still valid.
- Coordinator restart creates fresh local MUIDs and sends Discovery; clients
  see session-lost events and may retry idempotent reads.
- A dead application cannot own the CAMD session. Its outstanding operation
  expires at the normal protocol deadline even if no explicit IPC teardown is
  observed.

## Initial engine contract

`include/midici/midici.h` currently exposes caller-owned records and functions
for:

- strict envelope and all Management body codecs (Discovery, Endpoint
  Information, ACK/NAK and Invalidate); variable decoded fields are borrowed
  from the caller's frame and have no hidden allocation;
- MUID generation/collision recovery;
- a 16-entry request table with unique 7-bit IDs, deadlines, completion and
  remote cancellation;
- an initial synchronous Management Responder which emits at most one complete
  response for each input and implements Discovery, Endpoint Information,
  Invalidate receive and the used/unused MUID collision branches.

The Responder is protocol state, not the system coordinator: it owns no CAMD
session, clock, entropy source or output queue. The caller supplies the frame,
fresh entropy and response storage. Initiator peer/discovery-round state is the
remaining CI03 engine work and is not implied by the transaction table.

The engine has no internal mutex. The coordinator task serializes it. This is
intentional: adding locks would not define cross-process ownership and would
make a portable protocol engine responsible for an OS policy.

`tests/midici_concurrency.c` is the initial host experiment for this boundary:
eight simultaneous client threads submit the same endpoint operation through
one serialized context; exactly one obtains the transaction and seven receive
`MIDICI_BUSY`. It proves the bounded arbitration rule under host concurrency,
not AROS IPC, task-death cleanup or the final service design.

`ports/aros/midici_coordinator_probe.c` is the first native experiment. One
resident Exec task owns a named message port and keys a lease by both stable
task ID and task pointer, plus a monotonically changing transaction token. On
raspi-aarch64 AROS under QEMU, two independent tasks produced exactly one
owner and one busy result, explicit cancellation released the lease, and a
task that returned without cancelling was detected with `FindTaskByPID()` and
reaped before the next grant. The request/reply storage remained parent-owned,
so the coordinator never replied to a port belonging to a dead task.

The experiment also established a required lifetime rule: a client issuing
an explicit cancellation must remain alive until the coordinator ACKs it.
Otherwise death reaping may legitimately win and make the cancel token stale.
This rule must be hidden by `midici.library`, not imposed as an unsafe pointer
lifetime on applications.

## Service gate still open

Before publishing service vectors or an IPC wire format, the remaining AROS
runtime work must demonstrate:

1. the proven single lease is bound to one coordinator engine/MUID;
2. independent operations follow peer limits while conflicts remain exclusive;
3. forced task removal, not only natural return, leaves no permanent lease;
4. killing/restarting the coordinator produces fresh MUIDs and rediscovery;
5. service discovery and messages use supported Exec primitives on all target
   architectures.

Until that test exists, the service API is a reviewed architecture, not an
implementation-ready public ABI.
