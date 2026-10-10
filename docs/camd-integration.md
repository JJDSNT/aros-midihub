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

During incubation, MIDIHub uses CAMD client nodes to create virtual ports for
network, BLE, and the SoundFont synthesizer. These are standard CAMD clusters:
applications can send to and receive from them without a `DEVS:Midi` driver.
As implementations mature, transport-specific components should move to their
natural AROS homes; MIDIHub should consume the resulting CAMD-visible endpoints
rather than retain duplicate transport implementations.

## Target architecture

| Endpoint | Discovery or configuration | CAMD-facing component | Transport or output |
| --- | --- | --- | --- |
| USB MIDI | Poseidon class binding | Existing `DEVS:Midi` driver | Poseidon USB class |
| RTP-MIDI / AppleMIDI | MIDI Preferences, mDNS, session negotiation | `applemidi.device` | `bsdsocket.library` |
| Network MIDI 2.0 / UMP | MIDI Preferences, mDNS, session negotiation | `networkmidi2.device` | `bsdsocket.library` |
| BLE MIDI | Bluetooth service discovery and binding | `btmidi.class` CAMD clients | `bluetooth.library` GATT |
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
tasks remain behind their respective device boundaries.

Current CAMD drivers have a fixed `NPorts` after initialization. The preferred
long-term direction is an additive CAMD dynamic-endpoint extension that leaves
legacy `NPorts` unchanged while allowing a loaded device to register endpoint
identity, metadata and connected/disconnected lifecycle at run time. A bounded
pool of fixed ports with internal peer-to-port mapping is a fallback for CAMD
versions without that extension, not the target architecture. The portable
codecs remain independent of either native registration mechanism.

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
3. Connect the fixed-port adapter to the implemented `DriverData` shared-open
   helpers and bounded-work AROS executor: attach/detach each session worker
   to the `DriverData` fan-out and wake it after producer enqueue. Transmitter
   capacity already reaches the fan-out through the stable per-port receiver
   task. The unchanged cluster path already participates in the same
   first-open/last-close state. Add timestamp eligibility and reverse cluster
   projection after this path is operational.
   The host model already proves endpoint/session ownership, asynchronous
   receive teardown and separate native MIDI 1.0/UMP dispatch; do not require
   MIDI 1.0-to-MIDI 1.0 traffic to convert through UMP.
4. Close CAMD gate U01 using evidence from that private implementation:
   appended client-vector order, record/version policy, 32/64-bit builds,
   ownership tests and upstream review. Keep provider registration private.
5. Add native UMP transport/topology according
   to the M0-M3 gates in [CAMD improvements](camd-improvements.md).
6. Keep endpoint names and directions clear to CAMD clients across network,
   BLE, USB, serial, and synth.
7. Validate the `MIDIHub Synth` receive port with a native CAMD sender and
   live AHI playback.
8. Validate USB and BLE with physical devices, including binding, message
   transfer, SysEx, disconnection, and reconnection.
9. Use endpoint enumeration/state in the MIDIHub overview; retain cluster
   enumeration for CAMD 41.
10. Keep basic persistent source-to-destination routes and reconnection on the
   stable endpoint identity where available.
11. Add Profiles after route persistence and the current Network MIDI/Synth
   settings have stable storage contracts.
12. Extend CAMD driver removal only where hardware drivers require it.
