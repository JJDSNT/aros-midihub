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

### 1.2 `RethinkCAMD()` is a stub (verified)

The autodoc says "Not implemented" (`rethinkcamd.c`). CAMD scans `DEVS:Midi`
only in `InitCamd()`, so a driver installed later, such as the one Poseidon
writes when a USB MIDI device is plugged in, is ignored until CAMD is
reopened. The rescan patch implements `RethinkCAMD()` as a serialized scan
that loads new drivers; it should be upstreamed.

### 1.3 Drivers are never unloaded

A `DEVS:Midi` driver stays loaded with its clusters after its device is gone,
and its file cannot be replaced safely. Hot-plugged hardware needs a removal
lifecycle: mark the driver's ports as gone, detach clients' links from them
(their clusters can stay and wait, as MIDIHub routes do), then unload once no
port is open. This needs runtime tests with real devices.

### 1.4 Cluster names are garbage on 64-bit targets (verified, fixed)

`mysprintf()` (`strings.c`) read its variadic arguments from `&fmt+1`,
which only works where they are passed on the stack. On AArch64 and x86_64
every cluster name and driver path came out as garbage, so no program could
find a cluster by name. Found on a Raspberry Pi 3 running `raspi-aarch64`;
[the fix](../patches/aros-camd-names-64bit.patch) uses `VNewRawDoFmt()`
with a `va_list`.

## 2. Endpoint metadata

### 2.1 Link and cluster comments do nothing (verified)

`MLINK_Comment` is accepted by `SetMidiLinkAttrsA()` but ignored ("Not
implemented because it's not used"), and `GetMidiLinkAttrsA()` returns
nothing for it. `struct MidiLink` already has an `ml_ClusterComment` field
that is never filled.

Today a program cannot tell what a cluster is: MIDIHub.prefs guesses the
transport from the cluster's name, and has to ask `bluetooth.library`
separately to recognise BLE MIDI ports named after a device.

**Proposal:** implement `MLINK_Comment` and expose a cluster's comment, as
the original CAMD API intended. A short, conventional text such as
`"USB MIDI"`, `"Bluetooth LE MIDI"`, `"AppleMIDI"` or `"Software synth"`
would let any CAMD application label endpoints without knowing the
transports.

### 2.2 A richer description

Beyond a comment, applications would benefit from a few structured fields per
cluster: transport, the device or peer it belongs to, and whether it is
connected right now (a BLE device can be bound but away). This could be
`MLINK_` tags readable through `GetMidiLinkAttrsA()`, filled by the link's
owner. It would let MIDIHub.prefs drop transport-specific queries entirely.

## 3. Notification

### 3.1 Only clusters coming and going are reported (verified)

The cluster notification signals when a cluster is created or removed. It
does not signal when a link joins or leaves an existing cluster, so a view of
who sends to or receives from a cluster (MIDIHub.prefs' Direction column)
goes stale until something else refreshes it.

**Proposal:** signal the same listeners when a cluster's participants change,
or add a second notification with a flag saying what changed. The existing
`ClusterNotifyNode` could carry the reason without breaking old callers if
the new behaviour is opt-in.

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

**Proposal:** a CAMD-wide timestamp source, for example milliseconds from
`timer.device`'s `GetSysTime()` or an E-clock based counter, used when a node
does not provide its own, plus an optional delayed delivery (`PutMidi` with a
future timestamp). Delayed delivery is a larger change and could come later;
a common clock alone already lets receivers correlate events.

## 5. SysEx and errors

### 5.1 Oversized SysEx is dropped silently

`PutSysEx()` skips a receiver whose SysEx buffer is too small for the message
and sets a buffer-full error only when the buffer is large enough but full
(`putsysex.c` autodoc). The sender is not told, and a receiver that does not
check `GetMidiErr()` never learns of the loss. MIDIHub's bridges allocate
4 KB; a librarian dump can exceed that.

**Proposal:** report an error to the receiver in both cases, and document a
recommended minimum `MIDI_SysExSize`. A way to stream SysEx in parts would
remove the size limit altogether.

### 5.2 Errors are hard to observe

Overflow and parse errors (`CMEF_BufferFull`, `CMEF_RecvOverflow`,
`CMEF_MsgErr`) are per node and polled. Counters per cluster, readable by
monitoring tools, would make diagnosis much easier than adding debug output
to each client.

## 6. Driver interface

### 6.1 Virtual ports without a driver file

MIDIHub creates network, BLE and synth ports as ordinary CAMD clients, which
works but makes them look like applications: they have no
`DEVS:Midi`-style identity and are gone while their process is not running.
An API for a program to register a port that CAMD lists like a hardware port,
with the metadata from section 2, would give software and network endpoints
the same standing as USB ones without writing driver files at run time.

## Suggested order

1. Fix cluster names on 64-bit targets (1.4): without it CAMD is unusable
   there.
2. Lock the cluster notification list (1.1): small and safe.
3. Upstream the rescan patch (1.2).
4. Implement `MLINK_Comment` and cluster comments (2.1).
5. Notify participant changes (3.1).
6. A common timestamp source (4.1).
7. SysEx and error reporting (5).
8. Driver removal (1.3) and registered virtual ports (6.1), with hardware tests.
