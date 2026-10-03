# Configuration and future Preferences application

On AROS, the network program reads `ENV:MidiHub/Network` and falls back to
`ENVARC:MidiHub/Network` if the first file does not exist. This is a UTF-8 text
file with one `name=value` option per line. Empty lines and lines beginning
with `#` or `;` are ignored. `MIDIHub --config path` selects a specific file,
which must exist. Positional arguments override the ports and address in the
file. For example:

```text
local_port=5004
peer_ip=192.168.1.20
peer_port=5004
session_name=AROS MIDIHub
```

`local_port` and `peer_port` are control ports from 1 through 65534; the next
port carries data. Omit both `peer_ip` and `peer_port` to receive invitations
without initiating a connection. Without a file, the program listens on port
5004. The session name can be at most 63 bytes. An invalid file prevents
startup and does not alter an already loaded configuration.

## Preferences

A future `MIDIHubPrefs` application should edit these same settings with
`Use` (write to `ENV:`), `Save` (write to both `ENV:` and `ENVARC:`), and
`Cancel`. The CLI and GUI would share behavior, while the service would not
depend on the GUI. The first page should show the session name, local port,
remote peer, and connection status. A test button could attempt a session and
show invitations, synchronization, and received messages. The service must
expose that state before this button can be implemented.

The future SoundFont page should select an `.sf2` file
with a file requester, check that the engine can open it, and allow gain,
bank, and default program settings. Audio configuration must use the common
AROS audio path without assuming a particular target or device. The engine
should validate a new SoundFont before replacing the one in use, so a failed
load does not interrupt currently playing notes.

The optional `MIDIHubSynth` service now reads a plain SoundFont path from
`ENV:MidiHub/SoundFont`, falling back to `ENVARC:MidiHub/SoundFont`; an
explicit command-line path overrides both. This is the initial storage
contract for the future SoundFont page. The service loads the path at startup;
live bank switching is not implemented yet.

The service also reads `ENV:MidiHub/Backend`, falling back to
`ENVARC:MidiHub/Backend`. The value is `tiny` or `fluid`; the CLI
`--backend` option overrides it. The default AROS package provides `tiny`.
Preferences should show `fluid` only when an external FluidSynth-enabled
build is installed, and should identify an unavailable backend before saving.

AHI Preferences already has a **Music unit**. Its AHI documentation defines
this as the default audio mode for applications using AHI's low-level API
(`AHI_NO_UNIT`); it does not configure MIDI or CAMD ports, instruments, or
SoundFonts. MIDIHub should use the existing AHI audio mode preferences for
its synthesizer output and keep SoundFont selection and MIDI routing in its
own configuration. `SoundFontPlay` and `MIDIHubSynth` use AHI's device API on
unit 0. Selecting the Music unit instead would require a separate low-level
AHI playback implementation and test. See the upstream AHI
[user guide](https://github.com/aros-development-team/AROS/blob/13c7f81274825dd9bca047fc7caebfa21576a163/workbench/devs/AHI/Docs/ahiusr.texinfo)
and [AHI Preferences implementation](https://github.com/aros-development-team/AROS/blob/13c7f81274825dd9bca047fc7caebfa21576a163/workbench/devs/AHI/AHI/support.c).

Preview should send Note On/Off through the same synthesizer and audio path
used by CAMD clients. A small virtual key or `Test` button, with selectable
note and velocity, is enough initially. This exercises the SoundFont, routing,
and audio together. `MIDIHubCAMDProbe --synth` already sends a fixed note
through this path. A GUI preview with selectable note and velocity remains to
be implemented after native audio playback is verified.

`MIDIHub` names the package. `RTP-MIDI` names the transport, while
`AppleMIDI` names session negotiation over that transport; together they form
one connection, not two CAMD ports. The network, BLE, and synth programs
create their virtual CAMD ports directly when started.

## Bluetooth MIDI ownership

The peripheral implementation should be installed as `btmidi.class` in
`SYS:Classes/Bluetooth`, following the existing HID, Serial, and PAN profile
classes. Bluetooth Preferences already delegates a selected binding's
configuration window to its class. Its generic UI should continue to manage
the adapter, registered devices, pairing, trust, and class loading.

The `btmidi.class` configuration window should provide the BLE MIDI switch,
advertised name, CAMD port names, and live connection/activity status. A
MIDIHub Preferences Bluetooth page can open or embed the same settings; it
must not keep a second configuration copy. SoundFont selection, synth backend,
preview keyboard, and MIDI routing remain MIDIHub settings because the same
features apply to USB, RTP-MIDI, virtual MIDI, and other transports.
