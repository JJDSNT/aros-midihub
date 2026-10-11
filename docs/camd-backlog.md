# CAMD backlog — limits to remove and work still owed

**Scope:** what can be closed inside `camd.library` and its host tests, on
hosted AROS. Validation that needs the finished library is listed at the end
and is not scheduled here: raspi-aarch64 and m68k/i386 builds and runs,
hardware, upstream review, and checking UMP rules against the specification
text.

**State this starts from:** the provisional version 43 slice in
[`camd-endpoint-abi-draft.md`](camd-endpoint-abi-draft.md) (27 functions) and
its "Known limits of the provisional slice". Each item below removes one of
those limits or finishes something the slice left half done. When an item is
done, delete its limit there and its entry here.

Priorities: **P0** can hurt a user of the slice as it is. **P1** is a gap a
real client runs into soon. **P2** completes the surface before review. **P3**
is polish. Within a priority the order is the suggested order of work.

## P0 — can hurt as it is

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B01 | A program that closes `camd.library` without releasing its sessions, snapshots, watches and published endpoints keeps CAMD loaded for good, and CAMD goes on signalling its dead task. | Record the owning task of every such object and release them in the library's close hook for that task. A task that dies without closing the library is still not covered; say so. | The leak test's program exits normally, `Avail FLUSH` expunges CAMD, and the next open works. |
| B02 | Stored identities are never removed and there are 256. A program that publishes changing names fills the store; after that nothing new can be published or published persistently. | Let `PublishEndpoint()` ask for a temporary identity that is not stored, and make a full store fall back to temporary IDs instead of failing. Removal belongs to B14. | Publishing 300 distinct names succeeds, the first 256 at most are stored, and driver ports keep their IDs. |
| B03 | `DrainEndpointSession()` waits without limit: on a blocked port, or on a publisher that never reads. | Give it a time limit in milliseconds (0 = do not wait) and return a distinct result when it expires. It polls with `CamdWait()` today; wait on a signal from the worker or the publisher's read instead. | A drain on a publisher that does not read returns after its limit; the suite has no unbounded wait left. |
| B04 | A published name becomes two cluster names unchecked. | Refuse names CAMD cannot use as a cluster name: empty, too long for a cluster name with its `.out.0` suffix, control characters. Find the real cluster-name limit first. | Such names are `CAMD_REGISTRY_INVALID`, with a test for each rule. |
| B05 | Only one task at a time has ever driven the endpoint functions. | A stress pass in the compatibility suite: several tasks opening, sending, receiving, closing, publishing, withdrawing and taking snapshots at once, on the loopback port and a published endpoint. This is a test, but the locking it exercises is library scope. | It runs for a fixed number of rounds on hosted AROS with no hang, crash or lost count, several times in a row. Fix what it finds. |

## P1 — a real client runs into it soon

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B06 | `CAMDEndpointInfoV1.Flags` is always 0: a client cannot tell whether an endpoint takes input, output or both without trying to open it. | Define direction flags and set them for driver ports and published endpoints. | The snapshot shows directions; the suite checks them. |
| B07 | `ObtainEndpointSnapshot()` does not give the registry generation, so a watch and a snapshot cannot be put in order, and open-by-ID has no minimum generation. | Return the generation with the snapshot. Decide whether `CAMDSessionRequestV1` gets a minimum generation or the limit stays documented (see D1). | A client can tell that an event is older than its snapshot. |
| B08 | A legacy link that sends to a published endpoint whose publisher does not read loses messages, counted only inside CAMD. `PutPublished*()` also hides how many client queues were full. | A `GetPublishedEndpointStats()` with records taken, lost from legacy senders, and lost to full client queues. | The counts are visible and tested. |
| B09 | `PutEndpointSysEx()` takes no time, so SysEx is never scheduled. | Add a timed form (the registry and queue already carry the time). | A SysEx message with a time waits for it on the loopback port. |
| B10 | A session's worker runs at priority 0, below the port's receiver process (36), so scheduled output is late under load. | Run workers at the receiver's priority. | The scheduled-output check holds with a busy-looping task at priority 0. |
| B11 | A message that waits for its time holds back everything behind it in the session. | Decide (D2): keep strict order and document it as the contract, or order a session's waiting messages by time. | The contract is stated in the header and tested either way. |
| B12 | Scheduling exists only for driver ports. A published endpoint gets raw times. | Optional per publish request: "CAMD holds messages until their time before I take them". | A publisher that asks for it takes a timed message only when it is due. |
| B13 | Received SysEx longer than 4096 bytes is dropped whole, and no session can ask for more or less. | Let `CAMDSessionRequestV1` name a SysEx limit up to a system maximum; report the drop in the session's counters. | A session that asked for 16 KB receives a 10 KB message; an oversize one is counted. |

