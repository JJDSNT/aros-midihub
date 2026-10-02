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

MIDIHub uses CAMD client nodes to create virtual ports for network, BLE, and
the SoundFont synthesizer. These are standard CAMD clusters: applications can
send to and receive from them without a `DEVS:Midi` driver. The USB driver's
value as a reference is its CAMD contract and its device lifecycle, not a
requirement that every software endpoint copy its binary driver mechanism.

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

The existing CAMD client approach is appropriate for the network, BLE, and
synth processes because each process already owns its sockets, GATT channels,
or audio stream. A `DEVS:Midi` driver is appropriate when AROS needs a native
device binding such as Poseidon's USB class. The portable codecs and synth
remain independent of either registration mechanism.

## CAMD lifecycle constraint

The current AROS CAMD implementation scans `DEVS:Midi` during `InitCamd()`.
It does not watch the directory for new drivers. Its existing public
`RethinkCAMD()` entry is a stub. Poseidon writes a USB driver when a device
binds, so its ports depend on CAMD being initialized after that file exists.
This affects newly installed `DEVS:Midi` drivers, including a USB device
bound after CAMD opens. MIDIHub's virtual ports are created through CAMD's
normal client API when their processes start, so they do not have this
limitation.

[The CAMD rescan patch](../patches/aros-camd-rescan.patch) implements
`RethinkCAMD()` as a serialized scan that adds drivers absent from the loaded
list. A service that installs a new driver can call it without reopening CAMD.
The patch does not unload removed drivers or detach live CAMD clients; that
requires a separate safe removal design and runtime tests.

Driver removal still needs a safe lifecycle before the rescan patch can be
used for fully dynamic hardware drivers. A transport service with virtual
CAMD ports can instead release its links when it exits. Peer-specific virtual
ports may be added through the same CAMD client API, with stable naming and
clear ownership; persistent files containing stale device addresses or IP
addresses are unnecessary.

## Implementation sequence

1. Keep port names and directions clear to CAMD clients across network, BLE,
   USB, serial, and synth. The name of the source or destination should be
   visible without knowing the transport's internal implementation.
2. Validate the new `MIDIHub Synth` receive port with a native CAMD sender
   and live AHI playback. Add SoundFont selection and preview using this same
   path.
3. Validate USB and BLE with physical devices, including binding, message
   transfer, SysEx, disconnection, and reconnection. The USB driver can use
   `RethinkCAMD()` after it is written if CAMD is already open.
4. Add peer-specific virtual ports only when multi-peer routing needs them.
   Keep network and BLE processes responsible for their own transport state.
5. Extend CAMD driver removal only for hardware drivers that need it, with
   tests for clients holding open links. Virtual MIDIHub ports can continue
   to use the existing CAMD client lifecycle.
