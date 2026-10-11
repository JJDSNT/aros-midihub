# AROS patches

**Base:** AROS upstream `master` at `a7102948cd` (10 October 2026). The AROS
tree used here is `~/AROS`, branch `midihub/upstream-2026-10-11`.

## Now in upstream

These patches were carried here and have been removed because upstream has
their content. They remain in this repository's history.

| former patch | upstream |
|---|---|
| `aros-camd-names-64bit.patch` | `28ec43a517` camd: format cluster names through a va_list |
| `aros-camd-arena-segments.patch` | `cb8c4c5f39` camd: skip arena hunks when scanning a driver |
| `aros-llvmpipe-link.patch` | `ba2631f94c` llvmpipe: link the target-side LLVM archives by path |
| `aros-raspi-console-miniuart.patch` | `72a7ac6e0b` raspi-aarch64: print the debug console where the firmware routed it |
| `aros-raspi-bt-firmware.patch` | `bcd86885e7` and `e2cb3acaac`, which also add the Pi 400/CM4 firmware and its licence |
| `aros-ble-midi-upstream-gaps.patch` | `86aab939c4` bluetooth: add remaining LE peripheral and GATT support, with the review fixes of `32ee68ce33`; `bluetooth.library` is 45.20 |

The last two no longer applied: upstream's versions are later than the ones
the patches held. Nothing of the Bluetooth or Raspberry Pi work is carried
here any more; what the tree had before the update is kept outside the
repository in `~/backups/aros-tree-2026-10-11-before-upstream-update`.

## CAMD and USB MIDI

The patches in [`camd-series`](camd-series), applied in that order to the base
above. The first of them were commits of the `camd-robustness` branch in
`~/AROS`; two earlier ones are in upstream now.
The modified AROS sources are covered by [AROS-LICENSE](AROS-LICENSE).

