# CAMD integration design

## Reference contract

CAMD uses a shared endpoint control plane with path-selective payload handling.
Existing MIDI 1.0 routes remain native; UMP-capable clients and transports use
complete UMP events; conversion is inserted only at an incompatible boundary.
See the
[coexistence research](midi-protocol-coexistence-research.md) and the amended
[endpoint architecture decision](camd-endpoint-architecture-decision.md).

The AROS Poseidon `camdusbmidi.class` provides the integration model for
MIDIHub's network, BLE, serial, virtual, and software synthesizer endpoints:
ordinary MIDI applications see CAMD ports and exchange MIDI with them. The
protocol implementation stays behind the CAMD boundary. CAMD, networking,
Bluetooth, and audio are common AROS APIs; the portable codecs and synth
must not depend on a particular CPU or hardware controller.

The USB implementation has two pieces. Poseidon's class recognizes a device,
manages its binding, and writes a relocatable CAMD driver to `DEVS:Midi`.
CAMD `LoadSeg`s that driver, checks its `MidiDeviceData`, and creates
`<driver>.in.<port>` and `<driver>.out.<port>` clusters. Its driver callbacks
open and close a port and exchange MIDI bytes with the class. USB hardware
access stays inside Poseidon. The source is in AROS
`rom/usb/classes/camdmidi/` and `workbench/libs/camd/`.

