# CAMD backlog — limits to remove and work still owed

**Scope:** what can be closed inside `camd.library` and its host tests, on
hosted AROS. Validation that needs the finished library is listed at the end
and is not scheduled here: raspi-aarch64 and m68k/i386 builds and runs,
hardware, upstream review, and checking UMP rules against the specification
text.

**State this starts from:** the provisional version 43 slice in
[`camd-endpoint-abi-draft.md`](camd-endpoint-abi-draft.md) (31 functions) and
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

Closed in `aros-camd-endpoint-hold-and-drain.patch` (193 v43 checks, three
runs in a row):

| # | What was done |
|---|---|
| B12 | `CAMD_PUBLISH_HOLD_UNTIL_DUE`: each client session of such an endpoint gets a worker that moves a message to the publisher's side when its time has come; the publisher is signalled only then. MIDI 1.0 endpoints only. |
| B26 | `DrainEndpointSession()` is woken by the session's worker, by the publisher's read and by a withdrawal; its timer is only for the limit. With more than 16 tasks draining at once, or no free signal, a caller falls back to looking every two milliseconds. |

The test for these found two defects in the published endpoint's drain: it
signalled the publisher for a message that was still being held, and it kept
waiting on a withdrawn endpoint whose client had something queued. A drain on
a withdrawn endpoint now returns `CAMD_RESULT_RETIRED` (then `CAMD_REGISTRY_RETIRED`) at once.

Found while doing it, on hosted AROS: the shell that executes `User-Startup`
at boot runs each command with task switching disabled (`TDNestCnt` is 1 when
`main()` starts; a program started with `Run` has -1). A program there that
loops without waiting stops the whole system, and the first version of the
drain did exactly that, because the legacy `CamdWait()` only waits on its
first call in such a context. The runners now start the suite with `Run`, and
the suite refuses to start where task switching is off. Whether that state of
the boot shell is intended is a question for AROS, not for CAMD.

Closed in `aros-camd-endpoint-surface.patch` (217 v43 checks on hosted AROS,
host tests, builds for raspi-aarch64):

| # | What was done |
|---|---|
| B15 | `CAMDSessionRequestV1` and `CAMDPublishRequestV1` are accepted in an earlier layout, down to the first, and longer than today's when the extra bytes are zero. A publish record without `DataFormat` publishes a MIDI 1.0 endpoint; one that has the field must set it. Event records still have exactly their size: they travel in arrays. |
| B17 | `CAMD_GROUP_FLAG_` and `CAMD_BLOCK_FLAG_` values for direction, active, static, UI hint and MIDI 1.0 port; undefined bits and 31.25 kbit/s without the MIDI 1.0 flag are refused. They are CAMD's own values and are not yet compared with the specification text. |
| B18 | A UMP channel voice message of the other protocol is refused with `CAMD_RESULT_UNSUPPORTED`, sent by a client or emitted by the publisher; utility, system, data and stream messages pass on both. |
| B21 | The public results are `enum CAMDResult`, `CAMD_RESULT_` names with fixed values; the registry's enum left the public header and `camd_public_result()` maps it in every library function. |
| B23 | One rule: names compare without regard to ASCII case. A published name is folded for its stored ID and for the duplicate check, as a driver's file name already was. IDs stored earlier for names with capitals are not found again. |
| B24 | `midi/camdendpoint.h` is in four parts: what every caller needs, looking at endpoints, sessions, publishing. It also states the two size rules for records. |

Closed in `aros-camd-identity-functions.patch` (229 v43 checks, three runs in
one suite; host tests; builds for raspi-aarch64):

| # | What was done |
|---|---|
| B14 | `GetEndpointIdentityStore()`, `GetStoredEndpointIdentity()` and `ForgetEndpointIdentity()`, and the shell command `CAMDIdentities [FORGET <id>]`. The store's state is complete, recovered, unreadable or absent. An ID in use is `CAMD_RESULT_BUSY` to forget; a forgotten name gets a new ID. The suite's test that fills the store now forgets what it stored, so a second run in the same system has room. Nothing repairs an unreadable store: its files are still left for a person to look at. |
| B20 | With `CAMD_TEST_HOOKS=yes` in the environment of `make`, the library fails the file call that `CAMD_IDENTITY_FAULT` numbers. `tools/camd-compat-hosted.sh` fails each call of two replacements in turn, 15 boots: Open, Write (half the data stays), Flush, Close, Rename and DeleteFile each at least twice. After every one the next boot finds the drivers' IDs as they were. Without the hooks the runner says it skipped this. |

## P0 — can hurt as it is

Nothing open.

## P1 — a real client runs into it soon

Nothing open.

## P2 — complete the surface before review

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B16 | Fixed tables: 32 snapshots, 64 input sessions, 16 watches, 32 published endpoints, 32 sessions per endpoint, 64 records per queue. | Grow the system-wide tables on demand; keep per-session and per-endpoint bounds but let the request name them up to a maximum. | The suite opens more than today's limits without failure. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| B22 | No example and no installed documentation beyond autodoc headers. | A short sender, receiver and publisher example in the suite's directory, and one page that walks through them. |

## Raised by the upstream review of btmidi.class (2026-10-11)

A transport has to be a `DEVS:Midi` driver, so what the endpoint registry
knows about a transport is what a driver can tell it; see the amendment in
[the architecture decision](camd-endpoint-architecture-decision.md). These
wait for the maintainer's answer in AROS PR #1507 and for an agreed sketch,
and are not started. What each one would repair in `btmidi.class` is in
[the driver's limitations](ble-midi-backlog.md).

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| B29 | A driver cannot say which device is behind a port. Identity is the file name plus the port number, so a BLE or USB device that takes another port is another endpoint. | An optional block beside `MidiDeviceData`, found the way `MidiDeviceData` is, through which a driver gives identity evidence and its confidence per port. No change to `MidiDeviceData`, the vectors or the cluster names; a driver without it behaves as today. | `btmidi` reports a device's address and the endpoint keeps its ID when the device changes port; the 41.1 contract suite is unchanged. |
| B30 | All of a driver's ports are published as available, with or without a device. | Presence per port through the same block, mapped to available and offline. | An unbound `btmidi` port is offline in a snapshot and a watch reports the change. |
| B31 | A driver port has no name but `<driver>.<n>`. | A display name per port through the same block; the cluster names stay. | `GetEndpointInfo()` returns the device's name. |
| B32 | A driver cannot carry UMP or the time a message arrived. | A UMP path and a timed receive in the block, after B29 to B31 and after the specification gates. | Scheduled with the M0-M3 gates, not here. |

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
- UMP from a driver or transport (B32), and Function Block versus Group
  Terminal Block precedence.
- Offline state for a driver port whose device is unplugged: the legacy driver
  interface has no way to tell CAMD (B30).
- Unloading drivers: decided against in `camd-improvements.md` section 1.3.
- A received message's time as the hardware saw it: the legacy driver
  interface carries none (B32).
- Cleaning up after a task that dies without closing the library (B01 covers
  the task that closes it).