| patch | fixes |
|---|---|
| `aros-camd-driver-scan-align.patch` | no `DEVS:Midi` driver loads on AArch64 |
| `aros-debugdriver-port-index.patch` | `debugdriver` crashes on the first message to port 0 |
| `aros-camd-port-open.patch` | a driver port closes under a receiver still linked to it |
| `aros-camd-notify-lock.patch` | the cluster notification list is not locked |
| `aros-camd-rescan.patch` | `RethinkCAMD()` loads drivers installed after CAMD started |
| `aros-usb-midi-camd.patch` | USB MIDI receive buffers, transmit ring, failed open |
| `aros-usb-midi-lifecycle.patch` | the USB MIDI driver never loads; ports across unplugging |
| `aros-camd-part-notify.patch` | `MIDI_PartHook`/`MIDI_PartSignal` and the participant counts do nothing |
| `aros-camd-v42.patch` | version 42: `GetClusterAttrsA()`, `CamdTime()`, `MIDI_SystemClock`, cluster watches |
| `aros-camd-link-comments.patch` | implement `MLINK_Comment` and expose the selected cluster comment through CAMD 42 |
| `aros-camd-sysex-errors.patch` | apply `MIDI_ErrFilter` and report oversized SysEx as `CMEF_SysExTooBig` without partial messages |
| `aros-camd-endpoint-core.patch` | add the non-public endpoint registry and format-specific provider contract; initialize the registry and compile both inside CAMD without adding vectors |
| `aros-camd-endpoint-runtime.patch` | embed the bounded worker fan-out in each `DriverData`, relay transmitter capacity through its stable receiver task and compile the relay without changing the public ABI |
| `aros-camd-legacy-output-backend.patch` | add bounded native MIDI 1.0 output sessions and bind their physical callbacks to shared `DriverData` ownership and the legacy transmitter, without publishing endpoints or changing the public ABI |
| `aros-camd-identity-store.patch` | add the private bounded identity map, architecture-independent checked IFF format and recoverable `ENVARC:` storage backed by `uuid.library`; failure leaves legacy CAMD operational |
| `aros-camd-identity-recovery.patch` | share the actual main/`.new`/`.bak` recovery state machine with the exhaustive host fault-injection model |
| `aros-camd-legacy-identity-set.patch` | compile the legacy evidence keys and the all-or-nothing provider plus per-port ID resolution |
| `aros-camd-driver-endpoints.patch` | `LoadDriver()` publishes each loaded driver's ports as private output endpoints with stored IDs, and `FreeDriverData()` retires them first; a failure leaves the driver's clusters working |
| `aros-debugdriver-output.patch` | the debugdriver prints each byte it transmits, so a test can compare what reached a port |
| `aros-camd-endpoint-client.patch` | provisional camd.library 43: `ObtainEndpointSnapshot()`, `GetEndpointInfo()`, `ReleaseEndpointSnapshot()`, `OpenEndpointSession()`, `PutEndpointMidi()`, `PutEndpointSysEx()`, `DrainEndpointSession()`, `CloseEndpointSession()` and the installed `midi/camdendpoint.h`; native MIDI 1.0 output sessions only |
| `aros-camd-endpoint-expunge.patch` | camd.library refuses to be expunged while a client has left an endpoint session or snapshot open, instead of freeing the drivers under it |
| `aros-midi-loopback-driver.patch` | new `DEVS:Midi/loopback` driver with two ports: what is sent to `loopback.out.<n>` is received from `loopback.in.<n>` |
| `aros-camd-endpoint-input.patch` | input sessions: a driver port's receiver process hands each complete message to the port's receiving sessions beside its legacy cluster; `GetEndpointMidi()` reads a session's bounded queue and the opening task is signalled |
| `aros-camd-endpoint-watch.patch` | `StartEndpointWatch()`, `GetEndpointWatchEvent()` and `EndEndpointWatch()`: a bounded queue of endpoint changes with a signal, reporting loss instead of blocking |
| `aros-camd-identity-unreadable.patch` | identity files that hold no complete snapshot no longer switch endpoint publication off: that run gives temporary IDs and leaves the files untouched |
| `aros-camd-endpoint-publish.patch` | a program publishes its own endpoint: `PublishEndpoint()`, `WithdrawEndpoint()`, `SetPublishedEndpointState()`, `PutPublishedMidi()`, `PutPublishedSysEx()` and `GetPublishedMidi()`; the name gives the stable ID, and a withdrawn endpoint waits for its clients' sessions to close |
| `aros-camd-endpoint-projection.patch` | a published endpoint also takes part in the legacy clusters `<name>.out.0` and `<name>.in.0`, like a driver's port, so existing programs reach it by linking; complete messages pass between clusters and endpoint without a byte stream |
| `aros-camd-endpoint-diagnostics.patch` | `CancelEndpointSession()` and `GetEndpointSessionStats()`; an offline published endpoint leaves its legacy clusters; a published endpoint and a driver cannot share a name |
| `aros-camd-endpoint-time.patch` | received messages carry the `CamdTime()` of their arrival, and a message sent to a driver's port with a time waits in its session's queue until then: the session worker gains a timed retry on timer.device |
| `aros-camd-endpoint-ump.patch` | native UMP without conversion for published endpoints: `PutEndpointUMP()`, `GetEndpointUMP()`, `PutPublishedUMP()`, `GetPublishedUMP()`; Groups and Function Blocks with `SetPublishedEndpointTopology()`, `GetEndpointGroup()` and `GetEndpointFunctionBlock()`; word counts checked against the message type |
| `aros-camd-endpoint-layout.patch` | `midi/camdendpoint.h` checks every record's size at compile time |
| `aros-camd-endpoint-hardening.patch` | objects go with their task's last close of the library; temporary IDs and a full identity store; time-ordered session queues and timed SysEx; a bounded, really blocking drain; direction flags, snapshot generation, name checks, per-session SysEx limits and `GetPublishedEndpointStats()` |
| `aros-camd-receive-ring-and-wait.patch` | legacy fixes: a full receive ring loses the new bytes and reports `CMEF_RecvOverflow` instead of overwriting unread ones; `CamdWait()` really waits on every call; the realtime send ring wraps |
| `aros-camd-endpoint-hold-and-drain.patch` | `CAMD_PUBLISH_HOLD_UNTIL_DUE` keeps clients' timed messages until they are due before a publisher can take them; `DrainEndpointSession()` is woken when a queue empties or its endpoint is withdrawn instead of polling |

```sh
grep -v '^#' patches/camd-series | while read -r p; do
    git -C ~/AROS apply "$PWD/patches/aros-$p.patch" || break
done
```

