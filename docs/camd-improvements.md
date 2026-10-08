# Possible CAMD improvements

MIDIHub builds on `camd.library` (AROS `workbench/libs/camd/`) instead of
adding a parallel MIDI layer. Work on USB MIDI, BLE MIDI, the router and
MIDIHub.prefs has shown where CAMD itself limits that design. This document
proposes improvements for upstream AROS, ordered by how much they would help.
Each item says what is there today, why it matters, and what a change could
look like. Items marked **verified** were checked against the CAMD source at
upstream commit `37313d8aa0`; the others are design proposals.

[CAMD integration](camd-integration.md) describes how MIDIHub uses CAMD now.

## Status

Implemented, as the `camd-robustness` branch in `~/AROS` and as patches in
this repository ([the patch guide](../patches/README.md#camd-and-usb-midi)
lists them in order): section 1, the USB MIDI driver lifecycle, participant
notification (step 3), version 42 with `GetClusterAttrsA()`, `CamdTime()`
and `MIDI_SystemClock` (step 4), and cluster watches (step 5). Steps 1 and 2
(`MLINK_Comment`, SysEx size and the error filter) are not:
[Implementing the rest](#implementing-the-rest) gives their design.

Every change to CAMD must keep passing the compatibility suite
(`ports/aros/camd_compat.c`, `MIDIHubCAMDCompat`). It checks the 41.1
behaviour that programs rely on: nodes, links, clusters, message delivery
and filters, SysEx, errors, `ParseMidi()`, cluster notification, driver
ports, and that notification works under `LockCAMD()`.
[`tools/camd-compat-qemu.sh`](../tools/camd-compat-qemu.sh) runs it on
`raspi-aarch64` under QEMU:

```sh
tools/camd-compat-qemu.sh CAMD_LIBRARY MIDIHubCAMDCompat SD_DIR
```

The first boot runs the 41.1 contract and, with a camd.library 42, the
checks of what 42 adds (`--v42`); the second runs the `RethinkCAMD()` checks.
Run it against the library before the change and after it. A check that
passes before must pass after; a check whose result a change alters on
purpose (for example `GetMidiLinkAttrsA does not count MLINK_Comment` once
2.1 is done) is changed in the same commit, and the commit says why.

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

### 1.1 Cluster notification list is not locked (verified, fixed)

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

### 1.2 `RethinkCAMD()` is a stub (verified, fixed)

The autodoc says "Not implemented" (`rethinkcamd.c`). CAMD scans `DEVS:Midi`
only in `InitCamd()`, so a driver installed later, such as the one Poseidon
writes when a USB MIDI device is plugged in, is ignored until CAMD is
reopened. The rescan patch implements `RethinkCAMD()` as a serialized scan
that loads new drivers; it should be upstreamed.

**Compatibility:** safe as an implementation of an existing, documented
stub at its existing vector. A rescan should remain additive: load newly
discovered drivers but do not unload an already loaded driver.

The first version of the rescan patch had two problems that only appear at
run time, because at `InitCamd()` no client exists yet. Both are fixed, and
the suite's `--rethink` run covers them:

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

### 1.3 Hot-plugged devices (done differently: no unloading)

The first version of this item asked for drivers to be unloaded when their
device goes away. The USB MIDI driver shows that this is not what hot-plug
needs. `camdusbmidi.class` writes one small driver per device to
`DEVS:Midi`; the driver only forwards to the class, so it can stay loaded
for good. What has to survive unplugging is the port, and that belongs in
the class, not in CAMD:

- a port opens whether the device is there or not, so clients can link and
  wait;
- while the device is away the port drops what it is sent;
- when the device comes back the same ports carry MIDI again, without
  clients linking again.

This is implemented in `aros-usb-midi-lifecycle.patch`, together with the
bug that kept USB MIDI from ever appearing outside m68k: the driver file was
named after the device but the driver inside was named `poseidonusb`, and
CAMD rejects a driver whose name is not its file name.

Unloading would only matter for replacing a driver file at run time. It
needs callbacks stopped, every port closed and every reference to the
driver's `MidiDeviceData` gone before `UnLoadSeg()`, and nothing needs it
yet. It is not planned.

What CAMD still lacks for hot-plug is a way to tell clients whether a port
has a device behind it right now. That is the `connected` field of 2.2.

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

### 1.6 No driver loads on AArch64 (verified, fixed)

Found by the compatibility suite under QEMU, after 1.5. The scan stepped
through each hunk in `AROS_PTRALIGN` (8-byte) steps starting right after the
12-byte hunk header, so on 64-bit targets every address it tried was 4 bytes
off the aligned `MidiDeviceData`. No `DEVS:Midi` driver loaded on
`raspi-aarch64`; the log said `LoadDriver - It was not a success`. The scan
now starts at the first aligned address
([the fix](../patches/aros-camd-driver-scan-align.patch)).

Once drivers loaded, `debugdriver` itself crashed on the first message: it
stored port 0's user data at `UserData[-1]`, over its transmit function
pointer ([the fix](../patches/aros-debugdriver-port-index.patch)).

### 1.7 A port closed under a linked receiver (verified, fixed)

A driver port is opened by the first link to join its clusters, and CAMD
recorded the link's direction only then. A receiver that linked while the
port was already open for a sender was not recorded, and when the sender
left CAMD closed the port under it
([the fix](../patches/aros-camd-port-open.patch)).

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

## 6. Driver and endpoint interface

The fixed CAMD driver model is now a concrete limitation rather than only a
virtual-port concern. In the current implementation, `LoadDriver()` copies
`MidiDeviceData.NPorts` once and `AllocDriverData()` allocates a fixed
`driverdatas` array plus the corresponding input/output clusters. There is
no supported operation for a loaded driver to add or remove one of those
ports later.

`RethinkCAMD()` solves a different problem: it lets a complete driver appear
after CAMD has started. It does not let an already loaded driver change its
endpoint set. AppleMIDI peers, future Network MIDI 2.0 endpoints and other
discoverable transports make that distinction important.

The design direction is therefore an additive **dynamic endpoint lifecycle**,
not a change in the meaning of `NPorts`.

### 6.1 Virtual endpoints without a driver file

MIDIHub creates network, BLE and synth endpoints as ordinary CAMD clients,
which works but makes them look like applications: they have no
`DEVS:Midi`-style identity and are gone while their process is not running.
A future API could let software register a CAMD-visible endpoint with the
metadata from section 2, giving software endpoints the same standing as
driver-backed ones without writing driver files at run time.

This should be designed together with dynamic driver endpoints rather than as
an unrelated virtual-port mechanism. Both need identity, metadata, lifecycle,
notification and a representation that remains compatible with ordinary CAMD
clusters.

### 6.2 Dynamic endpoint registration

A loaded driver should eventually be able to expose endpoints discovered
after its initial `Init()`. The motivating case is a long-lived
`applemidi.device`: network peers may appear and disappear without the
device itself being reloaded.

Do not make `MidiDeviceData.NPorts` variable. It retains its 41.1 meaning:
the number of legacy fixed ports established during driver initialization.
Dynamic endpoints should instead use a versioned extension backed by
CAMD-private state or opaque handles.

The API names are deliberately left open. Before choosing calls such as
`Register...()` or `Unregister...()`, the model must establish:

- stable endpoint identity independent of a transient connection;
- direction and capabilities;
- human-readable and structured metadata;
- mapping to legacy CAMD clusters so old applications can use the endpoint;
- safe ownership and locking while clients hold links;
- notification of endpoint creation and state changes.

A fixed pool of `NPorts` with peer-to-port mapping remains a compatibility
fallback for a transport whose target CAMD does not provide this extension;
it should not define the long-term architecture.

### 6.3 Endpoint lifecycle and identity

Discovery, connectivity and existence are different states. A known endpoint
should not necessarily be destroyed because its transport is temporarily
unavailable. A useful lifecycle must be able to represent at least the
distinction between a registered endpoint that is connected, one that is
temporarily disconnected/inactive, and one that has actually been
unregistered.

That distinction matters to persistent routing: a saved route can continue
to refer to the same endpoint identity while a peer is offline and resume
when it returns. It also avoids unnecessary destruction and recreation of
clusters and links during transient network or radio loss.

The current `RemoveCluster()` frees the cluster immediately. Dynamic endpoint
removal therefore cannot simply expose that operation to drivers: the design
must specify what happens to existing links/references and which locks protect
the transition. Prefer state changes such as connected/disconnected where
identity should survive; reserve unregister/removal for actual endpoint
retirement.

The structured `connected` metadata discussed in section 2.2 belongs to this
lifecycle. It should be supplied through the new extension without growing
`MidiDeviceData`.

### 6.4 Endpoint notification versus cluster notification

The version 42 `ClusterWatch` remains deliberately scoped to the legacy CAMD
graph: `CWE_Added`, `CWE_Removed` and `CWE_Participants` describe changes
to clusters and their membership. Dynamic endpoint lifecycle must not broaden
that contract into a generic `CWE_Changed` event.

An endpoint can change state or metadata while its legacy CAMD clusters remain
unchanged. For example, a known AppleMIDI peer can move from connected to
disconnected while retaining its stable identity and cluster projection.
Treating that as a cluster change would mix two object models and make the
meaning of `ClusterWatch` ambiguous.

If dynamic endpoints need asynchronous observation, design that notification
with the endpoint API itself (conceptually an endpoint watch with
added/removed/changed events). A changed event can then tell the client to
query the endpoint's current properties without adding one event type for
every future property. The exact API and event names remain open.

This separation also leaves room for MIDI 2.0 discovery, where Endpoint and
Function Block properties can evolve independently of the legacy port/cluster
projection.

**Decision:** do not add `CWE_Changed` to the version 42 cluster watch for
endpoint state or metadata. Keep the existing cluster-watch ABI and semantics.

### 6.5 MIDI 2.0 extensibility

The initial dynamic-endpoint work does not require CAMD to carry native UMP,
but its terminology and object model should not assume that every future
endpoint is only a MIDI 1.0 port. MIDI 2.0 distinguishes a UMP Endpoint from
the Function Blocks and Groups it contains.

The first implementation may still project an endpoint into ordinary CAMD
clusters for legacy applications. The opaque/versioned model should leave room
for richer Endpoint, Function Block and Group metadata later, rather than
baking those concepts into `NPorts` or public 41.1 structures.

This follows the same broad compatibility pattern used by modern MIDI
subsystems: retain the traditional port-facing contract while adding richer
endpoint identity and topology beside it.

**Compatibility:** sections 6.1-6.5 are additive. `MidiDeviceData.NPorts`
keeps its existing meaning and no CAMD 41.1 public structure changes size or
layout. Existing drivers continue to expose fixed ports exactly as before.
Dynamically registered endpoints appear through compatible CAMD clusters to
legacy clients; richer identity, state and metadata require the versioned
extension. New library vectors are appended after the existing ABI and new
state remains private or opaque.

## Implementing the rest

The items below are in the order to do them. Each one is one commit in
`~/AROS` on top of `camd-robustness`, one patch in `patches/`, and new checks
in `MIDIHubCAMDCompat`; the suite must pass before and after, under QEMU. The
designs follow the original CAMD autodoc
([camd.doc](https://wiki.amigaos.net/amiga/autodocs/camd.doc.txt)) wherever
it defines the behaviour, and the contract above wherever it does not.

### Step 1: `MLINK_Comment` as the cluster comment (2.1)

- `SetMidiLinkAttrsA()` stores the caller's string in `ml_ClusterComment`
  (CAMD never copies or frees it, as with `MLINK_Name`).
- `MyMidiCluster` gets a private `char comment[35]`. A helper,
  `UpdateClusterComment(cluster)`, called with `CLSemaphore` held exclusively,
  copies at most 34 characters from the comment of the highest-priority
  link (nodes of type `NT_USER-MLTYPE_NTypes` are driver ports, skip them)
  that has one; on equal priority the receiver list is searched first. It
  runs when a link joins or leaves, when `MLINK_Priority` changes and when
  `MLINK_Comment` is set.
- `GetMidiLinkAttrsA(MLINK_Comment)` returns the cluster's comment for a
  link in a cluster, `ml_ClusterComment` otherwise, and counts the tag.
- `GetClusterAttrsA(MCLA_Comment)` returns the same comment (today it
  returns NULL); its check, `MCLA_Comment is NULL without a comment`, stays
  true for a cluster without one.
- Suite: the check `GetMidiLinkAttrsA does not count MLINK_Comment` becomes
  `MLINK_Comment returns the cluster comment`. Add checks for the priority
  rule, for falling back to the next link when the commenting one leaves,
  and for the 34-character limit.

### Step 2: SysEx size and the error filter (5.1)

- `MIDI_ErrFilter`: bits set are errors the node does not want. The default
  0 reports everything, as 41.1 does. Every place that sets `error` goes
  through one helper that drops the filtered bits.
- `PutSysEx()` knows the length (`GetSysXLen()`): skip a receiver whose
  `mi_SysExQueueSize` cannot hold it and flag it `CMEF_SysExTooBig`, as the
  autodoc says. On the byte-wise paths (`ParseMidi()`, drivers) the length
  is unknown; when the ring fills while the message started in an empty
  ring, the message cannot fit at all: flag `CMEF_SysExTooBig` instead of
  `CMEF_SysExFull`.
- `MCLA_SysExDropped` already counts SysEx dropped for a full buffer; count
  the oversized ones there too.
- Suite: an oversized SysEx to a node with a small buffer gives
  `CMEF_SysExTooBig` and no partial message, and the next message arrives;
  `MIDI_ErrFilter` with `CMEF_BufferFull` makes an overflow invisible to
  `GetMidiErr()` and `WaitMidi()`.

### Step 3: participant notification (3.1), done

- Maintain `mcl_Participants` (links in the cluster, driver ports
  excluded) and `mcl_PublicParticipants` (the same without `MLF_PrivateLink`
  links) in `AddClusterReceiver()`, `AddClusterSender()` and
  `UnlinkMidiLink()`.
- When they change, every other node with a link in the cluster is told:
  `Signal(mi_SigTask, 1 << mi_ParticipantSigBit)` if the bit is not -1, and
  `CallHookPkt(mi_ParticipantHook, node, cluster)` if there is a hook. The
  node whose link changed is not told. As `mi_ReceiveHook` already is, the
  hook is called in the changing task's context with `CLSemaphore` held,
  and must not add, remove or move links; say so in the autodoc.
- `MLF_PartChange` stays unused: its meaning is not documented anywhere.
- Suite: node A with `MIDI_PartSignal` is signalled when node B links to or
  leaves A's cluster, not when A changes its own links, and not for a cluster
  A is not in; the participant counts follow; a hook receives the cluster.

### Step 4: version 42, done

New functions need a new version, so programs can ask for them with
`OpenLibrary("camd.library", 42)`; a 41.x revision cannot be asked for.
Append after `Midi2Driver()`, in one commit with the version bump:

- `ULONG GetClusterAttrsA(struct MidiCluster *, struct TagItem *)`, read
  under `LockCAMD()`, with new `MCLA_` tags: `MCLA_Comment` (step 1),
  `MCLA_Participants`, `MCLA_PublicParticipants`, the error counters of 5.2
  (`MCLA_Overflows`, `MCLA_SysExDropped`, `MCLA_ParseErrors`, kept in
  `MyMidiCluster`), and `MCLA_Connected`: for a driver port, whether a device
  is behind it. Drivers cannot say that today; give `MidiDeviceData` users a
  way only through a new optional driver call, never by growing the
  structure, or leave `MCLA_Connected` to virtual ports (step 6).
- `ULONG CamdTime(void)`: milliseconds since CAMD started, from
  `timer.device`, monotonic, wrapping at 2^32. With the new node tag
  `MIDI_SystemClock` (appended to the `MIDI_` tags), CAMD stamps messages
  for that node with `CamdTime()` instead of reading `mi_TimeStamp`. Note
  that CAMD stamps a message with the receiving node's clock, not the
  sender's (the suite checks this).
- Suite: a second binary, or a section that runs only on 42, checks each
  new call; the 41.1 checks must still pass on 42.

As built: `MCLA_Connected` was left out. A driver cannot say whether a
device is behind its port, and the structure it shares with CAMD
(`MidiDeviceData`) must not grow; `MCLA_HasDriver` says whether a driver port
is in the cluster. Whether a port has a device can come with virtual ports
(step 6), or from a new optional driver call.

### Step 5: cluster watches, done

For programs that follow every cluster without linking to it, such as
MIDIHub.prefs: `StartClusterWatchA()`, `GetClusterWatchEvent()` and
`EndClusterWatch()` in 42. Each handle queues up to 32 events naming the
cluster (`CWE_Added`, `CWE_Removed`, `CWE_Participants`) and reports
`CWE_Lost` when it overflowed. They are not called `StartClusterNotifyA()`:
genmodule would derive a `StartClusterNotify()` varargs macro from that name,
clashing with the 41.1 call, which keeps signalling only clusters coming and
going.

### Step 6: dynamic endpoint design (6.1-6.5)

Use AppleMIDI/Network MIDI, BLE, USB hot-plug and the software synth as
concrete lifecycle cases. Specify identity, connected/disconnected state,
legacy cluster projection, ownership, locking and safe unregister semantics
before naming the public API. Keep `NPorts` untouched. A first implementation
can then cover virtual endpoints and dynamic driver endpoints through the same
underlying model where practical.

Native UMP is not a prerequisite for this step, but the opaque model must not
prevent later Endpoint, Function Block and Group metadata.

## Order and why

1. Done: section 1, the USB lifecycle, steps 3, 4 and 5.
2. Next: steps 1 and 2. They make documented 41.1 behaviour real; no new
   call.
3. Then MIDIHub uses version 42: MIDIHub.prefs follows clusters with a
   cluster watch and shows the counters, routes use `MIDI_PartSignal` to
   notice a device coming back, BLE MIDI stamps with `CamdTime()`.
4. Then design step 6 from the now-concrete dynamic endpoint requirements.
   AppleMIDI is the primary case; fixed `NPorts` pools are only a fallback.
5. Treat persistent routing and native MIDI 2.0/UMP as subsequent CAMD design
   questions rather than requirements for the first dynamic-endpoint API.


## 7. Native MIDI 2.0 / UMP architecture (future design contract)

**Status: design proposal, not implemented.** This section completes the intended
architecture for a future native MIDI 2.0 CAMD without changing the implementation
order above. Section 6.5 remains the prerequisite: dynamic endpoint handles must
be capable of representing UMP topology, but native UMP need not ship with the
first dynamic-endpoint implementation.

### 7.1 Scope and invariants

- Make UMP the lossless, native message representation for new MIDI 2.0-aware
  CAMD clients. UMP is a packet container, not a synonym for MIDI 2.0 Channel
  Voice: it also transports MIDI 1.0 messages.
- Preserve every existing CAMD 41.1/42 library vector, public structure,
  tag value, cluster behavior and driver ABI. Add versioned entry points and
  opaque handles; never reinterpret `MidiMsg` as a UMP packet.
- Legacy MIDI 1.0 applications remain functional without recompilation.
  Conversion is explicit in policy and observable when information is lost.
- Keep transport (USB, BLE, AppleMIDI, network), CAMD endpoint topology,
  MIDI protocol, synthesis and routing as separate concerns.
- Preserve unknown but structurally valid future UMP message types as opaque
  packets when possible; reject malformed or unsupported packets with
  diagnostics rather than silently corrupting them.

### 7.2 Native UMP event contract

Introduce a versioned UMP event API (names TBD) with:
- 1–4 32-bit words per complete UMP message, determined by its Message Type;
  exact original words, group and message ordering are retained.
- A separate monotonic timestamp and clock-domain identifier, plus flags for
  timestamp validity and scheduling policy. Do not encode internal time solely
  as JR Timestamp UMP utility messages.
- Batch send/receive, bounded queues, backpressure/error reporting, atomic
  delivery of complete messages and explicit ordering guarantees per sender.
- Defined handling for 32/64/96/128-bit messages, SysEx7, SysEx8, Mixed
  Data Set, Flex Data, Stream and Utility messages. Do not assume all messages
  are channel-addressed or group-addressed.
- Explicit endianness conversion at transport boundaries; native API word
  representation is host-endian and transport serialization follows the
  relevant transport specification.
- Stable ownership/lifetime of buffers, callbacks and endpoint references;
  safe teardown while messages are queued.

### 7.3 Endpoint topology, discovery and protocol selection

A CAMD UMP Endpoint is a persistent, opaque identity distinct from transport
connection, legacy cluster, Function Block and Group. Model:
- UMP version, manufacturer/model/identity where available, product instance
  identifier, supported/current MIDI protocols, RX/TX JR capabilities,
  endpoint direction and connection state;
- Function Block identifier, name, active state, direction, first Group,
  Group span, protocol/MIDI-CI capability and static/dynamic topology;
- Group validity and direction, with explicit mapping from each exposed
  legacy CAMD cluster to an endpoint/group/function as appropriate.
- UMP Endpoint Discovery and Stream Configuration / Protocol Request and
  Notification, including re-discovery after reconnect and capability changes.
  Do not implement deprecated MIDI-CI Protocol Negotiation as the primary
  mechanism.
- MIDI-CI discovery, Profiles, Property Exchange and Process Inquiry are
  distinct higher-layer capabilities, not synonyms for UMP protocol selection.
  CAMD transports MIDI-CI messages and provides a capability hook; policy and
  profile/property clients may live outside the CAMD core.
- Define timeout, retry, fallback, cache invalidation and conflicting
  capability reports. Never claim MIDI 2.0 Channel Voice support solely
  because the transport carries UMP.
- Retain section 6.4's separate endpoint watch; report topology, active
  protocol and state changes without altering legacy ClusterWatch semantics.

### 7.4 Legacy projection and translation

- Expose compatible MIDI 1.0 cluster views of native endpoints for existing
  applications, with stable names and configurable Group mapping.
- Specify direction-specific MIDI 1.0 byte/message ↔ UMP MIDI 1.0 conversion
  and MIDI 1.0 ↔ MIDI 2.0 Channel Voice translation where supported.
- Follow normative translation/bit-scaling rules, including CC, RPN/NRPN,
  Program Change, bank selection, pitch bend, note-on velocity zero, and
  per-note controls. Not every MIDI 2.0 message has a faithful MIDI 1.0
  equivalent: choose documented drop, approximation or explicit failure
  policies and expose counters.
- Define SysEx7 reassembly/fragmentation and SysEx8 incompatibility policy;
  maintain boundaries, ordering and maximum sizes. Do not truncate SysEx8
  silently into legacy SysEx.
- Keep a legacy sender's stream state distinct when translation needs
  context. Do not infer producer identity from a legacy `MidiMsg` received
  on a shared cluster.
- Prevent routing loops and duplicate deliveries between native UMP and
  legacy projections of the same endpoint.

### 7.5 Routing, multi-producer semantics and timing

- Route native UMP by endpoint, Function Block and Group where meaningful;
  preserve group-less endpoint/utility/stream messages and their scope.
- Existing CAMD merging remains supported, but merging does **not** provide
  per-application MIDI channel state isolation. Document concurrent producer
  ordering, contention and controller/note-state interactions.
- Optional isolated sessions require distinct source identity or explicit
  session endpoints; do not promise automatic isolation of old applications.
- Define per-source ordering, interleaving of multi-packet data streams,
  queue fairness, priority and overflow behavior; no partial UMP messages.
- Relate CAMD monotonic time (`CamdTime()` and future clock extensions),
  transport timebases and optional JR Clock/JR Timestamp without conflating
  JR timestamps with a universal scheduling clock. Specify late-event policy,
  clock discontinuity, reconnect and wraparound behavior.
- Keep System MIDI Out, route filters and software synthesis as consumers of
  this API, not hard-coded special cases inside CAMD.

### 7.6 Driver interface and portability

- Add versioned UMP-capable driver registration and callbacks beside legacy
  fixed-`NPorts` drivers; both may coexist.
- Transport drivers advertise actual capabilities, normalize complete UMP
  messages and serialize for their physical/network transport. A MIDI 1.0-only
  driver remains usable via an adapter, with documented conversion policy.
- Define locking and memory constraints for interrupt/task contexts, queue
  ownership, disconnect while I/O is pending, hot-plug and restart.
- Do not assume USB MIDI 2.0, BLE MIDI, AppleMIDI and network UMP have
  identical framing, timing or negotiation. Test each transport separately.

### 7.7 Errors, security and resource limits

- Add inspectable per-endpoint and per-route counters for malformed UMP,
  unsupported message/protocol, conversion loss, queue overflow, timeout,
  negotiation failure and transport disconnect.
- Bound SysEx7/SysEx8/Mixed Data Set reassembly, property payloads, pending
  transactions and endpoint metadata; enforce validation before allocation.
- Treat externally supplied discovery strings, MIDI-CI/Property Exchange and
  network messages as untrusted input. Avoid blocking the real-time MIDI
  delivery path on discovery, property queries or callbacks.
- Specify permissions/trust policy for remote peers and property writes in
  the relevant transport/service layer, not as an implicit CAMD side effect.

### 7.8 Implementation sequence (future, after current CAMD roadmap)

1. Freeze a versioned UMP API and opaque topology model with ABI review.
2. Implement parser/serializer, validation, event queues and unit tests.
3. Add a software loopback UMP endpoint and end-to-end native clients.
4. Add MIDI 1.0 projections and conversion with measurable loss reporting.
5. Add discovery, Function Block metadata, protocol selection and watches.
6. Integrate a real UMP-capable transport; validate hot-plug and fallback.
7. Validate MIDI-CI interoperability, performance and long-running stress.
8. Enable System MIDI Out and optional router/filter clients to use native
   UMP without changing the underlying CAMD contract.

### 7.9 Acceptance criteria

- Unmodified legacy CAMD compatibility suite still passes, including 41.1
  behavior and version 42 additions.
- Round-trip every defined UMP packet length and test unknown/invalid types;
  preserve payloads and group-less addressing where applicable.
- Exercise all 16 Groups and 16 channels per Group, multi-producer ordering,
  complete-message queue overflow and concurrent endpoint removal.
- Verify MIDI 1.0 conversion and explicitly test nonrepresentable MIDI 2.0
  events, high-resolution data, SysEx7/SysEx8 and conversion diagnostics.
- Verify UMP endpoint/function-block discovery, dynamic state changes,
  protocol selection, MIDI-CI separation and reconnect.
- Test timing (JR and non-JR), wraparound, late messages, and at least one
  native UMP transport alongside a legacy-only transport.
- Run under QEMU and supported AROS targets; document test gaps for hardware
  not yet available. No claim of MIDI 2.0 conformance without validating the
  applicable published MIDI Association specifications.

### 7.10 Normative references and implementation precedents

- MIDI Association M2-100-U (Overview), M2-101-UM (MIDI-CI),
  M2-102-U (Profiles), M2-103-UM (Property Exchange),
  M2-104-UM (UMP and MIDI 2.0 Protocol), and applicable updates:
  https://midi.org/midi-2-0
  https://midi.org/details-about-midi-2-0-midi-ci-profiles-and-property-exchange-updated-june-2023
- Linux ALSA MIDI 2.0 design (UMP endpoints, Function Blocks and legacy
  projections): https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html
- Windows MIDI Services implementation decisions:
  https://microsoft.github.io/MIDI/kb/midi2-implementation-details/
- Apple CoreMIDI MIDI 2.0 integration:
  https://developer.apple.com/documentation/coremidi/incorporating-midi-2-into-your-apps

**Scope boundary:** This section completes the *future architecture proposal*,
not an implementation or certification claim. Exact C symbols, structure
layouts, negotiation state machines and protocol conformance vectors are to be
frozen against the applicable specification revisions when implementation
begins.


### 7.11 Pre-implementation research audit (2026-10-08)

This section records the remaining **implementation blockers and normative
verification gates** so that an agent does not declare CAMD MIDI 2.0 complete
after merely adding UMP send/receive. The architectural target in 7.1–7.10
remains future work. **No native UMP support is claimed to be implemented.**

**Normative baseline:** The MIDI Association's MIDI 2.0 Core Specification
Collection updated 2025-12-18 includes UMP and MIDI 2.0 Protocol v1.1.2,
MIDI-CI v1.2.1, Profiles v1.1, Property Exchange v1.2, Overview v1.1 and
MIDI Clip File v1.0. Download and retain revision identifiers and any
applicable errata before freezing bit fields or translation algorithms.
The full normative PDFs may require MIDI Association login; public summaries
and Linux source are **not substitutes** for the normative texts.

#### Critical architectural decisions to freeze before coding

| ID | Design gate | Required decision / artifact |
|---|---|---|
| U01 | ABI/API | Exact new vectors, version gates, opaque handle ownership, C header and no changed 41.1/42 layouts |
| U02 | UMP parsing | Normative Message Type-to-length table, reserved types, validity rules, 32/64/96/128-bit round trips, stream parser boundaries |
| U03 | Message scope | Distinguish group-addressed Channel Voice and System messages from group-less Stream messages; document Utility/JR and Flex Data handling according to specification |
| U04 | Discovery state machine | Endpoint discovery, Function Block discovery, Stream Configuration Request/Notification, capability cache, timeouts, reconnect and fallback |
| U05 | Function Block topology | Direction, active state, Group spans, overlapping/invalid ranges, dynamic changes, USB Group Terminal Block fallback |
| U06 | Legacy bridge | Group-to-cluster projection and mapping, MIDI 1.0 byte stream / UMP MIDI 1.0 / MIDI 2.0 translation, loss reporting, reverse-direction behavior |
| U07 | Stateful translation | RPN/NRPN, bank/program, per-note controls, note-on velocity zero, CC resolution, running status, SysEx segmentation, reset and disconnection behavior |
| U08 | Data message support | SysEx7, SysEx8, Mixed Data Set, Flex Data, Stream and MIDI-CI boundaries; document transparent pass-through versus interpreted support |
| U09 | Scheduling | Explicit monotonic clock, timestamp validity, JR Clock/JR Timestamp handling, cross-transport mapping, late events, overflow and reconnect |
| U10 | Multi-producer | Ordering and fairness, source identity availability, atomic message delivery, no implicit per-application isolation |
| U11 | Driver contract | UMP-aware callbacks, legacy adapters, host/transport byte order, hotplug teardown, interrupt-context rules and queue bounds |
| U12 | Security/diagnostics | Untrusted discovery/property payloads, bounded allocations, per-endpoint counters, protocol mismatch and conversion-loss observability |
| U13 | Conformance | Normative test vectors, interoperability with another UMP stack, QEMU + hardware matrix, regression of existing CAMD ABI |
| U14 | Lifecycle | Startup/shutdown, disconnected but registered endpoint, unregister with outstanding references, notification delivery and watch overflow |

**Important correction/precision:** Protocol selection in the UMP v1.1
architecture is associated with Endpoint/Stream Configuration messages.
Do not implement deprecated MIDI-CI Protocol Negotiation as the default
path. Before writing packet constants, verify the exact message class and
status codes against M2-104-UM v1.1.2; public explanatory material sometimes
uses different shorthand for protocol request/notification.

**Implementation-level gaps that cannot be closed by prose alone:**
- Define concrete C prototypes, struct packing/alignment on 32-bit and
  64-bit AROS targets, function vector ordering, version discovery and
  safe handle release. Test binary compatibility with unmodified callers.
- Define message routing for non-channel and group-less UMP, avoiding
  duplication between endpoint-wide and per-Group subscriptions.
- Specify complete normative translation tables and lossy cases, including
  RPN/NRPN state machines and SysEx8-to-legacy behavior. Do not invent
  scaling formulas without normative validation.
- Define separate support levels: (a) raw UMP pass-through, (b) native
  MIDI 2.0 Channel Voice semantics, (c) UMP Endpoint discovery, (d) MIDI-CI
  Profiles/Property Exchange interoperability. Passing (a) is **not**
  sufficient to claim (b)–(d).
- Distinguish core transport support from transport-specific conformance.
  USB MIDI 2.0 and Network UMP require separate validation; AppleMIDI and
  BLE MIDI 1.0 do not become MIDI 2.0 merely because CAMD is UMP-capable.

#### Recommended milestone / definition-of-done matrix

- **M0: ABI freeze** — approved header/API sketch, compatibility test and
  UMP object ownership rules; no native implementation claim.
- **M1: UMP transport core** — parser/serializer and native loopback with
  all lengths, ordering, validation, error counters and queue teardown tests.
- **M2: Native graph** — Endpoint/Function Block/Group metadata, watches,
  discovery state machine, reconnect and no duplicate group-less messages.
- **M3: Legacy interop** — bidirectional translation and loss accounting,
  all 16 Groups, legacy cluster projection and original 41.1/42 suite.
- **M4: Hardware/transport** — one real native UMP transport, one MIDI 1.0
  fallback transport, hotplug, stress and timing tests.
- **M5: Feature complete** — documented capability matrix, MIDI-CI policy
  and interoperable tests of supported Profiles/Property Exchange features
  (or explicitly documented out-of-scope higher-layer services), with no
  remaining unclassified failing cases.

A release may claim **native UMP transport** after M1/M2, but must not claim
**complete MIDI 2.0 CAMD support** until the defined MIDI 2.0 protocol,
topology, interoperability and compatibility acceptance gates pass.
The user-facing System MIDI Out and Filters proposals remain independent.

#### Sources checked for this audit

- MIDI Association, MIDI 2.0 Core Specification Collection (2025-12-18):
  https://midi.org/midi-2-0-core-specification-collection
- MIDI Association, UMP and MIDI 2.0 Protocol v1.1.2:
  https://midi.org/universal-midi-packet-ump-and-midi-2-0-protocol-specification
- MIDI Association, 2023 UMP revision summary and protocol-negotiation change:
  https://midi.org/details-about-midi-2-0-midi-ci-profiles-and-property-exchange-updated-june-2023
- Linux ALSA MIDI 2.0 design, UMP endpoint and legacy projections:
  https://www.kernel.org/doc/html/latest/sound/designs/midi-2.0.html
- Linux USB MIDI 2.0 implementation:
  https://github.com/torvalds/linux/blob/master/sound/usb/midi2.c

**Research limitation:** The complete normative PDFs and associated test
vectors were not independently audited line by line in this pass. This is
an implementation-readiness checklist, **not** a certificate of normative
completeness. Obtain and review those texts before declaring U01–U13 closed.


### 7.12 Mandatory engineering instructions for the implementation agent

**Normative engineering constraints — not optional shortcuts.**

1. Preserve the CAMD 41.1 ABI and behavior and the documented version 42 extensions: existing library vectors, register assignments, public structure sizes/layouts, tags, and legacy driver interfaces must remain compatible. Run `MIDIHubCAMDCompat` before and after each change.
2. Implement **native, full-fidelity UMP** through additive versioned APIs and opaque endpoint objects. MIDI 1.0 cluster projections are compatibility adapters, **not** the internal MIDI 2.0 transport. Do not emulate MIDI 2.0 using a 16-channel MIDI 1.0 tunnel, fixed artificial port pools, SysEx wrappers or hard-coded devices.
3. Freeze the relevant MIDI Association specifications and errata before writing constants or state machines. Verify packet layouts, discovery, protocol configuration, translation, timing and capability reporting against the normative specifications, not just another operating system's implementation.
4. No hidden degradation: do not silently drop or truncate SysEx8, discard unsupported messages without diagnostics, fabricate negotiated capabilities, lose high-resolution information without a documented conversion policy, or return success for incomplete operations.
5. Define correct concurrency and lifecycle semantics: bounded queues, ownership, atomic complete UMP messages, multi-producer ordering, disconnect/reconnect, reference lifetime, hot-plug, safe unregister and task/interrupt context rules. Test races and failure paths.
6. Maintain architectural boundaries: CAMD owns protocol transport, endpoint graph, discovery/interoperability primitives; Router owns default-destination policy, routes and filters; Synth owns synthesis. System MIDI Out and MIDI Filters must not be hard-coded into CAMD.
7. Distinguish raw UMP transport, MIDI 2.0 Channel Voice, Endpoint/Function Block discovery and MIDI-CI capabilities. Advertise only supported features and preserve MIDI 1.0-only devices through explicit adapters.
8. Keep implementation portable across AROS targets, accounting for 32/64-bit ABI, alignment, endianness and timing. Platform-specific workarounds cannot become the general CAMD API.
9. Follow milestones M0–M5 and audit gates U01–U14; each change includes tests and documentation. If normative texts, hardware or interoperability fixtures are unavailable, mark the gate **blocked**, not passed.
10. Escalate irreversible public API or Group/Function Block mapping decisions for review. Prefer a documented incomplete capability over an expedient implementation that needs architectural replacement.

**Definition of done:** Full native MIDI 2.0 CAMD requires full-fidelity UMP, native endpoint topology and protocol handling, explicit legacy interoperability, robust lifecycle and timing, observable errors, and applicable normative/interoperability tests. A compiling library, loopback test or successful Note On is not completion.


### 6.6 Architecture review gate: endpoints are not dynamic USB ports

**Mandatory before implementing section 6 or freezing M0.** Read
[CAMD endpoint architecture review](camd-endpoint-architecture-review.md).
The legacy USB implementation motivated this proposal but does **not**
constrain the replacement architecture. Preserve the CAMD legacy
**application ABI**, not the old USB driver's fixed-port implementation.
Compare port resizing, central registry/providers and distributed graphs
before choosing a public API. Prefer logical endpoint identity, asynchronous
discovery, native UMP topology and compatible legacy projections; do not
build new APIs around `MidiDeviceData.NPorts`. Existing USB MIDI
functionality and legacy application behavior must still pass regression
tests after migration. Review the topology/identity/lifecycle contract
before agent-driven implementation.


### 6.7 No throwaway transition architecture — implement the final model directly

**Agent instruction:** Section 6.1–6.5 is historical design input, **not authorization** to implement an interim dynamic-port mechanism. Complete the architecture comparison and approve the target endpoint registry/provider, native UMP topology, lifecycle and legacy projection contracts **before writing a new public API**. Then implement that final architecture directly in small, tested increments. Do **not** build a fixed `NPorts` pool, a USB-only dynamic-port API, or another temporary solution intended to be replaced later. The existing USB implementation is replaceable; legacy CAMD application compatibility and functional USB MIDI support are not. Compatibility adapters are acceptable only as parts of the approved final architecture. See [endpoint architecture review](camd-endpoint-architecture-review.md).
