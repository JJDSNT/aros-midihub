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

MIDIHub currently uses CAMD client nodes for the network and BLE programs.
This provides standard CAMD clusters and already permits AROS applications to
use those transports. It does not give them USB's `DEVS:Midi` driver lifecycle
or automatic binding. The SoundFont programs currently render test notes via
AHI and do not expose a CAMD endpoint.

## Target architecture

| Endpoint | Discovery or configuration | CAMD-facing component | Transport or output |
| --- | --- | --- | --- |
| USB MIDI | Poseidon class binding | Existing `DEVS:Midi` driver | Poseidon USB class |
| RTP-MIDI / AppleMIDI | Network preferences, Bonjour, session negotiation | MIDIHub CAMD port | `bsdsocket.library` |
| BLE MIDI | Bluetooth service discovery and binding | MIDIHub CAMD port | `bluetooth.library` GATT |
| Serial / DIN | Configured AROS serial device and port | MIDIHub CAMD port | AROS serial API |
| Software synthesizer | SoundFont and AHI preferences | MIDIHub CAMD receive port | TinySoundFont to AHI |

For each MIDIHub endpoint, the CAMD-facing component should own a stable port
name, handle MIDI bytes and SysEx in both directions where applicable, and
release resources when the transport disappears. The synthesizer has only a
receive port: it consumes CAMD MIDI messages and produces PCM through AHI.
Network discovery and BLE binding should update the endpoint state without
requiring applications to understand AppleMIDI or GATT.

The USB driver's `MidiDeviceData` and port callbacks are the preferred model
for installed MIDIHub ports. A transport worker can own sockets, GATT channels,
or synthesis state while the CAMD driver presents those ports to applications.
The worker-to-driver message channel, queue bounds, ownership, and shutdown
sequence need to be designed and verified before replacing the working
application bridges. The portable MIDI codecs and synth can remain unchanged.

## CAMD lifecycle constraint

The current AROS CAMD implementation scans `DEVS:Midi` during `InitCamd()`.
It does not watch the directory for new drivers. Its existing public
`RethinkCAMD()` entry is a stub. Poseidon writes a USB driver when a device
binds, so its ports depend on CAMD being initialized after that file exists.
A dynamically discovered BLE or network peer cannot simply write a new driver
file and expect an already open CAMD library to expose it.

[The CAMD rescan patch](../patches/aros-camd-rescan.patch) implements
`RethinkCAMD()` as a serialized scan that adds drivers absent from the loaded
list. A service that installs a new driver can call it without reopening CAMD.
The patch does not unload removed drivers or detach live CAMD clients; that
requires a separate safe removal design and runtime tests.

The first driver-based MIDIHub package should install stable virtual ports
before CAMD initializes. Dynamic peers can attach to those ports through a
worker service. If individual ports per peer are needed, CAMD must gain a
safe driver removal or dynamic port registration path; that change belongs in
an AROS patch with tests for open clients, device removal, and reconnection.
Avoid making each connection create a persistent driver file with a stale
device address or IP address.

## Implementation sequence

1. Specify the installed port names and direction from the user's point of
   view. Use the same conventions for USB, network, BLE, serial, and synth.
2. Extract a bounded CAMD byte and SysEx exchange service from the existing
   network and BLE client bridges. Confirm its behavior with native CAMD
   clients on Linux-hosted AROS.
3. Add one static `DEVS:Midi` driver for a MIDIHub virtual endpoint and test
   CAMD's load, open, close, transmit, receive, and unload callbacks. Keep
   transport workers outside the driver binary.
4. Move network and BLE onto that endpoint, then add the synthesizer receive
   port and SoundFont/AHI preferences. Serial can use the same contract when
   a serial backend is available.
5. Extend CAMD for dynamic driver or port registration only if stable ports
   cannot meet multi-peer routing requirements. Test hot discovery and removal
   on a target with the relevant physical transport.

The current `MIDIHub` and `MIDIHubBLE` programs remain useful integration
probes until the driver-based ports pass those tests. USB physical I/O and
BLE physical I/O still require devices and AROS targets that expose them.
