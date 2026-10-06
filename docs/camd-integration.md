# CAMD integration design

## Reference contract

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
| RTP-MIDI / AppleMIDI | MIDI Preferences, mDNS, session negotiation | Native Network MIDI CAMD device | `bsdsocket.library` |
| BLE MIDI | Bluetooth service discovery and binding | `btmidi.class` CAMD clients | `bluetooth.library` GATT |
| Serial / DIN | Configured AROS serial device and port | Native serial MIDI component | AROS serial API |
| Software synthesizer | SoundFont and AHI preferences | Native synth service CAMD client | TinySoundFont or FluidSynth to AHI |

For each endpoint, the CAMD-facing component should own a stable port name,
handle MIDI bytes and SysEx in the directions it supports, and release resources
when the transport disappears. MIDIHub can then provide a unified overview and
basic persistent routes between those endpoints without requiring applications
or users to understand AppleMIDI, GATT, USB binding, or other transport details.

During incubation, CAMD client nodes remain useful for Network MIDI testing. The
native Network MIDI target, however, is a CAMD MIDI device interface: sockets,
discovery, peer/session state and any internal task remain behind that device
boundary. Current CAMD devices have a fixed `NPorts` after initialization, so
peer-to-port mapping must be defined explicitly unless CAMD later gains dynamic
port registration. The portable codecs remain independent of that native
registration mechanism.

## CAMD lifecycle

CAMD scans `DEVS:Midi` when it starts. With
[the rescan patch](../patches/aros-camd-rescan.patch) `RethinkCAMD()` loads
drivers added later, and a new driver's ports join clusters of the same name
that clients made while waiting for it. `camdusbmidi.class` calls it after
writing a device's driver. Drivers are never unloaded: a USB device's ports
stay, and carry MIDI again when it is plugged back in
([the lifecycle patch](../patches/aros-usb-midi-lifecycle.patch)). See
[CAMD improvements](camd-improvements.md) for the details and what remains.

The Network MIDI device must keep its exposed CAMD ports stable while peers
appear, disappear, and reconnect. Dynamic network identity is therefore an
internal device-lifecycle concern rather than a reason to expose the transport
as a standalone service.

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

`StartClusterNotify()` can be used to react when clusters are added or removed.
A saved route can therefore remain in a waiting state while an endpoint is
absent and become active when that endpoint appears.

See [midihub-architecture.md](midihub-architecture.md) for runtime ownership,
[preferences.md](preferences.md) for the user-facing model, and
[camd-improvements.md](camd-improvements.md) for changes proposed to CAMD
itself.

## Implementation sequence

1. Keep endpoint names and directions clear to CAMD clients across network,
   BLE, USB, serial, and synth.
2. Validate the `MIDIHub Synth` receive port with a native CAMD sender and
   live AHI playback.
3. Validate USB and BLE with physical devices, including binding, message
   transfer, SysEx, disconnection, and reconnection.
4. Implement enumeration of CAMD-visible endpoints for the MIDIHub overview.
5. Implement basic persistent source-to-destination routes and reconnection.
6. Add Profiles after route persistence and the current Network MIDI/Synth
   settings have stable storage contracts.
7. Extend CAMD driver removal only where hardware drivers require it.