[`camd-series`](camd-series) is the apply order.
[`tools/check-camd-patches.sh`](../tools/check-camd-patches.sh) (`make
check-patches`) applies it to a clean index of `~/AROS` and reports any CAMD
change in that tree which the series lacks.
[`tools/gen-camd-patch.sh`](../tools/gen-camd-patch.sh) writes a new patch as
the difference between that index and the tree and appends it to the series;
do not edit hunks by hand.

**Names on 64-bit targets.** `mysprintf()` took its variadic arguments from
`&fmt+1`, which only works where they sit on the stack (m68k, i386), so every
cluster name and `DEVS:Midi` path came out as garbage and nothing could find
a cluster by name. It now formats through `VNewRawDoFmt()` with a `va_list`.

**Arena-loaded modules.** `LoadDriver()` reads each hunk's size from the
longword before its `BPTR`. With `ELF_MODULE_ARENA` the section hunks store
a size of 0 and a container hunk holds the arena; subtracting the header
from 0 wrapped, and the scan ran to the top of RAM (a bus fault on a Pi 3).
Hunks no larger than their header are skipped; the container covers them.

**Aligned driver scan.** The scan then stepped through the container in
`AROS_PTRALIGN` steps from the end of its 12-byte header, 4 bytes off the
8-byte boundary where the `MidiDeviceData` is, and never found it: no driver
loaded on AArch64. It now starts at the first aligned address.

**debugdriver.** `OpenPort()` stored port N's user data at `UserData[N-1]`,
so port 0 overwrote the transmit function pointer, and the first message
executed data. Found once the driver loaded.

**Port open state.** A port opened for a sender did not record a receiver
that linked later; when the sender left, CAMD closed the port under the
receiver.

**Notification lock.** `StartClusterNotify()`/`EndClusterNotify()` changed
the list the cluster code walks, unlocked. The list has its own semaphore,
not `CLSemaphore`, so a caller holding `LockCAMD()` does not deadlock.

**RethinkCAMD().** It loads drivers added to `DEVS:Midi`. Their ports join
clusters of the same name that clients already made, and open for the links
waiting there, under `CLSemaphore`. Drivers are never unloaded.

**USB MIDI.** The first patch fixes the receive buffer format, the transmit
ring and a null dereference on a failed open. The second one:

- The class wrote `DEVS:Midi/<device>` with the name `poseidonusb` inside,
  and CAMD only loads a driver whose name is its file name: USB MIDI never
  appeared on any target but m68k. The driver's name is now written into it.
- The class calls `RethinkCAMD()` after writing the driver, so a device
  plugged in after CAMD started gets its clusters.
- Each device has its own 16 ports; before, a second device took over the
  first one's.
- Ports outlive the binding: they open while the device is away, drop what
  they are sent while it is unplugged instead of crashing, and carry MIDI
  again when it comes back, without clients linking again.

**Participant notification.** `MIDI_PartHook` and `MIDI_PartSignal` were
stored and never used. A node is now told, once, when another node's link or
a driver port joins or leaves a cluster it is linked to, and
`mcl_Participants`/`mcl_PublicParticipants` are kept.

**Version 42.** New calls after `Midi2Driver()`, nothing existing changed:
`GetClusterAttrsA()` (name, participants, driver port present, drop and
receive-error counters), `CamdTime()` (milliseconds from the E-clock) with the
node tag `MIDI_SystemClock`, and `StartClusterWatchA()`/
`GetClusterWatchEvent()`/`EndClusterWatch()`, which queue named events
(added, removed, participants, lost) for programs that follow every cluster.
`StartClusterNotify()` keeps its 41.1 meaning.

**Link comments.** `MLINK_Comment` now follows the original CAMD contract:
the highest-priority link supplies a private, 34-character cluster comment,
with receivers winning equal-priority ties. The value is recalculated when a
link joins, leaves, changes priority, or changes its comment. Driver-port list
nodes are ignored. `GetMidiLinkAttrsA(MLINK_Comment)` and CAMD 42's
`GetClusterAttrsA(MCLA_Comment)` return the selected cluster comment.

**SysEx and error filtering.** Every receive-side error now goes through the
node's `MIDI_ErrFilter`. `PutSysEx()` checks the complete message size before
delivery, reports `CMEF_SysExTooBig` when it cannot fit, and leaves no partial
message behind. Byte-wise input from `ParseMidi()` and drivers distinguishes a
message that cannot fit from a temporarily full ring and discards the rejected
message through EOX. The allocated extra ring byte is now used, so a message
exactly equal to `MIDI_SysExSize` fits.

