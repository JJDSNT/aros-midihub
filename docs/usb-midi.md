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

## AROS issue #92 — bounded CAMD USB transmit work (agent regression contract)

Reference: https://github.com/aros-development-team/AROS/issues/92
Related implementation/review: `rom/usb/classes/camdmidi/camd/poseidonusb.c`,
`GetFromCAMD()`, `nParseMidiOut()`, and
https://github.com/aros-development-team/AROS/pull/1733 .

**Interpretation:** The historical `TODO: Not sure what to do here??` on
`while (sent < 100)` is a question about bounding work performed by a
transmit soft interrupt, **not** a requirement to remove the limit.
The intended invariant is to pull no more than 100 MIDI bytes per
`GetFromCAMD()` invocation, while ensuring queued data makes progress after
the USB consumer frees capacity. A bounded pull without re-arming can stall
`PutSysEx()` after 100 bytes. Do not infer that a changed TODO comment alone
proves the end-to-end issue fixed.

**Existing coverage (do not duplicate):** `tools/review-usbmidi-tx.py`
compiles the real producer/parser function bodies with host shims and
ASan/UBSan, using `tests/review/usbmidi_tx_cases.c`. Cases include
99/100/101/4096/65536-byte SysEx, split short messages, full-ring
backpressure, two ports and detached draining. The PR draft also documents
native hosted CAMD + simulated USB-sink `PutSysEx()` tests at
101/4096/65536 bytes. These are reported results, not a physical USB pass.
The current harness checks completed output and bounded overall progress;
it **does not explicitly assert the per-invocation 100-byte maximum**.

**Agent action — extend the existing regression harness, not a new test
framework or standalone upstream PR:**

1. Tag the regression as `AROS-ISSUE-92` in test names/comments and retain
   the 99, 100, 101 and long-SysEx boundary cases.
2. Instrument the transmit callback to count bytes consumed **per call**
   to `GetFromCAMD()`, asserting at most 100 for each invocation; reset
   the counter for every invocation, including manually primed full-ring
   calls. Do not count the `0x100` no-data sentinel as a MIDI byte.
3. Assert that a 101-byte and long SysEx resume after the first batch
   when the USB consumer makes progress, with exact byte-for-byte output,
   no duplication, and no permanent pending/wakeup loop.
4. Verify full-ring backpressure does not consume/drop the next source
   byte and resumes after drain. Check empty and detached states clear
   pending work and do not signal a stale task.
5. Add a regression that would fail if the 100-byte cap were removed
   **or** if the continuation/re-arm path were removed. Prefer source-level
   behavior assertions over searching for a particular comment.
6. Run the existing host ASan/UBSan harness and, where available, the
   native hosted CAMD/soft-interrupt probe. Record exact commands,
   outcomes and any missing environment. Physical USB MIDI transmission,
   hot-unplug/replug and simultaneous real devices remain separately
   pending; do not mark those as passed.

**Upstream gate:** Compare the current AROS master and the status/diff of
#1733 before declaring #92 resolved upstream or preparing a change.
Keep issue tracking here in the repository documentation so the Midihub
agent sees it; do not create an unrelated GitHub issue. CAMD v42 API work
does not by itself establish this USB transport regression.

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
