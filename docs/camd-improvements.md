# Possible CAMD improvements

MIDIHub builds on `camd.library` (AROS `workbench/libs/camd/`) instead of
adding a parallel MIDI layer. Work on USB MIDI, BLE MIDI, the router and
MIDIHub.prefs has shown where CAMD itself limits that design. This document
proposes improvements for upstream AROS, ordered by how much they would help.
Each item says what is there today, why it matters, and what a change could
look like. Items marked **verified** were checked against the CAMD source at
upstream commit `37313d8aa0`; the others are design proposals.

[CAMD integration](camd-integration.md) describes how MIDIHub uses CAMD now.
Two fixes already exist as patches in this repository:
[the CAMD rescan patch](../patches/aros-camd-rescan.patch) and
[the USB MIDI CAMD fix](../patches/aros-usb-midi-camd.patch).

## Compatibility contract

These changes must preserve four kinds of compatibility with CAMD 41.1:

- **Binary ABI:** keep every existing library vector at the same offset, with
  the same register assignment and signature. Add new functions only after
  `Midi2Driver()`, the current last vector.
- **Public data ABI:** do not change the size or layout of `MidiNode`,
  `MidiLink`, `MidiCluster`, `ClusterNotifyNode`, `MidiMsg` or
  `MidiDeviceData`. Programs allocate some of these structures themselves.
  New state belongs in CAMD's private structures or an opaque object.
- **Source API:** do not renumber existing tags, flags or error bits. Append
  new identifiers, and let clients check the library version before using
  them.
- **Behaviour:** existing calls keep their present meaning. Behaviour that
  changes notification frequency, timestamp values, cluster lifetime or
  delivery order must be enabled through a new tag or a new function.

An extension should bump CAMD's minor version, while old calls continue to
behave as they do in 41.1. This is stricter than merely keeping programs
linkable: a program compiled for 41.1 must still run without having its
timing or notification assumptions changed.

## 1. Correctness

### 1.1 Cluster notification list is not locked (verified)

`StartClusterNotify()` and `EndClusterNotify()` add and remove the caller's
`ClusterNotifyNode` with plain `AddTail()`/`Remove()` and no lock
(`startclusternotify.c`, `endclusternotify.c`). `NewCluster()` and
`RemoveCluster()` walk the same list to signal every listener while holding
`CLSemaphore` (`clusters.c`). A program that starts or ends notification
while another task creates or removes a cluster can corrupt the list or have
a listener signalled after its node was removed.

**Proposal:** protect the list with a new private semaphore that
`StartClusterNotify()`, `EndClusterNotify()` and the two traversals in
`clusters.c` take. The traversals already hold `CLSemaphore`, so the lock
order is `CLSemaphore` first, then the notification semaphore. This is a
small fix and matters to every program that watches clusters, including
MIDIHub.prefs and `MIDIHubRouter`.

**Compatibility:** safe with a separate semaphore. It changes no public
layout or vector, and `EndClusterNotify()` cannot return while a traversal
is signalling the node. Taking `CLSemaphore` itself in the two functions
would not be safe: `LockCAMD()` holds `CLSemaphore` shared, exec cannot
upgrade a shared hold to an exclusive one, and a program that calls
`StartClusterNotify()` between `LockCAMD()` and `UnlockCAMD()`, which works
today, would deadlock.

### 1.2 `RethinkCAMD()` is a stub (verified)

The autodoc says "Not implemented" (`rethinkcamd.c`). CAMD scans `DEVS:Midi`
only in `InitCamd()`, so a driver installed later, such as the one Poseidon
writes when a USB MIDI device is plugged in, is ignored until CAMD is
reopened. The rescan patch implements `RethinkCAMD()` as a serialized scan
that loads new drivers; it should be upstreamed.

**Compatibility:** safe as an implementation of an existing, documented
stub at its existing vector. A rescan should remain additive: load newly
discovered drivers but do not unload an already loaded driver.

The patch as it stands has two problems that only appear at run time,
because at `InitCamd()` no client exists yet:

