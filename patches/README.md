# AROS BLE MIDI remaining patch

Upstream AROS commits `8ff80b6627` and `37313d8aa0` now provide the generic
GATT server, local service records, notifications, legacy LE advertising,
128-bit UUID discovery, and the `btgatt.class` Preferences UI. The former
three-patch BLE foundation in this repository has therefore been retired.

`aros-ble-midi-upstream-gaps.patch` is based on upstream commit
`37313d8aa0` (October 3, 2026) and now contains only the BLE MIDI gaps that
remain:

- encode and send ATT Write Command (`0x52`) for Write Without Response;
- route `BTPR_GATTWRITENORSP` and write-only endpoints through that operation;
- expose the negotiated ATT payload size instead of a fixed 20-byte value;
- include enabled 128-bit GATT service UUIDs in legacy advertising, which is
  required for iOS to discover the BLE MIDI service;
- attach an immutable value snapshot to every service-write event, so a later
  ATT packet cannot overwrite MIDI data before the class consumes the event,
  and name the device that wrote it (`BENA_Device`), so a class can keep
  per-connection state;
- queue outgoing notification snapshots until each active radio consumes
  them, instead of coalescing a MIDI burst into the latest value. The queue is
  bounded at 256 snapshots and reports backpressure instead of silently
  dropping data. A new value wakes the radio tasks immediately rather than
  waiting for their 100 ms maintenance tick;
- accept peripheral-role SMP pairing after explicit user consent, including
  Legacy Just Works and Secure Connections Just Works, Numeric Comparison,
  and Passkey Entry; answer
  controller LTK requests, distribute bonding keys, and reuse stored keys on
  reconnection;
- add the L2CAP LE Connection Parameter Update procedure. As the central the
  stack grants a peripheral's request through HCI LE Connection Update; as
  the peripheral it asks for the shortest interval preferred by an enabled
  service (`BSRA_LEConnInterval`), within the limits iOS accepts.
  `btmidi.class` requests 15 ms only while its service is enabled; the legacy
  stack-wide `BSA_LEConnInterval` remains available;
- answer the LE Remote Connection Parameter Request event (subevent 0x06)
  that a controller raises when the peer starts the link-layer Connection
  Parameters Request procedure: valid parameters are accepted, others are
  refused. Before, the event was ignored and the procedure could time out
  with the link; on a Pi 3 an iPhone's connections dropped with "connection
  timeout". The Connection Update Complete event (0x03) is now logged with
  the interval in effect;
- report the largest notification payload every subscribed LE device receives
  whole (`BSA_LENotifyPayload`), so an unrelated connected device with the
  default MTU does not shrink BLE MIDI packets;
- add `BGDP_STREAM` for characteristics whose value is a stream of events:
  a read returns no data, as the BLE MIDI specification requires.

It also fixes two generic SMP issues that affect the central role as well:

- the IO capability table selected Just Works instead of Passkey Entry when a
  KeyboardDisplay device met a DisplayOnly or DisplayYesNo device;
- an initiator rejected a responder's Secure Connections commitment that
  arrived before its own controller reported the DHKey. It now holds the
  commitment until the DHKey is ready.

Phase 3 key distribution follows the Core specification in both roles: the
responder sends its keys first, and the initiator answers once it has
received them.

The modified AROS files and their diff context are covered by the AROS Public
License 1.1; see [AROS-LICENSE](AROS-LICENSE). MIDIHub's own source remains
under its existing license. The patch advances `bluetooth.library` to 45.19
because it adds event data tags used by `btmidi.class`.