## P2 — complete the surface before review

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B14 | No way to see, remove or repair stored identities. A damaged store is only mentioned on the debug console. | Functions to list and remove stored identities and to report the store's state (complete, recovered from backup, unreadable), and a small shell command using them. | A removed identity gets a new ID on next use; the damaged-store case is visible to a program. |
| B15 | Records the caller hands in must have exactly today's size. | Accept shorter request records of an earlier layout by the same `Size` rule CAMD uses for records it fills in; refuse longer ones whose extra bytes are not zero. | A request cut to an earlier size opens; tests for shorter, equal and longer. |
| B16 | Fixed tables: 32 snapshots, 64 input sessions, 16 watches, 32 published endpoints, 32 sessions per endpoint, 64 records per queue. | Grow the system-wide tables on demand; keep per-session and per-endpoint bounds but let the request name them up to a maximum. | The suite opens more than today's limits without failure. |
| B17 | Function Block and Group flags have no defined values (direction, active, static). | Define the values the UMP specification's Function Block Info carries, as CAMD's own flags, and check only their range. Needs the values confirmed against the specification text later. | Flags round-trip through topology and snapshot. |
| B18 | Nothing keeps a MIDI 1.0-protocol UMP endpoint from carrying MIDI 2.0 channel voice messages, or the reverse. | Check the message type against the endpoint's protocol on send and emit, letting utility, system, data and stream types through. | A type-4 message on a MIDI 1.0-protocol endpoint is refused, with a test. |
| B19 | `PutPublished*()` runs receivers' work, legacy hooks included, in the publisher's task. | Document as the contract, or hand legacy delivery to a process of the endpoint. Only the second removes the limit; decide when B05 shows whether it matters. | Stated in the header, or delivery no longer runs in the caller. |
| B20 | Identity-file writes that fail half-way are covered only by the host model. | A debug-only switch in the store that fails a chosen DOS call, driven by the hosted runner. | The runner fails each write, flush, rename and delete once and the IDs survive. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| B21 | The functions return the internal `enum CAMDRegistryResult`. | Give the public results their own names and values before review, and map internally. |
| B22 | No example and no installed documentation beyond autodoc headers. | A short sender, receiver and publisher example in the suite's directory, and one page that walks through them. |
| B23 | Published names compare exactly, driver file names without regard to case. | One rule for both, chosen with B04. |
| B24 | `midi/camdendpoint.h` mixes client records, publisher records and constants. | Split or order it once the surface stops moving. |

## Decisions needed

- **D1 (for B07):** should opening by ID be able to demand "the endpoint as
  of generation N or newer" and fail as stale otherwise? The draft says yes;
  it adds a field to the request record.
- **D2 (for B11):** strict order per session, or order waiting messages by
  time? Strict order is simpler and is what CoreMIDI-style packet lists
  assume; ordering by time is what a sequencer that sends out of order wants.

## Not scheduled here

Deferred until the library is complete, by decision:

- Building and running on raspi-aarch64, m68k and i386; hardware.
- Upstream review of names, registers and records.
- Checking the UMP type table and every other UMP rule against the
  specification text (gates U02–U14).
- Any MIDI 1.0↔UMP conversion, and legacy clusters for UMP endpoints.
- UMP from a driver or transport, and Function Block versus Group Terminal
  Block precedence.
- Offline state for a driver port whose device is unplugged: the legacy driver
  interface has no way to tell CAMD.
- Unloading drivers: decided against in `camd-improvements.md` section 1.3.
- A received message's time as the hardware saw it: the legacy driver
  interface carries none.
- Cleaning up after a task that dies without closing the library (B01 covers
  the task that closes it).