- `LoadDriver()` builds the driver's clusters through `AllocDriverData()`,
  which calls `NewCluster()` and `AddClusterSender()`/`AddClusterReceiver()`
  without `CLSemaphore`. During a rescan other tasks may be walking or
  changing the cluster list. The rescan must hold `CLSemaphore` exclusively
  around `AllocDriverData()` (not around `LoadSeg()` or the driver's
  `Init()`).
- `NewCluster()` does not look for an existing cluster of the same name. If
  a client already linked to `usbmidi.out.0`, for example a MIDIHub route
  waiting for its device, the rescan creates a second cluster with that
  name; `FindCluster()` returns the first one and the waiting client never
  reaches the hardware. The driver must join the existing cluster, and on
  failure `FreeDriverData()` must remove only the clusters it created. This
  path needs a runtime test.

### 1.3 Drivers are never unloaded

A `DEVS:Midi` driver stays loaded with its clusters after its device is gone,
and its file cannot be replaced safely. Hot-plugged hardware needs a removal
lifecycle: mark the driver's ports as gone, detach clients' links from them
(their clusters can stay and wait, as MIDIHub routes do), then unload once no
port is open. This needs runtime tests with real devices.

**Compatibility:** requires a new, explicit lifecycle. `RethinkCAMD()` must
not start unloading drivers as a side effect. Before `UnLoadSeg()`, CAMD must
stop callbacks, close every port, remove every reference to the driver's
`MidiDeviceData` and wait for its internal reference count to reach zero.
The public cluster lifetime visible to old clients must not change.

### 1.4 Cluster names are garbage on 64-bit targets (verified, fixed)

`mysprintf()` (`strings.c`) read its variadic arguments from `&fmt+1`,
which only works where they are passed on the stack. On AArch64 and x86_64
every cluster name and driver path came out as garbage, so no program could
find a cluster by name. Found on a Raspberry Pi 3 running `raspi-aarch64`;
[the fix](../patches/aros-camd-names-64bit.patch) uses `VNewRawDoFmt()`
with a `va_list`.

**Compatibility:** safe. This changes an internal formatting helper and
restores the intended names on targets whose ABI passes variadic arguments
in registers.

### 1.5 Driver scan breaks on arena-loaded modules (verified, fixed)

`LoadDriver()` (`openmididevice.c`) read each hunk's size before its
`BPTR` and scanned it for `MidiDeviceData`. Modules loaded into one arena
(`ELF_MODULE_ARENA`) store a size of 0 in each section hunk and the real
size in a container hunk; the subtraction wrapped and the scan ran off the
end of memory. [The fix](../patches/aros-camd-arena-segments.patch) skips
hunks no larger than their header. A cleaner long-term design would be for
a driver to export its `MidiDeviceData` through a symbol or a resident
structure rather than being found by scanning memory.

**Compatibility:** safe. The patch adds internal bounds checks and does not
change the driver or library interface.

## 2. Endpoint metadata

### 2.1 Link comments do nothing (verified)

