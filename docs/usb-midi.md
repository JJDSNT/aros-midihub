# USB MIDI validation

## Current result

The upstream AROS Poseidon `camdusbmidi.class` recognizes an Audio/MIDI
interface and creates a CAMD driver under `DEVS:Midi`. CAMD loads drivers
from that directory and exposes their ports as MIDI clusters. The class is
included in the common USB classes MetaMake target for supported CPU targets.
MIDIHub can use these CAMD clusters without a USB-specific transport.

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
`git -C ~/AROS apply --check` passed against upstream commit
`13c7f81274825dd9bca047fc7caebfa21576a163`. A class build and runtime
test remain pending.

## Physical test

1. Apply the patch in a disposable AROS checkout or build tree, then build
   Poseidon, `camdusbmidi.class`, and CAMD for the chosen target.
2. Boot with the USB controller registered in Poseidon. Plug in one
   class-compliant USB MIDI 1.0 device and confirm that Poseidon binds
   `camdusbmidi.class` and creates `DEVS:Midi/<device>`.
3. Open CAMD after the device is detected. Confirm
   `<device>.in.0` and `<device>.out.0` clusters are present.
4. Send Note On/Off and Program Change from the device to a CAMD monitor,
   then send the same messages from CAMD to the device. Verify byte values
   and that no extra messages appear after the transmit queue empties.
5. Exercise SysEx in both directions, hot unplug, and reconnect. Record any
   failure separately: the existing parser still has SysEx TODOs and uses
   shared adapter state for multiple USB bindings.

The AROS hosted executable is useful for CAMD and MIDIHub network tests, but
it does not provide a USB host controller path in this build.
