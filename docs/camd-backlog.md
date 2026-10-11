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

## Done

Closed in `aros-camd-endpoint-hardening.patch` and verified on hosted AROS
(177 checks in `MIDIHubCAMDCompat --v43`, three runs in a row):

| # | What was done |
|---|---|
| B01 | Objects belong to the task that made them and go with its last close of the library. The runner checks that the library is then expunged, and that it stays for a task that never closed it. A task that dies without closing is still not covered. |
| B02 | `CAMD_PUBLISH_TEMPORARY`, and a full store falls back to temporary IDs for names and for driver ports. 300 names are published in a row. |
| B03 | `DrainEndpointSession()` takes a time limit and blocks on timer.device between looks. It is still polling, every two milliseconds, not woken by the worker: see B26. |
| B04 | Published names are checked: printable, no space at either end, at most 57 characters for a MIDI 1.0 endpoint. |
| B06 | `CAMDEndpointInfoV1.Flags` carries the directions. |
| B07 | `ObtainEndpointSnapshot()` returns the generation. Open-by-ID keeps no minimum generation, by decision. |
| B08 | `GetPublishedEndpointStats()`. |
| B09 | `PutEndpointSysEx()` takes an optional time. |
| B10 | Session workers run at priority 35, one below a port's receiver process. At 36 the loopback driver, which hands bytes back in the sender's task, overran the receiver's ring. |
| B11 | A session's queue is ordered by time, equal times by arrival. |
| B13 | A session may ask for a SysEx limit up to 65536 bytes; an oversize received message is dropped and counted. |

Closed in `aros-camd-receive-ring-and-wait.patch`, with the suite's stress
pass (six tasks, 900 rounds, no unexpected result in fifteen runs):

| # | What was done |
|---|---|
| B05 | `test_endpoint_stress`: two senders, a receiver, two publishers and an observer at once on the loopback port and on published endpoints. It found nothing; the count of received messages varies with timing because input sessions come and go, and is not asserted. |
| B25 | `Receiver()` refuses what the ring cannot hold; the receiver process reports `CMEF_RecvOverflow`, gives up a SysEx message in progress and looks for a status byte again. The ring's count is updated atomically. |
| B27 | `CamdWait()` waits for an answer signal from its timer process instead of borrowing a semaphore, so it blocks on every call. There is no test that holds `Forbid()` across a legacy wait. |
| B28 | `bufferend_rt` is set; 2000 realtime messages pass a driver port. |

Found while doing it, on hosted AROS: the shell that executes `User-Startup`
at boot runs each command with task switching disabled (`TDNestCnt` is 1 when
`main()` starts; a program started with `Run` has -1). A program there that
loops without waiting stops the whole system, and the first version of the
drain did exactly that, because the legacy `CamdWait()` only waits on its
first call in such a context. The runners now start the suite with `Run`, and
the suite refuses to start where task switching is off. Whether that state of
the boot shell is intended is a question for AROS, not for CAMD.

## P0 — can hurt as it is

Nothing open.

## P1 — a real client runs into it soon

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B12 | Scheduling exists only for driver ports. A published endpoint gets raw times. | Optional per publish request: "CAMD holds messages until their time before I take them". | A publisher that asks for it takes a timed message only when it is due. |
| B26 | `DrainEndpointSession()` looks every two milliseconds instead of being woken when the queue empties. | Have the session's worker, or the publisher's read, signal the drainer; keep the timer for the limit only. | A drain returns within a scheduling quantum of the last hand-over, without periodic wakeups. |

## P2 — complete the surface before review

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B14 | No way to see, remove or repair stored identities. A damaged store is only mentioned on the debug console. | Functions to list and remove stored identities and to report the store's state (complete, recovered from backup, unreadable), and a small shell command using them. | A removed identity gets a new ID on next use; the damaged-store case is visible to a program. |
| B15 | Records the caller hands in must have exactly today's size. | Accept shorter request records of an earlier layout by the same `Size` rule CAMD uses for records it fills in; refuse longer ones whose extra bytes are not zero. | A request cut to an earlier size opens; tests for shorter, equal and longer. |
| B16 | Fixed tables: 32 snapshots, 64 input sessions, 16 watches, 32 published endpoints, 32 sessions per endpoint, 64 records per queue. | Grow the system-wide tables on demand; keep per-session and per-endpoint bounds but let the request name them up to a maximum. | The suite opens more than today's limits without failure. |
| B17 | Function Block and Group flags have no defined values (direction, active, static). | Define the values the UMP specification's Function Block Info carries, as CAMD's own flags, and check only their range. Needs the values confirmed against the specification text later. | Flags round-trip through topology and snapshot. |
| B18 | Nothing keeps a MIDI 1.0-protocol UMP endpoint from carrying MIDI 2.0 channel voice messages, or the reverse. | Check the message type against the endpoint's protocol on send and emit, letting utility, system, data and stream types through. | A type-4 message on a MIDI 1.0-protocol endpoint is refused, with a test. |
| B20 | Identity-file writes that fail half-way are covered only by the host model. | A debug-only switch in the store that fails a chosen DOS call, driven by the hosted runner. | The runner fails each write, flush, rename and delete once and the IDs survive. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| B21 | The functions return the internal `enum CAMDRegistryResult`. | Give the public results their own names and values before review, and map internally. |
| B22 | No example and no installed documentation beyond autodoc headers. | A short sender, receiver and publisher example in the suite's directory, and one page that walks through them. |
| B23 | Published names compare exactly, driver file names without regard to case. | One rule for both, chosen with B04. |
| B24 | `midi/camdendpoint.h` mixes client records, publisher records and constants. | Split or order it once the surface stops moving. |

## Decisions needed

None open. Taken on 2026-10-11: order a session's messages by time (B11);
return the snapshot's generation without a minimum generation for opening
(B07); delivery in the publisher's task is the contract (B19, now only a
documented rule).

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