`MLINK_Comment` is accepted by `SetMidiLinkAttrsA()` but ignored ("Not
implemented because it's not used"), and `GetMidiLinkAttrsA()` returns
nothing for it. `struct MidiLink` already has an `ml_ClusterComment` field
that is never filled.

Today a program cannot tell what a cluster is: MIDIHub.prefs guesses the
transport from the cluster's name, and has to ask `bluetooth.library`
separately to recognise BLE MIDI ports named after a device.

**Proposal:** implement `MLINK_Comment` as the original CAMD autodoc
defines it: "the highest priority MidiLink in a MidiCluster has its comment
field copied to the MidiCluster's comment field". It is a cluster comment
supplied by links, which is what the field name `ml_ClusterComment` says.
A short, conventional text such as
`"USB MIDI"`, `"Bluetooth LE MIDI"`, `"AppleMIDI"` or `"Software synth"`
would let any CAMD application label endpoints without knowing the
transports.

**Compatibility:** safe without an ABI change. Each link keeps its
caller-owned pointer in `ml_ClusterComment`, with the same ownership rule as
`MLINK_Name` (CAMD never frees it). The cluster's comment lives in private
storage in `MyMidiCluster`, copied (up to the documented 34 characters) from
the highest-priority link whenever a link joins, leaves or changes priority
or comment. A driver's own clusters can carry a fixed comment. Reading it
needs a getter: `GetMidiLinkAttrsA(MLINK_Comment)` returning the cluster's
comment, as Amiga programs expect, plus the cluster getter of 2.2 for
programs that have no link in the cluster. Treating it as a per-link value,
with different values in one cluster, would contradict the original
semantics.

### 2.2 A richer description

Beyond a comment, applications would benefit from a few structured fields:
transport, the device or peer an endpoint belongs to, and whether it is
connected right now (a BLE device can be bound but away). Per-link fields can
be appended as new `MLINK_` tags. Cluster-wide fields need a new appended
`GetMidiClusterAttrsA()` function and private storage in `MyMidiCluster`. It
would let MIDIHub.prefs drop transport-specific queries entirely.

**Compatibility:** safe under those constraints. Do not append fields to the
public `MidiLink` or `MidiCluster` structures. An old library will ignore an
unknown setter tag, so a client that needs the value must check the CAMD
version or the getter's result.

## 3. Notification

### 3.1 Only clusters coming and going are reported (verified)

The cluster notification signals when a cluster is created or removed. It
does not signal when a link joins or leaves an existing cluster, so a view of
who sends to or receives from a cluster (MIDIHub.prefs' Direction column)
goes stale until something else refreshes it.

CAMD already has a documented API for participant changes, which AROS
accepts but never fires: `MIDI_PartHook` ("called whenever any of the
clusters that this node is linked to either adds or removes a member") and
`MIDI_PartSignal`, plus the `MLF_PartChange` link flag. `mcl_Participants`
and `mcl_PublicParticipants` are never maintained either.

**Proposal:** first implement `MIDI_PartHook`, `MIDI_PartSignal` and the
participant counts as documented. That covers a program watching the
clusters it is linked to, such as `MIDIHubRouter`. For a program that watches
every cluster without linking to them (MIDIHub.prefs), keep the current
meaning of `StartClusterNotify()` and add a second, versioned notification
API for cluster creation, removal and participant changes, with an opaque
subscription handle and an event reason.

**Compatibility:** implementing the part hook and signal fulfils an existing
contract: only nodes that set them are called, and they default to none and
-1. Do not add a reason field to `ClusterNotifyNode`: old programs allocate
the 41.1 size and CAMD would write past the allocation. Signalling legacy
cluster listeners on participant changes would also be a behaviour change.

## 4. Timing

### 4.1 Timestamps have no clock (verified)

`MIDI_TimeStamp` takes a pointer to a `ULONG` that the client keeps up to
date itself; `CreateMidiA()` points it at a dummy value
(`createmidia.c`). Messages carry whatever that variable holds, and CAMD
neither supplies a clock nor schedules delivery.

Network and BLE MIDI both deliver packets with sender timestamps: BLE MIDI
packets carry a 13-bit millisecond clock. Without a shared time base in CAMD
those timestamps cannot be used to remove transport jitter (see open work in
[BLE MIDI](ble-midi.md)).

**Proposal:** add an opt-in CAMD-wide timestamp source, for example
milliseconds from `timer.device`'s `GetSysTime()` or an E-clock based
counter. Keep the dummy zero value for nodes that do not request the new
clock. Specify that the clock is monotonic, define its 32-bit wraparound, and
expose a query function so transports can convert their own timestamps.
Delayed delivery should be a separate new function; `PutMidi()` must retain
its immediate-delivery semantics.

**Compatibility:** changing the default timestamp or making `PutMidi()` wait
would be a behavioural break. The opt-in tag and appended functions preserve
the 41.1 behaviour.

## 5. SysEx and errors

### 5.1 Oversized SysEx is dropped silently

The autodoc (AROS and Amiga) says `PutSysEx()` does not send a message to a
receiver whose SysEx buffer is too small, and sets `CMEF_SysExFull` only when
the buffer is large enough but full. The code (`sysexdistr.c`) has no size
check: an oversized message fills the ring, `CMEF_SysExFull` is set and the
partial message is dropped. `CMEF_SysExTooBig` is never set, and
`mi_ErrFilter` is stored but never applied. The sender is not told either
way. MIDIHub's bridges allocate 4 KB; a librarian dump can exceed that.

**Proposal:** implement the documented behaviour: skip a receiver whose
buffer cannot hold the message and flag it with `CMEF_SysExTooBig`; apply
`MIDI_ErrFilter` when setting error bits; document a recommended minimum
`MIDI_SysExSize`. A new streaming API can remove the size limit altogether.

**Compatibility:** keep the signatures and delivery rules of `PutSysEx()` and
`GetSysEx()`. Error reporting is observable because `WaitMidi()` returns
`FALSE` while an error is pending. An oversized message already produces an
error today (`CMEF_SysExFull`), so reporting `CMEF_SysExTooBig` instead only
changes which bit is set. Applying `MIDI_ErrFilter` hides errors that
programs see today, but only from programs that asked for it; both are bug
fixes towards the documented contract and should be called out as such. Streaming requires new appended
functions rather than changing the meaning of a partial SysEx passed to
`PutSysEx()`.

### 5.2 Errors are hard to observe

Overflow and parse errors (`CMEF_BufferFull`, `CMEF_RecvOverflow`,
`CMEF_MsgErr`) are per node and polled. Counters per cluster, readable by
monitoring tools, would make diagnosis much easier than adding debug output
to each client.

**Compatibility:** keep counters in private data and expose them through a
new query function or the proposed cluster getter. Do not extend a public
structure or change when `GetMidiErr()` clears the per-node error bits.

## 6. Driver interface

### 6.1 Virtual ports without a driver file

MIDIHub creates network, BLE and synth ports as ordinary CAMD clients, which
works but makes them look like applications: they have no
`DEVS:Midi`-style identity and are gone while their process is not running.
An API for a program to register a port that CAMD lists like a hardware port,
with the metadata from section 2, would give software and network endpoints
the same standing as USB ones without writing driver files at run time.

**Compatibility:** use new appended functions and opaque handles. Existing
application links must keep their current identity and cluster lifetime;
only clients that call the registration API acquire the new port semantics.

## Compatibility verdict

The patches for 64-bit names and arena-loaded modules preserve the CAMD 41.1
ABI and behaviour. The `RethinkCAMD()` patch preserves the ABI but needs the
locking and same-name cluster fixes of 1.2 before it is upstreamed. The
notification-list lock is safe only with its own semaphore (1.1).
`MLINK_Comment` and the participant hook and signal can be completed without
an ABI change, because their tags and fields already exist, provided they
follow the original CAMD semantics.

Driver unload, richer cluster metadata, detailed notifications, a CAMD clock,
scheduled delivery, streaming SysEx, counters and virtual ports are compatible
only with the constraints above. In particular, they must use private state,
new tags or vectors appended after the existing table. They must not enlarge
public structures or silently change the behaviour of legacy calls.

## Suggested order

1. Fix cluster names on 64-bit targets (1.4) and the driver scan of
   arena-loaded modules (1.5): without them CAMD is unusable or crashes.
2. Lock the cluster notification list with its own semaphore (1.1).
3. Fix the rescan patch's locking and same-name clusters, then upstream it
   (1.2).
4. Implement `MLINK_Comment` as the cluster comment (2.1).
5. Implement `MIDI_PartHook`/`MIDI_PartSignal` (3.1), then add versioned
   notifications for programs that watch every cluster.
6. Add an opt-in common timestamp source (4.1).
7. SysEx and error reporting (5).
8. Driver removal (1.3) and registered virtual ports (6.1), with hardware tests.
