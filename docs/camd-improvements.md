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

**Proposal:** take `CLSemaphore` in both functions. This is a small, safe fix
and matters to every program that watches clusters, including MIDIHub.prefs
and `MIDIHubRouter`.

**Compatibility:** safe. It changes no public layout or vector and only makes
the existing list operation atomic. `EndClusterNotify()` must not return
until a notification traversal that already holds `CLSemaphore` has ended.

### 1.2 `RethinkCAMD()` is a stub (verified)

The autodoc says "Not implemented" (`rethinkcamd.c`). CAMD scans `DEVS:Midi`
only in `InitCamd()`, so a driver installed later, such as the one Poseidon
writes when a USB MIDI device is plugged in, is ignored until CAMD is
reopened. The rescan patch implements `RethinkCAMD()` as a serialized scan
that loads new drivers; it should be upstreamed.

**Compatibility:** safe as an implementation of an existing, documented
stub at its existing vector. A rescan should remain additive: load newly
discovered drivers but do not unload an already loaded driver.

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

**Proposal:** implement the existing `MLINK_Comment` as a per-link property.
A short, conventional text such as
`"USB MIDI"`, `"Bluetooth LE MIDI"`, `"AppleMIDI"` or `"Software synth"`
would let any CAMD application label endpoints without knowing the
transports.

**Compatibility:** safe if CAMD follows the same ownership rule as
`MLINK_Name`: store the caller-owned string pointer in the existing
`ml_ClusterComment` field, return it through `GetMidiLinkAttrsA()`, and never
free it. The caller must keep the string valid until the link is removed or
the value is replaced. Despite the field's historical name, this is a link
comment; different links in one cluster may supply different values.

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

**Proposal:** preserve the current meaning of `StartClusterNotify()` and add
a second, versioned notification API for cluster creation, removal and
participant changes. It can use an opaque subscription handle or a new
structure passed only to the new functions, and must report an event reason.

**Compatibility:** do not add a reason field to `ClusterNotifyNode`: old
programs allocate the 41.1 size and CAMD would write past the allocation.
Signalling legacy listeners on participant changes would also be a behaviour
change, even without a layout change. A new appended API avoids both breaks.

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

`PutSysEx()` skips a receiver whose SysEx buffer is too small for the message
and sets a buffer-full error only when the buffer is large enough but full
(`putsysex.c` autodoc). The sender is not told, and a receiver that does not
check `GetMidiErr()` never learns of the loss. MIDIHub's bridges allocate
4 KB; a librarian dump can exceed that.

**Proposal:** report the existing `CMEF_SysExTooBig` error to a receiver whose
buffer cannot hold the message, honour `MIDI_ErrFilter`, and document a
recommended minimum `MIDI_SysExSize`. A new streaming API can remove the size
limit altogether.

**Compatibility:** keep the signatures and delivery rules of `PutSysEx()` and
`GetSysEx()`. Error reporting is observable because `WaitMidi()` returns
`FALSE` while an error is pending, so it must use the existing error/filter
contract and be called out as a bug fix. Streaming requires new appended
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

The existing patches for 64-bit names, arena-loaded modules and
`RethinkCAMD()`, plus the notification-list lock, preserve the CAMD 41.1 ABI.
`MLINK_Comment` can also be completed without an ABI change because both its
tag and storage field already exist.

Driver unload, richer cluster metadata, detailed notifications, a CAMD clock,
scheduled delivery, streaming SysEx, counters and virtual ports are compatible
only with the constraints above. In particular, they must use private state,
new tags or vectors appended after the existing table. They must not enlarge
public structures or silently change the behaviour of legacy calls.

## Suggested order

1. Fix cluster names on 64-bit targets (1.4) and the driver scan of
   arena-loaded modules (1.5): without them CAMD is unusable or crashes.
2. Lock the cluster notification list (1.1): small and safe.
3. Upstream the rescan patch (1.2).
4. Implement the existing per-link `MLINK_Comment` (2.1).
5. Add versioned participant notifications (3.1).
6. Add an opt-in common timestamp source (4.1).
7. SysEx and error reporting (5).
8. Driver removal (1.3) and registered virtual ports (6.1), with hardware tests.
