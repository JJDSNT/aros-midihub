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

## Synth backend

TinySoundFont is the default backend. Use `--backend tiny` or
`--backend fluid` to select explicitly. Without that option, the service reads
the one-line file `ENV:MidiHub/Backend`, then `ENVARC:MidiHub/Backend`, and
defaults to `tiny`. The configured value must be `tiny` or `fluid`.

The default AROS build includes TinySoundFont only. If `fluid` is selected
there, the service reports that the backend is unavailable. An opt-in build
with `MIDIHUB_ENABLE_FLUIDSYNTH` and an external FluidSynth library makes both
engines selectable in the same program. The host smoke test for that build is
`make test-synth-fluid`; it needs an external FluidSynth development package
or explicit `FLUIDSYNTH_CFLAGS` and `FLUIDSYNTH_LIBS`. No AROS FluidSynth port
is present in the current checkout, so FluidSynth has not been built or run
inside AROS. The CAMD port and AHI output code are shared between backends.

Both backends accept Note On/Off, Control Change, Program Change, and pitch
bend through `mh_synth_send`. FluidSynth additionally accepts channel
pressure, polyphonic pressure, and recognized synthesizer SysEx. CAMD supplies
complete SysEx messages to the service through its separate SysEx queue;
messages unsupported by the selected backend are consumed without affecting
playback. TinySoundFont does not handle pressure or SysEx. MIDI messages are
applied when they arrive; the service does not yet schedule them by CAMD
timestamp.

AHI playback uses two 2048-frame buffers at 44.1 kHz. This is a first live
path, not a latency or underrun guarantee. The source compiled and linked
with the existing AROS x64 and m68k GCC toolchains, but this service has not
yet been run inside AROS with AHI audio. GeneralUser GS has known timbre
differences because
TinySoundFont does not implement SoundFont modulators. See
[the SoundFont investigation](soundfonts.md).
