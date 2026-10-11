# BLE MIDI as a CAMD driver: what it cost and what is owed

**Written:** 2026-10-11, after `btmidi.class` was rewritten from a CAMD client
into a CAMD driver for AROS PR #1507 (commits `a45711131d`, `db7145795c` on
`btmidi-class-v2`; worktree `~/AROS-btmidi`).
**Purpose:** a list to come back to. Each entry says what was lost or left
weak, what it affects, and how it could be fixed. Delete an entry when it is
fixed; when the list is empty, delete the file.

Background: [BLE MIDI](ble-midi.md), [the review](upstream-prs.md),
[what the driver model cannot say](camd-integration.md), and the amendment to
[the architecture decision](camd-endpoint-architecture-decision.md).

**How far each statement was checked.** *Run* means it was seen on hosted
AROS with this repository's CAMD patches. *Read* means it follows from the
source and was not executed. Nothing here has run on a radio.

**Where a fix can live.**

- *class*: in `btmidi.class` alone, as a further commit to the PR or a later
  one. Needs nothing from upstream CAMD.
- *extension*: needs the driver-side block (backlog B29 to B32), which waits
  for the maintainer's answer in the PR.
- *camd*: in `camd.library`, independent of the extension.
- *midihub*: in this repository's own programs.

## Part 1: lost against the client version

The client version made one MidiNode with two clusters per role and per bound
device. These are things it did that the driver version does not.

| # | What was lost | Effect | Fix | Where | Checked |
|---|---|---|---|---|---|
| L01 | **A route follows the device.** A bound device's clusters carried its name. Now it takes the first free port from 1, so its number depends on which other devices are bound. | Remove one keyboard and another may move to its port; a saved route then points at the wrong device, silently. This is the loss that matters most in use. | Keep an address-to-port map in the class configuration (`btGetClsCfg`), so a device gets its old port back and a new device never takes a remembered one while free ports remain. The real answer is identity through the extension (B29). | class now, extension later | read |
| L02 | **Readable, editable names.** `BLE MIDI In`, `<device> In`; now `btmidi.in.<n>`. The device's name is only in the class's window. | A user choosing a port in any CAMD program sees a number. | A name per port through the extension (B31). Until then programs can only ask Bluetooth: MIDIHub.prefs could show the name beside the port (L10). | extension, midihub | read |
| L03 | **Presence in the CAMD graph.** A client's clusters had a participant only while its task lived, which is what `MIDI_PartSignal`, `MidiLinkConnected()` and the version 42 ClusterWatch report. A driver's ports are attached to all 16 pairs of clusters from the moment it is loaded. | MIDIHub's router cannot hold a BLE route "waiting" and wake it when the device returns: the port always looks present. All 16 endpoints are published as available. | Presence per port through the extension (B30). No class-only fix: a driver cannot detach from its clusters. | extension | read; the 16 endpoints were seen in a run |
| L04 | **Linking before the device exists.** A program could link to a cluster by name and wait for the class to join it. Now `OpenPort()` fails for a port no device is bound to, and so does the link. | A program has to try again, and nothing tells it when. | Either accept an open on an unbound port and attach the binding when it comes (the adapter already survives either side going first), or presence through the extension. The maintainer asked for the failure, so the first needs his agreement. | class, with upstream's agreement | run: a link to `btmidi.out.5` returned NULL |
| L05 | **Available as soon as the class starts.** An unpatched `camd.library` scans `DEVS:Midi` only when it is loaded. | On a first installation with CAMD already in memory, BLE MIDI appears only after CAMD is reloaded, in practice a reboot. With this repository's rescan patch nothing changes for `btmidi`, because it does not call `RethinkCAMD()`: it may not open `camd.library`. | A trigger for the rescan that does not make the class open CAMD: CAMD watching `DEVS:Midi` with a DOS notification, or a rescan when a client names a driver CAMD does not know. See W03. | camd | read |
| L06 | **More than 15 devices.** `NPorts` is 16; a 16th bound device gets no port. | The binding is refused without a word to the user. | Say so in the class's window and the Bluetooth log; raise `NPorts` if it ever matters. `NPorts` is a `UBYTE`. | class | read |
| L07 | **Several centrals sending SysEx at once.** Each central had its own reassembly, up to four. CAMD parses one byte stream per port, so only one SysEx at a time reaches it: a second is left out and counted as an error, and a message from another central that interrupts a SysEx ends it. | Rare: two phones dumping patches to AROS at the same moment. Channel messages from several centrals still interleave correctly. | Reassemble a second central's SysEx in the class and feed it when the first has ended. That brings a SysEx buffer back, which the review asked to drop, so only if it turns out to matter. | class | read |
| L08 | **Whole messages or nothing.** A malformed packet was dropped whole, and a SysEx longer than 4096 bytes was refused by the class. Now bytes go to CAMD as they are decoded: what came before the error stays, and the SysEx limit is CAMD's. | A broken packet can leave CAMD's parser with a partial message, which the next status byte clears. | Decode a packet into the buffer first and deliver it only if the whole packet is valid. Cheap. | class | read |
| L09 | **No disk needed.** The class has to write `DEVS:Midi/btmidi`, and once CAMD has loaded the driver the class stays in memory, since CAMD never unloads a driver. | No BLE MIDI through CAMD on a read-only `DEVS:`. The class cannot be expunged or replaced without a reboot once CAMD has run. | Ship the driver in the system image as well, so writing is only an update. Unloading is CAMD's decision (`camd-improvements.md`, section 1.3). | build, camd | read |
| L10 | **MIDIHub.prefs knows the ports.** It read the peripheral role's names from the class's configuration chunk and derived a bound device's from its name. There is no chunk now. | The Overview does not mark the ports as BLE MIDI, and saved routes that name `BLE MIDI In` no longer resolve. | Recognise `btmidi.in.<n>`/`btmidi.out.<n>`; get device and port from the class (it has no public query for that yet: a `btcGetAttrsA` tag on a binding would do); migrate saved routes. | midihub, class | read |