**Private endpoint core.** The final patch begins the accepted CAMD endpoint
registry/provider architecture without exposing a version 43 ABI. It adds
fixed-layout Endpoint, Group and Function Block records, generation-safe
leases, validated transactional topology, immutable snapshots and explicit
lifecycle/retirement, and bounded generation-tagged endpoint watches. Watch
overflow collapses stale queued events into one explicit `lost` marker; a
fresh snapshot is the recovery path. Retiring endpoints disappear from new
snapshots immediately but remain allocated through their last lease.
`CamdBase` owns the registry, whose opaque implementation owns and enforces a
dedicated Exec semaphore; initialization failure unwinds the registry, timer
and legacy semaphore.

The patch also compiles a private provider contract. Providers declare
MIDI 1.0, UMP-with-MIDI-1.0 and UMP-with-MIDI-2.0 native paths plus direction.
Sessions select one exact path; MIDI 1.0 short messages/complete SysEx and UMP
events use separate send and receive callbacks. Receive sinks expose only the
selected format, so the core cannot accidentally route a native MIDI 1.0 batch
through UMP. Retirement rejects new opens and sends while drain, cancel,
receive stop and close remain available. Numeric values, pointer-bearing
callback tables and the provisional private MIDI 1.0 envelope are not public
ABI.

The registry now copies provider descriptors into generation-safe slots and
requires each endpoint publication to name a live, identity- and
format-compatible owner. Format-fixed data sessions independently pin their
provider and endpoint, and dispatch only the matching MIDI 1.0 or UMP record
family. All provider callbacks run outside the registry semaphore. A stable
receive bridge filters asynchronous delivery and survives until receive stop
and every entered callback complete. Provider retirement removes all its
endpoints from new snapshots in one generation, rejects new data work and
waits for endpoint, session and callback leases. Host callbacks reenter
snapshot enumeration and retirement to prove those properties; reused slots
reject stale handles.

No public watch/session vector, UMP wire parser, converter or reverse legacy
projection uses the provider path yet, so existing CAMD behavior remains
unchanged. A private fixed-port adapter now publishes
caller-identified legacy driver ports as MIDI 1.0-only endpoints, checks
direction per port and forwards native events/SysEx without UMP. It is not yet
connected as an AROS data backend. `DriverData` does now replace its independent
open booleans with shared legacy-direction and endpoint-reference ownership:
the first owner opens the port, the last closes it and a failed open rolls back.
The matching host models and
tests live in
`prototypes/camd`, `tests/camd_endpoint_core.c` and
`tests/camd_provider_contract.c` plus
`tests/camd_legacy_driver_adapter.c`; they race registry snapshots/leases,
exercise watch overflow/resync and use a software provider to prove ownership,
retirement, exact-path dispatch and asynchronous receive teardown.

The patch also compiles a private format-fixed queue primitive. It preallocates
all MIDI 1.0 or UMP ring storage, commits batches atomically, bounds complete
SysEx messages, returns explicit full/oversize backpressure and exposes
saturating accepted/dequeued/cancelled/rejection counters. It is intended as
the provider's single transport-facing queue, not a mandatory second queue in
the registry. Sessions now request a minimum reservation and expose effective
capacity plus the MIDI 1.0 SysEx limit; inconsistent provider results are
closed and rejected. The host software provider uses the queue to prove full,
oversize, drain and cancel behavior. Interrupt ingress remains deliberately
unconnected. Transactional checkout/commit/release plus a bounded-work pump
call the downstream unlocked and retain the exact head item on backpressure or
callback failure. A private bounded-work worker supplies the task/signal
mechanism: AROS uses `CreateNewProcTags()` and Exec signals, wakes coalesce,
counters expose progress/block/failure, and stop waits for the active pump
call. Each `DriverData` now owns a bounded worker fan-out. The transmitter
signals the stable per-port receiver process only when it frees output
capacity; that task then wakes attached workers outside interrupt context. A
private output backend now owns each session's bounded native MIDI 1.0 queue,
pump and worker. Its AROS binding acquires/releases the shared `DriverData`
reference under `CLSemaphore`, submits short messages directly to the legacy
ring and preserves the borrowed-buffer lifetime for SysEx. Caller-supplied
endpoint IDs are required; automatic driver publication and timestamp
eligibility remain deliberately unconnected.

