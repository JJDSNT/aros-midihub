# BLE MIDI transport

BLE MIDI uses the native AROS `btmidi.class`, merged in
[AROS PR #1507](https://github.com/aros-development-team/AROS/pull/1507).
The source is `rom/bluetooth/classes/btmidi` in AROS. The obsolete client
class, `MIDIHubBLE` manual bridge and duplicate packet codec were removed
from this repository on 2026-10-22 (backlog L11).

## Build and installation

Build `kernel-bluetooth-classes-btmidi` in a current AROS tree. The
`contrib-aros-midihub` package depends on that native target; it does not
install a replacement class. An older AROS checkout without #1507 must be
updated before building the whole package.

The class embeds a CAMD driver and writes it to `DEVS:Midi/btmidi` when it
starts. CAMD loads the driver when it scans that directory. On systems
without rescan support, restart CAMD or reboot if it was already loaded
before the driver was installed. Rebuild test images from the native target
so an old contrib-installed class is not left in `Classes/Bluetooth`.

## Ports and roles

| Role | CAMD input | CAMD output |
|---|---|---|
| AROS as peripheral; a phone or computer connects to it | `btmidi.in.0` | `btmidi.out.0` |
| AROS as central; a registered BLE MIDI device binds to the class | `btmidi.in.N` | `btmidi.out.N`, N = 1–15 |

Bluetooth Preferences controls the radio, pairing, trust and loaded classes.
The `btgatt.class` settings enable advertising and the BLE MIDI service.
Open the `btmidi.class` window from the Classes page for the port list,
device bindings, driver state and activity counters.

For the central role, register and connect to the peripheral in Bluetooth
Preferences. Once its services are enumerated, the class binds and assigns
the first free port. Use the class window to find that port. A port with no
bound device cannot be opened. The number can change when bindings change.

For the peripheral role, enable advertising and the MIDI service, connect
from the phone's Bluetooth MIDI panel, and attach a CAMD application to
port 0. Several centrals share that port's byte stream; simultaneous SysEx
transfers have limitations recorded in the backlog.

MIDIHub Preferences recognises `btmidi.in.N` and `btmidi.out.N`. It shows
the offered/stopped service for port 0 and labels other registered ports
`Driver port`: the merged class exposes `BCBA_Device`, and that device
already exposes `BDA_Name` and `BDA_IsConnected`, but it does not expose the
binding's MIDI port. The review of
[AROS #1789](https://github.com/aros-development-team/AROS/pull/1789)
rejected a MIDI-specific Bluetooth binding attribute: MIDI applications are
expected to find ports through CAMD. Until an application-driven CAMD naming
or identity proposal is accepted, this UI does not claim that a particular
device is connected. Use the class window for the port mapping.
Saved `BLE MIDI In` / `BLE MIDI Out` routes migrate to port 0; old
`<device> In` / `<device> Out` routes require manual selection of the new
port. There is no private BMID configuration chunk or editable cluster name.

## Ownership

The Bluetooth stack owns ATT/GATT, service records, notifications,
advertising, pairing and link security. The class owns BLE MIDI framing
and the adapter to its CAMD driver. CAMD parses the MIDI byte stream.
MIDIHub consumes the resulting endpoints and supplies routing and synth UI.
The application-side `camd_bridge.c` remains for its other consumers; the
upstream BLE class does not use it.

## Host tests

The tests compile the actual AROS class codec, including the active
[codec follow-up #1735](https://github.com/aros-development-team/AROS/pull/1735),
rather than maintaining a second implementation. Set the source explicitly:

```sh
make test BTMIDI_CLASS=/path/to/AROS/rom/bluetooth/classes/btmidi
make fuzz BTMIDI_CLASS=/path/to/AROS/rom/bluetooth/classes/btmidi
```

Without an override, the Makefile selects the first existing directory:
`build/btmidi-codec/rom/bluetooth/classes/btmidi`,
`~/AROS-btmidi/rom/bluetooth/classes/btmidi`, then
`~/AROS/rom/bluetooth/classes/btmidi`. A fresh companion checkout therefore
needs an AROS checkout as well. Use the proposal revision for regression
tests of fixes that have not merged yet. `make test-blemidi-class` runs only
the codec vectors. The tests do not download source or modify that checkout.

## Validation and remaining work

On 2026-10-22, host tests and 100,000 fuzz rounds with ASan/UBSan passed
against #1735 revision `3918469bab111a3d633cc6261c654a2ebf6ae918`.
After the review, #1735 (revision `a7fe624994`, rebased on master) carries
the codec test itself as `rom/bluetooth/stack/tests/btmidi`, run by
`Makefile.host`, and bumps the class to 45.2.
The native class and embedded driver built for x64 and m68k using the
existing SDKs. MIDIHub Preferences, router, self-test and network service
also built for both architectures after removing the duplicate codec.

These are build and host-test results. The personal AROS build source is
older than #1507 and has unrelated local changes; the class was built from
the separate proposal checkout. A complete package build from a current,
clean upstream tree and real-radio validation remain separate checks.
The historical Pi 3/iPhone result used the retired client class and does
not validate the driver's lifecycle or current Preferences integration.

Follow [the Pi 3 test procedure](ble-midi-pi3-test.md) for hardware testing.
Remaining improvements and their dependencies are in the
[BLE backlog](ble-midi-backlog.md). L10's class-side port attribute is now
withdrawn after review; L10 again requires a concrete consumer and an audit of
the public CAMD naming/identity interfaces. This does not prevent using the
upstream class or keeping the obsolete local copy retired.
