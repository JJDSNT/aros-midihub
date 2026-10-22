A USB MIDI device never became usable through AROS's CAMD. The changes fix
what stood in the way, in the order it is met.

**1. Received messages, the transmit ring, a failed open.**
- A received message was passed to the driver by writing its length over
  the first byte of the USB event packet. The hook reads the length as a
  ULONG, so the message's own bytes became part of it and the bytes passed
  to CAMD were the ones after them. Each message now gets a length-prefixed
  buffer of its own.
- The transmit ring's position was masked with its size (4096) instead of
  size - 1, which took it back to 0 after every byte: nothing written was
  ever seen as queued. The full check now runs before a byte is taken.
- A failed `usbCAMDOpenPort()` made `OpenPort()` return a pointer computed
  from a NULL adapter.

**2. A driver per device, named after it; ports across unplugging.**
- The class wrote `DEVS:Midi/<device>` with "poseidonusb" inside, and
  camd.library only loads a driver whose name is its file name. The driver
  now has a marker before its name, and the class writes the device's name
  after it, replacing a file an older class wrote.
- Each device gets its own 16 ports; a second device used to take over the
  first one's.
- Ports outlive the binding: they open while the device is away, drop what
  they are sent while it is unplugged, and carry MIDI again when it comes
  back, without clients linking again.

**3. Let AROS's camd.library load and drive the driver.**
- `MidiDeviceData.Flags` was 0, the old format, which AROS's camd.library
  refuses. The driver already uses the new `OpenPort()` interface.
- `GetFromCAMD()`, the soft interrupt `ActivateXmit()` causes, was written
  as a hook taking the adapter third. A soft interrupt gets `is_Data`
  first; where these macros pass arguments by position the adapter was
  `SysBase`, and the first message sent crashed. It is now `AROS_INTH1`.

**4. Build for m68k too, with the driver compiled for each target.**
- The class was not built for m68k at all (its build file maps every CPU
  but m68k), although amiga-m68k builds Poseidon. Its m68k path would have
  installed `CAMDDriver.c`, a 2006 driver binary with `Flags` 0, which
  AROS's camd.library refuses, named by patching a fixed offset.
- Every target now installs the driver compiled from `camd/poseidonusb.c`,
  embedded as a C byte array as `btmidi.class` now does, since the m68k ld
  of binutils 2.32 refuses `ld -r --format binary`. `CAMDDriver.c` goes.

As in the review of #1507, the class does not open camd.library: CAMD
loads the driver and the driver opens the class. On current master CAMD
scans `DEVS:Midi` only when it starts, so a device plugged in later is seen
at the next start; #1732 makes CAMD load drivers added to `DEVS:Midi` by
itself, which covers this class and `btmidi.class` without either opening
camd.library.

### Validation, and what is missing

Hosted x86-64, no USB device: the generated driver file, with the device
name written after the marker as the class does, placed in `DEVS:Midi`.

| | master | this PR |
|---|---|---|
| CAMD loads the driver | refused (name, then Flags) | loaded |
| A client's link connects to the port | no | yes |
| Notes and SysEx sent with no device, twice | — | dropped, no hang, no crash |

The class, its driver and the embedded array build for x86-64 and
amiga-m68k (full module targets, link included), with no new warnings; the
m68k driver carries `Flags` 1 and the name marker. The two changed C files
also compile for raspi-aarch64.

**Hardware validation remains pending**: receiving and sending real MIDI,
unplugging and replugging, and two devices at once are untested.
Testing on real hardware would be very welcome.


### Long SysEx review follow-up (2026-10-20)

Addresses [the maintainer's review](https://github.com/aros-development-team/AROS/pull/1733#issuecomment-6290013208).
The bounded transmit soft interrupt records whether CAMD may still have data.
After USB consumption makes progress, the parser causes another bounded batch;
it also refills when a short message straddles the batch boundary. The empty
and detached paths clear the pending state. This preserves the bounded pull
and prevents `PutSysEx()` from stalling after its first hundred bytes.

The host regression harness compiles the actual producer/parser source with
ASan/UBSan: all 10 checks pass (7 failed before), including 99, 100, 101,
4096 and 65536-byte SysEx, a split short message, a full ring, two ports,
and detached draining. A separate native hosted AROS probe uses the real
CAMD library, generated driver and Exec soft interrupts with a simulated
USB sink: actual `PutSysEx()` calls for 101, 4096 and 65536 bytes return
and deliver every byte correctly. It finishes `Avail FLUSH`.
The complete USB class, driver and embedded array build and link for
x86-64 and amiga-m68k.

The USB sink is simulated. Physical MIDI I/O, unplug/replug and two
physical devices still need hardware validation.
