# Live SoundFont instrument

`MIDIHubSynth` creates one CAMD receiver named `MIDIHub Synth`. AROS MIDI
applications can send channel messages to that port. The program loads a
user-selected SF2 bank with TinySoundFont and continuously writes mono PCM to
`ahi.device` unit 0. The CAMD port exists while the program is running; it
does not require a `DEVS:Midi` driver or the CAMD rescan patch.

## Build and try it

Build the optional `contrib-aros-midihub-synth` MetaMake target. Supply an SF2
bank on the command line, or write its path as the only line of
`ENV:MidiHub/SoundFont` or `ENVARC:MidiHub/SoundFont`. The command-line path
takes precedence; without one, `ENV:` takes precedence over `ENVARC:`. A
future Preferences page can edit the same file. The separate
`contrib-aros-midihub-soundfonttest` target installs the GeneralUser GS test
bank under `MIDIHUB:SoundFonts/`.

```text
MIDIHUB:C/MIDIHubSynth MIDIHUB:SoundFonts/GeneralUser-GS.sf2
MIDIHUB:C/MIDIHubCAMDProbe --synth
```

Run the commands in separate AROS shells. The probe sends middle C for about
half a second. To use another application, connect its CAMD sender to
`MIDIHub Synth`. Press Ctrl-C in the synthesizer shell to close the port.
When a SoundFont path is configured, start the service with
`MIDIHUB:C/MIDIHubSynth` and no argument.

The service accepts the channel messages currently supported by
`mh_synth_send`: Note On/Off, Control Change, Program Change, and pitch bend.
Channel pressure, polyphonic pressure, and synthesizer SysEx are still
unsupported. CAMD SysEx is consumed and discarded. MIDI messages are applied
when they arrive; the service does not yet schedule them by CAMD timestamp.

AHI playback uses two 2048-frame buffers at 44.1 kHz. This is a first live
path, not a latency or underrun guarantee. The source compiled and linked
with the existing AROS x64 and m68k GCC toolchains, but this service has not
yet been run inside AROS with AHI audio. GeneralUser GS has known timbre
differences because
TinySoundFont does not implement SoundFont modulators. See
[the SoundFont investigation](soundfonts.md).