What the driver version gained, so the balance is on record: BLE MIDI is a
device to anything that enumerates CAMD drivers; CAMD does the parsing; the
endpoint registry publishes the ports with stored IDs without any change; a
packet takes as many whole messages as are waiting; the class no longer
depends on `camd.library`.

## Part 2: weak points in the code sent upstream

Not losses: things the driver version does that should be better.

| # | Weak point | Effect | Fix | Where | Checked |
|---|---|---|---|---|---|
| W01 | **The adapter has no version.** `struct BTMidiAdapter` is shared by the class and the driver stub, and neither checks that the other was built with the same layout. The class rewrites the file in `DEVS:Midi` when it differs, but a stub CAMD has already loaded stays in use until CAMD is reloaded. | Harmless today. Dangerous the day the struct changes, which the extension will do. | A version (or size) field first in the struct after `ba_ActivateFunc`, checked in `btmidiCAMDOpenPort()`; refuse a mismatch. Do it before the merge: it is cheap now and an ABI break later. | class | read |
| W02 | **Loss on input is invisible to the class.** CAMD's receive function returns nothing. With this repository's receive-ring patch a full ring refuses bytes and reports `CMEF_RecvOverflow` to CAMD clients; the class's error counter does not move. | The status window can show no errors while input was lost. | Nothing in the class. A timed, acknowledged receive belongs to the extension (B32). | extension | read |
| W03 | **`camdusbmidi.class`, as patched here, opens `camd.library`** to call `RethinkCAMD()` after writing a driver (`aros-usb-midi-lifecycle.patch`). The review of `btmidi.class` says a class must not open CAMD: the dependency runs the other way. | That patch would meet the same objection upstream. | The same trigger as L05, used by both classes. Settle it before sending the USB patches. | camd | read |
| W04 | **Two ways to name a device's port.** The patched USB class writes one driver file per device, with the device's ID as the driver's name, so path-bound identity follows the device. `btmidi` is one driver with a pool of ports, as the review asked. | The same question answered twice; the registry's identity for a USB device is stable and for a BLE device is not. | Decide once, in the sketch of the extension: per-port identity evidence makes the per-device files unnecessary. | extension | read |
| W05 | **Soft interrupt and `Disable()`.** Closing a port relies on a soft interrupt not running while interrupts are off, and on its having finished before the task goes on. The USB class relies on the same. | Fine on one CPU. Not examined for SMP x86-64. | Review with someone who knows AROS's SMP rules; a per-port spinlock if needed. | class | read |
| W06 | **Register arguments on m68k.** The stub declares CAMD's entry points with the registers of `midi/camddevices.h` and calls CAMD's functions through `AROS_UFC`, which the USB stub does not. | Never built or run for m68k. | Build for `amiga-m68k` and run the hosted test's equivalent there. | test | not checked |

## Part 3: not yet tested

| # | What | How |
|---|---|---|
| T01 | The driver version on a radio, peripheral role | [The Pi 3 test](ble-midi-pi3-test.md), section "Which class is under test". Owed to the PR. |
| T02 | The central role with a real BLE MIDI peripheral | Needs such a device. Never done, for either version. |
| T03 | The decoder inside AROS | The hosted run fed the driver's receive hook directly; the packet decoder and the per-central rules ran on the host only. T01 covers it. |
| T04 | An unpatched `camd.library` | The hosted tree has this repository's CAMD patches. Build upstream's `workbench-libs-camd` into a copy and rerun the test. |
| T05 | m68k | See W06. |
| T06 | A port that outlives its binding, and a binding that comes back | Bind, open the port, release the binding while a client sends, bind again: the client's data is dropped meanwhile and flows again. Read, never run. |

## Suggested order

1. T01, because the PR promises it.
2. W01 and L08, small and safe, as one more commit to the PR.
3. L01, the address-to-port map, with L06's message. Ask in the PR first
   whether he wants it there or afterwards.
4. L10, so MIDIHub works with the upstream class, then remove the copy in
   `ports/aros/btmidi`.
5. L05 and W03 together: one rescan trigger for both classes.
6. L02, L03, W02 and W04 with the extension, if upstream is open to it.
7. L04 and L07 only if use shows they matter.
