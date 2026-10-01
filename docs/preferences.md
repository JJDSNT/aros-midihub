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

Once synthesis is integrated, a SoundFont page should select an `.sf2` file
with a file requester, check that the engine can open it, and allow gain,
bank, and default program settings. Audio configuration must use the common
AROS audio path without assuming a particular target or device. The engine
should validate a new SoundFont before replacing the one in use, so a failed
load does not interrupt currently playing notes.

Preview should send Note On/Off through the same synthesizer and audio path
used by CAMD clients. A small virtual key or `Test` button, with selectable
note and velocity, is enough initially. This exercises the SoundFont, routing,
and audio together. The page and preview should be enabled only after the
synthesizer and audio output are integrated. The GeneralUser GS submodule and
WAV smoke test exist, but live audio preview does not.

`MIDIHub` names the package. `RTP-MIDI` names the transport, while
`AppleMIDI` names session negotiation over that transport; together they form
one connection, not two CAMD ports. If a CAMD driver is added to make ports
available at startup, `rtpmidi` would be an appropriate name. Communication
between that driver and the network service still needs to be designed and
tested inside AROS.