### Testing

[`tools/camd-compat-qemu.sh`](../tools/camd-compat-qemu.sh) runs
`MIDIHubCAMDCompat`, a suite of the CAMD 41.1 behaviour programs rely on,
on `raspi-aarch64` under QEMU. Against camd.library with the first four
patches and with all of them:

| | 41.1 contract | version 42 | `RethinkCAMD()` |
|---|---|---|---|
| first four patches | 50 of 50 | skipped | 4 of 8 (not implemented) |
| up to `camd-part-notify` | 50 of 50 | skipped | 8 of 8 |
| all patches | 68 of 68 | 34 of 34 | 13 of 13 |

`MIDIHubCAMDCompat --v43` is a client of the provisional endpoint functions: 193
checks on hosted AROS, a stress pass with six tasks among them. The runners repeat it twice in the second boot: after
`--v43-leak` left objects behind, closed the library and `Avail FLUSH`
expunged it, and after `--v43-abandon` ended without closing it, when the
library has to stay. They start the suite with `Run`: the shell that executes
`User-Startup` at boot runs commands with task switching disabled, and the
suite refuses to start there. Its session and a legacy link send to the same
debugdriver port, and the runner compares the bytes the driver printed. Input
sessions are tested on the loopback driver, in both directions between a
session and the port's legacy clusters. The suite also publishes an endpoint
and is its own client: both directions, offline and back with the same ID, and
withdrawal while sessions are open, and legacy links to its clusters in both
directions, which wait across a withdrawal and reach it again when it is
published anew.

Use the build tree's `bin/raspi-aarch64/AROS` as `SD_DIR`; an older copy with a
`debugdriver` from before `debugdriver-port-index` crashes on the first send.

[`tools/camd-compat-hosted.sh`](../tools/camd-compat-hosted.sh) runs the same
three passes on Linux-hosted AROS in a few seconds, from a copy of
`build-aros-linux/bin/linux-x86_64/AROS`, with the same results. Both scripts
also check that the endpoint IDs the first boot stores are the ones the second
boot publishes.

The hosted runner then damages the identity store's files in its copy and
boots again for each case: only `.bak` left, a truncated main file beside a
`.bak` or a complete `.new`, and junk beside a truncated `.new` and a `.bak`
all bring the same IDs back. With nothing complete the endpoints get
temporary IDs and the damaged file is not touched; with no file at all new
IDs are made and kept.

`build-aros-linux` lies inside this repository, which `~/AROS/contrib/extras`
links to, so mmake would scan the build tree as source and a full `make`
fails. After each `configure`, append `ignoredir build-aros-linux` to
`mmake.config` in every AROS build directory and delete `mmake.cache`.

The USB MIDI patches build for `raspi-aarch64` but have not been run with a
USB MIDI device.

The private endpoint core also compiles with the existing hosted
`x86_64-aros-gcc` and generated AROS headers using `-Wall -Wextra -Werror`.
The patch was generated and `git apply --check` verified against a clean
worktree containing the preceding thirteen patches; the worktree itself is
disposable and is not a build dependency.

## Network patches

`patches/net-series` lists patches that are independent of CAMD and of each
other in purpose. `make check-patches` applies both series. They were found
while running Bonami on hosted AROS (`tools/bonami-hosted.sh`).

| Patch | What it fixes |
|---|---|
| `aros-arostcp-netdb-unknown-keyword.patch` | An unknown keyword in a netdb file crashed the stack: `FindArg()`'s -1 was kept in an enum the compiler made unsigned and then used as an index into the table of parsers. |
| `aros-eth-unix-multicast.patch` | The hosted `eth.device` dropped every received multicast frame and refused `S2_MULTICAST`, `S2_ADDMULTICASTADDRESS` and `S2_DELMULTICASTADDRESS`. The host interface is opened promiscuous, so there is no filter to program. |
| `aros-arostcp-ipv4-multicast.patch` | IPv4 multicast on a SANA-II interface. `IFF_MULTICAST` was never set, and its bit was used for a private flag, now in `ss_cflags`; a group had no Ethernet address (RFC 1112 section 6.4); `ip_input()` had no case for a joined group; and a sender that chose its interface with `IP_MULTICAST_IF` still needed a route. |

