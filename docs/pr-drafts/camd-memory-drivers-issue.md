**Title:** camd: let a class hand CAMD its driver from memory, as AHI allows

Hi Kal, a question about one detail of the driver model you asked for in
#1507, not about the model itself.

btmidi.class is now a proper CAMD driver: `MidiDeviceData` with `Flags=1`,
`OpenPort()`/`ClosePort()`, bytes fed to CAMD's parser, ports `btmidi.N`.
The part I would like to discuss is only that the driver has to exist as a
**file** in `DEVS:Midi`, because `OpenMidiDevice()` can only find a driver
by `LoadSeg()`-ing one.

AHI does not need that. It opens a driver with `OpenLibrary("<name>.audio")`,
so a driver already in memory is found by name; with `AHI_AddAudioMode()` a
class can publish modes at run time. usbaudio.class does that, and #1727
goes further: btaudio.class adds a resident `btaudio.audio` and installs
nothing in `DEVS:AHI` or `DEVS:AudioModes`.

What the file costs a device class today:

- the driver exists twice, embedded in the class and copied to
  `DEVS:Midi`, and the copy has to be rewritten when the class changes;
- embedding it is fragile: the m68k `ld -r --format binary` failure you
  fixed in 58e427774c, the driver name patched into the binary for USB,
  the prebuilt 2006 `CAMDDriver.c`;
- it needs a writable `SYS:` and DOS when the class starts;
- CAMD only scanned `DEVS:Midi` at start (#1732 adds a DOS notification,
  where the filesystem supports it);
- USB writes one file per device, left behind when the device goes.

A minimal counterpart in camd.library would be two calls, say
`AddMidiDevice(struct MidiDeviceData *)` and `RemMidiDevice()`: CAMD does
what it does after `LoadSeg()` today (Init, ports, the `<name>.in.N` and
`.out.N` clusters, `OpenPort()` with its transmit and receive functions)
without the file. The driver stays a driver in every respect you asked
for. The open points are the ones btaudio faces too: the class must stay
loaded while CAMD uses the driver, and what clients linked to its ports
see when it is removed. With an older camd.library a class would still
write the file.

Would you consider that, or is there a reason the file itself matters for
CAMD? I would rather ask before writing anything.