**A transport is a CAMD device, and a CAMD device is a `DEVS:Midi` driver.**
This was settled by the upstream review of `btmidi.class` on 2026-10-11
(AROS PR #1507): the class had made a MidiNode with two clusters, as a
client does, and was sent back to be a `struct MidiDeviceData` driver like
the USB one. The objections apply to any transport: a client's clusters are
invisible to whatever enumerates MIDI by device, exist only while the
client's task does, add a queue in each direction, and repeat framing that
CAMD's parser already does for a driver. `btmidi.class` now follows the USB
model, and every later transport starts from it:

- a small relocatable driver in `DEVS:Midi`, embedded in and written by the
  component that owns the transport, whose `Init()` opens that component;
- a fixed number of ports, with the component mapping its peers or devices
  to them;
- bytes to CAMD's receive function, and CAMD's transmit function drained on
  `ActivateXmit`;
- no `camd.library` opened from the transport.

CAMD client nodes are for programs: the SoundFont synthesizer, MIDIHub's
router, tools. MIDIHub's own network and BLE code still uses them from the
incubation period; that code is to be replaced by the native components,
not carried upstream as it is.

## Target architecture

| Endpoint | Discovery or configuration | CAMD-facing component | Transport or output |
| --- | --- | --- | --- |
| USB MIDI | Poseidon class binding | Existing `DEVS:Midi` driver | Poseidon USB class |
| RTP-MIDI / AppleMIDI | MIDI Preferences, mDNS, session negotiation | a `DEVS:Midi` driver in front of `applemidi.device` | `bsdsocket.library` |
| Network MIDI 2.0 / UMP | MIDI Preferences, mDNS, session negotiation | a `DEVS:Midi` driver in front of `networkmidi2.device`; MIDI 1.0 until a driver can carry UMP | `bsdsocket.library` |
| BLE MIDI | Bluetooth service discovery and binding | `DEVS:Midi/btmidi`, written by `btmidi.class` | `bluetooth.library` GATT |
| Serial / DIN | Configured AROS serial device and port | Native serial MIDI component | AROS serial API |
| Software synthesizer | SoundFont and AHI preferences | Native synth service CAMD client | TinySoundFont or FluidSynth to AHI |

For each endpoint, the CAMD-facing component should own a stable port name,
handle MIDI bytes and SysEx in the directions it supports, and release resources
when the transport disappears. MIDIHub can then provide a unified overview and
basic persistent routes between those endpoints without requiring applications
or users to understand AppleMIDI, GATT, USB binding, or other transport details.

During incubation, CAMD client nodes remain useful for Network MIDI testing.
The native targets, however, are `applemidi.device` and
`networkmidi2.device`: sockets, discovery, peer/session state and any internal
tasks remain behind their respective device boundaries, and each reaches
CAMD through a driver as above.

## What the driver model cannot say

Current CAMD drivers have a fixed `NPorts` after initialization and exchange
a MIDI 1.0 byte stream. A bounded pool of fixed ports with internal
peer-to-port mapping is therefore where every transport starts; `btmidi`
does it with 16 ports. Doing so shows four things a driver has no way to
tell CAMD:

1. **Identity.** A port is known by its driver's file name and its number.
   A BLE device has an address and a USB device a serial number, and neither
   reaches CAMD. A device that takes another port is another endpoint.
2. **Presence.** All 16 ports of `btmidi` look alike, with or without a
   device. The only sign is `OpenPort()` failing.
3. **A name** to show the user for a port.
4. **UMP, and the time a message arrived.** The receive and transmit
   functions carry single bytes.

The direction is one additive extension **on the driver side**: an optional
block a driver may provide beside `MidiDeviceData`, through which it gives
CAMD a port's identity evidence, name and presence, and later a UMP path.
It leaves `MidiDeviceData`, `NPorts`, the library's vectors and the
`<driver>.in.<n>`/`<driver>.out.<n>` clusters as they are; a driver without
the block is treated exactly as today, and an application written for
CAMD 41 sees no difference. Inside CAMD the block feeds the endpoint
registry, which already publishes each loaded driver's ports
(`aros-camd-driver-endpoints.patch`) with file name plus port number as
path-bound identity.

This replaces the earlier idea of transports registering endpoints through
a provider interface of their own. The extension is a prerequisite for
stable routes to BLE and network peers and for native Network MIDI 2.0, not
a later refinement. It was put to the upstream maintainer as a question in
PR #1507; a concrete sketch goes into a separate issue if the answer is
yes, before any code. The portable codecs remain independent of it.

## CAMD lifecycle

CAMD scans `DEVS:Midi` when it starts. With
[the rescan patch](../patches/aros-camd-rescan.patch) `RethinkCAMD()` loads
drivers added later, and a new driver's ports join clusters of the same name
that clients made while waiting for it. `camdusbmidi.class` calls it after
writing a device's driver. Drivers are never unloaded: a USB device's ports
stay, and carry MIDI again when it is plugged back in
([the lifecycle patch](../patches/aros-usb-midi-lifecycle.patch)). See
[CAMD improvements](camd-improvements.md) for the details and what remains.

The Network MIDI devices must preserve endpoint identity while peers appear,
disappear and reconnect. With a future CAMD dynamic-endpoint extension, a
known endpoint may remain registered but disconnected/inactive while its peer
is away, then return to connected state without changing identity. On legacy
CAMD, the device may emulate that stability with a bounded fixed-port pool.
Dynamic network identity remains a device-lifecycle concern rather than a
reason to expose the transport as a standalone service.

## MIDIHub routing relationship

CAMD remains responsible for the MIDI graph. MIDIHub does not introduce a
parallel routing API or an advanced MIDI-processing layer.

The resident MIDIHub service may own the CAMD nodes/links needed to preserve
basic user routes while `MIDIHub.prefs` is closed. Its routing responsibility
is intentionally limited to source-to-destination connections, persistence,
waiting for unavailable endpoints, and optional automatic reconnection.

Advanced transformation such as transpose, velocity mapping, keyboard splits,
channel remapping, scripting, or processing graphs is outside this design and
belongs in specialized CAMD applications.

On CAMD 42, `StartClusterWatchA()` reports cluster additions, removals and
participant changes. The router also requests `MIDI_PartSignal`, so a saved
route can remain in a waiting state while an endpoint is absent and become
active immediately when its provider rejoins the existing cluster. CAMD 41
uses `StartClusterNotify()` plus a bounded polling fallback.

See [midihub-architecture.md](midihub-architecture.md) for runtime ownership,
[preferences.md](preferences.md) for the user-facing model, and
[camd-improvements.md](camd-improvements.md) for changes proposed to CAMD
itself.

## Implementation sequence

1. Approve the private invariants for the accepted
   [central registry/provider architecture](camd-endpoint-architecture-decision.md):
   sized pointer-free records, identity ownership, lifecycle, topology and
   lock order. This does not freeze public symbols.
2. Implement the private registry, generation snapshots and watches as the
   shared control plane, without changing CAMD 41/42 structures.
3. Use the compiled private AROS identity store to instantiate the fixed-port
   output adapter from loaded drivers, after native DOS/filesystem recovery
   fault injection. The host fault model already exercises the same compiled
   state machine. The store uses `uuid.library` and recoverable checked IFF
   main/`.new`/`.bak` snapshot; initialization failure leaves legacy CAMD
   operational. The
   versioned legacy evidence key is the
   case-folded `DEVS:Midi` module leaf plus port index; it is explicitly
   path-bound and excludes direction. Its compiled AROS backend
   already owns each session's bounded queue/pump/worker, calls the `DriverData`
   shared-open helpers, performs transactional attach/detach against the port
   fan-out and submits native MIDI 1.0 directly to the legacy transmitter.
   Transmitter capacity reaches the fan-out through the stable per-port
   receiver task. The unchanged cluster path already participates in the same
   first-open/last-close state. Add timestamp eligibility and reverse cluster
   projection after this path is operational.
   The host model already proves endpoint/session ownership, asynchronous
   receive teardown and separate native MIDI 1.0/UMP dispatch; do not require
   MIDI 1.0-to-MIDI 1.0 traffic to convert through UMP.
4. Close CAMD gate U01 using evidence from that private implementation:
   appended client-vector order, record/version policy, 32/64-bit builds,
   ownership tests and upstream review. Keep provider registration private.
   Bring the upstream discussion forward: the `btmidi.class` review showed
   that conformity with the existing device model is looked at first, so
   the ABI sketch should be discussed before more endpoint code is written,
   and sent in small pieces (fixes and the USB lifecycle, then version 42,
   then endpoints).
5. Sketch and agree the driver-side extension described under "What the
   driver model cannot say"; give `btmidi` and the USB driver a port's
   identity, name and presence through it.
6. Add native UMP transport/topology according
   to the M0-M3 gates in [CAMD improvements](camd-improvements.md), through
   the same extension.
7. Keep endpoint names and directions clear to CAMD clients across network,
   BLE, USB, serial, and synth.
8. Validate the `MIDIHub Synth` receive port with a native CAMD sender and
   live AHI playback.
9. Validate USB and BLE with physical devices, including binding, message
   transfer, SysEx, disconnection, and reconnection.
9. Use endpoint enumeration/state in the MIDIHub overview; retain cluster
   enumeration for CAMD 41.
10. Keep basic persistent source-to-destination routes and reconnection on the
   stable endpoint identity where available.
11. Add Profiles after route persistence and the current Network MIDI/Synth
   settings have stable storage contracts.
12. Extend CAMD driver removal only where hardware drivers require it.
