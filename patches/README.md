# AROS BLE MIDI remaining patch

Upstream AROS commits `8ff80b6627` and `37313d8aa0` now provide the generic
GATT server, local service records, notifications, legacy LE advertising,
128-bit UUID discovery, and the `btgatt.class` Preferences UI. The former
three-patch BLE foundation in this repository has therefore been retired.

`aros-ble-midi-upstream-gaps.patch` is based on upstream commit
`37313d8aa0` (October 3, 2026) and now contains only the BLE MIDI gaps that
remain:

- encode and send ATT Write Command (`0x52`) for Write Without Response;
- route `BTPR_GATTWRITENORSP` and write-only endpoints through that operation;
- expose the negotiated ATT payload size instead of a fixed 20-byte value;
- include enabled 128-bit GATT service UUIDs in legacy advertising, which is
  required for iOS to discover the BLE MIDI service;
- attach an immutable value snapshot to every service-write event, so a later
  ATT packet cannot overwrite MIDI data before the class consumes the event,
  and name the device that wrote it (`BENA_Device`), so a class can keep
  per-connection state;
- queue outgoing notification snapshots until each active radio consumes
  them, instead of coalescing a MIDI burst into the latest value. The queue is
  bounded at 256 snapshots; on sustained producer overrun it drops the oldest;
- accept peripheral-role SMP pairing, including Legacy Just Works and Secure
  Connections Just Works, Numeric Comparison, and Passkey Entry; answer
  controller LTK requests, distribute bonding keys, and reuse stored keys on
  reconnection;
- add the L2CAP LE Connection Parameter Update procedure. As the central the
  stack grants a peripheral's request through HCI LE Connection Update; as
  the peripheral it asks for the interval set with the new
  `BSA_LEConnInterval` stack attribute, within the limits iOS accepts.
  `btmidi.class` sets 15 ms, as the BLE MIDI specification asks.

It also fixes two generic SMP issues that affect the central role as well:

- the IO capability table selected Just Works instead of Passkey Entry when a
  KeyboardDisplay device met a DisplayOnly or DisplayYesNo device;
- an initiator rejected a responder's Secure Connections commitment that
  arrived before its own controller reported the DHKey. It now holds the
  commitment until the DHKey is ready.

Phase 3 key distribution follows the Core specification in both roles: the
responder sends its keys first, and the initiator answers once it has
received them.

The modified AROS files and their diff context are covered by the AROS Public
License 1.1; see [AROS-LICENSE](AROS-LICENSE). MIDIHub's own source remains
under its existing license. The patch advances `bluetooth.library` to 45.18
because it adds event data tags used by `btmidi.class`.

Apply it to the updated `~/AROS` checkout, without changing Bellatrix:

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-upstream-gaps.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-upstream-gaps.patch"
make -C ~/AROS/rom/bluetooth/stack -f Makefile.host test
```

The patch applies cleanly to the stated commit. The sanitizer-enabled host
suite passes `2530/2530` checks, including new ATT, GATT, and SMP responder
tests. The SMP tests also run an initiator against a responder, check the
full IO capability table, and check the Phase 3 order. The changed `bluetooth.library` sources and `btmidi.class` also pass an
m68k AROS syntax build.

`btmidi.class` uses the upstream service-record API. It registers the BLE MIDI
service with `btAddServiceRecord()`, receives incoming values through
`BEHMB_SERVICEWRITE`, and publishes outgoing packets with
`btSetServiceValue()`. `btgatt.class` owns radio advertising and the list of
enabled services in Bluetooth Preferences.

Peripheral-role pairing falls back to Legacy Just Works by default. With the
existing `btlesc` boot argument, capable controllers and peers negotiate Secure
Connections, including Numeric Comparison through the standard Bluetooth
pairing popup and Passkey Entry. OOB pairing remains unsupported.

## USB MIDI CAMD fix

`aros-usb-midi-camd.patch` still applies cleanly to upstream commit
`37313d8aa0`. It fixes
the Poseidon USB MIDI class's CAMD receive buffer format, transmit ring
handling, and failed port-open behavior. The modified AROS source is covered
by [AROS-LICENSE](AROS-LICENSE).

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-usb-midi-camd.patch"
git -C ~/AROS apply "$PWD/patches/aros-usb-midi-camd.patch"
```

The patch passed `git apply --check`. Runtime status and the hardware test
procedure are in [USB MIDI validation](../docs/usb-midi.md).

## CAMD driver rescan

`aros-camd-rescan.patch` still applies cleanly to upstream commit
`37313d8aa0`. It makes
the existing `RethinkCAMD()` entry rescan `DEVS:Midi` and load new driver files
once, even when CAMD is already open. It serializes scans separately from the
CAMD list lock so `LoadSeg` and driver initialization do not run while that
list is locked. It does not unload a driver whose file is removed.

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-camd-rescan.patch"
git -C ~/AROS apply "$PWD/patches/aros-camd-rescan.patch"
```

The patch passed `git apply --check`, and its changed C files compiled with
the AROS x64 GCC. A full CAMD build and native rescan test are pending. The
driver lifecycle and the remaining removal issue are documented in
[the CAMD integration design](../docs/camd-integration.md). The modified AROS
source is covered by [AROS-LICENSE](AROS-LICENSE).