Apply it to the updated `~/AROS` checkout, without changing Bellatrix:

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-upstream-gaps.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-upstream-gaps.patch"
make -C ~/AROS/rom/bluetooth/stack -f Makefile.host test
```

The patch applies cleanly to the stated commit. The sanitizer-enabled host
suite passes `2530/2530` checks, including new ATT, GATT, and SMP responder
tests. The SMP tests also run an initiator against a responder, check the
full IO capability table, and check the Phase 3 order. The changed `bluetooth.library` sources and `btmidi.class` also pass an
m68k AROS syntax build.

`btmidi.class` uses the upstream service-record API. It registers the BLE MIDI
service with `btAddServiceRecord()`, receives incoming values through
`BEHMB_SERVICEWRITE`, and publishes outgoing packets with
`btSetServiceValue()`. `btgatt.class` owns radio advertising and the list of
enabled services in Bluetooth Preferences.

Peripheral-role pairing falls back to Legacy Just Works by default, but the
standard Bluetooth pairing popup must still explicitly accept the peer. With
the existing `btlesc` boot argument, capable controllers and peers negotiate
Secure Connections, including Numeric Comparison and Passkey Entry. Central-
and peripheral-role long-term keys are persisted separately. OOB pairing
remains unsupported.

## Raspberry Pi on-board Bluetooth

Upstream AROS has the Raspberry Pi 3's on-board Bluetooth transport
(`pl011bt.resource`, `h4bthci.device`, `brcmbt.fwl`). The transport went in
before the stack had its firmware-loader mechanism (loaders in
`DEVS:Bluetooth/FWLoaders/`, bound by `BTStackLoader`), so there was nothing
to load a patchram with yet. Now that the mechanism exists, the patchram
download completes the radio's support; without it the radio gets no real
address. It is independent of BLE
MIDI, meant for upstream, and applies on its own to upstream commit
`37313d8aa0`.

`aros-raspi-bt-firmware.patch` adds `distfiles-raspi-aarch64-bt-fw` to
`arch/aarch64-raspi/boot/mmakefile.src`, beside the WiFi firmware download,
and makes `distfiles-raspi-aarch64le` depend on it. It fetches the patchram
`brcmbt.fwl` loads from `DEVS:Firmware/brcm/`: `BCM43430A1.hcd` (Pi 3, Zero W)
and `BCM4345C0.hcd` (Pi 3B+), from the Raspberry Pi `bluez-firmware`
repository. Without it a BCM43430A1 runs from ROM and reports the
placeholder address `AA:AA:AA:AA:AA:AA`.

The radio itself is registered once in Bluetooth Preferences (hardware
page: `DEVS:Bluetooth/h4bthci.device`, unit 0, then Save). `BTStackLoader`
brings up the saved radios on every boot, after binding the firmware
loaders, so the patchram is applied during the radio's own bring-up. A line
in `S:Startup-Sequence` would do the same for every target to serve one, so
none is added.

```sh
git -C ~/AROS apply "$PWD/patches/aros-raspi-bt-firmware.patch"
```

### Debug console on the mini-UART

`aros-raspi-console-miniuart.patch` is needed for the on-board radio to work
on `raspi-aarch64`. The bootstrap (`serialdebug.c`) and the kernel
(`kernel_startup.c`, `bcm27xx_ser_putc`) printed their serial log through
the PL011 and muxed GPIO 14/15 to it. On a Pi 3, 3B+ and Zero 2 W that
PL011 belongs to the Bluetooth radio, which `pl011bt.resource` puts on GPIO
30-33. Two pins then fed the PL011's receive line, and every kernel `bug()`
was sent to the radio. On the first test the radio never answered
`HCI_Reset` (`init step 0 -> timeout`).

The patch follows the firmware's routing, as Linux does. When the device
tree's `serial0` alias names the mini-UART (`/soc/serial@7e215040`), the
bootstrap prints through the AUX mini-UART the firmware has set up
(`enable_uart=1`, 115200, GPIO 14/15). It leaves the PL011 and its pins
alone, and passes the mini-UART's address in `KRN_DebugUartBase`, which the
kernel recognises for its early and later output. The serial cable stays on
the same pins at the same speed. Waits on the mini-UART are bounded, so a
disabled one cannot hang the boot. A Pi 2, or a Pi 3 with
`dtoverlay=disable-bt`, keeps the PL011 console.

```sh
git -C ~/AROS apply "$PWD/patches/aros-raspi-console-miniuart.patch"
```

## llvmpipe link order

`aros-llvmpipe-link.patch` is a build fix found while building
`raspi-aarch64` with `--with-toolchain=llvm`. It is unrelated to MIDI and
meant for upstream.

`workbench/hidds/llvmpipe` links the target-side LLVM archives as `-lLLVM…`
names. `TARGET_CXX_LDFLAGS` puts `-L$(CROSSTOOLSDIR)/lib` first, and when
the AROS toolchain itself was built with LLVM that directory also holds the
host's `libLLVM*.a`. The linker then picks those, and the link fails with
"is incompatible with aarch64elf". The patch passes the archives from
`$(AROS_DEVELOPER)/lib` by path, which the `mmakefile` already globs, so no
search order is involved. With it, `llvmpipe.hidd` links.

```sh
git -C ~/AROS apply "$PWD/patches/aros-llvmpipe-link.patch"
```

## CAMD and USB MIDI

Eighteen patches, applied in this order to upstream `master`. The first eleven
are commits of the `camd-robustness` branch in `~/AROS`; the link-comment and
SysEx/error patches follow that branch. The first two are also draft pull request #1483.
The modified AROS sources are covered by [AROS-LICENSE](AROS-LICENSE).

| patch | fixes |
|---|---|
| `aros-camd-names-64bit.patch` | cluster names and driver paths are garbage on AArch64 and x86_64 |
| `aros-camd-arena-segments.patch` | the driver scan runs off the end of memory for arena-loaded modules |
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

```sh
grep -v '^#' patches/camd-series | while read -r p; do
    git -C ~/AROS apply "$PWD/patches/aros-$p.patch" || break
done
```

[`camd-series`](camd-series) is the apply order.
[`tools/check-camd-patches.sh`](../tools/check-camd-patches.sh) (`make
check-patches`) applies it to a clean index of `~/AROS` and reports any CAMD
change in that tree which the series lacks. Generate a new patch as the
difference between that index and the tree, not by hand.

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
| all patches | 60 of 60 | 34 of 34 | 8 of 8 |

`MIDIHubCAMDCompat --v43` is a client of the provisional endpoint functions: 26
checks on hosted AROS, and 26 again after `--v43-leak` exits with a session
open and `Avail FLUSH` tries to expunge the library. Its session and a legacy link send to the same
debugdriver port, and the runner compares the bytes the driver printed.

Use the build tree's `bin/raspi-aarch64/AROS` as `SD_DIR`; an older copy with a
`debugdriver` from before `debugdriver-port-index` crashes on the first send.

[`tools/camd-compat-hosted.sh`](../tools/camd-compat-hosted.sh) runs the same
three passes on Linux-hosted AROS in a few seconds, from a copy of
`build-aros-linux/bin/linux-x86_64/AROS`, with the same results. Both scripts
also check that the endpoint IDs the first boot stores are the ones the second
boot publishes.

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
