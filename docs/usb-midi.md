# USB MIDI validation

## Current result

The upstream AROS Poseidon `camdusbmidi.class` recognizes an Audio/MIDI
interface and creates a CAMD driver under `DEVS:Midi`. CAMD loads drivers
from that directory and exposes their ports as MIDI clusters. The class is
included in the common USB classes MetaMake target for supported CPU targets.
MIDIHub can use these CAMD clusters without a USB-specific transport.
This USB class and driver pair is the design reference for MIDIHub's other
endpoints; see [the CAMD integration design](camd-integration.md).

This is **not yet a physical USB MIDI pass**. The Linux hosted build in this
workspace has `DEVS:Midi/hostmidi` but no `poseidon.library` or
`camdusbmidi.class`. Its host does not expose `/dev/bus/usb` or
`/sys/bus/usb/devices`. There is no attached USB MIDI device here. A
physical test needs an AROS target with a working Poseidon host controller
and a class-compliant USB MIDI 1.0 device.

The source audit identified three concrete failures in the upstream class:

- Receive passes a 4-byte USB event packet to the CAMD callback as though it
  were a length-prefixed MIDI buffer. The callback reads the first four bytes
  as a length, then reads beyond the packet. The patch constructs the expected
  length and MIDI bytes.
- Transmit uses `TXBufSize` instead of `TXBufSize - 1` to wrap a ring index.
  It also queues CAMD's `0x100` no-data sentinel as a MIDI byte. The patch
  checks capacity before consuming data, wraps correctly, and stops on the
  sentinel.
- `OpenPort` dereferences a null adapter when no USB MIDI binding is
  available. The patch returns a failed open to CAMD instead.

The fixes are in [the AROS USB MIDI patch](../patches/aros-usb-midi-camd.patch).

Reading the class further showed why USB MIDI could not have worked outside
m68k, and what unplugging did:

- The class writes `DEVS:Midi/<device>`, but the driver inside kept the
  name `poseidonusb`. CAMD only loads a driver whose name is its file name,
  so the driver was always rejected. (The m68k build patches the name in.)
- CAMD only read `DEVS:Midi` when it started, so a device plugged in later
  needed CAMD to be expunged first.
- All devices shared one set of 16 ports, and `OpenPort()` used the first
  binding: a second device took over the first one's ports.
- After unplugging, the first message sent to an open port signalled the
  freed task's message port.

[The lifecycle patch](../patches/aros-usb-midi-lifecycle.patch) writes the
device's name into the driver, calls `RethinkCAMD()` after writing it, gives
each device its own ports, and keeps a device's ports across unplugging: they
open while it is away, drop what they are sent, and carry MIDI again when it
returns. Both patches build for `raspi-aarch64`; neither has run with a USB
MIDI device yet.

## Physical test

1. Apply the patch in a disposable AROS checkout or build tree, then build
   Poseidon, `camdusbmidi.class`, and CAMD for the chosen target.
2. Boot with the USB controller registered in Poseidon. Plug in one
   class-compliant USB MIDI 1.0 device and confirm that Poseidon binds
   `camdusbmidi.class` and creates `DEVS:Midi/<device>`.
3. Confirm `<device>.in.0` and `<device>.out.0` clusters are present, with
   CAMD already running before the device was plugged in (MIDIHub.prefs
   open, for example). `MIDIHUB:C/MIDIHubCAMDProbe --rethink` lists the
   clusters.
4. Send Note On/Off and Program Change from the device to a CAMD monitor,
   then send the same messages from CAMD to the device. Verify byte values
   and that no extra messages appear after the transmit queue empties.
5. With `MIDIHubCAMDProbe --monitor <device>.in.0` running, unplug the
   device, send to `<device>.out.0` (nothing must hang or crash), plug it
   back in, and play: the same monitor receives again.
6. Exercise SysEx in both directions. Record any
   failure separately: the existing parser still has SysEx TODOs and uses
   shared adapter state for multiple USB bindings.

The AROS hosted executable is useful for CAMD and MIDIHub network tests, but
it does not provide a USB host controller path in this build.
